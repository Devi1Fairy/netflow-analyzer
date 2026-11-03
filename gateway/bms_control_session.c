#include "bms_control_session.h"

#include <errno.h>
#include <string.h>

void bms_control_session_init(bms_control_session_t *session,
                              bool serial_ready)
{
    *session = (bms_control_session_t){
        .connected = true,
        .serial_ready = serial_ready
    };
}

void bms_control_session_disconnect(bms_control_session_t *session)
{
    session->connected = false;
    session->serial_ready = false;
    session->used = 0U;
    session->discarding = false;
    session->count = 0U;
}

static void remove_pending(bms_control_session_t *session, size_t index)
{
    --session->count;
    memmove(&session->pending[index], &session->pending[index + 1U],
            (session->count - index) * sizeof(session->pending[0]));
}

static int emit_error(bms_control_session_t *session, uint32_t id,
                      const char *reason,
                      bms_control_error_sink_fn sink, void *context)
{
    if (sink(id, reason, context) != 0) {
        /* 没地方保存回执时，不能继续积压或执行控制请求。 */
        bms_control_session_disconnect(session);
        errno = ENOBUFS;
        return -1;
    }
    return 0;
}

static int expire_pending(bms_control_session_t *session, int64_t now_ms,
                          bms_control_error_sink_fn sink, void *context)
{
    if (session == NULL || sink == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!session->connected) {
        errno = ENOTCONN;
        return -1;
    }
    if (now_ms < 0 || now_ms < session->last_now_ms) {
        bms_control_session_disconnect(session);
        errno = EINVAL;
        return -1;
    }
    session->last_now_ms = now_ms;

    size_t index = 0U;
    while (index < session->count) {
        if (now_ms - session->pending[index].queued_ms <
            BMS_CONTROL_TTL_MS) {
            ++index;
            continue;
        }

        uint32_t id = session->pending[index].request.id;
        remove_pending(session, index);
        if (emit_error(session, id, "EXPIRED", sink, context) == -1) {
            return -1;
        }
    }
    return 0;
}

static int admit_request(bms_control_session_t *session, int64_t now_ms,
                         bms_control_error_sink_fn sink, void *context)
{
    bms_control_request_t request;
    uint32_t reply_id;
    bms_control_parse_result_t parsed = bms_control_parse_request(
        session->body, session->used, &request, &reply_id);

    if (parsed != BMS_CONTROL_PARSE_OK) {
        return emit_error(session, reply_id,
                          parsed == BMS_CONTROL_BAD_COMMAND ?
                          "BAD_COMMAND" : "BAD_REQUEST", sink, context);
    }
    if (request.id <= session->last_id) {
        return emit_error(session, request.id, "STALE_ID", sink, context);
    }

    /* 合法新号在NOT_READY/QUEUE_FULL等后续拒绝之前即被消耗。 */
    session->last_id = request.id;
    if (!session->serial_ready) {
        return emit_error(session, request.id, "NOT_READY", sink, context);
    }

    if (request.action == BMS_CONTROL_STOP) {
        size_t index = 0U;
        while (index < session->count) {
            if (session->pending[index].request.action == BMS_CONTROL_STOP) {
                ++index;
                continue;
            }
            uint32_t cancelled = session->pending[index].request.id;
            remove_pending(session, index);
            if (emit_error(session, cancelled, "CANCELLED",
                           sink, context) == -1) {
                return -1;
            }
        }
    }

    if (session->count == BMS_CONTROL_QUEUE_CAPACITY) {
        return emit_error(session, request.id, "QUEUE_FULL", sink, context);
    }

    /*
     * STOP先取消全部普通项，保留旧STOP；新STOP接在旧STOP之后。
     * 普通命令总在尾部入队，因此STOP自然位于普通命令之前。
     */
    session->pending[session->count++] = (bms_control_pending_t){
        .request = request,
        .queued_ms = now_ms
    };
    return 0;
}

int bms_control_session_feed(bms_control_session_t *session,
                             const unsigned char *bytes, size_t length,
                             int64_t now_ms,
                             bms_control_error_sink_fn sink, void *context)
{
    if (bytes == NULL && length != 0U) {
        errno = EINVAL;
        return -1;
    }
    if (expire_pending(session, now_ms, sink, context) == -1) {
        return -1;
    }

    for (size_t index = 0U; index < length; ++index) {
        if (bytes[index] == '\n') {
            int result = session->discarding ?
                emit_error(session, 0U, "BAD_REQUEST", sink, context) :
                admit_request(session, now_ms, sink, context);

            session->used = 0U;
            session->discarding = false;
            if (result == -1) {
                return -1;
            }
        } else if (!session->discarding) {
            if (session->used == BMS_CONTROL_MAX_BODY) {
                session->used = 0U;
                session->discarding = true;
            } else {
                /* 保留非法字节给解析器拒绝，绝不删除后拼成合法命令。 */
                session->body[session->used++] = bytes[index];
            }
        }
    }
    return 0;
}

int bms_control_session_peek(bms_control_session_t *session,
                             int64_t now_ms,
                             bms_control_error_sink_fn sink, void *context,
                             bms_control_pending_t *out)
{
    if (out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (expire_pending(session, now_ms, sink, context) == -1) {
        return -1;
    }
    if (session->count == 0U) {
        return 0;
    }
    *out = session->pending[0];
    return 1;
}

int bms_control_session_written(bms_control_session_t *session, uint32_t id)
{
    if (session == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!session->connected) {
        errno = ENOTCONN;
        return -1;
    }
    if (session->count == 0U || session->pending[0].request.id != id) {
        errno = EINVAL;
        return -1;
    }
    remove_pending(session, 0U);
    return 0;
}