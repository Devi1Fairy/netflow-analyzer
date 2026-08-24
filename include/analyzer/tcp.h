#ifndef NETFLOW_ANALYZER_TCP_H
#define NETFLOW_ANALYZER_TCP_H

#include "analyzer/byte_reader.h"
#include "analyzer/packet_info.h"

#include <stddef.h>
#include <stdint.h>

/*
 * TCP头部中的Data Offset以32位字为单位。
 *
 * 最小值5表示20字节，最大值15表示60字节。
 */
#define TCP_MIN_HEADER_LENGTH 20U
#define TCP_MAX_HEADER_LENGTH 60U

/*
 * TCP控制标志。
 *
 * 每个宏只占一个二进制位，因此可以使用按位或“|”组合：
 *
 * TCP_FLAG_SYN | TCP_FLAG_ACK
 */
#define TCP_FLAG_FIN UINT16_C(0x0001)
#define TCP_FLAG_SYN UINT16_C(0x0002)
#define TCP_FLAG_RST UINT16_C(0x0004)
#define TCP_FLAG_PSH UINT16_C(0x0008)
#define TCP_FLAG_ACK UINT16_C(0x0010)
#define TCP_FLAG_URG UINT16_C(0x0020)
#define TCP_FLAG_ECE UINT16_C(0x0040)
#define TCP_FLAG_CWR UINT16_C(0x0080)
#define TCP_FLAG_NS  UINT16_C(0x0100)

/**
 * @brief 解析IPv4负载中的TCP头部。
 *
 * 调用前必须已经成功完成Ethernet和IPv4解析，并且IPv4协议号
 * 必须为IPV4_PROTOCOL_TCP。
 *
 * 当前阶段不执行IPv4分片重组。遇到任何IPv4分片时返回ENOTSUP，
 * 避免把不完整的TCP段误认为完整TCP数据。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际捕获并可访问的帧长度。
 * @param packet_info 指向已经包含IPv4结果的统一结果对象。
 *
 * @return 成功时返回0；
 *         参数或调用顺序无效时返回EINVAL；
 *         TCP必要头部未完整捕获时返回ENODATA；
 *         TCP字段组合不合法时返回EBADMSG；
 *         IPv4数据报已经分片时返回ENOTSUP。
 */
int tcp_parse(const uint8_t *frame_data,
              size_t frame_length,
              packet_info_t *packet_info);

/**
 * @brief 根据TCP解析结果创建TCP负载只读游标。
 *
 * 函数不会复制TCP负载，也不取得原始数据所有权。
 * frame_data必须在payload使用期间保持有效。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际捕获并可访问的帧长度。
 * @param packet_info 指向已经包含TCP结果的统一结果对象。
 * @param payload 指向用于接收TCP负载子游标的结构体。
 *
 * @return 成功时返回0，参数或结果状态无效时返回EINVAL。
 */
int tcp_payload_view(const uint8_t *frame_data,
                     size_t frame_length,
                     const packet_info_t *packet_info,
                     byte_cursor_t *payload);

#endif