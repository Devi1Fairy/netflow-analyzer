#ifndef NETFLOW_ANALYZER_LOAD_WINDOW_H
#define NETFLOW_ANALYZER_LOAD_WINDOW_H

#include "analyzer/load_history.h"

#define LOAD_WINDOW_WIDTH_NS \
    (LOAD_HISTORY_WINDOW_SECONDS * UINT64_C(1000000000))

/* 调用者持有对象，先init，不直接修改字段，串行调用。 */
typedef struct {
    uint64_t start_monotonic_ns;
    uint64_t start_utc_seconds;
    uint64_t packet_count;
    uint64_t last_monotonic_ns;

    bool initialized;
} load_window_counter_t;

/*
 * 两个时间必须指向同一窗口起点。
 *
 * 单调时钟负责时长；UTC秒只用于生成窗口标签。
 * 不要求UTC整10分钟，不读取系统时钟。
 *
 * 来源/过滤配置改变，或UTC与单调时钟映射失效时，
 * 同时重置计数器与对应历史。
 */
int load_window_init(
    load_window_counter_t *counter,
    uint64_t monotonic_start_ns,
    uint64_t utc_start_seconds);

/*
 * 将在mono_ns时刻计入的包数增量加入当前窗口。
 * 实时采集可在每次读取一包时传delta=1。
 *
 * 不能把跨窗口的累计差值均分或全部塞入一个窗口。
 *
 * 时间区间为[start, end)，边界上的包属于下一窗。
 * 返回EAGAIN表示先close，再重试本次增量；本次尚未计入。
 *
 * 倒退时间返回ESTALE。
 * 计数超出精度上限返回EOVERFLOW。
 */
int load_window_add(
    load_window_counter_t *counter,
    uint64_t mono_ns,
    uint64_t delta);

/*
 * 检查并最多闭合一个600秒窗口。
 *
 * 未到期：*closed=false，window不变。
 * 到期：*closed=true，生成window，清零下一窗计数。
 *
 * complete/quality_ok必须描述整窗覆盖和质量，
 * 不是瞬时健康状态。
 * 提前调用不保存这两个标记，采集层需保存整窗证据。
 *
 * 无包窗口只有在这两个标记可信时才是有效零流量。
 *
 * 若已错过两个及以上终点，返回ESTALE：
 * 调用者须重建计数器/历史，不补造多个零窗口。
 *
 * 所有函数失败时不修改输出或counter。
 * EINVAL表示空指针/非法状态。
 * EOVERFLOW表示时间或计数溢出。
 * 输出对象不得与counter或彼此重叠。
 */
int load_window_close(
    load_window_counter_t *counter,
    uint64_t mono_ns,
    bool complete,
    bool quality_ok,
    bool *closed,
    load_history_window_t *window);

#endif