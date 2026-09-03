# Netflow Analyzer会话交接文档

最后更新：2026-09-02（Asia/Shanghai）

本文用于把当前项目状态、学习背景、协作方式、代码架构、测试、Git历史、已知边界和下一步计划完整交接给新的Codex会话。接手者应先完整阅读本文，再执行只读检查，不要根据标题直接开始大范围修改。

## 1. 新会话接手顺序

新会话建议先执行：

```bash
cd /home/zcb/workspace/netflow-analyzer

git status --short
git branch --show-current
git log -5 --oneline --decorate

cmake --build build
cmake -E chdir build ctest --output-on-failure
```

预期基线：

- 当前功能分支：`feature/nonroot-service`；
- 远程仓库：`git@github.com:Devi1Fairy/netflow-analyzer.git`；
- 当前分支已提交无上限实时模式、验收文档、CMake安装规则、非root systemd单元和服务日志行缓冲；记录本文档时最新已推送提交为`740d5ab fix(output): stream service logs promptly`，实际接手时仍须以`git log`为准；
- TCP状态机、流记录接入、终端显示、CSV字段和确定性三次握手验收均已提交；接手时仍须先检查工作区，不能覆盖用户后续未提交改动；
- 当前正式版本宏为`0.2.0`；
- 已有标签：`v0.0.1`、`v0.1.0`、`v0.2.0`；
- Debug构建目录：`/home/zcb/workspace/netflow-analyzer/build`；
- 本地主程序：`build/bin/netflow-analyzer`；
- 官方SDK交叉构建目录：`/home/zcb/build/netflow-analyzer-lubancat-sdk-release-v2`；
- 通用GCC交叉构建目录：`/home/zcb/build/netflow-analyzer-generic-sysroot-release`；
- x86_64与LubanCat-2N ARM64原生Debug构建的18项CTest全部通过；单次扫描优化已完成Ubuntu `lo`和LubanCat-2N物理网卡300流手工验收。优化版官方SDK ARM64产物只要求`GLIBC_2.17`，板端得到300次操作、44次淘汰、256条最终流和零drop；新增TCP状态功能也已在`lo`真实HTTP/1.0连接中处理12包并最终输出`closed`，两个drop字段均为0。
- 实时`--count`已改为可选上限；本机`lo`在省略上限后能于静默期正常报告，随后处理4个`complete` ICMP包，并在`SIGTERM`后完成统计、流汇总与清理。
- 主程序已在stdout首次I/O前显式启用行缓冲；严格普通文件重定向测试在进程结束前读到5秒周期报告，避免systemd journal日志延迟到缓冲区填满或服务退出。
- 提交`740d5ab`的官方SDK ARM64部署包已在LubanCat-2N完成首次非root systemd手工启停：进程使用专用用户，能力仅为`CAP_NET_RAW`，`NoNewPrivs=1`；真实4包ICMP得到1条双向流且两个drop字段为0，SIGTERM正常收尾。
- `systemctl enable`后的受控重启也已通过：boot ID变化，无人工`start`时服务已为`enabled`和`active`，一次启动成功且`NRestarts=0`；新PID保持相同专用身份和唯一`CAP_NET_RAW`能力，静默报告与重启后ICMP正确。

如果实际状态与上面不同，应先查看用户是否在新会话开始前继续修改了代码，不要覆盖未提交改动。

## 2. 用户目标与项目定位

用户正在准备嵌入式Linux应用开发、网络协议分析和DPI相关岗位。项目不是为了重新实现Wireshark或tcpdump，而是为了展示一条可嵌入业务程序的网络数据处理链：

```text
数据包采集
    ↓
二进制安全读取
    ↓
二层到四层协议解析
    ↓
规范化双向五元组
    ↓
流量聚合与过期
    ↓
规则异常检测 / 机器学习特征
    ↓
CSV、Qt上位机或云端展示
    ↓
ARM Linux开发板部署
```

目标岗位JD曾包含：

1. TCP/IP网络中二到七层协议分析、研究和内容解析开发；
2. DPI处理框架及应用开发与维护。

因此本项目当前的libpcap采集、协议解析、五元组流表、线程实验和未来应用层解析，与目标方向有直接关联。项目最终仍需要体现嵌入式Linux特点，例如受限资源、长期运行、网卡权限、交叉或原生构建、进程生命周期、性能和开发板部署，而不应只停留在Ubuntu桌面离线解析。

项目最初参考的两份外部规划文档仍位于：

- `/home/zcb/workspace/嵌入式Linux_网络协议与异常检测_学习项目路线.md`；
- `/home/zcb/workspace/网络流量分析与异常检测项目_逐步实现手册.md`。

它们用于理解长期路线，当前真实实现和下一步应以仓库代码、README、CHANGELOG及本文为准。

## 3. 与用户协作时必须遵守的约定

### 3.1 源代码由用户亲自输入

用户明确要求：

- C源码、头文件、CMake和测试代码由助手完整展示并解释，用户自己在VS Code中输入；
- 除非用户之后明确改变要求，否则不要直接替用户创建或修改C源码；
- 助手负责在功能验证后维护`README.md`、`CHANGELOG.md`、`docs/problem_log.md`和`docs/technical_decisions.md`；
- 用户删除过助手直接写入的源码，因此这一点非常重要。

提供代码时应：

- 先说明本步骤为什么做、对完整项目有什么作用；
- 给出准确文件路径、当前代码锚点或行号，以及“插入、替换或删除”的范围；
- `.h`和对应`.c`尽量同一步给出；
- 代码注释要完整、规范，并说明关键数据类型、所有权、生命周期和错误码；
- 命令行逐条解释用途，不要只贴命令；
- 控制单步规模，复杂步骤可以拆分，但不要把同一小功能机械拆成过多轮；
- 用户曾要求“代码多时写清原代码和行数”，因此优先给局部修改而不是重复整份大文件。

### 3.2 每一步都要测试和Git

每个实现步骤应包含：

1. 重点单元测试；
2. 全量CTest回归；
3. 必要的手动验收；
4. 文档更新；
5. `git add`、提交标题和`git push`说明。

通常使用：

```bash
cmake --build build
cmake -E chdir build ctest --output-on-failure
```

不要提交`build/`、`build-release/`或链接后的可执行文件。它们是可重建产物，不属于源码仓库。

### 3.3 讲解风格

用户希望理解代码，不只是让项目跑通。解释时尤其要覆盖：

- 为什么需要某个结构体或函数；
- 它位于整个处理链的哪一层；
- 指针指向谁、谁拥有内存、谁负责释放；
- `size_t`、固定宽度整数、字节序、错误码等类型选择；
- 线程、锁、条件变量和资源生命周期的时间顺序；
- 测试在自动化流程中的位置；
- 为什么采用当前技术，而不是只说“行业一般这样做”。

用户C语言熟悉度高于Python。Python验收脚本可以解释执行流程、参数解析和返回码，但除非用户追问，不必逐行展开所有Python语法。

日志使用正常的工程技术写作风格，不要每句话都写“我”。只记录真实发生的问题和可验证结论，不要为了面试故事编造虚假故障。可以把具体问题整理成面试素材，但要保持事实基础。

用户已经说过纯代码格式问题暂时不用专门修改。除非格式导致警告、可读性严重下降或掩盖逻辑错误，否则优先推进功能。

## 4. 当前构建环境

### 4.1 工具与依赖

项目当前使用：

- Ubuntu Linux；
- GCC；
- ISO C11，关闭编译器语言扩展；
- CMake 3.16及以上；
- Ninja；
- pkg-config；
- libpcap开发包；
- Python 3，仅用于端到端验收；LubanCat自带Python 3.8.10已经验证；
- Git和GitHub；
- VS Code及CMake Tools/C语言扩展。

主要CMake设置：

- `CMAKE_C_STANDARD 11`；
- `CMAKE_C_EXTENSIONS OFF`；
- `CMAKE_EXPORT_COMPILE_COMMANDS ON`；
- `-Wall -Wextra -Wpedantic`；
- 可执行文件输出到`build/bin`；
- 静态库输出到`build/lib`；
- 核心代码构建为`analyzer_core`静态库；
- libpcap通过`pkg_check_modules(... IMPORTED_TARGET libpcap)`引入；
- `capture.c`对libpcap的依赖为PRIVATE，上层公开头文件不暴露`pcap_t`。

### 4.2 Debug构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

如果`build`已经使用其他生成器配置，不要在同一目录直接改用Ninja。继续使用原生成器，或建立新的构建目录。

### 4.3 Release构建

```bash
cmake \
    -S . \
    -B build-release \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF

cmake --build build-release
```

Release主程序位于`build-release/bin/netflow-analyzer`。

### 4.4 ARM64交叉构建

当前有两条已经在LubanCat-2N运行成功的Release交叉构建路径：

- 官方Buildroot GCC 9.3、SDK内置glibc 2.29 sysroot和隔离的板端libpcap overlay；
- Ubuntu `aarch64-linux-gnu-gcc` 13、板端完整sysroot和GCC `-B`启动文件前缀。

