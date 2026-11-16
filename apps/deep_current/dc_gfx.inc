// DEEP CURRENT: the drawing (included by app_deep.c). Small, fast shapes that write g_fb in a clip box: light that
// adds (glows, the clear bodies, the trails) and alpha mixes, with a 4 x 4 ordered dither, so that faint light and soft
// gradients show on the 16-bit panel and do not make bands. All the shapes stop at the clip box and at the frame edges
// (gfx_w x gfx_h), so a wrong point never writes out of the frame.

static int c_x0, c_y0, c_x1, c_y1;  // the clip box (x1 and y1 are not drawn)
static int s_fw = SW, s_fh = SH;    // the frame now
static void clip_full(void)
{
    s_fw = gfx_w;
    s_fh = gfx_h;
    c_x0 = 0;
    c_y0 = 0;
    c_x1 = s_fw;
    c_y1 = s_fh;
}
static void clip_box(int x0, int y0, int x1, int y1)
{
    c_x0 = x0 < 0 ? 0 : x0;
    c_y0 = y0 < 0 ? 0 : y0;
    c_x1 = x1 > s_fw ? s_fw : x1;
    c_y1 = y1 > s_fh ? s_fh : y1;
}

static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static inline uint16_t swp(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
static inline int clamp255(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}
// a frame colour from 8-bit values, with the dither value d (0..15) of the pixel
static inline uint16_t pack_d(int r, int g, int b, int d)
{
    r = clamp255(r + (d >> 1));
    g = clamp255(g + (d >> 2));
    b = clamp255(b + (d >> 1));
    return swp((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)));
}
static inline void unpack8(uint16_t p, int *r, int *g, int *b)
{
    uint16_t v = swp(p);
    *r = (v >> 11) << 3;
    *g = ((v >> 5) & 63) << 2;
    *b = (v & 31) << 3;
}
// light that adds (r, g, b are the colour times the alpha, 0..255 each; more is clamped)
static inline void add_at(uint16_t *p, int x, int y, int r, int g, int b)
{
    int d = BAYER4[y & 3][x & 3];
    uint16_t v = swp(*p);
    int r5 = (v >> 11) + ((r + (d >> 1)) >> 3);
    int g6 = ((v >> 5) & 63) + ((g + (d >> 2)) >> 2);
    int b5 = (v & 31) + ((b + (d >> 1)) >> 3);
    if (r5 > 31) {
        r5 = 31;
    }
    if (g6 > 63) {
        g6 = 63;
    }
    if (b5 > 31) {
        b5 = 31;
    }
    *p = swp((uint16_t)((r5 << 11) | (g6 << 5) | b5));
}
// an alpha mix: a = 0..256
static inline void mix_at(uint16_t *p, int x, int y, int r, int g, int b, int a)
{
    int dr, dg, db;
    unpack8(*p, &dr, &dg, &db);
    dr += ((r - dr) * a) >> 8;
    dg += ((g - dg) * a) >> 8;
    db += ((b - db) * a) >> 8;
    *p = pack_d(dr, dg, db, BAYER4[y & 3][x & 3]);
}
static inline bool in_clip(int x, int y)
{
    return x >= c_x0 && x < c_x1 && y >= c_y0 && y < c_y1;
}
static inline void px_add(int x, int y, int r, int g, int b)
{
    if (in_clip(x, y)) {
        add_at(g_fb + y * s_fw + x, x, y, r, g, b);
    }
}
static inline void px_mix(int x, int y, int r, int g, int b, int a)
{
    if (a > 0 && in_clip(x, y)) {
        mix_at(g_fb + y * s_fw + x, x, y, r, g, b, a > 256 ? 256 : a);
    }
}
// a point of light at a float place: the light goes to the 4 pixels round it (no hard steps when it moves slowly)
static void dot_add(float fx, float fy, int r, int g, int b)
{
    fx -= 0.5f;
    fy -= 0.5f;
    int x = (int)floorf(fx), y = (int)floorf(fy);
    float u = fx - x, v = fy - y;
    int w00 = (int)((1 - u) * (1 - v) * 256), w10 = (int)(u * (1 - v) * 256), w01 = (int)((1 - u) * v * 256),
        w11 = 256 - w00 - w10 - w01;
    px_add(x, y, r * w00 >> 8, g * w00 >> 8, b * w00 >> 8);
    px_add(x + 1, y, r * w10 >> 8, g * w10 >> 8, b * w10 >> 8);
    px_add(x, y + 1, r * w01 >> 8, g * w01 >> 8, b * w01 >> 8);
    px_add(x + 1, y + 1, r * w11 >> 8, g * w11 >> 8, b * w11 >> 8);
}
static void dot_mix(float fx, float fy, int r, int g, int b, int a)
{
    fx -= 0.5f;
    fy -= 0.5f;
    int x = (int)floorf(fx), y = (int)floorf(fy);
    float u = fx - x, v = fy - y;
    px_mix(x, y, r, g, b, (int)((1 - u) * (1 - v) * a));
    px_mix(x + 1, y, r, g, b, (int)(u * (1 - v) * a));
    px_mix(x, y + 1, r, g, b, (int)((1 - u) * v * a));
    px_mix(x + 1, y + 1, r, g, b, (int)(u * v * a));
}

