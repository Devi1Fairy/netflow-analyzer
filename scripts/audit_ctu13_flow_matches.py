#!/usr/bin/env python3

"""
审计C流记录CSV与CTU-13监督标签之间的匹配关系。

默认只报告匹配质量。提供capture ID和输出路径时，同时生成
流特征对应的metadata sidecar，但不会生成训练集，也不会
自动选择歧义候选。
"""

import argparse
import csv
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import (
    DefaultDict,
    Dict,
    List,
    Optional,
    TextIO,
    Tuple,
)

from flow_sample_metadata import (
    MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS,
    MATCH_STATUS_AMBIGUOUS_SAME_LABEL,
    MATCH_STATUS_UNIQUE,
    MATCH_STATUS_UNMATCHED,
    SUPPORTED_LABEL_GROUPS,
    FlowSampleIdentity,
    FlowSampleMetadata,
    build_sample_metadata,
    write_sample_metadata_csv_header,
    write_sample_metadata_csv_record,
)

from ctu13_flow_identity import (
    Ctu13FlowIdentity,
    parse_ipv4_address,
    normalize_flow_identity,
)
from inspect_ctu13_labels import (
    EXPECTED_COLUMNS as EXPECTED_LABEL_COLUMNS,
    classify_label,
)


EXPECTED_FLOW_COLUMNS = (
    "protocol",
    "tcp_state",
    "endpoint_a_ip",
    "endpoint_a_port",
    "endpoint_b_ip",
    "endpoint_b_port",
    "a_to_b_packets",
    "a_to_b_captured_bytes",
    "a_to_b_wire_bytes",
    "b_to_a_packets",
    "b_to_a_captured_bytes",
    "b_to_a_wire_bytes",
    "first_seen_seconds",
    "first_seen_microseconds",
    "last_seen_seconds",
    "last_seen_microseconds",
)

SUPPORTED_PROTOCOLS = {
    1,
    6,
    17,
}

MICROSECONDS_PER_SECOND = 1_000_000

FlowKey = Tuple[int, int, int, int, int]


@dataclass(frozen=True)
class LabelInterval:
    """一条已经规范化且可用于连接的标签时间区间。"""

    start_microseconds: int
    end_microseconds: int
    label_group: str

@dataclass(frozen=True)
class FlowMatchClassification:
    """
    一条C流记录与监督标签之间的匹配分类。

    candidate_count保存时间与五元组都匹配的候选数。
    label_group只在候选标签能够得到单一结论时存在。
    """

    status: str
    candidate_count: int
    label_group: Optional[str]

def parse_arguments() -> argparse.Namespace:
    """解析标签文件和C流记录CSV路径。"""

    parser = argparse.ArgumentParser(
        description=(
            "Audit temporal five-tuple matches between "
            "CTU-13 labels and Netflow Analyzer flow CSV."
        )
    )

    parser.add_argument(
        "label_file",
        type=Path,
        help="Path to the CTU-13 .binetflow label file.",
    )

    parser.add_argument(
        "flow_csv",
        type=Path,
        help="Path to a flow CSV produced by --csv.",
    )

    parser.add_argument(
        "--capture-id",
        help=(
            "Stable identifier of the source capture. "
            "Required with --metadata-output."
        ),
    )

    parser.add_argument(
        "--metadata-output",
        type=Path,
        help=(
            "Create a flow sample metadata sidecar. "
            "The destination must not already exist."
        ),
    )

    arguments = parser.parse_args()

    if (
        (arguments.capture_id is None)
        != (arguments.metadata_output is None)
    ):
        parser.error(
            "--capture-id and --metadata-output "
            "must be used together"
        )

    return arguments


def require_text(
    row,
    column: str,
    source_name: str,
    line_number: int,
) -> str:
    """读取一个必需且非空的CSV字段。"""

    if column not in row or row[column] is None:
        raise ValueError(
            f"{source_name} line {line_number}: "
            f"missing field {column}"
        )

    value = row[column].strip()

    if not value:
        raise ValueError(
            f"{source_name} line {line_number}: "
            f"empty field {column}"
        )

    return value


def validate_row_shape(
    row,
    expected_columns,
    source_name: str,
    line_number: int,
) -> None:
    """验证CSV记录没有多余或缺失字段。"""

    if None in row:
        raise ValueError(
            f"{source_name} line {line_number}: "
            "too many fields"
        )

    if any(
        row[column] is None
        for column in expected_columns
    ):
        raise ValueError(
            f"{source_name} line {line_number}: "
            "missing fields"
        )


