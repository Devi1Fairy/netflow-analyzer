#define _POSIX_C_SOURCE 200809L
#include "analyzer/load_history_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "[FAIL] line %d: %s\n", __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

static const uint64_t BASE = UINT64_C(1696809829);

static const load_store_identity_t ID = {
    .source_id = "vm-loopback-test",
    .interface_name = "lo",
    .filter_expression = "icmp and host 127.0.0.1"
};

static int push(
    load_history_t *h,
    size_t i,
    uint64_t packets,
    bool usable)
{
    load_history_window_t w = {
        .start_seconds = BASE + (uint64_t)i * 600U,
        .end_seconds = BASE + ((uint64_t)i + 1U) * 600U,
        .packet_count = packets,
        .complete = usable,
        .quality_ok = usable
    };

    return load_history_push(h, &w);
}

/* 成功恢复可以改变环形下标，所以比较时间顺序和有效性。 */
static bool same_logical(
    const load_history_t *a,
    const load_history_t *b)
{
    if (a->length != b->length ||
        a->next_start_seconds != b->next_start_seconds) return false;

    size_t first_a =
        a->length == LOAD_HISTORY_WINDOWS ? a->next_index : 0;
    size_t first_b =
        b->length == LOAD_HISTORY_WINDOWS ? b->next_index : 0;

    for (size_t i = 0; i < a->length; ++i) {
        size_t x = (first_a + i) % LOAD_HISTORY_WINDOWS;
        size_t y = (first_b + i) % LOAD_HISTORY_WINDOWS;

        if (a->packets[x] != b->packets[y] ||
            a->usable[x] != b->usable[y]) return false;
    }

    return true;
}

/* 失败时要求输出对象连物理下标和数组都不变。 */
static bool same_state(
    const load_history_t *a,
    const load_history_t *b)
{
    return a->length == b->length &&
        a->next_index == b->next_index &&
        a->next_start_seconds == b->next_start_seconds &&
        memcmp(a->packets, b->packets, sizeof(a->packets)) == 0 &&
        memcmp(a->usable, b->usable, sizeof(a->usable)) == 0;
}

static int write_bytes(
    const char *path,
    const char *text,
    size_t length)
{
    FILE *out = fopen(path, "wb");
    if (out == NULL) return EIO;

    int rc = fwrite(text, 1, length, out) == length ? 0 : EIO;

    if (fclose(out) != 0) rc = EIO;

    return rc;
}

static int reject_body(
    const char *path,
    const char *body,
    load_history_t *h)
{
    const char prefix[] =
        "NFA_LOAD_HISTORY_V1\nmetric=application_read_packets\n"
        "window_seconds=600\nsource_id=vm-loopback-test\n"
        "interface=lo\nfilter=icmp and host 127.0.0.1\n";

    char text[4096];
    int n = snprintf(text, sizeof(text), "%s%s", prefix, body);

    CHECK(n >= 0 && (size_t)n < sizeof(text));
    CHECK(write_bytes(path, text, (size_t)n) == 0);

    load_history_t saved = *h;

    CHECK(load_history_store_load(path, &ID, h) == EINVAL);
    CHECK(same_state(h, &saved));

    return 0;
}

