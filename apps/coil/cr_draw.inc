// COIL RIDER: the world on the screen (included by app_coil.c).
// ONE PASS OF ROWS: the edges in the view go to the screen, a list of the active edges gives the crossings of each row,
// and the even-odd rule of each shape gives the spans. An air span gets the sky (a dithered colour of the row, the
// sun or the moon, two far ridges as height lines, the stars); a solid span gets the texture of its material (a tile in
// world units) and the BRIGHT EDGE: the pixels near the top of a solid run (a counter for each column) take the
// colour of the edge. Each pixel is written one time. Then smooth lines on the edges (they hide the steps of the fill),
// the pickups, the gate, the bike and the particles.

static float s_t;   // the time of the scene (s)
static float s_z0;  // the base zoom of the layout (px of a metre)

// ---------------------------------------------------------------- the looks of the three worlds
typedef struct {
    col3 sky0, sky1, sky2;        // the sky: the top, the middle, the horizon
    col3 far1, far2;              // the two ridges (the far one, the near one)
    col3 orb, orb_glow;           // the sun or the moon
    float orb_r;                  // its radius (a part of the frame height)
    col3 rock0, rock1;            // the inside: the base and the light of the texture
    col3 edge, edge_hi, edge_lo;  // the bright edge: its colour, the top line, the lower part
    col3 rim;                     // the lines of the walls and the ceilings
    col3 brick, mortar, brick_edge;
    col3 pick, pick_glow;
    bool stars;
} look_t;
static const look_t LOOKS[WORLD_N] = {
        // DUSK: a canyon at sunset (a violet sky, a low sun, plum rock, an amber edge)
        {{38, 28, 84},    {196, 92, 128},  {255, 176, 112}, {150, 72, 120}, {98, 44, 96},
         {255, 236, 190}, {255, 170, 110}, 0.20f,           {44, 26, 44},   {74, 44, 66},
         {255, 186, 76},  {255, 244, 176}, {200, 104, 52},  {176, 100, 90}, {164, 76, 58},
         {58, 28, 34},    {246, 206, 160}, {120, 255, 228}, {80, 220, 255}, false},
        // NIGHT: a blue night with a moon (dark slate rock, a cyan edge of frost moss)
        {{6, 10, 30},     {26, 40, 92},    {70, 96, 160},   {36, 52, 104},  {20, 30, 70},
         {236, 244, 255}, {120, 150, 230}, 0.11f,           {16, 22, 40},   {32, 44, 70},
         {110, 232, 255}, {222, 252, 255}, {40, 130, 170},  {70, 110, 160}, {120, 70, 64},
         {36, 22, 30},    {200, 210, 230}, {255, 220, 110}, {255, 170, 60}, true},
        // EMBER: a volcanic dusk (a red sky, black rock with hot cracks, an orange edge)
        {{28, 6, 14},     {112, 24, 32},   {236, 96, 52},   {84, 20, 26},   {50, 12, 18},
         {255, 214, 150}, {255, 110, 50},  0.15f,           {34, 16, 18},   {66, 28, 26},
         {255, 128, 52},  {255, 226, 150}, {170, 52, 30},   {150, 64, 44},  {140, 56, 40},
         {40, 16, 16},    {232, 170, 120}, {150, 255, 200}, {90, 255, 170}, false},
};
static const look_t *s_look = &LOOKS[0];
static int s_world = -1;

