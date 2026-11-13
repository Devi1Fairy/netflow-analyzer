#ifndef NETFLOW_ANALYZER_LOAD_LIVE_H
#define NETFLOW_ANALYZER_LOAD_LIVE_H

#include "analyzer/load_window.h"

/* 仅在窗口边界或需要重建时提供新的UTC/捕获统计快照。 */
typedef struct {
    uint64_t utc_seconds;
    uint64_t dropped_packets;
    uint64_t interface_dropped_packets;
    bool statistics_available;
} load_live_boundary_t;

typedef struct {
    load_window_counter_t counter;
    load_history_t history;
    uint64_t anchor_monotonic_ns;
    uint64_t anchor_utc_seconds;
    load_live_boundary_t baseline;
    bool first_window;
    bool carry_bad_quality;
} load_live_t;

typedef struct {
    bool closed;
    bool reset;
    load_history_window_t window;
    int history_status;  /* 0就绪；EAGAIN历史不足或质量不合格。 */
    bool statistics_known;
    bool quality_carried;
    uint64_t dropped_delta;
    uint64_t interface_dropped_delta;
} load_live_event_t;

/*
 * 一个对象对应一次固定来源/过滤配置的采集会话。
 * 无动态分配、系统调用或文件I/O；调用者串行使用。
 */
int load_live_init(
    load_live_t *live,
    uint64_t mono_ns,
    const load_live_boundary_t *boundary);

/*
 * delta为本次取得的包数；空闲唤醒传0。
 * 通常boundary=NULL；到窗口边界或单调时间倒退时必须提供快照。
 * 首窗保守标记不完整；不输出尚未闭合的尾窗。
 * 时钟映射偏移超过2秒、长停顿或统计倒退会重建全部历史。
 * 质量标记表示观察检查通过，不证明线侧零丢包。
 *
 * 非0返回时event不变，调用者应停用该观察器，继续基础采集。
 * 此时live可能已经推进，不得继续用于预测。
 */
int load_live_observe(
    load_live_t *live,
    uint64_t mono_ns,
    uint64_t delta,
    const load_live_boundary_t *boundary,
    load_live_event_t *event);

#endif
