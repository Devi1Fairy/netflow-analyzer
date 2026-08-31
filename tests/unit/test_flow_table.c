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
    flow_table_slot_t storage[4];
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
    /*
    * 哈希表中的物理位置由哈希值决定，
    * 因此不能假定第一条流必定位于storage[0]。
    */
    TEST_CHECK(record != NULL);
    TEST_CHECK(record->initialized);
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
    flow_table_slot_t storage[4];
    flow_table_t table;

    packet_info_t first_packet;
    packet_info_t second_packet;

    flow_key_t second_key;
    flow_direction_t second_direction;

    const flow_record_t *record;
    const flow_record_t *found_record;
    const flow_record_t *second_record_address;

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
    second_record_address = record;

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

    TEST_CHECK(found_record == second_record_address);
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
    flow_table_slot_t  storage[1];
    flow_table_t table;

    packet_info_t first_packet;
    packet_info_t second_packet;
    flow_table_probe_statistics_t probe_statistics;

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

    TEST_CHECK(
    flow_table_get_probe_statistics(
        &table,
        &probe_statistics
    ) == 0
    );

    TEST_CHECK(
        probe_statistics.packet_operation_count ==
            UINT64_C(1)
    );

    TEST_CHECK(
        probe_statistics.total_inspected_slot_count ==
            UINT64_C(1)
    );

    TEST_CHECK(
        probe_statistics.maximum_probe_length == 1U
    );

    TEST_CHECK(!probe_statistics.counters_saturated);

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

    TEST_CHECK(
    flow_table_get_probe_statistics(
        &table,
        &probe_statistics
    ) == 0
    );

    TEST_CHECK(
        probe_statistics.packet_operation_count ==
            UINT64_C(2)
    );

    TEST_CHECK(
        probe_statistics.total_inspected_slot_count ==
            UINT64_C(2)
    );

    TEST_CHECK(
        probe_statistics.maximum_probe_length == 1U
    );

    TEST_CHECK(!probe_statistics.counters_saturated);

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证按逻辑下标遍历流表，并验证清理后的状态。
 */
