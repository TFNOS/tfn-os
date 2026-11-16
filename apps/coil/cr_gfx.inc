// COIL RIDER: the drawing core (included by app_coil.c; a copy of the core of JELLY COURIER, apps/jelly/jl_gfx.inc,
// which is a copy of SUMMER CUP, apps/summer/sc_gfx.inc). Fast rows with a fine dither (no colour bands on the
// AMOLED), the far background in one pass (each pixel written one time), soft capsules and polygons with smooth
// edges, the camera transform. No sqrtf / sinf / division in the pixel loops: in an app file they are calls of the
// library (sqrtf about 0.36 us, a division about 0.3 us).

// ---------------------------------------------------------------- small math
// (IN AN APP FILE floorf, ceilf, fminf, fmaxf and the float division are calls into the firmware: about 1 us each on
// the 3.49 (a bench: a capsule of 30 rows took 0.5 ms). These inline forms take a few cycles.)
static inline float cr_floorf(float x)
{
    int i = (int)x;
    return (float)(i - (x < (float)i));
}
static inline float cr_ceilf(float x)
{
    int i = (int)x;
    return (float)(i + (x > (float)i));
}
static inline float cr_fminf(float a, float b)
{
    return a < b ? a : b;
}
static inline float cr_fmaxf(float a, float b)
{
    return a > b ? a : b;
}
#define floorf cr_floorf
#define ceilf  cr_ceilf
#define fminf  cr_fminf
#define fmaxf  cr_fmaxf
// 1 / x with multiplications only (3 Newton steps: about 1e-7 relative error)
static inline float frcp(float x)
{
    float a = x < 0 ? -x : x;
    if (a < 1e-30f) {
        return x < 0 ? -1e30f : 1e30f;
    }
    union {
        float f;
        uint32_t i;
    } u = {a};
    u.i = 0x7EF311C7u - u.i;
    float y = u.f;
    y = y * (2 - a * y);
    y = y * (2 - a * y);
    y = y * (2 - a * y);
    return x < 0 ? -y : y;
}
// an inline sine (a table of 256 steps, linear between; fsin of gfx.h is a call into the firmware)
static float s_sint[257];
static void jsin_init(void)
{
    for (int i = 0; i <= 256; i++) {
        s_sint[i] = fsin(i * (6.2831853f / 256));
    }
}
static inline float jsin(float a)
{
    float f = a * (256 / 6.2831853f);
    int i = (int)f;
    if (f < 0) {
        i--;
    }
    float t = f - i;
    i &= 255;
    return s_sint[i] + (s_sint[i + 1] - s_sint[i]) * t;
}
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
    int r = ((c.r * 31869) >> 16) + t, g = ((c.g * 64764) >> 16) + t, b = ((c.b * 31869) >> 16) + t;  // (x * 124 / 255)
    r >>= 2;
    g >>= 2;
    b >>= 2;
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
// TWO CORES: each core draws only its band of rows. s_x0 / s_x1 = the visible span of each row FOR THE CORE THAT
// ASKS (a per-core copy: the rows out of its band are empty), so every drawing function clips to the band.
#ifdef ESP_PLATFORM
static TaskHandle_t s_wk_task;
static int s_c0 = 2;  // the span set of core 0: 0 = its band (the scene), 2 = the whole frame
static inline int cid(void)
{
    return s_wk_task && xTaskGetCurrentTaskHandle() == s_wk_task ? 1 : s_c0;
}
#else
static int s_core_pc = 2;  // (the PC: the band that draws now, one after the other; 2 = the whole frame)
static inline int cid(void)
{
    return s_core_pc;
}
#endif
static int16_t s_fx0[MAXH], s_fx1[MAXH];        // the visible span of each row of the frame
static int16_t s_xv0[3][MAXH], s_xv1[3][MAXH];  // the spans of each core (its band only), [2] = the whole frame
static int s_split = MAXH;                      // the first row of core 1
#define s_x0 (s_xv0[cid()])
#define s_x1 (s_xv1[cid()])
// the bands of the two cores (made again when the split moves)
static void bands_set(int split)
{
    s_split = split;
    for (int y = 0; y < MAXH; y++) {
        bool c0 = y < split;
        s_xv0[0][y] = c0 ? s_fx0[y] : 0;
        s_xv1[0][y] = c0 ? s_fx1[y] : 0;
        s_xv0[1][y] = c0 ? 0 : s_fx0[y];
        s_xv1[1][y] = c0 ? 0 : s_fx1[y];
        s_xv0[2][y] = s_fx0[y];
        s_xv1[2][y] = s_fx1[y];
    }
}
static int s_frame_w, s_frame_h;
static bool s_round;
static void frame_setup(void)
{
    s_frame_w = SW;
    s_frame_h = SH;
    s_round = gfx_round();
    memset(s_fx0, 0, sizeof s_fx0);
    memset(s_fx1, 0, sizeof s_fx1);
    for (int y = 0; y < SH && y < MAXH; y++) {
        if (!s_round) {
            s_fx0[y] = 0;
            s_fx1[y] = (int16_t)SW;
            continue;
        }
        float r = SW * 0.5f, dy = y + 0.5f - SH * 0.5f, h2 = r * r - dy * dy;
        float hw = h2 > 0 ? fsq(h2) + 1 : 0;
        int a = (int)(SW * 0.5f - hw), b = (int)(SW * 0.5f + hw + 1);
        s_fx0[y] = (int16_t)clampi(a, 0, SW);
        s_fx1[y] = (int16_t)clampi(b, 0, SW);
        if (s_fx1[y] < s_fx0[y]) {
            s_fx1[y] = s_fx0[y];
        }
    }
    bands_set(MAXH);  // (all rows on core 0 until the worker runs)
}
static void paint_corners(void)
{
    if (!s_round) {
        return;
    }
    for (int y = 0; y < SH; y++) {
        uint16_t *p = g_fb + y * SW;
        if (s_fx0[y] > 0) {
            memset(p, 0, (size_t)s_fx0[y] * 2);
        }
        if (s_fx1[y] < SW) {
            memset(p + s_fx1[y], 0, (size_t)(SW - s_fx1[y]) * 2);
        }
    }
}
// THE FAR CLIP: while s_cf is on, the shapes draw only above the near layer (s_ntop[x] = the first row of the near
// layer in the column x): the sun, the gulls and the far things go between the far rows and the near layer
static int16_t s_ntop[660];
static bool s_cfv[3];
#define s_cf     (s_cfv[cid()])
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
        const uint16_t pr[2] = {c, c};
        fill2(p, x0, x1, pr);
        return;
    }
    for (int x = x0; x < x1; x++) {
        p[x] = mixc(p[x], c, a);
    }
}
// the fill of [x0, x1) of the row y with the pair c[0] (even x), c[1] (odd x): 32-bit writes (no clip)
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
    PR_SETUP,
    PR_EDGES,
    PR_ROWS,
    PR_LINES,
    PR_THINGS,
    PR_BIKE,
    PR_HUD,
    PR_UPD,
    PR_N
};
static int64_t s_prof[PR_N], s_prof_t0;
static inline void prof_start(void)
{
    s_prof_t0 = esp_timer_get_time();
}
static inline void prof_mark(int k)
{
    if (cid() == 1) {
        return;
    }
    int64_t t = esp_timer_get_time();
    s_prof[k] += t - s_prof_t0;
    s_prof_t0 = t;
}

