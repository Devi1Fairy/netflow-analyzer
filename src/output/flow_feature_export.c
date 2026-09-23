#include "analyzer/flow_feature_export.h"
#include "analyzer/ipv4.h"
#include "analyzer/tcp_flow_state.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <stddef.h>

/**
 * @brief 判断TCP阶段是否是当前版本定义的合法枚举值。
 */
static bool flow_feature_export_tcp_phase_is_valid(
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

    default:
        return false;
    }
}

/**
 * @brief 判断double是否是有限且非负的数值。
 *
 * NaN与任何数值比较都返回false；
 * 正无穷大于DBL_MAX；
 * 负数和负无穷小于0。
 *
 * 因此不依赖math库也能拒绝NaN和正负无穷。
 */
static bool flow_feature_export_nonnegative_is_valid(
    double value)
{
    return value >= 0.0 && value <= DBL_MAX;
}

/**
 * @brief 判断不平衡度是否位于闭区间0.0～1.0。
 *
 * 该比较也会拒绝NaN和正负无穷。
 */
static bool flow_feature_export_ratio_is_valid(
    double value)
{
    return value >= 0.0 && value <= 1.0;
}

/**
 * @brief 验证特征中的协议和TCP状态字段关系。
 */
static int flow_feature_export_validate_protocol_state(
    const flow_features_t *features,
    const char **tcp_phase_name)
{
    if (features == NULL || tcp_phase_name == NULL) {
        return EINVAL;
    }

    switch (features->protocol) {
    case IPV4_PROTOCOL_TCP:
        if (!features->tcp_state_applicable ||
            !flow_feature_export_tcp_phase_is_valid(
                features->tcp_phase
            )) {
            return EINVAL;
        }

        *tcp_phase_name = tcp_flow_phase_name(
            features->tcp_phase
        );

        return 0;

    case IPV4_PROTOCOL_UDP:
    case IPV4_PROTOCOL_ICMP:
        /*
         * 非TCP流使用唯一的规范表示：
         *
         * applicable = false
         * phase      = UNOBSERVED
         * handshake  = false
         */
        if (features->tcp_state_applicable ||
            features->tcp_phase !=
                TCP_FLOW_PHASE_UNOBSERVED ||
            features->tcp_handshake_completed) {
            return EINVAL;
        }

        *tcp_phase_name = "not-applicable";

        return 0;

    default:
        return ENOTSUP;
    }
}

/**
 * @brief 验证准备导出的数值特征。
 */
static int flow_feature_export_validate_values(
    const flow_features_t *features)
{
    if (features == NULL || !features->initialized) {
        return EINVAL;
    }

    /*
     * 一条特征记录必须来源于至少一个数据包。
     */
    if (features->total_packet_count == UINT64_C(0)) {
        return EINVAL;
    }

    /*
     * 实际捕获字节数不能大于线路原始字节数。
     */
    if (features->total_captured_byte_count >
        features->total_wire_byte_count) {
        return EINVAL;
    }

    if (!flow_feature_export_nonnegative_is_valid(
            features->mean_captured_bytes_per_packet
        ) ||
        !flow_feature_export_nonnegative_is_valid(
            features->mean_wire_bytes_per_packet
        )) {
        return EINVAL;
    }

    if (!flow_feature_export_ratio_is_valid(
            features->packet_count_imbalance_ratio
        ) ||
        !flow_feature_export_ratio_is_valid(
            features->wire_byte_count_imbalance_ratio
        )) {
        return EINVAL;
    }

    return 0;
}

int flow_feature_export_write_csv_header(FILE *output)
{
    if (output == NULL) {
        return EINVAL;
    }

    if (fputs(
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
            "tcp_handshake_completed\n",
            output) == EOF) {
        return EIO;
    }

    return 0;
}

int flow_feature_export_write_csv_record(
    FILE *output,
    const flow_features_t *features)
{
    const char *tcp_phase_name;
    int error_code;

    if (output == NULL || features == NULL) {
        return EINVAL;
    }

    /*
     * 在第一次写入之前完成全部验证。
     *
     * 参数或状态错误不会向CSV留下半行数据。
     */
    error_code = flow_feature_export_validate_values(
        features
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code =
        flow_feature_export_validate_protocol_state(
            features,
            &tcp_phase_name
        );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * DBL_DECIMAL_DIG表示：
     *
     * 将double转换为十进制文本后，再读取回来仍能恢复原始值
     * 所需的十进制有效数字数量。
     *
     * %.*g中的*由对应的int参数提供精度。
     */
    if (fprintf(
            output,
            "%s,%u,"
            "%" PRIu64 ","
            "%" PRIu64 ","
            "%" PRIu64 ","
            "%" PRIu64 ","
            "%.*g,%.*g,%.*g,%.*g,"
            "%u,%s,%u\n",
            FLOW_FEATURE_EXPORT_SCHEMA_VERSION,
            (unsigned int)features->protocol,
            features->duration_microseconds,
            features->total_packet_count,
            features->total_captured_byte_count,
            features->total_wire_byte_count,
            (int)DBL_DECIMAL_DIG,
            features->mean_captured_bytes_per_packet,
            (int)DBL_DECIMAL_DIG,
            features->mean_wire_bytes_per_packet,
            (int)DBL_DECIMAL_DIG,
            features->packet_count_imbalance_ratio,
            (int)DBL_DECIMAL_DIG,
            features->wire_byte_count_imbalance_ratio,
            (unsigned int)
                features->tcp_state_applicable,
            tcp_phase_name,
            (unsigned int)
                features->tcp_handshake_completed) < 0) {
        return EIO;
    }

    return 0;
}
