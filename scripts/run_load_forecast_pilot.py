"""真实10分钟序列→三步预测→C容量判定；只做离线模拟回放。"""

import argparse
import csv
import ctypes as C
import hashlib
import io
import json
import math
import tarfile
import tempfile
from datetime import datetime, timezone
from decimal import Decimal
from pathlib import Path

import numpy as np
import torch
from torch import nn


WINDOW = 600
HORIZON = 3
WARMUP = 1008


def fingerprint(path):
    """分块计算文件SHA-256，不把整个文件读进内存。"""
    digest = hashlib.sha256()

    with path.open("rb") as stream:
        for block in iter(
            lambda: stream.read(1024 * 1024),
            b"",
        ):
            digest.update(block)

    return digest.hexdigest()


def read_series(csv_path, time_archive):
    """读取包数和原始时间轴；缺行保留为NaN。"""
    with tarfile.open(time_archive, "r:gz") as archive:
        text = archive.extractfile(
            "times/times_10_minutes.csv"
        ).read().decode()

    rows = list(csv.DictReader(io.StringIO(text)))
    epochs = []

    for index, row in enumerate(rows):
        stamp = datetime.fromisoformat(row["time"])

        if int(row["id_time"]) != index or stamp.tzinfo is None:
            raise ValueError("invalid time index or timezone")

        value = stamp.timestamp()

        if not value.is_integer():
            raise ValueError("fractional source timestamp")

        epochs.append(int(value))

    epochs = np.array(epochs, dtype=np.int64)

    if len(epochs) < 2000 or not np.all(np.diff(epochs) > 0):
        raise ValueError(
            "insufficient or non-increasing source timestamps"
        )

    # 缺行保持NaN，不能当成真实零流量。
    y = np.full(len(epochs), np.nan)
    seen = set()

    with csv_path.open(
        encoding="utf-8",
        newline="",
    ) as stream:
        reader = csv.DictReader(stream)

        if not {"id_time", "n_packets"}.issubset(
            reader.fieldnames or []
        ):
            raise ValueError("missing id_time or n_packets")

        for row in reader:
            index = int(row["id_time"])

            if not 0 <= index < len(y) or index in seen:
                raise ValueError(
                    "duplicate or out-of-range time index"
                )

            seen.add(index)
            raw = row["n_packets"].strip()

            if not raw or raw.lower() == "nan":
                continue

            # Decimal先精确检查整数，再转换为浮点模型输入。
            number = Decimal(raw)

            if (
                not number.is_finite()
                or number < 0
                or number != number.to_integral_value()
                or number > 2**53 - 1
            ):
                raise ValueError("invalid packet count")

            y[index] = float(number)

    return y, epochs


def complete_windows(epochs):
    """保守判断窗口是否有完整的600秒闭合证据。"""
    # 源时间轴不是UTC整10分钟对齐。
    # 不移动时间戳，也不填补间断。
    # 末尾没有下一个起点，不能确认窗口完整。
    return np.r_[
        np.diff(epochs) == WINDOW,
        False,
    ]


