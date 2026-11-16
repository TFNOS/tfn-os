// GLASSHOUSE: the painters of the art: the
// painted backdrop of each sky, the top and the walnut front of the stand, the scenery, the picture of each part and
// of each fragile object. They paint once (the start of a level, or a pane that breaks): the frame only copies their
// layers. Included by app_glass.c.

// ---------------------------------------------------------------- the palette
#define C_IRON    HX(0xece6d6)
#define C_IRONSH  HX(0x8f9690)
#define C_IRONLIT HX(0xfff6e2)
#define C_BRICK   HX(0x7a3a2c)
#define C_BRICKLT HX(0x9a4e3a)
#define C_MORTAR  HX(0xc9b49a)
#define C_STONE   HX(0xd8cdb8)
#define C_WOOD    HX(0x9b6638)
#define C_WOODLT  HX(0xc98f57)
#define C_WOODDK  HX(0x5c391f)
#define C_HOOP    HX(0x3b3833)
#define C_HOOPLT  HX(0x8d877c)
#define C_LEAF    HX(0x2f6a43)
#define C_LEAFLT  HX(0x6fb36b)
#define C_LEAFDK  HX(0x173b2a)
#define C_TERRA   HX(0xb8623d)
#define C_HUD     HX(0x8adfcb)
#define C_WARN    HX(0xffb15c)
#define C_ROPE    HX(0xc8a46a)
#define C_BRASS   HX(0xc9a75e)
static const uint32_t GLASS_TINT[3] = {0xbfeee9, 0xd7f3ff, 0xc6f0d2};

// a gradient with stops (y0 .. y0 + h; the stops are fractions of h)
static void art_vstops(float x, float y0, float w, float h, const uint32_t *col, const float *at, int n)
{
    int ta = clampi((int)TXY(y0), cv_cy0, cv_cy1), tb = clampi((int)ceilf(TXY(y0 + h)), cv_cy0, cv_cy1);
    int xa = clampi((int)TXX(x), cv_cx0, cv_cx1), xb = clampi((int)ceilf(TXX(x + w)), cv_cx0, cv_cx1);
    for (int ty = ta; ty < tb; ty++) {
        float f = clampf((((float)ty + 0.5f) / CV.s + CV.oy - y0) / h, 0, 1);
        int k = 0;
        while (k < n - 2 && f > at[k + 1]) {
            k++;
        }
        float u = clampf((f - at[k]) / fmaxf(1e-4f, at[k + 1] - at[k]), 0, 1);
        uint16_t c = cmix(HX(col[k]), HX(col[k + 1]), u);
        if (CV.a) {
            for (int tx = xa; tx < xb; tx++) {
                cv_put(tx, ty, c, 256);
            }
            continue;
        }
        // (a layer with no alpha: a plain store, as cv_put does it at full alpha)
        if (CV.sky && c == KEY) {
            c = (uint16_t)(KEY ^ 0x0100u);
        }
        uint16_t *d = CV.c + ty * CV.w;
        for (int tx = xa; tx < xb; tx++) {
            d[tx] = c;
        }
    }
}

// ---------------------------------------------------------------- the skies (s_sky: 1 texel for each world px)
// Each sky: the stops of its gradient, a warm glow, clouds lit from below, a moon or a sun, three far tree lines.
typedef struct {
    uint32_t st[6];
    float at[6];
    uint32_t glow, cloud_lo, cloud_hi, tree_far, tree_near;
    float gx, gy, gr;
    int ga, nclouds, moon, stars;
} skyk_t;
static const skyk_t SKYK[SKY_N] = {
        // dusk (the look page)
        {{0x16223c, 0x33405f, 0x8a6474, 0xe2a27c, 0xf3c48e, 0xc9946c},
         {0, 0.3f, 0.55f, 0.72f, 0.86f, 1},
         0xffcf94,
         0x7d86b4,
         0xffc2a0,
         0x59597d,
         0x2a2c45,
         250,
         112,
         260,
         90,
         34,
         1,
         0},
        // night
        {{0x070b1c, 0x0f1733, 0x1d2a4f, 0x2f3d63, 0x3c4a6a, 0x2a3348},
         {0, 0.3f, 0.55f, 0.72f, 0.86f, 1},
         0x7f98d8,
         0x2c3760,
         0x6a78a8,
         0x26304d,
         0x141a2c,
         470,
         40,
         200,
         50,
         14,
         2,
         140},
        // dawn
        {{0x2a2350, 0x5b3f78, 0xb06a8c, 0xf2a08e, 0xffd3a6, 0xd9a78a},
         {0, 0.28f, 0.5f, 0.7f, 0.86f, 1},
         0xffe0b8,
         0x8a6ea6,
         0xffd0c0,
         0x7a5d84,
         0x3c2f4f,
         520,
         118,
         220,
         110,
         26,
         0,
         20},
        // day
        {{0x3f7fc2, 0x5d9ad6, 0x8fbde6, 0xc4def0, 0xe6efe9, 0xc9d6c4},
         {0, 0.3f, 0.55f, 0.75f, 0.9f, 1},
         0xfff6dc,
         0xd8e6f2,
         0xffffff,
         0x7d9a8c,
         0x4c6a58,
         140,
         26,
         200,
         80,
         22,
         3,
         0},
        // storm
        {{0x1a1f26, 0x2d353f, 0x4a5560, 0x6f7a80, 0x8b9490, 0x5f665e},
         {0, 0.3f, 0.55f, 0.72f, 0.88f, 1},
         0xc8d6e0,
         0x2a3038,
         0x6b7680,
         0x3f4a4e,
         0x232b2e,
         300,
         60,
         240,
         30,
         40,
         0,
         0},
        // winter
        {{0x3f5a84, 0x6f88ac, 0xa8bcd0, 0xd8e2ea, 0xf0f3f5, 0xc8d2da},
         {0, 0.3f, 0.55f, 0.75f, 0.9f, 1},
         0xfff2e0,
         0x9eb0c8,
         0xf6f8fa,
         0x7d8fa8,
         0x4d5b74,
         420,
         60,
         220,
         90,
         20,
         4,
         0},
        // gold
        {{0x3a2a3e, 0x7a4a46, 0xc8744a, 0xf2a95a, 0xffd88a, 0xd9a060},
         {0, 0.26f, 0.5f, 0.7f, 0.86f, 1},
         0xffe2a0,
         0xb06a5a,
         0xffd2a0,
         0x8a5a48,
         0x4a3030,
         120,
         116,
         260,
         130,
         26,
         5,
         0},
};
static void art_sky(int kind)
{
    const skyk_t *K = &SKYK[clampi(kind, 0, SKY_N - 1)];
    cv_set(s_sky, NULL, WW, WH, 1, 0, 0, NULL);
    s_rng = 11 + (uint32_t)kind * 7;
    art_vstops(0, 0, WW, WH, K->st, K->at, 6);
    cv_glow(K->gx, K->gy, K->gr, HX(K->glow), K->ga, false);
    // the stars
    for (int i = 0; i < K->stars; i++) {
        float x = rnd() * WW, y = rnd() * 90, b = rnd();
        cv_rect(x, y, 0.8f + b * 0.6f, 0.8f + b * 0.6f, C3(230, 236, 255), (int)(90 + 140 * b * (1 - y / 110)));
    }
    // the clouds: soft ellipses (three sizes of each one: a blur), lit from below
    for (int i = 0; i < K->nclouds; i++) {
        float cx = rnd() * WW, cy = 14 + rnd() * 74, rw = 24 + rnd() * 66;
        float warm = clampf((cy - 20) / 70, 0, 1);
        uint16_t c = cmix(HX(K->cloud_lo), HX(K->cloud_hi), warm);
        int a = (int)(256 * (0.10f + rnd() * 0.12f));
        for (int k = 0; k < 3; k++) {
            cv_ell_soft(cx, cy, rw * (1.15f - 0.15f * k), rw * (0.2f - 0.03f * k), c, a / 3 + 4);
        }
        cv_ell_soft(cx + 4, cy + rw * 0.08f, rw * 0.7f, rw * 0.05f, HX(K->cloud_hi), (int)(24 * warm));
    }
    // the moon or the sun
    if (K->moon == 1 || K->moon == 2) {
        float mx = K->moon == 1 ? 86 : 470, my = K->moon == 1 ? 30 : 34, r = K->moon == 1 ? 5.5f : 8;
        cv_glow(mx, my, r * 4.4f, C3(255, 244, 220), 90, false);
        cv_disc(mx, my, r, HX(0xfff1d6), 256);
        if (K->moon == 1) {
            cv_disc(mx + 2.5f, my - 1.4f, r - 0.5f, cmix(HX(K->st[1]), HX(K->st[0]), 0.3f), 220);
        } else {
            for (int k = 0; k < 5; k++) {
                cv_disc(mx - 3 + rnd() * 6, my - 3 + rnd() * 6, 1 + rnd() * 1.5f, HX(0xe4d8c0), 90);
            }
        }
    } else if (K->moon >= 3) {
        float sx = K->moon == 3 ? 140 : K->moon == 4 ? 420 : 120, sy = K->moon == 3 ? 26 : K->moon == 4 ? 60 : 112;
        float r = K->moon == 5 ? 14 : 9;
        cv_glow(sx, sy, r * 5, C3(255, 246, 220), K->moon == 4 ? 70 : 150, false);
        cv_disc(sx, sy, r, K->moon == 5 ? HX(0xfff0c0) : HX(0xfffaf0), K->moon == 4 ? 120 : 240);
    }
    // the storm: rain streaks; the winter: snow
    if (kind == SKY_STORM) {
        for (int i = 0; i < 260; i++) {
            float x = rnd() * WW, y = rnd() * 130;
            cv_line(x, y, x - 2, y + 7, 0.5f, C3(170, 185, 200), 60);
        }
    }
    if (kind == SKY_WINTER) {
        for (int i = 0; i < 220; i++) {
            cv_disc(rnd() * WW, rnd() * 130, 0.5f + rnd() * 0.6f, C3(255, 255, 255), 150);
        }
    }
    // the far tree lines
    for (int k = 0; k < 3; k++) {
        float base = 116 + k * 7;
        uint16_t col = cmix(HX(K->tree_far), HX(K->tree_near), k / 2.0f);
        float p[2 * 62];
        int n = 0;
        p[n++] = 0;
        p[n++] = WH;
        for (int i = 0; i <= 56; i++) {
            float px = (float)i * WW / 56;
            p[n++] = px;
            p[n++] = base - 10 - fabsf(fbm1(px * 0.021f + k * 9, 3)) * (22 - k * 4) -
                     (k == 0 ? 6 * sinf(px * 0.01f) : 0);
        }
        p[n++] = WW;
        p[n++] = WH;
        cv_poly(p, n / 2, col, 218);
    }
}

