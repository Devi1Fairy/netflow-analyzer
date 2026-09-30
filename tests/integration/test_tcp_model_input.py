#!/usr/bin/env python3
"""验证TCP候选记录到模型输入的固定编码契约。"""

import argparse
import sys
from pathlib import Path


def expect_value_error(encode_tcp_candidate, row, field, value):
    """修改一个字段并确认编码器拒绝该记录。"""

    invalid = dict(row)
    if value is None:
        invalid.pop(field)
    else:
        invalid[field] = value

    try:
        encode_tcp_candidate(invalid)
    except ValueError:
        return

    raise RuntimeError(
        f"invalid candidate was accepted: {field}={value!r}"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--scripts-dir",
        required=True,
        type=Path,
    )
    arguments = parser.parse_args()

    # 测试文件位于tests/integration，模块位于scripts。
    # 把scripts的绝对路径放入模块搜索路径后再导入。
    sys.path.insert(
        0,
        str(arguments.scripts_dir.resolve()),
    )

    from tcp_model_input import (
        ENCODING_VERSION,
        MODEL_FEATURE_NAMES,
        encode_tcp_candidate,
    )

    row = {
        "feature_schema_version": "flow_features_v1",
        "protocol": "6",
        "duration_microseconds": "1000000",
        "total_packet_count": "10",
        "total_captured_byte_count": "800",
        "total_wire_byte_count": "1000",
        "mean_captured_bytes_per_packet": "80.0",
        "mean_wire_bytes_per_packet": "100.0",
        "packet_count_imbalance_ratio": "0.25",
        "wire_byte_count_imbalance_ratio": "0.5",
        "tcp_state_applicable": "1",
        "tcp_phase": "established",
        "tcp_handshake_completed": "1",
        "candidate_label_group": "benign",
    }
    original = dict(row)

    features, label = encode_tcp_candidate(row)

    expected_features = (
        1000000.0,
        10.0,
        800.0,
        1000.0,
        80.0,
        100.0,
        0.25,
        0.5,
        1.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
    )

    if ENCODING_VERSION != "tcp_input_v1":
        raise RuntimeError("unexpected encoding version")
    if len(MODEL_FEATURE_NAMES) != 18:
        raise RuntimeError("unexpected feature name count")
    if features != expected_features or label != 0:
        raise RuntimeError(
            f"unexpected encoded result: {features!r}, {label}"
        )
    if row != original:
        raise RuntimeError("encoder modified the caller's mapping")

    # 标签单独返回；改变标签不能改变输入特征。
    malicious = dict(row)
    malicious["candidate_label_group"] = "malicious"
    malicious_features, malicious_label = (
        encode_tcp_candidate(malicious)
    )
    if malicious_features != features or malicious_label != 1:
        raise RuntimeError("label encoding differs")

    # 身份和来源字段不属于模型输入。
    with_metadata = dict(
        row,
        sample_id="flow_sample_id_v1:ignored",
        source_capture_id="iot23-source",
        split="validation",
    )
    if encode_tcp_candidate(with_metadata) != (features, label):
        raise RuntimeError("metadata changed model input")

    invalid_cases = (
        ("feature_schema_version", "unknown"),
        ("protocol", "17"),
        ("tcp_state_applicable", "0"),
        ("tcp_phase", "bad-phase"),
        ("candidate_label_group", "exclude"),
        ("duration_microseconds", "-1"),
        ("total_packet_count", "1.5"),
        ("mean_captured_bytes_per_packet", "nan"),
        ("mean_wire_bytes_per_packet", "inf"),
        ("packet_count_imbalance_ratio", "1.1"),
        ("wire_byte_count_imbalance_ratio", "-0.1"),
        ("tcp_handshake_completed", "2"),
        ("tcp_phase", None),
    )

    for field, value in invalid_cases:
        expect_value_error(
            encode_tcp_candidate,
            row,
            field,
            value,
        )

    print("[PASS] TCP model input encoding")
    return 0


if __name__ == "__main__":
    sys.exit(main())