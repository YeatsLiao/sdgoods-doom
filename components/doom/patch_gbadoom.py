#!/usr/bin/env python3
"""构建期把 GBADoom 的 z_zone.c 大缓冲从内部 .bss 迁到 PSRAM（幂等 + 逐字节校验）。

为什么需要这一步
----------------
ESP32-S3 内部 DRAM（dram0）要留给：
  · LVGL 绘制缓冲——平台架构红线，放 PSRAM 会黑条/红线，必须在 SRAM；
  · WiFi/BLE/音频驱动缓冲。
GBADoom 的 esp32-ai-passport 分支是为**无 PSRAM 的 ESP32-C3** 写的，z_zone.c 把
128KB 的 overflow_buffer 放成 `static byte[...]`（.bss → 内部 DRAM）。叠加本工程的
backbuffer/canvas 后 dram0 溢出约 100KB，链接失败。

本工程跑在 S3-R8（8MB Octal PSRAM），把这些大缓冲改成运行时从 PSRAM 分配即可，
访问延迟略高但 I_FinishUpdate 的帧握手已限流，不是瓶颈。

GBADoom 是外部 pinned 依赖（CI 会干净 checkout），不能要求上游改；所以补丁随源码
提交在本仓库，由 components/doom/CMakeLists.txt 每次 configure 阶段幂等覆盖过去，
保证「clone 后直接编译」得到一致固件。

行为
----
- 幂等：目标已含 PSRAM 版标记（MALLOC_CAP_SPIRAM）就跳过。
- 三处替换任一匹配不到就报错退出（上游结构变了要人工复核，绝不静默产出坏固件）。
"""

import sys

MARK = "SDGOODS: PSRAM overflow zone"

REPLACEMENTS = [
    (
        "#include <stdlib.h>\n",
        "#include <stdlib.h>\n#include \"esp_heap_caps.h\"   /* %s */\n" % MARK,
    ),
    (
        "#define OVERFLOW_SIZE (128 * 1024)  // .bss: safe size, leaves enough system heap for FB(38KB)+zone(80KB)+stack(16KB)\n"
        "static byte overflow_buffer[OVERFLOW_SIZE] __attribute__((aligned(4)));\n",
        "#define OVERFLOW_SIZE (128 * 1024)\n"
        "static byte *overflow_buffer;   /* %s: heap_caps_malloc(PSRAM) in Z_Init, not .bss */\n" % MARK,
    ),
    (
        "    memblock_t*\tblock;\n\n    unsigned int heapSize = maxHeapSize;\n",
        "    memblock_t*\tblock;\n\n"
        "    /* %s: overflow buffer 放 PSRAM，S3 内部 DRAM 留给 LVGL 绘制缓冲(必须在 SRAM)+WiFi/BLE/音频 */\n"
        "    if (!overflow_buffer)\n"
        "        overflow_buffer = heap_caps_malloc(OVERFLOW_SIZE, MALLOC_CAP_SPIRAM);\n\n"
        "    unsigned int heapSize = maxHeapSize;\n" % MARK,
    ),
]


def main():
    path = sys.argv[1]
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    if MARK in src:
        print("z_zone.c already PSRAM-patched, skip")
        return 0

    for old, new in REPLACEMENTS:
        if old not in src:
            sys.stderr.write("z_zone.c patch anchor not found, upstream changed? Re-check:\n%r\n" % old[:80])
            return 1
        src = src.replace(old, new, 1)

    with open(path, "w", encoding="utf-8") as f:
        f.write(src)
    print("z_zone.c patched: overflow_buffer -> PSRAM")
    return 0


if __name__ == "__main__":
    sys.exit(main())
