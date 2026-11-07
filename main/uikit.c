// The small kit for app screens (uikit.h).
#include "tfn_utf8.h"
#include "uikit.h"
#include <math.h>
#include <string.h>
#include "gfx.h"

// gfx colours are in the byte order of the LCD: the mixes work on the plain 565 value
static inline uint16_t sw16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
static inline uint16_t mix565(uint16_t d, uint16_t s, int a)
{
    uint32_t a5 = ((uint32_t)a + 4) >> 3;
    uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81F, ss = (s | ((uint32_t)s << 16)) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
    return (uint16_t)(r | (r >> 16));
}
uint16_t ui_mix(uint16_t a, uint16_t b, int t)
{
    if (t <= 0) {
        return a;
    }
    if (t >= 255) {
        return b;
    }
    return sw16(mix565(sw16(a), sw16(b), t));
}
void ui_px(int x, int y, uint16_t c, int a)
{
    if ((unsigned)x >= (unsigned)gfx_w || (unsigned)y >= (unsigned)gfx_h || a <= 0) {
        return;
    }
#if BOARD_HI_FRAME
    if (gfx_hi()) {
        // (the HI frame: the 2 x 2 buffer pixels of the logical pixel)
        const int s = gfx_hi_stride();
        uint16_t *q = &g_fb[2 * y * s + 2 * x];
        uint16_t *pp[4] = {q, q + 1, q + s, q + s + 1};
        for (int k = 0; k < 4; k++) {
            *pp[k] = a >= 255 ? c : sw16(mix565(sw16(*pp[k]), sw16(c), a));
        }
        return;
    }
#endif
    uint16_t *p = &g_fb[y * gfx_w + x];
    *p = a >= 255 ? c : sw16(mix565(sw16(*p), sw16(c), a));
}
static void span(int x0, int x1, int y, uint16_t c)  // [x0, x1)
{
    if ((unsigned)y >= (unsigned)gfx_h) {
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > gfx_w) {
        x1 = gfx_w;
    }
#if BOARD_HI_FRAME
    if (gfx_hi()) {
        const int s = gfx_hi_stride();
        for (int r = 0; r < 2; r++) {
            uint16_t *q = &g_fb[(2 * y + r) * s];
            for (int x = 2 * x0; x < 2 * x1; x++) {
                q[x] = c;
            }
        }
        return;
    }
#endif
    uint16_t *p = &g_fb[y * gfx_w];
    for (int x = x0; x < x1; x++) {
        p[x] = c;
    }
}
static void span_a(int x0, int x1, int y, uint16_t c, int a)
{
    if (a >= 255) {
        span(x0, x1, y, c);
        return;
    }
    for (int x = x0; x < x1; x++) {
        ui_px(x, y, c, a);
    }
}

