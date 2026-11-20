// INTEGRALS: the drawing helpers: the glyphs of the 4-bit atlases with alpha and a clip box, the background, the
// colours. s_fade (0..255) is the alpha of all the content of a screen (the fade between two screens).

static int s_fade = 255;
static int s_cx0, s_cy0, s_cx1 = 4096, s_cy1 = 4096;  // the clip box of the glyphs

static inline void clip_set(int x0, int y0, int x1, int y1)
{
    s_cx0 = x0 < 0 ? 0 : x0;
    s_cy0 = y0 < 0 ? 0 : y0;
    s_cx1 = x1 > gfx_w ? gfx_w : x1;
    s_cy1 = y1 > gfx_h ? gfx_h : y1;
    tfd_clip(s_cx0, s_cy0, s_cx1, s_cy1);
}
static inline void clip_off(void)
{
    clip_set(0, 0, gfx_w, gfx_h);
}
static inline int fa(int a)  // an alpha with the fade of the screen
{
    return a * s_fade / 255;
}

// one glyph of a 4-bit atlas (mglyph_t and kglyph_t have the same fields): x = the pen, base = the baseline
static void glyph_a4(const uint8_t *a4, uint32_t off, int gx, int gy, int w, int h, uint16_t col, int alpha)
{
    if (alpha <= 0 || w <= 0 || h <= 0) {
        return;
    }
    if (gx >= s_cx1 || gy >= s_cy1 || gx + w <= s_cx0 || gy + h <= s_cy0) {
        return;
    }
    const uint8_t *p = a4 + off;
    uint8_t lut[16];  // the alpha of each value of a nibble
    for (int v = 0; v < 16; v++) {
        lut[v] = (uint8_t)(v * 17 * alpha / 255);
    }
    const int i0 = gx < s_cx0 ? s_cx0 - gx : 0, i1 = gx + w > s_cx1 ? s_cx1 - gx : w;
    const int j0 = gy < s_cy0 ? s_cy0 - gy : 0, j1 = gy + h > s_cy1 ? s_cy1 - gy : h;
    for (int j = j0; j < j1; j++) {
        uint16_t *row = g_fb + (gy + j) * gfx_w + gx;
        int k = j * w + i0;
        for (int i = i0; i < i1; i++, k++) {
            const int nib = (p[k >> 1] >> ((k & 1) * 4)) & 15;
            if (!nib) {
                continue;
            }
            const int aa = lut[nib];
            row[i] = aa >= 255 ? col : tfd_mix(row[i], col, aa);
        }
    }
}

static inline void glyph_m(const mfont_t *f, const mglyph_t *g, int x, int base, uint16_t col, int alpha)
{
    glyph_a4(f->a4, g->off, x + g->ox, base + g->oy, g->w, g->h, col, alpha);
}

// UTF-8: the next code point
static int utf8_next(const char **ps)
{
    const uint8_t *s = (const uint8_t *)*ps;
    int c = *s++;
    if (c >= 0xE0 && s[0] && s[1]) {
        c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63);
        s += 2;
    } else if (c >= 0xC0 && s[0]) {
        c = ((c & 31) << 6) | (s[0] & 63);
        s += 1;
    }
    *ps = (const char *)s;
    return c;
}
static inline bool is_heb(int c)
{
    return c >= 0x590 && c <= 0x5FF;
}

static const kglyph_t *kf_glyph(const kfont_t *f, int cp)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        const int m = (lo + hi) / 2;
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

// the width of one word of the KF face (px * 4)
static int kf_w4(const kfont_t *f, const char *s, int n)
{
    int w4 = 0;
    const char *e = s + n;
    while (s < e && *s) {
        const kglyph_t *g = kf_glyph(f, utf8_next(&s));
        if (g) {
            w4 += g->adv4;
        }
    }
    return w4;
}

static int mirror_cp(int c)
{
    switch (c) {
    case '(':
        return ')';
    case ')':
        return '(';
    case '[':
        return ']';
    case ']':
        return '[';
    case '{':
        return '}';
    case '}':
        return '{';
    default:
        return c;
    }
}

