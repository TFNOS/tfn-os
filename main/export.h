#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/export.h.
#include "tfn/export.h"

#include <stdbool.h>
#include <stdint.h>

// Files for download (web page) in /data/exports, or /sdcard/exports (SAVE TO below).

// SAVE TO: the place of the user files of STUDIO, PADS and DRUMS (the songs, the exports). The store key sv_place:
// 0 = CARD (the default), 1 = BOARD; the card only when it is in. sv_asked = 1 after the first answer.
bool export_on_card(void);

// Mono 16-bit WAV.
bool export_wav(const char *path, const int16_t *samples, uint32_t n, uint32_t rate);

bool midi_save(midi_t *m, const char *path, int bpm);