// ---------------------------------------------------------------- the stand: its top, its front, the scenery
static void art_ground(int kind, float x0, float x1)
{
    float top = 128;
    static const uint32_t G0[GR_N] = {0x4d6a3c, 0x8a8478, 0xd8bf8a, 0xeef2f4, 0x5d8a3e, 0x9a6a40};
    static const uint32_t G1[GR_N] = {0x2c4226, 0x55504a, 0xa88a5a, 0xc6d0d8, 0x34562a, 0x5e3d22};
    static const uint32_t GA[GR_N] = {0x6d8a4c, 0xb0a898, 0xf0dcac, 0xffffff, 0x7fae58, 0xc08a58};
    static const uint32_t GB[GR_N] = {0x1f3220, 0x3c3832, 0x8a6e44, 0xa8b4c0, 0x22401c, 0x3c2412};
    kind = clampi(kind, 0, GR_N - 1);
    float poly[8] = {x0 + 10, top, x1 - 10, top, x1 + 2, 143, x0 - 2, 143};
    cv_poly(poly, 4, HX(G0[kind]), 256);
    // (the gradient of the band)
    for (int r = 0; r < 15; r++) {
        float f = r / 14.0f;
        float q[8] = {x0 + 10 - f * 12,           top + r,     x1 - 10 + f * 12,           top + r,
                      x1 - 10 + (f + 0.07f) * 12, top + r + 1, x0 + 10 - (f + 0.07f) * 12, top + r + 1};
        cv_poly(q, 4, cmix(HX(G0[kind]), HX(G1[kind]), f), 256);
    }
    s_rng = 5 + (uint32_t)kind;
    if (kind == GR_COBBLE) {
        for (int r = 0; r < 4; r++) {
            for (float x = x0 + (r & 1) * 4; x < x1; x += 8) {
                float y = top + 1.5f + r * 3.6f;
                cv_ell(x + rnd() * 2, y, 3.4f, 1.4f, 0, cmix(HX(GA[kind]), HX(G1[kind]), rnd() * 0.6f), 230);
                cv_ell(x - 0.6f, y - 0.5f, 2.0f, 0.5f, 0, C3(220, 214, 200), 90);
            }
        }
    } else if (kind == GR_PLANKS) {
        for (int r = 0; r < 5; r++) {
            cv_rect(x0, top + r * 3, x1 - x0, 0.45f, HX(GB[kind]), 160);
        }
        for (int i = 0; i < 60; i++) {
            float x = x0 + rnd() * (x1 - x0), y = top + (float)(int)(rnd() * 5) * 3;
            cv_rect(x, y, 0.45f, 3, HX(GB[kind]), 140);
        }
    } else if (kind == GR_SNOW) {
        for (int i = 0; i < 40; i++) {
            cv_ell(x0 + rnd() * (x1 - x0), top + rnd() * 3, 4 + rnd() * 9, 1.4f, 0, C3(255, 255, 255), 200);
        }
    }
    cv_grain(x0 + 2, top + 0.5f, x1 - x0 - 4, 14, 1300, HX(GA[kind]), HX(GB[kind]), 128, 0.6f);
    // the soft key light on the stand
    for (int r = 0; r < 15; r++) {
        float f = r / 14.0f, a = (f < 0.5f ? f * 2 : (1 - f) * 2) * 40;
        cv_rect(x0 + 60, top + r, x1 - x0 - 120, 1, C3(255, 190, 100), (int)a);
    }
}
// the walnut front with the brass plate (the name is drawn on it in each frame: the font of the frame)
static float s_plate_x0, s_plate_x1;
static void art_front(float x0, float x1, float plate_w)
{
    uint32_t st[3] = {0x5b3a24, 0x3d2617, 0x22150d};
    float at[3] = {0, 0.15f, 1};
    art_vstops(x0, 143, x1 - x0, WH - 143 + 2, st, at, 3);
    cv_rect(x0, 143, x1 - x0, 0.8f, HX(0xffd6a0), 140);
    for (int i = 0; i < 40; i++) {
        float y0 = 145 + rnd() * 26;
        float px = x0, py = y0;
        for (float x = x0 + 20; x <= x1; x += 20) {
            float y = y0 + sinf(x * 0.02f + i) * 1.4f;
            cv_line(px, py, x, y, 0.4f + rnd() * 0.5f, HX(0x1a0f08), (int)(64 + rnd() * 50));
            px = x;
            py = y;
        }
    }
    float pw = plate_w, px0 = 320 - pw / 2;
    s_plate_x0 = px0;
    s_plate_x1 = px0 + pw;
    uint32_t pst[3] = {0xf2d58b, 0xb88a3e, 0x7c5a24};
    float pat[3] = {0, 0.5f, 1};
    art_vstops(px0, 150, pw, 13, pst, pat, 3);
    cv_rect(px0 + 1.5f, 151.5f, pw - 3, 0.6f, HX(0x3c280a), 150);
    cv_rect(px0 + 1.5f, 161.0f, pw - 3, 0.6f, HX(0x3c280a), 150);
    cv_disc(px0 + 4, 156.5f, 1.2f, HX(0x6b4b1c), 256);
    cv_disc(px0 + pw - 4, 156.5f, 1.2f, HX(0x6b4b1c), 256);
}
// a gas lamp post (its light is drawn in each frame)
static void deco_lamppost(float x, float y)
{
    cv_line(x, y, x, y - 39, 1.6f, HX(0x1f2424), 256);
    cv_line(x - 4, y - 36, x + 4, y - 36, 0.7f, HX(0x1f2424), 256);
    cv_rect(x - 2.5f, y - 2, 5, 2.5f, HX(0x1f2424), 256);
    float hood[8] = {x - 4, y - 47, x + 4, y - 47, x + 2.5f, y - 39, x - 2.5f, y - 39};
    cv_poly(hood, 4, HX(0x2a2a26), 256);
    float gl[8] = {x - 2.6f, y - 46, x + 2.6f, y - 46, x + 1.6f, y - 40, x - 1.6f, y - 40};
    cv_poly(gl, 4, HX(0xffdd99), 256);
    float roof[6] = {x - 5, y - 47, x, y - 51, x + 5, y - 47};
    cv_poly(roof, 3, HX(0x2a2a26), 256);
}
static void deco_barrow(float x, float y)
{
    float b[8] = {x - 13, y - 13, x + 13, y - 13, x + 9, y - 6, x - 9, y - 6};
    cv_poly(b, 4, HX(0x7a5232), 256);
    cv_rect(x - 13, y - 13.6f, 26, 1, HX(0xa6744a), 256);
    cv_ell(x, y - 13.4f, 12, 1.6f, 0, HX(0x3b2a1c), 256);
    for (int k = 0; k < 5; k++) {
        cv_line(x - 8 + k * 4.5f, y - 13.5f, x - 9 + k * 4.5f, y - 17.5f, 0.7f, HX(0x7fbf5a), 256);
        cv_line(x - 8 + k * 4.5f, y - 16, x - 6 + k * 4.5f, y - 17.4f, 0.7f, HX(0x7fbf5a), 256);
    }
    cv_line(x + 9, y - 9, x + 23, y - 4, 1, HX(0x4a321e), 256);
    cv_line(x - 9, y - 7, x - 5, y, 1, HX(0x4a321e), 256);
    cv_ring(x - 13, y - 5, 4.4f, 1.2f, HX(0x2a2420), 256);
    for (int k = 0; k < 4; k++) {
        float a = k * 0.8f;
        cv_line(x - 13 + cosf(a) * 4, y - 5 + sinf(a) * 4, x - 13 - cosf(a) * 4, y - 5 - sinf(a) * 4, 0.5f,
                HX(0x2a2420), 256);
    }
}
static void deco_pots(float x, float y)
{
    for (int k = 0; k < 3; k++) {
        float p[8] = {x - 5 + k * 0.5f, y - 1 - k * 4, x + 5 + k * 0.5f, y - 1 - k * 4,
                      x + 4 + k * 0.5f, y - 5 - k * 4, x - 4 + k * 0.5f, y - 5 - k * 4};
        cv_poly(p, 4, k % 2 ? HX(0xc46d45) : HX(0xb35f3a), 256);
        cv_rect(x - 4.5f + k * 0.5f, y - 5 - k * 4, 9, 0.6f, HX(0xffd2aa), 128);
    }
}
static void deco_can(float x, float y)
{
    cv_rect(x - 4.5f, y - 7, 9, 7, HX(0x3f6f5a), 256);
    cv_line(x + 4.5f, y - 5, x + 10.5f, y - 10, 1, HX(0x3f6f5a), 256);
    cv_arc(x, y - 7, 3, 1, PI_, TAU_, HX(0x3f6f5a), 256);
    cv_rect(x - 3.5f, y - 6.4f, 7, 0.5f, HX(0xc8f0dc), 128);
}
// a small tree in a tub, or a bush (h = its height)
static void deco_tree(float x, float y, float h)
{
    if (h <= 0) {
        h = 30;
    }
    cv_line(x, y - 4, x - 1, y - h * 0.55f, 1.6f, HX(0x4b3a25), 256);
    for (int i = 0; i < 26; i++) {
        float a = rnd() * TAU_, r = rnd() * h * 0.32f;
        float cx = x + cosf(a) * r, cy = y - h * 0.68f + sinf(a) * r * 0.75f;
        cv_ell(cx, cy, 3 + rnd() * 4, 2 + rnd() * 3, rnd(), cmix(C_LEAFDK, C_LEAF, rnd()), 240);
    }
    for (int i = 0; i < 12; i++) {
        float a = -PI_ * (0.2f + 0.6f * rnd()), r = rnd() * h * 0.3f;
        cv_ell(x + cosf(a) * r - 2, y - h * 0.72f + sinf(a) * r * 0.7f, 2 + rnd() * 2, 1.4f, rnd(), C_LEAFLT, 180);
    }
    float tub[8] = {x - 6, y - 7, x + 6, y - 7, x + 5, y, x - 5, y};
    cv_poly(tub, 4, C_TERRA, 256);
    cv_rect(x - 6, y - 7, 12, 0.8f, HX(0xffc8a0), 128);
}
static void deco_hedge(float x, float y, float w)
{
    if (w <= 0) {
        w = 40;
    }
    for (int i = 0; i < (int)(w * 1.3f); i++) {
        float cx = x - w / 2 + rnd() * w, cy = y - 3 - rnd() * 9;
        cv_ell(cx, cy, 2.5f + rnd() * 3, 2 + rnd() * 2, rnd(), cmix(C_LEAFDK, C_LEAF, rnd() * 0.8f), 250);
    }
    for (int i = 0; i < (int)(w * 0.4f); i++) {
        cv_disc(x - w / 2 + rnd() * w, y - 8 - rnd() * 5, 0.8f + rnd(), C_LEAFLT, 150);
    }
}
static void deco_fence(float x, float y, float w)
{
    if (w <= 0) {
        w = 60;
    }
    for (float px = x - w / 2; px <= x + w / 2; px += 6) {
        float p[10] = {px - 1.4f, y, px + 1.4f, y, px + 1.4f, y - 13, px, y - 15, px - 1.4f, y - 13};
        cv_poly(p, 5, HX(0xe8e0cc), 256);
        cv_rect(px + 0.6f, y - 13, 0.8f, 13, HX(0xa89e88), 160);
    }
    cv_rect(x - w / 2, y - 11, w, 1.4f, HX(0xd8d0bc), 256);
    cv_rect(x - w / 2, y - 5, w, 1.4f, HX(0xd8d0bc), 256);
}
static void deco_bench(float x, float y, float w)
{
    if (w <= 0) {
        w = 34;
    }
    cv_rect(x - w / 2, y - 12, w, 2, HX(0x5a3f26), 256);
    cv_rect(x - w / 2 + 2, y - 10, 1.5f, 10, HX(0x5a3f26), 256);
    cv_rect(x + w / 2 - 3.5f, y - 10, 1.5f, 10, HX(0x5a3f26), 256);
    for (float k = 0; k < w - 8; k += 8) {
        float p[8] = {x - w / 2 + 3 + k, y - 19, x - w / 2 + 9 + k, y - 19,
                      x - w / 2 + 8 + k, y - 12, x - w / 2 + 4 + k, y - 12};
        cv_poly(p, 4, C_TERRA, 256);
        cv_ell(x - w / 2 + 6 + k, y - 21, 4, 2.5f, 0, C_LEAF, 256);
    }
}
// a riveted iron jib crane in dark green, with a steam winch at its foot; tip = the x of the end of the jib, top = its y
static void lattice(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, int n)
{
    uint16_t ink = HX(0x2c3c37), lace = HX(0x3f574f);
    cv_line(ax, ay, bx, by, 1.3f, ink, 256);
    cv_line(cx, cy, dx, dy, 1.3f, ink, 256);
    for (int i = 0; i < n; i++) {
        float f0 = (float)i / n, f1 = (float)(i + 1) / n;
        cv_line(lerpf(ax, bx, f0), lerpf(ay, by, f0), lerpf(cx, dx, f1), lerpf(cy, dy, f1), 0.6f, lace, 256);
        cv_line(lerpf(cx, dx, f1), lerpf(cy, dy, f1), lerpf(ax, bx, f1), lerpf(ay, by, f1), 0.6f, lace, 256);
    }
    for (int i = 0; i <= n; i += 2) {
        cv_rect(lerpf(ax, bx, (float)i / n) - 0.4f, lerpf(ay, by, (float)i / n) - 0.4f, 0.8f, 0.8f, C_BRASS, 256);
    }
}
static void deco_crane(float mx, float y, float tip)
{
    float mx0 = mx, mx1 = mx + 14, top = 16;
    cv_rect(mx - 36, y - 8, 64, 7, HX(0x8a8478), 256);
    cv_rect(mx - 36, y - 8, 64, 0.8f, HX(0xb2ab9c), 256);
    lattice(mx0, y - 8, mx0, top, mx1, y - 8, mx1, top, 14);
    cv_line(mx0 - 0.6f, y - 8, mx0 - 0.6f, top, 0.4f, HX(0x88a99a), 256);
    lattice(tip - 2, 17, mx0, 15, tip - 2, 21.5f, mx0, 21.5f, 20);
    lattice(mx1, 15, mx1 + 26, 16, mx1, 21.5f, mx1 + 26, 21, 3);
    for (int k = 0; k < 2; k++) {
        cv_rect(mx1 + 12, 22 + k * 6, 14, 6, k ? HX(0x25302c) : HX(0x33413c), 256);
        cv_rect(mx1 + 12, 22 + k * 6, 14, 0.6f, C_BRASS, 256);
    }
    cv_line(mx + 7, 4, tip + 12, 16.5f, 0.4f, HX(0xc9c3b5), 256);
    cv_line(mx + 7, 4, mx1 + 25, 16, 0.4f, HX(0xc9c3b5), 256);
    cv_line(mx + 1, 15, mx + 7, 4, 1.1f, HX(0x2c3c37), 256);
    cv_line(mx + 7, 4, mx + 13, 15, 1.1f, HX(0x2c3c37), 256);
    cv_disc(mx + 7, 4, 1.1f, C_BRASS, 256);
    cv_rect(tip - 5, 19.5f, 10, 3, HX(0x26302c), 256);
    // the steam winch: the boiler, the chimney, the drum
    float bx0 = mx - 32, bx1 = mx - 20;
    for (int i = 0; i < 12; i++) {
        float f = i / 11.0f;
        uint16_t c = f < 0.35f ? cmix(HX(0xe08a55), HX(0xb8562e), f / 0.35f)
                               : cmix(HX(0xb8562e), HX(0x5e2612), (f - 0.35f) / 0.65f);
        cv_rect(bx0 + i, y - 29, 1.05f, 21, c, 256);
    }
    cv_ell((bx0 + bx1) / 2, y - 29, 6, 3.5f, 0, HX(0xb8562e), 256);
    for (int k = 0; k < 3; k++) {
        cv_rect(bx0, y - 25 + k * 7, 12, 1, HX(0xd9b468), 256);
        cv_rect(bx0, y - 24 + k * 7, 12, 0.4f, HX(0x50320a), 150);
    }
    cv_rect(bx0 + 4.5f, y - 43, 3, 11, HX(0x2b2b2b), 256);
    cv_rect(bx0 + 4, y - 43.6f, 4, 1.2f, C_BRASS, 256);
    cv_rect(bx0 + 3, y - 14.5f, 6, 3, HX(0xffb35a), 256);
    cv_glow(bx0 + 6, y - 12, 10, HX(0xff9a40), 90, false);
    cv_rect(bx1, y - 21, 12, 9, HX(0x3a3530), 256);
    cv_disc(bx1 + 6, y - 16.5f, 4.5f, HX(0x5b544a), 256);
    for (int k = -3; k <= 3; k += 2) {
        cv_line(bx1 + 2, y - 16.5f + k, bx1 + 10, y - 16.5f + k, 0.5f, HX(0x8d8778), 256);
    }
}
// a gallows of timber for a load on a rope: a post and an arm (scenery; the hook is a static point of the rope)
static void deco_gallows(float x, float y, float h, float arm)
{
    if (h <= 0) {
        h = 80;
    }
    cv_rect(x - 2, y - h, 4, h, C_WOOD, 256);
    cv_rect(x - 2, y - h, 1.2f, h, C_WOODLT, 200);
    cv_rect(x - 6, y - 3, 12, 3, C_WOODDK, 256);
    if (fabsf(arm) < 1) {
        cv_rect(x - 3, y - h - 2, 6, 3, C_WOODDK, 256);
        return;
    }
    float x1 = x + arm;
    cv_rect(fminf(x, x1) - 2, y - h - 2, fabsf(arm) + 4, 4, C_WOOD, 256);
    cv_rect(fminf(x, x1) - 2, y - h - 2, fabsf(arm) + 4, 1, C_WOODLT, 200);
    cv_line(x, y - h + 14, x + arm * 0.45f, y - h + 1, 2, C_WOODDK, 256);
    cv_disc(x1, y - h + 3, 1.6f, HX(0x2a2622), 256);
}
// the inside of a glass house (behind its cells): dark green, palms, ferns and the warm light of lamps
static void deco_inside(float x0, float y0, float x1, float y1)
{
    uint32_t st[3] = {0x1b3a3a, 0x2c3b2c, 0x4b3a22};
    float at[3] = {0, 0.6f, 1};
    art_vstops(x0, y0, x1 - x0, y1 - y0, st, at, 3);
    cv_glow((x0 + x1) / 2, y1 - 20, (x1 - x0) * 0.4f, HX(0xffbe62), 90, false);
    for (int i = 0; i < (int)((x1 - x0) / 7); i++) {
        float px = x0 + rnd() * (x1 - x0), py = y0 + (y1 - y0) * (0.35f + rnd() * 0.6f);
        cv_ell(px, py, 6 + rnd() * 9, 3 + rnd() * 4, (rnd() - 0.5f) * 1.6f, cmix(C_LEAFDK, C_LEAF, rnd() * 0.6f), 230);
    }
    for (int i = 0; i < (int)((x1 - x0) / 18); i++) {
        float px = x0 + 8 + rnd() * (x1 - x0 - 16), py = y1 - 2;
        for (int k = 0; k < 6; k++) {
            float a = -PI_ / 2 + (k - 2.5f) * 0.45f;
            cv_quad(px, py, px + cosf(a) * 7, py + sinf(a) * 9, px + cosf(a) * 12, py + sinf(a) * 6, 1.1f,
                    k % 2 ? C_LEAFLT : C_LEAF, 230);
        }
    }
}
// a palm in the house: a trunk and fronds
static void deco_palm(float bx, float by, float top, int n, float s)
{
    cv_quad(bx, by, bx - 6, (by + top) / 2, bx - 2, top, 2.2f, HX(0x4b3a25), 256);
    for (float yy = top + 3; yy < by; yy += 3) {
        cv_line(bx - 4, yy, bx + 1, yy + 1, 0.6f, HX(0xc8aa6e), 128);
    }
    for (int k = 0; k < n; k++) {
        float ang = PI_ + ((float)k / (n - 1)) * PI_ + (rnd() - 0.5f) * 0.2f, len = s * (0.7f + rnd() * 0.4f);
        float x0 = bx - 2, y0 = top;
        float ex = x0 + cosf(ang) * len, ey = y0 + sinf(ang) * len + len * 0.25f;
        float mx = x0 + cosf(ang) * len * 0.5f, my = y0 + sinf(ang) * len * 0.5f - len * 0.12f;
        uint16_t col = k % 2 ? C_LEAF : C_LEAFDK;
        cv_quad(x0, y0, mx, my, ex, ey, 0.8f, col, 256);
        for (int q = 1; q < 10; q++) {
            float t = q / 10.0f, u = 1 - t;
            float px = u * u * x0 + 2 * u * t * mx + t * t * ex, py = u * u * y0 + 2 * u * t * my + t * t * ey;
            float l = (1 - t * 0.6f) * len * 0.22f;
            cv_line(px, py, px + cosf(ang - 1.1f + 0.5f) * l, py + sinf(ang - 1.1f + 0.5f) * l + l * 0.4f, 0.7f,
                    C_LEAFLT, 230);
            cv_line(px, py, px + cosf(ang + 1.1f + 0.5f) * l, py + sinf(ang + 1.1f + 0.5f) * l + l * 0.4f, 0.7f, col,
                    230);
        }
    }
}
// the fan of an arch (the transept of the orchid house): iron ribs over glass, a lantern on top
static void deco_arch(float cx, float cy, float r)
{
    for (int i = 0; i < 9; i++) {
        float a0 = PI_ + (i / 9.0f) * PI_, a1 = PI_ + ((i + 1) / 9.0f) * PI_;
        float p[8] = {cx,
                      cy,
                      cx + cosf(a0) * r,
                      cy + sinf(a0) * r,
                      cx + cosf((a0 + a1) / 2) * r,
                      cy + sinf((a0 + a1) / 2) * r,
                      cx + cosf(a1) * r,
                      cy + sinf(a1) * r};
        cv_poly(p, 4, HX(GLASS_TINT[i % 3]), 46 + (i * 13) % 5 * 6);
    }
    for (float rr = 17; rr <= r; rr += 13) {
        cv_arc(cx, cy, fminf(rr, r), 1.6f, PI_, TAU_, C_IRON, 256);
    }
    cv_arc(cx, cy, r, 1.6f, PI_, TAU_, C_IRON, 256);
    for (int i = 1; i < 9; i++) {
        float a = PI_ + (i / 9.0f) * PI_;
        cv_line(cx + cosf(a) * 4, cy + sinf(a) * 4, cx + cosf(a) * r, cy + sinf(a) * r, 0.9f, C_IRON, 256);
    }
    cv_disc(cx, cy, 4, C_IRON, 256);
    cv_line(cx - r - 1, cy, cx + r + 1, cy, 1.8f, C_IRON, 256);
    float lx = cx, ly = cy - r;
    cv_rect(lx - 7, ly - 9, 14, 9, C_IRON, 256);
    for (int k = 0; k < 3; k++) {
        cv_rect(lx - 6 + k * 4.4f, ly - 8, 3.2f, 7, C3(160, 220, 215), 140);
    }
    cv_arc(lx, ly - 9, 4, 8, PI_, TAU_, C_IRON, 256);
    cv_rect(lx + 2, ly - 9, 5, 9, C_IRONSH, 150);
    cv_line(lx, ly - 13, lx, ly - 22, 0.8f, C_IRON, 256);
    cv_disc(lx, ly - 15, 1.5f, C_IRON, 256);
    cv_line(lx - 6, ly - 20, lx + 6, ly - 20, 0.7f, C_IRON, 256);
    cv_line(lx + 6, ly - 20, lx + 3.5f, ly - 21.8f, 0.7f, C_IRON, 256);
    cv_line(lx + 6, ly - 20, lx + 3.5f, ly - 18.2f, 0.7f, C_IRON, 256);
    float tail[8] = {lx - 6, ly - 20, lx - 9, ly - 22.5f, lx - 8, ly - 20, lx - 9, ly - 17.5f};
    cv_poly(tail, 4, C_IRON, 256);
    cv_arc(cx, cy, r + 0.6f, 0.8f, PI_, PI_ * 1.45f, C3(255, 190, 120), 90);
}
// a brick knee wall with a stone coping (x0 .. x1, from y0 to the floor)
static void deco_wall(float x0, float x1, float y0)
{
    float y1 = FLOOR - 2;
    cv_rect(x0, y0, x1 - x0, y1 - y0, C_BRICK, 256);
    for (int row = 0; row < (int)((y1 - y0) / 2.6f); row++) {
        float y = y0 + 2 + row * 2.6f;
        cv_rect(x0, y, x1 - x0, 0.45f, C_MORTAR, 140);
        for (float px = x0 + (row % 2) * 2.5f; px < x1; px += 5) {
            cv_rect(px, y - 2.6f, 0.45f, 2.6f, C_MORTAR, 140);
        }
        for (float px = x0 + (row % 2) * 2.5f + 0.6f; px < x1 - 4; px += 5) {
            cv_rect(px, y - 2.1f, 3.6f, 1.6f, C_BRICKLT, (int)(64 + ((int)(px * 7) % 3) * 26));
        }
    }
    cv_rect(x0 - 1, y0 - 1.6f, x1 - x0 + 2, 2.2f, C_STONE, 256);
    cv_rect(x0 - 1, y0 - 1.6f, x1 - x0 + 2, 0.6f, HX(0xfff0d2), 200);
}
// a pipe from (x0, y0) by an elbow to (x1, y1)
static void deco_pipe(float x0, float y0, float x1, float y1)
{
    cv_line(x0, y0, x1, y0, 2, HX(0x6d6a63), 256);
    cv_line(x1, y0, x1, y1, 2, HX(0x6d6a63), 256);
    cv_line(x0, y0 - 0.7f, x1, y0 - 0.7f, 0.5f, HX(0xfff0dc), 128);
    cv_rect(x1 - 2.5f, y1 - 1, 5, 3, HX(0x4a4741), 256);
}
// the glow points of the scenery that flicker (lamps): drawn in each frame
#define MAXGLOW 8
static float s_glow[MAXGLOW][3];
static int s_nglow;
static void glow_add(float x, float y, float r)
{
    if (s_nglow < MAXGLOW) {
        s_glow[s_nglow][0] = x;
        s_glow[s_nglow][1] = y;
        s_glow[s_nglow][2] = r;
        s_nglow++;
    }
}
static void deco_lantern(float x, float y, float top)
{
    cv_line(x, top, x, y - 4, 0.5f, HX(0x1e1914), 230);
    float b[8] = {x - 2.6f, y - 4, x + 2.6f, y - 4, x + 2, y + 3, x - 2, y + 3};
    cv_poly(b, 4, HX(0x3a2c1a), 256);
    cv_rect(x - 1.6f, y - 2.8f, 3.2f, 4.8f, HX(0xffe6a8), 256);
    cv_rect(x - 0.25f, y - 2.8f, 0.5f, 4.8f, HX(0x3a2c1a), 256);
    cv_rect(x - 3, y - 5, 6, 1.2f, HX(0x3a2c1a), 256);
    cv_glow(x, y, 26, HX(0xffbe62), 60, true);
    glow_add(x, y, 16);
}
static void deco_shafts(void)
{
    // three soft slanted shafts of the low sun (light that adds)
    for (int k = 0; k < 3; k++) {
        float ox = -20 + k * 70, w = 26 + k * 8;
        for (float yy = 0; yy < 140; yy += 1) {
            float x = ox + 40 - yy * 0.62f + 100;
            for (float dx = 0; dx < w; dx += 1) {
                float f = dx / w, a = (f < 0.5f ? f : 1 - f) * 2 * 22;
                int tx = (int)TXX(x + dx), ty = (int)TXY(yy);
                for (int sy = 0; sy < 2; sy++) {
                    for (int sx = 0; sx < 2; sx++) {
                        cv_add(tx + sx, ty + sy, C3(255, 220, 170), (int)a);
                    }
                }
            }
        }
    }
}
// a bed of flowers (w wide)
static void deco_flowers(float x, float y, float w)
{
    if (w <= 0) {
        w = 40;
    }
    static const uint32_t FC[5] = {0xff9ccd, 0xffd34d, 0xf6f2ea, 0xd84a4a, 0x9a7ad8};
    for (int i = 0; i < (int)(w * 0.9f); i++) {
        float px = x - w / 2 + rnd() * w, h = 4 + rnd() * 7;
        cv_line(px, y - 1, px + (rnd() - 0.5f) * 2, y - h, 0.6f, cmix(C_LEAF, C_LEAFLT, rnd()), 230);
    }
    for (int i = 0; i < (int)(w * 0.5f); i++) {
        float px = x - w / 2 + rnd() * w, py = y - 5 - rnd() * 6;
        cv_disc(px, py, 0.9f + rnd() * 0.8f, HX(FC[(int)(rnd() * 5)]), 240);
    }
}
// a garden shed at the back of the stand (its door, its window, its roof)
static void deco_shed(float x, float y, float w)
{
    if (w <= 0) {
        w = 44;
    }
    float h = w * 0.8f;
    for (float px = x - w / 2; px < x + w / 2; px += 3) {
        cv_rect(px, y - h, 3.1f, h, ((int)(px / 3) % 2) ? HX(0x5e4630) : HX(0x6a5038), 256);
    }
    float roof[8] = {x - w / 2 - 4,    y - h,         x + w / 2 + 4,   y - h, x + w / 2 - 2,
                     y - h - w * 0.3f, x - w / 2 + 2, y - h - w * 0.3f};
    cv_poly(roof, 4, HX(0x3a3430), 256);
    cv_rect(x - w / 2 - 4, y - h, w + 8, 1, HX(0x8a7a6a), 200);
    cv_rect(x - w * 0.12f, y - h * 0.72f, w * 0.24f, h * 0.72f, HX(0x3e2c1c), 256);
    cv_disc(x + w * 0.07f, y - h * 0.36f, 0.7f, C_BRASS, 256);
    cv_rect(x + w * 0.2f, y - h * 0.7f, w * 0.2f, h * 0.24f, HX(0xffd88a), 200);
    cv_rect(x + w * 0.29f, y - h * 0.7f, 0.6f, h * 0.24f, HX(0x3e2c1c), 256);
    cv_glow(x + w * 0.3f, y - h * 0.58f, w * 0.3f, HX(0xffc070), 60, true);
}
// a rain barrel of oak with iron hoops
static void deco_barrel(float x, float y)
{
    for (int i = 0; i < 6; i++) {
        float f = i / 5.0f;
        cv_rect(x - 7 + i * 2.4f, y - 18, 2.5f, 18, cmul(C_WOOD, 0.7f + 0.5f * sinf(f * PI_)), 256);
    }
    for (int k = 0; k < 2; k++) {
        cv_rect(x - 7.3f, y - 15 + k * 10, 14.6f, 1.4f, C_HOOP, 256);
    }
    cv_ell(x, y - 18, 7, 1.6f, 0, HX(0x3c7f9e), 256);
}
// a few stones
static void deco_rocks(float x, float y, float w)
{
    if (w <= 0) {
        w = 24;
    }
    for (int i = 0; i < (int)(w / 5) + 2; i++) {
        float px = x - w / 2 + rnd() * w, r = 2 + rnd() * 3;
        cv_ell(px, y - r * 0.6f, r, r * 0.65f, 0, cmix(HX(0x6a665e), HX(0xb0aca0), rnd()), 256);
        cv_ell(px - r * 0.3f, y - r * 0.85f, r * 0.45f, r * 0.2f, 0, C3(240, 236, 226), 100);
    }
}
// one scenery item of the level
static void art_deco(const ddef_t *D)
{
    const char *k = D->kind;
    if (!strcmp(k, "lamppost")) {
        deco_lamppost(D->x, D->y);
        glow_add(D->x, D->y - 43, 14);
    } else if (!strcmp(k, "barrow")) {
        deco_barrow(D->x, D->y);
    } else if (!strcmp(k, "pots")) {
        deco_pots(D->x, D->y);
    } else if (!strcmp(k, "can")) {
        deco_can(D->x, D->y);
    } else if (!strcmp(k, "tree")) {
        deco_tree(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "hedge")) {
        deco_hedge(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "fence")) {
        deco_fence(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "bench")) {
        deco_bench(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "crane")) {
        deco_crane(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "gallows")) {
        deco_gallows(D->x, D->y, D->p[0], D->p[1]);
    } else if (!strcmp(k, "inside")) {
        deco_inside(D->x, D->y, D->p[0], D->p[1]);
    } else if (!strcmp(k, "palm")) {
        deco_palm(D->x, D->y, D->p[0], (int)D->p[1], D->p[2]);
    } else if (!strcmp(k, "arch")) {
        deco_arch(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "wall")) {
        deco_wall(D->x, D->p[0], D->y);
    } else if (!strcmp(k, "pipe")) {
        deco_pipe(D->x, D->y, D->p[0], D->p[1]);
    } else if (!strcmp(k, "lantern")) {
        deco_lantern(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "shafts")) {
        deco_shafts();
    } else if (!strcmp(k, "flowers")) {
        deco_flowers(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "shed")) {
        deco_shed(D->x, D->y, D->p[0]);
    } else if (!strcmp(k, "barrel")) {
        deco_barrel(D->x, D->y);
    } else if (!strcmp(k, "rocks")) {
        deco_rocks(D->x, D->y, D->p[0]);
    }
}

