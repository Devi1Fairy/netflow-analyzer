# Netflow Analyzer会话交接文档

最后更新：2026-11-05（UTC+08:00）

本文用于把当前项目状态、学习背景、协作方式、代码架构、测试、Git历史、已知边界和下一步计划完整交接给新的Codex会话。接手者应先完整阅读本文，再执行只读检查，不要根据标题直接开始大范围修改。

当前任务（2026-11-05）：用户已提交`c0f62a3 add bounded telemetry queue for async storage`，文档更新前工作区干净且与远程同步；VM构建及50/50项Debug CTest复核通过（13.34秒）。下一步提供独立SQLite消费者线程及临时数据库软件测试，由用户录入；候选代码通过仓库外严格编译、ASan／UBSan和重复测试，仓库第51项仍待集成验收，详见18.19。现有网关、TCP控制、串口配置均未改，不启动硬件；Qt离线持续采集、实时入库及ARM64新组件尚未验证。118条历史回放见18.17。

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

- 当前VM复核（2026-11-05，存储线程准备）：分支`feature/flow-features`，HEAD为`c0f62a3`，与远程同步且文档更新前工作区干净。构建无待编译目标，启用SQLite及可选ML的50/50项Debug CTest通过（13.34秒、退出0），新增队列已在实际仓库验收。独立SQLite线程仍为待用户录入候选，不把仓库外测试登记成51项或硬件实时入库。

- 当前VM复核（2026-11-04，生命周期解耦准备）：分支`feature/flow-features`，HEAD为`4607f1f`，与远程同步且文档更新前工作区干净。构建无待编译目标，启用SQLite及可选ML的49/49项Debug CTest通过（13.63秒、退出0）。当前代码仍为Qt接入后打开串口、单会话结束即退出；新队列、存储线程及独立于Qt的运行模式均未集成。

- 当前VM复核（2026-11-04，导入阶段）：分支`feature/flow-features`，父HEAD为`87ccb36`；CMakeLists.txt、gateway/bms_store.h、gateway/bms_import.c、tests/integration/test_bms_import.py及本交接文档为本步待提交文件，均属当前工作，不覆盖或回退。构建无待编译目标，49/49项Debug CTest通过（14.31秒、退出0），其中bms_import_tests为0.15秒。用户实际历史日志数据库及现存stdout／stderr已只读核对，完整证据与原导入退出码未回传的边界见18.17。

- 当前VM复核（2026-11-04，SQLite阶段）：分支`feature/flow-features`，HEAD为`87ccb36`，文档更新前工作区干净且与远程同步；构建无待编译目标，开启SQLite与可选ML的48/48项Debug CTest通过（15.06秒）。用户已安装libsqlite3-dev和sqlite3，实际包版本均为3.45.1-1ubuntu2.8。日志导入CLI及第49项测试尚待用户录入，不把仓库外候选验证当作已集成。

- 当前VM复核（2026-11-04，解析阶段）：分支`feature/flow-features`，HEAD为`ca22417`，文档更新前工作区干净；构建无待编译目标，47/47项Debug CTest通过（13.00秒），包含新增bms_telemetry_tests。VM有SQLite运行库3.45.1，但未安装libsqlite3-dev和sqlite3命令行；仅为候选代码验证在仓库外下载／解包匹配的开发包，没有安装系统包。SQLite存储源码及CMake注册仍待用户填写和验收。

- 当前VM复核（2026-11-04）：分支`feature/flow-features`，HEAD为`00aafac docs(integration): verify BMS TCP capture and CSV replay`，本轮文档更新前工作区干净；现有构建无待编译目标，启用可选ML的46/46项CTest通过（16.75秒）。这是新增解析模块前的软件基线，不是新的板端结果。后续历史条目中的测试数、未提交状态及“下一步”按各自日期解释。

- 最新控制阶段（2026-11-03）：Linux源码基线为`cfaf3bf214446818ddeeeab4f72e7f948a7b8f99`，此前VM启用可选ML的46/46项Debug CTest通过（12.36秒），用户在ARM64通过原生编译、旧CLI四组及新PTY五组软件检查。现收到真实Qt控制会话的板端输出：日志目录`/home/cat/bms-control-v1.X3nWaz/qt-stop-120s.EypQ7U`，监听`192.168.1.102:18080`、打开`/dev/ttyUSB0`并显示control enabled，收尾`lines=541 discarded=1 tail_bytes=0 discarding=0`，网关／timeout及tee退出均0。用户另确认启动、停止、清除故障均现场验证成功；逐条CMD／ACK、前后BMS遥测、Windows控制提交、运行后三阶段心跳及fuser尚未补充，不能由退出0或541行推断MCU严格执行ACK、零丢失、实际满120秒或故障注入保护通过。实物运行与用户三动作确认已记录，详细证据分层见第18.13.4节；下一步先补归档及结束状态，再安排独立netflow-analyzer旁路抓取。本轮只维护既有两份文档，不改源码、不代执行Git或新增硬件动作。

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

### 18.13 BMS网关TCP控制（实物会话正常退出，用户确认三项人工控制通过）

边界：Linux默认模式保持只读，用户已填写并提交显式TCP控制入口；助手只维护既有分类文档并给Git命令，不提交／推送。本阶段不改MCU协议或Windows代码，不自动向真实STM32发送任何控制字节。Windows交接是供用户转交的后续任务，不代表助手已修改或核验Windows仓库。

已确认的目标契约：

- 保留`bms-gateway SERIAL IPV4 PORT [SECONDS]`，已新增末尾可选`[--control]`；默认模式输出与行为兼容，禁止UART控制写入。端口0、单客户端单会话、应用总预算、现有DTR／RTS／HUPCL及打开关闭顺序不变，遥测正文及LF分帧不变。
- 只有显式control模式且串口打开并配置成功后，才向该TCP连接发送`GW 2 CONTROL\n`。能力帧只表示该连接可请求控制，不代表MCU执行确认；开关、能力与完整软件链路已在`cfaf3bf`接入，已通过ARM64软件检查及一次真实CH340／Qt控制会话运行。用户确认三项人工控制通过，逐条能力／请求／回执原文仍待归档，详见第18.13.4节。
- 请求为`CMD <十进制编号> <STOP|START|CLEAR>`加单LF，编号1..4294967295，合法请求按数值严格递增。STOP／START／CLEAR分别只映射单字节T／S／C，不允许直接转发网络原文或开放P／R／A／D。
- 仅当实际UART单字节write返回1，才安排`ACK <id> UART_ACCEPTED\n`；不代表MCU执行成功，MCU结果仍观察后续BMS状态。ERR支持BAD_REQUEST、BAD_COMMAND、STALE_ID、QUEUE_FULL、CANCELLED、EXPIRED、NOT_READY、SERIAL_WRITE，无法可靠取得编号用0；NOT_READY为核心状态，当前CLI配置失败直接结束且不发送能力。语法、编号、动作合法的新请求立即消耗编号，之后即使被拒绝也不能重用。
- 请求正文最多64字节，处理半行／多行；拒绝NUL、CR和非法控制字符，超长丢弃至LF再同步。队列实际最多8项，STOP优先于未写普通命令并取消积压START／CLEAR、逐项返回CANCELLED；已被串口驱动接受的字节不可撤销。
- 待写命令按单调时钟1000毫秒期限处理，到期EXPIRED且不补发。EAGAIN／EINTR尚未接受字节时保持有界等待，不能重置期限；write已返回1的字节不重发。已观察TCP断开后清空未写命令，不跨会话重放，不新增隐式UART动作。
- TCP输出同样有界，遥测／能力／ACK／ERR必须以完整帧顺序串行排出，不能部分发送后插入其他帧。UART写前须保证可安排终结回执；对端失联或输出无法继续时安全结束会话并丢弃待写项，不承诺已断链的客户端一定收到ERR，也不因此发送隐式STOP。无认证／TLS，仍限定可信实验网络，MCU本地保护保持独立。

已实现模块与软件验证（源码由用户填写）：

1. 纯请求解析及白名单映射＋单元测试已提交为`84d2681`，不接入生产入口。文件为`gateway/bms_control_protocol.h/.c`、`tests/unit/test_bms_control_protocol.c`及独立测试目标。输入是去掉LF的完整正文；严格单空格与大写动作，允许编号前导0但按数值处理，不依赖NUL结尾。只有解析成功才更新请求对象；可靠编号通过单独reply_id返回。该纯函数不维护last_id、不处理TCP分帧、不执行I/O。
2. `gateway/bms_control_session.h/.c`及单元测试已由用户提交为`2354fe9`：半行／多行与64字节限制、会话编号准入、最多8项待写队列、STOP取消与优先、1000毫秒EXPIRED、断开清理。使用调用者注入的非负单调毫秒，不实际等待；合法新号在NOT_READY／QUEUE_FULL拒绝前消耗。peek只复制队首，written只供I/O层在实际write返回1后出队，本模块不执行I/O或产生ACK。serial_ready是调用者提供的逻辑状态，不是本模块验证串口就绪。
3. `gateway/bms_control_io.h/.c`与测试已由用户提交为`774b195`：4096字节缓存逐行复制正文并追加LF、不附NUL；部分send只推进后缀，send每次非阻塞尝试一次，EAGAIN／EINTR保留缓存并返回事件循环。UART每次最多write一个T／S／C，要求已配置O_NONBLOCK并借用fd；读取CLOCK_MONOTONIC、经peek清理过期项，并在写前预留64字节回执容量。write返回1后才出队并安排ACK UART_ACCEPTED；EAGAIN／EINTR不出队、不ACK、不刷新期限；实际失败或零字节write尽力安排ERR SERIAL_WRITE并清空待写、结束会话，不隐式STOP。错误回调只复制到有界输出缓存，无法容纳时ENOBUFS终止。错误已入缓存不等于已送达，禁止失败后重放已写字节。旧同步行发送器仍保留给只读路径。
4. `gateway/bms_control_run.h/.c`及CLI接入已提交为`cfaf3bf`：保留默认只读调用路径，末尾识别`--control`，可省略SECONDS，端口0与原总预算保持不变；真正配置成功且仍有预算后才调用control入口，将能力帧先入发送缓存。增量framer公开原有1024字节行整理状态和feed／stats接口，仅以类型别名复用旧实现，不改控制线配置或原行整理算法。主循环单线程poll TCP与UART，有限批次读请求、统一有界输出、按命令期限缩短等待；静默UART不阻塞控制。观察到TCP结束优先清理待写，UART写前再PEEK连接／新输入；已观察到的结束之后不再写，不声称消除尚未观察到的断链竞态。退出清空队列、不隐式STOP；错误排出最多100毫秒且不超过原总期限。control的lines仅表示遥测入发送缓存，不等于Qt已处理。
5. `tests/support/bms_pty_ioctl.c`与`tests/integration/test_bms_gateway_control.py`已提交为`cfaf3bf`，构建单独`bms-gateway-pty-test`，只在该目标包装open／ioctl。open只允许`/dev/pts/数字`，不允许物理串口路径；真实termios和排他配置仍执行，仅模拟PTY缺失的DTR／RTS ioctl，并用测试专用环境变量注入配置失败。正式网关不链接这些替身、不加生产跳过配置开关。测试用PTY＋127.0.0.1验证，不能代替真实CH340控制线、心跳或MCU动作确认；ARM64软件与后续实物运行分别记录，不混合成自动化硬件测试通过。

本轮实际结果与未完成项：

- VM为x86_64，`cfaf3bf214446818ddeeeab4f72e7f948a7b8f99`工作区在本轮文档维护前干净，Debug构建成功，启用可选ML的46项CTest全通过、退出0，12.36秒；不更新ARM64历史测试数量。入口已在仓库调用控制模块。VM CMake正式程序SHA-256为`d5a50585c6f5a29d89d662f63433b9abdfdea88a84e34591a9f77d0a70e84ddc`，不是ARM64产物摘要。
- 入口候选仅通过编译器标准输入在`/tmp/nfa-bms-control-runtime.z2JXqT`生成对象和程序，没有写仓库C／头文件／测试／Python或CMake。严格C11、Wall／Wextra／Wpedantic／Werror编译成功，普通及ASan／UBSan两版均通过旧CLI四组和新增联调五组：可选SECONDS／control参数／无人接入预算；默认只读遥测及不写控制；能力首帧／静默UART控制／半行多行／重复编号／CR与超长恢复／遥测保持；配置失败无能力；control总预算无隐式命令。新联调还在暂停测试进程后注入START和FIN，恢复后未写该待处理字节。测试仅PTY和回环TCP；不能据此声称真实STM32执行、板端控制或Qt控制已通过。
- 另复用旧串口白盒用例验证候选别名及共享framer，片段、CRLF、多行、容量、控制字符、回调失败及管道HUP等检查通过。正式候选不链接替身时，普通PTY仍因控制线检查失败且未发送能力，说明没有放松生产配置；该检查未接触真实串口。
- 入口及PTY测试现已填写并通过仓库内回归。另以拟给板端的八个生产C文件和`BMS_SERIAL_LIBRARY=1`直接严格C11编译，在VM隔离目录`/tmp/nfa-bms-control-build-check.ZMU0m7`生成正式及PTY程序，旧CLI四组和新PTY五组均通过，证明直接编译命令在VM可用；该次为VM结果。用户随后提供ARM64原生编译及同两份脚本全部PASS的板端输出，详见第18.13.2节；真实控制会话日志及用户三动作确认见第18.13.4节。队列、期限及背压已有核心专项验证，不夸大成所有真实负载及竞态均覆盖，也不以软件替身或READY日志补写实物心跳和MCU状态。
- 另用本地PTY只读查询调制解调器状态，`TIOCMGET`实际返回errno25／ENOTTY；未访问真实串口。此结果解释为何后续PTY集成测试需要隔离的测试适配，不能用来否定CH340既有控制线验收。
- 当前可交接已实现的软件协议，明确源码提交、启动参数、能力、ACK／ERR、编号与限制；新版ARM64软件验证和一次面向真实串口及Qt的控制会话运行已完成，用户确认启动／停止／清除故障通过。本次进程已退出，不宣称端口仍在监听；Windows具体控制提交与逐条协议／状态证据仍待归档。源码、Git及硬件操作由用户执行。

