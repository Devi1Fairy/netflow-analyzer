#ifndef THREAD_PIPELINE_THREADS_H
#define THREAD_PIPELINE_THREADS_H

#include "pointer_queue.h"

#include <stddef.h>

/**
 * @brief 生产者线程使用的上下文。
 *
 * pthread_create只能向线程函数传递一个void *参数，因此需要把生产者
 * 使用的多个参数组织到一个结构体中，再把结构体地址传给线程。
 */
typedef struct {
    /**
     * 生产者向这个队列写入pipeline_task_t指针。
     */
    pointer_queue_t *task_queue;

    /**
     * 本次需要创建的任务总数。
     */
    size_t task_count;

    /**
     * 保存生产者线程的执行结果。
     *
     * 线程函数执行结束后，主线程先pthread_join，再读取该成员。
     */
    int error_code;
} producer_context_t;

/**
 * @brief 工作线程使用的上下文。
 */
typedef struct {
    /**
     * 工作线程编号。
     *
     * 两个工作线程共享相同的处理函数，通过这个编号区分任务是由哪个
     * 工作线程完成的。
     */
    size_t worker_index;

    /**
     * 工作线程从这个队列取得pipeline_task_t指针。
     */
    pointer_queue_t *task_queue;

    /**
     * 工作线程向这个队列写入pipeline_result_t指针。
     */
    pointer_queue_t *result_queue;

    /**
     * 保存工作线程的执行结果。
     */
    int error_code;
} worker_context_t;

/**
 * @brief 输出线程使用的上下文。
 */
typedef struct {
    /**
     * 输出线程从这个队列取得pipeline_result_t指针。
     */
    pointer_queue_t *result_queue;

    /**
     * CSV输出文件路径。
     *
     * 当前只保存字符串地址，不复制字符串。调用者必须保证该字符串在
     * 输出线程结束前始终有效。
     */
    const char *output_path;

    /**
     * 记录成功输出的结果数量。
     */
    size_t output_count;

    /**
     * 记录成功写入CSV的结果行数量，不包括表头。
     */
    size_t written_count;

    /**
     * 保存输出线程的执行结果。
     */
    int error_code;
} output_context_t;

/**
 * @brief 生产者线程入口。
 *
 * 动态创建任务，将任务指针写入任务队列，最后关闭任务队列。
 *
 * @param argument 指向producer_context_t。
 *
 * @return 当前没有需要返回给pthread_join的对象，因此返回NULL。
 */
void *producer_thread_main(void *argument);

/**
 * @brief 工作线程入口。
 *
 * 从任务队列读取任务，计算平方，然后把结果指针写入结果队列。
 *
 * @param argument 指向worker_context_t。
 *
 * @return 当前没有需要返回给pthread_join的对象，因此返回NULL。
 */
void *worker_thread_main(void *argument);

/**
 * @brief 输出线程入口。
 *
 * 从结果队列取出结果，将结果写入CSV，然后释放结果对象。
 *
 * 即使文件打开或写入失败，线程也会继续排空结果队列，避免上游线程
 * 永久阻塞。
 *
 * @param argument 指向output_context_t。
 *
 * @return 当前没有需要返回给pthread_join的对象，因此返回NULL。
 */
void *output_thread_main(void *argument);

#endif