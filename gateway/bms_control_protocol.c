#include "bms_control_protocol.h"

#include <string.h>

bms_control_parse_result_t bms_control_parse_request(
    const unsigned char *body,
    size_t length,
    bms_control_request_t *request,
    uint32_t *reply_id)
{
    if (reply_id != NULL) {
        *reply_id = 0U;
    }

    if (body == NULL || request == NULL || reply_id == NULL ||
        length < 4U || length > BMS_CONTROL_MAX_BODY ||
        memcmp(body, "CMD ", 4U) != 0) {
        return BMS_CONTROL_BAD_REQUEST;
    }

    uint32_t id = 0U;
    size_t position = 4U;

    while (position < length &&
           body[position] >= '0' && body[position] <= '9') {
        uint32_t digit = (uint32_t)(body[position] - '0');

        /* 在乘10和加digit之前判断，避免无符号回绕。 */
        if (id > (UINT32_MAX - digit) / 10U) {
            return BMS_CONTROL_BAD_REQUEST;
        }

        id = id * 10U + digit;
        ++position;
    }

    /* 非法数字后缀不能被截成一个看似合法的编号。 */
    if (position == 4U || id == 0U ||
        (position < length && body[position] != ' ')) {
        return BMS_CONTROL_BAD_REQUEST;
    }

    *reply_id = id;
    if (position == length) {
        return BMS_CONTROL_BAD_REQUEST;  /* 编号可读，但缺少动作。 */
    }

    size_t action_start = position + 1U;
    size_t action_length = length - action_start;
    if (action_length == 0U) {
        return BMS_CONTROL_BAD_REQUEST;
    }

    /* 控制协议限定可打印ASCII，不把CRLF宽松解释成合法请求。 */
    for (size_t index = 0U; index < length; ++index) {
        if (body[index] < 0x20U || body[index] > 0x7eU) {
            return BMS_CONTROL_BAD_REQUEST;
        }
    }

    /* 动作必须是一个词，不接受多余字段或首尾空格。 */
    if (memchr(body + action_start, ' ', action_length) != NULL) {
        return BMS_CONTROL_BAD_REQUEST;
    }

    bms_control_action_t action;
    if (action_length == 4U &&
        memcmp(body + action_start, "STOP", 4U) == 0) {
        action = BMS_CONTROL_STOP;
    } else if (action_length == 5U &&
               memcmp(body + action_start, "START", 5U) == 0) {
        action = BMS_CONTROL_START;
    } else if (action_length == 5U &&
               memcmp(body + action_start, "CLEAR", 5U) == 0) {
        action = BMS_CONTROL_CLEAR;
    } else {
        return BMS_CONTROL_BAD_COMMAND;
    }

    *request = (bms_control_request_t){
        .id = id,
        .action = action
    };
    return BMS_CONTROL_PARSE_OK;
}