// STANDBY: the live sky (standby.h), the look of the sky of WEATHER (apps/weather/wx_sky.inc) on the whole frame: the
// colours from the height of the sun at the place and from the clouds; the stars and the moon in its real phase at
// night; soft clouds that drift with the wind; rain, snow, fog and lightning from the weather code.
// A round frame: only the circle (the outside stays black). It writes g_fb directly (no clip box).
// THE COST: the still part (the gradient, the sun, the moon) is drawn into a PSRAM copy of the frame, again only when
// its colours change (the sun moves, the clouds grow, a flash of lightning); each frame copies it, then draws the
// stars, the clouds, the fog, the rain and the snow. The cloud masks are scaled to the frame once (no bilinear sample
// in each frame).
// The memory (PSRAM, made at the first draw, freed when an app opens: sb_sky_free): the masks, the stars and the drops
// (about 22 KB), the masks at the scale of the frame (17 KB on the 3.49, 100 KB on the 1.75C) and the copy of the
// still sky (one frame: 220 KB on the 3.49, 434 KB on the 1.75C).
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "standby_priv.h"

#define NCLOUD 7
#define NPART  220
#define NSTAR  90
#define CLW    112
#define CLH    52

typedef struct {
    float x, y, s, a;
    uint8_t k;
} cloud_t;
typedef struct {
    float x, y, v, ph;
} part_t;
typedef struct {
    uint8_t clm[3][CLW * CLH];  // three soft cloud shapes (alpha)
    cloud_t cl[NCLOUD];
    part_t pt[NPART];
    struct {
        float x, y;
        uint8_t b, ph;
    } st[NSTAR];
    int w, h;  // the frame of the positions
} sky_mem_t;
static sky_mem_t *s_m;
static uint8_t *s_pm;  // the 3 masks at the scale of the frame (s_pw x s_ph each)
static int s_pw, s_ph;
static uint16_t *s_bg;     // the still sky (gfx_w x gfx_h)
static float s_bg_key[6];  // the values that the still sky was drawn with
static sb_sky_t s_sky, s_to;
static bool s_first = true;
static float s_flash, s_bolt_t = -1, s_next_flash = 4;
static int16_t s_bolt[12][2];

