#define _POSIX_C_SOURCE 200809L
#include "analyzer/load_history_store.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STORE_LINE_SIZE (LOAD_STORE_FILTER_MAX + 32U)
#define STORE_FILE_MAX 65536
#define STORE_MAGIC "NFA_LOAD_HISTORY_V1"
#define STORE_METRIC "metric=application_read_packets"

static int system_error(void)
{
    return errno != 0 ? errno : EIO;
}

static bool text_ok(const char *text, size_t maximum, bool empty_ok)
{
    if (text == NULL) return false;

    for (size_t i = 0; i <= maximum; ++i) {
        unsigned char c = (unsigned char)text[i];

        if (c == '\0') return i != 0 || empty_ok;
        if (c < 32U || c > 126U) return false;
    }

    return false;
}

static bool identity_ok(const load_store_identity_t *id)
{
    return id != NULL &&
        text_ok(id->source_id, LOAD_STORE_SOURCE_MAX, false) &&
        text_ok(id->interface_name, LOAD_STORE_INTERFACE_MAX, false) &&
        text_ok(id->filter_expression, LOAD_STORE_FILTER_MAX, true);
}

/* 检查可序列化的逻辑状态，不导出未使用的数组位置。 */
static bool history_ok(const load_history_t *h)
{
    if (h == NULL || h->length > LOAD_HISTORY_WINDOWS ||
        h->next_index >= LOAD_HISTORY_WINDOWS) return false;

    if (h->length == 0)
        return h->next_index == 0 && h->next_start_seconds == 0;

    if (h->length < LOAD_HISTORY_WINDOWS &&
        h->next_index != h->length) return false;

    if (h->next_start_seconds <
        (uint64_t)h->length * LOAD_HISTORY_WINDOW_SECONDS) return false;

    size_t oldest =
        h->length == LOAD_HISTORY_WINDOWS ? h->next_index : 0;

    for (size_t i = 0; i < h->length; ++i) {
        size_t slot = (oldest + i) % LOAD_HISTORY_WINDOWS;

        if (h->packets[slot] > LOAD_HISTORY_MAX_PACKETS) return false;
    }

    return true;
}

static int write_snapshot(
    FILE *out,
    const load_store_identity_t *id,
    const load_history_t *h)
{
    uint64_t start = h->next_start_seconds -
        (uint64_t)h->length * LOAD_HISTORY_WINDOW_SECONDS;

    if (fprintf(
            out,
            STORE_MAGIC "\n" STORE_METRIC "\nwindow_seconds=600\n"
            "source_id=%s\ninterface=%s\nfilter=%s\n"
            "count=%zu\nstart=%" PRIu64 "\n",
            id->source_id,
            id->interface_name,
            id->filter_expression,
            h->length,
            start) < 0) return EIO;

    size_t oldest =
        h->length == LOAD_HISTORY_WINDOWS ? h->next_index : 0;

    for (size_t i = 0; i < h->length; ++i) {
        size_t slot = (oldest + i) % LOAD_HISTORY_WINDOWS;

        if (fprintf(
                out,
                "%" PRIu64 ",%d\n",
                h->packets[slot],
                h->usable[slot] ? 1 : 0) < 0) return EIO;
    }

    return fputs("END\n", out) == EOF ? EIO : 0;
}

int load_history_store_save(
    const char *path,
    const load_store_identity_t *id,
    const load_history_t *h)
{
    if (path == NULL || path[0] == '\0' ||
        !identity_ok(id) || !history_ok(h)) return EINVAL;

    const char suffix[] = ".tmp.XXXXXX";
    size_t length = strlen(path);

    if (length > SIZE_MAX - sizeof(suffix)) return EOVERFLOW;

    char *temporary = malloc(length + sizeof(suffix));
    if (temporary == NULL) return ENOMEM;

    memcpy(temporary, path, length);
    memcpy(temporary + length, suffix, sizeof(suffix));

    int fd = mkstemp(temporary);

    if (fd < 0) {
        int rc = system_error();
        free(temporary);
        return rc;
    }

    FILE *out = fdopen(fd, "w");

    if (out == NULL) {
        int rc = system_error();

        (void)close(fd);
        (void)unlink(temporary);
        free(temporary);
        return rc;
    }

    int rc = write_snapshot(out, id, h);

    if (rc == 0 && fflush(out) != 0) rc = EIO;
    if (rc == 0 && fsync(fileno(out)) != 0) rc = system_error();

    if (fclose(out) != 0 && rc == 0) rc = EIO;

    if (rc == 0 && rename(temporary, path) != 0)
        rc = system_error();

    if (rc != 0) (void)unlink(temporary);

    free(temporary);
    return rc;
}

/* 有界逐行读取，拒绝NUL、CR、控制字符、超长行和缺少末尾LF。 */
static int read_line(FILE *in, char line[STORE_LINE_SIZE])
{
    size_t used = 0;

    for (;;) {
        int c = fgetc(in);

        if (c == EOF) return ferror(in) ? EIO : EINVAL;

        if (c == '\n') {
            line[used] = '\0';
            return 0;
        }

        if (c < 32 || c > 126 ||
            used + 1U >= STORE_LINE_SIZE) return EINVAL;

        line[used++] = (char)c;
    }
}

