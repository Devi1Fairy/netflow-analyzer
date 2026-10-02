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

### 8.1 三例人工网络摘要测试：进程成功，解释质量未通过

2026-10-02用户在板端串行运行三个独立进程，结果目录为`/media/usb0/netflow-analyzer-data/logs/llm-evidence-v1.WyFCvi`。这轮仍使用同一模型、二进制、512 token上下文、2个计算线程和180秒时限；新token上限为96，额外通过`--system-prompt`传入共享说明。输入是人工构造的摘要，不是C程序已实现的主机窗口输出，也没有生成真实扫描流量。

共享system prompt如下；归档文件为`system.txt`：

```text
只根据摘要判断。正常候选仅表示未见明显异常；异常候选不等于确认攻击。输出三行：判断：正常候选/异常候选/证据不足；依据：引用具体数字或事实；局限：还缺什么证据。不得编造IP、漏洞或攻击者身份，不输出执行命令。简短回答。
```

人工输入：

- `normal`：60秒内，主机A向同一服务的443端口建立4条TCP连接，全部完成握手并正常关闭，连接失败0次。过去1小时，每分钟有3到5条同类连接。
- `suspicious`：60秒内，主机A向同一目标尝试连接300个不同TCP端口，290次只有SYN且未完成握手。平时每分钟访问1到3个目的端口。未提供扫描授权信息。
- `insufficient`：60秒内，主机A发送100个TCP包。没有目的端口、握手结果、连接失败数或历史基线。

用户提供的实际输出保留如下，不修成期望答案：

```text
normal:
 [end of text]

suspicious:
异常候选/证据不足；依据：没有提供扫描授权信息；局限：还缺什么证据。 [end of text]

insufficient:
异常候选/证据不足；依据：没有历史基线；局限：还缺什么证据。 [end of text]
```

| 案例 | 退出码 | 墙钟时间 | 最大RSS | 提示词速率 | 生成速率 |
|---|---:|---:|---:|---:|---:|
| `normal` | 0 | 16.01秒 | 675912 KiB／约660.1 MiB | 不可用：0毫秒且打印`inf` | 不可用：0毫秒且打印`inf` |
| `suspicious` | 0 | 29.66秒 | 721168 KiB／约704.3 MiB | 7.40 token/s | 5.18 token/s |
| `insufficient` | 0 | 26.45秒 | 751784 KiB／约734.2 MiB | 7.34 token/s | 5.23 token/s |

人工验收结果：

- `normal`没有可见的实质回答，不能视为正确识别正常行为。
- `suspicious`提到了缺少授权，但没有引用300个端口、290次未完成握手或历史范围，也没有给出单一判断；“局限”复制了指令中的占位要求。
- `insufficient`注意到没有基线，但仍输出两个并列类别和占位式局限，没有清楚给出证据不足的单一判断。
- 按事先要求的单一判断、事实依据和具体局限，三例均未通过；这是三个人工样例的质量门槛，不是检测准确率、恶意召回率或整个模型的通用能力评估。

`normal`的计时需要独立诊断。固定上游源码中，`src/llama-context.cpp`的`perf_get_data()`把导出计数下限设为1；`common/sampling.cpp`的`common_perf_print()`用计时时间作分母计算速率，零时间可产生`inf`。所以“0毫秒／1 token”不是输入只有一个token的证据，`inf`也不是有效吞吐。工具在EOG生成结束路径打印`[end of text]`，该字符串是结束标记，不是模型提供的网络判断。零计时的运行时原因尚未定位，不能只凭摘要归咎于模型规模、下载、量化、兼容补丁或C采集代码。

本轮采用斜杠列出类别并把“还缺什么证据”写入模板；模型复制该模板可能与提示词写法及指令遵循有关，但此解释仍需对照实验。用户随后提供了`system.txt`、`normal-input.txt`和完整`normal-runtime.txt`，实际文本及参数已核对，详见第8.2节；接下来只改变system prompt做有界对照。不因退出0宣布质量通过，也不为获得期望答案删除失败记录或提前改变模型。

