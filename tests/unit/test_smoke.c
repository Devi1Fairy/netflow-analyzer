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

    TEST_CHECK(app_request_stop(&context) == 0);

    TEST_CHECK(context.stop_requested);

    /*
     * 已请求停止后，app_run应该拒绝继续执行。
     */
    TEST_CHECK(app_run(&context) == ECANCELED);

    app_cleanup(&context);

    TEST_CHECK(!context.initialized);

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

    TEST_CHECK(strcmp(app_version(), "0.0.1") == 0);

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

    return EXIT_SUCCESS;
}