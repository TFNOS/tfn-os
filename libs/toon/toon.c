// TOON: the rasterizer, the camera, the cartoon puppets and the VHS filter.

#include <math.h>
#include <string.h>
#include "tfn/tfn_util.h"
#include "toon.h"
#include "tfn/gfx.h"

tcanvas_t T;

// ================================================================ raster
#define STACK_N 10
static tmat_t s_stack[STACK_N];
static int s_sp;
static uint8_t s_mask[T_MAX_W * T_MAX_H];
static uint8_t s_mark = 1;
static bool s_to_mask;

void t_begin(tcol_t *buf, int w, int h)
{
    T.px = buf;
    T.w = w;
    T.h = h;
    T.alpha = 255;
    T.clip = false;
    s_sp = 0;
    t_identity();
}
void t_identity(void)
{
    T.m = (tmat_t){1, 0, 0, 1, 0, 0};
}
void t_translate(float x, float y)
{
    T.m.e += T.m.a * x + T.m.c * y;
    T.m.f += T.m.b * x + T.m.d * y;
}
void t_scale(float sx, float sy)
{
    T.m.a *= sx;
    T.m.b *= sx;
    T.m.c *= sy;
    T.m.d *= sy;
}
void t_rotate(float r)
{
    float cs = fcos(r), sn = fsin(r);
    tmat_t m = T.m;
    T.m.a = m.a * cs + m.c * sn;
    T.m.b = m.b * cs + m.d * sn;
    T.m.c = -m.a * sn + m.c * cs;
    T.m.d = -m.b * sn + m.d * cs;
}
void t_push(void)
{
    if (s_sp < STACK_N) {
        s_stack[s_sp++] = T.m;
    }
}
void t_pop(void)
{
    if (s_sp > 0) {
        T.m = s_stack[--s_sp];
    }
}
float t_unit(void)
{
    return sqrtf(fabsf(T.m.a * T.m.d - T.m.b * T.m.c));
}
void t_xf(float x, float y, float *ox, float *oy)
{
    *ox = T.m.a * x + T.m.c * y + T.m.e;
    *oy = T.m.b * x + T.m.d * y + T.m.f;
}

static inline tcol_t blend(tcol_t d, tcol_t s, int a)
{
    int r = ((s >> 11) * a + (d >> 11) * (255 - a)) / 255;
    int g = (((s >> 5) & 63) * a + ((d >> 5) & 63) * (255 - a)) / 255;
    int b = ((s & 31) * a + (d & 31) * (255 - a)) / 255;
    return (tcol_t)((r << 11) | (g << 5) | b);
}
static void span(int y, int x0, int x1, tcol_t c)
{
    if (y < 0 || y >= T.h) {
        return;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 >= T.w) {
        x1 = T.w - 1;
    }
    if (x0 > x1) {
        return;
    }
    if (s_to_mask) {
        memset(&s_mask[y * T.w + x0], s_mark, (size_t)(x1 - x0 + 1));
        return;
    }
    tcol_t *p = T.px + y * T.w;
    if (!T.clip && T.alpha == 255) {
        for (int x = x0; x <= x1; x++) {
            p[x] = c;
        }
        return;
    }
    const uint8_t *mk = &s_mask[y * T.w];
    for (int x = x0; x <= x1; x++) {
        if (T.clip && mk[x] != s_mark) {
            continue;
        }
        p[x] = T.alpha == 255 ? c : blend(p[x], c, T.alpha);
    }
}
// Fill a polygon given in SCREEN coordinates (even-odd rule, pixel centers).
#define PMAX 96
static void fill_screen(const float *xy, int n, tcol_t c)
{
    if (n < 3) {
        return;
    }
    float ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < n; i++) {
        float y = xy[i * 2 + 1];
        if (y < ymin) {
            ymin = y;
        }
        if (y > ymax) {
            ymax = y;
        }
    }
    int y0 = (int)ceilf(ymin - 0.5f), y1 = (int)floorf(ymax - 0.5f);
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 >= T.h) {
        y1 = T.h - 1;
    }
    float xs[PMAX];
    for (int y = y0; y <= y1; y++) {
        float yc = y + 0.5f;
        int k = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            float ay = xy[i * 2 + 1], by = xy[j * 2 + 1];
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc)) {
                float ax = xy[i * 2], bx = xy[j * 2];
                if (k < PMAX) {
                    xs[k++] = ax + (yc - ay) * (bx - ax) / (by - ay);
                }
            }
        }
        for (int i = 1; i < k; i++) {
            float v = xs[i];
            int j = i - 1;
            while (j >= 0 && xs[j] > v) {
                xs[j + 1] = xs[j];
                j--;
            }
            xs[j + 1] = v;
        }
        for (int i = 0; i + 1 < k; i += 2) {
            span(y, (int)ceilf(xs[i] - 0.5f), (int)floorf(xs[i + 1] - 0.5f), c);
        }
    }
}
void t_poly(const float *xy, int n, tcol_t c)
{
    float s[PMAX * 2];
    if (n > PMAX) {
        n = PMAX;
    }
    for (int i = 0; i < n; i++) {
        t_xf(xy[i * 2], xy[i * 2 + 1], &s[i * 2], &s[i * 2 + 1]);
    }
    fill_screen(s, n, c);
}
void t_clear(tcol_t c)
{
    for (int i = 0; i < T.w * T.h; i++) {
        T.px[i] = c;
    }
}
void t_srect(int x, int y, int w, int h, tcol_t c)
{
    for (int r = y; r < y + h; r++) {
        span(r, x, x + w - 1, c);
    }
}
void t_rect(float x, float y, float w, float h, tcol_t c)
{
    if (T.m.b == 0 && T.m.c == 0) {
        float x0 = T.m.a * x + T.m.e, x1 = T.m.a * (x + w) + T.m.e, y0 = T.m.d * y + T.m.f,
              y1 = T.m.d * (y + h) + T.m.f;
        if (x0 > x1) {
            float q = x0;
            x0 = x1;
            x1 = q;
        }
        if (y0 > y1) {
            float q = y0;
            y0 = y1;
            y1 = q;
        }
        int iy0 = (int)ceilf(y0 - 0.5f), iy1 = (int)floorf(y1 - 0.5f), ix0 = (int)ceilf(x0 - 0.5f),
            ix1 = (int)floorf(x1 - 0.5f);
        if (ix1 < ix0) {
            ix1 = ix0;  // a thin rect still shows one pixel
        }
        if (iy1 < iy0) {
            iy1 = iy0;
        }
        for (int r = iy0; r <= iy1; r++) {
            span(r, ix0, ix1, c);
        }
        return;
    }
    float p[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
    t_poly(p, 4, c);
}
static int segs(float r)
{
    int n = (int)(r * t_unit() * 1.2f) + 5;
    return n > 40 ? 40 : n;
}
void t_ellipse(float cx, float cy, float rx, float ry, tcol_t c)
{
    if (rx <= 0 || ry <= 0) {
        return;
    }
    int n = segs(rx > ry ? rx : ry);
    float p[80], cs = fcos(6.2831853f / n), sn = fsin(6.2831853f / n), ux = 1, uy = 0;
    for (int i = 0; i < n; i++) {
        p[i * 2] = cx + ux * rx;
        p[i * 2 + 1] = cy + uy * ry;
        float nx = ux * cs - uy * sn;
        uy = ux * sn + uy * cs;
        ux = nx;
    }
    t_poly(p, n, c);
}
void t_capsule(float ax, float ay, float bx, float by, float r1, float r2, tcol_t c)
{
    float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy);
    if (L < 1e-4f) {
        dx = 1;
        dy = 0;
    } else {
        dx /= L;
        dy /= L;
    }
    int n = (int)((r1 > r2 ? r1 : r2) * t_unit() * 0.6f) + 2;
    if (n > 20) {
        n = 20;
    }
    float p[(20 + 1) * 4], cs = fcos(3.1415927f / n), sn = fsin(3.1415927f / n);
    int k = 0;
    float ux = -dy, uy = dx;  // the half circle around a
    for (int i = 0; i <= n; i++) {
        p[k++] = ax + ux * r1;
        p[k++] = ay + uy * r1;
        float nx = ux * cs - uy * sn;
        uy = ux * sn + uy * cs;
        ux = nx;
    }
    ux = dy;
    uy = -dx;  // the half circle around b
    for (int i = 0; i <= n; i++) {
        p[k++] = bx + ux * r2;
        p[k++] = by + uy * r2;
        float nx = ux * cs - uy * sn;
        uy = ux * sn + uy * cs;
        ux = nx;
    }
    t_poly(p, k / 2, c);
}
void t_line(float ax, float ay, float bx, float by, float w, tcol_t c)
{
    t_capsule(ax, ay, bx, by, w / 2, w / 2, c);
}
void t_tri(float ax, float ay, float bx, float by, float cx, float cy, tcol_t c)
{
    float p[6] = {ax, ay, bx, by, cx, cy};
    t_poly(p, 3, c);
}
void t_arc(float cx, float cy, float r, float a0, float a1, float w, tcol_t c)
{
    int n = (int)(fabsf(a1 - a0) * 2.5f) + 3;
    if (n > 16) {
        n = 16;
    }
    for (int i = 0; i < n; i++) {
        float u0 = a0 + (a1 - a0) * i / n, u1 = a0 + (a1 - a0) * (i + 1) / n;
        t_line(cx + fcos(u0) * r, cy + fsin(u0) * r, cx + fcos(u1) * r, cy + fsin(u1) * r, w, c);
    }
}
void t_mask_begin(void)
{
    if (++s_mark == 0) {
        memset(s_mask, 0, sizeof s_mask);
        s_mark = 1;
    }
}
void t_mask_poly(const float *xy, int n)
{
    s_to_mask = true;
    t_poly(xy, n, 0);
    s_to_mask = false;
}
void t_mask_ellipse(float cx, float cy, float rx, float ry)
{
    s_to_mask = true;
    t_ellipse(cx, cy, rx, ry, 0);
    s_to_mask = false;
}

