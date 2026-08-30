# LubanCat-2N多流与长稳基线

最后更新：2026-08-30（Asia/Shanghai）

本文补充LubanCat-2N上的流表容量边界、128流短时性能和10分钟长时间稳定性数据。测试继续使用ARM64原生Release程序、真实`eth0`、内核BPF过滤，以及VMware虚拟机产生的外部UDP流量。

## 1. 测试目标

第一组单流基线已经证明程序在约9 Kpps时能够处理20万包且零drop。本轮进一步回答：

- 固定256槽流表满载后是否能够继续运行；
- 128条活跃流是否显著增加每包CPU成本；
- 处理540万包后RSS、PPS、流数量和drop是否保持稳定；
- 目标进程CPU与整机软中断是否接近饱和。

UDP发生器使用128或300个同时保持打开的socket。每个socket由Linux分配不同的临时源端口，因此即使VMware NAT改写源IP，目标板仍会看到不同的UDP五元组。

## 2. 300流容量边界

发生器创建300个不同源端口，每条流发送一个12字节以内的小包；分析器使用：

```text
--count 300
--filter "udp and dst host 192.168.1.102 and dst port 9999"
```

结果：

```text
Total packets: 300
Processing results: complete=256 truncated=0 malformed=0 unsupported=0 flow_rejected=44
Expired flows: 0
Capture received packets: 304
Capture dropped packets: 0
Interface dropped packets: 0
Flow summary: 256 flow(s)
Maximum resident set size: 1760 KiB
Exit status: 0
```

结果精确满足：

```text
300 = 256 complete + 44 flow_rejected
```

这证明流表满载后程序不会因`ENOSPC`退出，已经存在的256条流继续保留，后续新流被归入`flow_rejected`。后端接收比应用计数多4没有伴随分类缺口或drop，继续按libpcap后端与应用交付边界解释。

## 3. 128流、20万包短时性能

选择128条流使流表占用率保持50%，既能覆盖多键查找，又不触发容量拒绝。发生器按轮询方式把20万包分给128个socket，并以约9000 PPS发送。

结果：

```text
Total packets: 200000
Processing results: complete=200000 truncated=0 malformed=0 unsupported=0 flow_rejected=0
Expired flows: 0
Capture received packets: 200000
Capture dropped packets: 0
Interface dropped packets: 0
Flow summary: 128 flow(s)
User time: 1.29 s
System time: 0.05 s
Elapsed time: 26.79 s
Maximum RSS: 1764 KiB
Exit status: 0
```

中间完整周期稳定在约8991～9003 PPS、4.316～4.321 Mbps，并持续显示：

```text
active_flows=128/256
flow_table_usage=50.00%
expired_flows=0
```

每包进程CPU成本为：

```text
(1.29 + 0.05)秒 / 200000包
≈ 6.70微秒/包
```

它与单流ICMP基线的约6.95微秒/包接近。由于UDP和ICMP解析路径、帧长不同，不能把差异解释为多流更快；可以得出的结论是，在128流和50%占用率下没有测出明显的哈希查找退化。

每个捕获帧为60字节：14字节Ethernet头、20字节IPv4头、8字节UDP头、12字节发生器载荷和6字节最小帧填充。libpcap报告的长度不包含物理层前导码、帧间隙，通常也不包含4字节FCS，因此不能把该Mbps直接当作完整链路占用。

## 4. 128流、540万包、10分钟长稳

发生器参数：

```text
FLOW_COUNT=128
PACKET_COUNT=5400000
TARGET_PPS=9000
BATCH_SIZE=90
```

理论活跃时间为：

```text
5400000 / 9000 = 600秒
```

分析器先启动，再启动监控和虚拟机发生器，因此整段墙钟时间还包含约16.91秒的前置静默。

最终结果：

