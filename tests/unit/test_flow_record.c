#include "analyzer/flow_record.h"
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
 * @brief 构造一条用于流记录测试的TCP数据包结果。
 *
 * 本测试关注流记录聚合，不重复测试底层协议字节解析。
 */
static int prepare_tcp_packet(
    packet_info_t *packet,
    int64_t timestamp_seconds,
    int32_t timestamp_microseconds,
    uint32_t captured_length,
    uint32_t wire_length,
    uint32_t source_ipv4,
    uint16_t source_port,
    uint32_t destination_ipv4,
    uint16_t destination_port)
{
    int error_code;

    error_code = packet_info_init(
        packet,
        timestamp_seconds,
        timestamp_microseconds,
        captured_length,
        wire_length
    );

    if (error_code != 0) {
        return error_code;
    }

    packet->has_ethernet = true;
    packet->has_ipv4 = true;
    packet->ipv4_protocol = IPV4_PROTOCOL_TCP;
    packet->source_ipv4 = source_ipv4;
    packet->destination_ipv4 =
        destination_ipv4;

    packet->has_tcp = true;
    packet->tcp_source_port = source_port;
    packet->tcp_destination_port =
        destination_port;

    return packet_info_mark_complete(packet);
}

/**
 * @brief 验证第一条数据包正确初始化流记录。
 */
