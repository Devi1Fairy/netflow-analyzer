#!/usr/bin/env python3

"""验证IoT-23流匹配审计CLI的成功与失败行为。"""

import argparse
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

from test_iot23_label_audit import build_record, write_log


def run_cli(
    script: Path,
    label_file: Path,
    flow_csv: Path,
) -> subprocess.CompletedProcess:
    """启动独立进程，捕获退出码、stdout和stderr。"""

    return subprocess.run(
        [
            sys.executable,
            str(script),
            str(label_file),
            str(flow_csv),
        ],
        capture_output=True,
        text=True,
        check=False,
        timeout=5,
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

    # 当前C流CSV格式定义暂位于CTU-13审计模块；
    # 本测试关注CLI行为，后续再抽取数据集无关的解析模块。
    sys.path.insert(0, str(arguments.script.parent.resolve()))
    from audit_ctu13_flow_matches import EXPECTED_FLOW_COLUMNS

    with tempfile.TemporaryDirectory(
        prefix="iot23-flow-match-",
        dir=arguments.work_dir,
    ) as temporary_directory:
        work_dir = Path(temporary_directory)
        label_file = work_dir / "labels.log"
        good_flow_csv = work_dir / "good-flows.csv"
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
        )

        if (
            success.returncode != 0
            or success.stdout != expected_stdout
            or success.stderr
        ):
            raise RuntimeError(
                "unexpected successful CLI result: "
                f"{success!r}"
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

    print("[PASS] IoT-23 flow audit CLI tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())