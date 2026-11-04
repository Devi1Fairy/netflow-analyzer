#include "bms_telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] line %d: %s\n", __LINE__, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static const char valid[] =
    "BMS state=1, faults=0x00000000, latched=0x00000000, "
    "load=0, clear=0, cell0_mv=3227, temp_est_dC=259, i_ma=0, "
    "iv=1, i_src=3, i_age=101, ina=1, bus_mv=0, "
    "sample=286605, ctrl=1433031, gap_max=20";

static size_t replace_once(char *buffer, size_t capacity,
                           const char *from, const char *to)
{
    const char *found = strstr(valid, from);
    CHECK(found != NULL);
    size_t prefix = (size_t)(found - valid);
    size_t middle = strlen(to);
    const char *suffix = found + strlen(from);
    size_t suffix_length = strlen(suffix);
    size_t length = prefix + middle + suffix_length;
    CHECK(length < capacity);
    memcpy(buffer, valid, prefix);
    memcpy(buffer + prefix, to, middle);
    memcpy(buffer + prefix + middle, suffix, suffix_length + 1U);
    return length;
}

static void expect_error(const char *line, size_t length,
                         bms_telemetry_result_t expected)
{
    bms_telemetry_t output;
    unsigned char before[sizeof(output)];
    memset(&output, 0xa5, sizeof(output));
    memcpy(before, &output, sizeof(output));
    CHECK(bms_telemetry_parse(line, length, &output) == expected);
    CHECK(memcmp(before, &output, sizeof(output)) == 0);
}

static void test_valid(void)
{
    bms_telemetry_t output;
    char raw[sizeof(valid) - 1U];
    memcpy(raw, valid, sizeof(raw));
    CHECK(bms_telemetry_parse(raw, sizeof(raw), &output) == BMS_TELEMETRY_OK);
    CHECK(output.state == 1U && output.faults == 0U && output.latched == 0U);
    CHECK(output.load == 0U && output.clear == 0U && output.cell0_mv == 3227U);
    CHECK(output.temp_est_dC == 259 && output.i_ma == 0);
    CHECK(output.iv == 1U && output.i_src == 3U && output.i_age == 101U);
    CHECK(output.ina == 1U && output.bus_mv == 0U);
    CHECK(output.sample == 286605U && output.ctrl == 1433031U);
    CHECK(output.gap_max == 20U);

    char line[BMS_TELEMETRY_MAX_LINE + 1U];
    const char *endings[] = {"\n", "\r\n"};
    for (size_t i = 0U; i < sizeof(endings) / sizeof(endings[0]); ++i) {
        int count = snprintf(line, sizeof(line), "%s%s", valid, endings[i]);
        CHECK(count > 0 && (size_t)count < sizeof(line));
        CHECK(bms_telemetry_parse(line, (size_t)count, &output) ==
              BMS_TELEMETRY_OK);
    }

    /* state放到最后，并在分隔位置增加空格，仍按字段名正确匹配。 */
    int count = snprintf(line, sizeof(line), "BMS %s, state = 1 ",
                         strstr(valid, "faults="));
    CHECK(count > 0 && (size_t)count < sizeof(line));
    CHECK(bms_telemetry_parse(line, (size_t)count, &output) == BMS_TELEMETRY_OK);
    CHECK(output.state == 1U && output.sample == 286605U);
    puts("[PASS] 16 fields, non-NUL input, LF/CRLF and reordered fields");
}

static void test_numbers(void)
{
    char line[BMS_TELEMETRY_MAX_LINE + 1U];
    bms_telemetry_t output;
    size_t length = replace_once(line, sizeof(line),
                                 "temp_est_dC=259", "temp_est_dC=-2147483648");
    CHECK(bms_telemetry_parse(line, length, &output) == BMS_TELEMETRY_OK);
    CHECK(output.temp_est_dC == INT32_MIN);

    length = replace_once(line, sizeof(line), "i_ma=0", "i_ma=-1234");
    CHECK(bms_telemetry_parse(line, length, &output) == BMS_TELEMETRY_OK);
    CHECK(output.i_ma == -1234);

    length = replace_once(line, sizeof(line), "i_ma=0", "i_ma=2147483647");
    CHECK(bms_telemetry_parse(line, length, &output) == BMS_TELEMETRY_OK);
    CHECK(output.i_ma == INT32_MAX);

    length = replace_once(line, sizeof(line), "sample=286605", "sample=4294967295");
    CHECK(bms_telemetry_parse(line, length, &output) == BMS_TELEMETRY_OK);
    CHECK(output.sample == UINT32_MAX);

    length = replace_once(line, sizeof(line), "faults=0x00000000", "faults=0XFFFFFFFF");
    CHECK(bms_telemetry_parse(line, length, &output) == BMS_TELEMETRY_OK);
    CHECK(output.faults == UINT32_MAX);
    puts("[PASS] signed values, int32/uint32 bounds and hexadecimal masks");
}

