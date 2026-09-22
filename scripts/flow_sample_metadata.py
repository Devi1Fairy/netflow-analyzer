#!/usr/bin/env python3

"""
定义流样本身份和后续元数据生成所需的稳定数据契约。

本模块不会训练模型，也不会把IP、端口或标签加入模型特征。
"""
import hashlib
import json
import re
from dataclasses import dataclass


SAMPLE_ID_SCHEMA_VERSION = "flow_sample_id_v1"

# re.compile(pattern)把正则字符串编译成可重复使用的Pattern对象。
# 后面的fullmatch(text)要求整个字符串匹配；成功返回Match，
# 失败返回None。
CAPTURE_ID_PATTERN = re.compile(
    r"[A-Za-z0-9][A-Za-z0-9._:-]{0,127}"
)

# frozenset(iterable)返回不可修改的集合。
# 后续使用“protocol in SUPPORTED_PROTOCOLS”进行成员检查。
SUPPORTED_PROTOCOLS = frozenset(
    (
        1,   # ICMP
        6,   # TCP
        17,  # UDP
    )
)

MAX_IPV4_ADDRESS = (1 << 32) - 1
MAX_PORT = (1 << 16) - 1
MAX_TIMESTAMP_MICROSECONDS = (1 << 63) - 1

# dataclass根据字段注解自动生成__init__、__repr__和__eq__。
# frozen=True禁止对象创建后重新给字段赋值，适合表示稳定身份。
@dataclass(frozen=True)
class FlowSampleIdentity:
    """
    一条已经完成生命周期的流样本身份。

    capture_id标识原始抓包来源。
    端点必须使用与C流表相同的规范化顺序。
    时间统一使用Unix微秒整数。
    """

    capture_id: str

    protocol: int

    endpoint_a_ipv4: int
    endpoint_a_port: int

    endpoint_b_ipv4: int
    endpoint_b_port: int

    first_seen_microseconds: int
    last_seen_microseconds: int

def _require_bounded_integer(
    name: str,
    value: int,
    minimum: int,
    maximum: int,
) -> None:
    """
    验证字段是指定闭区间内的整数。

    输入：
        name用于生成错误信息；
        value是被检查的值；
        minimum和maximum组成允许的闭区间。

    输出：
        验证成功时返回None。

    异常：
        value不是整数时抛出TypeError；
        value超出闭区间时抛出ValueError。
    """

    if (
        # isinstance(object, type)返回布尔值。
        # Python的bool是int的子类，所以必须先显式排除True和False。
        isinstance(value, bool)
        or not isinstance(value, int)
    ):
        raise TypeError(
            f"{name} must be an integer"
        )

    if value < minimum or value > maximum:
        raise ValueError(
            f"{name} is outside "
            f"[{minimum}, {maximum}]: {value}"
        )

def build_sample_id(
    identity: FlowSampleIdentity,
) -> str:
    """
    根据抓包来源、规范化流键和生命周期生成稳定ID。

    输入：
        identity必须是字段合法且端点顺序规范化的
        FlowSampleIdentity对象。

    输出：
        返回“模式版本:SHA-256十六进制摘要”形式的字符串。

    异常：
        对象类型错误时抛出TypeError；
        字段格式、范围或字段关系错误时抛出ValueError。

    相同输入必须得到相同ID；任何身份字段变化都应产生不同ID。
    """

    if not isinstance(
        identity,
        FlowSampleIdentity,
    ):
        raise TypeError(
            "identity must be FlowSampleIdentity"
        )

    if CAPTURE_ID_PATTERN.fullmatch(
        identity.capture_id
    ) is None:
        raise ValueError(
            "capture_id contains unsupported characters "
            "or has invalid length"
        )

    _require_bounded_integer(
        "protocol",
        identity.protocol,
        0,
        255,
    )

    if identity.protocol not in SUPPORTED_PROTOCOLS:
        raise ValueError(
            f"unsupported protocol: {identity.protocol}"
        )

    _require_bounded_integer(
        "endpoint_a_ipv4",
        identity.endpoint_a_ipv4,
        0,
        MAX_IPV4_ADDRESS,
    )

    _require_bounded_integer(
        "endpoint_b_ipv4",
        identity.endpoint_b_ipv4,
        0,
        MAX_IPV4_ADDRESS,
    )

    _require_bounded_integer(
        "endpoint_a_port",
        identity.endpoint_a_port,
        0,
        MAX_PORT,
    )

    _require_bounded_integer(
        "endpoint_b_port",
        identity.endpoint_b_port,
        0,
        MAX_PORT,
    )

    if (
        identity.protocol == 1
        and (
            identity.endpoint_a_port != 0
            or identity.endpoint_b_port != 0
        )
    ):
        raise ValueError(
            "ICMP sample ports must both be zero"
        )

    endpoint_a = (
        identity.endpoint_a_ipv4,
        identity.endpoint_a_port,
    )

    endpoint_b = (
        identity.endpoint_b_ipv4,
        identity.endpoint_b_port,
    )

    if endpoint_a > endpoint_b:
        raise ValueError(
            "sample endpoints are not canonical"
        )

    _require_bounded_integer(
        "first_seen_microseconds",
        identity.first_seen_microseconds,
        0,
        MAX_TIMESTAMP_MICROSECONDS,
    )

    _require_bounded_integer(
        "last_seen_microseconds",
        identity.last_seen_microseconds,
        0,
        MAX_TIMESTAMP_MICROSECONDS,
    )

    if (
        identity.first_seen_microseconds
        > identity.last_seen_microseconds
    ):
        raise ValueError(
            "first_seen_microseconds is after "
            "last_seen_microseconds"
        )

    canonical_fields = (
        SAMPLE_ID_SCHEMA_VERSION,
        identity.capture_id,
        identity.protocol,
        identity.endpoint_a_ipv4,
        identity.endpoint_a_port,
        identity.endpoint_b_ipv4,
        identity.endpoint_b_port,
        identity.first_seen_microseconds,
        identity.last_seen_microseconds,
    )

    # json.dumps(object, ...)把Python对象转换成str。
    # ensure_ascii=True保证输出只包含ASCII字符；
    # separators去掉逗号和冒号后的默认空格，固定序列化格式。
    #
    # str.encode("ascii")再把字符串转换成bytes，
    # 因为hashlib.sha256()接收字节而不是Python字符串。
    canonical_bytes = json.dumps(
        canonical_fields,
        ensure_ascii=True,
        separators=(",", ":"),
    ).encode("ascii")

    # hashlib.sha256(bytes)返回SHA-256摘要对象。
    # hexdigest()把32字节摘要转换成64字符的十六进制str。
    digest = hashlib.sha256(
        canonical_bytes
    ).hexdigest()

    return (
        f"{SAMPLE_ID_SCHEMA_VERSION}:"
        f"{digest}"
    )