#include "analyzer/ethernet.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief 在packet_info中记录Ethernet头部截断错误。
 *
 * error_offset使用实际捕获长度，表示解析到捕获缓冲区末尾时，
 * 仍然没有取得完整的14字节Ethernet头部。
 */
static int ethernet_record_truncation(packet_info_t *packet_info,
                                      size_t error_offset)
{
    int error_code;

    error_code = packet_info_set_error(
        packet_info,
        PACKET_PARSE_STATUS_TRUNCATED,
        PACKET_PARSE_LAYER_ETHERNET,
        ENODATA,
        error_offset
    );

    if (error_code != 0) {
        return error_code;
    }

    return ENODATA;
}

int ethernet_parse(const uint8_t *frame_data,
                   size_t frame_length,
                   packet_info_t *packet_info)
{
    byte_cursor_t frame_cursor;
    byte_cursor_t header_cursor;

    uint8_t destination_mac[ETHERNET_MAC_ADDRESS_LENGTH];
    uint8_t source_mac[ETHERNET_MAC_ADDRESS_LENGTH];
    uint16_t ether_type;

    packet_info_t updated_info;
    int error_code;

    /*
     * Ethernet解析必须从一个干净、已初始化的数据包结果开始。
     *
     * frame_length必须等于captured_length，避免调用者错误地使用
     * wire_length或其他不相关长度。
     */
    if (packet_info == NULL ||
        !packet_info->initialized ||
        packet_info->parse_status !=
            PACKET_PARSE_STATUS_NOT_STARTED ||
        packet_info->has_ethernet ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U)) {
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
     * 先原子性地切出完整14字节头部。
     *
     * 如果长度不足，父游标不会移动，也不会读取任何部分字段。
     */
    error_code = byte_cursor_read_slice(
        &frame_cursor,
        ETHERNET_HEADER_LENGTH,
        &header_cursor
    );

    if (error_code == ENODATA) {
        return ethernet_record_truncation(
            packet_info,
            frame_length
        );
    }

    if (error_code != 0) {
        return error_code;
    }

    /*
     * MAC地址是原始字节序列，直接读取到6字节数组。
     */
    error_code = byte_cursor_read_bytes(
        &header_cursor,
        destination_mac,
        sizeof(destination_mac)
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_bytes(
        &header_cursor,
        source_mac,
        sizeof(source_mac)
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * EtherType是网络大端序16位整数。
     */
    error_code = byte_cursor_read_be16(
        &header_cursor,
        &ether_type
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 所有字段读取成功后，先在局部副本中构造完整结果。
     *
     * 这样任何前置步骤失败都不会把部分MAC地址写入packet_info。
     */
    updated_info = *packet_info;

    memcpy(
        updated_info.destination_mac,
        destination_mac,
        sizeof(destination_mac)
    );

    memcpy(
        updated_info.source_mac,
        source_mac,
        sizeof(source_mac)
    );

    updated_info.ether_type = ether_type;
    updated_info.network_payload_offset =
        ETHERNET_HEADER_LENGTH;
    updated_info.network_payload_length =
        byte_cursor_remaining(&frame_cursor);

    /*
     * has_ethernet最后设置，表示以上所有Ethernet结果已经完整有效。
     */
    updated_info.has_ethernet = true;

    *packet_info = updated_info;

    /*
     * 这里只完成Ethernet层，后续还可能解析IPv4、ARP等网络层，
     * 因此不在这里调用packet_info_mark_complete。
     */
    return 0;
}

int ethernet_payload_view(const uint8_t *frame_data,
                          size_t frame_length,
                          const packet_info_t *packet_info,
                          byte_cursor_t *payload)
{
    byte_cursor_t frame_cursor;
    byte_cursor_t new_payload;
    int error_code;

    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_ethernet ||
        payload == NULL ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U) ||
        packet_info->network_payload_offset >
            frame_length ||
        packet_info->network_payload_length >
            frame_length -
                packet_info->network_payload_offset) {
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
        packet_info->network_payload_offset
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_slice(
        &frame_cursor,
        packet_info->network_payload_length,
        &new_payload
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 完整建立子游标后再发布给调用者。
     */
    *payload = new_payload;

    return 0;
}

int ethernet_format_mac(const uint8_t address[ETHERNET_MAC_ADDRESS_LENGTH],
                        char *buffer,
                        size_t buffer_size)
{
    char formatted[ETHERNET_MAC_STRING_SIZE];
    int written_length;

    if (address == NULL || buffer == NULL) {
        return EINVAL;
    }

    if (buffer_size < sizeof(formatted)) {
        return ENOSPC;
    }

    /*
     * uint8_t传入可变参数函数时会发生整数提升。
     *
     * 显式转换成unsigned int，使实参类型与%02x要求的类型匹配。
     */
    written_length = snprintf(
        formatted,
        sizeof(formatted),
        "%02x:%02x:%02x:%02x:%02x:%02x",
        (unsigned int)address[0],
        (unsigned int)address[1],
        (unsigned int)address[2],
        (unsigned int)address[3],
        (unsigned int)address[4],
        (unsigned int)address[5]
    );

    if (written_length < 0 || (size_t)written_length != ETHERNET_MAC_STRING_SIZE - 1U) {
        return EIO;
    }

    /*
     * snprintf先写入局部数组，成功后再复制到调用者缓冲区。
     *
     * 这样格式化失败时不会留下部分字符串。
     */
    memcpy(buffer, formatted, sizeof(formatted));

    return 0;
}
