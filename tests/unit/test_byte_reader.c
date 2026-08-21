#include "analyzer/byte_reader.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief 检查一个测试条件。
 *
 * assert在Release构建中可能因为NDEBUG而被移除，因此测试使用自己的
 * 检查宏，保证Debug和Release下都会真正执行检查。
 */
#define TEST_CHECK(condition)                                      \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr,                                        \
                    "[FAIL] %s:%d: %s\n",                          \
                    __FILE__,                                      \
                    __LINE__,                                      \
                    #condition);                                   \
            return EXIT_FAILURE;                                   \
        }                                                          \
    } while (false)

/**
 * @brief 验证普通字节缓冲区能够正确初始化游标。
 */
static int test_cursor_initialization(void)
{
    const uint8_t data[] = {
        0x10U,
        0x20U,
        0x30U
    };

    byte_cursor_t cursor;

    TEST_CHECK(byte_cursor_init(&cursor, data, sizeof(data)) == 0);

    /*
     * 游标应该借用原数组地址，而不是复制数据。
     */
    TEST_CHECK(cursor.data == data);

    /*
     * sizeof(data)返回整个数组包含的字节数，类型是size_t。
     */
    TEST_CHECK(cursor.length == sizeof(data));

    /*
     * 新游标应该从第0字节开始读取。
     */
    TEST_CHECK(cursor.offset == 0U);

    /*
     * 尚未读取任何数据，因此剩余长度等于总长度。
     */
    TEST_CHECK(byte_cursor_remaining(&cursor) == sizeof(data));

    return EXIT_SUCCESS;
}

/**
 * @brief 验证长度为0的空缓冲区是合法输入。
 */
