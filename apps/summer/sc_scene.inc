// SUMMER CUP: the far background of each scene (included by app_summer.c). The sky, the far hills, the cliffs of
// the coast and the far sea are ONE pass of rows (each pixel one time, only above the near layer: s_ntop). Then the
// sun, the glitter of the sea, the gulls and the pier draw with the far clip. The near layer of each event comes
// after (its own pass of rows, from s_ntop down).

typedef struct {
    col3 sky_top, sky_hor, sun_haze;   // the sky gradient and the warm haze near the horizon
    col3 sea_far, sea_near;            // the far sea from the horizon down
    col3 hill, cliff_lit, cliff_dark;  // the far hills (haze) and the cliffs of the coast
    col3 sun, sun_glow;
    float sun_fx, sun_up;  // the sun: its far x and its height over the horizon (px at the base zoom)
    float sun_r;           // its radius (px at the base zoom)
    float hor_y;           // the world height of the horizon line (m)
    float cliff_side;      // -1: the cliffs at the left, +1: at the right
    bool pier;
} pal_t;

static const pal_t PAL_BOWL = {
        {58, 140, 232},
        {188, 226, 248},
        {255, 236, 200},
        {38, 118, 186},
        {84, 168, 214},
        {150, 186, 214},
        {222, 168, 120},
        {160, 104, 82},
        {255, 250, 228},
        {255, 236, 170},
        3.4f,
        120,
        15,
        5.6f,
        -1,
        false,
};
static const pal_t PAL_SURF = {
        {72, 158, 238},
        {214, 236, 246},
        {255, 240, 214},
        {28, 104, 168},
        {44, 140, 192},
        {160, 196, 220},
        {214, 176, 136},
        {148, 112, 96},
        {255, 252, 236},
        {255, 240, 190},
        -3.0f,
        84,
        14,
        1.3f,
        1,
        true,
};
static const pal_t PAL_SKIM = {
        {66, 104, 196},
        {255, 190, 126},
        {255, 150, 90},
        {58, 92, 150},
        {96, 128, 170},
        {184, 146, 160},
        {226, 140, 96},
        {120, 70, 76},
        {255, 236, 170},
        {255, 170, 90},
        2.6f,
        22,
        22,
        1.2f,
        -1,
        true,
};
static const pal_t *s_pal = &PAL_SURF;

// the per-frame state of the far layers
static int s_hor;                     // the screen row of the horizon
static int16_t s_r1[660], s_r2[660];  // the top row of the far hills and of the cliffs in each column
static uint8_t s_r2a[660];            // the cover of the edge pixel of the cliffs (0..255)
static int s_cbot;                    // the bottom row of the cliffs (the water line under them)
static dc_t s_skyd[MAXH], s_seard[MAXH], s_cliffd[MAXH], s_hilld[MAXH];
static float s_far_k;  // the px of one far unit
static float s_t;      // the time of the scene (s)

static float s_gx, s_gy, s_gr;  // the glow of the sun: the middle and the radius (px)
static uint8_t s_glut[65];      // the light of the glow by (d / r)^2 in 64 steps (0..255)
static inline float far_x(int x)
{
    return (x - s_vcx) / s_far_k + cam_x * 0.12f;
}
static inline float far_sx(float fx)
{
    return s_vcx + (fx - cam_x * 0.12f) * s_far_k;
}

