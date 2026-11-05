#ifndef BMS_STORE_WORKER_H
#define BMS_STORE_WORKER_H

#include "bms_sample_queue.h"
#include "bms_store.h"

#include <stdatomic.h>

#define BMS_STORE_WORKER_PATH_MAX 1023U

typedef enum {
    BMS_STORE_WORKER_STARTING,
    BMS_STORE_WORKER_READY,
    BMS_STORE_WORKER_STOPPED,
    BMS_STORE_WORKER_FAILED
} bms_store_worker_state_t;

typedef struct {
    const char *database_path;
    const char *device_id;
    const char *session_id;
    size_t batch_size;       /* 1..128，建议128。 */
    int flush_interval_ms;   /* 1..60000，建议1000。 */
} bms_store_worker_config_t;

typedef struct {
    uint64_t popped_rows;
    uint64_t committed_rows;
    uint64_t committed_batches;
    int sqlite_rc;  /* SQLite结果码，成功为SQLITE_OK。 */
    int system_rc;  /* 时钟/队列错误，成功为0；不是SQLite结果码。 */
} bms_store_worker_result_t;

/*
 * {0}初始化，不复制。配置字符串在init中复制，queue借用至join结束。
 * 仅存储线程使用SQLite；主线程只能并发查询state，不能读取result。
 */
typedef struct {
    bms_sample_queue_t *queue;
    char database_path[BMS_STORE_WORKER_PATH_MAX + 1U];
    char device_id[BMS_STORE_ID_MAX + 1U];
    char session_id[BMS_STORE_ID_MAX + 1U];
    size_t batch_size;
    int flush_interval_ms;
    atomic_int state;
    bms_store_worker_result_t result;
    bool initialized;
} bms_store_worker_t;

/* 返回0或errno风格错误码；不创建线程，不打开数据库/串口。 */
int bms_store_worker_init(
    bms_store_worker_t *worker, bms_sample_queue_t *queue,
    const bms_store_worker_config_t *config);

/*
 * pthread_create的入口；每次init后只能运行一次，且不能有第二个消费者。
 * READY表示数据库已打开并准备好，不等于记录已提交。
 * 失败时关闭并排空队列，不自动重试；不执行任何UART/TCP操作。
 */
void *bms_store_worker_run(void *argument);

bms_store_worker_state_t bms_store_worker_get_state(
    const bms_store_worker_t *worker);

/*
 * 正常退出：先停止生产者，再queue_close，再pthread_join，最后queue_destroy。
 * join后才能读result；popped_rows-committed_rows是未确认提交数，
 * 包括失败批次及随后排空的记录，不据此断言磁盘上一定不存在这些行。
 * 不使用pthread_cancel；磁盘I/O没有硬实时上限。
 */
#endif