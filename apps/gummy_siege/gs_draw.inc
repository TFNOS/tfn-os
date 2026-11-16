// GUMMY SIEGE: the drawing of the world (included by app_gummy.c). The pictures of art.bin (tools/art/bake.py), the
// far picture with a small parallax, the cloth of the table in perspective with a fog, the props behind the play
// line, the bodies of tfphys with the textures of their kinds, the bears, the enemies, the balloons, the sling, the
// particles and the camera. All sizes come from the frame (640 x 172 or 320 x 240, frame_geom).

// ================================================================ the pictures (art.bin)
enum {
    P_FAR0,
    P_FAR1,
    P_FAR2,
    P_P0A,
    P_P0B,
    P_P1A,
    P_P1B,
    P_P2A,
    P_P2B,
    P_BEAR0,
    P_BEAR1,
    P_BEAR2,
    P_BEAR3,
    P_BEAR4,
    P_CHIP,
    P_PRETZEL,
    P_CRACKER,
    P_JAR,
    P_CUP,
    P_CAN,
    P_GIFT0,
    P_GIFT1,
    P_GIFT2,
    P_BLOCK0,
    P_BLOCK1,
    P_BLOCK2,
    P_BLOCK3,
    P_HELM0,
    P_HELM1,
    P_HELM2,
    P_N
};
static const char *const PIC_NAME[P_N] = {
        "far0",    "far1",    "far2",    "p0a",      "p0b",      "p1a",       "p1b",       "p2a",   "p2b",   "bear0",
        "bear1",   "bear2",   "bear3",   "bear4",    "t_chip",   "t_pretzel", "t_cracker", "t_jar", "t_cup", "t_can",
        "t_gift0", "t_gift1", "t_gift2", "t_block0", "t_block1", "t_block2",  "t_block3",  "helm0", "helm1", "helm2",
};
typedef struct pic {
    uint16_t w, h;
    uint16_t *c;    // the colours (the byte order of the frame); NULL for a P8 picture
    uint8_t *a;     // the alpha (NULL: no alpha)
    uint16_t *pal;  // P8 (the far pictures): 256 colours, and the indexes (w x h)
    uint8_t *idx;
    const struct pic *half;  // the picture at half the size (the sprites), or NULL
} pic_t;
static pic_t s_pic[P_N];
static pic_t s_mip[P_N][2];      // the sprites at 1/2 and 1/4 of the size
static uint16_t s_hzc[3];        // the colour of the horizon of each world (the fog of the cloth)
static uint16_t s_pdim[3][256];  // the palettes of the far pictures at half the light (the dim mode)
static bool s_dim;               // the dim mode: the world behind the map and the end card (no extra pass)
static inline uint16_t half565(uint16_t c)
{
    uint16_t v = (uint16_t)((c >> 8) | (c << 8));
    v = (uint16_t)((v >> 1) & 0x7BEF);
    return (uint16_t)((v >> 8) | (v << 8));
}
static void *s_art_mem[P_N * 3];
static int s_nart_mem;

