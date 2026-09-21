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