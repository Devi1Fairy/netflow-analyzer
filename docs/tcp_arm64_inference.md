# TCP模型ARM64真实样本推理验收

验收日期：2026-10-12。板端结果来自用户在LubanCat-2N实际运行后提供的日志，助手没有通过远程连接代跑。本次确认的是“PC训练和保存参数→板端加载→真实样本预测与PC对照”，不是检测效果验收或实时告警上线。

## 1. 本次完成了什么

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

可以写：在PC完成TCP模型训练及参数导出，在鲁班猫2N完成加载推理，50条真实流样本的分类结果与PC一致。

不能写成“准确率100%”：一致性比较回答的是同一模型跨平台是否得到相同结果，不是与真实标签是否一致。原模型在验证侧恒判恶意、[正常TCP诊断](tcp_normal_diagnostic.md)误报率30.8333%的限制没有因上板而改善。

本次未完成实时采集到推理的自动连接、在线主机时间窗、告警、模型剪枝／量化、NPU加速或板端训练；没有测量与抓包并发时的资源和延迟。当前先保留这次有限、可核对的部署证据，招聘会前不为扩功能重新启动长稳或后台任务。
