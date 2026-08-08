#include "message.h"

/*
 * htonl用于在测试中构造网络字节序的非法载荷长度。
 */
#include <arpa/inet.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 测试需要直接修改编码后消息头中的特定字节，因此定义测试使用的
 * 字节偏移量。
 *
 * 这些常量对应message.h中公开说明的12字节线上格式。
 */
#define TEST_MAGIC_OFFSET 0U
#define TEST_VERSION_OFFSET 4U
#define TEST_TYPE_OFFSET 5U
#define TEST_RESERVED_OFFSET 6U
#define TEST_PAYLOAD_LENGTH_OFFSET 8U

/**
 * @brief 检查函数是否返回了预期错误码。
 *
 * @param case_name 测试场景名称。
 *                  const char *表示函数只读取字符串，不修改它。
 * @param actual_error 被测试函数实际返回的int错误码。
 * @param expected_error 当前场景预期得到的int错误码。
 *
 * @return 错误码相同时返回true，否则输出错误信息并返回false。
 */
static bool expect_error_code(const char *case_name,
                              int actual_error,
                              int expected_error)
{
    if (actual_error == expected_error) {
        return true;
    }

    fprintf(stderr,
            "%s: expected %d (%s), got %d (%s)\n",
            case_name,
            expected_error,
            strerror(expected_error),
            actual_error,
            strerror(actual_error));

    return false;
}

/**
 * @brief 比较两个内存消息头的有效字段。
 *
 * 不能直接使用memcmp比较结构体，因为结构体中可能存在未初始化的
 * 填充字节。应当逐个比较具有实际语义的成员。
 *
 * @param left 指向第一个只读消息头。
 * @param right 指向第二个只读消息头。
 *
 * @return 三个有效字段全部相同时返回true，否则返回false。
 */
static bool message_headers_are_equal(
    const message_header_t *left,
    const message_header_t *right)
{
    return left->version == right->version &&
           left->type == right->type &&
           left->payload_length == right->payload_length;
}

/**
 * @brief 验证一个合法消息头能够正确编码并解码。
 *
 * 该测试不仅检查往返结果，还检查实际编码字节，确保字段位置和
 * 网络字节序符合协议设计。
 *
 * @return 编码字节和解码结果全部正确时返回true。
 */
static bool test_valid_header_round_trip(void)
{
    const message_header_t original_header = {
        .version = MESSAGE_PROTOCOL_VERSION,
        .type = MESSAGE_TYPE_TEXT,
        .payload_length = UINT32_C(17)
    };

    /*
     * 这是payload_length等于17时预期得到的完整12字节消息头。
     *
     * uint8_t数组适合保存和比较没有主机类型含义的原始协议字节。
     */
    const uint8_t expected_bytes[MESSAGE_HEADER_SIZE] = {
        0x4EU, 0x46U, 0x41U, 0x4EU,
        0x01U,
        0x01U,
        0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x11U
    };

    uint8_t encoded_header[MESSAGE_HEADER_SIZE];
    message_header_t decoded_header;
    int error_code;

    error_code =
        message_encode_header(&original_header,
                              encoded_header,
                              sizeof(encoded_header));

    if (error_code != 0) {
        fprintf(stderr,
                "Valid header encode failed: %s\n",
                strerror(error_code));

        return false;
    }

    /*
     * 两个对象都是固定大小的uint8_t数组，不存在结构体填充问题，
     * 因此可以安全地使用memcmp逐字节比较。
     */
    if (memcmp(encoded_header,
               expected_bytes,
               sizeof(expected_bytes)) != 0) {
        fprintf(stderr,
                "Encoded bytes do not match protocol layout\n");

        return false;
    }

    error_code =
        message_decode_header(encoded_header,
                              sizeof(encoded_header),
                              &decoded_header);

    if (error_code != 0) {
        fprintf(stderr,
                "Valid header decode failed: %s\n",
                strerror(error_code));

        return false;
    }

    if (!message_headers_are_equal(&original_header,
                                   &decoded_header)) {
        fprintf(stderr,
                "Decoded header does not match original header\n");

        return false;
    }

    return true;
}

/**
 * @brief 验证编码函数拒绝无效参数和无法编码的字段。
 *
 * @return 所有场景都返回预期错误码时返回true。
 */
