#!/usr/bin/env python3

"""按同一数据行号连接特征与IoT-23审查记录；不训练模型。"""

import csv
from dataclasses import dataclass
from itertools import zip_longest
from typing import FrozenSet, Mapping, TextIO, Tuple
from validate_ml_split_manifest import SPLITS

from flow_csv_identity import validate_row_shape
from flow_feature_alignment import EXPECTED_FEATURE_COLUMNS
from flow_sample_metadata import (
    CAPTURE_ID_PATTERN,
    SAMPLE_ID_SCHEMA_VERSION,
    SUPPORTED_FEATURE_SCHEMA_VERSION,
    SUPPORTED_LABEL_GROUPS,
)
from iot23_flow_review import (
    IOT23_REVIEW_CSV_COLUMNS,
    IOT23_REVIEW_SCHEMA_VERSION,
    REVIEW_NOT_UNIQUE,
    REVIEW_UNIQUE_REUSED,
    REVIEW_UNIQUE_UNREUSED_CANDIDATE,
)


@dataclass(frozen=True)
class ExactReviewedCandidate:
    """保留来源与标签，但模型特征只放在feature_values中。"""

    capture_id: str
    feature_row_number: int
    sample_id: str
    label_group: str
    feature_values: Tuple[str, ...]

@dataclass(frozen=True)
class SplitAssignedCandidate:
    """候选样本及其来源切分；身份字段不属于模型特征。"""

    candidate: ExactReviewedCandidate
    source_capture_id: str
    split: str

IOT23_CANDIDATE_SCHEMA_VERSION = "iot23_candidate_samples_v1"

IOT23_CANDIDATE_CSV_COLUMNS = (
    "candidate_schema_version",
    "feature_schema_version",
    "sample_id",
    "capture_id",
    "source_capture_id",
    "feature_row_number",
    "split",
    "candidate_label_group",
) + EXPECTED_FEATURE_COLUMNS[1:]

def assign_candidates_to_splits(
    candidates_by_capture: Mapping[
        str, Tuple[ExactReviewedCandidate, ...]
    ],
    assignments: Mapping[str, Tuple[str, str]],
) -> Tuple[SplitAssignedCandidate, ...]:
    """绑定已筛选候选与已验证清单；错误时不返回部分结果。"""

    if set(candidates_by_capture) != set(assignments):
        raise ValueError("candidate and manifest capture IDs differ")

    result = []
    seen_sample_ids = set()
    source_splits = {}
    split_counts = {split: 0 for split in SPLITS}

    # 排序让结果不依赖字典的插入顺序。
    for capture_id in sorted(assignments):
        source_id, split = assignments[capture_id]

        if (
            CAPTURE_ID_PATTERN.fullmatch(capture_id) is None
            or CAPTURE_ID_PATTERN.fullmatch(source_id) is None
            or split not in split_counts
        ):
            raise ValueError("invalid split assignment")

        previous_split = source_splits.get(source_id)
        if previous_split is not None and previous_split != split:
            raise ValueError("source capture crosses splits")
        source_splits[source_id] = split

        previous_row_number = 0
        for candidate in candidates_by_capture[capture_id]:
            if (
                not isinstance(candidate, ExactReviewedCandidate)
                or candidate.capture_id != capture_id
                or type(candidate.feature_row_number) is not int
                or candidate.feature_row_number <= previous_row_number
            ):
                raise ValueError("candidate capture or row order is invalid")

            if candidate.sample_id in seen_sample_ids:
                raise ValueError("duplicate sample_id")
            if candidate.label_group not in SUPPORTED_LABEL_GROUPS:
                raise ValueError("invalid candidate label")
            if (
                not isinstance(candidate.feature_values, tuple)
                or len(candidate.feature_values)
                != len(EXPECTED_FEATURE_COLUMNS) - 1
            ):
                raise ValueError("invalid candidate feature count")

            seen_sample_ids.add(candidate.sample_id)
            previous_row_number = candidate.feature_row_number
            result.append(
                SplitAssignedCandidate(candidate, source_id, split)
            )
            split_counts[split] += 1

    if split_counts["train"] == 0 or split_counts["validation"] == 0:
        raise ValueError("train and validation need selected candidates")

    return tuple(result)

