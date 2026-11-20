// INTEGRALS: the math typesetting engine. It reads the markup of tools/igm.py (a small part of TeX) and makes a
// list of draw items (glyphs of the STIX faces, tall glyphs, rules, radical signs, highlight boxes) in a box.
// The layout follows the rules of TeX in a short form: 4 styles (display, text, script, scriptscript), the classes
// of the atoms and their spaces, fractions on the math axis, scripts, big operators with limits, brackets that grow,
// and lines that break before a relation (aligned at the first relation) or before a + or a -.

enum {
    TS_D,
    TS_T,
    TS_S,
    TS_SS
};
enum {
    AC_ORD,
    AC_OP,
    AC_BIN,
    AC_REL,
    AC_OPEN,
    AC_CLOSE,
    AC_PUNCT,
    AC_INNER,
    AC_NONE
};
enum {
    TI_GLY,
    TI_TALL,
    TI_RULE,
    TI_RAD,
    TI_HL
};
enum {
    TC_INK,
    TC_HL,
    TC_DIM
};

typedef struct {
    int16_t x, y, w, h;
    uint16_t g;  // TI_GLY: the index of the glyph in its face; TI_TALL: the index in IG_TALL
    uint8_t kind, cls, font, pad;
} tit_t;
typedef struct {
    int w, a, d;
} tbox_t;
typedef struct {
    tbox_t b;
    int i0, i1;   // the items of the atom (at its own origin: x 0, baseline 0)
    uint8_t cls;  // AC_*
    int glue;     // a space of the markup after it (px)
} tatom_t;

#define TIT_MAX 2600
typedef struct {
    tit_t it[TIT_MAX];
    int n;
} tarena_t;
static tarena_t *s_ar;  // the arena of the layout now

static const int EMS[IGF_LEVELS] = IGF_EM_LIST;
static int s_base;            // the level of the text and display styles
static int s_tcls;            // the colour class of the items now (\hl, \dim)
static const char *s_tp;      // the cursor in the markup
static bool s_tfail;          // the arena is full or the markup is bad
static bool s_tbreak = true;  // a long line may break into rows

static inline int lv_of(int sty)
{
    int l = s_base + (sty >= TS_SS ? 4 : sty >= TS_S ? 2 : 0);
    return l > IGF_LEVELS - 1 ? IGF_LEVELS - 1 : l;
}
static inline int em_of(int sty)
{
    return EMS[lv_of(sty)];
}
static inline int axis_of(int sty)
{
    return (em_of(sty) * 26 + 50) / 100;
}
static inline int rule_of(int sty)
{
    const int r = (em_of(sty) * 6 + 50) / 100;
    return r < 1 ? 1 : r;
}

static int mg_index(const mfont_t *f, int cp)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        const int m = (lo + hi) / 2;
        if (f->g[m].cp == cp) {
            return m;
        }
        if (f->g[m].cp < cp) {
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    return -1;
}
static inline const mfont_t *face_of(int font)
{
    return font >= IGF_LEVELS ? &IGF_I[font - IGF_LEVELS] : &IGF_R[font];
}

static tit_t *it_new(int kind)
{
    if (s_ar->n >= TIT_MAX) {
        s_tfail = true;
        return NULL;
    }
    tit_t *t = &s_ar->it[s_ar->n++];
    memset(t, 0, sizeof *t);
    t->kind = (uint8_t)kind;
    t->cls = (uint8_t)s_tcls;
    return t;
}
static void it_move(int i0, int i1, int dx, int dy)
{
    for (int i = i0; i < i1; i++) {
        s_ar->it[i].x = (int16_t)(s_ar->it[i].x + dx);
        s_ar->it[i].y = (int16_t)(s_ar->it[i].y + dy);
    }
}

// one glyph at the origin; italic: the italic face
static tbox_t t_glyph(int cp, bool italic, int sty)
{
    tbox_t b = {0, 0, 0};
    const int font = lv_of(sty) + (italic ? IGF_LEVELS : 0);
    const mfont_t *f = face_of(font);
    int gi = mg_index(f, cp);
    if (gi < 0 && italic) {
        return t_glyph(cp, false, sty);
    }
    if (gi < 0) {
        gi = mg_index(f, '?');
        if (gi < 0) {
            return b;
        }
    }
    const mglyph_t *g = &f->g[gi];
    tit_t *t = it_new(TI_GLY);
    if (t) {
        t->g = (uint16_t)gi;
        t->font = (uint8_t)font;
    }
    b.w = (g->adv4 + 2) / 4;
    b.a = -g->oy;
    b.d = g->oy + g->h;
    if (b.a < 0) {
        b.a = 0;
    }
    if (b.d < 0) {
        b.d = 0;
    }
    return b;
}

// a tall glyph of the kind, at least h px high, centred on the axis: x 0, the baseline 0
static tbox_t t_tall(int kind, int h, int sty)
{
    int best = -1;
    for (int i = 0; i < IG_TALL_N; i++) {
        if (IG_TALL[i].kind != kind) {
            continue;
        }
        if (best < 0 || (IG_TALL[best].h < h && IG_TALL[i].h > IG_TALL[best].h) ||
            (IG_TALL[i].h >= h && IG_TALL[i].h < IG_TALL[best].h)) {
            best = i;
        }
    }
    tbox_t b = {0, 0, 0};
    if (best < 0) {
        return b;
    }
    const ig_tall_t *g = &IG_TALL[best];
    const int ax = axis_of(sty);
    tit_t *t = it_new(TI_TALL);
    if (t) {
        t->g = (uint16_t)best;
        t->x = (int16_t)(em_of(sty) / 16);
        t->y = (int16_t)(-ax - g->h / 2);
        t->w = g->w;
        t->h = g->h;
    }
    b.w = g->w + em_of(sty) / 8;
    b.a = ax + g->h / 2;
    b.d = g->h - b.a;
    return b;
}

