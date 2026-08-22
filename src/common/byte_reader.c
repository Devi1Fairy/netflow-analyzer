#include "analyzer/byte_reader.h"

#include <errno.h>
#include <string.h>

int byte_cursor_init(byte_cursor_t *cursor,
                     const uint8_t *data,
                     size_t length)
{
    /*
     * cursor必须指向一个真实的结构体，否则无法保存初始化结果。
     *
     * 当length大于0时，data必须提供有效的数据地址。
     * data为NULL且length为0表示空缓冲区，是合法输入。
     */
    if (cursor == NULL || (data == NULL && length > 0U)) {
        return EINVAL;
    }

    /*
     * 参数全部检查成功后，再一次性写入结构体。
     *
     * 这样可以保证：函数返回错误时，不会把调用者原有的cursor
     * 修改成只初始化了一部分的状态。
     *
     * 游标只借用data指向的缓冲区，不复制数据，也不取得所有权。
     */
    *cursor = (byte_cursor_t){
        .data = data,
        .length = length,
        .offset = 0U
    };

    return 0;
}

size_t byte_cursor_remaining(const byte_cursor_t *cursor)
{
    /*
     * 合法游标必须满足offset <= length。
     *
     * 必须先检查这个关系，再执行size_t无符号减法。如果offset大于
     * length，直接计算length - offset会发生无符号下溢，得到一个
     * 非常大的正整数。
     */
    if (cursor == NULL || cursor->offset > cursor->length) {
        return 0U;
    }

    return cursor->length - cursor->offset;
}

/**
 * @brief 检查游标是否能够安全读取指定数量的字节。
 *
 * 这是byte_reader.c内部使用的辅助函数，因此使用static，不会成为
 * 其他模块可以调用的全局符号。
 *
 * @param cursor 指向待检查的游标。
 * @param required_length 本次操作要求的字节数。
 *
 * @return 可以读取时返回0；
 *         参数或游标状态无效时返回EINVAL；
 *         剩余数据不足时返回ENODATA。
 */
static int byte_cursor_require_bytes(const byte_cursor_t *cursor,
                                     size_t required_length)
{
    if (cursor == NULL) {
        return EINVAL;
    }

    /*
     * 检查游标自身的不变量和数据地址。
     */
    if (cursor->offset > cursor->length ||
        (cursor->data == NULL && cursor->length > 0U)) {
        return EINVAL;
    }

    /*
     * 统一检查本次操作要求的全部长度。
     *
     * 例如read_be32必须在开始读取前确认完整的4字节都存在，不能读取
     * 一部分并移动offset后才发现数据不足。
     */
    if (byte_cursor_remaining(cursor) < required_length) {
        return ENODATA;
    }

    return 0;
}

int byte_cursor_peek_u8(const byte_cursor_t *cursor,
                        uint8_t *value)
{
    int error_code;

    /*
     * 输出地址必须有效，否则无法保存读取结果。
     */
    if (value == NULL) {
        return EINVAL;
    }

    /*
     * 检查游标状态以及是否至少存在1字节。
     */
    error_code = byte_cursor_require_bytes(cursor, 1U);

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 所有检查通过后才修改输出变量。
     */
    *value = cursor->data[cursor->offset];

    return 0;
}

int byte_cursor_read_u8(byte_cursor_t *cursor,
                        uint8_t *value)
{
    int error_code;

    /*
     * peek函数已经统一完成：
     *
     * 1. 空指针检查；
     * 2. 游标内部状态检查；
     * 3. 剩余长度检查；
     * 4. 当前字节读取。
     *
     * read复用peek，避免复制一套相同的边界检查逻辑。
     */
    error_code = byte_cursor_peek_u8(cursor, value);

    if (error_code != 0) {
        /*
         * peek失败时value和offset都没有变化，因此可以直接返回错误码。
         */
        return error_code;
    }

    /*
     * peek成功保证offset < length，因此offset加1不会超过length，
     * 也不会发生size_t溢出。
     */
    cursor->offset += 1U;

    return 0;
}

