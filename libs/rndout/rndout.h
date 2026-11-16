// rndout: a round layout for an app that draws a frame of fixed size (the round panels: the AMOLED-1.75C 466 x 466,
// the Watcher 412 x 412; app_t.round = APP_ROUND_HI).
//
// The app draws a SQUARE frame of S x S px (S about 2/3 of the panel: 311 on the 1.75C) with its usual code and the
// gfx_* calls: rndo_enter() / rndo_leave() put g_fb, gfx_w and gfx_h on the square frame and back again. Then
// rndo_out() scales the square frame out to the frame of the panel, and writes only the pixels in the circle (the
// panel sends only them; the pixels out of the circle are not written). The 1.75C (466 = 1.5 x 311): 2 source
// pixels give 3 (a copy, the mean of the two, a copy) on both axes, so the picture stays sharp; other sizes: the
// nearest source pixel; a frame of half the size: 2 x 2 (sharp pixel art). Use an even S (310) when the app writes two pixels at a time (32-bit stores).
//
// Use (one static rndo_t of the app, in .bss):
//   enter:  rndo_open(&R, 311);              leave: rndo_close(&R);
//   render: if (rndo_enter(&R, true)) { ...draw (gfx_w = gfx_h = S)...; rndo_leave(&R); rndo_out(&R); }
//   input:  rndo_point(&R, &x, &y);          (a point of the panel to a point of the square frame)
//   update and input: rndo_enter(&R, false) ... rndo_leave(&R) when their code reads gfx_w / gfx_h.
// The frame has the byte order of g_fb (the LCD order).
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "tfn/gfx.h"

#define RNDO_MAX 480  // the largest frame of a panel (the tables have one entry for each row and column)

typedef struct {
    uint16_t *fb;  // the square frame (PSRAM), s x s; NULL: no frame (the app draws as before)
    int s;         // its size
    int d;         // the size of the tables (the frame of the panel), 0: not made yet
    int depth;     // rndo_enter calls that are open
    bool sharp;    // the 1.5 path with no means (copies only: faster; set it after rndo_open)
    uint16_t *keep_fb;
    int keep_w, keep_h;
    int16_t ix[RNDO_MAX];  // each output column (and row): its source column (row)
    int16_t x0[RNDO_MAX];  // each output row: the span in the circle
    int16_t x1[RNDO_MAX];
} rndo_t;

