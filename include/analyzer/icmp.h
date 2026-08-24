#ifndef NETFLOW_ANALYZER_ICMP_H
#define NETFLOW_ANALYZER_ICMP_H

#include "analyzer/byte_reader.h"
#include "analyzer/packet_info.h"

#include <stddef.h>
#include <stdint.h>

/*
 * ICMP公共头部固定为8字节。
 *
 * 前4字节是type、code和checksum，
 * 后4字节的含义根据type变化。
 */
#define ICMP_HEADER_LENGTH 8U

/*
 * 常见ICMP消息类型。
 *
 * 这些数值由ICMP协议标准规定，不是本项目自定义值。
 */
#define ICMP_TYPE_ECHO_REPLY              UINT8_C(0)
#define ICMP_TYPE_DESTINATION_UNREACHABLE UINT8_C(3)
#define ICMP_TYPE_REDIRECT                UINT8_C(5)
#define ICMP_TYPE_ECHO_REQUEST            UINT8_C(8)
#define ICMP_TYPE_TIME_EXCEEDED           UINT8_C(11)
#define ICMP_TYPE_PARAMETER_PROBLEM       UINT8_C(12)

/**
 * @brief 解析IPv4负载中的ICMP消息。
 *
 * 调用前必须已经成功解析Ethernet和IPv4，并且IPv4协议号必须为
 * IPV4_PROTOCOL_ICMP。
 *
 * 当前阶段不支持IPv4分片重组。遇到IPv4分片时返回ENOTSUP。
 *
 * 对Echo Request和Echo Reply，函数还会提取identifier和sequence。
 * 对其他ICMP类型，仍会保存公共字段和rest_of_header原始值。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际捕获并可访问的帧长度。
 * @param packet_info 指向已经包含IPv4结果的统一结果对象。
 *
 * @return 成功时返回0；
 *         参数或调用顺序无效时返回EINVAL；
 *         ICMP必要头部未完整捕获时返回ENODATA；
 *         IPv4声明的ICMP区域小于公共头部时返回EBADMSG；
 *         IPv4数据报已经分片时返回ENOTSUP。
 */
int icmp_parse(const uint8_t *frame_data,
               size_t frame_length,
               packet_info_t *packet_info);

/**
 * @brief 根据ICMP解析结果创建ICMP负载只读游标。
 *
 * 函数不会复制负载，也不取得frame_data的所有权。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际捕获并可访问的帧长度。
 * @param packet_info 指向已经包含ICMP结果的统一结果对象。
 * @param payload 指向用于接收ICMP负载子游标的结构体。
 *
 * @return 成功时返回0，参数或结果状态无效时返回EINVAL。
 */
int icmp_payload_view(const uint8_t *frame_data,
                      size_t frame_length,
                      const packet_info_t *packet_info,
                      byte_cursor_t *payload);

#endif