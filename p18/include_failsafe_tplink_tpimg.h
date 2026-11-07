// SPDX-License-Identifier: GPL-2.0
/*
 * TP-Link 双镜像（A/B 槽）固件头 —— 解析与重签
 *
 * 适用：TP-Link WTA301 (IPQ5018)，以及任何用同一套 level-1 双镜像校验的机型。
 *
 * 头的语义全部来自一级 u-boot 反汇编（work/out-final/asmcmp/level1_flat.bin,
 * vaddr 0x4a920000），不是猜的：
 *
 *   checkFirmwarePartition 0x4a960ffc —— 只校验 6 个字段，然后 bx lr：
 *       +0x48 uboot 槽偏移   +0x4c uboot 槽长度
 *       +0x50 本头偏移       +0x54 本头长度
 *       +0x58 kernel 偏移    +0x60 rootfs 偏移
 *   任何一项不符 -> 该槽判为无效。
 *
 *   checkImage 0x4a961298 —— 两个 item：
 *       item2: md5(flash[+0x48 .. +0x48+0x4c])          与 +0x90 比对
 *       item3: md5(flash[+0x58 .. +0x58+0x5c]         即 kernel 区，
 *                  ‖ flash[+0x60 .. +0x60+0x64])      即 rootfs 前段
 *              与 +0xa0 比对
 *
 * 由此得到两条容易被弄错的事实（已在真机上逐字节验证）：
 *
 *   1) +0x5c 是「kernel 区长度」（含尾部 0xFF 补齐到 0x20000 的整数倍），
 *      不是 kernel 文件大小。
 *   2) +0x64 是「参与 md5 校验的 rootfs 长度」，**小于** rootfs 槽大小。
 *      本板槽是 0xC00000，但原厂与 OpenWrt 的 +0x64 都是 0xB00000 ——
 *      后 1 MiB 是 rootfs_data/UBI 起点，会被运行时覆写，所以不参与校验。
 *
 * 工作目录里 work/mk_op_pack.py 生成的三件套就是按这个语义造的，
 * 用它算出的 md5 与一级一致（实测 +0xa0 = md5(kernel ‖ rootfs[:0xb00000])）。
 */

#ifndef __TPLINK_TPIMG_H__
#define __TPLINK_TPIMG_H__

#include <common.h>

/* ---- 头上各字段的偏移 ---- */
#define TPIMG_F_UBOOT_OFF       0x48
#define TPIMG_F_UBOOT_LEN       0x4c
#define TPIMG_F_TPHDR_OFF       0x50
#define TPIMG_F_TPHDR_LEN       0x54
#define TPIMG_F_KERNEL_OFF      0x58
#define TPIMG_F_KERNEL_LEN      0x5c
#define TPIMG_F_ROOTFS_OFF      0x60
#define TPIMG_F_ROOTFS_VERIFY   0x64
#define TPIMG_F_MD5_UBOOT       0x90    /* md5(u-boot 槽)，16 字节 */
#define TPIMG_F_MD5_PAYLOAD     0xa0    /* md5(kernel 区 ‖ rootfs 前段)，16 字节 */

#define TPIMG_HDR_LEN           0x20000
#define TPIMG_ERASE_BLOCK       0x20000

/* 头 magic：00 00 01 00 55 aa 9d d1（前 8 字节，逐字节比较） */
extern const u8 tpimg_magic[8];

/* ---- WTA301 A 槽几何（真机备份 + 反汇编实证） ---- */
#define WTA301_UBOOT_OFF        0x04700000u
#define WTA301_UBOOT_LEN        0x00040000u
#define WTA301_TPHDR_OFF        0x04800000u
#define WTA301_TPHDR_LEN        0x00020000u
#define WTA301_KERNEL_OFF       0x04880000u
#define WTA301_ROOTFS_OFF       0x04FC0000u
#define WTA301_ROOTFS_SLOT      0x00C00000u
#define WTA301_KERNEL_MAX       (WTA301_ROOTFS_OFF - WTA301_KERNEL_OFF)  /* 0x740000 */
#define WTA301_VERIFY_MIN       0x00B00000u
#define WTA301_VERIFY_MAX       WTA301_ROOTFS_SLOT

/* 一块 flash 上能出现的镜像总长上限（头 + 内核区 + rootfs 槽） */
#define WTA301_IMAGE_MAX        (TPIMG_HDR_LEN + WTA301_KERNEL_MAX + WTA301_ROOTFS_SLOT)

struct tpimg_info {
	u32 uboot_off, uboot_len;
	u32 tphdr_off, tphdr_len;
	u32 kernel_off, kernel_len;
	u32 rootfs_off;
	u32 rootfs_verify_len;   /* = 头上 +0x64，参与 md5 的长度 */
	ulong rootfs_len;        /* 上传文件里实际带的 rootfs 字节数 */
	ulong rootfs_write_len;  /* 实际写入长度（对齐到擦除块） */
	const u8 *kernel;
	const u8 *rootfs;
};

/*
 * 解析 + 自检上传的 TP 镜像。
 * 返回 0 成功；-1 失败并把简短原因写进 *err（供 JSON 输出用），
 * 详细信息已经 printf 到串口。
 *
 * 上传布局（唯一接受的形式）：
 *      [0x20000 头][kernel 区 kernel_len 字节][rootfs rootfs_len 字节]
 *      总长度 = 0x20000 + kernel_len + rootfs_len
 */
int tpimg_parse(const void *addr, ulong size, struct tpimg_info *info,
		const char **err);