static int test_iteration_and_cleanup(void)
{
    flow_table_slot_t storage[2];
    flow_table_t table;

    packet_info_t packet;

    const flow_record_t *record;
    const flow_record_t *inserted_record;

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

    TEST_CHECK(created);
    TEST_CHECK(record != NULL);

    /*
     * 在调用flow_table_get之前保存插入接口返回的地址。
     *
     * 这样才能验证get取得的确实是原来的内部流记录。
     */
    inserted_record = record;

    TEST_CHECK(
        flow_table_get(
            &table,
            0U,
            &record
        ) == 0
    );

    TEST_CHECK(record == inserted_record);

    /*
     * 流表只有一条记录，逻辑下标1应该越界。
     */
    TEST_CHECK(
        flow_table_get(
            &table,
            1U,
            &record
        ) == ERANGE
    );

    /*
     * 接口失败时不应修改record输出参数。
     */
    TEST_CHECK(record == inserted_record);

    flow_table_cleanup(&table);

    TEST_CHECK(!table.initialized);
    TEST_CHECK(table.slots == NULL);
    TEST_CHECK(flow_table_count(&table) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证过期流被清理、活动流保持顺序，并能复用容量。
 */
static int test_expire_flows_and_reuse_capacity(void)
{
    flow_table_slot_t  storage[3];
    flow_table_t table;

    flow_key_t old_key;
    flow_key_t active_key;
    flow_key_t boundary_key;

    flow_direction_t direction;
    const flow_record_t *found_record;

    packet_info_t old_packet;
    packet_info_t active_packet;
    packet_info_t boundary_packet;
    packet_info_t new_packet;

    const flow_record_t *record;
    flow_timestamp_t cutoff;

    size_t expired_count;
    bool created;

    flow_record_t expired_records[3];

    size_t expired_index;
    bool old_snapshot_found;
    bool boundary_snapshot_found;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    /*
     * 第一条流最后活动于100秒，应该过期。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &old_packet,
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
     * 第二条流最后活动于300秒，应该保留。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &active_packet,
            INT64_C(300),
            UINT32_C(70),
            UINT32_C(70),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(3000)
        ) == 0
    );

    /*
     * 第三条流正好位于200秒截止时间。
     * 当前规则使用<=，因此它也应该过期。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &boundary_packet,
            INT64_C(200),
            UINT32_C(80),
            UINT32_C(80),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(4000)
        ) == 0
    );

    /*
    * 保存三条流的键，过期清理后通过公开查找接口验证结果。
    */
    TEST_CHECK(
        flow_key_from_packet(
            &old_packet,
            &old_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &active_packet,
            &active_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &boundary_packet,
            &boundary_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &old_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &active_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &boundary_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(flow_table_count(&table) == 3U);

    cutoff = (flow_timestamp_t){
        .seconds = INT64_C(200),
        .microseconds = INT32_C(0)
    };

    TEST_CHECK(
        flow_table_expire_before(
            &table,
            &cutoff,
            expired_records,
            sizeof(expired_records) /
                sizeof(expired_records[0]),
            &expired_count
        ) == 0
    );

    /*
    * 100秒的旧流应当已经被删除。
    */
    TEST_CHECK(
        flow_table_find(
            &table,
            &old_key,
            &found_record
        ) == ENOENT
    );

    /*
    * 正好位于200秒边界的流也应删除，因为规则使用<=。
    */
    TEST_CHECK(
        flow_table_find(
            &table,
            &boundary_key,
            &found_record
        ) == ENOENT
    );

    /*
    * 300秒的活动流必须继续存在。
    */
    TEST_CHECK(
        flow_table_find(
            &table,
            &active_key,
            &found_record
        ) == 0
    );

    TEST_CHECK(found_record != NULL);
    TEST_CHECK(
        found_record->key.endpoint_b.port ==
            UINT16_C(3000)
    );
    
    TEST_CHECK(expired_count == 2U);
    TEST_CHECK(flow_table_count(&table) == 1U);

    old_snapshot_found = false;
    boundary_snapshot_found = false;

    for (expired_index = 0U; expired_index < expired_count; expired_index += 1U) {
        
        TEST_CHECK(expired_records[expired_index].initialized);

        if (flow_key_equal(
                &expired_records[expired_index].key,
                &old_key)) {
            old_snapshot_found = true;
        }

        if (flow_key_equal(
                &expired_records[expired_index].key,
                &boundary_key)) {
            boundary_snapshot_found = true;
        }
    }

    TEST_CHECK(old_snapshot_found);
    TEST_CHECK(boundary_snapshot_found);

    /*
     * 清理过期流后，流表重新拥有空闲容量。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &new_packet,
            INT64_C(400),
            UINT32_C(90),
            UINT32_C(90),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(5000)
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &new_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(created);
    TEST_CHECK(flow_table_count(&table) == 2U);
    TEST_CHECK(record != NULL);
    TEST_CHECK(record->key.endpoint_b.port == UINT16_C(5000));

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证无效截止时间不会修改流表或输出参数。
 */
static int test_expire_argument_validation(void)
{
    flow_table_slot_t  storage[1];
    flow_table_t table;

    packet_info_t packet;
    const flow_record_t *record;
    const flow_record_t *stored_record;
    flow_record_t expired_records[1];

    flow_timestamp_t invalid_cutoff;
    flow_timestamp_t valid_cutoff;

    size_t expired_count;
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
    stored_record = record;

    /*
     * 微秒最大只能是999999。
     */
    invalid_cutoff = (flow_timestamp_t){
        .seconds = INT64_C(200),
        .microseconds = INT32_C(1000000)
    };

    /*
     * 哨兵值用于确认失败时没有修改输出参数。
     */
    expired_count = 99U;

    TEST_CHECK(
        flow_table_expire_before(
            &table,
            &invalid_cutoff,
            expired_records,
            sizeof(expired_records) /
                sizeof(expired_records[0]),
            &expired_count
        ) == EINVAL
    );

    TEST_CHECK(expired_count == 99U);
    TEST_CHECK(flow_table_count(&table) == 1U);
    TEST_CHECK(stored_record->initialized);

    valid_cutoff = (flow_timestamp_t){
        .seconds = INT64_C(200),
        .microseconds = INT32_C(0)
    };

    /*
     * expired_count不能为空。
     */
    TEST_CHECK(
        flow_table_expire_before(
            &table,
            &valid_cutoff,
            expired_records,
            sizeof(expired_records) /
                sizeof(expired_records[0]),
            NULL
        ) == EINVAL
    );

    TEST_CHECK(flow_table_count(&table) == 1U);

    expired_count = 99U;

    TEST_CHECK(
        flow_table_expire_before(
            &table,
            &valid_cutoff,
            NULL,
            1U,
            &expired_count
        ) == EINVAL
    );

    TEST_CHECK(expired_count == 99U);
    TEST_CHECK(flow_table_count(&table) == 1U);
    TEST_CHECK(stored_record->initialized);

    expired_count = 99U;

    TEST_CHECK(
        flow_table_expire_before(
            &table,
            &valid_cutoff,
            expired_records,
            0U,
            &expired_count
        ) == ENOSPC
    );

    TEST_CHECK(expired_count == 99U);
    TEST_CHECK(flow_table_count(&table) == 1U);
    TEST_CHECK(stored_record->initialized);

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证哈希冲突、数组回绕、DELETED探测和槽位复用。
 *
 * 三条测试流的目标端口经过选择，会在容量为4的流表中
 * 产生相同的哈希起点。
 */
static int test_collision_wraparound_and_deleted_slot(void)
{
    flow_table_slot_t storage[4];
    flow_table_t table;

    packet_info_t first_packet;
    packet_info_t second_packet;
    packet_info_t third_packet;

    flow_key_t first_key;
    flow_key_t second_key;
    flow_key_t third_key;

    flow_direction_t direction;

    const flow_record_t *record;
    const flow_record_t *second_record;
    const flow_record_t *found_record;
    flow_record_t expired_records[1];

    flow_timestamp_t cutoff;

    flow_table_probe_statistics_t probe_statistics;

    uint64_t first_hash;
    uint64_t second_hash;
    uint64_t third_hash;

    size_t first_index;
    size_t second_index;
    size_t expired_count;

    bool created;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    TEST_CHECK(
    flow_table_get_probe_statistics(
        &table,
        &probe_statistics
        ) == 0
    );

    TEST_CHECK(
        probe_statistics.packet_operation_count ==
            UINT64_C(0)
    );

    TEST_CHECK(
        probe_statistics.total_inspected_slot_count ==
            UINT64_C(0)
    );

    TEST_CHECK(
        probe_statistics.maximum_probe_length == 0U
    );

    TEST_CHECK(!probe_statistics.counters_saturated);

    /*
     * 2002、2006和2010在容量为4时会产生相同哈希起点。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &first_packet,
            INT64_C(100),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2002)
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &second_packet,
            INT64_C(300),
            UINT32_C(70),
            UINT32_C(70),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2006)
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &third_packet,
            INT64_C(400),
            UINT32_C(80),
            UINT32_C(80),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(2010)
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &first_packet,
            &first_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &second_packet,
            &second_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &third_packet,
            &third_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_hash(&first_key, &first_hash) == 0
    );
    TEST_CHECK(
        flow_key_hash(&second_key, &second_hash) == 0
    );
    TEST_CHECK(
        flow_key_hash(&third_key, &third_hash) == 0
    );

    first_index = (size_t)(
        first_hash %
        (uint64_t)(sizeof(storage) / sizeof(storage[0]))
    );

    TEST_CHECK(
        first_index ==
        (size_t)(
            second_hash %
            (uint64_t)(sizeof(storage) / sizeof(storage[0]))
        )
    );

    TEST_CHECK(
        first_index ==
        (size_t)(
            third_hash %
            (uint64_t)(sizeof(storage) / sizeof(storage[0]))
        )
    );

    /*
     * 这些测试数据的起始槽位应为数组最后一个槽位。
     * 第二条流发生冲突后必须绕回storage[0]。
     */
    TEST_CHECK(
        first_index ==
        (sizeof(storage) / sizeof(storage[0])) - 1U
    );

    second_index = 0U;

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
        record == &storage[first_index].record
    );

    TEST_CHECK(
    flow_table_get_probe_statistics(
        &table,
        &probe_statistics
        ) == 0
    );

    TEST_CHECK(
        probe_statistics.packet_operation_count ==
            UINT64_C(1)
    );

    TEST_CHECK(
        probe_statistics.total_inspected_slot_count ==
            UINT64_C(1)
    );

    TEST_CHECK(
        probe_statistics.maximum_probe_length == 1U
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &second_packet,
            &record,
            &created
        ) == 0
    );
    TEST_CHECK(created);
    TEST_CHECK(
        record == &storage[second_index].record
    );

    second_record = record;

    TEST_CHECK(
    flow_table_get_probe_statistics(
        &table,
        &probe_statistics
        ) == 0
    );

    TEST_CHECK(
        probe_statistics.packet_operation_count ==
            UINT64_C(2)
    );

    TEST_CHECK(
        probe_statistics.total_inspected_slot_count ==
            UINT64_C(3)
    );

    TEST_CHECK(
        probe_statistics.maximum_probe_length == 2U
    );

    /*
     * 删除第一条流，使哈希起点变成DELETED。
     */
    cutoff = (flow_timestamp_t){
        .seconds = INT64_C(100),
        .microseconds = INT32_C(0)
    };

    TEST_CHECK(
        flow_table_expire_before(
            &table,
            &cutoff,
            expired_records,
            sizeof(expired_records) /
                sizeof(expired_records[0]),
            &expired_count
        ) == 0
    );

    TEST_CHECK(expired_count == 1U);
    TEST_CHECK(flow_table_count(&table) == 1U);
    TEST_CHECK(
        storage[first_index].state ==
        FLOW_TABLE_SLOT_DELETED
    );

    /*
     * 查找不能在DELETED槽位停止，必须继续绕回storage[0]。
     */
    TEST_CHECK(
        flow_table_find(
            &table,
            &second_key,
            &found_record
        ) == 0
    );

    TEST_CHECK(found_record == second_record);

    TEST_CHECK(
    flow_table_get_probe_statistics(
        &table,
        &probe_statistics
        ) == 0
    );

    TEST_CHECK(
        probe_statistics.packet_operation_count ==
            UINT64_C(2)
    );

    TEST_CHECK(
        probe_statistics.total_inspected_slot_count ==
            UINT64_C(3)
    );

    TEST_CHECK(
        probe_statistics.maximum_probe_length == 2U
    );

    /*
     * 第三条冲突流应优先复用前面的DELETED槽位。
     */
    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &third_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(created);
    TEST_CHECK(flow_table_count(&table) == 2U);
    TEST_CHECK(
        record == &storage[first_index].record
    );
    TEST_CHECK(
        storage[first_index].state ==
        FLOW_TABLE_SLOT_OCCUPIED
    );

    TEST_CHECK(
    flow_table_get_probe_statistics(
        &table,
        &probe_statistics
        ) == 0
    );

    TEST_CHECK(
        probe_statistics.packet_operation_count ==
            UINT64_C(3)
    );

    TEST_CHECK(
        probe_statistics.total_inspected_slot_count ==
            UINT64_C(6)
    );

    TEST_CHECK(
        probe_statistics.maximum_probe_length == 3U
    );

    TEST_CHECK(!probe_statistics.counters_saturated);

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证探测统计查询、饱和保护和失败输出不变语义。
 */
static int test_probe_statistics_saturation_and_validation(void)
{
    flow_table_slot_t storage[1];
    flow_table_t table;

    packet_info_t packet;
    const flow_record_t *record;

    flow_table_probe_statistics_t statistics;

    const flow_table_probe_statistics_t sentinel_statistics = {
        .packet_operation_count = UINT64_C(11),
        .total_inspected_slot_count = UINT64_C(22),
        .maximum_probe_length = 7U,
        .counters_saturated = true
    };

    bool created;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    /*
     * 新初始化流表的统计必须全部为0。
     */
    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics
        ) == 0
    );

    TEST_CHECK(
        statistics.packet_operation_count ==
            UINT64_C(0)
    );

    TEST_CHECK(
        statistics.total_inspected_slot_count ==
            UINT64_C(0)
    );

    TEST_CHECK(statistics.maximum_probe_length == 0U);
    TEST_CHECK(!statistics.counters_saturated);

    /*
     * 参数错误不能修改调用者原有输出。
     */
    statistics = sentinel_statistics;

    TEST_CHECK(
        flow_table_get_probe_statistics(
            NULL,
            &statistics
        ) == EINVAL
    );

    TEST_CHECK(
        statistics.packet_operation_count ==
            sentinel_statistics.packet_operation_count
    );

    TEST_CHECK(
        statistics.total_inspected_slot_count ==
            sentinel_statistics.total_inspected_slot_count
    );

    TEST_CHECK(
        statistics.maximum_probe_length ==
            sentinel_statistics.maximum_probe_length
    );

    TEST_CHECK(
        statistics.counters_saturated ==
            sentinel_statistics.counters_saturated
    );

    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            NULL
        ) == EINVAL
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

    /*
     * 测试无法真的执行UINT64_MAX次操作，因此直接把内部累计值
     * 放到上限前一位，再通过公开数据包接口触发真实累计逻辑。
     */
    table.probe_statistics = (flow_table_probe_statistics_t){
        .packet_operation_count =
            UINT64_MAX - UINT64_C(1),

        .total_inspected_slot_count =
            UINT64_MAX - UINT64_C(1),

        .maximum_probe_length = 0U,
        .counters_saturated = false
    };

    /*
     * 第一次操作使两个累计值正好达到UINT64_MAX。
     */
    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(created);

    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics
        ) == 0
    );

    TEST_CHECK(
        statistics.packet_operation_count ==
            UINT64_MAX
    );

    TEST_CHECK(
        statistics.total_inspected_slot_count ==
            UINT64_MAX
    );

    TEST_CHECK(statistics.maximum_probe_length == 1U);
    TEST_CHECK(statistics.counters_saturated);

    /*
     * 再处理同一条流。业务记录仍应更新，但统计不能回绕。
     */
    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(!created);
    TEST_CHECK(record != NULL);

    TEST_CHECK(
        record->a_to_b.packet_count ==
            UINT64_C(2)
    );

    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics
        ) == 0
    );

    TEST_CHECK(
        statistics.packet_operation_count ==
            UINT64_MAX
    );

    TEST_CHECK(
        statistics.total_inspected_slot_count ==
            UINT64_MAX
    );

    TEST_CHECK(statistics.maximum_probe_length == 1U);
    TEST_CHECK(statistics.counters_saturated);

    /*
     * 最大探测长度超过capacity代表内部状态损坏。
     * 查询必须失败并保持输出哨兵值。
     */
    table.probe_statistics.maximum_probe_length =
        table.capacity + 1U;

    statistics = sentinel_statistics;

    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics
        ) == EINVAL
    );

    TEST_CHECK(
        statistics.packet_operation_count ==
            sentinel_statistics.packet_operation_count
    );

    TEST_CHECK(
        statistics.total_inspected_slot_count ==
            sentinel_statistics.total_inspected_slot_count
    );

    TEST_CHECK(
        statistics.maximum_probe_length ==
            sentinel_statistics.maximum_probe_length
    );

    TEST_CHECK(
        statistics.counters_saturated ==
            sentinel_statistics.counters_saturated
    );

    flow_table_cleanup(&table);

    /*
     * cleanup后的流表已经失效，查询仍不能修改输出。
     */
    statistics = sentinel_statistics;

    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics
        ) == EINVAL
    );

    TEST_CHECK(
        statistics.packet_operation_count ==
            sentinel_statistics.packet_operation_count
    );

    TEST_CHECK(
        statistics.total_inspected_slot_count ==
            sentinel_statistics.total_inspected_slot_count
    );

    TEST_CHECK(
        statistics.maximum_probe_length ==
            sentinel_statistics.maximum_probe_length
    );

    TEST_CHECK(
        statistics.counters_saturated ==
            sentinel_statistics.counters_saturated
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证按last_seen驱逐最旧流并复用释放容量。
 */
static int test_evict_oldest_and_reuse_capacity(void)
{
    flow_table_slot_t storage[3];
    flow_table_t table;

    packet_info_t first_packet;
    packet_info_t second_packet;
    packet_info_t third_packet;
    packet_info_t first_refresh_packet;
    packet_info_t replacement_packet;

    flow_key_t first_key;
    flow_key_t second_key;
    flow_key_t third_key;
    flow_direction_t direction;

    flow_record_t evicted_record;

    flow_table_probe_statistics_t statistics_before;
    flow_table_probe_statistics_t statistics_after;

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

    TEST_CHECK(
        prepare_tcp_packet(
            &second_packet,
            INT64_C(200),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(3000)
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &third_packet,
            INT64_C(300),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(4000)
        ) == 0
    );

    /*
     * 第一条流虽然最早创建，但在400秒再次活动。
     * 驱逐选择必须依据last_seen，而不是first_seen。
     */
    TEST_CHECK(
        prepare_tcp_packet(
            &first_refresh_packet,
            INT64_C(400),
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
            &replacement_packet,
            INT64_C(500),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(5000)
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &first_packet,
            &first_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &second_packet,
            &second_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &third_packet,
            &third_key,
            &direction
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

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &second_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &third_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &first_refresh_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(!created);
    TEST_CHECK(flow_table_count(&table) == 3U);

    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics_before
        ) == 0
    );

    TEST_CHECK(
        flow_table_evict_oldest(
            &table,
            &evicted_record
        ) == 0
    );

    TEST_CHECK(evicted_record.initialized);
    TEST_CHECK(
        flow_key_equal(
            &evicted_record.key,
            &second_key
        )
    );

    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(200)
    );

    TEST_CHECK(flow_table_count(&table) == 2U);

    /*
     * 第二条流最旧，应被移除。
     */
    TEST_CHECK(
        flow_table_find(
            &table,
            &second_key,
            &found_record
        ) == ENOENT
    );

    /*
     * 第一条流已经刷新到400秒，不能被错误驱逐。
     */
    TEST_CHECK(
        flow_table_find(
            &table,
            &first_key,
            &found_record
        ) == 0
    );

    TEST_CHECK(
        found_record->last_seen.seconds ==
            INT64_C(400)
    );

    TEST_CHECK(
        flow_table_find(
            &table,
            &third_key,
            &found_record
        ) == 0
    );

    /*
     * 管理性驱逐不属于数据包哈希探测，
     * 因此不能改变探测统计。
     */
    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics_after
        ) == 0
    );

    TEST_CHECK(
        statistics_after.packet_operation_count ==
            statistics_before.packet_operation_count
    );

    TEST_CHECK(
        statistics_after.total_inspected_slot_count ==
            statistics_before.total_inspected_slot_count
    );

    TEST_CHECK(
        statistics_after.maximum_probe_length ==
            statistics_before.maximum_probe_length
    );

    TEST_CHECK(
        statistics_after.counters_saturated ==
            statistics_before.counters_saturated
    );

    /*
     * 新流必须通过正常哈希探测复用DELETED容量。
     */
    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &replacement_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(created);
    TEST_CHECK(flow_table_count(&table) == 3U);

    /*
     * 输出是值副本，原槽位复用后仍保持完整。
     */
    TEST_CHECK(
        flow_key_equal(
            &evicted_record.key,
            &second_key
        )
    );

    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(200)
    );

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证满表数据包通过一次探测原位淘汰最旧流。
 */
