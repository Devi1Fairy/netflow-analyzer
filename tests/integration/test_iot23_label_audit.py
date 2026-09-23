#!/usr/bin/env python3

"""
验证IoT-23标签审计器的命令行行为。

测试使用临时生成的小型Zeek文本，不依赖公开数据集，
因此可以在开发机、目标板和未来CI环境重复执行。
"""

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


EXPECTED_BASE_COLUMNS = (
    "ts",
    "uid",
    "id.orig_h",
    "id.orig_p",
    "id.resp_h",
    "id.resp_p",
    "proto",
    "service",
    "duration",
    "orig_bytes",
    "resp_bytes",
    "conn_state",
    "local_orig",
    "local_resp",
    "missed_bytes",
    "history",
    "orig_pkts",
    "orig_ip_bytes",
    "resp_pkts",
    "resp_ip_bytes",
)

EXPECTED_APPENDED_COLUMNS = (
    "tunnel_parents",
    "label",
    "detailed-label",
)


def parse_arguments() -> argparse.Namespace:
    """解析CTest传入的脚本路径和临时目录。"""

    parser = argparse.ArgumentParser(
        description="Test the IoT-23 label audit tool."
    )

    parser.add_argument(
        "--script",
        required=True,
        type=Path,
        help="Path to inspect_iot23_labels.py.",
    )

    parser.add_argument(
        "--work-dir",
        required=True,
        type=Path,
        help="Writable directory for temporary files.",
    )

    return parser.parse_args()


def build_fields_directive(
    base_columns=EXPECTED_BASE_COLUMNS,
    appended_columns=EXPECTED_APPENDED_COLUMNS,
) -> str:
    """
    构造Zeek的#fields声明。

    基础字段使用制表符分隔，最后三个扩展字段名使用
    三个空格连接，以复现真实IoT-23文件格式。
    """

    appended_text = "   ".join(
        appended_columns
    )

    return (
        "#fields\t"
        + "\t".join(base_columns)
        + "\t"
        + appended_text
        + "\n"
    )


def build_record(
    protocol: str,
    generic_label: str,
    detailed_label: str,
    appended_override=None,
    duration: str = "1.000000",
    orig_pkts: str = "1",
    resp_pkts: str = "1",
) -> str:
    """构造一条字段数量正确的合成Zeek记录。"""

    base_fields = [
        "1526756261.000000",
        "CDeterministicUid",
        "192.168.2.5",
        "40000",
        "198.51.100.20",
        "80",
        protocol,
        "-",
        duration,
        "40",
        "50",
        "SF",
        "-",
        "-",
        "0",
        "Dd",
        orig_pkts,
        "68",
        resp_pkts,
        "78",
    ]

    if appended_override is None:
        appended_value = (
            f"(empty)   {generic_label}   "
            f"{detailed_label}"
        )
    else:
        appended_value = appended_override

    return (
        "\t".join(
            base_fields + [appended_value]
        )
        + "\n"
    )


def write_log(
    output_path: Path,
    rows,
    separator_directive=r"#separator \x09",
    fields_directive=None,
) -> None:
    """写出一个最小但结构完整的Zeek标签文件。"""

    if fields_directive is None:
        fields_directive = build_fields_directive()

    with output_path.open(
        "w",
        encoding="utf-8",
        newline="",
    ) as output_stream:
        output_stream.write(
            separator_directive + "\n"
        )
        output_stream.write("#set_separator\t,\n")
        output_stream.write("#path\tconn\n")
        output_stream.write(fields_directive)
        output_stream.write("#types\ttime\n")

        for row in rows:
            output_stream.write(row)

        output_stream.write("#close\tfinished\n")


def run_audit(
    script: Path,
    input_file: Path,
) -> subprocess.CompletedProcess:
    """
    使用当前Python解释器运行被测审计器。

    capture_output=True把stdout和stderr保存在返回对象中；
    check=False允许测试代码自行检查失败状态；
    timeout防止被测脚本意外永久阻塞。
    """

    return subprocess.run(
        [
            sys.executable,
            str(script),
            str(input_file),
        ],
        capture_output=True,
        text=True,
        timeout=5,
        check=False,
    )


def require_failure(
    completed_process: subprocess.CompletedProcess,
    expected_error: str,
) -> None:
    """要求子进程失败，并包含指定错误文本。"""

    if completed_process.returncode == 0:
        raise RuntimeError(
            "audit tool unexpectedly returned success\n"
            f"stdout:\n{completed_process.stdout}\n"
            f"stderr:\n{completed_process.stderr}"
        )

    if expected_error not in completed_process.stderr:
        raise RuntimeError(
            "missing expected error text: "
            f"{expected_error!r}\n"
            f"stdout:\n{completed_process.stdout}\n"
            f"stderr:\n{completed_process.stderr}"
        )


