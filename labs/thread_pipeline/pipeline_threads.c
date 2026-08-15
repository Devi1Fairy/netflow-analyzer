#include "pipeline_threads.h"

#include "pipeline_types.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void *producer_thread_main(void *argument)
{
    producer_context_t *context = (producer_context_t *)argument;

    int close_error;

    /*
     * pthread_create由本项目控制，正常情况下argument一定不是NULL。
     *
     * 仍然进行检查，避免错误参数导致空指针解引用。
     */
    if (context == NULL) {
        return NULL;
    }

    context->error_code = 0;

    for (size_t task_index = 0U; task_index < context->task_count; task_index++) {
        /*
         * 动态分配一个任务对象。
         *
         * sizeof(*task)会根据task实际指向的类型计算大小。
         * 即使以后修改task的类型，也不需要同步修改sizeof中的类型名。
         */
        pipeline_task_t *task = malloc(sizeof(*task));

        if (task == NULL) {
            context->error_code = ENOMEM;
            break;
        }

        /*
         * task_id从1开始，便于阅读。
         */
        task->task_id = (uint64_t)task_index + UINT64_C(1);

        /*
         * 当前使用1、2、3……作为模拟输入。
         *
         * task_count由当前程序控制，因此这里的转换不会超出uint32_t
         * 的表示范围。
         */
        task->input_value = (uint32_t)task_index + UINT32_C(1);

        /*
         * push之前，task归生产者所有。
         */
        context->error_code = pointer_queue_push(context->task_queue, task);

        if (context->error_code != 0) {
            /*
             * push失败表示所有权没有转移，生产者仍然负责释放task。
             */
            free(task);
            break;
        }

        /*
         * push成功后，任务所有权已经转移到队列。
         *
         * 生产者不能再访问或释放task，因为工作线程可能已经取出并
         * 释放了这个对象。
         */
        task = NULL;
    }

    /*
     * 无论正常完成还是中途失败，都必须关闭任务队列。
     *
     * 关闭操作会唤醒正在等待任务的工作线程。工作线程取完已有任务后，
     * 会收到ECANCELED并正常退出。
     */
    close_error = pointer_queue_close(context->task_queue);

    if (context->error_code == 0 && close_error != 0) {
        context->error_code = close_error;
    }

    return NULL;
}

void *worker_thread_main(void *argument)
{
    worker_context_t *context = (worker_context_t *)argument;

    if (context == NULL) {
        return NULL;
    }

    context->error_code = 0;

    for (;;) {
        void *raw_item = NULL;

        pipeline_task_t *task;
        pipeline_result_t *result;

        int pop_error;
        int push_error;

        pop_error = pointer_queue_pop(context->task_queue, &raw_item);

        if (pop_error == ECANCELED) {
            /*
             * ECANCELED表示任务队列已经关闭，并且所有已有任务都已经
             * 被取完。这不是故障，而是正常的结束信号。
             */
            break;
        }

        if (pop_error != 0) {
            context->error_code = pop_error;
            break;
        }

        /*
         * pop成功后，任务对象的所有权转移给当前工作线程。
         */
        task = (pipeline_task_t *)raw_item;

        /*
         * 为处理结果单独分配内存。
         *
         * 任务和结果是两个不同生命周期的对象：
         *
         * - task在计算完成后即可释放；
         * - result需要继续通过结果队列传给输出线程。
         */
        result = malloc(sizeof(*result));

        if (result == NULL) {
            free(task);
            context->error_code = ENOMEM;
            break;
        }

        result->task_id = task->task_id;
        result->input_value = task->input_value;

        /*
         * 先转换为uint64_t，再进行乘法。
         *
         * 如果两个uint32_t直接相乘，运算可能先在32位范围内溢出。
         */
        result->output_value = (uint64_t)task->input_value * (uint64_t)task->input_value;

        result->worker_index = context->worker_index;

        /*
         * 任务内容已经复制到结果中，当前任务不再需要。
         *
         * pop成功后任务属于工作线程，所以由工作线程释放。
         */
        free(task);
        task = NULL;

        /*
         * push之前，结果对象属于工作线程。
         */
        push_error = pointer_queue_push(context->result_queue, result);

        if (push_error != 0) {
            /*
             * push失败表示结果没有进入队列，仍由工作线程释放。
             */
            free(result);
            context->error_code = push_error;
            break;
        }

        /*
         * push成功后，结果所有权转移给结果队列。
         */
        result = NULL;
    }

    /*
     * 工作线程不能关闭result_queue。
     *
     * 因为另一个工作线程可能仍然在处理任务。由主线程等待全部工作
     * 线程退出以后，再统一关闭结果队列。
     */
    return NULL;
}

