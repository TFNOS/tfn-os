// THE TURNING WOOD: the gentle animation of each place.
// The spots come from the design (places.py AIR and ANIMS -> TW_AIR, TW_ANIMS in tw_game.inc), in the coordinates of
// the picture. Each frame draws over the copy of the picture in g_fb: the water ripples (only on its blue pixels), the
// falls run, fire, candles, smoke, lights that breathe, dust, glow worms, stars; in the open air the particles of the
// season (petals, seeds and butterflies, leaves, snow) and birds. Included by app_wood.c (after the view and s_pic).

#define AN_NP 44  // the particles of the air
typedef struct {
    float x, y, vx, vy, ph;
    uint8_t kind, c;
} an_part_t;
static an_part_t s_an_p[AN_NP];
static int s_an_np;
static float s_an_t;
static uint32_t s_an_rnd = 12345;
#define AN_NSTAR 48
static int16_t s_an_star[AN_NSTAR][2];
static int s_an_nstar;
static float s_an_bird_t;  // the time to the next flock
static float s_an_bird_x;  // the place of the flock (picture x), < -50: no flock
static float s_an_bird_y, s_an_bird_v;

static float an_rand(void)
{
    s_an_rnd = s_an_rnd * 1664525u + 1013904223u;
    return (s_an_rnd >> 8) * (1.0f / 16777216.0f);
}
static inline uint16_t an_sw(uint16_t p)
{
    return (uint16_t)(p >> 8 | p << 8);
}
static inline bool an_blue(uint16_t p)
{
    const uint16_t v = an_sw(p);
    const int r = v >> 11, g = (v >> 5) & 63, b = v & 31;
    return b >= r && b * 2 + 6 >= g && r + g / 2 + b < 70;
}
static inline bool an_light(uint16_t p)
{
    const uint16_t v = an_sw(p);
    return (v >> 11) + ((v >> 5) & 63) / 2 + (v & 31) > 52;
}
static inline uint16_t an_col(uint32_t c)
{
    return rgb((c >> 16) & 255, (c >> 8) & 255, c & 255);
}
// one pixel of the frame from a point of the picture (no draw outside the screen)
static inline void an_px(int px, int py, uint16_t c)
{
    const int x = px - s_vx, y = py - s_vy;
    if (x >= 0 && y >= 0 && x < SW && y < SH) {
        g_fb[y * SW + x] = c;
    }
}
static inline void an_blend(int px, int py, uint16_t c, int a)
{
    const int x = px - s_vx, y = py - s_vy;
    if (x < 0 || y < 0 || x >= SW || y >= SH) {
        return;
    }
    const uint16_t d = an_sw(g_fb[y * SW + x]), s = an_sw(c);
    const int r = ((d >> 11) * (256 - a) + (s >> 11) * a) >> 8;
    const int g = (((d >> 5) & 63) * (256 - a) + ((s >> 5) & 63) * a) >> 8;
    const int b = ((d & 31) * (256 - a) + (s & 31) * a) >> 8;
    g_fb[y * SW + x] = an_sw((uint16_t)(r << 11 | g << 5 | b));
}

// (floorf is a slow library call in an app file)
static inline int an_floor(float v)
{
    const int i = (int)v;
    return i - (v < (float)i);
}
// a dot at a place of the picture with a fraction: its colour goes to the 4 pixels round it by weight (smooth motion)
static void an_dot(float px, float py, uint16_t c, int a)
{
    const int x = an_floor(px), y = an_floor(py);
    const float fx = px - x, fy = py - y;
    an_blend(x, y, c, (int)(a * (1 - fx) * (1 - fy)));
    an_blend(x + 1, y, c, (int)(a * fx * (1 - fy)));
    an_blend(x, y + 1, c, (int)(a * (1 - fx) * fy));
    an_blend(x + 1, y + 1, c, (int)(a * fx * fy));
}
static inline uint16_t an_mix(uint16_t p, uint16_t q, int w)  // (w/256 of q; the byte order of the frame)
{
    const uint16_t a = an_sw(p), b = an_sw(q);
    const int r = ((a >> 11) * (256 - w) + (b >> 11) * w) >> 8;
    const int g = (((a >> 5) & 63) * (256 - w) + ((b >> 5) & 63) * w) >> 8;
    const int bl = ((a & 31) * (256 - w) + (b & 31) * w) >> 8;
    return an_sw((uint16_t)(r << 11 | g << 5 | bl));
}

