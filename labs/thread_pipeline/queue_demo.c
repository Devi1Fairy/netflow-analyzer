#include "pipeline_types.h"
#include "pointer_queue.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 通用指针阻塞队列演示入口。
 *
 * 当前演示：
 *
 * 1. 创建一个pipeline_task_t；
 * 2. 把任务地址写入void *队列；
 * 3. 从队列取出void *；
 * 4. 转换回pipeline_task_t *；
 * 5. 根据任务生成pipeline_result_t；
 * 6. 关闭并销毁队列。
 *
 * @return 全部操作成功时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    pointer_queue_t task_queue;

    /*
     * 当前演示使用栈对象，只用于隔离验证指针队列。
     *
     * 对象的生命周期覆盖整个main函数，因此它在队列中时仍然有效。
     * 下一步生产线程会改用malloc动态创建任务。
     */
    pipeline_task_t task = {
        .task_id = UINT64_C(1),
        .input_value = UINT32_C(12)
    };

    void *raw_item = NULL;
    pipeline_task_t *received_task;

    int error_code;

    error_code = pointer_queue_init(&task_queue);

    if (error_code != 0) {
        fprintf(stderr, "pointer_queue_init failed: %s\n", strerror(error_code));

        return EXIT_FAILURE;
    }

    /*
     * &task的类型是pipeline_task_t *。
     *
     * 在C语言中，对象指针可以转换成void *保存。
     */
    error_code = pointer_queue_push(&task_queue, &task);

    if (error_code != 0) {
        fprintf(stderr, "pointer_queue_push failed: %s\n",strerror(error_code));

        (void)pointer_queue_close(&task_queue);

        (void)pointer_queue_destroy(&task_queue);

        return EXIT_FAILURE;
    }

    printf("Task pointer pushed into queue.\n");

    /*
     * pop通过void **输出取出的通用指针。
     */
    error_code =  pointer_queue_pop(&task_queue, &raw_item);

    if (error_code != 0) {
        fprintf(stderr, "pointer_queue_pop failed: %s\n", strerror(error_code));

        (void)pointer_queue_close(&task_queue);

        return EXIT_FAILURE;
    }

    /*
     * 队列不知道实际类型，取出后由调用者恢复为pipeline_task_t *。
     */
    received_task = (pipeline_task_t *)raw_item;

    const pipeline_result_t result = {
        .task_id = received_task->task_id,
        .input_value = received_task->input_value,
        .output_value =(uint64_t)received_task->input_value * (uint64_t)received_task->input_value,
        .worker_index = 0U
    };

    printf("Task pointer popped from queue:\n");

    printf("  task_id=%" PRIu64 "\n",
           received_task->task_id);

    printf("  input_value=%" PRIu32 "\n",
           received_task->input_value);

    printf("Generated result:\n");

    printf("  output_value=%" PRIu64 "\n",
           result.output_value);

    error_code =
        pointer_queue_close(&task_queue);

    if (error_code != 0) {
        fprintf(stderr,
                "pointer_queue_close failed: %s\n",
                strerror(error_code));

        return EXIT_FAILURE;
    }

    error_code =
        pointer_queue_destroy(&task_queue);

    if (error_code != 0) {
        fprintf(stderr,
                "pointer_queue_destroy failed: %s\n",
                strerror(error_code));

        return EXIT_FAILURE;
    }

    printf("Pointer queue destroyed successfully.\n");

    return EXIT_SUCCESS;
}