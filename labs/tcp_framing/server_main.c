#include "tcp_server.h"
#include "message_io.h"

/*
 * PRIu32定义在inttypes.h中。
 *
 * uint32_t在不同平台上不一定对应unsigned int，因此打印uint32_t时，
 * 使用PRIu32比直接写%u更加可移植。
 */
#include <inttypes.h>
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
 * @brief TCP服务器接收完整消息的演示入口。
 *
 * 程序执行顺序：
 *
 * 1. 创建监听Socket；
 * 2. 等待一个客户端连接；
 * 3. 接收并解析一条完整消息；
 * 4. 显示消息类型、长度和文本载荷；
 * 5. 关闭已连接Socket和监听Socket。
 *
 * 当前程序只处理一个客户端的一条文本消息，完成后退出。
 *
 * @return 全部操作成功时返回EXIT_SUCCESS，
 *         任何创建、接收、校验或关闭操作失败时返回EXIT_FAILURE。
 */
int main(void)
{
    /*
     * 接收缓冲区比协议允许的最大载荷多1字节。
     *
     * 网络传输的文本不要求包含字符串结束符'\0'，服务器接收完整载荷后，
     * 会使用额外的1字节自行补上'\0'，从而安全地使用%s打印。
     */
    uint8_t payload_buffer[(size_t)MESSAGE_MAX_PAYLOAD_SIZE + 1U];

    /*
     * message_receive只有在消息头和载荷全部接收成功后，
     * 才会修改received_header。
     */
    message_header_t received_header = {0};

    /*
     * 文件描述符初始化为-1，表示当前没有打开的Socket。
     */
    int server_fd = -1;
    int client_fd = -1;

    int error_code;
    int close_error;
    bool operation_failed = false;

    error_code = tcp_server_create(SERVER_IP_ADDRESS, SERVER_PORT, &server_fd);

    if(error_code != 0){
        fprintf(stderr, "tcp_server_create failed: %s\n", strerror(error_code));
        
        return EXIT_FAILURE;
    }

    printf("TCP server listening on %s: %u\n", SERVER_IP_ADDRESS, (unsigned int)SERVER_PORT);

    printf("Waiting for one client connection...\n");

    /*
     * accept默认阻塞。如果还没有客户端，服务器会在这里正常等待。
     */
    error_code = tcp_server_accept(server_fd, &client_fd);

     if (error_code != 0) {
        fprintf(stderr, "tcp_server_accept failed: %s\n", strerror(error_code));

        close_error = connection_close(&server_fd);

        if (close_error != 0) {
            fprintf(stderr, "Failed to close server socket: %s\n", strerror(close_error));
        }

        return EXIT_FAILURE;
    }

    printf("Client connected successfully.\n");
    printf("Waiting for one framed message...\n");

    /*
     * 为payload_buffer保留最后1字节，不允许网络载荷覆盖它。
     *
     * sizeof(payload_buffer)是整个数组大小；
     * 减去1后，message_receive最多写入4096字节。
     */
    error_code = message_receive(client_fd, &received_header, payload_buffer, sizeof(payload_buffer) - 1U);

    if(error_code != 0){
        fprintf(stderr, "message_receive  failed: %s\n", strerror(error_code));

        /*
        * 一旦消息接收失败，连接中的字节边界可能已经无法恢复，
        * 因此不继续读取下一条消息，而是进入统一关闭流程。
        */
        operation_failed = true;
    }else if(received_header.type != MESSAGE_TYPE_TEXT){
        /*
         * 当前演示程序只接受文本消息。
         *
         * message_receive已经验证消息类型属于协议支持范围，但这里还要
         * 验证它是不是当前业务流程所期待的MESSAGE_TYPE_TEXT。
         */
        fprintf(stderr, "Expected a text message, but received type %u\n", (unsigned int)received_header.type);

        operation_failed = true;
    }else{
        /*
         * payload_length最大为4096，而数组大小为4097，因此这个下标
         * 始终位于payload_buffer的有效范围内。
         */
        payload_buffer[(size_t)received_header.payload_length] = '\0';

        printf("Message received successfully.\n");

        /*
        * message_type_t是枚举，转换成unsigned int后使用%u输出。
        *
        * payload_length是uint32_t，使用PRIu32输出，避免假设它在当前
        * 平台上一定等于unsigned int。
        */
        printf("Message type: %u\n", (unsigned int)received_header.type);

        printf("Payload length: %" PRIu32 "\n", received_header.payload_length);

        /*
        * payload_buffer的元素类型是uint8_t，适合保存任意网络字节。
        *
        * 当前已经确认消息类型是文本，并补上了'\0'，因此可以转换为
        * const char *交给%s打印。
        */
        printf("Payload: %s\n", (const char *)payload_buffer);
    }

    /*
     * 无论消息接收成功还是失败，都必须关闭已连接Socket。
     */
    close_error =
        connection_close(&client_fd);

    if (close_error != 0) {
        fprintf(stderr, "Failed to close client connection: %s\n", strerror(close_error));

        operation_failed = true;
    } else {
        printf("Client connection closed.\n");
    }

    /*
     * 关闭监听Socket后，服务器不再接受新的客户端。
     */
    close_error =
        connection_close(&server_fd);

    if (close_error != 0) {
        fprintf(stderr, "Failed to close server socket: %s\n", strerror(close_error));

        operation_failed = true;
    } else {
        printf("Server stopped.\n");
    }

    return operation_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}