def parse_unsigned_integer(
    raw_value: str,
    column: str,
    maximum: int,
) -> int:
    """解析指定上限内的十进制无符号整数。"""

    text = raw_value.strip()

    if not text.isdecimal():
        raise ValueError(
            f"invalid {column}: {raw_value!r}"
        )

    value = int(text, 10)

    if value > maximum:
        raise ValueError(
            f"{column} is out of range: {value}"
        )

    return value


def flow_key_from_label(
    identity: Ctu13FlowIdentity,
) -> FlowKey:
    """从标签身份提取不含时间的规范化五元组。"""

    return (
        identity.protocol,
        identity.endpoint_a.ipv4_address,
        identity.endpoint_a.port,
        identity.endpoint_b.ipv4_address,
        identity.endpoint_b.port,
    )


def load_label_index(
    label_file: Path,
):
    """
    读取可监督标签并按规范化五元组建立索引。

    exclude标签只计数，不进入匹配索引；因为它们不能作为当前
    二分类训练标签。协议不受支持的可监督标签单独计数。
    """

    index: DefaultDict[
        FlowKey,
        List[LabelInterval],
    ] = defaultdict(list)

    counts = {
        "labels_total": 0,
        "labels_excluded": 0,
        "labels_unsupported": 0,
        "labels_indexed": 0,
        "labels_indexed_malicious": 0,
        "labels_indexed_benign": 0,
    }

    with label_file.open(
        "r",
        encoding="utf-8",
        newline="",
    ) as input_stream:
        reader = csv.DictReader(input_stream)

        actual_columns = tuple(
            reader.fieldnames or ()
        )

        if actual_columns != EXPECTED_LABEL_COLUMNS:
            raise ValueError(
                "unexpected label CSV columns"
            )

        for row in reader:
            validate_row_shape(
                row,
                EXPECTED_LABEL_COLUMNS,
                "label CSV",
                reader.line_num,
            )

            counts["labels_total"] += 1

            raw_label = require_text(
                row,
                "Label",
                "label CSV",
                reader.line_num,
            )

            label_group = classify_label(
                raw_label
            )

            if label_group == "exclude":
                counts["labels_excluded"] += 1
                continue

            identity = normalize_flow_identity(
                row
            )

            if identity is None:
                counts["labels_unsupported"] += 1
                continue

            key = flow_key_from_label(
                identity
            )

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

            counts["labels_indexed"] += 1
            counts[
                f"labels_indexed_{label_group}"
            ] += 1

    if counts["labels_total"] == 0:
        raise ValueError(
            "label CSV contains no records"
        )

    return index, counts


def parse_flow_key(
    row,
    line_number: int,
) -> FlowKey:
    """解析并验证C流CSV中的规范化五元组。"""

    protocol = parse_unsigned_integer(
        require_text(
            row,
            "protocol",
            "flow CSV",
            line_number,
        ),
        "protocol",
        255,
    )

    if protocol not in SUPPORTED_PROTOCOLS:
        raise ValueError(
            f"unsupported flow CSV protocol: {protocol}"
        )

    endpoint_a_address = parse_ipv4_address(
        require_text(
            row,
            "endpoint_a_ip",
            "flow CSV",
            line_number,
        )
    )

    endpoint_b_address = parse_ipv4_address(
        require_text(
            row,
            "endpoint_b_ip",
            "flow CSV",
            line_number,
        )
    )

    endpoint_a_port = parse_unsigned_integer(
        require_text(
            row,
            "endpoint_a_port",
            "flow CSV",
            line_number,
        ),
        "endpoint_a_port",
        65535,
    )

    endpoint_b_port = parse_unsigned_integer(
        require_text(
            row,
            "endpoint_b_port",
            "flow CSV",
            line_number,
        ),
        "endpoint_b_port",
        65535,
    )

    if (
        protocol == 1
        and (
            endpoint_a_port != 0
            or endpoint_b_port != 0
        )
    ):
        raise ValueError(
            "ICMP flow CSV ports must both be zero"
        )

    endpoint_a = (
        endpoint_a_address,
        endpoint_a_port,
    )

    endpoint_b = (
        endpoint_b_address,
        endpoint_b_port,
    )

    if endpoint_a > endpoint_b:
        raise ValueError(
            "flow CSV endpoints are not canonical"
        )

    return (
        protocol,
        endpoint_a_address,
        endpoint_a_port,
        endpoint_b_address,
        endpoint_b_port,
    )


