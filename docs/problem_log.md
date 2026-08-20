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
