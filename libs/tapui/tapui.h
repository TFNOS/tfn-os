#pragma once
// TAPUI: the shared touch UI of the companion apps (COMPANIONS, REMOTE). One header with static functions: each app has
// its own copy (no ABI names, no new firmware).
//
// THE RULES:
// - Sizes in mm (BOARD_TOUCH_FRAME_UM of the board): a touch target is 9 mm or more where the room allows it, never
//   less than 6 mm; the BACK key is a wide key at the start of the title bar (the round frame: the whole top cap).
// - A grid FILLS its band: few items = big tiles; many items = the smallest good tiles and a momentum scroll.
// - The hit list: the drawing puts each target of the frame into a list; the input finds the target of the finger in
//   the list of the last frame (the layout lives in one place).
// - A long press on an item opens its context menu (a sheet of big rows; a tap outside closes it).
// - Hebrew: the frame is a mirror (BACK at the right, the text from the right).
// - THE SOUND OF EACH TOUCH (the boards have no vibration motor): a tap clicks, BACK and a close give the back sound,
//   a long press ticks (sfx_ui: SETUP > UI SOUNDS turns them off). An app that sounds its items itself (a key that
//   sends at the touch) sets tu_items_silent(true).
//
// THE USE: tu_init() at the enter; each render: tu_begin(nact?), the band (tu_grid_*, tu_row ...), tu_chrome(), the
// sheet (tu_menu_draw()), tu_end(); in input(): tu_input(ev) gives an event (TU_TAP, TU_LONG, TU_DOWN, TU_UP,
// TU_CANCEL) with the id of the target; in update(): tu_update(now) (the scroll moves).
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kscroll/kscroll.h"
#include "rndlay/rndlay.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/uikit.h"

// ---------------------------------------------------------------- the colours
#define TU_BG      rgb(11, 14, 22)
#define TU_BAR     rgb(20, 25, 38)
#define TU_SURF    rgb(30, 37, 56)
#define TU_SURF_HI rgb(52, 63, 92)
#define TU_LINE    rgb(60, 72, 102)
#define TU_TEXT    rgb(242, 245, 251)
#define TU_DIM     rgb(150, 160, 186)
#define TU_FAINT   rgb(96, 106, 132)
#define TU_ACC     rgb(70, 208, 174)
#define TU_PRI     rgb(58, 132, 236)
#define TU_WARN    rgb(246, 182, 64)
#define TU_BAD     rgb(236, 86, 76)
#define TU_HOT     rgb(255, 122, 58)
#define TU_SHADE   rgb(5, 7, 12)

// the size of mm in px of the logical frame
#define TU_MM(mm) ((int)((mm) * 1000.0f / (float)BOARD_TOUCH_FRAME_UM + 0.5f))

enum {
    TU_FK_BAR,
    TU_FK_BOX,
    TU_FK_ROUND
};
static struct {
    int fk;
    bool he;
    int top;                 // the height of the title bar (the round frame: the top cap)
    int bx0, bx1, by0, by1;  // the band
    int nact;                // the action keys in the foot (the round frame: in the bottom cap)
    int foot;                // the top of the foot (no foot: gfx_h)
    const kfont_t *f_title, *f_key, *f_big, *f_small, *f_tiny;
    int64_t now;
    // the toast
    char toast[48];
    uint16_t toast_c;
    int64_t toast_t;
} T_;

// ---------------------------------------------------------------- the hits
enum {
    TU_H_NONE,
    TU_H_BACK,
    TU_H_TOPKEY,
    TU_H_ACT,
    TU_H_ITEM,
    TU_H_SUB,
    TU_H_MENU,
    TU_H_SHADE
};
#define TU_ID(kind, i) ((kind) << 10 | (i))
#define TU_KIND(id)    ((id) >> 10)
#define TU_IDX(id)     ((id) & 0x3FF)
typedef struct {
    int16_t x, y, w, h;
    int id;
    bool band;  // a target of the band: only inside the band (the chrome covers the rest)
} tu_hit_t;
#define TU_NHIT 128
static tu_hit_t s_tu_hit[TU_NHIT], s_tu_new[TU_NHIT];
static int s_tu_nhit, s_tu_nnew;

static inline void tu_hit(int x, int y, int w, int h, int id, bool band)
{
    if (s_tu_nnew < TU_NHIT) {
        s_tu_new[s_tu_nnew++] = (tu_hit_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, id, band};
    }
}

static inline const tu_hit_t *tu_find(int x, int y)
{
    for (int i = s_tu_nhit - 1; i >= 0; i--) {
        const tu_hit_t *h = &s_tu_hit[i];
        if (h->band && (y < T_.by0 || y >= T_.by1)) {
            continue;
        }
        const int m = TU_MM(0.8f);  // (the edge of a finger)
        if (x >= h->x - m && x < h->x + h->w + m && y >= h->y - m && y < h->y + h->h + m) {
            return h;
        }
    }
    return NULL;
}

