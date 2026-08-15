#include "pipeline_threads.h"
#include "pointer_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 * strtoumax和uintmax_t定义在inttypes.h中。
 *
 * strtoumax用于把命令行字符串安全地转换成无符号整数。
 */
#include <inttypes.h>
/*
 * 用户没有提供任务数量时，默认创建12个任务。
 */
#define DEFAULT_PIPELINE_TASK_COUNT 12U

/*
 * 限制单次运行的最大任务数量。
 *
 * 防止错误参数让程序创建过大的CSV文件并长时间占用系统资源。
 * 一百万的平方仍然可以安全保存在uint64_t中。
 */
#define MAX_PIPELINE_TASK_COUNT 1000000U

/*
 * 使用两个工作线程并行处理任务。
 */
#define PIPELINE_WORKER_COUNT 2U

/**
 * @brief 把命令行字符串转换成任务数量。
 *
 * 合法输入必须满足：
 *
 * - 只包含十进制数字；
 * - 大于0；
 * - 不超过MAX_PIPELINE_TASK_COUNT。
 *
 * @param text 命令行提供的字符串。
 * @param task_count 用于保存转换结果的输出参数。
 *
 * @return 成功时返回0；
 *         格式错误时返回EINVAL；
 *         数值超出允许范围时返回ERANGE。
 */
static int parse_task_count(const char *text, size_t *task_count)
{
    char *end = NULL;

    uintmax_t parsed_value;

    if (text == NULL || task_count == NULL) {
        return EINVAL;
    }

    /*
     * 严格要求第一个字符是数字。
     *
     * 这样可以拒绝负数、正号、空格和空字符串。
     */
    if (text[0] < '0' || text[0] > '9') {
        return EINVAL;
    }

    /*
     * strtoumax失败时通过errno报告ERANGE。
     */
    errno = 0;

    parsed_value = strtoumax(text, &end, 10);

    if (errno == ERANGE) {
        return ERANGE;
    }

    /*
     * end应该正好指向字符串结尾。
     *
     * 例如"100abc"只能转换前面的100，因此必须判为无效。
     */
    if (end == text || *end != '\0') {
        return EINVAL;
    }

    if (parsed_value == 0U) {
        return EINVAL;
    }

    if (parsed_value > (uintmax_t)MAX_PIPELINE_TASK_COUNT) {
        return ERANGE;
    }

    *task_count = (size_t)parsed_value;

    return 0;
}

/**
 * @brief 输出带POSIX错误信息的错误日志。
 *
 * @param operation 失败的操作名称。
 * @param error_code pthread函数或队列函数返回的错误码。
 */
static void report_error(const char *operation, int error_code)
{
    fprintf(stderr, "%s failed: %s\n", operation, strerror(error_code));
}

/**
 * @brief 释放关闭队列中可能残留的动态对象，并销毁队列。
 *
 * 正常流程下，队列应该已经取空。这个函数仍然进行兜底清理，避免某个
 * 工作线程中途失败时，队列中残留的对象发生内存泄漏。
 *
 * @param queue 指向不再被任何线程使用的队列。
 * @param queue_name 用于错误日志的队列名称。
 *
 * @return 全部清理成功时返回0，否则返回遇到的第一个错误码。
 */
static int clean_up_queue(pointer_queue_t *queue, const char *queue_name)
{
    int first_error = 0;
    int error_code;

    /*
     * close是幂等操作，重复关闭是安全的。
     */
    error_code = pointer_queue_close(queue);

    if (error_code != 0) {
        report_error(queue_name, error_code);

        first_error = error_code;
    }

    for (;;) {
        void *remaining_item = NULL;

        error_code =
            pointer_queue_pop(queue, &remaining_item);

        if (error_code == ECANCELED) {
            /*
             * 队列已经关闭并且取空，清理完成。
             */
            break;
        }

        if (error_code != 0) {
            report_error("pointer_queue_pop during cleanup", error_code);

            if (first_error == 0) {
                first_error = error_code;
            }

            break;
        }

        /*
         * 两个队列保存的都是malloc创建的对象，因此都可以使用free。
         */
        free(remaining_item);
    }

    error_code = pointer_queue_destroy(queue);

    if (error_code != 0) {
        report_error("pointer_queue_destroy", error_code);

        if (first_error == 0) {
            first_error = error_code;
        }
    }

    return first_error;
}

/**
 * @brief 处理线程启动阶段失败时的统一清理。
 *
 * 此函数只在生产者尚未成功创建时使用，因此此时不会再产生新任务。
 */
