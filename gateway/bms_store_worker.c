#define _POSIX_C_SOURCE 200809L
#include "bms_store_worker.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static int valid_id(const char *text)
{
    if (text == NULL) return 0;

    size_t length = strnlen(text, BMS_STORE_ID_MAX + 1U);

    if (length == 0U || length > BMS_STORE_ID_MAX) return 0;

    for (size_t i = 0U; i < length; ++i) {
        unsigned char ch = (unsigned char)text[i];

        if (ch < 0x20U || ch > 0x7eU) return 0;
    }

    return 1;
}

int bms_store_worker_init(
    bms_store_worker_t *worker, bms_sample_queue_t *queue,
    const bms_store_worker_config_t *config)
{
    if (worker == NULL || queue == NULL || !queue->initialized ||
        config == NULL || config->database_path == NULL ||
        !valid_id(config->device_id) || !valid_id(config->session_id) ||
        config->batch_size == 0U ||
        config->batch_size > BMS_STORE_BATCH_MAX ||
        config->flush_interval_ms <= 0 ||
        config->flush_interval_ms > 60000) {
        return EINVAL;
    }

    if (worker->initialized) return EBUSY;

    size_t length = strnlen(
        config->database_path,
        BMS_STORE_WORKER_PATH_MAX + 1U
    );

    if (length == 0U || length > BMS_STORE_WORKER_PATH_MAX ||
        strcmp(config->database_path, ":memory:") == 0) {
        return EINVAL;
    }

    memcpy(
        worker->database_path,
        config->database_path,
        length + 1U
    );

    memcpy(
        worker->device_id,
        config->device_id,
        strlen(config->device_id) + 1U
    );

    memcpy(
        worker->session_id,
        config->session_id,
        strlen(config->session_id) + 1U
    );

    worker->queue = queue;
    worker->batch_size = config->batch_size;
    worker->flush_interval_ms = config->flush_interval_ms;
    worker->result = (bms_store_worker_result_t){0};

    atomic_init(&worker->state, BMS_STORE_WORKER_STARTING);

    worker->initialized = true;

    return 0;
}

bms_store_worker_state_t bms_store_worker_get_state(
    const bms_store_worker_t *worker)
{
    if (worker == NULL || !worker->initialized) {
        return BMS_STORE_WORKER_FAILED;
    }

    return (bms_store_worker_state_t)atomic_load_explicit(
        &worker->state,
        memory_order_acquire
    );
}

static int monotonic_ms(int64_t *value)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return errno;

    /* 给后续最多60000ms的截止时间加法保留空间。 */
    if (now.tv_sec < 0 ||
        (uint64_t)now.tv_sec > (uint64_t)(INT64_MAX - 61000) / 1000U) {
        return EOVERFLOW;
    }

    *value = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;

    return 0;
}

static int flush_batch(
    bms_store_worker_t *worker,
    bms_store_t *store,
    bms_store_row_t *rows,
    size_t *count)
{
    int rc = bms_store_write_batch(store, rows, *count);

    if (rc != SQLITE_OK) {
        worker->result.sqlite_rc = rc;
        return -1;
    }

    worker->result.committed_rows += *count;
    ++worker->result.committed_batches;

    *count = 0U;

    return 0;
}

static void consume(
    bms_store_worker_t *worker,
    bms_store_t *store)
{
    bms_store_row_t rows[BMS_STORE_BATCH_MAX];
    size_t count = 0U;
    int64_t deadline = 0;

    for (;;) {
        int wait_ms = 1000;

        if (count > 0U) {
            int64_t now;
            int rc = monotonic_ms(&now);

            if (rc != 0) {
                worker->result.system_rc = rc;
                return;
            }

            if (now >= deadline) {
                if (flush_batch(worker, store, rows, &count) != 0) {
                    return;
                }

                continue;
            }

            wait_ms = (int)(deadline - now);
        }

        bms_sample_t sample;

        int rc = bms_sample_queue_pop(
            worker->queue,
            &sample,
            wait_ms
        );

        if (rc == ETIMEDOUT) continue;

        if (rc == ECANCELED) {
            if (count > 0U) {
                (void)flush_batch(worker, store, rows, &count);
            }

            return;
        }

        if (rc != 0) {
            worker->result.system_rc = rc;
            return;
        }

        ++worker->result.popped_rows;

        if (count == 0U) {
            int64_t now;

            rc = monotonic_ms(&now);

            if (rc != 0) {
                worker->result.system_rc = rc;
                return;
            }

            deadline = now + worker->flush_interval_ms;
        }

        rows[count++] = (bms_store_row_t){
            .device_id = worker->device_id,
            .session_id = worker->session_id,
            .received_unix_us = sample.received_unix_us,
            .telemetry = sample.telemetry
        };

        if (count == worker->batch_size &&
            flush_batch(worker, store, rows, &count) != 0) {
            return;
        }

        /* 即使持续有样本，下一轮仍检查原截止时间，不滑动延期。 */
    }
}

static void close_and_drain(bms_store_worker_t *worker)
{
    int rc = bms_sample_queue_close(worker->queue);

    if (rc == 0) {
        bms_sample_t unused;

        while ((rc = bms_sample_queue_pop(
                    worker->queue, &unused, 1)) == 0) {
            ++worker->result.popped_rows;
        }

        if (rc == ECANCELED) rc = 0;
    }

    if (worker->result.system_rc == 0) {
        worker->result.system_rc = rc;
    }
}

void *bms_store_worker_run(void *argument)
{
    bms_store_worker_t *worker = argument;

    if (worker == NULL || !worker->initialized) return NULL;

    bms_store_t store = {0};

    worker->result.sqlite_rc = bms_store_open(
        &store,
        worker->database_path
    );

    if (worker->result.sqlite_rc == SQLITE_OK) {
        atomic_store_explicit(
            &worker->state,
            BMS_STORE_WORKER_READY,
            memory_order_release
        );

        consume(worker, &store);
    }

    /* 阻止失败后的新入队，并统计尚未处理的记录；绝不自动重试。 */
    close_and_drain(worker);

    int rc = bms_store_close(&store);

    if (worker->result.sqlite_rc == SQLITE_OK) {
        worker->result.sqlite_rc = rc;
    }

    int failed = worker->result.sqlite_rc != SQLITE_OK ||
                 worker->result.system_rc != 0;

    atomic_store_explicit(
        &worker->state,
        failed ? BMS_STORE_WORKER_FAILED : BMS_STORE_WORKER_STOPPED,
        memory_order_release
    );

    return NULL;
}