#include "tcp_server.h"

/*
 * inet_pton和htons声明在arpa/inet.h中。
 *
 * inet_pton把"127.0.0.1"这样的文本地址转换为网络使用的二进制地址；
 * htons把16位端口从主机字节序转换为网络字节序。
 */
#include <arpa/inet.h>

#include <errno.h>

/*
 * sockaddr_in、IPPROTO_TCP等IPv4网络类型和常量定义在netinet/in.h中。
 */
#include <netinet/in.h>

/*
 * socket、setsockopt、bind、listen和accept声明在sys/socket.h中。
 */
#include <sys/socket.h>

/*
 * close声明在unistd.h中。
 */
#include <unistd.h>

/*
 * 监听队列最多保存8个尚未被accept取走的已完成连接。
 *
 * 这个值不是服务器最大总连接数，只是内核等待队列的提示值。
 */
#define TCP_SERVER_LISTEN_BACKLOG 8

/**
 * @brief 在创建服务器的中间步骤失败时关闭临时Socket。
 *
 * close可能修改errno，所以调用者必须先保存真正的失败错误码，再调用
 * 该函数清理Socket。
 *
 * @param socket_fd 需要关闭的临时文件描述符。
 * @param error_code 原始操作失败时保存的错误码。
 *
 * @return 原始error_code，不受close结果影响。
 */
static int close_after_create_failure(int socket_fd,
                                      int error_code)
{
    (void)close(socket_fd);
    return error_code;
}

int tcp_server_create(const char *ip_address,
                      uint16_t port,
                      int *server_fd)
{
    /*
     * sockaddr_in是IPv4专用地址结构体。
     *
     * 使用{0}将包括填充区域在内的全部字节初始化为0，再单独设置
     * 地址族、端口和IPv4地址。
     */
    struct sockaddr_in server_address = {0};

    int socket_fd;
    int reuse_address = 1;
    int conversion_result;

    if (ip_address == NULL ||
        server_fd == NULL) {
        return EINVAL;
    }

    /*
     * 函数开始时先把输出设置为无效值。
     *
     * 这样任何后续步骤失败，调用者都不会误用未初始化的文件描述符。
     */
    *server_fd = -1;

    /*
     * AF_INET表示IPv4；
     * SOCK_STREAM表示可靠字节流；
     * IPPROTO_TCP明确选择TCP。
     *
     * socket成功时返回大于等于0的int文件描述符，失败时返回-1并设置
     * errno。
     */
    socket_fd =
        socket(AF_INET,
               SOCK_STREAM,
               IPPROTO_TCP);

    if (socket_fd < 0) {
        return errno;
    }

    /*
     * SO_REUSEADDR允许开发阶段的服务器在关闭后较快重新绑定相同地址。
     *
     * reuse_address使用int，是setsockopt对该选项要求的数据类型。
     */
    if (setsockopt(socket_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reuse_address,
                   (socklen_t)sizeof(reuse_address)) != 0) {
        const int error_code = errno;

        return close_after_create_failure(
            socket_fd,
            error_code);
    }

    server_address.sin_family = AF_INET;

    /*
     * TCP端口在线上使用大端网络字节序。
     */
    server_address.sin_port = htons(port);

    /*
     * inet_pton的返回值含义：
     *
     * 1：转换成功；
     * 0：文本不是合法IPv4地址；
     * -1：发生系统错误并设置errno。
     */
    conversion_result =
        inet_pton(AF_INET,
                  ip_address,
                  &server_address.sin_addr);

    if (conversion_result == 0) {
        return close_after_create_failure(
            socket_fd,
            EINVAL);
    }

    if (conversion_result < 0) {
        const int error_code = errno;

        return close_after_create_failure(
            socket_fd,
            error_code);
    }

    /*
     * bind需要通用的sockaddr指针，因此把sockaddr_in地址转换为
     * const struct sockaddr *。
     *
     * 这是POSIX Socket接口的常见设计：不同地址族使用不同结构体，
     * 但通过统一的sockaddr指针传入系统调用。
     */
    if (bind(socket_fd,
             (const struct sockaddr *)&server_address,
             (socklen_t)sizeof(server_address)) != 0) {
        const int error_code = errno;

        return close_after_create_failure(
            socket_fd,
            error_code);
    }

    /*
     * listen把已绑定Socket转换为监听Socket。
     */
    if (listen(socket_fd,
               TCP_SERVER_LISTEN_BACKLOG) != 0) {
        const int error_code = errno;

        return close_after_create_failure(
            socket_fd,
            error_code);
    }

    /*
     * 只有全部步骤成功后，才向调用者发布有效文件描述符。
     */
    *server_fd = socket_fd;

    return 0;
}

int tcp_server_accept(int server_fd,
                      int *client_fd)
{
    if (server_fd < 0) {
        return EBADF;
    }

    if (client_fd == NULL) {
        return EINVAL;
    }

    *client_fd = -1;

    for (;;) {
        int accepted_fd;

        /*
         * 当前阶段不需要记录客户端IP和端口，因此地址参数传NULL。
         *
         * accept成功后返回一个新的已连接Socket。监听Socket仍然有效。
         */
        accepted_fd =
            accept(server_fd,
                   NULL,
                   NULL);

        if (accepted_fd >= 0) {
            *client_fd = accepted_fd;
            return 0;
        }

        /*
         * accept被信号中断时可以重新等待。
         */
        if (errno == EINTR) {
            continue;
        }

        return errno;
    }
}

int connection_close(int *socket_fd)
{
    int descriptor;

    if (socket_fd == NULL) {
        return EINVAL;
    }

    /*
     * -1表示当前没有打开的文件描述符。
     *
     * 把重复关闭设计成成功，可以简化错误清理路径。
     */
    if (*socket_fd < 0) {
        return 0;
    }

    descriptor = *socket_fd;

    /*
     * 先设置为-1，再调用close。
     *
     * 这样调用者不会在close之后继续误用旧文件描述符。Linux中不应该
     * 因为close返回EINTR就盲目重试，因为原描述符可能已经关闭并被其他
     * 线程重新分配。
     */
    *socket_fd = -1;

    if (close(descriptor) != 0) {
        return errno;
    }

    return 0;
}