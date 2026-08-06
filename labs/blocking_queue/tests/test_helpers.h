#ifndef BLOCKING_QUEUE_TEST_HELPERS_H
#define BLOCKING_QUEUE_TEST_HELPERS_H

#include "queue.h"

#include <stdbool.h>

/**
 * @brief 初始化一个测试场景使用的队列。
 *
 * 该函数统一处理blocking_queue_init的错误输出，减少各个测试文件中
 * 重复的初始化代码。
 *
 * @param queue 指向待初始化的队列，不能为NULL。
 * @param test_name 当前测试名称，用于输出便于定位的错误信息。
 *
 * @return 初始化成功时返回true，否则返回false。
 */
bool initialize_test_queue(blocking_queue_t *queue,
                           const char *test_name);

/**
 * @brief 销毁一个测试场景使用的队列。
 *
 * 调用该函数前，必须保证所有使用队列的工作线程都已经结束。
 *
 * @param queue 指向已经初始化的队列，不能为NULL。
 * @param test_name 当前测试名称，用于输出便于定位的错误信息。
 *
 * @return 销毁成功时返回true，否则返回false。
 */
bool destroy_test_queue(blocking_queue_t *queue,
                        const char *test_name);
                    



#endif