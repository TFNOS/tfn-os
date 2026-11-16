// GLASSHOUSE: the picture. The world has layers in PSRAM: the sky (1 texel for each world px; it moves slower: a
// parallax), the bare stand (2 texels for each px: the backdrop and the scenery, painted once for a level) and the
// stand with the parts that rest (a copy of the bare stand with the picture of each part at its rest place, at 2, 1
// and 1/2 texel for each px). The bare stand is kept packed (the runs of each row that do not show the sky): it is
// about half of a full layer. Each frame copies the world layer to the screen at the zoom of the camera, then draws on
// it what moves: the parts that move, the pieces, the ropes, the water, the shards and the cracks that grow. A part
// that starts to move goes out of the world layer (the bare stand comes back under it); a part that comes to rest
// goes in again. Included by app_glass.c.

#define WW 640
#define WH 172
#define W2 (WW * 2)
#define H2 (WH * 2)
#define WQ (WW / 2)
#define HQ (WH / 2)
static uint16_t *s_sky, *s_l2, *s_l1, *s_l05;  // the sky (1x), the world (2x, 1x, 1/2x)
static uint8_t *s_bare;                        // the bare stand (2x), packed: the runs of each row
static uint32_t *s_bare_row;                   // [H2 + 1]: the offset of each row in s_bare

// pack the bare stand from a full layer: each row is a list of runs (u16 x, u16 n, n texels); a gap shows the sky
static bool bare_pack(const uint16_t *src)
{
    heap_caps_free(s_bare);
    s_bare = NULL;
    size_t sz = 0;
    for (int y = 0; y < H2; y++) {
        const uint16_t *r = src + y * W2;
        for (int x = 0; x < W2;) {
            while (x < W2 && r[x] == KEY) {
                x++;
            }
            if (x >= W2) {
                break;
            }
            int x0 = x;
            while (x < W2 && r[x] != KEY) {
                x++;
            }
            sz += 4 + 2 * (size_t)(x - x0);
        }
    }
    s_bare = (uint8_t *)heap_caps_malloc(sz + 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_bare) {
        return false;
    }
    uint32_t at = 0;
    for (int y = 0; y < H2; y++) {
        s_bare_row[y] = at;
        const uint16_t *r = src + y * W2;
        for (int x = 0; x < W2;) {
            while (x < W2 && r[x] == KEY) {
                x++;
            }
            if (x >= W2) {
                break;
            }
            int x0 = x;
            while (x < W2 && r[x] != KEY) {
                x++;
            }
            uint16_t hd[2] = {(uint16_t)x0, (uint16_t)(x - x0)};
            memcpy(s_bare + at, hd, 4);
            memcpy(s_bare + at + 4, r + x0, 2 * (size_t)(x - x0));
            at += 4 + 2 * (uint32_t)(x - x0);
        }
    }
    s_bare_row[H2] = at;
    return true;
}
// the bare stand of row y, texels x0 .. x1, into dst (dst[x] for each x); a gap gets the sky (the world layers have
// no KEY texel: the copy to the screen needs no test and no second read for the sky)
static void bare_row(int y, int x0, int x1, uint16_t *dst)
{
    const uint16_t *sky = s_sky + (y / 2) * WW;
    for (int x = x0; x < x1; x++) {
        dst[x] = sky[x >> 1];
    }
    if (!s_bare) {
        return;
    }
    const uint8_t *p = s_bare + s_bare_row[y], *e = s_bare + s_bare_row[y + 1];
    while (p < e) {
        uint16_t hd[2];
        memcpy(hd, p, 4);
        int a = hd[0] > x0 ? hd[0] : x0, b = hd[0] + hd[1] < x1 ? hd[0] + hd[1] : x1;
        if (a < b) {
            memcpy(dst + a, p + 4 + 2 * (a - hd[0]), 2 * (size_t)(b - a));
        }
        p += 4 + 2 * (size_t)hd[1];
    }
}