static void t_rule(int x, int y, int w, int h)
{
    tit_t *t = it_new(TI_RULE);
    if (t) {
        t->x = (int16_t)x;
        t->y = (int16_t)y;
        t->w = (int16_t)w;
        t->h = (int16_t)h;
    }
}

// ---------------------------------------------------------------- the parser
static inline void t_skip(void)
{
    while (*s_tp == ' ') {
        s_tp++;
    }
}
static bool t_cmd_is(const char *name)
{
    const size_t n = strlen(name);
    if (s_tp[0] != '\\' || strncmp(s_tp + 1, name, n)) {
        return false;
    }
    const char c = s_tp[1 + n];
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}
// the name of a command at the cursor (no move); len 0 = no command of letters
static int t_cmd_len(void)
{
    if (*s_tp != '\\') {
        return 0;
    }
    int n = 0;
    while ((s_tp[1 + n] >= 'a' && s_tp[1 + n] <= 'z') || (s_tp[1 + n] >= 'A' && s_tp[1 + n] <= 'Z')) {
        n++;
    }
    return n;
}

static tbox_t t_list(int sty, char stop, tatom_t *out, int *nout, int max);
static tbox_t t_pack(tatom_t *at, int n, int sty);

// a group {...} as one box (at the origin)
static tbox_t t_group(int sty)
{
    t_skip();
    if (*s_tp != '{') {
        tatom_t one[1];
        int n = 0;
        // one token: a digit run, a letter or one command
        const char *save = s_tp;
        (void)save;
        tbox_t b = t_list(sty, 1, one, &n, 1);
        (void)b;
        return n ? t_pack(one, n, sty) : (tbox_t){0, 0, 0};
    }
    s_tp++;
    tatom_t at[64];
    int n = 0;
    t_list(sty, '}', at, &n, 64);
    if (*s_tp == '}') {
        s_tp++;
    }
    return t_pack(at, n, sty);
}

static inline int sty_sup(int sty)
{
    return sty <= TS_T ? TS_S : TS_SS;
}
static inline int sty_frac(int sty)
{
    return sty == TS_D ? TS_T : sty == TS_T ? TS_S : TS_SS;
}

// the scripts of a base box (its items are i0..now): sup and sub at the right (x0 = the left end of the scripts)
static tbox_t t_scripts(tbox_t base, int sty, bool italic, bool bigop)
{
    tbox_t sup = {0, 0, 0}, sub = {0, 0, 0};
    int si0 = 0, si1 = 0, bi0 = 0, bi1 = 0;
    bool has_sup = false, has_sub = false;
    const int ss = sty_sup(sty);
    // the primes: glyphs of the base size after the base (the STIX prime is high already)
    while (*s_tp == '\'') {
        s_tp++;
        const int p0 = s_ar->n;
        tbox_t pr = t_glyph(0x2032, false, sty);
        it_move(p0, s_ar->n, base.w - (italic ? 0 : em_of(sty) / 16), 0);
        base.w += pr.w - (italic ? 0 : em_of(sty) / 16);
        if (pr.a > base.a) {
            base.a = pr.a;
        }
    }
    for (int k = 0; k < 3; k++) {
        t_skip();
        if (*s_tp == '^' && !has_sup) {
            s_tp++;
            si0 = s_ar->n;
            sup = t_group(ss);
            si1 = s_ar->n;
            has_sup = true;
        } else if (*s_tp == '_' && !has_sub) {
            s_tp++;
            bi0 = s_ar->n;
            sub = t_group(ss);
            bi1 = s_ar->n;
            has_sub = true;
        }
    }
    if (!has_sup && !has_sub) {
        return base;
    }
    const int em = em_of(sty);
    int up = base.a - em_of(ss) * 30 / 100;
    if (up < em * 40 / 100) {
        up = em * 40 / 100;
    }
    int dn = base.d + em_of(ss) * 10 / 100;
    if (dn < em * 18 / 100) {
        dn = em * 18 / 100;
    }
    if (has_sup && has_sub) {
        const int gap = (up - sup.d) - (sub.a - dn);
        if (gap < em / 6) {
            dn += em / 6 - gap;
        }
    }
    const int xs = base.w + (italic ? em / 20 : 0) + (bigop ? -em / 12 : 0);
    const int xb = base.w + (bigop ? -em * 30 / 100 : 0);
    tbox_t r = base;
    if (has_sup) {
        it_move(si0, si1, xs + (bigop ? em / 6 : 0), -up);
        if (up + sup.a > r.a) {
            r.a = up + sup.a;
        }
        if (xs + sup.w + em / 30 > r.w) {
            r.w = xs + sup.w + em / 30 + (bigop ? em / 6 : 0);
        }
    }
    if (has_sub) {
        it_move(bi0, bi1, xb, dn);
        if (dn + sub.d > r.d) {
            r.d = dn + sub.d;
        }
        if (xb + sub.w + em / 30 > r.w) {
            r.w = xb + sub.w + em / 30;
        }
    }
    return r;
}

// a fraction: the bar on the axis
static tbox_t t_frac(int sty, int fsty)
{
    const int i0 = s_ar->n;
    tbox_t nb = t_group(fsty);
    const int i1 = s_ar->n;
    tbox_t db = t_group(fsty);
    const int i2 = s_ar->n;
    const int em = em_of(sty), ax = axis_of(sty), th = rule_of(sty);
    const int pad = em / 10 + 1;
    const int w = (nb.w > db.w ? nb.w : db.w) + 2 * pad;
    const int gap = sty == TS_D ? em / 7 + 1 : em / 10 + 1;
    const int ytop = -ax - (th + 1) / 2;  // the top of the bar
    it_move(i0, i1, (w - nb.w) / 2, ytop - gap - nb.d);
    it_move(i1, i2, (w - db.w) / 2, ytop + th + gap + db.a);
    t_rule(pad / 2, ytop, w - pad, th);
    tbox_t r = {w + 1, -(ytop - gap - nb.d) + nb.a, ytop + th + gap + db.a + db.d};
    return r;
}

