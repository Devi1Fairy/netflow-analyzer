#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * 队列容量暂时设置为4，便于后续观察队列为空、未满和已满状态。
 * U后缀表示该常量是unsigned int类型。
 */
#define QUEUE_CAPACITY 4U

/**
 * @brief 保存有界队列的数据和运行状态。    
 *
 * 当前练习使用固定数组保存整数。后续会加入互斥锁和条件变量，
 * 使生产者和消费者能够安全地并发访问队列。
 */
typedef struct {
    /* data */ 
    int items[QUEUE_CAPACITY];   /**< 保存队列中的整数。 */
    size_t head;                 /**< 下一次读取元素的位置。 */
    size_t tail;                 /**< 下一次写入元素的位置。 */
    size_t count;                /**< 队列中当前有效元素的数量。 */
    bool closed;                 /**< true表示队列不再接收新元素。 */
}blocking_queue_t;


/**
 * @brief 将队列初始化为空队列。
 *
 * @param queue 指向待初始化队列的指针，不能为NULL。
 *
 * @return 成功时返回0；queue为NULL时返回EINVAL。
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
 * @brief 关闭队列，禁止生产者继续写入新数据。
 *
 * 关闭操作不会删除队列中已经存在的数据。消费者仍然可以调用
 * blocking_queue_pop取完剩余数据；队列关闭且取空后，pop将返回
 * ECANCELED。
 *
 * 重复关闭同一个队列是安全的，并且仍然返回成功。
 *
 * @param queue 指向已经初始化的队列，不能为NULL。
 *
 * @return 成功时返回0；queue为NULL时返回EINVAL。
 */
int blocking_queue_close(blocking_queue_t *queue);

/**
 * @brief 销毁队列并释放它拥有的资源。
 *
 * 调用该函数之前，调用者必须确保已经没有线程正在访问队列。
 * 销毁后的队列不能继续执行push、pop或close，除非重新调用
 * blocking_queue_init进行初始化。
 *
 * 当前单线程版本没有系统资源需要释放，因此暂时只清空普通状态。
 * 后续加入互斥锁和条件变量后，该函数将负责销毁这些同步资源。
 *
 * @param queue 指向已经初始化的队列，不能为NULL。
 *
 * @return 成功时返回0；queue为NULL时返回EINVAL。
 */
int blocking_queue_destroy(blocking_queue_t *queue);

#endif
