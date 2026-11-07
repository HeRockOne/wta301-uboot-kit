#!/bin/bash
# ⚠️ LEGACY —— 这是当时在本机跑的原样命令记录（`make -j6` 增量重建）。
#
#   ★ 正路请用同目录的 build_j1.sh：
#     - 它用 `make -j1`（-j6 会踩 dtbtable.S 的副作用竞态，见 README §4.1）
#     - 它自己处理工具链与源码树的定位
#     - 它跑完会出槽镜像
#
#   保留本文件只为追溯「当时究竟跑了什么命令」。见 README.md §3 / §4。
#
# 路径已参数化（原来是写死的本机路径）。UBOOT_TREE 指向 u-boot-2016 目录，
# TOOLCHAIN_BIN 指向含 arm-openwrt-linux-muslgnueabi-gcc 的 bin 目录。
#
# 绝不 distclean / mrproper —— 会删掉手写的 ipq5018-tplink-wta301.dts。
set -eu

KITROOT=$(cd "$(dirname "$0")/.." && pwd)

UBOOT_TREE=${UBOOT_TREE:-}
if [ -z "$UBOOT_TREE" ]; then
	for c in "$KITROOT/../src/u-boot-2016" /root/ub/u-boot-2016; do
		[ -f "$c/Makefile" ] && { UBOOT_TREE=$(cd "$c" && pwd); break; }
	done
fi
[ -n "${UBOOT_TREE:-}" ] || { echo "!! 请设 UBOOT_TREE=<u-boot-2016 目录>" >&2; exit 1; }

TOOLCHAIN_BIN=${TOOLCHAIN_BIN:-}
if [ -z "$TOOLCHAIN_BIN" ]; then
	for c in "$KITROOT/../src/staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin" \
	         "$UBOOT_TREE/../staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin"; do
		[ -x "$c/arm-openwrt-linux-muslgnueabi-gcc" ] && { TOOLCHAIN_BIN=$(cd "$c" && pwd); break; }
	done
fi
[ -n "${TOOLCHAIN_BIN:-}" ] || { echo "!! 请设 TOOLCHAIN_BIN=<.../bin>" >&2; exit 1; }

export ARCH=arm
export CROSS_COMPILE=arm-openwrt-linux-muslgnueabi-
export STAGING_DIR=$(dirname "$TOOLCHAIN_BIN")
export PATH="$TOOLCHAIN_BIN:$PATH"

cd "$UBOOT_TREE"

# 清掉 DTB 中间产物，保证 combine 步骤幂等
rm -f arch/arm/dts/ipq5018-tplink-wta301.dtb.comb.o \
      arch/arm/dts/.ipq5018-tplink-wta301.dtb.comb.o.cmd \
      arch/arm/dts/dtbtable.S arch/arm/dts/dtb_combined.bin

make -j6 2>&1 | tail -25
echo "---- result ----"
ls -l u-boot.bin dtb_combined.bin 2>/dev/null || true
md5sum u-boot.bin
