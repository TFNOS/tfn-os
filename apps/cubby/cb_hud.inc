// CUBBY: the shapes of the HUD (included by app_cubby.c). The buttons, the rings and the icons are a short list of
// shapes with soft edges, drawn into each tile in internal RAM (a blend of each pixel in PSRAM cost 13-34 ms a frame).
// The small text goes onto the frame after the tiles.

enum {
    H_DISC,
    H_RING,
    H_LINE,
    H_RECT,
    H_DARK
};
typedef struct {
    uint8_t kind, a;
    uint16_t col;
    float x0, y0, x1, y1, r, w;
} hshape_t;
#define HMAX 64
typedef struct {
    hshape_t h[HMAX];
    int n;
} hudset_t;
static hudset_t s_huds[2];
static hudset_t *s_hudb = &s_huds[0], *s_hudd = &s_huds[0];  // (the build set, the draw set)

static void hud_add(int kind, float x0, float y0, float x1, float y1, float r, float w, uint32_t hex, int a)
{
    if (s_hudb->n >= HMAX || a <= 0) {
        return;
    }
    hshape_t *h = &s_hudb->h[s_hudb->n++];
    h->kind = (uint8_t)kind;
    h->a = (uint8_t)(a > 255 ? 255 : a);
    h->col = (uint16_t)(((hex >> 19) & 31) << 11 | ((hex >> 10) & 63) << 5 | ((hex >> 3) & 31));
    h->x0 = x0;
    h->y0 = y0;
    h->x1 = x1;
    h->y1 = y1;
    h->r = r;
    h->w = w;
}
static void hud_disc(float cx, float cy, float r, uint32_t hex, int a)
{
    hud_add(H_DISC, cx, cy, 0, 0, r, 0, hex, a);
}
static void hud_ring(float cx, float cy, float r, float w, uint32_t hex, int a)
{
    hud_add(H_RING, cx, cy, 0, 0, r, w, hex, a);
}
static void hud_line(float x0, float y0, float x1, float y1, float w, uint32_t hex, int a)
{
    hud_add(H_LINE, x0, y0, x1, y1, 0, w, hex, a);
}
static void hud_rect(float x, float y, float w, float h, uint32_t hex, int a)
{
    hud_add(H_RECT, x, y, x + w, y + h, 0, 0, hex, a);
}
static void hud_dark(float x, float y, float w, float h, int a)
{
    hud_add(H_DARK, x, y, x + w, y + h, 0, 0, 0, a);
}

