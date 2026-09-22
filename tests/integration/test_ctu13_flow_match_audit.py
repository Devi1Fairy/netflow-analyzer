#!/usr/bin/env python3

"""验证CTU-13流匹配审计工具。"""

import argparse
import csv
import subprocess
import sys
import tempfile
from pathlib import Path


LABEL_COLUMNS = (
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

FLOW_COLUMNS = (
    "protocol",
    "tcp_state",
    "endpoint_a_ip",
    "endpoint_a_port",
    "endpoint_b_ip",
    "endpoint_b_port",
    "a_to_b_packets",
    "a_to_b_captured_bytes",
    "a_to_b_wire_bytes",
    "b_to_a_packets",
    "b_to_a_captured_bytes",
    "b_to_a_wire_bytes",
    "first_seen_seconds",
    "first_seen_microseconds",
    "last_seen_seconds",
    "last_seen_microseconds",
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--script",
        required=True,
        type=Path,
    )

    parser.add_argument(
        "--work-dir",
        required=True,
        type=Path,
    )

    return parser.parse_args()


def label_row(
    protocol: str,
    source_ip: str,
    source_port: str,
    destination_ip: str,
    destination_port: str,
    label: str,
    start_time: str = "2011/08/16 13:51:24.000000",
):
    """构造一条合成CTU-13标签。"""

    return [
        start_time,
        "2.000000",
        protocol,
        source_ip,
        source_port,
        "  <->",
        destination_ip,
        destination_port,
        "CON",
        "0",
        "0",
        "2",
        "120",
        "60",
        label,
    ]


def flow_row(
    protocol: int,
    endpoint_a_ip: str,
    endpoint_a_port: int,
    endpoint_b_ip: str,
    endpoint_b_port: int,
):
    """构造一条位于标签时间区间内的C流记录。"""

    tcp_state = (
        "established"
        if protocol == 6
        else "not-applicable"
    )

    return [
        str(protocol),
        tcp_state,
        endpoint_a_ip,
        str(endpoint_a_port),
        endpoint_b_ip,
        str(endpoint_b_port),
        "1",
        "60",
        "60",
        "1",
        "60",
        "60",
        "1313495484",
        "250000",
        "1313495484",
        "750000",
    ]


def write_csv(
    output_path: Path,
    columns,
    rows,
) -> None:
    """创建确定性测试CSV。"""

    with output_path.open(
        "w",
        encoding="utf-8",
        newline="",
    ) as output_stream:
        writer = csv.writer(output_stream)
        writer.writerow(columns)
        writer.writerows(rows)

def require(
    condition: bool,
    message: str,
) -> None:
    """要求condition成立，否则使测试失败。"""

    if not condition:
        raise RuntimeError(message)

def run_audit(
    script: Path,
    label_file: Path,
    flow_csv: Path,
):
    """使用当前Python解释器运行被测工具。"""

    return subprocess.run(
        [
            sys.executable,
            str(script),
            str(label_file),
            str(flow_csv),
        ],
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )


def run_tests(
    script: Path,
    work_dir: Path,
) -> None:
    """覆盖五种匹配分类及标签过滤边界。"""

    if not script.is_file():
        raise RuntimeError(
            f"audit script does not exist: {script}"
        )

    # 被测脚本依赖同目录中的其他项目模块，因此必须先加入
    # 模块搜索路径，再执行局部导入。
    sys.path.insert(0, str(script.parent))

    from audit_ctu13_flow_matches import (
        LabelInterval,
        build_flow_sample_metadata,
        classify_match_candidates,
    )

    from flow_sample_metadata import (
        MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS,
        MATCH_STATUS_AMBIGUOUS_SAME_LABEL,
        MATCH_STATUS_UNIQUE,
        MATCH_STATUS_UNMATCHED,
        FlowSampleIdentity,
    )

    unmatched = classify_match_candidates([])

    require(
        unmatched.status
        == MATCH_STATUS_UNMATCHED
        and unmatched.candidate_count == 0
        and unmatched.label_group is None,
        "empty candidate list was misclassified",
    )

    unique = classify_match_candidates(
        [
            LabelInterval(
                start_microseconds=100,
                end_microseconds=200,
                label_group="malicious",
            )
        ]
    )

    require(
        unique.status == MATCH_STATUS_UNIQUE
        and unique.candidate_count == 1
        and unique.label_group == "malicious",
        "unique candidate was misclassified",
    )

    same_label = classify_match_candidates(
        [
            LabelInterval(100, 200, "benign"),
            LabelInterval(150, 250, "benign"),
        ]
    )

    require(
        same_label.status
        == MATCH_STATUS_AMBIGUOUS_SAME_LABEL
        and same_label.candidate_count == 2
        and same_label.label_group == "benign",
        "same-label ambiguity was misclassified",
    )

    conflicting = classify_match_candidates(
        [
            LabelInterval(100, 200, "benign"),
            LabelInterval(150, 250, "malicious"),
        ]
    )

    require(
        conflicting.status
        == MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS
        and conflicting.candidate_count == 2
        and conflicting.label_group is None,
        "conflicting-label ambiguity was misclassified",
    )

    expected_identity = FlowSampleIdentity(
        capture_id="ctu13-scenario-7",
        protocol=6,
        endpoint_a_ipv4=167772161,
        endpoint_a_port=1000,
        endpoint_b_ipv4=167772162,
        endpoint_b_port=80,
        first_seen_microseconds=1313495484250000,
        last_seen_microseconds=1313495484750000,
    )

    metadata = build_flow_sample_metadata(
        capture_id="ctu13-scenario-7",
        feature_row_number=1,
        flow_key=(
            6,
            167772161,
            1000,
            167772162,
            80,
        ),
        first_seen_microseconds=1313495484250000,
        last_seen_microseconds=1313495484750000,
        classification=unique,
    )

    require(
        metadata.identity == expected_identity,
        "flow identity was not preserved in metadata",
    )

    require(
        metadata.feature_row_number == 1
        and metadata.match_status
        == MATCH_STATUS_UNIQUE
        and metadata.candidate_count == 1
        and metadata.label_group == "malicious"
        and metadata.is_trainable,
        "unique flow match metadata is incorrect",
    )

    with tempfile.TemporaryDirectory(
        prefix="ctu13-flow-match-",
        dir=work_dir,
    ) as temporary_directory:
        temporary_path = Path(
            temporary_directory
        )

        label_file = (
            temporary_path / "labels.binetflow"
        )

        flow_csv = (
            temporary_path / "flows.csv"
        )

        write_csv(
            label_file,
            LABEL_COLUMNS,
            [
                # 唯一恶意匹配；标签方向与C端规范化方向相反。
                label_row(
                    "tcp",
                    "10.0.0.2",
                    "80",
                    "10.0.0.1",
                    "1000",
                    "flow=From-Botnet-test",
                ),

                # 唯一正常匹配。
                label_row(
                    "udp",
                    "10.0.0.3",
                    "2000",
                    "10.0.0.4",
                    "53",
                    "flow=From-Normal-test",
                ),

                # 被策略排除，所以对应C流应当无匹配。
                label_row(
                    "udp",
                    "10.0.0.5",
                    "3000",
                    "10.0.0.6",
                    "53",
                    "flow=Background",
                ),

                # 可监督但协议不受当前分析器支持。
                label_row(
                    "arp",
                    "10.0.0.11",
                    "",
                    "10.0.0.12",
                    "",
                    "flow=From-Normal-test",
                ),

                # 同一五元组存在两个重叠的恶意标签。
                label_row(
                    "udp",
                    "10.0.0.7",
                    "4000",
                    "10.0.0.8",
                    "53",
                    "flow=From-Botnet-first",
                ),
                label_row(
                    "udp",
                    "10.0.0.7",
                    "4000",
                    "10.0.0.8",
                    "53",
                    "flow=From-Botnet-second",
                    "2011/08/16 13:51:24.500000",
                ),

                # 同一时间和五元组出现互相冲突的监督标签。
                label_row(
                    "udp",
                    "10.0.0.9",
                    "5000",
                    "10.0.0.10",
                    "53",
                    "flow=From-Botnet-conflict",
                ),
                label_row(
                    "udp",
                    "10.0.0.9",
                    "5000",
                    "10.0.0.10",
                    "53",
                    "flow=From-Normal-conflict",
                ),
            ],
        )

        write_csv(
            flow_csv,
            FLOW_COLUMNS,
            [
                flow_row(
                    6,
                    "10.0.0.1",
                    1000,
                    "10.0.0.2",
                    80,
                ),
                flow_row(
                    17,
                    "10.0.0.3",
                    2000,
                    "10.0.0.4",
                    53,
                ),
                flow_row(
                    17,
                    "10.0.0.5",
                    3000,
                    "10.0.0.6",
                    53,
                ),
                flow_row(
                    17,
                    "10.0.0.7",
                    4000,
                    "10.0.0.8",
                    53,
                ),
                flow_row(
                    17,
                    "10.0.0.9",
                    5000,
                    "10.0.0.10",
                    53,
                ),
            ],
        )

        completed_process = run_audit(
            script,
            label_file,
            flow_csv,
        )

        if completed_process.returncode != 0:
            raise RuntimeError(
                "valid flow match audit failed\n"
                f"stdout:\n{completed_process.stdout}\n"
                f"stderr:\n{completed_process.stderr}"
            )

        expected_output = (
            "labels_total=8\n"
            "labels_excluded=1\n"
            "labels_unsupported=1\n"
            "labels_indexed=6\n"
            "labels_indexed_malicious=4\n"
            "labels_indexed_benign=2\n"
            "flows_total=5\n"
            "matches_unique=2\n"
            "matches_unique_malicious=1\n"
            "matches_unique_benign=1\n"
            "matches_unmatched=1\n"
            "matches_ambiguous_same_label=1\n"
            "matches_ambiguous_conflicting_labels=1\n"
        )

        if completed_process.stdout != expected_output:
            raise RuntimeError(
                "unexpected flow match audit output\n"
                f"expected:\n{expected_output}\n"
                f"actual:\n{completed_process.stdout}"
            )

        if completed_process.stderr:
            raise RuntimeError(
                "valid audit wrote to stderr\n"
                f"stderr:\n{completed_process.stderr}"
            )


def main() -> int:
    arguments = parse_arguments()

    try:
        run_tests(
            arguments.script,
            arguments.work_dir,
        )
    except (OSError, RuntimeError) as error:
        print(
            f"[FAIL] {error}",
            file=sys.stderr,
        )
        return 1

    print("[PASS] CTU-13 flow match audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())