// DEEP CURRENT: the world (included by app_deep.c). The camera, the water (the rows of the background: the colour of
// the depth with a dither, the light rays, the pool of light round the creature in the dark, the dark edges), the far
// layers (a whale, the far floor with sea pens, far sparks), the floor of the abyss with its vents, the marine snow,
// the plankton, the particles and the deep current.

// ================================================================ the state of the dive
static int s_zone;  // 0..3
static const zone_t *Z = &ZONE[0];
static float s_time;               // the world clock (s)
static float s_cur_x, s_cur_open;  // the deep current: its x, and how open it is (0..1)
static bool s_cur_on;              // the current is open (the growth goal of the zone is reached)

// ================================================================ the camera
typedef struct {
    float x, y, z, tx, ty, tz, sh, punch, wide;
} cam_t;
static cam_t C;
static float s_shx, s_shy;  // the shake of this frame (px)
static inline float scx(void)
{
    return s_fw * 0.5f;
}
static inline float scy(void)
{
    return s_fh * 0.5f;
}
static inline float sx_of(float x)
{
    return scx() + wrapd(x - C.x) * C.z + s_shx;
}
static inline float sy_of(float y)
{
    return scy() + (y - C.y) * C.z + s_shy;
}
// a point of the world on the screen (false: far out of the screen, with a margin m px)
static inline bool on_screen(float x, float y, float m, float *sx, float *sy)
{
    *sx = sx_of(x);
    *sy = sy_of(y);
    return *sx > -m && *sx < s_fw + m && *sy > -m && *sy < s_fh + m;
}
static void cam_kick(float a)
{
    if (a > C.sh) {
        C.sh = a;
    }
}
static void cam_step(float dt, float hx, float hy, float vx, float vy, float zbase)
{
    // a soft follow with a look ahead, a breath of the zoom, the shake that fades
    float tx = hx + vx * 0.32f, ty = hy + vy * 0.22f;
    float tz = zbase * (1 + 0.035f * sinf(s_time * 0.42f)) * (1 + C.punch) / (1 + C.wide);
    float k = 1 - expf(-dt * 2.6f), kz = 1 - expf(-dt * 1.6f);
    C.x = wrapx(C.x + wrapd(tx - C.x) * k);
    C.y += (ty - C.y) * k;
    C.z += (tz - C.z) * kz;
    C.punch *= expf(-dt * 3.0f);
    // the creature stays in the middle part of the screen (a hard limit)
    float mx = s_fw * 0.32f / C.z, my = s_fh * 0.30f / C.z;
    float dx = wrapd(hx - C.x);
    if (dx > mx) {
        C.x = wrapx(C.x + dx - mx);
    }
    if (dx < -mx) {
        C.x = wrapx(C.x + dx + mx);
    }
    if (hy - C.y > my) {
        C.y = hy - my;
    }
    if (hy - C.y < -my) {
        C.y = hy + my;
    }
    // the view stays in the zone (a little of the surface shows at the top of the shallows)
    float half = s_fh * 0.5f / C.z, top = s_zone == 0 ? -26.0f : -10.0f, bot = Z->h + 14;
    if (bot - top < 2 * half) {
        C.y = (top + bot) * 0.5f;
    } else {
        C.y = clampf(C.y, top + half, bot - half);
    }
    C.sh *= expf(-dt * 6.5f);
    s_shx = C.sh * vnoise1(s_time * 37 + 3, 7);
    s_shy = C.sh * vnoise1(s_time * 41 + 71, 9);
}

