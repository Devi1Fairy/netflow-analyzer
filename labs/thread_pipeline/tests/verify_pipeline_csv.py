#!/usr/bin/env python3
"""
运行多线程流水线程序，并验证它生成的CSV文件。

这个脚本既用于12个任务的功能验收，也用于10000个任务的压力测试。
测试不依赖结果行顺序，因为两个工作线程的完成顺序不固定。
"""

import argparse
import csv
import subprocess
import sys
from pathlib import Path


# CSV表头必须严格使用这些字段及顺序。
EXPECTED_COLUMNS = [
    "task_id",
    "input_value",
    "output_value",
    "worker_index",
]


class VerificationError(RuntimeError):
    """表示流水线运行或CSV内容不符合预期。"""


def parse_arguments() -> argparse.Namespace:
    """解析CTest传入的命令行参数。"""

    parser = argparse.ArgumentParser(
        description="Run the thread pipeline and verify its CSV output."
    )

    parser.add_argument(
        "--program",
        required=True,
        type=Path,
        help="Path to the thread_pipeline_demo executable.",
    )

    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="Path used for the generated CSV file.",
    )

    parser.add_argument(
        "--task-count",
        required=True,
        type=int,
        help="Expected number of pipeline tasks.",
    )

    parser.add_argument(
        "--timeout",
        type=float,
        default=20.0,
        help="Maximum number of seconds allowed for the pipeline.",
    )

    arguments = parser.parse_args()

    if arguments.task_count <= 0:
        parser.error("--task-count must be greater than zero")

    if arguments.timeout <= 0:
        parser.error("--timeout must be greater than zero")

    return arguments


def run_pipeline(
    program: Path,
    output_file: Path,
    task_count: int,
    timeout: float,
) -> None:
    """
    启动被测程序并等待它完成。

    先删除旧CSV，避免程序本次没有生成文件，但测试误读了上次遗留结果。
    """

    try:
        output_file.unlink(missing_ok=True)
    except OSError as error:
        raise VerificationError(
            f"Failed to remove old output file: {error}"
        ) from error

    command = [
        str(program),
        str(output_file),
        str(task_count),
    ]

    try:
        completed_process = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise VerificationError(
            f"Pipeline timed out after {timeout} seconds"
        ) from error
    except OSError as error:
        raise VerificationError(
            f"Failed to start pipeline program: {error}"
        ) from error

    if completed_process.returncode != 0:
        raise VerificationError(
            "Pipeline program returned a non-zero status.\n"
            f"stdout:\n{completed_process.stdout}\n"
            f"stderr:\n{completed_process.stderr}"
        )

    if not output_file.is_file():
        raise VerificationError(
            f"Pipeline did not create CSV file: {output_file}"
        )


def parse_integer(
    row: dict[str, str],
    field_name: str,
    line_number: int,
) -> int:
    """读取CSV中的一个整数字段，并提供明确的错误信息。"""

    value = row[field_name]

    try:
        return int(value, 10)
    except ValueError as error:
        raise VerificationError(
            f"Line {line_number}: "
            f"{field_name} is not an integer: {value!r}"
        ) from error


def verify_csv(
    output_file: Path,
    expected_task_count: int,
) -> None:
    """
    验证CSV表头、任务编号、输入、平方结果和工作线程编号。

    使用set保存已经出现的任务编号，因此只需要扫描一次CSV，
    适合10000个任务的压力测试。
    """

    seen_task_ids: set[int] = set()

    try:
        csv_file = output_file.open(
            mode="r",
            encoding="utf-8",
            newline="",
        )
    except OSError as error:
        raise VerificationError(
            f"Failed to open CSV file: {error}"
        ) from error

    with csv_file:
        reader = csv.DictReader(csv_file)

        if reader.fieldnames != EXPECTED_COLUMNS:
            raise VerificationError(
                "Unexpected CSV header: "
                f"{reader.fieldnames!r}"
            )
        # CSV第一行是表头，所以第一条数据位于第2行。
        for line_number, row in enumerate(reader, start=2):
            # None键表示一行出现了多余字段。
            if None in row:
                raise VerificationError(
                    f"Line {line_number}: too many CSV fields"
                )

            # None值表示某一行缺少字段。
            if any(value is None for value in row.values()):
                raise VerificationError(
                    f"Line {line_number}: missing CSV field"
                )

            task_id = parse_integer(
                row,
                "task_id",
                line_number,
            )

            input_value = parse_integer(
                row,
                "input_value",
                line_number,
            )

            output_value = parse_integer(
                row,
                "output_value",
                line_number,
            )

            worker_index = parse_integer(
                row,
                "worker_index",
                line_number,
            )

            if task_id < 1 or task_id > expected_task_count:
                raise VerificationError(
                    f"Line {line_number}: "
                    f"task_id {task_id} is out of range"
                )

            if task_id in seen_task_ids:
                raise VerificationError(
                    f"Duplicate task_id: {task_id}"
                )

            seen_task_ids.add(task_id)

            if input_value != task_id:
                raise VerificationError(
                    f"Task {task_id}: expected input "
                    f"{task_id}, got {input_value}"
                )

            expected_output = task_id * task_id

            if output_value != expected_output:
                raise VerificationError(
                    f"Task {task_id}: expected output "
                    f"{expected_output}, got {output_value}"
                )

            if worker_index not in {0, 1}:
                raise VerificationError(
                    f"Task {task_id}: invalid worker_index "
                    f"{worker_index}"
                )

    if len(seen_task_ids) != expected_task_count:
        expected_task_ids = set(
            range(1, expected_task_count + 1)
        )

        missing_task_ids = sorted(
            expected_task_ids - seen_task_ids
        )

        # 最多显示前10个编号，防止错误日志过长。
        missing_preview = missing_task_ids[:10]

        raise VerificationError(
            f"Expected {expected_task_count} tasks, "
            f"got {len(seen_task_ids)}; "
            f"first missing IDs: {missing_preview}"
        )


def main() -> int:
    """测试脚本入口。"""

    arguments = parse_arguments()

    try:
        run_pipeline(
            program=arguments.program,
            output_file=arguments.output,
            task_count=arguments.task_count,
            timeout=arguments.timeout,
        )

        verify_csv(
            output_file=arguments.output,
            expected_task_count=arguments.task_count,
        )
    except VerificationError as error:
        print(
            f"[FAIL] {error}",
            file=sys.stderr,
        )

        return 1

    print(
        "[PASS] CSV acceptance: "
        f"{arguments.task_count} valid results"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())