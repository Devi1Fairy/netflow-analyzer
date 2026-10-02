# LubanCat-2N本地语言模型：CPU交叉构建与首次推理

记录日期：2026-10-02。板端结果来自用户提供的实际运行日志；虚拟机端已复核源码提交、CMake缓存、二进制和模型SHA-256。本文只记录独立语言模型进程的工程验收，没有把它接入网络分析器。

## 1. 本次完成了什么

已在LubanCat-2N上运行`llama.cpp`的ARM64 CPU产物，加载Qwen2.5-0.5B-Instruct的Q4_K_M GGUF并完成一次中文生成，退出码为0。首次短生成约5.26 token/s，整进程运行14.45秒，峰值RSS约687.4 MiB。

这证明交叉构建、板端动态加载、模型读取和CPU推理链可用，不证明模型能可靠分析网络。实际回答“网络流是网络中数据流的数学模型”没有准确描述本项目的双向流聚合，应记录为回答质量未通过，而不是异常检测能力验收通过。

当前没有实现：模型训练或剪枝、自行量化、NPU/GPU加速、连续推理服务、网络证据输入、在线告警或自动处置。使用别人发布的量化文件属于量化模型部署，不等于完成了量化或剪枝算法。

## 2. 环境与文件职责

| 项目 | 已确认配置 |
|---|---|
| 板端CPU | ARM64，4核Cortex-A55，最高约1.992 GHz |
| 内存 | 约1.9 GiB，运行前一次检查`available`约900 MiB；无swap |
| 板端系统工具 | GCC 9.4、CMake 3.16.3、glibc 2.31 |
| eMMC根分区 | ext4，约7.0 GiB；准备时只剩802 MiB |
| SD卡 | FAT32，约30 GiB，挂载`/media/usb0`，含`noexec`和`sync` |
| VM工具链 | 官方SDK Buildroot GCC/G++ 9.3，目标`aarch64-rockchip-linux-gnu` |

数据和构建按以下职责分开：

- 虚拟机构建：`/home/zcb/build/llama-cpu-arm64-sdk-v1`，不在板端安装训练环境或编译完整工程。
- 板端可执行文件：`/home/cat/bin/llama-completion-nfa-cpu-v1`，在eMMC的ext4上执行。
- 模型文件：`/media/usb0/netflow-analyzer-data/models/`，模型是被读取的数据，SD的`noexec`不妨碍加载。
- 手工测试日志：`/media/usb0/netflow-analyzer-data/logs/`，避免持续写入容量紧张的根分区。

`MemAvailable`才是本次启动前余量检查的依据，不能只看`free`列。上述900 MiB是历史快照，每次运行都要重新检查；不要因为进程名为`MainThread`就随意杀进程。SD挂载、安全卸载和`resize-all.service`隔离沿用已有部署约定，不因模型部署重新格式化或修改挂载。

## 3. 固定输入与产物

| 项目 | 内容 |
|---|---|
| 上游源码提交 | `5fc4f3c8c7103ffd0b7ff5ee4855bcc78a3ed5cd` |
| VM源码 | `/home/zcb/workspace/third_party/llama.cpp-nfa-cpu-v1` |
| VM产物 | `/home/zcb/build/llama-cpu-arm64-sdk-v1/bin/llama-completion` |
| 模型 | `qwen2.5-0.5b-instruct-q4_k_m.gguf`，约469 MiB |
| 模型SHA-256 | `74a4da8c9fdbcd15bd1f6d01d621410d31c6fc00986f5eb687824e7b93d7a9db` |
| 可执行文件SHA-256 | `f46b85605e0308481beb9f652bccffe0fee8cea4178a89e7a7b14af28172943a` |