// ================================================================ the water: the rows of the background
// The colour of the water depends on the depth (with a parallax: the far water moves slowly), the rays and the pool
// of light add to it, the edges are darker. Each row is cut into pieces of one colour; each piece gets 4 colours (a
// dither of the row) and is filled with them: the background only writes the frame (no read).
typedef struct {
    int16_t x;
    int16_t r, g, b;
} edge_t;
#define NEDGE 64
static float s_pool_x, s_pool_y, s_pool_r, s_pool_k;  // the pool of light (screen px), its strength
static float s_flash;                                 // a flash of light (the FLASH organ): 0..1
static void water_colour(float f, float *c)
{
    f = clampf(f, 0, 1);
    const uint8_t *a = f < 0.5f ? Z->w0 : Z->w1, *b = f < 0.5f ? Z->w1 : Z->w2;
    float k = f < 0.5f ? f * 2 : (f - 0.5f) * 2;
    for (int i = 0; i < 3; i++) {
        c[i] = a[i] + (b[i] - a[i]) * k;
    }
}
static void draw_water(void)
{
    const int W = s_fw, H = s_fh;
    // the vignette: steps at the two ends of each row (a darker frame, as the look page)
    static const int VX[] = {0, 10, 22, 36, 52, 70, 92, 118};
    static const float VK[] = {0.52f, 0.60f, 0.68f, 0.76f, 0.83f, 0.89f, 0.94f, 0.98f};
    const int NV = 8;
    // the rays (from above; they move a little with the far camera)
    float ray = Z->rays * clampf(1.15f - C.y / Z->h * 1.1f, 0, 1);
    float rox[5], rw[5], ra[5];
    for (int k = 0; k < 5; k++) {
        float o = 40 + k * 140 + sinf(s_time * 0.15f + k) * 20 - C.x * 0.25f;
        o = fmodf(o, 700.0f);
        if (o < -100) {
            o += 700;
        }
        rox[k] = o * W / SW;
        rw[k] = 20 + (k % 3) * 12;
        ra[k] = (0.05f + 0.03f * sinf(s_time * 0.4f + k * 2)) * ray * 2.4f;
    }
    for (int y = 0; y < H; y++) {
        // the depth of this row (the colour moves at half the speed of the camera: far water)
        float wy = C.y + (y - H * 0.5f) / C.z;
        float f = (C.y + (y - H * 0.5f) / C.z * 0.5f) / Z->h;
        float base[3];
        water_colour(f, base);
        float rowk = 1.10f - 0.28f * y / H;  // the top of the screen is brighter
        if (y < 6) {
            rowk *= 0.78f + 0.037f * y;
        }
        if (y > H - 8) {
            rowk *= 0.80f + 0.025f * (H - y);
        }
        for (int i = 0; i < 3; i++) {
            base[i] *= rowk;
        }
        // the surface (the shallows): the light under the waves
        if (s_zone == 0 && wy < 2) {
            float k = clampf(-wy / 26.0f, 0, 1);
            base[0] = lerpf(110, 70, k);
            base[1] = lerpf(205, 170, k);
            base[2] = lerpf(225, 205, k);
        }
        static edge_t e[NEDGE];
        int ne = 0;
        // the rays: a band that slants down to the right; it fades to nothing at the row 120
        if (ray > 0.01f && y < 124) {
            float t = (y + 4) / 128.0f, fade = 1 - y / 120.0f;
            for (int k = 0; k < 5 && ne < NEDGE - 2; k++) {
                int a0 = (int)(rox[k] + 40 * t), a1 = (int)(rox[k] + rw[k] + 60 * t);
                float L = ra[k] * fade;
                int r = (int)(120 * L), g = (int)(220 * L), b = (int)(255 * L);
                if (a1 <= 0 || a0 >= W || (r | g | b) == 0) {
                    continue;
                }
                // (a soft side: half of the light in the first and the last 6 px)
                e[ne++] = (edge_t){(int16_t)a0, (int16_t)(r / 2), (int16_t)(g / 2), (int16_t)(b / 2)};
                e[ne++] = (edge_t){(int16_t)(a0 + 6), (int16_t)(r - r / 2), (int16_t)(g - g / 2), (int16_t)(b - b / 2)};
                e[ne++] = (edge_t){(int16_t)(a1 - 6), (int16_t)-(r - r / 2), (int16_t)-(g - g / 2),
                                   (int16_t)-(b - b / 2)};
                e[ne++] = (edge_t){(int16_t)a1, (int16_t)-(r / 2), (int16_t)-(g / 2), (int16_t)-(b / 2)};
            }
        }
        // the pool of light round the creature (in the dark): four rings
        if (s_pool_k > 0.01f) {
            float dy = y + 0.5f - s_pool_y;
            for (int j = 0; j < 4 && ne < NEDGE - 2; j++) {
                float R = s_pool_r * (1.0f - j * 0.22f);
                if (fabsf(dy) >= R) {
                    continue;
                }
                int hw = (int)sqrtf(R * R - dy * dy);
                float L = s_pool_k * 0.30f;
                int r = (int)(Z->w0[0] * L + 4 * L), g = (int)(Z->w0[1] * 1.6f * L + 6 * L),
                    b = (int)(Z->w0[2] * 1.8f * L + 8 * L);
                e[ne++] = (edge_t){(int16_t)(s_pool_x - hw), (int16_t)r, (int16_t)g, (int16_t)b};
                e[ne++] = (edge_t){(int16_t)(s_pool_x + hw), (int16_t)-r, (int16_t)-g, (int16_t)-b};
            }
        }
        // (insertion sort of the edges by x)
        for (int i = 1; i < ne; i++) {
            for (int j = i; j > 0 && e[j - 1].x > e[j].x; j--) {
                edge_t t = e[j];
                e[j] = e[j - 1];
                e[j - 1] = t;
            }
        }
        // the cuts: the edges and the vignette steps; the sweep adds the light of the edges that start before each piece
        static int cuts[NEDGE + 20];
        int nc = 0;
        for (int i = 0; i < NV; i++) {
            cuts[nc++] = VX[i] * W / SW;
            cuts[nc++] = W - VX[i] * W / SW;
        }
        for (int i = 0; i < ne; i++) {
            if (e[i].x > 0 && e[i].x < W) {
                cuts[nc++] = e[i].x;
            }
        }
        cuts[nc++] = W;
        for (int i = 1; i < nc; i++) {
            for (int j = i; j > 0 && cuts[j - 1] > cuts[j]; j--) {
                int t = cuts[j];
                cuts[j] = cuts[j - 1];
                cuts[j - 1] = t;
            }
        }
        uint16_t *row = g_fb + y * W;
        const uint8_t *dr = BAYER4[y & 3];
        int x = 0, ei = 0, ar = 0, ag = 0, ab = 0;
        while (ei < ne && e[ei].x <= 0) {
            ar += e[ei].r;
            ag += e[ei].g;
            ab += e[ei].b;
            ei++;
        }
        for (int ci = 0; ci < nc && x < W; ci++) {
            int xb = cuts[ci];
            if (xb <= x) {
                continue;
            }
            if (xb > W) {
                xb = W;
            }
            // the vignette of the piece (the step that holds its middle)
            int xm = (x + xb) / 2, dmin = xm < W - xm ? xm : W - xm;
            float vk = 1.0f;
            for (int i = NV - 1; i >= 0; i--) {
                if (dmin >= VX[i] * W / SW) {
                    vk = i == NV - 1 ? 1.0f : VK[i];
                    break;
                }
            }
            float s = s_flash;
            int R = (int)((base[0] + ar) * vk + s * 160), G = (int)((base[1] + ag) * vk + s * 200),
                B = (int)((base[2] + ab) * vk + s * 220);
            uint16_t p0 = pack_d(R, G, B, dr[0]), p1 = pack_d(R, G, B, dr[1]), p2 = pack_d(R, G, B, dr[2]),
                     p3 = pack_d(R, G, B, dr[3]);
            uint16_t pat[4] = {p0, p1, p2, p3};
            // (two pixels in each store: the frame is in PSRAM, and a store costs more than its work)
            int i = x;
            if ((i & 1) && i < xb) {
                row[i] = pat[i & 3];
                i++;
            }
            const uint32_t w0 = (uint32_t)pat[0] | (uint32_t)pat[1] << 16,
                           w1 = (uint32_t)pat[2] | (uint32_t)pat[3] << 16;
            uint32_t *r32 = (uint32_t *)(row + i);
            for (; i + 1 < xb; i += 2) {
                *r32++ = (i & 2) ? w1 : w0;
            }
            if (i < xb) {
                row[i] = pat[i & 3];
            }
            x = xb;
            while (ei < ne && e[ei].x <= x) {
                ar += e[ei].r;
                ag += e[ei].g;
                ab += e[ei].b;
                ei++;
            }
        }
    }
}

