#!/usr/bin/env python3

"""验证IoT-23索引保留同键多候选与标签过滤规则。"""

import argparse
import sys
import tempfile
from pathlib import Path
from test_iot23_label_audit import build_record, write_log
import csv
from io import StringIO

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--scripts-dir",
        required=True,
        type=Path,
    )
    parser.add_argument(
        "--work-dir",
        required=True,
        type=Path,
    )
    arguments = parser.parse_args()

    sys.path.insert(0, str(arguments.scripts_dir))

    from iot23_flow_identity import (
        FlowEndpoint,
        Iot23FlowIdentity,
    )
    from iot23_label_index import (
        build_label_index,
        flow_key_from_identity,
        load_label_index,
        classify_flow_interval
    )

    endpoint_a = FlowEndpoint(
        ipv4_address=0xC0A80205,
        port=1234,
    )
    endpoint_b = FlowEndpoint(
        ipv4_address=0xC6336414,
        port=80,
    )

    first = Iot23FlowIdentity(
        protocol=6,
        endpoint_a=endpoint_a,
        endpoint_b=endpoint_b,
        start_time_microseconds=100,
        end_time_microseconds=110,
    )
    second = Iot23FlowIdentity(
        protocol=6,
        endpoint_a=endpoint_a,
        endpoint_b=endpoint_b,
        start_time_microseconds=200,
        end_time_microseconds=200,
    )
    udp = Iot23FlowIdentity(
        protocol=17,
        endpoint_a=endpoint_a,
        endpoint_b=endpoint_b,
        start_time_microseconds=300,
        end_time_microseconds=310,
    )

    index = build_label_index(
        [
            (first, "malicious"),
            (second, "benign"),
            (udp, "benign"),
            (first, "exclude"),
            (None, "malicious"),
        ]
    )

    tcp_candidates = index[
        flow_key_from_identity(first)
    ]
    udp_candidates = index[
        flow_key_from_identity(udp)
    ]

    if len(index) != 2:
        raise RuntimeError("expected TCP and UDP keys")

    if len(tcp_candidates) != 2:
        raise RuntimeError("same-key labels were lost")

    if [
        candidate.label_group
        for candidate in tcp_candidates
    ] != ["malicious", "benign"]:
        raise RuntimeError("candidate order changed")

    if (
        tcp_candidates[1].start_microseconds != 200
        or tcp_candidates[1].end_microseconds != 200
    ):
        raise RuntimeError("zero-duration interval changed")

    if (
        len(udp_candidates) != 1
        or udp_candidates[0].label_group != "benign"
    ):
        raise RuntimeError("UDP key was mixed with TCP")

    try:
        build_label_index(
            [(first, "unknown")]
        )
    except ValueError:
        pass
    else:
        raise RuntimeError("unknown label was accepted")

    tcp_key = flow_key_from_identity(first)
    udp_key = flow_key_from_identity(udp)

    overlapping_malicious = Iot23FlowIdentity(
        protocol=6,
        endpoint_a=endpoint_a,
        endpoint_b=endpoint_b,
        start_time_microseconds=105,
        end_time_microseconds=115,
    )
    same_label_index = build_label_index(
        [
            (first, "malicious"),
            (overlapping_malicious, "malicious"),
        ]
    )

    cases = (
        # 区间端点相等仍算匹配。
        (index, tcp_key, 110, 110, "unique", 1, "malicious"),
        # 同键但时间不相交。
        (index, tcp_key, 111, 199, "unmatched", 0, None),
        # 零时长标签。
        (index, tcp_key, 200, 200, "unique", 1, "benign"),
        # 两个候选给出相反标签。
        (
            index,
            tcp_key,
            100,
            200,
            "ambiguous_conflicting_labels",
            2,
            None,
        ),
        # 两个候选标签相同，仍不是唯一匹配。
        (
            same_label_index,
            tcp_key,
            106,
            106,
            "ambiguous_same_label",
            2,
            "malicious",
        ),
        # 相同端点但协议不同，不能串到TCP候选。
        (index, udp_key, 300, 300, "unique", 1, "benign"),
    )

    for (
        current_index,
        current_key,
        start,
        end,
        expected_status,
        expected_count,
        expected_group,
    ) in cases:
        result = classify_flow_interval(
            current_index,
            current_key,
            start,
            end,
        )

        actual = (
            result.status,
            result.candidate_count,
            result.label_group,
        )
        expected = (
            expected_status,
            expected_count,
            expected_group,
        )

        if actual != expected:
            raise RuntimeError(
                f"unexpected match classification: "
                f"{actual!r} != {expected!r}"
            )

    try:
        classify_flow_interval(index, tcp_key, 201, 200)
    except ValueError:
        pass
    else:
        raise RuntimeError("reversed flow interval was accepted")

    from audit_ctu13_flow_matches import EXPECTED_FLOW_COLUMNS
    from audit_iot23_flow_matches import audit_flow_csv

    audit_index = build_label_index(
        [
            (first, "malicious"),
            (overlapping_malicious, "malicious"),
            (second, "benign"),
            (udp, "benign"),
        ]
    )

    # StringIO是内存中的文本流，不需要创建真实CSV文件。
    flow_stream = StringIO()
    writer = csv.writer(flow_stream)
    writer.writerow(EXPECTED_FLOW_COLUMNS)

    for protocol, start, end in (
        (6, 100, 100),   # 唯一恶意
        (6, 106, 106),   # 两个同为恶意的候选
        (6, 100, 200),   # 恶意与正常候选冲突
        (6, 150, 160),   # 未匹配
        (17, 300, 300),  # 唯一正常
    ):
        writer.writerow(
            (
                protocol,
                "established"
                if protocol == 6
                else "not-applicable",
                "192.168.2.5",
                1234,
                "198.51.100.20",
                80,
                1, 60, 60,
                0, 0, 0,
                0, start,
                0, end,
            )
        )

    # 写完后读写位置在末尾；读CSV前必须回到开头。
    flow_stream.seek(0)

    audit_counts = audit_flow_csv(
        audit_index,
        flow_stream,
    )

    expected_counts = {
        "flows_total": 5,
        "matches_unique": 2,
        "matches_unique_malicious": 1,
        "matches_unique_benign": 1,
        "matches_unmatched": 1,
        "matches_ambiguous_same_label": 1,
        "matches_ambiguous_conflicting_labels": 1,
    }

    if audit_counts != expected_counts:
        raise RuntimeError(
            f"unexpected audit counts: {audit_counts!r}"
        )

    if not arguments.work_dir.is_dir():
        raise RuntimeError("work directory does not exist")

    with tempfile.TemporaryDirectory(
        prefix="iot23-label-index-",
        dir=arguments.work_dir,
    ) as temporary_directory:
        label_file = Path(temporary_directory) / "labels.log"

        write_log(
            label_file,
            [
                build_record("tcp", "Malicious", "Attack"),
                build_record("tcp", "Benign", "-"),
                build_record(
                    "udp",
                    "Benign",
                    "-",
                    duration="-",
                    orig_pkts="1",
                    resp_pkts="0",
                ),
                build_record("icmp", "Background", "-"),
                build_record(
                    "unknown_transport",
                    "Malicious",
                    "Attack",
                ),
            ],
        )

        file_index = load_label_index(label_file)
        by_protocol = {
            key[0]: candidates
            for key, candidates in file_index.items()
        }

        if set(by_protocol) != {6, 17}:
            raise RuntimeError("unexpected indexed protocols")

        if [
            candidate.label_group
            for candidate in by_protocol[6]
        ] != ["malicious", "benign"]:
            raise RuntimeError("TCP candidates were lost")

        if len(by_protocol[17]) != 1:
            raise RuntimeError("UDP candidate was lost")

        udp_candidate = by_protocol[17][0]
        if (
            udp_candidate.start_microseconds
            != udp_candidate.end_microseconds
        ):
            raise RuntimeError("zero-duration interval changed")

    print("[PASS] IoT-23 label index tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())