static void *art_keep(size_t n)
{
    void *p = gs_alloc(n);
    if (p && s_nart_mem < (int)(sizeof s_art_mem / sizeof s_art_mem[0])) {
        s_art_mem[s_nart_mem++] = p;
    } else if (p) {
        free(p);
        p = NULL;
    }
    return p;
}
static void art_free(void)
{
    for (int i = 0; i < s_nart_mem; i++) {
        free(s_art_mem[i]);
    }
    s_nart_mem = 0;
    memset(s_pic, 0, sizeof s_pic);
    memset(s_mip, 0, sizeof s_mip);
}
// a picture at half the size (a box of 2 x 2 pixels, the colours weighted by the alpha)
static bool pic_halve(const pic_t *s, pic_t *d)
{
    d->w = (uint16_t)(s->w / 2);
    d->h = (uint16_t)(s->h / 2);
    if (d->w < 2 || d->h < 2) {
        return false;
    }
    size_t n = (size_t)d->w * d->h;
    d->c = (uint16_t *)art_keep(n * 3);
    if (!d->c) {
        return false;
    }
    d->a = (uint8_t *)(d->c + n);
    d->half = NULL;
    for (int y = 0; y < d->h; y++) {
        for (int x = 0; x < d->w; x++) {
            int r = 0, g = 0, b = 0, at = 0;
            for (int k = 0; k < 4; k++) {
                int sx = 2 * x + (k & 1), sy = 2 * y + (k >> 1);
                int i = sy * s->w + sx, a = s->a ? s->a[i] : 255, cr, cg, cb;
                tfd_unrgb(s->c[i], &cr, &cg, &cb);
                r += cr * a;
                g += cg * a;
                b += cb * a;
                at += a;
            }
            int i = y * d->w + x;
            d->a[i] = (uint8_t)(at / 4);
            d->c[i] = at ? tfd_rgb(r / at, g / at, b / at) : 0;
        }
    }
    return true;
}
// the grade of a colour: sat = the part of its colour that stays (0 = grey), val = its light (1 = the same)
static const float FAR_GRADE[3][2] = {{0.55f, 0.60f}, {0.50f, 0.62f}, {0.55f, 0.64f}};
#define PROP_FOG 120  // the fog in the colours of the props (of 256)
static uint16_t grade(uint16_t c, float sat, float val)
{
    int r, g, b;
    tfd_unrgb(c, &r, &g, &b);
    float l = 0.3f * r + 0.59f * g + 0.11f * b;
    return tfd_rgb(clampi((int)((l + (r - l) * sat) * val), 0, 255), clampi((int)((l + (g - l) * sat) * val), 0, 255),
                   clampi((int)((l + (b - l) * sat) * val), 0, 255));
}
// a dark ring of 2 px round the shape of a sprite (the clear pixels near the shape get the ring colour; the soft edge
// of the shape mixes into it)
static void pic_outline(pic_t *p, uint16_t ring)
{
    if (!p->c || !p->a) {
        return;
    }
    int w = p->w, h = p->h;
    uint8_t *na = (uint8_t *)malloc((size_t)w * h);
    if (!na) {
        return;
    }
    memcpy(na, p->a, (size_t)w * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x, a = p->a[i];
            if (a >= 200) {
                continue;
            }
            int m1 = 0, m2 = 0;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    int xx = x + dx, yy = y + dy, d2 = dx * dx + dy * dy;
                    if (xx < 0 || yy < 0 || xx >= w || yy >= h || d2 > 5) {
                        continue;
                    }
                    int q = p->a[yy * w + xx];
                    if (d2 <= 2) {
                        if (q > m1) {
                            m1 = q;
                        }
                    } else if (q > m2) {
                        m2 = q;
                    }
                }
            }
            int ra = m1 >= 128 ? 255 : m2 >= 128 ? 170 : (m1 > m2 ? m1 : m2) * 6 / 5;
            if (ra <= a) {
                continue;
            }
            p->c[i] = a > 0 ? tfd_mix(ring, p->c[i], a * 256 / ra) : ring;
            na[i] = (uint8_t)clampi(ra, 0, 255);
        }
    }
    memcpy(p->a, na, (size_t)w * h);
    free(na);
}
// the pictures of art.bin (in the app file: gs_data.c), each one inflated into PSRAM
static bool art_load(void)
{
    art_free();
    const uint8_t *buf = GS_ART_BIN;
    size_t got = GS_ART_LEN;
    tinfl_decompressor *d = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
    bool ok = d && got > 6 && !memcmp(buf, "GSA1", 4);
    int cnt = ok ? buf[4] | buf[5] << 8 : 0;
    size_t at = 6;
    for (int k = 0; ok && k < cnt; k++) {
        if (at + 28 > got) {
            ok = false;
            break;
        }
        const uint8_t *h = buf + at;
        char nm[17];
        memcpy(nm, h, 16);
        nm[16] = 0;
        int kind = h[16], w = h[18] | h[19] << 8, hh = h[20] | h[21] << 8;
        uint32_t zl = (uint32_t)h[22] | (uint32_t)h[23] << 8 | (uint32_t)h[24] << 16 | (uint32_t)h[25] << 24;
        at += 26;
        if (at + zl > got || w <= 0 || hh <= 0 || w > 2048 || hh > 1024) {
            ok = false;
            break;
        }
        int id = -1;
        for (int i = 0; i < P_N; i++) {
            if (!strcmp(nm, PIC_NAME[i])) {
                id = i;
            }
        }
        if (id >= 0) {
            size_t px = (size_t)w * hh, out = kind == 2 ? 512 + px : px * 2 + (kind ? px : 0);
            uint8_t *dst = (uint8_t *)art_keep(out);
            if (!dst) {
                snprintf(s_err, sizeof s_err, "NO MEMORY (ART)");
                ok = false;
                break;
            }
            tinfl_init(d);
            size_t in = zl, outn = out;
            tinfl_status st = tinfl_decompress(d, buf + at, &in, dst, dst, &outn,
                                               TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            if (st != TINFL_STATUS_DONE || outn != out) {
                snprintf(s_err, sizeof s_err, "ART: BAD DATA (%s)", nm);
                ok = false;
                break;
            }
            pic_t *p = &s_pic[id];
            p->w = (uint16_t)w;
            p->h = (uint16_t)hh;
            if (kind == 2) {
                p->pal = (uint16_t *)dst;
                p->idx = dst + 512;
            } else {
                p->c = (uint16_t *)dst;
                p->a = kind ? dst + px * 2 : NULL;
            }
        }
        at += zl;
    }
    free(d);
    if (!ok) {
        if (!s_err[0]) {
            snprintf(s_err, sizeof s_err, "ART: BAD FILE");
        }
        art_free();
        return false;
    }
    for (int i = 0; i < P_N; i++) {
        if (!s_pic[i].c && !s_pic[i].idx) {
            snprintf(s_err, sizeof s_err, "ART: NO %s", PIC_NAME[i]);
            art_free();
            return false;
        }
    }
    // the far pictures: less colour and less light (so the background is not as loud as the fort);
    // the colour of the horizon: the mean of the row over the horizon line (row 175); the dim palettes
    for (int w = 0; w < 3; w++) {
        const pic_t *p = &s_pic[P_FAR0 + w];
        if (p->pal) {
            for (int i = 0; i < 256; i++) {
                p->pal[i] = grade(p->pal[i], FAR_GRADE[w][0], FAR_GRADE[w][1]);
            }
        }
        int r = 0, g = 0, b = 0;
        for (int x = 0; x < p->w; x += 4) {
            int cr, cg, cb;
            tfd_unrgb(p->idx ? p->pal[p->idx[175 * p->w + x]] : p->c[175 * p->w + x], &cr, &cg, &cb);
            r += cr;
            g += cg;
            b += cb;
        }
        if (p->pal) {
            for (int i = 0; i < 256; i++) {
                s_pdim[w][i] = half565(p->pal[i]);
            }
        }
        int m = p->w / 4;
        s_hzc[w] = tfd_rgb(r / m, g / m, b / m);
    }
    // the props behind the play line: the fog of their world goes into their colours here (no mix in each frame)
    for (int i = P_P0A; i <= P_P2B; i++) {
        pic_t *p = &s_pic[i];
        uint16_t fog = s_hzc[(i - P_P0A) / 2];
        if (!p->c) {
            continue;
        }
        for (int k = 0; k < p->w * p->h; k++) {
            p->c[k] = tfd_mix(grade(p->c[k], 0.6f, 0.8f), fog, PROP_FOG);
        }
    }
    // the bears: a dark ring round each one (it stays clear on any background)
    for (int i = P_BEAR0; i <= P_BEAR4; i++) {
        pic_outline(&s_pic[i], tfd_rgb(46, 12, 30));
    }
    // the small copies of the sprites (a zoom out stays smooth)
    for (int i = P_P0A; i < P_N; i++) {
        if (!s_pic[i].a) {
            continue;
        }
        if (pic_halve(&s_pic[i], &s_mip[i][0])) {
            s_pic[i].half = &s_mip[i][0];
            if (pic_halve(&s_mip[i][0], &s_mip[i][1])) {
                s_mip[i][0].half = &s_mip[i][1];
            }
        }
    }
    return true;
}

// ================================================================ the camera of the frame
static float s_shx, s_shy;  // the shake of this frame (px)
static int s_world;         // the world that the frame shows (0..2)
static inline float wsx(float x)
{
    return cam_sx(x) + s_shx;
}
static inline float wsy(float y)
{
    return cam_sy(y) + s_shy;
}

// ================================================================ the primitives
// a span of one colour (32-bit stores in the middle)
static inline void fill16(uint16_t *d, int x0, int x1, uint16_t c)
{
    if (x0 >= x1) {
        return;
    }
    if (x0 & 1) {
        d[x0++] = c;
    }
    uint32_t cc = c | (uint32_t)c << 16, *q = (uint32_t *)(void *)(d + x0);
    int n = (x1 - x0) >> 1;
    for (int i = 0; i < n; i++) {
        q[i] = cc;
    }
    if ((x1 - x0) & 1) {
        d[x1 - 1] = c;
    }
}
// the rows y0 .. y1 - 1 of the frame at half the light (two pixels in one 32-bit word: no multiply)
static void fb_half(int y0, int y1)
{
    y0 = clampi(y0, 0, SH);
    y1 = clampi(y1, 0, SH);
    uint32_t *p = (uint32_t *)(void *)(g_fb + y0 * SW);
    int n = (y1 - y0) * SW / 2;
    for (int i = 0; i < n; i++) {
        uint32_t x = p[i];
        x = ((x & 0x00FF00FFu) << 8) | ((x >> 8) & 0x00FF00FFu);  // (the byte order of the frame -> RGB565)
        x = (x >> 1) & 0x7BEF7BEFu;
        p[i] = ((x & 0x00FF00FFu) << 8) | ((x >> 8) & 0x00FF00FFu);
    }
}
// a sprite: the picture p, its point (ax, ay) (px of the picture) at the screen point (cx, cy), the scales sx and sy,
// the turn (c, s = the cosine and the sine of the angle, with the clock on the screen), the alpha (0..256), a tint (the
// colour mixes in by ta / 256)
static void spr_cs(const pic_t *p, float cx, float cy, float sx, float sy, float c, float s, float ax, float ay,
                   int alpha, uint16_t tint, int ta)
{
    if (!p || !p->c || sx <= 0.01f || sy <= 0.01f || alpha <= 0) {
        return;
    }
    if (s_dim) {
        tint = 0;
        ta = ta > 128 ? ta : 128;
    }
    // (a small copy for a small sprite: nearest samples of a 2x picture make a rough edge)
    while (p->half && sx < 0.55f && sy < 0.75f) {
        p = p->half;
        sx *= 2;
        sy *= 2;
        ax *= 0.5f;
        ay *= 0.5f;
    }
    float x0 = -ax * sx, x1 = (p->w - ax) * sx, y0 = -ay * sy, y1 = (p->h - ay) * sy;
    float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f;
    for (int k = 0; k < 4; k++) {
        float lx = (k & 1) ? x1 : x0, ly = (k & 2) ? y1 : y0;
        float X = cx + c * lx - s * ly, Y = cy + s * lx + c * ly;
        bx0 = minf(bx0, X);
        bx1 = maxf(bx1, X);
        by0 = minf(by0, Y);
        by1 = maxf(by1, Y);
    }
    int xa = clampi((int)floorf(bx0), 0, SW), xb = clampi((int)ceilf(bx1), 0, SW);
    int ya = clampi((int)floorf(by0), 0, SH), yb = clampi((int)ceilf(by1), 0, SH);
    if (xa >= xb || ya >= yb) {
        return;
    }
    float isx = 1.0f / sx, isy = 1.0f / sy;
    // the picture point of a screen point: u = ax + (c dx + s dy) / sx, v = ay + (-s dx + c dy) / sy (16.16 fixed point)
    int32_t dux = (int32_t)(c * isx * 65536.0f), dvx = (int32_t)(-s * isy * 65536.0f);
    int pw = p->w, ph = p->h;
    uint32_t uw = (uint32_t)pw << 16, vh = (uint32_t)ph << 16;
    for (int y = ya; y < yb; y++) {
        float dy = y + 0.5f - cy, dx = xa + 0.5f - cx;
        int32_t u = (int32_t)((ax + (c * dx + s * dy) * isx) * 65536.0f),
                v = (int32_t)((ay + (-s * dx + c * dy) * isy) * 65536.0f);
        uint16_t *d = g_fb + y * SW;
        for (int x = xa; x < xb; x++, u += dux, v += dvx) {
            if ((uint32_t)u >= uw || (uint32_t)v >= vh) {
                continue;  // (a negative value is a big unsigned one)
            }
            int i = (v >> 16) * pw + (u >> 16), a = p->a ? p->a[i] : 255;
            if (a < 8) {
                continue;
            }
            uint16_t col = p->c[i];
            if (ta > 0) {
                col = tfd_mix(col, tint, ta);
            }
            a = (a * alpha) >> 8;
            d[x] = a >= 250 ? col : tfd_mix(d[x], col, a + 1);
        }
    }
}
static void spr(const pic_t *p, float cx, float cy, float sx, float sy, float ang, float ax, float ay, int alpha,
                uint16_t tint, int ta)
{
    spr_cs(p, cx, cy, sx, sy, ang == 0 ? 1.0f : cosf(ang), ang == 0 ? 0.0f : sinf(ang), ax, ay, alpha, tint, ta);
}
// a soft round light that adds (as tfd_glow, with integer steps and a table: the glow of a bear at a big zoom is
// thousands of pixels)
static uint8_t s_glw[65];
static void glow_fast(float cx, float cy, float r, uint16_t c, int a)
{
    if (r < 1 || a <= 0) {
        return;
    }
    if (!s_glw[0]) {
        for (int i = 0; i <= 64; i++) {
            float q = 1 - i / 64.0f;
            s_glw[i] = (uint8_t)(255 * q * q);
        }
    }
    int ya = clampi((int)(cy - r), 0, SH), yb = clampi((int)(cy + r) + 1, 0, SH);
    int xa = clampi((int)(cx - r), 0, SW), xb = clampi((int)(cx + r) + 1, 0, SW);
    int32_t inv = (int32_t)(64.0f * 256.0f / (r * r));  // (d2 in 1/16 px units: 64 / r2, with 8 bits more)
    int icx = (int)(cx * 4), icy = (int)(cy * 4);
    for (int y = ya; y < yb; y++) {
        int dy = y * 4 + 2 - icy, dy2 = dy * dy;
        uint16_t *d = g_fb + y * SW;
        for (int x = xa; x < xb; x++) {
            int dx = x * 4 + 2 - icx;
            int t = (int)(((int64_t)(dx * dx + dy2) * inv) >> 12);
            if (t >= 64) {
                continue;
            }
            d[x] = tfd_add(d[x], c, s_glw[t] * a >> 8);
        }
    }
}
// a smooth line 1 px wide (Xiaolin Wu: two pixels for each step; the cost is its length)
static void aaline(float x0, float y0, float x1, float y1, uint16_t c, int a)
{
    float dx = x1 - x0, dy = y1 - y0;
    bool steep = fabsf(dy) > fabsf(dx);
    if (steep) {
        float t = x0;
        x0 = y0;
        y0 = t;
        t = x1;
        x1 = y1;
        y1 = t;
        t = dx;
        dx = dy;
        dy = t;
    }
    if (x0 > x1) {
        float t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
        dx = -dx;
        dy = -dy;
    }
    float g = dx > 0.001f ? dy / dx : 0;
    int xa = (int)floorf(x0 + 0.5f), xb = (int)floorf(x1 + 0.5f);
    float y = y0 + g * (xa + 0.5f - x0) - 0.5f;
    for (int x = xa; x <= xb; x++, y += g) {
        int iy = (int)floorf(y);
        int f = (int)((y - iy) * 256);
        if (steep) {
            tfd_px(iy, x, c, a * (256 - f) >> 8);
            tfd_px(iy + 1, x, c, a * f >> 8);
        } else {
            tfd_px(x, iy, c, a * (256 - f) >> 8);
            tfd_px(x, iy + 1, c, a * f >> 8);
        }
    }
}
// a ring as a polygon of smooth lines (cheaper than a ring of exact pixels)
static void ring_fast(float cx, float cy, float r, uint16_t c, int a)
{
    int n = r < 6 ? 10 : r < 16 ? 16 : 24;
    float px = cx + r, py = cy;
    for (int i = 1; i <= n; i++) {
        float an = i * 6.2831853f / n, qx = cx + cosf(an) * r, qy = cy + sinf(an) * r;
        aaline(px, py, qx, qy, c, a);
        px = qx;
        py = qy;
    }
}
// a textured convex polygon (screen points; n <= 8). The texture point of a screen point: u = m[0] x + m[1] y + m[2],
// v = m[3] x + m[4] y + m[5] (wrap: the texture repeats; else the edge pixel stays). An edge of rim colour (smooth)
// first, then the texture in the polygon 1 px smaller. tint mixes in by ta / 256; alpha is the fade of a piece.
// the polygon p (convex, n <= 8) d px smaller on each side (o); false: too small for that
static bool poly_inset(const float *p, int n, float d, float *o)
{
    float area = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
    }
    if (fabsf(area) < 1) {
        return false;
    }
    float sg = area > 0 ? 1.0f : -1.0f;
    for (int i = 0; i < n; i++) {
        int h = (i + n - 1) % n, j = (i + 1) % n;
        float ax = p[2 * i] - p[2 * h], ay = p[2 * i + 1] - p[2 * h + 1], bx = p[2 * j] - p[2 * i],
              by = p[2 * j + 1] - p[2 * i + 1];
        float la = sqrtf(ax * ax + ay * ay), lb = sqrtf(bx * bx + by * by);
        if (la < 0.01f || lb < 0.01f) {
            return false;
        }
        // (the inner normals; the corner of the two lines that are d px in)
        float nax = -ay / la * sg, nay = ax / la * sg, nbx = -by / lb * sg, nby = bx / lb * sg;
        float px = p[2 * h] + nax * d, py = p[2 * h + 1] + nay * d, qx = p[2 * i] + nbx * d,
              qy = p[2 * i + 1] + nby * d;
        float cr = ax * by - ay * bx;
        if (fabsf(cr) < 1e-4f * la * lb) {
            o[2 * i] = qx;
            o[2 * i + 1] = qy;
            continue;
        }
        float t = ((qx - px) * by - (qy - py) * bx) / cr;
        o[2 * i] = px + ax * t;
        o[2 * i + 1] = py + ay * t;
        float mx = o[2 * i] - p[2 * i], my = o[2 * i + 1] - p[2 * i + 1];
        if (mx * mx + my * my > 16 * d * d) {
            return false;  // (a sharp corner: no border)
        }
    }
    float a2 = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        a2 += o[2 * i] * o[2 * j + 1] - o[2 * j] * o[2 * i + 1];
    }
    return a2 * sg > fabsf(area) * 0.15f;
}
// the span of a convex polygon on the row sy (xl > xr: none)
static inline void span_at(const float *q, int n, float sy, float *xl, float *xr)
{
    *xl = 1e9f;
    *xr = -1e9f;
    for (int i = 0; i < n; i++) {
        float ax = q[2 * i], ay = q[2 * i + 1], bx = q[2 * ((i + 1) % n)], by = q[2 * ((i + 1) % n) + 1];
        if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
            float x = ax + (sy - ay) * (bx - ax) / (by - ay);
            *xl = minf(*xl, x);
            *xr = maxf(*xr, x);
        }
    }
}
static void tex_poly(const float *pt, int n, const pic_t *t, const float *m, bool wrap, uint16_t rim, uint16_t tint,
                     int ta, int alpha, float bw)
{
    if (n < 3 || n > 8 || alpha <= 0) {
        return;
    }
    if (s_dim) {
        tint = 0;
        ta = ta > 128 ? ta : 128;
        rim = half565(rim);
    }
    // a dark border of bw px (the things must stand out from the background): the texture fills the
    // shape bw px smaller, the border colour fills the ring between the two shapes (each pixel is written once)
    float in[16];
    const float *q = pt;
    bool ring = bw > 0.5f && rim && poly_inset(pt, n, bw, in);
    if (ring) {
        q = in;
    }
    float y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        y0 = minf(y0, pt[2 * i + 1]);
        y1 = maxf(y1, pt[2 * i + 1]);
    }
    int ya = clampi((int)floorf(y0), 0, SH), yb = clampi((int)ceilf(y1), 0, SH);
    int tw = t->w, th = t->h;
    for (int y = ya; y < yb; y++) {
        float sy = y + 0.5f;
        float ol, orr, xl, xr;
        span_at(pt, n, sy, &ol, &orr);
        span_at(q, n, sy, &xl, &xr);
        int oa = clampi((int)ceilf(ol - 0.5f), 0, SW), ob = clampi((int)floorf(orr - 0.5f) + 1, 0, SW);
        int xa = xl > xr ? ob : clampi((int)ceilf(xl - 0.5f), oa, ob),
            xb = xl > xr ? ob : clampi((int)floorf(xr - 0.5f) + 1, xa, ob);
        uint16_t *d = g_fb + y * SW;
        if (ring && oa < ob) {
            if (alpha >= 256) {
                fill16(d, oa, xa, rim);
                fill16(d, xb, ob, rim);
            } else {
                for (int x = oa; x < xa; x++) {
                    d[x] = tfd_mix(d[x], rim, alpha);
                }
                for (int x = xb; x < ob; x++) {
                    d[x] = tfd_mix(d[x], rim, alpha);
                }
            }
        }
        if (xa >= xb) {
            continue;
        }
        // (16.16 fixed point; a texture that repeats has a width and a height of a power of 2: a mask wraps it)
        int32_t u = (int32_t)((m[0] * (xa + 0.5f) + m[1] * sy + m[2]) * 65536.0f);
        int32_t v = (int32_t)((m[3] * (xa + 0.5f) + m[4] * sy + m[5]) * 65536.0f);
        int32_t du = (int32_t)(m[0] * 65536.0f), dv = (int32_t)(m[3] * 65536.0f);
        const uint16_t *tc = t->c;
        bool plain = ta <= 0 && alpha >= 256;
        for (int x = xa; x < xb; x++, u += du, v += dv) {
            int iu = u >> 16, iv = v >> 16;
            if (wrap) {
                iu &= tw - 1;
                iv &= th - 1;
            } else {
                iu = iu < 0 ? 0 : iu >= tw ? tw - 1 : iu;
                iv = iv < 0 ? 0 : iv >= th ? th - 1 : iv;
            }
            uint16_t col = tc[iv * tw + iu];
            if (plain) {
                d[x] = col;
                continue;
            }
            if (ta > 0) {
                col = tfd_mix(col, tint, ta);
            }
            d[x] = alpha >= 256 ? col : tfd_mix(d[x], col, alpha);
        }
    }
    // the edge: a smooth line round the shape (it hides the steps of the fill)
    if (rim) {
        for (int i = 0; i < n; i++) {
            aaline(pt[2 * i], pt[2 * i + 1], pt[2 * ((i + 1) % n)], pt[2 * ((i + 1) % n) + 1], rim, alpha * 7 / 8);
        }
    }
}
// the texture map of a body: the local point (lx, ly) (m, from the centre of mass, y up) goes to the texture point
// u = u0 + lx * ku, v = v0 - ly * kv
static void body_map(const tp_body_t *b, float u0, float ku, float v0, float kv, float *m)
{
    float k = cam_ppm(), c = b->xf.q.c, s = b->xf.q.s;
    float cx0 = SW * 0.5f + s_shx, cy0 = SH * 0.5f + s_shy;
    float A = s_cam.x - b->xf.p.x - cx0 / k, B = s_cam.y - b->xf.p.y + cy0 / k;
    // lx = (c / k) x - (s / k) y + (c A + s B); ly = (-s / k) x - (c / k) y + (-s A + c B)
    float lx_x = c / k, lx_y = -s / k, lx_0 = c * A + s * B;
    float ly_x = -s / k, ly_y = -c / k, ly_0 = -s * A + c * B;
    m[0] = ku * lx_x;
    m[1] = ku * lx_y;
    m[2] = u0 + ku * lx_0;
    m[3] = -kv * ly_x;
    m[4] = -kv * ly_y;
    m[5] = v0 - kv * ly_0;
}
// a flat ellipse (a shadow, a decal on the cloth)
static void ellipse(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    if (rx < 0.5f || ry < 0.3f || a <= 0) {
        return;
    }
    int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
    for (int y = y0; y <= y1; y++) {
        float dy = (y + 0.5f - cy) / ry;
        if (dy * dy >= 1) {
            continue;
        }
        float hw = rx * sqrtf(1 - dy * dy);
        tfd_hspan((int)(cx - hw + 0.5f), (int)(cx + hw + 0.5f), y, c, (int)(a * (1 - dy * dy * 0.6f)));
    }
}
static void star5(float cx, float cy, float r, float ang, uint16_t c, int a)
{
    float p[20];
    for (int k = 0; k < 10; k++) {
        float an = ang - 1.5708f + k * 0.6283f, rr = (k & 1) ? r * 0.45f : r;
        p[2 * k] = cx + cosf(an) * rr;
        p[2 * k + 1] = cy + sinf(an) * rr;
    }
    tfd_poly(p, 10, c, a);
}

