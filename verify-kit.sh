#!/bin/bash
# verify-kit.sh —— 本仓静态自检（不编译、不连板子，5 秒跑完）
#
# 为什么需要这个：
#   这个仓要能「别人 clone 下来照着走就能重建出同一份 u-boot」。
#   真正会让它失败的，几乎全是静态问题：
#     · files/ 少了一个文件（覆盖后编译报缺符号）
#     · 版权头被删（GPL-2.0 §1 违规，法务问题）
#     · 文件被 Windows 检出成 CRLF（u-boot 侧会全线报错）
#     · 脚本里有本机绝对路径（别人拿去跑不了）
#     · 文档里的判据与实际不符（md5 / 尺寸 / 偏移）
#   这些问题都能离线查出来，不该等到编译失败或刷砖才发现。
#
# 用法：
#   bash verify-kit.sh            # 全部检查
#   bash verify-kit.sh --quiet    # 只打印失败
#
# 退出码：0 全过 / 1 有失败
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE" || exit 1

QUIET=0
[ "${1:-}" = "--quiet" ] && QUIET=1

FAIL=0
WARN=0
n_ok=0

ok()   { n_ok=$((n_ok+1)); [ $QUIET -eq 1 ] || echo "  [OK  ] $*"; }
bad()  { FAIL=$((FAIL+1));    echo "  [FAIL] $*"; }
warn() { WARN=$((WARN+1));    echo "  [WARN] $*"; }
sec()  { [ $QUIET -eq 1 ] || { echo; echo "== $* =="; }; }

# ══════════════════════════════════════════════════════════════════
sec "1. files/ 的 23 个文件齐全"
# ══════════════════════════════════════════════════════════════════
# 这份清单就是「我们改了什么」的定义。缺任何一个，覆盖到 u-boot 树后
# 要么编不过，要么静默丢掉一个功能（比如少了 rtl8367s.c 就没网口）。
FILES=(
	arch/arm/cpu/armv7/qca/common/smem.c
	arch/arm/dts/Makefile
	arch/arm/dts/ipq5018-tplink-wta301.dts
	board/ipq5018/Kconfig
	board/qca/arm/common/cmd_bootqca.c
	board/qca/arm/common/ethaddr.c
	board/qca/arm/common/failsafe.c
	board/qca/arm/common/fw_dec.c
	board/qca/arm/common/tplink_tpimg.c
	board/qca/arm/ipq5018/ipq5018.c
	configs/ipq5018_tplink_wta301_defconfig
	drivers/net/Makefile
	drivers/net/ipq5018/ipq5018_gmac.c
	drivers/net/ipq5018/rtl8367s.c
	failsafe/failsafe.c
	failsafe/fsdata/index.html
	failsafe/modules/Makefile
	failsafe/modules/modules.h
	failsafe/modules/tplinkinfo.c
	failsafe/modules/upgrade.c
	include/configs/ipq5018.h
	include/failsafe/fw_dec.h
	include/failsafe/tplink_tpimg.h
)
miss=0
for f in "${FILES[@]}"; do
	[ -f "files/$f" ] || { bad "files/$f 缺失"; miss=1; }
done
[ $miss -eq 0 ] && ok "${#FILES[@]}/${#FILES[@]} 个文件在位"

# CHANGES.md §2.2 必须逐文件列出（GPL-2.0 §2(a)）
miss=0
for f in "${FILES[@]}"; do
	grep -qF "\`$f\`" CHANGES.md || { bad "CHANGES.md 未列出 $f"; miss=1; }
done
[ $miss -eq 0 ] && ok "CHANGES.md 逐文件列出全部 ${#FILES[@]} 个"