// the square frame of size s (false: no PSRAM; the app then draws the frame of the panel)
static inline bool rndo_open(rndo_t *R, int s)
{
    if (!R->fb) {
        R->fb = (uint16_t *)heap_caps_calloc((size_t)s * s, 2, MALLOC_CAP_SPIRAM);
    }
    R->s = s;
    R->d = 0;
    R->depth = 0;
    R->sharp = false;
    return R->fb != NULL;
}
static inline void rndo_close(rndo_t *R)
{
    free(R->fb);
    R->fb = NULL;
}
// the app sees the square frame: gfx_w = gfx_h = s (and g_fb = the square frame when draw is true)
static inline bool rndo_enter(rndo_t *R, bool draw)
{
    if (!R->fb) {
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
    gfx_w = R->s;
    gfx_h = R->s;
    return true;
}
static inline void rndo_leave(rndo_t *R)
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
// the size of the frame of the panel (gfx_w outside rndo_enter)
static inline int rndo_panel(const rndo_t *R)
{
    int d = R->depth > 0 ? R->keep_w : gfx_w;
    return d > RNDO_MAX ? RNDO_MAX : d;
}
// a point of the panel to a point of the square frame
static inline void rndo_point(const rndo_t *R, int *x, int *y)
{
    int d = rndo_panel(R);
    if (!R->fb || d <= 0) {
        return;
    }
    *x = *x * R->s / d;
    *y = *y * R->s / d;
}
// a point of the square frame to a point of the panel
static inline void rndo_to_panel(const rndo_t *R, int *x, int *y)
{
    int d = rndo_panel(R);
    if (!R->fb || d <= 0) {
        return;
    }
    *x = *x * d / R->s;
    *y = *y * d / R->s;
}
static inline uint16_t rndo_avg(uint16_t a, uint16_t b)
{
    // (the LCD order: the bytes back to the native order, the mean of the two, the bytes swapped again)
    uint16_t p = (uint16_t)((a >> 8) | (a << 8)), q = (uint16_t)((b >> 8) | (b << 8));
    uint16_t m = (uint16_t)(((p & 0xF7DE) >> 1) + ((q & 0xF7DE) >> 1));
    return (uint16_t)((m >> 8) | (m << 8));
}
static inline void rndo_tables(rndo_t *R, int d)
{
    for (int o = 0; o < d; o++) {
        int i = o * R->s / d;
        R->ix[o] = (int16_t)(i < R->s ? i : R->s - 1);
    }
    float r = d * 0.5f + 1;  // (+1: each pixel that shows is painted)
    for (int y = 0; y < d; y++) {
        float dy = y + 0.5f - d * 0.5f, h = r * r - dy * dy;
        float hw = h > 0 ? sqrtf(h) : 0;
        int a = (int)(d * 0.5f - hw), b = (int)(d * 0.5f + hw + 1);
        R->x0[y] = (int16_t)(a < 0 ? 0 : a);
        R->x1[y] = (int16_t)(b > d ? d : b);
    }
    R->d = d;
}
// the square frame out to the frame of the panel (call it after rndo_leave: g_fb is the frame of the panel)
static inline void rndo_out(rndo_t *R)
{
    if (!R->fb || R->depth != 0) {
        return;
    }
    int d = gfx_w < gfx_h ? gfx_w : gfx_h, S = R->s;
    if (d > RNDO_MAX) {
        d = RNDO_MAX;
    }
    if (d != R->d) {
        rndo_tables(R, d);
    }
    const uint16_t *src = R->fb;
    if (d == S * 3 / 2 || d == S * 3 / 2 + 1) {
        // the 1.75C (466 from 311, or from 310: the last column and row twice): each group of 2 x 2 source pixels
        // gives 3 x 3 (the middle row and column: the means)
        static uint16_t l0[RNDO_MAX], l1[RNDO_MAX];
        for (int y = 0; y < d; y++) {
            int k = y / 3, m = y - k * 3, a0 = R->x0[y], a1 = R->x1[y];
            if (m == 0) {
                int r0 = 2 * k < S ? 2 * k : S - 1, r1 = r0 + 1 < S ? r0 + 1 : r0;
                const uint16_t *a = src + (size_t)r0 * S, *b = src + (size_t)r1 * S;
                for (int j = 0, x = 0; x < d; j += 2, x += 3) {
                    int j0 = j < S ? j : S - 1, j1 = j0 + 1 < S ? j0 + 1 : j0;
                    uint16_t p = a[j0], q = a[j1], u = b[j0], v = b[j1];
                    l0[x] = p;
                    l1[x] = u;
                    if (x + 1 < d) {
                        l0[x + 1] = R->sharp ? p : rndo_avg(p, q);
                        l1[x + 1] = R->sharp ? u : rndo_avg(u, v);
                    }
                    if (x + 2 < d) {
                        l0[x + 2] = q;
                        l1[x + 2] = v;
                    }
                }
            }
            uint16_t *dst = g_fb + (size_t)y * gfx_w;
            if (m == 1 && !R->sharp) {
                for (int x = a0; x < a1; x++) {
                    dst[x] = rndo_avg(l0[x], l1[x]);
                }
            } else {
                const uint16_t *p = m == 2 ? l1 : l0;  // (sharp: the middle row is a copy of the first one)
                memcpy(dst + a0, p + a0, (size_t)(a1 - a0) * 2);
            }
        }
        return;
    }
    if (d == 2 * S || d == 2 * S + 1) {
        // a frame of half the size (466 from 233, 412 from 206): each source pixel gives 2 x 2
        for (int y = 0; y < d; y++) {
            int r = y >> 1 < S ? y >> 1 : S - 1, a0 = R->x0[y] & ~1, a1 = R->x1[y];
            const uint16_t *a = src + (size_t)r * S;
            uint16_t *dst = g_fb + (size_t)y * gfx_w;
            for (int x = a0; x < a1; x += 2) {
                uint16_t v = a[x >> 1 < S ? x >> 1 : S - 1];
                dst[x] = v;
                if (x + 1 < a1) {
                    dst[x + 1] = v;
                }
            }
        }
        return;
    }
    for (int y = 0; y < d; y++) {
        const uint16_t *a = src + (size_t)R->ix[y] * S;
        uint16_t *dst = g_fb + (size_t)y * gfx_w;
        for (int x = R->x0[y]; x < R->x1[y]; x++) {
            dst[x] = a[R->ix[x]];
        }
    }
}
