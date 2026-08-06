#include "queue.h"
#include "test_helpers.h"

#include <errno.h>
#include <pthread.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 传输1000个整数。
 *
 * 队列容量只有4，因此生产者和消费者会反复触发not_full、not_empty
 * 和环形下标回绕，比只传递几个元素更容易发现同步错误。
 */
#define CONCURRENT_TEST_ITEM_COUNT 1000U

/**
 * @brief 保存并发生产者线程的参数和运行结果。
 */
typedef struct {
    blocking_queue_t *queue; /**< 生产者和消费者共享的队列。 */
    size_t item_count;       /**< 生产者需要写入的元素数量。 */
    int error_code;          /**< 线程执行结果，0表示成功。 */
} producer_thread_context_t;

/**
 * @brief 连续产生整数并写入队列，完成后关闭队列。
 *
 * @param argument 指向producer_thread_context_t。
 *
 * @return 执行结果保存在上下文中，线程固定返回NULL。
 */
static void *producer_thread_main(void *argument)
{
    producer_thread_context_t *context = argument;

    for (size_t index = 0; index < context->item_count; index++) {
        const int error_code =
            blocking_queue_push(context->queue, (int)index);

        if (error_code != 0) {
            context->error_code = error_code;

            /*
             * 生产者异常退出前关闭队列，唤醒可能仍在等待数据的消费者。
             */
            (void)blocking_queue_close(context->queue);
            return NULL;
        }
    }

    /*
     * 完成全部数据后关闭队列。
     *
     * 消费者仍可以取完队列中已有的数据；队列取空后，pop返回
     * ECANCELED，消费者据此结束循环。
     */
    context->error_code =
        blocking_queue_close(context->queue);

    return NULL;
}

/**
 * @brief 验证一个生产者和一个消费者能够并发传输1000个整数。
 *
 * @return 数据数量、FIFO顺序和关闭状态全部正确时返回true。
 */
static bool test_concurrent_producer_consumer(void)
{
    blocking_queue_t queue;
    pthread_t producer_thread;

    producer_thread_context_t producer_context;

    size_t received_count = 0;
    bool fifo_order_valid = true;
    int consumer_error = 0;
    int error_code;

    if (!initialize_test_queue(
            &queue,
            "concurrent producer-consumer")) {
        return false;
    }

    producer_context = (producer_thread_context_t){
        .queue = &queue,
        .item_count = CONCURRENT_TEST_ITEM_COUNT,
        .error_code = 0
    };

    /*
     * 创建生产者线程。
     *
     * 第三个参数是线程入口函数；
     * 第四个参数是传给线程入口的上下文指针。
     */
    error_code = pthread_create(&producer_thread,
                                NULL,
                                producer_thread_main,
                                &producer_context);

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to create producer thread: %s\n",
                strerror(error_code));

        (void)destroy_test_queue(
            &queue,
            "concurrent producer-consumer");

        return false;
    }

    /*
     * 当前主测试线程充当消费者。
     *
     * pop会在队列为空时等待not_empty，所以不需要轮询，也不需要
     * sched_yield。
     */
    for (;;) {
        int actual_value;

        error_code =
            blocking_queue_pop(&queue, &actual_value);

        if (error_code == 0) {
            /*
             * 生产者按照0、1、2……的顺序写入，因此消费者也必须按照
             * 相同顺序读出。
             *
             * 即使发现顺序错误，也继续取空队列，避免生产者因为满队列
             * 无法结束。
             */
            if (actual_value != (int)received_count) {
                fifo_order_valid = false;
            }

            received_count++;
            continue;
        }

        if (error_code == ECANCELED) {
            /*
             * 队列已关闭且没有剩余数据，消费过程正常结束。
             */
            break;
        }

        /*
         * 其他错误都属于异常。
         *
         * 主动关闭队列，使可能正在等待not_full的生产者能够退出。
         */
        consumer_error = error_code;
        (void)blocking_queue_close(&queue);
        break;
    }

    /*
     * 等待生产者结束。
     *
     * join成功后，才能安全读取producer_context，也才能确定生产者
     * 不会继续访问queue。
     */
    error_code = pthread_join(producer_thread, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to join producer thread: %s\n",
                strerror(error_code));

        /*
         * 无法确定线程是否停止，因此不能安全销毁队列。
         */
        return false;
    }

    if (producer_context.error_code != 0) {
        fprintf(stderr,
                "Producer thread failed: %s\n",
                strerror(producer_context.error_code));

        (void)destroy_test_queue(
            &queue,
            "concurrent producer-consumer");

        return false;
    }

    if (consumer_error != 0) {
        fprintf(stderr,
                "Consumer failed: %s\n",
                strerror(consumer_error));

        (void)destroy_test_queue(
            &queue,
            "concurrent producer-consumer");

        return false;
    }

    if (!fifo_order_valid) {
        fprintf(stderr,
                "Concurrent FIFO order check failed\n");

        (void)destroy_test_queue(
            &queue,
            "concurrent producer-consumer");

        return false;
    }

    if (received_count != CONCURRENT_TEST_ITEM_COUNT) {
        fprintf(stderr,
                "Expected to receive %u values, but got %zu\n",
                CONCURRENT_TEST_ITEM_COUNT,
                received_count);

        (void)destroy_test_queue(
            &queue,
            "concurrent producer-consumer");

        return false;
    }

    /*
     * 两个线程都结束后，才能直接检查队列的普通成员。
     *
     * 并发执行期间不能绕过队列接口读取head、tail、count或closed，
     * 否则会产生数据竞争。
     */
    if (!queue.closed || queue.count != 0) {
        fprintf(stderr,
                "Unexpected final state: "
                "closed=%s, count=%zu\n",
                queue.closed ? "true" : "false",
                queue.count);

        (void)destroy_test_queue(
            &queue,
            "concurrent producer-consumer");

        return false;
    }

    return destroy_test_queue(
        &queue,
        "concurrent producer-consumer");
}

/**
 * @brief 并发测试程序入口。
 *
 * @return 并发测试通过时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    if (test_concurrent_producer_consumer()) {
        printf("[PASS] concurrent producer-consumer\n");
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "[FAIL] concurrent producer-consumer\n");
    return EXIT_FAILURE;
}