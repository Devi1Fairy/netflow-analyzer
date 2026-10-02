#include "analyzer/dns.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_CHECK(condition)                              \
    do {                                                   \
        if (!(condition)) {                                \
            fprintf(stderr,                                \
                    "[FAIL] %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #condition);       \
            return EXIT_FAILURE;                           \
        }                                                  \
    } while (false)

/**
 * @brief 验证查询头、大端转换、非零起始位置和剩余负载。
 */
static int test_dns_query_header(void)
{
    static const uint8_t data[] = {
        /* 前置字节，不属于DNS消息。 */
        0xee,

        /* ID=0x1234，flags=0x0100。 */
        0x12, 0x34,
        0x01, 0x00,

        /* 一个问题，其余区域没有记录。 */
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,

        /*
         * Question：名字a，QTYPE=A，QCLASS=IN。
         * 本轮解析器必须保留这些字节，不读取它们。
         */
        0x01, 0x61, 0x00,
        0x00, 0x01,
        0x00, 0x01
    };

    byte_cursor_t payload;
    dns_header_t header;

    TEST_CHECK(
        byte_cursor_init(&payload, data, sizeof(data)) == 0
    );

    TEST_CHECK(byte_cursor_skip(&payload, 1U) == 0);
    TEST_CHECK(dns_parse_header(&payload, &header) == 0);

    TEST_CHECK(header.transaction_id == UINT16_C(0x1234));
    TEST_CHECK(header.flags == UINT16_C(0x0100));
    TEST_CHECK(header.question_count == UINT16_C(1));
    TEST_CHECK(header.answer_count == UINT16_C(0));
    TEST_CHECK(header.authority_count == UINT16_C(0));
    TEST_CHECK(header.additional_count == UINT16_C(0));

    TEST_CHECK((header.flags & DNS_FLAG_QR) == 0U);
    TEST_CHECK((header.flags & DNS_FLAG_TC) == 0U);

    TEST_CHECK(payload.offset == 1U + DNS_HEADER_LENGTH);
    TEST_CHECK(byte_cursor_remaining(&payload) == 7U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证响应标志和四个独立计数字段。
 *
 * 这里只构造固定头，不构造完整DNS消息。
 * 因此成功仅代表头部读取成功。
 */
static int test_dns_response_header(void)
{
    static const uint8_t data[] = {
        0xab, 0xcd,
        0x83, 0x83,
        0x00, 0x01,
        0x00, 0x02,
        0x00, 0x03,
        0x00, 0x04
    };

    byte_cursor_t payload;
    dns_header_t header;

    TEST_CHECK(
        byte_cursor_init(&payload, data, sizeof(data)) == 0
    );

    TEST_CHECK(dns_parse_header(&payload, &header) == 0);

    TEST_CHECK(header.transaction_id == UINT16_C(0xabcd));
    TEST_CHECK(header.flags == UINT16_C(0x8383));
    TEST_CHECK(header.question_count == UINT16_C(1));
    TEST_CHECK(header.answer_count == UINT16_C(2));
    TEST_CHECK(header.authority_count == UINT16_C(3));
    TEST_CHECK(header.additional_count == UINT16_C(4));

    TEST_CHECK((header.flags & DNS_FLAG_QR) != 0U);
    TEST_CHECK((header.flags & DNS_FLAG_TC) != 0U);

    TEST_CHECK(payload.offset == DNS_HEADER_LENGTH);
    TEST_CHECK(byte_cursor_remaining(&payload) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证失败返回码，以及失败时两个输出都保持不变。
 */
static int check_dns_failure(
    byte_cursor_t *payload,
    int expected_error
)
{
    byte_cursor_t before = {0};

    dns_header_t header = {
        .transaction_id = UINT16_C(11),
        .flags = UINT16_C(22),
        .question_count = UINT16_C(33),
        .answer_count = UINT16_C(44),
        .authority_count = UINT16_C(55),
        .additional_count = UINT16_C(66)
    };

    if (payload != NULL) {
        before = *payload;
    }

    TEST_CHECK(
        dns_parse_header(payload, &header) == expected_error
    );

    TEST_CHECK(header.transaction_id == UINT16_C(11));
    TEST_CHECK(header.flags == UINT16_C(22));
    TEST_CHECK(header.question_count == UINT16_C(33));
    TEST_CHECK(header.answer_count == UINT16_C(44));
    TEST_CHECK(header.authority_count == UINT16_C(55));
    TEST_CHECK(header.additional_count == UINT16_C(66));

    if (payload != NULL) {
        TEST_CHECK(payload->data == before.data);
        TEST_CHECK(payload->length == before.length);
        TEST_CHECK(payload->offset == before.offset);
    }

    return EXIT_SUCCESS;
}

/**
 * @brief 覆盖所有不足12字节的长度及非法参数。
 */
static int test_dns_failure_contract(void)
{
    static const uint8_t data[DNS_HEADER_LENGTH] = {0};

    byte_cursor_t payload;
    size_t length;

    /*
     * 分别验证0到11字节，不能只测一种截断位置。
     */
    for (length = 0U; length < DNS_HEADER_LENGTH; ++length) {
        TEST_CHECK(
            byte_cursor_init(&payload, data, length) == 0
        );

        TEST_CHECK(
            check_dns_failure(&payload, ENODATA) == EXIT_SUCCESS
        );
    }

    /* 合法空缓冲区属于数据不足，不属于参数错误。 */
    TEST_CHECK(byte_cursor_init(&payload, NULL, 0U) == 0);

    TEST_CHECK(
        check_dns_failure(&payload, ENODATA) == EXIT_SUCCESS
    );

    TEST_CHECK(
        check_dns_failure(NULL, EINVAL) == EXIT_SUCCESS
    );

    /* 无效输出指针不能移动输入游标。 */
    TEST_CHECK(
        byte_cursor_init(&payload, data, sizeof(data)) == 0
    );

    TEST_CHECK(dns_parse_header(&payload, NULL) == EINVAL);
    TEST_CHECK(payload.data == data);
    TEST_CHECK(payload.length == sizeof(data));
    TEST_CHECK(payload.offset == 0U);

    /* 主动构造损坏游标，验证不变量检查。 */
    payload.offset = payload.length + 1U;

    TEST_CHECK(
        check_dns_failure(&payload, EINVAL) == EXIT_SUCCESS
    );

    /* 声称有数据却没有数据地址，同样属于非法游标。 */
    payload = (byte_cursor_t){
        .data = NULL,
        .length = DNS_HEADER_LENGTH,
        .offset = 0U
    };

    TEST_CHECK(
        check_dns_failure(&payload, EINVAL) == EXIT_SUCCESS
    );

    return EXIT_SUCCESS;
}

int main(void)
{
    TEST_CHECK(test_dns_query_header() == EXIT_SUCCESS);
    puts("[PASS] DNS query header");

    TEST_CHECK(test_dns_response_header() == EXIT_SUCCESS);
    puts("[PASS] DNS response header");

    TEST_CHECK(test_dns_failure_contract() == EXIT_SUCCESS);
    puts("[PASS] DNS failure contract");

    return EXIT_SUCCESS;
}