static int expect_line(FILE *in, const char *expected)
{
    char line[STORE_LINE_SIZE];
    int rc = read_line(in, line);

    if (rc != 0) return rc;

    return strcmp(line, expected) == 0 ? 0 : EINVAL;
}

static int expect_identity(
    FILE *in,
    const char *prefix,
    const char *expected)
{
    char line[STORE_LINE_SIZE];
    int rc = read_line(in, line);

    if (rc != 0) return rc;

    size_t length = strlen(prefix);

    if (strncmp(line, prefix, length) != 0) return EINVAL;

    return strcmp(line + length, expected) == 0 ? 0 : EXDEV;
}

/* 不接受负号、加号、空格；乘10前检查uint64_t溢出。 */
static int decimal(const char *text, uint64_t *value)
{
    if (*text == '\0') return EINVAL;

    uint64_t number = 0;

    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9') return EINVAL;

        uint64_t digit = (uint64_t)(*text - '0');

        if (number > (UINT64_MAX - digit) / 10U) return EINVAL;

        number = number * 10U + digit;
    }

    *value = number;
    return 0;
}

static int read_number(
    FILE *in,
    const char *prefix,
    uint64_t *value)
{
    char line[STORE_LINE_SIZE];
    int rc = read_line(in, line);

    if (rc != 0) return rc;

    size_t length = strlen(prefix);

    if (strncmp(line, prefix, length) != 0) return EINVAL;

    return decimal(line + length, value);
}

static int read_snapshot(
    FILE *in,
    const load_store_identity_t *id,
    load_history_t *candidate)
{
    int rc = expect_line(in, STORE_MAGIC);

    if (rc == 0) rc = expect_line(in, STORE_METRIC);
    if (rc == 0) rc = expect_line(in, "window_seconds=600");
    if (rc == 0) rc = expect_identity(in, "source_id=", id->source_id);
    if (rc == 0) rc = expect_identity(in, "interface=", id->interface_name);
    if (rc == 0) rc = expect_identity(in, "filter=", id->filter_expression);

    uint64_t count = 0;
    uint64_t start = 0;

    if (rc == 0) rc = read_number(in, "count=", &count);
    if (rc == 0) rc = read_number(in, "start=", &start);

    if (rc != 0) return rc;

    if (count > LOAD_HISTORY_WINDOWS ||
        (count == 0 && start != 0) ||
        start > UINT64_MAX - count * LOAD_HISTORY_WINDOW_SECONDS)
        return EINVAL;

    rc = load_history_init(candidate);
    if (rc != 0) return rc;

    for (uint64_t i = 0; i < count; ++i) {
        char line[STORE_LINE_SIZE];

        rc = read_line(in, line);
        if (rc != 0) return rc;

        char *comma = strchr(line, ',');

        if (comma == NULL ||
            (comma[1] != '0' && comma[1] != '1') ||
            comma[2] != '\0') return EINVAL;

        *comma = '\0';

        uint64_t packets = 0;
        rc = decimal(line, &packets);

        if (rc != 0 || packets > LOAD_HISTORY_MAX_PACKETS)
            return EINVAL;

        bool usable = comma[1] == '1';

        load_history_window_t window = {
            .start_seconds = start,
            .end_seconds = start + LOAD_HISTORY_WINDOW_SECONDS,
            .packet_count = packets,
            .complete = usable,
            .quality_ok = usable
        };

        rc = load_history_push(candidate, &window);
        if (rc != 0) return rc;

        start = window.end_seconds;
    }

    rc = expect_line(in, "END");
    if (rc != 0) return rc;

    int extra = fgetc(in);

    if (ferror(in)) return EIO;

    return extra == EOF ? 0 : EINVAL;
}

int load_history_store_load(
    const char *path,
    const load_store_identity_t *id,
    load_history_t *h)
{
    if (path == NULL || path[0] == '\0' ||
        !identity_ok(id) || h == NULL) return EINVAL;

    int fd = open(
        path,
        O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW
    );

    if (fd < 0) return system_error();

    struct stat status;

    if (fstat(fd, &status) != 0) {
        int rc = system_error();
        (void)close(fd);
        return rc;
    }

    if (!S_ISREG(status.st_mode) ||
        status.st_size < 0 ||
        status.st_size > STORE_FILE_MAX) {
        (void)close(fd);
        return EINVAL;
    }

    FILE *in = fdopen(fd, "r");

    if (in == NULL) {
        int rc = system_error();
        (void)close(fd);
        return rc;
    }

    load_history_t candidate;
    int rc = read_snapshot(in, id, &candidate);

    if (fclose(in) != 0 && rc == 0) rc = EIO;

    if (rc == 0) *h = candidate;

    return rc;
}