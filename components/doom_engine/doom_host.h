/* doom_host.h - GBADoom 引擎与 SDGOODS LVGL 外壳之间的共享状态。
 * btn_mask 由 LVGL 线程（按键回调）写、引擎任务读；frame_ready 由引擎置位、
 * LVGL poll 消费。全部 volatile，32 位位掩码读写在 S3 上天然原子。 */
#ifndef DOOM_HOST_H
#define DOOM_HOST_H

#include <stdint.h>
#include <stdbool.h>

#define DOOM_BTN_UP      (1u << 0)   /* 十字键↑ 前进（KEYD_UP，锁存）      */
#define DOOM_BTN_DOWN    (1u << 1)   /* 十字键↓ 后退（KEYD_DOWN，锁存）    */
#define DOOM_BTN_LEFT    (1u << 2)   /* 十字键← 左转（KEYD_LEFT，锁存）    */
#define DOOM_BTN_RIGHT   (1u << 3)   /* 十字键→ 右转（KEYD_RIGHT，锁存）   */
#define DOOM_BTN_A       (1u << 4)   /* A 开火 → KEYD_B（passport 实证）   */
#define DOOM_BTN_B       (1u << 5)   /* B 使用 → KEYD_A（开门/确认）       */
#define DOOM_BTN_L       (1u << 6)   /* L → KEYD_L（引擎内已重映射为“上一把枪”）*/
#define DOOM_BTN_R       (1u << 7)   /* R → KEYD_R（引擎内已重映射为“下一把枪”）*/
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

#endif /* DOOM_HOST_H */
