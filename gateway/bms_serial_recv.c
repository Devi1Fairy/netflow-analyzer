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

#define LINE_CAPACITY 1024U
#define RUN_SECONDS 30.0

typedef struct {
    char data[LINE_CAPACITY + 1U];
    size_t used;
    bool discarding;
    uint64_t lines;
    uint64_t discarded;
} line_buffer_t;

static volatile sig_atomic_t stop_requested = 0;

/* 信号中只发出请求；关闭设备和输出统计由main完成。 */
static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

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

/* 输入块只借用；输出完整文本行。超长/控制字符行丢到LF后恢复。 */
static int feed_bytes(line_buffer_t *buffer,
                      const unsigned char *data,
                      size_t length)
{
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
                    if (puts(buffer->data) == EOF) {
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

static int receive_lines(int fd, line_buffer_t *buffer)
{
    double started;

    if (monotonic_seconds(&started) == -1) {
        return -1;
    }

    while (!stop_requested) {
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

        if (now - started >= RUN_SECONDS) {
            return 0;
        }

        ready = poll(&watched, 1, 500);

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
                if (feed_bytes(buffer, chunk, (size_t)count) == -1) {
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

    if (receive_lines(fd, &buffer) == -1) {
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