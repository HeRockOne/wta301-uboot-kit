/*
* Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
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

#ifndef _IPQ5018_H
#define _IPQ5018_H

#ifndef DO_DEPS_ONLY
#include <generated/asm-offsets.h>
#endif

#define CONFIG_IPQ5018
#undef	CONFIG_QCA_DISABLE_SCM
#define CONFIG_SPI_FLASH_CYPRESS
#define CONFIG_SYS_NO_FLASH
#define CONFIG_SYS_CACHELINE_SIZE		64
#define CONFIG_IPQ_NO_RELOC

#define CONFIG_SYS_VSNPRINTF

/*
 * Enable Early and Late init
 * This config needs for secondary boot and to set BADOFF5E
 * This config also need for spi-nor boot,
 * set size and offset of hlos and rootfs
*/
#define CONFIG_BOARD_EARLY_INIT_F
#define CONFIG_BOARD_LATE_INIT

#define CONFIG_IPQ5018_UART
#define CONFIG_NR_DRAM_BANKS			1
#define CONFIG_SKIP_LOWLEVEL_INIT

#define CONFIG_SYS_BOOTM_LEN		(200 << 20)

#define CONFIG_ENV_SIZE_MAX			(256 << 10) /* 256 KB */

/*
* PSCI Calls enable
*/
#define CONFIG_ARMV7_PSCI

/*
 * Flash read/write commands
 */
#define CONFIG_CMD_FLASHREAD
#define CONFIG_CMD_FLASHWRITE
#if defined(CONFIG_CMD_FLASHREAD) || defined(CONFIG_CMD_FLASHWRITE)
#define CONFIG_CMD_FLASHRW
#endif

/*
 * Enable Env overwrite support
 */
#define CONFIG_ENV_OVERWRITE

/*
 * select serial console configuration
*/
#define CONFIG_CONS_INDEX			1
#define CONFIG_SYS_DEVICE_NULLDEV

/* allow to overwrite serial and ethaddr */
#define CONFIG_BAUDRATE				115200
#define CONFIG_SYS_BAUDRATE_TABLE		{4800, 9600, 19200, 38400, 57600,\
						115200}

#define CONFIG_SYS_CBSIZE			(512 * 2) /* Console I/O Buffer Size */
/*

		svc_sp     --> --------------
		irq_sp     --> |            |
		fiq_sp     --> |            |
		bd         --> |            |
		gd         --> |            |
		pgt        --> |            |
		malloc     --> |            |
		text_base  --> |------------|
*/

#define CONFIG_SYS_INIT_SP_ADDR         	(CONFIG_SYS_TEXT_BASE -\
						CONFIG_SYS_MALLOC_LEN - CONFIG_ENV_SIZE -\
						GENERATED_BD_INFO_SIZE)

#define CONFIG_SYS_MAXARGS			16
#define CONFIG_SYS_PBSIZE			(CONFIG_SYS_CBSIZE + \
							sizeof(CONFIG_SYS_PROMPT) + 16)

#define TLMM_BASE				0x01000000
#define GPIO_CONFIG_ADDR(x)			(TLMM_BASE + (x)*0x1000)
#define GPIO_IN_OUT_ADDR(x)			(TLMM_BASE + 0x4 + (x)*0x1000)

#define CONFIG_SYS_SDRAM_BASE			0x40000000
/*
 * WTA301: level-1 factory_boot (linked at 0x4a920000) loads the level-2
 * normal_boot image to 0x49900000 and jumps there with `blx r5`
 * (disassembled: 4a927d4c ldr r5,[pc,#156] -> 0x49900000; 4a927d64 blx r5).
 * An image linked at 0x4a920000 would therefore be jumped to at the wrong
 * base and the board would be dead, so this target must use 0x49900000.
 */
/*
 * EXPERIMENT (A/B control, 2026-11-02): the CUCC VS010 prebuilt uBootKit is
 * linked at 0x4a920000 and prints a banner on this board when loaded there.
 * To compare a *freshly built* vs010 image against our WTA301 image under
 * identical conditions (same mover, same destination, level-1 u-boot left
 * intact in RAM), the vs010 build is temporarily linked at 0x49900000 too.
 * This changes ONLY the link address - no functional code differs.
 */
