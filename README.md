# Netflow Analyzer

基于Linux C11的网络流量分析与异常检测学习项目。项目面向嵌入式Linux应用开发和网络协议分析岗位，逐步实现数据包采集、协议解析、双向流量聚合、异常检测、可视化与ARM Linux部署。

## 当前版本

当前源码版本：`v0.2.0`。

`v0.0.1`只建立了可构建、可运行、可测试的工程骨架；`v0.1.0`已经形成第一条可用的离线分析链路：

```text
离线PCAP
   │
   ▼
libpcap读取
   │
   ▼
Ethernet II → IPv4 → TCP / UDP / ICMP
   │
   ▼
规范化双向五元组
   │
   ▼
流表聚合 → 逐包预览 + 流汇总
```

因此，`v0.1.0`可以视为一个完整版本迭代，但不是整个项目完成。它具备明确输入、完整处理链、可观察输出和自动化验收；实时抓包、流过期、应用层协议、异常检测、可视化和开发板部署仍属于后续版本。

`v0.2.0`在这条链路上增加了流过期清理、FNV-1a哈希流表、CSV导出、实时抓包和BPF过滤。CLI可以通过`--interface NAME --count PACKETS [--filter EXPRESSION]`从网卡读取有限数量的数据包，并复用离线模式的协议解析、双向流聚合和终端汇总流程。

当前`Unreleased`开发进度进一步为实时模式加入`SIGINT`和`SIGTERM`优雅退出，以及基于`pcap_stats()`的运行统计。用户按下Ctrl+C或服务管理器发送终止信号后，程序会中断阻塞的libpcap读取，沿正常控制流取得libpcap累计统计、关闭采集句柄，并输出已经处理的数据包、抓包丢弃情况和流汇总。

实时主循环现在还会按数据包事件时间执行流过期：使用不会随乱序包倒退的时间高水位，每5秒扫描一次，并把空闲30秒的流在处理当前包之前输出和删除。过期记录先复制到调用者提供的固定容量缓冲区，因此删除槽位后终端仍能取得完整结果；程序结束时分别显示累计过期流和仍在流表中的流。

## v0.2.0新增

- 为流表增加稳定哈希、开放寻址和按最后活动时间清理过期流的接口；
- 支持将聚合后的流记录导出为CSV文件；
- 支持通过`--interface NAME --count PACKETS`进行有界实时抓包；
- 支持通过`--filter EXPRESSION`为实时抓包安装BPF过滤器；
- 使用离线自动化测试和`lo`真实流量完成BPF过滤验收。

## v0.1.0已经实现

- 使用libpcap读取离线Ethernet PCAP文件；
- 使用带边界检查的字节游标读取二进制网络数据；
- 统一记录时间戳、`caplen`、`wirelen`、原始数据视图和解析状态；
- 解析Ethernet II、IPv4、TCP、UDP和ICMP头部；
- 识别不支持、格式错误、捕获截断和IPv4分片等状态；
- 按IPv4协议号把负载分发到TCP、UDP或ICMP解析器；
- 使用规范化双向五元组把正向和反向数据包归入同一条流；
- 分方向统计包数、字节数、首包时间和末包时间；
- 使用调用者提供存储的定长流表完成离线聚合；
- 分析PCAP中的全部数据包，同时只预览前5个包，避免大量输出淹没终端；
- 提供13个C语言单元测试和1个Python端到端验收测试；
- 通过CMake组织构建、严格编译警告、CTest和`compile_commands.json`。

## 当前版本边界

当前版本有意保持实现简单，便于先验证数据模型和处理链：

- 实时模式已经支持BPF过滤、`SIGINT`/`SIGTERM`优雅退出和libpcap抓包统计，但打开网卡仍需要相应Linux权限；
- `pcap_stats()`字段的精确统计范围依赖操作系统和捕获后端，不能假定`Capture received packets`一定等于应用输出的`Total packets`；
- 只支持Ethernet链路类型和IPv4，不支持VLAN、IPv6与隧道封装；
- 不做IPv4分片重组、TCP流重组和校验和验证；
- 流表容量仍固定为256个槽位；实时过期可以复用旧槽位，但尚未实现动态扩容、负载因子控制和满载驱逐；
- 实时流过期暂时固定为空闲30秒、每5秒事件时间扫描一次，尚未开放CLI配置；
- 事件时间只随实际收到的数据包推进，接口完全静默时不会定时输出，下一包到来后才继续判断过期；
- 尚未解析DNS、HTTP等应用层协议；
- 尚未实现规则异常检测、机器学习、Qt界面或云端展示；
- ARM Linux开发板部署等待硬件到达后验证，不作为`v0.2.0`的发布阻塞项。

## 项目目录

