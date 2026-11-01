#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include "../../gateway/bms_tcp_io.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TEST_CHECK(condition)                                      \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr, "[FAIL] %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #condition);                \
            exit(EXIT_FAILURE);                                    \
        }                                                          \
    } while (false)

/*
 * 仅测试目标通过--wrap=send启用包装；正式网关不启用该链接选项。
 * __real_send由链接器解析到真实send，避免包装函数递归调用自己。
 */
static size_t send_limit = SIZE_MAX;
static size_t send_call_count = 0U;

ssize_t __real_send(int fd, const void *buffer, size_t length, int flags);

ssize_t __wrap_send(int fd, const void *buffer, size_t length, int flags)
{
    if (length > send_limit) {
        length = send_limit;
    }

    ++send_call_count;
    return __real_send(fd, buffer, length, flags);
}

/* 创建同一进程内的一对流式Socket，不监听任何TCP端口。 */
static void make_pair(int channel[2])
{
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, channel) == 0);
}

static void close_pair(int channel[2])
{
    TEST_CHECK(close(channel[0]) == 0);
    TEST_CHECK(close(channel[1]) == 0);
}

static void test_send_line(bool force_short_write)
{
    int channel[2];
    const char line[] = "BMS state=1, load=0";
    const char expected[] = "BMS state=1, load=0\n";
    char actual[sizeof(expected)];
    ssize_t received;

    make_pair(channel);
    send_limit = force_short_write ? 3U : SIZE_MAX;
    send_call_count = 0U;

    TEST_CHECK(bms_tcp_send_line(channel[0], line,
                                sizeof(line) - 1U, 1000) == 0);

    if (force_short_write) {
        TEST_CHECK(send_call_count > 1U);
    }
    send_limit = SIZE_MAX;

    received = recv(channel[1], actual, sizeof(expected) - 1U,
                    MSG_WAITALL);

    TEST_CHECK(received == (ssize_t)(sizeof(expected) - 1U));
    TEST_CHECK(memcmp(actual, expected, sizeof(expected) - 1U) == 0);
    TEST_CHECK(fcntl(channel[0], F_GETFD) >= 0);  /* 发送函数不拥有fd。 */
    close_pair(channel);
}

static void test_invalid_arguments(void)
{
    int channel[2];
    char oversized[BMS_TCP_MAX_LINE + 1U];
    const char with_nul[] = {'A', '\0', 'B'};

    make_pair(channel);
    memset(oversized, 'A', sizeof(oversized));

    TEST_CHECK(bms_tcp_send_line(-1, "BMS", 3U, 100) == -1 &&
               errno == EBADF);
    TEST_CHECK(bms_tcp_send_line(channel[0], NULL, 1U, 100) == -1 &&
               errno == EINVAL);
    TEST_CHECK(bms_tcp_send_line(channel[0], "", 0U, 100) == -1 &&
               errno == EINVAL);
    TEST_CHECK(bms_tcp_send_line(channel[0], "BMS", 3U, 0) == -1 &&
               errno == EINVAL);
    TEST_CHECK(bms_tcp_send_line(channel[0], oversized,
                                sizeof(oversized), 100) == -1 &&
               errno == EMSGSIZE);
    TEST_CHECK(bms_tcp_send_line(channel[0], "A\nB", 3U, 100) == -1 &&
               errno == EINVAL);
    TEST_CHECK(bms_tcp_send_line(channel[0], with_nul,
                                sizeof(with_nul), 100) == -1 &&
               errno == EINVAL);

    close_pair(channel);
}

static void test_closed_peer(void)
{
    int channel[2];

    make_pair(channel);
    TEST_CHECK(close(channel[1]) == 0);

    /* SIGPIPE保持默认动作；MSG_NOSIGNAL应让这里返回错误而非杀死进程。 */
    TEST_CHECK(bms_tcp_send_line(channel[0], "BMS", 3U, 1000) == -1);
    TEST_CHECK(errno == EPIPE || errno == ECONNRESET);
    TEST_CHECK(fcntl(channel[0], F_GETFD) >= 0);
    TEST_CHECK(close(channel[0]) == 0);
}

static void test_slow_peer_timeout(void)
{
    int channel[2];
    int small_buffer = 4096;
    char padding[2048] = {0};
    bool full = false;

    make_pair(channel);
    TEST_CHECK(setsockopt(channel[0], SOL_SOCKET, SO_SNDBUF,
                         &small_buffer, sizeof(small_buffer)) == 0);

    /* 对端不读；用有上限的循环填满发送缓冲区。 */
    for (unsigned int attempt = 0U; attempt < 128U; ++attempt) {
        ssize_t count = send(channel[0], padding, sizeof(padding),
                              MSG_DONTWAIT | MSG_NOSIGNAL);

        if (count > 0) {
            continue;
        }

        TEST_CHECK(count == -1);
        TEST_CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
        full = true;
        break;
    }

    TEST_CHECK(full);
    TEST_CHECK(bms_tcp_send_line(channel[0], "BMS", 3U, 20) == -1 &&
               errno == ETIMEDOUT);

    close_pair(channel);
}

int main(void)
{
    TEST_CHECK(signal(SIGPIPE, SIG_DFL) != SIG_ERR);

    test_send_line(false);
    fprintf(stderr, "[PASS] send line with LF\n");

    test_send_line(true);
    fprintf(stderr, "[PASS] short sends preserve all bytes\n");

    test_invalid_arguments();
    fprintf(stderr, "[PASS] invalid arguments\n");

    test_closed_peer();
    fprintf(stderr, "[PASS] closed peer without SIGPIPE termination\n");

    test_slow_peer_timeout();
    fprintf(stderr, "[PASS] slow peer timeout\n");

    return EXIT_SUCCESS;
}