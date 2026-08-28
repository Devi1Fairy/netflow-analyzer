# Netflow Analyzer会话交接文档

最后更新：2026-08-28（Asia/Shanghai）

本文用于把当前项目状态、学习背景、协作方式、代码架构、测试、Git历史、已知边界和下一步计划完整交接给新的Codex会话。接手者应先完整阅读本文，再执行只读检查，不要根据标题直接开始大范围修改。

## 1. 新会话接手顺序

新会话建议先执行：

```bash
cd /home/zcb/workspace/netflow-analyzer

git status --short
git branch --show-current
git log -5 --oneline --decorate

cmake --build build
ctest --test-dir build --output-on-failure
```

预期基线：

- 当前分支：`main`；
- 远程仓库：`git@github.com:Devi1Fairy/netflow-analyzer.git`；
- 当前已提交基线：`3c54aba feat(cli): stop live capture gracefully on signals`；
- 当前功能在该基线上继续加入`capture_get_statistics()`和实时统计输出；预计以`feat(capture): report live capture statistics`提交，准确哈希以实际`git log`为准；
- 当前正式版本宏为`0.2.0`；
- 已有标签：`v0.0.1`、`v0.1.0`、`v0.2.0`；
- 完成本轮提交和推送后，预期`main`、`origin/main`一致且工作区干净；
- Debug构建目录：`/home/zcb/workspace/netflow-analyzer/build`；
- 主程序：`build/bin/netflow-analyzer`；
- 当前15项CTest应全部通过。

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
ctest --test-dir build --output-on-failure
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
- Python 3，仅用于端到端验收；
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

### 4.4 VS Code与CMake历史问题

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
（本轮提交后）feat(capture): report live capture statistics
3c54aba feat(cli): stop live capture gracefully on signals
768985f feat(app): propagate stop requests to capture loop
3a3fb3e feat(capture): support breaking active reads
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

`v0.1.0`是第一条完整离线分析链的发布标签。`v0.2.0`已经发布哈希流表、过期底层接口、CSV、实时抓包和BPF过滤。信号优雅退出与libpcap运行统计属于`Unreleased`开发进度，当前不需要立即更换版本标签。

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
│   ├── flow_table.h
│   └── flow_export.h
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
│   │   └── flow_table.c
│   └── output/flow_export.c
├── tests/
│   ├── unit/
│   └── integration/test_offline_flow.py
├── scripts/check_target_env.sh
├── docs/
│   ├── problem_log.md
│   ├── technical_decisions.md
│   └── session_handoff.md
└── labs/
    ├── blocking_queue/
    ├── tcp_framing/
    └── thread_pipeline/
```

模块职责：

| 模块 | 职责 |
|---|---|
| `main.c` | 初始化上下文、解析参数、运行应用、统一清理，把错误码转换为进程退出码 |
| `app.c` | 应用编排；连接采集、协议解析、流表和输出，不承担具体协议字段解析 |
| `byte_reader` | 使用边界检查游标读取、跳过和切分原始二进制字节 |
| `capture` | 封装libpcap，统一离线文件、实时网卡、BPF、中断和运行统计接口，隐藏`pcap_t`、`struct pcap_stat`与`DLT_*` |
| `packet_info` | 保存一条数据包的捕获元数据、协议字段和解析状态 |
| `ethernet` | 解析Ethernet II头、MAC地址和EtherType |
| `ipv4` | 解析IPv4头、长度、地址、TTL、协议号和分片信息 |
| `tcp` | 解析TCP头、端口、序列号、确认号、标志和负载视图 |
| `udp` | 解析UDP头、端口、长度和负载视图 |
| `icmp` | 解析ICMP类型、代码、echo字段和负载视图 |
| `ipv4_dispatch` | 按IPv4协议号分发到TCP、UDP或ICMP解析器 |
| `flow_key` | 生成规范化双向五元组，判断相等，计算FNV-1a 64位哈希 |
| `flow_record` | 保存一个双向流的两个方向统计、首末时间并更新记录 |
| `flow_table` | 开放寻址哈希表、线性探测、删除标记、查找、遍历和过期删除 |
| `flow_export` | 将流记录写成固定字段顺序的CSV表头和记录 |

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
    ↓
终端逐包预览（最多5包）
    ↓
处理完整文件后输出流汇总
    ↓ 可选
CSV导出
```

