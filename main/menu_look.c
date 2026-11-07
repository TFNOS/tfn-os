// The menu: the group looks (the colours and the symbols), the group tile, the WiFi, the battery, the badge.
#include "app_menu_priv.h"
#include "tfn/theme.h"

// ---------------------------------------------------------------- drawing

// a filled polygon (n <= 16 points, x/y pairs), even-odd rule, pixel centres
void men_fill_poly(const float *p, int n, uint16_t c)
{
    float y0 = p[1], y1 = p[1];
    for (int i = 1; i < n; i++) {
        if (p[2 * i + 1] < y0) {
            y0 = p[2 * i + 1];
        }
        if (p[2 * i + 1] > y1) {
            y1 = p[2 * i + 1];
        }
    }
    for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++) {
        float yc = y + 0.5f, xs[16];
        int k = 0;
        for (int i = 0; i < n && k < 16; i++) {
            float ax = p[2 * i], ay = p[2 * i + 1];
            float bx = p[2 * ((i + 1) % n)], by = p[2 * ((i + 1) % n) + 1];
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc)) {
                xs[k++] = ax + (yc - ay) * (bx - ax) / (by - ay);
            }
        }
        for (int i = 1; i < k; i++) {  // a short insertion sort
            float v = xs[i];
            int j = i - 1;
            while (j >= 0 && xs[j] > v) {
                xs[j + 1] = xs[j];
                j--;
            }
            xs[j + 1] = v;
        }
        for (int i = 0; i + 1 < k; i += 2) {
            int xa = (int)ceilf(xs[i] - 0.5f), xb = (int)ceilf(xs[i + 1] - 0.5f);
            if (xb > xa) {
                gfx_fill_rect(xa, y, xb - xa, 1, c);
            }
        }
    }
}

void men_star(float cx, float cy, float r, uint16_t c)
{
    float p[20];
    for (int i = 0; i < 10; i++) {
        float a = -1.5707963f + i * 0.62831853f, rr = (i & 1) ? r * 0.43f : r;
        p[2 * i] = cx + rr * cosf(a);
        p[2 * i + 1] = cy + rr * sinf(a);
    }
    men_fill_poly(p, 10, c);
}

// a rectangle of width w from (ax, ay) to (bx, by)
static void bar(float ax, float ay, float bx, float by, float w, uint16_t c)
{
    float dx = bx - ax, dy = by - ay, m = sqrtf(dx * dx + dy * dy);
    if (m < 0.01f) {
        return;
    }
    float nx = -dy / m * w * 0.5f, ny = dx / m * w * 0.5f;
    float p[8] = {ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny};
    men_fill_poly(p, 4, c);
}

// ---------------------------------------------------------------- the group looks
// A look (ST_*, menu_tree.c) = a colour pair (the tile gradient, and the room of the group) and a flat symbol drawn
// by code. Looks 0 .. 23: each has its own colour pair (the nearest two of all 24 looks are far apart) and a small
// motion on the middle (selected) tile only. ST_TOP is the main menu (the BACK tile that goes to it).

// the colours of a group look (ST_*: top and bottom of the gradient)
void men_cat_colors(int st, uint8_t *top, uint8_t *bot)
{
    static const uint8_t C[ST_N + 1][6] = {
            {104, 70, 214, 38, 22, 104},    // games: violet
            {30, 160, 150, 8, 60, 76},      // util: teal
            {120, 130, 152, 44, 48, 64},    // settings: steel
            {236, 176, 44, 128, 70, 8},     // star (the favorites): gold
            {196, 92, 52, 84, 30, 22},      // quest: rust
            {214, 70, 150, 92, 18, 70},     // music: magenta
            {80, 190, 70, 20, 84, 30},      // kids: green
            {236, 96, 40, 120, 24, 16},     // action: orange red
            {70, 205, 235, 12, 84, 140},    // silly: cyan
            {250, 222, 72, 170, 122, 12},   // 2 players: lemon
            {20, 120, 150, 4, 34, 56},      // party: dark teal
            {74, 70, 76, 16, 14, 18},       // retro: charcoal
            {110, 225, 180, 14, 100, 84},   // puzzle: mint
            {40, 140, 96, 6, 52, 36},       // board games: felt green
            {176, 214, 52, 62, 102, 16},    // sports: lime
            {232, 40, 64, 104, 8, 26},      // racing: crimson
            {255, 150, 50, 150, 60, 10},    // tilt: orange
            {255, 176, 96, 20, 76, 160},    // sea: a sunset sky over the sea
            {150, 140, 220, 40, 30, 90},    // calm: lavender
            {30, 40, 90, 4, 6, 20},         // space: blue black
            {142, 60, 132, 44, 12, 40},     // learn: plum
            {120, 190, 255, 30, 80, 180},   // talk: sky
            {255, 160, 190, 170, 50, 100},  // heart: pink
            {150, 36, 44, 36, 6, 10},       // new: maroon
            {64, 84, 168, 14, 18, 50},      // the main menu (ST_TOP): navy
    };
    if (st < 0 || st > ST_N) {
        st = 0;
    }
    memcpy(top, C[st], 3);
    memcpy(bot, C[st] + 3, 3);
}

uint16_t men_grad(const uint8_t *a, const uint8_t *b, int k, int n)
{
    return rgb((uint8_t)(a[0] + (b[0] - a[0]) * k / n), (uint8_t)(a[1] + (b[1] - a[1]) * k / n),
               (uint8_t)(a[2] + (b[2] - a[2]) * k / n));
}

// The tile that is drawn now (its gradient rows): a hole in a symbol shows the tile colour of each row.
static struct {
    uint8_t a[3], b[3];
    int y, s, step;
} s_tg;  // (zero at the start: .bss, in PSRAM)
void men_tile_set(const uint8_t *a, const uint8_t *b, int y, int s, int step)
{
    memcpy(s_tg.a, a, 3);
    memcpy(s_tg.b, b, 3);
    s_tg.y = y;
    s_tg.s = s > 0 ? s : 1;
    s_tg.step = step > 0 ? step : 1;
}
static uint16_t tile_at(int y)
{
    if (s_tg.s < 1 || s_tg.step < 1) {
        return men_grad(s_tg.a, s_tg.b, 0, 1);
    }
    int k = y - s_tg.y;
    if (k < 0) {
        k = 0;
    }
    if (k > s_tg.s - 1) {
        k = s_tg.s - 1;
    }
    return men_grad(s_tg.a, s_tg.b, k - k % s_tg.step, s_tg.s);
}

