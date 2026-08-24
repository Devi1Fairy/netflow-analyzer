#include "analyzer/flow_table.h"
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
 * @brief 构造一条用于流表测试的TCP数据包结果。
 */
static int prepare_tcp_packet(
    packet_info_t *packet,
    int64_t timestamp_seconds,
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
        INT32_C(0),
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
 * @brief 验证新数据包创建流，反向数据包更新同一条流。
 */
static int test_create_and_update_flow(void)
{
    flow_record_t storage[4];
    flow_table_t table;

    packet_info_t forward_packet;
    packet_info_t reverse_packet;

    const flow_record_t *record;
    const flow_record_t *first_record_address;

    bool created;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &forward_packet,
            INT64_C(100),
            UINT32_C(60),
            UINT32_C(70),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &reverse_packet,
            INT64_C(101),
            UINT32_C(80),
            UINT32_C(90),
            UINT32_C(0x02020202),
            UINT16_C(2000),
            UINT32_C(0x01010101),
            UINT16_C(1000)
        ) == 0
    );

    /*
     * 第一条数据包创建新流。
     */
    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &forward_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(created);
    TEST_CHECK(flow_table_count(&table) == 1U);
    TEST_CHECK(record == &storage[0]);
    TEST_CHECK(
        record->a_to_b.packet_count ==
            UINT64_C(1)
    );
    TEST_CHECK(
        record->b_to_a.packet_count ==
            UINT64_C(0)
    );

    first_record_address = record;

    /*
     * 反向数据包必须更新同一个数组元素，不能创建第二条流。
     */
    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &reverse_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(!created);
    TEST_CHECK(flow_table_count(&table) == 1U);
    TEST_CHECK(record == first_record_address);

    TEST_CHECK(
        record->a_to_b.packet_count ==
            UINT64_C(1)
    );
    TEST_CHECK(
        record->b_to_a.packet_count ==
            UINT64_C(1)
    );
    TEST_CHECK(
        record->b_to_a.wire_byte_count ==
            UINT64_C(90)
    );

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证不同五元组创建不同流，并能根据键查找。
 */
static int test_multiple_flows_and_find(void)
{
    flow_record_t storage[4];
    flow_table_t table;

    packet_info_t first_packet;
    packet_info_t second_packet;

    flow_key_t second_key;
    flow_direction_t second_direction;

    const flow_record_t *record;
    const flow_record_t *found_record;

    bool created;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &first_packet,
            INT64_C(100),
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
            &second_packet,
            INT64_C(101),
            UINT32_C(70),
            UINT32_C(70),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(3000)
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &first_packet,
            &record,
            &created
        ) == 0
    );
    TEST_CHECK(created);

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &second_packet,
            &record,
            &created
        ) == 0
    );
    TEST_CHECK(created);

    TEST_CHECK(flow_table_count(&table) == 2U);

    TEST_CHECK(
        flow_key_from_packet(
            &second_packet,
            &second_key,
            &second_direction
        ) == 0
    );

    TEST_CHECK(
        flow_table_find(
            &table,
            &second_key,
            &found_record
        ) == 0
    );

    TEST_CHECK(found_record == &storage[1]);
    TEST_CHECK(
        found_record->key.endpoint_b.port ==
            UINT16_C(3000)
    );

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证容量耗尽时不越界，也不增加count。
 */
static int test_capacity_protection(void)
{
    flow_record_t storage[1];
    flow_table_t table;

    packet_info_t first_packet;
    packet_info_t second_packet;

    const flow_record_t *record;
    bool created;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &first_packet,
            INT64_C(100),
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
            &second_packet,
            INT64_C(101),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(3000)
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &first_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(flow_table_count(&table) == 1U);

    /*
     * 设置哨兵值，用于验证失败时输出参数不被修改。
     */
    record = NULL;
    created = true;

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &second_packet,
            &record,
            &created
        ) == ENOSPC
    );

    TEST_CHECK(flow_table_count(&table) == 1U);
    TEST_CHECK(record == NULL);
    TEST_CHECK(created);

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证按下标遍历和清理后的状态。
 */
static int test_iteration_and_cleanup(void)
{
    flow_record_t storage[2];
    flow_table_t table;

    packet_info_t packet;
    const flow_record_t *record;
    bool created;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &packet,
            INT64_C(100),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2000)
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(
        flow_table_get(
            &table,
            0U,
            &record
        ) == 0
    );
    TEST_CHECK(record == &storage[0]);

    TEST_CHECK(
        flow_table_get(
            &table,
            1U,
            &record
        ) == ERANGE
    );

    flow_table_cleanup(&table);

    TEST_CHECK(!table.initialized);
    TEST_CHECK(table.records == NULL);
    TEST_CHECK(flow_table_count(&table) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 固定容量流表单元测试入口。
 */
int main(void)
{
    if (test_create_and_update_flow() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] create and update flow\n");

    if (test_multiple_flows_and_find() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] multiple flows and find\n");

    if (test_capacity_protection() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] flow table capacity protection\n");

    if (test_iteration_and_cleanup() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] flow table iteration and cleanup\n");

    return EXIT_SUCCESS;
}