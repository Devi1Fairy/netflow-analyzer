#include "analyzer/ipv4.h"

#include "analyzer/ethernet.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * IPv4头部中的IHL以32位字为单位。
 * 一个32位字等于4字节。
 */
#define IPV4_HEADER_WORD_SIZE 4U

/*
 * 版本号位于第一个字节的高4位，IHL位于低4位。
 */
#define IPV4_VERSION_SHIFT 4U
#define IPV4_IHL_MASK UINT8_C(0x0F)

/*
 * IPv4标志和分片偏移共享同一个16位字段：
 *
 * 最高3位：标志；
 * 最低13位：分片偏移。
 */
#define IPV4_FLAG_DONT_FRAGMENT UINT16_C(0x4000)
#define IPV4_FLAG_MORE_FRAGMENTS UINT16_C(0x2000)
#define IPV4_FRAGMENT_OFFSET_MASK UINT16_C(0x1FFF)

/**
 * @brief 在packet_info中记录IPv4解析错误。
 *
 * @return 状态记录成功时返回original_error；
 *         状态记录失败时返回packet_info_set_error的错误码。
 */
static int ipv4_record_error(packet_info_t *packet_info,
                             packet_parse_status_t status,
                             int original_error,
                             size_t error_offset)
{
    int state_error;

    state_error = packet_info_set_error(
        packet_info,
        status,
        PACKET_PARSE_LAYER_IPV4,
        original_error,
        error_offset
    );

    if (state_error != 0) {
        return state_error;
    }

    return original_error;
}