// ================================================================ text
static const struct {
    char ch;
    const char *rows;
} GLYPHS[] = {
        {'A', ".###.#...##...#######...##...##...#"}, {'B', "####.#...##...#####.#...##...#####."},
        {'C', ".#####....#....#....#....#.....####"}, {'D', "####.#...##...##...##...##...#####."},
        {'E', "######....#....####.#....#....#####"}, {'F', "######....#....####.#....#....#...."},
        {'G', ".#####....#....#.####...##...#.###."}, {'H', "#...##...##...#######...##...##...#"},
        {'I', "#####..#....#....#....#....#..#####"}, {'K', "#...##..#.#.#..##...#.#..#..#.#...#"},
        {'L', "#....#....#....#....#....#....#####"}, {'M', "#...###.###.#.##.#.##...##...##...#"},
        {'N', "#...###..##.#.##..###...##...##...#"}, {'O', ".###.#...##...##...##...##...#.###."},
        {'P', "####.#...##...#####.#....#....#...."}, {'R', "####.#...##...#####.#.#..#..#.#...#"},
        {'S', ".#####....#.....###.....#....#####."}, {'T', "#####..#....#....#....#....#....#.."},
        {'U', "#...##...##...##...##...##...#.###."}, {'V', "#...##...##...##...##...#.#.#...#.."},
        {'W', "#...##...##...##.#.##.#.###.###...#"}, {'Y', "#...##...#.#.#...#....#....#....#.."},
        {'Q', ".###.#...##...##...##.#.##..#..##.#"}, {'X', "#...##...#.#.#...#...#.#.#...##...#"},
        {'!', "..#....#....#....#....#.........#.."}, {'.', "................................#.."},
        {':', ".......#....#.........#....#......."}, {'-', "................###................"},
        {'?', ".###.#...#....#...#...#.........#.."}, {'>', "#....##...###..####.###..##...#...."},
        {'0', ".###.#...##..###.#.###..##...#.###."}, {'1', "..#...##....#....#....#....#...###."},
        {'2', ".###.#...#....#..##..#...#....#####"}, {'3', "####.....#....#.###.....#....#####."},
        {'4', "...#...##..#.#.#..#.#####...#....#."}, {'5', "######....####.....#....##...#.###."},
        {'6', ".###.#....#....####.#...##...#.###."}, {'7', "#####....#...#...#...#....#....#..."},
        {'8', ".###.#...##...#.###.#...##...#.###."}, {'9', ".###.#...##...#.####....#....#.###."},
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPHS / sizeof GLYPHS[0]; i++) {
        if (GLYPHS[i].ch == ch) {
            return GLYPHS[i].rows;
        }
    }
    return NULL;
}
int t_text_w(const char *s, int sc)
{
    int n = (int)strlen(s);
    return n ? n * 6 * sc - sc : 0;
}
void t_text(const char *s, int x, int y, int sc, trow_fn col, void *ctx, bool outline, tcol_t shadow)
{
    int o = sc / 2 < 1 ? 1 : sc / 2;
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 0 && !outline) {
            continue;
        }
        if (pass == 1 && !shadow) {
            continue;
        }
        for (int ci = 0; s[ci]; ci++) {
            const char *g = glyph(s[ci]);
            if (!g) {
                continue;
            }
            for (int r = 0; r < 7; r++) {
                for (int c = 0; c < 5; c++) {
                    if (g[r * 5 + c] != '#') {
                        continue;
                    }
                    int px = x + (ci * 6 + c) * sc, py = y + r * sc;
                    if (pass == 0) {
                        t_srect(px - o, py - o, sc + 2 * o, sc + 2 * o, TC(0x140a16));
                    } else if (pass == 1) {
                        t_srect(px + o, py + o, sc, sc, shadow);
                    } else {
                        t_srect(px, py, sc, sc, col(r, ctx));
                    }
                }
            }
        }
    }
}
void t_text_xf(const char *s, float x, float y, float sc, trow_fn col, void *ctx, tcol_t shadow)
{
    float o = sc * 0.5f < 1 ? 1 : sc * 0.5f;
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 1 && !shadow) {
            continue;
        }
        for (int ci = 0; s[ci]; ci++) {
            const char *g = glyph(s[ci]);
            if (!g) {
                continue;
            }
            for (int r = 0; r < 7; r++) {
                for (int c = 0; c < 5; c++) {
                    if (g[r * 5 + c] != '#') {
                        continue;
                    }
                    float px = x + (ci * 6 + c) * sc, py = y + r * sc;
                    if (pass == 0) {
                        t_rect(px - o, py - o, sc + 2 * o, sc + 2 * o, TC(0x140a16));
                    } else if (pass == 1) {
                        t_rect(px + o, py + o, sc, sc, shadow);
                    } else {
                        t_rect(px, py, sc, sc, col(r, ctx));
                    }
                }
            }
        }
    }
}

// ================================================================ camera
void t_view_init(tview_t *v, int W, int H, float ps)
{
    v->W = W;
    v->H = H;
    v->ps = ps;
    v->iw = (int)(W * ps + 0.5f);
    v->ih = (int)(H * ps + 0.5f);
    v->base_y = -(H / 2.0f - 10);
}
void t_layer(const tview_t *v, const tcam_t *cam, float p)
{
    float zl = 1 + (cam->z - 1) * p, xl = cam->x * p, yl = v->base_y + (cam->y - v->base_y) * p, s = zl * v->ps;
    float cs = fcos(cam->roll), sn = fsin(cam->roll);
    T.m = (tmat_t){cs * s, sn * s, -sn * s, cs * s, v->iw / 2.0f + cam->sx * v->ps, v->ih / 2.0f + cam->sy * v->ps};
    t_translate(-xl, -yl);
}
void t_proj(const tview_t *v, const tcam_t *cam, float p, float x, float y, float *sx, float *sy)
{
    tmat_t keep = T.m;
    t_layer(v, cam, p);
    t_xf(x, y, sx, sy);
    T.m = keep;
}
void t_cam_clamp(const tview_t *v, tcam_t *cam, float half_w)
{
    float vw = v->W / cam->z, vh = v->H / cam->z;
    float lo = -half_w + vw / 2, hi = half_w - vw / 2;
    if (lo > hi) {
        lo = hi = 0;
    }
    if (cam->x < lo) {
        cam->x = lo;
    }
    if (cam->x > hi) {
        cam->x = hi;
    }
    if (cam->y > 16 - vh / 2) {
        cam->y = 16 - vh / 2;
    }
    if (cam->y < -150 + vh / 2) {
        cam->y = -150 + vh / 2;
    }
}

// ================================================================ puppets
#define D2R 0.017453293f
enum {
    P_TO,
    P_HD,
    P_FUA,
    P_FFA,
    P_BUA,
    P_BFA,
    P_FTH,
    P_FSH,
    P_BTH,
    P_BSH,
    P_FFO,
    P_BFO,
    P_FAL,
    P_BAL,
    P_FLL
};
static const float L_TH = 7.5f, L_SH = 7, L_UA = 7, L_FA = 7, HIPJ = 0.5f, SOLE = 2.6f, AIRHIP = -17;
static const float LX = 0.36f, LY = -0.93f, OW = 0.85f;
static const tcol_t OUT = TC(0x140a16), DARK = TC(0x1a0c10), EYEW = TC(0xf6f0e6), WHITE = TC(0xffffff);
static const tcol_t BLUSH = TC(0xec7a8a), TONGUE = TC(0xe0506a), SOLEC = TC(0x3a2a3a);
static const tramp_t SHOE = {{TC(0x6a6476), TC(0xb4aec2), TC(0xece8f2), TC(0xffffff)}};
static float s_pxs = 1;