static int test_flow_record_initialization(void)
{
    packet_info_t packet;
    flow_record_t record;

    TEST_CHECK(
        prepare_tcp_packet(
            &packet,
            INT64_C(100),
            INT32_C(200),
            UINT32_C(80),
            UINT32_C(100),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    TEST_CHECK(
        flow_record_init(&record, &packet) == 0
    );

    TEST_CHECK(record.initialized);
    TEST_CHECK(
        record.key.endpoint_a.ipv4_address ==
            UINT32_C(0x01010101)
    );
    TEST_CHECK(
        record.key.endpoint_b.ipv4_address ==
            UINT32_C(0x02020202)
    );

    /*
     * 源端点就是规范化后的A，因此第一包属于A→B。
     */
    TEST_CHECK(
        record.a_to_b.packet_count ==
            UINT64_C(1)
    );
    TEST_CHECK(
        record.a_to_b.captured_byte_count ==
            UINT64_C(80)
    );
    TEST_CHECK(
        record.a_to_b.wire_byte_count ==
            UINT64_C(100)
    );

    TEST_CHECK(
        record.b_to_a.packet_count ==
            UINT64_C(0)
    );

    TEST_CHECK(
        record.first_seen.seconds == INT64_C(100)
    );
    TEST_CHECK(
        record.first_seen.microseconds ==
            INT32_C(200)
    );
    TEST_CHECK(
        record.last_seen.seconds == INT64_C(100)
    );
    TEST_CHECK(
        record.last_seen.microseconds ==
            INT32_C(200)
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证正反方向数据包分别累计。
 */
static int test_bidirectional_updates(void)
{
    packet_info_t first_packet;
    packet_info_t reverse_packet;
    packet_info_t third_packet;

    flow_record_t record;

    /*
     * 第一包：A→B，60字节。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &first_packet,
            INT64_C(100),
            INT32_C(100),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    /*
     * 第二包：B→A，80字节。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &reverse_packet,
            INT64_C(101),
            INT32_C(200),
            UINT32_C(70),
            UINT32_C(80),
            UINT32_C(0x02020202),
            UINT16_C(2000),
            UINT32_C(0x01010101),
            UINT16_C(1000)
        ) == 0
    );

    /*
     * 第三包：A→B，100字节。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &third_packet,
            INT64_C(102),
            INT32_C(300),
            UINT32_C(90),
            UINT32_C(100),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    TEST_CHECK(
        flow_record_init(
            &record,
            &first_packet
        ) == 0
    );

    TEST_CHECK(
        flow_record_update(
            &record,
            &reverse_packet
        ) == 0
    );

    TEST_CHECK(
        flow_record_update(
            &record,
            &third_packet
        ) == 0
    );

    TEST_CHECK(
        record.a_to_b.packet_count ==
            UINT64_C(2)
    );
    TEST_CHECK(
        record.a_to_b.captured_byte_count ==
            UINT64_C(150)
    );
    TEST_CHECK(
        record.a_to_b.wire_byte_count ==
            UINT64_C(160)
    );

    TEST_CHECK(
        record.b_to_a.packet_count ==
            UINT64_C(1)
    );
    TEST_CHECK(
        record.b_to_a.captured_byte_count ==
            UINT64_C(70)
    );
    TEST_CHECK(
        record.b_to_a.wire_byte_count ==
            UINT64_C(80)
    );

    TEST_CHECK(
        record.first_seen.seconds == INT64_C(100)
    );
    TEST_CHECK(
        record.last_seen.seconds == INT64_C(102)
    );
    TEST_CHECK(
        record.last_seen.microseconds ==
            INT32_C(300)
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证乱序时间戳仍得到正确时间范围。
 */
static int test_out_of_order_timestamps(void)
{
    packet_info_t middle_packet;
    packet_info_t earlier_packet;
    packet_info_t later_packet;

    flow_record_t record;

    TEST_CHECK(
        prepare_tcp_packet(
            &middle_packet,
            INT64_C(100),
            INT32_C(500),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &earlier_packet,
            INT64_C(99),
            INT32_C(900000),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &later_packet,
            INT64_C(101),
            INT32_C(1),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x02020202),
            UINT16_C(2000),
            UINT32_C(0x01010101),
            UINT16_C(1000)
        ) == 0
    );

    TEST_CHECK(
        flow_record_init(
            &record,
            &middle_packet
        ) == 0
    );

    TEST_CHECK(
        flow_record_update(
            &record,
            &earlier_packet
        ) == 0
    );

    TEST_CHECK(
        flow_record_update(
            &record,
            &later_packet
        ) == 0
    );

    TEST_CHECK(
        record.first_seen.seconds == INT64_C(99)
    );
    TEST_CHECK(
        record.first_seen.microseconds ==
            INT32_C(900000)
    );
    TEST_CHECK(
        record.last_seen.seconds == INT64_C(101)
    );
    TEST_CHECK(
        record.last_seen.microseconds ==
            INT32_C(1)
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证其他连接的数据包不能加入当前记录。
 */
static int test_reject_different_flow(void)
{
    packet_info_t first_packet;
    packet_info_t different_packet;

    flow_record_t record;

    uint64_t original_packet_count;
    flow_timestamp_t original_last_seen;

    TEST_CHECK(
        prepare_tcp_packet(
            &first_packet,
            INT64_C(100),
            INT32_C(0),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    /*
     * 目标端口不同，因此属于另一条流。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &different_packet,
            INT64_C(200),
            INT32_C(0),
            UINT32_C(80),
            UINT32_C(80),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(3000)
        ) == 0
    );

    TEST_CHECK(
        flow_record_init(
            &record,
            &first_packet
        ) == 0
    );

    original_packet_count =
        record.a_to_b.packet_count;
    original_last_seen = record.last_seen;

    TEST_CHECK(
        flow_record_update(
            &record,
            &different_packet
        ) == ENOENT
    );

    /*
     * 更新失败后，计数器和时间都不能变化。
     */
    TEST_CHECK(
        record.a_to_b.packet_count ==
            original_packet_count
    );
    TEST_CHECK(
        record.last_seen.seconds ==
            original_last_seen.seconds
    );
    TEST_CHECK(
        record.last_seen.microseconds ==
            original_last_seen.microseconds
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证计数器溢出保护。
 */
static int test_counter_overflow_protection(void)
{
    packet_info_t first_packet;
    packet_info_t next_packet;

    flow_record_t record;

    TEST_CHECK(
        prepare_tcp_packet(
            &first_packet,
            INT64_C(100),
            INT32_C(0),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &next_packet,
            INT64_C(101),
            INT32_C(0),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    TEST_CHECK(
        flow_record_init(
            &record,
            &first_packet
        ) == 0
    );

    /*
     * 人工把计数器设置到最大值，模拟长期运行后的极端情况。
     */
    record.a_to_b.packet_count = UINT64_MAX;

    TEST_CHECK(
        flow_record_update(
            &record,
            &next_packet
        ) == EOVERFLOW
    );

    /*
     * 失败后计数器不能回绕成0。
     */
    TEST_CHECK(
        record.a_to_b.packet_count == UINT64_MAX
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证TCP状态随同一条流的数据包持续推进。
 */
static int test_tcp_state_updates_with_flow(void)
{
    packet_info_t syn_packet;
    packet_info_t syn_ack_packet;
    packet_info_t ack_packet;

    flow_record_t record;

    /*
     * 第一个包：A到B的SYN。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &syn_packet,
            INT64_C(100),
            INT32_C(0),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    syn_packet.tcp_flags = TCP_FLAG_SYN;

    /*
     * 第二个包：B到A的SYN+ACK。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &syn_ack_packet,
            INT64_C(101),
            INT32_C(0),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x02020202),
            UINT16_C(2000),
            UINT32_C(0x01010101),
            UINT16_C(1000)
        ) == 0
    );

    syn_ack_packet.tcp_flags =
        TCP_FLAG_SYN | TCP_FLAG_ACK;

    /*
     * 第三个包：A到B的最终ACK。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &ack_packet,
            INT64_C(102),
            INT32_C(0),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    ack_packet.tcp_flags = TCP_FLAG_ACK;

    TEST_CHECK(
        flow_record_init(
            &record,
            &syn_packet
        ) == 0
    );

    TEST_CHECK(record.tcp_state.initialized);

    TEST_CHECK(
        record.tcp_state.phase ==
            TCP_FLOW_PHASE_SYN_SEEN
    );

    TEST_CHECK(
        record.tcp_state.initiator_direction ==
            FLOW_DIRECTION_A_TO_B
    );

    TEST_CHECK(
        flow_record_update(
            &record,
            &syn_ack_packet
        ) == 0
    );

    TEST_CHECK(
        record.tcp_state.phase ==
            TCP_FLOW_PHASE_SYN_ACK_SEEN
    );

    TEST_CHECK(
        flow_record_update(
            &record,
            &ack_packet
        ) == 0
    );

    TEST_CHECK(
        record.tcp_state.phase ==
            TCP_FLOW_PHASE_ESTABLISHED
    );

    TEST_CHECK(
        record.tcp_state.handshake_completed
    );

    /*
     * 状态推进不能破坏原有的双向统计。
     */
    TEST_CHECK(
        record.a_to_b.packet_count ==
            UINT64_C(2)
    );

    TEST_CHECK(
        record.b_to_a.packet_count ==
            UINT64_C(1)
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证UDP流不会错误初始化TCP状态。
 */
static int test_udp_flow_has_no_tcp_state(void)
{
    packet_info_t packet;
    flow_record_t record;

    TEST_CHECK(
        packet_info_init(
            &packet,
            INT64_C(200),
            INT32_C(0),
            UINT32_C(60),
            UINT32_C(60)
        ) == 0
    );

    packet.has_ethernet = true;
    packet.has_ipv4 = true;
    packet.ipv4_protocol = IPV4_PROTOCOL_UDP;
    packet.source_ipv4 =
        UINT32_C(0x01010101);
    packet.destination_ipv4 =
        UINT32_C(0x02020202);

    packet.has_udp = true;
    packet.udp_source_port = UINT16_C(3000);
    packet.udp_destination_port =
        UINT16_C(4000);

    TEST_CHECK(
        packet_info_mark_complete(&packet) == 0
    );

    TEST_CHECK(
        flow_record_init(
            &record,
            &packet
        ) == 0
    );

    TEST_CHECK(record.initialized);

    /*
     * phase的零值本身不代表该UDP流处于UNOBSERVED。
     * initialized为false才表示整个TCP状态对象无效。
     */
    TEST_CHECK(!record.tcp_state.initialized);

    TEST_CHECK(
        record.a_to_b.packet_count ==
            UINT64_C(1)
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 单条流记录单元测试入口。
 */
int main(void)
{
    if (test_flow_record_initialization() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] flow record initialization\n");

    if (test_bidirectional_updates() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] bidirectional flow updates\n");

    if (test_out_of_order_timestamps() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] out-of-order timestamps\n");

    if (test_reject_different_flow() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] reject different flow\n");

    if (test_counter_overflow_protection() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] counter overflow protection\n");

    if (test_tcp_state_updates_with_flow() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP state updates with flow\n");

    if (test_udp_flow_has_no_tcp_state() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] UDP flow has no TCP state\n");

    return EXIT_SUCCESS;
}