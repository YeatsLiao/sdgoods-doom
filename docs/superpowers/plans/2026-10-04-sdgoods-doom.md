# SDGOODS-DOOM 独立单应用固件 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development 或 superpowers:executing-plans 逐任务实施本计划。步骤用 checkbox（`- [ ]`）跟踪。
>
> ⚠️ **Git 纪律（用户明确要求）**：不要擅自 `git commit` / `push`。每个任务末尾的"提交检查点"仅做 `git status` 汇报，**提交动作必须等用户明确指示**。
>
> 📦 **仓库口径（用户 2026-10-04 确认）**：本仓库是**单独的 sdgoods-doom 工程**，不留官方模板杂物。已删除：根 `docs/*`（平台文档）、`sdgoods-ai/`、`screenshot/`、`README_EN/TRADEMARK/AGENTS/LICENSING/LICENSE/NOTICE/version.txt`、`tools/` 内发布链路脚本（publish/pack_app/new_*_project/plane_stat 等）。必须保留（构建依赖）：`components/sdgoods_board|sdgoods_launcher|jpegenc`（平台 BSP，非"官方固件本体"）、`platform/`（prebuilt 引导+分区表）、`tools/` 构建/烧录/截屏/字体脚本、`sdkconfig*`、`dependencies.lock`。

**Goal:** 在谷仓次元屏（SDGOODS ESP32-S3 圆屏）上做一个开机直入 DOOM 的单应用固件，GBA 风格虚拟按键 + 单指交互。

**Architecture:** 以 SDGOODS-ESP32S3 派生的独立工程 `SDGOODS_DOOM`（沿用平台分区表，boot-direct）为底座，把 ai-passport-doom 验证过的 GBADoom（esp32-ai-passport 分支）引擎层移植进来。引擎任务（core 0）渲染 240×160 调色板索引帧并置 `frame_ready`；LVGL 循环（core 1）在 app poll 里把帧转 RGB565 写入 canvas 并 invalidate，由平台既有 SRAM 条带 flush 路径上屏；GBA 虚拟按键/拖拽转向全部写共享位掩码 `g_doom_host.btn_mask`，引擎 `I_ProcessKeyEvents()` 每循环读取并边沿检测成 DOOM 事件——绕开 CST816 单点触摸不能多按的限制（方向键锁存）。

**Tech Stack:** ESP-IDF 5.5、LVGL 8.3.11（canvas + indev）、GBADoom（PrBoom 分支，GPL）、WAD 经 `esp_partition_mmap` 零拷贝读取。

---

## 已核实的关键事实（实施者不必再查）

| 事实 | 出处 |
|---|---|
| 引擎键码只有 10 个：`KEYD_A=1 B=2 L=3 R=4 UP=5 DOWN=6 LEFT=7 RIGHT=8 START=9 SELECT=10`（`#define` 在 `GBADoom/include/doomdef.h`） | 实测 grep |
| 引擎语义：**`KEYD_B` = 开火、`KEYD_A` = 使用/开门**（passport 版按键表 UP→前进 / DOWN→右转+KEYD_A / OK→KEYD_B 已真机验证） | ai-passport-doom README §按键映射 |
| 引擎入口：`I_PreInitGraphics → I_Init → Z_Init → InitGlobals → D_DoomMain`（后者不返回），任务栈 16KB | passport `main/main.c` |
| `clock()` 必须 `--wrap` 到 `esp_timer_get_time()`（返回**微秒**），否则引擎 tic 循环忙死 | passport `i_system_esp32.c` 头注释 |
| 引擎内部渲染 240×160 **8bpp 调色板索引**，`I_GetBackBuffer()` 返回的 short* 实际是 38400 字节索引缓冲 | passport `i_system_esp32.c` |
| WAD 必须用 `DOOM1_PROCESSED.WAD`（seg_t 28 字节转换后），3,904,360 字节 < 3.75MB mmap 窗口 | passport README / WAD-FILES.md，实测 |
| SDGOODS 平台分区表**不可改**：`appdata` data/fat 位于 `0x1000000`、16MB；`otadata` 在 `0x310000` | `platform/partitions.csv` |
| SDGOODS = ESP32-S3-R8（8MB Octal PSRAM、`CONFIG_SPIRAM_USE_MALLOC=y`、32MB Flash）、ST77916 QSPI 360×360、**CST816 单点触摸**（`CONFIG_LV_INDEV_DEF_READ_PERIOD=30`）、LVGL 16bpp、`CONFIG_FREERTOS_HZ=100`（不动它） | sdkconfig / sdgoods_board.h |
| LVGL flush 走 `sdgoods_lcd_draw_bitmap_safe`，绘制缓冲必须在 SRAM；**canvas 大缓冲放 PSRAM 安全**（LVGL 逐条带读取） | ARCHITECTURE.md §4（已随模板删除，结论沉淀于此） |
| 触摸坐标由 LVGL indev 在 `lv_timer_handler()` 里读，应用层用 `lv_indev` 事件即可，**DOOM 不得自起触摸任务** | `sdgoods_input.c` `touchpad_read` |
| 应用注册：`sdgoods_app_t{label_zh,label_en,icon,show,poll}` + `sdgoods_app_shell_bind()`；poll 里 `sdgoods_app_shell_is_app_active()` 判前台 | 派生模板 app_template.c |
| 派生工具 `--dest` 要求目标目录不存在；`--no-commit` 只 init+add 不 commit；Windows GBK 控制台需 `$env:PYTHONIOENCODING='utf-8'` | 实测 |
| 本机 IDF：`D:\1.Soft\Espressif\frameworks\esp-idf-v5.5.5`，venv `idf5.5_py3.11_env`；系统 python 3.14 会让 export.ps1 找不到 venv，**必须先设 `IDF_PYTHON_ENV_PATH`** | build.bat（bambu-monitor）|
| 单应用烧录：app 写 `0x10000` + 擦 otadata `0x310000..0x2000` | SINGLE_APP_FIRMWARE.md（已删，结论在此） |
| 圆屏弦宽约束：可用宽度 ≈ `2*sqrt(180²-dy²)`，控件几何按此校验 | 同上 |
| GBADoom 本地：`D:\2.Project\GBADoom`（分支 esp32-ai-passport 已 checkout）；新工程默认路径 `../GBADoom` = `D:\2.Project\SDGOODS\GBADoom`（**需在 sdgoods-doom 下再放一份或 clone**） | 实测 |

