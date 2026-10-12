"""把TCP线性模型及其预处理参数打包为版本化JSON。"""

import json
import math
from typing import TextIO, Tuple

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

def read_tcp_model_artifact(
    input_stream: TextIO,
) -> Tuple[TcpLinearClassifier, TcpPreprocessingState]:
    """读取借用的JSON文本流，返回新模型和预处理参数。

    不关闭输入流，不训练模型，不重新拟合预处理参数。
    文件内容不符合当前v1契约时抛出ValueError。
    只有全部检查通过后才返回结果。
    """

    def unique_object(pairs):
        """把JSON键值对组成字典；重复字段直接报错。"""

        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(
                    f"duplicate JSON key: {key}"
                )
            result[key] = value
        return result

    # json.load读取文本流并还原普通Python值。
    # object_pairs_hook让我们检查每个JSON对象的重复键。
    artifact = json.load(
        input_stream,
        object_pairs_hook=unique_object,
    )

    if type(artifact) is not dict:
        raise ValueError(
            "model artifact must be a JSON object"
        )

    try:
        preprocessing = artifact["preprocessing"]
        parameters = artifact["model"]

        # 还原训练时保存的统计量，不在推理样本上重新fit。
        state = TcpPreprocessingState(
            version=preprocessing["version"],
            feature_names=tuple(
                artifact["feature_names"]
            ),
            log_standardized_feature_names=tuple(
                preprocessing[
                    "log_standardized_feature_names"
                ]
            ),
            log_means=tuple(_finite_list(
                preprocessing["log_means"],
                "log_means",
            )),
            log_scales=tuple(_finite_list(
                preprocessing["log_scales"],
                "log_scales",
            )),
        )

        weights = _finite_list(
            parameters["weights"],
            "weights",
        )
        bias = _finite_list(
            [parameters["bias"]],
            "bias",
        )[0]

        label_values = artifact["label_values"]
        threshold = artifact["logit_threshold"]

    except (KeyError, TypeError, OverflowError) as error:
        raise ValueError(
            "invalid model artifact structure"
        ) from error

    if len(weights) != len(MODEL_FEATURE_NAMES):
        raise ValueError(
            "unexpected model weight count"
        )

    # Python中True == 1，所以需要额外排除布尔标签。
    if (
        type(label_values) is not dict
        or any(
            type(value) is not int
            for value in label_values.values()
        )
    ):
        raise ValueError(
            "label values must be integers"
        )

    if type(threshold) not in (int, float):
        raise ValueError(
            "logit threshold must be numeric"
        )

    model = TcpLinearClassifier()

    # copy_把保存的数值复制进新模型，不共享原模型存储。
    # 加载参数不需要记录梯度，但模型仍保留原有Parameter。
    with torch.no_grad():
        model.weight.copy_(torch.tensor(
            weights,
            dtype=torch.float32,
            device="cpu",
        ))
        model.bias.copy_(torch.tensor(
            bias,
            dtype=torch.float32,
            device="cpu",
        ))

    # 复用已有导出校验，再比较完整契约。
    # 因而错误版本、字段顺序、阈值、缺失或额外字段均被拒绝。
    canonical = build_tcp_model_artifact(
        model,
        state,
    )
    if canonical != artifact:
        raise ValueError(
            "model artifact differs from the v1 contract"
        )

    model.eval()
    return model, state