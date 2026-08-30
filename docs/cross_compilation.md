# LubanCat-2N ARM64交叉编译手册

最后更新：2026-08-30（Asia/Shanghai）

本文记录Netflow Analyzer在x86_64 Ubuntu虚拟机上生成ARM64程序，并部署到LubanCat-2N运行的两种已验证方法。命令、环境变量、CMake参数、ABI检查和实际故障均来自本项目真实操作。

两种方法均已完成以下验收：

- 在x86_64虚拟机上完成Release交叉构建；
- `file`确认产物为AArch64 ELF；
- `readelf`确认动态加载器为`/lib/ld-linux-aarch64.so.1`；
- 最终程序只要求`GLIBC_2.17`，不再要求失败版本中的`GLIBC_2.34`；
- 动态依赖为板端已有的`libpcap.so.0.8`和`libc.so.6`；
- 通过`scp`部署到LubanCat-2N的ext4目录；
- 板端`ldd`、`--help`及真实ICMP抓包运行成功。

## 1. 最终环境和结果

### 1.1 已确认的环境

| 项目 | 实际值 |
|---|---|
| 开发主机 | Ubuntu 24.04.4 LTS，x86_64／amd64 |
| 目标板 | LubanCat-2N，AArch64／ARM64 |
| 目标板glibc | 最高导出`GLIBC_2.30` |
| 目标板libpcap | 1.9.1，SONAME为`libpcap.so.0.8` |
| 项目构建系统 | CMake 3.28.3、Ninja 1.11.1 |
| 项目测试基线 | x86_64 Debug构建17项CTest全部通过 |
| 开发板地址 | `192.168.1.102` |
| 开发板用户 | `cat` |

### 1.2 两种方法对比

| 项目 | 方法一：官方SDK | 方法二：通用GCC与板端sysroot |
|---|---|---|
| 编译器 | 官方Buildroot GCC 9.3 | Ubuntu `aarch64-linux-gnu-gcc` 13 |
| GCC启动文件 | 官方SDK内置sysroot | 通过GCC `-B`强制使用板端`Scrt1.o`等文件 |
| 基础libc | SDK glibc 2.29 | 从开发板同步的glibc 2.30 |
| libpcap | 从板端提取的隔离overlay | 板端完整sysroot中的libpcap 1.9.1 |
| 最终GLIBC需求 | `GLIBC_2.17` | `GLIBC_2.17` |
| 交叉程序大小 | 53160字节 | 76792字节 |
| 板端结果 | 成功 | 成功 |
| 推荐用途 | 正式、可重复的目标版本构建 | 学习、备用、验证通用工具链能力 |

文件大小和SHA-256只对应当时源码快照。重新编译或修改源码后数值变化是正常现象，不应把它们作为永久发布标识。

## 2. 交叉编译实际包含哪些层

一次动态链接的交叉编译不只有`gcc`：

```text
x86_64主机工具
    CMake、Ninja、pkg-config、GCC驱动、cc1、汇编器、链接器
                         │
                         ▼
ARM64目标编译环境
    目标头文件、crt启动文件、libc、动态加载器信息、libpcap
                         │
                         ▼
ARM64 ELF
    AArch64机器码、动态加载器路径、所需共享库SONAME和符号版本
                         │
                         ▼
LubanCat运行环境
    板端Linux内核、/lib/ld-linux-aarch64.so.1、libc.so.6、libpcap.so.0.8
```

需要始终区分：

- GCC和`cc1`运行在x86_64虚拟机上；
- GCC生成的是AArch64机器码；
- sysroot只在编译和链接时提供目标系统视图，不能在虚拟机上启动ARM系统；
- 动态链接程序通常不携带完整libc和libpcap，运行时仍加载鲁班猫本机的共享库。

因此：

```text
生成了ARM64 ELF ≠ 一定能在目标板运行
```

还必须保证目标板提供程序所要求的GLIBC符号版本和共享库SONAME。

## 3. Shell变量和命令行基础

### 3.1 `$NAME`不是目录名

命令中的：

