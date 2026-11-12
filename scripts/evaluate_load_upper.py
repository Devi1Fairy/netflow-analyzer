"""冻结点预测模型，用校准段生成三步经验上界；容量始终为模拟。"""

import argparse
import csv
import json
import math
import tempfile
from pathlib import Path

import numpy as np
import torch
from torch import nn

import run_load_forecast_pilot as pilot


UPPER_SCHEMA = "load_joint_log_upper_v1"


def packet_matrix(values):
    """转换并校验N×3包数矩阵，不修改输入。"""
    array = np.asarray(values, dtype=np.float64)

    if (
        array.ndim != 2
        or array.shape[1] != pilot.HORIZON
        or len(array) == 0
        or not np.isfinite(array).all()
        or np.any(array < 0)
    ):
        raise ValueError(
            "expected non-empty finite non-negative N x 3 counts"
        )

    return array


def fit_upper(prediction, actual, alpha=.05):
    """只用校准段误差，估计三步联合log上界修正。"""
    prediction = packet_matrix(prediction)
    actual = packet_matrix(actual)

    if prediction.shape != actual.shape:
        raise ValueError("calibration shapes differ")

    if not math.isfinite(alpha) or not 0 < alpha < 1:
        raise ValueError("alpha must be in (0, 1)")

    # 每个校准起点，取三个未来窗口中最大的低估量。
    scores = np.max(
        np.log1p(actual) - np.log1p(prediction),
        axis=1,
    )

    n = len(scores)

    # 从1开始的顺序统计量位置。
    rank = math.ceil((n + 1) * (1 - alpha))

    if rank > n:
        raise ValueError(
            "calibration set too small for requested level"
        )

    # np.partition只需要找到指定位置，不必完整排序。
    offset = max(
        0.,
        float(
            np.partition(scores, rank - 1)[rank - 1]
        ),
    )

    return {
        "schema": UPPER_SCHEMA,
        "horizon": pilot.HORIZON,
        "alpha": float(alpha),
        "calibration_origins": n,
        "rank": rank,
        "log_offset": offset,
    }


def apply_upper(prediction, state):
    """应用已冻结修正，不使用当前预测对应的真实标签。"""
    prediction = packet_matrix(prediction)

    if (
        not isinstance(state, dict)
        or state.get("schema") != UPPER_SCHEMA
        or state.get("horizon") != pilot.HORIZON
    ):
        raise ValueError(
            "upper calibration schema or horizon mismatch"
        )

    offset = state.get("log_offset")

    if (
        not isinstance(offset, (int, float))
        or not math.isfinite(offset)
        or offset < 0
    ):
        raise ValueError("invalid log offset")

    try:
        with np.errstate(
            over="raise",
            invalid="raise",
        ):
            upper = np.expm1(
                np.log1p(prediction) + offset
            )
    except FloatingPointError as error:
        raise ValueError(
            "upper bound overflow"
        ) from error

    if not np.isfinite(upper).all():
        raise ValueError("non-finite upper bound")

    # 防止浮点舍入使修正后的上界略低于原点预测。
    return np.maximum(upper, prediction)


def risk_episode_count(flags, origins):
    """相邻起点的连续风险算一段，数据缺口后重新计段。"""
    flags = np.asarray(flags, dtype=bool)
    origins = np.asarray(origins)

    if flags.ndim != 1 or origins.shape != flags.shape:
        raise ValueError(
            "risk flags and origins differ"
        )

    starts = flags.copy()

    starts[1:] &= (
        (~flags[:-1])
        | (np.diff(origins) != 1)
    )

    return int(starts.sum())


