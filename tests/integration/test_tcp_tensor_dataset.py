#!/usr/bin/env python3
"""验证TCP编码数据到PyTorch张量的转换。"""

import argparse
import sys
from dataclasses import replace
from pathlib import Path


def expect_value_error(callback):
    try:
        callback()
    except ValueError:
        return

    raise RuntimeError("invalid encoded dataset was accepted")


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

    from tcp_candidate_dataset import (
        EncodedTcpDataset,
        EncodedTcpSplit,
    )
    from tcp_model_input import MODEL_FEATURE_NAMES
    from tcp_tensor_dataset import tensorize_tcp_dataset

    def feature_row(duration, phase_position):
        numeric = (
            float(duration),
            2.0,
            120.0,
            140.0,
            60.0,
            70.0,
            0.5,
            0.25,
            1.0,
        )
        one_hot = tuple(
            float(position == phase_position)
            for position in range(9)
        )
        return numeric + one_hot

    train_row_a = feature_row(100, 3)
    train_row_b = feature_row(200, 8)
    validation_row = feature_row(300, 1)

    encoded_dataset = EncodedTcpDataset(
        train=EncodedTcpSplit(
            features=(
                train_row_a,
                train_row_b,
            ),
            labels=(0, 1),
            sample_ids=(
                "train-a",
                "train-b",
            ),
        ),
        validation=EncodedTcpSplit(
            features=(validation_row,),
            labels=(1,),
            sample_ids=("validation-a",),
        ),
        test=EncodedTcpSplit(
            features=(),
            labels=(),
            sample_ids=(),
        ),
    )

    tensor_dataset = tensorize_tcp_dataset(
        encoded_dataset
    )

    if tuple(tensor_dataset.train.features.shape) != (
        2,
        len(MODEL_FEATURE_NAMES),
    ):
        raise RuntimeError("unexpected training feature shape")

    if tuple(tensor_dataset.train.labels.shape) != (2,):
        raise RuntimeError("unexpected training label shape")

    if tensor_dataset.train.features.dtype != torch.float32:
        raise RuntimeError("features are not float32")

    if tensor_dataset.train.labels.dtype != torch.float32:
        raise RuntimeError("labels are not float32")

    if (
        tensor_dataset.train.features.device.type != "cpu"
        or tensor_dataset.train.labels.device.type != "cpu"
    ):
        raise RuntimeError("tensors are not on the CPU")

    if tensor_dataset.train.features.requires_grad:
        raise RuntimeError("input features unexpectedly require gradients")

    if tensor_dataset.train.labels.tolist() != [0.0, 1.0]:
        raise RuntimeError("unexpected training labels")

    if tensor_dataset.train.features[0].tolist() != list(
        train_row_a
    ):
        raise RuntimeError("unexpected training feature values")

    if tensor_dataset.train.sample_ids != (
        "train-a",
        "train-b",
    ):
        raise RuntimeError("sample IDs were not preserved")

    if tuple(tensor_dataset.test.features.shape) != (
        0,
        len(MODEL_FEATURE_NAMES),
    ):
        raise RuntimeError("empty test features have wrong shape")

    if tuple(tensor_dataset.test.labels.shape) != (0,):
        raise RuntimeError("empty test labels have wrong shape")

    # 修改Tensor不能反向修改作为输入的不可变Python元组。
    tensor_dataset.train.features[0, 0] = -1.0
    if encoded_dataset.train.features[0][0] != 100.0:
        raise RuntimeError("tensor does not own independent storage")

    # 单行特征宽度错误。
    wrong_width = replace(
        encoded_dataset,
        train=replace(
            encoded_dataset.train,
            features=(
                train_row_a[:-1],
                train_row_b,
            ),
        ),
    )
    expect_value_error(
        lambda: tensorize_tcp_dataset(wrong_width)
    )

    # 标签只能是整数0或1。
    invalid_label = replace(
        encoded_dataset,
        train=replace(
            encoded_dataset.train,
            labels=(0, 2),
        ),
    )
    expect_value_error(
        lambda: tensorize_tcp_dataset(invalid_label)
    )

    # 同一sample_id不能跨集合出现。
    duplicate_sample = replace(
        encoded_dataset,
        validation=replace(
            encoded_dataset.validation,
            sample_ids=("train-a",),
        ),
    )
    expect_value_error(
        lambda: tensorize_tcp_dataset(duplicate_sample)
    )

    # Python浮点数可能在转换成float32时溢出。
    overflowing_row = (
        1.0e100,
    ) + train_row_a[1:]
    overflowing_dataset = replace(
        encoded_dataset,
        train=replace(
            encoded_dataset.train,
            features=(
                overflowing_row,
                train_row_b,
            ),
        ),
    )
    expect_value_error(
        lambda: tensorize_tcp_dataset(
            overflowing_dataset
        )
    )

    print("[PASS] TCP tensor dataset conversion")
    return 0


if __name__ == "__main__":
    sys.exit(main())