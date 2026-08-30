#include "analyzer/flow_table.h"

#include <errno.h>
#include <stdint.h>

/**
 * @brief 判断流表的基本结构是否处于有效状态。
 *
 * 本函数只检查流表自身的基本字段，不遍历所有槽位。
 */
static bool flow_table_has_valid_shape(const flow_table_t *table)
{
    return table != NULL &&
           table->initialized &&
           table->slots != NULL &&
           table->capacity > 0U &&
           table->count <= table->capacity;
}

/**
 * @brief 累计一次数据包处理产生的哈希探测成本。
 *
 * 调用前必须保证：
 *
 * - table已经初始化且结构有效；
 * - inspected_slot_count位于1到table->capacity之间。
 *
 * 计数达到UINT64_MAX后保持饱和，不能回绕为0。
 * 本函数只更新观测数据，不修改任何流记录或槽位状态。
 */
static void flow_table_record_probe_statistics(
    flow_table_t *table,
    size_t inspected_slot_count)
{
    flow_table_probe_statistics_t updated_statistics;
    uint64_t inspected_slot_count_u64;

    updated_statistics = table->probe_statistics;
    inspected_slot_count_u64 = (uint64_t)inspected_slot_count;

    /*
     * 每次进入数据包探测路径都增加一次操作计数。
     */
    if (updated_statistics.packet_operation_count == UINT64_MAX) {
        updated_statistics.counters_saturated = true;
    } else {
        updated_statistics.packet_operation_count += UINT64_C(1);

        if (updated_statistics.packet_operation_count == UINT64_MAX) {
            updated_statistics.counters_saturated = true;
        }
    }

    /*
     * 累加实际检查的槽位数量。
     *
     * 先用减法判断是否会溢出，不能先执行可能溢出的加法。
     */
    if (UINT64_MAX - updated_statistics.total_inspected_slot_count < inspected_slot_count_u64) {
        updated_statistics.total_inspected_slot_count = UINT64_MAX;
        updated_statistics.counters_saturated = true;
    } else {

        updated_statistics.total_inspected_slot_count += inspected_slot_count_u64;

        if (updated_statistics.total_inspected_slot_count == UINT64_MAX) {
            updated_statistics.counters_saturated = true;
        }
    }

    if (inspected_slot_count > updated_statistics.maximum_probe_length) {
        updated_statistics.maximum_probe_length = inspected_slot_count;
    }

    /*
     * 完整计算后再一次性发布新统计快照。
     */
    table->probe_statistics = updated_statistics;
}

/**
 * @brief 判断timestamp是否早于或等于cutoff。
 *
 * 调用前必须保证两个时间戳的微秒字段均有效。
 */
static bool flow_timestamp_at_or_before(
    const flow_timestamp_t *timestamp,
    const flow_timestamp_t *cutoff)
{
    if (timestamp->seconds < cutoff->seconds) {
        return true;
    }

    if (timestamp->seconds > cutoff->seconds) {
        return false;
    }

    /*
     * 秒数相同时，再比较当前秒内的微秒部分。
     */
    return timestamp->microseconds <= cutoff->microseconds;
}

/**
 * @brief 根据流键寻找已有记录或可用于插入的槽位。
 *
 * 该函数同时服务于查找和插入：
 *
 * - found为true时，slot_index是已有记录的位置；
 * - found为false时，slot_index是可以插入新记录的位置；
 * - 没有已有记录且没有可用槽位时返回ENOSPC。
 *
 * 线性探测过程中会记住遇到的第一个DELETED槽位。
 * 如果最后确认流键不存在，插入操作应优先复用这个槽位。
 *
 * 成功时修改found、slot_index和inspected_slot_count。
 * 返回ENOSPC时只修改inspected_slot_count，其值为capacity。
 * 其他失败不修改任何输出参数。
 */
