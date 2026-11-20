// RING SEQUENCER: the drawing. The still parts of a screen are in a layer in PSRAM (s_bg): the rings, the steps, the
// controls, the text. Each frame copies back from the layer only the rows that the moving parts of the frame before
// changed in this frame buffer (the firmware has two buffers: g_fb holds the frame of two frames ago), then draws the
// moving parts (the arm, the trails, the pulses) and notes their rows. So a frame writes a small part of the 466 x 466
// frame, not all of it. A change of the layer redraws its box (bg_box) and copies that box to the two buffers.
// (Part of app_ringseq.c.)

static uint16_t *s_bg;  // the layer: s_W x s_H
static int s_W, s_H;
static uint16_t *s_tg;                  // the target of the primitives (s_bg or g_fb)
static int s_cx0, s_cy0, s_cx1, s_cy1;  // the clip box (x1, y1 not in it)
static int16_t *s_mx0, *s_mx1;          // the marks of the rows of the target (NULL: no marks)
// the two frame buffers: the rows to copy back from the layer before the next frame in that buffer
static uint16_t *s_bid[2];
static int16_t *s_dx0[2], *s_dx1[2];
static uint8_t s_full[2];
static int s_roll;  // the next rows of the slow copy (it removes what the system drew over the frame)

static inline int ifl(float v)
{
    int i = (int)v;
    return (v < i) ? i - 1 : i;
}
static inline int ice(float v)
{
    int i = (int)v;
    return (v > i) ? i + 1 : i;
}
// a fast square root (good to about 0.1 %: the edges of the shapes)
static inline float fsq(float x)
{
    if (x <= 0) {
        return 0;
    }
    union {
        float f;
        uint32_t i;
    } u = {x};
    u.i = 0x1FBD1DF5u + (u.i >> 1);
    return 0.5f * (u.f + x / u.f);
}
static inline uint16_t swp(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
// a mix of two colours of the frame (a = 0..256)
static inline uint16_t mixc(uint16_t d, uint16_t s, int a)
{
    if (a >= 256) {
        return s;
    }
    uint32_t dd = swp(d), ss = swp(s);
    dd = (dd | (dd << 16)) & 0x07E0F81Fu;
    ss = (ss | (ss << 16)) & 0x07E0F81Fu;
    a >>= 3;
    uint32_t m = ((dd * (uint32_t)(32 - a) + ss * (uint32_t)a) >> 5) & 0x07E0F81Fu;
    return swp((uint16_t)(m | (m >> 16)));
}
// light that adds (a = 0..256)
static inline uint16_t addc(uint16_t d, uint16_t s, int a)
{
    uint16_t D = swp(d), Sx = swp(s);
    int r = (D >> 11) + (((Sx >> 11) * a) >> 8);
    int g = ((D >> 5) & 63) + ((((Sx >> 5) & 63) * a) >> 8);
    int b = (D & 31) + (((Sx & 31) * a) >> 8);
    r = r > 31 ? 31 : r;
    g = g > 63 ? 63 : g;
    b = b > 31 ? 31 : b;
    return swp((uint16_t)((r << 11) | (g << 5) | b));
}
static uint16_t col(int r, int g, int b)
{
    return rgb((uint8_t)(r < 0     ? 0
                         : r > 255 ? 255
                                   : r),
               (uint8_t)(g < 0     ? 0
                         : g > 255 ? 255
                                   : g),
               (uint8_t)(b < 0     ? 0
                         : b > 255 ? 255
                                   : b));
}
// the colour of ring r at f percent
static uint16_t rcol(int r, int f)
{
    return col(RCOL[r][0] * f / 100, RCOL[r][1] * f / 100, RCOL[r][2] * f / 100);
}

static void clip_all(void)
{
    s_cx0 = 0;
    s_cy0 = 0;
    s_cx1 = s_W;
    s_cy1 = s_H;
}
static inline void mark(int y, int x0, int x1)
{
    if (s_mx0 && (unsigned)y < (unsigned)s_H) {
        if (x0 < s_mx0[y]) {
            s_mx0[y] = (int16_t)(x0 < 0 ? 0 : x0);
        }
        if (x1 > s_mx1[y]) {
            s_mx1[y] = (int16_t)(x1 > s_W ? s_W : x1);
        }
    }
}
static inline bool row_in(int y)
{
    return y >= s_cy0 && y < s_cy1;
}
static inline void pxm(int x, int y, uint16_t c, int a)
{
    if (x >= s_cx0 && x < s_cx1 && a > 0) {
        uint16_t *p = &s_tg[y * s_W + x];
        *p = mixc(*p, c, a);
    }
}
static inline void pxa(int x, int y, uint16_t c, int a)
{
    if (x >= s_cx0 && x < s_cx1 && a > 0) {
        uint16_t *p = &s_tg[y * s_W + x];
        *p = addc(*p, c, a);
    }
}
static inline void pxk(int x, int y, uint16_t c, int a, bool add)
{
    if (add) {
        pxa(x, y, c, a);
    } else {
        pxm(x, y, c, a);
    }
}
static bool box_out(float x0, float y0, float x1, float y1)
{
    return x1 < s_cx0 || x0 >= s_cx1 || y1 < s_cy0 || y0 >= s_cy1;
}
static void p_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    int x0 = x < s_cx0 ? s_cx0 : x, x1 = x + w > s_cx1 ? s_cx1 : x + w;
    for (int j = y < s_cy0 ? s_cy0 : y; j < y + h && j < s_cy1; j++) {
        uint16_t *p = &s_tg[j * s_W];
        for (int i = x0; i < x1; i++) {
            p[i] = c;
        }
        mark(j, x0, x1);
    }
}

