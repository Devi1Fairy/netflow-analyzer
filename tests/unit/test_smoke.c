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
    TEST_CHECK(context.stop_requested == 0);
    TEST_CHECK(context.command == APP_COMMAND_HELP);
    TEST_CHECK(context.capture_path == NULL);
    TEST_CHECK(context.csv_output_path == NULL);
    TEST_CHECK(context.interface_name == NULL);
    TEST_CHECK(context.filter_expression == NULL);
    TEST_CHECK(context.active_capture == NULL);
    TEST_CHECK(context.packet_limit == 0U);
    TEST_CHECK(context.error_message[0] == '\0');
    TEST_CHECK(context.flow_full_policy == APP_FLOW_FULL_POLICY_REJECT);

    TEST_CHECK(app_request_stop(&context) == 0);

    TEST_CHECK(context.stop_requested != 0);
    TEST_CHECK(context.active_capture == NULL);

    /*
     * 已请求停止后，app_run应该拒绝继续执行。
     */
    TEST_CHECK(app_run(&context) == ECANCELED);

    app_cleanup(&context);

    TEST_CHECK(!context.initialized);
    TEST_CHECK(context.capture_path == NULL);
    TEST_CHECK(context.stop_requested == 0);
    TEST_CHECK(context.error_message[0] == '\0');
    TEST_CHECK(context.csv_output_path == NULL);
    TEST_CHECK(context.interface_name == NULL);
    TEST_CHECK(context.filter_expression == NULL);
    TEST_CHECK(context.active_capture == NULL);
    TEST_CHECK(context.packet_limit == 0U);
    TEST_CHECK(context.flow_full_policy == APP_FLOW_FULL_POLICY_REJECT);

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

    TEST_CHECK(strcmp(app_version(), "0.2.0") == 0);

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
 * @brief 验证实时网卡和数据包数量能够保存到应用上下文。
 */
