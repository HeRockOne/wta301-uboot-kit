# A/B 槽分区地址核实表（2026-11-09）

> 用途：网页刷写双系统之前，把「代码里的常量」与「flash 里的实物」逐项对上。
> 任何一项不符就是刷砖事故，所以这张表只写实测值。
>
> 证据来源（都是本机已有文件，随时可复算）：
> · `work/backup/flash_full_128M.bin` —— 出厂全盘镜像（唯一物理基线）
> · `work/backup/mtd4_normal_boot.bin` / `mtd5_kernel.bin` / `mtd6_rootfs.bin` —— A 槽
> · `work/backup/mtd9_normal_boot_bak.bin` / `mtd10_kernel_bak.bin` /
>   `mtd11_rootfs_bak.bin` —— B 槽
> · 板上 `/proc/mtd`（A 槽启动时 B 段名字 = `normal_boot_bak` / `kernel_bak` /
>   `rootfs_bak` / `firmware_bak`）

---

## 1. 四段物理地址（两槽各一套）

| 段 | B 槽（image_1） | A 槽（image_2） | 各自长度 |
|---|---|---|---|
| u-boot | `0x00700000` | `0x04700000` | `0x00040000` |
| TP 头 | `0x00800000` | `0x04800000` | `0x00020000` |
| kernel | `0x00880000` | `0x04880000` | 声明 `0x00040000`，**窗口 `0x00740000`** |
| rootfs | `0x00FC0000` | `0x04FC0000` | 槽 `0x00C00000`，校验段常见 `0xB00000`～`0xC00000` |

两槽**内部间距完全一致**，只是整体平移 `0x4000000`：

```
u-boot → TP 头    +0x00100000
TP 头 → kernel    +0x00080000
kernel → rootfs   +0x00740000
```

## 2. 逐项核对：代码常量 vs 实物 TP 头

从 `flash_full_128M.bin` 直接读两个槽的 TP 头（`+0x48..+0x64` 是 6 个几何字段，
一级 u-boot 只校验这 6 项，见反汇编 `checkFirmwarePartition 0x4a960ffc`）：

| 字段 | 偏移 | B 槽实物 | A 槽实物 | 代码常量 |
|---|---|---|---|---|
| uboot_off | `+0x48` | `0x00700000` | `0x04700000` | `WTA301_B_UBOOT_OFF` / `WTA301_UBOOT_OFF` ✓ |
| uboot_len | `+0x4c` | `0x00040000` | `0x00040000` | `TPIMG_UBOOT_SLOT_LEN` ✓ |
| tphdr_off | `+0x50` | `0x00800000` | `0x04800000` | `WTA301_B_TPHDR_OFF` / `WTA301_TPHDR_OFF` ✓ |
| tphdr_len | `+0x54` | `0x00020000` | `0x00020000` | `TPIMG_HDR_LEN` ✓ |
| kernel_off | `+0x58` | `0x00880000` | `0x04880000` | `WTA301_B_KERNEL_OFF` / `WTA301_KERNEL_OFF` ✓ |
| rootfs_off | `+0x60` | `0x00FC0000` | `0x04FC0000` | `WTA301_B_ROOTFS_OFF` / `WTA301_ROOTFS_OFF` ✓ |

**6/6 全部一致。**

### 顺带记下两个容易记错的值

| | B 槽实物 | A 槽实物 |
|---|---|---|
| `+0x5c` kernel 区声明长 | `0x00400000` | `0x00400000` |
| `+0x64` rootfs 校验长 | `0x00C00000` | `0x00C00000`（出厂头） |
| `+0x90` md5(u-boot 段) | `76058d14…`（原厂） | `94867ec5…`（原厂） |
| `+0xa0` md5(kernel‖rootfs) | `c5c7dce6…` | `bbcba1b2…` |

> ⚠️ `+0x5c` 是「kernel 区长度」，不是 kernel 文件大小；`+0x64` 是「参与 md5 的
> rootfs 长度」，可以小于 rootfs 槽大小（OpenWrt 的包用 `0xB00000`，出厂用 `0xC00000`）。
> 这两个都在 `WTA301_VERIFY_MIN..MAX` 区间内就被接受。

## 3. 窗口与间隙（为什么 kernel 能到 0x740000）

```
B: kernel 0x0880000 ──0x400000──▶ 0x0C8000 ▸ 间隙 0x340000 ▸ rootfs 0x0FC0000
A: kernel 0x4880000 ──0x400000──▶ 0x4C8000 ▸ 间隙 0x340000 ▸ rootfs 0x4FC0000
```

从 kernel_off 到 rootfs_off = `0x740000`，中间 `0x340000` 那段**没有独立 mtd 分区**
（`/proc/mtd` 的 `kernel`/`kernel_bak` 只报 `0x400000`），但 raw `nand write` 写得进去。
OpenWrt 的包就是靠这段把 kernel 涨到 `0x420000`～`0x480000` 的。

源码里用编译期断言锁死两槽等宽：

```c
#if WTA301_B_KERNEL_MAX != WTA301_KERNEL_MAX
#error "B slot kernel window != A slot kernel window"
#endif
```

## 4. 边界安全（不会写残相邻区域）

| 检查 | B 槽实算 | 结论 |
|---|---|---|
| kernel 末端 ≤ rootfs 起点 | `0x0880000 + 0x740000 = 0x0FC0000` | 恰好够，不重叠 |
| rootfs 末端 ≤ 下一段 | `0x0FC0000 + 0xC00000 = 0x1BC0000` | 远小于 `boot_flag @0x04000000` ✓ |
| u-boot 段不碰一级 | `0x00700000 + 0x40000 = 0x00740000` | 一级在 `0x0..0x3C0000`，不相邻 ✓ |
| A 槽 rootfs 末端 | `0x4FC0000 + 0xC00000 = 0x5BC0000` | 128 MiB 内 ✓ |

**一级 `factory_boot`（`0x0..0x3C0000`）在任何路径下都不会被算到**：
写地址只能来自 `tpimg_slots[]` 白名单，而表里只有上表那 8 个数字。

## 5. 上传文件没有任何机会指定写哪里

`tpimg_parse()` 的几何校验从「只认 A 槽」改成「A 或 B 槽」，但仍然是**白名单**：

```c
slot = tpimg_match_geometry(h);   /* 逐项比对 6 个字段 */
if (!slot) { *err = "wrong_partition_geometry"; return -1; }
```

一个把 kernel_off 改成 `0x0` 的包，两套几何都不匹配 ⇒ 在校验阶段就被拒，
`nand erase` 一次都不会执行。

## 6. 复算方法

```bash
python - <<'PY'
import struct
f = open("work/backup/flash_full_128M.bin","rb")
for tag, off in (("B", 0x0800000), ("A", 0x4800000)):
    f.seek(off); h = f.read(0x200)
    be = lambda o: struct.unpack(">I", h[o:o+4])[0]
    print(tag, ["0x%08X" % be(o) for o in (0x48,0x4c,0x50,0x54,0x58,0x5c,0x60,0x64)])
PY
```
