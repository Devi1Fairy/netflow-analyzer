# 主项目问题记录

本文档记录正式主项目开发过程中实际遇到的环境、构建、运行和调试问题。`labs/`中的实验问题继续保存在各自实验目录内，避免热身实验和正式项目混淆。

## 1. 开发环境与构建配置

### 1.1 CMake能够编译，但VS Code找不到`analyzer/app.h`

现象：

源码使用公开头文件路径：

```c
#include "analyzer/app.h"
```

VS Code显示无法找到头文件，跳转和补全不能正常工作，但CMake配置中已经存在：

```cmake
target_include_directories(
    analyzer_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

排查过程：

1. 确认头文件真实存在于`include/analyzer/app.h`；
2. 确认`netflow_analyzer_cli`和测试目标都链接`analyzer_core`；
3. 使用CMake实际编译，`app.c`、`main.c`和`test_smoke.c`全部成功；
4. 检查`build/compile_commands.json`，确认真实编译命令包含：

```text
-I/home/zcb/workspace/netflow-analyzer/include
```

上述证据说明编译器能够找到头文件，问题只存在于编辑器的IntelliSense配置层。

根因：

- 工作区早期的CMake源码目录仍指向`labs/blocking_queue`，切换正式主项目后需要重新指向仓库根目录；
- C/C++扩展依赖`ms-vscode.cmake-tools`配置提供器，但该提供器没有正确应用根项目配置；
- `c_cpp_properties.json`仍配置为Clang和C17，而实际项目使用GCC和C11；
- C/C++扩展没有直接读取CMake生成的`compile_commands.json`。

解决办法：

1. 根项目导出编译数据库：

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

2. VS Code的CMake目录切换为正式主项目：

```jsonc
"cmake.sourceDirectory": "${workspaceFolder}",
"cmake.buildDirectory": "${workspaceFolder}/build"
```

3. C/C++扩展直接读取编译数据库：

```json
"compileCommands": "${workspaceFolder}/build/compile_commands.json"
```

4. IntelliSense配置和真实编译工具保持一致：

```json
"compilerPath": "/usr/bin/gcc",
"cStandard": "c11",
"intelliSenseMode": "linux-gcc-x64"
```

5. 删除`configurationProvider`配置，避免它覆盖直接编译数据库；随后执行：

```text
C/C++: Reset IntelliSense Database
Developer: Reload Window
```

验证结果：

- VS Code能够识别`#include "analyzer/app.h"`；
- 头文件跳转和代码补全恢复；
- CMake根项目保持零警告编译。

经验：

- 编辑器红线不一定表示编译器错误，应先用真实构建命令验证；
- `compile_commands.json`记录了每个源文件真正使用的编译器、宏、语言标准和头文件路径，是排查编辑器与构建配置不一致的重要证据；
- 不应该为了消除编辑器红线，把公开头文件改成`../include/...`等依赖源码位置的相对路径；
- 从实验子项目切换到正式根项目时，应同步检查CMake Tools、C/C++扩展、构建目录和调试目标。

## 2. 离线抓包与Ethernet解析

### 2.1 严格C11模式下libpcap头文件缺少BSD兼容类型

现象：

项目接入libpcap后，编译器在系统头文件中报告`u_int`、`u_short`、`u_char`等类型未定义。报错看起来来自libpcap，并不是业务代码中的语法错误。

根因：

项目使用严格C11模式，并关闭了C语言扩展。glibc会因此隐藏一部分BSD兼容类型，而当前系统中的libpcap头文件仍会使用这些类型。

解决办法：

- 在包含任何系统头文件之前定义`_DEFAULT_SOURCE`，使glibc公开libpcap所需的兼容类型；
- 单元测试中还需要使用`mkstemp`，因此同时定义`_POSIX_C_SOURCE 200809L`，公开对应的POSIX接口；
- 保留严格C11和编译警告，不通过关闭警告来掩盖问题。

验证结果：

- 抓包模块和单元测试可以正常编译；
- 编译过程保持零警告；
- 原有测试与新增抓包测试全部通过。

经验：

- 报错位置在第三方头文件中，不代表第三方库本身有问题，还要检查项目选择的语言标准和特性宏；
- 特性测试宏必须在系统头文件之前定义，否则可能不会生效；
- 解决这类问题时，应明确缺失接口属于哪一种标准扩展，避免随意添加宏。

### 2.2 Ethernet预览计数器未递增，预览上限失效

