#include "analyzer/flow_features.h"
#include "analyzer/ipv4.h"

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
 * @brief 判断两个浮点数是否在允许误差内相等。
 *
 * 浮点除法结果可能不能被二进制精确表示，因此测试不能直接使用==。
 */
static bool double_nearly_equal(
    double left,
    double right,
    double tolerance)
{
    double difference;

    difference = left - right;

    if (difference < 0.0) {
        difference = -difference;
    }

    return difference <= tolerance;
}

/**
 * @brief 逐字段判断两个特征对象是否完全相同。
 *
 * 这里不使用memcmp，因为C结构体可能包含未定义内容的填充字节。
 *
 * 浮点字段在本函数中用于检查对象是否被修改，
 * 并不是比较两个独立计算结果，因此可以直接使用==。
 */
static bool flow_features_equal(
    const flow_features_t *left,
    const flow_features_t *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }

    return left->protocol == right->protocol &&
           left->tcp_phase == right->tcp_phase &&
           left->duration_microseconds ==
               right->duration_microseconds &&
           left->total_packet_count ==
               right->total_packet_count &&
           left->total_captured_byte_count ==
               right->total_captured_byte_count &&
           left->total_wire_byte_count ==
               right->total_wire_byte_count &&
           left->mean_captured_bytes_per_packet ==
               right->mean_captured_bytes_per_packet &&
           left->mean_wire_bytes_per_packet ==
               right->mean_wire_bytes_per_packet &&
           left->packet_count_imbalance_ratio ==
               right->packet_count_imbalance_ratio &&
           left->wire_byte_count_imbalance_ratio ==
               right->wire_byte_count_imbalance_ratio &&
           left->tcp_state_applicable ==
               right->tcp_state_applicable &&
           left->tcp_handshake_completed ==
               right->tcp_handshake_completed &&
           left->initialized == right->initialized;
}

/**
 * @brief 验证一条正常TCP流的全部第一版特征。
 */
