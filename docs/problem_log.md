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

## 4. 实时抓包接入

### 4.1 新增网卡参数时漏初始化临时指针

现象：

为CLI增加`--interface`参数后，原有离线命令解析测试失败：

```text
[FAIL] tests/unit/test_smoke.c:152:
app_parse_arguments(&context, 3, arguments) == 0
```

失败用例传入的是合法参数`--read sample.pcap`，尚未进入实时抓包、网卡权限或libpcap读取阶段。

根因：

参数解析函数新增了局部指针`parsed_interface_name`，但只初始化了`parsed_capture_path`和`parsed_csv_output_path`。未初始化的自动存储期指针含有不确定值，不能把它当作可靠的`NULL`状态使用。

离线参数被正确保存到`parsed_capture_path`后，未初始化的`parsed_interface_name`可能碰巧表现为非`NULL`，从而触发“离线文件和实时网卡不能同时指定”的冲突检查并错误返回`EINVAL`。

解决办法：

- 在进入参数遍历前，把三个临时解析指针全部初始化为`NULL`；
- 继续使用临时变量完成全部验证，成功后再把结果发布到`app_context_t`；
- 保留原有`--read`冒烟测试，确保增加实时模式不会破坏离线路径。

修复后的初始化代码为：

```c
parsed_capture_path = NULL;
parsed_interface_name = NULL;
parsed_csv_output_path = NULL;
```

验证结果：

- 原有`--read sample.pcap`参数测试恢复通过；
- 新增`--interface lo`参数测试通过；
- 离线输入与实时输入冲突测试通过；
- CTest回归测试通过。

经验：

- 声明指针不会自动把它设置成`NULL`，局部变量必须在读取前明确初始化；
- 新增一种状态时，要同步检查“声明、初始化、重置、发布、清理和测试”完整链路；
- 未初始化值可能随编译选项和栈内容变化，因此故障可能表现得不稳定；
- 回归测试不仅验证新功能，也负责发现新功能对已有功能造成的破坏。

### 4.2 未主动执行ping，回环接口仍立即捕获到TCP数据

现象：

执行以下实时抓包命令后，没有在第二个终端主动制造流量，程序仍很快捕获到4个数据包并退出：

```bash
sudo ./build/bin/netflow-analyzer --interface lo --count 4
```

4个包都是`127.0.0.1:45601`与`127.0.0.1:50298`之间的TCP数据，方向统计分别为2个包。

原因：

`lo`表示整个系统的回环接口，不是只属于当前命令的测试通道。任何本机进程使用`127.0.0.1`或`::1`通信，数据都会经过相应回环路径，因此能够被实时采集程序看到。

使用`ss -tnp`检查端口后，确认`127.0.0.1:45601`由VS Code的`code-...`进程持有，另一端是它正在连接的本地服务。VS Code和扩展运行期间可以持续产生这类本地TCP流量。

验证结果：

- 第1包和第4包从端口45601发往50298，捕获字节数为`167 + 129 = 296`；
- 第2包和第3包方向相反，捕获字节数为`66 + 177 = 243`；
- 规范化双向五元组把4个包正确聚合为1条TCP流；
- 首末时间分别取自第1包和第4包；
- 全部15项CTest测试通过。

经验：

- 抓包程序观察的是接口上的实际流量，不会只接收测试者主动制造的数据包；
- `--count`统计所有捕获成功的包，不保证包属于某个指定进程或协议；
- 需要可重复验证指定流量时，应关闭无关程序，或者使用后续将加入的BPF过滤；
- 端口号只能描述连接端点，结合`ss -tnp`等工具才能进一步定位持有套接字的进程。

### 4.3 应用处理4个ICMP包，但libpcap报告接收8个包

现象：

在`lo`上使用`--filter "icmp" --count 4`抓包，并执行`ping -c 2 127.0.0.1`后，应用输出：

```text
Total packets: 4
Capture received packets: 8
```

最初疑问是程序是否重复计数，或者是否发生了4个包的丢失。

排查结论：

- `ping -c 2`会产生2个Echo Request和2个Echo Reply，共4个逻辑ICMP包；
- Linux回环路径会让每个逻辑包同时出现outgoing和incoming捕获事件；
- Linux PF_PACKET统计已经计算通过BPF的这些事件，因此`pcap_stats().ps_recv`可以得到8；
- libpcap在向应用交付回环数据时会抑制outgoing重复副本，因此`capture_next_packet()`仍只返回4个逻辑包；
- Linux统计还可能包含已经进入内核捕获缓冲区、但尚未被应用读取的包。

结果解释：

`Total packets`属于应用处理层，`Capture received packets`属于捕获后端层。二者的差值不是丢包数，不能使用：

```text
Capture received packets - Total packets
```

