/*
 * Copyright (c) 2016-2017, 2020 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <common.h>
#include <asm/errno.h>
#include <nand.h>
#include <part.h>
#include <mmc.h>
#include <asm/arch-qca-common/smem.h>
#include <asm/arch-qca-common/qca_common.h>
#include <sdhci.h>
#ifdef CONFIG_IPQ_TINY_SPI_NOR
#include <spi.h>
#include <spi_flash.h>
#endif
#if defined(CONFIG_ART_COMPRESSED) &&	\
	(defined(CONFIG_GZIP) || defined(CONFIG_LZMA))
#ifndef CONFIG_COMPRESSED_LOAD_ADDR
#define CONFIG_COMPRESSED_LOAD_ADDR CONFIG_SYS_LOAD_ADDR
#endif
#include <mapmem.h>
#include <lzma/LzmaTools.h>
#endif
#ifdef CONFIG_QCA_MMC
#ifndef CONFIG_SDHCI_SUPPORT
extern qca_mmc mmc_host;
#else
extern  struct sdhci_host mmc_host;
#endif
#endif

#ifdef CONFIG_TP_FACTORYINFO_MAC
/*
 * TP-Link 定制（WTA301 / AX55 等 M1CT 机型）的 eth MAC 来源。
 *
 * 为什么不能用 QCA 默认的 "读 ART 偏移 0"：
 *   本板 ART = gzip(tar(tmp/virtual_art.bin))，里面只有校准数据，
 *   整个解压结果（525824 B）里找不到任何 MAC（已实证：搜不到 98:97:cc）。
 *   裸读 ART offset 0 -> gzip 容器头 1f 8b -> is_valid_ethaddr() 失败 ->
 *   board_late_init() 打 "ethN MAC Address from ART is not valid" ->
 *   DTB 里 local-mac-address 保持 00:00:00:00:00:00（stock DTB 实测如此）->
 *   内核每次开机随机生成 MAC。
 *
 * 真实 MAC 在 factory_info 分区的 "M1CT" TLV 容器里：
 *   0x00  "M1CT"
 *   0x04  20 字节头（版本/摘要，含义未解）
 *   0x18  起是 TLV 序列： type(be16) len(be16) value(len)
 *         type 0x0000 = 6 字节 MAC      <-- 本例 98:97:cc:a8:1f:e7
 *         type 0x0001 = 8 字节
 *         type 0x0002 = 20 字节
 *         type 0x0003 = 16 字节
 *         type 0x0004 = 128 字节
 *         type 0x0005 = 256 字节（十六进制字符串）
 *         type 0x0006 = 192 字节
 *   已实证：整片 128 MB dump 里 98:97:cc 只出现一次，就在 0x40001c。
 */
#define TP_FACTORYINFO_NAND_OFFSET	0x400000	/* "factory_info" 分区 */
#define TP_FACTORYINFO_SIZE		0x20000		/* 128 KB */
#define TP_M1CT_HDR_LEN			0x18		/* magic(4) + 20 */
#define TP_M1CT_TLV_MAC			0x0000

static int tp_m1ct_find_mac(const u_char *buf, u32 buflen, u_char *mac)
{
	u32 off = TP_M1CT_HDR_LEN;

	while (off + 4 <= buflen) {
		u16 type = ((u16)buf[off] << 8) | buf[off + 1];
		u16 len = ((u16)buf[off + 2] << 8) | buf[off + 3];

		off += 4;
		if ((u32)len > buflen - off)
			return -EINVAL;
		if (type == TP_M1CT_TLV_MAC && len >= 6) {
			memcpy(mac, buf + off, 6);
			return 0;
		}
		off += len;
	}
	return -ENOENT;
}

/*
 * 成功返回 0 并填好 length 字节（length = 6 * CONFIG_IPQ_NO_MACS）。
 * 失败返回负值，调用方退回 ART 裸读。
 */
