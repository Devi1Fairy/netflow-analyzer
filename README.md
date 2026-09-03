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

`v0.2.0`在这条链路上增加了流过期清理、FNV-1a哈希流表、CSV导出、实时抓包和BPF过滤。当前CLI可以通过`--interface NAME [--count PACKETS] [--filter EXPRESSION]`从网卡读取数据包，并复用离线模式的协议解析、双向流聚合和终端汇总流程。省略`--count`时持续运行到收到停止信号，显式提供时则作为人工验收或测试的有限包数上限。

当前`Unreleased`开发进度进一步为实时模式加入`SIGINT`和`SIGTERM`优雅退出，以及基于`pcap_stats()`的运行统计。用户按下Ctrl+C或服务管理器发送终止信号后，程序会中断阻塞的libpcap读取，沿正常控制流取得libpcap累计统计、关闭采集句柄，并输出已经处理的数据包、抓包丢弃情况和流汇总。

实时主循环现在还会按数据包事件时间执行流过期：使用不会随乱序包倒退的时间高水位，每5秒扫描一次，并把空闲30秒的流在处理当前包之前输出和删除。过期记录先复制到调用者提供的固定容量缓冲区，因此删除槽位后终端仍能取得完整结果；程序结束时分别显示累计过期流和仍在流表中的流。

实时模式还会按单调时钟约每5秒输出一次运行指标，包括区间包数、完整/截断/畸形/不支持/流表拒绝分类、捕获/线路字节数、PPS、Mbps、活动流数量、流表占用率、过期流和容量淘汰流数量。采集句柄使用非阻塞模式，主循环通过`poll()`等待网卡可读或最多1秒超时，因此即使没有任何匹配包，也能定期输出零流量报告，同时避免忙轮询持续占用CPU。

程序退出时还会输出流表线性探测统计：数据包路径操作次数、实际检查槽位总数、平均探测长度、最大探测长度和计数是否饱和。`flow_table_find()`等管理查询不进入该统计，满载新流最终返回`ENOSPC`之前发生的完整扫描仍会记录，因此可以区分正常查找成本与容量拒绝成本。选择`evict-oldest`时，同一次满表探测还会确定最旧流并在原槽位完成替换，应用层不接触流表槽位，也不再发起淘汰后的第二次哈希探测。

当前`Unreleased`还为每条TCP流增加了旁路连接状态跟踪。状态机根据规范化流方向和TCP标志识别`unobserved`、`syn-seen`、`syn-ack-seen`、`established`、`midstream`、`fin-seen`、`fin-bidirectional`、`closed`和`reset`。终端流汇总与离线CSV复用同一组稳定名称；UDP和ICMP的`tcp_state`明确写为`not-applicable`。该状态机描述分析器实际观察到的报文过程，不等同于Linux内核socket状态，也尚未执行序列号确认、乱序处理或TCP字节流重组。

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

- 实时模式已经支持BPF过滤、`SIGINT`/`SIGTERM`优雅退出、libpcap抓包统计和周期运行指标，但打开网卡仍需要相应Linux权限；
- `pcap_stats()`字段的精确统计范围依赖操作系统和捕获后端，不能假定`Capture received packets`一定等于应用输出的`Total packets`；
- 只支持Ethernet链路类型和IPv4，不支持VLAN、IPv6与隧道封装；
- 已跟踪基本TCP握手、FIN关闭、RST中止和中途捕获状态，但不做IPv4分片重组、TCP乱序/字节流重组和校验和验证；
- 流表容量仍固定为256个槽位；已有流在满载时仍可更新，实时模式默认让新流数据包计入`flow_rejected`，也可以通过`--flow-full-policy evict-oldest`按`last_seen`淘汰最久未活动流并接纳当前包。该策略已经在一次满表探测中同时选择最旧流并原位替换，不再重复探测；当前仍没有动态扩容；
- 实时流过期暂时固定为空闲30秒、每5秒事件时间扫描一次，尚未开放CLI配置；
- 流过期的事件时间只随实际收到的数据包推进，接口完全静默时要等下一包到来才判断旧流；周期运行指标使用单调时钟，因此静默时仍会按时输出；
- 尚未解析DNS、HTTP等应用层协议；
- 尚未实现规则异常检测、机器学习、Qt界面或云端展示；
- ARM Linux开发板已完成原生Debug/Release构建、当前18项CTest、离线跨平台一致性、两种交叉产物、物理网卡抓包、真实TCP完整关闭、单流/多流性能、满载边界、流表探测成本和10分钟长稳基线；最新官方SDK产物又完成单次满表扫描优化复测，300个UDP新流对应300次探测操作、44次最旧流淘汰、256条最终流和零drop。
- LubanCat-2N已完成非root systemd手工启停、开机自启和第一批低风险沙箱加固：服务进程使用无登录专用用户，只获得`CAP_NET_RAW`且`NoNewPrivs=1`；静默周期日志、真实ICMP、双向流汇总和SIGTERM收尾均进入journal。受控重启后服务在无人登录和未人工`start`时一次启动成功，systemd 245安全评分由`5.2 MEDIUM`降为`3.7 OK`；服务方式异常恢复和长稳仍待验证。
- 目标镜像的`resize-all.service`会扫描`/proc/mounts`中的已挂载分区；一次SD卡持久化挂载实验触发VFAT重建且恢复失败。该服务现已禁用并屏蔽，板端Git工作树将恢复到eMMC ext4，SD卡在重新初始化和隔离挂载策略前保持卸载。

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
│   ├── tcp_flow_state.h
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
│       ├── tcp_flow_state.c
│       ├── flow_table.c
│       └── flow_expiration.c
├── tests/
│   ├── unit/
│   └── integration/test_offline_flow.py
├── scripts/check_target_env.sh
├── packaging/systemd/
│   ├── netflow-analyzer.service
│   └── netflow-analyzer.default
├── docs/
│   ├── problem_log.md
│   ├── technical_decisions.md
│   └── systemd_deployment.md
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

