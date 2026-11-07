#pragma once
// tficon: the shared look of the menu icons of the utilities. All apps and games
// have beautiful and animated icons. One family: a tile with round corners, a soft gradient from the top colour to
// the bottom colour, a gloss on the upper part, a rim of light, a darker bottom edge and a soft shadow; on it a
// symbol with a gradient, a soft shadow and a highlight. The motion uses icon_time() (app.h) with a loop of 2 s (or
// 1 s): the store draws 16 frames of 125 ms (sdk/docs/visual.md, section 8), so a loop of 2 s has no jump.
// All static (TFD_FN): an app includes this header in ONE source file (it includes libs/tfdraw/tfdraw.h).
// The cost: the shapes fill whole rows and calculate only the edge pixels. The menu draws a built-in icon each
// frame: keep it well under 1 ms on the ESP32-S3 (a tile of 76 px is 76 row fills).
#include "../tfdraw/tfdraw.h"
#include "tfn/app.h"

typedef struct {
    int x, y, w, h, r;
} tfi_box_t;

// the state of tfdraw before the icon (the app can use tfdraw on its own screen)
static uint16_t *tfi_sv_fb;
static int tfi_sv_w, tfi_sv_h, tfi_sv_c[4];
// the steps of the alpha of an edge pixel: 0 = smooth (an icon that the menu draws live); 4 = the store mode
static int tfi_aq;

// the start of an icon: tfdraw draws on the screen, only in the box of the icon
static inline void tfi_begin(int x, int y, int s)
{
    tfi_sv_fb = tfd_fb;
    tfi_sv_w = tfd_w;
    tfi_sv_h = tfd_h;
    tfi_sv_c[0] = tfd_cx0;
    tfi_sv_c[1] = tfd_cy0;
    tfi_sv_c[2] = tfd_cx1;
    tfi_sv_c[3] = tfd_cy1;
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
    tfi_aq = 0;
}
// the store mode, after tfi_begin(): the icon of a store app (icon.bin) has 255 colours at most; with more,
// mk_app.py takes fewer bits of each colour and the gradients get bands. Each edge pixel over a gradient is a new
// colour, so in this mode the edges use 4 steps of alpha (the board shows icon.bin smaller, with no smooth scale)
static inline void tfi_store(void)
{
    tfi_aq = 4;
}
// a pixel with alpha (the steps of the store mode)
static inline void tfi_px(int x, int y, uint16_t c, int a)
{
    if (tfi_aq && a > 0 && a < 256) {
        a = (a * tfi_aq + 128) / 256 * 256 / tfi_aq;
    }
    tfd_px(x, y, c, a);
}
// the end of an icon: the state of tfdraw as before
static inline void tfi_end(void)
{
    tfd_fb = tfi_sv_fb;
    tfd_w = tfi_sv_w;
    tfd_h = tfi_sv_h;
    tfd_clip(tfi_sv_c[0], tfi_sv_c[1], tfi_sv_c[2], tfi_sv_c[3]);
    tfi_aq = 0;
}

