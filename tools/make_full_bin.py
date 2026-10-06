#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_full_bin.py - 把 bootloader + 分区表 + app + WAD + SFX 合并成【单张整机镜像】。

定位（对齐 ai-passport-doom/tools/merge_all.py）：给「独立整机固件」做一键刷发布物，
用户拿到一个 full.bin，`esptool write_flash 0x0 full.bin` 一次刷完即可玩，无需分步。
这是 GitHub Releases 分发通道，**不是**上 SDGOODS 商店（商店只收不带地址的单个 app 镜像，
不投递 appdata，full.bin 会被 403）。

地址与 platform/partitions.csv + tools/flash_local.sh 严格一致（紧凑单体布局）：
    0x0        bootloader.bin        （本工程自编引导层）
    0x8000     partition-table.bin   （本机构建自带，appdata 紧跟 app 落低位）
    0x10000    SDGOODS_DOOM.bin      （factory app，单应用直启）
    0x210000   DOOM1_PROCESSED.WAD   （appdata 头部裸关卡数据，必须 PROCESSED 版）
    0x690000   DOOM_SFX.bin          （appdata+0x480000 音效库，可选；缺则静音）

紧凑表无 otadata 分区：factory 是唯一 app，bootloader 无选槽记录即直启 factory，
故不再需要擦 otadata，也不再需要 0x310000 那块填充。

ℹ 早期本版 appdata 落高 16MB（0x1000000），mmap 必须依赖 32 位 cache 地址映射的自编
  bootloader。现在 appdata 落低位 0x210000，该约束不再是硬要求；但仍用本工程自编
  bootloader（与 app 同一套构建产物），无副作用。

用法：
    python tools/make_full_bin.py                 # 默认构建目录 build_pub
    python tools/make_full_bin.py build_pub       # 指定构建目录
产出：
    build_pub/sdgoods-doom-full.bin               （单张整机镜像，可直接 write_flash 0x0）
    build_pub/sdgoods-doom-full.bin.zip           （Release 用；0xFF 填充压缩后很小）
"""
import sys
import zipfile
from pathlib import Path

# Windows 控制台默认 GBK，打印 ✓/✗ 等会 UnicodeEncodeError —— 强制 stdout 走 UTF-8。
try:
    sys.stdout.reconfigure(encoding="utf-8")
except (AttributeError, ValueError):
    pass

ROOT = Path(__file__).resolve().parent.parent          # sdgoods-doom/
BUILD = Path(sys.argv[1]) if len(sys.argv) > 1 else (ROOT / "build_pub")
if not BUILD.is_absolute():
    BUILD = ROOT / BUILD

APP_NAME = "SDGOODS_DOOM.bin"
# 地址表（务必与 partitions.csv 对齐）
ADDR_BL   = 0x0
ADDR_PT   = 0x8000
ADDR_APP  = 0x10000
ADDR_WAD  = 0x210000    # appdata 起点（紧跟 factory 2MB）
ADDR_SFX  = 0x690000    # = ADDR_WAD + 0x480000


def need(path: Path) -> bytes:
    if not path.is_file():
        sys.exit(f"✗ 缺少输入文件：{path}\n  请先 idf.py -B {BUILD.name} build（WAD/SFX 从 Releases 下载放仓库根）")
    return path.read_bytes()


def main():
    boot = need(BUILD / "bootloader" / "bootloader.bin")
    pt   = need(BUILD / "partition_table" / "partition-table.bin")
    app  = need(BUILD / APP_NAME)
    wad  = need(ROOT / "DOOM1_PROCESSED.WAD")
    sfx_p = ROOT / "DOOM_SFX.bin"
    sfx = sfx_p.read_bytes() if sfx_p.is_file() else None

    print(f"bootloader      : {len(boot):>10,} B  @0x{ADDR_BL:x}")
    print(f"partition-table : {len(pt):>10,} B  @0x{ADDR_PT:x}")
    print(f"app ({APP_NAME}): {len(app):>10,} B  @0x{ADDR_APP:x}")
    print(f"WAD (PROCESSED) : {len(wad):>10,} B  @0x{ADDR_WAD:x}")
    print(f"SFX             : {len(sfx):>10,} B  @0x{ADDR_SFX:x}" if sfx else "SFX             :        无（合并静音版，可玩不出声）")

    img = bytearray()

    def put(data: bytes, addr: int):
        if addr < len(img):
            sys.exit(f"✗ 地址 0x{addr:x} 与已写内容重叠（前一段尾部越过 0x{len(img):x}）")
        img.extend(b"\xff" * (addr - len(img)))   # 空隙用擦除态 0xFF 填充
        img.extend(data)

    put(boot, ADDR_BL)
    put(pt, ADDR_PT)
    put(app, ADDR_APP)
    put(wad, ADDR_WAD)
    if sfx:
        put(sfx, ADDR_SFX)

    full = BUILD / "sdgoods-doom-full.bin"
    full.write_bytes(bytes(img))
    size = len(img)
    print(f"\n✓ 整机镜像：{full}")
    print(f"  {size:,} B = {size/1048576:.1f} MB（紧凑布局：appdata 紧跟 app，已砍掉中间 15MB 空 ota 槽填充）")

    # Release 用 zip：0xFF 填充几乎全压掉，体积主要剩 WAD
    zpath = full.with_suffix(full.suffix + ".zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(full, full.name)
    print(f"✓ Release 包：{zpath}  {zpath.stat().st_size:,} B = {zpath.stat().st_size/1048576:.1f} MB")

    print("\n用户一键刷（解压后）：")
    print(f"  esptool.py --chip esp32s3 -p <COM> -b 921600 write_flash 0x0 {full.name}")
    print("  （刷完按 RST / 重新上电即进 DOOM；想回官方固件用平台安装通道重刷即可）")


if __name__ == "__main__":
    main()
