"""训练TCP线性二分类基线，并计算固定阈值下的分类指标。"""

import math
from dataclasses import dataclass
from typing import Optional, Tuple

import torch
from torch import nn

from tcp_model_input import MODEL_FEATURE_NAMES


MODEL_VERSION = "tcp_linear_v1"


class TcpLinearClassifier(nn.Module):
    """18个权重和1个偏置；输入[N,18]，返回[N]原始logit。"""

    def __init__(self):
        super().__init__()

        # nn.Parameter使Tensor自动注册为模型的可训练参数。
        # 线性单输出模型可以从全0开始，不需要随机初始化。
        self.weight = nn.Parameter(torch.zeros(
            len(MODEL_FEATURE_NAMES),
            dtype=torch.float32,
            device="cpu",
        ))

        # 空形状()表示一个标量，而不是长度为1的向量。
        self.bias = nn.Parameter(torch.zeros(
            (),
            dtype=torch.float32,
            device="cpu",
        ))

    def forward(self, features: torch.Tensor) -> torch.Tensor:
        # [N,18] @ [18]得到[N]，同一个偏置加到每条样本上。
        return features @ self.weight + self.bias


@dataclass(frozen=True)
class LinearTrainingResult:
    """拥有已训练模型，以及初始和每次更新后的损失记录。"""

    model: TcpLinearClassifier
    loss_history: Tuple[float, ...]
    positive_weight: float


@dataclass(frozen=True)
class BinaryMetrics:
    """以恶意为正类；分母为0的指标返回None。"""

    true_negative: int
    false_positive: int
    false_negative: int
    true_positive: int

    accuracy: float
    malicious_precision: Optional[float]
    malicious_recall: Optional[float]
    benign_false_positive_rate: Optional[float]
    balanced_accuracy: Optional[float]


def _validate_labels(labels: torch.Tensor) -> None:
    """要求标签为非空CPU float32向量，且每项为0或1。"""

    if labels.ndim != 1 or labels.numel() == 0:
        raise ValueError(
            "labels must be a non-empty vector"
        )

    if (
        labels.dtype != torch.float32
        or labels.device.type != "cpu"
    ):
        raise ValueError(
            "labels must be CPU float32"
        )

    # NaN和无穷值也无法通过这项0/1检查。
    if not (
        (labels == 0.0) | (labels == 1.0)
    ).all().item():
        raise ValueError("labels must be 0 or 1")


def train_tcp_linear_baseline(
    features: torch.Tensor,
    labels: torch.Tensor,
    *,
    epochs: int = 100,
    learning_rate: float = 0.05,
) -> LinearTrainingResult:
    """借用已预处理的训练张量，全批量训练并返回新模型。

    只从这份训练标签计算类别权重。输入内容不修改，也不将梯度
    回传到输入。每轮使用全部训练样本，不打乱、不读取验证集。
    """

    _validate_labels(labels)

    if (
        features.ndim != 2
        or features.shape
        != (labels.numel(), len(MODEL_FEATURE_NAMES))
    ):
        raise ValueError(
            "unexpected training feature shape"
        )

    if (
        features.dtype != torch.float32
        or features.device.type != "cpu"
    ):
        raise ValueError(
            "features must be CPU float32"
        )

    if not torch.isfinite(features).all().item():
        raise ValueError(
            "features contain non-finite values"
        )

    if type(epochs) is not int or epochs <= 0:
        raise ValueError(
            "epochs must be a positive integer"
        )

    if (
        not math.isfinite(learning_rate)
        or learning_rate <= 0.0
    ):
        raise ValueError(
            "learning rate must be finite and positive"
        )

    # detach借用原存储，但切断输入已有的计算图。
    # 后续只读取它们，优化器只更新新模型的参数。
    features = features.detach()
    labels = labels.detach()

    positive_count = int(
        (labels == 1.0).sum().item()
    )
    negative_count = labels.numel() - positive_count

    if positive_count == 0 or negative_count == 0:
        raise ValueError(
            "training needs both benign and malicious samples"
        )

    positive_weight = (
        negative_count / positive_count
    )

    criterion = nn.BCEWithLogitsLoss(
        pos_weight=torch.tensor(
            positive_weight,
            dtype=torch.float32,
            device="cpu",
        )
    )

    model = TcpLinearClassifier()
    model.train()

    # Adam维护参数的梯度统计，并据此调整每次参数更新。
    # foreach=False固定使用普通更新路径。
    optimizer = torch.optim.Adam(
        model.parameters(),
        lr=learning_rate,
        foreach=False,
    )

    history = []

    for _ in range(epochs):
        # 清除上一轮参数梯度，防止跨轮次意外累积。
        optimizer.zero_grad(set_to_none=True)

        # 前向计算：输入特征→模型评分→损失。
        logits = model(features)
        loss = criterion(logits, labels)

        if not torch.isfinite(loss).item():
            raise ValueError(
                "training produced a non-finite loss"
            )

        history.append(loss.item())

        # backward计算损失对权重和偏置的梯度。
        loss.backward()

        # step根据梯度更新模型参数。
        optimizer.step()

    model.eval()

    # 最后一次更新后再计算损失，确保包含最终模型的结果。
    # 此处只做评估，不需要构建反向传播计算图。
    with torch.no_grad():
        final_loss = criterion(
            model(features),
            labels,
        )

        if not torch.isfinite(final_loss).item():
            raise ValueError(
                "training produced a non-finite final loss"
            )

        history.append(final_loss.item())

    return LinearTrainingResult(
        model=model,
        loss_history=tuple(history),
        positive_weight=positive_weight,
    )


def evaluate_binary_logits(
    labels: torch.Tensor,
    logits: torch.Tensor,
) -> BinaryMetrics:
    """借用标签和评分，以logit>=0作为预测恶意的固定阈值。"""

    _validate_labels(labels)

    if logits.shape != labels.shape:
        raise ValueError(
            "logits and labels shapes differ"
        )

    if (
        logits.dtype != torch.float32
        or logits.device.type != "cpu"
    ):
        raise ValueError(
            "logits must be CPU float32"
        )

    if not torch.isfinite(logits).all().item():
        raise ValueError(
            "logits contain non-finite values"
        )

    actual_positive = labels == 1.0
    predicted_positive = logits >= 0.0

    # 布尔Tensor中的True相当于1，sum统计对应样本数量。
    tp = int((
        actual_positive & predicted_positive
    ).sum().item())

    tn = int((
        ~actual_positive & ~predicted_positive
    ).sum().item())

    fp = int((
        ~actual_positive & predicted_positive
    ).sum().item())

    fn = int((
        actual_positive & ~predicted_positive
    ).sum().item())

    def ratio(numerator, denominator):
        """没有可统计的分母时返回None，避免伪造指标。"""

        return (
            numerator / denominator
            if denominator
            else None
        )

    precision = ratio(tp, tp + fp)
    recall = ratio(tp, tp + fn)
    false_positive_rate = ratio(fp, fp + tn)

    balanced_accuracy = None
    if (
        recall is not None
        and false_positive_rate is not None
    ):
        balanced_accuracy = (
            recall + 1.0 - false_positive_rate
        ) / 2.0

    return BinaryMetrics(
        true_negative=tn,
        false_positive=fp,
        false_negative=fn,
        true_positive=tp,
        accuracy=(tp + tn) / labels.numel(),
        malicious_precision=precision,
        malicious_recall=recall,
        benign_false_positive_rate=false_positive_rate,
        balanced_accuracy=balanced_accuracy,
    )