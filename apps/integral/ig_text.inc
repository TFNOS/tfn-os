// INTEGRALS: the text of a step (English or Hebrew) with inline math ($...$): the words of the KF faces and the
// formulas of ig_type.inc in one block. The lines break between words; a Hebrew text goes from the right, and a run
// of English words, numbers and formulas in it goes from the left (a short form of the bidi rules).

#define TT_TOK  72
#define TT_FORM 20
enum {
    TK_WORD,
    TK_MATH
};
typedef struct {
    const char *s;
    int16_t n, w, x, y;  // the x of the left end and the baseline (after the layout)
    int8_t form;         // TK_MATH: the index of its formula
    uint8_t kind, heb, sp;
} ttok_t;
typedef struct {
    ttok_t tok[TT_TOK];
    int ntok;
    tform_t form[TT_FORM];
    int nform;
    const kfont_t *font;
    int w, h, lines;
    bool fit;
} ttext_t;

// the faces of the text from big to small, and the level of the math with each one
typedef struct {
    const kfont_t *f;
    int level;
} tface_t;
static const tface_t *face_list(int *n)
{
    static tface_t L[4];
    L[0] = (tface_t){&KF_BIG, 0};
    L[1] = (tface_t){&KF_TEXT, 1};
    L[2] = (tface_t){&KF_NAME, 2};
    L[3] = (tface_t){&KF_LABEL, 2};
    *n = 4;
    return L;
}

static char s_mbuf[4096];  // the inline formulas as NUL-ended texts (the parser needs the end)
static int s_mbuf_n;

static bool tok_heb(const char *s, int n)
{
    const char *e = s + n;
    while (s < e) {
        if (is_heb(utf8_next(&s))) {
            return true;
        }
    }
    return false;
}

// the tokens of a text (logical order); the formulas are laid out at the level
static void tt_tokens(ttext_t *T, const char *txt, const kfont_t *f, int level)
{
    T->ntok = T->nform = 0;
    const char *p = txt;
    bool sp = false;
    while (*p && T->ntok < TT_TOK) {
        if (*p == ' ') {
            sp = true;
            p++;
            continue;
        }
        ttok_t *k = &T->tok[T->ntok];
        memset(k, 0, sizeof *k);
        k->sp = sp;
        sp = false;
        if (*p == '$') {
            const char *e = strchr(p + 1, '$');
            const int n = e ? (int)(e - p - 1) : (int)strlen(p + 1);
            k->kind = TK_MATH;
            if (T->nform < TT_FORM && s_mbuf_n + n + 1 < (int)sizeof s_mbuf) {
                char *m = s_mbuf + s_mbuf_n;
                memcpy(m, p + 1, n);
                m[n] = 0;
                s_mbuf_n += n + 1;
                tform_t *fo = &T->form[T->nform];
                t_layout_at(m, level, TS_T, 4000, fo);
                k->form = (int8_t)T->nform++;
                k->w = (int16_t)fo->w;
            }
            p = e ? e + 1 : p + 1 + n;
            T->ntok++;
            continue;
        }
        const char *q = p;
        while (*q && *q != ' ' && *q != '$') {
            q++;
        }
        k->kind = TK_WORD;
        k->s = p;
        k->n = (int16_t)(q - p);
        k->heb = tok_heb(p, k->n);
        k->w = (int16_t)((kf_w4(f, p, k->n) + 3) / 4);
        p = q;
        T->ntok++;
    }
}

// a token with a latin letter, a digit or a formula (a token of signs only takes the side of the text around it)
static bool tok_strong_ltr(const ttok_t *k)
{
    if (k->kind == TK_MATH) {
        return true;
    }
    if (k->heb) {
        return false;
    }
    for (int i = 0; i < k->n; i++) {
        const char c = k->s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return true;
        }
    }
    return false;
}

