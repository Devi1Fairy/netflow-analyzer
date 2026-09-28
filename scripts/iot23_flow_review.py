#!/usr/bin/env python3

"""IoT-23逐行审查文件的版本化CSV契约。"""

import csv
from typing import TextIO


IOT23_REVIEW_SCHEMA_VERSION = "iot23_flow_review_v1"

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