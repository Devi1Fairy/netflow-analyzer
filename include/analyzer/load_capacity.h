#ifndef NETFLOW_ANALYZER_LOAD_CAPACITY_H
#define NETFLOW_ANALYZER_LOAD_CAPACITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 最多评估6个未来窗口，不分配动态内存。 */
#define LOAD_CAPACITY_MAX_STEPS 6U

/**
 * 容量数据的来源。
 *
 * UNVERIFIED：尚未标定，不能进行容量判断。
 * SIMULATED：模拟容量，只用于软件实验。
 * MEASURED：来自测量，具体证据由后续配置加载层核对。
 */
typedef enum {
    LOAD_CAPACITY_SOURCE_UNVERIFIED = 0,
    LOAD_CAPACITY_SOURCE_SIMULATED,
    LOAD_CAPACITY_SOURCE_MEASURED
} load_capacity_source_t;

/**
 * 容量评估结果。
 *
 * WITHIN_PPS_BUDGET只表示未超过PPS预算，
 * 不代表带宽、内存、磁盘和整个系统均正常。
 */
typedef enum {
    LOAD_CAPACITY_FORECAST_UNAVAILABLE = 0,
    LOAD_CAPACITY_PROFILE_UNVERIFIED,
    LOAD_CAPACITY_PROFILE_MISMATCH,
    LOAD_CAPACITY_WITHIN_PPS_BUDGET,
    LOAD_CAPACITY_PPS_RISK
} load_capacity_status_t;

/**
 * 容量配置。
 *
 * safe_pps：
 *     已经包含安全余量的PPS预算。
 *
 * workload_id：
 *     标识适用的协议、包长、流规模、程序构建和处理配置类别。
 *     预测与容量配置必须属于同一类别。
 *
 * window_seconds：
 *     适用的持续负载窗口。
 *     不能把短时间微基准当作长期处理能力。
 *
 * source：
 *     只是调用者声明的来源。
 *     测量证据及配置指纹由后续配置加载层核对。
 */
typedef struct {
    load_capacity_source_t source;
    uint64_t workload_id;
    uint32_t window_seconds;
    double safe_pps;
} load_capacity_profile_t;

/**
 * 未来窗口的包数预测。
 *
 * predicted_packets[0]：
 *     下一个完整窗口的预测包数，不是PPS。
 *
 * predicted_packets允许非整数，但不允许负数、NaN或Inf。
 *
 * input_complete：
 *     输入历史完整、及时且采集质量可信时为true。
 *     历史不足、数据失效或采集质量不可信时必须为false。
 *
 * 本模块不检查实际时间戳，也不决定模型需要多少历史；
 * 这些由后续模型适配层负责。
 */
typedef struct {
    uint64_t workload_id;
    uint32_t window_seconds;
    size_t step_count;
    double predicted_packets[LOAD_CAPACITY_MAX_STEPS];
    bool input_complete;
} load_capacity_forecast_t;

/**
 * 容量评估输出。
 */
typedef struct {
    load_capacity_status_t status;

    /*
     * 来源必须随事件保存和显示。
     * SIMULATED不能作为真实容量验收结果。
     */
    load_capacity_source_t source;

    /*
     * 第一个存在风险的未来窗口编号，从1开始。
     * 0表示没有发现越界。
     *
     * 它不是精确越界时间。
     */
    size_t first_risk_step;

    double peak_predicted_pps;
    double peak_budget_ratio;
} load_capacity_result_t;

/**
 * 将未来窗口包数转换为平均PPS，与容量预算比较。
 *
 * 达到预算（>=）即提示风险。
 *
 * 输入对象只借用到本次调用返回：
 * 不保存指针、不写文件、不操作硬件。
 *
 * 返回值：
 *     0：成功生成结果，包括不可评估状态。
 *     EINVAL：参数或字段非法。
 *     EOVERFLOW：计算结果无法表示。
 *
 * 返回非0时，result保持原值。
 * 调用者不能把保留的旧结果当成本次结果。
 *
 * result不得与任一输入对象重叠。
 */
int load_capacity_assess(
    const load_capacity_profile_t *profile,
    const load_capacity_forecast_t *forecast,
    load_capacity_result_t *result);

#endif