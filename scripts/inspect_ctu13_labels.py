#!/usr/bin/env python3

"""
审计CTU-13双向流标签文件。

脚本只读取官方.binetflow文件，不修改输入文件，也不生成训练集。
它负责验证字段结构，并按项目当前的保守标签策略统计：

- From-Botnet -> malicious
- From-Normal -> benign
- 其他标签    -> exclude
"""

import argparse
import csv
import sys
from pathlib import Path
from typing import Dict


EXPECTED_COLUMNS = (
    "StartTime",
    "Dur",
    "Proto",
    "SrcAddr",
    "Sport",
    "Dir",
    "DstAddr",
    "Dport",
    "State",
    "sTos",
    "dTos",
    "TotPkts",
    "TotBytes",
    "SrcBytes",
    "Label",
)

LABEL_GROUPS = (
    "malicious",
    "benign",
    "exclude",
)

MALICIOUS_LABEL_PREFIX = "flow=From-Botnet"
BENIGN_LABEL_PREFIX = "flow=From-Normal"


def parse_arguments() -> argparse.Namespace:
    """解析命令行中的CTU-13标签文件路径。"""

    parser = argparse.ArgumentParser(
        description=(
            "Validate and summarize labels from a CTU-13 "
            "bidirectional .binetflow file."
        )
    )

    parser.add_argument(
        "input_file",
        type=Path,
        help="Path to the CTU-13 .binetflow label file.",
    )

    return parser.parse_args()


def classify_label(raw_label: str) -> str:
    """
    把CTU-13原始标签映射到项目使用的三类审计结果。

    只有官方明确标记为从感染主机发出的流才视为恶意；
    只有官方明确标记为从正常主机发出的流才视为正常。
    其他方向和Background标签不强行赋予二分类标签。
    """

    if raw_label.startswith(MALICIOUS_LABEL_PREFIX):
        return "malicious"

    if raw_label.startswith(BENIGN_LABEL_PREFIX):
        return "benign"

    return "exclude"


def inspect_label_file(input_file: Path) -> Dict[str, int]:
    """
    验证并统计一个CTU-13双向流标签文件。

    文件对象由with语句拥有并负责关闭。
    csv.DictReader只在with代码块内借用文件对象。

    返回的新字典由调用者拥有，包含三个稳定分类的数量。
    """

    counts = {
        group: 0
        for group in LABEL_GROUPS
    }

    with input_file.open(
        "r",
        encoding="utf-8",
        newline="",
    ) as input_stream:
        reader = csv.DictReader(input_stream)

        actual_columns = tuple(reader.fieldnames or ())

        if actual_columns != EXPECTED_COLUMNS:
            raise ValueError(
                "unexpected CSV columns: "
                f"expected {EXPECTED_COLUMNS}, "
                f"received {actual_columns}"
            )

        for row in reader:
            # DictReader使用None键保存多出来的字段。
            if None in row:
                raise ValueError(
                    f"line {reader.line_num}: too many fields"
                )

            # 字段不足时，对应字典值会是None。
            if any(
                row[column] is None
                for column in EXPECTED_COLUMNS
            ):
                raise ValueError(
                    f"line {reader.line_num}: missing fields"
                )

            raw_label = row["Label"].strip()

            if not raw_label:
                raise ValueError(
                    f"line {reader.line_num}: empty Label"
                )

            group = classify_label(raw_label)
            counts[group] += 1

    if sum(counts.values()) == 0:
        raise ValueError(
            "the label file contains no data records"
        )

    return counts

def main() -> int:
    """运行标签审计并输出稳定的key=value摘要。"""

    arguments = parse_arguments()

    try:
        counts = inspect_label_file(
            arguments.input_file
        )
    except (OSError, csv.Error, ValueError) as error:
        print(
            f"CTU-13 label audit failed: {error}",
            file=sys.stderr,
        )
        return 1

    print(f"input={arguments.input_file}")
    print(f"total={sum(counts.values())}")

    for group in LABEL_GROUPS:
        print(f"{group}={counts[group]}")

    return 0


if __name__ == "__main__":
    sys.exit(main())