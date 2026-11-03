#include "../../gateway/bms_control_session.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "[FAIL] line %d: %s\n", \
                    __LINE__, #condition); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

typedef struct {
    uint32_t ids[32];
    const char *reasons[32];
    size_t count;
    size_t limit;
} errors_t;

static int save_error(uint32_t id, const char *reason, void *context)
{
    errors_t *errors = context;
    if (errors->count == errors->limit) {
        return -1;
    }
    errors->ids[errors->count] = id;
    errors->reasons[errors->count++] = reason;
    return 0;
}

static void feed(bms_control_session_t *session, errors_t *errors,
                 const char *text, int64_t now_ms)
{
    CHECK(bms_control_session_feed(session, (const unsigned char *)text,
          strlen(text), now_ms, save_error, errors) == 0);
}

static void expect_error(const errors_t *errors, size_t index,
                         uint32_t id, const char *reason)
{
    CHECK(index < errors->count);
    CHECK(errors->ids[index] == id);
    CHECK(strcmp(errors->reasons[index], reason) == 0);
}

static void fill_queue(bms_control_session_t *session, errors_t *errors,
                       const char *action)
{
    for (unsigned id = 1U; id <= BMS_CONTROL_QUEUE_CAPACITY; ++id) {
        char line[32];
        int length = snprintf(line, sizeof(line), "CMD %u %s\n", id, action);
        CHECK(length > 0 && (size_t)length < sizeof(line));
        feed(session, errors, line, 0);
    }
}

static void test_framing_and_ids(void)
{
    bms_control_session_t session;
    errors_t errors = {.limit = 32U};
    bms_control_pending_t pending;
    bms_control_session_init(&session, true);

    feed(&session, &errors, "CMD 1 STA", 0);
    CHECK(session.count == 0U && session.last_id == 0U);
    feed(&session, &errors, "RT\nCMD 2 CLEAR\n", 1);
    CHECK(session.count == 2U && errors.count == 0U);

    CHECK(bms_control_session_peek(
        &session, 1, save_error, &errors, &pending) == 1);
    CHECK(pending.request.id == 1U);
    CHECK((unsigned char)pending.request.action == 'S');
    CHECK(session.count == 2U); /* peek不出队，也不产生ACK。 */
    CHECK(bms_control_session_written(&session, 2U) == -1);
    CHECK(errno == EINVAL && session.count == 2U);

    /* 纯策略测试模拟“驱动已接受”；这里并没有真实UART或ACK。 */
    CHECK(bms_control_session_written(&session, 1U) == 0);
    CHECK(bms_control_session_peek(
        &session, 2, save_error, &errors, &pending) == 1);
    CHECK((unsigned char)pending.request.action == 'C');
    CHECK(bms_control_session_written(&session, 2U) == 0);
    CHECK(errors.count == 0U);

    feed(&session, &errors, "CMD 0002 CLEAR\nCMD 1 START\nCMD 3 P\n", 3);
    expect_error(&errors, 0U, 2U, "STALE_ID");
    expect_error(&errors, 1U, 1U, "STALE_ID");
    expect_error(&errors, 2U, 3U, "BAD_COMMAND");
    CHECK(session.last_id == 2U); /* 非白名单动作不消耗编号。 */

    feed(&session, &errors, "CMD 3 STOP\n", 3);
    CHECK(bms_control_session_peek(
        &session, 3, save_error, &errors, &pending) == 1);
    CHECK((unsigned char)pending.request.action == 'T');
    CHECK(bms_control_session_written(&session, 3U) == 0);
    feed(&session, &errors, "CMD 4294967295 STOP\nCMD 1 STOP\n", 4);
    CHECK(session.last_id == UINT32_MAX);
    expect_error(&errors, 3U, 1U, "STALE_ID");
    puts("[PASS] split/coalesced input, whitelist and session IDs");
}