### 18.13.1 Windows Qt控制接口交接（首轮实物联调前快照）

以下保留首轮实物联调前的可转交接口说明，协议约定不变，但其中“尚未实物联调”的状态属于当时快照；当前实际运行及用户三动作确认见第18.13.4节。不是助手已修改Windows代码或核验其仓库的记录。

```text
Windows Qt TCP控制交接（Linux软件已验收，实物控制尚未验收）

请先检查Windows BMS仓库的AGENTS、Git状态及现有TCP接收代码，保留已有修改。
Windows只读基线fe1f0c5仅作历史参考；Linux基线为netflow-analyzer的cfaf3bf214446818ddeeeab4f72e7f948a7b8f99。
Linux VM的46项CTest已经通过，包括纯协议、队列、I/O和PTY＋127.0.0.1联调。
鲁班猫现已在/home/cat/bms-control-v1.X3nWaz原生编译并通过四组CLI及五组PTY软件检查；未打开真实串口，不代表MCU已执行控制，也不是板端46项全量CTest结果。

1. Linux是TCP服务端，Qt主动连接；IPv4和端口由启动参数指定，没有固定默认值。
   预期实物端点仍为192.168.1.102:18080，必须在实物联调前复核。
   CLI：bms-gateway SERIAL IPV4 PORT [SECONDS] [--control]。
   默认只读；--control必须位于末尾。时长默认30秒、范围1..300，等待连接也计入总预算；单客户端单会话，无自动重连。
   新版已完成板端软件检查，但尚未启动面向真实串口及Qt的控制会话；不要假设端口正在监听。

2. 所有TCP消息均为文本正文＋单个LF，不附NUL；控制请求不接受CRLF。
   Qt必须使用有界接收缓存处理半包、多行，并将BMS、GW、ACK、ERR分别分发。
   BMS遥测正文保持原样、上限1024字节，继续复用已有解析、趋势和CSV。

3. 只有--control启用且串口实际打开、配置成功后，服务器才首先发送：
   GW 2 CONTROL
   不需要也不支持HELLO/PING协商。每个新连接先清除控制能力标志，收到本连接的精确能力行后才允许人工控制。
   旧服务器或只读连接没有能力行，按钮必须禁用；断开立即清除能力。

4. 请求示例（每行末尾另加单个LF）：
   CMD 1 STOP
   CMD 2 START
   CMD 3 CLEAR
   严格单空格、大写动作，正文最多64字节。
   编号1..4294967295，同一连接按数值严格递增。
   Qt每次提交分配新编号，不能因ERR或等待超时而复用旧号；达到上限不能回绕。
   同一会话语法、编号、动作合法的新请求，即使被QUEUE_FULL/NOT_READY拒绝也消耗编号。
   STOP/START/CLEAR只映射UART单字节T/S/C，不开放其他调试命令。

5. 成功回执：
   ACK 1 UART_ACCEPTED
   只表示串口驱动接受了字节，不是MCU执行完成。
   界面可显示“已提交串口，等待状态确认”，不能直接将负载状态改成已启动/停止。
   执行效果仍从后续BMS遥测及原MCU状态逻辑判断。

6. 错误格式：
   ERR <编号> <原因>
   原因：BAD_REQUEST、BAD_COMMAND、STALE_ID、QUEUE_FULL、CANCELLED、EXPIRED、NOT_READY、SERIAL_WRITE。
   无法可靠取得编号时为0。NOT_READY是核心支持的状态；当前CLI配置失败会直接结束连接，不发送能力帧。
   待写队列最多8项；STOP取消尚未写入的START/CLEAR，并逐项返回CANCELLED，已接受的字节不能撤销。
   待写命令1000ms到期后EXPIRED、不补发。回执可能与其他遥测交错，必须按编号关联，不能按到达顺序猜测。
   输出缓存耗尽或连接故障可能直接结束连接，不能保证每次拒绝都能送达ERR。

7. 回执超时只说明结果未知，不能自动重发，无论是否换新编号。
   断开后不跨连接重放旧请求；新连接重新获取能力，不能沿用上一连接的授权。
   不在启动、连接、重连或退出时自动发送STOP/START/CLEAR。
   MCU采样、保护和MOS控制仍由STM32本地执行；网络断开不代表负载已经关闭。
   无认证/TLS，仅限可信实验局域网。

本阶段仅在Windows实现协议分发、能力门控、人工命令发送和状态展示，并进行模拟服务端测试。
不要修改MCU协议，不自动对真实硬件发控制命令。真实联调另行安排，先验证STOP和心跳，不自动START。
```

### 18.13.2 新版ARM64隔离部署与软件验收（2026-11-03用户提供板端结果）

- 固定源码提交为`cfaf3bf214446818ddeeeab4f72e7f948a7b8f99`，由用户用Git archive导出gateway目录、两份网关集成测试及PTY替身；不打包Git、虚拟环境、模型、历史构建或数据集。
- 由用户交互SSH到历史已确认的`cat@192.168.1.102`，核对主机与架构并创建新的`/home/cat/bms-control-v1.XXXXXX`隔离目录；不得复用旧`bms-gateway-v1.9zr5rW`或假定板端主仓库已同步。密码仅在用户终端输入，若地址／认证不符先停止确认，不改SSH设置。
- 传输前后比较源码包SHA-256，确认一致后才在该新目录解包。使用系统cc原生编译8个gateway生产C文件，宏`BMS_SERIAL_LIBRARY=1`仅排除原串口main；正式程序不链接任何测试包装、不安装、不覆盖旧程序或启用服务。
- 两端Shell变量不共享：用户需把新隔离目录分别赋给VM与板端的`BMS_CONTROL_STAGE`，不使用旧`BMS_GATEWAY_DIR`、空变量或子串展开当默认值。VM打印源码包实际路径及`BMS_EXPECTED_SHA`赋值行，用户复制到板端；校验不一致不得解包。本次板端实际目录已由用户确认为`/home/cat/bms-control-v1.X3nWaz`，源码包校验显示通过；用户未贴出包的64位摘要原文，不用程序摘要代替源码包摘要。
- 另编译带open／ioctl包装的`bms-gateway-pty-test`，仅运行现有CLI和PTY两份集成脚本，分别用20秒和30秒外层超时并保存日志。测试只用不存在的路径、PTY及127.0.0.1，不打开CH340、不发送真实控制、不重跑全量历史／ML测试。

用户实际回传的板端证据：

- 主机`lubancat`，架构`aarch64`，显示“源码包SHA-256校验通过”。按上述固定提交导出／隔离编译流程验收，不以此推断板端主仓库HEAD；系统发行版与内核本次未重新采集，网卡及串口预检结果见下文。
- 正式产物`bms-gateway`的file结果为ARM aarch64、动态链接、解释器`/lib/ld-linux-aarch64.so.1`、含debug_info且未strip；BuildID为`e604df17e5961ef20b1d1d2c609e0c4cec343958`。help已实际显示`SERIAL IPV4 PORT [SECONDS] [--control]`、端口0..65535和时长1..300、默认30秒。

| 板端产物 | SHA-256 |
| --- | --- |
| `bms-gateway` | `1aa74d69e8be36f91bfaf4b89652f9308249c56632f99de96d3330fb92ff7fde` |
| `bms-gateway-pty-test` | `9d8ce6abc980b6ff3ab1fb7e11b708f83005b8d832132f02540b006d0a67e230` |

- 原CLI四组均PASS：help及参数验证；无人接入不打开串口；等待时SIGINT／SIGTERM退出；接入后不存在的设备安全失败。
- 新control脚本五组均PASS：可选时长及参数／无人接入预算；默认只读遥测兼容且无UART控制；能力首帧、静默UART控制、分帧及遥测；配置失败不发布能力；总预算及信号不产生隐式命令。测试仍是PTY及本机TCP，不是真实CH340。
- 用户回传最终成功标记“ARM64软件检查通过，未打开真实串口”，目录为`/home/cat/bms-control-v1.X3nWaz`。约定脚本在该目录保存`cli-check.txt`与`control-pty-check.txt`；本次仅收到终端PASS输出，未另行读取板端日志文件。外层20秒／30秒是测试超时上限，不是实测耗时；未单独贴出两份脚本退出码，不补写数值。
- 上述软件验收阶段只确认原生编译与两份专项脚本通过，不能改写为ARM64已通过当前46项CTest。正式／测试程序分离；该阶段未安装、覆盖旧程序、启用常驻服务或启动真实串口控制会话。后续真实运行及用户人工控制结果另见第18.13.4节，不能回填为软件检查已访问真实串口。
- 随后用户补充只读设备预检：实际节点`/dev/ttyUSB0`，字符设备`188,0`，属主／组`root:dialout`、权限`crw-rw----`；udev为`1a86:7523`、驱动`ch341`，当前会话读写权限检查通过且未打开设备。
- 用户进一步回传板端网络及占用预检：`lo=127.0.0.1/8`，`eth0 UP=192.168.1.102/24`；ModemManager为`loaded/inactive/dead`；`ss -ltnp 'sport = :18080'`仅有表头、未列监听，`sudo fuser -v /dev/ttyUSB0`未列占用进程。该时点预检通过，不保证稍后设备／端口仍无人占用，不据此修改服务或结束其他进程。用户随后确认STM32待机、负载输出关闭、心跳正常，以及Qt已实现控制能力门控；这是用户现场／实现声明，助手未核验Windows源码或提交。该次预检时实物尚未运行，后续运行与人工控制结果见第18.13.4节。

实物前预检与首轮操作边界（执行前记录）：

- 板端网卡与地址、实际节点及USB驱动、当前会话读写权限、ModemManager与设备／18080占用的查询现已收到用户结果；实际打开前若换会话、插拔或间隔较久须重新核对。查询节点和端口不等于打开串口／连接TCP；不运行stty、串口工具或任何TCP连接探测，不因历史节点为ttyUSB0而跳过核对。
- STM32待机、负载关闭、心跳正常及Qt能力门控已由用户确认；人工STOP的实际请求／回执仍须以下有界联调验证，不能从历史只读成功推断控制已验收。若Qt因BMS待机禁用停止按钮，先保留界面与日志，不以START制造测试条件。
- 条件满足后再由用户启动有界真实会话，仅让Qt占用唯一客户端连接。首轮只人工STOP并检查打开／转发／退出后三阶段心跳、UART_ACCEPTED与后续遥测，不自动START／CLEAR，不把ACK当作MCU动作完成。USB TO UART兼供电，继续不做插拔／断电实验。

### 18.13.3 首次Qt实物STOP联调方案（执行前方案，实际结果见18.13.4）

本节保留交给用户执行时的方案，不以方案文字代替实际验收；后续回传结果见第18.13.4节。正式产物、串口、网卡、权限、服务及占用的证据见第18.13.2节；执行前用户已确认BMS待机、负载输出关闭、心跳正常及Qt能力门控已实现，当时尚无Windows控制版本提交或本次协议日志。

板端运行命令如下，使用正式`bms-gateway`，禁止将PTY测试程序用于实物。子Shell中的变量只在该块内有效，实际日志目录会在启动前及退出后打印，不要求用户在另一个终端复用未赋值变量。

```bash
(
    set -euo pipefail

    BMS_CONTROL_STAGE=/home/cat/bms-control-v1.X3nWaz
    BMS_SERIAL_DEVICE=/dev/ttyUSB0

    test "$(uname -m)" = aarch64
    test -x "$BMS_CONTROL_STAGE/bms-gateway"
    test -c "$BMS_SERIAL_DEVICE"
    test -r "$BMS_SERIAL_DEVICE"
    test -w "$BMS_SERIAL_DEVICE"

    BMS_PROGRAM_SHA="$(sha256sum "$BMS_CONTROL_STAGE/bms-gateway")"
    test "${BMS_PROGRAM_SHA%% *}" = \
        1aa74d69e8be36f91bfaf4b89652f9308249c56632f99de96d3330fb92ff7fde

    BMS_QT_RUN_DIR="$(mktemp -d "$BMS_CONTROL_STAGE/qt-stop-120s.XXXXXX")"
    printf '日志目录：%s\n' "$BMS_QT_RUN_DIR"

    set +e
    timeout --signal=TERM --kill-after=5s 125s \
        "$BMS_CONTROL_STAGE/bms-gateway" \
        "$BMS_SERIAL_DEVICE" 192.168.1.102 18080 120 --control \
        2>&1 | tee "$BMS_QT_RUN_DIR/runtime.txt"
    BMS_QT_PIPE_STATUS=("${PIPESTATUS[@]}")

    printf '网关/timeout退出码：%s\n日志保存退出码：%s\n日志目录：%s\n' \
        "${BMS_QT_PIPE_STATUS[0]}" "${BMS_QT_PIPE_STATUS[1]}" \
        "$BMS_QT_RUN_DIR" |
        tee "$BMS_QT_RUN_DIR/exit-status.txt"
)
```

