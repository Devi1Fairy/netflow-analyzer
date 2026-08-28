/*
 * libpcap在Linux上使用u_int、u_short和u_char等BSD兼容类型。
 *
 * 项目使用严格C11模式，glibc默认可能隐藏这些扩展类型。
 * _DEFAULT_SOURCE要求glibc公开默认的Linux/BSD兼容接口。
 *
 * 特性测试宏必须出现在所有系统头文件之前。
 */
#define _DEFAULT_SOURCE

#include "analyzer/capture.h"

#include <errno.h>
#include <pcap/pcap.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 采集模块的内部对象。
 *
 * 这个结构体只在capture.c中定义，因此其他模块无法直接操作
 * native_handle，必须通过公开接口访问。
 */
struct capture {
    /**
     * libpcap原生采集句柄。
     *
     * 由pcap_open_offline或pcap_open_live创建，由pcap_close释放。
     */
    pcap_t *native_handle;

    /**
     * 转换后的项目内部链路层类型。
     */
    capture_link_type_t link_type;

    /**
     * true表示对象来自实时网卡，false表示来自离线PCAP文件。
     *
     * pcap_stats只支持实时采集，因此统计接口通过该字段在进入
     * libpcap之前拒绝离线句柄。
     */
    bool is_live;
};

/**
 * @brief 把错误信息安全写入调用者提供的缓冲区。
 *
 * snprintf会根据buffer_size限制写入长度，并在buffer_size大于0时
 * 保证字符串以空字符结尾。
 */
static void capture_copy_error(char *buffer,
                               size_t buffer_size,
                               const char *message)
{
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    if (message == NULL) {
        message = "";
    }

    (void)snprintf(buffer, buffer_size, "%s", message);
}

/**
 * @brief 把libpcap的原生链路层类型转换为项目内部类型。
 */
static capture_link_type_t capture_map_link_type(int native_link_type)
{
    if (native_link_type == DLT_EN10MB) {
        /*
         * DLT_EN10MB是libpcap对普通Ethernet的名称。
         *
         * 名称中的10MB来自历史上的10 Mb/s Ethernet，但它同样用于
         * 当前常见的百兆、千兆和更高速Ethernet抓包文件。
         */
        return CAPTURE_LINK_TYPE_ETHERNET;
    }

    return CAPTURE_LINK_TYPE_UNSUPPORTED;
}

int capture_open_offline(const char *file_path,
                         capture_t **capture,
                         char *error_buffer,
                         size_t error_buffer_size)
{
    char native_error[PCAP_ERRBUF_SIZE] = {0};
    capture_t *new_capture;
    int native_link_type;

    /*
     * 错误缓冲区及其长度必须成对出现。
     */
    if ((error_buffer == NULL && error_buffer_size != 0U) ||
        (error_buffer != NULL && error_buffer_size == 0U)) {
        return EINVAL;
    }

    /*
     * 如果调用者提供了错误缓冲区，先把它初始化为空字符串。
     */
    capture_copy_error(error_buffer, error_buffer_size, "");

    if (file_path == NULL || file_path[0] == '\0' || capture == NULL) {
        capture_copy_error(error_buffer,
                           error_buffer_size,
                           "invalid capture open arguments");

        return EINVAL;
    }

    /*
     * capture_t是不透明动态对象，因此由模块负责分配和释放。
     *
     * calloc会把成员初始化为0，避免未初始化指针。
     */
    new_capture = calloc(1U, sizeof(*new_capture));

    if (new_capture == NULL) {
        capture_copy_error(error_buffer,
                           error_buffer_size,
                           "failed to allocate capture object");

        return ENOMEM;
    }

    /*
     * pcap_open_offline打开已有的PCAP文件。
     *
     * 失败时返回NULL，并把可读错误说明写入native_error。
     */
    new_capture->native_handle = pcap_open_offline(file_path, native_error);

    if (new_capture->native_handle == NULL) {
        capture_copy_error(error_buffer,
                           error_buffer_size,
                           native_error);

        free(new_capture);
        return EIO;
    }

    /*
     * PCAP文件会记录其链路层类型，不能假定所有文件都是Ethernet。
     */
    native_link_type = pcap_datalink(new_capture->native_handle);

    if (native_link_type < 0) {
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            pcap_geterr(new_capture->native_handle)
        );

        pcap_close(new_capture->native_handle);
        free(new_capture);

        return EIO;
    }

    new_capture->link_type =
        capture_map_link_type(native_link_type);

    new_capture->is_live = false;

    /*
     * 所有初始化步骤成功后，才把对象地址交给调用者。
     *
     * 这能保证函数失败时不会发布半初始化对象。
     */
    *capture = new_capture;

    return 0;
}

