#ifndef NETFLOW_ANALYZER_PACKET_INFO_H
#define NETFLOW_ANALYZER_PACKET_INFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Ethernet MAC地址固定包含6个字节。
 *
 * 统一结果对象使用自己的项目级常量，不依赖libpcap或平台头文件。
 */
#define PACKET_MAC_ADDRESS_LENGTH 6U

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
     * true表示Ethernet头部已经成功解析。
     *
     * false时不能使用下面的MAC地址、EtherType和网络层范围。
     */
    bool has_ethernet;

    /**
     * Ethernet帧中的目标MAC地址。
     *
     * 数组按照网络中出现的字节顺序保存，不需要进行大小端转换。
     */
    uint8_t destination_mac[PACKET_MAC_ADDRESS_LENGTH];

    /**
     * Ethernet帧中的源MAC地址。
     */
    uint8_t source_mac[PACKET_MAC_ADDRESS_LENGTH];

    /**
     * Ethernet II头部中的EtherType。
     *
     * 该字段已经从网络大端序转换成主机可以直接比较的uint16_t。
     */
    uint16_t ether_type;

    /**
     * 网络层负载相对于整个数据包起始位置的偏移。
     *
     * 普通Ethernet II头部解析成功后，该值为14。
     */
    size_t network_payload_offset;

    /**
     * 当前实际捕获到的网络层负载长度。
     *
     * 该值基于captured_length计算，不能根据wire_length计算。
     */
    size_t network_payload_length;

    /**
     * true表示IPv4头部已经成功解析。
     *
     * false时不能使用下面的IPv4字段。
     */
    bool has_ipv4;

    /**
     * IPv4头部的实际字节数。
     *
     * IPv4使用IHL字段记录头部包含多少个32位字，
     * 因此合法长度范围是20～60字节。
     *
     * uint8_t已经足够保存这个范围。
     */
    uint8_t ipv4_header_length;

    /**
     * IPv4头部声明的数据报总长度。
     *
     * 包含IPv4头部和IPv4负载，不包含Ethernet头部。
     */
    uint16_t ipv4_total_length;

    /**
     * IPv4标识字段。
     *
     * 分片属于同一个原始数据报时，通常具有相同的标识值。
     */
    uint16_t ipv4_identification;

    /**
     * IPv4 DF标志。
     *
     * true表示Don't Fragment，即不允许路由器对数据报分片。
     */
    bool ipv4_dont_fragment;

    /**
     * IPv4 MF标志。
     *
     * true表示More Fragments，后面仍然存在其他分片。
     */
    bool ipv4_more_fragments;

    /**
     * IPv4分片偏移。
     *
     * 该数值的单位是8字节，不是普通的字节偏移。
     */
    uint16_t ipv4_fragment_offset;

    /**
     * IPv4生存时间TTL。
     *
     * 数据包每经过一个路由器，TTL通常减1。
     */
    uint8_t ipv4_ttl;

    /**
     * IPv4上层协议号。
     *
     * 常见值：
     *
     * 1  表示ICMP；
     * 6  表示TCP；
     * 17 表示UDP。
     */
    uint8_t ipv4_protocol;

    /**
     * IPv4头部校验和。
     *
     * 当前阶段只读取并保存，不执行校验。
     */
    uint16_t ipv4_header_checksum;

    /**
     * IPv4源地址的32位数值形式。
     *
     * 例如192.168.1.10保存为0xC0A8010A。
     * 使用整数便于比较、哈希和建立五元组流表。
     */
    uint32_t source_ipv4;

    /**
     * IPv4目标地址的32位数值形式。
     */
    uint32_t destination_ipv4;

    /**
     * IPv4负载相对于整个Ethernet帧起始位置的偏移。
     */
    size_t ipv4_payload_offset;

    /**
     * 当前捕获缓冲区内实际可以访问的IPv4负载长度。
     *
     * 该长度同时受captured_length和ipv4_total_length约束，
     * 不会把Ethernet填充字节误认为IPv4负载。
     */
    size_t ipv4_payload_length;

    /**
     * true表示IPv4头部完整，但IPv4数据报负载没有完整捕获。
     *
     * 这种情况下IPv4头部解析仍然可以成功；后续解析器再判断
     * TCP、UDP或ICMP头部是否拥有足够字节。
     */
    bool ipv4_payload_truncated;

    /**
     * true表示TCP头部已经成功解析。
     *
     * false时不能使用后面的TCP字段。
     */
    bool has_tcp;

    /**
     * TCP源端口。
     *
     * 端口是16位无符号整数，合法范围为0～65535。
     * 后续将与源IP、目标IP、目标端口和协议号共同组成五元组。
     */
    uint16_t tcp_source_port;

    /**
     * TCP目标端口。
     */
    uint16_t tcp_destination_port;

    /**
     * TCP序列号。
     *
     * 序列号描述当前TCP报文携带数据在字节流中的位置，
     * 是后续实现乱序处理和TCP流重组的关键字段。
     */
    uint32_t tcp_sequence_number;

    /**
     * TCP确认号。
     *
     * ACK标志有效时，该字段通常表示接收方期望收到的下一个序列号。
     */
    uint32_t tcp_acknowledgment_number;

    /**
     * TCP头部实际长度，单位为字节。
     *
     * TCP头部长度由Data Offset字段给出，合法范围为20～60字节。
     */
    uint8_t tcp_header_length;

    /**
     * TCP控制标志的位集合。
     *
     * 使用uint16_t是因为现代TCP共有9个控制标志，
     * 8位uint8_t无法同时保存NS、CWR、ECE等全部标志。
     */
    uint16_t tcp_flags;

    /**
     * TCP接收窗口大小。
     *
     * 该字段用于TCP流量控制。窗口扩大选项将在后续TCP选项解析中处理。
     */
    uint16_t tcp_window_size;

    /**
     * TCP校验和。
     *
     * 当前阶段只读取并保存，不执行校验。
     */
    uint16_t tcp_checksum;

    /**
     * TCP紧急指针。
     *
     * 只有URG控制标志有效时，该字段才具有协议意义。
     */
    uint16_t tcp_urgent_pointer;

    /**
     * TCP负载相对于完整Ethernet帧起始位置的偏移。
     */
    size_t tcp_payload_offset;

    /**
     * 当前捕获缓冲区中实际可以访问的TCP负载长度。
     */
    size_t tcp_payload_length;

    /**
     * true表示TCP头部完整，但TCP负载没有被完整捕获。
     */
    bool tcp_payload_truncated;

    /**
     * true表示UDP头部已经成功解析。
     *
     * false时不能使用后面的UDP字段。
     */
    bool has_udp;

    /**
     * UDP源端口。
     *
     * 后续可以和源IP、目标IP、目标端口及协议号组成五元组。
     */
    uint16_t udp_source_port;

    /**
     * UDP目标端口。
     */
    uint16_t udp_destination_port;

    /**
     * UDP头部声明的数据报长度。
     *
     * 该长度包含8字节UDP头部和后面的UDP负载。
     */
    uint16_t udp_length;

    /**
     * UDP校验和。
     *
     * 在IPv4中，该字段为0表示发送方没有计算UDP校验和。
     * 当前阶段只读取并保存，不执行校验。
     */
    uint16_t udp_checksum;

    /**
     * UDP负载相对于完整Ethernet帧起始位置的偏移。
     */
    size_t udp_payload_offset;

    /**
     * 当前捕获缓冲区内实际可以访问的UDP负载长度。
     */
    size_t udp_payload_length;

    /**
     * true表示UDP头部完整，但UDP负载没有被完整捕获。
     */
    bool udp_payload_truncated;

    /**
     * true表示ICMP公共头部已经成功解析。
     *
     * false时不能使用后面的ICMP字段。
     */
    bool has_icmp;

    /**
     * ICMP消息类型。
     *
     * 例如0表示Echo Reply，8表示Echo Request。
     */
    uint8_t icmp_type;

    /**
     * ICMP消息代码。
     *
     * code的具体含义取决于icmp_type。
     */
    uint8_t icmp_code;

    /**
     * ICMP校验和。
     *
     * 当前阶段只读取并保存，不执行校验和验证。
     */
    uint16_t icmp_checksum;

    /**
     * ICMP公共头部最后4字节的原始数值。
     *
     * 不同ICMP类型对这4字节有不同定义，因此先保留完整原始值。
     */
    uint32_t icmp_rest_of_header;

    /**
     * true表示当前ICMP消息是Echo Request或Echo Reply，
     * identifier和sequence字段有效。
     */
    bool icmp_has_echo_fields;

    /**
     * ICMP Echo标识符。
     *
     * ping程序通常使用该字段区分不同的探测会话。
     */
    uint16_t icmp_identifier;

    /**
     * ICMP Echo序列号。
     *
     * ping程序通常从1开始递增该字段。
     */
    uint16_t icmp_sequence;

    /**
     * ICMP公共头部之后的数据相对于完整帧的偏移。
     */
    size_t icmp_payload_offset;

    /**
     * 当前捕获缓冲区中实际可以访问的ICMP负载长度。
     */
    size_t icmp_payload_length;

    /**
     * true表示ICMP公共头部完整，但ICMP负载没有完整捕获。
     */
    bool icmp_payload_truncated;

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