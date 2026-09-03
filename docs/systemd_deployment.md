# LubanCat-2N非root systemd部署手册

本文记录Netflow Analyzer在LubanCat-2N上的正式运行布局、ARM64产物交付、Linux系统账户、systemd生命周期、capability权限、journal日志、验收、故障定位和回滚方法。

它解决的不是“程序能否手工运行”，而是以下长期运行问题：

```text
谁启动程序
→ 程序以什么身份运行
→ 只授予哪些内核权限
→ 参数从哪里读取
→ 日志保存到哪里
→ 系统如何停止和重启程序
→ 更新失败后如何恢复
```

本文命令区分两个执行环境：

- **开发电脑**：`/home/zcb/workspace/netflow-analyzer`所在的x86_64 Ubuntu虚拟机；
- **目标板**：用户`cat`登录的LubanCat-2N ARM64 Linux，地址为`192.168.1.102`。

不要把两个终端中的命令混用。

## 1. 最终文件和进程布局

目标板安装三个文件：

| 路径 | 所有者与模式 | 作用 |
|---|---|---|
| `/usr/local/bin/netflow-analyzer` | `root:root`，`0755` | 本机管理员安装的可执行程序 |
| `/etc/default/netflow-analyzer` | `root:root`，`0644` | 接口、BPF和流表满载策略 |
| `/etc/systemd/system/netflow-analyzer.service` | `root:root`，`0644` | systemd服务定义 |

路径选择遵循Linux常见约定：

- `/usr/local/bin`用于不由发行版软件包管理器提供、而由本机管理员安装的程序；
- `/etc`保存主机相关配置；
- `/etc/systemd/system`保存本机管理员维护的systemd单元，并优先于发行版单元目录；
- 程序不直接维护独立日志文件，stdout和stderr交给systemd journal；
- journal是否只保存在内存中的`/run/log/journal`，或持久化到`/var/log/journal`，由目标系统的journald配置决定。

程序文件和配置归`root`所有，但运行进程不是root：

```text
磁盘上的程序和配置：root拥有，服务用户不能篡改
运行中的进程：netflow-analyzer用户
抓包权限：systemd只授予CAP_NET_RAW
```

## 2. 为什么不让服务长期以root运行

打开Linux原始包捕获socket需要特殊权限，但“需要一种特权操作”不等于“需要拥有root的全部能力”。

如果程序长期以root运行，那么协议解析、格式化、未来应用层解析中的任何内存错误都可能处于完整系统权限下。当前方案把权限拆成两个维度：

1. Unix身份使用无登录的`netflow-analyzer`普通系统用户；
2. Linux capability只增加`CAP_NET_RAW`，允许创建原始和packet socket。

当前没有授予`CAP_NET_ADMIN`。分析器只监听已经存在的接口，不修改地址、路由、防火墙或接口配置，因此第一版没有理由扩大权限。

## 3. 在开发电脑生成ARM64部署包

完整的官方SDK和通用GCC配置见[交叉编译手册](cross_compilation.md)。本节只说明部署阶段。

### 3.1 重新构建当前提交

官方SDK构建目录：

```bash
export NFA_SDK_BUILD=/home/zcb/build/netflow-analyzer-lubancat-sdk-release-v2
```

配置好SDK工具链、`LD_LIBRARY_PATH`和ARM64 libpcap的`pkg-config`环境后执行：

```bash
cmake --build "$NFA_SDK_BUILD" -j2
```

不能复用早于当前源码提交的旧ELF。`file`、`readelf`和SHA-256应当在每次部署前重新检查。

### 3.2 检查ELF和ABI

```bash
file "$NFA_SDK_BUILD/bin/netflow-analyzer"
```

`file`读取ELF头，确认它是ARM64程序，不会真正运行目标程序。

```bash
"$LUBANCAT_TOOLCHAIN/bin/aarch64-rockchip-linux-gnu-readelf" \
    --version-info \
    "$NFA_SDK_BUILD/bin/netflow-analyzer" |
grep -o 'GLIBC_[0-9.]*' |
sort -Vu
```

这里的管道依次执行：

1. `readelf`输出版本依赖；
2. `grep -o`只保留`GLIBC_x.y`片段；
3. `sort -Vu`按版本排序并去重。