- 应用总预算120秒，包含等待Qt连接；外层125秒timeout及其5秒强制结束宽限只用于故障兜底，不是把连接后的预算再加120秒。若兜底触发，通常返回124，不登记为正常验收。`PIPESTATUS`在管道后立即保存，避免后续printf覆盖真实状态；set +e仅作用于子Shell，以便失败后仍打印结果。普通用户运行，不新增sudo权限。
- `runtime.txt`保存并实时显示网关运行输出（含stderr），`exit-status.txt`保存两个退出码及实际目录；两者不会自动包含Qt收到的能力、ACK或BMS正文。能力及回执走TCP，必须另存Qt日志，不能从Linux的READY或退出0推断ACK送达。
- 看到`LISTEN 192.168.1.102:18080`后，只由Windows Qt连接一次；不运行Test-NetConnection、nc、Python探测或其他虚拟客户端。接入后板端才应显示OPEN_SERIAL及control enabled；Qt应收到本连接的精确能力帧并显示持续BMS遥测。
- 先观察数秒遥测及心跳，再由用户只点击一次停止按钮，记录实际`CMD <id> STOP`及匹配编号的`ACK <id> UART_ACCEPTED`。本连接首次编号若为1，则为CMD 1 STOP／ACK 1 UART_ACCEPTED；以Qt实际编号为准。无能力、出现ERR、回执超时或停止按钮因待机禁用时，不自动重试、不发送START／CLEAR；保留日志后结束本轮定位。
- 继续观察后续BMS行和心跳，Qt保持连接直到应用总预算到期，避免自动重连；正常期望网关和tee退出均0。退出后用户执行`sudo fuser -v /dev/ttyUSB0`并检查心跳。Ctrl+C可提前结束网关，但不是MCU急停，不产生隐式控制。
- 原计划只验收串口驱动接受STOP、遥测持续、用户观察的打开／接收／退出后三阶段心跳。起始负载已关闭，后续load=0是状态一致证据，不足以证明运行态停机、MCU处理成功、MOS时序或保护动作；若没有MCU明确命令处理证据，应保留该边界。后续用户另确认实际人工验证了START／STOP／CLEAR，结果分层记录于第18.13.4节，不倒改原计划为助手自动执行三动作；不安排插拔／断电实验。

### 18.13.4 真实Qt控制会话验收（2026-11-03板端输出与用户现场确认）

用户回传“完全成功，并且验证了启动停止和清理故障”，同时提供如下板端终端输出；助手本轮未远程启动程序、发送命令、打开真实串口或读取板端文件。现场确认与终端可直接核对的证据分别记录。

| 项目 | 本次实际输出 |
| --- | --- |
| 日志目录 | `/home/cat/bms-control-v1.X3nWaz/qt-stop-120s.EypQ7U` |
| TCP监听 | `LISTEN 192.168.1.102:18080` |
| 实际串口 | `OPEN_SERIAL /dev/ttyUSB0` |
| 配置日志 | `READY: 115200/8N1 DTR=0 RTS=0 HUPCL=0; control enabled` |
| 收尾统计 | `lines=541 discarded=1 tail_bytes=0 discarding=0` |
| 网关／timeout退出码 | `0` |
| 日志保存退出码 | `0` |

- 本次真实连接、串口配置及control事件循环已实际运行并正常退出，不再属于“新版未上板”。541是控制模式下完整串口文本行成功入TCP发送缓存的计数，不是Qt已确认接收541行，也不是命令或ACK数量。启动行整理器会先丢弃片段至LF；`discarded=1`与既有同步策略相容，但没有本次原始串口字节可还原被丢弃内容，不宣称UART或TCP零丢失。
- 用户明确确认Qt侧启动、停止、清除故障均验证成功，可作为人工实物联调通过的现场记录。原指导只要求首轮STOP，额外两动作是用户报告的实际人工验证，不能写成助手自动化脚本对真实硬件发了三条命令，也不因此授权后续自动启动或清除故障。
- 尚未贴出本连接的GW能力原文、各CMD编号、对应ACK／ERR、动作前后BMS行或MCU命令处理日志；不编造编号、操作顺序、重复次数、状态变化或严格执行ACK。未说明CLEAR时是否有实际锁存故障，不能据此写成故障注入、故障恢复或本地保护有效性测试通过。Windows最新控制提交仍未知，历史`fe1f0c5`只读基线不能代用。
- 该运行按上一节120秒应用总预算安排；本次输出没有起止时间或明确退出原因。退出0表示正常路径结束，不能据此认定确实运行满120秒、证明未提前正常断开，或用541行反推采样率。无外层兜底超时错误的回传证据。
- 运行前待机、负载关闭与心跳正常已有用户确认；本次“完全成功”未分别补充串口打开／接收／退出后三阶段心跳、复位观察、最终负载状态及退出后fuser输出，不从旧只读验收回填。网关退出不会隐式发送STOP，尤其本次人工验证过START，应由用户确认结束后负载关闭、心跳正常；如需处理负载，使用已有本地安全操作，不依赖已退出网关。
- 本次未收到netflow-analyzer对该会话的旁路抓包记录，也未验证真实USB断连、常驻服务、网络认证／TLS或长期运行。不能将会话联通扩展成这些项目完成。原始Qt日志、遥测及后续PCAP留在仓库外，按需只将脱敏摘要写入现有文档。

收尾与下一步：

1. 用户在板端只读执行`sudo fuser -v /dev/ttyUSB0`，并确认最终负载关闭、心跳及复位观察；若没有占用，命令通常不列进程，不能将正常无输出当作错误。
2. 保存Qt三项人工控制的请求／回执及前后遥测，补记Windows控制提交；只归档现有实验，不为补日志自动重放命令。资料暂缺不否定用户现场确认，但保留人工确认与逐条原始证据的层次。
3. 本阶段文档可统一提交为`docs(gateway): record Qt hardware control validation`，仅包含既有两份分类文档，Git由用户执行。本轮文档校验不重跑全量历史测试。
4. 下一阶段计划让独立netflow-analyzer在已核对的面向Qt网卡上旁路采集真实TCP会话，检查双向统计、握手／关闭状态和drop指标。先复核实际分析器程序及CLI，再给有界操作；本轮不启动抓包、不新增TCP探测客户端、不自动发送控制、不声称已实现TCP字节流重组或CMD应用层解析。


### 18.14 BMS真实TCP旁路采集准备（11月3日历史方案，当前PCAP任务见18.14.1）

目标是在鲁班猫面向Qt的网卡上，由独立netflow-analyzer旁路采集BMS网关与Qt的真实TCP通信，核对双向流统计、握手／关闭状态及特征输出。分析器不作为18080端口的TCP客户端或服务端、不打开UART、不转发控制命令，也不将UART日志当作其PCAP输入。已有功能足以开展首次验收，本轮不修改业务代码。

本轮实际VM检查（2026-11-03）：

- 用户已提交并推送`a66a6ef docs(gateway): record Qt hardware control validation`，本轮文档维护前分支`feature/flow-features`与远程同步、工作区干净；未代执行Git写操作。
- `cmake --build build -j2`成功，显示无构建工作；仅复核本阶段相关的`capture_tests`、`offline_flow_acceptance`、`tcp_flow_state_tests`、`flow_feature_export_tests`，4/4通过、0.12秒。不是重跑当前全部46项，也不是板端结果，不重跑ML或历史长稳测试。
- VM程序实际help与当前源码共同确认：实时模式支持interface／count／filter／feature-csv；实时特征导出必须有正count，普通`--csv`及可配置`--flow-idle-timeout`仅供离线。不能向后续实时命令加入这些离线选项。

板端只读核对方案（尚未返回本轮结果）：

- 查询hostname、架构、IPv4地址、eMMC可用空间及原分析器服务状态，不打开串口或抓包，不启停／改写服务配置。
- 依既有ARM64测试及招聘会部署资料，仅检查三处历史候选：`/home/cat/build/netflow-analyzer-arm64-verify/bin/netflow-analyzer`、`/home/cat/bin/nfa-demo-7bce180/netflow-analyzer`、`/usr/local/bin/netflow-analyzer`；对实际存在且可执行者读取file、SHA-256及help。路径是待核对候选，不是本轮已确认程序；不存在则报告，不自动搜索全盘、重建、安装或覆盖旧版。
- `--version`的0.2.0不足以区分历史功能阶段，须看实际help与产物身份。旧安装版历史help缺feature-csv，不能直接拿它承担新版特征验收；按本轮输出选择可用版本。若候选均缺失或不能运行，再决定最小部署方案，暂不启动硬件会话。

核对后拟执行的完整实验（仍为计划）：

- 用户先保持STM32待机、负载关闭及心跳正常。首轮网关采用默认只读模式、不加`--control`；仅需遥测通信产生一条真实TCP连接，不重复启动／停止／清除故障，不为抓包制造负载动作。
- 提前准备两个板端终端及Windows Qt：分析器先启动，确认采集已准备后再启动有界网关，最后只由Qt连接；不使用Test-NetConnection／nc／Python客户端探测唯一网关会话。
- 计划分析器90秒捕获窗口、网关60秒总预算，给连接建立和关闭预留前后捕获时间；最终命令在板端程序与地址确认后再给。BPF限定TCP、已核对的板端地址和端口18080，避免SSH及其他流量占用包数上限；不猜Windows源地址。
- 若导出实时特征，配置足够大的有限count（拟10000）与定时SIGTERM正常收尾双重约束，不用小包数截断握手或FIN。输出放仓库外独立目录、日志直接重定向文件，避免Ctrl+C影响tee收尾；不自动修改setcap、服务用户、网卡或网络配置。
- Qt保持接收，正常关闭连接或等待网关预算结束；分析器在关闭报文之后收尾。检查实际流方向、TCP状态、统计分类、两个drop及CSV；closed是正常关闭的目标观察结果，不预先写成已通过。活动流与已过期流均须检查，不能只看最终Flow summary。
- UART文本行数、TCP包数与流特征行数不是同一单位：一个TCP包可承载半行／多行，ACK／握手／关闭也计包；该通信通常聚合成少量流，而非每条BMS遥测生成一条流。当前旁路状态机不做TCP字节流重组或BMS CMD语义解析，也不等于在线异常告警。

本轮仅记录VM复核和板端检查方案；尚未启动本次网关、抓包或Qt会话，不将既有实物控制结果当作已完成分析器联动。提交建议为`docs(integration): plan passive BMS TCP capture`，Git由用户执行。

### 18.14.1 当前任务：真实Ethernet PCAP与双向流CSV（2026-11-04，板端预检已回传）

本节按用户新明确的任务执行，取代上一节原只读／90秒抓包／60秒网关／仅特征CSV方案。暂停SQLite方向；不新增数据库依赖、MCU／Qt代码、BMS应用层解析或自动控制，不重复全量历史测试。

用户提供与确认的当前条件：

- Windows控制代码基线`528da45`，Linux历史控制基线`cfaf3bf214446818ddeeeab4f72e7f948a7b8f99`；Windows工作区未由助手检查，不将该参考提交推断为未修改的运行产物身份。
- 用户本次贴出的Windows ipconfig显示物理“以太网”IPv4为`192.168.1.100/24`、网关`192.168.1.1`。WSL的`172.20.208.1`、VMnet1的`192.168.2.1`、VMnet8的`192.168.78.1`不是本次Qt目标链路选用的Windows地址。
- 用户明确确认Qt已断开TCP，STM32待机、负载输出关闭、心跳正常。随后只由用户在捕获就绪后连接Qt，观察60～90秒、人工点击一次STOP、核对界面再主动断开；不安排START／CLEAR或制造故障。

本轮实际VM检查与产物：

