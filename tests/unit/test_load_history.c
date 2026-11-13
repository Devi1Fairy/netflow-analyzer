#include "analyzer/load_history.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf( \
            stderr, \
            "[FAIL] line %d: %s\n", \
            __LINE__, \
            #condition \
        ); \
        return EXIT_FAILURE; \
    } \
} while (0)

/* 故意使用非UTC整10分钟起点。 */
static const uint64_t BASE = UINT64_C(1696809829);

static load_history_window_t make_window(size_t index)
{
    uint64_t start =
        BASE + (uint64_t)index * 600U;

    return (load_history_window_t){
        .start_seconds = start,
        .end_seconds = start + 600U,
        .packet_count = (uint64_t)index,
        .complete = true,
        .quality_ok = true
    };
}

static int fill(load_history_t *h, size_t bad_index)
{
    CHECK(load_history_init(h) == 0);

    for (size_t i = 0; i < LOAD_HISTORY_WINDOWS; ++i) {
        load_history_window_t w = make_window(i);

        if (i == bad_index) {
            w.quality_ok = false;
        }

        CHECK(load_history_push(h, &w) == 0);
    }

    return 0;
}

static int correct_features(
    const double raw[18],
    size_t shift)
{
    for (size_t i = 0; i < 12; ++i) {
        CHECK(raw[i] == (double)(shift + 996U + i));
    }

    for (size_t i = 0; i < 3; ++i) {
        CHECK(raw[12 + i] == (double)(shift + 864U + i));
        CHECK(raw[15 + i] == (double)(shift + i));
    }

    return 0;
}

/* 比较字段和数组，不比较整个结构体的填充字节。 */
static int same_state(
    const load_history_t *a,
    const load_history_t *b)
{
    return
        a->length == b->length &&
        a->next_index == b->next_index &&
        a->next_start_seconds == b->next_start_seconds &&
        memcmp(
            a->packets,
            b->packets,
            sizeof(a->packets)
        ) == 0 &&
        memcmp(
            a->usable,
            b->usable,
            sizeof(a->usable)
        ) == 0;
}

