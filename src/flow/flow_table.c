#include "analyzer/flow_table.h"

#include <errno.h>

/**
 * @brief 在线性数组中查找流键对应的下标。
 *
 * @return 找到时返回true并写入index；
 *         没找到时返回false且不修改index。
 */
static bool flow_table_find_index(
    const flow_table_t *table,
    const flow_key_t *key,
    size_t *index)
{
    size_t current_index;

    if (table == NULL ||
        key == NULL ||
        index == NULL ||
        !table->initialized) {
        return false;
    }

    /*
     * 只有records[0]到records[count - 1]是有效记录。
     */
    for (current_index = 0U; current_index < table->count; current_index += 1U) {
        if (flow_key_equal(&table->records[current_index].key, key)) {
            *index = current_index;
            return true;
        }
    }

    return false;
}

int flow_table_init(flow_table_t *table,
                    flow_record_t *storage,
                    size_t capacity)
{
    flow_table_t new_table;

    /*
     * 容量为0的流表无法保存任何流，因此不接受。
     */
    if (table == NULL ||
        storage == NULL ||
        capacity == 0U) {
        return EINVAL;
    }

    new_table = (flow_table_t){
        .records = storage,
        .capacity = capacity,
        .count = 0U,
        .initialized = true
    };

    /*
     * 参数全部检查成功后再发布结果。
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

    const flow_record_t *result_record;
    bool result_created;

    size_t record_index;
    int error_code;

    if (table == NULL ||
        packet == NULL ||
        record == NULL ||
        created == NULL ||
        !table->initialized ||
        table->records == NULL ||
        table->capacity == 0U ||
        table->count > table->capacity) {
        return EINVAL;
    }

    /*
     * 流表需要先生成双向流键进行查找。
     *
     * flow_key_from_packet同时验证packet是否已经完整解析。
     * packet_direction在查找阶段暂时不使用，因为flow_record_update
     * 会再次确认方向并更新正确的方向统计。
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
     * 明确表示变量已经由函数写入，但当前层不需要使用。
     *
     * 这样可以避免编译器产生unused variable警告。
     */
    (void)packet_direction;

    if (flow_table_find_index(
            table,
            &packet_key,
            &record_index)) {
        /*
         * 找到已有流，直接累计当前数据包。
         */
        error_code = flow_record_update(
            &table->records[record_index],
            packet
        );

        if (error_code != 0) {
            return error_code;
        }

        result_record = &table->records[record_index];
        result_created = false;
    } else {
        /*
         * 未找到流时，需要创建新记录。
         *
         * 必须先检查容量，防止访问records[capacity]。
         */
        if (table->count == table->capacity) {
            return ENOSPC;
        }

        record_index = table->count;

        error_code = flow_record_init(
            &table->records[record_index],
            packet
        );

        if (error_code != 0) {
            return error_code;
        }

        /*
         * 只有流记录初始化成功后才能增加count。
         *
         * 否则失败元素会被错误地视为有效记录。
         */
        table->count += 1U;

        result_record = &table->records[record_index];
        result_created = true;
    }

    /*
     * 全部操作成功后再发布输出参数。
     */
    *record = result_record;
    *created = result_created;

    return 0;
}

int flow_table_find(
    const flow_table_t *table,
    const flow_key_t *key,
    const flow_record_t **record)
{
    size_t record_index;

    if (table == NULL ||
        key == NULL ||
        record == NULL ||
        !table->initialized ||
        table->records == NULL ||
        table->count > table->capacity) {
        return EINVAL;
    }

    if (!flow_table_find_index(
            table,
            key,
            &record_index)) {
        return ENOENT;
    }

    *record = &table->records[record_index];

    return 0;
}

int flow_table_get(
    const flow_table_t *table,
    size_t index,
    const flow_record_t **record)
{
    if (table == NULL ||
        record == NULL ||
        !table->initialized ||
        table->records == NULL ||
        table->count > table->capacity) {
        return EINVAL;
    }

    if (index >= table->count) {
        return ERANGE;
    }

    *record = &table->records[index];

    return 0;
}

size_t flow_table_count(const flow_table_t *table)
{
    if (table == NULL ||
        !table->initialized ||
        table->records == NULL ||
        table->count > table->capacity) {
        return 0U;
    }

    return table->count;
}

void flow_table_cleanup(flow_table_t *table)
{
    if (table == NULL) {
        return;
    }

    /*
     * 这里只解除对外部数组的借用。
     *
     * records指向的存储不属于flow_table，因此不能free。
     */
    *table = (flow_table_t){0};
}