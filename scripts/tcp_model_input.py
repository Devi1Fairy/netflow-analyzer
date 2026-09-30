"""将一条TCP候选记录编码为固定顺序的模型输入。"""

import math
from typing import Mapping, Tuple

from flow_csv_identity import parse_unsigned_integer
from flow_sample_metadata import SUPPORTED_FEATURE_SCHEMA_VERSION

ENCODING_VERSION = "tcp_input_v1"

NUMERIC_COLUMNS = (
    "duration_microseconds",
    "total_packet_count",
    "total_captured_byte_count",
    "total_wire_byte_count",
    "mean_captured_bytes_per_packet",
    "mean_wire_bytes_per_packet",
    "packet_count_imbalance_ratio",
    "wire_byte_count_imbalance_ratio",
    "tcp_handshake_completed",
)

TCP_PHASES = (
    "unobserved",
    "syn-seen",
    "syn-ack-seen",
    "established",
    "midstream",
    "fin-seen",
    "fin-bidirectional",
    "closed",
    "reset",
)

MODEL_FEATURE_NAMES = NUMERIC_COLUMNS + tuple(
    "tcp_phase_" + phase for phase in TCP_PHASES
)
LABEL_VALUES = {"benign": 0, "malicious": 1}


def encode_tcp_candidate(
    row: Mapping[str, str],
) -> Tuple[Tuple[float, ...], int]:
    """借用CSV行，返回18维原始数值和标签；不修改输入、不做标准化。

    仅检查本函数消费的字段。来源隔离、行号和文件完整性由调用层检查。
    非法或缺失字段抛出ValueError，不返回部分结果。
    """

    def text(name: str) -> str:
        value = row.get(name)
        if not isinstance(value, str) or not value:
            raise ValueError(f"missing or invalid field: {name}")
        return value

    if text("feature_schema_version") != SUPPORTED_FEATURE_SCHEMA_VERSION:
        raise ValueError("unsupported feature schema")
    if text("protocol") != "6" or text("tcp_state_applicable") != "1":
        raise ValueError("expected a TCP candidate")

    phase = text("tcp_phase")
    label = text("candidate_label_group")
    if phase not in TCP_PHASES:
        raise ValueError(f"unknown TCP phase: {phase}")
    if label not in LABEL_VALUES:
        raise ValueError(f"unknown label: {label}")

    values = []
    for position, name in enumerate(NUMERIC_COLUMNS):
        raw_value = text(name)
        if position < 4:
            # 前四列是uint64统计量，先按整数验证再转为模型用浮点数。
            value = float(parse_unsigned_integer(
                raw_value, name, (1 << 64) - 1
            ))
        elif name == "tcp_handshake_completed":
            if raw_value not in ("0", "1"):
                raise ValueError("handshake flag must be 0 or 1")
            value = float(raw_value)
        else:
            value = float(raw_value)

        # isfinite拒绝NaN和无穷大，避免它们污染后续训练计算。
        if not math.isfinite(value) or value < 0:
            raise ValueError(f"invalid numeric value: {name}")
        if name.endswith("_imbalance_ratio") and value > 1:
            raise ValueError(f"ratio exceeds 1: {name}")
        values.append(value)

    # 固定词表决定固定列位置，不从验证数据学习类别。
    one_hot = tuple(float(phase == known) for known in TCP_PHASES)
    return tuple(values) + one_hot, LABEL_VALUES[label]