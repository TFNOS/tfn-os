// FOUR SEASONS: the core (included by app_seasons.c). The scene files of the SD card (tools/bake.py: per season a
// palette and a picture of 1 byte a pixel, and the layers of the things that change), the loader that works a little in
// each frame, the composer (the base picture + the layers of the values of the things now), and the drawing of a scene
// into the frame: the plain copy through the palette, the blend of two seasons, the zoom and the round wipe of a change
// of place.

// the frame (enter() takes gfx_w x gfx_h) and the band of the scene in it: its top row VY and its height VH (the 3.49:
// the whole 640 x 172; a narrow frame of 320 x 240: 180 rows from row 30, with a bar over it and a bar under it)
static int SW = 640, SH = 172, VY = 0, VH = 172;
#define NARROW (SW < 400)
#define NSEA   4

#ifdef ESP_PLATFORM
#define FS_DIR BOARD_SD_ROOT "/seasons/"
static void *fs_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
}
#else
#ifndef FS_DIR
#define FS_DIR "w211_seasons/out/seasons/"
#endif
static void *fs_alloc(size_t n)
{
    return malloc(n);
}
#endif
static int64_t fs_us(void)
{
    return esp_timer_get_time();
}

static inline int clampi(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}
static inline float smooth01(float t)
{
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}
static inline float absf(float v)
{
    return v < 0 ? -v : v;
}
static uint32_t s_rng = 2026;
static inline uint32_t rnd32(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static inline float rndf(void)
{
    return (rnd32() & 0xFFFFFF) / 16777216.0f;
}
static inline float rndr(float a, float b)
{
    return a + (b - a) * rndf();
}
static inline uint16_t rgbc(int r, int g, int b)
{
    return rgb((uint8_t)clampi(r, 0, 255), (uint8_t)clampi(g, 0, 255), (uint8_t)clampi(b, 0, 255));
}
static inline void unrgb(uint16_t p, int *r, int *g, int *b)
{
    uint16_t c = (uint16_t)((p >> 8) | (p << 8));
    *r = ((c >> 11) & 31) << 3;
    *g = ((c >> 5) & 63) << 2;
    *b = (c & 31) << 3;
}
// a panel colour in the wide form of a blend (the fields of RGB565 with room between them)
static inline uint32_t wide565(uint16_t p)
{
    uint32_t c = (uint16_t)((p >> 8) | (p << 8));
    return (c | (c << 16)) & 0x07E0F81Fu;
}
static inline uint16_t narrow565(uint32_t r)
{
    r &= 0x07E0F81Fu;
    uint16_t o = (uint16_t)(r | (r >> 16));
    return (uint16_t)((o >> 8) | (o << 8));
}
// a blend of two panel colours (k 0..32)
static inline uint16_t mix565(uint16_t a, uint16_t b, int k)
{
    uint32_t x = wide565(a), y = wide565(b);
    return narrow565((x * (uint32_t)(32 - k) + y * (uint32_t)k) >> 5);
}
static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static char s_err[96];

// ================================================================ the scene files
#define FS_MAXVAR 8
#define FS_HEAD   20
typedef struct {
    uint8_t season, var, val, has;
    uint16_t x, y, w, h;
    uint32_t off, zlen;
} layer_t;
typedef struct {
    int id;  // the scene (-1: none)
    int w, h;
    uint8_t *file;  // the whole file (the blobs stay for a new compose)
    uint32_t len;
    int nvar, nlayer;
    uint32_t pal_off[NSEA], base_off[NSEA], base_zlen[NSEA];
    layer_t *layer;
    uint8_t *pic[NSEA];       // the composed pictures (w x h, 1 byte a pixel)
    uint16_t pal[NSEA][256];  // the palette of each season, panel colours
    uint8_t rgbpal[NSEA][256][3];
    uint8_t val[FS_MAXVAR];  // the values of the things in the composed pictures
    uint8_t dirty;           // bit s: season s must compose again
    bool ready;
} sset_t;
static sset_t s_set[2];
static tinfl_decompressor *s_infl;
static uint8_t *s_ltmp;  // a layer before it goes on the picture
static uint32_t s_ltmp_n;
#define LTMP_MAX (720 * 190)

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static bool inflate_to(const uint8_t *z, size_t zn, uint8_t *out, size_t outn)
{
    if (!s_infl) {
        return false;
    }
    tinfl_init(s_infl);
    size_t in = zn, o = outn;
    tinfl_status st = tinfl_decompress(s_infl, z, &in, out, out, &o,
                                       TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    return st == TINFL_STATUS_DONE && o == outn;
}

static void set_free(sset_t *S)
{
    for (int s = 0; s < NSEA; s++) {
        free(S->pic[s]);
        S->pic[s] = NULL;
    }
    free(S->file);
    S->file = NULL;
    free(S->layer);
    S->layer = NULL;
    S->id = -1;
    S->ready = false;
    S->w = S->h = 0;
}

// the header of a file in S->file: the sizes, the seasons, the layers
static bool set_parse(sset_t *S)
{
    const uint8_t *b = S->file;
    if (S->len < FS_HEAD + NSEA * 24 || memcmp(b, "FSN1", 4)) {
        return false;
    }
    int w = rd16(b + 8), h = rd16(b + 10);
    S->nvar = rd16(b + 12);
    S->nlayer = rd16(b + 14);
    if (w < SW || h < VH || w > 1024 || h > 256 || S->nvar > FS_MAXVAR || S->nlayer > 256) {
        return false;
    }
    if (S->len < (uint32_t)(FS_HEAD + NSEA * 24 + S->nlayer * 24)) {
        return false;
    }
    // (the pictures are kept between scenes of the same size)
    if (S->w != w || S->h != h) {
        for (int s = 0; s < NSEA; s++) {
            free(S->pic[s]);
            S->pic[s] = NULL;
        }
    }
    S->w = w;
    S->h = h;
    for (int s = 0; s < NSEA; s++) {
        const uint8_t *q = b + FS_HEAD + s * 24;
        S->pal_off[s] = rd32(q);
        S->base_off[s] = rd32(q + 4);
        S->base_zlen[s] = rd32(q + 8);
        if (S->pal_off[s] + 768 > S->len || S->base_off[s] + S->base_zlen[s] > S->len) {
            return false;
        }
        for (int i = 0; i < 256; i++) {
            const uint8_t *c = b + S->pal_off[s] + i * 3;
            S->rgbpal[s][i][0] = c[0];
            S->rgbpal[s][i][1] = c[1];
            S->rgbpal[s][i][2] = c[2];
            S->pal[s][i] = rgb(c[0], c[1], c[2]);
        }
        if (!S->pic[s]) {
            S->pic[s] = fs_alloc((size_t)w * h);
        }
        if (!S->pic[s]) {
            return false;
        }
    }
    free(S->layer);
    S->layer = calloc((size_t)(S->nlayer > 0 ? S->nlayer : 1), sizeof(layer_t));
    if (!S->layer) {
        return false;
    }
    for (int i = 0; i < S->nlayer; i++) {
        const uint8_t *q = b + FS_HEAD + NSEA * 24 + i * 24;
        layer_t *L = &S->layer[i];
        L->season = q[0];
        L->var = q[1];
        L->val = q[2];
        L->has = q[3];
        L->x = rd16(q + 4);
        L->y = rd16(q + 6);
        L->w = rd16(q + 8);
        L->h = rd16(q + 10);
        L->off = rd32(q + 12);
        L->zlen = rd32(q + 16);
        if (L->season >= NSEA || L->var >= S->nvar) {
            return false;
        }
        if (L->has &&
            (L->off + L->zlen > S->len || L->x + L->w > w || L->y + L->h > h || (uint32_t)L->w * L->h > LTMP_MAX)) {
            return false;
        }
    }
    return true;
}

// one season of a set: the base picture, then the layer of each thing whose value is not its first value
static bool set_compose(sset_t *S, int s, const uint8_t *vals)
{
    if (!inflate_to(S->file + S->base_off[s], S->base_zlen[s], S->pic[s], (size_t)S->w * S->h)) {
        return false;
    }
    for (int i = 0; i < S->nlayer; i++) {
        const layer_t *L = &S->layer[i];
        if (L->season != s || !L->has || L->val == 0 || vals[L->var] != L->val) {
            continue;
        }
        uint32_t n = (uint32_t)L->w * L->h;
        if (n > s_ltmp_n) {
            continue;
        }
        if (!inflate_to(S->file + L->off, L->zlen, s_ltmp, n)) {
            return false;
        }
        const uint8_t *src = s_ltmp;
        for (int y = 0; y < L->h; y++) {
            uint8_t *d = S->pic[s] + (size_t)(L->y + y) * S->w + L->x;
            for (int x = 0; x < L->w; x++, src++) {
                if (*src) {
                    d[x] = *src;
                }
            }
        }
    }
    return true;
}

// ================================================================ the loader (a little work in each frame)
enum {
    LS_IDLE,
    LS_READ,
    LS_PARSE,
    LS_SEASON,
    LS_DONE,
    LS_FAIL
};
static struct {
    int st, set, scene, season;
    FILE *f;
    uint32_t pos;
    uint8_t vals[FS_MAXVAR];
    int64_t t0;
} s_ld = {LS_IDLE};
static int s_load_ms;  // the time of the last load (for the log)

static void load_begin(int set, int scene, const uint8_t *vals)
{
    if (s_ld.f) {
        fclose(s_ld.f);
        s_ld.f = NULL;
    }
    sset_t *S = &s_set[set];
    S->ready = false;
    S->id = -1;
    s_ld.set = set;
    s_ld.scene = scene;
    s_ld.season = 0;
    s_ld.pos = 0;
    memcpy(s_ld.vals, vals, FS_MAXVAR);
    s_ld.t0 = fs_us();
    char p[80];
    snprintf(p, sizeof p, FS_DIR "s%02d.fsn", scene);
    s_ld.f = fopen(p, "rb");
    if (!s_ld.f) {
        snprintf(s_err, sizeof s_err, "NO FILE: %s", p);
        s_ld.st = LS_FAIL;
        return;
    }
    fseek(s_ld.f, 0, SEEK_END);
    long n = ftell(s_ld.f);
    fseek(s_ld.f, 0, SEEK_SET);
    if (n <= 0 || n > 4 * 1024 * 1024) {
        fclose(s_ld.f);
        s_ld.f = NULL;
        snprintf(s_err, sizeof s_err, "BAD FILE: %s", p);
        s_ld.st = LS_FAIL;
        return;
    }
    free(S->file);
    S->file = fs_alloc((size_t)n);
    S->len = (uint32_t)n;
    if (!S->file) {
        fclose(s_ld.f);
        s_ld.f = NULL;
        snprintf(s_err, sizeof s_err, "NO MEMORY FOR %s", p);
        s_ld.st = LS_FAIL;
        return;
    }
    s_ld.st = LS_READ;
}

// work until the time budget (us) is used; true = the load is complete (or failed)
static bool load_step(int64_t budget)
{
    int64_t t_end = fs_us() + budget;
    while (s_ld.st != LS_IDLE && s_ld.st != LS_DONE && s_ld.st != LS_FAIL) {
        sset_t *S = &s_set[s_ld.set];
        if (s_ld.st == LS_READ) {
            uint32_t n = S->len - s_ld.pos;
            if (n > 16384) {
                n = 16384;
            }
            if (fread(S->file + s_ld.pos, 1, n, s_ld.f) != n) {
                snprintf(s_err, sizeof s_err, "A READ FAILED (SCENE %d)", s_ld.scene);
                s_ld.st = LS_FAIL;
                break;
            }
            s_ld.pos += n;
            if (s_ld.pos >= S->len) {
                fclose(s_ld.f);
                s_ld.f = NULL;
                s_ld.st = LS_PARSE;
            }
        } else if (s_ld.st == LS_PARSE) {
            if (!set_parse(S)) {
                snprintf(s_err, sizeof s_err, "BAD DATA OR NO MEMORY (SCENE %d)", s_ld.scene);
                s_ld.st = LS_FAIL;
                break;
            }
            s_ld.st = LS_SEASON;
        } else if (s_ld.st == LS_SEASON) {
            if (!set_compose(S, s_ld.season, s_ld.vals)) {
                snprintf(s_err, sizeof s_err, "BAD PICTURE (SCENE %d)", s_ld.scene);
                s_ld.st = LS_FAIL;
                break;
            }
            if (++s_ld.season >= NSEA) {
                memcpy(S->val, s_ld.vals, FS_MAXVAR);
                S->id = s_ld.scene;
                S->dirty = 0;
                S->ready = true;
                s_ld.st = LS_DONE;
                s_load_ms = (int)((fs_us() - s_ld.t0) / 1000);
            }
        }
        if (fs_us() >= t_end) {
            break;
        }
    }
    if (s_ld.st == LS_FAIL && s_ld.f) {
        fclose(s_ld.f);
        s_ld.f = NULL;
    }
    return s_ld.st == LS_DONE || s_ld.st == LS_FAIL || s_ld.st == LS_IDLE;
}
static void load_cancel(void)
{
    if (s_ld.f) {
        fclose(s_ld.f);
        s_ld.f = NULL;
    }
    if (s_ld.st != LS_IDLE && s_ld.st != LS_DONE) {
        s_set[s_ld.set].ready = false;
        s_set[s_ld.set].id = -1;
    }
    s_ld.st = LS_IDLE;
}

// the things changed: compose again (the season s at once, the others in the next frames)
static void set_revalue(sset_t *S, const uint8_t *vals, int s_now)
{
    if (!S->ready || !memcmp(S->val, vals, FS_MAXVAR)) {
        return;
    }
    memcpy(S->val, vals, FS_MAXVAR);
    S->dirty = 0x0F;
    if (s_now >= 0 && s_now < NSEA) {
        set_compose(S, s_now, vals);
        S->dirty &= (uint8_t)~(1 << s_now);
    }
}
static void set_tick(sset_t *S)
{
    if (!S->ready || !S->dirty) {
        return;
    }
    for (int s = 0; s < NSEA; s++) {
        if (S->dirty & (1 << s)) {
            set_compose(S, s, S->val);
            S->dirty &= (uint8_t)~(1 << s);
            return;
        }
    }
}

// ================================================================ the drawing of a scene into the frame
// The frame has rows of SW px; the picture (w x h) shows from (ox, oy). The palette of the season; the blend: season b
// with the weight k (0..32).
static void draw_plain(const sset_t *S, int s, int ox, int oy)
{
    uint16_t lut[256];
    memcpy(lut, S->pal[s], sizeof lut);
    ox = clampi(ox, 0, S->w - SW);
    oy = clampi(oy, 0, S->h - VH);
    for (int y = 0; y < VH; y++) {
        const uint8_t *src = S->pic[s] + (size_t)(oy + y) * S->w + ox;
        uint32_t *d = (uint32_t *)(g_fb + (VY + y) * SW);
        for (int x = 0; x < SW / 2; x++, src += 2) {
            d[x] = (uint32_t)lut[src[0]] | ((uint32_t)lut[src[1]] << 16);
        }
    }
}
static void draw_blend(const sset_t *S, int sa, int sb, int k, int ox, int oy)
{
    if (k <= 0) {
        draw_plain(S, sa, ox, oy);
        return;
    }
    if (k >= 32) {
        draw_plain(S, sb, ox, oy);
        return;
    }
    uint32_t wa[256], wb[256];
    for (int i = 0; i < 256; i++) {
        wa[i] = wide565(S->pal[sa][i]) * (uint32_t)(32 - k);
        wb[i] = wide565(S->pal[sb][i]) * (uint32_t)k;
    }
    ox = clampi(ox, 0, S->w - SW);
    oy = clampi(oy, 0, S->h - VH);
    for (int y = 0; y < VH; y++) {
        const uint8_t *pa = S->pic[sa] + (size_t)(oy + y) * S->w + ox;
        const uint8_t *pb = S->pic[sb] + (size_t)(oy + y) * S->w + ox;
        uint16_t *d = g_fb + (VY + y) * SW;
        for (int x = 0; x < SW; x++) {
            d[x] = narrow565((wa[pa[x]] + wb[pb[x]]) >> 5);
        }
    }
}
// a zoom of the picture: the picture point (cx, cy) shows at the frame point (fx, fy), z >= 1 (rows of the frame
// y0..y1; the columns x0..x1 only, the rest stays)
static void draw_zoom_rows(const sset_t *S, int sa, int sb, int k, float cx, float cy, float fx, float fy, float z,
                           int y0, int y1)
{
    uint32_t wa[256], wb[256];
    uint16_t lut[256];
    bool bl = k > 0 && k < 32;
    if (bl) {
        for (int i = 0; i < 256; i++) {
            wa[i] = wide565(S->pal[sa][i]) * (uint32_t)(32 - k);
            wb[i] = wide565(S->pal[sb][i]) * (uint32_t)k;
        }
    } else {
        memcpy(lut, S->pal[k >= 32 ? sb : sa], sizeof lut);
    }
    const uint8_t *pic = S->pic[k >= 32 ? sb : sa];
    float iz = 1.0f / z;
    int32_t step = (int32_t)(iz * 65536.0f);
    for (int y = y0; y < y1; y++) {
        int sy = clampi((int)(cy + (y + 0.5f - fy) * iz), 0, S->h - 1);
        int32_t sx = (int32_t)((cx + (0.5f - fx) * iz) * 65536.0f);
        uint16_t *d = g_fb + (VY + y) * SW;
        if (bl) {
            const uint8_t *ra = S->pic[sa] + (size_t)sy * S->w, *rb = S->pic[sb] + (size_t)sy * S->w;
            for (int x = 0; x < SW; x++, sx += step) {
                int i = clampi(sx >> 16, 0, S->w - 1);
                d[x] = narrow565((wa[ra[i]] + wb[rb[i]]) >> 5);
            }
        } else {
            const uint8_t *r = pic + (size_t)sy * S->w;
            for (int x = 0; x < SW; x++, sx += step) {
                d[x] = lut[r[clampi(sx >> 16, 0, S->w - 1)]];
            }
        }
    }
}
// the round wipe: inside the circle (fx, fy, r) the scene B (plain or blend), outside it what is in the frame
static void draw_circle_in(const sset_t *S, int sa, int sb, int k, int ox, int oy, float fx, float fy, float r)
{
    uint32_t wa[256], wb[256];
    uint16_t lut[256];
    bool bl = k > 0 && k < 32;
    if (bl) {
        for (int i = 0; i < 256; i++) {
            wa[i] = wide565(S->pal[sa][i]) * (uint32_t)(32 - k);
            wb[i] = wide565(S->pal[sb][i]) * (uint32_t)k;
        }
    } else {
        memcpy(lut, S->pal[k >= 32 ? sb : sa], sizeof lut);
    }
    ox = clampi(ox, 0, S->w - SW);
    oy = clampi(oy, 0, S->h - VH);
    for (int y = 0; y < VH; y++) {
        float dy = y + 0.5f - fy;
        float q = r * r - dy * dy;
        if (q <= 0) {
            continue;
        }
        float hw = sqrtf(q);
        int x0 = clampi((int)(fx - hw), 0, SW), x1 = clampi((int)(fx + hw) + 1, 0, SW);
        uint16_t *d = g_fb + (VY + y) * SW;
        size_t row = (size_t)(oy + y) * S->w + ox;
        if (bl) {
            const uint8_t *pa = S->pic[sa] + row, *pb = S->pic[sb] + row;
            for (int x = x0; x < x1; x++) {
                d[x] = narrow565((wa[pa[x]] + wb[pb[x]]) >> 5);
            }
        } else {
            const uint8_t *p = S->pic[k >= 32 ? sb : sa] + row;
            for (int x = x0; x < x1; x++) {
                d[x] = lut[p[x]];
            }
        }
    }
}