// ---- the helpers of the symbols (float places; the y axis goes down). The symbols are drawn a few times in a frame:
// GCC makes them for size (SYM_FN), not for speed (-O2), about 4 KB less flash.
#if defined(__GNUC__) && !defined(__clang__)
#define SYM_FN __attribute__((optimize("Os")))
#else
#define SYM_FN
#endif
static void SYM_FN disc(float x, float y, float r, uint16_t c)
{
    int ir = (int)lroundf(r);
    gfx_fill_circle((int)lroundf(x), (int)lroundf(y), ir < 0 ? 0 : ir, c);
}
// a round hole: the tile colour of each row
static void SYM_FN hole(float x, float y, float r)
{
    int ir = (int)lroundf(r), ix = (int)lroundf(x), iy = (int)lroundf(y);
    for (int dy = -ir; dy <= ir; dy++) {
        int dx = (int)sqrtf((float)(ir * ir - dy * dy) + 0.25f);
        gfx_fill_rect(ix - dx, iy + dy, 2 * dx + 1, 1, tile_at(iy + dy));
    }
}
// a rectangle with round corners (radius r)
static void SYM_FN rbox(float x0, float y0, float x1, float y1, float r, uint16_t c)
{
    int a = (int)lroundf(x0), b = (int)lroundf(y0), e = (int)lroundf(x1), f = (int)lroundf(y1), q = (int)lroundf(r);
    if (q > (e - a) / 2) {
        q = (e - a) / 2;
    }
    if (q > (f - b) / 2) {
        q = (f - b) / 2;
    }
    if (q < 0) {
        q = 0;
    }
    gfx_fill_rect(a + q, b, e - a - 2 * q, f - b, c);
    gfx_fill_rect(a, b + q, e - a, f - b - 2 * q, c);
    gfx_fill_circle(a + q, b + q, q, c);
    gfx_fill_circle(e - 1 - q, b + q, q, c);
    gfx_fill_circle(a + q, f - 1 - q, q, c);
    gfx_fill_circle(e - 1 - q, f - 1 - q, q, c);
}
// a shape in its own units (u = the unit in pixels), turned by an angle and moved to (cx, cy)
typedef struct {
    float cx, cy, u, ca, sa;
} xf_t;
static xf_t xf(float cx, float cy, float u, float a)
{
    return (xf_t){cx, cy, u, cosf(a), sinf(a)};
}
static void xpt(const xf_t *f, float lx, float ly, float *x, float *y)
{
    *x = f->cx + (lx * f->ca - ly * f->sa) * f->u;
    *y = f->cy + (lx * f->sa + ly * f->ca) * f->u;
}
static void SYM_FN xpoly(const xf_t *f, const float *p, int n, uint16_t c)
{
    float q[32];
    if (n > 16) {
        n = 16;
    }
    for (int i = 0; i < n; i++) {
        xpt(f, p[2 * i], p[2 * i + 1], &q[2 * i], &q[2 * i + 1]);
    }
    men_fill_poly(q, n, c);
}
static void SYM_FN xdisc(const xf_t *f, float lx, float ly, float r, uint16_t c)
{
    float x, y;
    xpt(f, lx, ly, &x, &y);
    disc(x, y, r * f->u, c);
}
static void SYM_FN xhole(const xf_t *f, float lx, float ly, float r)
{
    float x, y;
    xpt(f, lx, ly, &x, &y);
    hole(x, y, r * f->u);
}
// a curved arrow: an arc of the circle (cx, cy, r) from the angle a0 to the angle a1, and its head at a1
static void SYM_FN arc_arrow(float cx, float cy, float r, float a0, float a1, float w, uint16_t c)
{
    float px = cx + cosf(a0) * r, py = cy + sinf(a0) * r;
    for (int i = 1; i <= 6; i++) {
        float a = a0 + (a1 - a0) * i / 6, qx = cx + cosf(a) * r, qy = cy + sinf(a) * r;
        bar(px, py, qx, qy, w, c);
        disc(qx, qy, w * 0.5f, c);
        px = qx;
        py = qy;
    }
    float d = a1 > a0 ? 1.0f : -1.0f, tx = -sinf(a1) * d, ty = cosf(a1) * d, nx = cosf(a1), ny = sinf(a1);
    float tri[6] = {px + tx * w * 2.4f, py + ty * w * 2.4f, px + nx * w * 1.7f,
                    py + ny * w * 1.7f, px - nx * w * 1.7f, py - ny * w * 1.7f};
    men_fill_poly(tri, 3, c);
}
// a die: a white square with cut corners (half size h, turned by a), its grey side, and n pips
static void SYM_FN die(float cx, float cy, float h, float a, int n)
{
    static const uint16_t PIPS[7] = {0, 0x010, 0x044, 0x054, 0x145, 0x155, 0x16D};  // a 3 x 3 grid, bit 8 = top left
    static const float OCT[16] = {-1, -0.6f, -0.6f, -1, 0.6f, -1, 1, -0.6f, 1, 0.6f, 0.6f, 1, -0.6f, 1, -1, 0.6f};
    xf_t f = xf(cx + h * 0.16f, cy + h * 0.18f, h, a);
    xpoly(&f, OCT, 8, rgb(166, 172, 192));
    f.cx = cx;
    f.cy = cy;
    xpoly(&f, OCT, 8, rgb(248, 248, 252));
    for (int k = 0; k < 9; k++) {
        if (PIPS[n] >> (8 - k) & 1) {
            float px, py, r = 0.17f * h;
            xpt(&f, (k % 3 - 1) * 0.52f, (k / 3 - 1) * 0.52f, &px, &py);
            int q = r < 1.3f ? 2 : 3;
            if (r >= 2.2f) {
                disc(px, py, r, rgb(40, 44, 60));
            } else {
                gfx_fill_rect((int)lroundf(px - q * 0.5f), (int)lroundf(py - q * 0.5f), q, q,
                              rgb(40, 44, 60));  // (a small pip: a square)
            }
        }
    }
}
// a heart of half width h (two round lobes and the lines from the tip that touch them)
static void SYM_FN heart(float cx, float cy, float h, uint16_t c)
{
    float lx = 0.47f * h, ly = cy - 0.3f * h, r = 0.53f * h, tipy = cy + 0.92f * h;
    // the point where the line from the tip touches the right lobe
    float vx = -lx, vy = tipy - ly, d = sqrtf(vx * vx + vy * vy), a = atan2f(vy, vx) - acosf(r / d);
    float tx = lx + cosf(a) * r, ty = ly + sinf(a) * r;
    disc(cx - lx, ly, r, c);
    disc(cx + lx, ly, r, c);
    float p[8] = {cx - tx, ty, cx, ly, cx + tx, ty, cx, tipy};
    men_fill_poly(p, 4, c);
}
// a flame: a round bottom (bx, by, r) that goes up to a tip (h above by, moved sideways by sw)
static void SYM_FN flame(float bx, float by, float r, float h, float sw, uint16_t c)
{
    float p[24];
    int n = 0;
    for (int i = 0; i <= 6; i++) {
        float a = i * 3.14159265f / 6;
        p[n++] = bx + cosf(a) * r;
        p[n++] = by + sinf(a) * r;
    }
    const float L[5][3] = {
            {-0.94f, 0.34f, 0.2f}, {-0.52f, 0.7f, 0.6f}, {0, 1, 1}, {0.46f, 0.68f, 0.6f}, {0.9f, 0.3f, 0.2f}};
    for (int i = 0; i < 5; i++) {
        p[n++] = bx + L[i][0] * r + L[i][2] * sw;
        p[n++] = by - L[i][1] * h;
    }
    men_fill_poly(p, 12, c);
}

