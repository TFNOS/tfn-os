// TOON: the rooftop stage (a roof in Tel Aviv at night). Five depth layers, a line-scroll floor.

#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "toon.h"
#include "tfn/gfx.h"

static inline float hashf(float n)
{
    uint32_t h = (uint32_t)(int32_t)(n * 97.0f);
    h ^= h >> 15;
    h *= 2654435761u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (float)(h & 0xFFFFFF) / 16777216.0f;
}
static inline float lerpf(float a, float b, float u)
{
    return a + (b - a) * u;
}

// ---- colors
static const tcol_t STAR1 = TC(0xe8e0ff), STAR2 = TC(0x8a7cc0);
static const tcol_t MOON[4] = {TC(0x24164a), TC(0x30205c), TC(0xf2e8cc), TC(0xd8c9a4)};
static const tcol_t FARC[3] = {TC(0x1a1030), TC(0x241640), TC(0x2c1c4c)};
static const tcol_t WIN[3] = {TC(0xffcf6a), TC(0xe8964a), TC(0x7ad8ff)};
static const tcol_t AVI = TC(0xff3040);
static const tcol_t MIDC[4] = {TC(0x22142f), TC(0x1a0f26), TC(0x3a2650), TC(0x2c1c3e)};
static const tcol_t MWIN[3] = {TC(0xffb85a), TC(0xff8a6a), TC(0x9ad0ff)};
static const tcol_t NEON_R[3] = {TC(0xff4060), TC(0x7a1a40), TC(0x4a1028)},
                    NEON_C[3] = {TC(0x40e0f0), TC(0x145a68), TC(0x0e3440)};
static const tcol_t SIGNB = TC(0x140a18), POLE = TC(0x0e0a14);
static const tcol_t BULB[4] = {TC(0xffd060), TC(0xff6080), TC(0x60e0ff), TC(0xa0ff80)},
                    BULBH[4] = {TC(0x6a4a20), TC(0x6a2030), TC(0x1e5060), TC(0x3a6020)};
static const tcol_t WALL[4] = {TC(0x2a1e32), TC(0x3e3048), TC(0x5e4c6a), TC(0x2e2238)};
static const tcol_t HEAT[6] = {TC(0xe6e2da), TC(0xa8a4ae), TC(0x6e6a78), TC(0x1e2a5a), TC(0x8a8a96), TC(0x4a6ac0)};
static const tcol_t SPC[5] = {TC(0x1e1428), TC(0x4c3058), TC(0x3a2440), TC(0x22304a), TC(0x3a2a24)};
static const tcol_t PIGEON[3] = {TC(0x8a8496), TC(0x5c566a), TC(0xc86a3a)};
static const tcol_t FL[5] = {TC(0x2a2232), TC(0x2f2738), TC(0x1c1624), TC(0x3c3248), TC(0x201a2a)};
static const tcol_t SHADOWC = TC(0x120c18);

// ---- the sky bands (made once from the color stops)
#define SKY_N 16
static tcol_t s_skies[T_STAGE_N][SKY_N + 2], *s_sky = s_skies[0];
static bool s_sky_ok;
typedef struct {
    float y;
    uint32_t c;
} skystop_t;
static const skystop_t SKY_STOPS[T_STAGE_N][6] = {
        {{-150, 0x07041a}, {-95, 0x140a33}, {-62, 0x2a1048}, {-40, 0x4a1656}, {-26, 0x7a2258}, {-14, 0xa8385a}},
        {{-150, 0x1a1040}, {-95, 0x3a1a58}, {-62, 0x8a3060}, {-40, 0xd8604a}, {-26, 0xf09848}, {-14, 0xf8c070}},
        {{-150, 0x020414}, {-95, 0x06102a}, {-62, 0x0c1a40}, {-40, 0x14285a}, {-26, 0x1e3a70}, {-14, 0x2a4a80}},
};
static void sky_init_one(int k0)
{
    const skystop_t *ST = SKY_STOPS[k0];
    s_sky = s_skies[k0];
    s_sky[0] = TC(ST[0].c);
    for (int i = 0; i < SKY_N; i++) {
        float ym = -150 + 136.0f * (i + 0.5f) / SKY_N;
        int k = 0;
        while (k < 4 && ST[k + 1].y < ym) {
            k++;
        }
        float u = (ym - ST[k].y) / (ST[k + 1].y - ST[k].y);
        if (u < 0) {
            u = 0;
        }
        if (u > 1) {
            u = 1;
        }
        uint32_t a = ST[k].c, b = ST[k + 1].c;
        uint32_t r = (uint32_t)lerpf((float)(a >> 16), (float)(b >> 16), u),
                 g = (uint32_t)lerpf((float)((a >> 8) & 255), (float)((b >> 8) & 255), u),
                 bl = (uint32_t)lerpf((float)(a & 255), (float)(b & 255), u);
        s_sky[i + 1] = TC((r << 16) | (g << 8) | bl);
    }
    s_sky[SKY_N + 1] = TC(ST[5].c);
}
static void sky_init(void)
{
    for (int k = 0; k < T_STAGE_N; k++) {
        sky_init_one(k);
    }
    s_sky = s_skies[0];
    s_sky_ok = true;
}

