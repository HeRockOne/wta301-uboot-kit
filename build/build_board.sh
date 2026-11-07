#!/bin/bash
# Incremental rebuild of an arbitrary ipq5018 board in this tree.
# NEVER run distclean/mrproper: it deletes our hand-added .dts files.
# Usage: UBOOT_TREE=/path/to/u-boot-2016 build_board.sh <defconfig-name-without-"_defconfig">
set -e
B="$1"
[ -n "${B:-}" ] || { echo "用法: build_board.sh <defconfig 名，不带 _defconfig>" >&2; exit 1; }

# 源码树 / 工具链均可用环境变量覆盖；未给时按本仓与 src/ 并列的布局猜
KITROOT=$(cd "$(dirname "$0")/.." && pwd)
if [ -z "${UBOOT_TREE:-}" ]; then
	for c in "$KITROOT/../src/u-boot-2016" /root/ub/u-boot-2016; do
		[ -f "$c/Makefile" ] && { UBOOT_TREE=$(cd "$c" && pwd); break; }
	done
fi
[ -n "${UBOOT_TREE:-}" ] || { echo "!! 请设 UBOOT_TREE=<u-boot-2016 目录>" >&2; exit 1; }

if [ -z "${TOOLCHAIN_BIN:-}" ]; then
	for c in "$KITROOT/../src/staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin" \
	         "$KITROOT/../../../src/staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin" \
	         "$UBOOT_TREE/../staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin"; do
		[ -x "$c/arm-openwrt-linux-muslgnueabi-gcc" ] && { TOOLCHAIN_BIN=$(cd "$c" && pwd); break; }
	done
fi
[ -n "${TOOLCHAIN_BIN:-}" ] || { echo "!! 请设 TOOLCHAIN_BIN=<.../bin>" >&2; exit 1; }

STAGING_DIR=$(dirname "$TOOLCHAIN_BIN")
export ARCH=arm
export CROSS_COMPILE=arm-openwrt-linux-muslgnueabi-
export STAGING_DIR
export PATH="$TOOLCHAIN_BIN:$PATH"
cd "$UBOOT_TREE"

echo "== defconfig: $B =="
make "$B"_defconfig >/dev/null

# purge every generated DTB/table artefact so the combine step is idempotent
rm -f arch/arm/dts/*.dtb.comb.o arch/arm/dts/.*.dtb.comb.o.cmd
rm -f arch/arm/dts/*.dtb.comb.S arch/arm/dts/dtb_combined.bin
rm -f arch/arm/dts/dtb_combined.bin.gz arch/arm/dts/dtbtable.S
rm -f arch/arm/dts/*.dtb

# `set -e` above would kill this script the instant make returns non-zero,
# before the report below could run - which is exactly what happened twice:
# the log showed only "== defconfig ==" and the real cause stayed hidden.
set +e
make -j6 > build.log 2>&1
rc=$?
set -e
if [ $rc -ne 0 ]; then
	echo "!! make FAILED rc=$rc - real error lines:"
	grep -n -i -m 20 'error\|undefined\|No rule\|not found\|No such file' build.log
	echo "!! last 8 lines:"
	tail -8 build.log
	exit $rc
fi
tail -4 build.log
ls -l u-boot.bin
md5sum u-boot.bin