// ---------------------------------------------------------------- the camera (the director moves it)
typedef struct {
    float x, y, z;
    float sx, sy;
} cam_t;
static cam_t s_cam = {320, 86, 1, 0, 0};
static float s_cx = 320, s_cy = 86;  // the point of the screen that shows the middle of the camera
static float s_clock;                // seconds (the picture, the shimmer)
static inline float scr_x(float wx)
{
    return (wx - s_cam.x) * s_cam.z + s_cx + s_cam.sx;
}
static inline float scr_y(float wy)
{
    return (wy - s_cam.y) * s_cam.z + s_cy + s_cam.sy;
}
static inline float wld_x(float sx)
{
    return (sx - s_cx - s_cam.sx) / s_cam.z + s_cam.x;
}
static inline float wld_y(float sy)
{
    return (sy - s_cy - s_cam.sy) / s_cam.z + s_cam.y;
}

// ---------------------------------------------------------------- the pictures of the parts
typedef struct {
    int16_t ox, oy;  // the texel of the middle of the part (its local 0, 0)
    uint16_t w, h;
    uint32_t at;  // the offset in the pool (colours; then w * h alpha bytes)
} spr_t;
#define POOL_BYTES (480 * 1024)
static uint8_t *s_pool;
static uint32_t s_pool_used;
static spr_t s_spr[MAXP + 2];  // the parts, then the object and its stand
static bool s_spr_ok[MAXP + 2];
#define SPR_OBJ   MAXP
#define SPR_STAND (MAXP + 1)
static uint16_t *spr_col(const spr_t *s)
{
    return (uint16_t *)(void *)(s_pool + s->at);
}
static uint8_t *spr_alpha(const spr_t *s)
{
    return s_pool + s->at + (uint32_t)s->w * s->h * 2;
}
// a new picture with the local box lx0 .. lx1, ly0 .. ly1 (px), all clear; the canvas draws into it
static bool spr_new(int i, float lx0, float ly0, float lx1, float ly1)
{
    spr_t *s = &s_spr[i];
    int w = (int)ceilf((lx1 - lx0) * 2) + 2, h = (int)ceilf((ly1 - ly0) * 2) + 2;
    uint32_t need = ((uint32_t)w * h * 3 + 3) & ~3u;
    s_spr_ok[i] = false;
    if (!s_pool || w <= 0 || h <= 0 || w > 1280 || h > 344 || s_pool_used + need > POOL_BYTES) {
        return false;
    }
    s->w = (uint16_t)w;
    s->h = (uint16_t)h;
    s->ox = (int16_t)(-lx0 * 2 + 1);
    s->oy = (int16_t)(-ly0 * 2 + 1);
    s->at = s_pool_used;
    s_pool_used += need;
    memset(s_pool + s->at, 0, (size_t)w * h * 3);
    cv_set(spr_col(s), spr_alpha(s), w, h, 2, -(float)s->ox / 2, -(float)s->oy / 2, NULL);
    s_spr_ok[i] = true;
    return true;
}
// the same picture again (all clear), to paint it again (a pane broke, the object cracked)
static bool spr_again(int i)
{
    if (!s_spr_ok[i]) {
        return false;
    }
    spr_t *s = &s_spr[i];
    memset(s_pool + s->at, 0, (size_t)s->w * s->h * 3);
    cv_set(spr_col(s), spr_alpha(s), s->w, s->h, 2, -(float)s->ox / 2, -(float)s->oy / 2, NULL);
    return true;
}