// the surface of the shallows: a bright line that waves
static void draw_surface(void)
{
    if (s_zone != 0) {
        return;
    }
    float y0 = sy_of(0);
    if (y0 < -4 || y0 > s_fh + 4) {
        return;
    }
    for (int x = 0; x < s_fw; x += 1) {
        float wx = C.x + (x - scx()) / C.z;
        float w = sinf(wx * 0.05f + s_time * 1.3f) * 1.6f + sinf(wx * 0.13f - s_time * 2.1f) * 0.8f;
        float yy = y0 + w * C.z * 0.6f;
        dot_add((float)x + 0.5f, yy, 150, 200, 210);
        dot_add((float)x + 0.5f, yy + 1.5f, 40, 90, 110);
    }
}

// ================================================================ the far layers
static int8_t s_floor[512];  // the far floor (px over its base line), it wraps
static int s_top[SW + 4];    // the top row of a floor in each column (a work row: not on the stack)
static void far_make(void)
{
    for (int i = 0; i < 512; i++) {
        s_floor[i] = (int8_t)(fabsf(fbm1(i * 0.024f, 31 + s_zone)) * 26);
    }
}
// the x of the far layer (parallax p) for the screen x
static inline float far_off(float p)
{
    return C.x * p;
}
static void draw_whale(void)
{
    if (!(Z->far & FAR_WHALE)) {
        return;
    }
    // a whale far away: only a darker shape (the look page). It swims slowly across the far layer.
    static const float WP[][2] = {{500, 50}, {552, 47}, {580, 47}, {610, 48}, {650, 52}, {690, 60}, {704, 62},
                                  {712, 58}, {724, 50}, {722, 60}, {734, 66}, {716, 64}, {700, 68}, {684, 66},
                                  {650, 71}, {600, 74}, {552, 72}, {504, 70}, {496, 62}};
    const int NWP = (int)(sizeof WP / sizeof WP[0]);
    float off = fmodf(s_time * 3.0f + 400 - far_off(0.25f), 1600.0f);
    if (off < 0) {
        off += 1600;
    }
    float ox = off - 700, oy = 8 - (C.y - Z->h * 0.3f) * 0.12f;
    float pts[2 * 24];
    bool vis = false;
    for (int i = 0; i < NWP; i++) {
        pts[2 * i] = (WP[i][0] - 500 + ox) * s_fw / SW;
        pts[2 * i + 1] = WP[i][1] + oy;
        if (pts[2 * i] > -10 && pts[2 * i] < s_fw + 10) {
            vis = true;
        }
    }
    if (!vis) {
        return;
    }
    int k = s_zone == 0 ? 60 : 107;
    poly_mix(pts, NWP, 3 * k / 107, 16 * k / 107, 32 * k / 107, s_zone == 0 ? 80 : 108);
    float fin[6] = {(560 - 500 + ox) * s_fw / SW, 70 + oy, (576 - 500 + ox) * s_fw / SW, 80 + oy,
                    (582 - 500 + ox) * s_fw / SW, 71 + oy};
    poly_mix(fin, 3, 3, 16, 32, s_zone == 0 ? 80 : 108);
}
static void draw_far_floor(void)
{
    if (!(Z->far & FAR_FLOOR)) {
        return;
    }
    // the base line of the far floor: it shows at the bottom of the screen when the camera is in the lower part of
    // the zone (a far parallax in y)
    float base = s_fh + 26 - (C.y / Z->h) * 46;
    if (base - 30 > s_fh) {
        return;
    }
    int *top = s_top, ymin = s_fh;
    float off = far_off(0.25f);
    int W = s_fw < SW ? s_fw : SW;
    for (int x = 0; x < W; x++) {
        int fi = ((int)(x + off) % 1024 + 1024) % 1024;
        int h = s_floor[fi >> 1];
        top[x] = (int)base - h;
        if (top[x] < ymin) {
            ymin = top[x];
        }
    }
    if (ymin < 0) {
        ymin = 0;
    }
    float c[3];
    water_colour(1, c);
    int fr = (int)(c[0] * 0.45f), fg = (int)(c[1] * 0.55f + 3), fb = (int)(c[2] * 0.62f + 6);
    for (int y = ymin; y < s_fh; y++) {
        uint16_t *row = g_fb + y * s_fw;
        const uint8_t *dr = BAYER4[y & 3];
        uint16_t pat[4];
        float k = 1 - clampf((y - base + 30) / 60.0f, 0, 0.35f);
        for (int i = 0; i < 4; i++) {
            pat[i] = pack_d((int)(fr * k), (int)(fg * k), (int)(fb * k), dr[i]);
        }
        for (int x = 0; x < W; x++) {
            if (y >= top[x]) {
                row[x] = pat[x & 3];
            }
        }
    }
    // the sea pens and the glass sponges of the far floor (thin stalks with a soft head)
    for (int i = 0; i < 30; i++) {
        float px = (float)(hash2(i, 77 + s_zone) % 1024);
        float sx = fmodf(px - off + 2048, 1024.0f);
        if (sx > W + 8) {
            continue;
        }
        int fi = ((int)px % 1024 + 1024) % 1024;
        float by = base - s_floor[fi >> 1] + 1, h = 10 + (hash2(i, 5) % 26);
        float sw = sinf(s_time * 0.8f + i) * 1.5f;
        line_mix(sx, by, sx + sw, by - h, 40 + s_zone * 4, 90, 110, 110);
        if (i & 1) {
            ell_add(sx + sw, by - h, 0, 1, 4, 2.5f, 90, 160, 170, 0.25f);
        }
    }
}
// far sparks: the light of life far away in the dark (they blink)
static void draw_sparks(void)
{
    if (!(Z->far & FAR_SPARKS)) {
        return;
    }
    float off = far_off(0.35f);
    for (int i = 0; i < 26; i++) {
        uint32_t h = hash2(i, 991 + s_zone);
        float per = 3 + (h % 5), ph = fmodf(s_time + (h % 97) * 0.13f, per) / per;
        if (ph > 0.22f) {
            continue;
        }
        float a = sinf(ph / 0.22f * 3.14159f);
        float x = fmodf((float)(h % 1400) - off + 2800, 1400.0f) * s_fw / 1400 * 2.2f - 200;
        float y = (float)((h >> 12) % 150) + 8;
        static const uint8_t SC[3][3] = {{90, 200, 255}, {157, 255, 140}, {255, 120, 210}};
        const uint8_t *c = SC[h % 3];
        glow(x, y, 4.5f, c[0], c[1], c[2], 0.5f * a);
        px_add((int)x, (int)y, (int)(200 * a), (int)(240 * a), (int)(255 * a));
    }
}
// the comb jellies of the middle layer (the look page): their rows of hairs flash in rainbow colours
static void draw_combs(void)
{
    if (s_zone == 0) {
        return;
    }
    float off = far_off(0.6f);
    for (int i = 0; i < 5; i++) {
        uint32_t h = hash2(i, 515 + s_zone);
        float ph = (h % 628) / 100.0f, s = 2.2f + (h % 20) / 10.0f;
        float x = fmodf((float)(h % 1200) - off + 2400, 1200.0f) * s_fw / 1200 * 1.5f - 80 +
                  sinf(s_time * 0.3f + ph) * 12;
        float y = 30 + (float)((h >> 10) % 100) + cosf(s_time * 0.25f + ph) * 6 - (C.y / Z->h - 0.5f) * 30;
        if (x < -10 || x > s_fw + 10) {
            continue;
        }
        ell_add(x, y, 0, 1, s * 2.2f, s * 1.5f, 160, 220, 255, 0.12f);
        for (int r = 0; r < 4; r++) {
            int hue = (int)(s_time * 140 + r * 60 + ph * 50) % 360;
            uint16_t c = hsv(hue, 255, 255);
            int cr, cg, cb;
            unpack8(c, &cr, &cg, &cb);
            float bx = x - s * 1.2f + r * s * 0.8f, by = y - s * 1.6f + fmodf(s_time * 8 + r, 3);
            for (int k = 0; k < (int)(s * 1.6f); k++) {
                px_add((int)bx, (int)by + k, cr * 3 / 4, cg * 3 / 4, cb * 3 / 4);
            }
        }
    }
}

