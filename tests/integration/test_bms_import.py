#!/usr/bin/env python3
"""仅用临时普通文件和SQLite验证导入工具；不访问串口或TCP。"""

import argparse
from contextlib import closing
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile


SAMPLE = (
    b"BMS state=1, faults=0x00000000, latched=0x00000000, "
    b"load=0, clear=0, cell0_mv=3227, temp_est_dC=259, i_ma=0, "
    b"iv=1, i_src=3, i_age=101, ina=1, bus_mv=0, "
    b"sample=286605, ctrl=1433031, gap_max=20"
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(program, *arguments):
    return subprocess.run(
        [str(program), *map(str, arguments)],
        capture_output=True,
        timeout=10,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True, type=Path)
    program = parser.parse_args().program.resolve()

    result = run(program, "--help")
    require(result.returncode == 0 and b"Usage:" in result.stdout, "帮助失败")
    result = run(program)
    require(result.returncode == 1 and result.stdout == b"", "参数错误没有失败")

    with tempfile.TemporaryDirectory(prefix="bms-import-test-") as name:
        root = Path(name)
        source = root / "valid.log"
        output = root / "valid.sqlite3"
        changed = SAMPLE.replace(b"i_ma=0", b"i_ma=-1234")
        # 260条跨过两个128条批次；混合LF/CRLF及非遥测行。
        source.write_bytes(b"BOOT ready\n\n" + (SAMPLE + b"\n" + changed + b"\r\n") * 130)
        original = source.read_bytes()
        result = run(program, source, output, "bms-'01", "offline-01")
        require(result.returncode == 0, result.stderr.decode(errors="replace"))
        counts = {
            key: int(value)
            for key, value in (
                line.split("=", 1) for line in result.stdout.decode().splitlines()
            )
        }
        require(counts == {
            "input_lines": 262, "imported_rows": 260,
            "skipped_lines": 2, "received_unix_us": 0,
        }, "导入统计不一致")
        require(source.read_bytes() == original, "输入文件被修改")
        with closing(sqlite3.connect(str(output))) as db:
            require(db.execute("SELECT COUNT(*) FROM bms_samples_v1").fetchone()[0] == 260,
                    "数据库行数错误")
            row = db.execute(
                "SELECT device_id,session_id,received_unix_us,sample,cell0_mv,i_ma "
                "FROM bms_samples_v1 ORDER BY id LIMIT 2"
            ).fetchall()
            require(row == [
                ("bms-'01", "offline-01", 0, 286605, 3227, 0),
                ("bms-'01", "offline-01", 0, 286605, 3227, -1234),
            ], "字段/未知时间/重复sample保存错误")
        require(not list(root.glob("valid.sqlite3.part.*")), "成功后残留临时库")
        print("[PASS] multi-batch import, LF/CRLF, skipped lines and SQL read-back")

        before = output.read_bytes()
        result = run(program, source, output, "device", "again")
        require(result.returncode == 1 and result.stdout == b"", "覆盖已有数据库")
        require(output.read_bytes() == before, "已有数据库被修改")
        link = root / "output-link"
        link.symlink_to(output)
        result = run(program, source, link, "device", "session")
        require(result.returncode == 1 and result.stdout == b"", "接受符号链接输出")
        require(output.read_bytes() == before, "链接目标被修改")

        for number, bad_id in enumerate(("", "x" * 65, "bad\nname")):
            target = root / ("bad-id-{}.sqlite3".format(number))
            result = run(program, source, target, bad_id, "session")
            require(result.returncode == 1 and result.stdout == b"", "非法ID被接受")
            require(not target.exists(), "非法参数发布了数据库")
        print("[PASS] CLI validation and existing output/symlink protection")

        invalid = [
            ("empty", b""),
            ("no-samples", b"BOOT ready\n"),
            ("tail", SAMPLE),
            ("nul", SAMPLE.replace(b"state=1", b"state=\x001") + b"\n"),
            ("bad-number", SAMPLE.replace(b"i_ma=0", b"i_ma=oops") + b"\n"),
            ("bad-prefix", b"BMS\n"),
            ("long", b"x" * 1024 + b"\n"),
            ("late-error", (SAMPLE + b"\n") * 128 + b"BMS broken\n"),
        ]
        for case, content in invalid:
            log = root / (case + ".log")
            target = root / (case + ".sqlite3")
            log.write_bytes(content)
            result = run(program, log, target, "device", "session")
            require(result.returncode == 1 and result.stdout == b"", case)
            require(not target.exists(), case + "错误地发布了最终数据库")
            parts = list(root.glob(target.name + ".part.*"))
            require(parts, case + "未保留待排查临时库")
            if case == "late-error":
                with closing(sqlite3.connect(str(parts[0]))) as db:
                    require(db.execute(
                        "SELECT COUNT(*) FROM bms_samples_v1"
                    ).fetchone()[0] == 128, "未覆盖已提交批次后的失败路径")
        print("[PASS] malformed/oversized/incomplete input never publishes final DB")

        # 恰好1024字节（包含LF）的合法行仍应接受。
        padding = 1024 - len(SAMPLE) - 1
        exact = SAMPLE.replace(b"state=1", b"state=" + b"0" * padding + b"1") + b"\n"
        require(len(exact) == 1024, "边界用例构造错误")
        log = root / "exact.log"
        target = root / "exact.sqlite3"
        log.write_bytes(exact)
        require(run(program, log, target, "device", "session").returncode == 0,
                "合法1024字节行被拒绝")

        fifo = root / "input.fifo"
        os.mkfifo(str(fifo))
        input_link = root / "input-link"
        input_link.symlink_to(source)
        large = root / "large.log"
        with large.open("wb") as stream:
            stream.truncate(16 * 1024 * 1024 + 1)
        for number, bad_input in enumerate((root, fifo, input_link, large)):
            target = root / ("bad-input-{}.sqlite3".format(number))
            result = run(program, bad_input, target, "device", "session")
            require(result.returncode == 1 and result.stdout == b"", "非法输入被接受")
            require(not target.exists(), "非法输入发布了数据库")
        print("[PASS] exact line boundary and non-regular/symlink/file-size rejection")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())