static void sky(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    t_layer(v, cam, 0);
    t_rect(-600, -400, 1200, 250, s_sky[0]);
    for (int i = 0; i < SKY_N; i++) {
        float y0 = -150 + 136.0f * i / SKY_N;
        t_rect(-600, y0, 1200, 136.0f / SKY_N + 0.6f, s_sky[i + 1]);
    }
    t_rect(-600, -14, 1200, 100, s_sky[SKY_N + 1]);
    if (st->stage == T_STAGE_SHUK) {
        float sx = v->W * 0.62f, sy = v->base_y - v->H * 0.1f;
        t_ellipse(sx, sy, 22, 22, TC(0xf8a858));
        t_ellipse(sx, sy, 15, 15, TC(0xffc870));
        t_ellipse(sx, sy, 10, 10, TC(0xfff0b0));
        return;
    }
    for (int i = 0; i < 70; i++) {
        float x = (hashf(i * 1.7f) - 0.5f) * 600, y = -170 + hashf(i * 3.3f) * 135;
        if (y > -36) {
            continue;
        }
        float tw = hashf(i * 9.1f + floorf(st->t * 2 + i * 0.37f));
        if (tw < 0.2f) {
            continue;
        }
        t_rect(x, y, 0.9f, 0.9f, tw > 0.75f ? STAR1 : STAR2);
    }
    float mx = v->W * (st->stage == T_STAGE_PORT ? 0.72f : 0.3f),
          my = v->base_y - v->H * (st->stage == T_STAGE_PORT ? 0.24f : 0.18f);
    t_ellipse(mx, my, 15, 15, MOON[0]);
    t_ellipse(mx, my, 11.5f, 11.5f, MOON[1]);
    t_ellipse(mx, my, 8, 8, MOON[2]);
    t_ellipse(mx - 2.5f, my + 1.5f, 1.8f, 1.6f, MOON[3]);
    t_ellipse(mx + 2.2f, my - 2.4f, 1.2f, 1.1f, MOON[3]);
    t_ellipse(mx + 1, my + 3.5f, 0.9f, 0.9f, MOON[3]);
}

static bool off_x(float x0, float x1)
{
    float a = T.m.a * x0 + T.m.e, b = T.m.a * x1 + T.m.e;
    return (a < -8 && b < -8) || (a > T.w + 8 && b > T.w + 8);
}
static void windows(float x0, float top, float w, float step, float yend, float hx, float hy, float thr,
                    const tcol_t *wc, float size, bool twin)
{
    for (float wy = top; wy < yend; wy += step) {
        for (float wx = x0; wx < x0 + w; wx += step) {
            float h = hashf(wx * hx + wy * hy);
            if (h <= thr) {
                continue;
            }
            tcol_t c = twin ? (h > 0.97f ? wc[2] : h > 0.86f ? wc[1] : wc[0]) : wc[2];
            t_rect(wx, wy, size, size, c);
        }
    }
}
static void far_layer(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    t_layer(v, cam, 0.15f);
    float x = -360;
    for (int i = 0; x < 360; i++) {
        float w = 8 + floorf(hashf(i * 3.1f) * 14), h = 8 + floorf(hashf(i * 7.7f) * 30), top = -14 - h;
        if (off_x(x, x + w)) {
            x += w + (hashf(i * 1.9f) < 0.3f ? 3 : 0);
            continue;
        }
        t_rect(x, top, w, -top + 30, FARC[0]);
        t_rect(x, top, 1, -top + 30, FARC[1]);
        windows(x + 2, top + 3, w - 3, 3, -16, 13.1f, 7.3f, 0.7f, WIN, 1, true);
        x += w + (hashf(i * 1.9f) < 0.3f ? 3 : 0);
    }
    // the three Azrieli towers: round, triangle, square
    static const struct {
        float x, w, top;
        int shape;
    } AZ[] = {{-84, 14, -86, 0}, {-64, 14, -82, 1}, {-44, 13, -78, 2}};
    for (int k = 0; k < 3; k++) {
        float ax = AZ[k].x, aw = AZ[k].w, at = AZ[k].top;
        int sh = AZ[k].shape;
        if (sh == 1) {
            float p[8] = {ax, 20, ax, at + 7, ax + aw, at, ax + aw, 20};
            t_poly(p, 4, FARC[0]);
        } else {
            t_rect(ax, at, aw, 20 - at, FARC[0]);
        }
        if (sh == 0) {
            t_rect(ax + aw * 0.35f, at, aw * 0.3f, 20 - at, FARC[2]);
        } else {
            t_rect(ax, at + (sh == 1 ? 7 : 0), 1.2f, 20 - at, FARC[2]);
        }
        for (float wy = at + 5; wy < -16; wy += 3) {
            for (float wx = ax + 2; wx < ax + aw - 1; wx += 2) {
                if (hashf(wx * 5.3f + wy * 3.9f) > 0.55f) {
                    t_rect(wx, wy, 1, 1, WIN[2]);
                }
            }
        }
        t_rect(ax, at + (sh == 1 ? 7 : 0) + 1, aw, 0.8f, WIN[2]);
    }
    t_rect(150, -88, 12, 120, FARC[0]);
    t_rect(155.5f, -100, 1, 12, FARC[0]);
    for (float wy = -84; wy < -16; wy += 3) {
        for (float wx = 152; wx < 161; wx += 3) {
            if (hashf(wx * 2.1f + wy * 1.3f) > 0.6f) {
                t_rect(wx, wy, 1, 1, WIN[0]);
            }
        }
    }
    if (fsin(st->t * 5) > 0) {
        t_rect(155.2f, -101, 1.6f, 1.6f, AVI);
        t_rect(-77.8f, -87.6f, 1.4f, 1.4f, AVI);
    }
}

