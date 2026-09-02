#include "analyzer/tcp_flow_state.h"
#include "analyzer/tcp.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 在Debug和Release构建中都有效的测试检查宏。
 */
#define TEST_CHECK(condition)                                      \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr,                                        \
                    "[FAIL] %s:%d: %s\n",                          \
                    __FILE__,                                      \
                    __LINE__,                                      \
                    #condition);                                   \
            return EXIT_FAILURE;                                   \
        }                                                          \
    } while (false)

/**
 * @brief 验证新状态对象进入合法的未观察阶段。
 */
static int test_tcp_flow_state_initialization(void)
{
    tcp_flow_state_t state = {0};

    TEST_CHECK(
        tcp_flow_state_init(&state) == 0
    );

    TEST_CHECK(state.initialized);

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_UNOBSERVED
    );

    TEST_CHECK(
        state.initiator_direction ==
            FLOW_DIRECTION_UNKNOWN
    );

    TEST_CHECK(
        state.first_fin_direction ==
            FLOW_DIRECTION_UNKNOWN
    );

    TEST_CHECK(!state.handshake_completed);

    TEST_CHECK(
        tcp_flow_state_init(NULL) == EINVAL
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证重新初始化会清除已有连接阶段和发起方向。
 */
static int test_tcp_flow_state_reinitialization(void)
{
    tcp_flow_state_t state = {
        .phase = TCP_FLOW_PHASE_ESTABLISHED,
        .initiator_direction =
            FLOW_DIRECTION_B_TO_A,
       .first_fin_direction =
            FLOW_DIRECTION_A_TO_B,
        .handshake_completed = true,
        .initialized = true
    };

    TEST_CHECK(
        tcp_flow_state_init(&state) == 0
    );

    TEST_CHECK(state.initialized);

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_UNOBSERVED
    );

    TEST_CHECK(
        state.first_fin_direction ==
            FLOW_DIRECTION_UNKNOWN
    );

    TEST_CHECK(!state.handshake_completed);

    TEST_CHECK(
        state.initiator_direction ==
            FLOW_DIRECTION_UNKNOWN
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证A到B发起的完整握手及合法重传。
 */
static int test_tcp_flow_state_a_to_b_handshake(void)
{
    tcp_flow_state_t state;

    TEST_CHECK(tcp_flow_state_init(&state) == 0);

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_SYN
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_SYN_SEEN
    );

    TEST_CHECK(
        state.initiator_direction ==
            FLOW_DIRECTION_A_TO_B
    );

    /*
     * 初始SYN重传不能破坏当前状态。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_SYN
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_SYN_SEEN
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_SYN | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_SYN_ACK_SEEN
    );

    /*
     * SYN+ACK重传同样保持当前状态。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_SYN | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_SYN_ACK_SEEN
    );

    /*
     * 第三个报文可以同时带有应用数据，因此PSH+ACK
     * 也可以作为完成握手的ACK事件。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_PSH | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_ESTABLISHED
    );

    TEST_CHECK(state.handshake_completed);

    TEST_CHECK(
        state.first_fin_direction ==
            FLOW_DIRECTION_UNKNOWN
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证规范化endpoint_b也可以成为TCP发起方。
 */
static int test_tcp_flow_state_b_to_a_handshake(void)
{
    tcp_flow_state_t state;

    TEST_CHECK(tcp_flow_state_init(&state) == 0);

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_SYN
        ) == 0
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_SYN | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_ESTABLISHED
    );

    TEST_CHECK(state.handshake_completed);

    TEST_CHECK(
        state.first_fin_direction ==
            FLOW_DIRECTION_UNKNOWN
    );

    TEST_CHECK(
        state.initiator_direction ==
            FLOW_DIRECTION_B_TO_A
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证缺少初始握手时进入MIDSTREAM。
 */
static int test_tcp_flow_state_midstream_detection(void)
{
    tcp_flow_state_t state;

    TEST_CHECK(tcp_flow_state_init(&state) == 0);

    /*
     * 状态机看到的第一包就是数据ACK，无法证明此前握手。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_PSH | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_MIDSTREAM
    );

    TEST_CHECK(
        state.initiator_direction ==
            FLOW_DIRECTION_UNKNOWN
    );

    /*
     * MIDSTREAM不会因为之后偶然看到SYN就反推为完整握手。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_SYN
        ) == 0
    );

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_MIDSTREAM
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证无效观察不会修改已有状态。
 */
static int test_tcp_flow_state_invalid_observations(void)
{
    tcp_flow_state_t state;

    TEST_CHECK(tcp_flow_state_init(&state) == 0);

    state.phase = TCP_FLOW_PHASE_SYN_SEEN;
    state.initiator_direction =
        FLOW_DIRECTION_A_TO_B;

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_UNKNOWN,
            TCP_FLAG_SYN
        ) == EINVAL
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_SYN_SEEN
    );

    TEST_CHECK(
        state.initiator_direction ==
            FLOW_DIRECTION_A_TO_B
    );

    state.initialized = false;

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_SYN
        ) == EINVAL
    );

    state.initialized = true;
    state.phase = (tcp_flow_phase_t)999;

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_SYN
        ) == EINVAL
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            NULL,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_SYN
        ) == EINVAL
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证双向FIN、FIN重传和最终ACK。
 */