static int test_process_packet_with_oldest_eviction(void)
{
    flow_table_slot_t storage[2];
    flow_table_t table;

    packet_info_t first_packet;
    packet_info_t second_packet;
    packet_info_t replacement_packet;

    flow_key_t first_key;
    flow_key_t second_key;
    flow_key_t replacement_key;
    flow_direction_t direction;

    flow_record_t evicted_record;

    const flow_record_t sentinel_record = {
        .last_seen = {
            .seconds = INT64_C(777),
            .microseconds = INT32_C(123456)
        },
        .initialized = true
    };

    const flow_record_t *record;
    const flow_record_t *found_record;

    flow_table_probe_statistics_t statistics_before;
    flow_table_probe_statistics_t statistics_after;

    bool created;
    bool evicted;

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
            INT64_C(200),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(3000)
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &replacement_packet,
            INT64_C(300),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(4000)
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &first_packet,
            &first_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &second_packet,
            &second_key,
            &direction
        ) == 0
    );

    TEST_CHECK(
        flow_key_from_packet(
            &replacement_packet,
            &replacement_key,
            &direction
        ) == 0
    );

    /*
     * 表未满时使用新接口，不应产生淘汰。
     */
    evicted_record = sentinel_record;
    evicted = true;

    TEST_CHECK(
        flow_table_process_packet_with_oldest_eviction(
            &table,
            &first_packet,
            &record,
            &created,
            &evicted_record,
            &evicted
        ) == 0
    );

    TEST_CHECK(created);
    TEST_CHECK(!evicted);
    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(777)
    );

    evicted_record = sentinel_record;
    evicted = true;

    TEST_CHECK(
        flow_table_process_packet_with_oldest_eviction(
            &table,
            &second_packet,
            &record,
            &created,
            &evicted_record,
            &evicted
        ) == 0
    );

    TEST_CHECK(created);
    TEST_CHECK(!evicted);
    TEST_CHECK(flow_table_count(&table) == 2U);

    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics_before
        ) == 0
    );

    /*
     * 第三条不同五元组进入满表：
     * first_packet的last_seen最早，必须被原位淘汰。
     */
    evicted_record = sentinel_record;
    evicted = false;

    TEST_CHECK(
        flow_table_process_packet_with_oldest_eviction(
            &table,
            &replacement_packet,
            &record,
            &created,
            &evicted_record,
            &evicted
        ) == 0
    );

    TEST_CHECK(created);
    TEST_CHECK(evicted);
    TEST_CHECK(flow_table_count(&table) == 2U);

    TEST_CHECK(
        flow_key_equal(
            &evicted_record.key,
            &first_key
        )
    );

    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(100)
    );

    TEST_CHECK(
        flow_key_equal(
            &record->key,
            &replacement_key
        )
    );

    /*
     * 原位淘汰只产生一次数据包探测。
     * 满容量为2，因此本次检查恰好2个槽位。
     */
    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics_after
        ) == 0
    );

    TEST_CHECK(
        statistics_after.packet_operation_count ==
            statistics_before.packet_operation_count +
                UINT64_C(1)
    );

    TEST_CHECK(
        statistics_after.total_inspected_slot_count ==
            statistics_before.total_inspected_slot_count +
                UINT64_C(2)
    );

    TEST_CHECK(
        statistics_after.maximum_probe_length == 2U
    );

    /*
     * 最旧流已经不存在，其他旧流和替换流仍能正常查找。
     */
    TEST_CHECK(
        flow_table_find(
            &table,
            &first_key,
            &found_record
        ) == ENOENT
    );

    TEST_CHECK(
        flow_table_find(
            &table,
            &second_key,
            &found_record
        ) == 0
    );

    TEST_CHECK(
        flow_table_find(
            &table,
            &replacement_key,
            &found_record
        ) == 0
    );

    /*
     * 满表中已有流再次到达时只更新，不能错误淘汰其他流。
     */
    evicted_record = sentinel_record;
    evicted = true;

    TEST_CHECK(
        flow_table_process_packet_with_oldest_eviction(
            &table,
            &replacement_packet,
            &record,
            &created,
            &evicted_record,
            &evicted
        ) == 0
    );

    TEST_CHECK(!created);
    TEST_CHECK(!evicted);
    TEST_CHECK(flow_table_count(&table) == 2U);
    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(777)
    );

    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证组合处理接口的参数检查和失败不修改语义。
 */
