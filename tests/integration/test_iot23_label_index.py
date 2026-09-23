#!/usr/bin/env python3

"""验证IoT-23索引保留同键多候选与标签过滤规则。"""

import argparse
import sys
import tempfile
from pathlib import Path
from test_iot23_label_audit import build_record, write_log

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
        load_label_index
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