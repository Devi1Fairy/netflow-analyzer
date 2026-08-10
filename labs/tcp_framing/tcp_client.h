#ifndef TCP_FRAMING_TCP_CLIENT_H
#define TCP_FRAMING_TCP_CLIENT_H

/*
 * uint16_t定义在stdint.h中。
 *
 * TCP端口在线上固定占16位，因此接口使用uint16_t，
 * 不使用宽度可能随平台变化的unsigned int。
 */
#include <stdint.h>

/**
 * @brief 创建一个IPv4 TCP Socket，并连接到指定服务器。
 *
 * 内部依次执行：
 *
 *     socket
 *     inet_pton
 *     connect
 *
 * 客户端通常不需要主动调用bind。操作系统会自动选择合适的本地IP
 * 和一个临时端口。
 *
 * @param ip_address 服务器IPv4地址字符串，例如"127.0.0.1"。
 *
 *                   const char *表示函数只读取字符串内容，
 *                   不允许通过该指针修改字符串。
 *
 * @param port 服务器TCP端口，使用主机字节序。
 *
 *             函数内部通过htons转换成16位网络字节序。
 *
 * @param client_fd 用于保存已连接Socket文件描述符的输出指针。
 *
 *                  成功时写入大于等于0的文件描述符；
 *                  失败时保持为-1。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         其他错误返回socket、inet_pton或connect产生的errno错误码。
 */
int tcp_client_connect(const char *ip_address,
                       uint16_t port,
                       int *client_fd);

#endif