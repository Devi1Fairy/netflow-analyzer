#define _POSIX_C_SOURCE 200809L

#include "bms_control_run.h"
#include "bms_control_io.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int monotonic_ms(int64_t *value)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return -1;
    }
    *value = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    return 0;
}

static int telemetry_sink(const char *line, size_t length, void *context)
{
    return bms_control_tx_enqueue(context, line, length);
}

/* 出错时仅有界排出已有回执，不再读命令或写UART。 */
static void drain_output(bms_control_tx_t *tx, int client_fd,
                          int64_t deadline,
                          const volatile sig_atomic_t *stop)
{
    int64_t now;
    if (monotonic_ms(&now) == -1) {
        return;
    }
    if (deadline > now + 100) {
        deadline = now + 100;
    }
    while (tx->length > 0U && !*stop) {
        if (monotonic_ms(&now) == -1 || now >= deadline) {
            break;
        }
        struct pollfd watched = {.fd = client_fd, .events = POLLOUT};
        int ready = poll(&watched, 1, (int)(deadline - now));
        if (ready <= 0 ||
            (watched.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 ||
            bms_control_tx_flush(tx, client_fd) == -1) {
            break;
        }
    }
}

int bms_control_run(int serial_fd, int client_fd, int64_t deadline_ms,
                    const volatile sig_atomic_t *stop,
                    bms_serial_stats_t *stats)
{
    if (stats != NULL) {
        *stats = (bms_serial_stats_t){0};
    }
    if (stop == NULL || stats == NULL || deadline_ms <= 0) {
        errno = EINVAL;
        return -1;
    }
    int serial_flags = fcntl(serial_fd, F_GETFL);
    int client_flags = fcntl(client_fd, F_GETFL);
    if (serial_flags == -1 || client_flags == -1) {
        errno = EBADF;
        return -1;
    }
    if ((serial_flags & O_NONBLOCK) == 0 ||
        (client_flags & O_NONBLOCK) == 0) {
        errno = EINVAL;
        return -1;
    }

    bms_serial_framer_t framer;
    bms_control_session_t session;
    bms_control_tx_t tx;
    bms_serial_framer_init(&framer);
    bms_control_session_init(&session, true);
    bms_control_tx_init(&tx);

    int result = -1;
    bool peer_gone = false;
    if (bms_control_tx_enqueue(
        &tx, "GW 2 CONTROL", sizeof("GW 2 CONTROL") - 1U) == -1) {
        goto finished;
    }

    while (!*stop) {
        int64_t now;
        if (monotonic_ms(&now) == -1) {
            goto finished;
        }
        if (now >= deadline_ms) {
            result = 0;
            break;
        }

        bms_control_pending_t head;
        if (bms_control_session_peek(
            &session, now, bms_control_tx_error, &tx, &head) == -1) {
            goto finished;
        }

        int wait_ms = deadline_ms - now < 200 ?
                      (int)(deadline_ms - now) : 200;
        for (size_t index = 0U; index < session.count; ++index) {
            int left = (int)(BMS_CONTROL_TTL_MS -
                       (now - session.pending[index].queued_ms));
            if (left < wait_ms) {
                wait_ms = left;
            }
        }

        struct pollfd watched[2] = {
            {.fd = client_fd,
             .events = POLLIN | (tx.length > 0U ? POLLOUT : 0)},
            {.fd = serial_fd,
             .events = POLLIN | (session.count > 0U ? POLLOUT : 0)}
        };
        int ready = poll(watched, 2, wait_ms);
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            goto finished;
        }
        if (ready == 0) {
            continue;
        }

        /* 已观察到客户端结束，优先丢弃待写，不能再向UART写。 */
        if ((watched[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            peer_gone = true;
            result = (watched[0].revents & POLLHUP) != 0 ? 0 : -1;
            errno = (watched[0].revents & POLLNVAL) != 0 ?
                    EBADF : ECONNRESET;
            goto finished;
        }
        if ((watched[1].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            errno = ENODEV;
            goto finished;
        }

        if ((watched[0].revents & POLLIN) != 0) {
            /* 限制每轮输入量；没有读完时，下面的PEEK会让出UART写入。 */
            for (unsigned attempt = 0U; attempt < 8U; ++attempt) {
                unsigned char chunk[256];
                ssize_t count = recv(client_fd, chunk, sizeof(chunk),
                                     MSG_DONTWAIT);
                if (count == 0) {
                    peer_gone = true;
                    result = 0;
                    goto finished;
                }
                if (count < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == EINTR) {
                        goto next_iteration;
                    }
                    peer_gone = true;
                    goto finished;
                }
                if (monotonic_ms(&now) == -1) {
                    goto finished;
                }
                if (*stop || now >= deadline_ms) {
                    result = 0;
                    goto finished;
                }
                if (bms_control_session_feed(
                    &session, chunk, (size_t)count, now,
                    bms_control_tx_error, &tx) == -1) {
                    goto finished;
                }
            }
        }

        if ((watched[1].revents & POLLIN) != 0) {
            unsigned char chunk[256];
            ssize_t count = read(serial_fd, chunk, sizeof(chunk));
            if (count > 0) {
                if (bms_serial_framer_feed(
                    &framer, chunk, (size_t)count,
                    telemetry_sink, &tx) == -1) {
                    goto finished;
                }
            } else if (count == 0) {
                errno = ENODEV;
                goto finished;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
                       errno != EINTR) {
                goto finished;
            }
        }

        if (monotonic_ms(&now) == -1) {
            goto finished;
        }
        if (*stop || now >= deadline_ms) {
            result = 0;
            goto finished;
        }

        if ((watched[1].revents & POLLOUT) != 0 && session.count > 0U) {
            unsigned char probe;
            ssize_t count = recv(client_fd, &probe, 1U,
                                 MSG_PEEK | MSG_DONTWAIT);
            if (count == 0) {
                peer_gone = true;
                result = 0;
                goto finished;
            }
            if (count > 0 || (count < 0 && errno == EINTR)) {
                goto next_iteration; /* 新输入优先，避免越过尚未处理的STOP。 */
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                peer_gone = true;
                goto finished;
            }
            if (bms_control_uart_step(&session, &tx, serial_fd) == -1) {
                goto finished;
            }
        }
        if ((watched[0].revents & POLLOUT) != 0 &&
            bms_control_tx_flush(&tx, client_fd) == -1) {
            peer_gone = true;
            goto finished;
        }

next_iteration:
        ;
    }
    result = 0;

finished:
    {
        int saved_errno = errno;
        bms_control_session_disconnect(&session);
        if (!peer_gone) {
            drain_output(&tx, client_fd, deadline_ms, stop);
        }
        bms_serial_framer_stats(&framer, stats);
        errno = saved_errno;
    }
    return result;
}