def load_frozen(args, y, stamps):
    """核对输入和旧模型，加载冻结参数，不重新训练。"""
    saved = json.loads(
        (args.point_run / "model.json").read_text()
    )

    if (
        saved.get("schema") != "load_forecast_pilot_v1"
        or saved.get("capacity_source") != "SIMULATED"
        or saved.get("window_seconds") != pilot.WINDOW
        or saved.get("horizon") != pilot.HORIZON
        or saved.get("architecture") != [30, 32, 16, 3]
    ):
        raise ValueError("point-run contract mismatch")

    for key, path in (
        ("series_sha256", args.series_csv),
        ("times_sha256", args.times_archive),
        (
            "capacity_library_sha256",
            args.capacity_library,
        ),
    ):
        if pilot.fingerprint(path) != saved[key]:
            raise ValueError(
                "point-run fingerprint mismatch: " + key
            )

    groups, computed = pilot.samples(y, stamps)

    # 重新计算只用于核对，没有重新选择参数。
    if computed != saved["preprocessing"]:
        raise ValueError(
            "point-run preprocessing changed"
        )

    model = nn.Sequential(
        nn.Linear(30, 32),
        nn.ReLU(),
        nn.Linear(32, 16),
        nn.ReLU(),
        nn.Linear(16, pilot.HORIZON),
    )

    model.load_state_dict(
        torch.load(
            args.point_run / "model.pt",
            map_location="cpu",
            weights_only=True,
        )
    )

    weights = np.load(
        args.point_run / "ridge.npy",
        allow_pickle=False,
    )

    if (
        weights.shape != (31, 3)
        or not np.isfinite(weights).all()
    ):
        raise ValueError("invalid ridge weights")

    def predict(group):
        result = pilot.baselines(group, y)

        result["ridge"] = pilot.decode(
            pilot.design(group) @ weights
            + group["x"][:, 11:12],
            computed,
        )

        result["mlp"] = pilot.decode(
            pilot.mlp_z(model, group),
            computed,
        )

        for values in result.values():
            packet_matrix(values)

        return result

    return saved, groups, computed, predict


def check_reference(path, predictions, origins):
    """确认点预测仍与原CSV一致，防止无意中换模型。"""
    saved = {
        name: []
        for name in predictions
    }

    with path.open(
        encoding="utf-8",
        newline="",
    ) as stream:
        for row in csv.DictReader(stream):
            name = row["method"]

            if name in saved:
                saved[name].append(
                    [
                        float(row["origin_unix"]),
                        float(row["p1"]),
                        float(row["p2"]),
                        float(row["p3"]),
                    ]
                )

    for name, values in predictions.items():
        reference = np.asarray(
            saved[name],
            dtype=np.float64,
        )

        if reference.shape != (len(values), 4):
            raise ValueError(
                "point reference rows differ: " + name
            )

        np.testing.assert_array_equal(
            reference[:, 0],
            origins,
        )

        np.testing.assert_allclose(
            reference[:, 1:],
            values,
            rtol=1e-9,
            atol=1e-6,
        )


def self_test(gate):
    prediction = np.zeros((20, 3))

    actual = np.repeat(
        np.arange(20)[:, None],
        3,
        axis=1,
    )

    state = fit_upper(
        prediction,
        actual,
        alpha=.1,
    )

    assert state["rank"] == 19

    np.testing.assert_allclose(
        apply_upper(prediction, state),
        18.,
        atol=1e-12,
    )

    reversed_state = fit_upper(
        prediction[::-1],
        actual[::-1],
        alpha=.1,
    )

    assert state == reversed_state

    np.testing.assert_array_equal(
        prediction,
        np.zeros((20, 3)),
    )

    print(
        "[PASS] corrected rank, calibration order "
        "and input ownership"
    )

    # 只有第三步低估，也必须进入联合上界修正。
    joint_actual = np.repeat(
        [[0., 0., 100.]],
        20,
        axis=0,
    )

    joint_state = fit_upper(
        prediction,
        joint_actual,
    )

    bound = apply_upper(
        prediction[:1],
        joint_state,
    )

    np.testing.assert_allclose(
        bound,
        100.,
        atol=1e-10,
    )

    # 同一个C规则：点预测无风险，上界可能提示风险。
    assert gate(.1, prediction[0])[0] == 0
    assert gate(.1, bound[0])[0] == 1

    zero_state = fit_upper(
        np.full((20, 3), 100.),
        prediction,
    )

    assert zero_state["log_offset"] == 0.

    np.testing.assert_allclose(
        apply_upper(
            np.full((1, 3), 100.),
            zero_state,
        ),
        100.,
    )

    print(
        "[PASS] joint three-step score, "
        "nonnegative correction and C risk rule"
    )

    invalid = [
        lambda: fit_upper(
            prediction[:1],
            actual[:1],
        ),
        lambda: fit_upper(
            prediction,
            actual,
            alpha=0.,
        ),
        lambda: fit_upper(
            prediction[:, :2],
            actual[:, :2],
        ),
        lambda: fit_upper(
            prediction,
            np.full((20, 3), np.nan),
        ),
        lambda: fit_upper(
            -np.ones((20, 3)),
            actual,
        ),
        lambda: apply_upper(
            prediction,
            dict(state, horizon=2),
        ),
        lambda: apply_upper(
            prediction,
            dict(state, log_offset=-1.),
        ),
        lambda: apply_upper(
            prediction,
            dict(state, log_offset=1000.),
        ),
    ]

    for operation in invalid:
        try:
            operation()
        except ValueError:
            pass
        else:
            raise AssertionError(
                "invalid calibration accepted"
            )

    assert risk_episode_count(
        [True, True, False, True, True],
        [10, 11, 12, 13, 15],
    ) == 3

    print(
        "[PASS] invalid states, overflow "
        "and risk-episode gaps"
    )