# 不设置包数上限，持续运行到Ctrl+C或外部SIGTERM。
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --filter "icmp"

# 只接收lo上的IPv4 ICMP流量；--count只统计匹配过滤器的数据包。
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"

# 流表满载时淘汰last_seen最早的流，并接纳当前新流。
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 300 \
    --filter "udp and dst portrange 30000-30299" \
    --flow-full-policy evict-oldest
```

若要观察周期运行指标，可以省略包数上限，让程序跨越多个5秒区间：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --filter "icmp"
```

先保持接口静默，确认出现`packets=0`报告；再在另一个终端运行`ping -i 0.2 -c 30 127.0.0.1`，确认流量进入后续报告；最后等待流量再次归零并按Ctrl+C结束。

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
| `-c N`、`--count N` | 可选的实时包数上限，N必须大于0；省略时持续运行到停止信号 |
| `--filter EXPRESSION` | 为实时抓包安装BPF过滤表达式；含空格时需要使用引号 |
| `--flow-full-policy POLICY` | 设置实时流表满载策略：默认`reject`，或使用`evict-oldest`淘汰最久未活动流 |
| `--csv FILE` | 把流记录导出到一个新CSV文件，不覆盖已有文件 |

离线分析会先显示文件与链路类型，再预览前5个数据包。程序仍会处理文件中的所有数据包，最后输出总包数、预览包数和双向流汇总。TCP流汇总包含`tcp_state`；指定`--csv`后，应用层在聚合成功后创建CSV文件，写入固定表头和全部流记录，其中TCP写入稳定阶段名称，UDP和ICMP写入`not-applicable`。C11的独占创建模式会在目标已存在时失败，避免静默覆盖原文件。

实时分析会等待网卡流量；提供`--count`时，达到指定数据包数量后结束，省略时则持续运行。两种模式收到`SIGINT`或`SIGTERM`停止请求后，都会取得libpcap运行统计、关闭采集句柄并输出流汇总。不提供`--filter`时，计数针对接口上返回的全部数据包，不只包含用户主动执行`ping`等命令产生的流量；例如VS Code及其本地服务也可能通过`lo`持续交换TCP数据。提供过滤器后，libpcap在数据包进入应用读取循环前执行匹配，只有匹配包会进入协议解析、流聚合和可选的`--count`计数。`--count`限制处理包数而不是等待时间；没有足够的匹配流量时程序会继续等待，用户可以使用Ctrl+C安全结束并保留已经聚合的结果。

实时过期调度器使用捕获数据包时间戳的最大值作为时间高水位，避免乱序包让时间倒退。扫描发生在当前包加入流表之前，因此同一五元组在空闲30秒后重新出现时，旧记录会先输出和删除，当前包再建立新记录。`Expired flows`是运行期间已经输出的累计数量，最终`Flow summary`只包含仍留在流表中的记录。

