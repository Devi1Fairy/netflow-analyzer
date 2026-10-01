# TCP线性二分类基线：首次训练与验证

验证日期：2026-10-01。环境为开发虚拟机x86_64、Python 3.12.3、PyTorch 2.14.0+cpu；本次真实训练显式使用1个PyTorch计算线程。

本步完成的是离线监督二分类基线，不是实时异常预警。训练和评估代码由用户输入，助手只运行验收和更新文档；训练结果只保存在本次Python进程内，没有生成模型文件，也没有改动C采集路径或上板运行。

## 1. 输入与训练约定

数据使用仓库外的`/home/zcb/datasets/netflow-analyzer/iot23-v2/splits/tcp-pilot-candidates-v2.csv`。SHA-256为：

```text
9fe3b49d52b48ad3bd1a301e1ace8017a499e46b1f061f40cfaac3051795369a
```

该文件由相同C提取器处理公开PCAP后生成，按现有唯一匹配、标签未复用和精确生命周期边界规则筛选；这些仍是候选监督标签，不等于人工独立确认的攻击真值。

| 集合 | 原始来源 | 正常 | 恶意 | 合计 |
|---|---|---:|---:|---:|
| 训练 | 完整Scenario 3-1 | 2221 | 55546 | 57767 |
| 验证 | 完整Scenario 8-1 | 2 | 6006 | 6008 |
| 测试 | 尚未提供 | 0 | 0 | 0 |

训练流程为：候选CSV校验与编码 → CPU `float32`张量 → 只在训练集合拟合预处理 → 两个集合复用相同预处理 → 只使用训练特征和标签更新模型 → 固定阈值评估。

| 项目 | 本次值与含义 |
|---|---|
| 输入契约 | `tcp_input_v1`，固定18维；身份、来源和标签不进入特征 |
| 预处理契约 | `tcp_preprocessing_v1`；前6列`log1p`后标准化，后12列保持原值 |
| 模型契约 | `tcp_linear_v1`；18个权重＋1个偏置，共19个参数 |
| 初始化 | 权重与偏置全0，无随机初始化或打乱 |
| 训练规模 | 全批量，每次更新使用全部57767条训练样本 |
| 优化器 | Adam，`learning_rate=0.05`，`foreach=False` |
| 更新次数 | `epochs=100`；损失历史含初始值和100次更新后的值，共101项 |
| 损失 | `BCEWithLogitsLoss`，不在损失前手动执行sigmoid |
| 类别权重 | 只从训练标签计算`pos_weight=2221/55546=0.03998487739891261` |
| 判断阈值 | `logit >= 0`判恶意，等价于sigmoid评分不小于0.5 |

这里恶意类是多数类，所以`pos_weight`小于1，用来降低多数类的总损失贡献；两类样本数乘各自权重后相等。它不是把验证分布用于训练，也不保证推理时评分已经校准为真实恶意概率。

当前数据规模允许先用全批量建立可重复基线，不引入DataLoader和随机小批次。所有输入张量只读借用，`detach()`切断输入计算图，优化器只更新新模型的参数。该实现不是所有规模下的最终训练方案。

## 2. 首次真实训练结果

加权训练损失由`0.053299639374017715`降到`0.0014024219708517194`。CPU单线程本次模型训练函数耗时约0.79秒；不包含CSV加载、编码和预处理，不是端到端训练性能或开发板推理性能。

损失包含类别权重，不能把其绝对值当作错误率，也不能与未加权的损失直接比较。

混淆矩阵以恶意为正类：TN＝正常判正常，FP＝正常误报恶意，FN＝恶意漏报正常，TP＝恶意判恶意。

| 集合与方法 | TN | FP | FN | TP |
|---|---:|---:|---:|---:|
| 训练：线性模型 | 2206 | 15 | 15 | 55531 |
| 训练：恒判恶意 | 0 | 2221 | 0 | 55546 |
| 验证：线性模型 | 0 | 2 | 0 | 6006 |
| 验证：恒判恶意 | 0 | 2 | 0 | 6006 |

| 集合与方法 | 准确率 | 恶意精确率 | 恶意召回率 | 正常误报率 | 平衡准确率 |
|---|---:|---:|---:|---:|---:|
| 训练：线性模型 | 99.9481% | 99.9730% | 99.9730% | 0.6754% | 99.6488% |
| 训练：恒判恶意 | 96.1552% | 96.1552% | 100% | 100% | 50% |
| 验证：线性模型 | 99.9667% | 99.9667% | 100% | 100% | 50% |
| 验证：恒判恶意 | 99.9667% | 99.9667% | 100% | 100% | 50% |

指标定义：

- 准确率：`(TP + TN) / 总样本数`；
- 恶意精确率：`TP / (TP + FP)`，回答“判恶意的流有多少是真正标签恶意”；
- 恶意召回率：`TP / (TP + FN)`，回答“标签恶意的流检出了多少”；
- 正常误报率：`FP / (FP + TN)`；
- 平衡准确率：`(恶意召回率 + 正常召回率) / 2`，正常召回率为`1 - 正常误报率`。

没有对应分母时返回`None`，不能伪造0或1；两类任一召回率无定义时，平衡准确率也为`None`。

结论：训练集合内能够区分大部分候选正常和恶意流，但在当前固定阈值下，验证集合6008条全部预测为恶意。验证99.9667%的准确率与恒判恶意完全相同，不能宣称模型已经取得有效跨场景识别能力。验证只有2条正常TCP，两条都误报；这个100%只描述本次`2/2`，不能估计所有正常网络流的总体误报率。

