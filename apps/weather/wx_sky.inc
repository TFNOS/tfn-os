// WEATHER: the live sky (included by app_weather.c): its colours from the height of the sun at the place and from the
// clouds; the stars and the moon in its real phase; clouds that drift with the wind; rain, snow, fog and lightning.
// The task: the render hook of the app task draws the rows y0 .. y1 of the screen; the sky is SKH px high from the
// row top (top < 0 when the page scrolls up).
// The memory: the file statics (the cloud masks, the particles, the stars), made once at the first draw.
static int s_skw = 172, s_skh = 360;  // the panel of the sky: portrait 172 x 360, landscape 212 x 172
#define SKW    s_skw
#define SKH    s_skh
#define NCLOUD 7
#define NPART  220
#define NSTAR  70

typedef struct {
    float elev;   // the height of the sun (degrees)
    float cloud;  // 0..1
    float rain, snow, fog, storm;
    float wind;  // the drift to the right (px a second)
    float moon;  // the age of the moon 0..1
} sky_t;
static sky_t s_sky, s_sky_to;  // the sky now (it glides) and its target

typedef struct {
    float x, y, s, a;
    uint8_t k;
} cloud_t;
typedef struct {
    float x, y, v, ph;
} part_t;
static cloud_t s_cl[NCLOUD];
static part_t s_pt[NPART];
static struct {
    uint16_t x;
    uint8_t y, b, ph;
} s_star[NSTAR];  // (x: 16 bits, the Tab5 sky is 360 px wide)
#define CLW 112
#define CLH 52
static uint8_t s_clm[3][CLW * CLH];  // three soft cloud shapes (alpha)
static bool s_sky_ok;
static float s_flash;   // a lightning flash 0..1
static float s_bolt_t;  // the time of the bolt now (s; < 0: none)
static int16_t s_bolt[12][2];
static float s_next_flash = 4;

static float frand(void)
{
    return (float)(esp_random() & 0xFFFF) / 65536.0f;
}
static float vhash(int x, int y, int k)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)k * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65536.0f;
}
static float vnoise(float x, float y, int k)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = vhash(ix, iy, k), b = vhash(ix + 1, iy, k), c = vhash(ix, iy + 1, k), d = vhash(ix + 1, iy + 1, k);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// the mask of the cloud shape k (0..255 for each px): the np puffs px, py, pr with soft edges and some noise
static void cloud_mask(int k, int np, const float *px, const float *py, const float *pr)
{
    for (int y = 0; y < CLH; y++) {
        for (int x = 0; x < CLW; x++) {
            float best = 0;
            for (int i = 0; i < np; i++) {
                float dx = x - px[i], dy = (y - py[i]) * 1.15f, d = sqrtf(dx * dx + dy * dy) / pr[i];
                float v = d < 1 ? 1 - d * d : 0;
                if (v > best) {
                    best = v;
                }
            }
            // a flat base: less at the bottom edge; a noise of 3 octaves makes the edge ragged and soft
            float fy = (float)y / CLH;
            if (fy > 0.75f) {
                best *= 1 - (fy - 0.75f) * 3.6f;
            }
            float nz = 0.55f * vnoise(x / 14.0f, y / 10.0f, k) + 0.3f * vnoise(x / 6.0f, y / 5.0f, k + 7) +
                       0.15f * vnoise(x / 3.0f, y / 2.5f, k + 13);
            float dd = best * (0.55f + 0.75f * nz);
            float v = (dd - 0.18f) / 0.5f;
            v = v < 0 ? 0 : v > 1 ? 1 : v * v * (3 - 2 * v);
            int a = (int)(v * 255);
            s_clm[k][y * CLW + x] = (uint8_t)(a < 0 ? 0 : a > 255 ? 255 : a);
        }
    }
}