当前官方SDK产物最高只要求`GLIBC_2.17`，动态加载器为`/lib/ld-linux-aarch64.so.1`，主要依赖为`libpcap.so.0.8`和`libc.so.6`。

### 3.3 使用DESTDIR进行暂存安装

```bash
export NFA_STAGE="$(mktemp -d /tmp/nfa-lubancat-stage.XXXXXX)"
```

`mktemp -d`原子地创建名称唯一、默认只允许当前用户访问的临时目录。模板中的六个`X`由工具替换，避免手写固定`/tmp`目录导致冲突或符号链接攻击。

```bash
DESTDIR="$NFA_STAGE" \
cmake --install "$NFA_SDK_BUILD" --prefix /usr/local
```

这里有两个不同层级的根：

```text
逻辑安装前缀：/usr/local
临时打包根：$NFA_STAGE
实际写入：$NFA_STAGE/usr/local/bin/netflow-analyzer
```

`DESTDIR=value command`只给紧随其后的`cmake`进程设置环境变量，不会永久修改Shell，也不会写入开发电脑真正的`/usr/local`。

把配置与服务单元加入暂存根：

```bash
install \
    -D \
    -m 0644 \
    packaging/systemd/netflow-analyzer.default \
    "$NFA_STAGE/etc/default/netflow-analyzer"
```

```bash
install \
    -D \
    -m 0644 \
    packaging/systemd/netflow-analyzer.service \
    "$NFA_STAGE/etc/systemd/system/netflow-analyzer.service"
```

`install`比`cp`更适合部署：它能够同时复制文件、建立父目录并设置明确模式。

- `-D`创建目标文件所需的父目录；
- `-m 0644`设置所有者可读写、组和其他用户只读；
- systemd单元和配置是数据文件，不需要执行位。

### 3.4 打包并传输

```bash
export NFA_COMMIT="$(git rev-parse --short HEAD)"
export NFA_BUNDLE="/tmp/netflow-analyzer-${NFA_COMMIT}-aarch64.tar.gz"

tar -C "$NFA_STAGE" -czf "$NFA_BUNDLE" .
```

`tar`参数：

- `-C DIR`先切换归档根，避免压缩包携带开发电脑的绝对路径；
- `-c`创建归档；
- `-z`使用gzip压缩；
- `-f FILE`指定归档文件名；
- 最后的`.`表示归档暂存根中的全部内容。

检查包内路径和摘要：

```bash
tar -tzf "$NFA_BUNDLE"
sha256sum "$NFA_BUNDLE"
```

`-t`只列出归档，不解压。SHA-256用于验证传输后的字节内容与开发电脑完全一致，但不替代可信来源和签名体系。

上传到目标板临时目录：

```bash
scp "$NFA_BUNDLE" cat@192.168.1.102:/tmp/
```

`scp`通过SSH传输；冒号左边是远端主机，冒号右边是远端路径。部署包是生成物，不提交Git。

2026-09-02首次服务化部署包：

```text
提交：740d5ab
文件：netflow-analyzer-740d5ab-aarch64.tar.gz
SHA-256：f916176829974d61ff853310638fd0b07c8570f715d97f67238aebfad259c55d
```

## 4. 在目标板安全安装

### 4.1 校验与临时解压

```bash
export NFA_BUNDLE=/tmp/netflow-analyzer-740d5ab-aarch64.tar.gz
sha256sum "$NFA_BUNDLE"
```

摘要必须与开发电脑一致。然后解压到新的临时目录，而不是直接用`tar -C /`覆盖系统：

```bash
export NFA_EXTRACT_DIR="$(mktemp -d /tmp/nfa-deploy.XXXXXX)"
tar -xzf "$NFA_BUNDLE" -C "$NFA_EXTRACT_DIR"
```

这种“两阶段安装”允许先检查内容，再逐个使用`sudo install`写入系统路径。

```bash
find "$NFA_EXTRACT_DIR" -type f -printf '%M %p\n'
file "$NFA_EXTRACT_DIR/usr/local/bin/netflow-analyzer"
ldd "$NFA_EXTRACT_DIR/usr/local/bin/netflow-analyzer"
"$NFA_EXTRACT_DIR/usr/local/bin/netflow-analyzer" --version
```