实时模式：

```text
CLI --interface NAME --count N [--filter EXPRESSION]
    ↓
严格解析N为大于0的size_t
    ↓
capture_open_live
    ↓
检查链路类型必须为Ethernet
    ↓
可选capture_set_filter
    ↓
capture_next_packet
    ↓ 超时EAGAIN
继续等待，不增加包计数
    ↓ 收到包
与离线模式复用同一解析和流聚合链
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

### 10.4 实时有限包数分析

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
- 实时模式必须提供`--count`；
- 当前实时模式不能与`--csv`组合。

达到数量上限或收到`SIGINT`/`SIGTERM`后，实时模式在关闭capture句柄前查询`pcap_stats()`，输出捕获后端接收、抓包缓冲区丢弃和接口丢弃统计。统计查询失败只降级显示不可用，不丢弃已经完成的流分析。

打开实时接口通常需要root权限或适当Linux capability。当前手工验收使用`sudo`运行主程序，但不要使用`sudo cmake`或`sudo cmake --build`，避免构建目录出现root所有权文件。

## 11. 最新实时抓包验收结果

系统接口：

```text
lo     Linux回环接口
ens33  VMware呈现给Ubuntu虚拟机的Ethernet接口
```

`lo`捕获本机经`127.0.0.1`或`::1`进行的通信，包不会离开系统。`ens33`承载虚拟机与宿主机、局域网或互联网的通信，具体范围取决于VMware NAT、桥接或Host-only模式。

最新统计验收执行：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"
```

另一个终端执行`ping -c 2 127.0.0.1`，应用取得2个Echo Request和2个Echo Reply，共4个逻辑ICMP包。终端同时输出`Total packets: 4`和`Capture received packets: 8`。

重要结论：

- 实时抓包看到的是接口上的全部匹配流量，不是只看到用户为了测试主动产生的流量；
- BPF已经能排除无关的VS Code回环TCP流量；
- `--count`限制包数，不限制等待时间；接口没有足够流量时程序会继续等待；
- Ctrl+C和SIGTERM已经能中断读取并沿正常路径输出已有统计；
- Linux回环包会出现outgoing和incoming捕获事件，libpcap向应用交付前会抑制重复副本，因此捕获后端的8和应用处理的4处于不同统计层级；
- 不能用`Capture received packets - Total packets`推导丢包，应查看专门的drop字段并保留平台可用性限制。

## 12. 当前测试体系

当前CTest共15项，最近一次全量执行全部通过，总耗时约0.07秒：

| 编号 | CTest名称 | 主要覆盖 |
|---:|---|---|
| 1 | `analyzer_smoke_tests` | 上下文生命周期、帮助、版本、离线/实时CLI参数及错误组合 |
| 2 | `byte_reader_tests` | 字节读取、跳过、切片、边界和失败不修改输出 |
| 3 | `capture_tests` | PCAP打开、链路类型、逐包读取、BPF、中断、实时统计参数、离线不支持语义和关闭 |
| 4 | `packet_info_tests` | 统一结果对象初始化、时间戳和错误状态 |
| 5 | `ethernet_tests` | Ethernet II字段、负载、截断和格式化 |
| 6 | `ipv4_tests` | IPv4长度、IHL、地址、分片、截断和畸形输入 |
| 7 | `tcp_tests` | TCP头字段、标志、负载和边界 |
| 8 | `udp_tests` | UDP长度、端口、负载和非法组合 |
| 9 | `icmp_tests` | ICMP通用字段、echo字段和负载 |
| 10 | `ipv4_dispatch_tests` | 协议号分发、未知协议和分片处理 |
| 11 | `flow_key_tests` | 双向规范化、方向、字段比较和稳定FNV-1a哈希 |
| 12 | `flow_record_tests` | 两个方向统计、首末时间、错误和溢出保护 |
| 13 | `flow_table_tests` | 哈希冲突、线性探测、回绕、删除标记、复用、遍历和过期 |
| 14 | `offline_flow_acceptance` | Python生成确定性PCAP，运行真实CLI，检查全文件聚合、预览和CSV |
| 15 | `flow_export_tests` | CSV表头、记录字段顺序、格式化和无效参数 |