// ---------------------------------------------------------------- spans and boxes
static void span_fill(int y, int x0, int x1, uint16_t c)
{
    if (y < c_y0 || y >= c_y1) {
        return;
    }
    if (x0 < c_x0) {
        x0 = c_x0;
    }
    if (x1 > c_x1) {
        x1 = c_x1;
    }
    uint16_t *p = g_fb + y * s_fw;
    for (int x = x0; x < x1; x++) {
        p[x] = c;
    }
}
static void span_mix(int y, int x0, int x1, int r, int g, int b, int a)
{
    if (y < c_y0 || y >= c_y1 || a <= 0) {
        return;
    }
    if (x0 < c_x0) {
        x0 = c_x0;
    }
    if (x1 > c_x1) {
        x1 = c_x1;
    }
    uint16_t *p = g_fb + y * s_fw;
    for (int x = x0; x < x1; x++) {
        mix_at(p + x, x, y, r, g, b, a);
    }
}
static void span_add(int y, int x0, int x1, int r, int g, int b)
{
    if (y < c_y0 || y >= c_y1) {
        return;
    }
    if (x0 < c_x0) {
        x0 = c_x0;
    }
    if (x1 > c_x1) {
        x1 = c_x1;
    }
    uint16_t *p = g_fb + y * s_fw;
    for (int x = x0; x < x1; x++) {
        add_at(p + x, x, y, r, g, b);
    }
}
__attribute__((unused)) static void box_fill(int x, int y, int w, int h, uint16_t c)
{
    for (int j = y; j < y + h; j++) {
        span_fill(j, x, x + w, c);
    }
}
static void box_mix(int x, int y, int w, int h, int r, int g, int b, int a)
{
    for (int j = y; j < y + h; j++) {
        span_mix(j, x, x + w, r, g, b, a);
    }
}
// a box with round corners (r) and a smooth edge, mixed with alpha a
static void rbox_mix(int x, int y, int w, int h, int r, int cr, int cg, int cb, int a)
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
            span_mix(y + j, x, x + w, cr, cg, cb, a);
            continue;
        }
        float hw = sqrtf(fmaxf(0, r * r - dy * dy));
        int in = (int)ceilf(r - hw);
        span_mix(y + j, x + in + 1, x + w - in - 1, cr, cg, cb, a);
        for (int k = 0; k <= in && k < w / 2 + 1; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int aa = cov >= 1 ? a : (int)(a * cov);
            px_mix(x + k, y + j, cr, cg, cb, aa);
            px_mix(x + w - 1 - k, y + j, cr, cg, cb, aa);
        }
    }
}
// the edge of a box with round corners: a line of 1 px (light that adds)
static void rbox_line_add(int x, int y, int w, int h, int r, int cr, int cg, int cb)
{
    for (int i = x + r; i < x + w - r; i++) {
        px_add(i, y, cr, cg, cb);
        px_add(i, y + h - 1, cr, cg, cb);
    }
    for (int j = y + r; j < y + h - r; j++) {
        px_add(x, j, cr, cg, cb);
        px_add(x + w - 1, j, cr, cg, cb);
    }
    int n = r * 3 + 2;
    for (int k = 0; k <= n; k++) {
        float an = (float)k / n * 1.5707963f, ox = (r - 0.5f) * (1 - cosf(an)), oy = (r - 0.5f) * (1 - sinf(an));
        dot_add(x + ox + 0.5f, y + oy + 0.5f, cr, cg, cb);
        dot_add(x + w - ox - 0.5f, y + oy + 0.5f, cr, cg, cb);
        dot_add(x + ox + 0.5f, y + h - oy - 0.5f, cr, cg, cb);
        dot_add(x + w - ox - 0.5f, y + h - oy - 0.5f, cr, cg, cb);
    }
}

