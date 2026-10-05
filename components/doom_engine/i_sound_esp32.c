/* i_sound_esp32.c - GBADoom 音频后端（SDGOODS ESP32-S3 版）
 *
 * 替换 GBA 的 i_audio.c（其逻辑全在 #ifdef GBA 内，ESP32 上是空壳 → 静音）。
 * 引擎 s_sound.c 算好 vol/sep 后调 I_StartSound(sfx_id, cnum, vol, sep)；本文件
 * 用 soundbank（DOOM_SFX.bin，运行时读进 PSRAM）+ 8 通道混音任务，把 8-bit 无符号
 * PCM 按 vol/sep 混成立体声 int16，经板载 sdgoods_audio 的 I2S0 TX 推给功放。
 *
 * 音乐（OPL/IT）本里程碑不做：I_PlaySong 等为空 stub（与排除的 i_audio.c 对齐符号）。
 * 采样率保留音效原生值（11025/22050），混音时定点变步长重采样到 I2S 输出率 16000。 */

/* doomtype.h 必须先于 FreeRTOS（true/false/boolean 与 freertos 头冲突，见 i_system_sdgoods.c）。 */
#include "doomtype.h"

#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "i_sound.h"          /* I_StartSound / I_InitSound / 音乐接口 / MAX_CHANNELS */
#include "sdgoods_audio.h"    /* stream_open / stream_write / stream_close / get_volume */
#include "doom_sfx_index.h"   /* DOOM_SFX_INDEX / DOOM_SFX_TOTAL_BYTES / DOOM_SFX_FLASH_OFFSET */

static const char *TAG = "doom_snd";

#define SFX_OUT_RATE 16000        /* 与板载 s_tx 的 RATE_HZ 一致 */
#define MIX_BLOCK    256          /* 每块帧数（≈16ms @16k）*/

/* ---- soundbank（PSRAM 副本）---- */
static uint8_t *s_bank;           /* DOOM_SFX.bin 全文 */
static bool     s_bank_ok;

/* ---- 通道状态 ---- */
typedef struct {
    const uint8_t *data;          /* 指向 s_bank 中该音效起点 */
    uint32_t       num_samples;
    uint32_t       pos_fp;         /* 16.16 定点读指针 */
    uint32_t       step_fp;        /* 每输出帧的输入步进 = (rate<<16)/SFX_OUT_RATE */
    int            vol;            /* 0..~1024（引擎传值，玩家动作 *=8）*/
    int            sep;            /* 0..255，NORM_SEP=128 居中 */
    bool           active;
} chan_t;
static chan_t s_ch[MAX_CHANNELS];

static volatile bool s_run;
static TaskHandle_t  s_task;

static inline int16_t sat16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

/* 一次性把 soundbank 从 appdata 分区读进 PSRAM。 */
static bool load_bank_from_partition(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "appdata");
    if (!part) { ESP_LOGE(TAG, "appdata partition not found"); return false; }

    s_bank = heap_caps_malloc(DOOM_SFX_TOTAL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_bank) { ESP_LOGE(TAG, "PSRAM alloc %u B failed", DOOM_SFX_TOTAL_BYTES); return false; }

    esp_err_t err = esp_partition_read(part, DOOM_SFX_FLASH_OFFSET, s_bank, DOOM_SFX_TOTAL_BYTES);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read bank @0x%x failed: %s", DOOM_SFX_FLASH_OFFSET, esp_err_to_name(err));
        heap_caps_free(s_bank); s_bank = NULL; return false;
    }
    ESP_LOGI(TAG, "soundbank loaded: %u B from appdata+0x%x (PSRAM @%p)",
             DOOM_SFX_TOTAL_BYTES, DOOM_SFX_FLASH_OFFSET, s_bank);
    return true;
}

/* 混音任务：把 8 通道叠成立体声，按全局音量缩放后推 I2S。 */
static void mix_task(void *arg)
{
    (void)arg;
    static int16_t buf[MIX_BLOCK * 2];

    while (s_run) {
        const int gpct = sdgoods_audio_get_volume();   // 0..100，CC 滑条
        for (int i = 0; i < MIX_BLOCK; i++) {
            int32_t accL = 0, accR = 0;
            for (int c = 0; c < MAX_CHANNELS; c++) {
                chan_t *ch = &s_ch[c];
                if (!ch->active) continue;

                uint32_t idx = ch->pos_fp >> 16;
                if (idx >= ch->num_samples) { ch->active = false; continue; }

                int32_t s8  = (int32_t)ch->data[idx] - 128;      // 有符号 -128..127
                int32_t base = s8 << 8;                          // 铺到 16-bit 幅度
                int32_t v = ch->vol; if (v < 0) v = 0; if (v > 1024) v = 1024;
                int32_t sg = (base * v) / 1024;                  // 音量
                int sep = ch->sep; if (sep < 0) sep = 0; if (sep > 255) sep = 255;
                accL += (sg * (255 - sep)) / 128;                // 相位：128≈居中
                accR += (sg * sep) / 128;

                ch->pos_fp += ch->step_fp;                       // 定点重采样推进
            }

            int32_t L = (accL * gpct) / 100;                     // 全局音量
            int32_t R = (accR * gpct) / 100;
            buf[i * 2]     = sat16(L);
            buf[i * 2 + 1] = sat16(R);
        }
        if (sdgoods_audio_stream_write(buf, MIX_BLOCK) != ESP_OK) {
            vTaskDelay(1);   // 通道异常时避让，不死转
        }
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

void I_InitSound(void)
{
    memset(s_ch, 0, sizeof(s_ch));
    s_bank_ok = load_bank_from_partition();
    if (!s_bank_ok) {
        ESP_LOGW(TAG, "soundbank 缺失，DOOM 将以静音运行（请先 write_flash 0x1480000 DOOM_SFX.bin）");
        // 不 return：仍提供接口，只是不出声，避免引擎崩
    }
    if (sdgoods_audio_stream_open() != ESP_OK) {
        ESP_LOGE(TAG, "audio stream open failed");
        return;
    }
    s_run = true;
    if (xTaskCreate(mix_task, "doom_mix", 3072, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "mix task create failed");
        s_run = false;
        sdgoods_audio_stream_close();
        return;
    }
    ESP_LOGI(TAG, "I_InitSound: DOOM SFX ready (out=%dHz)", SFX_OUT_RATE);
}

int I_StartSound(int id, int channel, int vol, int sep)
{
    if (channel < 0 || channel >= MAX_CHANNELS) return -1;
    if (!s_bank_ok || id < 1 || id >= DOOM_SFX_COUNT) return channel;

    const doom_sfx_entry_t *e = &DOOM_SFX_INDEX[id];
    if (e->num_samples == 0) return channel;   // 该音效无数据（none/chgun 等）

    chan_t *ch = &s_ch[channel];
    ch->data        = s_bank + e->data_offset;
    ch->num_samples = e->num_samples;
    ch->pos_fp      = 0;
    ch->step_fp     = (uint32_t)(((uint64_t)e->rate << 16) / SFX_OUT_RATE);
    ch->vol         = vol;
    ch->sep         = sep;
    ch->active      = true;
    return channel;
}

/* ---- 音乐：本里程碑不做，空 stub（与被排除的 i_audio.c 提供同名符号）---- */
void I_PlaySong(int handle, int looping)  { (void)handle; (void)looping; }
void I_PauseSong(int handle)              { (void)handle; }
void I_ResumeSong(int handle)             { (void)handle; }
void I_StopSong(int handle)               { (void)handle; }
void I_SetMusicVolume(int volume)         { (void)volume; }
