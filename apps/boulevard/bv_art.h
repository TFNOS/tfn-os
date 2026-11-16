// BOULEVARD: the Blender art of the view (made by tools/mk_art.py: do not edit).
#pragma once
#include <stdint.h>

// the skyline: 980 x 110 (it loops), a JPEG
extern const uint8_t BA_SKY_JPG[];
extern const uint32_t BA_SKY_N;
// a picture: w x h, the anchor (the ground point) at ax, ay; its rows at BA_SPR + off (h row offsets, then the runs:
// a byte type << 6 | n - 1; type 0 skip, 1 solid: n colours, 2 alpha: n x (colour, alpha))
typedef struct { int16_t w, h, ax, ay; uint32_t off; } ba_frame_t;
#define BA_NMIP 7
extern const float BA_MIP[BA_NMIP];
extern const uint8_t BA_SPR[];
extern const ba_frame_t BA_FRAMES[];
// the first picture (size 1) of each sprite; the sizes BA_MIP follow
extern const uint16_t BA_FIRST[];
#define BA_PAWN_0 0
#define BA_PAWN_1 1
#define BA_PAWN_2 2
#define BA_PAWN_3 3
#define BA_PAWN_4 4
#define BA_PAWN_5 5
#define BA_HOUSE 6
#define BA_TOWER 7
#define BA_DIE_1 8
#define BA_DIE_2 9
#define BA_DIE_3 10
#define BA_DIE_4 11
#define BA_DIE_5 12
#define BA_DIE_6 13
#define BA_DIER_1 14
#define BA_DIER_2 15
#define BA_DIER_3 16
#define BA_DIER_4 17
#define BA_DIER_5 18
#define BA_DIER_6 19
