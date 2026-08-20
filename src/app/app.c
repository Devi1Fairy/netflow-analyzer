#include "analyzer/app.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * 正常情况下，ANALYZER_VERSION由CMake根据project版本传入。
 *
 * 保留development作为脱离CMake单独编译时的兜底值。
 */
#ifndef ANALYZER_VERSION
#define ANALYZER_VERSION "development"
#endif

/**
 * @brief 打印命令行使用帮助。
 *
 * @param program_name 显示在Usage中的程序名称。
 *
 * @return 输出成功时返回0，输出失败时返回EIO。
 */
static int app_print_help(const char *program_name)
{
    const char *display_name = program_name != NULL ? program_name : "netflow-analyzer";

    if (printf(
            "Usage: %s [OPTION]\n"
            "\n"
            "Linux network traffic analyzer.\n"
            "\n"
            "Options:\n"
            "  -h, --help       Show this help message.\n"
            "  -V, --version    Show program version.\n",
            display_name) < 0) {
        return EIO;
    }

    return 0;
}

int app_context_init(app_context_t *context)
{
    if (context == NULL) {
        return EINVAL;
    }

    /*
     * 复合字面量一次性设置完整的初始状态。
     */
    *context = (app_context_t){
        .command = APP_COMMAND_HELP,
        .program_name = "netflow-analyzer",
        .stop_requested = false,
        .initialized = true
    };

    return 0;
}

int app_parse_arguments(app_context_t *context,
                        int argc,
                        char *argv[])
{
    if (context == NULL ||
        !context->initialized ||
        argc < 1 ||
        argv == NULL ||
        argv[0] == NULL) {
        return EINVAL;
    }

    /*
     * 只借用argv[0]的地址，不复制字符串，也不取得所有权。
     *
     * argv中的字符串在main运行期间始终有效。
     */
    context->program_name = argv[0];

    /*
     * 无参数时显示帮助，而不是报错或崩溃。
     */
    if (argc == 1) {
        context->command = APP_COMMAND_HELP;
        return 0;
    }

    /*
     * 当前阶段每次只接受一个选项。
     */
    if (argc != 2 || argv[1] == NULL) {
        return EINVAL;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        context->command = APP_COMMAND_HELP;
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        context->command = APP_COMMAND_VERSION;
        return 0;
    }

    return EINVAL;
}

int app_run(const app_context_t *context)
{
    if (context == NULL || !context->initialized) {
        return EINVAL;
    }

    if (context->stop_requested) {
        return ECANCELED;
    }

    switch (context->command) {
    case APP_COMMAND_HELP:
        return app_print_help(context->program_name);

    case APP_COMMAND_VERSION:
        if (printf("netflow-analyzer %s\n", app_version()) < 0) {
            return EIO;
        }

        return 0;

    default:
        return EINVAL;
    }
}

int app_request_stop(app_context_t *context)
{
    if (context == NULL || !context->initialized) {
        return EINVAL;
    }

    context->stop_requested = true;

    return 0;
}

void app_cleanup(app_context_t *context)
{
    if (context == NULL) {
        return;
    }

    /*
     * 当前没有需要close或free的资源。
     *
     * 清空结构体可以让后续误用更容易通过initialized检查发现。
     */
    *context = (app_context_t){0};
}

const char *app_version(void)
{
    /*
     * 字符串字面量具有静态存储期，会一直存在到进程结束。
     */
    return ANALYZER_VERSION;
}