int capture_open_live(const char *interface_name,
                        int snapshot_length,
                        bool promiscuous,
                        int read_timeout_ms,
                        capture_t **capture,
                        char *error_buffer,
                        size_t error_buffer_size)
{
    char native_error[PCAP_ERRBUF_SIZE] = {0};

    capture_t *new_capture;
    int native_link_type;

    /*
     * 错误缓冲区及其容量必须成对出现。
     */
    if ((error_buffer == NULL &&
         error_buffer_size != 0U) ||
        (error_buffer != NULL &&
         error_buffer_size == 0U)) {
        return EINVAL;
    }

    /*
     * 对于合法的可选缓冲区组合，先清空以前的错误内容。
     */
    capture_copy_error(
        error_buffer,
        error_buffer_size,
        ""
    );

    if (interface_name == NULL ||
        interface_name[0] == '\0' ||
        snapshot_length <= 0 ||
        read_timeout_ms <= 0 ||
        capture == NULL) {
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            "invalid live capture arguments"
        );

        return EINVAL;
    }

    /*
     * capture_t是不透明动态对象，由capture模块拥有。
     */
    new_capture = calloc(1U, sizeof(*new_capture));

    if (new_capture == NULL) {
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            "failed to allocate capture object"
        );

        return ENOMEM;
    }

    /*
     * pcap_open_live参数依次表示：
     *
     * interface_name：网卡名称；
     * snapshot_length：每个数据包最多保存的字节数；
     * promiscuous：是否请求混杂模式，libpcap使用0或1；
     * read_timeout_ms：读取等待时间；
     * native_error：接收libpcap错误说明。
     */
    new_capture->native_handle = pcap_open_live(
        interface_name,
        snapshot_length,
        promiscuous ? 1 : 0,
        read_timeout_ms,
        native_error
    );

    if (new_capture->native_handle == NULL) {
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            native_error
        );

        free(new_capture);
        return EIO;
    }

    /*
     * 实时接口同样可能不是普通Ethernet链路。
     *
     * 例如Linux的"any"接口通常使用Linux cooked capture类型，
     * 当前项目尚未解析这种链路头。
     */
    native_link_type = pcap_datalink(new_capture->native_handle);

    if (native_link_type < 0) {
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            pcap_geterr(new_capture->native_handle)
        );

        pcap_close(new_capture->native_handle);
        free(new_capture);

        return EIO;
    }

    new_capture->link_type = capture_map_link_type(native_link_type);

    new_capture->is_live = true;

    /*
     * 所有操作成功后才发布新对象。
     *
     * 因此失败时调用者原来的capture值不会被覆盖。
     */
    *capture = new_capture;

    return 0;
}

int capture_get_link_type(const capture_t *capture,
                          capture_link_type_t *link_type)
{
    if (capture == NULL || capture->native_handle == NULL || link_type == NULL) {
        return EINVAL;
    }

    *link_type = capture->link_type;

    return 0;
}

int capture_get_statistics(
    const capture_t *capture,
    capture_statistics_t *statistics)
{
    struct pcap_stat native_statistics = {0};

    capture_statistics_t new_statistics;
    int native_result;

    if (capture == NULL ||
        capture->native_handle == NULL ||
        statistics == NULL) {
        return EINVAL;
    }

    /*
     * 离线PCAP不保存当时运行系统的接收和丢包计数。
     *
     * 在这里明确返回ENOTSUP，比把libpcap的“不支持”统一解释为
     * 普通I/O错误更能表达真实原因。
     */
    if (!capture->is_live) {
        return ENOTSUP;
    }

    /*
     * pcap_stats返回的是从实时采集开始到当前时刻的累计统计快照。
     *
     * 查询失败时，详细错误仍保存在capture句柄中，调用者可以在
     * capture_close之前通过capture_get_error读取。
     */
    native_result = pcap_stats(
        capture->native_handle,
        &native_statistics
    );

    if (native_result != 0) {
        return EIO;
    }

    /*
     * 先在局部对象中完成从libpcap类型到项目类型的转换。
     *
     * 只有全部成功后才发布输出，保证失败时调用者原有的statistics
     * 内容保持不变。
     */
    new_statistics = (capture_statistics_t){
        .received_packets =
            (uint64_t)native_statistics.ps_recv,

        .dropped_packets =
            (uint64_t)native_statistics.ps_drop,

        .interface_dropped_packets =
            (uint64_t)native_statistics.ps_ifdrop
    };

    *statistics = new_statistics;

    return 0;
}

