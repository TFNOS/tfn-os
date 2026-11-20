#pragma once
// AATEXT: the anti-aliased Bungee faces of the firmware in place of the 5 x 7 dot font of gfx_text, for an app that drew
// its text with the gfx_text functions. One header with static functions: each app has its own copy (only ABI names:
// no new firmware, no font data).
//
// A dot scale k (the dot text is 7 * k px high and 6 * k px a letter) takes the face of about the same width:
// 1 = MF_TINY, 2 = MF_N16, 3 = MF_N24, 4 and more = MF_N34. y stays the top of the dot line, and the capitals keep the
// middle of the dot line. The big faces have capitals only: a small letter shows as its capital. A space is 0.3 of the
// capital height at least.
//
// THE USE: include this header after the tfn headers, then map the names of the file:
//   #define gfx_text           aat_text
//   #define gfx_text_width     aat_w
//   #define gfx_text_center    aat_text_center
//   #define gfx_text_center_in aat_text_center_in
//   #define gfx_clip_bottom    aat_clip_bottom     (only an app that clips its text)
//   #define gfx_clip_reset     aat_clip_reset
// The text writes g_fb directly: it obeys the screen edges and the bottom clip of aat_clip_bottom() (the gfx clip box
// is not readable), not the other clips of gfx.
#include <stddef.h>
#include <stdint.h>
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/uikit.h"

static int aat_clip_y = 1 << 30;  // nothing is drawn at y >= aat_clip_y

// the bottom clip of the gfx shapes and of this text
static inline void aat_clip_bottom(int y)
{
    aat_clip_y = y;
    gfx_clip_bottom(y);
}
static inline void aat_clip_reset(void)
{
    aat_clip_y = 1 << 30;
    gfx_clip_reset();
}

static inline const mfont_t *aat_face(int k)
{
    return k <= 1 ? &MF_TINY : k == 2 ? &MF_N16 : k == 3 ? &MF_N24 : &MF_N34;
}
// the code point that the face draws (a small letter: its capital in a face of capitals only, MF_N16 .. MF_N34)
static inline int aat_cp(int ch, const mfont_t *f)
{
    return (f->n <= 64 && ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
}
// the advance of a letter in quarters of a px (a space: 0.3 of the capital height at least)
static inline int aat_adv4(const mfont_t *f, int ch)
{
    const mglyph_t *g = mf_glyph(f, aat_cp(ch, f));
    if (!g) {
        return 0;
    }
    const int sp = f->asc * 6 / 5;
    return ch == ' ' && g->adv4 < sp ? sp : g->adv4;
}
// the width of s in face f, in px
static inline int aat_fw(const mfont_t *f, const char *s)
{
    int w4 = 0;
    for (; *s; s++) {
        w4 += aat_adv4(f, (uint8_t)*s);
    }
    return (w4 + 2) / 4;
}
static inline int aat_w(const char *s, int k)
{
    return aat_fw(aat_face(k), s);
}
// s in face f from x (the left end) on the baseline base
static inline void aat_draw(const mfont_t *f, int x, int base, const char *s, uint16_t c)
{
    const int y1 = aat_clip_y < gfx_h ? aat_clip_y : gfx_h;
    int x4 = x * 4;
    for (; *s; s++) {
        const int ch = (uint8_t)*s;
        const mglyph_t *g = mf_glyph(f, aat_cp(ch, f));
        if (!g) {
            continue;
        }
        const int gx = (x4 + 2) / 4 + g->ox, gy = base + g->oy;
        const uint8_t *src = f->a4 + g->off;
        for (int j = 0; j < g->h; j++) {
            const int py = gy + j;
            if (py < 0 || py >= y1) {
                continue;
            }
            uint16_t *row = g_fb + (size_t)py * gfx_w;
            for (int i = 0; i < g->w; i++) {
                const int px = gx + i;
                if (px < 0 || px >= gfx_w) {
                    continue;
                }
                const int n = j * g->w + i, v = (n & 1) ? src[n >> 1] >> 4 : src[n >> 1] & 15;
                if (v) {
                    row[px] = v == 15 ? c : ui_mix(row[px], c, v * 17);
                }
            }
        }
        x4 += aat_adv4(f, ch);
    }
}
// the baseline of face f in the dot line of scale k from y (its top): the capitals keep the middle of the dot line
static inline int aat_base(const mfont_t *f, int y, int k)
{
    const mglyph_t *g = mf_glyph(f, 'H');
    const int cap = g ? -g->oy : f->asc * 3 / 4;
    return y + (7 * k + cap + 1) / 2;
}
// the text from x (the left end) in the dot line from y (its top) of scale k
static inline void aat_text(int x, int y, const char *s, int k, uint16_t c)
{
    const mfont_t *f = aat_face(k);
    aat_draw(f, x, aat_base(f, y, k), s, c);
}
static inline void aat_text_center_in(int x, int w, int y, const char *s, int k, uint16_t c)
{
    aat_text(x + (w - aat_w(s, k)) / 2, y, s, k, c);
}
static inline void aat_text_center(int y, const char *s, int k, uint16_t c)
{
    aat_text_center_in(0, gfx_w, y, s, k, c);
}
// the text in the middle of x .. x + w, in the dot line from y of scale k: the face of the scale, or the biggest smaller
// face that fits in w px (all the Bungee faces, from MF_N34 to MF_TINY); the middle of the line stays
static inline void aat_text_fit(int x, int w, int y, const char *s, int k, uint16_t c)
{
    static const mfont_t *const F[8] = {&MF_N34, &MF_N28, &MF_N24, &MF_N20, &MF_N16, &MF_LABEL, &MF_SMALL, &MF_TINY};
    const mfont_t *f = aat_face(k);
    int i = 0;
    while (i < 7 && F[i] != f) {
        i++;
    }
    while (i < 7 && aat_fw(F[i], s) > w) {
        i++;
    }
    aat_draw(F[i], x + (w - aat_fw(F[i], s)) / 2, aat_base(F[i], y, k), s, c);
}
