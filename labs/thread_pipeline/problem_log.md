# 多线程流水线实验问题记录

## 1. 我遇到的问题

### 1.1 CMake已经识别目标，但找不到可执行程序

我遇到的现象：

```text
bash: ./build/pointer_queue_demo: No such file or directory
```

我的检查结果：

- `CMakeLists.txt`已经定义`pointer_queue_demo`；
- CMake生成的目标列表中已经存在`pointer_queue_demo`；
- `build`目录中仍然只有上一步生成的`pipeline_types_demo`。

我确认的根因：

我修改CMake配置后只执行了Configure，使CMake生成了新目标的编译规则，但没有执行Build，所以`pointer_queue_demo`尚未经过编译和链接。

我的解决办法：

我随后在A3实验目录执行：

```bash
cmake --build build
```

编译完成后我运行：

```bash
./build/pointer_queue_demo
```

我的验证结果：

```text
Task pointer pushed into queue.
Task pointer popped from queue:
  task_id=1
  input_value=12
Generated result:
  output_value=144
Pointer queue destroyed successfully.
```

我的总结：

```text
CMake Configure：读取CMakeLists.txt并生成构建规则
CMake Build：按照构建规则执行编译和链接
运行程序：执行Build生成的可执行文件
```

我认识到Configure成功只说明目标配置有效，不代表可执行文件已经生成。以后运行新目标前，我会确认`cmake --build build`已经成功完成。

### 1.2 注释`Threads::Threads`后仍然能够链接运行

我遇到的现象：

我从`pointer_queue`目标中暂时移除以下依赖后，程序在当前Ubuntu环境中仍能编译和运行：

```cmake
target_link_libraries(
    pointer_queue
    PUBLIC
        Threads::Threads
)
```

我的检查结果：

```text
CMAKE_HAVE_LIBC_PTHREAD=1
glibc 2.39
```

我在最终链接命令中也没有看到单独的`-pthread`或`-lpthread`。这是因为当前glibc已经在`libc`中提供pthread符号，CMake判断该平台不需要额外线程库参数。

我确认这不表示依赖声明多余。保留`Threads::Threads`仍有以下作用：

- 明确说明`pointer_queue`依赖平台线程能力；
- 由CMake在需要时自动传递`-pthread`、线程库或平台特定选项；
- 避免代码换到旧版Linux、其他Unix或交叉编译工具链后链接失败；
- `PUBLIC`保证静态库的最终使用者继承线程依赖。

我的总结：

我认识到当前机器不需要额外链接参数，不等于项目没有线程依赖。以后我会通过`find_package(Threads REQUIRED)`和`Threads::Threads`表达语义依赖，而不是根据某一台机器的链接结果删除配置。

### 1.3 流水线程序成功，但CSV验收测试失败

我遇到的现象：

```text
50% tests passed, 1 tests failed out of 2
thread_pipeline_acceptance (Failed)
```

我使用详细模式重新运行失败测试：

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

我的检查结果：

- `thread_pipeline_demo`退出状态为0；
- `pipeline_acceptance.csv`已经正常生成；
- CSV包含1行表头和12行结果；
- 失败位置是`verify_pipeline_csv.cmake`中的`list(GET)`。

我确认的根因：

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

我的总结：

我认识到测试失败不一定表示业务代码失败。以后我会使用CTest详细输出定位失败层次，并分别检查：

1. 被测程序是否成功退出；
2. 预期输出文件是否已经生成；
3. 失败是否来自测试脚本自身。

我认识到自动化测试代码同样是代码，也可能存在缺陷，因此也需要独立检查和调试。

### 1.4 ThreadSanitizer启动时报告unexpected memory mapping

我遇到的现象：

我使用GCC 13.3.0和`-fsanitize=thread`构建后，发现部分测试在业务代码开始执行前失败：

```text
FATAL: ThreadSanitizer: unexpected memory mapping
```

同一次CTest运行中，压力测试可能通过，而其他测试可能在启动阶段失败，表现具有随机性。

我的检查结果：

- TSan生成的是PIE可执行程序；
- 当前系统启用了进程地址空间随机化；
- 错误信息中没有任何项目源码的数据竞争调用栈；
- 使用`setarch x86_64 -R`仅关闭本次测试进程的地址随机化后，全部测试稳定通过。

我确认的根因：

我确认ThreadSanitizer需要在进程地址空间中预留大范围影子内存。当前系统随机生成的内存映射偶尔与TSan要求的地址布局冲突，导致TSan运行时在程序正式执行前终止。这属于检测工具与当前地址空间布局的兼容性问题，不是项目已经发现了数据竞争。

我的验证命令：

```bash
TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 \
setarch x86_64 -R \
ctest --test-dir build-tsan --output-on-failure
```

我的验证结果：

```text
pointer_queue_tests               Passed
thread_pipeline_acceptance       Passed
thread_pipeline_stress           Passed
100% tests passed
```

我的总结：

- 我会区分Sanitizer自身无法初始化和Sanitizer报告项目源码问题这两类故障；
- 我会通过读写地址、线程编号和项目源码调用栈判断是否是真正的数据竞争报告；
- 我确认`setarch x86_64 -R`只影响其启动的进程树，不会永久修改系统全局设置；
- 我不会为了运行测试而直接修改系统全局ASLR配置；
- 我会为ASan和TSan使用不同构建目录，不把它们组合到同一个可执行程序中。
