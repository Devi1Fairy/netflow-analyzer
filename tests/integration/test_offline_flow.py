#!/usr/bin/env python3

"""
验证离线PCAP分析和双向流聚合的完整命令行流程。

脚本不依赖Scapy等第三方库，只使用Python标准库构造一个
包含6个Ethernet/IPv4/ICMP数据包的PCAP文件。

CTest会向脚本传入待测试程序的绝对路径。
"""

import argparse
import socket
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    """解析CTest传入的命令行参数。"""

    parser = argparse.ArgumentParser(
        description="Test offline PCAP flow aggregation."
    )

    parser.add_argument(
        "--program",
        required=True,
        type=Path,
        help="Path to the netflow-analyzer executable.",
    )

    parser.add_argument(
        "--work-dir",
        required=True,
        type=Path,
        help="Writable directory used for temporary test files.",
    )

    return parser.parse_args()


def build_icmp_frame(
    source_mac: bytes,
    destination_mac: bytes,
    source_ipv4: str,
    destination_ipv4: str,
    icmp_type: int,
    sequence: int,
) -> bytes:
    """
    构造一条Ethernet II / IPv4 / ICMP Echo数据包。

    当前C解析器只读取校验和字段，不验证校验和，因此测试报文中的
    IPv4和ICMP校验和使用0。其他长度、协议号和地址字段均符合
    本次解析所需的格式。
    """

    if len(source_mac) != 6 or len(destination_mac) != 6:
        raise ValueError("MAC addresses must contain exactly 6 bytes")

    if not 0 <= icmp_type <= 0xFF:
        raise ValueError("ICMP type is outside uint8_t range")

    if not 0 <= sequence <= 0xFFFF:
        raise ValueError("ICMP sequence is outside uint16_t range")

    ethernet_header = (
        destination_mac
        + source_mac
        + struct.pack("!H", 0x0800)
    )

    icmp_payload = b"TEST"

    icmp_header = struct.pack(
        "!BBHHH",
        icmp_type,     # Type：8为请求，0为响应。
        0,             # Code：Echo消息固定为0。
        0,             # Checksum：当前解析器暂不验证。
        0x1234,        # Identifier。
        sequence,      # Sequence。
    )

    icmp_message = icmp_header + icmp_payload

    ipv4_total_length = 20 + len(icmp_message)

    ipv4_header = struct.pack(
        "!BBHHHBBH4s4s",
        0x45,                              # Version=4，IHL=5。
        0,                                 # DSCP和ECN。
        ipv4_total_length,                 # IPv4总长度。
        sequence,                          # Identification。
        0x4000,                            # DF=true，不分片。
        64,                                # TTL。
        1,                                 # Protocol=ICMP。
        0,                                 # Header checksum。
        socket.inet_aton(source_ipv4),
        socket.inet_aton(destination_ipv4),
    )

    return ethernet_header + ipv4_header + icmp_message


def write_test_pcap(pcap_path: Path) -> None:
    """
    写入包含3组ICMP请求和响应的PCAP文件。

    PCAP使用小端序、微秒时间戳和Ethernet链路类型。
    """

    endpoint_a_mac = bytes.fromhex("001122334455")
    endpoint_b_mac = bytes.fromhex("66778899aabb")

    packets: list[tuple[int, int, bytes]] = []

    base_timestamp = 1_700_000_000

    for sequence in range(1, 4):
        request = build_icmp_frame(
            source_mac=endpoint_a_mac,
            destination_mac=endpoint_b_mac,
            source_ipv4="10.0.0.1",
            destination_ipv4="10.0.0.2",
            icmp_type=8,
            sequence=sequence,
        )

        reply = build_icmp_frame(
            source_mac=endpoint_b_mac,
            destination_mac=endpoint_a_mac,
            source_ipv4="10.0.0.2",
            destination_ipv4="10.0.0.1",
            icmp_type=0,
            sequence=sequence,
        )

        packets.append(
            (
                base_timestamp + sequence,
                100,
                request,
            )
        )

        packets.append(
            (
                base_timestamp + sequence,
                200,
                reply,
            )
        )

    with pcap_path.open("wb") as pcap_file:
        # PCAP全局文件头。
        pcap_file.write(
            struct.pack(
                "<IHHIIII",
                0xA1B2C3D4,  # Magic number。
                2,           # Major version。
                4,           # Minor version。
                0,           # This zone。
                0,           # Timestamp accuracy。
                65535,       # Snapshot length。
                1,           # Link type：Ethernet。
            )
        )

        for (
            timestamp_seconds,
            timestamp_microseconds,
            frame,
        ) in packets:
            frame_length = len(frame)

            # 每条数据包的PCAP记录头。
            pcap_file.write(
                struct.pack(
                    "<IIII",
                    timestamp_seconds,
                    timestamp_microseconds,
                    frame_length,
                    frame_length,
                )
            )

            pcap_file.write(frame)

