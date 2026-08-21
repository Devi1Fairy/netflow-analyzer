#include "analyzer/byte_reader.h"

#include <errno.h>

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

int byte_cursor_peek_u8(const byte_cursor_t *cursor,
                        uint8_t *value)
{
    /*
     * cursor和输出参数value都必须指向有效对象。
     *
     * 先验证所有参数，再写入value，保证失败时输出变量保持不变。
     */
    if (cursor == NULL || value == NULL) {
        return EINVAL;
    }

    /*
     * offset大于length表示游标已经越过缓冲区结尾。
     *
     * length大于0但data为NULL表示游标声称存在数据，却没有对应地址。
     * 这两种情况都属于游标内部状态无效。
     */
    if (cursor->offset > cursor->length ||
        (cursor->data == NULL && cursor->length > 0U)) {
        return EINVAL;
    }

    /*
     * 剩余长度为0表示游标位于缓冲区结尾，没有字节可以查看。
     *
     * 此时返回ENODATA，但不修改value和offset。
     */
    if (byte_cursor_remaining(cursor) < 1U) {
        return ENODATA;
    }

    /*
     * 只有在参数、游标状态和剩余长度全部有效后，才访问原始缓冲区。
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