// ---------------------------------------------------------------- the pictures of the parts (local px, 2 texels each)
// a glass pane: the tint, the sheen and the edge light; cracks: the lines of the crack (in its final size)
static void glass_fill(float x0, float y0, float x1, float y1, int tint, int look, bool roof)
{
    uint16_t c = HX(GLASS_TINT[tint % 3]);
    int a = 38 + (tint * 13) % 5 * 6 + (roof ? 22 : 0);
    if (look == 2) {
        c = C3(236, 246, 246);
        a += 50;
    }
    if (look == 3) {
        static const uint32_t ST[4] = {0xf0b050, 0xd84a4a, 0x4a7ad8, 0x58b070};
        c = HX(ST[tint % 4]);
        a = 120;
    }
    // (the glass shows more of the light of the sky at its top: a little more of the tint there)
    float hh3 = (y1 - y0) / 3;
    cv_rect(x0, y0, x1 - x0, hh3, c, a + 26);
    cv_rect(x0, y0 + hh3, x1 - x0, hh3, c, a + 10);
    cv_rect(x0, y0 + 2 * hh3, x1 - x0, hh3, c, a);
    // the sheen: a soft slanted band of the studio light
    float w = x1 - x0, h = y1 - y0, sx = x0 + w * (0.15f + 0.5f * hash1(tint * 7 + (int)(x0 * 3)));
    float band[8] = {sx, y0, sx + w * 0.22f, y0, sx + w * 0.22f - h * 0.3f, y1, sx - h * 0.3f, y1};
    cv_poly(band, 4, C3(255, 246, 228), 40 + (roof ? 26 : 0));
    cv_rect(x0, y0, w, 0.5f, C3(255, 250, 236), 90);
}
// the lines of a crack from the point (cx, cy) in the box (cut at the box); t = how far it grew (px; 99 = all)
typedef struct {
    float x0, y0, x1, y1;
} seg_t;
static inline float cr_next(uint32_t *r)
{
    *r ^= *r << 13;
    *r ^= *r >> 17;
    *r ^= *r << 5;
    return (float)(*r >> 8) / 16777216.0f;
}
static int crack_segs(uint16_t seed, float cx, float cy, float bx0, float by0, float bx1, float by1, seg_t *out,
                      int max, float grow)
{
    uint32_t r = (uint32_t)seed * 2654435761u + 7;
    int n = 0;
    int arms = 6 + (int)(cr_next(&r) * 3);
    float rings[2];
    rings[0] = 3 + cr_next(&r) * 1.5f;
    rings[1] = 7 + cr_next(&r) * 3;
    for (int i = 0; i < arms; i++) {
        float a = (float)i / arms * TAU_ + cr_next(&r) * 0.7f, x = cx, y = cy, len = 0;
        for (int k = 0; k < 40 && n < max; k++) {
            float s = 1.4f + cr_next(&r) * 2.2f;
            a += (cr_next(&r) - 0.5f) * 0.75f;
            float nx = x + cosf(a) * s, ny = y + sinf(a) * s;
            bool out_ = nx < bx0 || nx > bx1 || ny < by0 || ny > by1;
            nx = clampf(nx, bx0, bx1);
            ny = clampf(ny, by0, by1);
            if (len < grow) {
                out[n++] = (seg_t){x, y, nx, ny};
            }
            // (a short branch now and then)
            if (k > 2 && cr_next(&r) < 0.1f && n < max && len + 2 < grow) {
                float sg = cr_next(&r) < 0.5f ? -1.0f : 1.0f;
                float b = a + sg * (0.5f + cr_next(&r) * 0.5f);
                float l = 2 + cr_next(&r) * 5;
                out[n++] = (seg_t){nx, ny, clampf(nx + cosf(b) * l, bx0, bx1), clampf(ny + sinf(b) * l, by0, by1)};
            }
            len += s;
            x = nx;
            y = ny;
            if (out_) {
                break;
            }
        }
    }
    // the rings round the hit point: short chords (when the crack grew far enough)
    for (int q = 0; q < 2 && grow > rings[q] + 2; q++) {
        float prevx = 0, prevy = 0;
        for (int i = 0; i <= arms && n < max; i++) {
            float a = (float)(i % arms) / arms * TAU_ + 0.35f;
            float px = clampf(cx + cosf(a) * rings[q] + (cr_next(&r) - 0.5f) * 1.2f, bx0, bx1);
            float py = clampf(cy + sinf(a) * rings[q] + (cr_next(&r) - 0.5f) * 1.2f, by0, by1);
            if (i > 0) {
                out[n++] = (seg_t){prevx, prevy, px, py};
            }
            prevx = px;
            prevy = py;
        }
    }
    return n;
}
static void crack_paint(uint16_t seed, float cx, float cy, float bx0, float by0, float bx1, float by1)
{
    seg_t sg[200];
    int n = crack_segs(seed, cx, cy, bx0, by0, bx1, by1, sg, 200, 99);
    for (int i = 0; i < n; i++) {
        cv_line(sg[i].x0 + 0.4f, sg[i].y0 + 0.4f, sg[i].x1 + 0.4f, sg[i].y1 + 0.4f, 0.9f, C3(10, 30, 30), 100);
    }
    for (int i = 0; i < n; i++) {
        cv_line(sg[i].x0, sg[i].y0, sg[i].x1, sg[i].y1, 0.6f, C3(250, 255, 255), 230);
    }
    cv_glow(cx, cy, 6, C3(240, 255, 255), 80, false);
}
// the jagged bits of a broken pane that stay in its frame
static void pane_bits(uint16_t seed, float x0, float y0, float x1, float y1)
{
    uint32_t r = (uint32_t)seed * 747796405u + 3;
    float px[4] = {x0, x1, x1, x0}, py[4] = {y0, y0, y1, y1};
    float mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    for (int e = 0; e < 4; e++) {
        float ax = px[e], ay = py[e], bx = px[(e + 1) % 4], by = py[(e + 1) % 4];
        for (int k = 0; k < 2; k++) {
            if (cr_next(&r) < 0.35f) {
                continue;
            }
            float f0 = cr_next(&r) * 0.7f;
            float f1 = f0 + 0.15f + cr_next(&r) * 0.25f, fm = (f0 + f1) / 2;
            float dep = 1.2f + cr_next(&r) * 2.8f;
            float qx = lerpf(ax, bx, fm), qy = lerpf(ay, by, fm), dx = mx - qx, dy = my - qy,
                  d = sqrtf(dx * dx + dy * dy) + 1e-3f;
            float t[6] = {lerpf(ax, bx, f0), lerpf(ay, by, f0), qx + dx / d * dep,
                          qy + dy / d * dep, lerpf(ax, bx, f1), lerpf(ay, by, f1)};
            cv_poly(t, 3, C3(200, 245, 245), 90);
            cv_line(t[0], t[1], t[2], t[3], 0.4f, C3(255, 255, 255), 170);
            cv_line(t[2], t[3], t[4], t[5], 0.4f, C3(255, 255, 255), 170);
        }
    }
}
// an iron bar with its shade
static void bar(float x0, float y0, float x1, float y1, float w)
{
    cv_line(x0 + 0.5f, y0 + 0.4f, x1 + 0.5f, y1 + 0.4f, w + 0.6f, C_IRONSH, 230);
    cv_line(x0, y0, x1, y1, w, C_IRON, 256);
}
// a glass cell: its iron frame (the rim and the bars between the panes) and each pane in its state
static void paint_cell(int i)
{
    const pdef_t *P = &LV->p[i];
    const prt_t *R = &G.p[i];
    float hw = P->w / 2, hh = P->h / 2;
    bool roof = P->kind == PK_ROOF;
    if (P->look == 1) {
        // (plants inside the cell, behind the glass)
        uint32_t st[3] = {0x1b3a3a, 0x2c3b2c, 0x4b3a22};
        float at[3] = {0, 0.6f, 1};
        art_vstops(-hw + RIM, -hh + RIM, P->w - 2 * RIM, P->h - 2 * RIM, st, at, 3);
        s_rng = 31 + (uint32_t)i * 17;
        for (int k = 0; k < (int)(P->w * P->h / 60) + 2; k++) {
            float px = -hw + RIM + rnd() * (P->w - 2 * RIM),
                  py = -hh + RIM + (P->h - 2 * RIM) * (0.25f + rnd() * 0.75f);
            cv_ell(px, py, 3 + rnd() * 5, 2 + rnd() * 3, (rnd() - 0.5f) * 1.6f, cmix(C_LEAFDK, C_LEAF, rnd() * 0.7f),
                   235);
        }
        for (int k = 0; k < (int)(P->w / 10) + 1; k++) {
            float px = -hw + 4 + rnd() * (P->w - 8), py = hh - RIM;
            for (int q = 0; q < 5; q++) {
                float a = -PI_ / 2 + (q - 2) * 0.5f;
                cv_quad(px, py, px + cosf(a) * 4, py + sinf(a) * 6, px + cosf(a) * 7, py + sinf(a) * 4, 0.9f,
                        q % 2 ? C_LEAFLT : C_LEAF, 220);
            }
        }
        cv_glow(0, hh * 0.3f, fmaxf(hw, hh) * 1.2f, HX(0xffbe62), 50, true);
    }
    for (int k = 0; k < R->npane; k++) {
        float x0, y0, x1, y1;
        pane_box(P, k, &x0, &y0, &x1, &y1);
        if (R->sub[k] == 2) {
            pane_bits(R->cseed[k], x0, y0, x1, y1);
            continue;
        }
        glass_fill(x0, y0, x1, y1, (i * 7 + k * 3) % 3 + (P->look == 3 ? k : 0), P->look, roof);
        if (R->sub[k] == 1) {
            crack_paint(R->cseed[k], R->cx[k], R->cy[k], x0, y0, x1, y1);
        }
    }
    // the frame
    float wr = RIM, wm = MULL;
    bar(-hw + wr / 2, -hh, -hw + wr / 2, hh, wr);
    bar(hw - wr / 2, -hh, hw - wr / 2, hh, wr);
    bar(-hw, -hh + wr / 2, hw, -hh + wr / 2, wr);
    bar(-hw, hh - wr / 2, hw, hh - wr / 2, wr);
    float iw = P->w - 2 * RIM, ih = P->h - 2 * RIM;
    float pw = (iw - (P->nx - 1) * MULL) / P->nx, ph = (ih - (P->ny - 1) * MULL) / P->ny;
    for (int q = 1; q < P->nx; q++) {
        float x = -hw + RIM + q * (pw + MULL) - MULL / 2;
        bar(x, -hh, x, hh, wm);
    }
    for (int q = 1; q < P->ny; q++) {
        float y = -hh + RIM + q * (ph + MULL) - MULL / 2;
        bar(-hw, y, hw, y, wm);
    }
    cv_line(-hw + 0.3f, -hh, -hw + 0.3f, hh, 0.5f, C3(255, 200, 140), 90);
    if (roof) {
        // the iron cresting on the top edge
        for (float px = -hw + 3; px < hw - 2; px += 4) {
            cv_line(px, -hh, px, -hh - 2.6f, 0.45f, C_IRON, 230);
            cv_ring(px + 2, -hh - 1.6f, 1, 0.45f, C_IRON, 200);
        }
    }
}
// the timber of a beam or a plank (w along x)
static void paint_wood(float hw, float hh, int look)
{
    uint16_t base = look == 1 ? HX(0x6e4a2c) : look == 2 ? HX(0xb88a5a) : C_WOOD;
    cv_rect(-hw, -hh, 2 * hw, 2 * hh, base, 256);
    cv_rect(-hw, -hh, 2 * hw, fminf(0.8f, hh), C_WOODLT, 220);
    cv_rect(-hw, hh - fminf(0.9f, hh * 0.6f), 2 * hw, fminf(0.9f, hh * 0.6f), C_WOODDK, 220);
    for (int k = 0; k < (int)(hw * 2 / 6) + 2; k++) {
        float y = -hh + 0.8f + rnd() * (2 * hh - 1.6f);
        float x = -hw + rnd() * 2 * hw, l = 4 + rnd() * 10;
        cv_line(x, y, fminf(hw, x + l), y + (rnd() - 0.5f) * 0.6f, 0.35f, C_WOODDK, 120);
    }
    cv_line(-hw + 0.2f, -hh, -hw + 0.2f, hh, 0.5f, C_WOODDK, 200);
    cv_line(hw - 0.2f, -hh, hw - 0.2f, hh, 0.5f, C_WOODDK, 200);
}
static void paint_crate(float hw, float hh)
{
    cv_rect(-hw, -hh, 2 * hw, 2 * hh, HX(0x7a5232), 256);
    int n = clampi((int)(hh * 2 / 4), 2, 8);
    for (int k = 0; k < n; k++) {
        float y0 = -hh + k * 2 * hh / n;
        cv_rect(-hw + 1.2f, y0 + 0.3f, 2 * hw - 2.4f, 2 * hh / n - 0.6f, k % 2 ? C_WOOD : HX(0xa87442), 256);
        cv_rect(-hw + 1.2f, y0 + 0.3f, 2 * hw - 2.4f, 0.4f, C_WOODLT, 160);
    }
    cv_line(-hw + 1.5f, hh - 1.5f, hw - 1.5f, -hh + 1.5f, 1.6f, HX(0x6b4628), 256);
    cv_rect(-hw, -hh, 1.4f, 2 * hh, HX(0x5c391f), 256);
    cv_rect(hw - 1.4f, -hh, 1.4f, 2 * hh, HX(0x5c391f), 256);
    cv_rect(-hw, -hh, 2 * hw, 1.2f, HX(0x5c391f), 256);
    cv_rect(-hw, hh - 1.2f, 2 * hw, 1.2f, HX(0x5c391f), 256);
    for (int s = -1; s <= 1; s += 2) {
        for (int t = -1; t <= 1; t += 2) {
            cv_disc(s * (hw - 0.7f), t * (hh - 0.6f), 0.45f, HX(0x2a2420), 256);
        }
    }
}
static void paint_tank(float hw, float hh, int look)
{
    for (float px = -hw; px < hw; px += 4) {
        float f = (px + hw) / (2 * hw), k = 0.75f + 0.45f * sinf(f * PI_) - f * 0.35f;
        cv_rect(px, -hh, fminf(4.2f, hw - px), 2 * hh, cmul(C_WOOD, k + (((int)((px + hw) / 4)) % 2) * 0.08f), 256);
        cv_rect(px, -hh, 0.4f, 2 * hh, HX(0x1e0f05), 110);
    }
    for (int q = 0; q < 3; q++) {
        float hy = -hh + 2 + q * (2 * hh - 5) / 2;
        cv_rect(-hw - 0.5f, hy, 2 * hw + 1, 2, C_HOOP, 256);
        cv_rect(-hw - 0.5f, hy, 2 * hw + 1, 0.5f, C_HOOPLT, 200);
    }
    cv_ell(0, -hh, hw, 3, 0, C_WOODDK, 256);
    cv_ell(0, -hh - 0.3f, hw, 2.7f, 0, C_WOODLT, 256);
    cv_ell(0, -hh + 0.3f, hw - 2.5f, 2, 0, look == 1 ? HX(0x5a3a20) : HX(0x3c7f9e), 256);
    cv_ell(-hw * 0.3f, -hh + 0.1f, hw * 0.3f, 0.6f, 0, C3(220, 245, 255), 100);
    if (hw > 10) {
        cv_rect(hw * 0.5f, -hh + 5, 3, 2 * hh - 9, HX(0x141e23), 190);
        cv_rect(hw * 0.5f + 0.4f, -hh + 5 + (2 * hh - 9) * 0.15f, 2.2f, (2 * hh - 9) * 0.85f, C3(110, 190, 230), 220);
    }
}
static void paint_ball(float r, int look)
{
    cv_disc(0, 0, r, HX(0x141518), 256);
    for (int k = 0; k < 8; k++) {
        float f = k / 7.0f;
        cv_disc(-r * 0.3f * f, -r * 0.3f * f, r * (1 - f * 0.75f),
                cmix(HX(0x141518), look == 1 ? HX(0x8a6a4a) : HX(0x7b7f86), f), 256);
    }
    cv_arc(0, 0, r - 0.5f, 0.8f, PI_ * 0.95f, PI_ * 1.45f, C3(255, 190, 120), 150);
    cv_arc(0, 0, r - 0.5f, 0.8f, -0.2f, 0.9f, C3(140, 200, 215), 90);
    cv_ell(r * 0.2f, r * 0.33f, r * 0.27f, r * 0.15f, 0.4f, HX(0x965028), 90);
    cv_rect(-1.6f, -r - 2.5f, 3.2f, 3, HX(0x5a554c), 256);
}
static void paint_iron(float hw, float hh, int kind)
{
    if (kind == PK_WEIGHT) {
        float p[12] = {-hw, hh, hw, hh, hw, -hh * 0.4f, hw * 0.6f, -hh, -hw * 0.6f, -hh, -hw, -hh * 0.4f};
        cv_poly(p, 6, HX(0x2e3034), 256);
        cv_line(-hw + 1, -hh * 0.4f, -hw * 0.6f + 1, -hh + 0.6f, 0.8f, HX(0x7b7f86), 220);
        cv_ring(0, -hh - 1.6f, 1.8f, 0.8f, HX(0x2e3034), 256);
        cv_rect(-hw * 0.5f, -hh * 0.1f, hw, 0.6f, HX(0x5a5e66), 200);
        return;
    }
    if (kind == PK_GIRDER) {
        cv_rect(-hw, -hh, 2 * hw, 2 * hh, HX(0x33413c), 256);
        cv_rect(-hw, -hh, 2 * hw, 1, HX(0x88a99a), 220);
        cv_rect(-hw, hh - 1, 2 * hw, 1, HX(0x1c2420), 256);
        for (float x = -hw + 2; x < hw - 1; x += 4) {
            cv_disc(x, 0, 0.45f, C_BRASS, 230);
        }
        return;
    }
    // a post: a column of painted iron with a cap and a foot
    cv_rect(-hw, -hh, 2 * hw, 2 * hh, C_IRON, 256);
    cv_rect(hw * 0.2f, -hh, hw * 0.8f, 2 * hh, C_IRONSH, 150);
    cv_rect(-hw, -hh, hw * 0.4f, 2 * hh, C_IRONLIT, 140);
    cv_rect(-hw - 0.8f, -hh, 2 * hw + 1.6f, 1.4f, C_IRON, 256);
    cv_rect(-hw - 0.8f, hh - 1.4f, 2 * hw + 1.6f, 1.4f, C_IRONSH, 256);
}
static void paint_stoneish(float hw, float hh, int kind, int look)
{
    if (kind == PK_BRICK) {
        cv_rect(-hw, -hh, 2 * hw, 2 * hh, C_BRICK, 256);
        for (float y = -hh + 2.6f; y < hh; y += 2.6f) {
            cv_rect(-hw, y, 2 * hw, 0.45f, C_MORTAR, 150);
        }
        int row = 0;
        for (float y = -hh; y < hh - 0.5f; y += 2.6f, row++) {
            for (float x = -hw + (row % 2) * 2.5f; x < hw; x += 5) {
                cv_rect(x, y, 0.45f, fminf(2.6f, hh - y), C_MORTAR, 150);
                cv_rect(x + 0.6f, y + 0.5f, fminf(3.6f, hw - x - 0.6f), 1.5f, C_BRICKLT, 70 + (int)(rnd() * 60));
            }
        }
        return;
    }
    if (kind == PK_SACK) {
        cv_ell(0, hh * 0.15f, hw, hh * 0.85f, 0, HX(0xb69a6a), 256);
        cv_ell(-hw * 0.3f, -hh * 0.1f, hw * 0.5f, hh * 0.4f, 0, HX(0xd2b888), 160);
        cv_rect(-hw * 0.25f, -hh, hw * 0.5f, hh * 0.35f, HX(0xa88a5a), 256);
        cv_line(-hw * 0.35f, -hh * 0.62f, hw * 0.35f, -hh * 0.62f, 0.8f, C_ROPE, 256);
        return;
    }
    uint16_t base = kind == PK_STONE ? HX(0x9a968c) : look == 1 ? HX(0xd8d2c4) : HX(0xb8b2a4);
    cv_rect(-hw, -hh, 2 * hw, 2 * hh, base, 256);
    cv_rect(-hw, -hh, 2 * hw, 0.8f, C3(255, 248, 230), 140);
    cv_rect(-hw, hh - 0.8f, 2 * hw, 0.8f, C3(60, 56, 50), 160);
    cv_grain(-hw, -hh, 2 * hw, 2 * hh, (int)(hw * hh * 0.8f) + 8, C3(240, 236, 226), C3(90, 86, 80), 70, 0.5f);
}
static void paint_pot(float hw, float hh)
{
    float p[8] = {-hw, -hh * 0.3f, hw, -hh * 0.3f, hw * 0.75f, hh, -hw * 0.75f, hh};
    cv_poly(p, 4, C_TERRA, 256);
    cv_rect(-hw - 0.5f, -hh * 0.3f - 1.6f, 2 * hw + 1, 1.8f, HX(0xc8724a), 256);
    cv_rect(-hw, -hh * 0.3f, 2 * hw, 0.6f, HX(0xffd2aa), 128);
    for (int k = 0; k < 7; k++) {
        float a = -PI_ / 2 + (k - 3) * 0.42f;
        cv_quad(0, -hh * 0.35f, cosf(a) * hw * 0.6f, -hh * 0.35f + sinf(a) * hh * 0.9f, cosf(a) * hw * 1.1f,
                -hh * 0.35f + sinf(a) * hh * 0.5f, 1.0f, k % 2 ? C_LEAFLT : C_LEAF, 240);
    }
}
static void paint_glassbody(float hw, float hh, int kind, int look)
{
    if (kind == PK_BOTTLE) {
        uint16_t c = look == 1 ? HX(0x7a3a1c) : look == 2 ? HX(0x3a5ab0) : HX(0x2f7a4a);
        float b[16] = {-hw,        hh,  hw,          hh,  hw,         -hh * 0.2f,  hw * 0.4f, -hh * 0.55f,
                       hw * 0.35f, -hh, -hw * 0.35f, -hh, -hw * 0.4f, -hh * 0.55f, -hw,       -hh * 0.2f};
        cv_poly(b, 8, c, 200);
        cv_line(-hw * 0.55f, hh * 0.8f, -hw * 0.55f, -hh * 0.1f, 0.7f, C3(255, 255, 240), 150);
        cv_rect(-hw * 0.4f, -hh, hw * 0.8f, 1, cmul(c, 0.7f), 230);
        return;
    }
    if (kind == PK_LAMP) {
        cv_rect(-hw, -hh, 2 * hw, 1.2f, HX(0x3a2c1a), 256);
        cv_rect(-hw * 0.8f, -hh + 1, 1.6f * hw, 2 * hh - 2, HX(0xffe6a8), 230);
        cv_rect(-0.25f, -hh + 1, 0.5f, 2 * hh - 2, HX(0x3a2c1a), 256);
        cv_rect(-hw, hh - 1.2f, 2 * hw, 1.2f, HX(0x3a2c1a), 256);
        cv_ring(0, -hh - 1.4f, 1.2f, 0.5f, HX(0x3a2c1a), 256);
        return;
    }
    // a pane of glass (alone) or a lens
    cv_rect(-hw, -hh, 2 * hw, 2 * hh, HX(GLASS_TINT[look % 3]), 80);
    cv_rect(-hw, -hh, 2 * hw, 0.6f, C3(255, 255, 255), 160);
    cv_line(-hw + 0.3f, -hh, -hw + 0.3f, hh, 0.6f, C3(255, 255, 255), 140);
    float band[8] = {-hw * 0.2f, -hh, hw * 0.2f, -hh, hw * 0.2f - hh * 0.3f, hh, -hw * 0.2f - hh * 0.3f, hh};
    cv_poly(band, 4, C3(255, 246, 228), 60);
}
static void paint_misc(float hw, float hh, int kind)
{
    if (kind == PK_WHEEL) {
        float r = hw;
        cv_ring(0, 0, r - 1, 2, C_WOOD, 256);
        cv_ring(0, 0, r - 0.4f, 0.6f, HX(0x3a3530), 256);
        for (int k = 0; k < 8; k++) {
            float a = k * TAU_ / 8;
            cv_line(0, 0, cosf(a) * (r - 1.5f), sinf(a) * (r - 1.5f), 0.9f, C_WOOD, 256);
        }
        cv_disc(0, 0, r * 0.22f, C_WOODDK, 256);
        return;
    }
    if (kind == PK_BUCKET) {
        float p[8] = {-hw, -hh, hw, -hh, hw * 0.8f, hh, -hw * 0.8f, hh};
        cv_poly(p, 4, HX(0x8a929a), 256);
        cv_rect(-hw * 0.2f, -hh, hw * 0.4f, 2 * hh, HX(0xc8d0d6), 90);
        cv_rect(-hw, -hh, 2 * hw, 0.9f, HX(0x5a6066), 256);
        cv_rect(-hw * 0.9f, hh * 0.3f, 1.8f * hw, 0.6f, HX(0x5a6066), 200);
        cv_arc(0, -hh, hw * 0.9f, 0.6f, PI_, TAU_, HX(0x3a3e44), 256);
        return;
    }
    if (kind == PK_BELL) {
        float p[12] = {-hw, hh, hw, hh, hw * 0.7f, 0, hw * 0.45f, -hh, -hw * 0.45f, -hh, -hw * 0.7f, 0};
        cv_poly(p, 6, HX(0xb8862e), 256);
        cv_line(-hw * 0.55f, hh * 0.7f, -hw * 0.4f, -hh * 0.6f, 0.9f, HX(0xffe08a), 180);
        cv_rect(-hw, hh - 1, 2 * hw, 1, HX(0x7a5418), 256);
        return;
    }
    cv_rect(-hw, -hh, 2 * hw, 2 * hh, C_WOOD, 256);
}
// the picture of part i (its sprite): the box of the shape, then the painter of its kind
static void art_part(int i)
{
    const pdef_t *P = &LV->p[i];
    float hw = P->w / 2, hh = P->h / 2, m = 3.5f;
    if (P->shape == SH_POLY) {
        // (a polygon: the points of the body round its centre of mass)
        tp_body_t *b = part_body(i);
        if (!b || b->shape != TP_POLY) {
            return;
        }
        float pts[16], x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int k = 0; k < b->n; k++) {
            pts[2 * k] = b->vl[k].x * PXM;
            pts[2 * k + 1] = -b->vl[k].y * PXM;
            x0 = fminf(x0, pts[2 * k]);
            x1 = fmaxf(x1, pts[2 * k]);
            y0 = fminf(y0, pts[2 * k + 1]);
            y1 = fmaxf(y1, pts[2 * k + 1]);
        }
        if (!spr_new(i, x0 - 2, y0 - 2, x1 + 2, y1 + 2)) {
            return;
        }
        s_rng = 101 + (uint32_t)i * 13;
        uint16_t c = P->kind == PK_STONE || P->kind == PK_SLAB ? HX(0x9a968c) : P->kind == PK_BRICK ? C_BRICK : C_WOOD;
        cv_poly(pts, b->n, c, 256);
        for (int k = 0; k < b->n; k++) {
            int q = (k + 1) % b->n;
            cv_line(pts[2 * k], pts[2 * k + 1], pts[2 * q], pts[2 * q + 1], 0.7f,
                    cmul(c, pts[2 * q + 1] < pts[2 * k + 1] + 0.1f ? 1.35f : 0.6f), 230);
        }
        cv_grain(x0, y0, x1 - x0, y1 - y0, (int)((x1 - x0) * (y1 - y0) * 0.3f), cmul(c, 1.25f), cmul(c, 0.7f), 60,
                 0.5f);
        return;
    }
    if (P->kind == PK_CELL || P->kind == PK_ROOF) {
        m = 4;
    }
    if (!spr_new(i, -hw - m, -hh - m, hw + m, hh + m)) {
        return;
    }
    s_rng = 101 + (uint32_t)i * 13;
    switch (P->kind) {
    case PK_CELL:
    case PK_ROOF:
        paint_cell(i);
        break;
    case PK_BEAM:
    case PK_PLANK:
        paint_wood(hw, hh, P->look);
        break;
    case PK_CRATE:
        paint_crate(hw, hh);
        break;
    case PK_TANK:
        paint_tank(hw, hh, P->look);
        break;
    case PK_BALL:
        paint_ball(hw, P->look);
        break;
    case PK_POST:
    case PK_WEIGHT:
    case PK_GIRDER:
        paint_iron(hw, hh, P->kind);
        break;
    case PK_BRICK:
    case PK_SLAB:
    case PK_STONE:
    case PK_SACK:
        paint_stoneish(hw, hh, P->kind, P->look);
        break;
    case PK_POT:
        paint_pot(hw, hh);
        break;
    case PK_PANE:
    case PK_BOTTLE:
    case PK_LENS:
    case PK_LAMP:
        paint_glassbody(hw, hh, P->kind, P->look);
        break;
    default:
        paint_misc(hw, hh, P->kind);
        break;
    }
}
// paint a cell again (a pane cracked to its full size, or broke)
// The panes that a picture shows: 0 whole, 1 cracked (the crack in its full size), 2 broken. A pane that changes is
// painted again alone: its box in the picture is cleared, then the painter of the cell runs with a clip to that box
// (the plants of the cell come back the same: their random numbers start from the same seed).
static uint8_t s_pst[MAXP][NSUB];
static uint8_t pane_shown(int i, int k)
{
    const prt_t *R = &G.p[i];
    if (R->sub[k] == 1 && (float)G.step * DT_STEP - R->crack_t[k] < 0.55f) {
        return 0;  // (it grows: drawn live)
    }
    return R->sub[k];
}
static void art_cell_pane(int i, int k)
{
    if (!s_spr_ok[i]) {
        return;
    }
    const spr_t *sp = &s_spr[i];
    float x0, y0, x1, y1;
    pane_box(&LV->p[i], k, &x0, &y0, &x1, &y1);
    int tx0 = clampi((int)floorf((x0 - 1.2f) * 2) + sp->ox, 0, sp->w),
        tx1 = clampi((int)ceilf((x1 + 1.2f) * 2) + sp->ox, 0, sp->w);
    int ty0 = clampi((int)floorf((y0 - 1.2f) * 2) + sp->oy, 0, sp->h),
        ty1 = clampi((int)ceilf((y1 + 1.2f) * 2) + sp->oy, 0, sp->h);
    uint16_t *c = spr_col(sp);
    uint8_t *al = spr_alpha(sp);
    for (int y = ty0; y < ty1; y++) {
        memset(c + y * sp->w + tx0, 0, (size_t)(tx1 - tx0) * 2);
        memset(al + y * sp->w + tx0, 0, (size_t)(tx1 - tx0));
    }
    cv_set(c, al, sp->w, sp->h, 2, -(float)sp->ox / 2, -(float)sp->oy / 2, NULL);
    cv_clip(tx0, ty0, tx1, ty1);
    s_rng = 101 + (uint32_t)i * 13;
    paint_cell(i);
    cv_noclip();
}
// the panes of a cell that changed: paint them again; true when one changed
static bool art_cell_update(int i)
{
    bool any = false;
    for (int k = 0; k < G.p[i].npane; k++) {
        uint8_t st = pane_shown(i, k);
        if (st == s_pst[i][k]) {
            continue;
        }
        s_pst[i][k] = st;
        art_cell_pane(i, k);
        any = true;
    }
    return any;
}

