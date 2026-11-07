#!/bin/bash
# 单线程全量构建 WTA301 u-boot（2016.01 / ipq5018）。
#
# 为什么 -j1：dtbtable.S 是 arch/arm/dts 里 %.dtb.comb.S 规则的**副作用**产物，
# 而 $(obj)/dtbtable.S: $(comb.o) 的 recipe 是空操作 @:。并行构建时
# dtbtable.o 可能抢在 comb.o 之前被编译 -> gcc 报
#     arch/arm/dts/dtbtable.S: No such file or directory
# 顶层 make 也不认 arch/arm/dts/Makefile 里的模式规则，所以没法单独生成。
# 串行全量是唯一确定性路径。（首次若仍报这个错，再跑一次即可。）
#
# 绝不 distclean/mrproper —— 会删手写的 ipq5018-tplink-wta301.dts。
#
# 用法：
#   UBOOT_TREE=/path/to/u-boot-2016 \
#   TOOLCHAIN_BIN=/path/to/toolchain/bin \
#   bash build_j1.sh
#
# 两者都可用环境变量给定；未给时按脚本位置猜（支持仓库并列 / 仓库内嵌套两种布局）。
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
KITROOT=$(cd "$HERE/.." && pwd)

# 1) u-boot 源码树
UBOOT_TREE=${UBOOT_TREE:-}
if [ -z "$UBOOT_TREE" ]; then
	for c in "$KITROOT/../src/u-boot-2016" \
	         "$KITROOT/../../../src/u-boot-2016" \
	         "$KITROOT/../../../../src/u-boot-2016" \
	         /root/ub/u-boot-2016; do
		[ -f "$c/Makefile" ] && { UBOOT_TREE=$(cd "$c" && pwd); break; }
	done
fi
[ -n "${UBOOT_TREE:-}" ] && [ -f "$UBOOT_TREE/Makefile" ] || {
	echo "!! 找不到 u-boot 源码树。请设 UBOOT_TREE=<u-boot-2016 目录>" >&2; exit 1; }

# 2) 工具链
TOOLCHAIN_BIN=${TOOLCHAIN_BIN:-}
if [ -z "$TOOLCHAIN_BIN" ]; then
	for c in "$KITROOT/../src/staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin" \
	         "$KITROOT/../../../src/staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin" \
	         "$UBOOT_TREE/../staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin"; do
		[ -x "$c/arm-openwrt-linux-muslgnueabi-gcc" ] && { TOOLCHAIN_BIN=$(cd "$c" && pwd); break; }
	done
fi
[ -n "${TOOLCHAIN_BIN:-}" ] && [ -x "$TOOLCHAIN_BIN/arm-openwrt-linux-muslgnueabi-gcc" ] || {
	echo "!! 找不到工具链。请设 TOOLCHAIN_BIN=<.../bin>（需含 arm-openwrt-linux-muslgnueabi-gcc）" >&2; exit 1; }

STAGING=$(dirname "$TOOLCHAIN_BIN")
export CROSS_COMPILE="$TOOLCHAIN_BIN/arm-openwrt-linux-muslgnueabi-"
export PATH="$TOOLCHAIN_BIN:$PATH"
export ARCH=arm
export STAGING_DIR="$STAGING"

echo "== 环境 =="
echo "  u-boot tree : $UBOOT_TREE"
echo "  toolchain   : $TOOLCHAIN_BIN"
"${CROSS_COMPILE}gcc" --version | head -1

cd "$UBOOT_TREE" || exit 1

echo "== defconfig =="
make ipq5018_tplink_wta301_defconfig > /tmp/dc.log 2>&1
rc=$?
echo "  defconfig rc=$rc"
[ $rc -ne 0 ] && { tail -20 /tmp/dc.log; exit $rc; }
grep -q "CONFIG_TP_FACTORYINFO_MAC=y" .config && echo "  MAC config OK"

# 清掉 DTB 中间产物，保证 combine 步骤幂等
rm -f arch/arm/dts/ipq5018-tplink-wta301.dtb.comb.o \
      arch/arm/dts/.ipq5018-tplink-wta301.dtb.comb.o.cmd \
      arch/arm/dts/dtbtable.S arch/arm/dts/dtb_combined.bin

echo "== make -j1（约 150–235 s）=="
make -j1 > build_j1.log 2>&1
rc=$?
echo "  make rc=$rc"
if [ $rc -ne 0 ]; then
	if grep -q 'dtbtable.S: No such file' build_j1.log; then
		echo "  (已知的 dtbtable.S 副作用问题 -> 自动再跑一次)"
		make -j1 > build_j1.log 2>&1
		rc=$?
		echo "  重跑 rc=$rc"
	fi
	if [ $rc -ne 0 ]; then
		echo "!! 失败，最后 20 行:"
		tail -20 build_j1.log
		exit $rc
	fi
fi

echo
echo "== 产物 =="
ls -l u-boot.bin
md5sum u-boot.bin
echo "  ★ 期望 556032 B / 3a4cb00e37065936e5ff89cb5710a519（P21c）"
echo "    上限 655360 B（一级解压缓冲 TPIMG_UBOOT_DEC_CAP）"
SIZE=$(stat -c%s u-boot.bin)
[ "$SIZE" -le 655360 ] && echo "  尺寸检查 OK（余量 $((655360-SIZE)) B）" || echo "  !! 超出一级解压上限 655360 B"

echo
echo "== 出槽镜像 =="
OUTDIR=${OUTDIR:-$KITROOT/out}
mkdir -p "$OUTDIR"
python "$(dirname "$0")/mk_uboot_slot.py" u-boot.bin "$OUTDIR"