def parse_flow_timestamp(
    row,
    prefix: str,
    line_number: int,
) -> int:
    """把C流CSV的秒和微秒字段合成为整数微秒。"""

    seconds = parse_unsigned_integer(
        require_text(
            row,
            f"{prefix}_seconds",
            "flow CSV",
            line_number,
        ),
        f"{prefix}_seconds",
        (1 << 63) - 1,
    )

    microseconds = parse_unsigned_integer(
        require_text(
            row,
            f"{prefix}_microseconds",
            "flow CSV",
            line_number,
        ),
        f"{prefix}_microseconds",
        999999,
    )

    return (
        seconds * MICROSECONDS_PER_SECOND
        + microseconds
    )


def intervals_overlap(
    first_start: int,
    first_end: int,
    second_start: int,
    second_end: int,
) -> bool:
    """判断两个闭区间是否至少共享一个时间点。"""

    return (
        first_start <= second_end
        and second_start <= first_end
    )

def classify_match_candidates(
    candidates: List[LabelInterval],
) -> FlowMatchClassification:
    """
    把候选标签列表归类为唯一、未匹配或两类歧义。

    本函数不选择歧义候选，也不把未知标签自动解释为正常。
    """

    candidate_count = len(candidates)

    if candidate_count == 0:
        return FlowMatchClassification(
            status=MATCH_STATUS_UNMATCHED,
            candidate_count=0,
            label_group=None,
        )

    candidate_groups = {
        candidate.label_group
        for candidate in candidates
    }

    if not candidate_groups.issubset(
        SUPPORTED_LABEL_GROUPS
    ):
        raise ValueError(
            "match candidates contain an unsupported "
            "label group"
        )

    if candidate_count == 1:
        return FlowMatchClassification(
            status=MATCH_STATUS_UNIQUE,
            candidate_count=1,
            label_group=candidates[0].label_group,
        )

    if len(candidate_groups) == 1:
        return FlowMatchClassification(
            status=(
                MATCH_STATUS_AMBIGUOUS_SAME_LABEL
            ),
            candidate_count=candidate_count,
            label_group=next(
                iter(candidate_groups)
            ),
        )

    return FlowMatchClassification(
        status=(
            MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS
        ),
        candidate_count=candidate_count,
        label_group=None,
    )

def build_flow_sample_metadata(
    capture_id: str,
    feature_row_number: int,
    flow_key: FlowKey,
    first_seen_microseconds: int,
    last_seen_microseconds: int,
    classification: FlowMatchClassification,
) -> FlowSampleMetadata:
    """
    把已经解析的C流身份和标签匹配结果组装成样本元数据。

    flow_key必须使用C流表相同的规范化端点顺序。
    feature_row_number从1开始，只计算特征CSV数据行。
    本函数创建新的不可变对象，不持有CSV行或文件资源。
    """

    if not isinstance(
        classification,
        FlowMatchClassification,
    ):
        raise TypeError(
            "classification must be "
            "FlowMatchClassification"
        )

    (
        protocol,
        endpoint_a_ipv4,
        endpoint_a_port,
        endpoint_b_ipv4,
        endpoint_b_port,
    ) = flow_key

    identity = FlowSampleIdentity(
        capture_id=capture_id,
        protocol=protocol,
        endpoint_a_ipv4=endpoint_a_ipv4,
        endpoint_a_port=endpoint_a_port,
        endpoint_b_ipv4=endpoint_b_ipv4,
        endpoint_b_port=endpoint_b_port,
        first_seen_microseconds=(
            first_seen_microseconds
        ),
        last_seen_microseconds=(
            last_seen_microseconds
        ),
    )

    return build_sample_metadata(
        identity=identity,
        feature_row_number=feature_row_number,
        match_status=classification.status,
        candidate_count=(
            classification.candidate_count
        ),
        label_group=classification.label_group,
    )

