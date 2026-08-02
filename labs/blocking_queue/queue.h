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
 * 当前练习使用固定数组保存整数。后续会在这个结构中加入互斥锁和
 * 条件变量，使生产者和消费者能够安全地并发访问队列。
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

#endif
