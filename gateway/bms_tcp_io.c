#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include "bms_tcp_io.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

/* 单调时钟不受系统日期调整影响；只用于本次发送的时间预算。 */
static int monotonic_ms(int64_t *value)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return -1;
    }

    *value = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    return 0;
}

int bms_tcp_send_line(int socket_fd,
                      const char *line,
                      size_t length,
                      int timeout_ms)
{
    unsigned char frame[BMS_TCP_MAX_LINE + 1U];
    size_t sent = 0U;
    int64_t now;
    int64_t deadline;

    if (socket_fd < 0) {
        errno = EBADF;
        return -1;
    }

    if (line == NULL || length == 0U || timeout_ms <= 0) {
        errno = EINVAL;
        return -1;
    }

    if (length > BMS_TCP_MAX_LINE) {
        errno = EMSGSIZE;
        return -1;
    }

    /* 保持“一次调用是一条文本行”的边界，不解析BMS字段。 */
    if (memchr(line, '\0', length) != NULL ||
        memchr(line, '\n', length) != NULL) {
        errno = EINVAL;
        return -1;
    }

    memcpy(frame, line, length);
    frame[length] = '\n';

    if (monotonic_ms(&now) == -1) {
        return -1;
    }
    deadline = now + timeout_ms;

    while (sent < length + 1U) {
        struct pollfd watched = {
            .fd = socket_fd,
            .events = POLLOUT
        };
        ssize_t count;
        int ready;

        if (monotonic_ms(&now) == -1) {
            return -1;
        }
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }

        /*
         * MSG_DONTWAIT只约束本次send，不修改整个描述符的阻塞标志。
         * MSG_NOSIGNAL让对端断开表现为错误返回，不终止整个进程。
         */
        count = send(socket_fd, frame + sent, length + 1U - sent,
                     MSG_DONTWAIT | MSG_NOSIGNAL);

        if (count > 0) {
            sent += (size_t)count;  /* 下次只发送尚未完成的后缀。 */
            continue;
        }
        if (count == 0) {
            errno = EPIPE;
            return -1;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;  /* 包括EINTR，不在这里吞掉停止信号。 */
        }

        /* 缓冲区满时等可写；每次使用同一个截止时间，不能重置预算。 */
        if (monotonic_ms(&now) == -1) {
            return -1;
        }
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }

        ready = poll(&watched, 1, (int)(deadline - now));
        if (ready == -1) {
            return -1;
        }
        if (ready == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        if ((watched.revents & POLLNVAL) != 0) {
            errno = EBADF;
            return -1;
        }
        if ((watched.revents & (POLLERR | POLLHUP)) != 0) {
            errno = EPIPE;
            return -1;
        }
    }

    return 0;
}

int bms_tcp_line_sink(const char *line, size_t length, void *context)
{
    if (context == NULL) {
        errno = EINVAL;
        return -1;
    }

    const bms_tcp_sink_t *sink = context;

    return bms_tcp_send_line(
        sink->socket_fd, line, length, sink->timeout_ms
    );
}