// the radical: the sign by lines, the bar over the body
static tbox_t t_sqrt(int sty)
{
    const int em = em_of(sty), th = rule_of(sty);
    int ii0 = -1, ii1 = -1;
    tbox_t ib = {0, 0, 0};
    t_skip();
    if (*s_tp == '[') {
        s_tp++;
        ii0 = s_ar->n;
        tatom_t at[16];
        int n = 0;
        t_list(TS_SS, ']', at, &n, 16);
        ib = t_pack(at, n, TS_SS);
        ii1 = s_ar->n;
        if (*s_tp == ']') {
            s_tp++;
        }
    }
    const int b0 = s_ar->n;
    tbox_t body = t_group(sty);
    const int b1 = s_ar->n;
    const int gap = em / 8 + 1;
    const int top = -(body.a + gap + th);
    const int h = body.a + gap + th + body.d + 1;
    int sw = em * 55 / 100 + h / 10;
    int lead = 0;
    if (ii0 >= 0 && ib.w > sw / 2) {
        lead = ib.w - sw / 2;
    }
    tit_t *t = it_new(TI_RAD);
    if (t) {
        t->x = (int16_t)lead;
        t->y = (int16_t)top;
        t->w = (int16_t)sw;
        t->h = (int16_t)h;
        t->g = (uint16_t)th;
    }
    it_move(b0, b1, lead + sw + em / 12, 0);
    t_rule(lead + sw - 1, top, body.w + em / 6 + 1, th);
    if (ii0 >= 0) {
        it_move(ii0, ii1, lead + sw / 2 - ib.w, top + h * 45 / 100 - ib.d);
    }
    tbox_t r = {lead + sw + body.w + em / 6, -top, body.d + 1};
    return r;
}

// a bracket that grows with its body: \l( ... \r)
static int tall_kind(char c)
{
    switch (c) {
    case '(':
        return IGT_LP;
    case ')':
        return IGT_RP;
    case '[':
        return IGT_LB;
    case ']':
        return IGT_RB;
    case '{':
        return IGT_LC;
    case '}':
        return IGT_RC;
    case '|':
        return IGT_BAR;
    default:
        return -1;
    }
}
// a delimiter of the height that the box (a, d) needs; '.' = none
static tbox_t t_delim(char c, tbox_t inner, int sty)
{
    tbox_t r = {0, 0, 0};
    const int k = tall_kind(c);
    if (k < 0) {
        return r;
    }
    const int ax = axis_of(sty);
    int need = 2 * (inner.a - ax > inner.d + ax ? inner.a - ax : inner.d + ax);
    need = need * 92 / 100;
    const int normal = em_of(sty) * 115 / 100;
    if (need <= normal) {
        const int cp = c == '|' ? '|' : c;
        return t_glyph(cp, false, sty);
    }
    return t_tall(k, need + 2, sty);
}
static tbox_t t_lr(int sty)
{
    char lc = s_tp[0];
    if (lc == '\\' && s_tp[1] == '{') {
        lc = '{';
        s_tp++;
    }
    s_tp++;
    const int i0 = s_ar->n;
    tatom_t at[64];
    int n = 0;
    t_list(sty, 'r', at, &n, 64);
    tbox_t body = t_pack(at, n, sty);
    const int i1 = s_ar->n;
    char rc = '.';
    if (t_cmd_is("r")) {
        s_tp += 2;
        rc = *s_tp;
        if (rc == '\\' && s_tp[1] == '}') {
            rc = '}';
            s_tp++;
        }
        if (rc) {
            s_tp++;
        }
    }
    const int l0 = s_ar->n;
    tbox_t lb = t_delim(lc, body, sty);
    const int l1 = s_ar->n;
    it_move(i0, i1, lb.w, 0);
    tbox_t rb = t_delim(rc, body, sty);
    it_move(l1, s_ar->n, lb.w + body.w, 0);
    (void)l0;
    tbox_t r = {lb.w + body.w + rb.w, body.a, body.d};
    if (lb.a > r.a) {
        r.a = lb.a;
    }
    if (rb.a > r.a) {
        r.a = rb.a;
    }
    if (lb.d > r.d) {
        r.d = lb.d;
    }
    if (rb.d > r.d) {
        r.d = rb.d;
    }
    return r;
}

// ---------------------------------------------------------------- the spaces between the atoms (TeX, in mu)
// 0 none, 1 thin (3 mu), 2 medium (4 mu), 3 thick (5 mu); a negative value: only in the display and text styles
static const int8_t SPACE_TAB[8][8] = {
        {0, 1, -2, -3, 0, 0, 0, -1},     {1, 1, 0, -3, 0, 0, 0, -1},     {-2, -2, 0, 0, -2, 0, 0, -2},
        {-3, -3, 0, 0, -3, 0, 0, -3},    {0, 0, 0, 0, 0, 0, 0, 0},       {0, 1, -2, -3, 0, 0, 0, -1},
        {-1, -1, 0, -1, -1, -1, -1, -1}, {-1, 1, -2, -3, -1, 0, -1, -1},
};
static int space_px(int l, int r, int sty)
{
    if (l >= AC_NONE || r >= AC_NONE) {
        return 0;
    }
    int v = SPACE_TAB[l][r];
    if (v < 0) {
        if (sty >= TS_S) {
            return 0;
        }
        v = -v;
    }
    static const int MU[4] = {0, 3, 4, 5};
    return (MU[v] * em_of(sty) + 9) / 18;
}

