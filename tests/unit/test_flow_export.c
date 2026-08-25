#include "analyzer/flow_export.h"
#include "analyzer/ipv4.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 在Debug和Release构建中都有效的测试检查宏。
 */
#define TEST_CHECK(condition)                                      \
    do {                                                           \
        if (!(condition)) {                                        \
            fprintf(stderr,                                        \
                    "[FAIL] %s:%d: %s\n",                          \
                    __FILE__,                                      \
                    __LINE__,                                      \
                    #condition);                                   \
            return EXIT_FAILURE;                                   \
        }                                                          \
    } while (false)

/**
 * @brief 从测试用临时流的开头读取全部文本。
 *
 * buffer_size包含字符串结束符所需空间。
 */
static int read_stream_text(
    FILE *stream,
    char *buffer,
    size_t buffer_size)
{
    size_t bytes_read;
    int next_character;

    if (stream == NULL ||
        buffer == NULL ||
        buffer_size == 0U) {
        return EINVAL;
    }

    /*
     * 把标准库输出缓冲区中的数据提交到底层临时文件。
     */
    if (fflush(stream) != 0) {
        return EIO;
    }

    /*
     * 写入后文件位置位于末尾。
     * 读取前必须把位置重新移动到文件开头。
     */
    if (fseek(stream, 0L, SEEK_SET) != 0) {
        return EIO;
    }

    bytes_read = fread(
        buffer,
        sizeof(char),
        buffer_size - 1U,
        stream
    );

    if (ferror(stream)) {
        return EIO;
    }

    /*
     * 缓冲区刚好被填满时，再读取一个字符判断是否发生截断。
     */
    if (bytes_read == buffer_size - 1U) {
        next_character = fgetc(stream);

        if (next_character != EOF) {
            return ENOSPC;
        }

        if (ferror(stream)) {
            return EIO;
        }
    }

    buffer[bytes_read] = '\0';

    return 0;
}

/**
 * @brief 验证CSV表头字段和顺序。
 */
static int test_csv_header(void)
{
    static const char expected_header[] =
        "protocol,"
        "endpoint_a_ip,"
        "endpoint_a_port,"
        "endpoint_b_ip,"
        "endpoint_b_port,"
        "a_to_b_packets,"
        "a_to_b_captured_bytes,"
        "a_to_b_wire_bytes,"
        "b_to_a_packets,"
        "b_to_a_captured_bytes,"
        "b_to_a_wire_bytes,"
        "first_seen_seconds,"
        "first_seen_microseconds,"
        "last_seen_seconds,"
        "last_seen_microseconds\n";

    char actual_text[512];
    FILE *output;

    output = tmpfile();
    TEST_CHECK(output != NULL);

    TEST_CHECK(
        flow_export_write_csv_header(output) == 0
    );

    TEST_CHECK(
        read_stream_text(
            output,
            actual_text,
            sizeof(actual_text)
        ) == 0
    );

    TEST_CHECK(
        strcmp(actual_text, expected_header) == 0
    );

    TEST_CHECK(fclose(output) == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证完整流记录被格式化为确定的CSV行。
 */
static int test_csv_record(void)
{
    static const char expected_record[] =
        "6,10.0.0.1,12345,10.0.0.2,443,"
        "3,180,200,"
        "2,120,140,"
        "100,123456,"
        "105,654321\n";

    flow_record_t record = {
        .key = {
            .endpoint_a = {
                .ipv4_address = UINT32_C(0x0A000001),
                .port = UINT16_C(12345)
            },
            .endpoint_b = {
                .ipv4_address = UINT32_C(0x0A000002),
                .port = UINT16_C(443)
            },
            .protocol = IPV4_PROTOCOL_TCP
        },
        .a_to_b = {
            .packet_count = UINT64_C(3),
            .captured_byte_count = UINT64_C(180),
            .wire_byte_count = UINT64_C(200)
        },
        .b_to_a = {
            .packet_count = UINT64_C(2),
            .captured_byte_count = UINT64_C(120),
            .wire_byte_count = UINT64_C(140)
        },
        .first_seen = {
            .seconds = INT64_C(100),
            .microseconds = INT32_C(123456)
        },
        .last_seen = {
            .seconds = INT64_C(105),
            .microseconds = INT32_C(654321)
        },
        .initialized = true
    };

    char actual_text[512];
    FILE *output;

    output = tmpfile();
    TEST_CHECK(output != NULL);

    TEST_CHECK(
        flow_export_write_csv_record(
            output,
            &record
        ) == 0
    );

    TEST_CHECK(
        read_stream_text(
            output,
            actual_text,
            sizeof(actual_text)
        ) == 0
    );

    TEST_CHECK(
        strcmp(actual_text, expected_record) == 0
    );

    TEST_CHECK(fclose(output) == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证无效参数不会产生CSV内容。
 */
static int test_csv_argument_validation(void)
{
    flow_record_t invalid_record = {0};

    char actual_text[16];
    FILE *output;

    output = tmpfile();
    TEST_CHECK(output != NULL);

    TEST_CHECK(
        flow_export_write_csv_header(NULL) ==
            EINVAL
    );

    TEST_CHECK(
        flow_export_write_csv_record(
            NULL,
            &invalid_record
        ) == EINVAL
    );

    TEST_CHECK(
        flow_export_write_csv_record(
            output,
            NULL
        ) == EINVAL
    );

    /*
     * 未初始化的流记录不能导出。
     */
    TEST_CHECK(
        flow_export_write_csv_record(
            output,
            &invalid_record
        ) == EINVAL
    );

    /*
     * 即使initialized为true，非法微秒字段仍然不能导出。
     */
    invalid_record.initialized = true;
    invalid_record.first_seen.microseconds =
        INT32_C(1000000);

    TEST_CHECK(
        flow_export_write_csv_record(
            output,
            &invalid_record
        ) == EINVAL
    );

    TEST_CHECK(
        read_stream_text(
            output,
            actual_text,
            sizeof(actual_text)
        ) == 0
    );

    TEST_CHECK(strcmp(actual_text, "") == 0);

    TEST_CHECK(fclose(output) == 0);

    return EXIT_SUCCESS;
}

/**
 * @brief CSV流记录导出单元测试入口。
 */
int main(void)
{
    if (test_csv_header() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] CSV header\n");

    if (test_csv_record() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] CSV flow record\n");

    if (test_csv_argument_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] CSV argument validation\n");

    return EXIT_SUCCESS;
}