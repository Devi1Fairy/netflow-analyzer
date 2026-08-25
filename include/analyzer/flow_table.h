#ifndef NETFLOW_ANALYZER_FLOW_TABLE_H
#define NETFLOW_ANALYZER_FLOW_TABLE_H

#include "analyzer/flow_key.h"
#include "analyzer/flow_record.h"
#include "analyzer/packet_info.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief 表示哈希表槽位当前所处的状态。
 *
 * EMPTY：
 *     槽位从未保存过流记录，查找到这里时可以停止。
 *
 * OCCUPIED：
 *     槽位当前保存着有效流记录。
 *
 * DELETED：
 *     槽位以前保存过记录，但记录已经过期删除。
 *     查找时不能在这里停止，否则可能漏掉后续发生哈希冲突的记录。
 */
typedef enum {
    FLOW_TABLE_SLOT_EMPTY = 0,
    FLOW_TABLE_SLOT_OCCUPIED,
    FLOW_TABLE_SLOT_DELETED
} flow_table_slot_state_t;

/**
 * @brief 表示哈希流表中的一个槽位。
 *
 * state用于说明record当前是否有效。
 * 只有state为FLOW_TABLE_SLOT_OCCUPIED时才能读取record。
 */
typedef struct {
    flow_table_slot_state_t state;
    flow_record_t record;
} flow_table_slot_t;

/**
 * @brief 表示使用开放寻址法实现的固定容量哈希流表。
 *
 * 流表借用调用者提供的flow_table_slot_t数组，不取得其所有权，
 * 因此cleanup时不会对slots调用free。
 *
 * 当前版本使用线性探测解决哈希冲突：
 *
 * 1. 使用flow_key_hash计算哈希值；
 * 2. 使用“哈希值 % capacity”得到起始槽位；
 * 3. 槽位被占用且键不同时，继续检查下一个槽位；
 * 4. 到达数组末尾时回到数组开头。
 *
 * 当前实现没有内部互斥锁，不能由多个线程同时修改。
 */
typedef struct {
    /**
     * 指向调用者提供的哈希槽位数组。
     */
    flow_table_slot_t *slots;

    /**
     * slots数组能够保存的槽位总数。
     */
    size_t capacity;

    /**
     * 当前处于OCCUPIED状态的有效流记录数量。
     *
     * DELETED槽位不计入count。
     */
    size_t count;

    /**
     * true表示流表已经成功初始化。
     */
    bool initialized;
} flow_table_t;

/**
 * @brief 使用调用者提供的固定容量槽位数组初始化流表。
 *
 * 初始化时会把所有槽位清零，使其进入EMPTY状态。
 *
 * storage必须在flow_table_t的整个使用期间保持有效。
 *
 * @param table 指向待初始化的流表。
 * @param storage 指向调用者提供的flow_table_slot_t数组。
 * @param capacity storage能够容纳的槽位数量。
 *
 * @return 成功时返回0，参数无效时返回EINVAL。
 */
int flow_table_init(flow_table_t *table,
                    flow_table_slot_t *storage,
                    size_t capacity);

/**
 * @brief 把一条完整解析的数据包加入流表。
 *
 * 函数首先生成双向流键，然后通过哈希值查找槽位：
 *
 * - 找到相同流键时，更新已有流记录；
 * - 没找到时，在EMPTY或DELETED槽位创建新流；
 * - 所有槽位都被占用时返回ENOSPC。
 *
 * 成功时：
 *
 * - record指向创建或更新后的流记录；
 * - created为true表示创建了新流；
 * - created为false表示更新了已有流。
 *
 * record指向流表内部存储，调用者不能free，也不能直接修改。
 * 调用流表的修改接口后，不应继续长期保存以前取得的record指针。
 *
 * 函数失败时不修改record和created。
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
 * 删除后的槽位被标记成DELETED，而不是EMPTY，保证哈希探测链不会中断。
 *
 * 如果删除后流表已经完全为空，则会把所有槽位恢复成EMPTY，
 * 清除已经没有意义的DELETED标记。
 *
 * 函数失败时不修改流表，也不修改expired_count。
 */
int flow_table_expire_before(
    flow_table_t *table,
    const flow_timestamp_t *cutoff,
    size_t *expired_count);

/**
 * @brief 根据双向流键查找流记录。
 *
 * 查找使用与插入相同的哈希值和线性探测规则。
 *
 * 成功时record指向流表内部的只读记录。
 * 未找到时返回ENOENT。
 *
 * 函数失败时不修改record。
 */
int flow_table_find(
    const flow_table_t *table,
    const flow_key_t *key,
    const flow_record_t **record);

/**
 * @brief 根据逻辑下标取得一条只读流记录。
 *
 * 哈希表中的有效记录不再连续排列，因此这里的index是逻辑下标，
 * 并不等于slots数组的物理下标。
 *
 * 例如流表中有两条记录时：
 *
 * flow_table_get(table, 0, ...)
 * flow_table_get(table, 1, ...)
 *
 * 都能获得有效记录，但它们可能实际位于slots[3]和slots[10]。
 *
 * 该接口主要供应用层遍历和输出流摘要使用。
 */
int flow_table_get(
    const flow_table_t *table,
    size_t index,
    const flow_record_t **record);

/**
 * @brief 返回当前有效流记录数量。
 *
 * @return 流表状态有效时返回count，否则返回0。
 */
size_t flow_table_count(const flow_table_t *table);

/**
 * @brief 使流表失效，并解除对外部槽位数组的借用。
 *
 * 函数不会free外部storage，因为该内存由调用者拥有。
 */
void flow_table_cleanup(flow_table_t *table);

#endif