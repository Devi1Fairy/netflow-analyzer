#include "analyzer/load_window.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf( \
            stderr, \
            "[FAIL] line %d: %s\n", \
            __LINE__, \
            #expr \
        ); \
        return EXIT_FAILURE; \
    } \
} while (0)

static bool same_counter(
    const load_window_counter_t *a,
    const load_window_counter_t *b)
{
    return
        a->start_monotonic_ns == b->start_monotonic_ns &&
        a->start_utc_seconds == b->start_utc_seconds &&
        a->packet_count == b->packet_count &&
        a->last_monotonic_ns == b->last_monotonic_ns &&
        a->initialized == b->initialized;
}

int main(void)
{
    const uint64_t width = LOAD_WINDOW_WIDTH_NS;
    const uint64_t mono = UINT64_C(123456789);
    const uint64_t utc = UINT64_C(1696809829);

    load_window_counter_t counter = {0};
    load_history_window_t window = {
        .packet_count = 777
    };

    bool closed = true;

    CHECK(load_window_init(&counter, mono, utc) == 0);
    CHECK(load_window_add(&counter, mono, 3) == 0);

    CHECK(
        load_window_add(
            &counter,
            mono + width - 1,
            2
        ) == 0
    );

    CHECK(
        load_window_close(
            &counter,
            mono + width - 1,
            true,
            true,
            &closed,
            &window
        ) == 0
    );

    CHECK(!closed && window.packet_count == 777);

    load_window_counter_t saved = counter;

    CHECK(
        load_window_add(
            &counter,
            mono + width,
            7
        ) == EAGAIN
    );

    CHECK(same_counter(&counter, &saved));

    CHECK(
        load_window_close(
            &counter,
            mono + width,
            true,
            true,
            &closed,
            &window
        ) == 0
    );

    CHECK(closed && window.packet_count == 5);
    CHECK(window.start_seconds == utc);
    CHECK(window.end_seconds == utc + 600);
    CHECK(window.complete && window.quality_ok);

    /* 边界包在close后重试，恰好进入下一窗一次。 */
    CHECK(
        load_window_add(
            &counter,
            mono + width,
            7
        ) == 0
    );

    CHECK(
        load_window_close(
            &counter,
            mono + 2 * width + 5,
            true,
            true,
            &closed,
            &window
        ) == 0
    );

    CHECK(closed && window.packet_count == 7);

    saved = counter;

    CHECK(
        load_window_add(
            &counter,
            mono + 2 * width + 1,
            1
        ) == ESTALE
    );

    CHECK(same_counter(&counter, &saved));

    CHECK(
        load_window_close(
            &counter,
            mono + 3 * width,
            true,
            true,
            &closed,
            &window
        ) == 0
    );

    CHECK(closed);
    CHECK(window.packet_count == 0 && window.quality_ok);

    CHECK(
        load_window_close(
            &counter,
            mono + 4 * width,
            true,
            false,
            &closed,
            &window
        ) == 0
    );

    CHECK(closed);
    CHECK(window.packet_count == 0 && !window.quality_ok);

    CHECK(
        load_window_close(
            &counter,
            mono + 5 * width,
            false,
            true,
            &closed,
            &window
        ) == 0
    );

    CHECK(!window.complete);

    puts(
        "[PASS] half-open boundaries, retry once, "
        "idle zero and quality flags"
    );

    saved = counter;
    window.packet_count = 777;
    closed = false;

    CHECK(
        load_window_close(
            &counter,
            mono + 7 * width,
            true,
            true,
            &closed,
            &window
        ) == ESTALE
    );

    CHECK(same_counter(&counter, &saved));
    CHECK(!closed && window.packet_count == 777);

    CHECK(load_window_init(&counter, mono, utc) == 0);

    CHECK(
        load_window_add(
            &counter,
            mono,
            LOAD_HISTORY_MAX_PACKETS
        ) == 0
    );

    saved = counter;

    CHECK(
        load_window_add(
            &counter,
            mono + 1,
            1
        ) == EOVERFLOW
    );

    CHECK(same_counter(&counter, &saved));

    CHECK(
        load_window_close(
            &counter,
            mono - 1,
            true,
            true,
            &closed,
            &window
        ) == ESTALE
    );

    CHECK(
        load_window_init(
            &counter,
            UINT64_MAX,
            utc
        ) == EOVERFLOW
    );

    CHECK(same_counter(&counter, &saved));

    CHECK(
        load_window_init(
            &counter,
            mono,
            UINT64_MAX
        ) == EOVERFLOW
    );

    CHECK(same_counter(&counter, &saved));

    CHECK(
        load_window_init(
            &counter,
            UINT64_MAX - width,
            utc
        ) == 0
    );

    saved = counter;

    CHECK(
        load_window_close(
            &counter,
            UINT64_MAX,
            true,
            true,
            &closed,
            &window
        ) == EOVERFLOW
    );

    CHECK(same_counter(&counter, &saved));

    CHECK(
        load_window_init(
            &counter,
            mono,
            UINT64_MAX - LOAD_HISTORY_WINDOW_SECONDS
        ) == 0
    );

    saved = counter;

    CHECK(
        load_window_close(
            &counter,
            mono + width,
            true,
            true,
            &closed,
            &window
        ) == EOVERFLOW
    );

    CHECK(same_counter(&counter, &saved));

    puts(
        "[PASS] long pauses, backward time "
        "and overflow preserve state"
    );

    /*
     * 软件时间推进，不等待真实10分钟或一周。
     * 每窗分两次计数，闭窗检查故意延迟5纳秒。
     */
    load_history_t history;

    CHECK(load_history_init(&history) == 0);
    CHECK(load_window_init(&counter, mono, utc) == 0);

    for (uint64_t i = 0; i < 2025; ++i) {
        uint64_t start = mono + i * width;
        uint64_t a = i / 2;
        uint64_t b = i - a;

        CHECK(
            load_window_add(
                &counter,
                start + 50,
                a
            ) == 0
        );

        CHECK(
            load_window_add(
                &counter,
                start + width - 1,
                b
            ) == 0
        );

        CHECK(
            load_window_close(
                &counter,
                start + width + 5,
                true,
                true,
                &closed,
                &window
            ) == 0
        );

        CHECK(closed && window.packet_count == i);
        CHECK(window.end_seconds == utc + (i + 1) * 600);

        CHECK(load_history_push(&history, &window) == 0);

        double raw[18];

        int rc = load_history_extract(
            &history,
            window.end_seconds,
            raw
        );

        if (i + 1 < 1008) {
            CHECK(rc == EAGAIN);
        } else {
            CHECK(rc == 0);

            uint64_t shift = i + 1 - 1008;

            for (size_t j = 0; j < 12; ++j) {
                CHECK(raw[j] == (double)(shift + 996 + j));
            }

            for (size_t j = 0; j < 3; ++j) {
                CHECK(raw[12 + j] == (double)(shift + 864 + j));
                CHECK(raw[15 + j] == (double)(shift + j));
            }
        }
    }

    puts(
        "[PASS] 2025 jittered windows feed history "
        "with exact 18-feature order"
    );

    CHECK(load_window_init(NULL, mono, utc) == EINVAL);
    CHECK(load_window_add(NULL, mono, 1) == EINVAL);

    CHECK(
        load_window_close(
            NULL,
            mono,
            true,
            true,
            &closed,
            &window
        ) == EINVAL
    );

    CHECK(
        load_window_close(
            &counter,
            mono,
            true,
            true,
            NULL,
            &window
        ) == EINVAL
    );

    CHECK(
        load_window_close(
            &counter,
            mono,
            true,
            true,
            &closed,
            NULL
        ) == EINVAL
    );

    counter.initialized = false;

    CHECK(load_window_add(&counter, mono, 1) == EINVAL);

    printf(
        "[PASS] invalid arguments; counter_bytes=%zu\n",
        sizeof(counter)
    );

    return EXIT_SUCCESS;
}