两种产物均为AArch64 ELF，动态加载器为`/lib/ld-linux-aarch64.so.1`，最终只要求`GLIBC_2.17`。交叉配置使用`BUILD_TESTING=OFF`；逻辑回归由x86_64与ARM64板端原生构建的18项CTest负责，交叉产物加载和真实采集由板端`ldd`、`--help`和ICMP测试负责。

完整目录、环境变量、CMake命令和故障处理见[`docs/cross_compilation.md`](cross_compilation.md)。

### 4.5 VS Code与CMake历史问题

曾经出现VS Code能够由CMake编译，但编辑器找不到头文件和不能补全的问题。关键点：

- CMake的`target_include_directories`只告诉编译器头文件路径，不一定自动修复编辑器配置；
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`生成的`compile_commands.json`可以让VS Code/clangd知道真实编译参数；
- 必须在VS Code中打开项目根目录`/home/zcb/workspace/netflow-analyzer`，不要误开`/home/zcb`或`/home/zcb/workspace`；
- 之前CMake Tools把构建输出配置到了`/home/zcb/build`，输出显示项目名为`zcb`，这说明工作区根目录选错；
- 正确构建目录应是`${workspaceFolder}/build`，也就是仓库内的`build`；
- 调试曾因VS Code默认“编译当前C文件”任务而尝试单独链接没有`main`的`queue.c`，出现`undefined reference to main`；正确做法是调试CMake生成的完整可执行目标，而不是单文件活动任务；
- GDB提示`Failed to set controlling terminal: Operation not permitted`通常不是业务代码故障；
- 调试启动曾非常慢，改为正确的CMake目标配置后明显恢复；调试当前不是项目主线。

## 5. 仓库和版本状态

GitHub仓库：`https://github.com/Devi1Fairy/netflow-analyzer`

SSH远程：

```text
origin git@github.com:Devi1Fairy/netflow-analyzer.git
```

当前重要提交，从新到旧：

```text
d427168 feat(output): export TCP flow state
29aa018 feat(app): report TCP state in flow summaries
1eea4d8 feat(flow): expose TCP phase names
83586c1 feat(flow): track TCP lifecycle in flow records
31ee759 feat(flow): replace oldest flow in one probe
b23fa59 docs(board): record ARM64 eviction validation
183617c docs(flow): record eviction policy validation
be08b7e feat(flow): add oldest-flow eviction policy
dd5221e feat(flow): report hash probe statistics
25d2c06 docs(board): record multiflow and soak baselines
295bf5c docs(board): record single-flow performance baseline
b070f09 docs(board): record ARM64 17-test baseline
a0de7df tag: v0.2.0
a8fd76f feat(cli): expose live capture filter option
8b047d2 feat(capture): add BPF filter support
61076f1 feat(cli): analyze bounded live packet streams
47d37ab feat(cli): add live capture interface probe
bf73660 feat(capture): add live interface opening
d92dd62 feat(cli): export flow summary to CSV
7296618 feat(output): add CSV flow exporter
7f6adad feat(flow): use hash table for flow aggregation
da61a6f docs: add technical decision log
194ec56 feat: add stable flow key hashing
234175f feat: add idle flow expiration
ce60c7b tag: v0.1.0, merge release PR
ebced21 test: add offline flow acceptance coverage
629c889 feat: aggregate offline packets into flows
1db9799 feat: add fixed-capacity flow table
03c7280 feat: add bidirectional flow record model
e2455b8 feat: add bidirectional flow key model
4d14044 feat: dispatch and display IPv4 payload protocols
f4d4a2b feat: parse ICMP messages
3208159 feat: parse UDP datagrams
e32a14c feat: parse TCP segments
```

用户已经练习过feature分支、compare链接、Pull Request、review和merge。历史PR包括至少`#1`、`#2`，`v0.1.0`通过PR `#4`合入。近期为了连续学习直接在`main`提交；如果下一阶段需要模拟团队协作，应先创建feature分支，再通过PR合入，不要假设必须直接推main。

`v0.1.0`是第一条完整离线分析链的发布标签。`v0.2.0`已经发布哈希流表、过期底层接口、CSV、实时抓包和BPF过滤。信号优雅退出、libpcap运行统计、实时流过期、容量淘汰、探测统计和TCP状态跟踪属于`Unreleased`开发进度，当前不需要仅因这一功能立即更换版本标签。

## 6. 当前目录结构与职责

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
│   ├── flow_expiration.h
│   ├── flow_export.h
│   └── runtime_metrics.h
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
│   ├── flow/
│   │   ├── flow_key.c
│   │   ├── flow_record.c
│   │   ├── tcp_flow_state.c
│   │   ├── flow_table.c
│   │   └── flow_expiration.c
│   ├── metrics/runtime_metrics.c
│   └── output/flow_export.c
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
│   ├── systemd_deployment.md
│   └── session_handoff.md
└── labs/
    ├── blocking_queue/
    ├── tcp_framing/
    └── thread_pipeline/
```

模块职责：

| 模块 | 职责 |
|---|---|
| `main.c` | 在首次输出前建立stdout行缓冲，初始化上下文、解析参数、安装停止信号处理、运行应用、统一清理，并把错误码转换为进程退出码 |
| `app.c` | 应用编排；连接采集、协议解析、流表、周期运行指标和输出，不承担具体协议字段解析 |
| `byte_reader` | 使用边界检查游标读取、跳过和切分原始二进制字节 |
| `capture` | 封装libpcap，统一离线文件、实时网卡、BPF、非阻塞等待、中断和运行统计接口，隐藏`pcap_t`、`struct pcap_stat`与`DLT_*` |
| `packet_info` | 保存一条数据包的捕获元数据、协议字段和解析状态 |
| `ethernet` | 解析Ethernet II头、MAC地址和EtherType |
| `ipv4` | 解析IPv4头、长度、地址、TTL、协议号和分片信息 |
| `tcp` | 解析TCP头、端口、序列号、确认号、标志和负载视图 |
| `udp` | 解析UDP头、端口、长度和负载视图 |
| `icmp` | 解析ICMP类型、代码、echo字段和负载视图 |
| `ipv4_dispatch` | 按IPv4协议号分发到TCP、UDP或ICMP解析器 |
| `flow_key` | 生成规范化双向五元组，判断相等，计算FNV-1a 64位哈希 |
| `flow_record` | 保存一个双向流的两个方向统计、首末时间，并为TCP流推进独立连接状态 |
| `tcp_flow_state` | 根据规范化方向和TCP标志跟踪握手、中途捕获、FIN关闭和RST中止，提供稳定阶段名称 |
| `flow_table` | 开放寻址哈希表、线性探测、删除标记、查找、遍历、返回值副本的过期删除，以及数据包路径探测成本统计 |
| `flow_expiration` | 维护数据包事件时间高水位、扫描周期和空闲截止时间，处理乱序与整数边界 |
| `flow_export` | 将流记录写成固定字段顺序的CSV表头和记录，包含TCP阶段或非TCP的`not-applicable` |
| `runtime_metrics` | 使用单调时钟和累计值差分计算区间PPS、Mbps、流表占用率和过期流数量 |

`common`目录存放不属于某一种网络协议或业务模块、但多个模块可复用的基础工具。当前只有安全字节读取器，未来可以放通用时间、错误转换、日志等工具，但不要把所有无法分类的业务代码都堆入`common`。

## 7. 当前端到端数据流

离线模式：

```text
CLI --read FILE [--csv FILE]
    ↓
app_parse_arguments
    ↓
capture_open_offline
    ↓
capture_next_packet，直到PCAP EOF
    ↓
app_process_packet
    ↓
packet_info_init
    ↓
ethernet_parse
    ↓ EtherType == 0x0800
ipv4_parse
    ↓ protocol = 1 / 6 / 17
ipv4_dispatch_payload
    ↓
icmp_parse / tcp_parse / udp_parse
    ↓
flow_table_process_packet
    ↓
flow_key_from_packet + flow_key_hash
    ↓
flow_record_init或flow_record_update
    ↓ TCP流
tcp_flow_state_observe推进旁路连接阶段
    ↓
终端逐包预览（最多5包）
    ↓
处理完整文件后输出流汇总
    ↓ 可选
CSV导出
```

实时模式：

```text
CLI --interface NAME [--count N] [--filter EXPRESSION]
    [--flow-full-policy reject|evict-oldest]
    ↓
提供N时严格解析为大于0的size_t；省略时无包数上限
    ↓
capture_open_live
    ↓
检查链路类型必须为Ethernet
    ↓
可选capture_set_filter
    ↓
capture_enable_nonblocking
    ↓
capture_next_packet
    ↓ 暂时无包EAGAIN
capture_wait_readable使用poll等待可读或最多1秒
    ↓
检查CLOCK_MONOTONIC；约每5秒输出区间运行指标
    ↓ 可读后再次读取
    ↓ 收到包
更新flow_expiration事件时间高水位
    ↓ 每推进5秒
