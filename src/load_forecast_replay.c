#include "analyzer/load_capacity.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* v1固定30->32->16->3，参数按PyTorch行优先顺序存储。 */
enum { PARAMETERS = 1571, VALUES = PARAMETERS + 5 };

typedef struct {
    double v[VALUES];
} model_t;

_Static_assert(
    sizeof(double) == 8 &&
    DBL_MANT_DIG == 53 &&
    DBL_MAX_EXP == 1024,
    "IEEE binary64 required"
);

_Static_assert(
    FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
    "IEEE binary32 required"
);

static int fail(const char *message)
{
    fprintf(stderr, "ERROR: %s\n", message);
    return EXIT_FAILURE;
}

/* 文件一律使用小端；不直接fread一个C结构体。 */
static int read_u64(FILE *stream, uint64_t *value)
{
    unsigned char bytes[8];

    if (fread(bytes, 1, sizeof(bytes), stream) != sizeof(bytes)) {
        return -1;
    }

    *value = 0;

    for (unsigned i = 0; i < 8; ++i) {
        *value |= (uint64_t)bytes[i] << (8U * i);
    }

    return 0;
}

static int read_double(FILE *stream, double *value)
{
    uint64_t bits;

    if (read_u64(stream, &bits) != 0) {
        return -1;
    }

    memcpy(value, &bits, sizeof(bits));

    return isfinite(*value) ? 0 : -1;
}

static int magic(FILE *stream, const char *expected)
{
    char bytes[8];

    return fread(bytes, 1, 8, stream) == 8 &&
           memcmp(bytes, expected, 8) == 0 ? 0 : -1;
}

static int at_end(FILE *stream)
{
    return fgetc(stream) == EOF && !ferror(stream);
}

static int load_model(const char *path, model_t *model)
{
    FILE *stream = fopen(path, "rb");

    if (stream == NULL) {
        return -1;
    }

    int ok = magic(stream, "NFAF001\n") == 0;

    for (size_t i = 0; ok && i < VALUES; ++i) {
        ok = read_double(stream, &model->v[i]) == 0;
    }

    if (ok) {
        ok = at_end(stream);
    }

    if (fclose(stream) != 0) {
        ok = 0;
    }

    if (!ok) {
        return -1;
    }

    /* mean/std/解码上限/校准偏移/PPS预算。 */
    if (model->v[0] < 0 ||
        model->v[1] < .1 ||
        model->v[2] < 0 ||
        model->v[3] < 0 ||
        model->v[4] <= 0) {
        return -1;
    }

    for (size_t i = 5; i < VALUES; ++i) {
        if (fabs(model->v[i]) > FLT_MAX) {
            return -1;
        }
    }

    return 0;
}

/* 输入是18个已验证的历史包数；不包含未来真实流量。 */
static int features(
    const model_t *m,
    const double raw[18],
    uint64_t origin,
    float x[30])
{
    for (size_t i = 0; i < 18; ++i) {
        if (!isfinite(raw[i]) ||
            raw[i] < 0 ||
            raw[i] > 9007199254740991.0 ||
            floor(raw[i]) != raw[i]) {
            return -1;
        }

        double z = (log1p(raw[i]) - m->v[0]) / m->v[1];

        if (!isfinite(z) || fabs(z) > FLT_MAX) {
            return -1;
        }

        x[i] = (float)z;
    }

    const double pi2 = 6.283185307179586476925286766559;

    for (size_t h = 0; h < 3; ++h) {
        uint64_t stamp = origin + UINT64_C(600) * h;

        double day =
            (double)(stamp % UINT64_C(86400)) / 86400.0;

        double week =
            ((double)((stamp / UINT64_C(86400) + 3) % 7)
             + day) / 7.0;

        x[18 + 4 * h] = (float)sin(pi2 * day);
        x[19 + 4 * h] = (float)cos(pi2 * day);
        x[20 + 4 * h] = (float)sin(pi2 * week);
        x[21 + 4 * h] = (float)cos(pi2 * week);
    }

    return 0;
}

/*
 * 用double累加降低舍入误差，每层仍转换为float。
 * 与PyTorch进行容差比较，不要求逐位一致。
 */
static int dense(
    const double *w,
    size_t inputs,
    size_t outputs,
    const float *x,
    float *y,
    int relu)
{
    const double *bias = w + inputs * outputs;

    for (size_t row = 0; row < outputs; ++row) {
        double sum = bias[row];

        for (size_t col = 0; col < inputs; ++col) {
            sum += w[row * inputs + col] * x[col];
        }

        if (!isfinite(sum) || fabs(sum) > FLT_MAX) {
            return -1;
        }

        y[row] = relu && sum < 0 ? 0.0F : (float)sum;
    }

    return 0;
}

