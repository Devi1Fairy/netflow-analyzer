#ifndef NETFLOW_ANALYZER_FLOW_EXPIRATION_H
#define NETFLOW_ANALYZER_FLOW_EXPIRATION_H

#include "analyzer/flow_record.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 保存实时流过期扫描的时间状态。
 *
 * latest_timestamp是已经观察到的最大数据包时间戳，
 * 不会因为后续乱序数据包而倒退。
 *
 * last_scan_timestamp记录上一次实际触发扫描时的时间高水位。
 */
typedef struct {
    /**
     * 目前观察到的最大数据包时间戳，也叫“时间高水位”，
     * 假设数据包时间戳依次为：100秒，103秒，101秒，107秒；它的变化是：100 → 103 → 103 → 107，
     * 第三个包是乱序包，时间戳从103退到101，但高水位不能倒退。
     * 如果直接把“当前数据包时间”作为现在，乱序包会让过期计算从103秒退回101秒，导致扫描行为不稳定。
     */
    flow_timestamp_t latest_timestamp;
    /**
     * 记录上一次真正触发流表扫描时的时间高水位；
     * 扫描条件是latest_timestamp >= last_scan_timestamp + scan_interval
     * 例如：last_scan_timestamp = 100.250000;scan_interval = 5秒;
     * 下一次扫描边界就是：105.250000
     */
    flow_timestamp_t last_scan_timestamp;

    /**
     * 表示一条流多久没有活动才过期，
     * 它负责计算：cutoff = latest_timestamp - idle_timeout
     * 假设：latest_timestamp = 110.250000;idle_timeout      = 30秒,得到：cutoff = 80.250000
     * 以后流表会删除record->last_seen <= 80.250000
     */
    int64_t idle_timeout_seconds;

    /**
     * 控制多久检查一次流表
     */
    int64_t scan_interval_seconds;

    bool has_observation;
    bool initialized;
} flow_expiration_schedule_t;

/**
 * @brief 初始化流过期调度器。
 *
 * 函数只保存时间策略，不读取系统时钟，也不操作流表。
 *
 * 函数失败时不修改schedule。
 *
 * @return 成功时返回0；
 *         参数为空、超时或扫描周期不大于0时返回EINVAL。
 */
int flow_expiration_schedule_init(
    flow_expiration_schedule_t *schedule,
    int64_t idle_timeout_seconds,
    int64_t scan_interval_seconds);

/**
 * @brief 向调度器提交一个数据包时间戳。
 *
 * 第一条时间戳只建立时间高水位，不触发扫描。
 *
 * 后续时间戳晚于当前高水位时更新高水位；乱序时间戳不会使
 * 高水位倒退。当高水位距离上次扫描时间达到扫描周期时：
 *
 * - scan_due返回true；
 * - cutoff返回“时间高水位减去空闲超时”；
 * - last_scan_timestamp更新为当前高水位。
 *
 * 不需要扫描时，scan_due返回false，cutoff被清零。
 *
 * 函数失败时不修改schedule、scan_due或cutoff。
 *
 * @return 成功时返回0；
 *         参数、调度器状态或时间戳无效时返回EINVAL；
 *         截止时间减法可能溢出时返回ERANGE。
 */
int flow_expiration_schedule_observe(
    flow_expiration_schedule_t *schedule,
    const flow_timestamp_t *timestamp,
    bool *scan_due,
    flow_timestamp_t *cutoff);

#endif