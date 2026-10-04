/* i_system_sdgoods.c - GBADoom 平台层（SDGOODS 圆屏版）
 * 引擎渲染 240x160 调色板索引 → 置 frame_ready，由 LVGL 线程转 RGB565 上 canvas。
 * 输入：读 g_doom_host.btn_mask（虚拟 GBA 键）→ 合并成引擎键集 → 边沿检测 D_PostEvent。
 * 计时：--wrap=clock（见 CMakeLists），返回以 CLOCKS_PER_SEC 为单位的时间（非微秒，见 __wrap_clock）。
 *
 * 与 passport i_system_esp32.c 的差异：
 *   - 删除全部直刷 SPI / 底部遮挡条 / 红字逻辑（刷屏交给 LVGL canvas + 平台 flush）；
 *   - 调色板按平台 LV_COLOR_16_SWAP=y 输出**字节交换后**的 RGB565（见 I_SetPallete_e32）。 */

/* doomtype.h 必须先于 FreeRTOS 包含：其 true/false/boolean 定义与 freertos 头冲突
 * （passport main.c 头注释记录过的坑）。 */
#include "doomtype.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include "i_system_e32.h"
#include "i_sound.h"
#include "doom_host.h"

doom_host_t g_doom_host;

/* backbuffer 放 PSRAM：S3 内部 DRAM 要留给 LVGL 绘制缓冲（必须在 SRAM，见平台
 * 架构红线）+ WiFi/BLE/音频，把 38KB 索引帧塞进 .bss 会撑爆 dram0（实测溢出）。PSRAM
 * 访问慢一些，但 I_FinishUpdate 的帧握手已经限流，不是瓶颈。palette 只 512B，留 .bss。 */
static unsigned char *s_backbuffer_data;
static uint16_t s_palette[256];                          /* 原生小端 RGB565 */

/* clock() 必须返回以 CLOCKS_PER_SEC 为单位的时间：引擎 I_GetTime 用
 * clock()/(CLOCKS_PER_SEC/TICRATE) 换算 35Hz 游戏 tic。xtensa/picolibc 的
 * CLOCKS_PER_SEC 实测=1000（非 passport 注释假设的 1e6），若直接返回 esp_timer 微秒，
 * I_GetTime 会快 1000×→引擎不按 35Hz 节流→“倍速”。这里把 µs 折算成 CLOCKS_PER_SEC
 * 单位，CPS 在换算中约掉，对任意 CLOCKS_PER_SEC 值都得到正确的 35Hz。
 * （newlib 原生 clock() 恒返回 0 会使 TryRunTics/D_Wipe 死循环喂不了看门狗，故必须 wrap。） */
clock_t __wrap_clock(void)
{
    return (clock_t)((int64_t)esp_timer_get_time() * CLOCKS_PER_SEC / 1000000);
}

unsigned char *I_GetBackBufferBytes(void) { return s_backbuffer_data; }
const uint16_t *I_GetPalette565(void) { return s_palette; }

void I_Init(void) { I_InitSound(); }
void I_InitScreen_e32(void) { ESP_LOGI("doom_plat", "screen handled by LVGL"); }

void I_CreateBackBuffer_e32(void)
{
    if (!s_backbuffer_data) {
        s_backbuffer_data = heap_caps_malloc(DOOM_FB_W * DOOM_FB_H, MALLOC_CAP_SPIRAM);
        ESP_LOGI("doom_plat", "backbuffer %dB @ %p (PSRAM)", DOOM_FB_W * DOOM_FB_H, s_backbuffer_data);
    }
    if (!s_backbuffer_data) {                       /* PSRAM 分配失败即死，别让下面 memset 空指针崩 */
        ESP_LOGE("doom_plat", "backbuffer PSRAM alloc FAILED");
        I_Error("backbuffer alloc failed");
        return;
    }
    memset(s_backbuffer_data, 0, DOOM_FB_W * DOOM_FB_H);
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
        /* 原生小端 RGB565（R 在高字节）；平台 CONFIG_LV_COLOR_16_SWAP=y 期望喂进 canvas
         * 的 lv_color_t.full 是字节交换后的序（见 sdgoods_screenshot.c：LVGL 缓冲高字节
         * 在前，否则 R/B 错乱），故此处做一次 bswap。 */
        uint16_t w = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        s_palette[i] = (uint16_t)((w >> 8) | (w << 8));
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
