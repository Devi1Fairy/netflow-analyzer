#!/usr/bin/env python3

"""
审计IoT-23 Zeek标注流文件。

脚本只读取输入文件，不修改原始数据，也不生成训练集。
它负责验证文件结构，并统计：

- 项目统一监督分类；
- Zeek协议；
- IoT-23详细标签。
"""

import argparse
import sys
from pathlib import Path
from typing import Dict, Tuple

from iot23_label import (
    LABEL_GROUPS,
    classify_label,
    parse_appended_label_fields,
)


# IoT-23标签文件前20个标准Zeek conn字段。
#
# 原本的第21个tunnel_parents字段被标签工具扩展成：
#
#     tunnel_parents   label   detailed-label
#
# 这三个值仍共同位于最后一个制表符字段中。
EXPECTED_BASE_COLUMNS = (
    "ts",
    "uid",
    "id.orig_h",
    "id.orig_p",
    "id.resp_h",
    "id.resp_p",
    "proto",
    "service",
    "duration",
    "orig_bytes",
    "resp_bytes",
    "conn_state",
    "local_orig",
    "local_resp",
    "missed_bytes",
    "history",
    "orig_pkts",
    "orig_ip_bytes",
    "resp_pkts",
    "resp_ip_bytes",
)

EXPECTED_APPENDED_COLUMNS = (
    "tunnel_parents",
    "label",
    "detailed-label",
)

EXPECTED_RECORD_FIELD_COUNT = (
    len(EXPECTED_BASE_COLUMNS) + 1
)


def parse_arguments() -> argparse.Namespace:
    """解析命令行中的IoT-23标签文件路径。"""

    parser = argparse.ArgumentParser(
        description=(
            "Validate and summarize an IoT-23 "
            "Zeek labeled connection log."
        )
    )

    parser.add_argument(
        "input_file",
        type=Path,
        help="Path to an IoT-23 zeek-conn-log.labeled file.",
    )

    return parser.parse_args()


def validate_fields_directive(
    line: str,
    line_number: int,
) -> None:
    """
    验证Zeek的#fields声明。

    line不包含行尾换行符。

    标准字段使用制表符分隔；最后一段内部的三个扩展列名
    使用连续空格分隔，因此要进行第二次split()。
    """

    fields = line.split("\t")

    if not fields or fields[0] != "#fields":
        raise ValueError(
            f"line {line_number}: invalid #fields directive"
        )

    actual_base_columns = tuple(fields[1:-1])

    if actual_base_columns != EXPECTED_BASE_COLUMNS:
        raise ValueError(
            f"line {line_number}: unexpected base columns"
        )

    if len(fields) < 2:
        raise ValueError(
            f"line {line_number}: missing appended columns"
        )

    actual_appended_columns = tuple(
        fields[-1].split()
    )

    if actual_appended_columns != EXPECTED_APPENDED_COLUMNS:
        raise ValueError(
            f"line {line_number}: "
            "unexpected appended label columns"
        )


def inspect_label_file(
    input_file: Path,
) -> Tuple[
    Dict[str, int],
    Dict[str, int],
    Dict[str, int],
]:
    """
    验证并统计IoT-23标签文件。

    返回三个由调用者拥有的新字典：

    1. 项目统一标签数量；
    2. 协议数量；
    3. 详细标签数量。

    文件对象由with语句管理，函数返回后文件已经关闭。
    """

    label_counts = {
        group: 0
        for group in LABEL_GROUPS
    }

    protocol_counts: Dict[str, int] = {}
    detailed_label_counts: Dict[str, int] = {}

    separator_seen = False
    fields_seen = False
    record_count = 0

    with input_file.open(
        "r",
        encoding="utf-8",
        newline="",
    ) as input_stream:
        for line_number, raw_line in enumerate(
            input_stream,
            start=1,
        ):
            line = raw_line.rstrip("\r\n")

            if not line:
                continue

            if line.startswith("#separator "):
                if separator_seen:
                    raise ValueError(
                        f"line {line_number}: "
                        "duplicate #separator directive"
                    )

                if line != r"#separator \x09":
                    raise ValueError(
                        f"line {line_number}: "
                        "unsupported field separator"
                    )

                separator_seen = True
                continue

            if line.startswith("#fields"):
                if fields_seen:
                    raise ValueError(
                        f"line {line_number}: "
                        "duplicate #fields directive"
                    )

                validate_fields_directive(
                    line,
                    line_number,
                )

                fields_seen = True
                continue

            # 其他Zeek元数据，例如#types、#open和#close，
            # 本步骤暂时跳过。
            if line.startswith("#"):
                continue

            if not separator_seen:
                raise ValueError(
                    f"line {line_number}: "
                    "data appears before #separator"
                )

            if not fields_seen:
                raise ValueError(
                    f"line {line_number}: "
                    "data appears before #fields"
                )

            fields = line.split("\t")

            if len(fields) != EXPECTED_RECORD_FIELD_COUNT:
                raise ValueError(
                    f"line {line_number}: expected "
                    f"{EXPECTED_RECORD_FIELD_COUNT} fields, "
                    f"received {len(fields)}"
                )

            if any(
                field == ""
                for field in fields[:-1]
            ):
                raise ValueError(
                    f"line {line_number}: "
                    "empty base field"
                )

            protocol = fields[6].strip()

            if not protocol or protocol == "-":
                raise ValueError(
                    f"line {line_number}: "
                    "missing protocol"
                )

            (
                _tunnel_parents,
                generic_label,
                detailed_label,
            ) = parse_appended_label_fields(
                fields[-1]
            )

            label_group = classify_label(
                generic_label
            )

            label_counts[label_group] += 1

            protocol_counts[protocol] = (
                protocol_counts.get(protocol, 0) + 1
            )

            detailed_label_counts[detailed_label] = (
                detailed_label_counts.get(
                    detailed_label,
                    0,
                ) + 1
            )

            record_count += 1

    if not separator_seen:
        raise ValueError(
            "missing #separator directive"
        )

    if not fields_seen:
        raise ValueError(
            "missing #fields directive"
        )

    if record_count == 0:
        raise ValueError(
            "the label file contains no data records"
        )

    return (
        label_counts,
        protocol_counts,
        detailed_label_counts,
    )


def main() -> int:
    """运行审计并输出顺序稳定的key=value摘要。"""

    arguments = parse_arguments()

    try:
        (
            label_counts,
            protocol_counts,
            detailed_label_counts,
        ) = inspect_label_file(
            arguments.input_file
        )
    except (
        OSError,
        UnicodeError,
        ValueError,
    ) as error:
        print(
            f"IoT-23 label audit failed: {error}",
            file=sys.stderr,
        )
        return 1

    print(f"input={arguments.input_file}")
    print(f"total={sum(label_counts.values())}")

    for group in LABEL_GROUPS:
        print(
            f"{group}={label_counts[group]}"
        )

    for protocol in sorted(protocol_counts):
        print(
            f"protocol[{protocol}]="
            f"{protocol_counts[protocol]}"
        )

    for detailed_label in sorted(
        detailed_label_counts
    ):
        print(
            f"detailed_label[{detailed_label}]="
            f"{detailed_label_counts[detailed_label]}"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())