static inline float lerpf(float a, float b, float u)
{
    return a + (b - a) * u;
}
void t_pose_blend(const tpose_t *a, const tpose_t *b, float u, tpose_t *o)
{
    const float *pa = &a->to, *pb = &b->to;
    float *po = &o->to;
    for (int i = 0; i < 16; i++) {
        po[i] = lerpf(pa[i], pb[i], u);
    }
    o->ex = u < 0.5f ? a->ex : b->ex;
}
static void to_rad(const tpose_t *p, float *P)
{
    const float *s = &p->to;
    for (int i = 0; i < 12; i++) {
        P[i] = s[i] * D2R;
    }
    P[P_FAL] = p->fal > 0 ? p->fal : 1;
    P[P_BAL] = p->bal > 0 ? p->bal : 1;
    P[P_FLL] = p->fll > 0 ? p->fll : 1;
}
static float foot_depth(const float *P)
{
    float f = HIPJ + L_TH * fcos(P[P_FTH]) + L_SH * fcos(P[P_FSH]),
          b = HIPJ + L_TH * fcos(P[P_BTH]) + L_SH * fcos(P[P_BSH]);
    return (f > b ? f : b) + SOLE;
}
float t_pose_hip(const tpose_t *p)
{
    float P[16];
    to_rad(p, P);
    return lerpf(-foot_depth(P), AIRHIP, p->air);
}
static void plant_feet(float *P, float D)
{
    for (int k = 0; k < 2; k++) {
        int th = k ? P_BTH : P_FTH, sh = k ? P_BSH : P_FSH;
        float ank = HIPJ + L_TH * fcos(P[th]) + L_SH * fcos(P[sh]), gap = D - SOLE - ank;
        if (gap > 0.15f && gap < 3) {
            float c = (D - SOLE - HIPJ - L_TH * fcos(P[th])) / L_SH;
            if (c <= 1 && c >= -1) {
                P[sh] = (P[sh] < 0 ? -1 : 1) * acosf(c);
            }
        }
    }
}
static void rotp(float x, float y, float a, float *ox, float *oy)
{
    float c = fcos(a), s = fsin(a);
    *ox = x * c - y * s;
    *oy = x * s + y * c;
}
static void chain(float sx, float sy, const float *ang, int n, float len, float (*out)[2])
{
    out[0][0] = sx;
    out[0][1] = sy;
    for (int i = 0; i < n; i++) {
        out[i + 1][0] = out[i][0] + fcos(ang[i]) * len;
        out[i + 1][1] = out[i][1] + fsin(ang[i]) * len;
    }
}
void t_puppet_solve(tpuppet_t *pp, float vx, float vy, float t)
{
    float *P = pp->P;
    to_rad(&pp->pose, P);
    if (!pp->air && pp->pose.air < 0.5f) {
        plant_feet(P, foot_depth(P));
    }
    float (*j)[2] = pp->j;
    float to = P[P_TO];
    rotp(3.2f, -11.2f, to, &j[TJ_FSH][0], &j[TJ_FSH][1]);
    rotp(-2.6f, -11.6f, to, &j[TJ_BSH][0], &j[TJ_BSH][1]);
    float pvx, pvy, hx, hy;
    rotp(0.8f, -13, to, &pvx, &pvy);
    pp->head_a = to + P[P_HD];
    rotp(1.2f, -9.8f, pp->head_a, &hx, &hy);
    j[TJ_HEAD][0] = pvx + hx;
    j[TJ_HEAD][1] = pvy + hy;
    float ua = L_UA * P[P_FAL], fa = L_FA * P[P_FAL];
    j[TJ_FEL][0] = j[TJ_FSH][0] + fsin(P[P_FUA]) * ua;
    j[TJ_FEL][1] = j[TJ_FSH][1] + fcos(P[P_FUA]) * ua;
    j[TJ_FHA][0] = j[TJ_FEL][0] + fsin(P[P_FFA]) * fa;
    j[TJ_FHA][1] = j[TJ_FEL][1] + fcos(P[P_FFA]) * fa;
    ua = L_UA * P[P_BAL];
    fa = L_FA * P[P_BAL];
    j[TJ_BEL][0] = j[TJ_BSH][0] + fsin(P[P_BUA]) * ua;
    j[TJ_BEL][1] = j[TJ_BSH][1] + fcos(P[P_BUA]) * ua;
    j[TJ_BHA][0] = j[TJ_BEL][0] + fsin(P[P_BFA]) * fa;
    j[TJ_BHA][1] = j[TJ_BEL][1] + fcos(P[P_BFA]) * fa;
    j[TJ_FHIP][0] = 1.6f;
    j[TJ_FHIP][1] = HIPJ;
    j[TJ_BHIP][0] = -1.6f;
    j[TJ_BHIP][1] = HIPJ;
    float ll = P[P_FLL];
    j[TJ_FKN][0] = j[TJ_FHIP][0] + fsin(P[P_FTH]) * L_TH * ll;
    j[TJ_FKN][1] = j[TJ_FHIP][1] + fcos(P[P_FTH]) * L_TH * ll;
    j[TJ_FAN][0] = j[TJ_FKN][0] + fsin(P[P_FSH]) * L_SH * ll;
    j[TJ_FAN][1] = j[TJ_FKN][1] + fcos(P[P_FSH]) * L_SH * ll;
    j[TJ_BKN][0] = j[TJ_BHIP][0] + fsin(P[P_BTH]) * L_TH;
    j[TJ_BKN][1] = j[TJ_BHIP][1] + fcos(P[P_BTH]) * L_TH;
    j[TJ_BAN][0] = j[TJ_BKN][0] + fsin(P[P_BSH]) * L_SH;
    j[TJ_BAN][1] = j[TJ_BKN][1] + fcos(P[P_BSH]) * L_SH;
    float tx, ty;
    rotp(8, 1.2f, P[P_FFO], &tx, &ty);
    j[TJ_FTOE][0] = j[TJ_FAN][0] + tx;
    j[TJ_FTOE][1] = j[TJ_FAN][1] + ty;
    rotp(6.3f, -2.2f, to, &j[TJ_KNOT][0], &j[TJ_KNOT][1]);
    // hair, headband tails and sash (the speed of the hip moves them)
    float vxl = vx * pp->face, sp = tfn_clampf(sqrtf(vxl * vxl + vy * vy) / 70, 0, 1), rotd = pp->rot / D2R;
    float th0 = 122 - rotd + tfn_clampf(vxl * 0.55f, -35, 55) + tfn_clampf(vy * 0.45f, -30, 45);
    float ha[T_HAIR_N - 1], b1[T_BAND_N - 1], b2[T_BAND_N - 1], sa[T_SASH_N - 1];
    for (int i = 0; i < T_HAIR_N - 1; i++) {
        ha[i] = (th0 - i * 7 + fsin(t * 5.5f - i * 0.9f) * (4 + i * 3) * (0.5f + sp)) * D2R;
    }
    for (int i = 0; i < T_BAND_N - 1; i++) {
        b1[i] = (th0 + 16 - i * 4 + fsin(t * 9 - i * 1.1f) * (6 + i * 4) * (0.5f + sp)) * D2R;
        b2[i] = (th0 - 6 - i * 5 + fsin(t * 8.2f - i * 1.0f + 1) * (6 + i * 4) * (0.5f + sp)) * D2R;
    }
    float s0 = 96 - rotd + tfn_clampf(vxl * 0.4f, -25, 45);
    for (int i = 0; i < T_SASH_N - 1; i++) {
        sa[i] = (s0 + i * 6 + fsin(t * 6.5f - i) * (4 + i * 3) * (0.4f + sp)) * D2R;
    }
    float px, py;
    rotp(-7.6f, -6.0f, pp->head_a, &px, &py);
    chain(j[TJ_HEAD][0] + px, j[TJ_HEAD][1] + py, ha, T_HAIR_N - 1, 3.9f, pp->hair);
    rotp(-10, -4.2f, pp->head_a, &px, &py);
    chain(j[TJ_HEAD][0] + px, j[TJ_HEAD][1] + py, b1, T_BAND_N - 1, 3.2f, pp->band1);
    chain(j[TJ_HEAD][0] + px, j[TJ_HEAD][1] + py, b2, T_BAND_N - 1, 3.0f, pp->band2);
    chain(j[TJ_KNOT][0], j[TJ_KNOT][1], sa, T_SASH_N - 1, 2.4f, pp->sash);
    t_puppet_joint(pp, TJ_HEAD, &pp->head_w[0], &pp->head_w[1]);
}
void t_puppet_joint(const tpuppet_t *pp, int j, float *wx, float *wy)
{
    float k = pp->look && pp->look->scale > 0 ? pp->look->scale : 1;
    float c = fcos(pp->rot), s = fsin(pp->rot), x = pp->j[j][0] * k, y = pp->j[j][1] * k;
    *wx = pp->x + pp->face * (x * c - y * s);
    *wy = pp->hip_y + x * s + y * c;
}

