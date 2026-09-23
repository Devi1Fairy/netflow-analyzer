#!/usr/bin/env python3

"""验证CTU-13流身份规范化规则。"""

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
        "StartTime": "2011/08/16 13:51:24.049047",
        "Dur": "1.250001",
        "Proto": "tcp",
        "SrcAddr": "198.51.100.20",
        "Sport": "443",
        "DstAddr": "192.0.2.10",
        "Dport": "55000",
    }


def require(
    condition: bool,
    message: str,
) -> None:
    """要求条件成立。"""

    if not condition:
        raise RuntimeError(message)


def require_value_error(
    operation: Callable[[], object],
    message: str,
) -> None:
    """要求操作抛出ValueError。"""

    try:
        operation()
    except ValueError:
        return

    raise RuntimeError(message)


def run_tests(scripts_dir: Path) -> None:
    """执行确定性的规范化测试。"""

    if not scripts_dir.is_dir():
        raise RuntimeError(
            f"scripts directory does not exist: {scripts_dir}"
        )

    sys.path.insert(0, str(scripts_dir))

    import ctu13_flow_identity as identity

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
        == 1313495484049047,
        "Scenario 7 timezone conversion is incorrect",
    )

    require(
        normalized.end_time_microseconds
        == normalized.start_time_microseconds
        + 1_250_001,
        "flow end time is incorrect",
    )

    require(
        normalized.endpoint_a.ipv4_address
        == int(IPv4Address("192.0.2.10"))
        and normalized.endpoint_a.port == 55000,
        "endpoint_a normalization is incorrect",
    )

    require(
        normalized.endpoint_b.ipv4_address
        == int(IPv4Address("198.51.100.20"))
        and normalized.endpoint_b.port == 443,
        "endpoint_b normalization is incorrect",
    )

    reversed_row = dict(row)

    reversed_row.update(
        {
            "SrcAddr": row["DstAddr"],
            "Sport": row["Dport"],
            "DstAddr": row["SrcAddr"],
            "Dport": row["Sport"],
        }
    )

    reversed_identity = (
        identity.normalize_flow_identity(
            reversed_row
        )
    )

    require(
        reversed_identity == normalized,
        "reversing endpoints changed the canonical identity",
    )

    icmp_row = build_row()

    icmp_row.update(
        {
            "Proto": "icmp",
            "Sport": "",
            "Dport": "",
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

    unsupported_row = build_row()
    unsupported_row["Proto"] = "arp"

    require(
        identity.normalize_flow_identity(
            unsupported_row
        ) is None,
        "unsupported ARP row was not rejected",
    )

    invalid_port_row = build_row()
    invalid_port_row["Sport"] = "70000"

    require_value_error(
        lambda: identity.normalize_flow_identity(
            invalid_port_row
        ),
        "out-of-range port was accepted",
    )

    invalid_duration_row = build_row()
    invalid_duration_row["Dur"] = "0.0000001"

    require_value_error(
        lambda: identity.normalize_flow_identity(
            invalid_duration_row
        ),
        "sub-microsecond duration was accepted",
    )

    print(
        "[PASS] CTU-13 flow identity normalization"
    )


def main() -> int:
    """测试程序入口。"""

    arguments = parse_arguments()

    try:
        run_tests(arguments.scripts_dir)
    except (ImportError, OSError, RuntimeError) as error:
        print(
            f"[FAIL] {error}",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())