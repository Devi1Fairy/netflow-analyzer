#include "../../gateway/bms_control_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] line %d: %s\n", \
                    __LINE__, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void expect_parse(const unsigned char *body, size_t length,
                         bms_control_parse_result_t expected,
                         uint32_t expected_id, unsigned char uart_byte)
{
    bms_control_request_t request = {
        .id = 123U, .action = BMS_CONTROL_START
    };
    uint32_t reply_id = 999U;

    CHECK(bms_control_parse_request(
        body, length, &request, &reply_id
    ) == expected);
    CHECK(reply_id == expected_id);

    if (expected == BMS_CONTROL_PARSE_OK) {
        CHECK(request.id == expected_id);
        CHECK((unsigned char)request.action == uart_byte);
    } else {
        /* 拒绝请求不能留下可被误执行的半成品输出。 */
        CHECK(request.id == 123U);
        CHECK(request.action == BMS_CONTROL_START);
    }
}

static void test_requests(void)
{
    const struct {
        const char *text;
        bms_control_parse_result_t result;
        uint32_t id;
        unsigned char byte;
    } cases[] = {
        {"CMD 1 STOP", BMS_CONTROL_PARSE_OK, 1U, 'T'},
        {"CMD 2 START", BMS_CONTROL_PARSE_OK, 2U, 'S'},
        {"CMD 3 CLEAR", BMS_CONTROL_PARSE_OK, 3U, 'C'},
        {"CMD 4294967295 STOP", BMS_CONTROL_PARSE_OK, UINT32_MAX, 'T'},
        {"CMD 0001 STOP", BMS_CONTROL_PARSE_OK, 1U, 'T'},
        {"", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"CMD 0 STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"CMD 4294967296 STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"CMD 999999999999999999999 STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"CMD +1 STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"CMD -1 STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"CMD 1x STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"cmd 1 STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {" CMD 1 STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"CMD\t1 STOP", BMS_CONTROL_BAD_REQUEST, 0U, 0},
        {"CMD 1", BMS_CONTROL_BAD_REQUEST, 1U, 0},
        {"CMD 1 ", BMS_CONTROL_BAD_REQUEST, 1U, 0},
        {"CMD 1  STOP", BMS_CONTROL_BAD_REQUEST, 1U, 0},
        {"CMD 1 STOP extra", BMS_CONTROL_BAD_REQUEST, 1U, 0},
        {"CMD 1 STOP ", BMS_CONTROL_BAD_REQUEST, 1U, 0},
        {"CMD 1 stop", BMS_CONTROL_BAD_COMMAND, 1U, 0},
        {"CMD 1 P", BMS_CONTROL_BAD_COMMAND, 1U, 0},
        {"CMD 1 R", BMS_CONTROL_BAD_COMMAND, 1U, 0},
        {"CMD 1 A", BMS_CONTROL_BAD_COMMAND, 1U, 0},
        {"CMD 1 D", BMS_CONTROL_BAD_COMMAND, 1U, 0}
    };

    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]);
         ++index) {
        expect_parse((const unsigned char *)cases[index].text,
                     strlen(cases[index].text), cases[index].result,
                     cases[index].id, cases[index].byte);
    }
    puts("[PASS] command grammar, uint32 bounds and UART whitelist");
}

static void test_bytes_and_capacity(void)
{
    const unsigned char invalid[] = {0, '\r', '\n', '\t', 0x1b, 0x7f, 0x80};
    for (size_t index = 0U; index < sizeof(invalid); ++index) {
        unsigned char body[] = "CMD 7 STOP";
        body[8] = invalid[index];
        expect_parse(body, sizeof(body) - 1U,
                     BMS_CONTROL_BAD_REQUEST, 7U, 0);
    }

    /* 没有NUL结束符也必须安全解析，不能依赖strlen(body)。 */
    const unsigned char raw[] = {'C','M','D',' ','9',' ','S','T','O','P'};
    expect_parse(raw, sizeof(raw), BMS_CONTROL_PARSE_OK, 9U, 'T');

    unsigned char body[BMS_CONTROL_MAX_BODY + 1U];
    memset(body, '0', sizeof(body));
    memcpy(body, "CMD ", 4U);
    size_t separator = BMS_CONTROL_MAX_BODY - 5U;
    body[separator - 1U] = '1';
    memcpy(body + separator, " STOP", 5U);
    expect_parse(body, BMS_CONTROL_MAX_BODY, BMS_CONTROL_PARSE_OK, 1U, 'T');

    body[BMS_CONTROL_MAX_BODY] = 'X';
    expect_parse(body, sizeof(body), BMS_CONTROL_BAD_REQUEST, 0U, 0);
    puts("[PASS] binary rejection, non-NUL input and 64/65-byte boundary");
}

static void test_invalid_arguments(void)
{
    bms_control_request_t request = {.id = 123U, .action = BMS_CONTROL_START};
    uint32_t reply_id = 999U;
    const unsigned char body[] = "CMD 1 STOP";

    expect_parse(NULL, 1U, BMS_CONTROL_BAD_REQUEST, 0U, 0);
    CHECK(bms_control_parse_request(
        body, sizeof(body) - 1U, NULL, &reply_id
    ) == BMS_CONTROL_BAD_REQUEST);
    CHECK(reply_id == 0U);

    CHECK(bms_control_parse_request(
        body, sizeof(body) - 1U, &request, NULL
    ) == BMS_CONTROL_BAD_REQUEST);
    CHECK(request.id == 123U && request.action == BMS_CONTROL_START);
    puts("[PASS] invalid arguments and unchanged rejected output");
}

int main(void)
{
    test_requests();
    test_bytes_and_capacity();
    test_invalid_arguments();
    return EXIT_SUCCESS;
}