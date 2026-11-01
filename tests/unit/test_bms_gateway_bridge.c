/*
 * 公共接口桥接测试：不再包含生产C文件，不打开真实串口。
 */
#define _POSIX_C_SOURCE 200809L

#include "../../gateway/bms_serial_core.h"
#include "../../gateway/bms_tcp_io.h"
#include "../../gateway/bms_tcp_server.h"

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
    } while (false)

/* sockets[0]是接收客户端，sockets[1]是服务端已接入的连接。 */
static void open_loopback_pair(int sockets[2])
{
    int listener = bms_tcp_listen_ipv4("127.0.0.1", 0);
    TEST_CHECK(listener >= 0);

    struct sockaddr_in address = {0};
    socklen_t address_size = sizeof(address);
    TEST_CHECK(getsockname(
        listener, (struct sockaddr *)&address, &address_size
    ) == 0);

    sockets[0] = socket(AF_INET, SOCK_STREAM, 0);
    TEST_CHECK(sockets[0] >= 0);

    struct timeval wait = {.tv_sec = 1, .tv_usec = 0};
    TEST_CHECK(setsockopt(
        sockets[0], SOL_SOCKET, SO_SNDTIMEO, &wait, sizeof(wait)
    ) == 0);
    TEST_CHECK(setsockopt(
        sockets[0], SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait)
    ) == 0);
    TEST_CHECK(connect(
        sockets[0], (const struct sockaddr *)&address, sizeof(address)
    ) == 0);

    sockets[1] = bms_tcp_accept_client(listener, 1000);
    TEST_CHECK(sockets[1] >= 0);
    TEST_CHECK(close(listener) == 0);
}

static void test_pipe_to_tcp(void)
{
    const char input[] =
        "startup\nBMS state=1\r\nBMS state=2\npending";
    const char expected[] = "BMS state=1\nBMS state=2\n";
    int channel[2];
    int sockets[2];

    open_loopback_pair(sockets);
    TEST_CHECK(pipe(channel) == 0);

    int flags = fcntl(channel[0], F_GETFL);
    TEST_CHECK(flags != -1);
    TEST_CHECK(fcntl(channel[0], F_SETFL, flags | O_NONBLOCK) == 0);
    TEST_CHECK(write(channel[1], input, sizeof(input) - 1U) ==
               (ssize_t)(sizeof(input) - 1U));
    TEST_CHECK(close(channel[1]) == 0);

    bms_serial_stats_t stats;
    volatile sig_atomic_t stop_flag = 0;

    bms_tcp_sink_t sink = {
        .socket_fd = sockets[1],
        .timeout_ms = 1000
    };

    int result = bms_serial_receive_fd(
        channel[0], bms_tcp_line_sink, &sink, 1000, &stop_flag, &stats
    );

    int saved_errno = errno;

    /* 输入写端已关闭，应报告模拟断连，而不是把断连当成功。 */
    TEST_CHECK(result == -1);
    TEST_CHECK(saved_errno == ENODEV);
    TEST_CHECK(stats.lines == 2U);
    TEST_CHECK(stats.discarded == 1U);
    TEST_CHECK(stats.tail_bytes == strlen("pending"));

    /* 结束发送方向，让接收端读到EOF并检查没有额外片段。 */
    TEST_CHECK(shutdown(sockets[1], SHUT_WR) == 0);
    char received[64];
    size_t used = 0U;

    while (used < sizeof(received)) {
        ssize_t count = recv(
            sockets[0], received + used, sizeof(received) - used, 0
        );
        if (count == 0) {
            break;
        }
        TEST_CHECK(count > 0);
        used += (size_t)count;
    }

    TEST_CHECK(used == sizeof(expected) - 1U);
    TEST_CHECK(memcmp(received, expected, used) == 0);
    TEST_CHECK(fcntl(sockets[1], F_GETFD) != -1);
    TEST_CHECK(fcntl(channel[0], F_GETFD) != -1);

    TEST_CHECK(close(channel[0]) == 0);
    TEST_CHECK(close(sockets[0]) == 0);
    TEST_CHECK(close(sockets[1]) == 0);
    puts("[PASS] pipe input reaches TCP as complete LF lines");
}

static void test_sink_errors(void)
{
    TEST_CHECK(bms_tcp_line_sink("BMS", 3U, NULL) == -1);
    TEST_CHECK(errno == EINVAL);

    /* 使用本地流式Socket对构造对端已关闭。 */
    int sockets[2];
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    TEST_CHECK(close(sockets[0]) == 0);
    TEST_CHECK(signal(SIGPIPE, SIG_DFL) != SIG_ERR);

    bms_tcp_sink_t sink = {
        .socket_fd = sockets[1],
        .timeout_ms = 100
    };

    int channel[2];
    TEST_CHECK(pipe(channel) == 0);
    int flags = fcntl(channel[0], F_GETFL);
    TEST_CHECK(flags != -1);
    TEST_CHECK(fcntl(channel[0], F_SETFL, flags | O_NONBLOCK) == 0);

    const char input[] = "startup\nBMS one\nBMS two\n";
    TEST_CHECK(write(channel[1], input, sizeof(input) - 1U) ==
               (ssize_t)(sizeof(input) - 1U));
    TEST_CHECK(close(channel[1]) == 0);

    bms_serial_stats_t stats;
    volatile sig_atomic_t stop_flag = 0;

    TEST_CHECK(bms_serial_receive_fd(
        channel[0], bms_tcp_line_sink, &sink, 1000, &stop_flag, &stats
    ) == -1);
    TEST_CHECK(errno == EPIPE || errno == ECONNRESET);
    TEST_CHECK(stats.lines == 0U);
    TEST_CHECK(fcntl(sockets[1], F_GETFD) != -1);

    TEST_CHECK(close(channel[0]) == 0);
    TEST_CHECK(close(sockets[1]) == 0);
    puts("[PASS] TCP sink errors propagate without closing caller fd");
}

static void test_receive_limits(void)
{
    int channel[2];
    TEST_CHECK(pipe(channel) == 0);
    bms_serial_stats_t stats;
    volatile sig_atomic_t stop_flag = 0;

    /* 拒绝阻塞fd，不悄悄改变调用者的fd标志。 */
    TEST_CHECK(bms_serial_receive_fd(
        channel[0], bms_tcp_line_sink, NULL, 20, &stop_flag, &stats
    ) == -1);
    TEST_CHECK(errno == EINVAL);

    int flags = fcntl(channel[0], F_GETFL);
    TEST_CHECK(flags != -1);
    TEST_CHECK(fcntl(channel[0], F_SETFL, flags | O_NONBLOCK) == 0);

    /* 写端保持打开但不写数据：到时返回，不调用输出回调。 */
    TEST_CHECK(bms_serial_receive_fd(
        channel[0], bms_tcp_line_sink, NULL, 20, &stop_flag, &stats
    ) == 0);
    TEST_CHECK(stats.lines == 0U);

    stop_flag = 1;
    TEST_CHECK(bms_serial_receive_fd(
        channel[0], bms_tcp_line_sink, NULL, 1000, &stop_flag, &stats
    ) == 0);
    TEST_CHECK(stats.lines == 0U);
    TEST_CHECK(stop_flag == 1);

    TEST_CHECK(close(channel[0]) == 0);
    TEST_CHECK(close(channel[1]) == 0);
    puts("[PASS] public receive API bounds waiting and preserves stop flag");
}

int main(void)
{
    test_pipe_to_tcp();
    test_sink_errors();
    test_receive_limits();
    return EXIT_SUCCESS;
}