#pragma once

#include <stdint.h>
#include "board.h"

// Frame buffer in the LOGICAL layout: gfx_w x gfx_h pixels, row after row (a landscape
// frame is 640 x 172), RGB565 with the bytes swapped for the SPI bus. The LCD task
// turns it to the physical layout while it sends it. There are two in PSRAM: the app
// draws into g_fb while the LCD task sends the other one.
extern uint16_t *g_fb;

// Logical orientation. All drawing uses logical coordinates.
typedef enum {
    ORIENT_PORTRAIT,       // 172 x 640, physical layout
    ORIENT_PORTRAIT_FLIP,  // 172 x 640, turned 180 degrees
    ORIENT_LAND_A,         // 640 x 172, logical down = physical -x
    ORIENT_LAND_B,         // 640 x 172, logical down = physical +x
} orient_t;

// Logical size for the current orientation
extern int gfx_w, gfx_h;

bool gfx_native(void);
void gfx_hi_want(bool on);
bool gfx_hi(void);
// A round layout now (BOARD_ROUND and the native frame): only the circle of diameter gfx_w
// (centre gfx_w / 2, gfx_h / 2) shows on the panel.
static inline bool gfx_round(void)
{
    return BOARD_ROUND && gfx_native();
}
// True when (x, y) is inside the round panel, at least margin px from its edge.
static inline bool gfx_in_circle(int x, int y, int margin)
{
    int r = gfx_w / 2 - margin, dx = x - gfx_w / 2, dy = y - gfx_h / 2;
    return r > 0 && dx * dx + dy * dy <= r * r;
}

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

// Fast sine and cosine from a table (for graphics and effects; about 1e-4 error).
float fsin(float x);
static inline float fcos(float x)
{
    return fsin(x + 1.5707963f);
}

// hue 0..359, sat and val 0..255
uint16_t hsv(int h, int s, int v);

void gfx_set_orient(orient_t o);
orient_t gfx_orient(void);
static inline bool orient_is_landscape(orient_t o)
{
    return o == ORIENT_LAND_A || o == ORIENT_LAND_B;
}
// Physical touch point to logical point
void gfx_touch_to_logical(int px, int py, int *x, int *y);

// A bottom clip line: nothing is drawn at logical y >= y (reset: no clip).
void gfx_clip_bottom(int y);
void gfx_clip_reset(void);
// A clip box: nothing is drawn outside x0 <= x < x1, y0 <= y < y1. gfx_clip_reset() does not
// remove it (an app icon can use the bottom line inside the box of the menu).
void gfx_clip_box(int x0, int y0, int x1, int y1);
void gfx_clip_box_off(void);

void gfx_fill(uint16_t c);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t c);
void gfx_frame_rect(int x, int y, int w, int h, int t, uint16_t c);
// Make an area darker: 0 = no change, 1 = half, 2 = quarter.
void gfx_darken_rect(int x, int y, int w, int h, int shift);
void gfx_fill_circle(int cx, int cy, int r, uint16_t c);
void gfx_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c);
void gfx_ring(int cx, int cy, int r_out, int r_in, uint16_t c);
// Filled triangle that points up: base from x to x+w at y+h, top at (x+w/2, y)
void gfx_spike(int x, int y, int w, int h, uint16_t c);

// 5x7 font, 6 pixels of advance at scale 1. Letters, digits and a few signs.
void gfx_text(int x, int y, const char *s, int scale, uint16_t c);
int gfx_text_width(const char *s, int scale);
void gfx_text_center(int y, const char *s, int scale, uint16_t c);
// Center the text in the band x .. x+w
void gfx_text_center_in(int x, int w, int y, const char *s, int scale, uint16_t c);
