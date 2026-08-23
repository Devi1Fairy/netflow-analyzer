#ifndef NETFLOW_ANALYZER_ETHERNET_H
#define NETFLOW_ANALYZER_ETHERNET_H

#include "analyzer/byte_reader.h"
#include "analyzer/packet_info.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Ethernet MAC地址长度固定为6字节。
 */
#define ETHERNET_MAC_ADDRESS_LENGTH PACKET_MAC_ADDRESS_LENGTH

/*
 * 普通Ethernet II头部固定为14字节：
 *
 * 目标MAC 6字节 + 源MAC 6字节 + EtherType 2字节。
 */
#define ETHERNET_HEADER_LENGTH 14U

/*
 * 格式化MAC地址所需的缓冲区大小。
 *
 * "00:11:22:33:44:55"包含17个可见字符，
 * 再加1个字符串结束符'\0'，总共需要18字节。
 */
#define ETHERNET_MAC_STRING_SIZE 18U

/*
 * 常见EtherType。
 *
 * UINT16_C保证常量使用与uint16_t兼容的无符号整数类型。
 */
#define ETHERNET_TYPE_IPV4 UINT16_C(0x0800)
#define ETHERNET_TYPE_ARP UINT16_C(0x0806)
#define ETHERNET_TYPE_VLAN UINT16_C(0x8100)
#define ETHERNET_TYPE_IPV6 UINT16_C(0x86DD)

/**
 * @brief 解析一条普通Ethernet II帧。
 *
 * 函数成功时把目标MAC、源MAC、EtherType以及网络层负载范围写入
 * packet_info。
 *
 * 未知EtherType不属于Ethernet解析错误。Ethernet解析器只负责读取
 * EtherType，是否支持对应网络层协议由上层分发逻辑决定。
 *
 * frame_length必须与packet_info中的captured_length一致，确保解析器
 * 和结果对象描述的是同一段捕获数据。
 *
 * 数据不足14字节时：
 *
 * - 返回ENODATA；
 * - packet_info状态设置为TRUNCATED；
 * - error_layer设置为ETHERNET；
 * - 不发布任何部分解析结果。
 *
 * @param frame_data 指向Ethernet帧的只读原始字节。
 * @param frame_length 实际可以访问的捕获长度。
 * @param packet_info 指向已经初始化的统一结果对象。
 *
 * @return 成功时返回0；
 *         参数或对象状态无效时返回EINVAL；
 *         数据不足完整Ethernet头部时返回ENODATA。
 */
int ethernet_parse(const uint8_t *frame_data,
                   size_t frame_length,
                   packet_info_t *packet_info);

/**
 * @brief 根据Ethernet解析结果创建网络层负载游标。
 *
 * 函数不会复制负载数据。payload只借用frame_data中的网络层区域，
 * 因此原始数据包必须在payload使用期间保持有效。
 *
 * 任何范围检查都基于frame_length和captured_length，不能使用
 * wire_length访问内存。
 *
 * 参数或保存的负载范围无效时，不修改payload。
 *
 * @param frame_data 指向完整捕获帧的只读字节。
 * @param frame_length 实际可访问的捕获长度。
 * @param packet_info 指向已经成功保存Ethernet结果的对象。
 * @param payload 指向用于接收网络层子游标的结构体。
 *
 * @return 成功时返回0，参数或结果状态无效时返回EINVAL。
 */
int ethernet_payload_view(const uint8_t *frame_data,
                          size_t frame_length,
                          const packet_info_t *packet_info,
                          byte_cursor_t *payload);

/**
 * @brief 把6字节MAC地址格式化为可读字符串。
 *
 * 输出格式为小写十六进制：
 *
 * 00:11:22:33:44:55
 *
 * buffer至少需要ETHERNET_MAC_STRING_SIZE字节。
 * 函数失败时不修改buffer。
 *
 * @param address 指向包含6字节MAC地址的数组。
 * @param buffer 指向接收格式化字符串的字符数组。
 * @param buffer_size buffer能够容纳的总字节数。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         缓冲区空间不足时返回ENOSPC；
 *         字符串格式化失败时返回EIO。
 */
int ethernet_format_mac(const uint8_t address[ETHERNET_MAC_ADDRESS_LENGTH],
                        char *buffer,
                        size_t buffer_size);

#endif
