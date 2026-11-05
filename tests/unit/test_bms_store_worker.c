#define _POSIX_C_SOURCE 200809L
#include "bms_store_worker.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] line %d: %s\n", \
                    __LINE__, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

/* 只在create前设置、join后重置，测试期间没有并发写这个标志。 */
static int slow_pop;

static void pause_ms(int milliseconds)
{
    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L
    };

    while (nanosleep(&delay, &delay) != 0) {
        CHECK(errno == EINTR);
    }
}

int __real_bms_sample_queue_pop(
    bms_sample_queue_t *, bms_sample_t *, int);

int __wrap_bms_sample_queue_pop(
    bms_sample_queue_t *queue,
    bms_sample_t *sample,
    int timeout_ms)
{
    int rc = __real_bms_sample_queue_pop(queue, sample, timeout_ms);

    /* 模拟消费者处理成本；队列预先装满，不依赖空闲超时刷新。 */
    if (rc == 0 && slow_pop) pause_ms(25);

    return rc;
}

static int64_t now_ms(void)
{
    struct timespec now;

    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);

    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bms_sample_t make_sample(uint32_t number)
{
    bms_sample_t sample = {0};

    sample.received_unix_us = INT64_C(1793785181000000) + number;
    sample.telemetry.sample = number;
    sample.telemetry.cell0_mv = 3227U;
    sample.telemetry.temp_est_dC = -15;
    sample.telemetry.i_ma = -1234;
    sample.telemetry.faults = UINT32_MAX;

    return sample;
}

static bms_store_worker_config_t configuration(const char *path)
{
    return (bms_store_worker_config_t){
        .database_path = path,
        .device_id = "bms-test-01",
        .session_id = "capture-01",
        .batch_size = 128U,
        .flush_interval_ms = 50
    };
}

static sqlite3 *open_reader(const char *path)
{
    sqlite3 *db = NULL;

    CHECK(sqlite3_open_v2(
        path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK);

    CHECK(sqlite3_busy_timeout(db, 20) == SQLITE_OK);

    return db;
}

static sqlite3_int64 scalar(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &statement, NULL);

    if (rc == SQLITE_BUSY) {
        (void)sqlite3_finalize(statement);
        return -1;
    }

    CHECK(rc == SQLITE_OK);

    rc = sqlite3_step(statement);

    sqlite3_int64 result = -1;

    if (rc == SQLITE_ROW) {
        result = sqlite3_column_int64(statement, 0);
    } else {
        CHECK(rc == SQLITE_BUSY);
    }

    rc = sqlite3_finalize(statement);

    CHECK(rc == SQLITE_OK || rc == SQLITE_BUSY);

    return result;
}

static void wait_ready(bms_store_worker_t *worker)
{
    int64_t deadline = now_ms() + 3000;

    while (bms_store_worker_get_state(worker) ==
           BMS_STORE_WORKER_STARTING) {
        CHECK(now_ms() < deadline);
        pause_ms(1);
    }

    CHECK(bms_store_worker_get_state(worker) ==
          BMS_STORE_WORKER_READY);
}

static void wait_rows(sqlite3 *db, sqlite3_int64 wanted)
{
    int64_t deadline = now_ms() + 3000;

    while (scalar(db, "SELECT COUNT(*) FROM bms_samples_v1;") !=
           wanted) {
        CHECK(now_ms() < deadline);
        pause_ms(2);
    }
}

static void successful_join(
    pthread_t thread,
    bms_store_worker_t *worker)
{
    CHECK(pthread_join(thread, NULL) == 0);

    CHECK(bms_store_worker_get_state(worker) ==
          BMS_STORE_WORKER_STOPPED);

    CHECK(worker->result.sqlite_rc == SQLITE_OK);
    CHECK(worker->result.system_rc == 0);
    CHECK(worker->result.popped_rows ==
          worker->result.committed_rows);
}