计算30秒空闲截止时间
    ↓
flow_table_expire_before复制并删除旧流
    ↓
输出Expired flow和累计过期数量
    ↓
当前包进入与离线模式复用的解析链
    ↓
输出完整/截断/畸形/不支持/流表拒绝分类
    ↓ 完整且有容量
流表聚合；满载时按策略拒绝新流，或在同一次满表扫描中选择并原位替换last_seen最早流
    ↓
总捕获包数达到N，或收到SIGINT/SIGTERM
    ↓
capture_get_statistics
    ↓
关闭capture句柄
    ↓
输出应用包数、libpcap统计、预览数和流汇总
```

`app_run_capture_analysis()`现在同时编排离线和实时输入。它根据`app_context_t.command`选择打开方式，后续处理链不区分数据来自文件还是网卡。这是刻意的职责分离：采集来源差异停留在采集层和应用编排层，协议解析器只接收字节与长度。

## 8. 关键数据模型与所有权

### 8.1 `capture_t`和二级指针

`capture_t`是不透明对象。结构体只在`capture.c`定义，公开头文件仅有：

```c
typedef struct capture capture_t;
```

打开函数使用：

```c
int capture_open_live(..., capture_t **capture, ...);
```

调用者拥有一个`capture_t *`变量，把它的地址`&capture`传入。函数成功后通过二级指针修改调用者的指针，使其指向新对象。内部流程坚持“局部创建、完整验证、最后发布”：

1. `calloc`分配`capture_t`；
2. libpcap打开句柄；
3. 查询并转换链路类型；
4. 全部成功才执行`*capture = new_capture`；
5. 失败时释放局部对象，不覆盖调用者原值。

关闭函数接收`capture_t **`，调用`pcap_close`和`free`后再把调用者指针设为`NULL`，避免悬空指针和重复关闭。

### 8.2 数据包视图生命周期

`capture_packet_view_t.data`指向libpcap管理的内存：

- 调用者不能修改；
- 调用者不能`free`；
- 只保证在下一次`capture_next_packet()`调用之前有效；
- 当前应用必须在下一次读取前完成协议解析；
- `packet_info_t`只保存解析出的数值和固定数组，不保存原始指针，因此可以安全进入流表统计。

### 8.3 `caplen`和`wirelen`

- `captured_length/caplen`：实际保存在内存或PCAP中的字节数，是所有数组边界检查的唯一依据；
- `wire_length/wirelen`：数据包在线路上的原始长度，只用于统计，不能用来访问`data`；
- `caplen < wirelen`表示抓包截断；被截掉的数据不会自动出现在“下一个包”，而是已经丢弃的当前包尾部。

### 8.4 `packet_info_t`

统一结果结构目前保存：

- 秒和微秒时间戳；
- caplen、wirelen；
- Ethernet MAC、EtherType、网络负载位置；
- IPv4头长度、总长度、ID、DF/MF、分片偏移、TTL、协议号、校验和、地址；
- TCP端口、序列号、确认号、头长、标志、窗口、校验和、紧急指针、负载位置和截断状态；
- UDP端口、长度、校验和、负载位置和截断状态；
- ICMP类型、代码、校验和、echo ID/sequence、负载位置和截断状态；
- 解析状态、错误层和错误偏移。

它不拥有动态内存，不需要`free`。

### 8.5 双向五元组

传统五元组是：

```text
源IP、源端口、目标IP、目标端口、协议号
```

项目把两个端点按IP和端口排序，得到`endpoint_a`和`endpoint_b`，因此正向包和反向包生成相同键。方向另存为`A_TO_B`或`B_TO_A`。注意：

- A/B只是规范化排序结果，不等于客户端/服务器；
- TCP/UDP使用真实端口；
- ICMP没有端口概念，两个端口统一为0；
- 同一流的两个方向共享一条`flow_record_t`；
- 每个方向分别统计包数、捕获字节和线路字节。

### 8.6 哈希流表

流表使用：

- FNV-1a 64位哈希；
- 固定字段顺序和固定字节序；
- 不直接哈希结构体原始内存，避免填充字节和主机字节序造成不稳定；
- 开放寻址；
- 线性探测；
- `EMPTY`、`OCCUPIED`、`DELETED`三种槽状态；
- 固定容量256槽，由`app.c`栈上数组提供；
- 当前不支持动态扩容；
- 当前没有内部锁，不能被多个线程同时修改。

数据包路径还累计线性探测操作数、检查槽位总数和最大探测长度。起始槽位算1次检查；命中、插入和满载`ENOSPC`都计入，`flow_table_find()`等管理查询不计入。两个`uint64_t`累计值达到上限后饱和而不回绕；应用退出时输出平均、最大和饱和状态。`evict-oldest`组合接口也只把当前包记为一次探测操作；满表扫描已经取得最旧候选，因此不再执行淘汰后的第二次哈希探测。

`DELETED`不能直接变回`EMPTY`，否则会提前截断发生哈希冲突后的探测链。流表完全为空时可以统一清除删除标记。

### 8.7 流表存储所有权

`flow_table_t`借用调用者提供的`flow_table_slot_t[]`，不拥有它。`flow_table_cleanup()`只解除借用并清空状态，不对数组调用`free`。当前数组是`app_run_capture_analysis()`的局部栈数组，流表只在该函数返回前使用，因此生命周期有效。

### 8.8 CSV输出所有权

`flow_export`函数借用调用者传入的`FILE *`，只写数据，不负责关闭。`app.c`负责：

1. 以C11的`"wx"`模式独占创建文件；
2. 拒绝覆盖已有文件；
3. 写表头；
4. 遍历流表写记录；
5. 无论成功失败都调用`fclose`；
6. 检查`fclose`可能报告的缓冲刷新错误。

当前CSV只允许离线模式使用，实时模式的参数组合会拒绝`--csv`。

CSV在协议号后使用稳定的`tcp_state`字段。TCP流通过`tcp_flow_phase_name()`输出状态机阶段，UDP和ICMP写入`not-applicable`。状态名称来自核心状态模块，终端和CSV不各自维护重复映射。字段仅含小写字母和短横线，不需要额外CSV转义。

### 8.9 TCP流状态与所有权

每条TCP `flow_record_t`按值保存独立的`tcp_flow_state_t`。状态对象只包含枚举、规范化方向和布尔值，不保存原始报文指针，不拥有动态内存，因此流记录复制、过期和淘汰时不需要额外`free`。

当前状态包括：

- `unobserved`：已经初始化但尚未观察报文；
- `syn-seen`、`syn-ack-seen`、`established`：完整握手路径；
- `midstream`：从连接中途开始捕获，不能证明完整握手；
- `fin-seen`、`fin-bidirectional`、`closed`：旁路观察到的关闭路径；
- `reset`：观察到RST中止。

`endpoint_a`和`endpoint_b`只表示规范化排序，不等于客户端和服务器；初始SYN的方向单独保存在`initiator_direction`。`handshake_completed`是历史事实，即使流之后关闭或重置也继续保留。`closed`和`reset`暂时是终止状态，同一五元组重新建立新连接留给下一轮生命周期设计。

这不是Linux内核TCP状态机的复制：当前不验证ACK号是否精确确认SYN或FIN，不处理乱序、重叠、重传字节、TCP选项和字节流重组，也不能仅凭状态字段宣称已经具备应用层DPI。

## 9. 协议与二进制解析原则

### 9.1 为什么使用字节游标

网络包是无符号二进制数据，不是C字符串。`byte_cursor_t`维护：

- 当前字节地址；
- 总长度；
- 当前读取偏移。

读取、跳过和切片前都做边界检查，避免越界。负载视图通常通过“父游标跳到负载位置，再建立子游标”实现。父游标和子游标都只是视图，不拥有底层内存。

### 9.2 为什么使用`memcpy`而不是字符串函数

网络数据可以包含任意`0x00`字节，也不保证以`'\0'`结尾。`strcpy`、`strlen`等字符串函数会在零字节处停止，或者越过真实缓冲区寻找终止符。固定长度二进制字段应先检查剩余长度，再使用`memcpy`复制准确字节数。

这一问题已经作为具体调试经验记录在问题日志，可用于面试说明“错误工具处理二进制数据导致测试失败，随后改为长度驱动的复制”。

### 9.3 大端和小端

网络多字节整数使用网络字节序，也就是大端：高位字节先出现。x86主机通常是小端。解析器按字节显式组合数值或通过安全读取函数转换，最终在结构体中保存主机可直接比较的整数。

MAC地址是固定6字节数组，没有把整个地址当作整数，因此不需要整体字节序转换。

### 9.4 当前包层次

普通Ethernet/IPv4/TCP包大致是：

```text
Ethernet II头 14字节
    目标MAC 6
    源MAC 6
    EtherType 2

IPv4头 20～60字节
    版本/IHL、总长度、ID、分片、TTL、协议、校验和、源/目标IP等

TCP头 20～60字节
    源/目标端口、序列号、确认号、标志、窗口、校验和等