int main(void)
{
    load_history_t history;
    double raw[18];

    for (size_t i = 0; i < 18; ++i) {
        raw[i] = -77;
    }

    CHECK(load_history_init(&history) == 0);

    CHECK(
        load_history_extract(
            &history,
            BASE,
            raw
        ) == EAGAIN
    );

    for (size_t i = 0; i < 18; ++i) {
        CHECK(raw[i] == -77);
    }

    CHECK(fill(&history, SIZE_MAX) == 0);

    CHECK(
        load_history_extract(
            &history,
            BASE + 1008U * 600U,
            raw
        ) == 0
    );

    CHECK(correct_features(raw, 0) == 0);
    CHECK(raw[15] == 0);  /* 真正零流量可用。 */

    /* 多次覆盖环形缓冲，检查特征仍取对位置。 */
    for (size_t i = 1008; i < 3033; ++i) {
        load_history_window_t w = make_window(i);

        CHECK(load_history_push(&history, &w) == 0);

        CHECK(
            load_history_extract(
                &history,
                w.end_seconds,
                raw
            ) == 0
        );

        CHECK(
            correct_features(
                raw,
                i + 1U - 1008U
            ) == 0
        );
    }

    puts(
        "[PASS] bounded warmup, zero traffic, "
        "feature order and ring rollover"
    );

    load_history_t saved = history;
    load_history_window_t w = make_window(3032);

    CHECK(load_history_push(&history, &w) == ESTALE);
    CHECK(same_state(&history, &saved));

    w = make_window(10);

    CHECK(load_history_push(&history, &w) == ESTALE);
    CHECK(same_state(&history, &saved));

    w = make_window(3033);
    --w.end_seconds;

    CHECK(load_history_push(&history, &w) == EINVAL);
    CHECK(same_state(&history, &saved));

    w = make_window(3033);
    w.packet_count = LOAD_HISTORY_MAX_PACKETS + 1U;

    CHECK(load_history_push(&history, &w) == EINVAL);
    CHECK(same_state(&history, &saved));

    for (size_t i = 0; i < 18; ++i) {
        raw[i] = -77;
    }

    CHECK(
        load_history_extract(
            &history,
            history.next_start_seconds + 1U,
            raw
        ) == ESTALE
    );

    for (size_t i = 0; i < 18; ++i) {
        CHECK(raw[i] == -77);
    }

    /* 向前缺一窗，旧历史不再参与预测。 */
    w = make_window(3034);

    CHECK(load_history_push(&history, &w) == 0);
    CHECK(history.length == 1);

    CHECK(
        load_history_extract(
            &history,
            w.end_seconds,
            raw
        ) == EAGAIN
    );

    puts(
        "[PASS] duplicate/backward/invalid input rejected; "
        "forward gap resets history"
    );

    /* 最近窗口质量失效，必须暂缓预测。 */
    CHECK(fill(&history, 1007) == 0);

    CHECK(
        load_history_extract(
            &history,
            history.next_start_seconds,
            raw
        ) == EAGAIN
    );

    for (size_t i = 0; i < 18; ++i) {
        CHECK(raw[i] == -77);
    }

    for (size_t i = 1008; i < 1020; ++i) {
        w = make_window(i);

        CHECK(load_history_push(&history, &w) == 0);

        int rc = load_history_extract(
            &history,
            w.end_seconds,
            raw
        );

        CHECK(rc == (i == 1019 ? 0 : EAGAIN));
    }

    CHECK(correct_features(raw, 12) == 0);

    /*
     * 第100窗不在当前需要的18个位置中。
     * 保留其无效标记，不影响本次完整输入。
     */
    CHECK(fill(&history, 100) == 0);

    CHECK(
        load_history_extract(
            &history,
            history.next_start_seconds,
            raw
        ) == 0
    );

    CHECK(correct_features(raw, 0) == 0);

    CHECK(fill(&history, SIZE_MAX) == 0);

    w = make_window(1008);
    w.complete = false;

    CHECK(load_history_push(&history, &w) == 0);

    CHECK(
        load_history_extract(
            &history,
            w.end_seconds,
            raw
        ) == EAGAIN
    );

    puts(
        "[PASS] selected unknown/partial windows block; "
        "unused unknown is not imputed"
    );

    /* 验证最大可精确表示计数，以及时间溢出输入。 */
    CHECK(fill(&history, SIZE_MAX) == 0);

    w = make_window(1008);
    w.packet_count = LOAD_HISTORY_MAX_PACKETS;

    CHECK(load_history_push(&history, &w) == 0);

    CHECK(
        load_history_extract(
            &history,
            w.end_seconds,
            raw
        ) == 0
    );

    CHECK(raw[11] == (double)LOAD_HISTORY_MAX_PACKETS);

    saved = history;
    w.start_seconds = UINT64_MAX - 100U;
    w.end_seconds = 0;

    CHECK(load_history_push(&history, &w) == EINVAL);
    CHECK(same_state(&history, &saved));

    CHECK(load_history_init(NULL) == EINVAL);
    CHECK(load_history_push(NULL, &w) == EINVAL);
    CHECK(load_history_push(&history, NULL) == EINVAL);

    CHECK(
        load_history_extract(
            NULL,
            BASE,
            raw
        ) == EINVAL
    );

    CHECK(
        load_history_extract(
            &history,
            BASE,
            NULL
        ) == EINVAL
    );

    history.next_index = LOAD_HISTORY_WINDOWS;

    CHECK(load_history_push(&history, &w) == EINVAL);

    printf(
        "[PASS] invalid arguments; history_bytes=%zu\n",
        sizeof(history)
    );

    return EXIT_SUCCESS;
}