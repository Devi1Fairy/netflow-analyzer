# Netflow Analyzer会话交接文档

最后更新：2026-11-03（UTC+08:00）

本文用于把当前项目状态、学习背景、协作方式、代码架构、测试、Git历史、已知边界和下一步计划完整交接给新的Codex会话。接手者应先完整阅读本文，再执行只读检查，不要根据标题直接开始大范围修改。

## 1. 新会话接手顺序

新会话建议先执行：

```bash
cd /home/zcb/workspace/netflow-analyzer

git status --short
git branch --show-current
git log -5 --oneline --decorate

cmake --build build
cmake -E chdir build ctest --output-on-failure
```

预期基线：

- 最新控制阶段（2026-11-03）：用户确认STM32→Linux网关→Windows Qt只读TCP接入已成功，提供Windows只读基线`fe1f0c5`；这是用户报告，未在本VM核对Windows仓库或补填新运行指标。Linux仍为`bac7744`，本轮开始保留既有两份未提交文档。助手检查现有gateway及五项专项测试代码，VM Debug重新构建、42/42项CTest通过（12.83秒），未连接板端或真实串口。下一阶段按第18.13节实现可选`--control`，MCU与Windows本轮不改；当前只读源码尚不支持该开关或控制能力帧。第一步仅给用户填写纯内存请求解析器与测试；仓库外普通／ASan／UBSan候选三组检查通过，尚未写入仓库、尚未注册第43项。不得把计划写成已完成TCP控制，Git和源码仍由用户操作。

- 最新Qt网络对照（2026-11-03）：用户截图确认Windows PowerShell到`192.168.1.102:18080`的`TcpTestSucceeded=True`、源地址`192.168.1.100`，板端无串口诊断器接入同一来源`('192.168.1.100', 8095)`后结束。已证明本次PowerShell TCP路径可用，未证明Qt按钮实际发起连接或Qt自身网络策略正常。下一步重新运行有界无串口诊断器，仅由Qt连接并查看日志，计划接入后保持5秒再关闭，不发送BMS遥测；不再让PowerShell占用该次会话，不修改驱动／防火墙／固件。Qt真实联调仍未通过，详见问题5.26及第18.12节；本轮保留已有两份未提交文档改动，源码与Git仍由用户操作。

- 最新网关阶段（2026-11-01）：网关源码快照仍为`a41203a`，VM HEAD为用户提交的`cc0846f`，本轮开始已有3份文档改动，均保留。五项VM专项及板端原生编译／四组无硬件CLI检查此前已通过。首次真实STM32→CH340→鲁班猫网关→TCP→VM文本接收现已验收通过：本次端点`192.168.1.102:18080`，网关`lines=118 discarded=1 tail_bytes=0 discarding=0`；VM接收PASS、退出0，本地文件独立核对118条完整BMS行、22656字节、末尾LF。用户确认打开／转发／退出后三阶段心跳正常、未观察到复位，并补齐板端网关及tee退出码均0、日志目录`/home/cat/bms-gateway-v1.9zr5rW/tcp-30s.bK9H8H`、退出后fuser未列占用进程。会话已结束，并非常驻监听；不据此声称长期稳定、UART零丢失或控制链路通过。下一步交接Windows Qt TCP接收入口，只接遥测并复用现有解析／界面；Qt、分析器联动及实物断连仍待验证，不自动启用服务或扩展控制。每步结束提供commit标题，Git仍由用户执行；详见第18.12节。

- 最新串口信号验收（2026-11-01）：虚拟机HEAD为`5258927 feat(gateway): add bounded BMS serial receiver`，与远程同步；SIGINT文档更新前工作区干净，当前保留未提交的3份验收文档，不覆盖已有改动。源码摘要仍为`8ef2fbc5ecb46c15da613055a3546822a1dfe39cc8681bb454abefa649450a0d`。用户已分别在板端`sigint-5s.yrjSSf`与`sigterm-5s.uhjKgd`得到明确发送INT／TERM的日志，各收尾24行、丢弃1段、尾部0字节、退出0，退出后`fuser`未列出占用；两次三阶段心跳均由用户分别确认正常，未观察到复位。SIGINT前明确通过权限检查并临时停止ModemManager至`inactive/dead`；首次149行运行时是否停止仍未知，SIGTERM前的服务复查输出未提供，均不补写。只确认两种程序信号路径，不声称实际键盘Ctrl+C、断连恢复或systemd部署已完成。下一步先确认STM32供电与接线安全，再安排断连和主机行缓冲回归；详细结果见第18.12节。本轮只维护既有文档，不改源码、不执行Git写操作或重跑历史37项测试。

- 首次STM32联动验收（2026-11-01，历史快照）：按用户决定，Linux源码放在本仓库`gateway/`，而非最初计划的BMS仓库。首次验收时虚拟机HEAD为`123a28e`、新源码尚未提交；之后已提交为上条的`5258927`，不以虚拟机提交推断板端主仓库身份。用户在`/home/cat/bms-serial-v1.i9yBPJ`原生编译ARM64程序，源码摘要一致；`receive-30s.AiBZSI`首次接收退出0、149行、启动丢弃1段、尾部32字节，三阶段心跳正常且未观察到复位。首次运行时ModemManager是否停止仍未知，后续SIGINT测试不能回推。具体证据见第18.12节及TD-045；TCP／Qt／抓包联动尚未实现，以下10月记录也保留为历史阶段。