// ---------------------------------------------------------------- the turned ellipse with a colour table
// lut[i] = the colour at q = (u / rx)^2 + (v / ry)^2 in [i / LUT_N, (i + 1) / LUT_N): ADD = the colour times the
// alpha, MIX = the colour and the alpha (0..256). u is along the axis (ax, ay), v across it.
#define LUT_N 32
typedef struct {
    int16_t r, g, b, a;
} lut_t;
static void ell_lut(float cx, float cy, float ax, float ay, float rx, float ry, const lut_t *lut, bool add)
{
    if (rx < 0.35f || ry < 0.35f) {
        return;
    }
    float irx = 1.0f / (rx * rx), iry = 1.0f / (ry * ry);
    float A = ax * ax * irx + ay * ay * iry, B0 = 2 * ax * ay * (irx - iry), C0 = ay * ay * irx + ax * ax * iry;
    float R = rx > ry ? rx : ry;
    int y0 = (int)floorf(cy - R - 1), y1 = (int)ceilf(cy + R + 1);
    if (y0 < c_y0) {
        y0 = c_y0;
    }
    if (y1 > c_y1 - 1) {
        y1 = c_y1 - 1;
    }
    if (cx + R < c_x0 - 1 || cx - R > c_x1 + 1) {
        return;
    }
    float i2a = 0.5f / A;
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy;
        float B = B0 * dy, C = C0 * dy * dy;
        float disc = B * B - 4 * A * (C - 1);
        if (disc <= 0) {
            continue;
        }
        float sq = sqrtf(disc);
        float xa = cx + (-B - sq) * i2a, xb = cx + (-B + sq) * i2a;
        int ia = (int)ceilf(xa - 0.5f), ib = (int)floorf(xb - 0.5f);
        if (ia < c_x0) {
            ia = c_x0;
        }
        if (ib > c_x1 - 1) {
            ib = c_x1 - 1;
        }
        if (ia > ib) {
            continue;
        }
        uint16_t *row = g_fb + y * s_fw;
        float dx = ia + 0.5f - cx;
        if (add) {
            for (int x = ia; x <= ib; x++, dx += 1) {
                float q = (A * dx + B) * dx + C;
                int i = (int)(q * LUT_N);
                if (i > LUT_N - 1) {
                    i = LUT_N - 1;
                }
                if (i < 0) {
                    i = 0;
                }
                const lut_t *L = &lut[i];
                if (L->r | L->g | L->b) {
                    add_at(row + x, x, y, L->r, L->g, L->b);
                }
            }
        } else {
            for (int x = ia; x <= ib; x++, dx += 1) {
                float q = (A * dx + B) * dx + C;
                int i = (int)(q * LUT_N);
                if (i > LUT_N - 1) {
                    i = LUT_N - 1;
                }
                if (i < 0) {
                    i = 0;
                }
                const lut_t *L = &lut[i];
                if (L->a > 0) {
                    mix_at(row + x, x, y, L->r, L->g, L->b, L->a);
                }
            }
        }
    }
}
// a table: one colour, a flat alpha, a soft edge of about one pixel (rpx = the small radius in px)
static void lut_flat(lut_t *lut, int r, int g, int b, float alpha, float rpx, bool add)
{
    for (int i = 0; i < LUT_N; i++) {
        float t = sqrtf((i + 0.5f) / LUT_N), e = (1 - t) * rpx + 0.35f;
        float a = alpha * (e > 1 ? 1 : e < 0 ? 0 : e);
        if (add) {
            lut[i].r = (int16_t)(r * a);
            lut[i].g = (int16_t)(g * a);
            lut[i].b = (int16_t)(b * a);
            lut[i].a = 0;
        } else {
            lut[i].r = (int16_t)r;
            lut[i].g = (int16_t)g;
            lut[i].b = (int16_t)b;
            lut[i].a = (int16_t)(a * 256);
        }
    }
}
// a table of three stops (t = the radius part 0..1: the middle, t1, the edge), each a colour and an alpha (light
// that adds), with a soft edge
static void lut_3(lut_t *lut, const float c0[3], float a0, float t1, const float c1[3], float a1, const float c2[3],
                  float a2, float rpx)
{
    for (int i = 0; i < LUT_N; i++) {
        float t = sqrtf((i + 0.5f) / LUT_N), c[3], a;
        if (t < t1) {
            float k = t / t1;
            for (int j = 0; j < 3; j++) {
                c[j] = c0[j] + (c1[j] - c0[j]) * k;
            }
            a = a0 + (a1 - a0) * k;
        } else {
            float k = (t - t1) / (1 - t1);
            for (int j = 0; j < 3; j++) {
                c[j] = c1[j] + (c2[j] - c1[j]) * k;
            }
            a = a1 + (a2 - a1) * k;
        }
        float e = (1 - t) * rpx + 0.35f;
        a *= e > 1 ? 1 : e < 0 ? 0 : e;
        lut[i].r = (int16_t)(c[0] * a);
        lut[i].g = (int16_t)(c[1] * a);
        lut[i].b = (int16_t)(c[2] * a);
        lut[i].a = 0;
    }
}
// a disc of one colour (mix) or of light (add)
static void disc_mix(float cx, float cy, float r, int cr, int cg, int cb, float alpha)
{
    lut_t L[LUT_N];
    lut_flat(L, cr, cg, cb, alpha, r, false);
    ell_lut(cx, cy, 1, 0, r, r, L, false);
}
static void ell_mix(float cx, float cy, float ax, float ay, float rx, float ry, int cr, int cg, int cb, float alpha)
{
    lut_t L[LUT_N];
    lut_flat(L, cr, cg, cb, alpha, rx < ry ? rx : ry, false);
    ell_lut(cx, cy, ax, ay, rx, ry, L, false);
}
static void ell_add(float cx, float cy, float ax, float ay, float rx, float ry, int cr, int cg, int cb, float alpha)
{
    lut_t L[LUT_N];
    lut_flat(L, cr, cg, cb, alpha, rx < ry ? rx : ry, true);
    ell_lut(cx, cy, ax, ay, rx, ry, L, true);
}