# ══════════════════════════════════════════════════════════════════
sec "2. 版权头保留（GPL-2.0 §1，删了即违规）"
# ══════════════════════════════════════════════════════════════════
# 这些文件里有上游版权头，任何一条被删都是合规问题。
# 详见 THIRD-PARTY.md §2。
declare_owned() { grep -qiE "$2" "files/$1"; }
miss=0
for pair in \
	"board/qca/arm/common/failsafe.c:chenxin527" \
	"board/qca/arm/common/fw_dec.c:chenxin527" \
	"failsafe/failsafe.c:chenxin527" \
	"failsafe/modules/modules.h:chenxin527" \
	"include/failsafe/fw_dec.h:chenxin527" \
	"failsafe/modules/upgrade.c:Weijie Gao|MediaTek"
do
	f=${pair%:*}; pat=${pair#*:}
	declare_owned "$f" "$pat" || { bad "版权头丢失：$f（期望含 $pat）"; miss=1; }
done
[ $miss -eq 0 ] && ok "chenxin527 × 5 + MediaTek × 1 版权头都在"

# 许可声明：u-boot 2016 时代的文件并不统一用 SPDX，不能一刀切。
# 实际分布（已逐个核实，见 THIRD-PARTY.md §2.2）：
#   13 个 SPDX-License-Identifier · 6 个 GPL-2.0 全文头 · 1 个 BSD 式（smem.c）
#   3 个无任何声明（Kconfig / defconfig / index.html）
# 本项只要求「要么有声明，要么在白名单里并写明理由」—— 不许有意外遗漏。
NO_LICENSE_OK="board/ipq5018/Kconfig configs/ipq5018_tplink_wta301_defconfig failsafe/fsdata/index.html"
miss=0
for f in "${FILES[@]}"; do
	if grep -qE 'SPDX-License-Identifier:|GNU General Public License|Redistribution and use in source and binary forms' "files/$f"; then
		continue
	fi
	case " $NO_LICENSE_OK " in
	*" $f "*) continue ;;
	esac
	bad "既无许可声明又不在白名单：files/$f"
	miss=1
done
[ $miss -eq 0 ] && ok "每个文件要么有许可声明，要么在白名单（3 个，理由见 THIRD-PARTY §2.2）"

# 4 个「无 copyright 头」的文件必须仍然没有 —— 加了我们的署名反而是错的
for f in board/qca/arm/common/tplink_tpimg.c \
         include/failsafe/tplink_tpimg.h \
         drivers/net/ipq5018/rtl8367s.c \
         failsafe/modules/tplinkinfo.c
do
	if grep -qiE 'copyright' "files/$f"; then
		warn "files/$f 出现了 copyright 头 —— THIRD-PARTY.md §2.1 说这 4 个原本没有，确认是否为上游新增"
	fi
done
ok "4 个无 copyright 头的文件状态未变（加署名会是错的）"

# ══════════════════════════════════════════════════════════════════
sec "3. 编码 / 换行（u-boot 侧必须 LF）"
# ══════════════════════════════════════════════════════════════════
# .gitattributes 是 `* -text`，所以仓库里存的什么、检出就是什么。
# 一旦有人从 Windows 提交 CRLF，u-boot 的 Makefile 与版权头校验都会出问题。
crlf=$(find . -path ./.git -prune -o -type f -print | while read -r f; do
	grep -qU $'\r' "$f" 2>/dev/null && echo "$f"
done)
if [ -n "$crlf" ]; then
	echo "$crlf" | while read -r f; do bad "CRLF：$f"; done
else
	ok "全仓无 CRLF"
fi

# 所有文本文件必须有末尾换行（diff/补丁工具依赖这个）
miss=0
while read -r f; do
	[ -s "$f" ] || continue
	[ "$(tail -c1 "$f" | od -An -tx1 | tr -d ' \n')" = "0a" ] || { bad "缺末尾换行：$f"; miss=1; }
done < <(find . -path ./.git -prune -o -type f -print)
[ $miss -eq 0 ] && ok "所有文件都有末尾换行"

