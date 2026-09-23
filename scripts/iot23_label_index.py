#!/usr/bin/env python3

"""按规范化双向五元组组织IoT-23标签候选。"""

from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple
from pathlib import Path

from inspect_iot23_labels import inspect_label_file

from iot23_flow_identity import Iot23FlowIdentity
from iot23_label import LABEL_GROUPS


# 协议号、A端IP/端口、B端IP/端口；时间不属于流键。
FlowKey = Tuple[int, int, int, int, int]
LabelInput = Tuple[Optional[Iot23FlowIdentity], str]


@dataclass(frozen=True)
class LabelInterval:
    """同一五元组下的一条独立标签时间区间。"""

    start_microseconds: int
    end_microseconds: int
    label_group: str


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