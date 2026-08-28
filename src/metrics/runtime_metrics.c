#include "analyzer/runtime_metrics.h"

#include <errno.h>
#include <stdint.h>

/*
 * 十进制网络速率单位。
 *
 * 1 Mbps表示每秒1,000,000 bit，
 * 不是每秒1,048,576 bit。
 */
#define RUNTIME_METRICS_BITS_PER_BYTE 8.0
#define RUNTIME_METRICS_BITS_PER_MEGABIT 1000000.0
#define RUNTIME_METRICS_NANOSECONDS_PER_SECOND 1000000000

/**
 * @brief 判断时间点字段是否合法。
 */
static bool runtime_metrics_timestamp_is_valid(
    const runtime_metrics_timestamp_t *timestamp)
{
    return timestamp != NULL &&
           timestamp->seconds >= INT64_C(0) &&
           timestamp->nanoseconds >= INT32_C(0) &&
           timestamp->nanoseconds < INT32_C(
               RUNTIME_METRICS_NANOSECONDS_PER_SECOND
           );
}

/**
 * @brief 比较两个合法时间点。
 *
 * @return left早于right时返回-1；
 *         相等时返回0；
 *         left晚于right时返回1。
 */
static int runtime_metrics_timestamp_compare(
    const runtime_metrics_timestamp_t *left,
    const runtime_metrics_timestamp_t *right)
{
    if (left->seconds < right->seconds) {
        return -1;
    }

    if (left->seconds > right->seconds) {
        return 1;
    }

    if (left->nanoseconds < right->nanoseconds) {
        return -1;
    }

    if (left->nanoseconds > right->nanoseconds) {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断当前累计量是否没有发生倒退。
 */
static bool runtime_metrics_totals_are_monotonic(
    const runtime_metrics_totals_t *current,
    const runtime_metrics_totals_t *previous)
{
    return current->packet_count >= previous->packet_count &&
           current->captured_byte_count >=
               previous->captured_byte_count &&
           current->wire_byte_count >=
               previous->wire_byte_count &&
           current->expired_flow_count >=
               previous->expired_flow_count;
}

/**
 * @brief 判断从previous到current是否至少经过指定秒数。
 */
static bool runtime_metrics_interval_elapsed(
    const runtime_metrics_timestamp_t *current,
    const runtime_metrics_timestamp_t *previous,
    int64_t interval_seconds)
{
    int64_t seconds_difference;

    seconds_difference = current->seconds - previous->seconds;

    if (seconds_difference > interval_seconds) {
        return true;
    }

    if (seconds_difference < interval_seconds) {
        return false;
    }

    /*
     * 秒字段正好相差interval_seconds时，
     * 纳秒字段必须到达或超过上一次报告的纳秒位置。
     */
    return current->nanoseconds >= previous->nanoseconds;
}

/**
 * @brief 判断调度器内部基本状态是否合法。
 */
static bool runtime_metrics_schedule_is_valid(
    const runtime_metrics_schedule_t *schedule)
{
    return schedule != NULL &&
           schedule->initialized &&
           schedule->report_interval_seconds > INT64_C(0) &&
           runtime_metrics_timestamp_is_valid(
               &schedule->last_report_timestamp
           );
}

int runtime_metrics_totals_add_packet(
    runtime_metrics_totals_t *totals,
    uint32_t captured_length,
    uint32_t wire_length)
{
    runtime_metrics_totals_t updated_totals;

    if (totals == NULL) {
        return EINVAL;
    }

    /*
     * 在修改任何字段前检查全部加法。
     *
     * 如果后面的字节累计失败，不能只增加packet_count，
     * 否则几个累计字段将不再描述同一组数据包。
     */
    if (totals->packet_count == UINT64_MAX ||
        totals->captured_byte_count >
            UINT64_MAX - (uint64_t)captured_length ||
        totals->wire_byte_count >
            UINT64_MAX - (uint64_t)wire_length) {
        return EOVERFLOW;
    }

    updated_totals = *totals;

    updated_totals.packet_count += UINT64_C(1);
    updated_totals.captured_byte_count += (uint64_t)captured_length;
    updated_totals.wire_byte_count += (uint64_t)wire_length;

    *totals = updated_totals;

    return 0;
}

int runtime_metrics_totals_add_expired_flows(
    runtime_metrics_totals_t *totals,
    uint64_t expired_flow_count)
{
    if (totals == NULL) {
        return EINVAL;
    }

    if (totals->expired_flow_count >
        UINT64_MAX - expired_flow_count) {
        return EOVERFLOW;
    }

    totals->expired_flow_count += expired_flow_count;

    return 0;
}

int runtime_metrics_schedule_init(
    runtime_metrics_schedule_t *schedule,
    int64_t report_interval_seconds,
    const runtime_metrics_timestamp_t *initial_timestamp,
    const runtime_metrics_totals_t *initial_totals)
{
    runtime_metrics_schedule_t new_schedule;

    if (schedule == NULL ||
        report_interval_seconds <= INT64_C(0) ||
        !runtime_metrics_timestamp_is_valid(
            initial_timestamp
        ) ||
        initial_totals == NULL) {
        return EINVAL;
    }

    new_schedule = (runtime_metrics_schedule_t){
        .last_report_timestamp = *initial_timestamp,
        .last_report_totals = *initial_totals,
        .report_interval_seconds = report_interval_seconds,
        .initialized = true
    };

    /*
     * 所有参数验证完成后再发布对象。
     */
    *schedule = new_schedule;

    return 0;
}

int runtime_metrics_schedule_observe(
    runtime_metrics_schedule_t *schedule,
    const runtime_metrics_timestamp_t *current_timestamp,
    const runtime_metrics_totals_t *current_totals,
    size_t active_flow_count,
    size_t flow_table_capacity,
    bool *report_due,
    runtime_metrics_report_t *report)
{
    runtime_metrics_schedule_t updated_schedule;
    runtime_metrics_report_t result_report = {0};

    int64_t elapsed_whole_seconds;
    int32_t elapsed_nanoseconds;

    double elapsed_seconds;

    if (!runtime_metrics_schedule_is_valid(schedule) ||
        !runtime_metrics_timestamp_is_valid(current_timestamp) ||
        current_totals == NULL ||
        flow_table_capacity == 0U ||
        active_flow_count > flow_table_capacity ||
        report_due == NULL ||
        report == NULL) {
        return EINVAL;
    }

    /*
     * 单调时钟不应倒退。
     *
     * 如果发生倒退，不应继续计算负时间或更新报告基线。
     */
    if (runtime_metrics_timestamp_compare(
            current_timestamp,
            &schedule->last_report_timestamp) < 0) {
        return ERANGE;
    }

    /*
     * 累计量必须保持不变或增加。
     *
     * 如果减小，减法会产生无意义的巨大无符号结果。
     */
    if (!runtime_metrics_totals_are_monotonic(
            current_totals,
            &schedule->last_report_totals)) {
        return ERANGE;
    }

    if (!runtime_metrics_interval_elapsed(
            current_timestamp,
            &schedule->last_report_timestamp,
            schedule->report_interval_seconds)) {
        /*
         * 尚未达到周期时只发布“无需报告”和零值报告，
         * 不更新调度器基线。
         */
        *report_due = false;
        *report = result_report;
        return 0;
    }

    elapsed_whole_seconds = current_timestamp->seconds - schedule->last_report_timestamp.seconds;

    elapsed_nanoseconds = current_timestamp->nanoseconds - schedule->last_report_timestamp.nanoseconds;

    /*
     * 纳秒部分为负时，从整秒字段借1秒。
     *
     * 例如：
     *
     * 100.900000000
     * 106.100000000
     *
     * 实际经过5.2秒，而不是6秒减去一个负纳秒值。
     */
    if (elapsed_nanoseconds < INT32_C(0)) {
        elapsed_whole_seconds -= INT64_C(1);
        elapsed_nanoseconds += INT32_C(RUNTIME_METRICS_NANOSECONDS_PER_SECOND);
    }

    elapsed_seconds =
        (double)elapsed_whole_seconds +
        (double)elapsed_nanoseconds / RUNTIME_METRICS_NANOSECONDS_PER_SECOND;

    result_report.elapsed_seconds = elapsed_seconds;

    result_report.interval_packet_count =
        current_totals->packet_count -
        schedule->last_report_totals.packet_count;

    result_report.interval_captured_byte_count =
        current_totals->captured_byte_count -
        schedule->last_report_totals.captured_byte_count;

    result_report.interval_wire_byte_count =
        current_totals->wire_byte_count -
        schedule->last_report_totals.wire_byte_count;

    result_report.interval_expired_flow_count =
        current_totals->expired_flow_count -
        schedule->last_report_totals.expired_flow_count;

    result_report.packets_per_second =
        (double)result_report.interval_packet_count / elapsed_seconds;

    result_report.captured_megabits_per_second =
        ((double)result_report.interval_captured_byte_count * RUNTIME_METRICS_BITS_PER_BYTE) /
        (elapsed_seconds * RUNTIME_METRICS_BITS_PER_MEGABIT);

    result_report.wire_megabits_per_second =
        ((double)result_report.interval_wire_byte_count * RUNTIME_METRICS_BITS_PER_BYTE) /
        (elapsed_seconds * RUNTIME_METRICS_BITS_PER_MEGABIT);

    result_report.active_flow_count = active_flow_count;

    result_report.flow_table_capacity = flow_table_capacity;

    result_report.flow_table_usage_percent =
        ((double)active_flow_count * 100.0) / (double)flow_table_capacity;

    /*
     * 只有完整报告计算成功后才推进周期基线。
     */
    updated_schedule = *schedule;
    updated_schedule.last_report_timestamp = *current_timestamp;
    updated_schedule.last_report_totals = *current_totals;

    *schedule = updated_schedule;
    *report_due = true;
    *report = result_report;

    return 0;
}