/* esp32_wad.c - 从 appdata(0x210000) 分区头部 mmap WAD。
 * 本工程是单应用固件，不把 appdata 挂 FAT，头部直接是裸 WAD 数据。
 * 烧录：esptool.py write_flash 0x210000 DOOM1_PROCESSED.WAD */
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "spi_flash_mmap.h"
#include "doom_iwad.h"

#define WAD_MAP_BYTES 0x440000u   /* 4.25MB ≥ DOOM1_PROCESSED.WAD 3,904,360B（已转标准 seg/nodes 格式、含 STGANUM/M_GAMMA，预留余量）*/

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
        ESP_LOGE(TAG, "bad header '%.4s' — WAD 未烧录？write_flash 0x210000", (const char*)mapped);
        return -1;
    }
    doom_iwad = (const unsigned char *)mapped;
    doom_iwad_len = (unsigned int)map_len;   /* 引擎只信 WAD 头里的目录偏移，用 map 长度安全 */
    ESP_LOGI(TAG, "WAD mmap ok: addr=0x%lx map=%uKB", (unsigned long)part->address, (unsigned)(map_len/1024));
    return 0;
}