// ================================================================ the floor of the abyss (zone 4, in the world)
static float floor_y(float x)
{
    return Z->h - 14 - fabsf(fbm1(x * 0.01f, 404)) * 30;
}
static void draw_abyss_floor(void)
{
    if (!(Z->far & FAR_VENTS)) {
        return;
    }
    float y0 = sy_of(Z->h - 50);
    if (y0 > s_fh) {
        return;
    }
    int W = s_fw;
    int *top = s_top, ymin = s_fh;
    for (int x = 0; x < W && x < SW + 4; x++) {
        float wx = C.x + (x - scx()) / C.z;
        top[x] = (int)sy_of(floor_y(wx));
        if (top[x] < ymin) {
            ymin = top[x];
        }
    }
    if (ymin < 0) {
        ymin = 0;
    }
    for (int y = ymin; y < s_fh; y++) {
        uint16_t *row = g_fb + y * s_fw;
        const uint8_t *dr = BAYER4[y & 3];
        uint16_t pat[4];
        for (int i = 0; i < 4; i++) {
            pat[i] = pack_d(10, 13, 18, dr[i]);
        }
        for (int x = 0; x < W && x < SW + 4; x++) {
            if (y >= top[x]) {
                row[x] = pat[x & 3];
            }
        }
    }
    // the vents: black smokers with a hot glow, smoke and tube worms (the look page)
    for (int v = 0; v < 4; v++) {
        float vxw = 260 + v * 512.0f, vsx, vsy;
        if (!on_screen(vxw, floor_y(vxw), 120, &vsx, &vsy)) {
            continue;
        }
        float z = C.z;
        float pts[12] = {vsx - 14 * z, vsy + 6 * z,  vsx - 7 * z, vsy - 22 * z, vsx - 3 * z,  vsy - 30 * z,
                         vsx + 3 * z,  vsy - 30 * z, vsx + 8 * z, vsy - 18 * z, vsx + 16 * z, vsy + 6 * z};
        poly_mix(pts, 6, 11, 18, 24, 256);
        glow(vsx, vsy - 28 * z, 16 * z, 255, 122, 42, 0.25f + 0.08f * sinf(s_time * 3 + v));
        for (int k = 0; k < 7; k++) {
            float t = fmodf(s_time * 0.25f + k / 7.0f, 1.0f), r = (3 + t * 12) * z;
            ell_mix(vsx + (sinf(t * 5 + s_time) * 4 + t * 8) * z, vsy - (32 + t * 60) * z, 1, 0, r, r * 0.8f, 10, 14,
                    20, 0.55f * (1 - t));
        }
        for (int k = 0; k < 9; k++) {
            float bx = vsx + (-34 + k * 4.5f) * z, hh = (10 + (k * 7) % 9) * z,
                  swy = sinf(s_time * 1.2f + k) * 1.2f * z;
            line_mix(bx, vsy + 4 * z, bx + swy, vsy + 4 * z - hh, 217, 210, 196, 230);
            ell_mix(bx + swy, vsy + 2 * z - hh, 0, 1, 2.4f * z, 1.8f * z, 255, 59, 74, 0.95f);
        }
    }
}

// ================================================================ the marine snow (3 layers, in screen space)
typedef struct {
    float x, y, v, ph;
    uint8_t k;
} snow_t;
#define NSNOW 134
static snow_t s_snow[NSNOW];
static float s_snow_cx, s_snow_cy;  // the camera at the last step (the layers move with it)
static void snow_make(void)
{
    for (int i = 0; i < NSNOW; i++) {
        snow_t *s = &s_snow[i];
        s->k = i < 70 ? 0 : i < 116 ? 1 : 2;
        s->x = rndf() * (SW + 40) - 20;
        s->y = rndf() * SH;
        static const float V[3] = {3, 5, 8};
        s->v = V[s->k] * (0.6f + rndf() * 0.8f);
        s->ph = rndf() * 6.283f;
    }
    s_snow_cx = C.x;
    s_snow_cy = C.y;
}
static void snow_step(float dt)
{
    static const float P[3] = {0.55f, 1.0f, 1.35f};
    float dcx = wrapd(C.x - s_snow_cx), dcy = C.y - s_snow_cy;
    s_snow_cx = C.x;
    s_snow_cy = C.y;
    for (int i = 0; i < NSNOW; i++) {
        snow_t *s = &s_snow[i];
        s->y += s->v * dt - dcy * C.z * P[s->k];
        s->x += sinf(s_time * 0.4f + s->ph) * 0.05f - 1.2f * dt * (s->k + 1) + Z->flow * dt * 0.3f -
                dcx * C.z * P[s->k];
        if (s->y > SH + 4) {
            s->y -= SH + 8;
        }
        if (s->y < -4) {
            s->y += SH + 8;
        }
        if (s->x < -20) {
            s->x += SW + 40;
        }
        if (s->x > SW + 20) {
            s->x -= SW + 40;
        }
    }
}
static void draw_snow(int layer)
{
    float k = 1 - Z->dark * 0.45f;
    for (int i = 0; i < NSNOW; i++) {
        snow_t *s = &s_snow[i];
        if ((layer == 2) != (s->k == 2)) {
            continue;
        }
        float x = s->x * s_fw / SW, y = s->y * s_fh / SH;
        if (s->k == 0) {
            dot_add(x, y, (int)(50 * k), (int)(58 * k), (int)(60 * k));
        } else if (s->k == 1) {
            dot_add(x, y, (int)(90 * k), (int)(104 * k), (int)(108 * k));
            dot_add(x + 0.6f, y + 0.4f, (int)(30 * k), (int)(34 * k), (int)(36 * k));
        } else {
            ell_add(x, y, 1, 0, 2.2f, 2.2f, 200, 230, 240, 0.16f * k);
        }
    }
}

