#ifndef BMS_CONTROL_PROTOCOL_H
#define BMS_CONTROL_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BMS_CONTROL_MAX_BODY 64U

/* 枚举值固定为允许的UART单字节；本模块不调用write。 */
typedef enum {
    BMS_CONTROL_STOP = 'T',
    BMS_CONTROL_START = 'S',
    BMS_CONTROL_CLEAR = 'C'
} bms_control_action_t;

typedef struct {
    uint32_t id;
    bms_control_action_t action;
} bms_control_request_t;

typedef enum {
    BMS_CONTROL_PARSE_OK = 0,
    BMS_CONTROL_BAD_REQUEST,
    BMS_CONTROL_BAD_COMMAND
} bms_control_parse_result_t;

/*
 * 借用body[0..length)，输入为去掉LF后的完整正文，不要求NUL结尾。
 * 只接受CMD、单空格、十进制编号、单空格、白名单动作。
 * 编号允许前导0，但数值必须在1..UINT32_MAX。
 *
 * reply_id返回可靠取得的编号，否则为0。
 * request仅成功时更新；两个输出对象必须有效且不得重叠。
 * 不保存输入指针，不分配内存、不维护会话编号、不执行任何I/O。
 */
bms_control_parse_result_t bms_control_parse_request(
    const unsigned char *body,
    size_t length,
    bms_control_request_t *request,
    uint32_t *reply_id
);

#endif