static int test_tcp_flow_features(void)
{
    flow_record_t record = {
        .key = {
            .protocol = IPV4_PROTOCOL_TCP
        },
        .tcp_state = {
            .phase = TCP_FLOW_PHASE_ESTABLISHED,
            .initiator_direction =
                FLOW_DIRECTION_A_TO_B,
            .first_fin_direction =
                FLOW_DIRECTION_UNKNOWN,
            .handshake_completed = true,
            .initialized = true
        },
        .a_to_b = {
            .packet_count = UINT64_C(10),
            .captured_byte_count = UINT64_C(800),
            .wire_byte_count = UINT64_C(1000)
        },
        .b_to_a = {
            .packet_count = UINT64_C(2),
            .captured_byte_count = UINT64_C(300),
            .wire_byte_count = UINT64_C(500)
        },
        .first_seen = {
            .seconds = INT64_C(100),
            .microseconds = INT32_C(900000)
        },
        .last_seen = {
            .seconds = INT64_C(103),
            .microseconds = INT32_C(100000)
        },
        .initialized = true
    };

    flow_features_t features = {0};

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == 0
    );

    TEST_CHECK(features.initialized);
    TEST_CHECK(
        features.protocol == IPV4_PROTOCOL_TCP
    );

    /*
     * 103.100000 - 100.900000 = 2.200000秒。
     *
     * 这个值同时验证了微秒部分需要借位的情况。
     */
    TEST_CHECK(
        features.duration_microseconds ==
            UINT64_C(2200000)
    );

    TEST_CHECK(
        features.total_packet_count ==
            UINT64_C(12)
    );

    TEST_CHECK(
        features.total_captured_byte_count ==
            UINT64_C(1100)
    );

    TEST_CHECK(
        features.total_wire_byte_count ==
            UINT64_C(1500)
    );

    /*
     * 1100 / 12 = 91.666666...
     */
    TEST_CHECK(
        double_nearly_equal(
            features.mean_captured_bytes_per_packet,
            91.6666666667,
            0.000001
        )
    );

    /*
     * 1500 / 12 = 125。
     */
    TEST_CHECK(
        double_nearly_equal(
            features.mean_wire_bytes_per_packet,
            125.0,
            0.000001
        )
    );

    /*
     * 包数不平衡度：
     *
     * |10 - 2| / 12 = 8 / 12 = 0.666666...
     */
    TEST_CHECK(
        double_nearly_equal(
            features.packet_count_imbalance_ratio,
            0.6666666667,
            0.000001
        )
    );

    /*
     * 线路字节不平衡度：
     *
     * |1000 - 500| / 1500 = 1 / 3。
     */
    TEST_CHECK(
        double_nearly_equal(
            features.wire_byte_count_imbalance_ratio,
            0.3333333333,
            0.000001
        )
    );

    TEST_CHECK(features.tcp_state_applicable);
    TEST_CHECK(
        features.tcp_phase ==
            TCP_FLOW_PHASE_ESTABLISHED
    );
    TEST_CHECK(
        features.tcp_handshake_completed
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证非TCP流不会错误发布TCP状态特征。
 */
static int test_udp_flow_features(void)
{
    flow_record_t record = {
        .key = {
            .protocol = IPV4_PROTOCOL_UDP
        },
        .tcp_state = {0},
        .a_to_b = {
            .packet_count = UINT64_C(3),
            .captured_byte_count = UINT64_C(180),
            .wire_byte_count = UINT64_C(240)
        },
        .b_to_a = {
            .packet_count = UINT64_C(1),
            .captured_byte_count = UINT64_C(60),
            .wire_byte_count = UINT64_C(80)
        },
        .first_seen = {
            .seconds = INT64_C(200),
            .microseconds = INT32_C(500000)
        },
        .last_seen = {
            .seconds = INT64_C(200),
            .microseconds = INT32_C(500000)
        },
        .initialized = true
    };

    flow_features_t features = {0};

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == 0
    );

    TEST_CHECK(features.initialized);
    TEST_CHECK(
        features.protocol == IPV4_PROTOCOL_UDP
    );

    /*
     * 多个数据包可能具有相同的捕获时间戳，
     * 因此持续时间为0是合法结果。
     */
    TEST_CHECK(
        features.duration_microseconds ==
            UINT64_C(0)
    );

    TEST_CHECK(
        features.total_packet_count ==
            UINT64_C(4)
    );

    TEST_CHECK(
        features.total_captured_byte_count ==
            UINT64_C(240)
    );

    TEST_CHECK(
        features.total_wire_byte_count ==
            UINT64_C(320)
    );

    TEST_CHECK(
        double_nearly_equal(
            features.mean_captured_bytes_per_packet,
            60.0,
            0.000001
        )
    );

    TEST_CHECK(
        double_nearly_equal(
            features.mean_wire_bytes_per_packet,
            80.0,
            0.000001
        )
    );

    /*
     * |3 - 1| / 4 = 0.5。
     */
    TEST_CHECK(
        double_nearly_equal(
            features.packet_count_imbalance_ratio,
            0.5,
            0.000001
        )
    );

    /*
     * |240 - 80| / 320 = 0.5。
     */
    TEST_CHECK(
        double_nearly_equal(
            features.wire_byte_count_imbalance_ratio,
            0.5,
            0.000001
        )
    );

    /*
     * UDP不使用TCP状态机。
     *
     * tcp_phase只是稳定占位值，必须结合
     * tcp_state_applicable=false理解。
     */
    TEST_CHECK(!features.tcp_state_applicable);
    TEST_CHECK(
        features.tcp_phase ==
            TCP_FLOW_PHASE_UNOBSERVED
    );
    TEST_CHECK(
        !features.tcp_handshake_completed
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证非法输入返回正确错误码，并且不修改输出对象。
 */
static int test_argument_and_protocol_validation(void)
{
    flow_record_t record = {
        .key = {
            .protocol = IPV4_PROTOCOL_UDP
        },
        .tcp_state = {0},
        .a_to_b = {
            .packet_count = UINT64_C(1),
            .captured_byte_count = UINT64_C(64),
            .wire_byte_count = UINT64_C(64)
        },
        .b_to_a = {0},
        .first_seen = {
            .seconds = INT64_C(10),
            .microseconds = INT32_C(0)
        },
        .last_seen = {
            .seconds = INT64_C(10),
            .microseconds = INT32_C(0)
        },
        .initialized = true
    };

    flow_features_t features = {
        .protocol = UINT8_C(99),
        .tcp_phase = TCP_FLOW_PHASE_RESET,
        .duration_microseconds = UINT64_C(11),
        .total_packet_count = UINT64_C(12),
        .total_captured_byte_count = UINT64_C(13),
        .total_wire_byte_count = UINT64_C(14),
        .mean_captured_bytes_per_packet = 15.25,
        .mean_wire_bytes_per_packet = 16.25,
        .packet_count_imbalance_ratio = 0.25,
        .wire_byte_count_imbalance_ratio = 0.75,
        .tcp_state_applicable = true,
        .tcp_handshake_completed = true,
        .initialized = true
    };

    flow_features_t expected_features = features;

    TEST_CHECK(
        flow_features_from_record(
            NULL,
            &features
        ) == EINVAL
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    TEST_CHECK(
        flow_features_from_record(
            &record,
            NULL
        ) == EINVAL
    );

    /*
     * record未初始化时，即使其余字段看起来合法，
     * 也不能提取特征。
     */
    record.initialized = false;

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    record.initialized = true;

    /*
     * 250不属于当前支持的ICMP、TCP或UDP。
     */
    record.key.protocol = UINT8_C(250);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == ENOTSUP
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    /*
     * UDP流不应该拥有已经初始化的TCP状态对象。
     */
    record.key.protocol = IPV4_PROTOCOL_UDP;
    record.tcp_state.initialized = true;

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    /*
     * TCP流必须拥有已经初始化的TCP状态对象。
     */
    record.key.protocol = IPV4_PROTOCOL_TCP;
    record.tcp_state = (tcp_flow_state_t){0};

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    /*
     * 即使状态对象标记为已初始化，
     * 非法的枚举值仍然不能进入特征数据。
     */
    record.tcp_state.initialized = true;
    record.tcp_state.phase =
        (tcp_flow_phase_t)99;

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证时间戳和方向统计中的非法状态。
 */
static int test_timestamp_and_stats_validation(void)
{
    flow_record_t record = {
        .key = {
            .protocol = IPV4_PROTOCOL_UDP
        },
        .tcp_state = {0},
        .a_to_b = {
            .packet_count = UINT64_C(1),
            .captured_byte_count = UINT64_C(64),
            .wire_byte_count = UINT64_C(64)
        },
        .b_to_a = {0},
        .first_seen = {
            .seconds = INT64_C(10),
            .microseconds = INT32_C(100)
        },
        .last_seen = {
            .seconds = INT64_C(10),
            .microseconds = INT32_C(100)
        },
        .initialized = true
    };

    flow_features_t features = {
        .protocol = UINT8_C(99),
        .tcp_phase = TCP_FLOW_PHASE_RESET,
        .duration_microseconds = UINT64_C(123),
        .total_packet_count = UINT64_C(456),
        .mean_wire_bytes_per_packet = 78.5,
        .initialized = true
    };

    flow_features_t expected_features = features;

    /*
     * 微秒部分的合法范围是0～999999。
     */
    record.first_seen.microseconds = -INT32_C(1);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    record.first_seen.microseconds =
        INT32_C(1000000);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    /*
     * 恢复合法微秒，但让first_seen晚于last_seen。
     */
    record.first_seen.microseconds = INT32_C(100);
    record.first_seen.seconds = INT64_C(11);
    record.last_seen.seconds = INT64_C(10);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    /*
     * 恢复合法时间后，构造一个没有任何数据包的空流。
     */
    record.first_seen.seconds = INT64_C(10);
    record.a_to_b = (flow_direction_stats_t){0};

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    /*
     * 没有数据包却已经产生字节数，方向统计不自洽。
     */
    record.a_to_b.captured_byte_count =
        UINT64_C(1);
    record.a_to_b.wire_byte_count =
        UINT64_C(1);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    /*
     * captured bytes不能大于wire bytes。
     */
    record.a_to_b.packet_count = UINT64_C(1);
    record.a_to_b.captured_byte_count =
        UINT64_C(65);
    record.a_to_b.wire_byte_count =
        UINT64_C(64);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EINVAL
    );

    /*
     * 秒数本身都能放入int64_t，但它们之间的微秒差
     * 无法放入uint64_t，因此应返回EOVERFLOW。
     */
    record.a_to_b.captured_byte_count =
        UINT64_C(64);
    record.a_to_b.wire_byte_count =
        UINT64_C(64);
    record.first_seen.seconds = INT64_MIN;
    record.first_seen.microseconds = INT32_C(0);
    record.last_seen.seconds = INT64_MAX;
    record.last_seen.microseconds = INT32_C(0);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EOVERFLOW
    );

    /*
     * 所有失败路径都不能修改调用者原有输出。
     */
    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证双向统计相加时的uint64_t溢出保护。
 */
static int test_feature_total_overflow(void)
{
    flow_record_t record = {
        .key = {
            .protocol = IPV4_PROTOCOL_UDP
        },
        .tcp_state = {0},
        .a_to_b = {
            .packet_count = UINT64_C(1),
            .captured_byte_count = UINT64_C(0),
            .wire_byte_count = UINT64_C(0)
        },
        .b_to_a = {
            .packet_count = UINT64_C(1),
            .captured_byte_count = UINT64_C(0),
            .wire_byte_count = UINT64_C(0)
        },
        .first_seen = {
            .seconds = INT64_C(1),
            .microseconds = INT32_C(0)
        },
        .last_seen = {
            .seconds = INT64_C(1),
            .microseconds = INT32_C(0)
        },
        .initialized = true
    };

    flow_features_t features = {
        .protocol = UINT8_C(99),
        .duration_microseconds = UINT64_C(123),
        .total_packet_count = UINT64_C(456),
        .initialized = true
    };

    flow_features_t expected_features = features;

    /*
     * UINT64_MAX + 1不能放入uint64_t。
     */
    record.a_to_b.packet_count = UINT64_MAX;
    record.b_to_a.packet_count = UINT64_C(1);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EOVERFLOW
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    /*
     * 包数恢复合法，构造捕获字节总量溢出。
     *
     * 每个方向内部仍满足captured <= wire。
     */
    record.a_to_b.packet_count = UINT64_C(1);
    record.a_to_b.captured_byte_count =
        UINT64_MAX;
    record.a_to_b.wire_byte_count =
        UINT64_MAX;

    record.b_to_a.packet_count = UINT64_C(1);
    record.b_to_a.captured_byte_count =
        UINT64_C(1);
    record.b_to_a.wire_byte_count =
        UINT64_C(1);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EOVERFLOW
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    /*
     * 捕获字节总量恢复为0，单独构造线路字节总量溢出。
     */
    record.a_to_b.captured_byte_count =
        UINT64_C(0);
    record.a_to_b.wire_byte_count =
        UINT64_MAX;

    record.b_to_a.captured_byte_count =
        UINT64_C(0);
    record.b_to_a.wire_byte_count =
        UINT64_C(1);

    TEST_CHECK(
        flow_features_from_record(
            &record,
            &features
        ) == EOVERFLOW
    );

    TEST_CHECK(
        flow_features_equal(
            &features,
            &expected_features
        )
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 流特征提取单元测试入口。
 */
int main(void)
{
    if (test_tcp_flow_features() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP flow features\n");

    if (test_udp_flow_features() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] UDP flow features\n");

    if (test_argument_and_protocol_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] argument and protocol validation\n");

    if (test_timestamp_and_stats_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] timestamp and stats validation\n");

    if (test_feature_total_overflow() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] feature total overflow\n");

    return EXIT_SUCCESS;
}
