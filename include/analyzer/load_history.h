#ifndef NETFLOW_ANALYZER_LOAD_HISTORY_H
#define NETFLOW_ANALYZER_LOAD_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOAD_HISTORY_WINDOWS 1008U
#define LOAD_HISTORY_FEATURES 18U
#define LOAD_HISTORY_WINDOW_SECONDS UINT64_C(600)
#define LOAD_HISTORY_MAX_PACKETS UINT64_C(9007199254740991)

/*
 * 调用者只能提交已经结束的600秒窗口。
 *
 * complete表示采集覆盖整个窗口。
 * quality_ok表示计数质量可信。
 *
 * 两者任一为false，该窗口包数不可用于模型。
 * 零包仍可为有效数据。
 */
typedef struct {
    uint64_t start_seconds;
    uint64_t end_seconds;
    uint64_t packet_count;

    bool complete;
    bool quality_ok;
} load_history_window_t;

/*
 * 调用者拥有对象；先init，不直接修改字段，不并发调用。
 *
 * 一个对象只属于一个采集来源/过滤配置。
 * 来源变化时重新init。
 */
typedef struct {
    uint64_t packets[LOAD_HISTORY_WINDOWS];
    bool usable[LOAD_HISTORY_WINDOWS];

    size_t length;
    size_t next_index;

    uint64_t next_start_seconds;
} load_history_t;

/* 初始化或显式清空历史；NULL返回EINVAL。 */
int load_history_init(load_history_t *history);

/*
 * 保存一个闭合窗口，不分配内存、不读取时钟。
 *
 * 时间向前跳过窗口时清除旧历史，当前窗口作为新起点。
 * 不合格窗口保留时间位置，但usable=false。
 *
 * 成功返回0。
 * 非法字段返回EINVAL。
 * 重复/倒退返回ESTALE。
 *
 * 返回非0时history保持原值。
 */
int load_history_push(
    load_history_t *history,
    const load_history_window_t *window);

/*
 * 提取：
 *   最近12窗；
 *   前一天对应3窗；
 *   前一周对应3窗。
 *
 * 共18个包数，顺序与现有模型相同。
 * origin_seconds必须等于最后闭合窗口终点。
 *
 * 历史不足或所需窗口不可信返回EAGAIN。
 * 起点不匹配返回ESTALE。
 * 参数/状态非法返回EINVAL。
 *
 * 所有失败均保持raw原值。
 * raw必须容纳18个double，且不得与history重叠。
 *
 * 本函数不判断当前真实时间；
 * 调度层仍须检查输入新鲜度。
 */
int load_history_extract(
    const load_history_t *history,
    uint64_t origin_seconds,
    double raw[LOAD_HISTORY_FEATURES]);

#endif