// The simulator: the FAT partition /data is a folder of the PC (sim_fs.c); the mount always works.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t wl_handle_t;
#define WL_INVALID_HANDLE -1
typedef struct {
    bool format_if_mount_failed;
    int max_files;
    size_t allocation_unit_size;
    bool disk_status_check_enable;
    bool use_one_fat;
} esp_vfs_fat_mount_config_t;
typedef esp_vfs_fat_mount_config_t esp_vfs_fat_sdmmc_mount_config_t;

esp_err_t esp_vfs_fat_spiflash_mount_rw_wl(const char *base, const char *label, const esp_vfs_fat_mount_config_t *cfg,
                                           wl_handle_t *wl);
esp_err_t esp_vfs_fat_spiflash_unmount_rw_wl(const char *base, wl_handle_t wl);
esp_err_t esp_vfs_fat_info(const char *base, uint64_t *total, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif
