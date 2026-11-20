// WIKI: the HTML of an article (MediaWiki, as in the ZIM files of Kiwix) to a document of words for the small
// screen. Only app_wiki.c includes this file.
// The task: the app task of TFN OS (an article opens).
// The memory: the arrays of the document (wdoc_t) are in PSRAM; doc_free() frees them.
//
// The document: blocks (a paragraph, a heading, a list item) of words; each word has its text, its link (a page of the
// file) and its marks (bold, a left-to-right word, glued to the word before it). Tables (the infobox), pictures,
// reference marks, edit links, navigation boxes and the reference lists are not in it (the Tab5 shows the
// pictures of the list d->pic: wk_pics.inc).
// The characters that the Rubik fonts do not have are changed (a dash, the quotes) or dropped (the vowel points).

enum {
    BK_P,
    BK_H2,
    BK_H3,
    BK_LI,
    BK_DD
};
enum {
    WF_BOLD = 1,
    WF_LTR = 2,
    WF_GLUE = 4,
    WF_NEUT = 8,
    WF_NUM = 16
};  // (NUM: a number: left to right, but for
// the neutral words it counts as right to left)     // (NEUT: only signs: it takes the direction around it)

typedef struct {
    uint32_t off;     // the text of the word in d_tx
    uint16_t len, w;  // its bytes; its width in px (the layout)
    uint16_t link;    // 0: none, else the link number + 1
    uint8_t flags;
    uint8_t blk;  // the kind of the block (BK_*)
    int16_t x;    // its left end on its line (the layout)
} wword_t;

#if BOARD_UI_TABLET
// The pictures of the article: each <img> of 100 x 60 px of the page or more (not
// the icons, the small flags, the pencils), also in the tables and the frames that the text skips, in the order of
// the page; each shows before the block that comes after it.
#define WK_PICS 24
typedef struct {
    char url[ZIM_URL_MAX];  // the entry of the picture (namespace C)
    uint16_t w, h;          // its size in the page (CSS px)
    uint32_t blk;           // it shows before this block
} wpic_t;
#endif

typedef struct {
    char *tx;  // the text of the words, one after the other (no 0 between them)
    uint32_t ntx, tx_cap;
    wword_t *w;
    uint32_t nw, w_cap;
    uint32_t *blk_first;  // the first word of each block; the kind is in the words
    uint32_t nblk, blk_cap;
    char *lk;  // the URLs of the links, each with its 0
    uint32_t nlk_bytes, lk_cap;
    uint32_t *lk_off;
    uint32_t nlk, lk_n_cap;
    bool cut;  // the article was too long: its end is not in the document
#if BOARD_UI_TABLET
    wpic_t *pic;
    int npic;
#endif
} wdoc_t;

static void doc_free(wdoc_t *d)
{
    free(d->tx);
    free(d->w);
    free(d->blk_first);
    free(d->lk);
    free(d->lk_off);
#if BOARD_UI_TABLET
    free(d->pic);
#endif
    memset(d, 0, sizeof *d);
}

static void *wk_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}

