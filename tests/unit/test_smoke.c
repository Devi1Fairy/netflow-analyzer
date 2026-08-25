#include "analyzer/app.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 检查一个测试条件。
 *
 * assert在Release构建中可能因为NDEBUG而被移除，因此测试使用自己的
 * 检查宏，保证Debug和Release下都会执行。
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
 * @brief 验证应用上下文的初始化、停止和清理。
 */
static int test_context_lifecycle(void)
{
    app_context_t context;

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(context.initialized);
    TEST_CHECK(!context.stop_requested);
    TEST_CHECK(context.command == APP_COMMAND_HELP);
    TEST_CHECK(context.capture_path == NULL);
    TEST_CHECK(context.csv_output_path == NULL);
    TEST_CHECK(context.error_message[0] == '\0');

    TEST_CHECK(app_request_stop(&context) == 0);

    TEST_CHECK(context.stop_requested);

    /*
     * 已请求停止后，app_run应该拒绝继续执行。
     */
    TEST_CHECK(app_run(&context) == ECANCELED);

    app_cleanup(&context);

    TEST_CHECK(!context.initialized);
    TEST_CHECK(context.capture_path == NULL);
    TEST_CHECK(context.error_message[0] == '\0');
    TEST_CHECK(context.csv_output_path == NULL);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证无参数时默认显示帮助。
 */
static int test_default_help_command(void)
{
    app_context_t context;

    char *arguments[] = {
        "netflow-analyzer",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(app_parse_arguments(&context, 1, arguments) == 0);

    TEST_CHECK(context.command == APP_COMMAND_HELP);

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证版本参数和CMake传入的版本号。
 */
static int test_version_command(void)
{
    app_context_t context;

    char *arguments[] = {"netflow-analyzer", "--version", NULL};

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(app_parse_arguments(&context, 2, arguments) == 0);

    TEST_CHECK(context.command == APP_COMMAND_VERSION);

    TEST_CHECK(strcmp(app_version(), "0.1.0") == 0);

    /*
     * app_run会把版本信息写到stdout。
     */
    TEST_CHECK(app_run(&context) == 0);

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证未知参数会被拒绝。
 */
static int test_invalid_argument(void)
{
    app_context_t context;

    char *arguments[] = {
        "netflow-analyzer",
        "--unknown",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(app_parse_arguments(&context, 2, arguments) == EINVAL);

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证离线读取命令及PCAP路径能够正确保存。
 */
static int test_read_capture_command(void)
{
    app_context_t context;

    char *arguments[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(&context, 3, arguments) == 0
    );

    TEST_CHECK(
        context.command ==
            APP_COMMAND_READ_CAPTURE
    );

    /*
     * context借用argv[2]的地址，没有复制字符串。
     */
    TEST_CHECK(context.capture_path == arguments[2]);
    TEST_CHECK(context.csv_output_path == NULL);

    TEST_CHECK(
        strcmp(context.capture_path,
               "sample.pcap") == 0
    );

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证读取不存在的PCAP时能够保存详细错误信息。
 */
static int test_missing_capture_file(void)
{
    app_context_t context;

    char *arguments[] = {
        "netflow-analyzer",
        "--read",
        "/proc/self/netflow-analyzer-missing.pcap",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(&context, 3, arguments) == 0
    );

    TEST_CHECK(app_run(&context) == EIO);

    /*
     * app_run没有直接打印错误，而是把详细原因交给main决定如何显示。
     */
    TEST_CHECK(context.error_message[0] != '\0');

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证--read没有提供文件路径时会被拒绝。
 */
static int test_missing_capture_argument(void)
{
    app_context_t context;

    char *arguments[] = {
        "netflow-analyzer",
        "--read",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(&context, 2, arguments) ==
            EINVAL
    );

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证离线输入和CSV输出参数能够同时保存。
 */
static int test_read_capture_with_csv_command(void)
{
    app_context_t context;

    char *arguments[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        "--csv",
        "flows.csv",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(
            &context,
            5,
            arguments
        ) == 0
    );

    TEST_CHECK(
        context.command ==
            APP_COMMAND_READ_CAPTURE
    );

    /*
     * 两个路径都直接借用argv中的字符串。
     */
    TEST_CHECK(context.capture_path == arguments[2]);
    TEST_CHECK(context.csv_output_path == arguments[4]);

    TEST_CHECK(
        strcmp(
            context.csv_output_path,
            "flows.csv"
        ) == 0
    );

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证缺失或脱离--read使用的CSV参数会被拒绝。
 */
static int test_invalid_csv_arguments(void)
{
    app_context_t context;

    char *missing_csv_path[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        "--csv",
        NULL
    };

    char *csv_without_capture[] = {
        "netflow-analyzer",
        "--csv",
        "flows.csv",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(
            &context,
            4,
            missing_csv_path
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            3,
            csv_without_capture
        ) == EINVAL
    );

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 阶段0冒烟测试入口。
 */
int main(void)
{
    if (test_context_lifecycle() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] context lifecycle\n");

    if (test_default_help_command() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] default help command\n");

    if (test_version_command() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] version command\n");

    if (test_invalid_argument() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] invalid argument\n");

    if (test_read_capture_command() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] read capture command\n");

    if (test_missing_capture_file() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] missing capture file\n");

    if (test_missing_capture_argument() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] missing capture argument\n");

    if (test_read_capture_with_csv_command() !=EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] read capture with CSV command\n");

    if (test_invalid_csv_arguments() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] invalid CSV arguments\n");

    return EXIT_SUCCESS;
}
