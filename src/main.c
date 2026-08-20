#include "analyzer/app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 网络流量分析器主入口。
 *
 * main只负责：
 *
 * 1. 初始化应用上下文；
 * 2. 解析命令行参数；
 * 3. 运行应用；
 * 4. 清理上下文；
 * 5. 将内部错误转换成进程退出状态。
 *
 * 具体业务功能放在app及后续模块中，避免main不断膨胀。
 *
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 *
 * @return 成功时返回EXIT_SUCCESS，否则返回EXIT_FAILURE。
 */
int main(int argc, char *argv[])
{
    app_context_t context;

    int error_code;

    error_code = app_context_init(&context);

    if (error_code != 0) {
        fprintf(stderr, "Failed to initialize application: %s\n",
                strerror(error_code));

        return EXIT_FAILURE;
    }

    error_code = app_parse_arguments(&context, argc, argv);

    if (error_code != 0) {
        fprintf(stderr,
                "Invalid arguments. "
                "Run '%s --help' for usage.\n",
                context.program_name);

        app_cleanup(&context);

        return EXIT_FAILURE;
    }

    error_code = app_run(&context);

    if (error_code != 0) {
        fprintf(stderr,
                "Application failed: %s\n",
                strerror(error_code));

        app_cleanup(&context);

        return EXIT_FAILURE;
    }

    app_cleanup(&context);

    return EXIT_SUCCESS;
}