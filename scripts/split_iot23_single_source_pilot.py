#!/usr/bin/env python3
"""仅供同一 PCAP 的二分类开发试验；不用于正式泛化评估。"""

import argparse
import csv
import hashlib
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

from flow_sample_metadata import SUPPORTED_FEATURE_SCHEMA_VERSION
from iot23_sample_selection import (
    IOT23_CANDIDATE_CSV_COLUMNS,
    IOT23_CANDIDATE_SCHEMA_VERSION,
)


CAPTURE_ID = "iot23-s8-1-full-idle3s"
SOURCE_ID = "iot23-s8-1-original"
LABELS = ("benign", "malicious")

# 前 8 列是版本、身份、来源、原切分和标签；只有后 12 列是模型特征。
FEATURE_COLUMNS = IOT23_CANDIDATE_CSV_COLUMNS[8:]


def feature_digest(values):
    """把固定顺序的特征字符串编码为稳定摘要，用于确定切分顺序。"""
    encoded = json.dumps(
        values,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def build_pilot_split(input_stream):
    """读取借用的 CSV 流；返回样本切分及审计计数，不写文件。"""
    reader = csv.DictReader(input_stream)
    if tuple(reader.fieldnames or ()) != IOT23_CANDIDATE_CSV_COLUMNS:
        raise ValueError("unexpected candidate CSV columns")

    # 键是完整的 12 列特征；同键的流必须进入同一个集合。
    groups = defaultdict(list)
    seen_sample_ids = set()

    for line_number, row in enumerate(reader, start=2):
        if None in row or any(
            row[name] is None for name in IOT23_CANDIDATE_CSV_COLUMNS
        ):
            raise ValueError(f"invalid CSV row at line {line_number}")

        if row["capture_id"] != CAPTURE_ID:
            continue

        if (
            row["candidate_schema_version"]
            != IOT23_CANDIDATE_SCHEMA_VERSION
            or row["feature_schema_version"]
            != SUPPORTED_FEATURE_SCHEMA_VERSION
            or row["source_capture_id"] != SOURCE_ID
            or row["candidate_label_group"] not in LABELS
        ):
            raise ValueError(f"invalid candidate at line {line_number}")

        sample_id = row["sample_id"]
        if not sample_id or sample_id in seen_sample_ids:
            raise ValueError(f"duplicate or empty sample ID at line {line_number}")
        seen_sample_ids.add(sample_id)

        signature = tuple(row[name] for name in FEATURE_COLUMNS)
        groups[signature].append(
            (sample_id, row["candidate_label_group"])
        )

    groups_by_label = {label: [] for label in LABELS}
    for signature, members in groups.items():
        labels = {label for _, label in members}

        # 同一输入向量若出现相反标签，先停下来审计，不能静默切分。
        if len(labels) != 1:
            raise ValueError("identical features have conflicting labels")

        label = labels.pop()
        groups_by_label[label].append((signature, members))

    if any(len(groups_by_label[label]) < 2 for label in LABELS):
        raise ValueError("each label needs at least two feature groups")

    assignments = {}
    row_counts = Counter()
    group_counts = Counter()

    for label in LABELS:
        entries = groups_by_label[label]

        # 最大组保留在训练侧。否则 5058 条相同 SYN 特征
        # 可能整体落进验证侧，造成极端失衡。
        largest = max(
            entries,
            key=lambda entry: (
                len(entry[1]),
                feature_digest(entry[0]),
            ),
        )
        remaining = sorted(
            (entry for entry in entries if entry[0] != largest[0]),
            key=lambda entry: feature_digest(entry[0]),
        )

        # 切分约 30% 的“不同特征组”，不是 30% 的数据行。
        validation_group_count = round(len(entries) * 0.30)
        validation_signatures = {
            signature
            for signature, _ in remaining[:validation_group_count]
        }

        for signature, members in entries:
            split = (
                "validation"
                if signature in validation_signatures
                else "train"
            )
            group_counts[split] += 1

            for sample_id, _ in members:
                assignments[sample_id] = split
                row_counts[(split, label)] += 1

    if len(assignments) != len(seen_sample_ids):
        raise ValueError("some candidates were not assigned")

    largest_group_size = max(len(members) for members in groups.values())
    return (
        assignments,
        row_counts,
        group_counts,
        len(groups),
        largest_group_size,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate_csv", type=Path)
    arguments = parser.parse_args()

    try:
        with arguments.candidate_csv.open(
            "r", encoding="utf-8", newline=""
        ) as input_stream:
            (
                assignments,
                row_counts,
                group_counts,
                pattern_count,
                largest_group_size,
            ) = build_pilot_split(input_stream)
    except (OSError, UnicodeError, csv.Error, ValueError) as error:
        print(f"pilot split failed: {error}", file=sys.stderr)
        return 1

    print(f"candidate_rows={len(assignments)}")
    print(f"unique_feature_patterns={pattern_count}")
    print(f"largest_pattern_rows={largest_group_size}")

    for split in ("train", "validation"):
        benign = row_counts[(split, "benign")]
        malicious = row_counts[(split, "malicious")]
        print(
            f"{split}: rows={benign + malicious} "
            f"benign={benign} malicious={malicious} "
            f"feature_groups={group_counts[split]}"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())