#include "analyzer/ethernet.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 在Debug和Release下都有效的测试检查宏。
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
 * @brief 验证普通Ethernet II IPv4帧能够正确解析。
 */
static int test_ethernet_ipv4_frame(void)
{
    static const uint8_t frame[] = {
        /*
         * 目标MAC：00:11:22:33:44:55
         */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,

        /*
         * 源MAC：66:77:88:99:aa:bb
         */
        0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb,

        /*
         * EtherType：0x0800，表示IPv4。
         */
        0x08, 0x00,

        /*
         * 当前测试使用的4字节模拟IPv4负载。
         */
        0xde, 0xad, 0xbe, 0xef
    };

    static const uint8_t expected_destination[] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55
    };

    static const uint8_t expected_source[] = {
        0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb
    };

    static const uint8_t expected_payload[] = {
        0xde, 0xad, 0xbe, 0xef
    };

    packet_info_t packet_info;
    byte_cursor_t payload;

    uint8_t payload_bytes[sizeof(expected_payload)];
    char destination_text[ETHERNET_MAC_STRING_SIZE];
    char source_text[ETHERNET_MAC_STRING_SIZE];

    TEST_CHECK(
        packet_info_init(
            &packet_info,
            INT64_C(100),
            INT32_C(200),
            (uint32_t)sizeof(frame),
            (uint32_t)sizeof(frame)
        ) == 0
    );

    TEST_CHECK(
        ethernet_parse(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_ethernet);

    TEST_CHECK(
        memcmp(
            packet_info.destination_mac,
            expected_destination,
            sizeof(expected_destination)
        ) == 0
    );

    TEST_CHECK(
        memcmp(
            packet_info.source_mac,
            expected_source,
            sizeof(expected_source)
        ) == 0
    );

    TEST_CHECK(
        packet_info.ether_type ==
            ETHERNET_TYPE_IPV4
    );

    TEST_CHECK(
        packet_info.network_payload_offset ==
            ETHERNET_HEADER_LENGTH
    );

    TEST_CHECK(
        packet_info.network_payload_length ==
            sizeof(expected_payload)
    );

    /*
     * Ethernet成功后仍未完成整个数据包解析。
     */
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_NOT_STARTED
    );

    TEST_CHECK(
        ethernet_payload_view(
            frame,
            sizeof(frame),
            &packet_info,
            &payload
        ) == 0
    );

    TEST_CHECK(
        byte_cursor_remaining(&payload) ==
            sizeof(expected_payload)
    );

    TEST_CHECK(
        byte_cursor_read_bytes(
            &payload,
            payload_bytes,
            sizeof(payload_bytes)
        ) == 0
    );

    TEST_CHECK(
        memcmp(
            payload_bytes,
            expected_payload,
            sizeof(expected_payload)
        ) == 0
    );

    TEST_CHECK(byte_cursor_remaining(&payload) == 0U);

    TEST_CHECK(
        ethernet_format_mac(
            packet_info.destination_mac,
            destination_text,
            sizeof(destination_text)
        ) == 0
    );

    TEST_CHECK(
        strcmp(
            destination_text,
            "00:11:22:33:44:55"
        ) == 0
    );

    TEST_CHECK(
        ethernet_format_mac(
            packet_info.source_mac,
            source_text,
            sizeof(source_text)
        ) == 0
    );

    TEST_CHECK(
        strcmp(
            source_text,
            "66:77:88:99:aa:bb"
        ) == 0
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证ARP和未知EtherType仍然属于合法Ethernet帧。
 */
static int test_ethernet_type_handling(void)
{
    static const uint8_t arp_frame[] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x08, 0x06
    };

    static const uint8_t unknown_frame[] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
        0x88, 0xb5
    };

    packet_info_t packet_info;
    byte_cursor_t payload;

    TEST_CHECK(
        packet_info_init(
            &packet_info,
            INT64_C(1),
            INT32_C(2),
            (uint32_t)sizeof(arp_frame),
            (uint32_t)sizeof(arp_frame)
        ) == 0
    );

    TEST_CHECK(
        ethernet_parse(
            arp_frame,
            sizeof(arp_frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        packet_info.ether_type ==
            ETHERNET_TYPE_ARP
    );

    /*
     * 只有14字节头部也是合法输入，网络层负载长度为0。
     */
    TEST_CHECK(packet_info.network_payload_length == 0U);

    TEST_CHECK(
        ethernet_payload_view(
            arp_frame,
            sizeof(arp_frame),
            &packet_info,
            &payload
        ) == 0
    );

    TEST_CHECK(byte_cursor_remaining(&payload) == 0U);

    TEST_CHECK(
        packet_info_init(
            &packet_info,
            INT64_C(3),
            INT32_C(4),
            (uint32_t)sizeof(unknown_frame),
            (uint32_t)sizeof(unknown_frame)
        ) == 0
    );

    TEST_CHECK(
        ethernet_parse(
            unknown_frame,
            sizeof(unknown_frame),
            &packet_info
        ) == 0
    );

    /*
     * Ethernet层只负责保存EtherType，不在这里拒绝未知网络层。
     */
    TEST_CHECK(
        packet_info.ether_type ==
            UINT16_C(0x88B5)
    );
    TEST_CHECK(packet_info.has_ethernet);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证不足14字节的Ethernet帧会被标记为截断。
 */
static int test_ethernet_truncated_frame(void)
{
    static const uint8_t truncated_frame[13] = {
        0x00
    };

    packet_info_t packet_info;
    byte_cursor_t payload;

    TEST_CHECK(
        packet_info_init(
            &packet_info,
            INT64_C(10),
            INT32_C(20),
            (uint32_t)sizeof(truncated_frame),
            UINT32_C(60)
        ) == 0
    );

    TEST_CHECK(
        ethernet_parse(
            truncated_frame,
            sizeof(truncated_frame),
            &packet_info
        ) == ENODATA
    );

    TEST_CHECK(!packet_info.has_ethernet);

    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_TRUNCATED
    );

    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_ETHERNET
    );

    TEST_CHECK(packet_info.error_code == ENODATA);

    TEST_CHECK(
        packet_info.error_offset ==
            sizeof(truncated_frame)
    );

    TEST_CHECK(
        packet_info_capture_is_truncated(&packet_info)
    );

    /*
     * Ethernet没有成功解析，因此不能取得网络层负载视图。
     */
    TEST_CHECK(
        ethernet_payload_view(
            truncated_frame,
            sizeof(truncated_frame),
            &packet_info,
            &payload
        ) == EINVAL
    );

    /*
     * 空捕获也是一种合法输入状态，但不足以包含Ethernet头部。
     */
    TEST_CHECK(
        packet_info_init(
            &packet_info,
            INT64_C(30),
            INT32_C(40),
            UINT32_C(0),
            UINT32_C(0)
        ) == 0
    );

    TEST_CHECK(
        ethernet_parse(
            NULL,
            0U,
            &packet_info
        ) == ENODATA
    );

    TEST_CHECK(packet_info.error_offset == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证无效参数和MAC格式化缓冲区检查。
 */
static int test_ethernet_argument_validation(void)
{
    static const uint8_t frame[ETHERNET_HEADER_LENGTH] = {
        0x00
    };

    static const uint8_t address[ETHERNET_MAC_ADDRESS_LENGTH] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55
    };

    packet_info_t packet_info;
    byte_cursor_t payload;
    char small_buffer[ETHERNET_MAC_STRING_SIZE - 1U];

    TEST_CHECK(
        packet_info_init(
            &packet_info,
            INT64_C(1),
            INT32_C(2),
            (uint32_t)sizeof(frame),
            (uint32_t)sizeof(frame)
        ) == 0
    );

    TEST_CHECK(
        ethernet_parse(
            NULL,
            sizeof(frame),
            &packet_info
        ) == EINVAL
    );

    TEST_CHECK(!packet_info.has_ethernet);

    TEST_CHECK(
        ethernet_parse(
            frame,
            sizeof(frame) - 1U,
            &packet_info
        ) == EINVAL
    );

    TEST_CHECK(!packet_info.has_ethernet);

    /*
     * Ethernet解析前不能建立网络层负载视图。
     */
    TEST_CHECK(
        ethernet_payload_view(
            frame,
            sizeof(frame),
            &packet_info,
            &payload
        ) == EINVAL
    );

    small_buffer[0] = 'X';

    TEST_CHECK(
        ethernet_format_mac(
            address,
            small_buffer,
            sizeof(small_buffer)
        ) == ENOSPC
    );

    /*
     * 失败时不能留下部分字符串。
     */
    TEST_CHECK(small_buffer[0] == 'X');

    TEST_CHECK(
        ethernet_format_mac(
            NULL,
            small_buffer,
            sizeof(small_buffer)
        ) == EINVAL
    );

    TEST_CHECK(
        ethernet_format_mac(
            address,
            NULL,
            ETHERNET_MAC_STRING_SIZE
        ) == EINVAL
    );

    return EXIT_SUCCESS;
}

/**
 * @brief Ethernet模块单元测试入口。
 */
int main(void)
{
    if (test_ethernet_ipv4_frame() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ethernet IPv4 frame\n");

    if (test_ethernet_type_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ethernet type handling\n");

    if (test_ethernet_truncated_frame() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ethernet truncated frame\n");

    if (test_ethernet_argument_validation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ethernet argument validation\n");

    return EXIT_SUCCESS;
}