// ================================================================ particles (the trail, bursts, ink, bubbles, genes)
enum {
    PK_TRAIL,
    PK_BURST,
    PK_INK,
    PK_BUBBLE,
    PK_GENE,
    PK_SPARK,
    PK_BLOOD
};
typedef struct {
    float x, y, vx, vy, life, max, sz;
    uint8_t kind, c[3];
} part_t;
#define NPART 300
static part_t s_part[NPART];
static int s_npart;
static part_t *part_new(int kind, float x, float y, float vx, float vy, float max, float sz, const uint8_t *c)
{
    if (s_npart >= NPART) {
        // (full: the oldest trail light makes room)
        int j = -1;
        for (int i = 0; i < s_npart; i++) {
            if (s_part[i].kind == PK_TRAIL) {
                j = i;
                break;
            }
        }
        if (j < 0) {
            return NULL;
        }
        s_part[j] = s_part[--s_npart];
    }
    part_t *p = &s_part[s_npart++];
    p->kind = (uint8_t)kind;
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->life = 0;
    p->max = max;
    p->sz = sz;
    p->c[0] = c[0];
    p->c[1] = c[1];
    p->c[2] = c[2];
    return p;
}
static void burst(float x, float y, int n, float sp, const uint8_t *c, int kind)
{
    for (int i = 0; i < n; i++) {
        float a = rndf() * 6.2831853f, v = sp * (0.3f + rndf() * 0.7f);
        part_new(kind, x, y, cosf(a) * v, sinf(a) * v, 0.6f + rndf() * 0.7f, 1, c);
    }
}
static void parts_step(float dt)
{
    for (int i = s_npart - 1; i >= 0; i--) {
        part_t *p = &s_part[i];
        p->life += dt;
        if (p->life >= p->max) {
            s_part[i] = s_part[--s_npart];
            continue;
        }
        float drag = p->kind == PK_TRAIL ? 1.5f : p->kind == PK_INK ? 1.2f : 2.0f;
        float k = expf(-drag * dt);
        p->vx *= k;
        p->vy *= k;
        if (p->kind == PK_BUBBLE) {
            p->vy -= 30 * dt;
        }
        p->x = wrapx(p->x + (p->vx + Z->flow * 0.5f) * dt);
        p->y += p->vy * dt;
    }
}
static void draw_parts(bool front)
{
    float z = C.z;
    for (int i = 0; i < s_npart; i++) {
        part_t *p = &s_part[i];
        bool ink = p->kind == PK_INK;
        if (ink != front) {
            continue;
        }
        float sx, sy;
        if (!on_screen(p->x, p->y, 30, &sx, &sy)) {
            continue;
        }
        float t = p->life / p->max;
        switch (p->kind) {
        case PK_TRAIL:
            glow(sx, sy, (2.5f + t * 3) * z, p->c[0], p->c[1], p->c[2], 0.55f * (1 - t));
            break;
        case PK_BURST:
            glow(sx, sy, 3 * z, 232, 251, 255, 0.8f * (1 - t));
            break;
        case PK_SPARK:
            dot_add(sx, sy, (int)(p->c[0] * (1 - t)), (int)(p->c[1] * (1 - t)), (int)(p->c[2] * (1 - t)));
            break;
        case PK_GENE:
            glow(sx, sy, 3.5f * z, p->c[0], p->c[1], p->c[2], 0.9f * (1 - t * 0.5f));
            break;
        case PK_BLOOD:
            glow(sx, sy, (2 + t * 4) * z, p->c[0], p->c[1], p->c[2], 0.6f * (1 - t));
            break;
        case PK_BUBBLE: {
            float r = (1.2f + p->sz) * z;
            dot_add(sx - r * 0.4f, sy - r * 0.4f, 120, 150, 160);
            for (int k = 0; k < 8; k++) {
                float a = k * 0.785f;
                dot_add(sx + cosf(a) * r, sy + sinf(a) * r, 40, 55, 60);
            }
            break;
        }
        case PK_INK: {
            // a cloud of glowing ink: a dark body with a red light in it
            float r = (6 + t * 16) * p->sz * z, a = (1 - t) * (1 - t);
            disc_mix(sx, sy, r, 18, 4, 14, 0.70f * a);
            glow(sx, sy, r * 0.8f, p->c[0], p->c[1], p->c[2], 0.30f * a);
            break;
        }
        }
    }
}

