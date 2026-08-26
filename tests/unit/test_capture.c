/*
 * _DEFAULT_SOURCE让glibc公开libpcap头文件所需的u_int、u_short和
 * u_char等BSD兼容类型。
 *
 * _POSIX_C_SOURCE 200809L让系统头文件公开mkstemp等POSIX.1-2008
 * 接口。
 *
 * 特性测试宏必须放在所有系统头文件之前。
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "analyzer/capture.h"
#include "analyzer/ethernet.h"
#include "analyzer/packet_info.h"

#include <errno.h>
#include <pcap/pcap.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#define TEST_CHECK(condition)                                      \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr,                                        \
                    "[FAIL] %s:%d: %s\n",                          \
                    __FILE__,                                      \
                    __LINE__,                                      \
                    #condition);                                   \
            return EXIT_FAILURE;                                   \
        }                                                          \
    } while (false)

/**
 * @brief 创建一个用于测试的Ethernet PCAP文件。
 *
 * packet_data为NULL且captured_length为0时，只写入PCAP文件头，
 * 创建一个没有数据包的空PCAP。
 *
 * packet_data非NULL时，写入一条具有固定时间戳的数据包。
 *
 * path_template必须以六个X结尾，供mkstemp替换为唯一文件名。
 */
static int create_ethernet_pcap(char *path_template,
                                const uint8_t *packet_data,
                                uint32_t captured_length,
                                uint32_t wire_length)
{
    int file_descriptor;
    int close_error;

    pcap_t *dead_handle;
    pcap_dumper_t *dumper;

    struct pcap_pkthdr packet_header = {
        .ts = {
            .tv_sec = 1700000000,
            .tv_usec = 123456
        },
        .caplen = 0U,
        .len = 0U
    };

    if (path_template == NULL ||
        (packet_data == NULL && captured_length > 0U) ||
        captured_length > wire_length) {
        return EINVAL;
    }

    file_descriptor = mkstemp(path_template);

    if (file_descriptor == -1) {
        return errno;
    }

    if (close(file_descriptor) != 0) {
        close_error = errno;
        (void)remove(path_template);

        return close_error;
    }

    /*
     * 创建一个描述Ethernet链路类型和最大捕获长度的虚拟句柄。
     */
    dead_handle = pcap_open_dead(DLT_EN10MB, 65535);

    if (dead_handle == NULL) {
        (void)remove(path_template);
        return ENOMEM;
    }

    dumper = pcap_dump_open(dead_handle, path_template);

    if (dumper == NULL) {
        fprintf(stderr,
                "Failed to create test PCAP: %s\n",
                pcap_geterr(dead_handle));

        pcap_close(dead_handle);
        (void)remove(path_template);

        return EIO;
    }

    if (packet_data != NULL) {
        packet_header.caplen = captured_length;
        packet_header.len = wire_length;

        /*
         * pcap_dump的第一个参数沿用了libpcap回调函数的u_char *
         * 参数形式。按照libpcap接口约定，把dumper转换后传入。
         *
         * 第二个参数是数据包元信息，第三个参数是实际字节。
         */
        pcap_dump((u_char *)dumper,
                  &packet_header,
                  (const u_char *)packet_data);
    }

    pcap_dump_close(dumper);
    pcap_close(dead_handle);

    return 0;
}

/**
 * @brief 验证采集模块拒绝无效参数和不存在的文件。
 */
