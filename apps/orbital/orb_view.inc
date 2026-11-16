// ORBITAL: the view (included by app_orbital.c). The camera (a soft zoom from the whole planet down to a few metres,
// a slow breath of the zoom and the roll, a punch at the big moments, the shake), the sky (stars, the sun), the planet
// (a texture round the planet that turns with it, the day and the night, the city lights, the glow of the air), the
// orbits, the band of the goal, the ships (flat shapes with a lit side and a shade side, as the look page), the
// tether, the flame, the particles.
// The camera: the world point (s_cx, s_cy) is in the middle of the screen; s_cs px for each km; the world angle s_croll
// points up on the screen (the local vertical: the planet is below). In the close views the planet is a backdrop at the
// scale of the orbit view (it would be a straight line far below at the true scale).

static void ctext_c(const kfont_t *f, int cx, int base, const char *s, uint16_t col);  // (orb_hud.inc: a cached text)

// ================================================================ the camera
static double s_cx, s_cy;           // the middle of the screen (km)
static float s_cs = 0.4f;           // px for each km
static float s_croll;               // the world angle that is up on the screen
static float s_cph_c = 1, s_cph_s;  // cos and sin of the turn from the world to the screen
static float s_shx, s_shy;          // the shake of this frame (px)
// the camera relative to the tug, eased: the offset (km) along the way and up, the log of the scale, the roll offset
static float s_co_a, s_co_u, s_co_ls, s_co_r;  // (the offset in px of the screen: a zoom does not swing it)
static bool s_cam_init;
static float s_cam_t;  // real seconds (the breath)
static float s_punch;  // a short zoom in (0..1) at a big moment
static int s_view;     // the view now: V_MAP, V_ORBIT, V_NEAR, V_CLOSE (for the HUD and the tests)
enum {
    V_MAP,
    V_ORBIT,
    V_NEAR,
    V_CLOSE
};
// the middle of the free area of the view (px): the 3.49 has the keys at its right end; the 320 x 240 frame has the
// text at the top and the keys at the bottom
static inline int vleft(void)
{
    return longl() && s_ctl_mode == CTL_TOUCH && s_state != ST_TITLE ? 140 : 0;
}  // (the turn keys)
static inline float vcx(void)
{
    return longl() ? (vleft() + SW - 124) * 0.5f : SW * 0.5f;
}
static inline float vcy(void)
{
    return longl() ? SH * 0.52f : rndl() ? SH * 0.47f : SH * 0.40f;
}
static inline float vhw(void)
{
    return longl() ? (SW - 124 - vleft()) * 0.5f : rndl() ? SW * 0.40f : SW * 0.5f;
}  // its half width
static inline float vhh(void)
{
    return longl() ? SH * 0.40f : rndl() ? SH * 0.24f : SH * 0.22f;
}  // its half height (the ships)
static float scale_orbit(void)
{
    return longl() ? 0.40f : rndl() ? 0.36f * SW / 320 : 0.36f;
}  // px/km: the orbit view (the planet limb is low)
static float scale_map(void)
{
    return longl() ? 0.17f : rndl() ? 0.2f * SW / 320 : 0.2f;
}  // px/km: far out (the top of the planet, the orbits)
static float scale_max(void)
{
    return longl() ? 2600.0f : 2400.0f;
}  // px/km: the closest view (2.6 px for each m)
static void cam_reset(void)
{
    s_cam_init = false;
    s_punch = 0;
}
static void cam_punch(float k)
{
    if (k > s_punch) {
        s_punch = k;
    }
}

static inline void w2s(double wx, double wy, float *sx, float *sy)
{
    float dx = (float)(wx - s_cx) * s_cs, dy = (float)(wy - s_cy) * s_cs;
    *sx = vcx() + dx * s_cph_c - dy * s_cph_s + s_shx;
    *sy = vcy() + dx * s_cph_s + dy * s_cph_c + s_shy;
}
// a world vector in metres to a screen vector (px)
static inline void v2s(float mx, float my, float *sx, float *sy)
{
    float k = s_cs * 0.001f;
    *sx = (mx * s_cph_c - my * s_cph_s) * k;
    *sy = (mx * s_cph_s + my * s_cph_c) * k;
}
static void cam_target_body(int *j)
{
    *j = -1;
    if (T.state == TH_ON || T.state == TH_FLY) {
        *j = T.to;
        return;
    }
    if (s_tgt > 0 && s_tgt < s_nb && B[s_tgt].on && !s_released[s_tgt]) {
        *j = s_tgt;
    } else {
        // (a capsule that falls to the ship after the release: the camera keeps it while it is near)
        for (int i = 1; i < s_nb; i++) {
            if (B[i].on && s_released[i] && B[i].kind != K_DEBRIS) {
                *j = i;
                break;
            }
        }
    }
}
static void cam_step(float dt)
{
    s_cam_t += dt;
    float th = atan2f((float)B[0].y, (float)B[0].x);
    float vx, vy;
    com_vel(&vx, &vy);
    float hz = (float)B[0].x * vy - (float)B[0].y * vx, dir = hz >= 0 ? 1.0f : -1.0f;
    // the local frame of the tug: along its way (a) and up (u)
    float ux = cosf(th), uy = sinf(th), ax = -uy * dir, ay = ux * dir;
    int j;
    cam_target_body(&j);
    // (a tow: a close view at the lock, at the start of each burn and in a hot pass through the air; else the orbit
    // view, where the band of the goal and the coming orbit show)
    if (T.state == TH_ON && j == T.to) {
        bool hot = B[j].in_air && B[j].heat > 0.15f;
        if (s_tow_rt > 6 && !(s_throttle > 0 && s_burn_rt < 2.5f) && !hot) {
            j = -1;
        }
    }
    float d = 1e9f, jx = 0, jy = 0;
    if (j > 0) {
        jx = (float)(B[j].x - B[0].x);
        jy = (float)(B[j].y - B[0].y);
        d = fsqrt(jx * jx + jy * jy);
    }
    float so = scale_orbit();
    float ga = 0, gu = 0, gs = so;
    int view;
    if (s_rate > 70 && d > 60) {
        // MAP: far out, the top half of the planet and the orbits; the tug a little above the middle
        view = V_MAP;
        gs = scale_map();
        ga = 0;
        gu = -18 / gs;
    } else if (j > 0 && d < 70) {
        // NEAR or CLOSE: the tug and the target (or the tow) in the frame
        float ja = jx * ax + jy * ay, ju = jx * ux + jy * uy;  // (the target in the local frame, km)
        float k = T.state == TH_ON ? 0.5f : 0.45f;
        ga = ja * k;
        gu = ju * k;
        // (the farther ship from the middle, plus the half size of a big ship, must stay 16 px inside the free area)
        float pad = 0.048f;
        float fx = (absf(ja) * (1 - k) + pad) / fmaxf(vhw() - 16, 20);
        float fy = (absf(ju) * (1 - k) + pad) / fmaxf(vhh() - 6, 20);
        gs = 1 / fmaxf(fmaxf(fx, fy), 1e-6f);
        gs = clampf(gs, so * 1.2f, scale_max());
        view = gs > 60 ? V_CLOSE : V_NEAR;
    } else if (j <= 0 && T.state != TH_ON && s_state == ST_PLAY && body_alt(&B[0]) > 0 && s_close > 0.5f) {
        // (no target: stay near the tug)
        view = V_CLOSE;
        gs = 1200;
    } else {
        // ORBIT: the tug a bit behind the middle (room ahead), the limb of the planet near the bottom
        view = V_ORBIT;
        ga = (SW * (longl() ? 0.16f : 0.12f)) / so;
        gu = longl() ? 8 : -10;
        gs = so;
    }
    // the breath: a slow zoom in and out (about each 6 s) and a small roll; a punch at a big moment
    float breath = 1 + 0.045f * fsin(s_cam_t * 1.05f) + 0.25f * s_punch;
    gs *= breath;
    float groll = 0.022f * fsin(s_cam_t * 0.7f + 1.3f);
    s_punch = fmaxf(0, s_punch - dt * 0.9f);
    float lgs = logf(gs);
    // (the offset eases in px of the screen, so the tug stays in the frame while the zoom changes)
    ga *= gs;
    gu *= gs;
    if (!s_cam_init) {
        s_co_a = ga;
        s_co_u = gu;
        s_co_ls = lgs;
        s_co_r = groll;
        s_cam_init = true;
    }
    float kz = follow_k(dt, view == V_MAP ? 0.9f : 0.6f), kp = follow_k(dt, 0.45f);
    // (the offset follows in px of the screen: a big zoom change does not swing the frame)
    s_co_ls += (lgs - s_co_ls) * kz;
    s_co_a += (ga - s_co_a) * kp;
    s_co_u += (gu - s_co_u) * kp;
    s_co_r += (groll - s_co_r) * kp;
    s_cs = expf(s_co_ls);
    float oa = s_co_a / s_cs, ou = s_co_u / s_cs;
    s_cx = B[0].x + (double)(ax * oa + ux * ou);
    s_cy = B[0].y + (double)(ay * oa + uy * ou);
    // up on the screen: the local vertical at the tug (the planet below), with the roll of the breath
    s_croll = th + s_co_r;
    float ph = -PI_F / 2 - s_croll;
    s_cph_c = cosf(ph);
    s_cph_s = sinf(ph);
    s_view = s_cs < so * 0.7f ? V_MAP : s_cs < so * 1.6f ? V_ORBIT : s_cs < 60 ? V_NEAR : V_CLOSE;
    (void)view;
    s_close = smooth01((s_co_ls - logf(so * 12)) / (logf(so * 120) - logf(so * 12)));
    s_shx = s_shake > 0 ? rnds() * s_shake : 0;
    s_shy = s_shake > 0 ? rnds() * s_shake : 0;
}

