// The DMA buffer of the SD card driver (one for the life of the board).
//
// The SDMMC and the SPI DMA of the ESP32-S3 cannot reach PSRAM. For each read or write to a buffer in PSRAM (the FATFS
// buffers are in PSRAM, and so are most buffers of the apps), the driver (sdmmc_read_sectors) uses a DMA-capable buffer in
// internal RAM. With no buffer of its own, it allocates one for each read and frees it after. The WiFi takes the internal
// DMA RAM in bursts: when a burst has all of it, the allocation fails, the read fails ("allocate_dma_buf: not enough mem")
// and the app gets "no file" or a short fread (the music and the pictures of the SD games).
// sd_dma_set() gives the driver one buffer before the mount: 1 KB (2 sectors in one command), or 512 B when the RAM is
// short. FATFS lets one task at a time use a volume (FF_FS_REENTRANT), so one buffer is enough.
// sd_dma_result(false) frees it after a mount that failed (no card: no RAM used), unless a card mounted before with it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sd_protocol_types.h"

static void *s_sd_dma;
static bool s_sd_dma_used;

static inline void sd_dma_set(sdmmc_host_t *host)
{
    size_t n = 1024;
    if (!s_sd_dma) {
        s_sd_dma = heap_caps_malloc(n, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (!s_sd_dma) {
        s_sd_dma = heap_caps_malloc(n = 512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (!s_sd_dma) {
        return;  // (the driver allocates for each read, as before)
    }
    n = heap_caps_get_allocated_size(s_sd_dma);
    host->dma_aligned_buffer = s_sd_dma;
    host->unaligned_multi_block_rw_max_chunk_size = n / 512;
}

static inline void sd_dma_result(bool mounted)
{
    if (mounted && !s_sd_dma_used && s_sd_dma) {
        ESP_LOGI("sd", "the SD driver keeps a DMA buffer of %u B", (unsigned)heap_caps_get_allocated_size(s_sd_dma));
    }
    if (mounted) {
        s_sd_dma_used = true;
    } else if (!s_sd_dma_used && s_sd_dma) {
        heap_caps_free(s_sd_dma);
        s_sd_dma = NULL;
    }
}
