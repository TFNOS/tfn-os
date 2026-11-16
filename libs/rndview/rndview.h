#pragma once
// rndview: a round layout for an app that draws a scene in a frame of its own size (the round panels: the AMOLED-1.75C
// 466 x 466, the Watcher 412 x 412; app_t.round = APP_ROUND_HI). As libs/rndout, but the frame can be a rectangle and
// its size can change from one screen of the app to the next (for example a square place view, then a 320 x 240
// close-up).
//
// The app draws its scene in a frame of w x h px with its usual code: rv_enter() / rv_leave() put g_fb, gfx_w and
// gfx_h on the frame and back again. rv_out() scales the frame to the width of the panel (k = panel / w), puts it in
// the middle of the panel and writes only the pixels in the circle. The rows above and below a frame that is not as
// tall as the panel (the bands) get the first and the last row of the frame, darker to the edge. Then the app draws its
// HUD (the buttons, the text) in the frame of the panel, on the arcs of the circle.
//
// Use (one static rv_t of the app, in .bss):
//   enter:  rv_open(&R, max_w * max_h);      leave: rv_close(&R);
//   render: rv_frame(&R, w, h); if (rv_enter(&R, true)) { ...draw (gfx_w = w, gfx_h = h)...; rv_leave(&R);
//           rv_out(&R); } ...the HUD...
//   input:  rv_point(&R, &x, &y)  (a point of the panel to a point of the frame)
// The frame has the byte order of g_fb (the LCD order).
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "tfn/gfx.h"

#define RV_MAX 480  // the largest frame of a panel (the tables have one entry for each row and column)

typedef struct {
    uint16_t *fb;  // the frame (PSRAM); NULL: no frame (the app draws as before)
    int cap;       // its size in pixels
    int w, h;      // the frame now
    int depth;     // rv_enter calls that are open
    uint16_t *keep_fb;
    int keep_w, keep_h;
    int tw, th, td;      // the tables were made for this frame and this panel
    int oy, oh;          // the frame on the panel: its top row and its height
    int16_t ix[RV_MAX];  // each output column (and row of the frame): its source column (row)
    int16_t iy[RV_MAX];  // each output row: its source row, or -1 - the distance into a band
    int16_t x0[RV_MAX];  // each output row: the span in the circle
    int16_t x1[RV_MAX];
} rv_t;