static int tp_get_mac_from_factory_info(u_char *enetaddr, u32 length)
{
	nand_info_t *nand = &nand_info[CONFIG_NAND_FLASH_INFO_IDX];
	u_char *buf;
	size_t rd = TP_FACTORYINFO_SIZE;
	int ret = -ENODEV;
	int i;

	if (!nand->name || length < 6)
		return -ENODEV;

	buf = malloc(TP_FACTORYINFO_SIZE);
	if (!buf)
		return -ENOMEM;

	/* 最后一个参数 lim 必须给真实设备大小：传 0 会在
	 * nand_util.c:738 判 "used_for_read > lim" 直接失败。
	 */
	if (nand_read_skip_bad(nand, TP_FACTORYINFO_NAND_OFFSET, &rd,
			       NULL, nand->size, buf) == 0 &&
	    rd >= TP_M1CT_HDR_LEN &&
	    !memcmp(buf, "M1CT", 4) &&
	    tp_m1ct_find_mac(buf, rd, enetaddr) == 0) {
		/* 第二个 MAC 顺延一位。本板只有一片 gmac 上联 RTL8367S，
		 * eth1 实际不用；写出去只为让 DTB 不再留 00:00:00:00:00:00。
		 */
		if (length >= 12) {
			memcpy(enetaddr + 6, enetaddr, 6);
			for (i = 11; i >= 6; i--) {
				if (++enetaddr[i])
					break;
			}
		}
		ret = 0;
	}

	free(buf);
	return ret;
}
#endif /* CONFIG_TP_FACTORYINFO_MAC */

/*
 * Gets the ethernet address from the ART partition table and return the value
 */