- `find -type f`只列普通文件；
- `%M`显示类似`ls -l`的权限；
- `ldd`在目标系统解析动态依赖，适合检查本项目自己生成的可信ELF；不要对不可信程序随意执行`ldd`；
- `--version`证明动态加载器和依赖已经足以进入程序的`main()`。

### 4.2 覆盖前备份

```bash
export NFA_BACKUP_DIR="/home/cat/netflow-analyzer-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$NFA_BACKUP_DIR"
```

时间戳让每轮升级使用不同目录。对每个已经存在的目标先备份：

```bash
if sudo test -e /usr/local/bin/netflow-analyzer; then
    sudo cp -a \
        /usr/local/bin/netflow-analyzer \
        "$NFA_BACKUP_DIR/netflow-analyzer.previous"
fi
```

配置和服务单元使用相同模式。`test -e`检查对象是否存在；`cp -a`尽量保留模式、所有者和时间，适合恢复，不等同于普通内容复制。

### 4.3 创建无登录系统账户

```bash
if ! getent group netflow-analyzer >/dev/null; then
    sudo groupadd --system netflow-analyzer
fi
```

```bash
if ! getent passwd netflow-analyzer >/dev/null; then
    sudo useradd \
        --system \
        --gid netflow-analyzer \
        --home-dir /nonexistent \
        --no-create-home \
        --shell /usr/sbin/nologin \
        netflow-analyzer
fi
```

Shell语义：

- `! command`对命令结果取反；
- `>/dev/null`丢弃标准输出，只使用退出状态；
- `if ...; then ...; fi`使命令可重复执行，已存在时不会再次创建；
- `getent`通过系统名称服务查询用户或组，不要求直接解析`/etc/passwd`；
- `--system`创建服务账户；
- `--no-create-home`不占用home目录；
- `nologin`阻止交互登录，但不妨碍systemd以该UID启动程序。

检查：

```bash
getent passwd netflow-analyzer
getent group netflow-analyzer
id netflow-analyzer
```

### 4.4 安装为root拥有的文件

```bash
sudo install \
    -o root \
    -g root \
    -m 0755 \
    "$NFA_EXTRACT_DIR/usr/local/bin/netflow-analyzer" \
    /usr/local/bin/netflow-analyzer
```

```bash
sudo install \
    -o root \
    -g root \
    -m 0644 \
    "$NFA_EXTRACT_DIR/etc/default/netflow-analyzer" \
    /etc/default/netflow-analyzer
```

systemd单元同样以`0644`安装到`/etc/systemd/system`。

- `sudo`只提升当前安装命令，不让后续服务永久使用root；
- `-o root -g root`防止服务进程修改自己的程序和配置；
- `0755`允许所有用户读取和执行程序，只有root能修改；
- `0644`允许读取配置，只有root能修改。

## 5. systemd如何启动这个服务

服务单元的关键关系：

```text
/etc/default/netflow-analyzer
    ↓ EnvironmentFile读取变量
netflow-analyzer.service
    ↓ systemd设置UID/GID、capability和安全边界
/usr/local/bin/netflow-analyzer
    ↓ stdout/stderr
systemd journal
```

systemd启动时不是把单元文件交给Shell执行。它解析`ExecStart`，建立服务进程的身份、命名空间和能力集合，然后执行ELF。因此不要把Shell重定向、管道或复杂脚本语法随意写入`ExecStart`。

当前参数：

```bash
NFA_INTERFACE=eth0
NFA_FILTER="icmp"
NFA_FLOW_FULL_POLICY=reject
```

`${NFA_FILTER}`在单元中作为一个参数展开，所以带空格的BPF仍保持一个命令行参数。

### 5.1 重新加载单元定义

```bash
sudo systemd-analyze verify \
    /etc/systemd/system/netflow-analyzer.service

sudo systemctl daemon-reload
```

- `systemd-analyze verify`做静态检查；
- `daemon-reload`通知PID 1重新读取磁盘上的单元文件；
- 修改单元后只执行`restart`而不执行`daemon-reload`，systemd仍可能使用旧定义；
- 修改`/etc/default`只影响下一次启动，通常直接`restart`即可，因为环境文件在每次启动时读取。

