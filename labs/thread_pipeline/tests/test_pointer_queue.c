#include "pointer_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 保存消费者线程的输入和输出。
 *
 * pthread_create只能传入一个void *，因此使用结构体同时传递队列，
 * 并保存pop最终返回的指针和错误码。
 */
typedef struct {
    pointer_queue_t *queue;
    void *item;
    int error_code;
} consumer_thread_context_t;

/**
 * @brief 保存生产者线程的输入和输出。
 */
typedef struct {
    pointer_queue_t *queue;
    void *item;
    int error_code;
} producer_thread_context_t;

/**
 * @brief 检查实际错误码是否等于预期错误码。
 */
static bool expect_error_code(const char *case_name,
                              int actual_error,
                              int expected_error)
{
    if (actual_error == expected_error) {
        return true;
    }

    fprintf(stderr,
            "%s: expected %d (%s), got %d (%s)\n",
            case_name,
            expected_error,
            strerror(expected_error),
            actual_error,
            strerror(actual_error));

    return false;
}

/**
 * @brief 初始化一个测试队列并统一输出错误。
 */
static bool initialize_test_queue(pointer_queue_t *queue,
                                  const char *test_name)
{
    const int error_code =
        pointer_queue_init(queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pointer_queue_init failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    return true;
}

/**
 * @brief 关闭、取空并销毁测试队列。
 *
 * 测试使用的对象都位于当前测试函数的栈上，因此清理时只丢弃指针，
 * 不调用free。
 *
 * 正式流水线中的动态对象必须由消费者释放。
 */
static bool cleanup_test_queue(pointer_queue_t *queue,
                               const char *test_name)
{
    bool cleanup_succeeded = true;
    int error_code;

    error_code =
        pointer_queue_close(queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pointer_queue_close failed: %s\n",
                test_name,
                strerror(error_code));

        cleanup_succeeded = false;
    }

    /*
     * 销毁指针队列前必须保证队列已经取空。
     */
    for (;;) {
        void *discarded_item = NULL;

        error_code =
            pointer_queue_pop(queue,
                              &discarded_item);

        if (error_code == 0) {
            continue;
        }

        if (error_code == ECANCELED) {
            break;
        }

        fprintf(stderr,
                "%s: queue drain failed: %s\n",
                test_name,
                strerror(error_code));

        cleanup_succeeded = false;
        break;
    }

    error_code =
        pointer_queue_destroy(queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pointer_queue_destroy failed: %s\n",
                test_name,
                strerror(error_code));

        cleanup_succeeded = false;
    }

    return cleanup_succeeded;
}

/**
 * @brief 消费者线程入口。
 */
static void *consumer_thread_main(void *argument)
{
    consumer_thread_context_t *context =
        (consumer_thread_context_t *)argument;

    context->error_code =
        pointer_queue_pop(context->queue,
                          &context->item);

    return NULL;
}

/**
 * @brief 生产者线程入口。
 */
static void *producer_thread_main(void *argument)
{
    producer_thread_context_t *context =
        (producer_thread_context_t *)argument;

    context->error_code =
        pointer_queue_push(context->queue,
                           context->item);

    return NULL;
}

/**
 * @brief 验证公开接口拒绝无效参数。
 */