// one word of the KF face from its left end x (a Hebrew word: its letters from the right, the brackets mirrored)
static void kf_word(const kfont_t *f, int x, int base, const char *s, int n, uint16_t col, int alpha)
{
    int cps[48], nc = 0;
    bool heb = false;
    const char *e = s + n;
    while (s < e && *s && nc < 48) {
        const int c = utf8_next(&s);
        heb |= is_heb(c);
        cps[nc++] = c;
    }
    int x4 = x * 4;
    for (int i = 0; i < nc; i++) {
        const int c = heb ? mirror_cp(cps[nc - 1 - i]) : cps[i];
        const kglyph_t *g = kf_glyph(f, c);
        if (!g) {
            continue;
        }
        glyph_a4(f->a4, g->off, (x4 >> 2) + g->ox, base + g->oy, g->w, g->h, col, alpha);
        x4 += g->adv4;
    }
}

static inline int kf_width(const kfont_t *f, const char *s)
{
    return (kf_w4(f, s, (int)strlen(s)) + 3) / 4;
}
// one line (one word or more, no wrap) with its centre at cx; a Hebrew line: its words from the right
static void kf_line_c(const kfont_t *f, int cx, int base, const char *s, uint16_t col, int alpha)
{
    bool heb = false;
    for (const char *q = s; *q;) {
        heb |= is_heb(utf8_next(&q));
    }
    const int sp4 = kf_w4(f, " ", 1);
    const int w = kf_width(f, s);
    int x4 = (cx - w / 2) * 4;
    if (!heb) {
        kf_word(f, x4 >> 2, base, s, (int)strlen(s), col, alpha);
        return;
    }
    // the words from the right end: the first word is at the right
    int xr4 = x4 + w * 4;
    const char *p = s;
    while (*p) {
        const char *q = p;
        while (*q && *q != ' ') {
            q++;
        }
        const int ww4 = kf_w4(f, p, (int)(q - p));
        xr4 -= ww4;
        kf_word(f, xr4 >> 2, base, p, (int)(q - p), col, alpha);
        xr4 -= sp4;
        p = *q ? q + 1 : q;
    }
}

// ---------------------------------------------------------------- the colours
#define C_BG0  rgb(13, 18, 34)
#define C_BG1  rgb(6, 9, 19)
#define C_INK  rgb(244, 236, 220)
#define C_HL   rgb(255, 196, 92)
#define C_DIM  rgb(120, 132, 160)
#define C_TXT  rgb(176, 188, 212)
#define C_SUB  rgb(122, 136, 168)
#define C_CARD rgb(24, 31, 54)
#define C_LINE rgb(44, 54, 86)

// the hue of each section (0 derivatives and basics, 1 the techniques, 2 the tricks, 3 the classics)
static const uint8_t SEC_RGB[4][3] = {{92, 214, 196}, {255, 186, 92}, {176, 140, 255}, {255, 118, 150}};
static inline uint16_t sec_col(int sec, int v)
{
    const uint8_t *c = SEC_RGB[sec & 3];
    return rgb((uint8_t)(c[0] * v / 255), (uint8_t)(c[1] * v / 255), (uint8_t)(c[2] * v / 255));
}

// ---------------------------------------------------------------- the background (made once for the frame size)
static uint16_t *s_bg;
static int s_bg_w, s_bg_h;
static void bg_make(void)
{
    if (s_bg && s_bg_w == gfx_w && s_bg_h == gfx_h) {
        return;
    }
    heap_caps_free(s_bg);
    s_bg = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_bg_w = gfx_w;
    s_bg_h = gfx_h;
    if (!s_bg) {
        return;
    }
    tfd_target(s_bg, gfx_w, gfx_h);
    tfd_noclip();
    for (int y = 0; y < gfx_h; y++) {
        const int t = y * 255 / (gfx_h - 1);
        const uint16_t c = ui_mix(C_BG0, C_BG1, t);
        for (int x = 0; x < gfx_w; x++) {
            s_bg[y * gfx_w + x] = c;
        }
    }
    // the paper of a graph: a soft grid of dots, and a glow at the top left
    tfd_glow(gfx_w * 0.18f, -gfx_h * 0.1f, gfx_h * 1.1f, rgb(40, 60, 120), 70);
    const int step = gfx_h > 300 ? 24 : 20;
    for (int y = step / 2; y < gfx_h; y += step) {
        for (int x = step / 2; x < gfx_w; x += step) {
            tfd_px(x, y, rgb(70, 86, 130), 46);
        }
    }
    tfd_screen();
}
static void bg_draw(void)
{
    if (s_bg) {
        memcpy(g_fb, s_bg, (size_t)gfx_w * gfx_h * 2);
    } else {
        gfx_fill(C_BG1);
    }
}
