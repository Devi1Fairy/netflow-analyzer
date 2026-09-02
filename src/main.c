#define _POSIX_C_SOURCE 200809L

#include "analyzer/app.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 信号handler不能接收自定义参数，因此使用具有静态存储期的
 * 唯一应用上下文。
 */
static app_context_t application_context;

/*
 * 保存安装前的信号处理方式，实时抓包结束后恢复。
 */
static struct sigaction previous_sigint_action;
static struct sigaction previous_sigterm_action;

static bool stop_signal_handlers_installed = false;

/**
 * @brief 把终止信号转换为应用停止请求。
 *
 * handler中不执行输出、资源释放或复杂清理。
 *
 * app_request_stop只设置sig_atomic_t标志，并通过libpcap允许在
 * UNIX信号处理函数中调用的pcap_breakloop请求中断读取。
 */
static void handle_stop_signal(int signal_number)
{
    (void)signal_number;

    (void)app_request_stop(&application_context);
}

/**
 * @brief 为实时抓包安装SIGINT和SIGTERM处理函数。
 *
 * @return 成功时返回0，系统接口失败时返回errno或EIO。
 */
static int install_stop_signal_handlers(void)
{
    struct sigaction action = {0};
    int error_code;

    action.sa_handler = handle_stop_signal;

    /*
     * handler执行期间同时屏蔽SIGINT和SIGTERM，避免两种停止信号
     * 相互嵌套执行handler。
     */
    errno = 0;

    if (sigemptyset(&action.sa_mask) != 0) {
        return errno != 0 ? errno : EIO;
    }

    errno = 0;

    if (sigaddset(&action.sa_mask, SIGINT) != 0) {
        return errno != 0 ? errno : EIO;
    }

    errno = 0;

    if (sigaddset(&action.sa_mask, SIGTERM) != 0) {
        return errno != 0 ? errno : EIO;
    }

    /*
     * 不设置SA_RESTART。
     *
     * 被信号打断的libpcap底层读取不应自动重新开始。
     */
    action.sa_flags = 0;

    errno = 0;

    if (sigaction(
            SIGINT,
            &action,
            &previous_sigint_action) != 0) {
        return errno != 0 ? errno : EIO;
    }

    errno = 0;

    if (sigaction(
            SIGTERM,
            &action,
            &previous_sigterm_action) != 0) {
        error_code = errno != 0 ? errno : EIO;

        /*
         * SIGTERM安装失败时撤销已经安装的SIGINT handler，
         * 避免留下半完成状态。
         */
        (void)sigaction(
            SIGINT,
            &previous_sigint_action,
            NULL
        );

        return error_code;
    }

    stop_signal_handlers_installed = true;

    return 0;
}

/**
 * @brief 恢复安装前的SIGINT和SIGTERM处理方式。
 */
static void restore_stop_signal_handlers(void)
{
    if (!stop_signal_handlers_installed) {
        return;
    }

    /*
     * 先恢复SIGTERM，再恢复SIGINT。
     *
     * 即使恢复失败，进程也即将结束，因此这里不能安全地通过
     * 复杂错误路径重新进入应用。
     */
    (void)sigaction(
        SIGTERM,
        &previous_sigterm_action,
        NULL
    );

    (void)sigaction(
        SIGINT,
        &previous_sigint_action,
        NULL
    );

    stop_signal_handlers_installed = false;
}

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
    int error_code;

    /*
     * stdout连接终端时通常采用行缓冲，但连接到systemd journal、
     * 管道或普通文件时通常会变成全缓冲。
     *
     * 显式设置为行缓冲，使每条以换行符结束的运行日志及时输出，
     * 而不是等待缓冲区填满或进程退出。
     *
     * setvbuf必须在stdout发生任何I/O操作之前调用。
     */
    if (setvbuf(stdout, NULL, _IOLBF, 0) != 0) {
        fprintf(
            stderr,
            "Failed to configure standard output buffering\n"
        );

        return EXIT_FAILURE;
    }

    error_code = app_context_init(&application_context);

    if (error_code != 0) {
        fprintf(stderr, "Failed to initialize application: %s\n",
                strerror(error_code));

        return EXIT_FAILURE;
    }

    error_code = app_parse_arguments(&application_context, argc, argv);

    if (error_code != 0) {
        fprintf(stderr,
                "Invalid arguments. "
                "Run '%s --help' for usage.\n",
                application_context.program_name);

        app_cleanup(&application_context);

        return EXIT_FAILURE;
    }

    /*
     * 当前只为实时抓包接管SIGINT和SIGTERM。
     *
     * 帮助、版本和离线分析暂时保留系统默认信号行为。
     */
    if (application_context.command ==
        APP_COMMAND_CAPTURE_INTERFACE) {
        error_code = install_stop_signal_handlers();

        if (error_code != 0) {
            fprintf(
                stderr,
                "Failed to install stop signal handlers: %s\n",
                strerror(error_code)
            );

            app_cleanup(&application_context);

            return EXIT_FAILURE;
        }
    }

    error_code = app_run(&application_context);

    /*
     * 在检查结果和清理上下文前恢复信号处理方式。
     */
    restore_stop_signal_handlers();

    if (error_code != 0) {
        const char *error_message = application_context.error_message[0] != '\0' ? application_context.error_message : strerror(error_code);

        fprintf(stderr,
                "Application failed: %s\n",
                error_message);

        app_cleanup(&application_context);

        return EXIT_FAILURE;
    }

     /*
     * 即使程序成功运行，也统一执行应用清理。
     *
     * 当前app_context_t还没有长期持有动态资源，但以后加入线程、
     * 队列和输出文件后，成功路径同样必须释放这些资源。
     */
    app_cleanup(&application_context);

    return EXIT_SUCCESS;
}