def main(argv=None):
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--capacity-library",
        type=Path,
        required=True,
    )

    parser.add_argument(
        "--point-run",
        type=Path,
    )

    parser.add_argument(
        "--series-csv",
        type=Path,
    )

    parser.add_argument(
        "--times-archive",
        type=Path,
    )

    parser.add_argument(
        "--output-root",
        type=Path,
    )

    parser.add_argument(
        "--alpha",
        type=float,
        default=.05,
    )

    parser.add_argument(
        "--evaluation-role",
        choices=(
            "development_diagnostic",
            "protocol_check",
        ),
        default="development_diagnostic",
    )

    parser.add_argument(
        "--self-test",
        action="store_true",
    )

    args = parser.parse_args(argv)

    torch.set_num_threads(1)

    gate = pilot.capacity_gate(
        args.capacity_library
    )

    if args.self_test:
        self_test(gate)
        return

    if not all(
        (
            args.point_run,
            args.series_csv,
            args.times_archive,
            args.output_root,
        )
    ):
        parser.error(
            "point-run, series-csv, times-archive "
            "and output-root required"
        )

    if (
        not math.isfinite(args.alpha)
        or not 0 < args.alpha < 1
    ):
        parser.error(
            "alpha must be in (0, 1)"
        )

    y, stamps = pilot.read_series(
        args.series_csv,
        args.times_archive,
    )

    saved, groups, state, predict = load_frozen(
        args,
        y,
        stamps,
    )

    budget = float(saved["safe_pps"])

    if not math.isfinite(budget) or budget <= 0:
        raise ValueError(
            "invalid saved simulated budget"
        )

    calibration = predict(
        groups["calibration"]
    )

    upper_states = {
        name: fit_upper(
            values,
            groups["calibration"]["truth"],
            args.alpha,
        )
        for name, values in calibration.items()
    }

    args.output_root.mkdir(
        parents=True,
        exist_ok=True,
    )

    output = Path(
        tempfile.mkdtemp(
            prefix="upper-pilot-",
            dir=args.output_root,
        )
    )

    # 先冻结上界参数，再读取测试参考或计算测试指标。
    frozen = {
        "schema": UPPER_SCHEMA,
        "evaluation_role": args.evaluation_role,
        "capacity_source": "SIMULATED",
        "safe_pps": budget,
        "point_run": str(
            args.point_run.resolve()
        ),
        "point_model_sha256": pilot.fingerprint(
            args.point_run / "model.pt"
        ),
        "point_ridge_sha256": pilot.fingerprint(
            args.point_run / "ridge.npy"
        ),
        "series_sha256": saved["series_sha256"],
        "times_sha256": saved["times_sha256"],
        "upper_states": upper_states,
        "coverage_note": (
            "nominal joint target; temporal dependence "
            "prevents an unconditional "
            f"{100 * (1 - args.alpha):g}% guarantee"
        ),
    }

    (output / "upper_calibration.json").write_text(
        json.dumps(frozen, indent=2) + "\n"
    )

    print(
        "output=" + str(output),
        flush=True,
    )

    group = groups["test"]
    points = predict(group)

    check_reference(
        args.point_run / "test_predictions.csv",
        points,
        stamps[group["origin"]],
    )

    variants = dict(points)

    for name, values in points.items():
        variants[name + "_upper"] = apply_upper(
            values,
            upper_states[name],
        )

    # “始终提示风险”对照：
    # 用于揭示仅靠持续告警获得高检出数的代价。
    constant = budget * pilot.WINDOW * 1.01

    if not math.isfinite(constant):
        raise ValueError(
            "always-risk reference overflow"
        )

    variants["always_risk"] = np.full_like(
        group["truth"],
        constant,
    )

    truth_flags = np.array(
        [
            gate(budget, row)[0]
            for row in group["truth"]
        ],
        bool,
    )

    report = {
        "schema": UPPER_SCHEMA,
        "capacity_source": "SIMULATED",
        "safe_pps": budget,
        "alpha": args.alpha,
        "evaluation_role": args.evaluation_role,
        "test_origins": len(group["origin"]),
        "actual_risk_origins": int(
            truth_flags.sum()
        ),
        "selected_point_method": (
            saved["selected_by_validation_mae"]
        ),
        "reference_predictions_verified": True,
        "methods": {},
        "notes": [
            (
                "decision origins overlap; "
                "episodes and actual events differ"
            ),
            (
                "coverage is empirical, "
                "not overload probability"
            ),
            (
                "offline lead excludes runtime delay; "
                "no board validation"
            ),
        ],
    }

    with (output / "evaluations.csv").open(
        "w",
        newline="",
    ) as stream:
        writer = csv.writer(stream)

        writer.writerow(
            [
                "variant",
                "origin_unix",
                "p1",
                "p2",
                "p3",
                "actual1",
                "actual2",
                "actual3",
                "risk",
                "first_risk_step",
                "peak_budget_ratio",
                "source",
            ]
        )

        for name, values in variants.items():
            upper = name.endswith("_upper")
            point = name in points

            kind = (
                "upper"
                if upper
                else "point"
                if point
                else "always_risk"
            )

            decisions = [
                gate(budget, row)
                for row in values
            ]

            flags = np.array(
                [
                    row[0]
                    for row in decisions
                ],
                bool,
            )

            tp = int(
                np.sum(flags & truth_flags)
            )

            fp = int(
                np.sum(flags & ~truth_flags)
            )

            fn = int(
                np.sum(~flags & truth_flags)
            )

            absolute = np.abs(
                values - group["truth"]
            )

            result = {
                "kind": kind,

                # 上界不是点估计，不用MAE给上界排名。
                "mae_packets_per_window": (
                    float(absolute.mean())
                    if point
                    else None
                ),

                "joint_empirical_coverage": (
                    float(
                        np.mean(
                            np.all(
                                group["truth"] <= values,
                                axis=1,
                            )
                        )
                    )
                    if upper
                    else None
                ),

                "coverage_by_horizon": (
                    np.mean(
                        group["truth"] <= values,
                        axis=0,
                    ).tolist()
                    if upper
                    else None
                ),

                "mean_upper_extra_packets": (
                    float(
                        np.mean(
                            values
                            - points[name[:-6]]
                        )
                    )
                    if upper
                    else None
                ),

                "risk_origins": int(
                    flags.sum()
                ),

                "risk_origins_fraction": float(
                    flags.mean()
                ),

                "risk_episode_count": (
                    risk_episode_count(
                        flags,
                        group["origin"],
                    )
                ),

                "decision_precision": (
                    tp / (tp + fp)
                    if tp + fp
                    else None
                ),

                "decision_recall": (
                    tp / (tp + fn)
                    if tp + fn
                    else None
                ),

                "false_risk_origins": fp,

                **pilot.event_metrics(
                    group,
                    values,
                    y,
                    stamps,
                    budget,
                    gate,
                    state["cuts"][3],
                ),
            }

            report["methods"][name] = result

            for i, origin in enumerate(
                group["origin"]
            ):
                writer.writerow(
                    [
                        name,
                        int(stamps[origin]),
                        *values[i],
                        *group["truth"][i],
                        *decisions[i],
                        "SIMULATED",
                    ]
                )

    (output / "report.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )

    print(
        json.dumps(report, indent=2),
        flush=True,
    )

    print(
        "[PASS] frozen point forecasts, calibrated upper "
        "and C simulated replay"
    )


if __name__ == "__main__":
    main()