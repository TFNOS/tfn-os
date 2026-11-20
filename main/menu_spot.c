// The menu "Spotlight": one item at a time in the light (all the screens but the round panel and the Tab5).
#include "app_menu_priv.h"

// The menu "Spotlight" (all the screens but the round panel).
//
// One item at a time in the light: big, animated, with a soft ring, a glint on its glass and a pool of
// light under it; its neighbours wait smaller and dimmer at both sides (above and below on the tall
// 172 x 640 portrait). The room takes the colour of the group. The name is in the marquee face
// (Bungee), the group, the place in the list and NEW in Rubik.
// Touch: drag or flick the row (a flick goes on with inertia); a tap on a side item brings it to the
// middle; a tap on the middle item opens it; a tap on the empty sides steps once; a hold on the middle
// app = favorite on / off. The tilt of the board moves the glint and the room a little.

#include "mfont.h"  // the Bungee faces (menu_font.inc, one shared copy in mfont.c)
#include "tfn/theme.h"

// ---------------------------------------------------------------- the boxes drawn on the room
// Each drawing marks its box. The frame buffers take turns (gfx_swap): when a buffer comes back, only
// the boxes that the last frame drew into it are copied again from the room (a full copy costs about
// 10 ms on the 3.49 and 20 ms on the CoreS3).
#define DR_MAX 40
typedef struct {
    int16_t x0, y0, x1, y1;
} drect_t;
static struct {
    uint16_t *fb;
    int n;
    bool full;
    drect_t r[DR_MAX];
} s_drawn[2];
static drect_t s_mk[DR_MAX];
static int s_nmk;
static bool s_mk_full;
static void mark(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > gfx_w) {
        x1 = gfx_w;
    }
    if (y1 > gfx_h) {
        y1 = gfx_h;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    if (s_nmk >= DR_MAX) {
        s_mk_full = true;
        return;
    }
    s_mk[s_nmk++] = (drect_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
}
void men_drawn_reset(void)
{
    s_drawn[0].fb = s_drawn[1].fb = NULL;
}

// ---------------------------------------------------------------- colours (native RGB565) and text
static const mglyph_t *mg(const mfont_t *f, int cp)
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
static int mtext_w_(const mfont_t *f, const char *s, int spacing)
{
    int w4 = 0, n = 0;
    for (; *s; s++, n++) {
        const mglyph_t *g = mg(f, (uint8_t)*s);
        if (g) {
            w4 += g->adv4;
        }
    }
    return (w4 + 2) / 4 + (n > 1 ? (n - 1) * spacing : 0);
}
// a line of text; x is its left end, base its baseline; spacing: extra pixels between the letters
int men_mtext_w(const mfont_t *f, const char *s, int spacing)
{
    return mtext_w_(f, s, spacing);
}
void men_mtext(const mfont_t *f, int x, int base, const char *s, uint16_t col, int alpha, int spacing)
{
    mark(x - 2, base - f->asc - 3, x + men_mtext_w(f, s, spacing) + 3, base - f->asc + f->line + 3);
    float fx = (float)x;
    for (; *s; s++) {
        const mglyph_t *g = mg(f, (uint8_t)*s);
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
            for (int k = 0; k < g->w; k++) {
                int idx = j * g->w + k, v = (idx & 1) ? src[idx >> 1] >> 4 : src[idx >> 1] & 15;
                if (v) {
                    pix(gx + k, y, col, v * 17 * alpha / 255);
                }
            }
        }
        fx += g->adv4 / 4.0f + spacing;
    }
}
void men_mtext_c(const mfont_t *f, int cx, int base, const char *s, uint16_t col, int alpha, int spacing)
{
    men_mtext(f, cx - men_mtext_w(f, s, spacing) / 2, base, s, col, alpha, spacing);
}
// the biggest name face that fits in the width w
static const mfont_t *name_font(const char *s, int w, bool big)
{
    static const mfont_t *const F[5] = {&MF_N34, &MF_N28, &MF_N24, &MF_N20, &MF_N16};
    for (int i = big ? 0 : 1; i < 5; i++) {
        if (men_mtext_w(F[i], s, 0) <= w) {
            return F[i];
        }
    }
    return &MF_N16;
}

// ---------------------------------------------------------------- shapes
static uint16_t bg_at(int x, int y);
// the part (0..255) of the pixel (px, py) inside a rounded rectangle (an exact edge: anti-aliasing)
int men_rr_cover(int px, int py, int x, int y, int w, int h, int r)
{
    float fx = px + 0.5f, fy = py + 0.5f, cx, cy;
    if (fx < x + r) {
        cx = (float)(x + r);
    } else if (fx > x + w - r) {
        cx = (float)(x + w - r);
    } else {
        return 255;
    }
    if (fy < y + r) {
        cy = (float)(y + r);
    } else if (fy > y + h - r) {
        cy = (float)(y + h - r);
    } else {
        return 255;
    }
    float d = sqrtf((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)), c = r - d + 0.5f;
    return c >= 1 ? 255 : c <= 0 ? 0 : (int)(c * 255);
}
void men_rr_fill(int x, int y, int w, int h, int r, uint16_t c, int a)
{
    mark(x, y, x + w, y + h);
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (j >= r && j < h - r) {
            if (a >= 255) {
                gfx_fill_rect(x, py, w, 1, lcd(c));
            } else {
                for (int i = 0; i < w; i++) {
                    pix(x + i, py, c, a);
                }
            }
            continue;
        }
        for (int i = 0; i < w; i++) {
            int cov = (i < r || i >= w - r) ? men_rr_cover(x + i, py, x, y, w, h, r) : 255;
            if (cov) {
                pix(x + i, py, c, cov * a / 255);
            }
        }
    }
}
// the corners of a square item outside its rounded shape: the room again (or the colour of the ring),
// with a soft edge
static void rr_corners(int x, int y, int s, int r, bool ring, uint16_t col)
{
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            int xs[2] = {x + i, x + s - 1 - i}, ys[2] = {y + j, y + s - 1 - j};
            for (int a = 0; a < 2; a++) {
                for (int b = 0; b < 2; b++) {
                    int px = xs[a], py = ys[b];
                    if ((unsigned)px >= (unsigned)gfx_w || (unsigned)py >= (unsigned)gfx_h) {
                        continue;
                    }
                    int cov = men_rr_cover(px, py, x, y, s, s, r);
                    if (cov >= 255) {
                        continue;
                    }
                    uint16_t *q = &g_fb[py * gfx_w + px];
                    // (the ring: its own rounded edge is 3 px further out, on the same centres)
                    uint16_t to = ring ? nmix(lcd(bg_at(px, py)), col,
                                              men_rr_cover(px, py, x - 3, y - 3, s + 6, s + 6, r + 3))
                                       : lcd(bg_at(px, py));
                    *q = lcd(cov <= 0 ? to : nmix(to, lcd(*q), cov));
                }
            }
        }
    }
}
// a soft light (or shadow) ellipse: col toward the pixels with the strength amax at the middle
void men_soft_ellipse(int cx, int cy, int rx, int ry, uint16_t col, int amax)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    mark(cx - rx, cy - ry, cx + rx + 1, cy + ry + 1);
    for (int y = cy - ry; y <= cy + ry; y++) {
        float dy = (float)(y - cy) / ry;
        if (dy * dy >= 1) {
            continue;
        }
        for (int x = cx - rx; x <= cx + rx; x++) {
            float dx = (float)(x - cx) / rx, d = dx * dx + dy * dy;
            if (d >= 1) {
                continue;
            }
            float q = 1 - d;
            pix(x, y, col, (int)(amax * q * q));
        }
    }
}
// the item as further away: darker (a shift is fast; the blend only near the middle, while it moves)
static void dim_item(int x, int y, int s, float d, uint16_t to)
{
    if (d < 0.98f) {
        int a5 = ((int)(70 + d * 70) + 4) >> 3, x0 = x < 0 ? 0 : x, x1 = x + s > gfx_w ? gfx_w : x + s;
        uint32_t ss = ((to | ((uint32_t)to << 16)) & 0x07E0F81F) * (uint32_t)a5;
        for (int j = y < 0 ? -y : 0; j < s && y + j < gfx_h; j++) {
            uint16_t *p = &g_fb[(y + j) * gfx_w + x0];
            for (int i = x0; i < x1; i++, p++) {
                uint16_t dn = lcd(*p);
                uint32_t dd = (dn | ((uint32_t)dn << 16)) & 0x07E0F81F;
                uint32_t r = ((dd * (uint32_t)(32 - a5) + ss) >> 5) & 0x07E0F81F;
                *p = lcd((uint16_t)(r | (r >> 16)));
            }
        }
        return;
    }
    gfx_darken_rect(x, y, s, s, d < 1.9f ? 1 : 2);
}
// the glint on the glass of the middle item: a soft band across it; g = its place 0..1
static void glint(int x, int y, int s, float g)
{
    float c = x + s * (g * 1.4f - 0.2f);
    for (int j = 0; j < s; j++) {
        float lx = c - (j - s / 2.0f) * 0.55f;  // the band leans
        int x0 = (int)(lx - s * 0.16f), x1 = (int)(lx + s * 0.16f);
        for (int i = x0; i <= x1; i++) {
            if (i < x || i >= x + s) {
                continue;
            }
            float u = 1 - fabsf((i - lx) / (s * 0.16f));
            pix(i, y + j, lcd(THEME_MENU_GLINT), (int)(70 * u * u));
        }
    }
}