// Two Hebrew words for the neon signs, as small bitmaps (left to right on the screen).
static const char *HEB_PE[5] = {"####", "#..#", "##.#", "...#", "####"};
static const char *HEB_LAMED[5] = {"#...", "####", "...#", "..#.", ".#.."};
static const char *HEB_ALEF[5] = {"#..#", "##.#", ".##.", "#.##", "#..#"};
static const char *HEB_QOF[5] = {"####", "...#", "#..#", "#.#.", "#..."};
static const char *HEB_YOD[5] = {"##", ".#", "..", "..", ".."};
static const char *HEB_VAV[5] = {"##", ".#", ".#", ".#", ".#"};
static const char *HEB_SAMEKH[5] = {"####", "#..#", "#..#", "#..#", ".##."};
static void heb_word(const char *const *const *letters, int n, float cx, float cy, float px, tcol_t c)
{
    float total = 0;
    for (int i = 0; i < n; i++) {
        total += (float)((int)__builtin_strlen(letters[i][0]) + 1);
    }
    total -= 1;
    float x = cx - total * px / 2;
    for (int i = 0; i < n; i++) {
        int lw = (int)__builtin_strlen(letters[i][0]);
        for (int r = 0; r < 5; r++) {
            for (int k = 0; k < lw; k++) {
                if (letters[i][r][k] == '#') {
                    t_rect(x + k * px, cy - 2.5f * px + r * px, px, px, c);
                }
            }
        }
        x += (lw + 1) * px;
    }
}
static void mid_layer(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    static const struct {
        float x, w, top;
        int sign;
    } MID[] = {{-330, 56, -40, 0}, {-268, 44, -50, 0}, {-218, 62, -36, 0}, {-150, 44, -58, 1},
               {-100, 50, -34, 0}, {-42, 38, -44, 0},  {4, 58, -32, 0},    {70, 46, -48, 0},
               {124, 56, -40, 2},  {188, 48, -54, 0},  {244, 60, -36, 0},  {310, 50, -46, 0}};
    t_layer(v, cam, 0.45f);
    for (unsigned i = 0; i < sizeof MID / sizeof MID[0]; i++) {
        float bx = MID[i].x, bw = MID[i].w, bt = MID[i].top;
        if (off_x(bx - 2, bx + bw + 2)) {
            continue;
        }
        t_rect(bx, bt, bw, 60 - bt, MIDC[0]);
        t_rect(bx + bw - 4, bt, 4, 60 - bt, MIDC[1]);
        t_rect(bx - 1, bt - 1, bw + 2, 1.6f, MIDC[2]);
        for (float wy = bt + 5; wy < -14; wy += 6) {
            for (float wx = bx + 4; wx < bx + bw - 6; wx += 6) {
                float h = hashf(wx * 3.7f + wy * 11.1f);
                if (h > 0.74f) {
                    t_rect(wx, wy, 2.4f, 2.6f, h > 0.95f ? MWIN[2] : h > 0.86f ? MWIN[1] : MWIN[0]);
                }
            }
        }
        if (!MID[i].sign && hashf(bx) > 0.4f) {
            t_rect(bx + 6, bt - 7, 9, 6, MIDC[3]);
            t_rect(bx + 7, bt - 9, 7, 2, MIDC[3]);
        }
        if (!MID[i].sign && hashf(bx + 3) > 0.5f) {
            t_line(bx + bw - 10, bt, bx + bw - 10, bt - 14, 0.8f, MIDC[3]);
            t_line(bx + bw - 14, bt - 10, bx + bw - 6, bt - 10, 0.8f, MIDC[3]);
        }
        if (MID[i].sign) {
            bool red = MID[i].sign == 1;
            const tcol_t *N = red ? NEON_R : NEON_C;
            bool on = hashf(floorf(st->t * 7) + (red ? 0 : 50)) > (red ? 0.08f : 0.04f);
            float sx = bx + 3, sy = bt + 5, sw = bw - 6;
            t_rect(sx, sy, sw, 12, SIGNB);
            t_rect(sx + 0.5f, sy + 0.5f, sw - 1, 11, on ? N[1] : N[2]);
            t_rect(sx + 1.5f, sy + 1.5f, sw - 3, 9, SIGNB);
            if (red) {
                const char *const *w[5] = {HEB_LAMED, HEB_PE, HEB_ALEF, HEB_LAMED, HEB_PE};
                heb_word(w, 5, sx + sw / 2, sy + 6, 1.25f, on ? N[0] : N[2]);
            } else {
                const char *const *w[5] = {HEB_QOF, HEB_SAMEKH, HEB_VAV, HEB_YOD, HEB_QOF};
                heb_word(w, 5, sx + sw / 2, sy + 6, 1.25f, on ? N[0] : N[2]);
            }
        }
    }
}
static void lights(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    static const float POLES[4] = {-260, -90, 80, 250};
    const float top = -54, sway = fsin(st->t * 1.3f) * 1.2f;
    t_layer(v, cam, 0.7f);
    for (int k = 0; k < 4; k++) {
        t_rect(POLES[k] - 0.7f, top, 1.4f, 60, POLE);
    }
    for (int k = 0; k < 3; k++) {
        float x0 = POLES[k], x1 = POLES[k + 1];
        int n = (int)((x1 - x0) / 9 + 0.5f);
        float px = x0, py = top;
        for (int i = 1; i <= 12; i++) {
            float u = i / 12.0f, x = lerpf(x0, x1, u), y = top + (9 + sway) * 4 * u * (1 - u);
            t_line(px, py, x, y, 0.6f, POLE);
            px = x;
            py = y;
        }
        for (int i = 1; i < n; i++) {
            float u = (float)i / n, x = lerpf(x0, x1, u), y = top + (9 + sway) * 4 * u * (1 - u) + 1.4f;
            int c = (i + k * 3) % 4;
            if (t_unit() < 1.7f) {
                t_rect(x - 1.5f, y - 1.5f, 3, 3, BULBH[c]);
                t_rect(x - 0.5f, y - 0.5f, 1, 1, BULB[c]);
            } else {
                t_ellipse(x, y, 2.1f, 2.1f, BULBH[c]);
                t_ellipse(x, y, 1.05f, 1.05f, BULB[c]);
            }
        }
    }
}
static void spectator(float x, int i, float cheer, float t)
{
    tcol_t shirt = SPC[2 + (i % 3)];
    float bob = fabsf(fsin(t * 7 + i * 2)) * 1.6f * cheer;
    static const float HG[4] = {1, 0.92f, 1.05f, 0.96f};
    float s = 0.8f * HG[i % 4];
    t_push();
    t_translate(x, -10);
    t_scale(s, s);
    t_rect(-3.2f, -19, 2.6f, 19, SPC[0]);
    t_rect(0.6f, -19, 2.6f, 19, SPC[0]);
    float body[8] = {-4.6f, -19 - bob, 4.6f, -19 - bob, 5.0f, -34 - bob, -5.0f, -34 - bob};
    t_poly(body, 4, shirt);
    t_ellipse(0, -38.5f - bob, 3.6f, 4.1f, SPC[0]);
    if (i % 4 == 1) {
        t_rect(-4.2f, -42 - bob, 8.4f, 1.6f, SPC[0]);
        t_rect(-3.4f, -44 - bob, 6.4f, 2.4f, SPC[0]);
    }
    float up = cheer < 0 ? 0 : cheer > 1 ? 1 : cheer;
    for (int sd = -1; sd <= 1; sd += 2) {
        float sx = sd * 4.4f, sy = -32 - bob;
        float ex = sx + sd * lerpf(1.5f, 3.5f, up), ey = sy + lerpf(8, -6, up);
        float hx = ex + sd * lerpf(0.5f, 0.8f, up),
              hy = ey + lerpf(7, -7, up) + (up > 0.4f ? fsin(t * 14 + sd) * 1.5f : 0);
        t_line(sx, sy, ex, ey, 2.2f, shirt);
        t_line(ex, ey, hx, hy, 2.2f, shirt);
    }
    t_rect(3.6f, -33 - bob, 1.0f, 13, SPC[1]);
    t_ellipse(2.6f, -39 - bob, 1.0f, 3.0f, SPC[1]);
    t_pop();
}
static void back_roof(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    t_layer(v, cam, 0.85f);
    t_rect(-300, -23, 600, 14, WALL[1]);
    t_rect(-300, -24, 600, 2, WALL[2]);
    t_rect(-300, -12, 600, 3, WALL[0]);
    for (float x = -300; x < 300; x += 16) {
        t_rect(x, -22, 0.8f, 10, WALL[3]);
    }
    // solar water heaters
    for (int k = 0; k < 2; k++) {
        float hx = k ? -124 : -152;
        t_rect(hx + 1, -24, 1, 14, HEAT[4]);
        t_rect(hx + 16, -30, 1, 20, HEAT[4]);
        float fr[8] = {hx - 0.6f, -10.6f, hx + 18.6f, -10.6f, hx + 18.6f, -27.6f, hx - 0.6f, -18.6f};
        t_poly(fr, 4, HEAT[4]);
        float pn[8] = {hx, -11, hx + 18, -11, hx + 18, -27, hx, -19};
        t_poly(pn, 4, HEAT[3]);
        t_rect(hx + 6, -20, 0.8f, 9, HEAT[5]);
        t_rect(hx + 12, -23, 0.8f, 12, HEAT[5]);
        t_capsule(hx + 1, -32, hx + 17, -32, 3.6f, 3.6f, HEAT[2]);
        t_capsule(hx + 1.2f, -32.4f, hx + 16.8f, -32.4f, 3.0f, 3.0f, HEAT[1]);
        t_capsule(hx + 1.6f, -33.6f, hx + 16.4f, -33.6f, 1.3f, 1.3f, HEAT[0]);
    }
    // the dish and the water tank
    t_rect(174, -26, 1.2f, 16, HEAT[2]);
    t_push();
    t_translate(175, -29);
    t_rotate(-0.5f);
    t_ellipse(0, 0, 2.2f, 6, HEAT[1]);
    t_pop();
    t_rect(200, -26, 1.4f, 16, WALL[3]);
    t_rect(214, -26, 1.4f, 16, WALL[3]);
    t_rect(198, -46, 19, 21, WALL[2]);
    t_rect(198, -40, 19, 1.2f, WALL[1]);
    t_rect(198, -32, 19, 1.2f, WALL[1]);
    // the pigeon flies away at the first good hit
    float pa = st->pigeon_t;
    if (pa < 2) {
        float px = 52 + (pa > 0 ? pa * 70 : 0), py = -24.5f - (pa > 0 ? pa * 40 + pa * pa * 10 : 0),
              flap = pa > 0 ? fsin(st->t * 40) : 0;
        t_ellipse(px, py, 2.4f, 1.4f, PIGEON[0]);
        t_ellipse(px + 2, py - 1.4f, 1.1f, 1.1f, PIGEON[0]);
        t_rect(px + 3, py - 1.5f, 0.9f, 0.6f, PIGEON[2]);
        if (pa > 0) {
            t_tri(px - 1, py, px + 1, py, px - 0.5f, py - 4 * flap, PIGEON[1]);
        }
    }
    static const float SX[4] = {-96, 112, 126, 141};
    for (int i = 0; i < 4; i++) {
        spectator(SX[i], i, st->cheer, st->t);
    }
}
static inline float fp(float y)
{
    return 1 + 0.015f * y;
}
static void floor_layer(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    static const float ROWS[10] = {-10, -7, -4, -1, 2, 6, 11, 17, 24, 32};
    float lw = 0.6f * v->ps * cam->z;
    if (lw < 1) {
        lw = 1;
    }
    for (int k = 0; k < 9; k++) {
        float y0 = ROWS[k], y1 = ROWS[k + 1], q[8];
        t_proj(v, cam, fp(y0), -900, y0, &q[0], &q[1]);
        t_proj(v, cam, fp(y0), 900, y0, &q[2], &q[3]);
        t_proj(v, cam, fp(y1), 900, y1, &q[4], &q[5]);
        t_proj(v, cam, fp(y1), -900, y1, &q[6], &q[7]);
        t_identity();
        t_poly(q, 4, k % 2 ? FL[1] : FL[0]);
        t_line(q[0], q[1], q[2], q[3], lw, FL[2]);
    }
    for (float x = -900; x <= 900; x += 30) {
        float a0, a1, b0, b1;
        t_proj(v, cam, fp(-10), x, -10, &a0, &a1);
        t_proj(v, cam, fp(32), x, 32, &b0, &b1);
        if ((a0 < -4 && b0 < -4) || (a0 > v->iw + 4 && b0 > v->iw + 4)) {
            continue;
        }
        t_identity();
        t_line(a0, a1, b0, b1, 1, FL[4]);
    }
    // a puddle with neon in it
    t_layer(v, cam, fp(10));
    t_ellipse(62, 10, 20, 3.2f, FL[2]);
    t_ellipse(60, 9.6f, 16, 2.2f, FL[3]);
    bool on = hashf(floorf(st->t * 7)) > 0.08f;
    t_rect(50, 8.6f, 5, 0.9f, on ? NEON_R[1] : FL[2]);
    t_rect(51, 10.2f, 3, 0.7f, on ? NEON_R[1] : FL[2]);
    t_rect(68, 9, 4, 0.8f, NEON_C[1]);
}

