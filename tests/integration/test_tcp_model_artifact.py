#!/usr/bin/env python3
"""验证模型JSON字段、独立快照及失败不写入。"""

import argparse
import io
import json
import sys
from dataclasses import replace
from pathlib import Path


def expect_value_error(callback):
    try:
        callback()
    except ValueError:
        return
    raise RuntimeError("invalid artifact input was accepted")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scripts-dir", required=True, type=Path)
    arguments = parser.parse_args()
    sys.path.insert(0, str(arguments.scripts_dir.resolve()))

    import torch

    from tcp_feature_preprocessing import (
        LOG_STANDARDIZED_FEATURE_NAMES,
        PREPROCESSING_VERSION,
        TcpPreprocessingState,
        apply_tcp_preprocessing,
    )
    from tcp_linear_baseline import TcpLinearClassifier
    from tcp_model_artifact import (
        ARTIFACT_SCHEMA_VERSION,
        build_tcp_model_artifact,
        read_tcp_model_artifact,
        write_tcp_model_artifact,
    )
    from tcp_model_input import MODEL_FEATURE_NAMES
    from tcp_tensor_dataset import TensorTcpSplit

    # 固定参数检查保存契约；本测试不训练，也不读公开数据。
    model = TcpLinearClassifier()
    with torch.no_grad():
        model.weight.copy_(torch.linspace(
            -0.5, 0.5, len(MODEL_FEATURE_NAMES),
            dtype=torch.float32,
        ))
        model.bias.fill_(0.25)

    state = TcpPreprocessingState(
        version=PREPROCESSING_VERSION,
        feature_names=MODEL_FEATURE_NAMES,
        log_standardized_feature_names=LOG_STANDARDIZED_FEATURE_NAMES,
        log_means=(1.0, 2.0, 3.0, 4.0, 5.0, 6.0),
        log_scales=(0.5, 1.0, 1.5, 2.0, 2.5, 3.0),
    )
    expected_weights = model.weight.detach().tolist()
    artifact = build_tcp_model_artifact(model, state)

    if (
        artifact["artifact_schema_version"] != ARTIFACT_SCHEMA_VERSION
        or artifact["feature_schema_version"] != "flow_features_v1"
        or artifact["encoding_version"] != "tcp_input_v1"
        or artifact["model_version"] != "tcp_linear_v1"
        or artifact["feature_names"] != list(MODEL_FEATURE_NAMES)
        or artifact["label_values"] != {"benign": 0, "malicious": 1}
        or artifact["parameter_dtype"] != "float32"
        or artifact["logit_threshold"] != 0.0
        or artifact["model"]["weights"] != expected_weights
        or artifact["model"]["bias"] != 0.25
    ):
        raise RuntimeError("unexpected model artifact fields")

    if artifact["preprocessing"] != {
        "version": PREPROCESSING_VERSION,
        "statistics_dtype": "float64",
        "log_standardized_feature_names": list(
            LOG_STANDARDIZED_FEATURE_NAMES
        ),
        "log_means": list(state.log_means),
        "log_scales": list(state.log_scales),
    }:
        raise RuntimeError("preprocessing parameters differ")

    output = io.StringIO()
    write_tcp_model_artifact(output, model, state)
    if output.closed or not output.getvalue().endswith("\n"):
        raise RuntimeError("borrowed output stream contract differs")
    if json.loads(output.getvalue()) != artifact:
        raise RuntimeError("JSON values differ after round-trip")

    # 从刚保存的JSON恢复模型，检查借用流不会被关闭。
    source = io.StringIO(output.getvalue())
    loaded_model, loaded_state = read_tcp_model_artifact(
        source
    )

    if (
        source.closed
        or loaded_state != state
        or loaded_model.training
    ):
        raise RuntimeError(
            "loaded model or preprocessing state differs"
        )

    # 构造三条固定输入；本测试不读取真实数据、不训练。
    raw_features = torch.zeros(
        (3, len(MODEL_FEATURE_NAMES)),
        dtype=torch.float32,
    )
    raw_features[:, :6] = torch.tensor(
        [
            [0.0, 1.0, 60.0, 60.0, 60.0, 60.0],
            [10.0, 2.0, 120.0, 120.0, 60.0, 60.0],
            [1000.0, 3.0, 180.0, 180.0, 60.0, 60.0],
        ],
        dtype=torch.float32,
    )
    phase_index = MODEL_FEATURE_NAMES.index(
        "tcp_phase_syn-seen"
    )
    raw_features[:, phase_index] = 1.0

    raw = TensorTcpSplit(
        features=raw_features,
        labels=torch.tensor(
            [0.0, 1.0, 0.0],
            dtype=torch.float32,
        ),
        sample_ids=(
            "synthetic-a",
            "synthetic-b",
            "synthetic-c",
        ),
    )

    before = apply_tcp_preprocessing(
        "before", raw, state
    )
    after = apply_tcp_preprocessing(
        "after", raw, loaded_state
    )

    with torch.no_grad():
        logits_before = model(before.features)
        logits_after = loaded_model(after.features)

    # 同机保存／加载应恢复相同参数和相同计算结果。
    if (
        not torch.equal(before.features, after.features)
        or not torch.equal(logits_before, logits_after)
    ):
        raise RuntimeError(
            "inference differs after artifact round-trip"
        )

    # 修改加载后的模型，不得影响原模型。
    with torch.no_grad():
        loaded_model.weight.add_(1.0)
    if model.weight.detach().tolist() != expected_weights:
        raise RuntimeError(
            "loaded model aliases original parameters"
        )

    # 每次重新解析，确保各个坏样例互不影响。
    for section, field, value in (
        (None, "artifact_schema_version", "unknown"),
        (None, "feature_schema_version", "unknown"),
        (None, "encoding_version", "unknown"),
        (None, "model_version", "unknown"),
        (None, "parameter_dtype", "float64"),
        (
            None,
            "feature_names",
            list(reversed(MODEL_FEATURE_NAMES)),
        ),
        (None, "logit_threshold", True),
        (None, "logit_threshold", 1.0),
        (
            None,
            "label_values",
            {"benign": False, "malicious": True},
        ),
        ("model", "weights", [0.0] * 17),
        ("model", "bias", float("nan")),
        ("preprocessing", "log_scales", [0.0] * 6),
        (
            "preprocessing",
            "statistics_dtype",
            "float32",
        ),
    ):
        bad = json.loads(output.getvalue())
        target = bad if section is None else bad[section]
        target[field] = value

        expect_value_error(
            lambda: read_tcp_model_artifact(
                io.StringIO(json.dumps(bad))
            )
        )

    for invalid_json in (
        "[]",
        '{"model": 1, "model": 2}',
        '{"broken":',
    ):
        expect_value_error(
            lambda: read_tcp_model_artifact(
                io.StringIO(invalid_json)
            )
        )

    # 改原模型不得改变之前生成的快照。
    with torch.no_grad():
        model.weight.add_(10.0)
        model.bias.fill_(8.0)
    if (
        artifact["model"]["weights"] != expected_weights
        or artifact["model"]["bias"] != 0.25
    ):
        raise RuntimeError("artifact still aliases model parameters")

    valid_model = TcpLinearClassifier()
    protected = io.StringIO("keep\n")
    protected.seek(0, io.SEEK_END)
    for bad_state in (
        replace(state, version="unknown"),
        replace(state, log_scales=(0.0,) + state.log_scales[1:]),
        replace(state, log_means=(True,) + state.log_means[1:]),
    ):
        expect_value_error(
            lambda: write_tcp_model_artifact(
                protected, valid_model, bad_state
            )
        )
        if protected.getvalue() != "keep\n":
            raise RuntimeError("invalid state changed output")

    with torch.no_grad():
        valid_model.weight[0] = float("nan")
    expect_value_error(
        lambda: write_tcp_model_artifact(
            protected, valid_model, state
        )
    )
    if protected.getvalue() != "keep\n":
        raise RuntimeError("invalid model changed output")

    print(
        "[PASS] TCP model artifact save/load "
        "and inference consistency"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())