int ipv4_parse(const uint8_t *frame_data,
               size_t frame_length,
               packet_info_t *packet_info)
{
    byte_cursor_t network_cursor;
    byte_cursor_t base_header_cursor;

    uint8_t version_and_ihl;
    uint8_t version;
    uint8_t ihl_words;

    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_and_fragment_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t source_address;
    uint32_t destination_address;

    size_t header_length;
    size_t options_length;
    size_t available_ipv4_length;
    size_t network_end_offset;

    packet_info_t updated_info;
    int error_code;

    /*
     * IPv4解析必须建立在完整Ethernet结果之上。
     *
     * 非IPv4 EtherType不是“损坏的IPv4”，而是调用者不应该调用
     * IPv4解析器，因此返回EINVAL且不修改packet_info。
     */
    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_ethernet ||
        packet_info->has_ipv4 ||
        packet_info->ether_type != ETHERNET_TYPE_IPV4 ||
        packet_info->parse_status !=
            PACKET_PARSE_STATUS_NOT_STARTED ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U)) {
        return EINVAL;
    }

    error_code = ethernet_payload_view(
        frame_data,
        frame_length,
        packet_info,
        &network_cursor
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 先切出固定的20字节基础头部。
     *
     * IPv4选项可能让实际头部超过20字节，但版本、IHL和总长度等
     * 必要字段都位于前20字节中。
     */
    error_code = byte_cursor_read_slice(
        &network_cursor,
        IPV4_MIN_HEADER_LENGTH,
        &base_header_cursor
    );

    if (error_code == ENODATA) {
        network_end_offset =
            packet_info->network_payload_offset +
            packet_info->network_payload_length;

        return ipv4_record_error(
            packet_info,
            PACKET_PARSE_STATUS_TRUNCATED,
            ENODATA,
            network_end_offset
        );
    }

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_u8(
        &base_header_cursor,
        &version_and_ihl
    );

    if (error_code != 0) {
        return error_code;
    }

    version = (uint8_t)(
        version_and_ihl >> IPV4_VERSION_SHIFT
    );

    ihl_words = (uint8_t)(
        version_and_ihl & IPV4_IHL_MASK
    );

    /*
     * EtherType声明这是IPv4，但头部版本号却不是4，
     * 表示数据内容与声明不一致。
     */
    if (version != IPV4_VERSION) {
        return ipv4_record_error(
            packet_info,
            PACKET_PARSE_STATUS_MALFORMED,
            EBADMSG,
            packet_info->network_payload_offset
        );
    }

    /*
     * IHL不能小于5，因为IPv4固定头部至少需要20字节。
     */
    if (ihl_words < UINT8_C(5)) {
        return ipv4_record_error(
            packet_info,
            PACKET_PARSE_STATUS_MALFORMED,
            EBADMSG,
            packet_info->network_payload_offset
        );
    }

    header_length =
        (size_t)ihl_words * IPV4_HEADER_WORD_SIZE;

    /*
     * 跳过DSCP和ECN字段。
     *
     * 当前阶段不需要保存该字段，但仍然必须让游标移动到总长度字段。
     */
    error_code = byte_cursor_skip(
        &base_header_cursor,
        1U
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &total_length
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &identification
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &flags_and_fragment_offset
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_u8(
        &base_header_cursor,
        &ttl
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_u8(
        &base_header_cursor,
        &protocol
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &base_header_cursor,
        &header_checksum
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be32(
        &base_header_cursor,
        &source_address
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be32(
        &base_header_cursor,
        &destination_address
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 总长度包含头部，因此不能小于IHL声明的头部长度。
     *
     * 错误偏移指向IPv4总长度字段，它位于IPv4起点后2字节。
     */
    if ((size_t)total_length < header_length) {
        return ipv4_record_error(
            packet_info,
            PACKET_PARSE_STATUS_MALFORMED,
            EBADMSG,
            packet_info->network_payload_offset + 2U
        );
    }

    /*
     * 如果IHL大于5，需要确认捕获数据中确实存在完整选项区域。
     */
    options_length =
        header_length - IPV4_MIN_HEADER_LENGTH;

    error_code = byte_cursor_skip(
        &network_cursor,
        options_length
    );

    if (error_code == ENODATA) {
        network_end_offset =
            packet_info->network_payload_offset +
            packet_info->network_payload_length;

        return ipv4_record_error(
            packet_info,
            PACKET_PARSE_STATUS_TRUNCATED,
            ENODATA,
            network_end_offset
        );
    }

    if (error_code != 0) {
        return error_code;
    }

    /*
     * Ethernet负载可能包含填充字节，所以不能直接把整个Ethernet
     * 负载都当成IPv4数据报。
     *
     * 另一方面，抓包也可能只保存IPv4数据报的前一部分。
     * 因此取“捕获长度”和“IPv4声明总长度”中的较小值。
     */
    available_ipv4_length =
        packet_info->network_payload_length;

    if ((size_t)total_length < available_ipv4_length) {
        available_ipv4_length =
            (size_t)total_length;
    }

    /*
     * 所有必要字段验证成功后，才在局部副本中发布解析结果。
     */
    updated_info = *packet_info;

    updated_info.ipv4_header_length =
        (uint8_t)header_length;
    updated_info.ipv4_total_length =
        total_length;
    updated_info.ipv4_identification =
        identification;
    updated_info.ipv4_dont_fragment =
        (flags_and_fragment_offset &
         IPV4_FLAG_DONT_FRAGMENT) != 0U;
    updated_info.ipv4_more_fragments =
        (flags_and_fragment_offset &
         IPV4_FLAG_MORE_FRAGMENTS) != 0U;
    updated_info.ipv4_fragment_offset =
        (uint16_t)(
            flags_and_fragment_offset &
            IPV4_FRAGMENT_OFFSET_MASK
        );
    updated_info.ipv4_ttl = ttl;
    updated_info.ipv4_protocol = protocol;
    updated_info.ipv4_header_checksum =
        header_checksum;
    updated_info.source_ipv4 =
        source_address;
    updated_info.destination_ipv4 =
        destination_address;

    updated_info.ipv4_payload_offset =
        packet_info->network_payload_offset +
        header_length;

    updated_info.ipv4_payload_length =
        available_ipv4_length -
        header_length;

    updated_info.ipv4_payload_truncated =
        packet_info->network_payload_length <
        (size_t)total_length;

    /*
     * has_ipv4最后设置，表示以上IPv4字段已经全部有效。
     */
    updated_info.has_ipv4 = true;

    *packet_info = updated_info;

    /*
     * 后续还需要根据protocol解析TCP、UDP或ICMP，
     * 所以这里暂时不标记整个数据包解析完成。
     */
    return 0;
}

int ipv4_payload_view(const uint8_t *frame_data,
                      size_t frame_length,
                      const packet_info_t *packet_info,
                      byte_cursor_t *payload)
{
    byte_cursor_t frame_cursor;
    byte_cursor_t new_payload;
    int error_code;

    if (packet_info == NULL ||
        !packet_info->initialized ||
        !packet_info->has_ipv4 ||
        payload == NULL ||
        frame_length !=
            (size_t)packet_info->captured_length ||
        (frame_data == NULL && frame_length > 0U) ||
        packet_info->ipv4_payload_offset >
            frame_length ||
        packet_info->ipv4_payload_length >
            frame_length -
                packet_info->ipv4_payload_offset) {
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
        packet_info->ipv4_payload_offset
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_slice(
        &frame_cursor,
        packet_info->ipv4_payload_length,
        &new_payload
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 完整建立子游标后再修改调用者的输出对象。
     */
    *payload = new_payload;

    return 0;
}

int ipv4_format_address(uint32_t address,
                        char *buffer,
                        size_t buffer_size)
{
    char formatted[IPV4_ADDRESS_STRING_SIZE] = {0};
    int written_length;

    if (buffer == NULL) {
        return EINVAL;
    }

    if (buffer_size < sizeof(formatted)) {
        return ENOSPC;
    }

    /*
     * address已经由byte_cursor_read_be32转换为主机可使用的数值。
     *
     * 通过移位依次取得从高位到低位的四个IPv4地址字节。
     */
    written_length = snprintf(
        formatted,
        sizeof(formatted),
        "%u.%u.%u.%u",
        (unsigned int)(
            (address >> 24U) & UINT32_C(0xFF)
        ),
        (unsigned int)(
            (address >> 16U) & UINT32_C(0xFF)
        ),
        (unsigned int)(
            (address >> 8U) & UINT32_C(0xFF)
        ),
        (unsigned int)(
            address & UINT32_C(0xFF)
        )
    );

    if (written_length < 0 ||
        (size_t)written_length >= sizeof(formatted)) {
        return EIO;
    }

    /*
     * 只有格式化完整成功后才复制到调用者缓冲区。
     * written_length不包含字符串结尾的'\0'，所以复制时需要加1。
     */
    memcpy(
        buffer,
        formatted,
        (size_t)written_length + 1U
    );

    return 0;
}