// ================================================================ the planet: a texture round it, made at the start
#define TEX_A 1024       // the angles round the planet
#define TEX_D 64         // the depth from the limb (0) into the disc (63)
static uint16_t *s_tex;  // TEX_D rows of TEX_A colours (PSRAM), in tiles of 8 x 8 (TEXI)
// The index of a texel: tiles of 8 angles x 8 depths (128 bytes). A small planet (the map view) steps over many rows in a
// few pixels; with rows of 2 KB, each pixel was a new line of the cache (the planet took 20-44 ms on the CoreS3).
#define TEXI(d, a) (((((d) >> 3) * (TEX_A >> 3) + ((a) >> 3)) << 6) | (((d) & 7) << 3) | ((a) & 7))
static uint8_t *s_city;        // the city lights (0..255) of each place (PSRAM)
static uint8_t s_drow[257];    // the depth (a part of the radius, 0..1 in 256 steps) to a row of the texture
static uint8_t s_lit[TEX_A];   // the light of each world angle (0..32): day 32, night 0
static uint8_t s_dusk[TEX_A];  // the dusk band (0..32)
static uint16_t s_atm[TEX_A];  // the colour of the air at each world angle
static uint8_t s_glw[TEX_A];   // the strength of the glow of the air at each world angle (0..32)
static void planet_make(void)
{
    if (!s_tex) {
        s_tex = orb_alloc((size_t)TEX_A * TEX_D * 2);
    }
    if (!s_city) {
        s_city = orb_alloc((size_t)TEX_A * TEX_D);
    }
    if (!s_tex || !s_city) {
        return;
    }
    for (int i = 0; i <= 256; i++) {
        float f = clampf(i / 256.0f * 1.6f, 0, 1);
        s_drow[i] = (uint8_t)clampi((int)(powf(f, 0.6f) * (TEX_D - 1) + 0.5f), 0, TEX_D - 1);
    }
    // the sea: bands from the limb (bright) to the deep (dark); the land: noise over a level; the clouds
    static const col3_t SEA0 = {59, 133, 184}, SEA1 = {13, 44, 87};
    static const col3_t LAND[3][2] = {
            {{201, 164, 106}, {227, 196, 140}}, {{127, 154, 79}, {168, 191, 108}}, {{163, 116, 76}, {201, 154, 106}}};
    for (int d = 0; d < TEX_D; d++) {
        float dz = d / (float)(TEX_D - 1);
        col3_t sea = c3mix(SEA0, SEA1, powf(dz, 0.7f));
        for (int a = 0; a < TEX_A; a++) {
            float u = a / (float)TEX_A * 32, v = dz * 4.0f;
            float n = fbm_p(u * 0.75f, v * 0.8f, 24, 3, 3);
            float kind = vnoise_p(u * 0.5f, v * 0.5f + 9, 16, 5);
            col3_t c = sea;
            // (soft bands of the sea: the look of the page)
            c = c3mul(c, 0.94f + 0.06f * vnoise_p(u * 3, v * 2, 96, 11));
            uint8_t city = 0;
            // (no land and no cloud deep in the disc: the map view shows a calm sea there, not spokes)
            float deep = clampf((dz - 0.5f) * 3, 0, 1);
            n -= deep * 0.4f;
            if (n > 0.56f) {
                int k = kind < 0.4f ? 0 : kind < 0.7f ? 1 : 2;
                float hi = smooth01((n - 0.62f) * 12);
                c = c3mix(LAND[k][0], LAND[k][1], hi);
                // (the coast: a thin light line)
                if (n < 0.575f) {
                    c = c3mix(c, c3(240, 230, 200), 0.35f);
                }
                uint32_t h = hash2(a * 7 + 1, d * 13 + 5);
                if ((h & 31) < 3 && n > 0.6f) {
                    city = (uint8_t)(120 + (h >> 8) % 136);
                }
            } else if (n > 0.53f) {
                c = c3mix(c, c3(120, 190, 210), 0.35f);  // (the shallow sea at a coast)
            }
            float cl = fbm_p(u * 1.3f + 40, v * 2.2f, 42, 17, 3);
            cl -= deep * 0.4f;
            if (cl > 0.6f) {
                c = c3mix(c, c3(240, 248, 255), clampf((cl - 0.6f) * 5, 0, 0.85f));
            }
            s_tex[TEXI(d, a)] = c3p(c);
            s_city[TEXI(d, a)] = city;
        }
    }
}
static void planet_free(void)
{
    orb_free(s_tex);
    s_tex = NULL;
    orb_free(s_city);
    s_city = NULL;
}
// the light of each world angle: the sun side is day, a band of dusk at the terminator (made again when the sun moves)
static float s_lit_sun = 1e9f;
static void planet_light(void)
{
    if (s_sun == s_lit_sun) {
        return;
    }
    s_lit_sun = s_sun;
    for (int a = 0; a < TEX_A; a++) {
        float ang = a * (TAU_F / TEX_A), l = cosf(ang - s_sun);
        s_lit[a] = (uint8_t)clampi((int)((l + 0.10f) / 0.22f * 32), 0, 32);
        float du = 1 - absf(l) / 0.22f;
        s_dusk[a] = (uint8_t)clampi((int)(du * 30), 0, 30);
        col3_t day = c3(140, 215, 255), dusk = c3(255, 150, 90), night = c3(60, 50, 140);
        col3_t c = l > 0 ? c3mix(dusk, day, clampf(l / 0.3f, 0, 1)) : c3mix(dusk, night, clampf(-l / 0.25f, 0, 1));
        s_atm[a] = c3p(c);
        // (the strength of the glow of the air at this angle, of 32)
        s_glw[a] = (uint8_t)clampi((int)(32 * (0.35f + 0.65f * (s_lit[a] + s_dusk[a]) / 40.0f)), 0, 32);
    }
}
// The holes: the opaque boxes of this frame (the keys, the plate of the title). The HUD writes each pixel there, so the
// sky and the planet skip them (only the rounded corners blend with the world). The CoreS3 has a slow quad PSRAM: each
// write to a line of the frame that is not in the cache reads the line first.
typedef struct {
    int16_t x, y, w, h, r;
} hole_t;
#define NHOLE 8
static hole_t s_holes[NHOLE];
static int s_nholes;
static void hole_add(int x, int y, int w, int h, int r)
{
    if (s_nholes >= NHOLE || w <= 2 * r || h <= 2 * r) {
        return;
    }
    hole_t o = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r};
    s_holes[s_nholes++] = o;
}
// the covered spans of the row y: sorted, with no overlap (iv[2k] .. iv[2k+1] - 1); the count
static int hole_spans(int y, int *iv)
{
    int n = 0;
    for (int k = 0; k < s_nholes; k++) {
        const hole_t *o = &s_holes[k];
        if (y < o->y || y >= o->y + o->h) {
            continue;
        }
        int in = y < o->y + o->r || y >= o->y + o->h - o->r ? o->r : 0;
        int a = clampi(o->x + in, 0, SW), b = clampi(o->x + o->w - in, 0, SW);
        if (a >= b) {
            continue;
        }
        // (an insert in order of the start, then a merge with the spans that touch it)
        int i = n;
        while (i > 0 && iv[2 * i - 2] > a) {
            iv[2 * i] = iv[2 * i - 2];
            iv[2 * i + 1] = iv[2 * i - 1];
            i--;
        }
        iv[2 * i] = a;
        iv[2 * i + 1] = b;
        n++;
    }
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (m > 0 && iv[2 * i] <= iv[2 * m - 1]) {
            if (iv[2 * i + 1] > iv[2 * m - 1]) {
                iv[2 * m - 1] = iv[2 * i + 1];
            }
            continue;
        }
        iv[2 * m] = iv[2 * i];
        iv[2 * m + 1] = iv[2 * i + 1];
        m++;
    }
    return m;
}
static int s_planet_px;  // the pixels of the planet in this frame (the speed log)
// The planet disc and its air glow, row by row (only the rows and the spans that are on the screen). The exact angle
// and radius of a pixel come only at knots each 16 px of a row (fatan2, fsqrt); between the knots they go in linear
// steps in fixed point (16.16): the radius of a disc is smooth along a row (the error is under 0.1 px).
#define PK 16
// the planet centre on the screen: the true place, but at the scale ps
static void planet_centre(float ps, float *pcx, float *pcy)
{
    float dx = (float)(0 - s_cx) * ps, dy = (float)(0 - s_cy) * ps;
    *pcx = vcx() + dx * s_cph_c - dy * s_cph_s + s_shx;
    *pcy = vcy() + dx * s_cph_s + dy * s_cph_c + s_shy;
}
static void draw_planet(float ps)
{
    if (!s_tex) {
        return;
    }
    float pcx, pcy;
    planet_centre(ps, &pcx, &pcy);
    float Rp = R_PL * ps, gw = clampf(Rp * 0.03f, 2.5f, 6), Ra = Rp + gw;
    float ph = -PI_F / 2 - s_croll;
    // the planet turns with the time: the texture angle = the world angle - the turn
    int spin = (int)(PLANET_SPIN * (float)s_clock * (TEX_A / TAU_F) * 65536.0f) & (TEX_A * 65536 - 1);
    int y0 = clampi((int)(pcy - Ra), 0, SH), y1 = clampi((int)(pcy + Ra) + 1, 0, SH);
    float kA = TEX_A / TAU_F * 65536.0f;
    // (e16: px under the limb in 16.16, negative over it; the depth index di = e * 256 / Rp; the glow t = 32 - over * 32 / gw)
    int kd8 = (int)(65536.0f / Rp), kg = (int)(32.0f / gw * 256), eg16 = -(int)(gw * 65536);
    int n = 0;
    uint16_t navy = rgb(5, 8, 26), citc = rgb(255, 200, 120), dusc = rgb(120, 50, 20);
    const uint16_t *tex = s_tex;
    const uint8_t *city = s_city;
    for (int y = y0; y < y1; y++) {
        float dy = y + 0.5f - pcy, h2 = Ra * Ra - dy * dy;
        if (h2 <= 0) {
            continue;
        }
        float hw = fsqrt(h2);
        int x0 = clampi((int)(pcx - hw), 0, SW), x1 = clampi((int)(pcx + hw) + 1, 0, SW);
        if (rndl()) {
            // (the round panel: no pixels out of the circle)
            float cy = y + 0.5f - SH * 0.5f, c2 = SW * SW * 0.25f - cy * cy;
            int cw = c2 > 0 ? (int)fsqrt(c2) + 1 : 0;
            x0 = x0 > SW / 2 - cw ? x0 : SW / 2 - cw;
            x1 = x1 < SW / 2 + cw ? x1 : SW / 2 + cw;
        }
        if (x0 >= x1) {
            continue;
        }
        uint16_t *row = g_fb + y * SW;
        int hv[2 * NHOLE + 2], nh = hole_spans(y, hv);
        // the knots: the angle (continuous along the row) and the radius at x0 + PK * k
        float pa = 0;
        int a16 = 0, e16 = 0, da16 = 0, de16 = 0, fresh = 1;
        for (int xk = x0; xk < x1; xk += PK) {
            int xe = xk + PK < x1 ? xk + PK : x1;
            // (a run fully in a hole has no knot: the next run starts with an exact knot)
            int inh = 0;
            for (int k = 0; k < nh; k++) {
                if (hv[2 * k] <= xk && hv[2 * k + 1] >= xe) {
                    inh = 1;
                    break;
                }
            }
            if (inh) {
                fresh = 1;
                continue;
            }
            if (fresh) {
                fresh = 0;
                float dx = xk + 0.5f - pcx;
                pa = fatan2(dy, dx) - ph;
                a16 = (int)(wrap_pi(pa) * kA);
                e16 = (int)((Rp - fsqrt(dx * dx + dy * dy)) * 65536);
            }
            float dx2 = xe + 0.5f - pcx, pa2 = fatan2(dy, dx2) - ph;
            float d = wrap_pi(pa2 - pa);
            pa = pa2;
            int e2 = (int)((Rp - fsqrt(dx2 * dx2 + dy * dy)) * 65536), m = xe - xk;
            da16 = (int)(d * kA) / m;
            de16 = (e2 - e16) / m;
            // (the runs of pixels out of the holes: a run in a hole only moves the angle and the depth on)
            int x = xk;
            while (x < xe) {
                int xs = xe, skip = 0;
                for (int k = 0; k < nh; k++) {
                    if (hv[2 * k + 1] <= x) {
                        continue;
                    }
                    if (hv[2 * k] <= x) {
                        skip = 1;
                        xs = hv[2 * k + 1] < xe ? hv[2 * k + 1] : xe;
                    } else if (hv[2 * k] < xe) {
                        xs = hv[2 * k];
                    }
                    break;
                }
                if (skip) {
                    a16 += da16 * (xs - x);
                    e16 += de16 * (xs - x);
                    x = xs;
                    continue;
                }
                for (; x < xs; x++, a16 += da16, e16 += de16) {
                    if (e16 <= eg16) {
                        continue;
                    }
                    int ai = (a16 >> 16) & (TEX_A - 1);
                    if (e16 < 0) {
                        // the glow of the air over the limb: light that adds, strongest at the limb
                        int t = 32 - (((-e16) * kg) >> 24);
                        if (t > 0) {
                            int k = (t * t * s_glw[ai]) >> 10;
                            if (k > 0) {
                                row[x] = add565(row[x], s_atm[ai], k > 32 ? 32 : k);
                            }
                        }
                        continue;
                    }
                    n++;
                    int di = ((e16 >> 8) * kd8) >> 16;
                    if (di > 256) {
                        di = 256;
                    }
                    int tix = ((a16 - spin) >> 16) & (TEX_A - 1), d = TEXI(s_drow[di], tix);
                    uint16_t c = tex[d];
                    int lit = s_lit[ai];
                    if (lit < 32) {
                        c = mix565(mix565(navy, c, 5), c, lit);
                        if (lit < 14) {
                            int cv = city[d];
                            if (cv) {
                                c = add565(c, citc, (cv * (14 - lit)) >> 8);
                            }
                        }
                    }
                    int du = s_dusk[ai];
                    if (du) {
                        c = add565(c, dusc, du >> 2);
                    }
                    // (the air haze at the limb: the edge is light blue; a smooth edge over the glow)
                    if (e16 < 6 * 65536) {
                        c = add565(c, s_atm[ai], (6 * 65536 - e16) * 3 >> 16);
                        if (e16 < 65536) {
                            c = mix565(row[x], c, e16 >> 11);
                        }
                    }
                    if (row[x] != c) {
                        row[x] = c;
                    }
                }
            }
        }
    }
    s_planet_px = n;
}

