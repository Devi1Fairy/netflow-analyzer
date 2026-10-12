# 招聘会演示：设备流量采集、双向流与特征导出

最后更新：2026-10-12。目标为2026-10-14下午招聘会。

## 1. 展示范围与当前结论

本轮收敛到已有的Linux C11网络流分析主链：设备通信、libpcap采集、协议解析、双向流聚合、TCP旁路状态、运行指标和特征CSV。暂缓DNS后续解析、LLM对照、模型上板、GUI、云端和正式多线程流水线，不把规划功能写成已实现能力。

当前已完成新版ARM64产物的物理网卡ICMP与特征导出手工验收，以及10秒静默SIGTERM收尾检查。首次TCP采集未通过验收，完整TCP设备消息演示仍待重测；历史板端TCP验收不能替代这次新产物的验证。

现有AI数据处理、PyTorch基线与误报诊断保留为实验模块。独立正常TCP诊断误报率为30.8333%，不能包装成低误报异常预警；见[tcp_normal_diagnostic.md](tcp_normal_diagnostic.md)。

## 2. 分清源码、程序与服务

| 位置 | 2026-10-11检查结果／用途 |
|---|---|
| 虚拟机源码 | `/home/zcb/workspace/netflow-analyzer`，`feature/flow-features`，构建源码提交为`7bce180` |
| 虚拟机演示构建 | `/home/zcb/build/netflow-analyzer-jobfair-arm64-7bce180`，独立ARM64 Release构建目录 |
| 开发板源码 | `/home/cat/workspace/netflow-analyzer`，用户报告为`feature/nonroot-service`、`7526fcc`，检查时无未提交改动；本轮没有更新此工作树 |
| 开发板演示程序 | 按本轮部署步骤放在`/home/cat/bin/nfa-demo-7bce180/netflow-analyzer`，与旧程序隔离 |
| 开发板原安装程序 | `/usr/local/bin/netflow-analyzer`，帮助中尚无`--feature-csv`，本轮不覆盖 |
| systemd服务 | 用户报告`inactive`、`disabled`；本轮保持原状，不因手工抓包而宣称新版已完成非root服务验收 |

板端上传前报告根分区7.0 GiB、已用6.0 GiB、可用748 MiB；内存`available`为758 MiB，无swap。这里只部署约61 KiB的C分析器和小型结果文件，不安装PyTorch或复制SDK／完整sysroot。历史容量与当前容量不同，后续仍应检查`df -hT /`。

虚拟机中的新程序与板端旧源码可以暂时并存，但必须按路径和指纹说明运行的是哪一个，不能只用仍为`0.2.0`的版本字符串判断功能是否相同。

## 3. 当前交叉产物与本机测试

本轮助手在既有官方SDK与libpcap overlay环境中构建，没有修改C源码、CMake源文件或测试。

| 项目 | 已检查的虚拟机结果 |
|---|---|
| 编译器 | 官方SDK GCC 9.3.0，AArch64 |
| 构建 | Release，`BUILD_TESTING=OFF`，23个构建步骤成功，未见编译警告 |
| 文件 | `bin/netflow-analyzer`，62,504字节，ARM aarch64 ELF |
| 动态加载器 | `/lib/ld-linux-aarch64.so.1` |
| 直接动态依赖 | `libpcap.so.0.8`、`libc.so.6` |
| 本程序的GLIBC版本需求 | `GLIBC_2.17`；依赖库本身的需求仍以板端环境为准 |
| RPATH／RUNPATH | 设置`CMAKE_SKIP_RPATH=ON`后检查动态段，没有构建机库搜索路径 |
| SHA-256 | `47ade7acd3e5b30029f9264b4052d92322718d19a25b3c0af5f22832da3b1aaa` |
| 本机回归 | 2026-10-11再次构建成功，x86_64 Debug启用可选ML的37/37项CTest通过，7.92秒 |

完整交叉环境解释见[cross_compilation.md](cross_compilation.md)。本次独立目录的可复现命令如下，在虚拟机执行：

