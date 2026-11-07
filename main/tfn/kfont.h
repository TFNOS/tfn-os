#pragma once

// The Rubik fonts of the quests (KEEPER, ROOM, FRAME, STAND-IN, SIDE B): English and Hebrew, 4-bit atlases.
// One copy in the flash (kfont.c): the quests share it and do not keep their own copy of about 69 KB.

#include <stdint.h>

typedef struct {
    uint16_t cp;
    uint8_t adv4;
    int8_t ox, oy;
    uint8_t w, h;
    uint32_t off;
} kglyph_t;
typedef struct {
    uint8_t asc, line;
    uint16_t n;
    const kglyph_t *g;
    const uint8_t *a4;
} kfont_t;

extern const kfont_t KF_LABEL, KF_TEXT, KF_NAME, KF_BIG, KF_TITLE;