def require_text(output: str, expected: str) -> None:
    """要求程序输出中包含指定文本。"""

    if expected not in output:
        raise RuntimeError(
            f"missing expected output: {expected!r}"
        )


def run_acceptance_test(
    program: Path,
    work_dir: Path,
) -> None:
    """生成PCAP、运行最终程序并检查结果。"""

    if not program.is_file():
        raise RuntimeError(
            f"analyzer executable does not exist: {program}"
        )

    if not work_dir.is_dir():
        raise RuntimeError(
            f"work directory does not exist: {work_dir}"
        )

    with tempfile.TemporaryDirectory(
        prefix="offline-flow-",
        dir=work_dir,
    ) as temporary_directory:
        pcap_path = (
            Path(temporary_directory)
            / "offline-flow-test.pcap"
        )

        write_test_pcap(pcap_path)

        completed_process = subprocess.run(
            [
                str(program),
                "--read",
                str(pcap_path),
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

        if completed_process.returncode != 0:
            raise RuntimeError(
                "analyzer returned non-zero status\n"
                f"exit code: {completed_process.returncode}\n"
                f"stdout:\n{completed_process.stdout}\n"
                f"stderr:\n{completed_process.stderr}"
            )

        output = completed_process.stdout

        packet_preview_lines = [
            line
            for line in output.splitlines()
            if line.startswith("Packet ")
        ]

        if len(packet_preview_lines) != 5:
            raise RuntimeError(
                "expected exactly 5 packet preview lines, "
                f"found {len(packet_preview_lines)}"
            )

        require_text(output, "Total packets: 6")
        require_text(output, "Previewed packets: 5")
        require_text(output, "Flow summary: 1 flow(s)")

        require_text(
            output,
            "protocol=ICMP "
            "endpoint_a=10.0.0.1:0 "
            "endpoint_b=10.0.0.2:0",
        )

        require_text(output, "a_to_b_packets=3")
        require_text(output, "a_to_b_captured_bytes=138")
        require_text(output, "a_to_b_wire_bytes=138")

        require_text(output, "b_to_a_packets=3")
        require_text(output, "b_to_a_captured_bytes=138")
        require_text(output, "b_to_a_wire_bytes=138")

        require_text(
            output,
            "first_seen=1700000001.000100",
        )

        require_text(
            output,
            "last_seen=1700000003.000200",
        )


def main() -> int:
    """验收测试程序入口。"""

    arguments = parse_arguments()

    try:
        run_acceptance_test(
            program=arguments.program.resolve(),
            work_dir=arguments.work_dir.resolve(),
        )
    except (
        OSError,
        RuntimeError,
        subprocess.TimeoutExpired,
        ValueError,
    ) as error:
        print(
            f"[FAIL] offline flow acceptance: {error}",
            file=sys.stderr,
        )
        return 1

    print("[PASS] offline flow acceptance")
    return 0


if __name__ == "__main__":
    sys.exit(main())            