// ================================================================ the sky
#define NSTAR 150
typedef struct {
    float a, y;
    uint8_t b, kind;
} star_t;
static star_t s_star[NSTAR];
static void sky_make(void)
{
    uint32_t keep = s_rng;
    s_rng = 0x51A2B3u;
    for (int i = 0; i < NSTAR; i++) {
        s_star[i].a = rndf() * TAU_F;
        s_star[i].y = rndf();
        float b = powf(rndf(), 2.2f);
        s_star[i].b = (uint8_t)(40 + b * 215);
        s_star[i].kind = rndf() < 0.15f ? 1 : rndf() < 0.2f ? 2 : 0;
    }
    s_rng = keep;
}
// the screen x of a direction in the sky (the world angle a): the sky turns with the roll of the camera
static float sky_x(float a, float *dlt)
{
    float d = wrap_pi(a - s_croll);
    if (dlt) {
        *dlt = d;
    }
    return SW * 0.5f + d * (SW / 2.4f);
}
static bool tug_in_shadow(void)
{
    float x = (float)B[0].x, y = (float)B[0].y, sx = cosf(s_sun), sy = sinf(s_sun);
    float along = x * sx + y * sy, perp = absf(x * sy - y * sx);
    return along < 0 && perp < R_PL;
}
static bool body_in_shadow(const body_t *b)
{
    float x = (float)b->x, y = (float)b->y, sx = cosf(s_sun), sy = sinf(s_sun);
    float along = x * sx + y * sy, perp = absf(x * sy - y * sx);
    return along < 0 && perp < R_PL;
}
// the sun: a sprite of added light (the warm glow and the white core), made once (the glow of tfdraw took a root and a
// blend for each of its 13000 pixels in each frame)
#define SUN_R 34
static uint16_t *s_sunspr;  // (2 SUN_R)^2 colours to add (the byte order of the frame); 0: nothing
static void sun_make(void)
{
    if (!s_sunspr) {
        s_sunspr = orb_alloc((size_t)(2 * SUN_R) * (2 * SUN_R) * 2);
    }
    if (!s_sunspr) {
        return;
    }
    float rg = SUN_R, rc = 12;
    for (int j = 0; j < 2 * SUN_R; j++) {
        for (int i = 0; i < 2 * SUN_R; i++) {
            float dx = i + 0.5f - SUN_R, dy = j + 0.5f - SUN_R, d2 = dx * dx + dy * dy;
            float q = 1 - d2 / (rg * rg), qc = 1 - d2 / (rc * rc);
            float w = (q > 0 ? q * q * 70 / 256.0f : 0), wc = (qc > 0 ? qc * qc * 230 / 256.0f : 0);
            int r = (int)(255 * w + 255 * wc), g = (int)(216 * w + 255 * wc), bl = (int)(168 * w + 255 * wc);
            s_sunspr[j * 2 * SUN_R + i] = (r | g | bl) ? rgbc(r, g, bl) : 0;
        }
    }
}
// d + s with each channel at most full (both in the byte order of the frame)
static inline uint16_t sadd565(uint16_t d, uint16_t s)
{
    uint16_t D = (uint16_t)((d >> 8) | (d << 8)), S = (uint16_t)((s >> 8) | (s << 8));
    int r = (D >> 11) + (S >> 11), g = ((D >> 5) & 63) + ((S >> 5) & 63), b = (D & 31) + (S & 31);
    if (r > 31) {
        r = 31;
    }
    if (g > 63) {
        g = 63;
    }
    if (b > 31) {
        b = 31;
    }
    uint16_t o = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((o >> 8) | (o << 8));
}
static void sun_blit(int cx, int cy)
{
    if (!s_sunspr) {
        return;
    }
    for (int j = 0; j < 2 * SUN_R; j++) {
        int y = cy - SUN_R + j;
        if ((unsigned)y >= (unsigned)SH) {
            continue;
        }
        uint16_t *row = g_fb + y * SW;
        const uint16_t *src = s_sunspr + j * 2 * SUN_R;
        int i0 = cx - SUN_R < 0 ? SUN_R - cx : 0, i1 = cx + SUN_R > SW ? SW - cx + SUN_R : 2 * SUN_R;
        for (int i = i0; i < i1; i++) {
            uint16_t v = src[i];
            if (v) {
                row[cx - SUN_R + i] = sadd565(row[cx - SUN_R + i], v);
            }
        }
    }
}
// The fill of the sky skips the inner disc of the planet too: the planet writes each pixel there (the fill was a third
// of the frame on the CoreS3). (s_hole_r <= 0: no disc)
static float s_hole_x, s_hole_y, s_hole_r;
// A fill and a copy that write only the words that change. The frame buffer keeps the frame of two frames ago, and
// most of the sky, the keys and the planet are the same: a line of the cache that only was read does not go back to
// the PSRAM (a written line does: a fill wrote each line two times on the bus).
static inline void fill16(uint16_t *p, int n, uint16_t c)
{
    if (n <= 0) {
        return;
    }
    if ((uintptr_t)p & 2) {
        if (*p != c) {
            *p = c;
        }
        p++;
        n--;
    }
    uint32_t v = c | ((uint32_t)c << 16), *q = (uint32_t *)p;
    for (int k = n >> 1; k > 0; k--, q++) {
        if (*q != v) {
            *q = v;
        }
    }
    if ((n & 1) && p[n - 1] != c) {
        p[n - 1] = c;
    }
}
static inline void copy16(uint16_t *d, const uint16_t *s, int n)
{
    if (n <= 0) {
        return;
    }
    if ((((uintptr_t)d ^ (uintptr_t)s) & 2) == 0) {
        if ((uintptr_t)d & 2) {
            if (*d != *s) {
                *d = *s;
            }
            d++;
            s++;
            n--;
        }
        uint32_t *q = (uint32_t *)d;
        const uint32_t *r = (const uint32_t *)s;
        for (int k = n >> 1; k > 0; k--, q++, r++) {
            if (*q != *r) {
                *q = *r;
            }
        }
        if ((n & 1) && d[n - 1] != s[n - 1]) {
            d[n - 1] = s[n - 1];
        }
        return;
    }
    for (int k = 0; k < n; k++) {
        if (d[k] != s[k]) {
            d[k] = s[k];
        }
    }
}
static void sky_fill(uint16_t c)
{
    float r2 = s_hole_r > 4 ? s_hole_r * s_hole_r : 0;
    int iv[2 * (NHOLE + 1) + 2];
    for (int y = 0; y < SH; y++) {
        uint16_t *row = g_fb + y * SW;
        // (the spans of the holes, and the span of the disc in its place in the order)
        int n = hole_spans(y, iv);
        float dy = y + 0.5f - s_hole_y, h2 = r2 - dy * dy;
        if (h2 > 1) {
            float hw = fsqrt(h2);
            int xa = clampi((int)floorf(s_hole_x - hw) + 1, 0, SW), xb = clampi((int)floorf(s_hole_x + hw), 0, SW);
            if (xa < xb) {
                int i = n;
                while (i > 0 && iv[2 * i - 2] > xa) {
                    iv[2 * i] = iv[2 * i - 2];
                    iv[2 * i + 1] = iv[2 * i - 1];
                    i--;
                }
                iv[2 * i] = xa;
                iv[2 * i + 1] = xb;
                n++;
            }
        }
        // (the gaps between the covered spans; the spans can overlap now)
        int x = 0;
        for (int i = 0; i < n; i++) {
            if (iv[2 * i] > x) {
                fill16(row + x, iv[2 * i] - x, c);
            }
            if (iv[2 * i + 1] > x) {
                x = iv[2 * i + 1];
            }
        }
        fill16(row + x, SW - x, c);
    }
}
static int64_t s_fill_us;  // (the speed log: the time of the fill of the sky)
static void draw_sky(void)
{
    int64_t tf = esp_timer_get_time();
    sky_fill(rgb(2, 3, 10));
    s_fill_us += esp_timer_get_time() - tf;
    // a faint band of the galaxy: a few soft glows that turn with the sky
    for (int i = 0; i < NSTAR; i++) {
        const star_t *s = &s_star[i];
        float dl, x = sky_x(s->a, &dl);
        if (x < -2 || x >= SW + 2) {
            continue;
        }
        int X = (int)x + (int)s_shx, Y = (int)(s->y * SH) + (int)s_shy;
        if ((unsigned)X >= (unsigned)SW || (unsigned)Y >= (unsigned)SH) {
            continue;
        }
        int tw = 200 + (int)(55 * fsin(s_cam_t * 2.1f + i));
        int b = s->b * tw >> 8;
        uint16_t c = s->kind == 1 ? rgb(255, 217, 176) : s->kind == 2 ? rgb(185, 214, 255) : rgb(255, 255, 255);
        uint16_t *p = &g_fb[Y * SW + X];
        *p = mix565(*p, c, b >> 3);
        if (s->b > 200) {
            // (a bright star: a small cross)
            if (X > 1 && X < SW - 2) {
                p[-1] = mix565(p[-1], c, b >> 5);
                p[1] = mix565(p[1], c, b >> 5);
            }
            if (Y > 1 && Y < SH - 2) {
                p[-SW] = mix565(p[-SW], c, b >> 5);
                p[SW] = mix565(p[SW], c, b >> 5);
            }
        }
    }
    // the sun: a white disc, a warm glow and long rays (not in the shadow of the planet)
    if (!tug_in_shadow()) {
        float dl, x = sky_x(s_sun, &dl);
        if (absf(dl) < 1.5f) {
            float y = SH * (0.30f + absf(dl) * 0.22f) + s_shy;
            x += s_shx;
            float rg = longl() ? 46 : 40;
            sun_blit((int)x, (int)y);
            tfd_hspan((int)(x - rg * 1.8f), (int)(x + rg * 1.8f), (int)y, rgb(255, 240, 220), 110);
            for (int k = -(int)(rg * 0.9f); k <= (int)(rg * 0.9f); k++) {
                tfd_px((int)x, (int)y + k, rgb(255, 240, 220), 110 - absf((float)k) * 100 / rg);
            }
            // (the flares on the line to the middle of the screen)
            for (int k = 1; k <= 3; k++) {
                float t = k * 0.24f;
                tfd_glow(x + (SW * 0.55f - x) * t * 1.3f, y + (SH * 0.4f - y) * t * 1.3f, 4 + k * 3, rgb(159, 208, 255),
                         40);
            }
        }
    }
}