static void test_bad_bytes_and_resync(void)
{
    bms_control_session_t session;
    errors_t errors = {.limit = 32U};
    bms_control_session_init(&session, true);

    const unsigned char invalid[] = {0, '\r', '\t', 0x1b, 0x7f, 0x80};
    for (size_t index = 0U; index < sizeof(invalid); ++index) {
        unsigned char line[] = "CMD 1 STXP\n";
        line[8] = invalid[index];
        CHECK(bms_control_session_feed(
            &session, line, sizeof(line) - 1U, 0,
            save_error, &errors) == 0);
        expect_error(&errors, index, 1U, "BAD_REQUEST");
    }
    CHECK(session.last_id == 0U && session.count == 0U);

    unsigned char oversized[BMS_CONTROL_MAX_BODY + 1U];
    memset(oversized, 'X', sizeof(oversized));
    CHECK(bms_control_session_feed(
        &session, oversized, sizeof(oversized), 0,
        save_error, &errors) == 0);
    CHECK(session.discarding && errors.count == sizeof(invalid));
    feed(&session, &errors, "\nCMD 1 START\n", 0);
    expect_error(&errors, sizeof(invalid), 0U, "BAD_REQUEST");
    CHECK(session.count == 1U && !session.discarding);

    /* 恰好64字节的合法正文，末尾LF不计入正文长度。 */
    unsigned char exact[BMS_CONTROL_MAX_BODY + 1U];
    memset(exact, '0', sizeof(exact));
    memcpy(exact, "CMD ", 4U);
    exact[BMS_CONTROL_MAX_BODY - 7U] = '2';
    memcpy(exact + BMS_CONTROL_MAX_BODY - 6U, " CLEAR", 6U);
    exact[BMS_CONTROL_MAX_BODY] = '\n';
    CHECK(bms_control_session_feed(
        &session, exact, sizeof(exact), 0, save_error, &errors) == 0);
    CHECK(session.count == 2U && session.last_id == 2U);

    feed(&session, &errors, "\nCMD 3 STOP\r\nCMD 3 CLEAR\n", 0);
    expect_error(&errors, sizeof(invalid) + 1U, 0U, "BAD_REQUEST");
    expect_error(&errors, sizeof(invalid) + 2U, 3U, "BAD_REQUEST");
    CHECK(session.count == 3U && session.last_id == 3U);
    puts("[PASS] invalid bytes, 64/65-byte boundary and LF resync");
}

static void test_queue_and_stop(void)
{
    bms_control_session_t session;
    errors_t errors = {.limit = 32U};
    bms_control_session_init(&session, true);
    fill_queue(&session, &errors, "START");

    feed(&session, &errors, "CMD 9 CLEAR\nCMD 9 START\n", 0);
    expect_error(&errors, 0U, 9U, "QUEUE_FULL");
    expect_error(&errors, 1U, 9U, "STALE_ID");
    CHECK(session.count == 8U && session.last_id == 9U);

    CHECK(bms_control_session_written(&session, 1U) == 0);
    feed(&session, &errors, "CMD 9 START\nCMD 10 CLEAR\nCMD 11 STOP\n", 0);
    expect_error(&errors, 2U, 9U, "STALE_ID");
    for (size_t index = 0U; index < 7U; ++index) {
        expect_error(&errors, 3U + index, (uint32_t)index + 2U, "CANCELLED");
    }
    expect_error(&errors, 10U, 10U, "CANCELLED");
    CHECK(session.count == 1U && session.pending[0].request.id == 11U);

    feed(&session, &errors,
         "CMD 12 START\nCMD 13 CLEAR\nCMD 11 STOP\n", 0);
    CHECK(session.count == 3U); /* 旧STOP不能取消队列。 */
    expect_error(&errors, 11U, 11U, "STALE_ID");

    feed(&session, &errors, "CMD 14 STOP\nCMD 15 START\n", 0);
    expect_error(&errors, 12U, 12U, "CANCELLED");
    expect_error(&errors, 13U, 13U, "CANCELLED");
    CHECK(session.count == 3U);
    CHECK(session.pending[0].request.id == 11U);
    CHECK(session.pending[1].request.id == 14U);
    CHECK(session.pending[2].request.id == 15U);

    bms_control_session_init(&session, true);
    errors = (errors_t){.limit = 32U};
    fill_queue(&session, &errors, "STOP");
    feed(&session, &errors, "CMD 9 STOP\n", 0);
    expect_error(&errors, 0U, 9U, "QUEUE_FULL");
    CHECK(session.count == 8U && session.last_id == 9U);
    puts("[PASS] bounded queue, consumed IDs and STOP cancellation/priority");
}