```text
netflow-analyzer/
├── CMakeLists.txt
├── README.md
├── CHANGELOG.md
├── include/analyzer/
│   ├── app.h
│   ├── byte_reader.h
│   ├── capture.h
│   ├── packet_info.h
│   ├── ethernet.h
│   ├── ipv4.h
│   ├── tcp.h
│   ├── udp.h
│   ├── icmp.h
│   ├── ipv4_dispatch.h
│   ├── flow_key.h
│   ├── flow_record.h
│   ├── flow_table.h
│   └── flow_expiration.h
├── src/
│   ├── main.c
│   ├── app/app.c
│   ├── common/byte_reader.c
│   ├── capture/capture.c
│   ├── protocol/
│   │   ├── packet_info.c
│   │   ├── ethernet.c
│   │   ├── ipv4.c
│   │   ├── tcp.c
│   │   ├── udp.c
│   │   ├── icmp.c
│   │   └── ipv4_dispatch.c
│   └── flow/
│       ├── flow_key.c
│       ├── flow_record.c
│       ├── flow_table.c
│       └── flow_expiration.c
├── tests/
│   ├── unit/
│   └── integration/test_offline_flow.py
├── scripts/check_target_env.sh
├── docs/
│   ├── problem_log.md
│   └── technical_decisions.md
└── labs/
```

`build/`和`build-release/`是CMake生成的构建目录，不属于源代码，不应提交到Git。

## 环境要求

正式程序需要：

- Linux；
- 支持C11的GCC或Clang；
- CMake 3.16或更高版本；
- Make或Ninja；
- pkg-config；
- libpcap开发包。

启用测试时还需要Python 3。Python只负责端到端验收，不是正式程序的运行依赖。

Ubuntu安装命令：

```bash
# 更新本机的软件包索引。
sudo apt update

# 安装C/CMake构建环境、Ninja、libpcap开发包和测试解释器。
sudo apt install build-essential cmake ninja-build pkg-config libpcap-dev python3
```

## Debug构建

在项目根目录执行：

```bash
# -S .指定当前目录为源码目录。
# -B build把生成文件放入build目录。
# Debug保留调试信息，适合日常开发、GDB和单元测试。
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 根据build中生成的规则编译全部目标。
cmake --build build
```

主要构建产物：

```text
build/bin/netflow-analyzer    命令行主程序
build/bin/*_tests             C语言单元测试程序
build/lib/libanalyzer_core.a  核心静态库
```

如果`build`已经使用其他CMake生成器配置，不要直接切换`-G Ninja`。可以继续使用原生成器，或新建另一个构建目录。

## Release构建

```bash
# Release启用编译优化。
# BUILD_TESTING=OFF表示发布构建不生成测试目标，也不要求Python。
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF

# 编译发布程序。
cmake --build build-release
```

Release主程序位于`build-release/bin/netflow-analyzer`。

## 命令行使用

```bash
# 显示帮助和所有可用参数。
./build/bin/netflow-analyzer --help

# 显示由CMake项目版本传入的程序版本。
./build/bin/netflow-analyzer --version

# 读取并分析一个离线PCAP文件。
./build/bin/netflow-analyzer --read /path/to/input.pcap

# 分析离线PCAP，并把最终双向流记录写入一个新CSV文件。
./build/bin/netflow-analyzer \
    --read /path/to/input.pcap \
    --csv /path/to/flows.csv

# 从lo实时读取4个数据包，然后输出逐包预览和双向流汇总。
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4

# 只接收lo上的IPv4 ICMP流量；--count只统计匹配过滤器的数据包。
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"
```

验证ICMP过滤时，可以让上面的过滤命令在终端1等待，再在终端2执行：

```bash
ping -c 2 127.0.0.1
```

验证实时流过期时，可以在终端1运行过滤后的4包采集，在终端2先执行一次IPv4 ping，等待至少31秒后再执行一次：

```bash
ping -c 1 127.0.0.1
sleep 31
ping -c 1 127.0.0.1
```

第二次ping的第一包到来时，程序会先输出第一段ICMP流，再用当前包创建新的流。最终输出应同时包含：

```text
Expired flows: 1
Flow summary: 1 flow(s)
```

当前参数：

| 参数 | 作用 |
|---|---|
| `-h`、`--help` | 显示帮助 |
| `-V`、`--version` | 显示版本 |
| `-r FILE`、`--read FILE` | 分析离线PCAP文件 |
| `-i NAME`、`--interface NAME` | 选择实时抓包网卡 |
| `-c N`、`--count N` | 实时模式最多读取N个数据包，N必须大于0 |
| `--filter EXPRESSION` | 为实时抓包安装BPF过滤表达式；含空格时需要使用引号 |
| `--csv FILE` | 把流记录导出到一个新CSV文件，不覆盖已有文件 |

离线分析会先显示文件与链路类型，再预览前5个数据包。程序仍会处理文件中的所有数据包，最后输出总包数、预览包数和双向流汇总。指定`--csv`后，应用层在聚合成功后创建CSV文件，写入固定表头和全部流记录；C11的独占创建模式会在目标已存在时失败，避免静默覆盖原文件。

实时分析会等待网卡流量，达到`--count`指定的数据包数量，或收到`SIGINT`、`SIGTERM`停止请求后，取得libpcap运行统计、关闭采集句柄并输出流汇总。不提供`--filter`时，计数针对接口上返回的全部数据包，不只包含用户主动执行`ping`等命令产生的流量；例如VS Code及其本地服务也可能通过`lo`持续交换TCP数据。提供过滤器后，libpcap在数据包进入应用读取循环前执行匹配，只有匹配包会增加`--count`计数并进入协议解析和流聚合。当前`--count`限制处理包数而不是等待时间；没有足够的匹配流量时程序会继续等待，用户可以使用Ctrl+C安全结束并保留已经聚合的结果。

