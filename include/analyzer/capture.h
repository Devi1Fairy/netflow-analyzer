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
 * 实时抓包默认使用1000毫秒的数据包缓冲超时。
 *
 * 这是libpcap的数据包缓冲策略，不是可靠的应用层周期定时器。
 * 某些平台或采集后端在第一个包到达前不会按该时间返回。
 *
 * 应用需要可靠周期唤醒时，应启用非阻塞模式并使用
 * capture_wait_readable()的显式等待超时。
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
 * @brief 表示一次实时采集描述符等待的正常结果。
 */
typedef enum {
    /**
     * 尚未产生有效等待结果。
     */
    CAPTURE_WAIT_STATUS_UNKNOWN = 0,

    /**
     * 采集描述符已经就绪，应再次尝试读取数据包。
     *
     * 由于libpcap缓冲和平台语义不同，就绪不保证下一次读取
     * 一定取得数据包；非阻塞读取仍可能返回EAGAIN。
     */
    CAPTURE_WAIT_STATUS_READY,

    /**
     * 指定等待时间已经到达，但没有发现描述符就绪。
     *
     * 这不是采集错误，上层可以借此执行周期任务。
     */
    CAPTURE_WAIT_STATUS_TIMEOUT
} capture_wait_status_t;

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
 * @brief 保存实时采集句柄提供的累计抓包统计。
 *
 * 这些数值来自libpcap的pcap_stats，并表示从实时采集开始到查询时的
 * 累计结果，不是本次查询之后新增的数量。
 *
 * 不同操作系统和抓包后端对各字段的统计范围可能不同。例如，
 * received_packets可能统计BPF过滤前或过滤后的数据包。
 *
 * 某个丢包字段为0也不一定能证明没有发生丢包，因为部分平台可能
 * 不提供对应统计。
 *
 * 本结构体不拥有动态内存，不需要cleanup或free。
 */
typedef struct {
    /**
     * libpcap报告的接收数据包数量。
     *
     * 该数值不等同于应用层已经成功处理的数据包数量。
     */
    uint64_t received_packets;

    /**
     * libpcap报告的操作系统抓包缓冲区丢包数量。
     *
     * 当应用读取速度跟不上数据包到达速度时，该数值可能增加。
     */
    uint64_t dropped_packets;

    /**
     * libpcap报告的网络接口或驱动丢包数量。
     *
     * 部分平台不支持该统计；此时0可能表示不支持，而不是确定没有
     * 发生丢包。
     */
    uint64_t interface_dropped_packets;
} capture_statistics_t;

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
 * read_timeout_ms控制libpcap的数据包缓冲策略。
 * 它不是可靠的应用周期定时器；不同平台在第一个包到达前的超时行为可能不同。
 * 当libpcap实际报告缓冲超时时，capture_next_packet返回EAGAIN，让上层获得一次控制机会。
 * 可靠的应用层周期唤醒应使用非阻塞模式和capture_wait_readable()。
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
 * @brief 查询实时采集对象的累计抓包统计。
 *
 * capture必须由capture_open_live成功创建。离线PCAP文件不保存
 * 抓包运行统计，因此对离线采集对象调用本函数会返回ENOTSUP。
 *
 * 函数成功后才修改statistics。参数错误、不支持或libpcap查询失败
 * 时，statistics原有内容保持不变。
 *
 * 本函数只读取统计，不取得capture或statistics的所有权。
 *
 * @param capture 指向已经成功打开的采集对象。
 * @param statistics 指向用于接收统计结果的结构体。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         对离线采集对象调用时返回ENOTSUP；
 *         libpcap查询失败时返回EIO。
 */
int capture_get_statistics(
    const capture_t *capture,
    capture_statistics_t *statistics
);

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
 * @brief 把实时采集句柄切换到非阻塞模式。
 *
 * 非阻塞模式下，当前没有可读数据包时，
 * capture_next_packet()立即返回EAGAIN。
 *
 * 本函数同时确认当前UNIX平台能够为该采集句柄提供可供poll等待的
 * 文件描述符。文件描述符只保存在capture模块内部，不向上层暴露。
 *
 * 重复对已经启用非阻塞模式的句柄调用是安全的。
 *
 * error_buffer和error_buffer_size必须同时提供或同时省略。
 * 函数失败时不关闭capture。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         离线句柄或没有可等待描述符时返回ENOTSUP；
 *         libpcap无法启用非阻塞模式时返回EIO。
 */
int capture_enable_nonblocking(
    capture_t *capture,
    char *error_buffer,
    size_t error_buffer_size);

/**
 * @brief 等待实时采集描述符可读或等待时间到达。
 *
 * capture必须已经通过capture_enable_nonblocking()配置。
 *
 * timeout_ms是应用层明确等待时间，不依赖libpcap的数据包缓冲
 * 超时语义。等待期间不会忙轮询。
 *
 * 成功时通过status区分描述符就绪和正常超时。
 * 函数失败时不修改status。
 *
 * @return 成功就绪或正常超时时返回0；
 *         参数或句柄状态无效时返回EINVAL；
 *         离线句柄返回ENOTSUP；
 *         poll被信号中断时返回EINTR；
 *         描述符或等待操作失败时返回对应errno或EIO。
 */
int capture_wait_readable(
    capture_t *capture,
    int timeout_ms,
    capture_wait_status_t *status);

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
 * @brief 请求中断当前采集读取。
 *
 * 本函数不关闭或释放capture，只通知libpcap结束正在进行的读取。
 * 后续capture_next_packet可能返回正常结束状态。
 *
 * libpcap保证pcap_breakloop可以在UNIX信号处理函数中使用。
 * 调用期间capture对象必须保持有效。
 *
 * capture为NULL时不执行任何操作，因此清理路径可以安全调用。
 *
 * @param capture 指向已经打开的离线或实时采集对象，可以为NULL。
 */
void capture_break_loop(capture_t *capture);

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