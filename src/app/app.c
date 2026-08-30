#define _POSIX_C_SOURCE 200809L

#include "analyzer/app.h"
#include "analyzer/capture.h"
#include "analyzer/ethernet.h"
#include "analyzer/packet_info.h"
#include "analyzer/ipv4.h"
#include "analyzer/icmp.h"
#include "analyzer/ipv4_dispatch.h"
#include "analyzer/flow_table.h"
#include "analyzer/flow_export.h"
#include "analyzer/flow_expiration.h"
#include "analyzer/runtime_metrics.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

/*
 * 当前命令只显示前5个数据包，避免大型PCAP在终端输出数万行。
 */
#define APP_CAPTURE_PREVIEW_LIMIT 5U

/*
 * 第一版离线流表最多保存256条不同的双向流。
 *
 * 当前使用调用者提供的固定数组。容量不足时返回ENOSPC，
 * 后续将通过流超时清理或动态哈希表解决容量限制。
 */
#define APP_FLOW_TABLE_CAPACITY 256U

/*
 * 实时事件循环最多等待1秒就重新获得一次控制权。
 *
 * 这个时间属于应用层poll等待，不是libpcap数据包缓冲超时。
 */
#define APP_CAPTURE_WAIT_TIMEOUT_MS 1000

/*
 * 第一版实时流过期策略。
 *
 * 一条流最后活动30秒后具备过期条件，
 * 但最多每经过5秒的数据包事件时间扫描一次流表。
 */
#define APP_FLOW_IDLE_TIMEOUT_SECONDS INT64_C(30)
#define APP_FLOW_EXPIRATION_SCAN_INTERVAL_SECONDS INT64_C(5)

/*
 * 实时运行指标每5秒产生一次周期报告。
 *
 * 实际速率使用两次报告之间的真实单调时钟间隔计算，
 * 不直接假设每次正好经过5秒。
 */
#define APP_RUNTIME_METRICS_INTERVAL_SECONDS INT64_C(5)

/*
 * 正常情况下，ANALYZER_VERSION由CMake根据project版本传入。
 *
 * 保留development作为脱离CMake单独编译时的兜底值。
 */
#ifndef ANALYZER_VERSION
#define ANALYZER_VERSION "development"
#endif

/**
 * @brief 读取适合运行时间测量的单调时钟。
 *
 * CLOCK_MONOTONIC不表示真实日期，只用于计算运行间隔。
 * 系统时间校准或人工修改日期不会使它正常倒退。
 *
 * 函数成功后才修改timestamp。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         系统调用失败时返回errno或EIO；
 *         系统时间值无法转换时返回ERANGE。
 */
static int app_get_monotonic_timestamp(
    runtime_metrics_timestamp_t *timestamp)
{
    struct timespec native_timestamp;
    runtime_metrics_timestamp_t result_timestamp;

    int native_error;

    if (timestamp == NULL) {
        return EINVAL;
    }

    errno = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &native_timestamp) != 0) {
        native_error = errno;

        return native_error != 0 ? native_error : EIO;
    }

    if (native_timestamp.tv_sec < (time_t)0 ||
        native_timestamp.tv_nsec < 0L ||
        native_timestamp.tv_nsec >= 1000000000L) {
        return ERANGE;
    }

    result_timestamp.seconds = (int64_t)native_timestamp.tv_sec;

    /*
     * 通过转换回来比较，检查time_t是否存在无法放入int64_t的值。
     */
    if ((time_t)result_timestamp.seconds != native_timestamp.tv_sec) {
        return ERANGE;
    }

    result_timestamp.nanoseconds = (int32_t)native_timestamp.tv_nsec;

    *timestamp = result_timestamp;

    return 0;
}

/**
 * @brief 输出一条周期运行指标报告。
 */
static int app_print_runtime_metrics(
    const runtime_metrics_report_t *report)
{
    if (report == NULL ||
        report->elapsed_seconds <= 0.0 ||
        report->flow_table_capacity == 0U ||
        report->active_flow_count >
            report->flow_table_capacity) {
        return EINVAL;
    }

    if (printf(
            "Runtime metrics: "
            "interval=%.3f "
            "packets=%" PRIu64 " "
            "complete=%" PRIu64 " "
            "truncated=%" PRIu64 " "
            "malformed=%" PRIu64 " "
            "unsupported=%" PRIu64 " "
            "flow_rejected=%" PRIu64 " "
            "captured_bytes=%" PRIu64 " "
            "wire_bytes=%" PRIu64 " "
            "pps=%.2f "
            "captured_mbps=%.6f "
            "wire_mbps=%.6f "
            "active_flows=%zu/%zu "
            "flow_table_usage=%.2f%% "
            "expired_flows=%" PRIu64 "\n\n",
            report->elapsed_seconds,
            report->interval_packet_count,
            report->interval_complete_packet_count,
            report->interval_truncated_packet_count,
            report->interval_malformed_packet_count,
            report->interval_unsupported_packet_count,
            report->interval_flow_rejected_packet_count,
            report->interval_captured_byte_count,
            report->interval_wire_byte_count,
            report->packets_per_second,
            report->captured_megabits_per_second,
            report->wire_megabits_per_second,
            report->active_flow_count,
            report->flow_table_capacity,
            report->flow_table_usage_percent,
            report->interval_expired_flow_count) < 0) {
        return EIO;
    }

    return 0;
}

/**
 * @brief 检查实时运行指标周期，并在到期时输出报告。
 *
 * 无论当前是否刚收到数据包，都可以调用本函数。
 * 尚未到周期时成功返回，但不产生输出。
 */