每例分别保存`<case>-input.txt`、`<case>-answer.txt`、`<case>-runtime.txt`和`<case>-exit.txt`。上表来自用户贴出的日志摘要，normal完整运行日志随后由用户贴出；助手没有在板端重跑或远程读取文件。三次是独立进程，峰值RSS逐例不同不能直接解释为同一进程持续泄漏；这次没有采集并行drop或长期稳定性测试。

### 8.2 normal完整日志复核与单变量对照

2026-10-02，用户提供的完整日志中，GNU time的`Command being timed`包含正确的system prompt和normal摘要，运行参数仍为512 token上下文、2线程、64批大小、96新token上限和180秒时限。进程退出0，墙钟16.01秒，没有触发超时；用户CPU时间22.39秒、系统CPU时间0.79秒，累计23.18秒，CPU占用144%。多线程累计CPU时间可以大于墙钟时间；内部零计时不表示实际没有计算。

固定源码的计数与结束行为还表明：

- `--n-predict 96`是生成上限，不是最低输出长度；非交互路径遇到EOG可以提前结束并打印`[end of text]`。
- `tools/completion/completion.cpp`也会把输入token传给`common_sampler_accept()`，`src/llama-sampler.cpp`的接受计数随之增加。因此`samplers time ... / 128 tokens`包含输入token，不能解释为生成了128个token。
- 日志把8887.21毫秒列为100%的未归类时间；这是计时归类异常的证据，不能拿零时间或`inf`计算有效吞吐。源码采样路径包含`llama_synchronize()`，因此也不能未经验证就认定“第一个结束token使同步未执行”是零计时原因。

目前可以排除传入文本遗漏和进程超时，但空回答及计时异常的具体原因尚未确定。随后只重跑normal，提供的命令保持原摘要、二进制、模型和推理参数不变，将system prompt简化为一句话说明任务，移除三行模板、斜杠类别和占位式局限。对照命令使用新的`llm-normal-prompt-v2.XXXXXX`目录保存文件，不覆盖旧结果；用户已返回本轮回答和资源日志片段，详见第8.3节。回答非空不等于完整三例验收或异常检测能力通过。

### 8.3 简化提示词后生成非空回答，但判断违背normal输入

2026-10-02，用户执行normal提示词对照并返回退出码0。此轮提供的system prompt为：

```text
你是网络流量分析助手。仅依据用户摘要，用中文一句话说明是否发现明显异常，并引用至少一个数字。不编造信息，也不把可疑直接认定为攻击。
```

normal摘要仍为60秒4条同服务443端口TCP连接、全部完成握手并正常关闭、失败0次、历史每分钟3到5条。用户返回的原始回答：

```text
发现明显异常，主机A的流量中存在大量异常连接，且连接数量和时间分布与正常情况不符。 [end of text]
```

资源数据：提示词计算13240.13毫秒／99 tokens（7.48 token/s），生成4788.85毫秒／25 runs（5.22 token/s），未归类时间7.57毫秒／0.0%；整进程24.00秒，用户CPU时间41.31秒、系统CPU时间0.61秒，平均CPU174%，最大RSS751964 KiB（约734.3 MiB），退出0。174%约表示平均使用1.74个核，不是四核整机超出100%。本轮计时恢复为非零，但没有独立定位或修复上轮零计时的根因。

验收结论：非空生成通过，事实遵循和判断质量仍失败。4条连接处于历史3到5条范围，摘要没有给出异常时间分布；回答没有引用任何数字，却虚构“大量异常连接”和分布不符。正常候选只表示现有摘要未见明显异常，不表示已经证明绝对安全；这个案例不能转换成真实流量误报率。新的具体目录名与完整命令／运行日志尚未由用户贴出，本节仅记录已提供的回答和日志片段，不声称助手在板端重跑或校验了全部文件。

随后固定诊断只要求从同一normal摘要提取连接数、失败数和历史范围，不再要求异常判断。它用于区分事实抽取与网络判断的问题，并非针对本例不断调提示词获取期望答案；用户已执行，结果见第8.4节。下一步建立同GGUF、固定源码的x86_64参考对照，不直接换模型或认定补丁错误。即使抽取正确，也必须保留本次失败，不能当作解释质量已经通过或接入在线告警的依据。

### 8.4 固定事实抽取复述正确，但遗漏失败次数

