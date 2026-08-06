#include "queue.h"
#include "test_helpers.h"

#include <pthread.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 保存单次消费者线程的输入和输出。
 *
 * pthread_create只能传递一个void指针，所以使用结构体同时传递队列指针，
 * 并保存线程最终取得的值和错误码。
 */
typedef struct {
    blocking_queue_t *queue; /**< 消费者访问的共享队列。 */
    int value;               /**< 消费者成功取出的值。 */
    int error_code;          /**< blocking_queue_pop的返回值。 */
} consumer_thread_context_t;

/**
 * @brief 保存单次生产者线程的输入和输出。
 */
typedef struct {
    blocking_queue_t *queue; /**< 生产者访问的共享队列。 */
    int value;               /**< 生产者需要写入的值。 */
    int error_code;          /**< blocking_queue_push的返回值。 */
} producer_thread_context_t;

/**
 * @brief 消费者线程入口。
 *
 * @param argument 指向consumer_thread_context_t。
 *
 * @return 结果通过上下文结构体返回，线程固定返回NULL。
 */
static void *consumer_thread_main(void *argument)
{
    consumer_thread_context_t *context = argument;

    context->error_code =
        blocking_queue_pop(context->queue, &context->value);

    return NULL;
}

/**
 * @brief 生产者线程入口。
 *
 * @param argument 指向producer_thread_context_t。
 *
 * @return 结果通过上下文结构体返回，线程固定返回NULL。
 */
static void *producer_thread_main(void *argument)
{
    producer_thread_context_t *context = argument;

    context->error_code =
        blocking_queue_push(context->queue, context->value);

    return NULL;
}

/**
 * @brief 验证空队列消费者能够取得生产者随后写入的数据。
 *
 * 消费者调用pop时，如果队列仍为空，就会等待not_empty。
 * 主线程写入42后，push会发送not_empty信号。
 *
 * @return 消费者成功取得42时返回true，否则返回false。
 */
static bool test_blocking_pop_on_empty_queue(void)
{
    blocking_queue_t queue;
    pthread_t consumer_thread;

    consumer_thread_context_t consumer_context;
    int error_code;

    if (!initialize_test_queue(&queue,
                               "blocking pop on empty queue")) {
        return false;
    }

    consumer_context = (consumer_thread_context_t){
        .queue = &queue,
        .value = 0,
        .error_code = 0
    };

    /*
     * 创建消费者线程。
     *
     * 如果消费者先运行，它会等待not_empty；
     * 如果主线程先写入42，消费者稍后也能正常取得42。
     *
     * 无论操作系统采用哪一种调度顺序，结果都必须正确。
     */
    error_code = pthread_create(&consumer_thread,
                                NULL,
                                consumer_thread_main,
                                &consumer_context);

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to create consumer thread: %s\n",
                strerror(error_code));

        (void)destroy_test_queue(
            &queue,
            "blocking pop on empty queue");

        return false;
    }

    /*
     * 写入一个元素。成功写入后，push会signal not_empty。
     */
    error_code = blocking_queue_push(&queue, 42);
    if (error_code != 0) {
        fprintf(stderr,
                "Push for consumer failed: %s\n",
                strerror(error_code));

        /*
         * 如果消费者仍在等待，close会把它唤醒，使pthread_join
         * 不会永久阻塞。
         */
        (void)blocking_queue_close(&queue);
        (void)pthread_join(consumer_thread, NULL);
        (void)destroy_test_queue(
            &queue,
            "blocking pop on empty queue");

        return false;
    }

    /*
     * 等待消费者线程结束，同时回收其线程资源。
     *
     * join成功之后，主线程才能安全读取consumer_context中的结果。
     */
    error_code = pthread_join(consumer_thread, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to join consumer thread: %s\n",
                strerror(error_code));

        /*
         * join失败后无法确认线程是否还在访问queue，因此不能安全销毁。
         */
        return false;
    }

    if (consumer_context.error_code != 0 ||
        consumer_context.value != 42) {
        fprintf(stderr,
                "Consumer expected value 42, "
                "but got value=%d, error=%d\n",
                consumer_context.value,
                consumer_context.error_code);

        (void)destroy_test_queue(
            &queue,
            "blocking pop on empty queue");

        return false;
    }

    return destroy_test_queue(
        &queue,
        "blocking pop on empty queue");
}