// ---------------------------------------------------------------- the fragile objects (local px: the middle of the box)
// the cracks of a hurt object (hurt 0.35 .. 1): thin white lines from a point
static void obj_cracks(float hw, float hh, float hurt)
{
    if (hurt < 0.35f) {
        return;
    }
    seg_t sg[80];
    int n = crack_segs(4321, hw * 0.2f, -hh * 0.1f, -hw * 0.8f, -hh * 0.9f, hw * 0.8f, hh * 0.9f, sg, 80,
                       4 + hurt * 14);
    for (int k = 0; k < n; k++) {
        cv_line(sg[k].x0, sg[k].y0, sg[k].x1, sg[k].y1, 0.5f, C3(250, 255, 255), 220);
    }
}
// a bell jar over the box (hw, hh)
static void bell_jar(float hw, float hh)
{
    float x0 = -hw, x1 = hw, top = -hh, base = hh;
    float body[2 * 24];
    int n = 0;
    body[n++] = x0;
    body[n++] = base;
    body[n++] = x0;
    body[n++] = top + hw * 0.9f;
    for (int k = 0; k <= 16; k++) {
        float a = PI_ + PI_ * k / 16;
        body[n++] = cosf(a) * hw;
        body[n++] = top + hw * 0.9f + sinf(a) * hw * 0.9f;
    }
    body[n++] = x1;
    body[n++] = base;
    cv_poly(body, n / 2, C3(205, 240, 255), 30);
    for (int k = 0; k + 1 < n / 2; k++) {
        cv_line(body[2 * k], body[2 * k + 1], body[2 * k + 2], body[2 * k + 3], 0.6f, C3(230, 250, 255), 140);
    }
    cv_line(x0 + 2.2f, base - 3, x0 + 2.2f, top + hw * 0.9f, 0.9f, C3(255, 250, 235), 200);
    cv_arc(0, top + hw * 0.9f, hw - 2.2f, 0.9f, PI_ * 1.05f, PI_ * 1.4f, C3(255, 250, 235), 200);
    cv_line(x1 - 2.5f, base - 4, x1 - 2.5f, top + hw * 1.2f, 0.5f, C3(255, 250, 235), 100);
    cv_glow(-hw * 0.45f, top + hw * 0.35f, 3, C3(255, 255, 255), 200, false);
    cv_ell(0, top + 0.6f, 1.6f, 1.2f, 0, C3(230, 250, 255), 180);
    cv_ell(0, base, hw, 1.4f, 0, C3(230, 250, 255), 120);
}
static void paint_orchid(float hw, float hh)
{
    float by = hh;
    float pot[8] = {-4, by - 7, 4, by - 7, 3, by, -3, by};
    cv_poly(pot, 4, C_TERRA, 256);
    cv_rect(-4, by - 7, 8, 0.8f, HX(0xffc8a0), 128);
    cv_ell(-5, by - 8, 5.5f, 1.8f, 0.35f, C_LEAF, 256);
    cv_ell(5, by - 8.5f, 5.5f, 1.8f, -0.4f, C_LEAF, 256);
    cv_ell(-5.5f, by - 8.8f, 3.5f, 0.6f, 0.35f, C_LEAFLT, 200);
    cv_quad(0, by - 8, 1, by - 24, 7, by - 17, 0.6f, HX(0x4c6b33), 256);
    const float F[4][3] = {{1.2f, -19.5f, 1}, {4, -21.4f, 1.05f}, {6.6f, -19.4f, 0.95f}, {7.4f, -15.6f, 0.8f}};
    for (int f = 0; f < 4; f++) {
        float ox = F[f][0], oy = by + F[f][1], s = F[f][2];
        for (int k = 0; k < 5; k++) {
            float a = (k / 5.0f) * TAU_ - PI_ / 2;
            cv_ell(ox + cosf(a) * 1.5f * s, oy + sinf(a) * 1.5f * s, 1.35f * s, 1.0f * s, a,
                   k == 2 ? HX(0xff9ccd) : HX(0xfff3f8), 256);
        }
        cv_ell(ox, oy + 0.7f * s, 0.9f * s, 0.8f * s, 0, HX(0xc0177a), 256);
        cv_rect(ox - 0.3f, oy - 0.2f, 0.6f, 0.6f, HX(0xffd34d), 256);
    }
    bell_jar(hw, hh);
}
static void paint_egg(float hw, float hh)
{
    cv_ell(0, 0, hw, hh, 0, HX(0xf4ead8), 256);
    cv_ell(-hw * 0.3f, -hh * 0.3f, hw * 0.45f, hh * 0.5f, -0.3f, HX(0xfffaf0), 200);
    cv_ell(hw * 0.35f, hh * 0.3f, hw * 0.5f, hh * 0.5f, 0, HX(0xc8b8a0), 90);
    for (int k = 0; k < 6; k++) {
        cv_disc((hash1(k) - 0.5f) * hw * 1.2f, (hash1(k + 9) - 0.5f) * hh * 1.4f, 0.4f, HX(0xb09878), 160);
    }
}
static void paint_bird(float hw, float hh)
{
    uint16_t w = HX(0xf6f2ea), b = HX(0x3a6ab8);
    cv_ell(0, hh * 0.15f, hw * 0.75f, hh * 0.55f, -0.2f, w, 256);
    cv_disc(hw * 0.45f, -hh * 0.35f, hh * 0.38f, w, 256);
    float beak[6] = {hw * 0.75f, -hh * 0.4f, hw, -hh * 0.3f, hw * 0.75f, -hh * 0.22f};
    cv_poly(beak, 3, HX(0xe8b040), 256);
    float tail[6] = {-hw * 0.6f, hh * 0.1f, -hw, -hh * 0.35f, -hw * 0.85f, hh * 0.3f};
    cv_poly(tail, 3, w, 256);
    cv_ell(-hw * 0.1f, hh * 0.05f, hw * 0.45f, hh * 0.25f, -0.4f, b, 230);
    for (int k = 0; k < 3; k++) {
        cv_line(-hw * 0.4f + k * 2, hh * 0.1f, -hw * 0.1f + k * 2, -hh * 0.05f, 0.4f, HX(0xd8e4f8), 200);
    }
    cv_disc(hw * 0.55f, -hh * 0.42f, 0.7f, HX(0x1a1a24), 256);
    cv_ell(hw * 0.2f, -hh * 0.4f, hw * 0.25f, hh * 0.12f, 0, C3(255, 255, 255), 140);
    cv_rect(-hw * 0.2f, hh * 0.65f, 1, hh * 0.35f, HX(0xa07a40), 256);
    cv_rect(hw * 0.15f, hh * 0.65f, 1, hh * 0.35f, HX(0xa07a40), 256);
}
static void paint_swan(float hw, float hh)
{
    uint16_t g = C3(220, 245, 250);
    cv_ell(-hw * 0.1f, hh * 0.35f, hw * 0.85f, hh * 0.45f, 0, g, 170);
    cv_quad(hw * 0.45f, hh * 0.2f, hw * 0.9f, -hh * 0.4f, hw * 0.35f, -hh * 0.75f, 2.0f, g, 190);
    cv_disc(hw * 0.38f, -hh * 0.78f, hh * 0.16f, g, 200);
    float beak[6] = {hw * 0.3f, -hh * 0.82f, hw * 0.05f, -hh * 0.68f, hw * 0.28f, -hh * 0.7f};
    cv_poly(beak, 3, HX(0xffb070), 230);
    float wing[8] = {-hw * 0.6f, hh * 0.2f, hw * 0.1f, -hh * 0.15f, hw * 0.3f, hh * 0.25f, -hw * 0.3f, hh * 0.45f};
    cv_poly(wing, 4, C3(240, 252, 255), 150);
    cv_line(-hw * 0.7f, hh * 0.5f, hw * 0.4f, hh * 0.1f, 0.6f, C3(255, 255, 255), 220);
    cv_glow(hw * 0.1f, -hh * 0.1f, hh * 0.5f, C3(255, 255, 255), 90, false);
}
static void paint_vase(float hw, float hh)
{
    float v[2 * 18];
    int n = 0;
    for (int k = 0; k <= 8; k++) {
        float t = k / 8.0f, y = -hh + t * 2 * hh;
        float r = hw * (0.42f + 0.55f * sinf(t * PI_ * 0.95f + 0.1f)) * (t < 0.15f ? 0.8f : 1);
        v[n++] = -r;
        v[n++] = y;
    }
    for (int k = 8; k >= 0; k--) {
        float t = k / 8.0f, y = -hh + t * 2 * hh;
        float r = hw * (0.42f + 0.55f * sinf(t * PI_ * 0.95f + 0.1f)) * (t < 0.15f ? 0.8f : 1);
        v[n++] = r;
        v[n++] = y;
    }
    cv_poly(v, n / 2, HX(0xf4f4f0), 256);
    for (int k = 0; k < 3; k++) {
        cv_ell(0, -hh * 0.1f + k * hh * 0.3f, hw * 0.6f, 0.5f, 0, HX(0x2a5ab0), 200);
    }
    for (int k = 0; k < 5; k++) {
        cv_disc(-hw * 0.5f + k * hw * 0.25f, hh * 0.15f, 1.1f, HX(0x2a5ab0), 230);
    }
    cv_line(-hw * 0.5f, -hh * 0.4f, -hw * 0.55f, hh * 0.6f, 0.8f, C3(255, 255, 255), 200);
}
static void paint_candle(float hw, float hh)
{
    cv_rect(-hw * 0.6f, -hh * 0.55f, hw * 1.2f, hh * 1.55f, HX(0xf6ecd0), 256);
    cv_rect(-hw * 0.6f, -hh * 0.55f, hw * 0.4f, hh * 1.55f, C3(255, 252, 240), 150);
    cv_ell(hw * 0.3f, -hh * 0.4f, 1.2f, 2.5f, 0, HX(0xf6ecd0), 256);
    cv_line(0, -hh * 0.55f, 0, -hh * 0.72f, 0.5f, HX(0x2a2420), 256);
    cv_ell(0, -hh * 0.85f, 1.3f, 2.6f, 0, HX(0xffc850), 240);
    cv_ell(0, -hh * 0.8f, 0.6f, 1.3f, 0, C3(255, 255, 240), 256);
    cv_glow(0, -hh * 0.85f, 9, HX(0xffb850), 120, true);
}
static void paint_globe(float hw, float hh)
{
    float r = hw * 0.95f;
    cv_rect(-hw, hh - 4, 2 * hw, 4, HX(0x6b4a2a), 256);
    cv_rect(-hw, hh - 4, 2 * hw, 0.8f, HX(0xb08a5a), 200);
    float cy = hh - 4 - r * 0.9f;
    cv_disc(0, cy, r, C3(200, 230, 245), 90);
    cv_ell(0, cy + r * 0.55f, r * 0.8f, r * 0.25f, 0, C3(255, 255, 255), 230);
    float house[10] = {-3, cy + r * 0.45f, 3, cy + r * 0.45f, 3, cy + r * 0.1f, 0, cy - r * 0.15f, -3, cy + r * 0.1f};
    cv_poly(house, 5, HX(0xc04a3a), 256);
    for (int k = 0; k < 14; k++) {
        cv_disc((hash1(k * 3) - 0.5f) * r * 1.4f, cy + (hash1(k * 5 + 1) - 0.5f) * r * 1.4f, 0.45f, C3(255, 255, 255),
                230);
    }
    cv_arc(0, cy, r - 1, 0.9f, PI_ * 1.1f, PI_ * 1.5f, C3(255, 255, 255), 200);
}
static void paint_teacup(float hw, float hh)
{
    cv_ell(0, hh - 0.8f, hw, 1.2f, 0, HX(0xf4f4f0), 256);
    float c[8] = {-hw * 0.7f, -hh * 0.6f, hw * 0.7f, -hh * 0.6f, hw * 0.45f, hh - 1, -hw * 0.45f, hh - 1};
    cv_poly(c, 4, HX(0xf8f6f2), 256);
    cv_ring(hw * 0.75f, 0, hh * 0.3f, 1, HX(0xf4f4f0), 256);
    cv_ell(0, -hh * 0.6f, hw * 0.7f, 1.2f, 0, HX(0x8a5a2a), 256);
    cv_rect(-hw * 0.55f, -hh * 0.2f, hw * 1.1f, 0.8f, HX(0x2a5ab0), 200);
    cv_disc(-hw * 0.1f, hh * 0.15f, 1.2f, HX(0xd04a6a), 220);
}
static void paint_fishbowl(float hw, float hh)
{
    float r = fminf(hw, hh);
    cv_disc(0, hh - r, r, C3(150, 210, 235), 110);
    cv_rect(-r, hh - r * 0.9f, 2 * r, r * 0.9f, C3(90, 170, 210), 80);
    float fx = r * 0.15f, fy = hh - r * 0.9f;
    cv_ell(fx, fy, 2.6f, 1.6f, 0, HX(0xff8a2a), 256);
    float t[6] = {fx - 2.4f, fy, fx - 4.6f, fy - 1.8f, fx - 4.6f, fy + 1.8f};
    cv_poly(t, 3, HX(0xff6a1a), 256);
    cv_disc(fx + 1.3f, fy - 0.4f, 0.4f, HX(0x1a1a1a), 256);
    cv_arc(0, hh - r, r - 0.8f, 0.9f, PI_ * 1.1f, PI_ * 1.45f, C3(255, 255, 255), 210);
    cv_ell(0, hh - 2 * r + 1, r * 0.55f, 1, 0, C3(220, 245, 255), 160);
    cv_line(-r * 0.3f, hh - 0.6f, -r * 0.1f, hh - 3, 0.5f, HX(0x3a8a4a), 230);
}
// the picture of the fragile object (its sprite) and of its stand
static void art_obj(void)
{
    const objk_t *K = &OBK[LV->obj];
    float hw = K->w / 2, hh = K->h / 2;
    if (!s_spr_ok[SPR_OBJ] && !spr_new(SPR_OBJ, -hw - 6, -hh - 6, hw + 6, hh + 3)) {
        return;
    }
    spr_again(SPR_OBJ);
    s_rng = 777;
    switch (LV->obj) {
    case OB_ORCHID:
        paint_orchid(hw, hh);
        break;
    case OB_EGG:
        paint_egg(hw, hh);
        break;
    case OB_BIRD:
        paint_bird(hw, hh);
        break;
    case OB_SWAN:
        paint_swan(hw, hh);
        break;
    case OB_VASE:
        paint_vase(hw, hh);
        break;
    case OB_CANDLE:
        paint_candle(hw, hh);
        break;
    case OB_GLOBE:
        paint_globe(hw, hh);
        break;
    case OB_TEACUP:
        paint_teacup(hw, hh);
        break;
    case OB_FISH:
        paint_fishbowl(hw, hh);
        break;
    default:
        paint_egg(hw, hh);
        break;
    }
    obj_cracks(hw, hh, G.o.hurt);
}
static void art_stand(void)
{
    const objk_t *K = &OBK[LV->obj];
    if (K->sw <= 0) {
        return;
    }
    float hw = K->sw / 2, hh = K->sh / 2;
    if (!spr_new(SPR_STAND, -hw - 2, -hh - 2, hw + 2, hh + 2)) {
        return;
    }
    if (LV->obj == OB_ORCHID) {
        // the round iron table of the orchid (its legs and its top)
        cv_quad(-8, -hh + 1, -4, -hh + 12, -9, hh, 0.9f, HX(0x2a2622), 256);
        cv_quad(8, -hh + 1, 4, -hh + 12, 9, hh, 0.9f, HX(0x2a2622), 256);
        cv_line(0, -hh + 1, 0, hh, 0.9f, HX(0x2a2622), 256);
        cv_ell(0, -hh + 1, hw, 2.4f, 0, HX(0x3a342d), 256);
        cv_ell(-3, -hh + 0.4f, 8, 0.8f, 0, C3(255, 220, 170), 128);
        return;
    }
    // a turned plinth of dark wood with a brass top
    cv_rect(-hw * 0.7f, -hh, hw * 1.4f, 2 * hh, HX(0x4a3020), 256);
    cv_rect(-hw * 0.7f, -hh, hw * 0.4f, 2 * hh, HX(0x6a4a30), 200);
    cv_rect(-hw, hh - 2, 2 * hw, 2, HX(0x3a2416), 256);
    cv_rect(-hw, -hh, 2 * hw, 1.6f, C_BRASS, 256);
    cv_rect(-hw, -hh, 2 * hw, 0.5f, HX(0xffe8a8), 220);
}
