#ifndef TCP_FRAMING_STREAM_IO_H
#define TCP_FRAMING_STREAM_IO_H

#include <stddef.h>

/**
 * @brief 完整发送指定长度的字节。
 *
 * send可能只发送buffer中的一部分数据。本函数会循环调用send，
 * 直到length字节全部发送，或者发生无法恢复的错误。
 *
 * 如果函数在已经发送部分数据后失败，连接中的消息可能只发送了一部分。
 * 调用者通常应该关闭该连接，不能直接发送下一条消息，否则接收端可能
 * 无法重新确定消息边界。
 *
 * @param socket_fd 已经连接的Socket文件描述符。
 *
 *                  POSIX使用int表示文件描述符。文件描述符本质上是
 *                  当前进程文件描述符表中的整数下标。
 *
 * @param buffer 指向待发送字节的只读指针。
 *
 *
 * @param length 必须发送的字节数。
 *
 *               length等于0时不执行send并直接返回成功，此时buffer
 *               可以为NULL。
 *
 * @return 全部发送成功时返回0；
 *         socket_fd小于0时返回EBADF；
 *         length大于0但buffer为NULL时返回EINVAL；
 *         对端关闭或send返回0时返回EPIPE；
 *         其他系统调用错误返回对应的errno错误码。
 */
int send_all(int socket_fd,
             const void *buffer,
             size_t length);

/**
 * @brief 完整接收指定长度的字节。
 *
 * recv可能只返回当前已经到达的一部分数据。本函数会循环调用recv，
 * 直到length字节全部接收，或者对端关闭连接、发生错误。
 *
 * 如果对端在完整数据到达前关闭连接，本函数返回ECONNRESET。
 * 此时buffer中可能已经保存了部分数据，调用者必须丢弃这条不完整消息。
 *
 * @param socket_fd 已经连接的Socket文件描述符。
 *
 * @param buffer 指向用于保存接收数据的可写缓冲区。
 *
 *              
 * @param length 必须接收的字节数。
 *
 *               length等于0时不执行recv并直接返回成功，此时buffer
 *               可以为NULL。
 *
 * @return 完整接收成功时返回0；
 *         socket_fd小于0时返回EBADF；
 *         length大于0但buffer为NULL时返回EINVAL；
 *         对端在完整数据到达前关闭时返回ECONNRESET；
 *         其他系统调用错误返回对应的errno错误码。
 */
int recv_exact(int socket_fd,
               void *buffer,
               size_t length);

#endif