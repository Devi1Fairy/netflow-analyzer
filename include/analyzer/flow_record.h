#ifndef NETFLOW_ANALYZER_FLOW_RECORD_H
#define NETFLOW_ANALYZER_FLOW_RECORD_H

#include "analyzer/flow_key.h"
#include "analyzer/packet_info.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 表示流记录中的一个时间点。
 *
 * 秒和微秒分开保存，避免立即执行：
 *
 * seconds * 1000000 + microseconds
 *
 * 对任意int64_t秒数执行上述乘法都可能溢出。
 */
typedef struct {
    /**
     * 时间戳的整数秒部分。
     */
    int64_t seconds;

    /**
     * 当前秒内的微秒部分，合法范围为0～999999。
     */
    int32_t microseconds;
} flow_timestamp_t;

/**
 * @brief 保存一条流在一个方向上的统计结果。
 */
typedef struct {
    /**
     * 当前方向的数据包数量。
     */
    uint64_t packet_count;

    /**
     * 当前方向实际捕获并保存在内存或PCAP中的字节数。
     *
     * 该值累计packet_info_t中的captured_length。
     */
    uint64_t captured_byte_count;

    /**
     * 当前方向数据包在线路上的原始字节数。
     *
     * 该值累计packet_info_t中的wire_length。
     */
    uint64_t wire_byte_count;
} flow_direction_stats_t;

/**
 * @brief 表示一条双向网络流的聚合记录。
 *
 * 该结构体不保存packet_info_t数组，也不保存原始报文指针。
 * 每收到一个属于该流的数据包，只更新计数器和时间范围。
 *
 * 当前结构体只保存数值，不拥有动态内存，因此不需要free。
 *
 * 当前版本没有内部锁，不能由多个线程同时修改同一个flow_record_t。
 * 多线程阶段将由流表分片或外部互斥锁保证同步。
 */
typedef struct {
    /**
     * 当前流的规范化双向五元组键。
     */
    flow_key_t key;

    /**
     * endpoint_a到endpoint_b方向的统计结果。
     */
    flow_direction_stats_t a_to_b;

    /**
     * endpoint_b到endpoint_a方向的统计结果。
     */
    flow_direction_stats_t b_to_a;

    /**
     * 当前流所有数据包中最早的捕获时间。
     */
    flow_timestamp_t first_seen;

    /**
     * 当前流所有数据包中最晚的捕获时间。
     */
    flow_timestamp_t last_seen;

    /**
     * true表示流记录已经成功初始化。
     */
    bool initialized;
} flow_record_t;

/**
 * @brief 使用第一条数据包创建流记录。
 *
 * 函数内部调用flow_key_from_packet生成规范化流键和数据包方向，
 * 然后把第一条数据包计入对应方向。
 *
 * 函数失败时不修改record。
 *
 * @param record 指向待初始化的流记录。
 * @param first_packet 指向当前流的第一条完整解析数据包。
 *
 * @return 成功时返回0；
 *         参数或数据包状态无效时返回EINVAL；
 *         协议暂不支持时返回ENOTSUP。
 */
int flow_record_init(
    flow_record_t *record,
    const packet_info_t *first_packet);

/**
 * @brief 把一条数据包累计到现有流记录中。
 *
 * 函数会重新生成数据包的双向流键，并检查它是否与record->key相同。
 * 因此调用者不能把属于其他连接的数据包错误加入当前记录。
 *
 * first_seen始终保存最早时间，last_seen始终保存最晚时间。
 * 即使PCAP中的数据包时间顺序不严格递增，也能得到正确时间范围。
 *
 * 任意计数器可能溢出时返回EOVERFLOW，并且不修改record。
 *
 * 函数失败时不修改record。
 *
 * @param record 指向已经初始化的流记录。
 * @param packet 指向准备累计的完整解析数据包。
 *
 * @return 成功时返回0；
 *         参数或状态无效时返回EINVAL；
 *         packet不属于当前流时返回ENOENT；
 *         协议暂不支持时返回ENOTSUP；
 *         计数器可能溢出时返回EOVERFLOW。
 */
int flow_record_update(
    flow_record_t *record,
    const packet_info_t *packet);

#endif