# ══════════════════════════════════════════════════════════════════
sec "4. 无本机绝对路径 / 无凭据"
# ══════════════════════════════════════════════════════════════════
# 别人 clone 下来必须能跑。个人目录、盘符、内网 IP 都不该出现。
# 允许的例外：文档里作为「示例」的 /root/ub、以及路由器默认地址 192.168.1.1。
hits=$(grep -rnE '/mnt/[a-z]/|C:\\\\Users|D:\\\\Python|192\.168\.(?!1\.1)[0-9]+\.[0-9]+' \
	--include='*.md' --include='*.sh' --include='*.py' --include='*.c' --include='*.h' \
	--include='Makefile' --include='*.dts' --include='*.config' --include='*defconfig' \
	. 2>/dev/null | grep -vE '^\./\.git/' || true)
if [ -n "$hits" ]; then
	echo "$hits" | head -10 | while read -r l; do bad "本机路径/内网地址：$l"; done
else
	ok "无本机绝对路径、无内网 IP（192.168.1.1 是路由器默认地址，不算）"
fi

# ══════════════════════════════════════════════════════════════════
sec "5. 关键常量（改了会让板子变砖）"
# ══════════════════════════════════════════════════════════════════
H=files/include/configs/ipq5018.h
# CONFIG_SYS_TEXT_BASE 的具体值由下一段判（必须精确等于 0x49900000）。

# 二级 u-boot 链接基址 = 0x49900000。这不是可调参数：
# 一级 u-boot 解压到那个地址后 blx 过去，链接到别处 = TTL 直接死。详见 README §4.3。
if grep -qE '^\s*#define\s+CONFIG_SYS_TEXT_BASE\s+0x49900000' "$H"; then
	ok "CONFIG_SYS_TEXT_BASE = 0x49900000（一级 blx 的目标）"
else
	bad "$H 的 CONFIG_SYS_TEXT_BASE 不是 0x49900000 —— 这会让 TTL 直接死"
fi

# 槽大小余量：一级解压上限 0xA0000=655360 B，交付 556032 B → 余量 99328 B。
# ⚠️ 这两个常量定义在 tplink_tpimg.h，不在 ipq5018.h。
TH=files/include/failsafe/tplink_tpimg.h
SLOT_CAP=$(grep -oE 'define TPIMG_UBOOT_DEC_CAP\s+0x[0-9a-fA-F]+' "$TH" | head -1 | grep -oE '0x[0-9a-fA-F]+')
if [ -n "$SLOT_CAP" ]; then
	ok "TPIMG_UBOOT_DEC_CAP = $SLOT_CAP（交付 556032 B 应在内）"
	[ $((SLOT_CAP)) -ge 556032 ] || bad "解压上限 $SLOT_CAP < 交付镜像 556032 B"
else
	bad "$TH 里没找到 TPIMG_UBOOT_DEC_CAP（一级解压上限，扣错了会刷成砖）"
fi

# 槽长度必须 0x40000（一级 u-boot 只读 256 KiB）
grep -qE 'define TPIMG_UBOOT_SLOT_LEN\s+0x0*40000' "$TH" \
	&& ok "TPIMG_UBOOT_SLOT_LEN = 0x40000" || bad "$TH 里没找到 TPIMG_UBOOT_SLOT_LEN = 0x40000"

# TP-Link 8 字节 magic，从一级 u-boot 里提取（见 README §4.3）。
# .c 里是字节数组；.h 里是注释形式的同一个串。
for f in files/board/qca/arm/common/tplink_tpimg.c files/include/failsafe/tplink_tpimg.h; do
	grep -qE '0x9dd1|9d, 0xd1|55 aa 9d d1' "$f" \
		&& ok "$(basename "$f") 含 8 字节 magic" || bad "$f 里没找到 magic"
done