// a game pad
static void SYM_FN sym_games(float cx, float cy, float u, uint16_t w, uint16_t dk)
{
    float bw = u * 1.5f, bh = u * 0.62f;
    gfx_fill_rect((int)(cx - bw / 2 + bh / 2), (int)(cy - bh / 2), (int)(bw - bh), (int)bh, w);
    gfx_fill_circle((int)(cx - bw / 2 + bh / 2), (int)cy, (int)(bh / 2), w);
    gfx_fill_circle((int)(cx + bw / 2 - bh / 2), (int)cy, (int)(bh / 2), w);
    gfx_fill_circle((int)(cx - bw * 0.3f), (int)(cy + bh * 0.42f), (int)(bh * 0.42f), w);
    gfx_fill_circle((int)(cx + bw * 0.3f), (int)(cy + bh * 0.42f), (int)(bh * 0.42f), w);
    float dx = cx - bw * 0.27f, d = bh * 0.56f, t = bh * 0.2f;
    gfx_fill_rect((int)(dx - d / 2), (int)(cy - t / 2), (int)d, (int)t + 1, dk);
    gfx_fill_rect((int)(dx - t / 2), (int)(cy - d / 2), (int)t + 1, (int)d, dk);
    float bx = cx + bw * 0.27f, o = bh * 0.2f;
    int br = (int)(bh * 0.1f) + 1;
    gfx_fill_circle((int)bx, (int)(cy - o), br, rgb(240, 70, 80));
    gfx_fill_circle((int)(bx + o), (int)cy, br, rgb(60, 200, 90));
    gfx_fill_circle((int)bx, (int)(cy + o), br, rgb(60, 120, 240));
    gfx_fill_circle((int)(bx - o), (int)cy, br, rgb(250, 210, 40));
}

// a wrench across a ruler
static void SYM_FN sym_util(float cx, float cy, float u, uint16_t w, uint16_t dk, uint16_t bg)
{
    bar(cx - u * 0.75f, cy + u * 0.35f, cx + u * 0.75f, cy - u * 0.35f, u * 0.34f, rgb(250, 210, 60));
    for (int i = -3; i <= 3; i++) {
        float tx = cx + i * u * 0.2f, ty = cy - i * u * 0.2f * 0.47f;
        bar(tx, ty, tx - u * 0.05f, ty - u * 0.1f, 1.5f, dk);
    }
    bar(cx - u * 0.55f, cy - u * 0.55f, cx + u * 0.3f, cy + u * 0.3f, u * 0.24f, w);
    float hx = cx + u * 0.45f, hy = cy + u * 0.45f;
    gfx_fill_circle((int)hx, (int)hy, (int)(u * 0.32f), w);
    bar(hx, hy, hx + u * 0.22f, hy + u * 0.22f, u * 0.24f, bg);  // the jaw
    gfx_fill_circle((int)(cx - u * 0.6f), (int)(cy - u * 0.6f), (int)(u * 0.17f), w);
}

// a gear
static void SYM_FN sym_settings(float cx, float cy, float u, uint16_t w, uint16_t bg)
{
    float r = u * 0.82f;
    for (int k = 0; k < 8; k++) {
        float a = k * 0.78539816f, ca = cosf(a), sa = sinf(a);
        bar(cx + ca * r * 0.6f, cy + sa * r * 0.6f, cx + ca * r * 1.08f, cy + sa * r * 1.08f, r * 0.38f, w);
    }
    gfx_fill_circle((int)cx, (int)cy, (int)(r * 0.82f), w);
    gfx_fill_circle((int)cx, (int)cy, (int)(r * 0.36f), bg);
}

// a map: a sheet with two folds, a dotted path and an X
static void SYM_FN sym_quest(float cx, float cy, float u)
{
    uint16_t pa = rgb(246, 226, 180), ink = rgb(120, 60, 30);
    float hw = u * 0.78f, hh = u * 0.6f;
    float p[8] = {cx - hw, cy - hh + u * 0.1f, cx + hw, cy - hh, cx + hw, cy + hh - u * 0.1f, cx - hw, cy + hh};
    men_fill_poly(p, 4, pa);
    bar(cx - hw / 3, cy - hh + 2, cx - hw / 3, cy + hh - 2, 1.5f, rgb(200, 170, 120));
    bar(cx + hw / 3, cy - hh + 2, cx + hw / 3, cy + hh - 2, 1.5f, rgb(200, 170, 120));
    for (int i = 0; i < 5; i++) {
        float a = i / 4.0f, px = cx - hw * 0.7f + a * hw * 1.1f, py = cy + hh * 0.45f - sinf(a * 3.1f) * hh * 0.6f;
        gfx_fill_circle((int)px, (int)py, (int)(u * 0.06f) + 1, ink);
    }
    float xx = cx + hw * 0.55f, xy = cy - hh * 0.35f, xs = u * 0.17f;
    bar(xx - xs, xy - xs, xx + xs, xy + xs, u * 0.1f, rgb(220, 40, 40));
    bar(xx - xs, xy + xs, xx + xs, xy - xs, u * 0.1f, rgb(220, 40, 40));
}

// two notes with a beam
static void SYM_FN sym_music(float cx, float cy, float u, uint16_t w)
{
    float r = u * 0.24f, x1 = cx - u * 0.42f, x2 = cx + u * 0.42f, y1 = cy + u * 0.5f, y2 = cy + u * 0.36f;
    gfx_fill_circle((int)x1, (int)y1, (int)r, w);
    gfx_fill_circle((int)x2, (int)y2, (int)r, w);
    bar(x1 + r * 0.8f, y1, x1 + r * 0.8f, cy - u * 0.5f, u * 0.1f, w);
    bar(x2 + r * 0.8f, y2, x2 + r * 0.8f, cy - u * 0.64f, u * 0.1f, w);
    bar(x1 + r * 0.8f, cy - u * 0.5f, x2 + r * 0.8f, cy - u * 0.64f, u * 0.2f, w);
}