static int app_maybe_print_runtime_metrics(
    runtime_metrics_schedule_t *schedule,
    const runtime_metrics_totals_t *totals,
    const flow_table_t *flow_table)
{
    runtime_metrics_timestamp_t current_timestamp;
    runtime_metrics_report_t report;

    bool report_due;
    int error_code;

    if (schedule == NULL ||
        totals == NULL ||
        flow_table == NULL ||
        !flow_table->initialized) {
        return EINVAL;
    }

    error_code = app_get_monotonic_timestamp(
        &current_timestamp
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = runtime_metrics_schedule_observe(
        schedule,
        &current_timestamp,
        totals,
        flow_table_count(flow_table),
        flow_table->capacity,
        &report_due,
        &report
    );

    if (error_code != 0) {
        return error_code;
    }

    if (!report_due) {
        return 0;
    }

    return app_print_runtime_metrics(&report);
}

/**
 * @brief 把十进制字符串解析为大于0的size_t数值。
 *
 * 函数先使用uintmax_t接收转换结果，再检查结果是否超出size_t范围。
 * 只有全部检查通过后才修改value，失败时保留调用者原来的值。
 *
 * @param text 待解析的十进制字符串。
 * @param value 用于接收解析结果。
 *
 * @return 成功时返回0，格式错误、数值为0或超出范围时返回EINVAL。
 */
static int app_parse_positive_size(
    const char *text,
    size_t *value)
{
    char *end_pointer = NULL;
    uintmax_t parsed_value;

    /*
     * 要求第一个字符就是数字。
     *
     * 这样同时拒绝空字符串、负数、正号和前导空格。
     */
    if (text == NULL ||
        value == NULL ||
        text[0] < '0' ||
        text[0] > '9') {
        return EINVAL;
    }

    /*
     * strtoumax失败时可能通过errno报告ERANGE。
     *
     * 调用前必须清空errno，不能使用更早函数留下的错误值。
     */
    errno = 0;

    parsed_value = strtoumax(
        text,
        &end_pointer,
        10
    );

    /*
     * end_pointer必须到达字符串末尾，否则说明存在非数字字符。
     */
    if (errno != 0 ||
        end_pointer == text ||
        *end_pointer != '\0' ||
        parsed_value == 0U ||
        parsed_value > (uintmax_t)SIZE_MAX) {
        return EINVAL;
    }

    *value = (size_t)parsed_value;

    return 0;
}

/**
 * @brief 打印命令行使用帮助。
 *
 * @param program_name 显示在Usage中的程序名称。
 *
 * @return 输出成功时返回0，输出失败时返回EIO。
 */
static int app_print_help(const char *program_name)
{
    const char *display_name = program_name != NULL ? program_name : "netflow-analyzer";

    if (printf(
            "Usage: %s [OPTION]\n"
            "       %s --read <PCAP_FILE> [--csv <CSV_FILE>]\n"
            "       %s --interface <INTERFACE> --count <PACKETS> [--filter <BPF_EXPRESSION>]\n"
            "\n"
            "Linux network traffic analyzer.\n"
            "\n"
            "Options:\n"
            "  -h, --help       Show this help message.\n"
            "  -V, --version    Show program version.\n"
            "  -r, --read FILE  Analyze an offline PCAP file.\n"
            "  -i, --interface NAME  Analyze a live capture interface.\n"
            "  -c, --count PACKETS   Stop after capturing PACKETS packets.\n"
            "      --filter EXPRESSION  Apply a BPF filter to live capture.\n"
            "      --csv FILE   Export flow records to a new CSV file.\n",
            display_name,
            display_name,
            display_name) < 0) {
        return EIO;
    }

    return 0;
}

/**
 * @brief 把IPv4协议号转换成适合流汇总显示的名称。
 *
 * @param protocol IPv4头部中的上层协议号。
 *
 * @return 指向静态字符串的只读指针。
 */
static const char *app_ipv4_protocol_name(uint8_t protocol)
{
    switch (protocol) {
    case IPV4_PROTOCOL_TCP:
        return "TCP";

    case IPV4_PROTOCOL_UDP:
        return "UDP";

    case IPV4_PROTOCOL_ICMP:
        return "ICMP";

    default:
        return "UNKNOWN";
    }
}

/**
 * @brief 把常见ICMP类型转换成适合命令行显示的名称。
 *
 * @param type ICMP消息类型。
 *
 * @return 指向静态字符串的只读指针。
 */
static const char *app_icmp_type_name(uint8_t type)
{
    switch (type) {
    case ICMP_TYPE_ECHO_REPLY:
        return "echo-reply";

    case ICMP_TYPE_DESTINATION_UNREACHABLE:
        return "destination-unreachable";

    case ICMP_TYPE_REDIRECT:
        return "redirect";

    case ICMP_TYPE_ECHO_REQUEST:
        return "echo-request";

    case ICMP_TYPE_TIME_EXCEEDED:
        return "time-exceeded";

    case ICMP_TYPE_PARAMETER_PROBLEM:
        return "parameter-problem";

    default:
        return "unknown";
    }
}

/**
 * @brief 使用统一终端格式输出一条流记录。
 *
 * label决定记录前缀，例如Flow或Expired flow。
 * display_index是从1开始的显示编号。
 */
static int app_print_flow_record(
    const char *label,
    size_t display_index,
    const flow_record_t *record)
{
    char endpoint_a_address[IPV4_ADDRESS_STRING_SIZE];
    char endpoint_b_address[IPV4_ADDRESS_STRING_SIZE];

    int error_code;

    if (label == NULL ||
        label[0] == '\0' ||
        display_index == 0U ||
        record == NULL ||
        !record->initialized ||
        record->first_seen.microseconds < INT32_C(0) ||
        record->first_seen.microseconds > INT32_C(999999) ||
        record->last_seen.microseconds < INT32_C(0) ||
        record->last_seen.microseconds > INT32_C(999999)) {
        return EINVAL;
    }

    error_code = ipv4_format_address(
        record->key.endpoint_a.ipv4_address,
        endpoint_a_address,
        sizeof(endpoint_a_address)
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = ipv4_format_address(
        record->key.endpoint_b.ipv4_address,
        endpoint_b_address,
        sizeof(endpoint_b_address)
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * ICMP没有端口，因此ICMP流的两个端口显示为0。
     *
     * 对活动流和过期流使用完全相同的字段顺序，
     * 便于人工比较，也便于后续脚本解析。
     */
    if (printf(
            "%s %zu: "
            "protocol=%s "
            "endpoint_a=%s:%u "
            "endpoint_b=%s:%u "
            "a_to_b_packets=%" PRIu64 " "
            "a_to_b_captured_bytes=%" PRIu64 " "
            "a_to_b_wire_bytes=%" PRIu64 " "
            "b_to_a_packets=%" PRIu64 " "
            "b_to_a_captured_bytes=%" PRIu64 " "
            "b_to_a_wire_bytes=%" PRIu64 " "
            "first_seen=%" PRId64 ".%06" PRId32 " "
            "last_seen=%" PRId64 ".%06" PRId32 "\n",
            label,
            display_index,
            app_ipv4_protocol_name(
                record->key.protocol
            ),
            endpoint_a_address,
            (unsigned int)record->key.endpoint_a.port,
            endpoint_b_address,
            (unsigned int)record->key.endpoint_b.port,
            record->a_to_b.packet_count,
            record->a_to_b.captured_byte_count,
            record->a_to_b.wire_byte_count,
            record->b_to_a.packet_count,
            record->b_to_a.captured_byte_count,
            record->b_to_a.wire_byte_count,
            record->first_seen.seconds,
            record->first_seen.microseconds,
            record->last_seen.seconds,
            record->last_seen.microseconds) < 0) {
        return EIO;
    }

    return 0;
}

/**
 * @brief 输出一次扫描返回的全部过期流副本。
 *
 * already_printed表示以前已经成功输出的过期流数量，
 * 用于让多批结果使用连续编号。
 */
static int app_print_expired_flows(
    const flow_record_t *records,
    size_t record_count,
    size_t already_printed)
{
    size_t index;
    int error_code;

    if (records == NULL) {
        return EINVAL;
    }

    /*
     * 后面要计算already_printed + index + 1，
     * 因此先检查编号累计是否可能溢出。
     */
    if (record_count > SIZE_MAX - already_printed) {
        return EOVERFLOW;
    }

    for (index = 0U; index < record_count; index += 1U) {
        error_code = app_print_flow_record(
            "Expired flow",
            already_printed + index + 1U,
            &records[index]
        );

        if (error_code != 0) {
            return error_code;
        }
    }

    return 0;
}

/**
 * @brief 输出流表中仍然保留的全部双向流记录。
 *
 * endpoint_a和endpoint_b是规范化排序后的端点，
 * 不一定分别对应客户端和服务器。
 *
 * @param table 指向已经初始化的流表。
 *
 * @return 成功时返回0；
 *         参数或流表状态无效时返回EINVAL；
 *         地址格式化失败时返回对应错误码；
 *         终端输出失败时返回EIO。
 */
static int app_print_flow_summary(const flow_table_t *table)
{
    const flow_record_t *record;

    size_t flow_count;
    size_t index;
    int error_code;

    if (table == NULL || !table->initialized) {
        return EINVAL;
    }

    flow_count = flow_table_count(table);

    if (printf(
            "\nFlow summary: %zu flow(s)\n",
            flow_count) < 0) {
        return EIO;
    }

    for (index = 0U; index < flow_count; index += 1U) {
        error_code = flow_table_get(
            table,
            index,
            &record
        );

        if (error_code != 0) {
            return error_code;
        }

        error_code = app_print_flow_record(
            "Flow",
            index + 1U,
            record
        );

        if (error_code != 0) {
            return error_code;
        }
    }

    return 0;
}

/**
 * @brief 输出数据包处理路径的累计流表探测统计。
 *
 * 平均探测长度只在累计计数未饱和时计算。
 * 一旦计数饱和，累计值已经不再精确，继续输出平均值会产生误导，
 * 因此使用average=unavailable明确表示不可用。
 *
 * @param table 指向仍然有效的流表。
 *
 * @return 成功时返回0；
 *         流表或统计状态无效时返回对应错误码；
 *         终端输出失败时返回EIO。
 */
static int app_print_flow_table_probe_statistics(
    const flow_table_t *table)
{
    flow_table_probe_statistics_t statistics;
    double average_probe_length;
    int error_code;

    /*
     * statistics是调用栈上的值副本。
     *
     * 查询函数不会返回内部指针，因此这里没有内存所有权转移，
     * 也不需要执行free。
     */
    error_code = flow_table_get_probe_statistics(
        table,
        &statistics
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 计数饱和后，真实分子或分母可能已经大于UINT64_MAX。
     * 此时用两个截断后的数值相除没有可靠意义。
     */
    if (statistics.counters_saturated) {
        if (printf(
                "Flow table probes: "
                "operations=%" PRIu64 " "
                "inspected_slots=%" PRIu64 " "
                "average=unavailable "
                "maximum=%zu "
                "saturated=true\n",
                statistics.packet_operation_count,
                statistics.total_inspected_slot_count,
                statistics.maximum_probe_length) < 0) {
            return EIO;
        }

        return 0;
    }

    /*
     * 尚未处理可进入流表的数据包时，平均探测长度定义为0。
     *
     * 两个操作数先转换为double，避免执行整数除法。
     */
    if (statistics.packet_operation_count == UINT64_C(0)) {
        average_probe_length = 0.0;
    } else {
        average_probe_length =
            (double)statistics.total_inspected_slot_count /
            (double)statistics.packet_operation_count;
    }

    if (printf(
            "Flow table probes: "
            "operations=%" PRIu64 " "
            "inspected_slots=%" PRIu64 " "
            "average=%.2f "
            "maximum=%zu "
            "saturated=false\n",
            statistics.packet_operation_count,
            statistics.total_inspected_slot_count,
            average_probe_length,
            statistics.maximum_probe_length) < 0) {
        return EIO;
    }

    return 0;
}

/**
 * @brief 把流表中的全部记录导出到一个新CSV文件。
 *
 * 本函数拥有局部FILE对象的完整生命周期：
 *
 * 1. 使用独占创建模式打开文件；
 * 2. 写入CSV表头；
 * 3. 遍历流表并写入记录；
 * 4. 无论中途是否失败，都执行fclose。
 *
 * "wx"模式拒绝覆盖已有文件。
 *
 * @param table 指向已经完成聚合的流表。
 * @param output_path 准备创建的CSV文件路径。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         文件已存在时返回EEXIST；
 *         其他打开、写入或关闭错误返回对应错误码。
 */
static int app_export_flow_table_csv(
    const flow_table_t *table,
    const char *output_path)
{
    const flow_record_t *record;
    FILE *output;

    size_t flow_count;
    size_t index;

    int operation_error;
    int close_error;

    if (table == NULL ||
        !table->initialized ||
        output_path == NULL ||
        output_path[0] == '\0') {
        return EINVAL;
    }

    /*
     * errno在成功调用后没有确定意义。
     * 打开前清零，失败后才能安全地判断是否获得了具体错误码。
     */
    errno = 0;

    /*
     * x表示独占创建。
     *
     * 如果文件已经存在，fopen失败，不会截断原文件。
     */
    output = fopen(output_path, "wx");

    if (output == NULL) {
        return errno != 0 ? errno : EIO;
    }

    operation_error =
        flow_export_write_csv_header(output);

    if (operation_error == 0) {
        flow_count = flow_table_count(table);

        for (index = 0U; index < flow_count; index += 1U) {
            operation_error = flow_table_get(table, index, &record);

            if (operation_error != 0) {
                break;
            }

            operation_error =
                flow_export_write_csv_record(
                    output,
                    record
                );

            if (operation_error != 0) {
                break;
            }
        }
    }

    /*
     * fclose可能在刷新标准库缓冲区时发现真正的磁盘写入错误，
     * 因此不能忽略它的返回值。
     */
    errno = 0;
    close_error = 0;

    if (fclose(output) != 0) {
        close_error = errno != 0 ? errno : EIO;
    }

    /*
     * 如果写入和关闭都失败，优先返回更早发生的写入错误。
     */
    if (operation_error != 0) {
        return operation_error;
    }

    return close_error;
}


/**
 * @brief 解析一条捕获数据包，更新流表，并按需输出数据包概要。
 *
 * 无论print_preview是否为true，函数都会完成协议解析，并把成功
 * 解析的TCP、UDP或ICMP数据包加入flow_table。
 *
 * print_preview只控制是否输出逐包信息，不影响流量聚合。
 *
 * packet->data由libpcap管理，只保证在下一次capture_next_packet
 * 调用前有效。因此必须在本函数返回前完成解析。
 *
 * 单个数据包截断、畸形或协议不支持时不加入流表，也不终止整个
 * PCAP读取流程。
 *
 * @param packet_number 当前数据包在整个PCAP中的序号。
 * @param packet 指向libpcap返回的只读数据包视图。
 * @param print_preview true表示输出逐包信息。
 * @param flow_table 指向当前离线分析使用的流表。
 * @param packet_result 接收当前数据包的最终处理分类。函数返回0时一定写入packet_result。函数返回非0时，packet_result内容未定义，调用者不能使用。
 *
 * @return 成功处理时返回0；
 *         参数或程序内部状态错误时返回对应错误码；
 *         新流因流表容量耗尽而被拒绝时返回0，并通过packet_result报告FLOW_REJECTED；
 *         终端输出失败时返回EIO。
 */
static int app_process_packet(
    size_t packet_number,
    const capture_packet_view_t *packet,
    bool print_preview,
    flow_table_t *flow_table,
    runtime_metrics_packet_result_t *packet_result)
{
    packet_info_t packet_info;
    const flow_record_t *updated_flow;
    runtime_metrics_packet_result_t result = RUNTIME_METRICS_PACKET_RESULT_COMPLETE;
    bool flow_created;

    char source_mac[ETHERNET_MAC_STRING_SIZE];
    char destination_mac[ETHERNET_MAC_STRING_SIZE];

    char source_ipv4[IPV4_ADDRESS_STRING_SIZE];
    char destination_ipv4[IPV4_ADDRESS_STRING_SIZE];

    int error_code;

    if (packet == NULL ||
        flow_table == NULL ||
        !flow_table->initialized ||
        packet_result == NULL) {
        return EINVAL;
    }
    /*
     * packet_info_init只复制时间戳、caplen和wirelen。
     *
     * 它不会保存packet->data，因此不会受到libpcap下一次读取
     * 导致原始数据指针失效的影响。
     */
    error_code = packet_info_init(
        &packet_info,
        packet->timestamp_seconds,
        packet->timestamp_microseconds,
        packet->captured_length,
        packet->wire_length
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 第一层先解析Ethernet。
     *
     * 解析边界必须使用captured_length，因为只有这些字节真实存在于
     * packet->data指向的缓冲区中。wire_length只能用于统计。
     */
    error_code = ethernet_parse(
        packet->data,
        (size_t)packet->captured_length,
        &packet_info
    );

    if (error_code == ENODATA) {

        if (!print_preview) {
            *packet_result = RUNTIME_METRICS_PACKET_RESULT_TRUNCATED;
            return 0;
        }

        /*
         * Ethernet头部不足14字节属于单个数据包截断。
         *
         * 记录并显示错误后继续处理PCAP中的下一包。
         */
        if (printf(
                "Packet %zu: "
                "timestamp=%" PRId64 ".%06" PRId32 " "
                "caplen=%" PRIu32 " "
                "wirelen=%" PRIu32 " "
                "ethernet=truncated "
                "error_offset=%zu\n",
                packet_number,
                packet_info.timestamp_seconds,
                packet_info.timestamp_microseconds,
                packet_info.captured_length,
                packet_info.wire_length,
                packet_info.error_offset) < 0) {
            return EIO;
        }
        *packet_result = RUNTIME_METRICS_PACKET_RESULT_TRUNCATED;
        return 0;
    }

    if (error_code != 0) {
        /*
         * EINVAL等错误通常表示调用顺序或程序内部状态存在问题，
         * 不能把它简单归因于输入数据包，因此返回给上层。
         */
        return error_code;
    }

    if (print_preview) {
        /*
        * Ethernet解析成功后，MAC地址字段才有效。
        */
        error_code = ethernet_format_mac(
            packet_info.source_mac,
            source_mac,
            sizeof(source_mac)
        );

        if (error_code != 0) {
            return error_code;
        }

        error_code = ethernet_format_mac(
            packet_info.destination_mac,
            destination_mac,
            sizeof(destination_mac)
        );

        if (error_code != 0) {
            return error_code;
        }
    }

    /*
     * 只有EtherType为0x0800时，Ethernet负载才是IPv4。
     *
     * ARP、IPv6或未知EtherType仍然可以是合法Ethernet帧，
     * 只是当前项目还没有对应的网络层解析器。
     */
    if (packet_info.ether_type != ETHERNET_TYPE_IPV4) {
        
        if (!print_preview) {
            *packet_result =
                RUNTIME_METRICS_PACKET_RESULT_UNSUPPORTED;
            return 0;
        }

        if (printf(
                "Packet %zu: "
                "timestamp=%" PRId64 ".%06" PRId32 " "
                "caplen=%" PRIu32 " "
                "wirelen=%" PRIu32 " "
                "src_mac=%s "
                "dst_mac=%s "
                "ethertype=0x%04" PRIx16 " "
                "network=unsupported\n",
                packet_number,
                packet_info.timestamp_seconds,
                packet_info.timestamp_microseconds,
                packet_info.captured_length,
                packet_info.wire_length,
                source_mac,
                destination_mac,
                packet_info.ether_type) < 0) {
            return EIO;
        }
        *packet_result =
            RUNTIME_METRICS_PACKET_RESULT_UNSUPPORTED;
        return 0;
    }

    /*
     * Ethernet已经确认其负载是IPv4，因此继续解析IPv4头部。
     */
    error_code = ipv4_parse(
        packet->data,
        (size_t)packet->captured_length,
        &packet_info
    );

    if (error_code == ENODATA || error_code == EBADMSG) {

        result = 
            error_code == 
                ENODATA
                ? RUNTIME_METRICS_PACKET_RESULT_TRUNCATED
                : RUNTIME_METRICS_PACKET_RESULT_MALFORMED;

        if (!print_preview) {
            *packet_result = result;
            return 0;
        }

        const char *ipv4_state;

        /*
         * ENODATA表示必要的IPv4头部没有完整捕获；
         * EBADMSG表示字节足够，但版本、IHL或长度关系不合法。
         */
        ipv4_state = error_code == ENODATA ? "truncated" : "malformed";

        if (printf(
                "Packet %zu: "
                "timestamp=%" PRId64 ".%06" PRId32 " "
                "caplen=%" PRIu32 " "
                "wirelen=%" PRIu32 " "
                "src_mac=%s "
                "dst_mac=%s "
                "ethertype=0x%04" PRIx16 " "
                "ipv4=%s "
                "error_offset=%zu\n",
                packet_number,
                packet_info.timestamp_seconds,
                packet_info.timestamp_microseconds,
                packet_info.captured_length,
                packet_info.wire_length,
                source_mac,
                destination_mac,
                packet_info.ether_type,
                ipv4_state,
                packet_info.error_offset) < 0) {
            return EIO;
        }

        /*
         * 单个数据包异常不终止整个PCAP文件预览。
         */
        *packet_result = result;
        return 0;
    }

    if (error_code != 0) {
        return error_code;
    }

    if (print_preview) {
    /*
     * has_ipv4为true后，源地址和目标地址才是有效结果。
     */
    error_code = ipv4_format_address(
        packet_info.source_ipv4,
        source_ipv4,
        sizeof(source_ipv4)
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = ipv4_format_address(
        packet_info.destination_ipv4,
        destination_ipv4,
        sizeof(destination_ipv4)
    );

    if (error_code != 0) {
        return error_code;
    }
}

    /*
     * IPv4解析完成后，根据protocol字段分发TCP、UDP或ICMP。
     */
    error_code = ipv4_dispatch_payload(
        packet->data,
        (size_t)packet->captured_length,
        &packet_info
    );

    /*
     * 下面三类错误可以由单个异常或暂不支持的数据包造成。
     *
     * 它们应当显示在当前数据包结果中，但不终止整个PCAP读取：
     *
     * ENODATA：必要上层头部未完整捕获；
     * EBADMSG：上层协议字段组合不合法；
     * ENOTSUP：协议或IPv4分片暂不支持。
     *
     * EINVAL等错误更可能来自程序调用顺序错误，因此仍返回上层。
     */
    if (error_code != 0 &&
        error_code != ENODATA &&
        error_code != EBADMSG &&
        error_code != ENOTSUP) {
        return error_code;
    }

    /*
     * 把协议解析器使用的errno风格结果，
     * 转换成稳定的应用处理分类。
     */
    if (error_code == ENODATA) {
        result =
            RUNTIME_METRICS_PACKET_RESULT_TRUNCATED;
    } else if (error_code == EBADMSG) {
        result =
            RUNTIME_METRICS_PACKET_RESULT_MALFORMED;
    } else if (error_code == ENOTSUP) {
        result =
            RUNTIME_METRICS_PACKET_RESULT_UNSUPPORTED;
    }

    /*
     * 只有协议分发成功、parse_status为COMPLETE的数据包，
     * 才能生成可靠的双向五元组并进入流表。
     */
    if (error_code == 0) {
        error_code = flow_table_process_packet(
            flow_table,
            &packet_info,
            &updated_flow,
            &flow_created
        );

        if (error_code == ENOSPC) {
            /*
             * 流表已满时，当前包已经成功解析，只是无法加入新流。
             *
             * flow_table_process_packet保证ENOSPC不会留下部分更新，
             * 因此可以记录拒绝并继续分析后续数据包。
             */
            result =
                RUNTIME_METRICS_PACKET_RESULT_FLOW_REJECTED;

            /*
             * 后续逐包输出仍应显示已经成功解析的协议字段。
             */
            error_code = 0;
        } else if (error_code != 0) {
            return error_code;
        } else {
            result =
                RUNTIME_METRICS_PACKET_RESULT_COMPLETE;

            /*
             * 当前应用只需要知道流表已经完成更新。
             */
            (void)updated_flow;
            (void)flow_created;
        }
    }

    /*
     * 第6包及后续数据包仍然参与流量聚合，但不输出逐包信息。
     */
    if (!print_preview) {
        *packet_result = result;
        return 0;
    }

    /*
     * 先输出所有IPv4数据包共有的信息。
     *
     * 这里暂时不换行，后面的协议分支会补充协议字段并结束当前行。
     */
    if (printf(
            "Packet %zu: "
            "timestamp=%" PRId64 ".%06" PRId32 " "
            "caplen=%" PRIu32 " "
            "wirelen=%" PRIu32 " "
            "src_mac=%s "
            "dst_mac=%s "
            "ethertype=0x%04" PRIx16 " "
            "src_ip=%s "
            "dst_ip=%s "
            "ttl=%u "
            "ip_length=%" PRIu16 " "
            "ip_payload_truncated=%s ",
            packet_number,
            packet_info.timestamp_seconds,
            packet_info.timestamp_microseconds,
            packet_info.captured_length,
            packet_info.wire_length,
            source_mac,
            destination_mac,
            packet_info.ether_type,
            source_ipv4,
            destination_ipv4,
            (unsigned int)packet_info.ipv4_ttl,
            packet_info.ipv4_total_length,
            packet_info.ipv4_payload_truncated ? "true" : "false") < 0) {
        return EIO;
    }

    /*
     * 协议分发失败时，输出错误状态并继续读取下一包。
     */
    if (error_code != 0) {
        const char *upper_state;

        if (error_code == ENODATA) {
            upper_state = "truncated";
        } else if (error_code == EBADMSG) {
            upper_state = "malformed";
        } else {
            upper_state = "unsupported";
        }

        if (printf(
                "protocol_number=%u "
                "upper=%s "
                "error_offset=%zu\n",
                (unsigned int)packet_info.ipv4_protocol,
                upper_state,
                packet_info.error_offset) < 0) {
            return EIO;
        }
        *packet_result = result;
        return 0;
    }

    /*
     * 分发器成功后，恰好有一种上层协议结果有效。
     */
    if (packet_info.has_tcp) {
        if (printf(
                "protocol=TCP "
                "src_port=%" PRIu16 " "
                "dst_port=%" PRIu16 " "
                "sequence=%" PRIu32 " "
                "acknowledgment=%" PRIu32 " "
                "flags=0x%03" PRIx16 " "
                "payload_length=%zu "
                "payload_truncated=%s\n\n",
                packet_info.tcp_source_port,
                packet_info.tcp_destination_port,
                packet_info.tcp_sequence_number,
                packet_info.tcp_acknowledgment_number,
                packet_info.tcp_flags,
                packet_info.tcp_payload_length,
                packet_info.tcp_payload_truncated
                    ? "true"
                    : "false") < 0) {
            return EIO;
        }
        *packet_result = result;
        return 0;
    }

    if (packet_info.has_udp) {
        if (printf(
                "protocol=UDP "
                "src_port=%" PRIu16 " "
                "dst_port=%" PRIu16 " "
                "udp_length=%" PRIu16 " "
                "payload_length=%zu "
                "payload_truncated=%s\n\n",
                packet_info.udp_source_port,
                packet_info.udp_destination_port,
                packet_info.udp_length,
                packet_info.udp_payload_length,
                packet_info.udp_payload_truncated
                    ? "true"
                    : "false") < 0) {
            return EIO;
        }
        *packet_result = result;
        return 0;
    }

    if (packet_info.has_icmp) {
        if (packet_info.icmp_has_echo_fields) {
            if (printf(
                    "protocol=ICMP "
                    "type=%s "
                    "code=%u "
                    "identifier=%" PRIu16 " "
                    "sequence=%" PRIu16 " "
                    "payload_length=%zu "
                    "payload_truncated=%s\n\n",
                    app_icmp_type_name(
                        packet_info.icmp_type
                    ),
                    (unsigned int)packet_info.icmp_code,
                    packet_info.icmp_identifier,
                    packet_info.icmp_sequence,
                    packet_info.icmp_payload_length,
                    packet_info.icmp_payload_truncated
                        ? "true"
                        : "false") < 0) {
                return EIO;
            }
        } else {
            if (printf(
                    "protocol=ICMP "
                    "type=%s "
                    "type_number=%u "
                    "code=%u "
                    "payload_length=%zu "
                    "payload_truncated=%s\n\n",
                    app_icmp_type_name(
                        packet_info.icmp_type
                    ),
                    (unsigned int)packet_info.icmp_type,
                    (unsigned int)packet_info.icmp_code,
                    packet_info.icmp_payload_length,
                    packet_info.icmp_payload_truncated
                        ? "true"
                        : "false") < 0) {
                return EIO;
            }
        }
        *packet_result = result;
        return 0;
    }

    /*
     * 分发器返回成功却没有任何协议结果，说明程序内部状态不一致。
     */
    return EIO;
}

/**
 * @brief 分析离线PCAP或有限数量的实时数据包。
 *
 * 两种输入复用相同的协议解析、流表聚合和结果输出流程，
 * 区别只在于采集句柄的打开方式和循环结束条件。
 *
 * 离线模式读取到文件末尾；
 * 实时模式读取到context->packet_limit指定的包数。
 */
static int app_run_capture_analysis(app_context_t *context)
{
    char capture_error[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    /*
     * pcap_stats错误信息可能依赖仍然存活的capture句柄。
     *
     * 如果查询失败，必须在capture_close之前把说明复制到这个
     * 应用层局部缓冲区，关闭后才能继续安全打印。
     */
    char statistics_error_message[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    /*
    * 哈希流表的每个物理位置都是一个槽位。
    *
    * 槽位中同时保存状态和流记录，因此这里不能再直接声明
    * flow_record_t数组。
    */
    flow_table_slot_t flow_slots[APP_FLOW_TABLE_CAPACITY];
    flow_table_t flow_table;

    /*
    * flow_table_expire_before会在删除前把过期记录复制到这里。
    *
    * 数组容量与流表相同，因此一次扫描最多过期整个流表时，
    * 输出缓冲区仍然足够。
    */
    flow_record_t expired_flow_records[APP_FLOW_TABLE_CAPACITY];

    /*
    * 调度器只保存时间和策略，不拥有动态资源。
    */
    flow_expiration_schedule_t expiration_schedule = {0};

    /*
    * runtime_metrics_totals保存启动以来的累计量。
    *
    * runtime_metrics_schedule保存上一次周期报告的时间和累计基线。
    */
    runtime_metrics_totals_t runtime_metrics_totals = {0};
    runtime_metrics_schedule_t runtime_metrics_schedule = {0};
    runtime_metrics_timestamp_t runtime_metrics_start = {0};
    runtime_metrics_packet_result_t packet_result = RUNTIME_METRICS_PACKET_RESULT_COMPLETE;

    flow_timestamp_t packet_timestamp = {0};
    flow_timestamp_t expiration_cutoff = {0};

    capture_t *capture = NULL;
    capture_link_type_t link_type = CAPTURE_LINK_TYPE_UNKNOWN;

    capture_packet_view_t packet;
    capture_read_status_t read_status;
    capture_wait_status_t wait_status = CAPTURE_WAIT_STATUS_UNKNOWN;
    capture_statistics_t capture_statistics = {0};

    const char *capture_source;
    const char *native_statistics_error = NULL;

    size_t total_packet_count = 0U;
    size_t previewed_packet_count = 0U;

    size_t expired_flow_count = 0U;
    size_t total_expired_flow_count = 0U;

    bool live_capture;
    bool print_preview;
    /*
     * false既可能表示尚未查询，也可能表示当前平台查询失败。
     * 只有capture_get_statistics明确返回0时才设置为true。
     */
    bool capture_statistics_available = false;

    bool expiration_scan_due = false;

    int error_code;
    int statistics_error_code = 0;

    if (context == NULL) {
        return EINVAL;
    }

    /*
     * 根据命令确定输入来源。
     *
     * 后面的协议解析和流量聚合不需要再区分数据来自文件还是网卡。
     */
    live_capture = context->command == APP_COMMAND_CAPTURE_INTERFACE;

    if (live_capture) {
        if (context->interface_name == NULL ||
            context->interface_name[0] == '\0' ||
            context->packet_limit == 0U) {
            return EINVAL;
        }

        capture_source = context->interface_name;

        error_code = capture_open_live(
            capture_source,
            CAPTURE_DEFAULT_SNAPSHOT_LENGTH,
            false,
            CAPTURE_DEFAULT_READ_TIMEOUT_MS,
            &capture,
            capture_error,
            sizeof(capture_error)
        );
    } else if (
        context->command == APP_COMMAND_READ_CAPTURE) {
        if (context->capture_path == NULL ||
            context->capture_path[0] == '\0') {
            return EINVAL;
        }

        capture_source = context->capture_path;

        error_code = capture_open_offline(
            capture_source,
            &capture,
            capture_error,
            sizeof(capture_error)
        );
    } else {
        return EINVAL;
    }

    if (error_code != 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to open %s '%s': %s",
            live_capture
                ? "interface"
                : "capture",
            capture_source,
            capture_error[0] != '\0'
                ? capture_error
                : "unknown libpcap error"
        );

        return error_code;
    }

    error_code = capture_get_link_type(
        capture,
        &link_type
    );

    if (error_code != 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to query capture link type"
        );

        capture_close(&capture);
        return error_code;
    }

    if (link_type != CAPTURE_LINK_TYPE_ETHERNET) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "capture uses an unsupported link-layer type"
        );

        capture_close(&capture);
        return ENOTSUP;
    }

    /*
     * 第一版只为实时采集安装CLI提供的BPF表达式。
     *
     * 安装位置必须满足：
     *
     * 1. capture句柄已经成功打开；
     * 2. 链路类型已经取得并验证；
     * 3. 尚未进入capture_next_packet读取循环。
     *
     * 因此安装失败时不会有任何未过滤数据包进入解析和流表。
     */
    if (live_capture && context->filter_expression != NULL) {
        error_code = capture_set_filter(
            capture,
            context->filter_expression,
            capture_error,
            sizeof(capture_error)
        );

        if (error_code != 0) {
            /*
             * capture_error属于当前函数，不依赖capture句柄生命周期。
             * 仍然必须在关闭句柄前完成应用层错误信息组装。
             */
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to install BPF filter '%s' "
                "on interface '%s': %s",
                context->filter_expression,
                capture_source,
                capture_error[0] != '\0'
                    ? capture_error
                    : "unknown libpcap error"
            );

            /*
             * 流表尚未初始化，因此这里只需要关闭capture。
             */
            capture_close(&capture);

            return error_code;
        }
    }

    /*
     * BPF安装完成后，把实时采集切换到非阻塞模式。
     *
     * 后续capture_next_packet没有现成数据时会立即返回EAGAIN，
     * 主循环再通过capture_wait_readable低CPU等待数据或超时。
     */
    if (live_capture) {
        error_code = capture_enable_nonblocking(
            capture,
            capture_error,
            sizeof(capture_error)
        );

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to enable nonblocking capture "
                "on interface '%s': %s",
                capture_source,
                capture_error[0] != '\0'
                    ? capture_error
                    : strerror(error_code)
            );

            /*
             * 流表尚未初始化，此处只需要关闭capture。
             */
            capture_close(&capture);
            return error_code;
        }
    }

    error_code = flow_table_init(
        &flow_table,
        flow_slots,
        sizeof(flow_slots) / sizeof(flow_slots[0])
    );

    if (error_code != 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to initialize flow table"
        );

        capture_close(&capture);
        return error_code;
    }

    /*
    * 离线PCAP继续保留完整文件级聚合语义。
    * 第一版只为实时采集初始化并使用过期调度器。
    */
    if (live_capture) {
        error_code = flow_expiration_schedule_init(
            &expiration_schedule,
            APP_FLOW_IDLE_TIMEOUT_SECONDS,
            APP_FLOW_EXPIRATION_SCAN_INTERVAL_SECONDS
        );

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to initialize flow expiration schedule"
            );

            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return error_code;
        }

        error_code = app_get_monotonic_timestamp(&runtime_metrics_start);

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to read initial monotonic clock: %s",
                strerror(error_code)
            );

            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return error_code;
        }

        error_code = runtime_metrics_schedule_init(
            &runtime_metrics_schedule,
            APP_RUNTIME_METRICS_INTERVAL_SECONDS,
            &runtime_metrics_start,
            &runtime_metrics_totals
        );

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to initialize runtime metrics: %s",
                strerror(error_code)
            );

            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return error_code;
        }
    }

    if (live_capture) {
        error_code = printf(
            "Capture interface: %s\n"
            "Packet limit: %zu\n"
            "BPF filter: %s\n"
            "Link type: Ethernet\n"
            "Flow idle timeout: %" PRId64 " second(s)\n"
            "Flow expiration scan interval: %" PRId64 " second(s)\n"
            "Runtime metrics interval: %" PRId64 " second(s)\n\n",
            capture_source,
            context->packet_limit,
            context->filter_expression != NULL
                ? context->filter_expression
                : "(none)",
            APP_FLOW_IDLE_TIMEOUT_SECONDS,
            APP_FLOW_EXPIRATION_SCAN_INTERVAL_SECONDS,
            APP_RUNTIME_METRICS_INTERVAL_SECONDS
        );
    } else {
        error_code = printf(
            "Capture file: %s\n"
            "Link type: Ethernet\n",
            capture_source
        );
    }

    if (error_code < 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to write capture information"
        );

        flow_table_cleanup(&flow_table);
        capture_close(&capture);
        return EIO;
    }

    /*
     * 从这里开始，停止请求需要能够中断正在进行的libpcap读取。
     *
     * context只临时借用capture，真正的关闭职责仍属于本函数。
     */
    context->active_capture = capture;
    for (;;) {
        /*
         * 停止请求可能发生在两次读取之间。
         *
         * 此时不再进入capture_next_packet，直接沿正常路径完成
         * capture关闭、流汇总输出和资源清理。
         */
        if (context->stop_requested != 0) {
            break;
        }

        /*
         * 离线模式由文件末尾结束；
         * 实时模式由用户指定的数据包数量结束。
         */
        if (live_capture && total_packet_count >= context->packet_limit) {
            break;
        }

        read_status = CAPTURE_READ_STATUS_UNKNOWN;

        error_code = capture_next_packet(
            capture,
            &packet,
            &read_status
        );

        /*
         * 停止请求可能发生在capture_next_packet阻塞期间。
         *
         * 此时优先按正常停止处理，不把libpcap被中断后的返回值
         * 错误解释为采集故障。
         */
        if (context->stop_requested != 0) {
            break;
        }

        /*
        * 实时句柄使用非阻塞模式；当前没有可交付的数据包时，
        * capture_next_packet()返回EAGAIN。
        *
        * EAGAIN不是网卡故障，也不能增加数据包计数，
        * 因此进入有界等待，而不是立即重试。
        */
        if (live_capture && error_code == EAGAIN) {
            /*
             * 非阻塞读取当前没有现成数据。
             *
             * 使用poll等待描述符可读或应用层等待超时，
             * 避免立即重试形成忙轮询。
             */
            wait_status = CAPTURE_WAIT_STATUS_UNKNOWN;

            error_code = capture_wait_readable(
                capture,
                APP_CAPTURE_WAIT_TIMEOUT_MS,
                &wait_status
            );

            /*
             * SIGINT或SIGTERM可能在poll等待期间到达。
             *
             * 信号处理函数已经设置stop_requested，此时优先走
             * 正常停止路径，不把EINTR解释成采集故障。
             */
            if (context->stop_requested != 0) {
                break;
            }

            /*
             * poll也可能被项目没有接管的其他信号中断。
             *
             * 此时没有发现永久故障，可以从循环顶部重新检查状态。
             */
            if (error_code == EINTR) {
                continue;
            }

            if (error_code != 0) {
                (void)snprintf(
                    context->error_message,
                    sizeof(context->error_message),
                    "failed to wait for capture activity "
                    "on interface '%s': %s",
                    capture_source,
                    strerror(error_code)
                );

                context->active_capture = NULL;
                flow_table_cleanup(&flow_table);
                capture_close(&capture);
                return error_code;
            }

            /*
             * 成功返回只能产生READY或TIMEOUT。
             *
             * READY表示下一轮应重新尝试非阻塞读取；
             * TIMEOUT表示本轮没有流量，但仍要推进周期指标。
             */
            if (wait_status != CAPTURE_WAIT_STATUS_READY &&
                wait_status != CAPTURE_WAIT_STATUS_TIMEOUT) {
                (void)snprintf(
                    context->error_message,
                    sizeof(context->error_message),
                    "capture wait returned an unexpected status"
                );

                context->active_capture = NULL;
                flow_table_cleanup(&flow_table);
                capture_close(&capture);
                return EIO;
            }

            /*
             * 无论因为数据就绪还是正常超时返回，都检查单调时钟。
             *
             * 尚未到5秒时函数只返回0，不产生输出。
             */
            error_code = app_maybe_print_runtime_metrics(
                &runtime_metrics_schedule,
                &runtime_metrics_totals,
                &flow_table
            );

            if (error_code != 0) {
                (void)snprintf(
                    context->error_message,
                    sizeof(context->error_message),
                    "failed to update runtime metrics: %s",
                    strerror(error_code)
                );

                context->active_capture = NULL;
                flow_table_cleanup(&flow_table);
                capture_close(&capture);
                return error_code;
            }

            /*
             * READY时下一轮读取已经到达的数据；
             * TIMEOUT时下一轮先尝试读取，再按需重新进入poll。
             */
            continue;
        }

        if (error_code != 0) {
            const char *read_error =
                capture_get_error(capture);

            /*
             * capture_get_error返回的字符串依赖capture句柄，
             * 必须在capture_close之前复制。
             */
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to read %s '%s': %s",
                live_capture ? "interface" : "capture",
                capture_source,
                read_error != NULL &&
                    read_error[0] != '\0'
                    ? read_error
                    : "unknown libpcap error"
            );

            context->active_capture = NULL;
            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return error_code;
        }

        if (read_status == CAPTURE_READ_STATUS_END_OF_FILE) {
            break;
        }

        if (read_status != CAPTURE_READ_STATUS_PACKET) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "capture returned an unexpected read status"
            );

            context->active_capture = NULL;
            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return EIO;
        }

        total_packet_count += 1U;

        error_code = runtime_metrics_totals_add_packet(
            &runtime_metrics_totals,
            packet.captured_length,
            packet.wire_length
        );

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to accumulate runtime packet metrics: %s",
                strerror(error_code)
            );

            context->active_capture = NULL;
            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return error_code;
        }

        print_preview = previewed_packet_count < APP_CAPTURE_PREVIEW_LIMIT;

    /*
    * 必须在把当前数据包加入流表之前判断过期。
    *
    * 如果一条旧流已经空闲30秒，而当前包恰好与旧流五元组相同，
    * 先更新流表会刷新last_seen，错误地把两个会话合并。
    */
    if (live_capture) {
        packet_timestamp = (flow_timestamp_t){
            .seconds = packet.timestamp_seconds,
            .microseconds = packet.timestamp_microseconds
        };

        error_code = flow_expiration_schedule_observe(
            &expiration_schedule,
            &packet_timestamp,
            &expiration_scan_due,
            &expiration_cutoff
        );

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to update flow expiration schedule "
                "for packet %zu: %s",
                total_packet_count,
                strerror(error_code)
            );

            context->active_capture = NULL;
            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return error_code;
        }

        if (expiration_scan_due) {
            error_code = flow_table_expire_before(
                &flow_table,
                &expiration_cutoff,
                expired_flow_records,
                sizeof(expired_flow_records) /
                    sizeof(expired_flow_records[0]),
                &expired_flow_count
            );

            if (error_code != 0) {
                (void)snprintf(
                    context->error_message,
                    sizeof(context->error_message),
                    "failed to expire flows before packet %zu: %s",
                    total_packet_count,
                    strerror(error_code)
                );

                context->active_capture = NULL;
                flow_table_cleanup(&flow_table);
                capture_close(&capture);
                return error_code;
            }

            error_code = app_print_expired_flows(
                expired_flow_records,
                expired_flow_count,
                total_expired_flow_count
            );

            if (error_code != 0) {
                (void)snprintf(
                    context->error_message,
                    sizeof(context->error_message),
                    "failed to write expired flow output: %s",
                    strerror(error_code)
                );

                context->active_capture = NULL;
                flow_table_cleanup(&flow_table);
                capture_close(&capture);
                return error_code;
            }

            total_expired_flow_count += expired_flow_count;

            error_code =
                runtime_metrics_totals_add_expired_flows(
                    &runtime_metrics_totals,
                    (uint64_t)expired_flow_count
                );

            if (error_code != 0) {
                (void)snprintf(
                    context->error_message,
                    sizeof(context->error_message),
                    "failed to accumulate expired flow metrics: %s",
                    strerror(error_code)
                );

                context->active_capture = NULL;
                flow_table_cleanup(&flow_table);
                capture_close(&capture);
                return error_code;
            }
        }
    }

        /*
         * 必须在下一次capture_next_packet之前完成当前包的解析。
         *
         * 流表只保存解析出的数值结果，不保存packet.data。
         */
        error_code = app_process_packet(
            total_packet_count,
            &packet,
            print_preview,
            &flow_table,
            &packet_result
        );

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to process packet %zu: %s",
                total_packet_count,
                strerror(error_code)
            );

            context->active_capture = NULL;
            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return error_code;
        }

        error_code = runtime_metrics_totals_add_packet_result(
                &runtime_metrics_totals,
                packet_result
            );

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to accumulate packet result "
                "for packet %zu: %s",
                total_packet_count,
                strerror(error_code)
            );

            context->active_capture = NULL;
            flow_table_cleanup(&flow_table);
            capture_close(&capture);
            return error_code;
        }

        if (print_preview) {
            previewed_packet_count += 1U;
        }

        if (live_capture) {
            /*
             * 当前包已经完成解析、聚合和过期处理。
             *
             * 此时输出的active_flows反映处理完当前包后的流表状态。
             */
            error_code = app_maybe_print_runtime_metrics(
                &runtime_metrics_schedule,
                &runtime_metrics_totals,
                &flow_table
            );

            if (error_code != 0) {
                (void)snprintf(
                    context->error_message,
                    sizeof(context->error_message),
                    "failed to update runtime metrics: %s",
                    strerror(error_code)
                );

                context->active_capture = NULL;
                flow_table_cleanup(&flow_table);
                capture_close(&capture);
                return error_code;
            }
        }
    }

    /*
     * pcap_stats只支持实时采集，因此离线模式不调用统计接口。
     *
     * 查询必须发生在capture_close之前，因为底层pcap_t仍然是
     * pcap_stats和capture_get_error所依赖的对象。
     */
    if (live_capture) {
        statistics_error_code =
            capture_get_statistics(
                capture,
                &capture_statistics
            );

        if (statistics_error_code == 0) {
            capture_statistics_available = true;
        } else {
            /*
             * EIO表示libpcap查询失败，详细说明保存在capture句柄中。
             *
             * 其他错误通常表示项目内部调用条件不满足，使用strerror
             * 转换项目返回的errno风格错误码。
             */
            if (statistics_error_code == EIO) {
                native_statistics_error = capture_get_error(capture);
            }

            if (native_statistics_error != NULL &&
                native_statistics_error[0] != '\0') {
                (void)snprintf(
                    statistics_error_message,
                    sizeof(statistics_error_message),
                    "%s",
                    native_statistics_error
                );
            } else {
                (void)snprintf(
                    statistics_error_message,
                    sizeof(statistics_error_message),
                    "%s",
                    strerror(statistics_error_code)
                );
            }
        }
    }

    /*
     * 先撤销context中的借用，再关闭真正拥有的capture对象。
     */
    context->active_capture = NULL;
    capture_close(&capture);

    if (printf(
            "Total packets: %zu\n"
            "Previewed packets: %zu\n"
            "Processing results: "
            "complete=%" PRIu64 " "
            "truncated=%" PRIu64 " "
            "malformed=%" PRIu64 " "
            "unsupported=%" PRIu64 " "
            "flow_rejected=%" PRIu64 "\n",
            total_packet_count,
            previewed_packet_count,
            runtime_metrics_totals.complete_packet_count,
            runtime_metrics_totals.truncated_packet_count,
            runtime_metrics_totals.malformed_packet_count,
            runtime_metrics_totals.unsupported_packet_count,
            runtime_metrics_totals.flow_rejected_packet_count) < 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to write packet summary"
        );

        flow_table_cleanup(&flow_table);
        return EIO;
    }

    error_code = app_print_flow_table_probe_statistics(
        &flow_table
    );

    if (error_code != 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to report flow table probe statistics: %s",
            strerror(error_code)
        );

        flow_table_cleanup(&flow_table);
        return error_code;
    }

    /*
    * 离线模式没有启用周期过期，因此只在实时模式输出该指标。
    */
    if (live_capture &&
        printf(
            "Expired flows: %zu\n",
            total_expired_flow_count) < 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to write expired flow count"
        );

        flow_table_cleanup(&flow_table);
        return EIO;
    }

    /*
     * Total packets是应用实际从capture_next_packet取得的包数。
     *
     * 以下数字来自libpcap和操作系统。由于平台、抓包后端和BPF
     * 实现不同，它们不保证与Total packets完全相等。
     */
    if (live_capture) {
        if (capture_statistics_available) {
            error_code = printf(
                "Capture received packets: %" PRIu64 "\n"
                "Capture dropped packets: %" PRIu64 "\n"
                "Interface dropped packets: %" PRIu64 "\n",
                capture_statistics.received_packets,
                capture_statistics.dropped_packets,
                capture_statistics.interface_dropped_packets
            );
        } else {
            error_code = printf(
                "Capture statistics: unavailable (%s)\n",
                statistics_error_message[0] != '\0'
                    ? statistics_error_message
                    : "unknown statistics error"
            );
        }

        if (error_code < 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to write capture statistics"
            );

            flow_table_cleanup(&flow_table);
            return EIO;
        }
    }

    error_code = app_print_flow_summary(&flow_table);

    if (error_code != 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to write flow summary"
        );

        flow_table_cleanup(&flow_table);
        return error_code;
    }

    if (context->csv_output_path != NULL) {
        error_code = app_export_flow_table_csv(
            &flow_table,
            context->csv_output_path
        );

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to export CSV '%s': %s",
                context->csv_output_path,
                strerror(error_code)
            );

            flow_table_cleanup(&flow_table);
            return error_code;
        }

        if (printf(
                "CSV output: %s\n",
                context->csv_output_path) < 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "CSV was created, but confirmation output failed"
            );

            flow_table_cleanup(&flow_table);
            return EIO;
        }
    }

    /*
     * flow_table_cleanup只解除对flow_slots的借用，不会free数组。
     *
     * 函数返回后，flow_slots作为局部数组自然结束生命周期。
     */
    flow_table_cleanup(&flow_table);

    return 0;
}

