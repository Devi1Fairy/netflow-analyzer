#include "queue.h"
#include "test_helpers.h"

#include <errno.h>
#include <pthread.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 并发压力测试传输10000个整数。
 *
 * 队列容量只有4，因此生产者和两个消费者会反复触发not_full、
 * not_empty和环形下标回绕，更容易发现死锁、数据重复和数据遗漏。
 */
#define CONCURRENT_TEST_ITEM_COUNT 10000U

/*
 * 测试使用两个消费者。
 *
 * 一个消费者运行在新创建的线程中，当前测试线程充当第二个消费者。
 */
#define CONCURRENT_TEST_CONSUMER_COUNT 2U

/**
 * @brief 保存并发生产者线程的参数和运行结果。
 */
typedef struct {
    blocking_queue_t *queue; /**< 生产者和消费者共享的队列。 */
    size_t item_count;       /**< 生产者需要写入的元素数量。 */
    int error_code;          /**< 线程执行结果，0表示成功。 */
} producer_thread_context_t;

/**
 * @brief 保存消费者的参数、共享验证数据和运行结果。
 */
typedef struct {
    blocking_queue_t *queue;       /**< 消费者访问的共享队列。 */
    unsigned int *seen_counts;     /**< 记录每个整数被处理的次数。 */
    size_t item_count;             /**< 合法整数的数量和取值上限。 */
    pthread_mutex_t *result_mutex; /**< 保护seen_counts。 */
    size_t consumer_id;            /**< 消费者编号，用于错误输出。 */
    size_t received_count;         /**< 当前消费者处理的元素数量。 */
    int error_code;                /**< 消费者执行结果，0表示成功。 */
}consumer_thread_context_t;

/**
 * @brief 连续产生0到item_count-1并写入队列。
 *
 * 全部写入完成后，生产者关闭队列，使两个消费者在处理完剩余元素后
 * 能够从blocking_queue_pop得到ECANCELED并结束。
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
     * 关闭不会丢弃队列中已经存在的数据，消费者仍然可以继续取出。
     */
    context->error_code =
        blocking_queue_close(context->queue);

    return NULL;
}

/**
 * @brief 持续从队列取出整数并记录处理次数。
 *
 * 两个消费者共享seen_counts数组，因此修改数组前必须获得
 * result_mutex。每个消费者自己的received_count只由该消费者修改，
 * 主线程会在线程结束后再读取，不会发生数据竞争。
 *
 * @param argument 指向consumer_thread_context_t。
 *
 * @return 执行结果保存在上下文中，线程固定返回NULL。
 */
static void *consumer_thread_main(void *argument)
{
    consumer_thread_context_t *context = argument;

    for (;;) {
        int actual_value;
        int error_code;

        error_code =
            blocking_queue_pop(context->queue, &actual_value);

        if (error_code == ECANCELED) {
            /*
             * 队列已经关闭并且没有剩余数据，消费者正常结束。
             */
            return NULL;
        }

        if (error_code != 0) {
            context->error_code = error_code;

            /*
             * 发生非预期错误时关闭队列，让其他线程也能够结束。
             */
            (void)blocking_queue_close(context->queue);
            return NULL;
        }

        /*
         * 生产者只应该生成0到item_count-1。
         *
         * 转换为size_t之前先检查负数，避免负数转换为非常大的无符号数。
         */
        if (actual_value < 0 ||
            (size_t)actual_value >= context->item_count) {
            context->error_code = ERANGE;

            (void)blocking_queue_close(context->queue);
            return NULL;
        }

        /*
         * 两个消费者可能同时修改seen_counts，因此必须使用额外的
         * result_mutex保护测试结果。
         *
         * 这个互斥锁只属于测试程序，不是队列实现的一部分。
         */
        error_code =
            pthread_mutex_lock(context->result_mutex);

        if (error_code != 0) {
            context->error_code = error_code;

            (void)blocking_queue_close(context->queue);
            return NULL;
        }

        context->seen_counts[(size_t)actual_value]++;
        context->received_count++;

        error_code =
            pthread_mutex_unlock(context->result_mutex);

        if (error_code != 0) {
            context->error_code = error_code;

            (void)blocking_queue_close(context->queue);
            return NULL;
        }
    }
}


