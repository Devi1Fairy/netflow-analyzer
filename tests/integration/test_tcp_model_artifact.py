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
    )
    from tcp_linear_baseline import TcpLinearClassifier
    from tcp_model_artifact import (
        ARTIFACT_SCHEMA_VERSION,
        build_tcp_model_artifact,
        write_tcp_model_artifact,
    )
    from tcp_model_input import MODEL_FEATURE_NAMES

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

    print("[PASS] TCP model artifact snapshot and JSON writing")
    return 0


if __name__ == "__main__":
    sys.exit(main())