// ================================================================ the far picture and the cloth
// the part of the row y that the panel shows: the round layout = the circle (the left end a multiple of 4); the other
// layouts = the whole row
static inline void row_span(int y, int *xa, int *xb)
{
    *xa = 0;
    *xb = SW;
    if (!GS_RND) {
        return;
    }
    float r = SW * 0.5f, dy = y + 0.5f - SH * 0.5f, q = r * r - dy * dy;
    int h = q > 0 ? (int)sqrtf(q) + 2 : 0;
    *xa = maxi(0, (SW / 2 - h) & ~3);
    *xb = mini(SW, SW / 2 + h);
}
// (the rows ya .. yb - 1 of the frame)
// (a copy of the far picture in RGB565, one memcpy a row, is not faster than the palette look-up: the
// PSRAM bandwidth is the limit, 3.49 about 10 ms and CoreS3 about 17 ms for the background)
static void far_rows(int ya, int yb)
{
    const pic_t *p = &s_pic[P_FAR0 + s_world];
    int hz = (int)(s_hzy + s_lift);
    if (!p->idx) {
        if (ya < hz) {
            tfd_vgrad(0, ya, SW, mini(hz, yb) - ya, tfd_rgb(60, 30, 80), tfd_rgb(240, 120, 80));
        }
        return;
    }
    const uint16_t *lut = s_dim ? s_pdim[s_world] : p->pal;
    // (a small parallax: the far picture moves 1/16 of the move of the play line)
    int off = clampi((int)(80 + s_cam.x * PPM * s_cam.z * 0.0625f - s_shx * 0.3f), 0, p->w - SW);
    int dy = (int)(s_shy * 0.3f);
    for (int y = ya; y < hz && y < yb; y++) {
        int fy = clampi(y - hz + 176 - dy, 0, p->h - 1);
        const uint8_t *s = p->idx + fy * p->w + off;
        uint32_t *d = (uint32_t *)(void *)(g_fb + y * SW);  // (two pixels in one store; SW is even)
        int xa, xb, x;
        row_span(y, &xa, &xb);
        for (x = xa; x + 4 <= xb; x += 4) {
            d[x >> 1] = lut[s[x]] | (uint32_t)lut[s[x + 1]] << 16;
            d[(x >> 1) + 1] = lut[s[x + 2]] | (uint32_t)lut[s[x + 3]] << 16;
        }
        for (; x < xb; x++) {
            g_fb[y * SW + x] = lut[s[x]];  // (the round frame: 466 is no multiple of 4)
        }
    }
}
// the three colours of the cloth of each world (the gingham: white, one stripe, two stripes). Dark and soft (a small
// step from check to check): the fort and the bears on it must be the brightest things on the table.
static const uint8_t CLOTH[3][3][3] = {
        {{150, 112, 108}, {124, 70, 74}, {100, 50, 58}},   // the picnic: red and white in the shade of the sunset
        {{82, 90, 110}, {62, 70, 90}, {46, 52, 70}},       // the kitchen: a blue towel on the counter
        {{150, 112, 134}, {128, 84, 112}, {106, 62, 94}},  // the party: pink
};
static void cloth_rows(int ya, int yb)
{
    // the scale of a row: s = 1 on the play line, 0 on the vanishing line (over the far end of the table: s_hzy)
    float k = cam_ppm(), H = s_tsy - s_hzy, Hv = s_tsy - s_vpy, chk = 0.5f;
    float depthk = 0.34f * chk * k / Hv;  // depth units of one check (the depth d = 1 / s)
    float edge = L && L->edge > 1.0f ? L->edge : 1e9f;
    uint16_t cw = tfd_rgb(CLOTH[s_world][0][0], CLOTH[s_world][0][1], CLOTH[s_world][0][2]);
    uint16_t cm = tfd_rgb(CLOTH[s_world][1][0], CLOTH[s_world][1][1], CLOTH[s_world][1][2]);
    uint16_t cd = tfd_rgb(CLOTH[s_world][2][0], CLOTH[s_world][2][1], CLOTH[s_world][2][2]);
    uint16_t fog = s_hzc[s_world];
    uint16_t floor_c = s_world == 1 ? tfd_rgb(22, 24, 36) : tfd_rgb(40, 26, 44);
    float camx = s_cam.x - s_shx / k;
    for (int y = maxi((int)(s_hzy + s_lift), ya); y < yb && y < SH; y++) {
        float yy = y + 0.5f - s_shy * 0.6f - s_lift;
        float s = maxf(0.03f, (yy - s_vpy) / Hv), s2 = maxf(0.03f, (yy + 1 - s_vpy) / Hv);
        float dv = 1.0f / s / depthk, dd = (1.0f / s - 1.0f / s2) / depthk;
        int iv = (int)floorf(dv) & 1;
        float f0 = dv - floorf(dv);
        int cov = dd > 0.001f ? clampi((int)(256 * f0 / dd), 0, 256) : 256;  // (the part of the row in this check)
        uint16_t a0 = iv ? cm : cw, a0p = iv ? cw : cm;                      // iu even: this check, the next one
        uint16_t b0 = iv ? cd : cm, b0p = iv ? cm : cd;                      // iu odd
        uint16_t A = cov >= 256 ? a0 : tfd_mix(a0p, a0, cov), B = cov >= 256 ? b0 : tfd_mix(b0p, b0, cov);
        // the fog to the horizon, and the light: the cloth near the eye is a little darker
        float fa = clampf(1.0f - (yy - s_hzy) / (H * 1.1f), 0, 1);
        int fogA = (int)(fa * fa * 170);
        float near = clampf((yy - s_tsy) / (SH - s_tsy + 1), 0, 1);
        uint16_t shade = s_world == 1 ? tfd_rgb(10, 12, 24) : tfd_rgb(60, 20, 40);
        A = tfd_mix(tfd_mix(A, shade, (int)(near * 70)), fog, fogA);
        B = tfd_mix(tfd_mix(B, shade, (int)(near * 70)), fog, fogA);
        if (s_dim) {
            A = half565(A);
            B = half565(B);
        }
        uint16_t *d = g_fb + y * SW;
        int xa, xb;
        row_span(y, &xa, &xb);
        float du = 1.0f / (k * s * chk);  // checks for one px
        float u = (camx + (xa + 0.5f - SW * 0.5f) / (k * s)) / chk;
        float xe = SW * 0.5f + (edge - camx) * k * s;
        int xend = xe < SW ? clampi((int)xe, 0, SW) : SW;
        xend = clampi(xend, xa, xb);
        if (du > 0.6f) {
            fill16(d, xa, xend, tfd_mix(A, B, 128));
        } else {
            int x = xa;
            while (x < xend) {
                float fu = floorf(u);
                int odd = (int)fu & 1;
                float xb = x + (fu + 1 - u) / du;  // the next line of the checks
                int xi = (int)xb;
                uint16_t c = odd ? B : A;
                int stop = xi < xend ? xi : xend;
                if (stop > x) {
                    fill16(d, x, stop, c);
                    x = stop;
                }
                if (x >= xend) {
                    break;
                }
                int fr = (int)((xb - xi) * 256);
                d[x] = tfd_mix(odd ? A : B, c, fr);  // (the pixel of the line: both colours)
                x++;
                u = fu + 1 + (x - xb) * du;
            }
        }
        if (xend < xb) {
            // the end of the table: its edge, then the floor far down
            fill16(d, xend, xb, tfd_mix(floor_c, fog, fogA / 2));
            if (xend > xa) {
                d[xend - 1] = tfd_mix(d[xend - 1], tfd_rgb(255, 240, 220), 140);
            }
        }
    }
}
// the background (the far picture and the cloth). (a second core for half of the rows gives no gain on the
// 3.49, 9.5 ms against 9.8 ms: the cost is the write of the frame in PSRAM, not the work of the CPU)
static void bg_draw(void)
{
    far_rows(0, SH);
    cloth_rows(0, SH);
}

