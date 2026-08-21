#ifndef NETFLOW_ANALYZER_BYTE_READER_H
#define NETFLOW_ANALYZER_BYTE_READER_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 表示一段只读字节缓冲区上的读取游标。
 *
 * 游标不复制数据，也不拥有data指向的内存。调用者必须保证：
 * 在游标使用期间，原始缓冲区仍然有效。
 *
 * 游标不能对data调用free，也不能通过data修改原始数据。
 *
 * 合法游标必须始终满足：
 *
 * offset <= length
 */
typedef struct {
    /**
     * 指向原始字节缓冲区的只读指针。
     *
     * uint8_t是宽度明确的8位无符号整数，适合表示不带字符串
     * 语义的原始网络字节。
     *
     * const表示只能通过这个指针读取数据，不能修改原始缓冲区。
     */
    const uint8_t *data;

    /**
     * 原始缓冲区包含的总字节数。
     *
     * size_t是C语言专门表示对象大小、数组长度和内存偏移的类型，
     * 与sizeof和标准内存函数使用的长度类型一致。
     */
    size_t length;

    /**
     * 下一次读取相对于data起始地址的字节偏移。
     *
     * 初始化时为0，每次成功读取后向后移动。
     */
    size_t offset;
} byte_cursor_t;

/**
 * @brief 使用一段只读字节缓冲区初始化读取游标。
 *
 * 空缓冲区是合法输入，因此data为NULL且length为0时初始化成功。
 *
 * data为NULL但length大于0，表示调用者声称存在数据却没有提供
 * 数据地址，属于无效参数。
 *
 * 初始化成功后，cursor的offset为0。
 * 参数无效时，不修改cursor原有内容。
 *
 * @param cursor 指向待初始化的读取游标。
 * @param data 指向原始字节缓冲区；空缓冲区时可以为NULL。
 * @param length 原始缓冲区的有效字节数。
 *
 * @return 成功时返回0，参数无效时返回EINVAL。
 */
int byte_cursor_init(byte_cursor_t *cursor,
                     const uint8_t *data,
                     size_t length);

/**
 * @brief 返回游标尚未读取的字节数。
 *
 * 剩余长度使用下面的关系计算：
 *
 * remaining = length - offset
 *
 * @param cursor 指向只读游标。
 *
 * @return 游标合法时返回尚未读取的字节数；
 *         cursor为NULL或内部状态不合法时返回0。
 */
size_t byte_cursor_remaining(const byte_cursor_t *cursor);

/**
 * @brief 查看游标当前位置的一个字节，但不移动游标。
 *
 * 函数成功时，把当前字节写入value指向的位置。
 *
 * 函数失败时，不修改value，也不修改cursor。
 *
 * @param cursor 指向只读游标。
 * @param value 指向保存读取结果的uint8_t变量。
 *
 * @return 成功时返回0；
 *         参数或游标状态无效时返回EINVAL；
 *         没有剩余字节时返回ENODATA。
 */
int byte_cursor_peek_u8(const byte_cursor_t *cursor,
                        uint8_t *value);

/**
 * @brief 读取游标当前位置的一个字节，并将游标向后移动一字节。
 *
 * 函数成功时：
 *
 * 1. 把当前字节写入value指向的位置；
 * 2. 将cursor的offset增加1。
 *
 * 函数失败时，不修改value，也不移动cursor。
 *
 * @param cursor 指向需要读取和移动的游标。
 * @param value 指向保存读取结果的uint8_t变量。
 *
 * @return 成功时返回0；
 *         参数或游标状态无效时返回EINVAL；
 *         没有剩余字节时返回ENODATA。
 */
int byte_cursor_read_u8(byte_cursor_t *cursor,
                        uint8_t *value);

#endif
