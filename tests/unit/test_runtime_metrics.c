#include "analyzer/runtime_metrics.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
 * @brief 对浮点计算结果进行允许微小误差的比较。
 */
static bool test_double_close(
    double actual,
    double expected)
{
    double difference = actual - expected;

    if (difference < 0.0) {
        difference = -difference;
    }

    return difference < 0.000000000001;
}

static int test_totals_accumulation_and_overflow(void)
{
    runtime_metrics_totals_t totals = {0};

    TEST_CHECK(
        runtime_metrics_totals_add_packet(
            &totals,
            UINT32_C(60),
            UINT32_C(64)
        ) == 0
    );

    TEST_CHECK(totals.packet_count == UINT64_C(1));
    TEST_CHECK(
        totals.captured_byte_count == UINT64_C(60)
    );
    TEST_CHECK(
        totals.wire_byte_count == UINT64_C(64)
    );

    TEST_CHECK(
        runtime_metrics_totals_add_expired_flows(
            &totals,
            UINT64_C(3)
        ) == 0
    );

    TEST_CHECK(
        totals.expired_flow_count == UINT64_C(3)
    );

    totals = (runtime_metrics_totals_t){
        .packet_count = UINT64_MAX,
        .captured_byte_count = UINT64_C(100),
        .wire_byte_count = UINT64_C(200),
        .expired_flow_count = UINT64_C(3)
    };

    TEST_CHECK(
        runtime_metrics_totals_add_packet(
            &totals,
            UINT32_C(60),
            UINT32_C(64)
        ) == EOVERFLOW
    );

    /*
     * 溢出失败后所有字段必须保持原值。
     */
    TEST_CHECK(totals.packet_count == UINT64_MAX);
    TEST_CHECK(
        totals.captured_byte_count == UINT64_C(100)
    );
    TEST_CHECK(
        totals.wire_byte_count == UINT64_C(200)
    );

    TEST_CHECK(
        runtime_metrics_totals_add_packet(
            NULL,
            UINT32_C(1),
            UINT32_C(1)
        ) == EINVAL
    );

    return EXIT_SUCCESS;
}

static int test_report_boundary_and_rates(void)
{
    runtime_metrics_schedule_t schedule;
    runtime_metrics_totals_t totals = {0};

    runtime_metrics_timestamp_t timestamp = {
        .seconds = INT64_C(100),
        .nanoseconds = INT32_C(250000000)
    };

    runtime_metrics_report_t report;
    bool report_due;

    TEST_CHECK(
        runtime_metrics_schedule_init(
            &schedule,
            INT64_C(5),
            &timestamp,
            &totals
        ) == 0
    );

    totals = (runtime_metrics_totals_t){
        .packet_count = UINT64_C(10),
        .captured_byte_count = UINT64_C(1000),
        .wire_byte_count = UINT64_C(1200),
        .expired_flow_count = UINT64_C(2)
    };

    /*
     * 距离5秒边界还差1纳秒，不应提前产生报告。
     */
    timestamp = (runtime_metrics_timestamp_t){
        .seconds = INT64_C(105),
        .nanoseconds = INT32_C(249999999)
    };

    report_due = true;
    report.elapsed_seconds = 99.0;

    TEST_CHECK(
        runtime_metrics_schedule_observe(
            &schedule,
            &timestamp,
            &totals,
            64U,
            256U,
            &report_due,
            &report
        ) == 0
    );

    TEST_CHECK(!report_due);
    TEST_CHECK(report.elapsed_seconds == 0.0);

    /*
     * 未到周期时不能推进报告基线。
     */
    TEST_CHECK(
        schedule.last_report_timestamp.seconds ==
            INT64_C(100)
    );
    TEST_CHECK(
        schedule.last_report_timestamp.nanoseconds ==
            INT32_C(250000000)
    );

    /*
     * 正好到达5秒边界。
     */
    timestamp.nanoseconds = INT32_C(250000000);

    TEST_CHECK(
        runtime_metrics_schedule_observe(
            &schedule,
            &timestamp,
            &totals,
            64U,
            256U,
            &report_due,
            &report
        ) == 0
    );

    TEST_CHECK(report_due);
    TEST_CHECK(
        test_double_close(
            report.elapsed_seconds,
            5.0
        )
    );
    TEST_CHECK(
        report.interval_packet_count ==
            UINT64_C(10)
    );
    TEST_CHECK(
        report.interval_captured_byte_count ==
            UINT64_C(1000)
    );
    TEST_CHECK(
        report.interval_wire_byte_count ==
            UINT64_C(1200)
    );
    TEST_CHECK(
        report.interval_expired_flow_count ==
            UINT64_C(2)
    );
    TEST_CHECK(
        test_double_close(
            report.packets_per_second,
            2.0
        )
    );
    TEST_CHECK(
        test_double_close(
            report.captured_megabits_per_second,
            0.0016
        )
    );
    TEST_CHECK(
        test_double_close(
            report.wire_megabits_per_second,
            0.00192
        )
    );
    TEST_CHECK(report.active_flow_count == 64U);
    TEST_CHECK(report.flow_table_capacity == 256U);
    TEST_CHECK(
        test_double_close(
            report.flow_table_usage_percent,
            25.0
        )
    );

    /*
     * 第二次报告晚了2.5秒，速率必须使用真实的7.5秒，
     * 不能仍然除以配置值5。
     */
    totals = (runtime_metrics_totals_t){
        .packet_count = UINT64_C(25),
        .captured_byte_count = UINT64_C(2500),
        .wire_byte_count = UINT64_C(3000),
        .expired_flow_count = UINT64_C(5)
    };

    timestamp = (runtime_metrics_timestamp_t){
        .seconds = INT64_C(112),
        .nanoseconds = INT32_C(750000000)
    };

    TEST_CHECK(
        runtime_metrics_schedule_observe(
            &schedule,
            &timestamp,
            &totals,
            128U,
            256U,
            &report_due,
            &report
        ) == 0
    );

    TEST_CHECK(report_due);
    TEST_CHECK(
        test_double_close(
            report.elapsed_seconds,
            7.5
        )
    );
    TEST_CHECK(
        report.interval_packet_count ==
            UINT64_C(15)
    );
    TEST_CHECK(
        test_double_close(
            report.packets_per_second,
            2.0
        )
    );
    TEST_CHECK(
        report.interval_expired_flow_count ==
            UINT64_C(3)
    );
    TEST_CHECK(
        test_double_close(
            report.flow_table_usage_percent,
            50.0
        )
    );

    return EXIT_SUCCESS;
}

