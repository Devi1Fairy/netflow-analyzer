#include "analyzer/udp.h"

#include "analyzer/ipv4.h"

#include <errno.h>

/**
 * @brief 在统一结果对象中记录UDP解析错误。
 *
 * @return 状态记录成功时返回original_error；
 *         状态记录失败时返回packet_info_set_error的错误码。
 */
static int udp_record_error(packet_info_t *packet_info,
                            packet_parse_status_t status,
                            int original_error,
                            size_t error_offset)
{
    int state_error;

    state_error = packet_info_set_error(
        packet_info,
        status,
        PACKET_PARSE_LAYER_UDP,
        original_error,
        error_offset
    );

    if (state_error != 0) {
        return state_error;
    }

    return original_error;
}

int udp_parse(const uint8_t *frame_data,
              size_t frame_length,
              packet_info_t *packet_info)
{
    byte_cursor_t datagram_cursor;
    byte_cursor_t header_cursor;

    uint16_t source_port;
    uint16_t destination_port;
    uint16_t udp_length;
    uint16_t checksum;

    size_t declared_datagram_length;
    size_t captured_datagram_end;

    packet_info_t updated_info;
    int error_code;

    /*
     * UDP解析必须建立在成功的IPv4解析结果上。
     */
    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_ipv4 ||
        packet_info->has_udp ||
        packet_info->parse_status !=
            PACKET_PARSE_STATUS_NOT_STARTED ||
        packet_info->ipv4_protocol != IPV4_PROTOCOL_UDP ||
        packet_info->ipv4_total_length <
            packet_info->ipv4_header_length ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U)) {
        return EINVAL;
    }

    /*
     * IPv4分片必须先经过分片重组，才能被当作完整UDP数据报处理。
     */
    if (packet_info->ipv4_more_fragments ||
        packet_info->ipv4_fragment_offset != UINT16_C(0)) {
        return udp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_UNSUPPORTED,
            ENOTSUP,
            packet_info->ipv4_payload_offset
        );
    }

    /*
     * IPv4总长度减去IPv4头部长度，得到协议声明的UDP数据报长度。
     */
    declared_datagram_length =
        (size_t)packet_info->ipv4_total_length -
        (size_t)packet_info->ipv4_header_length;

    /*
     * IPv4负载连8字节UDP头部都容不下，说明协议字段组合不合法。
     *
     * 这里检查的是IPv4声明的长度，不是实际捕获长度，因此属于
     * MALFORMED而不是TRUNCATED。
     */
    if (declared_datagram_length < UDP_HEADER_LENGTH) {
        return udp_record_error(
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
        &datagram_cursor
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 原子性切出固定8字节UDP头部。
     */
    error_code = byte_cursor_read_slice(
        &datagram_cursor,
        UDP_HEADER_LENGTH,
        &header_cursor
    );

    if (error_code == ENODATA) {
        captured_datagram_end =
            packet_info->ipv4_payload_offset +
            packet_info->ipv4_payload_length;

        return udp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_TRUNCATED,
            ENODATA,
            captured_datagram_end
        );
    }

    if (error_code != 0) {
        return error_code;
    }

    /*
     * UDP头部中的四个字段都是网络大端序16位整数。
     */
    error_code = byte_cursor_read_be16(
        &header_cursor,
        &source_port
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &destination_port
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &udp_length
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &checksum
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * UDP长度包含8字节头部，因此不能小于8。
     */
    if (udp_length < UDP_HEADER_LENGTH) {
        return udp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_MALFORMED,
            EBADMSG,
            packet_info->ipv4_payload_offset + 4U
        );
    }

    /*
     * 对于当前支持的未分片IPv4 UDP数据报：
     *
     * IPv4负载长度应当等于UDP长度字段。
     *
     * 二者不同表示IPv4与UDP头部对同一段数据给出了矛盾长度。
     */
    if ((size_t)udp_length != declared_datagram_length) {
        return udp_record_error(
            packet_info,
            PACKET_PARSE_STATUS_MALFORMED,
            EBADMSG,
            packet_info->ipv4_payload_offset + 4U
        );
    }

    /*
     * 全部字段验证成功后，再发布UDP结果。
     */
    updated_info = *packet_info;

    updated_info.udp_source_port = source_port;
    updated_info.udp_destination_port =
        destination_port;
    updated_info.udp_length = udp_length;
    updated_info.udp_checksum = checksum;

    updated_info.udp_payload_offset =
        packet_info->ipv4_payload_offset +
        UDP_HEADER_LENGTH;

    /*
     * datagram_cursor已经跳过8字节UDP头部，
     * 剩余的实际捕获数据就是当前可以访问的UDP负载。
     */
    updated_info.udp_payload_length =
        byte_cursor_remaining(&datagram_cursor);

    /*
     * UDP长度已经和IPv4声明长度相等，所以IPv4负载截断也意味着
     * UDP负载没有完整捕获。
     */
    updated_info.udp_payload_truncated =
        packet_info->ipv4_payload_truncated;

    /*
     * has_udp最后设置，表示所有UDP结果已经完整有效。
     */
    updated_info.has_udp = true;

    *packet_info = updated_info;

    /*
     * 后续仍可能继续解析DNS等应用层协议，因此这里不标记完成。
     */
    return 0;
}

int udp_payload_view(const uint8_t *frame_data,
                     size_t frame_length,
                     const packet_info_t *packet_info,
                     byte_cursor_t *payload)
{
    byte_cursor_t frame_cursor;
    byte_cursor_t new_payload;
    int error_code;

    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_udp ||
        packet_info->ipv4_protocol != IPV4_PROTOCOL_UDP ||
        payload == NULL ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U) ||
        packet_info->udp_payload_offset > frame_length ||
        packet_info->udp_payload_length >
            frame_length -
                packet_info->udp_payload_offset) {
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
     * 从整个Ethernet帧起点移动到UDP负载起点。
     */
    error_code = byte_cursor_skip(
        &frame_cursor,
        packet_info->udp_payload_offset
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 创建只覆盖UDP负载范围的子游标。
     */
    error_code = byte_cursor_read_slice(
        &frame_cursor,
        packet_info->udp_payload_length,
        &new_payload
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 完整建立子游标后再交给调用者。
     */
    *payload = new_payload;

    return 0;
}