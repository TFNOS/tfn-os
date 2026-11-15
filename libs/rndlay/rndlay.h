#pragma once
// rndlay: the shared round layout of the text utilities (REDDIT, TELEGRAM, GMAIL, WIKI, MQTT, the hardware tools ...).
// One header with static functions: each source file that includes it has its own copy (no ABI names, no new
// firmware). Use it only in the round frame of APP_ROUND_HI (gfx_round() is true): the circle of diameter gfx_w.
//
// The parts:
// - rl_half(): the half width of the circle at a row (the chord), less a margin;
// - rl_row(): the left edge and the width of a row of height h at y (the narrower end of the row);
// - rl_box(): the middle box of a given width: its top and its bottom stay inside the circle;
// - rl_disc(): the dark disc of the background and a thin rim; rl_caps(): the caps over a scrolled view;
// - rl_bar(): a scroll bar on the right arc of the circle;
// - rl_btn(): a round key on the arc (a touch target of 48 px or more);
// - rl_hit(): the test of a round key; rl_para(): a centred paragraph whose lines follow the circle.
#include <stdbool.h>
#include <stdint.h>
#include "tfn/gfx.h"
#include "tfn/kfont.h"
#include "tfn/uikit.h"

// the integer square root (no float call in an app file)
static inline int rl_isqrt(int v)
{
    if (v <= 0) {
        return 0;
    }
    int r = 0, b = 1 << 30;
    while (b > v) {
        b >>= 2;
    }
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

// the half chord of the circle at row y, m px inside the edge (0 out of the circle)
static inline int rl_half(int y, int m)
{
    const int r = gfx_w / 2 - m, dy = y - gfx_h / 2;
    if (r <= 0 || dy <= -r || dy >= r) {
        return 0;
    }
    return rl_isqrt(r * r - dy * dy);
}

// a row from y to y + h: its left edge and its width inside the circle (m px margin); false when it is too narrow
static inline bool rl_row(int y, int h, int m, int *x, int *w)
{
    const int dt = y - gfx_h / 2, db = y + h - gfx_h / 2;
    const int yy = (dt < 0 ? -dt : dt) > (db < 0 ? -db : db) ? y : y + h;
    const int hw = rl_half(yy, m);
    *x = gfx_w / 2 - hw;
    *w = 2 * hw;
    return hw > 20;
}

// the middle box of width w: x0 and the rows y0..y1 that stay m px inside the circle
static inline void rl_box(int w, int m, int *x0, int *y0, int *y1)
{
    const int r = gfx_w / 2 - m, hw = w / 2;
    const int dy = hw < r ? rl_isqrt(r * r - hw * hw) : 0;
    *x0 = gfx_w / 2 - hw;
    *y0 = gfx_h / 2 - dy;
    *y1 = gfx_h / 2 + dy;
}

// the background: the colour in the whole frame and a thin rim of the colour rim at the edge
static inline void rl_disc(uint16_t bg, uint16_t rim)
{
    gfx_fill(bg);
    gfx_ring(gfx_w / 2, gfx_h / 2, gfx_w / 2, gfx_w / 2 - 2, rim);
}

// the caps over a scrolled view: the colour bg above the row top and below the row bot, a thin line at each edge of
// the view and the rim again (the text and the round boxes of uikit do not obey gfx_clip_box(): draw the view first,
// then the caps, then the parts of the caps)
static inline void rl_caps(int top, int bot, uint16_t bg, uint16_t rim)
{
    gfx_fill_rect(0, 0, gfx_w, top, bg);
    gfx_fill_rect(0, bot, gfx_w, gfx_h - bot, bg);
    const int h0 = rl_half(top, 30), h1 = rl_half(bot, 30);
    gfx_fill_rect(gfx_w / 2 - h0, top - 1, 2 * h0, 1, rim);
    gfx_fill_rect(gfx_w / 2 - h1, bot, 2 * h1, 1, rim);
    gfx_ring(gfx_w / 2, gfx_h / 2, gfx_w / 2, gfx_w / 2 - 2, rim);
}

// the frame of an app that draws its flat layout in the middle box (rl_origin.h): the colour bg in the whole frame,
// the top cap above the row top in the colour panel with a line at its edge, the name of the app in it (font f)
static inline void rl_cap(int top, uint16_t bg, uint16_t panel, uint16_t line, const kfont_t *f, const char *name,
                          uint16_t fg)
{
    gfx_fill(bg);
    gfx_fill_rect(0, 0, gfx_w, top, panel);
    const int h = rl_half(top, 2);
    gfx_fill_rect(gfx_w / 2 - h, top - 1, 2 * h, 1, line);
    if (name) {
        ui_text_c(f, gfx_w / 2, top - 16, name, fg);
    }
}

// a scroll bar on the right arc (angles -a .. +a from the 3 o'clock point): at = the top of the thumb, sz = its size,
// al = its whole length (the units of ks_bar(): 0..len)
static inline void rl_bar(int at, int sz, int len, uint16_t track, uint16_t thumb)
{
    if (len <= 0) {
        return;
    }
    const float A = 0.62f;  // (about 36 degrees each side)
    const int cx = gfx_w / 2, cy = gfx_h / 2, r = gfx_w / 2 - 9;
    for (int k = 0; k <= 40; k++) {
        const float t = (float)k / 40.0f, a = -A + 2 * A * t;
        const int p = (int)(t * (float)len);
        const bool on = p >= at && p <= at + sz;
        const int x = cx + (int)(fcos(a) * (float)r), y = cy + (int)(fsin(a) * (float)r);
        gfx_fill_rect(x - (on ? 2 : 1), y - (on ? 2 : 1), on ? 5 : 2, on ? 5 : 2, on ? thumb : track);
    }
}

// a round key at (cx, cy) of radius r: a disc, a ring and a label of the font f in the middle
static inline void rl_btn(int cx, int cy, int r, const kfont_t *f, const char *label, uint16_t fill, uint16_t ring,
                          uint16_t col)
{
    ui_circle(cx, cy, r, fill, 256);
    ui_ring(cx, cy, r, 2, ring, 256);
    if (label && f) {
        ui_text_c(f, cx, cy + f->asc / 2 - 1, label, col);
    }
}

// a paragraph in the circle: the words go on centred lines, and the width of each line follows the chord at its row
// (m px margin). The base line of the first line is y, then each lh px; a '\n' starts a new line. It stops at the row
// ymax. It gives the base line after the last line.
static inline int rl_para(const kfont_t *f, const char *s, int y, int lh, int m, int ymax, uint16_t c)
{
    char line[160];
    while (*s && y <= ymax) {
        const int top = y - f->asc, w = 2 * rl_half(top < gfx_h / 2 ? top : y + 4, m);
        int n = 0, cut = 0;
        const char *p = s, *cut_p = s;
        while (*p && *p != '\n' && n < (int)sizeof line - 1) {
            line[n] = *p;
            line[n + 1] = 0;
            if (*p == ' ' && n > 0) {
                line[n] = 0;
                if (ui_text_w(f, line) > w && cut) {
                    break;
                }
                line[n] = ' ';
                cut = n;
                cut_p = p + 1;
            }
            n++;
            p++;
        }
        line[n] = 0;
        if (ui_text_w(f, line) > w && cut) {
            line[cut] = 0;
            p = cut_p;
        } else if (*p == '\n') {
            p++;
        }
        ui_text_c(f, gfx_w / 2, y, line, c);
        y += lh;
        s = p;
        while (*s == ' ') {
            s++;
        }
    }
    return y;
}

// true when (x, y) is on the round key at (cx, cy) of radius r (a margin of 8 px for the finger)
static inline bool rl_hit(int x, int y, int cx, int cy, int r)
{
    const int dx = x - cx, dy = y - cy, rr = r + 8;
    return dx * dx + dy * dy <= rr * rr;
}

// THE TOOL LAYOUT (MQTT, the hardware tools, HOME FRONT, DICTIONARY): the state in the top cap (0 .. rl_t_top()),
// the content in the middle band (rl_t_top() .. rl_t_bot()), one row of keys in the bottom cap (rl_t_key() ..
// rl_t_key() + rl_t_kh()). At 466: the cap 88 px, the band to 348, the keys 354 .. 405 (a touch target of 51 px).
static inline int rl_t_top(void)
{
    return gfx_w * 19 / 100;
}
static inline int rl_t_key(void)
{
    return gfx_h * 76 / 100;
}
static inline int rl_t_kh(void)
{
    return gfx_w * 11 / 100;
}
static inline int rl_t_bot(void)
{
    return rl_t_key() - 6;
}

// the key i of n in the bottom cap: its left edge and its width (the row follows the chord at 0.6 of its height;
// the round corners of the outer keys stay inside the circle)
static inline void rl_t_keyx(int i, int n, int *x, int *w)
{
    const int g = 6, hw = rl_half(rl_t_key() + rl_t_kh() * 6 / 10, 12);
    const int kw = (2 * hw - (n - 1) * g) / n;
    *x = gfx_w / 2 - hw + i * (kw + g);
    *w = kw;
}

// the background of the bottom cap: the colour panel under the row y0 and a line of the colour line at its edge
static inline void rl_t_bcap(int y0, uint16_t panel, uint16_t line)
{
    gfx_fill_rect(0, y0, gfx_w, gfx_h - y0, panel);
    const int h = rl_half(y0, 2);
    gfx_fill_rect(gfx_w / 2 - h, y0, 2 * h, 1, line);
}

// a row y .. y + h of a list in the band y0 .. y1: its left edge and its width follow the chord (m px margin) at the
// part of the row that shows (a row under a cap gets the width of the edge of the band)
static inline void rl_t_row(int y, int h, int y0, int y1, int m, int *x, int *w)
{
    int a = y < y0 ? y0 : y, b = y + h > y1 ? y1 : y + h;
    if (b < a + 1) {
        b = a + 1;
    }
    rl_row(a, b - a, m, x, w);
}
