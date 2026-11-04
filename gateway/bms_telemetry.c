#include "bms_telemetry.h"

#include <stdbool.h>
#include <string.h>

typedef enum { VALUE_U32, VALUE_I32, VALUE_HEX32 } value_kind_t;

typedef struct {
    const char *name;
    value_kind_t kind;
    void *destination;
    bool seen;
} field_spec_t;

static void trim_spaces(const char **begin, const char **end)
{
    while (*begin < *end && **begin == ' ') {
        *begin += 1;
    }
    while (*begin < *end && (*end)[-1] == ' ') {
        *end -= 1;
    }
}

static int digit_value(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bms_telemetry_result_t parse_integer(
    const char *begin, const char *end,
    value_kind_t kind, void *destination)
{
    trim_spaces(&begin, &end);
    bool negative = false;
    if (kind == VALUE_I32 && begin < end && *begin == '-') {
        negative = true;
        ++begin;
    }

    uint32_t base = kind == VALUE_HEX32 ? 16U : 10U;
    if (kind == VALUE_HEX32) {
        if (end - begin < 2 || begin[0] != '0' ||
            (begin[1] != 'x' && begin[1] != 'X')) {
            return BMS_TELEMETRY_BAD_FORMAT;
        }
        begin += 2;
    }
    if (begin == end) return BMS_TELEMETRY_BAD_FORMAT;

    uint32_t limit = UINT32_MAX;
    if (kind == VALUE_I32) {
        limit = (uint32_t)INT32_MAX + (negative ? 1U : 0U);
    }

    uint32_t value = 0U;
    for (; begin < end; ++begin) {
        int number = digit_value((unsigned char)*begin);
        if (number < 0 || (uint32_t)number >= base) {
            return BMS_TELEMETRY_BAD_FORMAT;
        }
        uint32_t digit = (uint32_t)number;

        /* 先检查再乘加，不能等无符号整数回绕后再判断。 */
        if (value > (limit - digit) / base) {
            return BMS_TELEMETRY_OUT_OF_RANGE;
        }
        value = value * base + digit;
    }

    /* 表中的地址只指向本次调用的局部candidate，类型由kind指定。 */
    if (kind == VALUE_I32) {
        int64_t signed_value = negative ? -(int64_t)value : (int64_t)value;
        *(int32_t *)destination = (int32_t)signed_value;
    } else {
        *(uint32_t *)destination = value;
    }
    return BMS_TELEMETRY_OK;
}

bms_telemetry_result_t bms_telemetry_parse(
    const char *line, size_t length, bms_telemetry_t *output)
{
    if (line == NULL || output == NULL) return BMS_TELEMETRY_BAD_FORMAT;
    if (length > BMS_TELEMETRY_MAX_LINE) return BMS_TELEMETRY_TOO_LONG;

    if (length > 0U && line[length - 1U] == '\n') {
        --length;
        if (length > 0U && line[length - 1U] == '\r') --length;
    }
    if (length < 4U || memcmp(line, "BMS ", 4U) != 0) {
        return BMS_TELEMETRY_NOT_SAMPLE;
    }

    for (size_t i = 0U; i < length; ++i) {
        unsigned char ch = (unsigned char)line[i];
        if (ch < 0x20U || ch > 0x7eU) return BMS_TELEMETRY_BAD_FORMAT;
    }

    bms_telemetry_t candidate = {0};
    field_spec_t fields[] = {
        {"state", VALUE_U32, &candidate.state, false},
        {"faults", VALUE_HEX32, &candidate.faults, false},
        {"latched", VALUE_HEX32, &candidate.latched, false},
        {"load", VALUE_U32, &candidate.load, false},
        {"clear", VALUE_U32, &candidate.clear, false},
        {"cell0_mv", VALUE_U32, &candidate.cell0_mv, false},
        {"temp_est_dC", VALUE_I32, &candidate.temp_est_dC, false},
        {"i_ma", VALUE_I32, &candidate.i_ma, false},
        {"iv", VALUE_U32, &candidate.iv, false},
        {"i_src", VALUE_U32, &candidate.i_src, false},
        {"i_age", VALUE_U32, &candidate.i_age, false},
        {"ina", VALUE_U32, &candidate.ina, false},
        {"bus_mv", VALUE_U32, &candidate.bus_mv, false},
        {"sample", VALUE_U32, &candidate.sample, false},
        {"ctrl", VALUE_U32, &candidate.ctrl, false},
        {"gap_max", VALUE_U32, &candidate.gap_max, false}
    };
    const size_t field_count = sizeof(fields) / sizeof(fields[0]);
    const char *position = line + 4U;
    const char *end = line + length;

    for (;;) {
        const char *comma = memchr(position, ',', (size_t)(end - position));
        const char *token_end = comma == NULL ? end : comma;
        const char *key = position;
        trim_spaces(&key, &token_end);
        const char *equals = memchr(key, '=', (size_t)(token_end - key));
        if (equals == NULL) return BMS_TELEMETRY_BAD_FORMAT;

        const char *key_end = equals;
        trim_spaces(&key, &key_end);
        size_t index;
        for (index = 0U; index < field_count; ++index) {
            size_t name_length = strlen(fields[index].name);
            if ((size_t)(key_end - key) == name_length &&
                memcmp(key, fields[index].name, name_length) == 0) break;
        }
        if (index == field_count || fields[index].seen) {
            return BMS_TELEMETRY_BAD_FORMAT;
        }

        bms_telemetry_result_t result = parse_integer(
            equals + 1U, token_end,
            fields[index].kind, fields[index].destination);
        if (result != BMS_TELEMETRY_OK) return result;
        fields[index].seen = true;

        if (comma == NULL) break;
        position = comma + 1U;
    }

    for (size_t i = 0U; i < field_count; ++i) {
        if (!fields[i].seen) return BMS_TELEMETRY_BAD_FORMAT;
    }

    /* 所有字段完整、合法后才发布，失败不会修改调用者的结构体。 */
    *output = candidate;
    return BMS_TELEMETRY_OK;
}