#ifndef NETFLOW_ANALYZER_UDP_H
#define NETFLOW_ANALYZER_UDP_H

#include "analyzer/byte_reader.h"
#include "analyzer/packet_info.h"

#include <stddef.h>
#include <stdint.h>

/*
 * UDP头部长度固定为8字节。
 */
#define UDP_HEADER_LENGTH 8U

/**
 * @brief 解析IPv4负载中的UDP数据报。
 *
 * 调用前必须已经成功解析Ethernet和IPv4，并且IPv4协议号必须为
 * IPV4_PROTOCOL_UDP。
 *
 * 当前阶段不支持IPv4分片重组。遇到IPv4分片时返回ENOTSUP，
 * 避免把分片负载误认为完整UDP数据报。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际捕获并可访问的帧长度。
 * @param packet_info 指向已经包含IPv4结果的统一结果对象。
 *
 * @return 成功时返回0；
 *         参数或调用顺序无效时返回EINVAL；
 *         UDP必要头部未完整捕获时返回ENODATA；
 *         UDP长度字段不合法时返回EBADMSG；
 *         IPv4数据报已经分片时返回ENOTSUP。
 */
int udp_parse(const uint8_t *frame_data,
              size_t frame_length,
              packet_info_t *packet_info);

/**
 * @brief 根据UDP解析结果创建UDP负载只读游标。
 *
 * 函数不会复制负载，也不取得frame_data的所有权。
 * frame_data必须在payload使用期间保持有效。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际捕获并可访问的帧长度。
 * @param packet_info 指向已经包含UDP结果的统一结果对象。
 * @param payload 指向用于接收UDP负载子游标的结构体。
 *
 * @return 成功时返回0，参数或结果状态无效时返回EINVAL。
 */
int udp_payload_view(const uint8_t *frame_data,
                     size_t frame_length,
                     const packet_info_t *packet_info,
                     byte_cursor_t *payload);

#endif