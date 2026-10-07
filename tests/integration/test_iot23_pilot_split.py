#!/usr/bin/env python3
"""用小型合成候选 CSV 检查单来源开发切分。"""

import argparse
import csv
import io
import sys
from pathlib import Path


def csv_text(columns, rows):
    output = io.StringIO()
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(columns)
    writer.writerows(rows)
    return output.getvalue()


def expect_value_error(build_pilot_split, text, expected_message):
    stream = io.StringIO(text)
    try:
        build_pilot_split(stream)
    except ValueError as error:
        if expected_message not in str(error):
            raise RuntimeError(f"unexpected error: {error}") from error
    else:
        raise RuntimeError("invalid input was accepted")

    if stream.closed:
        raise RuntimeError("split function closed the caller's stream")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scripts-dir", required=True, type=Path)
    arguments = parser.parse_args()

    sys.path.insert(0, str(arguments.scripts_dir.resolve()))

    from flow_sample_metadata import SUPPORTED_FEATURE_SCHEMA_VERSION
    from iot23_sample_selection import (
        IOT23_CANDIDATE_CSV_COLUMNS,
        IOT23_CANDIDATE_SCHEMA_VERSION,
    )
    from split_iot23_single_source_pilot import (
        CAPTURE_ID,
        SOURCE_ID,
        build_pilot_split,
    )

    def benign_feature(duration):
        return (
            "17", str(duration), "1", "60", "60", "60", "60",
            "1", "1", "0", "not-applicable", "0",
        )

    def malicious_feature(duration):
        return (
            "6", str(duration), "1", "74", "74", "74", "74",
            "1", "1", "1", "syn-seen", "0",
        )

    def row(number, sample_id, label, features):
        return (
            IOT23_CANDIDATE_SCHEMA_VERSION,
            SUPPORTED_FEATURE_SCHEMA_VERSION,
            sample_id,
            CAPTURE_ID,
            SOURCE_ID,
            str(number),
            "validation",  # 原 CSV 的跨来源切分；本试验不沿用它。
            label,
        ) + features

    rows = [
        row(1, "b1", "benign", benign_feature(1)),
        row(2, "b2", "benign", benign_feature(1)),
        row(3, "b3", "benign", benign_feature(1)),
        row(4, "b4", "benign", benign_feature(2)),
        row(5, "b5", "benign", benign_feature(3)),
        row(6, "m1", "malicious", malicious_feature(1)),
        row(7, "m2", "malicious", malicious_feature(1)),
        row(8, "m3", "malicious", malicious_feature(2)),
        row(9, "m4", "malicious", malicious_feature(3)),
    ]
    text = csv_text(IOT23_CANDIDATE_CSV_COLUMNS, rows)

    stream = io.StringIO(text)
    assignments, counts, group_counts, patterns, largest = (
        build_pilot_split(stream)
    )

    if stream.closed:
        raise RuntimeError("split function closed the caller's stream")
    if (
        len(assignments) != 9
        or patterns != 6
        or largest != 3
        or group_counts != {"train": 4, "validation": 2}
    ):
        raise RuntimeError("unexpected split summary")

    # 最大的正常组和最大的恶意组都必须完整留在训练侧。
    if any(assignments[name] != "train" for name in ("b1", "b2", "b3", "m1", "m2")):
        raise RuntimeError("largest feature group left the training set")

    if counts[("validation", "benign")] != 1 or counts[("validation", "malicious")] != 1:
        raise RuntimeError("validation must contain both labels")

    # 独立核对：完整相同的特征元组不能同时属于两侧。
    splits_by_features = {}
    for item in rows:
        sample_id = item[2]
        features = item[8:]
        splits_by_features.setdefault(features, set()).add(
            assignments[sample_id]
        )
    if any(len(splits) != 1 for splits in splits_by_features.values()):
        raise RuntimeError("identical features crossed the split")

    second_assignments = build_pilot_split(io.StringIO(text))[0]
    if assignments != second_assignments:
        raise RuntimeError("split is not deterministic")

    conflict = rows + [
        row(10, "conflict", "malicious", benign_feature(2))
    ]
    expect_value_error(
        build_pilot_split,
        csv_text(IOT23_CANDIDATE_CSV_COLUMNS, conflict),
        "conflicting labels",
    )

    duplicate_id = rows + [
        row(10, "b1", "benign", benign_feature(4))
    ]
    expect_value_error(
        build_pilot_split,
        csv_text(IOT23_CANDIDATE_CSV_COLUMNS, duplicate_id),
        "duplicate or empty sample ID",
    )

    expect_value_error(
        build_pilot_split,
        "wrong_header\n",
        "unexpected candidate CSV columns",
    )

    print("[PASS] IoT-23 pilot grouped split")
    return 0


if __name__ == "__main__":
    sys.exit(main())