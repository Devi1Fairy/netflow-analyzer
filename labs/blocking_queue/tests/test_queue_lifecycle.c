#include "queue.h"
#include "test_helpers.h"

#include <errno.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 验证队列关闭后的完整生命周期。
 *
 * 关闭后禁止继续写入，但关闭前已经存在的数据仍然可以被取出。
 * 队列关闭且取空后，pop必须返回ECANCELED。
 *
 * @return 所有关闭行为符合约定时返回true，否则返回false。
 */
static bool test_queue_close_lifecycle(void)
{
    const int expected_values[] = {10, 20, 30};
    const size_t value_count =
        sizeof(expected_values) / sizeof(expected_values[0]);

    blocking_queue_t queue;
    int error_code;

    if (!initialize_test_queue(&queue, "queue close lifecycle")) {
        return false;
    }

    /*
     * 关闭前先写入三个元素。
     */
    for (size_t index = 0; index < value_count; index++) {
        error_code =
            blocking_queue_push(&queue, expected_values[index]);

        if (error_code != 0) {
            fprintf(stderr,
                    "Push before close failed at index %zu: %s\n",
                    index,
                    strerror(error_code));

            (void)destroy_test_queue(
                &queue,
                "queue close lifecycle");

            return false;
        }
    }

    error_code = blocking_queue_close(&queue);
    if (error_code != 0 || !queue.closed) {
        fprintf(stderr,
                "First close failed: error=%d, closed=%s\n",
                error_code,
                queue.closed ? "true" : "false");

        (void)destroy_test_queue(
            &queue,
            "queue close lifecycle");

        return false;
    }

    /*
     * close应该具有幂等性。
     *
     * 幂等表示同一个关闭操作执行多次，最终状态与执行一次相同。
     */
    error_code = blocking_queue_close(&queue);
    if (error_code != 0 || !queue.closed) {
        fprintf(stderr,
                "Repeated close failed: error=%d, closed=%s\n",
                error_code,
                queue.closed ? "true" : "false");

        (void)destroy_test_queue(
            &queue,
            "queue close lifecycle");

        return false;
    }

    /*
     * 关闭后禁止继续写入。
     */
    error_code = blocking_queue_push(&queue, 40);
    if (error_code != ECANCELED) {
        fprintf(stderr,
                "Push after close: expected ECANCELED, got %d\n",
                error_code);

        (void)destroy_test_queue(
            &queue,
            "queue close lifecycle");

        return false;
    }

    /*
     * 关闭前已经写入的数据仍然可以按照FIFO顺序取出。
     */
    for (size_t index = 0; index < value_count; index++) {
        int actual_value;

        error_code = blocking_queue_pop(&queue, &actual_value);

        if (error_code != 0 ||
            actual_value != expected_values[index]) {
            fprintf(stderr,
                    "Close-drain mismatch at index %zu: "
                    "expected=%d, actual=%d, error=%d\n",
                    index,
                    expected_values[index],
                    actual_value,
                    error_code);

            (void)destroy_test_queue(
                &queue,
                "queue close lifecycle");

            return false;
        }
    }

    /*
     * 队列已经关闭且没有剩余数据，不应该继续等待新数据。
     */
    {
        int unused_value;

        error_code =
            blocking_queue_pop(&queue, &unused_value);
    }

    if (error_code != ECANCELED) {
        fprintf(stderr,
                "Pop from closed empty queue: "
                "expected ECANCELED, got %d\n",
                error_code);

        (void)destroy_test_queue(
            &queue,
            "queue close lifecycle");

        return false;
    }

    if (!queue.closed || queue.count != 0) {
        fprintf(stderr,
                "Unexpected final closed state: "
                "closed=%s, count=%zu\n",
                queue.closed ? "true" : "false",
                queue.count);

        (void)destroy_test_queue(
            &queue,
            "queue close lifecycle");

        return false;
    }

    return destroy_test_queue(&queue, "queue close lifecycle");
}

/**
 * @brief 验证队列销毁接口的基本行为。
 *
 * destroy负责结束队列对象的生命周期。调用前必须确保没有线程继续
 * 使用队列，但不要求队列一定为空。
 *
 * @return 正常队列能够销毁且NULL参数被拒绝时返回true。
 */
static bool test_queue_destruction(void)
{
    blocking_queue_t queue;
    int error_code;

    if (!initialize_test_queue(&queue, "queue destruction")) {
        return false;
    }

    /*
     * 写入一个元素并关闭，模拟一个真正使用过的队列。
     */
    error_code = blocking_queue_push(&queue, 10);
    if (error_code != 0) {
        fprintf(stderr,
                "Push before destroy failed: %s\n",
                strerror(error_code));

        (void)destroy_test_queue(&queue, "queue destruction");
        return false;
    }

    error_code = blocking_queue_close(&queue);
    if (error_code != 0) {
        fprintf(stderr,
                "Close before destroy failed: %s\n",
                strerror(error_code));

        (void)destroy_test_queue(&queue, "queue destruction");
        return false;
    }

    /*
     * 当前没有任何工作线程使用队列，因此可以安全销毁。
     *
     * 即使队列中还保留一个没有取出的元素，也不影响同步资源的销毁。
     */
    error_code = blocking_queue_destroy(&queue);
    if (error_code != 0) {
        fprintf(stderr,
                "Destroy initialized queue failed: %s\n",
                strerror(error_code));

        return false;
    }

    /*
     * NULL不指向有效队列，接口必须返回EINVAL而不是崩溃。
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
 * @brief 队列生命周期测试程序入口。
 *
 * @return 所有生命周期测试通过时返回EXIT_SUCCESS，
 *         否则返回EXIT_FAILURE。
 */
int main(void)
{
    bool all_passed = true;

    if (test_queue_close_lifecycle()) {
        printf("[PASS] queue close lifecycle\n");
    } else {
        fprintf(stderr, "[FAIL] queue close lifecycle\n");
        all_passed = false;
    }

    if (test_queue_destruction()) {
        printf("[PASS] queue destruction\n");
    } else {
        fprintf(stderr, "[FAIL] queue destruction\n");
        all_passed = false;
    }

    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}