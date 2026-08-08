#ifndef TCP_FRAMING_MESSAGE_H
#define TCP_FRAMING_MESSAGE_H

/*
 * size_t定义在stddef.h中。
 *
 * size_t是专门表示内存对象大小和数组下标的无符号整数类型，
 * sizeof运算符的返回值也是size_t。
 *
 * 它的宽度取决于当前平台：
 * 32位平台通常是32位，64位平台通常是64位。
 */
#include <stddef.h>

/*
 * uint8_t、uint16_t、uint32_t和UINT32_C定义在stdint.h中。
 *
 * 固定宽度整数适合网络协议，因为协议字段的字节数不能随编译平台改变。
 */
#include <stdint.h>

/*
 * 协议魔数对应ASCII字符串"NFAN"：
 *
 *     N    F    A    N
 *    4E   46   41   4E
 *
 * UINT32_C(value)用于构造适合32位无符号整数使用的常量。
 *
 * 如果直接写0x4E46414E，常量的实际整数类型由编译器根据数值范围决定；
 * 使用UINT32_C可以明确表达“这是一个32位协议常量”。
 *
 * 魔数不是加密或安全认证，只用于尽早发现错误输入或协议不匹配。
 */
#define MESSAGE_MAGIC UINT32_C(0x4E46414E)

/*
 * 当前协议版本。
 *
 * 1U中的U表示unsigned int，避免把该常量解释为有符号int。
 *
 * version在线上只占1字节，因此有效值必须能够放入uint8_t。
 */
#define MESSAGE_PROTOCOL_VERSION 1U

/*
 * 编码后的消息头固定占用12字节。
 *
 * 12U中的U表示unsigned int。
 * 当它与size_t比较时，编译器会把它转换为对应的无符号大小类型。
 */
#define MESSAGE_HEADER_SIZE 12U

/*
 * 单条消息允许的最大载荷长度为4096字节。
 *
 * payload_length在线上固定占4字节，所以这里使用UINT32_C构造与
 * uint32_t协议字段匹配的常量。
 *
 * 必须限制最大载荷，否则接收端直接相信网络中的长度字段，可能尝试
 * 分配几GB内存，造成内存耗尽。
 */
#define MESSAGE_MAX_PAYLOAD_SIZE UINT32_C(4096)

/**
 * @brief 定义当前协议支持的消息类型。
 *
 * enum让源码可以使用MESSAGE_TYPE_TEXT等有意义的名称，而不是直接
 * 使用难以理解的数字1和2。
 *
 * 需要注意：C语言不保证enum一定占1字节，不同编译器和平台可能使用
 * 不同大小。因此enum只用于程序内存，编码到网络时必须显式转换成
 * uint8_t。
 */
typedef enum {
    MESSAGE_TYPE_TEXT = 1, /**< 普通文本消息。 */
    MESSAGE_TYPE_PING = 2  /**< 连接存活检查消息。 */
} message_type_t;

/**
 * @brief 表示已经转换为主机字节序的消息头。
 *
 * 这个结构体只用于程序内部，不能直接通过send发送。
 *
 * 原因包括：
 *
 * 1. 结构体成员之间可能存在编译器填充字节；
 * 2. message_type_t的实际大小不固定；
 * 3. 主机可能使用小端字节序；
 * 4. 网络协议要求多字节整数使用网络字节序。
 */
typedef struct {
    
    uint8_t version;

    /*
     * 程序内部使用枚举保存消息类型，提高代码可读性。
     *
     * 编码时会把它转换成uint8_t，只在线上占1字节。
     */
    message_type_t type;

    /*
     * uint32_t是恰好32位的无符号整数。
     *
     * payload_length在协议中固定占4字节，所以不能使用unsigned int
     * 或size_t：
     *
     * - unsigned int的宽度由C实现决定；
     * - size_t会随32位、64位平台变化；
     * - uint32_t明确保证协议字段始终为32位。
     */
    uint32_t payload_length;
} message_header_t;

/**
 * @brief 将内存中的消息头编码为固定12字节网络格式。
 *
 * 多字节整数会转换为网络字节序，保留字段会被编码为0。
 *
 * @param header 指向待编码的消息头，不能为NULL。
 *               使用const表示本函数只读取该对象，不会修改它。
 * @param buffer 指向保存编码结果的字节缓冲区，不能为NULL。
 *               uint8_t适合表示不带类型含义的原始字节。
 * @param buffer_size buffer可用字节数。
 *                    使用size_t是因为它表示本机内存对象的大小。
 *
 * @return 成功时返回0；
 *         参数或消息类型无效时返回EINVAL；
 *         协议版本不支持时返回EPROTONOSUPPORT；
 *         缓冲区过小或载荷过大时返回EMSGSIZE。
 *
 * 返回类型使用int，是因为POSIX和errno风格错误码都使用int表示。
 */
int message_encode_header(const message_header_t *header,
                          uint8_t *buffer,
                          size_t buffer_size);

/**
 * @brief 从固定12字节网络格式中解码消息头。
 *
 * 函数会检查魔数、版本、类型、保留字段和最大载荷长度。
 * 只有全部检查通过后，才会修改header指向的输出对象。
 *
 * @param buffer 指向收到的网络字节，不能为NULL。
 *               const表示函数不会修改收到的原始字节。
 * @param buffer_size buffer中可以安全读取的字节数。
 * @param header 用于保存解码结果的对象，不能为NULL。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         缓冲区过小时返回EMSGSIZE；
 *         魔数、类型或保留字段无效时返回EBADMSG；
 *         协议版本不支持时返回EPROTONOSUPPORT；
 *         载荷长度超过限制时返回EMSGSIZE。
 */
int message_decode_header(const uint8_t *buffer,
                          size_t buffer_size,
                          message_header_t *header);


#endif