#ifdef CONFIG_TARGET_IPQ5018_TPLINK_WTA301
#define CONFIG_SYS_TEXT_BASE			0x49900000
#elif defined(CONFIG_TARGET_IPQ5018_CUCC_VS010) || \
      defined(CONFIG_TARGET_IPQ5018_CMCC_MR3000D_CI)
#define CONFIG_SYS_TEXT_BASE			0x49900000
#else
#define CONFIG_SYS_TEXT_BASE			0x4A920000
#endif
#define CONFIG_SYS_SDRAM_SIZE			0x10000000
#define CONFIG_MAX_RAM_BANK_SIZE		CONFIG_SYS_SDRAM_SIZE
#define CONFIG_SYS_LOAD_ADDR			(CONFIG_SYS_SDRAM_BASE + (64 << 20))
#define CONFIG_ROOTFS_LOAD_ADDR			(CONFIG_SYS_SDRAM_BASE + (16 << 20))

#define QCA_KERNEL_START_ADDR			CONFIG_SYS_SDRAM_BASE
#define QCA_DRAM_KERNEL_SIZE			CONFIG_SYS_SDRAM_SIZE
#define QCA_BOOT_PARAMS_ADDR			(QCA_KERNEL_START_ADDR + 0x100)

#define CONFIG_OF_COMBINE			1


#define CONFIG_QCA_SMEM_BASE			0x4AB00000

#define CONFIG_IPQ_FDT_HIGH			0x4A400000
#define CONFIG_ENV_IS_IN_SPI_FLASH		1
#define CONFIG_ENV_SECT_SIZE			(64 * 1024)

#define CONFIG_QCA_UBOOT_OFFSET			0xA800000
#define CONFIG_UBOOT_END_ADDR			0x4AA00000

/*
* IPQ_TFTP_MIN_ADDR: Starting address of Linux HLOS region.
* CONFIG_TZ_END_ADDR: Ending address of Trust Zone and starting
* address of WLAN Area.
* TFTP file can only be written in Linux HLOS region and WLAN AREA.
*/
#define IPQ_TFTP_MIN_ADDR			(CONFIG_SYS_SDRAM_BASE + (16 << 20))
#define CONFIG_TZ_END_ADDR			(CONFIG_SYS_SDRAM_BASE + (88 << 21))
#define CONFIG_SYS_SDRAM_END			((long long)CONFIG_SYS_SDRAM_BASE + gd->ram_size)

#ifndef __ASSEMBLY__
#include <compiler.h>
extern loff_t board_env_offset;
extern loff_t board_env_range;
extern loff_t board_env_size;
#endif

#define CONFIG_IPQ5018_ENV			1
#define CONFIG_ENV_OFFSET			board_env_offset
#define CONFIG_ENV_SIZE				CONFIG_ENV_SIZE_MAX
/*
 * ENV_SIZE is CONFIG_ENV_RANGE minus the 4-byte header, and the normal value
 * here is `board_env_range` - a plain global initialised to 0 that only gets a
 * real value inside board_init().  board_init() runs in init_sequence_r, after
 * the banner, but the environment is installed before it, so with
 * CONFIG_ENV_IS_NOWHERE on the WTA301 build the comparison
 *
 *     sizeof(default_environment) > ENV_SIZE
 *
 * evaluated `> (0 - 4)` and printed
 *
 *     *** Error - default environment is too large
 *
 * then aborted the environment and left the shell dead (14:00 RAM trial).
 * There is no flash region to query under NOWHERE, so use a static range.
 */
