#ifndef NETFLOW_ANALYZER_IPV4_DISPATCH_H
#define NETFLOW_ANALYZER_IPV4_DISPATCH_H

#include "analyzer/packet_info.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 根据IPv4协议号调用对应的上层协议解析器。
 *
 * 调用前必须已经成功完成Ethernet和IPv4解析。
 *
 * 当前支持：
 *
 * - ICMP，IPv4协议号1；
 * - TCP，IPv4协议号6；
 * - UDP，IPv4协议号17。
 *
 * 对支持的协议，分发器调用对应解析器。解析成功后，通过
 * packet_info_mark_complete把数据包标记为完整解析。
 *
 * 对未知协议，函数把解析状态设置为UNSUPPORTED并返回ENOTSUP。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际捕获并可访问的帧长度。
 * @param packet_info 指向已经包含IPv4解析结果的统一结果对象。
 *
 * @return 成功时返回0；
 *         调用状态无效时返回EINVAL；
 *         上层协议暂不支持时返回ENOTSUP；
 *         协议解析失败时返回对应解析器的错误码。
 */
int ipv4_dispatch_payload(const uint8_t *frame_data,
                          size_t frame_length,
                          packet_info_t *packet_info);

#endif