def run_tests(
    script: Path,
    work_dir: Path,
) -> None:
    """生成确定性输入并验证成功与失败路径。"""

    if not script.is_file():
        raise RuntimeError(
            f"audit script does not exist: {script}"
        )

    if not work_dir.is_dir():
        raise RuntimeError(
            f"work directory does not exist: {work_dir}"
        )

    with tempfile.TemporaryDirectory(
        prefix="iot23-label-audit-",
        dir=work_dir,
    ) as temporary_directory:
        temporary_path = Path(temporary_directory)

        # 成功路径：三类标签、三种协议和两个详细标签。
        valid_path = temporary_path / "valid.log"

        write_log(
            valid_path,
            [
                build_record(
                    "tcp",
                    "Malicious",
                    "Attack",
                ),
                build_record(
                    "udp",
                    "Benign",
                    "-",
                    duration="-",
                    orig_pkts="1",
                    resp_pkts="0",
                ),
                build_record(
                    "icmp",
                    "Background",
                    "-",
                ),
                build_record(
                    "unknown_transport",
                    "Background",
                    "-",
                ),
            ],
        )

        completed_process = run_audit(
            script,
            valid_path,
        )

        if completed_process.returncode != 0:
            raise RuntimeError(
                "valid input returned non-zero status\n"
                f"stdout:\n{completed_process.stdout}\n"
                f"stderr:\n{completed_process.stderr}"
            )

        expected_output = (
            f"input={valid_path}\n"
            "total=4\n"
            "malicious=1\n"
            "benign=1\n"
            "exclude=2\n"
            "identity_supported=3\n"
            "identity_unsupported=1\n"
            "identity_zero_duration=1\n"
            "protocol[icmp]=1\n"
            "protocol[tcp]=1\n"
            "protocol[udp]=1\n"
            "protocol[unknown_transport]=1\n"
            "detailed_label[-]=3\n"
            "detailed_label[Attack]=1\n"
        )

        if completed_process.stdout != expected_output:
            raise RuntimeError(
                "unexpected audit output\n"
                f"expected:\n{expected_output}\n"
                f"actual:\n{completed_process.stdout}"
            )

        if completed_process.stderr:
            raise RuntimeError(
                "valid audit wrote unexpected stderr\n"
                f"{completed_process.stderr}"
            )

        # 不允许使用逗号代替Zeek声明的制表符。
        bad_separator_path = (
            temporary_path / "bad-separator.log"
        )

        write_log(
            bad_separator_path,
            [
                build_record(
                    "tcp",
                    "Malicious",
                    "Attack",
                ),
            ],
            separator_directive="#separator ,",
        )

        require_failure(
            run_audit(script, bad_separator_path),
            "unsupported field separator",
        )

        # 修改一个基础列名，验证模式漂移会被发现。
        bad_columns = (
            EXPECTED_BASE_COLUMNS[:-1]
            + ("unexpected_ip_bytes",)
        )

        bad_fields_path = (
            temporary_path / "bad-fields.log"
        )

        write_log(
            bad_fields_path,
            [
                build_record(
                    "tcp",
                    "Malicious",
                    "Attack",
                ),
            ],
            fields_directive=build_fields_directive(
                base_columns=bad_columns
            ),
        )

        require_failure(
            run_audit(script, bad_fields_path),
            "unexpected base columns",
        )

        # 删除记录最后一段，使字段数由21变成20。
        complete_row = build_record(
            "tcp",
            "Malicious",
            "Attack",
        )

        incomplete_row = (
            "\t".join(
                complete_row.rstrip("\n").split("\t")[:-1]
            )
            + "\n"
        )

        missing_field_path = (
            temporary_path / "missing-field.log"
        )

        write_log(
            missing_field_path,
            [incomplete_row],
        )

        require_failure(
            run_audit(script, missing_field_path),
            "expected 21 fields, received 20",
        )

        # 末尾扩展字段必须同时包含三个值。
        malformed_appended_path = (
            temporary_path / "malformed-appended.log"
        )

        write_log(
            malformed_appended_path,
            [
                build_record(
                    "tcp",
                    "Malicious",
                    "Attack",
                    appended_override=(
                        "(empty) Malicious"
                    ),
                ),
            ],
        )

        require_failure(
            run_audit(
                script,
                malformed_appended_path,
            ),
            "expected tunnel_parents, label and "
            "detailed-label",
        )

        # 新标签不能被静默映射。
        unknown_label_path = (
            temporary_path / "unknown-label.log"
        )

        write_log(
            unknown_label_path,
            [
                build_record(
                    "tcp",
                    "Suspicious",
                    "Unknown",
                ),
            ],
        )

        require_failure(
            run_audit(script, unknown_label_path),
            "unsupported IoT-23 generic label",
        )

        # 多包记录缺少duration时，无法可靠推导结束时间。
        invalid_identity_path = (
            temporary_path / "invalid-identity.log"
        )

        write_log(
            invalid_identity_path,
            [
                build_record(
                    "tcp",
                    "Benign",
                    "-",
                    duration="-",
                    orig_pkts="1",
                    resp_pkts="1",
                ),
            ],
        )

        require_failure(
            run_audit(
                script,
                invalid_identity_path,
            ),
            "invalid flow identity: "
            "unset IoT-23 duration requires "
            "exactly one packet",
        )

        # 有完整头部但没有记录也必须失败。
        empty_path = temporary_path / "empty.log"

        write_log(
            empty_path,
            [],
        )

        require_failure(
            run_audit(script, empty_path),
            "contains no data records",
        )


def main() -> int:
    """运行全部测试，并把异常转换成非零退出状态。"""

    arguments = parse_arguments()

    try:
        run_tests(
            arguments.script,
            arguments.work_dir,
        )
    except (
        OSError,
        RuntimeError,
        subprocess.SubprocessError,
    ) as error:
        print(
            f"[FAIL] IoT-23 label audit tests: {error}",
            file=sys.stderr,
        )
        return 1

    print("[PASS] IoT-23 label audit tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())