// a pixel of the tile toward the colour c (RGB565, not swapped) by k (0..256)
static inline void hpx(uint16_t *d, uint16_t c, int k)
{
    if (k <= 0) {
        return;
    }
    uint16_t p = (uint16_t)((*d >> 8) | (*d << 8));
    int r = p >> 11, g = (p >> 5) & 63, b = p & 31;
    r += (((c >> 11) - r) * k) >> 8;
    g += ((((c >> 5) & 63) - g) * k) >> 8;
    b += (((c & 31) - b) * k) >> 8;
    p = (uint16_t)((r << 11) | (g << 5) | b);
    *d = (uint16_t)((p >> 8) | (p << 8));
}
// the cover of a pixel by a soft edge: d = the distance of the pixel centre into the shape (px; > 0.5 = all)
static inline int hcov(float d, int a)
{
    if (d >= 0.5f) {
        return a;
    }
    if (d <= -0.5f) {
        return 0;
    }
    return (int)((d + 0.5f) * a);
}
// the mask of a disc or a ring (a cache: made at the first use of its size): the cover of each px (size x size; the
// centre of the shape at the corner of px (R, R)), the span of each row with cover [s0, s1), and the hole of a ring
// [h0, h1) (h0 = h1: no hole)
typedef struct {
    uint8_t kind;
    uint16_t r4, w4;
    int R, size;
    uint8_t *m;
    int16_t *s0, *s1, *h0, *h1;
} hmask_t;
#define HMASK_N 24
static hmask_t s_hmask[HMASK_N];
static int s_nhmask;
static void hmask_free(void)
{
    for (int k = 0; k < s_nhmask; k++) {
        heap_caps_free(s_hmask[k].m);
        s_hmask[k].m = NULL;
    }
    s_nhmask = 0;
}
static const hmask_t *hmask_get(int kind, float r, float w)
{
    uint16_t r4 = (uint16_t)(r * 4 + 0.5f), w4 = (uint16_t)(kind == H_RING ? w * 4 + 0.5f : 0);
    for (int k = 0; k < s_nhmask; k++) {
        if (s_hmask[k].kind == kind && s_hmask[k].r4 == r4 && s_hmask[k].w4 == w4) {
            return &s_hmask[k];
        }
    }
    if (s_nhmask >= HMASK_N) {
        return NULL;
    }
    int R = (int)(r + 2), size = 2 * R;
    uint8_t *mem = heap_caps_malloc((size_t)size * size + (size_t)size * 8, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
        return NULL;
    }
    hmask_t *h = &s_hmask[s_nhmask++];
    h->kind = (uint8_t)kind;
    h->r4 = r4;
    h->w4 = w4;
    h->R = R;
    h->size = size;
    h->m = mem;
    int16_t *sp = (int16_t *)(mem + size * size);
    h->s0 = sp;
    h->s1 = sp + size;
    h->h0 = sp + 2 * size;
    h->h1 = sp + 3 * size;
    float rh = r - w;
    for (int iy = 0; iy < size; iy++) {
        float dy = iy + 0.5f - R;
        int s0 = size, s1 = 0, h0 = 0, h1 = 0;
        for (int ix = 0; ix < size; ix++) {
            float dx = ix + 0.5f - R, d = fsqrt(dx * dx + dy * dy);
            int k = hcov(r - d, 255);
            if (kind == H_RING) {
                int k2 = hcov(d - rh, 255);
                if (k2 < k) {
                    k = k2;
                }
            }
            mem[iy * size + ix] = (uint8_t)k;
            if (k) {
                if (ix < s0) {
                    s0 = ix;
                }
                s1 = ix + 1;
            }
        }
        // (the hole of a ring: the px with no cover between the two sides)
        if (kind == H_RING && s1 > s0) {
            int a = s0;
            while (a < s1 && mem[iy * size + a]) {
                a++;
            }
            int b = a;
            while (b < s1 && !mem[iy * size + b]) {
                b++;
            }
            if (b < s1) {
                h0 = a;
                h1 = b;
            }
        }
        h->s0[iy] = (int16_t)s0;
        h->s1[iy] = (int16_t)s1;
        h->h0[iy] = (int16_t)h0;
        h->h1[iy] = (int16_t)h1;
    }
    return h;
}

