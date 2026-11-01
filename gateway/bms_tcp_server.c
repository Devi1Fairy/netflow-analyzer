#define _GNU_SOURCE

#include "bms_tcp_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int server_monotonic_ms(int64_t *value)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return -1;
    }

    *value = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    return 0;
}

int bms_tcp_listen_ipv4(const char *bind_ipv4, uint16_t port)
{
    struct sockaddr_in address = {0};
    int reuse = 1;

    if (bind_ipv4 == NULL) {
        errno = EINVAL;
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    int converted = inet_pton(AF_INET, bind_ipv4, &address.sin_addr);
    if (converted != 1) {
        if (converted == 0) {
            errno = EINVAL;
        }
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        return -1;
    }

    /* 允许正常重启后的地址复用；不是允许多个进程同时占用同一监听口。 */
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1 ||
        bind(fd, (const struct sockaddr *)&address, sizeof(address)) == -1 ||
        listen(fd, 4) == -1) {
        int saved_errno = errno;
        (void)close(fd);
        errno = saved_errno;
        return -1;
    }

    return fd;
}

int bms_tcp_accept_client(int listener_fd, int timeout_ms)
{
    if (listener_fd < 0) {
        errno = EBADF;
        return -1;
    }
    if (timeout_ms <= 0) {
        errno = EINVAL;
        return -1;
    }

    int flags = fcntl(listener_fd, F_GETFL);
    if (flags == -1) {
        return -1;
    }
    if ((flags & O_NONBLOCK) == 0) {
        errno = EINVAL;
        return -1;
    }

    int64_t now;
    if (server_monotonic_ms(&now) == -1) {
        return -1;
    }
    int64_t deadline = now + timeout_ms;

    for (;;) {
        if (server_monotonic_ms(&now) == -1) {
            return -1;
        }
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }

        /*
         * 监听fd非阻塞，因此即使就绪状态变化，accept4也不会无限等待。
         * accept4同时给新fd设置标志，监听fd的NONBLOCK不会自动继承。
         */
        int client_fd = accept4(
            listener_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC
        );
        if (client_fd >= 0) {
            return client_fd;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }

        if (server_monotonic_ms(&now) == -1) {
            return -1;
        }
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }

        struct pollfd event = {
            .fd = listener_fd,
            .events = POLLIN
        };
        int ready = poll(&event, 1, (int)(deadline - now));

        if (ready == -1) {
            /* EINTR交给上层，避免收到退出信号后又无条件等待。 */
            return -1;
        }
        if (ready == 0) {
            errno = ETIMEDOUT;
            return -1;
        }
        if ((event.revents & POLLNVAL) != 0) {
            errno = EBADF;
            return -1;
        }
        if ((event.revents & (POLLERR | POLLHUP)) != 0) {
            errno = EIO;
            return -1;
        }
    }
}