#include "analyzer/flow_key.h"

#include "analyzer/ipv4.h"

#include <errno.h>

/**
 * @brief 按IPv4地址和端口比较两个端点。
 *
 * 比较顺序：
 *
 * 1. 先比较IPv4地址；
 * 2. IPv4地址相同时再比较端口。
 *
 * @return left较小时返回负数；
 *         完全相同时返回0；
 *         left较大时返回正数。
 */
static int flow_endpoint_compare(
    const flow_endpoint_t *left,
    const flow_endpoint_t *right)
{
    if (left->ipv4_address < right->ipv4_address) {
        return -1;
    }

    if (left->ipv4_address > right->ipv4_address) {
        return 1;
    }

    if (left->port < right->port) {
        return -1;
    }

    if (left->port > right->port) {
        return 1;
    }

    return 0;
}

/**
 * @brief 判断两个端点的IP地址和端口是否相等。
 */
static bool flow_endpoint_equal(
    const flow_endpoint_t *left,
    const flow_endpoint_t *right)
{
    return left->ipv4_address == right->ipv4_address &&
           left->port == right->port;
}

int flow_key_from_packet(
    const packet_info_t *packet_info,
    flow_key_t *key,
    flow_direction_t *direction)
{
    flow_endpoint_t source_endpoint;
    flow_endpoint_t destination_endpoint;

    flow_key_t new_key;
    flow_direction_t new_direction;

    uint16_t source_port;
    uint16_t destination_port;

    /*
     * 只有完成协议分发的数据包才能建立流键。
     *
     * 如果数据包仍然处于NOT_STARTED、TRUNCATED、MALFORMED或
     * UNSUPPORTED状态，就不能假设上层字段完整有效。
     */
    if (packet_info == NULL ||
        key == NULL ||
        direction == NULL ||
        !packet_info->initialized ||
        !packet_info->has_ipv4 ||
        packet_info->parse_status !=
            PACKET_PARSE_STATUS_COMPLETE) {
        return EINVAL;
    }

    /*
     * 根据IPv4协议号选择对应端口。
     */
    switch (packet_info->ipv4_protocol) {
    case IPV4_PROTOCOL_TCP:
        /*
         * 协议号和解析结果必须相互一致。
         */
        if (!packet_info->has_tcp ||
            packet_info->has_udp ||
            packet_info->has_icmp) {
            return EINVAL;
        }

        source_port = packet_info->tcp_source_port;
        destination_port =
            packet_info->tcp_destination_port;
        break;

    case IPV4_PROTOCOL_UDP:
        if (packet_info->has_tcp ||
            !packet_info->has_udp ||
            packet_info->has_icmp) {
            return EINVAL;
        }

        source_port = packet_info->udp_source_port;
        destination_port =
            packet_info->udp_destination_port;
        break;

    case IPV4_PROTOCOL_ICMP:
        if (packet_info->has_tcp ||
            packet_info->has_udp ||
            !packet_info->has_icmp) {
            return EINVAL;
        }

        /*
         * 标准五元组没有ICMP专用字段，因此第一版用0作为端口。
         */
        source_port = UINT16_C(0);
        destination_port = UINT16_C(0);
        break;

    default:
        return ENOTSUP;
    }

    source_endpoint = (flow_endpoint_t){
        .ipv4_address = packet_info->source_ipv4,
        .port = source_port
    };

    destination_endpoint = (flow_endpoint_t){
        .ipv4_address = packet_info->destination_ipv4,
        .port = destination_port
    };

    /*
     * 对两个端点进行规范化排序。
     *
     * 排序完成后，不论原始数据包方向如何，同一对端点都会得到
     * 相同的endpoint_a和endpoint_b。
     */
    if (flow_endpoint_compare(&source_endpoint, &destination_endpoint) <= 0) {
        new_key = (flow_key_t){
            .endpoint_a = source_endpoint,
            .endpoint_b = destination_endpoint,
            .protocol = packet_info->ipv4_protocol
        };

        new_direction = FLOW_DIRECTION_A_TO_B;
    } else {
        new_key = (flow_key_t){
            .endpoint_a = destination_endpoint,
            .endpoint_b = source_endpoint,
            .protocol = packet_info->ipv4_protocol
        };

        new_direction = FLOW_DIRECTION_B_TO_A;
    }

    /*
     * 所有检查和构造成功后，再发布两个输出结果。
     */
    *key = new_key;
    *direction = new_direction;

    return 0;
}