// a smiling face
static void SYM_FN sym_kids(float cx, float cy, float u, uint16_t dk)
{
    float r = u * 0.8f;
    gfx_fill_circle((int)cx, (int)cy, (int)r, rgb(255, 214, 60));
    gfx_fill_circle((int)(cx - r * 0.35f), (int)(cy - r * 0.25f), (int)(r * 0.12f) + 1, dk);
    gfx_fill_circle((int)(cx + r * 0.35f), (int)(cy - r * 0.25f), (int)(r * 0.12f) + 1, dk);
    for (int i = 0; i < 7; i++) {
        float a = 0.5f + i * 0.35f, b = a + 0.35f;
        bar(cx + cosf(a) * r * 0.55f, cy + sinf(a) * r * 0.55f, cx + cosf(b) * r * 0.55f, cy + sinf(b) * r * 0.55f,
            r * 0.12f, dk);
    }
}

// a lightning bolt
static void SYM_FN sym_action(float cx, float cy, float u)
{
    float p[12] = {cx + u * 0.15f, cy - u * 0.85f, cx - u * 0.5f, cy + u * 0.1f,  cx - u * 0.02f, cy + u * 0.1f,
                   cx - u * 0.2f,  cy + u * 0.85f, cx + u * 0.5f, cy - u * 0.15f, cx + u * 0.02f, cy - u * 0.15f};
    men_fill_poly(p, 6, rgb(255, 236, 90));
}

// a silly face: googly eyes (the pupils roll) and the tongue out (it moves)
static void SYM_FN sym_silly(float cx, float cy, float u, uint16_t dk, bool live, float tm)
{
    disc(cx, cy + u * 0.02f, u * 0.84f, rgb(255, 204, 64));
    const float EX[2] = {-0.33f, 0.33f}, EY[2] = {-0.24f, -0.28f}, ER[2] = {0.3f, 0.26f};
    const float PA[2] = {live ? tm * 4.6f : -0.7f, live ? 1.0f - tm * 3.3f : 2.5f};  // the pupils look two ways
    for (int i = 0; i < 2; i++) {
        float ex = cx + EX[i] * u, ey = cy + EY[i] * u, er = ER[i] * u, pr = er * 0.5f, off = er - pr - u * 0.03f;
        disc(ex, ey, er + u * 0.05f + 0.5f, dk);
        disc(ex, ey, er, rgb(255, 255, 255));
        disc(ex + cosf(PA[i]) * off, ey + sinf(PA[i]) * off, pr, dk);
    }
    float my = cy + u * 0.2f, mp[20];
    for (int i = 0; i < 10; i++) {  // the open mouth: the lower half of an ellipse
        float a = i * 3.14159265f / 9;
        mp[2 * i] = cx + cosf(a) * u * 0.46f;
        mp[2 * i + 1] = my + sinf(a) * u * 0.34f;
    }
    men_fill_poly(mp, 10, rgb(110, 26, 40));
    float tx = cx + u * 0.08f + (live ? sinf(tm * 6.0f) * u * 0.07f : 0), tw = u * 0.21f;
    uint16_t tc = rgb(250, 96, 130);
    gfx_fill_rect((int)lroundf(tx - tw), (int)lroundf(my + u * 0.12f), (int)lroundf(tw * 2), (int)lroundf(u * 0.44f),
                  tc);
    disc(tx, my + u * 0.56f, tw, tc);
    bar(tx, my + u * 0.22f, tx, my + u * 0.58f, u * 0.05f + 0.8f, rgb(214, 56, 96));
}

// two players, red and blue (they hop in turn)
static void SYM_FN sym_duo(float cx, float cy, float u, bool live, float tm)
{
    static const uint8_t PC[2][3] = {{226, 54, 66}, {44, 104, 226}};
    for (int i = 0; i < 2; i++) {
        float fx = cx + (i ? 0.38f : -0.38f) * u,
              hop = live ? fmaxf(0, sinf(tm * 6.0f + i * 3.14159265f)) * u * 0.1f : 0;
        float base = cy + u * 0.66f - hop, p[22];
        uint16_t c = rgb(PC[i][0], PC[i][1], PC[i][2]);
        for (int k = 0; k <= 10; k++) {  // the body: the top half of an ellipse on the floor
            float a = 3.14159265f + k * 3.14159265f / 10;
            p[2 * k] = fx + cosf(a) * u * 0.35f;
            p[2 * k + 1] = base + sinf(a) * u * 0.62f;
        }
        men_fill_poly(p, 11, c);
        disc(fx, base - u * 0.93f, u * 0.23f, c);
    }
}

// a party hat with stripes and a pompom, and confetti (it spins)
static void SYM_FN sym_party(float cx, float cy, float u, bool live, float tm)
{
    static const uint8_t CF[7][3] = {{255, 92, 160}, {90, 230, 255},  {130, 240, 110}, {255, 236, 110},
                                     {255, 150, 60}, {255, 255, 255}, {200, 150, 255}};
    static const float CP[7][3] = {{-0.74f, -0.6f, 0.4f}, {-0.5f, -0.16f, 2.0f}, {0.62f, -0.72f, 1.1f},
                                   {0.76f, -0.12f, 2.7f}, {0.66f, 0.5f, 0.2f},   {-0.8f, 0.36f, 1.6f},
                                   {-0.24f, -0.86f, 2.2f}};
    for (int i = 0; i < 7; i++) {
        float a = CP[i][2] + (live ? tm * (2.2f + i * 0.35f) : 0);
        float px = cx + CP[i][0] * u, py = cy + CP[i][1] * u + (live ? sinf(tm * 2.0f + i) * u * 0.05f : 0);
        float dx = cosf(a) * u * 0.1f, dy = sinf(a) * u * 0.1f;
        bar(px - dx, py - dy, px + dx, py + dy, u * 0.11f + 0.8f, rgb(CF[i][0], CF[i][1], CF[i][2]));
    }
    float tx = cx + u * 0.08f, ty = cy - u * 0.72f, lx = cx - u * 0.46f, ly = cy + u * 0.62f, rx = cx + u * 0.44f,
          ry = cy + u * 0.72f;
    float hat[6] = {tx, ty, lx, ly, rx, ry};
    men_fill_poly(hat, 3, rgb(255, 208, 64));
    for (int b = 0; b < 2; b++) {  // two stripes across the cone
        float f0 = 0.36f + b * 0.32f, f1 = f0 + 0.15f;
        float q[8] = {tx + (lx - tx) * f0, ty + (ly - ty) * f0, tx + (rx - tx) * f0, ty + (ry - ty) * f0,
                      tx + (rx - tx) * f1, ty + (ry - ty) * f1, tx + (lx - tx) * f1, ty + (ly - ty) * f1};
        men_fill_poly(q, 4, rgb(255, 86, 150));
    }
    disc(tx, ty, u * 0.15f, rgb(255, 255, 255));
}