static void far_setup(void)
{
    const pal_t *P = s_pal;
    s_far_k = cam_z * 0.32f;
    if (s_far_k < 6) {
        s_far_k = 6;
    }
    float hy = s_vcy + (cam_y - P->hor_y) * cam_z * 0.42f;
    s_hor = clampi((int)hy, -40, SH + 40);
    float ztop = SH > 300 ? 0.9f : 1.3f;
    for (int y = 0; y < SH; y++) {
        float t = (float)(s_hor - y) / (SH * ztop);
        col3 c = mix3(P->sky_hor, P->sky_top, smooth01(t * 1.25f));
        if (t < 0.18f) {
            c = mix3(c, P->sun_haze, (0.18f - t) * 2.2f);
        }
        s_skyd[y] = dcol(c);
        float u = (float)(y - s_hor) / (SH * 0.55f);
        col3 s = mix3(P->sea_far, P->sea_near, smooth01(u));
        if (u < 0.05f) {
            s = mix3(s, P->sky_hor, (0.05f - u) * 9);  // (a soft line at the horizon)
        }
        s_seard[y] = dcol(s);
        float ch = (float)(s_hor - y) / (s_far_k * 3.0f);
        s_cliffd[y] = dcol(mix3(P->cliff_dark, P->cliff_lit, clampf(ch * 1.4f + 0.15f, 0, 1)));
        s_hilld[y] = dcol(mix3(P->hill, P->sky_hor, 0.25f + 0.25f * clampf(ch, 0, 1)));
    }
    s_cbot = s_hor + (int)(s_far_k * 0.25f) + 1;
    {
        float zk = s_far_k / 14.0f;
        float sr = P->sun_r * (0.6f + 0.4f * zk);
        s_gx = far_sx(P->sun_fx);
        s_gy = s_hor - P->sun_up * zk;
        s_gr = sr * 4.2f;
        for (int i = 0; i <= 64; i++) {
            float q = 1 - i / 64.0f;                                      // (1 - (d / r)^2)
            float inner = 1 - i / 64.0f * (4.2f * 4.2f) / (1.9f * 1.9f);  // (the small strong glow)
            float v = 90 * q * q + (inner > 0 ? 150 * inner * inner : 0);
            s_glut[i] = (uint8_t)clampi((int)v, 0, 255);
        }
    }
    for (int x = 0; x < SW; x++) {
        float fx = far_x(x);
        float h1 = 0.55f + 0.32f * fsin(fx * 0.29f + 1.2f) + 0.18f * fsin(fx * 0.83f + 0.4f) + 0.05f * fsin(fx * 3.1f);
        s_r1[x] = (int16_t)(s_hor - h1 * s_far_k);
        // the cliffs: a headland at one side, with steps of rock
        float side = fx * P->cliff_side;
        float hc = side > 1.5f ? 1.2f + 0.6f * smooth01((side - 1.5f) * 0.6f) +
                                         0.35f * fsin(fx * 1.7f) * smooth01(side - 2.5f) + 0.12f * fsin(fx * 5.3f + 2)
                               : -1;
        if (side > 1.5f && side < 2.6f) {
            hc *= smooth01((side - 1.5f) / 1.1f);
        }
        if (hc > 0.02f) {
            float ry = s_hor - hc * s_far_k;
            int iy = (int)ceilf(ry);
            s_r2[x] = (int16_t)iy;
            s_r2a[x] = (uint8_t)clampi((int)((iy - ry) * 255), 0, 255);
        } else {
            s_r2[x] = 30000;
            s_r2a[x] = 0;
        }
    }
}

