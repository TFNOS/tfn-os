// LINEAR ALGEBRA: the text and the mathematics. A small typesetter with the real fonts of la_font.c:
//
// THE MATHEMATICS (between $ $ in a text, or a display line that starts with '='):
//   a single Latin letter is italic; a run of letters that is a name (det, tr, rank ...) is upright
//   x^2  x^{n-1}  x_1  x_{ij}      the scripts (one character, or a group in braces)
//   @f{a}{b}   a fraction           @r{x}   a square root          @p{x}   parentheses that grow
//   @m{a&b;c&d}  a matrix ([ ])      @d{...}  a determinant (| |)   @n{x}   a norm (double bars)
//   @h{x} the accent colour (the part that changed)   @1{x} @2{x} @3{x} @4{x} the colours of the pictures
//   @g{x} grey   @t{text} upright text   @s a thin space   @q a wide space
// THE TEXT: words, *emphasis* (the accent colour), $math$. A line that starts with '#' is the heading of a step.
// A Hebrew paragraph goes from the right: the Hebrew words from the right, each run of Latin words and mathematics
// from the left inside its place (a light form of the Unicode rule), the brackets of a Hebrew word mirrored.

// ---------------------------------------------------------------- the faces and the colours
static const mfont_t *const SZ[6] = {&LA_F11, &LA_F13, &LA_F15, &LA_F17, &LA_F20, &LA_F24};
static const mfont_t *const SZB[5] = {&LA_B15, &LA_B18, &LA_B22, &LA_B28, &LA_B36};

static int s_tsz = 2;    // the text face (an index of SZ)
static int s_msz = 3;    // the face of the display mathematics
static int s_mbase;      // the face of level 0 of the mathematics now
static bool s_mdisp;     // true = a display line (the fractions stay big)
static bool s_rtl;       // the paragraph goes from the right
static uint16_t s_tcol;  // the colour of the text now
static int s_talpha = 255;
static bool s_overflow;  // a text did not fit its box (the PC test fails on it)

#define COL_TEXT rgb(228, 232, 242)
#define COL_DIM  rgb(150, 158, 182)
#define COL_ACC  rgb(255, 198, 96)
#define COL_C1   rgb(84, 204, 255)
#define COL_C2   rgb(255, 112, 172)
#define COL_C3   rgb(156, 232, 122)
#define COL_C4   rgb(176, 146, 255)
#define COL_GREY rgb(110, 118, 142)

static int s_clx0, s_cly0, s_clx1 = 4096, s_cly1 = 4096;  // the clip of the text and the lines of the mathematics
static void ty_clip(int x0, int y0, int x1, int y1)
{
    s_clx0 = x0 < 0 ? 0 : x0;
    s_cly0 = y0 < 0 ? 0 : y0;
    s_clx1 = x1 > gfx_w ? gfx_w : x1;
    s_cly1 = y1 > gfx_h ? gfx_h : y1;
    tfd_clip(s_clx0, s_cly0, s_clx1, s_cly1);
}
static void ty_noclip(void)
{
    ty_clip(0, 0, gfx_w, gfx_h);
}