static bool test_encode_validation(void)
{
    const message_header_t valid_header = {
        .version = MESSAGE_PROTOCOL_VERSION,
        .type = MESSAGE_TYPE_TEXT,
        .payload_length = UINT32_C(17)
    };

    uint8_t encoded_header[MESSAGE_HEADER_SIZE];
    message_header_t invalid_header;
    int error_code;

    /*
     * header是必要输入，NULL必须返回EINVAL。
     */
    error_code =
        message_encode_header(NULL,
                              encoded_header,
                              sizeof(encoded_header));

    if (!expect_error_code("encode NULL header",
                           error_code,
                           EINVAL)) {
        return false;
    }

    /*
     * buffer是必要输出，NULL必须返回EINVAL。
     */
    error_code =
        message_encode_header(&valid_header,
                              NULL,
                              MESSAGE_HEADER_SIZE);

    if (!expect_error_code("encode NULL buffer",
                           error_code,
                           EINVAL)) {
        return false;
    }

    /*
     * 即使底层数组实际有12字节，只要调用者声明只有11字节可用，
     * 函数就不能越过buffer_size写入。
     */
    error_code =
        message_encode_header(
            &valid_header,
            encoded_header,
            MESSAGE_HEADER_SIZE - 1U);

    if (!expect_error_code("encode short buffer",
                           error_code,
                           EMSGSIZE)) {
        return false;
    }

    /*
     * 普通结构体可以安全地按值复制，因为它只包含普通数据成员，
     * 没有互斥锁、文件描述符或动态内存所有权。
     */
    invalid_header = valid_header;
    invalid_header.version =
        (uint8_t)(MESSAGE_PROTOCOL_VERSION + 1U);

    error_code =
        message_encode_header(&invalid_header,
                              encoded_header,
                              sizeof(encoded_header));

    if (!expect_error_code("encode unsupported version",
                           error_code,
                           EPROTONOSUPPORT)) {
        return false;
    }

    invalid_header = valid_header;

    /*
     * 255能够放入uint8_t，但不属于当前定义的TEXT或PING类型。
     */
    invalid_header.type =
        (message_type_t)UINT8_MAX;

    error_code =
        message_encode_header(&invalid_header,
                              encoded_header,
                              sizeof(encoded_header));

    if (!expect_error_code("encode invalid type",
                           error_code,
                           EINVAL)) {
        return false;
    }

    invalid_header = valid_header;
    invalid_header.payload_length =
        MESSAGE_MAX_PAYLOAD_SIZE + UINT32_C(1);

    error_code =
        message_encode_header(&invalid_header,
                              encoded_header,
                              sizeof(encoded_header));

    if (!expect_error_code("encode oversized payload",
                           error_code,
                           EMSGSIZE)) {
        return false;
    }

    return true;
}

/**
 * @brief 验证解码函数拒绝损坏、截断或不受支持的网络消息头。
 *
 * 测试先生成一个合法消息头，然后每次只破坏一个字段。
 * 这样可以明确知道错误是由哪个字段触发的。
 *
 * @return 所有非法消息头都被正确拒绝时返回true。
 */
