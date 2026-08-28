#ifndef NETFLOW_ANALYZER_RUNTIME_METRICS_H
#define NETFLOW_ANALYZER_RUNTIME_METRICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 表示运行指标使用的单调时钟时间点。
 *
 * seconds和nanoseconds共同表示一个时间点。
 * nanoseconds的合法范围是0到999999999。
 *
 * 该时间不表示真实日期。应用层后续会使用CLOCK_MONOTONIC，
 * 避免系统时间校准或人工修改日期导致速率计算倒退。
 */
typedef struct {
    int64_t seconds;
    int32_t nanoseconds;
} runtime_metrics_timestamp_t;

/**
 * @brief 保存从程序启动以来持续累加的运行总量。
 *
 * 这些字段只能保持不变或增加，不能在报告周期之间减小。
 */
typedef struct {
    /**
     * 应用成功从采集层取得的数据包总数。
     */
    uint64_t packet_count;

    /**
     * 应用实际取得并可访问的字节总数，对应caplen累计值。
     */
    uint64_t captured_byte_count;

    /**
     * 数据包在线路上的原始字节总数，对应wirelen累计值。
     */
    uint64_t wire_byte_count;

    /**
     * 实时运行期间累计过期并移除的流数量。
     */
    uint64_t expired_flow_count;
} runtime_metrics_totals_t;

/**
 * @brief 保存一个报告周期内计算得到的指标快照。
 *
 * packet和byte字段表示本周期增量，不是程序启动以来的总量。
 * active_flow_count和flow_table_capacity表示报告发生时的瞬时状态。
 */
typedef struct {
    double elapsed_seconds;

    uint64_t interval_packet_count;
    uint64_t interval_captured_byte_count;
    uint64_t interval_wire_byte_count;
    uint64_t interval_expired_flow_count;

    double packets_per_second;
    double captured_megabits_per_second;
    double wire_megabits_per_second;

    size_t active_flow_count;
    size_t flow_table_capacity;
    double flow_table_usage_percent;
} runtime_metrics_report_t;

/**
 * @brief 保存周期运行指标的上一次报告基线。
 *
 * 调度器不读取系统时钟，也不拥有动态内存。
 */
typedef struct {
    runtime_metrics_timestamp_t last_report_timestamp;
    runtime_metrics_totals_t last_report_totals;

    int64_t report_interval_seconds;

    bool initialized;
} runtime_metrics_schedule_t;

/**
 * @brief 把一个数据包的数量和字节数加入累计指标。
 *
 * 更新具有完整性：任何字段可能溢出时，totals保持原值。
 *
 * @return 成功时返回0；
 *         totals为空时返回EINVAL；
 *         任一累计字段可能溢出时返回EOVERFLOW。
 */
int runtime_metrics_totals_add_packet(
    runtime_metrics_totals_t *totals,
    uint32_t captured_length,
    uint32_t wire_length);

/**
 * @brief 把本次扫描删除的过期流数量加入累计指标。
 *
 * @return 成功时返回0；
 *         totals为空时返回EINVAL；
 *         累加可能溢出时返回EOVERFLOW。
 */
int runtime_metrics_totals_add_expired_flows(
    runtime_metrics_totals_t *totals,
    uint64_t expired_flow_count);

/**
 * @brief 初始化周期指标调度器。
 *
 * initial_timestamp和initial_totals共同构成第一个报告周期的基线。
 * 函数失败时不修改schedule。
 *
 * @return 成功时返回0，参数无效时返回EINVAL。
 */
int runtime_metrics_schedule_init(
    runtime_metrics_schedule_t *schedule,
    int64_t report_interval_seconds,
    const runtime_metrics_timestamp_t *initial_timestamp,
    const runtime_metrics_totals_t *initial_totals);

/**
 * @brief 观察当前时间和累计量，并在周期到达时生成报告。
 *
 * 如果尚未达到报告周期：
 *
 * - report_due设置为false；
 * - report清零；
 * - 调度器基线保持不变。
 *
 * 如果已经达到报告周期：
 *
 * - report_due设置为true；
 * - report保存本周期增量和速率；
 * - 调度器基线更新为当前时间和累计量。
 *
 * 函数失败时不修改schedule、report_due和report。
 *
 * @return 成功时返回0；
 *         参数或流表状态无效时返回EINVAL；
 *         时间倒退或累计量减小时返回ERANGE。
 */
int runtime_metrics_schedule_observe(
    runtime_metrics_schedule_t *schedule,
    const runtime_metrics_timestamp_t *current_timestamp,
    const runtime_metrics_totals_t *current_totals,
    size_t active_flow_count,
    size_t flow_table_capacity,
    bool *report_due,
    runtime_metrics_report_t *report);

#endif