// the phase of the motion now: 0 .. 1 in a loop of `period` s (use 2, 1 or 0.5 for the 16 frames of the store)
static inline float tfi_ph(float period)
{
    float p = icon_time() / period;
    return p - (float)(int)p;
}
// a soft step: 0 at e0, 1 at e1
static inline float tfi_step(float e0, float e1, float v)
{
    float k = (v - e0) / (e1 - e0);
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    return k * k * (3 - 2 * k);
}
// an alpha (0 .. 256) on one of n steps: the icon.bin of a store app has 255 colours at most (mk_app.py takes fewer
// bits of each colour when there are more: the gradients get bands), so a fade in a store icon uses few steps
static inline int tfi_qa(int a, int n)
{
    if (a <= 0) {
        return 0;
    }
    if (a >= 256) {
        return 256;
    }
    return (a * n / 256 + 1) * 256 / n;
}
static inline uint16_t tfi_hex(uint32_t h)
{
    return rgb((uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h);
}
// a mix of two colours with 8 bits for each channel (f = 0 .. 256): smoother than tfd_mix for a gradient
static inline uint16_t tfi_lerp(uint16_t c0, uint16_t c1, int f)
{
    int r0, g0, b0, r1, g1, b1;
    tfd_unrgb(c0, &r0, &g0, &b0);
    tfd_unrgb(c1, &r1, &g1, &b1);
    return tfd_rgb(r0 + (r1 - r0) * f / 256, g0 + (g1 - g0) * f / 256, b0 + (b1 - b0) * f / 256);
}
static inline uint16_t tfi_light(uint16_t c, int a)
{
    return tfd_mix(c, rgb(255, 255, 255), a);
}
static inline uint16_t tfi_dark(uint16_t c, int a)
{
    return tfd_mix(c, 0, a);
}

// a row j (0 .. h - 1) of a box with round corners (radius r): its distance into a corner (0 = a straight row)
static inline float tfi_corner_dy(int j, int h, int r)
{
    if (j < r) {
        return r - (j + 0.5f);
    }
    if (j >= h - r) {
        return (j + 0.5f) - (h - r);
    }
    return 0;
}
// one row of a box with round corners (x, y = its top left; j = the row; c = its colour; a = alpha)
TFD_FN void tfi_rbox_row(int x, int y, int w, int h, int r, int j, uint16_t c, int a)
{
    float dy = tfi_corner_dy(j, h, r);
    int yy = y + j;
    if (dy <= 0) {
        tfd_hspan(x, x + w, yy, c, a);
        return;
    }
    float dy2 = dy * dy, ro = r + 0.5f, ri = r - 0.5f;
    float xo = ro * ro > dy2 ? r - sqrtf(ro * ro - dy2) : (float)r;
    float xi = ri > 0 && ri * ri > dy2 ? r - sqrtf(ri * ri - dy2) : (float)r;
    int k0 = (int)xo, k1 = tfd_ce(xi);
    if (k1 > w / 2) {
        k1 = w / 2;
    }
    tfd_hspan(x + k1, x + w - k1, yy, c, a);
    for (int k = k0; k < k1; k++) {
        float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy2);
        if (cov <= 0) {
            continue;
        }
        int aa = cov >= 1 ? a : (int)(a * cov);
        tfi_px(x + k, yy, c, aa);
        if (x + w - 1 - k != x + k) {
            tfi_px(x + w - 1 - k, yy, c, aa);
        }
    }
}
// a box with round corners and a gradient from top to bot (a = alpha, 256 = solid)
TFD_FN void tfi_rbox(int x, int y, int w, int h, int r, uint16_t top, uint16_t bot, int a)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (r * 2 > h) {
        r = h / 2;
    }
    if (r * 2 > w) {
        r = w / 2;
    }
    for (int j = 0; j < h; j++) {
        tfi_rbox_row(x, y, w, h, r, j, top == bot ? top : tfi_lerp(top, bot, h > 1 ? j * 256 / (h - 1) : 0), a);
    }
}
// a box with round corners in the look of a glossy button: the gradient, a gloss on the upper part, a rim of light
// at the top and a darker last row
TFD_FN void tfi_gloss_box(int x, int y, int w, int h, int r, uint16_t top, uint16_t bot)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (r * 2 > h) {
        r = h / 2;
    }
    if (r * 2 > w) {
        r = w / 2;
    }
    int gh = h * 45 / 100, rim = h >= 40 ? 2 : 1;
    uint16_t wh = rgb(255, 255, 255);
    for (int j = 0; j < h; j++) {
        uint16_t c = tfi_lerp(top, bot, h > 1 ? j * 256 / (h - 1) : 0);
        if (j < rim) {
            c = tfi_lerp(c, wh, 120);
        } else if (j < gh) {
            c = tfi_lerp(c, wh, 18 + 40 * (gh - j) / gh);
        } else if (j == gh) {
            c = tfi_lerp(c, wh, 8);
        }
        if (j >= h - rim) {
            c = tfi_lerp(c, 0, 70);
        }
        tfi_rbox_row(x, y, w, h, r, j, c, 256);
    }
}
// the tile of an icon in the box of s px at (x, y): the soft shadow, then the glossy tile. Returns its box.
TFD_FN tfi_box_t tfi_tile(int x, int y, int s, uint16_t top, uint16_t bot)
{
    int m = (s + 8) / 16;
    tfi_box_t b = {x + m, y + m - (s >= 40 ? 1 : 0), s - 2 * m, s - 2 * m, 0};
    b.r = b.w * 22 / 100;
    // the shadow: the outline of the tile, lower and softer, under it
    int sh = s / 26 + 1;
    for (int k = 1; k <= sh + 1; k++) {
        int yy = b.y + b.h - 1 + k;
        int in = b.r - (int)sqrtf((float)(b.r * b.r) - (float)((b.r - k * 2) > 0 ? (b.r - k * 2) * (b.r - k * 2) : 0)) +
                 k / 2;
        tfd_hspan(b.x + in, b.x + b.w - in, yy, 0, 110 * (sh + 2 - k) / (sh + 2));
    }
    tfi_gloss_box(b.x, b.y, b.w, b.h, b.r, top, bot);
    return b;
}

