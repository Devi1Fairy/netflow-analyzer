#ifndef TCP_FRAMING_TCP_SERVER_H
#define TCP_FRAMING_TCP_SERVER_H

/*
 * uint16_t定义在stdint.h中。
 *
 * TCP端口在线上固定占16位，取值范围为0～65535，因此接口使用
 * uint16_t，而不是宽度由平台决定的int或unsigned int。
 */
#include <stdint.h>

/**
 * @brief 创建、绑定并启动一个IPv4 TCP监听Socket。
 *
 * 内部依次执行：
 *
 *     socket
 *     setsockopt(SO_REUSEADDR)
 *     inet_pton
 *     bind
 *     listen
 *
 * 函数成功后，server_fd保存监听Socket。监听Socket只负责接受新连接，
 * 不直接用于收发某个客户端的业务消息。
 *
 * @param ip_address 需要绑定的IPv4地址字符串，例如"127.0.0.1"。
 *
 *                   const char *表示函数只读取字符串，不修改它。
 *
 * @param port 需要绑定的TCP端口，使用主机字节序。
 *
 *             函数内部会通过htons转换成16位网络字节序。
 *
 * @param server_fd 用于保存监听Socket文件描述符的输出指针。
 *
 *                  成功时写入大于等于0的文件描述符；
 *                  失败时保持为-1。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL；
 *         其他错误返回socket、setsockopt、inet_pton、bind或listen
 *         对应的errno错误码。
 */
int tcp_server_create(const char *ip_address,
                      uint16_t port,
                      int *server_fd);

/**
 * @brief 等待并接受一个客户端TCP连接。
 *
 * accept默认是阻塞操作。如果当前没有客户端连接，调用线程会在这里
 * 等待，直到客户端connect或系统调用发生错误。
 *
 * @param server_fd 已经进入监听状态的Socket文件描述符。
 *
 * @param client_fd 用于保存已连接Socket的输出指针。
 *
 *                  监听Socket和已连接Socket职责不同：
 *
 *                  server_fd继续负责接受新连接；
 *                  client_fd负责与当前客户端收发消息。
 *
 * @return 成功时返回0；
 *         参数无效时返回EINVAL或EBADF；
 *         accept失败时返回对应的errno错误码。
 */
int tcp_server_accept(int server_fd,
                      int *client_fd);

/**
 * @brief 关闭一个Socket文件描述符，并将它设置为-1。
 *
 * 函数可以安全地重复调用。如果socket_fd当前已经是-1，直接返回成功。
 *
 * @param socket_fd 指向待关闭文件描述符的指针，不能为NULL。
 *
 * @return 关闭成功或原值已经为-1时返回0；
 *         socket_fd为NULL时返回EINVAL；
 *         close失败时返回对应的errno错误码。
 */
int connection_close(int *socket_fd);

#endif