实时过期调度器使用捕获数据包时间戳的最大值作为时间高水位，避免乱序包让时间倒退。扫描发生在当前包加入流表之前，因此同一五元组在空闲30秒后重新出现时，旧记录会先输出和删除，当前包再建立新记录。`Expired flows`是运行期间已经输出的累计数量，最终`Flow summary`只包含仍留在流表中的记录。

实时输出中的`Total packets`表示应用实际从libpcap取得的包数；`Capture received packets`、`Capture dropped packets`和`Interface dropped packets`来自捕获后端。它们观察的层级不同，不能通过`Capture received packets - Total packets`计算丢包。例如在Linux的`lo`接口上执行`ping -c 2`时，应用会处理2个请求和2个响应，共4个逻辑ICMP包；内核捕获统计可能同时计算每个包的outgoing和incoming事件而报告8，libpcap再抑制交付给应用的回环重复副本。这种`Total packets: 4`、`Capture received packets: 8`的结果不是应用重复处理或发生4个丢包。

## 自动化测试

```bash
# 先编译，确保测试程序和主程序都是最新版本。
cmake --build build

# 执行CMake已经注册的全部测试。
# --output-on-failure只在失败时展开测试输出，便于定位问题。
ctest --test-dir build --output-on-failure
```

当前共16项测试：

- 15项C语言单元测试，分别验证字节读取、抓包与BPF封装、数据模型、各层协议解析、分发、流键、流记录、流表、流过期调度和CSV格式化；
- 1项Python端到端测试，生成确定性的6包PCAP，启动真实命令行程序并验证完整分析、5包预览、双向流统计和CSV文件内容。

只运行端到端验收：

```bash
# -R按测试名称筛选，只运行离线流聚合验收。
ctest --test-dir build -R offline_flow_acceptance --output-on-failure
```

## 模块职责

| 模块 | 当前职责 |
|---|---|
| `main.c` | 安装和恢复实时模式的POSIX信号处理器，调用应用接口，并把错误码转换为进程退出状态 |
| `app.c` | 解析CLI参数，组织离线或实时采集、停止请求、协议解析、流表更新和输出 |
| `byte_reader` | 对无符号字节执行边界检查、跳过、读取和切片 |
| `capture` | 隔离libpcap类型和错误信息，统一封装离线文件、实时网卡采集、BPF编译安装、读取中断和实时抓包统计 |
| `packet_info` | 保存一个数据包的元数据、原始视图和解析结果 |
| `ethernet` | 解析MAC地址、EtherType和Ethernet负载 |
| `ipv4` | 解析IPv4头部、地址、长度、协议号和负载 |
| `tcp`、`udp`、`icmp` | 解析对应传输层或控制协议字段 |
| `ipv4_dispatch` | 根据IPv4协议号选择具体解析器 |
| `flow_key` | 生成与方向无关的规范化双向五元组 |
| `flow_record` | 保存一条流及两个方向的统计信息 |
| `flow_table` | 查找或创建流记录、聚合数据包，并在过期删除前返回值副本 |
| `flow_expiration` | 维护事件时间高水位、扫描周期和空闲截止时间，处理乱序及整数边界 |
| `flow_export` | 把流记录转换成具有固定字段顺序的CSV表头和数据行 |

## ARM Linux部署准备

开发板到达前不需要提前宣称“部署完成”。仓库中的环境检查脚本只做准备性检查，不安装软件，也不修改系统：

```bash
# 在开发电脑上检查通用构建依赖；非ARM只会给出提示。
sh scripts/check_target_env.sh

# 开发板到达后，要求目标必须是ARM，并检查完整测试环境。
sh scripts/check_target_env.sh --expect-arm --with-tests
```

首次上板计划采用目标机原生编译：先确认依赖，再完成Release构建、离线PCAP分析和结果对比。原生流程稳定后，再评估交叉编译工具链。

## 后续迭代

建议按以下顺序推进：

1. 增加应用层解析结果计数以及PPS、Mbps、流表占用率等周期运行指标；
2. 使用性能和容量数据决定是否增加流表满载驱逐、负载因子控制、重建或动态扩容；
3. 把实验中的阻塞队列和线程流水线接入正式分析链；
4. 增加TCP状态跟踪、流重组与DNS、HTTP等应用层解析；
5. 实现规则异常检测，再准备机器学习特征与模型；
6. 开发板到达后完成ARM Linux原生构建与运行验收；
7. 在稳定的数据接口之上实现Qt上位机，并按需要扩展云端展示。

版本变化见[CHANGELOG.md](CHANGELOG.md)，实际问题、原因和修复过程见[docs/problem_log.md](docs/problem_log.md)，技术、环境和硬件选型见[docs/technical_decisions.md](docs/technical_decisions.md)。