TCP负载 可变长度
```

UDP和ICMP位于IPv4负载位置，与TCP是并列的上层协议，不是TCP内部内容。

EtherType `0x0800`是IEEE/IANA约定的IPv4标识，不是项目自定义示例。IPv4协议号1、6、17分别表示ICMP、TCP、UDP。

## 10. 当前CLI能力

### 10.1 帮助和版本

```bash
./build/bin/netflow-analyzer --help
./build/bin/netflow-analyzer --version
```

### 10.2 离线分析

```bash
./build/bin/netflow-analyzer --read /path/to/input.pcap
```

行为：

- 读取完整PCAP；
- 只预览前5个包，避免终端被淹没；
- 第6包及以后仍会解析并参与流聚合；
- 文件结束后输出总包数、预览数和流汇总。

### 10.3 离线CSV

```bash
./build/bin/netflow-analyzer \
    --read /path/to/input.pcap \
    --csv /path/to/new-flows.csv
```

目标文件必须不存在。已有文件不会被覆盖。

### 10.4 实时有限或持续分析

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"
```

`--count`使用严格正整数解析：

- 0拒绝；
- 负数拒绝；
- 非数字拒绝；
- 超出`size_t`范围拒绝；
- `--count`不能与`--read`组合；
- 省略`--count`时，实时模式持续运行到收到停止请求或致命错误；
- 当前实时模式不能与`--csv`组合。

实时流表满载策略：

- 默认`--flow-full-policy reject`，新流包计入`flow_rejected`并继续运行；
- 可选`--flow-full-policy evict-oldest`，在确认新键不存在的同一次满表扫描中按最小`last_seen`复制最久未活动流，并用当前新流原位替换；
- 原位替换成功的当前包计入`complete`，淘汰使用独立`evicted_flows`计数；
- 该选项第一版只允许实时模式使用，离线模式仍保持完整文件级聚合和固定容量拒绝语义。

提供上限时达到数量，或任意实时模式收到`SIGINT`/`SIGTERM`后，程序都在关闭capture句柄前查询`pcap_stats()`，输出捕获后端接收、抓包缓冲区丢弃和接口丢弃统计。统计查询失败只降级显示不可用，不丢弃已经完成的流分析。

打开实时接口通常需要root权限或适当Linux capability。当前手工验收使用`sudo`运行主程序，但不要使用`sudo cmake`或`sudo cmake --build`，避免构建目录出现root所有权文件。

## 11. 最新实时抓包验收结果

系统接口：

```text
lo     Linux回环接口
ens33  VMware呈现给Ubuntu虚拟机的Ethernet接口
```

`lo`捕获本机经`127.0.0.1`或`::1`进行的通信，包不会离开系统。`ens33`承载虚拟机与宿主机、局域网或互联网的通信，具体范围取决于VMware NAT、桥接或Host-only模式。

最新周期指标验收执行：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 100 \
    --filter "icmp"
```

先保持接口静默，程序连续输出约5.007至5.008秒的零包报告；另一个终端执行`ping -i 0.2 -c 30 127.0.0.1`后，60个逻辑ICMP包分别进入16包和44包两个报告区间；流量结束后报告再次归零。Ctrl+C正常收尾，最终输出`Total packets: 60`、`Capture received packets: 120`和两个drop字段为0。

无上限服务生命周期前置验收省略`--count`，使用`timeout --signal=TERM`模拟systemd停止。程序启动时显示`Packet limit: unlimited`，静默期先输出0包报告；一次`ping -c 2 127.0.0.1`产生4个`complete`包，随后`SIGTERM`沿正常路径收尾。最终后端报告8包，两个drop字段均为0，证明没有依赖包数上限才能完成清理。

重要结论：

- 实时抓包看到的是接口上的全部匹配流量，不是只看到用户为了测试主动产生的流量；
- BPF已经能排除无关的VS Code回环TCP流量；
- `--count`限制包数，不限制等待时间；接口没有足够流量时程序会继续等待；
- libpcap读取缓冲区超时不是应用周期定时器；显式非阻塞模式配合`poll()`后，完全静默时主循环仍能定期获得控制权；
- 周期指标使用`CLOCK_MONOTONIC`和真实区间计算，`poll()`的1秒超时只决定检查粒度；
- Ctrl+C和SIGTERM已经能中断等待并沿正常路径输出已有统计；
- Linux回环包会出现outgoing和incoming捕获事件，libpcap向应用交付前会抑制重复副本，因此捕获后端的120和应用处理的60处于不同统计层级；
- 不能用`Capture received packets - Total packets`推导丢包，应查看专门的drop字段并保留平台可用性限制。

## 12. 当前测试体系

当前x86_64 Debug构建的CTest共18项，最近一次全量执行全部通过，总耗时约0.10秒：

| 编号 | CTest名称 | 主要覆盖 |
|---:|---|---|
| 1 | `analyzer_smoke_tests` | 上下文生命周期、帮助、版本、离线/实时CLI参数及错误组合 |
| 2 | `byte_reader_tests` | 字节读取、跳过、切片、边界和失败不修改输出 |
| 3 | `capture_tests` | PCAP打开、链路类型、逐包读取、BPF、非阻塞与等待参数、中断、实时统计、离线不支持语义和关闭 |
| 4 | `packet_info_tests` | 统一结果对象初始化、时间戳和错误状态 |
| 5 | `ethernet_tests` | Ethernet II字段、负载、截断和格式化 |
| 6 | `ipv4_tests` | IPv4长度、IHL、地址、分片、截断和畸形输入 |
| 7 | `tcp_tests` | TCP头字段、标志、负载和边界 |
| 8 | `udp_tests` | UDP长度、端口、负载和非法组合 |
| 9 | `icmp_tests` | ICMP通用字段、echo字段和负载 |
| 10 | `ipv4_dispatch_tests` | 协议号分发、未知协议和分片处理 |
| 11 | `flow_key_tests` | 双向规范化、方向、字段比较和稳定FNV-1a哈希 |
| 12 | `flow_record_tests` | 两个方向统计、首末时间、错误和溢出保护，以及TCP状态随首包和后续包推进、非TCP无状态 |
| 13 | `flow_table_tests` | 哈希冲突、线性探测、回绕、删除标记、复用、遍历、过期、探测统计、查询隔离、饱和保护，以及单次满表扫描选择并原位替换最旧流的成功/失败契约 |
| 14 | `offline_flow_acceptance` | 6包ICMP PCAP验证聚合、预览和`not-applicable` CSV；3包TCP握手验证终端与CSV的`established`；260包PCAP验证四类异常/拒绝、256槽满载、完整扫描和继续运行 |
| 15 | `flow_export_tests` | CSV表头、TCP状态字段、协议/状态不变量、记录顺序、格式化和无效参数 |
| 16 | `flow_expiration_tests` | 事件时间高水位、扫描边界、乱序时间戳、参数验证和截止时间下溢 |
| 17 | `runtime_metrics_tests` | 累计值差分、PPS/Mbps、流表占用率、零流量、时间边界和溢出保护 |
| 18 | `tcp_flow_state_tests` | 初始化、稳定名称、双向握手、重传、中途捕获、无效输入、FIN关闭、RST和终止状态 |

只运行重点测试示例：

```bash
cmake -E chdir build ctest \
    -R '^(analyzer_smoke_tests|capture_tests)$' \
    --output-on-failure
```

只运行离线端到端验收：

```bash
cmake -E chdir build ctest \
    -R '^offline_flow_acceptance$' \
    --output-on-failure