```bash
(
    set -eu
    NFA_JOBFAIR_TOOLCHAIN=/home/zcb/sdk/lubancat-rk356x-20260623/prebuilts/gcc/linux-x86/aarch64/gcc-buildroot-9.3.0-2020.03-x86_64_aarch64-rockchip-linux-gnu
    export LD_LIBRARY_PATH="$NFA_JOBFAIR_TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export PKG_CONFIG_SYSROOT_DIR=/home/zcb/sysroots/lubancat2n-pcap
    export PKG_CONFIG_LIBDIR=/home/zcb/sysroots/lubancat2n-pcap/usr/lib/aarch64-linux-gnu/pkgconfig
    unset PKG_CONFIG_PATH
    cmake -S /home/zcb/workspace/netflow-analyzer -B /home/zcb/build/netflow-analyzer-jobfair-arm64-7bce180 -G Ninja -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER="$NFA_JOBFAIR_TOOLCHAIN/bin/aarch64-rockchip-linux-gnu-gcc" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_SKIP_RPATH=ON
    cmake --build /home/zcb/build/netflow-analyzer-jobfair-arm64-7bce180 -j2
)
```

外层括号创建子shell，工具链环境只对其中的命令生效；`set -e`遇到失败停止，`-u`拒绝误用未定义变量。`BUILD_TESTING=OFF`仅表示这份交叉产物不构建测试，不能据此说37项测试已经在ARM64运行。37项来自另一个x86_64 Debug测试树。

部署时先在板端用`mkdir`创建新的演示子目录，已存在就先检查，不直接覆盖；虚拟机只用`scp`上传这一个程序。板端再用`sha256sum`、`ldd`和`--help`检查传输、动态加载与功能。用户已提供后续手工运行结果，但尚未贴出板端SHA-256、`ldd`、帮助和退出码原文，因此本记录不独立确认这些输出或完整的产物身份链。

## 4. 已完成：物理网卡ICMP与特征导出

证据来源：用户提供的LubanCat-2N实际终端输出，助手未通过SSH重跑。结果目录：

```text
/home/cat/nfa-demo-icmp.1yiMYo
```

用户按此前步骤在`eth0`上捕获指向／来自`192.168.1.102`的ICMP流量，虚拟机发送两次ping。采集配置为`--count 4`、`--feature-csv`和45秒安全超时，不启用后台服务。

实际摘要：

```text
Total packets: 4
Processing results: complete=4 truncated=0 malformed=0 unsupported=0 flow_rejected=0
Capture dropped packets: 0
Interface dropped packets: 0
Flow summary: 1 flow(s)
Flow 1: protocol=ICMP tcp_state=not-applicable endpoint_a=192.168.1.100:0 endpoint_b=192.168.1.102:0 a_to_b_packets=2 a_to_b_captured_bytes=196 a_to_b_wire_bytes=196 b_to_a_packets=2 b_to_a_captured_bytes=196 b_to_a_wire_bytes=196 first_seen=1791722993.201081 last_seen=1791722994.202787
```

`features.csv`实际为2行，内容为：

```csv
schema_version,protocol,duration_microseconds,total_packet_count,total_captured_byte_count,total_wire_byte_count,mean_captured_bytes_per_packet,mean_wire_bytes_per_packet,packet_count_imbalance_ratio,wire_byte_count_imbalance_ratio,tcp_state_applicable,tcp_phase,tcp_handshake_completed
flow_features_v1,1,1001706,4,392,392,98,98,0,0,0,not-applicable,0
```

助手已独立核对用户提供数值的算术关系：

| 字段 | 为什么是这个值 |
|---|---|
| `protocol=1` | IPv4 ICMP协议号，不是第1条流的编号 |
| `duration_microseconds=1001706` | `(1791722994.202787 - 1791722993.201081) × 1000000` |
| `total_packet_count=4` | 两方向各2包之和 |
| 两种总字节数均为392 | 两方向各196字节之和；本次两种计量相同不代表所有抓包都不截断 |
| 两种平均包长均为98 | `392 / 4`；这里按捕获／线路记录的帧长度统计，不只是ICMP载荷 |
| 两个不平衡度均为0 | 两方向包数和线路字节数分别相等 |
| TCP适用／握手字段均为0，阶段`not-applicable` | ICMP不拥有TCP生命周期，不表示TCP握手失败 |