#ifndef CONFIG_TARGET_IPQ5018_TPLINK_WTA301
#define CONFIG_ENV_RANGE			board_env_range
#else
#define CONFIG_ENV_RANGE			CONFIG_ENV_SIZE_MAX
#endif
/* WTA301：原值 500K 时，实测堆 = MALLOC_LEN + ENV_SIZE = 0xFD000 = 1012 KB。
 *
 * 这个数不够 /sysinfo/tplink 用：它一次要同时持有
 *     slot 0x40000 (256K) + dec 0xA0000 (640K) = 896K
 * 再叠上 httpd 每连接的会话内存，浏览器一开页面（多连接并发）
 * 640K 那次 malloc 就失败 → decode_ok 变 false → 页面误报「解压失败」，
 * 而串口 tplcheck（单连接、无 httpd 开销）同一镜像却是 OK。
 * 实测：单请求 6/6 成功，6 并发只有 1~2 个成功（work/scratch/diag_conc.py）。
 *
 * 加到 2 MB。DRAM 256 MB，堆从 relocaddr 往下长，栈跟着 INIT_SP_ADDR
 * 一起下移，布局自洽（bdinfo 的 irq_sp 会相应变小，属正常）。 */
#define CONFIG_SYS_MALLOC_LEN			(CONFIG_ENV_SIZE_MAX + (2048 << 10))

/*
 * NAND Flash Configs
*/

/* CONFIG_QPIC_NAND: QPIC NAND in BAM mode
 * CONFIG_IPQ_NAND: QPIC NAND in FIFO/block mode.
 * BAM is enabled by default.
 */
#define CONFIG_CMD_MTDPARTS
#define CONFIG_SYS_NAND_SELF_INIT

#ifdef CONFIG_NAND_FLASH
#define CONFIG_CMD_NAND
/*
 * WTA301 has no QCA MIBIB partition table in SMEM, so smem_getpart(
 * "0:APPSBLENV") fails and board_init() aborts with "cdp: get environment
 * part failed". Until a static partition table exists, keep the environment
 * in RAM only (CONFIG_ENV_IS_IN_NAND also gates that whole board_init block).
 */
#ifndef CONFIG_TARGET_IPQ5018_TPLINK_WTA301
#define CONFIG_ENV_IS_IN_NAND			1
#else
#define CONFIG_ENV_IS_NOWHERE			1
/*
 * 默认环境里**不要**设 failsafe。
 *
 * 2026-11-04 实测：以前这里种子 `failsafe=always`，本意是让没串口的人也能
 * 进网刷页救砖。但 do_httpd_check() -> failsafe_loop() 在没人访问时会一直
 * 挂住（>60 s 不返回），结果【每次上电都停在 Web failsafe，永远不进
 * OpenWrt】——用户必须每次接串口敲 run bootcmd，家用完全不可接受。
 *
 * 恢复路径（三条，都不依赖默认 env）：
 *   1. 上电 boomdelay 3 s 内串口敲任意键 -> cli -> `httpd` 起网刷页；
 *   2. bootcmd（bootipq）失败时 u-boot 自然落回 cli，同样可 `httpd`；
 *   3. 一级 u-boot 的 checkImage 校验 normal_boot 的 md5(header+0x90)，
 *      不通过自动回退 B 槽 normal_boot_bak(0x700000)。
 * 另外 WTA301 没有 /keys 节点（GPIO38 浮低，不是按键），所以
 * is_any_button_pressed_for_enough_time() 那条路本来就走不通。
 */
#define CONFIG_EXTRA_ENV_SETTINGS		\
	""
#endif
#define CONFIG_QPIC_NAND
#define CONFIG_SYS_NAND_ONFI_DETECTION
#define CONFIG_CMD_NAND_YAFFS
#define CONFIG_MTD_DEVICE
#define CONFIG_MTD_PARTITIONS
#endif

#ifdef CONFIG_QPIC_SERIAL
#define CONFIG_QPIC_NODE			"/soc/qpic-nand@79b0000/"
#ifdef QSPI_SERIAL_DEBUG /* QSPI DEBUG */
#define qspi_debug(fmt,args...)	printf (fmt ,##args)
#else
#define qspi_debug(fmt,args...)
#endif /* QSPI DEBUG */
#define CONFIG_PAGE_SCOPE_MULTI_PAGE_READ
#define CONFIG_QSPI_SERIAL_TRAINING
#define CONFIG_QSPI_LAYOUT_SWITCH
#endif

