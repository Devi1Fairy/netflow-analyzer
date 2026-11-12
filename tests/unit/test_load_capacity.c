#include "analyzer/load_capacity.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            fprintf(                                          \
                stderr,                                       \
                "[FAIL] line %d: %s\n",                        \
                __LINE__,                                     \
                #condition                                    \
            );                                                \
            return EXIT_FAILURE;                              \
        }                                                     \
    } while (false)

/**
 * 比较结果字段。
 *
 * 不使用memcmp比较整个结构体，
 * 避免把结构体填充字节当作业务结果。
 */
static bool unchanged(
    const load_capacity_result_t *a,
    const load_capacity_result_t *b)
{
    return a->status == b->status &&
           a->source == b->source &&
           a->first_risk_step == b->first_risk_step &&
           a->peak_predicted_pps == b->peak_predicted_pps &&
           a->peak_budget_ratio == b->peak_budget_ratio;
}

int main(void)
{
    /*
     * 本测试中的容量和预测全部为合成输入。
     * 不是板端测量，也不是模型实际输出。
     */
    load_capacity_profile_t profile = {
        .source = LOAD_CAPACITY_SOURCE_SIMULATED,
        .workload_id = UINT64_C(1),
        .window_seconds = 600U,
        .safe_pps = 1000.0
    };

    load_capacity_forecast_t forecast = {
        .workload_id = UINT64_C(1),
        .window_seconds = 600U,
        .step_count = 3U,
        .predicted_packets = {
            300000.0,
            600000.0,
            720000.0
        },
        .input_complete = true
    };

    load_capacity_result_t result;

    /*
     * 三个窗口分别为500、1000、1200 PPS。
     * 第二个窗口达到预算，所以最早风险窗口为2。
     */
    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == 0);

    CHECK(result.status == LOAD_CAPACITY_PPS_RISK);
    CHECK(result.source == LOAD_CAPACITY_SOURCE_SIMULATED);
    CHECK(result.first_risk_step == 2U);

    CHECK(fabs(
        result.peak_predicted_pps - 1200.0
    ) < 1e-9);

    CHECK(fabs(
        result.peak_budget_ratio - 1.2
    ) < 1e-9);

    puts(
        "[PASS] counts become PPS; earliest risk "
        "and simulated provenance retained"
    );

    /*
     * 修改为全部低于预算。
     */
    forecast.predicted_packets[1] = 420000.0;
    forecast.predicted_packets[2] = 480000.0;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == 0);

    CHECK(result.status == LOAD_CAPACITY_WITHIN_PPS_BUDGET);
    CHECK(result.first_risk_step == 0U);
    CHECK(result.peak_predicted_pps == 800.0);

    /*
     * 完整、可信窗口中的零流量是合法值。
     */
    forecast.predicted_packets[0] = 0.0;
    forecast.predicted_packets[1] = 0.0;
    forecast.predicted_packets[2] = 0.0;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == 0);

    CHECK(result.status == LOAD_CAPACITY_WITHIN_PPS_BUDGET);
    CHECK(result.peak_budget_ratio == 0.0);

    puts(
        "[PASS] below-budget and valid zero traffic "
        "are evaluated"
    );

    /*
     * 数据不完整：不能判断为正常。
     */
    forecast.input_complete = false;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == 0);

    CHECK(result.status == LOAD_CAPACITY_FORECAST_UNAVAILABLE);

    forecast.input_complete = true;

    /*
     * 容量未标定：不能判断为正常。
     */
    profile.source = LOAD_CAPACITY_SOURCE_UNVERIFIED;
    profile.safe_pps = 0.0;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == 0);

    CHECK(result.status == LOAD_CAPACITY_PROFILE_UNVERIFIED);

    /*
     * 负载类别不匹配。
     * MEASURED在这里也只是测试夹具中的枚举值。
     */
    profile.source = LOAD_CAPACITY_SOURCE_MEASURED;
    profile.safe_pps = 1000.0;

    forecast.workload_id = UINT64_C(2);

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == 0);

    CHECK(result.status == LOAD_CAPACITY_PROFILE_MISMATCH);

    /*
     * 窗口长度不匹配。
     */
    forecast.workload_id = UINT64_C(1);
    forecast.window_seconds = 60U;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == 0);

    CHECK(result.status == LOAD_CAPACITY_PROFILE_MISMATCH);

    forecast.window_seconds = 600U;

    puts(
        "[PASS] incomplete data, unknown capacity "
        "and mismatched profiles cannot be normal"
    );

    /*
     * 以下失败场景都必须保持原结果不变。
     */
    load_capacity_result_t before = result;

    CHECK(load_capacity_assess(
        NULL,
        &forecast,
        &result
    ) == EINVAL);

    CHECK(unchanged(&before, &result));

    size_t count = forecast.step_count;

    forecast.step_count = LOAD_CAPACITY_MAX_STEPS + 1U;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == EINVAL);

    CHECK(unchanged(&before, &result));

    forecast.step_count = 0U;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == EINVAL);

    CHECK(unchanged(&before, &result));

    forecast.step_count = count;
    forecast.window_seconds = 0U;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == EINVAL);

    CHECK(unchanged(&before, &result));

    forecast.window_seconds = 600U;

    const double bad_values[] = {
        -1.0,
        NAN,
        INFINITY
    };

    for (size_t i = 0U;
         i < sizeof(bad_values) / sizeof(bad_values[0]);
         ++i) {
        forecast.predicted_packets[0] = bad_values[i];

        CHECK(load_capacity_assess(
            &profile,
            &forecast,
            &result
        ) == EINVAL);

        CHECK(unchanged(&before, &result));
    }

    forecast.predicted_packets[0] = 0.0;
    profile.safe_pps = 0.0;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == EINVAL);

    CHECK(unchanged(&before, &result));

    /*
     * 极小容量和极大预测值导致比例溢出。
     */
    profile.safe_pps = DBL_MIN;
    forecast.predicted_packets[0] = DBL_MAX;

    CHECK(load_capacity_assess(
        &profile,
        &forecast,
        &result
    ) == EOVERFLOW);

    CHECK(unchanged(&before, &result));

    puts(
        "[PASS] invalid counts, bounds, capacity "
        "and overflow preserve previous result"
    );

    return EXIT_SUCCESS;
}