#define _POSIX_C_SOURCE 200809L

#include "../../gateway/bms_control_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] line %d: %s\n", __LINE__, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

/* 包装只在本测试目标启用；普通生产目标不启用--wrap。 */
static int watched_uart = -1;
static int watched_socket = -1;
static int next_write_error;
static int next_send_error;
static int zero_write;
static size_t send_limit = SIZE_MAX;
static size_t write_calls;
static size_t before_write_length;
static bms_control_tx_t *observed_tx;
static int64_t fake_ms;

ssize_t __real_write(int fd, const void *data, size_t length);
ssize_t __real_send(int fd, const void *data, size_t length, int flags);
int __real_clock_gettime(clockid_t clock_id, struct timespec *value);

ssize_t __wrap_write(int fd, const void *data, size_t length)
{
    if (fd == watched_uart) {
        ++write_calls;
        CHECK(length == 1U);
        CHECK(observed_tx->length == before_write_length);
        if (next_write_error != 0) {
            errno = next_write_error;
            next_write_error = 0;
            return -1;
        }
        if (zero_write) {
            zero_write = 0;
            return 0;
        }
    }
    return __real_write(fd, data, length);
}

ssize_t __wrap_send(int fd, const void *data, size_t length, int flags)
{
    if (fd == watched_socket) {
        CHECK((flags & (MSG_DONTWAIT | MSG_NOSIGNAL)) ==
              (MSG_DONTWAIT | MSG_NOSIGNAL));
        if (next_send_error != 0) {
            errno = next_send_error;
            next_send_error = 0;
            return -1;
        }
        if (length > send_limit) {
            length = send_limit;
        }
    }
    return __real_send(fd, data, length, flags);
}

int __wrap_clock_gettime(clockid_t clock_id, struct timespec *value)
{
    if (clock_id == CLOCK_MONOTONIC) {
        value->tv_sec = (time_t)(fake_ms / 1000);
        value->tv_nsec = (long)(fake_ms % 1000) * 1000000L;
        return 0;
    }
    return __real_clock_gettime(clock_id, value);
}

static void make_socket(int channel[2])
{
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, channel) == 0);
    watched_socket = channel[0];
    next_send_error = 0;
    send_limit = SIZE_MAX;
}

static void close_pair(int channel[2])
{
    CHECK(close(channel[0]) == 0);
    CHECK(close(channel[1]) == 0);
}

/* 所有循环均有次数上限，接收也不阻塞。 */
static void collect(bms_control_tx_t *tx, int channel[2],
                    const char *expected)
{
    unsigned char actual[8192];
    size_t length = strlen(expected);
    CHECK(length < sizeof(actual));
    for (size_t attempt = 0U; tx->length != 0U && attempt < 8192U;
         ++attempt) {
        CHECK(bms_control_tx_flush(tx, channel[0]) == 1);
    }
    CHECK(tx->length == 0U && tx->offset == 0U);

    size_t received = 0U;
    while (received < length) {
        ssize_t count = recv(channel[1], actual + received,
                             length - received, MSG_DONTWAIT);
        CHECK(count > 0);
        received += (size_t)count;
    }
    CHECK(memcmp(actual, expected, length) == 0);
    CHECK(recv(channel[1], actual, 1U, MSG_DONTWAIT) == -1);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK); /* 没有额外NUL。 */
}

static void take_output(bms_control_tx_t *tx, const char *expected)
{
    int channel[2];
    make_socket(channel);
    collect(tx, channel, expected);
    close_pair(channel);
}

static void fill_tx(bms_control_tx_t *tx)
{
    char body[1023];
    memset(body, 'X', sizeof(body));
    for (unsigned index = 0U; index < 4U; ++index) {
        CHECK(bms_control_tx_enqueue(tx, body, sizeof(body)) == 0);
    }
    CHECK(tx->length == BMS_CONTROL_TX_CAPACITY);
}

static void setup_serial(bms_control_session_t *session,
                         bms_control_tx_t *tx, int channel[2])
{
    CHECK(pipe(channel) == 0);
    for (unsigned index = 0U; index < 2U; ++index) {
        int flags = fcntl(channel[index], F_GETFL);
        CHECK(flags != -1);
        CHECK(fcntl(channel[index], F_SETFL, flags | O_NONBLOCK) == 0);
    }
    bms_control_session_init(session, true);
    bms_control_tx_init(tx);
    watched_uart = channel[1];
    observed_tx = tx;
    next_write_error = 0;
    zero_write = 0;
    write_calls = 0U;
    fake_ms = 0;
}

static void feed(bms_control_session_t *session,
                 bms_control_tx_t *tx, const char *text)
{
    CHECK(bms_control_session_feed(
        session, (const unsigned char *)text, strlen(text), fake_ms,
        bms_control_tx_error, tx) == 0);
}

static int step(bms_control_session_t *session,
                bms_control_tx_t *tx, int serial_fd)
{
    before_write_length = tx->length;
    return bms_control_uart_step(session, tx, serial_fd);
}

static void expect_empty(int fd)
{
    unsigned char byte;
    CHECK(read(fd, &byte, 1U) == -1);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
}

