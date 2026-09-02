#include "analyzer/tcp_flow_state.h"
#include "analyzer/tcp.h"

#include <errno.h>
#include <stddef.h>

int tcp_flow_state_init(tcp_flow_state_t *state)
{
    tcp_flow_state_t new_state;

    if (state == NULL) {
        return EINVAL;
    }

    /*
     * 先在局部变量中建立完整状态。
     *
     * 以后即使初始化逻辑增加其他验证，也可以继续保持
     * “失败不修改调用者对象”的接口约束。
     */
    new_state = (tcp_flow_state_t){
        .phase = TCP_FLOW_PHASE_UNOBSERVED,
        .initiator_direction = FLOW_DIRECTION_UNKNOWN,
        .first_fin_direction = FLOW_DIRECTION_UNKNOWN,
        .handshake_completed = false,
        .initialized = true
    };

    /*
     * 局部对象完整建立后再一次性发布。
     */
    *state = new_state;

    return 0;
}

/**
 * @brief 状态机内部使用的TCP事件。
 *
 * 该类型不放入公开头文件，因为调用者只需要提交TCP标志，
 * 具体如何分类属于状态机内部实现。
 */
typedef enum {
    TCP_FLOW_EVENT_OTHER = 0,
    TCP_FLOW_EVENT_SYN,
    TCP_FLOW_EVENT_SYN_ACK,
    TCP_FLOW_EVENT_ACK,
    TCP_FLOW_EVENT_FIN,
    TCP_FLOW_EVENT_RST
} tcp_flow_event_t;

/**
 * @brief 判断方向是否可以用于双向TCP状态跟踪。
 */
static bool tcp_flow_direction_is_valid(
    flow_direction_t direction)
{
    return direction == FLOW_DIRECTION_A_TO_B ||
           direction == FLOW_DIRECTION_B_TO_A;
}

/**
 * @brief 判断状态枚举值是否属于当前版本。
 */
static bool tcp_flow_phase_is_valid(
    tcp_flow_phase_t phase)
{
    switch (phase) {
        case TCP_FLOW_PHASE_UNOBSERVED:
        case TCP_FLOW_PHASE_SYN_SEEN:
        case TCP_FLOW_PHASE_SYN_ACK_SEEN:
        case TCP_FLOW_PHASE_ESTABLISHED:
        case TCP_FLOW_PHASE_MIDSTREAM:
        case TCP_FLOW_PHASE_FIN_SEEN:
        case TCP_FLOW_PHASE_FIN_BIDIRECTIONAL:
        case TCP_FLOW_PHASE_CLOSED:
        case TCP_FLOW_PHASE_RESET:
            return true;
    }

    return false;
}

/**
 * @brief 把TCP标志位组合转换为状态机事件。
 */
static tcp_flow_event_t tcp_flow_event_from_flags(
    uint16_t tcp_flags)
{
    bool has_syn;
    bool has_ack;
    bool has_fin;
    bool has_rst;

    has_syn = (tcp_flags & TCP_FLAG_SYN) != 0U;
    has_ack = (tcp_flags & TCP_FLAG_ACK) != 0U;
    has_fin = (tcp_flags & TCP_FLAG_FIN) != 0U;
    has_rst = (tcp_flags & TCP_FLAG_RST) != 0U;

    /*
     * RST表示立即中止，因此它的优先级最高。
     *
     * 即使一个异常报文同时设置RST和其他标志，
     * 状态机也按RESET处理。
     */
    if (has_rst) {
        return TCP_FLOW_EVENT_RST;
    }

    /*
     * FIN+ACK在正常关闭中很常见。
     *
     * 当前观察模型优先把它识别为FIN事件；
     * ACK的精确确认语义以后结合序列号处理。
     */
    if (has_fin) {
        return TCP_FLOW_EVENT_FIN;
    }

    if (has_syn && has_ack) {
        return TCP_FLOW_EVENT_SYN_ACK;
    }

    if (has_syn) {
        return TCP_FLOW_EVENT_SYN;
    }

    if (has_ack) {
        return TCP_FLOW_EVENT_ACK;
    }

    return TCP_FLOW_EVENT_OTHER;
}