static inline uint16_t swp(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
uint16_t sb_px_mix(uint16_t dst, uint16_t src, int a)
{
    if (a <= 0) {
        return dst;
    }
    if (a >= 256) {
        return src;
    }
    const uint16_t d = swp(dst), s = swp(src);
    const int dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
    const int sr = s >> 11, sg = (s >> 5) & 63, sbb = s & 31;
    const int r = dr + ((sr - dr) * a >> 8), g = dg + ((sg - dg) * a >> 8), b = db + ((sbb - db) * a >> 8);
    return swp((uint16_t)(r << 11 | g << 5 | b));
}
static float frand(void)
{
    return (float)(esp_random() & 0xFFFF) / 65536.0f;
}
static float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}
static float vhash(int x, int y, int k)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)k * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFF) / 65536.0f;
}
static float vnoise(float x, float y, int k)
{
    const int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    const float a = vhash(ix, iy, k), b = vhash(ix + 1, iy, k), c = vhash(ix, iy + 1, k), d = vhash(ix + 1, iy + 1, k);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// the mask of the cloud shape k: the np puffs with soft edges and some noise (as WEATHER)
static void cloud_mask(int k, int np, const float *px, const float *py, const float *pr)
{
    for (int y = 0; y < CLH; y++) {
        for (int x = 0; x < CLW; x++) {
            float best = 0;
            for (int i = 0; i < np; i++) {
                const float dx = x - px[i], dy = (y - py[i]) * 1.15f, d = sqrtf(dx * dx + dy * dy) / pr[i];
                const float v = d < 1 ? 1 - d * d : 0;
                best = v > best ? v : best;
            }
            const float fy = (float)y / CLH;
            if (fy > 0.75f) {
                best *= 1 - (fy - 0.75f) * 3.6f;
            }
            const float nz = 0.55f * vnoise(x / 14.0f, y / 10.0f, k) + 0.3f * vnoise(x / 6.0f, y / 5.0f, k + 7) +
                             0.15f * vnoise(x / 3.0f, y / 2.5f, k + 13);
            float v = (best * (0.55f + 0.75f * nz) - 0.18f) / 0.5f;
            v = v < 0 ? 0 : v > 1 ? 1 : v * v * (3 - 2 * v);
            s_m->clm[k][y * CLW + x] = (uint8_t)(v * 255);
        }
    }
}

// the scale of the clouds for this frame (the 3.49 bar is the base)
static float cl_scale(void)
{
    const float k = (float)(gfx_h < gfx_w ? gfx_h : gfx_w) / 172.0f;
    return k < 1 ? 1 : k > 2.4f ? 2.4f : k;
}

static void place_all(void)
{
    const int W = gfx_w, H = gfx_h;
    const float sc = cl_scale();
    for (int i = 0; i < NCLOUD; i++) {
        s_m->cl[i].x = frand() * (W + CLW * 2 * sc) - CLW * sc;
        s_m->cl[i].y = H * (0.04f + frand() * 0.55f);
        s_m->cl[i].s = (0.7f + frand() * 0.8f) * sc;
        s_m->cl[i].a = frand();
        s_m->cl[i].k = (uint8_t)(i % 3);
    }
    for (int i = 0; i < NPART; i++) {
        s_m->pt[i].x = frand() * W;
        s_m->pt[i].y = frand() * H;
        s_m->pt[i].v = 0.7f + 0.6f * frand();
        s_m->pt[i].ph = frand() * 6.28f;
    }
    for (int i = 0; i < NSTAR; i++) {
        s_m->st[i].x = frand() * W;
        s_m->st[i].y = frand() * H * 0.7f;
        s_m->st[i].b = (uint8_t)(90 + frand() * 165);
        s_m->st[i].ph = (uint8_t)(frand() * 255);
    }
    s_m->w = W;
    s_m->h = H;
}

// the masks at the scale of the frame: one bilinear scale now, the nearest sample in each frame
static void scale_masks(void)
{
    const float sc = cl_scale();
    heap_caps_free(s_pm);
    s_pw = (int)(CLW * sc);
    s_ph = (int)(CLH * sc);
    s_pm = heap_caps_malloc((size_t)3 * s_pw * s_ph, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pm) {
        return;
    }
    for (int k = 0; k < 3; k++) {
        const uint8_t *m = s_m->clm[k];
        for (int y = 0; y < s_ph; y++) {
            const int fy = y * (CLH - 1) * 256 / (s_ph - 1), my = fy >> 8, wy = fy & 255;
            const uint8_t *r1 = &m[my * CLW], *r2 = &m[(my + 1 < CLH ? my + 1 : my) * CLW];
            for (int x = 0; x < s_pw; x++) {
                const int fx = x * (CLW - 1) * 256 / (s_pw - 1), mx = fx >> 8, wx = fx & 255;
                const int mx2 = mx + 1 < CLW ? mx + 1 : mx;
                const int top = r1[mx] * (256 - wx) + r1[mx2] * wx, bot = r2[mx] * (256 - wx) + r2[mx2] * wx;
                s_pm[(k * s_ph + y) * s_pw + x] = (uint8_t)((top * (256 - wy) + bot * wy) >> 16);
            }
        }
    }
}

static bool sky_mem(void)
{
    if (!s_m) {
        s_m = heap_caps_malloc(sizeof *s_m, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_m) {
            return false;
        }
        for (int k = 0; k < 3; k++) {
            float px[9], py[9], pr[9];
            const int np = 6 + k;
            for (int i = 0; i < np; i++) {
                px[i] = CLW * (0.18f + 0.64f * i / (np - 1)) + (frand() - 0.5f) * 10;
                py[i] = CLH * (0.62f - 0.28f * sinf(3.14159f * i / (np - 1))) + (frand() - 0.5f) * 6;
                pr[i] = CLH * (0.22f + 0.16f * sinf(3.14159f * i / (np - 1))) * (0.85f + 0.3f * frand());
            }
            cloud_mask(k, np, px, py, pr);
        }
        s_m->w = 0;
    }
    if (s_m->w != gfx_w || s_m->h != gfx_h) {
        place_all();  // (a new frame size: the menu turned)
        scale_masks();
        heap_caps_free(s_bg);
        s_bg = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_bg_key[0] = 1e9f;  // (draw it again)
    }
    return s_pm != NULL;
}

void sb_sky_free(void)
{
    heap_caps_free(s_m);
    heap_caps_free(s_pm);
    heap_caps_free(s_bg);
    s_m = NULL;
    s_pm = NULL;
    s_bg = NULL;
}

// the target sky: the sun of the place now and the weather (no weather: a few clouds)
void sb_sky_target(const sbw_t *w, const sb_now_t *n)
{
    sb_sky_t *k = &s_to;
    memset(k, 0, sizeof *k);
    k->elev = n->elev;
    k->moon = n->moon;
    k->wind = 6;
    if (!w || !w->at) {
        k->cloud = 0.2f;
    } else {
        const float cc = isnan(w->cloud) ? 0.3f : w->cloud / 100.0f;
        k->cloud = cc;
        switch (sb_sky_kind(w->code)) {
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
        // the part of the wind that blows to the east moves the clouds to the right (the north is up)
        const float ws = isnan(w->wind) ? 5 : w->wind, wd = isnan(w->wdir) ? 270 : w->wdir;
        k->wind = -sinf(wd * 0.0174533f) * (2 + ws * 0.45f);
        if (fabsf(k->wind) < 2) {
            k->wind = k->wind < 0 ? -2 : 2;  // (the clouds always drift a little)
        }
    }
    if (s_first) {
        s_sky = s_to;  // (the first show: no glide from a sky of nothing)
        s_first = false;
    }
}

bool sb_sky_moves(void)
{
    return s_sky.cloud > 0.05f || s_sky.rain > 0.05f || s_sky.snow > 0.05f || s_sky.storm > 0.3f;
}

static void step_parts(float dt)
{
    const int W = gfx_w, H = gfx_h;
    for (int i = 0; i < NCLOUD; i++) {
        cloud_t *c = &s_m->cl[i];
        c->x += s_sky.wind * dt * (0.6f + 0.25f * c->s);
        const float w = CLW * c->s;
        if (c->x > W + 10) {
            c->x = -w - 10;
            c->y = H * (0.04f + frand() * 0.55f);
        }
        if (c->x < -w - 12) {
            c->x = (float)W + 8;
            c->y = H * (0.04f + frand() * 0.55f);
        }
    }
    for (int i = 0; i < NPART; i++) {
        part_t *p = &s_m->pt[i];
        if (s_sky.snow > 0.05f) {
            p->y += (28 + 14 * p->v) * dt;
            p->ph += dt * 1.5f;
            p->x += (s_sky.wind * 0.6f + sinf(p->ph) * 9) * dt;
        } else {
            p->y += (420 + 220 * p->v) * dt;
            p->x += s_sky.wind * 3 * dt;
        }
        if (p->y > H) {
            p->y -= H;
            p->x = frand() * W;
        }
        p->x = p->x < 0 ? p->x + W : p->x >= W ? p->x - W : p->x;
    }
}

void sb_sky_step(float dt)
{
    if (!sky_mem()) {
        return;
    }
    dt = dt > 0.5f ? 0.5f : dt;
    const float k = 1 - expf(-dt * 1.5f);
    s_sky.elev = lerpf(s_sky.elev, s_to.elev, k);
    s_sky.cloud = lerpf(s_sky.cloud, s_to.cloud, k);
    s_sky.rain = lerpf(s_sky.rain, s_to.rain, k);
    s_sky.snow = lerpf(s_sky.snow, s_to.snow, k);
    s_sky.fog = lerpf(s_sky.fog, s_to.fog, k);
    s_sky.storm = lerpf(s_sky.storm, s_to.storm, k);
    s_sky.wind = lerpf(s_sky.wind, s_to.wind, k);
    s_sky.moon = s_to.moon;
    step_parts(dt);
    s_flash *= expf(-dt * 9);
    if (s_bolt_t >= 0) {
        s_bolt_t += dt;
        s_bolt_t = s_bolt_t > 0.18f ? -1 : s_bolt_t;
    }
    if (s_sky.storm > 0.3f) {
        s_next_flash -= dt;
        if (s_next_flash <= 0) {
            s_flash = 1;
            s_bolt_t = 0;
            s_next_flash = 3 + frand() * 6;
            float x = gfx_w * (0.2f + 0.6f * frand()), y = gfx_h * 0.1f;
            for (int i = 0; i < 12; i++) {
                s_bolt[i][0] = (int16_t)x;
                s_bolt[i][1] = (int16_t)y;
                x += (frand() - 0.5f) * 26;
                y += gfx_h * 0.06f + frand() * 10;
            }
        }
    }
}

// ---------------------------------------------------------------- the drawing
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
    const float e = s_sky.elev;
    const int n = (int)(sizeof SKA / sizeof SKA[0]);
    int i = 0;
    while (i < n - 2 && e > SKA[i + 1].e) {
        i++;
    }
    float t = (e - SKA[i].e) / (SKA[i + 1].e - SKA[i].e);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    for (int c = 0; c < 3; c++) {
        top[c] = lerpf(SKA[i].t[c], SKA[i + 1].t[c], t);
        hor[c] = lerpf(SKA[i].h[c], SKA[i + 1].h[c], t);
    }
    const float day = e > 6 ? 1 : e < -8 ? 0 : (e + 8) / 14;
    const float gt[3] = {lerpf(16, 112, day), lerpf(18, 124, day), lerpf(26, 142, day)};
    const float gh[3] = {lerpf(30, 166, day), lerpf(32, 174, day), lerpf(42, 188, day)};
    const float a = powf(s_sky.cloud, 1.4f) * 0.88f;
    const float dark = 1 - 0.32f * s_sky.rain - 0.28f * s_sky.storm - 0.1f * s_sky.snow;
    for (int c = 0; c < 3; c++) {
        top[c] = lerpf(top[c], gt[c], a) * dark;
        hor[c] = lerpf(hor[c], gh[c], a) * dark;
        if (s_flash > 0.02f) {
            top[c] = lerpf(top[c], 230, s_flash * 0.7f);
            hor[c] = lerpf(hor[c], 235, s_flash * 0.7f);
        }
    }
}

// the x range of the row y that shows (a round frame: the circle)
static void row_span(int y, int *x0, int *x1)
{
    if (!gfx_round()) {
        *x0 = 0;
        *x1 = gfx_w;
        return;
    }
    const float r = gfx_w * 0.5f, dy = y + 0.5f - gfx_h * 0.5f;
    const float q = r * r - dy * dy;
    const int hw = q > 0 ? (int)ceilf(sqrtf(q)) : 0;
    *x0 = gfx_w / 2 - hw < 0 ? 0 : gfx_w / 2 - hw;
    *x1 = gfx_w / 2 + hw > gfx_w ? gfx_w : gfx_w / 2 + hw;
}

static inline void px_a(int x, int y, uint16_t c, int a)
{
    if (x < 0 || y < 0 || x >= gfx_w || y >= gfx_h || a <= 0 || (gfx_round() && !gfx_in_circle(x, y, 0))) {
        return;
    }
    uint16_t *p = &g_fb[y * gfx_w + x];
    *p = sb_px_mix(*p, c, a);
}

// the gradient: the top colour, a curve to the horizon colour, a darker band at the bottom (the text stands on it);
// two colours in each row (a dither: no bands of the 16-bit colour)
static void sky_grad(const float *tc, const float *hc)
{
    for (int y = 0; y < gfx_h; y++) {
        const float f = (float)y / gfx_h;
        const float k = f * f * (3 - 2 * f);
        float r = lerpf(tc[0], hc[0], k), g = lerpf(tc[1], hc[1], k), b = lerpf(tc[2], hc[2], k);
        if (f > 0.55f) {
            const float q = (f - 0.55f) / 0.45f * 0.45f;  // (a dark foot: up to 45 % darker at the bottom)
            r *= 1 - q;
            g *= 1 - q;
            b *= 1 - q;
        }
        const uint16_t c1 = rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
        const uint16_t c2 = rgb((uint8_t)(r + 4 > 255 ? 255 : r + 4), (uint8_t)(g + 2 > 255 ? 255 : g + 2),
                                (uint8_t)(b + 4 > 255 ? 255 : b + 4));
        int x0, x1;
        row_span(y, &x0, &x1);
        uint16_t *o = &g_fb[y * gfx_w];
        for (int x = 0; x < x0; x++) {
            o[x] = THEME_STANDBY_OUTSIDE;
        }
        for (int x = x0; x < x1; x++) {
            o[x] = ((x + y) & 1) ? c1 : c2;
        }
        for (int x = x1; x < gfx_w; x++) {
            o[x] = THEME_STANDBY_OUTSIDE;
        }
    }
}

// a soft round glow of radius r and alpha a at its middle
static void glow(float cx, float cy, float r, uint16_t c, int a)
{
    if (a <= 2) {
        return;
    }
    const int y0 = (int)(cy - r), y1 = (int)(cy + r), x0 = (int)(cx - r), x1 = (int)(cx + r);
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < gfx_h; y++) {
        for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < gfx_w; x++) {
            const float dx = (x - cx) / r, dy = (y - cy) / r, q = 1 - (dx * dx + dy * dy);
            if (q > 0) {
                px_a(x, y, c, (int)(a * q * q));
            }
        }
    }
}
static void disc(float cx, float cy, float r, uint16_t c, int a)
{
    for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++) {
        for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
            const float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            const float e = r + 0.5f - d;
            if (e > 0) {
                px_a(x, y, c, (int)(a * (e > 1 ? 1 : e)));
            }
        }
    }
}

