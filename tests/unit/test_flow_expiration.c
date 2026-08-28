#include "analyzer/flow_expiration.h"

#include <errno.h>
#include <stdbool.h>
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

static int test_scan_boundary_and_out_of_order(void)
{
    flow_expiration_schedule_t schedule;
    flow_timestamp_t timestamp;
    flow_timestamp_t cutoff;
    bool scan_due;

    TEST_CHECK(
        flow_expiration_schedule_init(
            &schedule,
            INT64_C(30),
            INT64_C(5)
        ) == 0
    );

    /*
     * 第一包只初始化时间高水位。
     */
    timestamp = (flow_timestamp_t){
        .seconds = INT64_C(100),
        .microseconds = INT32_C(250000)
    };

    scan_due = true;
    cutoff.seconds = INT64_C(99);
    cutoff.microseconds = INT32_C(99);

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &timestamp,
            &scan_due,
            &cutoff
        ) == 0
    );

    TEST_CHECK(!scan_due);
    TEST_CHECK(cutoff.seconds == INT64_C(0));
    TEST_CHECK(cutoff.microseconds == INT32_C(0));

    /*
     * 距离首次观察还差1微秒，不应提前扫描。
     */
    timestamp = (flow_timestamp_t){
        .seconds = INT64_C(105),
        .microseconds = INT32_C(249999)
    };

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &timestamp,
            &scan_due,
            &cutoff
        ) == 0
    );

    TEST_CHECK(!scan_due);

    /*
     * 正好达到5秒扫描边界。
     */
    timestamp.microseconds = INT32_C(250000);

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &timestamp,
            &scan_due,
            &cutoff
        ) == 0
    );

    TEST_CHECK(scan_due);
    TEST_CHECK(cutoff.seconds == INT64_C(75));
    TEST_CHECK(
        cutoff.microseconds == INT32_C(250000)
    );

    /*
     * 乱序包不能让时间高水位或扫描时间倒退。
     */
    timestamp = (flow_timestamp_t){
        .seconds = INT64_C(80),
        .microseconds = INT32_C(0)
    };

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &timestamp,
            &scan_due,
            &cutoff
        ) == 0
    );

    TEST_CHECK(!scan_due);
    TEST_CHECK(
        schedule.latest_timestamp.seconds ==
            INT64_C(105)
    );
    TEST_CHECK(
        schedule.latest_timestamp.microseconds ==
            INT32_C(250000)
    );

    /*
     * 从上一次真实扫描时间再经过5秒。
     */
    timestamp = (flow_timestamp_t){
        .seconds = INT64_C(110),
        .microseconds = INT32_C(250000)
    };

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &timestamp,
            &scan_due,
            &cutoff
        ) == 0
    );

    TEST_CHECK(scan_due);
    TEST_CHECK(cutoff.seconds == INT64_C(80));
    TEST_CHECK(
        cutoff.microseconds == INT32_C(250000)
    );

    return EXIT_SUCCESS;
}

static int test_argument_validation(void)
{
    flow_expiration_schedule_t schedule = {
        .idle_timeout_seconds = INT64_C(99)
    };

    flow_timestamp_t timestamp = {
        .seconds = INT64_C(100),
        .microseconds = INT32_C(1000000)
    };

    flow_timestamp_t cutoff = {
        .seconds = INT64_C(88),
        .microseconds = INT32_C(77)
    };

    bool scan_due = true;

    TEST_CHECK(
        flow_expiration_schedule_init(
            NULL,
            INT64_C(30),
            INT64_C(5)
        ) == EINVAL
    );

    TEST_CHECK(
        flow_expiration_schedule_init(
            &schedule,
            INT64_C(0),
            INT64_C(5)
        ) == EINVAL
    );

    /*
     * 初始化失败时保留原对象。
     */
    TEST_CHECK(
        schedule.idle_timeout_seconds ==
            INT64_C(99)
    );

    TEST_CHECK(
        flow_expiration_schedule_init(
            &schedule,
            INT64_C(30),
            INT64_C(5)
        ) == 0
    );

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &timestamp,
            &scan_due,
            &cutoff
        ) == EINVAL
    );

    TEST_CHECK(scan_due);
    TEST_CHECK(cutoff.seconds == INT64_C(88));
    TEST_CHECK(cutoff.microseconds == INT32_C(77));
    TEST_CHECK(!schedule.has_observation);

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            NULL,
            &scan_due,
            &cutoff
        ) == EINVAL
    );

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &(flow_timestamp_t){
                .seconds = INT64_C(100),
                .microseconds = INT32_C(0)
            },
            NULL,
            &cutoff
        ) == EINVAL
    );

    return EXIT_SUCCESS;
}

static int test_cutoff_underflow(void)
{
    flow_expiration_schedule_t schedule;
    flow_timestamp_t timestamp;
    flow_timestamp_t cutoff;
    bool scan_due;

    TEST_CHECK(
        flow_expiration_schedule_init(
            &schedule,
            INT64_C(30),
            INT64_C(5)
        ) == 0
    );

    timestamp = (flow_timestamp_t){
        .seconds = INT64_MIN + INT64_C(10),
        .microseconds = INT32_C(0)
    };

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &timestamp,
            &scan_due,
            &cutoff
        ) == 0
    );

    timestamp.seconds = INT64_MIN + INT64_C(15);
    scan_due = true;
    cutoff.seconds = INT64_C(99);
    cutoff.microseconds = INT32_C(88);

    TEST_CHECK(
        flow_expiration_schedule_observe(
            &schedule,
            &timestamp,
            &scan_due,
            &cutoff
        ) == ERANGE
    );

    /*
     * 失败时调度器和输出参数都保持原状。
     */
    TEST_CHECK(
        schedule.latest_timestamp.seconds ==
            INT64_MIN + INT64_C(10)
    );
    TEST_CHECK(scan_due);
    TEST_CHECK(cutoff.seconds == INT64_C(99));
    TEST_CHECK(cutoff.microseconds == INT32_C(88));

    return EXIT_SUCCESS;
}

int main(void)
{
    if (test_scan_boundary_and_out_of_order() !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] scan boundary and out-of-order timestamps\n");

    if (test_argument_validation() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] expiration schedule argument validation\n");

    if (test_cutoff_underflow() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    printf("[PASS] expiration cutoff underflow\n");

    return EXIT_SUCCESS;
}