只运行重点测试示例：

```bash
ctest \
    --test-dir build \
    -R '^(analyzer_smoke_tests|capture_tests)$' \
    --output-on-failure
```

只运行离线端到端验收：

```bash
ctest \
    --test-dir build \
    -R '^offline_flow_acceptance$' \
    --output-on-failure
```

为什么真实实时抓包没有自动化验收：

- CI或开发机不一定有同名网卡；
- 抓包可能需要root或capability；
- 实时流量时序不确定；
- 不能假设外网可用；
- 自动测试不应修改系统网卡权限。

因此当前对实时功能采用三层验证：参数单元测试、采集接口的不依赖权限错误测试、人工`lo`验收。BPF使用离线确定性PCAP验证过滤语义；信号唤醒和`pcap_stats()`成功路径使用真实`lo`手工验收。

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

数据库目前不是必需项。只有出现历史查询、持久告警、用户配置、跨重启状态等明确需求时，再评估SQLite或外部数据库。

## 16. 当前已知边界和风险

### 16.1 实时抓包

- 已支持BPF、SIGINT/SIGTERM优雅退出和`pcap_stats()`累计统计；
- `--count`只限制成功捕获的包数，不限制等待时间；
- libpcap读取超时在不同平台和捕获后端上的行为可能不同，不能把它当作严格定时器；
- `pcap_stats()`字段语义和可用性依赖平台，`ps_recv`不等于应用完成处理的包数，`ps_ifdrop == 0`也可能表示指标不可用；
- 当前只有应用取得的总包数，没有按协议解析成功、截断、畸形和不支持状态分类的运行计数；
- 当前混杂模式固定为false，没有CLI选项；
- 实时CSV尚未开放；
- `any`接口在Linux通常是cooked capture链路类型，当前只支持Ethernet，可能返回`ENOTSUP`；
- `lo`在当前环境表现为Ethernet链路并能解析，但其他平台不应无条件假设相同DLT。

### 16.2 协议

- 只支持Ethernet II和IPv4；
- 不支持VLAN、IPv6、PPPoE、Linux cooked capture和隧道；
- 不做IPv4分片重组；
- 不做TCP乱序重组和字节流重组；
- 不解析TCP选项；
- 不验证IPv4/TCP/UDP/ICMP校验和；
- 不解析DNS、HTTP、TLS等应用层协议；
- 未实现DPI内容识别和规则异常检测。

### 16.3 流表

- 固定256槽；
- 达到容量返回`ENOSPC`；
- 已有过期API，但主循环未定期调用；
- 过期记录删除前没有输出到下游；
- 没有负载因子、重建或动态扩容；
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

后续可观测性仍包括：协议解析结果分类、流表拒绝、过期/驱逐、PPS、Mbps、CPU和RSS。

### 18.3 实时流过期

已有`flow_table_expire_before()`，但还没有接入。需要决定：

- 流空闲超时；
- 扫描周期；
- 过期记录删除前如何输出；
- CSV、Qt或下游队列的所有权；
- 时间戳倒退如何处理；
- 固定表容量不足时的策略。

