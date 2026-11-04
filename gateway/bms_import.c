#define _GNU_SOURCE
#include "bms_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define IMPORT_MAX_BYTES (16U * 1024U * 1024U)

typedef struct {
    size_t lines;
    size_t skipped;
    size_t imported;
} import_stats_t;

static int valid_id(const char *text)
{
    for (size_t i = 0U; i <= BMS_STORE_ID_MAX; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == 0U) return i > 0U;
        if (ch < 0x20U || ch > 0x7eU) return 0;
    }
    return 0;
}

/* O_PATH先取得节点引用并检查类型，不调用字符设备的打开操作。 */
static FILE *open_regular_input(const char *path, struct stat *initial)
{
    int reference = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (reference == -1) return NULL;
    struct stat info;
    int problem = 0;
    if (fstat(reference, &info) == -1) problem = errno;
    else if (!S_ISREG(info.st_mode)) problem = EINVAL;
    else if (info.st_size < 0 || info.st_size > IMPORT_MAX_BYTES) problem = EFBIG;
    if (problem != 0) {
        (void)close(reference);
        errno = problem;
        return NULL;
    }

    *initial = info;
    char descriptor_path[64];
    (void)snprintf(descriptor_path, sizeof(descriptor_path),
                   "/proc/self/fd/%d", reference);
    int fd = open(descriptor_path, O_RDONLY | O_CLOEXEC);
    int saved = errno;
    (void)close(reference);
    if (fd == -1) {
        errno = saved;
        return NULL;
    }
    FILE *stream = fdopen(fd, "rb");
    if (stream == NULL) {
        saved = errno;
        (void)close(fd);
        errno = saved;
    }
    return stream;
}

static int flush_batch(bms_store_t *store, bms_store_row_t *rows,
                       size_t *pending, import_stats_t *stats)
{
    if (*pending == 0U) return 0;
    int rc = bms_store_write_batch(store, rows, *pending);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "database write: %s (%d)\n", sqlite3_errstr(rc), rc);
        return -1;
    }
    stats->imported += *pending;
    *pending = 0U;
    return 0;
}

