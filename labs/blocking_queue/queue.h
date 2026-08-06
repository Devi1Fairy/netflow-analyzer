#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * 队列容量暂时设置为4，便于后续观察队列为空、未满和已满状态。
 * U后缀表示该常量是unsigned int类型。
 */
#define QUEUE_CAPACITY 4U

/**
 * @brief 保存有界队列的数据、运行状态和同步资源。
 *
 * mutex保护items、head、tail、count和closed，确保多个线程不能同时
 * 修改队列状态。
 *
 * not_empty供消费者等待“队列中出现数据”；
 * not_full供生产者等待“队列中出现空闲位置”。
 * 
 * 并发环境中，调用者必须通过blocking_queue_push、
 * blocking_queue_pop和blocking_queue_close访问队列，不能直接读写成员。
 */
typedef struct {
    /* data */ 
    int items[QUEUE_CAPACITY];   /**< 保存队列中的整数。 */
    size_t head;                 /**< 下一次读取元素的位置。 */
    size_t tail;                 /**< 下一次写入元素的位置。 */
    size_t count;                /**< 队列中当前有效元素的数量。 */
    bool closed;                 /**< true表示队列不再接收新元素。 */

    /**
     * 保护队列的全部共享状态。
     *
     * 它必须通过pthread_mutex_init初始化，并通过
     * pthread_mutex_destroy销毁，不能直接复制。
     */
    pthread_mutex_t mutex;

    /**
     * 消费者在队列为空时等待该条件变量。
     *
     * 生产者成功写入数据后，通过该条件变量唤醒消费者。
     */
    pthread_cond_t not_empty;

    /**
     * 生产者在队列已满时等待该条件变量。
     *
     * 消费者成功取出数据后，通过该条件变量唤醒生产者。
     */
    pthread_cond_t not_full;

}blocking_queue_t;


/**
 * @brief 初始化一个空的阻塞队列。
 *
 * 该函数负责初始化互斥锁以及not_empty、not_full条件变量。
 * 初始化成功后，必须在所有线程结束后调用blocking_queue_destroy。
 *
 * @param queue 指向待初始化队列的指针，不能为NULL。
 *
 * @return 成功时返回0；
 *         queue为NULL时返回EINVAL；
 *         同步资源初始化失败时返回对应的POSIX错误码。
 */
int blocking_queue_init(blocking_queue_t *queue);

/**
 * @brief 将一个整数写入队尾。
 *
 * 当前是单线程、非阻塞版本。队列已满时不会等待，而是直接返回错误。
 * 后续加入条件变量后，队列已满时生产者将进入等待状态。
 *
 * @param queue 指向已经初始化的队列，不能为NULL。
 * @param value 需要写入队列的整数。
 *
 * @return 成功时返回0；
 *         queue为NULL时返回EINVAL；
 *         队列已经关闭时返回ECANCELED；
 *         队列已满时返回ENOSPC。
 */
int blocking_queue_push(blocking_queue_t *queue, int value);

/**
 * @brief 从队头取出最早写入的整数。
 *
 * 当前是单线程、非阻塞版本。队列为空时不会等待，而是直接返回错误。
 * 如果队列已经关闭但仍有剩余数据，允许消费者继续取完这些数据。
 *
 * @param queue 指向已经初始化的队列，不能为NULL。
 * @param value 用于保存取出结果的指针，不能为NULL。
 *
 * @return 成功时返回0；
 *         queue或value为NULL时返回EINVAL；
 *         队列未关闭但暂时为空时返回EAGAIN；
 *         队列已经关闭且没有剩余数据时返回ECANCELED。
 */
int blocking_queue_pop(blocking_queue_t *queue, int *value);

/**
 * @brief 关闭队列并唤醒全部等待线程。
 *
 * 关闭后不再允许生产者写入新数据。
 *
 * 已存在的数据仍然可以被消费者取出；队列关闭且取空后，消费者从
 * blocking_queue_pop得到ECANCELED。
 *
 * close会同时广播not_empty和not_full：
 * 等待数据的消费者和等待空间的生产者都会被唤醒并重新检查closed。
 *
 * 重复关闭同一个队列是安全的。
 *
 * @param queue 指向已经初始化的队列，不能为NULL。
 *
 * @return 成功时返回0；
 *         queue为NULL时返回EINVAL；
 *         同步操作失败时返回对应的POSIX错误码。
 */
int blocking_queue_close(blocking_queue_t *queue);

/**
 * @brief 销毁队列拥有的全部同步资源。
 *
 * 调用前必须确保所有工作线程已经结束，并且没有线程继续访问队列。
 *
 * 销毁后的队列不能继续执行push、pop或close，除非重新调用init。
 *
 * @param queue 指向已经初始化的队列，不能为NULL。
 *
 * @return 成功时返回0；
 *         queue为NULL时返回EINVAL；
 *         资源仍在使用或销毁失败时返回对应的POSIX错误码。
 */
int blocking_queue_destroy(blocking_queue_t *queue);

#endif