def audit_flow_matches(
    label_file: Path,
    flow_csv: Path,
    capture_id: Optional[str] = None,
    metadata_output_stream: Optional[TextIO] = None,
) -> Dict[str, int]:
    """
    审计所有C流记录并可选写出样本元数据。

    capture_id和metadata_output_stream必须同时提供或同时省略。
    本函数只借用输出流，不负责关闭或刷新它。
    """

    if (
        (capture_id is None)
        != (metadata_output_stream is None)
    ):
        raise ValueError(
            "capture_id and metadata_output_stream "
            "must be provided together"
        )

    if metadata_output_stream is not None:
        write_sample_metadata_csv_header(
            metadata_output_stream
        )

    label_index, counts = load_label_index(
        label_file
    )

    counts.update(
        {
            "flows_total": 0,
            "matches_unique": 0,
            "matches_unique_malicious": 0,
            "matches_unique_benign": 0,
            "matches_unmatched": 0,
            "matches_ambiguous_same_label": 0,
            "matches_ambiguous_conflicting_labels": 0,
        }
    )

    with flow_csv.open(
        "r",
        encoding="utf-8",
        newline="",
    ) as input_stream:
        reader = csv.DictReader(input_stream)

        actual_columns = tuple(
            reader.fieldnames or ()
        )

        if actual_columns != EXPECTED_FLOW_COLUMNS:
            raise ValueError(
                "unexpected flow CSV columns"
            )

        for row in reader:
            validate_row_shape(
                row,
                EXPECTED_FLOW_COLUMNS,
                "flow CSV",
                reader.line_num,
            )

            key = parse_flow_key(
                row,
                reader.line_num,
            )

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

            if first_seen > last_seen:
                raise ValueError(
                    "flow CSV first_seen is after last_seen"
                )

            counts["flows_total"] += 1

            candidates = [
                candidate
                for candidate in label_index.get(
                    key,
                    ()
                )
                if intervals_overlap(
                    first_seen,
                    last_seen,
                    candidate.start_microseconds,
                    candidate.end_microseconds,
                )
            ]

            classification = (
                classify_match_candidates(
                    candidates
                )
            )

            if metadata_output_stream is not None:
                metadata = build_flow_sample_metadata(
                    capture_id=capture_id,
                    feature_row_number=(
                        counts["flows_total"]
                    ),
                    flow_key=key,
                    first_seen_microseconds=(
                        first_seen
                    ),
                    last_seen_microseconds=(
                        last_seen
                    ),
                    classification=classification,
                )

                write_sample_metadata_csv_record(
                    metadata_output_stream,
                    metadata,
                )

            counts[
                f"matches_{classification.status}"
            ] += 1

            if (
                classification.status
                == MATCH_STATUS_UNIQUE
            ):
                counts[
                    "matches_unique_"
                    f"{classification.label_group}"
                ] += 1

    if counts["flows_total"] == 0:
        raise ValueError(
            "flow CSV contains no records"
        )

    return counts


def main() -> int:
    """运行审计并输出稳定的key=value摘要。"""

    arguments = parse_arguments()

    metadata_output_created = False

    try:
        if arguments.metadata_output is None:
            counts = audit_flow_matches(
                arguments.label_file,
                arguments.flow_csv,
            )
        else:
            with arguments.metadata_output.open(
                "x",
                encoding="utf-8",
                newline="",
            ) as metadata_output_stream:
                metadata_output_created = True

                counts = audit_flow_matches(
                    label_file=arguments.label_file,
                    flow_csv=arguments.flow_csv,
                    capture_id=arguments.capture_id,
                    metadata_output_stream=(
                        metadata_output_stream
                    ),
                )

    except (
        OSError,
        csv.Error,
        TypeError,
        ValueError,
    ) as error:
        cleanup_error = None

        if metadata_output_created:
            try:
                arguments.metadata_output.unlink()
            except OSError as current_cleanup_error:
                cleanup_error = current_cleanup_error

        print(
            f"CTU-13 flow match audit failed: {error}",
            file=sys.stderr,
        )

        if cleanup_error is not None:
            print(
                "Additionally failed to remove incomplete "
                f"metadata output: {cleanup_error}",
                file=sys.stderr,
            )

        return 1

    output_fields = (
        "labels_total",
        "labels_excluded",
        "labels_unsupported",
        "labels_indexed",
        "labels_indexed_malicious",
        "labels_indexed_benign",
        "flows_total",
        "matches_unique",
        "matches_unique_malicious",
        "matches_unique_benign",
        "matches_unmatched",
        "matches_ambiguous_same_label",
        "matches_ambiguous_conflicting_labels",
    )

    for field in output_fields:
        print(f"{field}={counts[field]}")

    return 0


if __name__ == "__main__":
    sys.exit(main())