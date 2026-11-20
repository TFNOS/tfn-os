// LINEAR ALGEBRA: the flow of a text with mathematics in a box (the lines of la_type.inc), from the left (English)
// or from the right (Hebrew). One function measures and draws: la_flow(text, box, draw).

enum {
    IT_WORD,
    IT_MATH,
    IT_NEUT
};
typedef struct {
    const char *s;
    uint16_t len;
    uint8_t type, heb, sp;
    uint16_t col;
    int w, a, d;  // quarter px, px, px
} it_t;
#define IT_MAX 120
static it_t s_it[IT_MAX];

static inline bool is_heb(int cp)
{
    return cp >= 0x590 && cp <= 0x5FF;
}
static bool str_heb(const char *s, int n)
{
    const char *e = s + n;
    while (s < e) {
        if (is_heb(utf8_next(&s))) {
            return true;
        }
    }
    return false;
}
static bool is_punct(char c)
{
    return c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '?' || c == '(' || c == ')' || c == '"' ||
           c == '\'';
}
static int mirror(int cp)
{
    switch (cp) {
    case '(':
        return ')';
    case ')':
        return '(';
    case '[':
        return ']';
    case ']':
        return '[';
    case '<':
        return '>';
    case '>':
        return '<';
    default:
        return cp;
    }
}
static int word_w(const mfont_t *f, const char *s, int n)
{
    const char *e = s + n;
    int w = 0;
    while (s < e) {
        w += ty_adv(f, utf8_next(&s));
    }
    return w;
}
// the characters of a part that keep their order in a Hebrew word (digits, Latin letters, a point between digits)
static inline bool ltr_ch(int cp)
{
    return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '.' || cp == '%';
}
// draw a word from x4 (its left end): LTR, or a Hebrew word (its letters from the right)
static void word_draw(const mfont_t *f, const it_t *it, int x4, int base)
{
    const char *s = it->s, *e = it->s + it->len;
    if (!s_rtl || (!it->heb && it->type != IT_NEUT)) {
        while (s < e) {
            x4 += ty_put(f, utf8_next(&s), x4, base, it->col, s_talpha);
        }
        return;
    }
    int r4 = x4 + it->w;  // from the right end
    while (s < e) {
        const char *p = s;
        int cp = utf8_next(&p);
        if (ltr_ch(cp)) {
            // a part with its own order: its width, then from its left end
            const char *q = s;
            int w = 0;
            while (q < e) {
                const char *q2 = q;
                int c2 = utf8_next(&q2);
                if (!ltr_ch(c2)) {
                    break;
                }
                w += ty_adv(f, c2);
                q = q2;
            }
            int lx = r4 - w;
            while (s < q) {
                lx += ty_put(f, utf8_next(&s), lx, base, it->col, s_talpha);
            }
            r4 -= w;
            continue;
        }
        cp = mirror(cp);
        r4 -= ty_adv(f, cp);
        ty_put(f, cp, r4, base, it->col, s_talpha);
        s = p;
    }
}
// one item of a word (or of a sign at its end)
static int push_word(int n, const char *a, int len, uint8_t type, uint8_t sp, uint16_t col, const mfont_t *f)
{
    if (n >= IT_MAX) {
        return n;
    }
    it_t *it = &s_it[n];
    it->s = a;
    it->len = (uint16_t)len;
    it->type = type;
    it->heb = type == IT_WORD && str_heb(a, len);
    it->sp = sp;
    it->col = col;
    it->w = word_w(f, a, len);
    it->a = f->asc;
    it->d = 0;
    return n + 1;
}
// cut a line of prose into items
static int tokenize(const char *s, const char *e, const mfont_t *f, uint16_t col)
{
    int n = 0;
    bool sp = false, emph = false;
    while (s < e && n < IT_MAX) {
        char c = *s;
        if (c == ' ') {
            sp = true;
            s++;
            continue;
        }
        if (c == '*') {
            emph = !emph;
            s++;
            continue;
        }
        uint16_t ic = emph ? COL_ACC : col;
        if (c == '$') {
            it_t *it = &s_it[n];
            const char *m = ++s;
            while (s < e && *s != '$') {
                s++;
            }
            it->s = m;
            it->len = (uint16_t)(s - m);
            it->type = IT_MATH;
            it->heb = 0;
            it->sp = sp;
            it->col = ic;
            int keep = s_mbase;
            bool kd = s_mdisp;
            s_mbase = s_tsz;
            s_mdisp = false;
            bx_t b = m_measure(m);
            s_mbase = keep;
            s_mdisp = kd;
            it->w = b.w;
            it->a = b.a;
            it->d = b.d;
            if (s < e) {
                s++;
            }
            n++;
            sp = false;
            continue;
        }
        const char *a = s;
        while (s < e && *s != ' ' && *s != '$' && *s != '*') {
            s++;
        }
        const char *b = s, *bt = b;
        uint8_t spv = sp;
        sp = false;
        // in a Hebrew text the signs at the ends of a word are items of their own (they go to the correct side)
        if (s_rtl) {
            while (a < b && is_punct(*a)) {
                n = push_word(n, a++, 1, IT_NEUT, spv, ic, f);
                spv = 0;
            }
            while (bt > a && is_punct(bt[-1])) {
                bt--;
            }
        }
        if (bt > a) {
            n = push_word(n, a, (int)(bt - a), IT_WORD, spv, ic, f);
            spv = 0;
        }
        for (const char *t = bt; t < b; t++) {
            n = push_word(n, t, 1, IT_NEUT, spv, ic, f);
            spv = 0;
        }
    }
    return n;
}
static void item_draw(const mfont_t *f, const it_t *it, int x4, int base)
{
    if (it->type == IT_MATH) {
        int keep = s_mbase;
        bool kd = s_mdisp;
        s_mbase = s_tsz;
        s_mdisp = false;
        const char *p = it->s;
        m_list(&p, 0, x4, base, true, it->col);
        s_mbase = keep;
        s_mdisp = kd;
    } else {
        word_draw(f, it, x4, base);
    }
}
static inline bool it_ltr(const it_t *it)
{
    return it->type == IT_MATH || (it->type == IT_WORD && !it->heb);
}
// one line of items [i0, i1) on the baseline: from x0 (LTR) or to x1 (RTL)
static void line_draw(const mfont_t *f, int i0, int i1, int x0, int x1, int base, int sp4)
{
    if (!s_rtl) {
        int x4 = x0 * 4;
        for (int i = i0; i < i1; i++) {
            if (i > i0 && s_it[i].sp) {
                x4 += sp4;
            }
            item_draw(f, &s_it[i], x4, base);
            x4 += s_it[i].w;
        }
        return;
    }
    int X = x1 * 4;
    int i = i0;
    while (i < i1) {
        if (i > i0 && s_it[i].sp) {
            X -= sp4;
        }
        if (!it_ltr(&s_it[i])) {
            X -= s_it[i].w;
            item_draw(f, &s_it[i], X, base);
            i++;
            continue;
        }
        // a run of LTR items with spaces between them: one block in its own order
        int j = i + 1, W = s_it[i].w;
        while (j < i1 && it_ltr(&s_it[j]) && s_it[j].sp) {
            W += sp4 + s_it[j].w;
            j++;
        }
        int x4 = X - W;
        for (int k = i; k < j; k++) {
            if (k > i) {
                x4 += sp4;
            }
            item_draw(f, &s_it[k], x4, base);
            x4 += s_it[k].w;
        }
        X -= W;
        i = j;
    }
}
// a paragraph in the box; returns its height. y = the top. The lines are not cut: a word wider than the box is an
// overflow.
static int para(const char *s, const char *e, const mfont_t *f, uint16_t col, int x0, int x1, int y, bool draw)
{
    int n = tokenize(s, e, f, col);
    int sp4 = ty_adv(f, ' ');
    int W4 = (x1 - x0) * 4;
    int amin = f->asc * 13 / 10, dmin = f->line - amin;
    if (dmin < 3) {
        dmin = 3;
    }
    int H = 0, i = 0;
    while (i < n) {
        int w = s_it[i].w, j = i + 1;
        while (j < n && w + (s_it[j].sp ? sp4 : 0) + s_it[j].w <= W4) {
            w += (s_it[j].sp ? sp4 : 0) + s_it[j].w;
            j++;
        }
        // do not start a line with an item that is glued to the item before it (a sign, a comma)
        while (j < n && j > i + 1 && !s_it[j].sp) {
            j--;
        }
        if (w > W4) {
            s_overflow = true;
        }
        int a = amin, d = dmin;
        for (int k = i; k < j; k++) {
            if (s_it[k].a + 1 > a) {
                a = s_it[k].a + 1;
            }
            if (s_it[k].d + 1 > d) {
                d = s_it[k].d + 1;
            }
        }
        if (draw) {
            line_draw(f, i, j, x0, x1, y + H + a, sp4);
        }
        H += a + d;
        i = j;
    }
    return H;
}

