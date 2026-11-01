#!/usr/bin/env python3
"""只测试CLI和TCP等待路径；不打开真实串口。"""

import argparse
import os
from pathlib import Path
import re
import select
import signal
import socket
import subprocess
import tempfile
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def wait_for_listen(process):
    """有界读取stderr，取得内核为端口0分配的实际端口。"""
    deadline = time.monotonic() + 3.0
    data = b""
    while time.monotonic() < deadline:
        ready, _, _ = select.select(
            [process.stderr], [], [], max(0.0, deadline - time.monotonic())
        )
        require(ready, "没有按时收到LISTEN")
        chunk = os.read(process.stderr.fileno(), 4096)
        require(chunk, "程序在LISTEN前退出")
        data += chunk
        require(len(data) <= 65536, "启动日志异常增大")
        match = re.search(rb"LISTEN 127\.0\.0\.1:(\d+)\n", data)
        if match:
            return int(match.group(1))
    raise RuntimeError("等待LISTEN超时")


def check_waiting_exit(program, missing_device, stop_signal):
    command = [str(program), str(missing_device), "127.0.0.1", "0", "10"]
    with subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    ) as process:
        try:
            port = wait_for_listen(process)
            if stop_signal is None:
                with socket.create_connection(("127.0.0.1", port), timeout=2):
                    pass
            else:
                process.send_signal(stop_signal)

            output, errors = process.communicate(timeout=3)
            require(output == b"", "程序不应向stdout输出BMS数据")

            if stop_signal is None:
                require(process.returncode != 0, "不存在的设备必须失败")
                require(b"OPEN_SERIAL " in errors, "未进入设备打开阶段")
                require(b"open serial:" in errors, "缺少设备打开错误")
                require(b"READY:" not in errors, "不应完成串口配置")
            else:
                require(process.returncode == 0, "等待中的受控停止应成功退出")
                require(b"OPEN_SERIAL " not in errors, "停止前不应打开串口")
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=2)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True, type=Path)
    program = parser.parse_args().program.resolve()

    result = subprocess.run(
        [str(program), "--help"], capture_output=True, timeout=3
    )
    require(result.returncode == 0 and b"Usage:" in result.stdout, "帮助失败")

    result = subprocess.run([str(program)], capture_output=True, timeout=3)
    require(result.returncode != 0, "缺少参数时必须失败")

    with tempfile.TemporaryDirectory(prefix="bms-gateway-cli-") as directory:
        # 只构造路径，不创建文件，保证测试不访问硬件节点。
        missing = Path(directory) / "missing-device"

        for port, seconds in [
            ("-1", "1"), ("65536", "1"), ("+1", "1"),
            ("0", "0"), ("0", "301"), ("0", "1x")
        ]:
            result = subprocess.run(
                [str(program), str(missing), "127.0.0.1", port, seconds],
                capture_output=True, timeout=3
            )
            require(result.returncode != 0, "错误参数被接受")
            require(b"LISTEN " not in result.stderr, "错误参数启动了监听")

        result = subprocess.run(
            [str(program), str(missing), "not-an-ip", "0", "1"],
            capture_output=True, timeout=3
        )
        require(result.returncode != 0, "非法IPv4地址被接受")
        require(b"OPEN_SERIAL " not in result.stderr, "非法地址触发了串口打开")
        print("[PASS] CLI help and argument validation")

        result = subprocess.run(
            [str(program), str(missing), "127.0.0.1", "0", "1"],
            capture_output=True, timeout=4
        )
        require(result.returncode != 0, "无人接入不应假报成功")
        require(b"WAIT_TIMEOUT" in result.stderr, "缺少等待超时记录")
        require(b"OPEN_SERIAL " not in result.stderr, "无人接入时打开了串口")
        print("[PASS] no-client deadline without serial open")

        for stop_signal in (signal.SIGINT, signal.SIGTERM):
            check_waiting_exit(program, missing, stop_signal)
        print("[PASS] SIGINT and SIGTERM stop the waiting gateway")

        check_waiting_exit(program, missing, None)
        print("[PASS] accepted client reaches missing-device failure safely")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())