// the classes of the atoms after the bin rule of TeX: a + or a - at the start, or after an operator, is an ord
static void fix_bins(tatom_t *at, int n)
{
    int prev = -1;
    for (int i = 0; i < n; i++) {
        if (at[i].cls == AC_NONE) {
            continue;
        }
        if (at[i].cls == AC_BIN) {
            const int pc = prev < 0 ? -1 : at[prev].cls;
            if (pc < 0 || pc == AC_BIN || pc == AC_OP || pc == AC_REL || pc == AC_OPEN || pc == AC_PUNCT) {
                at[i].cls = AC_ORD;
            }
        }
        if ((at[i].cls == AC_REL || at[i].cls == AC_CLOSE || at[i].cls == AC_PUNCT) && prev >= 0 &&
            at[prev].cls == AC_BIN) {
            at[prev].cls = AC_ORD;
        }
        prev = i;
    }
    if (prev >= 0 && at[prev].cls == AC_BIN) {
        at[prev].cls = AC_ORD;
    }
}

// the x of each atom: the items move to their places; the box of the list
static tbox_t t_pack(tatom_t *at, int n, int sty)
{
    fix_bins(at, n);
    tbox_t r = {0, 0, 0};
    int prev = -1;
    for (int i = 0; i < n; i++) {
        if (prev >= 0 && at[i].cls != AC_NONE) {
            r.w += space_px(at[prev].cls, at[i].cls, sty);
        }
        it_move(at[i].i0, at[i].i1, r.w, 0);
        r.w += at[i].b.w + at[i].glue;
        if (at[i].b.a > r.a) {
            r.a = at[i].b.a;
        }
        if (at[i].b.d > r.d) {
            r.d = at[i].b.d;
        }
        if (at[i].cls != AC_NONE) {
            prev = i;
        }
    }
    return r;
}

// ---------------------------------------------------------------- the commands
typedef struct {
    const char *name;
    uint16_t cp;
    uint8_t cls, italic;
} tsym_t;
static const tsym_t TSYM[] = {
        {"pi", 0x3C0, AC_ORD, 1},
        {"alpha", 0x3B1, AC_ORD, 1},
        {"beta", 0x3B2, AC_ORD, 1},
        {"gamma", 0x3B3, AC_ORD, 1},
        {"delta", 0x3B4, AC_ORD, 1},
        {"epsilon", 0x3F5, AC_ORD, 1},
        {"varepsilon", 0x3B5, AC_ORD, 1},
        {"zeta", 0x3B6, AC_ORD, 1},
        {"eta", 0x3B7, AC_ORD, 1},
        {"theta", 0x3B8, AC_ORD, 1},
        {"vartheta", 0x3D1, AC_ORD, 1},
        {"iota", 0x3B9, AC_ORD, 1},
        {"kappa", 0x3BA, AC_ORD, 1},
        {"lambda", 0x3BB, AC_ORD, 1},
        {"mu", 0x3BC, AC_ORD, 1},
        {"nu", 0x3BD, AC_ORD, 1},
        {"xi", 0x3BE, AC_ORD, 1},
        {"rho", 0x3C1, AC_ORD, 1},
        {"sigma", 0x3C3, AC_ORD, 1},
        {"tau", 0x3C4, AC_ORD, 1},
        {"upsilon", 0x3C5, AC_ORD, 1},
        {"phi", 0x3D5, AC_ORD, 1},
        {"varphi", 0x3C6, AC_ORD, 1},
        {"chi", 0x3C7, AC_ORD, 1},
        {"psi", 0x3C8, AC_ORD, 1},
        {"omega", 0x3C9, AC_ORD, 1},
        {"Gamma", 0x393, AC_ORD, 0},
        {"Delta", 0x394, AC_ORD, 0},
        {"Theta", 0x398, AC_ORD, 0},
        {"Lambda", 0x39B, AC_ORD, 0},
        {"Xi", 0x39E, AC_ORD, 0},
        {"Pi", 0x3A0, AC_ORD, 0},
        {"Sigma", 0x3A3, AC_ORD, 0},
        {"Phi", 0x3A6, AC_ORD, 0},
        {"Psi", 0x3A8, AC_ORD, 0},
        {"Omega", 0x3A9, AC_ORD, 0},
        {"infty", 0x221E, AC_ORD, 0},
        {"partial", 0x2202, AC_ORD, 0},
        {"e", 'e', AC_ORD, 0},
        {"C", 'C', AC_ORD, 1},
        {"cdot", 0x22C5, AC_BIN, 0},
        {"times", 0xD7, AC_BIN, 0},
        {"pm", 0xB1, AC_BIN, 0},
        {"mp", 0xB1, AC_BIN, 0},
        {"le", 0x2264, AC_REL, 0},
        {"ge", 0x2265, AC_REL, 0},
        {"ne", 0x2260, AC_REL, 0},
        {"approx", 0x2248, AC_REL, 0},
        {"equiv", 0x2261, AC_REL, 0},
        {"to", 0x2192, AC_REL, 0},
        {"Rightarrow", 0x21D2, AC_REL, 0},
        {"cdots", 0x22EF, AC_INNER, 0},
        {"ldots", 0x2026, AC_INNER, 0},
};
static const char *const TFUNC[] = {"sin",    "cos",    "tan",    "sec",  "csc",  "cot",  "ln",  "log", "exp",
                                    "arctan", "arcsin", "arccos", "sinh", "cosh", "tanh", "erf", "max", "min"};

// upright letters as one box (a function name, \d, \text)
static tbox_t t_word(const char *s, int n, int sty)
{
    tbox_t r = {0, 0, 0};
    for (int i = 0; i < n; i++) {
        const int j0 = s_ar->n;
        tbox_t g;
        if (s[i] == ' ') {
            g = (tbox_t){em_of(sty) / 4, 0, 0};
        } else {
            g = t_glyph((uint8_t)s[i], false, sty);
        }
        it_move(j0, s_ar->n, r.w, 0);
        r.w += g.w;
        if (g.a > r.a) {
            r.a = g.a;
        }
        if (g.d > r.d) {
            r.d = g.d;
        }
    }
    return r;
}