typedef struct {
    int x, y, w, h;
} rect_t;

// the places in a formula where a long formula can go to a new line (not in a brace): before a sign "=", "≈"
// or "≤" (the new line starts with the sign), and after ", " (the new line starts after the comma)
static int eq_cuts(const char *f, int n, int *cut, int *skip, int max)
{
    int k = 0, depth = 0;
    for (int i = 0; i < n && k < max; i++) {
        char c = f[i];
        if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
        } else if (depth == 0 && i > 0) {
            bool rel = c == '=' || (!strncmp(f + i, "≈", 3)) || (!strncmp(f + i, "≤", 3)) ||
                       (f[i - 1] == ' ' && (c == '+' || !strncmp(f + i, "−", 3)) && f[i + (c == '+' ? 1 : 3)] == ' ');
            if (rel) {
                cut[k] = i;
                skip[k++] = 0;
            } else if (c == '@' && f[i + 1] == 'q') {
                cut[k] = i;
                skip[k++] = 2;
            } else if (c == ',' && f[i + 1] == ' ' && i + 1 < n && strstr(f, "@q") == NULL) {
                cut[k] = i + 1;
                skip[k++] = 1;
            }
        }
    }
    return k;
}
// one display formula (n bytes from f), centred; the largest face that fits. When even the face of the text does not
// fit, the formula goes on more lines, each new line from a sign "=". Returns the height.
static int s_hmax = 1000;  // the largest height of one display formula (la_flow sets it from its box)
static int display(const char *f, int n, int x0, int x1, int y, int W, bool draw)
{
    static char buf[700];  // (static: the stack of the app task is small; display() does not call itself)
    if (n > (int)sizeof buf - 1) {
        n = (int)sizeof buf - 1;
    }
    memcpy(buf, f, n);
    buf[n] = 0;
    int keep = s_mbase;
    s_mdisp = true;
    int minb = s_tsz - 1 < 0 ? 0 : s_tsz - 1;
    bx_t b = {0, 0, 0};
    for (s_mbase = s_msz; s_mbase >= minb; s_mbase--) {
        b = m_measure(buf);
        if (b.w <= W * 4 && b.a + b.d <= s_hmax) {
            break;
        }
    }
    if (s_mbase < minb && b.w <= W * 4) {
        // only too tall (a big matrix): a smaller face, on one line
        for (s_mbase = minb - 1; s_mbase >= 0; s_mbase--) {
            b = m_measure(buf);
            if (b.a + b.d <= s_hmax) {
                break;
            }
        }
        if (s_mbase < 0) {
            s_mbase = 0;
        }
        if (draw) {
            m_draw(buf, (x0 + x1) * 2 - b.w / 2, y + b.a);
        }
        s_mbase = keep;
        s_mdisp = false;
        return b.a + b.d;
    }
    int H = 0;
    if (s_mbase >= minb) {
        if (draw) {
            m_draw(buf, (x0 + x1) * 2 - b.w / 2, y + b.a);
        }
        H = b.a + b.d;
    } else {
        // more lines: the parts between the signs "=", as many on a line as fit
        int cut[16], skip[16], nc = eq_cuts(buf, n, cut, skip, 16);
        int at[18], sk[18], np = 0;
        at[np] = 0;
        sk[np++] = 0;
        for (int i = 0; i < nc; i++) {
            at[np] = cut[i];
            sk[np++] = skip[i];
        }
        at[np] = n;
        int line_gap = SZ[s_tsz]->line / 5;
        for (int base = s_tsz; base >= 0; base--) {
            s_mbase = base;
            bool fit = true;
            int i = 0, h = 0;
            while (i < np) {
                int j = i + 1;
                static char part[700];
                bx_t pb = {0, 0, 0};
                // the longest run of parts from i that fits
                for (int k = i + 1; k <= np; k++) {
                    int L = at[k] - at[i] - sk[i];
                    memcpy(part, buf + at[i] + sk[i], L);
                    part[L] = 0;
                    bx_t t = m_measure(part);
                    if (t.w <= W * 4 || k == i + 1) {
                        j = k;
                        pb = t;
                        if (t.w > W * 4) {
                            fit = false;
                        }
                    } else {
                        break;
                    }
                }
                int L = at[j] - at[i] - sk[i];
                memcpy(part, buf + at[i] + sk[i], L);
                part[L] = 0;
                if (draw && (fit || base == 0)) {
                    m_draw(part, (x0 + x1) * 2 - pb.w / 2, y + h + pb.a);
                }
                h += pb.a + pb.d + (j < np ? line_gap : 0);
                i = j;
            }
            if (fit || base == 0) {
                if (!fit) {
                    s_overflow = true;
                }
                H = h;
                break;
            }
        }
    }
    s_mbase = keep;
    s_mdisp = false;
    return H;
}

