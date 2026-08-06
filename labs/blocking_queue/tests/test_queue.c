#include "queue.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

/*
 * 并发测试传输1000个整数。
 *
 * 队列容量只有4，因此生产者和消费者会反复遇到队列满、队列空以及
 * 数组下标循环归零，从而比只传输几个元素更容易暴露同步错误。
 */
#define CONCURRENT_TEST_ITEM_COUNT 1000U

/**
 * @brief 保存生产者线程运行所需的参数和执行结果。
 *
 * pthread_create只能给线程入口传递一个void指针，因此把多个参数
 * 组合到一个上下文结构体中。
 */
typedef struct {
    blocking_queue_t *queue; /**< 生产者和消费者共享的队列。 */
    size_t item_count;       /**< 生产者需要写入的元素数量。 */
    int error_code;          /**< 线程执行结果，0表示成功。 */
} producer_thread_context_t;

/**
 * @brief 保存单次消费者线程的输入和输出。
 *
 * 该上下文用于验证消费者能够等待not_empty，以及消费者取走数据后
 * 能够唤醒等待not_full的生产者。
 */
typedef struct {
    blocking_queue_t *queue; /**< 消费者访问的共享队列。 */
    int value;               /**< 消费者成功取出的值。 */
    int error_code;          /**< blocking_queue_pop的返回值。 */
} consumer_thread_context_t;

/**
 * @brief 为一个测试场景初始化独立队列。
 *
 * 该函数只属于测试代码，用于减少每个测试中重复的初始化和错误输出。
 *
 * @param queue 指向待初始化的测试队列，不能为NULL。
 * @param test_name 当前测试名称，用于输出可定位的错误信息。
 *
 * @return 初始化成功时返回true，否则返回false。
 */

