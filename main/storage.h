#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/storage.h.
#include "tfn/storage.h"

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

// FAT file system in the "storage" flash partition, mounted at /data.
#define STORAGE_ROOT    "/data"
#define STORAGE_EXPORTS "/data/exports"
#define STORAGE_SAMPLES "/data/samples"
#define STORAGE_SONGS   "/data/songs"

bool storage_init(void);
uint64_t storage_used(void);  // the bytes in use (0 if not mounted)
void storage_deinit(void);    // unmount /data (main/layout.c)

FILE *storage_open_read(const char *path);  // fopen "rb", or "<path>.tmp" when the file is missing