static int flow_table_probe(const flow_table_t *table,
                            const flow_key_t *key,
                            bool *found,
                            size_t *slot_index,
                            size_t *inspected_slot_count)
{
    uint64_t hash_value;

    size_t current_index;
    size_t probe_count;
    size_t first_deleted_index;

    bool has_deleted_slot;
    int error_code;

    if (!flow_table_has_valid_shape(table) ||
        key == NULL ||
        found == NULL ||
        slot_index == NULL ||
        inspected_slot_count == NULL) {
        return EINVAL;
    }

    error_code = flow_key_hash(key, &hash_value);

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 哈希值可能远大于数组容量。
     *
     * 取模后得到0到capacity - 1之间的合法数组下标。
     */
    current_index = (size_t)(hash_value % (uint64_t)table->capacity);

    has_deleted_slot = false;
    first_deleted_index = 0U;

    /*
     * 最多检查capacity个槽位。
     *
     * 这既能覆盖整个哈希表，也能防止数组已满时无限循环。
     */
    for (probe_count = 0U; probe_count < table->capacity; probe_count += 1U) {

        const flow_table_slot_t *current_slot = &table->slots[current_index];

        switch (current_slot->state) {
        case FLOW_TABLE_SLOT_EMPTY:
            /*
             * EMPTY表示这个探测链从未继续到更远的位置，
             * 因此可以确定表中不存在该流键。
             */
            *found = false;

            if (has_deleted_slot) {
                *slot_index = first_deleted_index;
            } else {
                *slot_index = current_index;
            }

            *inspected_slot_count = probe_count + 1U;

            return 0;

        case FLOW_TABLE_SLOT_OCCUPIED:
            /*
             * OCCUPIED槽位必须包含有效的flow_record_t。
             */
            if (!current_slot->record.initialized) {
                return EINVAL;
            }

            if (flow_key_equal(&current_slot->record.key, key)) {
                *found = true;
                *slot_index = current_index;
                *inspected_slot_count = probe_count + 1U;
                return 0;
            }

            break;

        case FLOW_TABLE_SLOT_DELETED:
            /*
             * DELETED不能终止查找。
             *
             * 只记录第一个DELETED位置，后面仍要继续查找，
             * 因为相同流键可能位于探测链的更后面。
             */
            if (!has_deleted_slot) {
                first_deleted_index = current_index;
                has_deleted_slot = true;
            }

            break;

        default:
            /*
             * 出现枚举定义以外的值，说明流表内部状态损坏。
             */
            return EINVAL;
        }

        /*
         * 移动到下一个槽位。
         *
         * 到达数组末尾后回到0，这就是线性探测的环形遍历。
         */
        current_index += 1U;

        if (current_index == table->capacity) {
            current_index = 0U;
        }
    }

    /*
     * 整张表已经检查完成。
     *
     * 如果存在DELETED槽位，仍然可以复用；否则表确实已满。
     */
    if (has_deleted_slot) {
        *found = false;
        *slot_index = first_deleted_index;
        *inspected_slot_count = table->capacity;
        return 0;
    }

    *inspected_slot_count = table->capacity;
    return ENOSPC;
}

int flow_table_init(flow_table_t *table,
                    flow_table_slot_t *storage,
                    size_t capacity)
{
    flow_table_t new_table;
    size_t slot_index;

    if (table == NULL ||
        storage == NULL ||
        capacity == 0U) {
        return EINVAL;
    }

    /*
     * 把所有槽位初始化为EMPTY。
     *
     * 不能只设置count为0，因为哈希查找会直接读取每个槽位的state。
     */
    for (slot_index = 0U;
         slot_index < capacity;
         slot_index += 1U) {
        storage[slot_index] = (flow_table_slot_t){0};
    }

    new_table = (flow_table_t){
        .slots = storage,
        .capacity = capacity,
        .count = 0U,
        .probe_statistics = {0},
        .initialized = true
    };

    /*
     * 所有初始化工作成功后再发布完整流表。
     */
    *table = new_table;

    return 0;
}

