// The menu of the Tab5: the drawing (the rooms, the text, the status line, the icons, the frames with only the icons).
#include "menu_tab5_priv.h"
#include "tfn/theme.h"

#if BOARD_UI_TABLET
static struct {
    uint16_t *p;
    int key;
    uint32_t use;
} s_tbr[TB_ROOMS];
static uint32_t s_tbr_clock;
static uint16_t *s_tbbg;  // the room now: LCD byte order, gfx_w x gfx_h

// ---------------------------------------------------------------- the rooms
// the background of a look: the colours of the group, two soft lights, a darker glass under the rail and a shade under
// the status line; made at a quarter of the resolution and scaled (about 15 ms), kept for TB_ROOMS looks
static void tb_room_build(uint16_t *o, int look, const tlay_t *L)
{
    const int w = gfx_w, h = gfx_h;
    uint8_t a[3], b[3];
    men_cat_colors(look, a, b);
    float top[3], bot[3], g1[3], g2[3];
    for (int i = 0; i < 3; i++) {
        top[i] = b[i] * 0.85f + a[i] * 0.15f;
        bot[i] = b[i] * 0.3f;
        g1[i] = a[i] * 0.9f + 255 * 0.1f;
        g2[i] = (a[i] + b[i]) * 0.5f;
    }
    const int Q = 4, gw = w / Q + 2, gh = h / Q + 2;
    uint8_t *grid = heap_caps_malloc((size_t)gw * gh * 3, MALLOC_CAP_SPIRAM);
    if (!grid) {
        for (int i = 0; i < w * h; i++) {
            o[i] = rgb(b[0] / 2, b[1] / 2, b[2] / 2);
        }
        return;
    }
    const float big = (float)(w > h ? w : h), l1x = w * 0.84f, l1y = h * 0.1f, r1 = big * 0.62f;
    const float l2x = w * 0.2f, l2y = h * 0.95f, r2 = big * 0.5f;
    for (int gy = 0; gy < gh; gy++) {
        float Y = (float)gy * Q, u = Y / (h - 1);
        if (u > 1) {
            u = 1;
        }
        for (int gx = 0; gx < gw; gx++) {
            float X = (float)gx * Q, c[3];
            float d1 = ((X - l1x) * (X - l1x) + (Y - l1y) * (Y - l1y)) / (r1 * r1);
            float d2 = ((X - l2x) * (X - l2x) + (Y - l2y) * (Y - l2y)) / (r2 * r2);
            float f1 = d1 < 1 ? (1 - d1) * (1 - d1) * 0.55f : 0, f2 = d2 < 1 ? (1 - d2) * (1 - d2) * 0.35f : 0;
            for (int i = 0; i < 3; i++) {
                c[i] = top[i] + (bot[i] - top[i]) * u;
                c[i] += (g1[i] - c[i]) * f1;
                c[i] += (g2[i] - c[i]) * f2;
            }
            uint8_t *p = &grid[(gy * gw + gx) * 3];
            for (int i = 0; i < 3; i++) {
                p[i] = (uint8_t)(c[i] > 255 ? 255 : c[i] < 0 ? 0 : c[i]);
            }
        }
    }
    const int sb = TB_SB_H(L);
    for (int y = 0; y < h; y++) {
        int gy = y / Q, fyq = y % Q;
        uint16_t *row = &o[y * w];
        int shade = y < sb ? 70 * (sb - y) / sb : 0;  // (the status line: a shade from the top)
        for (int x = 0; x < w; x++) {
            int gx = x / Q, fxq = x % Q, v[3];
            const uint8_t *p00 = &grid[(gy * gw + gx) * 3], *p10 = p00 + 3, *p01 = p00 + gw * 3, *p11 = p01 + 3;
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
            uint16_t c = nat(v[0], v[1], v[2]);
            if (x < L->rail_w) {
                c = nmix(c, 0, 100);  // the glass of the rail
            } else if (shade) {
                c = nmix(c, 0, shade);
            }
            if (x == L->rail_w - 1) {
                c = nmix(c, lcd(THEME_MENU_TB_RAIL_EDGE), 40);  // its edge
            }
            row[x] = lcd(c);
        }
    }
    free(grid);
}
static uint16_t *s_tb_cbuf;
#define TB_KEEP (96 * 96)
static uint16_t *s_tb_keep_buf;  // (tb_render_part)
void men_tb_rooms_free(void)
{
    for (int i = 0; i < TB_ROOMS; i++) {
        free(s_tbr[i].p);
        s_tbr[i].p = NULL;
    }
    free(s_tb_keep_buf);
    s_tb_keep_buf = NULL;
    s_tbbg = NULL;
    free(s_tb_cbuf);
    s_tb_cbuf = NULL;
}
static uint16_t *tb_room(int look, const tlay_t *L)
{
    const int key = (look << 22) | (gfx_w << 11) | gfx_h;
    int k = -1, old = 0;
    for (int i = 0; i < TB_ROOMS; i++) {
        if (s_tbr[i].p && s_tbr[i].key == key) {
            k = i;
        }
        if (!s_tbr[i].p || (s_tbr[old].p && s_tbr[i].use < s_tbr[old].use)) {
            old = i;
        }
    }
    if (k < 0) {
        k = old;
        free(s_tbr[k].p);
        s_tbr[k].p = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM);
        if (!s_tbr[k].p) {
            return NULL;
        }
        s_tbr[k].key = key;
        tb_room_build(s_tbr[k].p, look, L);
    }
    s_tbr[k].use = ++s_tbr_clock;
    return s_tbr[k].p;
}
// The round corners of an icon: tb_under() keeps the 4 corner boxes (r x r) of the frame before the icon draws (the
// room, the shadow, a dark veil), tb_round() puts them back out of the round shape, with a soft edge (PSRAM, r <= TB_RMAX).
#define TB_RMAX 40
static void tb_under(int x, int y, int s, int r)
{
    if (!s_tb_cbuf) {
        s_tb_cbuf = heap_caps_malloc(4 * TB_RMAX * TB_RMAX * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_tb_cbuf || r > TB_RMAX) {
        return;
    }
    for (int c = 0; c < 4; c++) {
        for (int j = 0; j < r; j++) {
            for (int i = 0; i < r; i++) {
                int px = (c & 1) ? x + s - 1 - i : x + i, py = (c & 2) ? y + s - 1 - j : y + j;
                bool in = (unsigned)px < (unsigned)gfx_w && (unsigned)py < (unsigned)gfx_h;
                s_tb_cbuf[(c * TB_RMAX + j) * TB_RMAX + i] = in ? g_fb[py * gfx_w + px] : 0;
            }
        }
    }
}
static void tb_round(int x, int y, int s, int r, int cx0, int cy0, int cx1, int cy1)
{
    if (!s_tb_cbuf || r > TB_RMAX) {
        return;
    }
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            int cov = men_rr_cover(x + i, y + j, x, y, s, s, r);
            if (cov >= 255) {
                continue;
            }
            for (int c = 0; c < 4; c++) {
                int px = (c & 1) ? x + s - 1 - i : x + i, py = (c & 2) ? y + s - 1 - j : y + j;
                if (px < cx0 || px >= cx1 || py < cy0 || py >= cy1) {
                    continue;
                }
                uint16_t *q = &g_fb[py * gfx_w + px];
                uint16_t to = lcd(s_tb_cbuf[(c * TB_RMAX + j) * TB_RMAX + i]);
                *q = lcd(cov <= 0 ? to : nmix(to, lcd(*q), cov));
            }
        }
    }
}
// the room again in a box (over what the grid drew there)
static void tb_reblit(int x0, int y0, int x1, int y1)
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
    if (!s_tbbg || x1 <= x0) {
        return;
    }
    for (int y = y0; y < y1; y++) {
        memcpy(&g_fb[y * gfx_w + x0], &s_tbbg[y * gfx_w + x0], (size_t)(x1 - x0) * 2);
    }
}
// a box toward black (a = 0..255)
static void tb_dim(int x0, int y0, int x1, int y1, int a)
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
    if (a <= 0) {
        return;
    }
    for (int y = y0; y < y1; y++) {
        uint16_t *p = &g_fb[y * gfx_w + x0];
        for (int x = x0; x < x1; x++, p++) {
            *p = lcd(nmix(lcd(*p), 0, a));
        }
    }
}