static void test_output_queue(void)
{
    bms_control_tx_t tx;
    int channel[2];
    bms_control_tx_init(&tx);
    const char nul_body[] = {'A', '\0', 'B'};
    char oversized[BMS_TCP_MAX_LINE + 1U];
    memset(oversized, 'X', sizeof(oversized));
    CHECK(bms_control_tx_enqueue(&tx, nul_body, sizeof(nul_body)) == -1);
    CHECK(errno == EINVAL && tx.length == 0U);
    CHECK(bms_control_tx_enqueue(&tx, "A\nB", 3U) == -1);
    CHECK(errno == EINVAL && tx.length == 0U);
    CHECK(bms_control_tx_enqueue(&tx, oversized, sizeof(oversized)) == -1);
    CHECK(errno == EMSGSIZE && tx.length == 0U);
    make_socket(channel);
    CHECK(bms_control_tx_enqueue(&tx, "BMS state=1", 11U) == 0);
    CHECK(bms_control_tx_error(7U, "CANCELLED", &tx) == 0);

    size_t original = tx.length;
    next_send_error = EAGAIN;
    CHECK(bms_control_tx_flush(&tx, channel[0]) == 0);
    CHECK(tx.length == original && tx.offset == 0U);
    next_send_error = EINTR;
    CHECK(bms_control_tx_flush(&tx, channel[0]) == 0);
    CHECK(tx.length == original && tx.offset == 0U);
    send_limit = 3U;
    collect(&tx, channel, "BMS state=1\nERR 7 CANCELLED\n");
    close_pair(channel);

    bms_control_tx_init(&tx);
    make_socket(channel);
    fill_tx(&tx);
    CHECK(bms_control_tx_enqueue(&tx, "X", 1U) == -1 && errno == ENOBUFS);
    CHECK(tx.length == BMS_CONTROL_TX_CAPACITY);
    send_limit = 1024U;
    CHECK(bms_control_tx_flush(&tx, channel[0]) == 1);
    CHECK(tx.offset == 1024U && tx.length == 3072U);
    char body[1023];
    memset(body, 'Y', sizeof(body));
    CHECK(bms_control_tx_enqueue(&tx, body, sizeof(body)) == 0);
    CHECK(tx.offset == 0U); /* 空间整理不能破坏部分发送后的后缀。 */

    char expected[5121];
    memset(expected, 'X', 4096U);
    for (unsigned index = 0U; index < 4U; ++index) {
        expected[(index + 1U) * 1024U - 1U] = '\n';
    }
    memset(expected + 4096U, 'Y', 1023U);
    expected[5119] = '\n';
    expected[5120] = '\0';
    send_limit = SIZE_MAX;
    collect(&tx, channel, expected);
    close_pair(channel);
    puts("[PASS] bounded LF output, short send, EINTR and compaction");
}

static void test_real_socket_backpressure(void)
{
    bms_control_tx_t tx;
    int channel[2];
    make_socket(channel);
    int capacity = 4096;
    CHECK(setsockopt(channel[0], SOL_SOCKET, SO_SNDBUF,
                     &capacity, sizeof(capacity)) == 0);
    char padding[4096] = {0};
    int full = 0;
    for (unsigned attempt = 0U; attempt < 128U; ++attempt) {
        ssize_t count = __real_send(channel[0], padding, sizeof(padding),
                                    MSG_DONTWAIT | MSG_NOSIGNAL);
        if (count == -1) {
            CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
            full = 1;
            break;
        }
        CHECK(count > 0);
    }
    CHECK(full);
    bms_control_tx_init(&tx);
    CHECK(bms_control_tx_enqueue(&tx, "BMS", 3U) == 0);
    CHECK(bms_control_tx_flush(&tx, channel[0]) == 0);
    CHECK(tx.length == 4U && tx.offset == 0U);
    close_pair(channel);

    make_socket(channel);
    CHECK(close(channel[1]) == 0);
    CHECK(bms_control_tx_flush(&tx, channel[0]) == -1);
    CHECK(errno == EPIPE || errno == ECONNRESET);
    CHECK(close(channel[0]) == 0);
    puts("[PASS] real Socket backpressure and closed peer without SIGPIPE");
}

static void test_mapping_and_ack(void)
{
    bms_control_session_t session;
    bms_control_tx_t tx;
    int channel[2];
    setup_serial(&session, &tx, channel);
    const char *commands[] = {
        "CMD 1 STOP\n", "CMD 2 START\n", "CMD 3 CLEAR\n"
    };
    const unsigned char expected[] = {'T', 'S', 'C'};
    for (unsigned index = 0U; index < 3U; ++index) {
        feed(&session, &tx, commands[index]);
        CHECK(tx.length == 0U);
        CHECK(step(&session, &tx, channel[1]) == 1);
        CHECK(write_calls == index + 1U && session.count == 0U);
        unsigned char byte;
        CHECK(read(channel[0], &byte, 1U) == 1 && byte == expected[index]);
        expect_empty(channel[0]);
        char reply[64];
        CHECK(snprintf(reply, sizeof(reply),
              "ACK %u UART_ACCEPTED\n", index + 1U) > 0);
        take_output(&tx, reply);
    }
    feed(&session, &tx, "CMD 3 CLEAR\n");
    CHECK(step(&session, &tx, channel[1]) == 0 && write_calls == 3U);
    take_output(&tx, "ERR 3 STALE_ID\n");
    feed(&session, &tx, "CMD 4294967295 STOP\n");
    CHECK(step(&session, &tx, channel[1]) == 1 && write_calls == 4U);
    unsigned char byte;
    CHECK(read(channel[0], &byte, 1U) == 1 && byte == 'T');
    take_output(&tx, "ACK 4294967295 UART_ACCEPTED\n");
    close_pair(channel);
    puts("[PASS] exact T/S/C, ACK after write acceptance and no duplicate write");
}