// ---------------------------------------------------------------- the textures (a tile of 64 x 64 for each material and
// each of 3 depths; made at a change of the world)
#define TEXN 64
#define TEXK 22.0f       // texels in a metre
static uint16_t *s_tex;  // MAT_N x 3 x 64 x 64 (PSRAM)
static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
// a smooth value noise that wraps at 64 (the tile repeats)
static float vnoise(float x, float y, int per)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    int x0 = ((xi % per) + per) % per, y0 = ((yi % per) + per) % per, x1 = (x0 + 1) % per, y1 = (y0 + 1) % per;
    float a = (hash2(x0, y0) & 1023) / 1023.0f, b = (hash2(x1, y0) & 1023) / 1023.0f;
    float c = (hash2(x0, y1) & 1023) / 1023.0f, d = (hash2(x1, y1) & 1023) / 1023.0f;
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
static void tex_make(int world)
{
    if (!s_tex) {
        s_tex = heap_caps_malloc((size_t)MAT_N * 3 * TEXN * TEXN * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_tex) {
        return;
    }
    const look_t *L = &LOOKS[world];
    for (int sh = 0; sh < 3; sh++) {
        float dk = sh == 0 ? 1.0f : sh == 1 ? 0.80f : 0.64f;  // (deeper = darker)
        uint16_t *rock = s_tex + (size_t)(MAT_ROCK * 3 + sh) * TEXN * TEXN;
        uint16_t *brick = s_tex + (size_t)(MAT_BRICK * 3 + sh) * TEXN * TEXN;
        for (int y = 0; y < TEXN; y++) {
            for (int x = 0; x < TEXN; x++) {
                // ROCK: a broad noise, thin strata, small stones, and (EMBER) hot cracks
                float n = vnoise(x / 8.0f, y / 8.0f, 8) * 0.6f + vnoise(x / 4.0f, y / 4.0f, 16) * 0.3f +
                          vnoise(x / 2.0f, y / 2.0f, 32) * 0.1f;
                float strata = 0.5f + 0.5f * fsin((y + 3 * vnoise(x / 16.0f, 0.5f, 4)) * 0.785f);
                float k = clampf(n * 0.75f + strata * 0.25f, 0, 1);
                uint32_t h = hash2(x * 7 + 1, y * 13 + 5);
                if ((h & 63) == 0) {
                    k = clampf(k + 0.45f, 0, 1);  // (a small stone)
                }
                col3 c = mix3(L->rock0, L->rock1, k * k);
                if (world == WORLD_EMBER) {
                    float cr = fabsf(vnoise(x / 6.0f, y / 6.0f, 64 / 6 + 1) - 0.5f);
                    if (cr < 0.035f) {
                        c = mix3(c, C3(255, 90, 30), (0.035f - cr) * 18.0f);
                    }
                }
                if (world == WORLD_NIGHT && (h & 255) < 3) {
                    c = mix3(c, C3(120, 200, 255), 0.5f);  // (frost points)
                }
                c = C3((int)(c.r * dk), (int)(c.g * dk), (int)(c.b * dk));
                rock[y * TEXN + x] = c16(c);
                // BRICK: rows of 8 texels, bricks of 16 (a half shift each row), the mortar lines, a noise
                int row = y / 8, bx = (x + (row & 1) * 8) % 16, by = y % 8;
                bool mortar = by == 0 || bx == 0;
                float bn = vnoise(x / 3.0f, y / 3.0f, 21) * 0.35f +
                           ((hash2((x + (row & 1) * 8) / 16, row) & 255) / 255.0f) * 0.3f;
                col3 b = mortar ? L->mortar : mix3(L->brick, C3(L->brick.r + 40, L->brick.g + 30, L->brick.b + 20), bn);
                if (!mortar && by == 1) {
                    b = mix3(b, C3(255, 230, 200), 0.18f);
                }
                b = C3((int)(b.r * dk), (int)(b.g * dk), (int)(b.b * dk));
                brick[y * TEXN + x] = c16(b);
            }
        }
    }
}
static void tex_free(void)
{
    if (s_tex) {
        heap_caps_free(s_tex);
    }
    s_tex = NULL;
}

// ---------------------------------------------------------------- the sky (rows) and the far ridges (height lines)
static dc_t s_skyrow[MAXH];
static dc_t s_far1row[MAXH], s_far2row[MAXH];
#define RIDGE_W 1024
static int16_t s_ridge1[RIDGE_W], s_ridge2[RIDGE_W];  // the height of each ridge (px over its base line)
static int s_hz1, s_hz2;                              // the base rows of the ridges now
static int s_r1off, s_r2off;                          // the x offset of each ridge now
static int s_orbx, s_orby, s_orbr;                    // the sun or the moon on the screen
static int16_t s_starx[48], s_stary[48];
static void sky_make(void)
{
    const look_t *L = s_look;
    for (int y = 0; y < SH && y < MAXH; y++) {
        float t = (float)y / (float)(SH - 1);
        col3 c = t < 0.55f ? mix3(L->sky0, L->sky1, t / 0.55f) : mix3(L->sky1, L->sky2, (t - 0.55f) / 0.45f);
        s_skyrow[y] = dcol(c);
        // the ridges: a little lighter near their top (the haze)
        s_far1row[y] = dcol(mix3(L->far1, L->sky2, 0.18f * (1 - t)));
        s_far2row[y] = dcol(mix3(L->far2, L->far1, 0.10f * (1 - t)));
    }
    // the ridge lines: sums of waves and a noise (a mesa top for the dusk)
    float hk = SH * 0.30f;
    for (int x = 0; x < RIDGE_W; x++) {
        float u = x / (float)RIDGE_W;
        float a = 0.55f + 0.25f * fsin(u * 6.2832f * 3 + 1.0f) + 0.15f * fsin(u * 6.2832f * 7 + 2.0f) +
                  0.10f * (vnoise(x / 18.0f, 0.5f, RIDGE_W / 18) - 0.5f);
        float b = 0.45f + 0.30f * fsin(u * 6.2832f * 2 + 4.0f) + 0.18f * fsin(u * 6.2832f * 5 + 0.3f) +
                  0.12f * (vnoise(x / 11.0f, 3.5f, RIDGE_W / 11) - 0.5f);
        if (s_world == WORLD_DUSK) {
            // flat mesa tops
            if (a > 0.72f) {
                a = 0.72f + (a - 0.72f) * 0.1f;
            }
            if (b > 0.62f) {
                b = 0.62f + (b - 0.62f) * 0.1f;
            }
        }
        if (s_world == WORLD_EMBER) {
            // a volcano cone in the far ridge
            float d = fabsf(u - 0.3f) * 6.0f;
            if (d < 1) {
                a = fmaxf(a, 1.15f - d * 0.9f - (d < 0.08f ? 0.06f : 0));
            }
        }
        s_ridge1[x] = (int16_t)(a * hk);
        s_ridge2[x] = (int16_t)(b * hk * 0.85f);
    }
    for (int i = 0; i < 48; i++) {
        uint32_t h = hash2(i * 31 + 7, i * 17 + 3);
        s_starx[i] = (int16_t)(h % (uint32_t)SW);
        s_stary[i] = (int16_t)((h >> 12) % (uint32_t)(SH * 6 / 10));
    }
}
// the far layers move a little with the camera
static void sky_setup(void)
{
    float lvmid = (s_by0 + s_by1) * 0.5f;
    int base = (int)(SH * 0.86f + (cam_y - lvmid) * cam_z * 0.06f);
    s_hz1 = base - (int)(SH * 0.05f);
    s_hz2 = base + (int)(SH * 0.06f);
    s_r1off = ((int)(cam_x * cam_z * 0.04f) % RIDGE_W + RIDGE_W) % RIDGE_W;
    s_r2off = ((int)(cam_x * cam_z * 0.09f) % RIDGE_W + RIDGE_W) % RIDGE_W;
    s_orbr = (int)(SH * s_look->orb_r);
    if (s_orbr > 70) {
        s_orbr = 70;
    }
    s_orbx = (int)(SW * 0.70f) - (int)(cam_x * cam_z * 0.01f) % 40;
    s_orby = s_hz1 - (int)(SH * 0.30f) - s_orbr / 2;
}
// (a fill that writes only the pixels that change: a test on the boards gave no gain, the plain fill is in use)
static void fill2_cmp(uint16_t *row, int x0, int x1, const uint16_t *c)
{
    if (x0 >= x1) {
        return;
    }
    if (x0 & 1) {
        if (row[x0] != c[1]) {
            row[x0] = c[1];
        }
        x0++;
    }
    uint32_t w = (uint32_t)c[0] | ((uint32_t)c[1] << 16);
    uint32_t *q = (uint32_t *)(row + x0);
    int n = (x1 - x0) >> 1;
    for (int i = 0; i < n; i++) {
        if (q[i] != w) {
            q[i] = w;
        }
    }
    if ((x1 - x0) & 1) {
        if (row[x1 - 1] != c[0]) {
            row[x1 - 1] = c[0];
        }
    }
}
// an air span of the row y: [x0, x1)
static void air_span(uint16_t *row, int y, int x0, int x1)
{
    if (x0 >= x1) {
        return;
    }
    const uint16_t *sky = s_skyrow[y].c[y & 1];
    int r1 = s_hz1 - y, r2 = s_hz2 - y;  // (a ridge shows at x when its height > r)
    if (r1 >= SH * 0.40f && r2 >= SH * 0.40f) {
        fill2(row, x0, x1, sky);
    } else {
        const uint16_t *f1 = s_far1row[y].c[y & 1], *f2 = s_far2row[y].c[y & 1];
        int o1 = s_r1off, o2 = s_r2off;
        for (int x = x0; x < x1; x++) {
            int i2 = (x + o2) & (RIDGE_W - 1), i1 = (x + o1) & (RIDGE_W - 1);
            uint16_t c = s_ridge2[i2] > r2 ? f2[x & 1] : s_ridge1[i1] > r1 ? f1[x & 1] : sky[x & 1];
            row[x] = c;
        }
    }
    // the sun or the moon (a disc with a soft rim) in this row
    int dy = y - s_orby;
    if (dy > -s_orbr - 6 && dy < s_orbr + 6) {
        int R = s_orbr + 6, h2 = R * R - dy * dy;
        if (h2 > 0) {
            int hw = (int)fsq((float)h2);
            int a = s_orbx - hw, b = s_orbx + hw + 1;
            if (a < x0) {
                a = x0;
            }
            if (b > x1) {
                b = x1;
            }
            float ir = frcp((float)s_orbr);
            for (int x = a; x < b; x++) {
                int dx = x - s_orbx;
                // (only over the sky: a ridge pixel stays)
                int i1 = (x + s_r1off) & (RIDGE_W - 1), i2 = (x + s_r2off) & (RIDGE_W - 1);
                if (s_ridge1[i1] > r1 || s_ridge2[i2] > r2) {
                    continue;
                }
                float d = fsq((float)(dx * dx + dy * dy)) * ir;
                if (d < 1.0f) {
                    row[x] = c16(s_look->orb);
                } else if (d < 1.0f + 6.0f * ir) {
                    row[x] = mixc(row[x], c16(s_look->orb_glow), (int)(220 * (1 - (d - 1) * s_orbr / 6.0f)));
                }
            }
        }
    }
}

// ---------------------------------------------------------------- the terrain raster
#define MAXVE 1600                    // the edges in the view
#define MAXX  128                     // the crossings in a row
static float v_x[MAXVE], v_k[MAXVE];  // the x at the row now, the step of x for each row
static int16_t v_r1[MAXVE];           // the last row
static uint8_t v_sh[MAXVE];
static int16_t v_head[MAXH], v_next[MAXVE];
static int s_nve;
// (the lines of the edges: the screen ends and the kind)
static float l_x0[MAXVE], l_y0[MAXVE], l_x1[MAXVE], l_y1[MAXVE];
static uint8_t l_kind[MAXVE];  // 0 = a top (a floor), 1 = a wall, 2 = a ceiling; | 4 = brick
static int s_nl;
// (the column state and the depth table are read for each solid pixel: they go to the internal RAM when it has room;
// the static copies in PSRAM are the fallback)
static int16_t s_ctop_ps[660], s_cstamp_ps[660];
static uint8_t s_dk_ps[256];
static int16_t *s_ctop = s_ctop_ps,
               *s_cstamp = s_cstamp_ps;  // the first row of the solid run of each column, its last row
static uint8_t *s_dk = s_dk_ps;          // the colour kind of each depth from the top of a solid run (px)
static void *s_fast;
static void fast_mem(bool on)
{
    if (on && !s_fast && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > 24000) {
        s_fast = heap_caps_malloc(660 * 4 + 256, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!on && s_fast) {
        heap_caps_free(s_fast);
        s_fast = NULL;
    }
    if (s_fast) {
        s_ctop = (int16_t *)s_fast;
        s_cstamp = s_ctop + 660;
        s_dk = (uint8_t *)(s_cstamp + 660);
    } else {
        s_ctop = s_ctop_ps;
        s_cstamp = s_cstamp_ps;
        s_dk = s_dk_ps;
    }
}
static dc_t s_edge_dc[3];         // the edge colours: the top line, the edge, the lower edge
static dc_t s_bedge_dc[3];        // the same for the bricks
static int s_view_x0, s_view_x1;  // (the world x of the view, for the things)

static void terrain_setup(void)
{
    // the view in the world (a margin for the roll and the shake)
    float hw = (SW * 0.5f + 20) / cam_z, hh = (SH * 0.5f + 20 + fabsf(s_roll) * SW * 0.5f) / cam_z;
    float vx0 = cam_x - hw, vx1 = cam_x + hw, vy0 = cam_y - hh, vy1 = cam_y + hh;
    for (int y = 0; y < SH; y++) {
        v_head[y] = -1;
    }
    s_nve = 0;
    s_nl = 0;
    for (int i = 0; i < s_ne; i++) {
        const edge_t *e = &E[i];
        float bx = e->ax + e->dx * e->len, by = e->ay + e->dy * e->len;
        // (an edge at the left of the view still changes the parity of the rows: it stays, as a line at x = -1)
        if (fminf(e->ax, bx) > vx1) {
            continue;
        }
        if (fmaxf(e->ay, by) < vy0 || fminf(e->ay, by) > vy1) {
            continue;
        }
        bool left = fmaxf(e->ax, bx) < vx0;
        float x0 = SX(e->ax), y0 = SYW(e->ax, e->ay), x1 = SX(bx), y1 = SYW(bx, by);
        if (left) {
            x0 = x1 = -1;
            y0 = SYc(-1, e->ay);
            y1 = SYc(-1, by);
        }
        if (!left && s_nl < MAXVE) {
            l_x0[s_nl] = x0;
            l_y0[s_nl] = y0;
            l_x1[s_nl] = x1;
            l_y1[s_nl] = y1;
            // the air normal (dy, -dx): up = a floor
            float ny = -e->dx;
            l_kind[s_nl] = (uint8_t)((ny > 0.45f ? 0 : ny < -0.45f ? 2 : 1) | (s_smat[e->shape] == MAT_BRICK ? 4 : 0));
            s_nl++;
        }
        // the fill: the rows whose middle is in [ytop, ybot)
        float yt = y0, yb = y1, xt = x0, xb = x1;
        if (yt > yb) {
            float t = yt;
            yt = yb;
            yb = t;
            t = xt;
            xt = xb;
            xb = t;
        }
        int r0 = (int)ceilf(yt - 0.5f), r1 = (int)ceilf(yb - 0.5f) - 1;
        if (r1 < r0 || r1 < 0 || r0 >= SH || s_nve >= MAXVE) {
            continue;
        }
        float k = (xb - xt) * frcp(yb - yt);
        float x = xt + (r0 + 0.5f - yt) * k;
        if (r0 < 0) {
            x += -r0 * k;
            r0 = 0;
        }
        if (r1 > SH - 1) {
            r1 = SH - 1;
        }
        int j = s_nve++;
        v_x[j] = x;
        v_k[j] = k;
        v_r1[j] = (int16_t)r1;
        v_sh[j] = e->shape;
        v_next[j] = v_head[r0];
        v_head[r0] = (int16_t)j;
    }
    s_view_x0 = (int)vx0 - 1;
    s_view_x1 = (int)vx1 + 1;
    // the depth table of the bright edge (px from the top of a solid run)
    int g0 = 1, g1 = (int)(0.10f * cam_z + 0.5f), g2 = g1 + (int)(0.07f * cam_z + 0.5f);
    if (g1 < 2) {
        g1 = 2;
    }
    if (g2 < g1 + 1) {
        g2 = g1 + 1;
    }
    int s1 = (int)(0.9f * cam_z), s2 = (int)(2.2f * cam_z);
    for (int d = 0; d < 256; d++) {
        s_dk[d] = (uint8_t)(d < g0 ? 0 : d < g1 ? 1 : d < g2 ? 2 : d < s1 ? 3 : d < s2 ? 4 : 5);
    }
    for (int x = 0; x < SW && x < 660; x++) {
        s_cstamp[x] = -1;
        s_ctop[x] = -9999;
    }
}
static void edge_colours(void)
{
    s_edge_dc[0] = dcol(s_look->edge_hi);
    s_edge_dc[1] = dcol(s_look->edge);
    s_edge_dc[2] = dcol(s_look->edge_lo);
    s_bedge_dc[0] = dcol(C3(255, 240, 210));
    s_bedge_dc[1] = dcol(s_look->brick_edge);
    s_bedge_dc[2] = dcol(mix3(s_look->brick_edge, s_look->mortar, 0.5f));
}
// a solid span [x0, x1) of the row y with the material m
static void solid_span(uint16_t *row, int y, int x0, int x1, int m)
{
    if (x0 >= x1) {
        return;
    }
    // the texture coordinates of x0 (16.16): the world point of the pixel
    float iz = frcp(cam_z);
    float wx = cam_x + (x0 + 0.5f - s_vcx - s_shx) * iz;
    float wy = cam_y + (s_vcy + s_shy + (x0 + 0.5f - s_vcx) * s_roll - (y + 0.5f)) * iz;
    int32_t u = (int32_t)(wx * TEXK * 65536.0f), du = (int32_t)(TEXK * iz * 65536.0f);
    int vr = ((int32_t)(-wy * TEXK * 65536.0f) >> 16) & (TEXN - 1);  // (no roll: one texture row for the span)
    const uint16_t *T = s_tex + (size_t)(m * 3) * TEXN * TEXN + vr * TEXN;
    const uint16_t *TR[3] = {T, T + TEXN * TEXN, T + 2 * TEXN * TEXN};
    const dc_t *ed = m == MAT_BRICK ? s_bedge_dc : s_edge_dc;
    int16_t *top = s_ctop, *st = s_cstamp;
    const uint8_t *dk = s_dk;
    int ym1 = y - 1, yo = y & 1;
    for (int x = x0; x < x1; x++) {
        int t = top[x];
        if (st[x] != ym1) {
            t = y;
            top[x] = (int16_t)y;
        }
        st[x] = (int16_t)y;
        int d = y - t;
        int k = d < 256 ? dk[d] : 5;
        if (k < 3) {
            row[x] = ed[k].c[yo][x & 1];
        } else {
            row[x] = TR[k - 3][(u >> 16) & (TEXN - 1)];
        }
        u += du;
    }
}
static void terrain_rows(void)
{
    static int16_t act[MAXVE];
    int na = 0;
    float cx[MAXX];
    uint8_t csh[MAXX];
    for (int y = 0; y < SH; y++) {
        // the new edges of this row, and the end of the old ones
        for (int j = v_head[y]; j >= 0; j = v_next[j]) {
            if (na < MAXVE) {
                act[na++] = (int16_t)j;
            }
        }
        int n = 0;
        for (int q = 0; q < na;) {
            int j = act[q];
            if (v_r1[j] < y) {
                act[q] = act[--na];
                continue;
            }
            if (n < MAXX) {
                // insertion in the order of x
                float x = v_x[j];
                int p = n++;
                while (p > 0 && cx[p - 1] > x) {
                    cx[p] = cx[p - 1];
                    csh[p] = csh[p - 1];
                    p--;
                }
                cx[p] = x;
                csh[p] = v_sh[j];
            }
            v_x[j] += v_k[j];
            q++;
        }
        int fx0 = s_x0[y], fx1 = s_x1[y];
        if (fx0 >= fx1) {
            continue;
        }
        uint16_t *row = &g_fb[y * SW];
        // the walk: the parity of each shape; the solid = any shape odd; the material = the highest odd shape
        uint32_t par = 0;
        int x = fx0;
        for (int i = 0; i <= n; i++) {
            int xe = i < n ? (int)(cx[i] + 0.5f) : fx1;
            if (xe > fx1) {
                xe = fx1;
            }
            if (xe > x) {
                if (!par) {
                    air_span(row, y, x, xe);
                } else {
                    int top = s_nshape - 1;  // (no __builtin_clz: the Tab5 ABI has no __clzsi2)
                    while (top > 0 && !((par >> top) & 1u)) {
                        top--;
                    }
                    solid_span(row, y, x, xe, s_smat[top]);
                }
                x = xe;
            }
            if (i < n) {
                par ^= 1u << (csh[i] & 31);
            }
        }
        // the stars (the night), only over the sky
        if (s_look->stars && y < SH * 6 / 10) {
            for (int i = 0; i < 48; i++) {
                if (s_stary[i] != y) {
                    continue;
                }
                int sx = s_starx[i];
                if (sx < fx0 || sx >= fx1) {
                    continue;
                }
                if (row[sx] == s_skyrow[y].c[y & 1][sx & 1]) {
                    row[sx] = rgb(220, 230, 255);
                }
            }
        }
    }
}
// the smooth lines on the edges (they hide the steps of the fill): a floor gets the bright top line, a wall and a
// ceiling a dim rim
static void aline(float x0, float y0, float x1, float y1, uint16_t c, int a)
{
    float dx = x1 - x0, dy = y1 - y0;
    float adx = fabsf(dx), ady = fabsf(dy);
    if (adx < 0.01f && ady < 0.01f) {
        return;
    }
    if (adx >= ady) {
        if (x0 > x1) {
            float t = x0;
            x0 = x1;
            x1 = t;
            t = y0;
            y0 = y1;
            y1 = t;
        }
        float k = dy * frcp(dx);
        int xa = (int)ceilf(x0 - 0.5f), xb = (int)floorf(x1 - 0.5f);
        if (xa < 0) {
            xa = 0;
        }
        if (xb > SW - 1) {
            xb = SW - 1;
        }
        for (int x = xa; x <= xb; x++) {
            float y = y0 + (x + 0.5f - x0) * k - 0.5f;
            int yi = (int)floorf(y);
            float f = y - yi;
            pxa(x, yi, c, (int)(a * (1 - f)));
            pxa(x, yi + 1, c, (int)(a * f));
        }
    } else {
        if (y0 > y1) {
            float t = x0;
            x0 = x1;
            x1 = t;
            t = y0;
            y0 = y1;
            y1 = t;
        }
        float k = dx * frcp(dy);
        int ya = (int)ceilf(y0 - 0.5f), yb = (int)floorf(y1 - 0.5f);
        if (ya < 0) {
            ya = 0;
        }
        if (yb > SH - 1) {
            yb = SH - 1;
        }
        for (int y = ya; y <= yb; y++) {
            float x = x0 + (y + 0.5f - y0) * k - 0.5f;
            int xi = (int)floorf(x);
            float f = x - xi;
            pxa(xi, y, c, (int)(a * (1 - f)));
            pxa(xi + 1, y, c, (int)(a * f));
        }
    }
}
static void terrain_lines(void)
{
    uint16_t top = c16(s_look->edge_hi), rim = c16(s_look->rim), btop = c16(C3(255, 236, 200)),
             brim = c16(s_look->brick_edge);
    for (int i = 0; i < s_nl; i++) {
        int k = l_kind[i] & 3;
        bool br = l_kind[i] & 4;
        if (k == 0) {
            aline(l_x0[i], l_y0[i], l_x1[i], l_y1[i], br ? btop : top, 230);
        } else {
            aline(l_x0[i], l_y0[i], l_x1[i], l_y1[i], br ? brim : rim, k == 1 ? 200 : 150);
        }
    }
}

// ---------------------------------------------------------------- the particles
enum {
    PK_DUST,
    PK_SPARK,
    PK_SHARD,
    PK_STAR
};
typedef struct {
    float x, y, vx, vy, t, life, r;
    uint8_t kind;
} part_t;
#define MAXPART 64
static part_t s_part[MAXPART];
static int s_npart;
static void part_add(int kind, float x, float y, float vx, float vy, float life, float r)
{
    if (s_npart >= MAXPART) {
        return;
    }
    part_t *p = &s_part[s_npart++];
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->t = 0;
    p->life = life;
    p->r = r;
    p->kind = (uint8_t)kind;
}
static void parts_step(float dt)
{
    for (int i = 0; i < s_npart;) {
        part_t *p = &s_part[i];
        p->t += dt;
        if (p->t >= p->life) {
            *p = s_part[--s_npart];
            continue;
        }
        float g = p->kind == PK_DUST ? 1.5f : p->kind == PK_STAR ? 0.0f : 7.0f;
        p->vy -= g * dt;
        p->vx -= p->vx * 1.5f * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        i++;
    }
}
static void parts_draw(void)
{
    for (int i = 0; i < s_npart; i++) {
        const part_t *p = &s_part[i];
        float k = 1 - p->t / p->life;
        float sx = SX(p->x), sy = SYW(p->x, p->y);
        float r = fmaxf(0.8f, p->r * cam_z * (p->kind == PK_DUST ? 1.6f - 0.6f * k : 1));
        switch (p->kind) {
        case PK_DUST:
            disc(sx, sy, r, c16(mix3(s_look->rock1, s_look->edge, 0.4f)), (int)(150 * k));
            break;
        case PK_SPARK:
            disc(sx, sy, r, rgb(255, 230, 150), (int)(255 * k));
            break;
        case PK_SHARD:
            disc(sx, sy, r, c16(s_look->pick), (int)(255 * k));
            break;
        default:
            glow(sx, sy, r * 3, s_look->pick_glow, (int)(120 * k));
            break;
        }
    }
}

// ---------------------------------------------------------------- the pickups and the gate
static void pick_draw(float x, float y, int k)
{
    float bob = 0.08f * fsin(s_t * 2.6f + k * 1.3f);
    float sx = SX(x), sy = SYW(x, y + bob);
    float r = 0.30f * cam_z;
    float sp = fsin(s_t * 2.0f + k);  // (the turn: the width of the crystal)
    float w = r * (0.35f + 0.65f * fabsf(sp));
    glow(sx, sy, r * 2.4f, s_look->pick_glow, 70);
    float pt[8] = {sx, sy - r * 1.25f, sx + w, sy, sx, sy + r * 1.25f, sx - w, sy};
    poly(pt, 4, c16(mix3(s_look->pick, C3(0, 0, 0), 0.35f)), 256);
    float pt2[8] = {sx, sy - r * 1.25f, sx + w * (sp > 0 ? 1 : 0.2f), sy,
                    sx, sy + r * 1.25f, sx - w * (sp > 0 ? 0.2f : 1), sy};
    poly(pt2, 4, c16(s_look->pick), 256);
    disc(sx - w * 0.25f, sy - r * 0.45f, fmaxf(0.8f, r * 0.16f), rgb(255, 255, 255), 230);
}
static float s_gate_k;  // 0..1 the gate opens (light)
static void gate_draw(void)
{
    float gx = gate_x(), gy = gate_y();
    if (gx < s_view_x0 - 3 || gx > s_view_x1 + 3) {
        return;
    }
    float o = s_gate_k;
    col3 cold = C3(110, 100, 120), hot = C3(255, 220, 120);
    uint16_t c = c16(mix3(cold, hot, o));
    float R = 0.85f, H = 1.0f;
    // the posts
    wcap(gx - R, gy, gx - R, gy + H, 0.09f, 0.09f, c, 256);
    wcap(gx + R, gy, gx + R, gy + H, 0.09f, 0.09f, c, 256);
    // the arch
    float px = gx + R, py = gy + H;
    for (int i = 1; i <= 10; i++) {
        float a = i * (3.1416f / 10);
        float qx = gx + R * fcos(a), qy = gy + H + R * fsin(a);
        wcap(px, py, qx, qy, 0.09f, 0.09f, c, 256);
        px = qx;
        py = qy;
    }
    // the light inside (open): a turning ring and a glow
    float cx = SX(gx), cy = SYW(gx, gy + H * 0.9f);
    if (o > 0.02f) {
        glow(cx, cy, R * cam_z * 1.6f, C3(255, 200, 110), (int)(110 * o));
        for (int i = 0; i < 6; i++) {
            float a = s_t * 2.2f + i * 1.047f;
            disc(cx + R * 0.6f * cam_z * fcos(a), cy + R * 0.75f * cam_z * fsin(a), fmaxf(1, 0.07f * cam_z),
                 rgb(255, 250, 220), (int)(230 * o));
        }
    } else {
        // closed: a dim dot for each missing pickup
        disc(cx, cy, fmaxf(1.2f, 0.10f * cam_z), c16(cold), 200);
    }
}

// ---------------------------------------------------------------- the bike
// a coil spring from (x0, y0) to (x1, y1) (screen px): n turns, the width w; its pitch shows the stretch
static void coil(float x0, float y0, float x1, float y1, float w, int n, uint16_t c, float lw)
{
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    if (l2 < 1) {
        return;
    }
    float il = frsqrt(l2), nx = -dy * il, ny = dx * il;
    float px = x0, py = y0;
    int m = n * 2;
    for (int i = 1; i <= m; i++) {
        float t = (float)i / (float)m;
        float side = i == m ? 0 : (i & 1 ? 1.0f : -1.0f);
        float qx = x0 + dx * t + nx * w * side, qy = y0 + dy * t + ny * w * side;
        capsule(px, py, qx, qy, lw, lw, c, 256);
        px = qx;
        py = qy;
    }
}
static void wheel_draw(const whl_t *W, bool rear)
{
    float sx = SX(W->x), sy = SYW(W->x, W->y), r = WR * cam_z;
    disc(sx, sy, r, rgb(26, 24, 32), 256);
    disc(sx, sy, r * 0.74f, rgb(70, 70, 86), 256);
    disc(sx, sy, r * 0.62f, rgb(36, 34, 46), 256);
    float lw = fmaxf(0.7f, r * 0.07f);
    for (int i = 0; i < 3; i++) {
        float a = -W->ang + i * 2.094f;  // (the screen y goes down)
        capsule(sx, sy, sx + r * 0.62f * fcos(a), sy + r * 0.62f * fsin(a), lw, lw, rgb(200, 204, 220), 256);
    }
    disc(sx, sy, fmaxf(1.2f, r * 0.18f), rear ? rgb(255, 170, 60) : rgb(220, 224, 236), 256);
}
// the body frame to the screen
static float s_bc, s_bs, s_bdd;  // the cos and sin of the body angle, the draw direction (-1..1)
static inline void bpt(float bx, float by, float *sx, float *sy)
{
    const bike_t *b = &G.b;
    bx *= s_bdd;
    float wx = b->x + bx * s_bc - by * s_bs, wy = b->y + bx * s_bs + by * s_bc;
    *sx = SX(wx);
    *sy = SYW(wx, wy);
}
static void bcap(float ax, float ay, float bx, float by, float r0, float r1, uint16_t c)
{
    float x0, y0, x1, y1;
    bpt(ax, ay, &x0, &y0);
    bpt(bx, by, &x1, &y1);
    capsule(x0, y0, x1, y1, fmaxf(0.7f, r0 * cam_z), fmaxf(0.7f, r1 * cam_z), c, 256);
}
static void bike_draw(void)
{
    const bike_t *b = &G.b;
    s_bc = fcos(b->a);
    s_bs = fsin(b->a);
    // the draw direction: during a turn it goes through 0 (the rider turns round)
    float k = G.turn_k * G.turn_k * (3 - 2 * G.turn_k);
    s_bdd = b->dir * (2 * k - 1);
    if (fabsf(s_bdd) < 0.15f) {
        s_bdd = s_bdd < 0 ? -0.15f : 0.15f;
    }
    float ln = b->lean_f * (b->dir > 0 ? 1 : -1);  // (+ = the rider forward)
    int ri = b->dir > 0 ? 0 : 1;
    // the suspension: the coil from the body to each wheel, with the damper rod inside
    for (int i = 0; i < 2; i++) {
        const whl_t *W = &b->wh[i];
        float ax = (i ? WX : -WX) * 0.78f, ay = 0.02f;
        float x0, y0;
        {
            float wx = b->x + ax * s_bc - ay * s_bs, wy = b->y + ax * s_bs + ay * s_bc;
            x0 = SX(wx);
            y0 = SYW(wx, wy);
        }
        float x1 = SX(W->x), y1 = SYW(W->x, W->y);
        capsule(x0, y0, x0 + (x1 - x0) * 0.55f, y0 + (y1 - y0) * 0.55f, fmaxf(1, 0.045f * cam_z),
                fmaxf(1, 0.045f * cam_z), rgb(150, 156, 176), 256);
        capsule(x1, y1, x1 + (x0 - x1) * 0.45f, y1 + (y0 - y1) * 0.45f, fmaxf(0.8f, 0.03f * cam_z),
                fmaxf(0.8f, 0.03f * cam_z), rgb(210, 214, 230), 256);
        coil(x0, y0, x1, y1, fmaxf(1.5f, 0.075f * cam_z), 6, rgb(255, 196, 60), fmaxf(0.7f, 0.022f * cam_z));
    }
    wheel_draw(&b->wh[1 - ri], false);
    wheel_draw(&b->wh[ri], true);
    // the body: the frame, the tank, the seat (the bike faces +x of the body frame in these numbers)
    {
        static const float BODY[][2] = {{0.50f, 0.06f},   {0.40f, 0.20f},   {0.10f, 0.24f},
                                        {-0.12f, 0.18f},  {-0.44f, 0.16f},  {-0.58f, 0.06f},
                                        {-0.36f, -0.06f}, {-0.08f, -0.14f}, {0.30f, -0.08f}};
        float pt[18];
        for (int i = 0; i < 9; i++) {
            bpt(BODY[i][0], BODY[i][1], &pt[2 * i], &pt[2 * i + 1]);
        }
        poly(pt, 9, rgb(30, 120, 124), 256);
        static const float TOP[][2] = {{0.46f, 0.10f},  {0.38f, 0.21f},  {0.10f, 0.25f},
                                       {-0.10f, 0.19f}, {-0.10f, 0.10f}, {0.30f, 0.08f}};
        float pt2[12];
        for (int i = 0; i < 6; i++) {
            bpt(TOP[i][0], TOP[i][1], &pt2[2 * i], &pt2[2 * i + 1]);
        }
        poly(pt2, 6, rgb(60, 214, 200), 256);
        bcap(-0.12f, 0.20f, -0.46f, 0.17f, 0.05f, 0.05f, rgb(30, 28, 40));     // (the seat)
        bcap(-0.02f, -0.06f, 0.20f, -0.06f, 0.09f, 0.09f, rgb(60, 60, 74));    // (the engine)
        bcap(0.36f, 0.22f, 0.30f, 0.40f, 0.025f, 0.025f, rgb(200, 204, 220));  // (the bars)
    }
    // the rider: the legs, the body, the arms, the head (the lean moves the body forward or back)
    {
        float hx = b->hx * b->dir;                                      // (the head x for the bike that faces +x)
        float sx = 0.02f + ln * 0.10f, sy = 0.50f - fabsf(ln) * 0.06f;  // the shoulder
        float px = -0.16f, py = 0.24f;                                  // the hip
        uint16_t suit = rgb(70, 76, 170), suit2 = rgb(48, 52, 124), boot = rgb(30, 26, 34);
        bcap(px, py, 0.10f, 0.12f, 0.075f, 0.065f, suit2);  // (the thigh)
        bcap(0.10f, 0.12f, -0.02f, -0.06f, 0.06f, 0.055f, suit2);
        bcap(-0.02f, -0.06f, 0.08f, -0.08f, 0.045f, 0.04f, boot);
        bcap(px, py, sx, sy, 0.10f, 0.09f, suit);  // (the body)
        float ex = (sx + 0.32f) * 0.5f + 0.02f, ey = (sy + 0.40f) * 0.5f - 0.05f;
        bcap(sx, sy, ex, ey, 0.05f, 0.045f, suit);
        bcap(ex, ey, 0.31f, 0.39f, 0.045f, 0.04f, suit);
        float hwx, hwy;
        head_pos(b, &hwx, &hwy);
        float hcx = SX(hwx), hcy = SYW(hwx, hwy);
        float hr = HEAD_R * cam_z;
        disc(hcx, hcy, hr, rgb(255, 126, 52), 256);
        float vx, vy;
        bpt(hx + 0.09f, HEAD_Y + 0.01f, &vx, &vy);
        disc(vx, vy, hr * 0.55f, rgb(40, 40, 60), 256);
        disc(hcx - hr * 0.3f, hcy - hr * 0.4f, fmaxf(0.8f, hr * 0.22f), rgb(255, 220, 190), 200);
    }
}