结论：本次物理网卡的采集、完整解析、双向流汇总及特征导出功能通过手工验收。它不是性能测试、模型效果测试或新版systemd验收；不能把本轮两个drop为0推广为任意负载零丢包。`Capture received packets`和进程退出码本轮未贴出，不补写猜测值。板端实际观察到的对端是`192.168.1.100`，不是必须等于虚拟机内部地址；本轮没有重新审计NAT规则。

## 5. 下一项待验证：一条TCP设备消息连接

目标是观察握手、请求／响应和正常关闭，并生成一条TCP特征。使用临时Python标准库socket发生器，不使用HTTP、DNS、真实恶意流量或LLM。发生器不是分析器的新功能，C程序也不会解析下面的应用消息文本。

需要三个独立终端：开发板A抓包，开发板B临时服务端，虚拟机C客户端。先把三个终端和各自命令准备好，再按A、B、C顺序执行。A抓包尚未返回提示符时就切换到B启动服务端；B打印`ready`后立即到C连接，不能等A采集结束后才执行B或C。C必须在虚拟机运行；开发板自己连接自己的地址通常走`lo`，不会出现在本次`eth0`采集中。

### 5.0 已完成：静默SIGTERM收尾对照

首次TCP结果目录为`/home/cat/nfa-demo-tcp.BVQJ81`。用户提供的原始日志有9个约5秒的零包报告，但没有最终汇总；`features.csv`为0字节，排查时没有找到演示程序进程。根分区仍有748 MiB可用，用户确认按过Ctrl+C，退出码变量未保存。没有收到B、C终端的通信输出，不能确认连接是否在采集窗口内发生，也不能从日志缺失反推出具体退出信号。旧目录保留，不覆盖。

随后用户执行10秒、直接重定向到日志文件、不使用管道的静默对照，未启动服务端或客户端。实际结果目录为`/home/cat/nfa-demo-close.Ew7nns`：

```text
收尾检查退出码：0
Runtime metrics: interval=5.006 packets=0 complete=0 truncated=0 malformed=0 unsupported=0 flow_rejected=0 captured_bytes=0 wire_bytes=0 pps=0.00 captured_mbps=0.000000 wire_mbps=0.000000 active_flows=0/256 flow_table_usage=0.00% expired_flows=0 evicted_flows=0
Total packets: 0
Previewed packets: 0
Processing results: complete=0 truncated=0 malformed=0 unsupported=0 flow_rejected=0
Flow table probes: operations=0 inspected_slots=0 average=0.00 maximum=0 saturated=false
Expired flows: 0
Evicted flows: 0
Capture received packets: 5
Capture dropped packets: 0
Interface dropped packets: 0
Flow summary: 0 flow(s)
Feature CSV output: /home/cat/nfa-demo-close.Ew7nns/features.csv
```

`wc -l`实际为1行。用户未贴出该行原文，但当前写出实现及正常关闭结果支持表头已保存；不能把它描述成已有TCP特征数据。`Capture received packets: 5`是后端统计，不等同于分析器处理的匹配TCP包数，本轮不猜测这5个事件的协议或来源。

对照命令如下，不产生测试流量，不按Ctrl+C：

```bash
sudo -v
NFA_CLOSE_BIN=/home/cat/bin/nfa-demo-7bce180/netflow-analyzer
NFA_CLOSE_DIR="$(mktemp -d /home/cat/nfa-demo-close.XXXXXX)"
printf '检查目录：%s\n' "$NFA_CLOSE_DIR"

if sudo timeout --preserve-status --signal=TERM --kill-after=5s 10s \
    "$NFA_CLOSE_BIN" \
    --interface eth0 \
    --count 1000 \
    --filter "tcp and host 192.168.1.102 and port 18080" \
    --feature-csv "$NFA_CLOSE_DIR/features.csv" \
    > "$NFA_CLOSE_DIR/run.txt" 2>&1
then
    NFA_CLOSE_STATUS=0
else
    NFA_CLOSE_STATUS=$?
fi

printf '收尾检查退出码：%s\n' "$NFA_CLOSE_STATUS"
sed -n '1,120p' "$NFA_CLOSE_DIR/run.txt"
wc -l "$NFA_CLOSE_DIR/features.csv"
```

