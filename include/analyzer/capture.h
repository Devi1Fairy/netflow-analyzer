#ifndef NETFLOW_ANALYZER_CAPTURE_H
#define NETFLOW_ANALYZER_CAPTURE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * 用于接收采集模块错误说明的建议缓冲区大小。
 *
 * 256字节与libpcap常用错误缓冲区大小一致，足以保存当前阶段的
 * 文件打开和读取错误。
 */
#define CAPTURE_ERROR_BUFFER_SIZE 256U

/*
 * 实时抓包默认最多保留每个数据包的65535字节。
 *
 * 这个大小足以完整捕获当前项目常见的标准Ethernet、IPv4、
 * TCP、UDP和ICMP数据包。
 */
#define CAPTURE_DEFAULT_SNAPSHOT_LENGTH 65535

/*
 * 实时读取默认最多等待1000毫秒。
 *
 * 超时不是抓包失败，而是让调用者定期获得控制权，
 * 以后可以在这里检查停止信号和运行状态。
 */
#define CAPTURE_DEFAULT_READ_TIMEOUT_MS 1000

/**
 * @brief 表示采集文件使用的链路层类型。
 *
 * 采集模块把libpcap的原生DLT值转换为项目内部枚举，避免上层模块
 * 直接依赖libpcap头文件。
 */
typedef enum {
    /**
     * 尚未取得有效的链路层类型。
     */
    CAPTURE_LINK_TYPE_UNKNOWN = 0,

    /**
     * 普通Ethernet II链路层。
     */
    CAPTURE_LINK_TYPE_ETHERNET = 1,

    /**
     * libpcap能够打开，但当前项目尚未支持的其他链路层类型。
     */
    CAPTURE_LINK_TYPE_UNSUPPORTED = 2
} capture_link_type_t;

/**
 * @brief 表示一次离线数据包读取的正常状态。
 *
 * 函数返回值用于表示操作是否出错；这个枚举用于区分成功读取到
 * 数据包和正常到达文件末尾。
 */
typedef enum {
    /**
     * 尚未产生有效读取状态。
     */
    CAPTURE_READ_STATUS_UNKNOWN = 0,

    /**
     * 成功取得一条数据包。
     */
    CAPTURE_READ_STATUS_PACKET = 1,

    /**
     * PCAP文件已经读取完毕。
     */
    CAPTURE_READ_STATUS_END_OF_FILE = 2
} capture_read_status_t;

/**
 * @brief 离线或实时采集句柄的不透明类型。
 *
 * 头文件只声明类型，不公开结构体成员。调用者不能直接访问内部的
 * pcap_t，也不能自己分配capture_t。
 *
 * 对象由capture_open_offline或capture_open_live创建，
 * 统一由capture_close释放。
 */
typedef struct capture capture_t;

/**
 * @brief 表示libpcap返回的一条只读数据包视图。
 *
 * 该结构体不拥有data指向的内存，调用者不能修改或释放data。
 *
 * data及其内容只保证在下一次调用capture_next_packet之前有效。
 * 如果需要跨越下一次读取继续保存数据，调用者必须复制
 * captured_length字节。
 */
typedef struct {
    /**
     * 捕获时间戳中的整数秒。
     *
     * int64_t提供明确的64位有符号范围，便于后续保存较长时间范围的
     * Unix时间戳。
     */
    int64_t timestamp_seconds;

    /**
     * 当前秒内的微秒部分，合法范围通常为0～999999。
     *
     * 当前使用pcap_open_offline，因此libpcap以微秒精度提供时间戳。
     */
    int32_t timestamp_microseconds;

    /**
     * 实际捕获并保存在data中的字节数。
     *
     * 后续初始化byte_cursor时必须使用这个长度，不能使用wire_length。
     */
    uint32_t captured_length;

    /**
     * 数据包在线路上的原始长度。
     *
     * 该字段用于流量统计，但不能作为data的可访问范围。
     */
    uint32_t wire_length;

    /**
     * 指向libpcap内部管理的只读数据。
     *
     * 调用者不能free，也不能通过这个指针修改数据。
     */
    const uint8_t *data;
} capture_packet_view_t;

/**
 * @brief 打开一个离线PCAP文件。
 *
 * 函数成功时创建capture_t对象，并通过capture输出对象地址。
 *
 * error_buffer和error_buffer_size必须同时提供或同时省略：
 *
 * - error_buffer非NULL且error_buffer_size大于0：接收错误说明；
 * - error_buffer为NULL且error_buffer_size为0：不接收错误说明。
 *
 * 成功时，如果提供了error_buffer，其内容会被设置为空字符串。
 *
 * 函数失败时不修改capture原有内容。
 *
 * @param file_path 待打开PCAP文件的路径。
 * @param capture 指向用于接收采集对象地址的指针变量。
 * @param error_buffer 可选的错误信息缓冲区。
 * @param error_buffer_size error_buffer能够容纳的字节数。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         内存分配失败时返回ENOMEM；
 *         libpcap无法打开或识别文件时返回EIO。
 */
int capture_open_offline(const char *file_path,
                         capture_t **capture,
                         char *error_buffer,
                         size_t error_buffer_size);