static void test_retry_stop_and_expiry(void)
{
    bms_control_session_t session;
    bms_control_tx_t tx;
    int channel[2];
    setup_serial(&session, &tx, channel);
    feed(&session, &tx, "CMD 1 START\n");
    next_write_error = EINTR;
    fake_ms = 100;
    CHECK(step(&session, &tx, channel[1]) == 0);
    CHECK(session.count == 1U && tx.length == 0U);
    CHECK(session.pending[0].queued_ms == 0);
    expect_empty(channel[0]);
    fake_ms = 200;
    feed(&session, &tx, "CMD 2 STOP\n");
    CHECK(step(&session, &tx, channel[1]) == 1);
    unsigned char byte;
    CHECK(read(channel[0], &byte, 1U) == 1 && byte == 'T');
    expect_empty(channel[0]);
    take_output(&tx, "ERR 1 CANCELLED\nACK 2 UART_ACCEPTED\n");
    CHECK(step(&session, &tx, channel[1]) == 0 && write_calls == 2U);
    close_pair(channel);

    setup_serial(&session, &tx, channel);
    char padding[4096] = {0};
    int full = 0;
    for (unsigned attempt = 0U; attempt < 128U; ++attempt) {
        ssize_t count = __real_write(channel[1], padding, sizeof(padding));
        if (count == -1) {
            CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
            full = 1;
            break;
        }
        CHECK(count > 0);
    }
    CHECK(full);
    feed(&session, &tx, "CMD 1 START\n");
    fake_ms = 999;
    CHECK(step(&session, &tx, channel[1]) == 0);
    CHECK(write_calls == 1U && session.count == 1U && tx.length == 0U);
    fake_ms = 1000;
    CHECK(step(&session, &tx, channel[1]) == 0);
    CHECK(write_calls == 1U && session.count == 0U);
    take_output(&tx, "ERR 1 EXPIRED\n");
    close_pair(channel);
    puts("[PASS] EINTR, STOP cancellation, real pipe EAGAIN and expiry");
}

static void test_fail_closed(void)
{
    bms_control_session_t session;
    bms_control_tx_t tx;
    int channel[2];
    for (unsigned mode = 0U; mode < 2U; ++mode) {
        setup_serial(&session, &tx, channel);
        feed(&session, &tx, "CMD 1 START\nCMD 2 CLEAR\n");
        next_write_error = mode == 0U ? EIO : 0;
        zero_write = mode == 1U;
        CHECK(step(&session, &tx, channel[1]) == -1 && errno == EIO);
        CHECK(!session.connected && session.count == 0U && write_calls == 1U);
        expect_empty(channel[0]);
        take_output(&tx, "ERR 1 SERIAL_WRITE\n");
        CHECK(step(&session, &tx, channel[1]) == -1 && errno == ENOTCONN);
        CHECK(write_calls == 1U);
        close_pair(channel);
    }

    setup_serial(&session, &tx, channel);
    feed(&session, &tx, "CMD 1 START\n");
    fill_tx(&tx);
    CHECK(step(&session, &tx, channel[1]) == -1 && errno == ENOBUFS);
    CHECK(!session.connected && session.count == 0U && write_calls == 0U);
    expect_empty(channel[0]);
    close_pair(channel);

    setup_serial(&session, &tx, channel);
    feed(&session, &tx, "CMD 1 START\n");
    CHECK(fcntl(channel[1], F_SETFL, 0) == 0); /* 故意提供不合格阻塞fd。 */
    CHECK(step(&session, &tx, channel[1]) == -1 && errno == EINVAL);
    CHECK(write_calls == 0U && !session.connected);
    take_output(&tx, "ERR 1 SERIAL_WRITE\n");
    close_pair(channel);

    setup_serial(&session, &tx, channel);
    feed(&session, &tx, "CMD 1 START\n");
    bms_control_session_disconnect(&session);
    CHECK(step(&session, &tx, channel[1]) == -1 && errno == ENOTCONN);
    CHECK(write_calls == 0U);
    expect_empty(channel[0]);
    close_pair(channel);
    puts("[PASS] write failure, reply reserve, blocking-fd refusal and disconnect");
}

int main(void)
{
    test_output_queue();
    test_real_socket_backpressure();
    test_mapping_and_ack();
    test_retry_stop_and_expiry();
    test_fail_closed();
    return EXIT_SUCCESS;
}