void *output_thread_main(void *argument)
{
    output_context_t *context = (output_context_t *)argument;

    FILE *output_file = NULL;

    if (context == NULL) {
        return NULL;
    }

    context->output_count = 0U;
    context->written_count = 0U;
    context->error_code = 0;

    /*
     * result_queue和output_path都是输出线程运行所必需的参数。
     */
    if (context->result_queue == NULL || context->output_path == NULL) {
        context->error_code = EINVAL;
        return NULL;
    }

    /*
     * 使用"w"模式创建CSV：
     *
     * - 文件不存在：创建新文件；
     * - 文件已经存在：清空旧内容并重新写入。
     *
     * FILE *不是文件内容本身，而是C标准库管理文件流时使用的句柄。
     * 文件流内部通常还包含缓冲区和底层文件描述符。
     */
    output_file = fopen(context->output_path, "w");

    if (output_file == NULL) {
        /*
         * fopen失败时，errno保存具体错误原因，例如：
         *
         * - EACCES：没有写权限；
         * - ENOENT：路径中的目录不存在；
         * - EMFILE：进程打开的文件数量达到限制。
         *
         * 这里不能直接退出线程，否则没有消费者继续取结果，工作线程
         * 可能阻塞在已满的result_queue中。
         */
        context->error_code = errno != 0 ? errno : EIO;
    } else {
        /*
         * CSV第一行是字段名称。
         *
         * 统一的表头让Qt、Python和其他程序知道每一列的含义。
         */
        if (fprintf(output_file, "task_id,input_value," "output_value,worker_index\n") < 0) {
            context->error_code = EIO;

            /*
             * 表头写入失败后，后续数据已经无法形成有效CSV。
             * 关闭文件，但仍然继续排空结果队列。
             */
            (void)fclose(output_file);
            output_file = NULL;
        }
    }

    for (;;) {
        void *raw_item = NULL;

        pipeline_result_t *result;

        int pop_error;

        pop_error = pointer_queue_pop(context->result_queue, &raw_item);

        if (pop_error == ECANCELED) {
            /*
             * 结果队列已经关闭，并且全部结果都已取完。
             */
            break;
        }

        if (pop_error != 0) {
            if (context->error_code == 0) {
                context->error_code = pop_error;
            }

            break;
        }

        /*
         * pop成功后，结果对象的所有权转移给输出线程。
         */
        result = (pipeline_result_t *)raw_item;

        context->output_count++;

        if (output_file != NULL) {
            /*
             * 每个fprintf对应CSV中的一行数据。
             *
             * PRIu64和PRIu32用于可移植地输出固定宽度整数；
             * worker_index是size_t，因此使用%zu。
             */
            if (fprintf(output_file,
                        "%" PRIu64
                        ",%" PRIu32
                        ",%" PRIu64
                        ",%zu\n",
                        result->task_id,
                        result->input_value,
                        result->output_value,
                        result->worker_index) < 0) {
                if (context->error_code == 0) {
                    context->error_code = EIO;
                }

                /*
                 * 一旦写入失败，继续写同一个文件通常没有意义。
                 *
                 * 关闭文件以后仍继续pop并free，保证流水线可以退出。
                 */
                (void)fclose(output_file);
                output_file = NULL;
            } else {
                context->written_count++;
            }
        }

        /*
         * 输出线程是结果对象的最终消费者。
         */
        free(result);
        result = NULL;
    }

    if (output_file != NULL) {
        /*
         * fclose会把C标准库缓冲区中的剩余数据刷新到底层文件，然后释放
         * FILE对象及其相关资源。
         */
        if (fclose(output_file) != 0 && context->error_code == 0) {
            context->error_code = errno != 0 ? errno : EIO;
        }

        output_file = NULL;
    }

    return NULL;
}