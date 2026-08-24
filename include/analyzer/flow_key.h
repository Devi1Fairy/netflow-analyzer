#ifndef NETFLOW_ANALYZER_FLOW_KEY_H
#define NETFLOW_ANALYZER_FLOW_KEY_H

#include "analyzer/packet_info.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 表示一条网络流中的一个通信端点。
 *
 * 端点由IPv4地址和端口共同组成：
 *
 * 192.168.1.10:52134
 *
 * ICMP没有端口，因此ICMP端点的port统一设置为0。
 */
typedef struct {
    /**
     * IPv4地址的32位数值形式。
     */
    uint32_t ipv4_address;

    /**
     * TCP或UDP端口。
     *
     * ICMP消息使用0。
     */
    uint16_t port;
} flow_endpoint_t;

/**
 * @brief 表示一条双向IPv4网络流的规范化键。
 *
 * endpoint_a和endpoint_b不是固定的客户端与服务器。
 * 它们按照IP地址和端口排序，从而保证正向包和反向包生成相同的键。
 */
typedef struct {
    /**
     * 排序后较小的通信端点。
     */
    flow_endpoint_t endpoint_a;

    /**
     * 排序后较大的通信端点。
     */
    flow_endpoint_t endpoint_b;

    /**
     * IPv4上层协议号。
     *
     * 常见值：
     *
     * 1  = ICMP；
     * 6  = TCP；
     * 17 = UDP。
     */
    uint8_t protocol;
} flow_key_t;

/**
 * @brief 表示一个数据包相对于规范化流键的方向。
 */
typedef enum {
    /**
     * 尚未获得有效方向。
     */
    FLOW_DIRECTION_UNKNOWN = 0,

    /**
     * 数据包源端点是endpoint_a，目标端点是endpoint_b。
     */
    FLOW_DIRECTION_A_TO_B,

    /**
     * 数据包源端点是endpoint_b，目标端点是endpoint_a。
     */
    FLOW_DIRECTION_B_TO_A
} flow_direction_t;

/**
 * @brief 从一条完整解析的数据包构造双向五元组键。
 *
 * 函数支持TCP、UDP和ICMP：
 *
 * - TCP使用TCP源端口和目标端口；
 * - UDP使用UDP源端口和目标端口；
 * - ICMP的两个端口都设置为0。
 *
 * 函数根据IP地址和端口对两个端点排序，使同一连接的正向包和
 * 反向包得到相同的flow_key_t。
 *
 * endpoint_a只是规范化排序后较小的端点，不一定是客户端。
 *
 * 当源端点和目标端点完全相同时，方向统一设置为A_TO_B。
 *
 * 函数失败时不修改key和direction。
 *
 * @param packet_info 指向已经完整解析的数据包结果。
 * @param key 指向用于接收规范化流键的结构体。
 * @param direction 指向用于接收当前数据包方向的枚举变量。
 *
 * @return 成功时返回0；
 *         参数或packet_info状态无效时返回EINVAL；
 *         协议无法组成当前流键时返回ENOTSUP。
 */
int flow_key_from_packet(
    const packet_info_t *packet_info,
    flow_key_t *key,
    flow_direction_t *direction);

/**
 * @brief 判断两个流键是否完全相等。
 *
 * 函数逐字段比较，不直接对结构体执行memcmp。
 *
 * C编译器可能在结构体成员之间加入填充字节。填充字节不属于业务
 * 数据，其内容也不一定稳定，因此不适合参与流键比较。
 *
 * @param left 第一个流键。
 * @param right 第二个流键。
 *
 * @return 两个非空流键字段完全相同时返回true，否则返回false。
 */
bool flow_key_equal(const flow_key_t *left,
                    const flow_key_t *right);

#endif