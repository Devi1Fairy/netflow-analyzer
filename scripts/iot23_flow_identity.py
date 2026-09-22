#!/usr/bin/env python3

"""
把IoT-23 Zeek标签记录规范化为项目使用的双向流身份。

本模块只负责字段解析与身份规范化，不读取文件、
不执行标签匹配，也不训练模型。
"""

from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from ipaddress import AddressValueError, IPv4Address
from typing import Mapping, Optional


MICROSECONDS_PER_SECOND = 1_000_000
MAX_TIMESTAMP_MICROSECONDS = (1 << 63) - 1

PROTOCOL_NUMBERS = {
    "icmp": 1,
    "tcp": 6,
    "udp": 17,
}


@dataclass(frozen=True)
class FlowEndpoint:
    """
    规范化流端点。

    ipv4_address使用与C端相同的32位数值语义。
    TCP和UDP端口范围为0到65535，ICMP固定为0。
    """

    ipv4_address: int
    port: int


@dataclass(frozen=True)
class Iot23FlowIdentity:
    """
    一条IoT-23标签记录的双向流身份。

    endpoint_a和endpoint_b只表示规范化排序，
    不表示Zeek的originator和responder。

    时间统一保存为Unix微秒整数。
    """

    protocol: int
    endpoint_a: FlowEndpoint
    endpoint_b: FlowEndpoint
    start_time_microseconds: int
    end_time_microseconds: int


def _require_text(
    row: Mapping[str, Optional[str]],
    column: str,
) -> str:
    """读取一个必需字段并移除首尾空白。"""

    if column not in row or row[column] is None:
        raise ValueError(
            f"missing IoT-23 column: {column}"
        )

    value = row[column].strip()

    if not value:
        raise ValueError(
            f"empty IoT-23 column: {column}"
        )

    return value


def parse_decimal_seconds_microseconds(
    raw_value: str,
    field_name: str,
) -> int:
    """
    把十进制秒精确转换为整数微秒。

    Decimal避免使用float造成二进制舍入误差。
    输入不能为负数、无限值、NaN或亚微秒精度。
    """

    try:
        seconds = Decimal(raw_value.strip())
    except InvalidOperation as error:
        raise ValueError(
            f"invalid IoT-23 {field_name}: "
            f"{raw_value!r}"
        ) from error

    if not seconds.is_finite() or seconds < 0:
        raise ValueError(
            f"invalid IoT-23 {field_name}: "
            f"{raw_value!r}"
        )

    scaled_value = (
        seconds * MICROSECONDS_PER_SECOND
    )

    integral_value = (
        scaled_value.to_integral_value()
    )

    if scaled_value != integral_value:
        raise ValueError(
            f"IoT-23 {field_name} has "
            "sub-microsecond precision: "
            f"{raw_value!r}"
        )

    result = int(integral_value)

    if result > MAX_TIMESTAMP_MICROSECONDS:
        raise ValueError(
            f"IoT-23 {field_name} is too large: "
            f"{raw_value!r}"
        )

    return result


def parse_packet_count(
    raw_value: str,
    column: str,
) -> int:
    """解析非负十进制数据包数量。"""

    text = raw_value.strip()

    if not text.isdecimal():
        raise ValueError(
            f"invalid IoT-23 {column}: "
            f"{raw_value!r}"
        )

    return int(text, 10)


def parse_duration_microseconds(
    raw_value: str,
    total_packet_count: int,
) -> int:
    """
    解析Zeek可选duration字段。

    本场景中所有duration为"-"的记录都只有一个包。
    单包记录的第一包和最后一包相同，因此表示为零时长。

    如果多包记录缺少duration，则无法可靠推导结束时间，
    必须拒绝而不是猜测。
    """

    text = raw_value.strip()

    if text == "-":
        if total_packet_count != 1:
            raise ValueError(
                "unset IoT-23 duration requires "
                "exactly one packet"
            )

        return 0

    return parse_decimal_seconds_microseconds(
        text,
        "duration",
    )


