// The microSD card: the parts that are the same on all boards with a card slot (board_sd.h). The board files have
// board_sd_card_mount() with their own bus and pins. The AMOLED-1.75C has no card slot (board_am175.c).
#include "sdkconfig.h"
#if !CONFIG_TFN_TARGET_AMOLED175
#include "board_sd.h"
#include <stdint.h>
#include "board.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_dma.h"
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
#include "soc/sdmmc_struct.h"  // (the data read timeout of the SDMMC host: board_sd_data_timeout)
#endif

static const char *TAG = "board";

static sdmmc_card_t *s_sd;
static SemaphoreHandle_t s_sd_mx;

void board_sd_init(void)
{
    s_sd_mx = xSemaphoreCreateMutex();
}

__attribute__((weak)) bool board_sd_card_in(void)
{
    return true;
}

__attribute__((weak)) void board_sd_card_released(void)
{
}

// The data read timeout of the SDMMC host. The driver sets 100 ms when it sets the clock (at the mount). A card that
// is busy after many writes (an app install: the files, then the rename of the folder) can need more time for one
// read: the host then stops the read (ESP_ERR_TIMEOUT, "sdmmc_read_sectors: error 0x107"), the data comes after that
// ("handle_idle_state_events unhandled: 00000008"), and the file system gives a fault for a file that is there
// (errno 2). The register holds 24 bits of card clock cycles: 500 ms at 20 MHz is 10 M cycles. The software timeout
// of a read command (SDMMC_DEFAULT_CMD_TIMEOUT_MS, 1000 ms) stays longer than it.
#define SD_DATA_TIMEOUT_MS 500
void board_sd_data_timeout(const sdmmc_card_t *card)
{
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    if (!card || card->real_freq_khz <= 0) {
        return;
    }
    uint64_t cycles = (uint64_t)SD_DATA_TIMEOUT_MS * (uint64_t)card->real_freq_khz;
    if (cycles > 0xFFFFFF) {
        cycles = 0xFFFFFF;
    }
#if CONFIG_IDF_TARGET_ESP32S3
    SDMMC.tmout.data = (uint32_t)cycles;
#else
    SDMMC.tmout.data_timeout = (uint32_t)cycles;
#endif
    ESP_LOGI(TAG, "SD card: data read timeout %u ms", (unsigned)(cycles / (uint64_t)card->real_freq_khz));
#else
    (void)card;
#endif
}

esp_err_t board_sd_mount_sdmmc_1bit(int clk, int cmd, int d0, const esp_vfs_fat_mount_config_t *mc, sdmmc_card_t **card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 20 MHz
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = clk;
    slot.cmd = cmd;
    slot.d0 = d0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    sd_dma_set(&host);
    esp_err_t err = esp_vfs_fat_sdmmc_mount(BOARD_SD_ROOT, &host, &slot, mc, card);
    sd_dma_result(err == ESP_OK);
    if (err == ESP_OK) {
        board_sd_data_timeout(*card);  // (after the mount: the clock of the driver does not change after it)
    }
    return err;
}

// fmt: a card that does not mount gets a new file system (board_sd_format)
static bool sd_mount(bool fmt)
{
    if (!s_sd_mx) {
        return false;
    }
    xSemaphoreTake(s_sd_mx, portMAX_DELAY);
    if (!s_sd && board_sd_card_in()) {
        esp_vfs_fat_mount_config_t mc = {
                .format_if_mount_failed = fmt,
                .max_files = 4,
                .allocation_unit_size = 16 * 1024,
        };
        esp_err_t err = board_sd_card_mount(&mc, &s_sd);
        if (err != ESP_OK) {
            s_sd = NULL;
            board_sd_card_released();
            ESP_LOGI(TAG, "no SD card (%s)", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "SD card: %s, %u MB", s_sd->cid.name,
                     (unsigned)((uint64_t)s_sd->csd.capacity * s_sd->csd.sector_size / (1024 * 1024)));
        }
    }
    bool ok = s_sd != NULL;
    xSemaphoreGive(s_sd_mx);
    return ok;
}

bool board_sd_check(void)
{
    if (!s_sd_mx) {
        return false;
    }
    xSemaphoreTake(s_sd_mx, portMAX_DELAY);
    if (s_sd && sdmmc_get_status(s_sd) != ESP_OK) {
        ESP_LOGI(TAG, "the SD card does not answer: unmounted");
        esp_vfs_fat_sdcard_unmount(BOARD_SD_ROOT, s_sd);
        s_sd = NULL;
        board_sd_card_released();
    }
    bool ok = s_sd != NULL;
    xSemaphoreGive(s_sd_mx);
    return ok;
}

bool board_sd_mounted(void)
{
    return s_sd != NULL;
}

bool board_sd_info(uint32_t *total_kb, uint32_t *free_kb)
{
    uint64_t t = 0, f = 0;
    if (!s_sd || esp_vfs_fat_info(BOARD_SD_ROOT, &t, &f) != ESP_OK) {
        return false;
    }
    *total_kb = (uint32_t)(t / 1024);
    *free_kb = (uint32_t)(f / 1024);
    return true;
}

bool board_sd_mount(void)
{
    return sd_mount(false);
}

// FILES (main/files.h, CARD TOOLS > FORMAT): a new, empty FAT file system on the card, then the card stays mounted.
// A card that does not mount (a new card, another file system): a mount that formats it.
bool board_sd_format(void)
{
    if (!s_sd_mx) {
        return false;
    }
    if (!s_sd && !sd_mount(false)) {
        bool ok = sd_mount(true);
        ESP_LOGI(TAG, "SD card format (it did not mount): %s", ok ? "done" : "FAILED");
        return ok;
    }
    xSemaphoreTake(s_sd_mx, portMAX_DELAY);
    esp_err_t err = s_sd ? esp_vfs_fat_sdcard_format(BOARD_SD_ROOT, s_sd) : ESP_FAIL;
    xSemaphoreGive(s_sd_mx);
    ESP_LOGI(TAG, "SD card format: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

void board_sd_off_at_restart(void)
{
    if (s_sd_mx && xSemaphoreTake(s_sd_mx, pdMS_TO_TICKS(300)) != pdTRUE) {
        return;
    }
    if (s_sd) {
        esp_vfs_fat_sdcard_unmount(BOARD_SD_ROOT, s_sd);
        s_sd = NULL;
    }
    if (s_sd_mx) {
        xSemaphoreGive(s_sd_mx);
    }
}
#endif