// the limits over and under a sign (a sum in the display style, lim)
static tbox_t t_limits_under(tbox_t op, int i0, int i1, int sty)
{
    const int em = em_of(sty);
    tbox_t sup = {0, 0, 0}, sub = {0, 0, 0};
    int s0 = 0, s1 = 0, b0 = 0, b1 = 0;
    for (int k = 0; k < 2; k++) {
        t_skip();
        if (*s_tp == '^') {
            s_tp++;
            s0 = s_ar->n;
            sup = t_group(sty_sup(sty));
            s1 = s_ar->n;
        } else if (*s_tp == '_') {
            s_tp++;
            b0 = s_ar->n;
            sub = t_group(sty_sup(sty));
            b1 = s_ar->n;
        }
    }
    int w = op.w;
    if (sup.w > w) {
        w = sup.w;
    }
    if (sub.w > w) {
        w = sub.w;
    }
    it_move(i0, i1, (w - op.w) / 2, 0);
    tbox_t r = {w + em / 10, op.a, op.d};
    if (s1 > s0) {
        it_move(s0, s1, (w - sup.w) / 2, -op.a - em / 8 - sup.d);
        r.a = op.a + em / 8 + sup.d + sup.a;
    }
    if (b1 > b0) {
        it_move(b0, b1, (w - sub.w) / 2, op.d + em / 8 + sub.a);
        r.d = op.d + em / 8 + sub.a + sub.d;
    }
    return r;
}

// a big operator (\int, \sum, \prod, \lim) and its limits
static tbox_t t_bigop(const char *name, int sty)
{
    const int em = em_of(sty);
    const int i0 = s_ar->n;
    tbox_t op;
    bool side = true;  // the limits at the right (the integral; a sum in the text style)
    const bool is_int = !strcmp(name, "int");
    if (is_int) {
        const int h = sty == TS_D ? em * 22 / 10 : sty == TS_T ? em * 15 / 10 : em * 12 / 10;
        op = t_tall(IGT_INT, h, sty);
    } else if (!strcmp(name, "lim")) {
        op = t_word("lim", 3, sty);
        side = false;
    } else if (sty == TS_D) {
        op = t_tall(IGT_SUM, em * 14 / 10, sty);
        side = false;
    } else {
        op = t_glyph(!strcmp(name, "sum") ? 0x2211 : 0x220F, false, sty);
    }
    if (side) {
        return t_scripts(op, sty, false, is_int);
    }
    return t_limits_under(op, i0, s_ar->n, sty);
}

// \ev{x}{F}{a}{b}: [F] with b at the top and a at the bottom of the right bracket
static tbox_t t_ev(int sty)
{
    const int v0 = s_ar->n;
    t_group(sty);  // the variable: not drawn
    s_ar->n = v0;
    const int f0 = s_ar->n;
    tbox_t F = t_group(sty);
    const int f1 = s_ar->n;
    tbox_t lb = t_delim('[', F, sty);
    const int l1 = s_ar->n;
    it_move(f0, f1, lb.w, 0);
    tbox_t rb = t_delim(']', F, sty);
    it_move(l1, s_ar->n, lb.w + F.w, 0);
    tbox_t body = {lb.w + F.w + rb.w, F.a, F.d};
    if (lb.a > body.a) {
        body.a = lb.a;
    }
    if (lb.d > body.d) {
        body.d = lb.d;
    }
    const int ss = sty_sup(sty), em = em_of(sty);
    const int a0 = s_ar->n;
    tbox_t lo = t_group(ss);
    const int a1 = s_ar->n;
    tbox_t hi = t_group(ss);
    const int a2 = s_ar->n;
    const int ylo = body.d - lo.d / 2;
    const int yhi = -body.a + hi.a * 7 / 10;
    it_move(a0, a1, body.w + em / 16, ylo);
    it_move(a1, a2, body.w + em / 16, yhi);
    tbox_t r = body;
    r.w = body.w + em / 16 + (lo.w > hi.w ? lo.w : hi.w) + em / 12;
    if (ylo + lo.d > r.d) {
        r.d = ylo + lo.d;
    }
    if (hi.a - yhi > r.a) {
        r.a = hi.a - yhi;
    }
    return r;
}

// \D{x}{F}: the fraction d/dx, then F
static tbox_t t_deriv(int sty)
{
    t_skip();
    char var = 'x';
    if (s_tp[0] == '{' && s_tp[1] && s_tp[2] == '}') {
        var = s_tp[1];
        s_tp += 3;
    }
    const int fsty = sty_frac(sty);
    const int i0 = s_ar->n;
    tbox_t nb = t_word("d", 1, fsty);
    const int i1 = s_ar->n;
    tbox_t db = t_word("d", 1, fsty);
    const int j0 = s_ar->n;
    tbox_t vv = t_glyph(var, true, fsty);
    it_move(j0, s_ar->n, db.w, 0);
    db.w += vv.w;
    if (vv.a > db.a) {
        db.a = vv.a;
    }
    const int i2 = s_ar->n;
    const int em = em_of(sty), ax = axis_of(sty), th = rule_of(sty), pad = em / 10 + 1;
    const int w = (nb.w > db.w ? nb.w : db.w) + 2 * pad;
    const int gap = sty == TS_D ? em / 7 + 1 : em / 10 + 1;
    const int ytop = -ax - (th + 1) / 2;
    it_move(i0, i1, (w - nb.w) / 2, ytop - gap - nb.d);
    it_move(i1, i2, (w - db.w) / 2, ytop + th + gap + db.a);
    t_rule(pad / 2, ytop, w - pad, th);
    tbox_t fb = {w + em / 8, -(ytop - gap - nb.d) + nb.a, ytop + th + gap + db.a + db.d};
    const int f0 = s_ar->n;
    tbox_t F = t_group(sty);
    it_move(f0, s_ar->n, fb.w, 0);
    tbox_t r = {fb.w + F.w, fb.a > F.a ? fb.a : F.a, fb.d > F.d ? fb.d : F.d};
    return r;
}