// ================================================================ plankton (food with no trait: clouds of specks)
typedef struct {
    float x, y, ph;
    uint8_t on;
} plank_t;
#define NPLANK 200
static plank_t s_plank[NPLANK];
static int plank_count(void)
{
    int n = 0;
    for (int i = 0; i < NPLANK; i++) {
        n += s_plank[i].on;
    }
    return n;
}
static void plank_cloud(float cx, float cy, int n)
{
    for (int i = 0; i < NPLANK && n > 0; i++) {
        if (s_plank[i].on) {
            continue;
        }
        float a = rndf() * 6.283f, r = sqrtf(rndf()) * 34;
        s_plank[i].x = wrapx(cx + cosf(a) * r * 1.6f);
        s_plank[i].y = clampf(cy + sinf(a) * r, 10, Z->h - 10);
        s_plank[i].ph = rndf() * 6.283f;
        s_plank[i].on = 1;
        n--;
    }
}
static void plank_step(float dt)
{
    for (int i = 0; i < NPLANK; i++) {
        plank_t *p = &s_plank[i];
        if (!p->on) {
            continue;
        }
        p->x = wrapx(p->x + (Z->flow * 0.7f + sinf(s_time * 0.7f + p->ph) * 3) * dt);
        p->y += cosf(s_time * 0.5f + p->ph * 1.3f) * 2.5f * dt;
    }
}
static void draw_plank(void)
{
    float z = C.z;
    for (int i = 0; i < NPLANK; i++) {
        plank_t *p = &s_plank[i];
        if (!p->on) {
            continue;
        }
        float sx, sy;
        if (!on_screen(p->x, p->y, 4, &sx, &sy)) {
            continue;
        }
        float tw = 0.6f + 0.4f * sinf(s_time * 3 + p->ph);
        dot_add(sx, sy, (int)(170 * tw), (int)(220 * tw), (int)(200 * tw));
        if (z > 1.4f) {
            dot_add(sx + 0.8f, sy, (int)(60 * tw), (int)(90 * tw), (int)(80 * tw));
        }
    }
}

