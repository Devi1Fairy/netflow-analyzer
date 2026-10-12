# TCP模型ARM64真实样本推理验收

验收日期：2026-10-12。板端结果来自用户在LubanCat-2N实际运行后提供的日志，助手没有通过远程连接代跑。本次确认的是“PC训练和保存参数→板端加载→真实样本预测与PC对照”，不是检测效果验收或实时告警上线。

最新结果：在首次50条验收后，已扩展到既有候选全集63775条，板端分类与PC逐条一致；整进程17.79秒、峰值RSS约246.2 MiB，外层退出0。全集包含原训练样本，不是新增独立测试集；验证侧仍恒判恶意，不能解释为检测准确率100%。全量来源、原始指标和计时范围见[第8节](#8-全量63775条扩展验收)。

第1～6节保留首次50条的历史记录，第7节使用最新对外表述，第8节记录后续全量验收。两次运行的时间和内存不能混写。

## 1. 首次50条完成了什么

在PC上按既有配方训练TCP线性二分类模型，保存模型权重、训练侧预处理参数和输入契约；随后在鲁班猫2N上加载这些参数，对50条真实TCP候选进行推理。板端不重新训练，也不使用这50条输入重新拟合均值和尺度。

结果：50条分类结果与PC完全一致；预处理最大绝对差为0，logit最大绝对差约为`9.54e-7`，满足预先保存的浮点容差。进程正常结束，`/usr/bin/time`记录退出码0。

这补充了[37项ARM64自动化测试](arm64_test_validation.md)之外的真实数据部署证据；本次是独立手工验收，没有新增第38项CTest。

## 2. 环境与来源

| 项目 | PC参考端 | ARM64推理端 |
|---|---|---|
| 平台 | x86_64 Linux虚拟机 | LubanCat-2N，四核Cortex-A55、2GB内存、8GB eMMC |
| Python | 3.12.3 | 3.8.10 |
| PyTorch | 2.14.0+cpu | 2.4.1 |
| 执行范围 | 训练、保存／加载对照、生成参考值 | 加载既有参数、CPU推理、数值对照和计时 |
| 推理设置 | CPU `float32`模型 | CPU `float32`模型，1个计算线程，关闭梯度记录 |

代码来源固定为`f3b26913dde8d137827797222d8b715dc8b52163`。传输包中的`scripts/`由该提交的`git archive`生成，不能由此推断板端主仓库的HEAD也切换到了这个提交。

主要接口为[scripts/tcp_model_artifact.py](../scripts/tcp_model_artifact.py)中的`write_tcp_model_artifact()`和`read_tcp_model_artifact()`。加载器检查版本、18列顺序、标签映射、固定阈值、有限参数、预处理尺度及重复JSON键，再返回新模型和预处理状态；不执行训练。

真实候选来源为`tcp-pilot-candidates-v2.csv`，SHA-256：

```text
9fe3b49d52b48ad3bd1a301e1ace8017a499e46b1f061f40cfaac3051795369a
```

沿用[首次训练记录](tcp_linear_baseline.md)的设置：Scenario 3-1训练57767条，Scenario 8-1验证6008条；CPU单线程、全0初始化、Adam学习率0.05、100次全批量更新。预处理与类别权重仅来自训练侧。此次导出重新运行同一训练配方，初始／最终损失仍为`0.053299639374017715`／`0.0014024219708517194`。

PC导出目录为：

```text
/home/zcb/datasets/netflow-analyzer/iot23-v2/arm64-inference-v1.drF4zz
```

| 文件 | 作用 | 实际大小 |
|---|---|---|
| `model.json` | `tcp_linear_artifact_v1`；18个权重、1个偏置、训练侧预处理参数、版本／列顺序／阈值 | 2044字节 |
| `inputs.json` | 50条已编码但尚未预处理的18维输入；样本ID、来源集合和标签用于核对，不送入模型 | 17359字节 |
| `expected.json` | PC预处理结果、logit、预测类别、样本顺序、代码／候选来源及比较容差 | 22114字节 |

三个JSON合计41517字节，约40.5 KiB。PC侧文件校验值如下，本轮再次执行`sha256sum -c SHA256SUMS`，三项均为`OK`：

```text
0be0b27006ddf64d6e08cfe9df88a27ef5d7b657ba37c90e821794a00354ee65  model.json
9ca939be3fba267c8d5d757ac8f03ebee5f2341ef89bd97afe8af95a0f5c31be  inputs.json
145a6653daa2a744900624248a5e78e9fa613438a610f0055e9d3fce25826c64  expected.json
```

发送包另外包含`source-scripts.tar`及覆盖四个文件的校验清单。板端此次日志没有单独贴出校验清单结果，因此不补写其逐项`OK`或归档SHA-256。

## 3. 50条样本如何选取

按既有候选顺序，从训练和验证的每个标签分别取最多16条，不按模型预测是否正确挑选：

| 来源集合 | 正常 | 恶意 | 合计 |
|---|---:|---:|---:|
| train | 16 | 16 | 32 |
| validation | 2 | 16 | 18 |
| 总计 | 18 | 32 | 50 |

验证集中本来只有2条正常TCP，不能凭空补足16条。本次含训练样本，不是新建立的独立测试集，也不是对完整63775条候选的板端评估。

完整数据链仍为C程序生成`flow_features_v1`，Python将数值与TCP阶段编码为18维`tcp_input_v1`。此次板端从已经编码的JSON开始，不重新读取PCAP，不运行C抓包程序，也不包含CSV到18维的编码耗时。

板端先依据`model.json`和`inputs.json`计算结果，之后才读取`expected.json`进行比较；PC参考值不是模型输入。比较前核对契约、样本ID顺序、张量形状与有限性，浮点比较使用预先保存的`rtol=1e-5`和`atol=1e-6`，分类使用固定`logit >= 0`阈值并要求逐条相等。

## 4. 实际板端结果

本次独立目录为`/home/cat/tcp-arm64-check-v1.uZscll`，使用已有虚拟环境`/home/cat/venvs/nfa-ml-py38/bin/python`。原始输出：

```text
architecture=aarch64
python_version=3.8.10
torch_version=2.4.1
source_git_commit=f3b26913dde8d137827797222d8b715dc8b52163
samples=50
max_preprocessing_abs_error=0.0
max_logit_abs_error=9.5367431640625e-07
prediction_mismatches=0
torch_import_ms=14485.846315976232
model_load_ms=6.070635048672557
preprocessing_ms=3.9545310428366065
first_batch_inference_ms=0.747562968172133
warm_batch_median_us=80.79397957772017
peak_rss_kib=202636
[PASS] ARM64 TCP model inference matches PC reference
```

外层`/usr/bin/time -v`另记录：

```text
User time (seconds): 15.89
System time (seconds): 0.65
Percent of CPU this job got: 99%
Elapsed (wall clock) time (h:mm:ss or m:ss): 0:16.58
Maximum resident set size (kbytes): 207728
Swaps: 0
Exit status: 0
```

用户通过`NFA_BOARD_INFER_LOG`读取了日志，但没有贴出该变量的完整文件名。这里只记录已知目录，不虚构`inference-log.*`的随机后缀。

## 5. 怎样理解时间和内存

| 指标 | 实际结果 | 计时范围 |
|---|---:|---|
| PyTorch导入 | 14.49秒 | `import torch`；不等于整个Python启动或所有模块导入 |
| 模型加载 | 6.07毫秒 | JSON模型读取、契约检查与参数恢复；不含PyTorch导入 |
| 50条预处理 | 3.95毫秒 | 复用训练侧统计量，不重新拟合 |
| 首次50条推理 | 0.748毫秒 | 首次模型前向计算，关闭梯度记录 |
| 热态50条推理中位数 | 80.79微秒，约0.0808毫秒 | 5次预热后，对同一批已预处理输入计时200次 |
| 整进程墙钟 | 16.58秒 | 含启动、导入、加载、校验、预处理和重复推理等 |
| 外层峰值RSS | 207728 KiB，约202.9 MiB | `/usr/bin/time -v`的进程生命周期测量 |

本次主要启动耗时在PyTorch导入，而不是19参数的模型前向计算。这里只对本次运行作解释，不据此反推此前29.33秒训练测试的各阶段耗时。

热态数字针对固定的`[50,18]`输入，不包含抓包、流生命周期等待、CSV读取、编码、预处理或排队。不能除以50后宣称单流端到端延迟，也不能换算为真实网络吞吐或与约9 Kpps的C采集实验直接比较。

脚本打印的`peak_rss_kib=202636`约197.9 MiB，是脚本读取`resource.getrusage()`时的峰值；外层记录为207728 KiB。保留两个原值，整进程资源描述使用较大的外层值，不猜测差异的具体原因。19个`float32`参数本身虽只有76字节，Python和PyTorch进程仍占约203 MiB，不能把参数大小当作部署内存。

## 6. 本次Linux操作的含义

本次使用新目录传输模型、输入、参考值和固定提交的源码快照，没有覆盖原C程序、系统Python、主仓库或systemd服务。

- `git archive <提交> scripts`只打包该提交已跟踪的脚本，不携带工作区未提交文件、虚拟环境或训练数据。
- `ssh`创建板端独立目录，`scp`传输资料；`sha256sum -c`用于检查文件字节是否与清单一致，不代替来源可信性判断。
- `tar --no-same-owner --no-same-permissions`解包时不照搬发送端用户身份和权限。
- 直接调用虚拟环境中的Python，不需要`source activate`，也不会因为当前Shell是否激活而用错解释器。
- `PYTHONDONTWRITEBYTECODE=1`避免导入脚本时写入`.pyc`；线程环境变量配合`torch.set_num_threads(1)`限制CPU计算并发，不代表程序只有一个系统线程。
- `/usr/bin/time -v`记录整个命令的资源；`timeout --signal=TERM --kill-after=5s 90s`限制运行时间。到90秒发TERM，再给5秒退出宽限，不是无限后台任务。
- `> "$NFA_BOARD_INFER_LOG" 2>&1`把标准输出和错误写入同一日志；`python -u - <目录>`中的`-u`关闭Python输出缓冲，单独的`-`表示脚本来自标准输入，目录是脚本参数。

此次一次性验收脚本通过Shell here-document送入Python，尚未整理为仓库中的独立部署CLI。日志中的PASS与退出0说明本次任务已经结束，不需要额外启动或停止常驻服务。

若只想重新查看现有资料，可在板端执行以下只读命令；它们不会重新开始推理：

```bash
NFA_BOARD_CHECK_DIR=/home/cat/tcp-arm64-check-v1.uZscll

ls -lh -- \
    "$NFA_BOARD_CHECK_DIR/model.json" \
    "$NFA_BOARD_CHECK_DIR/inputs.json" \
    "$NFA_BOARD_CHECK_DIR/expected.json"

find "$NFA_BOARD_CHECK_DIR" \
    -maxdepth 1 \
    -type f \
    -name 'inference-log.*' \
    -print
```

第二条命令列出候选日志路径；人工选中本次文件后用`sed -n '1,180p' <完整路径>`查看。不要用当前Shell的`$?`追溯已经结束的旧推理退出码，应以保存的日志为证据。

## 7. 可以怎样对外描述

最新可以写：在PC完成TCP模型训练及参数导出，在鲁班猫2N完成63775条真实TCP候选的分批推理，分类结果与PC逐条一致，整进程峰值RSS约246 MiB。

不能写成“准确率100%”：一致性比较回答的是同一模型跨平台是否得到相同结果，不是与真实标签是否一致。原模型在验证侧恒判恶意、[正常TCP诊断](tcp_normal_diagnostic.md)误报率30.8333%的限制没有因上板而改善。

本次未完成实时采集到推理的自动连接、在线主机时间窗、告警、模型剪枝／量化、NPU加速或板端训练；没有测量与抓包并发时的资源和延迟。当前先保留这次有限、可核对的部署证据，招聘会前不为扩功能重新启动长稳或后台任务。

## 8. 全量63775条扩展验收

### 8.1 来源、样本与参考包

在首次50条通过后，用户要求增加样本规模。本轮不重新训练或选择阈值，直接复用同一个`model.json`及训练侧预处理参数；模型SHA-256仍为`0be0b27006ddf64d6e08cfe9df88a27ef5d7b657ba37c90e821794a00354ee65`，脚本快照仍来自`f3b26913dde8d137827797222d8b715dc8b52163`。板端主仓库HEAD未由这份日志提供，不作推断。

| 来源集合 | 正常 | 恶意 | 合计 |
|---|---:|---:|---:|
| train，Scenario 3-1 | 2221 | 55546 | 57767 |
| validation，Scenario 8-1 | 2 | 6006 | 6008 |
| 总计 | 2223 | 61552 | 63775 |

这就是既有`tcp-pilot-candidates-v2.csv`的全部候选；不是63775条新留出样本，也不是对原始PCAP中所有流的效果评估。候选文件指纹沿用第2节，严格筛选与来源见[候选发布记录](iot23_pilot_provenance.md)。

PC参考包目录：

```text
/home/zcb/datasets/netflow-analyzer/iot23-v2/arm64-inference-full-v1.GRcy1I
```

用户先完成PC导出及数组独立读回，本轮助手又在PC执行`sha256sum -c SHA256SUMS`，以下5项均为`OK`；六个文件含清单合计15635004字节，约14.91 MiB：

```text
0be0b27006ddf64d6e08cfe9df88a27ef5d7b657ba37c90e821794a00354ee65  model.json
f0835760240c93a2cfef260c468e4a622919c0954a3b4e9ea9d921b6d8c2f1b6  manifest.json
4e99aa03982c406f03118d0bf5f4c4a79212ab80299dd9c13459f6bd4993de20  train.npz
0be706b591734c9bf561cbf836608b24de6943f53cc26496101f36f901a46fe8  validation.npz
f9bd675b283c0f85cdd463d241df26a5fd906b467b8963378c3f82215dba1138  source-scripts.tar
```

每份NPZ包含已编码但尚未预处理的`features[N,18]`、真实`labels[N]`、ASCII样本ID，以及PC预处理结果、logit和固定阈值预测。浮点数组为`float32`、参考分类为`int64`，加载时设置`allow_pickle=False`。标签只用于指标统计，ID只用于身份核对，PC参考输出只用于比较；模型只接收板端预处理后的18维特征。

全量板端目录为`/home/cat/tcp-arm64-full-v1.on838v`，密码SSH创建目录退出0已由用户提供。后续提供的命令将传输、校验和解包分开，源码解包目标仅为实验目录，不覆盖主仓库。原连接诊断已通过网络、握手和主机密钥检查，但公钥未被接受；`BatchMode=yes`又禁止密码提示，因此退出255。随后显式使用密码认证创建目录成功，不据此把此前60秒超时根因确定为等待密码。

用户提供了以下实际板端推理日志，但没有贴出全量板端校验的逐项`OK`、传输退出码或日志文件的随机后缀；这些字段不补写。下文记录的是已收到的推理PASS和外层`Exit status: 0`。

### 8.2 执行方式与完整性检查

板端仍使用Python 3.8.10、NumPy 1.24.4、PyTorch 2.4.1，模型在CPU上执行，计算线程设置为1，使用`model.eval()`和`torch.no_grad()`；不调用训练函数或重新拟合预处理参数。

一次性脚本通过`python -u - <实验目录>`的标准输入运行，暂未整理为仓库内独立部署CLI。它先检查ARM64架构、至少512 MiB可用内存和256 MiB可用磁盘，再核对清单版本、源码提交、18维列顺序、每个集合的数组名称／形状／类型／有限性、标签计数及集合内外样本ID唯一性。

每批最多1024条，训练集合共57批、验证集合共6批，尾批保留实际行数。板端复用保存的预处理状态，再调用模型；浮点结果使用预设`rtol=1e-5, atol=1e-6`，分类以`logit >= 0`为恶意并要求逐条一致。两个集合分别统计，不能将训练侧正确率当作泛化效果。

热态计时另取第一批1024条已预处理输入，先预热5次，再对同一批测量200次，取中位数。重复计算不计入63775条样本数量，也不代表新增204800条独立样本。

外层使用`/usr/bin/time -v`记录整进程资源；`timeout --signal=TERM --kill-after=5s 180s`限制运行时间，标准输出和错误直接重定向到独占创建的`inference-full.*`日志。任务没有后台化，也没有启动systemd服务；本次PASS及退出0表明已经正常结束，不需要额外发送停止命令。

### 8.3 实际板端关键输出

```text
architecture=aarch64
python_version=3.8.10
torch_version=2.4.1
numpy_version=1.24.4
source_git_commit=f3b26913dde8d137827797222d8b715dc8b52163
batch_size=1024
train_samples=57767
train_prediction_mismatches=0
train_npz_load_ms=155.63344303518534
train_preprocessing_ms=244.0987848676741
train_forward_ms=19.16830032132566
validation_samples=6008
validation_prediction_mismatches=0
validation_npz_load_ms=45.782485976815224
validation_preprocessing_ms=23.416549898684025
validation_forward_ms=1.7162170261144638
samples=63775
max_preprocessing_abs_error=0.0
max_logit_abs_error=1.9073486328125e-06
prediction_mismatches=0
torch_import_ms=14394.025388988666
model_load_ms=5.942587973549962
first_batch_inference_ms=0.6583109498023987
warm_batch_rows=1024
warm_iterations=200
warm_batch_median_us=140.5880320817232
peak_rss_kib=252120
[PASS] aarch64 full TCP inference matches PC reference
```

外层原始资源字段：

```text
User time (seconds): 16.82
System time (seconds): 0.87
Percent of CPU this job got: 99%
Elapsed (wall clock) time (h:mm:ss or m:ss): 0:17.79
Maximum resident set size (kbytes): 252120
Swaps: 0
Exit status: 0
```

预处理逐项相同，logit最大绝对差约`1.91e-6`，两侧分类全部一致。最大差超过`atol=1e-6`不等于失败：`allclose`逐项检查的是`|实际-参考| <= atol + rtol * |参考|`，本次按事先固定的组合容差通过，并非事后放宽限制。

与真实标签的指标也保持原基线结果：

| 指标 | train | validation |
|---|---:|---:|
| TN：正常判正常 | 2206 | 0 |
| FP：正常判恶意 | 15 | 2 |
| FN：恶意判正常 | 15 | 0 |
| TP：恶意判恶意 | 55531 | 6006 |
| accuracy | 0.9994806723561895 | 0.9996671105193076 |
| balanced_accuracy | 0.9964881195045062 | 0.5 |
| malicious_precision | 0.999729953552011 | 0.9996671105193076 |
| malicious_recall | 0.999729953552011 | 1.0 |
| benign_false_positive_rate | 0.00675371454299865 | 1.0 |

验证集6008条仍全部判为恶意，2条正常全部误报；99.9667%准确率仍与恒判恶意相同。正常样本只有2条，不据此估计真实部署总体误报率；既有42-1正常TCP诊断的30.8333%误报结论也不变。这次是跨平台一致性通过，不能写成检测效果达标或100%准确率。

### 8.4 时间、内存和对外表述

| 指标 | 实际结果 | 计时边界 |
|---|---:|---|
| PyTorch导入 | 14.394秒 | `import torch`，不含全部Python启动与所有模块导入 |
| 模型加载 | 5.943毫秒 | JSON读取、契约校验与恢复模型／预处理参数 |
| 全量预处理 | 267.515毫秒 | 两个集合各批预处理累计；不重新拟合 |
| 全量模型前向 | 20.885毫秒 | 两个集合各批前向累计；不含预处理及结果复制／比较 |
| 首次1024条前向 | 0.658毫秒 | 首批计算，已包含在训练集合前向累计中 |
| 热态1024条前向中位数 | 140.588微秒，约0.141毫秒 | 同一批已预处理输入，5次预热后测200次 |
| 整进程墙钟 | 17.79秒 | 包含导入、加载、NPZ读取、校验、全量处理及重复计时等 |
| 峰值RSS | 252120 KiB，约246.2 MiB | 脚本内部与外层本次原值相同 |

全量预处理和前向的总数是两个集合日志值相加，不是新的独立测量；首批、热态和累计口径不能混加。主要启动成本仍在框架导入；没有测量常驻进程多轮请求，也没有与抓包并发测量。输入从已编码的NPZ开始，不包含PCAP解析、流结束等待、CSV编码或在线排队，因此不把20.885毫秒或140.588微秒换算为整个服务吞吐、单流响应时延或与C采集9 Kpps的直接比较。

最新简历可写：“在PC训练TCP分类模型，将模型及预处理参数部署到鲁班猫2N，63775条真实候选预测与PC一致；记录分批推理耗时与峰值内存，尚未接入实时告警。”一页版可用“6.38万条”四舍五入，但原始验收保持63775。无需同时塞入所有时间数字；面试时说明包含原训练／验证候选、不是新增留出集，并能解释既有误报问题。

若需找回日志，只在板端执行以下只读命令，不会重跑实验：

```bash
find /home/cat/tcp-arm64-full-v1.on838v \
    -maxdepth 1 \
    -type f \
    -name 'inference-full.*' \
    -print
```

选中实际路径后使用`sed -n '1,220p' <日志完整路径>`查看。不要把随机后缀占位符当作真实路径，也不要用当前`$?`追溯旧退出码。本轮是额外手工验收，不新增第38项CTest；助手仅核对PC资料、维护文档并运行本机回归，未代跑板端实验。