static void sky_init(void)
{
    if (s_sky_ok) {
        return;
    }
    // the cloud shapes: puffs with soft edges and some noise
    for (int k = 0; k < 3; k++) {
        float px[9], py[9], pr[9];
        int np = 6 + k;
        for (int i = 0; i < np; i++) {
            px[i] = CLW * (0.18f + 0.64f * i / (np - 1)) + (frand() - 0.5f) * 10;
            py[i] = CLH * (0.62f - 0.28f * sinf(3.14159f * i / (np - 1))) + (frand() - 0.5f) * 6;
            pr[i] = CLH * (0.22f + 0.16f * sinf(3.14159f * i / (np - 1))) * (0.85f + 0.3f * frand());
        }
        cloud_mask(k, np, px, py, pr);
    }
    for (int i = 0; i < NCLOUD; i++) {
        s_cl[i].x = frand() * (SKW + CLW * 2) - CLW;
        s_cl[i].y = 20 + frand() * (SKH * 0.62f);
        s_cl[i].s = 0.7f + frand() * 0.8f;
        s_cl[i].a = frand();
        s_cl[i].k = (uint8_t)(i % 3);
    }
    for (int i = 0; i < NPART; i++) {
        s_pt[i].x = frand() * SKW;
        s_pt[i].y = frand() * SKH;
        s_pt[i].v = 0.7f + 0.6f * frand();
        s_pt[i].ph = frand() * 6.28f;
    }
    for (int i = 0; i < NSTAR; i++) {
        s_star[i].x = (uint8_t)(frand() * SKW);
        s_star[i].y = (uint8_t)(frand() * SKH * 0.65f);
        s_star[i].b = (uint8_t)(90 + frand() * 165);
        s_star[i].ph = (uint8_t)(frand() * 255);
    }
    s_sky_ok = true;
}

// the target sky of a place now
static void sky_target(const wx_t *w, const place_t *pl, int64_t now)
{
    sky_t *k = &s_sky_to;
    memset(k, 0, sizeof *k);
    k->elev = now ? sun_elev(pl->lat, pl->lon, now) : 30;
    k->moon = now ? moon_age(now) : 0.5f;
    if (!w || !w->fetched) {
        k->cloud = 0.2f;
        return;
    }
    const cond_t *c = cond_of(w->code);
    float cc = isnan(w->cloud) ? 0.3f : w->cloud / 100.0f;
    k->cloud = cc;
    switch (c->sky) {
    case SK_DRIZZLE:
        k->rain = 0.25f;
        break;
    case SK_RAIN:
        k->rain = 0.6f;
        break;
    case SK_HEAVY:
        k->rain = 1.0f;
        break;
    case SK_SNOW:
        k->snow = w->code == 75 || w->code == 86 ? 1.0f : 0.55f;
        break;
    case SK_FOG:
        k->fog = 1.0f;
        break;
    case SK_STORM:
        k->rain = 0.9f;
        k->storm = 1.0f;
        break;
    default:
        break;
    }
    if (k->rain > 0 || k->snow > 0 || k->storm > 0) {
        k->cloud = cc > 0.85f ? cc : 0.85f;
    }
    // the wind: the part of it that blows to the east moves the clouds to the right (the north is up)
    float ws = isnan(w->wind) ? 5 : w->wind, wd = isnan(w->wdir) ? 270 : w->wdir;
    k->wind = -sinf(wd * 0.0174533f) * (2 + ws * 0.45f);
}

