#include "analyzer/ethernet.h"
#include "analyzer/ipv4.h"
#include "analyzer/packet_info.h"
#include "analyzer/udp.h"

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
 * @brief 初始化结果对象，并完成Ethernet和IPv4解析。
 */
static int prepare_udp_test_packet(
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
 * @brief 验证带有4字节负载的普通UDP数据报。
 */
static int test_udp_basic_datagram(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
        0x08, 0x00,

        /*
         * IPv4总长度32字节：
         * 20字节IPv4头 + 12字节UDP数据报。
         */
        0x45, 0x00,
        0x00, 0x20,
        0x12, 0x34,
        0x40, 0x00,
        0x40, 0x11,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x0a,
        0x08, 0x08, 0x08, 0x08,

        /*
         * UDP头部：
         * source_port=53000；
         * destination_port=53；
         * length=12；
         * checksum=0x1A2B。
         */
        0xcf, 0x08,
        0x00, 0x35,
        0x00, 0x0c,
        0x1a, 0x2b,

        /* 4字节UDP负载。 */
        0xde, 0xad, 0xbe, 0xef
    };

    packet_info_t packet_info;
    byte_cursor_t payload;
    uint8_t payload_bytes[4];

    TEST_CHECK(
        prepare_udp_test_packet(
            frame,
            sizeof(frame),
            (uint32_t)sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        udp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_udp);
    TEST_CHECK(
        packet_info.udp_source_port ==
            UINT16_C(53000)
    );
    TEST_CHECK(
        packet_info.udp_destination_port ==
            UINT16_C(53)
    );
    TEST_CHECK(
        packet_info.udp_length ==
            UINT16_C(12)
    );
    TEST_CHECK(
        packet_info.udp_checksum ==
            UINT16_C(0x1A2B)
    );
    TEST_CHECK(
        packet_info.udp_payload_offset ==
            ETHERNET_HEADER_LENGTH +
            IPV4_MIN_HEADER_LENGTH +
            UDP_HEADER_LENGTH
    );
    TEST_CHECK(packet_info.udp_payload_length == 4U);
    TEST_CHECK(!packet_info.udp_payload_truncated);

    TEST_CHECK(
        udp_payload_view(
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

    TEST_CHECK(payload_bytes[0] == UINT8_C(0xDE));
    TEST_CHECK(payload_bytes[1] == UINT8_C(0xAD));
    TEST_CHECK(payload_bytes[2] == UINT8_C(0xBE));
    TEST_CHECK(payload_bytes[3] == UINT8_C(0xEF));

    return EXIT_SUCCESS;
}

/**
 * @brief 验证UDP负载只捕获了一部分。
 */
static int test_udp_payload_truncation(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4声明总长度32字节，
         * 实际只捕获8字节UDP头和2字节负载。
         */
        0x45, 0x00,
        0x00, 0x20,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x11,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        /* UDP声明长度12字节。 */
        0x03, 0xe8,
        0x07, 0xd0,
        0x00, 0x0c,
        0x00, 0x00,

        /* 只捕获到2字节UDP负载。 */
        0xaa, 0xbb
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_udp_test_packet(
            frame,
            sizeof(frame),
            UINT32_C(46),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.ipv4_payload_truncated);

    TEST_CHECK(
        udp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_udp);
    TEST_CHECK(packet_info.udp_payload_length == 2U);
    TEST_CHECK(packet_info.udp_payload_truncated);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证UDP头部截断。
 */
static int test_udp_header_truncation(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4声明存在8字节UDP头部，
         * 实际只捕获了7字节。
         */
        0x45, 0x00,
        0x00, 0x1c,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x11,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        0x03, 0xe8,
        0x07, 0xd0,
        0x00, 0x08,
        0x00
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_udp_test_packet(
            frame,
            sizeof(frame),
            UINT32_C(42),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        udp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == ENODATA
    );

    TEST_CHECK(!packet_info.has_udp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_TRUNCATED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_UDP
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证小于8字节的非法UDP长度。
 */
static int test_udp_invalid_length(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /* IPv4负载为8字节。 */
        0x45, 0x00,
        0x00, 0x1c,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x11,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        /*
         * UDP长度字段为7，小于固定8字节头部。
         */
        0x03, 0xe8,
        0x07, 0xd0,
        0x00, 0x07,
        0x00, 0x00
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_udp_test_packet(
            frame,
            sizeof(frame),
            (uint32_t)sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        udp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == EBADMSG
    );

    TEST_CHECK(!packet_info.has_udp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_MALFORMED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_UDP
    );

    return EXIT_SUCCESS;
}

/**
 * @brief UDP解析器单元测试入口。
 */
int main(void)
{
    if (test_udp_basic_datagram() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] UDP basic datagram\n");

    if (test_udp_payload_truncation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] UDP payload truncation\n");

    if (test_udp_header_truncation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] UDP header truncation\n");

    if (test_udp_invalid_length() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] UDP invalid length\n");

    return EXIT_SUCCESS;
}