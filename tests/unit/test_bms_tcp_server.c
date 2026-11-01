#define _POSIX_C_SOURCE 200809L

#include "../../gateway/bms_tcp_server.h"
#include "../../gateway/bms_tcp_io.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] %s:%d: %s (errno=%d)\n", \
                    __FILE__, __LINE__, #condition, errno); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

static void check_socket_flags(int fd)
{
    int status = fcntl(fd, F_GETFL);
    int descriptor = fcntl(fd, F_GETFD);

    TEST_CHECK(status != -1);
    TEST_CHECK(descriptor != -1);
    TEST_CHECK((status & O_NONBLOCK) != 0);
    TEST_CHECK((descriptor & FD_CLOEXEC) != 0);
}

static void test_loopback_line(void)
{
    int listener = bms_tcp_listen_ipv4("127.0.0.1", 0);
    TEST_CHECK(listener >= 0);
    check_socket_flags(listener);

    struct sockaddr_in address = {0};
    socklen_t address_size = sizeof(address);
    TEST_CHECK(getsockname(
        listener, (struct sockaddr *)&address, &address_size
    ) == 0);
    TEST_CHECK(address.sin_family == AF_INET);
    TEST_CHECK(ntohs(address.sin_port) != 0);
    TEST_CHECK(address.sin_addr.s_addr == htonl(INADDR_LOOPBACK));

    /* 已有监听者时，新建同地址监听必须失败，不能静默抢占端口。 */
    TEST_CHECK(bms_tcp_listen_ipv4(
        "127.0.0.1", ntohs(address.sin_port)
    ) == -1);
    TEST_CHECK(errno == EADDRINUSE);

    int client = socket(AF_INET, SOCK_STREAM, 0);
    TEST_CHECK(client >= 0);

    /* 仅测试客户端使用阻塞I/O；设置1秒上限，另有CTest总超时兜底。 */
    struct timeval wait = {.tv_sec = 1, .tv_usec = 0};
    TEST_CHECK(setsockopt(
        client, SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait)
    ) == 0);
    TEST_CHECK(setsockopt(
        client, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait)
    ) == 0);

    TEST_CHECK(connect(
        client, (const struct sockaddr *)&address, sizeof(address)
    ) == 0);

    int peer = bms_tcp_accept_client(listener, 1000);
    TEST_CHECK(peer >= 0);
    check_socket_flags(peer);

    const char line[] = "BMS state=1";
    const char expected[] = "BMS state=1\n";
    char received[sizeof(expected)] = {0};
    size_t used = 0;

    TEST_CHECK(bms_tcp_send_line(peer, line, sizeof(line) - 1, 1000) == 0);

    /* TCP是字节流，不能假设一次recv恰好得到一次send的全部内容。 */
    while (used < sizeof(expected) - 1) {
        ssize_t count = recv(
            client, received + used, sizeof(expected) - 1 - used, 0
        );
        TEST_CHECK(count > 0);
        used += (size_t)count;
    }
    TEST_CHECK(memcmp(received, expected, sizeof(expected) - 1) == 0);

    TEST_CHECK(close(peer) == 0);
    TEST_CHECK(fcntl(listener, F_GETFD) != -1);
    TEST_CHECK(close(client) == 0);
    TEST_CHECK(close(listener) == 0);
    puts("[PASS] IPv4 loopback accept and line delivery");
}

static void test_accept_timeout(void)
{
    int listener = bms_tcp_listen_ipv4("127.0.0.1", 0);
    TEST_CHECK(listener >= 0);

    errno = 0;
    TEST_CHECK(bms_tcp_accept_client(listener, 20) == -1);
    TEST_CHECK(errno == ETIMEDOUT);
    TEST_CHECK(fcntl(listener, F_GETFD) != -1);

    TEST_CHECK(close(listener) == 0);
    puts("[PASS] accept timeout preserves listener ownership");
}

static void test_invalid_arguments(void)
{
    TEST_CHECK(bms_tcp_listen_ipv4(NULL, 0) == -1);
    TEST_CHECK(errno == EINVAL);
    TEST_CHECK(bms_tcp_listen_ipv4("not-an-ip", 0) == -1);
    TEST_CHECK(errno == EINVAL);
    TEST_CHECK(bms_tcp_accept_client(-1, 100) == -1);
    TEST_CHECK(errno == EBADF);

    int blocking = socket(AF_INET, SOCK_STREAM, 0);
    TEST_CHECK(blocking >= 0);
    TEST_CHECK(bms_tcp_accept_client(blocking, 100) == -1);
    TEST_CHECK(errno == EINVAL);
    TEST_CHECK(close(blocking) == 0);

    int listener = bms_tcp_listen_ipv4("127.0.0.1", 0);
    TEST_CHECK(listener >= 0);
    TEST_CHECK(bms_tcp_accept_client(listener, 0) == -1);
    TEST_CHECK(errno == EINVAL);

    TEST_CHECK(close(listener) == 0);
    TEST_CHECK(bms_tcp_accept_client(listener, 100) == -1);
    TEST_CHECK(errno == EBADF);
    puts("[PASS] invalid arguments and closed listener");
}

int main(void)
{
    test_loopback_line();
    test_accept_timeout();
    test_invalid_arguments();
    return EXIT_SUCCESS;
}