### 5.2 start、stop、restart和enable的区别

```bash
sudo systemctl start netflow-analyzer
sudo systemctl stop netflow-analyzer
sudo systemctl restart netflow-analyzer
sudo systemctl enable netflow-analyzer
```

| 命令 | 当前是否运行 | 下次开机是否自动运行 |
|---|---|---|
| `start` | 启动 | 不改变 |
| `stop` | 停止 | 不改变 |
| `restart` | 先停再启动 | 不改变 |
| `enable` | 不一定立即启动 | 设置自动启动 |
| `enable --now` | 立即启动 | 设置自动启动 |
| `disable --now` | 立即停止 | 取消自动启动 |

首次部署应先`start`完成验收，不要直接`enable --now`。当前已经完成手工启停，开机自动启动和重启恢复留给下一步验证。

## 6. Linux capability权限边界

单元中的关键配置：

```ini
User=netflow-analyzer
Group=netflow-analyzer
AmbientCapabilities=CAP_NET_RAW
CapabilityBoundingSet=CAP_NET_RAW
NoNewPrivileges=yes
```

含义：

- `User`和`Group`决定Unix身份；
- `AmbientCapabilities`让最终执行的普通ELF在非root身份下保留指定能力；
- `CapabilityBoundingSet`设置能力上限，即使程序或子进程尝试获得其他能力也不能越过；
- `NoNewPrivileges`禁止后续`execve()`通过setuid、文件capability等机制获得新增权限。

本项目没有执行：

```bash
sudo setcap cap_net_raw=eip /usr/local/bin/netflow-analyzer
```

因此`getcap /usr/local/bin/netflow-analyzer`没有输出是正常现象。能力附着在systemd创建的服务进程上，而不是永久写在ELF扩展属性中。

运行时检查：

```bash
export NFA_PID="$(systemctl show netflow-analyzer -p MainPID --value)"

ps -o pid,user,group,comm,args -p "$NFA_PID"

sudo grep -E \
    '^(Name|Uid|Gid|CapInh|CapPrm|CapEff|CapBnd|CapAmb|NoNewPrivs):' \
    "/proc/$NFA_PID/status"
```

`/proc/PID/status`是内核提供的进程状态视图。`CAP_NET_RAW`编号为13，因此位掩码是：

```text
1 << 13 = 0x2000
```

当前实测的有效、边界和ambient能力只包含`0000000000002000`，`NoNewPrivs`为1，进程用户和组均为`netflow-analyzer`。

## 7. journal日志命令

常用命令：

```bash
# 查看该单元本次开机以来的全部日志。
sudo journalctl -u netflow-analyzer -b --no-pager

# 查看最后100条。
sudo journalctl -u netflow-analyzer -n 100 --no-pager

# 持续跟随新日志，类似tail -f。
sudo journalctl -u netflow-analyzer -f
```

参数：

- `-u`按systemd单元过滤；
- `-b`限制为当前boot；
- `-n`限制尾部记录数；
- `-f`持续等待新记录；
- `--no-pager`直接写终端，不启动`less`分页器。

目标板上的旧版`journalctl`不能解析：

```text
2026-09-02T21:37:11+08:00
```

因此不要用`date --iso-8601=seconds`生成`--since`参数。使用兼容格式：

```bash
export NFA_JOURNAL_SINCE="$(date '+%Y-%m-%d %H:%M:%S')"

sudo journalctl \
    -u netflow-analyzer \
    --since "$NFA_JOURNAL_SINCE" \
    --no-pager
```

引号必须保留，否则日期和时间会被Shell拆成两个参数。

systemd在每行前增加时间、主机名、日志标识和PID，例如：

```text
Sep 02 21:44:56 lubancat netflow-analyzer[49933]: Total packets: 4
```

方括号中的49933是本轮服务主进程PID，不是包数或流编号。

## 8. 首次板端验收结果

2026-09-02在LubanCat-2N物理接口`eth0`完成首次非root手工启停验收：