周期运行指标与流过期使用不同的时间概念：前者使用`CLOCK_MONOTONIC`测量真实运行间隔，后者使用数据包时间戳维护事件时间。`captured_mbps`按应用实际取得的`caplen`计算，`wire_mbps`按`wirelen`计算；二者都使用真实经过秒数作分母，而不是假定每次恰好5.000秒。libpcap打开接口时配置的读取超时属于抓包缓冲区超时，不保证在完全静默时唤醒读取，因此实时句柄显式设置为非阻塞，并由`poll()`提供有界等待。

每个成功取得的数据包最终只进入`complete`、`truncated`、`malformed`、`unsupported`或`flow_rejected`中的一类。周期报告显示当前窗口增量，多个窗口的分类需要求和；退出时的`Processing results`显示整个运行过程的累计量。`flow_rejected`统计无法进入满载流表的数据包数量，不是不同流的数量。

实时输出中的`Total packets`表示应用实际从libpcap取得的包数；`Capture received packets`、`Capture dropped packets`和`Interface dropped packets`来自捕获后端。它们观察的层级不同，不能通过`Capture received packets - Total packets`计算丢包。例如在Linux的`lo`接口上执行`ping -c 2`时，应用会处理2个请求和2个响应，共4个逻辑ICMP包；内核捕获统计可能同时计算每个包的outgoing和incoming事件而报告8，libpcap再抑制交付给应用的回环重复副本。这种`Total packets: 4`、`Capture received packets: 8`的结果不是应用重复处理或发生4个丢包。

## 自动化测试

```bash
# 先编译，确保测试程序和主程序都是最新版本。
cmake --build build

# 执行CMake已经注册的全部测试。
# --output-on-failure只在失败时展开测试输出，便于定位问题。
cmake -E chdir build ctest --output-on-failure
```

当前共18项测试：

- 17项C语言单元测试，分别验证字节读取、抓包与BPF及非阻塞等待封装、数据模型、各层协议解析、分发、流键、流记录、TCP状态机、流表、流过期调度、周期运行指标和CSV格式化；
- 1项Python端到端测试内部运行三个确定性场景：6包ICMP PCAP验证完整分析、5包预览、双向流统计和`not-applicable` CSV；3包TCP三次握手验证终端与CSV的`established`状态；260包压力PCAP验证截断、畸形、不支持、流表满载拒绝及满载后继续运行。

只运行端到端验收：

```bash
# -R按测试名称筛选，只运行离线流聚合验收。
cmake -E chdir build ctest -R offline_flow_acceptance --output-on-failure
```

## 模块职责

| 模块 | 当前职责 |
|---|---|
| `main.c` | 安装和恢复实时模式的POSIX信号处理器，调用应用接口，并把错误码转换为进程退出状态 |
| `app.c` | 解析CLI参数，组织离线或实时采集、停止请求、协议解析、流表更新、周期运行指标和输出 |
| `byte_reader` | 对无符号字节执行边界检查、跳过、读取和切片 |
| `capture` | 隔离libpcap类型和错误信息，统一封装离线文件、实时网卡采集、BPF编译安装、非阻塞等待、读取中断和实时抓包统计 |
| `packet_info` | 保存一个数据包的元数据、原始视图和解析结果 |
| `ethernet` | 解析MAC地址、EtherType和Ethernet负载 |
| `ipv4` | 解析IPv4头部、地址、长度、协议号和负载 |
| `tcp`、`udp`、`icmp` | 解析对应传输层或控制协议字段 |
| `ipv4_dispatch` | 根据IPv4协议号选择具体解析器 |
| `flow_key` | 生成与方向无关的规范化双向五元组 |
| `flow_record` | 保存一条流、两个方向的统计信息，并为TCP流推进独立连接状态 |
| `tcp_flow_state` | 根据规范化方向和TCP标志跟踪握手、中途捕获、FIN关闭与RST中止，提供稳定状态名称 |
| `flow_table` | 查找或创建流记录、聚合数据包，并在过期删除前返回值副本 |
| `flow_expiration` | 维护事件时间高水位、扫描周期和空闲截止时间，处理乱序及整数边界 |
| `runtime_metrics` | 根据单调时钟和累计计数生成处理结果分类、区间PPS、Mbps、流表占用率及过期流指标 |
| `flow_export` | 把流记录转换成具有固定字段顺序的CSV表头和数据行，包括TCP状态或非TCP的`not-applicable` |

## ARM Linux部署准备

仓库中的环境检查脚本只做准备性检查，不安装软件，也不修改系统：

```bash
# 在开发电脑上检查通用构建依赖；非ARM只会给出提示。
sh scripts/check_target_env.sh

# 在开发板上要求目标必须是ARM，并检查完整测试环境。
sh scripts/check_target_env.sh --expect-arm --with-tests
```

