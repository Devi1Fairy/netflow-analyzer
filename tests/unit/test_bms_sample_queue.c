#define _POSIX_C_SOURCE 200809L
#include "bms_sample_queue.h"

#include <errno.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] line %d: %s\n", __LINE__, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

/* 仅测试目标用链接包装器，确认消费者已走到条件等待，不靠sleep猜测。 */
static pthread_cond_t *observed_condition;
static sem_t wait_entered;

int __real_pthread_cond_timedwait(
    pthread_cond_t *, pthread_mutex_t *, const struct timespec *);

int __wrap_pthread_cond_timedwait(
    pthread_cond_t *condition, pthread_mutex_t *mutex,
    const struct timespec *deadline)
{
    if (condition == observed_condition) CHECK(sem_post(&wait_entered) == 0);
    return __real_pthread_cond_timedwait(condition, mutex, deadline);
}

static void wait_for_consumer(void)
{
    struct timespec deadline;
    CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 2;
    int rc;
    do {
        rc = sem_timedwait(&wait_entered, &deadline);
    } while (rc == -1 && errno == EINTR);
    CHECK(rc == 0);
}

static void test_fifo_and_bounds(void)
{
    bms_sample_queue_t queue = {0};
    bms_sample_t item = {
        .received_unix_us = 10,
        .telemetry = {.sample = 1U, .cell0_mv = 3200U}
    };
    bms_sample_t output = {0};
    CHECK(bms_sample_queue_init(NULL, 2U) == EINVAL);
    CHECK(bms_sample_queue_init(&queue, 0U) == EINVAL);
    CHECK(bms_sample_queue_init(&queue, BMS_SAMPLE_QUEUE_MAX + 1U) == EINVAL);
    CHECK(bms_sample_queue_try_push(&queue, &item) == EINVAL);
    CHECK(bms_sample_queue_init(&queue, 2U) == 0);
    CHECK(bms_sample_queue_init(&queue, 2U) == EBUSY);
    CHECK(bms_sample_queue_destroy(&queue) == EBUSY);
    CHECK(bms_sample_queue_try_push(&queue, NULL) == EINVAL);
    CHECK(bms_sample_queue_pop(&queue, NULL, 10) == EINVAL);
    CHECK(bms_sample_queue_pop(&queue, &output, 0) == EINVAL);
    CHECK(bms_sample_queue_pop(&queue, &output, 60001) == EINVAL);

    CHECK(bms_sample_queue_try_push(&queue, &item) == 0);
    item.received_unix_us = 20;
    item.telemetry.sample = 2U;
    item.telemetry.cell0_mv = 3300U;
    CHECK(bms_sample_queue_try_push(&queue, &item) == 0);
    CHECK(bms_sample_queue_try_push(&queue, &item) == EAGAIN);
    CHECK(bms_sample_queue_pop(&queue, &output, 10) == 0);
    CHECK(output.received_unix_us == 10 && output.telemetry.sample == 1U);
    CHECK(output.telemetry.cell0_mv == 3200U);

    item.telemetry.sample = 3U;
    CHECK(bms_sample_queue_try_push(&queue, &item) == 0);
    CHECK(bms_sample_queue_pop(&queue, &output, 10) == 0);
    CHECK(output.telemetry.sample == 2U);
    CHECK(bms_sample_queue_pop(&queue, &output, 10) == 0);
    CHECK(output.telemetry.sample == 3U);
    CHECK(bms_sample_queue_pop(&queue, &output, 10) == ETIMEDOUT);
    CHECK(output.telemetry.sample == 3U);

    /* 仅测试直接持锁，验证try_push不会等锁；业务代码不能这样访问成员。 */
    CHECK(pthread_mutex_lock(&queue.mutex) == 0);
    CHECK(bms_sample_queue_try_push(&queue, &item) == EBUSY);
    CHECK(pthread_mutex_unlock(&queue.mutex) == 0);
    CHECK(bms_sample_queue_close(&queue) == 0);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
    CHECK(bms_sample_queue_close(&queue) == EINVAL);

    CHECK(bms_sample_queue_init(&queue, BMS_SAMPLE_QUEUE_MAX) == 0);
    for (size_t i = 0U; i < BMS_SAMPLE_QUEUE_MAX; ++i) {
        item.telemetry.sample = (uint32_t)i;
        CHECK(bms_sample_queue_try_push(&queue, &item) == 0);
    }
    CHECK(bms_sample_queue_try_push(&queue, &item) == EAGAIN);
    CHECK(bms_sample_queue_close(&queue) == 0);
    CHECK(bms_sample_queue_close(&queue) == 0);
    CHECK(bms_sample_queue_try_push(&queue, &item) == ECANCELED);
    CHECK(bms_sample_queue_destroy(&queue) == EBUSY);
    for (size_t i = 0U; i < BMS_SAMPLE_QUEUE_MAX; ++i) {
        CHECK(bms_sample_queue_pop(&queue, &output, 10) == 0);
        CHECK(output.telemetry.sample == i);
    }
    CHECK(bms_sample_queue_pop(&queue, &output, 10) == ECANCELED);
    CHECK(output.telemetry.sample == BMS_SAMPLE_QUEUE_MAX - 1U);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
    puts("[PASS] value copy, FIFO wrap, full/busy/timeout and close-drain");
}

