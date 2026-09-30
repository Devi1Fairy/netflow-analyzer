# IoT-23 试点候选数据指纹

本页记录当前 IoT-23 试点候选 CSV 所用文件的 SHA-256，便于之后核对输入与产物是否仍是同一批字节。它是一次人工核对的文件指纹记录，不是自动化构建配方，也不证明候选标签是真值或样本已经满足正式训练、评估条件。

## TCP 试点 v2：2026-09-30 已发布

在原试点v1之后，完整Scenario 3-1替代前500包作为训练来源，两侧均仅保留严格筛选后的TCP候选。以下路径仍相对于`/home/zcb/datasets/netflow-analyzer/iot23-v2/`；旧v1文件与旧来源清单保留。本次来源切分在执行命令内声明并经过现有清单校验接口，不存在新建的v2磁盘清单。

| 新增文件 | SHA-256 |
| --- | --- |
| `scenario-3-1/full-3s.Tqx2iv/flows.csv` | `a41698a45eccf1f243e27012e277a4fa9b91b0f4c3ca8c7497c7e52c076991e8` |
| `scenario-3-1/full-3s.Tqx2iv/features.csv` | `6d55bcb2152296d8e857d40267b4310a7d8319b657aff3e20b6df685a41fd85e` |
| `scenario-3-1/full-3s.Tqx2iv/review.csv` | `42976540df4bbfd317ea013d11c59c3e059ca9c784263c4e34749befef2a42ee` |
| `splits/tcp-pilot-candidates-v2.csv` | `9fe3b49d52b48ad3bd1a301e1ace8017a499e46b1f061f40cfaac3051795369a` |

原始3-1 PCAP／标签及完整8-1输入仍使用下方v1记录中的路径。v2发布后由本机独立读回：63775条数据、20列、14773716字节；样本ID唯一，协议全部为6。

| 提取结果 | 原始来源 | 集合 | 正常 | 恶意 |
| --- | --- | --- | ---: | ---: |
| `iot23-s3-1-full-idle3s` | `iot23-s3-1-original` | train | 2221 | 55546 |
| `iot23-s8-1-full-idle3s` | `iot23-s8-1-original` | validation | 2 | 6006 |

候选文件名中的v2仅表示数据选择版本，内部格式保持`iot23_candidate_samples_v1`与`flow_features_v1`。前500包未与完整3-1重复合入。模型尚未训练，验证侧仅2条正常TCP不足以估计误报率；Scenario 42-1可用于正常TCP外部诊断，但已在数据准备期间查看，不属于未观察的最终测试。身份、来源和标签列不进入模型特征；后续预处理仅在训练侧拟合。

## 原试点 v1：生成背景与核对范围

- 仓库提交：`e2a5e063b3386144a10b81f0edc810e9fad383f8`；记录指纹前 `git status --short` 为空。
- 本机程序：`build/bin/netflow-analyzer`，SHA-256 为 `781a5670d603e6764807dd6ace6cc288f27f79d05b1e83a304a139ba73a1aaf4`。该摘要标识二进制文件，但单凭它和提交号不能证明二进制一定由该提交构建。
- 下表路径均相对于虚拟机上的仓库外目录 `/home/zcb/datasets/netflow-analyzer/iot23-v2/`。原始 PCAP、前缀 PCAP、Zeek 标签、C 程序导出、审查 sidecar、来源切分清单与最终候选文件都未加入 Git。
- 训练侧使用 Scenario 3-1 的前 500 包、3 秒流空闲阈值，`capture_id=iot23-s3-1-first500-idle3s`，在切分清单中声明 `source_capture_id=iot23-s3-1-original`；验证侧使用 Scenario 8-1 完整 PCAP、相同阈值，`capture_id=iot23-s8-1-full-idle3s`，声明 `source_capture_id=iot23-s8-1-original`。来源声明由清单校验，文件摘要本身不证明前缀一定从原始 PCAP 提取。

