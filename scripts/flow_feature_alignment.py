#!/usr/bin/env python3

"""只读核对普通流CSV与flow_features_v1特征CSV的行对应关系。"""

import csv
from itertools import zip_longest
from typing import TextIO

from flow_csv_identity import (
    EXPECTED_FLOW_COLUMNS,
    parse_flow_key,
    parse_flow_timestamp,
    parse_unsigned_integer,
    validate_row_shape,
)
from flow_sample_metadata import (
    SUPPORTED_FEATURE_SCHEMA_VERSION,
)

EXPECTED_FEATURE_COLUMNS = (
    "schema_version",
    "protocol",
    "duration_microseconds",
    "total_packet_count",
    "total_captured_byte_count",
    "total_wire_byte_count",
    "mean_captured_bytes_per_packet",
    "mean_wire_bytes_per_packet",
    "packet_count_imbalance_ratio",
    "wire_byte_count_imbalance_ratio",
    "tcp_state_applicable",
    "tcp_phase",
    "tcp_handshake_completed",
)

MAX_UNSIGNED_64 = (1 << 64) - 1


def validate_flow_feature_alignment(
    flow_stream: TextIO,
    feature_stream: TextIO,
) -> int:
    """
    逐行核对两份CSV，返回共同的数据行数。

    两个输入流由调用者拥有；本函数只读取，不关闭、不修改。
    调用时，两个流的位置都应在各自表头之前。

    这是内容一致性检查，不是“两个文件确由同一次运行产生”
    的密码学证明。
    """

    if flow_stream is feature_stream:
        raise ValueError(
            "flow and feature streams must differ"
        )

    flow_reader = csv.DictReader(flow_stream)
    feature_reader = csv.DictReader(feature_stream)

    if tuple(flow_reader.fieldnames or ()) != EXPECTED_FLOW_COLUMNS:
        raise ValueError("unexpected flow CSV columns")

    if tuple(feature_reader.fieldnames or ()) != (
        EXPECTED_FEATURE_COLUMNS
    ):
        raise ValueError("unexpected feature CSV columns")

    rows_checked = 0

    # zip_longest不会像zip那样悄悄丢掉较长文件的末尾行。
    for row_number, (flow, feature) in enumerate(
        zip_longest(flow_reader, feature_reader),
        start=1,
    ):
        if flow is None or feature is None:
            raise ValueError(
                f"flow and feature CSV row counts differ "
                f"at data row {row_number}"
            )

        validate_row_shape(
            flow,
            EXPECTED_FLOW_COLUMNS,
            "flow CSV",
            flow_reader.line_num,
        )
        validate_row_shape(
            feature,
            EXPECTED_FEATURE_COLUMNS,
            "feature CSV",
            feature_reader.line_num,
        )

        if feature["schema_version"] != (
            SUPPORTED_FEATURE_SCHEMA_VERSION
        ):
            raise ValueError(
                f"feature CSV row {row_number}: "
                "unsupported schema version"
            )

        key = parse_flow_key(
            flow,
            flow_reader.line_num,
        )
        first_seen = parse_flow_timestamp(
            flow,
            "first_seen",
            flow_reader.line_num,
        )
        last_seen = parse_flow_timestamp(
            flow,
            "last_seen",
            flow_reader.line_num,
        )

        if last_seen < first_seen:
            raise ValueError(
                f"flow CSV row {row_number}: reversed time interval"
            )

        expected = {
            "protocol": key[0],
            "duration_microseconds": last_seen - first_seen,
        }

        for target, left, right in (
            (
                "total_packet_count",
                "a_to_b_packets",
                "b_to_a_packets",
            ),
            (
                "total_captured_byte_count",
                "a_to_b_captured_bytes",
                "b_to_a_captured_bytes",
            ),
            (
                "total_wire_byte_count",
                "a_to_b_wire_bytes",
                "b_to_a_wire_bytes",
            ),
        ):
            total = (
                parse_unsigned_integer(
                    flow[left],
                    left,
                    MAX_UNSIGNED_64,
                )
                + parse_unsigned_integer(
                    flow[right],
                    right,
                    MAX_UNSIGNED_64,
                )
            )

            if total > MAX_UNSIGNED_64:
                raise ValueError(
                    f"flow CSV row {row_number}: {target} overflow"
                )

            expected[target] = total

        for column, expected_value in expected.items():
            actual_value = parse_unsigned_integer(
                feature[column],
                column,
                MAX_UNSIGNED_64,
            )

            if actual_value != expected_value:
                raise ValueError(
                    f"CSV data row {row_number} differs at {column}: "
                    f"{actual_value} != {expected_value}"
                )

        expected_applicable = int(key[0] == 6)
        actual_applicable = parse_unsigned_integer(
            feature["tcp_state_applicable"],
            "tcp_state_applicable",
            1,
        )

        if actual_applicable != expected_applicable:
            raise ValueError(
                f"CSV data row {row_number} differs at "
                "tcp_state_applicable"
            )

        if feature["tcp_phase"] != flow["tcp_state"]:
            raise ValueError(
                f"CSV data row {row_number} differs at tcp_phase"
            )

        rows_checked += 1

    if rows_checked == 0:
        raise ValueError("flow and feature CSV contain no records")

    return rows_checked