int flow_table_process_packet(
    flow_table_t *table,
    const packet_info_t *packet,
    const flow_record_t **record,
    bool *created)
{
    flow_key_t packet_key;
    flow_direction_t packet_direction;

    flow_record_t new_record;
    flow_table_slot_t *target_slot;

    const flow_record_t *result_record;
    size_t inspected_slot_count;
    bool result_created;
    bool found;

    size_t slot_index;
    int error_code;

    if (!flow_table_has_valid_shape(table) ||
        packet == NULL ||
        record == NULL ||
        created == NULL) {
        return EINVAL;
    }

    /*
     * 先生成经过端点规范化的双向流键。
     */
    error_code = flow_key_from_packet(
        packet,
        &packet_key,
        &packet_direction
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * flow_record_update内部会重新判断方向。
     * 当前函数这里只需要packet_key进行流表查找。
     */
    (void)packet_direction;

    error_code = flow_table_probe(
        table,
        &packet_key,
        &found,
        &slot_index,
        &inspected_slot_count
    );

    /*
    * 正常命中、正常插入和流表满载拒绝都实际执行了探测，
    * 因此都应进入观测统计。
    *
    * EINVAL等内部错误没有可靠的完整探测结果，不累计。
    */
    if (error_code == 0 ||
        error_code == ENOSPC) {
        flow_table_record_probe_statistics(
            table,
            inspected_slot_count
        );
    }

    if (error_code != 0) {
        return error_code;
    }

    target_slot = &table->slots[slot_index];

    if (found) {
        /*
         * 找到已有流后，在原槽位累计当前数据包。
         */
        error_code = flow_record_update(
            &target_slot->record,
            packet
        );

        if (error_code != 0) {
            return error_code;
        }

        result_record = &target_slot->record;
        result_created = false;
    } else {
        /*
         * probe返回的插入位置只能是EMPTY或DELETED。
         */
        if (target_slot->state != FLOW_TABLE_SLOT_EMPTY &&
            target_slot->state != FLOW_TABLE_SLOT_DELETED) {
            return EINVAL;
        }

        /*
         * 先在局部变量中初始化流记录。
         *
         * 如果初始化失败，原槽位和流表count都不会被修改。
         */
        error_code = flow_record_init(
            &new_record,
            packet
        );

        if (error_code != 0) {
            return error_code;
        }

        target_slot->record = new_record;
        target_slot->state = FLOW_TABLE_SLOT_OCCUPIED;
        table->count += 1U;

        result_record = &target_slot->record;
        result_created = true;
    }

    /*
     * 所有操作成功后再写入输出参数。
     */
    *record = result_record;
    *created = result_created;

    return 0;
}

int flow_table_expire_before(
    flow_table_t *table,
    const flow_timestamp_t *cutoff,
    flow_record_t *expired_records,
    size_t expired_records_capacity,
    size_t *expired_count)
{
    size_t slot_index;
    size_t observed_count;
    size_t matched_count;
    size_t output_index;

    if (!flow_table_has_valid_shape(table) ||
        cutoff == NULL ||
        expired_records == NULL ||
        expired_count == NULL ||
        cutoff->microseconds < INT32_C(0) ||
        cutoff->microseconds > INT32_C(999999)) {
        return EINVAL;
    }

    observed_count = 0U;
    matched_count = 0U;

    /*
     * 第一遍只验证和计数，不修改流表或输出数组。
     *
     * 只有确认流表结构有效、输出数组容量足够后，
     * 才能进入真正的复制和删除阶段。
     */
    for (slot_index = 0U; slot_index < table->capacity; slot_index += 1U) {

        const flow_table_slot_t *slot = &table->slots[slot_index];

        switch (slot->state) {
        case FLOW_TABLE_SLOT_EMPTY:
        case FLOW_TABLE_SLOT_DELETED:
            break;

        case FLOW_TABLE_SLOT_OCCUPIED:
            if (!slot->record.initialized ||
                slot->record.last_seen.microseconds <
                    INT32_C(0) ||
                slot->record.last_seen.microseconds >
                    INT32_C(999999)) {
                return EINVAL;
            }

            observed_count += 1U;

            if (flow_timestamp_at_or_before(&slot->record.last_seen, cutoff)) {
                matched_count += 1U;
            }

            break;

        default:
            return EINVAL;
        }
    }

    /*
     * count必须与实际OCCUPIED槽位数量一致。
     */
    if (observed_count != table->count) {
        return EINVAL;
    }

    /*
     * 容量不足时必须在任何复制和删除之前失败。
     */
    if (matched_count > expired_records_capacity) {
        return ENOSPC;
    }

    output_index = 0U;

    /*
     * 前面的验证已经排除了所有预期错误。
     * 第二遍先复制记录，再清空原槽位。
     */
    for (slot_index = 0U; slot_index < table->capacity; slot_index += 1U) {
    
        flow_table_slot_t *slot = &table->slots[slot_index];

        if (slot->state != FLOW_TABLE_SLOT_OCCUPIED) {
            continue;
        }

        if (!flow_timestamp_at_or_before(
                &slot->record.last_seen,
                cutoff)) {
            continue;
        }

        expired_records[output_index] = slot->record;
        output_index += 1U;

        slot->record = (flow_record_t){0};
        slot->state = FLOW_TABLE_SLOT_DELETED;
        table->count -= 1U;
    }

    /*
     * 如果已经没有有效记录，DELETED探测链也没有保留价值。
     */
    if (table->count == 0U) {
        for (slot_index = 0U; slot_index < table->capacity; slot_index += 1U) {
            table->slots[slot_index] = (flow_table_slot_t){0};
        }
    }

    *expired_count = output_index;

    return 0;
}

int flow_table_find(const flow_table_t *table,
                    const flow_key_t *key,
                    const flow_record_t **record)
{
    bool found;
    size_t slot_index;
    size_t inspected_slot_count;
    int error_code;

    if (!flow_table_has_valid_shape(table) ||
        key == NULL ||
        record == NULL) {
        return EINVAL;
    }

    error_code = flow_table_probe(table, key, &found, &slot_index, &inspected_slot_count);

    /*
     * 表中所有槽位都被占用，但没有匹配项时，
     * 对查找接口来说仍然只是“没有找到”。
     */
    if (error_code == ENOSPC) {
        return ENOENT;
    }

    if (error_code != 0) {
        return error_code;
    }

    if (!found) {
        return ENOENT;
    }

    *record = &table->slots[slot_index].record;

    return 0;
}

int flow_table_get(const flow_table_t *table,
                    size_t index,
                    const flow_record_t **record)
{
    size_t slot_index;
    size_t logical_index;

    if (!flow_table_has_valid_shape(table) ||
        record == NULL) {
        return EINVAL;
    }

    if (index >= table->count) {
        return ERANGE;
    }

    logical_index = 0U;

    /*
     * 哈希表中的有效槽位不一定连续。
     *
     * 因此这里扫描物理槽位，并只对OCCUPIED槽位进行逻辑计数。
     */
    for (slot_index = 0U; slot_index < table->capacity; slot_index += 1U) {
        const flow_table_slot_t *slot = &table->slots[slot_index];

        if (slot->state == FLOW_TABLE_SLOT_OCCUPIED) {
            if (!slot->record.initialized) {
                return EINVAL;
            }

            if (logical_index == index) {
                *record = &slot->record;
                return 0;
            }

            logical_index += 1U;
        } else if (slot->state != FLOW_TABLE_SLOT_EMPTY &&
                   slot->state != FLOW_TABLE_SLOT_DELETED) {
            return EINVAL;
        }
    }

    /*
     * index小于count却没有找到对应记录，
     * 说明count和槽位状态不一致。
     */
    return EINVAL;
}

size_t flow_table_count(const flow_table_t *table)
{
    if (!flow_table_has_valid_shape(table)) {
        return 0U;
    }

    return table->count;
}

int flow_table_get_probe_statistics(
    const flow_table_t *table,
    flow_table_probe_statistics_t *statistics)
{
    flow_table_probe_statistics_t result_statistics;

    if (!flow_table_has_valid_shape(table) ||
        statistics == NULL) {
        return EINVAL;
    }

    if (table->probe_statistics.maximum_probe_length >
        table->capacity) {
        return EINVAL;
    }

    /*
     * 先取得完整快照，成功后再发布给调用方。
     */
    result_statistics = table->probe_statistics;
    *statistics = result_statistics;

    return 0;
}

void flow_table_cleanup(flow_table_t *table)
{
    if (table == NULL) {
        return;
    }

    /*
     * 这里只解除对外部槽位数组的借用。
     *
     * slots指向的内存属于调用者，因此不能free。
     */
    *table = (flow_table_t){0};
}