// ================================================================ rings and paths
// a thin smooth line of about 1 px (the Wu method: two pixels in each step; no root): the paths, the rings
static void fline(float x0, float y0, float x1, float y1, uint16_t c, int a)
{
    bool steep = absf(y1 - y0) > absf(x1 - x0);
    float t;
    if (steep) {
        t = x0;
        x0 = y0;
        y0 = t;
        t = x1;
        x1 = y1;
        y1 = t;
    }
    if (x0 > x1) {
        t = x0;
        x0 = x1;
        x1 = t;
        t = y0;
        y0 = y1;
        y1 = t;
    }
    float dx = x1 - x0, g = dx > 1e-4f ? (y1 - y0) / dx : 0;
    int lim = steep ? SH : SW;
    int xs = tfd_fl(x0 + 0.5f), xe = tfd_fl(x1 + 0.5f);
    if (xs < 0) {
        xs = 0;
    }
    if (xe > lim - 1) {
        xe = lim - 1;
    }
    float y = y0 + g * (xs + 0.5f - x0) - 0.5f;
    for (int x = xs; x <= xe; x++, y += g) {
        int yi = tfd_fl(y);
        int a2 = (int)(a * (y - yi)), a1 = a - a2;
        if (steep) {
            tfd_px(yi, x, c, a1);
            tfd_px(yi + 1, x, c, a2);
        } else {
            tfd_px(x, yi, c, a1);
            tfd_px(x, yi + 1, c, a2);
        }
    }
}
// a thin circle as 48 short lines
static void fcircle(float cx, float cy, float r, uint16_t c, int a)
{
    float px = cx + r, py = cy;
    for (int i = 1; i <= 48; i++) {
        float t = i * (TAU_F / 48), x = cx + cosf(t) * r, y = cy + sinf(t) * r;
        fline(px, py, x, y, c, a);
        px = x;
        py = y;
    }
}
// a band between two radii round a point (row by row: only the rows on the screen; no smooth edge)
static void ring_band(float cx, float cy, float rin, float rout, uint16_t c, int a)
{
    int y0 = clampi((int)(cy - rout), 0, SH), y1 = clampi((int)(cy + rout) + 1, 0, SH);
    for (int y = y0; y < y1; y++) {
        float dy = y + 0.5f - cy, ho2 = rout * rout - dy * dy;
        if (ho2 <= 0) {
            continue;
        }
        float ho = fsqrt(ho2), hi2 = rin * rin - dy * dy, hi = hi2 > 0 ? fsqrt(hi2) : 0;
        if (hi > 0) {
            tfd_hspan((int)(cx - ho), (int)(cx - hi), y, c, a);
            tfd_hspan((int)(cx + hi), (int)(cx + ho), y, c, a);
        } else {
            tfd_hspan((int)(cx - ho), (int)(cx + ho), y, c, a);
        }
    }
}
// a thin circle (a band of 1.5 px) with dashes: dash = 0 for a whole line
static void ring_line(float cx, float cy, float r, uint16_t c, int a, int dash)
{
    if (r < 1) {
        return;
    }
    // (only the part on the screen: the angles of the steps of about 3 px)
    int n = clampi((int)(TAU_F * r / 6), 24, 2000);
    float px = 0, py = 0;
    for (int i = 0; i <= n; i++) {
        float t = i * TAU_F / n, x = cx + cosf(t) * r, y = cy + sinf(t) * r;
        if (i > 0 && (!dash || (i / dash) % 2 == 0)) {
            bool on = (x >= -4 && x < SW + 4 && y >= -4 && y < SH + 4) ||
                      (px >= -4 && px < SW + 4 && py >= -4 && py < SH + 4);
            if (on) {
                fline(px, py, x, y, c, a);
            }
        }
        px = x;
        py = y;
    }
}
// a predicted path of body i (km, the far views): solid at the start, then dashes; brightest near the body
static void draw_path(int i, uint16_t c, int a)
{
    const path_t *P = &s_path[i];
    if (P->n < 2) {
        return;
    }
    float px = 0, py = 0;
    bool pin = false;
    for (int k = 0; k < P->n; k++) {
        float x, y;
        w2s(P->x[k], P->y[k], &x, &y);
        bool in = x >= -20 && x < SW + 20 && y >= -20 && y < SH + 20;
        if (k > 0 && (in || pin) && (k < 6 || (k / 3) % 2 == 0)) {
            int aa = a * (P->n - k / 2) / P->n;
            uint16_t cc = (P->burn_at >= 0 && k >= P->burn_at) ? C_RED
                          : (P->air_at >= 0 && k >= P->air_at) ? rgb(255, 140, 70)
                                                               : c;
            fline(px, py, x, y, cc, aa);
        }
        px = x;
        py = y;
        pin = in;
    }
}
// the path of the tug relative to the target (the near views): the next minutes in the frame of the target (its local
// vertical turns with it), drawn from the target
static void draw_rel_path(int j, int kmax)
{
    const path_t *P = &s_path[0], *Q = &s_path[j];
    int n = P->n < Q->n ? P->n : Q->n;
    if (n < 2 || j <= 0) {
        return;
    }
    if (n > kmax) {
        n = kmax;
    }
    float tx, ty;
    w2s(B[j].x, B[j].y, &tx, &ty);
    float a0 = atan2f(Q->y[0], Q->x[0]);
    float px = 0, py = 0, best = 1e9f, bx = 0, by = 0;
    for (int k = 0; k < n; k++) {
        float a = atan2f(Q->y[k], Q->x[k]), da = a0 - a, c = cosf(da), s = sinf(da);
        float rx = (P->x[k] - Q->x[k]) * 1000, ry = (P->y[k] - Q->y[k]) * 1000;  // m
        float wx = rx * c - ry * s, wy = rx * s + ry * c, sx, sy;
        v2s(wx, wy, &sx, &sy);
        float x = tx + sx, y = ty + sy;
        float dd = wx * wx + wy * wy;
        if (dd < best) {
            best = dd;
            bx = x;
            by = y;
        }
        if (k > 0 && (k / 2) % 2 == 0) {
            fline(px, py, x, y, C_AMB, 220 - 150 * k / n);
        }
        px = x;
        py = y;
    }
    // (the closest point on the way)
    if (best < 1e8f) {
        tfd_ring(bx, by, 4, 1.4f, C_AMB, 220);
    }
}

