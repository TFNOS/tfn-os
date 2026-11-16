#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set is in each app; all are static, and an unused one gives no warning)
#define IC_FN static __attribute__((unused))
static struct {
    uint16_t *fb;
    int w, h, c[4];
} ic_keep __attribute__((unused));
// start an icon: tfdraw draws on the screen and only in the box; ic_end gives tfdraw its old target and clip back
IC_FN void ic_begin(int x, int y, int s)
{
    ic_keep.fb = tfd_fb;
    ic_keep.w = tfd_w;
    ic_keep.h = tfd_h;
    ic_keep.c[0] = tfd_cx0;
    ic_keep.c[1] = tfd_cy0;
    ic_keep.c[2] = tfd_cx1;
    ic_keep.c[3] = tfd_cy1;
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
}
IC_FN void ic_end(void)
{
    tfd_target(ic_keep.fb, ic_keep.w, ic_keep.h);
    tfd_clip(ic_keep.c[0], ic_keep.c[1], ic_keep.c[2], ic_keep.c[3]);
}
// the rows j0 .. j1 - 1 of the round tile (the corner radius of the menu frame), a gradient from c0 to c1
IC_FN void ic_band(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    int r = s / 8 + 2;
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        uint16_t c = tfd_mix(c0, c1, j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        if (dy <= 0) {
            tfd_hspan(x, x + s, y + j, c, 256);
            continue;
        }
        int in = tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy)));
        tfd_hspan(x + in + 1, x + s - in - 1, y + j, c, 256);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, y + j, c, a);
            tfd_px(x + s - 1 - k, y + j, c, a);
        }
    }
}
// the box inside the round corners (a shape in it does not cover a corner)
IC_FN void ic_inner(int x, int y, int s)
{
    int k = (s / 8 + 2) * 3 / 10 + 1;
    tfd_clip(x + k, y + k, x + s - k, y + s - k);
}
// a filled ellipse with a smooth edge
IC_FN void ic_oval(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = fabsf(yy + 0.5f - cy), v = ry + 0.5f - d;
        if (v <= 0) {
            continue;
        }
        float q = d < ry ? d / ry : 1, hw = rx * sqrtf(1 - q * q), l = cx - hw, r = cx + hw;
        int aa = v >= 1 ? a : (int)(a * v);
        int il = tfd_ce(l), ir = tfd_fl(r);
        if (il > ir) {
            tfd_px(ir, yy, c, (int)(aa * (r - l)));
            continue;
        }
        tfd_hspan(il, ir, yy, c, aa);
        tfd_px(il - 1, yy, c, (int)(aa * (il - l)));
        tfd_px(ir, yy, c, (int)(aa * (r - ir)));
    }
}
// a ball in the light from the upper left: the dark rim, the body, the shine
IC_FN void ic_ball(float cx, float cy, float r, uint16_t dark, uint16_t body, uint16_t shine)
{
    tfd_disc(cx, cy, r, dark, 256);
    tfd_disc(cx - r * 0.1f, cy - r * 0.12f, r * 0.84f, body, 256);
    tfd_disc(cx - r * 0.34f, cy - r * 0.38f, r * 0.27f, shine, 200);
}
// a shape of n points (x, y pairs round 0, 0), turned by ang, k times bigger, at cx, cy
IC_FN void ic_shape(const float *p, int n, float cx, float cy, float k, float ang, uint16_t c, int a)
{
    float q[64], cs = fcos(ang) * k, sn = fsin(ang) * k;
    if (n > 32) {
        n = 32;
    }
    for (int i = 0; i < n; i++) {
        q[2 * i] = cx + p[2 * i] * cs - p[2 * i + 1] * sn;
        q[2 * i + 1] = cy + p[2 * i] * sn + p[2 * i + 1] * cs;
    }
    tfd_poly(q, n, c, a);
}
// A store app shows the frames of icon.bin, with at most 255 colours for all 16 frames: mk_app.py drops the low bits
// of each colour until they fit (most icons: 3 bits of red, 4 of green, 3 of blue). A colour on that grid stays as it
// is: ic_g gives the nearest one. ic_dband makes a gradient of grid colours with an ordered dither (few colours, no
// bands).
IC_FN uint16_t ic_g(int r, int g, int b)
{
    r = (r + 16) / 32 * 32;
    g = (g + 8) / 16 * 16;
    b = (b + 16) / 32 * 32;
    return rgb((uint8_t)(r > 224 ? 224 : r), (uint8_t)(g > 240 ? 240 : g), (uint8_t)(b > 224 ? 224 : b));
}
IC_FN void ic_dband(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    static const uint8_t IC_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int r0, g0, b0, r1, g1, b1, r = s / 8 + 2;
    tfd_unrgb(c0, &r0, &g0, &b0);
    tfd_unrgb(c1, &r1, &g1, &b1);
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        int yy = y + j;
        if (yy < tfd_cy0 || yy >= tfd_cy1 || (unsigned)yy >= (unsigned)tfd_h) {
            continue;
        }
        // (the colour of the row in 1/16 steps of a colour value; a grid step is 32 = 512 for red and blue, 16 = 256
        // for green)
        int t = j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0;
        int fr = r0 * 16 + ((r1 - r0) * 16 * t >> 8), fg = g0 * 16 + ((g1 - g0) * 16 * t >> 8),
            fb = b0 * 16 + ((b1 - b0) * 16 * t >> 8);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        int in = dy > 0 ? tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy))) : -1;
        uint16_t *row = tfd_fb + yy * tfd_w;
        for (int i = in + 1; i < s - in - 1; i++) {
            int xx = x + i;
            if (xx < tfd_cx0 || xx >= tfd_cx1 || (unsigned)xx >= (unsigned)tfd_w) {
                continue;
            }
            int th = IC_BAYER[(j & 3) * 4 + (i & 3)];
            int lr = (fr >> 9) + (((fr >> 5) & 15) > th), lg = (fg >> 8) + (((fg >> 4) & 15) > th),
                lb = (fb >> 9) + (((fb >> 5) & 15) > th);
            row[xx] = rgb((uint8_t)(lr > 7 ? 224 : lr * 32), (uint8_t)(lg > 15 ? 240 : lg * 16),
                          (uint8_t)(lb > 7 ? 224 : lb * 32));
        }
        uint16_t c = ic_g(fr >> 4, fg >> 4, fb >> 4);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, yy, c, a);
            tfd_px(x + s - 1 - k, yy, c, a);
        }
    }
}
// a span of one row from l to r (in pixels, with soft ends)
IC_FN void ic_span(float l, float r, int yy, uint16_t c, int a)
{
    if (r <= l) {
        return;
    }
    int il = tfd_ce(l), ir = tfd_fl(r);
    if (il > ir) {
        tfd_px(ir, yy, c, (int)(a * (r - l)));
        return;
    }
    tfd_hspan(il, ir, yy, c, a);
    tfd_px(il - 1, yy, c, (int)(a * (il - l)));
    tfd_px(ir, yy, c, (int)(a * (r - ir)));
}
// the ring of an ellipse (rx, ry: the outer radii; w: its width), made of spans: cheap also when it is big
IC_FN void ic_ering(float cx, float cy, float rx, float ry, float w, uint16_t c, int a)
{
    float ix = rx - w, iy = ry - w;
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = yy + 0.5f - cy, qo = d / ry;
        if (qo * qo >= 1) {
            continue;
        }
        float ho = rx * sqrtf(1 - qo * qo), qi = iy > 0 ? d / iy : 2;
        if (qi * qi >= 1) {
            ic_span(cx - ho, cx + ho, yy, c, a);
            continue;
        }
        float hi = ix * sqrtf(1 - qi * qi);
        ic_span(cx - ho, cx - hi, yy, c, a);
        ic_span(cx + hi, cx + ho, yy, c, a);
    }
}
// FOUR SEASONS: the menu icon (included by app_seasons.c). Grandpa's kite flies over the valley: its tail of bows
// waves, it rises and dips; the season of the valley changes under it (a soft line goes left and right, the summer at
// one side, the autumn at the other, as a drag in the game). A loop of 2 s.
static void fs_icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s * 70 / 100, ic_g(64, 144, 224), ic_g(192, 224, 224));
    tfd_disc(x + 18 * u, y + 18 * u, 9 * u, ic_g(224, 224, 160), 90);
    tfd_disc(x + 18 * u, y + 18 * u, 6 * u, ic_g(224, 224, 128), 256);
    // the valley: two hills; the season line goes left and right (summer at the left, autumn at the right)
    float sl = x + (50 + 26 * fsin(an)) * u;
    static const uint8_t SUM[3][3] = {{96, 176, 64}, {64, 144, 48}, {32, 112, 48}},
                         AUT[3][3] = {{224, 160, 64}, {208, 112, 32}, {160, 80, 32}};
    for (int side = 0; side < 2; side++) {
        const uint8_t (*c)[3] = side ? AUT : SUM;
        tfd_clip(side ? (int)sl : x, y, side ? x + s : (int)sl, y + s);
        ic_band(x, y, s, s * 70 / 100, s, ic_g(c[1][0], c[1][1], c[1][2]), ic_g(c[2][0], c[2][1], c[2][2]));
        ic_oval(x + 26 * u, y + 76 * u, 40 * u, 16 * u, ic_g(c[0][0], c[0][1], c[0][2]), 256);
        ic_oval(x + 80 * u, y + 80 * u, 34 * u, 12 * u, ic_g(c[1][0], c[1][1], c[1][2]), 256);
        // (the tree on the hill takes the colour of its side)
        tfd_line(x + 30 * u, y + 66 * u, x + 30 * u, y + 56 * u, 2.4f * u, ic_g(96, 64, 32), 256);
        tfd_disc(x + 30 * u, y + 51 * u, 7.5f * u, ic_g(c[2][0], c[2][1], c[2][2]), 256);
        tfd_disc(x + 29 * u, y + 50 * u, 5.5f * u, ic_g(c[0][0], c[0][1], c[0][2]), 256);
    }
    tfd_clip(x, y, x + s, y + s);
    tfd_line(sl, y + 62 * u, sl, y + 98 * u, 1 * u, ic_g(224, 240, 224), 150);
    // the kite: it rises and dips, it leans; the string to the boy at the lower left
    float kx = x + 62 * u + 3 * u * fsin(an), ky = y + 30 * u + 4 * u * fcos(an), lean = 0.18f * fsin(an);
    tfd_line(x + 12 * u, y + 90 * u, kx - 1 * u, ky + 8 * u, 0.9f * u + 0.3f, ic_g(224, 224, 224), 230);
    // (the tail: bows on a wavy line)
    float px = kx, py = ky + 14 * u;
    for (int k = 1; k <= 6; k++) {
        float tx = kx - k * 3.6f * u, ty = ky + 14 * u + k * 5 * u + fsin(an * 2 - k * 0.9f) * 3.5f * u;
        tfd_line(px, py, tx, ty, 1 * u, ic_g(64, 64, 96), 256);
        if (k % 2 == 0) {
            float bw[] = {tx - 3 * u, ty - 2 * u, tx + 3 * u, ty + 2 * u,
                          tx + 3 * u, ty - 2 * u, tx - 3 * u, ty + 2 * u};
            tfd_poly(bw, 4, k % 4 ? ic_g(224, 64, 64) : ic_g(224, 192, 32), 256);
        }
        px = tx;
        py = ty;
    }
    static const float KT[] = {0, -16, 11, -2, 0, 14, -11, -2};
    static const float Q1[] = {0, -16, 11, -2, 0, -2};
    static const float Q2[] = {0, -2, -11, -2, 0, 14};
    ic_shape(KT, 4, kx + 1 * u, ky + 1.5f * u, u * 1.06f, lean, ic_g(64, 16, 32), 160);
    ic_shape(KT, 4, kx, ky, u, lean, ic_g(224, 48, 48), 256);
    ic_shape(Q1, 3, kx, ky, u, lean, ic_g(224, 192, 32), 256);
    ic_shape(Q2, 3, kx, ky, u, lean, ic_g(224, 192, 32), 256);
    float cs = fcos(lean), sn = fsin(lean);
    tfd_line(kx + 16 * sn * u, ky - 16 * cs * u, kx - 14 * sn * u, ky + 14 * cs * u, 1.2f * u, ic_g(96, 48, 16), 256);
    tfd_line(kx - 11 * cs * u + 2 * sn * u, ky - 11 * sn * u - 2 * cs * u, kx + 11 * cs * u + 2 * sn * u,
             ky + 11 * sn * u - 2 * cs * u, 1.2f * u, ic_g(96, 48, 16), 256);
    // (the boy with the string)
    tfd_disc(x + 12 * u, y + 84 * u, 2.6f * u, ic_g(224, 176, 128), 256);
    tfd_rrect((int)(x + 9 * u), (int)(y + 87 * u), (int)(6 * u), (int)(8 * u), (int)(2 * u), ic_g(64, 96, 192), 256);
    ic_end();
}