// ---------------------------------------------------------------- the glyphs
static int utf8_next(const char **ps)
{
    const uint8_t *s = (const uint8_t *)*ps;
    int c = s[0];
    if (c < 0x80) {
        *ps += c ? 1 : 0;
        return c;
    }
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    int cp = c & (0x3F >> n);
    for (int i = 1; i <= n; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *ps += i;
            return '?';
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *ps += n + 1;
    return cp;
}
static const mglyph_t *ty_glyph(const mfont_t *f, int cp)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        int c = f->g[m].cp;
        if (c == cp) {
            return &f->g[m];
        }
        if (c < cp) {
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    return cp == '?' ? NULL : ty_glyph(f, '?');
}
// the advance of a glyph in quarter px
static inline int ty_adv(const mfont_t *f, int cp)
{
    const mglyph_t *g = ty_glyph(f, cp);
    return g ? g->adv4 : 0;
}
// a glyph at x4 (quarter px) on the baseline; returns its advance (quarter px)
static int ty_put(const mfont_t *f, int cp, int x4, int base, uint16_t col, int alpha)
{
    const mglyph_t *g = ty_glyph(f, cp);
    if (!g) {
        return 0;
    }
    if (alpha <= 0) {
        return g->adv4;
    }
    int gx = ((x4 + 2) >> 2) + g->ox, gy = base + g->oy;
    const uint8_t *src = f->a4 + g->off;
    for (int j = 0; j < g->h; j++) {
        int y = gy + j;
        if (y < s_cly0 || y >= s_cly1) {
            continue;
        }
        uint16_t *row = &g_fb[y * gfx_w];
        for (int k = 0; k < g->w; k++) {
            int px = gx + k;
            if (px < s_clx0 || px >= s_clx1) {
                continue;
            }
            int idx = j * g->w + k, v = (idx & 1) ? src[idx >> 1] >> 4 : src[idx >> 1] & 15;
            if (!v) {
                continue;
            }
            int a = v * 17 * alpha / 255;
            row[px] = a >= 255 ? col : tfd_mix(row[px], col, a + (a >> 7));
        }
    }
    return g->adv4;
}

// ---------------------------------------------------------------- the mathematics
typedef struct {
    int w;     // the width (quarter px)
    int a, d;  // over and under the baseline (px)
} bx_t;
enum {
    K_NONE,
    K_ORD,
    K_BIN,
    K_REL,
    K_PUN,
    K_OPEN,
    K_CLOSE
};

static inline const mfont_t *m_face(int lvl)
{
    int i = s_mbase - 2 * lvl;
    return SZ[i < 0 ? 0 : i];
}
static inline int m_em(int lvl)  // the size of the face (px)
{
    return m_face(lvl)->line * 10 / 13;
}
static inline int m_axis(int lvl)  // the axis of the fractions over the baseline
{
    return (m_face(lvl)->asc * 9 + 10) / 20;
}
static inline float m_lw(int lvl)  // the width of the lines of the mathematics
{
    float w = m_em(lvl) / 13.0f;
    return w < 1.0f ? 1.0f : w;
}
static void bx_grow(bx_t *b, int a, int d)
{
    if (a > b->a) {
        b->a = a;
    }
    if (d > b->d) {
        b->d = d;
    }
}
static int m_kind(int cp)
{
    switch (cp) {
    case '+':
    case 0x2212:  // −
    case 0xB7:    // ·
    case 0xD7:    // ×
    case 0xB1:    // ±
    case 0x2297:  // ⊗
    case 0x2295:  // ⊕
    case 0x222A:  // ∪
        return K_BIN;
    case '=':
    case '<':
    case '>':
    case 0x2264:  // ≤
    case 0x2265:  // ≥
    case 0x2248:  // ≈
    case 0x2260:  // ≠
    case 0x2192:  // →
    case 0x21D2:  // ⇒
    case 0x2208:  // ∈
    case 0x2194:  // ↔
    case 0x22A5:  // ⊥
        return K_REL;
    case ':':
    case ',':
    case ';':
        return K_PUN;
    case '(':
    case '[':
    case 0x27E8:
        return K_OPEN;
    case ')':
    case ']':
    case 0x27E9:
        return K_CLOSE;
    default:
        return K_ORD;
    }
}
// the space before an item of kind k after an item of kind p (quarter px)
static int m_space(int p, int *k, int lvl)
{
    int em4 = m_em(lvl) * 4;
    if (*k == K_BIN && (p == K_NONE || p == K_BIN || p == K_REL || p == K_OPEN || p == K_PUN)) {
        *k = K_ORD;  // a sign at the start: no space
    }
    if (lvl > 0) {
        return 0;
    }
    if (*k == K_REL || (p == K_REL && *k != K_PUN && *k != K_CLOSE)) {
        return p == K_NONE ? 0 : em4 * 5 / 18;
    }
    if (*k == K_BIN || p == K_BIN) {
        return em4 * 4 / 18;
    }
    if (p == K_PUN) {
        return em4 * 3 / 18;
    }
    return 0;
}

static const char *const NAMES[] = {"det",  "tr", "rank", "ker", "dim", "cos", "sin", "tan", "max", "min", "diag",
                                    "span", "im", "Re",   "Im",  "exp", "log", "arg", "sgn", "adj", "lim"};

static bx_t m_list(const char **ps, int lvl, int x4, int base, bool draw, uint16_t col);

// a group: {...} or one item
static bx_t m_arg(const char **ps, int lvl, int x4, int base, bool draw, uint16_t col)
{
    if (**ps == '{') {
        (*ps)++;
        bx_t b = m_list(ps, lvl, x4, base, draw, col);
        if (**ps == '}') {
            (*ps)++;
        }
        return b;
    }
    bx_t b = {0, 0, 0};
    if (!**ps) {
        return b;
    }
    const mfont_t *f = m_face(lvl);
    int cp = utf8_next(ps);
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
        cp += 0xE000;
    }
    if (cp == '-') {
        cp = 0x2212;
    }
    const mglyph_t *g = ty_glyph(f, cp);
    if (g) {
        b.w = draw ? ty_put(f, cp, x4, base, col, s_talpha) : g->adv4;
        bx_grow(&b, -g->oy, g->oy + g->h);
    }
    return b;
}
// skip a group (to measure it again later)
static void m_skip(const char **ps)
{
    if (**ps != '{') {
        if (**ps) {
            utf8_next(ps);
        }
        return;
    }
    int depth = 0;
    do {
        if (**ps == '{') {
            depth++;
        } else if (**ps == '}') {
            depth--;
        }
        (*ps)++;
    } while (**ps && depth > 0);
}
// a vertical bracket from y0 to y1 at x (kind: '[' ']' '|' 'N' (a norm: two bars) '(' ')')
static void m_bracket(int kind, float x, int y0, int y1, int lvl, uint16_t col)
{
    float lw = m_lw(lvl);
    float ser = m_em(lvl) * 0.28f;
    int a = s_talpha;
    if (kind == '[' || kind == ']') {
        float s = kind == '[' ? 1 : -1;
        tfd_line(x, y0, x, y1, lw, col, a);
        tfd_line(x, y0, x + s * ser, y0, lw, col, a);
        tfd_line(x, y1, x + s * ser, y1, lw, col, a);
    } else if (kind == '|') {
        tfd_line(x, y0, x, y1, lw, col, a);
    } else if (kind == 'N') {
        tfd_line(x - lw, y0, x - lw, y1, lw, col, a);
        tfd_line(x + lw, y0, x + lw, y1, lw, col, a);
    } else {
        // a parenthesis: an arc of an ellipse in 10 parts
        float s = kind == '(' ? 1 : -1, h = (y1 - y0) * 0.5f, cy = (y0 + y1) * 0.5f, bulge = ser * 1.1f;
        float px = x + s * bulge, py = y0;
        for (int i = 1; i <= 10; i++) {
            float t = -1.0f + i * 0.2f;
            float nx = x + s * bulge * (t * t), ny = cy + t * h;
            tfd_line(px, py, nx, ny, lw, col, a);
            px = nx;
            py = ny;
        }
    }
}
// a matrix (bars: 0 [ ], 1 | |); the cells are separated by '&', the rows by ';'
static bx_t m_matrix(const char **ps, int lvl, int x4, int base, bool draw, uint16_t col, int bars)
{
    enum {
        MR = 8,
        MC = 8
    };
    // (static: the stack of the app task is small; a matrix is never inside a cell of a matrix)
    static const char *cell[MR][MC];
    static bx_t cb[MR][MC];
    int nr = 0, nc = 0, c = 0;
    int cw[MC] = {0}, ra[MR] = {0}, rd[MR] = {0};
    int clvl = s_mdisp ? lvl : lvl + 1;
    const mfont_t *f = m_face(clvl);
    if (**ps == '{') {
        (*ps)++;
    }
    for (;;) {
        if (nr < MR && c < MC) {
            cell[nr][c] = *ps;
        }
        bx_t b = m_list(ps, clvl, 0, 0, false, col);
        if (nr < MR && c < MC) {
            cb[nr][c] = b;
            if (b.w > cw[c]) {
                cw[c] = b.w;
            }
            if (b.a > ra[nr]) {
                ra[nr] = b.a;
            }
            if (b.d > rd[nr]) {
                rd[nr] = b.d;
            }
        }
        c++;
        if (c > nc) {
            nc = c;
        }
        char t = **ps;
        if (t) {
            (*ps)++;
        }
        if (t == ';') {
            nr++;
            c = 0;
        } else if (t != '&') {
            nr++;
            break;
        }
    }
    if (nr > MR) {
        nr = MR;
    }
    if (nc > MC) {
        nc = MC;
    }
    int asc = f->asc, gap = m_em(clvl) / 3 + 1;
    int H = 0;
    for (int r = 0; r < nr; r++) {
        if (ra[r] < asc) {
            ra[r] = asc;
        }
        if (rd[r] < asc / 3) {
            rd[r] = asc / 3;
        }
        H += ra[r] + rd[r] + (r ? gap : 0);
    }
    int colgap4 = m_em(clvl) * 4 * 7 / 10, pad4 = m_em(lvl) * 4 * 4 / 10;
    int W = pad4 * 2;
    for (int k = 0; k < nc; k++) {
        W += cw[k] + (k ? colgap4 : 0);
    }
    int axis = m_axis(lvl);
    int top = base - axis - H / 2 - 2, bot = top + H + 4;
    bx_t out = {W + m_em(lvl) * 4 / 3, base - top, bot - base};
    if (!draw) {
        return out;
    }
    int x0 = x4 + m_em(lvl) * 4 / 6;  // (a small space on each side)
    if (bars == 1) {
        m_bracket('|', (x0 >> 2) + 1.0f, top, bot, lvl, col);
        m_bracket('|', ((x0 + W) >> 2) - 1.0f, top, bot, lvl, col);
    } else {
        m_bracket('[', (x0 >> 2) + 1.0f, top, bot, lvl, col);
        m_bracket(']', ((x0 + W) >> 2) - 1.0f, top, bot, lvl, col);
    }
    int y = top + 2;
    for (int r = 0; r < nr; r++) {
        int bl = y + ra[r];
        int cx4 = x0 + pad4;
        for (int k = 0; k < nc; k++) {
            if (k < MC && r < MR && cell[r][k]) {
                const char *p = cell[r][k];
                m_list(&p, clvl, cx4 + (cw[k] - cb[r][k].w) / 2, bl, true, col);
            }
            cx4 += cw[k] + colgap4;
        }
        y = bl + rd[r] + gap;
    }
    return out;
}
// the scripts after an item (^ and _ in any order); x4 = the end of the item, ia = its height
static int m_scripts(const char **ps, int lvl, int x4, int base, bool draw, uint16_t col, int ia, bx_t *bb)
{
    const char *sup = NULL, *sub = NULL;
    while (**ps == '^' || **ps == '_') {
        char t = **ps;
        (*ps)++;
        if (t == '^') {
            sup = *ps;
        } else {
            sub = *ps;
        }
        m_skip(ps);
    }
    if (!sup && !sub) {
        return 0;
    }
    const mfont_t *f = m_face(lvl);
    int w = 0;
    if (sup) {
        const char *p = sup;
        bx_t s = m_arg(&p, lvl + 1, 0, 0, false, col);
        int up = ia - f->asc / 3;
        if (up < f->asc * 5 / 9) {
            up = f->asc * 5 / 9;
        }
        if (draw) {
            p = sup;
            m_arg(&p, lvl + 1, x4 + 2, base - up, true, col);
        }
        bx_grow(bb, up + s.a, 0);
        w = s.w + 2;
    }
    if (sub) {
        const char *p = sub;
        bx_t s = m_arg(&p, lvl + 1, 0, 0, false, col);
        int dn = f->asc * 2 / 7 + 1;
        if (draw) {
            p = sub;
            m_arg(&p, lvl + 1, x4 + 1, base + dn, true, col);
        }
        bx_grow(bb, 0, dn + s.d);
        if (s.w + 1 > w) {
            w = s.w + 1;
        }
    }
    return w + 2;
}
// one command after '@'; returns the box, *kind = its kind for the spaces
static bx_t m_cmd(const char **ps, int lvl, int x4, int base, bool draw, uint16_t col, int *kind)
{
    char c = **ps;
    (*ps)++;
    bx_t b = {0, 0, 0};
    *kind = K_ORD;
    const mfont_t *f = m_face(lvl);
    switch (c) {
    case 'f': {  // a fraction
        int fl = s_mdisp && lvl == 0 ? lvl : lvl + 1;
        const char *pn = *ps;
        bx_t n = m_arg(ps, fl, 0, 0, false, col);
        const char *pd = *ps;
        bx_t d = m_arg(ps, fl, 0, 0, false, col);
        int W = (n.w > d.w ? n.w : d.w) + 8, axis = m_axis(lvl), gap = m_em(lvl) / 6 + 1;
        float lw = m_lw(lvl);
        b.w = W + 4;
        b.a = axis + gap + n.d + n.a + 1;
        b.d = -axis + gap + d.a + d.d + 1;
        if (draw) {
            float yb = base - axis;
            tfd_line((x4 + 6) / 4.0f, yb, (x4 + W) / 4.0f, yb, lw, col, s_talpha);
            m_arg(&pn, fl, x4 + 2 + (W - n.w) / 2, (int)(yb - gap - n.d - lw / 2), true, col);
            m_arg(&pd, fl, x4 + 2 + (W - d.w) / 2, (int)(yb + gap + d.a + lw / 2 + 1), true, col);
        }
        return b;
    }
    case 'r': {  // a square root
        const char *p0 = *ps;
        bx_t in = m_arg(ps, lvl, 0, 0, false, col);
        int em = m_em(lvl), hook = em * 4 * 6 / 10;
        float lw = m_lw(lvl);
        int top = in.a + 3, bot = in.d;
        if (top < f->asc + 3) {
            top = f->asc + 3;
        }
        b.w = hook + in.w + 6;
        b.a = top + 1;
        b.d = bot + 1;
        if (draw) {
            float x = x4 / 4.0f, h = em * 0.6f;
            float yt = base - top, yb = base + bot;
            tfd_line(x + 1, base - h * 0.45f, x + h * 0.22f, base - h * 0.55f, lw, col, s_talpha);
            tfd_line(x + h * 0.22f, base - h * 0.55f, x + h * 0.5f, yb, lw * 1.3f, col, s_talpha);
            tfd_line(x + h * 0.5f, yb, x + hook / 4.0f, yt, lw, col, s_talpha);
            tfd_line(x + hook / 4.0f, yt, x + (hook + in.w + 4) / 4.0f, yt, lw, col, s_talpha);
            m_arg(&p0, lvl, x4 + hook + 2, base, true, col);
        }
        return b;
    }
    case 'm':
    case 'd':
        *kind = K_ORD;
        return m_matrix(ps, lvl, x4, base, draw, col, c == 'd');
    case 'p':
    case 'n': {  // parentheses or a norm that grow
        const char *p0 = *ps;
        bx_t in = m_arg(ps, lvl, 0, 0, false, col);
        int em = m_em(lvl), side = em * 4 * 4 / 10;
        int a = in.a > f->asc + 1 ? in.a + 1 : f->asc + 2, d = in.d > f->asc / 3 ? in.d + 1 : f->asc / 3 + 1;
        b.w = side * 2 + in.w;
        b.a = a;
        b.d = d;
        if (draw) {
            int y0 = base - a, y1 = base + d;
            if (c == 'n') {
                m_bracket('N', (x4 + side / 2) / 4.0f, y0, y1, lvl, col);
                m_bracket('N', (x4 + side + in.w + side / 2) / 4.0f, y0, y1, lvl, col);
            } else {
                m_bracket('(', (x4 + side / 3) / 4.0f, y0, y1, lvl, col);
                m_bracket(')', (x4 + side + in.w + side * 2 / 3) / 4.0f, y0, y1, lvl, col);
            }
            m_arg(&p0, lvl, x4 + side, base, true, col);
        }
        return b;
    }
    case 'h':
    case '1':
    case '2':
    case '3':
    case '4':
    case 'g': {
        uint16_t k = c == 'h'   ? COL_ACC
                     : c == '1' ? COL_C1
                     : c == '2' ? COL_C2
                     : c == '3' ? COL_C3
                     : c == '4' ? COL_C4
                                : COL_GREY;
        return m_arg(ps, lvl, x4, base, draw, k);
    }
    case 't': {  // upright text
        if (**ps == '{') {
            (*ps)++;
        }
        while (**ps && **ps != '}') {
            int cp = utf8_next(ps);
            const mglyph_t *g = ty_glyph(f, cp);
            if (g) {
                if (draw) {
                    ty_put(f, cp, x4 + b.w, base, col, s_talpha);
                }
                b.w += g->adv4;
                bx_grow(&b, -g->oy, g->oy + g->h);
            }
        }
        if (**ps == '}') {
            (*ps)++;
        }
        return b;
    }
    case '{':
    case '}': {  // a brace as a sign (a set)
        const mglyph_t *g = ty_glyph(f, c);
        if (g) {
            if (draw) {
                ty_put(f, c, x4, base, col, s_talpha);
            }
            b.w = g->adv4;
            bx_grow(&b, -g->oy, g->oy + g->h);
        }
        *kind = c == '{' ? K_OPEN : K_CLOSE;
        return b;
    }
    case 's':
        b.w = m_em(lvl) * 4 / 5;
        return b;
    case 'q':
        b.w = m_em(lvl) * 4;
        return b;
    default:
        return b;
    }
}
// a list of items to '}', '&', ';' or the end
static bx_t m_list(const char **ps, int lvl, int x4, int base, bool draw, uint16_t col)
{
    bx_t L = {0, 0, 0};
    int prev = K_NONE;
    const mfont_t *f = m_face(lvl);
    while (**ps && **ps != '}' && **ps != '&' && **ps != ';' && **ps != '$') {
        char c = **ps;
        if (c == ' ') {
            (*ps)++;
            continue;
        }
        bx_t b = {0, 0, 0};
        int kind = K_ORD;
        if (c == '{') {
            const char *p0 = *ps;
            b = m_arg(ps, lvl, 0, base, false, col);
            L.w += m_space(prev, &kind, lvl);
            if (draw) {
                const char *p = p0;
                m_arg(&p, lvl, x4 + L.w, base, true, col);
            }
        } else if (c == '@') {
            (*ps)++;
            const char *p0 = *ps;
            bx_t m = m_cmd(ps, lvl, 0, base, false, col, &kind);
            int sp = m_space(prev, &kind, lvl);
            if (draw) {
                const char *p = p0;
                m_cmd(&p, lvl, x4 + L.w + sp, base, true, col, &kind);
            }
            b = m;
            L.w += sp;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            // a run of letters: a name (upright) or italic letters
            const char *s = *ps;
            int n = 0;
            while ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z')) {
                n++;
            }
            bool name = false;
            for (unsigned k = 0; n > 1 && k < sizeof NAMES / sizeof NAMES[0]; k++) {
                if ((int)strlen(NAMES[k]) == n && !strncmp(NAMES[k], s, n)) {
                    name = true;
                }
            }
            int sp = m_space(prev, &kind, lvl);
            L.w += sp;
            if (name) {
                for (int i = 0; i < n; i++) {
                    const mglyph_t *g = ty_glyph(f, s[i]);
                    if (g) {
                        if (draw) {
                            ty_put(f, s[i], x4 + L.w + b.w, base, col, s_talpha);
                        }
                        b.w += g->adv4;
                        bx_grow(&b, -g->oy, g->oy + g->h);
                    }
                }
                b.w += m_em(lvl) * 4 / 6;  // (a thin space after a name)
                *ps += n;
            } else {
                // one italic letter (the next letters come in the next rounds of the loop)
                const mglyph_t *g = ty_glyph(f, 0xE000 + s[0]);
                if (g) {
                    if (draw) {
                        ty_put(f, 0xE000 + s[0], x4 + L.w, base, col, s_talpha);
                    }
                    b.w = g->adv4;
                    bx_grow(&b, -g->oy, g->oy + g->h);
                }
                (*ps)++;
            }
        } else {
            int cp = utf8_next(ps);
            if (cp == '-') {
                cp = 0x2212;
            }
            if (cp == '*') {
                cp = 0xB7;
            }
            kind = m_kind(cp);
            int sp = m_space(prev, &kind, lvl);
            L.w += sp;
            const mglyph_t *g = ty_glyph(f, cp);
            if (g) {
                if (draw) {
                    ty_put(f, cp, x4 + L.w, base, col, s_talpha);
                }
                b.w = g->adv4;
                bx_grow(&b, -g->oy, g->oy + g->h);
            }
        }
        bx_grow(&L, b.a, b.d);
        L.w += b.w;
        // the scripts of this item
        bx_t sb = {0, 0, 0};
        int sw = m_scripts(ps, lvl, x4 + L.w, base, draw, col, b.a, &sb);
        if (sw) {
            L.w += sw;
            bx_grow(&L, sb.a, sb.d);
        }
        prev = kind;
    }
    return L;
}
// a whole formula: its box (lvl 0 at the face s_mbase)
static bx_t m_measure(const char *s)
{
    const char *p = s;
    return m_list(&p, 0, 0, 0, false, s_tcol);
}
static void m_draw(const char *s, int x4, int base)
{
    const char *p = s;
    m_list(&p, 0, x4, base, true, s_tcol);
}