/*
 * 解析 TP-Link **官网原包**（易展容器，magic 55aa4c5e）。
 *
 * 用途：让网页刷机也能吃官网下载的原厂固件，不用先在 PC 上用
 * work/out-final/mk_pack_from_factory.py 转一次。
 *
 * 包内布局（实测 work/out-final/fw_202.bin，2.0.2）：
 *     0x000200  LZMA-Alone 原厂 u-boot（本函数不写这个，也不校验它）
 *     0x060200  kernel FIT
 *     0x460200  rootfs squashfs
 *
 * 成功时填好的 info 与 tpimg_parse() 的产出**语义完全一致**，
 * 所以后续 tpimg_build_header()/tpimg_write() 无需任何改动。
 * 唯一差别：kernel_len 固定为 0x400000（包内 kernel 窗口），
 * 保证 kernel‖rootfs 在内存里连续（build_header 算 md5 靠这个前提）。
 */
int tpimg_parse_factory(const void *addr, ulong size, struct tpimg_info *info,
			const char **err);

/*
 * 生成要写进 flash 的头：
 *   以 flash 上现行的头为底本，只改 +0x5c / +0x64 / +0xa0，其余原样保留。
 *
 * 为什么不拿上传来的头直接写 —— +0x90 是 u-boot 槽的 md5，而上传文件里
 * 不含 u-boot 槽，无法自算。现行头能从 A 槽启动起我们自己，就证明它的
 * +0x90 是对的，保留它比信任上传者更安全。
 *
 * out 必须指向 TPIMG_HDR_LEN 字节的可写缓冲。
 */
int tpimg_build_header(const struct tpimg_info *info, u8 *out,
		const char **err);

/* 生成写入命令并执行（供 web failsafe 与 tplflash 命令共用） */
int tpimg_write(const struct tpimg_info *info, u8 *hdr_buf);

/* ==================== 二级 u-boot 槽（0x4700000） ====================
 *
 * 为什么需要专门一套：一级 u-boot 把 0x4700000 的 0x40000 字节整块当
 * LZMA-Alone 流解压到 0x49900000（cap 0xa0000）后直接 blx。所以槽里必须
 * 是「压缩过的 u-boot.bin + 0xFF 补齐」，不能是裸 u-boot.bin（538KB 也
 * 根本装不进 256KB 的槽）。一级对解压失败**没有**裸镜像回退：它会打一行
 * 就照样 blx 过去。因此：
 *   - 上传给网页的文件 = 已压好的 0x40000 槽镜像（由 work/mk_uboot_slot.py 生成）
 *   - 板子这边用自带的 LZMA 解码器把它解一遍做校验（能解出合法 u-boot 才写）
 *   - 写之前先确认 image_1 备用槽是好的，不然一旦新镜像坏了就没退路
 */

#define TPIMG_UBOOT_SLOT_ADDR   0x04700000u  /* image_2（我们） */
#define TPIMG_UBOOT_SLOT_LEN    0x00040000u
#define TPIMG_UBOOT_DEC_CAP     0x000A0000u  /* 一级的解压上限 655360 */
#define TPIMG_UBOOT_DEC_ADDR    0x49900000u  /* 一级的解压目标 */

/* image_1（原厂备用槽）：hdr 0x800000 / uboot 0x700000 */
#define TPIMG_B_SLOT_ADDR       0x00700000u
#define TPIMG_B_HDR_ADDR        0x00800000u

/* 校验上传的槽镜像。返回 0 = 可以写。 */
int tpimg_uboot_validate(const void *addr, ulong size, const char **err);

/* 写入 u-boot 槽并重签 +0x90。hdr_buf 需 0x20000 字节。 */
int tpimg_uboot_write(const void *addr, u8 *hdr_buf, const char **err);

/* 全分区自检：A/B 两个槽、两个头、kernel/rootfs 的 md5 全对一遍。
 * 返回 0 表示全部一致；非 0 表示有项不符（已 printf 到串口）。*/
int tpimg_selfcheck(void);

/* ---- 从 TP 槽启动内核 ----
 *
 * 原厂 kernel 的 /chosen/bootargs 只有 “console=ttyMSM0,115200,n8 rw init=/init”
 * （从 work/backup/mtd5_kernel.bin 里挖出来的），**没有 root=**：
 * 原厂布局是裸 kernel + 裸 squashfs，内核自己的 init 找并挂载 rootfs。
 * 所以启动不需要 UBI，只要 nand read + bootm。
 *
 * ★ 不要照抄原厂的 init=/init（实测定案，2026-11-03）
 *   原厂 rootfs 里有 /init（OpenWrt 系老布局），但我们跑的 OpenWrt 主线
 *   rootfs 只有 /sbin/init，没有 /init。硬写 init=/init 会让内核走到
 *       Run /init as init process
 *       Kernel panic - not syncing: Requested init /init failed (error -2)
 *   因为 kernel_init() 对 cmdline 指定的 init 失败是直接 panic，不 fallback。
 *   故此处不指定 init=，让内核用默认搜索顺序
 *   (/sbin/init, /etc/init, /bin/init, /bin/sh)。原厂和 OpenWrt 都有
 *   /sbin/init，两边都能启动。
 *
 * bootargs 可通过环境变量 tplbootargs 覆盖（OpenWrt 等镜像需要不同的串）。
 *   注意：tplbootargs 只存在 RAM 里，reset 即丢；要持久化只能改这里重编。
 * 返回 0 = 已成功 bootm（不会返回）；非 0 = 没启动起来，调用方继续走 failsafe。
 */
int tpimg_boot_slot(void);

/* 内核读入的内存地址与可覆盖的默认 bootargs */
#define TPIMG_BOOT_LOAD_ADDR    0x44000000u
#define TPIMG_BOOT_ARGS_DEFAULT \
	"console=ttyMSM0,115200,n8 rw rootwait"

#endif /* __TPLINK_TPIMG_H__ */
