#!/usr/bin/env python3

"""将已绑定的IoT-23候选安全发布为新文件；不覆盖已有目标。"""

import os
import tempfile
from pathlib import Path
from typing import Tuple

from iot23_sample_selection import (
    SplitAssignedCandidate,
    write_iot23_candidate_csv,
)


def write_iot23_candidate_file_exclusive(
    output_path: Path,
    samples: Tuple[SplitAssignedCandidate, ...],
) -> int:
    """先写同目录临时文件，再独占发布；返回候选数据行数。"""

    if not isinstance(output_path, Path):
        raise TypeError("output_path must be a Path")

    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="",
            dir=output_path.parent,
            prefix=f".{output_path.name}.",
            suffix=".tmp",
            delete=False,
        ) as output_stream:
            temporary_path = Path(output_stream.name)
            row_count = write_iot23_candidate_csv(
                output_stream,
                samples,
            )
            output_stream.flush()
            os.fsync(output_stream.fileno())

        # 硬链接创建最终文件名；若目标已存在，抛出FileExistsError。
        # 与os.replace不同，它不会覆盖已有文件。
        os.link(temporary_path, output_path)
        return row_count
    finally:
        # 正常成功或普通异常均移除本函数创建的临时名字。
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except FileNotFoundError:
                pass