int main(void)
{
    char directory[] = "/tmp/nfa-load-store-test.XXXXXX";
    CHECK(mkdtemp(directory) != NULL);

    char path[256];
    char blocked[256];
    char special[256];

    CHECK(snprintf(
        path, sizeof(path), "%s/history.txt", directory) > 0);
    CHECK(snprintf(
        blocked, sizeof(blocked), "%s/blocked", directory) > 0);
    CHECK(snprintf(
        special, sizeof(special), "%s/special", directory) > 0);

    mode_t old_mask = umask(0077);

    load_history_t h;
    load_history_t restored;

    CHECK(load_history_init(&h) == 0);
    CHECK(load_history_init(&restored) == 0);

    load_history_t saved = restored;

    CHECK(load_history_store_load(path, &ID, &restored) == ENOENT);
    CHECK(same_state(&restored, &saved));

    CHECK(load_history_store_save(path, &ID, &h) == 0);
    CHECK(load_history_store_load(path, &ID, &restored) == 0);
    CHECK(same_logical(&h, &restored));

    CHECK(push(&h, 0, 6, false) == 0);
    CHECK(push(&h, 1, 0, true) == 0);

    CHECK(load_history_store_save(path, &ID, &h) == 0);
    CHECK(load_history_store_load(path, &ID, &restored) == 0);
    CHECK(same_logical(&h, &restored));

    CHECK(!restored.usable[0] && restored.usable[1]);
    CHECK(restored.packets[1] == 0);

    struct stat status;
    CHECK(stat(path, &status) == 0);
    CHECK((status.st_mode & 0777) == 0600);

    load_store_identity_t unfiltered = ID;
    unfiltered.filter_expression = "";

    CHECK(load_history_store_save(path, &unfiltered, &h) == 0);
    CHECK(load_history_store_load(path, &unfiltered, &restored) == 0);
    CHECK(same_logical(&h, &restored));

    saved = restored;

    CHECK(load_history_store_load(path, &ID, &restored) == EXDEV);
    CHECK(same_state(&restored, &saved));

    puts("[PASS] empty/partial snapshots, valid zero and unusable mask");

    CHECK(load_history_init(&h) == 0);

    for (size_t i = 0; i < 1045; ++i) {
        CHECK(push(
            &h, i, i % 7U == 0 ? 0 : (uint64_t)i, true) == 0);
    }

    CHECK(h.next_index == 37);
    CHECK(load_history_store_save(path, &ID, &h) == 0);
    CHECK(load_history_store_load(path, &ID, &restored) == 0);
    CHECK(restored.next_index == 0);
    CHECK(same_logical(&h, &restored));

    double x[18];
    double y[18];

    CHECK(load_history_extract(&h, h.next_start_seconds, x) == 0);
    CHECK(load_history_extract(
        &restored, restored.next_start_seconds, y) == 0);

    for (size_t i = 0; i < 18; ++i) {
        size_t index = i < 12 ? 1033U + i :
            (i < 15 ? 901U + i - 12U : 37U + i - 15U);

        double expected = index % 7U == 0 ? 0 : (double)index;

        CHECK(x[i] == expected && y[i] == expected);
    }

    CHECK(push(&h, 1045, 7, false) == 0);
    CHECK(push(&restored, 1045, 7, false) == 0);
    CHECK(same_logical(&h, &restored));

    CHECK(load_history_extract(
        &restored, restored.next_start_seconds, y) == EAGAIN);

    puts("[PASS] wrapped ring, 18 feature values and continued history");

    CHECK(load_history_store_save(path, &ID, &h) == 0);

    saved = restored;

    load_store_identity_t wrong = ID;
    wrong.source_id = "another-device";
    CHECK(load_history_store_load(path, &wrong, &restored) == EXDEV);

    wrong = ID;
    wrong.interface_name = "eth0";
    CHECK(load_history_store_load(path, &wrong, &restored) == EXDEV);

    wrong = ID;
    wrong.filter_expression = "";
    CHECK(load_history_store_load(path, &wrong, &restored) == EXDEV);
    CHECK(same_state(&restored, &saved));

    wrong = ID;
    wrong.source_id = "bad\nsource";
    CHECK(load_history_store_save(path, &wrong, &h) == EINVAL);

    load_history_t bad = h;
    bad.packets[bad.next_index] = LOAD_HISTORY_MAX_PACKETS + 1U;

    CHECK(load_history_store_save(path, &ID, &bad) == EINVAL);
    CHECK(load_history_store_load(path, &ID, &restored) == 0);
    CHECK(same_logical(&h, &restored));

    puts("[PASS] identity separation and invalid save preserves old file");

    const char *invalid[] = {
        "count=2\nstart=1696809829\n6,0\n0,1\nEND",
        "count=2\nstart=1696809829\n6,0\nEND\n",
        "count=1009\nstart=1696809829\nEND\n",
        "count=1\nstart=18446744073709551615\n0,1\nEND\n",
        "count=1\nstart=1696809829\n9007199254740992,1\nEND\n",
        "count=1\nstart=1696809829\n18446744073709551616,1\nEND\n",
        "count=1\nstart=1696809829\n6,2\nEND\n",
        "count=1\nstart=1696809829\n6,1,1\nEND\n",
        "count=1\nstart=1696809829\n+6,1\nEND\n",
        "count=0\nstart=1\nEND\n",
        "count=0\nstart=0\nEND\nextra\n",
        "count=0\r\nstart=0\nEND\n"
    };

    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(reject_body(path, invalid[i], &restored) == 0);
    }

    saved = restored;

    const char nul[] = "NFA_LOAD_HISTORY_V1\0ignored\n";
    CHECK(write_bytes(path, nul, sizeof(nul) - 1U) == 0);
    CHECK(load_history_store_load(path, &ID, &restored) == EINVAL);
    CHECK(same_state(&restored, &saved));

    const char version[] = "NFA_LOAD_HISTORY_V2\n";
    CHECK(write_bytes(path, version, sizeof(version) - 1U) == 0);
    CHECK(load_history_store_load(path, &ID, &restored) == EINVAL);

    char long_line[2048];
    memset(long_line, 'A', sizeof(long_line));
    long_line[sizeof(long_line) - 1U] = '\n';

    CHECK(write_bytes(path, long_line, sizeof(long_line)) == 0);
    CHECK(load_history_store_load(path, &ID, &restored) == EINVAL);

    FILE *large = fopen(path, "wb");
    CHECK(large != NULL);
    CHECK(fseek(large, 1048576L, SEEK_SET) == 0);
    CHECK(fputc('X', large) != EOF && fclose(large) == 0);

    CHECK(load_history_store_load(path, &ID, &restored) == EINVAL);
    CHECK(same_state(&restored, &saved));

    puts("[PASS] truncated, malformed and overflowing input is transactional");

    CHECK(mkdir(blocked, 0700) == 0);
    CHECK(load_history_store_save(blocked, &ID, &h) != 0);
    CHECK(load_history_store_save(path, &ID, &h) == 0);

    saved = restored;

    CHECK(mkfifo(special, 0600) == 0);
    CHECK(load_history_store_load(special, &ID, &restored) == EINVAL);
    CHECK(unlink(special) == 0);

    CHECK(symlink(path, special) == 0);
    CHECK(load_history_store_load(special, &ID, &restored) == ELOOP);
    CHECK(same_state(&restored, &saved));
    CHECK(unlink(special) == 0);

    DIR *entries = opendir(directory);
    CHECK(entries != NULL);

    struct dirent *entry;

    while ((entry = readdir(entries)) != NULL) {
        CHECK(strstr(entry->d_name, ".tmp.") == NULL);
    }

    CHECK(closedir(entries) == 0);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(blocked) == 0);
    CHECK(rmdir(directory) == 0);

    (void)umask(old_mask);

    puts("[PASS] rename failure cleanup, regular-file input and private mode");

    return EXIT_SUCCESS;
}