#include "analyzer/ethernet.h"
#include "analyzer/icmp.h"
#include "analyzer/ipv4.h"
#include "analyzer/packet_info.h"

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
static int prepare_icmp_test_packet(
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
 * @brief 验证ICMP Echo Request及其负载。
 */
static int test_icmp_echo_request(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,
        0x08, 0x00,

        /*
         * IPv4总长度32字节：
         * 20字节IPv4头 + 12字节ICMP消息。
         */
        0x45, 0x00,
        0x00, 0x20,
        0x12, 0x34,
        0x40, 0x00,
        0x40, 0x01,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x0a,
        0x08, 0x08, 0x08, 0x08,

        /*
         * ICMP Echo Request：
         * type=8；
         * code=0；
         * checksum=0xF7FF；
         * identifier=0x1234；
         * sequence=1。
         */
        0x08, 0x00,
        0xf7, 0xff,
        0x12, 0x34,
        0x00, 0x01,

        /* 4字节Echo负载。 */
        0xde, 0xad, 0xbe, 0xef
    };

    packet_info_t packet_info;
    byte_cursor_t payload;
    uint8_t payload_bytes[4];

    TEST_CHECK(
        prepare_icmp_test_packet(
            frame,
            sizeof(frame),
            (uint32_t)sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        icmp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_icmp);
    TEST_CHECK(
        packet_info.icmp_type ==
            ICMP_TYPE_ECHO_REQUEST
    );
    TEST_CHECK(packet_info.icmp_code == UINT8_C(0));
    TEST_CHECK(
        packet_info.icmp_checksum ==
            UINT16_C(0xF7FF)
    );
    TEST_CHECK(
        packet_info.icmp_rest_of_header ==
            UINT32_C(0x12340001)
    );
    TEST_CHECK(packet_info.icmp_has_echo_fields);
    TEST_CHECK(
        packet_info.icmp_identifier ==
            UINT16_C(0x1234)
    );
    TEST_CHECK(
        packet_info.icmp_sequence ==
            UINT16_C(1)
    );
    TEST_CHECK(packet_info.icmp_payload_length == 4U);
    TEST_CHECK(!packet_info.icmp_payload_truncated);

    TEST_CHECK(
        icmp_payload_view(
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
 * @brief 验证非Echo类型不会错误生成Echo字段。
 */
static int test_icmp_destination_unreachable(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /* IPv4负载只有8字节ICMP公共头部。 */
        0x45, 0x00,
        0x00, 0x1c,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x01,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        /*
         * type=3：Destination Unreachable；
         * code=3：Port Unreachable。
         */
        0x03, 0x03,
        0xab, 0xcd,
        0x00, 0x00, 0x00, 0x00
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_icmp_test_packet(
            frame,
            sizeof(frame),
            (uint32_t)sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        icmp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        packet_info.icmp_type ==
            ICMP_TYPE_DESTINATION_UNREACHABLE
    );
    TEST_CHECK(packet_info.icmp_code == UINT8_C(3));
    TEST_CHECK(!packet_info.icmp_has_echo_fields);
    TEST_CHECK(
        packet_info.icmp_identifier ==
            UINT16_C(0)
    );
    TEST_CHECK(
        packet_info.icmp_sequence ==
            UINT16_C(0)
    );
    TEST_CHECK(packet_info.icmp_payload_length == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证ICMP负载只捕获了一部分。
 */
static int test_icmp_payload_truncation(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4声明12字节ICMP消息，
         * 实际只捕获8字节头部和2字节负载。
         */
        0x45, 0x00,
        0x00, 0x20,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x01,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        0x08, 0x00,
        0x12, 0x34,
        0x00, 0x01,
        0x00, 0x02,

        /* 只捕获到2字节负载。 */
        0xaa, 0xbb
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_icmp_test_packet(
            frame,
            sizeof(frame),
            UINT32_C(46),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        icmp_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_icmp);
    TEST_CHECK(packet_info.icmp_payload_length == 2U);
    TEST_CHECK(packet_info.icmp_payload_truncated);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证ICMP头部截断和非法声明长度。
 */
static int test_icmp_error_handling(void)
{
    static const uint8_t truncated_frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4声明8字节ICMP头部，
         * 实际只捕获7字节。
         */
        0x45, 0x00,
        0x00, 0x1c,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x01,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        0x08, 0x00,
        0x12, 0x34,
        0x00, 0x01,
        0x00
    };

    static const uint8_t malformed_frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4总长度27字节，因此只声明了7字节ICMP区域。
         */
        0x45, 0x00,
        0x00, 0x1b,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x01,
        0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        0x08, 0x00,
        0x12, 0x34,
        0x00, 0x01,
        0x00
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_icmp_test_packet(
            truncated_frame,
            sizeof(truncated_frame),
            UINT32_C(42),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        icmp_parse(
            truncated_frame,
            sizeof(truncated_frame),
            &packet_info
        ) == ENODATA
    );

    TEST_CHECK(!packet_info.has_icmp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_TRUNCATED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_ICMP
    );

    TEST_CHECK(
        prepare_icmp_test_packet(
            malformed_frame,
            sizeof(malformed_frame),
            (uint32_t)sizeof(malformed_frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        icmp_parse(
            malformed_frame,
            sizeof(malformed_frame),
            &packet_info
        ) == EBADMSG
    );

    TEST_CHECK(!packet_info.has_icmp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_MALFORMED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_ICMP
    );

    return EXIT_SUCCESS;
}

/**
 * @brief ICMP解析器单元测试入口。
 */
int main(void)
{
    if (test_icmp_echo_request() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ICMP echo request\n");

    if (test_icmp_destination_unreachable() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ICMP destination unreachable\n");

    if (test_icmp_payload_truncation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ICMP payload truncation\n");

    if (test_icmp_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ICMP error handling\n");

    return EXIT_SUCCESS;
}