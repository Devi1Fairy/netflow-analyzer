#define _POSIX_C_SOURCE 200809L
#include "bms_sample_queue.h"

#include <errno.h>
#include <time.h>

static int unlock_queue(bms_sample_queue_t *queue, int result)
{
    int rc = pthread_mutex_unlock(&queue->mutex);
    return result != 0 ? result : rc;
}

int bms_sample_queue_init(bms_sample_queue_t *queue, size_t capacity)
{
    if (queue == NULL || capacity == 0U || capacity > BMS_SAMPLE_QUEUE_MAX) {
        return EINVAL;
    }
    if (queue->initialized) return EBUSY;

    int rc = pthread_mutex_init(&queue->mutex, NULL);
    if (rc != 0) return rc;
    pthread_condattr_t attributes;
    rc = pthread_condattr_init(&attributes);
    if (rc != 0) {
        (void)pthread_mutex_destroy(&queue->mutex);
        return rc;
    }
    rc = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (rc == 0) {
        rc = pthread_cond_init(&queue->not_empty, &attributes);
    }
    int attribute_rc = pthread_condattr_destroy(&attributes);
    if (rc == 0 && attribute_rc != 0) {
        (void)pthread_cond_destroy(&queue->not_empty);
        rc = attribute_rc;
    }
    if (rc != 0) {
        (void)pthread_mutex_destroy(&queue->mutex);
        return rc;
    }

    queue->capacity = capacity;
    queue->head = 0U;
    queue->count = 0U;
    queue->closed = false;
    queue->initialized = true;
    return 0;
}

int bms_sample_queue_try_push(
    bms_sample_queue_t *queue, const bms_sample_t *sample)
{
    if (queue == NULL || sample == NULL || !queue->initialized) return EINVAL;
    int rc = pthread_mutex_trylock(&queue->mutex);
    if (rc != 0) return rc;

    int result = 0;
    if (queue->closed) {
        result = ECANCELED;
    } else if (queue->count == queue->capacity) {
        result = EAGAIN;
    } else {
        size_t tail = (queue->head + queue->count) % queue->capacity;
        queue->items[tail] = *sample;
        ++queue->count;
        result = pthread_cond_signal(&queue->not_empty);
    }
    return unlock_queue(queue, result);
}

int bms_sample_queue_pop(
    bms_sample_queue_t *queue, bms_sample_t *output, int timeout_ms)
{
    if (queue == NULL || output == NULL || !queue->initialized ||
        timeout_ms <= 0 || timeout_ms > 60000) return EINVAL;

    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1) return errno;
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }

    int rc = pthread_mutex_lock(&queue->mutex);
    if (rc != 0) return rc;
    while (queue->count == 0U && !queue->closed) {
        rc = pthread_cond_timedwait(
            &queue->not_empty, &queue->mutex, &deadline);
        if (rc != 0) {
            /* 超时与数据到达可能同时发生，仍须在锁内复查条件。 */
            if (rc == ETIMEDOUT && (queue->count > 0U || queue->closed)) break;
            return unlock_queue(queue, rc);
        }
    }
    if (queue->count == 0U) return unlock_queue(queue, ECANCELED);

    *output = queue->items[queue->head];
    queue->head = (queue->head + 1U) % queue->capacity;
    --queue->count;
    return unlock_queue(queue, 0);
}

int bms_sample_queue_close(bms_sample_queue_t *queue)
{
    if (queue == NULL || !queue->initialized) return EINVAL;
    int rc = pthread_mutex_lock(&queue->mutex);
    if (rc != 0) return rc;
    queue->closed = true;
    rc = pthread_cond_broadcast(&queue->not_empty);
    return unlock_queue(queue, rc);
}

int bms_sample_queue_destroy(bms_sample_queue_t *queue)
{
    if (queue == NULL || !queue->initialized) return EINVAL;
    /* 这里要求所有使用者已退出；不能与push/pop/close并发调用。 */
    if (!queue->closed || queue->count != 0U) return EBUSY;
    int rc = pthread_cond_destroy(&queue->not_empty);
    if (rc != 0) return rc;
    rc = pthread_mutex_destroy(&queue->mutex);
    if (rc == 0) queue->initialized = false;
    return rc;
}