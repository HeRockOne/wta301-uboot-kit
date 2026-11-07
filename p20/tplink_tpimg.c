// SPDX-License-Identifier: GPL-2.0
/*
 * TP-Link 双镜像（A/B 槽）固件头 —— 解析与写入实现
 *
 * 头的字段语义与依据见 include/failsafe/tplink_tpimg.h 顶部的注释。
 * 一句话：所有常量都来自一级 u-boot 反汇编 + 真机 flash 逐字节核对，
 * 没有任何一项是猜的。
 */

#include <common.h>
#include <command.h>
#include <malloc.h>
#include <nand.h>
#include <u-boot/md5.h>
#include <lzma/LzmaTools.h>
#include <failsafe/failsafe.h>
#include <failsafe/tplink_tpimg.h>

const u8 tpimg_magic[8] = { 0x00, 0x00, 0x01, 0x00, 0x55, 0xaa, 0x9d, 0xd1 };

/* 网页刷 u-boot 时写入哪几个槽：0=两槽（默认） 1=只 image_1 2=只 image_2
 * 由 failsafe/modules/upgrade.c 在 /upload 阶段按前端表单设定；
 * u-boot 命令行 `tpluboot` 不走它，永远是两槽都写。 */
int tpimg_uboot_target = TPIMG_UBOOT_TARGET_BOTH;

#define TPIMG_ALIGN_UP(x, a)  (((x) + (a) - 1) & ~((a) - 1))

