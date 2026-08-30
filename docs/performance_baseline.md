# LubanCat-2N性能基线

最后更新：2026-08-30（Asia/Shanghai）

本文记录Netflow Analyzer在LubanCat-2N上的第一组可重复性能测量。测试使用ARM64原生Release构建、真实`eth0`、libpcap BPF过滤和来自VMware虚拟机的外部ICMP流量。

本轮结论是：在单条双向ICMP流、约9 Kpps和约7 Mbps的已测范围内，程序处理20万包时零应用拒绝、零捕获丢包，进程CPU成本约6.95微秒/包，最大RSS约1.64 MiB。当前没有证据要求把正式链路改成多线程。

## 1. 测量环境

| 项目 | 实际值 |
|---|---|
| 目标板 | LubanCat-2N，AArch64 |
| 程序构建 | ARM64原生Release，`BUILD_TESTING=OFF` |
| 程序路径 | `/home/cat/build/netflow-analyzer-release/bin/netflow-analyzer` |
| 捕获接口 | `eth0` |
| 流量发生器 | x86_64 VMware虚拟机 |
| 目标地址 | `192.168.1.102` |
| BPF | `icmp and host 192.168.1.102` |
| 进程资源工具 | GNU `/usr/bin/time -v` |
| 运行边界 | GNU `timeout`发送`SIGINT`，程序沿正常路径退出 |

开发板默认没有独立的GNU `time`程序，使用以下命令安装测量工具：

```bash
sudo apt update
sudo apt install -y time
```

该软件包只属于开发测量环境，不是正式程序依赖。

## 2. 为什么采用这种测量方式

### 2.1 使用Release而不是Debug

Debug构建适合断言、调试和CTest，但编译优化、调试信息和控制流可能改变CPU与内存数据。性能基线使用：

```bash
cmake \
    -S /media/usb0/Workspace/netflow-analyzer \
    -B /home/cat/build/netflow-analyzer-release \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF

cmake --build \
    /home/cat/build/netflow-analyzer-release \
    -j2
```

### 2.2 从另一台机器产生流量

如果在开发板本机同时运行流量生成器，测得的系统CPU会混入`ping`开销。由虚拟机发送ICMP，可以把发生器开销移出目标板。

### 2.3 同时保留程序输出和资源数据

基础命令结构为：

```bash
sudo /usr/bin/time -v \
    timeout --preserve-status --signal=INT 45s \
    stdbuf -oL -eL \
    程序及参数 \
    2>&1 | tee 结果文件
```

各部分作用：

- `/usr/bin/time -v`记录用户态CPU、内核态CPU、经过时间、CPU百分比和最大RSS；
- `timeout --signal=INT`在上限到达时模拟Ctrl+C，而不是直接杀死程序；
- `--preserve-status`保留程序正常退出状态；
- `stdbuf -oL -eL`让经由管道的标准输出和标准错误按行刷新；
- `2>&1`把标准错误合并到标准输出，因为GNU `time`把资源数据写到标准错误；
- `tee`同时显示并保存完整结果；
- `set -o pipefail`防止前面的测量程序失败却被最后成功的`tee`掩盖。

## 3. 四档测量结果

| 场景 | 应用总包数 | 活跃区间PPS | 活跃区间Mbps | User CPU | System CPU | 经过时间 | 整段平均CPU | 最大RSS | 后端接收 | 捕获丢包／接口丢包 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| BPF无匹配空闲 | 0 | 0 | 0 | 0.00 s | 0.01 s | 22.04 s | 约0.045% | 1764 KiB | 2 | 0／0 |
| 单ping低负载 | 2000 | 约92 | 约0.072 | 0.00 s | 0.03 s | 37.05 s | 约0.081% | 1640 KiB | 2002 | 0／0 |
| 单ping中负载 | 40000 | 1777～1822 | 1.39～1.43 | 0.27 s | 0.02 s | 45.04 s | 约0.644% | 1740 KiB | 40002 | 0／0 |
| 5个并发ping | 200000 | 8786～9067 | 6.89～7.11 | 1.27 s | 0.12 s | 32.73 s | 约4.247% | 1680 KiB | 200002 | 0／0 |

所有非空场景均满足：

```text
truncated=0
malformed=0
unsupported=0
flow_rejected=0
```

高负载场景在达到`--count 200000`后自行结束，没有等待60秒安全超时。

## 4. CPU和每包成本计算

GNU `time`的整段平均CPU计算为：

```text
(User CPU秒 + System CPU秒) / 墙钟秒 × 100%
```

20万包场景：

```text
(1.27 + 0.12) / 32.73 × 100%
≈ 4.247%
```

