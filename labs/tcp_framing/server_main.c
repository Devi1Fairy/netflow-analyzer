#include "tcp_server.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
/*
 * 当前只监听本机回环地址。
 *
 * 其他计算机不能通过局域网连接这个地址，适合本地学习和测试。
 */
#define SERVER_IP_ADDRESS "127.0.0.1"

/*
 * TCP端口字段固定为16位，因此使用UINT16_C构造对应宽度的常量。
 *
 * 9000大于1024，普通用户可以绑定，不需要root权限。
 */
#define SERVER_PORT UINT16_C(9000)

/**
 * @brief TCP服务器连接建立演示入口。
 *
 * 当前程序只接受一个客户端连接，不接收业务消息。
 *
 * @return 所有创建、接受和关闭操作成功时返回EXIT_SUCCESS，
 *         否则返回EXIT_FAILURE。
 */
int main(void)
{
    /*
     * 文件描述符初始化为-1，表示当前没有打开的Socket。
     */
    int server_fd = -1;
    int client_fd = -1;

    int error_code;
    int close_error;
    bool operation_failed = false;

    error_code =
        tcp_server_create(SERVER_IP_ADDRESS,
                          SERVER_PORT,
                          &server_fd);

    if (error_code != 0) {
        fprintf(stderr,
                "tcp_server_create failed: %s\n",
                strerror(error_code));

        return EXIT_FAILURE;
    }

    printf("TCP server listening on %s:%u\n",
           SERVER_IP_ADDRESS,
           (unsigned int)SERVER_PORT);

    printf("Waiting for one client connection...\n");

    /*
     * 如果还没有客户端，程序正常阻塞在accept中。
     */
    error_code =
        tcp_server_accept(server_fd,
                          &client_fd);

    if (error_code != 0) {
        fprintf(stderr,
                "tcp_server_accept failed: %s\n",
                strerror(error_code));

        close_error =
            connection_close(&server_fd);

        if (close_error != 0) {
            fprintf(stderr,
                    "Failed to close server socket: %s\n",
                    strerror(close_error));
        }

        return EXIT_FAILURE;
    }

    printf("Client connected successfully.\n");

    /*
     * 当前阶段不收发消息，连接成功后立即关闭客户端连接。
     */
    close_error =
        connection_close(&client_fd);

    if (close_error != 0) {
        fprintf(stderr,
                "Failed to close client connection: %s\n",
                strerror(close_error));

        operation_failed = true;
    } else {
        printf("Client connection closed.\n");
    }

    close_error =
        connection_close(&server_fd);

    if (close_error != 0) {
        fprintf(stderr,
                "Failed to close server socket: %s\n",
                strerror(close_error));

        operation_failed = true;
    } else {
        printf("Server stopped.\n");
    }

    return operation_failed
        ? EXIT_FAILURE
        : EXIT_SUCCESS;
}