// the props out of focus behind the play line (two of each world, at fixed points of the world)
static void props_draw(void)
{
    static const struct {
        int pic;
        float x, s, k;
    } PR[3][3] = {
            {{P_P0A, -3.6f, 0.46f, 1.05f}, {P_P0B, 15.5f, 0.52f, 0.95f}, {P_P0A, 26.0f, 0.40f, 1.0f}},
            {{P_P1B, -3.2f, 0.48f, 1.0f}, {P_P1A, 16.0f, 0.44f, 1.0f}, {P_P1B, 27.0f, 0.42f, 0.9f}},
            {{P_P2A, 16.5f, 0.46f, 0.9f}, {P_P2B, -3.4f, 0.50f, 1.0f}, {P_P2B, 27.5f, 0.42f, 0.95f}},
    };
    // (.s: the depth of the prop from the far end of the table (0) to the play line (1))
    float k = cam_ppm(), H = s_tsy - s_hzy, Hv = s_tsy - s_vpy;
    for (int i = 0; i < 3; i++) {
        const pic_t *p = &s_pic[PR[s_world][i].pic];
        if (!p->c) {
            continue;
        }
        float by = s_hzy + H * PR[s_world][i].s, s = (by - s_vpy) / Hv;
        float sc = s * s_cam.z * PR[s_world][i].k;
        float sx = SW * 0.5f + s_shx + (PR[s_world][i].x - s_cam.x) * k * s;
        by += s_shy * 0.5f + s_lift;
        if (sx + p->w * sc < 0 || sx - p->w * sc > SW) {
            continue;
        }
        ellipse(sx, by, p->w * sc * 0.45f, 2.0f + 2 * s, tfd_rgb(40, 20, 30), 70);
        spr(p, sx, by, sc, sc, 0, p->w * 0.5f, (float)p->h - 2, 256, 0, 0);
    }
}

