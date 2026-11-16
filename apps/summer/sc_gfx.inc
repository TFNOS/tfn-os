// SUMMER CUP: the drawing core (included by app_summer.c). Fast rows with a fine dither (no colour bands on the
// AMOLED), the far background in one pass (each pixel written one time), soft capsules and polygons with smooth
// edges, the camera transform. No sqrtf / sinf / division in the pixel loops: in an app file they are calls of the
// library (sqrtf about 0.36 us, a division about 0.3 us).

// ---------------------------------------------------------------- small math
static inline float frsqrt(float x)
{
    union {
        float f;
        uint32_t i;
    } u = {x};
    u.i = 0x5f375a86u - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}
static inline float fsq(float x)
{
    return x > 1e-12f ? x * frsqrt(x) : 0;
}
static inline float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}
static inline int clampi(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}
static inline float smooth01(float t)
{
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}
static inline float wrap_pi(float a)
{
    while (a > (float)M_PI) {
        a -= 2 * (float)M_PI;
    }
    while (a < -(float)M_PI) {
        a += 2 * (float)M_PI;
    }
    return a;
}
// atan2 with a polynomial (about 0.005 rad of error; atan2f is a slow call in an app file)
static float fatan2(float y, float x)
{
    float ax = fabsf(x), ay = fabsf(y);
    if (ax < 1e-9f && ay < 1e-9f) {
        return 0;
    }
    float a = ax < ay ? ax / ay : ay / ax, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0) {
        r = 3.14159274f - r;
    }
    return y < 0 ? -r : r;
}
static uint32_t s_rng = 0x2545F491u;
static inline uint32_t rnd32(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static inline float frnd(float a, float b)
{
    return a + (b - a) * (float)(rnd32() & 0xFFFF) / 65535.0f;
}

// ---------------------------------------------------------------- colours
typedef struct {
    uint8_t r, g, b;
} col3;
static inline col3 C3(int r, int g, int b)
{
    col3 c = {(uint8_t)clampi(r, 0, 255), (uint8_t)clampi(g, 0, 255), (uint8_t)clampi(b, 0, 255)};
    return c;
}
static inline col3 mix3(col3 a, col3 b, float t)
{
    t = clampf(t, 0, 1);
    return C3((int)(a.r + (b.r - a.r) * t), (int)(a.g + (b.g - a.g) * t), (int)(a.b + (b.b - a.b) * t));
}
static inline uint16_t c16(col3 c)
{
    return rgb(c.r, c.g, c.b);
}
static inline uint16_t swp(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
// the two dithered pixels of a colour in the row y (the 2 x 2 Bayer order: 4 steps between two RGB565 colours)
static const uint8_t BAY[2][2] = {{0, 2}, {3, 1}};
typedef struct {
    uint16_t c[2][2];
} dc_t;  // [row parity][column parity]
static inline uint16_t dpx(col3 c, int t)
{
    int r = (c.r * 124 / 255 + t) >> 2, g = (c.g * 252 / 255 + t) >> 2, b = (c.b * 124 / 255 + t) >> 2;
    if (r > 31) {
        r = 31;
    }
    if (g > 63) {
        g = 63;
    }
    if (b > 31) {
        b = 31;
    }
    return swp((uint16_t)((r << 11) | (g << 5) | b));
}
static inline dc_t dcol(col3 c)
{
    dc_t d;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            d.c[j][i] = dpx(c, BAY[j][i]);
        }
    }
    return d;
}
// a light that adds: the frame colour d + s * a / 256
static inline uint16_t addc(uint16_t d, col3 s, int a)
{
    uint16_t D = swp(d);
    int r = (D >> 11) + ((s.r >> 3) * a >> 8), g = ((D >> 5) & 63) + ((s.g >> 2) * a >> 8),
        b = (D & 31) + ((s.b >> 3) * a >> 8);
    if (r > 31) {
        r = 31;
    }
    if (g > 63) {
        g = 63;
    }
    if (b > 31) {
        b = 31;
    }
    return swp((uint16_t)((r << 11) | (g << 5) | b));
}
// a mix of the frame colour d toward s (a = 0..256)
static inline uint16_t mixc(uint16_t d, uint16_t s, int a)
{
    if (a <= 0) {
        return d;
    }
    if (a >= 256) {
        return s;
    }
    uint32_t dd = swp(d), ss = swp(s), a5 = (uint32_t)(a + 4) >> 3;
    dd = (dd | (dd << 16)) & 0x07E0F81F;
    ss = (ss | (ss << 16)) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
    return swp((uint16_t)(r | (r >> 16)));
}

