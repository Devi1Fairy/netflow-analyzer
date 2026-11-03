#!/usr/bin/env python3
"""只使用PTY和127.0.0.1；必须传入测试专用网关，禁止物理串口。"""

import argparse
import contextlib
import os
from pathlib import Path
import pty
import select
import signal
import socket
import subprocess
import time

from test_bms_gateway_cli import require, wait_for_listen


class Lines:
    def __init__(self, channel):
        self.channel = channel
        self.pending = b""

    def line(self):
        deadline = time.monotonic() + 2.0
        while b"\n" not in self.pending:
            remaining = deadline - time.monotonic()
            require(remaining > 0, "等待协议行超时")
            self.channel.settimeout(remaining)
            chunk = self.channel.recv(4096)
            require(chunk, "完整协议行前连接关闭")
            self.pending += chunk
            require(len(self.pending) <= 16384, "接收缓存异常增大")
        line, self.pending = self.pending.split(b"\n", 1)
        require(b"\0" not in line, "协议行附带NUL")
        return line


@contextlib.contextmanager
def launch(program, control, fail=False, seconds=4):
    master, slave = pty.openpty()
    os.set_blocking(master, False)
    environment = os.environ.copy()
    environment.pop("BMS_TEST_MODEM_FAIL", None)
    if fail:
        environment["BMS_TEST_MODEM_FAIL"] = "1"
    command = [str(program), os.ttyname(slave), "127.0.0.1", "0", str(seconds)]
    if control:
        command.append("--control")
    process = subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        env=environment
    )
    try:
        port = wait_for_listen(process)
        with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
            yield process, master, client
    finally:
        if process.poll() is None:
            process.send_signal(signal.SIGCONT)
            process.send_signal(signal.SIGTERM)
        try:
            process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate(timeout=2)
        os.close(slave)
        os.close(master)


def serial_bytes(master, expected):
    data = b""
    deadline = time.monotonic() + 2.0
    while len(data) < len(expected):
        remaining = deadline - time.monotonic()
        require(remaining > 0, "未收到预期控制字节")
        ready, _, _ = select.select([master], [], [], remaining)
        require(ready, "等待控制字节超时")
        data += os.read(master, 64)
    require(data == expected, "UART字节不一致: {!r}".format(data))


def serial_silent(master):
    ready, _, _ = select.select([master], [], [], 0.1)
    require(not ready, "不应向UART写入字节")


def test_readonly(program):
    with launch(program, False) as (process, master, client):
        # 先等待真实termios配置和tcflush结束，再注入遥测。
        ready, _, _ = select.select([process.stderr], [], [], 2)
        require(ready, "没有串口就绪日志")
        errors = os.read(process.stderr.fileno(), 4096)
        deadline = time.monotonic() + 2.0
        while b"READY:" not in errors:
            remaining = deadline - time.monotonic()
            require(remaining > 0, "没有串口就绪日志")
            ready, _, _ = select.select([process.stderr], [], [], remaining)
            require(ready, "没有串口就绪日志")
            errors += os.read(process.stderr.fileno(), 4096)

        os.write(master, b"startup fragment\nBMS state=1, load=0\r\n")
        lines = Lines(client)
        require(lines.line() == b"BMS state=1, load=0", "只读遥测改变")
        client.sendall(b"CMD 1 START\n")
        serial_silent(master)
        process.send_signal(signal.SIGTERM)
        output, errors = process.communicate(timeout=2)
        require(process.returncode == 0 and output == b"", "只读退出异常")
    print("[PASS] default readonly telemetry unchanged and no UART control")


