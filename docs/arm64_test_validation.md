# LubanCat-2N ARM64完整37项测试验收

验收日期：2026-10-12。板端结果来自用户实际终端输出，助手没有登录开发板代跑。本文记录功能回归和环境处理，不代表新增在线AI、性能压力或systemd部署验收。

## 1. 结论与源码状态

LubanCat-2N原生Debug构建中，同一套37项CTest全部通过，总耗时94.41秒，退出码0。测试包括33项C／普通Python测试及4项可选PyTorch测试：

- `tcp_tensor_dataset_tests`；
- `tcp_feature_preprocessing_tests`；
- `tcp_linear_baseline_tests`；
- `tcp_model_artifact_tests`。

验收时分支为`feature/flow-features`，HEAD为`3ad8a49 docs(demo): record ARM64 TCP feature validation`，工作区只有`M CMakeLists.txt`。准确表述是“基于3ad8a49加线性测试超时调整通过”，不是未修改的3ad8a49原样通过。

后续用户已提交并推送`4a6d8de test(ml): adjust baseline timeout for ARM64`，虚拟机也已同步该提交。本轮核对实际差异：两处重复的`TIMEOUT 20`均改为60，尚未合并为一处。构建树只注册一项线性基线测试，实际`TIMEOUT`为60；重复赋值不会让测试执行两次，也不会累计成120秒。此前文档草稿写成“已合并”缺少差异依据，本次更正，不改写源码。虚拟机在`4a6d8de`重新构建成功，37/37项x86_64 Debug CTest通过，耗时7.53秒、退出0；不是重新运行板端测试。

最终日志位于开发板，不在Git仓库中：

```text
/home/cat/build/netflow-analyzer-arm64-verify/ctest-arm64-37-timeout60.nxTPrF

100% tests passed, 0 tests failed out of 37
Total Test time (real) = 94.41 sec
CTest退出码：0
```

用户提供的是最终汇总、退出码和Git状态，没有贴出最终37项逐项耗时或日志SHA-256；不把首次运行或独立诊断的耗时冒充最终逐项耗时。首次失败日志和单项计时也应保留。

## 2. 板端环境与存储布局

| 项目 | 本次环境 |
|---|---|
| 开发板／架构 | LubanCat-2N，`aarch64` |
| Python | 系统Python 3.8.10；ML环境沿用该解释器 |
| CMake／CTest | 3.16.3 |
| libpcap | pkg-config报告1.9.1 |
| ML直接依赖 | NumPy 1.24.4、PyTorch 2.4.1，CPU运行 |
| 虚拟环境pip | 20.0.2 |
| 源码 | `/home/cat/workspace/netflow-analyzer` |
| 原生Debug构建 | `/home/cat/build/netflow-analyzer-arm64-verify` |
| ML虚拟环境 | `/home/cat/venvs/nfa-ml-py38`，实际471 MiB |
| wheel目录 | `/media/usb0/netflow-analyzer-data/wheels/nfa-arm64-py38-wheels.kjjqoc` |

虚拟机已有的项目ML环境为Python 3.12.3／NumPy 2.5.3／PyTorch 2.14.0+cpu。这里运行同一套测试，但不是相同依赖版本，也未验证两端公开数据训练结果逐位相等。板端兼容版本只用于此次受控测试，不改写面向虚拟机的`requirements-ml.txt`，不作为长期生产依赖或不可信模型文件的加载环境。

源码、构建和虚拟环境位于eMMC的ext4文件系统。SD卡为VFAT、带`noexec`，只保存wheel、备份和数据；不能直接复制虚拟机的`.venv`到板端，也不能将正常venv／VS Code Server直接搬到该VFAT数据盘。

## 3. 安装包准备与环境修复

### 3.1 在虚拟机下载板端wheel

本次下载目录为`/home/zcb/nfa-arm64-py38-wheels.kjjqoc`。下面是复现模板；新建目录的随机后缀会变化，传输后必须用实际目录名设置板端变量，不需要为已通过的验收再次安装。

