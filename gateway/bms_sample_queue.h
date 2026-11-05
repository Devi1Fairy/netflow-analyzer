#ifndef BMS_SAMPLE_QUEUE_H
#define BMS_SAMPLE_QUEUE_H

#include "bms_telemetry.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BMS_SAMPLE_QUEUE_MAX 256U

/* 队列只传递值，不保存行缓冲或字符串指针。 */
typedef struct {
    /* 接收Unix微秒；0为未知，由采集主线程填写，队列不生成时间。 */
    int64_t received_unix_us;
    bms_telemetry_t telemetry;
} bms_sample_t;

/* 成员由本模块维护；初始化后禁止复制，不直接读写共享成员。 */
typedef struct {
    bms_sample_t items[BMS_SAMPLE_QUEUE_MAX];
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    size_t capacity;
    size_t head;
    size_t count;
    bool closed;
    bool initialized;
} bms_sample_queue_t;

/* queue须为{0}；capacity为1..256；成功后必须按下述顺序销毁。 */
int bms_sample_queue_init(bms_sample_queue_t *queue, size_t capacity);

/*
 * 成功返回0，复制整个sample，调用者随后可修改自己的原对象。
 * 不等待互斥锁或空位：EBUSY=锁忙，EAGAIN=队列满，均未入队。
 * ECANCELED=已关闭，未入队；EINVAL=参数/初始化状态错误。
 * 其他同步错误应停止组件，不自动重试或推断操作未发生。
 * 只传递业务字段，不检查物理测量或枚举有效性。
 */
int bms_sample_queue_try_push(
    bms_sample_queue_t *queue, const bms_sample_t *sample);

/*
 * 成功返回0并取出队头；空队列按CLOCK_MONOTONIC等待。
 * timeout_ms为1..60000；ETIMEDOUT=等待超时。
 * 关闭后先取完已有记录，再返回ECANCELED。
 * ETIMEDOUT/ECANCELED不修改output。
 * 超时限制条件等待，不是获取锁及线程调度的硬实时保证。
 */
int bms_sample_queue_pop(
    bms_sample_queue_t *queue, bms_sample_t *output, int timeout_ms);

/* 禁止新入队并唤醒消费者；保留旧记录，可重复调用。 */
int bms_sample_queue_close(bms_sample_queue_t *queue);

/*
 * 必须先close，再让消费者排空并pthread_join所有使用者，最后destroy。
 * 没有消费者时可由当前线程在close后排空。
 * 尚未close或仍有记录返回EBUSY；不能用它替代pthread_join。
 * 不在信号处理函数中调用这些接口，不使用pthread_cancel终止消费者。
 * 所有接口直接返回错误码，不通过errno传递pthread错误。
 */
int bms_sample_queue_destroy(bms_sample_queue_t *queue);

#endif