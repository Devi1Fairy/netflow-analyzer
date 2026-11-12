"""导出冻结MLP并验证纯C离线推理；所有容量均为SIMULATED。"""

import argparse
import csv
import io
import json
import math
import struct
import subprocess
import tempfile
import time
from pathlib import Path

import numpy as np
import torch

import evaluate_load_upper as upper
import run_load_forecast_pilot as pilot


MODEL_MAGIC = b"NFAF001\n"
CASES_MAGIC = b"NFAC001\n"

PARAMETER_KEYS = (
    "0.weight", "0.bias",
    "2.weight", "2.bias",
    "4.weight", "4.bias",
)

HEADER = [
    "origin_unix",
    "p1", "p2", "p3",
    "u1", "u2", "u3",
    "point_risk",
    "upper_risk",
    "upper_first_risk_step",
    "upper_peak_budget_ratio",
    "capacity_source",
]


def model_bytes(state, parameters, offset, budget):
    """小端binary64；先5个标量，再1571个权重/偏置。"""
    values = [
        state["mean"],
        state["std"],
        state["log_ceiling"],
        offset,
        budget,
    ]

    for key in PARAMETER_KEYS:
        values.extend(
            parameters[key]
            .detach()
            .cpu()
            .double()
            .flatten()
            .tolist()
        )

    if len(values) != 1576 or not np.isfinite(values).all():
        raise ValueError("invalid model values")

    # <表示小端，1576d表示1576个64位浮点数。
    return MODEL_MAGIC + struct.pack("<1576d", *values)


def cases_bytes(origins, raw):
    """每条：uint64 UTC秒 + 18个binary64历史包数。"""
    if (
        raw.shape != (len(origins), 18)
        or not 0 < len(origins) <= 100000
    ):
        raise ValueError("invalid case shape")

    data = bytearray(
        CASES_MAGIC + struct.pack("<Q", len(origins))
    )

    for stamp, values in zip(origins, raw):
        data.extend(
            struct.pack("<Q18d", int(stamp), *values)
        )

    return bytes(data)


def run_native(program, model, cases):
    """有界执行；非零退出时不使用stdout作为成功结果。"""
    start = time.perf_counter()

    done = subprocess.run(
        [str(program.resolve()), str(model), str(cases)],
        capture_output=True,
        text=True,
        timeout=45,
    )

    elapsed = (time.perf_counter() - start) * 1000

    if done.returncode != 0:
        raise RuntimeError(done.stderr.strip())

    reader = csv.DictReader(io.StringIO(done.stdout))

    if reader.fieldnames != HEADER:
        raise ValueError("native CSV header mismatch")

    rows = list(reader)

    if not rows or any(
        r["capacity_source"] != "SIMULATED"
        for r in rows
    ):
        raise ValueError("native rows/source mismatch")

    return done, rows, elapsed


def array(rows, prefix):
    return np.array([
        [
            float(r[prefix + str(h)])
            for h in (1, 2, 3)
        ]
        for r in rows
    ])