## 文件结构（新建/修改总览）

```
D:\2.Project\SDGOODS\sdgoods-doom\            ← 已派生+瘦身（Task 1 完成项）
├── CMakeLists.txt                [改] 加 GBADOOM_PATH（cache，可 -D 覆盖）
├── components\doom\              [新] 引擎封装组件
│   ├── CMakeLists.txt            GBADoom GLOB + 排除表 + wrap=clock（源自 passport）
│   ├── doom_host.h               引擎⇄UI 共享接口（唯一新契约）
│   ├── i_system_sdgoods.c        平台层：时钟/调色板/帧标志/按键边沿检测（替代 i_system_esp32.c）
│   ├── esp32_wad.c               从 appdata 分区 mmap WAD（改造 passport 版）
│   └── doom_iwad.h               符号覆盖头（从 passport 原样复制）
├── main\apps\ui_doom.[ch]        [新] LVGL 屏：canvas + GBA 按键 + poll 提交帧 + 起引擎任务
├── main\apps\apps_registry.c     [改] flappy → doom；删 app_template/ui_flappy
├── main\CMakeLists.txt           [改] SRCS 换 ui_doom.c；REQUIRES 加 doom
├── main\main.c                   [已改 boot-direct] 首屏 ui_flappy_start→ui_doom_start；删 app_data_store_init 调用
├── DOOM1_PROCESSED.WAD           [复制] 烧 0x1000000
├── tools\flash_local.sh          [改] 追加 WAD 烧录步骤
├── .github\workflows\            [改] 构建时 checkout GBADoom + -DGBADOOM_PATH
└── README.md                     [重写] 移植说明 + 按键图 + WAD 管线
```

不修改：`components/sdgoods_board/`、`components/sdgoods_launcher/`、`platform/partitions.csv`、`sdkconfig` 平台契约项。

**已知取舍（MVP 不做）**：音频（GBADoom 非 GBA 路径 i_audio 是 stub，静音运行）；游戏内"退出"（引擎任务不返回，控制中心 Power 关机即整体停）；BLE 手柄；平台商店上架（WAD 超 3MB 槽，走独立固件路线）。

---

## Task 1: 派生独立工程 SDGOODS_DOOM ✅（基本完成）

- [x] **Step 1.1 前置确认**：GBADoom 分支 esp32-ai-passport ✓；sdgoods-doom 原有 `.git`（remote: YeatsLiao/sdgoods-doom）与 README ✓
- [x] **Step 1.2 派生**：`new_standalone_project.py SDGOODS_DOOM --app flappy --boot-direct --prune --no-commit` 派生到临时目录后合并（保留用户 `.git`），`project(SDGOODS_DOOM)` 已生效
- [x] **Step 1.3 仓库瘦身**（用户新增要求）：删官方文档/发布工具/模板杂物（详见顶部"仓库口径"）
- [ ] **Step 1.4 基线构建通过**：`idf.py -B build_pub build` 成功产出 `build_pub\SDGOODS_DOOM.bin`，记录大小（**≤3MB = 0x300000 红线**）。构建命令模板（本机环境坑已固化）：

```powershell
$env:IDF_PATH='D:\1.Soft\Espressif\frameworks\esp-idf-v5.5.5'
$env:IDF_TOOLS_PATH='D:\1.Soft\Espressif'
$env:IDF_PYTHON_ENV_PATH='D:\1.Soft\Espressif\python_env\idf5.5_py3.11_env'
. $env:IDF_PATH\export.ps1
idf.py -B build_pub build
```