// ---------------------------------------------------------------- boxes and circles
void ui_rrect(int x, int y, int w, int h, int r, uint16_t c)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (r > w / 2) {
        r = w / 2;
    }
    if (r > h / 2) {
        r = h / 2;
    }
    if (r < 1) {
        for (int j = 0; j < h; j++) {
            span(x, x + w, y + j, c);
        }
        return;
    }
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if ((unsigned)yy >= (unsigned)gfx_h) {
            continue;
        }
        float dy = j < r ? r - (j + 0.5f) : j >= h - r ? (j + 0.5f) - (h - r) : 0;
        if (dy <= 0) {
            span(x, x + w, yy, c);
            continue;
        }
        // the corner pixels: the coverage from the distance to the corner centre
        span(x + r, x + w - r, yy, c);
        for (int i = 0; i < r; i++) {
            float dx = r - (i + 0.5f), d = sqrtf(dx * dx + dy * dy);
            float cov = r - d + 0.5f;
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 255 : (int)(cov * 255);
            ui_px(x + i, yy, c, a);
            ui_px(x + w - 1 - i, yy, c, a);
        }
    }
}
void ui_rrect_a(int x, int y, int w, int h, int r, uint16_t c, int a)
{
    if (w <= 0 || h <= 0 || a <= 0) {
        return;
    }
    if (a >= 255) {
        ui_rrect(x, y, w, h, r, c);
        return;
    }
    if (r > w / 2) {
        r = w / 2;
    }
    if (r > h / 2) {
        r = h / 2;
    }
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if ((unsigned)yy >= (unsigned)gfx_h) {
            continue;
        }
        float dy = r < 1 ? 0 : j < r ? r - (j + 0.5f) : j >= h - r ? (j + 0.5f) - (h - r) : 0;
        if (dy <= 0) {
            span_a(x < 0 ? 0 : x, x + w > gfx_w ? gfx_w : x + w, yy, c, a);
            continue;
        }
        span_a(x + r < 0 ? 0 : x + r, x + w - r > gfx_w ? gfx_w : x + w - r, yy, c, a);
        for (int i = 0; i < r; i++) {
            float dx = r - (i + 0.5f), d = sqrtf(dx * dx + dy * dy), cov = r - d + 0.5f;
            if (cov <= 0) {
                continue;
            }
            int k = (int)((cov >= 1 ? 1 : cov) * a);
            ui_px(x + i, yy, c, k);
            ui_px(x + w - 1 - i, yy, c, k);
        }
    }
}
void ui_rrect_band(int x, int y, int w, int h, int r, uint16_t c, int a, int t)
{
    if (w <= 0 || h <= 0 || a <= 0) {
        return;
    }
    if (r > w / 2) {
        r = w / 2;
    }
    if (r > h / 2) {
        r = h / 2;
    }
    const int ix0 = x + t, ix1 = x + w - t;  // (the middle box, not drawn: x ix0..ix1-1 in the rows t..h-t-1)
    for (int j = 0; j < h; j++) {
        const int yy = y + j;
        if ((unsigned)yy >= (unsigned)gfx_h) {
            continue;
        }
        const bool mid = j >= t && j < h - t && ix1 > ix0;
        const float dy = r < 1 ? 0 : j < r ? r - (j + 0.5f) : j >= h - r ? (j + 0.5f) - (h - r) : 0;
        const int f0 = dy <= 0 ? x : x + r, f1 = dy <= 0 ? x + w : x + w - r;  // (the full part of the row)
        if (!mid) {
            span_a(f0 < 0 ? 0 : f0, f1 > gfx_w ? gfx_w : f1, yy, c, a);
        } else {
            const int l1 = f1 < ix0 ? f1 : ix0, r0 = f0 > ix1 ? f0 : ix1;
            span_a(f0 < 0 ? 0 : f0, l1 > gfx_w ? gfx_w : l1, yy, c, a);
            span_a(r0 < 0 ? 0 : r0, f1 > gfx_w ? gfx_w : f1, yy, c, a);
        }
        if (dy <= 0) {
            continue;
        }
        for (int i = 0; i < r; i++) {
            const float dx = r - (i + 0.5f), d = sqrtf(dx * dx + dy * dy), cov = r - d + 0.5f;
            if (cov <= 0) {
                continue;
            }
            const int k = (int)((cov >= 1 ? 1 : cov) * a);
            const int xl = x + i, xr = x + w - 1 - i;
            if (!mid || xl < ix0 || xl >= ix1) {
                ui_px(xl, yy, c, k);
            }
            if (!mid || xr < ix0 || xr >= ix1) {
                ui_px(xr, yy, c, k);
            }
        }
    }
}
void ui_rbox(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border, int t)
{
    if (t > 0) {
        ui_rrect(x, y, w, h, r, border);
        ui_rrect(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 1, fill);
    } else {
        ui_rrect(x, y, w, h, r, fill);
    }
}
void ui_circle(int cx, int cy, int r, uint16_t c, int a)
{
    if (r <= 0) {
        return;
    }
    float ro = r + 0.5f, ri = r - 0.5f;
    for (int dy = -r - 1; dy <= r + 1; dy++) {
        int yy = cy + dy;
        if ((unsigned)yy >= (unsigned)gfx_h) {
            continue;
        }
        float fy = (float)dy;
        if (fabsf(fy) > ro) {
            continue;
        }
        int xo = (int)ceilf(sqrtf(ro * ro - fy * fy));
        int xi = ri * ri > fy * fy ? (int)floorf(sqrtf(ri * ri - fy * fy)) : -1;
        if (xi >= 0) {
            span_a(cx - xi, cx + xi + 1, yy, c, a);
        }
        for (int dx = xi + 1; dx <= xo; dx++) {
            float d = sqrtf((float)dx * dx + fy * fy), cov = r - d + 0.5f;
            if (cov <= 0) {
                continue;
            }
            int k = (int)((cov >= 1 ? 1 : cov) * a);
            ui_px(cx + dx, yy, c, k);
            ui_px(cx - dx, yy, c, k);
        }
    }
}
void ui_ring(int cx, int cy, int r, int t, uint16_t c, int a)
{
    if (r <= 0 || t <= 0) {
        return;
    }
    float ro = r + t * 0.5f, ri = r - t * 0.5f;
    if (ri < 0) {
        ri = 0;
    }
    int R = (int)ceilf(ro) + 1;
    for (int dy = -R; dy <= R; dy++) {
        int yy = cy + dy;
        if ((unsigned)yy >= (unsigned)gfx_h) {
            continue;
        }
        float fy = (float)dy;
        if (fabsf(fy) > ro + 0.5f) {
            continue;
        }
        int xo = (int)ceilf(sqrtf(fmaxf(0, (ro + 0.5f) * (ro + 0.5f) - fy * fy)));
        int xi = (ri - 0.5f) > fabsf(fy) ? (int)floorf(sqrtf((ri - 0.5f) * (ri - 0.5f) - fy * fy)) : 0;
        for (int dx = xi; dx <= xo; dx++) {
            float d = sqrtf((float)dx * dx + fy * fy);
            float cov = fminf(ro - d + 0.5f, d - ri + 0.5f);
            if (cov <= 0) {
                continue;
            }
            int k = (int)((cov >= 1 ? 1 : cov) * a);
            ui_px(cx + dx, yy, c, k);
            if (dx) {
                ui_px(cx - dx, yy, c, k);
            }
        }
    }
}

