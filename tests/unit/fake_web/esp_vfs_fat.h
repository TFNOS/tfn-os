// A fake of the ESP-IDF FAT file system calls for the host test of main/tfn_web.c (the test sets the free space).
#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t esp_vfs_fat_info(const char *base_path, uint64_t *out_total_bytes, uint64_t *out_free_bytes);