- [ ] **Step 1.5 提交检查点**：`git status` 汇报，等用户指令。

---

## Task 2: 引擎组件 components/doom

**Files:**
- Copy: `D:\2.Project\ai-passport-doom\components\doom\CMakeLists.txt` / `doom_iwad.h` → `sdgoods-doom\components\doom\`
- Create: `components\doom\doom_host.h` / `i_system_sdgoods.c` / `esp32_wad.c`
- Modify: 根 `CMakeLists.txt`

- [ ] **Step 2.0 GBADoom 就位**：`git clone -b esp32-ai-passport D:\2.Project\GBADoom D:\2.Project\SDGOODS\GBADoom`（本地克隆即可，省网络）。

- [ ] **Step 2.1 根 CMakeLists** 替换为：

```cmake
cmake_minimum_required(VERSION 3.16)
# GBADoom 引擎源码树（分支 esp32-ai-passport）。默认同级 ../GBADoom；
# CI / 特殊路径可用 -DGBADOOM_PATH=<dir> 覆盖。
if(NOT DEFINED GBADOOM_PATH)
  set(GBADOOM_PATH "${CMAKE_CURRENT_LIST_DIR}/../GBADoom" CACHE PATH "GBADoom source tree")
endif()
if(NOT EXISTS "${GBADOOM_PATH}/source/d_main.c")
  message(FATAL_ERROR "GBADOOM_PATH=${GBADOOM_PATH} 无效。请先放置 GBADoom(esp32-ai-passport) 源码树")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SDGOODS_DOOM)
```

- [ ] **Step 2.2 复制并改 `components\doom\CMakeLists.txt`**（passport 原件基础上改两处，其余含排除表与 `-Wl,--wrap=clock` 原样保留）：

```cmake
set(ESP32_SRCS "i_system_sdgoods.c" "esp32_wad.c")
# REQUIRES 段去掉 bsp_doom：
    REQUIRES
        esp_partition
        spi_flash
    PRIV_REQUIRES
        esp_timer
        log
```

- [ ] **Step 2.3 `doom_host.h`**（引擎⇄UI 唯一契约）：

```c
/* doom_host.h - GBADoom 引擎与 SDGOODS LVGL 外壳之间的共享状态。
 * btn_mask 由 LVGL 线程（按键回调）写、引擎任务读；frame_ready 由引擎置位、
 * LVGL poll 消费。全部 volatile，32 位位掩码读写在 S3 上天然原子。 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define DOOM_BTN_UP      (1u << 0)   /* 十字键↑ 前进（KEYD_UP，锁存）      */
#define DOOM_BTN_DOWN    (1u << 1)   /* 十字键↓ 后退（KEYD_DOWN，锁存）    */
#define DOOM_BTN_LEFT    (1u << 2)   /* 十字键← 左转（KEYD_LEFT，锁存）    */
#define DOOM_BTN_RIGHT   (1u << 3)   /* 十字键→ 右转（KEYD_RIGHT，锁存）   */
#define DOOM_BTN_A       (1u << 4)   /* A 开火 → KEYD_B（passport 实证）   */
#define DOOM_BTN_B       (1u << 5)   /* B 使用 → KEYD_A（开门/确认）       */
#define DOOM_BTN_L       (1u << 6)   /* L 切枪 → KEYD_L                    */
#define DOOM_BTN_R       (1u << 7)   /* R 切枪 → KEYD_R                    */
#define DOOM_BTN_START   (1u << 8)   /* START 菜单 → KEYD_START            */
#define DOOM_BTN_SELECT  (1u << 9)   /* SELECT → KEYD_SELECT               */
#define DOOM_BTN_DRAG_L  (1u << 10)  /* 画面上拖拽左转（点按，与 LEFT 合并）*/
#define DOOM_BTN_DRAG_R  (1u << 11)  /* 画面上拖拽右转                     */

#define DOOM_GROUP_MOVE  (DOOM_BTN_UP | DOOM_BTN_DOWN)
#define DOOM_GROUP_TURN  (DOOM_BTN_LEFT | DOOM_BTN_RIGHT)

typedef struct {
    volatile uint32_t btn_mask;     /* DOOM_BTN_* 位集合                   */
    volatile bool     frame_ready;  /* 引擎完成一帧；UI 消费后清零          */
} doom_host_t;

extern doom_host_t g_doom_host;     /* 定义在 i_system_sdgoods.c           */