// the visual x of the tokens [a, b) of one line: ltr from x0; rtl from the right end x1
static void tt_place_line(ttext_t *T, int a, int b, int x0, int x1, int base, int spw, bool rtl)
{
    if (!rtl) {
        int x = x0;
        for (int i = a; i < b; i++) {
            if (i > a && T->tok[i].sp) {
                x += spw;
            }
            T->tok[i].x = (int16_t)x;
            T->tok[i].y = (int16_t)base;
            x += T->tok[i].w;
        }
        return;
    }
    // rtl: runs of ltr tokens (english words, numbers, formulas) keep their order inside the run
    int xr = x1;
    int i = a;
    while (i < b) {
        const bool ltr = tok_strong_ltr(&T->tok[i]);
        int j = i + 1;
        if (ltr) {
            // the run: strong ltr tokens, and the signs between them (a sign at its end goes with the rtl text)
            int last = i;
            while (j < b && !T->tok[j].heb) {
                if (tok_strong_ltr(&T->tok[j])) {
                    last = j;
                }
                j++;
            }
            j = last + 1;
            // the run: its width, then its tokens from its left end
            int w = 0;
            for (int q = i; q < j; q++) {
                w += T->tok[q].w + (q > i && T->tok[q].sp ? spw : 0);
            }
            if (i > a && T->tok[i].sp) {
                xr -= spw;
            }
            int x = xr - w;
            for (int q = i; q < j; q++) {
                if (q > i && T->tok[q].sp) {
                    x += spw;
                }
                T->tok[q].x = (int16_t)x;
                T->tok[q].y = (int16_t)base;
                x += T->tok[q].w;
            }
            xr -= w;
        } else {
            if (i > a && T->tok[i].sp) {
                xr -= spw;
            }
            xr -= T->tok[i].w;
            T->tok[i].x = (int16_t)xr;
            T->tok[i].y = (int16_t)base;
        }
        i = j;
    }
}

// one try: the lines of the text in the width w; false = more than maxh
static bool tt_try(ttext_t *T, const char *txt, const kfont_t *f, int level, int w, int maxh, bool rtl)
{
    const int keep = s_ar->n, keepm = s_mbuf_n;
    s_ar->n = keep;
    s_mbuf_n = keepm;
    tt_tokens(T, txt, f, level);
    T->font = f;
    const int spw = (kf_w4(f, " ", 1) + 3) / 4;
    int y = 0, i = 0, lines = 0, maxw = 0;
    while (i < T->ntok) {
        int j = i, lw = 0;
        while (j < T->ntok) {
            const int add = T->tok[j].w + (j > i && T->tok[j].sp ? spw : 0);
            if (j > i && lw + add > w) {
                // a token with no space before it (a comma after a formula) stays with the token before it
                int g = j;
                while (g > i + 1 && !T->tok[g].sp) {
                    g--;
                }
                if (g < j && g > i) {
                    lw = 0;
                    for (int q = i; q < g; q++) {
                        lw += T->tok[q].w + (q > i && T->tok[q].sp ? spw : 0);
                    }
                    j = g;
                }
                break;
            }
            lw += add;
            j++;
        }
        int la = f->asc + 2, ld = f->line - f->asc - 2;
        for (int q = i; q < j; q++) {
            if (T->tok[q].kind == TK_MATH) {
                const tform_t *fo = &T->form[T->tok[q].form];
                if (fo->base0 + 1 > la) {
                    la = fo->base0 + 1;
                }
                if (fo->h - fo->base0 + 1 > ld) {
                    ld = fo->h - fo->base0 + 1;
                }
            }
        }
        y += la;
        tt_place_line(T, i, j, 0, w, y, spw, rtl);
        y += ld;
        if (lw > maxw) {
            maxw = lw;
        }
        lines++;
        i = j;
    }
    T->w = maxw;
    T->h = y;
    T->lines = lines;
    T->fit = y <= maxh;
    return T->fit;
}

// the text in a box of w x maxh: the biggest face from the index first that fits
static void tt_layout(ttext_t *T, const char *txt, int first, int w, int maxh, bool rtl)
{
    int n;
    const tface_t *L = face_list(&n);
    const int keep = s_ar->n, keepm = s_mbuf_n;
    for (int i = first; i < n; i++) {
        s_ar->n = keep;
        s_mbuf_n = keepm;
        if (tt_try(T, txt, L[i].f, L[i].level, w, maxh, rtl)) {
            return;
        }
    }
}

// the block at (x, y) (the top left of the box of width w of the layout)
static void tt_draw(const tarena_t *ar, const ttext_t *T, int x, int y, uint16_t col, int alpha)
{
    const int a = fa(alpha);
    for (int i = 0; i < T->ntok; i++) {
        const ttok_t *k = &T->tok[i];
        if (k->kind == TK_WORD) {
            kf_word(T->font, x + k->x, y + k->y, k->s, k->n, col, a);
        } else {
            const tform_t *fo = &T->form[k->form];
            t_draw(ar, fo, x + k->x, y + k->y - fo->base0, alpha, 60);
        }
    }
}