// the height of one source line (from s to e) of the text, or its drawing at y
static int flow_line(const char *s, const char *e, int x0, int x1, int y, int W, bool draw)
{
    const mfont_t *tf = SZ[s_tsz];
    int bi = s_tsz - 2;
    const mfont_t *hf = SZB[bi < 0 ? 0 : bi > 4 ? 4 : bi];
    int gap = tf->line / 4;
    if (*s == '=') {
        return gap / 2 + display(s + 1, (int)(e - s - 1), x0, x1, y + gap / 2, W, draw) + gap;
    }
    if (*s == '#') {
        return para(s + 1, e, hf, COL_ACC, x0, x1, y, draw) + gap / 2;
    }
    if (e > s) {
        return para(s, e, tf, s_tcol, x0, x1, y, draw) + gap / 2;
    }
    return 0;
}
// the text in the box r from the source line "from" (0 = the first): the lines that fit, then *next = the first line
// that did not fit (-1: all fit). Returns the height. A line taller than the box alone is an overflow.
static int la_flow(const char *text, rect_t r, bool draw, int dx, int from, int *next)
{
    const char *s = text;
    int x0 = r.x + dx, x1 = r.x + r.w + dx, y = r.y, line = 0, placed = 0;
    s_hmax = r.h * 3 / 4;
    if (next) {
        *next = -1;
    }
    while (*s) {
        const char *e = strchr(s, '\n');
        if (!e) {
            e = s + strlen(s);
        }
        if (line >= from) {
            int h = flow_line(s, e, x0, x1, y, r.w, false);
            int hk = h;
            if (*s == '#' && *e) {
                // a heading stays with the line after it
                const char *s2 = e + 1, *e2 = strchr(s2, '\n');
                hk += flow_line(s2, e2 ? e2 : s2 + strlen(s2), x0, x1, y + h, r.w, false);
            }
            if (y + hk > r.y + r.h + 2 && placed) {
                if (next) {
                    *next = line;
                }
                break;
            }
            if (h > r.h + 2) {
                s_overflow = true;
            }
            if (draw) {
                flow_line(s, e, x0, x1, y, r.w, true);
            }
            y += h;
            placed++;
        }
        line++;
        s = *e ? e + 1 : e;
    }
    return y - r.y;
}
