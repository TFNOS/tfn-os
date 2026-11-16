// BOULEVARD: the view (included by app_blvd.c). The look of the look page (g_blvd.js): a strip
// of the board in perspective in front of the Tel Aviv skyline, the camera follows the token of the player whose turn
// it is; white houses and towers on the streets, the pawns hop tile by tile, the two dice tumble and land, the card of a
// street comes up, the shekels fly from one panel to another. The view plays the event log of the rules (bev_t) one by
// one: a shown copy of the positions and the cash moves with the events, not at once.

// the width of the frame (enter(): gfx_w; 640 on the 3.49, 320 on a narrow frame); the street view is VH rows high; a
// whole screen is FH rows (gfx_h): a narrow frame has a bar of buttons under the view
static int s_vw = 640;
#define VW s_vw
// The Tab5 (640 x 360): the street view of the 3.49 at 1.5 times (a bigger board, bigger pawns), lower on the frame;
// a sky over the skyline fills the rows above it, and the buttons go to the bar under the board.
#if BOARD_UI_TABLET
#define TAB5 1
#else
#define TAB5 0
#endif
// The round frame (gfx_round(): 466 x 466 on the 1.75C, 412 on the Watcher): all the screens are round (RPLAY:
// the street view 1.6 times bigger in the middle of the circle, the sky over it, the panels in the top cap, the buttons
// on the bottom arc; the title, the setup, the friends, MANAGE, TRADE and MAP have their own round places in
// app_blvd.c: rows in the width of the circle, the MAP as a ring of the 40 tiles).
#define RPLAY  gfx_round()
#define VH     (TAB5 ? 360 : RPLAY ? gfx_h : 172)
#define FH     gfx_h
#define NARROW (VW < 400 || RPLAY)
#define ZB     (TAB5 ? 1.5f : RPLAY ? 1.6f : 1.0f)               // the base zoom of the board (s_camz)
#define DY     (TAB5 ? 130 : RPLAY ? gfx_h * 36 / 100 : 0)       // the street view moves down by DY rows
#define SKY_Y0 (TAB5 ? 106 : RPLAY ? gfx_h * 36 / 100 - 24 : 0)  // the screen row of the row 0 of the skyline picture
#define TW     120.0f                                            // the width of a tile on the board (world units)
#define YN     (156.0f + DY)                                     // the near edge of the board on the screen
#define DEP    48.0f                                             // the depth of the board on the screen
#define KZ     0.6f
static float s_camx, s_camz = 1, s_shake;
static float sZ(float z)
{
    return 1.0f / (1.0f + z * KZ);
}
static void P(float X, float Z, float h, float *sx, float *sy)
{
    float s = sZ(Z), s1 = sZ(1);
    *sx = VW / 2 + (X - s_camx) * s * s_camz;
    *sy = YN - DEP * (1 - s) / (1 - s1) * s_camz - h * s * s_camz;
}
static uint16_t C3(const uint8_t *c)
{
    return tfd_rgb(c[0], c[1], c[2]);
}
static uint16_t CM(const uint8_t *c, float k)
{
    int r = (int)(c[0] * k), g = (int)(c[1] * k), b = (int)(c[2] * k);
    return tfd_rgb(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}
// a part of the board floor: a trapezoid with a flat top and bottom (Z0 and Z1 give one screen row each). The general
// soft polygon of tfdraw costs 4 float passes for each pixel (the board took 85-134 ms a frame on the 3.49): here the
// inside of each row is one solid span, and only the two side pixels and the top and bottom rows are soft.
static void quadP(float X0, float X1, float Z0, float Z1, uint16_t col, int a)
{
    float ax, ay, bx, by, cx, cy, dx, dy;
    P(X0, Z0, 0, &ax, &ay);
    P(X1, Z0, 0, &bx, &by);
    P(X1, Z1, 0, &cx, &cy);
    P(X0, Z1, 0, &dx, &dy);
    // (top = the row of d, c; bottom = the row of a, b)
    if (dy > ay) {
        float t;
        t = ax;
        ax = dx;
        dx = t;
        t = ay;
        ay = dy;
        dy = t;
        t = bx;
        bx = cx;
        cx = t;
        t = by;
        by = cy;
        cy = t;
    }
    const float yt = dy, yb = ay;
    if (yb - yt < 0.01f) {
        return;
    }
    int r0 = (int)floorf(yt), r1 = (int)ceilf(yb) - 1;
    if (r0 < 0) {
        r0 = 0;
    }
    if (r1 > VH - 1) {
        r1 = VH - 1;
    }
    for (int y = r0; y <= r1; y++) {
        const float y0 = fmaxf(yt, (float)y), y1 = fminf(yb, (float)y + 1), cov = y1 - y0;
        if (cov <= 0) {
            continue;
        }
        const float t = ((y0 + y1) * 0.5f - yt) / (yb - yt);
        const float xl = dx + (ax - dx) * t, xr = cx + (bx - cx) * t;
        if (xr <= xl) {
            continue;
        }
        const int ra = cov >= 0.999f ? a : (int)(a * cov);
        const int il = (int)floorf(xl), ir = (int)floorf(xr);
        if (il == ir) {
            tfd_px(il, y, col, (int)(ra * (xr - xl)));
            continue;
        }
        tfd_px(il, y, col, (int)(ra * (il + 1 - xl)));
        tfd_hspan(il + 1, ir, y, col, ra);
        tfd_px(ir, y, col, (int)(ra * (xr - ir)));
    }
}

// ---------------------------------------------------------------- the shekel sign (no font has it): two hooks
static void shekel(float x, float base, float h, uint16_t col)
{
    float w = h * 0.9f, t = h > 12 ? 2.0f : 1.4f;
    // the left hook: down the left side, along the bottom, up the right middle
    tfd_line(x, base - h, x, base, t, col, 256);
    tfd_line(x, base, x + w * 0.62f, base, t, col, 256);
    tfd_line(x + w * 0.62f, base, x + w * 0.62f, base - h * 0.55f, t, col, 256);
    // the right hook: up the right side, along the top, down the left middle
    tfd_line(x + w, base, x + w, base - h, t, col, 256);
    tfd_line(x + w, base - h, x + w * 0.38f, base - h, t, col, 256);
    tfd_line(x + w * 0.38f, base - h, x + w * 0.38f, base - h * 0.45f, t, col, 256);
}
// a sum of money: the digits and the shekel sign (Hebrew order: the number, then the sign at its left in a Hebrew
// line; the board shows "₪350" in English and "350 ₪" in Hebrew); cx: the middle; returns nothing
static void money_at(const mfont_t *f, int cx, int base, int32_t v, uint16_t col, uint16_t ol)
{
    char b[16];
    snprintf(b, sizeof b, "%ld", (long)(v < 0 ? -v : v));
    int w = mf_width(f, b, 1), h = f == &MF_N16 ? 12 : f == &MF_N20 ? 15 : f == &MF_N28 ? 21 : 9;
    float sw = h * 0.9f + 3;
    int x0 = cx - (int)((w + sw) / 2);
    if (s_lang) {
        mf_text_oc(f, x0 + w / 2, base, b, col, ol, 1, 1);
        shekel((float)(x0 + w + 3), (float)base, (float)h, col);
    } else {
        shekel((float)x0, (float)base, (float)h, col);
        mf_text_oc(f, x0 + (int)sw + w / 2, base, b, col, ol, 1, 1);
    }
}

// ---------------------------------------------------------------- the skyline (made once, PSRAM): 980 x 110
// The picture of Blender (Tel Aviv from the sea, bv_art.c BA_SKY_JPG); if it does not decode, the drawn skyline.
#define SKY_W 980
#define SKY_H 110
static uint16_t *s_sky;
static bool s_sky_art;  // (the Blender picture: its row 0 is the row 0 of the screen)
static uint32_t s_srng = 616;
static float srnd(void)
{
    s_srng = s_srng * 1664525u + 1013904223u;
    return (s_srng >> 8) / 16777216.0f;
}
#ifdef ESP_PLATFORM
typedef struct {
    uint32_t pos;
} sky_src_t;
static UINT sky_in(JDEC *jd, BYTE *buf, UINT n)
{
    sky_src_t *s = (sky_src_t *)jd->device;
    if (s->pos + n > BA_SKY_N) {
        n = BA_SKY_N - s->pos;
    }
    if (buf) {
        memcpy(buf, BA_SKY_JPG + s->pos, n);
    }
    s->pos += n;
    return n;
}
static UINT sky_out(JDEC *jd, void *bm, JRECT *r)
{
    const uint8_t *c = (const uint8_t *)bm;
    for (int y = r->top; y <= r->bottom; y++) {
        for (int x = r->left; x <= r->right; x++, c += 3) {
            if (y < SKY_H && x < SKY_W) {
                s_sky[y * SKY_W + x] = tfd_rgb(c[0], c[1], c[2]);
            }
        }
    }
    return 1;
}
static bool sky_art(void)
{
    void *work = heap_caps_malloc(4096, MALLOC_CAP_8BIT);
    if (!work) {
        return false;
    }
    sky_src_t src = {0};
    JDEC jd;
    JRESULT r = jd_prepare(&jd, sky_in, work, 4096, &src);
    if (r == JDR_OK) {
        r = jd_decomp(&jd, sky_out, 0);
    }
    heap_caps_free(work);
    return r == JDR_OK;
}
#else
// the PC tests: the raw picture of tools/mk_art.py (raw/sky.raw)
static bool sky_art(void)
{
    FILE *fp = fopen("../w232_blvd3d/raw/sky.raw", "rb");
    if (!fp) {
        fp = fopen("w232_blvd3d/raw/sky.raw", "rb");
    }
    if (!fp) {
        return false;
    }
    bool ok = fread(s_sky, 2, (size_t)SKY_W * SKY_H, fp) == (size_t)SKY_W * SKY_H;
    fclose(fp);
    return ok;
}
#endif
static void sky_make(void)
{
    if (!s_sky) {
        return;
    }
    s_sky_art = sky_art();
    if (s_sky_art) {
        return;
    }
    uint16_t *keep = g_fb;
    int kw = gfx_w, kh = gfx_h;
    g_fb = s_sky;
    gfx_w = SKY_W;
    gfx_h = SKY_H;
    tfd_screen();
    tfd_noclip();
    // the sky: deep blue at the top, light blue, then a warm band over the sea
    for (int y = 0; y < SKY_H; y++) {
        float t = y / (float)SKY_H;
        int r, g, b;
        if (t < 0.55f) {
            float u = t / 0.55f;
            r = (int)(95 + 73 * u);
            g = (int)(179 + 41 * u);
            b = 255;
        } else if (t < 0.85f) {
            float u = (t - 0.55f) / 0.3f;
            r = (int)(168 + 87 * u);
            g = (int)(220 + 6 * u);
            b = (int)(255 - 71 * u);
        } else {
            r = 255;
            g = 220;
            b = 170;
        }
        tfd_hspan(0, SKY_W - 1, y, tfd_rgb(r, g, b), 256);
    }
    tfd_glow(140, 88, 120, tfd_rgb(255, 241, 200), 110);
    // the sea with its sparkles, the hill of old Jaffa with its clock tower
    tfd_rect(0, 92, SKY_W, 18, tfd_rgb(47, 143, 208), 256);
    for (int i = 0; i < 60; i++) {
        tfd_rect((int)(srnd() * SKY_W), 93 + (int)(srnd() * 14), 3 + (int)(srnd() * 6), 1, tfd_rgb(255, 255, 255), 150);
    }
    {
        float q[12];
        int n = 0;
        for (int i = 0; i <= 5; i++) {
            float u = i / 5.0f;
            q[n++] = 150 * u;
            q[n++] = 96 - 26 * sinf(u * 3.14159f) + 6 * u;
        }
        float p2[16];
        int m = 0;
        for (int i = 0; i < n; i++) {
            p2[m++] = q[i];
        }
        p2[m++] = 150;
        p2[m++] = 100;
        p2[m++] = 0;
        p2[m++] = 100;
        tfd_poly(p2, m / 2, tfd_rgb(233, 201, 154), 256);
    }
    for (int i = 0; i < 12; i++) {
        tfd_rect(10 + i * 11, (int)(84 - sinf(i * 0.4f) * 8), 9, 14, tfd_rgb(217, 176, 122), 256);
    }
    tfd_rect(66, 62, 6, 26, tfd_rgb(201, 154, 98), 256);
    {
        float t3[6] = {64, 62, 69, 54, 74, 62};
        tfd_poly(t3, 3, tfd_rgb(201, 154, 98), 256);
    }
    tfd_disc(69, 68, 1.6f, tfd_rgb(255, 255, 255), 256);
    // the city: white blocks with ribbon windows
    for (int i = 0; i < 70; i++) {
        int bx = 150 + (int)(srnd() * 830), bw = 10 + (int)(srnd() * 22), bh = 10 + (int)(srnd() * 34);
        float m = srnd() * 0.7f;
        tfd_rect(bx, 96 - bh, bw, bh, tfd_rgb((int)(244 - 37 * m), (int)(239 - 15 * m), (int)(230 + 8 * m)), 256);
        for (int y = 96 - bh + 3; y < 94; y += 4) {
            tfd_rect(bx + 1, y, bw - 3, 1, tfd_rgb(80, 110, 140), 90);
        }
    }
    // the three Azrieli towers: round, triangle, square; a tall tower in the middle
    const int az = 640;
    tfd_rect(az - 9, 34, 18, 62, tfd_rgb(215, 230, 242), 256);
    tfd_disc(az, 34, 9, tfd_rgb(215, 230, 242), 256);
    {
        float tr[8] = {az + 22, 22, az + 40, 30, az + 38, 96, az + 20, 96};
        tfd_poly(tr, 4, tfd_rgb(188, 210, 228), 256);
    }
    tfd_rect(az + 50, 28, 17, 68, tfd_rgb(201, 219, 233), 256);
    for (int k = 0; k < 3; k++) {
        int tx = k == 0 ? az - 9 : k == 1 ? az + 21 : az + 50;
        for (int y = 36; y < 94; y += 3) {
            tfd_rect(tx + 1, y, 16, 1, tfd_rgb(90, 130, 170), 90);
        }
    }
    tfd_rect(420, 40, 14, 56, tfd_rgb(228, 214, 194), 256);
    for (int y = 42; y < 94; y += 3) {
        tfd_rect(421, y, 12, 1, tfd_rgb(120, 100, 80), 90);
    }
    tfd_rect(426, 34, 2, 6, tfd_rgb(208, 192, 168), 256);
    // the palms, the beach
    for (int i = 0; i < 18; i++) {
        float px = 160 + i * 46 + srnd() * 20, ph = 16 + srnd() * 10;
        tfd_line(px, 100, px + 1, 100 - ph, 1.4f, tfd_rgb(122, 90, 58), 256);
        for (int k = 0; k < 6; k++) {
            float a = -3.14159f + k * 0.6f;
            tfd_line(px + 1, 100 - ph, px + 1 + cosf(a) * 9, 100 - ph + 3 + sinf(a) * 1.5f, 1.4f, tfd_rgb(63, 138, 74),
                     256);
        }
    }
    tfd_rect(0, 100, SKY_W, 10, tfd_rgb(242, 217, 168), 256);
    g_fb = keep;
    gfx_w = kw;
    gfx_h = kh;
    tfd_screen();
}
static void sky_draw(void)
{
    int off = (int)fmodf(s_camx * 0.12f, (float)SKY_W);
    if (off < 0) {
        off += SKY_W;
    }
    if (TAB5 || RPLAY) {  // (the round frame too)
        // (the rows over the skyline: a soft blue that ends in the mean colour of its top row)
        static uint16_t top_c;
        static bool top_ok;
        if (!top_ok) {
            int r = 0, g = 0, b = 0, cr, cg, cb, sy0 = s_sky_art ? 0 : 4;
            for (int x = 0; x < SKY_W; x++) {
                tfd_unrgb(s_sky[sy0 * SKY_W + x], &cr, &cg, &cb);
                r += cr;
                g += cg;
                b += cb;
            }
            top_c = tfd_rgb(r / SKY_W, g / SKY_W, b / SKY_W);
            top_ok = true;
        }
        tfd_vgrad(0, 0, VW, SKY_Y0, tfd_mix(top_c, tfd_rgb(40, 96, 190), 150), top_c);
    }
    for (int y = 0; y < SKY_H && y + SKY_Y0 < VH; y++) {
        int sy = s_sky_art ? y : y + 4;
        if (sy >= SKY_H) {
            sy = SKY_H - 1;
        }
        const uint16_t *src = s_sky + sy * SKY_W;
        uint16_t *d = g_fb + (y + SKY_Y0) * VW;
        int n1 = SKY_W - off;
        if (n1 >= VW) {
            memcpy(d, src + off, VW * 2);
        } else {
            memcpy(d, src + off, (size_t)n1 * 2);
            memcpy(d + n1, src, (size_t)(VW - n1) * 2);
        }
    }
}

// ---------------------------------------------------------------- the Blender sprites (bv_art.c, tools/mk_art.py)
// The pawns, the houses, the towers and the dice at rest. A sprite has 7 sizes (BA_MIP); the draw takes the nearest
// bigger size and scales its pixels to the scale kx, ky (against size 1). The anchor (the ground point) goes to X, Y.
// a: the alpha of all (0..256); the rows over ytop are not drawn (a house that grows from the ground).
#define BA_ROW 256
static uint16_t s_bac[BA_ROW], s_baa[BA_ROW];
static void ba_row(const ba_frame_t *f, int r)
{
    uint32_t ro;
    memcpy(&ro, BA_SPR + f->off + 4 * r, 4);
    const uint8_t *p = BA_SPR + f->off + ro;
    int x = 0, w = f->w > BA_ROW ? BA_ROW : f->w;
    while (x < w) {
        int ty = p[0] >> 6, n = (p[0] & 63) + 1;
        p++;
        if (ty == 0) {
            for (int k = 0; k < n && x < w; k++) {
                s_baa[x++] = 0;
            }
            continue;
        }
        for (int k = 0; k < n && x < w; k++, x++) {
            s_bac[x] = (uint16_t)(p[0] | (p[1] << 8));
            p += 2;
            if (ty == 1) {
                s_baa[x] = 256;
            } else {
                s_baa[x] = (uint16_t)(p[0] + (p[0] >> 7));
                p++;
            }
        }
    }
}
static void ba_draw(int sp, float X, float Y, float kx, float ky, int a, int ytop)
{
    float k = kx > ky ? kx : ky;
    if (k <= 0.01f || a <= 0) {
        return;
    }
    int m = 0;
    while (m + 1 < BA_NMIP && BA_MIP[m + 1] >= k) {
        m++;
    }
    const ba_frame_t *f = &BA_FRAMES[BA_FIRST[sp] + m];
    if (f->w <= 1) {
        return;
    }
    float sx = kx / BA_MIP[m], sy = ky / BA_MIP[m];
    int dw = (int)ceilf(f->w * sx), dh = (int)ceilf(f->h * sy);
    int ix0 = (int)floorf(X - f->ax * sx), iy0 = (int)floorf(Y - f->ay * sy);
    int32_t stx = (int32_t)(65536.0f / sx), sty = (int32_t)(65536.0f / sy);
    int y0 = iy0, y1 = iy0 + dh, x0 = ix0, x1 = ix0 + dw;
    if (y0 < ytop) {
        y0 = ytop;
    }
    if (y0 < tfd_cy0) {
        y0 = tfd_cy0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > tfd_cy1) {
        y1 = tfd_cy1;
    }
    if (y1 > gfx_h) {
        y1 = gfx_h;
    }
    if (x0 < tfd_cx0) {
        x0 = tfd_cx0;
    }
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 > tfd_cx1) {
        x1 = tfd_cx1;
    }
    if (x1 > gfx_w) {
        x1 = gfx_w;
    }
    int last = -1, wl = (f->w > BA_ROW ? BA_ROW : f->w) - 1;
    for (int y = y0; y < y1; y++) {
        int r = (int)(((int64_t)(y - iy0) * sty + sty / 2) >> 16);
        if (r >= f->h) {
            r = f->h - 1;
        }
        if (r != last) {
            ba_row(f, r);
            last = r;
        }
        uint16_t *row = g_fb + y * gfx_w;
        int32_t fc = (x0 - ix0) * stx + stx / 2;
        for (int x = x0; x < x1; x++, fc += stx) {
            int c = fc >> 16;
            if (c > wl) {
                c = wl;
            }
            int al = s_baa[c];
            if (!al) {
                continue;
            }
            if (a < 256) {
                al = al * a >> 8;
            }
            row[x] = al >= 256 ? s_bac[c] : tfd_mix(row[x], s_bac[c], al);
        }
    }
}
// the height of a sprite over its anchor at the scale k (its top)
static float ba_top(int sp, float k)
{
    return BA_FRAMES[BA_FIRST[sp]].ay * k;
}
#define PAWN_K  0.40f  // (a pawn of 2.1 m at 40 px/m: about 33 px at the front)
#define HOUSE_K 0.45f  // (the houses and the towers stand on the far band of a tile)
#define TOWER_K 0.80f
#define DIE_K   0.56f