static void test_readiness_and_expiry(void)
{
    bms_control_session_t session;
    errors_t errors = {.limit = 32U};
    bms_control_pending_t pending = {.request = {.id = 99U}};
    bms_control_session_init(&session, false);
    feed(&session, &errors, "CMD 1 START\nCMD 1 START\nCMD 2 CLEAR\n", 0);
    expect_error(&errors, 0U, 1U, "NOT_READY");
    expect_error(&errors, 1U, 1U, "STALE_ID");
    expect_error(&errors, 2U, 2U, "NOT_READY");
    CHECK(session.count == 0U && session.last_id == 2U);

    bms_control_session_init(&session, true);
    errors = (errors_t){.limit = 32U};
    feed(&session, &errors, "CMD 1 START\n", 100);
    feed(&session, &errors, "CMD 2 CLEAR\n", 200);
    CHECK(bms_control_session_peek(
        &session, 1099, save_error, &errors, &pending) == 1);
    CHECK(pending.request.id == 1U && pending.queued_ms == 100);
    CHECK(bms_control_session_peek(
        &session, 1100, save_error, &errors, &pending) == 1);
    expect_error(&errors, 0U, 1U, "EXPIRED");
    CHECK(pending.request.id == 2U);

    CHECK(bms_control_session_peek(
        &session, 1199, save_error, &errors, &pending) == 1);
    CHECK(bms_control_session_peek(
        &session, 1200, save_error, &errors, &pending) == 0);
    expect_error(&errors, 1U, 2U, "EXPIRED");
    CHECK(pending.request.id == 2U); /* 空队列不更新out。 */

    feed(&session, &errors, "CMD 3 STOP\n", INT64_MAX - 500);
    CHECK(bms_control_session_peek(
        &session, INT64_MAX, save_error, &errors, &pending) == 1);
    CHECK(bms_control_session_peek(
        &session, INT64_MAX - 1, save_error, &errors, &pending) == -1);
    CHECK(errno == EINVAL && !session.connected && session.count == 0U);
    puts("[PASS] readiness, exact 1000ms expiry and monotonic clock guard");
}

static void test_disconnect_and_output_refusal(void)
{
    bms_control_session_t session;
    errors_t errors = {.limit = 32U};
    bms_control_pending_t pending;
    bms_control_session_init(&session, true);
    feed(&session, &errors, "CMD 1 START\nCMD 2 ST", 0);
    bms_control_session_disconnect(&session);
    CHECK(!session.connected && session.count == 0U && session.used == 0U);
    CHECK(bms_control_session_peek(
        &session, 0, save_error, &errors, &pending) == -1);
    CHECK(errno == ENOTCONN);
    CHECK(bms_control_session_written(&session, 1U) == -1);
    CHECK(errno == ENOTCONN);
    CHECK(bms_control_session_feed(
        &session, (const unsigned char *)"OP\n", 3U, 0,
        save_error, &errors) == -1);
    CHECK(errno == ENOTCONN && errors.count == 0U);

    bms_control_session_init(&session, true);
    feed(&session, &errors, "CMD 1 START\nCMD 2 CLEAR\n", 0);
    errors.limit = 1U; /* 只剩一个错误事件槽，第二个取消回执保存失败。 */
    CHECK(bms_control_session_feed(
        &session, (const unsigned char *)"CMD 3 STOP\n", 11U, 0,
        save_error, &errors) == -1);
    expect_error(&errors, 0U, 1U, "CANCELLED");
    CHECK(errno == ENOBUFS && !session.connected && session.count == 0U);

    CHECK(bms_control_session_feed(
        NULL, NULL, 0U, 0, save_error, &errors) == -1);
    CHECK(errno == EINVAL);
    puts("[PASS] disconnect cleanup and bounded-output refusal");
}

int main(void)
{
    test_framing_and_ids();
    test_bad_bytes_and_resync();
    test_queue_and_stop();
    test_readiness_and_expiry();
    test_disconnect_and_output_refusal();
    return EXIT_SUCCESS;
}