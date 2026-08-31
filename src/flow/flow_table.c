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
 * @brief 保存一次线性探测产生的内部结果。
 *
 * found为true时，slot_index指向已有流。
 * found为false且探测成功时，slot_index指向可插入槽位。
 *
 * oldest_found和oldest_slot_index记录探测期间看到的
 * last_seen最早的OCCUPIED槽位。只有完整扫描整张表后，
 * 该候选才代表整个流表的最旧记录。
 *
 * 本结构只在flow_table.c内部使用，不能暴露槽位下标给应用层。
 */
typedef struct {
    bool found;
    size_t slot_index;

    bool oldest_found;
    size_t oldest_slot_index;

    size_t inspected_slot_count;
} flow_table_probe_result_t;

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
 * @brief 判断left是否严格早于right。
 *
 * 调用前必须保证两个时间戳的微秒字段均有效。
 */
static bool flow_timestamp_before(
    const flow_timestamp_t *left,
    const flow_timestamp_t *right)
{
    if (left->seconds < right->seconds) {
        return true;
    }

    if (left->seconds > right->seconds) {
        return false;
    }

    return left->microseconds < right->microseconds;
}

/**
 * @brief 根据流键寻找已有记录、可插入槽位和最旧记录候选。
 *
 * 线性探测过程中会记住：
 *
 * - 与key相同的已有流；
 * - 第一个DELETED槽位；
 * - 已检查OCCUPIED槽位中last_seen最早的记录。
 *
 * 如果遇到EMPTY，可以确定key不存在并提前结束。
 * 此时oldest候选只属于已经检查的探测链，不能代表整个流表。
 *
 * 如果完整扫描后返回ENOSPC，oldest候选已经覆盖整张满表，
 * 后续可以在流表模块内部安全地用于原位替换。
 *
 * 成功或ENOSPC时发布probe_result。
 * EINVAL等错误不修改调用者的probe_result。
 */