// ================================================================ SHUK: the Carmel market at sunset
static const char *HEB_SHIN[5] = {"#.#.#", "#.#.#", "#.#.#", "#..##", "####."};
static const tcol_t FARS[3] = {TC(0x3a1e30), TC(0x4a2838), TC(0x5a3240)};
static const tcol_t WARM[3] = {TC(0xffc860), TC(0xff9840), TC(0xffe8a0)};
static const tcol_t WOOD[4] = {TC(0x2a180e), TC(0x4a2c18), TC(0x6a4224), TC(0x8a5a30)};
static const uint32_t FRUIT[6] = {0xff9a20, 0xe02a2a, 0x4a2a5a, 0x3a8a2a, 0xf8e040, 0xa01828};
static void shuk_far(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    (void)st;
    t_layer(v, cam, 0.15f);
    float x = -360;
    for (int i = 0; x < 360; i++) {
        float w = 14 + floorf(hashf(i * 3.1f + 5) * 16), h = 10 + floorf(hashf(i * 7.7f + 5) * 22), top = -14 - h;
        if (off_x(x, x + w)) {
            x += w;
            continue;
        }
        t_rect(x, top, w, -top + 30, FARS[i % 2]);
        t_rect(x, top - 1, w, 1.2f, FARS[2]);
        for (float wy = top + 4; wy < -16; wy += 7) {
            for (float wx = x + 3; wx < x + w - 4; wx += 6) {
                float hh = hashf(wx * 2.3f + wy * 5.1f);
                if (hh < 0.5f) {
                    continue;
                }
                tcol_t c = hh > 0.8f ? WARM[0] : FARS[0];
                t_rect(wx, wy, 2.4f, 3, c);
                t_ellipse(wx + 1.2f, wy, 1.2f, 1.0f, c);  // an arched window
            }
        }
        if (hashf(i * 4.4f) > 0.6f) {
            t_rect(x + w * 0.6f, top - 5, 5, 4, FARS[0]);
            t_rect(x + w * 0.6f + 1, top - 7, 3, 2, FARS[0]);
        }
        x += w;
    }
    // two palm trees against the sky
    static const float PX[2] = {-120, 96};
    for (int k = 0; k < 2; k++) {
        float bx = PX[k], ty = -64 - k * 6;
        t_line(bx, -14, bx + 3, ty, 1.4f, FARS[0]);
        for (int f = 0; f < 7; f++) {
            float a = -2.6f + f * 0.75f;
            t_line(bx + 3, ty, bx + 3 + fcos(a) * 11, ty + fsin(a) * 6 + 3, 1.0f, FARS[0]);
        }
    }
}
static void shuk_mid(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    t_layer(v, cam, 0.45f);
    static const uint32_t AW[4][2] = {
            {0xd83030, 0xf4ece0}, {0x2a8a4a, 0xf4ece0}, {0x2a5ab0, 0xf4ece0}, {0xe8a020, 0x6a2a18}};
    for (int i = 0; i < 11; i++) {
        float bx = -330 + i * 64, bw = 58;
        if (off_x(bx - 4, bx + bw + 4)) {
            continue;
        }
        t_rect(bx, -40, bw, 60, WOOD[0]);
        t_rect(bx + 2, -38, bw - 4, 24, WOOD[1]);
        // the awning, in stripes
        const uint32_t *a = AW[i % 4];
        for (int k = 0; k < 10; k++) {
            float x0 = bx - 4 + k * (bw + 8) / 10.0f, x1 = x0 + (bw + 8) / 10.0f;
            float q[8] = {x0 + 3, -46, x1 + 3, -46, x1, -37, x0, -37};
            t_poly(q, 4, TC(a[k & 1]));
        }
        t_rect(bx - 4, -37.5f, bw + 8, 1, WOOD[0]);
        // a lantern, the counter, the fruit
        float lx = bx + bw * 0.5f, ly = -32 + fsin(st->t * 1.6f + i) * 0.6f;
        t_line(lx, -37, lx, ly - 2, 0.5f, WOOD[0]);
        t_ellipse(lx, ly, 2.2f, 2.6f, WARM[1]);
        t_ellipse(lx, ly, 1.2f, 1.6f, WARM[2]);
        t_rect(bx, -14, bw, 5, WOOD[2]);
        t_rect(bx, -14, bw, 1, WOOD[3]);
        bool small = t_unit() < 2.2f;
        for (int m = 0; m < 3; m++) {
            tcol_t fc = TC(FRUIT[(i * 3 + m) % 6]);
            float mx = bx + 8 + m * 18;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3 - r; c++) {
                    float fx = mx + c * 3.6f + r * 1.8f, fy = -15.5f - r * 2.4f;
                    if (small) {
                        t_rect(fx - 1.6f, fy - 1.4f, 3.2f, 2.8f, fc);
                    } else {
                        t_ellipse(fx, fy, 1.8f, 1.6f, fc);
                    }
                }
            }
        }
        if (i == 5) {  // the sign of the market
            bool on = hashf(floorf(st->t * 6) + 9) > 0.06f;
            t_rect(bx + 10, -60, bw - 20, 11, SIGNB);
            t_rect(bx + 10.5f, -59.5f, bw - 21, 10, on ? NEON_R[1] : NEON_R[2]);
            t_rect(bx + 11.5f, -58.5f, bw - 23, 8, SIGNB);
            const char *const *w[3] = {HEB_QOF, HEB_VAV, HEB_SHIN};
            heb_word(w, 3, bx + bw / 2, -54.5f, 1.25f, on ? NEON_R[0] : NEON_R[2]);
        }
    }
}
static void shuk_lights(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    static const float POLES[4] = {-260, -90, 80, 250};
    const float top = -66, sway = fsin(st->t * 1.1f) * 1.4f;
    t_layer(v, cam, 0.7f);
    for (int k = 0; k < 3; k++) {
        float x0 = POLES[k], x1 = POLES[k + 1];
        float px = x0, py = top;
        for (int i = 1; i <= 12; i++) {
            float u = i / 12.0f, x = lerpf(x0, x1, u), y = top + (10 + sway) * 4 * u * (1 - u);
            t_line(px, py, x, y, 0.6f, POLE);
            px = x;
            py = y;
        }
        int n = (int)((x1 - x0) / 8);
        for (int i = 1; i < n; i++) {
            float u = (float)i / n, x = lerpf(x0, x1, u), y = top + (10 + sway) * 4 * u * (1 - u);
            static const uint32_t FC[5] = {0xe83a3a, 0xf8d040, 0x3a9adf, 0x3ac06a, 0xf4ece0};
            if (i & 1) {
                t_tri(x - 2.4f, y, x + 2.4f, y, x, y + 4.5f, TC(FC[(i + k) % 5]));
            } else {
                t_ellipse(x, y + 1.4f, 1.6f, 1.6f, WARM[1]);
                t_ellipse(x, y + 1.4f, 0.8f, 0.8f, WARM[2]);
            }
        }
    }
}
static void bird(const tstage_t *st, float x0, float y0, const tcol_t *c)
{
    float pa = st->pigeon_t;
    if (pa >= 2) {
        return;
    }
    float px = x0 + (pa > 0 ? pa * 70 : 0), py = y0 - (pa > 0 ? pa * 40 + pa * pa * 10 : 0),
          flap = pa > 0 ? fsin(st->t * 40) : 0;
    t_ellipse(px, py, 2.4f, 1.4f, c[0]);
    t_ellipse(px + 2, py - 1.4f, 1.1f, 1.1f, c[0]);
    t_rect(px + 3, py - 1.5f, 0.9f, 0.6f, c[2]);
    if (pa > 0) {
        t_tri(px - 1, py, px + 1, py, px - 0.5f, py - 4 * flap, c[1]);
    }
}
static void shuk_back(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    t_layer(v, cam, 0.85f);
    // a low wall of crates
    for (float x = -300; x < 300; x += 18) {
        float h = 10 + hashf(x * 0.3f) * 6;
        t_rect(x, -9 - h, 16, h, WOOD[1]);
        t_rect(x, -9 - h, 16, 1, WOOD[3]);
        t_rect(x + 5, -9 - h, 0.8f, h, WOOD[0]);
        t_rect(x + 10.5f, -9 - h, 0.8f, h, WOOD[0]);
        if (hashf(x * 0.7f) > 0.5f) {
            for (int c = 0; c < 4; c++) {
                t_rect(x + 1.6f + c * 3.4f, -11.4f - h, 3, 2.6f, TC(FRUIT[((int)(x * 0.1f) + c) & 3]));
            }
        }
    }
    // a juice cart
    t_rect(160, -30, 26, 16, WOOD[2]);
    t_rect(160, -30, 26, 2, WARM[1]);
    t_ellipse(166, -11, 4, 4, WOOD[0]);
    t_ellipse(180, -11, 4, 4, WOOD[0]);
    for (int c = 0; c < 5; c++) {
        t_ellipse(163 + c * 4.5f, -32, 2, 1.8f, TC(0xff9a20));
    }
    bird(st, 52, -26.5f, PIGEON);
    static const float SX[5] = {-120, -60, 96, 120, 214};
    for (int i = 0; i < 5; i++) {
        spectator(SX[i], i + 1, st->cheer, st->t);
    }
}