// ================================================================ the ships
// A part: a quad in the frame of the body (m; x forward, y to the side), its colour, and its kind: a hull that the sun
// lights (with a shade half on the far side), a dark part, a light that glows.
enum {
    PK_HULL,
    PK_DARK,
    PK_GLOW,
    PK_PANEL
};
typedef struct {
    float p[8];
    uint8_t r, g, b, kind;
} part3_t;
#define Q4(x0, y0, x1, y1, x2, y2, x3, y3) {x0, y0, x1, y1, x2, y2, x3, y3}
static const part3_t SHP_TUG[] = {
        {Q4(-11, -2.6f, -14, -3.6f, -14, 3.6f, -11, 2.6f), 58, 63, 72, PK_DARK},  // the bell
        {Q4(-9, -12, 4, -12, 4, -10, -9, -10), 29, 61, 122, PK_PANEL},            // the solar wings
        {Q4(-9, 10, 4, 10, 4, 12, -9, 12), 29, 61, 122, PK_PANEL},
        {Q4(-4, -10, -3, -10, -3, -5, -4, -5), 154, 163, 173, PK_DARK},
        {Q4(-4, 5, -3, 5, -3, 10, -4, 10), 154, 163, 173, PK_DARK},
        {Q4(-11, -4, 5, -5, 5, 5, -11, 4), 223, 228, 234, PK_HULL},               // the body
        {Q4(5, -5, 9, -2, 9, 2, 5, 5), 210, 216, 224, PK_HULL},                   // the nose
        {Q4(-6, -4.4f, -2, -4.7f, -2, 4.7f, -6, 4.4f), 255, 138, 61, PK_HULL},    // the orange band
        {Q4(5, -4, 8.4f, -1.8f, 8.4f, -1.2f, 5, -1.4f), 127, 227, 255, PK_GLOW},  // the window
        {Q4(9, -2.3f, 13, -3.8f, 14, -1.6f, 9.5f, -1.0f), 90, 99, 110, PK_DARK},  // the claw
        {Q4(9, 2.3f, 13, 3.8f, 14, 1.6f, 9.5f, 1.0f), 90, 99, 110, PK_DARK},
        {Q4(8, -0.7f, 13, -0.7f, 13, 0.7f, 8, 0.7f), 42, 47, 54, PK_DARK},  // the harpoon
};
static const part3_t SHP_KESTREL[] = {
        {Q4(42, -4, 48, -6, 48, 6, 42, 4), 58, 63, 72, PK_DARK},            // the engine bell
        {Q4(-20, -11, -13, -11, -13, -5, -20, -5), 255, 138, 61, PK_HULL},  // the cargo boxes
        {Q4(-12, -11, -6, -11, -6, -5, -12, -5), 63, 193, 201, PK_HULL},
        {Q4(10, -11, 17, -11, 17, -5, 10, -5), 242, 242, 242, PK_HULL},
        {Q4(17, -11, 23, -11, 23, -5, 17, -5), 255, 138, 61, PK_HULL},
        {Q4(-20, 5, -13, 5, -13, 11, -20, 11), 63, 193, 201, PK_HULL},
        {Q4(12, 5, 20, 5, 20, 11, 12, 11), 255, 207, 74, PK_HULL},
        {Q4(-6, -4.6f, 5, -4.6f, 5, 4.6f, -6, 4.6f), 60, 66, 76, PK_DARK},  // the bare frame in the gap
        {Q4(-40, -6, -24, -7, -22, 7, -40, 6), 217, 221, 227, PK_HULL},     // the bridge
        {Q4(-22, -5, -6, -5, -6, 5, -22, 5), 191, 198, 207, PK_HULL},
        {Q4(4, -5, 24, -5, 24, 5, 4, 5), 191, 198, 207, PK_HULL},
        {Q4(24, -8, 42, -6, 42, 6, 24, 8), 154, 164, 176, PK_HULL},           // the engine room
        {Q4(-44, -2, -40, -6, -40, 6, -44, 2), 200, 205, 212, PK_HULL},       // the bow (the hook)
        {Q4(-38, -3, -27, -3, -27, -0.6f, -38, -0.6f), 43, 52, 66, PK_DARK},  // the windows
        {Q4(-38, -3, -36, -3, -36, -0.6f, -38, -0.6f), 255, 211, 106, PK_GLOW},
};
static const part3_t SHP_SAT[] = {
        {Q4(-2, -20, 3, -20, 3, -7, -2, -7), 29, 61, 122, PK_PANEL},  // the solar wings
        {Q4(-2, 7, 3, 7, 3, 20, -2, 20), 29, 61, 122, PK_PANEL},
        {Q4(0, -7, 1, -7, 1, 7, 0, 7), 154, 163, 173, PK_DARK},
        {Q4(-6, -6, 6, -6, 6, 6, -6, 6), 214, 178, 92, PK_HULL},               // the gold body
        {Q4(6, -3, 10, -5, 10, 5, 6, 3), 225, 228, 232, PK_HULL},              // the dish
        {Q4(-9, -1.5f, -6, -1.5f, -6, 1.5f, -9, 1.5f), 90, 99, 110, PK_DARK},  // the hook
};
static const part3_t SHP_CAPSULE[] = {
        {Q4(-6, -6, 4, -3.5f, 4, 3.5f, -6, 6), 220, 224, 230, PK_HULL},  // the cone
        {Q4(4, -3.5f, 7, -2.5f, 7, 2.5f, 4, 3.5f), 190, 196, 204, PK_HULL},
        {Q4(-7.5f, -6.5f, -6, -6.5f, -6, 6.5f, -7.5f, 6.5f), 120, 70, 40, PK_DARK},  // the heat shield
        {Q4(-2, -4.6f, 1, -4, 1, -2.8f, -2, -3.4f), 127, 227, 255, PK_GLOW},         // the window
        {Q4(-9, -1.2f, -7.5f, -1.2f, -7.5f, 1.2f, -9, 1.2f), 90, 99, 110, PK_DARK},  // the hook
};
static const part3_t SHP_MODULE[] = {
        {Q4(-6, -26, 4, -26, 4, -12, -6, -12), 29, 61, 122, PK_PANEL},
        {Q4(-6, 12, 4, 12, 4, 26, -6, 26), 29, 61, 122, PK_PANEL},
        {Q4(-36, -10, 28, -10, 28, 10, -36, 10), 205, 210, 218, PK_HULL},    // the long module
        {Q4(-30, -11, -26, -11, -26, 11, -30, 11), 150, 158, 170, PK_HULL},  // the rings
        {Q4(0, -11, 4, -11, 4, 11, 0, 11), 150, 158, 170, PK_HULL},
        {Q4(20, -11, 24, -11, 24, 11, 20, 11), 150, 158, 170, PK_HULL},
        {Q4(28, -7, 36, -5, 36, 5, 28, 7), 120, 128, 140, PK_HULL},
        {Q4(-39, -3, -36, -3, -36, 3, -39, 3), 90, 99, 110, PK_DARK},  // the hook
        {Q4(-14, -6, 8, -6, 8, -3, -14, -3), 255, 138, 61, PK_HULL},   // the stripe
};
static const part3_t SHP_DEBRIS[] = {
        {Q4(-3, -2, 2, -3, 3, 1, -2, 2), 154, 164, 176, PK_HULL},
        {Q4(4, 3, 7, 2, 8, 5, 5, 6), 217, 221, 227, PK_HULL},
        {Q4(-7, 3, -4, 4, -5, 6, -8, 5), 255, 138, 61, PK_HULL},
        {Q4(-5, -7, -2, -6, -3, -4, -6, -5), 154, 164, 176, PK_HULL},
};
static const part3_t *shape_parts(int shape, int *n)
{
    switch (shape) {
    case SH_KESTREL:
        *n = (int)(sizeof SHP_KESTREL / sizeof SHP_KESTREL[0]);
        return SHP_KESTREL;
    case SH_SAT:
        *n = (int)(sizeof SHP_SAT / sizeof SHP_SAT[0]);
        return SHP_SAT;
    case SH_CAPSULE:
        *n = (int)(sizeof SHP_CAPSULE / sizeof SHP_CAPSULE[0]);
        return SHP_CAPSULE;
    case SH_MODULE:
        *n = (int)(sizeof SHP_MODULE / sizeof SHP_MODULE[0]);
        return SHP_MODULE;
    case SH_DEBRIS:
        *n = (int)(sizeof SHP_DEBRIS / sizeof SHP_DEBRIS[0]);
        return SHP_DEBRIS;
    }
    *n = (int)(sizeof SHP_TUG / sizeof SHP_TUG[0]);
    return SHP_TUG;
}
// draw a ship: its parts at the screen point (sx, sy), the world angle ang, k px for each m; light = 0..1 (the sun
// or a lamp), the sun side in the frame of the body (sy_sun: + = the sun is on the +y side)
static void draw_ship(const part3_t *P, int n, float sx, float sy, float ang, float k, float light, float sun_y,
                      float heat)
{
    float a = ang + (-PI_F / 2 - s_croll);
    float c = cosf(a) * k, s = sinf(a) * k;
    float shade = clampf(absf(sun_y), 0.2f, 1) * 150;
    for (int i = 0; i < n; i++) {
        const part3_t *q = &P[i];
        float pts[8];
        for (int v = 0; v < 4; v++) {
            float x = q->p[2 * v], y = q->p[2 * v + 1];
            pts[2 * v] = sx + x * c - y * s;
            pts[2 * v + 1] = sy + x * s + y * c;
        }
        col3_t col = c3(q->r, q->g, q->b);
        if (q->kind == PK_GLOW) {
            tfd_poly(pts, 4, c3p(col), 256);
            continue;
        }
        float l = q->kind == PK_PANEL ? 0.45f + 0.55f * light : 0.16f + 0.84f * light;
        col3_t cl = c3mul(col, l);
        // (the heat of the air: the hull glows red)
        if (heat > 0.15f) {
            cl = c3mix(cl, c3(255, 110, 50), clampf((heat - 0.15f) * 0.9f, 0, 0.7f));
        }
        tfd_poly(pts, 4, c3p(cl), 256);
        if (q->kind == PK_HULL && k * 8 > 3) {
            // the shade half (the side away from the sun) and a thin light line on the sun side
            float m[8];
            int up = sun_y > 0;  // (the sun on +y: the shade on the -y half)
            for (int v = 0; v < 2; v++) {
                int t0 = up ? v : 3 - v, t1 = up ? 3 - v : v;
                float mx = (pts[2 * t0] + pts[2 * t1]) * 0.5f, my = (pts[2 * t0 + 1] + pts[2 * t1 + 1]) * 0.5f;
                m[2 * v] = pts[2 * t0];
                m[2 * v + 1] = pts[2 * t0 + 1];
                m[6 - 2 * v] = mx;
                m[7 - 2 * v] = my;
            }
            tfd_poly(m, 4, rgb(16, 20, 34), (int)(shade * (0.4f + 0.6f * light)));
        }
    }
}
// the size of a body for its drawing (m: half its length), and the least size on the screen (px)
static float body_half(int shape)
{
    return shape == SH_KESTREL ? 46 : shape == SH_MODULE ? 39 : shape == SH_SAT ? 20 : shape == SH_CAPSULE ? 9 : 10;
}
static int s_shape[NB];  // the shape of each body (SH_*; the tug: -1)