// the bounds of a shape (output px)
static void hud_bounds(const hshape_t *h, float *bx0, float *by0, float *bx1, float *by1)
{
    if (h->kind == H_DISC || h->kind == H_RING) {
        *bx0 = h->x0 - h->r - 1;
        *bx1 = h->x0 + h->r + 1;
        *by0 = h->y0 - h->r - 1;
        *by1 = h->y0 + h->r + 1;
    } else if (h->kind == H_LINE) {
        *bx0 = FMIN(h->x0, h->x1) - h->w;
        *bx1 = FMAX(h->x0, h->x1) + h->w;
        *by0 = FMIN(h->y0, h->y1) - h->w;
        *by1 = FMAX(h->y0, h->y1) + h->w;
    } else {
        *bx0 = h->x0;
        *by0 = h->y0;
        *bx1 = h->x1;
        *by1 = h->y1;
    }
}
// the shapes on the output box [x0, x1) x [y0, y1)
static int hud_pick(int x0, int y0, int x1, int y1, const hshape_t **out)
{
    int n = 0;
    for (int q = 0; q < s_hudd->n; q++) {
        float bx0, by0, bx1, by1;
        hud_bounds(&s_hudd->h[q], &bx0, &by0, &bx1, &by1);
        if (bx1 < x0 || bx0 >= x1 || by1 < y0 || by0 >= y1) {
            continue;
        }
        out[n++] = &s_hudd->h[q];
    }
    return n;
}
// the shapes in one output row y; row[0] is the pixel at x0, the row ends at x1
static void hud_row(uint16_t *row, int y, int x0, int x1, const hshape_t *const *hs, int nh)
{
    float py = y + 0.5f;
    for (int q = 0; q < nh; q++) {
        const hshape_t *h = hs[q];
        int a = h->a * 256 / 255;
        if (h->kind == H_RECT || h->kind == H_DARK) {
            if (py < h->y0 || py >= h->y1) {
                continue;
            }
            int xa = (int)FMAX(h->x0, (float)x0), xb = (int)FMIN(h->x1, (float)x1);
            for (int x = xa; x < xb; x++) {
                hpx(&row[x - x0], h->kind == H_RECT ? h->col : 0, a);
            }
            continue;
        }
        if (h->kind == H_DISC || h->kind == H_RING) {
            const hmask_t *M = hmask_get(h->kind, h->r, h->w);
            if (M) {
                // (the mask: the centre at the nearest px corner)
                int cx = (int)(h->x0 + 0.5f), cy = (int)(h->y0 + 0.5f);
                int iy = y - (cy - M->R);
                if (iy < 0 || iy >= M->size) {
                    continue;
                }
                const uint8_t *mr = M->m + iy * M->size;
                int bx = cx - M->R;
                int sa = M->s0[iy], sb = M->s1[iy];
                uint16_t sw = (uint16_t)((h->col >> 8) | (h->col << 8));
                for (int part = 0; part < 2; part++) {
                    int ia = part ? M->h1[iy] : sa, ib = part ? sb : (M->h1[iy] > M->h0[iy] ? M->h0[iy] : sb);
                    if (part && M->h1[iy] <= M->h0[iy]) {
                        break;
                    }
                    int xa = bx + ia, xb = bx + ib;
                    if (xa < x0) {
                        xa = x0;
                    }
                    if (xb > x1) {
                        xb = x1;
                    }
                    for (int x = xa; x < xb; x++) {
                        int k = (mr[x - bx] * a) >> 8;
                        if (k >= 255 && a >= 256) {
                            row[x - x0] = sw;
                        } else {
                            hpx(&row[x - x0], h->col, k);
                        }
                    }
                }
                continue;
            }
            float dy = py - h->y0, ro = h->r + 0.5f, dy2 = dy * dy;
            float qo = ro * ro - dy2;
            if (qo <= 0) {
                continue;
            }
            float ho = fsqrt(qo);
            // (the squared distances of the full part: inside r - 0.5 and, for a ring, outside the hole + 0.5)
            float rin = h->r - 0.5f, full_o = rin > 0 ? rin * rin : -1;
            float rh = h->r - h->w, full_i = h->kind == H_RING ? (rh + 0.5f) * (rh + 0.5f) : -1;
            float hole = h->kind == H_RING && rh > 0.5f ? (rh - 0.5f) * (rh - 0.5f) : -1;
            float hi = hole > dy2 ? fsqrt(hole - dy2) : -1;
            uint16_t sw = (uint16_t)((h->col >> 8) | (h->col << 8));
            bool opaque = a >= 255;
            int xa = (int)FMAX(h->x0 - ho, (float)x0), xb = (int)FMIN(h->x0 + ho + 1, (float)x1);
            for (int x = xa; x < xb; x++) {
                float dx = x + 0.5f - h->x0;
                if (hi > 1 && dx > -hi + 0.5f && dx < hi - 0.5f) {
                    // (the hole of the ring: on to its far side)
                    int nx = (int)(h->x0 + hi - 0.5f);
                    if (nx > x) {
                        x = nx - 1;
                    }
                    continue;
                }
                float d2 = dx * dx + dy2;
                int k;
                if (d2 <= full_o && d2 >= full_i) {
                    k = a;
                } else {
                    float d = fsqrt(d2);
                    k = hcov(h->r - d, a);
                    if (h->kind == H_RING) {
                        int k2 = hcov(d - rh, a);
                        if (k2 < k) {
                            k = k2;
                        }
                    }
                }
                if (k >= 256 && opaque) {
                    row[x - x0] = sw;
                } else {
                    hpx(&row[x - x0], h->col, k);
                }
            }
            continue;
        }
        // a line with round ends: the distance to the segment
        float bx0, by0, bx1, by1;
        hud_bounds(h, &bx0, &by0, &bx1, &by1);
        if (py < by0 || py > by1) {
            continue;
        }
        float ex = h->x1 - h->x0, ey = h->y1 - h->y0, il = frcp(ex * ex + ey * ey + 1e-6f), hw = h->w * 0.5f;
        int xa = (int)FMAX(bx0, (float)x0), xb = (int)FMIN(bx1 + 1, (float)x1);
        float qy = py - h->y0;
        for (int x = xa; x < xb; x++) {
            float px = x + 0.5f - h->x0;
            float u = clampf((px * ex + qy * ey) * il, 0, 1);
            float dx = px - u * ex, dy = qy - u * ey;
            float d2 = dx * dx + dy * dy;
            if (d2 > (hw + 0.5f) * (hw + 0.5f)) {
                continue;
            }
            hpx(&row[x - x0], h->col, hcov(hw - fsqrt(d2), a));
        }
    }
}
// a done tile into the frame: each pixel as KX pixels, then the HUD in each output row
#if CB_ROUND
// (the round: each pixel as 2 x 2 px; the HUD on each of the two output rows)
static void tile_out_hud(tile_t *t)
{
    int w = t->x1 - t->x0, ox0 = t->x0 * KX, ow = w * KX;
    const hshape_t *hs[HMAX];
    int nh = hud_pick(ox0, t->y0 * KY, ox0 + ow, t->y1 * KY, hs);
    uint32_t buf32[TLW * KX / 2], hud32[TLW * KX / 2];
    for (int r = 0; r < t->y1 - t->y0; r++) {
        const uint32_t *src = t->p + r * TLW;
        for (int i = 0; i < w; i++) {
            uint32_t c = src[i];
            c = ((c & 0xFF) << 8) | ((c >> 8) & 0xFF);
            buf32[i] = c * 0x10001u;
        }
        // (only the px in the circle go out: the PSRAM writes are the cost of this stage)
        const int y = t->y0 + r;
        int a = s_rcl[y] > t->x0 ? s_rcl[y] - t->x0 : 0, b = s_rcr[y] < t->x1 ? s_rcr[y] - t->x0 : w;
        if (a >= b) {
            continue;
        }
        for (int k = 0; k < KY; k++) {
            const int oy = y * KY + k;
            uint16_t *o = g_fb + oy * OW + ox0;
            if (!nh) {
                memcpy(o + a * KX, (uint16_t *)buf32 + a * KX, (size_t)(b - a) * KX * 2);
                continue;
            }
            memcpy(hud32, buf32, (size_t)ow * 2);
            hud_row((uint16_t *)hud32, oy, ox0, ox0 + ow, hs, nh);
            memcpy(o + a * KX, (uint16_t *)hud32 + a * KX, (size_t)(b - a) * KX * 2);
        }
    }
}
#else
static void tile_out_hud(tile_t *t)
{
    int w = t->x1 - t->x0, ox0 = t->x0 * KX, ow = w * KX;
    const hshape_t *hs[HMAX];
    int nh = hud_pick(ox0, t->y0, ox0 + ow, t->y1, hs);
    uint32_t buf32[TLW * KX / 2];
    uint16_t *buf = (uint16_t *)buf32;
#if KX == 2
    if (!nh) {
        // (no HUD shape on the tile: the px go straight into the frame, 32 bits at a time)
        for (int r = 0; r < t->y1 - t->y0; r++) {
            const uint32_t *src = t->p + r * TLW;
            uint32_t *o = (uint32_t *)(g_fb + (t->y0 + r) * OW + ox0);
            for (int i = 0; i < w; i++) {
                uint32_t c = src[i];
                c = ((c & 0xFF) << 8) | ((c >> 8) & 0xFF);
                o[i] = c * 0x10001u;
            }
        }
        return;
    }
#endif
    for (int r = 0; r < t->y1 - t->y0; r++) {
        const uint32_t *src = t->p + r * TLW;
        uint16_t *dst = buf;
        // (each pixel to the byte order of the frame and twice: one 32-bit store; the soft edges of the tile make the
        // blocks of 2 px hard to see)
        for (int i = 0; i < w; i++) {
            uint32_t c = src[i];
            c = ((c & 0xFF) << 8) | ((c >> 8) & 0xFF);
#if KX == 2
            buf32[i] = c * 0x10001u;
#else
            buf[i] = (uint16_t)c;
#endif
        }
        if (nh) {
            hud_row(dst, t->y0 + r, ox0, ox0 + ow, hs, nh);
        }
        memcpy(g_fb + (t->y0 + r) * OW + ox0, dst, (size_t)ow * 2);
    }
}
#endif
