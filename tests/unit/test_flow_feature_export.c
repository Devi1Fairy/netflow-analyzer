#include "analyzer/flow_feature_export.h"
#include "analyzer/ipv4.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * @brief 从测试用临时流的开头读取全部文本。
 *
 * buffer_size包含字符串结束符所需空间。
 */
static int read_stream_text(
    FILE *stream,
    char *buffer,
    size_t buffer_size)
{
    size_t bytes_read;
    int next_character;

    if (stream == NULL ||
        buffer == NULL ||
        buffer_size == 0U) {
        return EXIT_FAILURE;
    }

    /*
     * 把stdio用户态缓冲区中的数据提交到临时文件。
     */
    if (fflush(stream) != 0) {
        return EXIT_FAILURE;
    }

    /*
     * 写入后文件位置位于末尾，读取前移动回开头。
     */
    if (fseek(stream, 0L, SEEK_SET) != 0) {
        return EXIT_FAILURE;
    }

    bytes_read = fread(
        buffer,
        sizeof(char),
        buffer_size - 1U,
        stream
    );

    if (ferror(stream)) {
        return EXIT_FAILURE;
    }

    /*
     * 缓冲区正好写满时，再读一个字符判断是否被截断。
     */
    if (bytes_read == buffer_size - 1U) {
        next_character = fgetc(stream);

        if (next_character != EOF) {
            return EXIT_FAILURE;
        }

        if (ferror(stream)) {
            return EXIT_FAILURE;
        }
    }

    buffer[bytes_read] = '\0';

    return EXIT_SUCCESS;
}

/**
 * @brief 验证记录导出失败，并且输出流保持为空。
 */