// the place of a target of the last frame (the tests tap it); false: not on the screen
static inline bool tu_target(int id, int *x, int *y)
{
    for (int i = s_tu_nhit - 1; i >= 0; i--) {
        if (s_tu_hit[i].id == id) {
            *x = s_tu_hit[i].x + s_tu_hit[i].w / 2;
            *y = s_tu_hit[i].y + s_tu_hit[i].h / 2;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- the scroll of the band
static kscroll_t s_tu_ks;
static float s_tu_content;  // the height of the content of the band now

static inline float tu_scroll(void)
{
    return ks_pos(&s_tu_ks);
}

static inline void tu_scroll_reset(void)
{
    ks_init(&s_tu_ks, 0, 1);
}

// ---------------------------------------------------------------- the text
static inline bool tu_has_he(const char *s)
{
    for (; *s; s++) {
        if ((uint8_t)*s == 0xD7) {
            return true;
        }
    }
    return false;
}

// one line in the width w: the font f, else the next smaller faces, else cut with ".." at a whole UTF-8 letter
static inline const kfont_t *tu_fit(const kfont_t *f, const char *s, int w, char *out, int max)
{
    snprintf(out, (size_t)max, "%s", s);
    const kfont_t *fs[4] = {f, T_.f_key, T_.f_small, T_.f_tiny};
    for (int k = 0; k < 4; k++) {
        if (fs[k]->asc <= f->asc && ui_text_w(fs[k], out) <= w) {
            return fs[k];
        }
    }
    const kfont_t *t = T_.f_tiny;
    int n = (int)strlen(out);
    while (n > 0) {
        n--;
        while (n > 0 && (out[n] & 0xC0) == 0x80) {
            n--;
        }
        char c = out[n];
        out[n] = 0;
        const int tw = ui_text_w(t, out) + ui_text_w(t, "..");
        out[n] = c;
        if (tw <= w) {
            break;
        }
    }
    snprintf(out + n, (size_t)(max - n), "..");
    return t;
}

// a line at the start of x .. x + w (the start: the left; Hebrew: the right)
static inline void tu_text(const kfont_t *f, int x, int w, int base, const char *s, uint16_t c)
{
    char b[96];
    f = tu_fit(f, s, w, b, sizeof b);
    const int tw = ui_text_w(f, b);
    const bool rtl = T_.he || tu_has_he(b);
    ui_text_c(f, rtl ? x + w - tw / 2 : x + tw / 2, base, b, c);
}

// a line at the end of x .. x + w (the right; Hebrew: the left)
static inline void tu_text_end(const kfont_t *f, int x, int w, int base, const char *s, uint16_t c)
{
    char b[96];
    f = tu_fit(f, s, w, b, sizeof b);
    const int tw = ui_text_w(f, b);
    ui_text_c(f, T_.he ? x + tw / 2 : x + w - tw / 2, base, b, c);
}

static inline void tu_text_c(const kfont_t *f, int cx, int w, int base, const char *s, uint16_t c)
{
    char b[96];
    f = tu_fit(f, s, w, b, sizeof b);
    ui_text_c(f, cx, base, b, c);
}

// a paragraph in x .. x + w from the top y (the lines are centred); it gives the y under the last line. With draw
// false it only measures (tu_para_h).
static inline int tu_para_do(const kfont_t *f, const char *s, int x, int w, int y, int ymax, uint16_t c, bool draw)
{
    char line[160];
    int base = y + f->asc;
    while (*s && base <= ymax) {
        int n = 0, cut = 0;
        const char *p = s, *cut_p = s;
        int lw = w;
        if (T_.fk == TU_FK_ROUND) {
            lw = 2 * rl_half(base - f->asc / 2, TU_MM(3)) < w ? 2 * rl_half(base - f->asc / 2, TU_MM(3)) : w;
        }
        while (*p && *p != '\n' && n < (int)sizeof line - 1) {
            line[n] = *p;
            line[n + 1] = 0;
            if (*p == ' ' && n > 0) {
                line[n] = 0;
                if (ui_text_w(f, line) > lw && cut) {
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
        if (ui_text_w(f, line) > lw && cut) {
            line[cut] = 0;
            p = cut_p;
        } else if (*p == '\n') {
            p++;
        }
        if (draw) {
            ui_text_c(f, x + w / 2, base, line, c);
        }
        base += f->line + 3;
        s = p;
        while (*s == ' ') {
            s++;
        }
    }
    return base - f->asc;
}

static inline int tu_para(const kfont_t *f, const char *s, int x, int w, int y, int ymax, uint16_t c)
{
    return tu_para_do(f, s, x, w, y, ymax, c, true);
}

// the height of the paragraph of tu_para from the top y (no drawing)
static inline int tu_para_h(const kfont_t *f, const char *s, int x, int w, int y)
{
    return tu_para_do(f, s, x, w, y, 1 << 20, 0, false) - y;
}

// ---------------------------------------------------------------- the frame
static inline void tu_init(bool he)
{
    T_.fk = gfx_round() ? TU_FK_ROUND : BOARD_BAR_SCREEN ? TU_FK_BAR : TU_FK_BOX;
    T_.he = he;
    const bool big = T_.fk == TU_FK_ROUND || gfx_h >= 360;
    T_.f_title = big ? &KF_TITLE : &KF_BIG;
    T_.f_big = &KF_BIG;
    T_.f_key = big ? &KF_BIG : &KF_TEXT;
    T_.f_small = big ? &KF_TEXT : &KF_NAME;
    T_.f_tiny = big ? &KF_NAME : &KF_LABEL;
    if (T_.fk == TU_FK_BAR) {
        T_.f_title = &KF_BIG;
    }
    if (T_.fk == TU_FK_ROUND) {
        T_.f_title = &KF_BIG;
    }
    s_tu_nhit = s_tu_nnew = 0;
    tu_scroll_reset();
}

static inline void tu_lang(bool he)
{
    T_.he = he;
}

// the band and the foot of a screen with nact action keys (0: the band to the bottom)
static inline void tu_begin(int nact, int64_t now)
{
    T_.now = now;
    T_.nact = nact;
    s_tu_nnew = 0;
    gfx_fill(TU_BG);
    if (T_.fk == TU_FK_ROUND) {
        T_.top = rl_t_top();
        T_.foot = nact ? rl_t_key() - 4 : gfx_h;
        T_.by0 = T_.top + 2;
        T_.by1 = nact ? T_.foot - 2 : gfx_h * 88 / 100;
        const int hw = rl_half(gfx_h / 2, TU_MM(3));
        T_.bx0 = gfx_w / 2 - hw;
        T_.bx1 = gfx_w / 2 + hw;
        return;
    }
    int top = TU_MM(T_.fk == TU_FK_BAR ? 5.8f : 6.5f);  // (the bar is 23 mm high: a lower title bar)
    if (top < 36) {
        top = 36;
    }
    if (top > 64) {
        top = 64;
    }
    T_.top = top;
    const int m = TU_MM(T_.fk == TU_FK_BAR ? 0.8f : 1.2f);
    T_.bx0 = m;
    T_.bx1 = gfx_w - m;
    T_.by0 = top + m;
    T_.foot = gfx_h;
    if (nact) {
        if (T_.fk == TU_FK_BAR) {
            const int cw = TU_MM(19);
            T_.foot = gfx_w - cw - m;  // (the bar: the keys in a column at the end)
            if (T_.he) {
                T_.bx0 = cw + 2 * m;
            } else {
                T_.bx1 = gfx_w - cw - 2 * m;
            }
        } else {
            T_.foot = gfx_h - TU_MM(7.5f) - 2 * m;
        }
    }
    T_.by1 = (nact && T_.fk != TU_FK_BAR) ? T_.foot - m : gfx_h - m;
}

// the left edge and the width of a row y .. y + h of the band (the round frame: the chord)
static inline void tu_row_x(int y, int h, int *x, int *w)
{
    if (T_.fk == TU_FK_ROUND) {
        rl_t_row(y, h, T_.by0, T_.by1, TU_MM(2.5f), x, w);
        return;
    }
    *x = T_.bx0;
    *w = T_.bx1 - T_.bx0;
}

// ---------------------------------------------------------------- the keys
enum {
    TU_K_NORMAL,
    TU_K_PRI,
    TU_K_ACC,
    TU_K_BAD,
    TU_K_OFF,
    TU_K_GHOST,
    TU_K_HOT,
    TU_K_WARN
};
#define TU_MORE "\x01"          // the label of the key of the title bar: three dots (a menu)
static int s_tu_down = -1;      // the target under the finger now (it lights)
static bool s_tu_items_silent;  // the app sounds the taps of its items itself
static bool s_tu_band_down;     // the touch now started in the band (the scrolled list)

static inline void tu_box(int x, int y, int w, int h, int style, bool down)
{
    uint16_t fill = TU_SURF, ring = TU_LINE;
    switch (style) {
    case TU_K_PRI:
        fill = TU_PRI;
        ring = rgb(120, 176, 250);
        break;
    case TU_K_ACC:
        fill = rgb(24, 98, 86);
        ring = TU_ACC;
        break;
    case TU_K_BAD:
        fill = rgb(92, 30, 30);
        ring = TU_BAD;
        break;
    case TU_K_OFF:
        fill = rgb(24, 28, 42);
        ring = rgb(40, 46, 64);
        break;
    case TU_K_GHOST:
        fill = TU_BG;
        ring = TU_LINE;
        break;
    case TU_K_HOT:
        fill = ui_mix(TU_SURF, TU_HOT, 130);
        ring = TU_HOT;
        break;
    case TU_K_WARN:
        fill = ui_mix(TU_SURF, TU_WARN, 70);
        ring = TU_WARN;
        break;
    default:
        break;
    }
    if (down && style != TU_K_OFF) {
        fill = ui_mix(fill, TU_TEXT, 50);
    }
    int r = h / 4;
    if (r > TU_MM(2.2f)) {
        r = TU_MM(2.2f);
    }
    ui_rbox(x, y, w, h, r, fill, ring, style == TU_K_GHOST ? 2 : 1);
}

static inline void tu_key(int x, int y, int w, int h, const char *label, int style, int id, bool band)
{
    tu_box(x, y, w, h, style, id >= 0 && s_tu_down == id);
    char b[64];
    const kfont_t *f = tu_fit(T_.f_key, label, w - TU_MM(2), b, sizeof b);
    ui_text_c(f, x + w / 2, y + h / 2 + f->asc / 2 - 1, b, style == TU_K_OFF ? TU_FAINT : TU_TEXT);
    if (id >= 0 && style != TU_K_OFF) {
        tu_hit(x, y, w, h, id, band);
    }
}

// the action key i of the foot
static inline void tu_act(int i, const char *label, int style)
{
    const int n = T_.nact, g = TU_MM(1.2f);
    int x, y, w, h;
    if (T_.fk == TU_FK_ROUND) {
        rl_t_keyx(i, n, &x, &w);
        y = rl_t_key();
        h = rl_t_kh();
    } else if (T_.fk == TU_FK_BAR) {
        const int top = T_.top + g, avail = gfx_h - top - g;
        h = (avail - (n - 1) * g) / n;
        w = TU_MM(19);
        x = T_.he ? g : gfx_w - w - g;
        y = top + i * (h + g);
    } else {
        h = TU_MM(7.5f);
        w = (gfx_w - 2 * g - (n - 1) * g) / n;
        const int k = T_.he ? n - 1 - i : i;
        x = g + k * (w + g);
        y = gfx_h - h - g;
    }
    tu_key(x, y, w, h, label, style, TU_ID(TU_H_ACT, i), false);
}

// AN EMPTY SCREEN: a text and one big key (the target id) in the band. The bar: the text and the key side by side (the
// key at the end; Hebrew: at the start). The box and the round frame: the key under the text, the two in the middle of
// the band. A text that does not fit takes the smaller face, then a lower key (7 mm: the AMOLED-1.8 has big mm), so the
// key always shows in full.
static inline void tu_empty(const char *say, const char *key, int id)
{
    int kh = TU_MM(T_.fk == TU_FK_BAR ? 11 : 9);
    const int gap = TU_MM(2);
    if (T_.fk == TU_FK_BAR) {
        const int kw = (T_.bx1 - T_.bx0) * 40 / 100;
        const int tx = T_.he ? T_.bx0 + kw + gap : T_.bx0;
        const int tw = T_.bx1 - T_.bx0 - kw - gap;
        const kfont_t *f = T_.f_small;
        int h = tu_para_h(f, say, tx, tw, T_.by0);
        if (h > T_.by1 - T_.by0) {
            f = T_.f_tiny;
            h = tu_para_h(f, say, tx, tw, T_.by0);
        }
        const int y = h < T_.by1 - T_.by0 ? (T_.by0 + T_.by1 - h) / 2 : T_.by0;
        tu_para(f, say, tx, tw, y, T_.by1, TU_DIM);
        tu_key(T_.he ? T_.bx0 : T_.bx1 - kw, (T_.by0 + T_.by1 - kh) / 2, kw, kh, key, TU_K_PRI, id, true);
        return;
    }
    int room = T_.by1 - T_.by0 - kh - gap - TU_MM(1);
    const kfont_t *f = T_.f_small;
    int h = tu_para_h(f, say, T_.bx0, T_.bx1 - T_.bx0, T_.by0);
    if (h > room) {
        f = T_.f_tiny;
        h = tu_para_h(f, say, T_.bx0, T_.bx1 - T_.bx0, T_.by0);
    }
    if (h > room) {
        room += kh - TU_MM(7);
        kh = TU_MM(7);
    }
    int y = T_.by0 + (room - h > 0 ? (room - h) / 2 : 0) + TU_MM(0.5f);
    y = tu_para(f, say, T_.bx0, T_.bx1 - T_.bx0, y, T_.by1 - kh - gap, TU_DIM) + gap;
    if (y + kh > T_.by1) {
        y = T_.by1 - kh;
    }
    int x, w;
    tu_row_x(y, kh, &x, &w);
    const int kw = w > TU_MM(70) ? TU_MM(70) : w;
    tu_key(x + (w - kw) / 2, y, kw, kh, key, TU_K_PRI, id, true);
}

static inline void tu_dot(int cx, int cy, int r, uint16_t c)
{
    ui_circle(cx, cy, r, c, 256);
}

// a chevron (BACK: to the start of the reading)
static inline void tu_chevron(int cx, int cy, int s, bool left, uint16_t c)
{
    const int t = s / 3 > 3 ? s / 3 : 3;
    for (int k = -s; k <= s; k++) {
        const int dx = (s - (k < 0 ? -k : k)) / 2;
        gfx_fill_rect(left ? cx - dx : cx + dx - t, cy + k, t, 1, c);
    }
}

// THE CHROME (after the band: it covers the rows that scroll out): the title, BACK, a key at the end of the title bar
// (top_label: NULL = none), a state with a dot (state: NULL = none), the place of the action keys
static inline void tu_chrome(const char *title, bool back, const char *top_label, int top_style, const char *state,
                             uint16_t state_c)
{
    if (T_.toast[0] && T_.now - T_.toast_t < 1800) {
        state = T_.toast;  // (the toast takes the place of the state)
        state_c = T_.toast_c;
    }
    if (T_.fk == TU_FK_ROUND) {
        gfx_fill_rect(0, 0, gfx_w, T_.top, TU_BAR);
        const int h = rl_half(T_.top, 2);
        gfx_fill_rect(gfx_w / 2 - h, T_.top - 1, 2 * h, 1, TU_LINE);
        gfx_fill_rect(0, T_.by1, gfx_w, gfx_h - T_.by1, TU_BG);
        if (T_.nact) {
            rl_t_bcap(rl_t_key() - 4, TU_BAR, TU_LINE);
        }
        gfx_ring(gfx_w / 2, gfx_h / 2, gfx_w / 2, gfx_w / 2 - 2, TU_LINE);
        const int base = T_.top - TU_MM(2);
        const int tw = gfx_w * 50 / 100;
        tu_text_c(T_.f_title, gfx_w / 2, tw, base, title, TU_TEXT);
        if (back) {
            char b[64];
            const kfont_t *f = tu_fit(T_.f_title, title, tw, b, sizeof b);
            const int w2 = ui_text_w(f, b) / 2 + TU_MM(3);
            tu_chevron(T_.he ? gfx_w / 2 + w2 : gfx_w / 2 - w2, base - f->asc / 2, TU_MM(1.6f), !T_.he, TU_DIM);
            tu_hit(0, 0, gfx_w, T_.top, TU_ID(TU_H_BACK, 0), false);
        }
        if (top_label) {
            // (the round frame: the key of the title bar goes under the title, at the top of the band)
        }
        if (state) {
            const int sw = ui_text_w(T_.f_tiny, state);
            tu_dot(gfx_w / 2 - sw / 2 - TU_MM(1.5f), base - T_.f_title->asc - TU_MM(2.2f), TU_MM(0.6f), state_c);
            ui_text_c(T_.f_tiny, gfx_w / 2 + TU_MM(0.5f), base - T_.f_title->asc - TU_MM(1.4f), state, state_c);
        }
        return;
    }
    gfx_fill_rect(0, 0, gfx_w, T_.top, TU_BAR);
    gfx_fill_rect(0, T_.top, gfx_w, 1, TU_LINE);
    if (T_.nact && T_.fk == TU_FK_BAR) {
        const int x = T_.he ? 0 : T_.bx1 + 1;
        const int w = T_.he ? T_.bx0 - 1 : gfx_w - T_.bx1 - 1;
        gfx_fill_rect(x, T_.top + 1, w, gfx_h - T_.top - 1, TU_BG);
    } else if (T_.nact) {
        gfx_fill_rect(0, T_.foot, gfx_w, gfx_h - T_.foot, TU_BG);
    } else {
        gfx_fill_rect(0, T_.by1, gfx_w, gfx_h - T_.by1, TU_BG);
    }
    const int g = TU_MM(0.9f);
    int x0 = TU_MM(2), x1 = gfx_w - TU_MM(2);  // (the room of the title)
    if (back) {
        const int bw = TU_MM(11), bh = T_.top - 2 * g;
        const int bx = T_.he ? gfx_w - bw - g : g;
        const int id = TU_ID(TU_H_BACK, 0);
        tu_box(bx, g, bw, bh, TU_K_NORMAL, s_tu_down == id);
        tu_chevron(bx + bw / 2 + (T_.he ? -bh / 10 : bh / 10), g + bh / 2, bh / 3, !T_.he, TU_TEXT);
        tu_hit(bx, 0, bw + g, T_.top, id, false);
        if (T_.he) {
            x1 = bx - TU_MM(1.2f);
        } else {
            x0 = bx + bw + TU_MM(1.2f);
        }
    }
    if (top_label) {
        // (TU_MORE: a narrow key with three dots)
        const bool more = top_label[0] == TU_MORE[0];
        int kw = ui_text_w(T_.f_key, top_label) + TU_MM(3);
        kw = more ? TU_MM(11) : kw > TU_MM(11) ? kw : TU_MM(11);
        const int kh = T_.top - 2 * g;
        const int kx = T_.he ? g : gfx_w - kw - g;
        if (more) {
            const int id = TU_ID(TU_H_TOPKEY, 0);
            tu_box(kx, g, kw, kh, top_style, s_tu_down == id);
            for (int k = -1; k <= 1; k++) {
                tu_dot(kx + kw / 2 + k * TU_MM(1.6f), g + kh / 2, TU_MM(0.55f) + 1, TU_TEXT);
            }
            tu_hit(kx, 0, kw + g, T_.top, id, false);
        } else {
            tu_key(kx, g, kw, kh, top_label, top_style, TU_ID(TU_H_TOPKEY, 0), false);
        }
        if (T_.he) {
            x0 = kx + kw + TU_MM(1.2f);
        } else {
            x1 = kx - TU_MM(1.2f);
        }
    }
    if (state && x1 - x0 - TU_MM(3) < ui_text_w(T_.f_small, title)) {
        state = NULL;  // (a narrow title bar: the whole room for the title)
    }
    if (state) {
        // (the title first: with no room for both, the state is its dot only)
        const int need = ui_text_w(T_.f_title, title) + TU_MM(2);
        int sw = ui_text_w(T_.f_tiny, state) + TU_MM(4);
        const bool text = x1 - x0 - sw >= need || x1 - x0 - sw >= TU_MM(28);
        if (!text) {
            sw = TU_MM(3);
        }
        const int sx = T_.he ? x0 : x1 - sw;
        tu_dot(T_.he ? sx + sw - TU_MM(1) : sx + TU_MM(1), T_.top / 2, TU_MM(0.8f), state_c);
        if (text) {
            ui_text_c(T_.f_tiny, T_.he ? sx + (sw - TU_MM(3)) / 2 : sx + TU_MM(3) + (sw - TU_MM(3)) / 2,
                      T_.top / 2 + T_.f_tiny->asc / 2 - 1, state, state_c);
        }
        if (T_.he) {
            x0 = sx + sw + TU_MM(1);
        } else {
            x1 = sx - TU_MM(1);
        }
    }
    tu_text(T_.f_title, x0, x1 - x0, T_.top / 2 + T_.f_title->asc / 2 - 1, title, TU_TEXT);
}

static inline void tu_toast(const char *s, uint16_t c)
{
    snprintf(T_.toast, sizeof T_.toast, "%s", s);
    T_.toast_c = c;
    T_.toast_t = T_.now;
}

static inline bool tu_toast_on(void)
{
    return T_.toast[0] && T_.now - T_.toast_t < 1800;
}

// ---------------------------------------------------------------- the grid (it fills the band)
typedef struct {
    int cols, rows;  // rows: all of them
    int cw, ch, gap;
    int top;  // the y of the first row (the scroll is in it)
} tu_grid_t;

// n tiles of at least min_w x min_h mm (at most max_h mm high): the columns and the rows fill the band
static inline tu_grid_t tu_grid(int n, float min_w, float min_h, float max_h, int y0)
{
    tu_grid_t g;
    g.gap = TU_MM(1.4f);
    int w = T_.bx1 - T_.bx0;
    if (T_.fk == TU_FK_ROUND) {
        w = 2 * rl_half(gfx_h / 2, TU_MM(4));
    }
    const int h = T_.by1 - y0;
    const int mw = TU_MM(min_w), mh = TU_MM(min_h), xh = TU_MM(max_h);
    g.cols = (w + g.gap) / (mw + g.gap);
    if (g.cols < 1) {
        g.cols = 1;
    }
    // (few items: fewer columns, so the tiles get wider)
    while (g.cols > 1 && (n + g.cols - 2) / (g.cols - 1) * (mh + g.gap) <= h && n <= (g.cols - 1) * 2) {
        g.cols--;
    }
    g.rows = n > 0 ? (n + g.cols - 1) / g.cols : 1;
    g.cw = (w - (g.cols - 1) * g.gap) / g.cols;
    g.ch = (h - (g.rows - 1) * g.gap) / g.rows;
    if (g.ch < mh) {
        g.ch = mh;
    }
    if (g.ch > xh) {
        g.ch = xh;
    }
    g.top = y0;
    s_tu_content = (float)(g.rows * (g.ch + g.gap) - g.gap + (y0 - T_.by0));
    return g;
}

// the place of the tile i; false: out of the band
static inline bool tu_cell(const tu_grid_t *g, int i, int *x, int *y, int *w, int *h)
{
    const int row = i / g->cols, col = i % g->cols;
    *y = g->top + row * (g->ch + g->gap) - (int)tu_scroll();
    *h = g->ch;
    int bx = T_.bx0, bw = T_.bx1 - T_.bx0;
    if (T_.fk == TU_FK_ROUND) {
        tu_row_x(*y, *h, &bx, &bw);
    }
    const int cw = (bw - (g->cols - 1) * g->gap) / g->cols;
    const int c = T_.he ? g->cols - 1 - col : col;
    *x = bx + c * (cw + g->gap);
    *w = cw;
    return *y + *h > T_.by0 && *y < T_.by1 && cw > TU_MM(4);
}

// ---------------------------------------------------------------- a card row of a list (a title, a line under it,
// a value at the end, a dot)
static inline int tu_row_h(void)
{
    return T_.fk == TU_FK_BAR ? TU_MM(9.5f) : TU_MM(11);
}

static inline void tu_card(int y, int h, const char *title, const char *sub, const char *end, uint16_t end_c,
                           uint16_t dot, int id, bool chevron)
{
    int x, w;
    tu_row_x(y, h, &x, &w);
    tu_box(x, y, w, h, TU_K_NORMAL, id >= 0 && s_tu_down == id);
    const int pad = TU_MM(2.5f);
    int tx = x + pad, tw = w - 2 * pad;
    if (dot) {
        const int dx = T_.he ? x + w - pad - TU_MM(0.8f) : x + pad + TU_MM(0.8f);
        tu_dot(dx, y + h / 2, TU_MM(0.9f), dot);
        tw -= TU_MM(3);
        if (!T_.he) {
            tx += TU_MM(3);
        }
    }
    if (chevron) {
        tu_chevron(T_.he ? x + pad + TU_MM(0.5f) : x + w - pad - TU_MM(0.5f), y + h / 2, TU_MM(1.2f), T_.he, TU_DIM);
        tw -= TU_MM(3);
        if (T_.he) {
            tx += TU_MM(3);
        }
    }
    if (end) {
        const int ew = ui_text_w(T_.f_small, end);
        tu_text_end(T_.f_small, tx, tw, y + h / 2 + T_.f_small->asc / 2 - 1, end, end_c);
        tw -= ew + TU_MM(2);
        if (T_.he) {
            tx += ew + TU_MM(2);
        }
    }
    if (sub) {
        tu_text(T_.f_key, tx, tw, y + h / 2 - TU_MM(0.4f), title, TU_TEXT);
        tu_text(T_.f_tiny, tx, tw, y + h / 2 + T_.f_tiny->asc + TU_MM(0.4f), sub, TU_DIM);
    } else {
        tu_text(T_.f_key, tx, tw, y + h / 2 + T_.f_key->asc / 2 - 1, title, TU_TEXT);
    }
    if (id >= 0) {
        tu_hit(x, y, w, h, id, true);
    }
}

// ---------------------------------------------------------------- the context menu (a sheet)
#define TU_MENU_MAX 6
static struct {
    bool on;
    int owner;  // the id of the item that opened it
    char title[40];
    int n;
    const char *label[TU_MENU_MAX];
    int style[TU_MENU_MAX];
    int64_t t0;
} s_tu_menu;

static inline void tu_menu_open(int owner, const char *title)
{
    memset(&s_tu_menu, 0, sizeof s_tu_menu);
    s_tu_menu.on = true;
    s_tu_menu.owner = owner;
    snprintf(s_tu_menu.title, sizeof s_tu_menu.title, "%s", title);
    s_tu_menu.t0 = T_.now;
}

static inline void tu_menu_add(const char *label, int style)
{
    if (s_tu_menu.n < TU_MENU_MAX) {
        s_tu_menu.label[s_tu_menu.n] = label;
        s_tu_menu.style[s_tu_menu.n++] = style;
    }
}

static inline bool tu_menu_on(void)
{
    return s_tu_menu.on;
}

static inline void tu_menu_draw(void)
{
    if (!s_tu_menu.on) {
        return;
    }
    gfx_darken_rect(0, 0, gfx_w, gfx_h, 2);
    tu_hit(0, 0, gfx_w, gfx_h, TU_ID(TU_H_SHADE, 0), false);
    const int n = s_tu_menu.n;
    const int g = TU_MM(1.2f), pad = TU_MM(1.5f);
    const int head = T_.f_small->line + TU_MM(1.5f);
    int w = T_.fk == TU_FK_ROUND ? gfx_w * 66 / 100 : TU_MM(64);
    if (w > gfx_w - 2 * pad) {
        w = gfx_w - 2 * pad;
    }
    int cols = 1;
    int rh = TU_MM(10);
    const int room = (T_.fk == TU_FK_ROUND ? gfx_h * 70 / 100 : gfx_h - 2 * pad) - head - pad;
    if (T_.fk == TU_FK_BAR) {
        cols = n <= 3 ? n : 3;  // (the bar: side by side, two rows at most)
        w = gfx_w - 2 * pad;
    } else if (n * (rh + g) > room) {
        cols = 2;
        w = T_.fk == TU_FK_ROUND ? gfx_w * 74 / 100 : gfx_w - 2 * pad;
    }
    int rows = (n + cols - 1) / cols;
    if (rows * (rh + g) > room) {
        rh = room / rows - g;
    }
    const int h = head + rows * (rh + g) + pad;
    const int x = (gfx_w - w) / 2, y = (gfx_h - h) / 2;
    ui_rbox(x - 1, y - 1, w + 2, h + 2, TU_MM(3), TU_BAR, TU_LINE, 1);
    tu_text_c(T_.f_small, gfx_w / 2, w - 2 * pad, y + TU_MM(1.2f) + T_.f_small->asc, s_tu_menu.title, TU_DIM);
    const int kw = (w - 2 * pad - (cols - 1) * g) / cols;
    for (int i = 0; i < n; i++) {
        const int r = i / cols, c = T_.he ? cols - 1 - i % cols : i % cols;
        tu_key(x + pad + c * (kw + g), y + head + r * (rh + g), kw, rh, s_tu_menu.label[i], s_tu_menu.style[i],
               TU_ID(TU_H_MENU, i), false);
    }
}

// ---------------------------------------------------------------- the input
enum {
    TU_E_NONE,
    TU_E_TAP,
    TU_E_LONG,
    TU_E_DOWN,
    TU_E_UP,
    TU_E_CANCEL,
    TU_E_MENU
};
typedef struct {
    int kind;  // TU_E_*
    int id;    // the target (TU_E_MENU: the row of the menu; owner = the item of the menu)
    int owner;
} tu_ev_t;

// the app sounds the taps of its items (TU_H_ITEM) itself: true / false
static inline void tu_items_silent(bool on)
{
    s_tu_items_silent = on;
}

static inline void tu_tap_sound(int id)
{
    if (TU_KIND(id) == TU_H_BACK) {
        sfx_ui(SFX_BACK);
    } else if (TU_KIND(id) != TU_H_ITEM || !s_tu_items_silent) {
        sfx_ui(SFX_CLICK);
    }
}

static inline tu_ev_t tu_input(const input_event_t *ev)
{
    tu_ev_t r = {TU_E_NONE, -1, -1};
    const tu_hit_t *h;
    switch (ev->kind) {
    case IN_DOWN:
        h = tu_find(ev->x, ev->y);
        s_tu_down = h ? h->id : -1;
        s_tu_band_down = !s_tu_menu.on && ev->y >= T_.by0 && ev->y < T_.by1;
        if (s_tu_band_down) {
            ks_down(&s_tu_ks, (float)ev->y, ev->t_ms);
        }
        r.kind = TU_E_DOWN;
        r.id = s_tu_down;
        break;
    case IN_MOVE:
        if (!s_tu_menu.on && ks_move(&s_tu_ks, (float)ev->y, ev->t_ms)) {
            input_tap_cancel();
            if (s_tu_down >= 0) {
                r.kind = TU_E_CANCEL;
                r.id = s_tu_down;
            }
            s_tu_down = -1;
        }
        break;
    case IN_UP:
        ks_up(&s_tu_ks, ev->t_ms);
        r.kind = TU_E_UP;
        r.id = s_tu_down;
        s_tu_down = -1;
        break;
    case IN_LONG:
        h = tu_find(ev->x, ev->y);
        if (h && !s_tu_menu.on) {
            r.kind = TU_E_LONG;
            r.id = h->id;
            if (TU_KIND(h->id) != TU_H_ITEM || !s_tu_items_silent) {
                sfx_ui(SFX_TICK);
            }
        }
        break;
    case IN_TAP:
        // (a touch that stopped a glide of the list is no tap; a touch outside the list (BACK, the keys of the foot,
        // the menu) always is: the mark of the last touch in the list does not stay on them)
        if (s_tu_band_down && !ks_tap_ok(&s_tu_ks)) {
            break;
        }
        h = tu_find(ev->x, ev->y);
        if (s_tu_menu.on) {
            if (h && TU_KIND(h->id) == TU_H_MENU) {
                r.kind = TU_E_MENU;
                r.id = TU_IDX(h->id);
                r.owner = s_tu_menu.owner;
                sfx_ui(SFX_CLICK);
            } else {
                sfx_ui(SFX_BACK);
            }
            s_tu_menu.on = false;  // (a tap outside closes it)
            break;
        }
        if (h) {
            r.kind = TU_E_TAP;
            r.id = h->id;
            tu_tap_sound(h->id);
        }
        break;
    default:
        break;
    }
    return r;
}

// each update: the momentum of the scroll; true = it moves
static inline bool tu_update(int64_t now)
{
    T_.now = now;
    const float mx = s_tu_content - (float)(T_.by1 - T_.by0);
    ks_set_view(&s_tu_ks, (float)(T_.by1 - T_.by0));
    ks_set_max(&s_tu_ks, mx > 0 ? mx : 0);
    return ks_update(&s_tu_ks, now);
}

// the end of a render: the hit list of this frame, and the scroll bar
static inline void tu_end(void)
{
    int at, sz, a;
    const int h = T_.by1 - T_.by0;
    if (ks_bar(&s_tu_ks, h, &at, &sz, &a) && a > 10 && !s_tu_menu.on) {
        if (T_.fk == TU_FK_ROUND) {
            rl_bar(at, sz, h, TU_SURF, ui_mix(TU_BG, TU_DIM, a > 255 ? 255 : a));
        } else {
            const int x = T_.he ? T_.bx0 - TU_MM(0.9f) : T_.bx1 + TU_MM(0.2f);
            ui_rrect(x, T_.by0 + at, TU_MM(0.7f), sz, TU_MM(0.35f), ui_mix(TU_BG, TU_DIM, a > 255 ? 255 : a));
        }
    }
    memcpy(s_tu_hit, s_tu_new, sizeof(tu_hit_t) * (size_t)s_tu_nnew);
    s_tu_nhit = s_tu_nnew;
}