/* UI 侧提交帧用（i_system_sdgoods.c 实现）： */
unsigned char *I_GetBackBufferBytes(void);        /* 240*160 的 8bpp 索引缓冲 */
const uint16_t *I_GetPalette565(void);            /* 256 项原生小端 RGB565（勿 bswap） */
#define DOOM_FB_W 240
#define DOOM_FB_H 160
```

- [ ] **Step 2.4 `i_system_sdgoods.c`**（骨架来自 passport `i_system_esp32.c`；删除全部直刷 SPI/遮挡条/红字逻辑，刷屏交给 LVGL；**不再字节交换**，LVGL flush 路径处理 `LV_COLOR_16_SWAP`）：

```c
/* i_system_sdgoods.c - GBADoom 平台层（SDGOODS 圆屏版）
 * 引擎渲染 240x160 调色板索引 → 置 frame_ready，由 LVGL 线程转 RGB565 上 canvas。
 * 输入：读 g_doom_host.btn_mask（虚拟 GBA 键）→ 合并成引擎键集 → 边沿检测 D_PostEvent。
 * 计时：--wrap=clock（见 CMakeLists），必须返回微秒。 */
#include "doomtype.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include "i_system_e32.h"
#include "i_sound.h"
#include "doom_host.h"

doom_host_t g_doom_host;

static byte s_backbuffer_data[DOOM_FB_W * DOOM_FB_H];   /* 8bpp 索引，.bss */
static uint16_t s_palette[256];                          /* 原生小端 RGB565 */

clock_t __wrap_clock(void) { return (clock_t)esp_timer_get_time(); }

unsigned char *I_GetBackBufferBytes(void) { return s_backbuffer_data; }
const uint16_t *I_GetPalette565(void) { return s_palette; }

void I_Init(void) { I_InitSound(); }
void I_InitScreen_e32(void) { ESP_LOGI("doom_plat", "screen handled by LVGL"); }

void I_CreateBackBuffer_e32(void)
{
    memset(s_backbuffer_data, 0, sizeof(s_backbuffer_data));
    memset(s_palette, 0, sizeof(s_palette));
}

int I_GetVideoWidth_e32(void)  { return DOOM_FB_W; }
int I_GetVideoHeight_e32(void) { return DOOM_FB_H; }
unsigned short *I_GetBackBuffer(void)  { return (unsigned short *)s_backbuffer_data; }
unsigned short *I_GetFrontBuffer(void) { return (unsigned short *)s_backbuffer_data; }

