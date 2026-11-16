// INK KARTS: the settings of the 3D core (included by app_kart.c). The code is the shared engine
// libs/tf3d/tf3d.inc (the vector math, the camera and the rasterizer); this game adds the round frame of its own
// (a size at run time), a nearer far cut and the glossy paint of tri_add.

// THE RENDER WIDTH: the 3D is drawn at half the width of the frame (the pixels of the 3.49 and the CoreS3 are only
// 0.13 mm wide): each pixel goes out as KX pixels (half of the work; the soft edges hide the steps). OW = the frame,
// VW = the 3D (the camera has fx = f / KX). The HUD and the text use the full frame.
// THE ROUND FRAME (the AMOLED-1.75C and the Watcher, 412 x 412, only the circle shows): the 3D is drawn at half the
// width AND half the height (KY = 2: each px goes out as 2 x 2), and the tiles out of the circle are not drawn: the
// frame stays light (the 1.75C needs about 30 ms to send a round frame). OH = the height of the frame.
#if BOARD_UI_TABLET
#define KT_LAY 1  // the layouts: 1 wide (the 3.49, the Tab5), 0 the 320 x 240 frame, 2 round
#define OW     640
#define OH     360
#define KX     2
#define KY     1
#elif BOARD_BAR_SCREEN
#define KT_LAY 1
#define OW     640
#define OH     172
#define KX     2
#ifdef KT_KY349
#define KY KT_KY349
#else
#define KY 2  // (THE SPEED: the 3D of the 3.49 at half the height too, with the soft vertical blend)
#endif
#elif BOARD_ROUND
// (the round frame: its size at run time; s_ow = gfx_w at the open: 412 on the Watcher, 466 on the AMOLED-1.75C with
// APP_ROUND_HI. The arrays have the size of the biggest frame, RMAX)
#define KT_LAY 2
#define RMAX   480
static int s_ow = 412;
// (THE SPEED OF THE 1.75C: a pixel of the 3D is a block of s_k x s_k px of the frame: 3 on the 466 frame, 2 on the
// 412 frame; the frame of 466 costs the 1.75C about 31 ms of core 1 to send, so the 3D must be light there)
static int s_k = 2;
#define OW     s_ow
#define OH     s_ow
#define KX     s_k
#define KY     s_k
#define KMAX   3
#define VW_MAX (RMAX / 2)
#define VH_MAX (RMAX / 2)
#else
#define KT_LAY 0
#define OW     320
#define OH     240
#define KX     2
#define KY     1
#endif
#define VW (OW / KX)
#define VH (OH / KY)
#ifndef VW_MAX
#define VW_MAX VW
#define VH_MAX VH
#endif
#if KT_LAY == 1
#define TLW 64  // a tile: 64 x 16 px (wide: few rows of a triangle split between two tiles)
#else
#define TLW 32  // (the 320 x 240 frame: 32 x 16 px, so the tiles of the two cores fit the internal RAM of the CoreS3)
#endif
#if VH_MAX > 256
#define TLH 24  // (the Tab5: 360 rows = 15 tile rows; a triangle keeps its tile rows in 4 bits)
#else
#define TLH 16
#endif
#define TCOLS_MAX ((VW_MAX + TLW - 1) / TLW)
#define TROWS_MAX ((VH_MAX + TLH - 1) / TLH)
#define NTILE_MAX (TCOLS_MAX * TROWS_MAX)
#define KT_FAR    125.0f
#define TF3D_FAR  KT_FAR  // (the far cut: the haze is full there)
// (KXF: a column of the tile in rows of the tile; the radius r is in rows)
#define KXF ((float)KX / KY)
#if !(defined(ESP_PLATFORM) && defined(__XTENSA__)) && defined(__x86_64__)
// (the PC: the time stamp counter / 16, a relative measure of the stages; not the cycles of the board)
#define TF3D_HOST_CY 1
static inline uint32_t cy_now(void)
{
    return (uint32_t)(__builtin_ia32_rdtsc() >> 4);
}
#endif
// the mix of tri_add: a glossy paint: the pixel goes toward the colour by 70 % of k, and a little light is added; two
// inks over each other keep a colour, they do not go white
#define TF3D_ADD_MIX(p, cr, cg, cb, k)                                                                                 \
    int pr = (int)((p) >> 11), pg = (int)(((p) >> 5) & 63), pb = (int)((p) & 31), m = ((k) * 180) >> 8;                \
    int r = pr + (((((cr) >> 3) - pr) * m) >> 8) + (((cr) * (k)) >> 13);                                               \
    int g = pg + (((((cg) >> 2) - pg) * m) >> 8) + (((cg) * (k)) >> 12);                                               \
    int b = pb + (((((cb) >> 3) - pb) * m) >> 8) + (((cb) * (k)) >> 13)
#include "tf3d/tf3d.inc"