// ---------------------------------------------------------------- a glow: light that adds, (1 - d^2 / r^2)^2
static void glow(float cx, float cy, float r, int cr, int cg, int cb, float alpha)
{
    if (alpha <= 0.004f) {
        return;
    }
    if (r < 0.8f) {
        dot_add(cx, cy, (int)(cr * alpha * 0.5f), (int)(cg * alpha * 0.5f), (int)(cb * alpha * 0.5f));
        return;
    }
    int R = (int)(cr * alpha), G = (int)(cg * alpha), B = (int)(cb * alpha);
    float ir2 = 1.0f / (r * r);
    int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    if (y0 < c_y0) {
        y0 = c_y0;
    }
    if (y1 > c_y1 - 1) {
        y1 = c_y1 - 1;
    }
    if (cx + r < c_x0 || cx - r > c_x1) {
        return;
    }
    for (int y = y0; y <= y1; y++) {
        float dy = y + 0.5f - cy, h = 1 - dy * dy * ir2;
        if (h <= 0) {
            continue;
        }
        float hw = r * sqrtf(h);
        int xa = (int)floorf(cx - hw), xb = (int)ceilf(cx + hw);
        if (xa < c_x0) {
            xa = c_x0;
        }
        if (xb > c_x1 - 1) {
            xb = c_x1 - 1;
        }
        uint16_t *row = g_fb + y * s_fw;
        float dx = xa + 0.5f - cx;
        for (int x = xa; x <= xb; x++, dx += 1) {
            float f = h - dx * dx * ir2;
            if (f <= 0) {
                continue;
            }
            f *= f;
            int fr = (int)(R * f), fg = (int)(G * f), fb = (int)(B * f);
            if (fr | fg | fb) {
                add_at(row + x, x, y, fr, fg, fb);
            }
        }
    }
}