static bool doc_init(wdoc_t *d, uint32_t html_len)
{
    memset(d, 0, sizeof *d);
    d->tx_cap = html_len / 3 + 16384;  // (the text of an article is about a fifth of its HTML)
    if (d->tx_cap > 512u * 1024u) {
        d->tx_cap = 512u * 1024u;
    }
    d->w_cap = d->tx_cap / 5 + 256;
    d->blk_cap = 4096;
    d->lk_cap = 96u * 1024u;
    d->lk_n_cap = 4096;
    d->tx = wk_alloc(d->tx_cap);
    d->w = wk_alloc(d->w_cap * sizeof(wword_t));
    d->blk_first = wk_alloc(d->blk_cap * sizeof(uint32_t));
    d->lk = wk_alloc(d->lk_cap);
    d->lk_off = wk_alloc(d->lk_n_cap * sizeof(uint32_t));
#if BOARD_UI_TABLET
    d->pic = wk_alloc(WK_PICS * sizeof(wpic_t));
    if (!d->pic) {
        doc_free(d);
        return false;
    }
#endif
    if (!d->tx || !d->w || !d->blk_first || !d->lk || !d->lk_off) {
        doc_free(d);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- the converter state
typedef struct {
    wdoc_t *d;
    int blk;       // the kind of the block that is open (-1: none)
    bool in_word;  // a word is open (its bytes are at the end of d->tx)
    uint32_t word_off;
    uint16_t cur_link;  // the link of the text now (0: none)
    int bold;           // the depth of <b> / <strong>
    bool glue_next;     // the next word joins the word before it (an attribute changed inside a word)
    uint16_t word_link;
    uint8_t word_flags;
    uint8_t mark;  // a direction mark before the next word: 0 none, 1 LRM (left to right), 2 RLM
} conv_t;

// a word takes the direction of a mark (1 LRM, 2 RLM): a sign word becomes strong; a number stays a number (RLM: it is
// not part of a Latin run). A Latin or a Hebrew word keeps its own direction.
static void mark_word(wword_t *w, uint8_t m)
{
    if (w->flags & WF_NEUT) {
        w->flags &= (uint8_t)~WF_NEUT;
        if (m == 1) {
            w->flags |= WF_LTR;
        } else {
            w->flags &= (uint8_t)~WF_LTR;
        }
    } else if ((w->flags & WF_NUM) && m == 2) {
        w->flags &= (uint8_t)~WF_LTR;
    }
}

static void word_end(conv_t *c)
{
    wdoc_t *d = c->d;
    if (!c->in_word) {
        return;
    }
    c->in_word = false;
    uint32_t len = d->ntx - c->word_off;
    if (!len || d->nw >= d->w_cap) {
        return;
    }
    if (c->blk < 0) {  // (text outside a block: a paragraph)
        c->blk = BK_P;
        if (d->nblk < d->blk_cap) {
            d->blk_first[d->nblk++] = d->nw;
        }
    }
    wword_t *w = &d->w[d->nw++];
    memset(w, 0, sizeof *w);
    w->off = c->word_off;
    w->len = (uint16_t)(len > 65535 ? 65535 : len);
    w->link = c->word_link;
    w->flags = c->word_flags;
    w->blk = (uint8_t)c->blk;
    // a word is left to right when its first letter is Latin or a digit; only signs: neutral
    bool sign_only = true;
    for (uint32_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)d->tx[c->word_off + i];
        bool an = ch >= 0x80 || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        if (an) {
            sign_only = false;
        }
        if (ch >= 0x80) {
            break;
        }
        if (an) {
            w->flags |= WF_LTR;
            if (ch >= '0' && ch <= '9') {
                w->flags |= WF_NUM;
            }
            break;
        }
    }
    if (sign_only) {
        w->flags = (uint8_t)((w->flags & ~WF_LTR) | WF_NEUT);
        if (c->mark) {
            mark_word(w, c->mark);
        }
        c->mark = 0;
        return;
    }
    if (c->mark) {
        mark_word(w, c->mark);
    }
    c->mark = 0;
    // the end signs of a Latin word ("Israel," "2023.") go to a word of their own: in a Hebrew line they stand at its
    // left, as the direction of the paragraph tells
    if ((w->flags & WF_LTR) && len > 1 && d->nw < d->w_cap) {
        uint32_t k = len;
        while (k > 1 && strchr(",.;:!?", d->tx[c->word_off + k - 1])) {
            k--;
        }
        if (k < len) {
            w->len = (uint16_t)k;
            wword_t *s = &d->w[d->nw++];
            *s = *w;
            s->off = c->word_off + k;
            s->len = (uint16_t)(len - k);
            s->flags = (uint8_t)((w->flags & WF_BOLD) | WF_GLUE | WF_NEUT);
        }
    }
}

static void block_end(conv_t *c)
{
    word_end(c);
    c->blk = -1;
    c->glue_next = false;
    c->mark = 0;
}

// a direction mark (LRM, RLM): it ends the word before it (the rest of the word joins it again: glued); a sign word
// just before the mark takes its direction, and the next word too (mark_word)
static void mark_dir(conv_t *c, uint8_t m)
{
    wdoc_t *d = c->d;
    bool inside = c->in_word;
    word_end(c);
    if (inside) {
        c->glue_next = true;
    }
    if (d->nw && d->nblk && d->nw - 1 >= d->blk_first[d->nblk - 1]) {
        wword_t *w = &d->w[d->nw - 1];
        if (w->flags & WF_NEUT) {
            mark_word(w, m);
        }
    }
    c->mark = m;
}

static void block_start(conv_t *c, int kind)
{
    block_end(c);
    c->blk = kind;
    if (c->d->nblk < c->d->blk_cap) {
        c->d->blk_first[c->d->nblk++] = c->d->nw;
    }
}

// the attributes of the text changed (a link, bold): a word that is open ends; its next part joins it
static void attr_change(conv_t *c)
{
    if (c->in_word) {
        word_end(c);
        c->glue_next = true;
    }
}

static void put_byte(conv_t *c, char ch)
{
    wdoc_t *d = c->d;
    if (d->ntx >= d->tx_cap) {
        d->cut = true;
        return;
    }
    if (!c->in_word) {
        c->in_word = true;
        c->word_off = d->ntx;
        c->word_link = c->cur_link;
        c->word_flags = (uint8_t)((c->bold ? WF_BOLD : 0) | (c->glue_next ? WF_GLUE : 0));
        c->glue_next = false;
    }
    d->tx[d->ntx++] = ch;
}

static void put_space(conv_t *c)
{
    word_end(c);
    c->glue_next = false;
}

// one code point of the text (the changes for the font)
static void put_cp(conv_t *c, uint32_t cp)
{
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0 || cp == 0x2009 || cp == 0x202F) {
        put_space(c);
        return;
    }
    if (cp < 0x20) {
        return;
    }
    if (cp < 0x7F) {
        if (strchr("()[]{}\"", (int)cp)) {  // (a bracket or a quote: a neutral word of its own)
            bool glue = c->in_word || c->glue_next;
            word_end(c);
            c->glue_next = glue;
            put_byte(c, (char)cp);
            word_end(c);
            c->glue_next = true;
            return;
        }
        put_byte(c, (char)cp);
        return;
    }
    // the direction marks LRM, RLM: no letter, but a direction for the words next to them (mark_dir)
    if (cp == 0x200E || cp == 0x200F) {
        mark_dir(c, cp == 0x200F ? 2 : 1);
        return;
    }
    // the vowel points and the cantillation marks, the other direction marks, the soft hyphen: dropped
    if ((cp >= 0x591 && cp <= 0x5C7 && cp != 0x5BE) || (cp >= 0x200B && cp <= 0x200F) ||
        (cp >= 0x202A && cp <= 0x202E) || cp == 0xAD || cp == 0x2060 || cp == 0xFEFF) {
        return;
    }
    uint32_t to = cp;
    if (cp == 0x5BE || cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2212) {
        to = '-';
    } else if (cp == 0x2018 || cp == 0x201B || cp == 0x2032) {
        to = 0x2019;
    } else if (cp == 0x201E || cp == 0x201F || cp == 0x2033 || cp == 0xAB || cp == 0xBB) {
        to = '"';
    } else if (cp == 0xD7) {
        to = 'x';
    } else if (cp == 0xB0) {
        put_byte(c, 'o');
        return;
    }
    bool known = (to >= 0x5D0 && to <= 0x5EA) || to == 0x5F3 || to == 0x5F4 || to == 0x2013 || to == 0x2014 ||
                 to == 0x2019 || to == 0x201C || to == 0x201D || to == 0x2026;
    if (to < 0x80) {
        put_byte(c, (char)to);
        return;
    }
    if (!known) {
        return;  // (a letter of another script: the font has none)
    }
    if (to < 0x800) {
        put_byte(c, (char)(0xC0 | (to >> 6)));
        put_byte(c, (char)(0x80 | (to & 0x3F)));
    } else {
        put_byte(c, (char)(0xE0 | (to >> 12)));
        put_byte(c, (char)(0x80 | ((to >> 6) & 0x3F)));
        put_byte(c, (char)(0x80 | (to & 0x3F)));
    }
}