现象：

程序设置最多显示5个数据包，但实际输出了6个；每一行都显示`Packet 0`，最后又显示`Displayed packets: 0`。

根因：

将数据包输出整理到辅助函数时，遗漏了成功读取数据包后的计数语句。循环条件一直看到计数器为0，所以显示上限失效，序号和最终统计也同时错误。

解决办法：

只在`capture_read_next()`明确返回“成功读取一个数据包”后执行：

```c
displayed_packet_count += 1U;
```

随后再将更新后的计数作为数据包序号传给预览函数。EOF和读取错误都不能增加计数。

验证结果：

- 最多只显示5个数据包；
- 数据包序号从1连续增加到5；
- 最终输出为`Displayed packets: 5`。

经验：

- 一个遗漏的状态更新可能同时破坏循环终止条件、显示序号和最终统计；
- 模块单元测试全部通过，并不代表命令行主流程正确。`v0.1.0`已经增加固定PCAP输入的CLI验收测试，覆盖输出数量、循环退出条件和流量聚合结果；
- 重构输出代码时，除了移动打印逻辑，还要检查与该逻辑相邻的状态更新是否被保留。

### 2.3 回环接口PCAP中的源MAC和目标MAC均为零

现象：

读取在回环接口上生成的PCAP文件时，程序输出的源MAC和目标MAC均为`00:00:00:00:00:00`，但EtherType能够正确解析为`0x0800`。

排查过程：

使用`tcpdump -e -nn -r <pcap文件>`读取同一个文件，比较链路层字段；必要时也可以使用Wireshark或tshark交叉验证。其他工具看到相同字节时，说明解析程序没有凭空把正常MAC地址改成零。

结论：

回环通信不经过物理以太网网卡，本来就没有真实的源MAC和目标MAC。本次测试PCAP中的Ethernet格式数据使用了全零地址，因此该输出是输入数据的真实反映，不是Ethernet解析器故障。

`0x0800`不是项目自定义值，而是标准EtherType，表示Ethernet负载通常为IPv4数据包。

经验：

- 字段值看起来异常时，应先区分“解析错误”和“输入数据本身如此”；
- 同一PCAP可以用tcpdump或Wireshark作为交叉验证工具；
- 回环流量适合验证IP、TCP和应用层逻辑，但不适合观察真实网卡MAC地址。

### 2.4 运行时使用libpcap，而不解析tcpdump文本输出

问题：

tcpdump已经能够读取PCAP并显示链路层信息，为什么项目还要直接使用libpcap？

技术判断：

tcpdump适合人工抓包和快速排查；libpcap适合让程序直接获得原始数据包及其元数据。tcpdump本身也建立在libpcap之上。若业务程序通过子进程启动tcpdump，再解析它的文本输出，会额外引入进程管理、管道通信、文本格式变化、错误传递和测试困难等问题。

项目选择：

- 正式运行路径使用libpcap读取结构化数据；
- tcpdump用于制作测试PCAP、查看原始流量和交叉验证解析结果；
- Wireshark用于需要图形化、逐字段检查时的人工分析。

这样可以直接把原始字节传给后续Ethernet、IPv4和传输层解析模块，也更便于接入线程流水线、统计分析、异常检测和图形化显示。

经验：

- 工具能够显示结果，不等于它适合作为程序内部API；
- 一次性的人工排障优先使用tcpdump，长期运行且需要继续处理数据时优先使用libpcap；
- 面试中可以说明二者是不同层次的工具关系，而不是互相替代的竞争关系。

## 3. 离线流量聚合与端到端验收

### 3.1 预览上限不能作为PCAP分析循环的终止条件

现象：

加入双向流表后，原来的循环仍以“最多显示5个数据包”为终止条件。如果PCAP中存在6个或更多数据包，程序只会解析和聚合前5个包，最终流统计不完整。

根因：

逐包预览和完整分析使用了同一个计数器。预览数量是用户界面的输出限制，分析数量则由PCAP文件中的实际数据包数量决定，二者职责不同。

解决办法：

- 主循环持续读取，直到libpcap返回文件结束；
- 使用`total_packet_count`统计所有读取成功的数据包；
- 使用`previewed_packet_count`单独限制逐包输出；
- 每个数据包都执行协议解析和流表更新，只有前5个包打印详细信息。

验证结果：

端到端测试生成包含6个ICMP数据包的固定PCAP，并验证：