// a pixel space alien (its legs move: two frames)
static void SYM_FN sym_retro(float cx, float cy, int s, bool live, float tm)
{
    static const uint16_t AL[2][8] = {{0x104, 0x088, 0x1FC, 0x376, 0x7FF, 0x5FD, 0x505, 0x0D8},
                                      {0x104, 0x489, 0x5FD, 0x777, 0x7FF, 0x3FE, 0x104, 0x202}};
    int fr = live ? ((int)(tm * 2.0f)) & 1 : 0, px = s / 11;
    if (px < 1) {
        px = 1;
    }
    int x0 = (int)lroundf(cx - 5.5f * px), y0 = (int)lroundf(cy - 4.0f * px);
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 11; c++) {
            if (AL[fr][r] >> (10 - c) & 1) {
                gfx_fill_rect(x0 + c * px, y0 + r * px, px, px, rgb(110, 255, 120));
            }
        }
    }
}

// a jigsaw piece: two knobs (up, right) and two holes (left, down); it turns a little
static void SYM_FN sym_puzzle(float cx, float cy, float u, bool live, float tm)
{
    xf_t f = xf(cx + u * 0.02f, cy, u * 1.18f, -0.12f + (live ? sinf(tm * 2.4f) * 0.1f : 0));
    static const float SQ[8] = {-0.6f, -0.28f, 0.28f, -0.28f, 0.28f, 0.6f, -0.6f, 0.6f};
    uint16_t pc = rgb(255, 146, 60);
    xpoly(&f, SQ, 4, pc);
    xdisc(&f, -0.16f, -0.44f, 0.21f, pc);
    xdisc(&f, 0.44f, 0.16f, 0.21f, pc);
    xhole(&f, -0.47f, 0.16f, 0.19f);
    xhole(&f, -0.16f, 0.47f, 0.19f);
}

// two dice (they rock)
static void SYM_FN sym_board(float cx, float cy, float u, bool live, float tm)
{
    float rk = live ? sinf(tm * 3.0f) * 0.09f : 0;
    die(cx + u * 0.36f, cy - u * 0.34f, u * 0.31f, 0.34f - rk, 3);
    die(cx - u * 0.2f, cy + u * 0.16f, u * 0.42f, -0.24f + rk, 5);
}

// a football (it turns)
static void SYM_FN sym_sports(float cx, float cy, float u, bool live, float tm)
{
    float r = u * 0.82f, th = live ? tm * 0.9f : 0.25f;
    uint16_t pc = rgb(34, 38, 50);
    disc(cx, cy, r, rgb(244, 246, 250));
    float pent[10];
    for (int k = 0; k < 5; k++) {
        float a = th + k * 1.2566371f - 1.5707963f;
        pent[2 * k] = cx + cosf(a) * r * 0.36f;
        pent[2 * k + 1] = cy + sinf(a) * r * 0.36f;
    }
    men_fill_poly(pent, 5, pc);
    static const float PR[6] = {0.97f, 0.8f, 0.68f, 0.8f, 0.97f, 0.97f}, PA[6] = {-0.44f, -0.28f, 0, 0.28f, 0.44f, 0};
    for (int k = 0; k < 5; k++) {
        float a = th + k * 1.2566371f - 1.5707963f, q[12];
        bar(cx + cosf(a) * r * 0.36f, cy + sinf(a) * r * 0.36f, cx + cosf(a) * r * 0.7f, cy + sinf(a) * r * 0.7f,
            u * 0.07f + 0.6f, pc);
        for (int i = 0; i < 6; i++) {
            q[2 * i] = cx + cosf(a + PA[i]) * r * PR[i];
            q[2 * i + 1] = cy + sinf(a + PA[i]) * r * PR[i];
        }
        men_fill_poly(q, 6, pc);
    }
}

// a checkered flag on a pole (it waves)
static void SYM_FN sym_racing(float cx, float cy, float u, int s, bool live, float tm)
{
    int nc = s < 50 ? 4 : 5, nr = s < 50 ? 3 : 4;
    float x0 = cx - u * 0.44f, x1 = cx + u * 0.82f, y0 = cy - u * 0.78f, y1 = cy + u * 0.16f,
          ph = live ? tm * 5.0f : 0.9f;
    bar(cx - u * 0.56f, cy + u * 0.88f, cx - u * 0.5f, cy - u * 0.82f, u * 0.11f + 1, rgb(214, 218, 228));
    disc(cx - u * 0.5f, cy - u * 0.86f, u * 0.09f + 0.5f, rgb(255, 214, 80));
    for (int c = 0; c < nc; c++) {
        float xa = x0 + (x1 - x0) * c / nc, xb = x0 + (x1 - x0) * (c + 1) / nc;
        float fa = (float)c / nc, fb = (float)(c + 1) / nc;
        float wa = sinf(fa * 4.4f - ph) * u * 0.09f * (0.3f + fa), wb = sinf(fb * 4.4f - ph) * u * 0.09f * (0.3f + fb);
        for (int r = 0; r < nr; r++) {
            float ya = y0 + (y1 - y0) * r / nr, yb = y0 + (y1 - y0) * (r + 1) / nr;
            float q[8] = {xa, ya + wa, xb, ya + wb, xb, yb + wb, xa, yb + wa};
            men_fill_poly(q, 4, (c + r) & 1 ? rgb(22, 22, 30) : rgb(246, 246, 250));
        }
    }
}

// a tilted board with a ball, between two curved arrows (the board rocks, the ball rolls)
static void SYM_FN sym_tilt(float cx, float cy, float u, uint16_t w, bool live, float tm)
{
    float a = -0.36f + (live ? sinf(tm * 2.4f) * 0.12f : 0), ca = cosf(a), sa = sinf(a), L = u * 0.6f, th = u * 0.12f;
    float bx = cx, by = cy + u * 0.1f, aw = u * 0.13f + 0.6f;
    arc_arrow(cx, cy + u * 0.04f, u * 0.74f, 3.32f, 4.45f, aw, w);  // the empty corners: up at the left,
    arc_arrow(cx, cy + u * 0.04f, u * 0.74f, 0.18f, 1.31f, aw, w);  // down at the right (one way round)
    bar(bx - ca * L, by - sa * L, bx + ca * L, by + sa * L, th * 2, w);
    disc(bx - ca * L, by - sa * L, th, w);
    disc(bx + ca * L, by + sa * L, th, w);
    float pos = (live ? -0.36f + sinf(tm * 2.4f - 1.2f) * 0.2f : -0.4f) * L * 1.6f, rb = u * 0.17f;
    disc(bx + ca * pos + sa * (th + rb), by + sa * pos - ca * (th + rb), rb, rgb(40, 44, 90));
}