// ---- drawing helpers (a pass 0 draws the outline, a pass 1 draws the paint)
static inline const tcol_t *tones(const tramp_t *m, int d)
{
    return d ? &m->c[0] : &m->c[1];
}
static void cap_draw(const float *a, const float *b, float r1, float r2, const tramp_t *m, int d, int pass)
{
    if (pass == 0) {
        t_capsule(a[0], a[1], b[0], b[1], r1 + OW, r2 + OW, OUT);
        return;
    }
    const tcol_t *tn = tones(m, d);
    float r = (r1 + r2) / 2, o = 0.3f * r;
    t_capsule(a[0], a[1], b[0], b[1], r1, r2, tn[0]);
    t_capsule(a[0] + LX * o, a[1] + LY * o, b[0] + LX * o, b[1] + LY * o, r1 * 0.68f, r2 * 0.68f, tn[1]);
    if (r * s_pxs >= 2.2f) {
        float o2 = 0.55f * r;
        t_capsule(a[0] + LX * o2, a[1] + LY * o2, b[0] + LX * o2, b[1] + LY * o2, r1 * 0.26f, r2 * 0.26f, tn[2]);
    }
}
static void chain_draw(const float (*pts)[2], int n, float r0, float r1, const tramp_t *m, int d, int pass)
{
    for (int i = 0; i < n - 1; i++) {
        float u = (float)i / (n - 1), v = (float)(i + 1) / (n - 1);
        cap_draw(pts[i], pts[i + 1], lerpf(r0, r1, u), lerpf(r0, r1, v), m, d, pass);
    }
}
// a rubber-hose limb: a curve from a through c (the elbow) to b
static void hose_draw(const float *a, const float *c, const float *b, float r, const tramp_t *m, int d, int pass)
{
    float cx = 2 * c[0] - (a[0] + b[0]) / 2, cy = 2 * c[1] - (a[1] + b[1]) / 2;
    float px = a[0], py = a[1];
    const tcol_t *tn = tones(m, d);
    for (int layer = 0; layer < (pass ? 2 : 1); layer++) {
        float rr = pass == 0 ? r + OW : layer == 0 ? r : r * 0.55f, o = layer ? r * 0.35f : 0;
        tcol_t col = pass == 0 ? OUT : tn[layer];
        px = a[0];
        py = a[1];
        for (int i = 1; i <= 4; i++) {
            float u = i / 4.0f, w = 1 - u;
            float x = w * w * a[0] + 2 * w * u * cx + u * u * b[0], y = w * w * a[1] + 2 * w * u * cy + u * u * b[1];
            t_capsule(px + LX * o, py + LY * o, x + LX * o, y + LY * o, rr, rr, col);
            px = x;
            py = y;
        }
    }
}
static void poly_shaded(const float *pts, int n, const tramp_t *m, int d, float off, int pass)
{
    if (pass == 0) {
        // the outline: the polygon, and a round stroke on each edge
        t_poly(pts, n, OUT);
        for (int i = 0, k = n - 1; i < n; k = i++) {
            t_line(pts[k * 2], pts[k * 2 + 1], pts[i * 2], pts[i * 2 + 1], 2 * OW, OUT);
        }
        return;
    }
    const tcol_t *tn = tones(m, d);
    t_poly(pts, n, tn[0]);
    t_mask_begin();
    t_mask_poly(pts, n);
    t_clip(true);
    t_push();
    t_translate(LX * off, LY * off);
    t_poly(pts, n, tn[1]);
    t_pop();
    t_clip(false);
}

// ---- body parts
static void glove_draw(const float *el, const float *ha, float r, const tramp_t *m, int d, int pass)
{
    float ang = atan2f(ha[1] - el[1], ha[0] - el[0]);
    float up = fcos(ang) >= 0 ? -1 : 1;
    t_push();
    t_translate(ha[0], ha[1]);
    t_rotate(ang);
    if (pass == 0) {
        t_ellipse(0.4f, 0, r + OW, r * 0.9f + OW, OUT);
        t_ellipse(-0.6f, up * r * 0.72f, 1.7f + OW, 1.4f + OW, OUT);
        t_capsule(-r - 1.4f, 0, -r + 0.6f, 0, 2 + OW, 2 + OW, OUT);
    } else {
        const tcol_t *tn = tones(m, d);
        t_capsule(-r - 1.4f, 0, -r + 0.6f, 0, 2, 2, tones(&SHOE, d)[1]);
        t_ellipse(0.4f, 0, r, r * 0.9f, tn[0]);
        t_ellipse(0.8f, up * 0.7f, r * 0.78f, r * 0.66f, tn[1]);
        t_ellipse(1.4f, up * r * 0.42f, r * 0.34f, r * 0.2f, tn[2]);
        t_ellipse(-0.6f, up * r * 0.72f, 1.7f, 1.4f, tn[1]);
    }
    t_pop();
}
// a bare hand: a small fist with a thumb
static void hand_draw(const float *el, const float *ha, const tramp_t *sk, int pass)
{
    float ang = atan2f(ha[1] - el[1], ha[0] - el[0]), r = 2.1f;
    float up = fcos(ang) >= 0 ? -1 : 1;
    t_push();
    t_translate(ha[0], ha[1]);
    t_rotate(ang);
    if (pass == 0) {
        t_ellipse(0.4f, 0, r + OW, r * 0.85f + OW, OUT);
        t_ellipse(-0.2f, up * 1.5f, 1.0f + OW, 0.9f + OW, OUT);
    } else {
        t_ellipse(0.4f, 0, r, r * 0.85f, sk->c[1]);
        t_ellipse(0.7f, up * 0.4f, r * 0.7f, r * 0.55f, sk->c[2]);
        t_ellipse(-0.2f, up * 1.5f, 1.0f, 0.9f, sk->c[2]);
    }
    t_pop();
}
static const float SHOE_PTS[] = {-3.0f, -2.6f, -3.6f, 0.6f, -3.4f, 2.6f, 7.6f,  2.6f,  9.0f,
                                 1.2f,  8.2f,  -0.9f, 4.2f, -1.8f, 1.6f, -3.4f, -1.6f, -3.6f};
static void shoe_draw(const float *an, float fo, const tramp_t *acc, int d, int pass)
{
    t_push();
    t_translate(an[0], an[1]);
    t_rotate(fo);
    poly_shaded(SHOE_PTS, 9, &SHOE, d, 1.2f, pass);
    if (pass) {
        t_rect(-3.5f, 1.5f, 12, 1.1f, SOLEC);
        tcol_t c = tones(acc, d)[1];
        t_line(-0.5f, -2.4f, 2.6f, 0.4f, 1, c);
        t_line(2.6f, 0.4f, 5.6f, -0.8f, 1, c);
    }
    t_pop();
}
static void leg_draw(const tpuppet_t *pp, int front, int pass)
{
    const float (*j)[2] = pp->j;
    const tlook_t *lk = pp->look;
    int d = front ? 0 : 1;
    const float *hip = j[front ? TJ_FHIP : TJ_BHIP], *kn = j[front ? TJ_FKN : TJ_BKN], *an = j[front ? TJ_FAN : TJ_BAN];
    float fo = pp->P[front ? P_FFO : P_BFO];
    float dx = an[0] - kn[0], dy = an[1] - kn[1], L = sqrtf(dx * dx + dy * dy);
    if (L < 0.01f) {
        L = 0.01f;
    }
    float w0[2] = {an[0] - dx * 2.0f / L, an[1] - dy * 2.0f / L},
          w1[2] = {an[0] - dx * 0.4f / L, an[1] - dy * 0.4f / L};
    float mid[2] = {(kn[0] + an[0]) / 2, (kn[1] + an[1]) / 2};
    shoe_draw(an, fo, &lk->acc, d, pass);
    cap_draw(w0, w1, 1.7f, 1.7f, &lk->acc, d, pass);
    hose_draw(kn, mid, an, 1.35f, &lk->skin, d, pass);
    cap_draw(hip, kn, 3.5f, 3.1f, &lk->pants, d, pass);
}
static void arm_draw(const tpuppet_t *pp, int front, int pass)
{
    const float (*j)[2] = pp->j;
    int d = front ? 0 : 1;
    const float *sh = j[front ? TJ_FSH : TJ_BSH], *el = j[front ? TJ_FEL : TJ_BEL], *ha = j[front ? TJ_FHA : TJ_BHA];
    hose_draw(sh, el, ha, 1.35f, &pp->look->skin, d, pass);
    if (pp->look->extra & T_X_HANDS) {
        hand_draw(el, ha, &pp->look->skin, pass);
    } else {
        glove_draw(el, ha, front ? 4.3f : 3.9f, &pp->look->acc, d, pass);
    }
}
static const float PEL[] = {-6.2f, -1.6f, 6.2f, -1.6f, 6.8f, 2.4f, 4.6f, 4.8f, -4.4f, 4.8f, -6.8f, 2.4f};
static const float TORSO[] = {-5.4f, -1.2f,  5.6f,  -1.2f,  6.4f,  -5,  6.8f,  -8.6f, 5.8f,  -11.8f,
                              2.6f,  -13.6f, -2.2f, -13.6f, -5.8f, -12, -6.6f, -8.4f, -6.2f, -4.4f};
