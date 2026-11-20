// CUBBY: the settings of the 3D core (included by app_cubby.c). The code is the shared engine libs/tf3d/tf3d.inc
// (the vector math, the camera and the rasterizer).

// THE RENDER WIDTH: the 3D is drawn at half the width of the frame (the pixels of the 3.49 and the CoreS3 are only
// 0.13 mm wide): each pixel goes out as KX pixels (half of the work; the soft edges hide the steps). OW = the frame,
// VW = the 3D (the camera has fx = f / KX). The HUD and the text use the full frame.
#define KX 2
#if BOARD_UI_TABLET
#define CB_WIDE 1  // (the Tab5, 640 x 360: the wide layout of the 3.49, a taller view)
#define CB_DY   188
#define OW      640
#define VH      360
#elif BOARD_BAR_SCREEN
#define CB_WIDE 1
#define CB_DY   0
#define OW      640
#define VH      172
#elif BOARD_ROUND
// THE ROUND (app_t.round = APP_ROUND_HI): the frame of the panel (BOARD_ROUND_HI: the AMOLED-1.75C 466 x
// 466; the Watcher its native 412 x 412). The 3D is drawn at half the width AND half the height (KY 2: each pixel goes
// out as 2 x 2 px; about the work of the 3.49), the tiles out of the circle are not drawn. OH = the rows of the frame.
#define CB_WIDE  0
#define CB_DY    0
#define CB_ROUND 1
#define KY       2
#define OW       BOARD_ROUND_HI
#define VH       (OW / KY)
#else
#define CB_WIDE 0
#define CB_DY   0
#define OW      320
#define VH      240
#endif
#ifndef KY
#define CB_ROUND 0
#define KY       1
#endif
#define OH  (VH * KY)         // the rows of the frame (the HUD and the text)
#define KXF ((float)KX / KY)  // a radius in the px of the rows, in the px of the columns: r / KXF
#define VW  (OW / KX)
#if CB_WIDE
#define TLW 64  // a tile: 64 x 16 px (wide: few rows of a triangle split between two tiles)
#elif CB_ROUND && !defined(CB_TLW)
#define TLW 64  // (the round: 64 x 16 px, as the 3.49: fewer setups of the triangles at the borders of the tiles)
#elif CB_ROUND
#define TLW CB_TLW
#else
#define TLW 32  // (the 320 x 240 frame: 32 x 16 px, so the tiles of the two cores fit the internal RAM of the CoreS3)
#endif
#if CB_DY
#define TLH 24  // (the Tab5: 360 rows = 15 tile rows; a triangle keeps its tile rows in 4 bits)
#elif CB_WIDE
#define TLH                                                                                                            \
    32      // (the 3.49, round 3: 64 x 32 px; the big island has many triangles, each one costs a setup in each
            // tile that it touches: 6 tile rows in place of 11)
#else
#define TLH 16
#endif
#define TF3D_ROUND CB_ROUND
#if !(defined(ESP_PLATFORM) && defined(__XTENSA__)) && defined(CB_HOST_CY)
// (the PC bench of the stages: the time stamp counter; the normal PC builds keep 0)
#include <x86intrin.h>
#define TF3D_HOST_CY 1
static inline uint32_t cy_now(void)
{
    return (uint32_t)__rdtsc();
}
#endif
#include "tf3d/tf3d.inc"
