#include "analyzer/flow_features.h"
#include "analyzer/ipv4.h"

#include <errno.h>
#include <stddef.h>

#define FLOW_FEATURES_MICROSECONDS_PER_SECOND UINT64_C(1000000)

/**
 * @brief 判断TCP阶段枚举值是否属于当前版本定义的合法范围。
 */
static bool flow_features_tcp_phase_is_valid(
    tcp_flow_phase_t phase)
{
    switch (phase) {
    case TCP_FLOW_PHASE_UNOBSERVED:
    case TCP_FLOW_PHASE_SYN_SEEN:
    case TCP_FLOW_PHASE_SYN_ACK_SEEN:
    case TCP_FLOW_PHASE_ESTABLISHED:
    case TCP_FLOW_PHASE_MIDSTREAM:
    case TCP_FLOW_PHASE_FIN_SEEN:
    case TCP_FLOW_PHASE_FIN_BIDIRECTIONAL:
    case TCP_FLOW_PHASE_CLOSED:
    case TCP_FLOW_PHASE_RESET:
        return true;

    default:
        return false;
    }
}

/**
 * @brief 检查一个方向的统计值是否自洽。
 */
static bool flow_features_direction_stats_are_valid(
    const flow_direction_stats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    /*
     * 没有数据包时，不应该已经累计出字节数。
     */
    if (stats->packet_count == UINT64_C(0) &&
        (stats->captured_byte_count != UINT64_C(0) ||
         stats->wire_byte_count != UINT64_C(0))) {
        return false;
    }

    /*
     * caplen表示真正保存下来的字节数，
     * 正常情况下不能大于线路上的原始长度。
     */
    if (stats->captured_byte_count >
        stats->wire_byte_count) {
        return false;
    }

    return true;
}

/**
 * @brief 检查时间戳中的微秒部分是否合法。
 */
static bool flow_features_timestamp_is_valid(
    const flow_timestamp_t *timestamp)
{
    if (timestamp == NULL) {
        return false;
    }

    return timestamp->microseconds >= INT32_C(0) &&
           timestamp->microseconds < INT32_C(1000000);
}

/**
 * @brief 比较两个流时间戳。
 */