// ================================================================ PORT: the Jaffa port at night
static const tcol_t HILL[3] = {TC(0x0c1428), TC(0x141e3a), TC(0x1c2848)};
static const tcol_t SEA[3] = {TC(0x08142c), TC(0x0e1e3e), TC(0x2a4a80)};
static void port_far(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    t_layer(v, cam, 0.15f);
    // the hill of old Jaffa, with its houses, the clock tower and the church tower
    float hill[12] = {-380, -30, -380, -48, -300, -60, -220, -70, -150, -74, -40, -30};
    t_poly(hill, 6, HILL[0]);
    for (int i = 0; i < 26; i++) {
        float hx = -360 + hashf(i * 2.7f) * 300, hy = -38 - hashf(i * 5.3f) * 30, w = 6 + hashf(i * 9.1f) * 8;
        if (off_x(hx, hx + w)) {
            continue;
        }
        t_rect(hx, hy, w, 8, HILL[1]);
        if (hashf(i * 3.3f) > 0.45f) {
            t_rect(hx + 2, hy + 2, 1.6f, 2, WIN[0]);
        }
    }
    t_rect(-160, -92, 8, 20, HILL[2]);
    t_ellipse(-156, -86, 2.2f, 2.2f, WIN[0]);  // the clock tower
    t_rect(-112, -96, 9, 26, HILL[2]);
    float roof[6] = {-113, -96, -98, -96, -107.5f, -106};
    t_poly(roof, 3, HILL[2]);  // the church tower
    // the lighthouse and its turning beam
    t_rect(-262, -74, 6, 20, HILL[2]);
    t_rect(-263, -76, 8, 3, HILL[2]);
    t_ellipse(-259, -77, 1.6f, 1.6f, WARM[2]);
    // the towers of Tel Aviv, far away
    for (int i = 0; i < 12; i++) {
        float tx = 120 + i * 20 + hashf(i * 1.3f) * 8, th = 10 + hashf(i * 4.1f) * 26;
        t_rect(tx, -32 - th, 8, th + 2, FARC[0]);
        for (float wy = -30 - th; wy < -34; wy += 3) {
            if (hashf(tx + wy * 3.1f) > 0.5f) {
                t_rect(tx + 2 + hashf(wy) * 4, wy, 1, 1, WIN[2]);
            }
        }
    }
}
static void port_mid(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    // the beam of the lighthouse turns (it is drawn here, not in the cached far layer)
    t_layer(v, cam, 0.15f);
    float a = st->t * 0.9f, ca = fcos(a), sa = fsin(a);
    if (ca > -0.2f) {
        uint8_t old = T.alpha;
        T.alpha = 46;
        t_tri(-259, -77, -259 + ca * 240, -77 + sa * 26 - 6, -259 + ca * 240, -77 + sa * 26 + 6, WARM[2]);
        T.alpha = old;
    }
    t_layer(v, cam, 0.45f);
    t_rect(-600, -36, 1200, 60, SEA[0]);
    t_rect(-600, -36, 1200, 1, SEA[2]);
    // the light of the moon on the waves
    float mx = 150;  // under the moon
    for (int r = 0; r < 9; r++) {
        float y = -34 + r * 3.2f, w = 6 + r * 2.4f;
        for (int k = 0; k < 3; k++) {
            float x = mx - w + hashf(r * 7.1f + k + floorf(st->t * 3)) * w * 2;
            t_rect(x, y, 2 + r * 0.6f, 0.7f, SEA[2]);
        }
    }
    for (int r = 0; r < 6; r++) {
        for (int k = 0; k < 14; k++) {
            float x = -300 + k * 46 + fsin(st->t * 0.8f + k + r) * 6, y = -32 + r * 5;
            t_rect(x, y, 6, 0.6f, SEA[1]);
        }
    }
    // the fishing boats bob on the water
    static const float BX[3] = {-210, 20, 190};
    static const uint32_t HC[3] = {0xd8dce4, 0x2a4a8a, 0xc84a2a};
    for (int k = 0; k < 3; k++) {
        float bx = BX[k], by = -30 + fsin(st->t * 1.3f + k * 2) * 1.0f, tilt = fsin(st->t * 1.1f + k) * 0.04f;
        t_push();
        t_translate(bx, by);
        t_rotate(tilt);
        float hull[8] = {-14, -4, 14, -4, 10, 1, -11, 1};
        t_poly(hull, 4, TC(HC[k]));
        t_rect(-14, -4.5f, 28, 0.9f, TC(0x101828));
        t_rect(-4, -9, 8, 5, TC(0xe8e4dc));
        t_rect(-2.5f, -8, 2, 2, WARM[0]);
        t_line(3, -4, 3, -26, 0.6f, TC(0x101828));
        t_ellipse(3, -26.5f, 0.9f, 0.9f, (fmodf(st->t + k, 1.6f) < 0.2f) ? WARM[2] : WARM[1]);
        t_pop();
    }
}
static void port_lights(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    (void)st;
    static const float POLES[4] = {-260, -90, 80, 250};
    t_layer(v, cam, 0.7f);
    for (int k = 0; k < 4; k++) {
        float x = POLES[k];
        t_rect(x - 0.7f, -50, 1.4f, 56, POLE);
        t_line(x, -50, x + 5, -52, 0.8f, POLE);
        uint8_t old = T.alpha;
        T.alpha = 70;
        t_ellipse(x + 5, -50, 9, 7, WARM[1]);
        T.alpha = old;
        t_ellipse(x + 5, -50.5f, 2, 1.6f, WARM[2]);
    }
}
static void port_back(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    t_layer(v, cam, 0.85f);
    // the edge of the pier: a railing with a rope, crates of fish, a coil of rope
    t_rect(-300, -12, 600, 3, WOOD[1]);
    for (float x = -300; x < 300; x += 22) {
        t_rect(x, -24, 1.6f, 13, WOOD[2]);
        t_rect(x, -24, 1.6f, 1, WOOD[3]);
    }
    for (float x = -300; x < 300; x += 22) {
        float px = x, py = -22;
        for (int i = 1; i <= 6; i++) {
            float u = i / 6.0f, nx = x + 22 * u, ny = -22 + 2.5f * 4 * u * (1 - u);
            t_line(px, py, nx, ny, 0.5f, WOOD[3]);
            px = nx;
            py = ny;
        }
    }
    t_rect(-150, -19, 18, 8, WOOD[2]);
    for (int c = 0; c < 5; c++) {
        t_ellipse(-147 + c * 3.4f, -19.5f, 1.8f, 0.9f, TC(0xb8c4d0));
    }
    t_ellipse(120, -12, 7, 2.4f, WOOD[3]);
    t_ellipse(120, -12, 4, 1.4f, WOOD[1]);
    // the fishermen watch, and fish (their rods go over the water)
    static const float SX[4] = {-200, -40, 150, 230};
    for (int i = 0; i < 4; i++) {
        spectator(SX[i], i + 2, st->cheer, st->t);
        if (i & 1) {
            t_line(SX[i] + 3, -33, SX[i] + 24, -48 + fsin(st->t * 2 + i) * 1.2f, 0.5f, TC(0x8a7a5a));
        }
    }
    static const tcol_t GULL[3] = {TC(0xe8ecf0), TC(0xa8b0b8), TC(0xe8a020)};
    bird(st, 52, -25.5f, GULL);
}