// the flame of the engine and the plasma of the air (the tug: from s_engine)
static void draw_flame(float sx, float sy, float ang, float k, float f)
{
    if (f < 0.02f) {
        return;
    }
    float a = ang + (-PI_F / 2 - s_croll), c = cosf(a), s = sinf(a);
    float fl = f * (0.85f + 0.15f * fsin(s_cam_t * 60));
    float bx = sx - c * 14 * k, by = sy - s * 14 * k, len = (6 + 22 * fl) * fmaxf(k, 0.5f), w = 2.6f * fmaxf(k, 0.6f);
    // (the glow: at most 16 px, each of its pixels is a read and a write of the frame)
    tfd_glow(bx - c * len * 0.3f, by - s * len * 0.3f, fminf(len * 0.8f, 16), rgb(127, 200, 255), (int)(150 * fl));
    float t1[6] = {bx - s * w, by + c * w, bx - c * len, by - s * len, bx + s * w, by - c * w};
    tfd_poly(t1, 3, rgb(120, 200, 255), (int)(210 * fl));
    float w2 = w * 0.45f, l2 = len * 0.5f;
    float t2[6] = {bx - s * w2, by + c * w2, bx - c * l2, by - s * l2, bx + s * w2, by - c * w2};
    tfd_poly(t2, 3, rgb(255, 255, 255), (int)(240 * fl));
}
static void draw_plasma(int i, float sx, float sy, float k)
{
    const body_t *b = &B[i];
    if (b->heat < 0.08f || !b->in_air) {
        return;
    }
    float vx = b->vx, vy = b->vy, v = fsqrt(vx * vx + vy * vy);
    if (v < 1e-6f) {
        return;
    }
    float svx, svy;
    v2s(vx / v * 1000, vy / v * 1000, &svx, &svy);
    float L = fsqrt(svx * svx + svy * svy);
    svx /= L;
    svy /= L;
    float r = (b->len + b->rad + 10) * fmaxf(k, 0.4f) * (0.8f + b->heat);
    int a = (int)clampf(b->heat * 260, 0, 240);
    tfd_glow(sx + svx * r * 0.35f, sy + svy * r * 0.35f, r, rgb(255, 120, 50), a);
    tfd_glow(sx + svx * r * 0.2f, sy + svy * r * 0.2f, r * 0.5f, rgb(255, 230, 170), a / 2);
}