```

为什么真实实时抓包没有自动化验收：

- CI或开发机不一定有同名网卡；
- 抓包可能需要root或capability；
- 实时流量时序不确定；
- 不能假设外网可用；
- 自动测试不应修改系统网卡权限。

因此当前对实时功能采用三层验证：参数单元测试、采集接口的不依赖权限错误测试、人工`lo`验收。BPF使用离线确定性PCAP验证过滤语义；信号唤醒、`pcap_stats()`成功路径、实时流过期和静默周期报告使用真实`lo`手工验收。过期验收通过两次间隔31秒的IPv4 ping观察到一条过期ICMP流和一条最终活动ICMP流；周期指标验收覆盖静默、突发流量、再次静默和Ctrl+C。

## 13. 已完成的实验项目

### 13.1 `labs/blocking_queue`

实现了基于pthread互斥锁和条件变量的阻塞队列，覆盖：

- 初始化、push、pop、关闭、销毁；
- 环形`head`和`tail`；
- `count`区分空与满；
- `not_empty`和`not_full`条件变量；
- `while`循环防止虚假唤醒和条件变化；
- 关闭后唤醒等待线程；
- 基础、生命周期、阻塞和并发测试；
- 测试拆分及公共helper。

用户已经理解：环形下标按容量回绕，而可读取元素数量由`count`决定；队列未填满不会因此出错。

### 13.2 `labs/tcp_framing`

实现了：

- 自定义消息头编码/解码；
- magic、版本、类型和payload长度；
- 网络字节序；
- `send_exact`/`recv_exact`处理短写和短读；
- TCP服务端监听socket和每客户端通信socket；
- 客户端连接；
- 消息头和payload分阶段读写；
- 单元和集成测试。

用户已理解：TCP是字节流，一次`send`不保证发送完整buffer，一次`recv`也不保证拿到完整消息，因此必须循环；服务器监听socket只负责accept，每个连接另有通信socket，双方都必须关闭自己的描述符。

### 13.3 `labs/thread_pipeline`

实现了：

- 线程安全指针队列；
- 输入、worker和输出三类线程；
- 两个队列连接三阶段；
- 线程上下文结构；
- 堆对象的所有权转移；
- CSV输出；
- C单元测试和Python验收脚本。

用户已理解：线程入口只有一个`void *`参数，因此用context打包多个依赖；生产者在堆上创建对象并把指针推入队列，消费者取得指针后接管所有权，使用完成后`free`。

这些实验暂未接入正式程序。接入前应先测量实时采集、解析和输出是否存在速度差异，再决定线程数、队列容量、背压和关闭顺序，不要为了“线程多”而提前复杂化。

## 14. 已记录的主要问题和面试素材

详细过程见`docs/problem_log.md`。重要问题包括：

1. 二进制数据最初使用字符串思维处理，遇到零字节后改为边界检查和`memcpy`；
2. VS Code不知道CMake头文件目录，通过正确工作区和`compile_commands.json`修复；
3. 离线主循环错误地把“预览5包”当成“只处理5包”，拆分总包计数和预览计数后修复；
4. Python验收脚本`with`缩进错误导致测试未真正进入C程序；
5. 流表测试存在先用实际结果生成期望值的恒真断言，代码评审后修复；
6. CSV验收路径被放入错误函数，触发Python局部变量`NameError`；
7. 新增`parsed_interface_name`后漏初始化，原有`--read`测试被误判为输入冲突；
8. 未主动`ping`仍捕获到`lo` TCP包，通过`ss -tnp`定位到VS Code本地通信；
9. libpcap的`caplen`和`wirelen`容易混淆，明确规定前者用于内存边界、后者只用于线路统计；
10. 测试通过不等于测试有效，断言本身也需要评审。
11. `lo`上应用处理4个ICMP包而`pcap_stats().ps_recv`报告8，确认是回环方向事件与libpcap重复抑制造成的统计层级差异。

讲面试故事时推荐结构：

```text
背景和目标
→ 实际现象或失败输出
→ 如何缩小范围
→ 根因
→ 修复
→ 自动化或手工验证
→ 后续如何防止复发
```

不要只说“项目很顺利”或只说“查资料解决了”，应引用具体错误、数据和验证。

## 15. 重要技术决策

详细理由见`docs/technical_decisions.md`。当前关键决策：

- Linux C11作为核心实现语言；
- CMake组织多模块和测试；
- libpcap作为采集后端；
- 先离线PCAP建立确定性输入，再接实时网卡；
- tcpdump用于快速抓包和命令行对照，Wireshark用于图形化逐字段验证，不作为业务程序依赖；
- 项目公开接口不暴露libpcap类型；
- 网络包采用边界检查字节游标和明确长度，不使用字符串API；
- 解析层分为Ethernet、IPv4、TCP/UDP/ICMP和分发器；
- 双向五元组通过端点排序规范化；
- FNV-1a 64位用于稳定流键哈希；
- 开放寻址和线性探测用于当前固定容量流表；
- `DELETED`保留探测链；
- 活动流放内存，当前用CSV进行测试、演示和离线交换，不急于引入数据库；
- 可视化优先Qt上位机，云端展示作为后续扩展；
- BPF、信号退出和libpcap运行统计都停留在capture边界内，不向上层暴露第三方原生类型；
- 捕获后端计数与应用处理计数分别输出，不能用二者差值计算丢包；
- 不在没有性能数据时提前引入AF_XDP、DPDK或复杂线程池。
- 持续服务由程序显式保证stdout按行刷新，不依赖终端探测结果或外部`stdbuf`命令。

数据库目前不是必需项。只有出现历史查询、持久告警、用户配置、跨重启状态等明确需求时，再评估SQLite或外部数据库。

## 16. 当前已知边界和风险

### 16.1 实时抓包

- 已支持BPF、SIGINT/SIGTERM优雅退出和`pcap_stats()`累计统计；
- `--count`只限制成功捕获的包数，不限制等待时间；
- libpcap读取超时在不同平台和捕获后端上的行为可能不同，不能把它当作严格定时器；
- `pcap_stats()`字段语义和可用性依赖平台，`ps_recv`不等于应用完成处理的包数，`ps_ifdrop == 0`也可能表示指标不可用；
- 应用主循环已经提交完整、截断、畸形、不支持和流表拒绝分类，并在周期及退出汇总中输出；
- 当前混杂模式固定为false，没有CLI选项；
- 实时CSV尚未开放；
- `any`接口在Linux通常是cooked capture链路类型，当前只支持Ethernet，可能返回`ENOTSUP`；
- `lo`在当前环境表现为Ethernet链路并能解析，但其他平台不应无条件假设相同DLT。

### 16.2 协议

- 只支持Ethernet II和IPv4；
- 不支持VLAN、IPv6、PPPoE、Linux cooked capture和隧道；
- 不做IPv4分片重组；
- 已完成基于方向和标志的TCP握手、中途捕获、FIN与RST基本状态跟踪；
- 不做TCP乱序重组和字节流重组；
- 不核对ACK号是否精确确认SYN或FIN，同一五元组在`closed`或`reset`后的重新建连尚未处理；
- 不解析TCP选项；
- 不验证IPv4/TCP/UDP/ICMP校验和；
- 不解析DNS、HTTP、TLS等应用层协议；
- 未实现DPI内容识别和规则异常检测。

### 16.3 流表

- 固定256槽；
- 达到容量返回`ENOSPC`；
- 已输出数据包路径累计操作数、检查槽位总数、平均和最大探测长度；
- 实时主循环已经按事件时间定期调用过期API；接口完全静默时仍要等下一包推进事件时间；
- 过期记录删除前没有输出到下游；
- 没有自动重建或动态扩容；实时模式已经提供显式最旧流淘汰，但默认仍为拒绝；
- 没有并发保护。

### 16.4 工程质量

- 当前严格警告和全部测试通过；
- 源码存在少量不影响功能的格式不一致，用户已要求暂不专门处理格式问题；
- 实时路径只有手工验收，没有环境独立的端到端自动化；
- Sanitizer还没有正式接入主项目构建预设；此前已向用户介绍ASan/UBSan和TSan，但不应假设已配置完成。

## 17. 已完成阶段：BPF过滤

本节保留BPF实施时的设计依据和测试方案，相关接口、CLI、自动化测试和`lo`人工验收均已完成。不要再把本节当成当前下一步重复实现。

### 17.1 为什么现在做BPF

最近`lo`验收没有主动`ping`却先抓到VS Code TCP流，直接证明当前实时模式缺少“选择目标流量”的能力。BPF可以让libpcap在数据进入当前分析循环前筛选包，例如：

```text
icmp
tcp
udp port 53
host 127.0.0.1 and icmp
tcp port 8080
```

对实时抓包而言，过滤不仅让演示更确定，也能减少无关包进入用户态后的解析和聚合开销。

### 17.2 建议的公开采集接口

在`include/analyzer/capture.h`增加类似接口：

```c
int capture_set_filter(
    capture_t *capture,
    const char *filter_expression,
    char *error_buffer,
    size_t error_buffer_size);
```

建议语义：

- `capture`必须已经成功打开；
- 表达式必须非空；
- 错误缓冲区与大小成对提供或同时省略；
- 成功返回0并清空错误缓冲区；
- 参数错误返回`EINVAL`；
- BPF编译或安装失败返回`EIO`并复制libpcap错误；
- 失败不关闭capture，调用者仍负责`capture_close`。

### 17.3 建议的libpcap实现流程

`src/capture/capture.c`内部可使用：

```text
pcap_compile
    ↓ 成功
pcap_setfilter
    ↓
pcap_freecode
```

关键点：

- `struct bpf_program`属于临时编译结果；
- 无论`pcap_setfilter`成功还是失败，只要`pcap_compile`成功，就必须调用`pcap_freecode`；
- 初版可以使用`PCAP_NETMASK_UNKNOWN`；
- 优化参数可以设置为1；
- `pcap_geterr`返回的字符串依赖采集句柄，如需关闭句柄前保留，必须先复制；
- 不要把`struct bpf_program`或libpcap宏暴露到公开头文件。

正式编码前建议查阅当前系统安装的libpcap手册或官方文档，确认`pcap_compile`、`pcap_setfilter`、`pcap_freecode`和网络掩码语义。

### 17.4 建议的CLI模型

在`app_context_t`加入借用argv字符串的：

```c
const char *filter_expression;
```

目标命令：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"
```