static float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
static void sky_step(float dt)
{
    float k = 1 - expf(-dt * 1.5f);
    s_sky.elev = lerp(s_sky.elev, s_sky_to.elev, k);
    s_sky.cloud = lerp(s_sky.cloud, s_sky_to.cloud, k);
    s_sky.rain = lerp(s_sky.rain, s_sky_to.rain, k);
    s_sky.snow = lerp(s_sky.snow, s_sky_to.snow, k);
    s_sky.fog = lerp(s_sky.fog, s_sky_to.fog, k);
    s_sky.storm = lerp(s_sky.storm, s_sky_to.storm, k);
    s_sky.wind = lerp(s_sky.wind, s_sky_to.wind, k);
    s_sky.moon = s_sky_to.moon;
    // the clouds drift; one that leaves comes in at the other side
    for (int i = 0; i < NCLOUD; i++) {
        cloud_t *c = &s_cl[i];
        c->x += s_sky.wind * dt * (0.6f + 0.25f * c->s);
        float w = CLW * c->s;
        if (c->x > SKW + 10) {
            c->x = -w - 10;
            c->y = 20 + frand() * SKH * 0.62f;
        }
        if (c->x < -w - 12) {
            c->x = SKW + 8;
            c->y = 20 + frand() * SKH * 0.62f;
        }
    }
    // the rain falls fast, the snow slowly and with a sway
    for (int i = 0; i < NPART; i++) {
        part_t *p = &s_pt[i];
        if (s_sky.snow > 0.05f) {
            p->y += (28 + 14 * p->v) * dt;
            p->ph += dt * 1.5f;
            p->x += (s_sky.wind * 0.6f + sinf(p->ph) * 9) * dt;
        } else {
            p->y += (420 + 220 * p->v) * dt;
            p->x += s_sky.wind * 3 * dt;
        }
        if (p->y > SKH) {
            p->y -= SKH;
            p->x = frand() * SKW;
        }
        if (p->x < 0) {
            p->x += SKW;
        }
        if (p->x >= SKW) {
            p->x -= SKW;
        }
    }
    // the lightning
    s_flash *= expf(-dt * 9);
    if (s_bolt_t >= 0) {
        s_bolt_t += dt;
        if (s_bolt_t > 0.18f) {
            s_bolt_t = -1;
        }
    }
    if (s_sky.storm > 0.3f) {
        s_next_flash -= dt;
        if (s_next_flash <= 0) {
            s_flash = 1;
            s_bolt_t = 0;
            s_next_flash = 3 + frand() * 6;
            float x = SKW * (0.2f + 0.6f * frand()), y = 40;
            for (int i = 0; i < 12; i++) {
                s_bolt[i][0] = (int16_t)x;
                s_bolt[i][1] = (int16_t)y;
                x += (frand() - 0.5f) * 26;
                y += 14 + frand() * 10;
            }
        }
    }
}

// the colours of the sky: anchors by the height of the sun (a clear sky), then grey for the clouds
typedef struct {
    float e;
    uint8_t t[3], h[3];
} sky_anchor_t;
static const sky_anchor_t SKA[] = {
        {-18, {5, 8, 22}, {12, 18, 42}},       {-11, {9, 13, 38}, {28, 34, 72}},
        {-6, {22, 28, 76}, {118, 74, 112}},    {-2, {38, 56, 118}, {236, 124, 92}},
        {2, {58, 98, 168}, {252, 172, 104}},   {8, {48, 118, 208}, {176, 204, 236}},
        {30, {34, 106, 212}, {138, 192, 244}},
};
static void sky_cols(float *top, float *hor)
{
    float e = s_sky.elev;
    int n = (int)(sizeof SKA / sizeof SKA[0]);
    int i = 0;
    while (i < n - 2 && e > SKA[i + 1].e) {
        i++;
    }
    float t = (e - SKA[i].e) / (SKA[i + 1].e - SKA[i].e);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    for (int c = 0; c < 3; c++) {
        top[c] = lerp(SKA[i].t[c], SKA[i + 1].t[c], t);
        hor[c] = lerp(SKA[i].h[c], SKA[i + 1].h[c], t);
    }
    // the clouds: towards grey (light in the day, dark at night); rain and storm darker
    float day = e > 6 ? 1 : e < -8 ? 0 : (e + 8) / 14;
    float gt[3] = {lerp(16, 112, day), lerp(18, 124, day), lerp(26, 142, day)};
    float gh[3] = {lerp(30, 166, day), lerp(32, 174, day), lerp(42, 188, day)};
    float a = powf(s_sky.cloud, 1.4f) * 0.88f;
    float dark = 1 - 0.32f * s_sky.rain - 0.28f * s_sky.storm - 0.1f * s_sky.snow;
    for (int c = 0; c < 3; c++) {
        top[c] = lerp(top[c], gt[c], a) * dark;
        hor[c] = lerp(hor[c], gh[c], a) * dark;
    }
    if (s_flash > 0.02f) {
        for (int c = 0; c < 3; c++) {
            top[c] = lerp(top[c], 230, s_flash * 0.7f);
            hor[c] = lerp(hor[c], 235, s_flash * 0.7f);
        }
    }
}

