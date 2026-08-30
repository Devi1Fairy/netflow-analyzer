#ifndef NETFLOW_ANALYZER_APP_H
#define NETFLOW_ANALYZER_APP_H

#include "analyzer/capture.h"

#include <stdbool.h>
#include <stddef.h>
#include <signal.h>

/*
 * 应用层错误信息缓冲区大小。
 *
 * app_run会把底层模块的错误转换成带有操作上下文的消息，
 * main只负责统一显示。
 */
#define APP_ERROR_MESSAGE_SIZE 512U

/**
 * @brief 表示主程序本次准备执行的命令。
 *
 * 当前阶段只支持帮助和版本信息。后续会增加离线PCAP分析和实时抓包。
 */
typedef enum {
    APP_COMMAND_HELP = 0,
    APP_COMMAND_VERSION,

    /**
     * 从离线PCAP文件读取并显示数据包概要。
     */
    APP_COMMAND_READ_CAPTURE,

    /**
     * 从实时网卡读取并分析有限数量的数据包。
     */
    APP_COMMAND_CAPTURE_INTERFACE
} app_command_t;

/**
 * @brief 表示流表容量耗尽时采用的应用层策略。
 *
 * 该策略属于应用编排，不改变flow_table_process_packet本身的
 * ENOSPC语义。
 */
typedef enum {
    /**
     * 保持现有行为：拒绝无法创建的新流数据包并继续运行。
     */
    APP_FLOW_FULL_POLICY_REJECT = 0,

    /**
     * 驱逐last_seen最早的流，然后重新尝试处理当前数据包。
     */
    APP_FLOW_FULL_POLICY_EVICT_OLDEST
} app_flow_full_policy_t;

/**
 * @brief 保存应用程序运行期间共享的顶层状态。
 *
 * 线程、抓包句柄、输出模块等资源以后都会逐步加入这个上下文。
 */
typedef struct {
    /**
     * 本次准备执行的命令。
     */
    app_command_t command;

    /**
     * 程序名称，通常借用argv[0]中的字符串地址。
     *
     * 上下文不拥有这个字符串，因此不能对它调用free。
     */
    const char *program_name;

    /**
     * 离线PCAP文件路径。
     *
     * 该指针借用argv中的字符串地址，不拥有字符串，也不能free。
     *
     * 只有command为APP_COMMAND_READ_CAPTURE时才应该使用。
     */
    const char *capture_path;

    /**
     * 实时抓包使用的网络接口名称，例如lo或eth0。
     *
     * 该指针借用argv中的字符串地址，不拥有字符串，也不能free。
     *
     * 只有command为APP_COMMAND_CAPTURE_INTERFACE时才应该使用。
     */
    const char *interface_name;

    /**
     * 实时抓包使用的可选BPF过滤表达式。
     *
     * 该指针借用argv中的字符串地址，不拥有字符串，也不能free。
     *
     * NULL表示不安装过滤器。第一版只允许与
     * APP_COMMAND_CAPTURE_INTERFACE组合。
     */
    const char *filter_expression;

    /**
     * 实时流表容量耗尽时采用的策略。
     *
     * 默认值为APP_FLOW_FULL_POLICY_REJECT。
     * 第一版只允许实时抓包命令显式设置该策略。
     */
    app_flow_full_policy_t flow_full_policy;

    /**
     * 实时抓包最多处理的数据包数量。
     *
     * 只有command为APP_COMMAND_CAPTURE_INTERFACE时才使用。
     * 0表示尚未提供合法的--count参数。
     */
    size_t packet_limit;

    /**
     * CSV流记录输出文件路径。
     *
     * 该指针借用argv中的字符串地址，不拥有字符串，也不能free。
     *
     * NULL表示只显示终端文本，不生成CSV文件。
     */
    const char *csv_output_path;

    /**
     * 当前正在使用的采集对象。
     *
     * 该指针只在采集读取循环运行期间临时借用，不拥有capture对象，
     * 不能通过它释放资源。
     *
     * NULL表示当前没有正在运行的采集读取。
     *
     * 关闭采集对象前必须先将本字段恢复为NULL，避免停止请求访问
     * 已经释放的capture。
     */
    capture_t *active_capture;

    /**
     * 保存应用层最近一次可读错误说明。
     *
     * 该数组属于app_context_t本身，不需要单独free。
     */
    char error_message[APP_ERROR_MESSAGE_SIZE];

    /**
     * 非0表示程序已经收到停止请求。
     *
     * 使用volatile sig_atomic_t，使信号处理函数能够以最小操作
     * 通知普通应用控制流停止。
     *
     * 该字段不是完整的多线程同步机制。
     */
    volatile sig_atomic_t stop_requested;

    /**
     * true表示上下文已经成功初始化。
     *
     * 其他接口通过该字段避免使用未初始化的上下文。
     */
    bool initialized;
} app_context_t;

/**
 * @brief 初始化应用上下文和默认值。
 *
 * 默认命令是显示帮助，默认程序名称是netflow-analyzer。
 *
 * @param context 指向待初始化的应用上下文。
 *
 * @return 成功时返回0，参数无效时返回EINVAL。
 */
int app_context_init(app_context_t *context);

/**
 * @brief 解析主程序收到的命令行参数。
 *
 * 当前支持：
 *
 * - 无参数；
 * - --help或-h；
 * - --version或-V。
 * - --read FILE或-r FILE
 * - --read FILE --csv CSV_FILE
 * - --interface NAME --count PACKETS [--filter EXPRESSION] [--flow-full-policy reject|evict-oldest]
 *
 * @param context 指向已经初始化的应用上下文。
 * @param argc main函数收到的参数数量。
 * @param argv main函数收到的参数数组。
 *
 * @return 成功时返回0，参数无效或命令未知时返回EINVAL。
 */
int app_parse_arguments(app_context_t *context,
                        int argc,
                        char *argv[]);

/**
 * @brief 根据解析结果执行当前命令。
 *
 * @param context 指向已经完成参数解析的上下文。
 *
 * @return 成功时返回0；
 *         上下文无效时返回EINVAL；
 *         已请求停止时返回ECANCELED；
 *         输出失败时返回EIO。
 */
int app_run(app_context_t *context);

/**
 * @brief 请求应用程序停止。
 *
 * 设置停止标志；如果当前存在活动采集对象，同时请求中断正在进行的libpcap读取。
 *
 * 本函数不直接关闭capture或释放其他资源。读取循环恢复控制后，仍由正常应用流程完成输出和清理。
 *
 * @param context 指向已经初始化的上下文。
 *
 * @return 成功时返回0，上下文无效时返回EINVAL。
 */
int app_request_stop(app_context_t *context);

/**
 * @brief 清理应用上下文。
 *
 * 当前阶段没有动态资源，因此只清空状态。后续会按照资源初始化的
 * 相反顺序关闭文件、抓包句柄、队列和线程。
 *
 * 重复调用是安全的。
 *
 * @param context 指向应用上下文，可以为NULL。
 */
void app_cleanup(app_context_t *context);

/**
 * @brief 获取程序版本字符串。
 *
 * @return 指向只读静态字符串的指针，调用者不能free或修改。
 */
const char *app_version(void);

#endif