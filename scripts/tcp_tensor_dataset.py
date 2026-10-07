"""把已验证的TCP候选数据集转换为PyTorch CPU张量。"""

import math
from dataclasses import dataclass
from typing import Tuple

import torch

from tcp_candidate_dataset import (
    EncodedTcpDataset,
    EncodedTcpSplit,
)
from tcp_model_input import MODEL_FEATURE_NAMES


FEATURE_COUNT = len(MODEL_FEATURE_NAMES)


@dataclass(frozen=True)
class TensorTcpSplit:
    """一个集合的模型张量，以及不进入模型的样本身份。"""

    features: torch.Tensor
    labels: torch.Tensor
    sample_ids: Tuple[str, ...]


@dataclass(frozen=True)
class TensorTcpDataset:
    """训练、验证和可选测试集合的CPU张量。"""

    train: TensorTcpSplit
    validation: TensorTcpSplit
    test: TensorTcpSplit


def _tensorize_split(
    split_name: str,
    split: EncodedTcpSplit,
) -> TensorTcpSplit:
    """检查一个编码集合并复制为CPU float32张量。"""

    sample_count = len(split.features)

    if (
        len(split.labels) != sample_count
        or len(split.sample_ids) != sample_count
    ):
        raise ValueError(
            f"{split_name}: encoded split lengths differ"
        )

    if len(set(split.sample_ids)) != sample_count:
        raise ValueError(
            f"{split_name}: duplicate sample_id"
        )

    if any(
        not isinstance(sample_id, str) or not sample_id
        for sample_id in split.sample_ids
    ):
        raise ValueError(
            f"{split_name}: invalid sample_id"
        )

    for row_number, values in enumerate(
        split.features,
        start=1,
    ):
        if len(values) != FEATURE_COUNT:
            raise ValueError(
                f"{split_name}: feature row {row_number} "
                f"does not contain {FEATURE_COUNT} values"
            )

        if any(
            not math.isfinite(value)
            for value in values
        ):
            raise ValueError(
                f"{split_name}: feature row {row_number} "
                "contains a non-finite value"
            )

    if any(
        type(label) is not int or label not in (0, 1)
        for label in split.labels
    ):
        raise ValueError(
            f"{split_name}: labels must be integer 0 or 1"
        )

    if sample_count == 0:
        feature_tensor = torch.empty(
            (0, FEATURE_COUNT),
            dtype=torch.float32,
            device="cpu",
        )
    else:
        feature_tensor = torch.tensor(
            split.features,
            dtype=torch.float32,
            device="cpu",
        )

    label_tensor = torch.tensor(
        split.labels,
        dtype=torch.float32,
        device="cpu",
    )

    # Python中的有限数转换为float32时仍可能溢出，因此转换后再检查。
    if not torch.isfinite(feature_tensor).all().item():
        raise ValueError(
            f"{split_name}: feature value cannot be represented "
            "as float32"
        )

    return TensorTcpSplit(
        features=feature_tensor,
        labels=label_tensor,
        sample_ids=split.sample_ids,
    )


def tensorize_tcp_dataset(
    dataset: EncodedTcpDataset,
) -> TensorTcpDataset:
    """把完整编码数据集复制为CPU张量。

    输入对象由调用者拥有且不会被修改。返回的Tensor拥有独立存储；
    sample_ids是不可变tuple，因此可以安全共享。训练集和验证集必须
    非空，test允许为空。
    """

    if not dataset.train.features:
        raise ValueError("training split is empty")

    if not dataset.validation.features:
        raise ValueError("validation split is empty")

    all_sample_ids = (
        dataset.train.sample_ids
        + dataset.validation.sample_ids
        + dataset.test.sample_ids
    )
    if len(set(all_sample_ids)) != len(all_sample_ids):
        raise ValueError(
            "sample_id appears in more than one split"
        )

    return TensorTcpDataset(
        train=_tensorize_split(
            "train",
            dataset.train,
        ),
        validation=_tensorize_split(
            "validation",
            dataset.validation,
        ),
        test=_tensorize_split(
            "test",
            dataset.test,
        ),
    )