// the sky in the rows y0 .. y1 of the screen (top = the screen row of the top of the sky); the rows under the sky get
// the colour of the page
static uint16_t s_page_c;  // the colour of the page under the sky (from the sky: the cards stand on it)
static void sky_page(void)
{
    float tc[3], hc[3];
    sky_cols(tc, hc);
    s_page_c = tfd_rgb((int)(hc[0] * 0.12f + 8), (int)(hc[1] * 0.12f + 10), (int)(hc[2] * 0.14f + 16));
}
// the gradient: top colour, a curve to the horizon colour, then a fade to the page colour at the bottom
static void sky_grad(int top, int y0, int y1, const float *tc, const float *hc)
{
    int pr, pg, pb;
    tfd_unrgb(s_page_c, &pr, &pg, &pb);
    for (int y = y0; y < y1; y++) {
        int sy = y - top;
        uint16_t c;
        if (sy >= SKH) {
            c = s_page_c;
        } else {
            float f = (float)sy / SKH;
            float k = f * f * (3 - 2 * f);
            float r = lerp(tc[0], hc[0], k), g = lerp(tc[1], hc[1], k), b = lerp(tc[2], hc[2], k);
            if (f > 0.72f) {
                float q = (f - 0.72f) / 0.28f;
                q = q * q;
                r = lerp(r, (float)pr, q);
                g = lerp(g, (float)pg, q);
                b = lerp(b, (float)pb, q);
            }
            // (a dither of two colours in a row: no bands of the 16-bit colour)
            c = tfd_rgb((int)r, (int)g, (int)b);
            uint16_t c2 = tfd_rgb((int)(r + 4), (int)(g + 2), (int)(b + 4));
            uint16_t *o = &tfd_fb[y * tfd_w];
            for (int x = 0; x < SKW; x++) {
                o[x] = ((x + y) & 1) ? c : c2;
            }
            continue;
        }
        tfd_hspan(0, SKW, y, c, 256);
    }
}

// the stars at night (night, clear: 0..1)
static void sky_stars(int top, float t, float night, float clear)
{
    for (int i = 0; i < NSTAR; i++) {
        float tw = 0.6f + 0.4f * sinf(t * (1.3f + (s_star[i].ph & 7) * 0.2f) + s_star[i].ph);
        int a = (int)(s_star[i].b * tw * night * clear);
        int x = s_star[i].x, y = top + s_star[i].y;
        tfd_pxadd(x, y, tfd_rgb(255, 255, 240), a);
        if (s_star[i].b > 220) {
            tfd_pxadd(x + 1, y, tfd_rgb(255, 255, 240), a / 3);
            tfd_pxadd(x, y + 1, tfd_rgb(255, 255, 240), a / 3);
        }
    }
}