/**
 * FNV-1a 64位哈希算法的初始值。
 */
#define FLOW_HASH_FNV_OFFSET_BASIS UINT64_C(14695981039346656037)

/**
 * FNV-1a 64位哈希算法使用的质数。
 */
#define FLOW_HASH_FNV_PRIME UINT64_C(1099511628211)

/**
 * @brief 将一个字节加入当前FNV-1a哈希状态。
 */
static uint64_t flow_hash_append_byte(uint64_t current_hash, uint8_t value)
{
    current_hash ^= (uint64_t)value;

    /*
     * 这里的无符号整数回绕是FNV-1a算法的一部分，
     * 不属于需要报告的计数器溢出错误。
     */
    current_hash *= FLOW_HASH_FNV_PRIME;

    return current_hash;
}

/**
 * @brief 按固定的大端字节顺序加入一个16位整数。
 *
 * 不能直接读取value的内存字节，否则哈希结果会受到主机大小端字节序影响。
 */
static uint64_t flow_hash_append_u16(uint64_t current_hash, uint16_t value)
{
    current_hash = flow_hash_append_byte(
        current_hash,
        (uint8_t)(
            (value >> 8U) &
            UINT16_C(0x00FF)
        )
    );

    current_hash = flow_hash_append_byte(
        current_hash,
        (uint8_t)(
            value &
            UINT16_C(0x00FF)
        )
    );

    return current_hash;
}

/**
 * @brief 按固定的大端字节顺序加入一个32位整数。
 */
static uint64_t flow_hash_append_u32(
    uint64_t current_hash,
    uint32_t value)
{
    current_hash = flow_hash_append_byte(
        current_hash,
        (uint8_t)(
            (value >> 24U) &
            UINT32_C(0x000000FF)
        )
    );

    current_hash = flow_hash_append_byte(
        current_hash,
        (uint8_t)(
            (value >> 16U) &
            UINT32_C(0x000000FF)
        )
    );

    current_hash = flow_hash_append_byte(
        current_hash,
        (uint8_t)(
            (value >> 8U) &
            UINT32_C(0x000000FF)
        )
    );

    current_hash = flow_hash_append_byte(
        current_hash,
        (uint8_t)(
            value &
            UINT32_C(0x000000FF)
        )
    );

    return current_hash;
}

bool flow_key_equal(const flow_key_t *left,
                    const flow_key_t *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }

    return left->protocol == right->protocol &&
           flow_endpoint_equal(
               &left->endpoint_a,
               &right->endpoint_a
           ) &&
           flow_endpoint_equal(
               &left->endpoint_b,
               &right->endpoint_b
           );
}

int flow_key_hash(const flow_key_t *key, uint64_t *hash_value)
{
    uint64_t result_hash;

    if (key == NULL || hash_value == NULL) {
        return EINVAL;
    }

    result_hash = FLOW_HASH_FNV_OFFSET_BASIS;

    /*
     * 只处理有业务意义的字段，不读取结构体填充字节。
     */
    result_hash = flow_hash_append_u32(
        result_hash,
        key->endpoint_a.ipv4_address
    );

    result_hash = flow_hash_append_u16(
        result_hash,
        key->endpoint_a.port
    );

    result_hash = flow_hash_append_u32(
        result_hash,
        key->endpoint_b.ipv4_address
    );

    result_hash = flow_hash_append_u16(
        result_hash,
        key->endpoint_b.port
    );

    result_hash = flow_hash_append_byte(
        result_hash,
        key->protocol
    );

    /*
     * 全部计算完成后再写入输出参数。
     */
    *hash_value = result_hash;

    return 0;
}