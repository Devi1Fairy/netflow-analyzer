"""读取并验证TCP候选CSV，按数据集合组织已编码样本。"""

import csv
from dataclasses import dataclass
from typing import TextIO, Tuple

from flow_csv_identity import (
    parse_unsigned_integer,
    validate_row_shape,
)
from flow_sample_metadata import (
    CAPTURE_ID_PATTERN,
    SAMPLE_ID_SCHEMA_VERSION,
)
from iot23_sample_selection import (
    IOT23_CANDIDATE_CSV_COLUMNS,
    IOT23_CANDIDATE_SCHEMA_VERSION,
)
from tcp_model_input import (
    MODEL_FEATURE_NAMES,
    encode_tcp_candidate,
)
from validate_ml_split_manifest import SPLITS


MAX_UNSIGNED_64 = (1 << 64) - 1


@dataclass(frozen=True)
class EncodedTcpSplit:
    """一个集合内顺序一致的特征、标签和可追踪样本ID。"""

    features: Tuple[Tuple[float, ...], ...]
    labels: Tuple[int, ...]
    sample_ids: Tuple[str, ...]


@dataclass(frozen=True)
class EncodedTcpDataset:
    """候选文件中的训练、验证和可选测试集合。"""

    train: EncodedTcpSplit
    validation: EncodedTcpSplit
    test: EncodedTcpSplit


def load_tcp_candidate_dataset(
    input_stream: TextIO,
) -> EncodedTcpDataset:
    """读取借用的CSV流并返回完整数据集；不关闭输入流。

    全部记录通过结构、来源和编码检查后才返回，错误时抛出
    ValueError，不返回部分数据。至少需要一条训练和验证记录。
    """

    reader = csv.DictReader(input_stream)
    if tuple(reader.fieldnames or ()) != IOT23_CANDIDATE_CSV_COLUMNS:
        raise ValueError("unexpected candidate CSV columns")

    builders = {
        split: {
            "features": [],
            "labels": [],
            "sample_ids": [],
        }
        for split in SPLITS
    }
    seen_sample_ids = set()
    source_splits = {}
    capture_assignments = {}
    previous_rows = {}
    sample_id_prefix = f"{SAMPLE_ID_SCHEMA_VERSION}:"

    def text(row, name, line_number):
        """取得必需的非空字符串字段。"""

        value = row.get(name)
        if not isinstance(value, str) or not value:
            raise ValueError(
                f"candidate CSV line {line_number}: "
                f"missing or invalid {name}"
            )
        return value

    for row in reader:
        line_number = reader.line_num

        validate_row_shape(
            row,
            IOT23_CANDIDATE_CSV_COLUMNS,
            "candidate CSV",
            line_number,
        )

        if text(
            row,
            "candidate_schema_version",
            line_number,
        ) != IOT23_CANDIDATE_SCHEMA_VERSION:
            raise ValueError(
                f"candidate CSV line {line_number}: "
                "unsupported candidate schema"
            )

        sample_id = text(row, "sample_id", line_number)
        capture_id = text(row, "capture_id", line_number)
        source_id = text(
            row,
            "source_capture_id",
            line_number,
        )
        split = text(row, "split", line_number)

        digest = sample_id[len(sample_id_prefix):]
        if (
            not sample_id.startswith(sample_id_prefix)
            or len(digest) != 64
            or any(
                char not in "0123456789abcdef"
                for char in digest
            )
        ):
            raise ValueError(
                f"candidate CSV line {line_number}: "
                "invalid sample_id"
            )

        if sample_id in seen_sample_ids:
            raise ValueError(
                f"candidate CSV line {line_number}: "
                "duplicate sample_id"
            )

        if (
            CAPTURE_ID_PATTERN.fullmatch(capture_id) is None
            or CAPTURE_ID_PATTERN.fullmatch(source_id) is None
        ):
            raise ValueError(
                f"candidate CSV line {line_number}: "
                "invalid capture ID"
            )

        if split not in builders:
            raise ValueError(
                f"candidate CSV line {line_number}: "
                "invalid split"
            )

        feature_row_number = parse_unsigned_integer(
            text(
                row,
                "feature_row_number",
                line_number,
            ),
            "feature_row_number",
            MAX_UNSIGNED_64,
        )

        previous = previous_rows.get(capture_id, 0)
        if feature_row_number <= previous:
            raise ValueError(
                f"candidate CSV line {line_number}: "
                "feature row order is invalid"
            )

        assignment = (source_id, split)
        previous_assignment = capture_assignments.get(
            capture_id
        )
        if (
            previous_assignment is not None
            and previous_assignment != assignment
        ):
            raise ValueError(
                f"candidate CSV line {line_number}: "
                "capture assignment changed"
            )

        previous_split = source_splits.get(source_id)
        if (
            previous_split is not None
            and previous_split != split
        ):
            raise ValueError(
                f"candidate CSV line {line_number}: "
                "source capture crosses splits"
            )

        features, label = encode_tcp_candidate(row)

        seen_sample_ids.add(sample_id)
        previous_rows[capture_id] = feature_row_number
        capture_assignments[capture_id] = assignment
        source_splits[source_id] = split

        builders[split]["features"].append(features)
        builders[split]["labels"].append(label)
        builders[split]["sample_ids"].append(sample_id)

    if (
        not builders["train"]["labels"]
        or not builders["validation"]["labels"]
    ):
        raise ValueError(
            "candidate CSV needs train and validation samples"
        )

    def freeze(split):
        """把构建期间的可变列表转换成不可变结果。"""

        builder = builders[split]
        result = EncodedTcpSplit(
            features=tuple(builder["features"]),
            labels=tuple(builder["labels"]),
            sample_ids=tuple(builder["sample_ids"]),
        )

        if (
            len(result.features) != len(result.labels)
            or len(result.labels) != len(result.sample_ids)
            or any(
                len(values) != len(MODEL_FEATURE_NAMES)
                for values in result.features
            )
        ):
            raise ValueError("encoded split lengths differ")

        return result

    return EncodedTcpDataset(
        train=freeze("train"),
        validation=freeze("validation"),
        test=freeze("test"),
    )