整体每包CPU成本：

```text
1.39秒 / 200000包
≈ 6.95微秒/包
```

4万包场景为：

```text
0.29秒 / 40000包
≈ 7.25微秒/包
```

两个负载级别的每包成本接近，说明当前范围内处理成本近似线性，没有出现明显锁竞争、内存增长或容量拐点。

该数字是整个进程的平均成本，包含采集调用、解析、流表更新、周期统计和少量输出，不等于某个解析函数的微基准耗时。

## 5. 内存结果

四次最大RSS位于1640～1764 KiB，即约1.60～1.72 MiB。高负载运行没有显示随包数增长的RSS上升。

`Maximum resident set size`是每个独立进程运行期间的峰值，不是持续采样曲线。不同运行之间相差几十或一百多KiB可能来自动态加载、页错误和统计时机，不能把较小的一次解释为程序主动释放了固定内存，也不能仅凭短测证明长期绝无泄漏。

## 6. 周期报告抖动

空闲时报告间隔稳定在约5.005秒。中负载曾观察到5.220～5.377秒，高负载为5.053～5.134秒。

当前主循环在两类位置检查单调时钟：

- 处理完一个数据包之后；
- `poll()`返回就绪或超时之后。

它没有独立定时线程，因此报告发生在主循环重新取得控制权时，而不是硬实时中断。libpcap、内核捕获缓冲、虚拟化网络和发生器调度都可能形成批量交付或唤醒抖动。

速率公式使用输出中的真实`interval`作为分母，所以即使报告不是精确5.000秒，PPS和Mbps仍然保持一致语义。只有未来需求明确要求严格周期边界时，才评估`timerfd`或新的事件调度结构。

## 7. 捕获后端固定多2包

四档结果分别出现：

```text
应用包数       捕获后端接收
0              2
2000           2002
40000          40002
200000         200002
```

应用计数只统计`pcap_next_ex()`实际交付并进入处理链的数据包，`pcap_stats()`来自更底层的累计统计，其范围依赖平台和捕获后端。固定的`+2`没有伴随应用分类缺口或drop增长，因此当前不视为数据处理Bug。

## 8. 管道输出缓冲问题

第一次高负载测量使用：

```bash
程序 2>&1 | tee 文件
```

程序的标准输出连接管道后，C标准库可能从终端按行缓冲切换为块缓冲。终端暂时看不到输出不表示程序卡死。人工Ctrl+C会同时影响管道中的分析器、`timeout`、`time`和`tee`，可能导致日志在刷新前丢失。

最终使用：

```bash
stdbuf -oL -eL 程序
```

使输出按行进入`tee`。这只改变标准I/O缓冲策略，不改变抓包或解析算法。

## 9. 20万包场景复现命令

开发板：

```bash
export NFA_BOARD_RELEASE_BIN=/home/cat/build/netflow-analyzer-release/bin/netflow-analyzer
export NFA_PERF_DIR=/home/cat/perf/netflow-analyzer

set -o pipefail

sudo /usr/bin/time -v \
    timeout \
        --preserve-status \
        --signal=INT \
        --kill-after=5s \
        60s \
    stdbuf -oL -eL \
    "$NFA_BOARD_RELEASE_BIN" \
        --interface eth0 \
        --count 200000 \
        --filter "icmp and host 192.168.1.102" \
    2>&1 | tee "$NFA_PERF_DIR/icmp-parallel-200k.txt"
```

虚拟机使用5个并发进程：

```bash
sudo -v
sleep 3

for run_id in 1 2 3 4 5
do
    sudo ping \
        -q \
        -n \
        -i 0.001 \
        -c 20000 \
        192.168.1.102 \
        > "/tmp/nfa-ping-high-${run_id}.txt" &
done

wait
```

## 10. 结论和边界

当前证据支持继续保持单线程：

- 约9 Kpps时进程整段平均CPU约4.25%；
- 20万包全部完成，应用拒绝和两个drop字段均为0；
- 每包CPU成本从约7.25微秒稳定到约6.95微秒；
- RSS没有随包数出现增长趋势；
- 引入线程会增加包数据复制、队列、锁、背压和关闭顺序复杂度。

本轮不能证明：

- 千兆线速或最小包极限PPS能力；
- 多流、高哈希碰撞或流表满载时的实时性能；
- TCP/UDP混合负载及应用层解析成本；
- 长时间运行的内存稳定性；
- 网卡软中断等未计入目标进程CPU时间的整机成本。

下一轮性能工作应优先补充整机CPU/软中断采样、长时间运行和多流负载。只有数据证明单线程成为瓶颈后，才重新评估`labs/thread_pipeline`进入正式程序。