// the soft shadow of a box with round corners (the box at x, y; the shadow d px lower): only the rows under the box
// (the box covers the rest), so it costs little
TFD_FN void tfi_drop(int x, int y, int w, int h, int r, int d, int a)
{
    if (w <= 0 || h <= 0 || d <= 0) {
        return;
    }
    if (r * 2 > h) {
        r = h / 2;
    }
    if (r * 2 > w) {
        r = w / 2;
    }
    int j0 = h - r - d;
    if (j0 < 0) {
        j0 = 0;
    }
    for (int j = j0; j < h; j++) {
        tfi_rbox_row(x, y + d, w, h, r, j, 0, a);
    }
}
// the soft shadow of a disc (d px lower): only the rows under the middle of the disc
TFD_FN void tfi_drop_disc(float cx, float cy, float r, float d, int a)
{
    float sy = cy + d;
    int y0 = tfd_fl(cy + d / 2), y1 = tfd_ce(sy + r + 1);
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - sy, h2 = (r + 0.5f) * (r + 0.5f) - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float hw = sqrtf(h2), hi2 = (r - 0.5f) * (r - 0.5f) - dy * dy, hwi = hi2 > 0 ? sqrtf(hi2) : 0;
        int x0 = tfd_fl(cx - hw), x1 = tfd_ce(cx + hw), xi0 = tfd_ce(cx - hwi), xi1 = tfd_fl(cx + hwi) - 1;
        if (xi0 <= xi1) {
            tfd_hspan(xi0, xi1 + 1, y, 0, a);
        }
        for (int x = x0; x <= x1; x++) {
            if (x >= xi0 && x <= xi1) {
                x = xi1;
                continue;
            }
            float dx = x + 0.5f - cx, cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov > 0) {
                tfi_px(x, y, 0, cov >= 1 ? a : (int)(a * cov));
            }
        }
    }
}
// a disc with a gradient from top to bot (a = alpha)
TFD_FN void tfi_disc(float cx, float cy, float r, uint16_t top, uint16_t bot, int a)
{
    int y0 = tfd_fl(cy - r - 1), y1 = tfd_ce(cy + r + 1);
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy;
        float h2 = (r + 0.5f) * (r + 0.5f) - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float f = (dy + r) / (2 * r);
        uint16_t c = top == bot ? top : tfi_lerp(top, bot, (int)(256 * (f < 0 ? 0 : f > 1 ? 1 : f)));
        float hw = sqrtf(h2);
        int x0 = tfd_fl(cx - hw), x1 = tfd_ce(cx + hw);
        int xi0 = x1 + 1, xi1 = x1;
        float hi2 = (r - 0.5f) * (r - 0.5f) - dy * dy;
        if (hi2 > 0) {
            float hwi = sqrtf(hi2);
            xi0 = tfd_ce(cx - hwi - 0.5f);
            xi1 = tfd_fl(cx + hwi - 0.5f);
            if (xi0 <= xi1) {
                tfd_hspan(xi0, xi1 + 1, y, c, a);
            }
        }
        for (int x = x0; x <= x1; x++) {
            if (x >= xi0 && x <= xi1) {
                x = xi1;
                continue;
            }
            float dx = x + 0.5f - cx, cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            tfi_px(x, y, c, cov >= 1 ? a : (int)(a * cov));
        }
    }
}
// a band of a ring (the middle radius r, the width w), all of it (arc = false) or only from the angle a0 to a1
// (radians, a0 < a1; 0 = right, the angles turn with the clock on the screen): only the pixels of the band
TFD_FN void tfi_band(float cx, float cy, float r, float w, bool arc, float a0, float a1, uint16_t c, int a)
{
    float ro = r + w / 2, ri = r - w / 2;
    if (ri < 0) {
        ri = 0;
    }
    float ux0 = fcos(a0), uy0 = fsin(a0), ux1 = fcos(a1), uy1 = fsin(a1);
    bool big = a1 - a0 > 3.14159265f;
    float ya = -1, yb = 1;
    if (arc) {
        // (the rows of an arc: from its ends, and the top or the bottom of the circle when the arc goes over it)
        ya = tfd_min(uy0, uy1);
        yb = tfd_max(uy0, uy1);
        for (int k = -4; k <= 5; k++) {
            float q = -1.5707963f + k * 3.14159265f;
            if (q >= a0 && q <= a1) {
                ya = tfd_min(ya, (k & 1) ? 1.0f : -1.0f);
                yb = tfd_max(yb, (k & 1) ? 1.0f : -1.0f);
            }
        }
    }
    int y0 = tfd_fl(cy + (ya < 0 ? ya * ro : ya * ri) - 1), y1 = tfd_ce(cy + (yb > 0 ? yb * ro : yb * ri) + 1);
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy, d2 = dy * dy;
        float A = (ro + 0.5f) * (ro + 0.5f) - d2;
        if (A <= 0) {
            continue;
        }
        float ho = sqrtf(A), D = (ri - 0.5f) * (ri - 0.5f) - d2, hi = ri > 0.5f && D > 0 ? sqrtf(D) : 0;
        int xa = tfd_fl(cx - ho - 0.5f), xb = tfd_ce(cx + ho);
        // (the pixels in the hole are empty: the left part ends at xl, the right part starts at xr)
        int xl = hi > 0 ? tfd_fl(cx - hi - 0.5f) : xb, xr = hi > 0 ? tfd_ce(cx + hi - 0.5f) : xb + 1;
        for (int x = xa; x <= xb; x++) {
            if (x > xl && x < xr) {
                x = xr;
            }
            float dx = x + 0.5f - cx;
            if (arc) {
                // (in the arc: after a0 and before a1, with the cross products)
                bool in0 = ux0 * dy - uy0 * dx >= 0, in1 = dx * uy1 - dy * ux1 >= 0;
                if (big ? !(in0 || in1) : !(in0 && in1)) {
                    continue;
                }
            }
            float d = sqrtf(dx * dx + d2), cov = tfd_min(ro + 0.5f - d, d - ri + 0.5f);
            if (cov <= 0) {
                continue;
            }
            tfi_px(x, y, c, cov >= 1 ? a : (int)(a * cov));
        }
    }
}
static inline void tfi_ring(float cx, float cy, float r, float w, uint16_t c, int a)
{
    tfi_band(cx, cy, r, w, false, 0, 0, c, a);
}
// an arc with round ends (see tfi_band)
TFD_FN void tfi_arc(float cx, float cy, float r, float w, float a0, float a1, uint16_t c, int a)
{
    if (a1 <= a0) {
        return;
    }
    if (a1 - a0 >= 6.2831f) {
        tfi_ring(cx, cy, r, w, c, a);
        return;
    }
    tfi_band(cx, cy, r, w, true, a0, a1, c, a);
    tfi_disc(cx + fcos(a0) * r, cy + fsin(a0) * r, w / 2, c, c, a);
    tfi_disc(cx + fcos(a1) * r, cy + fsin(a1) * r, w / 2, c, c, a);
}
// a convex polygon (n <= 12 points as x, y pairs, in any turn) with a smooth edge, fast: for each pixel row the
// left and the right edge at the top and at the bottom of the row; the pixels between them get a linear ramp
TFD_FN void tfi_convex(const float *p, int n, uint16_t c, int a)
{
    if (n < 3 || n > 12) {
        return;
    }
    float ya = 1e9f, yb = -1e9f, ex[12], ey0[12], ey1[12], ek[12], eax[12], eay[12];
    for (int i = 0; i < n; i++) {
        ya = tfd_min(ya, p[2 * i + 1]);
        yb = tfd_max(yb, p[2 * i + 1]);
        // (each edge: its y range and its slope; a flat edge gives its two ends)
        float ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * ((i + 1) % n)], by = p[2 * ((i + 1) % n) + 1];
        eax[i] = ax;
        eay[i] = ay;
        ey0[i] = tfd_min(ay, by);
        ey1[i] = tfd_max(ay, by);
        ek[i] = by != ay ? (bx - ax) / (by - ay) : 0;
        ex[i] = by != ay ? 1e9f : tfd_max(ax, bx);
    }
    int y0 = tfd_fl(ya), y1 = tfd_ce(yb) - 1;
    if (y0 < tfd_cy0) {
        y0 = tfd_cy0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 >= tfd_cy1) {
        y1 = tfd_cy1 - 1;
    }
    if (y1 >= tfd_h) {
        y1 = tfd_h - 1;
    }
    for (int y = y0; y <= y1; y++) {
        float t0 = tfd_max((float)y, ya), t1 = tfd_min((float)(y + 1), yb), fy = t1 - t0;
        if (fy <= 0) {
            continue;
        }
        // the left and the right x at the heights t0 and t1 (+ a little inside, so that a point counts)
        float xl[2] = {1e9f, 1e9f}, xr[2] = {-1e9f, -1e9f};
        for (int h = 0; h < 2; h++) {
            float yy = h ? t1 - 0.001f : t0 + 0.001f;
            for (int i = 0; i < n; i++) {
                if (yy < ey0[i] || yy > ey1[i]) {
                    continue;
                }
                float xx = ex[i] < 1e8f ? tfd_min(eax[i], ex[i]) : eax[i] + (yy - eay[i]) * ek[i];
                float xx2 = ex[i] < 1e8f ? ex[i] : xx;
                xl[h] = tfd_min(xl[h], xx);
                xr[h] = tfd_max(xr[h], xx2);
            }
        }
        // (a vertex inside the row: its x also bounds the row)
        for (int i = 0; i < n; i++) {
            float vy = p[2 * i + 1];
            if (vy > t0 && vy < t1) {
                float vx = p[2 * i];
                for (int h = 0; h < 2; h++) {
                    xl[h] = tfd_min(xl[h], vx);
                    xr[h] = tfd_max(xr[h], vx);
                }
            }
        }
        if (xl[0] > 1e8f || xl[1] > 1e8f) {
            if (xl[0] > 1e8f) {
                xl[0] = xl[1];
                xr[0] = xr[1];
            } else {
                xl[1] = xl[0];
                xr[1] = xr[0];
            }
        }
        float l0 = tfd_min(xl[0], xl[1]), l1 = tfd_max(xl[0], xl[1]), r0 = tfd_min(xr[0], xr[1]),
              r1 = tfd_max(xr[0], xr[1]);
        int xa = tfd_fl(l0), xb = tfd_ce(r1) - 1;
        int fa = (int)(a * fy);
        int xf0 = tfd_ce(l1), xf1 = tfd_fl(r0) - 1;  // (the pixels that are inside all of the row)
        // (the cover of an edge pixel: a ramp over the x range of the edge in this row, or the part of the pixel)
        float il = l1 - l0 >= 1 ? 1 / (l1 - l0) : 0, ir = r1 - r0 >= 1 ? 1 / (r1 - r0) : 0, lm = (l0 + l1) / 2,
              rm = (r0 + r1) / 2;
        for (int x = xa; x <= xb; x++) {
            if (x >= xf0 && x <= xf1) {
                tfd_hspan(xf0, xf1 + 1, y, c, fa);
                x = xf1;
                continue;
            }
            float cl = il > 0 ? (x + 0.5f - l0) * il : x + 1 - lm, cr = ir > 0 ? (r1 - x - 0.5f) * ir : rm - x;
            cl = cl < 0 ? 0 : cl > 1 ? 1 : cl;
            cr = cr < 0 ? 0 : cr > 1 ? 1 : cr;
            float cv = cl * cr;
            if (cv > 0.004f) {
                tfi_px(x, y, c, (int)(fa * cv));
            }
        }
    }
}
// a thick line (width w) as a convex quad, fast; caps = true adds round ends (for a solid line: with a < 256 the ends
// get the alpha two times)
TFD_FN void tfi_line(float x0, float y0, float x1, float y1, float w, bool caps, uint16_t c, int a)
{
    float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy);
    if (L < 0.01f) {
        tfi_disc(x0, y0, w / 2, c, c, a);
        return;
    }
    float nx = -dy / L * w / 2, ny = dx / L * w / 2;
    float q[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    tfi_convex(q, 4, c, a);
    if (caps) {
        // (a half disc at each end would be exact; a whole disc is close: it covers the end of the quad)
        tfi_disc(x0, y0, w / 2, c, c, a);
        tfi_disc(x1, y1, w / 2, c, c, a);
    }
}
// a small spot of light (the highlight of a glossy shape)
static inline void tfi_spark(float cx, float cy, float r, int a)
{
    tfi_disc(cx, cy, r, rgb(255, 255, 255), rgb(255, 255, 255), a);
}
// a soft glow: light that adds, strongest in the middle (as tfd_glow; in the store mode on 6 steps)
TFD_FN void tfi_glow(float cx, float cy, float r, uint16_t c, int a)
{
    int y0 = (int)(cy - r), y1 = (int)(cy + r);
    for (int y = y0; y <= y1; y++) {
        float dy = (y + 0.5f - cy) / r;
        if (dy * dy >= 1) {
            continue;
        }
        float hw = sqrtf(1 - dy * dy) * r;
        for (int x = (int)(cx - hw); x <= (int)(cx + hw); x++) {
            float dx = (x + 0.5f - cx) / r, q = 1 - (dx * dx + dy * dy);
            if (q <= 0) {
                continue;
            }
            int v = (int)(a * q * q);
            if (tfi_aq) {
                v = (v * 6 + 128) / 256 * 256 / 6;
            }
            tfd_pxadd(x, y, c, v);
        }
    }
}
