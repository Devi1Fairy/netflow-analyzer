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
from typing import List, Tuple


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

def write_pcap(
    pcap_path: Path,
    packets: List[Tuple[int, int, bytes]],
) -> None:
    """
    把给定数据包记录写成小端、微秒时间戳的Ethernet PCAP。
    """

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

def write_test_pcap(pcap_path: Path) -> None:
    """
    写入包含3组ICMP请求和响应的PCAP文件。

    PCAP使用小端序、微秒时间戳和Ethernet链路类型。
    """

    endpoint_a_mac = bytes.fromhex("001122334455")
    endpoint_b_mac = bytes.fromhex("66778899aabb")

    packets: List[Tuple[int, int, bytes]] = []

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

    write_pcap(pcap_path, packets)

def write_processing_results_pcap(
    pcap_path: Path,
) -> None:
    """
    构造覆盖全部应用处理结果的确定性PCAP。

    内容包括：

    - 1个截断Ethernet帧；
    - 1个当前不支持的ARP Ethernet帧；
    - 1个IPv4版本字段错误的畸形帧；
    - 257个不同的合法ICMP流。

    流表容量为256，因此最后一个合法ICMP包应被记为
    FLOW_REJECTED，而不是导致程序退出。
    """

    source_mac = bytes.fromhex("001122334455")
    destination_mac = bytes.fromhex("66778899aabb")

    packets: List[Tuple[int, int, bytes]] = []
    base_timestamp = 1_700_001_000

    # 只有10字节，连14字节Ethernet头都不完整。
    truncated_frame = b"\x00" * 10

    packets.append(
        (
            base_timestamp,
            100,
            truncated_frame,
        )
    )

    # Ethernet头完整，但EtherType为ARP；项目当前只处理IPv4。
    unsupported_frame = (
        destination_mac
        + source_mac
        + struct.pack("!H", 0x0806)
    )

    packets.append(
        (
            base_timestamp + 1,
            100,
            unsupported_frame,
        )
    )

    # 先构造合法IPv4/ICMP帧，再把IPv4版本从4改成6。
    malformed_frame = bytearray(
        build_icmp_frame(
            source_mac=source_mac,
            destination_mac=destination_mac,
            source_ipv4="198.51.100.1",
            destination_ipv4="198.51.100.2",
            icmp_type=8,
            sequence=1,
        )
    )

    # Ethernet头是14字节，因此索引14是IPv4头第一个字节。
    # 0x65表示Version=6、IHL=5，与EtherType=IPv4矛盾。
    malformed_frame[14] = 0x65

    packets.append(
        (
            base_timestamp + 2,
            100,
            bytes(malformed_frame),
        )
    )

    # 生成257条不同的ICMP流。
    for index in range(257):
        third_octet = index // 250
        fourth_octet = index % 250 + 1

        unique_source_ipv4 = (
            f"10.1.{third_octet}.{fourth_octet}"
        )

        frame = build_icmp_frame(
            source_mac=source_mac,
            destination_mac=destination_mac,
            source_ipv4=unique_source_ipv4,
            destination_ipv4="192.0.2.1",
            icmp_type=8,
            sequence=index + 1,
        )

        packets.append(
            (
                base_timestamp + 3 + index,
                100,
                frame,
            )
        )

    write_pcap(pcap_path, packets)

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

        csv_path = (
            Path(temporary_directory)
            / "offline-flow-result.csv"
        )

        write_test_pcap(pcap_path)

        completed_process = subprocess.run(
            [
                str(program),
                "--read",
                str(pcap_path),
                "--csv",
                str(csv_path),
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
        require_text(
            output,
            "Processing results: "
            "complete=6 "
            "truncated=0 "
            "malformed=0 "
            "unsupported=0 "
            "flow_rejected=0",
        )

        require_text(
            output,
            "Flow table probes: "
            "operations=6 "
            "inspected_slots=6 "
            "average=1.00 "
            "maximum=1 "
            "saturated=false",
        )

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

        require_text(
            output,
            f"CSV output: {csv_path}",
        )

        if not csv_path.is_file():
            raise RuntimeError(
                f"CSV output was not created: {csv_path}"
            )

        csv_lines = csv_path.read_text(
            encoding="utf-8"
        ).splitlines()

        expected_csv_lines = [
            (
                "protocol,"
                "endpoint_a_ip,"
                "endpoint_a_port,"
                "endpoint_b_ip,"
                "endpoint_b_port,"
                "a_to_b_packets,"
                "a_to_b_captured_bytes,"
                "a_to_b_wire_bytes,"
                "b_to_a_packets,"
                "b_to_a_captured_bytes,"
                "b_to_a_wire_bytes,"
                "first_seen_seconds,"
                "first_seen_microseconds,"
                "last_seen_seconds,"
                "last_seen_microseconds"
            ),
            (
                "1,10.0.0.1,0,10.0.0.2,0,"
                "3,138,138,"
                "3,138,138,"
                "1700000001,100,"
                "1700000003,200"
            ),
        ]

        if csv_lines != expected_csv_lines:
            raise RuntimeError(
                "CSV output does not match expected records\n"
                f"expected: {expected_csv_lines!r}\n"
                f"actual: {csv_lines!r}"
            )

        # 保存第一次成功生成的CSV内容。
        original_csv_content = csv_path.read_bytes()

        # 再次使用相同CSV路径运行程序。
        # 因为输出文件已经存在，本次运行必须失败。
        repeated_process = subprocess.run(
            [
                str(program),
                "--read",
                str(pcap_path),
                "--csv",
                str(csv_path),
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

        if repeated_process.returncode == 0:
            raise RuntimeError(
                "analyzer unexpectedly overwrote an existing CSV file"
            )

        # 拒绝覆盖后，原CSV内容必须保持不变。
        if csv_path.read_bytes() != original_csv_content:
            raise RuntimeError(
                "existing CSV content changed after overwrite rejection"
            )

def run_processing_results_test(
    program: Path,
    work_dir: Path,
) -> None:
    """
    验证异常包分类和流表满载后的继续运行策略。
    """

    with tempfile.TemporaryDirectory(
        prefix="offline-processing-results-",
        dir=work_dir,
    ) as temporary_directory:
        pcap_path = (
            Path(temporary_directory)
            / "processing-results-test.pcap"
        )

        write_processing_results_pcap(pcap_path)

        completed_process = subprocess.run(
            [
                str(program),
                "--read",
                str(pcap_path),
            ],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )

        if completed_process.returncode != 0:
            raise RuntimeError(
                "processing-results analysis returned "
                "non-zero status\n"
                f"exit code: {completed_process.returncode}\n"
                f"stdout:\n{completed_process.stdout}\n"
                f"stderr:\n{completed_process.stderr}"
            )

        output = completed_process.stdout

        require_text(output, "Total packets: 260")
        require_text(output, "Previewed packets: 5")

        require_text(
            output,
            "Processing results: "
            "complete=256 "
            "truncated=1 "
            "malformed=1 "
            "unsupported=1 "
            "flow_rejected=1",
        )

        require_text(
            output,
            "Flow table probes: operations=257 ",
        )

        require_text(
            output,
            "maximum=256 saturated=false",
        )

        require_text(
            output,
            "Flow summary: 256 flow(s)",
        )

def main() -> int:
    """验收测试程序入口。"""

    arguments = parse_arguments()

    try:
        run_acceptance_test(
            program=arguments.program.resolve(),
            work_dir=arguments.work_dir.resolve(),
        )

        run_processing_results_test(
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