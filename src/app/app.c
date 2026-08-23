#include "analyzer/app.h"
#include "analyzer/capture.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/*
 * 当前命令只显示前5个数据包，避免大型PCAP在终端输出数万行。
 */
#define APP_CAPTURE_PREVIEW_LIMIT 5U

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
            "       %s --read <PCAP_FILE>\n"
            "\n"
            "Linux network traffic analyzer.\n"
            "\n"
            "Options:\n"
            "  -h, --help       Show this help message.\n"
            "  -V, --version    Show program version.\n"
            "  -r, --read FILE  Preview packets from an offline PCAP.\n",
            display_name,
            display_name) < 0) {
        return EIO;
    }

    return 0;
}

/**
 * @brief 打开离线PCAP并输出前几个数据包的元数据。
 *
 * capture_t只在本函数执行期间使用，因此作为局部资源管理，
 * 不需要长期保存在app_context_t中。
 */
static int app_run_capture_preview(app_context_t *context)
{
    char capture_error[CAPTURE_ERROR_BUFFER_SIZE] = {0};

    capture_t *capture = NULL;
    capture_link_type_t link_type = CAPTURE_LINK_TYPE_UNKNOWN;

    capture_packet_view_t packet;
    capture_read_status_t read_status;

    size_t displayed_packet_count = 0U;
    int error_code;

    error_code = capture_open_offline(
        context->capture_path,
        &capture,
        capture_error,
        sizeof(capture_error)
    );

    if (error_code != 0) {
        /*
         * 把底层错误加上文件路径，形成适合最终用户阅读的应用错误。
         */
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to open capture '%s': %s",
            context->capture_path,
            capture_error[0] != '\0' ? capture_error : "unknown libpcap error"
        );

        return error_code;
    }

    error_code = capture_get_link_type(capture, &link_type);

    if (error_code != 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to query capture link type"
        );

        capture_close(&capture);
        return error_code;
    }

    if (link_type != CAPTURE_LINK_TYPE_ETHERNET) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "capture uses an unsupported link-layer type"
        );

        capture_close(&capture);
        return ENOTSUP;
    }

    if (printf(
            "Capture file: %s\n"
            "Link type: Ethernet\n",
            context->capture_path) < 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to write capture information"
        );

        capture_close(&capture);
        return EIO;
    }

    while (displayed_packet_count < APP_CAPTURE_PREVIEW_LIMIT) {
        read_status = CAPTURE_READ_STATUS_UNKNOWN;

        error_code = capture_next_packet(
            capture,
            &packet,
            &read_status
        );

        if (error_code != 0) {
            const char *read_error = capture_get_error(capture);

            /*
             * capture_get_error返回的字符串依赖capture句柄，
             * 必须在capture_close之前复制。
             */
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to read capture '%s': %s",
                context->capture_path,
                read_error != NULL && read_error[0] != '\0' ? read_error : "unknown libpcap error"
            );

            capture_close(&capture);
            return error_code;
        }

        if (read_status == CAPTURE_READ_STATUS_END_OF_FILE) {
            break;
        }

        if (read_status != CAPTURE_READ_STATUS_PACKET) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "capture returned an unexpected read status"
            );

            capture_close(&capture);
            return EIO;
        }

        displayed_packet_count += 1U;

        if (printf(
                "Packet %zu: "
                "timestamp=%" PRId64 ".%06" PRId32 " "
                "caplen=%" PRIu32 " "
                "wirelen=%" PRIu32 "\n",
                displayed_packet_count,
                packet.timestamp_seconds,
                packet.timestamp_microseconds,
                packet.captured_length,
                packet.wire_length) < 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to write packet information"
            );

            capture_close(&capture);
            return EIO;
        }

        /*
         * 当前循环只使用元数据，不保存packet.data。
         *
         * 下一次capture_next_packet可能使本次packet.data失效。
         */
    }

    if (printf("Displayed packets: %zu\n", displayed_packet_count) < 0) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "failed to write packet count"
        );

        capture_close(&capture);
        return EIO;
    }

    capture_close(&capture);

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
        .capture_path = NULL,
        .error_message = {0},
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
    context->capture_path = NULL;
    context->error_message[0] = '\0';
    /*
     * 无参数时显示帮助，而不是报错或崩溃。
     */
    if (argc == 1) {
        context->command = APP_COMMAND_HELP;
        return 0;
    }

    /*
     * help和version命令只接受一个选项。
     */
    if (argc == 2 && argv[1] != NULL) {
        if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0) {
            context->command = APP_COMMAND_HELP;
            return 0;
        }

        if (strcmp(argv[1], "--version") == 0 ||
            strcmp(argv[1], "-V") == 0) {
            context->command = APP_COMMAND_VERSION;
            return 0;
        }

        return EINVAL;
    }

    /*
     * 离线读取命令需要一个额外的PCAP文件路径：
     *
     * netflow-analyzer --read input.pcap
     */
    if (argc == 3 &&
        argv[1] != NULL &&
        argv[2] != NULL &&
        argv[2][0] != '\0' &&
        (strcmp(argv[1], "--read") == 0 ||
         strcmp(argv[1], "-r") == 0)) {
        context->command = APP_COMMAND_READ_CAPTURE;

        /*
         * argv中的字符串由进程启动环境管理。
         * context只借用地址，不复制、不free。
         */
        context->capture_path = argv[2];

        return 0;
    }

    return EINVAL;
}

int app_run(app_context_t *context)
{
    int error_code;

    if (context == NULL || !context->initialized) {
        return EINVAL;
    }

    /*
     * 每次运行前清空上一次错误。
     */
    context->error_message[0] = '\0';

    if (context->stop_requested) {
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "application stop has been requested"
        );

        return ECANCELED;
    }

    switch (context->command) {
    case APP_COMMAND_HELP:
        error_code = app_print_help(context->program_name);

        if (error_code != 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to write help information"
            );
        }

        return error_code;

    case APP_COMMAND_VERSION:
        if (printf("netflow-analyzer %s\n", app_version()) < 0) {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "failed to write version information"
            );

            return EIO;
        }

        return 0;

    case APP_COMMAND_READ_CAPTURE:
        if (context->capture_path == NULL || context->capture_path[0] == '\0') {
            (void)snprintf(
                context->error_message,
                sizeof(context->error_message),
                "capture path is missing"
            );

            return EINVAL;
        }

        return app_run_capture_preview(context);

    default:
        (void)snprintf(
            context->error_message,
            sizeof(context->error_message),
            "unknown application command"
        );

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