#!/usr/bin/env python3

"""
定义流样本身份和后续元数据生成所需的稳定数据契约。

本模块不会训练模型，也不会把IP、端口或标签加入模型特征。
"""
import hashlib
import json
import re
from dataclasses import dataclass
from typing import Optional


SAMPLE_ID_SCHEMA_VERSION = "flow_sample_id_v1"

SAMPLE_METADATA_SCHEMA_VERSION = "flow_sample_metadata_v1"

SUPPORTED_FEATURE_SCHEMA_VERSION = "flow_features_v1"

MATCH_STATUS_UNMATCHED = "unmatched"
MATCH_STATUS_UNIQUE = "unique"

MATCH_STATUS_AMBIGUOUS_SAME_LABEL = "ambiguous_same_label"

MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS = "ambiguous_conflicting_labels"

LABEL_GROUP_BENIGN = "benign"
LABEL_GROUP_MALICIOUS = "malicious"

SUPPORTED_MATCH_STATUSES = frozenset(
    (
        MATCH_STATUS_UNMATCHED,
        MATCH_STATUS_UNIQUE,
        MATCH_STATUS_AMBIGUOUS_SAME_LABEL,
        MATCH_STATUS_AMBIGUOUS_CONFLICTING_LABELS,
    )
)

SUPPORTED_LABEL_GROUPS = frozenset(
    (
        LABEL_GROUP_BENIGN,
        LABEL_GROUP_MALICIOUS,
    )
)

MAX_METADATA_COUNT = (1 << 63) - 1

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

@dataclass(frozen=True)
class FlowSampleMetadata:
    """
    一条模型特征对应的身份和标签连接结果。

    feature_row_number从1开始，只计算特征CSV中的数据行，
    不把CSV表头计入行号。

    identity保存标签连接所需的身份字段，但这些字段不会
    自动成为模型输入。

    match_status和candidate_count记录匹配质量。
    只有唯一匹配的样本才允许进入监督训练集。
    """

    feature_row_number: int
    identity: FlowSampleIdentity

    match_status: str
    candidate_count: int
    label_group: Optional[str]

    @property
    def sample_id(self) -> str:
        """根据不可变身份按需计算稳定样本ID。"""

        return build_sample_id(self.identity)

    @property
    def is_trainable(self) -> bool:
        """返回该样本是否具有可靠的监督标签。"""

        return (
            self.match_status
            == MATCH_STATUS_UNIQUE
            and self.label_group
            in SUPPORTED_LABEL_GROUPS
        )

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

def build_sample_metadata(
    identity: FlowSampleIdentity,
    feature_row_number: int,
    match_status: str,
    candidate_count: int,
    label_group: Optional[str],
) -> FlowSampleMetadata:
    """
    验证标签匹配结果并构造流样本元数据。

    唯一匹配才是可训练样本。未匹配和歧义结果仍保留，
    但不能静默转换为正常或恶意标签。
    """

    # 同时验证identity的类型、字段范围、端点顺序和时间关系。
    build_sample_id(identity)

    _require_bounded_integer(
        "feature_row_number",
        feature_row_number,
        1,
        MAX_METADATA_COUNT,
    )

    if not isinstance(match_status, str):
        raise TypeError(
            "match_status must be a string"
        )

    if match_status not in SUPPORTED_MATCH_STATUSES:
        raise ValueError(
            f"unsupported match status: {match_status}"
        )

    _require_bounded_integer(
        "candidate_count",
        candidate_count,
        0,
        MAX_METADATA_COUNT,
    )

    if (
        label_group is not None
        and not isinstance(label_group, str)
    ):
        raise TypeError(
            "label_group must be a string or None"
        )

    if (
        label_group is not None
        and label_group not in SUPPORTED_LABEL_GROUPS
    ):
        raise ValueError(
            f"unsupported label group: {label_group}"
        )

    if match_status == MATCH_STATUS_UNMATCHED:
        if candidate_count != 0 or label_group is not None:
            raise ValueError(
                "unmatched sample must have zero candidates "
                "and no label"
            )

    elif match_status == MATCH_STATUS_UNIQUE:
        if candidate_count != 1 or label_group is None:
            raise ValueError(
                "unique match must have one candidate "
                "and one supervised label"
            )

    elif (
        match_status
        == MATCH_STATUS_AMBIGUOUS_SAME_LABEL
    ):
        if candidate_count < 2 or label_group is None:
            raise ValueError(
                "same-label ambiguity must have at least "
                "two candidates with one shared label"
            )

    else:
        if candidate_count < 2 or label_group is not None:
            raise ValueError(
                "conflicting-label ambiguity must have at "
                "least two candidates and no final label"
            )

    return FlowSampleMetadata(
        feature_row_number=feature_row_number,
        identity=identity,
        match_status=match_status,
        candidate_count=candidate_count,
        label_group=label_group,
    )