// a disc with a smooth edge (a = 0..256); add: light that adds
static void p_disc(float cx, float cy, float r, uint16_t c, int a, bool add)
{
    if (r <= 0 || box_out(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1)) {
        return;
    }
    for (int y = ifl(cy - r - 1); y <= ice(cy + r + 1); y++) {
        if (!row_in(y)) {
            continue;
        }
        float dy = y + 0.5f - cy, h2 = (r + 0.5f) * (r + 0.5f) - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float hw = fsq(h2), hi2 = (r - 0.5f) * (r - 0.5f) - dy * dy, hwi = hi2 > 0 ? fsq(hi2) : -1;
        int x0 = ifl(cx - hw), x1 = ice(cx + hw);
        mark(y, x0, x1 + 1);
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx;
            if (hwi > 0 && dx > -hwi && dx < hwi) {
                pxk(x, y, c, a, add);
                continue;
            }
            float cov = r + 0.5f - fsq(dx * dx + dy * dy);
            if (cov > 0) {
                pxk(x, y, c, cov >= 1 ? a : (int)(a * cov), add);
            }
        }
    }
}
// a soft glow (light that adds, strongest in the middle)
static void p_glow(float cx, float cy, float r, uint16_t c, int a)
{
    if (r < 1 || box_out(cx - r, cy - r, cx + r, cy + r)) {
        return;
    }
    float ir2 = 1.0f / (r * r);
    for (int y = ifl(cy - r); y <= ice(cy + r); y++) {
        if (!row_in(y)) {
            continue;
        }
        float dy = y + 0.5f - cy, h2 = r * r - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float hw = fsq(h2);
        int x0 = ifl(cx - hw), x1 = ice(cx + hw);
        mark(y, x0, x1 + 1);
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, q = 1.0f - (dx * dx + dy * dy) * ir2;
            if (q > 0) {
                pxa(x, y, c, (int)(a * q * q));
            }
        }
    }
}
// An arc of a ring (the middle radius r, the width w) from the angle a0 to a1 (radians, 0 = up, with the clock; the
// arc is shorter than a half turn; a0 = a1 = 0: the full ring). The inside pixels need no root.
static void p_arc(float cx, float cy, float r, float w, float a0, float a1, uint16_t c, int a, bool add)
{
    float ro = r + w / 2, ri = r - w / 2;
    if (ri < 0) {
        ri = 0;
    }
    if (box_out(cx - ro - 1, cy - ro - 1, cx + ro + 1, cy + ro + 1)) {
        return;
    }
    bool part = a1 > a0;
    float u0x = fsin(a0), u0y = -fcos(a0), u1x = fsin(a1), u1y = -fcos(a1);
    float ro2 = (ro + 0.5f) * (ro + 0.5f), ri2 = ri > 0.5f ? (ri - 0.5f) * (ri - 0.5f) : -1;
    float so2 = (ro - 0.5f) * (ro - 0.5f), si2 = (ri + 0.5f) * (ri + 0.5f);
    for (int y = ifl(cy - ro - 1); y <= ice(cy + ro + 1); y++) {
        if (!row_in(y)) {
            continue;
        }
        float dy = y + 0.5f - cy, h2 = ro2 - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float hw = fsq(h2), hin = ri2 - dy * dy > 0 ? fsq(ri2 - dy * dy) : -1;
        int x0 = ifl(cx - hw), x1 = ice(cx + hw), mx0 = 1 << 20, mx1 = -1;
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx;
            if (hin > 0 && dx > -hin && dx < hin) {
                int xe = ifl(cx + hin - 0.5f);  // (the hole of the ring: go to its last pixel)
                x = xe > x ? xe : x;
                continue;
            }
            float d2 = dx * dx + dy * dy;
            float cov = 1;
            if (d2 > so2 || d2 < si2) {
                float d = fsq(d2);
                float e = ro + 0.5f - d, f = d - ri + 0.5f;
                cov = e < f ? e : f;
                if (cov <= 0) {
                    continue;
                }
            }
            if (part) {
                float s0 = u0x * dy - u0y * dx, s1 = u1y * dx - u1x * dy;
                float m = s0 < s1 ? s0 : s1;
                if (m <= -0.5f) {
                    continue;
                }
                if (m < 0.5f && m + 0.5f < cov) {
                    cov = m + 0.5f;
                }
            }
            pxk(x, y, c, cov >= 1 ? a : (int)(a * cov), add);
            if (x < mx0) {
                mx0 = x;
            }
            mx1 = x;
        }
        if (mx1 >= 0) {
            mark(y, mx0, mx1 + 1);
        }
    }
}
static void p_ring(float cx, float cy, float r, float w, uint16_t c, int a, bool add)
{
    p_arc(cx, cy, r, w, 0, 0, c, a, add);
}
// a line with round ends (w = its width)
static void p_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int a, bool add)
{
    float r = w / 2, vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    float bx0 = (x0 < x1 ? x0 : x1) - r - 1, bx1 = (x0 > x1 ? x0 : x1) + r + 1;
    float by0 = (y0 < y1 ? y0 : y1) - r - 1, by1 = (y0 > y1 ? y0 : y1) + r + 1;
    if (box_out(bx0, by0, bx1, by1)) {
        return;
    }
    float iL2 = L2 > 0 ? 1.0f / L2 : 0, ivy = (vy > 0.01f || vy < -0.01f) ? 1.0f / vy : 0;
    for (int y = ifl(by0); y <= ice(by1); y++) {
        if (!row_in(y)) {
            continue;
        }
        float xa = bx0, xb = bx1;
        if (ivy != 0) {
            // the part of the segment near this row, then its x range
            float ta = (y - r - 1.5f - y0) * ivy, tb = (y + r + 2.5f - y0) * ivy;
            if (ta > tb) {
                float t = ta;
                ta = tb;
                tb = t;
            }
            ta = ta < 0 ? 0 : ta > 1 ? 1 : ta;
            tb = tb < 0 ? 0 : tb > 1 ? 1 : tb;
            float xa2 = x0 + vx * ta, xb2 = x0 + vx * tb;
            xa = (xa2 < xb2 ? xa2 : xb2) - r - 1.5f;
            xb = (xa2 > xb2 ? xa2 : xb2) + r + 1.5f;
        }
        int ia = ifl(xa), ib = ice(xb), m0 = 1 << 20, m1 = -1;
        for (int x = ia; x <= ib; x++) {
            float px = x + 0.5f - x0, py = y + 0.5f - y0, t = (px * vx + py * vy) * iL2;
            t = t < 0 ? 0 : t > 1 ? 1 : t;
            float dx = px - t * vx, dy = py - t * vy, d2 = dx * dx + dy * dy;
            if (d2 > (r + 0.5f) * (r + 0.5f)) {
                continue;
            }
            float cov = r + 0.5f - fsq(d2);
            if (cov > 0) {
                pxk(x, y, c, cov >= 1 ? a : (int)(a * cov), add);
                if (x < m0) {
                    m0 = x;
                }
                m1 = x;
            }
        }
        if (m1 >= 0) {
            mark(y, m0, m1 + 1);
        }
    }
}
// a box with round corners and a smooth edge
static void p_rrect(float x, float y, float w, float h, float rad, uint16_t c, int a)
{
    if (box_out(x, y, x + w, y + h)) {
        return;
    }
    if (rad * 2 > h) {
        rad = h / 2;
    }
    if (rad * 2 > w) {
        rad = w / 2;
    }
    for (int j = ifl(y); j < ice(y + h); j++) {
        if (!row_in(j)) {
            continue;
        }
        float py = j + 0.5f, dy = 0;
        if (py < y + rad) {
            dy = y + rad - py;
        } else if (py > y + h - rad) {
            dy = py - (y + h - rad);
        }
        int x0 = ifl(x), x1 = ice(x + w);
        mark(j, x0, x1);
        for (int i = x0; i < x1; i++) {
            float px = i + 0.5f, dx = 0;
            if (px < x + rad) {
                dx = x + rad - px;
            } else if (px > x + w - rad) {
                dx = px - (x + w - rad);
            }
            float cov;
            if (dx > 0 && dy > 0) {
                cov = rad + 0.5f - fsq(dx * dx + dy * dy);
            } else {
                float ex = (px - x < x + w - px ? px - x : x + w - px) + 0.5f;
                float ey = (py - y < y + h - py ? py - y : y + h - py) + 0.5f;
                cov = ex < ey ? ex : ey;
            }
            if (cov > 0) {
                pxm(i, j, c, cov >= 1 ? a : (int)(a * cov));
            }
        }
    }
}
// a filled triangle with smooth edges (the points in any order)
static void p_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c, int a)
{
    float ar = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (ar < 0) {
        float t = x1;
        x1 = x2;
        x2 = t;
        t = y1;
        y1 = y2;
        y2 = t;
    }
    float ex[3] = {x0, x1, x2}, ey[3] = {y0, y1, y2}, nx[3], ny[3], nc[3];
    for (int i = 0; i < 3; i++) {
        int k = (i + 1) % 3;
        float dx = ex[k] - ex[i], dy = ey[k] - ey[i], l = fsq(dx * dx + dy * dy);
        if (l < 0.01f) {
            return;
        }
        nx[i] = -dy / l;
        ny[i] = dx / l;
        nc[i] = -(nx[i] * ex[i] + ny[i] * ey[i]);
    }
    float bx0 = ex[0], bx1 = ex[0], by0 = ey[0], by1 = ey[0];
    for (int i = 1; i < 3; i++) {
        bx0 = ex[i] < bx0 ? ex[i] : bx0;
        bx1 = ex[i] > bx1 ? ex[i] : bx1;
        by0 = ey[i] < by0 ? ey[i] : by0;
        by1 = ey[i] > by1 ? ey[i] : by1;
    }
    for (int y = ifl(by0); y <= ice(by1); y++) {
        if (!row_in(y)) {
            continue;
        }
        mark(y, ifl(bx0), ice(bx1) + 1);
        for (int x = ifl(bx0); x <= ice(bx1); x++) {
            float px = x + 0.5f, py = y + 0.5f, m = 1e9f;
            for (int i = 0; i < 3; i++) {
                float d = nx[i] * px + ny[i] * py + nc[i];
                m = d < m ? d : m;
            }
            if (m > -0.5f) {
                pxm(x, y, c, m >= 0.5f ? a : (int)(a * (m + 0.5f)));
            }
        }
    }
}

