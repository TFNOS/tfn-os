// THE ROUND VIEW OF STACK (the round panels: app_t.round = APP_ROUND_HI; the AMOLED-1.75C 466 x 466, the Watcher 412 x 412).
// The game draws a square view of RV_S x RV_S px (about the size of the CoreS3 frame) into a PSRAM buffer:
// rv_begin() points g_fb, gfx_w and gfx_h to it, rv_end() points them back and scales the view out to the frame of
// the panel, only inside the circle (1.5 times on the 1.75C: two source pixels give three, the middle one is their
// mean; the Watcher: the nearest source pixel). So the world of the game fills the whole circle. (The way of KEEPER:
// 466 x 466 drawn live is 2.8 times the pixels of the CoreS3 frame.) rv_pt() changes a touch point of the panel frame
// to a point of the view. Each app has its own copy of this file (no shared code). Before the include: gfx.h,
// esp_heap_caps.h, string.h. The other boards: this file is empty.
#if BOARD_ROUND
#ifndef RV_S
#define RV_S 311
#endif
#define RV_MAXD 480
static uint16_t *rv_fb, *rv_real;  // the view (PSRAM); the frame of the panel while the game draws the view
static int rv_rw, rv_rh;           // the size of the frame of the panel
static int rv_d;                   // the size of the tables below (0 = none yet)
static int16_t rv_xi[RV_MAXD], rv_x0[RV_MAXD], rv_x1[RV_MAXD];  // each column: its source; each row: the circle span
static uint16_t *rv_ln;                                         // two scaled lines (native colors)

static inline uint16_t rv_sw(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
static inline uint16_t rv_avg(uint16_t a, uint16_t b)
{
    return (uint16_t)(((a & 0xF7DE) >> 1) + ((b & 0xF7DE) >> 1));
}

// the size of the panel frame (also while the game draws the view)
static inline int rv_pw(void)
{
    return rv_real ? rv_rw : gfx_w;
}

// a point of the panel frame (a touch) to a point of the view
static inline void rv_pt(int *x, int *y)
{
    int d = rv_pw();
    if (d <= 0) {
        return;
    }
    *x = *x * RV_S / d;
    *y = *y * RV_S / d;
}

// the game draws into the view now (false: no memory: the frame is dark)
static bool rv_begin(void)
{
    if (!rv_fb) {
        rv_fb = heap_caps_malloc((size_t)RV_S * RV_S * 2, MALLOC_CAP_SPIRAM);
    }
    if (!rv_ln) {
        rv_ln = heap_caps_malloc(2 * RV_MAXD * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!rv_ln) {
            rv_ln = heap_caps_malloc(2 * RV_MAXD * 2, MALLOC_CAP_SPIRAM);
        }
    }
    if (!rv_fb || !rv_ln) {
        gfx_fill(0);
        return false;
    }
    rv_real = g_fb;
    rv_rw = gfx_w;
    rv_rh = gfx_h;
    g_fb = rv_fb;
    gfx_w = RV_S;
    gfx_h = RV_S;
    gfx_clip_reset();
    return true;
}

static void rv_tables(int d)
{
    for (int x = 0; x < d; x++) {
        int i = x * RV_S / d;
        rv_xi[x] = (int16_t)(i < RV_S ? i : RV_S - 1);
    }
    float r = d * 0.5f + 1;  // (+1: each pixel that shows is painted)
    for (int y = 0; y < d; y++) {
        float dy = y + 0.5f - d * 0.5f, h = r * r - dy * dy;
        float hw = h > 0 ? sqrtf(h) : 0;
        int x0 = (int)floorf(d * 0.5f - hw), x1 = (int)ceilf(d * 0.5f + hw);
        rv_x0[y] = (int16_t)(x0 < 0 ? 0 : x0);
        rv_x1[y] = (int16_t)(x1 > d ? d : x1);
    }
    rv_d = d;
}

// 1.5 times: the source row sy into the line l (native colors): p, the mean of p and q, q
static void rv_line15(const uint16_t *a, uint16_t *l, int d)
{
    for (int j = 0, x = 0; x < d; j += 2, x += 3) {
        uint16_t p = rv_sw(a[j]), q = j + 1 < RV_S ? rv_sw(a[j + 1]) : p;
        l[x] = p;
        if (x + 1 < d) {
            l[x + 1] = rv_avg(p, q);
        }
        if (x + 2 < d) {
            l[x + 2] = q;
        }
    }
}

// the frame of the panel again; the view scaled out into the circle
static void rv_end(void)
{
    if (!rv_real) {
        return;
    }
    g_fb = rv_real;
    gfx_w = rv_rw;
    gfx_h = rv_rh;
    rv_real = NULL;
    gfx_clip_reset();
    int d = gfx_w < gfx_h ? gfx_w : gfx_h;
    if (d > RV_MAXD) {
        d = RV_MAXD;
    }
    if (d != rv_d) {
        rv_tables(d);
    }
    if (d == RV_S * 3 / 2) {
        uint16_t *l0 = rv_ln, *l1 = rv_ln + RV_MAXD;
        for (int y = 0; y < d; y++) {
            int k = y / 3, m = y - k * 3, x0 = rv_x0[y], x1 = rv_x1[y];
            if (m == 0) {
                const uint16_t *a = rv_fb + (size_t)(2 * k) * RV_S;
                rv_line15(a, l0, d);
                rv_line15(2 * k + 1 < RV_S ? a + RV_S : a, l1, d);
            }
            uint16_t *dst = g_fb + (size_t)y * gfx_w;
            if (m == 1) {
                for (int x = x0; x < x1; x++) {
                    dst[x] = rv_sw(rv_avg(l0[x], l1[x]));
                }
            } else {
                const uint16_t *p = m ? l1 : l0;
                for (int x = x0; x < x1; x++) {
                    dst[x] = rv_sw(p[x]);
                }
            }
        }
        return;
    }
    // (the other sizes: the nearest source pixel)
    for (int y = 0; y < d; y++) {
        int sy = y * RV_S / d;
        const uint16_t *a = rv_fb + (size_t)(sy < RV_S ? sy : RV_S - 1) * RV_S;
        uint16_t *dst = g_fb + (size_t)y * gfx_w;
        for (int x = rv_x0[y]; x < rv_x1[y]; x++) {
            dst[x] = a[rv_xi[x]];
        }
    }
}

// the memory of the view back (at the leave of the app)
static void rv_free(void)
{
    if (rv_real) {
        rv_end();
    }
    heap_caps_free(rv_fb);
    heap_caps_free(rv_ln);
    rv_fb = rv_ln = NULL;
}
#endif
