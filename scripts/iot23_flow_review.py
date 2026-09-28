#!/usr/bin/env python3

"""IoT-23逐行审查文件的版本化CSV契约。"""

import csv
from typing import TextIO
from flow_sample_metadata import (
    FlowSampleMetadata,
    MATCH_STATUS_UNIQUE,
    build_sample_metadata,
)

IOT23_REVIEW_SCHEMA_VERSION = "iot23_flow_review_v1"
REVIEW_NOT_UNIQUE = "not_unique"
REVIEW_UNIQUE_REUSED = "unique_reused"
REVIEW_UNIQUE_UNREUSED_CANDIDATE = "unique_unreused_candidate"

IOT23_REVIEW_CSV_COLUMNS = (
    "review_schema_version",
    "feature_schema_version",
    "feature_row_number",
    "sample_id",
    "capture_id",
    "protocol",
    "endpoint_a_ip",
    "endpoint_a_port",
    "endpoint_b_ip",
    "endpoint_b_port",
    "first_seen_unix_microseconds",
    "last_seen_unix_microseconds",
    "match_status",
    "candidate_count",
    "label_group",
    "review_status",
)


def write_iot23_review_csv_header(
    output_stream: TextIO,
) -> None:
    """
    写出固定表头；只借用文本流，不负责关闭或刷新。

    底层写入失败时让异常传给调用者，避免误报成功。
    """

    writer = csv.writer(
        output_stream,
        lineterminator="\n",
    )
    writer.writerow(IOT23_REVIEW_CSV_COLUMNS)

def validate_iot23_review_record(
    metadata: FlowSampleMetadata,
    review_status: str,
) -> FlowSampleMetadata:
    """
    写出前验证身份、匹配信息和审查状态的组合。

    只读取metadata，不修改对象或写文件。
    返回重新验证后构造的元数据对象。
    """

    if not isinstance(metadata, FlowSampleMetadata):
        raise TypeError("metadata must be FlowSampleMetadata")

    validated = build_sample_metadata(
        identity=metadata.identity,
        feature_row_number=metadata.feature_row_number,
        match_status=metadata.match_status,
        candidate_count=metadata.candidate_count,
        label_group=metadata.label_group,
    )

    if (
        not isinstance(review_status, str)
        or review_status not in (
            REVIEW_NOT_UNIQUE,
            REVIEW_UNIQUE_REUSED,
            REVIEW_UNIQUE_UNREUSED_CANDIDATE,
        )
    ):
        raise ValueError("invalid IoT-23 review status")

    if validated.match_status == MATCH_STATUS_UNIQUE:
        if review_status == REVIEW_NOT_UNIQUE:
            raise ValueError(
                "unique match cannot have not_unique review"
            )
    elif review_status != REVIEW_NOT_UNIQUE:
        raise ValueError(
            "non-unique match cannot have unique review"
        )

    return validated
