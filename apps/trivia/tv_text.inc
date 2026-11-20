// TRIVIA: the text (included by app_trivia.c). The Rubik faces of the firmware (kfont.h) drawn by the app: a Hebrew
// paragraph from the right, a number keeps its order, the words wrap into lines, and the largest face that fits the box
// is chosen. The app draws the glyphs itself (an alpha for the fades, a soft shadow).
#include "bidi/bidi.h"  // (the order of a Hebrew line, and the mirror of its brackets)

#define TX_MAX   160  // the code points of one text
#define TX_LINES 6

static inline uint16_t sw565(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
// a mix of the frame colour d (the frame order) and s (the frame order) at a = 0..255: the three fields in one word
// (0x07E0F81F), one multiply (alpha in 32 steps)
static inline uint16_t mix_fb(uint16_t d, uint16_t s, int a)
{
    const uint32_t a5 = (uint32_t)(a + 4) >> 3;
    const uint32_t dn = sw565(d), sn = sw565(s);
    const uint32_t bg = (dn | dn << 16) & 0x07E0F81Fu, fg = (sn | sn << 16) & 0x07E0F81Fu;
    const uint32_t r = (bg + (((fg - bg) * a5) >> 5)) & 0x07E0F81Fu;
    return sw565((uint16_t)(r | r >> 16));
}
static inline void px_a(int x, int y, uint16_t c, int a)
{
    if ((unsigned)x >= (unsigned)gfx_w || (unsigned)y >= (unsigned)gfx_h || a <= 0) {
        return;
    }
    uint16_t *p = &g_fb[y * gfx_w + x];
    *p = a >= 255 ? c : mix_fb(*p, c, a);
}

static int utf8_cp(const char *s, uint16_t *cp, int max)
{
    int n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && n < max) {
        uint32_t c = *p++;
        if (c >= 0xE0 && p[0] && p[1]) {
            c = (c & 15) << 12 | (p[0] & 63u) << 6 | (p[1] & 63u);
            p += 2;
        } else if (c >= 0xC0 && p[0]) {
            c = (c & 31) << 6 | (p[0] & 63u);
            p++;
        }
        cp[n++] = (uint16_t)c;
    }
    return n;
}
static const kglyph_t *kg(const kfont_t *f, uint16_t c)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        const int m = (lo + hi) / 2;
        if (f->g[m].cp == c) {
            return &f->g[m];
        }
        if (f->g[m].cp < c) {
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    return NULL;
}
static float kadv(const kfont_t *f, uint16_t c)
{
    const kglyph_t *g = kg(f, c);
    return g ? g->adv4 / 4.0f : (c == ' ' ? f->asc * 0.3f : 0);
}
static void kglyph_draw(const kfont_t *f, float x, int base, uint16_t c, uint16_t col, int alpha)
{
    const kglyph_t *g = kg(f, c);
    if (!g) {
        return;
    }
    const int gx = (int)(x + 0.5f) + g->ox, gy = base + g->oy;
    const uint8_t *src = f->a4 + g->off;
    for (int j = 0; j < g->h; j++) {
        const int y = gy + j;
        if ((unsigned)y >= (unsigned)gfx_h) {
            continue;
        }
        for (int k = 0; k < g->w; k++) {
            const int i = j * g->w + k, v = (i & 1) ? src[i >> 1] >> 4 : src[i >> 1] & 15;
            if (v) {
                px_a(gx + k, y, col, alpha >= 255 ? v * 17 : (v * 17 * alpha) >> 8);
            }
        }
    }
}

static inline bool is_heb(uint16_t c)
{
    return c >= 0x590 && c <= 0x5FF;
}
static float word_w(const kfont_t *f, const uint16_t *cp, int n)
{
    float w = 0;
    for (int i = 0; i < n; i++) {
        w += kadv(f, cp[i]);
    }
    return w;
}
// one word from its left end x (a text with no Hebrew; a Hebrew line goes through bidi.h in tx_draw)
static void word_draw(const kfont_t *f, const uint16_t *cp, int n, float x, int base, uint16_t col, int a)
{
    for (int i = 0; i < n; i++) {
        kglyph_draw(f, x, base, cp[i], col, a);
        x += kadv(f, cp[i]);
    }
}

// A laid-out text: the lines (the start and the count of the code points), their widths, the face.
typedef struct {
    uint16_t cp[TX_MAX];
    int n, nl;
    int ls[TX_LINES], ln[TX_LINES];
    float lw[TX_LINES];
    const kfont_t *f;
    bool rtl;
} tx_t;

