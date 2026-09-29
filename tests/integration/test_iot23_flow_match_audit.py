#!/usr/bin/env python3

"""验证IoT-23流匹配审计CLI的成功与失败行为。"""

import argparse
import csv
import io
import subprocess
import sys
import tempfile
from pathlib import Path
from dataclasses import replace
from test_iot23_label_audit import build_record, write_log


def run_cli(
    script: Path,
    label_file: Path,
    flow_csv: Path,
    extra_args=(),
) -> subprocess.CompletedProcess:
    """启动独立进程，捕获退出码、stdout和stderr。"""

    command = [
        sys.executable,
        str(script),
        str(label_file),
        str(flow_csv),
    ]
    command.extend(extra_args)

    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        check=False,
        timeout=5,
    )


def make_csv_stream(columns, rows):
    """构造从表头开始的内存CSV，避免测试依赖外部数据文件。"""

    stream = io.StringIO()
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow(columns)
    writer.writerows(rows)
    stream.seek(0)
    return stream


def run_selection_contract_tests(
    collect_candidates,
    feature_columns,
    review_columns,
):
    """验证特征与审查记录的逐行连接及失败时拒绝行为。"""

    capture_id = "capture-a"
    feature_rows = (
        (
            "flow_features_v1", "6", "100", "2", "120", "120",
            "60", "60", "0", "0", "1", "established", "1",
        ),
        (
            "flow_features_v1", "17", "200", "1", "60", "60",
            "60", "60", "0", "0", "0", "not-applicable", "0",
        ),
    )
    review_rows = (
        (
            "iot23_flow_review_v1", "flow_features_v1", "1",
            "flow_sample_id_v1:" + "a" * 64, capture_id, "6",
            "192.0.2.1", "1234", "192.0.2.2", "80",
            "1000", "1100", "unique", "1", "malicious",
            "unique_unreused_candidate",
        ),
        (
            "iot23_flow_review_v1", "flow_features_v1", "2",
            "flow_sample_id_v1:" + "b" * 64, capture_id, "17",
            "198.51.100.1", "40000", "198.51.100.2", "53",
            "2000", "2200", "unique", "1", "benign",
            "unique_unreused_candidate",
        ),
    )

    def select(features, reviews, exact_rows):
        """每次用新的借用流运行，避免读指针位置影响下一个用例。"""

        with make_csv_stream(feature_columns, features) as feature_stream:
            with make_csv_stream(review_columns, reviews) as review_stream:
                selected = collect_candidates(
                    feature_stream,
                    review_stream,
                    capture_id,
                    exact_rows,
                )
                if feature_stream.closed or review_stream.closed:
                    raise RuntimeError("selector closed borrowed CSV streams")
                return selected

    def require_value_error(features, reviews, exact_rows, message):
        """错误输入必须抛错，而不是返回看似有效的部分样本。"""

        try:
            select(features, reviews, exact_rows)
        except ValueError as error:
            if message not in str(error):
                raise RuntimeError(
                    f"unexpected selection error: {error}"
                ) from error
        else:
            raise RuntimeError(
                f"selector accepted invalid input: {message}"
            )

    # 两行都是唯一未复用候选，但只有第1行的边界属于exact_rows。
    selected = select(feature_rows, review_rows, frozenset({1}))
    if (
        not isinstance(selected, tuple)
        or len(selected) != 1
        or selected[0].capture_id != capture_id
        or selected[0].feature_row_number != 1
        or selected[0].sample_id != review_rows[0][3]
        or selected[0].label_group != "malicious"
        or selected[0].feature_values != feature_rows[0][1:]
    ):
        raise RuntimeError(f"unexpected exact-row selection: {selected!r}")

    if select(feature_rows, review_rows, frozenset()) != ():
        raise RuntimeError("non-exact candidates were selected")

    wrong_protocol = list(feature_rows[0])
    wrong_protocol[1] = "17"
    require_value_error(
        (tuple(wrong_protocol), feature_rows[1]),
        review_rows,
        frozenset({1}),
        "feature and review differ at row 1",
    )

    # zip_longest必须发现review缺少末行；相同长度的错位也必须发现。
    require_value_error(
        feature_rows,
        review_rows[:1],
        frozenset({1}),
        "feature and review row counts differ at 2",
    )

    wrong_row_number = list(review_rows[1])
    wrong_row_number[2] = "3"
    require_value_error(
        feature_rows,
        (review_rows[0], tuple(wrong_row_number)),
        frozenset({1}),
        "review identity differs at row 2",
    )

    # 即使某行号被错误加入exact集合，非唯一审查状态也不能通过。
    not_unique = list(review_rows[1])
    not_unique[12] = "ambiguous_same_label"
    not_unique[13] = "2"
    not_unique[15] = "not_unique"
    require_value_error(
        feature_rows,
        (review_rows[0], tuple(not_unique)),
        frozenset({1, 2}),
        "exact row is not an unreused candidate: 2",
    )

    require_value_error(
        feature_rows,
        review_rows,
        frozenset({1, 3}),
        "some exact row numbers were not found",
    )