/**
 * @brief 打开指定网络接口进行实时抓包。
 *
 * snapshot_length表示每个数据包最多保留多少字节。
 * 数据包在线路上的真实长度仍通过wire_length提供。
 *
 * promiscuous为true时请求混杂模式，使网卡接收更多经过该接口、
 * 但目标MAC地址不一定属于本机的数据包。是否真正生效还取决于
 * 网卡类型、驱动、操作系统权限和网络环境。
 *
 * read_timeout_ms表示实时读取等待时间。等待期间没有收到数据包时，
 * capture_next_packet返回EAGAIN，让上层有机会检查停止请求。
 *
 * 打开实时网卡通常需要root权限，或者为程序配置CAP_NET_RAW等
 * Linux capability。权限不足时返回EIO，并通过error_buffer提供
 * libpcap的详细说明。
 *
 * error_buffer和error_buffer_size必须同时提供或同时省略。
 *
 * 函数失败时不修改capture原有内容。
 *
 * @param interface_name 准备监听的网络接口名称，例如lo或eth0。
 * @param snapshot_length 每个数据包最多捕获的字节数，必须大于0。
 * @param promiscuous true表示请求混杂模式。
 * @param read_timeout_ms 实时读取超时毫秒数，必须大于0。
 * @param capture 指向用于接收采集对象地址的指针变量。
 * @param error_buffer 可选的错误信息缓冲区。
 * @param error_buffer_size error_buffer能够容纳的字节数。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         内存分配失败时返回ENOMEM；
 *         网卡不存在、权限不足或libpcap打开失败时返回EIO。
 */
int capture_open_live(const char *interface_name,
                        int snapshot_length,
                        bool promiscuous,
                        int read_timeout_ms,
                        capture_t **capture,
                        char *error_buffer,
                        size_t error_buffer_size);

/**
 * @brief 查询采集文件的链路层类型。
 *
 * @param capture 指向已经成功打开的采集对象。
 * @param link_type 指向用于接收链路层类型的变量。
 *
 * @return 成功时返回0，参数无效时返回EINVAL。
 */
int capture_get_link_type(const capture_t *capture,
                          capture_link_type_t *link_type);

/**
 * @brief 编译并安装一个libpcap BPF过滤表达式。
 *
 * capture必须是已经成功打开的离线或实时采集对象。
 *
 * filter_expression只在本函数调用期间被借用，调用者仍然拥有该
 * 字符串。libpcap会把表达式编译为临时BPF程序并安装到采集句柄。
 *
 * error_buffer和error_buffer_size必须同时提供或同时省略：
 *
 * - error_buffer非NULL且error_buffer_size大于0：接收错误说明；
 * - error_buffer为NULL且error_buffer_size为0：不接收错误说明。
 *
 * 成功时，如果提供了error_buffer，其内容会被设置为空字符串。
 *
 * BPF编译或安装失败时不会关闭capture。调用者仍然拥有该采集对象，
 * 并负责最终调用capture_close。
 *
 * @param capture 指向已经成功打开的采集对象。
 * @param filter_expression 非空BPF过滤表达式，例如"icmp"或"tcp port 80"。
 * @param error_buffer 可选的错误信息缓冲区。
 * @param error_buffer_size error_buffer能够容纳的字节数。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         BPF编译或安装失败时返回EIO。
 */
int capture_set_filter(capture_t *capture,
                       const char *filter_expression,
                       char *error_buffer,
                       size_t error_buffer_size);

/**
 * @brief 返回libpcap为当前采集对象保存的最近错误信息。
 *
 * 返回的字符串由libpcap管理。调用者不能修改或释放该字符串，
 * capture_close之后也不能继续使用这个地址。
 *
 * 没有错误时可能返回空字符串。
 *
 * @param capture 指向已经成功打开的采集对象。
 *
 * @return 错误信息字符串；capture为NULL时返回固定的参数错误说明。
 */
const char *capture_get_error(const capture_t *capture);

/**
 * @brief 关闭采集句柄并释放capture_t对象。
 *
 * 成功关闭后会把调用者的指针设置为NULL，降低释放后继续使用和
 * 重复关闭的风险。
 *
 * capture为NULL或者*capture为NULL时不执行任何操作。
 *
 * @param capture 指向保存采集对象地址的指针变量。
 */
void capture_close(capture_t **capture);

/**
 * @brief  从离线文件或实时接口读取下一条数据包。
 *
 * 成功取得数据包时：
 *
 * - 返回0；
 * - status设置为CAPTURE_READ_STATUS_PACKET；
 * - packet填入时间戳、捕获长度、线路长度和数据地址。
 *
 * 正常到达文件末尾时：
 *
 * - 返回0；
 * - status设置为CAPTURE_READ_STATUS_END_OF_FILE；
 * - packet保持原值不变。
 *
 * 函数失败时不修改packet和status。
 *
 * packet->data由libpcap拥有，只保证在下一次调用本函数之前有效。
 * 
 * 实时抓包等待超时时：
 *
 * - 返回EAGAIN；
 * - packet和status保持原值不变；
 * - 这不表示网卡或采集句柄发生故障。
 *
 * @param capture 指向已经打开的离线或实时采集对象。
 * @param packet 指向用于接收数据包视图的结构体。
 * @param status 指向用于接收读取状态的变量。
 *
 * @return 成功读取或正常到达文件末尾时返回0；
 *         参数无效时返回EINVAL；
 *         libpcap暂时没有返回数据时返回EAGAIN；
 *         libpcap读取失败时返回EIO。
 */
int capture_next_packet(capture_t *capture,
                        capture_packet_view_t *packet,
                        capture_read_status_t *status);

#endif