def write_iot23_candidate_csv(
    output_stream: TextIO,
    samples: Tuple[SplitAssignedCandidate, ...],
) -> int:
    """先验证所有候选，再写入借用流；返回数据行数。"""

    if not isinstance(samples, tuple):
        raise TypeError("samples must be a tuple")

    rows = []
    seen_sample_ids = set()
    source_splits = {}
    split_counts = {split: 0 for split in SPLITS}

    for item in samples:
        if not isinstance(item, SplitAssignedCandidate):
            raise ValueError("invalid assigned candidate")

        candidate = item.candidate
        if (
            not isinstance(candidate, ExactReviewedCandidate)
            or item.split not in split_counts
            or CAPTURE_ID_PATTERN.fullmatch(
                item.source_capture_id
            ) is None
            or CAPTURE_ID_PATTERN.fullmatch(
                candidate.capture_id
            ) is None
            or type(candidate.feature_row_number) is not int
            or candidate.feature_row_number < 1
            or candidate.label_group not in SUPPORTED_LABEL_GROUPS
            or not isinstance(candidate.feature_values, tuple)
            or len(candidate.feature_values)
            != len(EXPECTED_FEATURE_COLUMNS) - 1
            or any(
                not isinstance(value, str)
                for value in candidate.feature_values
            )
        ):
            raise ValueError("invalid candidate row")

        if candidate.sample_id in seen_sample_ids:
            raise ValueError("duplicate sample_id")

        previous_split = source_splits.get(item.source_capture_id)
        if previous_split is not None and previous_split != item.split:
            raise ValueError("source capture crosses splits")

        seen_sample_ids.add(candidate.sample_id)
        source_splits[item.source_capture_id] = item.split
        split_counts[item.split] += 1

        rows.append(
            (
                IOT23_CANDIDATE_SCHEMA_VERSION,
                SUPPORTED_FEATURE_SCHEMA_VERSION,
                candidate.sample_id,
                candidate.capture_id,
                item.source_capture_id,
                candidate.feature_row_number,
                item.split,
                candidate.label_group,
            ) + candidate.feature_values
        )

    if split_counts["train"] == 0 or split_counts["validation"] == 0:
        raise ValueError("train and validation need selected candidates")

    # 到这里才开始写：上述校验失败时，输出流保持空白。
    writer = csv.writer(output_stream, lineterminator="\n")
    writer.writerow(IOT23_CANDIDATE_CSV_COLUMNS)
    writer.writerows(rows)
    return len(rows)