// ---------------------------------------------------------------- the shown state (the events move it)
static int8_t s_spos[MAXP];    // the tile where each pawn stands now
static int32_t s_scash[MAXP];  // the cash on the panels now
static float s_scash_f[MAXP];  // (the count goes to the cash)
static float s_mood[MAXP];
// the hop of one pawn: tile by tile (or one jump)
static int s_hop_p = -1, s_hop_from, s_hop_to, s_hop_back;
static float s_hop_t;
static bool s_hop_jump;
// the dice
typedef struct {
    int v;
    float x, y, vx, vy, t, yaw, spin, ax[3];
    bool rest;
    float base[3], tick;
} die_t;  // (tick: the next roll tick)
static die_t s_dice[2];
static bool s_dice_on;
static float s_sum_t = -1;
// the card that shows (a street to buy, a card of a deck, a trade, a message)
enum {
    CD_NONE,
    CD_BUY,
    CD_DECK,
    CD_INFO
};
static int s_card, s_card_tile, s_card_deck, s_card_id;
static float s_card_t;
// the shekel notes that fly, the texts that rise from a panel
typedef struct {
    float fx, fy, tx, ty, t;
    int v;
} note_t;
#define NNOTE 24
static note_t s_notes[NNOTE];
typedef struct {
    int32_t v;
    int who;
    float life;
    bool plus, on;
} pop_t;
#define NPOP 8
static pop_t s_pops[NPOP];
static int s_bubble_p = -1;  // a word over a panel ("JAM!", "DOUBLE!")
static char s_bubble[32];
static float s_bubble_t;
static int s_new_house = -1;  // the property whose new floor grows now
static float s_new_house_t;