// ---------------------------------------------------------------- the layout of the screen now
enum {
    SL_BAR,
    SL_STD,
    SL_TALL
};
typedef struct {
    int mode;          // SL_BAR: 640 x 172 (a panel at the left); SL_STD; SL_TALL: 172 x 640
    bool vert;         // the row goes down (SL_TALL)
    int cx, cy, big;   // the middle item: its centre and its size
    float gap1, gap2;  // the step to the next item, and after it
    int panel;         // SL_BAR: the width of the panel of the words
} spot_t;
static spot_t spot_lay(void)
{
    spot_t l = {0};
    if (gfx_w >= gfx_h * 2.4f) {
        l.mode = SL_BAR;
        l.panel = 176;
        l.big = gfx_h - 60 < 116 ? gfx_h - 60 : 116;
        l.cx = l.panel + (gfx_w - l.panel) / 2;
        l.cy = gfx_h / 2 - 6;
    } else if (gfx_h >= gfx_w * 2.4f) {
        l.mode = SL_TALL;
        l.vert = true;
        l.big = gfx_w - 44 < 128 ? gfx_w - 44 : 128;
        l.cx = gfx_w / 2;
        l.cy = (int)(gfx_h * 0.31f);
    } else {
        l.mode = SL_STD;
#if BOARD_UI_TABLET
        // the Tab5 (640 x 360 and 360 x 640): a bigger middle item. Upright, it takes 70 % of the width and sits
        // lower, so the words below it fill the bottom half too.
        bool up = gfx_h > gfx_w;
        float b = up ? gfx_w * 0.70f : gfx_h * 0.56f;
        l.big = (int)b;
        l.cx = gfx_w / 2;
        l.cy = (int)(gfx_h * (up ? 0.44f : 0.42f));
#else
        float b = gfx_h * 0.52f;
        if (b > gfx_w * 0.5f) {
            b = gfx_w * 0.5f;
        }
        if (b > 160) {
            b = 160;
        }
        l.big = (int)b;
        l.cx = gfx_w / 2;
        l.cy = (int)(gfx_h * 0.43f);
#endif
    }
    l.gap1 = l.big * (l.mode == SL_BAR ? 0.88f : 0.84f);
    l.gap2 = l.big * (l.mode == SL_BAR ? 0.66f : 0.62f);
    return l;
}
// the place of item i (the top-left corner) and its size, at the row place now
static void spot_item(const spot_t *l, int i, float pos, int *x, int *y, int *s, float *dist)
{
    float d = i - pos, ad = fabsf(d);
    float off = ad < 1 ? ad * l->gap1 : l->gap1 + (ad - 1) * l->gap2;
    float sc = 1 - 0.36f * fminf(ad, 1) - 0.14f * fminf(fmaxf(ad - 1, 0), 1);
    int sz = (int)(l->big * sc);
    float sign = d < 0 ? -1.0f : 1.0f, drop = (1 - sc) * l->big * 0.18f;
    if (l->vert) {
        *x = l->cx - sz / 2 + (int)drop;
        *y = l->cy - sz / 2 + (int)(sign * off);
    } else {
        *x = l->cx - sz / 2 + (int)(sign * off);
        *y = l->cy - sz / 2 + (int)drop;
    }
    *s = sz;
    *dist = ad;
}