推导应用丢包。抓包缓冲区和接口层丢包应分别查看`Capture dropped packets`和`Interface dropped packets`，同时保留平台可能不提供某些指标的限制说明。

经验：

- 监控指标必须标明观察层级，名称相似不代表统计口径相同；
- 回环接口适合功能验证，但方向语义和物理网卡不同；
- 遇到看似异常的计数时，应先检查操作系统、驱动和第三方库的统计定义，再判断业务计数是否错误；
- 该现象属于平台统计语义，不需要通过修改应用计数来强行让两个数字相等。

## 5. ARM Linux开发板环境

### 5.1 VFAT格式SD卡挂载后无法创建文件或目录

现象：

在LubanCat-2N上准备项目目录时，SD卡分区`/dev/mmcblk1p1`挂载在`/media/usb0`，文件系统类型为`vfat`（FAT32）。普通用户无法在挂载目录内创建文件或目录；尝试执行：

```bash
sudo chown ubuntu:ubuntu /media/usb0
```

仍然返回`Operation not permitted`，提高到root权限没有解决问题。

根因：

FAT32不保存Linux原生的每文件UID、GID和Unix权限位。分区挂载后，Linux通过挂载参数为其中所有文件和目录统一呈现所有者与权限。因此，对已挂载VFAT文件系统中的目录执行`chown`或`chmod`，不能像ext4上一样持久修改所有者或权限。

挂载点目录自身在未挂载时的权限，也不能决定已挂载VFAT内容的访问权限；挂载后的可见权限由该文件系统的挂载选项控制。

解决办法：

卸载后重新挂载`/dev/mmcblk1p1`，通过以下选项把文件系统映射给实际登录用户：

- `uid`：所有文件和目录对Linux呈现的用户ID；
- `gid`：所有文件和目录对Linux呈现的组ID；
- `dmask`：从目录基础权限中屏蔽的权限位；
- `fmask`：从普通文件基础权限中屏蔽的权限位。

例如，`dmask=0022`通常使目录呈现为`0755`，`fmask=0133`通常使普通文件呈现为`0644`。`uid`和`gid`应取自开发板实际登录用户，可先通过`id`确认，不应固定假设用户名为`ubuntu`。该`fmask`示例会屏蔽普通文件的执行位，适合保存源码和数据，不适合直接运行放在VFAT上的构建产物。

验证结果：

使用适合当前用户的`uid`、`gid`、`dmask`和`fmask`重新挂载后，已能够在`/media/usb0`下创建项目文件和目录。该结果只证明SD卡写权限问题已解决，尚不代表项目已经在开发板完成克隆、构建或运行验收。

经验：

- `sudo`只能绕过Linux权限检查，不能为FAT32补充其原本不支持的Unix所有权元数据；
- 排查移动存储权限问题时，应先用`findmnt`或`lsblk -f`确认设备、挂载点、文件系统类型和挂载选项；
- `dmask`和`fmask`表示“要屏蔽的权限位”，不是最终权限值；
- 如果需要重启后保持设置，可后续使用分区UUID在`/etc/fstab`中配置挂载选项，避免依赖可能变化的`/dev/mmcblk1p1`设备名；
- 如果项目后续需要符号链接、原生Unix权限或其他Linux文件系统语义，应评估使用ext4；重新格式化会清除数据，不能把它当作无风险的权限修复步骤。

#### 重启后挂载参数丢失导致Git只读

开发板完成受控重启后，SD卡再次显示为`root:root`。Git先以`detected dubious ownership`拒绝读取仓库；为自己确认可信的精确仓库路径加入`safe.directory`后，`git status`能够只读运行，但`git pull`仍因无法写入`.git/FETCH_HEAD`而返回`Permission denied`。

这两个错误属于不同边界：

- `safe.directory`只表示Git信任该仓库，不授予文件系统写权限；
- `.git/FETCH_HEAD`需要在`fetch`阶段更新，最终仍受VFAT挂载时模拟的UID、GID和权限位控制。

只执行带`uid`、`gid`、`dmask`和`fmask`的`remount`后，`findmnt`与`stat`仍显示原有参数和`root:root`，因此不能只根据`mount`退出状态判断设置已经生效。本次先离开挂载目录，再完整卸载并按当前`id -u`、`id -g`重新挂载；随后普通用户可以执行`git pull --ff-only`，证明仓库写权限恢复。

本次使用精确仓库例外，没有配置宽泛的`safe.directory '*'`，也没有用`sudo git pull`掩盖权限问题。当前重新挂载只恢复了本次启动中的状态；仍需使用分区UUID把相同选项写入`/etc/fstab`并做重启复测，才能证明挂载权限持久化。

### 5.2 CTest版本与VFAT执行权限导致板上测试无法启动

现象：

项目在LubanCat-2N上完成48个目标的链接后，从源码根目录执行：

