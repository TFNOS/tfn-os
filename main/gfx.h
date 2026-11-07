#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/gfx.h.
#include "tfn/gfx.h"

#include <stdint.h>
#include "board.h"

// The native mode (only on a board with BOARD_NATIVE_W): the frame is the full panel (for
// example 448 x 368 in landscape) and the board sends it with no scale. board_lcd_flush() gets
// the orientation with ORIENT_NATIVE added. The touch points stay in the LCD_W x LCD_H frame
// (gfx_touch_to_logical() gives native logical points, about 1.4 px steps).
#define ORIENT_NATIVE 0x100
// mode: 0 = off, 1 = the native frame, 2 (APP_ROUND_HI) = the round frame at the panel resolution (BOARD_ROUND_HI;
// the boards that have none: as 1)
void gfx_set_native(int mode);
// the frame now is the round frame at the panel resolution (the AMOLED-1.75C: 466 x 466)
bool gfx_round_hi(void);
#define ORIENT_RHI 0x800
// The HI frame (only on the Tab5; the other boards: no effect, gfx_hi() is false). The panel shows each frame pixel
// 2 x 2; in a HI frame the buffer has the resolution of the panel (2 x gfx_w by 2 x gfx_h, stride gfx_hi_stride()).
// The logical size (gfx_w, gfx_h) and the touch points do not change: the drawing functions of gfx and uikit draw each
// logical pixel as 2 x 2 buffer pixels. Code that writes g_fb directly must use the HI layout (books_text.c draws its
// letters at the full resolution of the panel: the sharp text of the readers). gfx_hi_want(): the wish of the app
// (main.c clears it at each change of app). main.c calls gfx_hi_frame() before each render: the frame is HI only when
// the app wants it and no system layer (the panel, a message, the home bar card) draws over it.
#define ORIENT_HI 0x400
void gfx_hi_frame(bool allow);
int gfx_hi_stride(void);
// The flags of board_lcd_flush() for this frame: ORIENT_NATIVE, ORIENT_HI.
int gfx_frame_bits(void);
// g_fb keeps the picture of the frame before the last one. When that picture is HI, this makes it a normal picture (one
// buffer pixel of each 2 x 2): for a system copy of the frame (the layer of a message, the panel).
void gfx_hi_flatten(void);
// Logical point to the physical touch frame (the inverse of gfx_touch_to_logical; for tests).
void gfx_logical_to_touch(int lx, int ly, int *px, int *py);

void gfx_init(void);
// Change to the other frame buffer. Returns the finished one, for board_lcd_flush().
// The new g_fb keeps the frame before the last one, so apps must draw all the screen.
uint16_t *gfx_swap(void);