// ---------------------------------------------------------------- the room: a cached background
// The gradient of the group, three soft lights, the pool of light under the middle item, the floor
// line and (the 3.49 bar) the shade of the panel of the words: made once for a group and a layout, at a
// quarter of the resolution and then scaled (it is all soft: about 15 ms), and copied each frame.
// The rooms that are kept use at most ROOM_BUDGET of PSRAM (enough for 24 looks; a room is 215 KB on the 3.49,
// 150 KB on the CoreS3, 322 KB on the AMOLED): the room that was used the longest time ago goes first, never the
// room in use. A room that must come back costs one slow frame.
#define ROOM_BUDGET (1300 * 1024)
static void bg_build(int cat);
static uint16_t *s_bgc;  // the room now: LCD byte order, gfx_w x gfx_h
int men_bgw, men_bgh, men_bg_key = -1;
static uint16_t *s_room[ST_N];                   // the room of each group look (made for the screen layout s_room_lay)
static uint32_t s_room_use[ST_N], s_room_clock;  // the last use of each room
static int s_room_lay = -1;
static int lay_key(void)
{
    return gfx_w * 1009 + gfx_h;
}
void men_rooms_free(void)
{
    for (int i = 0; i < ST_N; i++) {
        free(s_room[i]);
        s_room[i] = NULL;
    }
    s_bgc = NULL;
    men_bg_key = -1;
}
// the most rooms in the budget (at least 2: the room in use and the next one)
static int room_max(void)
{
    int m = ROOM_BUDGET / (gfx_w * gfx_h * 2);
    return m < 2 ? 2 : m;
}
static int rooms_n(void)
{
    int n = 0;
    for (int i = 0; i < ST_N; i++) {
        n += s_room[i] != NULL;
    }
    return n;
}
// the room of a group look (ST_*), made now if it is missing. make_space: when the budget is full, the oldest room
// goes (never the room in use); else (the pre-build of the top level) nothing is made when the budget is full.
static uint16_t *room_get(int cat, bool make_space)
{
    if (s_room_lay != lay_key()) {
        men_rooms_free();
        s_room_lay = lay_key();
    }
    if (cat < 0 || cat >= ST_N) {
        cat = ST_GAMES;
    }
    if (!s_room[cat]) {
        while (rooms_n() >= room_max()) {
            if (!make_space) {
                return NULL;
            }
            int old = -1;
            for (int i = 0; i < ST_N; i++) {
                if (s_room[i] && s_room[i] != s_bgc && (old < 0 || s_room_use[i] < s_room_use[old])) {
                    old = i;
                }
            }
            if (old < 0) {
                break;
            }
            free(s_room[old]);
            s_room[old] = NULL;
        }
        s_room[cat] = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM);
        if (!s_room[cat] && make_space) {
            // (no PSRAM: the other rooms go, then one more try)
            for (int i = 0; i < ST_N; i++) {
                if (s_room[i] && s_room[i] != s_bgc) {
                    free(s_room[i]);
                    s_room[i] = NULL;
                }
            }
            s_room[cat] = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM);
        }
        if (s_room[cat]) {
            uint16_t *keep = s_bgc;
            s_bgc = s_room[cat];
            bg_build(cat);
            s_bgc = keep;
        }
    }
    if (s_room[cat]) {
        s_room_use[cat] = ++s_room_clock;
    }
    return s_room[cat];
}
static void bg_build(int cat)
{
    int w = gfx_w, h = gfx_h;
    if (!s_bgc) {
        return;
    }
    spot_t l = spot_lay();
    uint8_t a[3], b[3];
    men_cat_colors(cat, a, b);
    float top[3], bot[3], lit[3], pool[3];
    for (int i = 0; i < 3; i++) {
        top[i] = b[i] * 0.75f + (i == 2 ? 20 : 10) * 0.25f;
        bot[i] = b[i] * 0.3f;
        lit[i] = a[i] * 0.85f + 255 * 0.15f;
        pool[i] = (a[i] + 255 * 2) / 3.0f;
    }
    float ox[3], oy[3], orr[3];
    for (int k = 0; k < 3; k++) {
        ox[k] = w * (0.18f + 0.32f * k);
        oy[k] = h * (k == 1 ? 0.62f : 0.32f);
        orr[k] = (h < w ? h : w) * (0.95f + 0.2f * k);
    }
    int fy = l.cy + l.big / 2 + (l.mode == SL_BAR ? 6 : 10);
    // the colours on a grid of 4 px
    const int Q = 4;
    int gw = w / Q + 2, gh = h / Q + 2;
    uint8_t *grid = heap_caps_malloc((size_t)gw * gh * 3, MALLOC_CAP_SPIRAM);
    if (!grid) {
        return;
    }
    for (int gy = 0; gy < gh; gy++) {
        float Y = (float)gy * Q, u = Y / (h - 1);
        if (u > 1) {
            u = 1;
        }
        for (int gx = 0; gx < gw; gx++) {
            float X = (float)gx * Q, f = 0, c[3];
            for (int k = 0; k < 3; k++) {
                float dx = (X - ox[k]) / orr[k], dy = (Y - oy[k]) / orr[k], d = dx * dx + dy * dy;
                if (d < 1) {
                    float q = 1 - d;
                    f += q * q * 0.3f;
                }
            }
            if (f > 0.6f) {
                f = 0.6f;
            }
            for (int i = 0; i < 3; i++) {
                float base = top[i] + (bot[i] - top[i]) * u;
                c[i] = base + (lit[i] - base) * f;
            }
            // the pool of light under the middle item (a disc behind it on the tall portrait)
            float pd, pa;
            if (!l.vert) {
                float dx = (X - l.cx) / (l.big * 1.25f), dy = (Y - fy) / (l.big * 0.26f);
                pd = dx * dx + dy * dy;
                pa = 0.43f;
            } else {
                float dx = (X - l.cx) / (l.big * 0.95f), dy = (Y - l.cy) / (l.big * 0.95f);
                pd = dx * dx + dy * dy;
                pa = 0.24f;
            }
            if (pd < 1) {
                float q = (1 - pd) * (1 - pd) * pa;
                for (int i = 0; i < 3; i++) {
                    c[i] += (pool[i] - c[i]) * q;
                }
            }
            uint8_t *o = &grid[(gy * gw + gx) * 3];
            for (int i = 0; i < 3; i++) {
                o[i] = (uint8_t)(c[i] > 255 ? 255 : c[i] < 0 ? 0 : c[i]);
            }
        }
    }
    // scale up (bilinear), with a small dither: no bands in the soft gradient
    for (int y = 0; y < h; y++) {
        int gy = y / Q, fyq = y % Q;
        uint16_t *row = &s_bgc[y * w];
        for (int x = 0; x < w; x++) {
            int gx = x / Q, fxq = x % Q;
            const uint8_t *p00 = &grid[(gy * gw + gx) * 3], *p10 = p00 + 3, *p01 = p00 + gw * 3, *p11 = p01 + 3;
            int v[3];
            for (int i = 0; i < 3; i++) {
                int t0 = p00[i] * (Q - fxq) + p10[i] * fxq, t1 = p01[i] * (Q - fxq) + p11[i] * fxq;
                v[i] = (t0 * (Q - fyq) + t1 * fyq) / (Q * Q) + ((x * 7 + y * 13) & 3) - 1;
                if (v[i] < 0) {
                    v[i] = 0;
                }
                if (v[i] > 255) {
                    v[i] = 255;
                }
            }
            row[x] = lcd(nat(v[0], v[1], v[2]));
        }
    }
    free(grid);
    // the sharp parts: the floor line, the shade of the panel
    if (!l.vert) {
        int x0 = l.mode == SL_BAR ? l.panel + 4 : 0;
        for (int x = x0; x < w; x++) {
            s_bgc[fy * w + x] = lcd(nmix(lcd(s_bgc[fy * w + x]), lcd(THEME_MENU_FLOOR), 18));
        }
    }
    if (l.mode == SL_BAR) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < l.panel; x++) {
                s_bgc[y * w + x] = lcd(nmix(lcd(s_bgc[y * w + x]), 0, 90));
            }
        }
    }
}
static void bg_blit(void)
{
    if (!s_bgc || men_bgw != gfx_w || men_bgh != gfx_h) {
        gfx_fill(THEME_MENU_BG);
        return;
    }
    memcpy(g_fb, s_bgc, (size_t)gfx_w * gfx_h * 2);
}
// The room back into the boxes of a frame: each row copies the union of the boxes once (the boxes overlap,
// the shadow, the ring and the neighbours; on the CoreS3 a copy from PSRAM costs about 0.3 us a pixel, and the boxes
// have 1.45 x the pixels of their union). ex: a box that this frame paints opaque (no copy there), or NULL.
static void bg_boxes(const drect_t *r, int n, const drect_t *ex)
{
    int y0 = gfx_h, y1 = 0;
    for (int k = 0; k < n; k++) {
        if (r[k].y0 < y0) {
            y0 = r[k].y0;
        }
        if (r[k].y1 > y1) {
            y1 = r[k].y1;
        }
    }
    for (int y = y0; y < y1; y++) {
        int16_t a[DR_MAX], b[DR_MAX];
        int m = 0;
        for (int k = 0; k < n; k++) {
            if (y < r[k].y0 || y >= r[k].y1) {
                continue;
            }
            int j = m++;
            while (j > 0 && a[j - 1] > r[k].x0) {  // (sorted by the left edge)
                a[j] = a[j - 1];
                b[j] = b[j - 1];
                j--;
            }
            a[j] = r[k].x0;
            b[j] = r[k].x1;
        }
        const bool cut = ex && y >= ex->y0 && y < ex->y1;
        for (int i = 0; i < m;) {
            int x0 = a[i], x1 = b[i];
            for (i++; i < m && a[i] <= x1; i++) {
                if (b[i] > x1) {
                    x1 = b[i];
                }
            }
            if (cut && x0 < ex->x1 && x1 > ex->x0) {
                // (the parts at the two sides of the box ex)
                if (x0 < ex->x0) {
                    memcpy(&g_fb[y * gfx_w + x0], &s_bgc[y * gfx_w + x0], (size_t)(ex->x0 - x0) * 2);
                }
                if (x1 > ex->x1) {
                    memcpy(&g_fb[y * gfx_w + ex->x1], &s_bgc[y * gfx_w + ex->x1], (size_t)(x1 - ex->x1) * 2);
                }
                continue;
            }
            memcpy(&g_fb[y * gfx_w + x0], &s_bgc[y * gfx_w + x0], (size_t)(x1 - x0) * 2);
        }
    }
}
static uint16_t bg_at(int x, int y)
{
    if (!s_bgc || (unsigned)x >= (unsigned)men_bgw || (unsigned)y >= (unsigned)men_bgh) {
        return THEME_MENU_BG;
    }
    return s_bgc[y * men_bgw + x];
}

