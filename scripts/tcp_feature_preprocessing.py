"""仅使用训练集拟合TCP模型特征预处理参数。"""

import math
from dataclasses import dataclass
from typing import Tuple

import torch

from tcp_model_input import MODEL_FEATURE_NAMES
from tcp_tensor_dataset import (
    TensorTcpDataset,
    TensorTcpSplit,
)


PREPROCESSING_VERSION = "tcp_preprocessing_v1"

LOG_STANDARDIZED_FEATURE_NAMES = (
    "duration_microseconds",
    "total_packet_count",
    "total_captured_byte_count",
    "total_wire_byte_count",
    "mean_captured_bytes_per_packet",
    "mean_wire_bytes_per_packet",
)

LOG_STANDARDIZED_INDICES = tuple(
    MODEL_FEATURE_NAMES.index(name)
    for name in LOG_STANDARDIZED_FEATURE_NAMES
)

MINIMUM_SCALE = 1.0e-12


@dataclass(frozen=True)
class TcpPreprocessingState:
    """可保存并供验证、测试和推理复用的预处理参数。"""

    version: str
    feature_names: Tuple[str, ...]
    log_standardized_feature_names: Tuple[str, ...]
    log_means: Tuple[float, ...]
    log_scales: Tuple[float, ...]


def _validate_tensor_split(
    split_name: str,
    split: TensorTcpSplit,
) -> None:
    """验证张量集合的形状、类型、设备和样本对应关系。"""

    if (
        split.features.ndim != 2
        or split.features.shape[1] != len(MODEL_FEATURE_NAMES)
    ):
        raise ValueError(
            f"{split_name}: unexpected feature shape"
        )

    if split.labels.ndim != 1:
        raise ValueError(
            f"{split_name}: unexpected label shape"
        )

    sample_count = split.features.shape[0]
    if (
        split.labels.shape[0] != sample_count
        or len(split.sample_ids) != sample_count
    ):
        raise ValueError(
            f"{split_name}: tensor split lengths differ"
        )

    if split.features.dtype != torch.float32:
        raise ValueError(
            f"{split_name}: features must be float32"
        )

    if split.labels.dtype != torch.float32:
        raise ValueError(
            f"{split_name}: labels must be float32"
        )

    if (
        split.features.device.type != "cpu"
        or split.labels.device.type != "cpu"
    ):
        raise ValueError(
            f"{split_name}: tensors must be on the CPU"
        )

    if not torch.isfinite(split.features).all().item():
        raise ValueError(
            f"{split_name}: features contain non-finite values"
        )

    if not torch.isfinite(split.labels).all().item():
        raise ValueError(
            f"{split_name}: labels contain non-finite values"
        )

    if split.labels.numel() > 0:
        valid_labels = (
            (split.labels == 0.0)
            | (split.labels == 1.0)
        )
        if not valid_labels.all().item():
            raise ValueError(
                f"{split_name}: labels must be 0 or 1"
            )

    if len(set(split.sample_ids)) != sample_count:
        raise ValueError(
            f"{split_name}: duplicate sample_id"
        )


def validate_tcp_preprocessing_state(
    state: TcpPreprocessingState,
) -> None:
    """验证预处理参数与当前模型输入契约兼容。"""

    if state.version != PREPROCESSING_VERSION:
        raise ValueError("unsupported preprocessing version")

    if state.feature_names != MODEL_FEATURE_NAMES:
        raise ValueError("preprocessing feature names differ")

    if (
        state.log_standardized_feature_names
        != LOG_STANDARDIZED_FEATURE_NAMES
    ):
        raise ValueError(
            "preprocessing transformed feature names differ"
        )

    expected_count = len(LOG_STANDARDIZED_INDICES)
    if (
        len(state.log_means) != expected_count
        or len(state.log_scales) != expected_count
    ):
        raise ValueError(
            "preprocessing parameter lengths differ"
        )

    if any(
        not math.isfinite(value)
        for value in state.log_means
    ):
        raise ValueError(
            "preprocessing means contain non-finite values"
        )

    if any(
        not math.isfinite(value) or value <= 0.0
        for value in state.log_scales
    ):
        raise ValueError(
            "preprocessing scales must be finite and positive"
        )


def fit_tcp_preprocessing(
    training_split: TensorTcpSplit,
) -> TcpPreprocessingState:
    """只使用训练集合拟合log1p后的均值和总体标准差。"""

    _validate_tensor_split(
        "training",
        training_split,
    )

    if training_split.features.shape[0] == 0:
        raise ValueError("training split is empty")

    indices = list(LOG_STANDARDIZED_INDICES)
    selected = training_split.features[
        :,
        indices,
    ].to(dtype=torch.float64)

    if (selected < 0.0).any().item():
        raise ValueError(
            "log-standardized features must be non-negative"
        )

    logged = torch.log1p(selected)
    means = logged.mean(dim=0)

    # unbiased=False计算总体标准差，适合把当前训练集视为拟合总体。
    scales = logged.std(
        dim=0,
        unbiased=False,
    )

    # 常量列中心化后始终为0，用1作为除数可以避免除零。
    scales = torch.where(
        scales > MINIMUM_SCALE,
        scales,
        torch.ones_like(scales),
    )

    return TcpPreprocessingState(
        version=PREPROCESSING_VERSION,
        feature_names=MODEL_FEATURE_NAMES,
        log_standardized_feature_names=(
            LOG_STANDARDIZED_FEATURE_NAMES
        ),
        log_means=tuple(
            value.item()
            for value in means
        ),
        log_scales=tuple(
            value.item()
            for value in scales
        ),
    )


def apply_tcp_preprocessing(
    split_name: str,
    split: TensorTcpSplit,
    state: TcpPreprocessingState,
) -> TensorTcpSplit:
    """使用既有参数转换一个集合，不重新拟合任何统计量。"""

    _validate_tensor_split(
        split_name,
        split,
    )
    validate_tcp_preprocessing_state(state)

    features = split.features.clone()
    labels = split.labels.clone()

    if features.shape[0] > 0:
        indices = list(LOG_STANDARDIZED_INDICES)
        selected = features[
            :,
            indices,
        ].to(dtype=torch.float64)

        if (selected < 0.0).any().item():
            raise ValueError(
                f"{split_name}: "
                "log-standardized features must be non-negative"
            )

        means = torch.tensor(
            state.log_means,
            dtype=torch.float64,
            device="cpu",
        )
        scales = torch.tensor(
            state.log_scales,
            dtype=torch.float64,
            device="cpu",
        )

        normalized = (
            torch.log1p(selected) - means
        ) / scales

        if not torch.isfinite(normalized).all().item():
            raise ValueError(
                f"{split_name}: preprocessing produced "
                "non-finite values"
            )

        features[:, indices] = normalized.to(
            dtype=torch.float32
        )

    return TensorTcpSplit(
        features=features,
        labels=labels,
        sample_ids=split.sample_ids,
    )


def preprocess_tcp_dataset(
    dataset: TensorTcpDataset,
    state: TcpPreprocessingState,
) -> TensorTcpDataset:
    """对三个集合应用同一份训练参数。"""

    return TensorTcpDataset(
        train=apply_tcp_preprocessing(
            "train",
            dataset.train,
            state,
        ),
        validation=apply_tcp_preprocessing(
            "validation",
            dataset.validation,
            state,
        ),
        test=apply_tcp_preprocessing(
            "test",
            dataset.test,
            state,
        ),
    )