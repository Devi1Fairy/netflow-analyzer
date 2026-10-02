"""把TCP线性模型及其预处理参数打包为版本化JSON。"""

import json
import math
from typing import TextIO

import torch

from flow_sample_metadata import SUPPORTED_FEATURE_SCHEMA_VERSION
from tcp_feature_preprocessing import (
    TcpPreprocessingState,
    validate_tcp_preprocessing_state,
)
from tcp_linear_baseline import MODEL_VERSION, TcpLinearClassifier
from tcp_model_input import (
    ENCODING_VERSION,
    LABEL_VALUES,
    MODEL_FEATURE_NAMES,
)


ARTIFACT_SCHEMA_VERSION = "tcp_linear_artifact_v1"


def _finite_list(values, name):
    """复制Python数值序列为新列表；拒绝布尔、NaN和无穷。"""

    if type(values) not in (tuple, list):
        raise ValueError(f"{name}: expected a numeric sequence")

    if any(
        type(value) not in (int, float)
        or not math.isfinite(value)
        for value in values
    ):
        raise ValueError(f"{name}: expected finite numbers")

    return [float(value) for value in values]


def build_tcp_model_artifact(
    model: TcpLinearClassifier,
    state: TcpPreprocessingState,
) -> dict:
    """借用模型和预处理状态，返回独立的推理参数快照。

    返回值只包含JSON支持的普通值，不包含Tensor或模型对象。
    调用期间不得并发更新模型参数。本函数不训练、不写文件。
    """

    # v1只支持当前类，不能把不同forward的子类标成同一模型。
    if type(model) is not TcpLinearClassifier:
        raise ValueError("unsupported TCP model type")
    if not isinstance(state, TcpPreprocessingState):
        raise ValueError("unexpected preprocessing state type")

    means = _finite_list(state.log_means, "log_means")
    scales = _finite_list(state.log_scales, "log_scales")
    validate_tcp_preprocessing_state(state)

    if (
        tuple(model.weight.shape) != (len(MODEL_FEATURE_NAMES),)
        or tuple(model.bias.shape) != ()
    ):
        raise ValueError("unexpected model parameter shape")

    for parameter in (model.weight, model.bias):
        if (
            parameter.dtype != torch.float32
            or parameter.device.type != "cpu"
        ):
            raise ValueError("model parameters must be CPU float32")
        if not torch.isfinite(parameter).all().item():
            raise ValueError("model parameters must be finite")

    return {
        "artifact_schema_version": ARTIFACT_SCHEMA_VERSION,
        "feature_schema_version": SUPPORTED_FEATURE_SCHEMA_VERSION,
        "encoding_version": ENCODING_VERSION,
        "model_version": MODEL_VERSION,
        "feature_names": list(MODEL_FEATURE_NAMES),
        "label_values": dict(LABEL_VALUES),
        "parameter_dtype": "float32",
        # 本契约对应当前固定阈值，不在保存时重新选择阈值。
        "logit_threshold": 0.0,
        "model": {
            # detach切断计算图；tolist/item复制为Python普通值。
            "weights": model.weight.detach().tolist(),
            "bias": model.bias.detach().item(),
        },
        "preprocessing": {
            "version": state.version,
            "statistics_dtype": "float64",
            "log_standardized_feature_names": list(
                state.log_standardized_feature_names
            ),
            "log_means": means,
            "log_scales": scales,
        },
    }


def write_tcp_model_artifact(
    output_stream: TextIO,
    model: TcpLinearClassifier,
    state: TcpPreprocessingState,
) -> None:
    """向借用文本流写JSON；不关闭、不刷新流，不管理文件路径。

    校验及序列化失败时不写任何内容。底层I/O失败仍可能部分写入，
    因此文件级独占创建和失败清理由后续发布层负责。
    """

    artifact = build_tcp_model_artifact(model, state)
    text = json.dumps(
        artifact,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        indent=2,
    ) + "\n"

    output_stream.write(text)