// ---------------------------------------------------------------- text (the kfont / mfont faces: English, Hebrew)

typedef kfont_t font_t;  // (mfont_t has the same fields)
#define MF(f) ((const font_t *)(const void *)(f))

static const kglyph_t *glyph_of(const font_t *f, uint16_t cp)
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
static int utf8_cps(const char *s, uint16_t *cp, int max)
{
    int n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && n < max) {
        uint32_t c = *p++;
        if (c >= 0xE0 && p[0] && p[1]) {
            c = ((c & 15) << 12) | ((uint32_t)(p[0] & 63) << 6) | (p[1] & 63);
            p += 2;
        } else if (c >= 0xC0 && p[0]) {
            c = ((c & 31) << 6) | (p[0] & 63);
            p++;
        }
        cp[n++] = (uint16_t)c;
    }
    return n;
}
// the order on the screen: a line with Hebrew in the order of the Unicode Bidirectional Algorithm
// (libs/bidi/bidi.h): from the right, a Latin word or a number keeps its order, a bracket of a right-to-left part shows
// its mirror
static int visual(const char *s, uint16_t *out, int max)
{
    uint16_t cp[64];
    int16_t ord[64];
    uint8_t mir[64];
    int n = utf8_cps(s, cp, max < 64 ? max : 64);
    if (!bidi_order(cp, n, ord, mir)) {
        memcpy(out, cp, n * sizeof(uint16_t));
        return n;
    }
    for (int i = 0; i < n; i++) {
        out[i] = mir[i] ? bidi_mirror(cp[ord[i]]) : cp[ord[i]];
    }
    return n;
}
static float adv_of(const font_t *f, uint16_t c)
{
    const kglyph_t *g = glyph_of(f, c);
    return g ? g->adv4 / 4.0f : (c == ' ' ? f->asc * 0.3f : 0);
}
static int p_textw(const font_t *f, const char *s)
{
    uint16_t v[64];
    int n = visual(s, v, 64);
    float w = 0;
    for (int i = 0; i < n; i++) {
        w += adv_of(f, v[i]);
    }
    return (int)(w + 0.5f);
}
// a line of text: align 0 = x is the left end, 1 = the middle, 2 = the right end; base = the baseline
static void p_text(const font_t *f, float x, int base, const char *s, uint16_t c, int align)
{
    uint16_t v[64];
    int n = visual(s, v, 64);
    float w = 0;
    for (int i = 0; i < n; i++) {
        w += adv_of(f, v[i]);
    }
    x -= align == 1 ? w / 2 : align == 2 ? w : 0;
    if (box_out(x - 4, base - f->line, x + w + 4, base + f->line / 2)) {
        return;
    }
    for (int i = 0; i < n; i++) {
        const kglyph_t *g = glyph_of(f, v[i]);
        if (g) {
            int gx = (int)(x + 0.5f) + g->ox, gy = base + g->oy;
            const uint8_t *src = f->a4 + g->off;
            for (int j = 0; j < g->h; j++) {
                int y = gy + j;
                if (!row_in(y) || y < 0 || y >= s_H) {
                    continue;
                }
                mark(y, gx, gx + g->w);
                for (int k = 0; k < g->w; k++) {
                    int idx = j * g->w + k, a4 = (idx & 1) ? src[idx >> 1] >> 4 : src[idx >> 1] & 15;
                    if (a4) {
                        pxm(gx + k, y, c, a4 * 17 + (a4 * 17 >> 7));
                    }
                }
            }
        }
        x += adv_of(f, v[i]);
    }
}
// the face has each letter of s (the mfont faces have no Hebrew)
static bool has_all(const font_t *f, const char *s)
{
    uint16_t cp[64];
    int n = utf8_cps(s, cp, 64);
    for (int i = 0; i < n; i++) {
        if (cp[i] != ' ' && !glyph_of(f, cp[i])) {
            return false;
        }
    }
    return true;
}
// the largest face (from the list, big to small) whose width of s is w or less and that has its letters
static const font_t *fit(const char *s, float w, const font_t *a, const font_t *b, const font_t *c)
{
    const font_t *L3[3] = {a, b, c}, *last = a;
    for (int i = 0; i < 3; i++) {
        if (!L3[i] || !has_all(L3[i], s)) {
            continue;
        }
        if (p_textw(L3[i], s) <= w) {
            return L3[i];
        }
        last = L3[i];
    }
    return last;
}