// ---------------------------------------------------------------- drawing
// a text with a dark shadow (one line, centred on cx)
static void tb_text_c(const kfont_t *f, int cx, int base, const char *s, uint16_t col)
{
    ui_text_c(f, cx + 1, base + 1, s, THEME_MENU_TB_SHADOW);
    ui_text_c(f, cx, base, s, col);
}
// s cut to the width w
static void tb_fit(const kfont_t *f, char *s, int w)
{
    int n = (int)strlen(s);
    while (n > 1 && ui_text_w(f, s) > w) {
        s[--n] = 0;
    }
}
// A Latin line of a Rubik face with an own letter spacing (sp < 0: closer letters, for a long name in a cell)
static const kglyph_t *tb_kg(const kfont_t *f, int cp)
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
static float tb_kadv(const kfont_t *f, int c)
{
    const kglyph_t *g = tb_kg(f, c);
    return g ? g->adv4 / 4.0f : c == ' ' ? f->asc * 0.3f : 0;
}
static int tb_kw(const kfont_t *f, const char *s, float sp)
{
    float w = 0;
    int n = 0;
    for (; *s; s++, n++) {
        w += tb_kadv(f, (uint8_t)*s);
    }
    return (int)lroundf(w + (n > 1 ? (n - 1) * sp : 0));
}
static void tb_kt(const kfont_t *f, float x, int base, const char *s, uint16_t col, float sp)  // col: native
{
    for (; *s; s++) {
        const kglyph_t *g = tb_kg(f, (uint8_t)*s);
        if (g) {
            int gx = (int)lroundf(x) + g->ox, gy = base + g->oy;
            const uint8_t *src = f->a4 + g->off;
            for (int j = 0; j < g->h; j++) {
                for (int k = 0; k < g->w; k++) {
                    int idx = j * g->w + k, v = (idx & 1) ? src[idx >> 1] >> 4 : src[idx >> 1] & 15;
                    if (v) {
                        pix(gx + k, gy + j, col, v * 17);
                    }
                }
            }
        }
        x += tb_kadv(f, (uint8_t)*s) + sp;
    }
}
// one line centred on cx, with a dark shadow
static void tb_line(const kfont_t *f, int cx, int base, const char *s, uint16_t col, float sp)
{
    int x = cx - tb_kw(f, s, sp) / 2;
    tb_kt(f, x + 1, base + 1, s, lcd(THEME_MENU_TB_SHADOW), sp);
    tb_kt(f, x, base, s, col, sp);
}
// The name of an item in the width w: one line; else two lines (the split at the space that gives the narrowest wider
// line); else the same with closer letters; else cut. col: native.
static void tb_label(int cx, int base, const char *name, int w, int lines, uint16_t col)
{
    const kfont_t *f = &KF_LABEL;
    char a[32], b[32];
    snprintf(a, sizeof a, "%s", name);
    const int n = (int)strlen(a);
    static const float SP[3] = {0, -0.7f, -1.2f};
    for (int q = 0; q < 3; q++) {
        if (tb_kw(f, a, SP[q]) <= w) {
            tb_line(f, cx, base, a, col, SP[q]);
            return;
        }
        if (lines < 2) {
            continue;
        }
        int best = -1, bw = 1 << 20;
        for (int i = 1; i < n - 1; i++) {
            if (a[i] != ' ') {
                continue;
            }
            a[i] = 0;
            int m = tb_kw(f, a, SP[q]), m2 = tb_kw(f, a + i + 1, SP[q]);
            a[i] = ' ';
            if (m2 > m) {
                m = m2;
            }
            if (m < bw) {
                bw = m;
                best = i;
            }
        }
        if (best >= 0 && bw <= w) {
            snprintf(b, sizeof b, "%s", a + best + 1);
            a[best] = 0;
            tb_line(f, cx, base, a, col, SP[q]);
            tb_line(f, cx, base + 15, b, col, SP[q]);
            return;
        }
    }
    // (too long: the closest letters, cut)
    int cut = n;
    while (cut > 1 && tb_kw(f, a, SP[2]) > w) {
        a[--cut] = 0;
    }
    tb_line(f, cx, base, a, col, SP[2]);
}
// a symbol tile of a look (the gradient, the symbol, round corners): the rail and the title
static void tb_sym_tile(int look, int x, int y, int s, bool live)
{
    uint8_t a[3], b[3];
    men_cat_colors(look, a, b);
    tb_under(x, y, s, s / 4);
    for (int k = 0; k < s; k += 2) {
        gfx_fill_rect(x, y + k, s, s - k < 2 ? s - k : 2, men_grad(a, b, k, s));
    }
    men_tile_set(a, b, y, s, 2);
    int ss = s * 70 / 100;
    gfx_clip_box(x, y, x + s, y + s);
    men_cat_symbol(look, x + (s - ss) / 2, y + (s - ss) / 2, ss, men_grad(a, b, s / 2, s), live ? icon_time() : -1);
    gfx_clip_box_off();
    tb_round(x, y, s, s / 4, 0, 0, gfx_w, gfx_h);
}
// the battery: an icon with its level and a bolt while it charges, the percent at its left; returns the left edge
// (pct, chg: board_battery() of this frame)
static int tb_battery(int xr, int cy, int pct, bool chg)
{
    if (pct < 0) {
        return xr;
    }
    const uint16_t edge = THEME_MENU_TB_BAT_EDGE;
    const uint16_t c = pct > 40 ? THEME_MENU_BAT_OK : pct > 15 ? THEME_MENU_BAT_WARN : THEME_MENU_BAT_LOW;
    int bx = xr - 30, by = cy - 7;
    ui_rbox(bx, by, 27, 14, 4, THEME_MENU_TB_BAT_PLATE, edge, 2);
    gfx_fill_rect(bx + 27, by + 4, 3, 6, edge);
    int w = (21 * pct + 50) / 100;
    if (w > 0) {
        ui_rrect(bx + 3, by + 3, w, 8, 2, c);
    }
    if (chg) {
        float p[12] = {bx + 15.5f, by + 1.0f,  bx + 9.5f,  by + 7.5f, bx + 13.5f, by + 7.5f,
                       bx + 11.5f, by + 13.0f, bx + 17.5f, by + 6.5f, bx + 13.5f, by + 6.5f};
        men_fill_poly(p, 6, THEME_MENU_BAT_BOLT);
    }
    char t[16];
    snprintf(t, sizeof t, "%d%%", pct);
    int tw = ui_text_w(&KF_LABEL, t);
    ui_text(&KF_LABEL, bx - 6 - tw, cy + 5, t, pct > 15 ? edge : c);
    return bx - 6 - tw;
}
// the WiFi signal: 4 bars (none with no network); returns the left edge
static int tb_wifi(int xr, int cy)
{
    int rssi = wifi_rssi();
    if (!rssi) {
        return xr;
    }
    int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1, x = xr - 19;
    for (int b = 0; b < 4; b++) {
        int h = 4 + b * 3;
        ui_rrect(x + b * 5, cy + 6 - h, 4, h, 1, b < bars ? THEME_MENU_TB_WIFI_ON : THEME_MENU_TB_WIFI_OFF);
    }
    return x;
}
// the status line at the right: the battery in the corner, the time next to it, then the WiFi; returns the left edge
static int tb_status(const tlay_t *L, int pct, bool chg)
{
    int cy = L->land ? 22 : 17;
    int x = tb_battery(gfx_w - 8, cy, pct, chg);
    int tw = ui_text_w(&KF_NAME, men_tb_clock);
    x -= x < gfx_w - 8 ? 12 : 0;
    ui_text(&KF_NAME, x - tw + 1, cy + 7, men_tb_clock, THEME_MENU_TB_SHADOW);
    ui_text(&KF_NAME, x - tw, cy + 6, men_tb_clock, THEME_MENU_TEXT);
    x -= tw + 12;
    return tb_wifi(x, cy);
}
// the title of the level (and BACK in a group in a group); right = the right end of its place
static void tb_title(const tlay_t *L, int right, int off)
{
    char cnt[24], title[24];
    const char *nm = men_level == LV_NEW   ? "NEW"
                     : men_level == LV_FAV ? "FAVORITES"
                     : men_level < 0       ? "HOME"
                                           : men_gname(men_level);
    snprintf(title, sizeof title, "%s", nm);
    int n = men_ntbv;
    snprintf(cnt, sizeof cnt, n == 1 ? "%d APP" : "%d APPS", n);
    int x = L->gx0 + 2, base = L->land ? 33 : 80;
    int bx, by, bw, bh;
    if (men_tb_back_box(L, &bx, &by, &bw, &bh)) {
        ui_rrect_a(bx, by, bw, bh, 12, THEME_MENU_TB_BACK, men_tb_back_hot ? 90 : 46);
        float cy = by + bh / 2.0f, ax = bx + 12.0f;
        float p[6] = {ax, cy, ax + 9, cy - 9, ax + 9, cy + 9};
        men_fill_poly(p, 3, THEME_MENU_TB_BACK);
        char up[24];
        snprintf(up, sizeof up, "%s", men_gname(men_gparent(men_level)));
        tb_fit(&KF_NAME, up, bw - 34);
        ui_text(&KF_NAME, bx + 26, by + bh / 2 + 6, up, THEME_MENU_TB_BACK);
        x = bx + bw + 10;
        base = by + bh / 2 + (L->land ? 9 : 10);
    }
    int cw = ui_text_w(&KF_LABEL, cnt);
    // the biggest face that fits (upright: from 28 px, on the side: from 20 px, the status line)
    static const mfont_t *const TF[5] = {&MF_N28, &MF_N24, &MF_N20, &MF_N16, &MF_SMALL};
    const mfont_t *f = TF[4];
    for (int k = L->land ? 2 : 0; k < 5; k++) {
        if (men_mtext_w(TF[k], title, 0) <= right - x - cw - 14) {
            f = TF[k];
            break;
        }
    }
    men_mtext(f, x, base + off, title, lcd(THEME_MENU_TEXT), 255, 0);
    int tx = x + men_mtext_w(f, title, 0) + 10;
    if (L->land) {
        ui_text(&KF_LABEL, tx, base - 1 + off, cnt, THEME_MENU_TB_COUNT);
    } else {
        ui_text(&KF_LABEL, right - cw, base + off, cnt, THEME_MENU_TB_COUNT);
    }
}
// ---------------------------------------------------------------- the cost
// THE COST (a profile of the Tab5 board with a full redraw: 18 fps,
// 51 ms for each frame of the menu: the copy of the room 10.7 ms, the 15 icons 31 ms (each soft shadow 1.1 ms in
// ui_rrect_a(): a function call for each pixel; each icon 0.55 ms), the rail, the title and the status 8 ms). So:
//  - tb_rrect_a(): the same pixels as ui_rrect_a(), in a clip box, with no call for each pixel.
//  - When nothing moves but the icons (no scroll, slide, press, launch or message, the same status), the frame
//    buffer (gfx_swap: two buffers in turn) has the frame before the last one: only the box of each icon and the
//    tile of the tab now are drawn again (the room, the shadow, the icon, its marks). s_tbrec keeps the state of each
//    buffer (tb_sig); menu_redraw_all() (a system layer drew over the menu) and each 64th frame draw all.
// The picture is the same as a full drawing.
static void tb_rrect_a(int x, int y, int w, int h, int r, uint16_t c, int a, int cx0, int cy0, int cx1, int cy1)
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
    if (cx0 < 0) {
        cx0 = 0;
    }
    if (cy0 < 0) {
        cy0 = 0;
    }
    if (cx1 > gfx_w) {
        cx1 = gfx_w;
    }
    if (cy1 > gfx_h) {
        cy1 = gfx_h;
    }
    const uint16_t cn = lcd(c);
    for (int j = 0; j < h; j++) {
        const int yy = y + j;
        if (yy < cy0 || yy >= cy1) {
            continue;
        }
        uint16_t *row = &g_fb[yy * gfx_w];
        float dy = r < 1 ? 0 : j < r ? r - (j + 0.5f) : j >= h - r ? (j + 0.5f) - (h - r) : 0;
        int s0 = dy <= 0 ? x : x + r, s1 = dy <= 0 ? x + w : x + w - r;
        if (s0 < cx0) {
            s0 = cx0;
        }
        if (s1 > cx1) {
            s1 = cx1;
        }
        for (int xx = s0; xx < s1; xx++) {
            row[xx] = lcd(nmix(lcd(row[xx]), cn, a));
        }
        if (dy <= 0) {
            continue;
        }
        for (int i = 0; i < r; i++) {
            float dx = r - (i + 0.5f), d = sqrtf(dx * dx + dy * dy), cov = r - d + 0.5f;
            if (cov <= 0) {
                continue;
            }
            int k = (int)((cov >= 1 ? 1 : cov) * a);
            if (k <= 0) {
                continue;
            }
            int xa = x + i, xb = x + w - 1 - i;
            if (xa >= cx0 && xa < cx1) {
                row[xa] = lcd(nmix(lcd(row[xa]), cn, k));
            }
            if (xb >= cx0 && xb < cx1) {
                row[xb] = lcd(nmix(lcd(row[xb]), cn, k));
            }
        }
    }
}
// the state of a frame buffer: the buffer and the value of all that does not move in it (0: draw all)
typedef struct {
    const uint16_t *fb;
    uint32_t sig;
} tbrec_t;
static tbrec_t s_tbrec[2];
static uint32_t s_tb_frame;
void men_tb_redraw_all(void)
{
    memset(s_tbrec, 0, sizeof s_tbrec);
}
#ifndef ESP_PLATFORM
int g_tb_part_n;  // (host tests: the frames with only the icons)
#endif
static uint32_t tb_mix(uint32_t h, uint32_t v)
{
    for (int k = 0; k < 4; k++) {
        h = (h ^ ((v >> (k * 8)) & 0xFF)) * 16777619u;
    }
    return h;
}
static uint32_t tb_fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// the icon part of an item of the grid at (x, y) (its cell), the icon of size s (sc: the scale of the press); it draws
// only in the box ix - 5 .. ix + is + 5, iy - 4 .. iy + is + 4 (tb_render_part)
static void tb_draw_icon(const tlay_t *L, int i, int x, int y, float sc, bool dark)
{
    const item_t *it = &men_items[i];
    int s = L->icon, is = (int)lroundf(s * sc), ix = x + (L->cw - is) / 2, iy = y + (s - is) / 2;
    int r = is / 5 + 1;
    int cx0 = ix > L->rail_w ? ix : L->rail_w, cy0 = iy > L->top ? iy : L->top;
    int cx1 = ix + is < gfx_w ? ix + is : gfx_w, cy1 = iy + is < gfx_h ? iy + is : gfx_h;
    if (cx1 > cx0 && cy1 > cy0) {
        tb_rrect_a(ix - 1, iy + 2, is + 2, is + 2, r + 1, THEME_MENU_SHADOW, 80, 0, 0, gfx_w,
                   gfx_h);  // (a soft shadow)
        tb_under(ix, iy, is, r);
        gfx_clip_box(cx0, cy0, cx1, cy1);
        if (it->kind == IT_APP) {
            it->app->draw_icon(ix, iy, is);
        } else {
            men_group_tile(it->cat, ix, iy, is, false, false);
        }
        gfx_clip_box_off();
        gfx_clip_reset();
        tb_round(ix, iy, is, r, cx0, cy0, cx1, cy1);
        if (dark) {
            tb_dim(cx0, cy0, cx1, cy1, 70);
        }
        if (it->kind == IT_APP) {
            if (men_fav_index(it->app) >= 0) {
                men_star(ix + is - 7, iy + is - 7, 10, THEME_MENU_FAV_EDGE);
                men_star(ix + is - 7, iy + is - 7, 7, THEME_MENU_FAV);
            }
            if (men_level != LV_NEW && men_is_new(it->app)) {  // the NEW mark: a yellow dot on the top left corner
                ui_circle(ix + 3, iy + 3, 6, THEME_MENU_TB_NEW_EDGE, 255);
                ui_circle(ix + 3, iy + 3, 4, THEME_MENU_PILL, 255);
            }
            men_badge(it->app, ix, iy, is);
        }
    }
}
// an item of the grid: the icon, the hold bar, the name
static void tb_draw_item(const tlay_t *L, int i, int x, int y, float sc, bool dark, int64_t now)
{
    const item_t *it = &men_items[i];
    const int s = L->icon;
    tb_draw_icon(L, i, x, y, sc, dark);
    // the hold: a bar fills under the icon
    if (men_tb_pi == i && men_tb_t == TT_GRID && !men_tb_hold_done && !men_tb_moved &&
        now - men_tb_down_t >= HOLD_SHOW_MS) {
        int w = (int)((now - men_tb_down_t - HOLD_SHOW_MS) * s / (HOLD_MS - HOLD_SHOW_MS));
        if (w > s) {
            w = s;
        }
        int bx = x + (L->cw - s) / 2;
        ui_rrect(bx, y + s + 3, s, 5, 2, THEME_MENU_HOLD_TRACK);
        if (w > 4) {
            ui_rrect(bx, y + s + 3, w, 5, 2, THEME_MENU_HOLD_FILL);
        }
    }
    tb_label(x + L->cw / 2, y + s + 19, men_item_name(it), L->cw - 1, 2, lcd(THEME_MENU_TEXT));
}
// the value of all that the frame shows and that does not move (the same value: the same picture, but the icons)
static uint32_t tb_sig(const tlay_t *L, float scroll, int pct, bool chg)
{
    uint32_t h = 2166136261u;
    h = tb_mix(h, (uint32_t)(gfx_w << 16 | gfx_h));
    h = tb_mix(h, (uint32_t)(uintptr_t)s_tbbg);
    h = tb_mix(h, (uint32_t)men_tb_look());
    h = tb_mix(h, (uint32_t)men_level);
    h = tb_mix(h, tb_fbits(scroll));
    h = tb_mix(h, tb_fbits(ks_pos(&men_tb_rks)));
    h = tb_mix(h, tb_fbits(men_tb_hl));
    h = tb_mix(h, (uint32_t)(men_tb_t | men_tb_back_hot << 4));
    h = tb_mix(h, (uint32_t)pct << 1 | (uint32_t)chg);
    h = tb_mix(h, (uint32_t)wifi_rssi());
    for (const char *c = men_tb_clock; *c; c++) {
        h = tb_mix(h, (uint8_t)*c);
    }
    h = tb_mix(h, (uint32_t)(men_ntbt << 16 | men_tb_new_n));
    for (int t = 0; t < men_ntbt; t++) {
        h = tb_mix(h, (uint32_t)(men_tbt[t].kind << 16 | (uint16_t)men_tbt[t].lev));
    }
    h = tb_mix(h, (uint32_t)men_ntbv);
    for (int k = 0; k < men_ntbv; k++) {
        int x, y;
        men_tb_cell(L, k, scroll, &x, &y);
        if (y + L->ch < L->top - 4 || y > gfx_h) {
            continue;
        }
        const item_t *it = &men_items[men_tbv[k]];
        h = tb_mix(h, (uint32_t)(k << 16 | it->kind << 8 | (uint8_t)it->cat));
        h = tb_mix(h, (uint32_t)(uintptr_t)it->app);
        if (it->kind == IT_APP) {
            int n = it->app && it->app->name ? notify_unread(it->app->name) : 0;
            h = tb_mix(h, (uint32_t)(n << 2 | (men_fav_index(it->app) >= 0) << 1 | men_is_new(it->app)));
        }
    }
    return h | 1;
}
// only the icons again (tb_sig: the frame in this buffer is the same but the icons)
static void tb_render_part(const tlay_t *L, float scroll)
{
    const int top = L->top - 2;  // (above: the room and the title; no change)
    if (!s_tb_keep_buf) {
        s_tb_keep_buf = heap_caps_malloc(TB_KEEP * 2, MALLOC_CAP_SPIRAM);
    }
    uint16_t *keep = s_tb_keep_buf;  // (the rows above `top` in the box of an icon)
    for (int k = 0; k < men_ntbv; k++) {
        int x, y;
        men_tb_cell(L, k, scroll, &x, &y);
        if (y + L->ch < L->top - 4 || y > gfx_h) {
            continue;
        }
        const int s = L->icon, ix = x + (L->cw - s) / 2, iy = y;
        int x0 = ix - 5, x1 = ix + s + 5, y0 = iy - 4, y1 = iy + s + 4;
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > gfx_w) {
            x1 = gfx_w;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (y1 > gfx_h) {
            y1 = gfx_h;
        }
        if (y1 <= top || x1 <= x0) {
            continue;
        }
        const int bw = x1 - x0, nk = y0 < top ? top - y0 : 0;
        if (!keep || nk * bw > TB_KEEP) {
            men_tb_redraw_all();
            return;
        }  // (no memory: all in the next frame)
        for (int j = 0; j < nk; j++) {
            memcpy(&keep[j * bw], &g_fb[(y0 + j) * gfx_w + x0], (size_t)bw * 2);
        }
        for (int yy = y0 + nk; yy < y1; yy++) {
            memcpy(&g_fb[yy * gfx_w + x0], &s_tbbg[yy * gfx_w + x0], (size_t)bw * 2);
        }
        tb_draw_icon(L, men_tbv[k], x, y, 1.0f, false);
        for (int j = 0; j < nk; j++) {
            memcpy(&g_fb[(y0 + j) * gfx_w + x0], &keep[j * bw], (size_t)bw * 2);
        }
        // the soft edge under the title band (as in tb_render)
        for (int yy = y0 + nk; yy < y1 && yy < top + 12; yy++) {
            int a = 255 - (yy - top) * 21;
            uint16_t *p = &g_fb[yy * gfx_w + x0];
            const uint16_t *q = &s_tbbg[yy * gfx_w + x0];
            for (int xx = x0; xx < x1; xx++, p++, q++) {
                *p = lcd(nmix(lcd(*p), lcd(*q), a));
            }
        }
    }
    // the tile of the tab now (its symbol moves): the room, the highlight, the tile
    int cur = men_tb_tab_of(men_level);
    if (cur >= 0 && men_tb_hl >= 0) {
        float rs = ks_pos(&men_tb_rks);
        int hy = (int)lroundf(men_tb_hl - rs);
        int y = L->tab_y0 + cur * L->tab_h - (int)lroundf(rs);
        int sz = L->sym, x = (L->rail_w - sz) / 2, sy = y + (L->land ? 5 : 9);
        int y0 = sy < 0 ? 0 : sy, y1 = sy + sz > gfx_h ? gfx_h : sy + sz;
        if (y1 > y0) {
            for (int yy = y0; yy < y1; yy++) {
                memcpy(&g_fb[yy * gfx_w + x], &s_tbbg[yy * gfx_w + x], (size_t)sz * 2);
            }
            tb_rrect_a(5, hy + 2, L->rail_w - 10, L->tab_h - 4, 14, THEME_MENU_TB_TAB_HL, 40, x, y0, x + sz, y1);
            tb_sym_tile(men_tb_tab_look(&men_tbt[cur]), x, sy, sz, true);
        }
    }
}
// the rail: the highlight of the tab now, the tabs
static void tb_render_rail(const tlay_t *L)
{
    float rs = ks_pos(&men_tb_rks);
    int cur = men_tb_tab_of(men_level);
    if (cur >= 0 && men_tb_hl >= 0) {
        int hy = (int)lroundf(men_tb_hl - rs);
        uint8_t a[3], b[3];
        men_cat_colors(men_tb_tab_look(&men_tbt[cur]), a, b);
        tb_rrect_a(5, hy + 2, L->rail_w - 10, L->tab_h - 4, 14, THEME_MENU_TB_TAB_HL, 40, 0, 0, gfx_w, gfx_h);
        ui_rrect(1, hy + 14, 4, L->tab_h - 28, 2, rgb((a[0] + 255) / 2, (a[1] + 255) / 2, (a[2] + 255) / 2));
    }
    for (int t = 0; t < men_ntbt; t++) {
        int y = L->tab_y0 + t * L->tab_h - (int)lroundf(rs);
        if (y + L->tab_h < 0 || y > gfx_h) {
            continue;
        }
        const ttab_t *tb = &men_tbt[t];
        bool sel = t == cur;
        int s = L->sym, x = (L->rail_w - s) / 2, sy = y + (L->land ? 5 : 9);
        tb_sym_tile(men_tb_tab_look(tb), x, sy, s, sel);
        if (t == men_tb_ptab && men_tb_t == TT_RAIL) {
            tb_dim(x, sy, x + s, sy + s, 80);
        }
        if (tb->kind == TB_NEW && !sel) {
            char n[12];
            snprintf(n, sizeof n, "%d", men_tb_new_n > 99 ? 99 : men_tb_new_n);
            int w = ui_text_w(&KF_LABEL, n) + 10;
            ui_rrect(x + s - w / 2 - 2, sy - 5, w, 16, 8, THEME_MENU_PILL);
            ui_text_c(&KF_LABEL, x + s - 2, sy + 7, n, THEME_MENU_PILL_TEXT);
        }
        tb_label(L->rail_w / 2, sy + s + (L->land ? 14 : 16), men_tb_tab_name(tb), L->rail_w - 8, 2,
                 sel ? lcd(THEME_MENU_TEXT) : lcd(THEME_MENU_TB_TAB_TEXT));
    }
}
// the launch: the frame goes dark, the icon grows to the middle
static void tb_render_launch(int64_t now)
{
    if (men_tb_li >= 0 && men_tb_lapp) {
        float k = (float)(now - men_tb_lt) / TB_LAUNCH_MS;
        if (k > 1) {
            k = 1;
        }
        float e = 1 - (1 - k) * (1 - k);
        tb_dim(0, 0, gfx_w, gfx_h, (int)(190 * e));
        int s = (int)lroundf(men_tb_ls * (1 + 1.1f * e));
        float cx = men_tb_lx + men_tb_ls / 2.0f, cy = men_tb_ly + men_tb_ls / 2.0f;
        cx += (gfx_w / 2.0f - cx) * e * 0.7f;
        cy += (gfx_h / 2.0f - cy) * e * 0.7f;
        int x = (int)lroundf(cx - s / 2.0f), y = (int)lroundf(cy - s / 2.0f);
        int r = s / 5 + 1;
        tb_under(x, y, s, r);
        gfx_clip_box(x, y, x + s, y + s);
        men_tb_lapp->draw_icon(x, y, s);
        gfx_clip_box_off();
        gfx_clip_reset();
        tb_round(x, y, s, r, 0, 0, gfx_w, gfx_h);
    }
}
void men_tb_render(int64_t now)
{
    tlay_t L = men_tb_lay();
    s_tbbg = tb_room(men_tb_look(), &L);
    if (!s_tbbg) {
        gfx_fill(THEME_MENU_BG);
        men_tb_redraw_all();
        return;
    }
    // the slide of a new level
    int off = 0;
    if (men_tb_sw_t && now - men_tb_sw_t < TB_SLIDE_MS) {
        float k = 1 - (float)(now - men_tb_sw_t) / TB_SLIDE_MS;
        off = (int)lroundf(men_tb_sw_dir * 56 * k * k * k);
    }
    float scroll = ks_pos(&men_tb_ks);
    int at, size, alpha;
    const bool bar = ks_bar(&men_tb_ks, gfx_h - L.top - 12, &at, &size, &alpha) && alpha * 150 / 256 > 0;
    bool chg = false;
    const int pct = board_battery(&chg, NULL);
    // only the icons when nothing else moves and this buffer has the same frame (but the icons)
    const bool still = off == 0 && !bar && men_tb_li < 0 && men_tb_ai < 0 && men_tb_pi < 0 && men_tb_ptab < 0 &&
                       !men_tb_back_hot && now - men_pop_at >= POP_MS && !men_tb_kf_on();
    const uint32_t sig = still ? tb_sig(&L, scroll, pct, chg) : 0;
    int slot = s_tbrec[0].fb == g_fb ? 0 : s_tbrec[1].fb == g_fb ? 1 : -1;
    bool part = still && slot >= 0 && s_tbrec[slot].sig == sig && (s_tb_frame++ & 63) >= 2;
#ifndef ESP_PLATFORM
    if (g_menu_dtest != 1) {
        part = false;  // (the host tests: all, but t5dirty)
    }
    if (g_menu_dtest == 2) {
        slot = -2;  // (its reference frame: no record)
    }
#endif
    if (slot != -2) {
        if (slot < 0) {
            slot = s_tbrec[0].fb == NULL ? 0 : s_tbrec[1].fb == NULL ? 1 : 0;
        }
        s_tbrec[slot].fb = g_fb;
        s_tbrec[slot].sig = sig;
    }
    if (part) {
#ifndef ESP_PLATFORM
        g_tb_part_n++;
#endif
        tb_render_part(&L, scroll);
        return;
    }
    memcpy(g_fb, s_tbbg, (size_t)gfx_w * gfx_h * 2);
    // the grid
    for (int k = 0; k < men_ntbv; k++) {
        int x, y;
        men_tb_cell(&L, k, scroll, &x, &y);
        y += off;
        if (y + L.ch < L.top - 4 || y > gfx_h) {
            continue;
        }
        int i = men_tbv[k];
        if (i == men_tb_li) {
            continue;  // (drawn last, over the dark)
        }
        float sc = i == men_tb_ai ? men_tb_as : 1.0f;
        tb_draw_item(&L, i, x, y, sc, i == men_tb_pi && men_tb_t == TT_GRID, now);
    }
    if (men_ntbv == 0) {
        tb_text_c(&KF_NAME, (L.gx0 + gfx_w) / 2, L.top + 80, men_ntbt ? "NO APPS HERE" : "NO APPS",
                  THEME_MENU_TB_EMPTY);
    }
    if (men_tb_kf_on()) {
        // the key focus: a ring round the icon and its name
        int x, y;
        men_tb_cell(&L, men_tb_kf, scroll, &x, &y);
        y += off;
        gfx_frame_rect(x + 3, y - 6, L.cw - 6, L.ch - 2, 3, THEME_MENU_TB_FOCUS);
        gfx_frame_rect(x + 6, y - 3, L.cw - 12, L.ch - 8, 1, THEME_MENU_TB_FOCUS_IN);
    }
    // the room again over the title band (a soft edge under it) and the rail
    tb_reblit(L.rail_w, 0, gfx_w, L.top - 2);
    for (int j = 0; j < 12; j++) {
        int y = L.top - 2 + j, a = 255 - j * 21;
        if (y >= gfx_h) {
            break;
        }
        uint16_t *p = &g_fb[y * gfx_w + L.rail_w];
        const uint16_t *q = &s_tbbg[y * gfx_w + L.rail_w];
        for (int x = L.rail_w; x < gfx_w; x++, p++, q++) {
            *p = lcd(nmix(lcd(*p), lcd(*q), a));
        }
    }
    tb_reblit(0, 0, L.rail_w, gfx_h);
    // the scroll bar
    if (ks_bar(&men_tb_ks, gfx_h - L.top - 12, &at, &size, &alpha)) {
        ui_rrect_a(gfx_w - 5, L.top + 6 + at, 3, size, 1, THEME_MENU_TB_SCROLL, alpha * 150 / 256);
    }
    tb_render_rail(&L);
    // the status line and the title
    int sl = tb_status(&L, pct, chg);
    tb_title(&L, L.land ? sl - 12 : L.gx1, off);
    tb_render_launch(now);
    // the message of a change (the sheet: a favorite, a move)
    if (now - men_pop_at < POP_MS) {
        int w = ui_text_w(&KF_NAME, men_pop_text) + 36, h = 44, px = (gfx_w - w) / 2, py = gfx_h / 2 - h / 2;
        ui_rrect(px - 2, py - 2, w + 4, h + 4, 16, THEME_MENU_POP_EDGE);
        ui_rrect(px, py, w, h, 14, THEME_MENU_POP_BG);
        ui_text_c(&KF_NAME, gfx_w / 2, py + 28, men_pop_text, THEME_MENU_POP_TEXT);
    }
}
#endif  // BOARD_UI_TABLET