// ---------------------------------------------------------------- an affine copy of a picture
// The picture of a part at the place (cx, cy) world px, turned (c, s), into a target where target = (world - o) * k.
// The world layers (key) blend over the sky colour under a KEY texel. Clip box in target px. am = the alpha 0..256.
static void blit_spr(uint16_t *dst, int dw, int dh, float k, float ox, float oy, const spr_t *sp, float cx, float cy,
                     float c, float s, int x0, int y0, int x1, int y1, int am, bool key)
{
    const uint16_t *col = spr_col(sp);
    const uint8_t *al = spr_alpha(sp);
    (void)dh;
    // the box of the picture in the target
    float hw = (float)sp->w / 4, hh = (float)sp->h / 4;
    float mx = ((float)sp->w / 2 - sp->ox) / 2,
          my = ((float)sp->h / 2 - sp->oy) / 2;  // (the middle of the picture, local)
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int q = 0; q < 4; q++) {
        float lx = mx + ((q & 1) ? hw : -hw), ly = my + ((q & 2) ? hh : -hh);
        float wx = cx + c * lx + s * ly, wy = cy - s * lx + c * ly;
        float tx = (wx - ox) * k, ty = (wy - oy) * k;
        bx0 = fminf(bx0, tx);
        bx1 = fmaxf(bx1, tx);
        by0 = fminf(by0, ty);
        by1 = fmaxf(by1, ty);
    }
    if (bx1 < (float)x0 || by1 < (float)y0 || bx0 > (float)x1 || by0 > (float)y1) {
        return;
    }
    int ya = clampi((int)floorf(by0), y0, y1), yb = clampi((int)ceilf(by1), y0, y1);
    int xa = clampi((int)floorf(bx0), x0, x1), xb = clampi((int)ceilf(bx1), x0, x1);
    if (ya >= yb || xa >= xb) {
        return;
    }
    // the texel of a target point: u = ox + 2 lx, lx = c dx - s dy (dx, dy = the world offset from the middle)
    float ik = 1.0f / k;
    int32_t du = (int32_t)(2 * c * ik * 65536), dv = (int32_t)(2 * s * ik * 65536);
    int W16 = sp->w << 16, H16 = sp->h << 16;
    for (int y = ya; y < yb; y++) {
        float dy = ((float)y + 0.5f) * ik + oy - cy, dx = ((float)xa + 0.5f) * ik + ox - cx;
        int32_t u = (int32_t)((sp->ox + 2 * (c * dx - s * dy)) * 65536),
                v = (int32_t)((sp->oy + 2 * (s * dx + c * dy)) * 65536);
        uint16_t *d = dst + y * dw;
        // (the columns of this row where the texel is in the picture, with 1 px to spare: the loop skips the rest of
        // the box of a turned picture; the test in the loop stays)
        float fa = 0, fb = (float)(xb - xa);
        if (du) {
            float t0 = -(float)u / du, t1 = ((float)W16 - (float)u) / du;
            fa = fmaxf(fa, fminf(t0, t1) - 1);
            fb = fminf(fb, fmaxf(t0, t1) + 1);
        } else if ((uint32_t)u >= (uint32_t)W16) {
            continue;
        }
        if (dv) {
            float t0 = -(float)v / dv, t1 = ((float)H16 - (float)v) / dv;
            fa = fmaxf(fa, fminf(t0, t1) - 1);
            fb = fminf(fb, fmaxf(t0, t1) + 1);
        } else if ((uint32_t)v >= (uint32_t)H16) {
            continue;
        }
        if (fb <= fa) {
            continue;
        }
        int sa = (int)fa, sb = (int)fb + 1;
        if (sb > xb - xa) {
            sb = xb - xa;
        }
        u += sa * du;
        v += sa * dv;
        for (int x = xa + sa; x < xa + sb; x++, u += du, v += dv) {
            if ((uint32_t)u >= (uint32_t)W16 || (uint32_t)v >= (uint32_t)H16) {
                continue;
            }
            int ti = (v >> 16) * sp->w + (u >> 16);
            int a = al[ti];
            if (!a) {
                continue;
            }
            a = (a * am) >> 8;
            uint16_t cc = col[ti], dd = d[x];
            if (key && dd == KEY) {
                dd = s_sky[clampi((int)((float)y / k + oy), 0, WH - 1) * WW +
                           clampi((int)((float)x / k + ox), 0, WW - 1)];
            }
            uint16_t r = a >= 250 ? cc : tfd_mix(dd, cc, a + (a >> 7));
            d[x] = (key && r == KEY) ? (uint16_t)(KEY ^ 0x0100u) : r;
        }
    }
}

// ---------------------------------------------------------------- the rest places of the parts (the bake)
typedef struct {
    bool in;
    float x, y, a;
} bk_t;
static bk_t s_bk[MAXP + 2];
// the decals: pieces and shards that rest on the stand (drawn into the world layer)
typedef struct {
    float p[12];
    uint8_t n;
    uint16_t col, lit;
} decal_t;
#define MAXDECAL 320
static decal_t *s_dec;
static int s_ndec;
static uint8_t s_zorder[MAXP + 2];
static int s_nz;

