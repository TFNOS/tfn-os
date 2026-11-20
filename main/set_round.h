#pragma once

// THE ROUND PAGES OF SETUP (BOARD_ROUND, the native round frame of APP_ROUND_HI: 466 x 466 on the AMOLED-1.75C, 412 on
// the Watcher). The pages of SETUP (WIFI, SAVED, QR, FRIENDS, NOTIFY, PUBLISHERS, POOL, TILT CALIB.) share one look:
// the title and a short line on the top cap, the content in the middle box (RB_X .. RB_X + RB_W, RB_Y .. RB_B), BACK
// on the bottom cap. RS() scales a value of the 466 frame to the frame now. Only the firmware pages include this file.
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "uikit.h"
#include "tfn/theme.h"
#include "set_text.h"  // (the text of the pages: anti-aliased faces in place of the dot font, on all boards)

// (the values are macros on all boards: a page of another board uses them only in a branch that is never taken)
#define RS(v)     ((v) * gfx_w / 466)
#define RB_X      RS(50)  // the middle box
#define RB_W      RS(366)
#define RB_Y      RS(104)
#define RB_B      RS(378)
#define RB_BACK_Y RS(388)  // BACK: the bottom cap
#define RB_BACK_H RS(50)
#define RB_BACK_W RS(160)

#if BOARD_ROUND
// the half width of the circle at the height y (0 out of the circle)
static inline int rr_half(int y)
{
    int r = gfx_w / 2, dy = y - gfx_h / 2;
    return dy * dy >= r * r ? 0 : (int)sqrtf((float)(r * r - dy * dy));
}

// the largest font of f, KF_TEXT, KF_LABEL that puts s in w px
static inline const kfont_t *rr_fit(const kfont_t *f, const char *s, int w)
{
    if (ui_text_w(f, s) <= w) {
        return f;
    }
    return ui_text_w(&KF_TEXT, s) <= w ? &KF_TEXT : &KF_LABEL;
}

// a line of text in the middle (base: the baseline), in the largest font that fits the circle there
static inline void rr_line(const kfont_t *f, int base, const char *s, uint16_t c)
{
    f = rr_fit(f, s, 2 * rr_half(base - f->asc) - RS(30));
    ui_text_c(f, gfx_w / 2, base, s, c);
}

// the title and the short line of the top cap
static inline void rr_head(const char *title, const char *sub, uint16_t tc, uint16_t sc)
{
    rr_line(&KF_BIG, RS(64), title, tc);
    if (sub && sub[0]) {
        rr_line(&KF_TEXT, RS(92), sub, sc);
    }
}

// a button: a box with soft corners and its word in the middle (the largest font that fits)
static inline void rr_btn(int x, int y, int w, int h, const char *t, uint16_t bg, uint16_t fg)
{
    ui_rrect(x, y, w, h, h / 4, bg);
    const kfont_t *f = rr_fit(&KF_TEXT, t, w - 10);
    ui_text_c(f, x + w / 2, y + h / 2 + f->asc / 2, t, fg);
}

// BACK on the bottom cap, and its zone
static inline void rr_back(const char *t, bool hot, uint16_t bg, uint16_t hot_bg, uint16_t fg, uint16_t hot_fg)
{
    rr_btn(gfx_w / 2 - RB_BACK_W / 2, RB_BACK_Y, RB_BACK_W, RB_BACK_H, t, hot ? hot_bg : bg, hot ? hot_fg : fg);
}
static inline bool rr_back_at(int x, int y)
{
    return y >= RB_BACK_Y - 8 && x >= gfx_w / 2 - RB_BACK_W / 2 - 10 && x < gfx_w / 2 + RB_BACK_W / 2 + 10;
}
#else
// (no round panel: no BACK on a cap)
static inline bool rr_back_at(int x, int y)
{
    (void)x;
    (void)y;
    return false;
}
#endif