def parse_ipv4_address(raw_value: str) -> int:
    """把点分十进制IPv4地址转换为32位整数。"""

    try:
        return int(IPv4Address(raw_value.strip()))
    except AddressValueError as error:
        raise ValueError(
            "invalid IoT-23 IPv4 address: "
            f"{raw_value!r}"
        ) from error


def parse_port(
    raw_value: str,
    column: str,
) -> int:
    """解析TCP或UDP十进制端口。"""

    text = raw_value.strip()

    if not text.isdecimal():
        raise ValueError(
            f"invalid IoT-23 {column}: "
            f"{raw_value!r}"
        )

    port = int(text, 10)

    if port > 65535:
        raise ValueError(
            f"IoT-23 {column} is out of range: "
            f"{port}"
        )

    return port


def normalize_flow_identity(
    row: Mapping[str, Optional[str]],
) -> Optional[Iot23FlowIdentity]:
    """
    规范化一条IoT-23 Zeek连接记录。

    返回：
        支持TCP、UDP或ICMP时返回新的身份对象；
        协议不受当前分析器支持时返回None。

    异常：
        必需字段缺失、数值非法或无法可靠推导时间区间时，
        抛出ValueError。
    """

    protocol_name = _require_text(
        row,
        "proto",
    ).lower()

    protocol = PROTOCOL_NUMBERS.get(
        protocol_name
    )

    if protocol is None:
        return None

    source_address = parse_ipv4_address(
        _require_text(row, "id.orig_h")
    )

    destination_address = parse_ipv4_address(
        _require_text(row, "id.resp_h")
    )

    if protocol == 1:
        # Zeek把ICMP类型和代码放在端口字段中。
        # C端流键不使用该语义，因此统一归零。
        source_port = 0
        destination_port = 0
    else:
        source_port = parse_port(
            _require_text(row, "id.orig_p"),
            "id.orig_p",
        )

        destination_port = parse_port(
            _require_text(row, "id.resp_p"),
            "id.resp_p",
        )

    source_endpoint = FlowEndpoint(
        ipv4_address=source_address,
        port=source_port,
    )

    destination_endpoint = FlowEndpoint(
        ipv4_address=destination_address,
        port=destination_port,
    )

    source_sort_key = (
        source_endpoint.ipv4_address,
        source_endpoint.port,
    )

    destination_sort_key = (
        destination_endpoint.ipv4_address,
        destination_endpoint.port,
    )

    if source_sort_key <= destination_sort_key:
        endpoint_a = source_endpoint
        endpoint_b = destination_endpoint
    else:
        endpoint_a = destination_endpoint
        endpoint_b = source_endpoint

    originator_packet_count = parse_packet_count(
        _require_text(row, "orig_pkts"),
        "orig_pkts",
    )

    responder_packet_count = parse_packet_count(
        _require_text(row, "resp_pkts"),
        "resp_pkts",
    )

    total_packet_count = (
        originator_packet_count
        + responder_packet_count
    )

    if total_packet_count == 0:
        raise ValueError(
            "IoT-23 flow contains no packets"
        )

    start_time_microseconds = (
        parse_decimal_seconds_microseconds(
            _require_text(row, "ts"),
            "ts",
        )
    )

    duration_microseconds = (
        parse_duration_microseconds(
            _require_text(row, "duration"),
            total_packet_count,
        )
    )

    end_time_microseconds = (
        start_time_microseconds
        + duration_microseconds
    )

    if (
        end_time_microseconds
        > MAX_TIMESTAMP_MICROSECONDS
    ):
        raise ValueError(
            "IoT-23 flow end time is too large"
        )

    return Iot23FlowIdentity(
        protocol=protocol,
        endpoint_a=endpoint_a,
        endpoint_b=endpoint_b,
        start_time_microseconds=(
            start_time_microseconds
        ),
        end_time_microseconds=(
            end_time_microseconds
        ),
    )