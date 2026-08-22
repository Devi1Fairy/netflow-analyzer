#include "analyzer/byte_reader.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * @brief 验证read_bytes能够复制包含0字节的二进制数据。
 */
static int test_read_bytes(void)
{
    const uint8_t data[] = {
        0x10U,
        0x00U,
        0x30U,
        0x40U
    };

    const uint8_t expected[] = {
        0x10U,
        0x00U,
        0x30U
    };

    uint8_t destination[sizeof(expected)] = {0U};
    byte_cursor_t cursor;

    TEST_CHECK(byte_cursor_init(&cursor, data, sizeof(data)) == 0);

    TEST_CHECK(
        byte_cursor_read_bytes(&cursor,
                               destination,
                               sizeof(destination)) == 0
    );

    /*
     * memcmp按照明确长度比较二进制数据，不受中间0x00影响。
     */
    TEST_CHECK(
        memcmp(destination,
               expected,
               sizeof(expected)) == 0
    );

    TEST_CHECK(cursor.offset == sizeof(destination));
    TEST_CHECK(byte_cursor_remaining(&cursor) == 1U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证read_bytes失败和零长度读取的状态语义。
 */
static int test_read_bytes_error_handling(void)
{
    const uint8_t data[] = {
        0x11U,
        0x22U
    };

    const uint8_t unchanged[] = {
        0xA5U,
        0xA5U,
        0xA5U
    };

    uint8_t destination[] = {
        0xA5U,
        0xA5U,
        0xA5U
    };

    byte_cursor_t cursor;

    TEST_CHECK(byte_cursor_init(&cursor, data, sizeof(data)) == 0);

    /*
     * 零长度读取是合法空操作，允许destination为NULL。
     */
    TEST_CHECK(
        byte_cursor_read_bytes(&cursor, NULL, 0U) == 0
    );

    TEST_CHECK(cursor.offset == 0U);

    /*
     * 非零长度读取必须提供目标地址。
     */
    TEST_CHECK(
        byte_cursor_read_bytes(&cursor, NULL, 1U) == EINVAL
    );

    TEST_CHECK(cursor.offset == 0U);

    /*
     * 源数据只有2字节，但要求读取3字节。
     */
    TEST_CHECK(
        byte_cursor_read_bytes(&cursor,
                               destination,
                               sizeof(destination)) == ENODATA
    );

    /*
     * 失败后目标缓冲区和游标位置都必须保持不变。
     */
    TEST_CHECK(
        memcmp(destination,
               unchanged,
               sizeof(destination)) == 0
    );

    TEST_CHECK(cursor.offset == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证skip能够安全移动游标，并拒绝超出剩余长度。
 */
static int test_skip(void)
{
    const uint8_t data[] = {
        0x10U,
        0x20U,
        0x30U,
        0x40U
    };

    byte_cursor_t cursor;

    TEST_CHECK(byte_cursor_init(&cursor, data, sizeof(data)) == 0);

    /*
     * 零长度跳过是合法空操作。
     */
    TEST_CHECK(byte_cursor_skip(&cursor, 0U) == 0);
    TEST_CHECK(cursor.offset == 0U);

    /*
     * 跳过前2字节。
     */
    TEST_CHECK(byte_cursor_skip(&cursor, 2U) == 0);
    TEST_CHECK(cursor.offset == 2U);
    TEST_CHECK(byte_cursor_remaining(&cursor) == 2U);

    /*
     * SIZE_MAX远大于剩余长度。
     *
     * 函数必须先比较剩余长度，而不能直接执行offset + SIZE_MAX。
     */
    TEST_CHECK(byte_cursor_skip(&cursor, SIZE_MAX) == ENODATA);
    TEST_CHECK(cursor.offset == 2U);

    /*
     * 剩余2字节，但要求跳过3字节，也必须失败且不移动。
     */
    TEST_CHECK(byte_cursor_skip(&cursor, 3U) == ENODATA);
    TEST_CHECK(cursor.offset == 2U);

    /*
     * 恰好跳过剩余2字节。
     */
    TEST_CHECK(byte_cursor_skip(&cursor, 2U) == 0);
    TEST_CHECK(cursor.offset == sizeof(data));
    TEST_CHECK(byte_cursor_remaining(&cursor) == 0U);

    /*
     * NULL游标属于参数错误。
     */
    TEST_CHECK(byte_cursor_skip(NULL, 0U) == EINVAL);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证子游标引用正确区域，并拥有独立的读取位置。
 */
static int test_read_slice(void)
{
    const uint8_t data[] = {
        0x10U,
        0x20U,
        0x30U,
        0x40U,
        0x50U
    };

    byte_cursor_t parent;
    byte_cursor_t slice;
    uint8_t value = 0U;

    TEST_CHECK(byte_cursor_init(&parent, data, sizeof(data)) == 0);

    /*
     * 先跳过第一个字节，使切片从data[1]开始。
     */
    TEST_CHECK(byte_cursor_skip(&parent, 1U) == 0);

    /*
     * 从父游标当前位置取得3字节子区域：
     *
     * 0x20 0x30 0x40
     */
    TEST_CHECK(
        byte_cursor_read_slice(&parent, 3U, &slice) == 0
    );

    /*
     * 子游标直接引用原数组，不复制数据。
     */
    TEST_CHECK(slice.data == &data[1]);
    TEST_CHECK(slice.length == 3U);
    TEST_CHECK(slice.offset == 0U);

    /*
     * 父游标已经消费了1 + 3字节，只剩最后的0x50。
     */
    TEST_CHECK(parent.offset == 4U);
    TEST_CHECK(byte_cursor_remaining(&parent) == 1U);

    /*
     * 读取子游标只会修改slice.offset，不会继续修改parent.offset。
     */
    TEST_CHECK(byte_cursor_read_u8(&slice, &value) == 0);
    TEST_CHECK(value == 0x20U);
    TEST_CHECK(slice.offset == 1U);
    TEST_CHECK(parent.offset == 4U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证切片创建失败及零长度切片的状态语义。
 */
static int test_read_slice_error_handling(void)
{
    const uint8_t data[] = {
        0x11U,
        0x22U
    };

    const uint8_t original_slice_data[] = {
        0xA5U
    };

    byte_cursor_t parent;
    byte_cursor_t empty_parent;

    /*
     * 设置可识别的原始状态，用于确认失败时slice不会被修改。
     */
    byte_cursor_t slice = {
        .data = original_slice_data,
        .length = sizeof(original_slice_data),
        .offset = 0U
    };

    TEST_CHECK(byte_cursor_init(&parent, data, sizeof(data)) == 0);

    /*
     * 父游标只有2字节，但要求创建3字节切片。
     */
    TEST_CHECK(
        byte_cursor_read_slice(&parent, 3U, &slice) == ENODATA
    );

    TEST_CHECK(parent.offset == 0U);
    TEST_CHECK(slice.data == original_slice_data);
    TEST_CHECK(slice.length == sizeof(original_slice_data));
    TEST_CHECK(slice.offset == 0U);

    /*
     * 没有提供输出结构体时返回EINVAL。
     */
    TEST_CHECK(
        byte_cursor_read_slice(&parent, 1U, NULL) == EINVAL
    );

    TEST_CHECK(parent.offset == 0U);

    /*
     * 父游标和子游标不能使用同一个结构体。
     */
    TEST_CHECK(
        byte_cursor_read_slice(&parent, 1U, &parent) == EINVAL
    );

    TEST_CHECK(parent.data == data);
    TEST_CHECK(parent.length == sizeof(data));
    TEST_CHECK(parent.offset == 0U);

    /*
     * 从空游标创建零长度切片是合法操作。
     */
    TEST_CHECK(byte_cursor_init(&empty_parent, NULL, 0U) == 0);

    TEST_CHECK(
        byte_cursor_read_slice(&empty_parent, 0U, &slice) == 0
    );

    TEST_CHECK(slice.data == NULL);
    TEST_CHECK(slice.length == 0U);
    TEST_CHECK(slice.offset == 0U);
    TEST_CHECK(empty_parent.offset == 0U);

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

        if (test_read_bytes() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] read bytes\n");

    if (test_read_bytes_error_handling() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] read bytes error handling\n");

    if (test_skip() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] skip\n");

    if (test_read_slice() != EXIT_SUCCESS) {
    return EXIT_FAILURE;
}

    printf("[PASS] read slice\n");

    if (test_read_slice_error_handling() != EXIT_SUCCESS) {
    return EXIT_FAILURE;
    }

    printf("[PASS] read slice error handling\n");
    
    return EXIT_SUCCESS;
}