void I_SetPallete_e32(const byte *palette)
{
    if (!palette) return;
    for (int i = 0; i < 256; i++) {
        unsigned int r = palette[i * 3 + 0], g = palette[i * 3 + 1], b = palette[i * 3 + 2];
        s_palette[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
}

/* 引擎每帧调一次：交标志位 + 限流（等 UI 消费，最长 20 tick≈200ms；
 * 每 4 帧主动让出喂 IDLE 看门狗——与 passport 同节奏）。 */
void I_FinishUpdate_e32(const byte *srcBuffer, const byte *pal,
                        const unsigned int w, const unsigned int h)
{
    (void)srcBuffer; (void)pal; (void)w; (void)h;
    static unsigned int n;
    g_doom_host.frame_ready = true;
    for (int i = 0; i < 20 && g_doom_host.frame_ready; i++) vTaskDelay(1);
    if ((++n & 3) == 0) vTaskDelay(1);
}

/* btn 位 → 引擎 KEYD_* 码。同一 KEYD 可由两个来源触发（锁存键 + 拖拽）。 */
static const struct { uint32_t bit; int key; } s_keymap[] = {
    { DOOM_BTN_UP,      KEYD_UP    }, { DOOM_BTN_DOWN,    KEYD_DOWN  },
    { DOOM_BTN_LEFT,    KEYD_LEFT  }, { DOOM_BTN_RIGHT,   KEYD_RIGHT },
    { DOOM_BTN_DRAG_L,  KEYD_LEFT  }, { DOOM_BTN_DRAG_R,  KEYD_RIGHT },
    { DOOM_BTN_A,       KEYD_B     }, { DOOM_BTN_B,       KEYD_A     },
    { DOOM_BTN_L,       KEYD_L     }, { DOOM_BTN_R,       KEYD_R     },
    { DOOM_BTN_START,   KEYD_START }, { DOOM_BTN_SELECT,  KEYD_SELECT},
};

void I_ProcessKeyEvents(void)
{
    uint32_t mask = g_doom_host.btn_mask;
    static uint32_t s_prev_keys;                       /* 已按下的 KEYD_* 集合 */
    uint32_t cur_keys = 0;
    for (unsigned i = 0; i < sizeof(s_keymap)/sizeof(s_keymap[0]); i++)
        if (mask & s_keymap[i].bit) cur_keys |= 1u << s_keymap[i].key;

    for (int k = 1; k <= KEYD_SELECT; k++) {
        bool was = (s_prev_keys >> k) & 1u, now = (cur_keys >> k) & 1u;
        if (was && !now) { event_t ev = { .type = ev_keyup,   .data1 = k }; D_PostEvent(&ev); }
        if (now && !was) { event_t ev = { .type = ev_keydown, .data1 = k }; D_PostEvent(&ev); }
    }
    s_prev_keys = cur_keys;
}

void I_Error(const char *error, ...)
{
    char msg[512];
    va_list v; va_start(v, error); vsnprintf(msg, sizeof(msg), error, v); va_end(v);
    ESP_LOGE("DOOM", "I_Error: %s", msg);
    while (1) vTaskDelay(portMAX_DELAY);
}

void I_Quit_e32(void) { ESP_LOGW("doom_plat", "I_Quit_e32 (no-op)"); }
```

> 注意：`doomtype.h` 必须最先 include（FreeRTOS 的 true/false 宏与 boolean enum 冲突，passport main.c 头注释的坑）；`event_t .type/.data1`、`ev_keyup/ev_keydown`、`KEYD_*`=1..10 均与 passport 用法一致（已编译验证过）。

- [ ] **Step 2.5 `esp32_wad.c`**（passport 版改造：分区 label `wad`→`appdata`、mmap 限 3.75MB 窗口；本工程不把 appdata 挂 FAT，裸数据合法）：

```c
/* esp32_wad.c - 从平台分区表 appdata(0x1000000,16MB) 头部 mmap WAD。
 * 烧录：esptool.py write_flash 0x1000000 DOOM1_PROCESSED.WAD */
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "doom_iwad.h"

#define WAD_MAP_BYTES 0x3C0000u   /* 3.75MB > DOOM1_PROCESSED.WAD 3,904,360B */

static const char *TAG = "doom_wad";
const unsigned char *doom_iwad;
unsigned int doom_iwad_len;

int doom_wad_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "appdata");
    if (!part) { ESP_LOGE(TAG, "appdata partition not found"); return -1; }

    size_t map_len = part->size < WAD_MAP_BYTES ? part->size : WAD_MAP_BYTES;
    spi_flash_mmap_handle_t handle;
    const void *mapped = NULL;
    esp_err_t err = esp_partition_mmap(part, 0, map_len, SPI_FLASH_MMAP_DATA,
                                       &mapped, &handle);
    if (err != ESP_OK) { ESP_LOGE(TAG, "mmap %zuB failed: %s", map_len, esp_err_to_name(err)); return -1; }
    if (memcmp(mapped, "IWAD", 4) != 0) {
        ESP_LOGE(TAG, "bad header '%.4s' — WAD 未烧录？write_flash 0x1000000", (const char*)mapped);
        return -1;
    }
    doom_iwad = (const unsigned char *)mapped;
    doom_iwad_len = (unsigned int)map_len;   /* 引擎只信 WAD 头里的目录偏移，用 map 长度安全 */
    ESP_LOGI(TAG, "WAD mmap ok: addr=0x%lx map=%uKB", (unsigned long)part->address, (unsigned)(map_len/1024));
    return 0;
}
```

> `doom_iwad.h` 从 passport 原样复制；若其签名与上述定义不符，以复制来的头为准同步 .c。组件 `INCLUDE_DIRS "."` 必须排在 `"${GBADOOM_PATH}/include"` 前（覆盖引擎编译期内嵌 WAD 声明）。

- [ ] **Step 2.6 `main/CMakeLists.txt` REQUIRES 追加 `doom`**。
- [ ] **Step 2.7 编译验证**（此时无人调用引擎，只要求全部编译过）。常见失败：`GBADOOM_PATH 无效`→Step 2.0/2.1；找不到 `i_system_e32.h`→INCLUDE 顺序；`--wrap=clock` 失效→CMake 尾部 link options。
- [ ] **Step 2.8 提交检查点**：汇报，等指令。

---

## Task 3: 应用层 ui_doom（画面 + GBA 虚拟键）

**Files:**
- Create: `main\apps\ui_doom.h` / `ui_doom.c`
- Modify: `main\apps\apps_registry.c`、`main\CMakeLists.txt`、`main\main.c`
- Delete: `main\apps\ui_flappy.[ch]`、`app_template.[ch]`（用户要求纯 DOOM 工程）

- [ ] **Step 3.1 `ui_doom.h`**：

```c
#pragma once
#include "lvgl.h"
void ui_doom_start(void);
void ui_doom_poll(void);
```

- [ ] **Step 3.2 `ui_doom.c`**（canvas 304×168 @ (28,62)，上带 L/R/START、下带十字键+AB+SELECT、画面拖拽层；几何按弦宽校验过）：

```c
#include "ui_doom.h"
#include "doomtype.h"                    /* 必须最先（passport 宏冲突坑） */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdgoods_board.h"
#include "doom_host.h"

static const char *TAG = "doom_ui";
extern void I_PreInitGraphics(void);
extern void I_Init(void);
extern void Z_Init(void);
extern void InitGlobals(void);
extern void D_DoomMain(void);
extern int  doom_wad_init(void);

#define CANVAS_W 304
#define CANVAS_H 168
#define CANVAS_X ((360 - CANVAS_W) / 2)  /* 28 */
#define CANVAS_Y 62                       /* 角点距圆心 √(152²+84²)=173 < 180 ✓ */

static lv_obj_t   *s_scr, *s_canvas;
static lv_color_t *s_cbuf;               /* 304*168*2 = 102KB → PSRAM */
static bool       s_engine_started;
static int32_t    s_drag_x0;
static bool       s_tap_fire;
static int64_t    s_fps_t0;  static int s_fps_n;

