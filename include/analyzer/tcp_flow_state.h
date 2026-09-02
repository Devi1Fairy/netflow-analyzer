#ifndef NETFLOW_ANALYZER_TCP_FLOW_STATE_H
#define NETFLOW_ANALYZER_TCP_FLOW_STATE_H

#include "analyzer/flow_key.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 表示一条TCP流当前所处的连接阶段。
 *
 * 状态描述旁路分析器已经观察到的TCP连接生命周期，
 * 包括三次握手、中途捕获、FIN正常关闭和RST中止。
 *
 * 这些状态不是Linux内核TCP socket状态的完整复制。
 */
typedef enum {
    /**
     * 状态对象已经初始化，但尚未观察任何TCP数据包。
     */
    TCP_FLOW_PHASE_UNOBSERVED = 0,

    /**
     * 已经观察到发起方发送的初始SYN。
     */
    TCP_FLOW_PHASE_SYN_SEEN,

    /**
     * 已经观察到响应方返回的SYN+ACK。
     */
    TCP_FLOW_PHASE_SYN_ACK_SEEN,

    /**
     * 已经观察到完整的TCP三次握手。
     */
    TCP_FLOW_PHASE_ESTABLISHED,

    /**
     * 抓包从连接中途开始，无法证明完整握手过程。
     *
     * 例如状态对象看到的第一包就是PSH+ACK。
     */
    TCP_FLOW_PHASE_MIDSTREAM,

    /**
     * 已经观察到一个方向发送FIN，连接开始关闭。
     */
    TCP_FLOW_PHASE_FIN_SEEN,

    /**
     * 两个方向都已经观察到FIN，等待最终ACK。
     */
    TCP_FLOW_PHASE_FIN_BIDIRECTIONAL,

    /**
     * 已经观察到双向FIN及其后的最终ACK。
     *
     * 这是旁路分析器根据标志位得到的关闭判断，
     * 不是Linux内核TCP端点状态。
     */
    TCP_FLOW_PHASE_CLOSED,

    /**
     * 已经观察到RST，连接被立即中止。
     */
    TCP_FLOW_PHASE_RESET
} tcp_flow_phase_t;

/**
 * @brief 保存一条TCP流的连接状态机上下文。
 *
 * 该结构体只保存数值，不拥有数据包内存或动态内存，
 * 因此不需要free。
 *
 * 每条TCP流以后将拥有独立的tcp_flow_state_t，
 * 不能让多个不同五元组共享同一个状态对象。
 */
typedef struct {
    /**
     * 当前TCP连接阶段。
     */
    tcp_flow_phase_t phase;

    /**
     * 初始SYN相对于规范化流键的方向。
     *
     * endpoint_a和endpoint_b只是排序结果，不代表客户端和服务器。
     * 只有观察到初始SYN后，该字段才会变为A_TO_B或B_TO_A。
     */
    flow_direction_t initiator_direction;

    /**
     * 第一个FIN相对于规范化流键的方向。
     *
     * 尚未观察到FIN时为FLOW_DIRECTION_UNKNOWN。
     * 该字段用于区分FIN重传和反方向FIN。
     */
    flow_direction_t first_fin_direction;

    /**
     * true表示状态机完整观察到了：
     *
     * SYN -> SYN+ACK -> ACK
     *
     * 该字段是历史事实。连接进入关闭或RESET状态后仍然保留，
     * 便于后续计算握手完成率和异常检测特征。
     */
    bool handshake_completed;

    /**
     * true表示状态对象已经通过tcp_flow_state_init初始化。
     */
    bool initialized;
} tcp_flow_state_t;

/**
 * @brief 把TCP流阶段转换成稳定的文本名称。
 *
 * 返回值用于终端、CSV、日志和后续Qt界面。
 * 所有合法状态都返回固定的小写短横线名称，例如：
 *
 * - TCP_FLOW_PHASE_SYN_SEEN -> "syn-seen"；
 * - TCP_FLOW_PHASE_ESTABLISHED -> "established"；
 * - TCP_FLOW_PHASE_CLOSED -> "closed"。
 *
 * 如果phase不是当前版本定义的合法枚举值，返回"unknown"。
 * 函数不会返回NULL。
 *
 * 返回的字符串位于程序的静态只读存储区：
 *
 * - 调用者不拥有该字符串；
 * - 不能修改字符串内容；
 * - 不需要也不能调用free；
 * - 字符串在整个程序运行期间保持有效。
 *
 * @param phase 准备转换的TCP连接阶段。
 *
 * @return 指向静态只读字符串的指针。
 */
const char *tcp_flow_phase_name(tcp_flow_phase_t phase);

/**
 * @brief 初始化一个TCP流状态对象。
 *
 * 成功后：
 *
 * - phase为TCP_FLOW_PHASE_UNOBSERVED；
 * - initiator_direction为FLOW_DIRECTION_UNKNOWN；
 * - first_fin_direction为FLOW_DIRECTION_UNKNOWN；
 * - handshake_completed为false；
 * - initialized为true。
 *
 * 该函数不分配动态内存，可以用来初始化新对象，也可以把已有
 * 状态对象重新恢复为尚未观察数据包的初始状态。
 *
 * @param state 指向调用者拥有的TCP流状态对象。
 *
 * @return 成功时返回0，state为空时返回EINVAL。
 */
int tcp_flow_state_init(tcp_flow_state_t *state);

/**
 * @brief 根据一个TCP数据包推进流连接状态。
 *
 * 调用者应传入：
 *
 * - flow_key_from_packet得到的数据包方向；
 * - packet_info_t中的tcp_flags。
 *
 * 函数只读取direction和tcp_flags，不保存数据包指针，
 * 也不取得任何外部内存的所有权。
 *
 * 第一版识别三次握手及其重传：
 *
 * SYN
 *     -> SYN_SEEN
 *
 * SYN + ACK
 *     -> SYN_ACK_SEEN
 *
 * ACK
 *     -> ESTABLISHED
 *
 * 如果状态机从连接中途开始观察，或者握手事件顺序无法证明
 * 一次完整握手，则进入TCP_FLOW_PHASE_MIDSTREAM。
 *
 * 状态机还识别FIN正常关闭和RST中止。
 *
 * CLOSED和RESET是当前版本的终止状态；同一五元组重新建立
 * 新连接的问题将在后续流生命周期步骤中处理。
 *
 * 函数失败时不修改state。
 *
 * @param state 指向已经初始化的TCP流状态对象。
 * @param direction 当前数据包相对于规范化流键的方向。
 * @param tcp_flags TCP控制标志位组合。
 *
 * @return 成功处理时返回0；
 *         state为空、未初始化、状态值无效或方向无效时返回EINVAL。
 */
int tcp_flow_state_observe(
    tcp_flow_state_t *state,
    flow_direction_t direction,
    uint16_t tcp_flags);

#endif