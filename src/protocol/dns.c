#include "analyzer/dns.h"

#include <errno.h>

int dns_parse_header(
    byte_cursor_t *payload,
    dns_header_t *header
)
{
    byte_cursor_t next_payload;
    byte_cursor_t header_cursor;

    dns_header_t result = {0};
    int error_code;

    if (payload == NULL || header == NULL) {
        return EINVAL;
    }

    /*
     * 复制游标状态，不复制底层报文。
     *
     * 后续读取只修改局部游标，避免失败时改变调用者的位置。
     */
    next_payload = *payload;

    /*
     * 安全切出完整的12字节头部。
     *
     * byte_reader同时检查游标是否合法、剩余长度是否足够。
     * header_cursor只能访问这12字节，不能越过头部边界。
     */
    error_code = byte_cursor_read_slice(
        &next_payload,
        DNS_HEADER_LENGTH,
        &header_cursor
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &result.transaction_id
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &result.flags
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &result.question_count
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &result.answer_count
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &result.authority_count
    );

    if (error_code != 0) {
        return error_code;
    }

    error_code = byte_cursor_read_be16(
        &header_cursor,
        &result.additional_count
    );

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 全部读取成功后才发布结果和新位置。
     * 后续解析器可从更新后的payload继续读取Question区域。
     */
    *header = result;
    *payload = next_payload;

    return 0;
}