// ================================================================ the things
static uint16_t s_rim;
static void verts_screen(const tp_body_t *b, float *pt)
{
    tp_vec_t v[TP_MAX_VERTS];
    int n = tp_verts(b, v);
    for (int i = 0; i < n; i++) {
        pt[2 * i] = wsx(v[i].x);
        pt[2 * i + 1] = wsy(v[i].y);
    }
}
// the box of the local shape of a body (the corners vl)
static void local_box(const tp_body_t *b, float *x0, float *y0, float *x1, float *y1)
{
    *x0 = *y0 = 1e9f;
    *x1 = *y1 = -1e9f;
    for (int i = 0; i < b->n; i++) {
        *x0 = minf(*x0, b->vl[i].x);
        *x1 = maxf(*x1, b->vl[i].x);
        *y0 = minf(*y0, b->vl[i].y);
        *y1 = maxf(*y1, b->vl[i].y);
    }
}
static float s_eye_x, s_eye_y;  // the point that the enemies look at (the bear, or the sling)
static void enemy_face(const tp_body_t *b, const thing_t *T, const trun_t *R, int alpha)
{
    const int e = T->kind - OK_KNIGHT;
    float s = ENEMY[e].s, h = ENEMY[e].h, k = cam_ppm(), c = b->xf.q.c, sn = b->xf.q.s;
    float now = (float)s_ticks * STEP_DT;
    bool hurt = now - R->hit_t < 0.35f && R->hit_t > 0;
#define LP(lx, ly) wsx(b->xf.p.x + c * (lx) - sn * (ly)), wsy(b->xf.p.y + sn * (lx) + c * (ly))
    // the eyes look at the bear; a blink now and then
    float ex = s_eye_x - b->xf.p.x, ey = s_eye_y - b->xf.p.y, el = sqrtf(ex * ex + ey * ey) + 0.01f;
    float lx = (c * ex + sn * ey) / el, ly = (-sn * ex + c * ey) / el;  // (the direction in the frame of the body)
    float er = s * 0.115f * k;
    bool blink = ((s_ticks + (uint32_t)(T->x * 97)) % 230) < 7;
    for (int side = -1; side <= 1; side += 2) {
        float ox = side * s * 0.15f, oy = h * 0.02f, X, Y;
        float px[2] = {LP(ox, oy)};
        X = px[0];
        Y = px[1];
        if (hurt || blink) {
            tfd_line(X - er, Y, X + er, Y, maxf(1.2f, er * 0.45f), tfd_rgb(70, 30, 10), alpha);
        } else {
            tfd_disc(X, Y, er, tfd_rgb(255, 255, 250), alpha);
            float qx = lx * er * 0.45f, qy = ly * er * 0.45f;
            float pp[2] = {LP(ox + qx / k, oy + qy / k)};
            tfd_disc(pp[0], pp[1], er * 0.55f, tfd_rgb(30, 16, 10), alpha);
            // the angry brow
            float b0[2] = {LP(ox - side * s * 0.09f, oy + s * 0.15f)},
                  b1[2] = {LP(ox + side * s * 0.07f, oy + s * 0.10f)};
            tfd_line(b0[0], b0[1], b1[0], b1[1], maxf(1.2f, er * 0.5f), tfd_rgb(120, 50, 10), alpha);
        }
    }
    // the mouth: a frown (a smile of joy when the bear misses: no; a grin when the bear is gone)
    float m0[2] = {LP(-s * 0.1f, -h * 0.13f)}, m1[2] = {LP(0, -h * 0.09f)}, m2[2] = {LP(s * 0.1f, -h * 0.13f)};
    float mw = maxf(1.1f, s * 0.03f * k);
    tfd_line(m0[0], m0[1], m1[0], m1[1], mw, tfd_rgb(110, 40, 10), alpha);
    tfd_line(m1[0], m1[1], m2[0], m2[1], mw, tfd_rgb(110, 40, 10), alpha);
    // a crack after half the damage
    if (R->dmg > ENEMY[e].hp * 0.5f) {
        float c0[2] = {LP(s * 0.18f, h * 0.30f)}, c1[2] = {LP(s * 0.08f, h * 0.18f)},
              c2[2] = {LP(s * 0.16f, h * 0.06f)};
        tfd_line(c0[0], c0[1], c1[0], c1[1], 1.2f, tfd_rgb(120, 60, 10), alpha);
        tfd_line(c1[0], c1[1], c2[0], c2[1], 1.2f, tfd_rgb(120, 60, 10), alpha);
    }
    // the helmet on the tip (it turns with the body)
    const pic_t *hp = &s_pic[P_HELM0 + e];
    float hs = (e == 2 ? 0.80f : 0.62f) * s * k / 34.0f;
    float hpt[2] = {LP(0, h * (e == 2 ? 0.30f : 0.24f))};
    spr_cs(hp, hpt[0], hpt[1], hs, hs, c, -sn, 24, 30, alpha, tfd_rgb(255, 255, 255), hurt ? 120 : 0);
#undef LP
}
static void thing_draw(const tp_body_t *b, const thing_t *T, const trun_t *R)
{
    float pt[2 * TP_MAX_VERTS], m[6];
    int alpha = b->life0 > 0 ? clampi((int)(256 * b->life / b->life0), 0, 256) : 256;
    if (b->shape != TP_POLY || b->n < 3) {
        return;
    }
    verts_screen(b, pt);
    float k = cam_ppm();
    float x0, y0, x1, y1;
    local_box(b, &x0, &y0, &x1, &y1);
    bool piece = b->gen > 0;
    const pic_t *t;
    bool wrap = true;
    uint16_t rim;
    switch (T->kind) {
    case OK_STICK:
        t = &s_pic[P_PRETZEL];
        // (along the stick: the texture repeats; across it: the 16 rows fill the thickness)
        body_map(b, 0, 64.0f / 0.5f, 8, 16.0f / 0.09f, m);
        rim = tfd_rgb(90, 42, 12);
        break;
    case OK_CRACKER:
    case OK_GIFT:
    case OK_JAR:
    case OK_CUP:
    case OK_CAN: {
        int id = T->kind == OK_CRACKER ? P_CRACKER
                 : T->kind == OK_JAR   ? P_JAR
                 : T->kind == OK_CUP   ? P_CUP
                 : T->kind == OK_CAN   ? P_CAN
                                       : P_GIFT0 + T->var % 3;
        t = &s_pic[id];
        // the whole thing: the picture fills its box; a piece: the middle of the picture, at the same scale
        float w = piece ? (T->kind == OK_JAR   ? 0.40f
                           : T->kind == OK_CUP ? 0.44f
                           : T->kind == OK_CAN ? 0.44f
                                               : maxf(0.2f, T->w))
                        : x1 - x0;
        float h = piece ? (T->kind == OK_JAR   ? 0.50f
                           : T->kind == OK_CUP ? 0.50f
                           : T->kind == OK_CAN ? 0.62f
                                               : maxf(0.2f, T->h > 0 ? T->h : T->w))
                        : y1 - y0;
        float ku = t->w / maxf(0.05f, w), kv = t->h / maxf(0.05f, h);
        body_map(b, piece ? t->w * 0.5f : -x0 * ku, ku, piece ? t->h * 0.5f : y1 * kv, kv, m);
        wrap = false;
        rim = T->kind == OK_JAR    ? tfd_rgb(120, 30, 30)
              : T->kind == OK_CAN  ? tfd_rgb(80, 80, 90)
              : T->kind == OK_GIFT ? tfd_rgb(70, 30, 50)
                                   : tfd_rgb(140, 90, 30);
        break;
    }
    case OK_BLOCK:
        t = &s_pic[P_BLOCK0 + T->var % 4];
        body_map(b, 0, 80, 0, 80, m);
        rim = tfd_rgb(40, 28, 24);
        break;
    default:  // the chips and the enemies
        t = &s_pic[P_CHIP];
        body_map(b, 32, 110, 32, 110, m);
        rim = tfd_rgb(150, 82, 16);
        break;
    }
    uint16_t tint = 0;
    int ta = 0;
    if (ok_enemy(T->kind)) {
        float now = (float)s_ticks * STEP_DT;
        if (R->hit_t > 0 && now - R->hit_t < 0.12f) {
            tint = tfd_rgb(255, 255, 255);
            ta = 150;
        } else {
            tint = tfd_rgb(255, 120, 40);
            ta = 40;
        }  // (the enemies: a warmer chip)
    }
    // (the border: a darker copy of the rim colour, wider at a big zoom; thin on a small piece)
    float bw = piece ? clampf(0.012f * k, 0.8f, 1.6f) : clampf(0.028f * k, 1.2f, 3.2f);
    tex_poly(pt, b->n, t, m, wrap, tfd_mix(rim, tfd_rgb(20, 8, 12), 150), tint, ta, alpha, bw);
    // a shine on the top edge of the big things (the light of the sky)
    if (!piece && (T->kind == OK_JAR || T->kind == OK_CAN || T->kind == OK_CUP) && k > 20) {
        float c = b->xf.q.c, s = b->xf.q.s, lx = x0 + (x1 - x0) * 0.22f;
        float ax = b->xf.p.x + c * lx - s * (y1 - 0.06f), ay = b->xf.p.y + s * lx + c * (y1 - 0.06f);
        float bx = b->xf.p.x + c * lx - s * (y0 + 0.08f), by = b->xf.p.y + s * lx + c * (y0 + 0.08f);
        tfd_line(wsx(ax), wsy(ay), wsx(bx), wsy(by), maxf(1, 0.03f * k), tfd_rgb(255, 255, 255), 70 * alpha / 256);
    }
    if (ok_enemy(T->kind) && !piece && k > 14) {
        enemy_face(b, T, R, alpha);
    }
    (void)k;
}
static void balloon_draw(const tp_body_t *b, const thing_t *T, int ti)
{
    static const uint8_t BC[4][3] = {{240, 60, 90}, {80, 170, 250}, {255, 200, 40}, {150, 90, 230}};
    const uint8_t *c = BC[T->var % 4];
    float k = cam_ppm(), x = wsx(b->xf.p.x), y = wsy(b->xf.p.y), r = 0.28f * k;
    // the rope: from its joint (a soft curve: two parts)
    for (int j = 0; j < W->joint_hi; j++) {
        const tp_joint_t *J = &W->joint[j];
        if (!(J->flags & TPJ_ALIVE) || J->user != ti) {
            continue;
        }
        tp_vec_t pa, pb;
        tp_joint_anchors(W, J, &pa, &pb);
        float ax = wsx(pa.x), ay = wsy(pa.y), bx = wsx(pb.x), by = wsy(pb.y);
        float mx = (ax + bx) * 0.5f + 2, my = (ay + by) * 0.5f + 3;
        tfd_line(ax, ay, mx, my, 1.0f, tfd_rgb(250, 246, 236), 200);
        tfd_line(mx, my, bx, by, 1.0f, tfd_rgb(250, 246, 236), 200);
    }
    float sq = 1.0f + 0.04f * sinf(s_ticks * 0.08f + ti);
    glow_fast(x, y, r * 1.6f, tfd_rgb(c[0], c[1], c[2]), 50);
    float tri[6] = {x - r * 0.18f, y + r * 1.12f, x + r * 0.18f, y + r * 1.12f, x, y + r * 0.9f};
    tfd_poly(tri, 3, tfd_rgb(c[0] * 3 / 4, c[1] * 3 / 4, c[2] * 3 / 4), 256);
    tfd_disc(x, y, r * sq, tfd_rgb(c[0] * 4 / 5, c[1] * 4 / 5, c[2] * 4 / 5), 256);
    tfd_disc(x - r * 0.12f, y - r * 0.1f, r * 0.82f * sq, tfd_rgb(c[0], c[1], c[2]), 256);
    tfd_disc(x - r * 0.38f, y - r * 0.4f, r * 0.24f, tfd_rgb(255, 255, 255), 190);
}
static void dip_draw(void)
{
    float k = cam_ppm();
    for (int i = 0; i < s_ndip; i++) {
        float x0 = wsx(s_dip[i].x0), x1 = wsx(s_dip[i].x1), yt = wsy(s_dip[i].y1), yb = wsy(0);
        float bowl[8] = {x0, yt, x1, yt, x1 - (x1 - x0) * 0.1f, yb, x0 + (x1 - x0) * 0.1f, yb};
        tfd_poly(bowl, 4, tfd_rgb(236, 232, 226), 256);
        // the cheese: a little lower than the rim, it moves
        float cy = yt + 0.05f * k;
        for (int x = (int)x0 + 2; x < (int)x1 - 2; x++) {
            float wv = sinf(x * 0.21f + s_ticks * 0.06f) * 1.2f;
            tfd_hspan(x, x + 1, (int)(cy + wv), tfd_rgb(255, 214, 90), 256);
            for (int y = (int)(cy + wv) + 1; y < (int)(cy + 0.12f * k); y++) {
                tfd_hspan(x, x + 1, y, tfd_rgb(255, 170, 30), 256);
            }
        }
        tfd_rect((int)x0, (int)yt - 1, (int)(x1 - x0), 2, tfd_rgb(255, 255, 255), 200);
        tfd_glow((x0 + x1) * 0.5f, cy, (x1 - x0) * 0.4f, tfd_rgb(255, 180, 40), 40);
    }
}