int main(void)
{
    char directory[] = "/tmp/nfa-bms-worker-test.XXXXXX";

    CHECK(mkdtemp(directory) != NULL);

    char path[256];

    CHECK(snprintf(
        path, sizeof(path), "%s/samples.sqlite3", directory) > 0);

    bms_store_worker_config_t config = configuration(path);
    bms_sample_queue_t queue = {0};
    bms_store_worker_t worker = {0};

    CHECK(bms_store_worker_init(&worker, &queue, &config) == EINVAL);
    CHECK(bms_sample_queue_init(&queue, 256U) == 0);

    config.batch_size = 129U;

    CHECK(bms_store_worker_init(&worker, &queue, &config) == EINVAL);

    config = configuration(path);
    config.device_id = "bad\nidentifier";

    CHECK(bms_store_worker_init(&worker, &queue, &config) == EINVAL);

    char device[] = "bms-test-01";
    char session[] = "capture-01";

    config = configuration(path);
    config.device_id = device;
    config.session_id = session;
    config.flush_interval_ms = 60000;

    CHECK(bms_store_worker_init(&worker, &queue, &config) == 0);
    CHECK(bms_store_worker_init(&worker, &queue, &config) == EBUSY);

    device[0] = 'X';
    session[0] = 'X'; /* 不影响已复制的配置。 */

    for (uint32_t i = 0U; i < 131U; ++i) {
        bms_sample_t sample = make_sample(i);

        CHECK(bms_sample_queue_try_push(&queue, &sample) == 0);
    }

    CHECK(bms_sample_queue_close(&queue) == 0);

    pthread_t thread;

    CHECK(pthread_create(
        &thread, NULL, bms_store_worker_run, &worker) == 0);

    successful_join(thread, &worker);

    CHECK(worker.result.committed_rows == 131U);
    CHECK(worker.result.committed_batches == 2U);

    sqlite3 *db = open_reader(path);

    CHECK(scalar(db,
        "SELECT COUNT(*) FROM bms_samples_v1 WHERE "
        "device_id='bms-test-01' AND session_id='capture-01' AND "
        "received_unix_us=1793785181000000+sample AND "
        "sample=id-1 AND cell0_mv=3227 AND temp_est_dC=-15 AND "
        "i_ma=-1234 AND faults=4294967295;") == 131);

    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
    CHECK(unlink(path) == 0);

    puts("[PASS] owned metadata, FIFO, full batch and shutdown partial batch");

    queue = (bms_sample_queue_t){0};
    worker = (bms_store_worker_t){0};

    CHECK(bms_sample_queue_init(&queue, 8U) == 0);

    config = configuration(path);

    CHECK(bms_store_worker_init(&worker, &queue, &config) == 0);

    bms_sample_t sample = make_sample(1U);

    CHECK(bms_sample_queue_try_push(&queue, &sample) == 0);

    CHECK(pthread_create(
        &thread, NULL, bms_store_worker_run, &worker) == 0);

    wait_ready(&worker);

    db = open_reader(path);

    /* 队列尚未关闭，也必须定时提交。 */
    wait_rows(db, 1);

    CHECK(bms_store_worker_get_state(&worker) ==
          BMS_STORE_WORKER_READY);

    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(bms_sample_queue_close(&queue) == 0);

    successful_join(thread, &worker);

    CHECK(worker.result.committed_rows == 1U);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
    CHECK(unlink(path) == 0);

    puts("[PASS] database readiness and timed flush before queue closure");

    queue = (bms_sample_queue_t){0};
    worker = (bms_store_worker_t){0};

    CHECK(bms_sample_queue_init(&queue, 8U) == 0);

    config = configuration(path);
    config.flush_interval_ms = 40;

    CHECK(bms_store_worker_init(&worker, &queue, &config) == 0);

    for (uint32_t i = 0U; i < 5U; ++i) {
        sample = make_sample(i);

        CHECK(bms_sample_queue_try_push(&queue, &sample) == 0);
    }

    CHECK(bms_sample_queue_close(&queue) == 0);

    slow_pop = 1;

    CHECK(pthread_create(
        &thread, NULL, bms_store_worker_run, &worker) == 0);

    successful_join(thread, &worker);

    slow_pop = 0;

    CHECK(worker.result.committed_rows == 5U);
    CHECK(worker.result.committed_batches >= 2U);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
    CHECK(unlink(path) == 0);

    puts("[PASS] continuously available samples do not postpone flush forever");

    bms_store_t setup = {0};

    CHECK(bms_store_open(&setup, path) == SQLITE_OK);

    CHECK(sqlite3_exec(setup.database,
        "CREATE TRIGGER reject_test BEFORE INSERT ON bms_samples_v1 "
        "WHEN NEW.sample=999 BEGIN SELECT RAISE(ABORT,'test'); END;",
        NULL, NULL, NULL) == SQLITE_OK);

    CHECK(bms_store_close(&setup) == SQLITE_OK);

    queue = (bms_sample_queue_t){0};
    worker = (bms_store_worker_t){0};

    CHECK(bms_sample_queue_init(&queue, 8U) == 0);

    config = configuration(path);
    config.batch_size = 2U;

    CHECK(bms_store_worker_init(&worker, &queue, &config) == 0);

    for (uint32_t i = 0U; i < 5U; ++i) {
        sample = make_sample(i == 1U ? 999U : i);

        CHECK(bms_sample_queue_try_push(&queue, &sample) == 0);
    }

    CHECK(pthread_create(
        &thread, NULL, bms_store_worker_run, &worker) == 0);

    CHECK(pthread_join(thread, NULL) == 0);

    CHECK(bms_store_worker_get_state(&worker) ==
          BMS_STORE_WORKER_FAILED);

    CHECK(worker.result.sqlite_rc == SQLITE_CONSTRAINT);
    CHECK(worker.result.system_rc == 0);
    CHECK(worker.result.popped_rows == 5U);
    CHECK(worker.result.committed_rows == 0U);
    CHECK(worker.result.committed_batches == 0U);

    CHECK(bms_sample_queue_try_push(&queue, &sample) == ECANCELED);

    db = open_reader(path);

    CHECK(scalar(db, "SELECT COUNT(*) FROM bms_samples_v1;") == 0);

    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
    CHECK(unlink(path) == 0);

    puts("[PASS] failed batch rolls back, closes/drains queue and is not retried");

    queue = (bms_sample_queue_t){0};
    worker = (bms_store_worker_t){0};

    CHECK(bms_sample_queue_init(&queue, 1U) == 0);

    /* 目录不是数据库文件，应打开失败。 */
    config = configuration(directory);

    CHECK(bms_store_worker_init(&worker, &queue, &config) == 0);
    CHECK(bms_sample_queue_try_push(&queue, &sample) == 0);

    CHECK(pthread_create(
        &thread, NULL, bms_store_worker_run, &worker) == 0);

    CHECK(pthread_join(thread, NULL) == 0);

    CHECK(bms_store_worker_get_state(&worker) ==
          BMS_STORE_WORKER_FAILED);

    CHECK(worker.result.sqlite_rc == SQLITE_CANTOPEN);
    CHECK(worker.result.system_rc == 0);
    CHECK(worker.result.popped_rows == 1U);
    CHECK(worker.result.committed_rows == 0U);
    CHECK(bms_sample_queue_destroy(&queue) == 0);
    CHECK(rmdir(directory) == 0);

    puts("[PASS] database open failure never reaches READY and drains queued values");

    return EXIT_SUCCESS;
}