本次只证明该程序在无匹配流量、直接日志输出和定时SIGTERM下能够正常输出汇总并关闭CSV，不证明TCP握手或关闭特征已经通过。普通`tee`配合前台Ctrl+C存在收尾输出丢失风险；此前VM最小管道对照保留了这一风险的证据，但没有重放板端真实故障或确认SIGPIPE。完整问题边界见[问题5.20](problem_log.md#520-tcp演示按ctrlc后缺少最终汇总且特征csv为空)。

### 5.1 开发板A：检查端口并开始抓包

```bash
NFA_TCP_BIN=/home/cat/bin/nfa-demo-7bce180/netflow-analyzer
sha256sum "$NFA_TCP_BIN"
ss -ltn 'sport = :18080'
sudo -v
```

`ss`预期只有表头，没有`LISTEN`记录。如果18080已被占用，先停止并查看，不杀死未知进程。`sudo -v`先完成认证，避免准备通信时才被密码提示打断。预检查通过后再执行下面的采集命令。

```bash
NFA_TCP_DIR="$(mktemp -d /home/cat/nfa-demo-tcp.XXXXXX)"
printf 'TCP结果目录：%s\n' "$NFA_TCP_DIR"

if sudo timeout --preserve-status --signal=TERM --kill-after=5s 90s \
    "$NFA_TCP_BIN" \
    --interface eth0 \
    --count 1000 \
    --filter "tcp and host 192.168.1.102 and port 18080" \
    --feature-csv "$NFA_TCP_DIR/features.csv" \
    > "$NFA_TCP_DIR/run.txt" 2>&1
then
    NFA_TCP_STATUS=0
else
    NFA_TCP_STATUS=$?
fi

printf 'TCP抓包退出码：%s\n' "$NFA_TCP_STATUS"
```

`--count 1000`是有限特征导出要求的包数安全上限，不是本次必须发1000包，也不能用很小的上限截断握手或关闭过程。本次不使用管道，不按Ctrl+C；日志直接写入文件，屏幕暂时没有输出是正常的。A运行期间立即去B、C产生受控流量，再等待90秒期限发送SIGTERM收尾。`if/else`紧接被测命令保存状态，避免后面的检查覆盖`$?`，也能在命令失败时打印状态。`--preserve-status`保留受管理程序的退出状态，所以期限触发后也可能退出0；退出0本身不能代替流和特征检查。正常响应停止时不会等待额外5秒；5秒仅是无法退出时的强制终止宽限。不要把强制终止后的不完整CSV算作通过。

实时流表使用固定30秒空闲超时，当前`--flow-idle-timeout`只能用于离线模式，不能给这条实时命令加它。若通信结束后等待超过约30秒，流可能在5秒扫描时被清理：日志会先输出`Expired flow 1: ... tcp_state=closed`，并把特征写入CSV，最终`Flow summary`则可能为0。这不是丢失特征；检查时必须同时保留过期流行和CSV，不能只看最终流表，不为了保留最终1条流而提前Ctrl+C。

### 5.2 开发板B：只接受一条连接的临时服务端

```bash
python3 - <<'PY'
import socket

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.settimeout(60.0)
    listener.bind(("192.168.1.102", 18080))
    listener.listen(1)
    print("ready: 192.168.1.102:18080; accept one connection", flush=True)

    connection, peer = listener.accept()
    with connection:
        connection.settimeout(5.0)
        with connection.makefile("rb") as reader:
            request = reader.readline(128)
        if request != b"netflow-demo\n":
            raise RuntimeError("unexpected request")
        connection.sendall(b"netflow-demo-ok\n")
        print("request received from:", peer, flush=True)

print("[PASS] one TCP connection handled; server exited")
PY
```

`AF_INET`选择IPv4，`SOCK_STREAM`选择TCP。`bind`指定本机监听地址，`listen`开始监听，`accept`接受一次连接；这里没有无限循环，不会处理完还常驻。`SO_REUSEADDR`方便重复本次实验，不会抢占另一个活动监听者。等待连接最多60秒，已接受连接的操作最多等待5秒。

`makefile("rb")`为socket建立二进制读取视图，`readline(128)`按换行符接收最多128字节，避免把一次TCP `recv()`误当作完整应用消息。读视图关闭后，原socket仍由外层`with connection`管理；`sendall`发送完整响应，离开连接上下文时关闭连接。以上超时是失败保护，发生超时后不要继续复用可能不完整的读取视图。

### 5.3 虚拟机C：发送一条设备消息并读取到EOF

```bash
python3 - <<'PY'
import socket

with socket.create_connection(("192.168.1.102", 18080), timeout=5.0) as connection:
    connection.sendall(b"netflow-demo\n")
    with connection.makefile("rb") as reader:
        response = reader.read(128)
    if response != b"netflow-demo-ok\n":
        raise RuntimeError("unexpected response")
    print("response:", response.decode("ascii").strip())

print("[PASS] TCP response checked; client exited")
PY
```

`create_connection`建立TCP连接；`b"..."`是发送到网络的字节串。这里响应小于128字节，`read(128)`会等到服务端关闭发送方向产生EOF后返回，再由客户端外层上下文关闭连接，便于观察完整关闭。与服务端不同，客户端不调用`bind/listen/accept`。连接失败或超时就停止核对，不在后台自动重试。

### 5.4 开发板A：采集结束后检查

```bash
grep -E \
    'Total packets:|Processing results:|Expired flows:|Expired flow [0-9]+:|Capture dropped packets:|Interface dropped packets:|Flow summary:|Flow [0-9]+:' \
    "$NFA_TCP_DIR/run.txt"
wc -l "$NFA_TCP_DIR/features.csv"
sed -n '1,2p' "$NFA_TCP_DIR/features.csv"
```

预期一条TCP生命周期，日志中的最终流或过期流为`tcp_state=closed`；特征为`protocol=6`、`tcp_state_applicable=1`、`tcp_phase=closed`、`tcp_handshake_completed=1`，文件共2行，两种drop为0。及时收尾时预期`Expired flows: 0`、`Flow summary: 1 flow(s)`；等待过期后则预期`Expired flows: 1`、`Flow summary: 0 flow(s)`，但特征仍只导出一次。包数取实测值，不硬编码为12包；ACK合并、分段和重传等会影响实际包数。所有包处理结果之和应与`Total packets`相等。本次简单受控连接期望全部`complete`，否则保留日志排查。

`closed`是本项目根据TCP标志与方向得出的旁路观察状态，不等同于Linux socket状态，也没有完成ACK序列号验证或TCP字节流重组。

截至本次文档更新，首次TCP采集已有未通过记录，10秒静默收尾对照已通过；上面的无管道TCP重测仍未收到板端结果，不记录推测的包数、目录、退出码、指纹和结果。

## 6. 结束方式与展示边界

- ICMP收到4包即结束，45秒超时是未收到足够包时的保护；原命令未加`--preserve-status`，超时通常返回124。
- TCP服务端处理一条连接后退出，客户端检查响应后退出；抓包最长约95秒进入强制结束，正常SIGTERM响应会更早结束。
- 本轮重测只等待定时SIGTERM收尾，不在普通`tee`管道中Ctrl+C。以后确需边显示边保存，应单独验证`tee -i`和停止策略；程序自身的SIGINT处理与整个Shell管道的生命周期不是同一层责任。不要关闭电源或用SIGKILL代替正常停止。
- 本次不启用systemd，因此没有新建开机自启或长期运行任务。历史专用用户与`CAP_NET_RAW`服务化能力见[systemd_deployment.md](systemd_deployment.md)，不混同于这次`sudo`手工抓包。
- 只采集受控虚拟机与开发板自身的通信；普通交换网络上的开发板不会自动获得其他设备之间的全部流量。当前没有重新搭建网关或镜像口。
- 此轮不重复性能压力或一小时实验。历史约9 Kpps与540万包结果见[performance_baseline.md](performance_baseline.md)及[multiflow_longrun_baseline.md](multiflow_longrun_baseline.md)，不代表本轮测得的吞吐率。
- 当前x86_64为37项自动化回归；板端只确认此前18项自动化基线及本页实际收到的手工证据，不宣称板端37项已通过。

Shell工具说明参考[GNU timeout手册](https://www.gnu.org/software/coreutils/manual/html_node/timeout-invocation.html)；临时发生器接口参考[Python socket文档](https://docs.python.org/3.10/library/socket.html)。这里只使用板端Python 3.8已有的接口，不要求升级Python。