static void test_rejections(void)
{
    const struct {
        const char *from;
        const char *to;
        bms_telemetry_result_t result;
    } cases[] = {
        {"sample=286605", "sample=4294967296", BMS_TELEMETRY_OUT_OF_RANGE},
        {"temp_est_dC=259", "temp_est_dC=2147483648", BMS_TELEMETRY_OUT_OF_RANGE},
        {"i_ma=0", "i_ma=-2147483649", BMS_TELEMETRY_OUT_OF_RANGE},
        {"faults=0x00000000", "faults=0x100000000", BMS_TELEMETRY_OUT_OF_RANGE},
        {"latched=0x00000000, ", "", BMS_TELEMETRY_BAD_FORMAT},
        {"gap_max=20", "gap_max=20, sample=9", BMS_TELEMETRY_BAD_FORMAT},
        {"gap_max=20", "gap_max=20, extra=1", BMS_TELEMETRY_BAD_FORMAT},
        {"gap_max=20", "gap_max=20,", BMS_TELEMETRY_BAD_FORMAT},
        {"i_ma=0", "i_ma=", BMS_TELEMETRY_BAD_FORMAT},
        {"i_ma=0", "i_ma=12junk", BMS_TELEMETRY_BAD_FORMAT},
        {"i_ma=0", "i_ma=+1", BMS_TELEMETRY_BAD_FORMAT},
        {"sample=286605", "sample=-1", BMS_TELEMETRY_BAD_FORMAT},
        {"faults=0x00000000", "faults=0x", BMS_TELEMETRY_BAD_FORMAT}
    };
    char line[BMS_TELEMETRY_MAX_LINE + 1U];
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        size_t length = replace_once(line, sizeof(line), cases[i].from, cases[i].to);
        expect_error(line, length, cases[i].result);
    }
    expect_error("READY", 5U, BMS_TELEMETRY_NOT_SAMPLE);
    expect_error("", 0U, BMS_TELEMETRY_NOT_SAMPLE);
    expect_error(NULL, 0U, BMS_TELEMETRY_BAD_FORMAT);
    CHECK(bms_telemetry_parse(valid, strlen(valid), NULL) == BMS_TELEMETRY_BAD_FORMAT);
    puts("[PASS] missing/duplicate/unknown fields, malformed values and unchanged output");
}

static void test_bytes_and_limit(void)
{
    const unsigned char bad[] = {0U, '\t', '\r', '\n', 0x1bU, 0x7fU, 0x80U};
    char line[BMS_TELEMETRY_MAX_LINE + 1U];
    for (size_t i = 0U; i < sizeof(bad); ++i) {
        memcpy(line, valid, sizeof(valid));
        line[10] = (char)bad[i];
        expect_error(line, strlen(valid), BMS_TELEMETRY_BAD_FORMAT);
    }

    /* 用合法的前导零将输入补到恰好1024字节，边界内仍应成功。 */
    size_t prefix = strlen("BMS state=");
    size_t padding = BMS_TELEMETRY_MAX_LINE - strlen(valid);
    memcpy(line, valid, prefix);
    memset(line + prefix, '0', padding);
    memcpy(line + prefix + padding, valid + prefix, strlen(valid) - prefix);
    bms_telemetry_t output;
    CHECK(bms_telemetry_parse(line, BMS_TELEMETRY_MAX_LINE, &output) ==
          BMS_TELEMETRY_OK);
    CHECK(output.state == 1U);
    line[BMS_TELEMETRY_MAX_LINE] = 'X';
    expect_error(line, sizeof(line), BMS_TELEMETRY_TOO_LONG);
    puts("[PASS] control bytes, 1024/1025-byte limit and unchanged rejected output");
}

int main(void)
{
    test_valid();
    test_numbers();
    test_rejections();
    test_bytes_and_limit();
    return EXIT_SUCCESS;
}