static const float SASH[] = {-6.0f, -3.6f, 6.2f, -3.6f, 6.2f, -1.0f, -6.0f, -1.0f};
static void torso_draw(const tpuppet_t *pp, int pass)
{
    const tlook_t *lk = pp->look;
    float to = pp->P[P_TO];
    t_push();
    t_rotate(to * 0.4f);
    poly_shaded(PEL, 6, &lk->pants, 0, 1.4f, pass);
    t_pop();
    t_push();
    t_rotate(to);
    poly_shaded(TORSO, 10, &lk->top, 0, 2.0f, pass);
    bool sash = !(lk->extra & T_X_NOSASH);
    if (pass == 0) {
        if (sash) {
            poly_shaded(SASH, 4, &lk->acc, 0, 0, 0);
            t_ellipse(6.3f, -2.2f, 1.7f + OW, 1.4f + OW, OUT);
        }
    } else {
        t_ellipse(3.8f, -9.4f, 1.6f, 2.2f, lk->top.c[3]);
        tcol_t ec = lk->acc.c[1];
        if (lk->emblem == 'p') {  // pi
            t_line(-1.8f, -9.6f, 3.8f, -10.0f, 1.0f, ec);
            t_line(-0.2f, -9.8f, -0.8f, -5.4f, 1.0f, ec);
            t_line(2.2f, -9.9f, 2.3f, -6.0f, 1.0f, ec);
            t_line(2.3f, -6.0f, 3.3f, -5.5f, 1.0f, ec);
        } else if (lk->emblem == 't') {  // tau
            t_line(-1.8f, -9.6f, 3.8f, -10.0f, 1.0f, ec);
            t_line(1.0f, -9.8f, 0.9f, -6.2f, 1.0f, ec);
            t_line(0.9f, -6.2f, 2.0f, -5.5f, 1.0f, ec);
        } else if (lk->emblem == 's') {  // sigma
            t_line(3.8f, -10.2f, -1.4f, -10.2f, 1.0f, ec);
            t_line(-1.4f, -10.2f, 1.4f, -7.9f, 1.0f, ec);
            t_line(1.4f, -7.9f, -1.4f, -5.6f, 1.0f, ec);
            t_line(-1.4f, -5.6f, 3.8f, -5.6f, 1.0f, ec);
        } else if (lk->emblem == 'd') {  // delta
            t_line(1.0f, -10.4f, -1.9f, -5.6f, 1.0f, ec);
            t_line(-1.9f, -5.6f, 3.9f, -5.6f, 1.0f, ec);
            t_line(3.9f, -5.6f, 1.0f, -10.4f, 1.0f, ec);
        } else if (lk->emblem == 'r') {  // square root
            t_line(-2.0f, -7.6f, -0.9f, -7.0f, 1.0f, ec);
            t_line(-0.9f, -7.0f, 0.4f, -5.4f, 1.0f, ec);
            t_line(0.4f, -5.4f, 1.8f, -10.2f, 1.0f, ec);
            t_line(1.8f, -10.2f, 4.4f, -10.2f, 1.0f, ec);
        } else if (lk->emblem == 'h') {  // theta
            t_push();
            t_translate(1.0f, -7.9f);
            t_scale(1, 1.3f);
            t_arc(0, 0, 2.0f, 0, 6.2832f, 0.85f, ec);
            t_pop();
            t_line(-0.8f, -7.9f, 2.8f, -7.9f, 0.9f, ec);
        } else if (lk->emblem == 'o') {  // omega
            t_arc(1.0f, -8.4f, 2.5f, 3.1416f * 0.72f, 3.1416f * 2.28f, 1.0f, ec);
            t_line(-1.9f, -5.6f, -0.6f, -5.6f, 1.0f, ec);
            t_line(2.6f, -5.6f, 3.9f, -5.6f, 1.0f, ec);
        } else if (lk->emblem == 'i') {  // infinity
            t_arc(-0.4f, -7.8f, 1.5f, 0, 6.2832f, 0.9f, ec);
            t_arc(2.6f, -7.8f, 1.5f, 0, 6.2832f, 0.9f, ec);
        }
        if (sash) {
            t_poly(SASH, 4, lk->acc.c[1]);
            t_rect(-5.8f, -3.5f, 11.8f, 1.3f, lk->acc.c[2]);
            t_ellipse(6.3f, -2.2f, 1.7f, 1.4f, lk->acc.c[2]);
            t_ellipse(6.6f, -2.7f, 0.7f, 0.5f, lk->acc.c[3]);
        }
    }
    t_pop();
}
static const float HAIR[] = {9.0f,   -5.2f, 9.4f,   -8.0f, 7.2f,  -11.0f, 3.0f,  -12.8f, -2.6f, -12.6f, -7.4f, -10.2f,
                             -10.2f, -5.6f, -10.6f, 0,     -9.4f, 5.0f,   -6.6f, 6.4f,   -5.4f, 1.4f,   -4.6f, -3.0f,
                             -2.0f,  -4.6f, 0.4f,   -3.4f, 2.0f,  -5.6f,  4.2f,  -3.8f,  5.8f,  -6.0f,  7.4f,  -4.6f};
static const float TUFT[] = {0.6f, -12.4f, 2.8f, -16.4f, 4.6f, -12.0f};
static const float SPIKY[] = {9.0f,   -5.2f, 10.2f,  -9.5f, 6.8f,   -9.6f, 6.4f,   -14.6f, 2.8f,   -11.8f, 0.4f,
                              -16.2f, -2.0f, -12.2f, -5.6f, -15.0f, -6.4f, -10.6f, -10.8f, -10.4f, -10.2f, -5.6f,
                              -10.6f, 0,     -9.4f,  4.0f,  -6.6f,  5.4f,  -5.4f,  1.4f,   -4.6f,  -3.0f,  -2.0f,
                              -4.6f,  0.4f,  -3.4f,  2.0f,  -5.6f,  4.2f,  -3.8f,  5.8f,   -6.0f,  7.4f,   -4.6f};
static const float MOHAWK[] = {-7.0f,  -8.0f, -5.0f,  -15.0f, -2.0f, -11.8f, 0.6f,   -17.4f, 3.0f,
                               -12.4f, 5.8f,  -15.6f, 7.0f,   -9.4f, 2.4f,   -10.8f, -2.4f,  -10.6f};
static const float BEARD[] = {-3.4f, 2.0f, 0.0f, 4.6f, 3.0f,  4.2f, 6.0f,  4.4f,  9.0f,
                              3.6f,  9.2f, 7.0f, 6.8f, 10.4f, 2.2f, 11.6f, -3.0f, 9.6f};