// the 1x and the 1/2x world layers from the 2x one (the mean of 2 x 2; a KEY texel takes the sky colour; all KEY: KEY)
static uint16_t mean4(uint16_t q0, uint16_t q1, uint16_t q2, uint16_t q3, uint16_t sky)
{
    uint16_t q[4] = {q0, q1, q2, q3};
    int nk = 0, r = 0, g = 0, bl = 0;
    for (int i = 0; i < 4; i++) {
        uint16_t c = q[i];
        if (c == KEY) {
            nk++;
            c = sky;
        }
        int rr, gg, bb;
        tfd_unrgb(c, &rr, &gg, &bb);
        r += rr;
        g += gg;
        bl += bb;
    }
    if (nk == 4) {
        return KEY;
    }
    uint16_t c = tfd_rgb(r / 4, g / 4, bl / 4);
    return c == KEY ? (uint16_t)(KEY ^ 0x0100u) : c;
}
static void l1_update(int tx0, int ty0, int tx1, int ty1)
{
    int x0 = clampi(tx0 / 2, 0, WW), x1 = clampi((tx1 + 1) / 2, 0, WW), y0 = clampi(ty0 / 2, 0, WH),
        y1 = clampi((ty1 + 1) / 2, 0, WH);
    for (int y = y0; y < y1; y++) {
        const uint16_t *a = s_l2 + (2 * y) * W2, *b = a + W2;
        uint16_t *d = s_l1 + y * WW;
        for (int x = x0; x < x1; x++) {
            d[x] = mean4(a[2 * x], a[2 * x + 1], b[2 * x], b[2 * x + 1], s_sky[y * WW + x]);
        }
    }
    // (the 1/2x layer: the box of the 1x one, made even)
    x0 &= ~1;
    y0 &= ~1;
    for (int y = y0 / 2; y < (y1 + 1) / 2 && y < HQ; y++) {
        const uint16_t *a = s_l1 + (2 * y) * WW, *b = a + WW;
        uint16_t *d = s_l05 + y * WQ;
        for (int x = x0 / 2; x < (x1 + 1) / 2 && x < WQ; x++) {
            d[x] = mean4(a[2 * x], a[2 * x + 1], b[2 * x], b[2 * x + 1], s_sky[(2 * y) * WW + 2 * x]);
        }
    }
}
static void bake_part(int i, int tx0, int ty0, int tx1, int ty1)
{
    if (!s_spr_ok[i] || !s_bk[i].in) {
        return;
    }
    float c = cosf(s_bk[i].a), s = -sinf(s_bk[i].a);
    // (the screen angle is + clockwise: the turn (c, s) of the picture is the physics turn)
    blit_spr(s_l2, W2, H2, 2, 0, 0, &s_spr[i], s_bk[i].x, s_bk[i].y, c, s, tx0, ty0, tx1, ty1, 256, true);
}
static void decal_draw(const decal_t *D, int tx0, int ty0, int tx1, int ty1)
{
    cv_set(s_l2, NULL, W2, H2, 2, 0, 0, s_sky);
    cv_clip(tx0, ty0, tx1, ty1);
    cv_poly(D->p, D->n, D->col, 230);
    if (D->n >= 3) {
        cv_line(D->p[0], D->p[1], D->p[2], D->p[3], 0.5f, D->lit, 200);
    }
    cv_noclip();
}
// the world layer again in a box (texels): the bare stand, then the parts that rest and the decals in the box
static void repair(int tx0, int ty0, int tx1, int ty1)
{
    tx0 = clampi(tx0, 0, W2);
    tx1 = clampi(tx1, 0, W2);
    ty0 = clampi(ty0, 0, H2);
    ty1 = clampi(ty1, 0, H2);
    if (tx0 >= tx1 || ty0 >= ty1) {
        return;
    }
    for (int y = ty0; y < ty1; y++) {
        bare_row(y, tx0, tx1, s_l2 + y * W2);
    }
    for (int q = 0; q < s_nz; q++) {
        bake_part(s_zorder[q], tx0, ty0, tx1, ty1);
    }
    for (int q = 0; q < s_ndec; q++) {
        const decal_t *D = &s_dec[q];
        float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f;
        for (int k = 0; k < D->n; k++) {
            bx0 = fminf(bx0, D->p[2 * k]);
            bx1 = fmaxf(bx1, D->p[2 * k]);
            by0 = fminf(by0, D->p[2 * k + 1]);
            by1 = fmaxf(by1, D->p[2 * k + 1]);
        }
        if (bx1 * 2 < tx0 || bx0 * 2 > tx1 || by1 * 2 < ty0 || by0 * 2 > ty1) {
            continue;
        }
        decal_draw(D, tx0, ty0, tx1, ty1);
    }
    l1_update(tx0, ty0, tx1, ty1);
}
// the box of a part at a place (texels)
static void spr_box2(int i, float x, float y, float a, int *x0, int *y0, int *x1, int *y1)
{
    const spr_t *sp = &s_spr[i];
    float c = cosf(a), s = -sinf(a);
    float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
    for (int q = 0; q < 4; q++) {
        float lx = ((q & 1) ? (float)(sp->w - sp->ox) : (float)-sp->ox) / 2,
              ly = ((q & 2) ? (float)(sp->h - sp->oy) : (float)-sp->oy) / 2;
        float wx = x + c * lx + s * ly, wy = y - s * lx + c * ly;
        bx0 = fminf(bx0, wx);
        bx1 = fmaxf(bx1, wx);
        by0 = fminf(by0, wy);
        by1 = fmaxf(by1, wy);
    }
    *x0 = (int)floorf(bx0 * 2) - 2;
    *y0 = (int)floorf(by0 * 2) - 2;
    *x1 = (int)ceilf(bx1 * 2) + 3;
    *y1 = (int)ceilf(by1 * 2) + 3;
}
// The boxes of the world layer to make again in this frame: a part that leaves the layer, a part that comes to rest, a
// picture that changed. They are kept (a box that touches another one joins it) and made again once, at the end of the
// sync of the frame: many parts that start to move together cost one repair of their region, not one each.
#define MAXDIRTY 8
static int s_dirty[MAXDIRTY][4], s_ndirty;
static void dirty_add(int x0, int y0, int x1, int y1)
{
    for (int k = 0; k < s_ndirty; k++) {
        int *d = s_dirty[k];
        if (x0 <= d[2] && x1 >= d[0] && y0 <= d[3] && y1 >= d[1]) {
            d[0] = x0 < d[0] ? x0 : d[0];
            d[1] = y0 < d[1] ? y0 : d[1];
            d[2] = x1 > d[2] ? x1 : d[2];
            d[3] = y1 > d[3] ? y1 : d[3];
            return;
        }
    }
    if (s_ndirty == MAXDIRTY) {
        repair(x0, y0, x1, y1);
        return;
    }
    int *d = s_dirty[s_ndirty++];
    d[0] = x0;
    d[1] = y0;
    d[2] = x1;
    d[3] = y1;
}
static void dirty_flush(void)
{
    for (int k = 0; k < s_ndirty; k++) {
        repair(s_dirty[k][0], s_dirty[k][1], s_dirty[k][2], s_dirty[k][3]);
    }
    s_ndirty = 0;
}
// a part leaves the world layer (it moves)
static void bake_out(int i)
{
    if (!s_bk[i].in) {
        return;
    }
    int x0, y0, x1, y1;
    spr_box2(i, s_bk[i].x, s_bk[i].y, s_bk[i].a, &x0, &y0, &x1, &y1);
    s_bk[i].in = false;
    dirty_add(x0, y0, x1, y1);
}
// a part comes to rest: into the world layer at its place now (the whole box again: the z order stays correct)
static void bake_in(int i, float x, float y, float a)
{
    if (s_bk[i].in || !s_spr_ok[i]) {
        return;
    }
    s_bk[i] = (bk_t){true, x, y, a};
    int x0, y0, x1, y1;
    spr_box2(i, x, y, a, &x0, &y0, &x1, &y1);
    dirty_add(x0, y0, x1, y1);
}
// the picture of a part that rests changed (spr_again + the painter): the world layer again in its box
static void bake_refresh(int i)
{
    if (!s_bk[i].in) {
        return;
    }
    int x0, y0, x1, y1;
    spr_box2(i, s_bk[i].x, s_bk[i].y, s_bk[i].a, &x0, &y0, &x1, &y1);
    dirty_add(x0, y0, x1, y1);
}
static void decal_add(const float *pts, int n, uint16_t col, uint16_t lit)
{
    if (!s_dec || n < 3 || n > 6) {
        return;
    }
    if (s_ndec >= MAXDECAL) {
        return;
    }
    decal_t *D = &s_dec[s_ndec++];
    memcpy(D->p, pts, sizeof(float) * (size_t)(2 * n));
    D->n = (uint8_t)n;
    D->col = col;
    D->lit = lit;
    float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f;
    for (int k = 0; k < n; k++) {
        bx0 = fminf(bx0, pts[2 * k]);
        bx1 = fmaxf(bx1, pts[2 * k]);
        by0 = fminf(by0, pts[2 * k + 1]);
        by1 = fmaxf(by1, pts[2 * k + 1]);
    }
    int x0 = clampi((int)(bx0 * 2) - 2, 0, W2), y0 = clampi((int)(by0 * 2) - 2, 0, H2),
        x1 = clampi((int)(bx1 * 2) + 3, 0, W2), y1 = clampi((int)(by1 * 2) + 3, 0, H2);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    decal_draw(D, x0, y0, x1, y1);
    l1_update(x0, y0, x1, y1);
}
// the world layer from the bare stand with each part at its rest place (the start of a level, and of a replay)
static void compose(void)
{
    s_ndirty = 0;
    for (int y = 0; y < H2; y++) {
        bare_row(y, 0, W2, s_l2 + y * W2);
    }
    for (int q = 0; q < s_nz; q++) {
        bake_part(s_zorder[q], 0, 0, W2, H2);
    }
    s_ndec = 0;
    l1_update(0, 0, W2, H2);
}