int tcp_flow_state_observe(
    tcp_flow_state_t *state,
    flow_direction_t direction,
    uint16_t tcp_flags)
{
    tcp_flow_state_t updated_state;
    tcp_flow_event_t event;

    if (state == NULL ||
        !state->initialized ||
        !tcp_flow_direction_is_valid(direction) ||
        !tcp_flow_phase_is_valid(state->phase)) {
        return EINVAL;
    }

    updated_state = *state;
    event = tcp_flow_event_from_flags(tcp_flags);

    /*
     * CLOSED和RESET是当前版本的终止状态。
     *
     * 后续观察到的迟到包或重传包不会重新打开连接。
     * 同一五元组真正重新建立连接的问题将在后续处理。
     */
    if (updated_state.phase ==
            TCP_FLOW_PHASE_CLOSED ||
        updated_state.phase ==
            TCP_FLOW_PHASE_RESET) {
        *state = updated_state;
        return 0;
    }

    /*
     * RST可以中止握手、活动连接或正在关闭的连接。
     */
    if (event == TCP_FLOW_EVENT_RST) {
        updated_state.phase =
            TCP_FLOW_PHASE_RESET;

        *state = updated_state;
        return 0;
    }

    /*
     * FIN处理独立于握手状态。
     *
     * 这样即使抓包从连接中途或关闭阶段开始，
     * 仍然可以记录已经观察到的关闭过程。
     */
    if (event == TCP_FLOW_EVENT_FIN) {
        if (updated_state.phase == TCP_FLOW_PHASE_FIN_SEEN) {
            if (direction !=
                updated_state.first_fin_direction) {
                updated_state.phase =
                    TCP_FLOW_PHASE_FIN_BIDIRECTIONAL;
            }

            /*
             * 相同方向再次发送FIN视为FIN重传。
             */
        } else if (updated_state.phase !=
                   TCP_FLOW_PHASE_FIN_BIDIRECTIONAL) {
            updated_state.phase =
                TCP_FLOW_PHASE_FIN_SEEN;

            updated_state.first_fin_direction =
                direction;
        }

        *state = updated_state;
        return 0;
    }

    switch (updated_state.phase) {
        case TCP_FLOW_PHASE_UNOBSERVED:
            if (event == TCP_FLOW_EVENT_SYN) {
                updated_state.phase =
                    TCP_FLOW_PHASE_SYN_SEEN;

                updated_state.initiator_direction =
                    direction;
            } else {
                updated_state.phase =
                    TCP_FLOW_PHASE_MIDSTREAM;
            }

            break;

        case TCP_FLOW_PHASE_SYN_SEEN:
            if (event == TCP_FLOW_EVENT_SYN &&
                direction ==
                    updated_state.initiator_direction) {
                break;
            }

            if (event == TCP_FLOW_EVENT_SYN_ACK &&
                direction !=
                    updated_state.initiator_direction) {
                updated_state.phase =
                    TCP_FLOW_PHASE_SYN_ACK_SEEN;

                break;
            }

            updated_state.phase =
                TCP_FLOW_PHASE_MIDSTREAM;

            break;

        case TCP_FLOW_PHASE_SYN_ACK_SEEN:
            if (event == TCP_FLOW_EVENT_SYN_ACK &&
                direction !=
                    updated_state.initiator_direction) {
                break;
            }

            if (event == TCP_FLOW_EVENT_ACK &&
                direction ==
                    updated_state.initiator_direction) {
                updated_state.phase =
                    TCP_FLOW_PHASE_ESTABLISHED;

                updated_state.handshake_completed =
                    true;

                break;
            }

            updated_state.phase =
                TCP_FLOW_PHASE_MIDSTREAM;

            break;

        case TCP_FLOW_PHASE_ESTABLISHED:
        case TCP_FLOW_PHASE_MIDSTREAM:
        case TCP_FLOW_PHASE_FIN_SEEN:
            /*
             * 普通ACK或数据包不改变这些状态。
             *
             * FIN_SEEN中的反向ACK可能确认第一个FIN，
             * 但连接仍可能长期保持半关闭，因此暂不认为CLOSED。
             */
            break;

        case TCP_FLOW_PHASE_FIN_BIDIRECTIONAL:
            /*
             * 第一个发送FIN的一方收到对端FIN后，通常会发送
             * 最终ACK。观察到该方向的ACK后记为CLOSED。
             */
            if (event == TCP_FLOW_EVENT_ACK &&
                direction ==
                    updated_state.first_fin_direction) {
                updated_state.phase =
                    TCP_FLOW_PHASE_CLOSED;
            }

            break;

        case TCP_FLOW_PHASE_CLOSED:
        case TCP_FLOW_PHASE_RESET:
            /*
             * 这两个状态已在switch前提前处理。
             */
            break;
    }

    *state = updated_state;

    return 0;
}
