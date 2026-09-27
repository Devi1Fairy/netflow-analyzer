#!/usr/bin/env python3

"""解析Netflow Analyzer导出的普通流CSV；不解释数据集标签。"""

from ipaddress import AddressValueError, IPv4Address
from typing import Tuple

EXPECTED_FLOW_COLUMNS = (
    "protocol",
    "tcp_state",
    "endpoint_a_ip",
    "endpoint_a_port",
    "endpoint_b_ip",
    "endpoint_b_port",
    "a_to_b_packets",
    "a_to_b_captured_bytes",
    "a_to_b_wire_bytes",
    "b_to_a_packets",
    "b_to_a_captured_bytes",
    "b_to_a_wire_bytes",
    "first_seen_seconds",
    "first_seen_microseconds",
    "last_seen_seconds",
    "last_seen_microseconds",
)

SUPPORTED_PROTOCOLS = {
    1,
    6,
    17,
}

MICROSECONDS_PER_SECOND = 1_000_000

FlowKey = Tuple[int, int, int, int, int]

def require_text(
    row,
    column: str,
    source_name: str,
    line_number: int,
) -> str:
    """读取一个必需且非空的CSV字段。"""

    if column not in row or row[column] is None:
        raise ValueError(
            f"{source_name} line {line_number}: "
            f"missing field {column}"
        )

    value = row[column].strip()

    if not value:
        raise ValueError(
            f"{source_name} line {line_number}: "
            f"empty field {column}"
        )

    return value


def validate_row_shape(
    row,
    expected_columns,
    source_name: str,
    line_number: int,
) -> None:
    """验证CSV记录没有多余或缺失字段。"""

    if None in row:
        raise ValueError(
            f"{source_name} line {line_number}: "
            "too many fields"
        )

    if any(
        row[column] is None
        for column in expected_columns
    ):
        raise ValueError(
            f"{source_name} line {line_number}: "
            "missing fields"
        )


def parse_unsigned_integer(
    raw_value: str,
    column: str,
    maximum: int,
) -> int:
    """解析指定上限内的十进制无符号整数。"""

    text = raw_value.strip()

    if not text.isdecimal():
        raise ValueError(
            f"invalid {column}: {raw_value!r}"
        )

    value = int(text, 10)

    if value > maximum:
        raise ValueError(
            f"{column} is out of range: {value}"
        )

    return value

def parse_flow_ipv4_address(raw_value: str) -> int:
    """把流CSV中的点分十进制IPv4转换为32位数值。"""

    try:
        return int(IPv4Address(raw_value.strip()))
    except AddressValueError as error:
        raise ValueError(
            f"invalid flow CSV IPv4 address: {raw_value!r}"
        ) from error

def parse_flow_key(
    row,
    line_number: int,
) -> FlowKey:
    """解析并验证C流CSV中的规范化五元组。"""

    protocol = parse_unsigned_integer(
        require_text(
            row,
            "protocol",
            "flow CSV",
            line_number,
        ),
        "protocol",
        255,
    )

    if protocol not in SUPPORTED_PROTOCOLS:
        raise ValueError(
            f"unsupported flow CSV protocol: {protocol}"
        )

    endpoint_a_address = parse_flow_ipv4_address(
        require_text(
            row,
            "endpoint_a_ip",
            "flow CSV",
            line_number,
        )
    )

    endpoint_b_address = parse_flow_ipv4_address(
        require_text(
            row,
            "endpoint_b_ip",
            "flow CSV",
            line_number,
        )
    )

    endpoint_a_port = parse_unsigned_integer(
        require_text(
            row,
            "endpoint_a_port",
            "flow CSV",
            line_number,
        ),
        "endpoint_a_port",
        65535,
    )

    endpoint_b_port = parse_unsigned_integer(
        require_text(
            row,
            "endpoint_b_port",
            "flow CSV",
            line_number,
        ),
        "endpoint_b_port",
        65535,
    )

    if (
        protocol == 1
        and (
            endpoint_a_port != 0
            or endpoint_b_port != 0
        )
    ):
        raise ValueError(
            "ICMP flow CSV ports must both be zero"
        )

    endpoint_a = (
        endpoint_a_address,
        endpoint_a_port,
    )

    endpoint_b = (
        endpoint_b_address,
        endpoint_b_port,
    )

    if endpoint_a > endpoint_b:
        raise ValueError(
            "flow CSV endpoints are not canonical"
        )

    return (
        protocol,
        endpoint_a_address,
        endpoint_a_port,
        endpoint_b_address,
        endpoint_b_port,
    )


def parse_flow_timestamp(
    row,
    prefix: str,
    line_number: int,
) -> int:
    """把C流CSV的秒和微秒字段合成为整数微秒。"""

    seconds = parse_unsigned_integer(
        require_text(
            row,
            f"{prefix}_seconds",
            "flow CSV",
            line_number,
        ),
        f"{prefix}_seconds",
        (1 << 63) - 1,
    )

    microseconds = parse_unsigned_integer(
        require_text(
            row,
            f"{prefix}_microseconds",
            "flow CSV",
            line_number,
        ),
        f"{prefix}_microseconds",
        999999,
    )

    return (
        seconds * MICROSECONDS_PER_SECOND
        + microseconds
    )
