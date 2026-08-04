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
 * @brief 队列单元测试程序入口。
 *
 * @return 所有测试通过时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    if (!test_queue_init()) {
        fprintf(stderr, "[FAIL] queue initialization\n");
        return EXIT_FAILURE;
    }

    printf("[PASS] queue initialization\n");
    return EXIT_SUCCESS;
}