/*
* SPI Flash Configs
*/
#define CONFIG_QCA_SPI
#define CONFIG_SPI_FLASH
#define CONFIG_CMD_SF
#define CONFIG_SPI_FLASH_STMICRO
#define CONFIG_SPI_FLASH_WINBOND
#define CONFIG_SPI_FLASH_MACRONIX
#define CONFIG_SPI_FLASH_GIGADEVICE
#define CONFIG_SPI_FLASH_SPANSION
#define CONFIG_SF_DEFAULT_BUS			0
#define CONFIG_SF_DEFAULT_CS			0
#define CONFIG_SF_DEFAULT_MODE			SPI_MODE_0
#define CONFIG_SF_DEFAULT_SPEED			(48 * 1000 * 1000)
#define CONFIG_SPI_FLASH_BAR			1
#define CONFIG_SPI_FLASH_USE_4K_SECTORS
#define CONFIG_IPQ_4B_ADDR_SWITCH_REQD

#define CONFIG_QUP_SPI_USE_DMA			1
#define CONFIG_QCA_BAM				1

/*
 * MMC configs
 */
#ifdef CONFIG_MMC_FLASH
#define CONFIG_QCA_MMC
#define CONFIG_MMC
#define CONFIG_CMD_MMC
#define CONFIG_GENERIC_MMC
#define CONFIG_SDHCI
#define CONFIG_SDHCI_QCA
#define CONFIG_ENV_IS_IN_MMC
#define CONFIG_SYS_MMC_ENV_DEV			0
#define CONFIG_SDHCI_SUPPORT
#define CONFIG_MMC_ADMA
/*
 * EFI/GPT partition parsing is needed outside the MMC block too:
 * board/qca/arm/common/failsafe.c and common/cmd_flashrw.c both call
 * get_partition_info_efi_by_name().  Keep it unconditional so the build does
 * not depend on MMC_FLASH being enabled.
 */
#define CONFIG_EFI_PARTITION
/*
* eMMC controller support only 4-bit
* force SDHC driver to 4-bit mode
*/
#define CONFIG_MMC_FORCE_CAP_4BIT_BUSWIDTH
#endif

#ifndef CONFIG_EFI_PARTITION
#define CONFIG_EFI_PARTITION
#endif

/*
* I2C Enable
*/
#ifdef CONFIG_IPQ_I2C
#define CONFIG_SYS_I2C_QUP
#define CONFIG_CMD_I2C
#define CONFIG_DM_I2C
#endif

/*
* GMAC Enable
*/

#define CONFIG_IPQ5018_GMAC
#define CONFIG_IPQ5018_MDIO

#define CONFIG_NET_RETRY_COUNT			5
#define CONFIG_SYS_RX_ETH_BUFFER		128
#define CONFIG_CMD_PING
#define CONFIG_MII
#define CONFIG_IPADDR				192.168.1.1
#define CONFIG_NETMASK				255.255.255.0
/*
 * do_network_check()（lib/ipq_api.c）每次启动都把 ipaddr/netmask/serverip
 * 重置成这里的编译默认值，所以原来 192.168.1.2 导致**每次刷机都得先
 * setenv serverip 192.168.1.100**。直接写成 TFTP 主机的地址。
 * 想临时改回：setenv custom_network 1 之后随便设。
 */
#define CONFIG_SERVERIP				192.168.1.100
#define CONFIG_CMD_TFTPPUT
#define CONFIG_IPQ_MDIO				1
#define CONFIG_IPQ_ETH_INIT_DEFER
#define CONFIG_IPQ_NO_MACS			2

/*
 * GEPHY
 */
#define CONFIG_GEPHY

/*
 * USB Support
 */
#ifdef CONFIG_USB_XHCI_IPQ
#define CONFIG_USB_XHCI
#define CONFIG_USB_XHCI_DWC3
#define CONFIG_CMD_USB
#define CONFIG_USB_STORAGE
#define CONFIG_SYS_USB_XHCI_MAX_ROOT_PORTS      2
#define CONFIG_USB_MAX_CONTROLLER_COUNT         1

/*
 * USB crashdump collection
 */
