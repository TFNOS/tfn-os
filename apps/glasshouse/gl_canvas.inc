// GLASSHOUSE: the canvas. The painters of the art draw with these functions into a target: a layer of the world (2
// texels for each world px), the sky (1 texel for each world px) or the picture of a part (a sprite with an alpha
// plane). The points are WORLD px (x to the right, y down); the target transform is target = (world - o) * s. The
// edges are smooth (4 rows of samples in each texel row). The turns use the table sine of gfx (fsin): sinf and cosf of
// the C library take about 12 us a call in an app file on the board. Included by app_glass.c.

// ---------------------------------------------------------------- the colours (the byte order of the frame: rgb())
#define KEY 0x0118u  // a texel of a world layer that shows the sky (the parallax layer)
static inline uint16_t C3(int r, int g, int b)
{
    return tfd_rgb(r, g, b);
}
static inline uint16_t HX(uint32_t v)
{
    return tfd_rgb((int)(v >> 16) & 255, (int)(v >> 8) & 255, (int)v & 255);
}
static inline uint16_t cmix(uint16_t a, uint16_t b, float t)
{
    return tfd_mix(a, b, (int)(t * 256));
}
static inline uint16_t cmul(uint16_t c, float k)
{
    int r, g, b;
    tfd_unrgb(c, &r, &g, &b);
    return tfd_rgb((int)(r * k), (int)(g * k), (int)(b * k));
}