def test_control(program):
    with launch(program, True) as (process, master, client):
        lines = Lines(client)
        require(lines.line() == b"GW 2 CONTROL", "首帧不是控制能力声明")

        # UART完全静默时，TCP控制也必须工作。
        client.sendall(b"CMD 1 ST")
        serial_silent(master)
        client.sendall(b"OP\n")
        serial_bytes(master, b"T")
        require(lines.line() == b"ACK 1 UART_ACCEPTED", "STOP回执错误")

        client.sendall(b"CMD 2 START\nCMD 3 CLEAR\n")
        serial_bytes(master, b"SC")
        require(lines.line() == b"ACK 2 UART_ACCEPTED", "START回执错误")
        require(lines.line() == b"ACK 3 UART_ACCEPTED", "CLEAR回执错误")

        client.sendall(b"CMD 3 CLEAR\nCMD 4 STOP\r\n")
        require(lines.line() == b"ERR 3 STALE_ID", "重复编号未拒绝")
        require(lines.line() == b"ERR 4 BAD_REQUEST", "CR未拒绝")
        serial_silent(master)
        client.sendall(b"X" * 65 + b"\nCMD 4 CLEAR\n")
        serial_bytes(master, b"C")
        require(lines.line() == b"ERR 0 BAD_REQUEST", "超长行未拒绝")
        require(lines.line() == b"ACK 4 UART_ACCEPTED", "超长行后未恢复")

        os.write(master, b"startup fragment\nBMS state=1, load=0\n")
        require(lines.line() == b"BMS state=1, load=0", "控制模式遥测改变")

        # 暂停进程后注入请求和FIN，确保先观察断开，再决定是否写UART。
        process.send_signal(signal.SIGSTOP)
        deadline = time.monotonic() + 1.0
        while True:
            child, status = os.waitpid(process.pid, os.WNOHANG | os.WUNTRACED)
            if child == process.pid and os.WIFSTOPPED(status):
                break
            require(time.monotonic() < deadline, "未能暂停测试进程")
            time.sleep(0.005)
        client.sendall(b"CMD 5 START\n")
        client.shutdown(socket.SHUT_WR)
        time.sleep(0.05)
        process.send_signal(signal.SIGCONT)
        output, errors = process.communicate(timeout=2)
        require(process.returncode == 0 and output == b"", "断开收尾异常")
        serial_silent(master)
    print("[PASS] capability first, silent-UART control, framing and telemetry")


def test_configuration_failure(program):
    with launch(program, True, fail=True) as (process, master, client):
        output, errors = process.communicate(timeout=2)
        require(process.returncode != 0, "配置失败却返回成功")
        require(b"configure serial:" in errors, "没有配置失败记录")
        require(b"READY:" not in errors and output == b"", "错误宣称已就绪")
        client.settimeout(2)
        require(client.recv(4096) == b"", "配置失败发送了能力帧")
        serial_silent(master)
    print("[PASS] configuration failure publishes no control capability")


def test_control_deadline(program):
    with launch(program, True, seconds=1) as (process, master, client):
        require(Lines(client).line() == b"GW 2 CONTROL", "能力帧缺失")
        started = time.monotonic()
        output, errors = process.communicate(timeout=2)
        require(process.returncode == 0 and output == b"", "总预算退出异常")
        require(time.monotonic() - started < 2.0, "总预算没有生效")
        serial_silent(master)

    for stop_signal in (signal.SIGINT, signal.SIGTERM):
        with launch(program, True) as (process, master, client):
            require(Lines(client).line() == b"GW 2 CONTROL", "能力帧缺失")
            client.sendall(b"CMD 1 STA")  # 未完成请求不能在退出时执行。
            process.send_signal(stop_signal)
            output, errors = process.communicate(timeout=2)
            require(process.returncode == 0 and output == b"", "信号退出异常")
            serial_silent(master)
    print("[PASS] control total budget and signals add no implicit command")


def test_cli_control(program):
    missing = "/dev/pts/2147483647"
    for tail in (["--control", "1"], ["0", "--control"],
                 ["1", "--control", "--control"]):
        result = subprocess.run(
            [str(program), missing, "127.0.0.1", "0"] + tail,
            capture_output=True, timeout=2
        )
        require(result.returncode != 0 and b"LISTEN " not in result.stderr,
                "非法control参数启动了监听")

    result = subprocess.run(
        [str(program), missing, "127.0.0.1", "0", "1", "--control"],
        capture_output=True, timeout=3
    )
    require(result.returncode != 0 and b"WAIT_TIMEOUT" in result.stderr,
            "control等待预算失效")
    require(b"OPEN_SERIAL " not in result.stderr, "无客户端却打开串口")

    # 省略SECONDS也应接受--control；接入后因不存在的PTY立即失败。
    process = subprocess.Popen(
        [str(program), missing, "127.0.0.1", "0", "--control"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    try:
        port = wait_for_listen(process)
        with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
            output, errors = process.communicate(timeout=2)
            client.settimeout(2)
            require(client.recv(4096) == b"", "打开失败发送了能力帧")
        require(process.returncode != 0 and output == b"", "打开失败状态异常")
        require(b"open serial:" in errors, "省略SECONDS路径没有到达设备打开")
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate(timeout=2)
    print("[PASS] control CLI optional seconds, validation and no-client budget")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", required=True, type=Path)
    program = parser.parse_args().program.resolve()
    require("pty-test" in program.name, "必须使用测试专用网关")
    test_cli_control(program)
    test_readonly(program)
    test_control(program)
    test_configuration_failure(program)
    test_control_deadline(program)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())