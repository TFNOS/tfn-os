// One copy of the Bungee faces (menu_font.inc) for the menu and the games, and a simple drawer.
// The menu keeps its own drawer (it marks the dirty rectangles); the games use mf_text().
#include <math.h>
#include <stddef.h>
#include "gfx.h"
#include "mfont.h"
#if BOARD_HI_FRAME
#include "uikit.h"
#endif
#include "menu_font.inc"

static inline uint16_t sw16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
static inline uint16_t mix565(uint16_t d, uint16_t s, int a)  // native colours, a 0..255
{
    uint32_t a5 = ((uint32_t)a + 4) >> 3;
    uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81F, ss = (s | ((uint32_t)s << 16)) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
    return (uint16_t)(r | (r >> 16));
}

const mglyph_t *mf_glyph(const mfont_t *f, int cp)
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

int mf_width(const mfont_t *f, const char *s, int spacing)
{
    int w4 = 0, n = 0;
    for (; *s; s++, n++) {
        const mglyph_t *g = mf_glyph(f, (uint8_t)*s);
        if (g) {
            w4 += g->adv4;
        }
    }
    return (w4 + 2) / 4 + (n > 1 ? (n - 1) * spacing : 0);
}

void mf_text(const mfont_t *f, int x, int base, const char *s, uint16_t col, int alpha, int spacing)
{
    if (alpha <= 0) {
        return;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    uint16_t nc = sw16(col);
    float fx = (float)x;
    for (; *s; s++) {
        const mglyph_t *g = mf_glyph(f, (uint8_t)*s);
        if (!g) {
            continue;
        }
        int gx = (int)lroundf(fx) + g->ox, gy = base + g->oy;
        const uint8_t *src = f->a4 + g->off;
        for (int j = 0; j < g->h; j++) {
            int y = gy + j;
            if ((unsigned)y >= (unsigned)gfx_h) {
                continue;
            }
            uint16_t *row = &g_fb[y * gfx_w];
            for (int k = 0; k < g->w; k++) {
                int px = gx + k;
                if ((unsigned)px >= (unsigned)gfx_w) {
                    continue;
                }
                int idx = j * g->w + k, v = (idx & 1) ? src[idx >> 1] >> 4 : src[idx >> 1] & 15;
                if (!v) {
                    continue;
                }
                int a = v * 17 * alpha / 255;
#if BOARD_HI_FRAME
                if (gfx_hi()) {
                    ui_px(px, y, col, a);  // (the HI frame: uikit draws the 2 x 2 buffer pixels)
                    continue;
                }
#endif
                row[px] = a >= 255 ? col : sw16(mix565(sw16(row[px]), nc, a));
            }
        }
        fx += g->adv4 / 4.0f + spacing;
    }
}

void mf_text_c(const mfont_t *f, int cx, int base, const char *s, uint16_t col, int alpha, int spacing)
{
    mf_text(f, cx - mf_width(f, s, spacing) / 2, base, s, col, alpha, spacing);
}

void mf_text_o(const mfont_t *f, int x, int base, const char *s, uint16_t col, uint16_t outline, int sh, int spacing)
{
    if (sh) {
        mf_text(f, x + sh, base + sh, s, outline, 200, spacing);
    }
    mf_text(f, x - 1, base, s, outline, 255, spacing);
    mf_text(f, x + 1, base, s, outline, 255, spacing);
    mf_text(f, x, base - 1, s, outline, 255, spacing);
    mf_text(f, x, base + 1, s, outline, 255, spacing);
    mf_text(f, x, base, s, col, 255, spacing);
}

void mf_text_oc(const mfont_t *f, int cx, int base, const char *s, uint16_t col, uint16_t outline, int sh, int spacing)
{
    mf_text_o(f, cx - mf_width(f, s, spacing) / 2, base, s, col, outline, sh, spacing);
}
