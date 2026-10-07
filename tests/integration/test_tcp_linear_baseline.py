#!/usr/bin/env python3
"""验证TCP线性训练、类别权重和二分类指标。"""

import argparse
import math
import sys
from pathlib import Path


def expect_value_error(callback):
    try:
        callback()
    except ValueError:
        return

    raise RuntimeError("invalid baseline input was accepted")


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

    from tcp_linear_baseline import (
        evaluate_binary_logits,
        train_tcp_linear_baseline,
    )
    from tcp_model_input import MODEL_FEATURE_NAMES

    # 构造可分样本：只有第一列变化，其余列为0。
    features = torch.zeros(
        (4, len(MODEL_FEATURE_NAMES)),
        dtype=torch.float32,
    )
    features[:, 0] = torch.tensor(
        [-2.0, -1.0, 1.0, 2.0],
        dtype=torch.float32,
    )
    labels = torch.tensor(
        [0.0, 0.0, 1.0, 1.0],
        dtype=torch.float32,
    )

    original_features = features.clone()
    original_labels = labels.clone()
    original_rng = torch.random.get_rng_state().clone()

    result = train_tcp_linear_baseline(
        features,
        labels,
    )

    if len(result.loss_history) != 101:
        raise RuntimeError("unexpected loss history length")

    if (
        result.loss_history[-1]
        >= result.loss_history[0] * 0.25
    ):
        raise RuntimeError("training did not sufficiently reduce loss")

    if result.positive_weight != 1.0:
        raise RuntimeError("balanced labels have wrong weight")

    parameter_count = sum(
        parameter.numel()
        for parameter in result.model.parameters()
    )
    if parameter_count != 19:
        raise RuntimeError("unexpected model parameter count")

    with torch.no_grad():
        logits = result.model(features)

    if tuple(logits.shape) != (4,):
        raise RuntimeError("unexpected model output shape")

    metrics = evaluate_binary_logits(labels, logits)
    if (
        metrics.accuracy != 1.0
        or metrics.malicious_recall != 1.0
        or metrics.benign_false_positive_rate != 0.0
    ):
        raise RuntimeError("separable samples were misclassified")

    if (
        not torch.equal(features, original_features)
        or not torch.equal(labels, original_labels)
    ):
        raise RuntimeError("training modified caller inputs")

    # 在同一环境重复运行，结果应保持一致。
    repeated = train_tcp_linear_baseline(features, labels)

    if result.loss_history != repeated.loss_history:
        raise RuntimeError("repeated loss history differs")

    for name, value in result.model.state_dict().items():
        if not torch.equal(
            value,
            repeated.model.state_dict()[name],
        ):
            raise RuntimeError("repeated model parameters differ")

    if not torch.equal(
        original_rng,
        torch.random.get_rng_state(),
    ):
        raise RuntimeError("training changed the random generator")

    # 一条正常、三条恶意：正类权重必须为1/3。
    unbalanced = train_tcp_linear_baseline(
        features,
        torch.tensor(
            [0.0, 1.0, 1.0, 1.0],
            dtype=torch.float32,
        ),
        epochs=1,
    )
    if not math.isclose(
        unbalanced.positive_weight,
        1.0 / 3.0,
    ):
        raise RuntimeError("incorrect class weight")

    # 手工构造TN=1、FP=1、FN=1、TP=2。
    # 最后一项logit为0，必须按固定规则预测恶意。
    example = evaluate_binary_logits(
        torch.tensor(
            [0.0, 0.0, 1.0, 1.0, 1.0],
            dtype=torch.float32,
        ),
        torch.tensor(
            [-1.0, 1.0, -1.0, 1.0, 0.0],
            dtype=torch.float32,
        ),
    )

    if (
        example.true_negative,
        example.false_positive,
        example.false_negative,
        example.true_positive,
    ) != (1, 1, 1, 2):
        raise RuntimeError("incorrect confusion counts")

    if (
        not math.isclose(example.accuracy, 0.6)
        or not math.isclose(
            example.malicious_precision,
            2.0 / 3.0,
        )
        or not math.isclose(
            example.malicious_recall,
            2.0 / 3.0,
        )
        or not math.isclose(
            example.benign_false_positive_rate,
            0.5,
        )
        or not math.isclose(
            example.balanced_accuracy,
            7.0 / 12.0,
        )
    ):
        raise RuntimeError("incorrect classification metrics")

    # 全部预测正常时，精确率分母为0。
    no_positive_prediction = evaluate_binary_logits(
        labels,
        -torch.ones_like(labels),
    )
    if no_positive_prediction.malicious_precision is not None:
        raise RuntimeError("undefined precision was not None")

    # 实际没有恶意样本时，召回率和均衡准确率无定义。
    benign_only = evaluate_binary_logits(
        torch.zeros_like(labels),
        -torch.ones_like(labels),
    )
    if (
        benign_only.malicious_recall is not None
        or benign_only.balanced_accuracy is not None
    ):
        raise RuntimeError("undefined metrics were not None")

    expect_value_error(
        lambda: train_tcp_linear_baseline(
            features,
            torch.ones_like(labels),
        )
    )
    expect_value_error(
        lambda: train_tcp_linear_baseline(
            features,
            labels,
            epochs=0,
        )
    )
    expect_value_error(
        lambda: train_tcp_linear_baseline(
            features,
            labels,
            learning_rate=float("nan"),
        )
    )
    expect_value_error(
        lambda: evaluate_binary_logits(
            labels,
            torch.full_like(labels, float("inf")),
        )
    )

    print("[PASS] TCP linear baseline training and metrics")
    return 0


if __name__ == "__main__":
    sys.exit(main())