// the far rows: each pixel above the near layer, one time
static void far_raster(void)
{
    // the rows over all far things and over the near layer: one fill of sky; under the cliffs: one fill of sea
    int ntop_min = SH, ridge_min = SH;
    for (int x = 0; x < SW; x++) {
        if (s_ntop[x] < ntop_min) {
            ntop_min = s_ntop[x];
        }
        if (s_r1[x] < ridge_min) {
            ridge_min = s_r1[x];
        }
        if (s_r2[x] - 1 < ridge_min) {
            ridge_min = s_r2[x] - 1;
        }
    }
    for (int y = 0; y < SH; y++) {
        int xa = s_x0[y], xb = s_x1[y];
        uint16_t *p = &g_fb[y * SW];
        const uint16_t *sk = s_skyd[y].c[y & 1], *sea = s_seard[y].c[y & 1], *cl = s_cliffd[y].c[y & 1],
                       *hl = s_hilld[y].c[y & 1];
        // the span of the glow in this row (the pixels there get the sky or the far thing plus the light of the sun)
        int gxa = SW, gxb = SW;
        float gdy = y + 0.5f - s_gy, g2 = s_gr * s_gr - gdy * gdy;
        if (g2 > 0) {
            float hw = fsq(g2);
            gxa = clampi((int)(s_gx - hw), xa, xb);
            gxb = clampi((int)(s_gx + hw) + 1, xa, xb);
        }
        if (y < ntop_min && y < ridge_min && y < s_hor) {
            if (gxa >= gxb) {
                fill2(p, xa, xb, sk);
                continue;
            }
            fill2(p, xa, gxa, sk);
            fill2(p, gxb, xb, sk);
            float ir2 = 64.0f / (s_gr * s_gr), gd2 = gdy * gdy;
            col3 gc = s_pal->sun_glow;
            for (int x = gxa; x < gxb; x++) {
                float dx = x + 0.5f - s_gx;
                int i = (int)((dx * dx + gd2) * ir2);
                p[x] = i < 64 ? addc(sk[x & 1], gc, s_glut[i]) : sk[x & 1];
            }
            continue;
        }
        if (y < ntop_min && y >= s_hor && y >= s_cbot) {
            fill2(p, xa, xb, sea);
            continue;
        }
        if (y < s_hor) {
            for (int x = xa; x < xb; x++) {
                if (y >= s_ntop[x]) {
                    continue;
                }
                if (y >= s_r2[x]) {
                    p[x] = cl[x & 1];
                } else if (y == s_r2[x] - 1 && s_r2a[x]) {
                    p[x] = mixc(y >= s_r1[x] ? hl[x & 1] : sk[x & 1], cl[x & 1], s_r2a[x]);
                } else if (y >= s_r1[x]) {
                    p[x] = hl[x & 1];
                } else {
                    p[x] = sk[x & 1];
                }
            }
            if (gxa < gxb) {
                float ir2 = 64.0f / (s_gr * s_gr), gd2 = gdy * gdy;
                col3 gc = s_pal->sun_glow;
                for (int x = gxa; x < gxb; x++) {
                    if (y >= s_ntop[x]) {
                        continue;
                    }
                    float dx = x + 0.5f - s_gx;
                    int i = (int)((dx * dx + gd2) * ir2);
                    if (i < 64) {
                        p[x] = addc(p[x], gc, s_glut[i] * 3 / 4);
                    }
                }
            }
        } else {
            for (int x = xa; x < xb; x++) {
                if (y >= s_ntop[x]) {
                    continue;
                }
                p[x] = (y >= s_r2[x] && y < s_cbot) ? cl[x & 1] : sea[x & 1];
            }
        }
    }
}