```bash
$NFA_GENERIC_BUILD
```

表示取出Shell变量`NFA_GENERIC_BUILD`保存的字符串。实际目录可能是：

```text
/home/zcb/build/netflow-analyzer-generic-sysroot-release
```

查看变量时使用：

```bash
printf 'NFA_GENERIC_BUILD=<%s>\n' "$NFA_GENERIC_BUILD"
```

尖括号可以帮助识别空值。如果输出为`<>`，说明变量未设置。

### 3.2 `export`的作用域

例如：

```bash
export NFA_GENERIC_BUILD=/home/zcb/build/netflow-analyzer-generic-sysroot-release
```

含义是：

1. 在当前Shell中创建或更新变量；
2. 把它传递给当前Shell随后启动的CMake、Ninja、GCC等子进程。

关闭终端或新开一个不继承当前环境的终端后，变量通常消失。出现以下错误时，应先检查变量是否为空：

```text
CMake Error: No build directory specified for -B
```

因为：

```bash
cmake -B "$NFA_SDK_BUILD"
```

在变量为空时等价于给`-B`传入空字符串。

### 3.3 为什么路径要加双引号

推荐：

```bash
cmake --build "$NFA_GENERIC_BUILD" -j2
```

双引号让变量展开后的完整字符串成为一个参数，即使路径以后包含空格也不会被Shell拆成多个参数。双引号不会阻止`$变量`展开。

### 3.4 反斜杠换行规则

Shell中的反斜杠可以续行：

```bash
cmake \
    -S . \
    -B build
```

反斜杠必须是该行最后一个字符，后面不能再有空格。此前出现过：

```text
The source directory ".../netflow-analyzer/ " does not exist
```

就是因为反斜杠或复制格式引入了额外空格。本手册对关键CMake配置优先给出单行命令，降低复制错误概率。

### 3.5 三组容易混淆的环境变量

| 变量 | 控制对象 | 运行在哪一侧 |
|---|---|---|
| `LD_LIBRARY_PATH` | GCC内部的`cc1`等主机程序查找共享库 | x86_64虚拟机 |
| `PKG_CONFIG_SYSROOT_DIR` | 给`.pc`中的`/usr`等路径增加目标sysroot前缀 | 构建配置阶段 |
| `PKG_CONFIG_LIBDIR` | 限制`pkg-config`只搜索指定目标`.pc`文件 | 构建配置阶段 |
| `CMAKE_SYSROOT` | 给编译器和CMake指定目标文件系统视图 | ARM64目标构建 |
| GCC `-B路径` | 调整GCC启动文件、库及内部工具的搜索前缀 | ARM64目标链接 |

不能用`LD_LIBRARY_PATH`代替sysroot，也不能用`CMAKE_SYSROOT`解决主机上`cc1`缺少x86_64共享库的问题。

## 4. 共同准备：确认目标ABI

在鲁班猫执行：

```bash
uname -m
getconf GNU_LIBC_VERSION
. /etc/os-release
printf 'OS=%s %s\n' "$NAME" "$VERSION_ID"
pkg-config --modversion libpcap
pkg-config --variable=pcfiledir libpcap
pkg-config --variable=includedir libpcap
pkg-config --variable=libdir libpcap
```

本次结果表明目标为AArch64，libpcap为1.9.1，目标libc最高提供`GLIBC_2.30`。

在虚拟机检查普通本机构建：

```bash
file build/bin/netflow-analyzer
```

本机构建显示`x86-64`，不能直接复制到ARM64开发板运行。

## 5. 从鲁班猫导出sysroot

完整板端sysroot主要供方法二使用。方法一只需从它提取libpcap。

### 5.1 两端都需要rsync

虚拟机发起`rsync`时，实际过程是：

```text
虚拟机rsync → SSH → 鲁班猫rsync
```

如果远端出现：

```text
bash: rsync: command not found
rsync error: error in rsync protocol data stream (code 12)
```

应在鲁班猫安装：

```bash
sudo apt update
sudo apt install -y rsync
```

### 5.2 创建本地目录