/* ---- 帧提交：索引→RGB565 最近邻放大（x 1.2667 / y 1.05） ---- */
static void commit_frame(void)
{
    const unsigned char *src = I_GetBackBufferBytes();
    const uint16_t *pal = I_GetPalette565();
    for (int y = 0; y < CANVAS_H; y++) {
        const unsigned char *srow = src + (y * DOOM_FB_H / CANVAS_H) * DOOM_FB_W;
        lv_color_t *drow = s_cbuf + y * CANVAS_W;
        for (int x = 0; x < CANVAS_W; x++)
            drow[x].full = pal[srow[x * DOOM_FB_W / CANVAS_W]];
    }
    lv_obj_invalidate(s_canvas);
    g_doom_host.frame_ready = false;
    s_fps_n++;
    int64_t now = esp_timer_get_time();
    if (now - s_fps_t0 > 5 * 1000 * 1000) {
        ESP_LOGI(TAG, "%d fps", (int)(s_fps_n * 1000000ll / (now - s_fps_t0)));
        s_fps_t0 = now; s_fps_n = 0;
    }
}

/* ---- 按键事件 ---- */
static void btn_hold_cb(lv_event_t *e)   /* 点按类：按下置位、抬起清零 */
{
    uint32_t bit = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) g_doom_host.btn_mask |= bit;
    else g_doom_host.btn_mask &= ~bit;
    s_tap_fire = false;                    /* 真按 A 键时取消 tap 脉冲 */
}
static void btn_latch_cb(lv_event_t *e)  /* 十字键：锁存 + 组内互斥 */
{
    uint32_t bit = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    uint32_t grp = (bit & (DOOM_BTN_UP | DOOM_BTN_DOWN)) ? DOOM_GROUP_MOVE : DOOM_GROUP_TURN;
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    if (g_doom_host.btn_mask & bit) g_doom_host.btn_mask &= ~bit;        /* 再点=松开 */
    else { g_doom_host.btn_mask &= ~grp; g_doom_host.btn_mask |= bit; }
}
static void pad_event_cb(lv_event_t *e)  /* 画面上：拖=转向(点按)，单击=开火脉冲 */
{
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t p; lv_indev_get_point(indev, &p);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:  s_drag_x0 = p.x; break;
    case LV_EVENT_PRESSING: {
        int32_t dx = p.x - s_drag_x0;
        g_doom_host.btn_mask &= ~(DOOM_BTN_DRAG_L | DOOM_BTN_DRAG_R);
        if (dx >  15) g_doom_host.btn_mask |= DOOM_BTN_DRAG_R;
        if (dx < -15) g_doom_host.btn_mask |= DOOM_BTN_DRAG_L;
        break; }
    case LV_EVENT_RELEASED: {
        int32_t moved = p.x - s_drag_x0;
        g_doom_host.btn_mask &= ~(DOOM_BTN_DRAG_L | DOOM_BTN_DRAG_R);
        if (moved > -15 && moved < 15) s_tap_fire = true;   /* 近似原地点击→开火脉冲 */
        break; }
    default: break;
    }
}

static lv_obj_t *make_btn(lv_obj_t *parent, int cx, int cy, int r,
                          const char *txt, uint32_t bit, bool latch)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, r * 2, r * 2);
    lv_obj_set_pos(b, cx - r, cy - r);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
    lv_obj_t *lbl = lv_obj_get_child(b, 0);
    lv_label_set_text(lbl, txt);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(b, latch ? btn_latch_cb : btn_hold_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)bit);
    if (!latch) lv_obj_add_event_cb(b, btn_hold_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)bit);
    return b;
}

/* ---- 引擎任务（core 0，永不返回） ---- */
static void doom_task(void *arg)
{
    (void)arg;
    if (doom_wad_init() != 0) { ESP_LOGE(TAG, "WAD load failed, task exit"); vTaskDelete(NULL); }
    I_PreInitGraphics(); I_Init(); Z_Init(); InitGlobals(); D_DoomMain();
}

static void on_menu_exit(void) { /* 单应用：引擎继续跑，画面留在原位 */ }

