#include "tcp_client.h"
#include "message_io.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*
 * close声明在unistd.h中。
 */
#include <unistd.h>

/*
 * 当前连接本机回环地址上的服务器。
 */
#define SERVER_IP_ADDRESS "127.0.0.1"

/*
 * UINT16_C确保9000被构造为适合uint16_t使用的整数常量。
 */
#define SERVER_PORT UINT16_C(9000)

/*
 * 当前演示程序发送的文本载荷。
 *
 * 数组实际包含最后的字符串结束符'\0'，但发送时不会把'\0'放入
 * 网络载荷，因为消息头中的payload_length已经明确记录正文长度。
 */
static const char CLIENT_MESSAGE[] = "Hello from TCP client.";

/**
 * @brief TCP客户端发送完整消息的演示入口。
 *
 * 程序执行顺序：
 *
 * 1. 连接TCP服务器；
 * 2. 发送一条MESSAGE_TYPE_TEXT消息；
 * 3. 关闭客户端Socket。
 *
 * @return 连接、发送和关闭均成功时返回EXIT_SUCCESS，
 *         否则返回EXIT_FAILURE。
 */
int main(void)
{
    /*
     * sizeof(CLIENT_MESSAGE)包含末尾的'\0'。
     *
     * 减去1后得到真正需要发送的文本字节数。
     * sizeof的结果类型是size_t，因此payload_length也使用size_t。
     */
    const size_t payload_length = sizeof(CLIENT_MESSAGE) - 1U;


    /*
     * -1表示当前没有有效的Socket文件描述符。
     */
    int client_fd = -1;
    int error_code;

    bool operation_failed = false;

    printf("Connecting to TCP server %s:%u...\n", SERVER_IP_ADDRESS, (unsigned int)SERVER_PORT);

    error_code = tcp_client_connect(SERVER_IP_ADDRESS, SERVER_PORT, &client_fd);

    if (error_code != 0) {
        fprintf(stderr, "tcp_client_connect failed: %s\n", strerror(error_code));

        return EXIT_FAILURE;
    }

    printf("Connected to TCP server successfully.\n");

    /*
     * message_send会自动完成：
     *
     * 1. 构造协议版本和payload_length；
     * 2. 编码固定12字节消息头；
     * 3. 通过send_all发送完整消息头；
     * 4. 通过send_all发送完整文本载荷。
     *
     * CLIENT_MESSAGE会自动转换成const void *，不会被message_send修改。
     */
    error_code = message_send(client_fd, MESSAGE_TYPE_TEXT, CLIENT_MESSAGE, payload_length);

    if (error_code != 0) {
        fprintf(stderr, "message_send failed: %s\n", strerror(error_code));

         /*
         * 如果发送过程中失败，连接中可能已经存在部分消息，
         * 所以不能继续发送其他消息，只能关闭当前连接。
         */
        operation_failed = true;
    }else {
        printf("Message sent successfully.\n");
        printf("Message type: %u\n", (unsigned int)MESSAGE_TYPE_TEXT);
        printf("Payload length: %zu\n", payload_length);
        printf("Payload: %s\n", CLIENT_MESSAGE);
    }

    /*
     * 当前tcp_server模块中的connection_close不应该成为客户端依赖，
     * 因此客户端暂时直接使用POSIX close关闭自己的Socket。
     */
    if (close(client_fd) != 0) {
        const int close_error = errno;

        /*
         * 即使close报告错误，也不应该继续使用旧文件描述符。
         */
        client_fd = -1;

        fprintf(stderr, "Failed to close client socket: %s\n", strerror(close_error));

        operation_failed = true;
    }else {
        client_fd = -1;
        printf("Client connection closed.\n");
    }

    return operation_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}