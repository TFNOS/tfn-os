#pragma once
// tfdraw: smooth shapes for the app files (WEATHER, ON AIR, the icons): a pixel with alpha, discs, rings, thick
// lines, rounded boxes with alpha, gradients, a glow that adds light, polygons, and text that fits a width. All
// static (TFD_FN: no warning for a function that an app does not use): an app includes this header in ONE
// source file. The colours are in the byte order of the frame (rgb() of gfx.h). They write g_fb and obey
// only the edges of the screen and the box of tfd_clip().
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "tfn/gfx.h"
#include "tfn/kfont.h"
#include "tfn/uikit.h"

#define TFD_FN static __attribute__((unused))
static inline int tfd_fl(float v)
{
    int i = (int)v;
    return v < (float)i ? i - 1 : i;
}
static inline int tfd_ce(float v)
{
    int i = (int)v;
    return v > (float)i ? i + 1 : i;
}
static inline float tfd_min(float a, float b)
{
    return a < b ? a : b;
}
static inline float tfd_max(float a, float b)
{
    return a > b ? a : b;
}
static int tfd_cx0, tfd_cy0, tfd_cx1 = 4096, tfd_cy1 = 4096;  // the clip box of tfdraw
static uint16_t *tfd_fb;                                      // the target: the screen or a picture
static int tfd_w, tfd_h;
static inline void tfd_screen(void)
{
    tfd_fb = g_fb;
    tfd_w = gfx_w;
    tfd_h = gfx_h;
}
static inline void tfd_target(uint16_t *buf, int w, int h)
{
    tfd_fb = buf;
    tfd_w = w;
    tfd_h = h;
}
static inline void tfd_clip(int x0, int y0, int x1, int y1)
{
    tfd_cx0 = x0;
    tfd_cy0 = y0;
    tfd_cx1 = x1;
    tfd_cy1 = y1;
}
static inline void tfd_noclip(void)
{
    tfd_cx0 = tfd_cy0 = 0;
    tfd_cx1 = tfd_cy1 = 4096;
}

