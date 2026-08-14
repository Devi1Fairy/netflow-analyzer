#include "pipeline_types.h"

/*
 * PRIu32和PRIu64定义在inttypes.h中。
 *
 * 它们用于可移植地打印uint32_t和uint64_t，避免假设这些类型一定
 * 对应unsigned int或unsigned long。
 */
#include <inttypes.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief A3数据模型演示入口。
 *
 * 当前程序模拟：
 *
 * 1. 生产线程创建任务；
 * 2. 处理线程计算输入值的平方；
 * 3. 输出线程显示处理结果。
 *
 * 下一步再把这三个连续操作放入不同线程，并通过队列传递对象。
 *
 * @return 数据模型演示完成时返回EXIT_SUCCESS。
 */
int main(void)
{
    /*
     * const表示task完成初始化后不再被修改。
     *
     * 未来生产线程创建任务后，也不会继续修改它，而是把任务所有权
     * 交给任务队列。
     */
    const pipeline_task_t task = {
        .task_id = UINT64_C(1),
        .input_value = UINT32_C(12)
    };

    /*
     * 当前模拟由下标0的处理线程完成任务。
     */
    const size_t worker_index = 0U;

    /*
     * 计算前先把input_value转换为uint64_t。
     *
     * 如果直接执行：
     *
     *     task.input_value * task.input_value
     *
     * 乘法可能先以32位完成并溢出，然后才把已经错误的结果保存到
     * uint64_t。
     *
     * 先转换操作数，乘法就会在64位范围内完成。
     */
    const uint64_t output_value =
        (uint64_t)task.input_value *
        (uint64_t)task.input_value;

    /*
     * 使用指定成员初始化器构造处理结果。
     */
    const pipeline_result_t result = {
        .task_id = task.task_id,
        .input_value = task.input_value,
        .output_value = output_value,
        .worker_index = worker_index
    };

    printf("Task created:\n");

    printf("  task_id=%" PRIu64 "\n",
           task.task_id);

    printf("  input_value=%" PRIu32 "\n",
           task.input_value);

    printf("Task processed:\n");

    printf("  task_id=%" PRIu64 "\n",
           result.task_id);

    printf("  input_value=%" PRIu32 "\n",
           result.input_value);

    printf("  output_value=%" PRIu64 "\n",
           result.output_value);

    /*
     * worker_index是size_t，因此使用%zu输出。
     */
    printf("  worker_index=%zu\n",
           result.worker_index);

    return EXIT_SUCCESS;
}