建议第一版只允许`--filter`和实时`--interface`组合，避免同一步扩张过大。以后可以自然放宽到离线PCAP过滤，因为libpcap过滤器同样可用于离线句柄。

应用层应在以下顺序中安装过滤器：

```text
打开capture
→ 查询/验证链路类型
→ 安装BPF
→ 初始化或进入读取循环
```

如果安装过滤器失败，要复制错误信息、关闭capture并返回，不能进入未过滤读取。

### 17.5 建议的测试

采集模块单元测试不应依赖root或真实网卡。推荐使用测试生成的离线PCAP：

1. 打开离线PCAP；
2. 安装合法表达式，例如`icmp`；
3. 确认合法表达式编译和安装成功；
4. 使用明显非法表达式确认返回`EIO`且错误字符串非空；
5. 最好构造至少一个ICMP包和一个TCP/UDP包，安装`icmp`后验证只读到ICMP包；
6. 验证空表达式、空capture和不匹配错误缓冲区参数返回`EINVAL`；
7. 保持已有capture测试全部通过。

应用层冒烟测试覆盖：

- `--interface lo --count 4 --filter icmp`合法；
- 缺少filter值非法；
- 空字符串非法；
- 重复`--filter`非法；
- 如果第一版只支持实时模式，`--read file.pcap --filter icmp`非法；
- 初始化、重新解析和cleanup后`filter_expression`状态正确。

人工验收：

终端1：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"
```

终端2：

```bash
ping -c 2 127.0.0.1
```

预期只看到4个ICMP包，不再被VS Code的TCP回环流量提前占满`--count`。

### 17.6 BPF完成后的文档和提交

测试通过后由助手更新：

- README参数和示例；
- CHANGELOG Unreleased；
- technical_decisions中的BPF选择、过滤位置和性能意义；
- 如遇真实故障，再更新problem_log。

建议提交标题：

```text
feat(capture): add BPF packet filtering
```

如果采集API和CLI改动较大，也可以拆为两个独立提交：

```text
feat(capture): add BPF filter support
feat(cli): expose live capture filter option
```

## 18. 当前推荐路线

### 18.1 信号优雅退出（已完成）

当前已经使用`sigaction()`、`volatile sig_atomic_t`和`pcap_breakloop()`实现Ctrl+C及SIGTERM的安全结束。下面保留实施时需要持续遵守的约束。

注意：

- 信号处理函数只能执行异步信号安全的最小操作；
- 普通`bool`不一定适合直接作为信号通信对象，通常使用`volatile sig_atomic_t`标志；
- handler中不要调用`printf`、`malloc`、`free`、`fclose`或复杂清理；
- 主循环观察标志后按正常路径关闭capture、输出流和清理资源；
- libpcap阻塞读取是否需要`pcap_breakloop`以及它在目标平台的信号安全语义，实施前要核对官方文档；
- 后续可考虑`--count`变为可选：达到数量或收到信号均退出。

### 18.2 抓包运行统计（基础完成）

`capture_statistics_t`和`capture_get_statistics()`已经封装`pcap_stats()`，实时退出时分别输出捕获后端累计接收、抓包缓冲区丢包、接口丢包和应用实际取得的总包数。平台原生`struct pcap_stat`没有暴露给上层，离线句柄明确返回`ENOTSUP`。

协议解析结果分类、流表拒绝、容量淘汰、CPU、RSS以及线性探测成本均已可观察；实时满载策略支持默认拒绝和显式最旧流淘汰。周期PPS、Mbps、流表占用率、累计过期和淘汰数量已经完成。

### 18.3 实时流过期（已完成第一版）

实时主循环已经接入以下策略：

- 以最大已观察数据包时间戳作为事件时间高水位，乱序包不能让时间倒退；
- 空闲超时固定30秒，事件时间每推进5秒扫描一次；
- 在处理当前包之前扫描，防止长时间空闲后的同五元组包刷新旧记录；
- 流表先把过期`flow_record_t`按值复制到调用者的256条固定缓冲区，再删除槽位；
- 运行期间逐条输出`Expired flow`，退出时分别显示累计`Expired flows`和剩余`Flow summary`；
- 离线PCAP仍保持完整文件级聚合，不启用周期过期；
- 单元测试覆盖扫描边界、乱序、值副本、容量不足和整数下溢，当前全部17项CTest通过；
- `lo`上两次间隔31秒的IPv4 ping人工验收通过。

已知边界：接口完全静默时事件时间不推进，旧流要等下一包到来才输出；超时和周期尚未开放CLI配置；实时过期流和容量淘汰流当前只输出终端，没有追加到持续CSV；固定流表仍缺少动态扩容和可配置容量。

### 18.4 周期运行指标与静默唤醒（已完成）

当前已经完成：

- 独立`runtime_metrics`模块按累计值差分生成区间包数、字节数、PPS、捕获/线路Mbps、流表占用率和累计过期数量；
- 使用`CLOCK_MONOTONIC`测量真实经过时间，不受系统墙钟调整影响；
- 实时句柄显式进入非阻塞模式，没有可交付包时返回暂时无数据；
- capture层通过可选择文件描述符和`poll()`提供有界等待，应用每次返回后检查周期任务和停止请求；
- 没有增加统计线程，避免为当前单线程所有的计数和流表引入锁、一致快照及线程回收复杂度；
- 静默、突发60个ICMP包、再次静默和Ctrl+C人工验收通过，全部17项CTest通过。
- 五种互斥的数据包处理结果已经接入`app_process_packet()`、累计量、周期报告和退出汇总；流表满载的新流包计入拒绝后继续运行。

已知边界：当前报告周期固定为5秒，等待检查粒度固定为1秒；`poll()`依赖POSIX可选择描述符；单流、多流、整机软中断和10分钟长稳已经测量，但严格周期边界、千兆高PPS和生产级数小时/数天浸泡仍待验证。

### 18.5 依据性能测量暂缓接入线程流水线

候选正式结构：

```text
capture线程
    ↓ packet队列
worker线程（解析与流聚合）
    ↓ result/event队列