- 当前工作区为`/home/zcb/workspace/netflow-analyzer`，主机`zcb-Ubuntu`、`x86_64`、内核`7.0.0-38-generic`；分支`feature/flow-features`，HEAD及远程为`a66a6ef`，保留此前未提交的本文件18.14节修改。未使用另一份旧源码目录替代当前工作区。
- README已读，但其中部分旧进度不代表当前功能；实际CLI为0.2.0，支持实时`--interface/--filter`与离线`--read/--csv`，没有`--write`。本轮目标保存原始PCAP并离线导出完整流CSV，不能把实时feature-csv当作完整流CSV。
- 当前Debug缓存的CMAKE_HOME_DIRECTORY指向本工作区；仅构建`netflow_analyzer_cli`，输出无工作可做，未运行CTest。本轮无C源码、头文件、CMake或测试改动；比较cfaf3bf至当前HEAD的gateway／src／include／CMakeLists.txt未见代码差异。
- VM实际分析器为`/home/zcb/workspace/netflow-analyzer/build/bin/netflow-analyzer`，x86-64 PIE、含debug_info；SHA-256为`781a5670d603e6764807dd6ace6cc288f27f79d05b1e83a304a139ba73a1aaf4`。该程序不是ARM64产物。
- 从当前工作区创建独立构建目录`/home/zcb/build/netflow-bms-pcap-arm64-a66a6ef.CfXCvN`，使用现存官方SDK GCC 9.3.0及ARM64 libpcap 1.9.1 overlay；Release、BUILD_TESTING=OFF、CMAKE_SKIP_RPATH=ON，只构建分析器，23步成功、退出0。没有覆盖旧构建／安装目录或重跑板端测试。
- 新构建的`bin/netflow-analyzer`为ARM aarch64、动态解释器`/lib/ld-linux-aarch64.so.1`、未strip；直接依赖libpcap.so.0.8和libc.so.6，程序最高GLIBC需求为2.17，未见RPATH／RUNPATH。SHA-256为`47ade7acd3e5b30029f9264b4052d92322718d19a25b3c0af5f22832da3b1aaa`；虽与历史演示产物摘要相同，这次来自当前工作区新构建，不是回到旧源码目录取文件。尚未传输到板端或运行，板端ABI／CLI仍待实际检查。
- 当前用户没有SSH配置文件。助手只对历史候选`cat@192.168.1.102`进行一次15秒上限、BatchMode及StrictHostKeyChecking=yes的只读SSH身份／网络查询，得到`Permission denied (publickey,password)`、退出255，远端命令未执行。不会重复认证探测、收集密码或修改SSH设置；需用户在已有板端终端执行预检，密码只在本地终端输入。该失败不能据此确认板端当前hostname、架构、接口或网关状态。

板端接下来只读核对：

- 在用户当前实际使用的板端项目终端读取pwd／Git状态与HEAD；若不是Git目录则如实报告，不跳入另一份历史旧工作区替代当前版本。
- 读取hostname、uname、当前用户、IPv4／链路、`ip route get 192.168.1.100`、TCP监听与已建立连接、网关／接收器／分析器／tcpdump进程；只列状态，不新建TCP探测连接、不杀未知进程。
- 核对实际UART节点、udev驱动、当前会话权限、ModemManager及fuser占用。只查询已验证网关隔离产物的file／SHA／help；不因历史目录存在就打开串口，若当前已有网关占用，先报告后决定如何复用／正常结束，禁止第二个进程抢串口。
- 根据去Windows的实际路由确认物理Ethernet接口，再查看其link及tcpdump支持的链路类型（-L，不开始保存数据），必须兼容EN10MB／Ethernet。不选any，不用未核对的eth0、IP或18080替代实际结果；同时核对tcpdump版本、可用存储和当前服务状态。

待上述结果齐全后的有界采集约定（尚未启动）：

- 新建仓库外唯一实验目录，记录新ARM64程序传输前后SHA、实际help及来源；不替换旧安装程序或启停原systemd服务。网关使用已核对产物和节点、显式IPv4／端口，120～180秒总预算，--control放末尾以供用户人工STOP；等待Qt同样计时。
- BPF仅为`tcp and host <已确认Windows IPv4> and port <实际网关端口>`。在实际Ethernet接口上先启动tcpdump保存PCAP，并按板端实际help启动独立分析器的interface／filter实时入口；不假设分析器能write PCAP，不把PCAP路径传给实时csv选项。
- 保存stdout／stderr、受控PID与退出码，设置墙钟和包数上限；就绪必须由真实进程与tcpdump监听日志等证据确认，明确提示用户后才连接Qt。不以进程已准备或0包日志作为采集通过，也不长时间阻塞助手等操作。
- 用户观察60～90秒、人工一次STOP并主动断开后，再让握手／关闭包完整进入捕获并正常结束采集，保存tcpdump捕获／内核丢弃统计。若超时／异常提前结束，按实际状态记录，不补写closed或零drop。
- 对同一PCAP校验身份／链路类型，用实际分析器read／csv导出完整流记录，选出目标双向TCP流。源码确认CSV为16列：protocol、tcp_state、两端IP／端口、双向packet／captured_bytes／wire_bytes及首末秒／微秒；实际数据仍待产生。端点按IP再端口规范化排序，不等同客户端／服务器；captured与wire累加捕获记录长度，不是BMS文本字节。
- TCP包数不等于BMS报告条数；tcpdump内核捕获丢弃不等于TCP网络丢包。没有TCP字节流重组，不通过逐包搜索BMS文本判断消息丢失。PCAP、CSV和原始日志留仓库外，仅将脱敏摘要与可复现命令写入既有文档，Git由用户操作。

当前状态：VM身份／CLI／CSV口径及新ARM64构建已核对；Windows物理IPv4与操作前安全状态已由用户确认。用户随后已回传板端身份、路由／Ethernet、进程／监听、UART占用及tcpdump链路类型，见18.14.2；产物部署、捕获就绪、PCAP、流CSV及流统计仍待验证。本轮尚无真实采集结果，不能出具目标流数值或采集通过结论。

### 18.14.2 板端预检结果与隔离部署（2026-11-04，用户已回传部署预检通过）

证据来源：用户附件`f14aa4b5-5f21-4a50-a142-5a47dc2c6b88/已粘贴的文本.txt`已完整读取，以下是板端回传结果，不是助手通过SSH执行的结果；助手仍无可用的非交互板端认证。

| 项目 | 本次实际结果 |
| --- | --- |
| 身份 | `lubancat`、`aarch64`、内核`4.19.232`；cat UID1000，当前会话含dialout组 |
| 工作目录 | `/home/cat`，不是Git仓库；未切到另一份旧工作树代替当前VM源码 |
| Windows路由 | `192.168.1.100 dev eth0 src 192.168.1.102` |
| 实际网卡 | `eth0 UP/LOWER_UP`，MTU1500，MAC`d2:33:17:12:93:f3`；硬件路径`/sys/devices/platform/fe2a0000.ethernet` |
| 监听／连接 | ss未列18080监听或连接；已有远程连接为SSH等，不作为Qt会话 |
| 相关进程 | ps未列bms-gateway、bms-serial-recv、netflow-analyzer或tcpdump |
| 实际UART | `/dev/ttyUSB0`，root:dialout、0660，USB`1a86:7523`、驱动ch341；当前读写检查通过，fuser未列占用 |
| ModemManager | loaded、inactive、dead |
| tcpdump | `/usr/sbin/tcpdump`，4.9.3；libpcap1.9.1（TPACKET_V3） |
| 链路类型 | tcpdump -L列EN10MB（Ethernet）及DOCSIS；本轮仅使用EN10MB，不用any或DOCSIS |
| 根分区 | `/dev/mmcblk0p3` ext4，7.0G、已用6.5G、可用214M、97% |

网关隔离产物`/home/cat/bms-control-v1.X3nWaz/bms-gateway`已由用户重新查询file／SHA／help：ARM aarch64，BuildID`e604df17e5961ef20b1d1d2c609e0c4cec343958`，SHA-256仍为`1aa74d69e8be36f91bfaf4b89652f9308249c56632f99de96d3330fb92ff7fde`，CLI为`SERIAL IPV4 PORT [SECONDS] [--control]`、默认30秒／1..300秒。查询help未打开串口；真实监听与后续控制能力仍需新会话证据。拟显式使用本次已确认空闲的192.168.1.102:18080，BPF为`tcp and host 192.168.1.100 and port 18080`。

存储决定与待执行部署：

- 根分区接近满，PCAP／CSV／运行日志安排在经重新核对挂载的数据卡，不直接写入/home/cat或/tmp；本轮不清理VS Code、软件包、历史实验，不格式化、挂载或修改服务。
- `/media/usb0`仅是历史数据卡挂载候选，必须用findmnt的-M精确验证为挂载点，且设备号不同于根文件系统、当前可写、剩余至少1GiB；若不满足，立即停止并回传结果，不能创建一个普通根盘目录冒充数据卡。该空间阈值是本次预留，不代表可无限采集，后续仍用包数与墙钟限制。
- ELF不放在可能带noexec的VFAT卡上。当前新构建的ARM64文件已在VM再次核对，62,504字节、SHA仍为`47ade7acd3e5b30029f9264b4052d92322718d19a25b3c0af5f22832da3b1aaa`；仅它和运行小型元数据放新eMMC运行目录，原安装与旧构建不覆盖。
- 拟新运行目录`/home/cat/nfa-bms-capture-a66a6ef-20261104`；拟新数据目录`/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-01`。二者尚未创建，创建前拒绝任何已存在目标，使用不带-p的mkdir确保不覆盖；若目标已存在保留并诊断，不删除重建。

板端A：验证挂载并创建新目录（尚未执行）：

```bash
(
    set -eu
    test "$(uname -m)" = aarch64

    NFA_DATA_MOUNT=/media/usb0
    findmnt -n -M "$NFA_DATA_MOUNT" -o TARGET,SOURCE,FSTYPE,OPTIONS
    test "$(stat -c %d "$NFA_DATA_MOUNT")" != "$(stat -c %d /)"
    test -w "$NFA_DATA_MOUNT"
    df -hT /home/cat "$NFA_DATA_MOUNT"
    NFA_DATA_FREE_KIB="$(df -Pk "$NFA_DATA_MOUNT" | awk 'NR == 2 {print $4}')"
    test "$NFA_DATA_FREE_KIB" -ge 1048576

    NFA_RUN_DIR=/home/cat/nfa-bms-capture-a66a6ef-20261104
    NFA_DATA_DIR=/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-01
    test -d /media/usb0/netflow-analyzer-data
    test ! -e "$NFA_RUN_DIR"
    test ! -e "$NFA_DATA_DIR"
    mkdir -- "$NFA_RUN_DIR" "$NFA_DATA_DIR"
    printf '运行目录：%s\n数据目录：%s\n目录准备完成，尚未开始捕获\n' \
        "$NFA_RUN_DIR" "$NFA_DATA_DIR"
)
```

虚拟机：仅在板端A成功后交互复制新程序，密码只在本地终端输入（尚未执行）：

```bash
NFA_ARM_BIN=/home/zcb/build/netflow-bms-pcap-arm64-a66a6ef.CfXCvN/bin/netflow-analyzer
sha256sum "$NFA_ARM_BIN"
if timeout --signal=TERM --kill-after=5s 90s \
    scp -p -o ConnectTimeout=10 -o StrictHostKeyChecking=yes \
        "$NFA_ARM_BIN" \
        cat@192.168.1.102:/home/cat/nfa-bms-capture-a66a6ef-20261104/netflow-analyzer
then
    printf '程序传输退出码：0\n'
else
    NFA_COPY_STATUS=$?
    printf '程序传输退出码：%s；停止后续操作\n' "$NFA_COPY_STATUS"
fi
```

板端B：只在传输成功后验证新产物与数据路径，不启动实时入口（尚未执行）：

```bash
(
    set -eu
    NFA_BIN=/home/cat/nfa-bms-capture-a66a6ef-20261104/netflow-analyzer
    NFA_DATA_DIR=/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-01
    NFA_ACTUAL_SHA="$(sha256sum "$NFA_BIN")"
    printf '%s\n' "$NFA_ACTUAL_SHA"
    test "${NFA_ACTUAL_SHA%% *}" = \
        47ade7acd3e5b30029f9264b4052d92322718d19a25b3c0af5f22832da3b1aaa
    file "$NFA_BIN"
    ldd "$NFA_BIN"
    "$NFA_BIN" --version
    "$NFA_BIN" --help
    findmnt -n -M /media/usb0 -o TARGET,SOURCE,FSTYPE,OPTIONS
    test -d "$NFA_DATA_DIR"
    test -w "$NFA_DATA_DIR"
    printf '产物和数据目录预检通过，尚未启动抓包或网关\n'
)
```

以上三段保留为部署操作记录，不再原样重跑创建目录或覆盖程序。用户随后回传挂载、目录创建、产物校验／ldd／实际CLI通过的结果，见18.14.3；未单独贴出SCP退出码，不补写。首轮仅用户人工STOP，不START／CLEAR；采集结束再离线read／csv导出同一PCAP。

### 18.14.3 部署预检通过与捕获启动方案（2026-11-04，就绪输出见18.14.4）

用户本次回传结果：

- `/media/usb0`实际为`/dev/mmcblk1p1`的vfat挂载，rw、noexec、sync、uid／gid1000；30G、已用1.8G、可用28G、7%。根分区仍为214M空闲／97%，不在根分区保存PCAP／CSV／捕获日志。
- 新运行目录`/home/cat/nfa-bms-capture-a66a6ef-20261104`与新数据目录`/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-01`已创建，成功标记明确尚未开始捕获。
- 板端新分析器SHA为`47ade7acd3e5b30029f9264b4052d92322718d19a25b3c0af5f22832da3b1aaa`，与当前VM隔离构建一致；file为ARM aarch64、动态解释器`/lib/ld-linux-aarch64.so.1`。实际ldd解析libpcap.so.0.8及libc.so.6均成功，version为0.2.0，实际help包含interface／filter与read／csv，无write。程序及数据目录预检通过，不等于实时采集通过。