```bash
ctest --test-dir build-arm --output-on-failure
```

CTest仍然显示源码根目录为测试目录，并报告`No tests were found`。检查确认开发板使用CTest 3.16.3，且`build-arm/CTestTestfile.cmake`已经生成。

进入`build-arm`后，CTest成功发现16项测试，但15个C测试程序均显示`permission denied`和`BAD_COMMAND`；Python离线验收能够启动，随后也因无法执行`bin/netflow-analyzer`而失败。此时测试程序没有进入业务逻辑，不能把结果解释为16项功能测试失败。

根因：

问题由两个独立的目标环境差异叠加造成：

1. CTest的`--test-dir`选项从CMake 3.20开始提供；开发板上的3.16.3不会按该选项切换到构建目录，因此应先`cd`进入构建目录；
2. 构建目录位于VFAT分区，当前挂载权限屏蔽了普通文件的执行位。链接器可以创建ELF文件，但Linux在启动进程时仍会检查文件执行权限和挂载选项，所以“链接成功”不等于“能够执行”。

解决办法：

保留位于`/media/usb0/Workspace/netflow-analyzer`的源码树，把CMake二进制目录改到Linux根文件系统：

```text
/media/usb0/Workspace/netflow-analyzer        VFAT源码树
/home/cat/build/netflow-analyzer-debug        Linux原生构建树
```

使用CMake的源码目录与构建目录分离能力，在`/home/cat/build/netflow-analyzer-debug`重新配置和构建。测试时先进入该目录，再执行：

```bash
ctest --output-on-failure
```

验证结果：

- 开发板完成Debug配置和原生构建；
- CTest在ext4构建目录中能够启动全部测试程序；
- 用户确认16项CTest全部通过；
- 开发板随后完成Release原生构建，确定性6包PCAP端到端验收通过；
- 不需要修改C源码、测试代码或放宽VFAT上全部文件的执行权限。

经验：

- CMake项目声明最低支持3.16时，文档中的测试命令也应兼容3.16，不能默认使用3.20才加入的选项；
- out-of-source build不仅保持源码目录整洁，还允许源码与构建产物位于具有不同权限语义的文件系统；
- 移动存储适合交换源码、PCAP和导出数据，Linux原生构建产物更适合放在ext4等支持Unix权限的文件系统；
- 如果长期在开发板上进行Git操作和源码开发，源码树也放到ext4会更稳妥；VFAT缺少符号链接、大小写和Unix权限等完整语义；
- 看到CTest的`BAD_COMMAND`时应先检查程序是否成功启动，再判断测试断言或业务代码。

### 5.3 VMware虚拟机可以访问开发板，但开发板不能反向访问虚拟机

现象：

- VMware虚拟机地址为`192.168.78.130`；
- LubanCat-2N地址为`192.168.1.102`；
- 虚拟机能够`ping`开发板，开发板不能`ping`虚拟机；
- 从虚拟机向开发板发送ICMP流量时，开发板上的Release程序使用BPF完成跨设备实时抓包验收。

原因：

两个地址不属于同一子网。虚拟机位于VMware NAT私有网络`192.168.78.0/24`，开发板位于物理局域网`192.168.1.0/24`。虚拟机主动访问开发板时，流量可以由宿主机执行NAT后进入物理网络；反向流量不能直接以`192.168.78.130`为目标建立新连接，因为物理网络通常没有指向VMware私有子网的路由，NAT也不会为未建立的入站连接自动创建映射。

这属于虚拟化网络拓扑，不是开发板网卡、ICMP协议解析、BPF或流表故障。

验证结果：

- 虚拟机发出的两次ICMP Echo请求能够到达开发板并收到响应；
- 开发板物理网卡上的分析程序按BPF过滤取得请求和响应，并完成双向流聚合；
- ARM64 Release程序第一次完成跨设备、非回环接口的实时抓包验证；
- 开发板实际观察到的对端为`192.168.1.100`，而不是虚拟机私网地址`192.168.78.130`，确认宿主机执行了地址转换；
- 两个Echo Request的TTL为63，开发板发出的Echo Reply在本机接口上为64，符合请求经过一个三层转发/NAT节点后到达开发板的路径；
- 应用处理4包，两个方向各2包、196字节，首末时间和ICMP序号1、2均正确；
- 使用`--count 20`并在ping结束后按Ctrl+C时，应用仍处理4包，而Linux捕获后端报告6包，两个drop字段均为0。

关于后端6包：

本次实验排除了“应用达到`--count 4`后立即停止”这一主要因素。Linux上的`ps_recv`表示捕获后端接受的累计包数，仍可能包含尚未交付给应用的包；项目还使用`pcap_open_live`先激活捕获句柄，再安装BPF，因此打开与安装过滤器之间的极短窗口也可能进入后端统计。