用户提供的摘要已在本机针对下表 13 个路径独立重算，结果逐项一致。

| 文件 | SHA-256 |
| --- | --- |
| `scenario-3-1/2018-05-19-20-57-19-192.168.2.5.pcap` | `c674dc0c8d584fa66e6f00c60df973c5fbacad551c850a76d75ec9952641d00b` |
| `scenario-3-1/pilot-first500.cD1Uq1/first500.pcap` | `9f8cd8e7574664c3bffc4ca8ebe9525247ef3f83a56c629d2f7c3e738ed11703` |
| `scenario-3-1/2018-05-19-20-57-19-192.168.2.5-zeek-conn-log.labeled` | `9851009bbca03e15089fa1a356dd4b5eee4a98161a51703626058a0b507f50d0` |
| `scenario-3-1/pilot-first500.cD1Uq1/idle-3s-flows.csv` | `6380d533e896c4d0db1e2cc0df7c110be7efc046668f45cd7102f661c206bb57` |
| `scenario-3-1/pilot-first500.cD1Uq1/idle-3s-features.csv` | `22e79ea5ee3da10a76e78c4ddf3db04d4a2e4643481ea1ff6124afb2cd2945a3` |
| `scenario-3-1/pilot-first500.cD1Uq1/idle-3s-review.csv` | `4cfd7af6213d2d5b1a00217e4fa9e17a43d359a314806a44f501191ffdf81baa` |
| `scenario-8-1/2018-07-31-15-15-09-192.168.100.113.pcap` | `80dcc2602519479ddcde889fa902fee19a76696630811452f8df38888af894f2` |
| `scenario-8-1/2018-07-31-15-15-09-192.168.100.113-zeek-conn-log.labeled` | `4877ca8f0f01902fbd18d28b7d06cb3d0be082355b7f2c8862c9deef1782eb8a` |
| `scenario-8-1/run-3s.enKX9W/flows.csv` | `dc0f8d57dd571b3c9eced9c592668fd3f1f5d59471998fd3663a52f33dc98992` |
| `scenario-8-1/run-3s.enKX9W/features.csv` | `47b8ad930377908bead0daaa38f7b46c2b7dcf78f5f47cea709f58e82bd7e7f9` |
| `scenario-8-1/run-3s.enKX9W/review.csv` | `bbc9fcedec151694d1bfd4b77df480859bc54bbd466db407610353a74e5ac60c` |
| `splits/source-split-v1.csv` | `c24074bb2ed4e23b9bcf83542781154836f7a68118a41c7ff3ab5e1fbe102b79` |
| `splits/pilot-candidates-v1.csv` | `3cedc5667c7e622ace66e80d2c4a2b213f539291956e1303945720018b99a86e` |

## 产物验收与局限

`pilot-candidates-v1.csv` 在虚拟机 ext4 上独占发布并读回：8320 个物理行（表头 1 行、候选 8319 行）、20 列、1948681 字节、权限 `0600`。训练候选 132 条（正常 38、恶意 94），验证候选 8187 条（正常 2181、恶意 6006）；按来源分组，无同一原始抓包跨训练／验证切分。逐行筛选要求唯一、标签记录未复用且时间边界精确。具体审计和筛选规则见[技术选型 TD-041](technical_decisions.md#td-041按原始抓包来源隔离机器学习训练与验证数据)。

这些摘要可用于检查文件是否变化，但当前发布函数**不会自动校验输入 SHA-256 或把指纹嵌入候选 CSV**；也未把完整命令、工具版本、前缀提取过程固化成可重放的构建流程。只有两个原始来源，训练侧仅有 132 条前缀候选，也没有未参与调参的独立测试来源。因此，该文件只用于试运行数据管线，不据此训练并报告正式泛化指标。下一阶段须增加独立训练及最终测试来源，再决定正式样本准入和训练方案。