static int flow_table_probe(
    const flow_table_t *table,
    const flow_key_t *key,
    flow_table_probe_result_t *probe_result)
{
    flow_table_probe_result_t result = {0};

    uint64_t hash_value;

    size_t current_index;
    size_t probe_count;
    size_t first_deleted_index;
    size_t occupied_slot_count;

    bool has_deleted_slot;
    int error_code;

    if (!flow_table_has_valid_shape(table) ||
        key == NULL ||
        probe_result == NULL) {
        return EINVAL;
    }

    error_code = flow_key_hash(key, &hash_value);

    if (error_code != 0) {
        return error_code;
    }

    current_index =
        (size_t)(hash_value % (uint64_t)table->capacity);

    has_deleted_slot = false;
    first_deleted_index = 0U;
    occupied_slot_count = 0U;

    for (probe_count = 0U;
         probe_count < table->capacity;
         probe_count += 1U) {
        const flow_table_slot_t *current_slot =
            &table->slots[current_index];

        switch (current_slot->state) {
        case FLOW_TABLE_SLOT_EMPTY:
            /*
             * EMPTY终止当前探测链，因此可以确认key不存在。
             */
            result.found = false;

            if (has_deleted_slot) {
                result.slot_index = first_deleted_index;
            } else {
                result.slot_index = current_index;
            }

            result.inspected_slot_count =
                probe_count + 1U;

            *probe_result = result;
            return 0;

        case FLOW_TABLE_SLOT_OCCUPIED:
            /*
             * 已占用槽位必须保存有效记录和有效时间戳。
             */
            if (!current_slot->record.initialized ||
                current_slot->record.last_seen.microseconds <
                    INT32_C(0) ||
                current_slot->record.last_seen.microseconds >
                    INT32_C(999999)) {
                return EINVAL;
            }

            occupied_slot_count += 1U;

            /*
             * 在正常哈希探测过程中顺便维护最旧候选，
             * 不额外遍历流表。
             */
            if (!result.oldest_found || flow_timestamp_before(
                    &current_slot->record.last_seen,
                    &table->slots[result.oldest_slot_index].record.last_seen)) {
                result.oldest_found = true;
                result.oldest_slot_index = current_index;
            }

            if (flow_key_equal(&current_slot->record.key, key)) {
                result.found = true;
                result.slot_index = current_index;
                result.inspected_slot_count = probe_count + 1U;

                *probe_result = result;
                return 0;
            }

            break;

        case FLOW_TABLE_SLOT_DELETED:
            /*
             * DELETED可以复用，但不能终止查找。
             */
            if (!has_deleted_slot) {
                has_deleted_slot = true;
                first_deleted_index = current_index;
            }

            break;

        default:
            return EINVAL;
        }

        current_index += 1U;

        if (current_index == table->capacity) {
            current_index = 0U;
        }
    }

    /*
     * 只有完整扫描后，才能核对count与实际槽位数量。
     */
    if (occupied_slot_count != table->count) {
        return EINVAL;
    }

    result.found = false;
    result.inspected_slot_count = table->capacity;

    if (has_deleted_slot) {
        result.slot_index = first_deleted_index;
        *probe_result = result;
        return 0;
    }

    /*
     * 没有EMPTY和DELETED，说明真正满表。
     *
     * 合法的非零容量满表中一定存在最旧候选。
     */
    if (!result.oldest_found) {
        return EINVAL;
    }

    *probe_result = result;
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

/**
 * @brief 实现普通数据包处理和可选的满表原位淘汰。
 *
 * evict_oldest_when_full为false时保持原有ENOSPC语义。
 *
 * evict_oldest_when_full为true时，evicted_record和evicted必须有效；
 * 真正满表时使用probe_result中的最旧候选原位创建新流。
 */
static int flow_table_process_packet_internal(
    flow_table_t *table,
    const packet_info_t *packet,
    bool evict_oldest_when_full,
    const flow_record_t **record,
    bool *created,
    flow_record_t *evicted_record,
    bool *evicted)
{
    flow_key_t packet_key;
    flow_direction_t packet_direction;

    flow_record_t new_record;
    flow_record_t result_evicted_record = {0};

    flow_table_slot_t *target_slot;

    const flow_record_t *result_record;

    bool result_created;
    bool result_evicted = false;

    int error_code;

    flow_table_probe_result_t probe_result = {0};

    if (!flow_table_has_valid_shape(table) ||
        packet == NULL ||
        record == NULL ||
        created == NULL ||
        (evict_oldest_when_full &&
         (evicted_record == NULL || evicted == NULL))) {
        return EINVAL;
    }

    error_code = flow_key_from_packet(
        packet,
        &packet_key,
        &packet_direction
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * flow_record_update会根据packet重新确定方向。
     * 当前函数只使用规范化后的packet_key进行哈希探测。
     */
    (void)packet_direction;

    error_code = flow_table_probe(
        table,
        &packet_key,
        &probe_result
    );

    /*
     * 正常查找、插入和完整满表扫描都是真实数据包探测。
     */
    if (error_code == 0 ||
        error_code == ENOSPC) {
        flow_table_record_probe_statistics(
            table,
            probe_result.inspected_slot_count
        );
    }

    if (error_code == ENOSPC &&
        evict_oldest_when_full) {
        /*
         * ENOSPC表示已经完整扫描整张表，并确认：
         *
         * - 当前key不存在；
         * - 没有EMPTY；
         * - 没有DELETED。
         *
         * 因此最旧候选必须存在，count也必须等于capacity。
         */
        if (!probe_result.oldest_found ||
            table->count != table->capacity) {
            return EINVAL;
        }

        target_slot =
            &table->slots[probe_result.oldest_slot_index];

        if (target_slot->state !=
                FLOW_TABLE_SLOT_OCCUPIED ||
            !target_slot->record.initialized) {
            return EINVAL;
        }

        /*
         * 必须先在局部变量中初始化新记录。
         *
         * 如果初始化失败，旧槽位、count和输出参数都保持不变。
         */
        error_code = flow_record_init(
            &new_record,
            packet
        );

        if (error_code != 0) {
            return error_code;
        }

        /*
         * 先取得独立值副本，再覆盖原槽位。
         */
        result_evicted_record = target_slot->record;

        target_slot->record = new_record;

        /*
         * 状态仍然是OCCUPIED，count也保持不变。
         */
        result_record = &target_slot->record;
        result_created = true;
        result_evicted = true;
    } else {
        if (error_code != 0) {
            return error_code;
        }

        target_slot =
            &table->slots[probe_result.slot_index];

        if (probe_result.found) {
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
            if (target_slot->state !=
                    FLOW_TABLE_SLOT_EMPTY &&
                target_slot->state !=
                    FLOW_TABLE_SLOT_DELETED) {
                return EINVAL;
            }

            /*
             * probe找到了可插入槽位，因此count必须小于capacity。
             */
            if (table->count >= table->capacity) {
                return EINVAL;
            }

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
    }

    /*
     * 流表修改完全成功后才发布调用者输出。
     */
    *record = result_record;
    *created = result_created;

    if (evict_oldest_when_full) {
        if (result_evicted) {
            *evicted_record = result_evicted_record;
        }

        *evicted = result_evicted;
    }

    return 0;
}

int flow_table_process_packet(
    flow_table_t *table,
    const packet_info_t *packet,
    const flow_record_t **record,
    bool *created)
{
    return flow_table_process_packet_internal(
        table,
        packet,
        false,
        record,
        created,
        NULL,
        NULL
    );
}

int flow_table_process_packet_with_oldest_eviction(
    flow_table_t *table,
    const packet_info_t *packet,
    const flow_record_t **record,
    bool *created,
    flow_record_t *evicted_record,
    bool *evicted)
{
    return flow_table_process_packet_internal(
        table,
        packet,
        true,
        record,
        created,
        evicted_record,
        evicted
    );
}

int flow_table_evict_oldest(
    flow_table_t *table,
    flow_record_t *evicted_record)
{
    flow_table_slot_t *oldest_slot;
    flow_record_t result_record;

    size_t slot_index;
    size_t oldest_slot_index = 0U;
    size_t observed_count = 0U;

    bool oldest_found = false;

    if (!flow_table_has_valid_shape(table) ||
        evicted_record == NULL) {
        return EINVAL;
    }

    /*
     * 第一遍只验证槽位并选择候选记录。
     *
     * 在完成全部验证前不能修改流表或输出对象，
     * 保证失败不修改语义。
     */
    for (slot_index = 0U;
         slot_index < table->capacity;
         slot_index += 1U) {
        const flow_table_slot_t *slot =
            &table->slots[slot_index];

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

            if (!oldest_found ||
                flow_timestamp_before(
                    &slot->record.last_seen,
                    &result_record.last_seen)) {
                result_record = slot->record;
                oldest_slot_index = slot_index;
                oldest_found = true;
            }

            break;

        default:
            return EINVAL;
        }
    }

    /*
     * table->count必须与实际OCCUPIED槽位数量一致。
     */
    if (observed_count != table->count) {
        return EINVAL;
    }

    if (!oldest_found) {
        return ENOENT;
    }

    oldest_slot = &table->slots[oldest_slot_index];

    /*
     * result_record已经保存独立值副本，
     * 现在可以安全清除原槽位。
     */
    oldest_slot->record = (flow_record_t){0};
    oldest_slot->state = FLOW_TABLE_SLOT_DELETED;
    table->count -= 1U;

    /*
     * 表完全为空后不再需要保留任何DELETED探测链。
     */
    if (table->count == 0U) {
        for (slot_index = 0U;
             slot_index < table->capacity;
             slot_index += 1U) {
            table->slots[slot_index] =
                (flow_table_slot_t){0};
        }
    }

    /*
     * 所有内部修改成功后，最后发布输出副本。
     */
    *evicted_record = result_record;

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
    flow_table_probe_result_t probe_result = {0};
    int error_code;

    if (!flow_table_has_valid_shape(table) ||
        key == NULL ||
        record == NULL) {
        return EINVAL;
    }

    error_code = flow_table_probe(
        table,
        key,
        &probe_result
    );

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

    if (!probe_result.found) {
        return ENOENT;
    }

    *record = &table->slots[probe_result.slot_index].record;

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