static const float CAPDOME[] = {-10.0f, -5.0f, -8.2f, -10.6f, -3.0f, -13.8f, 3.0f, -13.8f, 8.2f, -10.6f, 9.8f, -5.0f};
static const float VISOR[] = {6.0f, -6.6f, 14.6f, -5.8f, 14.2f, -4.0f, 6.2f, -4.2f};
static const float CHIN[] = {1.5f, 5.5f, 8.7f, 4.0f, 8.6f, 7.0f, 6.4f, 9.6f, 2.0f, 10.6f, -3.2f, 9.0f};
static void face_draw(const tpuppet_t *pp)
{
    const tlook_t *lk = pp->look;
    int ex = pp->pose.ex;
    tcol_t brow = lk->hair.c[0];
    float tilt = ex == EX_GRIT ? 1.2f : ex == EX_FURY ? 1.8f : ex == EX_SHOUT ? 1.4f : ex == EX_HURT ? -1.2f : 0;
    if (ex == EX_HURT) {
        t_line(2.6f, -2.2f, 5.4f, -0.6f, 0.9f, DARK);
        t_line(5.4f, -0.6f, 2.6f, 0.8f, 0.9f, DARK);
        t_line(9.0f, -2.0f, 7.4f, -0.6f, 0.9f, DARK);
        t_line(7.4f, -0.6f, 9.0f, 0.6f, 0.9f, DARK);
    } else if (ex == EX_KO) {
        t_line(2.8f, -2.4f, 6.0f, 0.8f, 0.9f, DARK);
        t_line(2.8f, 0.8f, 6.0f, -2.4f, 0.9f, DARK);
        t_line(7.4f, -2.0f, 9.2f, 0.6f, 0.9f, DARK);
        t_line(7.4f, 0.6f, 9.2f, -2.0f, 0.9f, DARK);
    } else if (ex == EX_SMILE) {
        t_arc(4.4f, 0.2f, 2.0f, 3.1416f * 1.1f, 3.1416f * 1.9f, 0.9f, DARK);
        t_arc(8.2f, 0.3f, 1.3f, 3.1416f * 1.1f, 3.1416f * 1.9f, 0.9f, DARK);
    } else {
        float sq = ex == EX_SHOUT ? 0.6f : 1;
        t_ellipse(4.4f, -0.6f, 3.0f, 3.3f * sq + 0.5f, OUT);
        t_ellipse(8.0f, -0.5f, 2.0f, 3.0f * sq + 0.5f, OUT);
        t_ellipse(4.4f, -0.6f, 2.5f, 3.3f * sq, EYEW);
        t_ellipse(8.0f, -0.5f, 1.5f, 3.0f * sq, EYEW);
        tcol_t pc = ex == EX_FURY ? lk->acc.c[2] : DARK;
        t_ellipse(5.3f, -0.2f, 1.3f, 1.5f * sq, pc);
        t_ellipse(8.6f, -0.1f, 0.95f, 1.3f * sq, pc);
        t_ellipse(4.9f, -1.2f, 0.5f, 0.5f, ex == EX_FURY ? lk->acc.c[3] : WHITE);
    }
    t_line(2.0f, -4.6f - (tilt < 0 ? -tilt : 0), 6.4f, -4.2f + tilt, 1.1f, brow);
    t_line(7.4f, -4.0f + tilt * 0.6f, 9.1f, -4.6f, 1.1f, brow);
    if (ex == EX_SHOUT || ex == EX_KO || ex == EX_HURT) {
        bool big = ex == EX_SHOUT;
        t_ellipse(6.2f, 6.0f, big ? 3.0f : 2.2f, big ? 3.6f : 2.4f, OUT);
        t_ellipse(6.2f, 6.0f, big ? 2.4f : 1.6f, big ? 3.0f : 1.8f, DARK);
        t_ellipse(6.2f, big ? 7.6f : 7.0f, big ? 1.8f : 1.2f, big ? 1.2f : 0.8f, TONGUE);
        if (big) {
            t_rect(4.4f, 3.2f, 3.6f, 0.9f, EYEW);
        }
        if (ex == EX_KO) {
            t_ellipse(7.2f, 8.8f, 1.1f, 1.8f, TONGUE);
        }
    } else if (ex == EX_GRIT || ex == EX_FURY) {
        t_rect(3.4f, 4.4f, 5.4f, 2.8f, OUT);
        t_rect(3.8f, 4.8f, 4.6f, 2.0f, EYEW);
        if (s_pxs >= 2) {
            t_rect(3.8f, 5.7f, 4.6f, 0.3f, DARK);
            t_rect(5.3f, 4.8f, 0.3f, 2.0f, DARK);
            t_rect(6.9f, 4.8f, 0.3f, 2.0f, DARK);
        }
    } else if (ex == EX_SMILE) {
        float p[14];
        for (int i = 0; i < 7; i++) {
            float u = i / 6.0f, w = 1 - u;
            p[i * 2] = w * w * 2.6f + 2 * w * u * 6 + u * u * 9.0f;
            p[i * 2 + 1] = w * w * 4.2f + 2 * w * u * 11.2f + u * u * 3.8f;
        }
        t_poly(p, 7, OUT);
        t_ellipse(6.0f, 6.6f, 1.8f, 1.0f, TONGUE);
        t_rect(3.6f, 4.4f, 4.6f, 1.0f, EYEW);
    } else {
        float x0 = 3.8f, y0 = 5.6f;
        for (int i = 1; i <= 4; i++) {
            float u = i / 4.0f, w = 1 - u, x = w * w * 3.8f + 2 * w * u * 6.4f + u * u * 8.6f,
                  y = w * w * 5.6f + 2 * w * u * 7.2f + u * u * 4.8f;
            t_line(x0, y0, x, y, 0.8f, DARK);
            x0 = x;
            y0 = y;
        }
    }
    if (lk->extra & T_X_GLASSES) {
        t_arc(4.4f, -0.6f, 3.4f, 0, 6.2832f, 0.75f, OUT);
        t_arc(8.1f, -0.5f, 2.3f, 0, 6.2832f, 0.75f, OUT);
        t_line(6.8f, -1.2f, 7.0f, -1.2f, 0.8f, OUT);
        t_line(1.0f, -1.0f, -3.0f, -0.2f, 0.75f, OUT);
    }
}
static void head_draw(const tpuppet_t *pp, int pass)
{
    const tlook_t *lk = pp->look;
    const tcol_t *sk = lk->skin.c, *hr = lk->hair.c, *ac = lk->acc.c;
    t_push();
    t_translate(pp->j[TJ_HEAD][0], pp->j[TJ_HEAD][1]);
    t_rotate(pp->head_a);
    if (pass == 0) {
        t_ellipse(0, 0, 9.6f + OW, 10.4f + OW, OUT);
        poly_shaded(CHIN, 6, &lk->skin, 0, 0, 0);
        t_ellipse(9.9f, 2.4f, 1.4f + OW, 1.2f + OW, OUT);
        if (lk->hair_style == T_HS_SPIKY) {
            poly_shaded(SPIKY, 22, &lk->hair, 0, 0, 0);
        } else if (lk->hair_style == T_HS_MOHAWK) {
            poly_shaded(MOHAWK, 9, &lk->hair, 0, 0, 0);
        } else if (lk->hair_style != T_HS_BALD) {
            poly_shaded(HAIR, 18, &lk->hair, 0, 0, 0);
            poly_shaded(TUFT, 3, &lk->hair, 0, 0, 0);
        }
        if (lk->extra & T_X_BAND) {
            t_ellipse(-10, -4.2f, 2.0f + OW, 2.0f + OW, OUT);
        }
        if (lk->extra & T_X_CAP) {
            poly_shaded(CAPDOME, 6, &lk->acc, 0, 0, 0);
            poly_shaded(VISOR, 4, &lk->acc, 0, 0, 0);
        }
    } else {
        t_ellipse(0, 0, 9.6f, 10.4f, sk[1]);
        t_poly(CHIN, 6, sk[1]);
        t_ellipse(1.6f, -0.8f, 7.8f, 8.8f, sk[2]);
        t_mask_begin();
        t_mask_poly(CHIN, 6);
        t_clip(true);
        t_ellipse(4.6f, 5.6f, 4.4f, 4.4f, sk[2]);
        t_clip(false);
        t_ellipse(5.4f, -3.2f, 2.2f, 1.0f, sk[3]);
        t_ellipse(9.9f, 2.4f, 1.4f, 1.2f, sk[2]);
        t_ellipse(2.4f, 3.6f, 2.0f, 1.0f, BLUSH);
        t_ellipse(-3.6f, 1.8f, 1.8f, 2.4f, sk[1]);
        t_ellipse(-3.4f, 2.0f, 0.7f, 1.2f, sk[0]);
        if (lk->extra & T_X_BEARD) {
            t_poly(BEARD, 9, hr[1]);
            t_ellipse(4.6f, 7.4f, 3.4f, 1.6f, hr[2]);
        }
        if (lk->hair_style == T_HS_SPIKY) {
            t_poly(SPIKY, 22, hr[1]);
            t_mask_begin();
            t_mask_poly(SPIKY, 22);
            t_clip(true);
            t_ellipse(1.6f, -10, 7.4f, 3.4f, hr[2]);
            t_ellipse(2.6f, -11, 3.2f, 0.9f, hr[3]);
            t_clip(false);
        } else if (lk->hair_style == T_HS_MOHAWK) {
            t_poly(MOHAWK, 9, hr[1]);
            t_ellipse(1, -12.6f, 2.2f, 2.4f, hr[2]);
        } else if (lk->hair_style == T_HS_BALD) {
            t_ellipse(2.4f, -7.6f, 3.4f, 1.6f, sk[3]);
        } else {
            t_poly(HAIR, 18, hr[1]);
            t_poly(TUFT, 3, hr[1]);
            t_mask_begin();
            t_mask_poly(HAIR, 18);
            t_clip(true);
            t_ellipse(1.6f, -9.6f, 7.4f, 3.2f, hr[2]);
            t_ellipse(2.6f, -10.6f, 3.2f, 0.9f, hr[3]);
            t_clip(false);
        }
        if (lk->extra & T_X_BAND) {
            t_capsule(-9.9f, -4.6f, 7.0f, -6.6f, 1.5f, 1.4f, ac[1]);
            t_capsule(-9.6f, -5.1f, 6.8f, -7.1f, 0.8f, 0.7f, ac[2]);
            t_ellipse(-10, -4.2f, 2.0f, 2.0f, ac[1]);
            t_ellipse(-9.7f, -4.7f, 1.1f, 1.1f, ac[2]);
        }
        if (lk->extra & T_X_CAP) {
            t_poly(CAPDOME, 6, ac[1]);
            t_ellipse(1, -11, 6.5f, 2.2f, ac[2]);
            t_poly(VISOR, 4, ac[0]);
        }
        face_draw(pp);
    }
    t_pop();
}
static void tails_draw(const tpuppet_t *pp, int pass)
{
    const tlook_t *lk = pp->look;
    if (lk->extra & T_X_BAND) {
        chain_draw((const float (*)[2])pp->band1, T_BAND_N, 0.9f, 0.55f, &lk->acc, 1, pass);
        chain_draw((const float (*)[2])pp->band2, T_BAND_N, 0.85f, 0.5f, &lk->acc, 1, pass);
    }
    if (lk->hair_style == T_HS_PONY) {
        chain_draw((const float (*)[2])pp->hair, T_HAIR_N, 4.2f, 1.4f, &lk->hair, 0, pass);
    }
    if (lk->hair_style == T_HS_LONG) {
        chain_draw((const float (*)[2])pp->hair, T_HAIR_N, 6.0f, 3.4f, &lk->hair, 0, pass);
    }
    if (lk->hair_style == T_HS_AFRO) {
        t_push();
        t_translate(pp->j[TJ_HEAD][0], pp->j[TJ_HEAD][1]);
        t_rotate(pp->head_a);
        if (pass == 0) {
            t_ellipse(-1.5f, -5.5f, 13.5f + OW, 12 + OW, OUT);
        } else {
            t_ellipse(-1.5f, -5.5f, 13.5f, 12, lk->hair.c[1]);
            t_ellipse(0, -9, 9, 6.5f, lk->hair.c[2]);
            t_ellipse(2, -12, 3.5f, 1.6f, lk->hair.c[3]);
        }
        t_pop();
    }
}
void t_puppet_draw(const tpuppet_t *pp, float pxs)
{
    s_pxs = pxs;
    float pivot = pp->air ? pp->hip_y : 0;
    t_push();
    float k = pp->look->scale > 0 ? pp->look->scale : 1;
    t_translate(pp->x, pivot);
    t_scale(pp->sx, pp->sy);
    t_translate(0, pp->hip_y - pivot);
    t_scale(k, k);
    t_scale((float)pp->face, 1);
    t_rotate(pp->rot);
    for (int g = 0; g < 7; g++) {
        for (int pass = 0; pass < 2; pass++) {
            switch (g) {
            case 0:
                tails_draw(pp, pass);
                break;
            case 1:
                arm_draw(pp, 0, pass);
                break;
            case 2:
                leg_draw(pp, 0, pass);
                break;
            case 3:
                leg_draw(pp, 1, pass);
                break;
            case 4:
                torso_draw(pp, pass);
                if (!(pp->look->extra & T_X_NOSASH)) {
                    chain_draw((const float (*)[2])pp->sash, T_SASH_N, 0.9f, 0.6f, &pp->look->acc, 0, pass);
                }
                break;
            case 5:
                head_draw(pp, pass);
                break;
            default:
                arm_draw(pp, 1, pass);
                break;
            }
        }
    }
    t_pop();
}

