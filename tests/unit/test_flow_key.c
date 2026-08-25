#include "analyzer/flow_key.h"
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
 * @brief 构造一条用于流键测试的已解析数据包结果。
 *
 * 本测试只验证流键模型，不重复测试Ethernet、IPv4和传输层解析器，
 * 因此直接构造packet_info_t所需字段。
 */
static int prepare_flow_packet(
    packet_info_t *packet_info,
    uint32_t source_ipv4,
    uint16_t source_port,
    uint32_t destination_ipv4,
    uint16_t destination_port,
    uint8_t protocol)
{
    int error_code;

    error_code = packet_info_init(
        packet_info,
        INT64_C(100),
        INT32_C(200),
        UINT32_C(100),
        UINT32_C(100)
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 流键只依赖已经解析完成的结构化字段，
     * 不保存或访问原始数据包。
     */
    packet_info->has_ethernet = true;
    packet_info->has_ipv4 = true;
    packet_info->source_ipv4 = source_ipv4;
    packet_info->destination_ipv4 =
        destination_ipv4;
    packet_info->ipv4_protocol = protocol;

    switch (protocol) {
    case IPV4_PROTOCOL_TCP:
        packet_info->has_tcp = true;
        packet_info->tcp_source_port = source_port;
        packet_info->tcp_destination_port =
            destination_port;
        break;

    case IPV4_PROTOCOL_UDP:
        packet_info->has_udp = true;
        packet_info->udp_source_port = source_port;
        packet_info->udp_destination_port =
            destination_port;
        break;

    case IPV4_PROTOCOL_ICMP:
        packet_info->has_icmp = true;
        break;

    default:
        /*
         * 未知协议仍可用于验证ENOTSUP行为。
         */
        break;
    }

    return packet_info_mark_complete(packet_info);
}

/**
 * @brief 验证TCP正反方向生成同一个流键。
 */
static int test_tcp_bidirectional_key(void)
{
    packet_info_t forward_packet;
    packet_info_t reverse_packet;

    flow_key_t forward_key;
    flow_key_t reverse_key;

    flow_direction_t forward_direction;
    flow_direction_t reverse_direction;

    uint64_t forward_hash;
    uint64_t reverse_hash;

    TEST_CHECK(
        prepare_flow_packet(
            &forward_packet,
            UINT32_C(0xC0A8010A),
            UINT16_C(52134),
            UINT32_C(0x08080808),
            UINT16_C(443),
            IPV4_PROTOCOL_TCP
        ) == 0
    );

    TEST_CHECK(
        prepare_flow_packet(
            &reverse_packet,
            UINT32_C(0x08080808),
            UINT16_C(443),
            UINT32_C(0xC0A8010A),
            UINT16_C(52134),
            IPV4_PROTOCOL_TCP
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &forward_packet,
            &forward_key,
            &forward_direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &reverse_packet,
            &reverse_key,
            &reverse_direction
        ) == 0
    );

    /*
     * 两个方向必须得到同一个规范化流键。
     */
    TEST_CHECK(
        flow_key_equal(
            &forward_key,
            &reverse_key
        )
    );

    /*
     * 同一条双向流的正向包和反向包生成相同规范化键，
     * 因此也必须生成相同哈希值。
     */
    TEST_CHECK(
        flow_key_hash(
            &forward_key,
            &forward_hash
        ) == 0
    );

    TEST_CHECK(
        flow_key_hash(
            &reverse_key,
            &reverse_hash
        ) == 0
    );

    TEST_CHECK(forward_hash == reverse_hash);

    /*
     * 固定结果用于验证字段顺序和字节顺序没有意外改变。
     *
     * 当前键为：
     * 8.8.8.8:443 <-> 192.168.1.10:52134，TCP。
     */
    TEST_CHECK(
        forward_hash ==
            UINT64_C(0x244C7CE026B9D2C3)
    );

    /*
     * 8.8.8.8的数值小于192.168.1.10，
     * 因此它被规范化为endpoint_a。
     */
    TEST_CHECK(
        forward_key.endpoint_a.ipv4_address ==
            UINT32_C(0x08080808)
    );
    TEST_CHECK(
        forward_key.endpoint_a.port ==
            UINT16_C(443)
    );
    TEST_CHECK(
        forward_key.endpoint_b.ipv4_address ==
            UINT32_C(0xC0A8010A)
    );
    TEST_CHECK(
        forward_key.endpoint_b.port ==
            UINT16_C(52134)
    );

    TEST_CHECK(
        forward_direction ==
            FLOW_DIRECTION_B_TO_A
    );
    TEST_CHECK(
        reverse_direction ==
            FLOW_DIRECTION_A_TO_B
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证IP相同时使用端口决定端点顺序。
 */
static int test_same_address_port_ordering(void)
{
    packet_info_t forward_packet;
    packet_info_t reverse_packet;

    flow_key_t forward_key;
    flow_key_t reverse_key;

    flow_direction_t forward_direction;
    flow_direction_t reverse_direction;

    /*
     * 两个进程都位于127.0.0.1，但端口不同。
     */
    TEST_CHECK(
        prepare_flow_packet(
            &forward_packet,
            UINT32_C(0x7F000001),
            UINT16_C(50000),
            UINT32_C(0x7F000001),
            UINT16_C(8080),
            IPV4_PROTOCOL_TCP
        ) == 0
    );

    TEST_CHECK(
        prepare_flow_packet(
            &reverse_packet,
            UINT32_C(0x7F000001),
            UINT16_C(8080),
            UINT32_C(0x7F000001),
            UINT16_C(50000),
            IPV4_PROTOCOL_TCP
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &forward_packet,
            &forward_key,
            &forward_direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &reverse_packet,
            &reverse_key,
            &reverse_direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_equal(
            &forward_key,
            &reverse_key
        )
    );

    TEST_CHECK(
        forward_key.endpoint_a.port ==
            UINT16_C(8080)
    );
    TEST_CHECK(
        forward_key.endpoint_b.port ==
            UINT16_C(50000)
    );

    TEST_CHECK(
        forward_direction ==
            FLOW_DIRECTION_B_TO_A
    );
    TEST_CHECK(
        reverse_direction ==
            FLOW_DIRECTION_A_TO_B
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证ICMP使用0端口并支持双向归一化。
 */
static int test_icmp_bidirectional_key(void)
{
    packet_info_t request_packet;
    packet_info_t reply_packet;

    flow_key_t request_key;
    flow_key_t reply_key;

    flow_direction_t request_direction;
    flow_direction_t reply_direction;

    TEST_CHECK(
        prepare_flow_packet(
            &request_packet,
            UINT32_C(0x0A000001),
            UINT16_C(1234),
            UINT32_C(0x08080808),
            UINT16_C(5678),
            IPV4_PROTOCOL_ICMP
        ) == 0
    );

    TEST_CHECK(
        prepare_flow_packet(
            &reply_packet,
            UINT32_C(0x08080808),
            UINT16_C(5678),
            UINT32_C(0x0A000001),
            UINT16_C(1234),
            IPV4_PROTOCOL_ICMP
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &request_packet,
            &request_key,
            &request_direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &reply_packet,
            &reply_key,
            &reply_direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_equal(
            &request_key,
            &reply_key
        )
    );

    TEST_CHECK(request_key.endpoint_a.port == UINT16_C(0));
    TEST_CHECK(request_key.endpoint_b.port == UINT16_C(0));
    TEST_CHECK(
        request_key.protocol == IPV4_PROTOCOL_ICMP
    );

    TEST_CHECK(
        request_direction ==
            FLOW_DIRECTION_B_TO_A
    );
    TEST_CHECK(
        reply_direction ==
            FLOW_DIRECTION_A_TO_B
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证协议号属于流键的一部分。
 */
static int test_protocol_separates_flows(void)
{
    packet_info_t tcp_packet;
    packet_info_t udp_packet;

    flow_key_t tcp_key;
    flow_key_t udp_key;

    flow_direction_t direction;

    TEST_CHECK(
        prepare_flow_packet(
            &tcp_packet,
            UINT32_C(0xC0A80101),
            UINT16_C(50000),
            UINT32_C(0x08080808),
            UINT16_C(53),
            IPV4_PROTOCOL_TCP
        ) == 0
    );

    TEST_CHECK(
        prepare_flow_packet(
            &udp_packet,
            UINT32_C(0xC0A80101),
            UINT16_C(50000),
            UINT32_C(0x08080808),
            UINT16_C(53),
            IPV4_PROTOCOL_UDP
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &tcp_packet,
            &tcp_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &udp_packet,
            &udp_key,
            &direction
        ) == 0
    );

    TEST_CHECK(!flow_key_equal(&tcp_key, &udp_key));

    return EXIT_SUCCESS;
}

/**
 * @brief 验证不同业务字段参与哈希，并验证参数错误处理。
 */
static int test_flow_key_hashing(void)
{
    flow_key_t original_key = {
        .endpoint_a = {
            .ipv4_address = UINT32_C(0x08080808),
            .port = UINT16_C(53)
        },
        .endpoint_b = {
            .ipv4_address = UINT32_C(0xC0A80101),
            .port = UINT16_C(50000)
        },
        .protocol = IPV4_PROTOCOL_UDP
    };

    flow_key_t changed_key;

    uint64_t original_hash;
    uint64_t changed_hash;
    uint64_t unchanged_hash;

    TEST_CHECK(
        flow_key_hash(
            &original_key,
            &original_hash
        ) == 0
    );

    /*
     * 改变端口后重新计算。
     *
     * 哈希算法理论上允许冲突，但这个固定测试输入用于发现
     * “忘记把端口加入哈希”等具体实现错误。
     */
    changed_key = original_key;
    changed_key.endpoint_b.port = UINT16_C(50001);

    TEST_CHECK(
        flow_key_hash(
            &changed_key,
            &changed_hash
        ) == 0
    );

    TEST_CHECK(original_hash != changed_hash);

    /*
     * 改变协议号也应该影响当前测试输入的哈希结果。
     */
    changed_key = original_key;
    changed_key.protocol = IPV4_PROTOCOL_TCP;

    TEST_CHECK(
        flow_key_hash(
            &changed_key,
            &changed_hash
        ) == 0
    );

    TEST_CHECK(original_hash != changed_hash);

    /*
     * 失败时输出变量必须保持原值。
     */
    unchanged_hash = UINT64_C(12345);

    TEST_CHECK(
        flow_key_hash(
            NULL,
            &unchanged_hash
        ) == EINVAL
    );

    TEST_CHECK(unchanged_hash == UINT64_C(12345));

    TEST_CHECK(
        flow_key_hash(
            &original_key,
            NULL
        ) == EINVAL
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证不完整结果和未知协议被拒绝。
 */
static int test_flow_key_error_handling(void)
{
    packet_info_t incomplete_packet;
    packet_info_t unsupported_packet;

    flow_key_t key = {
        .endpoint_a = {
            .ipv4_address = UINT32_C(1),
            .port = UINT16_C(2)
        },
        .endpoint_b = {
            .ipv4_address = UINT32_C(3),
            .port = UINT16_C(4)
        },
        .protocol = UINT8_C(5)
    };

    flow_key_t original_key = key;

    flow_direction_t direction =
        FLOW_DIRECTION_UNKNOWN;

    TEST_CHECK(
        packet_info_init(
            &incomplete_packet,
            INT64_C(100),
            INT32_C(200),
            UINT32_C(100),
            UINT32_C(100)
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &incomplete_packet,
            &key,
            &direction
        ) == EINVAL
    );

    /*
     * 失败时输出参数不能被部分修改。
     */
    TEST_CHECK(flow_key_equal(&key, &original_key));
    TEST_CHECK(
        direction == FLOW_DIRECTION_UNKNOWN
    );

    TEST_CHECK(
        prepare_flow_packet(
            &unsupported_packet,
            UINT32_C(0x01020304),
            UINT16_C(1000),
            UINT32_C(0x05060708),
            UINT16_C(2000),
            UINT8_C(99)
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &unsupported_packet,
            &key,
            &direction
        ) == ENOTSUP
    );

    TEST_CHECK(flow_key_equal(&key, &original_key));
    TEST_CHECK(
        direction == FLOW_DIRECTION_UNKNOWN
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 双向流键单元测试入口。
 */
int main(void)
{
    if (test_tcp_bidirectional_key() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP bidirectional key\n");

    if (test_same_address_port_ordering() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] same-address port ordering\n");

    if (test_icmp_bidirectional_key() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] ICMP bidirectional key\n");

    if (test_protocol_separates_flows() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] protocol separates flows\n");

    if (test_flow_key_error_handling() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] flow key error handling\n");

    if (test_flow_key_hashing() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] flow key hashing\n");

    return EXIT_SUCCESS;
}