output线程（CSV/Qt/告警）
```

但是原始`packet.data`由libpcap拥有，只到下一次读取有效。capture线程如果把包交给其他线程，必须复制`captured_length`字节到拥有明确生命周期的堆对象，不能直接把libpcap内部指针推入队列。这是正式接入线程时最重要的所有权变化之一。

首轮LubanCat ARM64原生Release实测：

- 空闲进程平均CPU约0.045%，最大RSS约1.72 MiB；
- 约8.8～9.1 Kpps时处理20万包，进程平均CPU约4.25%，整体成本约6.95微秒/包；
- 应用拒绝、捕获drop和接口drop均为0，RSS保持约1.60～1.72 MiB。

因此当前不把实验流水线接入正式程序。完整命令、计算和边界见[`docs/performance_baseline.md`](performance_baseline.md)。

### 18.6 流表线性探测可观测性（已完成）

当前已经完成：

- `flow_table_process_packet()`累计数据包操作数、检查槽位总数和单次最大探测长度；
- 起始槽位计为1，命中、插入和满载`ENOSPC`扫描均进入统计；
- `flow_table_find()`等管理查询不污染数据包路径统计；
- `uint64_t`累计值使用饱和保护，饱和后退出报告不再显示误导性的平均值；
- 单元测试覆盖确定性1、2、3槽冲突链、查询隔离、满表扫描、失败输出不变和饱和不回绕；
- 离线端到端验收覆盖6包无冲突平均值1，以及260包满表最大值256；
- 全部17项本地CTest通过。

LubanCat官方SDK交叉产物实测：

- 128流、20万包、50%占用率：`operations=200000`、`inspected_slots=200000`、`average=1.00`、`maximum=1`，零拒绝和零drop；
- 300流满载：`operations=300`、`inspected_slots=11928`、`average=39.76`、`maximum=256`；44次拒绝贡献11264次完整扫描，前256次成功建流平均检查约2.59个槽；
- 探测统计版20万包进程CPU成本约6.95微秒/包、最大RSS 1724 KiB，与此前基线处于同一量级。

结论：当前受控50%占用工作负载没有哈希查找退化，无需为了探测性能立即更换哈希或动态扩容；固定256条活跃流仍是独立的业务容量边界。下一步若处理容量问题，应先明确内存上限、驱逐偏差和被驱逐流的输出所有权。

### 18.7 实时最旧流淘汰与单次扫描（已完成）

当前已经完成：

- CLI增加`--flow-full-policy reject|evict-oldest`，默认保持`reject`；
- `flow_table_evict_oldest()`按最小`last_seen`选择记录，删除前返回不依赖槽位生命周期的值副本；
- 新增`flow_table_process_packet_with_oldest_eviction()`，与默认拒绝路径复用内部探测逻辑；满表扫描同步记录最旧候选，确认新键不存在且所有槽位均被占用后，在原槽位发布已经完整初始化的新记录；
- 替换前按值复制旧记录，槽位保持`OCCUPIED`、`count`保持不变；当前包计入`complete`，淘汰事件单独计入周期和退出`evicted_flows`；应用层只选择策略，不取得槽位指针或下标；
- 单元测试覆盖最旧选择、刷新后保留、`DELETED`复用、值副本、探测统计隔离、空表、无效参数、内部计数不一致和最后一条流；
- 组合接口测试还覆盖普通插入、已有流更新、满表替换后的容量与查找、每包只增加一次探测操作，以及无效包、空输出参数、损坏的内部计数和全部失败输出不变；
- CLI测试覆盖默认值、重新解析复位、合法值、缺失、空、未知、重复、离线组合、无来源以及绕过解析的非法上下文；
- 优化前Ubuntu `lo`基线为`operations=344`、`inspected_slots=25055`、`average=72.83`和`maximum=256`；最新官方SDK ARM64 Release板端基线为344次操作、25505次槽位检查、平均74.14、最大256、44次淘汰和两个drop字段为0；
- 优化后Ubuntu `lo`用300个不同UDP五元组复测：`operations=300`、`inspected_slots=12416`、`average=41.39`、`maximum=256`、`complete=300`、`flow_rejected=0`、44次淘汰、最终256条流且两个drop字段为0；被淘汰端口从30000到30043，与时间顺序一致；
- 优化后官方SDK ARM64产物在LubanCat-2N得到`operations=300`、`inspected_slots=15364`、`average=51.21`、`maximum=256`、`complete=300`、`flow_rejected=0`、44次淘汰、最终256条流、后端接收303包和两个drop字段为0；
- 官方GCC 9对局部探测结果给出的保守未初始化告警已经通过显式零初始化消除；本地重点测试和全部17项CTest、官方SDK Release构建、`git diff --check`及板端运行均通过。

旧实现每次满表新流执行“256槽满表探测、256槽独立淘汰扫描、256槽插入重试”。44次淘汰中，已报告的重试扫描为`44 * 256 = 11264`次槽位检查，未进入探测统计的独立淘汰扫描也是11264次。新实现将三次整表扫描收敛为一次，最多省去22528次槽位访问；`operations`从344降到300直接证明重试已经消除。两轮测试源端口和哈希分布不同，因此不能用25055和12416计算严格的CPU提升百分比；若需要性能结论，应使用完全相同的五元组序列和构建产物做成对测试。

### 18.8 应用层与DPI

推荐顺序：

1. TCP连接状态基本跟踪（已完成第一版）；
2. 同一五元组重新建连语义与TCP字节流重组；
3. DNS解析，先UDP再考虑TCP；
4. HTTP/1.x请求行和头部；
5. TLS ClientHello元数据，例如SNI和版本；
6. 可配置规则和异常检测；
7. 特征导出和机器学习。

没有TCP重组时，不能可靠地假设一个应用层消息完整存在于一个TCP包中。

第一版状态跟踪已经完成：每条TCP流拥有独立状态，支持完整握手、中途捕获、FIN关闭和RST，并通过稳定名称输出到终端与CSV。下一阶段进入重组前，应先定义序列号比较、乱序和重叠段、重复数据、每流内存上限、超时清理，以及`closed`/`reset`后相同五元组的新SYN如何创建新生命周期。状态跟踪解决“连接处于什么阶段”，字节流重组解决“按什么顺序向应用层交付哪些字节”，两者不能混为一谈。

### 18.9 异常检测

先实现可解释规则，再做机器学习：

- 单源短时间大量目标端口：端口扫描；
- SYN多、完成握手少：SYN异常；
- DNS请求速率或域名长度异常；
- 单流字节/包速率异常；
- 大量失败连接；
- 流持续时间和方向比例异常。

规则输出应使用稳定事件模型，后续Qt和云平台都消费同一数据，不应让检测逻辑直接依赖GUI。

### 18.10 可视化

当前建议：

- 先做Qt上位机，展示实时流列表、协议分布、包/字节速率和告警；
- 核心分析程序保持C模块，Qt可以使用C++并通过C API、进程间通信或稳定文件/消息格式连接；
- 云平台作为后续扩展，用于远程设备、长期历史和多节点汇总；
- 不需要现在同时实现完整Qt和云平台。

### 18.11 非root systemd服务化（手工启停与开机自启完成）

当前已经完成：

- `CMakeLists.txt`通过`GNUInstallDirs`定义可移植的运行文件安装位置，并安装`netflow_analyzer_cli`目标；
- `packaging/systemd/netflow-analyzer.service`使用专用`netflow-analyzer`用户和组运行，不以root身份长期执行分析器；
- 服务只保留抓取原始网络数据所需的`CAP_NET_RAW`环境能力和能力边界，并启用`NoNewPrivileges`、只读系统目录、私有临时目录及内核接口防护；
- `/etc/default/netflow-analyzer`模板提供接口、BPF和满表策略参数，第一版默认只抓ICMP，避免启用服务后立即采集全部流量；
- systemd使用`SIGTERM`停止无上限实时循环，复用现有信号处理、libpcap中断、统计和正常清理路径；
- stdout在`main()`首次I/O前被显式设置为行缓冲，周期报告可以及时进入journal，而不依赖`stdbuf`；
- 本地CMake配置、构建、18项CTest、临时安装前缀和服务单元静态检查已经完成。静态检查只因本机尚未安装`/usr/local/bin/netflow-analyzer`而报告目标不存在，没有发现单元语法错误；
- 严格的普通文件重定向测试排除了终端和sudo伪终端影响，在进程退出前已经读到周期报告。
- 官方SDK为提交`740d5ab`生成的ARM64部署包SHA-256为`f916176829974d61ff853310638fd0b07c8570f715d97f67238aebfad259c55d`，开发电脑和目标板传输摘要一致；
- 目标板创建了无登录、无home目录的`netflow-analyzer`系统用户和同名组；程序、配置和单元由root拥有，ELF本身没有文件capability；
- systemd手工启动后，进程实际UID/GID为专用账户，有效、边界和ambient能力只包含`CAP_NET_RAW`对应的`0x2000`，`NoNewPrivs=1`；
- 静默5秒周期报告在服务运行中进入journal；虚拟机2次ping对应4个完整ICMP包、4次单槽探测、1条双向流和两个drop字段为0；
- `systemctl stop`发送SIGTERM后输出最终汇总并正常退出；`Flow 1`是4包聚合得到的第一条流记录，不是额外包；
- 板端`journalctl`不能解析`date --iso-8601=seconds`产生的带`T`和时区偏移格式，已改用`date '+%Y-%m-%d %H:%M:%S'`或`-b`查询；
- 完整Linux命令、权限模型、journal用法、故障定位与回滚见[`docs/systemd_deployment.md`](systemd_deployment.md)。
- `enable`在`multi-user.target.wants`下建立启动依赖；受控重启后boot ID变化，无人工`start`时服务已为`enabled`和`active`；
- 新boot一次启动成功，`Result=success`、`NRestarts=0`，新PID继续使用专用用户和唯一`CAP_NET_RAW`能力；
- `critical-chain`显示服务排在`network-online.target`之后，当前物理`eth0`成功打开；该顺序不等价于互联网必然可达，异常网络恢复仍待单独验证；
- 当前boot的静默报告和虚拟机再次产生的4个ICMP包均正常进入journal。

下一步使用目标板的`systemd-analyze security`审计实际生效的沙箱边界，再规划服务方式长稳。手工启停和开机自启已经完成，但在安全审计、异常恢复与长稳通过前不能宣称生产级部署完成。

## 19. ARM Linux开发板计划

用户已经收到野火LubanCat-2N，并开始首次上板环境准备。选择原因主要是：

- 与嵌入式Linux学习目标匹配；
- 网络接口条件更适合采集、管理口、入口/出口或旁路分析实验；
- 文档和社区资料相对友好；
- 相比只适合桌面演示的单网口板，更接近未来网络设备场景。

当前已确认：

- SD卡分区为`/dev/mmcblk1p1`，以`vfat`（FAT32）格式挂载在`/media/usb0`；
- 因VFAT不保存Linux原生UID、GID和权限位，直接对挂载内容执行`chown`返回`Operation not permitted`；
- 通过挂载时指定当前用户对应的`uid`、`gid`、`dmask`和`fmask`，已经解决项目目录无法创建的问题；
- 项目源码已经位于`/media/usb0/Workspace/netflow-analyzer`；
- 开发板使用CMake/CTest 3.16.3，不支持3.20才加入的`ctest --test-dir`，测试需要先进入构建目录；
- VFAT构建树中的ELF文件因执行权限被屏蔽而无法启动，现已把构建树改到`/home/cat/build/netflow-analyzer-debug`；
- 板上Debug原生构建和当前18项CTest已经通过；
- 板端Python为3.8.10；验收脚本将Python 3.9内置泛型注解改为`typing.List`和`typing.Tuple`后，`offline_flow_acceptance`与全量测试均通过；
- 板上Release原生构建和确定性6包PCAP端到端验收已经通过；
- 系统位于容量8GB的eMMC，安装系统后空间有限；当前源码仍在SD卡，构建树临时位于eMMC，不建议再把完整源码和多个构建树长期迁入eMMC；
- eMMC根分区实际为7.0GB ext4，已用4.7GB、可用2.1GB；Debug和Release构建目录分别只有2.6MB与456KB，目前无需清理；
- VMware NAT虚拟机`192.168.78.130`能够`ping`开发板`192.168.1.102`，开发板不能反向`ping`虚拟机；这是NAT与路由边界，不是程序故障；
- Release程序已在开发板物理网卡完成来自虚拟机的ICMP实时抓包和双向流聚合；开发板实际观察到NAT后对端`192.168.1.100`；
- 受控测试中应用处理4个Echo包，后端报告接收6包且两个drop字段为0；多出的2包未进入应用，现有汇总统计无法还原其具体内容；
- 同一确定性6包PCAP已经核对SHA-256，并在x86_64与ARM64原生构建上得到相同标准输出和退出状态；
- 官方Buildroot GCC 9.3、glibc 2.29 sysroot和隔离libpcap overlay已经完成ARM64 Release交叉构建；
- Ubuntu GCC 13、板端完整sysroot和GCC `-B`启动文件前缀也已经完成ARM64 Release交叉构建；
- 两种交叉产物均只要求`GLIBC_2.17`，并在板端通过`ldd`、`--help`和真实ICMP抓包；
- 完整交叉命令见`docs/cross_compilation.md`，详细排查过程见`docs/problem_log.md`第5.1至5.14节；
- 单流Release性能基线已完成：最高约9 Kpps、20万包、每包CPU约6.95微秒、最大RSS约1.64 MiB且零drop，详见`docs/performance_baseline.md`；
- 多流与长稳基线已完成：300流得到256个完整流和44个满载拒绝；128流、约9 Kpps、540万包持续10分钟时每包CPU约6.24微秒、RSS采样恒定且零drop，整机平均空闲90.97%，详见`docs/multiflow_longrun_baseline.md`。
- 流表探测统计版官方SDK交叉产物已完成板端复测：128流、20万包时平均和最大探测长度均为1；300流满载时11928次槽位检查中有11264次来自44个拒绝包的完整扫描，两个场景均零drop。
- 最旧流淘汰版官方SDK交叉产物只要求`GLIBC_2.17`，SHA-256为`02da53505b1f1230a9a04c60af8a618d85b734ed89a7c19176f5d59a7d4a3604`；板端300流得到300个完整包、44次淘汰、256条最终流和零drop。
- 单次满表扫描优化版官方SDK产物SHA-256为`c2dcc119ebbd27d321dbf041683d57a31ac0d22645fe16fea2ce08e873b37036`；板端300流得到300次探测操作、44次淘汰、256条最终流和零drop，证明优化在ARM64真实运行环境中成立。
- TCP状态版ARM64原生Debug构建的18项CTest全部通过；板端`lo`的HTTP/1.0实测处理12个`complete` TCP包，双向各6包并聚合为1条流，最终为`tcp_state=closed`，两个drop字段均为0；后端`received=24`与应用处理12包属于回环捕获的不同统计层级。
- 仓库已提交CMake安装规则、非root systemd单元和默认参数模板；板端已安装提交`740d5ab`对应的官方SDK产物，并通过专用用户、`CAP_NET_RAW`、journal实时日志、真实ICMP和SIGTERM手工启停验收。

不要宣称完整开发板验证已经结束。目前已经完成存储写权限、源码获取、原生Debug/Release构建、当前18项板端CTest、确定性PCAP跨平台输出对比、两种交叉编译、跨设备ICMP实时抓包、TCP完整关闭状态、单流/多流性能、满载边界、流表探测成本、整机软中断、10分钟长稳，以及非root systemd手工启停和开机自启；尚未完成双向直连网络验证、systemd安全审计/异常恢复和生产级数小时/数天浸泡测试。当前仓库提供环境检查脚本：

```bash
sh scripts/check_target_env.sh
sh scripts/check_target_env.sh --expect-arm --with-tests
```

首次上板建议：

1. 检查架构、内存、磁盘、网卡和系统版本；
2. 安装或确认GCC、CMake、pkg-config、libpcap开发包；
3. 先在板上原生Release构建；
4. 运行帮助和版本；
5. 拷贝同一个离线PCAP，在PC和板上对比输出；
6. 再验证实时网卡和权限；
7. 原生流程稳定后选择官方SDK或通用GCC加板端sysroot进行交叉构建，并检查ELF ABI；
8. 单流、多流、满载边界、整机CPU/软中断和10分钟长稳数据已经记录；更长浸泡测试按部署需要进行。

板上网络环境需要记录：

- 接口名与链路类型；
- 混杂模式；
- 网卡卸载功能，例如GRO、LRO和checksum offload；
- libpcap版本；
- 权限模型；
- 是否通过交换机镜像、网桥或网关路径获得流量。

## 20. 新会话的推荐开场指令

用户可以在新会话中发送：

```text
请先完整阅读：
/home/zcb/workspace/netflow-analyzer/docs/session_handoff.md

