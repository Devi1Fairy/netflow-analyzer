#!/usr/bin/env python3
"""用合成候选CSV验证TCP数据集加载器。"""

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


def expect_value_error(loader, text):
    stream = io.StringIO(text)

    try:
        loader(stream)
    except ValueError:
        pass
    else:
        raise RuntimeError("invalid candidate CSV was accepted")

    if stream.closed:
        raise RuntimeError("loader closed the caller's stream")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--scripts-dir",
        required=True,
        type=Path,
    )
    arguments = parser.parse_args()

    sys.path.insert(
        0,
        str(arguments.scripts_dir.resolve()),
    )

    from flow_sample_metadata import (
        SUPPORTED_FEATURE_SCHEMA_VERSION,
    )
    from iot23_sample_selection import (
        IOT23_CANDIDATE_CSV_COLUMNS,
        IOT23_CANDIDATE_SCHEMA_VERSION,
    )
    from tcp_candidate_dataset import (
        load_tcp_candidate_dataset,
    )

    def sample_id(character):
        return "flow_sample_id_v1:" + character * 64

    def row(
        identifier,
        capture_id,
        source_id,
        row_number,
        split,
        label,
        phase,
    ):
        return (
            IOT23_CANDIDATE_SCHEMA_VERSION,
            SUPPORTED_FEATURE_SCHEMA_VERSION,
            sample_id(identifier),
            capture_id,
            source_id,
            str(row_number),
            split,
            label,
            "6",
            "100",
            "2",
            "120",
            "140",
            "60",
            "70",
            "0.5",
            "0.25",
            "1",
            phase,
            "1",
        )

    rows = [
        row(
            "a",
            "capture-train",
            "source-train",
            1,
            "train",
            "benign",
            "established",
        ),
        row(
            "b",
            "capture-train",
            "source-train",
            2,
            "train",
            "malicious",
            "reset",
        ),
        row(
            "c",
            "capture-validation",
            "source-validation",
            1,
            "validation",
            "malicious",
            "syn-seen",
        ),
        row(
            "d",
            "capture-test",
            "source-test",
            1,
            "test",
            "benign",
            "closed",
        ),
    ]
    text = csv_text(
        IOT23_CANDIDATE_CSV_COLUMNS,
        rows,
    )

    stream = io.StringIO(text)
    dataset = load_tcp_candidate_dataset(stream)

    if stream.closed:
        raise RuntimeError("loader closed the caller's stream")

    if (
        len(dataset.train.features) != 2
        or dataset.train.labels != (0, 1)
        or dataset.train.sample_ids
        != (sample_id("a"), sample_id("b"))
        or len(dataset.validation.features) != 1
        or dataset.validation.labels != (1,)
        or len(dataset.test.features) != 1
        or dataset.test.labels != (0,)
    ):
        raise RuntimeError("unexpected dataset split")

    if (
        len(dataset.train.features[0]) != 18
        or dataset.train.features[0][9:]
        != (0, 0, 0, 1, 0, 0, 0, 0, 0)
    ):
        raise RuntimeError("unexpected encoded features")

    # 错误表头。
    expect_value_error(
        load_tcp_candidate_dataset,
        csv_text(
            IOT23_CANDIDATE_CSV_COLUMNS[:-1],
            [rows[0][:-1]],
        ),
    )

    # 重复样本ID。
    duplicate = list(rows)
    duplicate[2] = tuple(
        sample_id("a") if index == 2 else value
        for index, value in enumerate(duplicate[2])
    )
    expect_value_error(
        load_tcp_candidate_dataset,
        csv_text(
            IOT23_CANDIDATE_CSV_COLUMNS,
            duplicate,
        ),
    )

    # 同一原始来源跨训练和验证集合。
    crossed = list(rows)
    crossed_row = list(crossed[2])
    crossed_row[4] = "source-train"
    crossed[2] = tuple(crossed_row)
    expect_value_error(
        load_tcp_candidate_dataset,
        csv_text(
            IOT23_CANDIDATE_CSV_COLUMNS,
            crossed,
        ),
    )

    # 同一capture_id的特征行号必须严格递增。
    reversed_rows = list(rows)
    reversed_row = list(reversed_rows[1])
    reversed_row[5] = "1"
    reversed_rows[1] = tuple(reversed_row)
    expect_value_error(
        load_tcp_candidate_dataset,
        csv_text(
            IOT23_CANDIDATE_CSV_COLUMNS,
            reversed_rows,
        ),
    )

    # 缺少验证数据。
    expect_value_error(
        load_tcp_candidate_dataset,
        csv_text(
            IOT23_CANDIDATE_CSV_COLUMNS,
            rows[:2],
        ),
    )

    # 底层单行编码器的错误必须继续向上传递。
    non_tcp = list(rows)
    non_tcp_row = list(non_tcp[0])
    non_tcp_row[8] = "17"
    non_tcp[0] = tuple(non_tcp_row)
    expect_value_error(
        load_tcp_candidate_dataset,
        csv_text(
            IOT23_CANDIDATE_CSV_COLUMNS,
            non_tcp,
        ),
    )

    print("[PASS] TCP candidate dataset loading")
    return 0


if __name__ == "__main__":
    sys.exit(main())