def collect_exact_reviewed_candidates(
    feature_stream: TextIO,
    review_stream: TextIO,
    capture_id: str,
    exact_rows: FrozenSet[int],
) -> Tuple[ExactReviewedCandidate, ...]:
    """
    读取调用者借用的两个文本流，返回通过检查的候选元组。

    exact_rows必须来自同一份流CSV的边界审计。函数不关闭输入流，
    不修改输入文件；任一行有问题就抛出ValueError，不返回部分结果。
    """

    if feature_stream is review_stream:
        raise ValueError("feature and review streams must differ")

    if CAPTURE_ID_PATTERN.fullmatch(capture_id) is None:
        raise ValueError("invalid capture_id")

    if (
        not isinstance(exact_rows, frozenset)
        or any(
            isinstance(number, bool)
            or not isinstance(number, int)
            or number < 1
            for number in exact_rows
        )
    ):
        raise ValueError("invalid exact row set")

    feature_reader = csv.DictReader(feature_stream)
    review_reader = csv.DictReader(review_stream)

    if tuple(feature_reader.fieldnames or ()) != EXPECTED_FEATURE_COLUMNS:
        raise ValueError("unexpected feature CSV columns")

    if tuple(review_reader.fieldnames or ()) != IOT23_REVIEW_CSV_COLUMNS:
        raise ValueError("unexpected review CSV columns")

    selected = []
    seen_sample_ids = set()
    row_count = 0
    sample_id_prefix = f"{SAMPLE_ID_SCHEMA_VERSION}:"

    # zip_longest能发现任意一侧提前结束；zip会悄悄丢弃较长文件的尾部。
    for row_count, (feature, review) in enumerate(
        zip_longest(feature_reader, review_reader),
        start=1,
    ):
        if feature is None or review is None:
            raise ValueError(
                f"feature and review row counts differ at {row_count}"
            )

        validate_row_shape(
            feature,
            EXPECTED_FEATURE_COLUMNS,
            "feature CSV",
            feature_reader.line_num,
        )
        validate_row_shape(
            review,
            IOT23_REVIEW_CSV_COLUMNS,
            "review CSV",
            review_reader.line_num,
        )

        if (
            feature["schema_version"]
            != SUPPORTED_FEATURE_SCHEMA_VERSION
            or review["review_schema_version"]
            != IOT23_REVIEW_SCHEMA_VERSION
            or review["feature_schema_version"]
            != SUPPORTED_FEATURE_SCHEMA_VERSION
        ):
            raise ValueError(f"unsupported schema at row {row_count}")

        if (
            review["feature_row_number"] != str(row_count)
            or review["capture_id"] != capture_id
        ):
            raise ValueError(f"review identity differs at row {row_count}")

        sample_id = review["sample_id"]
        digest = sample_id[len(sample_id_prefix):]
        if (
            not sample_id.startswith(sample_id_prefix)
            or len(digest) != 64
            or any(char not in "0123456789abcdef" for char in digest)
            or sample_id in seen_sample_ids
        ):
            raise ValueError(f"invalid sample_id at row {row_count}")
        seen_sample_ids.add(sample_id)

        first_seen = int(review["first_seen_unix_microseconds"])
        last_seen = int(review["last_seen_unix_microseconds"])
        duration = int(feature["duration_microseconds"])

        if (
            first_seen < 0
            or last_seen < first_seen
            or duration != last_seen - first_seen
            or feature["protocol"] != review["protocol"]
        ):
            raise ValueError(f"feature and review differ at row {row_count}")

        status = review["review_status"]
        match_status = review["match_status"]

        if status in (
            REVIEW_UNIQUE_UNREUSED_CANDIDATE,
            REVIEW_UNIQUE_REUSED,
        ):
            if (
                match_status != "unique"
                or review["candidate_count"] != "1"
                or review["label_group"] not in SUPPORTED_LABEL_GROUPS
            ):
                raise ValueError(f"invalid unique review at row {row_count}")
        elif status == REVIEW_NOT_UNIQUE:
            if match_status == "unique":
                raise ValueError(f"invalid non-unique review at row {row_count}")
        else:
            raise ValueError(f"unknown review status at row {row_count}")

        if row_count in exact_rows:
            if status != REVIEW_UNIQUE_UNREUSED_CANDIDATE:
                raise ValueError(
                    f"exact row is not an unreused candidate: {row_count}"
                )

            selected.append(
                ExactReviewedCandidate(
                    capture_id=capture_id,
                    feature_row_number=row_count,
                    sample_id=sample_id,
                    label_group=review["label_group"],
                    # 版本字段不是模型数值特征；身份字段也不混进来。
                    feature_values=tuple(
                        feature[column]
                        for column in EXPECTED_FEATURE_COLUMNS[1:]
                    ),
                )
            )

    if row_count == 0:
        raise ValueError("feature and review CSV contain no records")

    # 同时发现exact_rows含有超出文件末尾的行号。
    if len(selected) != len(exact_rows):
        raise ValueError("some exact row numbers were not found")

    return tuple(selected)