// ---------------------------------------------------------------- the camera
// The world (metres, y up) to the screen: a zoom, a shake and a small ROLL. The roll is a vertical shear of the whole
// picture (each column moves up or down by its distance from the middle times s_roll): for the small angles of the
// camera (at most about 5 degrees) it looks as a turn, and the column passes of the far layers and the ground keep
// one simple form.
static float cam_x, cam_y, cam_z = 40;  // the world point in the middle of the view, the px of a metre
static float s_vcx, s_vcy;              // the middle of the view on the screen
static float s_shx, s_shy;              // the shake now (px)
static float s_roll;                    // the roll now (the shear: px of y for each px of x)
static inline float SX(float wx)
{
    return s_vcx + (wx - cam_x) * cam_z + s_shx;
}
static inline float SYW(float wx, float wy)
{
    return s_vcy - (wy - cam_y) * cam_z + s_shy + (SX(wx) - s_vcx) * s_roll;
}
static inline float WXs(float sx)
{
    return cam_x + (sx - s_vcx - s_shx) / cam_z;
}
// the screen row of the world height wy in the screen column sx
static inline float SYc(float sx, float wy)
{
    return s_vcy - (wy - cam_y) * cam_z + s_shy + (sx - s_vcx) * s_roll;
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
    float ir = frcp(r);
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
// the x interval [*lo, *hi] of the row centre py inside a stadium of the radius R round the segment (x0, y0)-(x1, y1);
// false = no cover in this row
static bool stadium_span(float x0, float y0, float x1, float y1, float R, float py, float *lo, float *hi, float ivx,
                         float ivy, float iL2, float L)
{
    float a = 1e9f, b = -1e9f;
    float dy0 = py - y0, dy1 = py - y1;
    if (dy0 * dy0 < R * R) {
        float h = fsq(R * R - dy0 * dy0);
        a = fminf(a, x0 - h);
        b = fmaxf(b, x0 + h);
    }
    if (dy1 * dy1 < R * R) {
        float h = fsq(R * R - dy1 * dy1);
        a = fminf(a, x1 - h);
        b = fmaxf(b, x1 + h);
    }
    // the band: |s(x)| <= R and 0 <= t(x) <= 1, both linear in x
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    if (L2 > 1e-6f) {
        float lo2 = -1e9f, hi2 = 1e9f;
        // s(x) = ((x - x0) * -vy + dy0 * vx) / L  in [-R, R]
        if (fabsf(vy) > 1e-6f) {
            float c = dy0 * vx, xa = x0 + (c - R * L) * ivy, xb = x0 + (c + R * L) * ivy;
            if (xa > xb) {
                float t = xa;
                xa = xb;
                xb = t;
            }
            lo2 = fmaxf(lo2, xa);
            hi2 = fminf(hi2, xb);
        } else if (fabsf(dy0 * vx) > R * L) {
            lo2 = 1;
            hi2 = 0;
        }
        // t(x) = ((x - x0) * vx + dy0 * vy) / L2  in [0, 1]
        if (fabsf(vx) > 1e-6f) {
            float xa = x0 - dy0 * vy * ivx, xb = x0 + (L2 - dy0 * vy) * ivx;
            if (xa > xb) {
                float t = xa;
                xa = xb;
                xb = t;
            }
            lo2 = fmaxf(lo2, xa);
            hi2 = fminf(hi2, xb);
        } else {
            float t = dy0 * vy * iL2;
            if (t < 0 || t > 1) {
                lo2 = 1;
                hi2 = 0;
            }
        }
        if (lo2 <= hi2) {
            a = fminf(a, lo2);
            b = fmaxf(b, hi2);
        }
    }
    *lo = a;
    *hi = b;
    return a <= b;
}
// a capsule: a line from (x0, y0) to (x1, y1) with the radius r0 at the start and r1 at the end, round ends. Each row:
// the solid middle (inside the smaller radius) is a plain fill; only the pixels between it and the outer edge get
// the exact cover.
static void capsule(float x0, float y0, float x1, float y1, float r0, float r1, uint16_t c, int a)
{
    float rm = r0 > r1 ? r0 : r1, rn = r0 < r1 ? r0 : r1;
    int by0 = (int)floorf(fminf(y0, y1) - rm - 1), by1 = (int)ceilf(fmaxf(y0, y1) + rm + 1);
    if (by0 < 0) {
        by0 = 0;
    }
    if (by1 > SH - 1) {
        by1 = SH - 1;
    }
    if (by1 < by0) {
        return;
    }
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    float iL2 = L2 > 1e-6f ? frcp(L2) : 0;
    float dr = r1 - r0;
    float Ro = rm + 0.5f, Ri = rn - 0.5f;
    float ivx = fabsf(vx) > 1e-6f ? frcp(vx) : 0, ivy = fabsf(vy) > 1e-6f ? frcp(vy) : 0, Lq = fsq(L2);
    for (int y = by0; y <= by1; y++) {
        float py = y + 0.5f;
        float lo, hi;
        if (!stadium_span(x0, y0, x1, y1, Ro, py, &lo, &hi, ivx, ivy, iL2, Lq)) {
            continue;
        }
        int xa = (int)floorf(lo), xb = (int)ceilf(hi);
        if (xa < s_x0[y]) {
            xa = s_x0[y];
        }
        if (xb > s_x1[y] - 1) {
            xb = s_x1[y] - 1;
        }
        if (xa > xb) {
            continue;
        }
        int ia = xb + 1, ib = xb;  // the solid middle [ia, ib]
        if (Ri > 0.3f && !s_cf) {
            float li, hi2;
            if (stadium_span(x0, y0, x1, y1, Ri, py, &li, &hi2, ivx, ivy, iL2, Lq)) {
                ia = (int)ceilf(li);
                ib = (int)floorf(hi2) - 1;
                if (ia < xa) {
                    ia = xa;
                }
                if (ib > xb) {
                    ib = xb;
                }
            }
        }
        uint16_t *p = &g_fb[y * SW];
        float pyy = py - y0;
        for (int x = xa; x <= xb; x++) {
            if (x == ia && ia <= ib) {
                if (a >= 256) {
                    for (int k = ia; k <= ib; k++) {
                        p[k] = c;
                    }
                } else {
                    for (int k = ia; k <= ib; k++) {
                        p[k] = mixc(p[k], c, a);
                    }
                }
                x = ib;
                continue;
            }
            float px = x + 0.5f - x0;
            float t = (px * vx + pyy * vy) * iL2;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float dx = px - t * vx, dy = pyy - t * vy, d2 = dx * dx + dy * dy;
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
// an upright ellipse with a smooth edge (the faces, the glows): the solid middle of each row is a plain fill
static void ellipse_up(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    if (rx < 0.4f || ry < 0.4f) {
        return;
    }
    int y0 = (int)floorf(cy - ry - 1), y1 = (int)ceilf(cy + ry + 1);
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > SH - 1) {
        y1 = SH - 1;
    }
    float iry = frcp(ry), irx = frcp(rx), rmin = rx < ry ? rx : ry, irm = frcp(rmin);
    float ro = 1 + 0.5f * irm;  // (the outer edge in units of the radius)
    for (int y = y0; y <= y1; y++) {
        float dy = (y + 0.5f - cy) * iry, q = ro * ro - dy * dy;
        if (q <= 0) {
            continue;
        }
        float hw = fsq(q) * rx;
        int xa = (int)floorf(cx - hw), xb = (int)ceilf(cx + hw);
        if (xa < s_x0[y]) {
            xa = s_x0[y];
        }
        if (xb > s_x1[y] - 1) {
            xb = s_x1[y] - 1;
        }
        float ri = 1 - 0.5f * irm, qi = ri * ri - dy * dy;
        int ia = xb + 1, ib = xb;
        if (qi > 0) {
            float hi = fsq(qi) * rx;
            ia = (int)ceilf(cx - hi - 0.5f);
            ib = (int)floorf(cx + hi - 0.5f);
        }
        uint16_t *p = &g_fb[y * SW];
        for (int x = xa; x <= xb; x++) {
            if (x == ia && ia <= ib) {
                int e = ib < xb ? ib : xb;
                if (a >= 256) {
                    for (int k = ia; k <= e; k++) {
                        p[k] = c;
                    }
                } else {
                    for (int k = ia; k <= e; k++) {
                        p[k] = mixc(p[k], c, a);
                    }
                }
                x = e;
                continue;
            }
            float dx = (x + 0.5f - cx) * irx, d = fsq(dx * dx + dy * dy);
            float cov = (1 - d) * rmin + 0.5f;
            if (cov <= 0) {
                continue;
            }
            p[x] = cov >= 1 ? (a >= 256 ? c : mixc(p[x], c, a)) : mixc(p[x], c, (int)(a * cov));
        }
    }
}
// a filled polygon (even-odd) with a smooth edge: 4 rows of samples in each pixel row; up to 160 points
#define POLY_MAX 160
static float s_covv[3][660];
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
    static float e_y0v[3][POLY_MAX], e_y1v[3][POLY_MAX], e_x0v[3][POLY_MAX], e_kv[3][POLY_MAX];
    static uint8_t actv[3][POLY_MAX];
    int cc = cid();
    float *e_y0 = e_y0v[cc], *e_y1 = e_y1v[cc], *e_x0 = e_x0v[cc], *e_k = e_kv[cc], *s_cov = s_covv[cc];
    uint8_t *act = actv[cc];
    const int16_t *X0 = s_xv0[cc], *X1 = s_xv1[cc];
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
        e_k[ne] = (bx - ax) * frcp(by - ay);
        ne++;
    }
    for (int y = ya; y <= yb; y++) {
        bool any = false;
        int lo = w, hi = -1;
        // (the edges that touch this pixel row)
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
        if (ra < X0[y]) {
            ra = X0[y];
        }
        if (rb > X1[y] - 1) {
            rb = X1[y] - 1;
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
    capsule(SX(x0), SYW(x0, y0), SX(x1), SYW(x1, y1), fmaxf(0.6f, r0 * cam_z), fmaxf(0.6f, r1 * cam_z), c, a);
}
static inline void wdisc(float x, float y, float r, uint16_t c, int a)
{
    disc(SX(x), SYW(x, y), fmaxf(0.6f, r * cam_z), c, a);
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

// ---------------------------------------------------------------- the sprite cache (a shape drawn once, then copied)
// A sprite is drawn by its function two times into a scratch frame (on black, then on white): the difference gives the
// alpha of each pixel and the colour. The copy writes the solid pixels and mixes the edge pixels. The scratch frame
// exists only while a sprite is made.
typedef struct {
    int16_t x, y, w, h;
    uint16_t *c;
    uint8_t *a;
    int16_t *span;
    bool ok;
} sprite_t;
static void sprite_free(sprite_t *sp)
{
    if (sp->c) {
        heap_caps_free(sp->c);
    }
    if (sp->a) {
        heap_caps_free(sp->a);
    }
    if (sp->span) {
        heap_caps_free(sp->span);
    }
    memset(sp, 0, sizeof *sp);
}
static bool sprite_make(sprite_t *sp, int x, int y, int w, int h, void (*draw)(void))
{
    sprite_free(sp);
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > SW) {
        w = SW - x;
    }
    if (y + h > SH) {
        h = SH - y;
    }
    if (w <= 0 || h <= 0) {
        return false;
    }
    size_t n = (size_t)SW * SH;
    uint16_t *k = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint16_t *wt = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    sp->c = heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    sp->a = heap_caps_malloc((size_t)w * h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    sp->span = heap_caps_malloc((size_t)h * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!k || !wt || !sp->c || !sp->a || !sp->span) {
        if (k) {
            heap_caps_free(k);
        }
        if (wt) {
            heap_caps_free(wt);
        }
        sprite_free(sp);
        return false;
    }
    uint16_t *keep = g_fb;
    bool cf = s_cf;
    s_cf = false;
    memset(k, 0, n * 2);
    memset(wt, 0xFF, n * 2);
    g_fb = k;
    draw();
    g_fb = wt;
    draw();
    g_fb = keep;
    s_cf = cf;
    for (int j = 0; j < h; j++) {
        const uint16_t *pk = k + (size_t)(y + j) * SW + x, *pw = wt + (size_t)(y + j) * SW + x;
        uint16_t *oc = sp->c + (size_t)j * w;
        uint8_t *oa = sp->a + (size_t)j * w;
        for (int i = 0; i < w; i++) {
            uint16_t B = swp(pk[i]), W = swp(pw[i]);
            int gb = (B >> 5) & 63, gw = (W >> 5) & 63;
            int al = 63 - (gw - gb);
            if (al < 0) {
                al = 0;
            }
            if (al > 63) {
                al = 63;
            }
            int a8 = al * 255 / 63;
            oa[i] = (uint8_t)a8;
            if (a8 >= 250 || a8 == 0) {
                oc[i] = pk[i];
                continue;
            }
            // the colour = the colour on black / alpha
            int r = (B >> 11) * 255 / a8, g = gb * 255 / a8, b = (B & 31) * 255 / a8;
            if (r > 31) {
                r = 31;
            }
            if (g > 63) {
                g = 63;
            }
            if (b > 31) {
                b = 31;
            }
            oc[i] = swp((uint16_t)((r << 11) | (g << 5) | b));
        }
    }
    heap_caps_free(k);
    heap_caps_free(wt);
    // the first and the last pixel with a cover in each row (the copy skips the clear ends)
    for (int j = 0; j < h; j++) {
        const uint8_t *oa = sp->a + (size_t)j * w;
        int f = 0, l = w - 1;
        while (f < w && !oa[f]) {
            f++;
        }
        while (l >= f && !oa[l]) {
            l--;
        }
        sp->span[2 * j] = (int16_t)f;
        sp->span[2 * j + 1] = (int16_t)(l + 1);
    }
    sp->x = (int16_t)x;
    sp->y = (int16_t)y;
    sp->w = (int16_t)w;
    sp->h = (int16_t)h;
    sp->ok = true;
    return true;
}
// the copy of a sprite, moved by (dx, dy) from the place where it was made, only in the columns [cx0, cx1)
static void sprite_put_x(const sprite_t *sp, int dx, int dy, int cx0, int cx1);
static void sprite_put(const sprite_t *sp, int dx, int dy)
{
    sprite_put_x(sp, dx, dy, -100000, 100000);
}
static void sprite_put_x(const sprite_t *sp, int dx, int dy, int cx0, int cx1)
{
    if (!sp->ok) {
        return;
    }
    for (int j = 0; j < sp->h; j++) {
        int y = sp->y + j + dy;
        if ((unsigned)y >= (unsigned)SH) {
            continue;
        }
        int x0 = sp->x + dx, xa = x0 + sp->span[2 * j], xb = x0 + sp->span[2 * j + 1];
        if (xa < s_x0[y]) {
            xa = s_x0[y];
        }
        if (xb > s_x1[y]) {
            xb = s_x1[y];
        }
        if (xa < cx0) {
            xa = cx0;
        }
        if (xb > cx1) {
            xb = cx1;
        }
        const uint16_t *c = sp->c + (size_t)j * sp->w - x0;
        const uint8_t *a = sp->a + (size_t)j * sp->w - x0;
        uint16_t *p = &g_fb[y * SW];
        for (int x = xa; x < xb; x++) {
            int al = a[x];
            if (!al) {
                continue;
            }
            if (al >= 250) {
                int e = x + 1;
                while (e < xb && a[e] >= 250) {
                    e++;
                }
                memcpy(p + x, c + x, (size_t)(e - x) * 2);
                x = e - 1;
                continue;
            }
            p[x] = mixc(p[x], c[x], al + 1);
        }
    }
}
