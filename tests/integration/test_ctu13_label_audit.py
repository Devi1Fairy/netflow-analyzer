#!/usr/bin/env python3

"""
验证CTU-13标签审计工具的命令行行为。

测试使用临时生成的小型CSV，不依赖公开数据集文件，
因此可以在开发机、目标板和未来CI环境中重复执行。
"""

import argparse
import csv
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_COLUMNS = (
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


def parse_arguments() -> argparse.Namespace:
    """解析CTest传入的脚本路径和临时目录。"""

    parser = argparse.ArgumentParser(
        description="Test the CTU-13 label audit tool."
    )

    parser.add_argument(
        "--script",
        required=True,
        type=Path,
        help="Path to inspect_ctu13_labels.py.",
    )

    parser.add_argument(
        "--work-dir",
        required=True,
        type=Path,
        help="Writable directory for temporary test files.",
    )

    return parser.parse_args()


def build_row(label: str):
    """构造一条字段数量正确的合成CTU-13记录。"""

    return [
        "2011/08/16 13:51:24.000000",
        "1.000000",
        "tcp",
        "192.0.2.10",
        "12345",
        "  <->",
        "198.51.100.20",
        "80",
        "CON",
        "0",
        "0",
        "2",
        "120",
        "60",
        label,
    ]


def write_csv(
    output_path: Path,
    columns,
    rows,
) -> None:
    """把指定表头和数据行写入临时CSV文件。"""

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
    input_file: Path,
) -> subprocess.CompletedProcess:
    """使用当前Python解释器运行被测标签审计脚本。"""

    return subprocess.run(
        [
            sys.executable,
            str(script),
            str(input_file),
        ],
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )


def require_failure(
    completed_process: subprocess.CompletedProcess,
    expected_error: str,
) -> None:
    """要求子进程失败，并在stderr中包含指定错误。"""

    if completed_process.returncode == 0:
        raise RuntimeError(
            "audit tool unexpectedly returned success\n"
            f"stdout:\n{completed_process.stdout}\n"
            f"stderr:\n{completed_process.stderr}"
        )

    if expected_error not in completed_process.stderr:
        raise RuntimeError(
            "missing expected error text: "
            f"{expected_error!r}\n"
            f"stdout:\n{completed_process.stdout}\n"
            f"stderr:\n{completed_process.stderr}"
        )


def run_tests(
    script: Path,
    work_dir: Path,
) -> None:
    """生成确定性输入并验证成功和失败路径。"""

    if not script.is_file():
        raise RuntimeError(
            f"audit script does not exist: {script}"
        )

    if not work_dir.is_dir():
        raise RuntimeError(
            f"work directory does not exist: {work_dir}"
        )

    with tempfile.TemporaryDirectory(
        prefix="ctu13-label-audit-",
        dir=work_dir,
    ) as temporary_directory:
        temporary_path = Path(temporary_directory)

        valid_path = temporary_path / "valid.binetflow"

        write_csv(
            valid_path,
            EXPECTED_COLUMNS,
            [
                build_row(
                    "flow=From-Botnet-V48-UDP-DNS"
                ),
                build_row(
                    "flow=From-Normal-V48-Stribrek"
                ),
                build_row("flow=Background"),
                build_row("flow=To-Botnet-V48-TCP"),
            ],
        )

        completed_process = run_audit(
            script,
            valid_path,
        )

        if completed_process.returncode != 0:
            raise RuntimeError(
                "valid audit input returned non-zero status\n"
                f"exit code: "
                f"{completed_process.returncode}\n"
                f"stdout:\n{completed_process.stdout}\n"
                f"stderr:\n{completed_process.stderr}"
            )

        expected_output = (
            f"input={valid_path}\n"
            "total=4\n"
            "malicious=1\n"
            "benign=1\n"
            "exclude=2\n"
        )

        if completed_process.stdout != expected_output:
            raise RuntimeError(
                "unexpected audit output\n"
                f"expected:\n{expected_output}\n"
                f"actual:\n{completed_process.stdout}"
            )

        if completed_process.stderr:
            raise RuntimeError(
                "valid audit input wrote to stderr\n"
                f"stderr:\n{completed_process.stderr}"
            )

        invalid_header_path = (
            temporary_path / "invalid-header.binetflow"
        )

        write_csv(
            invalid_header_path,
            EXPECTED_COLUMNS[:-1],
            [
                build_row("flow=Background")[:-1],
            ],
        )

        require_failure(
            run_audit(script, invalid_header_path),
            "unexpected CSV columns",
        )

        missing_field_path = (
            temporary_path / "missing-field.binetflow"
        )

        write_csv(
            missing_field_path,
            EXPECTED_COLUMNS,
            [
                build_row("flow=Background")[:-1],
            ],
        )

        require_failure(
            run_audit(script, missing_field_path),
            "missing fields",
        )

        empty_label_path = (
            temporary_path / "empty-label.binetflow"
        )

        write_csv(
            empty_label_path,
            EXPECTED_COLUMNS,
            [
                build_row("   "),
            ],
        )

        require_failure(
            run_audit(script, empty_label_path),
            "empty Label",
        )

        empty_file_path = (
            temporary_path / "header-only.binetflow"
        )

        write_csv(
            empty_file_path,
            EXPECTED_COLUMNS,
            [],
        )

        require_failure(
            run_audit(script, empty_file_path),
            "contains no data records",
        )

        nonexistent_path = (
            temporary_path / "does-not-exist.binetflow"
        )

        require_failure(
            run_audit(script, nonexistent_path),
            "CTU-13 label audit failed",
        )


def main() -> int:
    """测试程序入口。"""

    arguments = parse_arguments()

    try:
        run_tests(
            script=arguments.script.resolve(),
            work_dir=arguments.work_dir.resolve(),
        )
    except (
        OSError,
        RuntimeError,
        subprocess.TimeoutExpired,
        ValueError,
    ) as error:
        print(
            f"[FAIL] CTU-13 label audit tests: {error}",
            file=sys.stderr,
        )
        return 1

    print("[PASS] CTU-13 label audit tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())