static inline bool rv_open(rv_t *R, int pixels)
{
    if (!R->fb) {
        R->fb = (uint16_t *)heap_caps_calloc((size_t)pixels, 2, MALLOC_CAP_SPIRAM);
    }
    R->cap = R->fb ? pixels : 0;
    R->depth = 0;
    R->td = 0;
    return R->fb != NULL;
}
static inline void rv_close(rv_t *R)
{
    free(R->fb);
    R->fb = NULL;
    R->cap = 0;
}
// the size of the frame for the next draw (w * h must not be more than the size of rv_open)
static inline void rv_frame(rv_t *R, int w, int h)
{
    if ((long)w * h > R->cap || w <= 0 || h <= 0) {
        return;
    }
    R->w = w;
    R->h = h;
}
static inline bool rv_enter(rv_t *R, bool draw)
{
    if (!R->fb || R->w <= 0) {
        return false;
    }
    if (R->depth++ == 0) {
        R->keep_fb = g_fb;
        R->keep_w = gfx_w;
        R->keep_h = gfx_h;
    }
    if (draw) {
        g_fb = R->fb;
    }
    gfx_w = R->w;
    gfx_h = R->h;
    return true;
}
static inline void rv_leave(rv_t *R)
{
    if (!R->fb || R->depth <= 0) {
        return;
    }
    if (--R->depth == 0) {
        g_fb = R->keep_fb;
        gfx_w = R->keep_w;
        gfx_h = R->keep_h;
    }
}
// the size of the panel (gfx_w outside rv_enter)
static inline int rv_panel(const rv_t *R)
{
    int d = R->depth > 0 ? R->keep_w : gfx_w;
    return d > RV_MAX ? RV_MAX : d;
}
// the frame on the panel: its top row and its height
static inline void rv_place(const rv_t *R, int d, int *oy, int *oh)
{
    *oh = R->h * d / R->w;
    *oy = (d - *oh) / 2;
}
// a point of the panel to a point of the frame
static inline void rv_point(const rv_t *R, int *x, int *y)
{
    const int d = rv_panel(R);
    if (!R->fb || d <= 0 || R->w <= 0) {
        return;
    }
    int oy, oh;
    rv_place(R, d, &oy, &oh);
    *x = *x * R->w / d;
    *y = (*y - oy) * R->w / d;
}
// a point of the frame to a point of the panel
static inline void rv_to_panel(const rv_t *R, int *x, int *y)
{
    const int d = rv_panel(R);
    if (!R->fb || d <= 0 || R->w <= 0) {
        return;
    }
    int oy, oh;
    rv_place(R, d, &oy, &oh);
    *x = *x * d / R->w;
    *y = oy + *y * d / R->w;
}
// the half chord of the circle of the panel (diameter d) at row y, m px inside the edge (0 out of the circle)
static inline int rv_half(int d, int y, int m)
{
    const int r = d / 2 - m, dy = y - d / 2;
    if (r <= 0 || dy <= -r || dy >= r) {
        return 0;
    }
    int v = r * r - dy * dy, h = 0;
    while ((h + 1) * (h + 1) <= v) {
        h++;
    }
    return h;
}
// the colour v (LCD order) with k/16 of its light
static inline uint16_t rv_dim(uint16_t v, int k)
{
    const uint16_t p = (uint16_t)((v >> 8) | (v << 8));
    const int r = (p >> 11) * k >> 4, g = ((p >> 5) & 63) * k >> 4, b = (p & 31) * k >> 4;
    const uint16_t o = (uint16_t)(r << 11 | g << 5 | b);
    return (uint16_t)((o >> 8) | (o << 8));
}
static inline void rv_tables(rv_t *R, int d)
{
    int oy, oh;
    rv_place(R, d, &oy, &oh);
    for (int o = 0; o < d; o++) {
        int i = o * R->w / d;
        R->ix[o] = (int16_t)(i < R->w ? i : R->w - 1);
        const int yy = o - oy;
        if (yy < 0) {
            R->iy[o] = (int16_t)(yy);  // (-1 at the edge of the frame, more into the band)
        } else if (yy >= oh) {
            R->iy[o] = (int16_t)(-1000 - (yy - oh));
        } else {
            int j = yy * R->w / d;
            R->iy[o] = (int16_t)(j < R->h ? j : R->h - 1);
        }
    }
    float r = d * 0.5f + 1;  // (+1: each pixel that shows is painted)
    for (int y = 0; y < d; y++) {
        float dy = y + 0.5f - d * 0.5f, hh = r * r - dy * dy;
        int hw = 0;
        if (hh > 0) {
            while ((hw + 1) * (hw + 1) <= (int)hh) {
                hw++;
            }
        }
        int a = (int)(d * 0.5f) - hw - 1, b = (int)(d * 0.5f) + hw + 2;
        R->x0[y] = (int16_t)(a < 0 ? 0 : a);
        R->x1[y] = (int16_t)(b > d ? d : b);
    }
    R->tw = R->w;
    R->th = R->h;
    R->td = d;
    R->oy = oy;
    R->oh = oh;
}
// the frame out to the frame of the panel (call it after rv_leave: g_fb is the frame of the panel). band = the length
// of the fade to black in the bands (px of the panel; 0: black bands)
static inline void rv_out(rv_t *R, int band)
{
    if (!R->fb || R->depth != 0 || R->w <= 0) {
        return;
    }
    int d = gfx_w < gfx_h ? gfx_w : gfx_h;
    if (d > RV_MAX) {
        d = RV_MAX;
    }
    if (d != R->td || R->w != R->tw || R->h != R->th) {
        rv_tables(R, d);
    }
    const int W = R->w;
    const bool two = d == 2 * W || d == 2 * W + 1;
    for (int y = 0; y < d; y++) {
        uint16_t *dst = g_fb + (size_t)y * gfx_w;
        const int a0 = R->x0[y], a1 = R->x1[y];
        int sy = R->iy[y], k = 16;
        if (sy < 0) {
            const int dist = sy <= -1000 ? -1000 - sy : -1 - sy;
            sy = sy <= -1000 ? R->h - 1 : 0;
            k = band > 0 && dist < band ? 16 - dist * 16 / band : 0;
            if (k <= 0) {
                for (int x = a0; x < a1; x++) {
                    dst[x] = 0;
                }
                continue;
            }
        }
        const uint16_t *src = R->fb + (size_t)sy * W;
        if (k < 16) {
            for (int x = a0; x < a1; x++) {
                dst[x] = rv_dim(src[R->ix[x]], k);
            }
        } else if (two) {
            for (int x = a0; x < a1; x++) {
                const int i = x >> 1;
                dst[x] = src[i < W ? i : W - 1];
            }
        } else {
            for (int x = a0; x < a1; x++) {
                dst[x] = src[R->ix[x]];
            }
        }
    }
}