// ================================================================ the bears
static void bear_spr(int kind, float x, float y, float r, float ang, float sqx, float sqy, int alpha, bool glow)
{
    if (kind < 0 || kind >= BK_N) {
        return;
    }
    float k = cam_ppm(), sc = 2.55f * r * k / 71.0f;
    float X = wsx(x), Y = wsy(y);
    if (glow) {
        glow_fast(X, Y, r * k * 1.35f, tfd_rgb(BEAR[kind].c[0], BEAR[kind].c[1], BEAR[kind].c[2]), 70);
    }
    spr(&s_pic[P_BEAR0 + kind], X, Y, sc * sqx, sc * sqy, ang, 32, 41, alpha, 0, 0);
}
static void bear_shadow(float x, float y, float r)
{
    float h = y - r;
    if (h > 2.5f) {
        return;
    }
    float k = cam_ppm(), a = 1.0f - clampf(h / 2.5f, 0, 1);
    ellipse(wsx(x), wsy(0) + 1, r * k * (0.8f + 0.4f * a), maxf(1.2f, r * k * 0.18f), tfd_rgb(40, 10, 20),
            (int)(110 * a));
}
static float s_hop_t;  // the hop of the next bear from the queue to the sling (0..1)
static void queue_draw(void)
{
    // the bears that wait on the table, at the left of the sling
    int skip = s_phase == PH_HOP ? 0 : 0;
    for (int i = skip; i < s_nqueue; i++) {
        int kd = s_queue[i];
        float r = BEAR[kd].r, x = SL_X - 0.75f - i * 0.62f;
        float bob = fabsf(sinf(s_ticks * 0.05f + i * 1.3f)) * 0.06f;
        bear_shadow(x, r, r);
        bear_spr(kd, x, r + bob, r, 0, 1.0f + bob * 0.6f, 1.0f - bob * 0.6f, 256, false);
    }
}
// the sling: a Y of wood at x = 0 with two bands of red rubber to the pouch
#define SL_ARM_L (-0.17f)
#define SL_ARM_R 0.19f
#define SL_ARM_Y 1.10f
static void sling_back(float px, float py)
{
    float k = cam_ppm();
    tfd_line(wsx(SL_X + SL_ARM_L), wsy(SL_ARM_Y), wsx(px), wsy(py), maxf(1.5f, 0.06f * k), tfd_rgb(120, 20, 30), 256);
}
static void sling_post(void)
{
    float k = cam_ppm(), w = maxf(2.0f, 0.10f * k);
    uint16_t wood = tfd_rgb(140, 84, 40), lt = tfd_rgb(200, 140, 80);
    ellipse(wsx(SL_X), wsy(0) + 1, 0.3f * k, maxf(1.2f, 0.05f * k), tfd_rgb(40, 10, 20), 100);
    tfd_line(wsx(SL_X), wsy(0), wsx(SL_X), wsy(0.66f), w, wood, 256);
    tfd_line(wsx(SL_X), wsy(0.66f), wsx(SL_X + SL_ARM_L), wsy(SL_ARM_Y), w * 0.85f, wood, 256);
    tfd_line(wsx(SL_X), wsy(0.66f), wsx(SL_X + SL_ARM_R), wsy(SL_ARM_Y), w * 0.85f, wood, 256);
    tfd_line(wsx(SL_X) - w * 0.2f, wsy(0.05f), wsx(SL_X) - w * 0.2f, wsy(0.62f), maxf(1, w * 0.3f), lt, 150);
    tfd_disc(wsx(SL_X + SL_ARM_L), wsy(SL_ARM_Y), w * 0.5f, lt, 256);
    tfd_disc(wsx(SL_X + SL_ARM_R), wsy(SL_ARM_Y), w * 0.5f, lt, 256);
}
static void sling_front(float px, float py)
{
    float k = cam_ppm();
    tfd_line(wsx(SL_X + SL_ARM_R), wsy(SL_ARM_Y), wsx(px), wsy(py), maxf(1.5f, 0.065f * k), tfd_rgb(200, 40, 50), 256);
    ellipse(wsx(px), wsy(py), maxf(2, 0.09f * k), maxf(1.5f, 0.07f * k), tfd_rgb(90, 40, 20), 256);
}
// the pouch now: the aim, the hop, or the rest
static void pouch_now(float *x, float *y)
{
    if (s_phase == PH_AIM && s_aiming && s_aim_p > 0.01f) {
        pouch_at(s_aim_a, s_aim_p, x, y);
    } else {
        *x = SL_X + 0.01f;
        *y = SL_Y;
    }
}
// the dots of the aim: the path of the next shot for the first 0.75 s (no drag in the world: the parabola is exact
// until the first hit)
static void aim_dots(void)
{
    if (s_phase != PH_AIM || !s_aiming || s_aim_p < PULL_MIN || s_bear_kind < 0) {
        return;
    }
    float x, y, vx, vy;
    pouch_at(s_aim_a, s_aim_p, &x, &y);
    shot_speed(s_bear_kind, s_aim_a, s_aim_p, &vx, &vy);
    float k = cam_ppm();
    for (int i = 1; i <= 15; i++) {
        float t = i * 0.05f, px = x + vx * t, py = y + vy * t - 4.9f * t * t;
        if (py < 0) {
            break;
        }
        float r = maxf(1.2f, (0.055f - i * 0.002f) * k);
        tfd_disc(wsx(px), wsy(py), r, tfd_rgb(255, 255, 255), 230 - i * 10);
    }
}
static void path_draw(void)
{
    // the dots of the last shot (and of this one while it flies)
    for (int q = 0; q < 2; q++) {
        int pi = q == 0 ? s_pathi ^ 1 : s_pathi;
        if (q == 0 && s_phase == PH_FLY) {
            continue;
        }
        int a = q == 0 ? 110 : 170;
        for (int i = 0; i < s_npath[pi]; i++) {
            tfd_disc(wsx(s_path[pi][i][0]), wsy(s_path[pi][i][1]), (i & 1) ? 1.3f : 1.9f, tfd_rgb(255, 250, 240), a);
        }
    }
}
static void bears_draw(void)
{
    // the bear in the sling (aim, hop) or in the air
    if (s_phase == PH_HOP && s_bear_kind >= 0) {
        float t = clampf((float)s_tick / HOP_TICKS, 0, 1), r = BEAR[s_bear_kind].r;
        float x0 = SL_X - 0.75f, y0 = r, x1 = SL_X + 0.01f, y1 = SL_Y;
        float x = lerpf(x0, x1, t), y = lerpf(y0, y1, t) + sinf(t * 3.14159f) * 0.7f;
        bear_shadow(x, y, r);
        bear_spr(s_bear_kind, x, y, r, -t * 6.2832f, 1, 1, 256, true);
    } else if (s_phase == PH_AIM && s_bear_kind >= 0) {
        float x, y, r = BEAR[s_bear_kind].r;
        pouch_now(&x, &y);
        float st = s_aiming ? s_aim_p * 0.18f : 0;
        bear_spr(s_bear_kind, x, y + r * 0.35f, r, 0, 1 + st, 1 - st, 256, true);
    }
    for (int i = 0; i < s_nbody; i++) {
        tp_body_t *b = tp_body(W, s_bear[i]);
        if (!b) {
            continue;
        }
        float r = b->radius, sq = clampf(s_bear_sq, -0.5f, 0.6f);
        // (the squash goes along the speed: the jelly flattens on a hit)
        bear_shadow(b->xf.p.x, b->xf.p.y, r);
        float ang = -atan2f(b->xf.q.s, b->xf.q.c);
        bear_spr(s_bear_kind, b->xf.p.x, b->xf.p.y, r, ang, 1 + sq * 0.35f, 1 - sq * 0.35f, 256, true);
        // the power waits: a ring that beats round the bear (the bear in the air: tap to use its power)
        if (s_phase == PH_FLY && !s_power_used && !s_power_armed && s_tick < 300) {
            float pulse = 0.5f + 0.5f * sinf(s_tick * 0.3f), k = cam_ppm();
            ring_fast(wsx(b->xf.p.x), wsy(b->xf.p.y), r * k * (1.55f + pulse * 0.3f), tfd_rgb(255, 255, 255),
                      110 + (int)(pulse * 110));
        }
        // a bear out of the top of the frame: an arrow at the edge
        float sy = wsy(b->xf.p.y);
        if (sy < -4) {
            float sx = clampf(wsx(b->xf.p.x), 10, SW - 10), ty = 3;
            if (GS_RND) {
                // (the round layout: the arrow at the edge of the circle)
                float r = SW * 0.5f - 6, dx;
                sx = clampf(sx, SW * 0.25f, SW * 0.75f);
                dx = sx - SW * 0.5f;
                ty = SH * 0.5f - sqrtf(r * r - dx * dx) + 4;
            }
            float tr[6] = {sx, ty, sx - 6, ty + 10, sx + 6, ty + 10};
            tfd_poly(tr, 3, tfd_rgb(BEAR[s_bear_kind].c[0], BEAR[s_bear_kind].c[1], BEAR[s_bear_kind].c[2]), 256);
        }
    }
    if (s_ghost_t >= 0) {
        float t = s_ghost_t / 0.45f, r = BEAR[s_ghost_k].r;
        bear_spr(s_ghost_k, s_ghost_x, s_ghost_y, r * (1 + t * 0.8f), 0, 1 + t * 0.6f, 1 - t * 0.5f,
                 (int)(256 * (1 - t)), false);
    }
}

