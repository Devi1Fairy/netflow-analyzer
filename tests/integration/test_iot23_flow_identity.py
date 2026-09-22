#!/usr/bin/env python3

"""验证IoT-23流身份规范化规则。"""

import argparse
import sys
from ipaddress import IPv4Address
from pathlib import Path
from typing import Callable, Dict


def parse_arguments() -> argparse.Namespace:
    """解析CMake传入的scripts目录。"""

    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--scripts-dir",
        required=True,
        type=Path,
    )

    return parser.parse_args()


def build_row() -> Dict[str, str]:
    """构造一条方向与规范化顺序相反的TCP记录。"""

    return {
        "ts": "1526756261.866500",
        "id.orig_h": "200.168.87.203",
        "id.orig_p": "59353",
        "id.resp_h": "192.168.2.5",
        "id.resp_p": "38792",
        "proto": "tcp",
        "duration": "2.998333",
        "orig_pkts": "3",
        "resp_pkts": "0",
    }


def require(
    condition: bool,
    message: str,
) -> None:
    """要求条件成立，否则测试失败。"""

    if not condition:
        raise RuntimeError(message)


def require_value_error(
    operation: Callable[[], object],
    message: str,
) -> None:
    """要求指定操作抛出ValueError。"""

    try:
        operation()
    except ValueError:
        return

    raise RuntimeError(message)


def run_tests(scripts_dir: Path) -> None:
    """执行确定性的IoT-23身份规范化测试。"""

    if not scripts_dir.is_dir():
        raise RuntimeError(
            f"scripts directory does not exist: "
            f"{scripts_dir}"
        )

    sys.path.insert(0, str(scripts_dir))

    import iot23_flow_identity as identity

    row = build_row()

    normalized = identity.normalize_flow_identity(
        row
    )

    require(
        normalized is not None,
        "supported TCP row was rejected",
    )

    require(
        normalized.protocol == 6,
        "TCP protocol number is incorrect",
    )

    require(
        normalized.start_time_microseconds
        == 1526756261866500,
        "Unix timestamp conversion is incorrect",
    )

    require(
        normalized.end_time_microseconds
        == 1526756264864833,
        "flow end time is incorrect",
    )

    require(
        normalized.endpoint_a.ipv4_address
        == int(IPv4Address("192.168.2.5"))
        and normalized.endpoint_a.port == 38792,
        "endpoint_a normalization is incorrect",
    )

    require(
        normalized.endpoint_b.ipv4_address
        == int(IPv4Address("200.168.87.203"))
        and normalized.endpoint_b.port == 59353,
        "endpoint_b normalization is incorrect",
    )

    # 交换Zeek的originator和responder后，
    # 规范化双向身份必须保持不变。
    reversed_row = dict(row)

    reversed_row.update(
        {
            "id.orig_h": row["id.resp_h"],
            "id.orig_p": row["id.resp_p"],
            "id.resp_h": row["id.orig_h"],
            "id.resp_p": row["id.orig_p"],
        }
    )

    reversed_identity = (
        identity.normalize_flow_identity(
            reversed_row
        )
    )

    require(
        reversed_identity == normalized,
        "reversing endpoints changed canonical identity",
    )

    # UDP使用协议号17，并继续保留端口。
    udp_row = build_row()
    udp_row["proto"] = "udp"

    udp_identity = identity.normalize_flow_identity(
        udp_row
    )

    require(
        udp_identity is not None
        and udp_identity.protocol == 17,
        "UDP protocol number is incorrect",
    )

    # Zeek可能把ICMP类型和代码放在端口字段中，
    # C端流键必须统一归零。
    icmp_row = build_row()

    icmp_row.update(
        {
            "proto": "icmp",
            "id.orig_p": "8",
            "id.resp_p": "0",
            "duration": "0.010000",
            "orig_pkts": "1",
            "resp_pkts": "1",
        }
    )

    icmp_identity = (
        identity.normalize_flow_identity(
            icmp_row
        )
    )

    require(
        icmp_identity is not None
        and icmp_identity.protocol == 1
        and icmp_identity.endpoint_a.port == 0
        and icmp_identity.endpoint_b.port == 0,
        "ICMP ports were not normalized to zero",
    )

    # 不支持的协议属于有效数据，但无法进入当前C流模型。
    unsupported_row = build_row()
    unsupported_row["proto"] = "arp"

    require(
        identity.normalize_flow_identity(
            unsupported_row
        ) is None,
        "unsupported protocol was not rejected",
    )

    # 单包且duration未设置时，表示为零时长点区间。
    single_packet_row = build_row()

    single_packet_row.update(
        {
            "duration": "-",
            "orig_pkts": "1",
            "resp_pkts": "0",
        }
    )

    single_packet_identity = (
        identity.normalize_flow_identity(
            single_packet_row
        )
    )

    require(
        single_packet_identity is not None
        and (
            single_packet_identity
            .start_time_microseconds
            == single_packet_identity
            .end_time_microseconds
        ),
        "single-packet unset duration was not zero",
    )

    # 多包记录缺少duration时，不能猜测结束时间。
    ambiguous_duration_row = dict(
        single_packet_row
    )
    ambiguous_duration_row["orig_pkts"] = "2"

    require_value_error(
        lambda: identity.normalize_flow_identity(
            ambiguous_duration_row
        ),
        "multi-packet unset duration was accepted",
    )

    invalid_port_row = build_row()
    invalid_port_row["id.orig_p"] = "70000"

    require_value_error(
        lambda: identity.normalize_flow_identity(
            invalid_port_row
        ),
        "out-of-range port was accepted",
    )

    zero_packet_row = build_row()

    zero_packet_row.update(
        {
            "orig_pkts": "0",
            "resp_pkts": "0",
        }
    )

    require_value_error(
        lambda: identity.normalize_flow_identity(
            zero_packet_row
        ),
        "zero-packet flow was accepted",
    )

    submicrosecond_timestamp_row = build_row()
    submicrosecond_timestamp_row["ts"] = (
        "1526756261.8665001"
    )

    require_value_error(
        lambda: identity.normalize_flow_identity(
            submicrosecond_timestamp_row
        ),
        "sub-microsecond timestamp was accepted",
    )

    submicrosecond_duration_row = build_row()
    submicrosecond_duration_row["duration"] = (
        "0.0000001"
    )

    require_value_error(
        lambda: identity.normalize_flow_identity(
            submicrosecond_duration_row
        ),
        "sub-microsecond duration was accepted",
    )

    negative_duration_row = build_row()
    negative_duration_row["duration"] = "-1.0"

    require_value_error(
        lambda: identity.normalize_flow_identity(
            negative_duration_row
        ),
        "negative duration was accepted",
    )

    missing_address_row = build_row()
    del missing_address_row["id.orig_h"]

    require_value_error(
        lambda: identity.normalize_flow_identity(
            missing_address_row
        ),
        "missing IPv4 address was accepted",
    )

    print(
        "[PASS] IoT-23 flow identity normalization"
    )


def main() -> int:
    """测试程序入口。"""

    arguments = parse_arguments()

    try:
        run_tests(arguments.scripts_dir)
    except (
        ImportError,
        OSError,
        RuntimeError,
    ) as error:
        print(
            f"[FAIL] {error}",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())