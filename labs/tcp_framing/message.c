#include "message.h"

/*
 * htonl、htons、ntohl和ntohs声明在arpa/inet.h中。
 *
 * hton：host to network，主机字节序转换为网络字节序；
 * ntoh：network to host，网络字节序转换为主机字节序；
 * l：32位long形式；
 * s：16位short形式。
 *
 * 这里的long和short是函数历史命名，不表示应该使用C语言的
 * long或short类型。参数实际使用uint32_t和uint16_t。
 */
#include <arpa/inet.h>

/*
 * EINVAL、EMSGSIZE、EBADMSG和EPROTONOSUPPORT等错误码定义在errno.h。
 */
#include <errno.h>

/*
 * bool、true和false定义在stdbool.h中。
 */
#include <stdbool.h>

/*
 * memcpy声明在string.h中。
 */
#include <string.h>

/*
 * 固定消息头中各字段的字节偏移量。
 *
 * 偏移量不会是负数，所以使用带U后缀的无符号常量。
 *
 * 字节布局：
 *
 * 0       4       5       6       8               12
 * +-------+-------+-------+-------+----------------+
 * | magic |version| type  |reserved| payload_length|
 * +-------+-------+-------+-------+----------------+
 */
#define MESSAGE_MAGIC_OFFSET 0U
#define MESSAGE_VERSION_OFFSET 4U
#define MESSAGE_TYPE_OFFSET 5U
#define MESSAGE_RESERVED_OFFSET 6U
#define MESSAGE_PAYLOAD_LENGTH_OFFSET 8U

/**
 * @brief 判断消息类型是否属于当前协议支持范围。
 *
 * 返回类型使用bool，因为结果只有“有效”和“无效”两种状态。
 *
 * @param type 待检查的消息类型。
 *
 * @return 类型有效时返回true，否则返回false。
 */
static bool message_type_is_valid(message_type_t type)
{
    return type == MESSAGE_TYPE_TEXT ||
           type == MESSAGE_TYPE_PING;
}

int message_encode_header(const message_header_t *header,
                          uint8_t *buffer,
                          size_t buffer_size)
{
    /*
     * 这些变量保存已经转换成网络字节序的整数。
     *
     * uint32_t固定占4字节，对应magic和payload_length；
     * uint16_t固定占2字节，对应reserved。
     */
    uint32_t network_magic;
    uint16_t network_reserved;
    uint32_t network_payload_length;

    /*
     * int用于保存errno/POSIX风格错误码，但本函数中没有额外函数错误
     * 需要暂存，因此直接在各个错误分支return。
     */
    if (header == NULL || buffer == NULL) {
        return EINVAL;
    }

    /*
     * buffer_size使用size_t，因为它描述本机缓冲区实际可用字节数。
     */
    if (buffer_size < MESSAGE_HEADER_SIZE) {
        return EMSGSIZE;
    }

    if (header->version != MESSAGE_PROTOCOL_VERSION) {
        return EPROTONOSUPPORT;
    }

    if (!message_type_is_valid(header->type)) {
        return EINVAL;
    }

    if (header->payload_length > MESSAGE_MAX_PAYLOAD_SIZE) {
        return EMSGSIZE;
    }

    /*
     * htonl表示host to network 32-bit。
     *
     * 网络字节序规定高位字节先传输，也称大端字节序。
     */
    network_magic = htonl(MESSAGE_MAGIC);

    /*
     * reserved在线上占2字节，因此通过htons转换16位整数。
     *
     * 当前协议规定reserved必须为0。
     */
    network_reserved = htons((uint16_t)0U);

    /*
     * payload_length在线上占4字节，因此通过htonl转换。
     */
    network_payload_length =
        htonl(header->payload_length);

    /*
     * 使用memcpy把多字节整数复制到字节缓冲区。
     *
     * 不能写成：
     *
     *     *(uint32_t *)(buffer + offset) = network_magic;
     *
     * 原因是buffer地址不一定满足uint32_t的内存对齐要求，而且这种
     * 类型转换还可能违反C语言的别名规则。
     */
    memcpy(buffer + MESSAGE_MAGIC_OFFSET,
           &network_magic,
           sizeof(network_magic));

    /*
     * version本身就是uint8_t，只占1字节，不需要转换字节序。
     */
    buffer[MESSAGE_VERSION_OFFSET] = header->version;

    /*
     * enum的内存大小不固定，因此显式转换为uint8_t，只编码1字节。
     *
     * 类型已经通过message_type_is_valid检查，当前数值可以安全放入
     * uint8_t。
     */
    buffer[MESSAGE_TYPE_OFFSET] =
        (uint8_t)header->type;

    memcpy(buffer + MESSAGE_RESERVED_OFFSET,
           &network_reserved,
           sizeof(network_reserved));

    memcpy(buffer + MESSAGE_PAYLOAD_LENGTH_OFFSET,
           &network_payload_length,
           sizeof(network_payload_length));

    return 0;
}

int message_decode_header(const uint8_t *buffer,
                          size_t buffer_size,
                          message_header_t *header)
{
    /*
     * network_开头的变量保存刚从网络字节中复制出的原始整数，
     * 此时还没有转换为当前主机的字节序。
     */
    uint32_t network_magic;
    uint16_t network_reserved;
    uint32_t network_payload_length;

    /*
     * 这些变量保存转换为主机字节序后的字段。
     */
    uint32_t magic;
    uint16_t reserved;
    uint32_t payload_length;
    message_type_t type;

    if (buffer == NULL || header == NULL) {
        return EINVAL;
    }

    if (buffer_size < MESSAGE_HEADER_SIZE) {
        return EMSGSIZE;
    }

    /*
     * 使用memcpy安全读取可能未对齐的网络字节。
     *
     * sizeof(network_magic)的类型是size_t，值在这里是4。
     */
    memcpy(&network_magic,
           buffer + MESSAGE_MAGIC_OFFSET,
           sizeof(network_magic));

    memcpy(&network_reserved,
           buffer + MESSAGE_RESERVED_OFFSET,
           sizeof(network_reserved));

    memcpy(&network_payload_length,
           buffer + MESSAGE_PAYLOAD_LENGTH_OFFSET,
           sizeof(network_payload_length));

    /*
     * ntohl把32位网络字节序转换为主机字节序；
     * ntohs把16位网络字节序转换为主机字节序。
     */
    magic = ntohl(network_magic);
    reserved = ntohs(network_reserved);
    payload_length = ntohl(network_payload_length);

    /*
     * 网络中的type只占1字节。
     *
     * 先读取uint8_t数值，再转换为message_type_t进行语义检查。
     */
    type =
        (message_type_t)buffer[MESSAGE_TYPE_OFFSET];

    if (magic != MESSAGE_MAGIC) {
        return EBADMSG;
    }

    if (buffer[MESSAGE_VERSION_OFFSET] !=
        MESSAGE_PROTOCOL_VERSION) {
        return EPROTONOSUPPORT;
    }

    if (!message_type_is_valid(type)) {
        return EBADMSG;
    }

    if (reserved != 0U) {
        return EBADMSG;
    }

    if (payload_length > MESSAGE_MAX_PAYLOAD_SIZE) {
        return EMSGSIZE;
    }

    /*
     * 使用复合字面量一次构造完整的message_header_t。
     *
     * 只有全部网络字段验证通过后才修改输出对象，避免调用者得到
     * “只更新了一部分”的无效结果。
     */
    *header = (message_header_t){
        .version = buffer[MESSAGE_VERSION_OFFSET],
        .type = type,
        .payload_length = payload_length
    };

    return 0;
}