void ui_doom_start(void)
{
    if (s_scr) { lv_scr_load(s_scr); return; }          /* 幂等守卫 */
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_cbuf = lv_mem_alloc(CANVAS_W * CANVAS_H * sizeof(lv_color_t));
    if (!s_cbuf) { ESP_LOGE(TAG, "canvas buf OOM"); return; }
    memset(s_cbuf, 0, CANVAS_W * CANVAS_H * sizeof(lv_color_t));
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_buffer(s_canvas, s_cbuf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_canvas, CANVAS_X, CANVAS_Y);

    /* 拖拽层：盖住 canvas，吃掉画面区手势 */
    lv_obj_t *pad = lv_obj_create(s_scr);
    lv_obj_set_size(pad, CANVAS_W, CANVAS_H);
    lv_obj_set_pos(pad, CANVAS_X, CANVAS_Y);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pad, pad_event_cb, LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(pad, pad_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(pad, pad_event_cb, LV_EVENT_RELEASED, NULL);

    /* 下带：十字键中心(112,290)，AB 菱形(232,306)/(274,274)，SELECT(180,344)
     * 上带：L(115,42) R(245,42) START(180,22)。全部按弦宽公式校验在圆内。 */
    make_btn(s_scr, 112, 255, 20, LV_SYMBOL_UP,    DOOM_BTN_UP,    true);
    make_btn(s_scr, 112, 324, 20, LV_SYMBOL_DOWN,  DOOM_BTN_DOWN,  true);
    make_btn(s_scr,  76, 290, 20, LV_SYMBOL_LEFT,  DOOM_BTN_LEFT,  true);
    make_btn(s_scr, 148, 290, 20, LV_SYMBOL_RIGHT, DOOM_BTN_RIGHT, true);
    make_btn(s_scr, 232, 306, 20, "B",  DOOM_BTN_B,      false);   /* 使用/开门 */
    make_btn(s_scr, 274, 274, 20, "A",  DOOM_BTN_A,      false);   /* 开火 */
    make_btn(s_scr, 115,  42, 15, "L",  DOOM_BTN_L,      false);   /* 切枪 */
    make_btn(s_scr, 245,  42, 15, "R",  DOOM_BTN_R,      false);
    make_btn(s_scr, 180,  22, 14, "ST", DOOM_BTN_START,  false);
    make_btn(s_scr, 180, 344, 14, "SE", DOOM_BTN_SELECT, false);

    lv_scr_load(s_scr);
    sdgoods_app_shell_bind(s_scr);                    /* 下滑控制中心/手势照旧 */
    sdgoods_app_shell_set_exit_cb(on_menu_exit);

    if (!s_engine_started) {
        s_engine_started = true;
        xTaskCreatePinnedToCore(doom_task, "doom", 16384, NULL, 5, NULL, 0);
    }
    s_fps_t0 = esp_timer_get_time();
}

void ui_doom_poll(void)
{
    if (!sdgoods_app_shell_is_app_active()) return;
    if (g_doom_host.frame_ready) commit_frame();
    /* tap 开火脉冲：置位一拍被引擎读到，下一拍清除 */
    static bool pulse;
    if (s_tap_fire && !pulse) { g_doom_host.btn_mask |= DOOM_BTN_A;  pulse = true;  s_tap_fire = false; }
    else if (pulse)           { g_doom_host.btn_mask &= ~DOOM_BTN_A; pulse = false; }
}
```

> 实施注意：① LVGL 8.3 `lv_btn` 默认含 label 子对象，`lv_obj_get_child(b,0)` 若为空则手动 `lv_label_create(b)`；② A/B 语义真机若反了，对调 `s_keymap` 两行即可；③ 按钮文本全 ASCII/LV_SYMBOL，**无新中文，无需重跑 gen_fonts.py**。

- [ ] **Step 3.3 接线**：`apps_registry.c` include `ui_doom.h`，注册表条目 `{ .label_zh="DOOM", .label_en="DOOM", .icon="doom", .show=ui_doom_start, .poll=ui_doom_poll }`；`main.c` boot-direct 生成的 `ui_flappy_start();` → `ui_doom_start();`（nav 三函数内同步替换）；`main/CMakeLists.txt` SRCS：删 `apps/ui_flappy.c`、`apps/app_template.c`，加 `apps/ui_doom.c`。
- [ ] **Step 3.4 删文件**：`main/apps/ui_flappy.[ch]`、`app_template.[ch]`。
- [ ] **Step 3.5**：`main.c` 删 `app_data_store_init();` 调用（appdata 头 3.75MB 被 WAD 占用，不可再挂 FAT；`app_data_store.c/h` 从 SRCS 移除并删文件——纯 DOOM 工程无持久数据需求）。
- [ ] **Step 3.6 编译**：`idf.py -B build_pub build`；记录 bin 大小（≤3MB）。
- [ ] **Step 3.7 提交检查点**：汇报，等指令。

---

## Task 4: WAD 资产与烧录链路

- [ ] **Step 4.1 复制 WAD**：`Copy-Item D:\2.Project\ai-passport-doom\DOOM1_PROCESSED.WAD .\DOOM1_PROCESSED.WAD`（重生成需 passport 的 merge_pwad.py + GbaWadUtil，README 里写清管线即可）。
- [ ] **Step 4.2 `tools/flash_local.sh`**：在烧 app 的 esptool 调用之后追加：

```bash
# 谷仓次元屏：WAD 烧进 appdata 分区头部（分区表不动）
WAD="$ROOT/DOOM1_PROCESSED.WAD"
if [ -f "$WAD" ]; then
  echo "· 烧录 WAD -> 0x1000000 (appdata)"
  esptool.py --chip esp32s3 -p "$PORT" -b "$BAUD" --before default_reset --after hard_reset \
      write_flash 0x1000000 "$WAD" || { echo "✗ WAD 烧录失败" >&2; exit 1; }
else
  echo "· 跳过 WAD（仓库内无 DOOM1_PROCESSED.WAD）"
fi
```

（实施时先读脚本尾部现有 esptool 调用行，照其变量名/形式插入。）
- [ ] **Step 4.3 README 快速开始里的 Windows 等价命令**：

```powershell
idf.py -B build_pub flash                 # 烧固件（含引导层）
esptool.py --chip esp32s3 -p COMx erase_region 0x310000 0x2000   # 擦 otadata→SINGLE 直启
esptool.py --chip esp32s3 -p COMx -b 921600 write_flash 0x1000000 DOOM1_PROCESSED.WAD
```

- [ ] **Step 4.4 提交检查点**：汇报，等指令。

---

## Task 5: 真机集成与调优（需用户接设备）

- [ ] **Step 5.1 烧录**（固件 + 擦 otadata + WAD）。
- [ ] **Step 5.2 串口验证**逐条核对：`doom_wad: WAD mmap ok: addr=0x1000000 map=3840KB`（失败=没烧 WAD）；GBADoom 初始化 banner；`doom_ui: NN fps`（每 5s）。**验收基线 ≥12 fps**。
- [ ] **Step 5.3 截屏核对几何**：`python tools/screenshot_recv.py -t -o shot.jpg`——canvas 无错位、10 键全在圆内、标题菜单可操作。
- [ ] **Step 5.4 交互人工验收清单**：点击画面=开火、按住 A=连发；拖拽=转向松手即停；▲▼ 锁存行进（可单指"移动中开火"）；L/R 切枪、B 开门、START 菜单；顶部下滑出控制中心；Power 关机；≥5 分钟无看门狗复位。
- [ ] **Step 5.5 性能不足调优顺序**：① commit_frame 换预计算 x 映射 LUT 消除法；② canvas 降 288×160；③ sdkconfig.defaults 开 PERF 优化。禁改平台契约项。
- [ ] **Step 5.6 平台回归**：首屏直入 DOOM / 下滑出控制中心 / `read_flash 0x10050 32` 回读 `SDGOODS_DOOM`。
- [ ] **Step 5.7 提交检查点**。

---

## Task 6: 文档 / 许可 / CI 收尾

- [ ] **Step 6.1 README.md 重写**（仓库口径=纯 DOOM 工程）：硬件表（S3-R8/360 圆屏 QSPI/CST816 单点/8MB PSRAM）；按键图 + 单指设计（锁存十字键/拖拽转向/tap 开火）；GBADoom 依赖（clone 命令 + `-DGBADOOM_PATH`）；WAD 两步管线说明；烧录三步；**许可声明**（components/doom+引擎 GPL、平台组件 Apache-2.0、WAD 属 id Software 仅 shareware 渠道随固件分发）。
- [ ] **Step 6.2 CI**：派生的 workflow 构建段改为 checkout 本仓库（path: main-repo）+ checkout `YeatsLiao/GBADoom@esp32-ai-passport`（path: gbadoom），构建传 `-DGBADOOM_PATH=${{ github.workspace }}/gbadoom`；保留 IDF setup 与产物上传。
- [ ] **Step 6.3 终审**：对全部改动跑一次 CodeReview（规格符合性 + 质量），问题回给实现方修复后复审。
- [ ] **Step 6.4 提交检查点（最终）**：`git status` 全量汇报，等用户明确说提交。

---

## Test Plan 汇总

| 层 | 手段 | 通过标准 |
|---|---|---|
| 编译 | `idf.py -B build_pub build` + CI | 0 error；bin ≤ 3MB |
| WAD | 串口 `WAD mmap ok` + 进游戏 | E1M1 可见可玩 |
| 性能 | `doom_ui: NN fps` | ≥12 fps（目标 15~25） |
| 输入 | Task 5.4 清单 | 移动+开火可同指完成；无卡键 |
| 平台 | 三条红线回归 | 首屏/控制中心/工程名 |
| 稳定 | ≥5 分钟运行 | 无 WDT 复位、无黑条 |

## Assumptions / Risks

1. **引擎 A/B 语义**：以 passport 实证为准（KEYD_B=开火）；真机反了则对调映射（Task 3 预案）。
2. **bin 超 3MB**：基线先看大小；超了降日志等级/尺寸优化/关 BLE（sdkconfig.defaults 层，不动平台契约）。
3. **拖拽层 vs 控制中心手势**：pad 只盖 canvas（y62..230），shell 手势层优先；真机验证，不行则 pad 让出 20px。
4. WAD 版权：沿用 passport 做法（shareware 随仓库分发）；GitHub 政策有异议就转 release 附件。
5. `lv_canvas`+16bpp+`LV_COLOR_16_SWAP`：canvas 缓冲写原生小端 RGB565，flush 自动交换——真机截屏若呈红蓝反色，则把 `I_SetPallete_e32` 增加 bswap（一行开关）。
