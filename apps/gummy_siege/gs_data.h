#pragma once
// GUMMY SIEGE: the data of the game in the app file (gs_data.c, made by tools/mk_data.py). The boards with no SD card
// (the AMOLED, the CoreS3, the Watcher) can then play it too: no data pack.
#include <stdint.h>
#define GS_SFX_N 24                     // the effects, in the order of FX_FILE (gs_fx.inc)
extern const uint8_t GS_LEVELS_TXT[];   // data/levels.txt (a 0 at the end)
extern const uint32_t GS_LEVELS_LEN;
extern const uint8_t GS_ART_BIN[];      // art.bin (tools/art/bake.py)
extern const uint32_t GS_ART_LEN;
extern const uint8_t *const GS_SFX_DATA[GS_SFX_N];  // the MP3 files
extern const uint32_t GS_SFX_LEN[GS_SFX_N];
