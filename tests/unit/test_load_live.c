#include "analyzer/load_live.h"
#include "analyzer/app.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "[FAIL] line %d: %s\n", __LINE__, #x); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void)
{
    const uint64_t mono = 123456789, utc = 1696809829;
    const uint64_t width = LOAD_WINDOW_WIDTH_NS;
    load_live_t live;
    load_live_event_t event;
    load_live_boundary_t b = {
        .utc_seconds = utc,
        .statistics_available = true
    };
    CHECK(load_live_init(&live, mono, &b) == 0);

    for (uint64_t i = 0; i < 1009; ++i) {
        uint64_t start = mono + i * width;
        CHECK(load_live_observe(
            &live, start + 10, i, NULL, &event) == 0);
        CHECK(!event.closed && !event.reset);
        b.utc_seconds = utc + (i + 1) * 600;
        if (i == 20) ++b.dropped_packets;
        CHECK(load_live_observe(
            &live, start + width + 1, 0, &b, &event) == 0);
        CHECK(event.closed && !event.reset);
        CHECK(event.window.packet_count == i);
        CHECK(event.statistics_known);
        CHECK(event.dropped_delta == (i == 20 ? 1U : 0U));
        CHECK(event.quality_carried == (i == 0 || i == 21));
        CHECK(event.window.start_seconds == utc + i * 600);
        CHECK(event.window.complete == (i != 0));
        CHECK(event.window.quality_ok == (i != 0 && i != 20 && i != 21));
        CHECK(event.history_status == (i == 1008 ? 0 : EAGAIN));
    }
    puts("[PASS] packet/idle observations, first window, adjacent loss and warmup");

    uint64_t later = live.counter.start_monotonic_ns + 2 * width;
    b.utc_seconds = utc + (later - mono) / UINT64_C(1000000000);
    CHECK(load_live_observe(&live, later, 3, &b, &event) == 0);
    CHECK(event.reset && !event.closed && live.history.length == 0);
    CHECK(live.counter.packet_count == 3);

    later += width;
    b.utc_seconds += 610;
    CHECK(load_live_observe(&live, later, 1, &b, &event) == 0);
    CHECK(event.reset && live.history.length == 0);
    later += width;
    b.utc_seconds += 600;
    b.dropped_packets = 0;
    CHECK(load_live_observe(&live, later, 0, &b, &event) == 0);
    CHECK(event.reset && live.history.length == 0);
    puts("[PASS] long pause, wall clock jump and statistics regression reset history");

    b = (load_live_boundary_t){.utc_seconds = utc};
    CHECK(load_live_init(&live, mono, &b) == 0);
    for (uint64_t i = 0; i < 4; ++i) {
        b.utc_seconds += 600;
        if (i != 0) b.statistics_available = true;
        CHECK(load_live_observe(
            &live, mono + (i + 1) * width, 0, &b, &event) == 0);
        CHECK(event.closed);
        CHECK(event.window.quality_ok == (i == 3));
    }
    event.window.packet_count = 777;
    CHECK(load_live_observe(
        &live, mono + 5 * width, 0, NULL, &event) == EINVAL);
    CHECK(event.window.packet_count == 777);
    CHECK(load_live_observe(
        &live, mono + 4 * width, LOAD_HISTORY_MAX_PACKETS + 1,
        NULL, &event) == EOVERFLOW);
    CHECK(event.window.packet_count == 777);
    puts("[PASS] unknown statistics stay unavailable and recover conservatively");

    app_context_t app;
    CHECK(app_context_init(&app) == 0);
    CHECK(!app.collect_load_windows);
    char *enabled[] = {"nfa", "--load-windows", "--interface", "unused"};
    CHECK(app_parse_arguments(&app, 4, enabled) == 0);
    CHECK(app.collect_load_windows);
    char *plain[] = {"nfa", "--interface", "unused"};
    CHECK(app_parse_arguments(&app, 3, plain) == 0);
    CHECK(!app.collect_load_windows);
    char *last[] = {"nfa", "--interface", "unused", "--load-windows"};
    CHECK(app_parse_arguments(&app, 4, last) == 0);
    CHECK(app.collect_load_windows);
    char *duplicate[] = {"nfa", "--interface", "unused",
                         "--load-windows", "--load-windows"};
    CHECK(app_parse_arguments(&app, 5, duplicate) == EINVAL);
    CHECK(!app.collect_load_windows);
    char *offline[] = {"nfa", "--read", "does-not-exist.pcap", "--load-windows"};
    CHECK(app_parse_arguments(&app, 4, offline) == EINVAL);
    CHECK(!app.collect_load_windows);
    app.command = APP_COMMAND_READ_CAPTURE;
    app.capture_path = "does-not-exist.pcap";
    app.collect_load_windows = true;
    CHECK(app_run(&app) == EINVAL);
    app_cleanup(&app);
    CHECK(!app.collect_load_windows);
    puts("[PASS] live-only flag, duplicate rejection, reset and direct-context guard");
    return EXIT_SUCCESS;
}