static int test_capture_open_error_handling(void)
{
    char error_buffer[CAPTURE_ERROR_BUFFER_SIZE] = {0};
    capture_t *capture = NULL;
    capture_link_type_t link_type = CAPTURE_LINK_TYPE_UNKNOWN;

    TEST_CHECK(
        capture_open_offline(NULL,
                             &capture,
                             error_buffer,
                             sizeof(error_buffer)) == EINVAL
    );

    TEST_CHECK(capture == NULL);
    TEST_CHECK(error_buffer[0] != '\0');

    TEST_CHECK(
        capture_open_offline("",
                             &capture,
                             error_buffer,
                             sizeof(error_buffer)) == EINVAL
    );

    TEST_CHECK(capture == NULL);

    /*
     * /proc/self下不存在这个普通抓包文件，并且测试不会创建它。
     */
    TEST_CHECK(
        capture_open_offline(
            "/proc/self/netflow-analyzer-missing.pcap",
            &capture,
            error_buffer,
            sizeof(error_buffer)
        ) == EIO
    );

    TEST_CHECK(capture == NULL);
    TEST_CHECK(error_buffer[0] != '\0');

    /*
     * 错误缓冲区和长度没有成对提供。
     */
    TEST_CHECK(
        capture_open_offline(
            "/proc/self/netflow-analyzer-missing.pcap",
            &capture,
            NULL,
            CAPTURE_ERROR_BUFFER_SIZE
        ) == EINVAL
    );

    TEST_CHECK(
        capture_get_link_type(NULL, &link_type) == EINVAL
    );

    TEST_CHECK(capture_get_error(NULL) != NULL);

    /*
     * 关闭NULL是合法空操作，不能导致崩溃。
     */
    capture_close(NULL);
    capture_close(&capture);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证实时抓包接口拒绝无效参数和不存在的网卡。
 *
 * 本测试不要求root权限，因为不会成功打开真实网卡。
 */
static int test_capture_open_live_error_handling(void)
{
    static const char missing_interface[] =
        "netflow-analyzer-interface-that-does-not-exist";

    char error_buffer[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    capture_t *capture = NULL;

    TEST_CHECK(
        capture_open_live(
            NULL,
            CAPTURE_DEFAULT_SNAPSHOT_LENGTH,
            false,
            CAPTURE_DEFAULT_READ_TIMEOUT_MS,
            &capture,
            error_buffer,
            sizeof(error_buffer)
        ) == EINVAL
    );

    TEST_CHECK(capture == NULL);
    TEST_CHECK(error_buffer[0] != '\0');

    TEST_CHECK(
        capture_open_live(
            "",
            CAPTURE_DEFAULT_SNAPSHOT_LENGTH,
            false,
            CAPTURE_DEFAULT_READ_TIMEOUT_MS,
            &capture,
            error_buffer,
            sizeof(error_buffer)
        ) == EINVAL
    );

    TEST_CHECK(capture == NULL);

    /*
     * 快照长度必须大于0。
     */
    TEST_CHECK(
        capture_open_live(
            "lo",
            0,
            false,
            CAPTURE_DEFAULT_READ_TIMEOUT_MS,
            &capture,
            error_buffer,
            sizeof(error_buffer)
        ) == EINVAL
    );

    /*
     * 正数超时保证上层以后能够定期检查停止请求。
     */
    TEST_CHECK(
        capture_open_live(
            "lo",
            CAPTURE_DEFAULT_SNAPSHOT_LENGTH,
            false,
            0,
            &capture,
            error_buffer,
            sizeof(error_buffer)
        ) == EINVAL
    );

    /*
     * capture输出参数不能为空。
     */
    TEST_CHECK(
        capture_open_live(
            "lo",
            CAPTURE_DEFAULT_SNAPSHOT_LENGTH,
            false,
            CAPTURE_DEFAULT_READ_TIMEOUT_MS,
            NULL,
            error_buffer,
            sizeof(error_buffer)
        ) == EINVAL
    );

    /*
     * 错误缓冲区和容量必须同时提供或同时省略。
     */
    TEST_CHECK(
        capture_open_live(
            "lo",
            CAPTURE_DEFAULT_SNAPSHOT_LENGTH,
            false,
            CAPTURE_DEFAULT_READ_TIMEOUT_MS,
            &capture,
            NULL,
            CAPTURE_ERROR_BUFFER_SIZE
        ) == EINVAL
    );

    /*
     * 一个足够特殊且确定不存在的接口名称，应由libpcap拒绝。
     */
    TEST_CHECK(
        capture_open_live(
            missing_interface,
            CAPTURE_DEFAULT_SNAPSHOT_LENGTH,
            false,
            CAPTURE_DEFAULT_READ_TIMEOUT_MS,
            &capture,
            error_buffer,
            sizeof(error_buffer)
        ) == EIO
    );

    TEST_CHECK(capture == NULL);
    TEST_CHECK(error_buffer[0] != '\0');

    /*
     * 失败后capture仍然为NULL，关闭是安全空操作。
     */
    capture_close(&capture);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证能够打开合法的空Ethernet PCAP并查询链路类型。
 */
static int test_open_empty_ethernet_capture(void)
{
    char pcap_path[] = "/tmp/netflow-analyzer-capture-XXXXXX";
    char error_buffer[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    capture_t *capture = NULL;
    capture_link_type_t link_type = CAPTURE_LINK_TYPE_UNKNOWN;

    int create_error;
    int open_error;
    int link_type_error;
    int null_output_error;
    int remove_error;
    bool error_string_available;

    create_error = create_ethernet_pcap(pcap_path, NULL, 0U, 0U);

    TEST_CHECK(create_error == 0);

    open_error = capture_open_offline(
        pcap_path,
        &capture,
        error_buffer,
        sizeof(error_buffer)
    );

    /*
     * 只有打开成功后才能调用需要有效句柄的查询函数。
     */
    if (open_error == 0) {
        link_type_error =
            capture_get_link_type(capture, &link_type);

        null_output_error =
            capture_get_link_type(capture, NULL);

        error_string_available =
            capture_get_error(capture) != NULL;
    } else {
        link_type_error = EINVAL;
        null_output_error = EINVAL;
        error_string_available = false;
    }

    /*
     * 在执行断言前先释放资源。
     *
     * 即使后面的验证失败，测试创建的句柄和临时文件也已经清理。
     */
    capture_close(&capture);
    remove_error = remove(pcap_path);

    TEST_CHECK(open_error == 0);
    TEST_CHECK(error_buffer[0] == '\0');
    TEST_CHECK(link_type_error == 0);
    TEST_CHECK(link_type == CAPTURE_LINK_TYPE_ETHERNET);
    TEST_CHECK(null_output_error == EINVAL);
    TEST_CHECK(error_string_available);
    TEST_CHECK(capture == NULL);
    TEST_CHECK(remove_error == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证能够读取一条数据包，并在下一次读取时报告文件结束。
 */
static int test_capture_packet_reading(void)
{
    /*
     * 这18字节模拟：
     *
     * 6字节目标MAC；
     * 6字节源MAC；
     * 2字节EtherType；
     * 4字节负载。
     *
     * 测试先验证capture模块原样返回字节，再在下一次读取前把
     * libpcap返回的数据交给packet_info和Ethernet解析器。
     */
    const uint8_t expected_data[] = {
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U,
        0x66U, 0x77U, 0x88U, 0x99U, 0xAAU, 0xBBU,
        0x08U, 0x00U,
        0xDEU, 0xADU, 0xBEU, 0xEFU
    };

    char pcap_path[] =
        "/tmp/netflow-analyzer-packet-XXXXXX";

    char error_buffer[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    /*
     * received_data属于测试自身。
     *
     * 必须在下一次capture_next_packet之前复制数据，因为libpcap
     * 返回的数据地址在下一次读取后可能失效。
     */
    uint8_t received_data[sizeof(expected_data)] = {0U};

    capture_t *capture = NULL;

    capture_packet_view_t packet = {
        .timestamp_seconds = 0,
        .timestamp_microseconds = 0,
        .captured_length = 0U,
        .wire_length = 0U,
        .data = NULL
    };

    /*
     * parsed_packet只保存复制后的数值和MAC字节，不保存packet.data。
     *
     * 因此即使后面再次调用capture_next_packet，解析结果仍然有效。
     */
    packet_info_t parsed_packet = {0};

    capture_read_status_t first_status =
        CAPTURE_READ_STATUS_UNKNOWN;

    capture_read_status_t second_status =
        CAPTURE_READ_STATUS_UNKNOWN;

    int create_error;
    int open_error;
    int null_packet_error = EINVAL;
    int null_status_error = EINVAL;
    int first_read_error = EINVAL;
    int second_read_error = EINVAL;
    int remove_error;
    int packet_info_error = EINVAL;
    int ethernet_error = EINVAL;

    int64_t timestamp_seconds = 0;
    int32_t timestamp_microseconds = 0;
    uint32_t captured_length = 0U;
    uint32_t wire_length = 0U;

    bool data_copied = false;

    create_error = create_ethernet_pcap(
        pcap_path,
        expected_data,
        (uint32_t)sizeof(expected_data),
        UINT32_C(60)
    );

    TEST_CHECK(create_error == 0);

    open_error = capture_open_offline(
        pcap_path,
        &capture,
        error_buffer,
        sizeof(error_buffer)
    );

    if (open_error == 0) {
        /*
         * 无效输出参数必须在调用pcap_next_ex之前被拒绝，
         * 因此不能消耗文件中的第一条数据包。
         */
        null_packet_error = capture_next_packet(
            capture,
            NULL,
            &first_status
        );

        null_status_error = capture_next_packet(
            capture,
            &packet,
            NULL
        );

        first_read_error = capture_next_packet(
            capture,
            &packet,
            &first_status
        );

        if (first_read_error == 0 && first_status == CAPTURE_READ_STATUS_PACKET) {
            timestamp_seconds = packet.timestamp_seconds;
            timestamp_microseconds = packet.timestamp_microseconds;
            captured_length = packet.captured_length;
            wire_length = packet.wire_length;

            /*
             * 只有确认指针有效且长度与目标数组一致后才能复制。
             */
            if (packet.data != NULL &&
                packet.captured_length ==
                    (uint32_t)sizeof(received_data)) {
                memcpy(received_data,
                       packet.data,
                       sizeof(received_data));

                data_copied = true;
            }

            /*
             * libpcap返回的数据地址只保证在下一次读取前有效，
             * 所以必须在这里完成当前包的Ethernet解析。
             */
            packet_info_error = packet_info_init(
                &parsed_packet,
                packet.timestamp_seconds,
                packet.timestamp_microseconds,
                packet.captured_length,
                packet.wire_length
            );

            if (packet_info_error == 0) {
                ethernet_error = ethernet_parse(
                    packet.data,
                    (size_t)packet.captured_length,
                    &parsed_packet
                );
        }

        /*
         * 文件只有一条数据包，所以第二次读取应该到达EOF。
         *
         * 调用之后不再访问第一次返回的packet.data。
         */
        second_read_error = capture_next_packet(
            capture,
            &packet,
            &second_status
        );
    }
}

    /*
     * 在执行断言前完成资源清理。
     */
    capture_close(&capture);
    remove_error = remove(pcap_path);

    TEST_CHECK(open_error == 0);
    TEST_CHECK(error_buffer[0] == '\0');

    TEST_CHECK(null_packet_error == EINVAL);
    TEST_CHECK(null_status_error == EINVAL);

    TEST_CHECK(first_read_error == 0);
    TEST_CHECK(first_status == CAPTURE_READ_STATUS_PACKET);

    TEST_CHECK(timestamp_seconds == INT64_C(1700000000));
    TEST_CHECK(timestamp_microseconds == INT32_C(123456));
    TEST_CHECK(
        captured_length == (uint32_t)sizeof(expected_data)
    );
    TEST_CHECK(wire_length == UINT32_C(60));

    TEST_CHECK(data_copied);

    TEST_CHECK(
        memcmp(received_data,
               expected_data,
               sizeof(expected_data)) == 0
    );

    /*
     * 验证真实链路：
     *
     * 临时PCAP → libpcap → capture_packet_view_t
     * → packet_info_t → Ethernet解析结果。
     */
    TEST_CHECK(packet_info_error == 0);
    TEST_CHECK(ethernet_error == 0);
    TEST_CHECK(parsed_packet.initialized);
    TEST_CHECK(parsed_packet.has_ethernet);

    TEST_CHECK(
        parsed_packet.timestamp_seconds ==
            INT64_C(1700000000)
    );

    TEST_CHECK(
        parsed_packet.timestamp_microseconds ==
            INT32_C(123456)
    );

    TEST_CHECK(
        parsed_packet.captured_length ==
            (uint32_t)sizeof(expected_data)
    );

    TEST_CHECK(
        parsed_packet.wire_length ==
            UINT32_C(60)
    );

    TEST_CHECK(
        memcmp(
            parsed_packet.destination_mac,
            expected_data,
            PACKET_MAC_ADDRESS_LENGTH
        ) == 0
    );

    TEST_CHECK(
        memcmp(
            parsed_packet.source_mac,
            expected_data +
                PACKET_MAC_ADDRESS_LENGTH,
            PACKET_MAC_ADDRESS_LENGTH
        ) == 0
    );

    TEST_CHECK(
        parsed_packet.ether_type ==
            ETHERNET_TYPE_IPV4
    );

    TEST_CHECK(
        parsed_packet.network_payload_offset ==
            ETHERNET_HEADER_LENGTH
    );

    TEST_CHECK(
        parsed_packet.network_payload_length ==
            sizeof(expected_data) -
                ETHERNET_HEADER_LENGTH
    );

    /*
     * PCAP中只保存了18字节，但wirelen记录为60字节，
     * 因此捕获结果应该被识别为snaplen截断。
     *
     * Ethernet头部仍然完整，所以Ethernet解析可以成功。
     */
    TEST_CHECK(
        packet_info_capture_is_truncated(
            &parsed_packet
        )
    );    

    TEST_CHECK(second_read_error == 0);
    TEST_CHECK(
        second_status ==
            CAPTURE_READ_STATUS_END_OF_FILE
    );

    TEST_CHECK(capture == NULL);
    TEST_CHECK(remove_error == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证采集读取可以在不关闭句柄的情况下被中断。
 */
static int test_capture_break_loop(void)
{
    const uint8_t packet_data[] = {
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U,
        0x66U, 0x77U, 0x88U, 0x99U, 0xAAU, 0xBBU,
        0x08U, 0x00U
    };

    char pcap_path[] =
        "/tmp/netflow-analyzer-break-loop-XXXXXX";

    char error_buffer[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    capture_t *capture = NULL;

    capture_packet_view_t packet = {
        .timestamp_seconds = 0,
        .timestamp_microseconds = 0,
        .captured_length = 0U,
        .wire_length = 0U,
        .data = NULL
    };

    capture_read_status_t read_status =
        CAPTURE_READ_STATUS_UNKNOWN;

    int create_error;
    int open_error;
    int read_error = EINVAL;
    int remove_error;

    create_error = create_ethernet_pcap(
        pcap_path,
        packet_data,
        (uint32_t)sizeof(packet_data),
        (uint32_t)sizeof(packet_data)
    );

    TEST_CHECK(create_error == 0);

    open_error = capture_open_offline(
        pcap_path,
        &capture,
        error_buffer,
        sizeof(error_buffer)
    );

    /*
     * NULL应该是安全的无操作。
     */
    capture_break_loop(NULL);

    if (open_error == 0) {
        /*
         * 文件中实际存在一个数据包。
         *
         * 先请求中断后，下一次读取不应返回该数据包，而应沿用
         * capture_next_packet现有的正常结束状态。
         */
        capture_break_loop(capture);

        read_error = capture_next_packet(
            capture,
            &packet,
            &read_status
        );
    }

    capture_close(&capture);
    remove_error = remove(pcap_path);

    TEST_CHECK(open_error == 0);
    TEST_CHECK(error_buffer[0] == '\0');
    TEST_CHECK(read_error == 0);
    TEST_CHECK(
        read_status ==
            CAPTURE_READ_STATUS_END_OF_FILE
    );
    TEST_CHECK(capture == NULL);
    TEST_CHECK(remove_error == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证BPF过滤参数、编译错误以及实际包过滤语义。
 *
 * 测试使用离线PCAP，因此不依赖真实网卡或root权限。
 */
static int test_capture_filtering(void)
{
    /*
     * 一条完整的Ethernet/IPv4/ICMP echo request：
     *
     * Ethernet头：14字节；
     * IPv4头：20字节，protocol字段为1，即ICMP；
     * ICMP头：8字节。
     *
     * BPF只需要识别链路类型、IPv4头和protocol字段，本测试不要求
     * 协议解析器验证IP或ICMP校验和。
     */
    const uint8_t icmp_packet_data[] = {
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U,
        0x66U, 0x77U, 0x88U, 0x99U, 0xAAU, 0xBBU,
        0x08U, 0x00U,

        0x45U, 0x00U, 0x00U, 0x1CU,
        0x00U, 0x01U, 0x00U, 0x00U,
        0x40U, 0x01U, 0x00U, 0x00U,
        0x7FU, 0x00U, 0x00U, 0x01U,
        0x7FU, 0x00U, 0x00U, 0x01U,

        0x08U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x01U, 0x00U, 0x01U
    };

    char pcap_path[] =
        "/tmp/netflow-analyzer-filter-XXXXXX";

    char error_buffer[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    capture_t *capture = NULL;
    capture_packet_view_t packet = {0};

    capture_read_status_t matching_status =
        CAPTURE_READ_STATUS_UNKNOWN;

    capture_read_status_t nonmatching_status =
        CAPTURE_READ_STATUS_UNKNOWN;

    int null_capture_error;
    int create_error;
    int first_open_error;
    int missing_buffer_error = -1;
    int missing_size_error = -1;
    int null_expression_error = -1;
    int empty_expression_error = -1;
    int invalid_expression_error = -1;
    int matching_filter_error = -1;
    int matching_read_error = -1;
    int second_open_error;
    int nonmatching_filter_error = -1;
    int nonmatching_read_error = -1;
    int remove_error;

    bool null_capture_message;
    bool null_expression_message = false;
    bool empty_expression_message = false;
    bool invalid_expression_message = false;
    bool matching_filter_cleared = false;
    bool matching_packet_equal = false;

    null_capture_error = capture_set_filter(
        NULL,
        "icmp",
        error_buffer,
        sizeof(error_buffer)
    );

    null_capture_message = error_buffer[0] != '\0';

    create_error = create_ethernet_pcap(
        pcap_path,
        icmp_packet_data,
        (uint32_t)sizeof(icmp_packet_data),
        (uint32_t)sizeof(icmp_packet_data)
    );

    TEST_CHECK(create_error == 0);

    first_open_error = capture_open_offline(
        pcap_path,
        &capture,
        error_buffer,
        sizeof(error_buffer)
    );

    if (first_open_error == 0) {
        /*
         * 错误缓冲区和容量必须同时提供或同时省略。
         */
        missing_buffer_error = capture_set_filter(
            capture,
            "icmp",
            NULL,
            CAPTURE_ERROR_BUFFER_SIZE
        );

        missing_size_error = capture_set_filter(
            capture,
            "icmp",
            error_buffer,
            0U
        );

        null_expression_error = capture_set_filter(
            capture,
            NULL,
            error_buffer,
            sizeof(error_buffer)
        );

        null_expression_message = error_buffer[0] != '\0';

        empty_expression_error = capture_set_filter(
            capture,
            "",
            error_buffer,
            sizeof(error_buffer)
        );

        empty_expression_message = error_buffer[0] != '\0';

        /*
         * 该表达式具有确定的语法错误，应该在pcap_compile阶段失败。
         */
        invalid_expression_error = capture_set_filter(
            capture,
            "icmp and and tcp",
            error_buffer,
            sizeof(error_buffer)
        );

        invalid_expression_message = error_buffer[0] != '\0';

        /*
         * 编译失败不能破坏capture。紧接着安装合法过滤器应当成功。
         */
        matching_filter_error = capture_set_filter(
            capture,
            "icmp",
            error_buffer,
            sizeof(error_buffer)
        );

        matching_filter_cleared = error_buffer[0] == '\0';

        if (matching_filter_error == 0) {
            matching_read_error = capture_next_packet(
                capture,
                &packet,
                &matching_status
            );

            if (matching_read_error == 0 &&
                matching_status == CAPTURE_READ_STATUS_PACKET &&
                packet.data != NULL &&
                packet.captured_length ==
                    (uint32_t)sizeof(icmp_packet_data)) {
                matching_packet_equal =
                    memcmp(
                        packet.data,
                        icmp_packet_data,
                        sizeof(icmp_packet_data)
                    ) == 0;
            }
        }
    }

    capture_close(&capture);

    /*
     * 重新打开同一个PCAP，并安装不匹配的TCP过滤器。
     *
     * 文件中只有ICMP包，因此读取结果应该直接到达EOF。
     * 此处同时验证NULL和0是合法的“省略错误缓冲区”组合。
     */
    second_open_error = capture_open_offline(
        pcap_path,
        &capture,
        error_buffer,
        sizeof(error_buffer)
    );

    if (second_open_error == 0) {
        nonmatching_filter_error = capture_set_filter(
            capture,
            "tcp",
            NULL,
            0U
        );

        if (nonmatching_filter_error == 0) {
            nonmatching_read_error = capture_next_packet(
                capture,
                &packet,
                &nonmatching_status
            );
        }
    }

    capture_close(&capture);
    remove_error = remove(pcap_path);

    TEST_CHECK(null_capture_error == EINVAL);
    TEST_CHECK(null_capture_message);

    TEST_CHECK(first_open_error == 0);
    TEST_CHECK(missing_buffer_error == EINVAL);
    TEST_CHECK(missing_size_error == EINVAL);

    TEST_CHECK(null_expression_error == EINVAL);
    TEST_CHECK(null_expression_message);

    TEST_CHECK(empty_expression_error == EINVAL);
    TEST_CHECK(empty_expression_message);

    TEST_CHECK(invalid_expression_error == EIO);
    TEST_CHECK(invalid_expression_message);

    TEST_CHECK(matching_filter_error == 0);
    TEST_CHECK(matching_filter_cleared);
    TEST_CHECK(matching_read_error == 0);
    TEST_CHECK(
        matching_status == CAPTURE_READ_STATUS_PACKET
    );
    TEST_CHECK(matching_packet_equal);

    TEST_CHECK(second_open_error == 0);
    TEST_CHECK(nonmatching_filter_error == 0);
    TEST_CHECK(nonmatching_read_error == 0);
    TEST_CHECK(
        nonmatching_status ==
            CAPTURE_READ_STATUS_END_OF_FILE
    );

    TEST_CHECK(capture == NULL);
    TEST_CHECK(remove_error == 0);

    return EXIT_SUCCESS;
}

int main(void)
{
    if (test_capture_open_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] capture open error handling\n");

    if (test_open_empty_ethernet_capture() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] open empty Ethernet capture\n");

    if (test_capture_packet_reading() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }   

    printf("[PASS] capture packet reading\n");

    if (test_capture_filtering() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] capture filtering\n");

    if (test_capture_open_live_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] live capture open error handling\n");

        if (test_capture_break_loop() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] capture break loop\n");

    return EXIT_SUCCESS;
}