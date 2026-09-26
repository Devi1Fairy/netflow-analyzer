#!/usr/bin/env python3

"""审计C程序导出的流CSV与IoT-23标签索引的匹配质量。"""

import csv
from typing import Dict, List, TextIO

from audit_ctu13_flow_matches import (
    EXPECTED_FLOW_COLUMNS,
    parse_flow_key,
    parse_flow_timestamp,
    validate_row_shape,
)
from flow_sample_metadata import MATCH_STATUS_UNIQUE
from iot23_label_index import (
    FlowKey,
    LabelInterval,
    classify_flow_interval,
)


def audit_flow_csv(
    index: Dict[FlowKey, List[LabelInterval]],
    input_stream: TextIO,
) -> Dict[str, int]:
    """
    逐行审计C流CSV，返回四类匹配数量。

    index和input_stream都由调用者拥有；本函数只借用，
    不关闭文件，也不修改索引。第一条数据行对应流行号1，
    CSV表头不计入flows_total。
    """

    counts = {
        "flows_total": 0,
        "matches_unique": 0,
        "matches_unique_malicious": 0,
        "matches_unique_benign": 0,
        "matches_unmatched": 0,
        "matches_ambiguous_same_label": 0,
        "matches_ambiguous_conflicting_labels": 0,
    }

    reader = csv.DictReader(input_stream)

    if tuple(reader.fieldnames or ()) != EXPECTED_FLOW_COLUMNS:
        raise ValueError("unexpected flow CSV columns")

    for row in reader:
        validate_row_shape(
            row,
            EXPECTED_FLOW_COLUMNS,
            "flow CSV",
            reader.line_num,
        )

        key = parse_flow_key(row, reader.line_num)
        first_seen = parse_flow_timestamp(
            row, "first_seen", reader.line_num
        )
        last_seen = parse_flow_timestamp(
            row, "last_seen", reader.line_num
        )

        result = classify_flow_interval(
            index,
            key,
            first_seen,
            last_seen,
        )

        counts["flows_total"] += 1
        counts[f"matches_{result.status}"] += 1

        if result.status == MATCH_STATUS_UNIQUE:
            counts[
                f"matches_unique_{result.label_group}"
            ] += 1

    if counts["flows_total"] == 0:
        raise ValueError("flow CSV contains no records")

    return counts