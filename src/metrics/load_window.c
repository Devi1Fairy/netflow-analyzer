#include "analyzer/load_window.h"

#include <errno.h>

static bool counter_ok(const load_window_counter_t *c)
{
    return
        c != NULL &&
        c->initialized &&
        c->start_monotonic_ns <=
            UINT64_MAX - LOAD_WINDOW_WIDTH_NS &&
        c->start_utc_seconds <=
            UINT64_MAX - LOAD_HISTORY_WINDOW_SECONDS &&
        c->packet_count <= LOAD_HISTORY_MAX_PACKETS &&
        c->last_monotonic_ns >= c->start_monotonic_ns &&
        c->last_monotonic_ns <
            c->start_monotonic_ns + LOAD_WINDOW_WIDTH_NS;
}

int load_window_init(
    load_window_counter_t *counter,
    uint64_t monotonic_start_ns,
    uint64_t utc_start_seconds)
{
    if (counter == NULL) {
        return EINVAL;
    }

    if (monotonic_start_ns >
            UINT64_MAX - LOAD_WINDOW_WIDTH_NS ||
        utc_start_seconds >
            UINT64_MAX - LOAD_HISTORY_WINDOW_SECONDS) {
        return EOVERFLOW;
    }

    *counter = (load_window_counter_t){
        .start_monotonic_ns = monotonic_start_ns,
        .start_utc_seconds = utc_start_seconds,
        .packet_count = 0,
        .last_monotonic_ns = monotonic_start_ns,
        .initialized = true
    };

    return 0;
}

int load_window_add(
    load_window_counter_t *counter,
    uint64_t mono_ns,
    uint64_t delta)
{
    if (!counter_ok(counter)) {
        return EINVAL;
    }

    if (mono_ns < counter->last_monotonic_ns) {
        return ESTALE;
    }

    uint64_t end =
        counter->start_monotonic_ns + LOAD_WINDOW_WIDTH_NS;

    if (mono_ns >= end) {
        return EAGAIN;
    }

    if (delta >
        LOAD_HISTORY_MAX_PACKETS - counter->packet_count) {
        return EOVERFLOW;
    }

    counter->packet_count += delta;
    counter->last_monotonic_ns = mono_ns;

    return 0;
}

int load_window_close(
    load_window_counter_t *counter,
    uint64_t mono_ns,
    bool complete,
    bool quality_ok,
    bool *closed,
    load_history_window_t *window)
{
    if (!counter_ok(counter) ||
        closed == NULL ||
        window == NULL) {
        return EINVAL;
    }

    if (mono_ns < counter->last_monotonic_ns) {
        return ESTALE;
    }

    uint64_t end =
        counter->start_monotonic_ns + LOAD_WINDOW_WIDTH_NS;

    if (mono_ns < end) {
        counter->last_monotonic_ns = mono_ns;
        *closed = false;
        return 0;
    }

    /*
     * 不能通过反复close，把长时间停顿解释为多个有效零窗口。
     */
    if (mono_ns - end >= LOAD_WINDOW_WIDTH_NS) {
        return ESTALE;
    }

    if (end > UINT64_MAX - LOAD_WINDOW_WIDTH_NS ||
        counter->start_utc_seconds >
            UINT64_MAX - 2U * LOAD_HISTORY_WINDOW_SECONDS) {
        return EOVERFLOW;
    }

    load_history_window_t candidate = {
        .start_seconds = counter->start_utc_seconds,
        .end_seconds =
            counter->start_utc_seconds +
            LOAD_HISTORY_WINDOW_SECONDS,
        .packet_count = counter->packet_count,
        .complete = complete,
        .quality_ok = quality_ok
    };

    counter->start_monotonic_ns = end;
    counter->start_utc_seconds = candidate.end_seconds;
    counter->packet_count = 0;
    counter->last_monotonic_ns = mono_ns;

    *window = candidate;
    *closed = true;

    return 0;
}