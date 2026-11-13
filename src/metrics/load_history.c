#include "analyzer/load_history.h"

#include <errno.h>
#include <string.h>

static bool state_ok(const load_history_t *h)
{
    if (h == NULL ||
        h->length > LOAD_HISTORY_WINDOWS ||
        h->next_index >= LOAD_HISTORY_WINDOWS) {
        return false;
    }

    if (h->length == 0) {
        return h->next_index == 0 &&
               h->next_start_seconds == 0;
    }

    return
        h->next_start_seconds >= LOAD_HISTORY_WINDOW_SECONDS &&
        (
            h->length == LOAD_HISTORY_WINDOWS ||
            h->next_index == h->length
        );
}

int load_history_init(load_history_t *history)
{
    if (history == NULL) {
        return EINVAL;
    }

    *history = (load_history_t){0};

    return 0;
}

int load_history_push(
    load_history_t *history,
    const load_history_window_t *window)
{
    if (!state_ok(history) ||
        window == NULL ||
        window->end_seconds < window->start_seconds ||
        window->end_seconds - window->start_seconds !=
            LOAD_HISTORY_WINDOW_SECONDS ||
        window->packet_count > LOAD_HISTORY_MAX_PACKETS) {
        return EINVAL;
    }

    if (history->length != 0 &&
        window->start_seconds < history->next_start_seconds) {
        return ESTALE;
    }

    if (history->length != 0 &&
        window->start_seconds > history->next_start_seconds) {
        /*
         * 出现时间缺口，不补零。
         * 当前窗口开始重新积累连续历史。
         */
        history->length = 0;
        history->next_index = 0;
    }

    size_t index = history->next_index;

    history->packets[index] = window->packet_count;

    history->usable[index] =
        window->complete && window->quality_ok;

    history->next_index =
        (index + 1U) % LOAD_HISTORY_WINDOWS;

    if (history->length < LOAD_HISTORY_WINDOWS) {
        ++history->length;
    }

    history->next_start_seconds = window->end_seconds;

    return 0;
}

int load_history_extract(
    const load_history_t *history,
    uint64_t origin_seconds,
    double raw[LOAD_HISTORY_FEATURES])
{
    if (!state_ok(history) || raw == NULL) {
        return EINVAL;
    }

    if (history->length != LOAD_HISTORY_WINDOWS) {
        return EAGAIN;
    }

    if (origin_seconds != history->next_start_seconds) {
        return ESTALE;
    }

    double candidate[LOAD_HISTORY_FEATURES];

    for (size_t i = 0; i < LOAD_HISTORY_FEATURES; ++i) {
        size_t offset;

        if (i < 12U) {
            offset = LOAD_HISTORY_WINDOWS - 12U + i;
        } else if (i < 15U) {
            offset =
                LOAD_HISTORY_WINDOWS - 144U + (i - 12U);
        } else {
            offset = i - 15U;
        }

        /*
         * 缓冲满时，next_index指向最旧窗口。
         * offset是相对最旧窗口的位置。
         */
        size_t index =
            (history->next_index + offset) %
            LOAD_HISTORY_WINDOWS;

        if (!history->usable[index]) {
            return EAGAIN;
        }

        candidate[i] = (double)history->packets[index];
    }

    memcpy(raw, candidate, sizeof(candidate));

    return 0;
}