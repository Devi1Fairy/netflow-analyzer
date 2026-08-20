# Netflow Analyzer

基于 Linux C11 的网络流量分析与异常检测学习项目。

项目将从离线 PCAP 文件读取开始，逐步实现 Ethernet、IPv4、TCP、UDP、ICMP 协议解析、五元组流量聚合、应用层协议识别和基础异常检测，并在核心功能稳定后扩展到实时抓包、多线程流水线、机器学习分析和 ARM Linux 部署。

## 当前版本

当前版本：`v0.0.1` 开发阶段。

目前已经建立可构建、可运行和可测试的主项目骨架，包括：

- 使用 CMake 组织多文件 C11 工程；
- 将核心应用逻辑构建为静态库；
- 提供命令行帮助和版本输出；
- 建立应用上下文的初始化、运行、停止和清理流程；
- 使用 CTest 运行 C 语言冒烟测试；
- 启用 `-Wall`、`-Wextra` 和 `-Wpedantic` 编译警告；
- 导出 `compile_commands.json`，供 VS Code 和静态分析工具使用。

当前版本还没有读取或解析真实网络数据包。协议解析功能会在后续阶段逐步加入。

## 项目目标

项目计划形成以下处理链路：

```text
PCAP 文件 / 实时网卡
          │
          ▼
      libpcap 采集
          │
          ▼
Ethernet / IPv4 / TCP / UDP / ICMP
          │
          ▼
     五元组双向流表
          │
          ├──────────────┐
          ▼              ▼
    应用协议解析       规则异常检测
          │              │
          └──────┬───────┘
                 ▼
        CSV / JSON / 日志 / 告警
```

核心程序首先使用 C11 完成。Python 主要用于自动化测试、数据分析和后期机器学习；图形化显示计划在核心分析链路稳定后使用 C++/Qt 实现。

## 当前目录结构

```text
netflow-analyzer/
├── CMakeLists.txt               # 根项目构建配置
├── README.md                    # 项目介绍与使用说明
├── include/analyzer/
│   └── app.h                    # 应用层公开接口
├── src/
│   ├── main.c                   # 程序入口和退出状态转换
│   └── app/
│       └── app.c                # 应用生命周期和顶层调度
├── tests/unit/
│   └── test_smoke.c             # 阶段0冒烟测试
├── labs/                        # 正式项目开始前的热身实验
│   ├── blocking_queue/          # 有界阻塞队列
│   ├── tcp_framing/             # TCP消息分帧
│   └── thread_pipeline/         # 多线程流水线
└── docs/
    └── problem_log.md           # 正式项目实际问题记录
```

`build/` 和 `build-release/` 是 CMake 生成的构建目录，不属于源代码，不应提交到 Git。

## 环境要求

当前阶段需要：

- Linux；
- 支持 C11 的 GCC 或 Clang；
- CMake 3.16 或更高版本；
- Make 或 Ninja 等 CMake 后端构建工具。

在 Ubuntu 中可以安装基础构建工具：

```bash
# 刷新软件包索引，使 apt 获得当前可用的软件版本信息。
sudo apt update

# 安装 GCC、标准构建工具、CMake 和 Ninja。
sudo apt install build-essential cmake ninja-build
```

后续进入 PCAP 读取阶段时还会使用 `libpcap-dev`，当前阶段暂不依赖它。

## Debug 构建

在项目根目录执行：

```bash
# -S . 表示源码目录是当前目录。
# -B build 表示把所有生成文件放入 build，避免污染源码目录。
# Debug 会保留调试信息，适合日常开发和断点调试。
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# 根据 build 中已经生成的构建规则编译所有目标。
cmake --build build
```

主要构建产物：

```text
build/bin/netflow-analyzer       命令行主程序
build/bin/analyzer_smoke_tests   冒烟测试程序
build/lib/libanalyzer_core.a     核心静态库
```

## Release 构建

Debug 和 Release 使用不同目录，避免两种配置的目标文件相互覆盖：

```bash
# Release 构建通常启用编译优化，适合发布和性能测试。
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release

# 编译 Release 目标。
cmake --build build-release
```

Release 主程序位于：

```text
build-release/bin/netflow-analyzer
```

## 运行程序

无参数运行时显示帮助：

```bash
./build/bin/netflow-analyzer
```

显式显示帮助：

```bash
./build/bin/netflow-analyzer --help
```

显示版本：

```bash
./build/bin/netflow-analyzer --version
```

传入未知参数时，程序会向标准错误输出提示，并返回非零退出状态：

```bash
./build/bin/netflow-analyzer --unknown
```

当前支持的参数：

| 参数 | 作用 |
|---|---|
| `-h`、`--help` | 显示帮助信息 |
| `-V`、`--version` | 显示程序版本 |

## 运行测试

先完成构建，再执行：

```bash
# --test-dir 指定包含 CTest 配置的构建目录。
# --output-on-failure 让失败测试显示详细输出，便于定位问题。
ctest --test-dir build --output-on-failure
```

也可以验证 Release 构建：

```bash
ctest --test-dir build-release --output-on-failure
```

当前冒烟测试覆盖：

- 应用上下文初始化、停止和清理；
- 无参数时选择帮助命令；
- 版本参数解析和版本号；
- 未知参数拒绝。

## 模块职责

当前调用关系：

```text
main.c
  └── app.c
        ├── 初始化应用上下文
        ├── 解析命令行参数
        ├── 调度当前命令
        ├── 接收停止请求
        └── 清理应用资源
```

`main.c` 只负责程序入口、顶层调用顺序和退出状态转换。`app.c` 负责组织整个应用的生命周期，但不会承载所有具体功能。

后续协议解析、采集、流表、检测和输出会分别放入独立模块，`app.c` 只负责按照正确顺序调用它们。

## 开发路线

主要阶段如下：

1. 建立最小工程骨架；
2. 实现安全字节读取工具；
3. 使用 libpcap 离线读取 PCAP；
4. 建立统一数据包结果对象；
5. 解析 Ethernet 和 IPv4；
6. 解析 TCP、UDP 和 ICMP；
7. 实现五元组双向流表；
8. 输出 CSV/JSON 和运行统计；
9. 解析 DNS、MQTT 或 Modbus TCP；
10. 实现端口扫描和高频连接等基础异常规则；
11. 增加实时抓包；
12. 接入多线程处理流水线；
13. 完善配置、日志、自动化测试和质量检查；
14. 扩展机器学习、Qt 图形化显示和 ARM Linux 部署。

## 开发原则

- 先检查数据长度，再读取任何协议字段；
- 先完成离线、单线程的正确版本，再加入实时抓包和多线程；
- 每个动态资源都必须有明确的所有者和释放位置；
- 每个功能至少包含正常输入和边界输入测试；
- 提交前保持零新增编译警告，并运行相关自动化测试；
- 实际遇到的问题和解决过程记录在 `docs/` 或对应实验目录中。