- `Total packets`为6；
- `Previewed packets`为5；
- 双向流表仍统计全部6个包；
- 两个方向各有3个包，字节数和首末时间戳与输入一致。

经验：

- 输出限制不能改变核心分析范围；
- 含义不同的状态不应为了省变量而共用一个计数器；
- 单元测试适合验证模块，CLI端到端测试适合发现模块连接后的行为偏差。

### 3.2 Python验收脚本中的`with`语句缩进错误

现象：

CTest启动离线流量验收测试后，Python在执行测试逻辑之前退出：

```text
IndentationError: expected an indented block after with statement
```

根因：

写PCAP文件的`with pcap_path.open("wb")`语句被错误地放进了数据包生成循环，并且其代码块没有继续缩进。Python使用缩进划分语句块，因此该错误属于语法错误，不是C程序或PCAP解析失败。

解决办法：

- 第一个循环只负责在内存中构造测试数据包；
- 循环结束后打开一次PCAP文件；
- 在`with`代码块内写入全局文件头，再循环写入各个数据包记录。

验证结果：

- Python脚本能够正常生成临时PCAP；
- `offline_flow_acceptance`通过；
- CTest中的14项测试全部通过。

经验：

- Python中的缩进是语法的一部分，调整循环或上下文管理器后需要检查整个代码块层级；
- 测试脚本自身也可能出错，应先区分“测试没有运行”和“被测程序运行后失败”；
- 端到端测试使用临时目录和确定性输入，可以避免依赖个人电脑上已有的PCAP文件。

### 3.3 流表遍历测试通过，但指针断言没有实际验证目标

现象：

哈希流表的14项测试全部通过，但代码评审发现遍历测试先调用`flow_table_get`覆盖了`record`，随后才执行：

```c
inserted_record = record;
TEST_CHECK(record == inserted_record);
```

这个比较本质上是把刚得到的值赋给另一个变量后再与自身比较，因此无论`flow_table_get`返回的是不是插入时的记录，断言都会成立。

根因：

测试用例没有在被测操作之前保存独立的期望值。测试能够编译、运行并返回0，只能说明断言表达式为真，不能自动证明断言检查了原需求。

解决办法：

- 在`flow_table_process_packet`成功后立即保存其返回的记录地址；
- 再调用`flow_table_get(table, 0, ...)`；
- 比较查询结果与先前保存的独立地址；
- 调用越界查询并返回`ERANGE`后，再检查输出指针仍保持原值，从而同时覆盖失败不修改输出参数的约定。

验证结果：

- 遍历接口返回插入时的同一条内部记录；
- 越界查询不修改输出参数；
- 哈希冲突、删除槽复用、离线验收等14项CTest测试全部通过。

经验：

- “测试通过”和“测试有效”不是同一件事；
- 期望值应在被测操作之前准备，不能由被测操作的实际结果反向生成；
- 代码评审不仅要检查业务实现，也要检查测试是否可能形成恒真断言；
- 这类问题适合在面试中说明：自动化测试仍需要开发人员审查测试目标、输入和断言之间的对应关系。

### 3.4 CSV验收路径放错函数，触发Python局部变量未定义

现象：

扩展离线验收脚本后，CTest在C程序启动前失败：

```text
NameError: name 'temporary_directory' is not defined
```

回溯显示错误发生在`write_test_pcap`内部创建CSV路径的位置。

根因：

`temporary_directory`是`run_acceptance_test`中`with tempfile.TemporaryDirectory(...) as temporary_directory`创建的局部变量，只在该函数作用域内可见。CSV路径代码被误放进`write_test_pcap`，而该函数只接收`pcap_path`，不能访问调用者的局部变量。

解决办法：

- `write_test_pcap`继续只负责根据传入路径生成PCAP；
- 在`run_acceptance_test`的临时目录代码块中同时创建`pcap_path`和`csv_path`；
- 两个路径分别传给PCAP生成逻辑和被测命令行程序；
- 保持路径变量与临时目录生命周期一致。

验证结果：

- Python脚本能够生成临时PCAP并向程序传入CSV路径；
- CSV文件包含预期表头和一条ICMP双向流记录；
- CSV相关3项测试和全部15项CTest测试通过。

经验：

- Python函数不能直接访问调用者的局部变量；
- 回溯最底部附近通常能找到最先触发异常的源码位置；
- 临时文件路径应在拥有临时目录生命周期的函数中统一创建；
- 函数职责越单一，变量应该属于哪个作用域越容易判断。