// the moon in its phase, with a glow (night: 0..1)
static void sky_moon(int top, float night)
{
    float mx = SKW * 0.27f, my = top + SKH * 0.2f, mr = 11;
    float vis = night * (1 - 0.8f * s_sky.cloud);
    tfd_glow(mx, my, 46, tfd_rgb(120, 130, 170), (int)(70 * vis));
    float ph = s_sky.moon * 6.2832f, lx = sinf(ph), lz = -cosf(ph);
    for (int y = (int)(my - mr - 1); y <= (int)(my + mr + 1); y++) {
        for (int x = (int)(mx - mr - 1); x <= (int)(mx + mr + 1); x++) {
            float dx = (x + 0.5f - mx) / mr, dy = (y + 0.5f - my) / mr, q = dx * dx + dy * dy;
            if (q >= 1.08f) {
                continue;
            }
            float nz = q < 1 ? sqrtf(1 - q) : 0;
            float lit = dx * lx + nz * lz;
            int edge = q > 1 ? (int)((1.08f - q) / 0.08f * 256) : 256;
            uint16_t c = lit > 0.05f    ? tfd_rgb(242, 238, 214)
                         : lit > -0.05f ? tfd_rgb(160, 160, 150)
                                        : tfd_rgb(42, 46, 64);
            tfd_px(x, y, c, (int)(edge * vis));
        }
    }
}

// the sun (behind the clouds) at the elevation e
static void sky_sun(int top, float e)
{
    float sx = SKW * 0.7f, sy = top + SKH * (0.62f - 0.4f * (e > 50 ? 50 : e) / 50);
    float vis = (e > 0 ? 1 : (e + 5) / 5) * (1 - 0.85f * s_sky.cloud);
    float warm = e < 10 ? 1 - e / 10 : 0;
    uint16_t gc = tfd_rgb(255, (int)lerp(236, 150, warm < 0 ? 0 : warm), (int)lerp(190, 70, warm < 0 ? 0 : warm));
    tfd_glow(sx, sy, 78, gc, (int)(90 * vis));
    tfd_glow(sx, sy, 30, tfd_rgb(255, 250, 230), (int)(120 * vis));
    tfd_disc(sx, sy, 13, tfd_rgb(255, 252, 236), (int)(256 * vis));
}

// one cloud: its mask scaled, the colour from tc (the top) to bc (the base) row by row
static void sky_cloud(const cloud_t *c, int top, const int *tc, const int *bc)
{
    float sc = c->s * (0.9f + 0.4f * s_sky.cloud);
    int cw = (int)(CLW * sc), ch = (int)(CLH * sc);
    int cx0 = (int)c->x, cy0 = top + (int)c->y;
    int ca = (int)(256 * (0.55f + 0.45f * s_sky.cloud) * (0.75f + 0.25f * c->a));
    const uint8_t *m = s_clm[c->k];
    for (int y = 0; y < ch; y++) {
        int yy = cy0 + y;
        if (yy < tfd_cy0 || yy >= tfd_cy1) {
            continue;
        }
        int my = y * CLH / ch;
        int k = my * 256 / CLH;
        uint16_t col = tfd_rgb(tc[0] + (bc[0] - tc[0]) * k / 256, tc[1] + (bc[1] - tc[1]) * k / 256,
                               tc[2] + (bc[2] - tc[2]) * k / 256);
        uint16_t *o = &tfd_fb[yy * tfd_w];
        const uint8_t *mr = &m[my * CLW];
        int xa = cx0 < 0 ? -cx0 : 0, xb = cx0 + cw > SKW ? SKW - cx0 : cw;
        for (int x = xa; x < xb; x++) {
            int a = mr[x * CLW / cw];
            if (a > 8) {
                o[cx0 + x] = tfd_mix(o[cx0 + x], col, a * ca >> 8);
            }
        }
    }
}

// the clouds: a light top and a darker base; at sunset the base is warm
static void sky_clouds(int top, float e)
{
    float day = e > 6 ? 1 : e < -8 ? 0 : (e + 8) / 14;
    float sunset = e > -6 && e < 8 ? 1 - fabsf(e - 1) / 7 : 0;
    float dark = 1 - 0.4f * s_sky.rain - 0.3f * s_sky.storm;
    const int tc[3] = {(int)(lerp(64, 250, day) * dark), (int)(lerp(70, 252, day) * dark),
                       (int)(lerp(92, 255, day) * dark)};
    const int bc[3] = {(int)(lerp(30, 176, day) * dark + 70 * sunset), (int)(lerp(34, 182, day) * dark + 20 * sunset),
                       (int)(lerp(50, 198, day) * dark)};
    int ncl = (int)(2 + s_sky.cloud * (NCLOUD - 2) + 0.5f);
    for (int i = 0; i < ncl && i < NCLOUD; i++) {
        sky_cloud(&s_cl[i], top, tc, bc);
    }
}

