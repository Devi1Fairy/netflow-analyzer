#include "bms_store.h"

#include <string.h>

static const char schema_sql[] =
    "CREATE TABLE IF NOT EXISTS bms_samples_v1 ("
    "id INTEGER PRIMARY KEY,"
    "device_id TEXT NOT NULL CHECK(length(device_id) BETWEEN 1 AND 64),"
    "session_id TEXT NOT NULL CHECK(length(session_id) BETWEEN 1 AND 64),"
    "received_unix_us INTEGER NOT NULL CHECK(received_unix_us >= 0),"
    "state INTEGER NOT NULL, faults INTEGER NOT NULL,"
    "latched INTEGER NOT NULL, load INTEGER NOT NULL,"
    "clear INTEGER NOT NULL, cell0_mv INTEGER NOT NULL,"
    "temp_est_dC INTEGER NOT NULL, i_ma INTEGER NOT NULL,"
    "iv INTEGER NOT NULL, i_src INTEGER NOT NULL,"
    "i_age INTEGER NOT NULL, ina INTEGER NOT NULL,"
    "bus_mv INTEGER NOT NULL, sample INTEGER NOT NULL,"
    "ctrl INTEGER NOT NULL, gap_max INTEGER NOT NULL"
    ");";

static const char insert_sql[] =
    "INSERT INTO bms_samples_v1 ("
    "device_id,session_id,received_unix_us,"
    "state,faults,latched,load,clear,cell0_mv,temp_est_dC,i_ma,"
    "iv,i_src,i_age,ina,bus_mv,sample,ctrl,gap_max"
    ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

static int valid_id(const char *text)
{
    if (text == NULL) return 0;
    size_t length = 0U;
    for (; length <= BMS_STORE_ID_MAX; ++length) {
        unsigned char ch = (unsigned char)text[length];
        if (ch == 0U) return length > 0U;
        if (ch < 0x20U || ch > 0x7eU) return 0;
    }
    return 0;
}

static int check_journal_mode(
    void *context, int columns, char **values, char **names)
{
    (void)names;
    int *is_delete = context;
    if (columns == 1 && values[0] != NULL &&
        strcmp(values[0], "delete") == 0) {
        *is_delete = 1;
    }
    return 0;
}

int bms_store_close(bms_store_t *store)
{
    if (store == NULL) return SQLITE_MISUSE;
    int statement_rc = sqlite3_finalize(store->insert);
    store->insert = NULL;
    int close_rc = sqlite3_close(store->database);
    if (close_rc == SQLITE_OK) {
        *store = (bms_store_t){0};
    } else {
        store->unusable = 1;
    }
    return close_rc != SQLITE_OK ? close_rc : statement_rc;
}

int bms_store_open(bms_store_t *store, const char *path)
{
    if (store == NULL || path == NULL || path[0] == '\0' ||
        strcmp(path, ":memory:") == 0 ||
        store->database != NULL || store->insert != NULL || store->unusable) {
        return SQLITE_MISUSE;
    }

    int rc = sqlite3_open_v2(path, &store->database,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                            SQLITE_OPEN_PRIVATECACHE, NULL);
    if (rc == SQLITE_OK && sqlite3_db_readonly(store->database, "main") != 0) {
        rc = SQLITE_READONLY;
    }
    /* 只限制锁竞争等待；并不保证磁盘I/O在100ms内完成。 */
    if (rc == SQLITE_OK) rc = sqlite3_busy_timeout(store->database, 100);

    int is_delete = 0;
    if (rc == SQLITE_OK) {
        rc = sqlite3_exec(store->database, "PRAGMA journal_mode=DELETE;",
                          check_journal_mode, &is_delete, NULL);
        if (rc == SQLITE_OK && !is_delete) rc = SQLITE_ERROR;
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_exec(store->database, "PRAGMA synchronous=FULL;",
                          NULL, NULL, NULL);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_exec(store->database, schema_sql, NULL, NULL, NULL);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_prepare_v2(store->database, insert_sql, -1,
                                &store->insert, NULL);
    }
    if (rc != SQLITE_OK) (void)bms_store_close(store);
    return rc;
}

static int insert_one(bms_store_t *store, const bms_store_row_t *row)
{
    sqlite3_stmt *statement = store->insert;
    int rc = sqlite3_bind_text(statement, 1, row->device_id, -1,
                               SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_text(statement, 2, row->session_id, -1,
                               SQLITE_TRANSIENT);
    }

    const bms_telemetry_t *t = &row->telemetry;
    const sqlite3_int64 values[] = {
        row->received_unix_us,
        t->state, t->faults, t->latched, t->load, t->clear,
        t->cell0_mv, t->temp_est_dC, t->i_ma,
        t->iv, t->i_src, t->i_age, t->ina, t->bus_mv,
        t->sample, t->ctrl, t->gap_max
    };
    for (size_t i = 0U; rc == SQLITE_OK &&
         i < sizeof(values) / sizeof(values[0]); ++i) {
        rc = sqlite3_bind_int64(statement, (int)i + 3, values[i]);
    }
    if (rc == SQLITE_OK) rc = sqlite3_step(statement);

    /* reset不清除绑定，因此两者都要做，并保留最先出现的错误。 */
    int reset_rc = sqlite3_reset(statement);
    int clear_rc = sqlite3_clear_bindings(statement);
    if (rc != SQLITE_DONE) return rc;
    return reset_rc != SQLITE_OK ? reset_rc : clear_rc;
}

int bms_store_write_batch(
    bms_store_t *store, const bms_store_row_t *rows, size_t count)
{
    if (store == NULL || store->database == NULL ||
        store->insert == NULL || store->unusable ||
        rows == NULL || count == 0U || count > BMS_STORE_BATCH_MAX) {
        return SQLITE_MISUSE;
    }
    for (size_t i = 0U; i < count; ++i) {
        if (!valid_id(rows[i].device_id) || !valid_id(rows[i].session_id) ||
            rows[i].received_unix_us < 0) {
            return SQLITE_MISMATCH;
        }
    }

    int rc = sqlite3_exec(store->database, "BEGIN IMMEDIATE;",
                          NULL, NULL, NULL);
    if (rc != SQLITE_OK) return rc;

    for (size_t i = 0U; i < count && rc == SQLITE_OK; ++i) {
        rc = insert_one(store, &rows[i]);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_exec(store->database, "COMMIT;", NULL, NULL, NULL);
    }
    if (rc != SQLITE_OK && !sqlite3_get_autocommit(store->database)) {
        int rollback_rc = sqlite3_exec(store->database, "ROLLBACK;",
                                       NULL, NULL, NULL);
        if (rollback_rc != SQLITE_OK) store->unusable = 1;
    }
    return rc;
}