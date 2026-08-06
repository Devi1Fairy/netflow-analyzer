#include "queue.h"
#include "test_helpers.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 验证队列初始化后的普通数据成员。
 *
 * 新队列应该为空，head和tail从下标0开始，closed应该为false。
 *
 * @return 初始状态正确时返回true，否则返回false。
 */
static bool test_queue_initialization(void)
{
    blocking_queue_t queue;

    if (!initialize_test_queue(&queue, "queue initialization")) {
        return false;
    }

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

        (void)destroy_test_queue(&queue, "queue initialization");
        return false;
    }

    /*
     * 当前blocking_queue_init会把items数组初始化为0。
     */
    for (size_t index = 0; index < QUEUE_CAPACITY; index++) {
        if (queue.items[index] != 0) {
            fprintf(stderr,
                    "items[%zu] should be 0, but got %d\n",
                    index,
                    queue.items[index]);

            (void)destroy_test_queue(&queue,
                                     "queue initialization");
            return false;
        }
    }

    return destroy_test_queue(&queue, "queue initialization");
}

/**
 * @brief 验证未填满的队列仍然遵守FIFO顺序。
 *
 * 测试向容量为4的队列中写入10、20、30，然后按照相同顺序取出。
 * 这可以证明队列依据count判断有效数据量，不要求队列必须填满。
 *
 * @return 入队和出队顺序正确时返回true，否则返回false。
 */
static bool test_partial_queue_fifo(void)
{
    const int expected_values[] = {10, 20, 30};
    const size_t value_count =
        sizeof(expected_values) / sizeof(expected_values[0]);

    blocking_queue_t queue;

    if (!initialize_test_queue(&queue, "partial queue FIFO")) {
        return false;
    }

    /*
     * Arrange：准备一个只包含三个元素的队列。
     */
    for (size_t index = 0; index < value_count; index++) {
        const int error_code =
            blocking_queue_push(&queue, expected_values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "Push failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            (void)destroy_test_queue(&queue,
                                     "partial queue FIFO");
            return false;
        }
    }

    /*
     * Assert：写入三个元素后，count必须等于3。
     */
    if (queue.count != value_count) {
        fprintf(stderr,
                "Expected count=%zu, but got %zu\n",
                value_count,
                queue.count);

        (void)destroy_test_queue(&queue, "partial queue FIFO");
        return false;
    }

    /*
     * Act和Assert：依次取出元素并检查FIFO顺序。
     */
    for (size_t index = 0; index < value_count; index++) {
        int actual_value;
        const int error_code =
            blocking_queue_pop(&queue, &actual_value);

        if (error_code != 0) {
            fprintf(stderr,
                    "Pop failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            (void)destroy_test_queue(&queue,
                                     "partial queue FIFO");
            return false;
        }

        if (actual_value != expected_values[index]) {
            fprintf(stderr,
                    "FIFO mismatch at index %zu: "
                    "expected=%d, actual=%d\n",
                    index,
                    expected_values[index],
                    actual_value);

            (void)destroy_test_queue(&queue,
                                     "partial queue FIFO");
            return false;
        }
    }

    /*
     * 写入和取出的数量相同，队列最终应该为空。
     *
     * 空队列要求head和tail指向同一位置，但不要求它们一定等于0。
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

        (void)destroy_test_queue(&queue, "partial queue FIFO");
        return false;
    }

    return destroy_test_queue(&queue, "partial queue FIFO");
}

/**
 * @brief 验证循环队列能够复用数组开头的空间。
 *
 * 测试过程：
 *
 * 1. 写入10、20、30、40，填满队列；
 * 2. 取出10和20；
 * 3. 写入50和60；
 * 4. 验证剩余顺序为30、40、50、60。
 *
 * @return 环形下标和FIFO顺序正确时返回true，否则返回false。
 */
static bool test_queue_wraparound(void)
{
    const int initial_values[] = {10, 20, 30, 40};
    const int wrapped_values[] = {50, 60};
    const int expected_values[] = {30, 40, 50, 60};

    const size_t initial_count =
        sizeof(initial_values) / sizeof(initial_values[0]);
    const size_t wrapped_count =
        sizeof(wrapped_values) / sizeof(wrapped_values[0]);
    const size_t expected_count =
        sizeof(expected_values) / sizeof(expected_values[0]);

    blocking_queue_t queue;

    if (!initialize_test_queue(&queue, "queue wraparound")) {
        return false;
    }

    /*
     * 第一次写满队列。完成后tail会从数组末尾回到下标0。
     */
    for (size_t index = 0; index < initial_count; index++) {
        const int error_code =
            blocking_queue_push(&queue, initial_values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "Initial push failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            (void)destroy_test_queue(&queue, "queue wraparound");
            return false;
        }
    }

    /*
     * 取出10和20，释放数组中的前两个位置。
     */
    for (size_t index = 0; index < 2U; index++) {
        int actual_value;
        const int error_code =
            blocking_queue_pop(&queue, &actual_value);

        if (error_code != 0 ||
            actual_value != initial_values[index]) {
            fprintf(stderr,
                    "Initial pop mismatch at index %zu: "
                    "expected=%d, actual=%d, error=%d\n",
                    index,
                    initial_values[index],
                    actual_value,
                    error_code);

            (void)destroy_test_queue(&queue, "queue wraparound");
            return false;
        }
    }

    /*
     * 50和60将复用数组开头已经释放的两个位置。
     */
    for (size_t index = 0; index < wrapped_count; index++) {
        const int error_code =
            blocking_queue_push(&queue, wrapped_values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "Wrapped push failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            (void)destroy_test_queue(&queue, "queue wraparound");
            return false;
        }
    }

    /*
     * 队列再次填满。
     *
     * 在环形队列中，满队列和空队列都可能出现head == tail，
     * 因此必须依靠count区分这两种状态。
     */
    if (queue.count != QUEUE_CAPACITY ||
        queue.head != queue.tail) {
        fprintf(stderr,
                "Unexpected wrapped full state: "
                "head=%zu, tail=%zu, count=%zu\n",
                queue.head,
                queue.tail,
                queue.count);

        (void)destroy_test_queue(&queue, "queue wraparound");
        return false;
    }

    /*
     * 逻辑顺序必须仍然是30、40、50、60。
     */
    for (size_t index = 0; index < expected_count; index++) {
        int actual_value;
        const int error_code =
            blocking_queue_pop(&queue, &actual_value);

        if (error_code != 0 ||
            actual_value != expected_values[index]) {
            fprintf(stderr,
                    "Wrapped FIFO mismatch at index %zu: "
                    "expected=%d, actual=%d, error=%d\n",
                    index,
                    expected_values[index],
                    actual_value,
                    error_code);

            (void)destroy_test_queue(&queue, "queue wraparound");
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

        (void)destroy_test_queue(&queue, "queue wraparound");
        return false;
    }

    return destroy_test_queue(&queue, "queue wraparound");
}

/**
 * @brief 基础队列测试程序入口。
 *
 * @return 所有基础测试通过时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    bool all_passed = true;

    if (test_queue_initialization()) {
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

    if (test_queue_wraparound()) {
        printf("[PASS] queue wraparound\n");
    } else {
        fprintf(stderr, "[FAIL] queue wraparound\n");
        all_passed = false;
    }

    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}