```text
Total packets: 5400000
Processing results: complete=5400000 truncated=0 malformed=0 unsupported=0 flow_rejected=0
Expired flows: 0
Capture received packets: 5400000
Capture dropped packets: 0
Interface dropped packets: 0
Flow summary: 128 flow(s)
User time: 33.16 s
System time: 0.56 s
Elapsed time: 616.91 s
Maximum RSS: 1764 KiB
Exit status: 0
```

每包CPU成本：

```text
(33.16 + 0.56)秒 / 5400000包
≈ 6.24微秒/包
```

整段单核CPU百分比约为：

```text
33.72 / 616.91 × 100%
≈ 5.47%
```

GNU `time`按整数显示为5%。该百分比采用单核刻度，若只用于表达四核总容量占比，约为1.37%。

周期报告开头有三个零流量区间，反映人工切换终端的前置静默。进入稳定负载后，尾部五个周期仍保持8991～9003 PPS、128条活跃流、50%占用率、零过期和零拒绝，没有随运行时间下降。

## 5. RSS趋势

`pidstat -r -p PID 5 120`每5秒采样一次，共覆盖600秒。首五个样本、末四个样本和平均值均为：

```text
VSZ=4424 KiB
RSS=564 KiB
%MEM=0.03
```

平均轻微缺页为0.01次/秒，严重缺页为0。RSS从首样本到尾样本完全相同，没有出现按包数或时间单向增长。

`pidstat`的564 KiB是周期采样值；GNU `time`的1764 KiB是进程整个生命周期内的峰值。启动阶段的短暂峰值可能发生在5秒采样点之间，因此两者并不矛盾，也不能互相替代。

本轮只能证明10分钟、540万包范围内没有可见泄漏，不能替代数小时或数天的生产级浸泡测试。

## 6. 整机CPU和软中断

10分钟`mpstat -P ALL 5 120`平均值：

| CPU | `%usr` | `%sys` | `%soft` | `%idle` |
|---|---:|---:|---:|---:|
| all | 4.11 | 3.05 | 1.69 | 90.97 |
| CPU0 | 3.62 | 2.60 | 8.00 | 85.71 |
| CPU1 | 4.44 | 2.82 | 0.15 | 92.37 |
| CPU2 | 4.63 | 2.81 | 0.15 | 92.19 |
| CPU3 | 3.62 | 3.83 | 0.09 | 92.25 |

网络软中断仍主要集中在CPU0，但CPU0平均空闲85.71%，整机平均空闲90.97%，没有软中断或用户态CPU饱和迹象。

短时ICMP整机补测曾得到更高的CPU0软中断比例。两个场景的协议、收发方向、内核ICMP处理、网卡批处理和采样窗口不同，不能据此断言软中断成本随时间下降。

测试前累计快照为：

```text
NET_TX=69960
NET_RX=3003872
```

测试后的`softirqs-10m-after.txt`没有生成，因此不能计算严格覆盖本轮的累计增量。测试后再读取`/proc/softirqs`会混入间隔期间的SSH和其他网络活动，不能补作精确终值。由于`mpstat`已经连续覆盖600秒，本轮不为补一个累计次数而重跑540万包。

## 7. 阶段结论

当前证据支持：

- 固定256槽流表满载后的行为明确且可恢复：新流被拒绝，主循环继续；
- 128条活跃流、50%占用率没有造成可见的每包CPU退化；
- 540万包全部完成，应用分类、后端接收和两个drop字段完全闭合；
- 10分钟内RSS固定、无严重缺页、PPS稳定、流数量稳定；
- 目标进程和整机CPU均有较大余量，CPU0软中断尚未接近饱和；
- 当前仍不需要把实验性线程流水线接入正式主链。

下一阶段不应继续重复同一种ICMP或UDP压测。应根据产品方向在以下工作中选择：

1. 为固定流表增加可观测的探测长度、负载因子或驱逐策略；
2. 增加TCP状态跟踪和流重组，为DNS/HTTP等应用层解析建立基础；
3. 完成非root抓包权限和更接近部署环境的服务化运行；
4. 在需求提高到更高PPS时再测IRQ亲和性、RPS和更高性能捕获后端。
