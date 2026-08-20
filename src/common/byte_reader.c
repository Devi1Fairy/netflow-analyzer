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