// ---------------------------------------------------------------- the shards of the panes (pictures only)
typedef struct {
    float x, y, vx, vy, a, va, s, life;
    uint8_t rest, tint;
    uint16_t seed;
} shard_t;
#define MAXSHARD 360
static shard_t *s_sh;
static int s_nsh;
static uint32_t s_vrng = 77;
static inline float vrnd(void)
{
    s_vrng ^= s_vrng << 13;
    s_vrng ^= s_vrng >> 17;
    s_vrng ^= s_vrng << 5;
    return (float)(s_vrng >> 8) / 16777216.0f;
}
static int s_tinkle;  // shards that hit the floor in this frame (a sound)
static float s_tinkle_v, s_tinkle_x;
static float floor_at(float x);
static void shard_burst(int pi, int k, const tp_body_t *b, float x0, float y0, float x1, float y1, float wcx, float wcy,
                        float push_x, float push_y)
{
    float area = (x1 - x0) * (y1 - y0);
    int n = clampi((int)(area / 12), 6, 28);
    (void)pi;
    (void)k;
    for (int q = 0; q < n && s_sh; q++) {
        if (s_nsh >= MAXSHARD) {
            // (full: the oldest shard that rests gives its place)
            int r = -1;
            for (int i = 0; i < s_nsh && r < 0; i++) {
                if (s_sh[i].rest) {
                    r = i;
                }
            }
            if (r < 0) {
                return;
            }
            s_sh[r] = s_sh[--s_nsh];
        }
        shard_t *S = &s_sh[s_nsh++];
        float lx = x0 + vrnd() * (x1 - x0), ly = y0 + vrnd() * (y1 - y0), wx, wy;
        loc2w(b, lx, ly, &wx, &wy);
        float dx = wx - wcx, dy = wy - wcy, d = sqrtf(dx * dx + dy * dy) + 0.5f, sp = 30 + vrnd() * 70;
        S->x = wx;
        S->y = wy;
        S->vx = b->v.x * PXM + dx / d * sp + push_x * PXM * (0.5f + vrnd());
        S->vy = -b->v.y * PXM + dy / d * sp * 0.6f - 30 - push_y * PXM * vrnd();
        S->a = vrnd() * TAU_;
        S->va = (vrnd() - 0.5f) * 14;
        S->s = 1.0f + vrnd() * 2.0f * fminf(1.5f, area / 160);
        S->life = 0;
        S->rest = 0;
        S->tint = (uint8_t)(vrnd() * 3);
        S->seed = (uint16_t)(vrnd() * 65535);
    }
}
static uint16_t s_glass_tint[3];
static void shards_step(float dt)
{
    for (int i = 0; i < s_nsh; i++) {
        shard_t *S = &s_sh[i];
        if (S->rest) {
            continue;
        }
        S->life += dt;
        S->vy += 400 * dt;
        S->x += S->vx * dt;
        S->y += S->vy * dt;
        S->a += S->va * dt;
        float floor_y = floor_at(S->x) - 0.8f + (float)(S->seed % 5) * 0.4f;
        if (S->y > floor_y && S->x > STAND_X0 && S->x < STAND_X1 && S->y < floor_y + 12) {
            S->y = floor_y;
            if (S->vy > 60) {
                s_tinkle++;
                if (S->vy > s_tinkle_v) {
                    s_tinkle_v = S->vy;
                    s_tinkle_x = S->x;
                }
            }
            if (S->vy > 0) {
                S->vy = -S->vy * 0.22f;
            }
            S->vx *= 0.5f;
            S->va *= 0.55f;
            if (fabsf(S->vy) < 16 && fabsf(S->vx) < 6) {
                S->rest = 1;
                // (a shard that rests becomes a decal of the world layer: it costs nothing more)
                float c = cosf(S->a), s = sinf(S->a), r = S->s;
                float p[6] = {S->x + c * r,        S->y + s * r * 0.4f, S->x - c * r * 0.6f + s * r * 0.5f,
                              S->y - s * r * 0.3f, S->x - c * r * 0.2f, S->y + 0.6f};
                decal_add(p, 3, cmix(s_glass_tint[S->tint], C3(255, 255, 255), 0.35f), C3(255, 255, 255));
                s_sh[i] = s_sh[--s_nsh];
                i--;
                continue;
            }
        }
        if (S->y > WH + 40 || S->x < -60 || S->x > WW + 60) {
            s_sh[i] = s_sh[--s_nsh];
            i--;
        }
    }
}