以下命令在虚拟机执行：

```bash
mkdir -p /home/zcb/sysroots/lubancat2n/usr/include
mkdir -p /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu
mkdir -p /home/zcb/sysroots/lubancat2n/lib/aarch64-linux-gnu
mkdir -p /home/zcb/sysroots/lubancat2n/usr/share/pkgconfig
```

`mkdir -p`递归创建缺失的中间目录；目录已经存在时不报错。此前只创建`usr/`而没有创建`usr/lib/`，导致接收端报：

```text
mkdir ".../usr/lib/aarch64-linux-gnu" failed: No such file or directory
rsync error: error in file IO (code 11)
```

### 5.3 正确同步目录

```bash
rsync -a --info=progress2 cat@192.168.1.102:/usr/include/ /home/zcb/sysroots/lubancat2n/usr/include/
rsync -a --info=progress2 cat@192.168.1.102:/usr/lib/aarch64-linux-gnu/ /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/
rsync -a --info=progress2 cat@192.168.1.102:/lib/aarch64-linux-gnu/ /home/zcb/sysroots/lubancat2n/lib/aarch64-linux-gnu/
rsync -a --info=progress2 cat@192.168.1.102:/usr/share/pkgconfig/ /home/zcb/sysroots/lubancat2n/usr/share/pkgconfig/
rsync -aL cat@192.168.1.102:/lib/ld-linux-aarch64.so.1 /home/zcb/sysroots/lubancat2n/lib/
```

参数含义：

- `-a`：递归同步并保留目录结构、时间和符号链接等归档语义；
- `-L`：追踪符号链接并复制最终文件；只对明确的动态加载器文件使用；
- `--info=progress2`：显示整个同步任务的累计进度；
- 源路径结尾的`/`：复制目录内容，而不是在目标中再创建一层同名目录。

不要对整个`/usr/lib/aarch64-linux-gnu/`使用`-aL`。本次曾因此追踪所有库链接、复制约2GB数据，并在Qt失效链接处出现：

```text
symlink has no referent: "/usr/lib/aarch64-linux-gnu/qt-default/qtchooser/default.conf"
rsync error: some files/attrs were not transferred (code 23)
```

该Qt链接与项目无关，但说明对完整系统库树使用`-L`过于激进。重新用`-a`同步即可保留该失效链接本身，不要求解析其目标。

### 5.4 检查sysroot关键文件

```bash
ls -l /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/Scrt1.o
ls -l /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/libc.so
ls -l /home/zcb/sysroots/lubancat2n/lib/aarch64-linux-gnu/libc.so.6
ls -l /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/libpcap.so
find /home/zcb/sysroots/lubancat2n -name libpcap.pc -print
```

sysroot位于仓库外，不应提交到Git。目标板升级glibc或libpcap后，应重新生成并记录版本。

## 6. 方法一：鲁班猫官方SDK工具链

### 6.1 方法结构

```text
官方Buildroot GCC 9.3及其glibc 2.29 sysroot
                       +
从鲁班猫提取的libpcap 1.9.1 overlay
                       ↓
             ARM64动态链接程序
```

官方SDK路径：

```text
/home/zcb/workspace/LubanCat_Linux_rk356x_SDK_20260623.tgz
```

该2GB压缩包不是直接检出的源码树，而是仅包含`.repo/`对象的离线BSP仓库。manifest确认它包含：

```text
prebuilts/gcc/linux-x86/aarch64/
    gcc-buildroot-9.3.0-2020.03-x86_64_aarch64-rockchip-linux-gnu
    gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu
```

本项目优先使用Buildroot GCC 9.3。

### 6.2 只检出工具链

```bash
mkdir -p /home/zcb/sdk/lubancat-rk356x-20260623
tar -xzf /home/zcb/workspace/LubanCat_Linux_rk356x_SDK_20260623.tgz -C /home/zcb/sdk/lubancat-rk356x-20260623
cd /home/zcb/sdk/lubancat-rk356x-20260623
./.repo/repo/repo sync -l prebuilts/gcc/linux-x86/aarch64/gcc-buildroot-9.3.0-2020.03-x86_64_aarch64-rockchip-linux-gnu
```