static int test_live_interface_command(void)
{
    app_context_t context;

    char *arguments[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
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
            APP_COMMAND_CAPTURE_INTERFACE
    );

    TEST_CHECK(
        context.interface_name ==
            arguments[2]
    );

    TEST_CHECK(
        strcmp(context.interface_name, "lo") == 0
    );

    TEST_CHECK(context.packet_limit == 4U);
    TEST_CHECK(context.capture_path == NULL);
    TEST_CHECK(context.csv_output_path == NULL);
    TEST_CHECK(context.filter_expression == NULL);

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证实时抓包能够保存BPF表达式并在重新解析时清除它。
 */
static int test_live_interface_with_filter_command(void)
{
    app_context_t context;

    char *live_arguments[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--filter",
        "host 127.0.0.1 and icmp",
        NULL
    };

    char *offline_arguments[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(
            &context,
            7,
            live_arguments
        ) == 0
    );

    TEST_CHECK(
        context.command ==
            APP_COMMAND_CAPTURE_INTERFACE
    );

    /*
     * context直接借用argv[6]，没有复制或取得字符串所有权。
     */
    TEST_CHECK(
        context.filter_expression ==
            live_arguments[6]
    );

    TEST_CHECK(
        strcmp(
            context.filter_expression,
            "host 127.0.0.1 and icmp"
        ) == 0
    );

    TEST_CHECK(context.packet_limit == 4U);
    TEST_CHECK(context.capture_path == NULL);
    TEST_CHECK(context.csv_output_path == NULL);

    /*
     * 同一个上下文可以重新解析参数。
     *
     * 第二次解析没有--filter，因此必须清除第一次保存的借用指针，
     * 不能把旧过滤器错误地带入离线命令。
     */
    TEST_CHECK(
        app_parse_arguments(
            &context,
            3,
            offline_arguments
        ) == 0
    );

    TEST_CHECK(
        context.command ==
            APP_COMMAND_READ_CAPTURE
    );

    TEST_CHECK(context.filter_expression == NULL);
    TEST_CHECK(context.interface_name == NULL);
    TEST_CHECK(context.packet_limit == 0U);

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证BPF过滤参数的缺失、重复和错误组合。
 */
static int test_invalid_filter_arguments(void)
{
    app_context_t context;

    char *missing_expression[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--filter",
        NULL
    };

    char *empty_expression[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--filter",
        "",
        NULL
    };

    char *duplicate_filter[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--filter",
        "icmp",
        "--filter",
        "tcp",
        NULL
    };

    char *offline_filter[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        "--filter",
        "icmp",
        NULL
    };

    char *filter_without_source[] = {
        "netflow-analyzer",
        "--filter",
        "icmp",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(
            &context,
            6,
            missing_expression
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            7,
            empty_expression
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            9,
            duplicate_filter
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            5,
            offline_filter
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            3,
            filter_without_source
        ) == EINVAL
    );

    /*
     * 失败的解析不能向context发布部分过滤状态。
     */
    TEST_CHECK(context.filter_expression == NULL);

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证实时抓包参数的缺失、格式和组合错误。
 */
static int test_invalid_live_arguments(void)
{
    app_context_t context;

    char *missing_count[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        NULL
    };

    char *zero_count[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "0",
        NULL
    };

    char *invalid_count[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "four",
        NULL
    };

    char *count_with_offline_file[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        "--count",
        "4",
        NULL
    };

    char *mixed_sources[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        "--interface",
        "lo",
        NULL
    };

    char *live_with_csv[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--csv",
        "flows.csv",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(
            &context,
            3,
            missing_count
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            5,
            zero_count
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            5,
            invalid_count
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            5,
            count_with_offline_file
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            5,
            mixed_sources
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            7,
            live_with_csv
        ) == EINVAL
    );

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证app_run拒绝绕过参数解析构造的非法过滤状态。
 *
 * 两个场景都在打开网卡或PCAP前返回，因此不依赖root或测试文件。
 */
static int test_filter_run_validation(void)
{
    app_context_t context;

    TEST_CHECK(app_context_init(&context) == 0);

    /*
     * 模拟调用者绕过app_parse_arguments，手工把过滤器放入离线命令。
     */
    context.command = APP_COMMAND_READ_CAPTURE;
    context.capture_path = "sample.pcap";
    context.filter_expression = "icmp";

    TEST_CHECK(app_run(&context) == EINVAL);
    TEST_CHECK(context.error_message[0] != '\0');

    TEST_CHECK(
        strstr(
            context.error_message,
            "only supported for live capture"
        ) != NULL
    );

    app_cleanup(&context);

    TEST_CHECK(app_context_init(&context) == 0);

    /*
     * 非NULL空字符串不是“没有过滤器”，而是非法表达式状态。
     */
    context.command = APP_COMMAND_CAPTURE_INTERFACE;
    context.interface_name = "lo";
    context.packet_limit = 1U;
    context.filter_expression = "";

    TEST_CHECK(app_run(&context) == EINVAL);
    TEST_CHECK(context.error_message[0] != '\0');

    TEST_CHECK(
        strstr(
            context.error_message,
            "filter expression is empty"
        ) != NULL
    );

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证实时满表策略解析和重新解析时恢复默认值。
 */
static int test_live_flow_full_policy_command(void)
{
    app_context_t context;

    char *live_arguments[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--flow-full-policy",
        "evict-oldest",
        NULL
    };

    char *offline_arguments[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        NULL
    };

    char *default_arguments[] = {
        "netflow-analyzer",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    TEST_CHECK(
        app_parse_arguments(
            &context,
            7,
            live_arguments
        ) == 0
    );

    TEST_CHECK(
        context.command ==
            APP_COMMAND_CAPTURE_INTERFACE
    );

    TEST_CHECK(
        context.flow_full_policy ==
            APP_FLOW_FULL_POLICY_EVICT_OLDEST
    );

    /*
     * 无参数命令会提前返回帮助模式。
     * 即使如此，也必须在返回前清除旧策略。
     */
    TEST_CHECK(
        app_parse_arguments(
            &context,
            1,
            default_arguments
        ) == 0
    );

    TEST_CHECK(
        context.command ==
            APP_COMMAND_HELP
    );

    TEST_CHECK(
        context.flow_full_policy ==
            APP_FLOW_FULL_POLICY_REJECT
    );

    /*
     * 再次设置EVICT_OLDEST，用于验证后面的离线重新解析。
     */
    TEST_CHECK(
        app_parse_arguments(
            &context,
            7,
            live_arguments
        ) == 0
    );

    TEST_CHECK(
        context.flow_full_policy ==
            APP_FLOW_FULL_POLICY_EVICT_OLDEST
    );

    /*
     * 第二次命令没有策略参数，必须恢复默认REJECT。
     */
    TEST_CHECK(
        app_parse_arguments(
            &context,
            3,
            offline_arguments
        ) == 0
    );

    TEST_CHECK(
        context.command ==
            APP_COMMAND_READ_CAPTURE
    );

    TEST_CHECK(
        context.flow_full_policy ==
            APP_FLOW_FULL_POLICY_REJECT
    );

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证满表策略的缺失、未知、重复和错误组合。
 */
static int test_invalid_flow_full_policy_arguments(void)
{
    app_context_t context;

    char *missing_value[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--flow-full-policy",
        NULL
    };

    char *empty_value[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--flow-full-policy",
        "",
        NULL
    };

    char *unknown_value[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--flow-full-policy",
        "random",
        NULL
    };

    char *duplicate_policy[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--flow-full-policy",
        "reject",
        "--flow-full-policy",
        "evict-oldest",
        NULL
    };

    char *offline_policy[] = {
        "netflow-analyzer",
        "--read",
        "sample.pcap",
        "--flow-full-policy",
        "evict-oldest",
        NULL
    };

    char *policy_without_source[] = {
        "netflow-analyzer",
        "--flow-full-policy",
        "evict-oldest",
        NULL
    };

    char *valid_eviction_policy[] = {
        "netflow-analyzer",
        "--interface",
        "lo",
        "--count",
        "4",
        "--flow-full-policy",
        "evict-oldest",
        NULL
    };

    TEST_CHECK(app_context_init(&context) == 0);

    /*
     * 先保存非默认策略，再触发解析失败，
     * 才能真正验证失败路径是否清除旧状态。
     */
    TEST_CHECK(
        app_parse_arguments(
            &context,
            7,
            valid_eviction_policy
        ) == 0
    );

    TEST_CHECK(
        context.flow_full_policy ==
            APP_FLOW_FULL_POLICY_EVICT_OLDEST
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            6,
            missing_value
        ) == EINVAL
    );

    TEST_CHECK(
        context.flow_full_policy ==
            APP_FLOW_FULL_POLICY_REJECT
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            7,
            empty_value
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            7,
            unknown_value
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            9,
            duplicate_policy
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            5,
            offline_policy
        ) == EINVAL
    );

    TEST_CHECK(
        app_parse_arguments(
            &context,
            3,
            policy_without_source
        ) == EINVAL
    );

    /*
     * 失败解析不能向context发布局部策略。
     */
    TEST_CHECK(
        context.flow_full_policy ==
            APP_FLOW_FULL_POLICY_REJECT
    );

    app_cleanup(&context);

    return EXIT_SUCCESS;
}

/**
 * @brief 验证app_run拒绝手工构造的非法满表策略状态。
 */
static int test_flow_full_policy_run_validation(void)
{
    app_context_t context;

    TEST_CHECK(app_context_init(&context) == 0);

    /*
     * 离线模式不能启用驱逐策略。
     */
    context.command = APP_COMMAND_READ_CAPTURE;
    context.capture_path = "sample.pcap";
    context.flow_full_policy =
        APP_FLOW_FULL_POLICY_EVICT_OLDEST;

    TEST_CHECK(app_run(&context) == EINVAL);

    TEST_CHECK(
        strstr(
            context.error_message,
            "only supported for live capture"
        ) != NULL
    );

    app_cleanup(&context);

    TEST_CHECK(app_context_init(&context) == 0);

    /*
     * 非法枚举值必须在尝试打开网卡之前被拒绝。
     */
    context.command =
        APP_COMMAND_CAPTURE_INTERFACE;

    context.interface_name = "lo";
    context.packet_limit = 1U;
    context.flow_full_policy =
        (app_flow_full_policy_t)99;

    TEST_CHECK(app_run(&context) == EINVAL);

    TEST_CHECK(
        strstr(
            context.error_message,
            "invalid flow full policy"
        ) != NULL
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

    if (test_live_interface_with_filter_command() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] live interface with filter command\n");

    if (test_invalid_filter_arguments() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] invalid filter arguments\n");

    if (test_filter_run_validation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] filter run validation\n");

    if (test_live_interface_command() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] live interface command\n");

    if (test_invalid_live_arguments() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] invalid live arguments\n");

    if (test_live_flow_full_policy_command() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] live flow full policy command\n");

    if (test_invalid_flow_full_policy_arguments() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] invalid flow full policy arguments\n");

    if (test_flow_full_policy_run_validation() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] flow full policy run validation\n");

    return EXIT_SUCCESS;
}
