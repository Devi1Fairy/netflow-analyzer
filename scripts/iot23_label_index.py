#!/usr/bin/env python3

"""按规范化双向五元组组织IoT-23标签候选。"""

from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple
from pathlib import Path

from inspect_iot23_labels import inspect_label_file

from iot23_flow_identity import (
    Iot23FlowIdentity,
    MAX_TIMESTAMP_MICROSECONDS,
)
from iot23_label import LABEL_GROUPS

from flow_sample_metadata import (
    MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS,
    MATCH_STATUS_AMBIGUOUS_SAME_LABEL,
    MATCH_STATUS_UNIQUE,
    MATCH_STATUS_UNMATCHED,
    SUPPORTED_LABEL_GROUPS,
)

# 协议号、A端IP/端口、B端IP/端口；时间不属于流键。
FlowKey = Tuple[int, int, int, int, int]
LabelInput = Tuple[Optional[Iot23FlowIdentity], str]


@dataclass(frozen=True)
class LabelInterval:
    """同一五元组下的一条独立标签时间区间。"""

    start_microseconds: int
    end_microseconds: int
    label_group: str

@dataclass(frozen=True)
class FlowMatchClassification:
    """一条C流与IoT-23标签候选的匹配结果。"""

    status: str
    candidate_count: int
    label_group: Optional[str]

def flow_key_from_identity(
    identity: Iot23FlowIdentity,
) -> FlowKey:
    """提取与C端双向流键字段顺序一致的五元组。"""

    return (
        identity.protocol,
        identity.endpoint_a.ipv4_address,
        identity.endpoint_a.port,
        identity.endpoint_b.ipv4_address,
        identity.endpoint_b.port,
    )


def _append_label(
    index: Dict[FlowKey, List[LabelInterval]],
    identity: Optional[Iot23FlowIdentity],
    label_group: str,
) -> None:
    """把一条可监督标签加入调用者拥有的索引。"""

    if label_group not in LABEL_GROUPS:
        raise ValueError(
            f"unsupported label group: {label_group!r}"
        )

    if label_group == "exclude" or identity is None:
        return

    key = flow_key_from_identity(identity)

    # 同一流键可能对应多个独立时间区间，不能覆盖旧候选。
    index.setdefault(key, []).append(
        LabelInterval(
            start_microseconds=identity.start_time_microseconds,
            end_microseconds=identity.end_time_microseconds,
            label_group=label_group,
        )
    )


def build_label_index(
    records: Iterable[LabelInput],
) -> Dict[FlowKey, List[LabelInterval]]:
    """从已规范化的记录建立候选索引。"""

    index: Dict[FlowKey, List[LabelInterval]] = {}

    for identity, label_group in records:
        _append_label(index, identity, label_group)

    return index


def load_label_index(
    label_file: Path,
) -> Dict[FlowKey, List[LabelInterval]]:
    """边验证标签文件，边建立候选索引。"""

    index: Dict[FlowKey, List[LabelInterval]] = {}

    def collect(
        identity: Optional[Iot23FlowIdentity],
        label_group: str,
    ) -> None:
        _append_label(index, identity, label_group)

    inspect_label_file(
        label_file,
        on_record=collect,
    )

    return index

def classify_flow_interval(
    index: Dict[FlowKey, List[LabelInterval]],
    key: FlowKey,
    first_seen_microseconds: int,
    last_seen_microseconds: int,
) -> FlowMatchClassification:
    """
    根据双向流键和闭时间区间分类标签候选。

    index由调用者拥有；本函数只读取，不修改它。
    key不含时间，同键的不同生命周期靠时间区间区分。
    返回的新对象由调用者持有；无文件或内存释放责任。
    """

    if (
        isinstance(first_seen_microseconds, bool)
        or not isinstance(first_seen_microseconds, int)
        or isinstance(last_seen_microseconds, bool)
        or not isinstance(last_seen_microseconds, int)
    ):
        raise TypeError(
            "flow interval timestamps must be integers"
        )

    if (
        first_seen_microseconds < 0
        or first_seen_microseconds > last_seen_microseconds
        or last_seen_microseconds > MAX_TIMESTAMP_MICROSECONDS
    ):
        raise ValueError("invalid flow interval")

    # 闭区间相交：两端恰好相等也算候选；
    # 这让零时长Zeek标签仍可与同一时刻的C流匹配。
    candidates = [
        candidate
        for candidate in index.get(key, ())
        if (
            first_seen_microseconds <= candidate.end_microseconds
            and candidate.start_microseconds <= last_seen_microseconds
        )
    ]

    candidate_count = len(candidates)

    if candidate_count == 0:
        return FlowMatchClassification(
            status=MATCH_STATUS_UNMATCHED,
            candidate_count=0,
            label_group=None,
        )

    groups = {
        candidate.label_group
        for candidate in candidates
    }

    if not groups.issubset(SUPPORTED_LABEL_GROUPS):
        raise ValueError(
            "match candidates contain unsupported labels"
        )

    if candidate_count == 1:
        return FlowMatchClassification(
            status=MATCH_STATUS_UNIQUE,
            candidate_count=1,
            label_group=candidates[0].label_group,
        )

    if len(groups) == 1:
        return FlowMatchClassification(
            status=MATCH_STATUS_AMBIGUOUS_SAME_LABEL,
            candidate_count=candidate_count,
            label_group=next(iter(groups)),
        )

    return FlowMatchClassification(
        status=MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS,
        candidate_count=candidate_count,
        label_group=None,
    )