#define CONFIG_FS_FAT
#define CONFIG_FAT_WRITE
#define CONFIG_CMD_FAT

/*
 * Block Device & Disk  Partition Config
 */
#define HAVE_BLOCK_DEVICE
#define CONFIG_DOS_PARTITION
#endif

/*
 * PCIE Enable
 */
#define PCI_MAX_DEVICES				2
#if defined(CONFIG_PCI_IPQ)
#define CONFIG_PCI
#define CONFIG_CMD_PCI
#define CONFIG_PCI_SCAN_SHOW
#endif

/*
* Expose SPI driver as a pseudo NAND driver to make use
* of U-Boot's MTD framework.
*/
#define CONFIG_SYS_MAX_NAND_DEVICE		CONFIG_IPQ_MAX_NAND_DEVICE + \
							CONFIG_IPQ_MAX_SPI_DEVICE

#define CONFIG_IPQ_MAX_NAND_DEVICE		1
#define CONFIG_IPQ_MAX_SPI_DEVICE		1

#define CONFIG_IPQ_MAX_BLSP_QUPS		3
#define CONFIG_QPIC_NAND_NAND_INFO_IDX		0
#define CONFIG_IPQ_SPI_NOR_INFO_IDX		1

#define CONFIG_NAND_FLASH_INFO_IDX		CONFIG_QPIC_NAND_NAND_INFO_IDX
#define CONFIG_SPI_FLASH_INFO_IDX		CONFIG_IPQ_SPI_NOR_INFO_IDX

#define QCA_SPI_NOR_DEVICE			"spi0.0"

/*
* U-Boot Env Configs
*/
#define CONFIG_OF_LIBFDT			1
#define CONFIG_SYS_HUSH_PARSER
#define CONFIG_CMD_XIMG

/* MTEST */
#define CONFIG_SYS_MEMTEST_START		CONFIG_SYS_SDRAM_BASE + 0x1300000
#define CONFIG_SYS_MEMTEST_END			CONFIG_SYS_MEMTEST_START + 0x100

/* NSS firmware loaded using bootm */
#define CONFIG_BOOTCOMMAND			"bootipq"
#define CONFIG_BOOTARGS				"console=ttyMSM0,115200n8"
#define QCA_ROOT_FS_PART_NAME			"rootfs"

#define CONFIG_BOOTDELAY			3

#define NUM_ALT_PARTITION			16

#ifdef CONFIG_IPQ_TINY
/* undef gzip lib */
#undef CONFIG_GZIP
#undef CONFIG_ZLIB

#else
#define CONFIG_CMD_BOOTZ

/* Multicore CPU support */
#define CONFIG_SMP_CMD_SUPPORT

/* Mii command support */
#define CONFIG_CMD_MII

/* compress crash dump support */
#define CONFIG_CMD_ZIP
#define CONFIG_GZIP_COMPRESSED

/* Enable DTB compress */
#define CONFIG_COMPRESSED_DTB_MAX_SIZE		0x40000
#define CONFIG_COMPRESSED_DTB_BASE		CONFIG_SYS_TEXT_BASE -\
						CONFIG_COMPRESSED_DTB_MAX_SIZE
#endif

#define CONFIG_FDT_FIXUP_PARTITIONS

#define CONFIG_IPQ_FDT_FIXUP
/*
* Below Configs need to be updated after enabling reset_crashdump
* Included now to avoid build failure
*/
#define CONFIG_OF_BOARD_SETUP

#define TCSR_BOOT_MISC_REG	((u32 *)0x193D100)

#ifdef CONFIG_OF_BOARD_SETUP
#define DLOAD_DISABLE				(~BIT(4))
#define DLOAD_ENABLE				BIT(4)
#define CRASHDUMP_RESET                        BIT(11)

#define CLEAR_MAGIC				0x0
#define SCM_CMD_TZ_CONFIG_HW_FOR_RAM_DUMP_ID	0x9
#define SCM_CMD_TZ_FORCE_DLOAD_ID		0x10
#define SCM_CMD_TZ_PSHOLD			0x16
#define BOOT_VERSION				0
#define TZ_VERSION				1
#endif


