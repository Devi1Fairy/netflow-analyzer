#include "analyzer/load_capacity.h"

#include <errno.h>
#include <math.h>

int load_capacity_assess(
    const load_capacity_profile_t *profile,
    const load_capacity_forecast_t *forecast,
    load_capacity_result_t *result)
{
    if (profile == NULL ||
        forecast == NULL ||
        result == NULL ||
        forecast->workload_id == 0U ||
        forecast->window_seconds == 0U ||
        forecast->step_count == 0U ||
        forecast->step_count > LOAD_CAPACITY_MAX_STEPS) {
        return EINVAL;
    }

    if (profile->source != LOAD_CAPACITY_SOURCE_UNVERIFIED &&
        profile->source != LOAD_CAPACITY_SOURCE_SIMULATED &&
        profile->source != LOAD_CAPACITY_SOURCE_MEASURED) {
        return EINVAL;
    }

    /*
     * 先验证所有预测值。
     *
     * isfinite()用于拒绝NaN和正负无穷大。
     * 模型可以输出非整数包数，但不能输出负数。
     */
    for (size_t i = 0U; i < forecast->step_count; ++i) {
        double packets = forecast->predicted_packets[i];

        if (!isfinite(packets) || packets < 0.0) {
            return EINVAL;
        }
    }

    /*
     * 未标定容量允许暂时没有有效预算。
     * 模拟容量和实测容量则必须提供完整配置。
     */
    if (profile->source != LOAD_CAPACITY_SOURCE_UNVERIFIED &&
        (profile->workload_id == 0U ||
         profile->window_seconds == 0U ||
         !isfinite(profile->safe_pps) ||
         profile->safe_pps <= 0.0)) {
        return EINVAL;
    }

    /*
     * 使用局部结果。
     *
     * 后续任何计算失败，都不会留下部分更新。
     */
    load_capacity_result_t candidate = {
        .status = LOAD_CAPACITY_FORECAST_UNAVAILABLE,
        .source = profile->source,
        .first_risk_step = 0U,
        .peak_predicted_pps = 0.0,
        .peak_budget_ratio = 0.0
    };

    /*
     * 数据不完整时，不能因为预测值很小就判断为正常。
     */
    if (!forecast->input_complete) {
        *result = candidate;
        return 0;
    }

    /*
     * 没有容量依据时，明确返回“容量未标定”。
     */
    if (profile->source == LOAD_CAPACITY_SOURCE_UNVERIFIED) {
        candidate.status = LOAD_CAPACITY_PROFILE_UNVERIFIED;
        *result = candidate;
        return 0;
    }

    /*
     * 不同负载类别或不同窗口长度的配置不能直接比较。
     */
    if (forecast->workload_id != profile->workload_id ||
        forecast->window_seconds != profile->window_seconds) {
        candidate.status = LOAD_CAPACITY_PROFILE_MISMATCH;
        *result = candidate;
        return 0;
    }

    candidate.status = LOAD_CAPACITY_WITHIN_PPS_BUDGET;

    for (size_t i = 0U; i < forecast->step_count; ++i) {
        double pps =
            forecast->predicted_packets[i] /
            (double)forecast->window_seconds;

        double ratio = pps / profile->safe_pps;

        if (!isfinite(pps) || !isfinite(ratio)) {
            return EOVERFLOW;
        }

        if (pps > candidate.peak_predicted_pps) {
            candidate.peak_predicted_pps = pps;
            candidate.peak_budget_ratio = ratio;
        }

        /*
         * 只记录第一次达到预算的窗口。
         * 后续窗口即使负载更高，也不覆盖最早风险位置。
         */
        if (ratio >= 1.0 &&
            candidate.first_risk_step == 0U) {
            candidate.first_risk_step = i + 1U;
            candidate.status = LOAD_CAPACITY_PPS_RISK;
        }
    }

    *result = candidate;
    return 0;
}