int app_context_init(app_context_t *context)
{
    if (context == NULL) {
        return EINVAL;
    }

    /*
     * 复合字面量一次性设置完整的初始状态。
     */
    *context = (app_context_t){
        .command = APP_COMMAND_HELP,
        .program_name = "netflow-analyzer",
        .capture_path = NULL,
        .interface_name = NULL,
        .filter_expression = NULL,
        .packet_limit = 0U,
        .csv_output_path = NULL,
        .active_capture = NULL,
        .error_message = {0},
        .stop_requested = 0,
        .initialized = true
    };

    return 0;
}

int app_parse_arguments(app_context_t *context,
                        int argc,
                        char *argv[])
{
    const char *parsed_capture_path;
    const char *parsed_interface_name;
    const char *parsed_filter_expression;
    const char *parsed_csv_output_path;
    size_t parsed_packet_limit;

    int argument_index;
    int error_code;

    if (context == NULL ||
        !context->initialized ||
        argc < 1 ||
        argv == NULL ||
        argv[0] == NULL) {
        return EINVAL;
    }

    /*
     * 每次解析前恢复参数相关字段的默认值。
     */
    context->program_name = argv[0];
    context->command = APP_COMMAND_HELP;
    context->capture_path = NULL;
    context->interface_name = NULL;
    context->filter_expression = NULL;
    context->packet_limit = 0U;
    context->csv_output_path = NULL;
    context->error_message[0] = '\0';

    /*
     * 无参数时显示帮助。
     */
    if (argc == 1) {
        return 0;
    }

    /*
     * 帮助和版本不能与其他选项组合。
     */
    if (argc == 2 && argv[1] != NULL) {
        if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0) {
            context->command = APP_COMMAND_HELP;
            return 0;
        }

        if (strcmp(argv[1], "--version") == 0 ||
            strcmp(argv[1], "-V") == 0) {
            context->command = APP_COMMAND_VERSION;
            return 0;
        }

        return EINVAL;
    }

    parsed_capture_path = NULL;
    parsed_interface_name = NULL;
    parsed_filter_expression = NULL;
    parsed_csv_output_path = NULL;
    parsed_packet_limit = 0U;

    argument_index = 1;

    /*
     * 从argv[1]开始逐组选取“选项 + 参数”。
     *
     * 当前允许：
     *
     * --read input.pcap --csv flows.csv
     * --csv flows.csv --read input.pcap
     */
    while (argument_index < argc) {
        const char *option = argv[argument_index];
        const char *option_value;

        if (option == NULL) {
            return EINVAL;
        }

        /*
         * 所有当前业务选项后面都必须存在一个非空参数。
         */
        if (argument_index + 1 >= argc ||
            argv[argument_index + 1] == NULL ||
            argv[argument_index + 1][0] == '\0') {
            return EINVAL;
        }

        option_value = argv[argument_index + 1];

        if (strcmp(option, "--read") == 0 ||
            strcmp(option, "-r") == 0) {
            if (parsed_capture_path != NULL) {
                return EINVAL;
            }

            parsed_capture_path = option_value;
        } else if (strcmp(option, "--interface") == 0 ||
                   strcmp(option, "-i") == 0) {
            if (parsed_interface_name != NULL) {
                return EINVAL;
            }

            parsed_interface_name = option_value;
        } else if (strcmp(option, "--count") == 0 ||
                   strcmp(option, "-c") == 0) {
            /*
             * 0既表示尚未提供参数，也是非法的数据包数量。
             */
            if (parsed_packet_limit != 0U) {
                return EINVAL;
            }

            error_code = app_parse_positive_size(
                option_value,
                &parsed_packet_limit
            );

            if (error_code != 0) {
                return EINVAL;
            }
        } else if (strcmp(option, "--filter") == 0) {
            /*
             * 重复提供过滤器会导致最终使用哪一个表达式不明确。
             */
            if (parsed_filter_expression != NULL) {
                return EINVAL;
            }

            /*
             * option_value直接借用argv中的字符串。
             *
             * 循环开头已经拒绝NULL和空字符串，因此这里不需要再次
             * 检查内容是否为空。
             */
            parsed_filter_expression = option_value;
        } else if (strcmp(option, "--csv") == 0){
            if (parsed_csv_output_path != NULL) {
                return EINVAL;
            }

            parsed_csv_output_path = option_value;
        } else {
            return EINVAL;
        }

        /*
         * 本轮已经消费一个选项和它后面的值。
         */
        argument_index += 2;
    }

    /*
     * 一次只能选择一种数据来源。
     *
     * 同时指定离线文件和实时网卡会让命令语义不明确。
     */
    if (parsed_capture_path != NULL &&
        parsed_interface_name != NULL) {
        return EINVAL;
    }

    /*
     * 必须选择离线文件或实时网卡中的一种。
     */
    if (parsed_capture_path == NULL &&
        parsed_interface_name == NULL) {
        return EINVAL;
    }

    /*
     * --count只属于实时抓包。
     */
    if (parsed_capture_path != NULL &&
        parsed_packet_limit != 0U) {
        return EINVAL;
    }

    /*
     * 第一版只允许实时抓包使用BPF过滤器。
     *
     * capture层本身支持离线句柄，以便进行确定性单元测试；但CLI先
     * 保持较小的功能范围，后续可以再放宽离线过滤。
     */
    if (parsed_capture_path != NULL &&
        parsed_filter_expression != NULL) {
        return EINVAL;
    }

    /*
     * 实时抓包必须提供一个大于0的数据包数量。
     */
    if (parsed_interface_name != NULL &&
        parsed_packet_limit == 0U) {
        return EINVAL;
    }

    /*
     * 当前CSV导出只接在完整的离线聚合流程之后。
     *
     * 实时抓包还没有读取循环，因此暂不允许和--csv组合。
     */
    if (parsed_interface_name != NULL &&
        parsed_csv_output_path != NULL) {
        return EINVAL;
    }

    /*
     * 全部参数验证完成后才发布解析结果，避免留下半解析状态。
     */
    context->capture_path = parsed_capture_path;
    context->interface_name = parsed_interface_name;
    context->filter_expression = parsed_filter_expression;
    context->csv_output_path = parsed_csv_output_path;
    context->packet_limit = parsed_packet_limit;

    if (parsed_capture_path != NULL) {
        context->command = APP_COMMAND_READ_CAPTURE;
    } else {
        context->command = APP_COMMAND_CAPTURE_INTERFACE;
    }

    return 0;
}

