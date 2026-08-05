#include "queue.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

    return true;
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

    return true;
}

/**
 * @brief 验证队列写满后不能继续写入。
 *
 * @return 第五次入队返回ENOSPC且队列状态未被破坏时返回true。
 */
static bool test_full_queue(void)
{
    blocking_queue_t queue;

    if (!initialize_test_queue(&queue, "full queue")) {
        return false;
    }

    /*
     * 写入恰好等于QUEUE_CAPACITY数量的元素，使队列达到满状态。
     */
    for (size_t index = 0; index < QUEUE_CAPACITY; index++) {
        const int value = (int)(index + 1U);
        const int error_code =
            blocking_queue_push(&queue, value);

        if (error_code != 0) {
            fprintf(stderr,
                    "Push before queue was full failed "
                    "at index %zu: %s\n",
                    index,
                    strerror(error_code));

            return false;
        }
    }

    if (queue.count != QUEUE_CAPACITY) {
        fprintf(stderr,
                "Expected full count=%u, got %zu\n",
                QUEUE_CAPACITY,
                queue.count);

        return false;
    }

    /*
    * 队列已经装满，第五次入队必须被拒绝。
    */
    {
        const int error_code =
            blocking_queue_push(&queue, 999);

        if (error_code != ENOSPC) {
            fprintf(stderr,
                    "Push to full queue: expected ENOSPC, got %d\n",
                    error_code);

            return false;
        }
    }

    /*
     * 失败的入队不能改变已有元素数量。
     */
    if (queue.count != QUEUE_CAPACITY) {
        fprintf(stderr,
                "Rejected push changed count: expected %u, got %zu\n",
                QUEUE_CAPACITY,
                queue.count);

        return false;
    }

    return true;
}

/**
 * @brief 验证未关闭的空队列不能读取数据。
 *
 * @return pop返回EAGAIN且没有修改输出参数时返回true。
 */
static bool test_empty_queue(void)
{
    blocking_queue_t queue;
    int output_value = 12345;
    int error_code;

    if (!initialize_test_queue(&queue, "empty queue")) {
        return false;
    }

    error_code = blocking_queue_pop(&queue, &output_value);

    if (error_code != EAGAIN) {
        fprintf(stderr,
                "Pop from empty queue: expected EAGAIN, got %d\n",
                error_code);

        return false;
    }

    /*
     * pop失败时不能修改调用者提供的输出变量。
     */
    if (output_value != 12345) {
        fprintf(stderr,
                "Failed pop modified output value: expected 12345, got %d\n",
                output_value);

        return false;
    }

    if (queue.count != 0 ||
        queue.head != 0 ||
        queue.tail != 0 ||
        queue.closed) {
        fprintf(stderr,
                "Empty pop changed queue state: "
                "head=%zu, tail=%zu, count=%zu, closed=%s\n",
                queue.head,
                queue.tail,
                queue.count,
                queue.closed ? "true" : "false");

        return false;
    }

    return true;
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

    return true;
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

    return true;
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

    if (test_full_queue()) {
        printf("[PASS] full queue protection\n");
    } else {
        fprintf(stderr, "[FAIL] full queue protection\n");
        all_passed = false;
    }

    if (test_empty_queue()) {
        printf("[PASS] empty queue protection\n");
    } else {
        fprintf(stderr, "[FAIL] empty queue protection\n");
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
    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;

        if (test_queue_destroy()) {
        printf("[PASS] queue destruction\n");
    } else {
        fprintf(stderr, "[FAIL] queue destruction\n");
        all_passed = false;
    }
}


