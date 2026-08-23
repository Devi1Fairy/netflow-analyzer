#ifndef NETFLOW_ANALYZER_IPV4_H
#define NETFLOW_ANALYZER_IPV4_H

#include "analyzer/byte_reader.h"
#include "analyzer/packet_info.h"

#include <stddef.h>
#include <stdint.h>

/*
 * IPv4版本号固定为4。
 */
#define IPV4_VERSION UINT8_C(4)

/*
 * IPv4头部使用IHL字段表示头部包含多少个32位字。
 *
 * 不带选项时为5个32位字，也就是20字节；
 * IHL最大为15，也就是60字节。
 */
#define IPV4_MIN_HEADER_LENGTH 20U
#define IPV4_MAX_HEADER_LENGTH 60U

/*
 * 点分十进制IPv4地址的最大文本长度。
 *
 * "255.255.255.255"包含15个可见字符，
 * 再加字符串结束符'\0'，共需要16字节。
 */
#define IPV4_ADDRESS_STRING_SIZE 16U

/*
 * IPv4头部中的常见上层协议号。
 *
 * 这些数值由互联网协议标准统一规定，不是本项目自定义值。
 */
#define IPV4_PROTOCOL_ICMP UINT8_C(1)
#define IPV4_PROTOCOL_TCP  UINT8_C(6)
#define IPV4_PROTOCOL_UDP  UINT8_C(17)

/**
 * @brief 解析Ethernet帧中的IPv4头部。
 *
 * 调用前必须已经成功执行ethernet_parse，并且EtherType必须为IPv4。
 *
 * 函数解析：
 *
 * - 版本号和头部长度；
 * - 数据报总长度；
 * - 标识和分片信息；
 * - TTL和上层协议号；
 * - 头部校验和；
 * - 源IPv4地址和目标IPv4地址；
 * - 当前捕获到的IPv4负载范围。
 *
 * IPv4头部完整、但数据报负载只捕获了一部分时，函数仍然成功，
 * 并通过packet_info->ipv4_payload_truncated记录这一事实。
 *
 * 当前阶段只保存校验和字段，不验证校验和。实际抓包可能受到网卡
 * checksum offload影响，在发送端抓到的校验和不一定已经由网卡填好。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际可以访问的捕获长度。
 * @param packet_info 指向已包含Ethernet结果的统一结果对象。
 *
 * @return 成功时返回0；
 *         参数或调用顺序无效时返回EINVAL；
 *         IPv4必要头部被截断时返回ENODATA；
 *         IPv4字段组合不合法时返回EBADMSG。
 */
int ipv4_parse(const uint8_t *frame_data,
               size_t frame_length,
               packet_info_t *packet_info);

/**
 * @brief 根据IPv4解析结果创建IPv4负载游标。
 *
 * 函数不会复制负载，payload只借用frame_data中的一段内存。
 * frame_data必须在payload使用期间保持有效。
 *
 * IPv4分片偏移不为0时，这段负载不一定从完整的传输层头部开始，
 * 因此调用者必须先检查分片信息，再决定是否调用TCP或UDP解析器。
 *
 * @param frame_data 指向完整Ethernet帧的只读字节。
 * @param frame_length 实际可以访问的捕获长度。
 * @param packet_info 指向已经成功保存IPv4结果的对象。
 * @param payload 指向用于接收IPv4负载子游标的结构体。
 *
 * @return 成功时返回0，参数或结果状态无效时返回EINVAL。
 */
int ipv4_payload_view(const uint8_t *frame_data,
                      size_t frame_length,
                      const packet_info_t *packet_info,
                      byte_cursor_t *payload);

/**
 * @brief 把32位IPv4地址格式化为点分十进制字符串。
 *
 * 例如0xC0A8010A会被格式化为：
 *
 * 192.168.1.10
 *
 * buffer至少需要IPV4_ADDRESS_STRING_SIZE字节。
 * 函数失败时不修改buffer。
 *
 * @param address IPv4地址的32位数值形式。
 * @param buffer 指向接收字符串的字符数组。
 * @param buffer_size buffer的总容量。
 *
 * @return 成功时返回0；
 *         buffer为NULL时返回EINVAL；
 *         缓冲区过小时返回ENOSPC；
 *         格式化失败时返回EIO。
 */
int ipv4_format_address(uint32_t address,
                        char *buffer,
                        size_t buffer_size);

#endif