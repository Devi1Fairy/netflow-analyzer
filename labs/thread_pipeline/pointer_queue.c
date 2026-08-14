#include "pointer_queue.h"

#include <errno.h>

int pointer_queue_init(pointer_queue_t *queue)
{
    int error_code;

    if(queue == NULL){
        return EINVAL;
    }

    /*
     * 同步资源初始化前，可以安全清空整个结构体。
     *
     * 初始化成功后不能复制包含pthread_mutex_t和pthread_cond_t的
     * 整个结构体。
     */
    *queue = (pointer_queue_t){0};

      error_code =
        pthread_mutex_init(&queue->mutex,
                           NULL);

    if (error_code != 0) {
        return error_code;
    }

    error_code =
        pthread_cond_init(&queue->not_empty,
                          NULL);

    if (error_code != 0) {
        (void)pthread_mutex_destroy(
            &queue->mutex);

        *queue = (pointer_queue_t){0};

        return error_code;
    }

    error_code =
        pthread_cond_init(&queue->not_full,
                          NULL);

    if (error_code != 0) {
        /*
         * 初始化失败时，按照初始化的相反顺序回滚。
         */
        (void)pthread_cond_destroy(
            &queue->not_empty);

        (void)pthread_mutex_destroy(
            &queue->mutex);

        *queue = (pointer_queue_t){0};

        return error_code;
    }

    return 0;
}

int pointer_queue_push(pointer_queue_t *queue,
                       void *item)
{
    int operation_error = 0;
    int lock_error;
    int unlock_error;

    if (queue == NULL ||
        item == NULL) {
        return EINVAL;
    }

    lock_error =
        pthread_mutex_lock(&queue->mutex);

    if (lock_error != 0) {
        return lock_error;
    }

    /*
     * 队列已满且没有关闭时，生产者等待空闲位置。
     *
     * 必须使用while：
     *
     * - 条件变量可能虚假唤醒；
     * - 多个生产者可能同时被唤醒；
     * - 当前线程重新获得锁前，其他生产者可能再次填满队列。
     */
    while (queue->count == POINTER_QUEUE_CAPACITY && !queue->closed) {
        operation_error = pthread_cond_wait(&queue->not_full, &queue->mutex);

        if (operation_error != 0) {
            break;
        }
    }

    if (operation_error == 0) {
        if (queue->closed) {
            /*
             * 队列关闭后不能再转移对象所有权。
             */
            operation_error = ECANCELED;
        } else {
            const size_t insertion_index = queue->tail;

            queue->items[insertion_index] = item;

            queue->tail = (queue->tail + 1U) % POINTER_QUEUE_CAPACITY;

            queue->count++;

            /*
             * 入队成功后，队列一定不为空，唤醒一个消费者。
             */
            operation_error = pthread_cond_signal(&queue->not_empty);

            if (operation_error != 0) {
                /*
                 * 当前仍然持有mutex，没有消费者能够看到这个指针。
                 *
                 * 如果通知失败，回滚本次入队，保证“返回非0表示所有权
                 * 没有转移”的接口约定。
                 */
                queue->items[insertion_index] = NULL;

                queue->tail = insertion_index;

                queue->count--;
            }
        }
    }

    unlock_error = pthread_mutex_unlock(&queue->mutex);

    if (unlock_error != 0) {
        return unlock_error;
    }

    return operation_error;
}

