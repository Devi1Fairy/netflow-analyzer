#include "analyzer/flow_expiration.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>

static bool flow_expiration_timestamp_is_valid(
    const flow_timestamp_t *timestamp)
{
    return timestamp != NULL &&
           timestamp->microseconds >= INT32_C(0) &&
           timestamp->microseconds <= INT32_C(999999);
}

static int flow_expiration_timestamp_compare(
    const flow_timestamp_t *left,
    const flow_timestamp_t *right)
{
    if (left->seconds < right->seconds) {
        return -1;
    }

    if (left->seconds > right->seconds) {
        return 1;
    }

    if (left->microseconds < right->microseconds) {
        return -1;
    }

    if (left->microseconds > right->microseconds) {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断current是否至少比earlier晚interval_seconds。
 *
 * 使用“earlier + interval”进行比较，并在加法前检查上溢。
 */
static bool flow_expiration_interval_elapsed(
    const flow_timestamp_t *current,
    const flow_timestamp_t *earlier,
    int64_t interval_seconds)
{
    flow_timestamp_t target;

    /*
     * 如果目标时间会超过INT64_MAX，那么当前int64_t时间戳
     * 不可能合法地到达该目标。
     */
    if (earlier->seconds > INT64_MAX - interval_seconds) {
        return false;
    }

    target = (flow_timestamp_t){
        .seconds = earlier->seconds + interval_seconds,
        .microseconds = earlier->microseconds
    };

    return flow_expiration_timestamp_compare(
        current,
        &target
    ) >= 0;
}

static bool flow_expiration_schedule_is_valid(
    const flow_expiration_schedule_t *schedule)
{
    if (schedule == NULL ||
        !schedule->initialized ||
        schedule->idle_timeout_seconds <= INT64_C(0) ||
        schedule->scan_interval_seconds <= INT64_C(0)) {
        return false;
    }

    if (!schedule->has_observation) {
        return true;
    }

    if (!flow_expiration_timestamp_is_valid(
            &schedule->latest_timestamp) ||
        !flow_expiration_timestamp_is_valid(
            &schedule->last_scan_timestamp)) {
        return false;
    }

    /*
     * 上一次扫描时间不能晚于当前时间高水位。
     */
    return flow_expiration_timestamp_compare(
        &schedule->last_scan_timestamp,
        &schedule->latest_timestamp
    ) <= 0;
}

int flow_expiration_schedule_init(
    flow_expiration_schedule_t *schedule,
    int64_t idle_timeout_seconds,
    int64_t scan_interval_seconds)
{
    flow_expiration_schedule_t new_schedule;

    if (schedule == NULL ||
        idle_timeout_seconds <= INT64_C(0) ||
        scan_interval_seconds <= INT64_C(0)) {
        return EINVAL;
    }

    new_schedule = (flow_expiration_schedule_t){
        .latest_timestamp = {0},
        .last_scan_timestamp = {0},
        .idle_timeout_seconds =
            idle_timeout_seconds,
        .scan_interval_seconds =
            scan_interval_seconds,
        .has_observation = false,
        .initialized = true
    };

    *schedule = new_schedule;

    return 0;
}

int flow_expiration_schedule_observe(
    flow_expiration_schedule_t *schedule,
    const flow_timestamp_t *timestamp,
    bool *scan_due,
    flow_timestamp_t *cutoff)
{
    flow_expiration_schedule_t updated_schedule;
    flow_timestamp_t result_cutoff = {0};
    bool result_scan_due = false;

    if (!flow_expiration_schedule_is_valid(schedule) ||
        !flow_expiration_timestamp_is_valid(timestamp) ||
        scan_due == NULL ||
        cutoff == NULL) {
        return EINVAL;
    }

    updated_schedule = *schedule;

    /*
     * 第一条数据包只建立时间基准。
     */
    if (!updated_schedule.has_observation) {
        updated_schedule.latest_timestamp = *timestamp;
        updated_schedule.last_scan_timestamp = *timestamp;
        updated_schedule.has_observation = true;
    } else {
        /*
         * 只允许时间高水位前进，不允许乱序包让它倒退。
         */
        if (flow_expiration_timestamp_compare(
                timestamp,
                &updated_schedule.latest_timestamp) > 0) {
            updated_schedule.latest_timestamp = *timestamp;
        }

        if (flow_expiration_interval_elapsed(
                &updated_schedule.latest_timestamp,
                &updated_schedule.last_scan_timestamp,
                updated_schedule.scan_interval_seconds)) {
            /*
             * 计算截止时间前检查有符号减法下溢。
             */
            if (updated_schedule.latest_timestamp.seconds < INT64_MIN +
                    updated_schedule.idle_timeout_seconds) {
                return ERANGE;
            }

            result_cutoff = (flow_timestamp_t){
                .seconds =
                    updated_schedule.latest_timestamp.seconds - updated_schedule.idle_timeout_seconds,
                .microseconds =
                    updated_schedule.latest_timestamp.microseconds
            };

            updated_schedule.last_scan_timestamp = updated_schedule.latest_timestamp;
            result_scan_due = true;
        }
    }

    /*
     * 所有计算成功后再发布状态和输出参数。
     */
    *schedule = updated_schedule;
    *scan_due = result_scan_due;
    *cutoff = result_cutoff;

    return 0;
}