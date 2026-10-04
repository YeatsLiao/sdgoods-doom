/* ui_doom.c - DOOM 应用层（SDGOODS 圆屏 · 360x360）
 *
 * 布局：中间 canvas 304x168 @(28,62) 显示 240x160 放大帧；
 *       全部按键排在画面下方的可达区（避开圆屏顶端触摸盲区与 0~50 顶部下滑捕获带）：
 *       左十字键（按住才动、松手即停）+ 右 A/B + 底部 START/SELECT + 中上 L/R；
 *       画面区不挂手势（转向=◀▶、开火=A）。
 * 帧链路：引擎任务(core0) 写索引 backbuffer → frame_ready → 本文件 poll 里
 *         转 RGB565 写 canvas(PSRAM) → lv_obj_invalidate → 平台 SRAM 条带 flush。
 * 输入链路：LVGL 回调写 g_doom_host.btn_mask → 引擎 I_ProcessKeyEvents 边沿检测。
 */
#include "doomtype.h"                    /* 必须最先（FreeRTOS 宏冲突坑，passport 实证） */
#include "ui_doom.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdgoods_board.h"
#include "sdgoods_tap.h"
#include "doom_host.h"

/* 输入链自测开关：置 1 时开机自动合成一遗“开菜单 → New Game → 选章节 → 选难度”
 * 序列（仅用于无人工介入的回归测试）。正式固件保持 0。 */