然后只读检查git status、最近提交和CTest基线，不要直接修改C源码。
周期PPS、Mbps、流表占用率、静默报告、五种应用处理结果、流表线性探测统计、实时`reject|evict-oldest`满表策略的单次扫描原位替换，以及TCP握手/关闭基本状态跟踪和终端/CSV输出已经完成。x86_64与LubanCat-2N ARM64原生Debug构建的18项CTest全部通过；板端`lo`真实HTTP/1.0连接以12个`complete`包得到1条`closed` TCP流，两个drop字段均为0。实时`--count`已改为可选，无上限模式的静默报告、ICMP处理和`SIGTERM`正常收尾已完成验收。LubanCat-2N已完成非root systemd手工启停与开机自启，专用用户只获得`CAP_NET_RAW`，新boot一次启动成功且journal实时日志、真实ICMP和SIGTERM收尾正确；下一步进行systemd安全审计、异常恢复和服务方式长稳。
仍然由我自己输入C代码，你负责完整说明、测试步骤、Git步骤以及测试通过后的日志文档更新。
```

接手者完成阅读后，应先用一小段话复述：

- 当前HEAD和测试状态；
- 当前实时命令；
- 为什么现有性能数据不支持接入线程流水线，以及下一步为何转向新的功能或部署问题；
- 本轮不会直接替用户修改C源码。

然后先检查TCP状态跟踪的源码、测试、文档和Git提交状态，再进入下一项技术选择；性能方法见`docs/performance_baseline.md`与`docs/multiflow_longrun_baseline.md`，交叉构建方法见`docs/cross_compilation.md`。

## 21. 本次交接结论

当前项目已经从“离线协议解析练习”发展为一条可实际运行的网络分析主链：

```text
离线PCAP或实时网卡
→ libpcap统一采集
→ 可选BPF过滤、非阻塞poll等待、信号中断与捕获后端统计
→ Ethernet/IPv4/TCP/UDP/ICMP解析
→ 双向五元组
→ FNV-1a开放寻址哈希流表
→ 数据包路径平均/最大线性探测统计
→ 事件时间调度、过期值副本与实时清理
→ 双向包数、字节数和时间统计
→ TCP握手、中途捕获、FIN关闭和RST状态
→ 周期PPS、Mbps、流表占用率和过期数量
→ 含tcp_state的终端流汇总或离线CSV
```

实时采集的第一组可控性能力已经完成：

```text
BPF过滤
→ 信号优雅退出
→ 抓包统计
→ 实时流过期输出
→ 静默期周期运行指标
```

应用处理结果分类、默认满载拒绝、显式最旧流淘汰、线性探测可观测性，以及单次满表扫描中的最旧候选选择和原位替换已经完成。TCP流现在按值保存独立旁路状态，能够区分完整握手、中途捕获、FIN关闭和RST，并在终端及CSV中使用统一名称；确定性3包握手PCAP与状态不变量测试已纳入18项CTest，x86_64与LubanCat-2N ARM64原生Debug构建均全部通过。板端`lo`真实HTTP/1.0连接进一步证明12个完整TCP包能正确聚合为1条双向流并最终进入`closed`，两个drop字段均为0。实时`--count`已改为可选，无上限模式已通过静默、ICMP和`SIGTERM`正常收尾验收，为systemd服务化提供了正确的持续运行语义。CMake安装规则、专用用户systemd单元、默认参数模板和stdout行缓冲已经完成；LubanCat-2N非root手工启停和开机自启已验证专用账户、仅`CAP_NET_RAW`、journal实时日志、真实ICMP和SIGTERM正常收尾，下一步是systemd安全审计、异常恢复和服务方式长稳。两种既有ARM64交叉构建和单次扫描优化版官方SDK产物均通过此前板端实际运行，Python验收脚本兼容板端Python 3.8.10。性能基线已经覆盖空闲、单流、多流、满载边界、探测成本、整机软中断和10分钟长稳；当前仍没有接入`labs/thread_pipeline`。