static int
test_process_packet_with_oldest_eviction_validation(void)
{
    flow_table_slot_t storage[1];
    flow_table_t table;

    packet_info_t valid_packet;
    packet_info_t replacement_packet;
    packet_info_t invalid_packet = {0};

    flow_record_t evicted_record;
    flow_record_t stored_record;

    const flow_record_t sentinel_record = {
        .last_seen = {
            .seconds = INT64_C(777),
            .microseconds = INT32_C(123456)
        },
        .initialized = true
    };

    const flow_record_t *record;

    flow_table_probe_statistics_t statistics_before;
    flow_table_probe_statistics_t statistics_after;

    bool created;
    bool evicted;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    TEST_CHECK(
        prepare_tcp_packet(
            &valid_packet,
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
            &replacement_packet,
            INT64_C(200),
            UINT32_C(60),
            UINT32_C(60),
            UINT32_C(0x01010101),
            UINT16_C(1000),
            UINT32_C(0x02020202),
            UINT16_C(3000)
        ) == 0
    );

    /*
     * invalid_packet没有经过packet_info_init和协议解析，
     * 不能生成有效流键。
     *
     * 失败时四个输出都必须保持哨兵值，流表仍为空。
     */
    record = &sentinel_record;
    created = true;
    evicted_record = sentinel_record;
    evicted = true;

    TEST_CHECK(
        flow_table_process_packet_with_oldest_eviction(
            &table,
            &invalid_packet,
            &record,
            &created,
            &evicted_record,
            &evicted
        ) == EINVAL
    );

    TEST_CHECK(record == &sentinel_record);
    TEST_CHECK(created);
    TEST_CHECK(evicted);
    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(777)
    );
    TEST_CHECK(flow_table_count(&table) == 0U);

    /*
     * 新接口必须提供用于接收被淘汰记录的值对象。
     * 参数检查失败发生在哈希探测和流表修改之前。
     */
    record = &sentinel_record;
    created = true;
    evicted = true;

    TEST_CHECK(
        flow_table_process_packet_with_oldest_eviction(
            &table,
            &valid_packet,
            &record,
            &created,
            NULL,
            &evicted
        ) == EINVAL
    );

    TEST_CHECK(record == &sentinel_record);
    TEST_CHECK(created);
    TEST_CHECK(evicted);
    TEST_CHECK(flow_table_count(&table) == 0U);

    /*
     * 是否发生淘汰的布尔输出同样不能为空。
     */
    record = &sentinel_record;
    created = true;
    evicted_record = sentinel_record;

    TEST_CHECK(
        flow_table_process_packet_with_oldest_eviction(
            &table,
            &valid_packet,
            &record,
            &created,
            &evicted_record,
            NULL
        ) == EINVAL
    );

    TEST_CHECK(record == &sentinel_record);
    TEST_CHECK(created);
    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(777)
    );
    TEST_CHECK(flow_table_count(&table) == 0U);

    /*
     * 先正常填满容量为1的流表。
     */
    TEST_CHECK(
        flow_table_process_packet(
            &table,
            &valid_packet,
            &record,
            &created
        ) == 0
    );

    TEST_CHECK(created);
    TEST_CHECK(flow_table_count(&table) == 1U);

    /*
     * 保存槽位值副本，用于确认深层验证失败后没有覆盖旧流。
     */
    stored_record = storage[0].record;

    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics_before
        ) == 0
    );

    /*
     * 人为制造count与实际OCCUPIED槽位数量不一致：
     *
     * 物理槽位有1条流，但table.count错误地记录为0。
     *
     * replacement_packet不属于已有流，所以probe会完整扫描，
     * 随后发现occupied_slot_count与table->count不一致。
     */
    table.count = 0U;

    record = &sentinel_record;
    created = true;
    evicted_record = sentinel_record;
    evicted = true;

    TEST_CHECK(
        flow_table_process_packet_with_oldest_eviction(
            &table,
            &replacement_packet,
            &record,
            &created,
            &evicted_record,
            &evicted
        ) == EINVAL
    );

    /*
     * 调用者输出保持不变。
     */
    TEST_CHECK(record == &sentinel_record);
    TEST_CHECK(created);
    TEST_CHECK(evicted);
    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(777)
    );

    /*
     * 原槽位仍然保存调用前的旧流，没有被新记录覆盖。
     */
    TEST_CHECK(
        storage[0].state ==
            FLOW_TABLE_SLOT_OCCUPIED
    );
    TEST_CHECK(storage[0].record.initialized);
    TEST_CHECK(
        flow_key_equal(
            &storage[0].record.key,
            &stored_record.key
        )
    );
    TEST_CHECK(
        storage[0].record.last_seen.seconds ==
            stored_record.last_seen.seconds
    );

    /*
     * 内部一致性错误没有形成可靠的完整数据包操作，
     * 因此不能污染探测统计。
     */
    TEST_CHECK(
        flow_table_get_probe_statistics(
            &table,
            &statistics_after
        ) == 0
    );

    TEST_CHECK(
        statistics_after.packet_operation_count ==
            statistics_before.packet_operation_count
    );
    TEST_CHECK(
        statistics_after.total_inspected_slot_count ==
            statistics_before.total_inspected_slot_count
    );
    TEST_CHECK(
        statistics_after.maximum_probe_length ==
            statistics_before.maximum_probe_length
    );
    TEST_CHECK(
        statistics_after.counters_saturated ==
            statistics_before.counters_saturated
    );

    /*
     * 测试人为破坏了count，清理前先恢复合法状态。
     */
    table.count = 1U;
    flow_table_cleanup(&table);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证空表、无效参数、内部不一致和最后一条流驱逐。
 */