// the box of the panel of player p (n players): one row (two rows of 3 for 5-6 players on a narrow frame)
static void panel_box(int p, int n, int *x, int *y, int *w)
{
    if (RPLAY) {
        // (the round frame: one or two rows in the top cap, each in the width of the circle there)
        int n1 = n <= 3 ? n : n / 2, row = p < n1 ? 0 : 1, per = row ? n - n1 : n1, c = row ? p - n1 : p;
        *y = gfx_h * (n <= 3 ? 11 : 6) / 100 + row * 27;
        int dy = *y - gfx_h / 2, half = (int)sqrtf((float)(gfx_w * gfx_w / 4 - dy * dy)) - 10;
        *w = (2 * half - (per - 1) * 4) / per;
        if (*w > 110) {
            *w = 110;
        }
        *x = VW / 2 - (per * *w + (per - 1) * 4) / 2 + c * (*w + 4);
        return;
    }
    if (!NARROW) {
        *w = n <= 4 ? 120 : 102;
        *x = 6 + p * (*w + 4);
        *y = 3;
        return;
    }
    int per = n <= 4 ? n : 3, c = p % per, r = p / per;
    *w = (VW - 12 - (per - 1) * 4) / per;
    *x = 6 + c * (*w + 4);
    *y = 3 + r * 26;
}
static void panel_xy(int p, int n, float *x, float *y)
{
    int bx, by, w;
    panel_box(p, n, &bx, &by, &w);
    *x = bx + w / 2.0f;
    *y = by + 11;
}
static void notes_fly(int from, int to, int32_t v)
{
    int n = g_v.n;
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < NNOTE; i++) {
            if (s_notes[i].t > 0 && s_notes[i].t < 1) {
                continue;
            }
            note_t *q = &s_notes[i];
            if (from == BANK) {
                q->fx = VW / 2.0f;
                q->fy = 120;
            } else {
                panel_xy(from, n, &q->fx, &q->fy);
            }
            if (to == BANK) {
                q->tx = VW / 2.0f;
                q->ty = 120;
            } else {
                panel_xy(to, n, &q->tx, &q->ty);
            }
            q->t = -k * 0.09f;
            q->v = v >= 200 ? 200 : v >= 100 ? 100 : v >= 50 ? 50 : 20;
            break;
        }
    }
}
static void pop_add(int who, int32_t v, bool plus)
{
    for (int i = 0; i < NPOP; i++) {
        if (s_pops[i].on && s_pops[i].life < 1.6f) {
            continue;
        }
        s_pops[i].who = who;
        s_pops[i].life = 0;
        s_pops[i].plus = plus;
        s_pops[i].v = v;
        s_pops[i].on = true;
        return;
    }
}