typedef struct {
    bms_sample_queue_t *queue;
    bms_sample_t output;
    int result;
} one_pop_t;

static void *one_pop(void *argument)
{
    one_pop_t *context = argument;
    context->result = bms_sample_queue_pop(
        context->queue, &context->output, 5000);
    return NULL;
}

static void test_wait_and_wakeup(int close_instead)
{
    bms_sample_queue_t queue = {0};
    CHECK(bms_sample_queue_init(&queue, 1U) == 0);
    CHECK(sem_init(&wait_entered, 0, 0U) == 0);
    observed_condition = &queue.not_empty;
    one_pop_t context = {
        .queue = &queue,
        .output = {.telemetry = {.sample = 99U}},
        .result = -1
    };
    pthread_t consumer;
    CHECK(pthread_create(&consumer, NULL, one_pop, &context) == 0);
    wait_for_consumer();

    /* 无数据唤醒一次，消费者必须重新检查while条件并继续等待。 */
    CHECK(pthread_mutex_lock(&queue.mutex) == 0);
    CHECK(pthread_cond_signal(&queue.not_empty) == 0);
    CHECK(pthread_mutex_unlock(&queue.mutex) == 0);
    wait_for_consumer();

    if (close_instead) {
        CHECK(bms_sample_queue_close(&queue) == 0);
    } else {
        bms_sample_t item = {.telemetry = {.sample = 42U}};
        int rc;
        do {
            rc = bms_sample_queue_try_push(&queue, &item);
            if (rc == EBUSY) (void)sched_yield();
        } while (rc == EBUSY);
        CHECK(rc == 0);
    }
    CHECK(pthread_join(consumer, NULL) == 0);
    CHECK(context.result == (close_instead ? ECANCELED : 0));
    CHECK(context.output.telemetry.sample == (close_instead ? 99U : 42U));
    observed_condition = NULL;
    CHECK(sem_destroy(&wait_entered) == 0);
    CHECK(bms_sample_queue_close(&queue) == 0);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
}

typedef struct {
    bms_sample_queue_t *queue;
    size_t count;
    int result;
} stream_context_t;

static void *consume_stream(void *argument)
{
    stream_context_t *context = argument;
    bms_sample_t item;
    for (;;) {
        int rc = bms_sample_queue_pop(context->queue, &item, 1000);
        if (rc == ETIMEDOUT) continue;
        if (rc == ECANCELED) break;
        if (rc != 0 || item.telemetry.sample != context->count + 1U ||
            item.received_unix_us != (int64_t)(context->count + 1U) * 10) {
            context->result = rc != 0 ? rc : EPROTO;
            (void)bms_sample_queue_close(context->queue);
            break;
        }
        ++context->count;
    }
    return NULL;
}

static void test_concurrent_stream(void)
{
    bms_sample_queue_t queue = {0};
    CHECK(bms_sample_queue_init(&queue, 8U) == 0);
    stream_context_t context = {.queue = &queue};
    pthread_t consumer;
    CHECK(pthread_create(&consumer, NULL, consume_stream, &context) == 0);

    /* 仅并发测试让步重试以检查全序；真实采集不可照搬此重试循环。 */
    for (uint32_t i = 1U; i <= 10000U; ++i) {
        bms_sample_t item = {
            .received_unix_us = (int64_t)i * 10,
            .telemetry = {.sample = i}
        };
        int rc;
        do {
            rc = bms_sample_queue_try_push(&queue, &item);
            if (rc == EBUSY || rc == EAGAIN) (void)sched_yield();
        } while (rc == EBUSY || rc == EAGAIN);
        CHECK(rc == 0);
    }
    CHECK(bms_sample_queue_close(&queue) == 0);
    CHECK(pthread_join(consumer, NULL) == 0);
    CHECK(context.result == 0 && context.count == 10000U);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
    puts("[PASS] 10000 synthetic records cross threads in order");
}

int main(void)
{
    test_fifo_and_bounds();
    test_wait_and_wakeup(0);
    test_wait_and_wakeup(1);
    puts("[PASS] observed condition wait, empty wakeup recheck and close wakeup");
    test_concurrent_stream();
    return EXIT_SUCCESS;
}