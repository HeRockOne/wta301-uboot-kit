# CHANGES —— 相对上游的修改记录

本文件回答 GPL-2.0 要求的两个问题：

- **§2(a)** 改过的文件要有「我改了它」的显著声明 → 见 §2
- **§3** 分发二进制时要能给出对应源码 → 见 §3

---

## 1. 来历

本目录（`patches/uboot/files/`）是一份 **overlay**：按 u-boot 树内相对路径存放
23 个文件，覆盖到基座上即得到我们编译用的树。

基座有两层，**都不是我们写的**：

| 层 | 项目 | 许可 |
|---|---|---|
| 外层（含 failsafe 网页恢复框架） | `chenxin527/uboot-qsdk12.5-build` @ `9ce31573` | GPL-2.0 |
| 内层（QSDK 12.5 u-boot 2016.01） | CodeLinaro `NHSS.QSDK.12.5`<br>= 发布标签 `AU_LINUX_QSDK_NHSS.QSDK.12.5_TARGET_ALL.12.5.2783.2994` | GPL-2.0+ |

我们做的事：在这两层之上加 **TP-Link WTA301（IPQ5018 + QCN6122 + RTL8367S）**
的板级支持。

---

## 2. 修改声明（GPL-2.0 §2(a)）

> **本目录全部 23 个文件均已由本工程修改或新增。**
> 修改日期：2026 年 10 月–11 月。
> 修改者：WTA301 OpenWrt 移植工程。
> 原始版权与许可见各文件头，以及本仓 `THIRD-PARTY.md` §2。

### 2.1 必须诚实说明的一点

**我们无法逐行区分哪些是 chenxin527 写的、哪些是我们写的。**

原因：构建基座 `chenxin527/uboot-qsdk12.5-build` 的 commit 有记录（`9ce31573`），
但**内层 codelinaro 的 commit 当时没有记录**。没有那个精确基线，就无法做
逐行 diff 归因。

因此：

- 我们**不给这些文件加自己的 `Copyright` 头**（加错了比不加更糟）；
- 只在文件里已有的版权头原样保留，许可统一为 **GPL-2.0**；
- 归属判断依据是文件头里已存在的声明，不是我们的猜测。

如果你需要精确归因，请用 `9ce31573` + QSDK 发布标签重建基座后 `diff`。

### 2.2 逐文件说明

「我们的改动」一列是**功能层面的描述**，不是行数统计。

| 文件 | 行数 | 我们的改动 |
|---|---|---|
| `arch/arm/dts/ipq5018-tplink-wta301.dts` | 323 | **新增**。WTA301 板级 DTS：内存、NAND 分区、GMAC、RTL8367S、按键、LED |
| `arch/arm/dts/Makefile` | 494 | 加一行，把上面那个 dtb 纳入编译 |
| `configs/ipq5018_tplink_wta301_defconfig` | 360 | **新增**。板级 defconfig |
| `board/ipq5018/Kconfig` | 104 | 加 WTA301 的 board 条目 |
| `board/qca/arm/ipq5018/ipq5018.c` | 2284 | 板级初始化：DDR、NAND、GPIO、按键 40 秒进 failsafe、LED |
| `include/configs/ipq5018.h` | 603 | 板级宏：分区偏移、`relocaddr=0x49900000`、环境变量、`TPIMG_UBOOT_SLOT_LEN` |
| `drivers/net/ipq5018/rtl8367s.c` | 542 | **新增**。RTL8367S 交换机 bring-up（4 个前面板网口走 2.5G HSGMII trunk） |
| `drivers/net/ipq5018/ipq5018_gmac.c` | 969 | GMAC 驱动适配（failsafe 模式下要能收发） |
| `drivers/net/Makefile` | 120 | 把 `rtl8367s.o` 纳入编译 |
| `board/qca/arm/common/tplink_tpimg.c` | 1157 | TP-Link TPIMG 固件头解析/校验，**WTA301 的 8 字节 magic 与双槽布局** |
| `include/failsafe/tplink_tpimg.h` | 224 | 上面的头文件 |
| `board/qca/arm/common/fw_dec.c` | 146 | 固件解压（LZMA），OOM 修复 |
| `include/failsafe/fw_dec.h` | 107 | 上面的头文件 |
| `board/qca/arm/common/failsafe.c` | 1068 | failsafe 主流程：HTTP 服务、刷写、槽位判定 |
| `failsafe/failsafe.c` | 291 | failsafe 核心 |
| `failsafe/modules/upgrade.c` | 291 | 升级模块（**源自 MediaTek/OpenWrt 原版**，仅适配） |
| `failsafe/modules/tplinkinfo.c` | 151 | **新增**。`/sysinfo/tplink` —— 槽位判定与固件信息 API |
| `failsafe/modules/modules.h` | 47 | 模块表 |
| `failsafe/modules/Makefile` | 14 | 编译规则 |
| `failsafe/fsdata/index.html` | 1118 | **重写**。网页恢复控制台 UI（中文、自研，无厂商素材） |
| `board/qca/arm/common/cmd_bootqca.c` | 1037 | 引导命令适配 |
| `board/qca/arm/common/ethaddr.c` | 301 | MAC 地址从 ART/factory_info 读取 |
| `arch/arm/cpu/armv7/qca/common/smem.c` | 1333 | SMEM 适配（共享内存，供上面几项查询板型） |