`repo sync -l`中的`-l`表示只使用压缩包中的本地Git对象，不联网拉取。最后的项目路径限制本次只生成工具链工作目录，不检出kernel、U-Boot等无关内容。

### 6.3 设置SDK变量

```bash
export LUBANCAT_SDK=/home/zcb/sdk/lubancat-rk356x-20260623
export LUBANCAT_TOOLCHAIN="$LUBANCAT_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-buildroot-9.3.0-2020.03-x86_64_aarch64-rockchip-linux-gnu"
export LUBANCAT_CC="$LUBANCAT_TOOLCHAIN/bin/aarch64-rockchip-linux-gnu-gcc"
export NFA_SDK_BUILD=/home/zcb/build/netflow-analyzer-lubancat-sdk-release-v2
```

验证：

```bash
"$LUBANCAT_CC" --version
"$LUBANCAT_CC" -dumpmachine
"$LUBANCAT_CC" -print-sysroot
```

已验证结果：

```text
GCC：9.3.0
目标三元组：aarch64-rockchip-linux-gnu
SDK libc：glibc 2.29
```

### 6.4 解决SDK主机库依赖

官方工具链的`cc1`是运行在x86_64虚拟机上的旧程序，需要：

```text
libisl.so.15
libmpfr.so.4
```

Ubuntu 24.04默认没有这些旧SONAME，但SDK已经在以下目录携带：

```text
$LUBANCAT_TOOLCHAIN/lib
```

因此设置：

