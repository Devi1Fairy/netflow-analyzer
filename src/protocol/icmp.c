#include "analyzer/icmp.h"

#include "analyzer/ipv4.h"

#include <errno.h>
#include <stdbool.h>

/**
 * @brief 判断ICMP类型是否包含Echo标识符和序列号。
 */
static bool icmp_type_is_echo(uint8_t type)
{
    return type == ICMP_TYPE_ECHO_REQUEST ||
           type == ICMP_TYPE_ECHO_REPLY;
}

/**
 * @brief 在统一结果对象中记录ICMP解析错误。
 *
 * @return 状态记录成功时返回original_error；
 *         状态记录失败时返回packet_info_set_error的错误码。
 */
static int icmp_record_error(packet_info_t *packet_info,
                             packet_parse_status_t status,
                             int original_error,
                             size_t error_offset)
{
    int state_error;

    state_error = packet_info_set_error(
        packet_info,
        status,
        PACKET_PARSE_LAYER_ICMP,
        original_error,
        error_offset
    );

    if (state_error != 0) {
        return state_error;
    }

    return original_error;
}

int icmp_parse(const uint8_t *frame_data,
               size_t frame_length,
               packet_info_t *packet_info)
{
    byte_cursor_t message_cursor;
    byte_cursor_t header_cursor;

    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint32_t rest_of_header;

    size_t declared_message_length;
    size_t captured_message_end;

    packet_info_t updated_info;
    int error_code;

    /*
     * ICMP解析必须建立在成功的IPv4解析结果上。
     */
    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_ipv4 ||
        packet_info->has_icmp ||
        packet_info->parse_status !=
            PACKET_PARSE_STATUS_NOT_STARTED ||
        packet_info->ipv4_protocol != IPV4_PROTOCOL_ICMP ||
        packet_info->ipv4_total_length <
            packet_info->ipv4_header_length ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U)) {
        return EINVAL;
    }

    /*
     * IPv4分片必须先完成重组。
     *
     * 即使首分片包含ICMP头部，它也不能代表完整ICMP消息。
     */
    if (packet_info->ipv4_more_fragments ||
        packet_info->ipv4_fragment_offset != UINT16_C(0)) {
        return icmp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_UNSUPPORTED,
            ENOTSUP,
            packet_info->ipv4_payload_offset
        );
    }

    /*
     * IPv4总长度减去IPv4头部长度，得到协议声明的ICMP消息长度。
     */
    declared_message_length =
        (size_t)packet_info->ipv4_total_length -
        (size_t)packet_info->ipv4_header_length;

    /*
     * IPv4声明的负载连8字节ICMP公共头部都容不下，
     * 属于协议字段组合不合法。
     */
    if (declared_message_length < ICMP_HEADER_LENGTH) {
        return icmp_record_error(
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
        &message_cursor
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 先原子性切出固定8字节ICMP公共头部。
     */
    error_code = byte_cursor_read_slice(
        &message_cursor,
        ICMP_HEADER_LENGTH,
        &header_cursor
    );

    if (error_code == ENODATA) {
        captured_message_end =
            packet_info->ipv4_payload_offset +
            packet_info->ipv4_payload_length;

        return icmp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_TRUNCATED,
            ENODATA,
            captured_message_end
        );
    }

    if (error_code != 0) {
        return error_code;
    }

    /*
     * type和code是单字节字段。
     */
    error_code = byte_cursor_read_u8(
        &header_cursor,
        &type
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_u8(
        &header_cursor,
        &code
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * checksum和rest_of_header采用网络大端序。
     */
    error_code = byte_cursor_read_be16(
        &header_cursor,
        &checksum
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be32(
        &header_cursor,
        &rest_of_header
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 所有公共字段读取成功后，在局部副本中建立结果。
     */
    updated_info = *packet_info;

    updated_info.icmp_type = type;
    updated_info.icmp_code = code;
    updated_info.icmp_checksum = checksum;
    updated_info.icmp_rest_of_header =
        rest_of_header;

    updated_info.icmp_has_echo_fields =
        icmp_type_is_echo(type);

    if (updated_info.icmp_has_echo_fields) {
        /*
         * Echo消息的rest_of_header结构：
         *
         * 高16位：identifier；
         * 低16位：sequence。
         */
        updated_info.icmp_identifier =
            (uint16_t)(rest_of_header >> 16U);

        updated_info.icmp_sequence =
            (uint16_t)(
                rest_of_header & UINT32_C(0x0000FFFF)
            );
    } else {
        /*
         * 非Echo类型的后4字节有其他含义，不能错误解释成
         * identifier和sequence。
         */
        updated_info.icmp_identifier = UINT16_C(0);
        updated_info.icmp_sequence = UINT16_C(0);
    }

    updated_info.icmp_payload_offset =
        packet_info->ipv4_payload_offset +
        ICMP_HEADER_LENGTH;

    /*
     * message_cursor已经跳过公共头部，剩余部分就是当前捕获到的
     * ICMP负载。
     */
    updated_info.icmp_payload_length =
        byte_cursor_remaining(&message_cursor);

    updated_info.icmp_payload_truncated =
        packet_info->ipv4_payload_truncated;

    /*
     * has_icmp最后设置，表示以上ICMP结果已经完整有效。
     */
    updated_info.has_icmp = true;

    *packet_info = updated_info;

    /*
     * 是否标记整个数据包解析完成，将由后续统一协议分发器决定。
     */
    return 0;
}

int icmp_payload_view(const uint8_t *frame_data,
                      size_t frame_length,
                      const packet_info_t *packet_info,
                      byte_cursor_t *payload)
{
    byte_cursor_t frame_cursor;
    byte_cursor_t new_payload;
    int error_code;

    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_icmp ||
        packet_info->ipv4_protocol != IPV4_PROTOCOL_ICMP ||
        payload == NULL ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U) ||
        packet_info->icmp_payload_offset > frame_length ||
        packet_info->icmp_payload_length >
            frame_length -
                packet_info->icmp_payload_offset) {
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

    /*
     * 从完整帧起点移动到ICMP公共头部之后。
     */
    error_code = byte_cursor_skip(
        &frame_cursor,
        packet_info->icmp_payload_offset
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 创建只覆盖ICMP负载范围的子游标。
     */
    error_code = byte_cursor_read_slice(
        &frame_cursor,
        packet_info->icmp_payload_length,
        &new_payload
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 完整建立子游标后再发布结果。
     */
    *payload = new_payload;

    return 0;
}