static bool initialize_test_queue(blocking_queue_t *queue,
                                  const char *test_name)
{
    const int error_code = blocking_queue_init(queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: blocking_queue_init failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    return true;
}

/**
 * @brief 销毁测试场景使用的队列。
 *
 * @param queue 指向已经初始化的测试队列。
 * @param test_name 当前测试名称，用于输出可定位的错误信息。
 *
 * @return 销毁成功时返回true，否则返回false。
 */
static bool destroy_test_queue(blocking_queue_t *queue,
                               const char *test_name)
{
    const int error_code = blocking_queue_destroy(queue);

    if (error_code != 0) {
        fprintf(stderr,
                "%s: blocking_queue_destroy failed: %s\n",
                test_name,
                strerror(error_code));

        return false;
    }

    return true;
}

/**
 * @brief 从队列中取出一个整数。
 *
 * 如果队列为空，该线程会阻塞在not_empty条件变量上，直到生产者
 * 写入数据或关闭队列。
 *
 * @param argument 指向consumer_thread_context_t的void指针。
 *
 * @return 测试结果保存在上下文中，线程固定返回NULL。
 */
static void *consumer_thread_main(void *argument)
{
    consumer_thread_context_t *context = argument;

    context->error_code =
        blocking_queue_pop(context->queue,
                           &context->value);

    return NULL;
}

/**
 * @brief 连续产生整数并写入阻塞队列，完成后关闭队列。
 *
 * push现在会在队列已满时自动等待not_full，所以线程不再需要处理
 * ENOSPC，也不再需要sched_yield轮询。
 *
 * @param argument 指向producer_thread_context_t的void指针。
 *
 * @return 测试结果保存在上下文中，线程固定返回NULL。
 */
static void *producer_thread_main(void *argument)
{
    /*
     * C语言允许把void指针隐式转换为其他对象指针，
     * 因此这里不需要进行显式强制类型转换。
     */
    producer_thread_context_t *context = argument;

    for (size_t index = 0; index < context->item_count; index++) {
        const int error_code =
            blocking_queue_push(context->queue,
                                (int)index);
        if (error_code != 0) {
            context->error_code = error_code;

         /*
        * 唤醒可能仍在等待not_empty的消费者，使测试能够结束。
        */
        (void)blocking_queue_close(context->queue);

        return NULL;
        }
    }

    /*
     * 完成全部数据后关闭队列，同时唤醒所有仍在等待的线程。
     */
    context->error_code =
        blocking_queue_close(context->queue);

    return NULL;
}


/**
 * @brief 验证队列初始化后的所有普通数据成员。
 *
 * @return 所有成员符合初始状态时返回true，否则返回false。
 */
static bool test_queue_init(void)
{
    blocking_queue_t queue;
    int error_code;

    /*
    * 调用被测试的初始化接口。
    */
    error_code = blocking_queue_init(&queue);
    if (error_code != 0) {
        fprintf(stderr,
                "blocking_queue_init failed: %s\n",
                strerror(error_code));

        return false;
    }

    /*
    * 新队列必须为空，读写位置都应该从下标0开始。
    */
    if (queue.head != 0 ||
        queue.tail != 0 ||
        queue.count != 0 ||
        queue.closed) {
        fprintf(stderr,
                "Unexpected initial state: "
                "head=%zu, tail=%zu, count=%zu, closed=%s\n",
                queue.head,
                queue.tail,
                queue.count,
                queue.closed ? "true" : "false");

        return false;
    }
    /*
    * 当前初始化接口约定items数组也全部初始化为0。
    */
    for (size_t index = 0; index < QUEUE_CAPACITY; index++) {
        if (queue.items[index] != 0) {
            fprintf(stderr,
                    "items[%zu] should be 0, but got %d\n",
                    index,
                    queue.items[index]);

            return false;
        }
    }

    return destroy_test_queue(&queue, "queue initialization");
}

/**
 * @brief 验证部分填充队列仍然遵守FIFO顺序。
 *
 * 测试只向容量为4的队列写入三个整数，然后按照相同顺序取出，
 * 用于证明队列操作依据count判断有效数据数量，而不是要求队列填满。
 *
 * @return 入队、出队和最终状态都符合预期时返回true，否则返回false。
 */
static bool test_partial_queue_fifo(void){
    const int expected_values[] = {10, 20, 30};
    const size_t value_count =
        sizeof(expected_values) / sizeof(expected_values[0]);

    blocking_queue_t queue;
    int error_code;

    /*
    * 每个测试都创建并初始化自己的队列，防止不同测试之间共享状态。
    */
    error_code = blocking_queue_init(&queue);
    if (error_code != 0) {
        fprintf(stderr,
                "Queue initialization failed in FIFO test: %s\n",
                strerror(error_code));

        return false;
    }

    /*
    * 只写入三个元素。队列容量为4，因此整个过程不应该出现满队列错误。
    */
    for (size_t index = 0; index < value_count; index++) {
        error_code =blocking_queue_push(&queue, expected_values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "Failed to push %d at index %zu: %s\n",
                    expected_values[index],
                    index,
                    strerror(error_code));

            return false;
        }
    }
    /*
    * 三次入队后，队列中应该恰好有三个有效元素。
    */
    if (queue.count != value_count) {
        fprintf(stderr,
                "Expected count=%zu after pushes, but got %zu\n",
                value_count,
                queue.count);

        return false;
    }

    /*
    * 按照写入顺序取出数据，验证队列符合先进先出规则。
    */
    for (size_t index = 0; index < value_count; index++) {
        int actual_value;

        error_code = blocking_queue_pop(&queue, &actual_value);
        if (error_code != 0) {
            fprintf(stderr,
                    "Failed to pop value at index %zu: %s\n",
                    index,
                    strerror(error_code));

            return false;
        }

        if (actual_value != expected_values[index]) {
            fprintf(stderr,
                    "FIFO mismatch at index %zu: "
                    "expected %d, got %d\n",
                    index,
                    expected_values[index],
                    actual_value);

            return false;
        }
    }

    /*
     * 写入和取出的数量相同，队列最终应该重新变为空队列。
     * 空队列中head和tail指向同一物理位置，但不要求它们必须为0。
     */
    if (queue.count != 0 ||
        queue.head != queue.tail ||
        queue.closed) {
        fprintf(stderr,
                "Unexpected final state: "
                "head=%zu, tail=%zu, count=%zu, closed=%s\n",
                queue.head,
                queue.tail,
                queue.count,
                queue.closed ? "true" : "false");

        return false;
    }

    return destroy_test_queue(&queue, "partial queue FIFO");
}