```bash
export LD_LIBRARY_PATH="$LUBANCAT_TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

`${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}`表示：只有原变量非空时，才追加冒号和原值，避免得到没有意义的尾部冒号。

验证`cc1`能够运行：

```bash
"$LUBANCAT_CC" -E -x c /dev/null -o /dev/null
echo $?
```

返回值应为0。该`LD_LIBRARY_PATH`只解决主机工具运行问题，不会让最终ARM64程序依赖`libisl.so.15`。

### 6.5 创建仅包含libpcap的overlay

SDK sysroot不包含libpcap。不要把板端整个`/usr/lib/aarch64-linux-gnu`追加到SDK链接路径，因为其中的`libc.so`可能覆盖SDK libc并重新形成混合环境。

```bash
export LUBANCAT_PCAP_SYSROOT=/home/zcb/sysroots/lubancat2n-pcap
mkdir -p "$LUBANCAT_PCAP_SYSROOT/usr/include/pcap"
mkdir -p "$LUBANCAT_PCAP_SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig"
cp -a /home/zcb/sysroots/lubancat2n/usr/include/pcap/. "$LUBANCAT_PCAP_SYSROOT/usr/include/pcap/"
cp -a /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/libpcap.so "$LUBANCAT_PCAP_SYSROOT/usr/lib/aarch64-linux-gnu/"
cp -a /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/libpcap.so.0.8 "$LUBANCAT_PCAP_SYSROOT/usr/lib/aarch64-linux-gnu/"
cp -a /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/libpcap.so.1.9.1 "$LUBANCAT_PCAP_SYSROOT/usr/lib/aarch64-linux-gnu/"
cp -a /home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/pkgconfig/libpcap.pc "$LUBANCAT_PCAP_SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig/"
```

检查该库目录只包含libpcap：

```bash
find "$LUBANCAT_PCAP_SYSROOT/usr/lib/aarch64-linux-gnu" -maxdepth 1 -type f -printf '%f\n'
```

其中不应出现`libc.so`、`libpthread.so`或动态加载器。

### 6.6 定向pkg-config

```bash
export PKG_CONFIG_SYSROOT_DIR="$LUBANCAT_PCAP_SYSROOT"
export PKG_CONFIG_LIBDIR="$LUBANCAT_PCAP_SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig"
unset PKG_CONFIG_PATH
```

作用分别是：

- `PKG_CONFIG_SYSROOT_DIR`把`libpcap.pc`中的`/usr/include`和`/usr/lib/...`转换成overlay中的绝对路径；
- `PKG_CONFIG_LIBDIR`阻止主机`pkg-config`误选x86_64的`libpcap.pc`；
- 清空`PKG_CONFIG_PATH`避免用户自定义搜索路径重新引入主机包。

验证：

```bash
pkg-config --modversion libpcap
pkg-config --cflags --libs libpcap
```

路径必须指向`/home/zcb/sysroots/lubancat2n-pcap`。

### 6.7 CMake配置与语法

```bash
cd /home/zcb/workspace/netflow-analyzer
cmake -S . -B "$NFA_SDK_BUILD" -G Ninja -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER="$LUBANCAT_CC" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
```

参数解释：

| 参数 | 含义 |
|---|---|
| `-S .` | 当前目录是CMake源码目录 |
| `-B "$NFA_SDK_BUILD"` | 生成物放在仓库外的独立目录 |
| `-G Ninja` | 选择Ninja生成器 |
| `-DNAME=value` | 创建或覆盖CMake缓存变量，等号两侧不能加Shell空格 |
| `CMAKE_SYSTEM_NAME=Linux` | 声明目标系统是Linux，并使CMake进入交叉编译模式 |
| `CMAKE_SYSTEM_PROCESSOR=aarch64` | 声明目标处理器架构 |
| `CMAKE_C_COMPILER=...` | 指定官方ARM64 GCC |
| `CMAKE_BUILD_TYPE=Release` | 使用优化Release配置 |
| `BUILD_TESTING=OFF` | 不构建无法在x86_64主机直接执行的ARM64测试程序 |

这里故意不设置`CMAKE_SYSROOT`。官方GCC已内置匹配的sysroot，额外覆盖可能破坏它自己的启动文件和libc选择。

构建：

```bash
cmake --build "$NFA_SDK_BUILD" -j2
```

`--build`让CMake调用已经配置好的底层构建工具；`-j2`允许两个并发任务。

### 6.8 SDK产物ABI检查

```bash
file "$NFA_SDK_BUILD/bin/netflow-analyzer"
"$LUBANCAT_TOOLCHAIN/bin/aarch64-rockchip-linux-gnu-readelf" --version-info "$NFA_SDK_BUILD/bin/netflow-analyzer" | grep -o 'GLIBC_[0-9.]*' | sort -Vu
"$LUBANCAT_TOOLCHAIN/bin/aarch64-rockchip-linux-gnu-readelf" -l "$NFA_SDK_BUILD/bin/netflow-analyzer" | grep Requesting
"$LUBANCAT_TOOLCHAIN/bin/aarch64-rockchip-linux-gnu-readelf" -d "$NFA_SDK_BUILD/bin/netflow-analyzer" | grep NEEDED
```

已验证结果：

```text
ARM aarch64
GLIBC_2.17
/lib/ld-linux-aarch64.so.1
libpcap.so.0.8
libc.so.6
```

SDK提供glibc 2.29并不意味着程序一定要求2.29。ELF只记录程序实际引用符号所需的最低版本；本项目最终只用到`GLIBC_2.17`级别的符号。

## 7. 方法二：Ubuntu通用GCC与板端完整sysroot

### 7.1 方法结构

```text
Ubuntu aarch64-linux-gnu-gcc 13
              +
从鲁班猫导出的完整头文件、启动文件、libc和libpcap
              +
GCC -B强制板端启动文件优先
              ↓
        ARM64动态链接程序
```

该方法没有使用厂商编译器，但仍然需要目标sysroot。对于动态链接的libpcap程序，只有一个交叉GCC可执行文件并不足够。

### 7.2 安装通用交叉工具链

```bash
sudo apt update
sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

验证：

```bash
aarch64-linux-gnu-gcc --version
aarch64-linux-gnu-gcc -dumpmachine
```

目标三元组应为`aarch64-linux-gnu`。

### 7.3 清理官方SDK专用环境

```bash
unset LD_LIBRARY_PATH
```