static int test_invalid_state_preserves_outputs(void)
{
    runtime_metrics_schedule_t schedule;
    runtime_metrics_totals_t totals = {
        .packet_count = UINT64_C(10),
        .captured_byte_count = UINT64_C(100),
        .wire_byte_count = UINT64_C(120),
        .expired_flow_count = UINT64_C(1)
    };

    runtime_metrics_timestamp_t timestamp = {
        .seconds = INT64_C(100),
        .nanoseconds = INT32_C(0)
    };

    runtime_metrics_report_t report = {
        .elapsed_seconds = 77.0
    };

    bool report_due = true;

    TEST_CHECK(
        runtime_metrics_schedule_init(
            &schedule,
            INT64_C(5),
            &timestamp,
            &totals
        ) == 0
    );

    /*
     * 时间倒退必须失败，并保留调度器与输出参数。
     */
    timestamp.seconds = INT64_C(99);

    TEST_CHECK(
        runtime_metrics_schedule_observe(
            &schedule,
            &timestamp,
            &totals,
            1U,
            256U,
            &report_due,
            &report
        ) == ERANGE
    );

    TEST_CHECK(
        schedule.last_report_timestamp.seconds ==
            INT64_C(100)
    );
    TEST_CHECK(report_due);
    TEST_CHECK(report.elapsed_seconds == 77.0);

    /*
     * 累计包数减小也表示状态不连续。
     */
    timestamp.seconds = INT64_C(101);
    totals.packet_count = UINT64_C(9);

    TEST_CHECK(
        runtime_metrics_schedule_observe(
            &schedule,
            &timestamp,
            &totals,
            1U,
            256U,
            &report_due,
            &report
        ) == ERANGE
    );

    /*
     * 活动流数量不能超过流表容量。
     */
    totals.packet_count = UINT64_C(10);

    TEST_CHECK(
        runtime_metrics_schedule_observe(
            &schedule,
            &timestamp,
            &totals,
            257U,
            256U,
            &report_due,
            &report
        ) == EINVAL
    );

    TEST_CHECK(
        runtime_metrics_schedule_init(
            &schedule,
            INT64_C(0),
            &timestamp,
            &totals
        ) == EINVAL
    );

    return EXIT_SUCCESS;
}

int main(void)
{
    if (test_totals_accumulation_and_overflow() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] runtime metric totals and overflow\n");

    if (test_report_boundary_and_rates() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] runtime metric interval and rates\n");

    if (test_invalid_state_preserves_outputs() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] runtime metric validation\n");

    return EXIT_SUCCESS;
}