/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * 「谷仓共创计划」与「谷仓 SDGOODS 开放平台」项目、谷仓次元屏（谷仓电子徽章）设备，
 *   以及本基础代码的著作权与相关权利，均归深圳希德创新网络有限公司所有。
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于应用层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

/*
 * main.c —— 应用层装配点（Application entry）
 *
 * 这个文件属于**应用层**：它决定「用哪些应用、首屏是什么、怎么接线」。
 * 平台能力（屏 / 触摸 / 电源键 / 音频 / 应用框架 / 字体）
 * 全部来自 components/bsp，用一行 `#include "bsp.h"` 拿到。
 *
 * 本工程是独立单应用游戏固件：boot-direct 直接进 DOOM（ui_doom_start），
 * 不再有「多应用注册表 / 启动器」那一层——轮询与电源键钩子在 app_main 里直接接线。
 */

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp.h"              /* 硬件抽象层：板级支持包总入口 */
#include "ui_doom.h"           /* 应用层：boot-direct 直启首屏（DOOM） */
#include "build_version.h"    /* 自动生成：版本号 + SDGOODS 品牌信息 */
#include "lvgl.h"              /* lv_refr_now：创建首屏后立刻刷新一帧，避免背光点亮时的白屏闪烁 */

static const char *TAG = "SDGOODS";

/* 电源键短按「一级返回」钩子：单应用全屏 DOOM 无子页可返回——始终返回 false，
   交平台默认导航（控制中心浮层若开着由其自身消费；否则熄屏 + 浅睡，引擎继续在 core0 跑）。 */
static bool doom_power_short_handler(void)
{
    return false;
}

void app_main(void)
{
    /* 品牌与版本横幅 —— 固件自带的「身份证」。
       看串口日志或 dump 固件都能看出源头与授权状态。
       文案统一从 build_version.h 取，不要在别处另写一份字面量。 */
    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, " %s：%s", SDGOODS_PROGRAM, SDGOODS_PLATFORM);
    ESP_LOGI(TAG, " %s（%s）", SDGOODS_PRODUCT, SDGOODS_BRAND);
    ESP_LOGI(TAG, " %s", SDGOODS_VENDOR);
    ESP_LOGI(TAG, " 固件版本 %s", BUILD_VERSION_STR);
    ESP_LOGI(TAG, " %s", SDGOODS_LICENSE_TAG);
    ESP_LOGI(TAG, " %s", SDGOODS_HOMEPAGE);
    ESP_LOGI(TAG, " 联系 %s", SDGOODS_CONTACT_EMAIL);
    ESP_LOGI(TAG, "========================================================");

    /* 静音噪音大的子系统日志（只留 warn 以上），让串口日志聚焦在自己的代码上。
       调试某个子系统时，把它改成 ESP_LOG_INFO 或 DEBUG。 */
    esp_log_level_set("gpio", ESP_LOG_WARN);
    esp_log_level_set("i2c", ESP_LOG_ERROR);

    /* 电池供电自锁：拉高保持上电，关机时由平台层 sdgoods_power_off.c 释放 */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_BAT_CONTROL_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_BAT_CONTROL_GPIO, BOARD_BAT_CONTROL_LATCH_LEVEL);

    /* 屏控制器上电初始化（此时背光仍关着，见下） */
    sdgoods_lcd_init_panel();
    sdgoods_lcd_apply_vendor_madctl();
    /* 背光刻意延迟到开机动画首帧画好之后再点亮：
       LCD 复位后的默认帧是白屏，提前点亮会看到「先白屏再跳主页」的闪烁。 */

    sdgoods_key_init();

    /* 界面语言：出厂默认英文，用户可在 DEMO 页切换（存 NVS，重启保留）。
       必须在任何界面创建之前调用 —— 首屏是在 sdgoods_ui_home_create_show() 里创建的。 */
    sdg_i18n_init();

    /* LVGL 移植 + 触摸 + 硬件信息 */
    sdgoods_lvgl_init();
    ESP_ERROR_CHECK(sdgoods_touch_init());
    ESP_ERROR_CHECK(sdgoods_hw_info_init());

    /* 板载服务（Audio）排在 LVGL/触摸之后 —— 内存原因，勿提前。
     * （WiFi/BLE 已随独立游戏需求移除，不再占用内部 DMA RAM。） */
    ESP_ERROR_CHECK(sdgoods_audio_init());

    /* 中文 fallback 已在编译期写入 si_yuan 图标字体的 .fallback 字段
       （见 components/sdgoods_board/fonts/si_yuan_black_icon_*.c），
       不可在运行时写 const 字体结构体，否则会触发 ESP32 flash Cache 错误。 */

    /* ★ 接线：单应用直启——把 DOOM 的逐帧 poll 与电源键短按钩子直接注册给平台层。
       （原多应用注册表已随架构重组移除；nav 导航钩子对单应用无意义，不再注册。） */
    sdgoods_apps_set_poll(ui_doom_poll);
    sdgoods_set_power_short_handler(doom_power_short_handler);

    /* 直接进入首屏：本工程(app0)不再单独播开机动画 —— 设备开机动画由 Launcher 在上电时
       负责，从 Launcher 启动 app0 时若再播一遍会重复。先建好首屏并刷新一帧，再点亮背光，
       避免 LCD 复位后的白屏闪烁。 */
    ui_doom_start();
    lv_refr_now(NULL);
    sdgoods_lcd_set_backlight(60);   /* 出厂默认亮度 60%（与 sdgoods_cc.c 的 s_bri_user 一致） */

    sdgoods_app_shell_init();   /* 应用标准框架（共享音量等），须在 sdgoods_audio_init 之后 */

    /* ⚠️ 本工程不初始化 app_data_store：appdata 分区(0x1000000)头部 4.25MB
       被 DOOM1_PROCESSED.WAD 裸数据占用，由 components/doom/esp32_wad.c 直接 mmap，
       不可再挂 FAT（挂上会把 WAD 当文件系统目录区读坏）。单应用纯游戏也无持久数据需求。 */

    sdgoods_lvgl_loop();      /* 永不返回：sdgoods_power_key_poll + lv_timer_handler + apps_poll */
}
