#!/bin/sh

#
# 检查netflow-analyzer的构建和测试环境。
#
# 脚本只读取系统信息，不安装软件、不修改配置。
#
# 使用方式：
#
#   普通检查：
#       ./scripts/check_target_env.sh
#
#   要求当前设备必须是ARM：
#       ./scripts/check_target_env.sh --expect-arm
#
#   同时要求具备完整测试环境：
#       ./scripts/check_target_env.sh --with-tests
#
#   开发板首次验收：
#       ./scripts/check_target_env.sh --expect-arm --with-tests
#

set -u

expect_arm=false
with_tests=false
required_failure_count=0

print_usage()
{
    printf '%s\n' \
        "Usage: $0 [OPTION]" \
        "" \
        "Options:" \
        "  --expect-arm  Fail if the current CPU is not ARM." \
        "  --with-tests  Require Python for acceptance tests." \
        "  -h, --help    Show this help message."
}

print_pass()
{
    printf '[PASS] %s\n' "$1"
}

print_warning()
{
    printf '[WARN] %s\n' "$1"
}

print_failure()
{
    printf '[FAIL] %s\n' "$1"
    required_failure_count=$((required_failure_count + 1))
}

check_required_command()
{
    command_name=$1
    description=$2

    if command -v "$command_name" >/dev/null 2>&1; then
        print_pass "$description: $(command -v "$command_name")"
    else
        print_failure "$description is missing: $command_name"
    fi
}

while [ "$#" -gt 0 ]; do
    case "$1" in
    --expect-arm)
        expect_arm=true
        ;;

    --with-tests)
        with_tests=true
        ;;

    -h|--help)
        print_usage
        exit 0
        ;;

    *)
        printf '[FAIL] unknown option: %s\n' "$1" >&2
        print_usage >&2
        exit 2
        ;;
    esac

    shift
done

printf '%s\n' "netflow-analyzer target environment check"
printf '%s\n' "-----------------------------------------"

architecture=$(uname -m 2>/dev/null || printf '%s' "unknown")
printf '[INFO] CPU architecture: %s\n' "$architecture"

case "$architecture" in
aarch64|arm64|armv7l|armv8l)
    print_pass "ARM architecture detected"
    ;;

*)
    if [ "$expect_arm" = true ]; then
        print_failure \
            "expected an ARM target, found $architecture"
    else
        print_warning \
            "current system is not ARM; this is acceptable on the development PC"
    fi
    ;;
esac

if [ -r /etc/os-release ]; then
    os_name=$(
        sed -n 's/^PRETTY_NAME=//p' /etc/os-release |
            tr -d '"'
    )

    if [ -n "$os_name" ]; then
        printf '[INFO] Operating system: %s\n' "$os_name"
    else
        printf '%s\n' \
            "[INFO] Operating system: /etc/os-release found"
    fi
else
    print_warning "/etc/os-release is not available"
fi

check_required_command \
    cc \
    "C compiler"

check_required_command \
    cmake \
    "CMake"

check_required_command \
    pkg-config \
    "pkg-config"

if command -v cc >/dev/null 2>&1; then
    compiler_version=$(
        cc --version 2>/dev/null |
            sed -n '1p'
    )

    printf '[INFO] Compiler: %s\n' \
        "${compiler_version:-unknown version}"
fi

if command -v cmake >/dev/null 2>&1; then
    cmake_version=$(
        cmake --version 2>/dev/null |
            sed -n '1p'
    )

    printf '[INFO] CMake: %s\n' \
        "${cmake_version:-unknown version}"
fi

#
# Ninja不是唯一可用的生成器。
# 如果没有Ninja，但存在make，项目仍然能够构建。
#
if command -v ninja >/dev/null 2>&1; then
    print_pass "Build tool: Ninja"
elif command -v make >/dev/null 2>&1; then
    print_pass "Build tool: Make"
    print_warning \
        "Ninja is unavailable; use the default Unix Makefiles generator"
else
    print_failure "neither Ninja nor Make is available"
fi

#
# libpcap是正式程序的必需依赖。
#
if command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists libpcap; then
        pcap_version=$(
            pkg-config --modversion libpcap
        )

        print_pass "libpcap development package: $pcap_version"
    else
        print_failure \
            "pkg-config cannot find the libpcap development package"
    fi
fi

#
# Python只用于端到端测试，不是正式程序的运行依赖。
#
if command -v python3 >/dev/null 2>&1; then
    python_version=$(
        python3 --version 2>&1
    )

    print_pass "Python test interpreter: $python_version"
else
    if [ "$with_tests" = true ]; then
        print_failure \
            "Python 3 is required because --with-tests was specified"
    else
        print_warning \
            "Python 3 is missing; production builds can use BUILD_TESTING=OFF"
    fi
fi

#
# Git只在直接从GitHub取得代码时需要。
#
if command -v git >/dev/null 2>&1; then
    print_pass "Git is available"
else
    print_warning \
        "Git is unavailable; copy the source tree to the target manually"
fi

if [ -w . ]; then
    print_pass "current directory is writable"
else
    print_failure "current directory is not writable"
fi

if command -v df >/dev/null 2>&1; then
    disk_information=$(
        df -h . 2>/dev/null |
            sed -n '2p'
    )

    printf '[INFO] Build filesystem: %s\n' \
        "${disk_information:-unknown}"
fi

printf '%s\n' "-----------------------------------------"

if [ "$required_failure_count" -ne 0 ]; then
    printf '[FAIL] environment check found %s required problem(s)\n' \
        "$required_failure_count"
    exit 1
fi

printf '%s\n' "[PASS] environment is ready for the selected build mode"
exit 0