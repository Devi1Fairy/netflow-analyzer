#ifndef NETFLOW_ANALYZER_PACKET_INFO_H
#define NETFLOW_ANALYZER_PACKET_INFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 表示一条数据包当前的解析状态。
 *
 * 捕获是否被截断与协议解析是否失败是两个相关但不同的概念：
 *
 * - caplen小于wirelen表示抓包时没有保存完整数据；
 * - 只有解析所需字段确实缺失时，解析状态才设置为TRUNCATED。
 */
typedef enum {
    /**
     * 已经创建结果对象，但尚未开始协议解析。
     */
    PACKET_PARSE_STATUS_NOT_STARTED = 0,

    /**
     * 当前支持的协议层已经成功解析。
     */
    PACKET_PARSE_STATUS_COMPLETE,

    /**
     * 捕获数据不足，无法读取完整的必要协议字段。
     */
    PACKET_PARSE_STATUS_TRUNCATED,

    /**
     * 数据长度足够，但协议字段组合不符合协议规范。
     */
    PACKET_PARSE_STATUS_MALFORMED,

    /**
     * 数据包使用了项目当前尚未支持的协议或格式。
     */
    PACKET_PARSE_STATUS_UNSUPPORTED
} packet_parse_status_t;

/**
 * @brief 表示数据包解析失败时所在的协议层。
 *
 * 该枚举保存项目自己的协议层概念，不依赖libpcap的数据类型。
 */
typedef enum {
    /**
     * 没有发生协议层错误。
     */
    PACKET_PARSE_LAYER_NONE = 0,

    /**
     * PCAP捕获元数据或捕获结果存在问题。
     */
    PACKET_PARSE_LAYER_CAPTURE,

    /**
     * Ethernet链路层解析失败。
     */
    PACKET_PARSE_LAYER_ETHERNET,

    /**
     * IPv4网络层解析失败。
     */
    PACKET_PARSE_LAYER_IPV4,

    /**
     * TCP传输层解析失败。
     */
    PACKET_PARSE_LAYER_TCP,

    /**
     * UDP传输层解析失败。
     */
    PACKET_PARSE_LAYER_UDP,

    /**
     * ICMP协议解析失败。
     */
    PACKET_PARSE_LAYER_ICMP,

    /**
     * 应用层协议解析失败。
     */
    PACKET_PARSE_LAYER_APPLICATION
} packet_parse_layer_t;

/**
 * @brief 保存一条数据包的结构化分析结果。
 *
 * 当前阶段只建立捕获元数据和统一错误状态。后续实现各层解析器时，
 * 会继续加入Ethernet、IPv4、TCP、UDP和ICMP字段。
 *
 * 该结构体不保存packet.data指针，也不拥有原始数据包内存。
 * 因此它不需要free，并且不会受到libpcap下一次读取导致的
 * packet.data失效影响。
 */
typedef struct {
    /**
     * 数据包捕获时间戳的整数秒部分。
     *
     * 使用int64_t保证时间戳拥有明确的64位有符号范围。
     */
    int64_t timestamp_seconds;

    /**
     * 当前秒内的微秒部分，合法范围为0～999999。
     */
    int32_t timestamp_microseconds;

    /**
     * 实际保存在捕获缓冲区中的字节数，也就是caplen。
     *
     * 任何协议字段访问都只能以该长度作为边界。
     */
    uint32_t captured_length;

    /**
     * 数据包在线路上的原始长度，也就是wirelen。
     *
     * 该长度可以用于流量统计，但不能作为内存访问边界。
     */
    uint32_t wire_length;

    /**
     * 当前数据包的统一解析状态。
     */
    packet_parse_status_t parse_status;

    /**
     * 解析失败所在的协议层。
     *
     * 没有错误时为PACKET_PARSE_LAYER_NONE。
     */
    packet_parse_layer_t error_layer;

    /**
     * 使用errno风格保存的正整数错误码。
     *
     * 例如：
     *
     * ENODATA表示捕获数据不足；
     * EBADMSG表示协议字段非法；
     * ENOTSUP表示协议暂不支持。
     *
     * 没有错误时为0。
     */
    int error_code;

    /**
     * 发生错误时相对于数据包起始地址的字节偏移。
     *
     * size_t专门用于表示对象大小、数组下标和内存偏移。
     */
    size_t error_offset;

    /**
     * true表示该对象已经由packet_info_init成功初始化。
     */
    bool initialized;
} packet_info_t;

/**
 * @brief 使用一条捕获记录的基础元数据初始化结果对象。
 *
 * 该函数只复制数值，不保存原始数据包指针。
 *
 * 参数检查失败时，不修改info原有内容。
 *
 * @param info 指向待初始化的数据包结果对象。
 * @param timestamp_seconds 捕获时间戳的整数秒部分。
 * @param timestamp_microseconds 当前秒内的微秒部分。
 * @param captured_length 实际捕获并可访问的字节数。
 * @param wire_length 数据包在线路上的原始长度。
 *
 * @return 成功时返回0，参数无效时返回EINVAL。
 */
int packet_info_init(packet_info_t *info,
                     int64_t timestamp_seconds,
                     int32_t timestamp_microseconds,
                     uint32_t captured_length,
                     uint32_t wire_length);

/**
 * @brief 在数据包结果中记录一次协议解析错误。
 *
 * status必须是TRUNCATED、MALFORMED或UNSUPPORTED之一。
 * error_layer不能是NONE，error_code必须是正整数。
 *
 * error_offset不能超过captured_length，因为它表示实际捕获缓冲区中的
 * 位置。恰好等于captured_length表示错误发生在缓冲区末尾。
 *
 * 参数检查失败时，不修改info。
 *
 * @param info 指向已经初始化的数据包结果对象。
 * @param status 准备记录的失败状态。
 * @param error_layer 发生错误的协议层。
 * @param error_code errno风格的正整数错误码。
 * @param error_offset 发生错误时的数据包字节偏移。
 *
 * @return 成功时返回0，参数或状态无效时返回EINVAL。
 */
int packet_info_set_error(packet_info_t *info,
                          packet_parse_status_t status,
                          packet_parse_layer_t error_layer,
                          int error_code,
                          size_t error_offset);

/**
 * @brief 将数据包结果标记为解析完成。
 *
 * 标记成功时会同时清空之前保存的错误层、错误码和错误偏移。
 *
 * @param info 指向已经初始化的数据包结果对象。
 *
 * @return 成功时返回0，对象无效时返回EINVAL。
 */
int packet_info_mark_complete(packet_info_t *info);

/**
 * @brief 判断抓包时是否只保存了数据包的一部分。
 *
 * captured_length小于wire_length时返回true。
 *
 * 这只表示原始数据包被截断，并不直接表示协议解析一定失败。例如只
 * 解析Ethernet头部时，即使应用负载没有完整捕获，也可能解析成功。
 *
 * @param info 指向已经初始化的数据包结果对象。
 *
 * @return 捕获被截断时返回true，否则返回false。
 */
bool packet_info_capture_is_truncated(const packet_info_t *info);

#endif