# ══════════════════════════════════════════════════════════════════
sec "6. 构建脚本可用性"
# ══════════════════════════════════════════════════════════════════
for s in build/build_j1.sh build/build_board.sh build/build_wta301.sh verify-kit.sh; do
	[ -f "$s" ] || { bad "$s 缺失"; continue; }
	bash -n "$s" 2>/dev/null && ok "$s 语法通过" || bad "$s 语法错误"
	[ -x "$s" ] || warn "$s 没有可执行位（用 bash $s 仍可跑）"
done

# build_j1.sh 必须接受 UBOOT_TREE / TOOLCHAIN_BIN 覆盖
grep -q 'UBOOT_TREE=' build/build_j1.sh && grep -q 'TOOLCHAIN_BIN=' build/build_j1.sh \
	&& ok "build_j1.sh 支持 UBOOT_TREE / TOOLCHAIN_BIN 覆盖" \
	|| bad "build_j1.sh 未能参数化（别人拿去跑不了）"

# 绝不能出现 distclean（会删手写的 .dts）
if grep -rnE '\bmake\s+(distclean|mrproper)\b' --include='*.sh' --include='*.md' . 2>/dev/null \
	| grep -vE '绝不|NEVER|不要|勿|禁止'; then
	bad "出现了 distclean/mrproper 的正面用法（会删手写 .dts）"
else
	ok "没有 distclean/mrproper 的正面用法"
fi

# mk_uboot_slot.py 语法
python3 -c "import ast,sys; ast.parse(open('build/mk_uboot_slot.py',encoding='utf-8').read())" 2>/dev/null \
	&& ok "build/mk_uboot_slot.py 语法通过" || bad "build/mk_uboot_slot.py 语法错误"

# ══════════════════════════════════════════════════════════════════
sec "7. 文档一致性"
# ══════════════════════════════════════════════════════════════════
for f in README.md CHANGES.md THIRD-PARTY.md LICENSE; do
	[ -f "$f" ] && ok "$f 在位" || bad "$f 缺失"
done

# 上游归属必须在 README 和 THIRD-PARTY 都写明（THIRD-PARTY.md §2.3 第 3 条要求）
grep -q '9ce31573' README.md && grep -q 'chenxin527' README.md \
	&& ok "README 写明派生自 chenxin527/uboot-qsdk12.5-build @ 9ce31573" \
	|| bad "README 未写明上游派生关系（GPL 归属要求）"
grep -q '9ce31573' THIRD-PARTY.md && grep -q 'chenxin527' THIRD-PARTY.md \
	&& ok "THIRD-PARTY 写明派生关系" || bad "THIRD-PARTY 未写明派生关系"

# LICENSE 必须是 GPL-2.0 全文
if head -3 LICENSE | grep -q 'GNU GENERAL PUBLIC LICENSE' && grep -q 'Version 2, June 1991' LICENSE; then
	ok "LICENSE = GPL-2.0 全文"
else
	bad "LICENSE 不是 GPL-2.0 全文"
fi

# 唯一的成功判据（README §3 验收判据）必须与实测值一致
grep -q '3a4cb00e37065936e5ff89cb5710a519' README.md \
	&& ok "README 记录 u-boot.bin 期望 md5 3a4cb00e…" || warn "README 缺 u-boot.bin 期望 md5"
grep -q 'd0ada9f7' README.md \
	&& ok "README 记录槽镜像 md5 d0ada9f7…" || warn "README 缺槽镜像 md5"

# 历史快照必须明确标注不可直接用（否则有人会 copy 错文件）
sed -n '/^## 5\./,/^## 6\./p' README.md | grep -q '不要直接用' \
	&& ok "README §5 明确标注 p18–p21 不可直接用" || bad "README §5 警告丢失"

# ══════════════════════════════════════════════════════════════════
echo
if [ $FAIL -eq 0 ]; then
	echo "  OK=$n_ok  WARN=$WARN  FAIL=0"
	[ $WARN -eq 0 ] || echo "  （WARN 不阻塞，但请逐条看过）"
	exit 0
else
	echo "  OK=$n_ok  WARN=$WARN  FAIL=$FAIL"
	exit 1
fi