// ---------------------------------------------------------------- small math and noise
#define PI_  3.14159265f
#define TAU_ 6.2831853f
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
static uint32_t s_rng = 1;
static inline uint32_t rnd_u(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static inline float rnd(void)
{
    return (float)(rnd_u() >> 8) / 16777216.0f;
}
static inline float hash1(int n)
{
    uint32_t h = (uint32_t)n * 0x9E3779B1u;
    h ^= h >> 15;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    h *= 0xC2B2AE3Du;
    h ^= h >> 16;
    return (float)(h >> 8) / 16777216.0f;
}
static float noise1(float x)
{
    int i = (int)floorf(x);
    float f = x - (float)i, u = f * f * (3 - 2 * f);
    return lerpf(hash1(i) * 2 - 1, hash1(i + 1) * 2 - 1, u);
}
static float fbm1(float x, int oct)
{
    float s = 0, a = 0.5f;
    for (int i = 0; i < oct; i++) {
        s += a * noise1(x);
        x *= 2.03f;
        a *= 0.5f;
    }
    return s;
}

// ---------------------------------------------------------------- the target
typedef struct {
    uint16_t *c;  // the colours
    uint8_t *a;   // the alpha plane (a sprite), or NULL (an opaque layer)
    int w, h;
    float s, ox, oy;      // target = (world - o) * s
    const uint16_t *sky;  // a world layer: the sky under a KEY texel (1x, world px), or NULL
} cv_t;
static cv_t CV;
static int cv_cx0, cv_cy0, cv_cx1 = 1 << 20, cv_cy1 = 1 << 20;  // the clip box (target px)
static void cv_set(uint16_t *c, uint8_t *a, int w, int h, float s, float ox, float oy, const uint16_t *sky)
{
    CV.c = c;
    CV.a = a;
    CV.w = w;
    CV.h = h;
    CV.s = s;
    CV.ox = ox;
    CV.oy = oy;
    CV.sky = sky;
    cv_cx0 = cv_cy0 = 0;
    cv_cx1 = w;
    cv_cy1 = h;
}
static void cv_clip(int x0, int y0, int x1, int y1)
{
    cv_cx0 = clampi(x0, 0, CV.w);
    cv_cy0 = clampi(y0, 0, CV.h);
    cv_cx1 = clampi(x1, 0, CV.w);
    cv_cy1 = clampi(y1, 0, CV.h);
}
static void cv_noclip(void)
{
    cv_cx0 = cv_cy0 = 0;
    cv_cx1 = CV.w;
    cv_cy1 = CV.h;
}
#define TXX(x) (((x) - CV.ox) * CV.s)
#define TXY(y) (((y) - CV.oy) * CV.s)

// the sky colour under a KEY texel of a world layer (the zoom 1 place of the sky)
static inline uint16_t cv_sky_at(int x, int y)
{
    int sx = clampi((int)((x / CV.s) + CV.ox), 0, 639), sy = clampi((int)((y / CV.s) + CV.oy), 0, 171);
    return CV.sky ? CV.sky[sy * 640 + sx] : 0;
}
// one texel: a = 0..256
static inline void cv_put(int x, int y, uint16_t col, int a)
{
    if (a <= 0 || x < cv_cx0 || y < cv_cy0 || x >= cv_cx1 || y >= cv_cy1) {
        return;
    }
    int i = y * CV.w + x;
    if (CV.a) {
        int ad = CV.a[i];
        if (a >= 256 || ad == 0) {
            CV.c[i] = col;
            int na = a >= 256 ? 255 : (a * 255) >> 8;
            CV.a[i] = (uint8_t)(na > ad ? na : ad + ((na * (255 - ad)) >> 8));
            return;
        }
        // over: ao = as + ad (1 - as); the colour moves to the source by as / ao
        int as = (a * 255) >> 8, ao = as + ((ad * (255 - as)) / 255);
        if (ao <= 0) {
            return;
        }
        CV.c[i] = tfd_mix(CV.c[i], col, (as * 256) / ao);
        CV.a[i] = (uint8_t)(ao > 255 ? 255 : ao);
        return;
    }
    uint16_t d = CV.c[i];
    if (!CV.sky) {
        CV.c[i] = a >= 256 ? col : tfd_mix(d, col, a);
        return;
    }
    if (d == KEY) {
        d = cv_sky_at(x, y);
    }
    uint16_t r = a >= 256 ? col : tfd_mix(d, col, a);
    CV.c[i] = r == KEY ? (uint16_t)(KEY ^ 0x0100u) : r;
}
// light that adds (a lamp glow): no alpha change on a sprite
static inline void cv_add(int x, int y, uint16_t col, int a)
{
    if (a <= 0 || x < cv_cx0 || y < cv_cy0 || x >= cv_cx1 || y >= cv_cy1) {
        return;
    }
    int i = y * CV.w + x;
    if (CV.a && CV.a[i] == 0) {
        return;
    }
    uint16_t d = CV.c[i];
    if (!CV.a && CV.sky && d == KEY) {
        d = cv_sky_at(x, y);
    }
    uint16_t r = tfd_add(d, col, a);
    CV.c[i] = (CV.sky && r == KEY) ? (uint16_t)(KEY ^ 0x0100u) : r;
}

// ---------------------------------------------------------------- the shapes
static float s_cov[1400];
// a filled polygon (world points as x, y pairs; even-odd rule), smooth edges
static void cv_poly(const float *wp, int n, uint16_t col, int a)
{
    if (n < 3 || n > 64 || a <= 0) {
        return;
    }
    float p[128];
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        p[2 * i] = TXX(wp[2 * i]);
        p[2 * i + 1] = TXY(wp[2 * i + 1]);
        x0 = fminf(x0, p[2 * i]);
        x1 = fmaxf(x1, p[2 * i]);
        y0 = fminf(y0, p[2 * i + 1]);
        y1 = fmaxf(y1, p[2 * i + 1]);
    }
    int xa = clampi((int)floorf(x0), cv_cx0, cv_cx1 - 1), xb = clampi((int)ceilf(x1), cv_cx0, cv_cx1 - 1);
    int ya = clampi((int)floorf(y0), cv_cy0, cv_cy1 - 1), yb = clampi((int)ceilf(y1), cv_cy0, cv_cy1 - 1);
    int w = xb - xa + 1;
    if (w <= 0 || w > 1380 || x1 < cv_cx0 || x0 > cv_cx1 || y1 < cv_cy0 || y0 > cv_cy1) {
        return;
    }
    // (each row: the spans of its 4 sample rows first; then only the texels from the first span to the last one are
    // cleared, added and written: a thin slanted line does not cost the whole width of its box in each row)
    for (int y = ya; y <= yb; y++) {
        float sl[4][32], sr[4][32];
        int ns[4], lo = w, hi = -1;
        for (int s = 0; s < 4; s++) {
            float sy = (float)y + 0.125f + (float)s * 0.25f, xs[64];
            int k = 0;
            ns[s] = 0;
            for (int i = 0; i < n && k < 64; i++) {
                float ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * ((i + 1) % n)], by = p[2 * ((i + 1) % n) + 1];
                if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                    xs[k++] = ax + (sy - ay) * (bx - ax) / (by - ay);
                }
            }
            for (int i = 1; i < k; i++) {
                for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                    float t = xs[j];
                    xs[j] = xs[j - 1];
                    xs[j - 1] = t;
                }
            }
            for (int j = 0; j + 1 < k && ns[s] < 32; j += 2) {
                float l = fmaxf(0, xs[j] - (float)xa), r = fminf((float)w, xs[j + 1] - (float)xa);
                if (r <= l) {
                    continue;
                }
                sl[s][ns[s]] = l;
                sr[s][ns[s]] = r;
                ns[s]++;
                if ((int)l < lo) {
                    lo = (int)l;
                }
                if ((int)r > hi) {
                    hi = (int)r;
                }
            }
        }
        if (hi < 0) {
            continue;
        }
        if (hi > w - 1) {
            hi = w - 1;
        }
        memset(s_cov + lo, 0, sizeof(float) * (size_t)(hi - lo + 2));
        for (int s = 0; s < 4; s++) {
            for (int q = 0; q < ns[s]; q++) {
                float l = sl[s][q], r = sr[s][q];
                int il = (int)l, ir = (int)r;
                if (il == ir) {
                    s_cov[il] += (r - l) * 0.25f;
                    continue;
                }
                s_cov[il] += ((float)il + 1 - l) * 0.25f;
                for (int i = il + 1; i < ir; i++) {
                    s_cov[i] += 0.25f;
                }
                if (ir < w) {
                    s_cov[ir] += (r - (float)ir) * 0.25f;
                }
            }
        }
        for (int i = lo; i <= hi; i++) {
            float c = s_cov[i];
            if (c > 0.004f) {
                cv_put(xa + i, y, col, c >= 0.996f ? a : (int)((float)a * c));
            }
        }
    }
}
static void cv_rect(float x, float y, float w, float h, uint16_t col, int a)
{
    float p[8] = {x, y, x + w, y, x + w, y + h, x, y + h};
    cv_poly(p, 4, col, a);
}
// a line of width lw (butt ends)
static void cv_line(float x0, float y0, float x1, float y1, float lw, uint16_t col, int a)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-4f) {
        return;
    }
    float nx = -dy / l * lw * 0.5f, ny = dx / l * lw * 0.5f;
    float p[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    cv_poly(p, 4, col, a);
}
// an ellipse (rot in rad)
static void cv_ell(float cx, float cy, float rx, float ry, float rot, uint16_t col, int a)
{
    if (rx <= 0.01f || ry <= 0.01f) {
        return;
    }
    int n = clampi((int)((rx + ry) * CV.s * 0.9f) + 8, 10, 64);
    float p[128], c = fcos(rot), s = fsin(rot);
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)n * TAU_, ex = fcos(t) * rx, ey = fsin(t) * ry;
        p[2 * i] = cx + ex * c - ey * s;
        p[2 * i + 1] = cy + ex * s + ey * c;
    }
    cv_poly(p, n, col, a);
}
static inline void cv_disc(float cx, float cy, float r, uint16_t col, int a)
{
    cv_ell(cx, cy, r, r, 0, col, a);
}
// an arc of width lw from the angle a0 to a1 (the screen angles: 0 = right, + = down)
static void cv_arc(float cx, float cy, float r, float lw, float a0, float a1, uint16_t col, int a)
{
    int n = clampi((int)(fabsf(a1 - a0) * r * CV.s * 0.35f) + 4, 4, 30);
    float p[128];
    for (int i = 0; i <= n; i++) {
        float t = a0 + (a1 - a0) * (float)i / (float)n;
        p[2 * i] = cx + fcos(t) * (r + lw * 0.5f);
        p[2 * i + 1] = cy + fsin(t) * (r + lw * 0.5f);
        p[2 * (2 * n + 1 - i)] = cx + fcos(t) * (r - lw * 0.5f);
        p[2 * (2 * n + 1 - i) + 1] = cy + fsin(t) * (r - lw * 0.5f);
    }
    cv_poly(p, 2 * n + 2, col, a);
}
static inline void cv_ring(float cx, float cy, float r, float lw, uint16_t col, int a)
{
    cv_arc(cx, cy, r, lw, 0, TAU_ * 0.999f, col, a);
}
// a soft ellipse with no smooth edge, for big soft shapes (the clouds): a span of each row, written at once (a layer
// with no alpha only; a picture with alpha gets cv_ell)
static void cv_ell_soft(float cx, float cy, float rx, float ry, uint16_t col, int a)
{
    if (CV.a) {
        cv_ell(cx, cy, rx, ry, 0, col, a);
        return;
    }
    if (rx <= 0.5f || ry <= 0.5f || a <= 0) {
        return;
    }
    float tcx = TXX(cx), tcy = TXY(cy), trx = rx * CV.s, try_ = ry * CV.s;
    int ya = clampi((int)(tcy - try_), cv_cy0, cv_cy1), yb = clampi((int)(tcy + try_) + 1, cv_cy0, cv_cy1);
    for (int y = ya; y < yb; y++) {
        float dy = ((float)y + 0.5f - tcy) / try_;
        if (dy * dy >= 1) {
            continue;
        }
        float hw = trx * sqrtf(1 - dy * dy);
        int xa = clampi((int)(tcx - hw + 0.5f), cv_cx0, cv_cx1), xb = clampi((int)(tcx + hw + 0.5f), cv_cx0, cv_cx1);
        uint16_t *d = CV.c + y * CV.w;
        for (int x = xa; x < xb; x++) {
            uint16_t o = d[x];
            if (CV.sky && o == KEY) {
                o = cv_sky_at(x, y);
            }
            uint16_t r = tfd_mix(o, col, a);
            d[x] = (CV.sky && r == KEY) ? (uint16_t)(KEY ^ 0x0100u) : r;
        }
    }
}
// a stroke along a quadratic curve (more segments for a longer curve)
static void cv_quad(float x0, float y0, float qx, float qy, float x1, float y1, float lw, uint16_t col, int a)
{
    float px = x0, py = y0;
    float len = (fabsf(qx - x0) + fabsf(qy - y0) + fabsf(x1 - qx) + fabsf(y1 - qy)) * CV.s;
    int nseg = clampi((int)(len / 6), 3, 10);
    for (int i = 1; i <= nseg; i++) {
        float t = (float)i / nseg, u = 1 - t;
        float x = u * u * x0 + 2 * u * t * qx + t * t * x1, y = u * u * y0 + 2 * u * t * qy + t * t * y1;
        cv_line(px, py, x, y, lw, col, a);
        px = x;
        py = y;
    }
}
// a soft radial glow (source over): the alpha a in the middle, 0.35 a at 0.45 r, 0 at r (the glow() of the look page)
static void cv_glow(float cx, float cy, float r, uint16_t col, int a, bool add)
{
    float tr = r * CV.s, tx = TXX(cx), ty = TXY(cy);
    int y0 = (int)(ty - tr), y1 = (int)(ty + tr) + 1, x0 = (int)(tx - tr), x1 = (int)(tx + tr) + 1;
    for (int y = y0; y <= y1; y++) {
        if (y < cv_cy0 || y >= cv_cy1) {
            continue;
        }
        float dy = ((float)y + 0.5f - ty) / tr;
        for (int x = x0; x <= x1; x++) {
            if (x < cv_cx0 || x >= cv_cx1) {
                continue;
            }
            float dx = ((float)x + 0.5f - tx) / tr, d = sqrtf(dx * dx + dy * dy);
            if (d >= 1) {
                continue;
            }
            float k = d < 0.45f ? 1 - d / 0.45f * 0.65f : 0.35f * (1 - (d - 0.45f) / 0.55f);
            int aa = (int)((float)a * k);
            if (add) {
                cv_add(x, y, col, aa);
            } else {
                cv_put(x, y, col, aa);
            }
        }
    }
}
// many tiny grains (moss, gravel, the grain of the wood): n random dots in a box
static void cv_grain(float x, float y, float w, float h, int n, uint16_t c0, uint16_t c1, int a, float sz)
{
    for (int i = 0; i < n; i++) {
        float px = x + rnd() * w, py = y + rnd() * h;
        cv_rect(px, py, sz * (0.6f + rnd()), sz * 0.6f, rnd() < 0.5f ? c0 : c1, a);
    }
}
