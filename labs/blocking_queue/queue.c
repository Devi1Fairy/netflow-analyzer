#include "queue.h"

#include <errno.h>

int blocking_queue_init(blocking_queue_t *queue)
{
    /*
     * 外部传入的指针不能直接信任。NULL不指向有效对象，
     * 如果继续通过它访问结构体成员，会产生未定义行为。
     */
    if(queue == NULL){
        return EINVAL;
    }

     /*
     * 使用C11的结构体复合字面量将所有成员初始化为零：
     * items全部为0，head/tail/count为0，closed为false。
     */
    *queue = (blocking_queue_t){0};

    return 0;
}

int blocking_queue_push(blocking_queue_t *queue, int value)
{
    /*
     * 入队需要访问和修改queue指向的对象，因此必须先验证指针。
     */
    if(queue == NULL){
        return EINVAL;
    }

     /*
     * 关闭是队列的永久状态。关闭后不再接收任何新数据，
     * 即使数组中仍有剩余位置，也不能继续入队。
     */
    if(queue->closed){
        return ECANCELED;
    }

     /*
     * 有界队列不能写入超过容量的数据。
     * 当前单线程版本直接返回ENOSPC，后续多线程版本会在这里等待。
     */
    if(queue->count == QUEUE_CAPACITY){
        return ENOSPC;
    }

     /*
     * tail始终表示下一次写入位置。
     */
    queue->items[queue->tail] = value;

     /*
     * 写入完成后移动tail。
     * 取模运算保证tail到达数组末尾后重新回到下标0。
     */
    queue->tail = (queue->tail + 1u) % QUEUE_CAPACITY;

    /*
     * 成功写入一个元素后，有效元素数量增加。
     */
    queue->count++;

    return 0;
}

int blocking_queue_pop(blocking_queue_t *queue, int *value)
{
    /*
     * queue用于访问队列，value用于返回数据。
     * 任意一个指针为NULL都不能继续执行。
     */
    if(queue == NULL || value == NULL){
        return EINVAL;
    }

    /*
     * count为0表示队列中没有有效数据。
     *
     * 队列尚未关闭时返回EAGAIN，表示当前暂时没有数据；
     * 队列已经关闭时返回ECANCELED，表示以后也不会再有新数据。
     */
    if(queue->count == 0){
        return queue->closed ? ECANCELED : EAGAIN;
    }

    /*
     * head始终指向下一次应该读取的位置。
     * 先读取数据，再移动head。
     */
    *value = queue->items[queue->head];

    /*
     * 取模运算保证head到达数组末尾后回到下标0，
     * 从而形成循环队列。
     */
    queue->head = (queue->head + 1U) % QUEUE_CAPACITY;

    /*
     * 前面已经确认count不为0，因此这里减1不会造成
     * size_t无符号整数下溢。
     */
    queue->count--;
    
    return 0;
}