当前输出能够证明多出的2包没有进入协议解析和流聚合，也没有被报告为缓冲区或接口丢包，但不能仅凭`pcap_stats()`还原它们的具体内容。除非增加抓包文件或更细粒度观测，否则不应声称它们一定是重复ICMP、NAT副本或某一种后台流量。

经验：

- “能够返回已有连接的响应”和“能够从外部主动访问NAT内部主机”是两种不同能力；
- 跨子网测试前应先检查双方地址、掩码、路由表和虚拟机网络模式；
- 如果需要开发板主动连接虚拟机，优先给虚拟机增加桥接网卡，使其获得`192.168.1.x`地址；可以保留原NAT网卡用于互联网访问；
- NAT模式仍然足以完成“虚拟机主动发包、开发板抓取并分析”的当前验收目标。

### 5.4 同一确定性PCAP的x86_64与ARM64输出对比

目标：

板端原生构建和测试通过后，还需要区分“两个平台各自能运行”与“同一输入得到一致业务结果”。因此使用Python端到端验收生成的确定性6包PCAP，在开发虚拟机和LubanCat-2N上分别运行Release程序。

验证方法：

- 对两个平台使用完全相同的PCAP文件，并核对SHA-256摘要一致；
- 分别保存程序标准输出和退出状态；
- 使用`diff -u`对标准输出作逐行比较；
- 同时人工核对包数、预览数、双向流字段和时间戳。

结果：

- 两个平台退出状态均为0，`diff -u`没有差异；
- 都处理6包、预览5包并生成1条ICMP双向流；
- 两个方向各3包、138捕获字节和138线路字节；
- 首末时间戳及所有终端字段一致。

结论与边界：

该结果证明当前确定性离线输入在x86_64与AArch64 Linux上具有一致的应用输出，也验证了固定宽度整数、显式网络字节序和稳定流键处理在这两个小端平台上的可移植性。它不等价于大端平台验证，也不能代替ARM性能、实时网卡和长期运行测试。

### 5.5 libpcap读取超时没有在接口静默时触发周期报告

现象：

实时周期指标初版仍使用阻塞式`pcap_next_ex()`。程序在`lo`上启动后，如果没有匹配ICMP包，预期的5秒报告没有出现；约21秒后发送第一个包，才输出：

```text
Runtime metrics: interval=20.999 packets=1 ...
```

这说明指标计算本身能够执行，但主循环在静默期没有获得运行机会。

根因：

`pcap_open_live()`的`to_ms`是捕获缓冲区读取超时，不是保证应用每隔指定毫秒从`pcap_next_ex()`返回的墙钟定时器。其行为依赖操作系统、libpcap捕获机制和数据包是否已经到达；不能把它作为周期任务调度器。

解决办法：

- 实时句柄显式设置为非阻塞；
- 取得libpcap提供的可选择文件描述符；
- 主循环暂时无包时调用`poll()`，等待可读或最多1秒超时；
- 每次返回后用`CLOCK_MONOTONIC`检查真实的5秒指标期限；
- 保留`EINTR`和停止请求处理，使信号退出继续沿正常清理路径工作。

没有新增统计线程，因为当前累计计数和流表由单线程主循环拥有。单独线程会引入一致快照、锁、输出交错、停止和回收问题，而现阶段没有相应性能收益证据。

验证结果：

- 静默时连续输出约5.007至5.008秒的零包报告；
- 30次本地ping产生60个应用包，分别进入16包和44包两个区间，合计保持60；
- 报告中的PPS和Mbps按各区间真实时长计算，流量结束后再次稳定输出零；
- Ctrl+C后正常输出最终流、应用包数和捕获后端统计；
- 17项CTest全部通过，`git diff --check`无错误。

经验：

- 第三方库中的“timeout”必须先确认作用层级和平台保证，不能只根据参数名推断定时语义；
- 事件循环需要同时照顾数据就绪、周期任务和停止请求，`poll()`提供的是有界等待，真正的报告时钟仍由单调时钟负责；
- 设置非阻塞不会让libpcap“自己唤醒”，而是让读取在无包时立即返回；真正避免忙轮询并让程序定期获得控制权的是`poll()`；
- 功能验收已经通过，但CPU占用仍应在开发板空闲和高流量场景中单独量化。

### 5.6 使用rsync导出板端sysroot时的远端工具、目录和符号链接问题

目标：

为了在没有官方SDK的情况下交叉编译，需要从LubanCat-2N导出目标头文件、启动文件、libc、libpcap和`pkg-config`元数据，形成只用于编译和链接的sysroot快照。

第一次错误：

虚拟机执行`rsync`后出现：

```text
bash: rsync: command not found
rsync: connection unexpectedly closed
rsync error: error in rsync protocol data stream (code 12)
```

