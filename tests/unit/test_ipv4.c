#include "analyzer/ethernet.h"
#include "analyzer/ipv4.h"
#include "analyzer/packet_info.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * @brief 初始化测试结果对象并完成Ethernet解析。
 *
 * IPv4解析器要求调用者已经完成Ethernet解析，因此把公共准备过程
 * 提取成辅助函数，避免每个测试重复相同代码。
 */
static int prepare_ipv4_test_packet(
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

    return ethernet_parse(
        frame,
        frame_length,
        packet_info
    );
}

/**
 * @brief 验证不带选项的普通IPv4数据包。
 */
static int test_ipv4_basic_packet(void)
{
    static const uint8_t frame[] = {
        /* Ethernet目标MAC。 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,

        /* Ethernet源MAC。 */
        0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,

        /* EtherType：IPv4。 */
        0x08, 0x00,

        /*
         * IPv4头部：
         *
         * version=4，IHL=5；
         * total_length=24；
         * identification=0x1234；
         * DF=true；
         * TTL=64；
         * protocol=TCP；
         * source=192.168.1.10；
         * destination=10.0.0.5。
         */
        0x45, 0x00,
        0x00, 0x18,
        0x12, 0x34,
        0x40, 0x00,
        0x40,
        0x06,
        0x1a, 0x2b,
        0xc0, 0xa8, 0x01, 0x0a,
        0x0a, 0x00, 0x00, 0x05,

        /* 4字节模拟IPv4负载。 */
        0xde, 0xad, 0xbe, 0xef
    };

    static const uint8_t expected_payload[] = {
        0xde, 0xad, 0xbe, 0xef
    };

    packet_info_t packet_info;
    byte_cursor_t payload;

    uint8_t actual_payload[sizeof(expected_payload)];
    char source_text[IPV4_ADDRESS_STRING_SIZE];
    char destination_text[IPV4_ADDRESS_STRING_SIZE];

    TEST_CHECK(
        prepare_ipv4_test_packet(
            frame,
            sizeof(frame),
            (uint32_t)sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_ipv4);
    TEST_CHECK(
        packet_info.ipv4_header_length ==
            UINT8_C(20)
    );
    TEST_CHECK(
        packet_info.ipv4_total_length ==
            UINT16_C(24)
    );
    TEST_CHECK(
        packet_info.ipv4_identification ==
            UINT16_C(0x1234)
    );
    TEST_CHECK(packet_info.ipv4_dont_fragment);
    TEST_CHECK(!packet_info.ipv4_more_fragments);
    TEST_CHECK(
        packet_info.ipv4_fragment_offset ==
            UINT16_C(0)
    );
    TEST_CHECK(
        packet_info.ipv4_ttl ==
            UINT8_C(64)
    );
    TEST_CHECK(
        packet_info.ipv4_protocol ==
            IPV4_PROTOCOL_TCP
    );
    TEST_CHECK(
        packet_info.ipv4_header_checksum ==
            UINT16_C(0x1A2B)
    );
    TEST_CHECK(
        packet_info.source_ipv4 ==
            UINT32_C(0xC0A8010A)
    );
    TEST_CHECK(
        packet_info.destination_ipv4 ==
            UINT32_C(0x0A000005)
    );
    TEST_CHECK(
        packet_info.ipv4_payload_offset ==
            ETHERNET_HEADER_LENGTH +
            IPV4_MIN_HEADER_LENGTH
    );
    TEST_CHECK(
        packet_info.ipv4_payload_length ==
            sizeof(expected_payload)
    );
    TEST_CHECK(!packet_info.ipv4_payload_truncated);

    TEST_CHECK(
        ipv4_payload_view(
            frame,
            sizeof(frame),
            &packet_info,
            &payload
        ) == 0
    );

    TEST_CHECK(
        byte_cursor_read_bytes(
            &payload,
            actual_payload,
            sizeof(actual_payload)
        ) == 0
    );

    TEST_CHECK(
        memcmp(
            actual_payload,
            expected_payload,
            sizeof(expected_payload)
        ) == 0
    );

    TEST_CHECK(
        ipv4_format_address(
            packet_info.source_ipv4,
            source_text,
            sizeof(source_text)
        ) == 0
    );

    TEST_CHECK(
        strcmp(source_text, "192.168.1.10") == 0
    );

    TEST_CHECK(
        ipv4_format_address(
            packet_info.destination_ipv4,
            destination_text,
            sizeof(destination_text)
        ) == 0
    );

    TEST_CHECK(
        strcmp(destination_text, "10.0.0.5") == 0
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证IPv4选项、分片字段和Ethernet尾部填充。
 */
static int test_ipv4_options_and_padding(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * version=4，IHL=6，因此IPv4头部为24字节。
         * total_length=26，意味着IPv4负载只有2字节。
         */
        0x46, 0x00,
        0x00, 0x1a,
        0xab, 0xcd,

        /*
         * MF=true，fragment_offset=1。
         */
        0x20, 0x01,

        0x20,
        0x11,
        0x12, 0x34,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08,

        /* 4字节IPv4选项。 */
        0x01, 0x01, 0x00, 0x00,

        /* IPv4声明的2字节负载。 */
        0xaa, 0xbb,

        /*
         * Ethernet尾部填充。
         * 这些字节不能被算进IPv4负载。
         */
        0xcc, 0xdd, 0xee, 0xff
    };

    packet_info_t packet_info;
    byte_cursor_t payload;
    uint8_t payload_bytes[2];

    TEST_CHECK(
        prepare_ipv4_test_packet(
            frame,
            sizeof(frame),
            (uint32_t)sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        packet_info.ipv4_header_length ==
            UINT8_C(24)
    );
    TEST_CHECK(
        packet_info.ipv4_total_length ==
            UINT16_C(26)
    );
    TEST_CHECK(!packet_info.ipv4_dont_fragment);
    TEST_CHECK(packet_info.ipv4_more_fragments);
    TEST_CHECK(
        packet_info.ipv4_fragment_offset ==
            UINT16_C(1)
    );

    /*
     * 尽管Ethernet负载后面还有4字节填充，
     * IPv4负载长度仍然只能是2。
     */
    TEST_CHECK(
        packet_info.ipv4_payload_length == 2U
    );

    TEST_CHECK(
        ipv4_payload_view(
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

    TEST_CHECK(payload_bytes[0] == UINT8_C(0xAA));
    TEST_CHECK(payload_bytes[1] == UINT8_C(0xBB));

    return EXIT_SUCCESS;
}

/**
 * @brief 验证IPv4头完整但数据报负载未完整捕获。
 */
static int test_ipv4_captured_payload_truncation(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4声明总长度为40字节，但PCAP中只保存了
         * 20字节头部和2字节负载。
         */
        0x45, 0x00,
        0x00, 0x28,
        0x00, 0x01,
        0x00, 0x00,
        0x40,
        0x11,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x01,
        0xc0, 0xa8, 0x01, 0x02,

        0xaa, 0xbb
    };

    packet_info_t packet_info;

    /*
     * 线路长度为Ethernet头14字节加IPv4总长度40字节。
     */
    TEST_CHECK(
        prepare_ipv4_test_packet(
            frame,
            sizeof(frame),
            UINT32_C(54),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_ipv4);
    TEST_CHECK(packet_info.ipv4_payload_truncated);
    TEST_CHECK(
        packet_info.ipv4_payload_length == 2U
    );
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_NOT_STARTED
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证必要头部截断和非法版本号。
 */
static int test_ipv4_error_handling(void)
{
    static const uint8_t truncated_frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /* 只有19字节IPv4区域，不足最小20字节头部。 */
        0x45, 0x00, 0x00, 0x14,
        0x00, 0x01, 0x00, 0x00,
        0x40, 0x06, 0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07
    };

    static const uint8_t wrong_version_frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * 高4位为6，表示版本号6，与EtherType IPv4冲突。
         */
        0x65, 0x00, 0x00, 0x14,
        0x00, 0x01, 0x00, 0x00,
        0x40, 0x06, 0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_ipv4_test_packet(
            truncated_frame,
            sizeof(truncated_frame),
            (uint32_t)sizeof(truncated_frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_parse(
            truncated_frame,
            sizeof(truncated_frame),
            &packet_info
        ) == ENODATA
    );

    TEST_CHECK(!packet_info.has_ipv4);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_TRUNCATED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_IPV4
    );
    TEST_CHECK(packet_info.error_code == ENODATA);
    TEST_CHECK(
        packet_info.error_offset ==
            sizeof(truncated_frame)
    );

    TEST_CHECK(
        prepare_ipv4_test_packet(
            wrong_version_frame,
            sizeof(wrong_version_frame),
            (uint32_t)sizeof(wrong_version_frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_parse(
            wrong_version_frame,
            sizeof(wrong_version_frame),
            &packet_info
        ) == EBADMSG
    );

    TEST_CHECK(!packet_info.has_ipv4);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_MALFORMED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_IPV4
    );
    TEST_CHECK(packet_info.error_code == EBADMSG);
    TEST_CHECK(
        packet_info.error_offset ==
            ETHERNET_HEADER_LENGTH
    );

    return EXIT_SUCCESS;
}

/**
 * @brief IPv4解析器单元测试入口。
 */
int main(void)
{
    if (test_ipv4_basic_packet() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] IPv4 basic packet\n");

    if (test_ipv4_options_and_padding() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] IPv4 options and padding\n");

    if (test_ipv4_captured_payload_truncation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] IPv4 captured payload truncation\n");

    if (test_ipv4_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] IPv4 error handling\n");

    return EXIT_SUCCESS;
}