static void sky_stars(float t, float night, float clear)
{
    for (int i = 0; i < NSTAR; i++) {
        const float tw = 0.6f + 0.4f * fsin(t * (1.3f + (s_m->st[i].ph & 7) * 0.2f) + s_m->st[i].ph);
        const int a = (int)(s_m->st[i].b * tw * night * clear);
        const int x = (int)s_m->st[i].x, y = (int)s_m->st[i].y;
        if (gfx_round() && !gfx_in_circle(x, y, 4)) {
            continue;
        }
        px_a(x, y, rgb(255, 255, 240), a);
        if (s_m->st[i].b > 220) {
            px_a(x + 1, y, rgb(255, 255, 240), a / 3);
            px_a(x, y + 1, rgb(255, 255, 240), a / 3);
        }
    }
}

static void sky_moon(float night)
{
    const float sc = cl_scale();
    const float mx = gfx_w * (gfx_round() ? 0.3f : 0.27f), my = gfx_h * 0.2f, mr = 11 * sc;
    const float vis = night * (1 - 0.8f * s_sky.cloud);
    glow(mx, my, 46 * sc, rgb(120, 130, 170), (int)(70 * vis));
    const float ph = s_sky.moon * 6.2832f, lx = fsin(ph), lz = -fcos(ph);
    for (int y = (int)(my - mr - 1); y <= (int)(my + mr + 1); y++) {
        for (int x = (int)(mx - mr - 1); x <= (int)(mx + mr + 1); x++) {
            const float dx = (x + 0.5f - mx) / mr, dy = (y + 0.5f - my) / mr, q = dx * dx + dy * dy;
            if (q >= 1.08f) {
                continue;
            }
            const float nz = q < 1 ? sqrtf(1 - q) : 0;
            const float lit = dx * lx + nz * lz;
            const int edge = q > 1 ? (int)((1.08f - q) / 0.08f * 256) : 256;
            const uint16_t c = lit > 0.05f ? rgb(242, 238, 214) : lit > -0.05f ? rgb(160, 160, 150) : rgb(42, 46, 64);
            px_a(x, y, c, (int)(edge * vis));
        }
    }
}