// ================================================================ the VHS filter
// The work runs at the source width; each source pixel becomes 2 x 2 output pixels, and the
// second output row is darker (a scanline).
static uint8_t s_nz[4096];
static bool s_nz_ok;
#define VPAD 6
typedef struct {
    int32_t vY[T_LO_W + 2 * VPAD], pY[T_LO_W + 2 * VPAD + 1], pI[T_LO_W + 2 * VPAD + 1], pQ[T_LO_W + 2 * VPAD + 1];
} vrow_t;
static vrow_t s_vr_ps[2];       // one set of row buffers for each core (PSRAM)
static vrow_t *s_vr = s_vr_ps;  // internal RAM while two cores work (faster)
static int16_t s_vig[T_LO_W];
static int s_vig_w;
// the job of one frame (the two cores read it)
static struct {
    const tcol_t *src;
    uint16_t *dst;
    int w, h, fr, band, fk, fade, glitch, roll;
} s_vj;

static inline int nz(uint32_t i)
{
    return s_nz[i & 4095];
}
static inline int clamp255(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}
static inline uint16_t pack(int r, int g, int b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}
static void vhs_row(vrow_t *v, int r)
{
    const int w = s_vj.w, h = s_vj.h, W = w * 2, H = h * 2, fr = s_vj.fr, band = s_vj.band, fk = s_vj.fk,
              fade = s_vj.fade, glitch = s_vj.glitch, roll = s_vj.roll;
    const uint32_t black = (uint32_t)pack(8, 8, 8) * 0x10001u;
    do {
        uint32_t *d0 = (uint32_t *)(s_vj.dst + (2 * r) * W), *d1 = d0 + w;
        int yy = (2 * r + roll) % (H + 8);
        if (yy < 0) {
            yy += H + 8;
        }
        if (yy >= H) {
            for (int x = 0; x < w; x++) {
                d0[x] = d1[x] = black;
            }
            break;
        }
        int sr = yy >> 1;
        // the jitter of this line (in source pixels)
        int off = (nz((uint32_t)(r * 37 + fr * 101)) > 232) ? (nz((uint32_t)(r * 11 + fr)) & 1 ? 1 : -1) : 0;
        int db = r - band;
        if (db < 0) {
            db = -db;
        }
        int bk = db < 3 ? (3 - db) * 85 : 0;  // 0..255 in the tracking band
        if (bk) {
            off += (nz((uint32_t)(r * 13 + fr * 7)) - 128) * 5 / 128;
        }
        if (glitch && nz((uint32_t)((r >> 2) * 977 + fr * 31)) > 128) {
            off += (nz((uint32_t)((r >> 2) * 131 + fr * 17)) - 128) * 35 * glitch / (128 * 256);
        }
        if (r >= h - 3) {
            off += (r - (h - 3)) * 2 + (nz((uint32_t)(r + fr * 3)) >> 6);
        }
        const tcol_t *row = s_vj.src + sr * w;
        // the row with VPAD pixels of the edge color on each side: Y, and running sums of Y, I, Q
        v->pY[0] = v->pI[0] = v->pQ[0] = 0;
        for (int k = 0; k < w + 2 * VPAD; k++) {
            int sx = k - VPAD - off;
            if (sx < 0) {
                sx = 0;
            } else if (sx >= w) {
                sx = w - 1;
            }
            tcol_t c = row[sx];
            int R = (c >> 11) << 3, G = ((c >> 5) & 63) << 2, B = (c & 31) << 3;
            int Y = (77 * R + 150 * G + 29 * B) >> 8;
            v->vY[k] = Y;
            v->pY[k + 1] = v->pY[k] + Y;
            v->pI[k + 1] = v->pI[k] + ((153 * R - 70 * G - 82 * B) >> 8);
            v->pQ[k + 1] = v->pQ[k] + ((54 * R - 134 * G + 80 * B) >> 8);
        }
        bool drop = nz((uint32_t)(r * 7919 + fr * 104729)) > 252;
        int dx0 = nz((uint32_t)(r * 31 + fr)) * w / 256, dlen = 4 + (nz((uint32_t)(r * 17 + fr)) >> 4);
        int vy = 256 - ((2 * r - h) * (2 * r - h) * 56) / (h * h);
        int nadd = 12 + (bk * 90 >> 8);
        const int32_t *vY = v->vY, *pY = v->pY, *pI = v->pI, *pQ = v->pQ;
        for (int x = 0; x < w; x++) {
            int k = x + VPAD;
            int ym = (vY[k - 1] + 2 * vY[k] + vY[k + 1]) >> 2;
            int wide = (pY[k + 3] - pY[k - 2]) * 205 >> 10;                 // 5 pixels
            int L = ym + ((ym - wide) * 154 >> 8) + (vY[k - 5] * 20 >> 8);  // edge halo and a faint ghost
            L += (nz((uint32_t)(x * 7 + r * 613 + fr * 2963)) - 128) * nadd >> 7;
            int ci = (pI[k + 2] - pI[k - 5]) * 42 >> 8, cq = (pQ[k + 2] - pQ[k - 5]) * 42 >> 8;  // 7 pixels, lags right
            int R = L + ((245 * ci + 159 * cq) >> 8), G = L - ((70 * ci + 166 * cq) >> 8),
                B = L - ((283 * ci - 436 * cq) >> 8);
            R = 16 + (R * 230 >> 8);
            G = 12 + (G * 225 >> 8);
            B = 22 + (B * 220 >> 8);
            if (drop && x > dx0 && x < dx0 + dlen) {
                R = G = B = 200 + (nz((uint32_t)(x + fr)) >> 2);
            }
            if (fk) {
                R += (255 - R) * fk >> 8;
                G += (255 - G) * fk >> 8;
                B += (255 - B) * fk >> 8;
            }
            int m = (s_vig[x] * vy >> 8) * fade >> 8;
            int r0c = clamp255(R * m >> 8), g0 = clamp255(G * m >> 8), b0 = clamp255(B * m >> 8);
            uint32_t c0 = pack(r0c, g0, b0), c1 = pack(r0c * 215 >> 8, g0 * 215 >> 8, b0 * 215 >> 8);
            d0[x] = c0 | c0 << 16;
            d1[x] = c1 | c1 << 16;
        }
    } while (0);
}