static bool test_argument_validation(void)
{
    const char *test_name =
        "argument validation";

    pointer_queue_t queue;
    int value = 42;
    void *output_item = NULL;

    if (!expect_error_code(
            "init NULL queue",
            pointer_queue_init(NULL),
            EINVAL)) {
        return false;
    }

    if (!initialize_test_queue(
            &queue,
            test_name)) {
        return false;
    }

    if (!expect_error_code(
            "push NULL queue",
            pointer_queue_push(NULL,
                               &value),
            EINVAL) ||
        !expect_error_code(
            "push NULL item",
            pointer_queue_push(&queue,
                               NULL),
            EINVAL) ||
        !expect_error_code(
            "pop NULL queue",
            pointer_queue_pop(NULL,
                              &output_item),
            EINVAL) ||
        !expect_error_code(
            "pop NULL output",
            pointer_queue_pop(&queue,
                              NULL),
            EINVAL) ||
        !expect_error_code(
            "close NULL queue",
            pointer_queue_close(NULL),
            EINVAL) ||
        !expect_error_code(
            "destroy NULL queue",
            pointer_queue_destroy(NULL),
            EINVAL)) {
        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    return cleanup_test_queue(
        &queue,
        test_name);
}

/**
 * @brief 验证指针FIFO顺序、指针身份和环形下标回绕。
 *
 * 流程：
 *
 * 1. 写入0、1、2、3，填满队列；
 * 2. 取出0和1；
 * 3. 写入4和5，使tail回到数组开头；
 * 4. 验证剩余顺序是2、3、4、5。
 */
static bool test_pointer_fifo_and_wraparound(void)
{
    const char *test_name =
        "pointer FIFO and wraparound";

    int values[] = {
        10,
        20,
        30,
        40,
        50,
        60
    };

    const size_t value_count =
        sizeof(values) /
        sizeof(values[0]);

    pointer_queue_t queue;
    int error_code;

    if (!initialize_test_queue(
            &queue,
            test_name)) {
        return false;
    }

    /*
     * 先写满容量为4的队列。
     */
    for (size_t index = 0;
         index < POINTER_QUEUE_CAPACITY;
         index++) {
        error_code =
            pointer_queue_push(&queue,
                               &values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "%s: initial push %zu failed: %s\n",
                    test_name,
                    index,
                    strerror(error_code));

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    /*
     * 取出前两个指针。
     */
    for (size_t index = 0;
         index < 2U;
         index++) {
        void *actual_item = NULL;

        error_code =
            pointer_queue_pop(&queue,
                              &actual_item);

        if (error_code != 0 ||
            actual_item != &values[index]) {
            fprintf(stderr,
                    "%s: first pop %zu mismatch\n",
                    test_name,
                    index);

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    /*
     * 写入最后两个指针，使tail发生环形回绕。
     */
    for (size_t index =
             POINTER_QUEUE_CAPACITY;
         index < value_count;
         index++) {
        error_code =
            pointer_queue_push(&queue,
                               &values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "%s: wrapped push %zu failed: %s\n",
                    test_name,
                    index,
                    strerror(error_code));

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    /*
     * 当前逻辑顺序应为values[2]、[3]、[4]、[5]。
     */
    for (size_t index = 2U;
         index < value_count;
         index++) {
        void *actual_item = NULL;

        error_code =
            pointer_queue_pop(&queue,
                              &actual_item);

        if (error_code != 0 ||
            actual_item != &values[index]) {
            fprintf(stderr,
                    "%s: remaining pop %zu mismatch\n",
                    test_name,
                    index);

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }

        /*
         * 除了比较地址，也通过恢复类型检查指向的值。
         */
        if (*(int *)actual_item != values[index]) {
            fprintf(stderr,
                    "%s: pointer %zu refers to wrong value\n",
                    test_name,
                    index);

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    return cleanup_test_queue(
        &queue,
        test_name);
}

/**
 * @brief 验证关闭、剩余对象处理和销毁约束。
 */
static bool test_close_and_destroy_lifecycle(void)
{
    const char *test_name =
        "close and destroy lifecycle";

    int values[] = {
        10,
        20,
        30
    };

    pointer_queue_t queue;
    void *actual_item = NULL;
    int error_code;

    if (!initialize_test_queue(
            &queue,
            test_name)) {
        return false;
    }

    /*
     * 队列尚未关闭，即使为空也不允许销毁。
     */
    error_code =
        pointer_queue_destroy(&queue);

    if (!expect_error_code(
            "destroy open queue",
            error_code,
            EBUSY)) {
        /*
         * 如果错误地返回0，队列已经失效，不能再次清理。
         */
        if (error_code != 0) {
            (void)cleanup_test_queue(
                &queue,
                test_name);
        }

        return false;
    }

    error_code =
        pointer_queue_push(&queue,
                           &values[0]);

    if (error_code == 0) {
        error_code =
            pointer_queue_push(&queue,
                               &values[1]);
    }

    if (error_code != 0) {
        fprintf(stderr,
                "%s: initial push failed: %s\n",
                test_name,
                strerror(error_code));

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    error_code =
        pointer_queue_close(&queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: close failed: %s\n",
                test_name,
                strerror(error_code));

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * close是幂等操作，重复调用仍然成功。
     */
    if (!expect_error_code(
            "repeated close",
            pointer_queue_close(&queue),
            0)) {
        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * 队列关闭后不能接收第三个指针，所有权仍属于调用者。
     */
    if (!expect_error_code(
            "push after close",
            pointer_queue_push(&queue,
                               &values[2]),
            ECANCELED)) {
        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * 队列虽然已经关闭，但其中仍有两个对象，因此不能销毁。
     */
    error_code =
        pointer_queue_destroy(&queue);

    if (!expect_error_code(
            "destroy nonempty queue",
            error_code,
            EBUSY)) {
        if (error_code != 0) {
            (void)cleanup_test_queue(
                &queue,
                test_name);
        }

        return false;
    }

    /*
     * 关闭前已入队的对象仍然可以按照FIFO顺序取出。
     */
    for (size_t index = 0;
         index < 2U;
         index++) {
        error_code =
            pointer_queue_pop(&queue,
                              &actual_item);

        if (error_code != 0 ||
            actual_item != &values[index]) {
            fprintf(stderr,
                    "%s: draining item %zu failed\n",
                    test_name,
                    index);

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    /*
     * 使用非NULL哨兵值，验证失败时pop会把输出重置为NULL。
     */
    actual_item = &values[2];

    error_code =
        pointer_queue_pop(&queue,
                          &actual_item);

    if (!expect_error_code(
            "pop closed empty queue",
            error_code,
            ECANCELED) ||
        actual_item != NULL) {
        fprintf(stderr,
                "%s: failed pop did not clear output\n",
                test_name);

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    error_code =
        pointer_queue_destroy(&queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: final destroy failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    return true;
}

/**
 * @brief 验证空队列消费者能够取得随后写入的指针。
 */
static bool test_blocking_pop_on_empty_queue(void)
{
    const char *test_name =
        "blocking pop on empty queue";

    pointer_queue_t queue;
    pthread_t consumer_thread;

    int value = 42;
    int error_code;

    consumer_thread_context_t context;

    if (!initialize_test_queue(
            &queue,
            test_name)) {
        return false;
    }

    context = (consumer_thread_context_t){
        .queue = &queue,
        .item = NULL,
        .error_code = 0
    };

    error_code =
        pthread_create(&consumer_thread,
                       NULL,
                       consumer_thread_main,
                       &context);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pthread_create failed: %s\n",
                test_name,
                strerror(error_code));

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * 如果消费者已经运行，它会等待not_empty；
     * 如果主线程先运行，消费者稍后也能直接取出该指针。
     *
     * 测试不依赖具体线程调度顺序。
     */
    error_code =
        pointer_queue_push(&queue,
                           &value);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: push failed: %s\n",
                test_name,
                strerror(error_code));

        (void)pointer_queue_close(&queue);
        (void)pthread_join(consumer_thread,
                           NULL);
        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    error_code =
        pthread_join(consumer_thread,
                     NULL);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pthread_join failed: %s\n",
                test_name,
                strerror(error_code));

        /*
         * 无法确认线程是否仍在使用队列，因此不能安全销毁。
         */
        return false;
    }

    if (context.error_code != 0 ||
        context.item != &value) {
        fprintf(stderr,
                "%s: consumer received wrong pointer or error\n",
                test_name);

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    return cleanup_test_queue(
        &queue,
        test_name);
}

/**
 * @brief 验证满队列生产者在消费者释放空间后能够继续写入。
 */
static bool test_blocking_push_on_full_queue(void)
{
    const char *test_name =
        "blocking push on full queue";

    int values[] = {
        10,
        20,
        30,
        40,
        50
    };

    pointer_queue_t queue;
    pthread_t producer_thread;

    producer_thread_context_t context;

    void *actual_item = NULL;
    int error_code;

    if (!initialize_test_queue(
            &queue,
            test_name)) {
        return false;
    }

    for (size_t index = 0;
         index < POINTER_QUEUE_CAPACITY;
         index++) {
        error_code =
            pointer_queue_push(&queue,
                               &values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "%s: fill push %zu failed: %s\n",
                    test_name,
                    index,
                    strerror(error_code));

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    context = (producer_thread_context_t){
        .queue = &queue,
        .item = &values[4],
        .error_code = 0
    };

    error_code =
        pthread_create(&producer_thread,
                       NULL,
                       producer_thread_main,
                       &context);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pthread_create failed: %s\n",
                test_name,
                strerror(error_code));

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * 取出第一个指针并释放一个槽位，pop会signal not_full。
     */
    error_code =
        pointer_queue_pop(&queue,
                          &actual_item);

    if (error_code != 0 ||
        actual_item != &values[0]) {
        fprintf(stderr,
                "%s: first pop failed\n",
                test_name);

        (void)pointer_queue_close(&queue);
        (void)pthread_join(producer_thread,
                           NULL);
        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    error_code =
        pthread_join(producer_thread,
                     NULL);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pthread_join failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    if (context.error_code != 0) {
        fprintf(stderr,
                "%s: producer push failed: %s\n",
                test_name,
                strerror(context.error_code));

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * 剩余顺序应是values[1]、[2]、[3]、[4]。
     */
    for (size_t index = 1U;
         index < 5U;
         index++) {
        actual_item = NULL;

        error_code =
            pointer_queue_pop(&queue,
                              &actual_item);

        if (error_code != 0 ||
            actual_item != &values[index]) {
            fprintf(stderr,
                    "%s: remaining item %zu mismatch\n",
                    test_name,
                    index);

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    return cleanup_test_queue(
        &queue,
        test_name);
}

/**
 * @brief 验证close能够结束空队列上的消费者。
 */
static bool test_close_wakes_waiting_consumer(void)
{
    const char *test_name =
        "close wakes waiting consumer";

    pointer_queue_t queue;
    pthread_t consumer_thread;

    consumer_thread_context_t context;
    int error_code;

    if (!initialize_test_queue(
            &queue,
            test_name)) {
        return false;
    }

    context = (consumer_thread_context_t){
        .queue = &queue,
        .item = NULL,
        .error_code = 0
    };

    error_code =
        pthread_create(&consumer_thread,
                       NULL,
                       consumer_thread_main,
                       &context);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pthread_create failed: %s\n",
                test_name,
                strerror(error_code));

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * 无论消费者已经等待，还是稍后才进入pop，
     * 最终都必须发现队列关闭并返回ECANCELED。
     */
    error_code =
        pointer_queue_close(&queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: close failed: %s\n",
                test_name,
                strerror(error_code));

        (void)pointer_queue_close(&queue);
        (void)pthread_join(consumer_thread,
                           NULL);
        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    error_code =
        pthread_join(consumer_thread,
                     NULL);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pthread_join failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    if (context.error_code != ECANCELED ||
        context.item != NULL) {
        fprintf(stderr,
                "%s: consumer expected ECANCELED and NULL\n",
                test_name);

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * 队列已经关闭且为空，可以直接销毁。
     */
    error_code =
        pointer_queue_destroy(&queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: destroy failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    return true;
}

/**
 * @brief 验证close能够结束满队列上的生产者。
 */
static bool test_close_wakes_waiting_producer(void)
{
    const char *test_name =
        "close wakes waiting producer";

    int values[] = {
        10,
        20,
        30,
        40,
        50
    };

    pointer_queue_t queue;
    pthread_t producer_thread;

    producer_thread_context_t context;
    int error_code;

    if (!initialize_test_queue(
            &queue,
            test_name)) {
        return false;
    }

    for (size_t index = 0;
         index < POINTER_QUEUE_CAPACITY;
         index++) {
        error_code =
            pointer_queue_push(&queue,
                               &values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "%s: fill push %zu failed: %s\n",
                    test_name,
                    index,
                    strerror(error_code));

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    context = (producer_thread_context_t){
        .queue = &queue,
        .item = &values[4],
        .error_code = 0
    };

    error_code =
        pthread_create(&producer_thread,
                       NULL,
                       producer_thread_main,
                       &context);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pthread_create failed: %s\n",
                test_name,
                strerror(error_code));

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * close广播not_full。等待中的生产者被唤醒后必须返回ECANCELED，
     * 不能把values[4]写入队列。
     */
    error_code =
        pointer_queue_close(&queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: close failed: %s\n",
                test_name,
                strerror(error_code));

        (void)pointer_queue_close(&queue);
        (void)pthread_join(producer_thread,
                           NULL);
        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    error_code =
        pthread_join(producer_thread,
                     NULL);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: pthread_join failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    if (context.error_code != ECANCELED) {
        fprintf(stderr,
                "%s: producer expected ECANCELED, got %d\n",
                test_name,
                context.error_code);

        (void)cleanup_test_queue(
            &queue,
            test_name);

        return false;
    }

    /*
     * 关闭前的四个指针仍然必须完整保留并按FIFO顺序取出。
     */
    for (size_t index = 0;
         index < POINTER_QUEUE_CAPACITY;
         index++) {
        void *actual_item = NULL;

        error_code =
            pointer_queue_pop(&queue,
                              &actual_item);

        if (error_code != 0 ||
            actual_item != &values[index]) {
            fprintf(stderr,
                    "%s: retained item %zu mismatch\n",
                    test_name,
                    index);

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    /*
     * 如果额外的values[4]被错误写入，这次pop就不会返回ECANCELED。
     */
    {
        void *actual_item = &values[4];

        error_code =
            pointer_queue_pop(&queue,
                              &actual_item);

        if (error_code != ECANCELED ||
            actual_item != NULL) {
            fprintf(stderr,
                    "%s: rejected pointer was unexpectedly queued\n",
                    test_name);

            (void)cleanup_test_queue(
                &queue,
                test_name);

            return false;
        }
    }

    error_code =
        pointer_queue_destroy(&queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: destroy failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    return true;
}

/**
 * @brief 指针阻塞队列测试程序入口。
 */
int main(void)
{
    bool all_passed = true;

    if (test_argument_validation()) {
        printf("[PASS] argument validation\n");
    } else {
        fprintf(stderr,
                "[FAIL] argument validation\n");
        all_passed = false;
    }

    if (test_pointer_fifo_and_wraparound()) {
        printf("[PASS] pointer FIFO and wraparound\n");
    } else {
        fprintf(stderr,
                "[FAIL] pointer FIFO and wraparound\n");
        all_passed = false;
    }

    if (test_close_and_destroy_lifecycle()) {
        printf("[PASS] close and destroy lifecycle\n");
    } else {
        fprintf(stderr,
                "[FAIL] close and destroy lifecycle\n");
        all_passed = false;
    }

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

    if (test_close_wakes_waiting_consumer()) {
        printf("[PASS] close wakes waiting consumer\n");
    } else {
        fprintf(stderr,
                "[FAIL] close wakes waiting consumer\n");
        all_passed = false;
    }

    if (test_close_wakes_waiting_producer()) {
        printf("[PASS] close wakes waiting producer\n");
    } else {
        fprintf(stderr,
                "[FAIL] close wakes waiting producer\n");
        all_passed = false;
    }

    return all_passed
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}