// ---------------------------------------------------------------- the frame: the visible span of each row
// (the round frame: only the circle shows; the corners are painted black with a fast fill)
#define MAXH 480  // (the round frame at the panel resolution of the 1.75C: 466 rows)
static int16_t s_x0[MAXH], s_x1[MAXH];
static int s_frame_w, s_frame_h;
static bool s_round;
static void frame_setup(void)
{
    s_frame_w = SW;
    s_frame_h = SH;
    s_round = gfx_round();
    for (int y = 0; y < SH && y < MAXH; y++) {
        if (!s_round) {
            s_x0[y] = 0;
            s_x1[y] = (int16_t)SW;
            continue;
        }
        float r = SW * 0.5f, dy = y + 0.5f - SH * 0.5f, h2 = r * r - dy * dy;
        float hw = h2 > 0 ? fsq(h2) + 1 : 0;
        int a = (int)(SW * 0.5f - hw), b = (int)(SW * 0.5f + hw + 1);
        s_x0[y] = (int16_t)clampi(a, 0, SW);
        s_x1[y] = (int16_t)clampi(b, 0, SW);
        if (s_x1[y] < s_x0[y]) {
            s_x1[y] = s_x0[y];
        }
    }
}
static void paint_corners(void)
{
    if (!s_round) {
        return;
    }
    for (int y = 0; y < SH; y++) {
        uint16_t *p = g_fb + y * SW;
        if (s_x0[y] > 0) {
            memset(p, 0, (size_t)s_x0[y] * 2);
        }
        if (s_x1[y] < SW) {
            memset(p + s_x1[y], 0, (size_t)(SW - s_x1[y]) * 2);
        }
    }
}
// THE FAR CLIP: while s_cf is on, the shapes draw only above the near layer (s_ntop[x] = the first row of the near
// layer in the column x): the sun, the gulls and the far things go between the far rows and the near layer
static int16_t s_ntop[660];
static bool s_cf;
#define CF(x, y) (s_cf && (y) >= s_ntop[x])
static inline bool vis(int x, int y)
{
    return (unsigned)y < (unsigned)SH && x >= s_x0[y] && x < s_x1[y] && !CF(x, y);
}
static inline void pxa(int x, int y, uint16_t c, int a)
{
    if (a <= 0 || !vis(x, y)) {
        return;
    }
    uint16_t *p = &g_fb[y * SW + x];
    *p = a >= 256 ? c : mixc(*p, c, a);
}
static inline void pxadd(int x, int y, col3 c, int a)
{
    if (a <= 0 || !vis(x, y)) {
        return;
    }
    uint16_t *p = &g_fb[y * SW + x];
    *p = addc(*p, c, a);
}
static void hspan(int x0, int x1, int y, uint16_t c, int a)
{
    if ((unsigned)y >= (unsigned)SH || a <= 0) {
        return;
    }
    if (x0 < s_x0[y]) {
        x0 = s_x0[y];
    }
    if (x1 > s_x1[y]) {
        x1 = s_x1[y];
    }
    uint16_t *p = &g_fb[y * SW];
    if (s_cf) {
        for (int x = x0; x < x1; x++) {
            if (y < s_ntop[x]) {
                p[x] = a >= 256 ? c : mixc(p[x], c, a);
            }
        }
        return;
    }
    if (a >= 256) {
        for (int x = x0; x < x1; x++) {
            p[x] = c;
        }
        return;
    }
    for (int x = x0; x < x1; x++) {
        p[x] = mixc(p[x], c, a);
    }
}
// the fill of [x0, x1) of the row y with the pair c[0] (even x), c[1] (odd x): 32-bit writes (no clip)
static inline void fill2(uint16_t *row, int x0, int x1, const uint16_t *c)
{
    if (x0 >= x1) {
        return;
    }
    if (x0 & 1) {
        row[x0] = c[1];
        x0++;
    }
    uint32_t w = (uint32_t)c[0] | ((uint32_t)c[1] << 16);
    uint32_t *q = (uint32_t *)(row + x0);
    int n = (x1 - x0) >> 1;
    for (int i = 0; i < n; i++) {
        q[i] = w;
    }
    if ((x1 - x0) & 1) {
        row[x1 - 1] = c[0];
    }
}
static void hspan_d(int x0, int x1, int y, const dc_t *d)
{
    if ((unsigned)y >= (unsigned)SH) {
        return;
    }
    if (x0 < s_x0[y]) {
        x0 = s_x0[y];
    }
    if (x1 > s_x1[y]) {
        x1 = s_x1[y];
    }
    uint16_t *p = &g_fb[y * SW];
    const uint16_t *c = d->c[y & 1];
    for (int x = x0; x < x1; x++) {
        p[x] = c[x & 1];
    }
}