通用Ubuntu GCC不需要SDK中的旧版`libisl`和`libmpfr`。保留该路径可能让CMake或其他主机程序意外加载SDK中的旧主机库。

设置本方法变量：

```bash
export NFA_GENERIC_SYSROOT=/home/zcb/sysroots/lubancat2n
export NFA_GENERIC_BUILD=/home/zcb/build/netflow-analyzer-generic-sysroot-release
export PKG_CONFIG_SYSROOT_DIR="$NFA_GENERIC_SYSROOT"
export PKG_CONFIG_LIBDIR="$NFA_GENERIC_SYSROOT/usr/lib/aarch64-linux-gnu/pkgconfig:$NFA_GENERIC_SYSROOT/usr/share/pkgconfig"
unset PKG_CONFIG_PATH
```

### 7.4 为什么只有`CMAKE_SYSROOT`仍然失败

第一次通用GCC构建虽然设置了板端sysroot，但GCC仍从Ubuntu目录选择启动文件：

```text
/usr/aarch64-linux-gnu/lib/crt1.o
```

最终程序引用：

```text
__libc_start_main@GLIBC_2.34
```

板端只有glibc 2.30，所以动态加载器在进入`main()`之前拒绝程序。

GCC驱动不仅搜索sysroot，还拥有编译时写入的启动文件前缀。需要使用GCC自己的`-B`参数把板端目录放到搜索顺序最前面。

验证：

```bash
aarch64-linux-gnu-gcc --sysroot="$NFA_GENERIC_SYSROOT" -B"$NFA_GENERIC_SYSROOT/usr/lib/aarch64-linux-gnu/" -print-file-name=Scrt1.o
```

正确结果必须位于：

```text
/home/zcb/sysroots/lubancat2n/usr/lib/aarch64-linux-gnu/Scrt1.o
```

可分别检查：

```bash
aarch64-linux-gnu-gcc --sysroot="$NFA_GENERIC_SYSROOT" -B"$NFA_GENERIC_SYSROOT/usr/lib/aarch64-linux-gnu/" -print-file-name=crt1.o
aarch64-linux-gnu-gcc --sysroot="$NFA_GENERIC_SYSROOT" -B"$NFA_GENERIC_SYSROOT/usr/lib/aarch64-linux-gnu/" -print-file-name=crti.o
aarch64-linux-gnu-gcc --sysroot="$NFA_GENERIC_SYSROOT" -B"$NFA_GENERIC_SYSROOT/usr/lib/aarch64-linux-gnu/" -print-file-name=crtn.o
aarch64-linux-gnu-gcc --sysroot="$NFA_GENERIC_SYSROOT" -B"$NFA_GENERIC_SYSROOT/usr/lib/aarch64-linux-gnu/" -print-file-name=libc.so
```

本次探测确认板端`Scrt1.o`、`crti.o`、`crtn.o`和`libc.so`均排在Ubuntu版本之前。GCC 13自己的`crtbeginS.o/crtendS.o`仍由编译器提供，最终通过ELF检查和板端运行验证其兼容性。

### 7.5 通用GCC的CMake配置

```bash
cd /home/zcb/workspace/netflow-analyzer
cmake -S . -B "$NFA_GENERIC_BUILD" -G Ninja -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc -DCMAKE_SYSROOT="$NFA_GENERIC_SYSROOT" -DCMAKE_C_FLAGS="-B$NFA_GENERIC_SYSROOT/usr/lib/aarch64-linux-gnu/" -DCMAKE_FIND_ROOT_PATH="$NFA_GENERIC_SYSROOT" -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
```

该命令中存在两个不同层级的`-B`：

| 位置 | 解析者 | 含义 |
|---|---|---|
| `cmake ... -B "$NFA_GENERIC_BUILD"` | CMake | 指定CMake二进制构建目录 |
| `CMAKE_C_FLAGS="-B..."` | GCC | 调整GCC启动文件和库搜索前缀 |

新增参数解释：