- systemd成功加载单元；
- 进程以`netflow-analyzer`用户和组运行；
- 运行时能力边界只有`CAP_NET_RAW`，`NoNewPrivs=1`；
- `/usr/local/bin/netflow-analyzer`本身没有文件capability；
- 静默期5秒周期报告能够在服务仍运行时进入journal，证明stdout行缓冲生效；
- VMware NAT虚拟机向`192.168.1.102`发送2次ping，应用取得4个ICMP包；
- 4包全部为`complete`，4次哈希操作各检查1个槽；
- 捕获后端报告6包，两个drop字段均为0；
- `systemctl stop`发送SIGTERM后程序正常输出最终汇总并退出，没有发生流拒绝、过期或淘汰。

最终1条流：

```text
192.168.1.100 → 192.168.1.102：2个Echo Request，196字节
192.168.1.102 → 192.168.1.100：2个Echo Reply，196字节
```

`Flow 1`表示流汇总中的第一条记录，不表示额外数据包。ICMP没有TCP/UDP端口，所以端口为0；它也不拥有TCP状态，因此显示`tcp_state=not-applicable`。服务停止时该流尚未满足30秒空闲过期条件，所以`Expired flows: 0`并在最终活动流表汇总中出现。

后端6包与应用4包属于已经记录的libpcap统计层级差异，不能用差值推导丢包；本轮两个专用drop字段均为0。

## 9. 开机自启与重启恢复验收

2026-09-03完成受控重启验收：

- 重启前执行`systemctl enable netflow-analyzer`，在`multi-user.target.wants`下建立指向正式单元的符号链接；
- 重启前后`/proc/sys/kernel/random/boot_id`不同，证明观察到的是新的内核boot，而不是原服务进程重启；
- 重新登录后没有人工执行`systemctl start`，单元已经同时处于`enabled`和`active`；
- `Result=success`且`NRestarts=0`，证明本次boot一次启动成功，没有依赖失败重试恢复；
- `systemd-analyze critical-chain`显示分析器排在`network-online.target`之后；
- 新boot中的主进程拥有新的PID，但用户和组仍为`netflow-analyzer`；
- 有效、边界和ambient能力仍只包含`CAP_NET_RAW`对应的`0x2000`，`NoNewPrivs=1`；
- 当前boot的journal在无人登录启动后持续收到静默周期报告；
- 虚拟机再次发送2次ping后，journal中的周期报告正确出现4个完整ICMP包和1条活动流。

`enable`本身只创建启动依赖，不证明服务能够跨重启运行。本轮通过不同boot ID、无人工`start`、新PID、运行状态、权限集合和真实流量共同证明开机自启成立。

`After=network-online.target`只保证systemd声明的启动顺序，不等于互联网一定可达，也不保证每种网络管理器都采用相同的“在线”标准。当前物理`eth0`在服务启动时已经可供libpcap打开；网络延迟、接口缺失和恢复过程仍可在后续故障注入中单独验证。

服务在验收后保持`enabled`和`active`，作为后续服务方式长稳测试的基础。

## 10. systemd安全评分基线

2026-09-03在目标板执行：

```bash
sudo systemd-analyze \
    --no-pager \
    security \
    netflow-analyzer.service
```

目标环境：

```text
systemd 245 (245.4-4ubuntu3.24)
编译时启用SECCOMP、APPARMOR、SELINUX、AUDIT和KMOD等支持
Overall exposure: 5.2 MEDIUM
```

这个分数是systemd依据单元沙箱属性计算的启发式暴露面，不是C代码漏洞扫描，也不能直接换算成被攻击概率。报告中的叉号表示“该项没有达到评分器最严格的条件”，不自动等于配置错误。

### 10.1 抓包业务必须保留的暴露面

| 报告项 | 权重 | 当前判断 |
|---|---:|---|
| `PrivateNetwork=` | 0.5 | 不能启用；独立网络命名空间会使服务看不到宿主机`eth0` |
| `RestrictAddressFamilies=~AF_PACKET` | 0.2 | 不能禁止；Linux libpcap实时抓包依赖packet socket |
| `AmbientCapabilities=` | 0.1 | 接受；非root ELF需要动态获得`CAP_NET_RAW` |
| `CapabilityBoundingSet=~CAP_NET_*` | 0.1 | 接受其中的`CAP_NET_RAW`；当前边界已经排除其他能力 |

这些项目即使让总分下降，也会同时破坏核心业务，不能为了评分变绿而启用。

### 10.2 先保留并通过实验缩小的网络能力

