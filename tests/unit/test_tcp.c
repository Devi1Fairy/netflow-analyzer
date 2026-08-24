#include "analyzer/ethernet.h"
#include "analyzer/ipv4.h"
#include "analyzer/packet_info.h"
#include "analyzer/tcp.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 在Debug和Release构建中都有效的测试检查宏。
 */
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
 * @brief 初始化结果对象，并依次完成Ethernet和IPv4解析。
 */
static int prepare_tcp_test_packet(
    const uint8_t *frame,
    size_t frame_length,
    uint32_t wire_length,
    packet_info_t *packet_info)
{
    int error_code;

    if (frame_length > UINT32_MAX) {
        return EOVERFLOW;
    }

    error_code = packet_info_init(
        packet_info,
        INT64_C(100),
        INT32_C(200),
        (uint32_t)frame_length,
        wire_length
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = ethernet_parse(
        frame,
        frame_length,
        packet_info
    );

    if (error_code != 0) {
        return error_code;
    }

    return ipv4_parse(
        frame,
        frame_length,
        packet_info
    );
}

/**
 * @brief 验证不带选项和负载的普通TCP SYN报文。
 */
static int test_tcp_basic_syn(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
        0x08, 0x00,

        /*
         * IPv4头部：总长度40字节，协议为TCP。
         */
        0x45, 0x00,
        0x00, 0x28,
        0x12, 0x34,
        0x40, 0x00,
        0x40, 0x06,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x0a,
        0x0a, 0x00, 0x00, 0x05,

        /*
         * TCP头部：
         *
         * source_port=12345；
         * destination_port=80；
         * sequence=0x01020304；
         * acknowledgment=0；
         * header_length=20；
         * flags=SYN；
         * window=64240。
         */
        0x30, 0x39,
        0x00, 0x50,
        0x01, 0x02, 0x03, 0x04,
        0x00, 0x00, 0x00, 0x00,
        0x50, 0x02,
        0xfa, 0xf0,
        0x12, 0x34,
        0x00, 0x00
    };

    packet_info_t packet_info;
    byte_cursor_t payload;

    TEST_CHECK(
        prepare_tcp_test_packet(
            frame,
            sizeof(frame),
            (uint32_t)sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        tcp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_tcp);
    TEST_CHECK(
        packet_info.tcp_source_port ==
            UINT16_C(12345)
    );
    TEST_CHECK(
        packet_info.tcp_destination_port ==
            UINT16_C(80)
    );
    TEST_CHECK(
        packet_info.tcp_sequence_number ==
            UINT32_C(0x01020304)
    );
    TEST_CHECK(
        packet_info.tcp_acknowledgment_number ==
            UINT32_C(0)
    );
    TEST_CHECK(
        packet_info.tcp_header_length ==
            UINT8_C(20)
    );
    TEST_CHECK(
        packet_info.tcp_flags == TCP_FLAG_SYN
    );
    TEST_CHECK(
        packet_info.tcp_window_size ==
            UINT16_C(64240)
    );
    TEST_CHECK(
        packet_info.tcp_checksum ==
            UINT16_C(0x1234)
    );
    TEST_CHECK(
        packet_info.tcp_urgent_pointer ==
            UINT16_C(0)
    );
    TEST_CHECK(packet_info.tcp_payload_length == 0U);
    TEST_CHECK(!packet_info.tcp_payload_truncated);

    TEST_CHECK(
        tcp_payload_view(
            frame,
            sizeof(frame),
            &packet_info,
            &payload
        ) == 0
    );

    TEST_CHECK(byte_cursor_remaining(&payload) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证TCP选项、ACK/PSH标志和应用层负载。
 */
static int test_tcp_options_and_payload(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4总长度47字节：
         * 20字节IPv4头 + 24字节TCP头 + 3字节负载。
         */
        0x45, 0x00,
        0x00, 0x2f,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x06,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x01,
        0xc0, 0xa8, 0x01, 0x02,

        /* TCP基础头部。 */
        0x01, 0xbb,
        0xc3, 0x50,
        0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88,

        /*
         * Data Offset=6，即24字节TCP头部。
         * 标志为PSH和ACK。
         */
        0x60, 0x18,
        0x10, 0x00,
        0xab, 0xcd,
        0x00, 0x00,

        /* 4字节TCP选项。 */
        0x01, 0x01, 0x01, 0x01,

        /* 3字节TCP负载："GET"。 */
        0x47, 0x45, 0x54
    };

    packet_info_t packet_info;
    byte_cursor_t payload;
    uint8_t payload_bytes[3];

    TEST_CHECK(
        prepare_tcp_test_packet(
            frame,
            sizeof(frame),
            (uint32_t)sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        tcp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        packet_info.tcp_source_port ==
            UINT16_C(443)
    );
    TEST_CHECK(
        packet_info.tcp_destination_port ==
            UINT16_C(50000)
    );
    TEST_CHECK(
        packet_info.tcp_header_length ==
            UINT8_C(24)
    );
    TEST_CHECK(
        packet_info.tcp_flags ==
            (TCP_FLAG_PSH | TCP_FLAG_ACK)
    );
    TEST_CHECK(packet_info.tcp_payload_length == 3U);

    TEST_CHECK(
        tcp_payload_view(
            frame,
            sizeof(frame),
            &packet_info,
            &payload
        ) == 0
    );

    TEST_CHECK(
        byte_cursor_read_bytes(
            &payload,
            payload_bytes,
            sizeof(payload_bytes)
        ) == 0
    );

    TEST_CHECK(payload_bytes[0] == UINT8_C('G'));
    TEST_CHECK(payload_bytes[1] == UINT8_C('E'));
    TEST_CHECK(payload_bytes[2] == UINT8_C('T'));

    return EXIT_SUCCESS;
}

/**
 * @brief 验证TCP负载只捕获了一部分时仍可解析完整头部。
 */
static int test_tcp_payload_truncation(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4声明总长度45字节，
         * 但这里只捕获20字节TCP头和2字节负载。
         */
        0x45, 0x00,
        0x00, 0x2d,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x06,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        /* TCP头部。 */
        0x03, 0xe8,
        0x07, 0xd0,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x02,
        0x50, 0x10,
        0x20, 0x00,
        0x12, 0x34,
        0x00, 0x00,

        /* 只捕获到2字节负载。 */
        0xaa, 0xbb
    };

    packet_info_t packet_info;

    /*
     * wire_length对应完整45字节IPv4数据报加14字节Ethernet头。
     */
    TEST_CHECK(
        prepare_tcp_test_packet(
            frame,
            sizeof(frame),
            UINT32_C(59),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.ipv4_payload_truncated);

    TEST_CHECK(
        tcp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_tcp);
    TEST_CHECK(packet_info.tcp_payload_length == 2U);
    TEST_CHECK(packet_info.tcp_payload_truncated);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证TCP头部截断和非法Data Offset。
 */
static int test_tcp_error_handling(void)
{
    static const uint8_t truncated_frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4声明存在20字节TCP头，
         * 实际只捕获了19字节。
         */
        0x45, 0x00,
        0x00, 0x28,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x06,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        0x03, 0xe8,
        0x07, 0xd0,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x50, 0x02,
        0x20, 0x00,
        0x12, 0x34,
        0x00
    };

    static const uint8_t malformed_frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /* 普通IPv4头部。 */
        0x45, 0x00,
        0x00, 0x28,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x06,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        /*
         * TCP中Data Offset=4，声称头部只有16字节，
         * 小于TCP规定的最小20字节。
         */
        0x03, 0xe8,
        0x07, 0xd0,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x40, 0x02,
        0x20, 0x00,
        0x12, 0x34,
        0x00, 0x00
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_tcp_test_packet(
            truncated_frame,
            sizeof(truncated_frame),
            UINT32_C(54),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        tcp_parse(
            truncated_frame,
            sizeof(truncated_frame),
            &packet_info
        ) == ENODATA
    );

    TEST_CHECK(!packet_info.has_tcp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_TRUNCATED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_TCP
    );

    TEST_CHECK(
        prepare_tcp_test_packet(
            malformed_frame,
            sizeof(malformed_frame),
            (uint32_t)sizeof(malformed_frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        tcp_parse(
            malformed_frame,
            sizeof(malformed_frame),
            &packet_info
        ) == EBADMSG
    );

    TEST_CHECK(!packet_info.has_tcp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_MALFORMED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_TCP
    );

    return EXIT_SUCCESS;
}

/**
 * @brief TCP解析器单元测试入口。
 */
int main(void)
{
    if (test_tcp_basic_syn() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP basic SYN\n");

    if (test_tcp_options_and_payload() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP options and payload\n");

    if (test_tcp_payload_truncation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP payload truncation\n");

    if (test_tcp_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP error handling\n");

    return EXIT_SUCCESS;
}