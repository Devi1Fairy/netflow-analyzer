#define _POSIX_C_SOURCE 200809L

#include "bms_serial_core.h"
#include "bms_tcp_io.h"
#include "bms_tcp_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

typedef struct {
    bms_tcp_sink_t tcp;
    int64_t deadline_ms;
} gateway_sink_t;

static void request_stop(int number)
{
    (void)number;
    stop_requested = 1;
}

static int monotonic_ms(int64_t *value)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return -1;
    }
    *value = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    return 0;
}

static int remaining_ms(int64_t deadline)
{
    int64_t now;
    if (monotonic_ms(&now) == -1) {
        return -1;
    }
    return now >= deadline ? 0 : (int)(deadline - now);
}

/* 只接受十进制数字，不接受空串、符号、空格或尾随字符。 */
static int parse_number(const char *text, int maximum, int *value)
{
    if (*text == '\0') {
        return -1;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
    }

    char *end;
    errno = 0;
    long number = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || number > maximum) {
        return -1;
    }
    *value = (int)number;
    return 0;
}

/* 先撤销本地所有权，close失败也不重试，避免误关复用后的fd。 */
static int close_owned(int *fd)
{
    if (*fd < 0) {
        return 0;
    }
    int owned = *fd;
    *fd = -1;
    return close(owned);
}

static int gateway_line_sink(const char *line, size_t length, void *context)
{
    gateway_sink_t *sink = context;

    if (stop_requested) {
        errno = ECANCELED;
        return -1;
    }

    int left = remaining_ms(sink->deadline_ms);
    if (left < 0) {
        return -1;
    }
    if (left == 0) {
        errno = ETIMEDOUT;
        return -1;
    }

    bms_tcp_sink_t bounded = sink->tcp;
    if (bounded.timeout_ms > left) {
        bounded.timeout_ms = left;
    }
    return bms_tcp_line_sink(line, length, &bounded);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s SERIAL IPV4 PORT [SECONDS]\n"
               "PORT: 0..65535; SECONDS: 1..300, default 30.\n",
               argv[0]);
        return EXIT_SUCCESS;
    }

    int port;
    int seconds = 30;
    if ((argc != 4 && argc != 5) ||
        parse_number(argv[3], 65535, &port) == -1 ||
        (argc == 5 && parse_number(argv[4], 300, &seconds) == -1) ||
        seconds == 0 || argv[1][0] == '\0') {
        fprintf(stderr, "Invalid arguments; use --help\n");
        return EXIT_FAILURE;
    }

    struct sigaction action = {0};
    action.sa_handler = request_stop;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) == -1) {
        perror("sigaction SIGPIPE");
        return EXIT_FAILURE;
    }

    int64_t started;
    if (monotonic_ms(&started) == -1) {
        perror("clock");
        return EXIT_FAILURE;
    }
    int64_t deadline = started + (int64_t)seconds * 1000;
    int listener = -1;
    int client = -1;
    int serial = -1;
    int status = EXIT_FAILURE;
    bool exclusive = false;
    bms_serial_stats_t stats = {0};

    listener = bms_tcp_listen_ipv4(argv[2], (uint16_t)port);
    if (listener == -1) {
        perror("listen");
        goto cleanup;
    }

    struct sockaddr_in address = {0};
    socklen_t address_size = sizeof(address);
    if (getsockname(listener, (struct sockaddr *)&address,
                    &address_size) == -1) {
        perror("getsockname");
        goto cleanup;
    }
    fprintf(stderr, "LISTEN %s:%u\n", argv[2],
            (unsigned)ntohs(address.sin_port));

    while (!stop_requested && client < 0) {
        int left = remaining_ms(deadline);
        if (left < 0) {
            perror("clock");
            goto cleanup;
        }
        if (left == 0) {
            fprintf(stderr, "WAIT_TIMEOUT: serial not opened\n");
            goto cleanup;
        }
        client = bms_tcp_accept_client(listener, left < 200 ? left : 200);
        if (client == -1 && errno != ETIMEDOUT && errno != EINTR) {
            perror("accept");
            goto cleanup;
        }
    }

    if (stop_requested) {
        status = EXIT_SUCCESS;
        goto cleanup;
    }

    /* 本版只处理一个会话：接入后关闭监听，不再接收后续客户端。 */
    if (close_owned(&listener) == -1) {
        perror("close listener");
        goto cleanup;
    }
    int left = remaining_ms(deadline);
    if (left < 0) {
        perror("clock");
        goto cleanup;
    }
    if (left == 0) {
        fprintf(stderr, "No time left before serial open\n");
        goto cleanup;
    }
    if (stop_requested) {
        status = EXIT_SUCCESS;
        goto cleanup;
    }

    fprintf(stderr, "OPEN_SERIAL %s\n", argv[1]);
    serial = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (serial == -1) {
        perror("open serial");
        goto cleanup;
    }
    if (ioctl(serial, TIOCEXCL) == -1) {
        perror("TIOCEXCL");
        goto cleanup;
    }
    exclusive = true;

    if (bms_serial_configure_fd(serial) == -1) {
        perror("configure serial");
        goto cleanup;
    }
    fprintf(stderr, "READY: 115200/8N1 DTR=0 RTS=0 HUPCL=0; telemetry only\n");

    left = remaining_ms(deadline);
    if (left < 0) {
        perror("clock");
        goto cleanup;
    }
    if (left == 0) {
        fprintf(stderr, "No time left after serial configuration\n");
        goto cleanup;
    }

    gateway_sink_t sink = {
        .tcp = {.socket_fd = client, .timeout_ms = 100},
        .deadline_ms = deadline
    };
    int result = bms_serial_receive_fd(
        serial, gateway_line_sink, &sink, left, &stop_requested, &stats
    );
    int saved_errno = errno;

    if (stop_requested &&
        (result == 0 || saved_errno == EINTR || saved_errno == ECANCELED)) {
        status = EXIT_SUCCESS;
    } else if (result == 0 && stats.lines > 0U) {
        status = EXIT_SUCCESS;
    } else if (result == -1) {
        errno = saved_errno;
        perror("receive/forward");
    } else {
        fprintf(stderr, "No complete BMS lines forwarded\n");
    }

cleanup:
    fprintf(stderr, "lines=%" PRIu64 " discarded=%" PRIu64
            " tail_bytes=%zu discarding=%d\n",
            stats.lines, stats.discarded, stats.tail_bytes,
            stats.discarding ? 1 : 0);

    /* 不恢复旧termios，不重新拉起DTR/RTS，不自动重连。 */
    if (exclusive && ioctl(serial, TIOCNXCL) == -1) {
        perror("TIOCNXCL");
        status = EXIT_FAILURE;
    }
    if (close_owned(&serial) == -1) {
        perror("close serial");
        status = EXIT_FAILURE;
    }
    if (close_owned(&client) == -1) {
        perror("close client");
        status = EXIT_FAILURE;
    }
    if (close_owned(&listener) == -1) {
        perror("close listener");
        status = EXIT_FAILURE;
    }
    return status;
}