int byte_cursor_read_be16(byte_cursor_t *cursor,
                          uint16_t *value)
{
    int error_code;
    size_t offset;
    uint16_t decoded_value;

    if (value == NULL) {
        return EINVAL;
    }

    /*
     * 在读取任何一个字节之前，先保证完整的2字节都存在。
     */
    error_code = byte_cursor_require_bytes(cursor, 2U);

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 保存当前偏移，使下面的下标表达式更清楚。
     */
    offset = cursor->offset;

    /*
     * 大端16位整数的组合方式：
     *
     * 第0字节左移8位，放到结果的高8位；
     * 第1字节保持不变，放到结果的低8位。
     *
     * 例如：
     *
     * 0x12 << 8 = 0x1200
     * 0x1200 | 0x34 = 0x1234
     */
    decoded_value = (uint16_t)(
        ((uint16_t)cursor->data[offset] << 8U) |
        (uint16_t)cursor->data[offset + 1U]
    );

    /*
     * 所有读取和计算都成功后，再提交输出值和新偏移。
     */
    *value = decoded_value;
    cursor->offset += 2U;

    return 0;
}

int byte_cursor_read_be32(byte_cursor_t *cursor,
                          uint32_t *value)
{
    int error_code;
    size_t offset;
    uint32_t decoded_value;

    if (value == NULL) {
        return EINVAL;
    }

    /*
     * 在访问缓冲区前确认完整的4字节都存在。
     */
    error_code = byte_cursor_require_bytes(cursor, 4U);

    if (error_code != 0) {
        return error_code;
    }

    offset = cursor->offset;

    /*
     * 把大端的4个字节分别移动到32位结果中的正确位置：
     *
     * 字节0 → 位31～24
     * 字节1 → 位23～16
     * 字节2 → 位15～8
     * 字节3 → 位7～0
     *
     * 每个字节必须先转换为uint32_t再移位，避免使用有符号int执行
     * 24位左移时产生溢出或未定义行为。
     */
    decoded_value =
        ((uint32_t)cursor->data[offset] << 24U) |
        ((uint32_t)cursor->data[offset + 1U] << 16U) |
        ((uint32_t)cursor->data[offset + 2U] << 8U) |
        (uint32_t)cursor->data[offset + 3U];

    *value = decoded_value;
    cursor->offset += 4U;

    return 0;
}

int byte_cursor_read_bytes(byte_cursor_t *cursor,
                           uint8_t *destination,
                           size_t byte_count)
{
    int error_code;

    /*
     *读取非零数量字节时必须提供目标地址。
     *
     *零长度读取不会访问destination，因此允许destination为NULL。
     */
    if (destination == NULL && byte_count > 0U) {
        return EINVAL;
    }

    /*
     * 在复制任何字节之前，先检查游标状态和完整源数据长度。
     */
    error_code = byte_cursor_require_bytes(cursor, byte_count);

    if (error_code != 0) {
        return error_code;
    }

    /*
     * byte_count为0时不调用memcpy。
     *
     * 这样即使空游标的data和destination都是NULL，也不会把空指针
     * 传给内存复制函数。
     */
    if (byte_count == 0U) {
        return 0;
    }

    /*
     * 网络数据是任意二进制字节，可能包含0x00，因此不能使用strlen、
     * strcpy等字符串函数。memcpy严格按照byte_count复制。
     */
    memcpy(destination, cursor->data + cursor->offset, byte_count);

    /*
     * require_bytes已经保证：
     *
     * byte_count <= length - offset
     *
     * 所以这里的加法不会超过length，也不会发生size_t溢出。
     */
    cursor->offset += byte_count;

    return 0;
}

int byte_cursor_skip(byte_cursor_t *cursor,
                     size_t byte_count)
{
    int error_code;

    /*
     * 在移动游标前检查完整长度。
     */
    error_code = byte_cursor_require_bytes(cursor, byte_count);

    if (error_code != 0) {
        return error_code;
    }

    /*
     * 不访问数据内容，只更新下一次读取位置。
     *
     * byte_count为0时，这是一条合法的空操作。
     */
    cursor->offset += byte_count;

    return 0;
}