int app_run(app_context_t *context)
{
    int error_code;

    if (context == NULL || !context->initialized) {
        return EINVAL;
    }

    /*
     * 每次运行前清空上一次错误。
     */
    context->error_message[0] = '\0';

    if (context->stop_requested) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "application stop has been requested"
        );

        return ECANCELED;
    }

    switch (context->command) {
    case APP_COMMAND_HELP:
        error_code = app_print_help(context->program_name);

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to write help information"
            );
        }

        return error_code;

    case APP_COMMAND_VERSION:
        if (printf("netflow-analyzer %s\n", app_version()) < 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to write version information"
            );

            return EIO;
        }

        return 0;

    case APP_COMMAND_READ_CAPTURE:
        if (context->capture_path == NULL || context->capture_path[0] == '\0') {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "capture path is missing"
            );

            return EINVAL;
        }

        /*
         * 第一版CLI不允许离线分析使用BPF过滤器。
         *
         * app_parse_arguments已经检查该规则；这里继续验证是为了防止
         * 调用者绕过参数解析，手动构造出不一致的上下文。
         */
        if (context->filter_expression != NULL) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "BPF filter is only supported for live capture"
            );

            return EINVAL;
        }

        if (context->csv_output_path != NULL && context->csv_output_path[0] == '\0') {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "CSV output path is empty"
            );

            return EINVAL;
        }

        return app_run_capture_analysis(context);

        case APP_COMMAND_CAPTURE_INTERFACE:
        if (context->interface_name == NULL ||
            context->interface_name[0] == '\0') {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "capture interface is missing"
            );

            return EINVAL;
        }

        if (context->packet_limit == 0U) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "live packet limit is missing"
            );

            return EINVAL;
        }

        /*
         * NULL表示不使用过滤器；非NULL表达式必须至少包含一个字符。
         */
        if (context->filter_expression != NULL &&
            context->filter_expression[0] == '\0') {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "BPF filter expression is empty"
            );

            return EINVAL;
        }

        return app_run_capture_analysis(context);

    default:
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "unknown application command"
        );

        return EINVAL;
    }
}

int app_request_stop(app_context_t *context)
{
    if (context == NULL || !context->initialized) {
        return EINVAL;
    }

    /*
     * 先发布停止标志，使读取循环恢复后能够识别正常停止。
     */
    context->stop_requested = 1;

    /*
     * active_capture为NULL时这是安全的无操作。
     *
     * 如果当前正在进行采集读取，则通知libpcap尽快返回。
     * 真正的关闭和资源清理由正常应用控制流完成。
     */
    capture_break_loop(context->active_capture);

    return 0;
}

void app_cleanup(app_context_t *context)
{
    if (context == NULL) {
        return;
    }

    /*
     * 当前没有需要close或free的资源。
     *
     * 清空结构体可以让后续误用更容易通过initialized检查发现。
     */
    *context = (app_context_t){0};
}

const char *app_version(void)
{
    /*
     * 字符串字面量具有静态存储期，会一直存在到进程结束。
     */
    return ANALYZER_VERSION;
}