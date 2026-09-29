#!/usr/bin/env python3

"""验证同源前缀不会跨训练和验证集合。"""

import argparse
import csv
import subprocess
import sys
import tempfile
from pathlib import Path


COLUMNS = ("capture_id", "source_capture_id", "split")


def run_case(script, path, rows, expected_output=None, expected_error=None):
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(COLUMNS)
        writer.writerows(rows)

    result = subprocess.run(
        [sys.executable, str(script), str(path)],
        capture_output=True,
        text=True,
        check=False,
        timeout=5,
    )

    if expected_output is not None:
        if result.returncode != 0 or result.stdout != expected_output:
            raise RuntimeError(f"unexpected success result: {result}")
    else:
        if result.returncode == 0 or result.stdout:
            raise RuntimeError(f"failure leaked stdout: {result}")
        if expected_error not in result.stderr:
            raise RuntimeError(f"unexpected error: {result.stderr}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    arguments = parser.parse_args()

    sys.path.insert(0, str(arguments.script.parent.resolve()))
    from validate_ml_split_manifest import (
        validate_manifest_with_assignments,
    )

    with tempfile.TemporaryDirectory(
        prefix="ml-split-", dir=arguments.work_dir
    ) as temporary_directory:
        manifest = Path(temporary_directory) / "splits.csv"

        run_case(
            arguments.script,
            manifest,
            [
                ("s3-first200", "iot23-s3-1", "train"),
                ("s3-first500", "iot23-s3-1", "train"),
                ("other-capture", "iot23-other", "validation"),
            ],
            expected_output=(
                "captures_total=3\n"
                "source_groups=2\n"
                "train=2\n"
                "validation=1\n"
                "test=0\n"
            ),
        )

        with manifest.open(
            "r", encoding="utf-8", newline=""
        ) as input_stream:
            counts, source_count, assignments = (
                validate_manifest_with_assignments(input_stream)
            )
            if input_stream.closed:
                raise RuntimeError("validator closed a borrowed stream")

        expected_assignments = {
            "s3-first200": ("iot23-s3-1", "train"),
            "s3-first500": ("iot23-s3-1", "train"),
            "other-capture": ("iot23-other", "validation"),
        }
        if (
            counts != {"train": 2, "validation": 1, "test": 0}
            or source_count != 2
            or assignments != expected_assignments
        ):
            raise RuntimeError(
                f"unexpected validated assignments: {assignments!r}"
            )

        run_case(
            arguments.script,
            manifest,
            [
                ("s3-first200", "iot23-s3-1", "train"),
                ("s3-first500", "iot23-s3-1", "validation"),
            ],
            expected_error="source_capture_id crosses splits",
        )

        run_case(
            arguments.script,
            manifest,
            [
                ("s3-first200", "iot23-s3-1", "train"),
                ("s3-first500", "iot23-s3-1", "train"),
            ],
            expected_error="train and validation each need",
        )

        run_case(
            arguments.script,
            manifest,
            [
                ("same-id", "iot23-s3-1", "train"),
                ("same-id", "iot23-other", "validation"),
            ],
            expected_error="duplicate capture_id",
        )

    print("[PASS] ML split manifest")
    return 0


if __name__ == "__main__":
    sys.exit(main())