// ================================================================ the deep current (a column of light that goes down)
static void draw_current(void)
{
    if (s_cur_open < 0.01f) {
        return;
    }
    float y0 = Z->h * 0.5f, y1 = Z->h + 30;
    float sx, sy;
    on_screen(s_cur_x, y0, 2000, &sx, &sy);
    if (sx < -120 || sx > s_fw + 120) {
        return;
    }
    float z = C.z, o = s_cur_open;
    // the glow of the column, and a spiral of lights that goes down in it
    for (float wy = y0; wy < y1; wy += 26) {
        float k = clampf((wy - y0) / 60.0f, 0, 1);
        glow(sx, sy_of(wy), 30 * z, 60, 170, 255, 0.10f * o * k);
    }
    for (int i = 0; i < 46; i++) {
        float t = fmodf(s_time * 0.35f + i / 46.0f, 1.0f);
        float wy = y0 + t * (y1 - y0), a = s_time * 2.2f + i * 0.9f;
        float r = (12 + 10 * t) * z, px = sx + cosf(a) * r, py = sy_of(wy) + sinf(a) * r * 0.25f;
        float lum = o * sinf(t * 3.14159f) * (0.5f + 0.5f * (sinf(a) > 0));
        glow(px, py, 3.2f * z, 120, 230, 255, 0.7f * lum);
    }
}
