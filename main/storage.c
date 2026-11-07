// FAT file system with wear levelling in the "storage" partition.

#include <stdio.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "storage.h"
#include "device_guard.h"

static const char *TAG = "storage";
static bool s_ok;
static wl_handle_t s_wl = WL_INVALID_HANDLE;

bool storage_init(void)
{
    esp_vfs_fat_mount_config_t cfg = {
            .max_files = 6,
            .format_if_mount_failed = !device_guard_enabled(),
            .allocation_unit_size = 4096,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(STORAGE_ROOT, "storage", &cfg, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return false;
    }
    mkdir(STORAGE_EXPORTS, 0777);
    mkdir(STORAGE_SAMPLES, 0777);
    mkdir(STORAGE_SONGS, 0777);
    mkdir(STORAGE_MIDI, 0777);
    s_ok = true;
    ESP_LOGI(TAG, "mounted, %u KB free", (unsigned)(storage_free() / 1024));
    return true;
}

bool storage_ok(void)
{
    return s_ok;
}

// (main/layout.c: the change of the layout unmounts /data after it packs the files)
void storage_deinit(void)
{
    if (!s_ok) {
        return;
    }
    esp_vfs_fat_spiflash_unmount_rw_wl(STORAGE_ROOT, s_wl);
    s_wl = WL_INVALID_HANDLE;
    s_ok = false;
}

uint64_t storage_used(void)
{
    uint64_t total = 0, free_b = 0;
    if (!s_ok || esp_vfs_fat_info(STORAGE_ROOT, &total, &free_b) != ESP_OK) {
        return 0;
    }
    return total - free_b;
}

uint64_t storage_free(void)
{
    uint64_t total = 0, free_b = 0;
    if (!s_ok || esp_vfs_fat_info(STORAGE_ROOT, &total, &free_b) != ESP_OK) {
        return 0;
    }
    return free_b;
}

// (never a cut file. The data goes to "<path>.tmp" first; when all of it is written
// and closed, the old file goes and the new one takes its name. A full /data keeps the old file; a power cut between the
// two steps leaves only "<path>.tmp", which storage_open_read() then opens.)
bool storage_write(const char *path, const void *data, size_t len)
{
    if (!s_ok) {
        return false;
    }
    char tmp[160];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
        return false;
    }
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return false;
    }
    const size_t n = fwrite(data, 1, len, f);
    const bool ok = fclose(f) == 0 && n == len;
    if (!ok) {
        remove(tmp);
        return false;
    }
    remove(path);
    return rename(tmp, path) == 0;
}

FILE *storage_open_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) {
        return f;
    }
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    return fopen(tmp, "rb");
}

bool storage_read(const char *path, void *data, size_t max, size_t *len)
{
    *len = 0;
    if (!s_ok) {
        return false;
    }
    FILE *f = storage_open_read(path);
    if (!f) {
        return false;
    }
    *len = fread(data, 1, max, f);
    fclose(f);
    return true;
}