// ---------------------------------------------------------------- the time of each stage of the frame (the log)
enum {
    PR_FAR,
    PR_PROPS,
    PR_NEAR,
    PR_FG,
    PR_HUD,
    PR_N
};
static int64_t s_prof[PR_N], s_prof_t0;
static inline void prof_start(void)
{
    s_prof_t0 = esp_timer_get_time();
}
static inline void prof_mark(int k)
{
    int64_t t = esp_timer_get_time();
    s_prof[k] += t - s_prof_t0;
    s_prof_t0 = t;
}

// ---------------------------------------------------------------- the camera
static float cam_x, cam_y, cam_z = 40;  // the world point in the middle of the view, the px of a metre
static float s_vcx, s_vcy;              // the middle of the view on the screen
static float s_shx, s_shy;              // the shake now (px)
static inline float SX(float wx)
{
    return s_vcx + (wx - cam_x) * cam_z + s_shx;
}
static inline float SY(float wy)
{
    return s_vcy - (wy - cam_y) * cam_z + s_shy;
}
static inline float WXs(float sx)
{
    return cam_x + (sx - s_vcx - s_shx) / cam_z;
}
static inline float WYs(float sy)
{
    return cam_y - (sy - s_vcy - s_shy) / cam_z;
}

// ---------------------------------------------------------------- soft shapes (smooth edges, only the visible part)
// a disc
static void disc(float cx, float cy, float r, uint16_t c, int a)
{
    if (r < 0.3f) {
        return;
    }
    int y0 = (int)floorf(cy - r - 1), y1 = (int)ceilf(cy + r + 1);
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > SH - 1) {
        y1 = SH - 1;
    }
    float ro = r + 0.5f, ri = r - 0.5f;
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy, h2 = ro * ro - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float hw = fsq(h2);
        int xa = (int)floorf(cx - hw), xb = (int)ceilf(cx + hw);
        float hi2 = ri * ri - dy * dy;
        int ia = xb + 1, ib = xb;
        if (hi2 > 0) {
            float hwi = fsq(hi2);
            ia = (int)ceilf(cx - hwi - 0.5f);
            ib = (int)floorf(cx + hwi - 0.5f);
            if (ia <= ib) {
                hspan(ia, ib + 1, y, c, a);
            }
        }
        for (int x = xa; x <= xb; x++) {
            if (x >= ia && x <= ib) {
                x = ib;
                continue;
            }
            float dx = x + 0.5f - cx, d2 = dx * dx + dy * dy;
            float cov = ro - fsq(d2);
            if (cov <= 0) {
                continue;
            }
            pxa(x, y, c, cov >= 1 ? a : (int)(a * cov));
        }
    }
}
// a glow that adds light (a soft falloff)
static void glow(float cx, float cy, float r, col3 c, int a)
{
    if (r < 1) {
        return;
    }
    int y0 = (int)(cy - r), y1 = (int)(cy + r);
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > SH - 1) {
        y1 = SH - 1;
    }
    float ir = 1.0f / r;
    for (int y = y0; y <= y1; y++) {
        float dy = (y + 0.5f - cy) * ir, q0 = 1 - dy * dy;
        if (q0 <= 0) {
            continue;
        }
        float hw = fsq(q0) * r;
        int xa = (int)(cx - hw), xb = (int)(cx + hw);
        if (xa < s_x0[y]) {
            xa = s_x0[y];
        }
        if (xb > s_x1[y] - 1) {
            xb = s_x1[y] - 1;
        }
        uint16_t *p = &g_fb[y * SW];
        for (int x = xa; x <= xb; x++) {
            float dx = (x + 0.5f - cx) * ir, q = q0 - dx * dx;
            if (q <= 0 || CF(x, y)) {
                continue;
            }
            p[x] = addc(p[x], c, (int)(a * q * q));
        }
    }
}
// a capsule: a line from (x0, y0) to (x1, y1) with the radius r0 at the start and r1 at the end, round ends
static void capsule(float x0, float y0, float x1, float y1, float r0, float r1, uint16_t c, int a)
{
    float rm = r0 > r1 ? r0 : r1;
    int bx0 = (int)floorf(fminf(x0, x1) - rm - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + rm + 1);
    int by0 = (int)floorf(fminf(y0, y1) - rm - 1), by1 = (int)ceilf(fmaxf(y0, y1) + rm + 1);
    if (by0 < 0) {
        by0 = 0;
    }
    if (by1 > SH - 1) {
        by1 = SH - 1;
    }
    if (bx0 < 0) {
        bx0 = 0;
    }
    if (bx1 > SW - 1) {
        bx1 = SW - 1;
    }
    if (bx1 < bx0 || by1 < by0) {
        return;
    }
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    float iL2 = L2 > 1e-6f ? 1.0f / L2 : 0;
    float dr = r1 - r0;
    float avy = fabsf(vy), L = fsq(L2);
    float ivy = avy > 1e-4f ? 1.0f / vy : 0;
    for (int y = by0; y <= by1; y++) {
        int xa = bx0, xb = bx1;
        if (avy > 0.5f * L && L > 0) {
            float t = (y + 0.5f - y0) * ivy;
            float xc = x0 + vx * clampf(t, 0, 1);
            float hw = (rm + 1.5f) * L / avy + 1;
            int a2 = (int)floorf(xc - hw), b2 = (int)ceilf(xc + hw);
            if (a2 > xa) {
                xa = a2;
            }
            if (b2 < xb) {
                xb = b2;
            }
        }
        if (xa < s_x0[y]) {
            xa = s_x0[y];
        }
        if (xb > s_x1[y] - 1) {
            xb = s_x1[y] - 1;
        }
        uint16_t *p = &g_fb[y * SW];
        float py = y + 0.5f - y0;
        for (int x = xa; x <= xb; x++) {
            float px = x + 0.5f - x0;
            float t = (px * vx + py * vy) * iL2;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float dx = px - t * vx, dy = py - t * vy, d2 = dx * dx + dy * dy;
            float r = r0 + dr * t, ro = r + 0.5f;
            if (d2 >= ro * ro || CF(x, y)) {
                continue;
            }
            float cov = ro - fsq(d2);
            if (cov >= 1) {
                p[x] = a >= 256 ? c : mixc(p[x], c, a);
            } else {
                p[x] = mixc(p[x], c, (int)(a * cov));
            }
        }
    }
}
// a filled polygon (even-odd) with a smooth edge: 4 rows of samples in each pixel row; up to 160 points
#define POLY_MAX 160
static float s_cov[660];
static void poly(const float *pt, int n, uint16_t c, int a)
{
    if (n < 3 || n > POLY_MAX) {
        return;
    }
    float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
    for (int i = 0; i < n; i++) {
        x0 = fminf(x0, pt[2 * i]);
        x1 = fmaxf(x1, pt[2 * i]);
        y0 = fminf(y0, pt[2 * i + 1]);
        y1 = fmaxf(y1, pt[2 * i + 1]);
    }
    int xa = (int)floorf(x0), xb = (int)ceilf(x1), ya = (int)floorf(y0), yb = (int)ceilf(y1);
    if (xa < 0) {
        xa = 0;
    }
    if (xb > SW - 1) {
        xb = SW - 1;
    }
    if (ya < 0) {
        ya = 0;
    }
    if (yb > SH - 1) {
        yb = SH - 1;
    }
    int w = xb - xa + 1;
    if (w <= 0 || w > 656) {
        return;
    }
    float xs[POLY_MAX];
    // the edges: the top y, the bottom y, the x at the top, the slope dx/dy (no division in the rows)
    static float e_y0[POLY_MAX], e_y1[POLY_MAX], e_x0[POLY_MAX], e_k[POLY_MAX];
    int ne = 0;
    for (int i = 0; i < n; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        float ax = pt[2 * i], ay = pt[2 * i + 1], bx = pt[2 * j], by = pt[2 * j + 1];
        if (ay == by) {
            continue;
        }
        if (ay > by) {
            float t = ax;
            ax = bx;
            bx = t;
            t = ay;
            ay = by;
            by = t;
        }
        e_y0[ne] = ay;
        e_y1[ne] = by;
        e_x0[ne] = ax;
        e_k[ne] = (bx - ax) / (by - ay);
        ne++;
    }
    for (int y = ya; y <= yb; y++) {
        bool any = false;
        int lo = w, hi = -1;
        // (the edges that touch this pixel row)
        static uint8_t act[POLY_MAX];
        int na = 0;
        for (int i = 0; i < ne; i++) {
            if (e_y0[i] < y + 1 && e_y1[i] > y) {
                act[na++] = (uint8_t)i;
            }
        }
        if (!na) {
            continue;
        }
        for (int s = 0; s < 4; s++) {
            float sy = y + 0.125f + s * 0.25f;
            int k = 0;
            for (int q = 0; q < na; q++) {
                int i = act[q];
                if (e_y0[i] <= sy && e_y1[i] > sy) {
                    xs[k++] = e_x0[i] + (sy - e_y0[i]) * e_k[i];
                }
            }
            for (int i = 1; i < k; i++) {
                for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                    float t = xs[j];
                    xs[j] = xs[j - 1];
                    xs[j - 1] = t;
                }
            }
            for (int j = 0; j + 1 < k; j += 2) {
                float l = fmaxf(0, xs[j] - xa), r = fminf((float)w, xs[j + 1] - xa);
                if (r <= l) {
                    continue;
                }
                int il = (int)l, ir = (int)r;
                if (!any) {
                    memset(s_cov, 0, sizeof(float) * (size_t)(w + 1));
                    any = true;
                }
                if (il < lo) {
                    lo = il;
                }
                if (ir > hi) {
                    hi = ir;
                }
                if (il == ir) {
                    s_cov[il] += (r - l) * 0.25f;
                    continue;
                }
                s_cov[il] += (il + 1 - l) * 0.25f;
                for (int i = il + 1; i < ir; i++) {
                    s_cov[i] += 0.25f;
                }
                if (ir < w) {
                    s_cov[ir] += (r - ir) * 0.25f;
                }
            }
        }
        if (!any) {
            continue;
        }
        int ra = xa + lo, rb = xa + (hi < w ? hi : w - 1);
        if (ra < s_x0[y]) {
            ra = s_x0[y];
        }
        if (rb > s_x1[y] - 1) {
            rb = s_x1[y] - 1;
        }
        uint16_t *p = &g_fb[y * SW];
        for (int x = ra; x <= rb; x++) {
            float cv = s_cov[x - xa];
            if (cv <= 0.002f || CF(x, y)) {
                continue;
            }
            if (cv >= 0.998f) {
                p[x] = a >= 256 ? c : mixc(p[x], c, a);
            } else {
                p[x] = mixc(p[x], c, (int)(a * cv));
            }
        }
    }
}
// a world capsule (metres)
static inline void wcap(float x0, float y0, float x1, float y1, float r0, float r1, uint16_t c, int a)
{
    capsule(SX(x0), SY(y0), SX(x1), SY(y1), fmaxf(0.6f, r0 * cam_z), fmaxf(0.6f, r1 * cam_z), c, a);
}
static inline void wdisc(float x, float y, float r, uint16_t c, int a)
{
    disc(SX(x), SY(y), fmaxf(0.6f, r * cam_z), c, a);
}

