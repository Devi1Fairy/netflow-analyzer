#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "bms_serial_core.h"

#define LINE_CAPACITY BMS_SERIAL_LINE_CAPACITY
#define RUN_SECONDS 30.0

typedef bms_serial_framer_t line_buffer_t;

#ifndef BMS_SERIAL_LIBRARY
static volatile sig_atomic_t stop_requested = 0;

/* 信号中只发出请求；关闭设备和输出统计由main完成。 */
static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}
#endif

static int monotonic_seconds(double *value)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return -1;
    }

    *value = (double)now.tv_sec + (double)now.tv_nsec / 1e9;
    return 0;
}

/* fd由main拥有。本函数配置串口，不发送BMS业务字符。 */
static int configure_serial(int fd)
{
    struct termios settings;
    int controls = TIOCM_DTR | TIOCM_RTS;
    int observed = 0;

    if (tcgetattr(fd, &settings) == -1) {
        return -1;
    }

    cfmakeraw(&settings);

    settings.c_iflag &= ~(IXON | IXOFF | IXANY | INPCK | IGNPAR);
    settings.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS | HUPCL);
    settings.c_cflag |= CS8 | CLOCAL | CREAD;

    settings.c_cc[VMIN] = 1;
    settings.c_cc[VTIME] = 0;

    if (cfsetispeed(&settings, B115200) == -1 ||
        cfsetospeed(&settings, B115200) == -1 ||
        tcsetattr(fd, TCSANOW, &settings) == -1) {
        return -1;
    }

    /* 对齐现有Qt：一次请求撤销DTR/RTS，不分别切换两根线。 */
    if (ioctl(fd, TIOCMBIC, &controls) == -1 ||
        ioctl(fd, TIOCMGET, &observed) == -1) {
        return -1;
    }

    if ((observed & controls) != 0) {
        errno = EIO;
        return -1;
    }

    /* 回读关键参数，不能只凭tcsetattr返回0就认定全部生效。 */
    if (tcgetattr(fd, &settings) == -1) {
        return -1;
    }

    if (cfgetispeed(&settings) != B115200 ||
        cfgetospeed(&settings) != B115200 ||
        (settings.c_cflag & CSIZE) != CS8 ||
        (settings.c_cflag & (PARENB | CSTOPB | CRTSCTS | HUPCL)) != 0 ||
        (settings.c_iflag & (IXON | IXOFF | IXANY)) != 0 ||
        settings.c_cc[VMIN] != 1 ||
        settings.c_cc[VTIME] != 0) {
        errno = EIO;
        return -1;
    }

    /* 舍弃旧输入；随后再丢到首个LF，避免把起始残行当完整报告。 */
    return tcflush(fd, TCIFLUSH);
}

/*
 * 完整行不含LF，以NUL结尾；line只在回调期间有效，不得保存此指针。
 * context由调用者拥有，本模块不解释、不释放。
 * 返回0表示处理成功；返回-1时由回调设置errno。
 */
typedef int (*line_sink_fn)(const char *line,
                            size_t length,
                            void *context);

#ifndef BMS_SERIAL_LIBRARY
/* 保留独立接收程序原来的stdout输出行为。 */
static int stdout_line_sink(const char *line, size_t length, void *context)
{
    (void)length;
    (void)context;
    return puts(line) == EOF ? -1 : 0;
}
#endif

/* 输入块只借用；输出完整文本行。超长/控制字符行丢到LF后恢复。 */
static int feed_bytes(line_buffer_t *buffer,
                      const unsigned char *data,
                      size_t length,
                      line_sink_fn sink,
                      void *context)
{
    if (buffer == NULL || sink == NULL ||
        (data == NULL && length != 0U)) {
        errno = EINVAL;
        return -1;
    }
    for (size_t index = 0; index < length; ++index) {
        unsigned char byte = data[index];

        if (byte == '\n') {
            if (buffer->discarding) {
                ++buffer->discarded;
            } else {
                if (buffer->used > 0U &&
                    buffer->data[buffer->used - 1U] == '\r') {
                    --buffer->used;
                }

                buffer->data[buffer->used] = '\0';

                if (buffer->used > 0U) {
                    if (sink(buffer->data, buffer->used, context) == -1) {
                        /* 立即结束本次处理；上层不得盲目重发失败行。 */
                        return -1;
                    }

                    ++buffer->lines;
                }
            }

            buffer->used = 0U;
            buffer->discarding = false;
            continue;
        }

        if (buffer->discarding) {
            continue;
        }

        /* 保留TAB/CR；拒绝NUL、ESC等控制字节，不把噪声当文本。 */
        if ((byte < 0x20U && byte != '\t' && byte != '\r') ||
            byte == 0x7fU ||
            buffer->used == LINE_CAPACITY) {
            buffer->used = 0U;
            buffer->discarding = true;
            continue;
        }

        buffer->data[buffer->used++] = (char)byte;
    }

    return 0;
}

