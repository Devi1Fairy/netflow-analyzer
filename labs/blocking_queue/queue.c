#include "queue.h"

#include <errno.h>

int blocking_queue_init(blocking_queue_t *queue)
{
    int error_code;

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
    * 第一步：初始化保护共享状态的互斥锁。
    */
    error_code = pthread_mutex_init(&queue->mutex, NULL);
    if (error_code != 0) {
        return error_code;
    }

    /*
    * 第二步：初始化消费者使用的not_empty条件变量。
    *
    * 如果失败，必须回滚前面已经初始化成功的mutex。
    */
    error_code = pthread_cond_init(&queue->not_empty, NULL);
    if (error_code != 0) {
        (void)pthread_mutex_destroy(&queue->mutex);
        *queue = (blocking_queue_t){0};

        return error_code;
    }

    /*
    * 第三步：初始化生产者使用的not_full条件变量。
    *
    * 如果失败，需要按照初始化的相反顺序回滚not_empty和mutex。
    */
    error_code = pthread_cond_init(&queue->not_full, NULL);
    if (error_code != 0) {
        (void)pthread_cond_destroy(&queue->not_empty);
        (void)pthread_mutex_destroy(&queue->mutex);
        *queue = (blocking_queue_t){0};

        return error_code;
    
    }
    return 0;
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
    * 条件变量必须和一个受mutex保护的条件表达式配合使用。
    *
    * 这里真正等待的条件是：
    *
    *     queue->count < QUEUE_CAPACITY
    *
    * while而不是if的原因：
    * 1. 条件变量允许虚假唤醒；
    * 2. 多个生产者可能同时被唤醒；
    * 3. 当前线程重新获得mutex前，其他生产者可能再次填满队列。
    */
    while (queue->count == QUEUE_CAPACITY &&
        !queue->closed) {
        /*
         * pthread_cond_wait会以一个原子操作完成：
         *
         * 1. 释放queue->mutex；
         * 2. 让当前线程睡眠；
         * 3. 被唤醒后重新获得queue->mutex；
         * 4. 返回当前函数。
         *
         * 如果等待时不释放mutex，消费者就无法pop，队列也永远
         * 不可能出现空闲位置。
         */
        operation_error =
            pthread_cond_wait(&queue->not_full,
                              &queue->mutex);
        if (operation_error != 0) {
            break;
        }
    }

    if (operation_error == 0) {
        /*
         * 生产者可能因为close广播not_full而被唤醒。
         *
         * 被唤醒不等于获得了可写空间，所以必须先检查closed。
         */
        if (queue->closed) {
            operation_error = ECANCELED;
        } else {
            queue->items[queue->tail] = value;
            queue->tail =
                (queue->tail + 1U) % QUEUE_CAPACITY;
            queue->count++;

            /*
             * 成功增加一个元素后，队列一定不再为空。
             *
             * 一个新元素最多只需要唤醒一个消费者，因此使用signal，
             * 不需要把所有消费者都唤醒。
             */
            operation_error =
                pthread_cond_signal(&queue->not_empty);
        }
    }
    
    /*
    * 无论入队成功、队列关闭还是同步操作失败，都必须释放互斥锁。
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
     * 消费者真正等待的条件是：
     *
     *     queue->count > 0
     *
     * 如果队列关闭，就不能继续等待新数据，因为以后不会再有生产者
     * 写入。因此while条件还必须包含!queue->closed。
     */
    while (queue->count == 0 &&
           !queue->closed) {
        /*
         * 等待期间mutex会自动释放，使生产者能够获得mutex并push。
         *
         * 被唤醒后，pthread_cond_wait会先重新获得mutex，然后才返回。
         */
        operation_error =
            pthread_cond_wait(&queue->not_empty,
                              &queue->mutex);

        if (operation_error != 0) {
            break;
        }
    }

    if (operation_error == 0) {
        /*
         * while结束有两种可能：
         *
         * 1. count > 0：存在可读取的数据；
         * 2. closed == true且count == 0：生产结束并且队列已取空。
         */
        if (queue->count == 0) {
            operation_error = ECANCELED;
        } else {
            *value = queue->items[queue->head];
            queue->head =
                (queue->head + 1U) % QUEUE_CAPACITY;
            queue->count--;

            /*
             * 成功取出一个元素后，队列一定不再是满队列。
             *
             * 一个空闲位置最多只需要唤醒一个生产者。
             */
            operation_error =
                pthread_cond_signal(&queue->not_full);
        }
    }


    unlock_error = pthread_mutex_unlock(&queue->mutex);
    if (unlock_error != 0) {
        return unlock_error;
    }

    return operation_error;
}

int blocking_queue_close(blocking_queue_t *queue)
{
    int operation_error = 0;
    int broadcast_error;
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

    /*
     * 唤醒所有等待数据的消费者。
     *
     * 如果队列中仍有数据，消费者可以继续取走；
     * 如果队列已经为空，消费者会返回ECANCELED。
     */
    broadcast_error =
        pthread_cond_broadcast(&queue->not_empty);

    if (broadcast_error != 0) {
        operation_error = broadcast_error;
    }

     /*
     * 唤醒所有等待空间的生产者。
     *
     * 这些生产者重新获得mutex后会发现closed == true，
     * 然后返回ECANCELED。
     */
    broadcast_error =
        pthread_cond_broadcast(&queue->not_full);

    if (operation_error == 0 &&
        broadcast_error != 0) {
        operation_error = broadcast_error;
    }


    unlock_error = pthread_mutex_unlock(&queue->mutex);
    if (unlock_error != 0) {
        return unlock_error;
    }

    return operation_error;
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
     * 调用者必须保证没有线程仍在push、pop或等待条件变量。
     *
     * 按照初始化的相反顺序销毁同步资源：
     *
     * not_full → not_empty → mutex
     */

    destroy_error = pthread_cond_destroy(&queue->not_full);
    if (destroy_error != 0) {
        return destroy_error;
    }

    destroy_error = pthread_cond_destroy(&queue->not_empty);
    if (destroy_error != 0) {
        return destroy_error;
    }

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