| 参数 | 含义 |
|---|---|
| `CMAKE_SYSROOT` | 让编译和链接把板端目录视为目标根文件系统 |
| `CMAKE_C_FLAGS=-B...` | 在编译及链接命令中让板端启动文件目录优先 |
| `CMAKE_FIND_ROOT_PATH` | 让CMake包、头文件和库搜索以板端sysroot为根 |
| `MODE_PROGRAM=NEVER` | CMake、pkg-config等可执行工具仍在x86_64主机查找 |
| `MODE_LIBRARY=ONLY` | 库只在目标根路径中查找 |
| `MODE_INCLUDE=ONLY` | 头文件只在目标根路径中查找 |
| `MODE_PACKAGE=ONLY` | CMake包配置只在目标根路径中查找 |

这是交叉编译中“构建程序”和“目标依赖”分离的关键：

```text
程序工具从主机找，头文件和库从目标sysroot找。
```

构建：

```bash
cmake --build "$NFA_GENERIC_BUILD" -j2
```

### 7.6 通用GCC产物ABI检查

```bash
file "$NFA_GENERIC_BUILD/bin/netflow-analyzer"
aarch64-linux-gnu-readelf --version-info "$NFA_GENERIC_BUILD/bin/netflow-analyzer" | grep -o 'GLIBC_[0-9.]*' | sort -Vu
aarch64-linux-gnu-readelf -l "$NFA_GENERIC_BUILD/bin/netflow-analyzer" | grep Requesting
aarch64-linux-gnu-readelf -d "$NFA_GENERIC_BUILD/bin/netflow-analyzer" | grep NEEDED
```

已验证结果：

```text
ARM aarch64 PIE
GLIBC_2.17
/lib/ld-linux-aarch64.so.1
libpcap.so.0.8
libc.so.6
```

这证明不使用官方SDK也能够成功，但前提是完整控制目标sysroot和GCC启动文件选择。

## 8. 上传、校验和板端运行

### 8.1 官方SDK产物

```bash
scp /home/zcb/build/netflow-analyzer-lubancat-sdk-release-v2/bin/netflow-analyzer cat@192.168.1.102:/home/cat/bin/netflow-analyzer-cross-sdk
```

本次验证时的SHA-256：

```text
12bfa1b5a1735390529b7685168c632a074d00a0384d878fde3c5ffad2ffc9cd
```

### 8.2 通用GCC产物

```bash
scp /home/zcb/build/netflow-analyzer-generic-sysroot-release/bin/netflow-analyzer cat@192.168.1.102:/home/cat/bin/netflow-analyzer-cross-generic
```

本次验证时的SHA-256：

```text
fc7ffa55fdd491428af40d15dc019d8944040a019d71838d925c1cf45b8e1f35
```

### 8.3 板端通用检查

以下命令中的文件名根据要验证的方法替换：

```bash
chmod 755 /home/cat/bin/netflow-analyzer-cross-sdk
sha256sum /home/cat/bin/netflow-analyzer-cross-sdk
file /home/cat/bin/netflow-analyzer-cross-sdk
ldd /home/cat/bin/netflow-analyzer-cross-sdk
/home/cat/bin/netflow-analyzer-cross-sdk --help
```

程序放在`/home/cat/bin`而不是VFAT的`/media/usb0`，因为ext4支持Linux执行位和符号链接语义。

### 8.4 实时ICMP验收

鲁班猫终端：

```bash
sudo /home/cat/bin/netflow-analyzer-cross-sdk --interface eth0 --count 4 --filter "icmp and host 192.168.1.102"
```

虚拟机另一个终端：

```bash
ping -c 2 192.168.1.102
```

通用GCC产物只需把程序名替换为`netflow-analyzer-cross-generic`。

应用验收重点：

```text
Total packets: 4
Processing results: complete=4 truncated=0 malformed=0 unsupported=0 flow_rejected=0
Flow summary: 1 flow(s)
```

`Capture received packets`可能大于应用的4包，这是libpcap/Linux捕获后端统计语义，不影响本次ABI和运行验证。

## 9. 测试分工

交叉配置使用：