// a die: a cube that tumbles and ends with its value on top
static void die_throw(die_t *d, int v, int i)
{
    static const float TOPS[7][3] = {{0, 0, 0},       {0, 0, 0},       {-1.5708f, 0, 0}, {0, 0, -1.5708f},
                                     {0, 0, 1.5708f}, {1.5708f, 0, 0}, {3.14159f, 0, 0}};
    memset(d, 0, sizeof *d);
    d->v = v;
    d->x = VW - 40 + i * 14;
    d->y = 52 - i * 10 + DY;
    d->vx = -250 - i * 26;
    d->vy = 30 + i * 20;
    d->base[0] = TOPS[v][0];
    d->base[1] = TOPS[v][1];
    d->base[2] = TOPS[v][2];
    d->yaw = 0.3f + i * 0.5f;
    d->spin = 14 + i * 3;
    d->ax[0] = 0.6f;
    d->ax[1] = 0.8f;
    d->ax[2] = 0.3f + i * 0.2f;
}
static void die_step(die_t *d, float dt)
{
    d->t += dt;
    if (d->rest) {
        return;
    }
    d->vy += 520 * dt;
    d->x += d->vx * dt;
    d->y += d->vy * dt;
    if (d->y > 128 + DY) {
        d->y = 128 + DY;
        if (fabsf(d->vy) > 60) {
            s_shake = fmaxf(s_shake, 1.2f);
        }
        if (fabsf(d->vy) > 25) {
            die_sound(fminf(1, fabsf(d->vy) / 300), false);  // (the clack of the bounce)
        }
        d->vy = -d->vy * 0.42f;
        d->vx *= 0.72f;
        if (fabsf(d->vy) < 40) {
            d->vy = 0;
        }
    }
    if (d->vy == 0) {
        d->vx *= expf(-dt * 7);
        if (fabsf(d->vx) < 4) {
            d->rest = true;
        }
        // (the roll: a soft tick now and then, faster when the die is fast)
        else if ((d->tick -= dt) <= 0) {
            die_sound(0, true);
            d->tick = 0.03f + 2.4f / (fabsf(d->vx) + 20);
        }
    }
}
typedef float m3[3][3];
static void rot_m(m3 r, float ax, float ay, float az)
{
    float cx = cosf(ax), sx = sinf(ax), cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
    r[0][0] = cy * cz;
    r[0][1] = -cy * sz;
    r[0][2] = sy;
    r[1][0] = sx * sy * cz + cx * sz;
    r[1][1] = -sx * sy * sz + cx * cz;
    r[1][2] = -sx * cy;
    r[2][0] = -cx * sy * cz + sx * sz;
    r[2][1] = cx * sy * sz + sx * cz;
    r[2][2] = cx * cy;
}
static void mm3(m3 o, m3 a, m3 b)
{
    m3 r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i][j] = 0;
            for (int k = 0; k < 3; k++) {
                r[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    memcpy(o, r, sizeof r);
}
static void axis_m(m3 r, const float *a, float th)
{
    float l = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), x = a[0] / l, y = a[1] / l, z = a[2] / l, c = cosf(th),
          s = sinf(th), t = 1 - c;
    r[0][0] = t * x * x + c;
    r[0][1] = t * x * y - s * z;
    r[0][2] = t * x * z + s * y;
    r[1][0] = t * x * y + s * z;
    r[1][1] = t * y * y + c;
    r[1][2] = t * y * z - s * x;
    r[2][0] = t * x * z - s * y;
    r[2][1] = t * y * z + s * x;
    r[2][2] = t * z * z + c;
}
static void die_draw(const die_t *d, bool red)
{
    const float S = 9 * ZB;
    float th = d->rest ? 0 : fmaxf(0, (1.4f - d->t) * d->spin);
    m3 A, B, M, R;
    rot_m(A, 0, d->yaw, 0);
    rot_m(B, d->base[0], d->base[1], d->base[2]);
    mm3(M, A, B);
    axis_m(R, d->ax, th);
    mm3(M, M, R);
    rot_m(A, 0.55f, 0, 0);
    mm3(M, A, M);
    static const int8_t V[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                   {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
    float px[8], py[8];
    for (int i = 0; i < 8; i++) {
        float r0 = M[0][0] * V[i][0] + M[0][1] * V[i][1] + M[0][2] * V[i][2];
        float r1 = M[1][0] * V[i][0] + M[1][1] * V[i][1] + M[1][2] * V[i][2];
        px[i] = d->x + r0 * S;
        py[i] = d->y + r1 * S;
    }
    static const struct {
        int8_t v[4], n[3], val;
    } F[6] = {{{3, 2, 1, 0}, {0, 0, -1}, 2}, {{4, 5, 6, 7}, {0, 0, 1}, 5},  {{0, 1, 5, 4}, {0, -1, 0}, 1},
              {{7, 6, 2, 3}, {0, 1, 0}, 6},  {{0, 4, 7, 3}, {-1, 0, 0}, 4}, {{1, 2, 6, 5}, {1, 0, 0}, 3}};
    static const float PIPS[7][6][2] = {
            {{0}},
            {{0.5f, 0.5f}},
            {{0.27f, 0.27f}, {0.73f, 0.73f}},
            {{0.25f, 0.25f}, {0.5f, 0.5f}, {0.75f, 0.75f}},
            {{0.27f, 0.27f}, {0.73f, 0.27f}, {0.27f, 0.73f}, {0.73f, 0.73f}},
            {{0.25f, 0.25f}, {0.75f, 0.25f}, {0.5f, 0.5f}, {0.25f, 0.75f}, {0.75f, 0.75f}},
            {{0.27f, 0.22f}, {0.73f, 0.22f}, {0.27f, 0.5f}, {0.73f, 0.5f}, {0.27f, 0.78f}, {0.73f, 0.78f}}};
    tfd_disc(d->x + 3, 137 + DY, S, tfd_rgb(20, 30, 60), 70);
    if (d->rest) {
        ba_draw((red ? BA_DIER_1 : BA_DIE_1) + d->v - 1, d->x, d->y + S, DIE_K * ZB, DIE_K * ZB, 256, -9999);
        return;
    }
    static const uint8_t WHITE[3] = {251, 248, 242}, RED[3] = {232, 71, 58};
    const uint8_t *base = red ? RED : WHITE;
    for (int f = 0; f < 6; f++) {
        float n0 = M[0][0] * F[f].n[0] + M[0][1] * F[f].n[1] + M[0][2] * F[f].n[2];
        float n1 = M[1][0] * F[f].n[0] + M[1][1] * F[f].n[1] + M[1][2] * F[f].n[2];
        float n2 = M[2][0] * F[f].n[0] + M[2][1] * F[f].n[1] + M[2][2] * F[f].n[2];
        if (n2 > -0.05f) {
            continue;
        }
        float lit = fminf(1, fmaxf(0.3f, 0.55f - n1 * 0.35f - n0 * 0.15f));
        float q[8];
        for (int k = 0; k < 4; k++) {
            q[2 * k] = px[F[f].v[k]];
            q[2 * k + 1] = py[F[f].v[k]];
        }
        tfd_poly(q, 4, CM(base, lit + 0.1f), 256);
        int val = F[f].val;
        float fz = fabsf(n2);
        for (int k = 0; k < val; k++) {
            float u = PIPS[val][k][0], v = PIPS[val][k][1];
            float axp = q[0] + (q[2] - q[0]) * u, ayp = q[1] + (q[3] - q[1]) * u;
            float bxp = q[6] + (q[4] - q[6]) * u, byp = q[7] + (q[5] - q[7]) * u;
            float cx = axp + (bxp - axp) * v, cy = ayp + (byp - ayp) * v;
            uint16_t pc = red ? tfd_rgb(255, 255, 255) : val == 1 ? tfd_rgb(210, 58, 42) : tfd_rgb(31, 36, 51);
            tfd_disc(cx, cy, (val == 1 ? 1.9f : 1.3f) * (0.5f + fz * 0.5f), pc, 256);
        }
    }
}

// ---------------------------------------------------------------- the board
static int s_turn_shown;  // the player whose turn the view shows
static float token_x(int p)
{
    float off = (p - (g_v.n - 1) * 0.5f) * (g_v.n > 4 ? 11 : 15);
    if (p == s_hop_p && s_hop_t >= 0) {
        if (s_hop_jump) {
            float f = fminf(1, s_hop_t);
            int d = s_hop_to - s_hop_from;
            return (s_hop_from + d * f) * TW + TW / 2 + off;
        }
        int k = (int)s_hop_t;
        float f = s_hop_t - k;
        return (s_hop_from + (s_hop_back ? -(k + f) : (k + f))) * TW + TW / 2 + off;
    }
    return s_spos[p] * TW + TW / 2 + off;
}
// a White City house or a tower (the Blender sprites); grow: a new one rises from the ground (0..1)
static void building(float X, float Z, bool tower, int owner, float grow)
{
    if (grow < 0.03f) {
        return;
    }
    float x, y;
    P(X, Z, 0, &x, &y);
    float s = sZ(Z) * s_camz, k = s * (tower ? TOWER_K : HOUSE_K);
    int sp = tower ? BA_TOWER : BA_HOUSE;
    float top = y - ba_top(sp, k);
    ba_draw(sp, x, y, k, k, 256, grow < 1 ? (int)(y - (y - top) * grow) : -9999);
    if (tower && owner < MAXP && grow >= 1) {
        // the flag of the player who has the tower, on its roof
        float fh = 9 * s;
        tfd_line(x, top + 2, x, top - fh, 1.2f, tfd_rgb(70, 80, 100), 256);
        tfd_rect((int)x + 1, (int)(top - fh), (int)(8 * s) + 2, (int)(5 * s) + 1, C3(SEAT_RGB[owner]), 256);
    }
}
static void pawn(int p, float X, float Z, float lift, float sq)
{
    float x, y;
    P(X, Z, 0, &x, &y);
    float s = sZ(Z) * s_camz;
    tfd_disc(x + 3 * s, y + 1, 10 * s * (1 - lift / 70), tfd_rgb(20, 30, 60), 70);
    // the Blender pawn of the seat colour (a squash when it lands)
    float yy = y - lift * s, k = s * PAWN_K;
    ba_draw(BA_PAWN_0 + p, x, yy, k * (1 + sq * 0.25f), k * (1 - sq * 0.25f), 256, -9999);
}
// the text on a tile (a name of one or two lines, the price): a real face, centred, not scaled
static void tile_text(int t, float X0)
{
    const tile_t *T = &TILES[t];
    float cx, cy;
    P(X0 + TW / 2, 0.45f, 0, &cx, &cy);
    if (cx < -60 || cx > VW + 60) {
        return;
    }
    char b[48];
    snprintf(b, sizeof b, "%s", T->board[s_lang]);
    char *bar = strchr(b, '|');
    uint16_t ink = tfd_rgb(31, 42, 68);
    if (bar) {
        *bar = 0;
        ui_text_c(&KF_LABEL, (int)cx, (int)cy - 6, b, ink);
        ui_text_c(&KF_LABEL, (int)cx, (int)cy + 5, bar + 1, ink);
    } else {
        ui_text_c(&KF_LABEL, (int)cx, (int)cy, b, ink);
    }
    float px, py;
    P(X0 + TW / 2, 0.14f, 0, &px, &py);
    if (T->kind == TK_STREET || T->kind == TK_TRANS || T->kind == TK_UTIL) {
        int pr = PROP_OF[t];
        const bpr_t *R = &g_v.R[pr];
        if (R->owner == NOBODY) {
            money_at(&MF_TINY, (int)px, (int)py + 3, T->price, tfd_rgb(90, 100, 120), tfd_rgb(248, 239, 220));
        } else if (R->mort) {
            ui_text_c(&KF_LABEL, (int)px, (int)py + 3, s_lang ? "משועבד" : "MORTGAGED", tfd_rgb(160, 60, 50));
        }
    } else if (T->kind == TK_TAX) {
        money_at(&MF_TINY, (int)px, (int)py + 3, T->price, tfd_rgb(160, 60, 50), tfd_rgb(248, 239, 220));
    } else if (T->kind == TK_START) {
        money_at(&MF_TINY, (int)px, (int)py + 3, START_BONUS, tfd_rgb(192, 57, 43), tfd_rgb(248, 239, 220));
    }
}
#if defined(BV_PROF) && defined(ESP_PLATFORM)
#include "esp_timer.h"
static int64_t s_bd[4], s_bd_t;
#define BD(i)                                                                                                          \
    do {                                                                                                               \
        int64_t _t = esp_timer_get_time();                                                                             \
        s_bd[i] += _t - s_bd_t;                                                                                        \
        s_bd_t = _t;                                                                                                   \
    } while (0)
#define BD0() (s_bd_t = esp_timer_get_time())
#else
#define BD(i)                                                                                                          \
    do {                                                                                                               \
    } while (0)
#define BD0()                                                                                                          \
    do {                                                                                                               \
    } while (0)
#endif
static void board_draw(void)
{
    int k0 = (int)floorf((s_camx - 460 / s_camz) / TW) - 1, k1 = (int)ceilf((s_camx + 460 / s_camz) / TW) + 1;
    float e0x, e0y, e1x, e1y;
    P(k0 * TW, 0, 0, &e0x, &e0y);
    P(k1 * TW, 0, 0, &e1x, &e1y);
    tfd_rect((int)e0x, (int)e0y, (int)(e1x - e0x), 9, tfd_rgb(30, 58, 95), 256);
    tfd_rect((int)e0x, (int)e0y, (int)(e1x - e0x), 1, tfd_rgb(47, 90, 138), 256);
    BD0();
    for (int k = k0; k < k1; k++) {
        int t = ((k % NT) + NT) % NT;
        const tile_t *T = &TILES[t];
        float X0 = k * TW, X1 = X0 + TW;
        quadP(X0, X1, 0, 1, t % 2 ? tfd_rgb(248, 239, 220) : tfd_rgb(239, 228, 204), 256);
        // the band of the group on the far side
        uint16_t gc;
        if (T->group < G_NONE) {
            gc = tfd_rgb(GROUP_RGB[T->group][0], GROUP_RGB[T->group][1], GROUP_RGB[T->group][2]);
        } else if (T->kind == TK_START) {
            gc = tfd_rgb(255, 107, 74);
        } else if (T->kind == TK_TAX) {
            gc = tfd_rgb(201, 193, 178);
        } else if (T->kind == TK_BEACH) {
            gc = tfd_rgb(255, 209, 102);
        } else if (T->kind == TK_JAM || T->kind == TK_WORKS) {
            gc = tfd_rgb(154, 154, 154);
        } else {
            gc = tfd_rgb(255, 210, 63);
        }
        quadP(X0 + 3, X1 - 3, 0.66f, 0.97f, gc, 256);
        if (T->kind == TK_JAM || T->kind == TK_WORKS) {
            for (int s = 0; s < 5; s++) {
                static const uint8_t CARC[5][3] = {
                        {255, 74, 58}, {255, 210, 63}, {59, 140, 255}, {255, 255, 255}, {47, 194, 122}};
                quadP(X0 + 14 + s * 22, X0 + 26 + s * 22, 0.72f, 0.9f, C3(CARC[s]), 256);
            }
        }
        // (the line between two tiles: a thin trapezoid; the soft tfd_line of a leaning line costs about 5 ms each)
        quadP(X0 - 0.6f, X0 + 0.6f, 0, 1, tfd_rgb(40, 50, 80), 140);
        BD(0);
        tile_text(t, X0);
        BD(1);
        // the player who has the street: a flag of his colour at the left of the band
        int pr = PROP_OF[t];
        if (pr >= 0 && g_v.R[pr].owner < MAXP) {
            float fx, fy;
            P(X0 + 10, 0.6f, 0, &fx, &fy);
            tfd_disc(fx, fy, 3.2f * sZ(0.6f) * s_camz, C3(SEAT_RGB[g_v.R[pr].owner]), 256);
        }
    }
    BD(0);
    // the houses and the towers (far first), then the pawns
    for (int k = k0; k < k1; k++) {
        int t = ((k % NT) + NT) % NT, pr = PROP_OF[t];
        if (pr < 0 || !g_v.R[pr].level) {
            continue;
        }
        int lv = g_v.R[pr].level;
        float grow = pr == s_new_house ? fminf(1, s_new_house_t * 3) : 1;
        if (lv >= 5) {
            building(k * TW + TW / 2 + 6, 0.88f, true, g_v.R[pr].owner, grow);
        } else {
            for (int j = 0; j < lv; j++) {
                building(k * TW + 24 + j * 25, 0.88f, false, g_v.R[pr].owner, j == lv - 1 ? grow : 1);
            }
        }
    }
    BD(2);
    for (int i = 0; i < g_v.n; i++) {
        int p = (i + s_turn_shown + 1) % g_v.n;  // (the pawn of the turn last: in front)
        if (g_v.P[p].out) {
            continue;
        }
        float lift = 0, sq = 0;
        if (p == s_hop_p && s_hop_t >= 0) {
            float f = s_hop_jump ? fminf(1, s_hop_t) : s_hop_t - floorf(s_hop_t);
            lift = sinf(f * 3.14159f) * (s_hop_jump ? 60 : 26);
            sq = f < 0.15f ? (0.15f - f) * 4 : 0;
        }
        float Z = 0.22f + (p % 2) * 0.17f;
        float x = token_x(p);
        // (the pawns of the other tiles: only the ones in the view)
        float sx, sy;
        P(x, Z, 0, &sx, &sy);
        if (sx < -30 || sx > VW + 30) {
            continue;
        }
        pawn(p, x, Z, lift, sq);
    }
    BD(3);
}

// ---------------------------------------------------------------- the faces of the players (the panels)
static void face(int p, float cx, float cy, float r)
{
    int lk = g_v.P[p].look % NLOOK;
    const uint8_t *seat = SEAT_RGB[p];
    tfd_disc(cx, cy, r, CM(seat, 0.75f), 256);
    uint16_t hair = C3(LOOK_HAIR[lk]), skin = C3(LOOK_SKIN[lk]);
    int st = LOOK_STYLE[lk];
    if (st == 0 || st == 5) {
        tfd_disc(cx, cy + 1, r * 0.82f, hair, 256);
    }
    tfd_disc(cx, cy + r * 1.15f, r * 0.75f, C3(seat), 256);
    tfd_rect((int)(cx - r * 0.2f), (int)(cy + r * 0.55f), (int)(r * 0.4f) + 1, (int)(r * 0.4f), skin, 256);
    tfd_disc(cx, cy + r * 0.1f, r * 0.55f, skin, 256);
    // the hair on the top of the head
    if (st == 1) {
        for (int k = 0; k < 6; k++) {
            tfd_disc(cx - r * 0.45f + k * r * 0.18f, cy - r * 0.42f, r * 0.17f, hair, 256);
        }
    } else if (st == 3) {
        tfd_disc(cx, cy - r * 0.3f, r * 0.55f, tfd_rgb(47, 79, 216), 256);
        tfd_rect((int)cx, (int)(cy - r * 0.3f), (int)(r * 0.75f), 2, tfd_rgb(47, 79, 216), 256);
    } else {
        tfd_disc(cx, cy - r * 0.32f, r * 0.5f, hair, 256);
        if (st == 2) {
            tfd_disc(cx, cy - r * 0.78f, r * 0.3f, hair, 256);
        }
    }
    tfd_disc(cx, cy + r * 0.18f, r * 0.5f, skin, 256);
    tfd_disc(cx - r * 0.2f, cy + r * 0.08f, 0.9f, tfd_rgb(27, 27, 34), 256);
    tfd_disc(cx + r * 0.2f, cy + r * 0.08f, 0.9f, tfd_rgb(27, 27, 34), 256);
    float m = s_mood[p];
    uint16_t lip = tfd_rgb(122, 42, 42);
    if (m > 0.3f) {
        tfd_arc(cx, cy + r * 0.22f, r * 0.22f, 1.0f, 0.3f, 2.8f, lip, 256);
    } else if (m < -0.3f) {
        tfd_arc(cx, cy + r * 0.52f, r * 0.2f, 1.0f, 3.5f, 5.9f, lip, 256);
    } else {
        tfd_line(cx - r * 0.14f, cy + r * 0.38f, cx + r * 0.14f, cy + r * 0.38f, 1, lip, 256);
    }
}
static const char *pname(int p);
static void panels_draw(void)
{
    int n = g_v.n;
    for (int p = 0; p < n; p++) {
        int x, y, w;
        panel_box(p, n, &x, &y, &w);
        bool on = p == s_turn_shown && g_v.phase != PH_OVER;
        const uint8_t *c = SEAT_RGB[p];
        if (on) {
            tfd_rrect(x - 2, y - 2, w + 4, 27, 6, C3(c), 120);
        }
        tfd_rrect(x, y, w, 23, 5, tfd_rgb(11, 14, 22), 230);
        tfd_rrect(x + 2, y + 2, w - 4, 19, 4, g_v.P[p].out ? tfd_rgb(40, 40, 46) : tfd_rgb(22, 33, 58), 256);
        face(p, x + 12, y + 11.5f, 8);
        uint16_t nc = g_v.P[p].out ? tfd_rgb(120, 120, 130) : on ? tfd_rgb(255, 255, 255) : tfd_rgb(220, 228, 245);
        ui_text_c(&KF_LABEL, x + 23 + (w - 26) / 2, y + 10, pname(p), nc);
        if (g_v.P[p].out) {
            ui_text_c(&KF_LABEL, x + 23 + (w - 26) / 2, y + 20, s_lang ? "פשט רגל" : "BROKE", tfd_rgb(255, 106, 90));
        } else {
            money_at(&MF_TINY, x + 23 + (w - 26) / 2, y + 20, (int32_t)s_scash_f[p], tfd_rgb(255, 226, 122),
                     tfd_rgb(11, 14, 22));
        }
        if (g_v.P[p].jail) {
            tfd_rrect(x + w - 9, y + 3, 6, 6, 1, tfd_rgb(255, 80, 60), 256);
        }
        if (g_v.P[p].cards) {
            tfd_rrect(x + w - 9, y + 12, 6, 6, 1, tfd_rgb(80, 220, 120), 256);
        }
    }
}
static void notes_draw(void)
{
    static const uint8_t NC[4][3] = {{216, 74, 74}, {63, 168, 96}, {232, 162, 58}, {74, 122, 216}};
    for (int i = 0; i < NNOTE; i++) {
        note_t *q = &s_notes[i];
        if (q->t <= 0 || q->t >= 1) {
            continue;
        }
        float f = q->t < 0.5f ? 2 * q->t * q->t : 1 - 2 * (1 - q->t) * (1 - q->t);
        float x = q->fx + (q->tx - q->fx) * f, y = q->fy + (q->ty - q->fy) * f - sinf(f * 3.14159f) * 30;
        int ci = q->v == 20 ? 0 : q->v == 50 ? 1 : q->v == 100 ? 2 : 3;
        float a = q->t * 8;
        float ca = cosf(a) * 6, sa = sinf(a) * 6, cb = -sinf(a) * 3, sb = cosf(a) * 3;
        float qq[8] = {x - ca - cb, y - sa - sb, x + ca - cb, y + sa - sb,
                       x + ca + cb, y + sa + sb, x - ca + cb, y - sa + sb};
        tfd_poly(qq, 4, C3(NC[ci]), 256);
        tfd_disc(x - ca * 0.4f, y - sa * 0.4f, 1.5f, tfd_rgb(255, 255, 255), 160);
    }
    for (int i = 0; i < NPOP; i++) {
        pop_t *q = &s_pops[i];
        if (!q->on || q->life >= 1.6f) {
            continue;
        }
        float px, py;
        panel_xy(q->who, g_v.n, &px, &py);
        float t = q->life / 1.6f;
        int a = (int)(fminf(1, (1 - t) * 3) * 256);
        int y = (int)(40 + t * 10);
        tfd_rrect((int)px - 32, y - 10, 64, 13, 3, tfd_rgb(15, 22, 40), a * 150 / 256);
        uint16_t col = q->plus ? tfd_rgb(62, 224, 138) : tfd_rgb(255, 106, 90);
        money_at(&MF_TINY, (int)px, y, q->v, col, tfd_rgb(15, 22, 40));
        mf_text_oc(&MF_TINY, (int)px - 26, y, q->plus ? "+" : "-", col, tfd_rgb(15, 22, 40), 1, 1);
    }
    if (s_bubble_p >= 0 && s_bubble_t > 0) {
        float px, py;
        panel_xy(s_bubble_p, g_v.n, &px, &py);
        int w = ui_text_w(&KF_TEXT, s_bubble) + 18;
        float k = fminf(1, (2.2f - s_bubble_t) * 6);
        tfd_rrect((int)px - w / 2, 32, w, 22, 8, tfd_rgb(255, 255, 255), (int)(240 * fminf(1, s_bubble_t * 3)));
        ui_text_c(&KF_TEXT, (int)px, 48, s_bubble, tfd_rgb(31, 42, 68));
        (void)k;
    }
}
