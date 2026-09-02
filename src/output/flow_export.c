#include "analyzer/flow_export.h"
#include "analyzer/tcp_flow_state.h"
#include "analyzer/ipv4.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>

int flow_export_write_csv_header(FILE *output)
{
    if (output == NULL) {
        return EINVAL;
    }

    /*
     * 表头是固定字符串，不需要格式化参数，因此使用fputs。
     *
     * fputs不会自动添加换行，所以字符串末尾显式包含'\n'。
     */
    if (fputs(
            "protocol,"
            "tcp_state,"
            "endpoint_a_ip,"
            "endpoint_a_port,"
            "endpoint_b_ip,"
            "endpoint_b_port,"
            "a_to_b_packets,"
            "a_to_b_captured_bytes,"
            "a_to_b_wire_bytes,"
            "b_to_a_packets,"
            "b_to_a_captured_bytes,"
            "b_to_a_wire_bytes,"
            "first_seen_seconds,"
            "first_seen_microseconds,"
            "last_seen_seconds,"
            "last_seen_microseconds\n",
            output) == EOF) {
        return EIO;
    }

    return 0;
}

int flow_export_write_csv_record(
    FILE *output,
    const flow_record_t *record)
{
    char endpoint_a_address[IPV4_ADDRESS_STRING_SIZE];
    char endpoint_b_address[IPV4_ADDRESS_STRING_SIZE];

    const char *tcp_state_name;

    int error_code;

    /*
     * 只有完整初始化且时间戳有效的记录才能导出。
     *
     * 在写入任何内容之前完成验证，避免参数错误时产生半行CSV。
     */
    if (output == NULL ||
        record == NULL ||
        !record->initialized ||
        record->first_seen.microseconds < INT32_C(0) ||
        record->first_seen.microseconds > INT32_C(999999) ||
        record->last_seen.microseconds < INT32_C(0) ||
        record->last_seen.microseconds > INT32_C(999999)) {
        return EINVAL;
    }

    /*
    * TCP流必须携带已经初始化的状态对象。
    *
    * 非TCP流没有TCP生命周期，使用not-applicable表示，
    * 并拒绝非TCP记录意外携带已经初始化的TCP状态。
    */
    if (record->key.protocol == IPV4_PROTOCOL_TCP) {
        if (!record->tcp_state.initialized) {
            return EINVAL;
        }

        tcp_state_name = tcp_flow_phase_name(
            record->tcp_state.phase
        );
    } else {
        if (record->tcp_state.initialized) {
            return EINVAL;
        }

        tcp_state_name = "not-applicable";
    }

    error_code = ipv4_format_address(
        record->key.endpoint_a.ipv4_address,
        endpoint_a_address,
        sizeof(endpoint_a_address)
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = ipv4_format_address(
        record->key.endpoint_b.ipv4_address,
        endpoint_b_address,
        sizeof(endpoint_b_address)
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * IPv4地址只包含数字和点，不包含逗号、双引号或换行，
     * tcp_state名称只包含小写字母和短横线，均不包含逗号、双引号或换行。
     * 当前其他字段也都是整数，因此暂时不需要CSV转义。
     *
     * protocol和port转换成unsigned int，是因为uint8_t和uint16_t
     * 传给可变参数函数时会发生整数提升。显式转换可以让参数类型
     * 与%u的要求保持一致。
     */
    if (fprintf(
            output,
            "%u,%s,%s,%u,%s,%u,"
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ","
            "%" PRId64 ",%" PRId32 ","
            "%" PRId64 ",%" PRId32 "\n",
            (unsigned int)record->key.protocol,
            tcp_state_name,
            endpoint_a_address,
            (unsigned int)record->key.endpoint_a.port,
            endpoint_b_address,
            (unsigned int)record->key.endpoint_b.port,
            record->a_to_b.packet_count,
            record->a_to_b.captured_byte_count,
            record->a_to_b.wire_byte_count,
            record->b_to_a.packet_count,
            record->b_to_a.captured_byte_count,
            record->b_to_a.wire_byte_count,
            record->first_seen.seconds,
            record->first_seen.microseconds,
            record->last_seen.seconds,
            record->last_seen.microseconds) < 0) {
        return EIO;
    }

    return 0;
}