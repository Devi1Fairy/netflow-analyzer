#include "queue.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