// ================================================================ the particles
// a small flat quad (a crumb, a bit of confetti): no smooth edge (they are a few px and they move fast)
static void quad_fill(const float *q, uint16_t c, int a)
{
    float y0 = minf(minf(q[1], q[3]), minf(q[5], q[7])), y1 = maxf(maxf(q[1], q[3]), maxf(q[5], q[7]));
    int ya = clampi((int)floorf(y0), 0, SH), yb = clampi((int)ceilf(y1), 0, SH);
    for (int y = ya; y < yb; y++) {
        float xl, xr;
        span_at(q, 4, y + 0.5f, &xl, &xr);
        if (xl > xr) {
            continue;
        }
        int xa = clampi((int)(xl + 0.5f), 0, SW), xb = clampi((int)(xr + 0.5f), 0, SW);
        if (xb <= xa && xa < SW) {
            xb = xa + 1;  // (a thin row keeps 1 px)
        }
        uint16_t *d = g_fb + y * SW;
        for (int x = xa; x < xb; x++) {
            d[x] = a >= 256 ? c : tfd_mix(d[x], c, a);
        }
    }
}
static void parts_draw(void)
{
    float k = cam_ppm();
    // the particles of tfphys (crumbs, salsa, shards, sparks)
    for (int i = 0; i < W->npart; i++) {
        const tp_part_t *p = &W->part[i];
        float x = wsx(p->p.x), y = wsy(p->p.y);
        if (x < -4 || x > SW + 4 || y < -4 || y > SH + 4) {
            continue;
        }
        int a = p->life0 > 0 ? clampi((int)(300 * p->life / p->life0), 0, 256) : 256;
        switch (p->kind) {
        case TP_PART_WATER:
            tfd_disc(x, y, maxf(1.0f, 0.035f * k), tfd_rgb(214, 30, 30), a);
            break;
        case TP_PART_SHARD:
            tfd_rect((int)x, (int)y, 2, 1, tfd_rgb(220, 240, 255), a);
            break;
        case TP_PART_SPARK:
            tfd_rect((int)x, (int)y, 1, 1, tfd_rgb(255, 240, 160), a);
            break;
        default: {
            uint16_t c = p->mat == M_CRACKER ? tfd_rgb(230, 190, 120) : tfd_rgb(240, 170, 60);
            int s = k > 40 ? 2 : 1;
            tfd_rect((int)x, (int)y, s, s, c, a);
            break;
        }
        }
    }
    // the particles of the looks
    for (int i = 0; i < s_nfxp; i++) {
        const fxp_t *p = &s_fxp[i];
        float t = p->life / p->max, x = wsx(p->x), y = wsy(p->y), r = p->size * k;
        int a = (int)(256 * (1 - t));
        switch (p->kind) {
        case FP_PUFF:
            tfd_disc(x, y, r * (1 + t), p->col, a / 2);
            break;
        case FP_SPARK:
            tfd_disc(x, y, maxf(0.8f, r), p->col, a);
            break;
        case FP_STAR:
            star5(x, y, maxf(2, r * 1.4f), p->life * 6, p->col, a);
            break;
        case FP_DROP:
            tfd_disc(x, y, maxf(0.9f, r), p->col, a);
            break;
        case FP_CONFETTI:
        case FP_CRUMB: {
            float c = cosf(p->a) * maxf(1.2f, r), s = sinf(p->a) * maxf(1.2f, r);
            float q[8] = {x - c + s * 0.5f, y - s - c * 0.5f, x + c + s * 0.5f, y + s - c * 0.5f,
                          x + c - s * 0.5f, y + s + c * 0.5f, x - c - s * 0.5f, y - s + c * 0.5f};
            quad_fill(q, p->col, a);
            break;
        }
        case FP_HELMET: {
            float hs = 0.62f * p->size * k / 34.0f;
            spr(&s_pic[P_HELM0 + p->var % 3], x, y, hs, hs, p->a, 24, 24, a, 0, 0);
            break;
        }
        case FP_RING:
            ring_fast(x, y, r * (0.3f + t), p->col, a);
            break;
        default:
            break;
        }
    }
}
static void decals_draw(void)
{
    float k = cam_ppm();
    for (int i = 0; i < s_ndecal; i++) {
        const decal_t *d = &s_decal[i];
        float g = clampf(d->t * 4, 0.2f, 1), x = wsx(d->x), y = wsy(0) + 2;
        ellipse(x, y, d->w * k * g, maxf(1.5f, d->w * k * 0.16f * g), tfd_rgb(200, 26, 26), 200);
        ellipse(x - d->w * k * 0.3f, y + 1, d->w * k * 0.2f * g, maxf(1, d->w * k * 0.05f), tfd_rgb(60, 140, 40), 160);
    }
}

