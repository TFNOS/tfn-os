// INTEGRALS: the content (the data file IGD1 of tools/mk_data.py, deflated in ig_data.c). enter() inflates it
// into one PSRAM block; the tables point into that block.

extern const uint32_t ig_data_len, ig_data_zlen;
extern const uint8_t ig_data_z[];

#define IG_NS 160  // the samples of a curve (igc.py IG_NS)

typedef struct {
    uint32_t en, he, when_en, when_he, icon;
    uint8_t section, pad;
    uint16_t first, n;
    uint16_t pad2;
} igd_chapter_t;
typedef struct {
    uint32_t en, he, head;
    uint8_t kind, nvals, def, pad;
    uint32_t pname, first_var;
} igd_lesson_t;
typedef struct {
    uint32_t label, first_step;
    uint16_t nsteps;
    int16_t graph;
} igd_var_t;
typedef struct {
    uint32_t math, en, he;
} igd_step_t;
typedef struct {
    float x0, x1, y0, y1, lo, hi;
    uint32_t value, lo_lbl, hi_lbl;
    uint8_t kind, ncurve;
    uint16_t pad;
    int16_t s[2][IG_NS];
} igd_graph_t;
enum {
    IGK_AREA = 1,
    IGK_TWO = 2,
    IGK_INF_HI = 4,
    IGK_INF_LO = 8
};
enum {
    LK_IDEA,
    LK_EX,
    LK_GEM
};

static uint8_t *s_db;  // the inflated file
static int s_nch, s_nles;
static const igd_chapter_t *s_ch;
static const igd_lesson_t *s_les;
static const igd_var_t *s_var;
static const igd_step_t *s_step;
static const igd_graph_t *s_graph;
static const char *s_str;

static bool db_load(void)
{
    if (s_db) {
        return true;
    }
    s_db = heap_caps_malloc(ig_data_len + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_db) {
        return false;
    }
    tinfl_decompressor *d = malloc(sizeof(tinfl_decompressor));
    if (!d) {
        heap_caps_free(s_db);
        s_db = NULL;
        return false;
    }
    tinfl_init(d);
    size_t in = ig_data_zlen, got = ig_data_len;
    const int st = tinfl_decompress(d, ig_data_z, &in, s_db, s_db, &got,
                                    TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(d);
    if (st != TINFL_STATUS_DONE || got != ig_data_len || memcmp(s_db, "IGD1", 4)) {
        heap_caps_free(s_db);
        s_db = NULL;
        return false;
    }
    const uint8_t *p = s_db + 4;
    uint16_t nch, nles;
    uint32_t nvar, nstep, ngraph;
    memcpy(&nch, p, 2);
    memcpy(&nles, p + 2, 2);
    memcpy(&nvar, p + 4, 4);
    memcpy(&nstep, p + 8, 4);
    memcpy(&ngraph, p + 12, 4);
    p += 20;
    s_nch = nch;
    s_nles = nles;
    s_ch = (const igd_chapter_t *)p;
    p += sizeof(igd_chapter_t) * nch;
    s_les = (const igd_lesson_t *)p;
    p += sizeof(igd_lesson_t) * nles;
    s_var = (const igd_var_t *)p;
    p += sizeof(igd_var_t) * nvar;
    s_step = (const igd_step_t *)p;
    p += sizeof(igd_step_t) * nstep;
    s_graph = (const igd_graph_t *)p;
    p += sizeof(igd_graph_t) * ngraph;
    s_str = (const char *)p;
    return true;
}

static void db_free(void)
{
    heap_caps_free(s_db);
    s_db = NULL;
}

static inline const char *S_(uint32_t off)
{
    return s_str + off;
}
// a text in the language of the app
static inline const char *L_(uint32_t en, uint32_t he)
{
    return S_(s_he && he ? he : en);
}
static inline const igd_lesson_t *les_at(int ch, int li)
{
    return &s_les[s_ch[ch].first + li];
}
static inline const igd_var_t *var_at(int ch, int li, int vi)
{
    const igd_lesson_t *l = les_at(ch, li);
    return &s_var[l->first_var + (vi < l->nvals ? vi : 0)];
}
static inline const igd_step_t *step_at(const igd_var_t *v, int si)
{
    return &s_step[v->first_step + (si < v->nsteps ? si : v->nsteps - 1)];
}
static inline const igd_graph_t *graph_of(const igd_var_t *v)
{
    return v->graph >= 0 ? &s_graph[v->graph] : NULL;
}
