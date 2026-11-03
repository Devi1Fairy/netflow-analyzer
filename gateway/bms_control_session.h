#ifndef BMS_CONTROL_SESSION_H
#define BMS_CONTROL_SESSION_H

#include "bms_control_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BMS_CONTROL_QUEUE_CAPACITY 8U
#define BMS_CONTROL_TTL_MS 1000

typedef struct {
    bms_control_request_t request;
    int64_t queued_ms;
} bms_control_pending_t;

/*
 * reason为静态字符串；回调须立即复制到自己的有界输出缓存。
 * 返回0表示已接收错误事件，非0表示无法接收。
 * 不得阻塞、重入session或执行UART写入。
 */
typedef int (*bms_control_error_sink_fn)(
    uint32_t id, const char *reason, void *context
);

/* 由调用者持有，无动态内存；内部字段仅供观察，不直接修改。 */
typedef struct {
    unsigned char body[BMS_CONTROL_MAX_BODY];
    size_t used;
    bool discarding;
    bool connected;
    bool serial_ready;
    uint32_t last_id;
    int64_t last_now_ms;
    bms_control_pending_t pending[BMS_CONTROL_QUEUE_CAPACITY];
    size_t count;
} bms_control_session_t;

/*
 * 每个新连接只初始化一次；serial_ready只是调用者提供的就绪状态。
 * 本模块不打开设备、不验证硬件、不发送能力帧。
 * 初始化后若串口失效，调用disconnect结束会话，不复用旧队列。
 */
void bms_control_session_init(bms_control_session_t *session,
                              bool serial_ready);

/* 清空半行和待写命令，不发回复、不隐式STOP；之后只能重新初始化。 */
void bms_control_session_disconnect(bms_control_session_t *session);

/*
 * 借用bytes[0..length)，允许半行/多行；仅LF结束后才准入命令。
 * now_ms为调用者取得的非负CLOCK_MONOTONIC毫秒，各次调用不得倒退。
 * 返回0成功，-1时errno说明错误，调用者必须结束该会话。
 * 回调拒收时errno=ENOBUFS，自动清空队列并标记断开。
 * context仅在调用期间借用；不保存输入、回调或context指针。
 */
int bms_control_session_feed(bms_control_session_t *session,
                             const unsigned char *bytes, size_t length,
                             int64_t now_ms,
                             bms_control_error_sink_fn sink, void *context);

/*
 * 先清理过期项，再复制队首到out；返回1有命令、0空队列、-1错误。
 * 不出队、不写UART、不产生ACK；out仅在返回1时更新。
 */
int bms_control_session_peek(bms_control_session_t *session,
                             int64_t now_ms,
                             bms_control_error_sink_fn sink, void *context,
                             bms_control_pending_t *out);

/*
 * 未来写入层仅在单字节write返回1后立即调用，移除对应队首。
 * peek与本操作之间不得再feed/peek，否则STOP可能取消原队首。
 * 返回0成功，-1错误；本函数仍不产生ACK，ACK由真实I/O层负责。
 */
int bms_control_session_written(bms_control_session_t *session, uint32_t id);

#endif