static void sky_sun(float e)
{
    const float sc = cl_scale();
    const float sx = gfx_w * (gfx_w > gfx_h && !gfx_round() ? 0.58f : 0.72f),
                sy = gfx_h * (0.62f - 0.42f * (e > 50 ? 50 : e) / 50);
    const float vis = (e > 0 ? 1 : (e + 5) / 5) * (1 - 0.85f * s_sky.cloud);
    float warm = e < 10 ? 1 - e / 10 : 0;
    warm = warm > 1 ? 1 : warm;
    const uint16_t gc = rgb(255, (uint8_t)lerpf(236, 150, warm), (uint8_t)lerpf(190, 70, warm));
    glow(sx, sy, 78 * sc, gc, (int)(90 * vis));
    glow(sx, sy, 30 * sc, rgb(255, 250, 230), (int)(120 * vis));
    disc(sx, sy, 13 * sc, rgb(255, 252, 236), (int)(256 * vis));
}

static void sky_cloud(const cloud_t *c, const int *tc, const int *bc)
{
    const float sc = c->s * (0.9f + 0.4f * s_sky.cloud);
    const int cw = (int)(CLW * sc), ch = (int)(CLH * sc);
    const int cx0 = (int)c->x, cy0 = (int)c->y;
    const int ca = (int)(256 * (0.55f + 0.45f * s_sky.cloud) * (0.75f + 0.25f * c->a));
    const uint8_t *m = &s_pm[c->k * s_ph * s_pw];
    for (int y = 0; y < ch; y++) {
        const int yy = cy0 + y;
        if (yy < 0 || yy >= gfx_h) {
            continue;
        }
        const int my = y * s_ph / ch, k = my * 256 / s_ph;
        const uint16_t col =
                rgb((uint8_t)(tc[0] + (bc[0] - tc[0]) * k / 256), (uint8_t)(tc[1] + (bc[1] - tc[1]) * k / 256),
                    (uint8_t)(tc[2] + (bc[2] - tc[2]) * k / 256));
        uint16_t *o = &g_fb[yy * gfx_w];
        const uint8_t *mr = &m[my * s_pw];
        int x0, x1;
        row_span(yy, &x0, &x1);
        const int xa = cx0 < x0 ? x0 - cx0 : 0, xb = cx0 + cw > x1 ? x1 - cx0 : cw;
        const int step = s_pw * 65536 / cw;  // (16.16: the mask x for each px)
        for (int x = xa; x < xb; x++) {
            const int a = mr[(x * step) >> 16];
            if (a > 8) {
                o[cx0 + x] = sb_px_mix(o[cx0 + x], col, a * ca >> 8);
            }
        }
    }
}