---

## 3. 对应源码（GPL-2.0 §3）

发布包里的 `wta301-uboot-p21c.bin`（262,144 B）是**二进制**。
GPL-2.0 §3 要求随二进制提供「对应源码」（Corresponding Source）。

我们提供的对应源码由三部分组成：

| 组成 | 位置 |
|---|---|
| 我们的修改（overlay，23 文件全量） | 本目录 `files/` |
| 构建脚本 + 工具链要求 | 本目录 `build/`，`../README.md` §3 |
| 上游基座 | `chenxin527/uboot-qsdk12.5-build` @ `9ce31573`<br>+ CodeLinaro `NHSS.QSDK.12.5`<br>（公开镜像 `1980490718/u-boot-2016`） |

### 3.1 这个对应源码的强度（诚实评估）

| 项 | 状态 |
|---|---|
| overlay 完整 | ✅ 23/23 文件全量，不打 diff |
| 构建脚本完整 | ✅ `build_j1.sh` 已参数化（`UBOOT_TREE` / `TOOLCHAIN_BIN`） |
| 外层基座精确 | ✅ `9ce31573`，且**由产物自证**：`strings u-boot.bin` → `U-Boot 2016.01-9ce3157-dirty` |
| 内层基座精确 | ⚠️ 只到 QSDK 发布标签，**commit 未固定** |
| 从零重编出同 md5 | ⚠️ 未实证（见 `../README.md` §3.1） |

**结论**：对应源码在「同代基座 + overlay」的意义上是完整的——
别人拿到能编出**功能等价**的 u-boot，能刷、能救砖。
但不是「逐字节可复现」，因为内层 commit 未固定。

如果这不够，唯一的补救是把 codelinaro 的 commit 钉下来并写进 `../README.md` §1。

---

## 4. 这算合规吗

**算。** 理由：

1. 基座是 GPL-2.0 / GPL-2.0+，我们的衍生作品也用 GPL-2.0（仓库根 `LICENSE`），
   **许可兼容，无冲突**；
2. 版权头全部原样保留（`verify-kit.sh` 逐文件校验，删了会报 FAIL）；
3. 修改有声明（本文件）+ 分发二进制有对应源码（§3）；
4. 未使用任何厂商专有素材：`index.html` 是自研中文 UI，`file(1)` 确认
   `files/` 下无二进制 blob。

**唯一的实际缺口不是法务，是工程**：内层基座 commit 未固定，
导致「逐字节可复现」做不到。这不违反 GPL（GPL 只要求能给出源码，
不要求 bit-identical 构建），但对外应如实说明。

---

## 5. 命名提醒

上游 `chenxin527` 的项目自称 **"uBootKit"**。**不要**把本目录对外也叫
"uBootKit"，会被当成冒名或混淆。

建议：**WTA301 U-Boot 复现包** / **WTA301 u-boot kit**。
