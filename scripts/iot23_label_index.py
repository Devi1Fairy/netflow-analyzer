#!/usr/bin/env python3

"""按规范化双向五元组组织IoT-23标签候选。"""

from collections import defaultdict
from dataclasses import dataclass
from typing import DefaultDict, Dict, Iterable, List, Optional, Tuple

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


def build_label_index(
    records: Iterable[LabelInput],
) -> Dict[FlowKey, List[LabelInterval]]:
    """
    从已规范化的记录建立候选索引。

    None表示当前C分析器不支持的协议；exclude不能用作
    监督标签。两者都不进入索引。

    返回的新字典及其中的列表由调用者持有。
    """

    index: DefaultDict[
        FlowKey,
        List[LabelInterval],
    ] = defaultdict(list)

    for identity, label_group in records:
        if label_group not in LABEL_GROUPS:
            raise ValueError(
                f"unsupported label group: {label_group!r}"
            )

        if label_group == "exclude" or identity is None:
            continue

        key = flow_key_from_identity(identity)

        # append保留同键的每一条标签和输入顺序；
        # 不能用赋值覆盖旧区间。
        index[key].append(
            LabelInterval(
                start_microseconds=(
                    identity.start_time_microseconds
                ),
                end_microseconds=(
                    identity.end_time_microseconds
                ),
                label_group=label_group,
            )
        )

    return dict(index)