以下为交给用户在三个鲁班猫Bash终端执行的捕获启动方案，助手未远程执行。先打开并准备好三个终端，Qt保持断开。A／B分别前台运行，避免后台任务及tee的SIGINT生命周期歧义；各有420秒墙钟兜底和10000包上限，不是要求等满420秒。内层sh只记录其PID后exec实际命令，方便确认进程身份与后续精确停止，不新增业务代码。所有重定向使用noclobber，既有输出存在即停止，不覆盖或删除旧实验。

终端A：tcpdump保存原始PCAP（待执行）：

```bash
(
    set -euC
    NFA_DATA_DIR=/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-01
    findmnt -n -M /media/usb0 >/dev/null
    test -d "$NFA_DATA_DIR"
    test -w "$NFA_DATA_DIR"
    for NFA_NAME in traffic.pcap tcpdump.stdout tcpdump.stderr tcpdump.pid tcpdump.exit-code; do
        test ! -e "$NFA_DATA_DIR/$NFA_NAME"
    done
    sudo -v
    printf 'tcpdump即将启动；本终端保持运行，接着去终端B，不要连接Qt\n'
    if sudo timeout --preserve-status --signal=INT --kill-after=5s 420s \
        sh -c 'set -euC; printf "%s\n" "$$" > "$1"; shift; exec "$@"' \
        nfa-tcpdump "$NFA_DATA_DIR/tcpdump.pid" \
        /usr/sbin/tcpdump -i eth0 -y EN10MB -p -n -s 0 -U -c 10000 -Z cat \
        -w "$NFA_DATA_DIR/traffic.pcap" \
        'tcp and host 192.168.1.100 and port 18080' \
        > "$NFA_DATA_DIR/tcpdump.stdout" 2> "$NFA_DATA_DIR/tcpdump.stderr"
    then
        NFA_DUMP_STATUS=0
    else
        NFA_DUMP_STATUS=$?
    fi
    printf '%s\n' "$NFA_DUMP_STATUS" > "$NFA_DATA_DIR/tcpdump.exit-code"
    printf 'tcpdump退出码：%s\n' "$NFA_DUMP_STATUS"
    cat "$NFA_DATA_DIR/tcpdump.stderr"
)
```

终端B：当前新产物实时interface／filter入口，仅记录实时统计，不传实时csv选项（待执行）：

```bash
(
    set -euC
    NFA_DATA_DIR=/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-01
    NFA_BIN=/home/cat/nfa-bms-capture-a66a6ef-20261104/netflow-analyzer
    NFA_STDBUF="$(command -v stdbuf)"
    findmnt -n -M /media/usb0 >/dev/null
    test -x "$NFA_BIN"
    for NFA_NAME in live.stdout live.stderr live.pid live.exit-code; do
        test ! -e "$NFA_DATA_DIR/$NFA_NAME"
    done
    sudo -v
    printf '实时分析器即将启动；本终端保持运行，接着去终端C，不要连接Qt\n'
    if sudo timeout --preserve-status --signal=TERM --kill-after=5s 420s \
        sh -c 'set -euC; printf "%s\n" "$$" > "$1"; shift; exec "$@"' \
        nfa-live "$NFA_DATA_DIR/live.pid" \
        "$NFA_STDBUF" -oL -eL "$NFA_BIN" \
        --interface eth0 --count 10000 \
        --filter 'tcp and host 192.168.1.100 and port 18080' \
        > "$NFA_DATA_DIR/live.stdout" 2> "$NFA_DATA_DIR/live.stderr"
    then
        NFA_LIVE_STATUS=0
    else
        NFA_LIVE_STATUS=$?
    fi
    printf '%s\n' "$NFA_LIVE_STATUS" > "$NFA_DATA_DIR/live.exit-code"
    printf '实时分析器退出码：%s\n' "$NFA_LIVE_STATUS"
    tail -n 30 "$NFA_DATA_DIR/live.stdout"
    cat "$NFA_DATA_DIR/live.stderr"
)
```

终端C：最多10秒的就绪检查，不启动网关、不打开串口、不建立TCP连接（待执行）：

```bash
(
    set -eu
    NFA_DATA_DIR=/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-01
    NFA_BIN=/home/cat/nfa-bms-capture-a66a6ef-20261104/netflow-analyzer
    sudo -v
    NFA_CAPTURE_READY=0
    for NFA_TRY in 1 2 3 4 5 6 7 8 9 10; do
        if test -s "$NFA_DATA_DIR/tcpdump.pid" &&
           test -s "$NFA_DATA_DIR/live.pid" &&
           grep -Fq 'listening on eth0, link-type EN10MB' "$NFA_DATA_DIR/tcpdump.stderr" &&
           grep -Fxq 'Capture interface: eth0' "$NFA_DATA_DIR/live.stdout" &&
           grep -Fxq 'BPF filter: tcp and host 192.168.1.100 and port 18080' "$NFA_DATA_DIR/live.stdout" &&
           grep -Fxq 'Link type: Ethernet' "$NFA_DATA_DIR/live.stdout"; then
            NFA_CAPTURE_READY=1
            break
        fi
        sleep 1
    done
    if test "$NFA_CAPTURE_READY" -ne 1; then
        printf '捕获未确认就绪；不要启动网关或连接Qt，请回传两个终端及日志输出\n'
        exit 1
    fi
    NFA_DUMP_PID="$(cat "$NFA_DATA_DIR/tcpdump.pid")"
    NFA_LIVE_PID="$(cat "$NFA_DATA_DIR/live.pid")"
    case "$NFA_DUMP_PID" in ''|*[!0-9]*) exit 1;; esac
    case "$NFA_LIVE_PID" in ''|*[!0-9]*) exit 1;; esac
    test "$NFA_DUMP_PID" -gt 1
    test "$NFA_LIVE_PID" -gt 1
    sudo kill -0 "$NFA_DUMP_PID"
    sudo kill -0 "$NFA_LIVE_PID"
    test "$(sudo readlink "/proc/$NFA_DUMP_PID/exe")" = "$(readlink -f /usr/sbin/tcpdump)"
    test "$(sudo readlink "/proc/$NFA_LIVE_PID/exe")" = "$(readlink -f "$NFA_BIN")"
    ps -p "$NFA_DUMP_PID,$NFA_LIVE_PID" -o pid,user,etimes,args
    sed -n '1,4p' "$NFA_DATA_DIR/tcpdump.stderr"
    sed -n '1,10p' "$NFA_DATA_DIR/live.stdout"
    printf 'CAPTURE_READY_GATEWAY_NOT_STARTED：两路捕获当前已就绪，网关尚未启动；先不要连接Qt，请回传此输出\n'
)
```

只有C真实输出成功标记才能登记当前捕获就绪，不能从准备脚本或历史日志推断。pid文件经kill -0与/proc/exe核对，避免把退出后的旧日志当作当前运行；ps的etimes用于核对剩余采集预算。用户回传后再在C验证进程仍活跃及剩余预算，启动180秒网关并明确通知Qt连接192.168.1.102:18080。若等待过久，旧会话不得强行续接；先正常结束、保留旧文件，再另建新实验目录。

需要提前取消时，可在各自前台A／B终端按Ctrl+C一次，观察日志及退出码；当前无tee管道，不用pkill／killall或猜测PID。更精确的后续停止会先核对记录PID的实际exe，再给tcpdump发SIGINT、实时分析器发SIGTERM，并回A／B核对收尾。正常目标是在Qt主动断开且关闭报文进入捕获后提前结束，不必等满420秒；兜底只是防止无人收尾无限运行。尚未收到捕获进程、PCAP或TCP流证据，部署通过不等于本轮采集通过。

### 18.14.4 两路捕获已启动，待Qt真实会话（2026-11-04）

用户回传终端C的实际输出：tcpdump PID为163099、用户cat、etimes为19秒；实时分析器PID为163205、用户root、etimes为7秒。tcpdump显示`listening on eth0, link-type EN10MB (Ethernet), capture size 262144 bytes`；分析器显示eth0、10000包上限、`tcp and host 192.168.1.100 and port 18080`、Ethernet链路和5秒指标周期。首个周期packets=0、active_flows=0，与Qt尚未接入的准备阶段相容，不作为真实TCP采集通过证据。就绪脚本已真实输出`CAPTURE_READY_GATEWAY_NOT_STARTED`。

- 此为用户回传时两路捕获实际就绪，不保证随后仍在420秒预算内。启动网关前从各pid文件重新读取PID，核对`/proc/<pid>/exe`、进程存活及etimes不超过210秒，确保余下时间覆盖180秒网关总预算、190秒外层兜底和收尾余量。超时或进程退出则不启动网关，先保留旧文件并结束残留捕获，另开新实验目录；不复用旧日志冒充当前就绪。
- 下一步命令供用户在鲁班猫终端C执行，助手不远程操作：复核已验证网关SHA、`/dev/ttyUSB0`的CH340身份／ch341绑定／当前会话权限、ModemManager仍inactive、端口18080无监听和串口无占用；然后以普通用户运行`/home/cat/bms-control-v1.X3nWaz/bms-gateway /dev/ttyUSB0 192.168.1.102 18080 180 --control`。外层`timeout --signal=TERM --kill-after=5s 190s`仅作兜底，stderr合并保存为数据目录下`gateway.runtime.txt`，管道两个退出码立即从PIPESTATUS保存至`gateway.exit-status.txt`。先检查目标日志不存在，不覆盖既有文件。
- 只有实际出现`LISTEN 192.168.1.102:18080`且A／B仍运行时才由用户Windows Qt连接；OPEN_SERIAL与control enabled应在接入之后出现。不得为判断端口可达另启nc／Test-NetConnection或虚拟客户端，避免占用单客户端会话。`--control`只开启白名单能力，不自动发送任何MCU命令。
- 本轮用户保持负载关闭／待机，观察60～90秒，其间仅人工点击一次STOP、保存实际CMD编号与ACK／ERR和后续遥测；不START、不CLEAR、不自动重连或为补日志重试。ACK UART_ACCEPTED只表示串口驱动接受字节，起始已经load=0不能据此证明负载运行态关断。用户随后主动断开Qt TCP；网关退出不隐式发STOP。
- 网关收尾后等约2秒让关闭报文进入捕获，再在A／B各自前台终端按一次Ctrl+C，等待各自输出退出码与统计；也可按已记录PID并核对实际exe后精确发信号。保留A／B输出、网关日志及Qt请求／回执；下一轮再对已结束并落盘的同一PCAP离线read／csv。关闭是否完整、drop数、PCAP包数和双向流状态均待实际结果，不从准备成功推断。

后续用户报告：在鲁班猫终端C执行网关启动整段后立即返回，没有任何输出。尚未收到LISTEN、OPEN_SERIAL、gateway日志或退出码，因此不能登记网关已启动或判定具体失败项。给出的命令在首个运行提示前使用了多项`test`及`set -e`，其中任一失败即可无解释退出；这是当前诊断方向，不代表已经确认权限、SHA、服务或捕获超时中的某项为根因。下一步只读打印这些实际状态及捕获PID／etimes／退出文件，不重跑启动段、不建立探测连接、不打开串口；即使定位后修正检查，也须重新核对420秒捕获窗口，保留旧数据，不盲目续接或覆盖。

只读诊断回传（同日）：当前机器确为lubancat／aarch64；终端C的`id`仅含cat、sudo、audio、video，不含dialout。实际`/dev/ttyUSB0`为root:dialout／0660、1a86:7523／ch341，读／写检查均返回1；这是启动段在提示之前退出的直接权限障碍。网关SHA仍为1aa74d69…ff7fde、可执行，数据卡目录可写，ModemManager为inactive／dead，18080无监听，fuser返回1且没有占用。两路捕获PID均已不在，两个保存退出码均为0，网关两份运行记录不存在。此次没有Qt流采集通过证据，也不从退出0推断捕获结束原因或PCAP内容。

下一步先由用户核对`id cat`／`getent group dialout`；若账户记录仍有dialout，刷新终端C的身份（新SSH登录或单独执行`newgrp dialout`进入新Shell），再回传`id`和只读权限检查。不重复加组、不放宽权限、不以sudo绕过；目前刷新尚未实际验证。权限通过后另建新的数据目录重开A／B捕获，本次01目录原样保留，不能重跑原目录覆盖输出。详细排查并入既有`docs/problem_log.md`第5.24节，不新建零散Markdown。

本轮仅文档更新；没有改动源码、启动板端程序、自动发送真实控制或重跑历史测试。Git仍由用户执行，建议最终采集收尾后统一提交脱敏文档，不提交原始PCAP或硬件日志。

### 18.14.5 本机终端权限复查通过，新一轮采集目录由02改为03（2026-11-04，待执行）

用户回传`ls -l /dev/ttyUSB0`为root:dialout／0660，以及当前本机终端读写检查`[PASS] 当前终端串口权限通过`。这次访问检查已通过，后续网关应在这个相同终端／Shell执行；未收到新的完整id或实际采用的刷新方法，不指定为newgrp、SSH或注销桌面修复，不推断其他终端状态。未打开设备，也没有新网关会话或PCAP证据。