static void sky_clouds(float e)
{
    const float day = e > 6 ? 1 : e < -8 ? 0 : (e + 8) / 14;
    const float sunset = e > -6 && e < 8 ? 1 - fabsf(e - 1) / 7 : 0;
    const float dark = 1 - 0.4f * s_sky.rain - 0.3f * s_sky.storm;
    const int tc[3] = {(int)(lerpf(64, 250, day) * dark), (int)(lerpf(70, 252, day) * dark),
                       (int)(lerpf(92, 255, day) * dark)};
    int bc[3] = {(int)(lerpf(30, 176, day) * dark + 70 * sunset), (int)(lerpf(34, 182, day) * dark + 20 * sunset),
                 (int)(lerpf(50, 198, day) * dark)};
    bc[0] = bc[0] > 255 ? 255 : bc[0];
    const int ncl = (int)(2 + s_sky.cloud * (NCLOUD - 2) + 0.5f);
    for (int i = 0; i < ncl && i < NCLOUD; i++) {
        sky_cloud(&s_m->cl[i], tc, bc);
    }
}

static void sky_fog(float t)
{
    for (int b = 0; b < 3; b++) {
        const float cy = gfx_h * (0.45f + 0.16f * b) + 6 * fsin(t * 0.3f + b);
        for (int y = (int)cy - 26; y < (int)cy + 26; y++) {
            if (y < 0 || y >= gfx_h) {
                continue;
            }
            const float q = 1 - fabsf(y - cy) / 26;
            const int a = (int)(q * q * 120 * s_sky.fog);
            int x0, x1;
            row_span(y, &x0, &x1);
            for (int x = x0; x < x1 && a > 4; x++) {
                g_fb[y * gfx_w + x] = sb_px_mix(g_fb[y * gfx_w + x], rgb(196, 202, 212), a);
            }
        }
    }
}