static int test_tcp_flow_state_graceful_close(void)
{
    tcp_flow_state_t state;

    TEST_CHECK(tcp_flow_state_init(&state) == 0);

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_SYN
        ) == 0
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_SYN | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(state.handshake_completed);

    /*
     * A端首先发送FIN+ACK。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_FIN | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_FIN_SEEN
    );

    TEST_CHECK(
        state.first_fin_direction ==
            FLOW_DIRECTION_A_TO_B
    );

    /*
     * A端重传FIN，仍然只观察到一个方向的FIN。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_FIN | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_FIN_SEEN
    );

    /*
     * B端的普通ACK不会立即关闭整个双向连接。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_FIN_SEEN
    );

    /*
     * B端也发送FIN。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_FIN | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase ==
            TCP_FLOW_PHASE_FIN_BIDIRECTIONAL
    );

    /*
     * 最初发送FIN的A端发送最终ACK。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_CLOSED
    );

    TEST_CHECK(state.handshake_completed);

    /*
     * CLOSED是本版本的终止状态，迟到包不能重新打开它。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_RST
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_CLOSED
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证握手期间和连接建立后的RST。
 */
static int test_tcp_flow_state_reset(void)
{
    tcp_flow_state_t state;

    TEST_CHECK(tcp_flow_state_init(&state) == 0);

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_SYN
        ) == 0
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_RST | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_RESET
    );

    TEST_CHECK(!state.handshake_completed);

    /*
     * RESET是终止状态，后续SYN不能直接复用旧状态。
     */
    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_SYN
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_RESET
    );

    /*
     * 再验证完整握手后收到RST时，
     * handshake_completed历史信息仍然保留。
     */
    TEST_CHECK(tcp_flow_state_init(&state) == 0);

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_SYN
        ) == 0
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_SYN | TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_A_TO_B,
            TCP_FLAG_ACK
        ) == 0
    );

    TEST_CHECK(
        tcp_flow_state_observe(
            &state,
            FLOW_DIRECTION_B_TO_A,
            TCP_FLAG_RST
        ) == 0
    );

    TEST_CHECK(
        state.phase == TCP_FLOW_PHASE_RESET
    );

    TEST_CHECK(state.handshake_completed);

    return EXIT_SUCCESS;
}

/**
 * @brief TCP流状态单元测试入口。
 */
int main(void)
{
    if (test_tcp_flow_state_initialization() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP flow state initialization\n");

    if (test_tcp_flow_state_reinitialization() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP flow state reinitialization\n");

    if (test_tcp_flow_state_a_to_b_handshake() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP A-to-B handshake state\n");

    if (test_tcp_flow_state_b_to_a_handshake() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP B-to-A handshake state\n");

    if (test_tcp_flow_state_midstream_detection() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP midstream detection\n");

    if (test_tcp_flow_state_invalid_observations() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP invalid observations\n");

    if (test_tcp_flow_state_graceful_close() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP graceful close state\n");

    if (test_tcp_flow_state_reset() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP reset state\n");

    return EXIT_SUCCESS;
}