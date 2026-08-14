#ifndef THREAD_PIPELINE_POINTER_QUEUE_H
#define THREAD_PIPELINE_POINTER_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * 队列容量设置为4，便于后续频繁触发满队列和空队列。
 *
 */
#define POINTER_QUEUE_CAPACITY 4U

/**
 * @brief 保存指针的有界阻塞环形队列。
 *
 * 队列只保存对象地址，不复制对象内容，也不知道对象的实际类型。
 *
 * mutex保护以下全部共享状态：
 *
 * - items；
 * - head；
 * - tail；
 * - count；
 * - closed。
 *
 * not_empty供消费者等待数据；
 * not_full供生产者等待空间。
 */
typedef struct {
    /*
     * 保存通用对象指针。
     *
     * void *可以指向pipeline_task_t、pipeline_result_t或其他对象。
     * 从队列取出后，调用者必须转换回正确类型才能访问对象内容。
     */
    void *items[POINTER_QUEUE_CAPACITY];

    size_t head;  /**< 下一次读取元素的位置。 */
    size_t tail;  /**< 下一次写入元素的位置。 */
    size_t count; /**< 当前有效指针数量。 */

    /*
     * true表示队列永久停止接收新元素。
     */
    bool closed;

    /*
     * 保护队列全部共享状态。
     */
    pthread_mutex_t mutex;

    /*
     * 消费者在队列为空时等待not_empty。
     */
    pthread_cond_t not_empty;

    /*
     * 生产者在队列已满时等待not_full。
     */
    pthread_cond_t not_full;
} pointer_queue_t;

/**
 * @brief 初始化一个空的指针阻塞队列。
 *
 * @param queue 指向待初始化队列，不能为NULL。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         同步资源初始化失败时返回对应POSIX错误码。
 */
int pointer_queue_init(pointer_queue_t *queue);

/**
 * @brief 把一个非NULL对象指针写入队尾。
 *
 * 如果队列已满且尚未关闭，调用线程会等待not_full。
 *
 * 所有权约定：
 *
 * - 返回0：指针已经进入队列，调用者不再拥有该对象；
 * - 返回非0：指针没有进入队列，调用者仍负责该对象。
 *
 * @param queue 指向已经初始化的队列。
 * @param item 待入队的对象指针，不能为NULL。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         队列关闭时返回ECANCELED；
 *         同步操作失败时返回对应POSIX错误码。
 */
int pointer_queue_push(pointer_queue_t *queue,
                       void *item);

/**
 * @brief 从队头取出最早写入的对象指针。
 *
 * 如果队列为空且尚未关闭，调用线程会等待not_empty。
 *
 * 队列关闭后，已经存在的对象仍然可以继续取出；队列关闭且取空后，
 * 返回ECANCELED。
 *
 * 所有权约定：
 *
 * - 返回0：调用者获得取出对象的所有权；
 * - 返回非0：输出保持NULL，调用者没有获得对象。
 *
 * @param queue 指向已经初始化的队列。
 *
 * @param item 用于保存取出指针的输出参数。
 *
 *             参数类型是void **，因为函数需要修改调用者持有的
 *             void *变量。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         队列关闭且取空时返回ECANCELED；
 *         同步操作失败时返回对应POSIX错误码。
 */
int pointer_queue_pop(pointer_queue_t *queue,
                      void **item);

/**
 * @brief 关闭队列并唤醒全部等待线程。
 *
 * 关闭后禁止继续push，但已存在对象仍能被pop取出。
 *
 * 重复关闭是安全的。
 *
 * @param queue 指向已经初始化的队列。
 *
 * @return 成功时返回0，否则返回对应错误码。
 */
int pointer_queue_close(pointer_queue_t *queue);

/**
 * @brief 销毁队列拥有的同步资源。
 *
 * 调用前必须满足：
 *
 * - 队列已经关闭；
 * - 队列已经取空；
 * - 所有使用队列的线程已经退出并被join。
 *
 * 队列不会自动free对象。队列必须在销毁前取空，确保每个对象都已经
 * 交给对应消费者处理和释放。
 *
 * @param queue 指向已经初始化的队列。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         队列未关闭或未取空时返回EBUSY；
 *         同步资源销毁失败时返回对应错误码。
 */
int pointer_queue_destroy(pointer_queue_t *queue);

#endif