// a command that is a sign or a letter (TSYM): true and the atom
static bool t_symbol(int n, int sty, tatom_t *a)
{
    for (size_t i = 0; i < sizeof TSYM / sizeof TSYM[0]; i++) {
        if ((int)strlen(TSYM[i].name) == n && !strncmp(s_tp + 1, TSYM[i].name, n)) {
            s_tp += 1 + n;
            a->b = t_glyph(TSYM[i].cp, TSYM[i].italic, sty);
            a->cls = TSYM[i].cls;
            if (TSYM[i].cp == 'e' || TSYM[i].italic) {
                a->b = t_scripts(a->b, sty, TSYM[i].italic, false);
            }
            return true;
        }
    }
    return false;
}

// a space of the markup (\, \; \! \quad); true = it was one
static bool t_space(int sty, int *px)
{
    const int em = em_of(sty);
    if (s_tp[0] != '\\') {
        return false;
    }
    switch (s_tp[1]) {
    case ',':
        *px = (3 * em + 9) / 18;
        break;
    case ':':
        *px = (4 * em + 9) / 18;
        break;
    case ';':
        *px = (5 * em + 9) / 18;
        break;
    case '!':
        *px = -(3 * em + 9) / 18;
        break;
    case ' ':
        *px = em / 3;
        break;
    default:
        if (t_cmd_is("quad")) {
            s_tp += 5;
            *px = em;
            return true;
        }
        if (t_cmd_is("qquad")) {
            s_tp += 6;
            *px = 2 * em;
            return true;
        }
        return false;
    }
    s_tp += 2;
    return true;
}

// one command at the cursor -> the atom (false: not known; the cursor moves past it)
static bool t_command(int sty, tatom_t *a)
{
    const int n = t_cmd_len();
    char name[16];
    snprintf(name, sizeof name, "%.*s", n < 15 ? n : 15, s_tp + 1);
    a->cls = AC_ORD;
    if (t_symbol(n, sty, a)) {
        return true;
    }
    for (size_t i = 0; i < sizeof TFUNC / sizeof TFUNC[0]; i++) {
        if (!strcmp(name, TFUNC[i])) {
            s_tp += 1 + n;
            a->b = t_scripts(t_word(name, n, sty), sty, false, false);
            a->cls = AC_OP;
            return true;
        }
    }
    s_tp += 1 + n;
    if (!strcmp(name, "frac") || !strcmp(name, "tfrac")) {
        a->b = t_frac(sty, name[0] == 't' ? (sty <= TS_T ? TS_S : TS_SS) : sty_frac(sty));
        a->cls = AC_INNER;
    } else if (!strcmp(name, "sqrt")) {
        a->b = t_scripts(t_sqrt(sty), sty, false, false);
    } else if (!strcmp(name, "int") || !strcmp(name, "sum") || !strcmp(name, "prod") || !strcmp(name, "lim")) {
        a->b = t_bigop(name, sty);
        a->cls = AC_OP;
    } else if (!strcmp(name, "l")) {
        a->b = t_scripts(t_lr(sty), sty, false, false);
        a->cls = AC_INNER;
    } else if (!strcmp(name, "ev")) {
        a->b = t_ev(sty);
        a->cls = AC_INNER;
    } else if (!strcmp(name, "D")) {
        a->b = t_deriv(sty);
        a->cls = AC_INNER;
    } else if (!strcmp(name, "d")) {
        a->b = t_word("d", 1, sty);
    } else if (!strcmp(name, "hl") || !strcmp(name, "dim")) {
        const int save = s_tcls;
        const int i0 = s_ar->n;
        tit_t *hb = name[0] == 'h' ? it_new(TI_HL) : NULL;
        s_tcls = name[0] == 'h' ? TC_HL : TC_DIM;
        tbox_t g = t_group(sty);
        s_tcls = save;
        if (hb) {
            const int p = em_of(sty) / 7 + 1;
            hb = &s_ar->it[i0];
            hb->x = (int16_t)-p;
            hb->y = (int16_t)(-g.a - p / 2);
            hb->w = (int16_t)(g.w + 2 * p);
            hb->h = (int16_t)(g.a + g.d + p);
        }
        a->b = t_scripts(g, sty, false, false);
    } else if (!strcmp(name, "text")) {
        const char *q = s_tp + 1;
        const char *e = strchr(q, '}');
        const int len = e ? (int)(e - q) : 0;
        a->b = t_word(q, len, sty);
        s_tp = e ? e + 1 : q;
    } else {
        s_tfail = true;
        return false;
    }
    return true;
}

static inline bool t_stop(char stop)
{
    const char c = *s_tp;
    if (!c) {
        return true;
    }
    if (stop == '}' && c == '}') {
        return true;
    }
    if (stop == ']' && c == ']') {
        return true;
    }
    if (stop == 'r' && t_cmd_is("r")) {
        return true;
    }
    if (stop == '\n' && c == '\\' && s_tp[1] == '\\') {
        return true;
    }
    return false;
}

// one character at the cursor -> the atom
static void t_char(int sty, tatom_t *a)
{
    const char c = *s_tp;
    if ((c >= '0' && c <= '9') || (c == '.' && s_tp[1] >= '0' && s_tp[1] <= '9')) {
        const char *q = s_tp;
        while ((*q >= '0' && *q <= '9') || (*q == '.' && q[1] >= '0' && q[1] <= '9')) {
            q++;
        }
        a->b = t_scripts(t_word(s_tp, (int)(q - s_tp), sty), sty, false, false);
        s_tp = q;
        a->cls = AC_ORD;
        return;
    }
    s_tp++;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        a->b = t_scripts(t_glyph(c, true, sty), sty, true, false);
        a->cls = AC_ORD;
        return;
    }
    int cp = (uint8_t)c;
    a->cls = AC_ORD;
    switch (c) {
    case '+':
        a->cls = AC_BIN;
        break;
    case '-':
        cp = 0x2212;
        a->cls = AC_BIN;
        break;
    case '=':
    case '<':
    case '>':
        a->cls = AC_REL;
        break;
    case ',':
    case ';':
        a->cls = AC_PUNCT;
        break;
    case '(':
    case '[':
        a->cls = AC_OPEN;
        break;
    case ')':
    case ']':
    case '!':
        a->cls = AC_CLOSE;
        break;
    default:
        break;
    }
    a->b = t_glyph(cp, false, sty);
    if (a->cls == AC_CLOSE || a->cls == AC_ORD) {
        a->b = t_scripts(a->b, sty, false, false);
    }
}