2026-10-02，用户返回的实际输入与原normal摘要完全一致。事实抽取命令的system prompt为：

```text
只从用户摘要中提取当前TCP连接数量、连接失败次数和历史每分钟连接数量范围。用一句中文复述，不判断异常，不补充摘要没有的事实。
```

用户返回退出码0与原始回答：

```text
主机A在60秒内建立了4条TCP连接，全部完成握手并正常关闭。过去1小时，每分钟有3到5条同类连接。 [end of text]
```

验收结果：连接数4与历史范围每分钟3到5条均复述正确，握手／关闭描述也与输入一致，没有再次虚构异常。但是预先要求的第三项“连接失败0次”没有明确输出，严格完整性验收仍未通过。不能在看到结果后删掉失败次数要求，也不能因“全部完成握手并正常关闭”就认定已经显式抽取失败次数；这一轮是局部事实复述正确，不是此前异常判断错误已解决。

资源数据：提示词计算13142.75毫秒／99 tokens（7.53 token/s），生成6455.91毫秒／34 runs（5.27 token/s），未归类时间7.12毫秒／0.0%；墙钟25.86秒，用户CPU时间44.87秒、系统CPU时间0.59秒，CPU175%，最大RSS748108 KiB（约730.6 MiB），退出0。实际日志目录为`/media/usb0/netflow-analyzer-data/logs/llm-normal-facts-v1.kXkUhp`；仍是一个独立有界进程，没有常驻服务或在线数据接入。数据来自用户贴出的输入、回答和日志片段，助手未在板端重跑。

下一步先在虚拟机新目录`/home/zcb/build/llama-cpu-x86-ref-v1`构建原生CPU参考产物，固定同一上游提交，不混用ARM64 SDK缓存。现有GCC 9补丁位于ARM专用分支，x86_64不执行该兼容分支；不要撤销用户补丁或修改成功的ARM构建。随后用同SHA-256的GGUF、保存的相同prompt和相同推理参数比较异常判断与事实抽取两项任务，保留所有输出。参考构建和推理尚未执行，不能提前记录通过；两平台输出相同不能证明全部算子正确，输出不同也可能受数值路径影响，不能单凭文本差异认定ARM补丁错误。

## 9. 验收边界与下一步

2026-10-02虚拟机已有用户工作区配置通过`cmake --build build`及36/36项CTest；该数量包含尚未提交的模型产物测试，不能写成已发布版本或板端的36项基线。语言模型只完成上述手工冒烟验收，没有加入CTest；此前ARM64网络分析器18项基线也不能因此自动升级。

三例固定网络摘要现已执行，进程均退出0，但解释质量没有通过，详见第8.1节。normal简化system prompt后的对照虚构异常；固定事实抽取随后正确复述连接数与历史范围，却遗漏失败次数，严格完整性验收仍未通过，详见第8.2～8.4节。下一步先建立固定源码的x86_64参考产物，再用同GGUF、prompt和参数作受控对照，不以反复改提示词或挑选回答替代验收；不要先增加剪枝、模型规模或常驻API服务器。

后续拟议链路是：C采集／解析／流聚合 → 主机与时间窗证据摘要 → 规则或受限模型挑选事件 → 有界解释任务 → 独立LLM进程输出解释。这是计划，不是已实现架构；不能每个数据包都同步调用约5 token/s的模型。

既有18维TCP特征已出现正常和训练恶意输入相同的案例；只把相同18维数字换成自然语言交给LLM，并不能凭空补出可区分的信息。必须先检查新增证据的来源与覆盖，再评价模型解释。IP可以作为主机分组键，不能单凭某个IP判恶意。

语言模型输出不作为事实来源，不直接执行Shell、修改防火墙或做自动封禁；用户输入和报文负载也不能成为可执行指令。正式接入前仍需要质量测试、并发资源与抓包drop复测、超时／内存限制和输出协议。

相关记录：[TD-043](technical_decisions.md#td-043先用独立有界cpu进程验证端侧语言模型)、[问题记录第7节](problem_log.md#7-端侧语言模型部署)、[正常TCP诊断](tcp_normal_diagnostic.md)、[交叉编译手册](cross_compilation.md)。
