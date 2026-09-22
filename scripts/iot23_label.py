#!/usr/bin/env python3

"""
提供IoT-23标签字段的基础解析与分类功能。

本模块只处理单个字段，不读取文件，也不修改输入数据。
后续命令行审计器和匹配工具可以共同复用这里的规则。
"""

from typing import Tuple


LABEL_GROUPS = (
    "malicious",
    "benign",
    "exclude",
)


def parse_appended_label_fields(
    raw_value: str,
) -> Tuple[str, str, str]:
    """
    解析IoT-23数据行末尾追加的三个字段。

    输入示例：

        "(empty)   Malicious   PartOfAHorizontalPortScan"

    返回：

        (
            "(empty)",
            "Malicious",
            "PartOfAHorizontalPortScan",
        )

    split()不传分隔符时，会把连续的空格或制表符视为
    一个分隔区域，因此不依赖字段之间恰好有几个空格。

    返回的三个str都是新字符串引用，不拥有文件对象或其他资源。
    """

    fields = raw_value.split()

    if len(fields) != 3:
        raise ValueError(
            "expected tunnel_parents, label and "
            "detailed-label in appended IoT-23 field"
        )

    tunnel_parents, generic_label, detailed_label = fields

    return (
        tunnel_parents,
        generic_label,
        detailed_label,
    )


def classify_label(generic_label: str) -> str:
    """
    将IoT-23通用标签映射为项目内部监督分类。

    Malicious和Benign可以进入二分类候选集合。
    Background表示标签不确定，必须排除。

    遇到未知标签时抛出ValueError，避免把新类别静默映射成
    正常、恶意或排除。
    """

    if generic_label == "Malicious":
        return "malicious"

    if generic_label == "Benign":
        return "benign"

    if generic_label == "Background":
        return "exclude"

    raise ValueError(
        f"unsupported IoT-23 generic label: "
        f"{generic_label!r}"
    )