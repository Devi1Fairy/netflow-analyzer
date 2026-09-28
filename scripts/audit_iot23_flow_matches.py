#!/usr/bin/env python3

"""审计C程序导出的流CSV与IoT-23标签索引的匹配质量。"""

import argparse
import csv
import sys
from pathlib import Path
from typing import Dict, FrozenSet, List, TextIO, Tuple

from flow_csv_identity  import (
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
    load_label_index
)


def audit_flow_csv_with_unreused_rows(
    index: Dict[FlowKey, List[LabelInterval]],
    input_stream: TextIO,
) -> Tuple[Dict[str, int], FrozenSet[int]]:
    """
    逐行审计C流CSV，返回统计摘要和未复用唯一流的数据行号集合。

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
        "unique_label_records": 0,
        "reused_label_records": 0,
        "duplicate_unique_assignments": 0,
        "matches_unique_unreused": 0,
        "matches_unique_reused": 0,
    }

    unique_label_rows: Dict[Tuple[FlowKey, int], List[int]] = {}

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

            if result.unique_candidate_index is None:
                raise ValueError("unique match lacks candidate index")

            label_identity = (key, result.unique_candidate_index)
            unique_label_rows.setdefault(
                label_identity, []
            ).append(counts["flows_total"])

    if counts["flows_total"] == 0:
        raise ValueError("flow CSV contains no records")

    counts["unique_label_records"] = len(unique_label_rows)
    counts["reused_label_records"] = sum(
        len(rows) > 1 for rows in unique_label_rows.values()
    )
    counts["duplicate_unique_assignments"] = sum(
        len(rows) - 1 for rows in unique_label_rows.values()
    )
    counts["matches_unique_unreused"] = sum(
        1 for rows in unique_label_rows.values()
        if len(rows) == 1
    )
    counts["matches_unique_reused"] = sum(
        len(rows) for rows in unique_label_rows.values()
        if len(rows) > 1
    )

    unreused_rows = frozenset(
        rows[0] for rows in unique_label_rows.values()
        if len(rows) == 1
    )

    return counts, unreused_rows

def audit_flow_csv(
    index: Dict[FlowKey, List[LabelInterval]],
    input_stream: TextIO,
) -> Dict[str, int]:
    """保留现有只返回摘要的调用接口。"""

    counts, _ = audit_flow_csv_with_unreused_rows(
        index,
        input_stream,
    )
    return counts

def main() -> int:
    """加载标签、打开C流CSV，并打印稳定的审计摘要。"""

    parser = argparse.ArgumentParser(
        description=(
            "Audit IoT-23 labels against "
            "Netflow Analyzer flow CSV."
        )
    )
    parser.add_argument(
        "label_file",
        type=Path,
        help="IoT-23 Zeek labeled connection log.",
    )
    parser.add_argument(
        "flow_csv",
        type=Path,
        help="Flow CSV produced by netflow-analyzer --csv.",
    )
    arguments = parser.parse_args()

    try:
        index = load_label_index(arguments.label_file)

        with arguments.flow_csv.open(
            "r",
            encoding="utf-8",
            newline="",
        ) as input_stream:
            counts = audit_flow_csv(index, input_stream)

    except (
        OSError,
        UnicodeError,
        csv.Error,
        ValueError,
    ) as error:
        print(
            f"IoT-23 flow match audit failed: {error}",
            file=sys.stderr,
        )
        return 1

    # 只在完整读取成功后输出，避免失败时留下半份摘要。
    for field in (
        "flows_total",
        "matches_unique",
        "matches_unique_malicious",
        "matches_unique_benign",
        "matches_unmatched",
        "matches_ambiguous_same_label",
        "matches_ambiguous_conflicting_labels",
        "unique_label_records",
        "reused_label_records",
        "duplicate_unique_assignments",
        "matches_unique_unreused",
        "matches_unique_reused",
    ):
        print(f"{field}={counts[field]}")

    return 0


if __name__ == "__main__":
    sys.exit(main())