// the particles of the air for the season (at the entry of a place and after a change of the season)
static void an_part_new(an_part_t *p, bool top)
{
    const int se = T.season;
    p->x = an_rand() * PW;
    p->y = top ? -4 - an_rand() * 30 : an_rand() * PH;
    p->ph = an_rand() * 6.3f;
    p->c = (uint8_t)(an_rand() * 4);
    if (se == 3) {
        p->kind = 0;
        p->vx = 4 - an_rand() * 8;
        p->vy = 10 + an_rand() * 12;
    }  // snow
    else if (se == 2) {
        p->kind = 1;
        p->vx = 6 + an_rand() * 10;
        p->vy = 12 + an_rand() * 10;
    }  // leaves
    else if (se == 0) {
        p->kind = 2;
        p->vx = 10 + an_rand() * 8;
        p->vy = 6 + an_rand() * 6;
    }  // petals
    else {
        p->kind = 3;
        p->vx = 4 + an_rand() * 6;
        p->vy = -2 - an_rand() * 3;
        if (top) {
            p->y = PH * (0.3f + 0.7f * an_rand());
        }
    }  // seeds
}
static void anim_enter(void)
{
    s_an_np = 0;
    if (TW_AIR[T.pos] == AIR_OUT) {
        s_an_np = T.season == 3 ? AN_NP : T.season == 1 ? 14 : T.season == 2 ? 24 : 20;
    } else if (TW_AIR[T.pos] == AIR_NIGHT && T.season == 3) {
        s_an_np = AN_NP;
    }
    for (int i = 0; i < s_an_np; i++) {
        an_part_new(&s_an_p[i], false);
    }
    s_an_bird_x = -100;
    s_an_bird_t = 4 + an_rand() * 8;
    // the stars: the light pixels with dark pixels at the sides, in the boxes of TWINKLE
    s_an_nstar = 0;
    for (int i = 0; i < TW_NANIM; i++) {
        const tw_anim_t *A = &TW_ANIMS[i];
        if (A->place != T.pos || A->kind != TA_TWINKLE) {
            continue;
        }
        for (int y = A->b > 2 ? A->b : 2; y < (A->d < PH - 2 ? A->d : PH - 2) && s_an_nstar < AN_NSTAR; y += 1) {
            for (int x = A->a + 2; x < A->c - 2 && s_an_nstar < AN_NSTAR; x += 1) {
                const uint16_t *p = s_pic + y * PW + x;
                if (an_light(p[0]) && !an_light(p[-2]) && !an_light(p[2]) && !an_light(p[-2 * PW]) &&
                    !an_light(p[2 * PW])) {
                    s_an_star[s_an_nstar][0] = (int16_t)x;
                    s_an_star[s_an_nstar][1] = (int16_t)y;
                    s_an_nstar++;
                    x += 6;
                }
            }
        }
    }
}
static void anim_step(float dt)
{
    s_an_t += dt;
    for (int i = 0; i < s_an_np; i++) {
        an_part_t *p = &s_an_p[i];
        const float sway = fsin(s_an_t * (p->kind == 1 ? 1.6f : 1.1f) + p->ph);
        p->x += (p->vx + sway * (p->kind == 1 ? 14 : 6)) * dt;
        p->y += (p->vy + (p->kind == 1 ? fabsf(sway) * 6 : 0)) * dt;
        if (p->y > PH + 6 || p->x > PW + 10 || p->x < -10 || p->y < -40) {
            an_part_new(p, true);
        }
        if (p->kind == 3 && p->y < 10) {
            an_part_new(p, true);
        }
    }
    // the birds: a small flock crosses the sky now and then (not at night, not in a room)
    if (TW_AIR[T.pos] == AIR_OUT && T.season != 3) {
        if (s_an_bird_x < -60) {
            if ((s_an_bird_t -= dt) <= 0) {
                s_an_bird_x = -40;
                s_an_bird_y = 16 + an_rand() * 40;
                s_an_bird_v = 26 + an_rand() * 16;
            }
        } else if ((s_an_bird_x += s_an_bird_v * dt) > PW + 40) {
            s_an_bird_x = -100;
            s_an_bird_t = 10 + an_rand() * 14;
        }
    }
}
static void an_water(const tw_anim_t *A)
{
    const int x0 = A->a > s_vx ? A->a : s_vx, x1 = A->c < s_vx + SW ? A->c : s_vx + SW;
    const int y0 = A->b > s_vy ? A->b : s_vy, y1 = A->d < s_vy + SH ? A->d : s_vy + SH;
    const float h = (float)(A->d - A->b);
    for (int y = y0; y < y1; y++) {
        const float q = (y - A->b) / h;  // (bigger ripples nearer)
        const float off = fsin(s_an_t * 1.7f + y * 0.55f) * (0.6f + 1.6f * q) + 0.5f * fsin(s_an_t * 0.9f + y * 0.13f);
        const int o0 = an_floor(off), w = (int)((off - o0) * 256);
        if (!o0 && w < 8) {
            continue;
        }
        const uint16_t *src = s_pic + y * PW;
        uint16_t *dst = g_fb + (y - s_vy) * SW - s_vx;
        for (int x = x0; x < x1; x++) {
            const int sx = x + o0;
            if (sx < 0 || sx + 1 >= PW) {
                continue;
            }
            if (an_blue(src[x]) && an_blue(src[sx]) && an_blue(src[sx + 1])) {
                dst[x] = an_mix(src[sx], src[sx + 1], w);
            }
        }
    }
    // the glints: a few light points on the water that come and go
    for (int k = 0; k < 10; k++) {
        const float ph = k * 2.39f;
        const float life = fsin(s_an_t * 1.3f + ph);
        if (life < 0.55f) {
            continue;
        }
        const int gx = A->a + (int)((A->c - A->a) * (0.5f + 0.5f * fsin(ph * 3.1f + (int)(s_an_t * 0.2f + k) * 1.7f)));
        const int gy = A->b + (int)((A->d - A->b) * (0.5f + 0.5f * fsin(ph * 5.3f + (int)(s_an_t * 0.2f + k) * 2.9f)));
        if (gx < 1 || gx >= PW - 1 || gy < 0 || gy >= PH || !an_blue(s_pic[gy * PW + gx])) {
            continue;
        }
        const int a = (int)((life - 0.55f) * 500);
        an_blend(gx, gy, rgb(255, 255, 255), a > 220 ? 220 : a);
        an_blend(gx - 1, gy, rgb(255, 255, 255), a / 3);
        an_blend(gx + 1, gy, rgb(255, 255, 255), a / 3);
    }
}
static void an_fall(const tw_anim_t *A)
{
    const int h = A->d - A->b, sh = (int)(s_an_t * 70) % h;
    const int x0 = A->a > s_vx ? A->a : s_vx, x1 = A->c < s_vx + SW ? A->c : s_vx + SW;
    const int y0 = A->b > s_vy ? A->b : s_vy, y1 = A->d < s_vy + SH ? A->d : s_vy + SH;
    for (int y = y0; y < y1; y++) {
        int sy = A->b + ((y - A->b - sh) % h + h) % h;
        const uint16_t *src = s_pic + sy * PW, *here = s_pic + y * PW;
        uint16_t *dst = g_fb + (y - s_vy) * SW - s_vx;
        for (int x = x0; x < x1; x++) {
            if (an_light(here[x]) && an_light(src[x])) {
                dst[x] = src[x];
            }
        }
    }
}
static void an_fire(int x, int y, int r)
{
    const float f = 0.5f + 0.25f * fsin(s_an_t * 9.0f) + 0.25f * fsin(s_an_t * 13.7f + 1.3f);
    ui_circle(x - s_vx, y - s_vy, (int)(r * (1.1f + 0.15f * f)), rgb(255, 130, 40), (int)(26 + 26 * f));
    ui_circle(x - s_vx, y + 2 - s_vy, (int)(r * 0.45f), rgb(255, 210, 120), (int)(30 + 40 * f));
    for (int k = 0; k < 7; k++) {  // (the sparks)
        const float a = s_an_t * (0.55f + k * 0.07f) + k * 0.37f;
        const float u = a - (int)a;
        an_dot(x + fsin(k * 2.1f + a * 3) * (6 + u * 10), y - u * r * 2.2f, rgb(255, 200, 90), (int)(230 * (1 - u)));
    }
}
static void an_candle(int x, int y)
{
    const float f = 0.6f + 0.25f * fsin(s_an_t * 11.0f + x) + 0.15f * fsin(s_an_t * 17.0f + y);
    ui_circle(x - s_vx, y - s_vy, (int)(8 + 3 * f), rgb(255, 184, 92), (int)(30 + 34 * f));
    an_blend(x, y - 1, rgb(255, 244, 200), 230);
    an_blend(x, y - 2, rgb(255, 214, 120), (int)(120 + 100 * f));
}
static void an_smoke(int x, int y)
{
    for (int k = 0; k < 8; k++) {
        const float a = s_an_t * 0.16f + k / 8.0f;
        const float u = a - (int)a;
        const int px = x + (int)(fsin(u * 4 + k) * 4 + u * 26), py = y - (int)(u * 64);
        ui_circle(px - s_vx, py - s_vy, (int)(3 + u * 10), rgb(196, 190, 184),
                  (int)(70 * (1 - u) * (u < 0.1f ? u * 10 : 1)));
    }
}
static void an_motes(const tw_anim_t *A, bool flies)
{
    const int n = flies ? 12 : 16;
    const uint16_t c = flies ? an_col(A->col) : rgb(255, 241, 200);
    for (int k = 0; k < n; k++) {
        const float ph = k * 1.93f;
        const float fx = 0.5f + 0.45f * fsin(s_an_t * (0.05f + k * 0.006f) + ph) + 0.05f * fsin(s_an_t * 0.7f + ph * 2);
        const float fy =
                0.5f + 0.45f * fsin(s_an_t * (0.043f + k * 0.005f) + ph * 1.7f) + 0.05f * fsin(s_an_t * 0.6f + ph);
        const float x = A->a + (A->c - A->a) * fx, y = A->b + (A->d - A->b) * fy;
        const float tw = 0.5f + 0.5f * fsin(s_an_t * (flies ? 1.8f : 1.1f) + ph * 3);
        if (flies) {
            ui_circle((int)x - s_vx, (int)y - s_vy, 3, c, (int)(70 * tw));
            an_dot(x, y, c, (int)(120 + 130 * tw));
        } else {
            an_dot(x, y, c, (int)(60 + 140 * tw));
        }
    }
}
static void an_twinkle(void)
{
    for (int i = 0; i < s_an_nstar; i++) {
        const int x = s_an_star[i][0], y = s_an_star[i][1];
        const float tw = fsin(s_an_t * (0.8f + (i % 5) * 0.31f) + i * 1.7f);
        if (tw < -0.5f) {
            an_px(x, y, s_pic[y * PW + x - 2]);  // (dark for a moment)
        } else if (tw > 0.85f) {
            an_blend(x - 1, y, rgb(255, 255, 255), 120);
            an_blend(x + 1, y, rgb(255, 255, 255), 120);
            an_blend(x, y - 1, rgb(255, 255, 255), 120);
            an_blend(x, y + 1, rgb(255, 255, 255), 120);
        }
    }
}
static void an_air(void)
{
    static const uint32_t LEAF[4] = {0xd4581e, 0xe8a02a, 0xb8401a, 0xf0c040};
    static const uint32_t PETAL[4] = {0xffd0e0, 0xffffff, 0xffb0c8, 0xfff0f4};
    for (int i = 0; i < s_an_np; i++) {
        const an_part_t *p = &s_an_p[i];
        const float x = p->x, y = p->y;
        switch (p->kind) {
        case 0:  // snow
            an_dot(x, y, rgb(244, 248, 255), 235);
            if (i % 3 == 0) {
                an_dot(x + 1, y, rgb(244, 248, 255), 150);
                an_dot(x, y + 1, rgb(244, 248, 255), 150);
            }
            break;
        case 1: {  // a leaf that turns as it falls
            const uint16_t c = an_col(LEAF[p->c]);
            const float t = fsin(s_an_t * 3 + p->ph);
            an_dot(x, y, c, 255);
            if (t > 0) {
                an_dot(x + 1, y, c, 255);
                an_dot(x + 2, y + 1, c, 230);
                an_dot(x + 1, y + 1, c, 255);
            } else {
                an_dot(x, y + 1, c, 255);
                an_dot(x + 1, y + 2, c, 230);
            }
            break;
        }
        case 2: {  // a petal
            const uint16_t c = an_col(PETAL[p->c]);
            an_dot(x, y, c, 255);
            if (fsin(s_an_t * 2.4f + p->ph) > -0.3f) {
                an_dot(x + 1, y, c, 230);
            }
            break;
        }
        default:  // a seed of the summer, light and slow
            an_dot(x, y, rgb(255, 251, 232), 210);
            an_dot(x - 1, y, rgb(255, 251, 232), 80);
            an_dot(x + 1, y, rgb(255, 251, 232), 80);
            an_dot(x, y - 1, rgb(255, 251, 232), 80);
            break;
        }
    }
    if (TW_AIR[T.pos] == AIR_OUT && T.season == 1) {  // two butterflies in the summer
        for (int k = 0; k < 2; k++) {
            const float ph = k * 2.7f;
            const float bx = 120 + k * 300 + fsin(s_an_t * 0.23f + ph) * 110 + fsin(s_an_t * 0.9f + ph) * 20;
            const float by = 150 + fsin(s_an_t * 0.31f + ph * 2) * 30 + fsin(s_an_t * 1.3f + ph) * 8;
            const uint16_t c = k ? rgb(250, 250, 244) : rgb(240, 160, 32);
            const int w = fsin(s_an_t * 16 + ph) > 0 ? 2 : 1;
            for (int d = 1; d <= w; d++) {
                an_dot(bx - d, by - (d == 2), c, 255);
                an_dot(bx + d, by - (d == 2), c, 255);
            }
            an_dot(bx, by, rgb(40, 30, 20), 255);
        }
    }
    if (s_an_bird_x > -60) {  // the birds: small "v" shapes
        for (int k = 0; k < 3; k++) {
            const float bx = s_an_bird_x - k * 14 - (k & 1) * 4, by = s_an_bird_y + k * 5 + (k & 1) * 3;
            const int up = fsin(s_an_t * 9 + k) > 0;
            const uint16_t c = rgb(48, 40, 44);
            an_dot(bx, by, c, 255);
            an_dot(bx - 1, by - up, c, 255);
            an_dot(bx - 2, by - up * 2 + !up, c, 255);
            an_dot(bx + 1, by - up, c, 255);
            an_dot(bx + 2, by - up * 2 + !up, c, 255);
        }
    }
}
// all of the animation of the place now, over the picture in g_fb
static void anim_draw(void)
{
    const int bit = 1 << T.season;
    for (int i = 0; i < TW_NANIM; i++) {
        const tw_anim_t *A = &TW_ANIMS[i];
        if (A->place != T.pos || !(A->smask & bit)) {
            continue;
        }
        switch (A->kind) {
        case TA_WATER:
            an_water(A);
            break;
        case TA_FALL:
            an_fall(A);
            break;
        case TA_FIRE:
            an_fire(A->a, A->b, A->c);
            break;
        case TA_CANDLE:
            an_candle(A->a, A->b);
            break;
        case TA_SMOKE:
            an_smoke(A->a, A->b);
            break;
        case TA_GLOW:
            ui_circle(A->a - s_vx, A->b - s_vy, A->c, an_col(A->col),
                      (int)(26 + 22 * fsin(s_an_t * 1.2f + A->a * 0.01f)));
            break;
        case TA_MOTES:
            an_motes(A, false);
            break;
        case TA_FLIES:
            an_motes(A, true);
            break;
        case TA_TWINKLE:
            break;
        }
    }
    an_twinkle();
    an_air();
}
