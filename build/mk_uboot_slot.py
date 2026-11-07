#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mk_uboot_slot.py —— 把编译出的 u-boot.bin 变成「网页可直接上传」的槽镜像。

为什么需要这一步（一级反汇编实证，不是推测）：

    level-1 读 0x4700000 的 0x40000 字节 → 0x44000000
            → lzmaBuffToBuffDecompress(out=0x49900000, cap=0xa0000, 0x44000000, len)
            → blx 0x49900000

    而且它对解压失败**没有裸镜像回退**：只打印一行，然后照样 blx 过去。
    所以槽里必须是 LZMA-Alone 压缩流 + 0xFF 补齐；裸 u-boot.bin（538 KB）
    既装不进 256 KB 的槽，也不会被识别。

    另：一级校验 image_2 时比对 header[0x90] == md5(整个 0x40000 槽)。
    本脚本只产槽；摘要由板子自己算（它写完槽后用新摘要重签 0x90），
    这样不需要把 header 一起传来传去。

用法：
    python mk_uboot_slot.py [u-boot.bin] [输出目录]

产物：
    <outdir>/uboot_slot_0x40000.bin   —— 网页「高级：单独升级 U-Boot」上传它
"""
import hashlib
import lzma
import os
import shutil
import sys

SLOT_LEN = 0x40000
DEC_CAP = 0xA0000          # 一级的解压上限 655360
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_BIN = os.path.join(ROOT, 'src', 'u-boot-2016', 'u-boot.bin')


def die(msg):
    print('REFUSE: ' + msg)
    sys.exit(1)


def main():
    uboot = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_BIN
    outdir = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(
        os.path.abspath(__file__))

    if not os.path.isfile(uboot):
        die('找不到 %s' % uboot)

    plain = open(uboot, 'rb').read()
    print('u-boot.bin        %d bytes (0x%x)' % (len(plain), len(plain)))

    # 一级的硬上限：解压后必须 <= 0xa0000
    if len(plain) > DEC_CAP:
        die('明文 %d 超过一级解压上限 0x%x，刷进去一级解不掉'
            % (len(plain), DEC_CAP))
    print('  <= 0xa0000 cap   ok, margin %d bytes' % (DEC_CAP - len(plain)))

    # 一级的 LZMA 解码器（lib/lzma）读的是 buffer 长度，不是流长度，
    # 所以必须自己压成 LZMA-Alone（含 8 字节大小头），这正是主机构建时
    # mk_normal_boot_slot.py 用的同一格式。
    comp = lzma.compress(plain, format=lzma.FORMAT_ALONE, preset=9)
    print('lzma preset 9     %d bytes (0x%x)  = %.1f%% of slot'
          % (len(comp), len(comp), 100.0 * len(comp) / SLOT_LEN))

    if len(comp) > SLOT_LEN:
        die('压缩后 %d 超过槽 0x%x，装不下（需要裁功能或换压缩等级）'
            % (len(comp), SLOT_LEN))

    # 0xFF 补齐（NAND 擦除态，一级不关心尾部）
    image = comp + b'\xff' * (SLOT_LEN - len(comp))
    assert len(image) == SLOT_LEN

    # 自检 1：能被解回原文（用同一族的解码器验，等价于板子上的那段）
    back = lzma.decompress(image, format=lzma.FORMAT_ALONE)
    if back != plain:
        die('自检失败：解压结果与 u-boot.bin 不一致')
    print('round-trip        ok（解压后与 u-boot.bin 逐字节一致）')

    # 自检 2：板子上的 sanity 判据必须成立
    first = int.from_bytes(plain[:4], 'little')
    if (first >> 24) != 0xEA and (first & 0xFFFFF000) != 0xE59FF000:
        die('u-boot.bin 首字 0x%08x 不是 ARM reset 向量，板子会拒收' % first)
    if b'U-Boot 20' not in plain:
        die('u-boot.bin 里找不到 "U-Boot 20" banner，板子会拒收')
    print('board checks      ok（首字是 ARM 分支、含 banner 字符串）')

    # 自检 3：成品指纹
    print('slot md5          %s' % hashlib.md5(image).hexdigest())
    print('slot crc32        0x%08X' % (__import__('zlib').crc32(image) & 0xffffffff))

    out = os.path.join(outdir, 'uboot_slot_0x40000.bin')
    open(out, 'wb').write(image)
    print('\nwrote %s (%d bytes)' % (out, len(image)))
    print('\n下一步：http://192.168.1.1 → 高级：单独升级 U-Boot → 选这个文件')
    print('        → 上传并校验 → 写入 → 重启')


if __name__ == '__main__':
    main()