类别分布和来源差异已经在数据准备阶段观察到；本次尚未逐样本分析权重、特征捷径或错误原因，不能仅凭这些计数认定故障根因。下一轮应保留当前基线，优先补充独立正常TCP诊断并分析错误，不能只为提高验证分数反复调阈值。Scenario 42-1此前已被查看，可作为诊断来源，但不能改称未观察的最终测试集。

## 3. 如何复现

先进入项目根目录。普通系统Python不一定安装PyTorch，所以ML命令显式使用项目`.venv/bin/python`，不依赖当前终端是否显示`(.venv)`。

```bash
cd /home/zcb/workspace/netflow-analyzer

python3 -m py_compile \
    scripts/tcp_linear_baseline.py \
    tests/integration/test_tcp_linear_baseline.py

.venv/bin/python \
    tests/integration/test_tcp_linear_baseline.py \
    --scripts-dir scripts

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DNFA_ML_PYTHON_EXECUTABLE:FILEPATH=/home/zcb/workspace/netflow-analyzer/.venv/bin/python

cmake --build build
ctest --test-dir build --output-on-failure
```

`py_compile`只检查语法，不能代替执行时的导入、训练和指标测试。`--scripts-dir`告诉测试程序模块的位置；CMake的`-D名称:FILEPATH=值`设置一个路径类型的缓存变量，使三个可选ML测试使用含PyTorch的解释器。普通C/libpcap项目仍不强制依赖PyTorch。

本次直接测试输出`[PASS] TCP linear baseline training and metrics`，启用ML解释器后全量CTest为35/35，约6.34秒。合成回归覆盖19个参数、可分样本训练、输入不变、重复结果一致、不消费随机数状态、训练标签权重、已知混淆矩阵、零分母以及非法输入。它不读取公开数据，不代表真实数据效果已被自动化证明；Release、Sanitizer和ARM64没有按本步重跑。

下面的here-document把临时Python程序作为标准输入交给解释器：`<<'PY'`保留其中的Python文本，不让shell展开变量；末尾独占一行的`PY`结束输入。命令只读取候选文件，模型训练在内存完成，进程退出后不会继续运行，也不会保存模型。

```bash
.venv/bin/python - <<'PY'
import hashlib
import json
import sys
from dataclasses import asdict
from pathlib import Path

import torch

# 加入模块查找路径；本命令须在项目根目录执行。
sys.path.insert(0, str(Path("scripts").resolve()))
from tcp_candidate_dataset import load_tcp_candidate_dataset
from tcp_tensor_dataset import tensorize_tcp_dataset
from tcp_feature_preprocessing import (
    fit_tcp_preprocessing,
    preprocess_tcp_dataset,
)
from tcp_linear_baseline import (
    train_tcp_linear_baseline,
    evaluate_binary_logits,
)

candidate_path = Path(
    "/home/zcb/datasets/netflow-analyzer/iot23-v2/"
    "splits/tcp-pilot-candidates-v2.csv"
)
expected_digest = (
    "9fe3b49d52b48ad3bd1a301e1ace8017a499"
    "e46b1f061f40cfaac3051795369a"
)
# read_bytes读文件内容；hexdigest把SHA-256摘要转换为十六进制文本。
digest = hashlib.sha256(candidate_path.read_bytes()).hexdigest()
if digest != expected_digest:
    raise RuntimeError("candidate CSV fingerprint differs")

with candidate_path.open(encoding="utf-8", newline="") as stream:
    encoded = load_tcp_candidate_dataset(stream)
tensors = tensorize_tcp_dataset(encoded)

# 验证集合只能复用训练参数，不调用fit。
state = fit_tcp_preprocessing(tensors.train)
processed = preprocess_tcp_dataset(tensors, state)
torch.set_num_threads(1)
result = train_tcp_linear_baseline(
    processed.train.features,
    processed.train.labels,
    epochs=100,
    learning_rate=0.05,
)

print("candidate_sha256=" + digest)
print("positive_weight=" + repr(result.positive_weight))
print("loss_initial=" + repr(result.loss_history[0]))
print("loss_final=" + repr(result.loss_history[-1]))

# no_grad关闭梯度记录；评估不再更新模型。
with torch.no_grad():
    for name in ("train", "validation"):
        split = getattr(processed, name)
        learned = evaluate_binary_logits(
            split.labels, result.model(split.features)
        )
        # 全部为正的logit，构造“恒判恶意”对照。
        constant = evaluate_binary_logits(
            split.labels, torch.ones_like(split.labels)
        )
        # asdict把指标数据类转换为字典，json.dumps生成可读文本。
        print(name + "_linear=" + json.dumps(asdict(learned)))
        print(name + "_always_malicious=" + json.dumps(asdict(constant)))
PY
```

依赖版本、线程和计算平台变化可能带来浮点差异；本次合成测试只证明当前环境内重复训练一致，不承诺跨平台逐位相等。

## 4. 本步边界与后续

尚未完成模型与预处理参数的文件持久化、加载后预测一致性、独立最终测试、ARM64推理、在线主机时间窗或告警事件。训练内的低误报不能代替独立来源误报验证，单流二分类也不能直接等同于按IP聚合的异常预警。

首次训练时最新已提交版本为`67c573a feat(ml): add train-only TCP feature preprocessing`；线性模块、测试和CMake注册仍为用户工作区的未提交改动。该提交是本步的父版本，不能写成它本身已经包含线性模型。