def self_test(program):
    """合成包测试，不读取真实网卡或设备。"""
    torch.manual_seed(7)
    torch.set_num_threads(1)

    model = torch.nn.Sequential(
        torch.nn.Linear(30, 32),
        torch.nn.ReLU(),
        torch.nn.Linear(32, 16),
        torch.nn.ReLU(),
        torch.nn.Linear(16, 3),
    )

    state = {
        "mean": 5.0,
        "std": 2.0,
        "log_ceiling": 30.0,
    }

    origin = np.array(
        [1700000029, 1700000629],
        dtype=np.int64,
    )

    raw = np.arange(
        36,
        dtype=np.float64,
    ).reshape(2, 18)

    raw[0, 0] = 0

    stamp = origin[:, None] + 600 * np.arange(3)
    day = (stamp % 86400) / 86400
    week = ((stamp // 86400 + 3) % 7 + day) / 7

    calendar = np.stack([
        np.sin(2 * np.pi * day),
        np.cos(2 * np.pi * day),
        np.sin(2 * np.pi * week),
        np.cos(2 * np.pi * week),
    ], -1)

    x = np.concatenate([
        (np.log1p(raw) - 5) / 2,
        calendar.reshape(2, 12),
    ], 1)

    group = {"x": x.astype(np.float32)}

    point = pilot.decode(
        pilot.mlp_z(model, group),
        state,
    )

    expected = upper.apply_upper(point, {
        "schema": upper.UPPER_SCHEMA,
        "horizon": 3,
        "log_offset": .3,
    })

    mb = model_bytes(
        state,
        model.state_dict(),
        .3,
        .05,
    )

    cb = cases_bytes(origin, raw)

    with tempfile.TemporaryDirectory(
        prefix="load-native-test-"
    ) as directory:
        directory = Path(directory)
        mp = directory / "model.bin"
        cp = directory / "cases.bin"

        mp.write_bytes(mb)
        cp.write_bytes(cb)

        _, rows, _ = run_native(program, mp, cp)

        np.testing.assert_allclose(
            array(rows, "p"),
            point,
            rtol=2e-6,
            atol=.01,
        )

        np.testing.assert_allclose(
            array(rows, "u"),
            expected,
            rtol=2e-6,
            atol=.01,
        )

        if [
            int(r["origin_unix"]) for r in rows
        ] != origin.tolist():
            raise AssertionError("timestamp changed")

        for r in rows:
            for prefix, flag in (
                ("p", "point_risk"),
                ("u", "upper_risk"),
            ):
                risk = any(
                    float(r[prefix + str(h)]) / 600 >= .05
                    for h in (1, 2, 3)
                )

                if int(r[flag]) != int(risk):
                    raise AssertionError(
                        "capacity result mismatch"
                    )

        print(
            "[PASS] native raw preprocessing, MLP, "
            "upper and SIMULATED capacity"
        )

        # 损坏模型/输入不能输出半份CSV。
        bad = [
            (mb[:7], cb),
            (b"BADF001\n" + mb[8:], cb),
            (mb, b"BADC001\n" + cb[8:]),
            (mb + b"x", cb),
            (mb, cb[:-1]),
            (mb, cb + b"x"),
            (mb, CASES_MAGIC + struct.pack("<Q", 0)),
        ]

        for index, value in (
            (0, math.nan),
            (1, 0),
            (3, -1),
            (4, 0),
            (5, math.inf),
            (5, 1e300),
            (3, 1000),
        ):
            damaged = bytearray(mb)

            struct.pack_into(
                "<d",
                damaged,
                8 + 8 * index,
                value,
            )

            bad.append((bytes(damaged), cb))

        # 第二条记录放坏值，确认第一行不会提前发布。
        for value in (math.nan, -1, .5, 2**53):
            damaged = bytearray(cb)

            struct.pack_into(
                "<d",
                damaged,
                16 + 152 + 8,
                value,
            )

            bad.append((mb, bytes(damaged)))

        damaged = bytearray(cb)

        struct.pack_into(
            "<Q",
            damaged,
            16 + 152,
            int(origin[0]),
        )

        bad.append((mb, bytes(damaged)))

        for model_data, case_data in bad:
            mp.write_bytes(model_data)
            cp.write_bytes(case_data)

            done = subprocess.run(
                [str(program.resolve()), str(mp), str(cp)],
                capture_output=True,
                timeout=5,
            )

            if done.returncode == 0 or done.stdout:
                raise AssertionError(
                    "malformed package published output"
                )

        print(
            "[PASS] invalid model/cases rejected "
            "with empty stdout"
        )


def reference_rows(path, variant, origins, expected):
    with path.open(
        newline="",
        encoding="utf-8",
    ) as stream:
        rows = [
            r for r in csv.DictReader(stream)
            if r["variant"] == variant
        ]

    if [
        int(r["origin_unix"]) for r in rows
    ] != origins.tolist():
        raise ValueError(
            "frozen reference origins changed"
        )

    np.testing.assert_allclose(
        array(rows, "p"),
        expected,
        rtol=1e-9,
        atol=1e-6,
    )

    return rows


def verify(args):
    frozen = json.loads(
        (
            args.upper_run / "upper_calibration.json"
        ).read_text()
    )

    if (
        frozen.get("schema") != upper.UPPER_SCHEMA
        or frozen.get("capacity_source") != "SIMULATED"
    ):
        raise ValueError(
            "upper artifact contract mismatch"
        )

    args.point_run = Path(frozen["point_run"])

    for key, path in (
        ("point_model_sha256", args.point_run / "model.pt"),
        ("point_ridge_sha256", args.point_run / "ridge.npy"),
        ("series_sha256", args.series_csv),
        ("times_sha256", args.times_archive),
    ):
        if pilot.fingerprint(path) != frozen[key]:
            raise ValueError(
                "frozen fingerprint changed: " + key
            )

    y, stamps = pilot.read_series(
        args.series_csv,
        args.times_archive,
    )

    saved, groups, state, predict = upper.load_frozen(
        args,
        y,
        stamps,
    )

    if frozen["safe_pps"] != saved["safe_pps"]:
        raise ValueError("simulated budget changed")

    test = groups["test"]
    origins = test["origin"]
    epochs = stamps[origins]

    point = predict(test)["mlp"]

    bounds = upper.apply_upper(
        point,
        frozen["upper_states"]["mlp"],
    )

    reference = args.upper_run / "evaluations.csv"

    pr = reference_rows(
        reference,
        "mlp",
        epochs,
        point,
    )

    ur = reference_rows(
        reference,
        "mlp_upper",
        epochs,
        bounds,
    )

    history = np.concatenate([
        origins[:, None] + np.arange(-12, 0),
        origins[:, None] + np.arange(3) - 144,
        origins[:, None] + np.arange(3) - 1008,
    ], axis=1)

    raw = y[history]

    weights = torch.load(
        args.point_run / "model.pt",
        map_location="cpu",
        weights_only=True,
    )

    args.output_root.mkdir(
        parents=True,
        exist_ok=True,
    )

    # 每次创建新目录，保留旧实验。
    output = Path(tempfile.mkdtemp(
        prefix="native-pilot-",
        dir=args.output_root,
    ))

    mp = output / "model.bin"
    cp = output / "cases.bin"

    mp.write_bytes(model_bytes(
        state,
        weights,
        frozen["upper_states"]["mlp"]["log_offset"],
        saved["safe_pps"],
    ))

    cp.write_bytes(cases_bytes(epochs, raw))

    done, rows, elapsed = run_native(
        args.program,
        mp,
        cp,
    )

    (output / "native.csv").write_text(done.stdout)
    (output / "native.stderr.txt").write_text(done.stderr)

    if [
        int(r["origin_unix"]) for r in rows
    ] != epochs.tolist():
        raise ValueError(
            "native case order/count changed"
        )

    native_point = array(rows, "p")
    native_upper = array(rows, "u")

    np.testing.assert_allclose(
        native_point,
        point,
        rtol=2e-6,
        atol=.01,
    )

    np.testing.assert_allclose(
        native_upper,
        bounds,
        rtol=2e-6,
        atol=.01,
    )

    point_mismatch = sum(
        int(a["point_risk"]) != int(b["risk"])
        for a, b in zip(rows, pr)
    )

    upper_mismatch = sum(
        int(a["upper_risk"]) != int(b["risk"])
        for a, b in zip(rows, ur)
    )

    first_mismatch = sum(
        int(a["upper_first_risk_step"])
        != int(b["first_risk_step"])
        for a, b in zip(rows, ur)
    )

    np.testing.assert_allclose(
        [float(r["upper_peak_budget_ratio"]) for r in rows],
        [float(r["peak_budget_ratio"]) for r in ur],
        rtol=2e-6,
        atol=1e-9,
    )

    if point_mismatch or upper_mismatch or first_mismatch:
        raise AssertionError(
            "native risk decisions differ; "
            "inspect numeric boundaries"
        )

    report = {
        "schema": "load_native_replay_v1",
        "capacity_source": "SIMULATED",
        "samples": len(rows),
        "model_parameters": 1571,
        "input": (
            "18 validated historical packet counts "
            "and UTC origin"
        ),
        "upper_calibration_sha256": pilot.fingerprint(
            args.upper_run / "upper_calibration.json"
        ),
        "point_model_sha256": frozen["point_model_sha256"],
        "program_sha256": pilot.fingerprint(args.program),
        "model_sha256": pilot.fingerprint(mp),
        "cases_sha256": pilot.fingerprint(cp),
        "point_max_abs_error": float(
            np.max(np.abs(native_point - point))
        ),
        "upper_max_abs_error": float(
            np.max(np.abs(native_upper - bounds))
        ),
        "point_max_relative_error": float(np.max(
            np.abs(native_point - point)
            / np.maximum(1, np.abs(point))
        )),
        "upper_max_relative_error": float(np.max(
            np.abs(native_upper - bounds)
            / np.maximum(1, np.abs(bounds))
        )),
        "point_risk_mismatches": point_mismatch,
        "upper_risk_mismatches": upper_mismatch,
        "upper_first_step_mismatches": first_mismatch,
        "native_process_elapsed_ms": elapsed,
        "timing_scope": (
            "VM process startup, file IO, inference, "
            "capacity, CSV"
        ),
        "online_window_builder_verified": False,
        "board_deployment_verified": False,
    }

    (output / "report.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )

    print(json.dumps(report, indent=2))
    print("output=" + str(output))

    print(
        "[PASS] native C forecast matches "
        "frozen Python reference"
    )


def main():
    parser = argparse.ArgumentParser(
        description=__doc__
    )

    parser.add_argument(
        "--program",
        required=True,
        type=Path,
    )

    parser.add_argument("--upper-run", type=Path)
    parser.add_argument("--series-csv", type=Path)
    parser.add_argument("--times-archive", type=Path)
    parser.add_argument("--capacity-library", type=Path)
    parser.add_argument("--output-root", type=Path)

    parser.add_argument(
        "--self-test",
        action="store_true",
    )

    args = parser.parse_args()
    torch.set_num_threads(1)

    if args.self_test:
        self_test(args.program)
        return

    if any(
        getattr(args, name) is None
        for name in (
            "upper_run",
            "series_csv",
            "times_archive",
            "capacity_library",
            "output_root",
        )
    ):
        parser.error(
            "verification requires all data/artifact paths"
        )

    verify(args)


if __name__ == "__main__":
    main()