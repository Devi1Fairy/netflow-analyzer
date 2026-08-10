#include "tcp_client.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/**
 * @brief TCP客户端连接演示程序入口。
 *
 * 当前程序只连接服务器，不发送业务消息。
 *
 * @return 连接和关闭均成功时返回EXIT_SUCCESS，
 *         否则返回EXIT_FAILURE。
 */
int main(void)
{
    /*
     * -1表示当前没有有效的Socket文件描述符。
     */
    int client_fd = -1;
    int error_code;

    printf("Connecting to TCP server %s:%u...\n",
           SERVER_IP_ADDRESS,
           (unsigned int)SERVER_PORT);

    error_code =
        tcp_client_connect(SERVER_IP_ADDRESS,
                           SERVER_PORT,
                           &client_fd);

    if (error_code != 0) {
        fprintf(stderr,
                "tcp_client_connect failed: %s\n",
                strerror(error_code));

        return EXIT_FAILURE;
    }

    printf("Connected to TCP server successfully.\n");

    /*
     * 当前阶段只验证连接建立，所以连接成功后立即关闭Socket。
     *
     * 这里没有调用tcp_server.h中的connection_close，因为客户端
     * 不应该为了关闭Socket而依赖服务器模块。后面可以把通用的
     * Socket关闭功能提取到独立模块。
     */
    if (close(client_fd) != 0) {
        const int close_error = errno;

        /*
         * 即使close报告错误，也不应该继续使用旧文件描述符。
         */
        client_fd = -1;

        fprintf(stderr,
                "Failed to close client socket: %s\n",
                strerror(close_error));

        return EXIT_FAILURE;
    }

    client_fd = -1;

    printf("Client connection closed.\n");

    return EXIT_SUCCESS;
}