static bool test_decode_validation(void)
{
    const message_header_t valid_header = {
        .version = MESSAGE_PROTOCOL_VERSION,
        .type = MESSAGE_TYPE_TEXT,
        .payload_length = UINT32_C(17)
    };

    /*
     * valid_bytes保存合法基准数据；
     * invalid_bytes用于每个测试场景的独立修改。
     */
    uint8_t valid_bytes[MESSAGE_HEADER_SIZE];
    uint8_t invalid_bytes[MESSAGE_HEADER_SIZE];

    /*
     * decoded_header先设置为哨兵值。
     *
     * 解码失败后，它不应该被修改。
     */
    message_header_t decoded_header = {
        .version = UINT8_C(99),
        .type = MESSAGE_TYPE_PING,
        .payload_length = UINT32_C(123)
    };

    const message_header_t sentinel_header = decoded_header;

    uint32_t network_oversized_length;
    int error_code;

    error_code =
        message_encode_header(&valid_header,
                              valid_bytes,
                              sizeof(valid_bytes));

    if (error_code != 0) {
        fprintf(stderr,
                "Failed to prepare valid test header: %s\n",
                strerror(error_code));

        return false;
    }

    error_code =
        message_decode_header(NULL,
                              sizeof(valid_bytes),
                              &decoded_header);

    if (!expect_error_code("decode NULL buffer",
                           error_code,
                           EINVAL)) {
        return false;
    }

    error_code =
        message_decode_header(valid_bytes,
                              sizeof(valid_bytes),
                              NULL);

    if (!expect_error_code("decode NULL output",
                           error_code,
                           EINVAL)) {
        return false;
    }

    error_code =
        message_decode_header(
            valid_bytes,
            MESSAGE_HEADER_SIZE - 1U,
            &decoded_header);

    if (!expect_error_code("decode truncated header",
                           error_code,
                           EMSGSIZE)) {
        return false;
    }

    /*
     * 每个场景先从合法字节重新复制，避免上一个场景的修改影响下一个。
     */
    memcpy(invalid_bytes,
           valid_bytes,
           sizeof(invalid_bytes));

    invalid_bytes[TEST_MAGIC_OFFSET] ^= 0xFFU;

    error_code =
        message_decode_header(invalid_bytes,
                              sizeof(invalid_bytes),
                              &decoded_header);

    if (!expect_error_code("decode invalid magic",
                           error_code,
                           EBADMSG)) {
        return false;
    }

    /*
     * 解码失败时，输出结构体应该保持原值。
     */
    if (!message_headers_are_equal(&decoded_header,
                                   &sentinel_header)) {
        fprintf(stderr,
                "Decode failure unexpectedly modified output\n");

        return false;
    }

    memcpy(invalid_bytes,
           valid_bytes,
           sizeof(invalid_bytes));

    invalid_bytes[TEST_VERSION_OFFSET] =
        (uint8_t)(MESSAGE_PROTOCOL_VERSION + 1U);

    error_code =
        message_decode_header(invalid_bytes,
                              sizeof(invalid_bytes),
                              &decoded_header);

    if (!expect_error_code("decode unsupported version",
                           error_code,
                           EPROTONOSUPPORT)) {
        return false;
    }

    memcpy(invalid_bytes,
           valid_bytes,
           sizeof(invalid_bytes));

    invalid_bytes[TEST_TYPE_OFFSET] = UINT8_MAX;

    error_code =
        message_decode_header(invalid_bytes,
                              sizeof(invalid_bytes),
                              &decoded_header);

    if (!expect_error_code("decode invalid type",
                           error_code,
                           EBADMSG)) {
        return false;
    }

    memcpy(invalid_bytes,
           valid_bytes,
           sizeof(invalid_bytes));

    invalid_bytes[TEST_RESERVED_OFFSET] = 1U;

    error_code =
        message_decode_header(invalid_bytes,
                              sizeof(invalid_bytes),
                              &decoded_header);

    if (!expect_error_code("decode nonzero reserved field",
                           error_code,
                           EBADMSG)) {
        return false;
    }

    memcpy(invalid_bytes,
           valid_bytes,
           sizeof(invalid_bytes));

    /*
     * 构造4097并转换为32位网络字节序。
     *
     * 不能直接把主机内存中的4097复制进网络消息，因为x86通常使用
     * 小端字节序。
     */
    network_oversized_length =
        htonl(MESSAGE_MAX_PAYLOAD_SIZE +
              UINT32_C(1));

    memcpy(invalid_bytes +
               TEST_PAYLOAD_LENGTH_OFFSET,
           &network_oversized_length,
           sizeof(network_oversized_length));

    error_code =
        message_decode_header(invalid_bytes,
                              sizeof(invalid_bytes),
                              &decoded_header);

    if (!expect_error_code("decode oversized payload",
                           error_code,
                           EMSGSIZE)) {
        return false;
    }

    return true;
}

/**
 * @brief 消息头单元测试程序入口。
 *
 * 每组测试失败后，程序仍然继续执行后续组，方便一次看到多个问题。
 *
 * @return 所有测试通过时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(void)
{
    bool all_passed = true;

    if (test_valid_header_round_trip()) {
        printf("[PASS] valid header round-trip\n");
    } else {
        fprintf(stderr,
                "[FAIL] valid header round-trip\n");
        all_passed = false;
    }

    if (test_encode_validation()) {
        printf("[PASS] encode validation\n");
    } else {
        fprintf(stderr,
                "[FAIL] encode validation\n");
        all_passed = false;
    }

    if (test_decode_validation()) {
        printf("[PASS] decode validation\n");
    } else {
        fprintf(stderr,
                "[FAIL] decode validation\n");
        all_passed = false;
    }

    return all_passed ? EXIT_SUCCESS : EXIT_FAILURE;
}