def samples(y, epochs):
    """构造因果输入、三步目标和时间分段。"""
    n = len(y)

    cuts = [
        0,
        int(n * .7),
        int(n * .8),
        int(n * .9),
        n,
    ]

    logs = np.log1p(y)
    closed = complete_windows(epochs)

    observed = logs[:cuts[1]][
        np.isfinite(logs[:cuts[1]])
        & closed[:cuts[1]]
    ]

    if len(observed) < 100:
        raise ValueError(
            "insufficient observed training data"
        )

    mean = float(observed.mean())
    std = max(float(observed.std()), .1)

    normalized = (logs - mean) / std

    origin = np.arange(
        WARMUP,
        n - HORIZON,
    )

    future = origin[:, None] + np.arange(HORIZON)

    # 所有包数输入都早于预测起点。
    history = np.concatenate(
        [
            origin[:, None] + np.arange(-12, 0),
            future - 144,
            future - 1008,
        ],
        axis=1,
    )

    # 累计非600秒边界数。
    # 从最早历史到最后目标闭合点之间不能存在间断。
    jumps = np.r_[
        0,
        np.cumsum(np.diff(epochs) != WINDOW),
    ]

    contiguous = (
        jumps[future[:, -1] + 1]
        == jumps[history.min(axis=1)]
    )

    valid = (
        contiguous
        & np.isfinite(y[history]).all(1)
        & np.isfinite(y[future]).all(1)
    )

    origin = origin[valid]
    future = future[valid]
    history = history[valid]

    # 未来日历是已知信息，未来真实包数不是输入。
    stamp = epochs[future]

    day = (stamp % 86400) / 86400
    week = ((stamp // 86400 + 3) % 7 + day) / 7

    calendar = np.stack(
        [
            np.sin(2 * np.pi * day),
            np.cos(2 * np.pi * day),
            np.sin(2 * np.pi * week),
            np.cos(2 * np.pi * week),
        ],
        axis=2,
    )

    x = np.concatenate(
        [
            normalized[history],
            calendar.reshape(len(origin), 12),
        ],
        axis=1,
    ).astype(np.float32)

    z = normalized[future].astype(np.float32)
    truth = y[future]

    groups = {}

    for name, left, right in zip(
        ("train", "validation", "calibration", "test"),
        cuts[:-1],
        cuts[1:],
    ):
        # 三个目标必须全部位于同一分段。
        mask = (
            (origin >= left)
            & (origin + HORIZON <= right)
        )

        if mask.sum() < 10:
            raise ValueError(
                "too few evaluable origins: " + name
            )

        groups[name] = {
            "x": x[mask],
            "z": z[mask],
            "truth": truth[mask],
            "origin": origin[mask],
            "possible": max(
                0,
                min(right, n - 1)
                - max(left, WARMUP)
                - HORIZON
                + 1,
            ),
        }

    state = {
        "mean": mean,
        "std": std,
        "log_ceiling": float(
            observed.max() + np.log(16)
        ),
        "cuts": cuts,
        "time_discontinuities": np.flatnonzero(
            np.diff(epochs) != WINDOW
        ).tolist(),
        "time_policy": (
            "source timestamps retained; "
            "complete contiguous 600-second spans only"
        ),
    }

    return groups, state


def decode(z, state):
    """把归一化输出还原成非负包数预测。"""
    logs = np.clip(
        z * state["std"] + state["mean"],
        0,
        state["log_ceiling"],
    )

    return np.expm1(logs)


def design(group):
    """为岭回归添加常数列，最后一列对应偏置。"""
    return np.column_stack(
        [
            group["x"].astype(np.float64),
            np.ones(len(group["x"])),
        ]
    )


def mlp_z(model, group):
    """执行前向推理，不计算或保存梯度。"""
    model.eval()

    with torch.inference_mode():
        x = torch.from_numpy(group["x"])

        # 第11列为上一窗口的归一化包数。
        # 网络学习相对于上一窗口的修正。
        return (
            model(x) + x[:, 11:12]
        ).numpy().astype(np.float64)


def baselines(group, y):
    """三个无需训练的对照方法。"""
    t = group["origin"][:, None]

    return {
        "previous": np.repeat(
            y[t - 1],
            HORIZON,
            axis=1,
        ),
        "daily": y[
            t + np.arange(HORIZON) - 144
        ],
        "weekly": y[
            t + np.arange(HORIZON) - 1008
        ],
    }


def fit(groups, state, epochs):
    """只使用训练和验证分段选择参数。"""
    train = groups["train"]
    validation = groups["validation"]

    x = design(train)
    v = design(validation)

    residual = (
        train["z"] - train["x"][:, 11:12]
    )

    ridge_best = None

    for alpha in (1., 10., 100., 1000.):
        penalty = np.eye(x.shape[1]) * alpha

        # 不对偏置施加正则。
        penalty[-1, -1] = 0

        weights = np.linalg.solve(
            x.T @ x + penalty,
            x.T @ residual,
        )

        prediction = decode(
            v @ weights
            + validation["x"][:, 11:12],
            state,
        )

        error = float(
            np.abs(
                prediction - validation["truth"]
            ).mean()
        )

        if (
            ridge_best is None
            or error < ridge_best[0]
        ):
            ridge_best = (
                error,
                alpha,
                weights,
            )

    model = nn.Sequential(
        nn.Linear(30, 32),
        nn.ReLU(),
        nn.Linear(32, 16),
        nn.ReLU(),
        nn.Linear(16, HORIZON),
    )

    # 初始修正为0，即初始预测等于上一窗口。
    nn.init.zeros_(model[-1].weight)
    nn.init.zeros_(model[-1].bias)

    optimizer = torch.optim.Adam(
        model.parameters(),
        lr=.002,
    )

    tx = torch.from_numpy(train["x"])
    tz = torch.from_numpy(train["z"])

    best = None
    stale = 0
    history = []

    for epoch in range(1, epochs + 1):
        model.train()
        order = torch.randperm(len(tx))

        for indices in order.split(256):
            optimizer.zero_grad(set_to_none=True)

            output = (
                model(tx[indices])
                + tx[indices, 11:12]
            )

            loss = nn.functional.smooth_l1_loss(
                output,
                tz[indices],
            )

            loss.backward()

            nn.utils.clip_grad_norm_(
                model.parameters(),
                1.,
            )

            optimizer.step()

        error = float(
            np.abs(
                decode(
                    mlp_z(model, validation),
                    state,
                )
                - validation["truth"]
            ).mean()
        )

        history.append(
            {
                "epoch": epoch,
                "validation_mae": error,
            }
        )

        if best is None or error < best[0]:
            # clone保存独立参数副本，后续训练不会修改它。
            best = (
                error,
                epoch,
                {
                    k: value.detach().clone()
                    for k, value
                    in model.state_dict().items()
                },
            )

            stale = 0
        else:
            stale += 1

        if stale >= 6:
            break

    model.load_state_dict(best[2])

    return (
        model,
        ridge_best,
        history,
        best[1],
    )


def capacity_gate(path):
    """通过明确的ABI调用本项目C共享库。"""
    library = C.CDLL(str(path.resolve()))

    library.load_capacity_bridge_version.argtypes = []
    library.load_capacity_bridge_version.restype = (
        C.c_uint32
    )

    if library.load_capacity_bridge_version() != 1:
        raise ValueError(
            "unsupported capacity bridge ABI"
        )

    call = library.load_capacity_assess_simulated_3

    # 四个double输入，再加size_t*和double*输出。
    call.argtypes = (
        [C.c_double] * 4
        + [
            C.POINTER(C.c_size_t),
            C.POINTER(C.c_double),
        ]
    )

    call.restype = C.c_int

    def evaluate(budget, values):
        if len(values) != HORIZON:
            raise ValueError(
                "three predicted windows required"
            )

        first = C.c_size_t()
        ratio = C.c_double()

        # byref()借用对象地址，供C函数填写输出。
        rc = call(
            float(budget),
            *[float(value) for value in values],
            C.byref(first),
            C.byref(ratio),
        )

        if rc < 0:
            raise ValueError(
                "C capacity assessment failed: errno="
                + str(-rc)
            )

        if rc not in (0, 1):
            raise ValueError(
                "invalid capacity bridge result"
            )

        return (
            rc,
            first.value,
            ratio.value,
        )

    return evaluate


def event_metrics(
    group,
    prediction,
    y,
    epochs,
    budget,
    gate,
    test_start,
):
    """按连续越界事件评价，避免重复计算重叠预测。"""
    closed = complete_windows(epochs)

    above = (
        closed
        & np.isfinite(y)
        & (y / WINDOW >= budget)
    )

    events = []

    for start in range(
        test_start + HORIZON - 1,
        len(y),
    ):
        if (
            above[start]
            and closed[start - 1]
            and np.isfinite(y[start - 1])
            and not above[start - 1]
        ):
            end = start + 1

            while end < len(y) and above[end]:
                end += 1

            events.append((start, end))

    leads = []
    covered = 0

    for start, end in events:
        best = None

        available = np.flatnonzero(
            (group["origin"] < start)
            & (
                group["origin"] + HORIZON
                > start
            )
        )

        covered += int(len(available) > 0)

        for i in available:
            origin = int(group["origin"][i])
            targets = origin + np.arange(HORIZON)

            inside = (
                (targets >= start)
                & (targets < end)
            )

            # 这是事后评价时用真实事件范围匹配预测，
            # 不会把事件范围用于模型输入或训练。
            matched = np.where(
                inside,
                prediction[i],
                0.,
            )

            if gate(budget, matched)[0]:
                lead = (
                    epochs[start] - epochs[origin]
                ) / 60

                best = (
                    lead
                    if best is None
                    else max(best, lead)
                )

        if best is not None:
            leads.append(float(best))

    return {
        "simulated_events": len(events),
        "events_with_forecast_opportunity": covered,
        "events_without_forecast_opportunity": (
            len(events) - covered
        ),
        "warned_before_onset": len(leads),
        "median_offline_lead_minutes": (
            float(np.median(leads))
            if leads
            else None
        ),
    }


def self_test(gate):
    """无需公开数据，验证ABI和关键数据边界。"""
    result = gate(
        1000.,
        [300000., 600000., 720000.],
    )

    assert result[0:2] == (1, 2)
    assert abs(result[2] - 1.2) < 1e-12

    for bad in (
        [-1., 0., 0.],
        [float("nan"), 0., 0.],
    ):
        try:
            gate(1000., bad)
        except ValueError:
            pass
        else:
            raise AssertionError(
                "invalid forecast accepted"
            )

    stamps = (
        np.arange(2400, dtype=np.int64) * WINDOW
        + 1696809600
    )

    y = 100. + np.arange(2400) % 144

    groups, state = samples(y, stamps)
    t = int(groups["test"]["origin"][0])

    changed = y.copy()
    changed[t] += 10000

    altered, altered_state = samples(
        changed,
        stamps,
    )

    np.testing.assert_array_equal(
        groups["test"]["x"][0],
        altered["test"]["x"][0],
    )

    assert state == altered_state

    assert not np.array_equal(
        groups["test"]["x"][1],
        altered["test"]["x"][1],
    )

    changed[t - 1] = np.nan
    missing, _ = samples(changed, stamps)

    assert t not in missing["test"]["origin"]

    changed[t - 1] = 0.
    zero, _ = samples(changed, stamps)

    assert t in zero["test"]["origin"]

    # 两个预测起点命中同一事件，只能算一个事件。
    tiny = np.zeros(10)
    tiny[4:6] = 720000.

    clock = (
        np.arange(10, dtype=np.int64) * WINDOW
    )

    known = {
        "origin": np.array([2, 3])
    }

    guesses = np.array(
        [
            [0., 0., 720000.],
            [0., 720000., 720000.],
        ]
    )

    event = event_metrics(
        known,
        guesses,
        tiny,
        clock,
        1000.,
        gate,
        0,
    )

    assert (
        event["simulated_events"]
        == event["warned_before_onset"]
        == 1
    )

    assert event["median_offline_lead_minutes"] == 20.

    shifted = stamps.copy()
    shifted[t - 5:] += 67

    try:
        samples(y, shifted)
    except ValueError:
        pass
    else:
        raise AssertionError(
            "discontinuous history accepted as complete"
        )

    print(
        "[PASS] C ABI, invalid values, causal features, "
        "train-only scaling, missing/zero, time gaps "
        "and event deduplication"
    )


def main(argv=None):
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--capacity-library",
        type=Path,
        required=True,
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
        "--simulated-safe-pps",
        type=float,
    )

    parser.add_argument(
        "--epochs",
        type=int,
        default=24,
    )

    parser.add_argument(
        "--self-test",
        action="store_true",
    )

    args = parser.parse_args(argv)

    torch.set_num_threads(1)
    torch.manual_seed(7)
    torch.use_deterministic_algorithms(True)

    gate = capacity_gate(args.capacity_library)

    if args.self_test:
        self_test(gate)
        return

    if not all(
        (
            args.series_csv,
            args.times_archive,
            args.output_root,
        )
    ):
        parser.error(
            "series-csv, times-archive "
            "and output-root are required"
        )

    if not 1 <= args.epochs <= 100:
        parser.error("epochs must be in 1..100")

    y, stamps = read_series(
        args.series_csv,
        args.times_archive,
    )

    groups, state = samples(y, stamps)

    train_values = y[:state["cuts"][1]]

    train_closed = complete_windows(
        stamps
    )[:state["cuts"][1]]

    budget = args.simulated_safe_pps

    if budget is None:
        # 仅用于固定模拟场景，不代表实测处理上限。
        budget = float(
            np.quantile(
                train_values[
                    np.isfinite(train_values)
                    & train_closed
                ] / WINDOW,
                .9,
            )
        )

    if not math.isfinite(budget) or budget <= 0:
        parser.error(
            "positive simulated budget required"
        )

    args.output_root.mkdir(
        parents=True,
        exist_ok=True,
    )

    # 每次创建新目录，保留原有实验结果。
    output = Path(
        tempfile.mkdtemp(
            prefix="capacity-pilot-",
            dir=args.output_root,
        )
    )

    print(
        "output=" + str(output),
        flush=True,
    )

    print(
        "capacity_source=SIMULATED safe_pps="
        + str(budget),
        flush=True,
    )

    model, ridge, history, best_epoch = fit(
        groups,
        state,
        args.epochs,
    )

    def predict(group):
        result = baselines(group, y)

        result["ridge"] = decode(
            design(group) @ ridge[2]
            + group["x"][:, 11:12],
            state,
        )

        result["mlp"] = decode(
            mlp_z(model, group),
            state,
        )

        return result

    validation = predict(
        groups["validation"]
    )

    selected = min(
        validation,
        key=lambda name: np.abs(
            validation[name]
            - groups["validation"]["truth"]
        ).mean(),
    )

    # 先冻结选择、预算及训练参数，再计算测试指标。
    metadata = {
        "schema": "load_forecast_pilot_v1",
        "window_seconds": WINDOW,
        "horizon": HORIZON,
        "architecture": [30, 32, 16, 3],
        "seed": 7,
        "preprocessing": state,
        "best_epoch": best_epoch,
        "training": {
            "optimizer": "Adam",
            "lr": .002,
            "batch_size": 256,
            "loss": "SmoothL1",
            "beta": 1.,
            "clip_norm": 1.,
            "max_epochs": args.epochs,
        },
        "ridge_alpha": ridge[1],
        "selected_by_validation_mae": selected,
        "history": history,
        "capacity_source": "SIMULATED",
        "safe_pps": budget,
        "budget_policy": (
            "explicit"
            if args.simulated_safe_pps is not None
            else "training_q90_scenario"
        ),
        "series_csv": str(
            args.series_csv.resolve()
        ),
        "series_sha256": fingerprint(
            args.series_csv
        ),
        "times_sha256": fingerprint(
            args.times_archive
        ),
        "capacity_library_sha256": fingerprint(
            args.capacity_library
        ),
    }

    (output / "model.json").write_text(
        json.dumps(metadata, indent=2) + "\n"
    )

    torch.save(
        model.state_dict(),
        output / "model.pt",
    )

    np.save(
        output / "ridge.npy",
        ridge[2],
    )

    group = groups["test"]
    predictions = predict(group)

    truth_flags = np.array(
        [
            gate(budget, row)[0]
            for row in group["truth"]
        ],
        bool,
    )

    report = {
        "capacity_source": "SIMULATED",
        "safe_pps": budget,
        "selected_by_validation_mae": selected,
        "splits": {
            name: {
                "evaluable_origins": len(
                    values["origin"]
                ),
                "possible_origins": values["possible"],
                "first_origin_utc": datetime.fromtimestamp(
                    int(stamps[values["origin"][0]]),
                    timezone.utc,
                ).isoformat(),
                "last_origin_utc": datetime.fromtimestamp(
                    int(stamps[values["origin"][-1]]),
                    timezone.utc,
                ).isoformat(),
            }
            for name, values in groups.items()
        },
        "methods": {},
        "decision_definition": (
            "overlapping forecast origins, "
            "not independent alarm events"
        ),
        "event_definition": (
            "continuous actual exceedance "
            "of simulated PPS budget"
        ),
        "lead_definition": (
            "offline window-onset lead, "
            "excludes runtime delay"
        ),
    }

    with (output / "test_predictions.csv").open(
        "w",
        newline="",
    ) as stream:
        writer = csv.writer(stream)

        writer.writerow(
            [
                "method",
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
                "capacity_source",
            ]
        )

        for name, prediction in predictions.items():
            decisions = [
                gate(budget, row)
                for row in prediction
            ]

            flags = np.array(
                [
                    result[0]
                    for result in decisions
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
                prediction - group["truth"]
            )

            total = float(
                group["truth"].sum()
            )

            result = {
                "mae_packets_per_window": float(
                    absolute.mean()
                ),
                "mae_by_horizon": (
                    absolute.mean(0).tolist()
                ),
                "wape": (
                    float(absolute.sum() / total)
                    if total
                    else None
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
                **event_metrics(
                    group,
                    prediction,
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
                        *prediction[i],
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
        "[PASS] offline forecasting and "
        "SIMULATED C-capacity replay; "
        "not board deployment"
    )


if __name__ == "__main__":
    main()