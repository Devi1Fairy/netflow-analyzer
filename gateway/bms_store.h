#ifndef BMS_STORE_H
#define BMS_STORE_H

#include "bms_telemetry.h"

#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>

#define BMS_STORE_ID_MAX 64U
#define BMS_STORE_BATCH_MAX 128U

/* 必须初始化为{0}；内部句柄仅由本模块维护，不复制、不并发使用。 */
typedef struct {
    sqlite3 *database;
    sqlite3_stmt *insert;
    int unusable;
} bms_store_t;

typedef struct {
    const char *device_id;
    const char *session_id;
    /* 网关接收Unix微秒；0表示历史日志时间未知，不是MCU采样时间。 */
    int64_t received_unix_us;
    bms_telemetry_t telemetry;
} bms_store_row_t;

/*
 * 打开/创建本应用专用的磁盘数据库及bms_samples_v1表。
 * store须为{0}或已成功关闭；失败会清理本次打开的资源。
 * 不支持:memory:；不负责迁移已有表结构。
 * 以下函数均返回SQLite结果码，成功为SQLITE_OK。
 */
int bms_store_open(bms_store_t *store, const char *path);

/*
 * 同步写入1..128条；每次调用使用一个事务。
 * ID为1..64字节可打印ASCII、NUL结束；输入借用至函数返回。
 * received_unix_us为非负网关接收Unix微秒；0表示未知，不是采样时间。
 * 已知写入失败时回滚本批；返回失败不自动重试或宣称恰好一次。
 * 发生回滚失败时标记unusable，只允许关闭，不继续写入。
 */
int bms_store_write_batch(
    bms_store_t *store, const bms_store_row_t *rows, size_t count);

/* 释放语句及连接；已关闭的对象可重复关闭。 */
int bms_store_close(bms_store_t *store);

#endif