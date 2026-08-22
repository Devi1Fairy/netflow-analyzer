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

/**
 * @brief 读取一个网络字节序16位无符号整数。
 *
 * 函数成功时读取连续2字节，将其转换为主机可使用的uint16_t，
 * 并把cursor的offset向后移动2字节。
 *
 * 函数失败时，不修改value，也不移动cursor。
 *
 * @param cursor 指向需要读取和移动的游标。
 * @param value 指向保存16位读取结果的变量。
 *
 * @return 成功时返回0；
 *         参数或游标状态无效时返回EINVAL；
 *         剩余数据少于2字节时返回ENODATA。
 */
int byte_cursor_read_be16(byte_cursor_t *cursor,
                          uint16_t *value);

/**
 * @brief 读取一个网络字节序32位无符号整数。
 *
 * 函数成功时读取连续4字节，将其转换为主机可使用的uint32_t，
 * 并把cursor的offset向后移动4字节。
 *
 * 函数失败时，不修改value，也不移动cursor。
 *
 * @param cursor 指向需要读取和移动的游标。
 * @param value 指向保存32位读取结果的变量。
 *
 * @return 成功时返回0；
 *         参数或游标状态无效时返回EINVAL；
 *         剩余数据少于4字节时返回ENODATA。
 */
int byte_cursor_read_be32(byte_cursor_t *cursor,
                          uint32_t *value);

/**
 * @brief 复制游标当前位置开始的连续字节，并向后移动游标。
 *
 * destination必须能够容纳byte_count字节。由于C语言中的指针本身
 * 不包含目标缓冲区容量，该容量由调用者负责保证。
 *
 * byte_count为0时操作成功，此时destination可以为NULL，游标不移动。
 *
 * 函数失败时，不修改destination，也不移动cursor。
 *
 * @param cursor 指向需要读取和移动的游标。
 * @param destination 指向接收字节的目标缓冲区；
 *                    byte_count为0时可以为NULL。
 * @param byte_count 需要复制的字节数。
 *
 * @return 成功时返回0；
 *         参数或游标状态无效时返回EINVAL；
 *         剩余数据少于byte_count时返回ENODATA。
 */
int byte_cursor_read_bytes(byte_cursor_t *cursor,
                           uint8_t *destination,
                           size_t byte_count);

/**
 * @brief 安全跳过游标当前位置开始的指定数量字节。
 *
 * byte_count为0时操作成功，游标保持不变。
 *
 * 函数失败时不移动cursor。
 *
 * @param cursor 指向需要移动的游标。
 * @param byte_count 需要跳过的字节数。
 *
 * @return 成功时返回0；
 *         游标状态无效时返回EINVAL；
 *         剩余数据少于byte_count时返回ENODATA。
 */
int byte_cursor_skip(byte_cursor_t *cursor,
                     size_t byte_count);

/**
 * @brief 读取指定长度的字节区域，并为该区域创建子游标。
 *
 * 函数不会复制原始数据。slice只借用父游标所引用缓冲区中的一段
 * 连续区域，因此原始缓冲区必须在slice使用期间保持有效。
 *
 * 函数成功时：
 *
 * 1. slice从父游标当前位置开始；
 * 2. slice的length等于byte_count；
 * 3. slice的offset初始化为0；
 * 4. 父游标的offset向后移动byte_count字节。
 *
 * 父游标和子游标分别保存自己的offset。读取子游标不会继续移动
 * 父游标，但二者引用的是同一块底层字节缓冲区。
 *
 * byte_count为0时操作成功，并创建一个空切片。
 *
 * cursor和slice不能指向同一个byte_cursor_t结构体，因为一个结构体
 * 无法同时保存父游标状态和子游标状态。
 *
 * 函数失败时，不修改cursor，也不修改slice。
 *
 * @param cursor 指向需要读取和移动的父游标。
 * @param byte_count 子游标能够访问的字节数。
 * @param slice 指向用于保存子游标的结构体。
 *
 * @return 成功时返回0；
 *         参数或游标状态无效时返回EINVAL；
 *         剩余数据少于byte_count时返回ENODATA。
 */
int byte_cursor_read_slice(byte_cursor_t *cursor,
                           size_t byte_count,
                           byte_cursor_t *slice);                     

#endif