// ================================================================ the world in the frame
// the time of the parts of a frame (the log of the board: far, cloth, props, things, bears, particles, HUD)
static float s_prof[8];
static int64_t s_prof_t;
static inline void prof(int i)
{
    int64_t t = gs_us();
    s_prof[i] += (t - s_prof_t) / 1000.0f;
    s_prof_t = t;
}
static void world_draw(void)
{
    float sh = s_shake;
    s_shx = sh > 0.2f ? rnds() * sh : 0;
    s_shy = sh > 0.2f ? rnds() * sh * 0.6f : 0;
    s_prof_t = gs_us();
    bg_draw();
    prof(0);
    props_draw();
    decals_draw();
    prof(2);
    tfd_screen();
    // the shadows of the things on the table
    float k = cam_ppm();
    for (int i = 0; i < W->body_hi; i++) {
        const tp_body_t *b = &W->body[i];
        if (!(b->flags & TP_ALIVE) || b->kind != TP_DYNAMIC || !user_thing(b->user)) {
            continue;
        }
        float h = b->xf.p.y - b->extent * 0.7f;
        if (h > 1.5f || thing_of(b->user)->kind == OK_BALLOON) {
            continue;
        }
        float a = 1 - clampf(h / 1.5f, 0, 1);
        ellipse(wsx(b->xf.p.x), wsy(0) + 1, b->extent * k * 0.9f, maxf(1.2f, b->extent * k * 0.14f),
                tfd_rgb(40, 10, 24), (int)(80 * a));
    }
    // the point that the enemies look at
    tp_body_t *bb = s_nbody ? bear_main() : NULL;
    s_eye_x = bb ? bb->xf.p.x : SL_X;
    s_eye_y = bb ? bb->xf.p.y : SL_Y;
    dip_draw();
    queue_draw();
    prof(2);
    float px, py;
    pouch_now(&px, &py);
    bool in_sling = (s_phase == PH_AIM && s_bear_kind >= 0);
    sling_back(px, in_sling ? py : SL_Y);
    sling_post();
    // the things: the static blocks first, then the rest
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < W->body_hi; i++) {
            const tp_body_t *b = &W->body[i];
            if (!(b->flags & TP_ALIVE) || !user_thing(b->user)) {
                continue;
            }
            if ((b->kind == TP_STATIC) != (pass == 0)) {
                continue;
            }
            float sx = wsx(b->xf.p.x), r = b->extent * k + 30;
            if (sx + r < 0 || sx - r > SW) {
                continue;
            }
            const thing_t *T = thing_of(b->user);
            if (T->kind == OK_BALLOON) {
                balloon_draw(b, T, b->user);
            } else {
                thing_draw(b, T, &s_tr[b->user]);
            }
        }
    }
    prof(3);
    path_draw();
    bears_draw();
    sling_front(px, in_sling ? py : SL_Y);
    aim_dots();
    prof(4);
    parts_draw();
    prof(5);
}
