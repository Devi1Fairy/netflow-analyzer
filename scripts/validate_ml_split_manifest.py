#!/usr/bin/env python3
"""检查训练/验证切分是否把同一原始抓包放进不同集合。"""

import argparse
import csv
import sys
from pathlib import Path

from flow_sample_metadata import CAPTURE_ID_PATTERN


COLUMNS = ("capture_id", "source_capture_id", "split")
SPLITS = ("train", "validation", "test")


def validate_manifest_with_assignments(input_stream):
    """读取借用的文本流；成功返回各集合计数和原始抓包数。"""
    reader = csv.DictReader(input_stream)
    if tuple(reader.fieldnames or ()) != COLUMNS:
        raise ValueError("unexpected manifest columns")

    seen_captures = set()
    source_splits = {}
    assignments = {}
    counts = {split: 0 for split in SPLITS}

    for line_number, row in enumerate(reader, start=2):
        if None in row or any(row[name] is None for name in COLUMNS):
            raise ValueError(f"invalid CSV row at line {line_number}")

        capture_id = row["capture_id"]
        source_id = row["source_capture_id"]
        split = row["split"]

        for name, value in (
            ("capture_id", capture_id),
            ("source_capture_id", source_id),
        ):
            if CAPTURE_ID_PATTERN.fullmatch(value) is None:
                raise ValueError(f"invalid {name} at line {line_number}")

        if split not in counts:
            raise ValueError(f"invalid split at line {line_number}")
        if capture_id in seen_captures:
            raise ValueError(f"duplicate capture_id at line {line_number}")

        previous_split = source_splits.get(source_id)
        if previous_split is not None and previous_split != split:
            raise ValueError(
                f"source_capture_id crosses splits at line {line_number}"
            )

        seen_captures.add(capture_id)
        source_splits[source_id] = split
        # 把通过验证的提取结果绑定到原始抓包及数据集合。
        assignments[capture_id] = (source_id, split)
        counts[split] += 1

    if counts["train"] == 0 or counts["validation"] == 0:
        raise ValueError("train and validation each need a source capture")

    return counts, len(source_splits), assignments


def validate_manifest(input_stream):
    """兼容现有调用者：仍只返回计数和来源数量。"""

    counts, source_count, _ = validate_manifest_with_assignments(
        input_stream
    )
    return counts, source_count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    arguments = parser.parse_args()

    try:
        with arguments.manifest.open(
            "r", encoding="utf-8", newline=""
        ) as input_stream:
            counts, source_count = validate_manifest(input_stream)
    except (OSError, UnicodeError, csv.Error, ValueError) as error:
        print(f"split manifest validation failed: {error}", file=sys.stderr)
        return 1

    print(f"captures_total={sum(counts.values())}")
    print(f"source_groups={source_count}")
    for split in SPLITS:
        print(f"{split}={counts[split]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())