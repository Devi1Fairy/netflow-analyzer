#ifndef TCP_FRAMING_MESSAGE_IO_H
#define TCP_FRAMING_MESSAGE_IO_H

#include "message.h"

/*
 * size_t定义在stddef.h中。
 *
 * 消息头中的payload_length使用uint32_t，是线上固定宽度字段；
 * 这里的payload_length和payload_buffer_size使用size_t，是本机内存
 * 缓冲区的实际大小。
 */
#include <stddef.h>

/**
 * @brief 发送一条包含固定消息头和变长载荷的完整消息。
 *
 * 函数会自动构造协议版本和载荷长度，并依次发送：
 *
 *     12字节消息头
 *     payload_length字节载荷
 *
 * 如果消息头已经发送、载荷发送失败，连接中会留下不完整消息。
 * 调用者应该关闭该连接，不能继续发送下一条消息。
 *
 * @param socket_fd 已经连接的Socket文件描述符。
 *
 * @param type 消息类型。
 *             message_type_t提高程序内部可读性，编码时转换为1字节。
 *
 * @param payload 指向待发送载荷的只读指针。
 *                使用const void *允许发送文本或任意二进制数据。
 *                payload_length为0时可以为NULL。
 *
 * @param payload_length 载荷的本机内存长度。
 *                       使用size_t是因为它通常来自sizeof或缓冲区大小。
 *                       编码前会检查它能否安全转换为uint32_t协议字段。
 *
 * @return 完整消息发送成功时返回0；
 *         socket_fd无效时返回EBADF；
 *         非零载荷对应NULL指针时返回EINVAL；
 *         类型无效时返回EINVAL；
 *         载荷超过协议上限时返回EMSGSIZE；
 *         其他错误返回编解码或send_all产生的错误码。
 */
int message_send(int socket_fd,
                 message_type_t type,
                 const void *payload,
                 size_t payload_length);

/**
 * @brief 接收并解析一条完整消息。
 *
 * 函数先读取固定12字节消息头，解码得到payload_length，再将载荷写入
 * 调用者提供的缓冲区。
 *
 * 只有消息头和全部载荷都成功接收后，函数才会修改header输出对象。
 *
 * 如果消息头已经读取，但载荷缓冲区过小或载荷接收失败，当前连接已经
 * 无法安全继续解析下一条消息，调用者应该关闭连接。
 *
 * @param socket_fd 已经连接的Socket文件描述符。
 *
 * @param header 用于保存解码后消息头的输出对象，不能为NULL。
 *
 * @param payload_buffer 用于保存载荷的可写缓冲区。
 *                       消息载荷长度为0时可以为NULL。
 *
 * @param payload_buffer_size payload_buffer实际可用字节数。
 *                            使用size_t表示本机内存大小。
 *
 * @return 完整消息接收成功时返回0；
 *         socket_fd无效时返回EBADF；
 *         header为NULL时返回EINVAL；
 *         非零载荷对应NULL缓冲区时返回EINVAL；
 *         缓冲区过小、消息头截断或载荷过大时返回EMSGSIZE；
 *         对端提前关闭时返回ECONNRESET；
 *         其他错误返回解码或recv_exact产生的错误码。
 */
int message_receive(int socket_fd,
                    message_header_t *header,
                    void *payload_buffer,
                    size_t payload_buffer_size);

#endif