来源为[Qwen官方GGUF文件](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/blob/main/qwen2.5-0.5b-instruct-q4_k_m.gguf)和[固定提交的llama.cpp](https://github.com/ggml-org/llama.cpp/tree/5fc4f3c8c7103ffd0b7ff5ee4855bcc78a3ed5cd)。`Q4_K_M`是量化格式名称，不表示整个运行时只需要文件大小的内存，也不表示每个张量都恰好用4位存储。

板端直接下载曾发生三次20秒连接超时、未收到有效数据。后来在虚拟机下载，经`scp`传到板端的`.part`文件，校验SHA-256成功后改成正式文件名。超时只说明这次请求失败，不能仅凭它判定整个板端网络损坏；不要反复重试后把残缺文件当作模型。

SHA-256用于确认传输前后字节相同，不单独证明文件来源可信。正式模型文件已经存在时先检查，不覆盖、不删除它或未知的旧`.part`文件。

## 4. GCC 9的NEON兼容问题

首次构建在`arch/arm/quants.c`失败：SDK的旧`arm_neon.h`缺少源码使用的`vld1q_s8_x4`和`vld1q_u8_x4`接口。补充两项`ggml_`封装后，又在`arch/arm/repack.cpp`遇到直接调用原始名字以及非`q`的`vld1_s8_x4`缺失。

最终由用户在第三方源码的`ggml/src/ggml-cpu/ggml-cpu-impl.h`输入兼容代码：

- 只在非Clang、GCC主版本小于10的AArch64分支启用。
- `q`版依次读取4个16字节向量，总共64字节；非`q`版读取4个8字节向量，总共32字节。
- 不分配内存、不修改输入；调用者仍须保证输入区间可读，封装不会自动补足越界数据。
- 将源码直接使用的三个原始函数名映射到兼容封装，覆盖量化和repack两条编译路径。
- 新编译器保留原始接口；本次最终构建和板端推理通过，但未完成所有量化类型的数值对照或性能回归。

用户实际输入的完整补丁已存为[llama-cpp-gcc9-neon-x4.patch](patches/llama-cpp-gcc9-neon-x4.patch)。它仅记录第三方源码变更，没有修改Netflow Analyzer的C源码，也没有提交第三方仓库。记录时已对干净Git索引进行正向`--check --cached`检查，并对实际修改后的工作树进行反向`--reverse --check`检查，两项均通过；检查不应用或撤销补丁。

不能用关闭`-Werror`解决未声明的接口或错误的向量类型；同样，CPU具备指令能力不代表旧编译器头文件已经提供全部对应函数。

## 5. 在虚拟机复现交叉构建

以下命令使用Bash。已有成功的源码和构建目录不要重新初始化；复现时使用新的目录名。先固定提交、应用相同补丁，再配置CMake，避免升级上游后把旧补丁直接套上去。

```bash
NFA_LLM_SOURCE=/home/zcb/workspace/third_party/llama.cpp-nfa-cpu-v1
NFA_LLM_BUILD=/home/zcb/build/llama-cpu-arm64-sdk-v1
NFA_LLM_REVISION=5fc4f3c8c7103ffd0b7ff5ee4855bcc78a3ed5cd
NFA_LLM_PATCH=/home/zcb/workspace/netflow-analyzer/docs/patches/llama-cpp-gcc9-neon-x4.patch
```

仅在新的、尚不存在的源码目录中执行这一组：

```bash
(
    set -eu
    test ! -e "$NFA_LLM_SOURCE"
    mkdir -p /home/zcb/workspace/third_party
    git init "$NFA_LLM_SOURCE"
    git -C "$NFA_LLM_SOURCE" remote add origin \
        https://github.com/ggml-org/llama.cpp.git
    git -C "$NFA_LLM_SOURCE" fetch --depth=1 origin "$NFA_LLM_REVISION"
    git -C "$NFA_LLM_SOURCE" checkout --detach "$NFA_LLM_REVISION"
    git -C "$NFA_LLM_SOURCE" apply --check "$NFA_LLM_PATCH"
    git -C "$NFA_LLM_SOURCE" apply "$NFA_LLM_PATCH"
)
```

`git -C`指定仓库目录，无需改变当前终端目录；`--depth=1`减少下载历史，固定提交控制本次输入。`apply --check`先确认补丁可用，下一条才真正改源码。已经应用过补丁的成功目录不应再执行一次。

构建配置如下；如果已有构建目录，只在确认它属于同一工具链和配置后继续构建，否则换新目录。不要清空未知目录或混用不同编译器的CMake缓存。

```bash
(
    set -eu
    NFA_LLM_TOOLCHAIN=/home/zcb/sdk/lubancat-rk356x-20260623/prebuilts/gcc/linux-x86/aarch64/gcc-buildroot-9.3.0-2020.03-x86_64_aarch64-rockchip-linux-gnu
    export LD_LIBRARY_PATH="$NFA_LLM_TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    NFA_LLM_CC="$NFA_LLM_TOOLCHAIN/bin/aarch64-rockchip-linux-gnu-gcc"
    NFA_LLM_CXX="$NFA_LLM_TOOLCHAIN/bin/aarch64-rockchip-linux-gnu-g++"
    NFA_LLM_SYSROOT="$("$NFA_LLM_CC" -print-sysroot)"
    test -x "$NFA_LLM_CC"
    test -x "$NFA_LLM_CXX"
    test -d "$NFA_LLM_SYSROOT"

    cmake -S "$NFA_LLM_SOURCE" -B "$NFA_LLM_BUILD" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_C_COMPILER="$NFA_LLM_CC" \
        -DCMAKE_CXX_COMPILER="$NFA_LLM_CXX" \
        -DCMAKE_FIND_ROOT_PATH="$NFA_LLM_SYSROOT" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
        -DBUILD_SHARED_LIBS=OFF \
        -DGGML_NATIVE=OFF \
        -DGGML_CPU_ARM_ARCH=armv8.2-a+fp16+dotprod \
        -DGGML_CPU_KLEIDIAI=OFF \
        -DGGML_OPENMP=OFF \
        -DGGML_BLAS=OFF \
        -DGGML_LLAMAFILE=OFF \
        -DGGML_CUDA=OFF \
        -DGGML_VULKAN=OFF \
        -DLLAMA_BUILD_COMMON=ON \
        -DLLAMA_BUILD_TOOLS=ON \
        -DLLAMA_BUILD_TESTS=OFF \
        -DLLAMA_BUILD_EXAMPLES=OFF \
        -DLLAMA_BUILD_SERVER=OFF \
        -DLLAMA_BUILD_APP=OFF \
        -DLLAMA_OPENSSL=OFF \
        -DLLAMA_SUBPROCESS=OFF

    cmake --build "$NFA_LLM_BUILD" --target llama-completion -j2
)
```

重要语法和参数：

| 写法 | 意义与本次理由 |
|---|---|
| `NAME=value` | 在当前Shell定义变量，等号两侧不能有空格；新终端不会自动继承未导出的变量 |
| `"$NAME"` | 取变量的值并保持为一个参数；未定义或空值会使`-B`等参数失效 |
| `$(命令)` | 取得命令的标准输出；这里从选定编译器取得自身sysroot |
| `( ... )` | 在子Shell执行；临时的`LD_LIBRARY_PATH`不会污染外层终端 |
| `${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}` | 原变量有值时才追加冒号和旧值，避免无意产生空路径项 |
| `export LD_LIBRARY_PATH=...` | 让VM上的编译器子进程找到旧SDK的宿主依赖，如`libisl.so.15`；不是向板端复制这个变量 |
| `-S`／`-B`／`-G Ninja` | 分别选择源码、独立构建目录和构建工具；反斜杠续行时，其后不能再放空格 |
| `-D名称=值` | 设置CMake缓存变量，不是直接传给C编译器的`#define` |
| `SYSTEM_NAME`／`PROCESSOR`／编译器路径 | 声明目标Linux ARM64并实际选用SDK编译器；只写目标处理器名称不会把宿主GCC变成交叉编译器 |
| `FIND_ROOT_PATH`及四个查找模式 | 编译工具仍在VM运行，库／头／包查找限制到目标根；SDK编译器自身使用内建sysroot |
| `GGML_NATIVE=OFF`与ARM架构 | 不照搬VM的x86本机指令，明确使用板端支持的ARMv8.2、FP16和dotprod配置 |
| `BUILD_SHARED_LIBS=OFF`与两个静态运行库参数 | 把项目库和C++／GCC运行库链接进可执行文件；glibc等系统库仍动态加载，不是完全静态程序 |
| CPU与工具裁剪开关 | 不引入GPU、额外数学库、服务器、界面或HTTPS依赖；本步只需要本地GGUF单次生成 |
| `GGML_OPENMP=OFF` | 不使用OpenMP；llama.cpp自己的线程池仍可工作，并不等于单线程 |
| `--target llama-completion -j2` | 只构建本次工具，同时最多两个编译任务；与运行时`--threads 2`不是同一设置 |

本次没有复用Netflow Analyzer的libpcap overlay，因为llama.cpp不需要libpcap。官方SDK与通用sysroot的基础区别见[交叉编译手册](cross_compilation.md)。

## 6. 检查产物并传输到板端

虚拟机只读检查：

```bash
NFA_LLM_VM_BIN=/home/zcb/build/llama-cpu-arm64-sdk-v1/bin/llama-completion
file "$NFA_LLM_VM_BIN"
readelf -d "$NFA_LLM_VM_BIN" | grep NEEDED
readelf --version-info "$NFA_LLM_VM_BIN" | \
    grep -oE 'GLIBC_[0-9.]+' | sort -Vu
sha256sum "$NFA_LLM_VM_BIN"
```

实际产物为ARM64 ELF，解释器`/lib/ld-linux-aarch64.so.1`；依赖`libdl.so.2`、`libpthread.so.0`、`libm.so.6`和`libc.so.6`。它要求的GLIBC符号版本为2.17、2.18、2.27和2.29，未出现此前通用错误产物的`GLIBC_2.34`；板端glibc 2.31随后实际加载并完成推理。

`file`中的`with debug_info, not stripped`描述二进制仍有调试信息，不表示CPU推理配置错误，也不能把这份文件称为已经strip的最小产物。这里不要为了缩小体积改变已记录的验收文件，否则SHA-256也会改变。

传输时使用新文件名，并在板端先确认目标不存在，再从VM执行：

```bash
ssh cat@192.168.1.102 \
    'test ! -e /home/cat/bin/llama-completion-nfa-cpu-v1.part && test ! -e /home/cat/bin/llama-completion-nfa-cpu-v1 && mkdir -p /home/cat/bin'
```

只有上条返回0才继续；现有已成功产物不要重复覆盖：

```bash
scp "$NFA_LLM_VM_BIN" \
    cat@192.168.1.102:/home/cat/bin/llama-completion-nfa-cpu-v1.part
```

板端执行以下组；校验不通过或正式文件存在时立即停止，保留临时文件排查：

```bash
(
    set -eu
    NFA_LLM_PART=/home/cat/bin/llama-completion-nfa-cpu-v1.part
    NFA_LLM_BIN=/home/cat/bin/llama-completion-nfa-cpu-v1
    NFA_LLM_EXPECTED_SHA=f46b85605e0308481beb9f652bccffe0fee8cea4178a89e7a7b14af28172943a
    test ! -e "$NFA_LLM_BIN"
    printf '%s  %s\n' "$NFA_LLM_EXPECTED_SHA" "$NFA_LLM_PART" | sha256sum --check
    chmod 0755 "$NFA_LLM_PART"
    mv -- "$NFA_LLM_PART" "$NFA_LLM_BIN"
)
```

`scp`负责传字节；`sha256sum --check`再确认一致；`chmod 0755`给eMMC上文件执行权限；最后`mv`让完整且已验证的文件成为正式入口。模型传输采用同样的临时文件校验思想，但SD上的模型只是数据，不需要添加执行位。

## 7. 板端有界首次运行

本次是手工单次进程，不配置systemd、不执行`enable`，也不会退出后继续常驻。命令外层用180秒时限；到时发送TERM，再过5秒仍未退出才KILL。成功生成可以提前结束，本次14.45秒便结束。

```bash
(
    set -eu
    set -o pipefail
    NFA_LLM_BIN=/home/cat/bin/llama-completion-nfa-cpu-v1
    NFA_LLM_MODEL=/media/usb0/netflow-analyzer-data/models/qwen2.5-0.5b-instruct-q4_k_m.gguf
    test -x "$NFA_LLM_BIN"
    test -r "$NFA_LLM_MODEL"
    test -x /usr/bin/time
    command -v timeout >/dev/null
    NFA_LLM_AVAILABLE_KB="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)"
    test -n "$NFA_LLM_AVAILABLE_KB"
    if test "$NFA_LLM_AVAILABLE_KB" -lt 819200; then
        printf '可用内存不足800 MiB，请先关闭不需要的应用并重新检查。\n'
        exit 1
    fi

    mkdir -p /media/usb0/netflow-analyzer-data/logs
    NFA_LLM_RUN_DIR="/media/usb0/netflow-analyzer-data/logs/llm-first-run-$(date '+%Y%m%d-%H%M%S')"
    mkdir "$NFA_LLM_RUN_DIR"
    set +e
    /usr/bin/time -v \
        timeout --signal=TERM --kill-after=5s 180s \
        "$NFA_LLM_BIN" \
        --model "$NFA_LLM_MODEL" \
        --ctx-size 512 \
        --threads 2 \
        --threads-batch 2 \
        --batch-size 64 \
        --ubatch-size 64 \
        --n-predict 64 \
        --gpu-layers 0 \
        --temp 0 \
        --single-turn \
        --simple-io \
        --no-display-prompt \
        --no-warmup \
        --prompt '请用中文一句话解释什么是网络流，不超过40个汉字。' \
        </dev/null 2>&1 | tee "$NFA_LLM_RUN_DIR/run.txt"
    NFA_LLM_STATUS=${PIPESTATUS[0]}
    printf '\n推理退出码：%s\n日志目录：%s\n' "$NFA_LLM_STATUS" "$NFA_LLM_RUN_DIR"
    exit "$NFA_LLM_STATUS"
)
```

这是一份带检查的复现命令；记录时没有再次在板上运行。首次实测目录为`/media/usb0/netflow-analyzer-data/logs/llm-first-run-20261002-155819`。

| 参数／Shell操作 | 含义 |
|---|---|
| `ctx-size 512` | 本次上下文窗口最多512 token，输入、聊天模板和输出共同受窗口约束 |
| `threads 2`／`threads-batch 2` | 设置生成与提示词批量计算的CPU线程数，不表示整机只使用两个核心 |
| `batch-size 64`／`ubatch-size 64` | 提示词处理的逻辑批和物理微批大小，不是一次输出64个汉字 |
| `n-predict 64` | 新生成token的上限；模型可遇到结束标记提前停止，token不等于字符 |
| `gpu-layers 0` | 请求零GPU卸载层；本次CPU-only构建提示此选项被忽略是预期行为 |
| `temp 0` | 使用贪心选择，降低采样随机性，不保证语义正确或跨版本逐字一致 |
| `single-turn` | 一轮回答后结束，不进入持续对话 |
| `simple-io`／`no-display-prompt` | 简化终端交互、不回显完整输入；运行日志和警告仍会输出 |
| `no-warmup` | 跳过启动预热；这里测首次启动，不把它当稳态吞吐基准 |
| `</dev/null` | 不等待交互式终端输入 |
| `2>&1` | 把stderr日志和time统计一起送入后面的管道 |
| `tee run.txt` | 在屏幕显示的同时保存本次日志，并不会启动后台服务 |
| `set -o pipefail` | 避免前段失败被最后成功的`tee`掩盖 |
| `set +e`与`PIPESTATUS[0]` | 允许失败后继续报告；立即保存time/timeout一侧的退出码，不能先执行别的命令再取 |

800 MiB门槛是保守的启动检查，不是内存硬限制或安全证明。时限同样不限制峰值内存；在无swap的2 GiB设备上，后台应用和并发模型进程仍可触发OOM。失败时区分超时退出124、KILL相关137及内核OOM证据，不把所有非零码都归因于模型。

## 8. 实测结果与警告解释

| 指标 | 首次实测 |
|---|---:|
| 模型加载计时 | 3461.41 ms |
| 提示词计算 | 3438.66 ms／23 token，6.69 token/s |
| 自回归生成 | 2091.62 ms／11 runs，5.26 token/s |
| 内部`total time` | 5600.20 ms／34 token |
| 整进程墙钟时间 | 14.45 s |
| 用户／系统CPU时间 | 15.94 s／0.98 s |
| 进程平均CPU | 117% |
| 最大RSS | 703896 KiB，约687.4 MiB |
| swap计数 | 0；系统原本未配置swap |
| 退出码 | 0 |

117%约等于平均占用1.17个逻辑核心，不是占用了四核整机的117%。它包括提示词计算、生成等整个进程的平均CPU成本；实际整机负载还包括桌面、SSH等其他进程。内部`total time`与外部墙钟14.45秒采用不同测量范围，不能把两者直接相减就声称已定位某段耗时。

峰值RSS可能包括驻留的映射权重页和计算缓冲区，不等于模型文件大小，也不是全部匿名堆内存。一轮短运行不能证明长期无泄漏、稳定吞吐或与抓包并发时仍零drop。输出中的文件I/O计数为0也不能据此说没有读取SD上的模型。

三类警告分别处理：

1. **没有可用GPU**：本次只构建CPU后端，零GPU层选项没有实际作用；不表示ARM64运行失败，后续不会仅为消除警告启用GPU/NPU。
2. **`'</s>'` token类型被覆盖**：加载器发现控制样式token的元数据类型不符合预期，进行了覆盖；本次随后成功结束。保留警告和文件指纹，不把它直接认定为下载损坏，也不以一次成功证明所有token语义正确。
3. **提示词可能应放入system role**：该固定版本在有用户prompt而无自定义system prompt时给出通用提示。工具源码仍会把本次问题作为用户消息加入聊天模板；不必为了消除警告把用户问题移到system role。未来可另设system prompt约束“只基于提供的事实、证据不足时说不足”，但约束效果仍需测试。

网络流在本项目中的准确含义：根据协议及规范化的双方IP／端口，把相关数据包聚合成一条双向统计记录，并按配置的生命周期边界区分后续记录。模型这次的“数学模型”回答未准确解释这一含义。

## 9. 验收边界与下一步

2026-10-02虚拟机已有用户工作区配置通过`cmake --build build`及36/36项CTest；该数量包含尚未提交的模型产物测试，不能写成已发布版本或板端的36项基线。语言模型只完成上述手工冒烟验收，没有加入CTest；此前ARM64网络分析器18项基线也不能因此自动升级。

下一步先使用固定、简短、可人工判断的网络摘要，分别测试正常行为、异常候选和证据不足三类输入，保留原始输出、错误说明、耗时及RSS。不要先增加剪枝、模型规模或常驻API服务器。

后续拟议链路是：C采集／解析／流聚合 → 主机与时间窗证据摘要 → 规则或受限模型挑选事件 → 有界解释任务 → 独立LLM进程输出解释。这是计划，不是已实现架构；不能每个数据包都同步调用约5 token/s的模型。

既有18维TCP特征已出现正常和训练恶意输入相同的案例；只把相同18维数字换成自然语言交给LLM，并不能凭空补出可区分的信息。必须先检查新增证据的来源与覆盖，再评价模型解释。IP可以作为主机分组键，不能单凭某个IP判恶意。

语言模型输出不作为事实来源，不直接执行Shell、修改防火墙或做自动封禁；用户输入和报文负载也不能成为可执行指令。正式接入前仍需要质量测试、并发资源与抓包drop复测、超时／内存限制和输出协议。

相关记录：[TD-043](technical_decisions.md#td-043先用独立有界cpu进程验证端侧语言模型)、[问题记录第7节](problem_log.md#7-端侧语言模型部署)、[正常TCP诊断](tcp_normal_diagnostic.md)、[交叉编译手册](cross_compilation.md)。