/* ARM32 不允许非对齐 32 位访问，逐字节拼 */
static u32 be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void put_be32(u8 *p, u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static void print_digest(const char *label, const u8 *d)
{
	printf("  %s %02x%02x%02x%02x%02x%02x%02x%02x"
	       "%02x%02x%02x%02x%02x%02x%02x%02x\n", label,
	       d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
	       d[8], d[9], d[10], d[11], d[12], d[13], d[14], d[15]);
}

/* 固件头里描述「分区几何」的字段：任何一项不符就拒绝，绝不允许
 * 上传文件自己指定要写到哪 —— 那是能写残 factory_boot 的口子。 */
static const struct {
	u32 field_off;
	u32 want;
	const char *name;
} tpimg_geom[] = {
	{ TPIMG_F_UBOOT_OFF,  WTA301_UBOOT_OFF,  "uboot offset"  },
	{ TPIMG_F_UBOOT_LEN,  WTA301_UBOOT_LEN,  "uboot length"  },
	{ TPIMG_F_TPHDR_OFF,  WTA301_TPHDR_OFF,  "tphdr offset"  },
	{ TPIMG_F_TPHDR_LEN,  WTA301_TPHDR_LEN,  "tphdr length"  },
	{ TPIMG_F_KERNEL_OFF, WTA301_KERNEL_OFF, "kernel offset" },
	{ TPIMG_F_ROOTFS_OFF, WTA301_ROOTFS_OFF, "rootfs offset" },
};

int tpimg_parse(const void *addr, ulong size, struct tpimg_info *info,
		const char **err)
{
	const u8 *h = addr;
	ulong avail, rlen;
	u32 klen, vlen;
	int i;

	memset(info, 0, sizeof(*info));

	if (!addr || size < TPIMG_HDR_LEN) {
		*err = "too_short";
		printf("tpimg: file too short (%lu, need >= 0x%x)\n",
		       size, TPIMG_HDR_LEN);
		return -1;
	}

	if (memcmp(h, tpimg_magic, 8)) {
		*err = "bad_magic";
		printf("tpimg: bad magic %02x%02x%02x%02x %02x%02x%02x%02x,"
		       " expected 00000100 55aa9dd1\n",
		       h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(tpimg_geom); i++) {
		u32 got = be32(h + tpimg_geom[i].field_off);

		if (got != tpimg_geom[i].want) {
			*err = "wrong_partition_geometry";
			printf("tpimg: header +0x%02x %s = 0x%08x,"
			       " expected 0x%08x\n",
			       tpimg_geom[i].field_off, tpimg_geom[i].name,
			       got, tpimg_geom[i].want);
			return -1;
		}
	}

	info->uboot_off  = be32(h + TPIMG_F_UBOOT_OFF);
	info->uboot_len  = be32(h + TPIMG_F_UBOOT_LEN);
	info->tphdr_off  = be32(h + TPIMG_F_TPHDR_OFF);
	info->tphdr_len  = be32(h + TPIMG_F_TPHDR_LEN);
	info->kernel_off = be32(h + TPIMG_F_KERNEL_OFF);
	info->rootfs_off = be32(h + TPIMG_F_ROOTFS_OFF);

	klen = be32(h + TPIMG_F_KERNEL_LEN);
	vlen = be32(h + TPIMG_F_ROOTFS_VERIFY);

	printf("tpimg: header says kernel 0x%x @0x%08x, rootfs verify 0x%x @0x%08x\n",
	       klen, info->kernel_off, vlen, info->rootfs_off);

	if (!klen || (klen & (TPIMG_ERASE_BLOCK - 1)) || klen > WTA301_KERNEL_MAX) {
		*err = "bad_kernel_len";
		printf("tpimg: kernel length 0x%x invalid"
		       " (must be nonzero, 0x%x-aligned, <= 0x%x)\n",
		       klen, TPIMG_ERASE_BLOCK, WTA301_KERNEL_MAX);
		return -1;
	}

	if (vlen < WTA301_VERIFY_MIN || vlen > WTA301_VERIFY_MAX ||
	    (vlen & (TPIMG_ERASE_BLOCK - 1))) {
		*err = "bad_rootfs_verify_len";
		printf("tpimg: rootfs verify length 0x%x invalid"
		       " (must be 0x%x-aligned, 0x%x..0x%x)\n",
		       vlen, TPIMG_ERASE_BLOCK, WTA301_VERIFY_MIN,
		       WTA301_VERIFY_MAX);
		return -1;
	}

	avail = size - TPIMG_HDR_LEN;
	if (avail < klen) {
		*err = "truncated_kernel";
		printf("tpimg: file has 0x%lx after header, header needs 0x%x"
		       " for kernel alone\n", avail, klen);
		return -1;
	}

	rlen = avail - klen;
	if (rlen < vlen) {
		*err = "truncated_rootfs";
		printf("tpimg: file has 0x%lx for rootfs, header needs at least"
		       " 0x%x to verify\n", rlen, vlen);
		return -1;
	}

	if (rlen > WTA301_ROOTFS_SLOT) {
		*err = "rootfs_too_big";
		printf("tpimg: rootfs 0x%lx exceeds slot 0x%x\n",
		       rlen, WTA301_ROOTFS_SLOT);
		return -1;
	}

	info->kernel     = h + TPIMG_HDR_LEN;
	info->rootfs     = info->kernel + klen;
	info->kernel_len = klen;
	info->rootfs_verify_len = vlen;
	info->rootfs_len = rlen;
	info->rootfs_write_len = TPIMG_ALIGN_UP(rlen, TPIMG_ERASE_BLOCK);

	/* 原厂刷写器只做 erase+write，不嗅探内容类型；一级反汇编也确认它只校验
	 * +0x48..+0x60 那 6 个几何字段和 md5，**不看** kernel/rootfs 的 magic。
	 * 所以这里只打印不拒绝。真写错了，一级下次启动 md5 校验不过，会自己
	 * 回退到 image_1（实测过），比在这里拦下来更省事。 */
	printf("tpimg: kernel magic 0x%08x, rootfs magic 0x%08x\n",
	       be32(info->kernel), be32(info->rootfs));

	if (rlen != info->rootfs_write_len)
		printf("tpimg: rootfs 0x%lx will be padded to 0x%lx (0xFF)\n",
		       rlen, info->rootfs_write_len);

	return 0;
}

/*
 * ===================== TP-Link 官网原包（易展容器）=====================
 *
 * 官网下载的 `*_up_boot*.bin` 头也是 `00 00 01 00`，但 +0x04 是 `55 aa 4c 5e`
 * （小端 u32 = 0x5E4CAA55），和本板 flash 里的 `55 aa 9d d1` 不同。
 * 实测（work/out-final/fw_202.bin，TP-Link 官网 2.0.2，17,302,016 B）：
 *
 *     0x000000..0x000200   512 B 包头（magic + 签名/摘要，无几何字段）
 *     0x000200..0x060000   LZMA-Alone（原厂二级 u-boot），解压出 0x5ae2c 字节
 *     0x060200..           kernel FIT，magic d00dfeed，前面 0x200 字节为 0
 *     0x460200..           rootfs squashfs，magic hsqs，同样前置 0x200
 *
 * 关键：包内是**平铺明文**，不加密。kernel 起点到 rootfs 起点的跨度恰好
 * 是 0x400000；FIT 读长度后剩下的部分实测全为 0xFF，所以把 kernel_len 定成
 * TPFACT_KERNEL_AREA，kernel‖rootfs 在内存里就是**连续的**，可直接交给
 * tpimg_build_header() 算 md5 —— 不用拷 17 MB，也不用 PC 侧转换。
 *
 * 为什么必须连续：tpimg_build_header() 算的是
 *     md5(info->kernel, kernel_len + rootfs_verify_len)
 * 它假定 kernel 后面紧跟 rootfs（我们的 TP 镜像就是这么排的）。
 */
#define TPFACT_KERNEL_OFF     0x00060200u  /* FIT 起点 */
#define TPFACT_ROOTFS_OFF     0x000460200u /* squashfs 起点 */
#define TPFACT_KERNEL_AREA    (TPFACT_ROOTFS_OFF - TPFACT_KERNEL_OFF) /* 0x400000 */

int tpimg_parse_factory(const void *addr, ulong size, struct tpimg_info *info,
			const char **err)
{
	const u8 *b = addr;
	const u8 *fit, *sqfs;
	u32 fit_len;
	u64 bytes_used;
	ulong vlen;

	memset(info, 0, sizeof(*info));

	if (!addr || size < TPFACT_ROOTFS_OFF + 0x30) {
		*err = "factory_too_short";
		printf("tpimg: factory container too short (%lu, need > 0x%x)\n",
		       size, TPFACT_ROOTFS_OFF + 0x30);
		return -1;
	}

	fit = b + TPFACT_KERNEL_OFF;
	/* DTB/FIT magic 的大端值是 0xD00DFEED（字节 d0 0d fe ed）。
	 * 注意别写成 0xEDFE0DD0 —— 那是同一串字节按小端读出来的值，
	 * fw_dec.h 的 HEADER_MAGIC_FIT 用的就是小端口径（配合 *(u32*) 原生读），
	 * 这里用 be32() 逐字节拼，口径相反，混用会永远不等 —— P19 真踩过。 */
	if (be32(fit) != 0xD00DFEED) {
		*err = "factory_no_fit";
		printf("tpimg: no FIT magic at 0x%x (got 0x%08x),"
		       " this factory layout is not the known one\n",
		       TPFACT_KERNEL_OFF, be32(fit));
		return -1;
	}

	/* FIT 头 +0x04 是大端 u32 totalsize */
	fit_len = be32(fit + 4);
	if (!fit_len || fit_len > TPFACT_KERNEL_AREA) {
		*err = "factory_kernel_too_big";
		printf("tpimg: FIT totalsize 0x%x does not fit the 0x%x"
		       " window before rootfs\n", fit_len, TPFACT_KERNEL_AREA);
		return -1;
	}

	sqfs = b + TPFACT_ROOTFS_OFF;
	if (memcmp(sqfs, "hsqs", 4)) {
		*err = "factory_no_squashfs";
		printf("tpimg: no squashfs magic at 0x%x, layout mismatch\n",
		       TPFACT_ROOTFS_OFF);
		return -1;
	}

	/* squashfs 超块 +0x28 是小端 u64 bytes_used；逐步拼，避免非对齐 64 位读 */
	bytes_used = (u64)sqfs[0x28] | ((u64)sqfs[0x29] << 8) |
		     ((u64)sqfs[0x2a] << 16) | ((u64)sqfs[0x2b] << 24) |
		     ((u64)sqfs[0x2c] << 32) | ((u64)sqfs[0x2d] << 40) |
		     ((u64)sqfs[0x2e] << 48) | ((u64)sqfs[0x2f] << 56);

	if (bytes_used < WTA301_VERIFY_MIN || bytes_used > WTA301_ROOTFS_SLOT) {
		*err = "factory_bad_rootfs_len";
		printf("tpimg: squashfs bytes_used 0x%llx out of range"
		       " (0x%x..0x%x)\n", (unsigned long long)bytes_used,
		       WTA301_VERIFY_MIN, WTA301_ROOTFS_SLOT);
		return -1;
	}

	/* 写 NAND 必须整擦除块，+0x64 也要能整块算进 md5 */
	vlen = TPIMG_ALIGN_UP((ulong)bytes_used, TPIMG_ERASE_BLOCK);
	if (vlen > WTA301_ROOTFS_SLOT) {
		*err = "factory_rootfs_too_big";
		printf("tpimg: rootfs 0x%lx aligned exceeds slot 0x%x\n",
		       vlen, WTA301_ROOTFS_SLOT);
		return -1;
	}
	if (TPFACT_ROOTFS_OFF + vlen > size) {
		*err = "factory_truncated_rootfs";
		printf("tpimg: factory file 0x%lx cannot hold rootfs 0x%lx"
		       " (need 0x%lx)\n", size, vlen,
		       (ulong)TPFACT_ROOTFS_OFF + vlen);
		return -1;
	}

	printf("tpimg: factory container detected\n");
	printf("tpimg:   FIT   0x%06x len 0x%08x\n", TPFACT_KERNEL_OFF, fit_len);
	printf("tpimg:   sqfs  0x%06x bytes_used 0x%llx -> verify 0x%lx\n",
	       TPFACT_ROOTFS_OFF, (unsigned long long)bytes_used, vlen);
	printf("tpimg:   kernel area 0x%x, kernel||rootfs is contiguous\n",
	       TPFACT_KERNEL_AREA);

	/* 头里的几何字段用本板真值（一级只让写这四个位置） */
	info->uboot_off         = WTA301_UBOOT_OFF;
	info->uboot_len         = WTA301_UBOOT_LEN;
	info->tphdr_off         = WTA301_TPHDR_OFF;
	info->tphdr_len         = WTA301_TPHDR_LEN;
	info->kernel_off        = WTA301_KERNEL_OFF;
	info->rootfs_off        = WTA301_ROOTFS_OFF;
	info->kernel_len        = TPFACT_KERNEL_AREA;
	info->rootfs_verify_len = vlen;
	info->kernel            = fit;
	info->rootfs            = sqfs;
	info->rootfs_len        = vlen;
	info->rootfs_write_len  = vlen;

	return 0;
}

int tpimg_build_header(const struct tpimg_info *info, u8 *out,
		const char **err)
{
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	size_t len = TPIMG_HDR_LEN;
	u32 old_klen, old_vlen;
	u8 sum[16];
	int ret;

	ret = nand_read(nand, WTA301_TPHDR_OFF, &len, out);
	if (ret || len != TPIMG_HDR_LEN) {
		*err = "header_read_failed";
		printf("tpimg: cannot read live header at 0x%x (ret=%d, got=%lu)\n",
		       WTA301_TPHDR_OFF, ret, (ulong)len);
		return -1;
	}

	if (memcmp(out, tpimg_magic, 8) || be32(out + TPIMG_F_KERNEL_OFF) !=
	    WTA301_KERNEL_OFF) {
		*err = "live_header_invalid";
		printf("tpimg: live header at 0x%x is not a WTA301 TP header\n",
		       WTA301_TPHDR_OFF);
		return -1;
	}

	old_klen = be32(out + TPIMG_F_KERNEL_LEN);
	old_vlen = be32(out + TPIMG_F_ROOTFS_VERIFY);
	printf("tpimg: live header kernel_len 0x%x -> 0x%x,"
	       " rootfs_verify 0x%x -> 0x%x\n",
	       old_klen, info->kernel_len, old_vlen, info->rootfs_verify_len);

	put_be32(out + TPIMG_F_KERNEL_LEN, info->kernel_len);
	put_be32(out + TPIMG_F_ROOTFS_VERIFY, info->rootfs_verify_len);

	/* +0x90（u-boot 槽 md5）不动：上传文件里没有 u-boot 槽，算不出来；
	 * 而现行头能让我们从 A 槽启动，说明它的 +0x90 已经被一级验过。 */
	md5((u8 *)info->kernel, (int)(info->kernel_len + info->rootfs_verify_len),
	    sum);
	memcpy(out + TPIMG_F_MD5_PAYLOAD, sum, 16);
	print_digest("new payload md5:", sum);
	print_digest("kept uboot md5:", out + TPIMG_F_MD5_UBOOT);

	return 0;
}

static int tpimg_run(const char *fmt, ...)
{
	char cmd[192];
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsnprintf(cmd, sizeof(cmd), fmt, args);
	va_end(args);

	if (n >= (int)sizeof(cmd)) {
		printf("tpimg: command too long\n");
		return -1;
	}

	printf("\n### %s\n", cmd);
	return run_command(cmd, 0);
}

int tpimg_write(const struct tpimg_info *info, u8 *hdr_buf)
{
	ulong kaddr = (ulong)info->kernel;
	ulong raddr = (ulong)info->rootfs;
	const char *err;
	int ret;

	ret = tpimg_build_header(info, hdr_buf, &err);
	if (ret)
		return ret;

	printf("\n********************************\n"
	       " TP-LINK FIRMWARE UPGRADING\n"
	       " DO NOT POWER OFF DEVICE\n"
	       "********************************\n");

	/* 顺序固定：kernel -> rootfs -> 头。
	 * 中途断电时头还是旧的，一级校验不过就跳另一个槽 —— 不会变砖。 */
	ret = tpimg_run("nand erase 0x%x 0x%x", WTA301_KERNEL_OFF,
			info->kernel_len);
	if (ret)
		goto fail;
	ret = tpimg_run("nand write 0x%lx 0x%x 0x%x", kaddr,
			WTA301_KERNEL_OFF, info->kernel_len);
	if (ret)
		goto fail;

	ret = tpimg_run("nand erase 0x%x 0x%lx", WTA301_ROOTFS_OFF,
			info->rootfs_write_len);
	if (ret)
		goto fail;
	ret = tpimg_run("nand write 0x%lx 0x%x 0x%lx", raddr,
			WTA301_ROOTFS_OFF, info->rootfs_write_len);
	if (ret)
		goto fail;

	ret = tpimg_run("nand erase 0x%x 0x%x", WTA301_TPHDR_OFF,
			TPIMG_HDR_LEN);
	if (ret)
		goto fail;
	ret = tpimg_run("nand write 0x%lx 0x%x 0x%x", (ulong)hdr_buf,
			WTA301_TPHDR_OFF, TPIMG_HDR_LEN);
	if (ret)
		goto fail;

	printf("\ntpimg: A slot written and header re-signed, reset to boot it\n");
	return 0;

fail:
	printf("\ntpimg: write failed at step above, A slot is now inconsistent\n"
	       "tpimg: level-1 will fall back to the other slot\n");
	return ret;
}

/* ============================ u-boot 命令 ============================ */

static u8 tplflash_hdr[TPIMG_HDR_LEN] __aligned(64);

/* ---------------- 二级 u-boot 槽 ---------------- */

/* u-boot 2016 没有 memmem，自己写一个定长版本 */
static const u8 *find_bytes(const u8 *hay, ulong hay_len,
			    const char *needle, ulong needle_len)
{
	ulong i;

	if (!needle_len || hay_len < needle_len)
		return NULL;
	for (i = 0; i + needle_len <= hay_len; i++)
		if (!memcmp(hay + i, needle, needle_len))
			return hay + i;
	return NULL;
}

/* 解压缓冲：只有校验时才 malloc，不常驻。 */
static int uboot_image_sane(const u8 *img, ulong len)
{
	u32 first;

	if (len < 0x1000 || len > TPIMG_UBOOT_DEC_CAP) {
		printf("tpimg: u-boot slot decompressed to 0x%lx, outside 0x1000..0x%x\n",
		       len, TPIMG_UBOOT_DEC_CAP);
		return 0;
	}

	/* 第一字必须是 ARM 分支指令（u-boot 的 reset 向量）。
	 * 0xEAxxxxxx = b，0xE59FF0xx = ldr pc,[pc,#..]（旧式向量表）也接受。 */
	memcpy(&first, img, 4);
	if ((first >> 24) != 0xEA && (first & 0xFFFFF000) != 0xE59FF000) {
		printf("tpimg: u-boot slot first word 0x%08x is not an ARM reset"
		       " vector\n", first);
		return 0;
	}

	/* 必须能找出自己的 banner 字符串，否则多半是别人的镜像 */
	if (!find_bytes(img, len, "U-Boot 20", 9)) {
		puts("tpimg: u-boot slot has no \"U-Boot 20\" banner string\n");
		return 0;
	}

	return 1;
}

int tpimg_uboot_validate(const void *addr, ulong size, const char **err)
{
	if (size != TPIMG_UBOOT_SLOT_LEN) {
		*err = "uboot_slot_bad_size";
		printf("tpimg: u-boot slot must be exactly 0x%x bytes, got 0x%lx\n",
		       TPIMG_UBOOT_SLOT_LEN, size);
		return -1;
	}

	/* 为什么不再解一遍 LZMA / 验 ARM 向量：
	 * 一级的反汇编里，lzmaBuffToBuffDecompress 返回非 0 时它**只打一行日志**
	 * （0x4a927d38 bl -> 0x4a927d44 puts），然后照样 blx r5 跳过去。
	 * 也就是说它自己都不验压缩流，我们多解一遍既不能阻止它跳，也挡不住
	 * 真正的风险（刷错文件）；真正的保险是 image_1 永远完好 + 先写槽后写头。
	 * 唯一必须检的就是长度，否则会写穿 0x40000 的槽。 */
	printf("tpimg: u-boot slot 0x%lx bytes, written as-is\n", size);
	return 0;
}

/* 读 A/B 头 + 比 md5。why: 写掉 image_2 之前必须确认 image_1 是完好的，
 * 否则新镜像一旦有问题，两槽全废 -> 只能 TTL。 */
/* 判断“对面的槽”是否可用（用于自检输出，不再阻断刷写）。 */
static int __maybe_unused other_slot_ok(void)
{
	u8 *hdr = tplflash_hdr;
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	size_t len = TPIMG_HDR_LEN;
	u8 *slot = NULL;
	u8 sum[16];
	int ok = 0;

	len = TPIMG_HDR_LEN;
	if (nand_read(nand, TPIMG_B_HDR_ADDR, &len, hdr) ||
	    len != TPIMG_HDR_LEN) {
		puts("tpimg: image_1 header unreadable\n");
		return 0;
	}
	if (memcmp(hdr, tpimg_magic, 8) ||
	    be32(hdr + TPIMG_F_UBOOT_OFF) != TPIMG_B_SLOT_ADDR ||
	    be32(hdr + TPIMG_F_UBOOT_LEN) != TPIMG_UBOOT_SLOT_LEN) {
		puts("tpimg: image_1 header is not a valid slot descriptor\n");
		return 0;
	}

	slot = malloc(TPIMG_UBOOT_SLOT_LEN);
	if (!slot)
		return 0;
	len = TPIMG_UBOOT_SLOT_LEN;
	if (nand_read(nand, TPIMG_B_SLOT_ADDR, &len, slot) ||
	    len != TPIMG_UBOOT_SLOT_LEN) {
		puts("tpimg: image_1 slot unreadable\n");
		goto out;
	}

	md5(slot, TPIMG_UBOOT_SLOT_LEN, sum);
	if (memcmp(sum, hdr + TPIMG_F_MD5_UBOOT, 16)) {
		puts("tpimg: image_1 slot md5 does NOT match its header\n");
		print_digest("  header :", hdr + TPIMG_F_MD5_UBOOT);
		print_digest("  on flash:", sum);
		goto out;
	}

	puts("tpimg: image_1 (0x700000) verified OK - a fallback exists\n");
	ok = 1;
out:
	free(slot);
	return ok;
}

/* 写**一个** u-boot 槽并重签它自己的头。
 *
 * slot_off : 槽载荷偏移（image_1 = 0x700000 / image_2 = 0x4700000）
 * hdr_off  : 该槽的头部偏移（image_1 = 0x800000 / image_2 = 0x4800000）
 *
 * 顺序固定：先擦写槽，最后才改头。中途断电时该槽的头仍持旧 md5 ->
 * 一级判该槽无效并改用另一个槽，不会变砖。
 */
static int tpimg_uboot_write_slot(ulong slot_off, ulong hdr_off,
				  const void *addr, const u8 *sum,
				  u8 *hdr_buf, const char **err)
{
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	size_t len = TPIMG_HDR_LEN;
	int ret;

	/* 读该槽现行头做底本，并确认它确实属于这个槽 */
	if (nand_read(nand, hdr_off, &len, hdr_buf) ||
	    len != TPIMG_HDR_LEN) {
		*err = "header_read_failed";
		return -1;
	}
	if (memcmp(hdr_buf, tpimg_magic, 8) ||
	    be32(hdr_buf + TPIMG_F_UBOOT_OFF) != slot_off) {
		*err = "slot_header_invalid";
		printf("tpimg: header @0x%lx does not describe slot 0x%lx\n",
		       hdr_off, slot_off);
		return -1;
	}

	printf("tpimg: writing slot 0x%lx (header 0x%lx)\n", slot_off, hdr_off);
	ret = tpimg_run("nand erase 0x%lx 0x%x", slot_off, TPIMG_UBOOT_SLOT_LEN);
	if (ret)
		return ret;
	ret = tpimg_run("nand write 0x%lx 0x%lx 0x%x", (ulong)addr,
			slot_off, TPIMG_UBOOT_SLOT_LEN);
	if (ret)
		return ret;

	memcpy(hdr_buf + TPIMG_F_MD5_UBOOT, sum, 16);
	ret = tpimg_run("nand erase 0x%lx 0x%x", hdr_off, TPIMG_HDR_LEN);
	if (ret)
		return ret;
	ret = tpimg_run("nand write 0x%lx 0x%lx 0x%x", (ulong)hdr_buf,
			hdr_off, TPIMG_HDR_LEN);
	if (ret)
		return ret;

	return 0;
}

int tpimg_uboot_write(const void *addr, u8 *hdr_buf, const char **err)
{
	u8 sum[16];
	int ret;
	bool w1, w2;

	/* 前端可选只写一个槽；默认两槽都写 */
	switch (tpimg_uboot_target) {
	case TPIMG_UBOOT_TARGET_IMAGE1:
		w1 = true;  w2 = false;
		break;
	case TPIMG_UBOOT_TARGET_IMAGE2:
		w1 = false; w2 = true;
		break;
	default:
		w1 = true;  w2 = true;
		break;
	}

	/* 新摘要先算好，再动 flash */
	md5((u8 *)addr, TPIMG_UBOOT_SLOT_LEN, sum);
	print_digest("tpimg: new u-boot slot md5:", sum);

	printf("\n********************************\n"
	       " UPGRADING U-BOOT (%s%s%s)\n"
	       " DO NOT POWER OFF DEVICE\n"
	       "********************************\n",
	       w1 ? "image_1" : "", (w1 && w2) ? " + " : "",
	       w2 ? "image_2" : "");

	/* ★ 两槽都刷同一个镜像（这是双槽冗余的前提）
	 *
	 * 为什么必须两槽都写：一级先验 image_1，通过就启动它。只写 image_2
	 * 的话新 kit 永远不执行。
	 *
	 * 为什么这个顺序还是安全的：每写一个槽都是「先槽、后头」，所以
	 *   - 写完 image_2 后中断：image_2 好、image_1 旧 -> 两个都能启动
	 *   - 写 image_1 中途中断：image_1 头仍旧 md5 -> 判无效 -> 回退 image_2
	 * 任何时刻至少有一个槽是完整可用的。
	 */
	ret = tpimg_uboot_write_slot(WTA301_UBOOT_OFF, WTA301_TPHDR_OFF,
				     addr, sum, hdr_buf, err);
	if (ret)
		goto fail;
	if (!w2)
		goto done;
	ret = tpimg_uboot_write_slot(TPIMG_B_SLOT_ADDR, TPIMG_B_HDR_ADDR,
				     addr, sum, hdr_buf, err);
	if (ret)
		goto fail;

done:
	if (w1 && w2)
		puts("\ntpimg: BOTH u-boot slots written and re-signed"
		     " (image_1 + image_2)\n"
		     "tpimg: dual-slot redundancy intact, reset to boot it\n");
	else
		printf("\ntpimg: only %s written and re-signed\n"
		       "tpimg: the other slot still holds the previous image\n",
		       w1 ? "image_1" : "image_2");
	return 0;

fail:
	puts("\ntpimg: u-boot write failed - the failed slot's header still holds\n"
	     "tpimg: the OLD md5, so level-1 can fall back to the other slot\n");
	return ret;
}

static int do_tplflash(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct tpimg_info info;
	const char *err = "";
	ulong addr, size;
	int do_write = 0;
	int ret;

	if (argc < 3)
		return CMD_RET_USAGE;

	addr = simple_strtoul(argv[1], NULL, 16);
	size = simple_strtoul(argv[2], NULL, 16);
	if (argc > 3 && !strcmp(argv[3], "write"))
		do_write = 1;

	ret = tpimg_parse((void *)addr, size, &info, &err);
	if (ret) {
		printf("tplflash: parse failed: %s\n", err);
		return CMD_RET_FAILURE;
	}

	printf("tplflash: image OK%s\n", do_write ? "" : " (dry run)");
	if (!do_write) {
		puts("tplflash: nothing written; append 'write' to flash\n");
		return CMD_RET_SUCCESS;
	}

	ret = tpimg_write(&info, tplflash_hdr);
	if (ret) {
		printf("tplflash: write failed (%d)\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(tplflash, 5, 0, do_tplflash,
	   "parse/write a TP-Link A-slot firmware image (header + kernel + rootfs)",
	   "<addr> <size> [write]\n"
	   "    - parse and self-check the image at <addr>, no flash access\n"
	   "    - add 'write' to erase/write kernel, rootfs and the re-signed header");

/* ============================ 自检 ============================ */

/* 自检分块读的缓冲区大小。
 *
 * 这个缓冲**不能**放 .bss：u-boot 的 DTB 地址是按
 *   dtb_base = align64K(TEXT_BASE + mon_len) + PGTABLE_SIZE + 4
 * 算的（lib/fdtdec.c:1336，mon_len = __bss_end - _start，见
 * common/board_f.c:272），所以 .bss 涨大 64KB，DTB 就被推后 64KB。
 * 而 env 里的 fdtcontroladdr 会覆盖这个计算结果（lib/fdtdec.c:1382）——
 * 一旦旧值落在新的 .bss 区间里，清零 .bss 就把 DTB 抹了，
 * 表现为启动到 failsafe 时 httpd_find_instance 里 data abort（已踩过）。
 * 所以这里改从堆上要（32KB 很小，不会像 16MB 那样 out of memory）。 */
#define MD5_CHUNK 0x8000

/* 验一个槽：头合法 + 槽内容 md5 == 头里的 +0x90。返回 1 = OK */
static int check_one_slot(const char *name, u32 hdr_addr, u32 slot_addr,
			  u8 *hdr_buf, u8 *slot_buf)
{
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	size_t len = TPIMG_HDR_LEN;
	u8 sum[16];
	int ok = 0;

	if (nand_read(nand, hdr_addr, &len, hdr_buf) || len != TPIMG_HDR_LEN) {
		printf("  %-8s header 0x%06x  UNREADABLE\n", name, hdr_addr);
		return 0;
	}
	if (memcmp(hdr_buf, tpimg_magic, 8)) {
		printf("  %-8s header 0x%06x  bad magic\n", name, hdr_addr);
		return 0;
	}

	/* 几何字段必须描述这个槽自己 */
	if (be32(hdr_buf + TPIMG_F_UBOOT_OFF) != slot_addr ||
	    be32(hdr_buf + TPIMG_F_UBOOT_LEN) != TPIMG_UBOOT_SLOT_LEN) {
		printf("  %-8s header 0x%06x  points at 0x%06x/0x%x, not 0x%06x\n",
		       name, hdr_addr, be32(hdr_buf + TPIMG_F_UBOOT_OFF),
		       be32(hdr_buf + TPIMG_F_UBOOT_LEN), slot_addr);
		return 0;
	}

	len = TPIMG_UBOOT_SLOT_LEN;
	if (nand_read(nand, slot_addr, &len, slot_buf) ||
	    len != TPIMG_UBOOT_SLOT_LEN) {
		printf("  %-8s slot   0x%06x  UNREADABLE\n", name, slot_addr);
		return 0;
	}
	md5(slot_buf, TPIMG_UBOOT_SLOT_LEN, sum);
	if (memcmp(sum, hdr_buf + TPIMG_F_MD5_UBOOT, 16)) {
		printf("  %-8s slot   0x%06x  md5 MISMATCH\n", name, slot_addr);
		print_digest("     header :", hdr_buf + TPIMG_F_MD5_UBOOT);
		print_digest("     on flash:", sum);
		return 0;
	}

	/* 槽里的 LZMA 能解出真 u-boot 才算真能用 */
	{
		u8 *dec = malloc(TPIMG_UBOOT_DEC_CAP);
		SizeT dsz = TPIMG_UBOOT_DEC_CAP;

		if (!dec)
			return 0;
		if (lzmaBuffToBuffDecompress(dec, &dsz,
					     slot_buf, TPIMG_UBOOT_SLOT_LEN - 1) ||
		    !uboot_image_sane(dec, dsz)) {
			printf("  %-8s slot   0x%06x  LZMA decode/garbage\n",
			       name, slot_addr);
			free(dec);
			return 0;
		}
		printf("  %-8s slot   0x%06x  OK  (md5 matches, decodes to 0x%lx)\n",
		       name, slot_addr, (ulong)dsz);
		free(dec);
		ok = 1;
	}

	return ok;
}

int tpimg_selfcheck(void)
{
	u8 *hdr = tplflash_hdr;
	u8 *slot = NULL;
	u8 sum[16];
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	size_t len;
	int a_ok, b_ok;
	int rc = 0;

	puts("\n=========== flash partition self-check ===========\n");

	slot = malloc(TPIMG_UBOOT_SLOT_LEN);
	if (!slot) {
		puts("out of memory\n");
		return -1;
	}

	printf("[1/3] u-boot slots\n");
	a_ok = check_one_slot("image_2", WTA301_TPHDR_OFF,
			      WTA301_UBOOT_OFF, hdr, slot);
	b_ok = check_one_slot("image_1", TPIMG_B_HDR_ADDR,
			      TPIMG_B_SLOT_ADDR, hdr, slot);
	if (!a_ok)
		rc = -1;
	if (!b_ok) {
		puts("  WARNING: no verified fallback slot - do NOT upgrade u-boot\n");
		rc = -2;
	}

	/* [2/3] image_2 的 kernel/rootfs：分块读 + 流式 md5
	 *
	 * 不能一次 malloc(klen+vlen) 再算：实测 16 MB 直接 out of memory。
	 * 用 64 KB 固定缓冲，nand_read 一块 update 一块。 */
	printf("[2/3] image_2 kernel + rootfs\n");
	len = TPIMG_HDR_LEN;
	if (!nand_read(nand, WTA301_TPHDR_OFF, &len, hdr) &&
	    len == TPIMG_HDR_LEN && !memcmp(hdr, tpimg_magic, 8)) {
		u32 klen = be32(hdr + TPIMG_F_KERNEL_LEN);
		u32 vlen = be32(hdr + TPIMG_F_ROOTFS_VERIFY);

		if (klen > WTA301_KERNEL_MAX || vlen > WTA301_VERIFY_MAX ||
		    !klen || !vlen) {
			printf("  bad geometry klen=0x%x vlen=0x%x\n", klen, vlen);
			rc = -1;
		} else {
			ulong buf;
			struct MD5Context ctx;
			u32 off;
			int err = 0;

			buf = (ulong)malloc(MD5_CHUNK);
			if (!buf) {
				puts("  out of memory for the hash buffer\n");
				return -1;
			}

			md5_starts(&ctx);

			/* 先 kernel 区（klen 字节，含尾部 0xFF 补齐）*/
			for (off = 0; off < klen && !err; ) {
				u32 n = klen - off > MD5_CHUNK ? MD5_CHUNK : klen - off;
				size_t glen = n;

				if (nand_read(nand, WTA301_KERNEL_OFF + off,
					      &glen, (void *)buf) || glen != n) {
					err = 1;
					break;
				}
				md5_update(&ctx, (void *)buf, n);
				off += n;
			}
			/* 再 rootfs 前 vlen 字节（后 1MB 是 rootfs_data，不参与校验）*/
			for (off = 0; off < vlen && !err; ) {
				u32 n = vlen - off > MD5_CHUNK ? MD5_CHUNK : vlen - off;
				size_t glen = n;

				if (nand_read(nand, WTA301_ROOTFS_OFF + off,
					      &glen, (void *)buf) || glen != n) {
					err = 1;
					break;
				}
				md5_update(&ctx, (void *)buf, n);
				off += n;
			}
			if (err) {
				puts("  nand read failed while hashing\n");
				rc = -1;
			} else {
				md5_finish(sum, &ctx);
				if (memcmp(sum, hdr + TPIMG_F_MD5_PAYLOAD, 16)) {
					puts("  kernel+rootfs md5 MISMATCH\n");
					print_digest("     header :",
						hdr + TPIMG_F_MD5_PAYLOAD);
					print_digest("     on flash:", sum);
					rc = -1;
				} else {
					printf("  kernel 0x%x + rootfs 0x%x", klen, vlen);
					puts("  md5 OK\n");
				}
			}
			free((void *)buf);
		}
	} else {
		puts("  image_2 header unreadable\n");
		rc = -1;
	}

	/* [3/3] 结论 */
	printf("[3/3] verdict: ");
	if (rc == 0)
		puts("all partitions consistent\n");
	else if (rc == -2)
		puts("image_2 OK but NO usable fallback\n");
	else
		puts("INCONSISTENT - see lines above\n");
	puts("=================================================\n\n");

	free(slot);
	return rc;
}

/* -------------------- 静默槽位状态（给网页用） -------------------- */

/* boot_flag 分区：0x4000000 / 0x20000，首 8 字节 = 魔数 55aa4246 + 当前槽号。
 * 实测值 55aa4246 00000002 出现在跑 image_2 的时候（2026-11-06）。
 * 注意它只是「一级 u-boot 上次决定启动谁」的旁证，不是本进程的身份，
 * 所以 JSON 里要带 assumed:true。 */
#define TPIMG_BOOT_FLAG_OFF	0x04000000u
#define TPIMG_BOOT_FLAG_MAGIC	0x55aa4246u

/* 读一个槽的状态，全程不往串口打正常信息（失败时打一行原因）。
 *
 * ★ 内存约束：CONFIG_SYS_MALLOC_LEN = 256K + 500K = 756 KB（ipq5018.h:188），
 *   而 u-boot 自己、httpd、tftp 都在同一个池里要内存。实测教训：
 *   同时持有 hdr(128K) + slot(256K) + dec(640K) = 1024 KB → 第三个 malloc
 *   静默失败 → decode_ok 永远 0（页面显示“解压失败”，但 md5 明明对）。
 *   所以这里不 malloc hdr（用调用者的静态缓冲），峰值降到 896 KB，
 *   与 tplcheck 的 check_one_slot() 完全一致（后者已在真机验证通过）。
 */
static void status_one_slot(int idx, struct tpimg_slot_state *st, u32 hdr_addr,
			    u32 slot_addr, u8 *hdr_buf, u8 *slot_buf)
{
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	size_t len = TPIMG_HDR_LEN;
	u8 sum[16];
	u8 *dec;
	SizeT dsz;
	int i;

	memset(st, 0, sizeof(*st));

	if (nand_read(nand, hdr_addr, &len, hdr_buf) || len != TPIMG_HDR_LEN)
		return;
	if (memcmp(hdr_buf, tpimg_magic, 8))
		return;
	if (be32(hdr_buf + TPIMG_F_UBOOT_OFF) != slot_addr ||
	    be32(hdr_buf + TPIMG_F_UBOOT_LEN) != TPIMG_UBOOT_SLOT_LEN)
		return;
	st->hdr_ok = 1;

	for (i = 0; i < 16; i++)
		sprintf(st->md5 + i * 2, "%02x", hdr_buf[TPIMG_F_MD5_UBOOT + i]);
	st->md5[32] = 0;

	len = TPIMG_UBOOT_SLOT_LEN;
	if (nand_read(nand, slot_addr, &len, slot_buf) ||
	    len != TPIMG_UBOOT_SLOT_LEN)
		return;

	md5(slot_buf, TPIMG_UBOOT_SLOT_LEN, sum);
	st->md5_match = !memcmp(sum, hdr_buf + TPIMG_F_MD5_UBOOT, 16);

	/* LZMA 解一遍：能解出合法 u-boot 才算这一槽真能用。
	 * 这步是唯一占大块的，失败原因必须上串口，否则页面上只看到
	 * 一个“解压失败”，看不出是内存不够还是流坏了。 */
	dec = malloc(TPIMG_UBOOT_DEC_CAP);
	if (!dec) {
		printf("tplinkinfo: slot %d - no memory for 0x%x decode buffer\n",
		       idx, TPIMG_UBOOT_DEC_CAP);
		return;
	}
	dsz = TPIMG_UBOOT_DEC_CAP;
	if (lzmaBuffToBuffDecompress(dec, &dsz, slot_buf,
				     TPIMG_UBOOT_SLOT_LEN - 1)) {
		printf("tplinkinfo: slot %d - LZMA decode failed\n", idx);
	} else if (!uboot_image_sane(dec, dsz)) {
		printf("tplinkinfo: slot %d - decoded 0x%lx bytes but it is not u-boot\n",
		       idx, (ulong)dsz);
	} else {
		st->decode_ok = 1;
	}
	free(dec);
}

int tpimg_get_status(struct tpimg_status *st)
{
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	u8 *slot;
	size_t len = TPIMG_HDR_LEN;
	u8 bf[16];

	if (!st)
		return -1;
	memset(st, 0, sizeof(*st));

	/* hdr 用模块级的静态 128KB（tplflash_hdr），不 split 堆。
	 * 静态缓冲的尺寸早已计入 mon_len，不会像旧注释提到的 .bss 涨大
	 * 那样把 DTB 推后 —— 它本来就在。 */
	slot = malloc(TPIMG_UBOOT_SLOT_LEN);
	if (!slot)
		return -1;

	status_one_slot(1, &st->slot[0], TPIMG_B_HDR_ADDR, TPIMG_B_SLOT_ADDR,
			tplflash_hdr, slot);
	status_one_slot(2, &st->slot[1], WTA301_TPHDR_OFF, WTA301_UBOOT_OFF,
			tplflash_hdr, slot);
	free(slot);

	/* boot_flag */
	len = sizeof(bf);
	if (!nand_read(nand, TPIMG_BOOT_FLAG_OFF, &len, bf) &&
	    len == sizeof(bf) && be32(bf) == TPIMG_BOOT_FLAG_MAGIC) {
		st->boot_flag_ok = 1;
		st->boot_flag_slot = be32(bf + 4);
	}

	/* 内核/rootfs 实际来自哪个槽：tpimg_boot_slot() 先试 image_2 再试 image_1 */
	len = TPIMG_HDR_LEN;
	if (!nand_read(nand, WTA301_TPHDR_OFF, &len, tplflash_hdr) &&
	    len == TPIMG_HDR_LEN && !memcmp(tplflash_hdr, tpimg_magic, 8) &&
	    be32(tplflash_hdr + TPIMG_F_KERNEL_LEN) &&
	    be32(tplflash_hdr + TPIMG_F_ROOTFS_VERIFY))
		st->fw_slot = 2;
	else if (!nand_read(nand, TPIMG_B_HDR_ADDR, &len, tplflash_hdr) &&
		 len == TPIMG_HDR_LEN && !memcmp(tplflash_hdr, tpimg_magic, 8) &&
		 be32(tplflash_hdr + TPIMG_F_KERNEL_LEN) &&
		 be32(tplflash_hdr + TPIMG_F_ROOTFS_VERIFY))
		st->fw_slot = 1;
	else
		st->fw_slot = 0;

	return 0;
}

static int do_tplcheck(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	(void)argc;
	(void)argv;
	return tpimg_selfcheck() ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

static int do_tpluboot(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	const char *err = "";
	ulong addr, size;
	int do_write = 0;
	int ret;

	if (argc < 3)
		return CMD_RET_USAGE;

	addr = simple_strtoul(argv[1], NULL, 16);
	size = simple_strtoul(argv[2], NULL, 16);
	if (argc > 3 && !strcmp(argv[3], "write"))
		do_write = 1;

	ret = tpimg_uboot_validate((void *)addr, size, &err);
	if (ret) {
		printf("tpluboot: %s -> refusing\n", err);
		return CMD_RET_FAILURE;
	}

	if (!do_write) {
		puts("tpluboot: dry run, nothing written;"
		     " append 'write' to install\n");
		return CMD_RET_SUCCESS;
	}

	/* 命令行 tpluboot 永远两槽都写，不受网页那套三选一影响 */
	tpimg_uboot_target = TPIMG_UBOOT_TARGET_BOTH;
	ret = tpimg_uboot_write((void *)addr, tplflash_hdr, &err);
	if (ret) {
		printf("tpluboot: write failed (%s)\n", err);
		return CMD_RET_FAILURE;
	}
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(tpluboot, 5, 0, do_tpluboot,
	   "install a compressed u-boot slot image into BOTH slots",
	   "<addr> <size> [write]\n"
	   "    - <size> must be exactly 0x40000 (LZMA-Alone + 0xFF pad)\n"
	   "    - writes image_2 (0x4700000, hdr 0x4800000) and then\n"
	   "      image_1 (0x700000, hdr 0x800000) with the same image,\n"
	   "      re-signing each header, so dual-slot redundancy holds\n"
	   "    - each slot is written payload-first, header-last: an\n"
	   "      interruption always leaves one intact slot\n"
	   "\n");

U_BOOT_CMD(tplcheck, 1, 0, do_tplcheck,
	   "verify every flash partition (both u-boot slots + A-slot payload)",
	   "\n"
	   "    - image_2/image_1 headers, slot md5s, LZMA decode\n"
	   "    - image_2 kernel + rootfs md5 against the live header");
/* ------------------------------------------------------------------ *
 *  从 TP 槽启动内核
 *
 *  依据全在原厂镜像里（work/backup/）：
 *    - 原厂 kernel 的 /chosen/bootargs = "console=ttyMSM0,115200,n8 rw init=/init"
 *      —— 没有 root= ，内核自己的 init 负责找并挂载 rootfs；
 *    - 原厂二级 u-boot 里 bootcmd=bootipq，qca 参考路径的 ubi/read 在本板
 *      smem 布局下必然失败（实测 UBI init error 22），所以不能照抄。
 *  因此这里只做三件事：nand read 内核 -> setenv bootargs -> bootm。
 * ------------------------------------------------------------------ */
static int tpl_boot_one(const char *name, u32 hdr_addr)
{
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	size_t len = TPIMG_HDR_LEN;
	u8 *h = tplflash_hdr;
	u32 koff, klen;
	char cmd[256];

	len = TPIMG_HDR_LEN;
	if (nand_read(nand, hdr_addr, &len, h) || len != TPIMG_HDR_LEN) {
		printf("tplboot: %s header unreadable at 0x%x\n", name, hdr_addr);
		return -1;
	}

	if (memcmp(h, tpimg_magic, 8)) {
		printf("tplboot: %s slot has no TP header\n", name);
		return -1;
	}

	koff = be32(h + TPIMG_F_KERNEL_OFF);
	klen = be32(h + TPIMG_F_KERNEL_LEN);

	if (!klen || klen > WTA301_KERNEL_MAX) {
		printf("tplboot: %s kernel length 0x%x out of range\n", name, klen);
		return -1;
	}

	printf("tplboot: %s kernel 0x%x @0x%08x -> 0x%08x\n",
	       name, klen, koff, TPIMG_BOOT_LOAD_ADDR);
	if (tpimg_run("nand read 0x%x 0x%x 0x%x",
		      TPIMG_BOOT_LOAD_ADDR, koff, klen))
		return -1;

	{
		const char *ba = getenv("tplbootargs");

		if (!ba)
			ba = TPIMG_BOOT_ARGS_DEFAULT;
		printf("tplboot: bootargs=\"%s\"\n", ba);
		setenv("bootargs", ba);
	}

	snprintf(cmd, sizeof(cmd), "bootm 0x%x", TPIMG_BOOT_LOAD_ADDR);
	tpimg_run(cmd);		/* 成功则不再返回 */

	printf("tplboot: %s did not boot\n", name);
	return -1;
}

int tpimg_boot_slot(void)
{
	if (tpl_boot_one("image_2", WTA301_TPHDR_OFF) == 0)
		return 0;
	if (tpl_boot_one("image_1", TPIMG_B_HDR_ADDR) == 0)
		return 0;
	puts("tplboot: no bootable TP slot\n");
	return -1;
}
