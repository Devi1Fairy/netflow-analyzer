#define _POSIX_C_SOURCE 200809L

#include "bms_control_io.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

void bms_control_tx_init(bms_control_tx_t *tx)
{
    *tx = (bms_control_tx_t){0};
}

int bms_control_tx_enqueue(bms_control_tx_t *tx,
                           const char *body, size_t length)
{
    if (tx == NULL || body == NULL || length == 0U) {
        errno = EINVAL;
        return -1;
    }
    if (length > BMS_TCP_MAX_LINE) {
        errno = EMSGSIZE;
        return -1;
    }
    if (memchr(body, '\0', length) != NULL ||
        memchr(body, '\n', length) != NULL) {
        errno = EINVAL;
        return -1;
    }
    if (length + 1U > BMS_CONTROL_TX_CAPACITY - tx->length) {
        errno = ENOBUFS;
        return -1;
    }

    if (tx->offset + tx->length + length + 1U >
        BMS_CONTROL_TX_CAPACITY) {
        memmove(tx->data, tx->data + tx->offset, tx->length);
        tx->offset = 0U;
    }
    memcpy(tx->data + tx->offset + tx->length, body, length);
    tx->data[tx->offset + tx->length + length] = '\n';
    tx->length += length + 1U;
    return 0;
}

int bms_control_tx_flush(bms_control_tx_t *tx, int socket_fd)
{
    if (tx == NULL || socket_fd < 0) {
        errno = EINVAL;
        return -1;
    }
    if (tx->length == 0U) {
        return 0;
    }

    ssize_t count = send(socket_fd, tx->data + tx->offset, tx->length,
                         MSG_DONTWAIT | MSG_NOSIGNAL);
    if (count > 0) {
        tx->offset += (size_t)count;
        tx->length -= (size_t)count;
        if (tx->length == 0U) {
            tx->offset = 0U;
        }
        return 1;
    }
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                      errno == EINTR)) {
        return 0;
    }
    if (count == 0) {
        errno = EPIPE;
    }
    return -1;
}

static int queue_reply(bms_control_tx_t *tx, const char *kind,
                       uint32_t id, const char *word)
{
    char body[BMS_CONTROL_REPLY_RESERVE];
    int length = snprintf(body, sizeof(body),
                          "%s %" PRIu32 " %s", kind, id, word);
    if (length < 0 || (size_t)length >= sizeof(body)) {
        errno = EOVERFLOW;
        return -1;
    }
    return bms_control_tx_enqueue(tx, body, (size_t)length);
}

int bms_control_tx_error(uint32_t id, const char *reason, void *context)
{
    static const char *const allowed[] = {
        "BAD_REQUEST", "BAD_COMMAND", "STALE_ID", "QUEUE_FULL",
        "CANCELLED", "EXPIRED", "NOT_READY", "SERIAL_WRITE"
    };

    if (context != NULL && reason != NULL) {
        for (size_t index = 0U;
             index < sizeof(allowed) / sizeof(allowed[0]); ++index) {
            if (strcmp(reason, allowed[index]) == 0) {
                return queue_reply(context, "ERR", id, reason);
            }
        }
    }
    errno = EINVAL;
    return -1;
}

static int abort_session(bms_control_session_t *session, int error)
{
    bms_control_session_disconnect(session);
    errno = error;
    return -1;
}

int bms_control_uart_step(bms_control_session_t *session,
                          bms_control_tx_t *tx, int serial_fd)
{
    if (session == NULL || tx == NULL) {
        if (session != NULL) {
            bms_control_session_disconnect(session);
        }
        errno = EINVAL;
        return -1;
    }
    if (!session->connected) {
        errno = ENOTCONN;
        return -1;
    }

    /* 只检查，不替调用者修改描述符标志或串口配置。 */
    int flags = fcntl(serial_fd, F_GETFL);
    int serial_error = flags == -1 ? errno :
                       ((flags & O_NONBLOCK) == 0 ? EINVAL : 0);

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return abort_session(session, errno);
    }
    int64_t now_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    bms_control_pending_t pending;
    int available = bms_control_session_peek(
        session, now_ms, bms_control_tx_error, tx, &pending);
    if (available == -1) {
        return abort_session(session, errno);
    }
    if (available == 0) {
        return 0;
    }

    /* 为ACK或SERIAL_WRITE预留空间，不能先写UART再发现放不下回执。 */
    if (BMS_CONTROL_TX_CAPACITY - tx->length <
        BMS_CONTROL_REPLY_RESERVE) {
        return abort_session(session, ENOBUFS);
    }

    unsigned char byte = (unsigned char)pending.request.action;
    if (byte != 'T' && byte != 'S' && byte != 'C') {
        return abort_session(session, EINVAL);
    }

    ssize_t count = -1;
    if (serial_error == 0) {
        count = write(serial_fd, &byte, 1U);
        if (count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                            errno == EINTR)) {
            return 0; /* 不出队、不ACK，也不刷新queued_ms。 */
        }
        serial_error = count == 1 ? 0 : (count == -1 ? errno : EIO);
    }

    if (serial_error != 0) {
        (void)bms_control_tx_error(
            pending.request.id, "SERIAL_WRITE", tx);
        return abort_session(session, serial_error);
    }

    /*
     * 此时驱动已经接受字节；即使后续内部错误，也不能再次写该命令。
     * 单线程且已预留容量，正常情况下出队和ACK入缓存不会失败。
     */
    if (bms_control_session_written(session, pending.request.id) == -1 ||
        queue_reply(tx, "ACK", pending.request.id, "UART_ACCEPTED") == -1) {
        return abort_session(session, errno);
    }
    return 1;
}