// the sun over the waves (the waves move)
static void SYM_FN sym_sea(float cx, float cy, float u, bool live, float tm)
{
    float hy = cy + u * 0.06f, sr = u * 0.46f, p[26];
    uint16_t sun = rgb(255, 238, 150);
    for (int k = 0; k < 5; k++) {
        float a = -2.62f + k * 0.52f;
        bar(cx + cosf(a) * u * 0.58f, hy + sinf(a) * u * 0.58f, cx + cosf(a) * u * 0.78f, hy + sinf(a) * u * 0.78f,
            u * 0.08f + 0.6f, sun);
    }
    for (int i = 0; i <= 12; i++) {
        float a = 3.14159265f + i * 3.14159265f / 12;
        p[2 * i] = cx + cosf(a) * sr;
        p[2 * i + 1] = hy + sinf(a) * sr;
    }
    men_fill_poly(p, 13, sun);
    float ph = live ? tm * 3.0f : 0.6f;
    for (int k = 0; k < 2; k++) {
        float wy = hy + u * (0.24f + 0.3f * k), amp = u * 0.08f, ww = u * 0.12f + 0.6f;
        uint16_t c = k ? rgb(255, 255, 255) : rgb(196, 232, 255);
        float px = cx - u * 0.8f, py = wy + sinf(ph + k * 1.7f) * amp;
        for (int i = 1; i <= 10; i++) {
            float qx = cx - u * 0.8f + u * 1.6f * i / 10, qy = wy + sinf(ph + k * 1.7f + i * 0.95f) * amp;
            bar(px, py, qx, qy, ww, c);
            disc(qx, qy, ww * 0.5f, c);
            px = qx;
            py = qy;
        }
    }
}

// the moon and stars (the stars twinkle)
static void SYM_FN sym_calm(float cx, float cy, float u, bool live, float tm)
{
    disc(cx - u * 0.12f, cy + u * 0.06f, u * 0.56f, rgb(255, 240, 176));
    hole(cx + u * 0.14f, cy - u * 0.14f, u * 0.47f);
    static const float SP[3][3] = {{0.46f, -0.52f, 0.2f}, {0.68f, 0.12f, 0.13f}, {-0.02f, -0.8f, 0.11f}};
    for (int i = 0; i < 3; i++) {
        float r = SP[i][2] * u * (live ? 0.8f + 0.3f * sinf(tm * 3.0f + i * 2.1f) : 1.0f);
        men_star(cx + SP[i][0] * u, cy + SP[i][1] * u, r < 1 ? 1 : r, rgb(255, 255, 236));
    }
}

// a rocket that flies up to the right (its flame flickers), and stars
static void SYM_FN sym_space(float cx, float cy, float u, bool live, float tm)
{
    static const float SD[3][3] = {{-0.72f, -0.5f, 0.07f}, {0.7f, 0.6f, 0.06f}, {-0.3f, -0.84f, 0.05f}};
    for (int i = 0; i < 3; i++) {
        disc(cx + SD[i][0] * u, cy + SD[i][1] * u, SD[i][2] * u + 0.5f, rgb(230, 236, 255));
    }
    float fl = live ? 0.34f + 0.1f * sinf(tm * 23.0f) + 0.06f * sinf(tm * 37.0f) : 0.34f;
    xf_t f = xf(cx - u * 0.04f, cy + u * 0.04f, u * 0.98f, 0.785f + (live ? sinf(tm * 9.0f) * 0.03f : 0));
    float out[6] = {-0.16f, 0.4f, 0.16f, 0.4f, 0, 0.44f + fl}, in[6] = {-0.08f, 0.4f, 0.08f, 0.4f, 0, 0.4f + fl * 0.6f};
    xpoly(&f, out, 3, rgb(255, 140, 40));
    xpoly(&f, in, 3, rgb(255, 230, 100));
    static const float FL[8] = {-0.2f, 0.04f, -0.44f, 0.36f, -0.44f, 0.56f, -0.17f, 0.4f},
                       FR[8] = {0.2f, 0.04f, 0.44f, 0.36f, 0.44f, 0.56f, 0.17f, 0.4f};
    xpoly(&f, FL, 4, rgb(236, 64, 76));
    xpoly(&f, FR, 4, rgb(236, 64, 76));
    static const float BODY[22] = {0,     -0.84f, 0.13f, -0.66f, 0.21f, -0.42f, 0.24f, -0.1f,  0.22f,  0.22f,  0.16f,
                                   0.44f, -0.16f, 0.44f, -0.22f, 0.22f, -0.24f, -0.1f, -0.21f, -0.42f, -0.13f, -0.66f};
    xpoly(&f, BODY, 11, rgb(240, 242, 250));
    static const float NOSE[10] = {0, -0.84f, 0.13f, -0.66f, 0.17f, -0.56f, -0.17f, -0.56f, -0.13f, -0.66f};
    xpoly(&f, NOSE, 5, rgb(236, 64, 76));
    xdisc(&f, 0, -0.24f, 0.14f, rgb(40, 50, 96));
    xdisc(&f, 0, -0.24f, 0.09f, rgb(120, 206, 255));
}

// a light bulb (its light pulses)
static void SYM_FN sym_learn(float cx, float cy, float u, bool live, float tm)
{
    float gy = cy - u * 0.16f, ray = live ? 0.04f * sinf(tm * 4.0f) : 0;
    uint16_t glass = rgb(255, 226, 84);
    for (int k = 0; k < 5; k++) {
        float a = -2.62f + k * 0.52f, ca = cosf(a), sa = sinf(a);
        bar(cx + ca * u * 0.6f, gy + sa * u * 0.6f, cx + ca * u * (0.74f + ray), gy + sa * u * (0.74f + ray),
            u * 0.08f + 0.6f, rgb(255, 238, 150));
    }
    disc(cx, gy, u * 0.46f, glass);
    float nk[8] = {cx - u * 0.32f, gy + u * 0.3f,  cx + u * 0.32f, gy + u * 0.3f,
                   cx + u * 0.2f,  cy + u * 0.42f, cx - u * 0.2f,  cy + u * 0.42f};
    men_fill_poly(nk, 4, glass);
    uint16_t lt = rgb(214, 218, 228), dg = rgb(124, 130, 152);
    gfx_fill_rect((int)lroundf(cx - u * 0.22f), (int)lroundf(cy + u * 0.42f), (int)lroundf(u * 0.44f),
                  (int)lroundf(u * 0.12f) + 1, lt);
    gfx_fill_rect((int)lroundf(cx - u * 0.2f), (int)lroundf(cy + u * 0.54f), (int)lroundf(u * 0.4f),
                  (int)lroundf(u * 0.1f) + 1, dg);
    gfx_fill_rect((int)lroundf(cx - u * 0.19f), (int)lroundf(cy + u * 0.64f), (int)lroundf(u * 0.38f),
                  (int)lroundf(u * 0.1f) + 1, lt);
    disc(cx, cy + u * 0.77f, u * 0.1f, dg);
    disc(cx - u * 0.17f, gy - u * 0.17f, u * 0.09f + 0.4f, rgb(255, 255, 240));  // the shine
}