static int expect_record_error_without_output(
    const flow_features_t *features,
    int expected_error)
{
    char actual_text[16];
    FILE *output;

    output = tmpfile();
    TEST_CHECK(output != NULL);

    TEST_CHECK(
        flow_feature_export_write_csv_record(
            output,
            features
        ) == expected_error
    );

    /*
     * 导出器必须在第一次写入前完成验证。
     */
    TEST_CHECK(
        read_stream_text(
            output,
            actual_text,
            sizeof(actual_text)
        ) == EXIT_SUCCESS
    );

    TEST_CHECK(strcmp(actual_text, "") == 0);
    TEST_CHECK(fclose(output) == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证版本化特征CSV表头和字段顺序。
 */
static int test_feature_csv_header(void)
{
    static const char expected_header[] =
        "schema_version,"
        "protocol,"
        "duration_microseconds,"
        "total_packet_count,"
        "total_captured_byte_count,"
        "total_wire_byte_count,"
        "mean_captured_bytes_per_packet,"
        "mean_wire_bytes_per_packet,"
        "packet_count_imbalance_ratio,"
        "wire_byte_count_imbalance_ratio,"
        "tcp_state_applicable,"
        "tcp_phase,"
        "tcp_handshake_completed\n";

    char actual_text[512];
    FILE *output;

    output = tmpfile();
    TEST_CHECK(output != NULL);

    TEST_CHECK(
        flow_feature_export_write_csv_header(
            output
        ) == 0
    );

    TEST_CHECK(
        read_stream_text(
            output,
            actual_text,
            sizeof(actual_text)
        ) == EXIT_SUCCESS
    );

    TEST_CHECK(
        strcmp(actual_text, expected_header) == 0
    );

    TEST_CHECK(fclose(output) == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证TCP特征被格式化成稳定的CSV记录。
 */
static int test_tcp_feature_csv_record(void)
{
    static const char expected_record[] =
        "flow_features_v1,"
        "6,"
        "2200000,"
        "4,"
        "240,"
        "320,"
        "60,"
        "80,"
        "0.5,"
        "0.25,"
        "1,"
        "established,"
        "1\n";

    flow_features_t features = {
        .protocol = IPV4_PROTOCOL_TCP,
        .tcp_phase = TCP_FLOW_PHASE_ESTABLISHED,
        .duration_microseconds = UINT64_C(2200000),
        .total_packet_count = UINT64_C(4),
        .total_captured_byte_count = UINT64_C(240),
        .total_wire_byte_count = UINT64_C(320),
        .mean_captured_bytes_per_packet = 60.0,
        .mean_wire_bytes_per_packet = 80.0,
        .packet_count_imbalance_ratio = 0.5,
        .wire_byte_count_imbalance_ratio = 0.25,
        .tcp_state_applicable = true,
        .tcp_handshake_completed = true,
        .initialized = true
    };

    char actual_text[512];
    FILE *output;

    output = tmpfile();
    TEST_CHECK(output != NULL);

    TEST_CHECK(
        flow_feature_export_write_csv_record(
            output,
            &features
        ) == 0
    );

    TEST_CHECK(
        read_stream_text(
            output,
            actual_text,
            sizeof(actual_text)
        ) == EXIT_SUCCESS
    );

    TEST_CHECK(
        strcmp(actual_text, expected_record) == 0
    );

    TEST_CHECK(fclose(output) == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证UDP特征使用非TCP规范表示。
 */
static int test_udp_feature_csv_record(void)
{
    static const char expected_record[] =
        "flow_features_v1,"
        "17,"
        "0,"
        "4,"
        "240,"
        "320,"
        "60,"
        "80,"
        "0.5,"
        "0.5,"
        "0,"
        "not-applicable,"
        "0\n";

    flow_features_t features = {
        .protocol = IPV4_PROTOCOL_UDP,
        .tcp_phase = TCP_FLOW_PHASE_UNOBSERVED,
        .duration_microseconds = UINT64_C(0),
        .total_packet_count = UINT64_C(4),
        .total_captured_byte_count = UINT64_C(240),
        .total_wire_byte_count = UINT64_C(320),
        .mean_captured_bytes_per_packet = 60.0,
        .mean_wire_bytes_per_packet = 80.0,
        .packet_count_imbalance_ratio = 0.5,
        .wire_byte_count_imbalance_ratio = 0.5,
        .tcp_state_applicable = false,
        .tcp_handshake_completed = false,
        .initialized = true
    };

    char actual_text[512];
    FILE *output;

    output = tmpfile();
    TEST_CHECK(output != NULL);

    TEST_CHECK(
        flow_feature_export_write_csv_record(
            output,
            &features
        ) == 0
    );

    TEST_CHECK(
        read_stream_text(
            output,
            actual_text,
            sizeof(actual_text)
        ) == EXIT_SUCCESS
    );

    TEST_CHECK(
        strcmp(actual_text, expected_record) == 0
    );

    TEST_CHECK(fclose(output) == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证无效参数和协议状态不会产生CSV内容。
 */
static int test_feature_csv_argument_validation(void)
{
    flow_features_t valid_features = {
        .protocol = IPV4_PROTOCOL_UDP,
        .tcp_phase = TCP_FLOW_PHASE_UNOBSERVED,
        .duration_microseconds = UINT64_C(100),
        .total_packet_count = UINT64_C(2),
        .total_captured_byte_count = UINT64_C(100),
        .total_wire_byte_count = UINT64_C(120),
        .mean_captured_bytes_per_packet = 50.0,
        .mean_wire_bytes_per_packet = 60.0,
        .packet_count_imbalance_ratio = 0.0,
        .wire_byte_count_imbalance_ratio = 0.0,
        .tcp_state_applicable = false,
        .tcp_handshake_completed = false,
        .initialized = true
    };

    flow_features_t features;

    TEST_CHECK(
        flow_feature_export_write_csv_header(NULL) ==
            EINVAL
    );

    TEST_CHECK(
        flow_feature_export_write_csv_record(
            NULL,
            &valid_features
        ) == EINVAL
    );

    TEST_CHECK(
        expect_record_error_without_output(
            NULL,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 未初始化的特征对象不能导出。
     */
    features = valid_features;
    features.initialized = false;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 当前只支持ICMP、TCP和UDP。
     */
    features = valid_features;
    features.protocol = UINT8_C(250);

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            ENOTSUP
        ) == EXIT_SUCCESS
    );

    /*
     * TCP流必须声明TCP状态字段有效。
     */
    features = valid_features;
    features.protocol = IPV4_PROTOCOL_TCP;
    features.tcp_phase =
        TCP_FLOW_PHASE_ESTABLISHED;
    features.tcp_state_applicable = false;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * TCP流不能使用未定义的阶段枚举值。
     */
    features.tcp_state_applicable = true;
    features.tcp_phase =
        (tcp_flow_phase_t)99;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 非TCP流不能声明TCP状态字段有效。
     */
    features = valid_features;
    features.tcp_state_applicable = true;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 非TCP流必须使用UNOBSERVED作为内部占位阶段。
     */
    features = valid_features;
    features.tcp_phase =
        TCP_FLOW_PHASE_ESTABLISHED;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 非TCP流不可能完成TCP三次握手。
     */
    features = valid_features;
    features.tcp_handshake_completed = true;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证不能进入训练数据集的非法数值。
 */
static int test_feature_csv_value_validation(void)
{
    flow_features_t valid_features = {
        .protocol = IPV4_PROTOCOL_UDP,
        .tcp_phase = TCP_FLOW_PHASE_UNOBSERVED,
        .duration_microseconds = UINT64_C(100),
        .total_packet_count = UINT64_C(2),
        .total_captured_byte_count = UINT64_C(100),
        .total_wire_byte_count = UINT64_C(120),
        .mean_captured_bytes_per_packet = 50.0,
        .mean_wire_bytes_per_packet = 60.0,
        .packet_count_imbalance_ratio = 0.0,
        .wire_byte_count_imbalance_ratio = 0.0,
        .tcp_state_applicable = false,
        .tcp_handshake_completed = false,
        .initialized = true
    };

    flow_features_t features;

    /*
     * 特征必须来源于至少一个数据包。
     */
    features = valid_features;
    features.total_packet_count = UINT64_C(0);

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 捕获字节数不能超过线路原始字节数。
     */
    features = valid_features;
    features.total_captured_byte_count =
        UINT64_C(121);

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 平均包长不能为负数。
     */
    features = valid_features;
    features.mean_captured_bytes_per_packet =
        -1.0;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 无穷值不能进入数据集。
     */
    features = valid_features;
    features.mean_wire_bytes_per_packet =
        INFINITY;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * NaN不能进入数据集。
     *
     * NaN表示“不是一个数”，会破坏许多训练和归一化流程。
     */
    features = valid_features;
    features.mean_captured_bytes_per_packet =
        NAN;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    /*
     * 不平衡度的合法范围是0.0～1.0。
     */
    features = valid_features;
    features.packet_count_imbalance_ratio =
        -0.1;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    features = valid_features;
    features.wire_byte_count_imbalance_ratio =
        1.1;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    features = valid_features;
    features.packet_count_imbalance_ratio =
        NAN;

    TEST_CHECK(
        expect_record_error_without_output(
            &features,
            EINVAL
        ) == EXIT_SUCCESS
    );

    return EXIT_SUCCESS;
}

/**
 * @brief 验证非整除浮点特征保留足够的十进制精度。
 */
static int test_feature_csv_double_precision(void)
{
    static const char expected_record[] =
        "flow_features_v1,"
        "1,"
        "3,"
        "3,"
        "2,"
        "3,"
        "0.66666666666666663,"
        "1,"
        "0.33333333333333331,"
        "0.33333333333333331,"
        "0,"
        "not-applicable,"
        "0\n";

    flow_features_t features = {
        .protocol = IPV4_PROTOCOL_ICMP,
        .tcp_phase = TCP_FLOW_PHASE_UNOBSERVED,
        .duration_microseconds = UINT64_C(3),
        .total_packet_count = UINT64_C(3),
        .total_captured_byte_count = UINT64_C(2),
        .total_wire_byte_count = UINT64_C(3),
        .mean_captured_bytes_per_packet =
            2.0 / 3.0,
        .mean_wire_bytes_per_packet = 1.0,
        .packet_count_imbalance_ratio =
            1.0 / 3.0,
        .wire_byte_count_imbalance_ratio =
            1.0 / 3.0,
        .tcp_state_applicable = false,
        .tcp_handshake_completed = false,
        .initialized = true
    };

    char actual_text[512];
    FILE *output;

    output = tmpfile();
    TEST_CHECK(output != NULL);

    TEST_CHECK(
        flow_feature_export_write_csv_record(
            output,
            &features
        ) == 0
    );

    TEST_CHECK(
        read_stream_text(
            output,
            actual_text,
            sizeof(actual_text)
        ) == EXIT_SUCCESS
    );

    TEST_CHECK(
        strcmp(actual_text, expected_record) == 0
    );

    TEST_CHECK(fclose(output) == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 流特征CSV导出单元测试入口。
 */
int main(void)
{
    if (test_feature_csv_header() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] feature CSV header\n");

    if (test_tcp_feature_csv_record() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] TCP feature CSV record\n");

    if (test_udp_feature_csv_record() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] UDP feature CSV record\n");

    if (test_feature_csv_argument_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] feature CSV argument validation\n");

    if (test_feature_csv_value_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] feature CSV value validation\n");

    if (test_feature_csv_double_precision() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] feature CSV double precision\n");

    return EXIT_SUCCESS;
}
