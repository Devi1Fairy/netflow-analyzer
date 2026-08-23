#include "analyzer/packet_info.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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
 * @brief 验证结果对象能够保存捕获元数据并建立默认解析状态。
 */
static int test_packet_info_initialization(void)
{
    packet_info_t info;

    TEST_CHECK(
        packet_info_init(
            &info,
            INT64_C(1724385600),
            INT32_C(123456),
            UINT32_C(96),
            UINT32_C(1500)
        ) == 0
    );

    TEST_CHECK(info.initialized);
    TEST_CHECK(
        info.timestamp_seconds == INT64_C(1724385600)
    );
    TEST_CHECK(
        info.timestamp_microseconds == INT32_C(123456)
    );
    TEST_CHECK(
        info.captured_length == UINT32_C(96)
    );
    TEST_CHECK(
        info.wire_length == UINT32_C(1500)
    );
    TEST_CHECK(
        info.parse_status ==
            PACKET_PARSE_STATUS_NOT_STARTED
    );
    TEST_CHECK(
        info.error_layer ==
            PACKET_PARSE_LAYER_NONE
    );
    TEST_CHECK(info.error_code == 0);
    TEST_CHECK(info.error_offset == 0U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证非法时间戳不会修改调用者原有对象。
 */
static int test_packet_info_argument_validation(void)
{
    packet_info_t info = {
        .timestamp_seconds = INT64_C(99),
        .initialized = true
    };

    TEST_CHECK(
        packet_info_init(
            NULL,
            INT64_C(0),
            INT32_C(0),
            UINT32_C(0),
            UINT32_C(0)
        ) == EINVAL
    );

    TEST_CHECK(
        packet_info_init(
            &info,
            INT64_C(0),
            INT32_C(-1),
            UINT32_C(0),
            UINT32_C(0)
        ) == EINVAL
    );

    TEST_CHECK(info.timestamp_seconds == INT64_C(99));
    TEST_CHECK(info.initialized);

    TEST_CHECK(
        packet_info_init(
            &info,
            INT64_C(0),
            INT32_C(1000000),
            UINT32_C(0),
            UINT32_C(0)
        ) == EINVAL
    );

    TEST_CHECK(info.timestamp_seconds == INT64_C(99));
    TEST_CHECK(info.initialized);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证截断、畸形和不支持状态能够被统一记录。
 */
static int test_packet_info_error_state(void)
{
    packet_info_t info;

    TEST_CHECK(
        packet_info_init(
            &info,
            INT64_C(100),
            INT32_C(200),
            UINT32_C(10),
            UINT32_C(60)
        ) == 0
    );

    TEST_CHECK(
        packet_info_set_error(
            &info,
            PACKET_PARSE_STATUS_TRUNCATED,
            PACKET_PARSE_LAYER_ETHERNET,
            ENODATA,
            10U
        ) == 0
    );

    TEST_CHECK(
        info.parse_status ==
            PACKET_PARSE_STATUS_TRUNCATED
    );
    TEST_CHECK(
        info.error_layer ==
            PACKET_PARSE_LAYER_ETHERNET
    );
    TEST_CHECK(info.error_code == ENODATA);
    TEST_CHECK(info.error_offset == 10U);

    /*
     * COMPLETE不是错误状态，不能传给packet_info_set_error。
     */
    TEST_CHECK(
        packet_info_set_error(
            &info,
            PACKET_PARSE_STATUS_COMPLETE,
            PACKET_PARSE_LAYER_ETHERNET,
            ENODATA,
            10U
        ) == EINVAL
    );

    /*
     * 失败调用不能破坏之前保存的错误状态。
     */
    TEST_CHECK(
        info.parse_status ==
            PACKET_PARSE_STATUS_TRUNCATED
    );
    TEST_CHECK(info.error_code == ENODATA);

    /*
     * 错误偏移不能超过实际捕获缓冲区长度。
     */
    TEST_CHECK(
        packet_info_set_error(
            &info,
            PACKET_PARSE_STATUS_MALFORMED,
            PACKET_PARSE_LAYER_ETHERNET,
            EBADMSG,
            11U
        ) == EINVAL
    );

    TEST_CHECK(info.error_offset == 10U);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证完成状态和捕获截断判断。
 */
static int test_packet_info_completion_and_truncation(void)
{
    packet_info_t info;

    TEST_CHECK(
        packet_info_init(
            &info,
            INT64_C(1),
            INT32_C(2),
            UINT32_C(96),
            UINT32_C(1500)
        ) == 0
    );

    TEST_CHECK(packet_info_capture_is_truncated(&info));

    TEST_CHECK(
        packet_info_set_error(
            &info,
            PACKET_PARSE_STATUS_TRUNCATED,
            PACKET_PARSE_LAYER_ETHERNET,
            ENODATA,
            14U
        ) == 0
    );

    TEST_CHECK(packet_info_mark_complete(&info) == 0);

    TEST_CHECK(
        info.parse_status ==
            PACKET_PARSE_STATUS_COMPLETE
    );
    TEST_CHECK(
        info.error_layer ==
            PACKET_PARSE_LAYER_NONE
    );
    TEST_CHECK(info.error_code == 0);
    TEST_CHECK(info.error_offset == 0U);

    TEST_CHECK(
        packet_info_init(
            &info,
            INT64_C(1),
            INT32_C(2),
            UINT32_C(64),
            UINT32_C(64)
        ) == 0
    );

    TEST_CHECK(!packet_info_capture_is_truncated(&info));
    TEST_CHECK(!packet_info_capture_is_truncated(NULL));

    return EXIT_SUCCESS;
}

/**
 * @brief packet_info模块单元测试入口。
 */
int main(void)
{
    if (test_packet_info_initialization() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] packet info initialization\n");

    if (test_packet_info_argument_validation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] packet info argument validation\n");

    if (test_packet_info_error_state() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] packet info error state\n");

    if (test_packet_info_completion_and_truncation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] packet info completion and truncation\n");

    return EXIT_SUCCESS;
}