static void stop_partially_started_pipeline(pointer_queue_t *task_queue, pointer_queue_t *result_queue,
                                                pthread_t worker_threads[], size_t started_worker_count,
                                                pthread_t output_thread, bool output_thread_started)
{
    int error_code;

    /*
     * 唤醒可能正在等待任务的工作线程。
     */
    error_code = pointer_queue_close(task_queue);

    if (error_code != 0) {
        report_error("pointer_queue_close(task_queue)", error_code);
    }

    /*
     * 唤醒可能正在等待结果的输出线程。
     */
    error_code = pointer_queue_close(result_queue);

    if (error_code != 0) {
        report_error("pointer_queue_close(result_queue)", error_code);
    }

    for (size_t index = 0U; index < started_worker_count; index++) {
        error_code = pthread_join(worker_threads[index], NULL);

        if (error_code != 0) {
            report_error("pthread_join(worker)", error_code);
        }
    }

    if (output_thread_started) {
        error_code = pthread_join(output_thread, NULL);

        if (error_code != 0) {
            report_error("pthread_join(output)", error_code);
        }
    }

    (void)clean_up_queue(task_queue, "task_queue");

    (void)clean_up_queue(result_queue, "result_queue");
}

/**
 * @brief 多线程流水线演示入口。
 *
 * 时间流程：
 *
 * 1. 初始化任务队列和结果队列；
 * 2. 创建输出线程；
 * 3. 创建两个工作线程；
 * 4. 创建生产者线程；
 * 5. 等待生产者结束；
 * 6. 等待全部工作线程结束；
 * 7. 关闭结果队列；
 * 8. 等待输出线程结束；
 * 9. 销毁两个队列。
 *
 * @return 流水线全部执行成功时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(int argc, char *argv[])
{
    pointer_queue_t task_queue;
    pointer_queue_t result_queue;

    pthread_t producer_thread;
    pthread_t worker_threads[PIPELINE_WORKER_COUNT];
    pthread_t output_thread;

    producer_context_t producer_context;
    worker_context_t worker_contexts[PIPELINE_WORKER_COUNT];
    output_context_t output_context;

    size_t started_worker_count = 0U;
    /*
     * 默认使用12个任务，命令行可以覆盖这个数量。
     */
    size_t task_count = (size_t)DEFAULT_PIPELINE_TASK_COUNT;

    /*
    * CSV输出路径默认写到当前工作目录。
    *
    * 如果命令行提供一个参数，则使用用户指定的路径。
    */
    const char *output_path;

    bool pipeline_succeeded = true;

    int error_code;

    /*
     * 命令行格式：
     *
     * thread_pipeline_demo [output_csv_path] [task_count]
     *
     * argc == 1：全部使用默认值；
     * argc == 2：只指定CSV路径；
     * argc == 3：同时指定CSV路径和任务数量。
     */
    if (argc > 3) {
        fprintf(stderr, "Usage: %s " "[output_csv_path] " "[task_count]\n",argv[0]);

        return EXIT_FAILURE;
    }

    if (argc >= 2) {
        /*
         * argv字符串在整个进程运行期间保持有效，所以可以把地址交给
         * 输出线程上下文。
         */
        output_path = argv[1];
    } else {
        output_path = "pipeline_results.csv";
    }

    if (argc == 3) {
        error_code = parse_task_count(argv[2], &task_count);

        if (error_code != 0) {
            fprintf(stderr, "Invalid task count '%s': %s\n", argv[2], strerror(error_code));

            return EXIT_FAILURE;
        }
    }

    error_code = pointer_queue_init(&task_queue);

    if (error_code != 0) {
        report_error("pointer_queue_init(task_queue)", error_code);

        return EXIT_FAILURE;
    }

    error_code = pointer_queue_init(&result_queue);

    if (error_code != 0) {
        report_error("pointer_queue_init(result_queue)", error_code);

        (void)clean_up_queue(&task_queue, "task_queue");

        return EXIT_FAILURE;
    }

    output_context = (output_context_t){
        .result_queue = &result_queue,
        .output_path = output_path,
        .output_count = 0U,
        .written_count = 0U,
        .error_code = 0
    };

    /*
     * 先创建输出线程。
     *
     * 如果先创建工作线程，而结果队列被填满且没有消费者，工作线程会
     * 阻塞在push中。提前启动输出线程可以持续排空结果队列。
     */
    error_code = pthread_create(&output_thread, NULL, output_thread_main, &output_context);

    if (error_code != 0) {
        report_error("pthread_create(output)", error_code);

        (void)clean_up_queue(&task_queue, "task_queue");

        (void)clean_up_queue(&result_queue, "result_queue");

        return EXIT_FAILURE;
    }

    for (size_t worker_index = 0U; worker_index < PIPELINE_WORKER_COUNT; worker_index++) {
        worker_contexts[worker_index] =
            (worker_context_t){
                .worker_index = worker_index,
                .task_queue = &task_queue,
                .result_queue = &result_queue,
                .error_code = 0
            };

        error_code = pthread_create(&worker_threads[worker_index],
                                    NULL,
                                    worker_thread_main,
                                    &worker_contexts[worker_index]);

        if (error_code != 0) {
            report_error("pthread_create(worker)", error_code);

            stop_partially_started_pipeline(
                &task_queue,
                &result_queue,
                worker_threads,
                started_worker_count,
                output_thread,
                true);

            return EXIT_FAILURE;
        }

        started_worker_count++;
    }

    producer_context = (producer_context_t){
        .task_queue = &task_queue,
        .task_count = task_count,
        .error_code = 0
    };

    /*
     * 工作线程已经准备好以后，再启动生产者。
     *
     * 工作线程此时可能阻塞在空任务队列上；生产者push第一个任务后，
     * not_empty会唤醒其中一个工作线程。
     */
    error_code = pthread_create(&producer_thread,
                                NULL,
                                producer_thread_main,
                                &producer_context);

    if (error_code != 0) {
        report_error("pthread_create(producer)", error_code);

        stop_partially_started_pipeline(
            &task_queue,
            &result_queue,
            worker_threads,
            started_worker_count,
            output_thread,
            true);

        return EXIT_FAILURE;
    }

    /*
     * 等待生产者完成。
     *
     * pthread_join不仅回收线程资源，也保证生产者写入context的结果
     * 对当前主线程可见。
     */
    error_code =
        pthread_join(producer_thread, NULL);

    if (error_code != 0) {
        report_error("pthread_join(producer)", error_code);

        /*
         * join失败时无法可靠判断目标线程是否仍在使用共享对象。
         * 直接结束进程比继续销毁队列更安全。
         */
        return EXIT_FAILURE;
    }

    /*
     * 生产者退出前已经关闭task_queue。
     *
     * 两个工作线程会继续取完队列中的已有任务，然后正常退出。
     */
    for (size_t worker_index = 0U; worker_index < PIPELINE_WORKER_COUNT; worker_index++) {
        error_code = pthread_join(worker_threads[worker_index], NULL);

        if (error_code != 0) {
            report_error("pthread_join(worker)", error_code);

            return EXIT_FAILURE;
        }
    }

    /*
     * 只有全部工作线程结束后，主线程才能关闭结果队列。
     *
     * 如果某个工作线程还可能push结果，提前关闭就会丢失结果。
     */
    error_code = pointer_queue_close(&result_queue);

    if (error_code != 0) {
        report_error("pointer_queue_close(result_queue)", error_code);

        return EXIT_FAILURE;
    }

    /*
     * 输出线程会继续取完结果队列中的已有结果。
     *
     * 队列关闭并取空后，pop返回ECANCELED，输出线程退出。
     */
    error_code = pthread_join(output_thread, NULL);

    if (error_code != 0) {
        report_error("pthread_join(output)", error_code);

        return EXIT_FAILURE;
    }

    if (producer_context.error_code != 0) {
        report_error("producer thread", producer_context.error_code);

        pipeline_succeeded = false;
    }

    for (size_t worker_index = 0U; worker_index < PIPELINE_WORKER_COUNT; worker_index++) {
        if (worker_contexts[worker_index].error_code != 0) {
            report_error("worker thread",
                         worker_contexts[worker_index].error_code);

            pipeline_succeeded = false;
        }
    }

    if (output_context.error_code != 0) {
        report_error("output thread", output_context.error_code);

        pipeline_succeeded = false;
    }

    if (output_context.output_count != task_count) {
        fprintf(stderr,
                "Expected %zu results, but received %zu.\n",
                task_count,
                output_context.output_count);

        pipeline_succeeded = false;
    }

    if (output_context.written_count != task_count) {
        fprintf(stderr,
                "Expected %zu CSV rows, but wrote %zu.\n",
                task_count,
                output_context.written_count);

        pipeline_succeeded = false;
    }

    /*
     * 正常情况下队列已经关闭并取空。
     *
     * clean_up_queue同时负责异常情况下的残留对象释放。
     */
    if (clean_up_queue(&task_queue, "task_queue") != 0) {
        pipeline_succeeded = false;
    }

    if (clean_up_queue(&result_queue, "result_queue") != 0) {
        pipeline_succeeded = false;
    }

    if (!pipeline_succeeded) {
        return EXIT_FAILURE;
    }

     printf("Pipeline completed: %zu tasks written to %s.\n", output_context.written_count, output_path);
    return EXIT_SUCCESS;
}