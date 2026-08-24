#include "analyzer/ethernet.h"
#include "analyzer/ipv4.h"
#include "analyzer/ipv4_dispatch.h"
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
static int prepare_dispatch_test_packet(
    const uint8_t *frame,
    size_t frame_length,
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
        (uint32_t)frame_length
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
 * @brief 验证协议号6被分发到TCP解析器。
 */
static int test_dispatch_tcp(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /* IPv4：总长度40，protocol=6。 */
        0x45, 0x00,
        0x00, 0x28,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x06,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x01,
        0xc0, 0xa8, 0x01, 0x02,

        /* 最小20字节TCP SYN头部。 */
        0x30, 0x39,
        0x00, 0x50,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00,
        0x50, 0x02,
        0x20, 0x00,
        0x12, 0x34,
        0x00, 0x00
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_dispatch_test_packet(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_dispatch_payload(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(packet_info.has_tcp);
    TEST_CHECK(!packet_info.has_udp);
    TEST_CHECK(!packet_info.has_icmp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_COMPLETE
    );
    TEST_CHECK(
        packet_info.tcp_source_port ==
            UINT16_C(12345)
    );
    TEST_CHECK(
        packet_info.tcp_destination_port ==
            UINT16_C(80)
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证协议号17被分发到UDP解析器。
 */
static int test_dispatch_udp(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /* IPv4：总长度28，protocol=17。 */
        0x45, 0x00,
        0x00, 0x1c,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x11,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x01,
        0x08, 0x08, 0x08, 0x08,

        /* 只有8字节头部的UDP数据报。 */
        0xcf, 0x08,
        0x00, 0x35,
        0x00, 0x08,
        0x00, 0x00
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_dispatch_test_packet(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_dispatch_payload(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(!packet_info.has_tcp);
    TEST_CHECK(packet_info.has_udp);
    TEST_CHECK(!packet_info.has_icmp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_COMPLETE
    );
    TEST_CHECK(
        packet_info.udp_destination_port ==
            UINT16_C(53)
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证协议号1被分发到ICMP解析器。
 */
static int test_dispatch_icmp(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /* IPv4：总长度28，protocol=1。 */
        0x45, 0x00,
        0x00, 0x1c,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x01,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x01,
        0x08, 0x08, 0x08, 0x08,

        /* ICMP Echo Request。 */
        0x08, 0x00,
        0x12, 0x34,
        0xab, 0xcd,
        0x00, 0x01
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_dispatch_test_packet(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_dispatch_payload(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(!packet_info.has_tcp);
    TEST_CHECK(!packet_info.has_udp);
    TEST_CHECK(packet_info.has_icmp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_COMPLETE
    );
    TEST_CHECK(
        packet_info.icmp_type == UINT8_C(8)
    );
    TEST_CHECK(
        packet_info.icmp_identifier ==
            UINT16_C(0xABCD)
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证未知IPv4协议号被标记为暂不支持。
 */
static int test_dispatch_unsupported_protocol(void)
{
    static const uint8_t frame[] = {
        /* Ethernet头部。 */
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x08, 0x00,

        /*
         * IPv4总长度20，没有负载。
         * protocol=99，项目当前没有对应解析器。
         */
        0x45, 0x00,
        0x00, 0x14,
        0x00, 0x01,
        0x40, 0x00,
        0x40, 0x63,
        0x00, 0x00,
        0xc0, 0xa8, 0x01, 0x01,
        0xc0, 0xa8, 0x01, 0x02
    };

    packet_info_t packet_info;

    TEST_CHECK(
        prepare_dispatch_test_packet(
            frame,
            sizeof(frame),
            &packet_info
        ) == 0
    );

    TEST_CHECK(
        ipv4_dispatch_payload(
            frame,
            sizeof(frame),
            &packet_info
        ) == ENOTSUP
    );

    TEST_CHECK(!packet_info.has_tcp);
    TEST_CHECK(!packet_info.has_udp);
    TEST_CHECK(!packet_info.has_icmp);
    TEST_CHECK(
        packet_info.parse_status ==
            PACKET_PARSE_STATUS_UNSUPPORTED
    );
    TEST_CHECK(
        packet_info.error_layer ==
            PACKET_PARSE_LAYER_IPV4
    );
    TEST_CHECK(packet_info.error_code == ENOTSUP);
    TEST_CHECK(
        packet_info.error_offset ==
            packet_info.ipv4_payload_offset
    );

    return EXIT_SUCCESS;
}

/**
 * @brief IPv4上层协议分发器单元测试入口。
 */
int main(void)
{
    if (test_dispatch_tcp() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] dispatch TCP\n");

    if (test_dispatch_udp() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] dispatch UDP\n");

    if (test_dispatch_icmp() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] dispatch ICMP\n");

    if (test_dispatch_unsupported_protocol() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] dispatch unsupported protocol\n");

    return EXIT_SUCCESS;
}