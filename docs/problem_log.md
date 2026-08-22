# 主项目问题记录

我用本文档记录正式主项目开发过程中实际遇到的环境、构建、运行和调试问题。`labs/`中的实验问题继续保存在各自实验目录内，避免热身实验和正式项目混淆。

## 1. 我遇到的开发环境与构建配置问题

### 1.1 CMake能够编译，但VS Code找不到`analyzer/app.h`

我遇到的现象：

我的源码使用公开头文件路径：

```c
#include "analyzer/app.h"
```

我在VS Code中看到无法找到头文件，跳转和补全不能正常工作，但我的CMake配置中已经存在：

```cmake
target_include_directories(
    analyzer_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

我的排查过程：

1. 我确认头文件真实存在于`include/analyzer/app.h`；
2. 我确认`netflow_analyzer_cli`和测试目标都链接`analyzer_core`；
3. 我使用CMake实际编译，`app.c`、`main.c`和`test_smoke.c`全部成功；
4. 我检查`build/compile_commands.json`，确认真实编译命令包含：

```text
-I/home/zcb/workspace/netflow-analyzer/include
```

我根据上述证据确认编译器能够找到头文件，问题只存在于编辑器的IntelliSense配置层。

我确认的根因：

- 工作区早期的CMake源码目录仍指向`labs/blocking_queue`，切换正式主项目后需要重新指向仓库根目录；
- C/C++扩展依赖`ms-vscode.cmake-tools`配置提供器，但该提供器没有正确应用根项目配置；
- `c_cpp_properties.json`仍配置为Clang和C17，而实际项目使用GCC和C11；
- C/C++扩展没有直接读取CMake生成的`compile_commands.json`。

我的解决办法：

1. 我让根项目导出编译数据库：

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

2. 我把VS Code的CMake目录切换为正式主项目：

```jsonc
"cmake.sourceDirectory": "${workspaceFolder}",
"cmake.buildDirectory": "${workspaceFolder}/build"
```

3. 我让C/C++扩展直接读取编译数据库：

```json
"compileCommands": "${workspaceFolder}/build/compile_commands.json"
```

4. 我让IntelliSense配置和真实编译工具保持一致：

```json
"compilerPath": "/usr/bin/gcc",
"cStandard": "c11",
"intelliSenseMode": "linux-gcc-x64"
```

5. 我删除`configurationProvider`配置，避免它覆盖直接编译数据库；随后执行：

```text
C/C++: Reset IntelliSense Database
Developer: Reload Window
```

我的验证结果：

- 我确认VS Code能够识别`#include "analyzer/app.h"`；
- 我确认头文件跳转和代码补全已经恢复；
- 我确认CMake根项目保持零警告编译。

我的总结：

- 我认识到编辑器红线不一定表示编译器错误，因此会先用真实构建命令验证；
- 我会把`compile_commands.json`作为排查依据，因为它记录了每个源文件真正使用的编译器、宏、语言标准和头文件路径；
- 我不会为了消除编辑器红线，把公开头文件改成`../include/...`等依赖源码位置的相对路径；
- 从实验子项目切换到正式根项目时，我会同步检查CMake Tools、C/C++扩展、构建目录和调试目标。