static inline uint16_t tfd_sw(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
// a mix of two frame colours: a = 0 gives d, a = 256 gives s
static inline uint16_t tfd_mix(uint16_t d, uint16_t s, int a)
{
    if (a <= 0) {
        return d;
    }
    if (a >= 256) {
        return s;
    }
    uint32_t dd = tfd_sw(d), ss = tfd_sw(s), a5 = (uint32_t)(a + 4) >> 3;
    dd = (dd | (dd << 16)) & 0x07E0F81F;
    ss = (ss | (ss << 16)) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
    return tfd_sw((uint16_t)(r | (r >> 16)));
}
// light that adds (a glow): d + s * a / 256, each channel at most full
static inline uint16_t tfd_add(uint16_t d, uint16_t s, int a)
{
    uint16_t D = tfd_sw(d), S = tfd_sw(s);
    int r = (D >> 11) + (((S >> 11) * a) >> 8), g = ((D >> 5) & 63) + ((((S >> 5) & 63) * a) >> 8),
        b = (D & 31) + (((S & 31) * a) >> 8);
    if (r > 31) {
        r = 31;
    }
    if (g > 63) {
        g = 63;
    }
    if (b > 31) {
        b = 31;
    }
    return tfd_sw((uint16_t)((r << 11) | (g << 5) | b));
}
static inline uint16_t tfd_rgb(int r, int g, int b)
{
    return rgb((uint8_t)(r < 0     ? 0
                         : r > 255 ? 255
                                   : r),
               (uint8_t)(g < 0     ? 0
                         : g > 255 ? 255
                                   : g),
               (uint8_t)(b < 0     ? 0
                         : b > 255 ? 255
                                   : b));
}
static inline void tfd_unrgb(uint16_t c, int *r, int *g, int *b)
{
    uint16_t v = tfd_sw(c);
    *r = ((v >> 11) & 31) * 255 / 31;
    *g = ((v >> 5) & 63) * 255 / 63;
    *b = (v & 31) * 255 / 31;
}
static inline bool tfd_in(int x, int y)
{
    return x >= tfd_cx0 && y >= tfd_cy0 && x < tfd_cx1 && y < tfd_cy1 && (unsigned)x < (unsigned)tfd_w &&
           (unsigned)y < (unsigned)tfd_h;
}
static inline void tfd_px(int x, int y, uint16_t c, int a)
{
    if (a <= 0 || !tfd_in(x, y)) {
        return;
    }
    uint16_t *p = &tfd_fb[y * tfd_w + x];
    *p = a >= 256 ? c : tfd_mix(*p, c, a);
}
static inline void tfd_pxadd(int x, int y, uint16_t c, int a)
{
    if (a <= 0 || !tfd_in(x, y)) {
        return;
    }
    uint16_t *p = &tfd_fb[y * tfd_w + x];
    *p = tfd_add(*p, c, a);
}

// a span of one row with alpha (x1 not drawn)
TFD_FN void tfd_hspan(int x0, int x1, int y, uint16_t c, int a)
{
    if (y < tfd_cy0 || y >= tfd_cy1 || (unsigned)y >= (unsigned)tfd_h || a <= 0) {
        return;
    }
    if (x0 < tfd_cx0) {
        x0 = tfd_cx0;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > tfd_cx1) {
        x1 = tfd_cx1;
    }
    if (x1 > tfd_w) {
        x1 = tfd_w;
    }
    uint16_t *p = &tfd_fb[y * tfd_w];
    if (a >= 256) {
        for (int x = x0; x < x1; x++) {
            p[x] = c;
        }
        return;
    }
    for (int x = x0; x < x1; x++) {
        p[x] = tfd_mix(p[x], c, a);
    }
}
TFD_FN void tfd_rect(int x, int y, int w, int h, uint16_t c, int a)
{
    for (int j = y; j < y + h; j++) {
        tfd_hspan(x, x + w, j, c, a);
    }
}
// a gradient from the top colour to the bottom colour
TFD_FN void tfd_vgrad(int x, int y, int w, int h, uint16_t c0, uint16_t c1)
{
    for (int j = 0; j < h; j++) {
        tfd_hspan(x, x + w, y + j, tfd_mix(c0, c1, h > 1 ? j * 256 / (h - 1) : 0), 256);
    }
}

// a disc with a smooth edge (a = the alpha of the inside, 0..256)
TFD_FN void tfd_disc(float cx, float cy, float r, uint16_t c, int a)
{
    int y0 = tfd_fl(cy - r - 1), y1 = tfd_ce(cy + r + 1);
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy;
        float h2 = (r + 0.5f) * (r + 0.5f) - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float hw = sqrtf(h2);
        int x0 = tfd_fl(cx - hw), x1 = tfd_ce(cx + hw);
        // the inside (the pixels whose middle is at r - 0.5 or nearer) is one span; only the edge needs a root
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
            float dx = x + 0.5f - cx, d = sqrtf(dx * dx + dy * dy);
            float cov = r + 0.5f - d;
            if (cov <= 0) {
                continue;
            }
            tfd_px(x, y, c, cov >= 1 ? a : (int)(a * cov));
        }
    }
}
// a ring: the middle radius r, the width w
TFD_FN void tfd_ring(float cx, float cy, float r, float w, uint16_t c, int a)
{
    float ro = r + w / 2, ri = r - w / 2;
    int y0 = tfd_fl(cy - ro - 1), y1 = tfd_ce(cy + ro + 1);
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy;
        if (dy * dy > (ro + 1) * (ro + 1)) {
            continue;
        }
        float hw = sqrtf((ro + 1) * (ro + 1) - dy * dy);
        int x0 = tfd_fl(cx - hw), x1 = tfd_ce(cx + hw);
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, d = sqrtf(dx * dx + dy * dy);
            float cov = tfd_min(ro + 0.5f - d, d - ri + 0.5f);
            if (cov <= 0) {
                continue;
            }
            tfd_px(x, y, c, cov >= 1 ? a : (int)(a * cov));
        }
    }
}
// an arc of a ring, from the angle a0 to a1 (radians; 0 = right, the angles turn with the clock on the screen)
TFD_FN void tfd_arc(float cx, float cy, float r, float w, float a0, float a1, uint16_t c, int a)
{
    float ro = r + w / 2, ri = r - w / 2;
    int y0 = tfd_fl(cy - ro - 1), y1 = tfd_ce(cy + ro + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = tfd_fl(cx - ro - 1); x <= tfd_ce(cx + ro + 1); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float cov = tfd_min(ro + 0.5f - d, d - ri + 0.5f);
            if (cov <= 0) {
                continue;
            }
            float an = atan2f(dy, dx);
            while (an < a0) {
                an += 6.2831853f;
            }
            while (an > a0 + 6.2831853f) {
                an -= 6.2831853f;
            }
            if (an > a1) {
                continue;
            }
            tfd_px(x, y, c, cov >= 1 ? a : (int)(a * cov));
        }
    }
}
// a line with round ends (w = its width)
TFD_FN void tfd_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int a)
{
    float r = w / 2;
    int bx0 = tfd_fl(tfd_min(x0, x1) - r - 1), bx1 = tfd_ce(tfd_max(x0, x1) + r + 1);
    int by0 = tfd_fl(tfd_min(y0, y1) - r - 1), by1 = tfd_ce(tfd_max(y0, y1) + r + 1);
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    float avy = vy < 0 ? -vy : vy, L = sqrtf(L2);
    for (int y = by0; y <= by1; y++) {
        int xa = bx0, xb = bx1;
        if (avy > 0.5f * L && L > 0) {
            // (a steep line: the x of the line in this row, and the width of the line in a row)
            float t = (y + 0.5f - y0) / vy;
            float xc = x0 + vx * (t < 0 ? 0 : t > 1 ? 1 : t);
            float hw = (r + 1.5f) * L / avy + 1;
            int a2 = tfd_fl(xc - hw), b2 = tfd_ce(xc + hw);
            if (a2 > xa) {
                xa = a2;
            }
            if (b2 < xb) {
                xb = b2;
            }
        }
        for (int x = xa; x <= xb; x++) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0;
            float t = L2 > 0 ? (px * vx + py * vy) / L2 : 0;
            if (t < 0) {
                t = 0;
            }
            if (t > 1) {
                t = 1;
            }
            float dx = px - t * vx, dy = py - t * vy;
            float cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            tfd_px(x, y, c, cov >= 1 ? a : (int)(a * cov));
        }
    }
}
// a box with round corners (radius r) and a smooth edge
TFD_FN void tfd_rrect(int x, int y, int w, int h, int r, uint16_t c, int a)
{
    if (r * 2 > h) {
        r = h / 2;
    }
    if (r * 2 > w) {
        r = w / 2;
    }
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        float dy = 0;
        if (j < r) {
            dy = r - (j + 0.5f);
        } else if (j >= h - r) {
            dy = (j + 0.5f) - (h - r);
        }
        if (dy <= 0) {
            tfd_hspan(x, x + w, yy, c, a);
            continue;
        }
        float hw = sqrtf(tfd_max(0, r * r - dy * dy));
        int inner = tfd_ce(r - hw);
        tfd_hspan(x + inner + 1, x + w - inner - 1, yy, c, a);
        for (int k = 0; k <= inner && k < w / 2 + 1; k++) {
            float dx = r - (k + 0.5f), d = sqrtf(dx * dx + dy * dy);
            float cov = r + 0.5f - d;
            if (cov <= 0) {
                continue;
            }
            int aa = cov >= 1 ? a : (int)(a * cov);
            tfd_px(x + k, yy, c, aa);
            tfd_px(x + w - 1 - k, yy, c, aa);
        }
    }
}
// a soft glow: light that adds, strongest in the middle (a cheap falloff, r = its radius)
TFD_FN void tfd_glow(float cx, float cy, float r, uint16_t c, int a)
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
            tfd_pxadd(x, y, c, (int)(a * q * q));
        }
    }
}
// a filled polygon (n points as x, y pairs; also a concave one: the even-odd rule) with a smooth edge: 4 rows of
// samples in each pixel row, each with the exact x of its ends (made for the icons; at most 640 px wide)
TFD_FN void tfd_poly(const float *p, int n, uint16_t c, int a)
{
    float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
    for (int i = 0; i < n; i++) {
        x0 = tfd_min(x0, p[2 * i]);
        x1 = tfd_max(x1, p[2 * i]);
        y0 = tfd_min(y0, p[2 * i + 1]);
        y1 = tfd_max(y1, p[2 * i + 1]);
    }
    int xa = tfd_fl(x0), xb = tfd_ce(x1), ya = tfd_fl(y0), yb = tfd_ce(y1);
    if (xa < tfd_cx0) {
        xa = tfd_cx0;
    }
    if (xa < 0) {
        xa = 0;
    }
    if (xb > tfd_cx1 - 1) {
        xb = tfd_cx1 - 1;
    }
    if (xb > tfd_w - 1) {
        xb = tfd_w - 1;
    }
    if (ya < tfd_cy0) {
        ya = tfd_cy0;
    }
    if (ya < 0) {
        ya = 0;
    }
    if (yb > tfd_cy1 - 1) {
        yb = tfd_cy1 - 1;
    }
    if (yb > tfd_h - 1) {
        yb = tfd_h - 1;
    }
    int w = xb - xa + 1;
    if (w <= 0 || w > 640 || n < 3 || n > 32) {
        return;
    }
    float cov[642];
    for (int y = ya; y <= yb; y++) {
        bool any = false;
        memset(cov, 0, sizeof(float) * (size_t)(w + 1));
        for (int s = 0; s < 4; s++) {
            float sy = y + 0.125f + s * 0.25f, xs[32];
            int k = 0;
            for (int i = 0; i < n; i++) {
                float ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * ((i + 1) % n)], by = p[2 * ((i + 1) % n) + 1];
                if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                    xs[k++] = ax + (sy - ay) * (bx - ax) / (by - ay);
                }
            }
            for (int i = 1; i < k; i++) {
                for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                    float t = xs[j];
                    xs[j] = xs[j - 1];
                    xs[j - 1] = t;
                }
            }
            for (int j = 0; j + 1 < k; j += 2) {
                float l = tfd_max(0, xs[j] - xa), r = tfd_min((float)w, xs[j + 1] - xa);
                if (r <= l) {
                    continue;
                }
                int il = (int)l, ir = (int)r;
                any = true;
                if (il == ir) {
                    cov[il] += (r - l) * 0.25f;
                    continue;
                }
                cov[il] += (il + 1 - l) * 0.25f;
                for (int i = il + 1; i < ir; i++) {
                    cov[i] += 0.25f;
                }
                if (ir < w) {
                    cov[ir] += (r - ir) * 0.25f;
                }
            }
        }
        if (!any) {
            continue;
        }
        for (int i = 0; i < w; i++) {
            if (cov[i] > 0.002f) {
                tfd_px(xa + i, y, c, cov[i] >= 1 ? a : (int)(a * cov[i]));
            }
        }
    }
}