// ---------------------------------------------------------------- drawing on the screen
// the world layer to the screen at the camera: the 2x layer at a big zoom, the 1x one, or the 1/2x one at a small
// zoom (the sky is in the layers). Rows over and under the world repeat its first and its last row (the dark sky, the
// dark table); the columns out of it repeat its edge.
static void blit_world(void)
{
    float z = s_cam.z;
    const uint16_t *L = s_l1;
    int lw = WW, lh = WH;
    float ks = 1.0f;
    if (z > 1.45f) {
        L = s_l2;
        lw = W2;
        lh = H2;
        ks = 2.0f;
    } else if (z < 0.74f) {
        L = s_l05;
        lw = WQ;
        lh = HQ;
        ks = 0.5f;
    }
    int32_t du = (int32_t)(ks / z * 65536);
    int32_t u0 = (int32_t)((s_cam.x + (0.5f - s_cx - s_cam.sx) / z) * ks * 65536);
    int32_t lim = (lw - 1) << 16;
    // (the columns where the source is in the layer: a loop with no test)
    int xa = 0, xb = gfx_w;
    while (xa < xb && u0 + xa * du < 0) {
        xa++;
    }
    while (xb > xa && u0 + (xb - 1) * du > lim) {
        xb--;
    }
    for (int y = 0; y < gfx_h; y++) {
        int ly = clampi((int)((s_cam.y + ((float)y + 0.5f - s_cy - s_cam.sy) / z) * ks), 0, lh - 1);
        const uint16_t *src = L + ly * lw;
        uint16_t *d = g_fb + y * gfx_w;
        for (int x = 0; x < xa; x++) {
            d[x] = src[0];
        }
        int32_t u = u0 + xa * du;
        int x = xa;
        for (; x + 4 <= xb; x += 4) {
            d[x] = src[u >> 16];
            u += du;
            d[x + 1] = src[u >> 16];
            u += du;
            d[x + 2] = src[u >> 16];
            u += du;
            d[x + 3] = src[u >> 16];
            u += du;
        }
        for (; x < xb; x++, u += du) {
            d[x] = src[u >> 16];
        }
        for (x = xb; x < gfx_w; x++) {
            d[x] = src[lw - 1];
        }
    }
}

// a part that moves: its picture on the screen
static void draw_spr_live(int i, float x, float y, float a, int am)
{
    if (!s_spr_ok[i]) {
        return;
    }
    float c = cosf(a), s = -sinf(a);
    float k = s_cam.z, ox = s_cam.x - (s_cx + s_cam.sx) / k, oy = s_cam.y - (s_cy + s_cam.sy) / k;
    blit_spr(g_fb, gfx_w, gfx_h, k, ox, oy, &s_spr[i], x, y, c, s, 0, 0, gfx_w, gfx_h, am, false);
}
// the canvas on the screen at the camera (the painters draw world px)
static void cv_screen(void)
{
    float k = s_cam.z;
    cv_set(g_fb, NULL, gfx_w, gfx_h, k, s_cam.x - (s_cx + s_cam.sx) / k, s_cam.y - (s_cy + s_cam.sy) / k, NULL);
}