// the atoms of a list up to its stop (stop 1: one atom); out gets them (their items at their own origins)
static tbox_t t_list(int sty, char stop, tatom_t *out, int *nout, int max)
{
    int n = 0;
    while (!s_tfail) {
        t_skip();
        if (stop != 1 && t_stop(stop)) {
            break;
        }
        if (!*s_tp || n >= max) {
            break;
        }
        int glue = 0;
        if (t_space(sty, &glue)) {
            if (n) {
                out[n - 1].glue += glue;
            }
            continue;
        }
        tatom_t *a = &out[n];
        memset(a, 0, sizeof *a);
        a->i0 = s_ar->n;
        if (*s_tp == '{') {
            a->b = t_scripts(t_group(sty), sty, false, false);
            a->cls = AC_ORD;
        } else if (*s_tp == '\\' && t_cmd_len() > 0) {
            if (!t_command(sty, a)) {
                break;
            }
        } else if (*s_tp == '\\') {
            s_tp += 2;  // (an escaped sign that the app does not draw)
            continue;
        } else {
            t_char(sty, a);
        }
        a->i1 = s_ar->n;
        n++;
        if (stop == 1) {
            break;
        }
    }
    *nout = n;
    return (tbox_t){0, 0, 0};
}

// ---------------------------------------------------------------- the lines of a formula
// A formula: its items (a range of the arena) at the block origin (the top left), its size.
typedef struct {
    int i0, i1;
    int w, h;
    int base0;  // the baseline of the first row (for inline math: the baseline in the block)
    int level;
    bool fit;  // false: it did not fit the box at the smallest level
} tform_t;

#define TROW_MAX 12
typedef struct {
    int a0, a1;  // the atoms [a0, a1) of the line
    int x;       // the x of the row start in the block
    int a, d;
} trow_t;

// the atoms of all the lines (one array; the lines are its parts)
#define TLINE_ATOMS 160
static tatom_t s_la[TLINE_ATOMS];
static int s_lx[TLINE_ATOMS];  // the x of each atom in its line (after the spaces)

// the atom i of the lines is one glyph of + or the minus (a line that goes on with it)
static bool s_tp0_bin(int i)
{
    if (s_la[i].i1 - s_la[i].i0 != 1) {
        return false;
    }
    const tit_t *t = &s_ar->it[s_la[i].i0];
    if (t->kind != TI_GLY) {
        return false;
    }
    const int cp = face_of(t->font)->g[t->g].cp;
    return cp == '+' || cp == 0x2212;
}

// the rows of the formula: the breaks before a relation (then before a + or -) when a line is wider than maxw
static int t_rows(int nl, const int *l0, const int *l1, int maxw, int sty, trow_t *rows, int *relx_out)
{
    int nr = 0;
    int relx = -1;
    const int em = em_of(sty);
    for (int li = 0; li < nl; li++) {
        // the x of each atom of this line, with the spaces
        int xx = 0, prev = -1;
        for (int i = l0[li]; i < l1[li]; i++) {
            if (prev >= 0 && s_la[i].cls != AC_NONE) {
                xx += space_px(s_la[prev].cls, s_la[i].cls, sty);
            }
            s_lx[i] = xx;
            xx += s_la[i].b.w + s_la[i].glue;
            if (s_la[i].cls != AC_NONE) {
                prev = i;
            }
        }
        // the first relation of the first line: the column of the alignment
        int first_rel = -1;
        for (int i = l0[li]; i < l1[li]; i++) {
            if (s_la[i].cls == AC_REL) {
                first_rel = i;
                break;
            }
        }
        if (relx < 0 && first_rel > l0[li]) {
            relx = s_lx[first_rel];
        }
        int start = l0[li];
        while (start < l1[li] && nr < TROW_MAX) {
            const bool starts_rel = s_la[start].cls == AC_REL;
            int indent = 0;
            if (start > l0[li]) {  // a break of a long line
                indent = starts_rel ? (relx >= 0 ? relx : em) : (relx >= 0 ? relx + em : em);
            } else if (li > 0 && starts_rel) {  // a line that goes on with a relation
                indent = relx >= 0 ? relx : 0;
            } else if (li > 0 && (s_la[start].cls == AC_BIN || s_la[start].cls == AC_ORD) &&
                       (s_tp0_bin(start))) {  // a line that goes on with a + or a -
                indent = relx >= 0 ? relx + em : em;
            }
            const int xs = s_lx[start];
            int end = l1[li];
            if (s_tbreak && indent + s_lx[l1[li] - 1] + s_la[l1[li] - 1].b.w - xs > maxw) {
                // the last relation (else + or -) that keeps the row in maxw; at least one atom
                int brk = -1, brk2 = -1;
                for (int i = start + 1; i < l1[li]; i++) {
                    const int wi = indent + s_lx[i] - xs;
                    if (wi > maxw) {
                        break;
                    }
                    if (s_la[i].cls == AC_REL) {
                        brk = i;
                    }
                    if (s_la[i].cls == AC_BIN) {
                        brk2 = i;
                    }
                }
                end = brk > start ? brk : brk2 > start ? brk2 : l1[li];
            }
            // a row that does not fit with its indent: less indent (the alignment goes, the row stays whole)
            const int rw = s_lx[end - 1] + s_la[end - 1].b.w - xs;
            if (indent > 0 && indent + rw > maxw) {
                indent = maxw - rw > em ? maxw - rw : (rw + em <= maxw ? em : 0);
            }
            trow_t *r = &rows[nr++];
            r->a0 = start;
            r->a1 = end;
            r->x = indent - xs;
            r->a = r->d = 0;
            for (int i = start; i < end; i++) {
                if (s_la[i].b.a > r->a) {
                    r->a = s_la[i].b.a;
                }
                if (s_la[i].b.d > r->d) {
                    r->d = s_la[i].b.d;
                }
            }
            start = end;
        }
    }
    *relx_out = relx;
    return nr;
}