#define CONFIG_IPQ5018_TZ_WONCE_4_ADDR		0x193d010
/*
* CRASH DUMP ENABLE
*/
/* #define CONFIG_QCA_APPSBL_DLOAD */
#ifdef CONFIG_QCA_APPSBL_DLOAD

#undef CONFIG_NET_RETRY_COUNT
#define CONFIG_NET_RETRY_COUNT  500

#define IPQ_TEMP_DUMP_ADDR 0x44000000
#endif

#define CONFIG_QCA_KERNEL_CRASHDUMP_ADDRESS	*((unsigned int *)0x08600658)
#define CONFIG_CPU_CONTEXT_DUMP_SIZE		4096
#define TLV_BUF_OFFSET				1012 * 1024
#define CONFIG_TLV_DUMP_SIZE			12 * 1024

/* L1 cache line size is 64 bytes, L2 cache line size is 128 bytes
* Cache flush and invalidation based on L1 cache, so the cache line
* size is configured to 64 */
#define CONFIG_SYS_CACHELINE_SIZE		64

/*
 * UBI write command
 */
#ifdef CONFIG_UBI_WRITE
#define CONFIG_CMD_UBI
#define CONFIG_RBTREE
#define IPQ_UBI_VOL_WRITE_SUPPORT
#endif

#undef CONFIG_BOOTM_NETBSD
#undef CONFIG_BOOTM_PLAN9
#undef CONFIG_BOOTM_RTEMS
#undef CONFIG_BOOTM_VXWORKS

#ifdef CONFIG_ART_COMPRESSED
/*
 * This location use to keep comprssed data for
 * uncompress process.
 * default location is CONFIG_SYS_LOAD_ADDR if not defined.
 */
#define CONFIG_COMPRESSED_LOAD_ADDR (CONFIG_SYS_LOAD_ADDR + (1 << 22))
#endif

#ifdef CONFIG_SMP_CMD_SUPPORT
#define NR_CPUS				2
#endif

/*
 * 96 MHz
 */

#define INTERNAL_96MHZ

/*#define CONFIG_IPQ_BT_SUPPORT*/

#define CONFIG_LIST_OF_CONFIG_NAMES_SUPPORT

#ifdef CONFIG_LIST_OF_CONFIG_NAMES_SUPPORT
#define CONFIG_NAME_MAX_ENTRIES	6
#define CONFIG_NAME_MAX_LEN	32
#endif

/*
 * Lib functions
 */
#define CONFIG_LIB_BOOTCONFIG
#define CONFIG_LIB_IPQ_API
#define CONFIG_LIB_FLASHRW

/*
 * Other commands
 */
#define CONFIG_IPQ_XTRACT_N_FLASH
#define CONFIG_IPQ_MIBIB_RELOAD
#define CONFIG_CMD_BOOTCONFIG
#define CONFIG_CMD_BOOTFLASH
#define CONFIG_CMD_IMI
#define CONFIG_HTTPD
#define CONFIG_TCP
#define CONFIG_LIB_RAND
#define CONFIG_HTTPD_DEBUG
#define CONFIG_INCLUDE_EXTRA_DTB
#define CONFIG_CALL_FUNC_CAPTURE

/*
 * Disable some modules
 */
#define CONFIG_CMD_DISABLE_EXECTZT
#define CONFIG_DISABLE_SIGNED_BOOT

/*
 * Console record support
 */
#define CONFIG_CONSOLE_RECORD
#define CONFIG_CONSOLE_RECORD_OUT_SIZE 0x3000

/*
 * Network abort detection for U-Boot autoboot
 */
/* #define CONFIG_NET_ABORT */

/*
 * DHCP Server support
 */
#define CONFIG_DHCPD

#ifdef CONFIG_DHCPD
#define CONFIG_CMD_DISABLE_BOOTP
#else
#define CONFIG_CMD_DHCP
#endif /* CONFIG_DHCPD */

/*
 * Telnet Server support
 */
#define CONFIG_TELNETD

/*
 * wget support
 */
#define CONFIG_WGET

#endif /* _IPQ5018_H */
