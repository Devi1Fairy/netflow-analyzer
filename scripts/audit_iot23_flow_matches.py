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
from flow_sample_metadata import (
    CAPTURE_ID_PATTERN,
    FlowSampleIdentity,
    MATCH_STATUS_UNIQUE,
    SUPPORTED_MATCH_STATUSES,
    build_sample_metadata,
)
from iot23_label_index import (
    FlowKey,
    LabelInterval,
    classify_flow_interval,
    load_label_index
)

from iot23_flow_review import (
    REVIEW_NOT_UNIQUE,
    REVIEW_UNIQUE_REUSED,
    REVIEW_UNIQUE_UNREUSED_CANDIDATE,
    write_iot23_review_csv_header,
    write_iot23_review_csv_record,
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

def classify_iot23_row_review(
    row_number: int,
    match_status: str,
    unreused_rows: FrozenSet[int],
) -> str:
    """
    根据同一次审计的未复用集合，给一条流确定审查状态。

    row_number从1开始，不计CSV表头。unreused_rows必须来自
    这份流CSV的audit_flow_csv_with_unreused_rows结果。
    返回值不是最终训练准入结论。
    """

    if (
        isinstance(row_number, bool)
        or not isinstance(row_number, int)
        or row_number < 1
    ):
        raise ValueError("invalid flow data row number")

    if (
        not isinstance(match_status, str)
        or match_status not in SUPPORTED_MATCH_STATUSES
    ):
        raise ValueError("invalid flow match status")

    if row_number in unreused_rows:
        if match_status != MATCH_STATUS_UNIQUE:
            raise ValueError(
                "non-unique flow appears in unreused rows"
            )
        return REVIEW_UNIQUE_UNREUSED_CANDIDATE

    if match_status == MATCH_STATUS_UNIQUE:
        return REVIEW_UNIQUE_REUSED

    return REVIEW_NOT_UNIQUE

def write_iot23_review_csv(
    index: Dict[FlowKey, List[LabelInterval]],
    input_stream: TextIO,
    output_stream: TextIO,
    capture_id: str,
) -> Dict[str, int]:
    """
    根据完整流CSV审计结果，写出逐行IoT-23审查CSV。

    input_stream必须可定位，并且当前位置应在CSV表头之前。
    两遍读取期间，输入内容和标签索引都不能变化。

    index、input_stream和output_stream由调用者拥有；
    本函数不关闭或刷新它们。返回完整审计摘要。

    底层写入失败可能留下部分输出；文件级清理由后续CLI负责。
    """

    if input_stream is output_stream:
        raise ValueError(
            "input and output streams must differ"
        )

    if (
        not isinstance(capture_id, str)
        or CAPTURE_ID_PATTERN.fullmatch(capture_id) is None
    ):
        raise ValueError("invalid capture_id")

    if not input_stream.seekable():
        raise ValueError(
            "flow CSV input stream must be seekable"
        )

    start_position = input_stream.tell()

    # 第一遍：必须读完整份文件，才能知道哪些Zeek标签被复用。
    counts, unreused_rows = (
        audit_flow_csv_with_unreused_rows(
            index,
            input_stream,
        )
    )

    # 第二遍：回到原来的表头位置，逐行生成审查记录。
    input_stream.seek(start_position)
    reader = csv.DictReader(input_stream)

    if tuple(reader.fieldnames or ()) != EXPECTED_FLOW_COLUMNS:
        raise ValueError("unexpected flow CSV columns")

    write_iot23_review_csv_header(output_stream)

    row_number = 0

    for row in reader:
        validate_row_shape(
            row,
            EXPECTED_FLOW_COLUMNS,
            "flow CSV",
            reader.line_num,
        )

        key = parse_flow_key(row, reader.line_num)
        first_seen = parse_flow_timestamp(
            row,
            "first_seen",
            reader.line_num,
        )
        last_seen = parse_flow_timestamp(
            row,
            "last_seen",
            reader.line_num,
        )

        classification = classify_flow_interval(
            index,
            key,
            first_seen,
            last_seen,
        )

        row_number += 1

        (
            protocol,
            endpoint_a_ipv4,
            endpoint_a_port,
            endpoint_b_ipv4,
            endpoint_b_port,
        ) = key

        identity = FlowSampleIdentity(
            capture_id=capture_id,
            protocol=protocol,
            endpoint_a_ipv4=endpoint_a_ipv4,
            endpoint_a_port=endpoint_a_port,
            endpoint_b_ipv4=endpoint_b_ipv4,
            endpoint_b_port=endpoint_b_port,
            first_seen_microseconds=first_seen,
            last_seen_microseconds=last_seen,
        )

        metadata = build_sample_metadata(
            identity=identity,
            feature_row_number=row_number,
            match_status=classification.status,
            candidate_count=classification.candidate_count,
            label_group=classification.label_group,
        )

        review_status = classify_iot23_row_review(
            row_number,
            classification.status,
            unreused_rows,
        )

        write_iot23_review_csv_record(
            output_stream,
            metadata,
            review_status,
        )

    if row_number != counts["flows_total"]:
        raise ValueError(
            "flow CSV changed between audit and review passes"
        )

    return counts

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