// the sun with its glow, the glitter of the sea under it, the gulls, the pier (with the far clip)
static void far_props(void)
{
    const pal_t *P = s_pal;
    s_cf = true;
    float zk = s_far_k / 14.0f;
    float sx = far_sx(P->sun_fx), sy = s_hor - P->sun_up * zk, sr = P->sun_r * (0.6f + 0.4f * zk);
    if (sy > -sr * 3 && sy < SH + sr) {
        disc(sx, sy, sr, c16(P->sun), 256);
    }
    // the glitter: short bright dashes on the sea under the sun, they move with the time
    if (s_hor < SH) {
        for (int k = 0; k < 46; k++) {
            uint32_t h = (uint32_t)k * 2654435761u;
            float dep = ((h >> 8) & 255) / 255.0f;  // 0 = at the horizon
            int y = s_hor + 1 + (int)(dep * dep * SH * 0.3f);
            if (y >= SH) {
                continue;
            }
            float spread = sr * (1.2f + dep * 3.5f);
            float ph = s_t * (0.8f + ((h >> 16) & 7) * 0.15f) + k;
            float x = sx + ((float)((h >> 3) & 255) / 255.0f - 0.5f) * 2 * spread + fsin(ph) * 3;
            int len = 2 + (int)(dep * 7);
            int a = (int)(120 * (0.5f + 0.5f * fsin(ph * 2.3f)));
            for (int i = 0; i < len; i++) {
                pxadd((int)x + i, y, P->sun, a);
            }
        }
    }
    // the pier: a long deck on posts at the horizon, a house at its end
    if (P->pier && s_hor > 0 && s_hor < SH) {
        float x0 = far_sx(-P->cliff_side * 2.2f), x1 = far_sx(-P->cliff_side * 9.0f);
        if (x0 > x1) {
            float t = x0;
            x0 = x1;
            x1 = t;
        }
        int dy = (int)(s_far_k * 0.22f) + 1;
        uint16_t pc = c16(mix3(P->cliff_dark, P->sea_far, 0.35f));
        hspan((int)x0, (int)x1, s_hor - dy, pc, 230);
        for (float x = x0; x < x1; x += s_far_k * 0.35f) {
            capsule(x, (float)(s_hor - dy), x, (float)s_hor + 1, 0.5f, 0.5f, pc, 200);
        }
        float hx = P->cliff_side < 0 ? x1 : x0;
        int hw = (int)(s_far_k * 0.5f) + 2, hh = (int)(s_far_k * 0.3f) + 2;
        for (int j = 0; j < hh; j++) {
            hspan((int)hx - hw / 2, (int)hx + hw / 2, s_hor - dy - j, pc, 230);
        }
    }
    // gulls: two thin wings that beat
    for (int k = 0; k < 3; k++) {
        float gx = s_vcx + fsin(s_t * 0.07f + k * 2.1f) * SW * 0.45f + (k - 1) * SW * 0.2f;
        float gy = SH * (0.14f + 0.07f * k) + fsin(s_t * 0.5f + k) * 6;
        float w = 4 + 2.0f * zk, fl = fsin(s_t * 7 + k * 1.3f) * w * 0.45f;
        uint16_t gc = rgb(70, 80, 100);
        capsule(gx - w, gy - fl, gx, gy + 1, 0.6f, 0.9f, gc, 220);
        capsule(gx + w, gy - fl, gx, gy + 1, 0.6f, 0.9f, gc, 220);
    }
    s_cf = false;
}

// a palm tree at the world point (x, y) with its height h (m) and its lean (to the right +): drawn in the world
static void palm(float x, float y, float h, float lean, float sway)
{
    uint16_t tc = rgb(120, 86, 58), td = rgb(86, 58, 40);
    float px = x, py = y;
    for (int i = 1; i <= 5; i++) {
        float t = i / 5.0f;
        float nx = x + lean * h * t * t + sway * 0.12f * t * t, ny = y + h * t;
        wcap(px, py, nx, ny, 0.16f - 0.06f * t, 0.15f - 0.06f * t, (i & 1) ? tc : td, 256);
        px = nx;
        py = ny;
    }
    // the leaves: curved fronds (capsule chains) that fall from the top
    uint16_t lc = rgb(44, 132, 70), ld = rgb(28, 96, 56);
    for (int k = 0; k < 6; k++) {
        float a = -2.6f + k * 1.04f + sway * 0.08f * fsin(s_t * 1.3f + k);
        float dx = fcos(a), dy = fsin(a) * 0.45f + 0.35f;
        float qx = px, qy = py;
        for (int i = 1; i <= 3; i++) {
            float t = i / 3.0f;
            float nx = px + dx * h * 0.32f * t, ny = py + dy * h * 0.32f * t - h * 0.22f * t * t;
            wcap(qx, qy, nx, ny, 0.12f * (1.1f - t), 0.11f * (1.1f - t), (k & 1) ? lc : ld, 256);
            qx = nx;
            qy = ny;
        }
    }
    wdisc(px, py, 0.13f, rgb(70, 50, 30), 256);
}