static const tcol_t FLS[2][5] = {
        {TC(0x5a4a40), TC(0x63523f), TC(0x3a3028), TC(0x6e5a48), TC(0x2e261f)},  // the stones of the market
        {TC(0x3a2a1e), TC(0x45321f), TC(0x24180f), TC(0x5a4230), TC(0x1e140c)},  // the planks of the pier
};
static void floor_stage(const tview_t *v, const tcam_t *cam, const tcol_t *P, float seam)
{
    static const float ROWS[10] = {-10, -7, -4, -1, 2, 6, 11, 17, 24, 32};
    float lw = 0.6f * v->ps * cam->z;
    if (lw < 1) {
        lw = 1;
    }
    for (int k = 0; k < 9; k++) {
        float y0 = ROWS[k], y1 = ROWS[k + 1], q[8];
        t_proj(v, cam, fp(y0), -900, y0, &q[0], &q[1]);
        t_proj(v, cam, fp(y0), 900, y0, &q[2], &q[3]);
        t_proj(v, cam, fp(y1), 900, y1, &q[4], &q[5]);
        t_proj(v, cam, fp(y1), -900, y1, &q[6], &q[7]);
        t_identity();
        t_poly(q, 4, k % 2 ? P[1] : P[0]);
        t_line(q[0], q[1], q[2], q[3], lw, P[2]);
    }
    for (float x = -900; x <= 900; x += seam) {
        float a0, a1, b0, b1;
        t_proj(v, cam, fp(-10), x, -10, &a0, &a1);
        t_proj(v, cam, fp(32), x, 32, &b0, &b1);
        if ((a0 < -4 && b0 < -4) || (a0 > v->iw + 4 && b0 > v->iw + 4)) {
            continue;
        }
        t_identity();
        t_line(a0, a1, b0, b1, 1, P[4]);
    }
}