// ================================================================ the world
static float s_tug_k;  // px for each m of the tug in this frame (with its least size)
static float s_tug_sx, s_tug_sy;
static void body_screen(int i, float *sx, float *sy)
{
    w2s(B[i].x, B[i].y, sx, sy);
}
static float body_k(int i)
{
    // px for each m: the true scale, but a body is never smaller than about 16 px (24 px for the tug) on the screen
    float k = s_cs * 0.001f, half = i == 0 ? 12 : body_half(s_shape[i]);
    float least = (i == 0 ? 11.0f : 9.0f) / half;
    return k > least ? k : least;
}
static void draw_bodies(void)
{
    // the light: the sun, or the lamp of the tug in the shadow of the planet (the targets that the lamp reaches)
    bool dark0 = tug_in_shadow();
    float nw = nose_world();
    for (int i = s_nb - 1; i >= 0; i--) {
        const body_t *b = &B[i];
        if (!b->on) {
            continue;
        }
        float sx, sy;
        body_screen(i, &sx, &sy);
        float k = body_k(i), half = i == 0 ? 14 : body_half(s_shape[i]);
        if (sx < -half * k - 40 || sx > SW + half * k + 40 || sy < -half * k - 40 || sy > SH + half * k + 40) {
            continue;
        }
        float ang = i == 0 ? nw : b->ang;
        float light = body_in_shadow(b) ? 0 : 1;
        if (i > 0 && light < 1) {
            // (the lamp of the tug: a cone of 0.4 rad, 450 m)
            float rx, ry;
            rel_m(i, &rx, &ry);
            float d = fsqrt(rx * rx + ry * ry), off = absf(wrap_pi(atan2f(ry, rx) - nw));
            if (d < 450 && off < 0.45f) {
                light = fmaxf(light, (1 - d / 450) * (1 - off / 0.45f) * 1.6f);
            }
            light = fminf(light, 1);
        }
        if (i == 0 && dark0) {
            light = 0.35f;  // (the own lights of the tug)
        }
        // the sun side in the frame of the body
        float sun_y = sinf(s_sun - ang);
        draw_plasma(i, sx, sy, k);
        if (i == 0) {
            draw_flame(sx, sy, ang, k, s_engine);
            draw_ship(SHP_TUG, (int)(sizeof SHP_TUG / sizeof SHP_TUG[0]), sx, sy, ang, k, light, sun_y, b->heat);
            // the green light of the tug blinks
            if (fsin(s_cam_t * 5) > 0.6f) {
                float a = ang + (-PI_F / 2 - s_croll), c = cosf(a), s = sinf(a);
                tfd_glow(sx + (-2 * c + 5.6f * s) * k, sy + (-2 * s - 5.6f * c) * k, 4 + 2 * k, rgb(93, 255, 138), 220);
            }
            s_tug_k = k;
            s_tug_sx = sx;
            s_tug_sy = sy;
            continue;
        }
        int n;
        const part3_t *P = shape_parts(s_shape[i], &n);
        draw_ship(P, n, sx, sy, ang, k, light, sun_y, b->heat);
        // the red beacon (all targets; in the dark it is the only thing that shows)
        if (b->kind != K_DEBRIS && fsin(s_cam_t * 4 + i) > 0.3f) {
            float a = ang + (-PI_F / 2 - s_croll), c = cosf(a), s = sinf(a);
            float bx = -half * 0.6f, by = -half * 0.2f;
            tfd_glow(sx + (bx * c - by * s) * k, sy + (bx * s + by * c) * k, 4 + 3 * k, rgb(255, 58, 42), 240);
        }
    }
}
// the tether: the harpoon in flight, or the rope (taut: a line; slack: a curve)
static void draw_tether(void)
{
    if (T.state == TH_NONE) {
        return;
    }
    float nw = nose_world(), k = s_tug_k;
    float a = nw + (-PI_F / 2 - s_croll), c = cosf(a), s = sinf(a);
    float hx0 = s_tug_sx + c * 13 * k, hy0 = s_tug_sy + s * 13 * k;
    float ex, ey;
    if (T.state == TH_ON) {
        int i = T.to;
        double hxw, hyw;
        hook_world(&B[i], &hxw, &hyw);
        // (a body drawn bigger than its true size: its hook on the drawing)
        float bsx, bsy;
        body_screen(i, &bsx, &bsy);
        float bk = body_k(i), ba = B[i].ang + (-PI_F / 2 - s_croll), bc = cosf(ba), bs = sinf(ba);
        ex = bsx + (B[i].hx * bc - B[i].hy * bs) * bk;
        ey = bsy + (B[i].hx * bs + B[i].hy * bc) * bk;
        (void)hxw;
        (void)hyw;
    } else {
        float sx, sy;
        v2s(T.px, T.py, &sx, &sy);
        ex = s_tug_sx + sx;
        ey = s_tug_sy + sy;
    }
    float dx = ex - hx0, dy = ey - hy0, d = fsqrt(dx * dx + dy * dy);
    uint16_t cy = T.tension > ROPE_MAX * 0.6f ? C_RED : C_CYA;
    if (T.state == TH_ON && T.tension < 50 && d > 2) {
        // (slack: a sag to the side, as long as the rope is longer than the way)
        float ropepx = T.L * s_cs * 0.001f;
        float sag = ropepx > d ? fsqrt(ropepx * ropepx - d * d) * 0.35f : 0;
        sag = fminf(sag, 30) * (0.8f + 0.2f * fsin(s_cam_t * 3));
        float mx = (hx0 + ex) / 2 - dy / d * sag, my = (hy0 + ey) / 2 + dx / d * sag;
        tfd_line(hx0, hy0, mx, my, 2.6f, cy, 50);
        tfd_line(mx, my, ex, ey, 2.6f, cy, 50);
        tfd_line(hx0, hy0, mx, my, 1.0f, cy, 230);
        tfd_line(mx, my, ex, ey, 1.0f, cy, 230);
    } else {
        tfd_line(hx0, hy0, ex, ey, 2.8f, cy, 60);
        tfd_line(hx0, hy0, ex, ey, 1.0f, cy, 240);
    }
    if (T.state == TH_FLY) {
        tfd_disc(ex, ey, 1.6f, C_WHT, 256);
    }
}
static void draw_parts(void)
{
    float k = s_cs * 0.001f;
    if (k < 0.08f) {
        return;  // (far views: the particles are smaller than a pixel)
    }
    for (int i = 0; i < s_npart; i++) {
        const part_t *p = &s_part[i];
        float sx, sy;
        v2s(p->x, p->y, &sx, &sy);
        sx += s_tug_sx;
        sy += s_tug_sy;
        if (sx < -10 || sx > SW + 10 || sy < -10 || sy > SH + 10) {
            continue;
        }
        float t = p->life / p->max;
        switch (p->kind) {
        // (the puffs and the smoke: at most 5 px; a big soft disc reads and writes each pixel of the frame)
        case P_PUFF:
            tfd_disc(sx, sy, fminf((0.8f + t * 4) * fmaxf(k, 0.4f), 4), rgb(220, 235, 255), (int)(130 * (1 - t)));
            break;
        case P_SMOKE:
            tfd_disc(sx, sy, fminf((1 + t * 6) * fmaxf(k, 0.4f), 5), rgb(200, 215, 235), (int)(90 * (1 - t)));
            break;
        case P_SPARK:
            tfd_px((int)sx, (int)sy, rgb(255, (int)(200 - t * 120), 90), (int)(256 * (1 - t)));
            break;
        case P_EMBER:
            tfd_disc(sx, sy, 1.2f, rgb(255, (int)(170 - t * 100), 60), (int)(220 * (1 - t)));
            break;
        default:
            tfd_disc(sx, sy, 1 + t * 5, rgb(230, 240, 255), (int)(70 * (1 - t)));
            break;
        }
    }
}
// the recovery ship of the landing missions (on the planet, it turns with it) and the end of the fall
static float s_land_ia_show, s_land_it_show;
static int s_land_ok_show;
static void draw_land_marks(float ps)
{
    if (MIS[s_mis].goal_kind != G_LAND) {
        return;
    }
    float za = zone_angle(s_clock), r = R_PL;
    float dx = (float)((double)(r * cosf(za)) - s_cx) * ps, dy = (float)((double)(r * sinf(za)) - s_cy) * ps;
    float x = vcx() + dx * s_cph_c - dy * s_cph_s + s_shx, y = vcy() + dx * s_cph_s + dy * s_cph_c + s_shy;
    if (x > -20 && x < SW + 20 && y > -20 && y < SH + 20) {
        float p = 0.6f + 0.4f * fsin(s_cam_t * 4);
        tfd_glow(x, y, 10, C_CYA, (int)(160 * p));
        tfd_disc(x, y, 3, C_CYA, 256);
        ctext_c(&KF_LABEL, (int)x, (int)y - 8, TR("SHIP", "ספינה"), C_CYA);
    }
    if (s_land_ok_show) {
        float ia = s_land_ia_show;
        float lx = (float)((double)(r * cosf(ia)) - s_cx) * ps, ly = (float)((double)(r * sinf(ia)) - s_cy) * ps;
        float X = vcx() + lx * s_cph_c - ly * s_cph_s, Y = vcy() + lx * s_cph_s + ly * s_cph_c;
        if (X > -10 && X < SW + 10 && Y > -10 && Y < SH + 10) {
            tfd_line(X - 5, Y - 5, X + 5, Y + 5, 2, C_AMB, 240);
            tfd_line(X - 5, Y + 5, X + 5, Y - 5, 2, C_AMB, 240);
        }
    }
}
// the whole world of a frame
static int64_t s_prof[12];  // the time of each part of the frame (us, the speed log): sky, planet, far, ships, hud,
                            // update, marks, keys; [11] the fill of the sky
