#include "analyzer/tcp.h"

#include "analyzer/ipv4.h"

#include <errno.h>

/*
 * TCP头部长度字段位于第12字节的高4位。
 */
#define TCP_DATA_OFFSET_SHIFT 4U
#define TCP_DATA_OFFSET_MASK UINT8_C(0xF0)

/*
 * NS标志位于TCP头部第12字节的最低位。
 * 其他8个控制标志位于第13字节。
 */
#define TCP_NS_MASK UINT8_C(0x01)

/*
 * TCP头部长度以32位字为单位。
 */
#define TCP_HEADER_WORD_SIZE 4U

/**
 * @brief 在统一结果对象中记录TCP解析错误。
 *
 * @return 状态记录成功时返回original_error；
 *         状态记录失败时返回packet_info_set_error的错误码。
 */
static int tcp_record_error(packet_info_t *packet_info,
                            packet_parse_status_t status,
                            int original_error,
                            size_t error_offset)
{
    int state_error;

    state_error = packet_info_set_error(
        packet_info,
        status,
        PACKET_PARSE_LAYER_TCP,
        original_error,
        error_offset
    );

    if (state_error != 0) {
        return state_error;
    }

    return original_error;
}

int tcp_parse(const uint8_t *frame_data,
              size_t frame_length,
              packet_info_t *packet_info)
{
    byte_cursor_t segment_cursor;
    byte_cursor_t base_header_cursor;

    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence_number;
    uint32_t acknowledgment_number;
    uint8_t data_offset_and_ns;
    uint8_t control_flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;

    uint8_t data_offset_words;
    uint16_t combined_flags;

    size_t declared_segment_length;
    size_t header_length;
    size_t options_length;
    size_t captured_segment_end;

    packet_info_t updated_info;
    int error_code;

    /*
     * TCP解析必须建立在成功的IPv4解析结果上。
     *
     * frame_length必须等于captured_length，防止调用者错误使用
     * wire_length访问并不存在的内存。
     */
    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_ipv4 ||
        packet_info->has_tcp ||
        packet_info->parse_status !=
            PACKET_PARSE_STATUS_NOT_STARTED ||
        packet_info->ipv4_protocol != IPV4_PROTOCOL_TCP ||
        packet_info->ipv4_total_length <
            packet_info->ipv4_header_length ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U)) {
        return EINVAL;
    }

    /*
     * 一个TCP段可能被IPv4拆成多个分片。
     *
     * 非首分片通常没有TCP头部；首分片也可能只包含TCP段的一部分。
     * 在实现IPv4分片重组之前，直接拒绝分片比返回不完整结果更可靠。
     */
    if (packet_info->ipv4_more_fragments ||
        packet_info->ipv4_fragment_offset != UINT16_C(0)) {
        return tcp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_UNSUPPORTED,
            ENOTSUP,
            packet_info->ipv4_payload_offset
        );
    }

    /*
     * IPv4总长度包含IPv4头部，所以减去头部长度后，
     * 才是协议声明的完整TCP段长度。
     */
    declared_segment_length =
        (size_t)packet_info->ipv4_total_length -
        (size_t)packet_info->ipv4_header_length;

    /*
     * 即使捕获缓冲区中有额外字节，IPv4声明的TCP段长度小于20字节
     * 仍然属于畸形报文，不是捕获截断。
     */
    if (declared_segment_length < TCP_MIN_HEADER_LENGTH) {
        return tcp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_MALFORMED,
            EBADMSG,
            packet_info->ipv4_payload_offset
        );
    }

    error_code = ipv4_payload_view(
        frame_data,
        frame_length,
        packet_info,
        &segment_cursor
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 先原子性地切出固定20字节TCP基础头部。
     *
     * 如果捕获数据不足，父游标不会移动，也不会发布任何TCP字段。
     */
    error_code = byte_cursor_read_slice(
        &segment_cursor,
        TCP_MIN_HEADER_LENGTH,
        &base_header_cursor
    );

    if (error_code == ENODATA) {
        captured_segment_end =
            packet_info->ipv4_payload_offset +
            packet_info->ipv4_payload_length;

        return tcp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_TRUNCATED,
            ENODATA,
            captured_segment_end
        );
    }

    if (error_code != 0) {
        return error_code;
    }

    /*
     * TCP多字节数字均采用网络大端序。
     */
    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &source_port
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &destination_port
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be32(
        &base_header_cursor,
        &sequence_number
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be32(
        &base_header_cursor,
        &acknowledgment_number
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_u8(
        &base_header_cursor,
        &data_offset_and_ns
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_u8(
        &base_header_cursor,
        &control_flags
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &window_size
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &checksum
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &urgent_pointer
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * Data Offset位于第12字节高4位，单位是32位字。
     *
     * 例如0x50的高4位是5：
     *
     * 5 × 4字节 = 20字节TCP头部。
     */
    data_offset_words =
        (uint8_t)(
            (data_offset_and_ns & TCP_DATA_OFFSET_MASK) >>
            TCP_DATA_OFFSET_SHIFT
        );

    header_length =
        (size_t)data_offset_words * TCP_HEADER_WORD_SIZE;

    /*
     * TCP头部小于20字节，或者超过协议允许的60字节，
     * 都属于字段内容不合法。
     */
    if (header_length < TCP_MIN_HEADER_LENGTH ||
        header_length > TCP_MAX_HEADER_LENGTH) {
        return tcp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_MALFORMED,
            EBADMSG,
            packet_info->ipv4_payload_offset + 12U
        );
    }

    /*
     * TCP头部声明的长度不能超过IPv4声明的TCP段总长度。
     *
     * 这种情况是协议字段自相矛盾，因此记录MALFORMED，
     * 而不是记录TRUNCATED。
     */
    if (header_length > declared_segment_length) {
        return tcp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_MALFORMED,
            EBADMSG,
            packet_info->ipv4_payload_offset + 12U
        );
    }

    options_length =
        header_length - TCP_MIN_HEADER_LENGTH;

    /*
     * IPv4声明存在完整TCP头部，但PCAP没有保存完整TCP选项时，
     * 才属于捕获截断。
     */
    if (byte_cursor_remaining(&segment_cursor) <
        options_length) {
        captured_segment_end =
            packet_info->ipv4_payload_offset +
            packet_info->ipv4_payload_length;

        return tcp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_TRUNCATED,
            ENODATA,
            captured_segment_end
        );
    }

    /*
     * 当前阶段不解释具体TCP选项，只安全跳过选项区域。
     *
     * 后续实现TCP状态分析时，可以继续解析MSS、Window Scale、
     * SACK和Timestamp等选项。
     */
    error_code = byte_cursor_skip(
        &segment_cursor,
        options_length
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 第12字节中的NS标志需要移动到uint16_t的第9位，
     * 再与第13字节的8个控制标志组合。
     */
    combined_flags =
        (uint16_t)(
            ((uint16_t)(
                data_offset_and_ns & TCP_NS_MASK
             ) << 8U) |
            (uint16_t)control_flags
        );

    /*
     * 所有读取和验证都成功后，才在副本中建立TCP结果。
     */
    updated_info = *packet_info;

    updated_info.tcp_source_port = source_port;
    updated_info.tcp_destination_port =
        destination_port;
    updated_info.tcp_sequence_number =
        sequence_number;
    updated_info.tcp_acknowledgment_number =
        acknowledgment_number;
    updated_info.tcp_header_length =
        (uint8_t)header_length;
    updated_info.tcp_flags = combined_flags;
    updated_info.tcp_window_size = window_size;
    updated_info.tcp_checksum = checksum;
    updated_info.tcp_urgent_pointer =
        urgent_pointer;

    updated_info.tcp_payload_offset =
        packet_info->ipv4_payload_offset +
        header_length;

    updated_info.tcp_payload_length =
        byte_cursor_remaining(&segment_cursor);

    /*
     * IPv4负载被截断意味着TCP负载也可能没有完整捕获。
     *
     * TCP头部不完整时函数已经提前返回，因此has_tcp为true时，
     * 该标志只描述TCP负载部分是否可能缺失。
     */
    updated_info.tcp_payload_truncated =
        packet_info->ipv4_payload_truncated;

    /*
     * has_tcp最后设置，表示所有TCP字段已经完整有效。
     */
    updated_info.has_tcp = true;

    *packet_info = updated_info;

    /*
     * 当前只完成传输层解析，后续还可能继续解析应用层协议，
     * 因此这里不调用packet_info_mark_complete。
     */
    return 0;
}

int tcp_payload_view(const uint8_t *frame_data,
                     size_t frame_length,
                     const packet_info_t *packet_info,
                     byte_cursor_t *payload)
{
    byte_cursor_t frame_cursor;
    byte_cursor_t new_payload;
    int error_code;

    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_tcp ||
        packet_info->ipv4_protocol != IPV4_PROTOCOL_TCP ||
        payload == NULL ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U) ||
        packet_info->tcp_payload_offset > frame_length ||
        packet_info->tcp_payload_length >
            frame_length -
                packet_info->tcp_payload_offset) {
        return EINVAL;
    }

    error_code = byte_cursor_init(
        &frame_cursor,
        frame_data,
        frame_length
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_skip(
        &frame_cursor,
        packet_info->tcp_payload_offset
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_slice(
        &frame_cursor,
        packet_info->tcp_payload_length,
        &new_payload
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 子游标完整建立后再发布结果。
     */
    *payload = new_payload;

    return 0;
}