/**
 * @brief 验证满队列中的生产者能在消费者释放空间后继续写入。
 *
 * 测试先填满队列，再创建消费者线程取出最早的数据。主测试线程随后
 * 写入999。如果push先发现队列已满，它会等待not_full；消费者pop后
 * 会发送not_full信号，使push继续执行。
 *
 * @return 生产者成功写入且FIFO顺序保持正确时返回true。
 */
static bool test_blocking_push_on_full_queue(void)
{
    const int expected_remaining[] = {2, 3, 4, 999};
    const size_t expected_count =
        sizeof(expected_remaining) /
        sizeof(expected_remaining[0]);


    blocking_queue_t queue;
    pthread_t consumer_thread;
    consumer_thread_context_t consumer_context;
    int error_code;

    if (!initialize_test_queue(&queue, "blocking push on full queue")) {
        return false;
    }

     /*
     * 写入1、2、3、4，使队列达到满状态。
     */
    for (size_t index = 0; index < QUEUE_CAPACITY; index++) {
        const int value = (int)(index + 1U);
        error_code =
            blocking_queue_push(&queue, value);

        if (error_code != 0) {
            fprintf(stderr,
                    "Initial push failed "
                    "at index %zu: %s\n",
                    index,
                    strerror(error_code));

            return false;
        }
    }

    consumer_context = (consumer_thread_context_t){
        .queue = &queue,
        .value = 0,
        .error_code = 0
    };

    /*
     * 创建一个消费者，用于从满队列取出最早的元素1。
     */
    error_code = pthread_create(&consumer_thread,
                                NULL,
                                consumer_thread_main,
                                &consumer_context);

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to create consumer thread: %s\n",
                strerror(error_code));

        (void)blocking_queue_destroy(&queue);
        return false;
    }

     /*
     * 如果消费者尚未取出数据，这次push会阻塞在not_full上；
     * 消费者pop后发送not_full信号，push才会继续写入999。
     */
    error_code = blocking_queue_push(&queue, 999);
    if (error_code != 0) {
        fprintf(stderr,
                "Blocking push failed: %s\n",
                strerror(error_code));

        (void)blocking_queue_close(&queue);
        (void)pthread_join(consumer_thread, NULL);
        (void)blocking_queue_destroy(&queue);

        return false;
    }

    error_code = pthread_join(consumer_thread, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to join consumer thread: %s\n",
                strerror(error_code));

        /*
         * 无法确认消费者是否已经结束，因此不能销毁队列。
         */
        return false;
    }

    if (consumer_context.error_code != 0 ||
        consumer_context.value != 1) {
        fprintf(stderr,
                "Consumer expected value 1, got value=%d, error=%d\n",
                consumer_context.value,
                consumer_context.error_code);

        (void)blocking_queue_destroy(&queue);
        return false;
    }

     /*
     * 此时逻辑队列应该是2、3、4、999。
     *
     * 先关闭队列，使取完四个元素后的下一次pop能够返回ECANCELED，
     * 而不是再次等待。
     */
