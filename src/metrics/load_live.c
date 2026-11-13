#include "analyzer/load_live.h"

#include <errno.h>

int load_live_init(
    load_live_t *live,
    uint64_t mono_ns,
    const load_live_boundary_t *boundary)
{
    if (live == NULL || boundary == NULL) return EINVAL;
    load_window_counter_t counter;
    int rc = load_window_init(&counter, mono_ns, boundary->utc_seconds);
    if (rc != 0) return rc;
    *live = (load_live_t){
        .counter = counter,
        .anchor_monotonic_ns = mono_ns,
        .anchor_utc_seconds = boundary->utc_seconds,
        .baseline = *boundary,
        .first_window = true,
        .carry_bad_quality = true
    };
    return load_history_init(&live->history);
}

int load_live_observe(
    load_live_t *live,
    uint64_t mono_ns,
    uint64_t delta,
    const load_live_boundary_t *boundary,
    load_live_event_t *event)
{
    if (live == NULL || event == NULL) return EINVAL;
    if (delta > LOAD_HISTORY_MAX_PACKETS) return EOVERFLOW;

    load_live_event_t result = {0};
    int rc = load_window_add(&live->counter, mono_ns, delta);
    if (rc == 0) {
        *event = result;
        return 0;
    }
    if (rc != EAGAIN && rc != ESTALE) return rc;
    if (boundary == NULL) return EINVAL;

    bool reset = rc == ESTALE;
    if (!reset) {
        uint64_t elapsed = mono_ns - live->anchor_monotonic_ns;
        uint64_t seconds = elapsed / UINT64_C(1000000000);
        if (seconds > UINT64_MAX - live->anchor_utc_seconds)
            return EOVERFLOW;
        uint64_t expected = live->anchor_utc_seconds + seconds;
        uint64_t drift = expected > boundary->utc_seconds
            ? expected - boundary->utc_seconds
            : boundary->utc_seconds - expected;
        reset = drift > 2U ||
            mono_ns - live->counter.start_monotonic_ns >=
                2U * LOAD_WINDOW_WIDTH_NS;
    }

    bool both = live->baseline.statistics_available &&
        boundary->statistics_available;
    if (both &&
        (boundary->dropped_packets < live->baseline.dropped_packets ||
         boundary->interface_dropped_packets <
             live->baseline.interface_dropped_packets)) {
        reset = true;
    }

    if (reset) {
        rc = load_live_init(live, mono_ns, boundary);
        if (rc != 0) return rc;
        rc = load_window_add(&live->counter, mono_ns, delta);
        if (rc != 0) return rc;
        result.reset = true;
        result.history_status = EAGAIN;
        *event = result;
        return 0;
    }

    bool issue = !both ||
        boundary->dropped_packets != live->baseline.dropped_packets ||
        boundary->interface_dropped_packets !=
            live->baseline.interface_dropped_packets;

    /*
     * 查询可能晚于边界。
     * 损失/不确定性同时污染当前与下一窗口，不猜测归属。
     */
    result.statistics_known = both;
    result.quality_carried = live->carry_bad_quality;
    if (both) {
        result.dropped_delta = boundary->dropped_packets -
            live->baseline.dropped_packets;
        result.interface_dropped_delta = boundary->interface_dropped_packets -
            live->baseline.interface_dropped_packets;
    }
    bool quality = !issue && !live->carry_bad_quality;
    rc = load_window_close(
        &live->counter, mono_ns, !live->first_window, quality,
        &result.closed, &result.window);
    if (rc != 0) return rc;
    if (!result.closed) return EIO;

    rc = load_history_push(&live->history, &result.window);
    if (rc != 0) return rc;
    double raw[LOAD_HISTORY_FEATURES];
    result.history_status = load_history_extract(
        &live->history, result.window.end_seconds, raw);
    if (result.history_status != 0 && result.history_status != EAGAIN)
        return result.history_status;

    live->baseline = *boundary;
    live->first_window = false;
    live->carry_bad_quality = issue;
    rc = load_window_add(&live->counter, mono_ns, delta);
    if (rc != 0) return rc;
    *event = result;
    return 0;
}
