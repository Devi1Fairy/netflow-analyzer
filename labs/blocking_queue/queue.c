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
    * 在互斥锁初始化之前，将所有普通成员设置为初始状态。
    *
    * 此时mutex尚未初始化，因此可以对整个结构体赋值。
    * mutex初始化成功后，不能再复制整个结构体。
    */
    *queue = (blocking_queue_t){0};

    /*
    * 使用默认互斥锁属性。
    *
    * 第二个参数为NULL表示不指定特殊属性。
    * pthread函数成功时返回0，失败时直接返回POSIX错误码。
    */
    return pthread_mutex_init(&queue->mutex, NULL);
}

int blocking_queue_push(blocking_queue_t *queue, int value)
{
    int operation_error = 0;
    int lock_error;
    int unlock_error;
    /*
     * 入队需要访问和修改queue指向的对象，因此必须先验证指针。
     */
    if(queue == NULL){
        return EINVAL;
    }

     /*
     * 获得互斥锁后，其他线程不能同时修改队列状态。
     *
     * 如果另一个线程已经持有该锁，当前线程会暂时等待，
     * 直到对方释放互斥锁。这里等待的是“锁”，不是队列空间。
     */
    lock_error = pthread_mutex_lock(&queue->mutex);
    if (lock_error != 0) {
        return lock_error;
    }

     /*
     * 状态检查必须在互斥锁内部完成。
     *
     * 如果在加锁前检查count，其他线程可能在检查完成后、
     * 当前线程加锁前修改count，使检查结果失效。
     */
    if(queue->closed){
        operation_error = ECANCELED;
    }else if(queue->count == QUEUE_CAPACITY){
        operation_error = ENOSPC;
    }else{
        /*
        * 下面三个修改共同构成一次完整的入队操作，
        * 必须作为一个不可被其他线程打断的整体执行。
        */
        queue->items[queue->tail] = value;
        queue->tail = (queue->tail + 1U) % QUEUE_CAPACITY;
        queue->count++;
    }
    /*
    * 无论入队成功、队列关闭还是队列已满，都必须释放互斥锁。
    *
    * 如果在持锁状态直接return，其他线程以后将永远无法获得该锁。
    */
    unlock_error = pthread_mutex_unlock(&queue->mutex);
    if (unlock_error != 0) {
        return unlock_error;
    }

    return operation_error;
}

int blocking_queue_pop(blocking_queue_t *queue, int *value)
{
    int operation_error = 0;
    int lock_error;
    int unlock_error;
    /*
     * queue用于访问队列，value用于返回数据。
     * 任意一个指针为NULL都不能继续执行。
     */
    if(queue == NULL || value == NULL){
        return EINVAL;
    }

    lock_error = pthread_mutex_lock(&queue->mutex);
    if (lock_error != 0) {
        return lock_error;
    }

    /*
    * count和closed必须在同一个临界区中读取。
    *
    * 否则一个线程可能刚判断队列为空，另一个线程就完成了入队，
    * 导致当前线程依据已经失效的状态作出决定。
    */
    if(queue->count == 0){
        operation_error =
         queue->closed ? ECANCELED : EAGAIN;
    }else{
        /*
         * 读取元素、移动head和减少count共同构成一次出队操作。
         */
        *value = queue->items[queue->head];
        queue->head = (queue->head + 1U) % QUEUE_CAPACITY;
        queue->count--;
    }

    unlock_error = pthread_mutex_unlock(&queue->mutex);
    if (unlock_error != 0) {
        return unlock_error;
    }

    return operation_error;
}

int blocking_queue_close(blocking_queue_t *queue)
{
    int lock_error;
    int unlock_error;

    if(queue == NULL){
        return EINVAL;
    }

    /*
     * closed也属于共享状态，因此必须在互斥锁保护下修改。
     */
    lock_error = pthread_mutex_lock(&queue->mutex);
    if (lock_error != 0) {
        return lock_error;
    }

     /*
     * closed一旦设置为true，就不会再恢复为false。
     * 重复执行该赋值不会破坏队列状态，因此close是幂等操作。
     */
    queue->closed = true;

    unlock_error = pthread_mutex_unlock(&queue->mutex);
    if (unlock_error != 0) {
        return unlock_error;
    }

    return 0;
}

int blocking_queue_destroy(blocking_queue_t *queue)
{
    int destroy_error;
    /*
    * 销毁操作需要访问queue指向的对象，因此必须先验证指针。
    */
   if(queue == NULL){
        return EINVAL;
   }

    /*
    * destroy不能通过mutex自身来防止并发访问。
    *
    * 调用者必须首先停止并等待所有工作线程退出，确保已经没有线程
    * 使用该队列，然后才能销毁互斥锁。
    */
    destroy_error = pthread_mutex_destroy(&queue->mutex);
    if (destroy_error != 0) {
        return destroy_error;
    }

    /*
     * 只有互斥锁成功销毁后，才可以清空整个结构体。
     *
     * 此后该对象已经失效，必须重新init才能再次使用。
     */
   *queue = (blocking_queue_t){0};

   return 0;
}
