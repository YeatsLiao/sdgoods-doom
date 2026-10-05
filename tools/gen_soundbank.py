#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_soundbank.py - 把 GBADoom 的 DOOM 音效 WAV 转成 ESP32 可用的 soundbank。

背景：DOOM1_GBA.WAD 已删光音频 lump（实测 DS* = 0），唯一音效来源是
GBADoom/music/DS*.wav。本脚本把它们归一为 8-bit 无符号 PCM，产出：
  1) DOOM_SFX.bin        —— 拼接后的裸 PCM，烧进 appdata 分区 WAD 之后的固定偏移；
  2) doom_sfx_index.h    —— 按 sfx_id 对齐的索引数组（{data_offset, num_samples, rate}），
                            编译期进固件；运行时把 .bin 读进 PSRAM，用 offset 定位。

不重采样：保留每条音效的原生采样率（11025/22050），由运行时混音器按 rate 变步长到
I2S 输出率。16-bit WAV 折算成 8-bit 无符号（128=silence）；8-bit 原样。

用法：python gen_soundbank.py [music_dir] [out_dir]
  music_dir 默认  <repo>/../GBADoom/music
  out_dir    默认  <repo>            （DOOM_SFX.bin 落这，索引头落 components/doom/）
"""
import os
import struct
import sys

# 与 GBADoom source/sounds.c 的 S_sfx[] 顺序严格一致（index == sfx_id）。
# index 0 = "none" 哑元；"chgun" 是 link 到 pistol，仓库无 DSCHGUN.wav → 置空。
SFX_NAMES = [
    "none", "pistol", "shotgn", "sgcock", "dshtgn", "dbopn", "dbcls", "dbload",
    "plasma", "bfg", "sawup", "sawidl", "sawful", "sawhit", "rlaunc", "rxplod",
    "firsht", "firxpl", "pstart", "pstop", "doropn", "dorcls", "stnmov", "swtchn",
    "swtchx", "plpain", "dmpain", "popain", "vipain", "mnpain", "pepain", "slop",
    "itemup", "wpnup", "oof", "telept", "posit1", "posit2", "posit3", "bgsit1",
    "bgsit2", "sgtsit", "cacsit", "brssit", "cybsit", "spisit", "bspsit", "kntsit",
    "vilsit", "mansit", "pesit", "sklatk", "sgtatk", "skepch", "vilatk", "claw",
    "skeswg", "pldeth", "pdiehi", "podth1", "podth2", "podth3", "bgdth1", "bgdth2",
    "sgtdth", "cacdth", "skldth", "brsdth", "cybdth", "spidth", "bspdth", "vildth",
    "kntdth", "pedth", "skedth", "posact", "bgact", "dmact", "bspact", "bspwlk",
    "vilact", "noway", "barexp", "punch", "hoof", "metal", "chgun", "tink",
    "bdopn", "bdcls", "itmbk", "flame", "flamst", "getpow", "bospit", "boscub",
    "bossit", "bospn", "bosdth", "manatk", "mandth", "sssit", "ssdth", "keenpn",
    "keendt", "skeact", "skesit", "skeatk", "radio",
]


def read_wav(path):
    """返回 (samples: bytes(8bit unsigned), rate: int)。只支持 mono，8/16 bit PCM。"""
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE: " + path)
    i = data.find(b"fmt ")
    if i < 0:
        raise ValueError("no fmt chunk: " + path)
    audiofmt, channels, rate, _byterate, _align, bits = struct.unpack("<HHIIHH", data[i + 8:i + 24])
    if audiofmt != 1:
        raise ValueError("not PCM: " + path)
    j = data.find(b"data", i)
    if j < 0:
        raise ValueError("no data chunk: " + path)
    dlen = struct.unpack("<I", data[j + 4:j + 8])[0]
    raw = data[j + 8:j + 8 + dlen]

    if channels == 2:  # 理论上都是 mono，保险起见取左声道
        if bits == 16:
            n = len(raw) // 4
            raw = b"".join(raw[k * 4:k * 4 + 2] for k in range(n))
        else:
            raw = raw[0::2]

    if bits == 8:
        return raw, rate  # DOOM DMX 8-bit 即无符号（128=silence），原样
    if bits == 16:
        out = bytearray(len(raw) // 2)
        for k in range(len(raw) // 2):
            s = struct.unpack_from("<h", raw, k * 2)[0]  # signed
            v = (s >> 8) + 128                            # → unsigned 8-bit
            out[k] = 0 if v < 0 else (255 if v > 255 else v)
        return bytes(out), rate
    raise ValueError("unsupported bit depth %d: %s" % (bits, path))


def main():
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # sdgoods-doom/
    music_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(repo), "GBADoom", "music")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else repo
    if not os.path.isdir(music_dir):
        sys.exit("music dir not found: " + music_dir)

    blob = bytearray()
    entries = []  # (offset, count, rate) per sfx_id
    matched = 0
    for sid, name in enumerate(SFX_NAMES):
        if sid == 0:
            entries.append((0, 0, 0))
            continue
        lump = ("DS" + name).upper()
        wav = os.path.join(music_dir, lump + ".wav")
        if not os.path.isfile(wav):
            entries.append((0, 0, 0))
            continue
        pcm, rate = read_wav(wav)
        offset = len(blob)
        blob += pcm
        entries.append((offset, len(pcm), rate))
        matched += 1

    # 4 字节对齐拼接总长（便于分区读取；不改 offset 语义）
    bin_path = os.path.join(out_dir, "DOOM_SFX.bin")
    with open(bin_path, "wb") as f:
        f.write(blob)

    hdr_path = os.path.join(out_dir, "components", "doom_engine", "doom_sfx_index.h")
    os.makedirs(os.path.dirname(hdr_path), exist_ok=True)
    with open(hdr_path, "w", encoding="utf-8") as f:
        f.write("/* 由 tools/gen_soundbank.py 自动生成，勿手改。\n"
                " * 索引 = sfx_id（与 GBADoom S_sfx[] 顺序一致）。DOOM_SFX.bin 于运行时\n"
                " * 读进 PSRAM，data_offset 为该音效在其中的字节偏移。 */\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("#define DOOM_SFX_COUNT %d\n" % len(SFX_NAMES))
        f.write("#define DOOM_SFX_TOTAL_BYTES %du   /* DOOM_SFX.bin 大小 */\n" % len(blob))
        f.write("/* 建议烧进 appdata 分区、WAD 之后的固定偏移（≥ DOOM1_GBA.WAD 4,278,841B）*/\n")
        f.write("#define DOOM_SFX_FLASH_OFFSET 0x480000u   /* 4.5MB，留 >200KB 余量给 WAD */\n\n")
        f.write("typedef struct { uint32_t data_offset; uint32_t num_samples; uint32_t rate; } doom_sfx_entry_t;\n")
        f.write("static const doom_sfx_entry_t DOOM_SFX_INDEX[DOOM_SFX_COUNT] = {\n")
        for sid, (off, cnt, rate) in enumerate(entries):
            note = ""
            if cnt == 0:
                note = "  /* %s: 无 */" % SFX_NAMES[sid]
            f.write("  {%du, %du, %du},%s\n" % (off, cnt, rate, note))
        f.write("};\n")

    print("matched %d/%d sfx, bank=%d bytes (%.2f MB)" % (matched, len(SFX_NAMES), len(blob), len(blob) / 1048576))
    print("wrote:", bin_path)
    print("wrote:", hdr_path)


if __name__ == "__main__":
    main()