原因是`rsync`通过SSH工作时需要两端都安装程序。虚拟机的Receiver为3.2.7，但远端LubanCat没有`rsync`命令。板端安装`rsync`后恢复。

第二次错误：

```text
mkdir ".../usr/lib/aarch64-linux-gnu" failed: No such file or directory
rsync error: error in file IO (code 11)
```

`[Receiver]`表明故障发生在虚拟机接收端。此前只创建了sysroot的`usr/`，没有递归创建`usr/lib/`中间目录。使用`mkdir -p`创建完整目标路径后恢复。

第三次错误：

对完整`/usr/lib/aarch64-linux-gnu/`使用`rsync -aL`时，`-L`要求追踪每一条符号链接，最终复制约2GB，并在板端原本就失效的Qt链接处出现：

```text
symlink has no referent:
"/usr/lib/aarch64-linux-gnu/qt-default/qtchooser/default.conf"
rsync error: some files/attrs were not transferred (code 23)
```

该Qt文件与项目无关，libpcap、启动文件和其他关键内容已经传输，但这说明对整个系统库树追踪符号链接不是合适的sysroot策略。重新使用`rsync -a`保留符号链接，只对明确的`/lib/ld-linux-aarch64.so.1`单文件使用`-aL`。

经验：

- sysroot是目标系统供编译器使用的头文件和库视图，不是完整、可启动的系统副本；
- `rsync`错误中的Sender／Receiver有助于判断问题位于远端还是本地；
- `mkdir -p`需要覆盖完整目标父目录；
- 对完整系统目录应保留符号链接，对经过确认的单文件才按需解引用；
- sysroot体积大且与目标镜像版本绑定，不应提交Git。

### 5.7 通用Ubuntu交叉GCC生成ARM64程序，但板端缺少GLIBC_2.34

现象：

Ubuntu 24.04的`aarch64-linux-gnu-gcc` 13成功生成了ARM64 ELF，上传LubanCat后却在进入程序之前失败：

```text
/home/cat/bin/netflow-analyzer-cross:
/lib/aarch64-linux-gnu/libc.so.6:
version `GLIBC_2.34' not found
```

ABI检查显示：

```text
程序需要：GLIBC_2.17、GLIBC_2.34
板端sysroot libc最高：GLIBC_2.30
触发符号：__libc_start_main@GLIBC_2.34
```

CMake缓存已经把libpcap定向到板端sysroot，但GCC查询启动文件仍返回：

```text
/usr/aarch64-linux-gnu/lib/crt1.o
```

根因：

`CMAKE_SYSROOT`控制了目标头文件和许多库搜索，但Ubuntu交叉GCC驱动仍有自己的启动文件前缀。链接过程混用了板端libpcap和Ubuntu 24.04的glibc启动文件，后者让`__libc_start_main`要求`GLIBC_2.34`。因此“ELF是AArch64”只能证明指令集正确，不能证明目标ABI兼容。

解决办法：

在通用GCC方法中同时设置：

```text
--sysroot=/home/zcb/sysroots/lubancat2n
-B/home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/
```

`--sysroot`提供板端目标文件系统视图；GCC `-B`把板端启动文件目录放到搜索顺序最前面。探测确认`Scrt1.o`、`crti.o`、`crtn.o`和`libc.so`均来自板端sysroot。

验证结果：

- Ubuntu GCC 13完成Release交叉构建；
- 产物为AArch64 PIE；
- 动态加载器为`/lib/ld-linux-aarch64.so.1`；
- 最终只要求`GLIBC_2.17`；
- 板端`ldd`、`--help`和真实ICMP抓包成功。

禁止的错误修复：

不能把Ubuntu的ARM64 `libc.so.6`复制到开发板，也不能手工替换或伪造板端libc链接。glibc是整个用户空间的基础依赖，强行升级可能使系统命令和服务整体无法启动。

### 5.8 官方SDK工具链在Ubuntu 24.04上缺少libisl.so.15

现象：

官方离线SDK通过`repo sync -l`只检出Buildroot GCC 9.3后，CMake编译器探测失败：

```text
The C compiler identification is unknown
cc1: error while loading shared libraries:
libisl.so.15: cannot open shared object file
```

`ldd cc1`还显示`libmpfr.so.4`未找到。

根因：

交叉编译器包含两类程序：

- GCC驱动、`cc1`等运行在x86_64虚拟机上的主机程序；
- 它们生成和链接的AArch64目标文件。

SDK中的旧版x86_64 `cc1`依赖Ubuntu 24.04不再默认提供的旧SONAME。SDK自身已经携带`libisl.so.15`、`libmpfr.so.4`、`libmpc.so.3`和`libgmp.so.10`，只是主机动态加载器没有搜索SDK的`lib/`。

解决办法：

```bash
export LD_LIBRARY_PATH="$LUBANCAT_TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

