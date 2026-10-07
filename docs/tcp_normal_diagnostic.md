# TCP线性基线：独立来源正常流诊断

执行日期：2026-10-01。代码版本为`2f6be223e665f054cc91b8185fbf226fe78ac79b`；执行前工作区干净，x86_64 Debug启用ML解释器后35/35 CTest通过。

## 1. 为什么做这一步

[首次训练](tcp_linear_baseline.md)使用Scenario 3-1训练、Scenario 8-1验证。验证侧只有2条正常TCP，且6008条全部判恶意；99.9667%的准确率不能说明正常流识别可靠。因此本步用此前准备的Scenario 42-1，检查冻结基线对更多正常TCP候选的误报。

42-1是与训练来源不同的开发诊断来源，但它此前已经被查看，不能称为未观察的最终测试集。本步不把它加入训练，不重新拟合其预处理参数，不调整阈值，不修改C特征契约，也不保存模型或数据文件。

模型此前只存在于临时Python进程中，因此本步按原来的CPU单线程、全0初始化、100次Adam更新、学习率0.05和训练侧类别权重重新训练一次。训练输入仍只来自3-1，然后才对42-1执行推理；不是使用42-1更新模型。

## 2. 输入及严格筛选

输入路径相对于`/home/zcb/datasets/netflow-analyzer/iot23-v2/`：

| 文件 | SHA-256 |
|---|---|
| `splits/tcp-pilot-candidates-v2.csv` | `9fe3b49d52b48ad3bd1a301e1ace8017a499e46b1f061f40cfaac3051795369a` |
| `scenario-42-1.9y5VmS/2019-01-10-14-34-38-192.168.1.197-zeek-conn-log.labeled` | `269fa1b22d9a37e159cf41b81a213a0032d21306f5d39b4c20cb0d211b04e8aa` |
| `scenario-42-1.9y5VmS/run-3s.5x7T7w/flows.csv` | `237519544760c579cbc9767375609c31e86c6d6cf5904b767684dae163ea17d8` |
| `scenario-42-1.9y5VmS/run-3s.5x7T7w/features.csv` | `eaebbc1bb9d16c1c8995e3bbe03c1807cdb85374a232c5695b9d2e8ca9d2b755` |

四份输入在执行前后均检查摘要，没有改写。复用现有流／特征对齐、标签索引、边界审计和逐行候选连接函数，得到：

- 普通流与特征CSV对齐4598条；
- 唯一且标签未复用的候选3799条，其中边界精确3764条、`label_inside_flow`35条；
- 严格候选只接受3764条精确记录，其中TCP1440条，全部是候选标签正常；
- 6条标签恶意记录此前已确认属于非精确边界，本步继续排除，不为凑二分类数据而放宽条件。

审查sidecar在`io.StringIO`中生成，仅作为内存文本流，不创建`review.csv`。IP、端口和样本ID只用于连接与追踪，不输入模型。

## 3. 结果与含义

阈值固定为`logit >= 0`判恶意。

| 方法 | 正常判正常TN | 正常误报FP | 正常识别率 | 正常误报率 |
|---|---:|---:|---:|---:|
| 线性模型 | 996 | 444 | 69.1667% | 30.8333% |
| 恒判恶意 | 0 | 1440 | 0% | 100% |

正常流诊断说明模型比恒判恶意能识别更多正常流，但约30.83%的误报仍不能支持直接上线预警。这不是正常与恶意两类完整评估：本批没有通过准入的恶意样本，所以恶意召回率和平衡准确率为`None`，JSON中显示`null`。`FN=0`不代表证明了没有漏报。

按实际TCP阶段分组：

| TCP阶段 | 正常候选 | 误报 | 本组误报率 |
|---|---:|---:|---:|
| `fin-seen` | 850 | 0 | 0% |
| `reset` | 146 | 0 | 0% |
| `syn-seen` | 444 | 444 | 100% |

这些阶段是旁路状态跟踪结果，不能直接解释为真实内核socket状态。`syn-seen`只说明当前C流观察到了SYN阶段，不能单凭它判断攻击。

进一步比较原始18维编码向量发现：42-1有215条正常TCP候选与训练侧恶意记录的向量完全一致；代表性输入是零持续时间、1包、捕获／线路均74字节、两个方向不平衡度均1、未完成握手、`syn-seen`。训练侧该代表性向量对应27923条恶意候选。训练自身共11582种编码模式，本次未发现同模式内两种标签冲突；冲突在跨来源正常样本出现后才暴露。另有229条误报没有与训练恶意向量精确相等，本步没有把其根因归入上述完全相同输入问题。

