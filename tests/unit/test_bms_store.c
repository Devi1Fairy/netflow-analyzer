#define _POSIX_C_SOURCE 200809L
#include "bms_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] line %d: %s\n", __LINE__, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static const char sample_line[] =
    "BMS state=1, faults=0x00000000, latched=0x00000000, "
    "load=0, clear=0, cell0_mv=3227, temp_est_dC=259, i_ma=0, "
    "iv=1, i_src=3, i_age=101, ina=1, bus_mv=0, "
    "sample=286605, ctrl=1433031, gap_max=20";

static void execute(sqlite3 *db, const char *sql)
{
    CHECK(sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK);
}

static sqlite3_int64 scalar(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = NULL;
    CHECK(sqlite3_prepare_v2(db, sql, -1, &statement, NULL) == SQLITE_OK);
    CHECK(sqlite3_step(statement) == SQLITE_ROW);
    sqlite3_int64 result = sqlite3_column_int64(statement, 0);
    CHECK(sqlite3_step(statement) == SQLITE_DONE);
    CHECK(sqlite3_finalize(statement) == SQLITE_OK);
    return result;
}

static void verify_rows(const char *path,
                        const bms_store_row_t *rows, size_t count)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *statement = NULL;
    CHECK(sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK);
    const char *sql =
        "SELECT device_id,session_id,received_unix_us,"
        "state,faults,latched,load,clear,cell0_mv,temp_est_dC,i_ma,"
        "iv,i_src,i_age,ina,bus_mv,sample,ctrl,gap_max "
        "FROM bms_samples_v1 ORDER BY id;";
    CHECK(sqlite3_prepare_v2(db, sql, -1, &statement, NULL) == SQLITE_OK);
    CHECK(sqlite3_column_count(statement) == 19);

    for (size_t i = 0U; i < count; ++i) {
        CHECK(sqlite3_step(statement) == SQLITE_ROW);
        CHECK(strcmp((const char *)sqlite3_column_text(statement, 0),
                     rows[i].device_id) == 0);
        CHECK(strcmp((const char *)sqlite3_column_text(statement, 1),
                     rows[i].session_id) == 0);
        const bms_telemetry_t *t = &rows[i].telemetry;
        const sqlite3_int64 expected[] = {
            rows[i].received_unix_us,
            t->state, t->faults, t->latched, t->load, t->clear,
            t->cell0_mv, t->temp_est_dC, t->i_ma,
            t->iv, t->i_src, t->i_age, t->ina, t->bus_mv,
            t->sample, t->ctrl, t->gap_max
        };
        for (size_t j = 0U; j < sizeof(expected) / sizeof(expected[0]); ++j) {
            CHECK(sqlite3_column_type(statement, (int)j + 2) == SQLITE_INTEGER);
            CHECK(sqlite3_column_int64(statement, (int)j + 2) == expected[j]);
        }
    }
    CHECK(sqlite3_step(statement) == SQLITE_DONE);
    CHECK(sqlite3_finalize(statement) == SQLITE_OK);
    CHECK(sqlite3_close(db) == SQLITE_OK);
}

