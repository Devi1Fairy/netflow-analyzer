#!/usr/bin/env python3
"""验证TCP特征预处理只从训练集合拟合参数。"""

import argparse
import math
import sys
from dataclasses import replace
from pathlib import Path


def expect_value_error(callback):
    try:
        callback()
    except ValueError:
        return

    raise RuntimeError("invalid preprocessing input was accepted")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--scripts-dir",
        required=True,
        type=Path,
    )
    arguments = parser.parse_args()

    sys.path.insert(
        0,
        str(arguments.scripts_dir.resolve()),
    )

    import torch

    from tcp_feature_preprocessing import (
        LOG_STANDARDIZED_FEATURE_NAMES,
        PREPROCESSING_VERSION,
        apply_tcp_preprocessing,
        fit_tcp_preprocessing,
        preprocess_tcp_dataset,
    )
    from tcp_model_input import MODEL_FEATURE_NAMES
    from tcp_tensor_dataset import (
        TensorTcpDataset,
        TensorTcpSplit,
    )

    def feature_row(log_values, phase_position):
        if len(log_values) != 6:
            raise RuntimeError("test log value count differs")

        scale_values = tuple(
            math.expm1(value)
            for value in log_values
        )
        unchanged_values = (
            0.25,
            0.75,
            1.0,
        ) + tuple(
            float(position == phase_position)
            for position in range(9)
        )
        return scale_values + unchanged_values

    def tensor_split(rows, labels, sample_ids):
        if rows:
            features = torch.tensor(
                rows,
                dtype=torch.float32,
            )
        else:
            features = torch.empty(
                (0, len(MODEL_FEATURE_NAMES)),
                dtype=torch.float32,
            )

        return TensorTcpSplit(
            features=features,
            labels=torch.tensor(
                labels,
                dtype=torch.float32,
            ),
            sample_ids=sample_ids,
        )

    train_row_a = feature_row(
        (1.0, 1.0, 1.0, 1.0, 1.0, 2.0),
        3,
    )
    train_row_b = feature_row(
        (3.0, 3.0, 3.0, 3.0, 3.0, 2.0),
        8,
    )
    validation_row = feature_row(
        (5.0, 5.0, 5.0, 5.0, 5.0, 5.0),
        1,
    )

    dataset = TensorTcpDataset(
        train=tensor_split(
            (train_row_a, train_row_b),
            (0, 1),
            ("train-a", "train-b"),
        ),
        validation=tensor_split(
            (validation_row,),
            (1,),
            ("validation-a",),
        ),
        test=tensor_split(
            (),
            (),
            (),
        ),
    )

    original_train = dataset.train.features.clone()
    original_validation = (
        dataset.validation.features.clone()
    )

    state = fit_tcp_preprocessing(dataset.train)

    if state.version != PREPROCESSING_VERSION:
        raise RuntimeError("unexpected preprocessing version")

    if state.feature_names != MODEL_FEATURE_NAMES:
        raise RuntimeError("feature names were not preserved")

    if (
        state.log_standardized_feature_names
        != LOG_STANDARDIZED_FEATURE_NAMES
    ):
        raise RuntimeError(
            "transformed feature names differ"
        )

    expected_means = torch.full(
        (6,),
        2.0,
        dtype=torch.float64,
    )
    expected_scales = torch.ones(
        6,
        dtype=torch.float64,
    )

    if not torch.allclose(
        torch.tensor(
            state.log_means,
            dtype=torch.float64,
        ),
        expected_means,
        atol=1.0e-6,
        rtol=0.0,
    ):
        raise RuntimeError("unexpected training means")

    if not torch.allclose(
        torch.tensor(
            state.log_scales,
            dtype=torch.float64,
        ),
        expected_scales,
        atol=1.0e-6,
        rtol=0.0,
    ):
        raise RuntimeError("unexpected training scales")

    transformed = preprocess_tcp_dataset(
        dataset,
        state,
    )

    expected_train_prefix = torch.tensor(
        (
            (-1.0, -1.0, -1.0, -1.0, -1.0, 0.0),
            (1.0, 1.0, 1.0, 1.0, 1.0, 0.0),
        ),
        dtype=torch.float32,
    )
    if not torch.allclose(
        transformed.train.features[:, :6],
        expected_train_prefix,
        atol=1.0e-6,
        rtol=0.0,
    ):
        raise RuntimeError(
            "training standardization differs"
        )

    expected_validation_prefix = torch.full(
        (1, 6),
        3.0,
        dtype=torch.float32,
    )
    if not torch.allclose(
        transformed.validation.features[:, :6],
        expected_validation_prefix,
        atol=1.0e-6,
        rtol=0.0,
    ):
        raise RuntimeError(
            "validation did not reuse training parameters"
        )

    # 比例、握手标志和TCP阶段独热列必须保持原值。
    if not torch.equal(
        transformed.train.features[:, 6:],
        original_train[:, 6:],
    ):
        raise RuntimeError(
            "non-scaled training features changed"
        )

    if not torch.equal(
        transformed.validation.features[:, 6:],
        original_validation[:, 6:],
    ):
        raise RuntimeError(
            "non-scaled validation features changed"
        )

    # 输入张量不能被原地修改。
    if not torch.equal(
        dataset.train.features,
        original_train,
    ):
        raise RuntimeError("training input was modified")

    if not torch.equal(
        dataset.validation.features,
        original_validation,
    ):
        raise RuntimeError("validation input was modified")

    if (
        transformed.train.labels.data_ptr()
        == dataset.train.labels.data_ptr()
    ):
        raise RuntimeError("labels unexpectedly share storage")

    if transformed.train.sample_ids != dataset.train.sample_ids:
        raise RuntimeError("sample IDs were not preserved")

    if tuple(transformed.test.features.shape) != (
        0,
        len(MODEL_FEATURE_NAMES),
    ):
        raise RuntimeError("empty test shape changed")

    # 参数的特征契约不匹配时必须拒绝。
    wrong_names = replace(
        state,
        feature_names=state.feature_names[:-1],
    )
    expect_value_error(
        lambda: apply_tcp_preprocessing(
            "validation",
            dataset.validation,
            wrong_names,
        )
    )

    # 标准差不能为0或负数。
    invalid_scale = replace(
        state,
        log_scales=(
            0.0,
        ) + state.log_scales[1:],
    )
    expect_value_error(
        lambda: apply_tcp_preprocessing(
            "validation",
            dataset.validation,
            invalid_scale,
        )
    )

    # 需要做log1p的原始特征不能为负数。
    negative_features = (
        dataset.validation.features.clone()
    )
    negative_features[0, 0] = -1.0
    negative_split = replace(
        dataset.validation,
        features=negative_features,
    )
    expect_value_error(
        lambda: apply_tcp_preprocessing(
            "validation",
            negative_split,
            state,
        )
    )

    # 空集合不能用于拟合。
    expect_value_error(
        lambda: fit_tcp_preprocessing(dataset.test)
    )

    print("[PASS] TCP feature preprocessing")
    return 0


if __name__ == "__main__":
    sys.exit(main())