int capture_set_filter(capture_t *capture,
                       const char *filter_expression,
                       char *error_buffer,
                       size_t error_buffer_size)
{
    struct bpf_program filter_program = {0};
    int native_result;

    /*
     * 错误缓冲区及其容量必须成对出现。
     *
     * 先验证这一组合，才能安全决定后续是否可以向缓冲区写入。
     */
    if ((error_buffer == NULL && error_buffer_size != 0U) ||
        (error_buffer != NULL && error_buffer_size == 0U)) {
        return EINVAL;
    }

    /*
     * 对于合法的可选缓冲区组合，先清除调用者以前保存的错误。
     *
     * 这样成功返回时，error_buffer为空字符串。
     */
    capture_copy_error(error_buffer, error_buffer_size, "");

    if (capture == NULL ||
        capture->native_handle == NULL ||
        filter_expression == NULL ||
        filter_expression[0] == '\0') {
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            "invalid capture filter arguments"
        );

        return EINVAL;
    }

    /*
     * pcap_compile把文本表达式编译为临时BPF程序。
     *
     * 第四个参数1表示启用优化。
     *
     * 初版使用PCAP_NETMASK_UNKNOWN，不依赖具体网卡的IPv4掩码。
     * 普通的icmp、tcp、udp和端口过滤不受影响，但依赖IPv4广播地址
     * 判断的表达式可能无法使用。
     */
    native_result = pcap_compile(
        capture->native_handle,
        &filter_program,
        filter_expression,
        1,
        PCAP_NETMASK_UNKNOWN
    );

    if (native_result != 0) {
        /*
         * pcap_compile失败时没有可供本模块释放的成功编译结果。
         *
         * pcap_geterr返回的字符串属于capture句柄，因此将它复制到
         * 调用者缓冲区，而不把该地址作为长期结果保存。
         */
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            pcap_geterr(capture->native_handle)
        );

        return EIO;
    }

    /*
     * 编译成功后，把BPF程序安装到当前离线或实时采集句柄。
     */
    native_result = pcap_setfilter(
        capture->native_handle,
        &filter_program
    );

    if (native_result != 0) {
        capture_copy_error(
            error_buffer,
            error_buffer_size,
            pcap_geterr(capture->native_handle)
        );

        /*
         * 只要pcap_compile成功，无论安装是否成功，都必须释放临时
         * 编译结果。
         */
        pcap_freecode(&filter_program);

        return EIO;
    }

    /*
     * 过滤器已经安装到capture句柄。临时编译结果不再由本函数保留。
     */
    pcap_freecode(&filter_program);

    return 0;
}

const char *capture_get_error(const capture_t *capture)
{
    if (capture == NULL || capture->native_handle == NULL) {
        return "invalid capture handle";
    }

    return pcap_geterr(capture->native_handle);
}

void capture_break_loop(capture_t *capture)
{
    if (capture == NULL ||
        capture->native_handle == NULL) {
        return;
    }

    /*
     * 不在这里关闭句柄。
     *
     * pcap_breakloop只设置中断状态，并在Linux实时网卡上尝试
     * 唤醒阻塞读取。实际资源仍由正常控制流通过capture_close释放。
     */
    pcap_breakloop(capture->native_handle);
}

void capture_close(capture_t **capture)
{
    capture_t *object;

    if (capture == NULL || *capture == NULL) {
        return;
    }

    object = *capture;

    if (object->native_handle != NULL) {
        pcap_close(object->native_handle);
        object->native_handle = NULL;
    }

    free(object);

    /*
     * free只释放对象，不会自动修改调用者保存的地址。
     *
     * 手动设置为NULL可以避免调用者继续持有悬空指针。
     */
    *capture = NULL;
}

int capture_next_packet(capture_t *capture,
                        capture_packet_view_t *packet,
                        capture_read_status_t *status)
{
    struct pcap_pkthdr *native_header = NULL;
    const u_char *native_data = NULL;

    capture_packet_view_t new_packet;
    int native_result;

    if (capture == NULL ||
        capture->native_handle == NULL ||
        packet == NULL ||
        status == NULL) {
        return EINVAL;
    }

    /*
     * pcap_next_ex通过两个输出参数返回：
     *
     * native_header：时间戳、caplen和wirelen；
     * native_data：数据包原始字节地址。
     *
     * 二者都由libpcap管理，调用者不能释放。
     */
    native_result = pcap_next_ex(
        capture->native_handle,
        &native_header,
        &native_data
    );

    if (native_result == 1) {
        /*
         * 返回1表示成功取得一条数据包。
         *
         * pcap_open_offline默认以微秒精度返回时间戳，因此tv_usec
         * 表示当前秒内的微秒数。
         */
        new_packet = (capture_packet_view_t){
            .timestamp_seconds = (int64_t)native_header->ts.tv_sec,

            .timestamp_microseconds = (int32_t)native_header->ts.tv_usec,

            .captured_length = (uint32_t)native_header->caplen,

            .wire_length = (uint32_t)native_header->len,

            /*
             * u_char是libpcap使用的无符号字节类型。
             *
             * 项目公开接口统一使用uint8_t表示原始二进制字节。
             */
            .data = (const uint8_t *)native_data
        };

        /*
         * 完整转换成功后再发布输出结果。
         */
        *packet = new_packet;
        *status = CAPTURE_READ_STATUS_PACKET;

        return 0;
    }

    if (native_result == PCAP_ERROR_BREAK) {
        /*
         * 对离线PCAP而言，PCAP_ERROR_BREAK表示文件已经读取完毕，
         * 不是错误。
         *
         * 此时只更新status，不修改packet。
         */
        *status = CAPTURE_READ_STATUS_END_OF_FILE;

        return 0;
    }

    if (native_result == 0) {
        /*
         * 返回0主要用于实时抓包超时。当前对象是离线PCAP，正常情况
         * 不应该出现，但仍然显式转换为EAGAIN。
         */
        return EAGAIN;
    }

    /*
     * PCAP_ERROR以及其他负数状态表示libpcap读取失败。
     *
     * 调用者可以通过capture_get_error取得详细错误说明。
     */
    return EIO;
}