int main(void)
{
    char directory[] = "/tmp/nfa-bms-store-test.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[256];
    int length = snprintf(path, sizeof(path), "%s/telemetry.sqlite3", directory);
    CHECK(length > 0 && (size_t)length < sizeof(path));

    bms_store_t store = {0};
    CHECK(bms_store_open(NULL, path) == SQLITE_MISUSE);
    CHECK(bms_store_open(&store, "") == SQLITE_MISUSE);
    CHECK(bms_store_open(&store, ":memory:") == SQLITE_MISUSE);
    CHECK(bms_store_open(&store, directory) == SQLITE_CANTOPEN);
    CHECK(store.database == NULL && store.insert == NULL);
    CHECK(bms_store_open(&store, path) == SQLITE_OK);
    CHECK(bms_store_open(&store, path) == SQLITE_MISUSE);

    bms_store_row_t rows[2] = {0};
    rows[0].device_id = "bms'); DROP TABLE bms_samples_v1; --";
    rows[0].session_id = "run-01";
    rows[0].received_unix_us = INT64_C(1793785181723066);
    CHECK(bms_telemetry_parse(sample_line, strlen(sample_line),
                              &rows[0].telemetry) == BMS_TELEMETRY_OK);
    rows[1] = rows[0];
    rows[1].session_id = "run-02";
    rows[1].received_unix_us += 200000;
    rows[1].telemetry.faults = UINT32_MAX;
    rows[1].telemetry.latched = UINT32_MAX;
    rows[1].telemetry.temp_est_dC = INT32_MIN;
    rows[1].telemetry.i_ma = -1234;
    rows[1].telemetry.ctrl = UINT32_MAX;

    CHECK(bms_store_write_batch(NULL, rows, 1U) == SQLITE_MISUSE);
    CHECK(bms_store_write_batch(&store, NULL, 1U) == SQLITE_MISUSE);
    CHECK(bms_store_write_batch(&store, rows, 0U) == SQLITE_MISUSE);
    CHECK(bms_store_write_batch(&store, rows, BMS_STORE_BATCH_MAX + 1U) ==
          SQLITE_MISUSE);

    bms_store_row_t bad = rows[0];
    bad.device_id = "";
    CHECK(bms_store_write_batch(&store, &bad, 1U) == SQLITE_MISMATCH);
    bad.device_id = NULL;
    CHECK(bms_store_write_batch(&store, &bad, 1U) == SQLITE_MISMATCH);
    bad.device_id = "bad\nidentifier";
    CHECK(bms_store_write_batch(&store, &bad, 1U) == SQLITE_MISMATCH);
    char long_id[BMS_STORE_ID_MAX + 2U];
    memset(long_id, 'x', sizeof(long_id) - 1U);
    long_id[sizeof(long_id) - 1U] = '\0';
    bad.device_id = long_id;
    CHECK(bms_store_write_batch(&store, &bad, 1U) == SQLITE_MISMATCH);
    bad = rows[0];
    bad.received_unix_us = -1;
    CHECK(bms_store_write_batch(&store, &bad, 1U) == SQLITE_MISMATCH);

    CHECK(bms_store_write_batch(&store, rows, 2U) == SQLITE_OK);
    CHECK(bms_store_close(&store) == SQLITE_OK);
    CHECK(bms_store_close(&store) == SQLITE_OK);
    verify_rows(path, rows, 2U);
    puts("[PASS] metadata, 16 telemetry fields, integer bounds and close/reopen");

    CHECK(bms_store_open(&store, path) == SQLITE_OK);
    sqlite3 *other = NULL;
    CHECK(sqlite3_open_v2(path, &other, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK);
    CHECK(scalar(other, "SELECT COUNT(*) FROM bms_samples_v1;") == 2);
    CHECK(scalar(store.database, "PRAGMA synchronous;") == 2);

    /* 在第二条记录处制造SQL错误，验证第一条也必须回滚。 */
    execute(other,
        "CREATE TRIGGER reject_test BEFORE INSERT ON bms_samples_v1 "
        "WHEN NEW.sample = 999 BEGIN "
        "SELECT RAISE(ABORT, 'injected failure'); END;");
    bms_store_row_t batch[2] = {rows[0], rows[1]};
    batch[0].telemetry.sample = 10U;
    batch[1].telemetry.sample = 999U;
    CHECK(bms_store_write_batch(&store, batch, 2U) == SQLITE_CONSTRAINT);
    CHECK(scalar(other, "SELECT COUNT(*) FROM bms_samples_v1;") == 2);
    execute(other, "DROP TRIGGER reject_test;");
    CHECK(bms_store_write_batch(&store, batch, 1U) == SQLITE_OK);
    CHECK(scalar(other, "SELECT COUNT(*) FROM bms_samples_v1;") == 3);
    puts("[PASS] failed batch rolls back all rows; later batch still works");

    execute(other, "BEGIN IMMEDIATE;");
    CHECK(bms_store_write_batch(&store, rows, 1U) == SQLITE_BUSY);
    execute(other, "ROLLBACK;");
    CHECK(scalar(other, "SELECT COUNT(*) FROM bms_samples_v1;") == 3);

    /* 读事务允许BEGIN IMMEDIATE，但会阻止DELETE日志模式下的COMMIT。 */
    execute(other, "BEGIN;");
    CHECK(scalar(other, "SELECT COUNT(*) FROM bms_samples_v1;") == 3);
    CHECK(bms_store_write_batch(&store, rows, 1U) == SQLITE_BUSY);
    execute(other, "ROLLBACK;");
    CHECK(scalar(other, "SELECT COUNT(*) FROM bms_samples_v1;") == 3);
    CHECK(bms_store_write_batch(&store, rows, 1U) == SQLITE_OK);
    CHECK(scalar(other, "SELECT COUNT(*) FROM bms_samples_v1;") == 4);
    puts("[PASS] BEGIN/COMMIT lock contention returns BUSY without partial writes");

    char full_id[BMS_STORE_ID_MAX + 1U];
    memset(full_id, 'd', sizeof(full_id) - 1U);
    full_id[sizeof(full_id) - 1U] = '\0';
    bms_store_row_t maximum[BMS_STORE_BATCH_MAX];
    for (size_t i = 0U; i < BMS_STORE_BATCH_MAX; ++i) {
        maximum[i] = rows[0];
        maximum[i].device_id = full_id;
    }
    CHECK(bms_store_write_batch(&store, maximum, BMS_STORE_BATCH_MAX) ==
          SQLITE_OK);
    CHECK(scalar(other, "SELECT COUNT(*) FROM bms_samples_v1;") ==
          4 + BMS_STORE_BATCH_MAX);
    CHECK(scalar(other,
        "SELECT COUNT(*) FROM bms_samples_v1 WHERE length(device_id)=64;") ==
          BMS_STORE_BATCH_MAX);
    CHECK(sqlite3_close(other) == SQLITE_OK);
    CHECK(bms_store_close(&store) == SQLITE_OK);
    CHECK(bms_store_write_batch(&store, rows, 1U) == SQLITE_MISUSE);
    puts("[PASS] bounded batch/IDs, repeated sample values and resource cleanup");

    /* 仅删除本测试在独立临时目录中创建的数据库。 */
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    return EXIT_SUCCESS;
}