static void sky_fall(void)
{
    const int np = (int)(NPART * (s_sky.rain > s_sky.snow ? s_sky.rain : s_sky.snow));
    const bool rnd = gfx_round();
    if (s_sky.snow > 0.05f) {
        for (int i = 0; i < np; i++) {
            const int x = (int)s_m->pt[i].x, y = (int)s_m->pt[i].y;
            if (rnd && !gfx_in_circle(x, y, 2)) {
                continue;
            }
            const int a = (int)(150 + 100 * s_m->pt[i].v);
            px_a(x, y, rgb(255, 255, 255), a);
            if (s_m->pt[i].v > 1.0f) {
                px_a(x + 1, y, rgb(255, 255, 255), a / 2);
                px_a(x, y + 1, rgb(255, 255, 255), a / 2);
            }
        }
        return;
    }
    const float dx = s_sky.wind * 3 / 620;
    const uint16_t rc = rgb(196, 214, 240);
    for (int i = 0; i < np; i++) {
        const float x = s_m->pt[i].x, y = s_m->pt[i].y;
        if (rnd && !gfx_in_circle((int)x, (int)y, 4)) {
            continue;
        }
        const int len = (int)(9 + 6 * s_m->pt[i].v);
        for (int k = 0; k < len; k++) {
            px_a((int)(x - dx * k), (int)(y - k), rc, 30 + 70 * k / len);
        }
    }
}