// ---------------------------------------------------------------- text
static const kglyph_t *kglyph(const kfont_t *f, int cp)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->g[m].cp == cp) {
            return &f->g[m];
        }
        if (f->g[m].cp < cp) {
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    return NULL;
}
static inline bool heb(uint16_t c)
{
    return c >= 0x590 && c <= 0x5FF;
}
static float adv(const kfont_t *f, uint16_t c)
{
    const kglyph_t *g = kglyph(f, c);
    return g ? g->adv4 / 4.0f : (c == ' ' ? f->asc * 0.3f : 0);
}
static float run_w(const kfont_t *f, const uint16_t *cp, int n)
{
    float w = 0;
    for (int i = 0; i < n; i++) {
        w += adv(f, cp[i]);
    }
    return w;
}
static void glyph(const kfont_t *f, float x, int base, uint16_t c, uint16_t col)
{
    const kglyph_t *g = kglyph(f, c);
    if (!g) {
        return;
    }
    int gx = (int)lroundf(x) + g->ox, gy = base + g->oy;
    const uint8_t *src = f->a4 + g->off;
    for (int j = 0; j < g->h; j++) {
        int y = gy + j;
        if ((unsigned)y >= (unsigned)gfx_h) {
            continue;
        }
        for (int k = 0; k < g->w; k++) {
            int idx = j * g->w + k, v = (idx & 1) ? src[idx >> 1] >> 4 : src[idx >> 1] & 15;
            if (v) {
                ui_px(gx + k, y, col, v * 17);
            }
        }
    }
}
// a letter of a Latin or a number run (left to right) inside the word i..j: a Latin letter or a digit, or a sign between
// two of them ("2.5", "A/B")
static bool alnum16(uint16_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static bool ltr_at(const uint16_t *cp, int i, int j, int k)
{
    if (alnum16(cp[k])) {
        return true;
    }
    if (heb(cp[k]) || k == i || k + 1 >= j) {
        return false;
    }
    return alnum16(cp[k - 1]) && alnum16(cp[k + 1]) && (cp[k] == '.' || cp[k] == ',' || cp[k] == ':' || cp[k] == '/');
}
// one line from x (the left end): a line with Hebrew from the right (the words from the right, the letters of a Hebrew
// word from the right; a number or a Latin word keeps its order)
static void line_at(const kfont_t *f, const uint16_t *cp, int n, float x0, int base, uint16_t col)
{
    bool rtl = false;
    for (int i = 0; i < n; i++) {
        if (heb(cp[i])) {
            rtl = true;
            break;
        }
    }
    if (!rtl) {
        float x = x0;
        for (int i = 0; i < n; i++) {
            glyph(f, x, base, cp[i], col);
            x += adv(f, cp[i]);
        }
        return;
    }
    float x = x0 + run_w(f, cp, n);
    int i = 0;
    while (i < n) {
        int j = i;
        while (j < n && cp[j] != ' ') {
            j++;
        }
        bool hw = false;
        for (int k = i; k < j; k++) {
            if (heb(cp[k])) {
                hw = true;
                break;
            }
        }
        float ww = run_w(f, cp + i, j - i);
        x -= ww;
        if (hw) {
            // a word with Hebrew: its runs from the right; the letters of a Hebrew run from the right, a Latin or a number
            // run in its order ("ה-WIFI" shows WIFI-ה; the whole word does not turn)
            float xx = x + ww;
            int k = i;
            while (k < j) {
                int e = k + 1;
                bool l = ltr_at(cp, i, j, k);
                while (e < j && ltr_at(cp, i, j, e) == l) {
                    e++;
                }
                float rw = run_w(f, cp + k, e - k);
                xx -= rw;
                if (l) {
                    float xr = xx;
                    for (int m = k; m < e; m++) {
                        glyph(f, xr, base, cp[m], col);
                        xr += adv(f, cp[m]);
                    }
                } else {
                    float xr = xx + rw;
                    for (int m = k; m < e; m++) {
                        xr -= adv(f, cp[m]);
                        glyph(f, xr, base, cp[m], col);
                    }
                }
                k = e;
            }
        } else {
            float xx = x;
            for (int k = i; k < j; k++) {
                glyph(f, xx, base, cp[k], col);
                xx += adv(f, cp[k]);
            }
        }
        if (j < n) {
            x -= adv(f, ' ');
        }
        i = j + 1;
    }
}
#define TXT_MAX 96
void ui_text(const kfont_t *f, int x, int base, const char *utf8, uint16_t col)
{
    uint16_t cp[TXT_MAX];
    int n = tfn_utf8_decode(utf8, cp, TXT_MAX, true);
    line_at(f, cp, n, (float)x, base, col);
}
void ui_text_c(const kfont_t *f, int cx, int base, const char *utf8, uint16_t col)
{
    uint16_t cp[TXT_MAX];
    int n = tfn_utf8_decode(utf8, cp, TXT_MAX, true);
    line_at(f, cp, n, cx - run_w(f, cp, n) / 2, base, col);
}
int ui_text_w(const kfont_t *f, const char *utf8)
{
    uint16_t cp[TXT_MAX];
    int n = tfn_utf8_decode(utf8, cp, TXT_MAX, true);
    return (int)ceilf(run_w(f, cp, n));
}
const kfont_t *ui_text_fit(const kfont_t *const *fonts, int n, const char *utf8, int w)
{
    for (int i = 0; i < n; i++) {
        if (ui_text_w(fonts[i], utf8) <= w) {
            return fonts[i];
        }
    }
    return fonts[n - 1];
}
uint16_t ui_name_colour(const char *utf8)
{
    static const uint8_t PAL[8][3] = {{255, 170, 90},  {120, 210, 255}, {170, 230, 120}, {255, 140, 170},
                                      {200, 160, 255}, {255, 220, 110}, {110, 230, 200}, {240, 150, 120}};
    uint32_t h = 2166136261u;
    for (const char *p = utf8; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    const uint8_t *c = PAL[h % 8];
    return rgb(c[0], c[1], c[2]);
}
void ui_initial(const char *utf8, char out[4])
{
    // the first letter of the last word: "12AB MINE" = M, "TFN 12AB KIM" = K, "TFN 1234" = 1
    const uint8_t *p = (const uint8_t *)utf8, *w = p;
    for (const uint8_t *q = p; *q; q++) {
        if (*q != ' ' && (q == p || q[-1] == ' ')) {
            w = q;
        }
    }
    p = w;
    int n = *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : *p ? 1 : 0;
    memcpy(out, p, (size_t)n);
    out[n] = 0;
    if (n == 1 && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 32);
    }
}
