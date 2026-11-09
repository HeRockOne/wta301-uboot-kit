# THIRD-PARTY —— 本仓库的上游归属

本仓（**WTA301 u-boot kit**）不是从 codelinaro 裸树改起，而是**派生自**：

> [`chenxin527/uboot-qsdk12.5-build`](https://github.com/chenxin527/uboot-qsdk12.5-build)
> @ commit **`9ce31573`**（GPL-2.0，上游自称 "uBootKit"）

它提供了 failsafe（网页恢复）框架、`fw_dec.c` 等基础设施。
我们在其上加了 TP-Link **WTA301**（IPQ5018 + RTL8367S）的板级支持。

**这份归属没有商量的余地。** 见 README.md §1。

---

## 1. 基座两层

| 层 | 上游 | 引用 | 许可 |
|---|---|---|---|
| 外层（构建包装 + failsafe 框架） | `chenxin527/uboot-qsdk12.5-build` | **`9ce31573`** —— 精确钉住，且**由产物自证**：交付的 `u-boot.bin` 里版本横幅就是 `U-Boot 2016.01-9ce3157-dirty` | GPL-2.0 |
| 内层（QSDK 12.5 u-boot 2016.01） | CodeLinaro | 分支 `NHSS.QSDK.12.5`，等于发布标签 `AU_LINUX_QSDK_NHSS.QSDK.12.5_TARGET_ALL.12.5.2783.2994`。**commit 未固定** | GPL-2.0+ |

`u-boot-2016/` 源码树**不进本仓**（体积原因），按 README.md §3 自行取。
本仓只放「我们改了什么」。

---

## 2. `files/` 里实际有 4 个不同上游

「我们的改动」不等于「我们写的」。逐文件说明见 `CHANGES.md` §2.2。

| 版权持有人 | 文件 | 来历 |
|---|---|---|
| **chenxin527**（2026） | `board/qca/arm/common/failsafe.c`<br>`board/qca/arm/common/fw_dec.c`<br>`failsafe/failsafe.c`<br>`failsafe/modules/modules.h`<br>`include/failsafe/fw_dec.h` | uBootKit 框架 |
| **MediaTek Inc. / Weijie Gao**（2022） | `failsafe/modules/upgrade.c` | **OpenWrt 原版 failsafe**（不是 chenxin527 的） |
| **The Linux Foundation / Qualcomm**（2013–2021） | `board/qca/arm/ipq5018/ipq5018.c`<br>`drivers/net/ipq5018/ipq5018_gmac.c`<br>`board/qca/arm/common/cmd_bootqca.c` 等 | QSDK 原始代码 |
| **Google Inc.**（2009） | `arch/arm/cpu/armv7/qca/common/smem.c` 中的片段 | U-Boot / Linux ARM 渊源 |

### 2.1 四个文件的版权头缺失（诚实告知）

```
board/qca/arm/common/tplink_tpimg.c
include/failsafe/tplink_tpimg.h
drivers/net/ipq5018/rtl8367s.c
failsafe/modules/tplinkinfo.c
```

这四个文件**没有任何 copyright 头**，只有 `// SPDX-License-Identifier: GPL-2.0`。

它们是在 chenxin527 的通用 TP-Link 代码基础上改的，但我们**没有保留基座
commit 的副本**，所以无法逐行判断哪些是原作者写的、哪些是我们加的 ——
因此**不往上面加我们的署名**（那可能是错的）。

⚠️ **如果你要重用这四个文件，请自行追溯来源**，不要把它们当成我们的原创。

### 2.2 每个文件的许可声明长什么样（不是统一 SPDX）

`files/` 下的 23 个文件**不是同一个年代、也不是同一套声明风格**。
逐个数过（`verify-kit.sh` 第 2 节会复核）：

| 声明形式 | 数量 | 说明 |
|---|---|---|
| `SPDX-License-Identifier: GPL-2.0` | 13 | uBootKit 与 2022 年后的文件用 SPDX |
| **GPL-2.0 全文头** | 6 | u-boot 2016 时代的老文件，整段 `GNU General Public License` 正文 |
| **BSD 三条款式** | 1 | `arch/arm/cpu/armv7/qca/common/smem.c` —— 见下 |
| **无任何声明** | 3 | 见下 |

#### `smem.c` 是 BSD 式，不是 GPL

```c
/*
 * Copyright (c) 2013, 2015-2017, 2020 The Linux Foundation. All rights reserved.
 * Based on smem.c from lk.
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  * Redistributions of source code must retain the above copyright ...
```

这是 QSDK 原始代码（源自 LK / Google），**保留原许可，不改成 GPL-2.0**。
BSD-3-Clause 与 GPL-2.0 兼容（可组合进 GPL 作品），所以整仓按 GPL-2.0
分发没有冲突 —— 但这个文件本身仍然以 BSD 条款对外。

#### 三个没有声明头的文件（都是故意的，不要随手加）

| 文件 | 为什么不加 |
|---|---|
| `board/ipq5018/Kconfig` | u-boot 惯例：Kconfig 不带许可头，上游同类文件都没有 |
| `configs/ipq5018_tplink_wta301_defconfig` | defconfig 同样不带 |
| `failsafe/fsdata/index.html` | **本仓新写**的网页恢复 UI（无厂商素材），纯 HTML+JS，**没有嵌入任何第三方库**，所以不需要版权头。这个文件会被**逐字节嵌入 `u-boot.bin`**（经 LZMA 进槽镜像），改动会改变槽镜像 md5 —— 这是**预期行为**，P22 就是因为加了「启动槽」卡片而变成 `0cf778c2…`。归属在此处声明即可。 |

### 2.3 硬性约束

1. **任何版权头都不得删除。** 删任何一个即违反 GPL-2.0 §1（保留版权声明）。
   `verify-kit.sh` 会逐文件校验，删了立刻 FAIL。
2. **修改声明在 `CHANGES.md`**（GPL-2.0 §2(a) 要求）。
   改 `files/` 里任何东西时，**同一提交里更新 `CHANGES.md`**。
3. 不要把它叫「uBootKit」而不提来源。上游同名项目的作者是 chenxin527；
   对外用「WTA301 u-boot kit」或「WTA301 U-Boot 复现包」避免混淆。

---

## 3. 没有改过、但被依赖的上游代码

| 组件 | 用途 | 许可 |
|---|---|---|
| 工具链 `arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi` | 编译 | GPL / LGPL 组合 |
| U-Boot 2016.01 本体（`api/`、`arch/`、`drivers/` 里我们没动的部分） | 基座 | GPL-2.0+ |

---

## 4. 能不能开源（一句话）

**能。** 基座是 GPL-2.0 / GPL-2.0+，本仓也用 GPL-2.0（见 `LICENSE`），
许可兼容无冲突。GPL 只要求「标明 + 给源码」，不禁止分发。

**唯一的真缺口是工程问题，不是法务**：内层 codelinaro commit 未固定，
所以「逐字节可复现」做不到。GPL **不**要求 bit-identical 构建，
不构成违规，但对外应如实说明（README.md §1.2 已写）。

---

## 5. 商标

「TP-Link」「WTA301」是其各自权利人的商标，本仓仅用于**标识所适配的硬件**。
本工程与 TP-Link 无任何隶属或背书关系。