// a speech bubble with three dots (they hop in turn)
static void SYM_FN sym_talk(float cx, float cy, float u, bool live, float tm)
{
    uint16_t wc = rgb(250, 252, 255);
    float y1 = cy + u * 0.36f;
    rbox(cx - u * 0.82f, cy - u * 0.64f, cx + u * 0.82f, y1, u * 0.3f, wc);
    float tail[6] = {cx - u * 0.48f, y1 - 1, cx - u * 0.1f, y1 - 1, cx - u * 0.58f, cy + u * 0.8f};
    men_fill_poly(tail, 3, wc);
    for (int i = 0; i < 3; i++) {
        float hop = live ? fmaxf(0, sinf(tm * 6.0f - i * 0.9f)) * u * 0.12f : 0;
        disc(cx + (i - 1) * u * 0.4f, cy - u * 0.14f - hop, u * 0.12f, rgb(40, 110, 220));
    }
}

// a heart with a white edge (it beats)
static void SYM_FN sym_heart(float cx, float cy, float u, bool live, float tm)
{
    float sc = 1.0f;
    if (live) {
        float p = fmodf(tm, 1.2f);
        sc += 0.08f * expf(-(p - 0.1f) * (p - 0.1f) / 0.004f) + 0.05f * expf(-(p - 0.34f) * (p - 0.34f) / 0.004f);
    }
    heart(cx, cy - u * 0.06f, u * 0.86f * sc, rgb(255, 255, 255));
    heart(cx, cy - u * 0.03f, u * 0.7f * sc, rgb(232, 36, 72));
    disc(cx - u * 0.34f * sc, cy - u * 0.3f * sc, u * 0.1f * sc + 0.4f, rgb(255, 160, 184));
}

// a flame (it flickers)
static void SYM_FN sym_new(float cx, float cy, float u, bool live, float tm)
{
    float sw = live ? sinf(tm * 7.0f) * 0.1f + sinf(tm * 11.3f) * 0.05f : 0.06f,
          tip = live ? 0.06f * sinf(tm * 13.0f) : 0;
    flame(cx, cy + u * 0.34f, u * 0.5f, u * (1.16f + tip), sw * u, rgb(255, 120, 36));
    flame(cx, cy + u * 0.46f, u * 0.3f, u * (0.66f + tip * 0.6f), -sw * u * 0.6f, rgb(255, 214, 80));
}

// the main menu: four tiles
static void SYM_FN sym_top(float cx, float cy, float u, uint16_t w)
{
    for (int i = 0; i < 4; i++) {
        float qx = cx + ((i & 1) ? 0.08f : -0.72f) * u, qy = cy + ((i >> 1) ? 0.08f : -0.72f) * u;
        rbox(qx, qy, qx + u * 0.64f, qy + u * 0.64f, u * 0.14f, w);
    }
}

// the symbol of a group look, in the box x, y, s, s; bg = the tile colour behind the middle (the first 8 looks);
// tm = icon_time() on the middle (selected) tile: the small motion, < 0: still. One copy for both menus.
void __attribute__((noinline)) SYM_FN men_cat_symbol(int cat, int x, int y, int s, uint16_t bg,
                                                     float tm)  // cat: the look (ST_*)
{
    float cx = x + s * 0.5f, cy = y + s * 0.5f, u = s * 0.5f;
    uint16_t w = rgb(236, 238, 248), dk = rgb(46, 50, 70);
    bool live = tm >= 0;
    switch (cat) {
    case CAT_GAMES:
        sym_games(cx, cy, u, w, dk);
        break;
    case CAT_UTIL:
        sym_util(cx, cy, u, w, dk, bg);
        break;
    case CAT_SETTINGS:
        sym_settings(cx, cy, u, w, bg);
        break;
    case ST_QUEST:
        sym_quest(cx, cy, u);
        break;
    case ST_MUSIC:
        sym_music(cx, cy, u, w);
        break;
    case ST_KIDS:
        sym_kids(cx, cy, u, dk);
        break;
    case ST_ACTION:
        sym_action(cx, cy, u);
        break;
    case ST_SILLY:
        sym_silly(cx, cy, u, dk, live, tm);
        break;
    case ST_DUO:
        sym_duo(cx, cy, u, live, tm);
        break;
    case ST_PARTY:
        sym_party(cx, cy, u, live, tm);
        break;
    case ST_RETRO:
        sym_retro(cx, cy, s, live, tm);
        break;
    case ST_PUZZLE:
        sym_puzzle(cx, cy, u, live, tm);
        break;
    case ST_BOARD:
        sym_board(cx, cy, u, live, tm);
        break;
    case ST_SPORTS:
        sym_sports(cx, cy, u, live, tm);
        break;
    case ST_RACING:
        sym_racing(cx, cy, u, s, live, tm);
        break;
    case ST_TILT:
        sym_tilt(cx, cy, u, w, live, tm);
        break;
    case ST_SEA:
        sym_sea(cx, cy, u, live, tm);
        break;
    case ST_CALM:
        sym_calm(cx, cy, u, live, tm);
        break;
    case ST_SPACE:
        sym_space(cx, cy, u, live, tm);
        break;
    case ST_LEARN:
        sym_learn(cx, cy, u, live, tm);
        break;
    case ST_TALK:
        sym_talk(cx, cy, u, live, tm);
        break;
    case ST_HEART:
        sym_heart(cx, cy, u, live, tm);
        break;
    case ST_NEW:
        sym_new(cx, cy, u, live, tm);
        break;
    case ST_TOP:
        sym_top(cx, cy, u, w);
        break;
    default:
        men_star(cx, cy + u * 0.04f, u * 0.92f, rgb(255, 238, 120));
        men_star(cx, cy + u * 0.04f, u * 0.55f, rgb(255, 250, 200));
        break;
    }
}

// The title of a level: "TFN OS" (the top level), the name of a group, or the path of a group in a group
// ("GAMES / QUESTS"). up: only the path of its parents (the round menu shows it small above the name; "" for a
// group on the top level). When fits() says that the path is too wide, the first names go and a "<" says that
// there is more above: "< QUESTS / SHORT", "< SHORT".
void men_level_path(int g, bool up, char *o, int n, bool (*fits)(const char *))
{
    const char *nm[G_MAX + 2];
    int d = 0;
    o[0] = 0;
    if (g < 0) {
        if (!up) {
            snprintf(o, (size_t)n, "TFN OS");
        }
        return;
    }
    for (int p = up ? men_gparent(g) : g; p >= 0 && d < G_MAX + 2; p = p == LV_FAV ? -1 : men_gparent(p)) {
        nm[d++] = men_gname(p);
    }
    for (int k = 0; k < d; k++) {
        int m = snprintf(o, (size_t)n, "%s", k ? "< " : "");
        for (int i = d - 1 - k; i >= 0 && m < n; i--) {
            m += snprintf(o + m, (size_t)(n - m), "%s%s", nm[i], i ? " / " : "");
        }
        if (fits(o)) {
            return;
        }
    }
}
// where the BACK of the level now goes: "TO GAMES" (the group above), "TO THE MENU" (the top level)
void men_back_to(char *o, int n)
{
    int p = men_gparent(men_level);
    if (p >= 0) {
        snprintf(o, (size_t)n, "TO %s", men_gname(p));
    } else {
        snprintf(o, (size_t)n, "TO THE MENU");
    }
}

