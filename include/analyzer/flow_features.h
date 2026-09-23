#ifndef NETFLOW_ANALYZER_FLOW_FEATURES_H
#define NETFLOW_ANALYZER_FLOW_FEATURES_H

#include "analyzer/flow_record.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 保存从一条双向流记录中提取出的模型输入特征。
 *
 * 该结构体只保存数值，不拥有动态内存或原始数据包指针，
 * 因此不需要调用free。
 *
 * 当前特征不包含IP地址和端口，避免第一版模型直接记忆
 * 特定设备、网络或服务标识。
 *
 * protocol和tcp_phase属于类别特征，不能把数值大小解释为
 * 协议或TCP阶段之间的距离。
 */
typedef struct {
    /**
     * IPv4上层协议号。
     *
     * 当前支持1（ICMP）、6（TCP）和17（UDP）。
     */
    uint8_t protocol;

    /**
     * 当前TCP流的生命周期阶段。
     *
     * 只有tcp_state_applicable为true时，该字段才有效。
     */
    tcp_flow_phase_t tcp_phase;

    /**
     * last_seen减去first_seen得到的流持续时间，单位为微秒。
     */
    uint64_t duration_microseconds;

    /**
     * 两个方向的数据包总数。
     */
    uint64_t total_packet_count;

    /**
     * 两个方向实际捕获字节数的总和。
     */
    uint64_t total_captured_byte_count;

    /**
     * 两个方向线路原始字节数的总和。
     */
    uint64_t total_wire_byte_count;

    /**
     * 每个数据包的平均捕获字节数。
     */
    double mean_captured_bytes_per_packet;

    /**
     * 每个数据包的平均线路字节数。
     */
    double mean_wire_bytes_per_packet;

    /**
     * 两个方向包数的不平衡度，合法范围为0.0～1.0。
     */
    double packet_count_imbalance_ratio;

    /**
     * 两个方向线路字节数的不平衡度，合法范围为0.0～1.0。
     */
    double wire_byte_count_imbalance_ratio;

    /**
     * true表示当前记录是TCP流，tcp_phase字段有效。
     */
    bool tcp_state_applicable;

    /**
     * true表示旁路状态机完整观察到TCP三次握手。
     *
     * 非TCP流固定为false。
     */
    bool tcp_handshake_completed;

    /**
     * true表示特征对象已经成功生成。
     */
    bool initialized;
} flow_features_t;

/**
 * @brief 从一条已经初始化的双向流记录中提取数值特征。
 *
 * record和features均由调用者拥有。
 * 本函数只读取record，不保存其中的地址，也不分配动态内存。
 *
 * 函数先在局部对象中完成全部验证和计算，只有全部成功后才
 * 修改features，因此失败不会向调用者发布半初始化结果。
 *
 * @param record 指向准备转换的只读流记录。
 * @param features 指向用于接收结果的特征对象。
 *
 * @return 成功时返回0；
 *         参数、记录状态、时间戳或协议状态无效时返回EINVAL；
 *         协议不受支持时返回ENOTSUP；
 *         总量或持续时间无法放入uint64_t时返回EOVERFLOW。
 */
int flow_features_from_record(
    const flow_record_t *record,
    flow_features_t *features);

#endif