```bash
NFA_ARM_WHEELS="$(mktemp -d /home/zcb/nfa-arm64-py38-wheels.XXXXXX)"

/home/zcb/workspace/netflow-analyzer/.venv/bin/python -m pip download \
    --index-url https://pypi.org/simple \
    --dest "$NFA_ARM_WHEELS" \
    --platform manylinux2014_aarch64 \
    --python-version 38 \
    --implementation cp \
    --abi cp38 \
    --only-binary=:all: \
    --no-deps \
    torch==2.4.1 \
    numpy==1.24.4 \
    filelock==3.16.1 \
    typing-extensions==4.12.2 \
    sympy==1.13.3 \
    mpmath==1.3.0 \
    networkx==3.1 \
    jinja2==3.1.6 \
    MarkupSafe==2.1.5 \
    fsspec==2024.10.0 \
    setuptools==75.3.2

(
    cd "$NFA_ARM_WHEELS" || exit 1
    sha256sum ./*.whl > SHA256SUMS
)
```

`download`只下载，不往虚拟机环境安装。`aarch64`和`cp38`选择板端架构与Python ABI；`--only-binary`拒绝源码构建；`--no-deps`禁止按宿主机条件递归选择依赖，上面已经明确列出所需11个包。ARM64 wheel不是虚拟机x86_64可运行的包。

本次11个wheel共109 MiB；读取ZIP条目得到解压后425.7 MiB，其中torch约339.9 MiB、NumPy约46.2 MiB。实际venv占471 MiB，压缩包大小及ZIP条目大小都不能当作最终安装空间的硬上限。

先在板端确认`/media/usb0`确为挂载点并创建`netflow-analyzer-data/wheels`目录，再从虚拟机执行：

```bash
scp -r "$NFA_ARM_WHEELS" \
    cat@192.168.1.102:/media/usb0/netflow-analyzer-data/wheels/
```