static int predict(
    const model_t *m,
    const double raw[18],
    uint64_t origin,
    double point[3],
    double upper[3])
{
    float x[30], a[32], b[16], z[3];

    if (features(m, raw, origin, x) != 0 ||
        dense(m->v + 5, 30, 32, x, a, 1) != 0 ||
        dense(m->v + 5 + 992, 32, 16, a, b, 1) != 0 ||
        dense(m->v + 5 + 992 + 528, 16, 3, b, z, 0) != 0) {
        return -1;
    }

    for (size_t h = 0; h < 3; ++h) {
        double residual = (double)z[h] + (double)x[11];

        if (!isfinite(residual) || fabs(residual) > FLT_MAX) {
            return -1;
        }

        float normalized = (float)residual;

        double decoded =
            (double)normalized * m->v[1] + m->v[0];

        if (!isfinite(decoded)) {
            return -1;
        }

        decoded = fmin(fmax(decoded, 0.0), m->v[2]);

        point[h] = expm1(decoded);

        upper[h] = fmax(
            point[h],
            expm1(log1p(point[h]) + m->v[3])
        );

        if (!isfinite(point[h]) || !isfinite(upper[h])) {
            return -1;
        }
    }

    return 0;
}

static int assess(
    double budget,
    const double packets[3],
    load_capacity_result_t *result)
{
    load_capacity_profile_t profile = {
        .source = LOAD_CAPACITY_SOURCE_SIMULATED,
        .workload_id = 1,
        .window_seconds = 600,
        .safe_pps = budget
    };

    load_capacity_forecast_t forecast = {
        .workload_id = 1,
        .window_seconds = 600,
        .step_count = 3,
        .input_complete = true
    };

    memcpy(
        forecast.predicted_packets,
        packets,
        3 * sizeof(double)
    );

    return load_capacity_assess(
        &profile,
        &forecast,
        result
    );
}

static int replay(
    const model_t *m,
    const char *path,
    FILE *output,
    uint64_t *completed)
{
    FILE *input = fopen(path, "rb");

    if (input == NULL) {
        return -1;
    }

    uint64_t count = 0, previous = 0;

    int ok =
        magic(input, "NFAC001\n") == 0 &&
        read_u64(input, &count) == 0 &&
        count > 0 &&
        count <= 100000;

    if (ok && fprintf(
            output,
            "origin_unix,p1,p2,p3,u1,u2,u3,point_risk,upper_risk,"
            "upper_first_risk_step,upper_peak_budget_ratio,"
            "capacity_source\n") < 0) {
        ok = 0;
    }

    for (uint64_t row = 0; ok && row < count; ++row) {
        uint64_t origin;
        double raw[18], p[3], u[3];
        load_capacity_result_t pr, ur;

        ok =
            read_u64(input, &origin) == 0 &&
            origin <= UINT64_C(253402298399) &&
            (row == 0 || origin > previous);

        for (size_t i = 0; ok && i < 18; ++i) {
            ok = read_double(input, &raw[i]) == 0;
        }

        if (!ok ||
            predict(m, raw, origin, p, u) != 0 ||
            assess(m->v[4], p, &pr) != 0 ||
            assess(m->v[4], u, &ur) != 0) {
            ok = 0;
            break;
        }

        int n = fprintf(
            output,
            "%" PRIu64 ",%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
            "%d,%d,%zu,%.17g,SIMULATED\n",
            origin,
            p[0], p[1], p[2],
            u[0], u[1], u[2],
            pr.status == LOAD_CAPACITY_PPS_RISK,
            ur.status == LOAD_CAPACITY_PPS_RISK,
            ur.first_risk_step,
            ur.peak_budget_ratio
        );

        if (n < 0) {
            ok = 0;
        }

        previous = origin;
    }

    if (ok) {
        ok = at_end(input) && !ferror(output);
    }

    if (fclose(input) != 0) {
        ok = 0;
    }

    if (ok) {
        *completed = count;
    }

    return ok ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: load-forecast-replay MODEL.bin CASES.bin");
        return EXIT_SUCCESS;
    }

    if (argc != 3) {
        return fail("expected MODEL.bin CASES.bin");
    }

    model_t model;

    if (load_model(argv[1], &model) != 0) {
        return fail("invalid model");
    }

    /*
     * 先暂存结果，整包验证成功后才发布stdout。
     * 输入损坏时，避免留下半份成功CSV。
     */
    FILE *staged = tmpfile();

    if (staged == NULL) {
        return fail("cannot create temporary output");
    }

    uint64_t count = 0;

    if (replay(&model, argv[2], staged, &count) != 0 ||
        fflush(staged) != 0 ||
        fseek(staged, 0, SEEK_SET) != 0) {
        fclose(staged);
        return fail("invalid cases or inference/output failure");
    }

    char buffer[8192];
    size_t n;
    int ok = 1;

    while ((n = fread(buffer, 1, sizeof(buffer), staged)) > 0) {
        if (fwrite(buffer, 1, n, stdout) != n) {
            ok = 0;
            break;
        }
    }

    if (ferror(staged)) {
        ok = 0;
    }

    if (fclose(staged) != 0) {
        ok = 0;
    }

    if (fflush(stdout) != 0) {
        ok = 0;
    }

    if (!ok) {
        return fail("publishing CSV failed");
    }

    fprintf(
        stderr,
        "samples=%" PRIu64 " capacity_source=SIMULATED\n",
        count
    );

    return EXIT_SUCCESS;
}