不能直接调用过期删除然后丢弃结果，否则用户看不到已经结束的流。

### 18.4 性能测量后再接线程流水线

候选正式结构：

```text
capture线程
    ↓ packet队列
worker线程（解析与流聚合）
    ↓ result/event队列
output线程（CSV/Qt/告警）
```

但是原始`packet.data`由libpcap拥有，只到下一次读取有效。capture线程如果把包交给其他线程，必须复制`captured_length`字节到拥有明确生命周期的堆对象，不能直接把libpcap内部指针推入队列。这是正式接入线程时最重要的所有权变化之一。

### 18.5 应用层与DPI

推荐顺序：

1. TCP连接状态基本跟踪；
2. TCP字节流重组；
3. DNS解析，先UDP再考虑TCP；
4. HTTP/1.x请求行和头部；
5. TLS ClientHello元数据，例如SNI和版本；
6. 可配置规则和异常检测；
7. 特征导出和机器学习。

没有TCP重组时，不能可靠地假设一个应用层消息完整存在于一个TCP包中。

### 18.6 异常检测

先实现可解释规则，再做机器学习：

- 单源短时间大量目标端口：端口扫描；
- SYN多、完成握手少：SYN异常；
- DNS请求速率或域名长度异常；
- 单流字节/包速率异常；
- 大量失败连接；
- 流持续时间和方向比例异常。

规则输出应使用稳定事件模型，后续Qt和云平台都消费同一数据，不应让检测逻辑直接依赖GUI。

### 18.7 可视化

当前建议：

- 先做Qt上位机，展示实时流列表、协议分布、包/字节速率和告警；
- 核心分析程序保持C模块，Qt可以使用C++并通过C API、进程间通信或稳定文件/消息格式连接；
- 云平台作为后续扩展，用于远程设备、长期历史和多节点汇总；
- 不需要现在同时实现完整Qt和云平台。

## 19. ARM Linux开发板计划

用户已经购买野火LubanCat-2N，硬件尚未开始正式上板验证。选择原因主要是：

- 与嵌入式Linux学习目标匹配；
- 网络接口条件更适合采集、管理口、入口/出口或旁路分析实验；
- 文档和社区资料相对友好；
- 相比只适合桌面演示的单网口板，更接近未来网络设备场景。

不要宣称已经在开发板部署成功。当前仓库只有环境检查脚本：

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
7. 记录CPU、内存、包速率和丢包；
8. 原生流程稳定后再评估交叉编译。

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
继续按照交接文档第18.3节，一步一步教我把现有流过期接口接入实时主流程。
仍然由我自己输入C代码，你负责完整说明、测试步骤、Git步骤以及测试通过后的日志文档更新。
```

接手者完成阅读后，应先用一小段话复述：

- 当前HEAD和测试状态；
- 当前实时命令；
- 为什么下一步是实时流过期与过期结果输出；
- 本轮不会直接替用户修改C源码。

然后再开始第一个实时流过期设计步骤。

## 21. 本次交接结论

当前项目已经从“离线协议解析练习”发展为一条可实际运行的网络分析主链：

```text
离线PCAP或实时网卡
→ libpcap统一采集
→ 可选BPF过滤、信号中断与捕获后端统计
→ Ethernet/IPv4/TCP/UDP/ICMP解析
→ 双向五元组
→ FNV-1a开放寻址哈希流表
→ 双向包数、字节数和时间统计
→ 终端流汇总或离线CSV
```

实时采集的第一组可控性能力已经完成：

```text
BPF过滤
→ 信号优雅退出
→ 抓包统计
```

当前下一步是把已有`flow_table_expire_before()`接入实时主循环，并先解决“过期记录删除前如何输出、扫描周期和时间语义”这三个问题。完成实时流过期输出后，再增加周期速率和容量指标，测量单线程瓶颈，并决定是否把`labs/thread_pipeline`接入正式程序。