/**
 * @brief 验证一个生产者和两个消费者能够处理10000个整数。
 *
 * 测试验证：
 *
 * 1. 生产者能够写入0到9999；
 * 2. 两个消费者都能在队列关闭后退出；
 * 3. 总处理数量等于10000；
 * 4. 每个整数恰好被处理一次；
 * 5. 所有线程结束后队列处于关闭且为空的状态。
 *
 * @return 所有并发检查通过时返回true，否则返回false。
 */
static bool test_one_producer_two_consumers(void)
{
    /*
     * seen_counts[index]表示整数index被消费者处理了多少次。
     *
     * 使用{0}将整个数组初始化为0。
     * 10000个unsigned int通常占用约40KB，放在测试线程栈中是安全的。
     */
    unsigned int seen_counts[CONCURRENT_TEST_ITEM_COUNT] = {0};

    blocking_queue_t queue;
    pthread_mutex_t result_mutex;

    pthread_t consumer_thread;
    pthread_t producer_thread;

    consumer_thread_context_t consumer_contexts[CONCURRENT_TEST_CONSUMER_COUNT];
    producer_thread_context_t producer_context;

    size_t total_received;
    bool test_passed = true;
    int error_code;

    if (!initialize_test_queue(
            &queue,
            "concurrent producer-consumer")) {
        return false;
    }

    /*
     * result_mutex只保护测试统计数据seen_counts。
     *
     * 队列内部状态仍然由queue.mutex保护，两者职责不同。
     */
    error_code = pthread_mutex_init(&result_mutex, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to initialize result mutex: %s\n",
                strerror(error_code));

        (void)destroy_test_queue(
            &queue,
            "one producer two consumers");

        return false;
    }

     producer_context = (producer_thread_context_t){
        .queue = &queue,
        .item_count = CONCURRENT_TEST_ITEM_COUNT,
        .error_code = 0
    };

    consumer_contexts[0] = (consumer_thread_context_t){
        .queue = &queue,
        .seen_counts = seen_counts,
        .item_count = CONCURRENT_TEST_ITEM_COUNT,
        .result_mutex = &result_mutex,
        .consumer_id = 1U,
        .received_count = 0,
        .error_code = 0
    };

    consumer_contexts[1] = (consumer_thread_context_t){
        .queue = &queue,
        .seen_counts = seen_counts,
        .item_count = CONCURRENT_TEST_ITEM_COUNT,
        .result_mutex = &result_mutex,
        .consumer_id = 2U,
        .received_count = 0,
        .error_code = 0
    };

    /*
     * 先创建第一个消费者。
     *
     * 此时队列为空，所以该消费者很可能先等待not_empty。
     */
    error_code = pthread_create(&consumer_thread,
                                NULL,
                                consumer_thread_main,
                                &consumer_contexts[0]);

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to create consumer thread: %s\n",
                strerror(error_code));

        (void)pthread_mutex_destroy(&result_mutex);
        (void)destroy_test_queue(
            &queue,
            "one producer two consumers");

        return false;
    }

    /*
     * 创建生产者线程，开始写入0到9999。
     */
    error_code = pthread_create(&producer_thread,
                                NULL,
                                producer_thread_main,
                                &producer_context);

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to create producer thread: %s\n",
                strerror(error_code));

         /*
         * 第一个消费者可能正在等待not_empty。
         * 关闭队列并等待消费者退出后，才能销毁同步资源。
         */
        (void)blocking_queue_close(&queue);
        (void)pthread_join(consumer_thread, NULL);
        (void)pthread_mutex_destroy(&result_mutex);
        (void)destroy_test_queue(
            &queue,
            "one producer two consumers");


        return false;
    }

    /*
     * 当前测试线程直接调用消费者入口函数，充当第二个消费者。
     *
     * 线程入口本质上仍然是普通C函数，所以也可以由当前线程直接调用。
     * 该调用会持续处理数据，直到生产者关闭队列并且队列被取空。
     */
    (void)consumer_thread_main(&consumer_contexts[1]);

    /*
     * 第二个消费者结束意味着队列已经关闭并且暂时没有数据。
     * 接下来等待生产者和第一个消费者彻底结束。
     */
    error_code = pthread_join(producer_thread, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to join producer thread: %s\n",
                strerror(error_code));

        /*
         * 无法确定生产者是否还在使用queue，因此不能安全销毁队列。
         */
        return false;
    }

    error_code = pthread_join(consumer_thread, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to join consumer thread: %s\n",
                strerror(error_code));

        return false;
    }

    /*
     * pthread_join完成后，工作线程不会再修改上下文和seen_counts，
     * 当前线程可以安全地检查测试结果。
     */
    if (producer_context.error_code != 0) {
        fprintf(stderr,
                "Producer failed: %s\n",
                strerror(producer_context.error_code));

        test_passed = false;
    }

    for (size_t index = 0;
         index < CONCURRENT_TEST_CONSUMER_COUNT;
         index++) {
        if (consumer_contexts[index].error_code != 0) {
            fprintf(stderr,
                    "Consumer %zu failed: %s\n",
                    consumer_contexts[index].consumer_id,
                    strerror(consumer_contexts[index].error_code));

            test_passed = false;
        }
    }

    total_received =
        consumer_contexts[0].received_count +
        consumer_contexts[1].received_count;

    if (total_received != CONCURRENT_TEST_ITEM_COUNT) {
        fprintf(stderr,
                "Expected %u total values, but got %zu\n",
                CONCURRENT_TEST_ITEM_COUNT,
                total_received);

        test_passed = false;
    }

    /*
     * 每个整数必须恰好出现一次。
     *
     * seen_counts[index] == 0表示遗漏；
     * seen_counts[index] > 1表示重复。
     */
    for (size_t index = 0;
         index < CONCURRENT_TEST_ITEM_COUNT;
         index++) {
        if (seen_counts[index] != 1U) {
            fprintf(stderr,
                    "Value %zu was processed %u times\n",
                    index,
                    seen_counts[index]);

            test_passed = false;

            /*
             * 输出第一个错误已经足够定位问题，避免产生大量重复日志。
             */
            break;
        }
    }

    /*
     * 所有线程都结束后，才能绕过队列接口读取这些普通成员。
     */
    if (!queue.closed || queue.count != 0) {
        fprintf(stderr,
                "Unexpected final queue state: "
                "closed=%s, count=%zu\n",
                queue.closed ? "true" : "false",
                queue.count);

        test_passed = false;
    }

    /*
     * 输出两个消费者实际处理的任务分布。
     *
     * 操作系统调度不保证任务平均分配，所以两个数量不必相等；
     * 只要总数正确且每个值恰好出现一次即可。
     */
    printf("Consumer 1 processed %zu items\n",
           consumer_contexts[0].received_count);
    printf("Consumer 2 processed %zu items\n",
           consumer_contexts[1].received_count);
    printf("Total processed: %zu items\n",
           total_received);

    /*
     * 所有线程结束后，销毁测试结果互斥锁。
     */
    error_code = pthread_mutex_destroy(&result_mutex);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to destroy result mutex: %s\n",
                strerror(error_code));

        test_passed = false;
    }

    if (!destroy_test_queue(
            &queue,
            "one producer two consumers")) {
        test_passed = false;
    }

    return test_passed;
}


/**
 * @brief 并发测试程序入口。
 *
 * @return 并发测试通过时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    if (test_one_producer_two_consumers()) {
        printf("[PASS] one producer and two consumers\n");
        return EXIT_SUCCESS;
    }

    fprintf(stderr, "[FAIL] one producer and two consumers\n");
    return EXIT_FAILURE;
}