报告还对`AF_INET`、`AF_INET6`、`AF_NETLINK`和`AF_UNIX`扣分。libpcap在激活接口时除了创建`AF_PACKET` socket，还可能通过Internet或netlink socket查询接口状态、索引和其他元数据；未来BPF如果使用主机名，也可能触发名称解析。journal的连接由systemd在`exec`前建立，但应用和库是否另建本地socket仍应以实测为准。

因此下一阶段可以尝试地址族白名单，但必须保留至少：

```text
AF_PACKET AF_NETLINK AF_INET AF_INET6 AF_UNIX
```

这能排除未使用的特殊地址族，却不会让评分器把上述业务需要的地址族全部判为安全。每次收紧后必须重新验证启动、静默报告、ICMP和SIGTERM。

### 10.3 第一批低风险加固候选

当前程序不访问物理设备节点、不修改时钟或主机名、不读取内核日志、不创建命名空间、不申请实时调度、不使用JIT，也不使用SysV IPC。第一批候选为：

```ini
PrivateDevices=yes
ProtectClock=yes
ProtectKernelLogs=yes
ProtectHostname=yes
RestrictNamespaces=yes
RestrictRealtime=yes
SystemCallArchitectures=native
MemoryDenyWriteExecute=yes
RemoveIPC=yes
UMask=0077
```

其中`PrivateDevices=yes`预计同时改善设备ACL相关评分；最终仍以systemd 245重新评分为准。`RestrictNamespaces=yes`能同时覆盖用户、cgroup、IPC、网络、mount、PID和UTS命名空间创建权限。systemd在执行程序前建立服务需要的隔离环境，不等于服务进程之后仍可以创建新命名空间。

### 10.4 暂不直接启用的高风险或高成本项

- `SystemCallFilter=`：潜在降分较大，但必须确认libpcap使用的`socket`、`ioctl`、`poll`、`recv`和内存管理调用；后续按系统调用组逐批拒绝；
- `PrivateUsers=yes`：用户命名空间中的能力语义可能影响初始网络命名空间上的`CAP_NET_RAW`；
- `IPAddressDeny=any`：依赖目标内核和cgroup网络过滤支持，还要验证是否影响当前抓包路径；
- `RootDirectory=`或`RootImage=`：需要为动态加载器、libc、libpcap和配置构造完整运行根，当前0.1权重不值得增加部署复杂度。

当前结论不是“5.2已经足够安全”，而是先建立可复现基线，再按风险从低到高逐批加固并回归。下一小步只加入第一批低风险选项；地址族和系统调用限制单独实验，以便失败时能定位具体边界。

### 10.5 第一批加固的目标板结果

提交`dc542ad`在LubanCat-2N部署后，服务使用新进程重新启动；专用用户、唯一`CAP_NET_RAW`能力、`NoNewPrivs=1`、静默周期报告和真实ICMP抓包均保持正常。目标板再次执行安全审计，命令状态为0，结果由：

```text
5.2 MEDIUM
```

降低到：

```text
3.7 OK
```

评分降低1.5。以下限制在报告中已经显示为通过：

- `PrivateDevices=yes`；
- `ProtectKernelLogs=yes`和`ProtectHostname=yes`；
- `RestrictNamespaces=yes`覆盖七类命名空间创建；
- `RestrictRealtime=yes`和`SystemCallArchitectures=native`；
- `MemoryDenyWriteExecute=yes`、`RemoveIPC=yes`和`UMask=0077`。

报告仍对两处相关属性扣分：

- `DeviceAllow=`由基线的0.2降为0.1；`PrivateDevices=yes`已经通过，但systemd为服务保留的最小设备集合仍被评分器视为设备ACL，并不表示服务重新获得任意硬件访问；
- 单元已经配置`ProtectClock=yes`，能力边界也排除了`CAP_SYS_TIME`，但systemd 245的评分仍显示0.2。systemd上游已有同代版本中显式配置仍不改变该评分的报告，因此本文把它记录为旧版评分器限制，不通过叠加未知限制来追求绿项：<https://github.com/systemd/systemd/issues/15758>。