/**
 * @brief 验证满队列生产者能够在消费者释放空间后继续写入。
 *
 * 测试先写满队列，然后创建生产者线程写入50。主线程取走10后，
 * pop会发送not_full信号，生产者便可以继续写入50。
 *
 * @return 生产者成功写入且FIFO顺序正确时返回true，否则返回false。
 */
static bool test_blocking_push_on_full_queue(void)
{
    const int initial_values[] = {10, 20, 30, 40};
    const int expected_values[] = {20, 30, 40, 50};

    const size_t initial_count =
        sizeof(initial_values) / sizeof(initial_values[0]);
    const size_t expected_count =
        sizeof(expected_values) / sizeof(expected_values[0]);

    blocking_queue_t queue;
    pthread_t producer_thread;

    producer_thread_context_t producer_context;
    int error_code;
    int first_value;

    if (!initialize_test_queue(&queue,
                               "blocking push on full queue")) {
        return false;
    }

    /*
     * 先写入四个元素，使队列达到满状态。
     */
    for (size_t index = 0; index < initial_count; index++) {
        error_code =
            blocking_queue_push(&queue, initial_values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "Initial push failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            (void)destroy_test_queue(
                &queue,
                "blocking push on full queue");

            return false;
        }
    }

    producer_context = (producer_thread_context_t){
        .queue = &queue,
        .value = 50,
        .error_code = 0
    };

    /*
     * 生产者尝试向满队列写入50。
     *
     * 如果生产者先运行，它会等待not_full；
     * 如果主线程先取出10，生产者稍后可以直接写入。
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
            "blocking push on full queue");

        return false;
    }

    /*
     * 取出队头的10并释放一个位置。
     *
     * pop成功后会signal not_full，唤醒可能正在等待的生产者。
     */
    error_code = blocking_queue_pop(&queue, &first_value);
    if (error_code != 0) {
        fprintf(stderr,
                "Pop for producer failed: %s\n",
                strerror(error_code));

        (void)blocking_queue_close(&queue);
        (void)pthread_join(producer_thread, NULL);
        (void)destroy_test_queue(
            &queue,
            "blocking push on full queue");

        return false;
    }

    error_code = pthread_join(producer_thread, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to join producer thread: %s\n",
                strerror(error_code));

        return false;
    }

    if (first_value != 10) {
        fprintf(stderr,
                "Expected first value 10, but got %d\n",
                first_value);

        (void)destroy_test_queue(
            &queue,
            "blocking push on full queue");

        return false;
    }

    if (producer_context.error_code != 0) {
        fprintf(stderr,
                "Producer failed to push 50: %s\n",
                strerror(producer_context.error_code));

        (void)destroy_test_queue(
            &queue,
            "blocking push on full queue");

        return false;
    }

    /*
     * 取出10并写入50后，逻辑顺序应该是20、30、40、50。
     */
    for (size_t index = 0; index < expected_count; index++) {
        int actual_value;

        error_code = blocking_queue_pop(&queue, &actual_value);

        if (error_code != 0 ||
            actual_value != expected_values[index]) {
            fprintf(stderr,
                    "Remaining FIFO mismatch at index %zu: "
                    "expected=%d, actual=%d, error=%d\n",
                    index,
                    expected_values[index],
                    actual_value,
                    error_code);

            (void)destroy_test_queue(
                &queue,
                "blocking push on full queue");

            return false;
        }
    }

    return destroy_test_queue(
        &queue,
        "blocking push on full queue");
}

/**
 * @brief 条件变量阻塞测试程序入口。
 *
 * @return 所有阻塞测试通过时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    bool all_passed = true;

    if (test_blocking_pop_on_empty_queue()) {
        printf("[PASS] blocking pop on empty queue\n");
    } else {
        fprintf(stderr,
                "[FAIL] blocking pop on empty queue\n");
        all_passed = false;
    }

    if (test_blocking_push_on_full_queue()) {
        printf("[PASS] blocking push on full queue\n");
    } else {
        fprintf(stderr,
                "[FAIL] blocking push on full queue\n");
        all_passed = false;
    }

    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}