设置后`ldd cc1`的全部依赖均可解析，CMake识别GCC 9.3并完成构建。没有把旧库安装或复制到主机`/usr/lib`，避免污染Ubuntu系统环境。

相关Shell错误：

新开终端后，`NFA_SDK_BUILD`等变量消失，CMake曾报告：

```text
CMake Error: No build directory specified for -B
```

`-B "$NFA_SDK_BUILD"`在变量为空时没有有效目录参数。通过`printf '<%s>\n' "$NFA_SDK_BUILD"`检查，并在当前Shell重新执行`export`后解决。

验证结果：

- 官方GCC内置sysroot使用glibc 2.29，低于板端glibc 2.30；
- SDK没有libpcap，因此从板端提取pcap头文件、共享库和`libpcap.pc`到独立overlay；
- 最终产物只要求`GLIBC_2.17`；
- 板端动态加载和实时ICMP抓包成功。

### 5.9 官方GCC 9报告局部处理结果可能未初始化

现象：

官方GCC 9 Release交叉构建完成，但对`app_process_packet()`中的局部`result`给出：

```text
warning: ‘result’ may be used uninitialized in this function
[-Wmaybe-uninitialized]
```

控制流检查结果：

- 调用者的`packet_result`已经初始化；
- 协议分发错误的三类可继续结果都会给`result`赋值；
- 分发成功后，流表成功和`ENOSPC`拒绝路径也都会赋值；
- 其他错误直接返回，不会继续读取`result`。

因此现有控制流没有发现实际未初始化读取，告警来自GCC 9在Release优化下无法证明所有分支覆盖。为了让正常成功语义显式，并避免未来控制流变化引入风险，局部结果使用`RUNTIME_METRICS_PACKET_RESULT_COMPLETE`初始化。

验证结果：

- x86_64 Debug构建完成；
- 17项CTest全部通过；
- 官方SDK和通用GCC两种ARM64 Release构建均成功；
- 两种交叉产物在LubanCat-2N运行成功。

两条交叉编译方法的完整命令、变量和CMake参数见[`docs/cross_compilation.md`](cross_compilation.md)。

### 5.10 板端Python 3.8无法加载Python 3.9内置泛型注解

现象：

LubanCat原生Debug构建成功。CTest中的16个C测试全部通过，但`offline_flow_acceptance`在加载Python脚本时失败：

```text
TypeError: 'type' object is not subscriptable
```

异常位置是：

```python
packets: list[tuple[int, int, bytes]]
```

环境检查确认板端解释器为：

```text
/usr/bin/python3
Python 3.8.10
```

原因：

- `list[...]`和`tuple[...]`作为类型注解使用属于Python 3.9引入的内置泛型语法；
- Python 3.8在定义`write_pcap()`时会计算函数参数注解，内置`list`尚不支持该下标操作；
- 异常发生在测试脚本加载阶段，尚未创建PCAP，也没有启动`netflow-analyzer`；
- 因此该结果不能解释为C程序、ARM64架构、libpcap或离线分析逻辑失败。

处理：

没有为了测试脚本升级或替换系统Python，而是从`typing`导入`List`和`Tuple`，把三处数据包列表注解统一改为：

```python
List[Tuple[int, int, bytes]]
```

该选择保持类型信息不变，同时兼容目标系统提供的Python 3.8，避免为了测试工具改变板端系统级Python及其包依赖。

验证结果：

- x86_64上的`offline_flow_acceptance`通过；
- x86_64全量17项CTest通过；
- LubanCat上的`offline_flow_acceptance`通过；
- LubanCat ARM64原生Debug全量17项CTest通过；
- 修改只涉及Python验收脚本，没有修改C业务代码。

### 5.11 性能测量缺少GNU time且管道输出暂时不可见

第一个现象：

开发板执行：

```bash
/usr/bin/time --version
```

返回：

```text
bash: /usr/bin/time: No such file or directory
```

Bash自身的`time`关键字通常只能显示`real`、`user`和`sys`，不能直接提供最大RSS等详细字段。安装`time`软件包后取得GNU `/usr/bin/time -v`；它只作为开发测量工具，不是正式程序运行依赖。

第二个现象：

把程序输出接入：

```bash
2>&1 | tee 结果文件
```

后，终端暂时没有显示周期报告。人工按Ctrl+C后，结果文件为空。

原因：

- `stdout`直接连接终端时通常按行缓冲；
- 连接管道后，C标准库可能改用块缓冲；
- 没有立即显示不表示抓包循环卡死；
- Ctrl+C会作用于整个前台管道，分析器、`timeout`、`time`和`tee`可能同时退出，缓冲数据来不及保存。

处理：

```bash
stdbuf -oL -eL netflow-analyzer ... 2>&1 | tee 结果文件
```