// wrap the text in width w with face f; false when it needs more than maxl lines (or a word is wider than w)
static bool tx_wrap(tx_t *t, const kfont_t *f, int w, int maxl)
{
    t->f = f;
    t->nl = 0;
    const float sp = kadv(f, ' ');
    int i = 0;
    while (i < t->n) {
        while (i < t->n && t->cp[i] == ' ') {
            i++;
        }
        if (i >= t->n) {
            break;
        }
        if (t->nl >= maxl || t->nl >= TX_LINES) {
            return false;
        }
        const int start = i;
        float lw = 0;
        int end = i;
        while (end < t->n) {
            int j = end;
            while (j < t->n && t->cp[j] == ' ') {
                j++;
            }
            int e = j;
            while (e < t->n && t->cp[e] != ' ') {
                e++;
            }
            const float ww = word_w(f, t->cp + j, e - j), add = (end == start ? 0 : sp) + ww;
            if (end != start && lw + add > w) {
                break;
            }
            if (end == start && ww > w) {
                return false;
            }
            lw += add;
            end = e;
        }
        t->ls[t->nl] = start;
        t->ln[t->nl] = end - start;
        t->lw[t->nl] = lw;
        t->nl++;
        i = end;
    }
    return true;
}
// lay out utf8 in a box of w x h: the first face of fonts[] (big to small) whose lines fit; the last face cuts lines
static void tx_fit(tx_t *t, const char *utf8, const kfont_t *const *fonts, int nf, int w, int h)
{
    t->n = utf8_cp(utf8, t->cp, TX_MAX);
    t->rtl = false;
    for (int i = 0; i < t->n; i++) {
        if (is_heb(t->cp[i])) {
            t->rtl = true;
            break;
        }
    }
    for (int k = 0; k < nf; k++) {
        const int maxl = h / fonts[k]->line;
        if (maxl >= 1 && tx_wrap(t, fonts[k], w, maxl)) {
            return;
        }
    }
    tx_wrap(t, fonts[nf - 1], w, TX_LINES);
    const int maxl = h / fonts[nf - 1]->line;
    if (t->nl > maxl) {
        t->nl = maxl < 1 ? 1 : maxl;
    }
}
// the height of the lines
static inline int tx_h(const tx_t *t)
{
    return t->nl * t->f->line;
}
// draw the lines: cx = the centre of each line, y = the top of the first line
static void tx_draw(const tx_t *t, int cx, int y, uint16_t col, int a)
{
    const kfont_t *f = t->f;
    const float sp = kadv(f, ' ');
    const int lead = (f->line - f->asc) / 2;
    for (int l = 0; l < t->nl; l++) {
        const int base = y + l * f->line + f->asc + lead;
        const uint16_t *cp = t->cp + t->ls[l];
        const int n = t->ln[l];
        float xr = cx + t->lw[l] / 2;
        if (!t->rtl) {
            xr = cx - t->lw[l] / 2;
        }
        if (t->rtl) {
            // (a Hebrew line: the order of the Unicode Bidirectional Algorithm, libs/bidi/bidi.h: a number keeps its
            // order, and a bracket of a right-to-left part shows its mirror)
            int16_t ord[BIDI_MAX];
            uint8_t mir[BIDI_MAX];
            const int m = n < BIDI_MAX ? n : BIDI_MAX;
            bidi_order(cp, m, ord, mir);
            float x = cx - word_w(f, cp, m) / 2;  // (the drawn width: a double space counts too)
            for (int k = 0; k < m; k++) {
                const uint16_t c = mir[k] ? bidi_mirror(cp[ord[k]]) : cp[ord[k]];
                kglyph_draw(f, x, base, c, col, a);
                x += kadv(f, c);
            }
            continue;
        }
        int i = 0;
        while (i < n) {
            while (i < n && cp[i] == ' ') {
                i++;
            }
            int e = i;
            while (e < n && cp[e] != ' ') {
                e++;
            }
            if (e > i) {
                const float ww = word_w(f, cp + i, e - i);
                word_draw(f, cp + i, e - i, xr, base, col, a);
                xr += ww + sp;
            }
            i = e;
        }
    }
}
// the text with a soft dark shadow (1 px down and right)
static void tx_draw_sh(const tx_t *t, int cx, int y, uint16_t col, int a)
{
    tx_draw(t, cx + 1, y + 1, rgb(10, 6, 24), a * 3 / 4);
    tx_draw(t, cx, y, col, a);
}
// a text in a box, centred in it (both ways)
static void tx_box(const char *utf8, const kfont_t *const *fonts, int nf, int x, int y, int w, int h, uint16_t col,
                   int a)
{
    static tx_t t;
    tx_fit(&t, utf8, fonts, nf, w, h);
    tx_draw_sh(&t, x + w / 2, y + (h - tx_h(&t)) / 2, col, a);
}
// one line, centred at cx with the baseline base (the largest face of fonts[] whose width is w or less)
static void tx_line(const char *utf8, const kfont_t *const *fonts, int nf, int cx, int base, int w, uint16_t col, int a)
{
    static tx_t t;
    tx_fit(&t, utf8, fonts, nf, w, 1000);
    for (int k = 0; k < nf; k++) {
        if (tx_wrap(&t, fonts[k], w, 1)) {
            break;
        }
    }
    t.nl = 1;
    const int lead = (t.f->line - t.f->asc) / 2;
    tx_draw_sh(&t, cx, base - t.f->asc - lead, col, a);
}