static inline int64_t prof_t(void)
{
    return esp_timer_get_time();
}
static void draw_world(void)
{
    int64_t t0 = prof_t();
    float so = scale_orbit(), ps = s_cs < so ? s_cs : so;
    // (the hole of the sky: the opaque inner disc of the planet, 2 px in from the limb)
    s_hole_r = s_tex ? R_PL * ps - 2 : 0;
    planet_centre(ps, &s_hole_x, &s_hole_y);
    draw_sky();
    int64_t t1 = prof_t();
    s_prof[0] += t1 - t0;
    s_prof[11] += s_fill_us;
    s_fill_us = 0;
    planet_light();
    draw_planet(ps);
    t0 = prof_t();
    s_prof[1] += t0 - t1;
    // the planet centre at the scale of the planet (for the rings of the goals and the air)
    float dx = (float)(0 - s_cx) * ps, dy = (float)(0 - s_cy) * ps;
    float pcx = vcx() + dx * s_cph_c - dy * s_cph_s + s_shx, pcy = vcy() + dx * s_cph_s + dy * s_cph_c + s_shy;
    const mis_t *M = &MIS[s_mis];
    // the far views: the band of the goal, the line of the air, the orbits, the closest approach
    float far_a = clampf((so * 1.7f - s_cs) / (so * 0.6f), 0, 1);
    if (far_a > 0.02f) {
        if (M->goal_kind == G_BAND || M->goal_kind == G_DELIVER2) {
            ring_band(pcx, pcy, (R_PL + M->lo) * ps, (R_PL + M->hi) * ps, rgb(80, 220, 120), (int)(70 * far_a));
            ring_line(pcx, pcy, (R_PL + M->lo) * ps, rgb(120, 240, 150), (int)(150 * far_a), 0);
            ring_line(pcx, pcy, (R_PL + M->hi) * ps, rgb(120, 240, 150), (int)(150 * far_a), 0);
        }
        if (M->goal_kind == G_LIFT) {
            ring_line(pcx, pcy, (R_PL + M->lo) * ps, rgb(120, 240, 150), (int)(170 * far_a), 6);
        }
        ring_line(pcx, pcy, (R_PL + ATM_TOP) * ps, rgb(255, 140, 70), (int)(70 * far_a), 3);
        if (s_cs <= so * 1.7f) {
            for (int i = 1; i < s_nb; i++) {
                if (B[i].on && s_path[i].n > 1) {
                    draw_path(i, B[i].kind == K_DEBRIS ? C_RED : C_CYA, (int)(150 * far_a));
                }
            }
            draw_path(0, C_AMB, (int)(220 * far_a));
            if (s_ca_k >= 0 && s_ca_to > 0 && s_ca_to < s_nb) {
                float ax, ay, bx, by;
                w2s(s_path[0].x[s_ca_k], s_path[0].y[s_ca_k], &ax, &ay);
                w2s(s_path[s_ca_to].x[s_ca_k], s_path[s_ca_to].y[s_ca_k], &bx, &by);
                tfd_ring(ax, ay, 4, 1.5f, C_AMB, (int)(240 * far_a));
                tfd_ring(bx, by, 4, 1.5f, C_CYA, (int)(240 * far_a));
                fline(ax, ay, bx, by, C_WHT, (int)(160 * far_a));
            }
            // the GUIDE: the place of the next planned burn
            if (s_help == HELP_GUIDE && !s_auto && s_ap == AP_WAIT && s_ap_wait > 0 && s_pdt > 0) {
                int kb = (int)(s_ap_wait / s_pdt);
                if (kb < s_path[0].n) {
                    float gx, gy;
                    w2s(s_path[0].x[kb], s_path[0].y[kb], &gx, &gy);
                    float pts[8] = {gx, gy - 6, gx + 6, gy, gx, gy + 6, gx - 6, gy};
                    tfd_poly(pts, 4, C_GRN, (int)(250 * far_a));
                }
            }
        }
    }
    draw_land_marks(ps);
    t1 = prof_t();
    s_prof[2] += t1 - t0;
    // the near views: the way of the tug relative to the target for the next minutes
    int j;
    cam_target_body(&j);
    if (s_cs > so * 1.7f && j > 0 && T.state == TH_NONE && s_d_rel > 30) {
        draw_rel_path(j, s_close > 0.5f ? NP : NP / 4);
    }
    draw_bodies();
    draw_tether();
    draw_parts();
    s_prof[3] += prof_t() - t1;
}
