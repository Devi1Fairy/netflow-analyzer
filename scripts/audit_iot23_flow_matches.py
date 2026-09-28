#!/usr/bin/env python3

"""审计C程序导出的流CSV与IoT-23标签索引的匹配质量。"""

import argparse
import csv
import sys
from pathlib import Path
from typing import Dict, FrozenSet, List, TextIO, Tuple
from flow_feature_alignment import validate_flow_feature_alignment

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
    load_label_index,
    classify_interval_boundary
)

BOUNDARY_RELATIONS = (
    "exact",
    "flow_inside_label",
    "label_inside_flow",
    "partial_overlap",
    "disjoint",
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

def audit_candidate_boundaries_with_exact_rows(
    index: Dict[FlowKey, List[LabelInterval]],
    input_stream: TextIO,
) -> Tuple[Dict[str, int], Dict[str, int], FrozenSet[int]]:
    """
    只审计“逐流唯一且标签未被其他C流复用”的时间边界。

    第一遍确定完整文件中的未复用行号；第二遍按相同行号
    找回唯一Zeek候选。借用输入流和索引，不关闭或修改它们。
    返回审计摘要、五类边界计数，以及从 1 开始的 exact 数据行号集合，不决定训练资格。
    """

    if not input_stream.seekable():
        raise ValueError("flow CSV input stream must be seekable")

    start_position = input_stream.tell()
    counts, candidate_rows = audit_flow_csv_with_unreused_rows(
        index,
        input_stream,
    )

    input_stream.seek(start_position)
    reader = csv.DictReader(input_stream)

    if tuple(reader.fieldnames or ()) != EXPECTED_FLOW_COLUMNS:
        raise ValueError("unexpected flow CSV columns")

    boundary_counts = {
        relation: 0 for relation in BOUNDARY_RELATIONS
    }

    # 行号与流CSV、特征CSV的数据行号一致；表头不计入。
    # 仅记录未复用唯一候选中边界完全一致的行。
    exact_rows = set()

    row_number = 0

    for row in reader:
        row_number += 1
        validate_row_shape(
            row,
            EXPECTED_FLOW_COLUMNS,
            "flow CSV",
            reader.line_num,
        )

        if row_number not in candidate_rows:
            continue

        key = parse_flow_key(row, reader.line_num)
        first_seen = parse_flow_timestamp(
            row, "first_seen", reader.line_num
        )
        last_seen = parse_flow_timestamp(
            row, "last_seen", reader.line_num
        )

        match = classify_flow_interval(
            index, key, first_seen, last_seen
        )
        if (
            match.status != MATCH_STATUS_UNIQUE
            or match.unique_candidate_index is None
        ):
            raise ValueError(
                "unreused row is no longer a unique match"
            )

        label = index[key][match.unique_candidate_index]
        relation = classify_interval_boundary(
            first_seen,
            last_seen,
            label,
        )

        # 唯一“相交”候选不可能与C流完全不相交。
        if relation == "disjoint":
            raise ValueError(
                "unique match has disjoint time intervals"
            )

        boundary_counts[relation] += 1

        if relation == "exact":
            exact_rows.add(row_number)

    if (
        row_number != counts["flows_total"]
        or sum(boundary_counts.values())
        != counts["matches_unique_unreused"]
    ):
        raise ValueError(
            "flow CSV changed between boundary audit passes"
        )

    if len(exact_rows) != boundary_counts["exact"]:
        raise ValueError("exact boundary row count differs")

    return counts, boundary_counts, frozenset(exact_rows)


def audit_candidate_boundaries(
    index: Dict[FlowKey, List[LabelInterval]],
    input_stream: TextIO,
) -> Tuple[Dict[str, int], Dict[str, int]]:
    """保持原有CLI使用的双返回值接口。"""

    counts, boundary_counts, _ = (
        audit_candidate_boundaries_with_exact_rows(
            index,
            input_stream,
        )
    )
    return counts, boundary_counts

def main() -> int:
    """审计IoT-23流；可选地生成逐行审查sidecar。"""

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
    parser.add_argument(
        "--feature-csv",
        type=Path,
        help="Matching flow_features_v1 CSV.",
    )
    parser.add_argument(
        "--capture-id",
        help="Stable identifier of the source capture.",
    )
    parser.add_argument(
        "--review-output",
        type=Path,
        help="Create a new IoT-23 review sidecar CSV.",
    )
    parser.add_argument(
        "--boundary-summary",
        action="store_true",
        help="Read-only boundary counts for unreused unique matches.",
    )

    arguments = parser.parse_args()

    review_options = (
        arguments.feature_csv,
        arguments.capture_id,
        arguments.review_output,
    )

    if (
        any(value is not None for value in review_options)
        and not all(
            value is not None for value in review_options
        )
    ):
        parser.error(
            "--feature-csv, --capture-id and "
            "--review-output must be used together"
        )

    if arguments.boundary_summary and arguments.review_output is not None:
        parser.error(
            "--boundary-summary cannot be combined with --review-output"
        )

    review_output_created = False
    boundary_counts = None

    try:
        aligned_rows = None

        if arguments.review_output is not None:
            # 校验失败时还没有创建目标文件。
            with arguments.flow_csv.open(
                "r",
                encoding="utf-8",
                newline="",
            ) as flow_stream:
                with arguments.feature_csv.open(
                    "r",
                    encoding="utf-8",
                    newline="",
                ) as feature_stream:
                    aligned_rows = (
                        validate_flow_feature_alignment(
                            flow_stream,
                            feature_stream,
                        )
                    )

        index = load_label_index(arguments.label_file)

        with arguments.flow_csv.open(
            "r",
            encoding="utf-8",
            newline="",
        ) as input_stream:
            if arguments.review_output is None:
                if arguments.boundary_summary:
                    counts, boundary_counts = audit_candidate_boundaries(
                        index,
                        input_stream,
                    )
                else:
                    counts = audit_flow_csv(
                        index,
                        input_stream,
                    )
            else:
                # x表示独占创建：目标已存在时直接失败，不覆盖。
                with arguments.review_output.open(
                    "x",
                    encoding="utf-8",
                    newline="",
                ) as output_stream:
                    review_output_created = True

                    counts = write_iot23_review_csv(
                        index=index,
                        input_stream=input_stream,
                        output_stream=output_stream,
                        capture_id=arguments.capture_id,
                    )

                    if counts["flows_total"] != aligned_rows:
                        raise ValueError(
                            "review and feature CSV row counts differ"
                        )

    except (
        OSError,
        UnicodeError,
        csv.Error,
        TypeError,
        ValueError,
    ) as error:
        cleanup_error = None

        if review_output_created:
            try:
                arguments.review_output.unlink()
            except OSError as current_cleanup_error:
                cleanup_error = current_cleanup_error

        print(
            f"IoT-23 flow match audit failed: {error}",
            file=sys.stderr,
        )

        if cleanup_error is not None:
            print(
                "Additionally failed to remove incomplete "
                f"review output: {cleanup_error}",
                file=sys.stderr,
            )

        return 1

    # 只在全部成功后输出摘要；失败时stdout保持为空。
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

    if boundary_counts is not None:
        print(
            "candidate_boundary_total="
            f"{sum(boundary_counts.values())}"
        )
        for relation in BOUNDARY_RELATIONS:
            print(
                f"candidate_boundary_{relation}="
                f"{boundary_counts[relation]}"
            )

    return 0


if __name__ == "__main__":
    sys.exit(main())