// a box with round corners and a smooth edge, with alpha (ui_rrect_a is not in the ABI of the app files)
static void rrect_a(int x, int y, int w, int h, int r, uint16_t c, int a)
{
    if (r * 2 > h) {
        r = h / 2;
    }
    if (r * 2 > w) {
        r = w / 2;
    }
    for (int j = 0; j < h; j++) {
        float dy = 0;
        if (j < r) {
            dy = r - (j + 0.5f);
        } else if (j >= h - r) {
            dy = (j + 0.5f) - (h - r);
        }
        if (dy <= 0) {
            hspan(x, x + w, y + j, c, a);
            continue;
        }
        float hw = fsq(fmaxf(0, r * r - dy * dy));
        float in = r - hw;  // (the inset of the edge in this row)
        int i0 = (int)in;
        float fr = 1 - (in - i0);
        hspan(x + i0 + 1, x + w - i0 - 1, y + j, c, a);
        pxa(x + i0, y + j, c, (int)(a * fr));
        pxa(x + w - 1 - i0, y + j, c, (int)(a * fr));
    }
}

// ---------------------------------------------------------------- text helpers (KF faces: English and Hebrew)
static void text_c(const kfont_t *f, int cx, int base, const char *s, uint16_t c)
{
    ui_text_c(f, cx, base, s, c);
}
static void text_sh(const kfont_t *f, int cx, int base, const char *s, uint16_t c)
{
    ui_text_c(f, cx + 1, base + 2, s, rgb(20, 24, 40));
    ui_text_c(f, cx, base, s, c);
}
static void num_c(const mfont_t *f, int cx, int base, const char *s, uint16_t c)
{
    mf_text_oc(f, cx, base, s, c, rgb(20, 30, 60), 2, 1);
}