原拟使用02，用户随后明确02目录已存在，并指定新目录改为`/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-03`。本次完整重发的四段命令已全部改为03，旧01与02目录原样保留；02内具体文件／进程状态尚未回传，不将“目录被占用”扩展成已启动或完成一次采集。执行顺序为：终端C先完成不启动计时的身份／串口身份与权限／实际路由／两个产物SHA／服务、端口、占用和数据卡预检，再用不覆盖的mkdir创建03；随后用户连续执行终端A的tcpdump、终端B的实时分析器、同一终端C的有界就绪检查和网关启动，不在捕获运行中等待聊天回传。改为03后的四段命令已重新在VM通过`bash -n`，这是脚本语法检查，不是板端执行或业务验收；没有创建03或重启板端进程的助手实操证据。

- A／B维持原物理eth0／EN10MB、Windows192.168.1.100／TCP18080的BPF、420秒及10000包上限、实际exec PID记录、各自输出和退出码；本次更新仅是用户手动执行命令，不改业务源码。关键检查增加ERR提示，失败会显示对应命令，不再只有静默test退出。
- C在最多10秒就绪等待后核对PID、实际exe、存活及运行时间不超过210秒，打印CAPTURE_READY后立即运行已验证网关180秒总预算（190秒外层兜底，--control末尾），保留stderr和管道状态。只有现场出现LISTEN且A／B仍运行时才能用Windows Qt连接；不做额外TCP探测，也不自动发送MCU控制。若出现FAIL、捕获提前退出或既有03目录／文件，停止并回传，不删旧文件重试。
- 用户在Qt观察60～90秒、最多人工一次STOP，保留能力／请求／回执及后续遥测，不START／CLEAR，不为了启用按钮而启动负载。主动断开后等网关返回、再等约2秒，于A／B各按一次Ctrl+C并等待统计、退出码。下一轮才离线分析已完整落盘的PCAP；无完整握手／关闭时如实记录，不预先宣称closed或零drop。

本节保留准备时“权限预检通过，新一轮真实采集待执行”的状态；后续已执行结果见18.14.6，不再作为待执行指令。本轮只维护现有分类文档，未修改业务代码、用户组／设备权限／服务配置，未重跑历史测试，Git由用户执行。

### 18.14.6 真实BMS TCP采集与PCAP到CSV一致性验收通过（2026-11-04）

以下为用户回传的本次板端输出，不是VM回放或助手远程执行。数据目录为`/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-03`；未回传新的源码／二进制变更，既有部署来源为VM的a66a6ef（下述采集不改源码），分析器为`/home/cat/nfa-bms-capture-a66a6ef-20261104/netflow-analyzer`，此前已在板端验证SHA为47ade7ac…3b1aaa。网关为`/home/cat/bms-control-v1.X3nWaz/bms-gateway`，此前已验证SHA为1aa74d69…ff7fde。Windows528da45仅为用户给出的控制基线，不替代Windows工作树检查。

| 项目 | 实际回传结果 |
| --- | --- |
| 捕获接口／类型 | eth0、EN10MB（Ethernet），snapshot 262144字节 |
| 当前过滤表达式 | `tcp and host 192.168.1.100 and port 18080`（按此次03命令执行；就绪日志确认程序与接口） |
| 捕获启动先后 | 网关启动前tcpdump已运行8秒、live已运行4秒，输出CAPTURE_READY |
| tcpdump收尾 | captured=2041、received by filter=2041、dropped by kernel=0；退出码0 |
| 实时分析器收尾 | total=2041、complete=2041；truncated／malformed／unsupported／flow_rejected均0，退出码0 |
| 实时后端统计 | received=2043、capture dropped=0、interface dropped=0 |
| 网关 | LISTEN 192.168.1.102:18080、OPEN_SERIAL /dev/ttyUSB0、READY 115200/8N1 DTR=0 RTS=0 HUPCL=0，control enabled |
| 网关收尾 | lines=1014、discarded=1、tail_bytes=0、discarding=0；网关／timeout退出0、tee退出0 |
| 实时流 | 1条TCP流，观察到的tcp_state=closed |
| A端点／B端点 | Windows 192.168.1.100:30502／LubanCat 192.168.1.102:18080 |
| A→B | 1021包，captured=61272 B、wire=61272 B |
| B→A | 1020包，captured=233958 B、wire=233958 B |
| 首末包时间 | 1793785181.723066～1793785350.928395，跨度169.205329秒 |
| 本轮STOP | 用户明确未点击STOP，不登记本轮STOP请求／回执或控制验收 |
| 心跳与负载 | 用户现场确认打开串口／转发期间／退出后三阶段心跳正常，未观察到复位，最终负载关闭 |
| 离线PCAP处理 | total=2041、complete=2041；其余四类均0，1条双向TCP流 |
| CSV读回 | flows.csv为2行（16列表头＋1条流），protocol=6、tcp_state=closed，身份、状态、方向包数／字节数核对PASS |

- 两方向合计2041包，与tcpdump捕获包数一致；捕获／原始记录长度各合计295230 B。方向由IP／端口规范化排序决定，A不是固定的客户端语义，本次恰好对应Windows。captured按caplen累计，wire按PCAP记录原始长度累计，均不是TCP应用正文长度，也不计入未由捕获记录提供的以太网前导码等物理开销。
- `closed`是现有旁路TCP状态机的观察结果，不等同于内核socket状态或严格序列号／重传验证。169.205329秒是首末捕获时间差，不是整个进程墙钟时长或人工观察时长；此次未提供明确退出原因，不能认定是用户主动断开或运行满180秒。
- tcpdump的kernel drop=0、实时分析器自己的capture dropped=0和interface dropped=0现已分别有实际回传，不是用一个句柄的值代填另一个。但这些捕获指标不证明TCP网络无丢包、无重传或Qt应用无缺行。实时received=2043来自pcap_stats累计快照，应用实际处理2041包，两个数属于不同统计层级；只据现有汇总不能还原额外2包的内容或明确原因，不直接登记为网络丢包、串口丢行或解析失败。应用、PCAP及离线CSV的2041包核对一致。
- 1014是网关完整串口行入TCP输出缓存的统计，不是Qt确认接收条数；不把它等同于1020个B→A TCP包，也不将差值6直接解释为TCP控制包数。discarded=1与起始片段丢弃策略相容，没有原始字节证据还原其内容，不写成丢失一整条报告。
- 当前已确认真实网卡采集、目标双向流聚合、观察到closed和网关正常收尾。用户随后明确本轮未点击STOP，因此本轮按“控制模式开启下的遥测TCP会话”记录，不计为STOP控制验收，也不将没有STOP回执记作控制失败；不能把control enabled当成实际发过命令或改写为不带--control的只读启动。无需为补STOP重做采集或重放硬件命令。三阶段心跳正常、未观察到复位和最终负载关闭属于用户现场确认，已补入表格；Qt本连接能力帧原文未贴出，不补写接收日志或具体内容。

离线核对已实际执行并由用户回传，不再是仅在VM通过语法检查：板端以同一分析器运行`--read traffic.pcap --csv flows.csv`，不加idle切段、不传实时filter、不打开串口；脚本使用30秒外层兜底、拒绝覆盖输出、保存退出码和SHA。实际输出2041包全部complete、零拒绝／解析错误，导出并读回1条流，全部12项身份／状态／方向计数与实时结果相同，输出`[PASS] PCAP flow CSV identity, state and counters match live flow`。CSV首末时间实际也与实时汇总相同，`offline_duration_microseconds=169205329`。本次未另贴脚本第一行离线退出码，但流程已通过进入读回核对和SHA输出前的退出0检查，不编造未粘贴的原文。

本轮运行参数记录（不再重跑或覆盖现有产物；完整PID／日志／拒绝覆盖封装见本节前面的方案与用户实际执行命令）：

- PCAP：`/usr/sbin/tcpdump -i eth0 -y EN10MB -p -n -s 0 -U -c 10000 -Z cat -w "$NFA_DIR/traffic.pcap" 'tcp and host 192.168.1.100 and port 18080'`，外层sudo／timeout为420秒、INT、5秒强制结束宽限、preserve-status。
- 实时分析器：`stdbuf -oL -eL "$NFA_BIN" --interface eth0 --count 10000 --filter 'tcp and host 192.168.1.100 and port 18080'`，外层sudo／timeout为420秒、TERM、5秒宽限、preserve-status。
- 网关：`"$BMS_BIN" /dev/ttyUSB0 192.168.1.102 18080 180 --control`，外层timeout为190秒、TERM、5秒宽限，stderr合并tee保存；180秒包含等客户端时间，只有Qt接入后才打开串口。
- 离线：`"$NFA_BIN" --read "$NFA_DIR/traffic.pcap" --csv "$NFA_DIR/flows.csv"`，外层timeout为30秒、TERM、5秒宽限。

上述NFA_DIR为本节03数据目录，NFA_BIN和BMS_BIN为本节开头的两个实际板端产物路径。本次回传的完整SHA-256：

| 产物 | SHA-256 |
| --- | --- |
| netflow-analyzer | `47ade7acd3e5b30029f9264b4052d92322718d19a25b3c0af5f22832da3b1aaa` |
| traffic.pcap | `e97a0343729f3e437d948fcf919b189e414da167c472c6e0276b9fe96533e3f2` |
| flows.csv | `c835dd66240f4bd74d7da5b3c84c16ce2409c73601ced3a11c6a77eaf95db0d0` |

文件归档位置均在03数据目录：`traffic.pcap`、`flows.csv`、`tcpdump.stdout`／`tcpdump.stderr`／`tcpdump.exit-code`、`live.stdout`／`live.stderr`／`live.exit-code`、`gateway.runtime.txt`／`gateway.exit-status.txt`、`offline.stdout`／`offline.stderr`／`offline.exit-code`、`analysis-inputs.sha256`和`flows.sha256`。当前收到的是用户终端读回结果，不虚构助手直接读取了板端文件。

本轮结论与边界：

- 已通过：LubanCat物理eth0采集Qt与真实STM32网关遥测TCP流量、保存PCAP、单条双向流聚合及观察到closed、实时与同次PCAP离线CSV一致、捕获层丢弃指标均0、用户现场心跳及最终负载状态确认。
- 未通过项：没有本轮新增失败。此前终端会话串口权限障碍已有只读PASS及真实打开串口证据；01窗口已结束、02目录已有等准备问题保留，不倒改为成功采集。
- 未测试／未验证：本轮未点STOP，不补做控制命令；未逐条核对Qt收到的1014行，也未定位后端额外2包、验证严格TCP序列号／重传或常驻服务长期运行。未提供明确退出原因，不将closed认定为一定由用户主动断开触发。
- 未实现且不在本轮扩展：TCP字节流重组、BMS应用层报文解析或从逐包搜索断言消息丢失。没有新加数据库、真实充放电实验或MCU／Qt代码变更。

本次不因实物采集重复运行历史46项CTest，未修改源码。原始PCAP／Qt日志／板端运行文件保留在仓库外；仅两份既有分类文档由用户在当前VM权威工作树review、stage、commit、push。建议提交标题`docs(integration): verify BMS TCP capture and CSV replay`，助手不执行Git写操作。

### 18.15 BMS历史数据存储准备：独立遥测解析（2026-11-04，VM仓库验收通过）

用户在采集收尾后询问数据库是否应放在网关SD卡，随后要求继续。当前目标是为电池遥测建立可持久化的数据入口，不是把netflow流CSV当作电池数据。拟先形成“完整BMS文本行→有类型的结构体→后续SQLite”的链路；数据库、表结构、写入线程和Qt历史查询均尚未实现。

本轮拟交付由用户自行填写的文件：`gateway/bms_telemetry.h`、`gateway/bms_telemetry.c`、`tests/unit/test_bms_telemetry.c`，以及现有`CMakeLists.txt`中的独立静态库与测试注册。暂不链接到生产网关，不改串口、TCP控制、MCU、Qt或分析器源码。Git仍由用户执行。

解析契约：

- 接收借用的指针和长度，不要求输入以NUL结束，不保存指针，不分配内存或执行I/O。输入总上限1024字节，包含调用者提供的可选行尾；允许无行尾、LF或CRLF。
- 以实际遥测中的16个字段为当前格式：按字段名匹配，可换序、允许分隔位置的ASCII空格；拒绝缺失、重复、未知字段、非法控制字节、空值及数值溢出。未来增加字段须显式调整契约和测试，不默默忽略。
- `faults`和`latched`解析为带0x／0X前缀的32位无符号掩码；`temp_est_dC`和`i_ma`为32位有符号整数，其余保留32位无符号原始值。这是网关接入类型约定，不推断固件内部变量类型、所有状态枚举或计数单位。
- 先解析到局部candidate，全部成功才写调用者output；任何失败均不改变原output。保留iv／i_src／i_age／ina等原始信息，解析成功不代表传感器准确、物理量可信、保护生效或数据无丢失。sample／ctrl／gap_max不猜测为时间戳或全局唯一标识。

实际验证与未完成项：