static int import_stream(FILE *input, bms_store_t *store,
                         const char *device, const char *session,
                         import_stats_t *stats)
{
    char line[BMS_TELEMETRY_MAX_LINE];
    bms_store_row_t rows[BMS_STORE_BATCH_MAX];
    size_t used = 0U, bytes = 0U, pending = 0U;
    int ch;

    while ((ch = fgetc(input)) != EOF) {
        if (++bytes > IMPORT_MAX_BYTES || used == sizeof(line)) {
            fprintf(stderr, "input exceeds file/line limit at line %zu\n",
                    stats->lines + 1U);
            return -1;
        }
        line[used++] = (char)ch;
        if (ch != '\n') continue;
        ++stats->lines;

        size_t body = used - 1U;
        if (body > 0U && line[body - 1U] == '\r') --body;
        for (size_t i = 0U; i < body; ++i) {
            unsigned char byte = (unsigned char)line[i];
            if (byte < 0x20U || byte > 0x7eU) {
                fprintf(stderr, "invalid text byte at line %zu\n", stats->lines);
                return -1;
            }
        }

        bms_telemetry_t telemetry;
        bms_telemetry_result_t result =
            bms_telemetry_parse(line, used, &telemetry);
        if (result == BMS_TELEMETRY_NOT_SAMPLE &&
            !(body >= 3U && memcmp(line, "BMS", 3U) == 0)) {
            ++stats->skipped;
        } else if (result != BMS_TELEMETRY_OK) {
            fprintf(stderr, "invalid BMS sample at line %zu: %d\n",
                    stats->lines, (int)result);
            return -1;
        } else {
            rows[pending++] = (bms_store_row_t){
                .device_id = device,
                .session_id = session,
                /* 历史日志没有逐条时间戳：0明确表示未知，不伪造时间。 */
                .received_unix_us = 0,
                .telemetry = telemetry
            };
            if (pending == BMS_STORE_BATCH_MAX &&
                flush_batch(store, rows, &pending, stats) != 0) return -1;
        }
        used = 0U;
    }

    if (ferror(input)) {
        fprintf(stderr, "input read failed\n");
        return -1;
    }
    if (used != 0U) {
        fprintf(stderr, "unterminated final line %zu\n", stats->lines + 1U);
        return -1;
    }
    if (stats->imported + pending == 0U) {
        fprintf(stderr, "no BMS samples found\n");
        return -1;
    }
    return flush_batch(store, rows, &pending, stats);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: bms-import INPUT_LOG NEW_DATABASE DEVICE_ID SESSION_ID");
        puts("Regular input only, max 16 MiB; complete LF/CRLF lines required.");
        puts("received_unix_us=0 means unknown; no UART or TCP access.");
        return EXIT_SUCCESS;
    }
    if (argc != 5 || argv[2][0] == '\0' ||
        !valid_id(argv[3]) || !valid_id(argv[4])) {
        fprintf(stderr, "Usage: bms-import INPUT_LOG NEW_DATABASE DEVICE_ID SESSION_ID\n");
        return EXIT_FAILURE;
    }

    char temporary[4096];
    int length = snprintf(temporary, sizeof(temporary), "%s.part.XXXXXX", argv[2]);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        fprintf(stderr, "output path is too long\n");
        return EXIT_FAILURE;
    }

    struct stat existing;
    if (lstat(argv[2], &existing) == 0) {
        fprintf(stderr, "output already exists; refusing overwrite\n");
        return EXIT_FAILURE;
    }
    if (errno != ENOENT) {
        perror("check output");
        return EXIT_FAILURE;
    }

    struct stat initial;
    FILE *input = open_regular_input(argv[1], &initial);
    if (input == NULL) {
        perror("input");
        return EXIT_FAILURE;
    }
    int created = 0, status = EXIT_FAILURE;
    bms_store_t store = {0};
    import_stats_t stats = {0};

    int fd = mkstemp(temporary);
    if (fd == -1) {
        perror("temporary database");
        goto done;
    }
    created = 1;
    if (close(fd) == -1) {
        perror("close temporary file");
        goto done;
    }
    int rc = bms_store_open(&store, temporary);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "database open: %s (%d)\n", sqlite3_errstr(rc), rc);
        goto done;
    }
    if (import_stream(input, &store, argv[3], argv[4], &stats) != 0) goto done;

    struct stat after;
    if (fstat(fileno(input), &after) == -1 ||
        after.st_size != initial.st_size ||
        after.st_mtim.tv_sec != initial.st_mtim.tv_sec ||
        after.st_mtim.tv_nsec != initial.st_mtim.tv_nsec ||
        after.st_ctim.tv_sec != initial.st_ctim.tv_sec ||
        after.st_ctim.tv_nsec != initial.st_ctim.tv_nsec) {
        fprintf(stderr, "input changed during import or stat failed\n");
        goto done;
    }
    int input_rc = fclose(input);
    input = NULL;
    if (input_rc != 0) {
        perror("close input");
        goto done;
    }
    rc = bms_store_close(&store);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "database close: %s (%d)\n", sqlite3_errstr(rc), rc);
        goto done;
    }

    /* Linux无覆盖发布；不支持此操作时失败，不退回可覆盖的rename。 */
    if (renameat2(AT_FDCWD, temporary, AT_FDCWD, argv[2],
                  RENAME_NOREPLACE) == -1) {
        perror("publish database");
        goto done;
    }
    created = 0;
    if (printf("input_lines=%zu\nimported_rows=%zu\nskipped_lines=%zu\n"
               "received_unix_us=0\n",
               stats.lines, stats.imported, stats.skipped) < 0 ||
        fflush(stdout) != 0) {
        perror("write summary; database was already published");
        goto done;
    }
    status = EXIT_SUCCESS;

done:
    if (input != NULL) (void)fclose(input);
    if (bms_store_close(&store) != SQLITE_OK) {
        fprintf(stderr, "database cleanup failed\n");
        status = EXIT_FAILURE;
    }
    if (created) {
        fprintf(stderr, "INCOMPLETE database retained: %s\n", temporary);
    }
    return status;
}