static int test_evict_oldest_validation(void)
{
    flow_table_slot_t storage[1];
    flow_table_t table;

    packet_info_t packet;
    const flow_record_t *record;

    flow_record_t evicted_record;

    const flow_record_t sentinel_record = {
        .last_seen = {
            .seconds = INT64_C(777),
            .microseconds = INT32_C(123456)
        },
        .initialized = true
    };

    bool created;

    TEST_CHECK(
        flow_table_init(
            &table,
            storage,
            sizeof(storage) / sizeof(storage[0])
        ) == 0
    );

    /*
     * 空表返回ENOENT，并保持输出哨兵不变。
     */
    evicted_record = sentinel_record;

    TEST_CHECK(
        flow_table_evict_oldest(
            &table,
            &evicted_record
        ) == ENOENT
    );

    TEST_CHECK(evicted_record.initialized);
    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(777)
    );

    TEST_CHECK(
        flow_table_evict_oldest(
            NULL,
            &evicted_record
        ) == EINVAL
    );

    TEST_CHECK(
        flow_table_evict_oldest(
            &table,
            NULL
        ) == EINVAL
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

    /*
     * 制造count与OCCUPIED槽数量不一致。
     * 驱逐必须先发现错误，不能部分删除。
     */
    table.count = 0U;
    evicted_record = sentinel_record;

    TEST_CHECK(
        flow_table_evict_oldest(
            &table,
            &evicted_record
        ) == EINVAL
    );

    TEST_CHECK(evicted_record.initialized);
    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(777)
    );

    table.count = 1U;

    /*
     * 驱逐最后一条流后，全部槽位应恢复为EMPTY。
     */
    TEST_CHECK(
        flow_table_evict_oldest(
            &table,
            &evicted_record
        ) == 0
    );

    TEST_CHECK(flow_table_count(&table) == 0U);
    TEST_CHECK(
        storage[0].state ==
            FLOW_TABLE_SLOT_EMPTY
    );

    flow_table_cleanup(&table);

    evicted_record = sentinel_record;

    TEST_CHECK(
        flow_table_evict_oldest(
            &table,
            &evicted_record
        ) == EINVAL
    );

    TEST_CHECK(
        evicted_record.last_seen.seconds ==
            INT64_C(777)
    );

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

    if (test_expire_flows_and_reuse_capacity() !=
    EXIT_SUCCESS) {
    return EXIT_FAILURE;
    }

    printf("[PASS] expire flows and reuse capacity\n");

    if (test_expire_argument_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] expire argument validation\n");

    if (test_collision_wraparound_and_deleted_slot() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf(
        "[PASS] hash collision, wraparound and deleted slot\n"
    );

    if (test_probe_statistics_saturation_and_validation() !=
    EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf(
        "[PASS] probe statistics saturation and validation\n"
    );

    if (test_evict_oldest_and_reuse_capacity() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] evict oldest flow and reuse capacity\n");

    if (test_process_packet_with_oldest_eviction() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf(
        "[PASS] process packet with oldest eviction\n"
    );

    if (test_process_packet_with_oldest_eviction_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf(
        "[PASS] process packet with oldest eviction validation\n"
    );

    if (test_evict_oldest_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] evict oldest flow validation\n");

    return EXIT_SUCCESS;
}