// the fog: soft bands that drift
static void sky_fog(int top, float t)
{
    for (int b = 0; b < 3; b++) {
        float cy = top + SKH * (0.45f + 0.16f * b) + 6 * sinf(t * 0.3f + b);
        for (int y = (int)cy - 26; y < (int)cy + 26; y++) {
            float q = 1 - fabsf(y - cy) / 26;
            int a = (int)(q * q * 120 * s_sky.fog);
            if (a > 4) {
                tfd_hspan(0, SKW, y, tfd_rgb(196, 202, 212), a);
            }
        }
    }
}

// the rain and the snow
static void sky_fall(int top)
{
    int np = (int)(NPART * (s_sky.rain > s_sky.snow ? s_sky.rain : s_sky.snow));
    if (s_sky.snow > 0.05f) {
        for (int i = 0; i < np; i++) {
            int x = (int)s_pt[i].x, y = top + (int)s_pt[i].y;
            int a = (int)(150 + 100 * s_pt[i].v);
            tfd_px(x, y, tfd_rgb(255, 255, 255), a);
            if (s_pt[i].v > 1.0f) {
                tfd_px(x + 1, y, tfd_rgb(255, 255, 255), a / 2);
                tfd_px(x, y + 1, tfd_rgb(255, 255, 255), a / 2);
            }
        }
    } else if (np > 0) {
        float dx = s_sky.wind * 3 / 620;  // (the slant of a drop: its x for each px of fall)
        uint16_t rc = tfd_rgb(196, 214, 240);
        for (int i = 0; i < np; i++) {
            float x = s_pt[i].x, y = top + s_pt[i].y;
            int len = (int)(9 + 6 * s_pt[i].v);
            for (int k = 0; k < len; k++) {
                tfd_px((int)(x - dx * k), (int)(y - k), rc, 30 + 70 * k / len);
            }
        }
    }
}

static void sky_draw(int top, int y0, int y1, float t)
{
    float tc[3], hc[3];
    sky_cols(tc, hc);
    sky_page();
    sky_grad(top, y0, y1, tc, hc);
    if (top + SKH <= y0) {
        return;
    }
    tfd_clip(0, y0 > top ? y0 : top, SKW, y1 < top + SKH ? y1 : top + SKH);
    float e = s_sky.elev;
    // the stars and the moon at night
    float night = e < -4 ? (e < -10 ? 1 : (-4 - e) / 6) : 0;
    float clear = 1 - s_sky.cloud;
    if (night > 0.02f && clear > 0.1f) {
        sky_stars(top, t, night, clear);
    }
    if (night > 0.02f && s_sky.moon > 0.03f && s_sky.moon < 0.97f) {
        sky_moon(top, night);
    }
    if (e > -5) {
        sky_sun(top, e);
    }
    if (s_sky.cloud > 0.05f) {
        sky_clouds(top, e);
    }
    if (s_sky.fog > 0.05f) {
        sky_fog(top, t);
    }
    sky_fall(top);
    // the bolt of the lightning
    if (s_bolt_t >= 0) {
        for (int i = 0; i < 11; i++) {
            tfd_line(s_bolt[i][0], top + s_bolt[i][1], s_bolt[i + 1][0], top + s_bolt[i + 1][1], 2.2f,
                     tfd_rgb(255, 255, 255), 256);
            tfd_line(s_bolt[i][0], top + s_bolt[i][1], s_bolt[i + 1][0], top + s_bolt[i + 1][1], 6,
                     tfd_rgb(180, 190, 255), 60);
        }
    }
    tfd_noclip();
}
