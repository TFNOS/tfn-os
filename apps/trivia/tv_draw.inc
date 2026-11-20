// TRIVIA: the drawing (included by app_trivia.c): the background of the subject, boxes with soft edges and an alpha,
// the pictures (icons and the logo), the timer arc, the sparks and the confetti.

// ---------------------------------------------------------------- the pictures (tv_art.c)
typedef struct {
    int w, h;
    const uint16_t *c;
    const uint8_t *a;
} pic_t;
static uint8_t *s_art[TV_ART_SIZES + 1];  // the inflated streams (only the sizes of this frame)
static pic_t s_icon[TV_ART_SIZES][TV_ART_NICON], s_logo[TV_ART_NLOGO];

static bool inflate_to(const uint8_t *z, size_t zn, uint8_t *out, size_t n)
{
    tinfl_decompressor *d = malloc(sizeof(tinfl_decompressor));
    if (!d) {
        return false;
    }
    tinfl_init(d);
    size_t in = zn, got = n;
    const int st = tinfl_decompress(d, z, &in, out, out, &got,
                                    TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(d);
    return st == TINFL_STATUS_DONE && got == n;
}
static int pics_cut(const uint8_t *b, pic_t *p, int n)
{
    int off = 0;
    for (int i = 0; i < n; i++) {
        p[i].w = b[off] | b[off + 1] << 8;
        p[i].h = b[off + 2] | b[off + 3] << 8;
        off += 4;
        p[i].c = (const uint16_t *)(b + off);
        off += p[i].w * p[i].h * 2;
        p[i].a = b + off;
        off += p[i].w * p[i].h;
    }
    return off;
}
// inflate stream k (an icon size, or TV_ART_SIZES = the logos); false = no memory
static bool art_load(int k)
{
    if (s_art[k]) {
        return true;
    }
    s_art[k] = heap_caps_malloc(TV_ART_RAW[k] + 4, MALLOC_CAP_SPIRAM);
    if (!s_art[k] || !inflate_to(TV_ART_DATA[k], TV_ART_Z[k], s_art[k], TV_ART_RAW[k])) {
        free(s_art[k]);
        s_art[k] = NULL;
        return false;
    }
    if (k < TV_ART_SIZES) {
        pics_cut(s_art[k], s_icon[k], TV_ART_NICON);
    } else {
        pics_cut(s_art[k], s_logo, TV_ART_NLOGO);
    }
    return true;
}
static void art_free(void)
{
    for (int k = 0; k <= TV_ART_SIZES; k++) {
        free(s_art[k]);
        s_art[k] = NULL;
    }
}
// a picture with its centre at (cx, cy), at the alpha a
static void pic_draw(const pic_t *p, int cx, int cy, int a)
{
    if (!p || !p->c) {
        return;
    }
    const int x0 = cx - p->w / 2, y0 = cy - p->h / 2;
    for (int j = 0; j < p->h; j++) {
        const int y = y0 + j;
        if ((unsigned)y >= (unsigned)gfx_h) {
            continue;
        }
        const uint16_t *c = p->c + j * p->w;
        const uint8_t *al = p->a + j * p->w;
        for (int i = 0; i < p->w; i++) {
            const int v = al[i] * a / 255;
            if (v > 8) {
                px_a(x0 + i, y, c[i], v);
            }
        }
    }
}
// an icon: size index sz (TV_ART_PX), id TI_*
static void icon(int sz, int id, int cx, int cy, int a)
{
    if (sz < 0 || sz >= TV_ART_SIZES || id < 0 || id >= TV_ART_NICON || !s_art[sz]) {
        return;
    }
    pic_draw(&s_icon[sz][id], cx, cy, a);
}

// ---------------------------------------------------------------- the colours
// the hue of each subject (build_bank.py SUBJ) and of the title
static const uint8_t SUBJ_RGB[TV_NSUBJ + 1][3] = {
        {40, 92, 196},  {150, 96, 54}, {150, 108, 40}, {30, 128, 150}, {54, 120, 196}, {44, 140, 76}, {40, 150, 96},
        {150, 64, 170}, {170, 50, 80}, {200, 110, 40}, {180, 120, 30}, {70, 80, 190},  {92, 52, 168},
};
static const char *const SUBJ_HE[TV_NSUBJ] = {"ישראל", "היסטוריה", "תנ״ך",   "גאוגרפיה", "מדע",  "טבע",
                                              "ספורט", "מוזיקה",   "קולנוע", "שפה",      "אוכל", "טכנולוגיה"};
// the colours of the 4 answer places (a quiz look): rose, blue, amber, violet
static const uint8_t ANS_RGB[4][3] = {{226, 70, 110}, {50, 128, 236}, {232, 160, 36}, {136, 86, 222}};
#define C_WHITE rgb(255, 255, 255)
#define C_GOOD  rgb(60, 210, 110)
#define C_BAD   rgb(240, 70, 70)
#define C_GOLD  rgb(255, 214, 90)
#define C_DIM   rgb(190, 184, 220)
#define C_INK   rgb(18, 12, 40)

static uint16_t rgb3(const uint8_t *c, int k)  // k = 0..512: 256 = the colour, less = darker, more = lighter
{
    int r = c[0], g = c[1], b = c[2];
    if (k <= 256) {
        r = r * k / 256, g = g * k / 256, b = b * k / 256;
    } else {
        const int t = k - 256;
        r += (255 - r) * t / 256, g += (255 - g) * t / 256, b += (255 - b) * t / 256;
    }
    return rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

// ---------------------------------------------------------------- the background
// the theme colour now moves smoothly to the colour of the subject (or of the title)
static float s_th[3] = {92, 52, 168}, s_th_to[3] = {92, 52, 168};
typedef struct {
    float x, y, r, vx, vy, ph;
} bokeh_t;
#define N_BOKEH 6
static bokeh_t s_bok[N_BOKEH];

static void theme_to(int subj)
{
    const uint8_t *c = SUBJ_RGB[subj < 0 || subj >= TV_NSUBJ ? TV_NSUBJ : subj];
    for (int i = 0; i < 3; i++) {
        s_th_to[i] = c[i];
    }
}
static void bokeh_init(void)
{
    for (int i = 0; i < N_BOKEH; i++) {
        bokeh_t *b = &s_bok[i];
        b->x = (float)(esp_random() % (unsigned)gfx_w);
        b->y = (float)(esp_random() % (unsigned)gfx_h);
        b->r = gfx_h * (0.10f + (esp_random() % 100) / 700.0f);
        b->vx = ((int)(esp_random() % 200) - 100) / 12.0f;
        b->vy = ((int)(esp_random() % 200) - 100) / 18.0f;
        b->ph = (esp_random() % 628) / 100.0f;
    }
}
static void bg_update(float dt)
{
    for (int i = 0; i < 3; i++) {
        s_th[i] += (s_th_to[i] - s_th[i]) * (dt * 3 > 1 ? 1 : dt * 3);
    }
    for (int i = 0; i < N_BOKEH; i++) {
        bokeh_t *b = &s_bok[i];
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->ph += dt * 0.7f;
        if (b->x < -b->r) {
            b->x = gfx_w + b->r;
        }
        if (b->x > gfx_w + b->r) {
            b->x = -b->r;
        }
        if (b->y < -b->r) {
            b->y = gfx_h + b->r;
        }
        if (b->y > gfx_h + b->r) {
            b->y = -b->r;
        }
    }
}
// the colour of each row of the background (the dark panels mix it: no read of the frame)
#define ROWS_MAX 480
static uint16_t s_row[ROWS_MAX];
// the gradient (light at the top, deep at the bottom) and the soft lights that drift
// the holes of the background: boxes that a solid box covers in this frame (the background does not paint them: each
// px written to the frame in PSRAM costs; an overdraw of the whole frame was about 10 ms on the 3.49)
#define N_HOLE 8
static int s_hole[N_HOLE][4], s_nhole;  // x0, y0, x1, y1 (inclusive x0, exclusive x1 and y1)
static void hole_add(int x, int y, int w, int h, int r)
{
    if (s_nhole < N_HOLE && w > 2 * r) {
        s_hole[s_nhole][0] = x + r, s_hole[s_nhole][1] = y, s_hole[s_nhole][2] = x + w - r, s_hole[s_nhole][3] = y + h;
        s_nhole++;
    }
}
static void bg_row(int y, uint16_t c)
{
    int x = 0;
    while (x < gfx_w) {
        int next = gfx_w, skip_to = x;
        for (int i = 0; i < s_nhole; i++) {
            const int *h = s_hole[i];
            if (y < h[1] || y >= h[3] || h[2] <= x) {
                continue;
            }
            if (h[0] <= x) {
                skip_to = h[2] > skip_to ? h[2] : skip_to;
            } else if (h[0] < next) {
                next = h[0];
            }
        }
        if (skip_to > x) {
            x = skip_to;
            continue;
        }
        uint16_t *p = &g_fb[y * gfx_w];
        for (int i = x; i < next; i++) {
            p[i] = c;
        }
        x = next;
    }
}
static void bg_draw(bool lights)
{
    const int r = (int)s_th[0], g = (int)s_th[1], b = (int)s_th[2];
    for (int y = 0; y < gfx_h && y < ROWS_MAX; y++) {
        const int k = 200 - y * 150 / gfx_h;  // 200 .. 50 (of 256)
        s_row[y] = rgb((uint8_t)(r * k / 256 + 6), (uint8_t)(g * k / 256 + 4), (uint8_t)(b * k / 256 + 14));
        bg_row(y, s_row[y]);
    }
    const uint16_t lc = rgb((uint8_t)(r / 2 + 128), (uint8_t)(g / 2 + 128), (uint8_t)(b / 2 + 128));
    for (int i = 0; lights && i < N_BOKEH; i++) {
        const bokeh_t *bk = &s_bok[i];
        const int a = 14 + (int)(10 * fsin(bk->ph));
        ui_circle((int)bk->x, (int)bk->y, (int)bk->r, lc, a);
    }
}

// ---------------------------------------------------------------- boxes
// a filled box with round corners of radius r and soft edges, at the alpha a (0..255)
typedef struct {
    int dark;   // >= 0: each row = the background row mixed with the ink (a dark glass panel)
    int band;   // > 0: only a border of this width (the inside stays for another box)
    int split;  // the rows above it get the colour c2 (a gloss on the upper half)
    uint16_t c2;
} boxopt_t;
static void box_core(int x, int y, int w, int h, int r, uint16_t c, int a, const boxopt_t *o);
static void box_a(int x, int y, int w, int h, int r, uint16_t c, int a)
{
    box_core(x, y, w, h, r, c, a, NULL);
}
// a dark glass panel: each row is the background colour of the row mixed with the ink (dark = 0..255): no blend
static void panel(int x, int y, int w, int h, int r, int dark)
{
    const boxopt_t o = {dark, 0, 0, 0};
    box_core(x, y, w, h, r, 0, 255, &o);
}
static void box_core(int x, int y, int w, int h, int r, uint16_t c0, int a, const boxopt_t *o)
{
    if (w <= 0 || h <= 0 || a <= 0) {
        return;
    }
    if (r * 2 > h) {
        r = h / 2;
    }
    if (r * 2 > w) {
        r = w / 2;
    }
    for (int j = 0; j < h; j++) {
        const int yy = y + j;
        if ((unsigned)yy >= (unsigned)gfx_h) {
            continue;
        }
        float in = 0;  // how far the corner moves the row in (px, with the fraction)
        const float dy = j < r ? r - j - 0.5f : (j >= h - r ? j - (h - r) + 0.5f : -1);
        if (dy >= 0) {
            const float q = (float)r * r - dy * dy;
            float s = 0;
            if (q > 0) {
                s = q;
                float z = q > 1 ? q / 2 : 1;  // the square root by Newton (no sqrtf in a loop of the app file)
                for (int it = 0; it < 6; it++) {
                    z = 0.5f * (z + s / z);
                }
                s = z;
            }
            in = r - s;
        }
        uint16_t c = c0;
        if (o && o->dark >= 0) {
            c = mix_fb(s_row[yy < ROWS_MAX ? yy : ROWS_MAX - 1], rgb(10, 6, 30), o->dark);
        } else if (o && j < o->split) {
            c = o->c2;
        }
        const int x0 = x + (int)in, x1 = x + w - (int)in;
        const float fr = in - (int)in;
        const int ea = (int)(a * (1 - fr));
        if (x1 <= x0) {
            continue;
        }
        if (fr > 0.01f) {
            px_a(x0, yy, c, ea);
            px_a(x1 - 1, yy, c, ea);
        }
        const int s0 = fr > 0.01f ? x0 + 1 : x0, s1 = fr > 0.01f ? x1 - 1 : x1;
        int xa = s0 < 0 ? 0 : s0, xb = s1 > gfx_w ? gfx_w : s1;
        uint16_t *p = &g_fb[yy * gfx_w];
        if (o && o->band > 0 && j >= o->band && j < h - o->band) {
            // the band only: the two sides of the row
            const int l1 = xa + o->band < xb ? xa + o->band : xb, r0 = xb - o->band > l1 ? xb - o->band : l1;
            for (int i = xa; i < l1; i++) {
                p[i] = a >= 255 ? c : mix_fb(p[i], c, a);
            }
            for (int i = r0; i < xb; i++) {
                p[i] = a >= 255 ? c : mix_fb(p[i], c, a);
            }
        } else if (a >= 255) {
            for (int i = xa; i < xb; i++) {
                p[i] = c;
            }
        } else {
            for (int i = xa; i < xb; i++) {
                p[i] = mix_fb(p[i], c, a);
            }
        }
    }
}
// a card: a soft shadow, the fill, a light top half (a gloss), a border
static void card(int x, int y, int w, int h, int r, uint16_t fill, uint16_t edge, int a)
{
    // the shadow shows only under the card: a strip of the lower part (no blend of the hidden part)
    box_a(x + 2, y + h - r, w, r + 3, r, rgb(8, 4, 20), a / 3);
    // the border (a band of 4 px: the fill paints the inside; each px of the frame once)
    const boxopt_t ob = {-1, 4, 0, 0};
    box_core(x, y, w, h, r, edge, a, &ob);
    // the fill with a gloss: a lighter colour on the upper half
    const boxopt_t of = {-1, 0, (h - 4) / 2, mix_fb(fill, C_WHITE, 22)};
    box_core(x + 2, y + 2, w - 4, h - 4, r - 2 > 1 ? r - 2 : 1, fill, a, &of);
}

// ---------------------------------------------------------------- the timer arc
// an arc of radius r (dots of radius t) from the top, clockwise, part f (0..1) of the circle
static void arc(int cx, int cy, int r, int t, float f, uint16_t c, int a)
{
    if (f <= 0) {
        return;
    }
    const int n = (int)(6.2832f * r / (t * 0.9f + 0.5f) * f) + 1;
    for (int i = 0; i <= n; i++) {
        const float ang = -1.5708f + 6.2832f * f * i / (n ? n : 1);
        ui_circle(cx + (int)(r * fcos(ang)), cy + (int)(r * fsin(ang)), t, c, a);
    }
}
// the colour of the time left (green, then amber, then red)
static uint16_t time_col(float f)
{
    return f > 0.5f ? rgb((uint8_t)(60 + (1 - f) * 2 * 180), 210, 110)
                    : rgb(240, (uint8_t)(70 + f * 2 * 140), (uint8_t)(70 + f * 40));
}

// ---------------------------------------------------------------- sparks and confetti
typedef struct {
    float x, y, vx, vy, life, spin;
    uint16_t c;
    uint8_t kind, sz;  // kind 0 = a spark (a dot), 1 = confetti (a small bar that turns)
} part_t;
#define N_PART 72
static part_t s_part[N_PART];

static void part_add(float x, float y, float vx, float vy, uint16_t c, int kind)
{
    for (int i = 0; i < N_PART; i++) {
        part_t *p = &s_part[i];
        if (p->life > 0) {
            continue;
        }
        p->x = x, p->y = y, p->vx = vx, p->vy = vy, p->c = c, p->kind = (uint8_t)kind;
        p->life = kind ? 3.0f : 0.8f + (esp_random() % 50) / 100.0f;
        p->spin = (esp_random() % 628) / 100.0f;
        p->sz = (uint8_t)(2 + esp_random() % 3);
        return;
    }
}
// a burst of sparks from the box (a right answer)
static void burst(int cx, int cy, uint16_t c)
{
    for (int i = 0; i < 26; i++) {
        const float ang = (esp_random() % 628) / 100.0f, sp = 60 + esp_random() % 160;
        part_add((float)cx, (float)cy, sp * fcos(ang), sp * fsin(ang) - 40, i % 3 ? c : C_GOLD, 0);
    }
}
static void confetti(int n)
{
    for (int i = 0; i < n; i++) {
        const uint8_t *k = ANS_RGB[i % 4];
        part_add((float)(esp_random() % (unsigned)gfx_w), -(float)(esp_random() % 60), ((int)(esp_random() % 60) - 30),
                 40 + esp_random() % 60, i % 5 == 4 ? C_GOLD : rgb3(k, 300), 1);
    }
}
static int part_update(float dt)
{
    int live = 0;
    for (int i = 0; i < N_PART; i++) {
        part_t *p = &s_part[i];
        if (p->life <= 0) {
            continue;
        }
        live++;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += (p->kind ? 30 : 260) * dt;
        p->spin += dt * 6;
        if (p->y > gfx_h + 10) {
            p->life = 0;
        }
    }
    return live;
}
static void part_draw(void)
{
    for (int i = 0; i < N_PART; i++) {
        const part_t *p = &s_part[i];
        if (p->life <= 0) {
            continue;
        }
        const int a = p->life > 0.4f ? 255 : (int)(p->life * 637);
        if (p->kind == 0) {
            ui_circle((int)p->x, (int)p->y, p->sz, p->c, a);
        } else {
            const float s = fsin(p->spin);
            const int w = 2 + (int)(4 * (s < 0 ? -s : s));
            box_a((int)p->x - w / 2, (int)p->y - 3, w, 6, 1, p->c, a);
        }
    }
}
static void part_clear(void)
{
    memset(s_part, 0, sizeof s_part);
}