剩余的宿主网络、`AF_PACKET`和`CAP_NET_RAW`扣分是实时抓包的业务成本。第一批通用加固完成后，本项目不继续以降低评分为主线；地址族白名单和系统调用过滤仅在后续出现明确威胁模型时单独实验。

对应版本的准确语义以systemd v245官方`systemd.exec`和`systemd-analyze`手册为准：

- <https://github.com/systemd/systemd/blob/v245/man/systemd.exec.xml>
- <https://github.com/systemd/systemd/blob/v245/man/systemd-analyze.xml>

## 11. systemctl stop为什么能够优雅结束

当前停止链路：

```text
systemctl stop
→ systemd向主进程发送SIGTERM
→ main中的信号handler只发布停止请求
→ pcap_breakloop中断采集等待
→ 主循环回到普通控制流
→ 查询pcap_stats并关闭capture
→ 输出累计指标和流汇总
→ main返回EXIT_SUCCESS
→ systemd记录Result=success
```

信号handler不直接执行`printf`、释放流表或关闭复杂对象，因为许多C库函数不具备异步信号安全性。资源清理仍在主循环所属线程完成。

`Restart=on-failure`只在异常退出时重启。正常处理SIGTERM并返回成功不会形成重启循环。

## 12. 常见故障定位

先收集三组信息：

```bash
systemctl status netflow-analyzer --no-pager --full
sudo journalctl -u netflow-analyzer -n 100 --no-pager
systemctl show netflow-analyzer \
    -p Result \
    -p ExecMainCode \
    -p ExecMainStatus
```

| 现象 | 常见层级 | 优先检查 |
|---|---|---|
| `203/EXEC` | 执行阶段 | 路径、`0755`、AArch64 ELF、动态加载器 |
| `217/USER` | 身份阶段 | 用户和组是否存在 |
| `Permission denied`打开接口 | capability | `CapEff`、`CapBnd`、`CapAmb`与`CAP_NET_RAW` |
| BPF参数错误 | 配置展开 | `/etc/default`引号和`ps ... args` |
| 修改单元后行为不变 | systemd缓存 | 是否执行`daemon-reload` |
| `systemd-analyze verify`报告其他单元 | 验证器加载系统环境 | 先看是否点名本单元，再用`daemon-reload`、`systemctl show`和真实启动复核 |
| `journalctl`时间解析失败 | 工具版本 | 使用`YYYY-MM-DD HH:MM:SS`或`-b` |
| 日志直到退出才出现 | stdio缓冲 | 当前ELF是否包含`setvbuf`改动，是否部署了旧产物 |
| 服务反复重启 | 退出状态 | journal、`NRestarts`和`Restart=on-failure` |

排除故障后可使用：

```bash
sudo systemctl reset-failed netflow-analyzer
```

它只清除systemd记录的失败和启动限速状态，不修改程序、配置或日志。

## 13. 回滚与更新原则

更新前先`stop`并备份现有三个文件。新程序安装后先手工`start`验证，不要直接覆盖并重启后离开设备。

如果新版本失败：

```bash
sudo systemctl stop netflow-analyzer

sudo install \
    -o root \
    -g root \
    -m 0755 \
    "$NFA_BACKUP_DIR/netflow-analyzer.previous" \
    /usr/local/bin/netflow-analyzer
```

如单元或配置也有备份，则按原模式恢复，随后：

```bash
sudo systemctl daemon-reload
sudo systemctl start netflow-analyzer
```

只有确认恢复版本能够启动后，才考虑清理临时解压目录和旧部署包。不要用宽泛的递归删除命令清理`/tmp`、`/home/cat`或系统目录。

## 14. 仍待完成的服务验收

首次手工启动、非root身份、能力边界、journal实时日志、真实ICMP、SIGTERM收尾、开机自启、重启恢复和第一批低风险沙箱加固已经通过。加固后安全评分由`5.2 MEDIUM`降为`3.7 OK`。仍需：

1. 通过受控故障注入验证接口暂不可用时的重启和恢复行为；
2. 进行数小时或数天服务方式浸泡测试，观察journal占用、内存、CPU和drop；
3. 根据长期日志需求决定journal采用易失还是持久存储及其容量上限。

地址族白名单和系统调用拒绝组不再作为当前必做项；只有在出现明确安全需求时才单独立项，避免为了评分破坏libpcap的数据面。