- 最新全量ARM64模型验收（2026-10-12）：本轮文档更新前HEAD为`11f8273`，`feature/flow-features`与远程同步、工作区干净；首次50条文档已提交。用户在`/home/cat/tcp-arm64-full-v1.on838v`用固定`f3b2691`脚本快照加载原PC模型和预处理参数，每批最多1024条，覆盖训练57767条与验证6008条，共63775条，分类与PC逐条一致。预处理最大绝对差0、logit最大绝对差`1.9073486328125e-06`，按原定组合容差通过，PASS且外层退出0；整进程17.79秒、PyTorch导入14.394秒、峰值RSS252120 KiB（约246.2 MiB），固定1024条热态中位数140.588微秒不代表在线吞吐或单流时延。两集合混淆计数与原基线相同，验证2条正常仍全部误报；含原训练样本，不是新留出集，既有42-1正常误报30.8333%不变。PC包为`arm64-inference-full-v1.GRcy1I`，本机5项SHA-256重新通过；板端实际日志后缀、传输退出码及逐项校验输出未提供，不补写。源码快照不等于板端主仓库HEAD。助手仅更新6份文档，构建及37/37项x86_64 Debug CTest通过（8.25秒），未改源码／README、安装依赖、代跑板端或启用服务，文档待用户提交。最新简历可写6.38万条真实候选分批预测与PC一致；下一步回到简历和证据整理，不自动新增实验。完整证据见[tcp_arm64_inference.md第8节](tcp_arm64_inference.md#8-全量63775条扩展验收)，以下50条为首次历史验收。
- 最新真实模型上板验收（2026-10-12）：HEAD为`f3b26913dde8d137827797222d8b715dc8b52163`，`feature/flow-features`与远程同步，文档更新前工作区干净。模型JSON保存／加载及往返测试已由用户提交；PC导出真实参数和50条输入后，用户在`/home/cat/tcp-arm64-check-v1.uZscll`用固定提交的脚本快照完成ARM64推理，分类与PC全部一致，预处理最大绝对差0、logit最大绝对差`9.5367431640625e-07`，PASS且外层退出0。PyTorch导入14.49秒、整进程16.58秒；50条热态前向中位数80.79微秒，不能解释为单流延迟或网络吞吐。外层峰值RSS207728 KiB，约202.9 MiB；保留脚本内部202636 KiB原值。50条由32条训练与18条验证样本组成，不是新留出集或检测准确率100%，原正常流误报30.8333%不变。详情与JSON指纹见[tcp_arm64_inference.md](tcp_arm64_inference.md)，简历素材已更新为PC训练、板端加载推理。助手本轮只更新6份文档，虚拟机重新构建及37/37项CTest通过（8.17秒）；未修改源码／README、重跑板端实验、安装依赖或启用服务，待用户提交推送。下一步准备一页简历及面试讲解，不自动扩展DNS、LLM或在线告警。以下暂停上板和未保存模型等描述是当时历史状态，不覆盖本条最新验收。
- 最新ARM64及同步验收（2026-10-12）：用户在LubanCat-2N原生Debug构建`/home/cat/build/netflow-analyzer-arm64-verify`中完成37/37项CTest，耗时94.41秒、退出0；验收时为`3ad8a49`加板端CMake超时改动，现已提交／推送为`4a6d8de`并同步虚拟机。虚拟机在该提交重新构建成功，37/37项CTest通过（7.53秒、退出0）。首次板端36/37，原线性测试独立运行29.33秒且退出0后，超时从20秒改为60秒；实际提交是两处设置均改60，尚未合并，生成配置只注册一次且超时60。此前草稿“已合并”表述已纠正，不为整理冗余另改源码。33项普通测试加4项可选ML测试全部通过；板端Python 3.8.10、NumPy 1.24.4、PyTorch 2.4.1，与虚拟机依赖不同。最终板端日志`ctest-arm64-37-timeout60.nxTPrF`。已处理ensurepip缺失及安装后根盘仅268 MiB的问题，保留两个活动VS Code Server版本、备份并清理另两个版本后剩余1.5 GiB。用户还遇到提交身份未配置，后续已成功创建`4a6d8de`；作用域未提供。详见[验收记录](arm64_test_validation.md)和问题5.21～5.23。助手只更新5份文档，待用户提交／推送后板端拉取；之后按[简历素材](jobfair_project_brief.md)准备一页简历与项目讲解，不重跑长稳、不启动服务或扩展功能。以下“板端仅18项”为历史快照。
- 招聘会表达材料（2026-10-12）：用户已提交TCP验收为`3ad8a49`，检查时与远程同步、工作区干净；构建及37/37项x86_64 Debug CTest通过（7.64秒）。新增[jobfair_project_brief.md](jobfair_project_brief.md)，给出4条核心简历素材、可选AI实验条目、1分钟介绍、演示顺序和面试追问，均引用已有验收并区分历史18项ARM64测试与新版手工数据链。未收到的TCP退出码／发生器PASS仍不补写。下一步由用户调整实际简历、练习项目介绍，再按需要检查演示材料；本轮不修改README或源码、不启动服务、不新增功能，文档由用户提交。
- 最新TCP数据链验收（2026-10-12）：检查时HEAD为`1cbb72a`、与远程同步且工作区干净；构建及37/37项x86_64 Debug CTest通过（8.57秒）。用户提供板端`/home/cat/nfa-demo-tcp.YpNAuj`：10包全部完整处理、双向各5包／313和298字节、合计611字节、持续4591微秒、最终1条`closed`流，特征CSV2行且握手完成1，两个drop为0，数据链核对通过。本次退出码、B／C发生器PASS、运行时长及板端指纹仍未提供，不补写。已纠正演示文档的静默过期说明：过期按新捕获包的事件时间推进，不随静默墙钟等待自动发生，故`Expired flows: 0`正常。下一步补齐已有运行的退出状态和消息核对证据，再整理招聘会演示／简历，不扩功能、不重做一小时实验。详见[jobfair_demo.md](jobfair_demo.md)；以下旧“待重测”描述是历史快照。
- 最新TCP演示排查（2026-10-11）：本轮检查HEAD为`f84cb91`，与远程同步且文档更新前工作区干净，构建及37/37项x86_64 Debug CTest通过（8.88秒）。首次板端TCP目录`/home/cat/nfa-demo-tcp.BVQJ81`只有约45秒零包报告、缺最终汇总、特征CSV为0字节；用户确认Ctrl+C且退出码未保存，不能确认SIGPIPE，也尚无B／C通信证据。无管道10秒静默SIGTERM对照已由用户验证：`/home/cat/nfa-demo-close.Ew7nns`，退出0、完整0包／0流汇总、两个drop为0，特征CSV1行；后端received原值5，不猜测来源。下一步改为直接日志重定向并自动定时收尾，提前准备A／B／C三个终端，在A采集期间运行服务端和虚拟机客户端，不等A结束才通信，不按Ctrl+C；完整TCP重测尚未通过。详见[jobfair_demo.md](jobfair_demo.md)和问题5.20；演示程序仍来自`7bce180`，本次只更新文档，由用户提交。
- 招聘会收尾优先级（2026-10-11）：用户准备10月14日下午招聘会，暂停DNS后续、LLM实验及模型上板，把已有采集分析服务的短时演示、简历和面试材料作为主线；AI保留为离线实验，不能宣称可靠异常预警。虚拟机HEAD为`7bce180`，本轮文档更新前工作区干净，重新构建及37/37项x86_64 Debug CTest通过（7.92秒）。独立官方SDK ARM64演示产物在`/home/zcb/build/netflow-analyzer-jobfair-arm64-7bce180/bin/netflow-analyzer`，62,504字节、直接依赖libpcap／libc、程序自身只要求`GLIBC_2.17`、无构建机RPATH，SHA-256为`47ade7acd3e5b30029f9264b4052d92322718d19a25b3c0af5f22832da3b1aaa`。用户提供板端`eth0`实测：4包全部完整处理、两个drop为0、双向各2包／196字节、1条特征记录且数值完全吻合，目录`/home/cat/nfa-demo-icmp.1yiMYo`；未贴出板端SHA／ldd／帮助／退出码，不替其补写通过。板端源码仍为`/home/cat/workspace/netflow-analyzer`的`feature/nonroot-service`、`7526fcc`；演示程序与旧安装程序隔离，服务保持`inactive/disabled`。下一项为有界TCP设备消息演示，尚未执行；完整步骤及边界见[jobfair_demo.md](jobfair_demo.md)。板端自动化仍只确认历史18项，不将本次手工验收计入37项。
- 当前DNS进展（2026-10-02）：用户已实现并提交`492bc28 feat(protocol): add bounded DNS header parser`，检查时与`origin/feature/flow-features`同步、工作区干净。本机复测构建、DNS专项测试与37/37项x86_64 Debug CTest通过（7.80秒）；DNS固定头以局部游标／结果读取12字节，失败不修改调用者状态。尚无域名解析、UDP接入或DNS板端验收，ARM64仍只确认此前18项网络分析器测试。先前五份LLM验收文档也已随本次提交保存，不再报告为待提交；本轮新增DNS验收文档仍由用户提交。下一步教用户实现有界域名解码，助手不直接修改C源码。旧LLM原生参考实验暂缓，不将以下历史“下一步”视为当前指令。
- 最新端侧摘要验收（2026-10-02）：原三例质量未通过，normal简化prompt对照又虚构“大量异常连接”；固定事实抽取随后正确复述4条连接和历史每分钟3到5条，却遗漏失败0次，严格完整性仍未通过。事实抽取目录`/media/usb0/netflow-analyzer-data/logs/llm-normal-facts-v1.kXkUhp`，退出0、墙钟25.86秒、生成5.27 token/s、峰值RSS约730.6 MiB；这是用户提供的实际日志，不是助手板端重跑。下一步先在新目录`/home/zcb/build/llama-cpu-x86-ref-v1`构建固定源码的原生CPU参考，再比较同GGUF／prompt／参数输出；参考尚未构建，零计时、错误判断和遗漏原因均未独立定位，不直接接在线告警。详细事实见部署手册第8.1～8.4节及问题7.4～7.6。本次VM主项目构建与36/36项CTest通过（最新复测7.69秒），HEAD为`f7a43cd`并与远程同步；五份验收文档仍待用户提交，不修改源代码或自动提交。
- 最新补充（2026-10-02）：独立Qwen2.5-0.5B-Instruct Q4_K_M语言模型已在LubanCat-2N CPU上完成首次推理，退出0、生成约5.26 token/s、峰值RSS约687.4 MiB。上游固定提交、SDK构建、用户输入的GCC 9兼容补丁、模型／产物SHA-256和Shell语法均见[local_llm_deployment.md](local_llm_deployment.md)。模型的网络流定义回答不准确，仅工程冒烟通过；没有网络摘要输入、在线告警、剪枝或常驻LLM服务。本项与尚未上板的TCP分类基线分开。
- 本次实际检查：HEAD为`1e747c3 docs(ml): record normal TCP diagnostic and feature collisions`，分支与远程同步；用户工作区已有`CMakeLists.txt`、`scripts/tcp_feature_preprocessing.py`改动及新增模型产物模块／测试，不覆盖、不自动提交。`cmake --build build`成功，当前启用ML的x86_64 Debug工作区36/36项CTest通过（7.55秒），包含尚未提交的`tcp_model_artifact_tests`。以下35项记录描述此前已提交基线，ARM64网络分析器仍仅确认历史18项；LLM手工验收不计入CTest。
- 当前功能分支：`feature/flow-features`；
- 远程仓库：`git@github.com:Devi1Fairy/netflow-analyzer.git`；
- 当前分支在既有非root systemd、TCP状态和流表生命周期功能之上，已提交流特征、标签审计、样本连接、TCP编码与加载、张量适配、训练侧预处理和线性分类；本次正常TCP诊断前最新提交为`2f6be22 feat(ml): add TCP linear classification baseline`，已同步远程，诊断前工作区干净，实际接手时须以`git log`和`git status`为准；
- TCP状态机、流记录接入、终端显示、CSV字段和确定性三次握手验收均已提交；接手时仍须先检查工作区，不能覆盖用户后续未提交改动；
- 离线CLI已新增可选`--flow-idle-timeout SECONDS`，默认继续整文件聚合；确定性6包ICMP验收已证明30秒阈值会导出2包旧流，并把同键后续4包建立为新流。普通流CSV和特征CSV均精确产生两条顺序一致的记录；
- 当前正式版本宏为`0.2.0`；
- 已有标签：`v0.0.1`、`v0.1.0`、`v0.2.0`；
- Debug构建目录：`/home/zcb/workspace/netflow-analyzer/build`；
- 本地主程序：`build/bin/netflow-analyzer`；
- 官方SDK交叉构建目录：`/home/zcb/build/netflow-analyzer-lubancat-sdk-release-v2`；
- 通用GCC交叉构建目录：`/home/zcb/build/netflow-analyzer-generic-sysroot-release`；
- 当前`feature/flow-features`的x86_64 Debug普通配置注册32项CTest；通过`NFA_ML_PYTHON_EXECUTABLE`显式提供项目`.venv`后，额外注册PyTorch张量、特征预处理和线性基线测试，本次全量达到35/35。LubanCat-2N ARM64原生Debug仍以此前18项基线为已验证状态，新增特征、数据集和模型输入模块尚未上板回归，不能写成板端32或35项已通过。此前单次扫描优化已完成Ubuntu `lo`和LubanCat-2N物理网卡300流手工验收；优化版官方SDK ARM64产物只要求`GLIBC_2.17`，板端得到300次操作、44次淘汰、256条最终流和零drop；TCP状态功能也已在`lo`真实HTTP/1.0连接中处理12包并最终输出`closed`，两个drop字段均为0。
- 第一版流特征链已经完成：`flow_features_t`从双向流记录提取版本化模型输入，`--feature-csv`支持离线和带`--count`的有限实时模式；运行期间的过期流与淘汰流立即导出，停止时再导出最终剩余流。Ubuntu手工验收分别得到“2包过期流+4包剩余流”以及“44条淘汰流+256条剩余流=300条数据行”。CTU-13受截断报文和事务边界限制，未形成可靠训练数据；后续IoT-23 TCP候选链已经完成首次离线线性基线训练与验证，但模型持久化、ARM64推理和在线异常预警尚未实现，当前效果限制见TD-042。
- `flow_sample_id_v1`现已根据抓包来源、规范化流键和生命周期时间生成稳定SHA-256标识；`flow_sample_metadata_v1` sidecar通过`feature_row_number`把特征行连接到样本身份、匹配状态、候选数量、监督标签和可训练标志。当前契约仅把逐流唯一匹配的正常或恶意样本标为可训练；IoT-23实测存在跨流复用，仍需额外筛选和边界验证，不能直接沿用这一标志生成可靠训练集。身份字段不进入`flow_features_v1`。
- 新增标签审计测试前，当前分支已经通过x86_64 Release优化构建的20项CTest；独立`build-sanitize`以ASan和UBSan插桩重新构建并通过相同20项测试，没有Sanitizer或泄漏报告。新增的标签审计、身份规范化、样本元数据和匹配审计四项Python数据契约测试目前只在x86_64 Debug测试树执行；Release和Sanitizer构建尚未重新配置。Sanitizer配置通过CMake命令行参数临时建立，尚未成为仓库Preset。
- CTU-13 Scenario 7已选为第一轮公开数据集对齐试点。标准库Python工具严格审计官方双向`.binetflow`标签，真实114077条记录按保守规则得到63条`malicious`、1669条`benign`和112345条`exclude`；确定性合成CSV测试不依赖外部数据文件。公开全流量文件已经下载并通过bzip2 CRC检查，实际为7466160包的单接口Ethernet pcapng，捕获内容按42至66字节截断但保留原始线路长度。前1000包小样本得到604个`complete`、396个`truncated`、47条最终流、47行特征和零`flow_rejected`。标签侧现已把`CEST +02:00`时间精确转换为UTC Unix微秒，并按与C端一致的IP、端口规则生成双向流身份；首条标签时间与PCAP首包时间精确相等，TCP、UDP、ICMP和不支持协议边界有确定性测试。恶意DNS子集的45包空闲阈值扫描表明：关闭分段时2条流全部歧义；1秒最多得到5条唯一匹配但切成10条流；3秒和5秒均得到4条唯一匹配，其中5秒只产生6条流；10/30/60秒均只有1条唯一匹配，120秒没有唯一匹配。公开UDP包只保留到42字节UDP头，缺少DNS事务ID，而官方标签可在相同五元组相隔约1.258毫秒时生成不同记录，因此任何单一空闲阈值都不能精确重建Argus DNS事务边界。sidecar能够如实记录这些结果，但当前仍未形成正式训练集，也没有训练模型或在线推理。
- 第二个试点IoT-23 v2 Scenario 3-1已有完整标签文件审计、身份规范化和候选索引：真实156103条标签全部转换成功，`identity_unsupported=0`，其中73944条单包记录为零时长；标签组为151567条恶意、4536条正常、0条排除。索引包含81404个规范化双向流键，其中71026个键有多条候选、单键最多185条。定向8包PCAP试点在默认聚合下得到2条同标签歧义流；3秒空闲分段后得到与前四条Zeek记录逐条对应的4条唯一恶意候选。前200包有184包完整处理、16包不支持、零流表拒绝；默认模式27条流中4条唯一候选、23条同标签歧义，3秒模式60条流中58条唯一候选（41恶意、17正常）、2条同标签歧义。普通流CSV解析已抽到数据集无关的`flow_csv_identity.py`，CTU-13和IoT-23直接共用；合成5行CSV和独立CLI成功/失败测试均由CTest覆盖。前200包是连续前缀而非留出集，唯一候选不等于已验证正确标签；前500包的逐行审查sidecar已生成，但全场景质量尚未完成，不能把标签数或索引键数误称为可训练样本数。
- 前200包的只读全局复查进一步发现：默认模式4条逐流唯一候选指向4条不同Zeek记录；3秒模式58条逐流唯一候选仅指向51条不同Zeek记录，有5条Zeek记录被多条C流复用。正式CLI现已输出三项独立复用计数，真实样本分别得到默认模式`4/0/0`及3秒模式`51/5/7`（不同标签／复用标签／额外分配），与只读检查一致。CLI仍不执行全局一对一训练筛选，不能把`matches_unique=58`直接当成58个独立监督样本。
- IoT-23区间分类为逐流`unique`结果返回`unique_candidate_index`，表示该规范化流键下标签候选列表中的零基位置；其他状态返回`None`。它与流键组合可在当前索引中区别标签记录，供CLI计算跨C流复用；不是跨文件稳定标签ID。合成单流、复用、不同流键同位置和失败路径测试以及x86_64 Debug 28项CTest全部通过；Release、Sanitizer和ARM64尚未按新增Python测试重跑。
- 前200包的5条复用标签已做只读包级抽查：3条Benign NTP和1条Malicious TCP C&C标签的Zeek包数分别与多个C流分段之和相等，首末时间一致；另一条TCP C&C的完整Zeek记录有592包，样本前缀只覆盖其中21包。代表性TCP流后两段无新SYN，支持“3秒空闲阈值将同一Zeek记录切开”的解释；不能据此认定全场景匹配正确或将58条逐流唯一流作为58个独立训练样本。下一步扩大有界样本时先监测256槽流表拒绝和输出完整性。
- 同一PCAP前500包扩样已完成：452包完整处理、48包不支持、零流表拒绝，CSV行数与导出流数相符。默认模式导出69条流，4条逐流唯一、65条同标签歧义；3秒模式过期171条、剩余3条，共导出174条流，其中160条逐流唯一（116恶意、44正常）、14条同标签歧义。160条逐流唯一流仅涉及138条不同Zeek记录，5条标签被复用、22次额外分配；默认模式4条逐流唯一流对应4条不同记录。完整计数、样本指纹和边界见TD-040；不得把138视作可靠训练样本，也不得把同一连续前缀当作阈值泛化验证。
- 前500包复用来源已只读定位：22次额外分配中17次来自同一条恶意TCP标签被18条C流命中，另一条恶意TCP标签贡献2次，3条正常UDP标签各贡献1次。该长TCP标签的时间区间远超前500包跨度，仍须保留前缀与逐流匹配边界；不能将未复用的133条记录直接宣布为可靠训练集。
- IoT-23正式CLI新增按C流统计的`matches_unique_unreused`／`matches_unique_reused`：前500包3秒模式为`133/27`，其中27条是命中5条复用标签的全部C流，而不只是22次额外分配。默认调用仍只报告摘要；带齐审查文件参数时已在真实前500包上生成174条逐行记录，三态计数为14／27／133；尚无最终训练筛选，也未修改元数据的`is_trainable`语义。x86_64 Debug 28项CTest通过。
- 新增`audit_flow_csv_with_unreused_rows`返回摘要和从1开始的未复用唯一流数据行号集合；旧`audit_flow_csv`保持只返回摘要，CLI输出不变。合成6行得到`{5}`；真实前500包3秒模式返回133个行号，第37数据行（复用长TCP首段）不在其中。初次回归曾因`unique_label_records`赋值缩进在`raise`之后导致两项测试失败，修正后针对性2项及完整28项CTest通过。行号只是普通流CSV内的位置；后来CLI已写出审查sidecar，但这一集合单独不能证明与特征CSV逐行对应或成为最终训练名单。
- 离线生命周期集成测试现额外按数据行号配对普通流CSV和特征CSV，并用流CSV重算协议、时长、包数、捕获字节和线路字节，覆盖过期流及结束时剩余流；本机x86_64 Debug 28项CTest通过。同一次IoT-23前500包离线运行的默认69对和3秒174对记录也已只读逐行核对，五项均一致。这是当前输出顺序的验证，不允许把不同运行或不同参数生成的CSV按行连接，更不代表133条未复用候选已有可靠监督标签。
- `classify_iot23_row_review`现以同一份审计的从1开始的流数据行号、原始匹配状态和未复用行号集合返回三类审查结果；六行合成样本中第1、6行均为`unique_reused`、第5行为`unique_unreused_candidate`。真实前500包3秒模式只读复核为`not_unique=14`、`unique_reused=27`、`unique_unreused_candidate=133`，合计174；本机完整28项CTest通过。该纯函数不改`FlowSampleMetadata.is_trainable`或CSV模式，不写sidecar；候选仍须后续标签边界和独立样本验证。
- 新模块`iot23_flow_review.py`固定`iot23_flow_review_v1`的16列表头，并用`csv.writer`向借用的文本流写出稳定换行；列包括`feature_row_number`、`sample_id`、`capture_id`、双向流身份、四态匹配信息和三态`review_status`，故意不设`is_trainable`。最初仅实现表头，随后补充单条记录与借用流批量写出；当前CLI已由合成测试和真实前500包文件验证落盘，x86_64 Debug 28项CTest通过。
- `validate_iot23_review_record`新增写出前结构校验：先用`build_sample_metadata`重新验证不可变身份和四态匹配信息，再验证三态审查值与`unique`／非唯一状态的组合；审查常量已移至`iot23_flow_review.py`，审计模块只导入。合成测试覆盖三种合法组合和三种非法组合，x86_64 Debug 28项CTest通过。它既不写数据行，也不负责从标签索引证明复用真假；真实审查值仍须来自同次完整审计。
- `write_iot23_review_csv_record`现先执行上述验证，再向借用的文本流写出一条16列记录；IPv4由整数转为点分地址，缺失标签为CSV空字段。测试核对两条完整数据行，并证明非法组合或损坏的元数据不会改变原有输出；本机x86_64 Debug完整28项CTest通过。它不从Zeek索引推导审查状态，也不保证底层写入异常时文件级原子性；这些文件级责任现由CLI处理正常异常，但强杀或断电仍可能留下部分文件。
- `write_iot23_review_csv`现对可定位的C流CSV做两遍处理：先完整审计并取得未复用唯一行号，再重读同一输入，按流行号生成身份、三态审查结果及16列CSV；返回值沿用原审计摘要。六行合成测试核对表头、逐行状态和摘要，缺字段行在第一遍失败且不写输出；本机x86_64 Debug 28项CTest通过。输入与索引在两遍之间必须不变，行数一致检查不能证明相同行数的内容未变；CLI现负责预检、独占创建和正常异常清理，但借用流函数本身仍不负责文件生命周期。
- `validate_flow_feature_alignment`现可只读核对普通流CSV和特征CSV的完整表头、版本、数据行数及逐行协议、时长、包／字节合计和TCP阶段；合成六行匹配、同数错位及缺行测试和本机x86_64 Debug 28项CTest通过。该函数只借用输入流，现已接入CLI作为创建sidecar前的检查；它不证明两文件必由同一运行生成，也不验证均值等全部特征字段，源文件仍须成对保存。
- IoT-23审计CLI现将`--feature-csv`、`--capture-id`、`--review-output`作为成组选项：预检两份CSV后才独占创建sidecar，成功后才输出旧摘要；已存在文件保全、预检失败不创建、创建后正常异常清理均由合成CLI测试覆盖，本机x86_64 Debug 28项CTest通过。真实前500包3秒模式的两份现有CSV通过174对预检，CLI已写出`idle-3s-review.csv`：175个物理行、174条数据，三态`not_unique=14`、`unique_reused=27`、`unique_unreused_candidate=133`，SHA-256为`4cfd7af6213d2d5b1a00217e4fa9e17a43d359a314806a44f501191ffdf81baa`。正常异常清理不覆盖断电、`SIGKILL`或输入在多遍处理期间变化，不能称为完全原子提交。
- 新增纯函数`classify_interval_boundary`区分C流和单条Zeek标签的精确一致、两种包含、部分相交及不相交；7个合成边界场景和逆序输入拒绝已覆盖，本机定向与完整28项CTest通过。只读重查真实sidecar的133条未复用唯一候选：`exact=132`、`flow_inside_label=1`，其余为0。唯一非精确者是特征行173，C流终点`1526756934497363`微秒恰是前500包PCAP最后包时间，标签终点`1526756936497278`微秒，晚`1999915`微秒；这支持样本前缀截断而非已验证完整标签。函数不写sidecar或决定训练准入；后续CLI已将该边界摘要固化，下一步重点转向防泄漏数据切分。
- IoT-23审计CLI现可选`--boundary-summary`，复用完整流审计的未复用行号，只读重扫同一流CSV并在原摘要后输出五类边界计数；默认行为和已有sidecar模式不变。合成测试覆盖成功、坏CSV无半份stdout，以及与`--review-output`冲突时在创建任何目标前退出；初版检查放在写文件之后的缺陷已修正。x86_64 Debug 28项CTest通过，真实前500包3秒模式得到候选总数133、`exact=132`、`flow_inside_label=1`、其他为0。两遍读入期间输入仍须保持不变，时间一致不等于已可训练。
- 新增独立切分清单校验器`validate_ml_split_manifest.py`，以`capture_id,source_capture_id,split`区分提取结果和原始PCAP来源；同源前缀不得跨训练／验证／测试集合，训练和验证各至少需要一个来源。合成测试覆盖合法同组、跨组泄漏、单来源和重复ID，失败时stdout为空；本机x86_64 Debug完整29项CTest通过。此前仅用Scenario 3-1前500包作为训练来源手工运行返回1，提示缺少独立验证来源，符合预期。仓库外真实清单`iot23-v2/splits/source-split-v1.csv`现把3-1前500包声明为`train`、8-1完整抓包声明为`validation`，两个不同来源通过校验：`captures_total=2`、`source_groups=2`、`train=1`、`validation=1`、`test=0`。校验器不连接特征行、不证明标签可靠，也不决定最终训练准入。
- 边界审计新增不可变的精确候选行号返回值，并以保留旧双返回值的包装函数维持CLI兼容；合成测试覆盖非精确空集合与精确`{1}`，本机x86_64 Debug完整29项CTest通过。真实只读重查：3-1前500包有133条未复用唯一候选，其中132行精确，该文件第173行排除；8-1完整抓包8187条未复用唯一候选全部精确。行号仅对应本次各自CSV的数据行，不能跨抓包解释；与同次特征CSV及review sidecar的后续只读连接见下一条。
- 只读`collect_exact_reviewed_candidates`现按数据行号连接同次特征CSV与IoT-23 review sidecar，只接受精确且未复用的唯一候选，并把身份／标签与特征值分开保留。合成测试覆盖只选精确行、空集合及多种错位／状态矛盾／越界拒绝，本机完整29项CTest通过。真实只读验收已在流／特征逐行对齐后完成：3-1前500包选出132条候选（恶意94、正常38），8-1完整抓包选出8187条（恶意6006、正常2181）。这些仍是内存候选对象，尚未把来源切分、输入文件指纹和最终准入规则绑定成持久化样本表，也未训练模型。
- 候选与来源切分现可通过`assign_candidates_to_splits`在内存中绑定；真实只读串联确认`train=132`、`validation=8187`、合计8319条。新`iot23_candidate_samples_v1`固定前8列版本／身份／来源／行号／切分／候选标签，之后是`flow_features_v1`特征列；`write_iot23_candidate_csv`先校验全部候选，再写入借用文本流，合成测试覆盖逐列输出和重复样本ID失败时无部分输出。本机x86_64 Debug完整29项CTest与`git diff --check`通过。当前未把真实候选落盘，也未实现写入故障下的文件级原子性或输入指纹绑定；132条训练候选仅来自3-1前500包，虽与8-1验证来源隔离，仍只能用于流水线试运行，不能作为正式模型评估。不要从8-1按行随机挪样本来凑训练比例；后续须增加独立来源，并为最终测试保留未参与调参的来源。
- 用户已运行真实候选CSV内存往返验收，结果与本机复核一致：3-1前500包`aligned=174, exact=132, selected=132`，8-1完整抓包`aligned=9295, exact=8187, selected=8187`；`io.StringIO`写出并读回8319条、20列，训练132／验证8187，候选标签分布为训练正常38／恶意94、验证正常2181／恶意6006，最终显示`[PASS] real IoT-23 candidate CSV memory round-trip`。命令还检查表头后12列对应特征列和样本ID唯一；没有创建磁盘候选文件，也没有验证文件级原子性或标签真值。此步完成后由用户提交文档，下一步再设计真实候选文件的安全落盘。
- 候选文件独占发布层现由用户实现：`iot23_candidate_file.py`先在同目录写临时CSV、刷新并`fsync`文件，再用不覆盖已有目标的硬链接发布；`finally`清理本次临时文件。测试覆盖正常两行、目标已存在时原字节保全、重复样本ID失败时目标和临时文件均不存在，本机完整29项CTest与`git diff --check`通过。真实8319条候选后来已在虚拟机ext4完成受控落盘和读回，结果见下一条；当前实现依赖支持硬链接的文件系统，不适用于直接假定为VFAT的板端SD卡，强杀／断电后的临时文件及父目录持久性仍未验证。
- 真实候选文件现已独占发布到虚拟机ext4的仓库外`iot23-v2/splits/pilot-candidates-v1.csv`，用户和本机只读复核一致：8320物理行＝1表头＋8319候选，20列，大小1948681字节、权限`0600`，SHA-256为`3cedc5667c7e622ace66e80d2c4a2b213f539291956e1303945720018b99a86e`。发布脚本读回检查样本ID唯一、两份输入分别`aligned=174/9295`与`exact=selected=132/8187`、来源切分训练132／验证8187，候选标签分别为训练正常38／恶意94、验证正常2181／恶意6006，最终输出`[PASS] real IoT-23 candidate file publish and read-back`；发布时仓库干净。该文件不加入Git，也不等于标签真值或可报告的模型效果；更多独立训练来源与最终测试来源仍待完成。
- 用户提供的仓库提交、程序二进制及13项仓库外数据文件SHA-256已由本机独立重算并记录在[IoT-23试点候选数据指纹](iot23_pilot_provenance.md)。这解决了当前试点输入／输出文件的人工指纹记录，不等于发布流程已自动校验输入或将其与产物绑定；训练与正式评估仍未开始。
- 第三份独立原始来源候选IoT-23 v2 Scenario 42-1已下载至仓库外`iot23-v2/scenario-42-1.9y5VmS/`，PCAP和Zeek标签SHA-256分别为`85c6eb89f2f8ee5d58f46bd3ebe70532649ce7454468c5b3025476417140fb00`、`269fa1b22d9a37e159cf41b81a213a0032d21306f5d39b4c20cb0d211b04e8aa`。PCAP为24485包，标签4426条中恶意仅6、正常4420，身份全部支持。`capinfos`的`inferred: 45 bytes`来自唯一一条45/91字节的截断TCP包（最后第24485包），不是整个PCAP的抓包上限；文件头上限为262144字节。随后以3秒空闲阈值完成C端离线处理：24485包＝完整22748＋截断1＋不支持1736，零畸形／流表拒绝，1736个不支持包由独立ARP计数核对；过期4597条流加结束时1条，对应普通流和特征CSV各4598条数据行。只读审计为唯一4337（恶意6、正常4331）、同标签歧义261；未复用唯一3799中边界精确3764、`label_inside_flow`35。进一步核对6条恶意唯一流全都未复用但非精确：起点相同，C流终点比标签晚248至498微秒；逐包查看确认标签止于对端FIN/ACK，C流还收进其后本机的零载荷纯ACK。严格准入后恶意候选为0，不能将该场景直接补进恶意训练或最终测试，也不能未经独立验证就放宽所有非精确边界；尚未生成逐行review sidecar或改动切分清单。完整路径与CSV指纹见TD-041。
- Scenario 8-1单来源二分类开发试验已新增只读分组切分脚本`split_iot23_single_source_pilot.py`，但没有修改正式跨来源切分清单、候选CSV或C源码。真实候选8187条只有1188种完整特征模式，最大的一组5058条恶意流都呈单包74字节、`syn-seen`；它们是不同流而非应直接删除的重复事件。脚本按完整特征模式成组、每类最大组留训练侧、其余组按稳定摘要分配，得到训练7447条（正常1728、恶意5719，832组）及验证740条（正常453、恶意287，356组），相同模式不会跨侧。`py_compile`、真实文件运行和新增不依赖外部数据集的合成分组测试已通过，本机完整CTest为30/30；新增测试尚未在ARM64、Release或Sanitizer构建重跑。该切分仅可供后续验证模型输入流程，仍是同一个原始PCAP，不能用于声称跨场景泛化或异常预警；训练模型前须先确认下述协议捷径，并计划独立来源测试。
- 训练前的只读协议捷径审计进一步发现：Scenario 8-1的6006条恶意候选全是TCP，2181条正常候选中2179条是UDP、只有2条是TCP；当前同源分组验证侧更是453条正常全为UDP、287条恶意全为TCP。只按协议判别即可得到740/740验证正确，故在该切分上训练复杂模型即使高分也不能证明识别异常。拿另一来源Scenario 42-1的3764条严格精确正常候选做反例，协议规则会误报其中1440条正常TCP（约38.3%）；该来源严格准入的恶意候选为0，不能据此测恶意召回。下一步先把协议单列基线作为显式对照，再寻找同时含正常TCP与恶意TCP等反例且可独立验证的来源；本发现不改变正式来源隔离清单，也不表示已训练模型或实现预警。
- IoT-23 v2 Scenario 8-1的仓库外原始PCAP与对应Zeek标签已下载并在本机复核SHA-256，分别为`80dcc2602519479ddcde889fa902fee19a76696630811452f8df38888af894f2`和`4877ca8f0f01902fbd18d28b7d06cb3d0be082355b7f2c8862c9deef1782eb8a`。PCAP为单接口Ethernet、微秒时间精度、严格有序的23623包；标签审计共10403条：恶意8222、正常2181、排除0，身份全部支持，零时长6185，TCP 8224、UDP 2179。完整PCAP以3秒空闲阈值离线处理后退出码0：16677包完整、6946包不支持（独立确认为ARP）、零截断／畸形／流表拒绝；9294条过期流加1条剩余流，普通流与特征CSV各9295条数据行，逐行对齐检查全部通过。只读IoT-23匹配审计得8187条逐流唯一且未复用候选（恶意6006、正常2181，时间边界全部`exact`）、1108条同标签歧义，未匹配和冲突歧义均为0。逐行审查sidecar在`run-3s.enKX9W/review.csv`，SHA-256为`bbc9fcedec151694d1bfd4b77df480859bc54bbd466db407610353a74e5ac60c`；只读核对9295条数据、连续特征行号、唯一样本ID及8187／1108两类审查状态。`label_group=malicious`的7114行包含1108条歧义流，不能直接用于训练。随后真实来源切分、内存候选绑定及候选CSV的真实落盘已完成，最终训练准入与正式评估仍待确定，不能把当前结果直接称为可靠验证集。
- Scenario 3-1完整PCAP的3秒空闲分段已由用户运行并由本机只读复核，结果目录为`/home/zcb/datasets/netflow-analyzer/iot23-v2/scenario-3-1/full-3s.Tqx2iv/`。496959包＝完整491299＋不支持5660，零截断／畸形／流表拒绝；过期115971条加剩余1条，流／特征CSV各115972条数据，现有逐行对齐检查全部通过。标签审计为唯一70875、同标签歧义45066、冲突歧义31、未匹配0；唯一且未复用65476条，其中精确59667、`flow_inside_label=3`、`label_inside_flow=5806`。精确候选正常4121（ICMP 1165、TCP 2221、UDP 735）、恶意55546（全部TCP），故同协议TCP开发候选为57767条，正常／恶意约1:25。冲突、同标签歧义、复用、非精确行保持排除，完整数据不能和重叠的前500包重复计入或跨集合使用。下一步使用现有CLI为完整3-1生成review sidecar并连接严格候选，再推进训练输入；尚未训练模型，独立来源评估及类别失衡仍需处理。完整计数、文件SHA-256与结论边界见TD-041。
- 完整Scenario 3-1的`full-3s.Tqx2iv/review.csv`现已生成，SHA-256为`42976540df4bbfd317ea013d11c59c3e059ca9c784263c4e34749befef2a42ee`。本机重新核对流／特征对齐、精确行号和该审查文件的候选连接，得到59667条严格候选，其中TCP正常2221、恶意55546。进一步与Scenario 8-1严格TCP候选（正常2、恶意6006）按原始来源分别绑定train／validation，并已由用户独占发布到仓库外`iot23-v2/splits/tcp-pilot-candidates-v2.csv`。本机独立读回63775条20列、14773716字节，样本ID全部唯一、协议全部为6，训练57767／验证6008；SHA-256为`9fe3b49d52b48ad3bd1a301e1ace8017a499e46b1f061f40cfaac3051795369a`，与用户一致。此次切分在命令内声明并经现有清单接口验证，未改旧`source-split-v1.csv`，原前500包未合入。下一步准备隔离训练环境、模型特征编码和仅在训练侧拟合的预处理；尚未训练模型。8-1正常TCP仅2条，不足以估计误报率；42-1可作正常TCP外部诊断，但已参与数据审计，不能称为未观察的最终测试集。新增文件指纹见`docs/iot23_pilot_provenance.md`。
- 虚拟机项目内`.venv`训练环境已建立，本机复核Python 3.12.3、NumPy 2.5.3、PyTorch 2.14.0+cpu，`torch.version.cuda=None`；张量平方和为13、反向求导梯度为[4,6]，`pip check`无依赖冲突。`.venv/`已由现有Git忽略规则排除，`requirements-ml.txt`固定已验证的NumPy与CPU版PyTorch直接依赖，`pip --dry-run`确认当前环境满足声明。`tcp_model_input.py`现将TCP候选编码为9个原始数值特征加9种TCP阶段固定独热编码，共18维，并单独返回正常0／恶意1标签；协议和TCP适用标志仅用于验证，身份／来源／切分／标签不进入特征向量。合成测试覆盖列顺序、状态位置、调用者映射不变、元数据隔离及非法模式／数值／比例／标志拒绝；真实63775条候选也已手工全部编码成功。本机完整CTest为31/31；测试本身只用标准库和仓库模块，不依赖PyTorch或仓库外数据。下一步读取完整候选CSV并构造分集合张量，再只用训练集合拟合预处理参数；模型尚未训练。
- `tcp_candidate_dataset.py`现把完整候选CSV读取为不可变的`train`、`validation`和可选`test`集合，每条保留18维数值元组、0／1标签及可追踪`sample_id`。加载器校验20列表头、候选版本、样本ID格式与唯一性、抓包ID、集合值、同一捕获内严格递增的特征行号、捕获分配稳定性和原始来源不跨集合，并复用`encode_tcp_candidate`检查单行特征；它借用但不关闭调用者文本流，任一错误均抛出`ValueError`且不返回部分数据。合成测试覆盖正常三集合以及坏表头、重复样本、来源泄漏、行号倒退、缺验证集和非TCP输入；真实`tcp-pilot-candidates-v2.csv`只读加载得到训练57767条（正常2221、恶意55546）、验证6008条（正常2、恶意6006）、测试0条，全部宽度18且文件SHA-256仍为`9fe3b49d52b48ad3bd1a301e1ace8017a499e46b1f061f40cfaac3051795369a`。本机x86_64 Debug完整CTest为32/32；Release、Sanitizer和ARM64尚未按新增测试重跑。下一步建立薄的PyTorch张量适配层，再只用训练集合拟合标准化参数；尚未创建张量、拟合预处理或训练模型。
- `tcp_tensor_dataset.py`现把上述不可变编码集合复制为CPU `float32`张量：特征保持二维`[N,18]`，标签保持一维`[N]`以适配后续单logit二分类损失，`sample_id`继续作为张量外的不可变追踪信息。适配器检查长度、特征宽度、有限值、0／1整数标签、集合内及跨集合样本ID唯一性，并为可选空测试集显式创建`[0,18]`；转换后的张量拥有独立存储，但`frozen`数据类不代表Tensor内容不可变。PyTorch测试通过可选CMake缓存变量`NFA_ML_PYTHON_EXECUTABLE`注册，普通C/libpcap构建不依赖PyTorch；本机显式使用`.venv/bin/python`后完整CTest为33/33。真实63775条候选得到训练特征`[57767,18]`／标签`[57767]`、验证`[6008,18]`／`[6008]`、测试`[0,18]`／`[0]`，恶意标签计数仍为55546和6006，张量本体共4846900字节（约4.622 MiB）。下一步只在训练特征上拟合标准化参数，再把相同参数应用到训练、验证和未来测试集合；尚未拟合预处理或训练模型。
- `tcp_feature_preprocessing.py`现以版本化`tcp_preprocessing_v1`状态实现训练侧拟合：只对持续时间、包数、两种总字节数和两种平均包长这前6个非负规模特征执行`log1p`与总体标准差标准化；两个不平衡比例、握手标志和9位TCP阶段独热保持0／1原值。拟合以`float64`计算并把均值／尺度保存为不可变Python元组，常量或近常量列用尺度1避免除零；应用阶段复制特征与标签，不修改输入，只允许验证／测试复用现有状态。合成测试覆盖训练参数唯一来源、常量列、空测试集、输入所有权、状态契约、非法尺度和负输入；本机启用ML解释器后完整CTest为34/34。真实训练集拟合后的6列均值在`2e-5`内为0、总体标准差为1，验证集前四列均值约`-0.69`至`-0.71`，说明来源分布不同且验证数据未参与拟合；后12列、标签和样本ID全部保持不变。六个`log_mean`依次为`7.623181018,1.044791771,4.866489490,4.866489490,4.313382133,4.313382133`，`log_scale`为`7.388617733,0.351575494,0.548843847,0.548843847,0.031835683,0.031835683`。当前两对捕获／线路统计得到相同参数，先作为本批数据特征重复现象保留，待训练基线后通过相关性或消融复审；尚未持久化参数、建立DataLoader、处理类别失衡或训练模型。
- `tcp_linear_baseline.py`新增`tcp_linear_v1`：18权重与1偏置、全0初始化、CPU全批量Adam100次更新、学习率0.05；类别权重只用训练标签计算为`2221/55546`，固定logit阈值0，不读验证侧拟合或更新。直接合成测试及启用ML解释器后的完整35项CTest通过。真实v2候选SHA-256未变，训练57767条、验证6008条、测试0条；加权训练损失由`0.053299639374017715`降至`0.0014024219708517194`。训练混淆计数为`TN=2206, FP=15, FN=15, TP=55531`；验证为`TN=0, FP=2, FN=0, TP=6006`，全部判恶意，与恒判恶意对照完全相同。99.9667%的验证准确率不能当作有效泛化，正常侧仅2条也不能估计部署误报率。模型只在临时进程内训练，尚未保存或上板；完整命令和解释见[tcp_linear_baseline.md](tcp_linear_baseline.md)。下一步保留基线并诊断独立正常TCP，再推进模型／预处理持久化和加载一致性；不要只为验证分数调阈值。
- Scenario 42-1正常TCP诊断已完成：保持原3-1训练数据和所有参数，复用42-1流／特征4598对、边界精确3764条候选，在内存生成审查sidecar并筛出1440条正常TCP，不保存数据文件或改切分。固定阈值下`TN=996, FP=444`，误报率30.8333%；444条误报全部为`syn-seen`，850条`fin-seen`和146条`reset`无误报。215条正常候选与训练恶意的原始18维向量完全一致，代表性单包74字节SYN向量在训练侧有27923条恶意候选；其余229条误报仍需另查，不能把所有误报归为精确输入重合。该批无恶意样本，召回率和平衡准确率无定义；42-1已被查看，只作为开发诊断。四份输入摘要前后不变，35/35 CTest通过。完整复现见[tcp_normal_diagnostic.md](tcp_normal_diagnostic.md)；下一工程步保存基线模型与编码／预处理／阈值契约并验证加载预测一致，同时保留误报限制，不启用在线告警。
- 实时`--count`已改为可选上限；本机`lo`在省略上限后能于静默期正常报告，随后处理4个`complete` ICMP包，并在`SIGTERM`后完成统计、流汇总与清理。
- 主程序已在stdout首次I/O前显式启用行缓冲；严格普通文件重定向测试在进程结束前读到5秒周期报告，避免systemd journal日志延迟到缓冲区填满或服务退出。
- 提交`740d5ab`的官方SDK ARM64部署包已在LubanCat-2N完成首次非root systemd手工启停：进程使用专用用户，能力仅为`CAP_NET_RAW`，`NoNewPrivs=1`；真实4包ICMP得到1条双向流且两个drop字段为0，SIGTERM正常收尾。
- `systemctl enable`后的受控重启也已通过：boot ID变化，无人工`start`时服务已为`enabled`和`active`，一次启动成功且`NRestarts=0`；新PID保持相同专用身份和唯一`CAP_NET_RAW`能力，静默报告与重启后ICMP正确。
- 目标板systemd版本为`245 (245.4-4ubuntu3.24)`；提交`dc542ad`的第一批低风险沙箱配置保持非root身份、唯一`CAP_NET_RAW`、静默报告和真实ICMP正常，安全评分由`5.2 MEDIUM`降为`3.7 OK`。`ProtectClock=yes`仍被旧评分器扣分，不继续以追求分数作为项目主线。
- 开发板VFAT持久化实验暴露了厂商存储服务风险：`usbmount`动态挂载数据卡，`resize-all.service`又遍历`/proc/mounts`；辅助脚本重建VFAT后恢复失败，UUID由`CB8E-AD01`变为`9741-D596`，原板端工作树丢失。项目权威副本仍在开发电脑和GitHub；旧`fstab`条目已移除，`resize-all.service`已禁用并屏蔽。工作树随后通过Git Bundle恢复到eMMC ext4，新构建路径下18项板端CTest全部通过；修正`usbmount`配置后，SD卡已跨重启唯一挂载到`/media/usb0`并通过普通用户读写验收，只承担大容量数据存储。
- 未插入SD卡的后续重启确认屏蔽跨boot保持；同一boot中分析器第一次打开`eth0`时，`ETHTOOL_GET_TS_INFO`暂时返回`EBUSY`，systemd在2秒后自动重启，第二个进程成功运行并持续输出周期报告。随后使用不存在接口完成连续失败实验，显式`30s/5次`策略在5个进程失败后拒绝第6次启动；提交`903edd4`的正式单元已部署，摘要、静态检查、有效参数、正常启动和4包ICMP数据面均通过。

如果实际状态与上面不同，应先查看用户是否在新会话开始前继续修改了代码，不要覆盖未提交改动。

## 2. 用户目标与项目定位

用户正在准备嵌入式Linux应用开发、网络协议分析和DPI相关岗位。项目不是为了重新实现Wireshark或tcpdump，而是为了展示一条可嵌入业务程序的网络数据处理链：

```text
数据包采集
    ↓
二进制安全读取
    ↓
二层到四层协议解析
    ↓
规范化双向五元组
    ↓
流量聚合与过期
    ↓
规则异常检测 / 机器学习特征
    ↓
CSV、Qt上位机或云端展示
    ↓
ARM Linux开发板部署
```

目标岗位JD曾包含：

1. TCP/IP网络中二到七层协议分析、研究和内容解析开发；
2. DPI处理框架及应用开发与维护。

因此本项目当前的libpcap采集、协议解析、五元组流表、线程实验和未来应用层解析，与目标方向有直接关联。项目最终仍需要体现嵌入式Linux特点，例如受限资源、长期运行、网卡权限、交叉或原生构建、进程生命周期、性能和开发板部署，而不应只停留在Ubuntu桌面离线解析。

项目最初参考的两份外部规划文档仍位于：

- `/home/zcb/workspace/嵌入式Linux_网络协议与异常检测_学习项目路线.md`；
- `/home/zcb/workspace/网络流量分析与异常检测项目_逐步实现手册.md`。

它们用于理解长期路线，当前真实实现和下一步应以仓库代码、README、CHANGELOG及本文为准。

## 3. 与用户协作时必须遵守的约定

### 3.1 源代码由用户亲自输入

用户明确要求：

- C源码、头文件、CMake和测试代码由助手完整展示并解释，用户自己在VS Code中输入；
- 除非用户之后明确改变要求，否则不要直接替用户创建或修改C源码；
- 助手负责在功能验证后维护`README.md`、`CHANGELOG.md`、`docs/problem_log.md`和`docs/technical_decisions.md`；
- 用户删除过助手直接写入的源码，因此这一点非常重要。

提供代码时应：

- 先说明本步骤为什么做、对完整项目有什么作用；
- 给出准确文件路径、当前代码锚点或行号，以及“插入、替换或删除”的范围；
- `.h`和对应`.c`尽量同一步给出；
- 代码注释要完整、规范，并说明关键数据类型、所有权、生命周期和错误码；
- 命令行逐条解释用途，不要只贴命令；
- 控制单步规模，复杂步骤可以拆分，但不要把同一小功能机械拆成过多轮；
- 用户曾要求“代码多时写清原代码和行数”，因此优先给局部修改而不是重复整份大文件。

### 3.2 每一步都要测试和Git

每个实现步骤应包含：

1. 重点单元测试；
2. 全量CTest回归；
3. 必要的手动验收；
4. 文档更新；
5. `git add`、提交标题和`git push`说明。

2026-11-01补充：用户要求以后每一步结束都给出可直接使用的commit标题，Git仍由用户执行；不要把候选预检写成板端验收。

通常使用：

```bash
cmake --build build
cmake -E chdir build ctest --output-on-failure
```

不要提交`build/`、`build-release/`或链接后的可执行文件。它们是可重建产物，不属于源码仓库。

### 3.3 讲解风格

用户希望理解代码，不只是让项目跑通。解释时尤其要覆盖：

- 为什么需要某个结构体或函数；
- 它位于整个处理链的哪一层；
- 指针指向谁、谁拥有内存、谁负责释放；
- `size_t`、固定宽度整数、字节序、错误码等类型选择；
- 线程、锁、条件变量和资源生命周期的时间顺序；
- 测试在自动化流程中的位置；
- 为什么采用当前技术，而不是只说“行业一般这样做”。

用户C语言熟悉度高于Python。Python验收脚本可以解释执行流程、参数解析和返回码，但除非用户追问，不必逐行展开所有Python语法。

日志使用正常的工程技术写作风格，不要每句话都写“我”。只记录真实发生的问题和可验证结论，不要为了面试故事编造虚假故障。可以把具体问题整理成面试素材，但要保持事实基础。

用户已经说过纯代码格式问题暂时不用专门修改。除非格式导致警告、可读性严重下降或掩盖逻辑错误，否则优先推进功能。

## 4. 当前构建环境

### 4.1 工具与依赖

项目当前使用：

- Ubuntu Linux；
- GCC；
- ISO C11，关闭编译器语言扩展；
- CMake 3.16及以上；
- Ninja；
- pkg-config；
- libpcap开发包；
- Python 3，仅用于端到端验收；LubanCat自带Python 3.8.10已经验证；
- Git和GitHub；
- VS Code及CMake Tools/C语言扩展。

主要CMake设置：

- `CMAKE_C_STANDARD 11`；
- `CMAKE_C_EXTENSIONS OFF`；
- `CMAKE_EXPORT_COMPILE_COMMANDS ON`；
- `-Wall -Wextra -Wpedantic`；
- 可执行文件输出到`build/bin`；
- 静态库输出到`build/lib`；
- 核心代码构建为`analyzer_core`静态库；
- libpcap通过`pkg_check_modules(... IMPORTED_TARGET libpcap)`引入；
- `capture.c`对libpcap的依赖为PRIVATE，上层公开头文件不暴露`pcap_t`。

### 4.2 Debug构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

如果`build`已经使用其他生成器配置，不要在同一目录直接改用Ninja。继续使用原生成器，或建立新的构建目录。

### 4.3 Release构建

```bash
cmake \
    -S . \
    -B build-release \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF

cmake --build build-release
```

Release主程序位于`build-release/bin/netflow-analyzer`。

### 4.4 ARM64交叉构建

当前有两条已经在LubanCat-2N运行成功的Release交叉构建路径：

- 官方Buildroot GCC 9.3、SDK内置glibc 2.29 sysroot和隔离的板端libpcap overlay；
- Ubuntu `aarch64-linux-gnu-gcc` 13、板端完整sysroot和GCC `-B`启动文件前缀。

两种产物均为AArch64 ELF，动态加载器为`/lib/ld-linux-aarch64.so.1`，最终只要求`GLIBC_2.17`。交叉配置使用`BUILD_TESTING=OFF`；既有逻辑回归由当时x86_64与ARM64板端原生构建的18项CTest负责，交叉产物加载和真实采集由板端`ldd`、`--help`和ICMP测试负责。当前分支已扩展到24项x86_64 Debug测试，新增模块仍需重新交叉构建并执行板端原生测试和运行验收。

完整目录、环境变量、CMake命令和故障处理见[`docs/cross_compilation.md`](cross_compilation.md)。

### 4.5 VS Code与CMake历史问题

曾经出现VS Code能够由CMake编译，但编辑器找不到头文件和不能补全的问题。关键点：

- CMake的`target_include_directories`只告诉编译器头文件路径，不一定自动修复编辑器配置；
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON`生成的`compile_commands.json`可以让VS Code/clangd知道真实编译参数；
- 必须在VS Code中打开项目根目录`/home/zcb/workspace/netflow-analyzer`，不要误开`/home/zcb`或`/home/zcb/workspace`；
- 之前CMake Tools把构建输出配置到了`/home/zcb/build`，输出显示项目名为`zcb`，这说明工作区根目录选错；
- 正确构建目录应是`${workspaceFolder}/build`，也就是仓库内的`build`；
- 调试曾因VS Code默认“编译当前C文件”任务而尝试单独链接没有`main`的`queue.c`，出现`undefined reference to main`；正确做法是调试CMake生成的完整可执行目标，而不是单文件活动任务；
- GDB提示`Failed to set controlling terminal: Operation not permitted`通常不是业务代码故障；
- 调试启动曾非常慢，改为正确的CMake目标配置后明显恢复；调试当前不是项目主线。

## 5. 仓库和版本状态

GitHub仓库：`https://github.com/Devi1Fairy/netflow-analyzer`

SSH远程：

```text
origin git@github.com:Devi1Fairy/netflow-analyzer.git
```

当前重要提交，从新到旧：

```text
d427168 feat(output): export TCP flow state
29aa018 feat(app): report TCP state in flow summaries
1eea4d8 feat(flow): expose TCP phase names
83586c1 feat(flow): track TCP lifecycle in flow records
31ee759 feat(flow): replace oldest flow in one probe
b23fa59 docs(board): record ARM64 eviction validation
183617c docs(flow): record eviction policy validation
be08b7e feat(flow): add oldest-flow eviction policy
dd5221e feat(flow): report hash probe statistics
25d2c06 docs(board): record multiflow and soak baselines
295bf5c docs(board): record single-flow performance baseline
b070f09 docs(board): record ARM64 17-test baseline
a0de7df tag: v0.2.0
a8fd76f feat(cli): expose live capture filter option
8b047d2 feat(capture): add BPF filter support
61076f1 feat(cli): analyze bounded live packet streams
47d37ab feat(cli): add live capture interface probe
bf73660 feat(capture): add live interface opening
d92dd62 feat(cli): export flow summary to CSV
7296618 feat(output): add CSV flow exporter
7f6adad feat(flow): use hash table for flow aggregation
da61a6f docs: add technical decision log
194ec56 feat: add stable flow key hashing
234175f feat: add idle flow expiration
ce60c7b tag: v0.1.0, merge release PR
ebced21 test: add offline flow acceptance coverage
629c889 feat: aggregate offline packets into flows
1db9799 feat: add fixed-capacity flow table
03c7280 feat: add bidirectional flow record model
e2455b8 feat: add bidirectional flow key model
4d14044 feat: dispatch and display IPv4 payload protocols
f4d4a2b feat: parse ICMP messages
3208159 feat: parse UDP datagrams
e32a14c feat: parse TCP segments
```

用户已经练习过feature分支、compare链接、Pull Request、review和merge。历史PR包括至少`#1`、`#2`，`v0.1.0`通过PR `#4`合入。近期为了连续学习直接在`main`提交；如果下一阶段需要模拟团队协作，应先创建feature分支，再通过PR合入，不要假设必须直接推main。

`v0.1.0`是第一条完整离线分析链的发布标签。`v0.2.0`已经发布哈希流表、过期底层接口、CSV、实时抓包和BPF过滤。信号优雅退出、libpcap运行统计、实时流过期、容量淘汰、探测统计和TCP状态跟踪属于`Unreleased`开发进度，当前不需要仅因这一功能立即更换版本标签。

## 6. 当前目录结构与职责

```text
netflow-analyzer/
├── CMakeLists.txt
├── README.md
├── CHANGELOG.md
├── include/analyzer/
│   ├── app.h
│   ├── byte_reader.h
│   ├── capture.h
│   ├── packet_info.h
│   ├── ethernet.h
│   ├── ipv4.h
│   ├── tcp.h
│   ├── udp.h
│   ├── icmp.h
│   ├── ipv4_dispatch.h
│   ├── flow_key.h
│   ├── flow_record.h
│   ├── tcp_flow_state.h
│   ├── flow_features.h
│   ├── flow_table.h
│   ├── flow_expiration.h
│   ├── flow_export.h
│   ├── flow_feature_export.h
│   └── runtime_metrics.h
├── src/
│   ├── main.c
│   ├── app/app.c
│   ├── common/byte_reader.c
│   ├── capture/capture.c
│   ├── protocol/
│   │   ├── packet_info.c
│   │   ├── ethernet.c
│   │   ├── ipv4.c
│   │   ├── tcp.c
│   │   ├── udp.c
│   │   ├── icmp.c
│   │   └── ipv4_dispatch.c
│   ├── flow/
│   │   ├── flow_key.c
│   │   ├── flow_record.c
│   │   ├── tcp_flow_state.c
│   │   ├── flow_features.c
│   │   ├── flow_table.c
│   │   └── flow_expiration.c
│   ├── metrics/runtime_metrics.c
│   ├── output/flow_export.c
│   └── output/flow_feature_export.c
├── tests/
│   ├── unit/
│   └── integration/test_offline_flow.py
├── scripts/check_target_env.sh
├── packaging/systemd/
│   ├── netflow-analyzer.service
│   └── netflow-analyzer.default
├── docs/
│   ├── problem_log.md
│   ├── technical_decisions.md
│   ├── systemd_deployment.md
│   └── session_handoff.md
└── labs/
    ├── blocking_queue/
    ├── tcp_framing/
    └── thread_pipeline/
```

模块职责：

| 模块 | 职责 |
|---|---|
| `main.c` | 在首次输出前建立stdout行缓冲，初始化上下文、解析参数、安装停止信号处理、运行应用、统一清理，并把错误码转换为进程退出码 |
| `app.c` | 应用编排；连接采集、协议解析、流表、周期运行指标和输出，不承担具体协议字段解析 |
| `byte_reader` | 使用边界检查游标读取、跳过和切分原始二进制字节 |
| `capture` | 封装libpcap，统一离线文件、实时网卡、BPF、非阻塞等待、中断和运行统计接口，隐藏`pcap_t`、`struct pcap_stat`与`DLT_*` |
| `packet_info` | 保存一条数据包的捕获元数据、协议字段和解析状态 |
| `ethernet` | 解析Ethernet II头、MAC地址和EtherType |
| `ipv4` | 解析IPv4头、长度、地址、TTL、协议号和分片信息 |
| `tcp` | 解析TCP头、端口、序列号、确认号、标志和负载视图 |
| `udp` | 解析UDP头、端口、长度和负载视图 |
| `icmp` | 解析ICMP类型、代码、echo字段和负载视图 |
| `ipv4_dispatch` | 按IPv4协议号分发到TCP、UDP或ICMP解析器 |
| `flow_key` | 生成规范化双向五元组，判断相等，计算FNV-1a 64位哈希 |
| `flow_record` | 保存一个双向流的两个方向统计、首末时间，并为TCP流推进独立连接状态 |
| `tcp_flow_state` | 根据规范化方向和TCP标志跟踪握手、中途捕获、FIN关闭和RST中止，提供稳定阶段名称 |
| `flow_table` | 开放寻址哈希表、线性探测、删除标记、查找、遍历、返回值副本的过期删除，以及数据包路径探测成本统计 |
| `flow_expiration` | 维护数据包事件时间高水位、扫描周期和空闲截止时间，处理乱序与整数边界 |
| `flow_export` | 将流记录写成固定字段顺序的CSV表头和记录，包含TCP阶段或非TCP的`not-applicable` |
| `flow_features` | 从完整双向流记录生成持续时间、总量、均值、方向不平衡度和TCP生命周期模型特征 |
| `flow_feature_export` | 验证特征对象并按`flow_features_v1`固定字段顺序写CSV，不拥有调用者的输出流 |
| `runtime_metrics` | 使用单调时钟和累计值差分计算区间PPS、Mbps、流表占用率和过期流数量 |

`common`目录存放不属于某一种网络协议或业务模块、但多个模块可复用的基础工具。当前只有安全字节读取器，未来可以放通用时间、错误转换、日志等工具，但不要把所有无法分类的业务代码都堆入`common`。

## 7. 当前端到端数据流

离线模式：

```text
CLI --read FILE [--csv FILE] [--feature-csv FILE]
    ↓
app_parse_arguments
    ↓
capture_open_offline
    ↓
capture_next_packet，直到PCAP EOF
    ↓
app_process_packet
    ↓
packet_info_init
    ↓
ethernet_parse
    ↓ EtherType == 0x0800
ipv4_parse
    ↓ protocol = 1 / 6 / 17
ipv4_dispatch_payload
    ↓
icmp_parse / tcp_parse / udp_parse
    ↓
flow_table_process_packet
    ↓
flow_key_from_packet + flow_key_hash
    ↓
flow_record_init或flow_record_update
    ↓ TCP流
tcp_flow_state_observe推进旁路连接阶段
    ↓
终端逐包预览（最多5包）
    ↓
处理完整文件后输出流汇总
    ↓ 可选
普通流记录CSV或版本化特征CSV导出
```

实时模式：

```text
CLI --interface NAME [--count N] [--filter EXPRESSION]
    [--flow-full-policy reject|evict-oldest]
    [--feature-csv FILE]
    ↓
提供N时严格解析为大于0的size_t；省略时无包数上限
    ↓
capture_open_live
    ↓
检查链路类型必须为Ethernet
    ↓
可选capture_set_filter
    ↓
capture_enable_nonblocking
    ↓
capture_next_packet
    ↓ 暂时无包EAGAIN
capture_wait_readable使用poll等待可读或最多1秒
    ↓
检查CLOCK_MONOTONIC；约每5秒输出区间运行指标
    ↓ 可读后再次读取
    ↓ 收到包
更新flow_expiration事件时间高水位
    ↓ 每推进5秒
计算30秒空闲截止时间
    ↓
flow_table_expire_before复制并删除旧流
    ↓
输出Expired flow和累计过期数量
    ↓
当前包进入与离线模式复用的解析链
    ↓
输出完整/截断/畸形/不支持/流表拒绝分类
    ↓ 完整且有容量
流表聚合；满载时按策略拒绝新流，或在同一次满表扫描中选择并原位替换last_seen最早流
    ↓ 可选特征CSV
过期流在删除时导出；淘汰流在覆盖前导出
    ↓
总捕获包数达到N，或收到SIGINT/SIGTERM
    ↓
capture_get_statistics
    ↓
关闭capture句柄
    ↓
把最终剩余流导出到特征CSV
    ↓
输出应用包数、libpcap统计、预览数和流汇总
```

`app_run_capture_analysis()`现在同时编排离线和实时输入。它根据`app_context_t.command`选择打开方式，后续处理链不区分数据来自文件还是网卡。这是刻意的职责分离：采集来源差异停留在采集层和应用编排层，协议解析器只接收字节与长度。

## 8. 关键数据模型与所有权

### 8.1 `capture_t`和二级指针

`capture_t`是不透明对象。结构体只在`capture.c`定义，公开头文件仅有：

```c
typedef struct capture capture_t;
```

打开函数使用：

```c
int capture_open_live(..., capture_t **capture, ...);
```

调用者拥有一个`capture_t *`变量，把它的地址`&capture`传入。函数成功后通过二级指针修改调用者的指针，使其指向新对象。内部流程坚持“局部创建、完整验证、最后发布”：

1. `calloc`分配`capture_t`；
2. libpcap打开句柄；
3. 查询并转换链路类型；
4. 全部成功才执行`*capture = new_capture`；
5. 失败时释放局部对象，不覆盖调用者原值。

关闭函数接收`capture_t **`，调用`pcap_close`和`free`后再把调用者指针设为`NULL`，避免悬空指针和重复关闭。

### 8.2 数据包视图生命周期

`capture_packet_view_t.data`指向libpcap管理的内存：

- 调用者不能修改；
- 调用者不能`free`；
- 只保证在下一次`capture_next_packet()`调用之前有效；
- 当前应用必须在下一次读取前完成协议解析；
- `packet_info_t`只保存解析出的数值和固定数组，不保存原始指针，因此可以安全进入流表统计。

### 8.3 `caplen`和`wirelen`

- `captured_length/caplen`：实际保存在内存或PCAP中的字节数，是所有数组边界检查的唯一依据；
- `wire_length/wirelen`：数据包在线路上的原始长度，只用于统计，不能用来访问`data`；
- `caplen < wirelen`表示抓包截断；被截掉的数据不会自动出现在“下一个包”，而是已经丢弃的当前包尾部。

### 8.4 `packet_info_t`

统一结果结构目前保存：

- 秒和微秒时间戳；
- caplen、wirelen；
- Ethernet MAC、EtherType、网络负载位置；
- IPv4头长度、总长度、ID、DF/MF、分片偏移、TTL、协议号、校验和、地址；
- TCP端口、序列号、确认号、头长、标志、窗口、校验和、紧急指针、负载位置和截断状态；
- UDP端口、长度、校验和、负载位置和截断状态；
- ICMP类型、代码、校验和、echo ID/sequence、负载位置和截断状态；
- 解析状态、错误层和错误偏移。

它不拥有动态内存，不需要`free`。

### 8.5 双向五元组

传统五元组是：

```text
源IP、源端口、目标IP、目标端口、协议号
```

项目把两个端点按IP和端口排序，得到`endpoint_a`和`endpoint_b`，因此正向包和反向包生成相同键。方向另存为`A_TO_B`或`B_TO_A`。注意：

- A/B只是规范化排序结果，不等于客户端/服务器；
- TCP/UDP使用真实端口；
- ICMP没有端口概念，两个端口统一为0；
- 同一流的两个方向共享一条`flow_record_t`；
- 每个方向分别统计包数、捕获字节和线路字节。

### 8.6 哈希流表

流表使用：

- FNV-1a 64位哈希；
- 固定字段顺序和固定字节序；
- 不直接哈希结构体原始内存，避免填充字节和主机字节序造成不稳定；
- 开放寻址；
- 线性探测；
- `EMPTY`、`OCCUPIED`、`DELETED`三种槽状态；
- 固定容量256槽，由`app.c`栈上数组提供；
- 当前不支持动态扩容；
- 当前没有内部锁，不能被多个线程同时修改。

数据包路径还累计线性探测操作数、检查槽位总数和最大探测长度。起始槽位算1次检查；命中、插入和满载`ENOSPC`都计入，`flow_table_find()`等管理查询不计入。两个`uint64_t`累计值达到上限后饱和而不回绕；应用退出时输出平均、最大和饱和状态。`evict-oldest`组合接口也只把当前包记为一次探测操作；满表扫描已经取得最旧候选，因此不再执行淘汰后的第二次哈希探测。

`DELETED`不能直接变回`EMPTY`，否则会提前截断发生哈希冲突后的探测链。流表完全为空时可以统一清除删除标记。

### 8.7 流表存储所有权

`flow_table_t`借用调用者提供的`flow_table_slot_t[]`，不拥有它。`flow_table_cleanup()`只解除借用并清空状态，不对数组调用`free`。当前数组是`app_run_capture_analysis()`的局部栈数组，流表只在该函数返回前使用，因此生命周期有效。

### 8.8 CSV输出所有权

`flow_export`函数借用调用者传入的`FILE *`，只写数据，不负责关闭。`app.c`负责：

1. 以C11的`"wx"`模式独占创建文件；
2. 拒绝覆盖已有文件；
3. 写表头；
4. 遍历流表写记录；
5. 无论成功失败都调用`fclose`；
6. 检查`fclose`可能报告的缓冲刷新错误。

普通流记录CSV只允许离线模式使用，实时模式的参数组合会拒绝`--csv`。版本化特征CSV使用独立参数和生命周期，见8.10。

CSV在协议号后使用稳定的`tcp_state`字段。TCP流通过`tcp_flow_phase_name()`输出状态机阶段，UDP和ICMP写入`not-applicable`。状态名称来自核心状态模块，终端和CSV不各自维护重复映射。字段仅含小写字母和短横线，不需要额外CSV转义。

### 8.9 TCP流状态与所有权

每条TCP `flow_record_t`按值保存独立的`tcp_flow_state_t`。状态对象只包含枚举、规范化方向和布尔值，不保存原始报文指针，不拥有动态内存，因此流记录复制、过期和淘汰时不需要额外`free`。

当前状态包括：

- `unobserved`：已经初始化但尚未观察报文；
- `syn-seen`、`syn-ack-seen`、`established`：完整握手路径；
- `midstream`：从连接中途开始捕获，不能证明完整握手；
- `fin-seen`、`fin-bidirectional`、`closed`：旁路观察到的关闭路径；
- `reset`：观察到RST中止。

`endpoint_a`和`endpoint_b`只表示规范化排序，不等于客户端和服务器；初始SYN的方向单独保存在`initiator_direction`。`handshake_completed`是历史事实，即使流之后关闭或重置也继续保留。`closed`和`reset`暂时是终止状态，同一五元组重新建立新连接留给下一轮生命周期设计。

这不是Linux内核TCP状态机的复制：当前不验证ACK号是否精确确认SYN或FIN，不处理乱序、重叠、重传字节、TCP选项和字节流重组，也不能仅凭状态字段宣称已经具备应用层DPI。

### 8.10 流特征与特征CSV所有权

`flow_features_t`只保存数值、枚举和布尔值，不拥有动态内存、原始数据包或流表槽位。`flow_features_from_record()`借用只读`flow_record_t`，先在局部对象中完成协议状态、时间戳、总量和溢出检查，全部成功后才把完整结果复制给调用者。

第一版`flow_features_v1`包含协议、微秒持续时间、双向包/字节总量、平均包长、包数/线路字节方向不平衡度、TCP状态是否适用、TCP阶段和握手完成状态。`schema_version`是文件格式元数据，不是模型输入；`protocol`和`tcp_phase`是类别特征，不能把枚举数值差解释为语义距离。输出不包含IP、端口、内部`initialized`字段或异常标签。

特征CSV的`FILE *`由`app_run_capture_analysis_with_feature_csv()`唯一拥有：

1. 以`"wx"`独占创建目标文件，已有文件不覆盖；
2. 写入一次版本化表头；
3. 把输出流以借用方式传给分析函数和导出器；
4. 过期流在删除时写出，被淘汰流在槽位覆盖前写出；
5. 采集结束后遍历流表写出最终剩余流；
6. 无论分析成功或失败都由包装函数调用`fclose()`并检查刷新错误。

这三类流记录是互斥的值副本集合。只在退出时遍历最终流表会遗漏已经过期或被淘汰的记录；逐包写特征又会为同一条流生成大量变化中的重复快照。因此当前选择“生命周期结束时恰好写一次”。

离线模式可以使用`--flow-idle-timeout SECONDS`显式启用空闲分段。选项只接受正的64位秒数，不能与实时`--interface`组合；省略时仍保持同键跨整份PCAP聚合。启用后复用事件时间高水位和5秒扫描调度，在当前包进入流表前导出并删除空闲流，因此同键后续包会建立新生命周期。这是可审计的项目分段规则，不等于复制Argus的完整分流语义。

离线模式允许`--csv`和`--feature-csv`同时出现，但路径必须不同。实时特征CSV必须配合大于0的`--count`，避免无上限服务持续增加文件并耗尽开发板存储。该约束只控制本次任务的输入包数，不等价于生产级文件轮转或配额管理。

### 8.11 样本身份与元数据sidecar所有权

`scripts/flow_sample_metadata.py`把模型输入和数据治理信息分开。`flow_features_v1`继续只保存模型输入；`FlowSampleIdentity`保存`capture_id`、协议、规范化端点和首末Unix微秒，`FlowSampleMetadata`再增加从1开始的特征行号、匹配状态、候选数和可选标签。

`flow_sample_id_v1`把模式版本和全部身份字段按固定顺序序列化为紧凑ASCII JSON，再计算SHA-256摘要。它是确定性的低碰撞标识，不是标签、密码或数字签名。身份对象使用冻结的dataclass，端点必须已经按C端流键规则排序；ICMP端口固定为0。

`flow_sample_metadata_v1` sidecar包含完整身份、`sample_id`、`feature_row_number`、匹配质量和`is_trainable`。当前`is_trainable`属性只检查逐流`unique`且标签为`benign`或`malicious`；`unmatched`、`ambiguous_same_label`和`ambiguous_conflicting_labels`都保留为不可训练证据。IoT-23另有跨C流复用风险，不能直接把这个逐流属性当作最终训练准入结果；另设的`iot23_flow_review_v1` sidecar采用三态审查，不沿用此布尔值。

Python写出函数借用调用者提供的`TextIO`，不负责关闭或刷新。审计CLI才拥有目标文件：`--capture-id`和`--metadata-output`必须成对提供，目标以`"x"`模式独占创建，失败时尝试删除本次产生的不完整文件，已有文件不会被覆盖。

`feature_row_number`要求普通流CSV和特征CSV来自同一次分析、相同生命周期参数和相同输出顺序。它不能把不同运行产生的两个文件任意拼接；后续训练集构建器仍需检查模式版本、行数和对应关系。

## 9. 协议与二进制解析原则

### 9.1 为什么使用字节游标

网络包是无符号二进制数据，不是C字符串。`byte_cursor_t`维护：

- 当前字节地址；
- 总长度；
- 当前读取偏移。

读取、跳过和切片前都做边界检查，避免越界。负载视图通常通过“父游标跳到负载位置，再建立子游标”实现。父游标和子游标都只是视图，不拥有底层内存。

### 9.2 为什么使用`memcpy`而不是字符串函数

网络数据可以包含任意`0x00`字节，也不保证以`'\0'`结尾。`strcpy`、`strlen`等字符串函数会在零字节处停止，或者越过真实缓冲区寻找终止符。固定长度二进制字段应先检查剩余长度，再使用`memcpy`复制准确字节数。

这一问题已经作为具体调试经验记录在问题日志，可用于面试说明“错误工具处理二进制数据导致测试失败，随后改为长度驱动的复制”。

### 9.3 大端和小端

网络多字节整数使用网络字节序，也就是大端：高位字节先出现。x86主机通常是小端。解析器按字节显式组合数值或通过安全读取函数转换，最终在结构体中保存主机可直接比较的整数。

MAC地址是固定6字节数组，没有把整个地址当作整数，因此不需要整体字节序转换。

### 9.4 当前包层次

普通Ethernet/IPv4/TCP包大致是：

```text
Ethernet II头 14字节
    目标MAC 6
    源MAC 6
    EtherType 2

IPv4头 20～60字节
    版本/IHL、总长度、ID、分片、TTL、协议、校验和、源/目标IP等

TCP头 20～60字节
    源/目标端口、序列号、确认号、标志、窗口、校验和等

TCP负载 可变长度
```

UDP和ICMP位于IPv4负载位置，与TCP是并列的上层协议，不是TCP内部内容。

EtherType `0x0800`是IEEE/IANA约定的IPv4标识，不是项目自定义示例。IPv4协议号1、6、17分别表示ICMP、TCP、UDP。

## 10. 当前CLI能力

### 10.1 帮助和版本

```bash
./build/bin/netflow-analyzer --help
./build/bin/netflow-analyzer --version
```

### 10.2 离线分析

```bash
./build/bin/netflow-analyzer --read /path/to/input.pcap
```

行为：

- 读取完整PCAP；
- 只预览前5个包，避免终端被淹没；
- 第6包及以后仍会解析并参与流聚合；
- 文件结束后输出总包数、预览数和流汇总。

### 10.3 离线CSV

```bash
./build/bin/netflow-analyzer \
    --read /path/to/input.pcap \
    --csv /path/to/new-flows.csv
```

目标文件必须不存在。已有文件不会被覆盖。

### 10.4 版本化流特征CSV

离线生成：

```bash
./build/bin/netflow-analyzer \
    --read /path/to/input.pcap \
    --feature-csv /path/to/new-features.csv
```

有限实时生成：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp" \
    --feature-csv /path/to/new-features.csv
```

目标文件必须不存在。离线模式可以同时指定`--csv`和`--feature-csv`，但两者路径必须不同。实时模式使用`--feature-csv`时必须提供严格正整数`--count`；无上限实时模式会在参数验证阶段被拒绝。

当前输出模式为`flow_features_v1`。它没有标签、IP或端口；身份和匹配结果由独立的`flow_sample_metadata_v1` sidecar保存。sidecar只是建立可审计连接，不代表真实数据已经形成可监督训练集。

CTU-13匹配审计及可选sidecar导出：

```bash
python3 scripts/audit_ctu13_flow_matches.py \
    /path/to/capture20110816-2.binetflow \
    /path/to/flows.csv \
    --capture-id ctu13-scenario-7 \
    --metadata-output /path/to/new-metadata.csv
```

普通流CSV和对应的特征CSV必须来自同一次分析，才能用元数据中的`feature_row_number`连接。`--capture-id`和`--metadata-output`必须同时出现；目标文件必须不存在，失败时不会保留本次不完整输出。

### 10.5 实时有限或持续分析

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"
```

`--count`使用严格正整数解析：

- 0拒绝；
- 负数拒绝；
- 非数字拒绝；
- 超出`size_t`范围拒绝；
- `--count`不能与`--read`组合；
- 省略`--count`时，实时模式持续运行到收到停止请求或致命错误；
- 当前实时模式不能与`--csv`组合。

实时流表满载策略：

- 默认`--flow-full-policy reject`，新流包计入`flow_rejected`并继续运行；
- 可选`--flow-full-policy evict-oldest`，在确认新键不存在的同一次满表扫描中按最小`last_seen`复制最久未活动流，并用当前新流原位替换；
- 原位替换成功的当前包计入`complete`，淘汰使用独立`evicted_flows`计数；
- 该选项第一版只允许实时模式使用，离线模式仍保持完整文件级聚合和固定容量拒绝语义。

提供上限时达到数量，或任意实时模式收到`SIGINT`/`SIGTERM`后，程序都在关闭capture句柄前查询`pcap_stats()`，输出捕获后端接收、抓包缓冲区丢弃和接口丢弃统计。统计查询失败只降级显示不可用，不丢弃已经完成的流分析。

打开实时接口通常需要root权限或适当Linux capability。当前手工验收使用`sudo`运行主程序，但不要使用`sudo cmake`或`sudo cmake --build`，避免构建目录出现root所有权文件。

## 11. 最新实时抓包验收结果

系统接口：

```text
lo     Linux回环接口
ens33  VMware呈现给Ubuntu虚拟机的Ethernet接口
```

`lo`捕获本机经`127.0.0.1`或`::1`进行的通信，包不会离开系统。`ens33`承载虚拟机与宿主机、局域网或互联网的通信，具体范围取决于VMware NAT、桥接或Host-only模式。

最新周期指标验收执行：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 100 \
    --filter "icmp"
```

先保持接口静默，程序连续输出约5.007至5.008秒的零包报告；另一个终端执行`ping -i 0.2 -c 30 127.0.0.1`后，60个逻辑ICMP包分别进入16包和44包两个报告区间；流量结束后报告再次归零。Ctrl+C正常收尾，最终输出`Total packets: 60`、`Capture received packets: 120`和两个drop字段为0。

无上限服务生命周期前置验收省略`--count`，使用`timeout --signal=TERM`模拟systemd停止。程序启动时显示`Packet limit: unlimited`，静默期先输出0包报告；一次`ping -c 2 127.0.0.1`产生4个`complete`包，随后`SIGTERM`沿正常路径收尾。最终后端报告8包，两个drop字段均为0，证明没有依赖包数上限才能完成清理。

重要结论：

- 实时抓包看到的是接口上的全部匹配流量，不是只看到用户为了测试主动产生的流量；
- BPF已经能排除无关的VS Code回环TCP流量；
- `--count`限制包数，不限制等待时间；接口没有足够流量时程序会继续等待；
- libpcap读取缓冲区超时不是应用周期定时器；显式非阻塞模式配合`poll()`后，完全静默时主循环仍能定期获得控制权；
- 周期指标使用`CLOCK_MONOTONIC`和真实区间计算，`poll()`的1秒超时只决定检查粒度；
- Ctrl+C和SIGTERM已经能中断等待并沿正常路径输出已有统计；
- Linux回环包会出现outgoing和incoming捕获事件，libpcap向应用交付前会抑制重复副本，因此捕获后端的120和应用处理的60处于不同统计层级；
- 不能用`Capture received packets - Total packets`推导丢包，应查看专门的drop字段并保留平台可用性限制。

## 12. 当前测试体系

最新ARM64原生Debug全量回归（2026-10-12）：33项普通测试与4项可选ML测试共37项全部通过，94.41秒、退出0。验收时为`3ad8a49`加板端CMake改动，现已提交／同步为`4a6d8de`，虚拟机在该提交37/37项通过（7.53秒、退出0）。线性基线原测试独立通过但耗时29.33秒，因此两处重复的CTest超时均由20秒调整为60秒，尚未合并；生成配置中只注册一次且超时60，测试样本、轮数和断言未放宽。板端使用Python 3.8.10／NumPy 1.24.4／PyTorch 2.4.1，不能称为与虚拟机完全相同的运行环境。详细命令、日志和范围见[arm64_test_validation.md](arm64_test_validation.md)。下文18／35／36项表述均保留为对应日期的历史记录；包括DNS固定头单元测试在内的37项本次已在ARM64执行，但不等于新增DNS域名解析或在线AI能力。

最新TCP复测（2026-10-12）：虚拟机`1cbb72a`构建及37/37项CTest通过（8.57秒）；用户提供新版独立ARM64程序物理网卡10包TCP、1条双向`closed`流和1条握手完成特征的手工结果，数值核对通过。板端自动化仍仅确认历史18项，本次不是新增板端CTest；退出码及发生器PASS尚未提供。

最新短时排查（2026-10-11）：虚拟机`f84cb91`再次通过37/37项CTest（8.88秒）；板端10秒静默SIGTERM、直接日志重定向的手工对照退出0，保存完整0包／0流汇总和1行特征CSV。它是停止与文件收尾验收，不是TCP通信验收或新增板端CTest；首次TCP的Ctrl+C／管道问题及尚待验证内容见问题5.20。

招聘会收尾复测（2026-10-11）：虚拟机`7bce180`构建成功，37/37项x86_64 Debug CTest通过（7.92秒）；新版官方SDK ARM64程序另外通过用户提供的物理网卡4包ICMP、双向汇总与特征导出手工验收，见[jobfair_demo.md](jobfair_demo.md)。这不是37项板端CTest；当前板端自动化记录仍为历史18项，新的TCP设备消息步骤待执行。

最新DNS基线（2026-10-02）：当前HEAD为`492bc28`，构建与37/37项CTest复测通过，新增`dns_tests`覆盖查询／响应固定头、游标位置、大端转换与失败输出不变；`tcp_model_artifact_tests`也在当前已提交工作树中通过。下文35／36项和未提交状态保留为历史快照，选择测试时使用名称而非旧编号。新增DNS尚未在ARM64执行，不更新板端测试数量。

最新补充（2026-10-02）：用户工作区已增加可选`tcp_model_artifact_tests`，本次现有构建及36/36项CTest全部通过；该模块、预处理和CMake改动尚未提交，不因本次文档更新宣称模型产物功能已经发布。下面的35项表是之前已提交配置，按测试名称而非旧编号选择。LubanCat-2N仅新增独立LLM手工冒烟，不更新板端CTest数量。

当前`feature/flow-features`分支的x86_64 Debug普通配置注册32项CTest；本次`NFA_ML_PYTHON_EXECUTABLE`指向项目`.venv/bin/python`，额外注册3项可选ML测试，35/35全部通过，总耗时约6.34秒。下表编号对应本次启用ML的测试树；关闭ML后后续编号会改变，应按测试名称选择。LubanCat-2N板端最新已验证基线仍为特征模块加入前的18项，后续上板时必须重新配置并运行适用的新增测试：

| 编号 | CTest名称 | 主要覆盖 |
|---:|---|---|
| 1 | `analyzer_smoke_tests` | 上下文生命周期、帮助、版本、离线/实时CLI参数及错误组合 |
| 2 | `byte_reader_tests` | 字节读取、跳过、切片、边界和失败不修改输出 |
| 3 | `capture_tests` | PCAP打开、链路类型、逐包读取、BPF、非阻塞与等待参数、中断、实时统计、离线不支持语义和关闭 |
| 4 | `packet_info_tests` | 统一结果对象初始化、时间戳和错误状态 |
| 5 | `ethernet_tests` | Ethernet II字段、负载、截断和格式化 |
| 6 | `ipv4_tests` | IPv4长度、IHL、地址、分片、截断和畸形输入 |
| 7 | `tcp_tests` | TCP头字段、标志、负载和边界 |
| 8 | `udp_tests` | UDP长度、端口、负载和非法组合 |
| 9 | `icmp_tests` | ICMP通用字段、echo字段和负载 |
| 10 | `ipv4_dispatch_tests` | 协议号分发、未知协议和分片处理 |
| 11 | `flow_key_tests` | 双向规范化、方向、字段比较和稳定FNV-1a哈希 |
| 12 | `flow_record_tests` | 两个方向统计、首末时间、错误和溢出保护，以及TCP状态随首包和后续包推进、非TCP无状态 |
| 13 | `flow_table_tests` | 哈希冲突、线性探测、回绕、删除标记、复用、遍历、过期、探测统计、查询隔离、饱和保护，以及单次满表扫描选择并原位替换最旧流的成功/失败契约 |
| 14 | `offline_flow_acceptance` | 6包ICMP PCAP验证聚合、预览、普通CSV、精确特征行和已有特征文件不覆盖；3包TCP握手验证终端与CSV的`established`；同键6包ICMP在30秒离线空闲阈值下验证2包过期流与4包最终流的终端、普通CSV和特征CSV；260包PCAP验证四类异常/拒绝、256槽满载、完整扫描和继续运行 |
| 15 | `ctu13_label_audit_tests` | 合成CSV验证保守三类标签映射、字段契约、空标签、缺失字段、空文件、不存在文件和CLI退出码 |
| 16 | `ctu13_flow_identity_tests` | Scenario 7时区、精确微秒、协议号、双向端点规范化、ICMP端口、不支持协议和非法字段 |
| 17 | `iot23_label_audit_tests` | 合成Zeek文件验证扩展标签字段、标签分类、协议计数、身份统计、单包零时长和非法多包持续时间 |
| 18 | `iot23_flow_identity_tests` | Unix微秒、双向端点、TCP/UDP/ICMP、单包缺失持续时间、不支持协议和非法字段 |
| 19 | `iot23_label_index_tests` | 同键多候选、协议隔离、排除和不支持协议过滤、零时长、四种流区间分类、合成Zeek索引及借用流CSV统计 |
| 20 | `flow_sample_metadata_tests` | 稳定样本ID、身份不变量、四种匹配状态、可训练标志、固定sidecar表头与记录、借用输出流和失败不写半行 |
| 21 | `ctu13_flow_match_audit_tests` | 合成标签与流CSV验证唯一、未匹配、排除、不支持协议、两种歧义，以及元数据行号、CLI参数配对、已有文件保护和失败清理 |
| 22 | `iot23_flow_match_audit_tests` | 合成Zeek标签和C流CSV验证独立CLI唯一恶意摘要，以及有效行后缺字段导致的非零退出、错误信息和stdout无半份摘要 |
| 23 | `ml_split_manifest_tests` | 抓包来源分组、同源不跨集合、训练／验证非空和非法清单拒绝 |
| 24 | `iot23_pilot_split_tests` | 同源开发试验的相同特征模式成组、重复切分稳定和冲突标签拒绝 |
| 25 | `tcp_model_input_tests` | TCP18维编码、类别独热、列顺序、元数据隔离和非法特征拒绝 |
| 26 | `tcp_candidate_dataset_tests` | 完整候选CSV加载、集合与样本身份、来源隔离、行号和字段契约 |
| 27 | `tcp_tensor_dataset_tests` | 可选ML：CPU float32、张量形状、空测试集、数据所有权与标签校验 |
| 28 | `tcp_feature_preprocessing_tests` | 可选ML：只用训练侧拟合、常量列、空集合、输入不变和参数契约 |
| 29 | `tcp_linear_baseline_tests` | 可选ML：合成训练、19参数、重复结果、类别权重、混淆矩阵、零分母和非法输入 |
| 30 | `flow_export_tests` | CSV表头、TCP状态字段、协议/状态不变量、记录顺序、格式化和无效参数 |
| 31 | `flow_expiration_tests` | 事件时间高水位、扫描边界、乱序时间戳、参数验证和截止时间下溢 |
| 32 | `runtime_metrics_tests` | 累计值差分、PPS/Mbps、流表占用率、零流量、时间边界和溢出保护 |
| 33 | `tcp_flow_state_tests` | 初始化、稳定名称、双向握手、重传、中途捕获、无效输入、FIN关闭、RST和终止状态 |
| 34 | `flow_features_tests` | 流特征总量、均值、持续时间、方向不平衡度、TCP状态关系、错误边界和失败不修改输出 |
| 35 | `flow_feature_export_tests` | `flow_features_v1`表头、TCP/非TCP行、布尔编码、浮点精度、状态不变量和写入错误 |

只运行重点测试示例：

```bash
cmake -E chdir build ctest \
    -R '^(analyzer_smoke_tests|capture_tests)$' \
    --output-on-failure
```

只运行离线端到端验收：

```bash
cmake -E chdir build ctest \
    -R '^offline_flow_acceptance$' \
    --output-on-failure
```

为什么真实实时抓包没有自动化验收：

- CI或开发机不一定有同名网卡；
- 抓包可能需要root或capability；
- 实时流量时序不确定；
- 不能假设外网可用；
- 自动测试不应修改系统网卡权限。

因此当前对实时功能采用三层验证：参数单元测试、采集接口的不依赖权限错误测试、人工`lo`验收。BPF使用离线确定性PCAP验证过滤语义；信号唤醒、`pcap_stats()`成功路径、实时流过期和静默周期报告使用真实`lo`手工验收。过期验收通过两次间隔31秒的IPv4 ping观察到一条过期ICMP流和一条最终活动ICMP流；周期指标验收覆盖静默、突发流量、再次静默和Ctrl+C。

## 13. 已完成的实验项目

### 13.1 `labs/blocking_queue`

实现了基于pthread互斥锁和条件变量的阻塞队列，覆盖：

- 初始化、push、pop、关闭、销毁；
- 环形`head`和`tail`；
- `count`区分空与满；
- `not_empty`和`not_full`条件变量；
- `while`循环防止虚假唤醒和条件变化；
- 关闭后唤醒等待线程；
- 基础、生命周期、阻塞和并发测试；
- 测试拆分及公共helper。

用户已经理解：环形下标按容量回绕，而可读取元素数量由`count`决定；队列未填满不会因此出错。

### 13.2 `labs/tcp_framing`

实现了：

- 自定义消息头编码/解码；
- magic、版本、类型和payload长度；
- 网络字节序；
- `send_exact`/`recv_exact`处理短写和短读；
- TCP服务端监听socket和每客户端通信socket；
- 客户端连接；
- 消息头和payload分阶段读写；
- 单元和集成测试。

用户已理解：TCP是字节流，一次`send`不保证发送完整buffer，一次`recv`也不保证拿到完整消息，因此必须循环；服务器监听socket只负责accept，每个连接另有通信socket，双方都必须关闭自己的描述符。

### 13.3 `labs/thread_pipeline`

实现了：

- 线程安全指针队列；
- 输入、worker和输出三类线程；
- 两个队列连接三阶段；
- 线程上下文结构；
- 堆对象的所有权转移；
- CSV输出；
- C单元测试和Python验收脚本。

用户已理解：线程入口只有一个`void *`参数，因此用context打包多个依赖；生产者在堆上创建对象并把指针推入队列，消费者取得指针后接管所有权，使用完成后`free`。

这些实验暂未接入正式程序。接入前应先测量实时采集、解析和输出是否存在速度差异，再决定线程数、队列容量、背压和关闭顺序，不要为了“线程多”而提前复杂化。

## 14. 已记录的主要问题和面试素材

详细过程见`docs/problem_log.md`。重要问题包括：

1. 二进制数据最初使用字符串思维处理，遇到零字节后改为边界检查和`memcpy`；
2. VS Code不知道CMake头文件目录，通过正确工作区和`compile_commands.json`修复；
3. 离线主循环错误地把“预览5包”当成“只处理5包”，拆分总包计数和预览计数后修复；
4. Python验收脚本`with`缩进错误导致测试未真正进入C程序；
5. 流表测试存在先用实际结果生成期望值的恒真断言，代码评审后修复；
6. CSV验收路径被放入错误函数，触发Python局部变量`NameError`；
7. 新增`parsed_interface_name`后漏初始化，原有`--read`测试被误判为输入冲突；
8. 未主动`ping`仍捕获到`lo` TCP包，通过`ss -tnp`定位到VS Code本地通信；
9. libpcap的`caplen`和`wirelen`容易混淆，明确规定前者用于内存边界、后者只用于线路统计；
10. 测试通过不等于测试有效，断言本身也需要评审。
11. `lo`上应用处理4个ICMP包而`pcap_stats().ps_recv`报告8，确认是回环方向事件与libpcap重复抑制造成的统计层级差异。

讲面试故事时推荐结构：

```text
背景和目标
→ 实际现象或失败输出
→ 如何缩小范围
→ 根因
→ 修复
→ 自动化或手工验证
→ 后续如何防止复发
```

不要只说“项目很顺利”或只说“查资料解决了”，应引用具体错误、数据和验证。

## 15. 重要技术决策

详细理由见`docs/technical_decisions.md`。当前关键决策：

- Linux C11作为核心实现语言；
- CMake组织多模块和测试；
- libpcap作为采集后端；
- 先离线PCAP建立确定性输入，再接实时网卡；
- tcpdump用于快速抓包和命令行对照，Wireshark用于图形化逐字段验证，不作为业务程序依赖；
- 项目公开接口不暴露libpcap类型；
- 网络包采用边界检查字节游标和明确长度，不使用字符串API；
- 解析层分为Ethernet、IPv4、TCP/UDP/ICMP和分发器；
- 双向五元组通过端点排序规范化；
- FNV-1a 64位用于稳定流键哈希；
- 开放寻址和线性探测用于当前固定容量流表；
- `DELETED`保留探测链；
- 活动流放内存，当前用CSV进行测试、演示和离线交换，不急于引入数据库；
- 可视化优先Qt上位机，云端展示作为后续扩展；
- BPF、信号退出和libpcap运行统计都停留在capture边界内，不向上层暴露第三方原生类型；
- 捕获后端计数与应用处理计数分别输出，不能用二者差值计算丢包；
- 不在没有性能数据时提前引入AF_XDP、DPDK或复杂线程池。
- 持续服务由程序显式保证stdout按行刷新，不依赖终端探测结果或外部`stdbuf`命令。

数据库目前不是必需项。只有出现历史查询、持久告警、用户配置、跨重启状态等明确需求时，再评估SQLite或外部数据库。

## 16. 当前已知边界和风险

### 16.1 实时抓包

- 已支持BPF、SIGINT/SIGTERM优雅退出和`pcap_stats()`累计统计；
- `--count`只限制成功捕获的包数，不限制等待时间；
- libpcap读取超时在不同平台和捕获后端上的行为可能不同，不能把它当作严格定时器；
- `pcap_stats()`字段语义和可用性依赖平台，`ps_recv`不等于应用完成处理的包数，`ps_ifdrop == 0`也可能表示指标不可用；
- 应用主循环已经提交完整、截断、畸形、不支持和流表拒绝分类，并在周期及退出汇总中输出；
- 当前混杂模式固定为false，没有CLI选项；
- 普通流记录`--csv`仍只支持离线模式；`--feature-csv`已经支持有限实时模式，但必须同时提供`--count`，尚无无上限服务所需的轮转、配额和恢复协议；
- `any`接口在Linux通常是cooked capture链路类型，当前只支持Ethernet，可能返回`ENOTSUP`；
- `lo`在当前环境表现为Ethernet链路并能解析，但其他平台不应无条件假设相同DLT。

### 16.2 协议

- 只支持Ethernet II和IPv4；
- 不支持VLAN、IPv6、PPPoE、Linux cooked capture和隧道；
- 不做IPv4分片重组；
- 已完成基于方向和标志的TCP握手、中途捕获、FIN与RST基本状态跟踪；
- 不做TCP乱序重组和字节流重组；
- 不核对ACK号是否精确确认SYN或FIN，同一五元组在`closed`或`reset`后的重新建连尚未处理；
- 不解析TCP选项；
- 不验证IPv4/TCP/UDP/ICMP校验和；
- 已有独立DNS固定头读取器，但尚未接入应用处理链；域名、问题和资源记录尚未解析，也不解析HTTP、TLS等应用层协议；
- 未实现DPI内容识别和规则异常检测。

### 16.3 流表

- 固定256槽；
- 达到容量返回`ENOSPC`；
- 已输出数据包路径累计操作数、检查槽位总数、平均和最大探测长度；
- 实时主循环已经按事件时间定期调用过期API；接口完全静默时仍要等下一包推进事件时间；
- 配置`--feature-csv`时，过期记录会在删除前、淘汰记录会在覆盖前输出值副本；未配置特征文件时仍只进入终端生命周期输出；
- 没有自动重建或动态扩容；实时模式已经提供显式最旧流淘汰，但默认仍为拒绝；
- 没有并发保护。

### 16.4 流特征和机器学习

- 已完成`flow_features_v1`、IoT-23 TCP候选加载、训练侧预处理及首次线性模型训练；阈值固定为logit 0，尚无阈值优化、模型持久化或在线推理；
- 首次跨来源验证全部判恶意，与恒判恶意基线相同；验证正常侧仅2条，不能据此声称跨场景识别有效或部署低误报，详见TD-042；
- 特征CSV不输出IP和端口，可以降低身份记忆风险；独立`flow_sample_metadata_v1` sidecar现已用稳定样本ID连接身份、匹配状态和可选监督标签；
- `protocol`和`tcp_phase`必须按类别特征处理，`schema_version`不能进入模型；
- 训练集与实时推理必须对齐列名、顺序、单位、方向定义、类别编码和归一化参数；
- 优先选择能够取得原始PCAP的公开数据集，让相同C提取器生成特征；只有预聚合表格时，需要逐项记录可映射、需换算和无法复现的字段；
- 新增特征、数据集和模型模块尚未在LubanCat-2N回归，板端仍以此前18项为已验证基线，不能宣称特征实时导出或模型推理已上板验证。

### 16.5 工程质量

- 当前严格警告和全部测试通过；
- 源码存在少量不影响功能的格式不一致，用户已要求暂不专门处理格式问题；
- 实时路径只有手工验收，没有环境独立的端到端自动化；
- ASan与UBSan独立构建已通过新增标签审计测试前的20项CTest，但还没有正式接入主项目构建预设；标签审计、身份规范化、样本元数据和匹配审计四项Python测试尚未在该构建树重新配置执行。TSan也未配置，不能把本次单线程测试解释为已经完成并发竞态验证。

## 17. 已完成阶段：BPF过滤

本节保留BPF实施时的设计依据和测试方案，相关接口、CLI、自动化测试和`lo`人工验收均已完成。不要再把本节当成当前下一步重复实现。

### 17.1 为什么现在做BPF

最近`lo`验收没有主动`ping`却先抓到VS Code TCP流，直接证明当前实时模式缺少“选择目标流量”的能力。BPF可以让libpcap在数据进入当前分析循环前筛选包，例如：

```text
icmp
tcp
udp port 53
host 127.0.0.1 and icmp
tcp port 8080
```

对实时抓包而言，过滤不仅让演示更确定，也能减少无关包进入用户态后的解析和聚合开销。

### 17.2 建议的公开采集接口

在`include/analyzer/capture.h`增加类似接口：

```c
int capture_set_filter(
    capture_t *capture,
    const char *filter_expression,
    char *error_buffer,
    size_t error_buffer_size);
```

建议语义：

- `capture`必须已经成功打开；
- 表达式必须非空；
- 错误缓冲区与大小成对提供或同时省略；
- 成功返回0并清空错误缓冲区；
- 参数错误返回`EINVAL`；
- BPF编译或安装失败返回`EIO`并复制libpcap错误；
- 失败不关闭capture，调用者仍负责`capture_close`。

### 17.3 建议的libpcap实现流程

`src/capture/capture.c`内部可使用：

```text
pcap_compile
    ↓ 成功
pcap_setfilter
    ↓
pcap_freecode
```

关键点：

- `struct bpf_program`属于临时编译结果；
- 无论`pcap_setfilter`成功还是失败，只要`pcap_compile`成功，就必须调用`pcap_freecode`；
- 初版可以使用`PCAP_NETMASK_UNKNOWN`；
- 优化参数可以设置为1；
- `pcap_geterr`返回的字符串依赖采集句柄，如需关闭句柄前保留，必须先复制；
- 不要把`struct bpf_program`或libpcap宏暴露到公开头文件。

正式编码前建议查阅当前系统安装的libpcap手册或官方文档，确认`pcap_compile`、`pcap_setfilter`、`pcap_freecode`和网络掩码语义。

### 17.4 建议的CLI模型

在`app_context_t`加入借用argv字符串的：

```c
const char *filter_expression;
```

目标命令：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"
```

建议第一版只允许`--filter`和实时`--interface`组合，避免同一步扩张过大。以后可以自然放宽到离线PCAP过滤，因为libpcap过滤器同样可用于离线句柄。

应用层应在以下顺序中安装过滤器：

```text
打开capture
→ 查询/验证链路类型
→ 安装BPF
→ 初始化或进入读取循环
```

如果安装过滤器失败，要复制错误信息、关闭capture并返回，不能进入未过滤读取。

### 17.5 建议的测试

采集模块单元测试不应依赖root或真实网卡。推荐使用测试生成的离线PCAP：

1. 打开离线PCAP；
2. 安装合法表达式，例如`icmp`；
3. 确认合法表达式编译和安装成功；
4. 使用明显非法表达式确认返回`EIO`且错误字符串非空；
5. 最好构造至少一个ICMP包和一个TCP/UDP包，安装`icmp`后验证只读到ICMP包；
6. 验证空表达式、空capture和不匹配错误缓冲区参数返回`EINVAL`；
7. 保持已有capture测试全部通过。

应用层冒烟测试覆盖：

- `--interface lo --count 4 --filter icmp`合法；
- 缺少filter值非法；
- 空字符串非法；
- 重复`--filter`非法；
- 如果第一版只支持实时模式，`--read file.pcap --filter icmp`非法；
- 初始化、重新解析和cleanup后`filter_expression`状态正确。

人工验收：

终端1：

```bash
sudo ./build/bin/netflow-analyzer \
    --interface lo \
    --count 4 \
    --filter "icmp"
```

终端2：

```bash
ping -c 2 127.0.0.1
```

预期只看到4个ICMP包，不再被VS Code的TCP回环流量提前占满`--count`。

### 17.6 BPF完成后的文档和提交

测试通过后由助手更新：

- README参数和示例；
- CHANGELOG Unreleased；
- technical_decisions中的BPF选择、过滤位置和性能意义；
- 如遇真实故障，再更新problem_log。

建议提交标题：

```text
feat(capture): add BPF packet filtering
```

如果采集API和CLI改动较大，也可以拆为两个独立提交：

```text
feat(capture): add BPF filter support
feat(cli): expose live capture filter option
```

## 18. 当前推荐路线

当前路线（2026-11-01）：公共串口库、完整行回调、TCP收发及公共接口桥接四项VM回归已通过但相关改动未提交。下一步由用户填写独立单客户端入口与CLI专项，先用回环地址和不存在的设备路径验证参数、等待时限、等待中的两种受控信号及设备打开失败。再安排板端身份／权限／占用／ModemManager预检和有限时的真实串口到TCP验收，由用户观察打开、运行、退出三阶段心跳。当前不拔线、不做断电实验，不自动启用后台服务。STM32保护与MOS仍本地执行，分析器独立旁路，Qt入口和跨项目联动仍未验收。源码及Git由用户操作，助手维护既有文档，每步结束给出commit标题。

此前路线（2026-10-12）：为10月14日下午招聘会冻结功能范围，优先形成“设备通信→抓包→协议解析→双向流／TCP旁路状态→指标／特征输出”的短时可复现演示，以及简历与面试证据。新版ARM64物理网卡ICMP、TCP旁路完整握手与关闭及特征导出已经收到成功数据链实测，下一项补齐已有TCP运行的退出码与消息核对证据，再整理展示材料；见[jobfair_demo.md](jobfair_demo.md)。不更新板端旧分支或覆盖原服务程序，不启动常驻LLM，不重复一小时实验。DNS进一步解析、模型保存加载／上板、GUI、云端及正式多线程流水线均暂缓；AI实验和质量失败记录保留，但不作为可靠检测功能或本轮演示的必要条件。

此前路线（2026-10-02，招聘会后再评估）：围绕协议分析、嵌入式Linux与AI工程落地收拢范围，优先推进第一个应用层DNS模块。固定头已提交并通过37项本地回归；原计划下一步为域名标签、压缩指针和长度／跳转边界，随后再接Question区域与UDP处理链。暂缓原生x86_64 LLM参考构建及进一步提示词实验，不创建常驻LLM服务。此前三例、normal对照及事实抽取的质量失败记录全部保留，根因仍未定位；详细证据见[local_llm_deployment.md](local_llm_deployment.md)和TD-043。保留现有TCP分类和模型产物模块，正常误报与输入碰撞限制不因新增DNS而自动解决；本轮不实现主机窗口、规则检测、有界队列或在线告警。

### 18.1 信号优雅退出（已完成）

当前已经使用`sigaction()`、`volatile sig_atomic_t`和`pcap_breakloop()`实现Ctrl+C及SIGTERM的安全结束。下面保留实施时需要持续遵守的约束。

注意：

- 信号处理函数只能执行异步信号安全的最小操作；
- 普通`bool`不一定适合直接作为信号通信对象，通常使用`volatile sig_atomic_t`标志；
- handler中不要调用`printf`、`malloc`、`free`、`fclose`或复杂清理；
- 主循环观察标志后按正常路径关闭capture、输出流和清理资源；
- libpcap阻塞读取是否需要`pcap_breakloop`以及它在目标平台的信号安全语义，实施前要核对官方文档；
- 后续可考虑`--count`变为可选：达到数量或收到信号均退出。

### 18.2 抓包运行统计（基础完成）

`capture_statistics_t`和`capture_get_statistics()`已经封装`pcap_stats()`，实时退出时分别输出捕获后端累计接收、抓包缓冲区丢包、接口丢包和应用实际取得的总包数。平台原生`struct pcap_stat`没有暴露给上层，离线句柄明确返回`ENOTSUP`。

协议解析结果分类、流表拒绝、容量淘汰、CPU、RSS以及线性探测成本均已可观察；实时满载策略支持默认拒绝和显式最旧流淘汰。周期PPS、Mbps、流表占用率、累计过期和淘汰数量已经完成。

### 18.3 实时流过期（已完成第一版）

实时主循环已经接入以下策略：

- 以最大已观察数据包时间戳作为事件时间高水位，乱序包不能让时间倒退；
- 空闲超时固定30秒，事件时间每推进5秒扫描一次；
- 在处理当前包之前扫描，防止长时间空闲后的同五元组包刷新旧记录；
- 流表先把过期`flow_record_t`按值复制到调用者的256条固定缓冲区，再删除槽位；
- 运行期间逐条输出`Expired flow`，退出时分别显示累计`Expired flows`和剩余`Flow summary`；
- 离线PCAP仍保持完整文件级聚合，不启用周期过期；
- 单元测试覆盖扫描边界、乱序、值副本、容量不足和整数下溢，当前全部17项CTest通过；
- `lo`上两次间隔31秒的IPv4 ping人工验收通过。

已知边界：接口完全静默时事件时间不推进，旧流要等下一包到来才输出；超时和周期尚未开放CLI配置；实时过期流和容量淘汰流当前只输出终端，没有追加到持续CSV；固定流表仍缺少动态扩容和可配置容量。

### 18.4 周期运行指标与静默唤醒（已完成）

当前已经完成：

- 独立`runtime_metrics`模块按累计值差分生成区间包数、字节数、PPS、捕获/线路Mbps、流表占用率和累计过期数量；
- 使用`CLOCK_MONOTONIC`测量真实经过时间，不受系统墙钟调整影响；
- 实时句柄显式进入非阻塞模式，没有可交付包时返回暂时无数据；
- capture层通过可选择文件描述符和`poll()`提供有界等待，应用每次返回后检查周期任务和停止请求；
- 没有增加统计线程，避免为当前单线程所有的计数和流表引入锁、一致快照及线程回收复杂度；
- 静默、突发60个ICMP包、再次静默和Ctrl+C人工验收通过，全部17项CTest通过。
- 五种互斥的数据包处理结果已经接入`app_process_packet()`、累计量、周期报告和退出汇总；流表满载的新流包计入拒绝后继续运行。

已知边界：当前报告周期固定为5秒，等待检查粒度固定为1秒；`poll()`依赖POSIX可选择描述符；单流、多流、整机软中断和10分钟长稳已经测量，但严格周期边界、千兆高PPS和生产级数小时/数天浸泡仍待验证。

### 18.5 依据性能测量暂缓接入线程流水线

候选正式结构：

```text
capture线程
    ↓ packet队列
worker线程（解析与流聚合）
    ↓ result/event队列
output线程（CSV/Qt/告警）
```

但是原始`packet.data`由libpcap拥有，只到下一次读取有效。capture线程如果把包交给其他线程，必须复制`captured_length`字节到拥有明确生命周期的堆对象，不能直接把libpcap内部指针推入队列。这是正式接入线程时最重要的所有权变化之一。

首轮LubanCat ARM64原生Release实测：

- 空闲进程平均CPU约0.045%，最大RSS约1.72 MiB；
- 约8.8～9.1 Kpps时处理20万包，进程平均CPU约4.25%，整体成本约6.95微秒/包；
- 应用拒绝、捕获drop和接口drop均为0，RSS保持约1.60～1.72 MiB。

因此当前不把实验流水线接入正式程序。完整命令、计算和边界见[`docs/performance_baseline.md`](performance_baseline.md)。

### 18.6 流表线性探测可观测性（已完成）

当前已经完成：

- `flow_table_process_packet()`累计数据包操作数、检查槽位总数和单次最大探测长度；
- 起始槽位计为1，命中、插入和满载`ENOSPC`扫描均进入统计；
- `flow_table_find()`等管理查询不污染数据包路径统计；
- `uint64_t`累计值使用饱和保护，饱和后退出报告不再显示误导性的平均值；
- 单元测试覆盖确定性1、2、3槽冲突链、查询隔离、满表扫描、失败输出不变和饱和不回绕；
- 离线端到端验收覆盖6包无冲突平均值1，以及260包满表最大值256；
- 全部17项本地CTest通过。

LubanCat官方SDK交叉产物实测：

- 128流、20万包、50%占用率：`operations=200000`、`inspected_slots=200000`、`average=1.00`、`maximum=1`，零拒绝和零drop；
- 300流满载：`operations=300`、`inspected_slots=11928`、`average=39.76`、`maximum=256`；44次拒绝贡献11264次完整扫描，前256次成功建流平均检查约2.59个槽；
- 探测统计版20万包进程CPU成本约6.95微秒/包、最大RSS 1724 KiB，与此前基线处于同一量级。

结论：当前受控50%占用工作负载没有哈希查找退化，无需为了探测性能立即更换哈希或动态扩容；固定256条活跃流仍是独立的业务容量边界。下一步若处理容量问题，应先明确内存上限、驱逐偏差和被驱逐流的输出所有权。

### 18.7 实时最旧流淘汰与单次扫描（已完成）

当前已经完成：

- CLI增加`--flow-full-policy reject|evict-oldest`，默认保持`reject`；
- `flow_table_evict_oldest()`按最小`last_seen`选择记录，删除前返回不依赖槽位生命周期的值副本；
- 新增`flow_table_process_packet_with_oldest_eviction()`，与默认拒绝路径复用内部探测逻辑；满表扫描同步记录最旧候选，确认新键不存在且所有槽位均被占用后，在原槽位发布已经完整初始化的新记录；
- 替换前按值复制旧记录，槽位保持`OCCUPIED`、`count`保持不变；当前包计入`complete`，淘汰事件单独计入周期和退出`evicted_flows`；应用层只选择策略，不取得槽位指针或下标；
- 单元测试覆盖最旧选择、刷新后保留、`DELETED`复用、值副本、探测统计隔离、空表、无效参数、内部计数不一致和最后一条流；
- 组合接口测试还覆盖普通插入、已有流更新、满表替换后的容量与查找、每包只增加一次探测操作，以及无效包、空输出参数、损坏的内部计数和全部失败输出不变；
- CLI测试覆盖默认值、重新解析复位、合法值、缺失、空、未知、重复、离线组合、无来源以及绕过解析的非法上下文；
- 优化前Ubuntu `lo`基线为`operations=344`、`inspected_slots=25055`、`average=72.83`和`maximum=256`；最新官方SDK ARM64 Release板端基线为344次操作、25505次槽位检查、平均74.14、最大256、44次淘汰和两个drop字段为0；
- 优化后Ubuntu `lo`用300个不同UDP五元组复测：`operations=300`、`inspected_slots=12416`、`average=41.39`、`maximum=256`、`complete=300`、`flow_rejected=0`、44次淘汰、最终256条流且两个drop字段为0；被淘汰端口从30000到30043，与时间顺序一致；
- 优化后官方SDK ARM64产物在LubanCat-2N得到`operations=300`、`inspected_slots=15364`、`average=51.21`、`maximum=256`、`complete=300`、`flow_rejected=0`、44次淘汰、最终256条流、后端接收303包和两个drop字段为0；
- 官方GCC 9对局部探测结果给出的保守未初始化告警已经通过显式零初始化消除；本地重点测试和全部17项CTest、官方SDK Release构建、`git diff --check`及板端运行均通过。

旧实现每次满表新流执行“256槽满表探测、256槽独立淘汰扫描、256槽插入重试”。44次淘汰中，已报告的重试扫描为`44 * 256 = 11264`次槽位检查，未进入探测统计的独立淘汰扫描也是11264次。新实现将三次整表扫描收敛为一次，最多省去22528次槽位访问；`operations`从344降到300直接证明重试已经消除。两轮测试源端口和哈希分布不同，因此不能用25055和12416计算严格的CPU提升百分比；若需要性能结论，应使用完全相同的五元组序列和构建产物做成对测试。

### 18.8 应用层与DPI

推荐顺序：

1. TCP连接状态基本跟踪（已完成第一版）；
2. DNS解析，先建立独立UDP应用层模块：固定头已完成，域名和压缩指针为当前下一步；
3. 同一五元组重新建连语义与TCP字节流重组，为后续TCP应用层协议准备；
4. HTTP/1.x请求行和头部；
5. TLS ClientHello元数据，例如SNI和版本；
6. 可配置规则和异常检测；
7. 特征导出与受限的离线机器学习训练／评估已完成；ARM64模型推理与在线预警仍待实现和验收。

没有TCP重组时，不能可靠地假设一个应用层消息完整存在于一个TCP包中。

第一版状态跟踪已经完成：每条TCP流拥有独立状态，支持完整握手、中途捕获、FIN关闭和RST，并通过稳定名称输出到终端与CSV。下一阶段进入重组前，应先定义序列号比较、乱序和重叠段、重复数据、每流内存上限、超时清理，以及`closed`/`reset`后相同五元组的新SYN如何创建新生命周期。状态跟踪解决“连接处于什么阶段”，字节流重组解决“按什么顺序向应用层交付哪些字节”，两者不能混为一谈。

### 18.9 异常检测

先实现可解释规则，再做机器学习：

- 单源短时间大量目标端口：端口扫描；
- SYN多、完成握手少：SYN异常；
- DNS请求速率或域名长度异常；
- 单流字节/包速率异常；
- 大量失败连接；
- 流持续时间和方向比例异常。

规则输出应使用稳定事件模型，后续Qt和云平台都消费同一数据，不应让检测逻辑直接依赖GUI。

第一版稳定流特征和`flow_features_v1`导出已经完成，但这不等于机器学习异常检测已经完成。公开数据集仍应优先提供原始PCAP或足够完整的字段定义，并逐列核对名称、单位、方向、类别含义和标签来源；优先让公开PCAP通过同一个C提取器生成训练特征，降低训练与实时部署之间的语义偏差。

当前特征文件仍不直接包含标签、IP、端口或样本ID。第一版`flow_sample_id_v1`和`flow_sample_metadata_v1` sidecar已经把模型输入与身份/标签连接分离，并用特征行号记录同一次分析中的对应关系。若公开数据集只提供预聚合表格，仍必须显式验证字段映射，不能把任意文件按行拼接。取得足够的唯一匹配样本后，再建立Python训练/验证划分、预处理参数保存、基线模型和评价指标，最后才评估是否把推理部署到ARM64板端。

CTU-13 Scenario 7的第一轮匹配审计已经完成：审计器要求规范化双向五元组相等且时间区间重叠，并把结果分成唯一、未匹配、同标签歧义和冲突标签歧义。前1000包的47条流全部落在保守策略排除的背景标签范围，因此正式监督索引中均未匹配；感染主机DNS子集的2条流都覆盖多个恶意Argus记录，因此属于同标签歧义。sidecar现已能为每条结果生成稳定ID并保留匹配质量，但公开TCP截断和流边界差异仍阻止当前数据直接形成一对一训练样本。下一步用候选空闲阈值重跑真实匹配审计，然后才决定是否扩展场景并进入训练代码。

### 18.10 可视化

当前建议：

- 先做Qt上位机，展示实时流列表、协议分布、包/字节速率和告警；
- 核心分析程序保持C模块，Qt可以使用C++并通过C API、进程间通信或稳定文件/消息格式连接；
- 云平台作为后续扩展，用于远程设备、长期历史和多节点汇总；
- 不需要现在同时实现完整Qt和云平台。

### 18.11 非root systemd服务化（手工启停与开机自启完成）

当前已经完成：

- `CMakeLists.txt`通过`GNUInstallDirs`定义可移植的运行文件安装位置，并安装`netflow_analyzer_cli`目标；
- `packaging/systemd/netflow-analyzer.service`使用专用`netflow-analyzer`用户和组运行，不以root身份长期执行分析器；
- 服务只保留抓取原始网络数据所需的`CAP_NET_RAW`环境能力和能力边界，并启用`NoNewPrivileges`、只读系统目录、私有临时目录及内核接口防护；
- `/etc/default/netflow-analyzer`模板提供接口、BPF和满表策略参数，第一版默认只抓ICMP，避免启用服务后立即采集全部流量；
- systemd使用`SIGTERM`停止无上限实时循环，复用现有信号处理、libpcap中断、统计和正常清理路径；
- stdout在`main()`首次I/O前被显式设置为行缓冲，周期报告可以及时进入journal，而不依赖`stdbuf`；
- 本地CMake配置、构建、18项CTest、临时安装前缀和服务单元静态检查已经完成。静态检查只因本机尚未安装`/usr/local/bin/netflow-analyzer`而报告目标不存在，没有发现单元语法错误；
- 严格的普通文件重定向测试排除了终端和sudo伪终端影响，在进程退出前已经读到周期报告。
- 官方SDK为提交`740d5ab`生成的ARM64部署包SHA-256为`f916176829974d61ff853310638fd0b07c8570f715d97f67238aebfad259c55d`，开发电脑和目标板传输摘要一致；
- 目标板创建了无登录、无home目录的`netflow-analyzer`系统用户和同名组；程序、配置和单元由root拥有，ELF本身没有文件capability；
- systemd手工启动后，进程实际UID/GID为专用账户，有效、边界和ambient能力只包含`CAP_NET_RAW`对应的`0x2000`，`NoNewPrivs=1`；
- 静默5秒周期报告在服务运行中进入journal；虚拟机2次ping对应4个完整ICMP包、4次单槽探测、1条双向流和两个drop字段为0；
- `systemctl stop`发送SIGTERM后输出最终汇总并正常退出；`Flow 1`是4包聚合得到的第一条流记录，不是额外包；
- 板端`journalctl`不能解析`date --iso-8601=seconds`产生的带`T`和时区偏移格式，已改用`date '+%Y-%m-%d %H:%M:%S'`或`-b`查询；
- 完整Linux命令、权限模型、journal用法、故障定位与回滚见[`docs/systemd_deployment.md`](systemd_deployment.md)。
- `enable`在`multi-user.target.wants`下建立启动依赖；受控重启后boot ID变化，无人工`start`时服务已为`enabled`和`active`；
- 新boot一次启动成功，`Result=success`、`NRestarts=0`，新PID继续使用专用用户和唯一`CAP_NET_RAW`能力；
- `critical-chain`显示服务排在`network-online.target`之后，当前物理`eth0`成功打开；该顺序不等价于互联网必然可达，异常网络恢复仍待单独验证；
- 当前boot的静默报告和虚拟机再次产生的4个ICMP包均正常进入journal。
- systemd 245安全审计基线为`5.2 MEDIUM`；报告已按业务必需、低风险候选和必须实验三类记录，不能为降低分数启用`PrivateNetwork=yes`或禁止`AF_PACKET`；

第一批低风险沙箱加固和评分复测已经完成。其后的VFAT持久化实验触发了厂商`resize-all.service`重建数据卡且恢复失败；旧`fstab`条目已删除，事故证据已保存，该服务已同时`disable`和`mask`，且后续boot仍为`masked`。工作树已经通过Git Bundle恢复到eMMC ext4，新Debug构建路径下18项CTest全部通过。修正`usbmount.conf`后，SD卡又完成只读文件系统检查、跨重启唯一自动挂载和普通用户创建/写入/读取/删除验证，现在只用作PCAP、CSV、数据集和日志的数据盘。同一阶段还自然观察到一次接口瞬态繁忙并由systemd在2秒后成功恢复；后续确定性不存在接口实验又证明正式`30s/5次`策略能停止连续失败循环，恢复和4包ICMP数据面复测均通过。服务后来连续运行数天，证明进程能够保持运行，但磁盘容量事故使该经历不具备严格受控长稳报告的条件；当前不重复一小时实验，生产级声明前再按需补做。地址族与系统调用限制也不再作为当前必做项。

### 18.12 STM32／BMS串口接入：首次有界接收通过

范围与职责（2026-11-01，首次串口阶段快照；最新进展见本节末尾）：

- 用户已有STM32F103ZET6／HAL／FreeRTOS的BMS和Windows Qt串口上位机。本阶段只新增Linux侧接入；保护和MOS控制仍由STM32本地完成。
- 源码正式归属为本仓库`gateway/bms_serial_recv.c`，独立于`src/`、`analyzer_core`和原CLI；当前尚未注册到CMake／CTest。`build/gateway/`是虚拟机产物目录，不提交；板端目录是实验部署副本，不代表Linux源码正式迁出netflow仓库。
- 已实现的本阶段链路仅为“STM32 USB TO UART→CH340→鲁班猫文本接收→文件”。TCP转发、Qt TCP入口和netflow-analyzer抓取这段联动流量均属后续计划，不把三个项目模块混写成已经联通。

设备与环境证据：

- 用户板端枚举到USB `1a86:7523`、驱动`ch341`、节点`/dev/ttyUSB0`；节点为`root:dialout`，模式`crw-rw----`。设备重插后仍须重新核对，不预设节点名永远不变。
- 板端账户已执行加入`dialout`，新SSH会话曾通过读写权限检查，本地旧桌面终端仍出现权限失败。后续网关预检已取得旧进程`id`无dialout、账户`id cat`含`20(dialout)`及组记录含cat的对照，确认旧会话未加载更新组身份；随后另一会话权限检查通过。新会话完整`id`未贴出，详见问题5.24。
- 首次149行运行前，ModemManager曾为`active/running`，`sudo fuser -v /dev/ttyUSB0`未列出当前占用；用户不确定当次是否停止服务，仍保留未知。后续SIGINT测试前有明确证据：权限通过，用户临时停止ModemManager，状态从`active/running`变为`inactive/dead`。停止不等于禁用，也不证明它此前实际干扰过串口；以后开口仍重新查状态与占用，需要变更时由用户确认并执行。
- 虚拟机源码SHA-256与用户贴出的板端结果均为`8ef2fbc5ecb46c15da613055a3546822a1dfe39cc8681bb454abefa649450a0d`；板端原生编译退出0，`file`显示ARM aarch64及加载器`/lib/ld-linux-aarch64.so.1`。本次未据此推断板端Git分支或提交。

部署与日志位置：

```text
板端源码：/home/cat/bms-serial-v1.i9yBPJ/bms_serial_recv.c
板端程序：/home/cat/bms-serial-v1.i9yBPJ/bms-serial-recv
运行目录：/home/cat/bms-serial-v1.i9yBPJ/receive-30s.AiBZSI
完整文本：receive.txt
配置与收尾：runtime.txt
```

程序配置为115200／8N1／无流控、内部单调时钟约30秒退出；给用户的运行命令另有40秒`timeout`和5秒强杀宽限。实际贴出的日志为：

```text
READY: /dev/ttyUSB0 115200/8N1 flow=none DTR=0 RTS=0 HUPCL=0
Receive for 30 seconds; Ctrl+C stops. No BMS command sent.
lines=149 discarded=1 tail_bytes=32 discarding=0
接收退出码：0
wc -l receive.txt：149
```

解释与验收边界：

- `lines=149`与文件行数一致；只说明149条完整文本行已输出，不等于每条BMS字段都已解析校验，也不是串口零丢帧证明。
- `discarded=1`与启动时主动丢弃首个LF之前片段的策略一致；不能把它当作硬件丢包计数。`tail_bytes=32`是退出时缓存内未形成完整行的尾部，按设计不发布；`discarding=0`表示退出时不在丢弃模式。
- 用户贴出的前5条均以`BMS`开头，含`state=1`、`faults=0x00000000`、`latched=0x00000000`、`load=0`，`sample`从91587递增到91595、`ctrl`从457941递增到457981。这些结论只覆盖已贴出的5条，不据此解释全部149条的保护状态或传感器准确性。
- 用户确认打开时、接收中和退出后三个阶段心跳持续正常，未观察到复位。因此首次真实有界接收及人工心跳观察通过；没有波形测量或复位原因寄存器证据，不能外推“所有开关口操作绝不复位”。
- DTR／RTS是驱动状态回读及程序日志，不是物理电平测量；程序没有发送BMS业务字符，但配置串口不等于对硬件毫无影响。退出时不恢复旧termios，不重新拉起DTR／RTS。
- 这是30秒配置下的成功运行；未提供独立墙钟计时、全量日志文件或丢帧基准，不声称精确运行时长、零丢帧或长期稳定。该首次运行不是信号测试；后续两种信号的证据单独记录如下。

后续SIGINT提前退出验收（同日）：

- 用户继续使用同一板端程序，日志目录为`/home/cat/bms-serial-v1.i9yBPJ/sigint-5s.yrjSSf`；运行前权限通过、ModemManager明确临时停止，`fuser`未列出占用。
- 命令使用`LC_ALL=C timeout --verbose --preserve-status --signal=INT --kill-after=5s 5s`，约5秒后自动发送INT，并保留程序自身的退出码；超时后仍不退出才再等5秒强杀。它不是实际键盘Ctrl+C操作，也未验证完整终端前台进程组行为。
- 用户贴出的关键输出为：

```text
READY: /dev/ttyUSB0 115200/8N1 flow=none DTR=0 RTS=0 HUPCL=0
Receive for 30 seconds; Ctrl+C stops. No BMS command sent.
timeout: sending signal INT to command '/home/cat/bms-serial-v1.i9yBPJ/bms-serial-recv'
lines=24 discarded=1 tail_bytes=0 discarding=0
SIGINT测试退出码：0
wc -l receive.txt：24
```

- 有明确发送INT的记录及之后的收尾统计，无强杀或清理错误输出；`wc`与24条完整文本计数一致。退出后再次运行`sudo fuser -v /dev/ttyUSB0`未列出占用，本次SIGINT收尾与资源占用检查通过。
- 用户贴出的最后3条日志仍以`BMS`开头，`sample`依次97677、97679、97681，`ctrl`依次488391、488401、488411；不把局部日志当作全量字段校验或零丢帧证明。
- 用户确认本次打开时、接收中和退出后三阶段心跳均正常，未观察到复位。该人工观察与首次149行运行分别确认，不把前次结果自动外推到本次。
- 这一项只覆盖受控SIGINT；后续SIGTERM独立记录如下，不把同一个handler或相似日志当作另一信号已经验证的依据。没有在板端重跑历史37项测试，也未启用网关常驻服务。

后续SIGTERM提前退出验收（同日）：

- 用户按5秒受控SIGTERM步骤继续使用同一板端程序，实际日志目录为`/home/cat/bms-serial-v1.i9yBPJ/sigterm-5s.uhjKgd`。本次未贴出运行前ModemManager复查输出，不推断服务当时状态；不能用首次或SIGINT的输出代替本次检查。
- 用户先提供收尾与占用输出，之后另行补充保存的`BMS_TERM_STATUS`为0，并确认打开时、接收中和退出后三阶段心跳都正常，未观察到复位。没有使用后来`fuser`命令的`$?`替代接收程序状态。

```text
READY: /dev/ttyUSB0 115200/8N1 flow=none DTR=0 RTS=0 HUPCL=0
Receive for 30 seconds; Ctrl+C stops. No BMS command sent.
timeout: sending signal TERM to command '/home/cat/bms-serial-v1.i9yBPJ/bms-serial-recv'
lines=24 discarded=1 tail_bytes=0 discarding=0
SIGTERM退出码：0
wc -l receive.txt：24
```

- TERM发送记录之后出现主流程收尾统计，文件行数与24条完整文本计数一致，无清理错误输出；退出后`sudo fuser -v /dev/ttyUSB0`未列出占用。本次SIGTERM收尾、资源占用检查和人工心跳观察通过。
- 本次能够再次打开并读取同一节点，为前次信号退出后重新打开提供成功证据，但不是拔线／重插、自动重连、systemd部署或长期稳定性验收。两次24行也不是精确时序或零丢帧证明。
- 仍待验证实际键盘Ctrl+C的前台进程组行为、设备断连与重新枚举及超长／异常行的板端路径；不为正常文本接收添加或发送S／T／C等BMS控制命令。

供电约束补充（同日，用户确认）：

- USB TO UART同时给STM32板供电，不能假设拔线后MCU仍持续运行；其他独立供电路径、跳线位置与电源隔离／防倒灌措施尚未核对。本轮不拔线，也不指导直接并接第二路电源。
- “通信断开但MCU保持运行”和“USB移除／设备掉电”必须分别定义。后者可另行验证Linux接收程序的设备移除处理，但不能用于证明MCU在通信中断时仍执行保护；电源变化后心跳停止也不能直接归因于DTR／RTS。
- 当前暂缓物理断连，真实设备移除／重插仍标待验证。随后先完成不动硬件的主机行缓冲与模拟断连测试，覆盖半行、多行、容量边界、残行和接收端挂断等逻辑；实际主机结果见下文。
- 伪终端不等于CH340，不能把模拟当作DTR／RTS或供电行为验证。若测试需要替代不支持的硬件控制操作，替代层必须显式隔离、限定测试设备，不得通过删除生产串口配置检查来让测试通过；主机结果不记为ARM64实物验收。

主机回归结果（同日，用户已填写并验证）：

- 用户明确本轮先不做断电实验。用户已添加`tests/unit/test_bms_serial_recv.c`，临时改名并包含原C文件，直接调用`feed_bytes`与`receive_lines`；不调用生产`main`或串口配置函数，不打开真实串口，也不删除／替代DTR／RTS检查。
- 用临时文件捕获实际`puts`输出，用匿名管道驱动真实`poll/read`；关闭管道写端产生模拟挂断，不涉及MCU供电。5组检查覆盖启动片段／半行／CRLF／多行／残行、1024与1025字节容量边界和恢复、NUL／ESC／DEL及空行处理、带小块数据挂断、无数据挂断。
- 挂断用例检查接收循环返回`-1`且`errno=ENODEV`，不是把断连当成功接收；测试自己关闭管道和捕获文件，不宣称已经覆盖生产`main`全部清理分支。带数据用例小于256字节，不宣称挂断时任意积压数据都已无损排空。
- 助手在虚拟机用编译器标准输入进行候选预检，只在临时目录生成普通与ASan／UBSan可执行文件；两版5组均通过，未写入仓库测试源码或修改CMake，也未运行真实串口。候选白盒测试不等于完整CLI、CH340驱动或ARM64断连实测。
- 用户已在现有`if(BUILD_TESTING)`块注册独立`bms_serial_recv_tests`目标，不链接`analyzer_core`，CTest超时10秒，并反馈成功。助手在虚拟机复核该目标构建及单项CTest，通过、退出0；当前本机构建配置共注册38项，但本轮没有重跑其余37项，更不等于ARM64已通过38项。修改的CMake与新增测试仍由用户提交，源码和既有文档改动均保留。

TCP发送模块主机验收（同日，用户已填写并验证）：

- 首版计划让鲁班猫充当TCP服务端、Qt主动连接，传输保持现有BMS文本，LF分行；只做数据上报，不下发S／T／C。监听、客户端接入已完成下文VM回环验收；串口输出对接及Qt入口尚未实现。
- 用户已添加`gateway/bms_tcp_io.h`、`gateway/bms_tcp_io.c`与`tests/unit/test_bms_tcp_io.c`：向已连接流式Socket发送最多1024字节正文并追加LF，用固定栈缓冲、部分发送偏移、单调时钟总预算、`MSG_DONTWAIT`和`poll(POLLOUT)`实现有界同步发送。它会等待，不能写成完整异步事件循环或保证串口绝不积压。
- `MSG_NOSIGNAL`避免对端断开时SIGPIPE终止进程；信号中断返回EINTR供上层退出。函数借用fd，不关闭或修改输入；失败可能已有前缀发送，未来调用方必须关闭该客户端，不能在同一字节流里整行重发。成功只代表本机内核接收，不等于Qt已处理。
- 测试用`socketpair(AF_UNIX, SOCK_STREAM)`，不监听TCP端口、不打开串口；仅测试目标通过`--wrap=send`把单次发送限制到3字节，验证短写后顺序与内容，同时检查正常行、参数拒绝、对端关闭及发送缓冲区满的超时。包装不进入正式网关链接配置。
- 此前候选普通与ASan／UBSan预检5组均通过。用户填写代码并反馈成功后，助手重新构建并运行`bms_serial_recv_tests`、`bms_tcp_io_tests`，两项均通过、退出0，合计约0.03秒；`ctest -N`确认当前注册39项，本轮只运行这两项，不等于39项全通过或ARM64新增测试已验收。用户CMake／源码及已有文档改动均保留，未执行Git写操作。

TCP监听与接入主机验收（同日，用户已填写并验证）：

- 用户已添加`gateway/bms_tcp_server.h`、`gateway/bms_tcp_server.c`与`tests/unit/test_bms_tcp_server.c`，仍为独立网关模块，不改抓包主循环。`bms_tcp_listen_ipv4`接受显式数字IPv4地址和端口，创建非阻塞／close-on-exec监听fd；`bms_tcp_accept_client`用单调时钟总预算及`poll`等待连接，返回同样带上述标志的客户端fd。EINTR上交调用方，不无条件重试；fd由调用者拥有并关闭。
- 测试只绑定虚拟机`127.0.0.1`、端口0，由内核选择空闲端口；覆盖真实IPv4回环连接、接入后调用既有发送模块验证完整LF行、同地址端口冲突、无人连接的超时、无效参数、阻塞fd拒绝及已关闭fd。不打开串口、不接触Qt，不启动常驻监听。
- 此前助手在仓库外`/tmp/nfa-bms-tcp-server.guxd0l`用编译器标准输入完成普通与ASan／UBSan两版3组候选预检。用户填写并反馈成功后，助手重新构建上述三个网关目标并运行三项CTest，全部通过、退出0，合计约0.05秒；`ctest -N`确认注册40项。没有重跑其余37项或进行信号注入、板端TCP、实物断连测试，未执行Git写操作。

完整行回调主机验收（同日，用户已填写并提交）：

- 用户已在同一文件增加`line_sink_fn`，由`feed_bytes`和`receive_lines`显式接收回调及`void *context`，不再在行整理函数中直接调用`puts`；独立接收CLI选择`stdout_line_sink`，不改变串口配置、1024字节上限、启动片段丢弃及SIGINT／SIGTERM路径。此步仅解耦完整行输出，不直接启用TCP或修改Qt。
- 回调获得不含LF、以NUL结尾的行和显式长度；行缓存只在回调期间借用，不能保存指针供以后使用，context仍由调用者拥有。回调返回0才递增成功交付行数；返回-1时保留errno并立即结束本次接收，不能盲目重试失败行或继续复用失败状态。未来发送失败可能已交付字节前缀，客户端关闭及会话恢复策略仍待实现。
- 原5组主机回归上新增2组：自定义回调复制两条完整行并接收context；第二条注入EPIPE后只计首条成功、第三条不再调用，空回调按EINVAL拒绝。该EPIPE是测试注入，不代表真实TCP／USB断开已验证。
- 此前候选普通与ASan／UBSan两版7组预检通过。用户填写并提交为`6032719`后，助手复核当前源码及三项网关CTest全部通过，直接运行`bms_serial_recv_tests`也得到原5组加新增2组的PASS。本次开始时工作区干净，未执行Git写操作。注册数保持40，没有运行其余37项或重新上板；原板端SHA与验收结果仍只对应旧源码。

管道到TCP桥接主机验收（同日，当时未提交，后续纳入a41203a）：

- 用户已在`bms_tcp_io.h/.c`中添加`bms_tcp_sink_t`与`bms_tcp_line_sink`，不为一个适配器拆额外业务模块。context携带借用的客户端fd与逐行超时；回调只调用既有`bms_tcp_send_line`，不保存行指针、不关闭Socket、不吞错误，也不整行重试。独立串口CLI仍选择stdout，不接入网络。
- 用户已新增`tests/unit/test_bms_gateway_bridge.c`及独立`bms_gateway_bridge_tests`目标。当前测试临时改名并包含串口C文件，只为调用真实`receive_lines`／`feed_bytes`，不调用串口main或配置函数；正式业务代码不包含其他C文件。使用匿名管道模拟输入和127.0.0.1、内核分配端口的真实TCP连接，输入少于256字节，不声称挂断时任意积压数据均已排空。
- 第一组先写入启动片段、两条合成BMS完整行及末尾残行，再关闭管道写端。接收循环预期返回-1／ENODEV，完整行计数2、丢弃1、残留7字节；TCP接收端逐字节核对只收到两条LF行。该预期输入挂断不等于完整网关CLI以0退出，也不涉及STM32掉电。
- 第二组用本地AF_UNIX流式Socket对构造对端已关闭，验证真实发送错误经回调和行整理函数返回、成功行数0、fd仍由调用者关闭；同时拒绝空context。不是IPv4 TCP断连时序、自动重连或硬件拔线验收。
- 此前候选普通及ASan／UBSan两组预检与发送模块5组回归通过。用户填写并反馈成功后，助手复核当前四个网关目标构建与四项CTest，全部通过、退出0（约0.06秒）；`ctest -N`确认注册41项，没有运行其余37项。HEAD仍为6032719，当前桥接实现／CMake／两份文档改动未提交，保留已有工作，未执行Git写操作。
- `timeout_ms`仍是逐行预算，不是整次接收的总预算；有界同步发送可能阻塞上游。接收循环的30秒检查只发生在循环边界，不能据此承诺加入TCP回调后整会话严格30秒结束；整体截止、停止响应、慢客户端及会话恢复策略由后续正式网关入口单独设计和验证。

串口公共接口主机验收（当时未提交，后续纳入a41203a）：

- 用户已添加`bms_serial_core.h`，以`BMS_SERIAL_LIBRARY`构建排除CLI main及私有信号状态的静态库；桥接测试已只包含头文件并链接该库，不包含生产C文件。配置接口复用旧115200／8N1、DTR／RTS／HUPCL检查；公共接收借用非阻塞fd、回调、context、停止标志和统计结果，不打开／配置／关闭fd、不清零停止标志。旧CLI与7组白盒测试保留兼容入口。
- 接收核心以绝对单调时钟截止时间限制poll等待，公共接口接受显式毫秒预算。预算不能强制中断阻塞回调，不构成硬实时保证。桥接测试含完整行传输、真实发送错误、阻塞fd拒绝、无输入预算及已置位停止标志检查。
- 此前仓库外3组公共接口普通／Sanitizer预检、原7组白盒回归通过；用户填写后，本次静态库与四个网关目标构建、四项CTest全部通过（约0.07秒）。当前注册41项但未全跑，HEAD仍6032719，相关改动未提交，未重新上板。

独立网关入口候选预检（历史阶段，后续用户已填写并提交）：

- 拟新增`gateway/bms_gateway.c`和`bms_gateway`目标（输出`bms-gateway`），链接串口库和现有TCP模块，不依赖analyzer_core、不安装系统路径或启用systemd。CLI为`SERIAL IPV4 PORT [SECONDS]`，数字IPv4、端口0至65535（0用于测试分配端口）、时长1至300秒，默认30秒。
- 入口一次只处理第一条会话：先接入客户端，再关闭监听并打开串口。应用层等待、接收、输出共享启动时建立的时间预算；接入每次最多等待200毫秒，发送每行最多100毫秒并受剩余预算约束。100毫秒是首版策略，不是测得的性能结果。驱动调用／调度／时钟粒度使它不构成硬实时退出保证。
- 入口拥有全部fd与独占状态，统一释放TIOCNXCL及关闭资源，不恢复旧termios或拉起DTR／RTS，不自动重连或转发客户端命令。没有认证或TLS，不用于不可信网络。单会话不等于并发竞争中每次第二个TCP connect都必定被拒绝。
- 等待超时且未开设备返回失败；受控停止可退出0，但不代表采集成功或所有数据到达。停止／发送失败可能留下TCP尾部残行，未来Qt必须只解析完整LF行；失败不整行重试。真实串口、完整会话中断连／背压／信号路径仍待验收。
- 拟新增`tests/integration/test_bms_gateway_cli.py`和CLI专项。只用Python标准库、127.0.0.1及内核分配端口，设备路径仅在临时目录构造而不创建。测试帮助／参数、无人接入1秒超时、等待中的SIGINT／SIGTERM，以及接入后打开不存在设备失败，均有有界进程和日志等待；不执行真实串口配置成功路径。
- 助手仅在`/tmp/nfa-bms-gateway-cli.YsaFG3`通过编译器标准输入生成普通和ASan／UBSan入口候选，两版CLI四组检查均通过、退出0。未创建仓库C／Python源码或修改CMake，当前41项尚不含拟新增的CLI测试。用户填写后先跑5项网关专项，不自动连接板端或重跑长稳。

独立入口已提交与板端隔离验收（2026-11-01，当前阶段）：

- 当前提交为`a41203af4ae748816a59bdfc4fb5af337e614bad`，入口、公共串口库和CLI测试已进入仓库，不再是待填写候选。本轮开始时工作区干净，分支与本地`origin/feature/flow-features`同步；没有执行fetch、提交或推送。
- VM重新构建`bms_gateway`及四个C测试目标成功，五项网关专项CTest全部通过，耗时1.16秒；没有重跑其他测试，不将此结果写作新版全部板端测试通过。
- 另用`cc -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -DBMS_SERIAL_LIBRARY=1`直接编译四个生产C文件，原CLI脚本四组检查通过。宏只用于排除旧串口CLI入口，不替代硬件配置检查；生产文件不用测试的`--wrap=send`。
- 使用`git archive`从上述提交打包七个gateway源码／头文件及现有CLI测试，未打包Git目录、虚拟环境、模型或历史构建。临时源码包为`/tmp/nfa-bms-gateway-a41203a.6dbtCZ/bms-gateway-a41203a.tar.gz`，SHA-256为`cf1d1df9e684834632b8df902f62d8f92fd93b42c99056bb31f92279025abda1`；`/tmp`路径可能被清理，后续使用前应检查存在性。
- 对文档中的`cat@192.168.1.102`执行一次有界、BatchMode、严格已知主机校验的只读SSH查询，返回255／`Permission denied (publickey,password)`；没有获得系统、IP、设备节点或服务状态等新板端输出。改由用户在终端交互认证，不收集密码或修改SSH配置。
- 用户随后在VM终端交互执行SSH命令，贴出`SSH_OK`、主机名`lubancat`、用户`cat`、新目录`/home/cat/bms-gateway-v1.9zr5rW`及退出码0。用户还确认VM主机为`zcb-Ubuntu`、用户`zcb`、工作目录`/home/zcb/workspace/netflow-analyzer`且源码包存在，排除了执行位置和源码包缺失；原整段脚本没有输出的具体原因尚未定位，不直接归因于认证或正则校验。
- 按该实际目录给出单独SCP及板端SHA-256检查命令后，用户回复“OK了”，确认传输与校验通过；没有贴出SCP退出码和完整校验原始输出，不补写。板端目录仅存放隔离源码包，不代表板端主仓库已同步。
- 用户随后贴出板端`aarch64`、严格C11原生编译退出码0、正常CLI帮助。`file bms-gateway`为64位ARM AArch64动态链接ELF，解释器`/lib/ld-linux-aarch64.so.1`，BuildID为`9242baf24f5aa8277b16b6619fe337c9dc992d30`，带调试信息、未剥离。`file`显示`shared object`不等于编译失败，程序的帮助和后续测试已实际执行；本次未另取ELF标志来独立核对PIE属性。
- 用户在同一板端目录执行现有`test_bms_gateway_cli.py`，外层`timeout`上限30秒，保存退出码0；四组原始PASS分别为帮助与参数校验、无客户端截止且不开串口、等待中的SIGINT／SIGTERM停止、接入后打开不存在设备的安全失败路径。脚本只用127.0.0.1、端口0及不存在的普通路径，不访问真实串口。没有提供实测耗时，不把30秒上限写成实际运行时长；不将四组脚本检查或此前五项VM专项改写为新版全量ARM64测试。
- 真实转发预检已贴出ModemManager为`loaded/inactive/dead`，实际设备仍为`/dev/ttyUSB0`、USB `1a86:7523`、`ch341`、`root:dialout 660`。旧终端初次权限失败，进一步取得当前进程`id`不含dialout、账户`id cat`含dialout及组成员记录含cat的明确对照；用户随后在另一会话的读写检查通过，不再修改组记录或设备权限。当前会话的完整新`id`、最新网卡地址输出及明确占用查询结果仍待补充，正式打开前复核，不把旧结果当作永久状态。
- 首次真实转发按两终端方案执行：板端网关使用30秒共享预算、外层timeout上限45秒，VM客户端只接收TCP原始文本、不发送业务字节。用户已贴出`LISTEN 192.168.1.102:18080`、`OPEN_SERIAL /dev/ttyUSB0`及`READY: 115200/8N1 DTR=0 RTS=0 HUPCL=0; telemetry only`。该地址／端口现为本次真实实验端点，但仍是显式参数，不是程序固定默认值或常驻服务。
- 本轮VM HEAD已为用户提交的`cc0846f`，工作区开始时干净、较本地远程跟踪分支领先1条；该文档提交不改变板端源码包仍来自`a41203a`的事实。助手仅补记两份既有文档，不修改源码、不代跑板端或执行Git写操作。
- TCP契约：显式IPv4／端口参数、无固定默认端口；正文上限1024字节、LF分帧，原始BMS文本不包装JSON。仅遥测转发，不读取或转发客户端控制字符；没有认证／TLS。一次接入后关闭监听，默认30秒共享会话预算，不是常驻服务或自动重连。`LISTEN`、`READY`及统计只写本地stderr，不作为TCP消息。

首次真实UART到TCP验收（2026-11-01，数据、心跳及收尾均已确认）：

- 板端收尾统计为`lines=118 discarded=1 tail_bytes=0 discarding=0`。`discarded=1`与既定启动片段丢弃策略一致，不把它记作网络丢包率，也不能由118条一致推导出UART全量零丢失。
- 用户贴出VM客户端`[PASS] received complete BMS text over TCP`及退出码0，接收目录为`/home/zcb/bms-tcp-v1.Qz3z3E`。助手在本地VM只读复核`receive.txt`：118个LF、22656字节，118条全部以`BMS `开头、其他行0，末字节为十进制10（LF），没有尾部残行，与网关成功转发行计数相同。
- 本地文件SHA-256为`d6c2c741ad5c701d9b424e78de5e3de0430e86e661d24b1eec3e60a592dbaad2`。文件保留在仓库外，未将原始运行日志加入Git；本次不重新编译、运行板端程序或扩大历史CTest数量。
- 用户展示的第一条真实TCP文本如下，传输时末尾另有LF；没有附加JSON、二进制长度头或控制回复：

```text
BMS state=1, faults=0x00000000, latched=0x00000000, load=0, clear=0, cell0_mv=3227, temp_est_dC=259, i_ma=0, iv=1, i_src=3, i_age=101, ina=1, bus_mv=0, sample=286605, ctrl=1433031, gap_max=20
```

- 用户另行确认本次新版网关在打开串口时、转发期间和退出后三个阶段心跳均正常，未观察到复位。这是本次人工观察，不是示波器电平证据，也不证明以后每次连接或断连都不会影响MCU。
- 用户随后通过保留的`BMS_PIPE_STATUS`补齐网关退出码0、tee日志保存退出码0，实际板端日志目录为`/home/cat/bms-gateway-v1.9zr5rW/tcp-30s.bK9H8H`，运行日志为其中的`runtime.txt`；退出后`sudo fuser -v /dev/ttyUSB0`没有列出占用进程。这是用户提供的板端证据，不是助手自动SSH重跑或由VM结果推断。30秒仍只记为应用预算，不记为实测墙钟时长。
- 本次真实串口到VM的TCP文本、118条完整行计数对齐、两端退出0、资源占用复查及三阶段人工心跳验收通过。只确认这一有限会话；没有验证异常断连、反复重连、慢客户端、长期运行或每个BMS字段的物理准确性。启动丢弃1段不能宣传为零丢包或无损串口。

Windows Qt对接约束（用户已报告代码完成，实际版本与实物联调待核对／验收）：

- 先核对Windows实际BMS工作目录、AGENTS、Git状态／分支／提交、Qt版本及构建方式，不把历史`D:\qrs\BMS`或旧提交当成当前事实；Linux源码归本仓库`gateway/`，本轮不跨仓库修改源代码。
- 实现仅接收遥测的TCP入口，复用既有BMS行解析、概览、趋势和CSV，不另建字段语义；串口与TCP模式互斥，不破坏原串口的DTR／RTS及关闭行为。TCP模式不打开本地COM，不发送S／T／C、HELLO、PING或初始化业务数据，控制按钮禁用。
- 接收缓存按LF字节分帧，处理半行／多行，正文上限1024字节；超长行丢弃至下个LF再恢复，断开时不把残片当完整报文，新会话清空旧残片。不是一次网络读取对应一条消息；`LISTEN`／`READY`／统计不在TCP数据中，不能等待该握手。
- 使用异步网络事件，不在GUI线程阻塞等待。区分TCP连接成立与收到有效BMS遥测，未知文本交给现有规则忽略，不伪造设备在线／保护状态；第一版不自动重连，不增加协议握手／认证或网关控制功能。
- 本次已结束的`192.168.1.102:18080`只是已验证实验端点，不是当前仍在运行的服务。后续Windows实测前由用户复核板端条件、准备客户端后再手动启动有界单会话网关；不要让Windows Codex自行连接设备或开串口。
- 先给出准确文件位置、修改前后代码和解释，由用户填写，Git仍由用户执行。可先用合成字节流测试分帧／超长恢复／断开清理；主机测试通过不写成Windows Qt与真实BMS已经联动。每个实现步骤结束提供commit标题，文档维护既有分类文件。

Windows Qt首轮真实联调计划与首次失败（2026-11-01）：

- 用户报告Qt已经写好，询问如何连接Linux；尚未提供Windows源码、实际提交、构建／测试输出或界面截图，不能据此认定Qt真实联动通过。本轮VM开始时HEAD为`bac7744 docs(gateway): record first real BMS TCP forwarding`、工作区干净，先前验收文档已由用户提交。
- 先准备Windows Qt为TCP模式，使用板端当前实际IPv4及本次端口18080，自动重连关闭、控制按钮禁用，不打开COM或启动其他客户端。板端用权限已通过的会话复查设备身份、附加组、地址、ModemManager、串口占用与端口空闲；不自动修改驱动、组、服务或防火墙。
- 本次计划手动运行同一独立网关，应用总预算60秒、外层timeout上限75秒，日志放在板端新建`qt-60s.XXXXXX`目录。60秒包含等待Qt连接，不是连接后重新计时；是计划参数，不是已测得运行时长。
- 看到本次进程的LISTEN后只由Qt点击连接，不能先用Test-NetConnection、telnet、浏览器或VM接收脚本探测。网关只处理第一条连接，探测也可能占用唯一会话并触发打开串口，随后网关不再监听；当前不据此实现自动重连。
- 首轮先不主动断开／重连，等待网关有界结束；检查Qt收到有效BMS文本、概览与趋势更新、CSV按现有策略保存、UI不阻塞及最终断开状态，另行观察MCU三阶段心跳。若Qt有原始完整接收行计数可与网关lines核对；未读过Qt实现，不假定曲线点数或重采样CSV行数与lines必定相等。
- 保存Qt前后截图／日志、实际CSV路径、板端运行日志及网关／tee退出码，退出后fuser复查。此次不修改MCU／Qt／分析器源码、不发送业务控制、不拔线、不启用服务或运行历史全量测试。
- 首次反馈为网关LISTEN后`WAIT_TIMEOUT: serial not opened`，四项统计均0；网关退出1、tee退出0。Qt截图网络页填入预期地址／端口但状态仍“未连接”。可确认该会话未接入客户端、没有打开串口；点击发生时是否仍在监听窗口、Qt实际目标／按钮处理／错误日志和Windows网络可达性尚未确认。Qt实物联调未通过，不能因VM既往成功就归因Qt解析或固件。
- 无串口网络对照现已有结果（2026-11-03）：用户截图中Windows PowerShell的目标`192.168.1.102:18080`、源`192.168.1.100`、`TcpTestSucceeded=True`，板端同时`DIAG_ACCEPT ('192.168.1.100', 8095)`并结束。该次网络路径验证通过；8095只是临时源端口，Qt目标仍是18080。尚无Qt连接到诊断器的证据，不能宣称Qt联动通过或完全排除应用级网络限制。
- 下一步重新运行诊断器后只由Qt尝试连接，并读取Qt日志页，不再执行PowerShell或其他TCP客户端；计划接入后保持5秒便于观察，再关闭，全过程仍由70秒timeout限制。该诊断不访问串口、也不发送BMS数据，因此Qt没有遥测或随后断开是预期；每个实例只接入一次，下一次尝试须重新启动。不得将Test-NetConnection探测发给真实网关以免占用其唯一会话；当前不改Qt源码或防火墙，详细现象与边界见问题5.26。

上述为只读阶段的诊断历史。用户随后确认STM32→Linux网关→Windows Qt只读接入成功，并给出Windows基线`fe1f0c5`；未提供该仓库本地核验及本次详细计数，不补写。先前连接超时的具体修复原因仍未知，不将恢复归因于未验证的Qt改动或防火墙调整。当前转入下节控制阶段；分析器旁路抓取、实物断连与常驻服务仍未验证。

### 18.13 BMS网关TCP控制（需求已确认，尚待逐步实现）

边界：Linux现有网关源码保持只读；用户亲自填写C、头文件、CMake及测试，助手只维护既有分类文档并给Git命令，不提交／推送。本阶段不改MCU协议或Windows代码，不自动向真实STM32发送任何控制字节。

已确认的目标契约：

- 保留`bms-gateway SERIAL IPV4 PORT [SECONDS]`，后续新增末尾可选`[--control]`；默认模式输出与行为兼容，禁止UART控制写入。端口0、单客户端单会话、应用总预算、现有DTR／RTS／HUPCL及打开关闭顺序不变，遥测正文及LF分帧不变。
- 只有显式control模式且串口打开并配置成功后，才向该TCP连接发送`GW 2 CONTROL\n`。能力帧只表示该连接可请求控制，不代表MCU执行确认；不得提前发布部分功能。当前源码尚无该开关和帧。
- 请求为`CMD <十进制编号> <STOP|START|CLEAR>`加单LF，编号1..4294967295，合法请求按数值严格递增。STOP／START／CLEAR分别只映射单字节T／S／C，不允许直接转发网络原文或开放P／R／A／D。
- 仅当实际UART单字节write返回1，才安排`ACK <id> UART_ACCEPTED\n`；不代表MCU执行成功，MCU结果仍观察后续BMS状态。ERR原因候选为BAD_REQUEST、BAD_COMMAND、STALE_ID、QUEUE_FULL、CANCELLED、EXPIRED、NOT_READY、SERIAL_WRITE，无法可靠取得编号用0。语法、编号、动作合法的新请求立即消耗编号，之后即使被拒绝也不能重用。
- 请求正文最多64字节，处理半行／多行；拒绝NUL、CR和非法控制字符，超长丢弃至LF再同步。队列计划最多8项，STOP优先于未写普通命令并取消积压START／CLEAR、逐项返回CANCELLED；已被串口驱动接受的字节不可撤销。
- 待写命令按单调时钟1000毫秒期限处理，到期EXPIRED且不补发。EAGAIN／EINTR尚未接受字节时保持有界等待，不能重置期限；write已返回1的字节不重发。已观察TCP断开后清空未写命令，不跨会话重放，不新增隐式UART动作。
- TCP输出同样有界，遥测／能力／ACK／ERR必须以完整帧顺序串行排出，不能部分发送后插入其他帧。UART写前须保证可安排终结回执；对端失联或输出无法继续时安全结束会话并丢弃待写项，不承诺已断链的客户端一定收到ERR，也不因此发送隐式STOP。无认证／TLS，仍限定可信实验网络，MCU本地保护保持独立。

检查与分步路线（全部后续接入均为计划）：

1. 纯请求解析及白名单映射＋单元测试，不接入生产入口。本步候选为`gateway/bms_control_protocol.h/.c`、`tests/unit/test_bms_control_protocol.c`及独立测试目标。输入是去掉LF的完整正文；严格单空格与大写动作，允许编号前导0但按数值处理，不依赖NUL结尾。只有解析成功才更新请求对象；可靠编号通过单独reply_id返回。该纯函数不维护last_id、不处理TCP分帧、不执行I/O。
2. 有界TCP请求分帧与会话编号准入，解析成功后先拒绝旧号，再消耗合法新号；错误格式化与输出仍不能写UART。
3. 纯队列策略：最多8项、STOP取消与优先、1000毫秒EXPIRED、断开清理，使用可注入单调时间的软件测试。
4. 有界非阻塞TCP输出及单字节UART写入状态机，验证短写／背压／EAGAIN／EINTR与回执次序。旧同步行发送器可保留给只读路径，不能把它当成双向事件循环。
5. control模式双向poll会话与CLI开关／能力帧接入；只读路径不改，复用串口安全配置及遥测行整理逻辑。现有串口接收循环只poll串口，不能在遥测回调里顺手收控制，否则静默期控制不能及时处理。
6. PTY＋回环TCP软件验收全部需求后，再整理Windows接口交接与用户发起的实物验证。普通PTY不等于CH340，测试不能通过删除生产DTR／RTS检查来通过；若需模拟，仅隔离在测试专用、限定PTY设备的系统调用替身，不加生产跳过配置开关。

本轮实际结果与未完成项：

- VM为x86_64，现有Debug构建成功，启用可选ML的42项CTest全通过、退出0，12.83秒；不更新ARM64历史测试数量。
- 助手仅通过编译器标准输入在`/tmp/nfa-bms-control-parser.whpl3P`生成候选对象和测试程序，没有写仓库C／头文件／测试或CMake。严格C11编译及普通、ASan／UBSan两版均通过三组检查：语法／uint32边界／白名单、非法字节／非NUL视图／64与65字节边界、参数拒绝／失败输出不变。
- 候选还不是仓库已实现功能，待用户填写后再运行新专项与完整回归。当前仍注册42项，不能声称已通过43项或已控制UART。重复编号、STOP队列、超时、ACK时序及控制CLI等尚未实现／验证。
- 另用本地PTY只读查询调制解调器状态，`TIOCMGET`实际返回errno25／ENOTTY；未访问真实串口。此结果解释为何后续PTY集成测试需要隔离的测试适配，不能用来否定CH340既有控制线验收。
- 完成整个控制软件验收后，Windows交接须给实际实现提交／启动命令、能力帧、请求与ACK／ERR样例、错误和编号规则、已跑测试与剩余实物项目；本轮不把上述目标契约标成可用控制接口。

## 19. ARM Linux开发板计划

用户已经收到野火LubanCat-2N，并开始首次上板环境准备。选择原因主要是：

- 与嵌入式Linux学习目标匹配；
- 网络接口条件更适合采集、管理口、入口/出口或旁路分析实验；
- 文档和社区资料相对友好；
- 相比只适合桌面演示的单网口板，更接近未来网络设备场景。

当前已确认：

- SD卡分区为`/dev/mmcblk1p1`，以`vfat`（FAT32）格式使用；事故期间曾因重复管理出现在`/media/usb0`和`/media/usb1`，当前由`usbmount`唯一挂载到`/media/usb0`；
- 因VFAT不保存Linux原生UID、GID和权限位，直接对挂载内容执行`chown`返回`Operation not permitted`；
- 通过挂载时指定当前用户对应的`uid`、`gid`、`dmask`和`fmask`，已经解决项目目录无法创建的问题；
- 后续尝试把旧UUID写入`/etc/fstab`固化权限时，厂商`resize-all.service`扫描了`/proc/mounts`中的数据卡；其VFAT回退路径重建文件系统但恢复失败，UUID由`CB8E-AD01`变为`9741-D596`，原板端工作树已不在卡中；
- 用户确认卡上没有项目之外的其他重要数据，开发电脑与GitHub仍保存权威仓库；旧SD卡`fstab`条目已删除，不再与`usbmount`重复管理；
- `resize-all.service`已禁用并屏蔽：`is-enabled`返回`masked`，`/etc/systemd/system/resize-all.service`指向`/dev/null`，启动依赖不存在，手工`start`被拒绝；
- `usbmount.conf`中拼错、使用弯引号且重复赋值的`FS_MOUNTOPTIONS`已经修正；重启后挂载选项包含`uid=1000,gid=1000,fmask=0133,dmask=0022,noexec,nodev`，根目录为`cat:cat`和`0755`；
- `fsck.vfat -n /dev/mmcblk1p1`只读检查返回0；普通用户完成临时文件创建、31字节写入、同步、读取和删除，文件呈现`0644`，保留`pcap`、`csv`、`datasets`和`logs`四个数据目录；
- 板端项目源码已通过Git Bundle恢复到eMMC的`/home/cat/workspace/netflow-analyzer`，不再把Git工作树放在VFAT；
- 开发板使用CMake/CTest 3.16.3，不支持3.20才加入的`ctest --test-dir`，测试需要先进入构建目录；
- VFAT构建树中的ELF文件因执行权限被屏蔽而无法启动，现已把构建树改到`/home/cat/build/netflow-analyzer-debug`；
- 板上Debug原生构建和当前18项CTest已经通过；
- 板端Python为3.8.10；验收脚本将Python 3.9内置泛型注解改为`typing.List`和`typing.Tuple`后，`offline_flow_acceptance`与全量测试均通过；
- 板上Release原生构建和确定性6包PCAP端到端验收已经通过；
- 系统位于容量8GB的eMMC，根分区实际为7.0GB ext4，已用5.4GB、可用1.4GB；恢复后的源码约2.4MB，新Debug构建树约3.2MB，不在其中长期累积大型PCAP、模型数据集、日志和大量旧构建；
- 后续一次数天运行使根分区达到7.0GB/100%并影响SSH登录；只读排查发现`/home/cat/.vscode-server/cli/servers`约3.1GB，包含4个完整版本和1个未完成的`.staging`，journal约587.8MB且报告截断文件。删除未完成安装和旧版本后释放约2.6GB，证明VS Code Server多版本是主要可回收占用，长期服务指标日志是次要持续增长项；当前继续使用Remote-SSH，但已关闭客户端VS Code自动更新。journald现已设置100MB占用上限、1GB空闲预留、20MB单文件上限和7天保留期，重启、轮转和清理后占用为28.0MB且`journalctl --verify`全部通过；空的`/etc/hosts`也已恢复本机映射，最终根分区使用率为71%，分析器为`inactive/dead/disabled`；详细证据见`docs/problem_log.md`第5.19节；
- 新构建树位于`/home/cat/build/netflow-analyzer-debug-emmc`，没有复用保存旧SD卡绝对源码路径的CMake缓存；当前18项板端CTest全部通过；
- VMware NAT虚拟机`192.168.78.130`能够`ping`开发板`192.168.1.102`，开发板不能反向`ping`虚拟机；这是NAT与路由边界，不是程序故障；
- Release程序已在开发板物理网卡完成来自虚拟机的ICMP实时抓包和双向流聚合；开发板实际观察到NAT后对端`192.168.1.100`；
- 受控测试中应用处理4个Echo包，后端报告接收6包且两个drop字段为0；多出的2包未进入应用，现有汇总统计无法还原其具体内容；
- 同一确定性6包PCAP已经核对SHA-256，并在x86_64与ARM64原生构建上得到相同标准输出和退出状态；
- 官方Buildroot GCC 9.3、glibc 2.29 sysroot和隔离libpcap overlay已经完成ARM64 Release交叉构建；
- Ubuntu GCC 13、板端完整sysroot和GCC `-B`启动文件前缀也已经完成ARM64 Release交叉构建；
- 两种交叉产物均只要求`GLIBC_2.17`，并在板端通过`ldd`、`--help`和真实ICMP抓包；
- 完整交叉命令见`docs/cross_compilation.md`，详细排查过程见`docs/problem_log.md`第5.1至5.14节；
- 单流Release性能基线已完成：最高约9 Kpps、20万包、每包CPU约6.95微秒、最大RSS约1.64 MiB且零drop，详见`docs/performance_baseline.md`；
- 多流与长稳基线已完成：300流得到256个完整流和44个满载拒绝；128流、约9 Kpps、540万包持续10分钟时每包CPU约6.24微秒、RSS采样恒定且零drop，整机平均空闲90.97%，详见`docs/multiflow_longrun_baseline.md`。
- 流表探测统计版官方SDK交叉产物已完成板端复测：128流、20万包时平均和最大探测长度均为1；300流满载时11928次槽位检查中有11264次来自44个拒绝包的完整扫描，两个场景均零drop。
- 最旧流淘汰版官方SDK交叉产物只要求`GLIBC_2.17`，SHA-256为`02da53505b1f1230a9a04c60af8a618d85b734ed89a7c19176f5d59a7d4a3604`；板端300流得到300个完整包、44次淘汰、256条最终流和零drop。
- 单次满表扫描优化版官方SDK产物SHA-256为`c2dcc119ebbd27d321dbf041683d57a31ac0d22645fe16fea2ce08e873b37036`；板端300流得到300次探测操作、44次淘汰、256条最终流和零drop，证明优化在ARM64真实运行环境中成立。
- TCP状态版ARM64原生Debug构建的18项CTest全部通过；板端`lo`的HTTP/1.0实测处理12个`complete` TCP包，双向各6包并聚合为1条流，最终为`tcp_state=closed`，两个drop字段均为0；后端`received=24`与应用处理12包属于回环捕获的不同统计层级。
- 仓库已提交CMake安装规则、非root systemd单元和默认参数模板；板端已安装提交`740d5ab`对应的官方SDK产物，并通过专用用户、`CAP_NET_RAW`、journal实时日志、真实ICMP和SIGTERM手工启停验收。

不要宣称完整开发板验证已经结束。已经完成源码获取、原生Debug/Release构建、当前18项板端CTest、确定性PCAP跨平台输出对比、两种交叉编译、跨设备ICMP实时抓包、TCP完整关闭状态、单流/多流性能、满载边界、流表探测成本、整机软中断、10分钟长稳，以及非root systemd手工启停、开机自启、异常恢复、连续失败限速和第一批安全加固。SD卡事故后，板端工作树已经通过Git Bundle恢复到eMMC并重新通过18项CTest；仍需完成双向直连网络验证和生产级数小时/数天浸泡测试。当前仓库提供环境检查脚本：

```bash
sh scripts/check_target_env.sh
sh scripts/check_target_env.sh --expect-arm --with-tests
```

首次上板建议：

1. 检查架构、内存、磁盘、网卡和系统版本；
2. 安装或确认GCC、CMake、pkg-config、libpcap开发包；
3. 先在板上原生Release构建；
4. 运行帮助和版本；
5. 拷贝同一个离线PCAP，在PC和板上对比输出；
6. 再验证实时网卡和权限；
7. 原生流程稳定后选择官方SDK或通用GCC加板端sysroot进行交叉构建，并检查ELF ABI；
8. 单流、多流、满载边界、整机CPU/软中断和10分钟长稳数据已经记录；更长浸泡测试按部署需要进行。

板上网络环境需要记录：

- 接口名与链路类型；
- 混杂模式；
- 网卡卸载功能，例如GRO、LRO和checksum offload；
- libpcap版本；
- 权限模型；
- 是否通过交换机镜像、网桥或网关路径获得流量。

## 20. 新会话的推荐开场指令

用户可以在新会话中发送：

```text
请先完整阅读：
/home/zcb/workspace/netflow-analyzer/docs/session_handoff.md

然后只读检查git status、最近提交和CTest基线，不要直接修改C源码。
周期PPS、Mbps、流表占用率、静默报告、五种应用处理结果、流表探测统计、实时容量策略、TCP生命周期，以及普通流CSV和`flow_features_v1`特征CSV已经完成。当前`feature/flow-features`工作树的x86_64 Debug构建29项CTest全部通过；Release和ASan/UBSan仍为新增数据集测试前的20项通过基线。LubanCat-2N最新已验证基线仍为新增特征前的18项，不能宣称板端29项已通过。CTU-13标签与C流匹配审计揭示截断和流边界歧义；IoT-23 v2 Scenario 3-1的156103条真实标签已全部完成身份规范化，其中73944条为零时长，候选索引得到81404个规范化双向流键。定向8包试点已由独立CLI重跑：默认模式2条同标签歧义、3秒分段后4条唯一恶意候选；前200包连续样本在零流表拒绝下，默认模式27条流有4条唯一候选，3秒分段后60条流有58条唯一候选、2条同标签歧义。四状态纯内存分类、借用文本流的C流CSV统计以及CLI端到端成功/失败均有合成测试。公共流CSV解析已抽离，CTU-13与IoT-23回归保持29项通过；唯一候选尚非已验证正确标签，全场景质量评估仍未完成，也没有训练、异常判定或在线推理。下一步需要独立抓包来源并保持容量、数据完整性和逐流边界审计。
跨C流复用审计已接入CLI；前200包3秒模式的58条逐流唯一候选只对应51条不同Zeek记录，5条复用标签的包级边界已抽查。前500包3秒模式处理452个完整包、无流表拒绝，导出174条流，其中160条逐流唯一仅涉及138条不同Zeek记录，5条记录被复用且产生22次额外分配。它们都不是独立留出集或可靠训练样本；下一步扩样仍须先核对流表容量、输出完整性和复用边界。
正式CLI还报告前500包`matches_unique_unreused=133`和`matches_unique_reused=27`，后者计入的是复用标签命中的全部流而不只是22次额外分配。不带sidecar参数时CLI仍仅输出摘要；带齐`--feature-csv`、`--capture-id`和`--review-output`时已为前500包3秒模式生成174条逐行IoT-23审查记录，三态计数14／27／133。内存接口另返回未复用行号，但尚无最终训练准入判断，133也不是可靠训练样本数。
新接口另返回133个未复用唯一流的普通CSV数据行号，合成6行测试精确期望`{5}`，旧摘要CLI输出保持不变。仍需用同次分析的身份和输出顺序验证特征行对应；这一集合本身不是训练集。
仍然由我自己输入C代码，你负责完整说明、测试步骤、Git步骤以及测试通过后的日志文档更新。
```

接手者完成阅读后，应先用一小段话复述：

- 当前HEAD和测试状态；
- 当前实时命令；
- 为什么现有性能数据不支持接入线程流水线，以及下一步为何转向新的功能或部署问题；
- 本轮不会直接替用户修改C源码。

然后先检查流特征、版本化导出、CLI生命周期、测试、文档和Git提交状态，再进入数据集对齐；性能方法见`docs/performance_baseline.md`与`docs/multiflow_longrun_baseline.md`，交叉构建方法见`docs/cross_compilation.md`。

## 21. 本次交接结论

最新两项信号收尾（2026-11-01）：接收程序已由用户提交为`5258927`，源码摘要未变；当前保留3份未提交的验收文档，不覆盖已有工作。板端`sigint-5s.yrjSSf`和`sigterm-5s.uhjKgd`分别有明确INT／TERM发送记录，两次均收尾24行、丢弃1段、尾部0字节、退出0，退出后未查到占用；用户分别确认各次三阶段心跳正常、未观察到复位。SIGINT前ModemManager明确已临时停止，首次149行运行时是否停止仍未知，SIGTERM前的复查输出未提供。完整证据见第18.12节；下一步先确认STM32供电，再安全地测试断连，不直接扩展TCP网关。受控信号路径通过不等于实际键盘Ctrl+C、拔线恢复或systemd部署通过。以下首次硬件接入及10月记录保留为历史快照；源码及Git仍由用户操作。

首次硬件接入（2026-11-01，历史快照）：独立串口源码放入本仓库`gateway/`，用户在鲁班猫原生编译且退出0，摘要一致；`receive-30s.AiBZSI`首次接收退出0、149行，启动丢弃1段、尾部32字节，三阶段心跳正常且未观察到复位。该次ModemManager状态仍未知。首次验收时虚拟机HEAD为`123a28e`且源码尚未提交，后续状态以上条SIGINT记录为准；不改写首次运行的未知项，也不提前声明TCP／Qt／抓包联动完成。

最新全量模型部署收尾（2026-10-12）：用户在鲁班猫2N对既有63775条TCP候选完成每批最多1024条的推理，分类与PC逐条一致；预处理最大绝对差0、logit最大绝对差约`1.91e-6`，按预设组合容差通过、外层退出0。目录`/home/cat/tcp-arm64-full-v1.on838v`，源码快照`f3b2691`；没有提供日志随机后缀，不虚构。整进程17.79秒、峰值RSS约246.2 MiB，固定1024条热态前向中位数140.588微秒；不外推在线吞吐或单流时延。原训练57767／验证6008均在其中，没有重新训练或增加独立留出集，验证仍恒判恶意、正常诊断30.8333%误报限制不变。本轮文档更新前HEAD为`11f8273`，构建及37/37项虚拟机CTest通过（8.25秒）；6份文档待用户提交，README／源码／服务保持不变。下一步可把简历50条改为6.38万条真实候选预测与PC一致，并整理现有日志与面试说明，不自动重跑长稳或接入在线告警。以下50条是首次验收记录。

最新模型部署收尾（2026-10-12）：PC已训练并导出真实TCP模型及训练侧预处理参数，用户在LubanCat-2N完成50条真实样本推理，与PC分类全部一致，logit最大绝对差约`9.54e-7`、退出0；快照来源`f3b2691`，不推断板端主仓库HEAD。本次只证明部署一致性，不改变30.8333%正常误报率或升级为实时告警。整进程16.58秒，其中PyTorch导入14.49秒；50条热态80.79微秒不外推在线性能，外层峰值RSS约202.9 MiB。独立记录见[tcp_arm64_inference.md](tcp_arm64_inference.md)，37项虚拟机回归已复核通过（8.17秒），文档待用户提交。招聘会简历现在可写“PC训练、鲁班猫加载推理，50条真实样本预测与PC一致”；下一步回到简历及讲解，不新增实验或自动运行后台任务。

最新收尾（2026-10-12）：ARM64同一套37项CTest已由用户完整跑通（94.41秒、退出0），超时改动提交为`4a6d8de`；虚拟机已同步且37/37项回归通过（7.53秒）。实际提交保留两段60秒设置，仅属冗余，不再写成已合并。5份验收／招聘会文档由助手更新，尚待用户提交推送，随后板端只需拉取文档，不必重跑测试。接下来准备一页简历、证据页及1分钟项目介绍，暂不新增DNS、AI或多线程功能。此前TCP手工结果的未收到退出状态仍不补写，本次CTest通过不能反推这些证据。

最新验收（2026-10-12）：新版ARM64实测已获得ICMP与TCP两条演示数据链证据；TCP重测10包全部完整处理、1条`closed`流、2行CSV中握手完成1且数值一致、两个drop为0。本次退出码及B／C的PASS仍未贴出，只补核对已有运行，不默认重跑或开展新功能。静默流过期说明已按源码纠正，旧失败仍保留且未确诊SIGPIPE。当前文档更新前HEAD为`1cbb72a`，VM37/37回归通过；旧板端分支、安装程序和服务均不自动修改。

当前优先级与验收（2026-10-11）：以招聘会演示收尾为主，暂停DNS域名解码及AI部署扩展。虚拟机HEAD为`f84cb91`，37/37项回归通过；来自`7bce180`的新版独立ARM64产物已有4个ICMP包、1条双向流及1条数值吻合特征的板端手工证据，两个drop为0，另有10秒静默SIGTERM退出0、完整汇总与CSV1行的收尾对照。首次TCP没有流量与正常收尾证据，用户确认按过Ctrl+C但退出码未保存，不宣称具体故障信号已定位。下步在新目录用直接日志重定向、定时SIGTERM和明确三终端顺序重测TCP设备通信，不自动切换板端源码分支、替换服务或扩展后台任务。详细身份、命令、未收到的板端校验项和实际验收边界见[jobfair_demo.md](jobfair_demo.md)。本次只修改文档，由用户提交和推送。

此前优先级与验收（2026-10-02，历史快照）：HEAD为`492bc28`，DNS固定头与37/37项本地CTest已通过，原计划下一步为独立域名解码；助手已更新固定头验收和TD-044，文档由用户提交。以下LLM对照“下一步”为当时计划，现已暂缓，尚未构建或执行，不继续按旧计划自动推进。DNS仍未接入实时／离线主链，板端测试数量保持历史18项，不能声称已完成DNS内容识别、完整DPI或低误报AI预警。

端侧摘要测试的最新结论：原三例及normal简化prompt对照未通过质量验收；固定事实抽取已正确复述连接数和历史范围，但遗漏失败次数，严格完整性仍未通过。事实抽取25.86秒、生成5.27 token/s、峰值RSS约730.6 MiB、退出0，目录为`llm-normal-facts-v1.kXkUhp`。下一步先建立固定源码的原生x86_64参考构建，再作同GGUF／prompt／参数对照；参考尚未构建，不能提前记录通过，也不能单凭文本同异认定后端正确或补丁错误。保留所有失败日志，不反复针对本例调prompt择优记录。此轮没有建立真实主机窗口、异常检测或在线预警，不能把人工样例结果当作检测准确率。

最新端侧LLM状态（2026-10-02）：官方SDK ARM64 CPU产物已加载SD上的Qwen2.5-0.5B-Instruct Q4_K_M，单轮中文生成退出0；生成约5.26 token/s、整进程14.45秒、峰值RSS约687.4 MiB。回答“网络流是数学模型”未通过本项目概念准确性检查，所以只确认工程部署链，没有确认网络分析或预警能力；LLM与下面的TCP线性模型是两个独立模块。当前VM工作区36项CTest通过，包含未提交的模型保存测试；源代码和Git发布仍由用户控制。完整记录见[local_llm_deployment.md](local_llm_deployment.md)。

最新ML状态（2026-10-01）：从C流特征到IoT-23候选CSV、18维编码、CPU张量、训练侧预处理、线性分类及固定阈值指标的离线链路已经跑通，启用ML解释器后x86_64 Debug为35/35。首次训练的训练侧错误30条，验证侧6008条全预测恶意，与恒判恶意对照相同；完整结果见[tcp_linear_baseline.md](tcp_linear_baseline.md)。未保存模型，未上板推理，未建立在线告警；下一步优先补充独立正常TCP诊断，之后固化模型／预处理保存与加载契约。

随后已在42-1严格1440条候选正常TCP上诊断：996条判正常、444条误报，全部误报为`syn-seen`；其中215条输入与训练恶意向量精确相同，不能仅通过更深网络区分。完整证据见[tcp_normal_diagnostic.md](tcp_normal_diagnostic.md)。正常侧诊断已完成，下一工程步骤转向基线保存与加载一致性，当前模型仍不能宣称低误报或用于上线预警。

以下保留早期阶段记录，其中阶段性测试数量和“尚未实现”表述描述当时状态；当前状态以第1节、第12节和上述最新ML状态为准。

当前项目已经从“离线协议解析练习”发展为一条可实际运行的网络分析主链：

```text
离线PCAP或实时网卡
→ libpcap统一采集
→ 可选BPF过滤、非阻塞poll等待、信号中断与捕获后端统计
→ Ethernet/IPv4/TCP/UDP/ICMP解析
→ 双向五元组
→ FNV-1a开放寻址哈希流表
→ 数据包路径平均/最大线性探测统计
→ 事件时间调度、过期值副本与实时清理
→ 双向包数、字节数和时间统计
→ TCP握手、中途捕获、FIN关闭和RST状态
→ 周期PPS、Mbps、流表占用率和过期数量
→ 含tcp_state的终端流汇总或普通流CSV
→ 生命周期结束时生成flow_features_v1特征CSV
```

实时采集的第一组可控性能力已经完成：

```text
BPF过滤
→ 信号优雅退出
→ 抓包统计
→ 实时流过期输出
→ 静默期周期运行指标
```

应用处理结果分类、满载策略、线性探测可观测性和TCP状态跟踪已经完成。第一版流特征链把完整流记录转换为版本化模型输入；过期、淘汰和最终剩余记录在各自生命周期结束时写出。CTU-13 Scenario 7匹配审计发现截断PCAP和Argus DNS事务边界导致不能可靠一对一连接。第二个试点IoT-23 v2 Scenario 3-1已完成标签审计、精确微秒、双向身份规范化和候选索引：156103条标签对应81404个流键，71026个键有多条候选；8包定向试点通过3秒分段得到4条与前四条Zeek记录一致的流，正式CLI重跑均为唯一恶意候选。前200包连续样本在默认模式生成27条流（4条唯一候选、23条同标签歧义），3秒模式生成60条流（58条唯一候选、2条同标签歧义），184包完整处理、16包不支持、零流表拒绝。公共流CSV解析已从CTU-13审计脚本抽离，两套审计器共同使用；四状态纯内存分类、C流CSV借用流统计和CLI端到端测试均已由合成数据覆盖，x86_64 Debug现有29项CTest全部通过。唯一候选只是当前流键与时间区间下的单个候选，IoT-23全场景质量仍待完成，不能把标签或流键数量当作可训练样本数。ARM64板端仍以之前18项和真实TCP、systemd、性能、存储恢复验收为已验证基线；新增特征尚未上板。训练、评估和在线推理均未实现。`labs/thread_pipeline`仍因现有性能数据未显示单线程瓶颈而不接入正式路径。

IoT-23前200包3秒模式的58条逐流唯一候选只涉及51条不同Zeek记录；5条标签被复用，其中3条各被2条C流指向、2条各被3条指向。包级抽查支持空闲阈值切开Zeek生命周期，且一条长TCP标签延续到样本之外。当前CLI已报告复用但尚不执行全局一对一筛选，扩大样本或构建IoT-23监督元数据前仍须审计这一边界。

同一PCAP前500包扩样保持`flow_rejected=0`：默认模式69条流有4条逐流唯一及65条同标签歧义；3秒模式累计174条流有160条逐流唯一及14条同标签歧义，160条唯一流只指向138条不同Zeek记录，5条标签被复用，产生22次额外分配。`175`行CSV含表头，与171条过期流加3条剩余流相符；48个不支持包已由独立ARP计数交叉核对。完整样本指纹和分项见TD-040。下一步仍要以容量、行数和跨流复用为门槛，不能把逐流唯一数或不同标签数直接当作可信训练样本数。

新增的按C流复用审计字段把160条逐流唯一分成133条未复用和27条复用；`27=5+22`，不能只剔除22条额外命中而保留每组第一条。合成混合用例和独立CLI测试均已覆盖，x86_64 Debug 28项CTest通过。CLI尚未输出逐行准入判断，`FlowSampleMetadata.is_trainable`仍只表达逐流唯一性；IoT-23监督元数据生成与可靠性筛选均未实现。

`audit_flow_csv_with_unreused_rows`现把每条逐流唯一C流的数据行号按标签身份暂存，读完整份CSV后才确定未复用集合；旧`audit_flow_csv`兼容返回摘要。真实前500包3秒模式集合大小为133，范围4～174，已知复用长TCP的数据行37被排除。此函数本身只提供普通流CSV位置，生成sidecar与特征文件预检是后来CLI的职责；大规模审计的额外行号内存仍待测。

离线生命周期集成测试新增两份CSV的同序行校验：一条已过期流和一条最终剩余流的协议、微秒时长、包数及两种字节总量互相吻合。另对IoT-23前500包两次同运行成对输出只读核对，默认模式69对、3秒模式174对，五项均无差异。此结果只支持这些同次离线输出的行号连接；特征不含流键，单靠相同统计值不能证明任意两条流的身份，更不能用来连接不同运行的文件。x86_64 Debug完整28项CTest通过；IoT-23逐行sidecar后来已在真实前500包上生成，但训练准入仍未实现。

IoT-23逐行审查函数已把“逐流唯一但标签被另一条C流复用”与“唯一且未复用的候选”分开。六行合成测试和前500包真实样本分别得到`3/2/1`与`14/27/133`（非唯一／唯一但复用／唯一未复用候选）；后者三类相加为174。函数要求未复用集合来自同一份流CSV审计，不更改通用`FlowSampleMetadata.is_trainable`或`flow_sample_metadata_v1`语义；真实前500包文件已写出并核对行数与三态计数，最终训练准入仍待验证。

独立IoT-23审查文件现固定为`iot23_flow_review_v1`的16列格式：与现有通用元数据一样保留特征行号、稳定样本ID和可追溯身份，但将末列改为三态`review_status`，不输出可能误导IoT-23复用流的`is_trainable`。表头、单条记录、借用流批量写出以及CLI文件级预检和正常失败清理均已由合成测试覆盖；真实前500包sidecar有175个物理行、174条数据，SHA-256为`4cfd7af6213d2d5b1a00217e4fa9e17a43d359a314806a44f501191ffdf81baa`，完全原子提交尚未实现。x86_64 Debug 28项CTest通过。

审查模块现有`validate_iot23_review_record(metadata, review_status)`，在写行之前重新调用通用元数据构造器校验身份、候选数和标签组，再检查`unique`不能对应`not_unique`、非唯一不能对应任一唯一审查状态。函数只借用不可变输入，返回新的已验证元数据；两种唯一审查状态之间的真假仍由完整标签审计与未复用行号集合决定。`write_iot23_review_csv_record`复用这层校验，然后写出完整的16列CSV数据行。`write_iot23_review_csv`先审计完整输入，再回到表头逐行分类并调用单行接口；独立`validate_flow_feature_alignment`拒绝两份CSV表头、版本、行数及核心统计字段不一致。CLI现在成组接受特征CSV、抓包ID和审查输出路径，预检成功后独占创建文件，并对正常失败进行清理；合成端到端测试及本机x86_64 Debug 28项CTest通过，真实前500包文件也已通过174行与14／27／133三态检查。边界纯函数与新增的只读`--boundary-summary`将133条未复用候选分为132条精确一致和1条C流包含于标签（特征行173，终点恰在样本前缀边界）；下一步制定防泄漏的数据切分准则，不能仅凭时间一致或`unique_unreused_candidate`直接进入训练。文件级完全原子性和可靠监督训练集仍未完成。