static volatile int s_vnext;  // the next row to make
static void vhs_take_rows(vrow_t *v)
{
    for (;;) {
        int r = __atomic_fetch_add(&s_vnext, 1, __ATOMIC_RELAXED);
        if (r >= s_vj.h) {
            return;
        }
        vhs_row(v, r);
    }
}
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static TaskHandle_t s_vt;
static SemaphoreHandle_t s_vgo, s_vdone;
static volatile bool s_vquit;
static void vhs_worker(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_vgo, portMAX_DELAY);
        if (s_vquit) {
            break;
        }
        vhs_take_rows(&s_vr[1]);
        xSemaphoreGive(s_vdone);
    }
    xSemaphoreGive(s_vdone);
    vTaskDelete(NULL);
}
void t_two_cores(bool on)
{
    if (on && s_vr == s_vr_ps) {
        vrow_t *v = heap_caps_malloc(2 * sizeof(vrow_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (v) {
            s_vr = v;
        }
    } else if (!on && s_vr != s_vr_ps) {
        vrow_t *v = s_vr;
        s_vr = s_vr_ps;
        heap_caps_free(v);
    }
    if (on && !s_vt) {
        s_vgo = xSemaphoreCreateBinary();
        s_vdone = xSemaphoreCreateBinary();
        s_vquit = false;
        // under the audio (8), the LCD (7), the touch (5) and the IMU (4) tasks of core 1
        if (!s_vgo || !s_vdone || xTaskCreatePinnedToCore(vhs_worker, "toon", 3072, NULL, 3, &s_vt, 1) != pdPASS) {
            s_vt = NULL;
            ESP_LOGW("toon", "no second core: one core makes the tape");
        }
    } else if (!on && s_vt) {
        s_vquit = true;
        xSemaphoreGive(s_vgo);
        xSemaphoreTake(s_vdone, pdMS_TO_TICKS(500));
        vTaskDelay(pdMS_TO_TICKS(2));  // the worker deletes itself
        s_vt = NULL;
        vSemaphoreDelete(s_vgo);
        vSemaphoreDelete(s_vdone);
        s_vgo = s_vdone = NULL;
    }
}
#else
void t_two_cores(bool on)
{
    (void)on;
}
#endif

static bool s_vbusy;
void t_vhs_begin(const tcol_t *src, int w, int h, uint16_t *dst, const tvhs_t *o)
{
    if (!s_nz_ok) {
        uint32_t s = 12345;
        for (int i = 0; i < 4096; i++) {
            s = s * 1103515245u + 12345u;
            s_nz[i] = (uint8_t)(s >> 16);
        }
        s_nz_ok = true;
    }
    if (s_vig_w != w) {
        for (int x = 0; x < w; x++) {
            s_vig[x] = (int16_t)(256 - ((2 * x - w) * (2 * x - w) * 46) / (w * w));
        }
        s_vig_w = w;
    }
    s_vj.src = src;
    s_vj.dst = dst;
    s_vj.w = w;
    s_vj.h = h;
    s_vj.fr = (int)(o->t * 30) & 1023;
    s_vj.band = (int)((fmodf(o->t * 0.11f, 1.4f) - 0.2f) * h);
    s_vj.fk = (int)(tfn_clampf(o->flash, 0, 1) * 205);
    s_vj.fade = (int)(tfn_clampf(o->fade, 0, 1) * 256);
    s_vj.glitch = (int)(tfn_clampf(o->glitch, 0, 1) * 256);
    s_vj.roll = o->roll & ~1;
    s_vnext = 0;
    s_vbusy = false;
#ifdef ESP_PLATFORM
    if (s_vt) {
        s_vbusy = true;
        xSemaphoreGive(s_vgo);
    }
#endif
}
void t_vhs_end(void)
{
    vhs_take_rows(&s_vr[0]);
#ifdef ESP_PLATFORM
    if (s_vbusy && xSemaphoreTake(s_vdone, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE("toon", "the second core did not end its rows");
    }
#endif
    s_vbusy = false;
}
void t_vhs(const tcol_t *src, int w, int h, uint16_t *dst, const tvhs_t *o)
{
    t_vhs_begin(src, w, h, dst, o);
    t_vhs_end();
}

// ================================================================ other looks (for special moves)
static inline uint16_t swap16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
// The clean picture: each pixel 2 x 2, no tape.
void t_post_clean(const tcol_t *src, int w, int h, uint16_t *dst)
{
    for (int r = 0; r < h; r++) {
        uint32_t *d0 = (uint32_t *)(dst + 2 * r * 2 * w), *d1 = d0 + w;
        const tcol_t *row = src + r * w;
        for (int x = 0; x < w; x++) {
            uint32_t c = swap16(row[x]);
            d0[x] = d1[x] = c | c << 16;
        }
    }
}
// The looks of t_post_fx. Each source pixel becomes 2 x 2 output pixels.
static inline void put2(uint16_t *dst, int w, int x, int r, int R, int G, int B)
{
    R = R < 0 ? 0 : R > 255 ? 255 : R;
    G = G < 0 ? 0 : G > 255 ? 255 : G;
    B = B < 0 ? 0 : B > 255 ? 255 : B;
    uint32_t c = swap16((uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3)));
    uint32_t *d0 = (uint32_t *)(dst + 2 * r * 2 * w), *d1 = d0 + w;
    d0[x] = d1[x] = c | c << 16;
}
#define RGB8(c, R, G, B)                                                                                               \
    do {                                                                                                               \
        R = ((c) >> 11) << 3;                                                                                          \
        G = (((c) >> 5) & 63) << 2;                                                                                    \
        B = ((c) & 31) << 3;                                                                                           \
    } while (0)
void t_post_fx(const tcol_t *src, int w, int h, uint16_t *dst, int kind, float t)
{
    // the heat camera palette, and the hue rotation of this frame
    static uint8_t heat[256][3];
    static bool heat_ok;
    if (!heat_ok) {
        static const uint8_t K[6][3] = {{8, 0, 30},   {60, 0, 140},  {200, 0, 120},
                                        {255, 60, 0}, {255, 200, 0}, {255, 255, 230}};
        for (int i = 0; i < 256; i++) {
            float u = i / 255.0f * 5;
            int k = (int)u;
            if (k > 4) {
                k = 4;
            }
            float f = u - k;
            for (int c = 0; c < 3; c++) {
                heat[i][c] = (uint8_t)(K[k][c] + (K[k + 1][c] - K[k][c]) * f);
            }
        }
        heat_ok = true;
    }
    float a = t * 2.4f, cs = fcos(a), sn = fsin(a);
    int m[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float v = (i == j ? cs + (1 - cs) / 3 : (1 - cs) / 3) + ((j - i + 3) % 3 == 1   ? -1
                                                                     : (j - i + 3) % 3 == 2 ? 1
                                                                                            : 0) *
                                                                            sn * 0.57735f;
            m[i][j] = (int)(v * 256);
        }
    }
    uint32_t seed = (uint32_t)(t * 24) * 2654435761u;
    int scratch = (int)((seed >> 8) % (uint32_t)w), scratch2 = (int)((seed >> 20) % (uint32_t)w);
    for (int r = 0; r < h; r++) {
        const tcol_t *row = src + r * w;
        int aR = 0, aG = 0, aB = 0;
        for (int x = 0; x < w; x++) {
            int R, G, B;
            RGB8(row[x], R, G, B);
            int L = (77 * R + 150 * G + 29 * B) >> 8;
            switch (kind) {
            case T_FX_SMEAR:  // a speed smear: each pixel keeps some of the pixels at its left
                if (x == 0) {
                    aR = R;
                    aG = G;
                    aB = B;
                }
                aR += (R - aR) * 40 >> 8;
                aG += (G - aG) * 40 >> 8;
                aB += (B - aB) * 40 >> 8;
                put2(dst, w, x, r, (R + 2 * aR) / 3 - 6, (G + 2 * aG) / 3 + 4, (B + 2 * aB) / 3 + 30);
                break;
            case T_FX_SEPIA: {  // old film: sepia, a vignette, two scratches that jump
                int v = L * (256 - ((2 * x - w) * (2 * x - w) * 60) / (w * w)) >> 8;
                if (x == scratch || x == scratch2) {
                    v = v / 3 + 150;
                }
                put2(dst, w, x, r, v * 112 / 100, v * 90 / 100, v * 64 / 100);
                break;
            }
            case T_FX_RGB: {  // the red from the left, the blue from the right
                int xl = x - 3 < 0 ? 0 : x - 3, xr = x + 3 >= w ? w - 1 : x + 3;
                int R2, G2, B2, R3, G3, B3;
                RGB8(row[xl], R2, G2, B2);
                RGB8(row[xr], R3, G3, B3);
                (void)G2;
                (void)B2;
                (void)R3;
                (void)G3;
                put2(dst, w, x, r, R2, G, B3);
                break;
            }
            case T_FX_HEAT:
                put2(dst, w, x, r, heat[L][0], heat[L][1], heat[L][2]);
                break;
            case T_FX_NOIR: {  // black and white, hard; the reds stay red
                int v = (L - 128) * 2 + 128;
                if (R > G + 50 && R > B + 50) {
                    put2(dst, w, x, r, 255, 30, 40);
                } else {
                    put2(dst, w, x, r, v, v, v);
                }
                break;
            }
            default:  // the hues turn
                put2(dst, w, x, r, (m[0][0] * R + m[0][1] * G + m[0][2] * B) >> 8,
                     (m[1][0] * R + m[1][1] * G + m[1][2] * B) >> 8, (m[2][0] * R + m[2][1] * G + m[2][2] * B) >> 8);
                break;
            }
        }
    }
}
// Black and white comic ink: round halftone dots in 45 degree rows (two dots in each 8 x 8 tile).
static uint8_t s_dot[64];
static bool s_dot_ok;
void t_post_ink(const tcol_t *src, int w, int h, uint16_t *dst)
{
    if (!s_dot_ok) {
        static const float CX[5] = {0, 8, 0, 8, 4}, CY[5] = {0, 0, 8, 8, 4};
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                float best = 99;
                for (int k = 0; k < 5; k++) {
                    float dx = x + 0.5f - CX[k], dy = y + 0.5f - CY[k], d = sqrtf(dx * dx + dy * dy);
                    if (d < best) {
                        best = d;
                    }
                }
                float t = best / 3.19f;
                int v = (int)(t * t * 255);
                s_dot[y * 8 + x] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
        s_dot_ok = true;
    }
    const uint16_t ink = swap16(TC(0x161018)), paper = swap16(TC(0xf4ecd8));
    for (int Y = 0; Y < 2 * h; Y++) {
        const tcol_t *row = src + (Y >> 1) * w;
        uint16_t *d = dst + Y * 2 * w;
        const uint8_t *tile = &s_dot[(Y & 7) * 8];
        for (int X = 0; X < 2 * w; X++) {
            tcol_t c = row[X >> 1];
            int L = (77 * ((c >> 11) << 3) + 150 * (((c >> 5) & 63) << 2) + 29 * ((c & 31) << 3)) >> 8;
            int dark = (150 - L) * 2;  // L < 22: solid ink, L > 150: paper (skin is paper)
            d[X] = dark > tile[X & 7] ? ink : paper;
        }
    }
}