同一预处理和确定性分类器对完全相同的输入只能返回相同评分，所以这组215条不能靠仅增加网络深度来与训练侧相同向量区分。该结论只针对完全相同输入组，不等于整个任务不可学习，也不独立证明公开标签的安全真值。还需要逐样本诊断与更有区分力的信息，例如按主机／时间窗统计目标多样性、连接尝试和握手比例等行为；IP可以作为聚合键，不宜直接作为模型记忆恶意设备的捷径。

## 4. 可复现命令

在开发虚拟机的项目根目录运行；不需要`sudo`、联网、重跑PCAP或一小时实验。命令只读外部数据、借用现有模块，在内存中训练和评估，退出后停止，不修改正式训练／验证清单。

```bash
cd /home/zcb/workspace/netflow-analyzer

.venv/bin/python - <<'PY'
import hashlib
import io
import json
import sys
from collections import Counter, defaultdict
from dataclasses import asdict
from pathlib import Path

import torch

sys.path.insert(0, str(Path("scripts").resolve()))
from audit_iot23_flow_matches import (
    audit_candidate_boundaries_with_exact_rows,
    write_iot23_review_csv,
)
from flow_feature_alignment import (
    EXPECTED_FEATURE_COLUMNS,
    validate_flow_feature_alignment,
)
from flow_sample_metadata import SUPPORTED_FEATURE_SCHEMA_VERSION
from iot23_label_index import load_label_index
from iot23_sample_selection import collect_exact_reviewed_candidates
from tcp_candidate_dataset import load_tcp_candidate_dataset
from tcp_tensor_dataset import TensorTcpSplit, tensorize_tcp_dataset
from tcp_feature_preprocessing import (
    fit_tcp_preprocessing,
    apply_tcp_preprocessing,
)
from tcp_model_input import encode_tcp_candidate
from tcp_linear_baseline import (
    train_tcp_linear_baseline,
    evaluate_binary_logits,
)

root = Path("/home/zcb/datasets/netflow-analyzer/iot23-v2")
source42 = root / "scenario-42-1.9y5VmS"
run42 = source42 / "run-3s.5x7T7w"
label_file = source42 / (
    "2019-01-10-14-34-38-192.168.1.197-zeek-conn-log.labeled"
)
flow_path = run42 / "flows.csv"
feature_path = run42 / "features.csv"
candidate_path = root / "splits/tcp-pilot-candidates-v2.csv"
expected = {
    candidate_path: "9fe3b49d52b48ad3bd1a301e1ace8017a499e46b1f061f40cfaac3051795369a",
    label_file: "269fa1b22d9a37e159cf41b81a213a0032d21306f5d39b4c20cb0d211b04e8aa",
    flow_path: "237519544760c579cbc9767375609c31e86c6d6cf5904b767684dae163ea17d8",
    feature_path: "eaebbc1bb9d16c1c8995e3bbe03c1807cdb85374a232c5695b9d2e8ca9d2b755",
}

def check_fingerprints():
    # 只读内容并检查摘要；输入变化就失败，不继续使用旧计数。
    for path, fingerprint in expected.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != fingerprint:
            raise RuntimeError(f"input fingerprint differs: {path}")

check_fingerprints()
index42 = load_label_index(label_file)

# 先确认同次分析的流行与特征行能按位置对应。
with flow_path.open(encoding="utf-8", newline="") as flows:
    with feature_path.open(encoding="utf-8", newline="") as features:
        aligned = validate_flow_feature_alignment(flows, features)
with flow_path.open(encoding="utf-8", newline="") as flows:
    counts, boundaries, exact_rows = (
        audit_candidate_boundaries_with_exact_rows(index42, flows)
    )

# StringIO提供内存里的文本文件接口，不向磁盘写sidecar。
with io.StringIO() as review:
    with flow_path.open(encoding="utf-8", newline="") as flows:
        review_counts = write_iot23_review_csv(
            index42, flows, review,
            "iot23-s42-1-full-idle3s-diagnostic",
        )
    if review_counts != counts:
        raise RuntimeError("audit and review counts differ")
    # 写完位置在末尾；seek(0)回表头，供后面的CSV读取器读取。
    review.seek(0)
    with feature_path.open(encoding="utf-8", newline="") as features:
        selected = collect_exact_reviewed_candidates(
            features, review,
            "iot23-s42-1-full-idle3s-diagnostic", exact_rows,
        )

vectors, labels, ids, phases = [], [], [], []
for candidate in selected:
    # zip将固定列名与该条特征值配对，dict得到编码器接收的映射。
    row = dict(zip(EXPECTED_FEATURE_COLUMNS[1:], candidate.feature_values))
    if row["protocol"] != "6":
        continue
    row["feature_schema_version"] = SUPPORTED_FEATURE_SCHEMA_VERSION
    row["candidate_label_group"] = candidate.label_group
    vector, label = encode_tcp_candidate(row)
    if label != 0:
        raise RuntimeError("diagnostic TCP sample is not benign")
    vectors.append(vector)
    labels.append(label)
    ids.append(candidate.sample_id)
    phases.append(row["tcp_phase"])
if (aligned, len(selected), len(vectors)) != (4598, 3764, 1440):
    raise RuntimeError("unexpected Scenario 42 candidate counts")

with candidate_path.open(encoding="utf-8", newline="") as stream:
    encoded = load_tcp_candidate_dataset(stream)
tensors = tensorize_tcp_dataset(encoded)
torch.set_num_threads(1)

# 只有原训练侧可以fit。42-1只复用参数和模型。
state = fit_tcp_preprocessing(tensors.train)
training = apply_tcp_preprocessing("train", tensors.train, state)
result = train_tcp_linear_baseline(
    training.features, training.labels, epochs=100, learning_rate=0.05,
)
raw42 = TensorTcpSplit(
    features=torch.tensor(vectors, dtype=torch.float32, device="cpu"),
    labels=torch.tensor(labels, dtype=torch.float32, device="cpu"),
    sample_ids=tuple(ids),
)
diag42 = apply_tcp_preprocessing("scenario42_diagnostic", raw42, state)
with torch.no_grad():
    logits = result.model(diag42.features)
    metrics = evaluate_binary_logits(diag42.labels, logits)
    constant = evaluate_binary_logits(
        diag42.labels, torch.ones_like(diag42.labels),
    )
    false_positive = logits >= 0

print(f"scenario42: aligned={aligned} exact={len(selected)} tcp={len(vectors)}")
print("scenario42_linear=" + json.dumps(asdict(metrics)))
print("scenario42_always_malicious=" + json.dumps(asdict(constant)))
phase_counts = Counter(phases)
phase_fp = Counter(
    phase for position, phase in enumerate(phases)
    if false_positive[position].item()
)
for phase, total in sorted(phase_counts.items()):
    print(f"phase[{phase}]: count={total} false_positive={phase_fp[phase]}")

# tuple向量可作为字典键；比较18维数值，而不是比较标签或IP。
patterns = defaultdict(Counter)
for vector, label in zip(encoded.train.features, encoded.train.labels):
    patterns[vector][label] += 1
overlap = sum(
    1 for vector in vectors
    if patterns.get(vector, Counter())[1] > 0
)
print(f"normal_matches_training_malicious_vector={overlap}")

check_fingerprints()
# PASS只表示数据检查和诊断执行成功，不表示模型误报率合格。
print("[PASS] Scenario 42 strict normal-TCP diagnostic execution")
PY
```

本次重点输出为`aligned=4598 exact=3764 tcp=1440`、`true_negative=996`、`false_positive=444`、`benign_false_positive_rate=0.30833333333333335`、`normal_matches_training_malicious_vector=215`。PyTorch版本或计算平台变化可能带来浮点差异；遇到数据检查错误时先核对路径与输入指纹，不改断言掩盖问题。

## 5. 后续边界

本步完成了一个来源的候选正常TCP误报诊断，并定位了部分输入不可区分的情况；它不是最终恶意识别评估，也不证明所有正常网络或未来攻击分布。现有特征和严格筛选会影响覆盖范围，候选标签仍来自公开记录。新增正常诊断不能直接通过将其加入训练来替代独立验证。

下一步可以保存该基线的模型、18维编码契约、预处理参数和固定阈值，再验证加载前后预测一致；保存的仍是已知存在误报的开发基线，不代表可以启用生产告警。之后的特征改进须以这个基线作对照，并保留未观察的独立最终测试来源。
