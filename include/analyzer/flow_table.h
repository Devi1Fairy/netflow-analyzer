#ifndef NETFLOW_ANALYZER_FLOW_TABLE_H
#define NETFLOW_ANALYZER_FLOW_TABLE_H

#include "analyzer/flow_key.h"
#include "analyzer/flow_record.h"
#include "analyzer/packet_info.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * @brief 表示数据包处理路径中的累计哈希探测统计。
 *
 * 一次探测是flow_table_process_packet为了查找或插入一条流，
 * 从哈希起始槽位开始执行的一次线性扫描。
 *
 * 起始槽位本身也算一次槽位检查，因此正常无冲突操作的
 * probe length为1，而不是0。
 *
 * flow_table_find等管理和测试查询不计入这些统计，
 * 避免输出、验收或诊断查询污染真实数据包路径的成本。
 */
typedef struct {
    /**
     * 已经进入哈希探测的数据包处理操作数量。
     *
     * 找到已有流、创建新流和最终返回ENOSPC的操作都应计入。
     */
    uint64_t packet_operation_count;

    /**
     * 所有数据包处理操作实际检查的槽位数量总和。
     *
     * 后续可以通过：
     *
     * total_inspected_slot_count / packet_operation_count
     *
     * 计算平均探测长度。
     */
    uint64_t total_inspected_slot_count;

    /**
     * 单次数据包处理观察到的最大探测长度。
     *
     * 该数值范围应为0到流表capacity。
     * 尚未处理数据包时为0。
     */
    size_t maximum_probe_length;

    /**
     * true表示至少一个累计计数已经达到类型上限。
     *
     * 统计计数达到上限后应饱和，而不是回绕为0。
     * 观测能力不能因为整数溢出破坏程序处理主链。
     */
    bool counters_saturated;
} flow_table_probe_statistics_t;

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
     * 数据包处理路径从初始化以来的累计探测统计。
     *
     * 该字段由流表模块维护，调用方应通过公开查询接口读取快照。
     */
    flow_table_probe_statistics_t probe_statistics;

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
 * @brief 处理数据包，并在真正满表时原位淘汰最旧流。
 *
 * 正常情况下，本函数与flow_table_process_packet行为相同：
 *
 * - 找到已有流时更新记录；
 * - 存在EMPTY或DELETED槽位时创建新流；
 * - 上述两种情况都不会淘汰流。
 *
 * 如果当前数据包属于新流，并且一次完整哈希探测确认流表中
 * 没有EMPTY或DELETED槽位，则使用探测期间找到的最旧槽位：
 *
 * 1. 把旧记录按值复制到evicted_record；
 * 2. 在局部变量中初始化当前包的新流记录；
 * 3. 使用新记录原位替换最旧槽位。
 *
 * 原位替换前后槽位都保持OCCUPIED，因此table->count不变，
 * 也不会产生会截断开放寻址探测链的EMPTY槽位。
 *
 * 成功时：
 *
 * - record指向创建或更新后的流表内部记录；
 * - created表示是否创建了新流；
 * - evicted表示是否发生了原位淘汰；
 * - evicted为true时，evicted_record包含被淘汰流的值副本；
 * - evicted为false时，不修改evicted_record。
 *
 * record由流表拥有，调用者不能free或直接修改。
 * evicted_record由调用者拥有，不依赖原槽位的后续生命周期。
 *
 * 每个数据包只记录一次哈希探测操作。原位替换不会再次增加
 * packet_operation_count或total_inspected_slot_count。
 *
 * 函数失败时不修改record、created、evicted_record和evicted。
 *
 * @return 成功时返回0，参数、数据包或流表状态无效时返回相应错误码。
 */
int flow_table_process_packet_with_oldest_eviction(
    flow_table_t *table,
    const packet_info_t *packet,
    const flow_record_t **record,
    bool *created,
    flow_record_t *evicted_record,
    bool *evicted);

/**
 * @brief 移除最后活动时间最早的一条流，并返回其值副本。
 *
 * “最旧”由最小的record->last_seen决定。
 * 多条流拥有相同last_seen时，调用者不能依赖具体选择哪一条。
 *
 * 删除前把记录按值复制到evicted_record。
 * flow_record_t不拥有动态内存，因此原槽位删除或复用后，
 * 该副本仍然有效。
 *
 * 删除后的槽位标记为DELETED，保证已有哈希探测链不中断。
 * 如果删除后流表完全为空，则把全部槽位恢复为EMPTY。
 *
 * 本操作不属于数据包哈希探测，不修改probe_statistics。
 *
 * 函数失败时不修改流表和evicted_record。
 *
 * @param table 指向已经初始化且至少包含一条流的流表。
 * @param evicted_record 指向独立于流表槽位的输出对象。
 *
 * @return 成功时返回0；
 *         参数或流表内部状态无效时返回EINVAL；
 *         流表为空时返回ENOENT。
 */
int flow_table_evict_oldest(
    flow_table_t *table,
    flow_record_t *evicted_record);

/**
 * @brief 删除最后活动时间不晚于cutoff的流记录，并返回其值副本。
 *
 * 满足以下条件的记录会被删除：
 *
 * record->last_seen <= *cutoff
 *
 * 删除前，每条过期记录会按值复制到expired_records数组。
 * flow_record_t不拥有动态内存，因此这些副本不依赖流表槽位，
 * 在原记录被删除后仍然有效。
 *
 * expired_records中的记录顺序由哈希槽位的物理扫描顺序决定，
 * 调用者不能依赖它们按创建时间或最后活动时间排序。
 *
 * expired_records_capacity必须足以保存本次所有过期记录。
 * 如果容量不足，函数返回ENOSPC，并且不修改流表、
 * expired_records或expired_count。
 *
 * 删除后的槽位被标记成DELETED，而不是EMPTY，保证哈希探测链
 * 不会中断。如果删除后流表完全为空，则把全部槽位恢复成EMPTY。
 *
 * 函数失败时不修改流表、expired_records或expired_count。
 *
 * @param table 指向待执行过期清理的流表。
 * @param cutoff 指向包含截止时间的有效时间戳。
 * @param expired_records 指向调用者提供的流记录输出数组。
 * @param expired_records_capacity 输出数组最多可以保存的记录数量。
 * @param expired_count 用于接收实际删除并复制的记录数量。
 *
 * @return 成功时返回0；
 *         参数或流表状态无效时返回EINVAL；
 *         输出数组容量不足时返回ENOSPC。
 */
int flow_table_expire_before(
    flow_table_t *table,
    const flow_timestamp_t *cutoff,
    flow_record_t *expired_records,
    size_t expired_records_capacity,
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
 * @brief 取得数据包处理路径的累计哈希探测统计快照。
 *
 * 统计只包含flow_table_process_packet触发的探测，
 * 不包含flow_table_find和flow_table_get等查询操作。
 *
 * 后续实现中，即使数据包因为流表已满而返回ENOSPC，
 * 已经发生的槽位检查也应进入统计。
 *
 * 函数成功后才修改statistics。
 * 参数或流表状态无效时返回EINVAL，并保持输出对象原值不变。
 *
 * @param table 指向已经初始化的流表。
 * @param statistics 指向用于接收统计快照的对象。
 *
 * @return 成功时返回0，参数或流表状态无效时返回EINVAL。
 */
int flow_table_get_probe_statistics(
    const flow_table_t *table,
    flow_table_probe_statistics_t *statistics);

/**
 * @brief 使流表失效，并解除对外部槽位数组的借用。
 *
 * 函数不会free外部storage，因为该内存由调用者拥有。
 */
void flow_table_cleanup(flow_table_t *table);

#endif