// ---------------------------------------------------------------- lines (1 px, smooth)
// light that adds along a line (r, g, b = the colour times the alpha)
static void line_add(float x0, float y0, float x1, float y1, int r, int g, int b)
{
    float dx = x1 - x0, dy = y1 - y0, adx = fabsf(dx), ady = fabsf(dy);
    int n = (int)(adx > ady ? adx : ady) + 1;
    if (n > 800) {
        return;
    }
    float sx = dx / n, sy = dy / n, x = x0, y = y0;
    // (the light of each step goes to the two pixels across the line: no thick spots)
    if (adx >= ady) {
        for (int i = 0; i <= n; i++, x += sx, y += sy) {
            float fy = y - 0.5f;
            int iy = (int)floorf(fy), ix = (int)floorf(x);
            float v = fy - iy;
            int w0 = (int)((1 - v) * 256), w1 = 256 - w0;
            px_add(ix, iy, r * w0 >> 8, g * w0 >> 8, b * w0 >> 8);
            px_add(ix, iy + 1, r * w1 >> 8, g * w1 >> 8, b * w1 >> 8);
        }
    } else {
        for (int i = 0; i <= n; i++, x += sx, y += sy) {
            float fx = x - 0.5f;
            int ix = (int)floorf(fx), iy = (int)floorf(y);
            float u = fx - ix;
            int w0 = (int)((1 - u) * 256), w1 = 256 - w0;
            px_add(ix, iy, r * w0 >> 8, g * w0 >> 8, b * w0 >> 8);
            px_add(ix + 1, iy, r * w1 >> 8, g * w1 >> 8, b * w1 >> 8);
        }
    }
}
static void line_mix(float x0, float y0, float x1, float y1, int r, int g, int b, int a)
{
    float dx = x1 - x0, dy = y1 - y0, adx = fabsf(dx), ady = fabsf(dy);
    int n = (int)(adx > ady ? adx : ady) + 1;
    if (n > 800) {
        return;
    }
    float sx = dx / n, sy = dy / n, x = x0, y = y0;
    if (adx >= ady) {
        for (int i = 0; i <= n; i++, x += sx, y += sy) {
            float fy = y - 0.5f;
            int iy = (int)floorf(fy), ix = (int)floorf(x);
            float v = fy - iy;
            px_mix(ix, iy, r, g, b, (int)((1 - v) * a));
            px_mix(ix, iy + 1, r, g, b, (int)(v * a));
        }
    } else {
        for (int i = 0; i <= n; i++, x += sx, y += sy) {
            float fx = x - 0.5f;
            int ix = (int)floorf(fx), iy = (int)floorf(y);
            float u = fx - ix;
            px_mix(ix, iy, r, g, b, (int)((1 - u) * a));
            px_mix(ix + 1, iy, r, g, b, (int)(u * a));
        }
    }
}
// a thick line (w px) with round ends, mixed
static void tline_mix(float x0, float y0, float x1, float y1, float w, int cr, int cg, int cb, int a)
{
    float rr = w * 0.5f;
    int bx0 = (int)floorf(fminf(x0, x1) - rr - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + rr + 1);
    int by0 = (int)floorf(fminf(y0, y1) - rr - 1), by1 = (int)ceilf(fmaxf(y0, y1) + rr + 1);
    if (bx0 < c_x0) {
        bx0 = c_x0;
    }
    if (by0 < c_y0) {
        by0 = c_y0;
    }
    if (bx1 > c_x1 - 1) {
        bx1 = c_x1 - 1;
    }
    if (by1 > c_y1 - 1) {
        by1 = c_y1 - 1;
    }
    if ((bx1 - bx0) * (by1 - by0) > 40000) {
        return;
    }
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    for (int y = by0; y <= by1; y++) {
        for (int x = bx0; x <= bx1; x++) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0, t = L2 > 0 ? (px * vx + py * vy) / L2 : 0;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float ddx = px - t * vx, ddy = py - t * vy, cov = rr + 0.5f - sqrtf(ddx * ddx + ddy * ddy);
            if (cov <= 0) {
                continue;
            }
            mix_at(g_fb + y * s_fw + x, x, y, cr, cg, cb, cov >= 1 ? a : (int)(a * cov));
        }
    }
}

