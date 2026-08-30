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
 * @brief 表示一个数据包经过应用处理后的最终结果。
 *
 * 这些结果互斥：一个数据包最终只能计入其中一种。
 *
 * NOT_STARTED不属于可报告结果，因为它只是解析过程中的中间状态。
 */
typedef enum {
    /**
     * 当前支持的协议已经完整解析，并成功进入流表。
     */
    RUNTIME_METRICS_PACKET_RESULT_COMPLETE = 0,

    /**
     * 捕获数据不足，无法读取必要协议字段。
     */
    RUNTIME_METRICS_PACKET_RESULT_TRUNCATED,

    /**
     * 数据长度足够，但协议字段组合不符合规范。
     */
    RUNTIME_METRICS_PACKET_RESULT_MALFORMED,

    /**
     * 数据包格式合法，但协议或封装暂未得到支持。
     */
    RUNTIME_METRICS_PACKET_RESULT_UNSUPPORTED,

    /**
     * 协议解析成功，但流表因为容量或策略拒绝该数据包。
     */
    RUNTIME_METRICS_PACKET_RESULT_FLOW_REJECTED
} runtime_metrics_packet_result_t;

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
     * 完整解析并成功进入流表的数据包数量。
     */
    uint64_t complete_packet_count;

    /**
     * 因必要字段没有完整捕获而无法继续解析的数据包数量。
     */
    uint64_t truncated_packet_count;

    /**
     * 因协议字段组合非法而无法继续处理的数据包数量。
     */
    uint64_t malformed_packet_count;

    /**
     * 使用当前项目尚未支持的协议或封装的数据包数量。
     */
    uint64_t unsupported_packet_count;

    /**
     * 解析成功但未能进入流表的数据包数量。
     */
    uint64_t flow_rejected_packet_count;

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

    /**
     * 实时运行期间因为流表满载策略而主动淘汰的流数量。
     *
     * 该值表示流生命周期事件，不表示数据包处理失败。
     * 使用evict-oldest策略后，触发淘汰的当前数据包仍可能完整进入流表。
     */
    uint64_t evicted_flow_count;
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
    uint64_t interval_complete_packet_count;
    uint64_t interval_truncated_packet_count;
    uint64_t interval_malformed_packet_count;
    uint64_t interval_unsupported_packet_count;
    uint64_t interval_flow_rejected_packet_count;
    uint64_t interval_captured_byte_count;
    uint64_t interval_wire_byte_count;
    uint64_t interval_expired_flow_count;
    uint64_t interval_evicted_flow_count;

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
 * @brief 把一个数据包的最终处理结果加入累计指标。
 *
 * 本函数只增加对应的分类计数，不增加packet_count和字节数。
 * packet_count及字节数仍由runtime_metrics_totals_add_packet()维护。
 *
 * 更新失败时totals保持原值。
 *
 * @return 成功时返回0；
 *         totals为空或result无效时返回EINVAL；
 *         对应分类计数已经达到UINT64_MAX时返回EOVERFLOW。
 */
int runtime_metrics_totals_add_packet_result(
    runtime_metrics_totals_t *totals,
    runtime_metrics_packet_result_t result);

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
 * @brief 把本次容量处理主动淘汰的流数量加入累计指标。
 *
 * 本函数只更新流淘汰事件计数，不修改数据包处理结果。
 * totals由调用者拥有，本函数只在调用期间借用该指针。
 *
 * @return 成功时返回0；
 *         totals为空时返回EINVAL；
 *         累加可能溢出时返回EOVERFLOW。
 */
int runtime_metrics_totals_add_evicted_flows(
    runtime_metrics_totals_t *totals,
    uint64_t evicted_flow_count);

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