static int test_empty_cursor(void)
{
    byte_cursor_t cursor;

    TEST_CHECK(byte_cursor_init(&cursor, NULL, 0U) == 0);

    TEST_CHECK(cursor.data == NULL);
    TEST_CHECK(cursor.length == 0U);
    TEST_CHECK(cursor.offset == 0U);
    TEST_CHECK(byte_cursor_remaining(&cursor) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证无效初始化会返回EINVAL，并保持原状态不变。
 */
static int test_invalid_initialization(void)
{
    const uint8_t original_data[] = {
        0xAAU
    };

    /*
     * 先设置一个可以识别的原始状态。
     *
     * 如果失败的初始化错误地修改了cursor，下面的检查就会发现。
     */
    byte_cursor_t cursor = {
        .data = original_data,
        .length = sizeof(original_data),
        .offset = 0U
    };

    /*
     * 没有提供用于保存结果的结构体地址。
     */
    TEST_CHECK(
        byte_cursor_init(NULL,
                         original_data,
                         sizeof(original_data)) == EINVAL
    );

    /*
     * 声称存在1字节数据，却没有提供数据地址。
     */
    TEST_CHECK(byte_cursor_init(&cursor, NULL, 1U) == EINVAL);

    /*
     * 失败后，调用者原有的cursor内容应该保持不变。
     */
    TEST_CHECK(cursor.data == original_data);
    TEST_CHECK(cursor.length == sizeof(original_data));
    TEST_CHECK(cursor.offset == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证不同偏移状态下的剩余长度计算。
 */
static int test_remaining_calculation(void)
{
    const uint8_t data[] = {
        0x01U,
        0x02U,
        0x03U
    };

    byte_cursor_t cursor;

    TEST_CHECK(byte_cursor_init(&cursor, data, sizeof(data)) == 0);

    /*
     * 模拟已经读取1字节。
     */
    cursor.offset = 1U;

    TEST_CHECK(byte_cursor_remaining(&cursor) == 2U);

    /*
     * offset等于length表示数据恰好读取完毕，是合法状态。
     */
    cursor.offset = cursor.length;

    TEST_CHECK(byte_cursor_remaining(&cursor) == 0U);

    /*
     * 模拟损坏状态：offset越过缓冲区结尾。
     *
     * 函数必须返回0，不能让size_t减法下溢成一个巨大正整数。
     */
    cursor.offset = cursor.length + 1U;

    TEST_CHECK(byte_cursor_remaining(&cursor) == 0U);

    /*
     * NULL游标也不能造成空指针解引用。
     */
    TEST_CHECK(byte_cursor_remaining(NULL) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证peek_u8能够查看当前字节，但不会移动游标。
 */
static int test_peek_u8(void)
{
    const uint8_t data[] = {
        0x10U,
        0x20U
    };

    byte_cursor_t cursor;
    uint8_t value = 0U;

    TEST_CHECK(byte_cursor_init(&cursor, data, sizeof(data)) == 0);

    /*
     * 第一次查看当前位置的字节。
     */
    TEST_CHECK(byte_cursor_peek_u8(&cursor, &value) == 0);
    TEST_CHECK(value == 0x10U);

    /*
     * peek只查看数据，因此offset和剩余长度都不能改变。
     */
    TEST_CHECK(cursor.offset == 0U);
    TEST_CHECK(byte_cursor_remaining(&cursor) == sizeof(data));

    /*
     * 再次peek应该仍然得到同一个字节。
     */
    value = 0U;

    TEST_CHECK(byte_cursor_peek_u8(&cursor, &value) == 0);
    TEST_CHECK(value == 0x10U);
    TEST_CHECK(cursor.offset == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证read_u8能够按顺序读取字节并移动游标。
 */
static int test_read_u8(void)
{
    const uint8_t data[] = {
        0x10U,
        0x20U
    };

    byte_cursor_t cursor;
    uint8_t value = 0U;

    TEST_CHECK(byte_cursor_init(&cursor, data, sizeof(data)) == 0);

    /*
     * 读取第一个字节。
     */
    TEST_CHECK(byte_cursor_read_u8(&cursor, &value) == 0);
    TEST_CHECK(value == 0x10U);
    TEST_CHECK(cursor.offset == 1U);
    TEST_CHECK(byte_cursor_remaining(&cursor) == 1U);

    /*
     * 再次读取时，应该取得第二个字节。
     */
    value = 0U;

    TEST_CHECK(byte_cursor_read_u8(&cursor, &value) == 0);
    TEST_CHECK(value == 0x20U);
    TEST_CHECK(cursor.offset == sizeof(data));
    TEST_CHECK(byte_cursor_remaining(&cursor) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证u8读取失败时不会修改输出值或游标位置。
 */
static int test_u8_error_handling(void)
{
    byte_cursor_t empty_cursor;
    uint8_t value = 0xA5U;

    TEST_CHECK(byte_cursor_init(&empty_cursor, NULL, 0U) == 0);

    /*
     * 空游标是合法状态，但没有数据可以查看。
     */
    TEST_CHECK(
        byte_cursor_peek_u8(&empty_cursor, &value) == ENODATA
    );

    TEST_CHECK(value == 0xA5U);
    TEST_CHECK(empty_cursor.offset == 0U);

    /*
     * 空游标也没有数据可以读取。
     */
    TEST_CHECK(
        byte_cursor_read_u8(&empty_cursor, &value) == ENODATA
    );

    TEST_CHECK(value == 0xA5U);
    TEST_CHECK(empty_cursor.offset == 0U);

    /*
     * 输出地址为NULL时，函数无法保存读取结果。
     */
    TEST_CHECK(
        byte_cursor_peek_u8(&empty_cursor, NULL) == EINVAL
    );

    TEST_CHECK(
        byte_cursor_read_u8(&empty_cursor, NULL) == EINVAL
    );

    TEST_CHECK(empty_cursor.offset == 0U);

    /*
     * 游标地址本身为NULL也属于参数错误。
     */
    TEST_CHECK(byte_cursor_peek_u8(NULL, &value) == EINVAL);
    TEST_CHECK(value == 0xA5U);

    TEST_CHECK(byte_cursor_read_u8(NULL, &value) == EINVAL);
    TEST_CHECK(value == 0xA5U);

    /*
     * 人为构造内部状态无效的游标：
     *
     * length声称存在1字节，但data没有提供数据地址。
     */
    byte_cursor_t invalid_cursor = {
        .data = NULL,
        .length = 1U,
        .offset = 0U
    };

    TEST_CHECK(
        byte_cursor_peek_u8(&invalid_cursor, &value) == EINVAL
    );

    TEST_CHECK(value == 0xA5U);
    TEST_CHECK(invalid_cursor.offset == 0U);

    TEST_CHECK(
        byte_cursor_read_u8(&invalid_cursor, &value) == EINVAL
    );

    TEST_CHECK(value == 0xA5U);
    TEST_CHECK(invalid_cursor.offset == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证连续读取大端16位整数。
 */
static int test_read_be16(void)
{
    const uint8_t data[] = {
        0x12U,
        0x34U,
        0xABU,
        0xCDU
    };

    byte_cursor_t cursor;
    uint16_t value = 0U;

    TEST_CHECK(byte_cursor_init(&cursor, data, sizeof(data)) == 0);

    TEST_CHECK(byte_cursor_read_be16(&cursor, &value) == 0);
    TEST_CHECK(value == UINT16_C(0x1234));
    TEST_CHECK(cursor.offset == 2U);
    TEST_CHECK(byte_cursor_remaining(&cursor) == 2U);

    TEST_CHECK(byte_cursor_read_be16(&cursor, &value) == 0);
    TEST_CHECK(value == UINT16_C(0xABCD));
    TEST_CHECK(cursor.offset == sizeof(data));
    TEST_CHECK(byte_cursor_remaining(&cursor) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证从非对齐起始地址读取大端32位整数。
 */
static int test_read_be32(void)
{
    const uint8_t data[] = {
        0xFFU,
        0x89U,
        0xABU,
        0xCDU,
        0xEFU
    };

    byte_cursor_t cursor;
    uint32_t value = 0U;

    /*
     * 从data[1]开始建立游标，而不是从数组首地址开始。
     *
     * 这模拟网络字段位于任意字节偏移的情况。我们的实现逐字节读取，
     * 不依赖uint32_t地址对齐。
     */
    TEST_CHECK(
        byte_cursor_init(&cursor,
                         &data[1],
                         sizeof(data) - 1U) == 0
    );

    TEST_CHECK(byte_cursor_read_be32(&cursor, &value) == 0);
    TEST_CHECK(value == UINT32_C(0x89ABCDEF));
    TEST_CHECK(cursor.offset == 4U);
    TEST_CHECK(byte_cursor_remaining(&cursor) == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证多字节读取遇到截断数据时不会部分消费缓冲区。
 */
static int test_multibyte_error_handling(void)
{
    const uint8_t one_byte[] = {
        0x12U
    };

    const uint8_t three_bytes[] = {
        0x12U,
        0x34U,
        0x56U
    };

    byte_cursor_t cursor;
    uint16_t value16 = UINT16_C(0xA5A5);
    uint32_t value32 = UINT32_C(0xA5A5A5A5);

    /*
     * 只剩1字节，无法完成16位读取。
     */
    TEST_CHECK(
        byte_cursor_init(&cursor,
                         one_byte,
                         sizeof(one_byte)) == 0
    );

    TEST_CHECK(
        byte_cursor_read_be16(&cursor, &value16) == ENODATA
    );

    TEST_CHECK(value16 == UINT16_C(0xA5A5));
    TEST_CHECK(cursor.offset == 0U);

    /*
     * 只剩3字节，无法完成32位读取。
     */
    TEST_CHECK(
        byte_cursor_init(&cursor,
                         three_bytes,
                         sizeof(three_bytes)) == 0
    );

    TEST_CHECK(
        byte_cursor_read_be32(&cursor, &value32) == ENODATA
    );

    TEST_CHECK(value32 == UINT32_C(0xA5A5A5A5));
    TEST_CHECK(cursor.offset == 0U);

    /*
     * NULL输出地址属于参数错误，并且不能移动游标。
     */
    TEST_CHECK(byte_cursor_read_be16(&cursor, NULL) == EINVAL);
    TEST_CHECK(cursor.offset == 0U);

    TEST_CHECK(byte_cursor_read_be32(&cursor, NULL) == EINVAL);
    TEST_CHECK(cursor.offset == 0U);

    /*
     * NULL游标属于参数错误，输出变量必须保持原值。
     */
    TEST_CHECK(byte_cursor_read_be16(NULL, &value16) == EINVAL);
    TEST_CHECK(value16 == UINT16_C(0xA5A5));

    TEST_CHECK(byte_cursor_read_be32(NULL, &value32) == EINVAL);
    TEST_CHECK(value32 == UINT32_C(0xA5A5A5A5));

    return EXIT_SUCCESS;
}

/**
 * @brief 安全字节读取工具的基础测试入口。
 */
int main(void)
{
    if (test_cursor_initialization() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] cursor initialization\n");

    if (test_empty_cursor() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] empty cursor\n");

    if (test_invalid_initialization() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] invalid initialization\n");

    if (test_remaining_calculation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] remaining calculation\n");

        if (test_peek_u8() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] peek u8\n");

    if (test_read_u8() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] read u8\n");

    if (test_u8_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] u8 error handling\n");

    if (test_read_be16() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] read be16\n");

    if (test_read_be32() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] read be32\n");

    if (test_multibyte_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] multibyte error handling\n");
    
    return EXIT_SUCCESS;
}