int men_level_slot(void)
{
    return men_level < 0 ? G_MAX + 1 : men_level;
}
void men_spot_clamp(void)
{
    if (men_sel > men_n_items - 1) {
        men_sel = men_n_items - 1;
    }
    if (men_sel < 0) {
        men_sel = 0;
    }
}
// the selection of a level after a change: the one it had; a group starts on its first app (not BACK)
void men_spot_level_changed(void)
{
    int k = men_level_slot();
    men_sel = men_sel_of[k];
    if (men_level >= 0 && men_sel == 0 && men_n_items > 1) {
        men_sel = 1;
    }
    men_spot_clamp();
    men_pos = (float)men_sel;
    men_vel = 0;
    men_touch = men_drag = false;
    men_pressed = -1;
}
// the group look whose colours the room has now
static int room_cat(void)
{
    if (men_level >= 0) {
        return men_gstyle(men_level);
    }
    int i = (int)lroundf(men_pos);
    if (i < 0) {
        i = 0;
    }
    if (i >= men_n_items) {
        i = men_n_items - 1;
    }
    return men_n_items > 0 && men_items[i].kind == IT_CAT ? men_gstyle(men_items[i].cat) : ST_GAMES;
}
// the item under a point, -1: none. First an item whose own box holds the point (the middle one first); then the
// 6 px around the boxes, where a side item comes before the middle one (a tap that misses a
// side tile by a little selects it; it does not open the middle item)
static int spot_hit(int px, int py)
{
    spot_t l = spot_lay();
    int mid = (int)lroundf(men_pos);
    for (int pass = 0; pass < 2; pass++) {
        int m = pass ? 6 : 0, best = -1;
        float bd = 1e9f;
        for (int i = 0; i < men_n_items; i++) {
            int x, y, s;
            float d;
            spot_item(&l, i, men_pos, &x, &y, &s, &d);
            if (d > 2.6f) {
                continue;
            }
            if (px < x - m || px >= x + s + m || py < y - m || py >= y + s + m) {
                continue;
            }
            float k = pass && i == mid ? d + 100 : d;  // (in the margins the middle item comes last)
            if (k < bd) {
                bd = k;
                best = i;
            }
        }
        if (best >= 0) {
            return best;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- input and update
// the next (dir 1) or the last (dir -1) group that has apps, among the groups next to this one (the same parent)
static int next_group(int dir)
{
    int par = men_gparent(men_level), order[G_MAX + 1], n = 0;
    for (int k = 0; k < men_lay.ne && n <= G_MAX; k++) {
        const lent_t *e = &men_lay.e[k];
        if (e->parent != par) {
            continue;
        }
        if (e->kind == E_GRP && men_cat_count[e->ref] > 0) {
            order[n++] = e->ref;
        } else if (e->kind == E_FAV && par < 0 && men_lay.fav == FAVM_GROUP && men_cat_count[LV_FAV] > 0) {
            order[n++] = LV_FAV;
        }
    }
    for (int k = 0; k < n; k++) {
        if (order[k] == men_level) {
            return order[(k + dir + n) % n];
        }
    }
    return n ? order[0] : men_level;
}
// a swipe up or down can start here (in a group only)
static bool vsw_zone(const spot_t *l, int x)
{
    if (men_level < 0) {
        return false;
    }
    if (l->mode == SL_BAR) {
        return x < l->panel;
    }
    return l->mode == SL_STD;
}
void men_spot_input(const input_event_t *ev)
{
    spot_t l = spot_lay();
    int a = l.vert ? ev->y : ev->x;
    switch (ev->kind) {
    case IN_DOWN:
        men_touch = true;
        men_drag = false;
        men_hold_done = false;
        men_a0 = men_last_a = a;
        men_last_t = ev->t_ms;
        men_pos0 = men_pos;
        men_vel = 0;
        men_pressed = spot_hit(ev->x, ev->y);
        men_down_t = ev->t_ms;
        men_x0 = ev->x;
        men_y0 = ev->y;
        men_vsw = false;
        break;
    case IN_MOVE:
        // up or down more than along the row: a swipe to the next group
        if (!men_drag && !men_vsw && !men_hold_done && vsw_zone(&l, men_x0) && abs(ev->y - men_y0) > DRAG_PX &&
            abs(ev->y - men_y0) > abs(ev->x - men_x0) * 3 / 2) {
            men_vsw = true;
            men_pressed = -1;
            input_tap_cancel();
        }
        if (men_vsw) {
            break;
        }
        if (!men_drag && !men_hold_done && abs(a - men_a0) > DRAG_PX) {
            men_drag = true;
            men_pressed = -1;
            input_tap_cancel();
        }
        if (men_drag) {
            float raw = men_pos0 - (a - men_a0) / l.gap1;
            // a rubber band past the first and the last item
            if (raw < 0) {
                raw *= 0.35f;
            }
            if (raw > men_n_items - 1) {
                raw = (men_n_items - 1) + (raw - (men_n_items - 1)) * 0.35f;
            }
            men_pos = raw;
            int64_t dt = ev->t_ms - men_last_t;
            if (dt > 0) {
                float v = -(float)(a - men_last_a) / l.gap1 * 1000.0f / (float)dt;
                men_vel = 0.6f * men_vel + 0.4f * v;
            }
            men_last_a = a;
            men_last_t = ev->t_ms;
        }
        break;
    case IN_UP:
        men_touch = false;
        if (men_vsw) {
            int dy = ev->y - men_y0;
            if (abs(dy) >= 28) {
                men_vsw_dir = dy < 0 ? 1 : -1;
                int g = next_group(men_vsw_dir);
                if (g != men_level) {
                    sfx_ui(SFX_TICK);
                    men_go_level(g);
                    men_vsw_at = ev->t_ms;
                } else {
                    men_bump_at = ev->t_ms;
                    sfx_tone(240, 190, 30, W_PULSE, 22);
                }  // no other group here: a bump, a low tick
            }
            men_vsw = false;
            men_hold_done = true;  // (no tap after the swipe)
            men_pressed = -1;
            break;
        }
        if (men_drag) {
            if (ev->t_ms - men_last_t > 80) {
                men_vel = 0;  // the finger stopped before it went up
            }
            // a flick goes on: about a quarter of a second of its speed
            men_sel = (int)lroundf(men_pos + men_vel * 0.28f);
            men_spot_clamp();
        }
        men_drag = false;
        men_pressed = -1;
        break;
    case IN_TAP: {
        if (men_hold_done) {
            break;
        }
        int i = spot_hit(ev->x, ev->y);
        int mid = (int)lroundf(men_pos);
        if (i >= 0 && i == mid) {
            men_open_item(i);
        } else if (i >= 0) {
            men_sel = i;
            sfx_ui(SFX_TICK);
        } else {
            // the empty sides: one step that way
            int p = l.vert ? ev->y : ev->x, c = l.vert ? l.cy : l.cx;
            men_sel = mid + (p < c ? -1 : 1);
            men_spot_clamp();
        }
        break;
    }
    default:
        break;
    }
}
void men_spot_update(int64_t now, float dt)
{
    if (gfx_w != men_was_w || gfx_h != men_was_h) {
        men_was_w = gfx_w;
        men_was_h = gfx_h;
        men_bg_key = -1;
    }
    // (one missing room in a frame, only while the row is still and no finger is down: the group nearest to the middle
    // first; it stops when the budget of the rooms is full)
    static int64_t s_still_t;
    if (men_touch || fabsf(men_pos - (float)men_sel) > 0.004f) {
        s_still_t = now;
    } else if (now - s_still_t > 150 && men_level < 0 && men_bg_key >= 0 && rooms_n() < room_max()) {
        int best = -1;
        for (int i = 0; i < men_n_items; i++) {
            if (men_items[i].kind == IT_CAT && !s_room[men_gstyle(men_items[i].cat)] &&
                (best < 0 || abs(i - men_sel) < abs(best - men_sel))) {
                best = i;
            }
        }
        if (best >= 0) {
            room_get(men_gstyle(men_items[best].cat), false);
        }
    }
    // no SETUP in the layout: a hold of 8 s on an empty place of the top level opens it (a way back to SETUP)
    if (men_touch && !men_drag && !men_vsw && !men_hold_done && men_pressed < 0 && men_level < 0 &&
        now - men_down_t >= SECRET_MS && !men_setup_in_menu()) {
        men_hold_done = true;
        sfx_ui(SFX_CLICK);
        app_open(&app_settings);
        return;
    }
    // HOME 2.0 (menu_home.c): a hold on an app or a group opens its sheet; a hold on the background, the edit mode
    if (men_touch && !men_drag && !men_vsw && !men_hold_done && now - men_down_t >= HOLD_MS) {
        if (men_pressed >= 0 && men_pressed < men_n_items) {
            int i = men_pressed;
            if (men_home_hold_item(i, men_level)) {
                men_hold_done = true;
                men_sel = i;
                men_pos = (float)i;
                men_pressed = -1;
                return;
            }
        } else {
            men_hold_done = true;
            men_home_hold_bg(men_down_t, men_x0, men_y0);
            return;
        }
    }
    if (!men_drag) {
        float k = 1 - expf(-dt * 11);
        men_pos += (men_sel - men_pos) * k;
        if (fabsf(men_sel - men_pos) < 0.002f) {
            men_pos = (float)men_sel;
        }
    }
    int mid = (int)lroundf(men_pos);
    if (mid != men_shown) {
        if (men_shown >= 0 && mid >= 0 && mid < men_n_items) {
            sfx_ui(SFX_TICK);
        }
        men_shown = mid;
    }
    men_sel_of[men_level_slot()] = men_sel;
    // the tilt: only its change shows (a slow base follows the hand), so a board on a table is still
    float tx = 0, ty = 0;
    imu_tilt_logical(&tx, &ty);
    men_tilt_base += (tx - men_tilt_base) * (1 - expf(-dt / 1.6f));
    float tl = (tx - men_tilt_base) * 9;
    men_tilt += (fmaxf(-1, fminf(1, tl)) - men_tilt) * (1 - expf(-dt / 0.1f));
    men_glow_t += dt;
    (void)now;
}

// ---------------------------------------------------------------- drawing
// a group tile (or BACK: the look of the level that it goes to): a rounded gradient square, the symbol, the count;
// live: the middle tile (the symbol moves)
void men_group_tile(int cat, int x, int y, int s, bool back, bool live)
{
    uint8_t a[3], b[3];
    int st = back ? men_gstyle_up(cat) : men_gstyle(cat);
    men_cat_colors(st, a, b);
    for (int k = 0; k < s; k += 2) {
        gfx_fill_rect(x, y + k, s, s - k < 2 ? s - k : 2, men_grad(a, b, k, s));
    }
    men_tile_set(a, b, y, s, 2);
    uint16_t mid = men_grad(a, b, s / 2, s);
    if (back) {
        float cx = x + s * 0.5f, cy = y + s * 0.46f, u = s * 0.3f;
        float p[14] = {
                cx - u,         cy,     cx - u * 0.1f,  cy - u * 0.85f, cx - u * 0.1f,  cy - u * 0.35f, cx + u,
                cy - u * 0.35f, cx + u, cy + u * 0.35f, cx - u * 0.1f,  cy + u * 0.35f, cx - u * 0.1f,  cy + u * 0.85f};
        men_fill_poly(p, 7, rgb(236, 238, 248));
    } else {
        int ss = s * 62 / 100;
        gfx_clip_box(x, y, x + s, y + s);
        men_cat_symbol(st, x + (s - ss) / 2, y + (s - ss) / 2 - s / 14, ss, mid, live ? icon_time() : -1);
        gfx_clip_box_off();
        char t[8];
        snprintf(t, sizeof(t), "%d", men_cat_count[cat]);
        const mfont_t *f = s >= 90 ? &MF_LABEL : &MF_SMALL;
        men_mtext(f, x + s - men_mtext_w(f, t, 0) - s / 14, y + s - s / 14, t, lcd(THEME_MENU_TILE_COUNT), 255, 0);
    }
}
// the title of a level in the menu face, and the width of its place (level_path)
#define TITLE_SP 3             // the spacing of its letters
#define BUMP_MS  260           // the bump of the words
static int s_fit_w, s_fit_sp;  // (level_title sets them before each use)
#if BOARD_UI_TABLET
#define SP_TF MF_LABEL  // the Tab5: the face of the title, the place and the count (a bigger frame pixel)
#else
#define SP_TF MF_SMALL
#endif
static bool title_fits(const char *s)
{
    return men_mtext_w(&SP_TF, s, s_fit_sp) <= s_fit_w;
}
// the title of the level now in o, and the spacing of its letters (less when it is too wide)
static int level_title(char *o, int n)
{
    s_fit_sp = TITLE_SP;
    men_level_path(men_level, false, o, n, title_fits);
    if (!title_fits(o)) {
        s_fit_sp = 1;
        men_level_path(men_level, false, o, n, title_fits);
    }
    return s_fit_sp;
}
// the title of the level now for the layout l (its spacing; s_fit_w = the width of its place); show_idx: the place in
// the list ("02 / 14", idx) stays beside it (a long path on a narrow screen takes its room: the bar at the bottom
// shows the place too)
static bool fits_all(const char *s)
{
    (void)s;
    return true;
}
static int spot_title(const spot_t *l, const char *idx, char *o, int n, bool *show_idx)
{
    s_fit_w = l->mode == SL_BAR    ? l->panel - 24
              : l->mode == SL_TALL ? gfx_w - 16
                                   : gfx_w - 92 - men_mtext_w(&SP_TF, idx, 1);
    *show_idx = true;
    if (l->mode == SL_STD) {
        men_level_path(men_level, false, o, n, fits_all);  // (the whole path)
        s_fit_sp = TITLE_SP;
        if (!title_fits(o)) {
            s_fit_w = gfx_w - 84;
            *show_idx = false;
        }
    }
    return level_title(o, n);
}
bool men_is_new(const app_t *a)
{
    if (men_builtin_new(a)) {
        return true;
    }
    return men_tfn_new(tfn_entry_of(a));  // (an installed app: in apps_all.inc as new, or new=1 in its manifest)
}
const char *men_item_name(const item_t *it)
{
    return it->kind == IT_APP ? it->app->name : it->kind == IT_BACK ? "BACK" : men_gname(it->cat);
}
// a small yellow pill with a word (NEW)
static void pill(int x, int y, const char *t)
{
    int w = men_mtext_w(&MF_TINY, t, 1) + 14;
    men_rr_fill(x, y, w, 15, 7, lcd(THEME_MENU_PILL), 255);
    men_mtext(&MF_TINY, x + 7, y + 11, t, lcd(THEME_MENU_PILL_TEXT), 255, 1);
}
// the battery and the WiFi signal, with their box; returns their left edge
static int bat(int x, int y)
{
    mark(x - 90, y - 2, x + 1, y + 14);
    return men_draw_status(x, y);
}
static int s_last_slot;
#ifndef ESP_PLATFORM
int g_menu_dtest;  // host tests only (w49_menu/dirty_test)
int g_menu_level(void)
{
    return men_level;
}
#endif
static void spot_render_body(int64_t now);
static void spot_render_inner(int64_t now);
void men_spot_render(int64_t now)
{
    spot_render_body(now);
#ifndef ESP_PLATFORM
    if (g_menu_dtest == 2) {
        return;
    }
#endif
    // keep the boxes of this frame for the next use of this buffer
    int slot = s_drawn[0].fb == g_fb ? 0 : s_drawn[1].fb == g_fb ? 1 : 1 - s_last_slot;
    s_drawn[slot].fb = g_fb;
    s_drawn[slot].n = s_nmk;
    s_drawn[slot].full = s_mk_full;
    memcpy(s_drawn[slot].r, s_mk, sizeof(drect_t) * (size_t)s_nmk);
    s_last_slot = slot;
}
// the room behind the items: a new room for a new look or frame size, then a full copy or only the boxes that the last
// frame of this buffer drew
static void spot_room(const spot_t *l, int cat)
{
    int key = cat * 1000003 + gfx_w * 1009 + gfx_h;
    if (men_bg_key != key) {
        s_bgc = room_get(cat, true);
        men_bgw = gfx_w;
        men_bgh = gfx_h;
        men_bg_key = key;
        men_drawn_reset();
    }
    int slot = s_drawn[0].fb == g_fb ? 0 : s_drawn[1].fb == g_fb ? 1 : -1;
    // (a full copy now and then: the overlays of main.c, such as WEB LISTENS, draw on the frame too;
    // and always on the host, where the fuzz fills the buffer before each frame)
    static int s_full_n;
#ifdef ESP_PLATFORM
    bool full_now = ++s_full_n >= 50;
#else
    bool full_now = g_menu_dtest != 1;
#endif
    if (full_now) {
        s_full_n = 0;
    }
    // the middle band of the light square of the middle item: rr_fill() paints it opaque later in this frame (all
    // that the far items draw there before goes under it), so the room needs no copy there
    drect_t ex = {0, 0, 0, 0};
    {
        int m = (int)lroundf(men_pos), x, y, sz;
        float d;
        if (m >= 0 && m < men_n_items) {
            spot_item(l, m, men_pos, &x, &y, &sz, &d);
            x += (int)(men_tilt * 6 * (1 - fminf(d, 1) * 0.5f));  // (as in the loop of the items below)
            bool shown = !(x + sz < -4 || x > gfx_w + 4 || y + sz < -4 || y > gfx_h + 4 || sz < 8) &&
                         !(l->vert && m > men_pos + 1.15f) && !(l->mode == SL_BAR && x < l->panel - sz / 2);
            if (shown && d < 0.5f) {
                int R = sz / 8 + 2 + 3;  // (the corner of the light square: rr_fill blends it)
                ex = (drect_t){(int16_t)(x - 3 < 0 ? 0 : x - 3), (int16_t)(y - 3 + R < 0 ? 0 : y - 3 + R),
                               (int16_t)(x + sz + 3 > gfx_w ? gfx_w : x + sz + 3),
                               (int16_t)(y + sz + 3 - R > gfx_h ? gfx_h : y + sz + 3 - R)};
            }
        }
    }
    if (slot < 0 || full_now || s_drawn[slot].full || !s_bgc || men_bgw != gfx_w || men_bgh != gfx_h) {
        bg_blit();
    } else {
        bg_boxes(s_drawn[slot].r, s_drawn[slot].n, ex.x1 > ex.x0 && ex.y1 > ex.y0 ? &ex : NULL);
    }
    s_nmk = 0;
    s_mk_full = false;
}
// the items, the far ones first (light: the light square of the middle item, dark: the dim of the others, fy: the floor,
// t: the time of the glint); returns the middle item
static int spot_items(const spot_t *l, int64_t now, uint16_t light, uint16_t dark, int fy, float t)
{
    // the items: the far ones first
    int order[64], n = 0;
    for (int i = 0; i < men_n_items && n < 64; i++) {
        if (fabsf(i - men_pos) < 3.6f) {
            order[n++] = i;
        }
    }
    for (int a = 1; a < n; a++) {
        int v = order[a], j = a - 1;
        while (j >= 0 && fabsf(order[j] - men_pos) < fabsf(v - men_pos)) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    int mid = (int)lroundf(men_pos);
    for (int k = 0; k < n; k++) {
        int i = order[k], x, y, s;
        float d;
        spot_item(l, i, men_pos, &x, &y, &s, &d);
        x += (int)(men_tilt * 6 * (1 - fminf(d, 1) * 0.5f));
        if (x + s < -4 || x > gfx_w + 4 || y + s < -4 || y > gfx_h + 4 || s < 8) {
            continue;
        }
        if (l->vert && i > men_pos + 1.15f) {
            continue;  // (the words are below the next item)
        }
        if (l->mode == SL_BAR && x < l->panel - s / 2) {
            continue;  // (not under the panel of the words)
        }
        const item_t *it = &men_items[i];
        bool focus = i == mid && d < 0.5f;
        mark(x - 4, y - 4, x + s + 4, y + s + 4);
        int r = s / 8 + 2;
        // its shadow on the floor
        if (!l->vert) {
            men_soft_ellipse(x + s / 2, fy, (int)(s * 0.42f), (int)(s * 0.07f) + 2, lcd(THEME_MENU_SHADOW), 120);
        }
        if (focus) {
            men_rr_fill(x - 3, y - 3, s + 6, s + 6, r + 3, light, 255);
        }
        if (it->kind == IT_APP) {
            gfx_clip_box(x, y, x + s, y + s);  // (some icons have a fixed size and draw outside)
            it->app->draw_icon(x, y, s);
            gfx_clip_box_off();
            gfx_clip_reset();
            men_badge(it->app, x, y, s);
        } else {
            men_group_tile(it->cat, x, y, s, it->kind == IT_BACK, focus);
        }
        if (focus) {
            glint(x, y, s, 0.5f + men_tilt * 0.45f + sinf(t * 0.7f) * 0.25f);
            if (men_pressed == i) {
                gfx_darken_rect(x, y, s, s, 1);
            }
            rr_corners(x, y, s, r, true, light);
        } else {
            dim_item(x, y, s, d, dark);
            rr_corners(x, y, s, r, false, 0);
        }
        if (it->kind == IT_APP && men_fav_index(it->app) >= 0) {
            int st = s / 9 + 4;
            men_star(x + s - st - 2, y + st + 2, st + 3, THEME_MENU_FAV_EDGE);
            men_star(x + s - st - 2, y + st + 2, st, THEME_MENU_FAV);
        }
        // the hold to change the favorite: a bar fills along the bottom of the middle app
        if (men_pressed == i && (it->kind == IT_APP || (it->kind == IT_CAT && it->cat != LV_FAV)) && men_touch &&
            !men_drag && !men_hold_done) {
            int64_t held = now - men_down_t;
            if (held >= HOLD_SHOW_MS) {
                int w = (int)((held - HOLD_SHOW_MS) * s / (HOLD_MS - HOLD_SHOW_MS));
                if (w > s) {
                    w = s;
                }
                gfx_fill_rect(x, y + s - 7, s, 7, THEME_MENU_HOLD_TRACK);
                gfx_fill_rect(x, y + s - 7, w, 7, THEME_MENU_HOLD_FILL);
            }
        }
    }
    return mid;
}
// the words: the title, the name of the middle item, its index and count, the battery, the place bar, the message
static void spot_words(const spot_t *l, int64_t now, int mid, const uint8_t *ca)
{
    // the words
    if (men_n_items == 0) {
        men_mtext_c(&MF_N20, gfx_w / 2, gfx_h / 2, "NO APPS", lcd(THEME_MENU_TEXT), 255, 0);
        return;
    }
    if (mid < 0) {
        mid = 0;
    }
    if (mid >= men_n_items) {
        mid = men_n_items - 1;
    }
    const item_t *it = &men_items[mid];
    const char *name = men_item_name(it);
    char idx[16];
    snprintf(idx, sizeof idx, "%02d / %02d", mid + 1, men_n_items);
    // the title: the path of the level ("GAMES / QUESTS"), in the width of its place
    char label[96];
    bool show_idx;
    int tsp = spot_title(l, idx, label, sizeof label, &show_idx);
    uint16_t lab = nat((ca[0] + 255) / 2, (ca[1] + 255) / 2, (ca[2] + 255) / 2), white = lcd(THEME_MENU_TEXT),
             soft = lcd(THEME_MENU_TEXT_SOFT);
    bool nw = it->kind == IT_APP && men_is_new(it->app);
    char cnt[24] = "";
    if (it->kind == IT_CAT) {
        snprintf(cnt, sizeof cnt, men_cat_count[it->cat] == 1 ? "%d APP" : "%d APPS", men_cat_count[it->cat]);
    }
    if (it->kind == IT_BACK) {
        men_back_to(cnt, sizeof cnt);
    }
    float prog = men_n_items > 1 ? men_pos / (men_n_items - 1) : 0;
    // a swipe that found no other group: the words go a little that way and come back
    int bo = 0;
    if (men_bump_at && now - men_bump_at < BUMP_MS) {
        bo = (int)lroundf(-men_vsw_dir * 7 * sinf((float)(now - men_bump_at) / BUMP_MS * 3.14159265f));
    }
    if (l->mode == SL_BAR) {
        // the panel of the words at the left (after a swipe to another group, they slide in)
        int po = bo;
        if (now - men_vsw_at < 220) {
            float k = 1 - (float)(now - men_vsw_at) / 220;
            po += (int)(men_vsw_dir * 26 * k * k);
        }
        men_mtext(&MF_SMALL, 16, 30 + po, label, lab, 255, tsp);
        const mfont_t *f = name_font(name, l->panel - 26, false);
        men_mtext(f, 15, 84 + po, name, white, 255, 0);
        men_mtext(&MF_SMALL, 16, 108 + po, idx, soft, 200, 1);
        if (nw) {
            pill(16, 120 + po, "NEW");
        } else if (cnt[0]) {
            men_mtext(&MF_SMALL, 16, 132 + po, cnt, soft, 220, 1);
        }
        if (it->kind == IT_APP && !nw) {
            men_mtext(&MF_TINY, 16, gfx_h - 12, "HOLD: OPTIONS", soft, 110, 1);
        }
        bat(l->panel - 8, gfx_h - 22);
        int x0 = l->panel + 20, x1 = gfx_w - 20;
        for (int x = x0; x < x1; x++) {
            pix(x, gfx_h - 10, white, 34);
            pix(x, gfx_h - 9, white, 34);
        }
        mark(x0, gfx_h - 11, x1, gfx_h - 7);
        int m = x0 + (int)((x1 - x0) * prog);
        men_rr_fill(m - 10, gfx_h - 11, 20, 4, 2, lab, 255);
    } else if (l->mode == SL_STD) {
        men_mtext(&SP_TF, 12, 20 + bo, label, lab, 255, tsp);
        int bl = bat(gfx_w - 4, 8);
        if (bl > gfx_w - 56) {
            bl = gfx_w - 56;  // (the place of the index as before, also with no battery)
        }
        if (show_idx) {
            men_mtext(&SP_TF, bl - 10 - men_mtext_w(&SP_TF, idx, 1), 18, idx, soft, 200, 1);
        }
#if BOARD_UI_TABLET
        const mfont_t *f = name_font(name, gfx_w - 24, true);
        int base = l->cy + l->big / 2 + f->asc + 26;
        const int cgap = 24;  // (the count is below the bigger name)
#else
        const mfont_t *f = name_font(name, gfx_w - 24, gfx_w > 400);
        int base = l->cy + l->big / 2 + f->asc + (gfx_h > 300 ? 26 : 18);
        const int cgap = 18;
#endif
        if (base > gfx_h - 26) {
            base = gfx_h - 26;
        }
        men_mtext_c(f, gfx_w / 2, base + bo, name, white, 255, 0);
        if (nw) {
            pill(gfx_w / 2 - 20, base + 6 + bo, "NEW");
        } else if (cnt[0]) {
            men_mtext_c(&SP_TF, gfx_w / 2, base + cgap + bo, cnt, soft, 220, 1);
        }
        int x0 = gfx_w / 8, x1 = gfx_w - gfx_w / 8;
        for (int x = x0; x < x1; x++) {
            pix(x, gfx_h - 5, white, 34);
        }
        mark(x0, gfx_h - 7, x1, gfx_h - 1);
        int m = x0 + (int)((x1 - x0) * prog);
        men_rr_fill(m - 8, gfx_h - 6, 16, 4, 2, lab, 255);
    } else {
        men_mtext_c(&MF_SMALL, gfx_w / 2, 22, label, lab, 255, tsp);
        bat(gfx_w - 4, gfx_h - 20);
        const mfont_t *f = name_font(name, gfx_w - 16, false);
        int base = l->cy + (int)l->gap1 + (int)(l->big * 0.32f) + 34 + f->asc;
        if (base > gfx_h - 90) {
            base = gfx_h - 90;
        }
        men_mtext_c(f, gfx_w / 2, base, name, white, 255, 0);
        men_mtext_c(&MF_SMALL, gfx_w / 2, base + 22, idx, soft, 200, 1);
        if (nw) {
            pill(gfx_w / 2 - 20, base + 32, "NEW");
        } else if (cnt[0]) {
            men_mtext_c(&MF_SMALL, gfx_w / 2, base + 42, cnt, soft, 220, 1);
        }
        int y0 = 40, y1 = gfx_h - 40;
        for (int y = y0; y < y1; y++) {
            pix(gfx_w - 6, y, white, 34);
        }
        mark(gfx_w - 9, y0 - 9, gfx_w - 3, y1 + 9);
        int m = y0 + (int)((y1 - y0) * prog);
        men_rr_fill(gfx_w - 8, m - 8, 4, 16, 2, lab, 255);
    }
    if (men_level == LV_FAV && men_n_items <= 1) {
        men_mtext_c(&MF_LABEL, gfx_w / 2, gfx_h - 30, "HOLD AN ICON", white, 255, 0);
    }
    // the favorite message
    if (now - men_pop_at < POP_MS) {
        const mfont_t *f = men_mtext_w(&MF_LABEL, men_pop_text, 1) + 28 <= gfx_w - 8 ? &MF_LABEL : &MF_TINY;
        int w = men_mtext_w(f, men_pop_text, 1) + (f == &MF_LABEL ? 28 : 16), h = f == &MF_LABEL ? 30 : 24,
            px = (gfx_w - w) / 2, py = gfx_h / 2 - h / 2;
        men_rr_fill(px - 2, py - 2, w + 4, h + 4, 12, lcd(THEME_MENU_POP_EDGE), 255);
        men_rr_fill(px, py, w, h, 10, lcd(THEME_MENU_POP_BG), 255);
        men_mtext_c(f, gfx_w / 2, py + (f == &MF_LABEL ? 20 : 16), men_pop_text, lcd(THEME_MENU_POP_TEXT), 255, 1);
    }
}
// the read ahead: the icons of the items near the view (both ways: a scroll can turn) go to the icon task before they
// show, so a scroll draws no plain tile and never waits for the card
static void spot_read_ahead(void)
{
    const int c = (int)lroundf(men_pos);
    for (int d = 0; d <= 7; d++) {
        for (int sg = -1; sg <= 1; sg += 2) {
            const int i = c + d * sg;
            if (i >= 0 && i < men_n_items && men_items[i].kind == IT_APP && (d || sg < 0)) {
                tfn_icon_want(men_items[i].app);
            }
        }
    }
}
static void spot_render_body(int64_t now)
{
    tfn_icon_ask(true);  // (no card read in a frame of the menu: tfn_icons.c)
    spot_render_inner(now);
    spot_read_ahead();
    tfn_icon_ask(false);
    (void)tfn_icon_missed();  // (also the trim of the icon files that the task read)
}
static void spot_render_inner(int64_t now)
{
    spot_t l = spot_lay();
    int cat = room_cat();
    float t = men_glow_t;
    spot_room(&l, cat);
    uint8_t ca[3], cb[3];
    men_cat_colors(cat, ca, cb);
    uint16_t light = nat((ca[0] + 255 * 2) / 3, (ca[1] + 255 * 2) / 3, (ca[2] + 255 * 2) / 3);
    uint16_t dark = nat(cb[0] / 3, cb[1] / 3, cb[2] / 3);
    // the floor: a faint line and the pool of light under the middle item
    int fy = l.vert ? 0 : l.cy + l.big / 2 + (l.mode == SL_BAR ? 6 : 10);
    int mid = spot_items(&l, now, light, dark, fy, t);
    spot_words(&l, now, mid, ca);
}
