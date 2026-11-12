#include "analyzer/load_capacity.h"

#include <errno.h>
#include <stdint.h>

/*
 * ABI版本1：
 * 600秒窗口、3步预测、仅模拟容量。
 */
uint32_t load_capacity_bridge_version(void)
{
    return UINT32_C(1);
}

/*
 * 返回值：
 *     1：存在PPS风险。
 *     0：未超过PPS预算。
 *     负数：-errno。
 *
 * first_step和peak_ratio是借用的输出指针。
 * 调用失败时不修改输出。
 */
int load_capacity_assess_simulated_3(
    double safe_pps,
    double p1,
    double p2,
    double p3,
    size_t *first_step,
    double *peak_ratio)
{
    if (first_step == NULL || peak_ratio == NULL) {
        return -EINVAL;
    }

    load_capacity_profile_t profile = {
        .source = LOAD_CAPACITY_SOURCE_SIMULATED,
        .workload_id = UINT64_C(1),
        .window_seconds = 600U,
        .safe_pps = safe_pps
    };

    load_capacity_forecast_t forecast = {
        .workload_id = UINT64_C(1),
        .window_seconds = 600U,
        .step_count = 3U,
        .predicted_packets = {p1, p2, p3},
        .input_complete = true
    };

    load_capacity_result_t result;

    int rc = load_capacity_assess(
        &profile,
        &forecast,
        &result
    );

    if (rc != 0) {
        return -rc;
    }

    if (result.status != LOAD_CAPACITY_WITHIN_PPS_BUDGET &&
        result.status != LOAD_CAPACITY_PPS_RISK) {
        return -EPROTO;
    }

    *first_step = result.first_risk_step;
    *peak_ratio = result.peak_budget_ratio;

    return result.status == LOAD_CAPACITY_PPS_RISK ? 1 : 0;
}