- 2026-11-04，在VM现有HEAD上构建并运行原46项CTest，46/46通过、16.75秒、退出0；本轮没有重新运行板端历史测试。
- 拟提供的头文件、实现及测试以标准输入分别编译到仓库外的临时对象文件，不创建或修改仓库业务文件。GCC 13.3.0、C11、Wall／Wextra／Wpedantic／Werror及ASan／UBSan下，四组测试通过、退出0：完整16字段／非NUL输入／行尾与字段换序，有符号和数值边界，缺失／重复／未知字段及失败不改output，控制字节与1024／1025字节边界。临时产物目录为`/tmp/nfa-bms-telemetry-proposal.E90pzz`，它不是部署包或已提交源码。
- 上述临时验证之后，用户已录入并提交为`ca22417`，当前检查与远程同步、工作区干净；助手在此提交运行构建及完整CTest，47/47通过、13.00秒、退出0。此为VM仓库实际验收，不是ARM64解析验收。没有接入串口回调、设备打开或真实MCU命令。

后续计划：解析验收后在VM用临时数据库验证SQLite参数绑定、事务及读回，再讨论上板。历史板端数据卡挂载为`/media/usb0`、VFAT、noexec／sync，根分区最近用户回传只余214M；不要把数据库或大依赖装进根分区，不格式化现有卡。常驻记录还需显式设计串口生命周期：现有网关仅在Qt接入后开串口且单会话有总预算，直接增加INSERT不会自动得到Qt离线期间的连续记录。未设计或验证前不宣称常驻历史采集完成。

用户实际提交标题为`feat(gateway): add bounded BMS telemetry parser`，新增解析模块的仓库软件验收已完成；下一步为第18.16节的可选SQLite模块，不重复板端实验。

### 18.16 BMS SQLite批量存储（2026-11-04，VM仓库验收通过）

本轮目标：提供`gateway/bms_store.h`、`gateway/bms_store.c`、`tests/unit/test_bms_store.c`及两处CMake插入代码，由用户自行填写；助手只维护本交接文档，不改业务源码或执行Git写操作。独立模块不链接到当前bms_gateway，网络分析主链继续不依赖数据库。

拟定契约：

- 由调用者以`{0}`初始化并独占维护上下文，通过open／write_batch／close管理磁盘数据库和预编译语句，不复制上下文或并发使用；返回SQLite结果码，成功为SQLITE_OK。失败打开时释放句柄，不宣称删除已经创建的文件。
- 使用版本化表名`bms_samples_v1`，保存16个原始遥测字段，以及设备标识、采集会话标识、网关接收Unix微秒和数据库行ID，共20列。接收时间不等于MCU采样时间；sample不加唯一约束，不作为跨重启或跨设备的全局ID。当前不保存原始行，也未实现表结构迁移、去重或Qt历史查询。
- ID限定1～64字节可打印ASCII；数值统一用sqlite3_bind_int64，避免UINT32_MAX经32位有符号绑定变负。字符串使用参数绑定及SQLITE_TRANSIENT复制，不拼接为SQL。
- 每次调用1～128条，一个BEGIN IMMEDIATE／COMMIT事务；失败时检查事务状态并回滚，回滚失败则标记上下文不可继续写。失败不自动重试、不承诺恰好一次；不能从一般I/O失败推断绝无提交。
- 显式采用DELETE回滚日志和FULL同步，不启用WAL；busy_timeout=100只限制锁竞争等待，不是整个磁盘I/O或批次的硬实时上限。当前接口是同步软件模块，不可据此把磁盘写入直接放进控制主循环；异步队列、拥塞／磁盘满策略和常驻生命周期仍待设计。
- CMake拟新增默认OFF的`NFA_ENABLE_BMS_SQLITE`；仅启用时find_package(SQLite3 REQUIRED)，使用SQLite::SQLite3导入目标并注册bms_store_tests。当前47项基础上启用后预期48项，必须等实际仓库配置及测试结果，不提前登记48/48。

实际验证：

- 上轮候选验证时，VM仅安装libsqlite3-0 3.45.1-1ubuntu2.8，开发包和CLI尚缺失。助手只在`/tmp/nfa-bms-sqlite-check.1iRkHu`下载并解包同版本Ubuntu libsqlite3-dev，用其中头文件与系统运行库独立编译候选模块；CMake 3.28.3的SQLite3查找检查通过。该次助手没有安装软件、修改系统配置或创建仓库源码；用户后续安装结果见下条。
- 候选源码经标准输入编译到临时对象文件，GCC 13.3.0、C11、Wall／Wextra／Wpedantic／Werror、ASan／UBSan下四组通过、退出0：20列记录的19个业务／元数据列关闭后逐项读回（含引号字符串、负数、32位边界）；第二条记录触发SQL错误时整批回滚且后续可写；BEGIN及COMMIT两种锁竞争返回BUSY且不留下半批；128条／64字节ID边界、重复sample允许及资源清理。测试只创建并清理自己独立临时目录中的模拟数据库。
- 上述候选验证之后，用户已录入并提交为`87ccb36`；助手在当前VM构建并完整回归，48/48项Debug CTest通过（15.06秒、退出0），包含bms_store_tests。用户实际安装的开发包与CLI均为3.45.1-1ubuntu2.8，不再报告为当前缺失。
- 回滚本身失败后的unusable分支已有防护设计，但未注入验证。ARM64 SQLite、SD卡实际写入、实时设备入库、磁盘满、损坏恢复、掉电耐久或长期性能仍无验收证据。

用户已采用提交标题`feat(gateway): add optional SQLite telemetry storage`。下一步为18.17的离线日志导入，不直接把同步数据库写入接进控制主循环，也不在根分区最近仅余214M的板端安装依赖、写数据库或格式化现有卡。

### 18.17 BMS历史日志导入CLI（2026-11-04，VM验收通过，已提交4607f1f）

用户已填写`gateway/bms_import.c`、`tests/integration/test_bms_import.py`及CMake两处插入，并对bms_store.h补充时间字段注释，不改变表结构。助手只维护本交接文档，不填写仓库业务源码或代执行Git。实际CLI为`bms-import INPUT_LOG NEW_DATABASE DEVICE_ID SESSION_ID`，目标名bms_import、产物名bms-import，仅在NFA_ENABLE_BMS_SQLITE启用时构建；集成测试已放在既有Python查找之后，开启SQLite及可选ML共49项。本步父提交为87ccb36，随后用户已将5个相关文件提交为4607f1f，当前检查与远程同步，不再报告为未提交。

输入与数据边界：

- 只读取已经结束且不再变化的普通日志文件，最多16 MiB，单行总长度最多1024字节（含LF或CRLF）。Linux O_PATH取得节点引用后先fstat确认类型，再经/proc/self/fd读取同一普通文件；拒绝符号链接、目录、FIFO等，不打开串口驱动或建立TCP连接。该工具使用Linux接口，不宣称纯POSIX跨系统可移植。
- 使用固定行缓冲和最多128条待写记录；逐条解析BMS行，允许空行及不以BMS开头的普通ASCII诊断行并计数跳过。非法BMS字段、控制字节、超长行或没有LF结尾的尾行导致整个导入失败，不把可疑半行当成完整样本。运行中检测文件大小、mtime、ctime常规变化并拒绝发布，不将此检查当作防恶意篡改的快照机制。
- 历史日志没有逐条接收时间，received_unix_us=0保留为未知；不得把导入时刻、文件mtime或sample伪造为原采样／接收时间。设备和采集会话标识由用户指定，本版不自动推断硬件唯一ID、追加导入、去重或合并多个数据库。

输出与失败边界：

- 先用mkstemp在最终目标同一目录创建.part.XXXXXX临时数据库，按批事务写入，完整解析且关闭输入和数据库后再用renameat2(RENAME_NOREPLACE)无覆盖发布。现有输出或符号链接不覆盖，文件系统不支持时失败，不退回可覆盖的rename。
- 解析／数据库等业务失败时不发布最终路径，stdout不输出成功统计，stderr报错并给出保留的不完整临时库路径；早前已提交的128条批次可能存在于临时库，不冒充全文件事务。临时文件不自动删除、不自动续传或重试。最终发布之后若stdout输出本身失败，错误明确说明数据库已经发布，不承诺这一类I/O错误仍无最终文件。
- 只有全部完成才输出input_lines、imported_rows、skipped_lines、received_unix_us=0。无覆盖发布是可见性和覆盖保护，不代表掉电耐久；未增加父目录fsync或验证SD卡断电。信号中断可能留下.part文件，不能手工改名伪装成功。

实际验证：

- 原仓库48/48项VM Debug回归通过（15.06秒）。候选C文件经标准输入编译到`/tmp/nfa-bms-import-check.SOYAWY`，当前bms_store／bms_telemetry也重编译到该目录；C11、Wall／Wextra／Wpedantic／Werror、ASan／UBSan下，Python四组通过、退出0。覆盖260条跨批导入、LF／CRLF、跳过与查询、旧输出和链接保护、非法ID／空文件／坏数字／NUL／超长／无LF尾行／128条已提交后失败不发布、1024字节合法边界及目录／FIFO／链接／16MiB超限拒绝。严格ASan退出码及UBSan遇错终止配置下再次通过。
- VM现有历史文件`/home/zcb/bms-tcp-v1.Qz3z3E/receive.txt`为22656字节。助手只复制到上述临时目录的historical-118.log，用候选CLI写入historical-118.sqlite3，手动测试标识为historical-bms／offline-check；输出input_lines=118、imported_rows=118、skipped_lines=0、received_unix_us=0，退出0。只读查询COUNT=118、sample最小286605／最大286839、接收时间最小／最大均0。原文件及副本SHA-256均为`d6c2c741ad5c701d9b424e78de5e3de0430e86e661d24b1eec3e60a592dbaad2`，导入后原文件摘要未变。这是VM回放11月1日保存的历史接收日志，不是重新抓取硬件、板端数据库验收或实际电池充放电。
- 用户随后回传正式构建产物的导入／查询结果，助手在当前工作树重新构建并完整回归：49/49项Debug CTest通过、14.31秒、退出0，bms_import_tests通过（0.15秒）。新源码、测试、两处CMake及时间注释已读回核对，git diff --check通过；没有替用户写源码或执行Git写操作。
- 尚未分别注入验证：输入运行中变化、发布时晚到的竞争者、发布后stdout错误、磁盘满、信号中断、父目录掉电持久性及ARM64／VFAT行为。当前通过的是VM历史文件回放，不是板端实时入库或实际电池充放电。

用户正式导入与助手只读复核结果：

| 核对项 | 实际结果 |
| --- | --- |
| 数据目录 | `/home/zcb/bms-import-v1.ULkGJr`，数据库为bms.sqlite3（16 KiB），原始数据不入Git |
| 保存的运行输出 | import.stdout为69字节，显示input_lines=118、imported_rows=118、skipped_lines=0、received_unix_us=0；import.stderr为0字节 |
| 数据库记录 | COUNT=118，device_id=bms-demo-01，session_id=historical-118-01；全部118条接收时间为0，表示未知 |
| sample范围 | 286605～286839；不据计数步长推断采样时序或零丢失 |
| 前三条读回 | sample依次286605／286607／286609，cell0_mv均3227，temp_est_dC为259／259／260，i_ma均0，state=1、load=0；不作为新硬件状态测量 |
| 结构快速检查 | sqlite3 -readonly执行PRAGMA quick_check返回ok；该检查不是掉电耐久或全部数据真实性保证 |
| 数据库SHA-256 | `01bd58a037e7ed5a2fb6ef57a89889b5c805d63790e6fc3aed7fd702b1796c71` |
| 历史输入SHA-256 | 仍为`d6c2c741ad5c701d9b424e78de5e3de0430e86e661d24b1eec3e60a592dbaad2`，原日志未修改 |

用户这次没有贴出原导入进程退出码，目录中也没有单独退出码记录，不补写该数值；已确认最终库、完整统计、stderr为空和查询一致性。助手本轮独立构建／CTest退出0与只读查询成功，不能替代原进程退出状态。

当前VM构建产物`build/bin/bms-import`的SHA-256为`dc25f14ad29241ff758916fa99e0fe0b13d2ddde323e1e20ddc7aa689d746dc2`；源码gateway/bms_import.c为`440e3f86992f5b178ba8e1733b895ec1deffa2cdbdfef8c34117832cc57b06ed`，集成测试为`a598b4ee6ddedde7f31a4a7f49d3dad64a6664e1890f46860b3a23ce061aa102`。这是未提交工作树与本机构建的指纹，不当作ARM64产物或新增提交号。

用户已提交`feat(gateway): add bounded BMS log importer`（4607f1f），5个相关文件已归档，数据库、历史原日志、构建产物和临时库不入Git。下一阶段为18.18的生命周期解耦准备，不直接再起第二个进程争用串口，也不因本轮回放成功默认启用服务或开展新的硬件动作。

### 18.18 串口／Qt生命周期解耦准备：有界遥测队列（2026-11-04提供，2026-11-05仓库验收）