int pointer_queue_pop(pointer_queue_t *queue,
                      void **item)
{
    void *popped_item = NULL;

    int operation_error = 0;
    int lock_error;
    int unlock_error;

    if (queue == NULL || item == NULL) {
        return EINVAL;
    }

    /*
     * 输出参数先设置为NULL。
     *
     * 因此返回非0时，调用者不会误用之前遗留的旧指针。
     */
    *item = NULL;

    lock_error = pthread_mutex_lock(&queue->mutex);

    if (lock_error != 0) {
        return lock_error;
    }

    /*
     * 队列为空但仍可能产生新对象时，消费者等待not_empty。
     */
    while (queue->count == 0U && !queue->closed) {
        operation_error = pthread_cond_wait(&queue->not_empty, &queue->mutex);

        if (operation_error != 0) {
            break;
        }
    }

    if (operation_error == 0) {
        if (queue->count == 0U) {
            /*
             * 运行到这里表示队列已经关闭并且取空。
             */
            operation_error = ECANCELED;
        } else {
            const size_t removal_index = queue->head;

            popped_item = queue->items[removal_index];

            /*
             * 清空已取出的槽位，避免队列内部保留悬空指针。
             */
            queue->items[removal_index] = NULL;

            queue->head = (queue->head + 1U) % POINTER_QUEUE_CAPACITY;

            queue->count--;

            /*
             * 出队成功后，队列一定不再满，唤醒一个生产者。
             */
            operation_error = pthread_cond_signal(&queue->not_full);

            if (operation_error != 0) {
                /*
                 * 通知失败时回滚出队。
                 *
                 * 当前仍然持有mutex，因此其他线程看不到中间状态。
                 */
                queue->head = removal_index;

                queue->items[removal_index] = popped_item;

                queue->count++;

                popped_item = NULL;
            } else {
                /*
                 * 只有出队和条件通知全部成功后，才发布输出指针。
                 */
                *item = popped_item;
            }
        }
    }

    unlock_error =
        pthread_mutex_unlock(&queue->mutex);

    if (unlock_error != 0) {
        return unlock_error;
    }

    return operation_error;
}

int pointer_queue_close(pointer_queue_t *queue)
{
    int operation_error = 0;
    int broadcast_error;
    int lock_error;
    int unlock_error;

    if (queue == NULL) {
        return EINVAL;
    }

    lock_error = pthread_mutex_lock(&queue->mutex);

    if (lock_error != 0) {
        return lock_error;
    }

    /*
     * closed一旦变成true，就不会重新打开。
     */
    queue->closed = true;

    /*
     * 唤醒等待数据的消费者。
     *
     * 如果仍有数据，消费者继续取出；
     * 如果已经为空，消费者返回ECANCELED。
     */
    broadcast_error = pthread_cond_broadcast(&queue->not_empty);

    if (broadcast_error != 0) {
        operation_error = broadcast_error;
    }

    /*
     * 唤醒等待空间的生产者。
     *
     * 生产者重新获得mutex后发现closed为true，返回ECANCELED。
     */
    broadcast_error = pthread_cond_broadcast(&queue->not_full);

    if (operation_error == 0 && broadcast_error != 0) {
        operation_error = broadcast_error;
    }

    unlock_error = pthread_mutex_unlock(&queue->mutex);

    if (unlock_error != 0) {
        return unlock_error;
    }

    return operation_error;
}

int pointer_queue_destroy(pointer_queue_t *queue)
{
    bool can_destroy;

    int error_code;
    int unlock_error;

    if (queue == NULL) {
        return EINVAL;
    }

    /*
     * 即使调用者应该已经join全部线程，也通过mutex读取生命周期状态，
     * 保持访问规则一致。
     */
    error_code = pthread_mutex_lock(&queue->mutex);

    if (error_code != 0) {
        return error_code;
    }

    can_destroy = queue->closed && queue->count == 0U;

    unlock_error = pthread_mutex_unlock(&queue->mutex);

    if (unlock_error != 0) {
        return unlock_error;
    }

    if (!can_destroy) {
        /*
         * 未关闭或仍有对象时禁止销毁。
         *
         * 如果队列自动丢弃指针，就可能造成内存泄漏。
         */
        return EBUSY;
    }

    /*
     * 按照初始化的相反顺序销毁同步资源。
     */
    error_code = pthread_cond_destroy(&queue->not_full);

    if (error_code != 0) {
        return error_code;
    }

    error_code = pthread_cond_destroy(&queue->not_empty);

    if (error_code != 0) {
        return error_code;
    }

    error_code = pthread_mutex_destroy(&queue->mutex);

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 全部同步资源销毁后，可以清空整个结构体。
     */
    *queue = (pointer_queue_t){0};

    return 0;
}