#define DOOM_INPUT_SELFTEST 0
#define SELFTEST_ST_X  200
#define SELFTEST_ST_Y  300

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
static lv_color_t *s_cbuf;               /* 304*168*2 = 102KB → lv_mem_alloc=malloc → PSRAM */
static bool       s_engine_started;
static lv_obj_t  *s_btns[16];            /* 游戏按键，bind 后统一提到最上层压过 CC 顶部捕获带 */
static int        s_btn_n;
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
static void btn_hold_cb(lv_event_t *e)   /* 点按/按住类：按下置位、抬起清零 */
{
    uint32_t bit = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    bool down = (lv_event_get_code(e) == LV_EVENT_PRESSED);
#if DOOM_INPUT_SELFTEST
    ESP_LOGW("doom_ui", "HITTEST btn bit=0x%08lx %s", (unsigned long)bit, down ? "DOWN" : "up");
#endif
    if (down) g_doom_host.btn_mask |= bit;
    else g_doom_host.btn_mask &= ~bit;
}
static void btn_latch_cb(lv_event_t *e)  /* 十字键：锁存 + 组内互斥 */
{
    uint32_t bit = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    uint32_t grp = (bit & (DOOM_BTN_UP | DOOM_BTN_DOWN)) ? DOOM_GROUP_MOVE : DOOM_GROUP_TURN;
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) return;
    if (g_doom_host.btn_mask & bit) g_doom_host.btn_mask &= ~bit;        /* 再点=松开 */
    else { g_doom_host.btn_mask &= ~grp; g_doom_host.btn_mask |= bit; }
}
static lv_obj_t *make_btn(lv_obj_t *parent, int cx, int cy, int r,
                          const char *txt, uint32_t bit, bool latch)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, r * 2, r * 2);
    lv_obj_set_pos(b, cx - r, cy - r);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
    lv_obj_t *lbl = lv_obj_get_child(b, 0);          /* LVGL 8.3 lv_btn 默认自带 label */
    if (!lbl) lbl = lv_label_create(b);
    lv_label_set_text(lbl, txt);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(b, latch ? btn_latch_cb : btn_hold_cb, LV_EVENT_PRESSED, (void *)(uintptr_t)bit);
    if (!latch) {                                    /* 按住类：抬起 / 手指滑出都立即松开，防卡键 */
        lv_obj_add_event_cb(b, btn_hold_cb, LV_EVENT_RELEASED,   (void *)(uintptr_t)bit);
        lv_obj_add_event_cb(b, btn_hold_cb, LV_EVENT_PRESS_LOST, (void *)(uintptr_t)bit);
    }
    if (s_btn_n < (int)(sizeof(s_btns) / sizeof(s_btns[0]))) s_btns[s_btn_n++] = b;
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
    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_buffer(s_canvas, s_cbuf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_canvas, CANVAS_X, CANVAS_Y);

    /* 画面区不再挂拖拽/点击层：转向用 ◀▶ 键、开火用 A 键（单点触摸下更直观，也避免误触）。 */

    /* 全部按键集中在画面下方 y≈232~316 的可达区（均在圆盘内、避开顶端触摸盲区与 0~50 捕获带）。
     * 坐标已按圆方程 x²+y²（以圆心 180,180 为原点）校验在半径 180 内。 */
    make_btn(s_scr,  82, 244, 16, LV_SYMBOL_UP,    DOOM_BTN_UP,    false);   /* 前进 */
    make_btn(s_scr,  82, 300, 16, LV_SYMBOL_DOWN,  DOOM_BTN_DOWN,  false);   /* 后退 */
    make_btn(s_scr,  54, 272, 16, LV_SYMBOL_LEFT,  DOOM_BTN_LEFT,  false);   /* 左转 */
    make_btn(s_scr, 110, 272, 16, LV_SYMBOL_RIGHT, DOOM_BTN_RIGHT, false);   /* 右转 */
    make_btn(s_scr, 296, 250, 20, "A",  DOOM_BTN_A,      false);   /* 开火 */
    make_btn(s_scr, 258, 296, 18, "B",  DOOM_BTN_B,      false);   /* 使用/开门 */
    make_btn(s_scr, 200, 300, 15, "ST", DOOM_BTN_START,  false);   /* 菜单 */
    make_btn(s_scr, 152, 300, 13, "SE", DOOM_BTN_SELECT, false);   /* 选枪 */
    make_btn(s_scr, 176, 244, 12, "L",  DOOM_BTN_L,      false);   /* 上一把枪 */
    make_btn(s_scr, 226, 244, 12, "R",  DOOM_BTN_R,      false);   /* 下一把枪 */

    lv_scr_load(s_scr);
    sdgoods_app_shell_bind(s_scr);                    /* 装顶部下滑控制中心捕获带 */
    /* ⚠ 捕获带(0~50px, move_foreground) 会盖住上排 ST/L/R，使其收不到触摸。
     *   bind 之后把游戏按键整体提到最上层，压过捕获带 → 按键恢复可点，
     *   控制中心仍可从顶部空白处下滑唤出。 */
    for (int i = 0; i < s_btn_n; i++) lv_obj_move_foreground(s_btns[i]);
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
#if DOOM_INPUT_SELFTEST
    /* 自测：开机后自动走一遗“菜单 → New Game → 选难度”序列（START 开菜单，
     * 两次 B(=KEYD_A 菜单确认) 选中新游戏与难度），验证能否进入关卡渲染。
     * 序列靠时间窗产生按下/抬起边沿，交给引擎边沿检测。 */
    {
        static int64_t t0; if (!t0) t0 = esp_timer_get_time();
        double e = (esp_timer_get_time() - t0) / 1000000.0;
        uint32_t want = 0;
        if (e >= 4.0 && e < 4.3) want |= DOOM_BTN_START;   /* 开菜单 */
        if (e >= 5.5 && e < 5.8) want |= DOOM_BTN_B;        /* New Game → 选章节 */
        if (e >= 7.0 && e < 7.3) want |= DOOM_BTN_B;        /* 章节 → 选难度 */
        if (e >= 8.5 && e < 8.8) want |= DOOM_BTN_B;        /* 难度 → 开始关卡 */
        uint32_t hold = g_doom_host.btn_mask & ~(DOOM_BTN_START | DOOM_BTN_B);
        g_doom_host.btn_mask = hold | want;
    }
#endif
}