// a group tile (or the BACK tile when back is true: the look of the level that it goes to); live: the middle tile
void men_draw_cat_tile(int cat, int x, int y, int s, bool pressed, bool back, bool live)
{
    uint8_t a[3], b[3];
    int st = back ? men_gstyle_up(cat) : men_gstyle(cat);
    men_cat_colors(st, a, b);
    gfx_fill_rect(x - 3, y - 3, s + 6, s + 6, pressed ? THEME_MENU_TILE_PRESSED : THEME_MENU_TILE_EDGE);
    for (int k = 0; k < s; k += 4) {
        gfx_fill_rect(x, y + k, s, s - k < 4 ? s - k : 4, men_grad(a, b, k, s));
    }
    men_tile_set(a, b, y, s, 4);
    uint16_t mid = men_grad(a, b, s / 2, s);
    gfx_clip_box(x, y, x + s, y + s);
    if (back) {
        // a big arrow to the left, and the symbol of the level above small
        float cx = x + s * 0.5f, cy = y + s * 0.42f, u = s * 0.3f;
        float p[14] = {
                cx - u,         cy,     cx - u * 0.1f,  cy - u * 0.85f, cx - u * 0.1f,  cy - u * 0.35f, cx + u,
                cy - u * 0.35f, cx + u, cy + u * 0.35f, cx - u * 0.1f,  cy + u * 0.35f, cx - u * 0.1f,  cy + u * 0.85f};
        men_fill_poly(p, 7, rgb(236, 238, 248));
        int ss = s / 4;
        men_cat_symbol(st, x + s - ss - 4, y + s - ss - 4, ss, men_grad(a, b, s - ss / 2 - 4, s), -1);
    } else {
        int ss = s * 64 / 100;
        men_cat_symbol(st, x + (s - ss) / 2, y + (s - ss) / 2 - s / 14, ss, mid, live ? icon_time() : -1);
        char t[8];
        snprintf(t, sizeof(t), "%d", men_cat_count[cat]);
        int sc = s >= 90 ? 2 : 1;
        if (gfx_round()) {
            ui_text(sc == 2 ? &KF_NAME : &KF_LABEL, x + s - ui_text_w(sc == 2 ? &KF_NAME : &KF_LABEL, t) - 10,
                    y + s - 9, t, THEME_MENU_TILE_COUNT);  // (the round menu: the smooth font)
        } else {
            gfx_text(x + s - gfx_text_width(t, sc) - 5, y + s - 8 * sc - 3, t, sc, THEME_MENU_TILE_COUNT);
        }
    }
    gfx_clip_box_off();
    if (pressed) {
        gfx_darken_rect(x, y, s, s, 1);
    }
}

// The WiFi signal while the board is on a WiFi network: 4 bars (the lit ones: the strength). x = the right edge,
// y = the top (11 px high). Returns the left edge of what it drew (x: nothing).
int men_draw_wifi(int x, int y)
{
    int rssi = wifi_rssi();
    if (!rssi) {
        return x;
    }
    int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    int lx = x - 15;
    for (int b = 0; b < 4; b++) {
        int h = 3 + b * 2 + (b == 3);
        gfx_fill_rect(lx + b * 4, y + 11 - h, 3, h, b < bars ? THEME_MENU_WIFI_ON : THEME_MENU_WIFI_OFF);
    }
    return lx - 1;
}

// The battery: an icon with its level (green, yellow, red), a bolt while it charges, and the
// percent at its left. x = the right edge, y = the top. Nothing when the board has no battery.
// Returns the left edge of what it drew (x: nothing).
int men_draw_battery(int x, int y)
{
    bool chg = false;
    int pct = board_battery(&chg, NULL);
    if (pct < 0) {
        return x;
    }
    uint16_t edge = THEME_MENU_BAT_EDGE;
    uint16_t c = pct > 40 ? THEME_MENU_BAT_OK : pct > 15 ? THEME_MENU_BAT_WARN : THEME_MENU_BAT_LOW;
    int bx = x - 24;
    gfx_fill_rect(bx - 1, y - 1, 26, 13, THEME_MENU_BAT_PLATE);  // a dark plate under it
    gfx_frame_rect(bx, y, 22, 11, 1, edge);
    gfx_fill_rect(bx + 22, y + 3, 2, 5, edge);
    int w = (18 * pct + 50) / 100;
    if (w > 0) {
        gfx_fill_rect(bx + 2, y + 2, w, 7, c);
    }
    if (chg) {
        // a small bolt
        uint16_t b = THEME_MENU_BAT_BOLT;
        gfx_fill_rect(bx + 11, y + 1, 2, 4, b);
        gfx_fill_rect(bx + 9, y + 5, 5, 1, b);
        gfx_fill_rect(bx + 9, y + 6, 2, 4, b);
    }
    char t[16];
    snprintf(t, sizeof(t), "%d%%", pct);
    if (gfx_round()) {  // (the round menu: the smooth font)
        const int tw = ui_text_w(&KF_LABEL, t);
        ui_text(&KF_LABEL, bx - 5 - tw, y + 10, t, pct > 15 ? edge : c);
        return bx - 5 - tw;
    }
    gfx_text(bx - 4 - gfx_text_width(t, 1), y + 2, t, 1, pct > 15 ? edge : c);
    return bx - 4 - gfx_text_width(t, 1);
}

// the battery, and the WiFi signal at its left; returns the left edge
int men_draw_status(int x, int y)
{
    int l = men_draw_battery(x, y);
    return men_draw_wifi(l == x ? x : l - 7, y);
}

// the badge of the unread notices of an app (notify.h): a red pill with the count at the top right of its icon
void men_badge(const app_t *a, int x, int y, int s)
{
    const int n = a && a->name ? notify_unread(a->name) : 0;
    if (n <= 0) {
        return;
    }
    char t[8];
    if (n > 99) {
        snprintf(t, sizeof t, "99+");
    } else {
        snprintf(t, sizeof t, "%d", n);
    }
    const kfont_t *f = s >= 90 ? &KF_NAME : &KF_LABEL;
    const int r = s / 8 + 5, tw = ui_text_w(f, t), w = tw + 10 > 2 * r ? tw + 10 : 2 * r;
    const int cx = x + s - w / 2 + 2, cy = y + r - 2;
    ui_rrect(cx - w / 2 - 2, cy - r - 2, w + 4, 2 * r + 4, r + 2, THEME_MENU_BADGE_EDGE);
    ui_rrect(cx - w / 2, cy - r, w, 2 * r, r, THEME_MENU_BADGE_BG);
    ui_text_c(f, cx, cy + f->asc / 2, t, THEME_MENU_BADGE_TEXT);
}