error_code = blocking_queue_close(&queue);
    if (error_code != 0) {
        fprintf(stderr,
                "Close after blocking push failed: %s\n",
                strerror(error_code));

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    for (size_t index = 0;
         index < expected_count;
         index++) {
        int actual_value;

        error_code =
            blocking_queue_pop(&queue, &actual_value);

        if (error_code != 0 ||
            actual_value != expected_remaining[index]) {
            fprintf(stderr,
                    "Remaining FIFO mismatch at index %zu: "
                    "expected=%d, actual=%d, error=%d\n",
                    index,
                    expected_remaining[index],
                    actual_value,
                    error_code);

            (void)blocking_queue_destroy(&queue);
            return false;
        }
    }

    {
        int unused_value;

        error_code =
            blocking_queue_pop(&queue, &unused_value);
    }

    if (error_code != ECANCELED) {
        fprintf(stderr,
                "Closed empty queue: expected ECANCELED, got %d\n",
                error_code);

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    return destroy_test_queue(
        &queue,
        "blocking push on full queue");
}

/**
 * @brief 验证空队列中的消费者能等待生产者写入数据。
 *
 * 测试先创建消费者线程。消费者调用pop时，如果队列仍为空，就会
 * 等待not_empty。主测试线程随后写入42并唤醒消费者。
 *
 * @return 消费者成功取得42且线程正常结束时返回true。
 */
static bool test_blocking_pop_on_empty_queue(void)
{
    blocking_queue_t queue;
    pthread_t consumer_thread;
    consumer_thread_context_t consumer_context;
    int error_code;

    if (!initialize_test_queue(&queue, "blocking pop on empty queue")) {
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
     * 如果消费者先运行，它会在空队列上等待not_empty；
     * 如果主线程先运行并写入42，消费者随后也能正常取出42。
     *
     * 两种调度顺序都必须得到相同结果。
     */
    error_code = pthread_create(&consumer_thread,
                                NULL,
                                consumer_thread_main,
                                &consumer_context);

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to create consumer thread: %s\n",
                strerror(error_code));

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    /*
     * 成功写入后，blocking_queue_push会signal not_empty。
     */
    error_code = blocking_queue_push(&queue, 42);
    if (error_code != 0) {
        fprintf(stderr,
                "Push for waiting consumer failed: %s\n",
                strerror(error_code));
        /*
         * close会广播not_empty，使可能仍在等待的消费者退出。
         */
        (void)blocking_queue_close(&queue);
        (void)pthread_join(consumer_thread, NULL);
        (void)blocking_queue_destroy(&queue);

        return false;
    }
    error_code = pthread_join(consumer_thread, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to join consumer thread: %s\n",
                strerror(error_code));

        return false;
    }

    if (consumer_context.error_code != 0) {
        fprintf(stderr,
                "Waiting consumer failed: %s\n",
                strerror(consumer_context.error_code));

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    if (consumer_context.value != 42) {
        fprintf(stderr,
                "Waiting consumer expected 42, got %d\n",
                consumer_context.value);

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    /*
     * 消费者已经取走唯一的数据。关闭队列后，后续pop应立即返回
     * ECANCELED，不会继续等待。
     */
    error_code = blocking_queue_close(&queue);
    if (error_code != 0) {
        fprintf(stderr,
                "Close after blocking pop failed: %s\n",
                strerror(error_code));

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    {
        int unused_value;

        error_code =
            blocking_queue_pop(&queue, &unused_value);
    }

    if (error_code != ECANCELED) {
        fprintf(stderr,
                "Pop after close: expected ECANCELED, got %d\n",
                error_code);

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    return destroy_test_queue(
        &queue,
        "blocking pop on empty queue");
}

/**
* @brief 验证循环队列跨过数组末尾后仍能正确复用空间。
*
* 测试先写满队列，取出两个元素，再写入两个元素。新元素应该复用
* 数组开头已经释放的位置，最终出队顺序仍然必须符合FIFO。
*
* @return 循环复用和FIFO顺序都正确时返回true，否则返回false。
*/
static bool test_queue_wraparound(void)
{
    const int initial_values[] = {10, 20, 30, 40};
    const int expected_remaining[] = {30, 40, 50, 60};
    const size_t initial_count =
        sizeof(initial_values) / sizeof(initial_values[0]);
    const size_t remaining_count =
        sizeof(expected_remaining) / sizeof(expected_remaining[0]);

    blocking_queue_t queue;

    if (!initialize_test_queue(&queue, "queue wraparound")) {
        return false;
    }

    /*
     * 第一次写满数组，此时tail应该循环回到下标0。
     */
    for (size_t index = 0; index < initial_count; index++) {
        const int error_code =
            blocking_queue_push(&queue, initial_values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "Initial push failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            return false;
        }
    }

    /*
     * 取出10和20，释放数组下标0和1。
     */
    for (size_t index = 0; index < 2U; index++) {
        int actual_value;
        const int error_code =
            blocking_queue_pop(&queue, &actual_value);

        if (error_code != 0) {
            fprintf(stderr,
                    "Initial pop failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            return false;
        }

        if (actual_value != initial_values[index]) {
            fprintf(stderr,
                    "Initial pop mismatch: expected %d, got %d\n",
                    initial_values[index],
                    actual_value);

            return false;
        }
    }

    /*
     * 写入50和60，tail将复用数组下标0和1。
     */
    {
        const int error_code = blocking_queue_push(&queue, 50);

        if (error_code != 0) {
            fprintf(stderr,
                    "Wraparound push 50 failed: %s\n",
                    strerror(error_code));

            return false;
        }
    }

    {
        const int error_code = blocking_queue_push(&queue, 60);

        if (error_code != 0) {
            fprintf(stderr,
                    "Wraparound push 60 failed: %s\n",
                    strerror(error_code));

            return false;
        }
    }

    /*
     * 此时队列逻辑顺序应该是30、40、50、60。
     * 队列已满时head和tail可以相等，由count区分满和空。
     */
    if (queue.count != QUEUE_CAPACITY ||
        queue.head != queue.tail) {
        fprintf(stderr,
                "Unexpected wrapped full state: "
                "head=%zu, tail=%zu, count=%zu\n",
                queue.head,
                queue.tail,
                queue.count);

        return false;
    }

    for (size_t index = 0; index < remaining_count; index++) {
        int actual_value;
        const int error_code =
            blocking_queue_pop(&queue, &actual_value);

        if (error_code != 0) {
            fprintf(stderr,
                    "Wrapped pop failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            return false;
        }

        if (actual_value != expected_remaining[index]) {
            fprintf(stderr,
                    "Wrapped FIFO mismatch at index %zu: "
                    "expected %d, got %d\n",
                    index,
                    expected_remaining[index],
                    actual_value);

            return false;
        }
    }

    if (queue.count != 0 ||
        queue.head != queue.tail) {
        fprintf(stderr,
                "Unexpected wrapped empty state: "
                "head=%zu, tail=%zu, count=%zu\n",
                queue.head,
                queue.tail,
                queue.count);

        return false;
    }

    return destroy_test_queue(&queue, "queue wraparound");
}

/**
* @brief 验证队列关闭后的完整生命周期。
*
* 关闭后禁止继续入队，但允许消费者取完关闭前已经存在的数据；
* 队列关闭且取空后，pop必须返回ECANCELED。
*
* @return 所有关闭行为符合接口约定时返回true，否则返回false。
*/
static bool test_queue_close(void)
{
    const int expected_values[] = {10, 20};
    const size_t value_count =
        sizeof(expected_values) / sizeof(expected_values[0]);

    blocking_queue_t queue;
    int error_code;

    if (!initialize_test_queue(&queue, "queue close")) {
        return false;
    }

    for (size_t index = 0; index < value_count; index++) {
        error_code =
            blocking_queue_push(&queue, expected_values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "Push before close failed: %s\n",
                    strerror(error_code));

            return false;
        }
    }

    error_code = blocking_queue_close(&queue);
    if (error_code != 0 || !queue.closed) {
        fprintf(stderr,
                "First close failed: error=%d, closed=%s\n",
                error_code,
                queue.closed ? "true" : "false");

        return false;
    }

    /*
     * close是幂等操作，重复关闭不应该报错或破坏状态。
     */
    error_code = blocking_queue_close(&queue);
    if (error_code != 0 || !queue.closed) {
        fprintf(stderr,
                "Repeated close failed: error=%d, closed=%s\n",
                error_code,
                queue.closed ? "true" : "false");

        return false;
    }

    error_code = blocking_queue_push(&queue, 30);
    if (error_code != ECANCELED) {
        fprintf(stderr,
                "Push after close: expected ECANCELED, got %d\n",
                error_code);

        return false;
    }

    /*
     * 关闭不会丢弃队列中已经存在的数据。
     */
    for (size_t index = 0; index < value_count; index++) {
        int actual_value;

        error_code = blocking_queue_pop(&queue, &actual_value);
        if (error_code != 0) {
            fprintf(stderr,
                    "Pop remaining value after close failed: %s\n",
                    strerror(error_code));

            return false;
        }

        if (actual_value != expected_values[index]) {
            fprintf(stderr,
                    "Close-drain mismatch: expected %d, got %d\n",
                    expected_values[index],
                    actual_value);

            return false;
        }
    }

    /*
     * 关闭且取空表示以后不会再有数据，消费者可以结束工作。
     */
    {
        int unused_value;

        error_code = blocking_queue_pop(&queue, &unused_value);
    }

    if (error_code != ECANCELED) {
        fprintf(stderr,
                "Pop from closed empty queue: "
                "expected ECANCELED, got %d\n",
                error_code);

        return false;
    }

    if (!queue.closed || queue.count != 0) {
        fprintf(stderr,
                "Unexpected final closed state: "
                "closed=%s, count=%zu\n",
                queue.closed ? "true" : "false",
                queue.count);

        return false;
    }

    return destroy_test_queue(&queue, "queue close");
}

/**
 * @brief 验证队列销毁接口的基本行为。
 *
 * @return 正常队列能够销毁且NULL参数被拒绝时返回true。
 */
static bool test_queue_destroy(void)
{
    blocking_queue_t queue;
    int error_code;

    if (!initialize_test_queue(&queue, "queue destroy")) {
        return false;
    }

    /*
     * 先写入数据并关闭队列，模拟一个实际使用过的队列。
     * destroy负责结束对象生命周期，不要求队列必须为空。
     *
     * 多线程版本中，调用者仍然必须保证所有工作线程已经退出。
     */
    error_code = blocking_queue_push(&queue, 10);
    if (error_code != 0) {
        fprintf(stderr,
                "Push before destroy failed: %s\n",
                strerror(error_code));

        return false;
    }

    error_code = blocking_queue_close(&queue);
    if (error_code != 0) {
        fprintf(stderr,
                "Close before destroy failed: %s\n",
                strerror(error_code));

        return false;
    }

    error_code = blocking_queue_destroy(&queue);
    if (error_code != 0) {
        fprintf(stderr,
                "Destroy initialized queue failed: %s\n",
                strerror(error_code));

        return false;
    }

    /*
     * NULL不指向有效队列，接口必须返回EINVAL而不是发生崩溃。
     */
    error_code = blocking_queue_destroy(NULL);
    if (error_code != EINVAL) {
        fprintf(stderr,
                "Destroy NULL: expected EINVAL, got %d\n",
                error_code);

        return false;
    }

    return true;
}


/**
 * @brief 验证一个生产者线程和一个消费者线程能够并发传输数据。
 *
 * 生产者线程依次写入0到999，当前测试线程同时读取数据并验证FIFO。
 * 队列关闭且取空后，消费者收到ECANCELED并结束。
 *
 * @return 线程、队列状态和全部数据都符合预期时返回true。
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

    if (!initialize_test_queue(&queue,
                               "concurrent producer-consumer")) {
        return false;
    }

    /*
     * 使用指定初始化器明确说明每个成员的含义。
     *
     * context的存储位于当前测试函数栈上，但测试会在函数返回前
     * pthread_join生产者，因此线程使用期间该对象始终有效。
     */
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
     *
     * pthread_create直接返回POSIX错误码，不通过errno报告错误。
     */
    error_code = pthread_create(&producer_thread,
                                NULL,
                                producer_thread_main,
                                &producer_context);

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to create producer thread: %s\n",
                strerror(error_code));

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    /*
    * 当前测试线程充当消费者。
    *
    * pop会在空队列上等待not_empty，因此这里不再进行EAGAIN轮询。
    * 生产者完成并关闭队列后，pop最终返回ECANCELED结束循环。
    */
    for (;;) {
        int actual_value;

        error_code =
            blocking_queue_pop(&queue, &actual_value);

        if (error_code == 0) {
            /*
             * 生产者按0、1、2……的顺序写入，因此消费者也必须按照
             * 完全相同的顺序读取。
             *
             * 即使发现顺序错误，也继续把队列取空，避免生产者因
             * 队列已满而无法结束。
             */
            if (actual_value != (int)received_count) {
                fifo_order_valid = false;
            }

            received_count++;
            continue;
        }

        if (error_code == ECANCELED) {
            /*
             * 队列已经关闭并且没有剩余数据，消费过程正常结束。
             */
            break;
        }

        /*
         * 其他错误不属于预期行为。
         *
         * 主动关闭队列，让生产者能够从重试循环中退出。
         */
        consumer_error = error_code;
        (void)blocking_queue_close(&queue);
        break;
    }

    /*
     * 等待生产者线程结束并回收其线程资源。
     *
     * pthread_join之后，主线程才能安全读取producer_context中的
     * 最终结果，并确认生产者不再访问queue。
     */
    error_code = pthread_join(producer_thread, NULL);
    if (error_code != 0) {
        fprintf(stderr,
                "Failed to join producer thread: %s\n",
                strerror(error_code));

        /*
         * join失败时无法确认线程是否已经停止，因此不能安全地
         * 销毁它可能仍在访问的队列。
         */
        return false;
    }

    if (producer_context.error_code != 0) {
        fprintf(stderr,
                "Producer thread failed: %s\n",
                strerror(producer_context.error_code));

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    if (consumer_error != 0) {
        fprintf(stderr,
                "Consumer failed: %s\n",
                strerror(consumer_error));

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    if (!fifo_order_valid) {
        fprintf(stderr,
                "Concurrent FIFO order check failed\n");

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    if (received_count != CONCURRENT_TEST_ITEM_COUNT) {
        fprintf(stderr,
                "Expected to receive %u values, but got %zu\n",
                CONCURRENT_TEST_ITEM_COUNT,
                received_count);

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    /*
     * 两个线程都结束后才能直接检查普通成员。
     *
     * 并发运行期间不能绕过队列接口直接读取这些成员。
     */
    if (!queue.closed || queue.count != 0) {
        fprintf(stderr,
                "Unexpected final concurrent state: "
                "closed=%s, count=%zu\n",
                queue.closed ? "true" : "false",
                queue.count);

        (void)blocking_queue_destroy(&queue);
        return false;
    }

    return destroy_test_queue(
        &queue,
        "concurrent producer-consumer");
}

/**
 * @brief 队列单元测试程序入口。
 *
 * 程序依次运行所有测试。任意测试失败都会把最终退出码设置为失败，
 * 但不会阻止后续测试继续运行。
 *
 * @return 所有测试通过时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    bool all_passed = true;

    if (test_queue_init()) {
        printf("[PASS] queue initialization\n");
    } else {
        fprintf(stderr, "[FAIL] queue initialization\n");
        all_passed = false;
    }

    if (test_partial_queue_fifo()) {
        printf("[PASS] partial queue FIFO\n");
    } else {
        fprintf(stderr, "[FAIL] partial queue FIFO\n");
        all_passed = false;
    }

    if (test_blocking_push_on_full_queue()) {
        printf("[PASS] blocking push on full queue\n");
    } else {
        fprintf(stderr, "[FAIL] blocking push on full queue\n");
        all_passed = false;
    }

    if (test_blocking_pop_on_empty_queue()) {
        printf("[PASS] blocking pop on empty queue\n");
    } else {
        fprintf(stderr, "[FAIL] blocking pop on empty queue\n");
        all_passed = false;
    }

    if (test_queue_wraparound()) {
        printf("[PASS] queue wraparound\n");
    } else {
        fprintf(stderr, "[FAIL] queue wraparound\n");
        all_passed = false;
    }

    if (test_queue_close()) {
        printf("[PASS] queue close lifecycle\n");
    } else {
        fprintf(stderr, "[FAIL] queue close lifecycle\n");
        all_passed = false;
    }
    
    if (test_queue_destroy()) {
        printf("[PASS] queue destruction\n");
    } else {
        fprintf(stderr, "[FAIL] queue destruction\n");
        all_passed = false;
    }

    if (test_concurrent_producer_consumer()) {
        printf("[PASS] concurrent producer-consumer\n");
    } else {
        fprintf(stderr,
                "[FAIL] concurrent producer-consumer\n");
        all_passed = false;
    }

    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;

}