LubanCat-2N已经完成ARM64原生Debug/Release构建、当前18项板端CTest、确定性离线PCAP端到端验收，以及来自VMware NAT虚拟机的物理网卡ICMP实时抓包。新增TCP状态功能也已在`lo`上通过真实HTTP/1.0连接验收：应用处理12个完整TCP包，聚合为1条双向流并最终输出`tcp_state=closed`，两个drop字段均为0。板端Python 3.8.10最初无法解释Python 3.9才支持的`list[tuple[...]]`类型注解；验收脚本改用`typing.List`和`typing.Tuple`后，目标测试及全量18项CTest均通过。

板载系统位于容量8GB的eMMC；当前根文件系统约7.0GB，已用5.4GB、可用1.4GB。一次为VFAT SD卡固化挂载权限的实验暴露了目标镜像中`usbmount`与`resize-all.service`的冲突：扩容辅助脚本扫描已挂载分区并重建了VFAT，但恢复失败。该服务现已禁用并屏蔽；源码权威副本仍在开发电脑和GitHub，板端工作树下一步恢复到`/home/cat/workspace/netflow-analyzer`的ext4目录。项目源码与活动构建树只有MB量级，可以放入eMMC；PCAP、CSV和模型数据集等大文件在SD卡重新初始化并验证唯一挂载策略后再迁入。

开发电脑已经完成两条ARM64交叉编译和板端运行链：一条使用鲁班猫官方Buildroot GCC 9.3及隔离的板端libpcap overlay，另一条使用Ubuntu GCC 13、从板端导出的完整sysroot和GCC `-B`启动文件前缀。两种产物均为AArch64 ELF、只要求`GLIBC_2.17`，并在板端通过`ldd`、`--help`和实时ICMP抓包。完整Shell环境变量、CMake参数、ABI检查与故障记录见[交叉编译手册](docs/cross_compilation.md)。

提交`740d5ab`的官方SDK ARM64产物已经通过暂存安装打包并部署为systemd服务。程序和配置由root拥有，运行进程使用`netflow-analyzer`专用账户；systemd只授予`CAP_NET_RAW`，不把文件capability永久写入ELF。首次手工启停处理4个完整ICMP包、聚合1条双向流，两个drop字段为0，并在SIGTERM后正常收尾。完整的Linux命令、用户/组、权限、capability、journal、故障定位和回滚见[非root systemd部署手册](docs/systemd_deployment.md)。

开发板上的CTest为3.16.3，低于`--test-dir`参数所需的3.20，因此测试时应先进入构建目录再运行`ctest`。同一确定性6包PCAP此前在x86_64与ARM64原生构建上得到一致标准输出和退出状态；SD卡事故前，当前18项测试也已在两侧全部通过，工作树恢复到eMMC后需要重新验证板端基线。Release单流基线最高约9 Kpps、20万包零drop、每包CPU约6.95微秒；128流长稳基线以约9 Kpps处理540万包，全部分类为`complete`，两个drop为0、每包CPU约6.24微秒，RSS采样从首到尾保持564 KiB。300流容量测试也精确得到256个完整流和44个`flow_rejected`。完整方法见[单流性能基线](docs/performance_baseline.md)和[多流与长稳基线](docs/multiflow_longrun_baseline.md)。

## 后续迭代

建议按以下顺序推进：

1. 把板端Git工作树恢复到eMMC ext4并复跑18项CTest，随后进行systemd接口故障恢复和服务方式长稳验收；
2. 处理同一五元组关闭后重新建连，随后增加TCP乱序与字节流重组，再进入DNS、HTTP等应用层解析；
3. 当前继续保持单线程；只有后续测量证明单线程成为瓶颈，才复审实验中的阻塞队列和线程流水线；
4. 如果真实负载超过256条活跃流，再根据占用率、探测长度、拒绝和淘汰数据评估可配置容量、重建或动态扩容；
5. 实现规则异常检测，再准备机器学习特征与模型；
6. 在稳定的数据接口之上实现Qt上位机，并按需要扩展云端展示。

版本变化见[CHANGELOG.md](CHANGELOG.md)，实际问题、原因和修复过程见[docs/problem_log.md](docs/problem_log.md)，技术、环境和硬件选型见[docs/technical_decisions.md](docs/technical_decisions.md)，两种ARM64构建方式见[docs/cross_compilation.md](docs/cross_compilation.md)，非root服务安装见[docs/systemd_deployment.md](docs/systemd_deployment.md)，首轮板端测量见[docs/performance_baseline.md](docs/performance_baseline.md)。
