#include "analyzer/flow_record.h"

#include <errno.h>
#include <stddef.h>

/**
 * @brief 比较两个流时间戳。
 *
 * @return left早于right时返回负数；
 *         相同时返回0；
 *         left晚于right时返回正数。
 */
static int flow_timestamp_compare(
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
 * @brief 向一个方向的统计对象中累计一条数据包。
 *
 * 所有溢出检查在修改stats之前完成。
 */
static int flow_direction_stats_add_packet(
    flow_direction_stats_t *stats,
    const packet_info_t *packet)
{
    uint64_t captured_length;
    uint64_t wire_length;

    if (stats == NULL || packet == NULL) {
        return EINVAL;
    }

    captured_length = (uint64_t)packet->captured_length;
    wire_length = (uint64_t)packet->wire_length;

    /*
     * 无符号整数溢出不会由C语言自动报错，而是发生模运算回绕。
     *
     * 例如UINT64_MAX + 1会变成0，所以必须提前检查。
     */
    if (stats->packet_count == UINT64_MAX ||
        captured_length > UINT64_MAX - stats->captured_byte_count ||
        wire_length > UINT64_MAX - stats->wire_byte_count) {
        return EOVERFLOW;
    }

    stats->packet_count += UINT64_C(1);
    stats->captured_byte_count += captured_length;
    stats->wire_byte_count += wire_length;

    return 0;
}

/**
 * @brief 根据数据包方向选择对应的方向统计对象。
 */
static flow_direction_stats_t *flow_record_select_stats(
    flow_record_t *record,
    flow_direction_t direction)
{
    if (record == NULL) {
        return NULL;
    }

    switch (direction) {
    case FLOW_DIRECTION_A_TO_B:
        return &record->a_to_b;

    case FLOW_DIRECTION_B_TO_A:
        return &record->b_to_a;

    default:
        return NULL;
    }
}

int flow_record_init(
    flow_record_t *record,
    const packet_info_t *first_packet)
{
    flow_record_t new_record;
    flow_direction_stats_t *direction_stats;
    flow_direction_t direction;
    flow_key_t key;
    int error_code;

    if (record == NULL || first_packet == NULL) {
        return EINVAL;
    }

    /*
     * 从第一条数据包生成规范化双向流键。
     */
    error_code = flow_key_from_packet(
        first_packet,
        &key,
        &direction
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 先完整建立局部流记录，失败时不会污染调用者原有record。
     */
    new_record = (flow_record_t){
        .key = key,
        .tcp_state = {0},
        .a_to_b = {
            .packet_count = UINT64_C(0),
            .captured_byte_count = UINT64_C(0),
            .wire_byte_count = UINT64_C(0)
        },
        .b_to_a = {
            .packet_count = UINT64_C(0),
            .captured_byte_count = UINT64_C(0),
            .wire_byte_count = UINT64_C(0)
        },
        .first_seen = {
            .seconds = first_packet->timestamp_seconds,
            .microseconds =
                first_packet->timestamp_microseconds
        },
        .last_seen = {
            .seconds = first_packet->timestamp_seconds,
            .microseconds =
                first_packet->timestamp_microseconds
        },
        .initialized = true
    };

    /*
     * TCP流拥有独立的连接状态对象。
     *
     * flow_key_from_packet已经验证协议号与has_tcp一致，
     * 所以这里可以使用has_tcp判断是否需要初始化状态机。
     */
    if (first_packet->has_tcp) {
        error_code = tcp_flow_state_init(
            &new_record.tcp_state
        );

        if (error_code != 0) {
            return error_code;
        }

        /*
         * 创建流记录时，第一包同样必须进入状态机。
         *
         * 如果第一包是SYN，状态进入SYN_SEEN；
         * 如果抓包从普通ACK开始，则进入MIDSTREAM。
         */
        error_code = tcp_flow_state_observe(
            &new_record.tcp_state,
            direction,
            first_packet->tcp_flags
        );

        if (error_code != 0) {
            return error_code;
        }
    }

    direction_stats = flow_record_select_stats(
        &new_record,
        direction
    );

    if (direction_stats == NULL) {
        return EINVAL;
    }

    error_code = flow_direction_stats_add_packet(
        direction_stats,
        first_packet
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 局部对象完整建立后，再发布给调用者。
     */
    *record = new_record;

    return 0;
}

int flow_record_update(
    flow_record_t *record,
    const packet_info_t *packet)
{
    flow_record_t updated_record;
    flow_direction_stats_t *direction_stats;

    flow_timestamp_t packet_timestamp;
    flow_direction_t direction;
    flow_key_t packet_key;

    int error_code;

    if (record == NULL ||
        packet == NULL ||
        !record->initialized) {
        return EINVAL;
    }

    /*
     * 重新从数据包提取流键和方向。
     *
     * 这会同时验证packet是否已经完整解析。
     */
    error_code = flow_key_from_packet(
        packet,
        &packet_key,
        &direction
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 防止调用者把其他连接的数据包加入当前流。
     */
    if (!flow_key_equal(&record->key, &packet_key)) {
        return ENOENT;
    }

    /*
     * 在副本上完成所有更新。
     *
     * 即使后面发现计数器溢出，原record也不会被部分修改。
     */
    updated_record = *record;

    /*
     * TCP流记录必须拥有一个已经初始化的TCP状态对象。
     *
     * UDP或ICMP流则不能意外携带有效TCP状态。
     * 这些检查用于发现内部结构被错误构造或破坏的情况。
     */
    if (packet->has_tcp) {
        if (!updated_record.tcp_state.initialized) {
            return EINVAL;
        }

        error_code = tcp_flow_state_observe(
            &updated_record.tcp_state,
            direction,
            packet->tcp_flags
        );

        if (error_code != 0) {
            return error_code;
        }
    } else if (updated_record.tcp_state.initialized) {
        return EINVAL;
    }

    direction_stats = flow_record_select_stats(
        &updated_record,
        direction
    );

    if (direction_stats == NULL) {
        return EINVAL;
    }

    error_code = flow_direction_stats_add_packet(
        direction_stats,
        packet
    );

    if (error_code != 0) {
        return error_code;
    }

    packet_timestamp = (flow_timestamp_t){
        .seconds = packet->timestamp_seconds,
        .microseconds = packet->timestamp_microseconds
    };

    /*
     * PCAP中的数据包通常按时间排序，但合并多个抓包文件、
     * 多接口抓包或时间校正后可能出现乱序。
     *
     * 因此不能简单地把当前时间赋给last_seen，而应计算最小值和最大值。
     */
    if (flow_timestamp_compare(&packet_timestamp, &updated_record.first_seen) < 0) {
        updated_record.first_seen = packet_timestamp;
    }

    if (flow_timestamp_compare(&packet_timestamp, &updated_record.last_seen) > 0) {
        updated_record.last_seen = packet_timestamp;
    }

    /*
     * 全部更新成功后再发布结果。
     */
    *record = updated_record;

    return 0;
}