板端在实际wheel目录中执行`sha256sum -c SHA256SUMS`，本次11个包全部为`OK`。这是传输一致性检查，不代替发布者身份或软件供应链审计。发布文件来源见[PyTorch 2.4.1](https://pypi.org/project/torch/2.4.1/#files)与[NumPy 1.24.4](https://pypi.org/project/numpy/1.24.4/#files)。

### 3.2 修复ensurepip并创建独立环境

第一次`python3 -m venv`报`ensurepip is not available`，目录中已经有解释器，但`-m pip`报`No module named pip`。Ubuntu缺少`python3.8-venv`，不是wheel损坏。

在板端预览安装计划，确认没有不期望的删除／大范围升级后补齐组件：

```bash
sudo apt-get --simulate --no-install-recommends install python3.8-venv
sudo apt-get --no-install-recommends install python3.8-venv

NFA_BOARD_VENV=/home/cat/venvs/nfa-ml-py38
python3 -m venv "$NFA_BOARD_VENV"
"$NFA_BOARD_VENV/bin/python" -m pip --version
```

失败目录可以复用，无需删除或加`--clear`。只安装系统venv组件使用sudo；venv创建和pip安装使用普通用户，不升级系统Python，不使用`sudo pip`。本次重新创建后pip为20.0.2。虚拟环境原理见[Python 3.8 venv文档](https://docs.python.org/3.8/library/venv.html)。

### 3.3 离线安装与空间复查

以下变量是本次实际路径；新复现必须调整wheel目录。安装前确认SD卡挂载、目录可写并复查eMMC余量；任一步失败均停止，不继续运行依赖于其成功的命令。

```bash
NFA_BOARD_VENV=/home/cat/venvs/nfa-ml-py38
NFA_BOARD_WHEELS=/media/usb0/netflow-analyzer-data/wheels/nfa-arm64-py38-wheels.kjjqoc

mountpoint /media/usb0
df -hT /home/cat

NFA_PIP_TMP="$(mktemp -d "$NFA_BOARD_WHEELS/pip-tmp.XXXXXX")"

TMPDIR="$NFA_PIP_TMP" \
PYTHONDONTWRITEBYTECODE=1 \
"$NFA_BOARD_VENV/bin/python" -m pip install \
    --no-index \
    --find-links "$NFA_BOARD_WHEELS" \
    --only-binary=:all: \
    --no-cache-dir \
    --no-compile \
    torch==2.4.1 \
    numpy==1.24.4 \
    setuptools==75.3.2

PYTHONDONTWRITEBYTECODE=1 \
"$NFA_BOARD_VENV/bin/python" -m pip check

du -sh "$NFA_BOARD_VENV"
df -hT /home/cat
```

`TMPDIR`将临时文件放SD；`--no-index`不联网，`--find-links`限定本地目录，`--no-cache-dir`不保留pip缓存，`--no-compile`不生成安装期字节码。`PYTHONDONTWRITEBYTECODE`限制运行时字节码写入。这些设置不把最终共享库移到SD，也不是运行内存上限。

安装后`pip check`通过，但根盘只余268 MiB、97%使用率，因此暂停继续验收并检查容量。`.vscode-server`共2.7 GiB；两个版本均有活动进程，不能简单只保留最新一个。保留`645f29…`和`07f806…`，按步骤备份并清理`04c0d9…`和`a44adf…`后，用户提供根盘剩余1.5 GiB、78%使用率，`.vscode-server`降至1.5 GiB。完整事故与证据边界见[问题5.19](problem_log.md#519-长期服务日志与多个vs-code-server版本共同写满目标板根分区)。不清理源码、测试日志、虚拟环境或整个服务器目录。

## 4. 超时诊断与唯一测试配置改动

首次完整运行结果为36/37，总耗时84.60秒、退出8。唯一失败是`tcp_linear_baseline_tests`在20.08秒超时；其他三项ML测试为16.07、16.67和17.13秒。原始日志：

```text
/home/cat/build/netflow-analyzer-arm64-verify/ctest-arm64-37.sQ8MWP
```

直接运行原线性测试，保留同样的单线程环境，外层使用90秒诊断保护并以`/usr/bin/time -v`计时。得到：

| 指标 | 独立诊断结果 |
|---|---:|
| 功能输出 | `[PASS] TCP linear baseline training and metrics` |
| 墙钟耗时 | 29.33秒 |
| 用户／系统CPU时间 | 27.97／1.03秒 |
| 最大RSS | 252008 KiB，约246.1 MiB |
| 退出码 | 0 |

诊断日志：`/home/cat/build/netflow-analyzer-arm64-verify/linear-baseline-timing.NZUYVm`。这项测试使用4条合成样本，验证两次默认训练的可重复性、类别权重、指标和非法输入；不是公开数据集全量训练。当前未细分依赖导入、优化器初始化和训练各自开销，不把29.33秒当作单次模型推理时延。

根据功能通过而总耗时超过原预算的证据，用户在`CMakeLists.txt`中将两段线性测试超时设置都改为60秒。`4a6d8de`中的实际配置为：

```cmake
        set_tests_properties(
            tcp_linear_baseline_tests
            PROPERTIES TIMEOUT 60
        )

        set_tests_properties(
            tcp_linear_baseline_tests
            PROPERTIES TIMEOUT 60
        )
```

60秒约为本次实测耗时的两倍，保留防卡死的有限预算；并非减少训练轮数、放宽断言或移除失败测试，也不是性能优化。两段同值设置仍属冗余，后续可以合并，但本次不为文档收尾增加源码改动。若只改第一段而留下后一段20秒，后者仍会覆盖前者。修改后必须重新执行CMake生成，不能直接改构建树中的`CTestTestfile.cmake`。本轮只读核对生成配置，确认线性测试注册一次且超时为60。属性语义见[CMake 3.16 TIMEOUT文档](https://cmake.org/cmake/help/v3.16/prop_test/TIMEOUT.html)。其他测试仍保持原预算。

## 5. 最终37项回归命令

在板端实际源码工作区完成上述调整后，以普通用户执行；这里是原生编译，不运行虚拟机的x86_64可执行文件或`BUILD_TESTING=OFF`的交叉部署产物。

```bash
NFA_BOARD_SOURCE=/home/cat/workspace/netflow-analyzer
NFA_BOARD_BUILD=/home/cat/build/netflow-analyzer-arm64-verify
NFA_BOARD_VENV=/home/cat/venvs/nfa-ml-py38

cmake \
    -S "$NFA_BOARD_SOURCE" \
    -B "$NFA_BOARD_BUILD" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DPython3_EXECUTABLE=/usr/bin/python3 \
    -DNFA_ML_PYTHON_EXECUTABLE:FILEPATH="$NFA_BOARD_VENV/bin/python"

cmake --build "$NFA_BOARD_BUILD" -j2
cmake -E chdir "$NFA_BOARD_BUILD" ctest -N
```

配置和构建必须成功，`ctest -N`应显示`Total Tests: 37`后才继续。`-S/-B`分别选择源码和独立构建目录；`BUILD_TESTING=ON`生成并注册测试；系统Python负责普通脚本测试，`NFA_ML_PYTHON_EXECUTABLE`显式启用4项PyTorch测试。

CTest 3.16.3使用`cmake -E chdir`进入构建目录，不依赖较新版本的`ctest --test-dir`。串行运行与日志保存：

```bash
NFA_TEST_LOG="$(mktemp "$NFA_BOARD_BUILD/ctest-arm64-37-timeout60.XXXXXX")"

(
    set -o pipefail
    export PYTHONDONTWRITEBYTECODE=1
    export OMP_NUM_THREADS=1
    export OPENBLAS_NUM_THREADS=1
    export MKL_NUM_THREADS=1

    cmake -E chdir "$NFA_BOARD_BUILD" \
        ctest --output-on-failure -j1 \
        2>&1 | tee "$NFA_TEST_LOG"
)

NFA_CTEST_STATUS=$?
printf 'CTest退出码：%s\n' "$NFA_CTEST_STATUS"
printf '测试日志：%s\n' "$NFA_TEST_LOG"

git -C "$NFA_BOARD_SOURCE" status --short --branch
git -C "$NFA_BOARD_SOURCE" log -1 --oneline
```

`-j1`限制CTest并行测试数，环境变量限制相关计算库线程；两者不是同一层并行开关。括号限定设置作用域，`2>&1`合并输出，`tee`同时显示与保存，`pipefail`避免CTest失败被tee成功掩盖。退出码必须立即保存，不能用稍后其他命令的`$?`追溯原测试结果。

## 6. 对外表述与后续动作

简历可更新为：“x86_64与LubanCat-2N ARM64原生Debug均通过同一套37项CTest，并完成真实ICMP／TCP双向统计与特征导出核对。”面试时说明依赖版本与线性测试预算适配，不隐去首次超时。

本次不证明：Release／ASan／UBSan全37项通过、持续集成已经建立、模型准确率或在线误报达标、完整DNS域名／应用层DPI、数天长稳、性能上限、当前systemd服务状态或之前TCP发生器尚未提供的退出状态。

板端CMake改动已由用户提交为`4a6d8de`，虚拟机同步后的37项回归也已完成；当前剩余动作是用户审查并提交验收与简历素材文档，再让板端快进拉取文档。不要在两个工作区独立复制出相同CMake提交造成分叉，不提交venv、wheel或构建目录，不自动打版本tag。原始日志继续保留在板端，必要时另行复制归档；本记录没有伪造日志文件指纹。

本次提交前还遇到Git身份未配置，`git commit`报`Please tell me who you are`，随后`git push`提示`Everything up-to-date`；这只能说明没有新提交可推送，不能代表改动已上传。提交姓名／邮箱与GitHub认证是两回事，可在当前仓库使用`git config --local user.name`和`git config --local user.email`配置实际身份，再依次提交、推送。完整记录见[问题5.23](problem_log.md#523-开发板git未配置提交身份导致提交失败)。用户最终采用的配置作用域未提供，不推断为全局或仓库级设置。