```text
BUILD_TESTING=OFF
```

原因不是放弃测试，而是x86_64主机不能直接执行生成的ARM64测试程序。当前测试矩阵为：

| 层次 | 验证内容 |
|---|---|
| x86_64 Debug + 17项CTest | 协议解析、流表、指标和端到端逻辑回归 |
| ARM64 Release交叉构建 | 编译器、头文件、链接和架构可移植性 |
| `file`／`readelf` | ELF架构、加载器、共享库和GLIBC符号版本 |
| SHA-256 | 传输前后文件完整性 |
| 板端`ldd`与`--help` | 动态加载和基础进程启动 |
| 板端实时ICMP | libpcap、BPF、协议解析、流表和输出完整运行链 |

如果以后希望在x86_64 CI中直接运行ARM64单元测试，可评估QEMU user-mode和CMake交叉运行模拟器，但它不能替代真实网卡、权限、驱动和性能测试。

## 10. 实际故障速查

### 10.1 `No build directory specified for -B`

原因：`$NFA_SDK_BUILD`或`$NFA_GENERIC_BUILD`为空，通常发生在新开终端后。

检查：

```bash
printf '<%s>\n' "$NFA_GENERIC_BUILD"
```

解决：重新执行对应方法的`export`命令，或在CMake命令中使用绝对路径。

### 10.2 `libisl.so.15: cannot open shared object file`

原因：官方SDK的x86_64 `cc1`依赖旧版主机库，Ubuntu 24.04默认搜索路径中不存在。

解决：

```bash
export LD_LIBRARY_PATH="$LUBANCAT_TOOLCHAIN/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

不要把SDK库复制到系统`/usr/lib`。

### 10.3 板端报告`GLIBC_2.34 not found`

原因：通用GCC选择了Ubuntu 24.04的启动文件，而不是板端`crt1.o/Scrt1.o`。

检查：

```bash
aarch64-linux-gnu-readelf --version-info 程序路径 | grep GLIBC
```

解决：设置完整板端`CMAKE_SYSROOT`，并通过GCC `-B`让板端启动文件优先。不要升级或手工替换鲁班猫的`libc.so.6`。

### 10.4 CMake提示编译器unknown或broken

应查看CMake输出中真正失败的内部命令。本次不是ARM编译错误，而是x86_64的`cc1`无法加载`libisl.so.15`。修复环境后应使用新构建目录，避免失败的编译器探测缓存干扰。

### 10.5 GCC 9报告`result may be used uninitialized`

旧GCC在Release优化下无法证明所有分支均给局部处理结果赋值。调用者已有初值，现有成功路径也会赋值，但为了让默认成功语义显式且跨编译器无告警，局部变量初始化为：

```c
runtime_metrics_packet_result_t result =
        RUNTIME_METRICS_PACKET_RESULT_COMPLETE;
```

修改后x86_64的17项CTest通过，两种ARM64交叉构建和板端运行均成功。

## 11. 方法选择和维护

推荐策略：

1. 日常开发在x86_64运行完整CTest；
2. 板端原生构建用于区分源码兼容性和交叉工具链问题；
3. 官方SDK方法作为主要ARM64 Release方案；
4. 通用GCC加板端sysroot作为可解释的备用方案和工具链学习材料；
5. 每次发布都执行`file`、`readelf`、SHA-256和板端运行检查。

官方SDK方法的优点：

- 编译器、启动文件和目标libc由厂商配套；
- 更接近BSP和固件构建环境；
- 版本固定后更容易复现。

通用GCC方法的优点：

- 不依赖厂商工具链；
- 清楚展示sysroot、启动文件、libc和第三方库的关系；
- 厂商SDK不可用时仍有备用路径。

两种方法当前都依赖本机绝对路径和手动环境变量。下一次工程化应把稳定参数整理为CMake toolchain文件或Preset，并提供环境检查脚本。SDK、sysroot、构建目录和可执行文件体积较大且可重建，不应提交到Git；仓库只保存构建说明、工具链文件、版本信息和必要校验逻辑。