// ---------------------------------------------------------------- text (the kfont faces: English and Hebrew)
// cut a UTF-8 text so that it fits w px (with "..." at the end); out has max bytes
TFD_FN const char *tfd_fit(const kfont_t *f, const char *s, int w, char *out, int max)
{
    if (ui_text_w(f, s) <= w) {
        return s;
    }
    int n = (int)strlen(s);
    if (n > max - 4) {
        n = max - 4;
    }
    while (n > 0) {
        while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) {
            n--;  // (the start of a letter)
        }
        memcpy(out, s, (size_t)n);
        while (n > 0 && out[n - 1] == ' ') {
            n--;
        }
        memcpy(out + n, "...", 4);
        if (ui_text_w(f, out) <= w) {
            return out;
        }
        n--;
    }
    return "";
}
TFD_FN bool tfd_heb(const char *s)
{
    for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
        if (*p == 0xD7) {
            return true;  // (U+05C0 .. U+05FF)
        }
    }
    return false;
}
// the first face in which the text fits w px (else the last one: the text is then cut)
TFD_FN const kfont_t *tfd_pick(const kfont_t *a, const kfont_t *b, const char *s, int w)
{
    return ui_text_w(a, s) <= w ? a : b;
}
// a text from x (the left end) or to x (the right end), cut to w px
TFD_FN void tfd_text_l(const kfont_t *f, int x, int base, const char *s, uint16_t c, int w)
{
    char b[160];
    const char *t = tfd_fit(f, s, w, b, sizeof b);
    ui_text_c(f, x + ui_text_w(f, t) / 2, base, t, c);
}
TFD_FN void tfd_text_r(const kfont_t *f, int x, int base, const char *s, uint16_t c, int w)
{
    char b[160];
    const char *t = tfd_fit(f, s, w, b, sizeof b);
    int tw = ui_text_w(f, t);
    ui_text_c(f, x - tw + tw / 2, base, t, c);
}
TFD_FN void tfd_text_c(const kfont_t *f, int cx, int base, const char *s, uint16_t c, int w)
{
    char b[160];
    ui_text_c(f, cx, base, tfd_fit(f, s, w, b, sizeof b), c);
}
// a text at the start side of a line: the left end in English, the right end in Hebrew (rtl)
TFD_FN void tfd_text_s(const kfont_t *f, int x0, int x1, int base, const char *s, uint16_t c, bool rtl)
{
    if (rtl) {
        tfd_text_r(f, x1, base, s, c, x1 - x0);
    } else {
        tfd_text_l(f, x0, base, s, c, x1 - x0);
    }
}