def run_split_binding_tests(
    candidate_type,
    assign_candidates,
    feature_columns,
):
    """验证来源绑定和失败拒绝，不依赖真实数据集。"""

    values = tuple("0" for _ in feature_columns[1:])
    first = candidate_type(
        "capture-a", 1, "flow_sample_id_v1:" + "a" * 64,
        "malicious", values,
    )
    second = replace(
        first,
        capture_id="capture-b",
        sample_id="flow_sample_id_v1:" + "b" * 64,
        label_group="benign",
    )
    candidates = {
        "capture-a": (first,),
        "capture-b": (second,),
    }
    assignments = {
        "capture-a": ("source-a", "train"),
        "capture-b": ("source-b", "validation"),
    }

    bound = assign_candidates(candidates, assignments)
    actual = tuple(
        (
            item.candidate.sample_id,
            item.source_capture_id,
            item.split,
            item.candidate.feature_values,
        )
        for item in bound
    )
    expected = (
        (first.sample_id, "source-a", "train", values),
        (second.sample_id, "source-b", "validation", values),
    )
    if actual != expected:
        raise RuntimeError(f"unexpected split binding: {actual!r}")

    def require_rejected(candidate_map, assignment_map):
        try:
            assign_candidates(candidate_map, assignment_map)
        except ValueError:
            return
        raise RuntimeError("invalid split binding was accepted")

    # 候选与清单覆盖的抓包不一致。
    require_rejected({"capture-a": (first,)}, assignments)

    # 同一原始PCAP不能同时出现在训练和验证。
    require_rejected(
        candidates,
        {
            "capture-a": ("same-source", "train"),
            "capture-b": ("same-source", "validation"),
        },
    )

    # 不允许同一个样本身份重复进入结果。
    require_rejected(
        {
            "capture-a": (first,),
            "capture-b": (replace(second, sample_id=first.sample_id),),
        },
        assignments,
    )

    # 清单有验证来源，但筛选后没有验证样本也不能继续。
    require_rejected(
        {"capture-a": (first,), "capture-b": ()},
        assignments,
    )

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    arguments = parser.parse_args()

    if not arguments.script.is_file():
        raise RuntimeError("audit CLI script does not exist")

    if not arguments.work_dir.is_dir():
        raise RuntimeError("test work directory does not exist")

    # C流CSV格式由数据集无关的flow_csv_identity模块定义；
    # 本测试只验证CLI行为，不依赖CTU-13审计器。
    sys.path.insert(0, str(arguments.script.parent.resolve()))
    from audit_iot23_flow_matches import (
        audit_candidate_boundaries_with_exact_rows,
    )
    from iot23_label_index import load_label_index
    from flow_csv_identity  import EXPECTED_FLOW_COLUMNS
    from flow_feature_alignment import EXPECTED_FEATURE_COLUMNS
    from iot23_flow_review import IOT23_REVIEW_CSV_COLUMNS
    from iot23_sample_selection import (
        ExactReviewedCandidate,
        assign_candidates_to_splits,
        collect_exact_reviewed_candidates,
    )

    run_selection_contract_tests(
        collect_exact_reviewed_candidates,
        EXPECTED_FEATURE_COLUMNS,
        IOT23_REVIEW_CSV_COLUMNS,
    )

    run_split_binding_tests(
        ExactReviewedCandidate,
        assign_candidates_to_splits,
        EXPECTED_FEATURE_COLUMNS,
    )

    with tempfile.TemporaryDirectory(
        prefix="iot23-flow-match-",
        dir=arguments.work_dir,
    ) as temporary_directory:
        work_dir = Path(temporary_directory)
        label_file = work_dir / "labels.log"
        good_flow_csv = work_dir / "good-flows.csv"
        reused_flow_csv = work_dir / "reused-flows.csv"
        bad_flow_csv = work_dir / "bad-flows.csv"

        # build_record的默认五元组和时间是固定的。
        write_log(
            label_file,
            [build_record("tcp", "Malicious", "Attack")],
        )

        flow_row = (
            6,
            "established",
            "192.168.2.5",
            40000,
            "198.51.100.20",
            80,
            1, 60, 60,
            1, 60, 60,
            1526756261, 0,
            1526756261, 500000,
        )

        with good_flow_csv.open(
            "w",
            encoding="utf-8",
            newline="",
        ) as output_stream:
            writer = csv.writer(
                output_stream,
                lineterminator="\n",
            )
            writer.writerow(EXPECTED_FLOW_COLUMNS)
            writer.writerow(flow_row)

        success = run_cli(
            arguments.script,
            label_file,
            good_flow_csv,
        )

        expected_stdout = (
            "flows_total=1\n"
            "matches_unique=1\n"
            "matches_unique_malicious=1\n"
            "matches_unique_benign=0\n"
            "matches_unmatched=0\n"
            "matches_ambiguous_same_label=0\n"
            "matches_ambiguous_conflicting_labels=0\n"
            "unique_label_records=1\n"
            "reused_label_records=0\n"
            "duplicate_unique_assignments=0\n"
            "matches_unique_unreused=1\n"
            "matches_unique_reused=0\n"
        )

        boundary_result = run_cli(
            arguments.script,
            label_file,
            good_flow_csv,
            extra_args=("--boundary-summary",),
        )
        expected_boundary_stdout = expected_stdout + (
            "candidate_boundary_total=1\n"
            "candidate_boundary_exact=0\n"
            "candidate_boundary_flow_inside_label=1\n"
            "candidate_boundary_label_inside_flow=0\n"
            "candidate_boundary_partial_overlap=0\n"
            "candidate_boundary_disjoint=0\n"
        )

        if (
            boundary_result.returncode != 0
            or boundary_result.stdout != expected_boundary_stdout
            or boundary_result.stderr
        ):
            raise RuntimeError(
                f"unexpected boundary CLI result: {boundary_result!r}"
            )

        index = load_label_index(label_file)

        # 原有C流只落在Zeek标签内部：是唯一候选，但不是exact。
        with good_flow_csv.open(
            "r", encoding="utf-8", newline=""
        ) as input_stream:
            counts, boundaries, exact_rows = (
                audit_candidate_boundaries_with_exact_rows(
                    index, input_stream
                )
            )

        if (
            counts["matches_unique_unreused"] != 1
            or boundaries["flow_inside_label"] != 1
            or exact_rows != frozenset()
        ):
            raise RuntimeError("non-exact candidate was selected")

        # 标签时间为1526756261.000000～1526756262.000000；
        # 让C流首末时间完全一致，数据行1才应进入集合。
        exact_flow_csv = work_dir / "exact-flows.csv"
        with exact_flow_csv.open(
            "w", encoding="utf-8", newline=""
        ) as output_stream:
            writer = csv.writer(output_stream, lineterminator="\n")
            writer.writerow(EXPECTED_FLOW_COLUMNS)
            writer.writerow(
                flow_row[:-2] + (1526756262, 0)
            )

        with exact_flow_csv.open(
            "r", encoding="utf-8", newline=""
        ) as input_stream:
            counts, boundaries, exact_rows = (
                audit_candidate_boundaries_with_exact_rows(
                    index, input_stream
                )
            )

        if (
            counts["matches_unique_unreused"] != 1
            or boundaries["exact"] != 1
            or exact_rows != frozenset({1})
        ):
            raise RuntimeError("exact candidate row was not selected")

        if (
            success.returncode != 0
            or success.stdout != expected_stdout
            or success.stderr
        ):
            raise RuntimeError(
                "unexpected successful CLI result: "
                f"{success!r}"
            )

        # 复制已有TCP流，只改变首末时间；两条C流仍与同一条
        # 1526756261.0～1526756262.0的Zeek标签记录相交。
        second_flow_row = list(flow_row)
        second_flow_row[13] = 100000
        second_flow_row[15] = 300000

        good_feature_csv = work_dir / "good-features.csv"
        bad_feature_csv = work_dir / "bad-features.csv"
        review_output = work_dir / "review.csv"

        with good_feature_csv.open(
            "w",
            encoding="utf-8",
            newline="",
        ) as output_stream:
            writer = csv.writer(
                output_stream,
                lineterminator="\n",
            )
            writer.writerow(EXPECTED_FEATURE_COLUMNS)
            writer.writerow(
                (
                    "flow_features_v1",
                    6,
                    500000,
                    2,
                    120,
                    120,
                    60,
                    60,
                    0,
                    0,
                    1,
                    "established",
                    1,
                )
            )

        review_args = (
            "--feature-csv",
            str(good_feature_csv),
            "--capture-id",
            "iot23-scenario-3-1",
            "--review-output",
            str(review_output),
        )

        mixed_review_output = work_dir / "mixed-review.csv"
        mixed_result = run_cli(
            arguments.script,
            label_file,
            good_flow_csv,
            extra_args=(
                "--feature-csv",
                str(good_feature_csv),
                "--capture-id",
                "iot23-scenario-3-1",
                "--review-output",
                str(mixed_review_output),
                "--boundary-summary",
            ),
        )

        if (
            mixed_result.returncode == 0
            or mixed_result.stdout
            or mixed_review_output.exists()
            or "cannot be combined" not in mixed_result.stderr
        ):
            raise RuntimeError(
                f"mixed options were not rejected safely: "
                f"{mixed_result!r}"
            )

        review_success = run_cli(
            arguments.script,
            label_file,
            good_flow_csv,
            extra_args=review_args,
        )

        if (
            review_success.returncode != 0
            or review_success.stdout != expected_stdout
            or review_success.stderr
        ):
            raise RuntimeError(
                "unexpected review CLI result: "
                f"{review_success!r}"
            )

        with review_output.open(
            "r",
            encoding="utf-8",
            newline="",
        ) as input_stream:
            review_rows = list(csv.DictReader(input_stream))

        if (
            len(review_rows) != 1
            or review_rows[0]["feature_row_number"] != "1"
            or review_rows[0]["match_status"] != "unique"
            or review_rows[0]["review_status"]
            != "unique_unreused_candidate"
            or review_rows[0]["label_group"] != "malicious"
        ):
            raise RuntimeError(
                f"unexpected review sidecar: {review_rows!r}"
            )

        # 已有文件绝不能被覆盖，也不能在失败清理时被删除。
        original_review = review_output.read_bytes()

        existing_result = run_cli(
            arguments.script,
            label_file,
            good_flow_csv,
            extra_args=review_args,
        )

        if (
            existing_result.returncode == 0
            or existing_result.stdout
            or review_output.read_bytes() != original_review
        ):
            raise RuntimeError(
                "existing review file was not preserved"
            )

        # 同样只有一行，但协议不匹配：预检应在创建前失败。
        bad_feature_csv.write_text(
            good_feature_csv.read_text(
                encoding="utf-8"
            ).replace(
                "flow_features_v1,6,",
                "flow_features_v1,17,",
                1,
            ),
            encoding="utf-8",
        )

        bad_review_output = work_dir / "bad-review.csv"

        mismatch_result = run_cli(
            arguments.script,
            label_file,
            good_flow_csv,
            extra_args=(
                "--feature-csv",
                str(bad_feature_csv),
                "--capture-id",
                "iot23-scenario-3-1",
                "--review-output",
                str(bad_review_output),
            ),
        )

        if (
            mismatch_result.returncode == 0
            or mismatch_result.stdout
            or bad_review_output.exists()
        ):
            raise RuntimeError(
                "misaligned feature CSV created a review file"
            )

        # 预检通过、输出文件已创建，但非法capture-id使写出失败；
        # CLI必须删除本次创建的不完整文件。
        invalid_id_output = work_dir / "invalid-id-review.csv"

        invalid_id_result = run_cli(
            arguments.script,
            label_file,
            good_flow_csv,
            extra_args=(
                "--feature-csv",
                str(good_feature_csv),
                "--capture-id",
                "invalid capture id",
                "--review-output",
                str(invalid_id_output),
            ),
        )

        if (
            invalid_id_result.returncode == 0
            or invalid_id_result.stdout
            or invalid_id_output.exists()
        ):
            raise RuntimeError(
                "failed review output was not cleaned up"
            )

        with reused_flow_csv.open(
            "w",
            encoding="utf-8",
            newline="",
        ) as output_stream:
            writer = csv.writer(
                output_stream,
                lineterminator="\n",
            )
            writer.writerow(EXPECTED_FLOW_COLUMNS)
            writer.writerow(flow_row)
            writer.writerow(second_flow_row)

        reuse_result = run_cli(
            arguments.script,
            label_file,
            reused_flow_csv,
        )

        expected_reuse_stdout = (
            "flows_total=2\n"
            "matches_unique=2\n"
            "matches_unique_malicious=2\n"
            "matches_unique_benign=0\n"
            "matches_unmatched=0\n"
            "matches_ambiguous_same_label=0\n"
            "matches_ambiguous_conflicting_labels=0\n"
            "unique_label_records=1\n"
            "reused_label_records=1\n"
            "duplicate_unique_assignments=1\n"
            "matches_unique_unreused=0\n"
            "matches_unique_reused=2\n"
        )

        if (
            reuse_result.returncode != 0
            or reuse_result.stdout != expected_reuse_stdout
            or reuse_result.stderr
        ):
            raise RuntimeError(
                "unexpected reused-label CLI result: "
                f"{reuse_result!r}"
            )

        # 第一行有效，第二行字段不足：验证处理到一半失败时，
        # CLI不会向stdout留下看似完整的统计摘要。
        with bad_flow_csv.open(
            "w",
            encoding="utf-8",
            newline="",
        ) as output_stream:
            writer = csv.writer(
                output_stream,
                lineterminator="\n",
            )
            writer.writerow(EXPECTED_FLOW_COLUMNS)
            writer.writerow(flow_row)
            writer.writerow((6,))

        failure = run_cli(
            arguments.script,
            label_file,
            bad_flow_csv,
        )

        if (
            failure.returncode == 0
            or failure.stdout
            or "missing fields" not in failure.stderr
        ):
            raise RuntimeError(
                "malformed CSV was not rejected cleanly: "
                f"{failure!r}"
            )

        boundary_failure = run_cli(
            arguments.script,
            label_file,
            bad_flow_csv,
            extra_args=("--boundary-summary",),
        )
        if (
            boundary_failure.returncode == 0
            or boundary_failure.stdout
            or "missing fields" not in boundary_failure.stderr
        ):
            raise RuntimeError(
                f"boundary audit did not fail cleanly: "
                f"{boundary_failure!r}"
            )

    print("[PASS] IoT-23 flow audit CLI tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
