#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define STORAGE_MIDI "/data/midi"  // MIDI files from the web page, for TILES

bool storage_ok(void);
// Free space in bytes (0 if not mounted).
uint64_t storage_free(void);

// Whole-file helpers. Return false on an error.
bool storage_write(const char *path, const void *data, size_t len);  // (no cut file: "<path>.tmp" first)
// Reads up to max bytes; *len gets the size read.
bool storage_read(const char *path, void *data, size_t max, size_t *len);