// lay out the markup at one level: the items stay in the arena; false = no room in the arena
static bool t_layout_at(const char *m, int level, int sty, int maxw, tform_t *f)
{
    s_base = level;
    s_tcls = TC_INK;
    s_tp = m;
    s_tfail = false;
    f->i0 = s_ar->n;
    int nl = 0, l0[8], l1[8], na = 0;
    while (nl < 8) {
        int n = 0;
        l0[nl] = na;
        t_list(sty, '\n', s_la + na, &n, TLINE_ATOMS - na);
        fix_bins(s_la + na, n);
        na += n;
        l1[nl] = na;
        if (n) {
            nl++;
        }
        if (s_tp[0] == '\\' && s_tp[1] == '\\') {
            s_tp += 2;
            continue;
        }
        break;
    }
    trow_t rows[TROW_MAX];
    int relx;
    const int nr = nl ? t_rows(nl, l0, l1, maxw, sty, rows, &relx) : 0;
    const int em = em_of(sty);
    const int gap = sty == TS_D ? em * 35 / 100 : em / 5;
    int y = 0, w = 0, minx = 0;
    for (int r = 0; r < nr; r++) {
        if (rows[r].x + s_lx[rows[r].a0] < minx) {
            minx = rows[r].x + s_lx[rows[r].a0];
        }
    }
    for (int r = 0; r < nr; r++) {
        y += (r ? rows[r - 1].d + gap : 0) + rows[r].a;
        if (!r) {
            f->base0 = rows[0].a;
        }
        for (int i = rows[r].a0; i < rows[r].a1; i++) {
            it_move(s_la[i].i0, s_la[i].i1, rows[r].x + s_lx[i] - minx, y);
        }
        const int last = rows[r].a1 - 1;
        const int rw = rows[r].x + s_lx[last] + s_la[last].b.w - minx;
        if (rw > w) {
            w = rw;
        }
    }
    f->i1 = s_ar->n;
    f->w = w;
    f->h = nr ? y + rows[nr - 1].d : 0;
    f->level = level;
    return !s_tfail;
}

// the formula at the biggest level that fits maxw x maxh: first in one row at lv_max and the level under it, then
// (brk) with the breaks of the long lines at each level; brk false: one row at each level
static void t_layout(const char *m, int lv_max, int lv_min, int sty, int maxw, int maxh, tform_t *f, bool brk)
{
    const int keep = s_ar->n;
    for (int pass = 0; pass < 2; pass++) {
        for (int lv = lv_max; lv <= lv_min; lv++) {
            if (brk && pass == 0 && lv > lv_max + 1) {
                break;
            }
            s_tbreak = pass == 1;
            s_ar->n = keep;
            const bool ok = t_layout_at(m, lv, sty, maxw, f);
            f->fit = ok && f->w <= maxw && f->h <= maxh;
            if (f->fit) {
                s_tbreak = true;
                return;
            }
        }
        if (!brk) {
            break;
        }
    }
    s_tbreak = true;
}

// ---------------------------------------------------------------- the drawing of a formula
static uint16_t s_tcol[3];  // the colours of TC_INK, TC_HL, TC_DIM

static void rad_draw(const tit_t *t, int ox, int oy, uint16_t c, int alpha)
{
    const float x = (float)(ox + t->x), y = (float)(oy + t->y);
    const float w = t->w, h = t->h, th = t->g < 1 ? 1.0f : (float)t->g;
    const float yb = y + h - 1;
    tfd_line(x, y + h * 0.58f, x + w * 0.22f, y + h * 0.50f, th * 0.9f, c, alpha);
    tfd_line(x + w * 0.22f, y + h * 0.50f, x + w * 0.50f, yb, th * 1.7f, c, alpha);
    tfd_line(x + w * 0.50f, yb, x + w - 0.5f, y + th * 0.5f, th * 0.95f, c, alpha);
}

// the items of a formula at (ox, oy) (the block origin); alpha 0..255; glow = the alpha of the highlight boxes
static void t_draw(const tarena_t *ar, const tform_t *f, int ox, int oy, int alpha, int glow)
{
    alpha = fa(alpha);
    glow = fa(glow);
    for (int i = f->i0; i < f->i1; i++) {
        const tit_t *t = &ar->it[i];
        if (t->kind == TI_HL && glow > 0) {
            tfd_rrect(ox + t->x, oy + t->y, t->w, t->h, t->h / 4 + 2, s_tcol[TC_HL], glow * alpha / 255);
        }
    }
    for (int i = f->i0; i < f->i1; i++) {
        const tit_t *t = &ar->it[i];
        const uint16_t c = s_tcol[t->cls];
        switch (t->kind) {
        case TI_GLY: {
            const mfont_t *fo = face_of(t->font);
            glyph_m(fo, &fo->g[t->g], ox + t->x, oy + t->y, c, alpha);
            break;
        }
        case TI_TALL: {
            const ig_tall_t *g = &IG_TALL[t->g];
            glyph_a4(IG_TALL_DATA, g->off, ox + t->x, oy + t->y, g->w, g->h, c, alpha);
            break;
        }
        case TI_RULE:
            tfd_rect(ox + t->x, oy + t->y, t->w, t->h, c, alpha);
            break;
        case TI_RAD:
            rad_draw(t, ox, oy, c, alpha);
            break;
        default:
            break;
        }
    }
}