// ---------------------------------------------------------------- the HTML
static bool name_is(const char *n, int nl, const char *s)
{
    return (int)strlen(s) == nl && !strncasecmp(n, s, (size_t)nl);
}

// the value of an attribute in the text of a tag (t .. e) into out; false: not there
static bool attr(const char *t, const char *e, const char *name, char *out, int max)
{
    size_t nl = strlen(name);
    for (const char *p = t; p + nl + 2 < e; p++) {
        if ((p == t || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n') && !strncasecmp(p, name, nl) && p[nl] == '=') {
            const char *v = p + nl + 1;
            char q = (*v == '"' || *v == '\'') ? *v++ : ' ';
            int i = 0;
            while (v < e && *v != q && *v != '>' && i < max - 1) {
                out[i++] = *v++;
            }
            out[i] = 0;
            return true;
        }
    }
    return false;
}

static bool class_has(const char *cls, const char *word)
{
    size_t n = strlen(word);
    for (const char *p = cls; (p = strstr(p, word)); p += n) {
        if ((p == cls || p[-1] == ' ') && (p[n] == 0 || p[n] == ' ')) {
            return true;
        }
    }
    return false;
}

static int hexv(char h)
{
    if (h >= '0' && h <= '9') {
        return h - '0';
    }
    if (h >= 'a' && h <= 'f') {
        return h - 'a' + 10;
    }
    if (h >= 'A' && h <= 'F') {
        return h - 'A' + 10;
    }
    return -1;
}

// a link of an article to the URL of its entry: "./Tel_Aviv#x" -> "Tel_Aviv" (decoded); 0: not a page of the file
static uint16_t add_link(conv_t *c, const char *href)
{
    wdoc_t *d = c->d;
    if (!href[0] || href[0] == '#' || strstr(href, "://") || !strncmp(href, "//", 2) || !strncmp(href, "mailto:", 7)) {
        return 0;
    }
    const char *h = href;
    while (!strncmp(h, "./", 2)) {
        h += 2;
    }
    while (!strncmp(h, "../", 3)) {
        h += 3;
    }
    char url[ZIM_URL_MAX];
    int n = 0;
    for (; *h && *h != '#' && *h != '?' && n < ZIM_URL_MAX - 1; h++) {
        if (*h == '%' && hexv(h[1]) >= 0 && hexv(h[2]) >= 0) {
            url[n++] = (char)(hexv(h[1]) * 16 + hexv(h[2]));
            h += 2;
        } else {
            url[n++] = *h;
        }
    }
    url[n] = 0;
    if (!n || d->nlk >= d->lk_n_cap || d->nlk_bytes + (uint32_t)n + 1 > d->lk_cap || d->nlk >= 65534) {
        return 0;
    }
    d->lk_off[d->nlk] = d->nlk_bytes;
    memcpy(d->lk + d->nlk_bytes, url, (size_t)n + 1);
    d->nlk_bytes += (uint32_t)n + 1;
    return (uint16_t)(++d->nlk);
}

static const char *link_url(const wdoc_t *d, uint16_t link)
{
    return link ? d->lk + d->lk_off[link - 1] : "";
}

// an entity at p (after '&'): its code point and the length of its text; 0: not an entity
static int entity(const char *p, const char *e, uint32_t *cp)
{
    static const struct {
        const char *n;
        uint32_t cp;
    } NAMED[] = {
            {"amp;", '&'},      {"lt;", '<'},       {"gt;", '>'},       {"quot;", '"'},      {"apos;", '\''},
            {"nbsp;", 0xA0},    {"ndash;", 0x2013}, {"mdash;", 0x2014}, {"hellip;", 0x2026}, {"rsquo;", 0x2019},
            {"lsquo;", 0x2018}, {"ldquo;", 0x201C}, {"rdquo;", 0x201D}, {"times;", 0xD7},    {"deg;", 0xB0},
            {"lrm;", 0x200E},   {"rlm;", 0x200F},   {"shy;", 0xAD},
    };
    if (p < e && *p == '#') {
        int base = 10, i = 1;
        if (p + 1 < e && (p[1] == 'x' || p[1] == 'X')) {
            base = 16;
            i = 2;
        }
        uint32_t v = 0;
        int digits = 0;
        while (p + i < e && i < 10) {
            int h = base == 16 ? hexv(p[i]) : (p[i] >= '0' && p[i] <= '9' ? p[i] - '0' : -1);
            if (h < 0) {
                break;
            }
            v = v * (uint32_t)base + (uint32_t)h;
            i++;
            digits++;
        }
        if (!digits || p + i >= e || p[i] != ';') {
            return 0;
        }
        *cp = v;
        return i + 1;
    }
    for (size_t k = 0; k < sizeof NAMED / sizeof NAMED[0]; k++) {
        size_t n = strlen(NAMED[k].n);
        if (p + n <= e && !strncmp(p, NAMED[k].n, n)) {
            *cp = NAMED[k].cp;
            return (int)n;
        }
    }
    return 0;
}

// a UTF-8 code point at p; its length in *n
static uint32_t utf8_cp(const unsigned char *p, const unsigned char *e, int *n)
{
    if (p[0] < 0x80 || p + 1 >= e) {
        *n = 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0) {
        *n = 2;
        return (uint32_t)(p[0] & 0x1F) << 6 | (p[1] & 0x3F);
    }
    if ((p[0] & 0xF0) == 0xE0 && p + 2 < e) {
        *n = 3;
        return (uint32_t)(p[0] & 0x0F) << 12 | (uint32_t)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
    }
    if ((p[0] & 0xF8) == 0xF0 && p + 3 < e) {
        *n = 4;
        return (uint32_t)(p[0] & 0x07) << 18 | (uint32_t)(p[1] & 0x3F) << 12 | (uint32_t)(p[2] & 0x3F) << 6 |
               (p[3] & 0x3F);
    }
    *n = 1;
    return 0xFFFD;
}

// the tags that are not in the document, with all that is in them
static bool skip_tag(const char *n, int nl, const char *t, const char *e)
{
    if (name_is(n, nl, "table") || name_is(n, nl, "style") || name_is(n, nl, "script") || name_is(n, nl, "figure") ||
        name_is(n, nl, "math") || name_is(n, nl, "noscript") || name_is(n, nl, "svg") || name_is(n, nl, "map") ||
        name_is(n, nl, "audio") || name_is(n, nl, "video") || name_is(n, nl, "header") || name_is(n, nl, "nav") ||
        name_is(n, nl, "footer") || name_is(n, nl, "gallery")) {
        return true;
    }
    char cls[160];
    if (!attr(t, e, "class", cls, sizeof cls)) {
        return false;
    }
    if (name_is(n, nl, "sup") && class_has(cls, "reference")) {
        return true;
    }
    if (name_is(n, nl, "span") && class_has(cls, "mw-editsection")) {
        return true;
    }
    if (name_is(n, nl, "ol") && class_has(cls, "references")) {
        return true;
    }
    static const char *const CLS[] = {"navbox",
                                      "reflist",
                                      "thumb",
                                      "toc",
                                      "metadata",
                                      "noprint",
                                      "mw-references-wrap",
                                      "infobox",
                                      "gallery",
                                      "mw-empty-elt",
                                      "sistersitebox",
                                      "dablink",
                                      "mw-kartographer-maplink",
                                      "references",
                                      "refbegin",
                                      NULL};
    for (int i = 0; CLS[i]; i++) {
        if (class_has(cls, CLS[i])) {
            return true;
        }
    }
    return false;
}

#if BOARD_UI_TABLET
// an <img> of the page into the list of the pictures (t..e: its attributes)
static void add_pic(wdoc_t *d, const char *t, const char *e)
{
    char src[ZIM_URL_MAX * 3], v[16];
    if (d->npic >= WK_PICS || !attr(t, e, "src", src, sizeof src)) {
        return;
    }
    int w = attr(t, e, "width", v, sizeof v) ? atoi(v) : 0, h = attr(t, e, "height", v, sizeof v) ? atoi(v) : 0;
    if (w < 100 || h < 60 || strstr(src, "://") || !strncmp(src, "//", 2)) {
        return;
    }
    const char *s = src;
    while (!strncmp(s, "./", 2)) {
        s += 2;
    }
    while (!strncmp(s, "../", 3)) {
        s += 3;
    }
    wpic_t *p = &d->pic[d->npic];
    int n = 0;
    for (; *s && *s != '#' && *s != '?' && n < ZIM_URL_MAX - 1; s++) {
        if (*s == '%' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) {
            p->url[n++] = (char)(hexv(s[1]) * 16 + hexv(s[2]));
            s += 2;
        } else {
            p->url[n++] = *s;
        }
    }
    p->url[n] = 0;
    if (!n) {
        return;
    }
    for (int i = 0; i < d->npic; i++) {
        if (!strcmp(d->pic[i].url, p->url)) {
            return;  // (one copy of a picture)
        }
    }
    p->w = (uint16_t)w;
    p->h = (uint16_t)h;
    p->blk = d->nblk;
    // before the first words (the infobox: a logo, a flag, a photo, a map): only one picture, the first that is 180 px
    // wide or more (the main photo), else the biggest
    if (d->nw == 0 && d->npic > 0) {
        const wpic_t *q = &d->pic[0];
        bool q_main = q->w >= 180, p_main = w >= 180;
        if ((p_main && !q_main) || (p_main == q_main && !q_main && (uint32_t)w * (uint32_t)h > (uint32_t)q->w * q->h)) {
            d->pic[0] = *p;
        }
        return;
    }
    d->npic++;
}
#endif

static bool void_tag(const char *n, int nl)
{
    return name_is(n, nl, "br") || name_is(n, nl, "img") || name_is(n, nl, "hr") || name_is(n, nl, "link") ||
           name_is(n, nl, "meta") || name_is(n, nl, "input") || name_is(n, nl, "area") || name_is(n, nl, "wbr") ||
           name_is(n, nl, "source") || name_is(n, nl, "col") || name_is(n, nl, "base");
}

// The '>' that ends the tag at p: a '>' in a quoted value ("..." or '...' after '=') is not the end (for example, the
// value of data-ooui has '>' in it, and its JSON must not come out as words of the text). A quote that does not close:
// the first '>'.
static const char *tag_end(const char *p, const char *e)
{
    char qc = 0;
    for (const char *s = p + 1; s < e; s++) {
        if (qc) {
            if (*s == qc) {
                qc = 0;
            }
        } else if ((*s == '"' || *s == '\'') && s[-1] == '=') {
            qc = *s;
        } else if (*s == '>') {
            return s;
        }
    }
    return memchr(p, '>', (size_t)(e - p));
}

// a tag of the name of the skipped tag (q: the '>' of the tag): a close ends one level, an open tag that is not
// self-closed (no "/>") starts one more level
static void skip_step(int *depth, bool close, const char *q)
{
    if (close) {
        (*depth)--;
    } else if (q[-1] != '/') {
        (*depth)++;
    }
}

// the first s in p .. e (NULL: none). The HTML has no 0 at its end (a blob of a zstd cluster: the next article
// follows), so strstr() would read past it
static const char *find_in(const char *p, const char *e, const char *s)
{
    const size_t n = strlen(s);
    for (; p + n <= e; p++) {
        if (*p == *s && !memcmp(p, s, n)) {
            return p;
        }
    }
    return NULL;
}

// the HTML of an article (len bytes) into d. false: no memory
static bool html_to_doc(const uint8_t *html, uint32_t len, wdoc_t *d)
{
    if (!doc_init(d, len)) {
        return false;
    }
    conv_t c = {0};
    c.d = d;
    c.blk = -1;
    const char *p = (const char *)html, *e = p + len;
    // the content: from the text of the page (not the title bar or the menus)
    const char *body = find_in(p, e, "id=\"mw-content-text\"");
    if (body) {
        while (body > p && *body != '<') {
            body--;
        }
        p = body;
    }
    char skip_name[16] = "";
    int skip_depth = 0;
    char href[ZIM_URL_MAX * 3];
    // the link stack: a link inside a link is rare; one level is enough
    while (p < e && !d->cut) {
        if (*p == '<') {
            if (p + 3 < e && !strncmp(p, "<!--", 4)) {  // a comment
                const char *q = find_in(p + 4, e, "-->");
                p = q ? q + 3 : e;
                continue;
            }
            const char *q = tag_end(p, e);
            if (!q) {
                break;
            }
            bool close = p[1] == '/';
            const char *n = p + 1 + (close ? 1 : 0);
            int nl = 0;
            while (n + nl < q && ((n[nl] >= 'a' && n[nl] <= 'z') || (n[nl] >= 'A' && n[nl] <= 'Z') ||
                                  (n[nl] >= '0' && n[nl] <= '9'))) {
                nl++;
            }
            const char *t = n + nl, *te = q;
            p = q + 1;
            if (!nl) {
                continue;
            }
#if BOARD_UI_TABLET
            if (!close && name_is(n, nl, "img")) {
                add_pic(d, t, te);  // (also in a table or a frame that is skipped)
            }
#endif
            if (skip_depth) {  // inside a tag that is not in the document
                if (name_is(n, nl, skip_name)) {
                    skip_step(&skip_depth, close, q);
                }
                continue;
            }
            if (!close && skip_tag(n, nl, t, te)) {
                if (void_tag(n, nl) || q[-1] == '/') {
                    continue;
                }
                snprintf(skip_name, sizeof skip_name, "%.*s", nl < 15 ? nl : 15, n);
                skip_depth = 1;
                attr_change(&c);
                continue;
            }
            if (name_is(n, nl, "p") || name_is(n, nl, "div") || name_is(n, nl, "blockquote") || name_is(n, nl, "dt") ||
                name_is(n, nl, "caption") || name_is(n, nl, "ul") || name_is(n, nl, "ol") || name_is(n, nl, "dl") ||
                name_is(n, nl, "section")) {
                block_end(&c);  // (the next text starts a new paragraph)
            } else if (nl == 2 && (n[0] == 'h' || n[0] == 'H') && n[1] >= '1' && n[1] <= '6') {
                if (close) {
                    block_end(&c);
                } else {
                    block_start(&c, n[1] <= '2' ? BK_H2 : BK_H3);
                }
            } else if (name_is(n, nl, "li")) {
                if (close) {
                    block_end(&c);
                } else {
                    block_start(&c, BK_LI);
                }
            } else if (name_is(n, nl, "dd")) {
                if (close) {
                    block_end(&c);
                } else {
                    block_start(&c, BK_DD);
                }
            } else if (name_is(n, nl, "br")) {
                int k = c.blk;
                block_end(&c);
                if (k >= 0) {
                    block_start(&c, k == BK_LI ? BK_DD : k);
                }
            } else if (name_is(n, nl, "a")) {
                attr_change(&c);
                if (close) {
                    c.cur_link = 0;
                } else {
                    c.cur_link = attr(t, te, "href", href, sizeof href) ? add_link(&c, href) : 0;
                }
            } else if (name_is(n, nl, "b") || name_is(n, nl, "strong")) {
                attr_change(&c);
                c.bold += close ? (c.bold > 0 ? -1 : 0) : 1;
            }
            continue;
        }
        if (skip_depth) {
            p++;
            continue;
        }
        if (*p == '&') {
            uint32_t cp;
            int n = entity(p + 1, e, &cp);
            if (n) {
                put_cp(&c, cp);
                p += n + 1;
                continue;
            }
        }
        int n;
        uint32_t cp = utf8_cp((const unsigned char *)p, (const unsigned char *)e, &n);
        put_cp(&c, cp);
        p += n;
    }
    block_end(&c);
    return true;
}