// ---------------------------------------------------------------- the layer and the frame buffers

static bool gfx_alloc(void)
{
    if (s_bg && s_W == gfx_w && s_H == gfx_h) {
        return true;
    }
    free(s_bg);
    for (int i = 0; i < 2; i++) {
        free(s_dx0[i]);
        free(s_dx1[i]);
        s_dx0[i] = s_dx1[i] = NULL;
        s_bid[i] = NULL;
    }
    s_W = gfx_w;
    s_H = gfx_h;
    s_bg = heap_caps_malloc((size_t)s_W * s_H * 2, MALLOC_CAP_SPIRAM);
    for (int i = 0; i < 2; i++) {
        s_dx0[i] = heap_caps_malloc(s_H * 2, MALLOC_CAP_SPIRAM);
        s_dx1[i] = heap_caps_malloc(s_H * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_bg || !s_dx0[0] || !s_dx0[1] || !s_dx1[0] || !s_dx1[1]) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        for (int y = 0; y < s_H; y++) {
            s_dx0[i][y] = 32767;
            s_dx1[i][y] = -1;
        }
        s_full[i] = 1;
    }
    return true;
}
static void gfx_free(void)
{
    free(s_bg);
    s_bg = NULL;
    for (int i = 0; i < 2; i++) {
        free(s_dx0[i]);
        free(s_dx1[i]);
        s_dx0[i] = s_dx1[i] = NULL;
        s_bid[i] = NULL;
    }
    s_W = s_H = 0;
}
static void full_both(void)
{
    s_full[0] = s_full[1] = 1;
}
// a box of the layer changed: the two buffers copy it before their next frame
static void mark_both(int x0, int y0, int x1, int y1)
{
    for (int i = 0; i < 2; i++) {
        for (int y = y0 < 0 ? 0 : y0; y < y1 && y < s_H; y++) {
            if (x0 < s_dx0[i][y]) {
                s_dx0[i][y] = (int16_t)(x0 < 0 ? 0 : x0);
            }
            if (x1 > s_dx1[i][y]) {
                s_dx1[i][y] = (int16_t)(x1 > s_W ? s_W : x1);
            }
        }
    }
}
static int buf_slot(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_bid[i] == g_fb) {
            return i;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (!s_bid[i]) {
            s_bid[i] = g_fb;
            s_full[i] = 1;
            return i;
        }
    }
    s_bid[0] = g_fb;
    s_bid[1] = NULL;
    s_full[0] = 1;
    return 0;
}
// copy back the rows of the layer that this buffer needs; then the overlay marks go into its lists
#define ROLL_ROWS 12
// Some pixels of the buffer that no moving part changed must be the same as the layer; else something else drew in
// this buffer (the system, a test): then all of the layer goes back.
static bool frame_changed(int sl)
{
    const int16_t *a = s_dx0[sl], *b = s_dx1[sl];
    int gx0 = s_W / 2 - 100, gx1 = s_W / 2 + 100, gy1 = s_H / 6 + 30;
    for (int k = 0; k < 24; k++) {
        float ang = k * 0.7853982f + (k / 8) * 0.39f, rr = (0.12f + 0.15f * (k / 8)) * (s_W < s_H ? s_W : s_H);
        int x = s_W / 2 + (int)(rr * fsin(ang)), y = s_H / 2 - (int)(rr * fcos(ang));
        if (k >= 16) {  // (and a grid for the wide frames)
            x = s_W * (2 * (k - 16) + 1) / 16;
            y = s_H * (k % 2 ? 3 : 1) / 4;
        }
        if ((unsigned)x >= (unsigned)s_W || (unsigned)y >= (unsigned)s_H || (y < gy1 && x >= gx0 && x < gx1) ||
            (x >= a[y] && x < b[y]) || (y - s_roll + s_H) % s_H < ROLL_ROWS) {
            continue;
        }
        if (g_fb[y * s_W + x] != s_bg[y * s_W + x]) {
            return true;
        }
    }
    return false;
}
static void frame_restore(int sl)
{
    int16_t *a = s_dx0[sl], *b = s_dx1[sl];
    if (!s_full[sl] && frame_changed(sl)) {
        s_full[sl] = 1;
    }
    if (s_full[sl]) {
        memcpy(g_fb, s_bg, (size_t)s_W * s_H * 2);
        s_full[sl] = 0;
    } else {
        // the grip of the settings panel at the top middle: the system draws it over the frame for some seconds
        int gx0 = s_W / 2 - 100, gx1 = s_W / 2 + 100, gy1 = s_H / 6 + 30;
        for (int y = 0; y < s_H; y++) {
            int x0 = a[y], x1 = b[y];
            if ((y - s_roll + s_H) % s_H < ROLL_ROWS) {
                x0 = 0;
                x1 = s_W;
            } else if (y < gy1) {
                x0 = x0 < gx0 ? x0 : gx0;
                x1 = x1 > gx1 ? x1 : gx1;
            }
            if (x1 > x0) {
                memcpy(g_fb + y * s_W + x0, s_bg + y * s_W + x0, (size_t)(x1 - x0) * 2);
            }
        }
        s_roll = (s_roll + ROLL_ROWS) % s_H;
    }
    for (int y = 0; y < s_H; y++) {
        a[y] = 32767;
        b[y] = -1;
    }
}
static void target_bg(int x0, int y0, int x1, int y1)
{
    s_tg = s_bg;
    s_mx0 = s_mx1 = NULL;
    s_cx0 = x0 < 0 ? 0 : x0;
    s_cy0 = y0 < 0 ? 0 : y0;
    s_cx1 = x1 > s_W ? s_W : x1;
    s_cy1 = y1 > s_H ? s_H : y1;
}
static void target_frame(int sl)
{
    s_tg = g_fb;
    s_mx0 = s_dx0[sl];
    s_mx1 = s_dx1[sl];
    clip_all();
}
