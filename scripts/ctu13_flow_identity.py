#!/usr/bin/env python3

"""
把CTU-13 Scenario 7标签行规范化为本项目使用的双向流身份。

本模块只负责字段解析和规范化，不读取文件、不训练模型，
也不决定一条标签能否与某个C程序输出样本匹配。
"""

from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from decimal import Decimal, InvalidOperation
from ipaddress import AddressValueError, IPv4Address
from typing import Mapping, Optional


MICROSECONDS_PER_SECOND = 1_000_000
SECONDS_PER_DAY = 86_400

# Scenario 7发生在2011年8月，当时布拉格使用CEST，即UTC+02:00。
#
# 使用显式时区，避免结果受到运行机器当前时区影响。
SCENARIO_7_TIMEZONE = timezone(timedelta(hours=2))

UNIX_EPOCH_UTC = datetime(
    1970,
    1,
    1,
    tzinfo=timezone.utc,
)

PROTOCOL_NUMBERS = {
    "icmp": 1,
    "tcp": 6,
    "udp": 17,
}


@dataclass(frozen=True)
class FlowEndpoint:
    """
    规范化流端点。

    ipv4_address使用与C代码相同的32位数值语义；
    port范围为0到65535，ICMP固定使用0。
    """

    ipv4_address: int
    port: int


@dataclass(frozen=True)
class Ctu13FlowIdentity:
    """
    一条CTU-13标签记录中可用于连接C流记录的身份信息。

    时间统一使用UTC Unix微秒，避免浮点秒比较误差。
    endpoint_a和endpoint_b只表示排序结果，不代表客户端或服务端。
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
    """读取并清理一个必需字段。"""

    if column not in row or row[column] is None:
        raise ValueError(
            f"missing CTU-13 column: {column}"
        )

    value = row[column].strip()

    if not value:
        raise ValueError(
            f"empty CTU-13 column: {column}"
        )

    return value


def parse_scenario_7_time_microseconds(
    raw_value: str,
) -> int:
    """
    把Scenario 7本地时间转换成UTC Unix微秒。

    这里不使用datetime.timestamp()的浮点返回值，
    而是使用timedelta的整数成员完成精确计算。
    """

    try:
        local_time = datetime.strptime(
            raw_value.strip(),
            "%Y/%m/%d %H:%M:%S.%f",
        )
    except ValueError as error:
        raise ValueError(
            f"invalid CTU-13 StartTime: {raw_value!r}"
        ) from error

    aware_time = local_time.replace(
        tzinfo=SCENARIO_7_TIMEZONE
    )

    delta = (
        aware_time.astimezone(timezone.utc)
        - UNIX_EPOCH_UTC
    )

    result = (
        (
            delta.days * SECONDS_PER_DAY
            + delta.seconds
        )
        * MICROSECONDS_PER_SECOND
        + delta.microseconds
    )

    if result < 0:
        raise ValueError(
            "CTU-13 StartTime precedes the Unix epoch"
        )

    return result


def parse_duration_microseconds(
    raw_value: str,
) -> int:
    """
    把十进制秒转换为整数微秒。

    Decimal避免使用float后产生类似0.999999999的舍入误差。
    """

    try:
        duration_seconds = Decimal(
            raw_value.strip()
        )
    except InvalidOperation as error:
        raise ValueError(
            f"invalid CTU-13 Dur: {raw_value!r}"
        ) from error

    if (
        not duration_seconds.is_finite()
        or duration_seconds < 0
    ):
        raise ValueError(
            f"invalid CTU-13 Dur: {raw_value!r}"
        )

    scaled_duration = (
        duration_seconds
        * MICROSECONDS_PER_SECOND
    )

    integral_duration = (
        scaled_duration.to_integral_value()
    )

    if scaled_duration != integral_duration:
        raise ValueError(
            "CTU-13 Dur has sub-microsecond precision: "
            f"{raw_value!r}"
        )

    return int(integral_duration)


def parse_ipv4_address(raw_value: str) -> int:
    """把点分十进制IPv4地址转换为32位数值。"""

    try:
        return int(IPv4Address(raw_value.strip()))
    except AddressValueError as error:
        raise ValueError(
            f"invalid CTU-13 IPv4 address: {raw_value!r}"
        ) from error


def parse_port(
    raw_value: str,
    column: str,
) -> int:
    """解析TCP或UDP十进制端口。"""

    text = raw_value.strip()

    if not text.isdecimal():
        raise ValueError(
            f"invalid CTU-13 {column}: {raw_value!r}"
        )

    port = int(text, 10)

    if port > 65535:
        raise ValueError(
            f"CTU-13 {column} is out of range: {port}"
        )

    return port


def normalize_flow_identity(
    row: Mapping[str, Optional[str]],
) -> Optional[Ctu13FlowIdentity]:
    """
    规范化一条CTU-13标签记录。

    返回None表示协议是有效的CTU-13数据，但当前分析器不支持；
    数据损坏或字段非法则抛出ValueError。
    """

    protocol_name = _require_text(
        row,
        "Proto",
    ).lower()

    protocol = PROTOCOL_NUMBERS.get(
        protocol_name
    )

    if protocol is None:
        return None

    source_address = parse_ipv4_address(
        _require_text(row, "SrcAddr")
    )

    destination_address = parse_ipv4_address(
        _require_text(row, "DstAddr")
    )

    if protocol == 1:
        # ICMP不使用TCP/UDP端口，与C端flow_key语义保持一致。
        source_port = 0
        destination_port = 0
    else:
        source_port = parse_port(
            _require_text(row, "Sport"),
            "Sport",
        )

        destination_port = parse_port(
            _require_text(row, "Dport"),
            "Dport",
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

    start_time_microseconds = (
        parse_scenario_7_time_microseconds(
            _require_text(row, "StartTime")
        )
    )

    duration_microseconds = (
        parse_duration_microseconds(
            _require_text(row, "Dur")
        )
    )

    return Ctu13FlowIdentity(
        protocol=protocol,
        endpoint_a=endpoint_a,
        endpoint_b=endpoint_b,
        start_time_microseconds=start_time_microseconds,
        end_time_microseconds=(
            start_time_microseconds
            + duration_microseconds
        ),
    )