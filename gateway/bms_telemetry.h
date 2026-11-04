#ifndef BMS_TELEMETRY_H
#define BMS_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

/* 输入总长度上限，包含调用者提供的可选行尾。 */
#define BMS_TELEMETRY_MAX_LINE 1024U

typedef struct {
    uint32_t state;
    uint32_t faults;
    uint32_t latched;
    uint32_t load;
    uint32_t clear;
    uint32_t cell0_mv;
    int32_t temp_est_dC;
    int32_t i_ma;
    uint32_t iv;
    uint32_t i_src;
    uint32_t i_age;
    uint32_t ina;
    uint32_t bus_mv;
    uint32_t sample;
    uint32_t ctrl;
    uint32_t gap_max;
} bms_telemetry_t;

typedef enum {
    BMS_TELEMETRY_OK = 0,
    BMS_TELEMETRY_NOT_SAMPLE,
    BMS_TELEMETRY_BAD_FORMAT,
    BMS_TELEMETRY_OUT_OF_RANGE,
    BMS_TELEMETRY_TOO_LONG
} bms_telemetry_result_t;

/*
 * 借用line[0..length)，不要求NUL结束，不保存输入指针。
 * 接受无行尾、LF或CRLF；字段顺序不限，允许分隔位置的空格。
 * 当前16个字段必须各出现一次；未知、缺失、重复字段均拒绝。
 * faults/latched要求0x或0X十六进制，其余字段为十进制。
 * 仅temp_est_dC和i_ma允许负数，不接受前导加号。
 * 返回OK才写output；任何失败均保持调用者output原样。
 * 不执行I/O，不分配内存，不判定状态枚举或物理测量有效性。
 */
bms_telemetry_result_t bms_telemetry_parse(
    const char *line, size_t length, bms_telemetry_t *output);

#endif