最新验收：用户已录入并提交为`c0f62a3 add bounded telemetry queue for async storage`；2026-11-05 VM工作区干净、构建无待编译目标，50/50项Debug CTest通过（13.34秒、退出0）。这只完成独立队列，不代表SQLite线程、网关解耦模式或ARM64实时采集已经集成。下文候选提供时的49项和“待录入”属于11月4日历史快照。

代码检查依据：当前bms_gateway.c先等待accept，接入后关闭listener，再打开／配置串口；bms_control_run绑定一个client_fd，客户端结束会话即返回。仅在完整行回调中同步执行SQLite会引入磁盘／锁等待，也不会得到Qt离线期间的数据。本轮不直接重写该入口，不改变旧CLI的无人接入／单客户端单会话／总预算／默认只读和显式控制契约。

后续目标架构（计划，尚未实现）：

- 同一进程的主循环是唯一串口及TCP控制所有者；新增显式模式才允许在没有Qt时采集，初次联调仍有总运行预算，旧命令保持兼容。Qt断开只结束网络会话和未发控制队列，不关闭采集或重放命令；新连接重新初始化会话，存储线程绝不发送UART控制。
- 每条完整遥测由主线程解析并填写接收时间，按值尝试入队；设备及采集会话标识由之后的存储线程上下文稳定持有，不随Qt重连改变。Unix接收时间与队列超时不同：前者应由真实采集填入，后者使用CLOCK_MONOTONIC。
- 一个存储线程独占bms_store及SQLite，通过队列取样、限时批量刷新。SIGINT／SIGTERM处理器仍只置标志，主循环再关闭队列，消费者排空并报告写入结果后join；不能用volatile sig_atomic_t代替线程同步或在信号处理器中操作互斥锁。
- 队列满／锁忙仅是明确拒绝入队，不是自动落盘或隐式丢弃策略；之后的上层必须计数并确定告警／存储故障策略，不宣称零丢失。入队成功只表示内存接受，不等于SQLite提交。磁盘失败、退出预算和Qt慢客户端处理仍待实现与验证。

本轮仅提供由用户填写的gateway/bms_sample_queue.h、gateway/bms_sample_queue.c、tests/unit/test_bms_sample_queue.c及CMake两处插入。拟在NFA_ENABLE_BMS_SQLITE内查找Threads并建立独立库，公开链接bms_telemetry及Threads::Threads；不链接到当前网关或启动存储线程。测试目标单独使用--wrap=pthread_cond_timedwait观察实际等待，不能把包装器放进正式库。当前49项基础上预期50项，必须等用户实际仓库验收后记录。

候选接口与边界：

- bms_sample_t只包含接收Unix微秒和bms_telemetry_t，不保留串口行／字符串／动态对象指针。固定最多256个值槽，初始化选择1～256容量；上下文须{0}初始化，成功后不可复制，初始化／销毁不与使用者并发。
- try_push用pthread_mutex_trylock，不等待队列锁或空间；EBUSY为锁忙、EAGAIN为满、ECANCELED为关闭，均未入队；不覆盖旧数据。其他同步错误停止组件且不自动重试。它是互斥锁保护的队列，不是无锁／wait-free或硬实时保证。
- pop对空队列使用CLOCK_MONOTONIC条件变量和同一次绝对截止时间，参数1～60000ms；用while复查条件，应对无数据唤醒及超时／到达同时发生。ETIMEDOUT及关闭取空的ECANCELED不修改输出；不把调度与获取互斥锁当作硬时限。
- close保留旧记录、广播唤醒等待者，后续push拒绝；pop仍按FIFO排空后返回ECANCELED。destroy要求先close、消费者结束并join且队列已空；不能用destroy的EBUSY检测代替join，不使用pthread_cancel。所有函数直接返回错误码，不把pthread错误放进errno。
- 目标为一个采集生产者／一个存储消费者；本轮没有多生产者／多消费者、实时采集延迟、数据库吞吐或队列零拒绝承诺。

实际验证：

- 4607f1f上的现有仓库49/49项Debug CTest通过（13.63秒、退出0）。源码／旧网关未修改，本轮不在板端重跑历史测试。
- 候选头文件／实现／测试通过标准输入分别编译为仓库外对象，目录`/tmp/nfa-bms-sample-queue-check.Ut2BTE`，GCC 13.3.0、C11、Wall／Wextra／Wpedantic／Werror、pthread及ASan／UBSan。三组输出通过、退出0：按值复制及FIFO环回、容量／锁忙／超时／关闭排空；通过条件等待包装与有界信号量握手，验证实际等待、无数据唤醒后重新等待、数据／关闭唤醒；10000条合成值跨线程按序传递。严格ASan退出码及UBSan遇错终止下连续重复50轮通过。
- 并发测试让步重试仅用于验证全部合成记录的顺序，不是生产者实际运行的重试／背压策略。未执行TSan、同步错误注入、实际墙钟调整、存储线程、Qt断连后继续入库、ARM64或真实串口实验，不把这些功能写成完成。

队列录入及第50项仓库验收现已完成。下一步先连接存储线程及临时数据库软件验证（18.19），再接新增运行模式及PTY重连测试，最后单独安排有界板端联调；不得直接运行第二个串口进程、启用常驻systemd或自动发送MCU命令。原建议标题为`feat(gateway): add bounded telemetry queue for async storage`，用户实际提交标题以上述c0f62a3为准。

### 18.19 SQLite专属消费者线程（2026-11-05，候选验证通过，待用户录入）

本轮有界目标：将已有有界队列和同步bms_store连接为独立线程组件，SQLite句柄仅在线程内部创建／使用／关闭；不改变旧bms_gateway入口或其链接关系，不打开串口，不发送TCP／UART控制，不执行Git写操作。助手只维护本交接文档，用户填写gateway/bms_store_worker.h、gateway/bms_store_worker.c、tests/unit/test_bms_store_worker.c及CMake两处注册。

候选接口与所有权：

- init只校验／复制配置，不创建线程或打开文件。路径最多1023字节，设备／采集会话ID为1～64字节可打印ASCII；外部配置字符串只需保持至init返回。队列及worker对象须保持至pthread_join结束，初始化后不复制，且只启动一次、只有一个队列消费者。
- pthread_create调用run入口；线程打开数据库及准备语句后发布READY。状态通过C11 atomic acquire／release读取，不并发读取普通result字段；详细计数与错误仅在join后检查。后续网关只有观察到READY才允许开始新的串口采集模式，当前尚未接入该模式。
- batch_size为1～128、flush_interval_ms为1～60000，推荐128条或1000ms。定时依据本批首条出队后的CLOCK_MONOTONIC截止时间；每次成功取样仍检查原截止时间，不因持续输入而滑动延期。它只触发提交尝试，不保证端到端持久化时延或磁盘I/O硬上限。
- 入队成功不等于提交成功。result分别记录popped_rows、committed_rows、committed_batches、SQLite码及系统／队列码。正常退出由主线程停止生产、close队列，消费者排空并提交尾批，再join、检查结果、destroy队列；信号处理器仍只置标志，不用pthread_cancel。
- 打开／写入／时钟或队列失败时关闭队列，停止接纳并排空其余内存值，统计到popped_rows但不写入／重试。popped_rows减committed_rows为未确认提交数，包含失败批次和随后丢弃值；一般I/O故障可能存在提交歧义，不将其声称为确定未落盘或恰好一次。该失败策略不是自动MCU停机／保护动作，后续主循环必须单独上报存储失败与入队拒绝。
- 首次采集接收Unix时间仍由之后的主循环填写；消费者沿用样本值，不改成写库时刻。队列容量仍最多256，批次最多128；不新增数据库轮转／配额、恢复补传、故障时保留队列、掉电承诺或后台服务。

实际验证与边界：

- 当前实际仓库c0f62a3：50/50项Debug CTest通过（13.34秒、退出0）。源码、CMake与现有网关均未由助手改写。
- 候选头／实现／测试通过标准输入编译为仓库外对象，目录`/tmp/nfa-bms-store-worker-check.kvwRCY`；GCC 13.3.0、C11、Wall／Wextra／Wpedantic／Werror、pthread及ASan／UBSan。五组通过：131条按128＋3提交、复制配置／FIFO／字段读回；线程READY及队列尚未关闭时的定时可见提交；预填队列的持续可用样本不会无限延期；触发器制造批次失败后回滚、关闭／排空、不重试；数据库路径为目录时打开失败、没有提交且清队列。
- 测试目标单独用--wrap=bms_sample_queue_pop，在预填队列成功取样后增加25ms处理成本；正式库不包含包装器。将截止时间故意改成逐样本滑动的仓库外缺陷变体时，持续输入检查按预期失败（退出1）；这不是实际仓库出现的新故障。
- 最终候选在泄漏检查／UBSan遇错终止设置下连续50轮通过。不把这登记成实际仓库第51项。未执行TSan、pthread_create失败注入、真实满盘／介质故障／掉电、SD卡、ARM64或真实串口实验，也没有Qt离线持续入库结果。

用户录入后先运行bms_store_worker_tests，再完整回归；当前配置预计51项，须等实际输出验收。建议通过后提交标题：`feat(gateway): add asynchronous SQLite storage worker`。随后才设计显式新网关模式、生产者背压统计和PTY连接／断开验证，不直接以现有单会话入口宣称常驻采集完成。

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

当前推进（2026-11-05，SQLite线程准备）：队列已由用户提交为c0f62a3，VM 50/50项Debug CTest通过（13.34秒），文档更新前工作区干净。新消费者线程候选在仓库外严格编译及ASan／UBSan五组、50轮重复测试通过，由用户填写源码／测试／CMake；仓库51项及ARM64／实时入库仍待验证。助手只更新本交接文档，不改旧网关或硬件，也不执行Git写操作，详见18.19。以下为此前阶段快照。

当前推进（2026-11-04，生命周期解耦准备）：用户已提交4607f1f，VM 49/49项Debug CTest复核通过（13.63秒），文档更新前工作区干净。拟由用户填写独立有界遥测队列及并发单元测试，为后续单串口主循环＋SQLite专属线程做准备；候选代码在仓库外ASan／UBSan及50轮重复测试通过，仓库第50项、存储线程及新运行模式均未实现／验收。当前仍是原有单会话网关，助手只改本交接文档，不动源码、设备或Git，详见18.18。以下为此前阶段快照。

当前收尾（2026-11-04，历史日志导入）：用户已录入CLI与测试，父HEAD为`87ccb36`，5个相关文件待提交；VM 49/49项Debug CTest复核通过（14.31秒）。正式目录`/home/zcb/bms-import-v1.ULkGJr`读回118条、sample范围286605～286839、全部时间未知为0、quick_check=ok，stdout完整且stderr为空，原日志摘要未变；原导入进程退出码未回传，不补写。助手只更新交接文档，不改业务源码、不重跑硬件或执行Git写操作，完整指纹与边界见18.17。下一步由用户review／提交，再设计串口采集与Qt连接解耦；ARM64实时入库、SD部署及掉电等仍待验证。以下为此前阶段快照。

当前推进（2026-11-04，SQLite准备）：解析模块已提交`ca22417`且VM 47/47项Debug CTest通过（13.00秒），文档中上一轮“待录入”已更新为实际仓库验收。接着提供可选、独立的SQLite存储模块及事务软件测试，由用户录入；候选代码在仓库外ASan／UBSan测试通过，仓库48项、ARM64及SD卡数据库均待验证。助手没有安装系统包、填写业务源码、改网关／MCU／Qt运行链、打开硬件或执行Git写操作，详见18.16。以下记录保留为此前阶段快照。

当前推进（2026-11-04）：采集收尾已提交为`00aafac`，VM原46/46项CTest再次通过（16.75秒）。现在提供由用户录入的纯BMS遥测解析模块、单元测试和CMake注册，为后续SQLite做准备；候选代码仅在仓库外完成独立编译与ASan／UBSan验证，仓库47项集成验收及ARM64解析均待实际结果。本轮助手只维护本交接文档，不填写业务源码、改网关运行行为、安装数据库、打开硬件或执行Git写操作，详见18.15。以下“最新收尾”为此前采集或历史阶段快照，不替代本条任务。

最新收尾（2026-11-04）：真实BMS遥测TCP会话已完成“物理eth0采集→PCAP→离线完整流CSV”闭环，03数据目录为`/media/usb0/netflow-analyzer-data/bms-tcp-a66a6ef-20261104-03`。tcpdump、实时分析器及离线处理均对应2041个完整包，1条TCP流、观察到closed，Windows→网关1021包／61272 B、反向1020包／233958 B；CSV读回身份、状态和计数核对PASS。实时后端received=2043、两个drop为0，额外2包未定位，不当作网络丢包。用户确认本轮未点击STOP，三阶段心跳正常、未观察到复位、最终负载关闭。当前VM源版本`a66a6ef67f1166794d9678f40d7b320024d90f04`、分支feature/flow-features；运行产物SHA、实际命令和日志清单见18.14.6。只有两份已有分类文档待用户review／commit／push，源码未改、未重跑历史测试，不提交原始PCAP／日志、不默认继续控制、数据库或真实充放电实验。建议提交`docs(integration): verify BMS TCP capture and CSV replay`。下面11月1日及10月段落保留为历史快照，其中“最新”和“下一步”均指当时状态。

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