static int flow_features_timestamp_compare(
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
 * @brief 执行带溢出检查的uint64_t加法。
 *
 * 失败时不修改result。
 */
static int flow_features_add_uint64(
    uint64_t left,
    uint64_t right,
    uint64_t *result)
{
    if (result == NULL) {
        return EINVAL;
    }

    if (right > UINT64_MAX - left) {
        return EOVERFLOW;
    }

    *result = left + right;

    return 0;
}

/**
 * @brief 计算两个时间戳之间的微秒差。
 *
 * 函数避免直接执行：
 *
 *     (last_seconds - first_seconds) * 1000000
 *
 * 因为秒数相减和乘法都可能溢出。
 */
static int flow_features_calculate_duration(
    const flow_timestamp_t *first,
    const flow_timestamp_t *last,
    uint64_t *duration_microseconds)
{
    uint64_t second_difference;
    uint64_t microsecond_difference;

    if (duration_microseconds == NULL ||
        !flow_features_timestamp_is_valid(first) ||
        !flow_features_timestamp_is_valid(last)) {
        return EINVAL;
    }

    if (flow_features_timestamp_compare(first, last) > 0) {
        return EINVAL;
    }

    /*
     * 已经确认last不早于first。
     *
     * 转为uint64_t后进行减法，可以避免有符号整数减法溢出。
     */
    second_difference =
        (uint64_t)last->seconds -
        (uint64_t)first->seconds;

    if (last->microseconds < first->microseconds) {
        /*
         * 例如：
         *
         * first = 100.900000
         * last  = 101.100000
         *
         * 先从秒差中借1秒，再计算剩余的200000微秒。
         */
        if (second_difference == UINT64_C(0)) {
            return EINVAL;
        }

        second_difference -= UINT64_C(1);

        microsecond_difference =
            FLOW_FEATURES_MICROSECONDS_PER_SECOND -
            (uint64_t)first->microseconds +
            (uint64_t)last->microseconds;
    } else {
        microsecond_difference =
            (uint64_t)(
                last->microseconds -
                first->microseconds
            );
    }

    /*
     * 检查：
     *
     * second_difference * 1000000
     *     + microsecond_difference
     *
     * 能否放入uint64_t。
     */
    if (second_difference >
        (UINT64_MAX - microsecond_difference) /
            FLOW_FEATURES_MICROSECONDS_PER_SECOND) {
        return EOVERFLOW;
    }

    *duration_microseconds =
        second_difference *
            FLOW_FEATURES_MICROSECONDS_PER_SECOND +
        microsecond_difference;

    return 0;
}

/**
 * @brief 计算两个方向之间的不平衡度。
 *
 * 计算公式：
 *
 *     abs(left - right) / (left + right)
 *
 * total为0时没有方向性证据，因此返回0.0。
 */
static double flow_features_calculate_imbalance(
    uint64_t left,
    uint64_t right,
    uint64_t total)
{
    uint64_t difference;

    if (total == UINT64_C(0)) {
        return 0.0;
    }

    if (left >= right) {
        difference = left - right;
    } else {
        difference = right - left;
    }

    return (double)difference / (double)total;
}

int flow_features_from_record(
    const flow_record_t *record,
    flow_features_t *features)
{
    flow_features_t new_features;
    uint64_t total_packet_count;
    uint64_t total_captured_byte_count;
    uint64_t total_wire_byte_count;
    uint64_t duration_microseconds;
    int error_code;

    if (record == NULL || features == NULL) {
        return EINVAL;
    }

    if (!record->initialized) {
        return EINVAL;
    }

    /*
     * 先检查协议与TCP状态对象之间的关系。
     */
    switch (record->key.protocol) {
    case IPV4_PROTOCOL_TCP:
        if (!record->tcp_state.initialized ||
            !flow_features_tcp_phase_is_valid(
                record->tcp_state.phase
            )) {
            return EINVAL;
        }
        break;

    case IPV4_PROTOCOL_UDP:
    case IPV4_PROTOCOL_ICMP:
        if (record->tcp_state.initialized) {
            return EINVAL;
        }
        break;

    default:
        return ENOTSUP;
    }

    if (!flow_features_direction_stats_are_valid(
            &record->a_to_b
        ) ||
        !flow_features_direction_stats_are_valid(
            &record->b_to_a
        )) {
        return EINVAL;
    }

    /*
     * 所有总量都使用带溢出检查的加法。
     */
    error_code = flow_features_add_uint64(
        record->a_to_b.packet_count,
        record->b_to_a.packet_count,
        &total_packet_count
    );

    if (error_code != 0) {
        return error_code;
    }

    if (total_packet_count == UINT64_C(0)) {
        return EINVAL;
    }

    error_code = flow_features_add_uint64(
        record->a_to_b.captured_byte_count,
        record->b_to_a.captured_byte_count,
        &total_captured_byte_count
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = flow_features_add_uint64(
        record->a_to_b.wire_byte_count,
        record->b_to_a.wire_byte_count,
        &total_wire_byte_count
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = flow_features_calculate_duration(
        &record->first_seen,
        &record->last_seen,
        &duration_microseconds
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 先完整构造局部对象。
     *
     * 即使后续代码修改时增加新的失败条件，
     * 也不会向调用者发布半初始化的features。
     */
    new_features = (flow_features_t){
        .protocol = record->key.protocol,
        .tcp_phase = TCP_FLOW_PHASE_UNOBSERVED,
        .duration_microseconds = duration_microseconds,
        .total_packet_count = total_packet_count,
        .total_captured_byte_count =
            total_captured_byte_count,
        .total_wire_byte_count =
            total_wire_byte_count,
        .mean_captured_bytes_per_packet =
            (double)total_captured_byte_count /
            (double)total_packet_count,
        .mean_wire_bytes_per_packet =
            (double)total_wire_byte_count /
            (double)total_packet_count,
        .packet_count_imbalance_ratio =
            flow_features_calculate_imbalance(
                record->a_to_b.packet_count,
                record->b_to_a.packet_count,
                total_packet_count
            ),
        .wire_byte_count_imbalance_ratio =
            flow_features_calculate_imbalance(
                record->a_to_b.wire_byte_count,
                record->b_to_a.wire_byte_count,
                total_wire_byte_count
            ),
        .tcp_state_applicable = false,
        .tcp_handshake_completed = false,
        .initialized = true
    };

    if (record->key.protocol == IPV4_PROTOCOL_TCP) {
        new_features.tcp_phase =
            record->tcp_state.phase;
        new_features.tcp_state_applicable = true;
        new_features.tcp_handshake_completed =
            record->tcp_state.handshake_completed;
    }

    /*
     * 全部验证和计算成功后，才把结果发布给调用者。
     */
    *features = new_features;

    return 0;
}
