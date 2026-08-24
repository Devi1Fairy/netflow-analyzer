#ifndef NETFLOW_ANALYZER_FLOW_TABLE_H
#define NETFLOW_ANALYZER_FLOW_TABLE_H

#include "analyzer/flow_key.h"
#include "analyzer/flow_record.h"
#include "analyzer/packet_info.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 表示第一版固定容量流表。
 *
 * 流表借用调用者提供的flow_record_t数组，不取得数组所有权，
 * 不会对records调用free。
 *
 * 有效流记录始终位于：
 *
 * records[0] 到 records[count - 1]
 *
 * records[count]之后的元素属于未使用空间，其内容不能被读取。
 *
 * 当前版本使用线性查找，也没有内部互斥锁，不能由多个线程同时修改。
 */
typedef struct {
    /**
     * 指向调用者提供的流记录数组。
     */
    flow_record_t *records;

    /**
     * records数组最多能够保存多少条流。
     */
    size_t capacity;

    /**
     * 当前已经保存的有效流记录数量。
     */
    size_t count;

    /**
     * true表示流表已经成功初始化。
     */
    bool initialized;
} flow_table_t;

/**
 * @brief 使用调用者提供的固定容量数组初始化流表。
 *
 * 函数不会清空整个storage数组，因为count初始化为0后，
 * 所有数组元素在逻辑上都属于未使用空间。
 *
 * storage必须在flow_table_t的整个使用期间保持有效。
 *
 * 参数无效时不修改table。
 *
 * @param table 指向待初始化的流表。
 * @param storage 指向调用者提供的flow_record_t数组。
 * @param capacity storage能够保存的元素数量。
 *
 * @return 成功时返回0，参数无效时返回EINVAL。
 */
int flow_table_init(flow_table_t *table,
                    flow_record_t *storage,
                    size_t capacity);

/**
 * @brief 把一条完整解析的数据包加入流表。
 *
 * 函数首先从packet生成双向流键，然后在线性数组中查找：
 *
 * - 找到相同流键时，更新已有flow_record_t；
 * - 没找到时，在数组末尾创建新的flow_record_t；
 * - 流表已满时返回ENOSPC。
 *
 * 成功时：
 *
 * - record指向创建或更新后的流记录；
 * - created为true表示创建了新流；
 * - created为false表示更新了已有流。
 *
 * record返回的是流表内部数组元素的只读地址。调用者不能free，
 * 也不应绕过流表直接修改该记录。
 *
 * 函数失败时不修改record和created。
 *
 * @param table 指向已经初始化的流表。
 * @param packet 指向已经完整解析的数据包结果。
 * @param record 指向用于接收流记录只读指针的变量。
 * @param created 指向用于接收是否创建新流的布尔变量。
 *
 * @return 成功时返回0；
 *         参数或状态无效时返回EINVAL；
 *         协议暂不支持时返回ENOTSUP；
 *         流表容量已满时返回ENOSPC；
 *         更新已有流失败时返回对应错误码。
 */
int flow_table_process_packet(
    flow_table_t *table,
    const packet_info_t *packet,
    const flow_record_t **record,
    bool *created);

/**
 * @brief 删除最后活动时间不晚于cutoff的流记录。
 *
 * 满足以下条件的记录会被删除：
 *
 * record->last_seen <= *cutoff
 *
 * 删除完成后，仍然有效的记录会被稳定地移动到数组前部，
 * 并继续满足以下流表不变量：
 *
 * records[0]到records[count - 1]都是有效记录。
 *
 * “稳定移动”表示未过期记录之间的相对顺序不会变化。
 *
 * 由于函数可能移动数组元素，之前由flow_table_find、
 * flow_table_get或flow_table_process_packet返回的记录指针，
 * 在本函数成功后都必须视为失效，不能继续使用。
 *
 * 函数失败时不修改流表，也不修改expired_count。
 *
 * @param table 指向已经初始化的流表。
 * @param cutoff 指向过期截止时间。
 * @param expired_count 用于接收本次删除的流记录数量。
 *
 * @return 成功时返回0；
 *         参数、时间戳或流表状态无效时返回EINVAL。
 */
int flow_table_expire_before(
    flow_table_t *table,
    const flow_timestamp_t *cutoff,
    size_t *expired_count);

/**
 * @brief 根据双向流键查找流记录。
 *
 * 成功时record指向流表内部的只读流记录。
 * 未找到时返回ENOENT。
 *
 * 函数失败时不修改record。
 *
 * @param table 指向已经初始化的流表。
 * @param key 指向准备查找的双向流键。
 * @param record 指向用于接收流记录只读指针的变量。
 *
 * @return 找到时返回0；
 *         参数无效时返回EINVAL；
 *         没找到时返回ENOENT。
 */
int flow_table_find(
    const flow_table_t *table,
    const flow_key_t *key,
    const flow_record_t **record);

/**
 * @brief 根据数组下标取得一条只读流记录。
 *
 * 该接口用于遍历流表：
 *
 * for (index = 0; index < flow_table_count(&table); ++index)
 *
 * @param table 指向已经初始化的流表。
 * @param index 有效流记录下标。
 * @param record 指向用于接收流记录只读指针的变量。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         index超出有效范围时返回ERANGE。
 */
int flow_table_get(
    const flow_table_t *table,
    size_t index,
    const flow_record_t **record);

/**
 * @brief 返回流表当前保存的流记录数量。
 *
 * @return 流表有效时返回count，否则返回0。
 */
size_t flow_table_count(const flow_table_t *table);

/**
 * @brief 使流表失效并解除对外部storage的借用。
 *
 * 函数不会free或清空storage，因为storage由调用者拥有。
 *
 * 清理后再次使用流表前必须重新调用flow_table_init。
 */
void flow_table_cleanup(flow_table_t *table);

#endif