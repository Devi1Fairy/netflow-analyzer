#include "tcp_client.h"

/*
 * inet_pton和htons声明在arpa/inet.h中。
 */
#include <arpa/inet.h>

#include <errno.h>

/*
 * sockaddr_in和IPPROTO_TCP等IPv4类型、常量定义在netinet/in.h中。
 */
#include <netinet/in.h>

/*
 * socket和connect声明在sys/socket.h中。
 */
#include <sys/socket.h>

/*
 * close声明在unistd.h中。
 */
#include <unistd.h>

/**
 * @brief 连接建立过程中发生错误时，关闭临时Socket。
 *
 * close可能改变errno，因此调用者必须先保存原始错误码，
 * 再调用本函数清理Socket。
 *
 * @param socket_fd 需要关闭的临时Socket文件描述符。
 * @param error_code socket、inet_pton或connect产生的原始错误码。
 *
 * @return 原始error_code，不受close结果影响。
 */
static int close_after_connect_failure(int socket_fd,
                                       int error_code)
{
    /*
     * 当前错误处理路径更关心最初导致连接失败的错误。
     * 因此忽略close的返回值。
     */
    (void)close(socket_fd);

    return error_code;
}

int tcp_client_connect(const char *ip_address,
                       uint16_t port,
                       int *client_fd)
{
    /*
     * sockaddr_in是IPv4专用地址结构体。
     *
     * {0}把整个结构体初始化为0，再分别填写地址族、
     * 服务器端口和服务器IPv4地址。
     */
    struct sockaddr_in server_address = {0};

    int socket_fd;
    int conversion_result;

    /*
     * client_fd是输出指针，必须先检查它是否有效。
     */
    if (client_fd == NULL) {
        return EINVAL;
    }

    /*
     * 函数刚开始就把输出设置为无效值。
     *
     * 这样后续任何操作失败，调用者都不会误用旧的文件描述符。
     */
    *client_fd = -1;

    /*
     * NULL不是有效的地址字符串。
     *
     * 远程服务器端口0也不是本实验可连接的有效服务端口。
     */
    if (ip_address == NULL ||
        port == UINT16_C(0)) {
        return EINVAL;
    }

    /*
     * AF_INET表示IPv4；
     * SOCK_STREAM表示可靠的字节流Socket；
     * IPPROTO_TCP明确指定TCP协议。
     *
     * POSIX规定Socket文件描述符和系统调用返回值使用int。
     */
    socket_fd =
        socket(AF_INET,
               SOCK_STREAM,
               IPPROTO_TCP);

    if (socket_fd < 0) {
        return errno;
    }

    server_address.sin_family = AF_INET;

    /*
     * htons表示host to network short。
     *
     * 它把16位端口从主机字节序转换成网络字节序。
     */
    server_address.sin_port = htons(port);

    /*
     * inet_pton把"127.0.0.1"这样的文本地址转换为
     * sockaddr_in需要的二进制IPv4地址。
     *
     * 返回值：
     * 1：转换成功；
     * 0：字符串不是合法IPv4地址；
     * -1：发生系统错误，并设置errno。
     */
    conversion_result =
        inet_pton(AF_INET,
                  ip_address,
                  &server_address.sin_addr);

    if (conversion_result == 0) {
        return close_after_connect_failure(
            socket_fd,
            EINVAL);
    }

    if (conversion_result < 0) {
        const int error_code = errno;

        return close_after_connect_failure(
            socket_fd,
            error_code);
    }

    /*
     * connect使用服务器的IP地址和端口发起TCP连接。
     *
     * sockaddr_in是IPv4专用结构体，而connect接受通用的
     * sockaddr指针，因此这里需要进行指针类型转换。
     *
     * socklen_t是POSIX专门用于表示Socket地址长度的类型。
     */
    if (connect(
            socket_fd,
            (const struct sockaddr *)&server_address,
            (socklen_t)sizeof(server_address)) != 0) {
        const int error_code = errno;

        /*
         * 阻塞connect被信号中断后，连接状态可能不确定。
         * 因此这里不在同一个Socket上盲目重试，而是关闭它，
         * 将错误交给调用者处理。
         */
        return close_after_connect_failure(
            socket_fd,
            error_code);
    }

    /*
     * 只有TCP连接成功建立后，才把文件描述符交给调用者。
     */
    *client_fd = socket_fd;

    return 0;
}