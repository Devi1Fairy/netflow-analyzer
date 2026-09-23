#!/usr/bin/env python3

"""验证稳定流样本ID的数据契约。"""

import io
import argparse
import sys
from dataclasses import FrozenInstanceError
from pathlib import Path
from typing import Callable, Type

EXPECTED_SAMPLE_ID = (
    "flow_sample_id_v1:"
    "65df234cbaff0e371d77c66e18d3d2b1"
    "f598decb509e3cf3c08158b5687da678"
)


def parse_arguments() -> argparse.Namespace:
    """解析CMake传入的scripts目录。"""

    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--scripts-dir",
        required=True,
        type=Path,
    )

    return parser.parse_args()


def require(
    condition: bool,
    message: str,
) -> None:
    """要求condition为真，否则使测试失败。"""

    if not condition:
        raise RuntimeError(message)


def require_exception(
    expected_exception: Type[BaseException],
    operation: Callable[[], object],
    message: str,
) -> None:
    """要求operation抛出指定类型的异常。"""

    try:
        operation()
    except expected_exception:
        return

    raise RuntimeError(message)


def run_tests(scripts_dir: Path) -> None:
    """执行样本ID的确定性和输入边界测试。"""

    if not scripts_dir.is_dir():
        raise RuntimeError(
            f"scripts directory does not exist: {scripts_dir}"
        )

    # 将仓库的scripts目录临时加入模块搜索路径，
    # 让测试能够导入尚未安装成Python包的项目模块。
    sys.path.insert(0, str(scripts_dir))

    from flow_sample_metadata import (
        LABEL_GROUP_BENIGN,
        LABEL_GROUP_MALICIOUS,
        MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS,
        MATCH_STATUS_AMBIGUOUS_SAME_LABEL,
        MATCH_STATUS_UNIQUE,
        MATCH_STATUS_UNMATCHED,
        SAMPLE_METADATA_SCHEMA_VERSION,
        SUPPORTED_FEATURE_SCHEMA_VERSION,
        FlowSampleIdentity,
        FlowSampleMetadata,
        build_sample_id,
        build_sample_metadata,
        write_sample_metadata_csv_header,
        write_sample_metadata_csv_record,
    )

    identity = FlowSampleIdentity(
        capture_id="ctu13-scenario-7",
        protocol=6,
        endpoint_a_ipv4=3221225994,
        endpoint_a_port=55000,
        endpoint_b_ipv4=3325256724,
        endpoint_b_port=443,
        first_seen_microseconds=1313495484049047,
        last_seen_microseconds=1313495485299048,
    )

    sample_id = build_sample_id(identity)

    # 不仅检查格式，还锁定一组已知输入的精确摘要。
    # 字段顺序、JSON格式或模式版本意外变化时，本测试会失败。
    require(
        sample_id == EXPECTED_SAMPLE_ID,
        f"unexpected sample ID: {sample_id}",
    )

    # 相同身份重复计算时必须得到完全相同的结果。
    require(
        build_sample_id(identity) == sample_id,
        "sample ID is not deterministic",
    )

    changed_capture = FlowSampleIdentity(
        capture_id="ctu13-scenario-8",
        protocol=identity.protocol,
        endpoint_a_ipv4=identity.endpoint_a_ipv4,
        endpoint_a_port=identity.endpoint_a_port,
        endpoint_b_ipv4=identity.endpoint_b_ipv4,
        endpoint_b_port=identity.endpoint_b_port,
        first_seen_microseconds=identity.first_seen_microseconds,
        last_seen_microseconds=identity.last_seen_microseconds,
    )

    require(
        build_sample_id(changed_capture) != sample_id,
        "capture_id does not affect sample identity",
    )

    reversed_endpoints = FlowSampleIdentity(
        capture_id=identity.capture_id,
        protocol=identity.protocol,
        endpoint_a_ipv4=identity.endpoint_b_ipv4,
        endpoint_a_port=identity.endpoint_b_port,
        endpoint_b_ipv4=identity.endpoint_a_ipv4,
        endpoint_b_port=identity.endpoint_a_port,
        first_seen_microseconds=identity.first_seen_microseconds,
        last_seen_microseconds=identity.last_seen_microseconds,
    )

    # 本模块要求调用者先完成端点规范化，不能默默接受反向顺序。
    require_exception(
        ValueError,
        lambda: build_sample_id(reversed_endpoints),
        "non-canonical endpoints were accepted",
    )

    require_exception(
        TypeError,
        lambda: build_sample_id("not-an-identity"),
        "invalid identity object was accepted",
    )

    require_exception(
        FrozenInstanceError,
        lambda: setattr(identity, "protocol", 17),
        "sample identity was mutable",
    )

    require(
        SAMPLE_METADATA_SCHEMA_VERSION
        == "flow_sample_metadata_v1",
        "unexpected metadata schema version",
    )

    require(
        SUPPORTED_FEATURE_SCHEMA_VERSION
        == "flow_features_v1",
        "unexpected feature schema version",
    )

    unique_metadata = build_sample_metadata(
        identity=identity,
        feature_row_number=1,
        match_status=MATCH_STATUS_UNIQUE,
        candidate_count=1,
        label_group=LABEL_GROUP_MALICIOUS,
    )

    require(
        unique_metadata.sample_id == sample_id,
        "metadata sample ID differs from identity ID",
    )

    require(
        unique_metadata.is_trainable,
        "unique supervised match is not trainable",
    )

    unmatched_metadata = build_sample_metadata(
        identity=identity,
        feature_row_number=2,
        match_status=MATCH_STATUS_UNMATCHED,
        candidate_count=0,
        label_group=None,
    )

    require(
        not unmatched_metadata.is_trainable,
        "unmatched sample became trainable",
    )

    same_label_ambiguity = build_sample_metadata(
        identity=identity,
        feature_row_number=3,
        match_status=(
            MATCH_STATUS_AMBIGUOUS_SAME_LABEL
        ),
        candidate_count=2,
        label_group=LABEL_GROUP_BENIGN,
    )

    require(
        not same_label_ambiguity.is_trainable,
        "same-label ambiguity became trainable",
    )

    conflicting_ambiguity = build_sample_metadata(
        identity=identity,
        feature_row_number=4,
        match_status=(
            MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS
        ),
        candidate_count=2,
        label_group=None,
    )

    require(
        not conflicting_ambiguity.is_trainable,
        "conflicting-label ambiguity became trainable",
    )

    require_exception(
        ValueError,
        lambda: build_sample_metadata(
            identity=identity,
            feature_row_number=0,
            match_status=MATCH_STATUS_UNIQUE,
            candidate_count=1,
            label_group=LABEL_GROUP_MALICIOUS,
        ),
        "zero feature row number was accepted",
    )

    require_exception(
        ValueError,
        lambda: build_sample_metadata(
            identity=identity,
            feature_row_number=1,
            match_status=MATCH_STATUS_UNIQUE,
            candidate_count=1,
            label_group=None,
        ),
        "unique match without a label was accepted",
    )

    require_exception(
        ValueError,
        lambda: build_sample_metadata(
            identity=identity,
            feature_row_number=1,
            match_status=(
                MATCH_STATUS_AMBIGUOUS_SAME_LABEL
            ),
            candidate_count=1,
            label_group=LABEL_GROUP_BENIGN,
        ),
        "single-candidate ambiguity was accepted",
    )

    output_stream = io.StringIO()

    write_sample_metadata_csv_header(
        output_stream
    )

    write_sample_metadata_csv_record(
        output_stream,
        unique_metadata,
    )

    write_sample_metadata_csv_record(
        output_stream,
        unmatched_metadata,
    )

    expected_csv = (
        "metadata_schema_version,"
        "feature_schema_version,"
        "feature_row_number,"
        "sample_id,"
        "capture_id,"
        "protocol,"
        "endpoint_a_ip,"
        "endpoint_a_port,"
        "endpoint_b_ip,"
        "endpoint_b_port,"
        "first_seen_unix_microseconds,"
        "last_seen_unix_microseconds,"
        "match_status,"
        "candidate_count,"
        "label_group,"
        "is_trainable\n"
        "flow_sample_metadata_v1,"
        "flow_features_v1,"
        "1,"
        f"{sample_id},"
        "ctu13-scenario-7,"
        "6,"
        "192.0.2.10,"
        "55000,"
        "198.51.100.20,"
        "443,"
        "1313495484049047,"
        "1313495485299048,"
        "unique,"
        "1,"
        "malicious,"
        "1\n"
        "flow_sample_metadata_v1,"
        "flow_features_v1,"
        "2,"
        f"{sample_id},"
        "ctu13-scenario-7,"
        "6,"
        "192.0.2.10,"
        "55000,"
        "198.51.100.20,"
        "443,"
        "1313495484049047,"
        "1313495485299048,"
        "unmatched,"
        "0,"
        ","
        "0\n"
    )

    require(
        output_stream.getvalue()
        == expected_csv,
        "metadata CSV output is not stable",
    )

    require(
        not output_stream.closed,
        "metadata writer closed the borrowed stream",
    )

    invalid_metadata = FlowSampleMetadata(
        feature_row_number=1,
        identity=identity,
        match_status=MATCH_STATUS_UNIQUE,
        candidate_count=2,
        label_group=LABEL_GROUP_MALICIOUS,
    )

    invalid_output = io.StringIO()

    require_exception(
        ValueError,
        lambda: write_sample_metadata_csv_record(
            invalid_output,
            invalid_metadata,
        ),
        "invalid metadata was written",
    )

    require(
        invalid_output.getvalue() == "",
        "invalid metadata partially modified output",
    )

    print("[PASS] flow sample metadata contract")


def main() -> int:
    """测试程序入口。"""

    arguments = parse_arguments()

    try:
        run_tests(arguments.scripts_dir)
    except (
        ImportError,
        OSError,
        RuntimeError,
        TypeError,
        ValueError,
    ) as error:
        print(
            f"[FAIL] {error}",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
    