static int receive_lines_until(int fd,
                               line_buffer_t *buffer,
                               line_sink_fn sink,
                               void *context,
                               const volatile sig_atomic_t *stop_flag,
                               double deadline)
{
    if (buffer == NULL || sink == NULL || stop_flag == NULL) {
        errno = EINVAL;
        return -1;
    }

    while (!*stop_flag) {
        struct pollfd watched = {
            .fd = fd,
            .events = POLLIN
        };

        unsigned char chunk[256];
        double now;
        int ready;

        if (monotonic_seconds(&now) == -1) {
            return -1;
        }

        if (now >= deadline) {
            return 0;
        }

        double remaining_ms = (deadline - now) * 1000.0;
        int wait_ms = remaining_ms >= 500.0 ? 500 : (int)remaining_ms;
        if (wait_ms < 1) {
            wait_ms = 1;
        }

        ready = poll(&watched, 1, wait_ms);

        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        if (ready == 0) {
            continue;
        }

        if ((watched.revents & POLLNVAL) != 0) {
            errno = EBADF;
            return -1;
        }

        if ((watched.revents & POLLIN) != 0) {
            ssize_t count = read(fd, chunk, sizeof(chunk));

            if (count > 0) {
                if (feed_bytes(buffer, chunk, (size_t)count,
                               sink, context) == -1) {
                    return -1;
                }
            } else if (count == 0) {
                errno = ENODEV;
                return -1;
            } else if (errno != EAGAIN &&
                       errno != EWOULDBLOCK &&
                       errno != EINTR) {
                return -1;
            }
        }

        if ((watched.revents & (POLLHUP | POLLERR)) != 0) {
            errno = ENODEV;
            return -1;
        }
    }

    return 0;
}

void bms_serial_framer_init(bms_serial_framer_t *framer)
{
    *framer = (bms_serial_framer_t){.discarding = true};
}

int bms_serial_framer_feed(bms_serial_framer_t *framer,
                           const unsigned char *data, size_t length,
                           bms_serial_line_sink_fn sink, void *context)
{
    return feed_bytes(framer, data, length, sink, context);
}

void bms_serial_framer_stats(const bms_serial_framer_t *framer,
                             bms_serial_stats_t *stats)
{
    *stats = (bms_serial_stats_t){
        .lines = framer->lines,
        .discarded = framer->discarded,
        .tail_bytes = framer->used,
        .discarding = framer->discarding
    };
}

int bms_serial_configure_fd(int fd)
{
    return configure_serial(fd);
}

int bms_serial_receive_fd(int fd,
                          bms_serial_line_sink_fn sink,
                          void *context,
                          int timeout_ms,
                          const volatile sig_atomic_t *stop_flag,
                          bms_serial_stats_t *stats)
{
    if (stats != NULL) {
        *stats = (bms_serial_stats_t){0};
    }
    if (fd < 0) {
        errno = EBADF;
        return -1;
    }
    if (sink == NULL || stop_flag == NULL || stats == NULL ||
        timeout_ms <= 0) {
        errno = EINVAL;
        return -1;
    }

    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        return -1;
    }
    if ((flags & O_NONBLOCK) == 0) {
        errno = EINVAL;
        return -1;
    }

    double started;
    if (monotonic_seconds(&started) == -1) {
        return -1;
    }

    line_buffer_t buffer = {.discarding = true};
    int result = receive_lines_until(
        fd, &buffer, sink, context, stop_flag,
        started + (double)timeout_ms / 1000.0
    );
    int saved_errno = errno;

    stats->lines = buffer.lines;
    stats->discarded = buffer.discarded;
    stats->tail_bytes = buffer.used;
    stats->discarding = buffer.discarding;
    errno = saved_errno;
    return result;
}

#ifndef BMS_SERIAL_LIBRARY
/* 保留原CLI和白盒测试使用的4参数入口。 */
static int receive_lines(int fd,
                         line_buffer_t *buffer,
                         line_sink_fn sink,
                         void *context)
{
    double started;

    if (monotonic_seconds(&started) == -1) {
        return -1;
    }
    return receive_lines_until(
        fd, buffer, sink, context, &stop_requested, started + RUN_SECONDS
    );
}

int main(int argc, char **argv)
{
    struct sigaction action = {0};

    line_buffer_t buffer = {
        .discarding = true
    };

    bool exclusive = false;
    int fd = -1;
    int status = EXIT_FAILURE;

    if (argc != 2) {
        fprintf(stderr,
                "Usage: %s /dev/actual-serial-node\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0) {
        fprintf(stderr, "Cannot enable stdout line buffering\n");
        return EXIT_FAILURE;
    }

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

    /* RDWR用于串口配置；本程序没有向fd调用write。 */
    fd = open(argv[1],
              O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (ioctl(fd, TIOCEXCL) == -1) {
        perror("TIOCEXCL");
        goto cleanup;
    }

    exclusive = true;

    if (configure_serial(fd) == -1) {
        perror("configure serial");
        goto cleanup;
    }

    fprintf(stderr,
            "READY: %s 115200/8N1 flow=none DTR=0 RTS=0 HUPCL=0\n"
            "Receive for 30 seconds; Ctrl+C stops. No BMS command sent.\n",
            argv[1]);

    if (receive_lines(fd, &buffer, stdout_line_sink, NULL) == -1) {
        perror("receive/output");
    } else if (buffer.lines == 0U) {
        fprintf(stderr, "No complete text lines received\n");
    } else {
        status = EXIT_SUCCESS;
    }

cleanup:
    fprintf(stderr,
            "lines=%" PRIu64 " discarded=%" PRIu64
            " tail_bytes=%zu discarding=%d\n",
            buffer.lines,
            buffer.discarded,
            buffer.used,
            buffer.discarding ? 1 : 0);

    /* 不恢复打开前的旧termios，也不重新拉起DTR/RTS。 */
    if (exclusive && ioctl(fd, TIOCNXCL) == -1) {
        perror("TIOCNXCL");
        status = EXIT_FAILURE;
    }

    if (close(fd) == -1) {
        perror("close");
        status = EXIT_FAILURE;
    }

    return status;
}
#endif