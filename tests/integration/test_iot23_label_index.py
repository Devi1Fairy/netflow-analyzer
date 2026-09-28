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
    from iot23_flow_review import (
        IOT23_REVIEW_SCHEMA_VERSION,
        IOT23_REVIEW_CSV_COLUMNS,
        write_iot23_review_csv_header,
        REVIEW_NOT_UNIQUE,
        REVIEW_UNIQUE_REUSED,
        REVIEW_UNIQUE_UNREUSED_CANDIDATE,
        validate_iot23_review_record,
        write_iot23_review_csv_record
    )
    from flow_sample_metadata import (
        FlowSampleIdentity,
        build_sample_metadata,
        FlowSampleMetadata
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
        (index, tcp_key, 110, 110, "unique", 1, "malicious", 0),
        # 同键但时间不相交。
        (index, tcp_key, 111, 199, "unmatched", 0, None, None),
        # 零时长标签。
        (index, tcp_key, 200, 200, "unique", 1, "benign", 1),
        # 两个候选给出相反标签。
        (
            index,
            tcp_key,
            100,
            200,
            "ambiguous_conflicting_labels",
            2,
            None,
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
            None,
        ),
        # 相同端点但协议不同，不能串到TCP候选。
        (index, udp_key, 300, 300, "unique", 1, "benign", 0),
    )

    for (
        current_index,
        current_key,
        start,
        end,
        expected_status,
        expected_count,
        expected_group,
        expected_candidate_index,
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
            result.unique_candidate_index,
        )
        expected = (
            expected_status,
            expected_count,
            expected_group,
            expected_candidate_index,
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

    from flow_csv_identity  import EXPECTED_FLOW_COLUMNS
    from audit_iot23_flow_matches import (
            audit_flow_csv,
            audit_flow_csv_with_unreused_rows,
            classify_iot23_row_review,
            write_iot23_review_csv,
        )

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
        (6, 101, 102),   # 与首条TCP流复用同一标签
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
        "flows_total": 6,
        "matches_unique": 3,
        "matches_unique_malicious": 2,
        "matches_unique_benign": 1,
        "matches_unmatched": 1,
        "matches_ambiguous_same_label": 1,
        "matches_ambiguous_conflicting_labels": 1,
        "unique_label_records": 2,
        "reused_label_records": 1,
        "duplicate_unique_assignments": 1,
        "matches_unique_unreused": 1,
        "matches_unique_reused": 2,
    }

    if audit_counts != expected_counts:
        raise RuntimeError(
            f"unexpected audit counts: {audit_counts!r}"
        )

    # 重新从开头读取：上面的audit_flow_csv已经消费了文本流。
    flow_stream.seek(0)

    detailed_counts, unreused_rows = (
        audit_flow_csv_with_unreused_rows(
            audit_index,
            flow_stream,
        )
    )

    if detailed_counts != expected_counts:
        raise RuntimeError(
            f"unexpected detailed counts: {detailed_counts!r}"
        )

    if unreused_rows != frozenset({5}):
        raise RuntimeError(
            f"unexpected unreused rows: {unreused_rows!r}"
        )

    review_cases = (
        (1, "unique", "unique_reused"),
        (2, "ambiguous_same_label", "not_unique"),
        (3, "ambiguous_conflicting_labels", "not_unique"),
        (4, "unmatched", "not_unique"),
        (5, "unique", "unique_unreused_candidate"),
        (6, "unique", "unique_reused"),
    )

    review_output = StringIO()
    write_iot23_review_csv_header(review_output)

    expected_header = (
        "review_schema_version,feature_schema_version,"
        "feature_row_number,sample_id,capture_id,protocol,"
        "endpoint_a_ip,endpoint_a_port,endpoint_b_ip,"
        "endpoint_b_port,first_seen_unix_microseconds,"
        "last_seen_unix_microseconds,match_status,"
        "candidate_count,label_group,review_status\n"
    )

    if IOT23_REVIEW_SCHEMA_VERSION != "iot23_flow_review_v1":
        raise RuntimeError("unexpected review schema version")

    if review_output.getvalue() != expected_header:
        raise RuntimeError("unexpected IoT-23 review CSV header")

    if IOT23_REVIEW_CSV_COLUMNS != tuple(
        expected_header.strip().split(",")
    ):
        raise RuntimeError("review CSV columns differ from header")

    for row_number, match_status, expected in review_cases:
        actual = classify_iot23_row_review(
            row_number,
            match_status,
            unreused_rows,
        )
        if actual != expected:
            raise RuntimeError(
                f"row {row_number}: {actual!r} != {expected!r}"
            )

    identity = FlowSampleIdentity(
        capture_id="iot23-scenario-3-1",
        protocol=6,
        endpoint_a_ipv4=endpoint_a.ipv4_address,
        endpoint_a_port=endpoint_a.port,
        endpoint_b_ipv4=endpoint_b.ipv4_address,
        endpoint_b_port=endpoint_b.port,
        first_seen_microseconds=100,
        last_seen_microseconds=110,
    )

    unique_metadata = build_sample_metadata(
        identity=identity,
        feature_row_number=1,
        match_status="unique",
        candidate_count=1,
        label_group="malicious",
    )
    unmatched_metadata = build_sample_metadata(
        identity=identity,
        feature_row_number=4,
        match_status="unmatched",
        candidate_count=0,
        label_group=None,
    )

    valid_cases = (
        (unique_metadata, REVIEW_UNIQUE_REUSED),
        (unique_metadata, REVIEW_UNIQUE_UNREUSED_CANDIDATE),
        (unmatched_metadata, REVIEW_NOT_UNIQUE),
    )

    for metadata, review_status in valid_cases:
        if validate_iot23_review_record(
            metadata, review_status
        ) != metadata:
            raise RuntimeError("valid review record changed")

    write_iot23_review_csv_record(
        review_output,
        unique_metadata,
        REVIEW_UNIQUE_REUSED,
    )
    write_iot23_review_csv_record(
        review_output,
        unmatched_metadata,
        REVIEW_NOT_UNIQUE,
    )

    expected_records = (
        "iot23_flow_review_v1,flow_features_v1,1,"
        f"{unique_metadata.sample_id},"
        "iot23-scenario-3-1,6,192.168.2.5,1234,"
        "198.51.100.20,80,100,110,unique,1,malicious,"
        "unique_reused\n"
        "iot23_flow_review_v1,flow_features_v1,4,"
        f"{unmatched_metadata.sample_id},"
        "iot23-scenario-3-1,6,192.168.2.5,1234,"
        "198.51.100.20,80,100,110,unmatched,0,,not_unique\n"
    )

    if review_output.getvalue() != expected_header + expected_records:
        raise RuntimeError("unexpected IoT-23 review CSV records")

    invalid_metadata = FlowSampleMetadata(
        feature_row_number=7,
        identity=identity,
        match_status="unique",
        candidate_count=0,
        label_group="malicious",
    )

    invalid_cases = (
        (unique_metadata, REVIEW_NOT_UNIQUE),
        (unmatched_metadata, REVIEW_UNIQUE_REUSED),
        (unique_metadata, "unknown"),
        (
            invalid_metadata,
            REVIEW_UNIQUE_UNREUSED_CANDIDATE,
        ),
    )

    for metadata, review_status in invalid_cases:
        try:
            validate_iot23_review_record(
                metadata, review_status
            )
        except ValueError:
            pass
        else:
            raise RuntimeError(
                "invalid review combination was accepted"
            )

    before_invalid = review_output.getvalue()

    for metadata, review_status in invalid_cases:
        try:
            write_iot23_review_csv_record(
                review_output,
                metadata,
                review_status,
            )
        except ValueError:
            pass
        else:
            raise RuntimeError(
                "invalid review record was written"
            )

        if review_output.getvalue() != before_invalid:
            raise RuntimeError(
                "invalid review record changed the output"
            )

    # 现有flow_stream包含六行合成C流。此前审计已经把位置读到末尾。
    flow_stream.seek(0)
    batch_output = StringIO()

    batch_counts = write_iot23_review_csv(
        index=audit_index,
        input_stream=flow_stream,
        output_stream=batch_output,
        capture_id="iot23-scenario-3-1",
    )

    if batch_counts != expected_counts:
        raise RuntimeError(
            f"unexpected batch audit counts: {batch_counts!r}"
        )

    batch_reader = csv.DictReader(
        StringIO(batch_output.getvalue())
    )

    if tuple(batch_reader.fieldnames or ()) != (
        IOT23_REVIEW_CSV_COLUMNS
    ):
        raise RuntimeError(
            "unexpected batch review CSV header"
        )

    batch_rows = list(batch_reader)

    if len(batch_rows) != 6:
        raise RuntimeError(
            "batch review CSV must contain six data rows"
        )

    if [
        row["feature_row_number"]
        for row in batch_rows
    ] != [str(number) for number in range(1, 7)]:
        raise RuntimeError(
            "batch feature row numbers are incorrect"
        )

    if [
        row["match_status"]
        for row in batch_rows
    ] != [case[1] for case in review_cases]:
        raise RuntimeError(
            "batch match statuses are incorrect"
        )

    if [
        row["review_status"]
        for row in batch_rows
    ] != [case[2] for case in review_cases]:
        raise RuntimeError(
            "batch review statuses are incorrect"
        )

    # 第一遍遇到坏CSV时，批量函数不应开始写审查文件。
    bad_flow_stream = StringIO(
        flow_stream.getvalue() + "6\n"
    )
    bad_output = StringIO()

    try:
        write_iot23_review_csv(
            index=audit_index,
            input_stream=bad_flow_stream,
            output_stream=bad_output,
            capture_id="iot23-scenario-3-1",
        )
    except ValueError:
        pass
    else:
        raise RuntimeError(
            "malformed flow CSV was accepted"
        )

    if bad_output.getvalue():
        raise RuntimeError(
            "malformed flow CSV produced review output"
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