static void sky_bolt(void)
{
    if (s_bolt_t < 0) {
        return;
    }
    for (int i = 0; i < 11; i++) {
        const int x0 = s_bolt[i][0], y0 = s_bolt[i][1], x1 = s_bolt[i + 1][0], y1 = s_bolt[i + 1][1];
        const int n = abs(y1 - y0) + abs(x1 - x0) + 1;
        for (int k = 0; k <= n; k++) {
            const int x = x0 + (x1 - x0) * k / n, y = y0 + (y1 - y0) * k / n;
            px_a(x, y, rgb(255, 255, 255), 256);
            px_a(x + 1, y, rgb(180, 190, 255), 90);
            px_a(x - 1, y, rgb(180, 190, 255), 90);
        }
    }
}

// the still sky: the gradient, the moon, the sun
static void sky_still(float night)
{
    float tc[3], hc[3];
    sky_cols(tc, hc);
    sky_grad(tc, hc);
    if (night > 0.02f && s_sky.moon > 0.03f && s_sky.moon < 0.97f) {
        sky_moon(night);
    }
    if (s_sky.elev > -5) {
        sky_sun(s_sky.elev);
    }
}

void sb_sky_draw(float t)
{
    const float e = s_sky.elev;
    const float night = e < -4 ? (e < -10 ? 1 : (-4 - e) / 6) : 0;
    if (!sky_mem() || !s_bg) {
        sky_still(night);  // (no memory for the copy: all of it in each frame)
    } else {
        const float key[6] = {e, s_sky.cloud, s_sky.rain, s_sky.snow, s_sky.storm, s_flash};
        bool again = false;
        for (int i = 0; i < 6; i++) {
            again |= fabsf(key[i] - s_bg_key[i]) > (i ? 0.01f : 0.2f);
        }
        if (again) {
            sky_still(night);
            memcpy(s_bg, g_fb, (size_t)gfx_w * gfx_h * 2);
            memcpy(s_bg_key, key, sizeof key);
        } else {
            memcpy(g_fb, s_bg, (size_t)gfx_w * gfx_h * 2);
        }
    }
    if (!s_m || !s_pm) {
        return;
    }
    const float clear = 1 - s_sky.cloud;
    if (night > 0.02f && clear > 0.1f) {
        sky_stars(t, night, clear);
    }
    if (s_sky.cloud > 0.05f) {
        sky_clouds(e);
    }
    if (s_sky.fog > 0.05f) {
        sky_fog(t);
    }
    sky_fall();
    sky_bolt();
}