int get_eth_mac_address(uchar *enetaddr, uint no_of_macs)
{
	s32 ret = 0 ;
	u32 start_blocks;
	u32 size_blocks;
	u32 length = (6 * no_of_macs);
	u32 flash_type;
	loff_t art_offset;
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
#ifdef CONFIG_QCA_MMC
	block_dev_desc_t *blk_dev;
	disk_partition_t disk_info;
	struct mmc *mmc;
	char mmc_blks[512];
#endif
#ifdef CONFIG_IPQ_TINY_SPI_NOR
	struct spi_flash *flash = NULL;
#if defined(CONFIG_ART_COMPRESSED) && (defined(CONFIG_GZIP) || defined(CONFIG_LZMA))
	void *load_buf, *image_buf;
	unsigned long img_size;
	unsigned long desMaxSize;
#endif
#endif

	if (sfi->flash_type != SMEM_BOOT_MMC_FLASH) {
		if (qca_smem_flash_info.flash_type == SMEM_BOOT_SPI_FLASH)
			flash_type = CONFIG_SPI_FLASH_INFO_IDX;
		else if (qca_smem_flash_info.flash_type == SMEM_BOOT_NAND_FLASH)
			flash_type = CONFIG_NAND_FLASH_INFO_IDX;
		else if (qca_smem_flash_info.flash_type == SMEM_BOOT_QSPI_NAND_FLASH)
			flash_type = CONFIG_NAND_FLASH_INFO_IDX;
		else {
			printf("Unknown flash type\n");
			return -EINVAL;
		}

		ret = smem_getpart("0:ART", &start_blocks, &size_blocks);
		if (ret < 0) {
			printf("No ART partition found\n");
			return ret;
		}

		/*
		 * ART partition 0th position will contain Mac address.
		 */
		art_offset =
		((loff_t) qca_smem_flash_info.flash_block_size * start_blocks);

#ifdef CONFIG_IPQ_TINY_SPI_NOR
		flash = spi_flash_probe(CONFIG_SF_DEFAULT_BUS, CONFIG_SF_DEFAULT_CS,
				CONFIG_SF_DEFAULT_SPEED, CONFIG_SF_DEFAULT_MODE);
		if (flash == NULL){
			printf("No SPI flash device found\n");
			ret = -1;
		} else {
#if defined(CONFIG_ART_COMPRESSED) && (defined(CONFIG_GZIP) || defined(CONFIG_LZMA))
		image_buf = map_sysmem(CONFIG_COMPRESSED_LOAD_ADDR, 0);
		load_buf = map_sysmem(CONFIG_COMPRESSED_LOAD_ADDR + 0x100000, 0);
		img_size = qca_smem_flash_info.flash_block_size * size_blocks;
		desMaxSize = 0x100000;
		ret = spi_flash_read(flash, art_offset, img_size, image_buf);
		if (ret == 0) {
			ret = -1;
#ifdef CONFIG_GZIP
			ret = gunzip(load_buf, desMaxSize, image_buf, &img_size);
#endif
#ifdef CONFIG_LZMA
			if (ret != 0)
				ret = lzmaBuffToBuffDecompress(load_buf,
					(SizeT *)&desMaxSize,
					image_buf,
					(SizeT)img_size);
#endif
			if (ret == 0) {
				memcpy(enetaddr, load_buf, length);
			} else {
				printf("Invalid compression type..\n");
				ret = -1;
			}
		}
#else
		ret = spi_flash_read(flash, art_offset, length, enetaddr);
#endif
		}
		/*
		 * Avoid unused warning
		 */
		(void)flash_type;
#else
#ifdef CONFIG_TP_FACTORYINFO_MAC
		/*
		 * WTA301 等 TP-Link 定制板：MAC 不在 ART。
		 * ART 在本板是 gzip(tar(tmp/virtual_art.bin))，只装校准数据，
		 * 裸读 offset 0 得到的是 gzip 容器头 -> is_valid_ethaddr() 必失败
		 * -> "ethN MAC Address from ART is not valid" -> DTB 里
		 * local-mac-address 保持 [00 00 00 00 00 00] -> 内核每次开机随机 MAC。
		 * 真实 MAC 在 factory_info 分区的 "M1CT" TLV 容器里，type 0 = 6 字节。
		 * 已实证：flash 0x40001c = 98:97:cc:a8:1f:e7（整片 dump 唯一一处）。
		 */
		ret = tp_get_mac_from_factory_info(enetaddr, length);
		if (ret < 0) {
			/* 没 factory_info 的机型：退回原来的 ART 裸读 */
			ret = nand_read(&nand_info[flash_type],
					art_offset, &length, enetaddr);
		}
#else
		ret = nand_read(&nand_info[flash_type],
				art_offset, &length, enetaddr);
#endif
#endif
		if (ret < 0)
			printf("ART partition read failed..\n");
#ifdef CONFIG_QCA_MMC
	} else {
		blk_dev = mmc_get_dev(mmc_host.dev_num);
		ret = get_partition_info_efi_by_name(blk_dev, "0:ART", &disk_info);
		/*
		 * ART partition 0th position will contain MAC address.
		 * Read 1 block.
		 */
		if (ret == 0) {
			mmc = mmc_host.mmc;
			ret = mmc->block_dev.block_read
				(mmc_host.dev_num, disk_info.start,
						1, mmc_blks);
			memcpy(enetaddr, mmc_blks, length);
                }
		if (ret < 0)
			printf("ART partition read failed..\n");
#endif
	}
	return ret;
}

void set_ethmac_addr(void)
{
	int i, ret;
	uchar enetaddr[CONFIG_IPQ_NO_MACS * 6];
	uchar *mac_addr;
	char ethaddr[16] = "ethaddr";
	char mac[64];
	/* Get the MAC address from ART partition */
	ret = get_eth_mac_address(enetaddr, CONFIG_IPQ_NO_MACS);
	for (i = 0; (ret >= 0) && (i < CONFIG_IPQ_NO_MACS); i++) {
		mac_addr = &enetaddr[i * 6];
		if (!is_valid_ethaddr(mac_addr)) {
			printf("eth%d MAC Address from ART is not valid\n", i);
		} else {
			/*
			 * U-Boot uses these to patch the 'local-mac-address'
			 * dts entry for the ethernet entries, which in turn
			 * will be picked up by the HLOS driver
			 */
			snprintf(mac, sizeof(mac), "%x:%x:%x:%x:%x:%x",
					mac_addr[0], mac_addr[1],
					mac_addr[2], mac_addr[3],
					mac_addr[4], mac_addr[5]);
			setenv(ethaddr, mac);
		}
		snprintf(ethaddr, sizeof(ethaddr), "eth%daddr", (i + 1));
	}
}