int t_stage_mask = 63;  // the layers to draw (for speed tests)
void t_stage_draw(const tview_t *v, const tcam_t *cam, const tstage_t *st)
{
    if (!s_sky_ok) {
        sky_init();
    }
    // the sky and the far towers come from a cache while the camera does not change them
    static tcol_t *bg;
    static struct {
        int z, roll, sx, sy, y, farx, w, h, tick, mask, stage;
    } last;
    if (!bg) {
        bg = heap_caps_malloc(T_LO_W * T_LO_H * sizeof(tcol_t), MALLOC_CAP_SPIRAM);
    }
    // the key is in whole pixels (the far layer moves 0.15 x the camera: a 1-pixel step is not seen)
    float zl = 1 + (cam->z - 1) * 0.15f;
    int kz = (int)(zl * 64 + 0.5f), kr = (int)floorf(cam->roll * 200 + 0.5f);
    int ksx = (int)floorf(cam->sx * v->ps + 0.5f), ksy = (int)floorf(cam->sy * v->ps + 0.5f);
    int ky = (int)floorf((cam->y - v->base_y) * 0.15f * zl * v->ps + 0.5f);
    int farx = (int)floorf(cam->x * 0.15f * zl * v->ps + 0.5f), tick = (int)(st->t * 2);
    int sg = st->stage >= 0 && st->stage < T_STAGE_N ? st->stage : 0;
    s_sky = s_skies[sg];
    bool same = bg && last.z == kz && last.roll == kr && last.sx == ksx && last.sy == ksy && last.y == ky &&
                last.farx == farx && last.w == T.w && last.h == T.h && last.tick == tick &&
                last.mask == (t_stage_mask & 3) && last.stage == sg;
    if (same) {
        memcpy(T.px, bg, (size_t)(T.w * T.h) * sizeof(tcol_t));
    } else {
        t_identity();
        t_clear(s_sky[SKY_N + 1]);
        if (t_stage_mask & 1) {
            sky(v, cam, st);
        }
        if (t_stage_mask & 2) {
            if (sg == T_STAGE_SHUK) {
                shuk_far(v, cam, st);
            } else if (sg == T_STAGE_PORT) {
                port_far(v, cam, st);
            } else {
                far_layer(v, cam, st);
            }
        }
        if (bg) {
            memcpy(bg, T.px, (size_t)(T.w * T.h) * sizeof(tcol_t));
            last.z = kz;
            last.roll = kr;
            last.sx = ksx;
            last.sy = ksy;
            last.y = ky;
            last.farx = farx;
            last.w = T.w;
            last.h = T.h;
            last.tick = tick;
            last.mask = t_stage_mask & 3;
            last.stage = sg;
        }
    }
    if (sg == T_STAGE_SHUK) {
        if (t_stage_mask & 4) {
            shuk_mid(v, cam, st);
        }
        if (t_stage_mask & 8) {
            shuk_lights(v, cam, st);
        }
        if (t_stage_mask & 16) {
            shuk_back(v, cam, st);
        }
        if (t_stage_mask & 32) {
            floor_stage(v, cam, FLS[0], 18);
        }
    } else if (sg == T_STAGE_PORT) {
        if (t_stage_mask & 4) {
            port_mid(v, cam, st);
        }
        if (t_stage_mask & 8) {
            port_lights(v, cam, st);
        }
        if (t_stage_mask & 16) {
            port_back(v, cam, st);
        }
        if (t_stage_mask & 32) {
            floor_stage(v, cam, FLS[1], 10);
        }
    } else {
        if (t_stage_mask & 4) {
            mid_layer(v, cam, st);
        }
        if (t_stage_mask & 8) {
            lights(v, cam, st);
        }
        if (t_stage_mask & 16) {
            back_roof(v, cam, st);
        }
        if (t_stage_mask & 32) {
            floor_layer(v, cam, st);
        }
    }
}
void t_shadow(float x, float h)
{
    float k = 1 - (h < 0 ? 0 : h > 60 ? 60 : h) / 70;
    t_ellipse(x, 0.6f, 11 * k, 2.1f * k, SHADOWC);
}