`-oL`和`-eL`分别让标准输出和标准错误按行刷新。之后终端能够实时显示周期报告，日志也完整保留。

这只是性能测试阶段的外部补救。进入systemd服务化后，程序已经在`main()`对stdout执行：

```c
setvbuf(stdout, NULL, _IOLBF, 0);
```

调用位于stdout任何I/O之前，使终端、普通文件、Shell管道和systemd journal都采用明确的行刷新契约，不再要求部署环境通过`stdbuf`改变标准I/O行为。stderr仍用于启动与致命错误。

此前“命令中存在`| tee`且看起来也能逐行输出”并不能证明分析器面对的就是普通管道：`sudo`可能为子进程分配伪终端；全缓冲也会在缓冲区填满时成批输出；正常退出还会刷新剩余数据。严格复测把`> logfile 2>&1`放在`sudo sh -c`内部，确保重定向覆盖sudo可能提供的伪终端。12秒运行尚未结束时，第7秒已经可以从普通文件读取第一条5秒周期报告，证明程序自身的行缓冲生效。

验证结果：

- 空闲22.04秒：应用0包、进程CPU时间0.01秒、最大RSS 1764 KiB、drop为0；
- 20万包场景通过`stdbuf`完整保存程序和GNU `time`输出，应用分类、资源数据和退出状态均可追溯；
- 服务化前的严格普通文件重定向测试无需`stdbuf`，运行期间即可读取周期报告；
- 详细数据和复现命令记录在`docs/performance_baseline.md`。

### 5.12 长稳监控缺少测试后软中断快照

10分钟、128流、540万包测试完成后，读取：

```text
softirqs-10m-after.txt
```

返回`No such file or directory`。测试前快照已经保存，但测试后聚合命令没有形成文件，因此不能计算严格覆盖本轮600秒窗口的`NET_RX`和`NET_TX`累计增量。

测试结束后再读取`/proc/softirqs`不能无条件当作终值，因为累计计数会继续包含SSH和其他网络活动。没有为了补一个累计次数重跑540万包，原因是同一窗口内的`mpstat -P ALL 5 120`已经连续记录软中断CPU占用：整机平均`%soft=1.69`，CPU0为8.00%，整机仍空闲90.97%。

其余长稳证据完整：540万包全部分类为`complete`，后端接收同为540万，两个drop为0；`pidstat`首尾RSS均为564 KiB，严重缺页为0；尾部PPS仍约9 K。缺失的累计快照作为测量边界记录，不解释为程序故障。

### 5.13 多行`printf`格式字符串之间误加逗号

为实时退出汇总增加`Expired flows`和`Evicted flows`两行时，构建出现：

```text
warning: format '%zu' expects argument of type 'size_t', but argument 2 has type 'char *'
warning: too many arguments for format
```

错误代码把两个字符串写成了两个函数实参：

```c
printf(
    "Expired flows: %zu\n",
    "Evicted flows: %zu\n",
    total_expired_flow_count,
    total_evicted_flow_count);
```

因此第一个字符串是唯一的格式字符串，第二个字符串成为第一个`%zu`对应的`char *`参数，后面的两个`size_t`又成为多余实参。

C会在编译期自动拼接相邻字符串字面量。删除两个字符串之间的逗号后，它们构成同一个包含两个`%zu`的格式字符串：

```c
printf(
    "Expired flows: %zu\n"
    "Evicted flows: %zu\n",
    total_expired_flow_count,
    total_evicted_flow_count);
```

修正后严格格式警告消失，本地17项CTest全部通过，300个不同UDP五元组的实时淘汰验收也成功完成。该问题说明多行`printf`修改后必须核对格式说明符数量、类型与实参顺序，不能只根据终端排版判断逗号位置。

### 5.14 官方GCC 9无法证明探测结果与返回码的条件化初始化关系

单次满表扫描优化在x86_64 Debug构建和测试中通过，但使用官方GCC 9执行Release交叉构建时，对`flow_table_process_packet_internal()`中的局部探测结果给出多条告警：

```text
warning: ‘probe_result.oldest_slot_index’ may be used uninitialized
warning: ‘probe_result.oldest_found’ may be used uninitialized
warning: ‘probe_result.slot_index’ may be used uninitialized
warning: ‘probe_result.found’ may be used uninitialized
```

`flow_table_probe()`采用“局部构造、成功后发布”的失败不修改契约：

- 返回`0`时已经完整写入`probe_result`，结果表示已有流或可插入槽位；
- 返回`ENOSPC`时也已经完整写入`probe_result`，结果包含整张满表的最旧候选；
- 返回`EINVAL`等其他错误时不修改调用者传入的结果对象。

调用者只在返回`0`或`ENOSPC`后读取结果字段，其他错误会直接返回，因此控制流检查没有发现实际的未初始化读取。告警来自旧版GCC在Release优化下无法稳定证明“特定返回码意味着输出参数已经发布”的跨函数关系。

