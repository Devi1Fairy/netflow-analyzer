# 多线程流水线实验问题记录

## 1. 遇到的问题

### 1.1 CMake已经识别目标，但找不到可执行程序

现象：

```text
bash: ./build/pointer_queue_demo: No such file or directory
```

检查结果：

- `CMakeLists.txt`已经定义`pointer_queue_demo`；
- CMake生成的目标列表中已经存在`pointer_queue_demo`；
- `build`目录中仍然只有上一步生成的`pipeline_types_demo`。

根因：

修改CMake配置后执行了Configure，使CMake生成了新目标的编译规则，但没有执行Build，所以`pointer_queue_demo`尚未经过编译和链接。

解决办法：

在A3实验目录执行：

```bash
cmake --build build
```

编译完成后运行：

```bash
./build/pointer_queue_demo
```

验证结果：

```text
Task pointer pushed into queue.
Task pointer popped from queue:
  task_id=1
  input_value=12
Generated result:
  output_value=144
Pointer queue destroyed successfully.
```

经验：

```text
CMake Configure：读取CMakeLists.txt并生成构建规则
CMake Build：按照构建规则执行编译和链接
运行程序：执行Build生成的可执行文件
```

Configure成功只说明目标配置有效，不代表可执行文件已经生成。运行新目标前，应当确认`cmake --build build`已经成功完成。

### 1.2 注释`Threads::Threads`后仍然能够链接运行

现象：

从`pointer_queue`目标中暂时移除以下依赖后，程序在当前Ubuntu环境中仍能编译和运行：

```cmake
target_link_libraries(
    pointer_queue
    PUBLIC
        Threads::Threads
)
```

检查结果：

```text
CMAKE_HAVE_LIBC_PTHREAD=1
glibc 2.39
```

最终链接命令中也没有单独出现`-pthread`或`-lpthread`。这是因为当前glibc已经在`libc`中提供pthread符号，CMake判断该平台不需要额外线程库参数。

这不表示依赖声明多余。保留`Threads::Threads`仍有以下作用：

- 明确说明`pointer_queue`依赖平台线程能力；
- 由CMake在需要时自动传递`-pthread`、线程库或平台特定选项；
- 避免代码换到旧版Linux、其他Unix或交叉编译工具链后链接失败；
- `PUBLIC`保证静态库的最终使用者继承线程依赖。

经验：

当前机器不需要额外链接参数，不等于项目没有线程依赖。现代CMake应通过`find_package(Threads REQUIRED)`和`Threads::Threads`表达语义依赖，而不是根据某一台机器的链接结果删除配置。

### 1.3 流水线程序成功，但CSV验收测试失败

现象：

```text
50% tests passed, 1 tests failed out of 2
thread_pipeline_acceptance (Failed)
```

使用详细模式重新运行失败测试：

```bash
ctest --test-dir build \
      -R thread_pipeline_acceptance \
      --output-on-failure \
      -V
```

CTest报告：

```text
list sub-command GET requires at least three arguments
```

检查结果：

- `thread_pipeline_demo`退出状态为0；
- `pipeline_acceptance.csv`已经正常生成；
- CSV包含1行表头和12行结果；
- 失败位置是`verify_pipeline_csv.cmake`中的`list(GET)`。

根因：

CMake的`list(GET)`需要依次提供列表名、元素下标和输出变量。原代码漏写了表头所在的下标`0`：

```cmake
list(GET
    csv_lines
    csv_header
)
```

正确写法：

```cmake
list(GET
    csv_lines
    0
    csv_header
)
```

经验：

测试失败不一定表示业务代码失败。应使用CTest详细输出定位失败层次，并分别检查：

1. 被测程序是否成功退出；
2. 预期输出文件是否已经生成；
3. 失败是否来自测试脚本自身。

自动化测试代码同样是代码，也可能存在缺陷，需要独立检查和调试。
