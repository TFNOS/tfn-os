// DIRT KINGS: the data of the app (made by tools/mk_data.py: do not edit).
#pragma once
#include <stdint.h>

// an arena: the picture (JPEG, DK_AW x DK_AH, DK_PPU px a unit; the track unit (0, 0) is at the picture point
// (DK_OX * DK_PPU, DK_OY * DK_PPU) and y goes up), the colour of the ground out of the picture (as in the frame), the
// track: n points (x, y in 1/64 unit), the length of a lap (units), the jumps and the mud (0..1 along the line; -1 =
// none), the start line, day or night, the name
typedef struct {
    const uint8_t *jpg;
    uint32_t jpg_n;
    uint16_t ground;
    uint16_t n;
    const int16_t *pts;
    float len, jump[2], mud[2], start;
    uint8_t night, cross;
    const char *name;
} dk_arena_t;
#define DK_AW 784
#define DK_AH 756
#define DK_PPU 14.0f
#define DK_OX 4.0f
#define DK_OY 48.0f
#define DK_ARENA_W 48.0f
#define DK_ARENA_H 36.0f
#define DK_TW 4.40f
#define DK_BERM 1.10f
#define DK_NARENA 8
extern const dk_arena_t DK_ARENAS[DK_NARENA];

// a sprite: w x h, the centre (ax, ay) in it, its runs at DK_SPR + off (h row offsets of uint16 from off, then the
// runs: a byte (type << 6 | n - 1); type 0 skip n, 1 n palette indexes, 2 n x (index, alpha)); the palette of the
// sprite: DK_PAL[pal] (256 frame colours)
typedef struct { int16_t w, h, ax, ay; uint32_t off; uint8_t pal; } dk_sprite_t;
#define DK_SPPU 17.5f            // the px of a sprite for one unit
#define DK_NHEAD 64              // the headings of a truck (heading k: k / DK_NHEAD of a turn, from +x to +y)
#define DK_NCOL 4                // red, blue, yellow, white
#define DK_SP_TRUCK(col, k) ((col) * DK_NHEAD + (k))
#define DK_SP_MONEY (DK_NCOL * DK_NHEAD)
#define DK_SP_NITRO (DK_SP_MONEY + 1)
#define DK_NSPRITE (DK_SP_NITRO + 1)
extern const dk_sprite_t DK_SPRITES[DK_NSPRITE];
extern const uint8_t DK_SPR[];
extern const uint16_t DK_PAL[DK_NCOL + 1][256];