处理时没有改变公开输出的失败不修改语义，只把两个调用点的私有局部变量显式零初始化：

```c
flow_table_probe_result_t probe_result = {0};
```

`{0}`为所有布尔值和索引提供确定初值；正常成功路径仍由`flow_table_probe()`覆盖完整结构，错误路径仍不会把局部默认值发布给API调用者。第二个`flow_table_find()`调用点同步采用相同写法，使相同输出参数模式保持一致，并避免后续控制流调整再次触发旧编译器告警。

验证结果：

- x86_64 Debug构建没有警告；
- 本地17项CTest全部通过；
- `git diff --check`通过；
- 官方Buildroot GCC 9.3 Release交叉构建不再报告该告警；
- 生成的程序仍为AArch64 ELF，动态加载器为`/lib/ld-linux-aarch64.so.1`，最高只要求`GLIBC_2.17`；
- 本轮产物SHA-256为`c2dcc119ebbd27d321dbf041683d57a31ac0d22645fe16fea2ce08e873b37036`；
- 产物在LubanCat-2N成功加载并完成300流复测：300包全部`complete`、44次最旧流淘汰、最终256条流、`operations=300`，两个drop字段均为0。

### 5.15 CSV新增TCP状态后测试预期和Python检查块未同步

第一个现象：

CSV表头测试通过，但完整记录测试失败：

```text
[PASS] CSV header
[FAIL] tests/unit/test_flow_export.c:215: strcmp(actual_text, expected_record) == 0
```

实现已经在协议号后写入`tcp_state_name`，实际记录以：

```text
6,established,10.0.0.1,12345,...
```

开头，但`expected_record`仍保持旧格式：

```text
6,10.0.0.1,12345,...
```

因此编译器和格式检查都没有报错，运行时的精确字符串比较仍会失败。这属于输出契约变化后测试夹具未同步，不是状态机或`fprintf`参数错位。把预期记录增加`established`字段后，CSV记录测试通过。

第二个现象：

TCP CSV验证代码被粘贴到`run_processing_results_test()`末尾，而不是`run_tcp_state_output_test()`内部，并出现缩进错误。错误位置既访问了该函数中不存在的`csv_path`，又会使Python在执行C程序前就因语法或作用域问题失败。

处理：

- 从压力场景函数删除整段TCP CSV检查；
- 把检查块移动到三次握手场景的`with TemporaryDirectory`作用域内；
- 使用一致的8空格外层缩进，并在读取文件前检查CSV确实存在；
- 用`PYTHONPYCACHEPREFIX=/tmp/... python3 -m py_compile`先检查脚本语法，避免在仓库生成`__pycache__`；
- 再依次运行`flow_export_tests`、`offline_flow_acceptance`和全量CTest。

验证结果：

- CSV表头、TCP状态记录、协议与状态不变量及错误参数单元测试通过；
- 3包TCP握手在终端和CSV中均得到`established`；
- 6包ICMP CSV使用`not-applicable`；
- 本机18项CTest全部通过。

该问题说明结构化输出增加字段时要同时检查四层：格式串与实参、单元测试预期、端到端文件预期，以及测试代码所处函数和资源作用域。只看到表头通过不能证明数据行契约已经完整更新。

### 5.16 目标板journalctl不能解析带时区偏移的ISO时间

现象：

首次systemd服务验收使用：

```bash
export NFA_JOURNAL_SINCE="$(date --iso-8601=seconds)"
sudo journalctl \
    -u netflow-analyzer \
    --since "$NFA_JOURNAL_SINCE" \
    --no-pager
```

变量值为：

```text
2026-09-02T21:37:11+08:00
```

目标板返回：

```text
Failed to parse timestamp: 2026-09-02T21:37:11+08:00
```

原因：

目标板提供的`journalctl`时间解析器不接受该命令生成的带`T`和显式时区偏移格式。服务本身仍为`active`，日志也已经进入journal，因此这是客户端查询参数兼容性问题，不是分析器、stdout行缓冲或systemd单元故障。

处理：

```bash
export NFA_JOURNAL_SINCE="$(date '+%Y-%m-%d %H:%M:%S')"
```

该变量必须用双引号传给`--since`，避免Shell按中间空格拆成日期和时间两个参数。也可以不使用绝对时间，改用：

```bash
sudo journalctl -u netflow-analyzer -b --no-pager
sudo journalctl -u netflow-analyzer -n 100 --no-pager
```

验证：

- 改用`YYYY-MM-DD HH:MM:SS`后能够查看本轮服务日志；
- 静默周期报告、4个ICMP包和SIGTERM后的最终汇总均完整存在；
- 完整命令兼容性已经写入[`docs/systemd_deployment.md`](systemd_deployment.md)。