// ---------------------------------------------------------------- polygons
// a filled polygon (n points as x, y pairs; the even-odd rule) at the middle of each row, with a soft end of each span
#define PMAX 40
static void poly_do(const float *p, int n, int cr, int cg, int cb, int a, bool add)
{
    if (n < 3 || n > PMAX) {
        return;
    }
    float y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        y0 = fminf(y0, p[2 * i + 1]);
        y1 = fmaxf(y1, p[2 * i + 1]);
    }
    int ya = (int)floorf(y0), yb = (int)ceilf(y1);
    if (ya < c_y0) {
        ya = c_y0;
    }
    if (yb > c_y1 - 1) {
        yb = c_y1 - 1;
    }
    int R = cr * a >> 8, G = cg * a >> 8, B = cb * a >> 8;
    for (int y = ya; y <= yb; y++) {
        float sy = y + 0.5f, xs[PMAX];
        int k = 0;
        for (int i = 0; i < n; i++) {
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
        for (int j = 0; j + 1 < k; j += 2) {
            float l = xs[j], r = xs[j + 1];
            if (r <= l) {
                continue;
            }
            int il = (int)floorf(l), ir = (int)floorf(r);
            if (il == ir) {
                float cov = r - l;
                if (add) {
                    px_add(il, y, (int)(R * cov), (int)(G * cov), (int)(B * cov));
                } else {
                    px_mix(il, y, cr, cg, cb, (int)(a * cov));
                }
                continue;
            }
            float cl = il + 1 - l, crr = r - ir;
            if (add) {
                px_add(il, y, (int)(R * cl), (int)(G * cl), (int)(B * cl));
                px_add(ir, y, (int)(R * crr), (int)(G * crr), (int)(B * crr));
                span_add(y, il + 1, ir, R, G, B);
            } else {
                px_mix(il, y, cr, cg, cb, (int)(a * cl));
                px_mix(ir, y, cr, cg, cb, (int)(a * crr));
                span_mix(y, il + 1, ir, cr, cg, cb, a);
            }
        }
    }
}
static void poly_mix(const float *p, int n, int r, int g, int b, int a)
{
    poly_do(p, n, r, g, b, a, false);
}
static void poly_add(const float *p, int n, int r, int g, int b, int a)
{
    poly_do(p, n, r, g, b, a, true);
}
// the outline of a polygon (closed), light that adds
static void poly_line_add(const float *p, int n, int r, int g, int b)
{
    for (int i = 0; i < n; i++) {
        line_add(p[2 * i], p[2 * i + 1], p[2 * ((i + 1) % n)], p[2 * ((i + 1) % n) + 1], r, g, b);
    }
}
// a quadratic curve into points (for the fins and the flukes): from a to b with the control c, k points (no a)
static int quad_pts(float *out, int at, float ax, float ay, float cx, float cy, float bx, float by, int k)
{
    for (int i = 1; i <= k; i++) {
        float t = (float)i / k, u = 1 - t;
        out[2 * at] = u * u * ax + 2 * u * t * cx + t * t * bx;
        out[2 * at + 1] = u * u * ay + 2 * u * t * cy + t * t * by;
        at++;
    }
    return at;
}

// ---------------------------------------------------------------- text (the Rubik faces: English and Hebrew)
// text with a dark ring round it (4 offsets), then the colour
static void text_o(const kfont_t *f, int cx, int base, const char *s, uint16_t col, uint16_t ring)
{
    ui_text_c(f, cx - 1, base, s, ring);
    ui_text_c(f, cx + 1, base, s, ring);
    ui_text_c(f, cx, base - 1, s, ring);
    ui_text_c(f, cx, base + 1, s, ring);
    ui_text_c(f, cx, base, s, col);
}
// text from x (the left end) or to x (the right end)
static void text_l(const kfont_t *f, int x, int base, const char *s, uint16_t col)
{
    ui_text_c(f, x + ui_text_w(f, s) / 2, base, s, col);
}
__attribute__((unused)) static void text_r(const kfont_t *f, int x, int base, const char *s, uint16_t col)
{
    int w = ui_text_w(f, s);
    ui_text_c(f, x - w + w / 2, base, s, col);
}
static void text_lo(const kfont_t *f, int x, int base, const char *s, uint16_t col, uint16_t ring)
{
    text_o(f, x + ui_text_w(f, s) / 2, base, s, col, ring);
}
// a colour of the frame from 8-bit values (no dither)
static inline uint16_t rgbc(int r, int g, int b)
{
    return rgb((uint8_t)clamp255(r), (uint8_t)clamp255(g), (uint8_t)clamp255(b));
}
