#include "stream_io.h"

#include <errno.h>

#include <stdint.h>

/*
 * send、recv和MSG_NOSIGNAL声明在sys/socket.h中。
 */
#include <sys/socket.h>

/*
 * ssize_t定义在sys/types.h中。
 *
 * send和recv需要同时表示：
 *
 * - 正数：实际处理的字节数；
 * - 0：没有处理字节或对端关闭；
 * - -1：系统调用失败。
 *
 * 所以返回类型必须是有符号的ssize_t，不能使用无符号size_t。
 */
#include <sys/types.h>

int send_all(int socket_fd,
             const void *buffer,
             size_t length)
{
    /*
     * byte_buffer指向待发送的原始字节。
     *
     * const说明不能通过这个指针修改调用者的数据。
     */
    const uint8_t *byte_buffer = buffer;

    /*
     * total_sent记录已经成功发送的字节数。
     *
     * 它表示内存对象中的位置和数量，所以使用size_t。
     */
    size_t total_sent = 0;

    if (socket_fd < 0) {
        return EBADF;
    }

    /*
     * 长度为0时没有数据需要发送。
     *
     * 先处理这个场景，使调用者可以合法地传入NULL和0。
     */
    if (length == 0) {
        return 0;
    }

    if (buffer == NULL) {
        return EINVAL;
    }
    
    while (total_sent < length) {
        /*
         * send_result必须使用ssize_t，因为send失败时返回-1。
         */
        ssize_t send_result;

        /*
         * length - total_sent是当前还没有发送的字节数。
         *
         * byte_buffer + total_sent指向尚未发送部分的第一个字节。
         *
         * MSG_NOSIGNAL是Linux提供的发送标志。向已经关闭的连接发送时，
         * 它让send返回EPIPE，而不是产生可能终止整个进程的SIGPIPE。
         */
        send_result =
            send(socket_fd,
                 byte_buffer + total_sent,
                 length - total_sent,
                 MSG_NOSIGNAL);

        if (send_result > 0) {
            /*
             * send_result为正数时，可以安全转换为size_t并累加。
             *
             * send保证返回值不会大于本次请求发送的长度。
             */
            total_sent += (size_t)send_result;
            continue;
        }

        if (send_result == 0) {
            /*
             * 对于length大于0的流式Socket，send返回0表示无法继续发送。
             */
            return EPIPE;
        }

        /*
         * 执行到这里说明send_result == -1。
         *
         * 系统调用被信号中断时，errno等于EINTR。此时没有必要认为连接
         * 已经损坏，重新执行send即可。
         */
        if (errno == EINTR) {
            continue;
        }

        /*
         * errno可能在后续函数调用中发生变化，因此立即把当前值作为
         * 函数返回值返回。
         */
        return errno;
    }

    return 0;
}

int recv_exact(int socket_fd,
               void *buffer,
               size_t length)
{
    /*
     * 接收函数需要修改调用者的缓冲区，因此这里使用非const指针。
     */
    uint8_t *byte_buffer = buffer;

    /*
     * total_received记录已经成功接收的字节数。
     */
    size_t total_received = 0;

    if (socket_fd < 0) {
        return EBADF;
    }

    if (length == 0) {
        return 0;
    }

    if (buffer == NULL) {
        return EINVAL;
    }

    while (total_received < length) {
        ssize_t recv_result;

        recv_result =
            recv(socket_fd,
                 byte_buffer + total_received,
                 length - total_received,
                 0);

        if (recv_result > 0) {
            total_received +=
                (size_t)recv_result;
            continue;
        }

        if (recv_result == 0) {
            /*
             * 流式Socket中，recv返回0表示对端已经执行有序关闭。
             *
             * 当前函数尚未收满length字节，因此从应用层消息角度看，
             * 收到的是一条被截断的消息。这里统一映射为ECONNRESET。
             */
            return ECONNRESET;
        }

        if (errno == EINTR) {
            continue;
        }

        return errno;
    }

    return 0;
}