# 网页刷写 A/B 双系统（P23）

> 一句话：**进 kit 网页恢复页，可以把固件刷进任意一个槽**，不用串口、
> 不用进系统跑脚本。配合 `POST /bootflag`，双系统从「装」到「切」全在浏览器里完成。

P22 只解决了「切」。P23 解决「装」。

---

## 1. 以前为什么只能刷 A 槽

不是硬件限制，是**三段写死**：

| 位置 | 原来是 | 问题 |
|---|---|---|
| `tpimg_geom[]` 白名单 | 只认 `0x4700000 / 0x4800000 / 0x4880000 / 0x4FC0000` | B 槽的包在 `tpimg_parse()` 就被拒 |
| `tpimg_write()` | `nand erase/write` 的三处地址是**常量** `WTA301_*_OFF` | 就算过了白名单也只会往 A 槽写 |
| 结尾日志 | 写死 `tpimg: A slot written` | 看不出实际写了哪 |

原厂这么设计是有意的：B 槽是**出厂回退槽**，网页升级只许动 A 槽。

## 2. P23 改了什么

### 2.1 槽几何表（唯一地址来源）

```c
static const struct tpimg_slot_geo tpimg_slots[] = {
    { "image_1 (B slot)", 0x00700000, 0x00800000, 0x00880000, 0x00FC0000 },
    { "image_2 (A slot)", 0x04700000, 0x04800000, 0x04880000, 0x04FC0000 },
};
```

写 flash 的地址**只能**从这里取。表里只有两个槽，上传文件没有机会指定别的。

### 2.2 解析：白名单从「一套」变「两套」

```c
slot = tpimg_match_geometry(h);   /* 逐项比对 +0x48..+0x60 六个字段 */
if (!slot) { *err = "wrong_partition_geometry"; return -1; }
```

一个把 kernel_off 改成 `0x0` 的包，两套都不匹配 ⇒ 校验阶段就拒，
`nand erase` 一次都不会执行。

### 2.3 写入：地址来自 `info`，且写入前再复核一次

```c
g = tpimg_slot_by_kernel(be32(hdr_buf + TPIMG_F_KERNEL_OFF));
if (!g) return -1;                       /* 不在表里就不写 */
... nand erase/write 用 g->kernel_off / g->rootfs_off / g->tphdr_off ...
```

### 2.4 重签：`+0x90` 现在**现场读 flash 算**

以前是「沿用现行头里的 +0x90」。跨槽写入时这是错的：现行头的 +0x90 描述的是
**它自己那个槽**的 u-boot，照抄过去会把目标槽签成错的摘要。

现在：读目标槽的 `0x40000` 字节 → 算 md5 → 写进 `+0x90`。
并且**先检查那个槽不是空的**：全 `0xFF` 就拒写并报 `target_uboot_blank`，
因为固件包不含 u-boot，写进去也启不了。

### 2.5 界面：固件页多了「写到哪个槽」

```
○ 按固件声明的槽      ← 默认，旧行为不变
○ 强制写 B 槽（image_1）
○ 强制写 A 槽（image_2）
```

选 B 时页面会读 `/sysinfo/tplink` 的槽位状态，B 槽 u-boot 校验不过就提前标红。

---

## 3. 完整双系统刷写流程（全部在浏览器）

前提：板子里的二级 u-boot 是 **P22 或 P23**（带 `/bootflag`）。

### 步骤 0：进恢复页

板子在 u-boot 里把自己设成 `192.168.1.1`（编译期常量
`include/configs/ipq5018.h:336`）。浏览器开 `http://192.168.1.1/`。

> kit 的恢复页只在 `bootipq` **失败**后才进得去
> （`cmd_bootqca.c:1019`）。想主动进去就在串口 `IPQ5018#` 里敲 `httpd`。

### 步骤 1：把带 `/bootflag` 的 u-boot 刷进两槽

「高级 → U-Boot」→ 选 `wta301-uboot-p22.bin`（或 P23）→ 写入目标选**两槽都写**。

这一步是前提：没有 `/bootflag` 就没有下面的「切」。

### 步骤 2：刷 B 槽（原厂改造版）

「固件升级」→ 选 `stockv3-slotB-webupload.bin` → 写入目标选 **强制写 B 槽**
→ 上传并校验 → 写入 Flash。

包里的头本来就声明 B 槽几何，所以选「按固件声明的槽」效果一样；
显式选 B 槽是为了在一堆包里不出错。

### 步骤 3：刷 A 槽（OpenWrt）

「固件升级」→ 选 `op-p33-slotA-webupload.bin` → 写入目标选 **强制写 A 槽**
→ 上传并校验 → 写入 Flash。

### 步骤 4：切

「启动槽」卡片 → 点「[ A ] 下次从 image_2 启动」→ 重启 → OpenWrt。
想回原厂就点「[ B ]」→ 重启。

---

## 4. 三种「包」的区别（别选错）

| 文件 | magic | 能不能直接上传 |
|---|---|---|
| `wta301-modv2-slotB.img` / `wta301-stockv3-slotB.bin` | ASCII `WTA301V2` | ❌ 我们自造的容器，网页不认 |
| `*-webupload.bin` | `00000100 55aa9dd1` | ✅ TP 头格式，网页认 |
| `wta301-op-p33-web.bin` | `00000100 55aa9dd1` | ✅ 同上 |

转换工具：`wta301-2512/work/stock-mod/mk_tpimg_pack.py`

```bash
python mk_tpimg_pack.py <tphdr 0x20000> <kernel> <rootfs> <out.bin>
```

它会按头里的 `+0x5c` / `+0x64` 校验长度、补 `0xFF`、并复核 `+0xa0` 自洽。

## 5. 安全边界

| 风险 | 现状 |
|---|---|
| 写残一级 `factory_boot` | 不可能：地址只来自 `tpimg_slots[]`，表里没有 `0x0` |
| 上传改装包指定任意偏移 | 不可能：几何必须匹配 A 或 B 全套六项 |
| 目标槽 u-boot 是空的 | 拒写，报 `target_uboot_blank` |
| 刷到一半断电 | 头最后写；头还是旧的 ⇒ 一级判该槽坏 ⇒ 落另一槽 |
| 目标槽 rootfs 超槽 | `tpimg_parse` 里 `rlen > 0xC00000` 直接拒 |

## 6. 未验证的部分

- ⬜ **P23 未上板**。上面的流程是按代码推的，不是实测记录。
- ⬜ 跨槽写入（B 包 → B 槽）的真实 `nand erase/write` 序列没在板子上跑过。
- ✅ 离线可验的部分：几何白名单、包自洽（`+0xa0`）、A/B 地址与出厂头逐项一致
  （见 `SLOT-LAYOUT.md`）。
