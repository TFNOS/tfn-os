// SCENES: the pixel-art engine (scn.h). A C copy of the JavaScript scene engine.
#include "scn.h"
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "scn";

const int SCN_VW[SCN_BOARDS] = {640, 320, 206, 320}, SCN_VH[SCN_BOARDS] = {172, 240, 206, 86},
          SCN_VS[SCN_BOARDS] = {1, 1, 2, 2};

// ---------------------------------------------------------------- numbers
static const uint8_t B4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
float sc_bay(int x, int y)
{
    return ((float)B4[((y & 3) << 2) | (x & 3)] + 0.5f) / 16.0f;
}

uint32_t sc_h2u(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return h;
}
float sc_h3(int a, int b, int c)
{
    // h2(a, (h2(b, c) * 2147483647) | 0), with integers (the same value as the JS doubles)
    uint32_t u = sc_h2u(b, c);
    return sc_h2(a, (int)(((uint64_t)u * 2147483647ull) >> 32));
}
float sc_vn(float x, int s)
{
    int ii = sc_floor(x);
    float f = x - (float)ii, u = f * f * (3 - 2 * f);
    return sc_lerp(sc_h2(ii, s), sc_h2(ii + 1, s), u);
}
float sc_vn2(float x, float y, int s)
{
    int ii = sc_floor(x), jj = sc_floor(y);
    float fx = x - (float)ii, fy = y - (float)jj, u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    return sc_lerp(sc_lerp(sc_h3(ii, jj, s), sc_h3(ii + 1, jj, s), u),
                   sc_lerp(sc_h3(ii, jj + 1, s), sc_h3(ii + 1, jj + 1, s), u), v);
}
float sc_fbm2(float x, float y, int s, int oct)
{
    float v = 0, a = 0.5f, f = 1, n = 0;
    if (oct <= 0) {
        oct = 3;
    }
    for (int o = 0; o < oct; o++) {
        v += a * sc_vn2(x * f, y * f, s + o * 31);
        n += a;
        a *= 0.5f;
        f *= 2;
    }
    return v / n;
}
float sc_rnd(sc_rng_t *r)
{
    r->s += 0x6D2B79F5u;
    uint32_t s = r->s, t = (s ^ (s >> 15)) * (1u | s);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return (float)(t ^ (t >> 14)) * (1.0f / 4294967296.0f);
}

// ---------------------------------------------------------------- colours
static inline int cl8(float v)
{
    int i = sc_floor(v + 0.5f);
    return i < 0 ? 0 : i > 255 ? 255 : i;
}
scol sc_pack(float r, float g, float b)
{
    return 0xFF000000u | ((uint32_t)cl8(r) << 16) | ((uint32_t)cl8(g) << 8) | (uint32_t)cl8(b);
}
scol sc_mix(scol a, scol b, float t)
{
    return sc_pack(sc_r(a) + (sc_r(b) - sc_r(a)) * t, sc_g(a) + (sc_g(b) - sc_g(a)) * t,
                   sc_b(a) + (sc_b(b) - sc_b(a)) * t);
}
scol sc_mulc(scol a, float k)
{
    return sc_pack(sc_r(a) * k, sc_g(a) * k, sc_b(a) * k);
}
scol sc_addc(scol a, scol b, float k)
{
    return sc_pack(sc_r(a) + sc_r(b) * k, sc_g(a) + sc_g(b) * k, sc_b(a) + sc_b(b) * k);
}
void sc_ramp(const scol *k, int nk, int n, scol *out)
{
    if (n <= 0) {
        n = nk;
    }
    for (int i = 0; i < n; i++) {
        if (nk == 1) {
            out[i] = k[0];
            continue;
        }
        float f = n == 1 ? 0 : (float)i / (float)(n - 1) * (float)(nk - 1);
        int j = sc_imin(nk - 2, sc_floor(f));
        out[i] = sc_mix(k[j], k[j + 1], f - (float)j);
    }
}
scol sc_pick(const scol *rp, int n, float v, int x, int y, float k)
{
    int i = sc_floor(v + 0.5f + (sc_bay(x, y) - 0.5f) * k);
    return rp[i < 0 ? 0 : i > n - 1 ? n - 1 : i];
}

// ---------------------------------------------------------------- the profile
#include "esp_timer.h"
int64_t scn_prof_us[SCN_PROF_N];
const char *scn_prof_name[SCN_PROF_N];
static int64_t s_prof_t;
void scn_prof_start(void)
{
    s_prof_t = esp_timer_get_time();
}
void scn_prof(int i, const char *name)
{
    int64_t t = esp_timer_get_time();
    if (i >= 0 && i < SCN_PROF_N) {
        scn_prof_us[i] += t - s_prof_t;
        scn_prof_name[i] = name;
    }
    s_prof_t = t;
}

// ---------------------------------------------------------------- a long build must let the other tasks run (the watchdog)
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
static int64_t s_yield_t;
void scn_yield(void)
{
#ifdef ESP_PLATFORM
    int64_t t = esp_timer_get_time();
    if (t - s_yield_t > 100000) {
        vTaskDelay(1);
        s_yield_t = esp_timer_get_time();
    }
#endif
}

// ---------------------------------------------------------------- the pool
typedef struct blk {
    struct blk *next, *prev;
    double size;
} blk_t;
static blk_t s_pool = {&s_pool, &s_pool, 0};
bool scn_oom;
int scn_pool_bytes, scn_pool_peak;
void *scn_alloc(int bytes)
{
    if (bytes <= 0) {
        bytes = 1;
    }
    blk_t *b = heap_caps_malloc(sizeof(blk_t) + (size_t)bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!b) {
        scn_oom = true;
        ESP_LOGE(TAG, "no memory for %d bytes", bytes);
        return NULL;
    }
    memset(b + 1, 0, (size_t)bytes);
    b->size = bytes;
    scn_pool_bytes += bytes;
    if (scn_pool_bytes > scn_pool_peak) {
        scn_pool_peak = scn_pool_bytes;
    }
    b->next = s_pool.next;
    b->prev = &s_pool;
    s_pool.next->prev = b;
    s_pool.next = b;
    return b + 1;
}
void scn_free(void *p)
{
    if (!p) {
        return;
    }
    blk_t *b = (blk_t *)p - 1;
    scn_pool_bytes -= (int)b->size;
    b->prev->next = b->next;
    b->next->prev = b->prev;
    heap_caps_free(b);
}
void scn_pool_free(void)
{
    while (s_pool.next != &s_pool) {
        scn_free(s_pool.next + 1);
    }
    scn_oom = false;
    scn_pool_bytes = scn_pool_peak = 0;
}

// ---------------------------------------------------------------- surfaces
scn_surf *scn_surf_new(int w, int h)
{
    scn_yield();
    scn_surf *s = scn_alloc((int)sizeof(scn_surf));
    if (!s) {
        return NULL;
    }
    s->d = scn_alloc(w * h * 2);
    if (!s->d) {
        scn_free(s);
        return NULL;
    }
    s->w = w;
    s->h = h;
    return s;
}
void scn_surf_del(scn_surf *s)
{
    if (!s) {
        return;
    }
    scn_free(s->d);
    scn_free(s);
}
void scn_clear(scn_surf *s, scol c)
{
    uint16_t p = sc_565(c);
    for (int i = 0, n = s->w * s->h; i < n; i++) {
        s->d[i] = p;
    }
}
// the clip box of a surface in world coordinates: [x0, x1) x [y0, y1)
#define CLIP_X0(s) ((s)->ox)
#define CLIP_Y0(s) ((s)->oy)
#define CLIP_X1(s) ((s)->ox + (s)->w)
#define CLIP_Y1(s) ((s)->oy + (s)->h)
void scn_rect(scn_surf *s, float x, float y, float w, float h, scol c)
{
    int x0 = sc_imax(CLIP_X0(s), sc_round(x)), y0 = sc_imax(CLIP_Y0(s), sc_round(y));
    int x1 = sc_imin(CLIP_X1(s), sc_round(x + w)), y1 = sc_imin(CLIP_Y1(s), sc_round(y + h));
    if (x1 <= x0) {
        return;
    }
    uint16_t p = sc_565(c);
    for (int j = y0; j < y1; j++) {
        uint16_t *r = s->d + (j - s->oy) * s->w - s->ox;
        for (int i = x0; i < x1; i++) {
            r[i] = p;
        }
    }
}
void scn_hl(scn_surf *s, float x0, float x1, float y, scol c)
{
    if (x1 < x0) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    scn_rect(s, x0, y, x1 - x0 + 1, 1, c);
}
void scn_vl(scn_surf *s, float x, float y0, float y1, scol c)
{
    if (y1 < y0) {
        float t = y0;
        y0 = y1;
        y1 = t;
    }
    scn_rect(s, x, y0, 1, y1 - y0 + 1, c);
}
void scn_line(scn_surf *s, float fx0, float fy0, float fx1, float fy1, scol c)
{
    int x0 = sc_round(fx0), y0 = sc_round(fy0), x1 = sc_round(fx1), y1 = sc_round(fy1);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    uint16_t p = sc_565(c);
    for (int n = 0; n < 100000; n++) {
        scn_pxi(s, x0, y0, p);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
static void ell_any(scn_surf *s, float cx, float cy, float rx, float ry, scol c, scn_pixfn f, void *ctx)
{
    int y0 = sc_floor(cy - ry), y1 = (int)ceilf(cy + ry);
    if (y0 < CLIP_Y0(s)) {
        y0 = CLIP_Y0(s);
    }
    if (y1 > CLIP_Y1(s) - 1) {
        y1 = CLIP_Y1(s) - 1;
    }
    uint16_t p = sc_565(c);
    for (int y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f - cy) / ry;
        if (fabsf(dy) > 1) {
            continue;
        }
        float hw = rx * sqrtf(1 - dy * dy);
        int a = sc_imax(sc_round(cx - hw), CLIP_X0(s)), b = sc_imin(sc_round(cx + hw), CLIP_X1(s));
        uint16_t *r = s->d + (y - s->oy) * s->w - s->ox;
        if (f) {
            for (int x = a; x < b; x++) {
                scol v = f(x, y, ctx);
                if (v) {
                    r[x] = sc_565(v);
                }
            }
        } else if (c) {
            for (int x = a; x < b; x++) {
                r[x] = p;
            }
        }
    }
}
void scn_ell(scn_surf *s, float cx, float cy, float rx, float ry, scol c)
{
    ell_any(s, cx, cy, rx, ry, c, NULL, NULL);
}
void scn_ell_fn(scn_surf *s, float cx, float cy, float rx, float ry, scn_pixfn f, void *ctx)
{
    ell_any(s, cx, cy, rx, ry, 0, f, ctx);
}
void scn_ring(scn_surf *s, float cx, float cy, float r, scol c)
{
    int n = sc_imax(12, sc_round(r * 7));
    uint16_t p = sc_565(c);
    for (int i = 0; i < n; i++) {
        float a = (float)i / (float)n * 6.2831853f;
        scn_pxi(s, sc_round(cx + cosf(a) * r), sc_round(cy + sinf(a) * r), p);
    }
}
static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}
static void poly_any(scn_surf *s, const float *pts, int n, scol c, scn_pixfn f, void *ctx)
{
    float fy0 = 1e9f, fy1 = -1e9f;
    for (int i = 0; i < n; i++) {
        if (pts[i * 2 + 1] < fy0) {
            fy0 = pts[i * 2 + 1];
        }
        if (pts[i * 2 + 1] > fy1) {
            fy1 = pts[i * 2 + 1];
        }
    }
    int y0 = sc_imax(CLIP_Y0(s), sc_floor(fy0)), y1 = sc_imin(CLIP_Y1(s) - 1, (int)ceilf(fy1));
    float xs[64];
    uint16_t p = sc_565(c);
    for (int y = y0; y <= y1; y++) {
        float yc = (float)y + 0.5f;
        int nx = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            const float *a = pts + i * 2, *b = pts + j * 2;
            if ((a[1] <= yc) != (b[1] <= yc) && nx < 64) {
                xs[nx++] = a[0] + (yc - a[1]) / (b[1] - a[1]) * (b[0] - a[0]);
            }
        }
        if (nx > 2) {
            qsort(xs, (size_t)nx, sizeof(float), cmpf);
        } else if (nx == 2 && xs[1] < xs[0]) {
            float t = xs[0];
            xs[0] = xs[1];
            xs[1] = t;
        }
        uint16_t *r = s->d + (y - s->oy) * s->w - s->ox;
        for (int k = 0; k + 1 < nx; k += 2) {
            int xa = sc_imax(CLIP_X0(s), sc_round(xs[k])), xb = sc_imin(CLIP_X1(s), sc_round(xs[k + 1]));
            if (f) {
                for (int x = xa; x < xb; x++) {
                    scol v = f(x, y, ctx);
                    if (v) {
                        r[x] = sc_565(v);
                    }
                }
            } else {
                for (int x = xa; x < xb; x++) {
                    r[x] = p;
                }
            }
        }
    }
}
void scn_poly(scn_surf *s, const float *xy, int n, scol c)
{
    poly_any(s, xy, n, c, NULL, NULL);
}
void scn_poly_fn(scn_surf *s, const float *xy, int n, scn_pixfn f, void *ctx)
{
    poly_any(s, xy, n, 0, f, ctx);
}
void scn_fill_fn(scn_surf *s, float fx0, float fy0, float fx1, float fy1, scn_pixfn f, void *ctx)
{
    int x0 = sc_imax(CLIP_X0(s), sc_floor(fx0)), y0 = sc_imax(CLIP_Y0(s), sc_floor(fy0));
    int x1 = sc_imin(CLIP_X1(s), (int)ceilf(fx1)), y1 = sc_imin(CLIP_Y1(s), (int)ceilf(fy1));
    for (int y = y0; y < y1; y++) {
        uint16_t *r = s->d + (y - s->oy) * s->w - s->ox;
        for (int x = x0; x < x1; x++) {
            scol v = f(x, y, ctx);
            if (v) {
                r[x] = sc_565(v);
            }
        }
    }
}
void scn_tint(scn_surf *s, float fx0, float fy0, float fx1, float fy1, scn_tintfn f, void *ctx)
{
    int x0 = sc_imax(CLIP_X0(s), sc_floor(fx0)), y0 = sc_imax(CLIP_Y0(s), sc_floor(fy0));
    int x1 = sc_imin(CLIP_X1(s), (int)ceilf(fx1)), y1 = sc_imin(CLIP_Y1(s), (int)ceilf(fy1));
    for (int y = y0; y < y1; y++) {
        uint16_t *r = s->d + (y - s->oy) * s->w - s->ox;
        for (int x = x0; x < x1; x++) {
            if (r[x]) {
                r[x] = sc_565(f(sc_888(r[x]), x, y, ctx));
            }
        }
    }
}
void scn_blit(scn_surf *s, const scn_surf *src, float fdx, float fdy, bool flip)
{
    int dx = sc_round(fdx), dy = sc_round(fdy);
    int ya = sc_imax(0, CLIP_Y0(s) - dy), yb = sc_imin(src->h, CLIP_Y1(s) - dy);
    int xa = sc_imax(0, CLIP_X0(s) - dx), xb = sc_imin(src->w, CLIP_X1(s) - dx);
    for (int y = ya; y < yb; y++) {
        const uint16_t *sr = src->d + y * src->w;
        uint16_t *r = s->d + (y + dy - s->oy) * s->w + dx - s->ox;
        for (int x = xa; x < xb; x++) {
            uint16_t p = sr[flip ? src->w - 1 - x : x];
            if (p) {
                r[x] = p;
            }
        }
    }
}
// pow(1 - sqrt(u), 1.7) for u = (d / r)^2 in [0, 1): the glow of a pixel without sqrtf and powf
static float s_glow_lut[1025];
void scn_glow(scn_surf *s, float cx, float cy, float r, scol col, float k, float sy)
{
    if (s_glow_lut[0] == 0) {
        for (int i = 0; i <= 1024; i++) {
            s_glow_lut[i] = powf(1 - sqrtf(i / 1024.0f), 1.7f);
        }
    }
    if (sy <= 0) {
        sy = 1;
    }
    int R = sc_r(col), Gc = sc_g(col), Bc = sc_b(col);
    int x0 = sc_imax(CLIP_X0(s), sc_floor(cx - r)), x1 = sc_imin(CLIP_X1(s) - 1, (int)ceilf(cx + r));
    int y0 = sc_imax(CLIP_Y0(s), sc_floor(cy - r * sy)), y1 = sc_imin(CLIP_Y1(s) - 1, (int)ceilf(cy + r * sy));
    float ir2 = 1024.0f / (r * r);
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = s->d + (y - s->oy) * s->w - s->ox;
        float ddy = ((float)y + 0.5f - cy) / sy, dy2 = ddy * ddy;
        for (int x = x0; x <= x1; x++) {
            float ddx = (float)x + 0.5f - cx, u = (ddx * ddx + dy2) * ir2;
            if (u >= 1024) {
                continue;
            }
            float a = s_glow_lut[(int)u] * k;
            if (a < 0.06f) {
                continue;
            }
            int st = (int)(a * 6 + sc_bay(x, y));  // 0..6 sixths (a >= 0)
            if (st <= 0) {
                continue;
            }
            scol c = sc_888(row[x]);
            int rr = sc_r(c) + R * st / 6, gg = sc_g(c) + Gc * st / 6, bb = sc_b(c) + Bc * st / 6;
            row[x] = sc_565(0xFF000000u | ((uint32_t)(rr > 255 ? 255 : rr) << 16) |
                            ((uint32_t)(gg > 255 ? 255 : gg) << 8) | (uint32_t)(bb > 255 ? 255 : bb));
        }
    }
}
void scn_blend(scn_surf *s, float fx, float fy, scol c, float a)
{
    int x = sc_round(fx), y = sc_round(fy);
    if (x < CLIP_X0(s) || y < CLIP_Y0(s) || x >= CLIP_X1(s) || y >= CLIP_Y1(s)) {
        return;
    }
    int q = sc_floor(a * 4 + sc_bay(x, y));
    if (q <= 0) {
        return;
    }
    uint16_t *p = s->d + (y - s->oy) * s->w + (x - s->ox);
    *p = q >= 4 ? sc_565(c) : scn_mixp(*p, c, q * 64);
}
void scn_halo(scn_surf *s, float cx, float cy, const float (*rings)[2], int n, scol col, float sy)
{
    if (sy <= 0) {
        sy = 1;
    }
    float R0 = rings[0][0];
    int y0 = sc_floor(cy - R0 * sy), x0 = sc_floor(cx - R0);
    float yl = cy + R0 * sy, xl = cx + R0;
    if (y0 < CLIP_Y0(s)) {
        y0 = CLIP_Y0(s);
    }
    if (x0 < CLIP_X0(s)) {
        x0 = CLIP_X0(s);
    }
    int y1 = sc_imin(CLIP_Y1(s) - 1, sc_floor(yl)), x1 = sc_imin(CLIP_X1(s) - 1, sc_floor(xl));
    float r2[8];
    int k[8];
    if (n > 8) {
        n = 8;
    }
    for (int q = 0; q < n; q++) {
        r2[q] = rings[q][0] * rings[q][0];
        k[q] = (int)(rings[q][1] * 256 + 0.5f);
    }
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = s->d + (y - s->oy) * s->w - s->ox;
        float ddy = ((float)y - cy) / sy, dy2 = ddy * ddy;
        for (int x = x0; x <= x1; x++) {
            float ddx = (float)x - cx, d2 = ddx * ddx + dy2;
            int ri = -1;
            for (int q = n - 1; q >= 0; q--) {
                if (d2 < r2[q]) {
                    ri = q;
                    break;
                }  // the smallest ring (the last one that has it)
            }
            if (ri < 0 || k[ri] == 0 || (ri == 0 && n > 1 && ((x + y) % 2))) {
                continue;
            }
            row[x] = scn_mixp(row[x], col, k[ri]);
        }
    }
}

// ---------------------------------------------------------------- the 3 x 5 font
typedef struct {
    char ch;
    const char *g;
} f3_t;
static const f3_t F3[] = {
        {'A', "111101111101101"}, {'B', "110101110101110"},  {'C', "111100100100111"}, {'D', "110101101101110"},
        {'E', "111100110100111"}, {'F', "111100110100100"},  {'G', "111100101101111"}, {'H', "101101111101101"},
        {'I', "111010010010111"}, {'J', "001001001101111"},  {'K', "101101110101101"}, {'L', "100100100100111"},
        {'M', "101111111101101"}, {'N', "110101101101101"},  {'O', "111101101101111"}, {'P', "111101111100100"},
        {'Q', "111101101111001"}, {'R', "110101110101101"},  {'S', "111100111001111"}, {'T', "111010010010010"},
        {'U', "101101101101111"}, {'V', "101101101101010"},  {'W', "101101111111101"}, {'X', "101101010101101"},
        {'Y', "101101010010010"}, {'Z', "111001010100111"},  {'0', "111101101101111"}, {'1', "010110010010111"},
        {'2', "111001111100111"}, {'3', "111001111001111"},  {'4', "101101111001001"}, {'5', "111100111001111"},
        {'6', "111100111101111"}, {'7', "111001001010010"},  {'8', "111101111101111"}, {'9', "111101111001111"},
        {' ', "000000000000000"}, {'.', "000000000000010"},  {':', "000010000010000"}, {'-', "000000111000000"},
        {'&', "010101010101011"}, {'\'', "010010000000000"}, {'!', "010010010000010"}, {'+', "000010111010000"},
        {'/', "001001010100100"},
};
int scn_text3(scn_surf *s, const char *str, float x, float y, scol c, int sc)
{
    if (sc <= 0) {
        sc = 1;
    }
    int n = (int)strlen(str);
    for (int i = 0; i < n; i++) {
        char ch = str[i];
        if (ch >= 'a' && ch <= 'z') {
            ch = (char)(ch - 32);
        }
        const char *g = F3[36].g;
        for (size_t q = 0; q < sizeof F3 / sizeof F3[0]; q++) {
            if (F3[q].ch == ch) {
                g = F3[q].g;
                break;
            }
        }
        for (int k = 0; k < 15; k++) {
            if (g[k] == '1') {
                scn_rect(s, x + (float)((i * 4 + (k % 3)) * sc), y + (float)((k / 3) * sc), (float)sc, (float)sc, c);
            }
        }
    }
    return n * 4 * sc - sc;
}

// ---------------------------------------------------------------- the pine
void scn_pine(scn_surf *s, float x, float base, float h, const scol *pal, scol pal1, float wide, int light, int seed)
{
    bool one = pal1 != 0;
    int tiers = sc_imax(3, sc_round(h / 8));
    float w0 = h * (wide > 0 ? wide : 0.34f);
    int side = light ? light : 1;
    float trunkH = (float)sc_imax(2, sc_round(h * 0.1f));
    scn_rect(s, x - 1, base - trunkH, 3, trunkH + 1, one ? pal1 : pal[1]);
    for (int k = 0; k < tiers; k++) {
        float top = base - h + (float)k * (h - trunkH) / (float)tiers, bot = top + (h - trunkH) / (float)tiers * 1.7f;
        float hw = w0 * ((float)k + 1.4f) / ((float)tiers + 0.4f);
        for (int y = sc_floor(top); y <= (int)ceilf(bot); y++) {
            float u = ((float)y - top) / (bot - top);
            if (u < 0 || u > 1) {
                continue;
            }
            int jag = (u > 0.75f) ? (((sc_floor((float)y * 0.7f + (float)seed)) % 2) ? 0 : 1) : 0;
            int w = sc_round(hw * powf(u, 0.85f)) - jag;
            for (int xx = -w; xx <= w; xx++) {
                if (one) {
                    scn_px(s, x + (float)xx, (float)y, pal1);
                    continue;
                }
                scol c = (float)(xx * side) > (float)w * 0.35f   ? pal[3]
                         : (float)(xx * side) > -(float)w * 0.2f ? pal[2]
                                                                 : pal[1];
                if (u > 0.85f && ((xx + y + seed) % 3) == 0) {
                    c = pal[1];
                }
                if (abs(xx) == w || y == (int)ceilf(bot)) {
                    c = (xx * side > 0 && pal[4]) ? pal[4] : pal[0];
                }
                scn_px(s, x + (float)xx, (float)y, c);
            }
        }
    }
}

// ---------------------------------------------------------------- still layers
static int nearest(const uint16_t *pal, int n, uint16_t p)
{
    scol c = sc_888(p);
    int best = 1, bd = 1 << 30;
    for (int i = 1; i < n; i++) {
        scol q = sc_888(pal[i]);
        int dr = sc_r(c) - sc_r(q), dg = sc_g(c) - sc_g(q), db = sc_b(c) - sc_b(q),
            d = dr * dr * 2 + dg * dg * 3 + db * db;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}
scn_layer *scn_layer_make(const scn_surf *src, int cx0, int cx1)
{
    scn_yield();
    // the columns in src pixels, then the box of the pixels that are there
    int a = sc_imax(0, cx0 - src->ox), b = sc_imin(src->w, cx1 - src->ox);
    int bx0 = 1 << 30, bx1 = -1, by0 = 1 << 30, by1 = -1;
    for (int y = 0; y < src->h; y++) {
        const uint16_t *r = src->d + y * src->w;
        for (int x = a; x < b; x++) {
            if (r[x]) {
                if (x < bx0) {
                    bx0 = x;
                }
                if (x > bx1) {
                    bx1 = x;
                }
                if (y < by0) {
                    by0 = y;
                }
                if (y > by1) {
                    by1 = y;
                }
            }
        }
    }
    scn_layer *L = scn_alloc((int)sizeof(scn_layer));
    if (!L) {
        return NULL;
    }
    L->npal = 1;
    if (bx1 < 0) {
        L->w = L->h = 0;
        return L;
    }
    L->x0 = bx0 + src->ox;
    L->y0 = by0 + src->oy;
    L->w = bx1 - bx0 + 1;
    L->h = by1 - by0 + 1;
    L->d = scn_alloc(L->w * L->h);
    if (!L->d) {
        return NULL;
    }
    // the palette: a small hash of the colours (565 -> index)
    static uint16_t hk[1024];
    static uint8_t hv[1024];
    memset(hv, 0, sizeof hv);
    int over = 0;
    for (int y = 0; y < L->h; y++) {
        const uint16_t *r = src->d + (y + by0) * src->w + bx0;
        uint8_t *o = L->d + y * L->w;
        for (int x = 0; x < L->w; x++) {
            uint16_t p = r[x];
            if (!p) {
                continue;
            }
            unsigned hsh = ((p * 40503u) >> 6) & 1023;
            while (hv[hsh] && hk[hsh] != p) {
                hsh = (hsh + 1) & 1023;
            }
            if (!hv[hsh]) {
                if (L->npal < 256) {
                    hk[hsh] = p;
                    hv[hsh] = (uint8_t)L->npal;
                    L->pal[L->npal++] = p;
                } else {
                    over++;
                    o[x] = (uint8_t)nearest(L->pal, 256, p);
                    continue;
                }
            }
            o[x] = hv[hsh];
        }
    }
    if (over) {
        ESP_LOGW(TAG, "a layer has more than 255 colours: %d pixels got the nearest colour", over);
    }
    return L;
}
void scn_layer_draw(scn_surf *s, const scn_layer *L, int shx, int shy)
{
    // world x -> layer x + shx; the rows and columns that both have
    int y0 = sc_imax(CLIP_Y0(s), L->y0 - shy), y1 = sc_imin(CLIP_Y1(s), L->y0 + L->h - shy);
    int x0 = sc_imax(CLIP_X0(s), L->x0 - shx), x1 = sc_imin(CLIP_X1(s), L->x0 + L->w - shx);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    uint16_t pal[256];  // the palette in internal RAM (the layer is in PSRAM)
    memcpy(pal, L->pal, sizeof pal);
    for (int y = y0; y < y1; y++) {
        const uint8_t *lr = L->d + (y + shy - L->y0) * L->w + (shx - L->x0);
        uint16_t *r = s->d + (y - s->oy) * s->w - s->ox;
        int x = x0;
        for (; x < x1 && ((uintptr_t)(lr + x) & 3); x++) {
            uint8_t i = lr[x];
            if (i) {
                r[x] = pal[i];
            }
        }
        for (; x + 4 <= x1; x += 4) {  // four at a time: skip the holes fast
            uint32_t w4 = *(const uint32_t *)(lr + x);
            if (!w4) {
                continue;
            }
            uint8_t i0 = (uint8_t)w4, i1 = (uint8_t)(w4 >> 8), i2 = (uint8_t)(w4 >> 16), i3 = (uint8_t)(w4 >> 24);
            if (i0) {
                r[x] = pal[i0];
            }
            if (i1) {
                r[x + 1] = pal[i1];
            }
            if (i2) {
                r[x + 2] = pal[i2];
            }
            if (i3) {
                r[x + 3] = pal[i3];
            }
        }
        for (; x < x1; x++) {
            uint8_t i = lr[x];
            if (i) {
                r[x] = pal[i];
            }
        }
    }
}
void scn_layer_draw_full(scn_surf *s, const scn_layer *L, int shx, int shy)
{
    int y0 = sc_imax(CLIP_Y0(s), L->y0 - shy), y1 = sc_imin(CLIP_Y1(s), L->y0 + L->h - shy);
    int x0 = sc_imax(CLIP_X0(s), L->x0 - shx), x1 = sc_imin(CLIP_X1(s), L->x0 + L->w - shx);
    if (x1 <= x0) {
        return;
    }
    uint16_t pal[256];  // in internal RAM
    memcpy(pal, L->pal, sizeof pal);
    pal[0] = L->pal[1];
    for (int y = y0; y < y1; y++) {
        const uint8_t *lr = L->d + (y + shy - L->y0) * L->w + (shx - L->x0);
        uint16_t *r = s->d + (y - s->oy) * s->w - s->ox;
        for (int x = x0; x < x1; x++) {
            r[x] = pal[lr[x]];
        }
    }
}
void scn_path_range(void (*path)(float t, float L, int *vx, int *vy), float L, int *vx0, int *vx1, int *vy0, int *vy1)
{
    *vx0 = *vy0 = 1 << 30;
    *vx1 = *vy1 = -(1 << 30);
    for (int i = 0; i <= 512; i++) {
        int x, y;
        path(L * (float)i / 512.0f, L, &x, &y);
        if (x < *vx0) {
            *vx0 = x;
        }
        if (x > *vx1) {
            *vx1 = x;
        }
        if (y < *vy0) {
            *vy0 = y;
        }
        if (y > *vy1) {
            *vy1 = y;
        }
    }
}
void scn_par_cols(int pad, float p, int vx0, int vx1, int vw, int *cx0, int *cx1)
{
    // the layer x of world x is x + pad - vx * (1 - p) = pad + vx * p + (x - vx)
    float a = (float)pad + (float)vx0 * p, b = (float)pad + (float)vx1 * p;
    if (b < a) {
        float t = a;
        a = b;
        b = t;
    }
    *cx0 = sc_floor(a) - 2;
    *cx1 = (int)ceilf(b) + vw + 2;
}

// ---------------------------------------------------------------- people (engine2_px.js)
static const char *const HEAD_FRONT[] = {".....PP.....", "....HHHH....", "...HHHHHH...", "...hhhhhh...", "...rssssr...",
                                         "...ssssss...", "...sesses...", "...ssssss...", "....SssS...."};
static const char *const HEAD_BACK[] = {".....PP.....", "....HHHH....", "...HHHHHH...", "...hhhhhh...", "...rrrrrr...",
                                        "...rrrrrr...", "...rrrrrr...", "...RrrrrR...", "....SSSS...."};
static const char *const BODY_FRONT[] = {"...KKKKKK...", "..CCkkkkcc..", ".cCCccckcdd.", ".cCCccckcdd.",
                                         ".cCccccccdd.", ".mCcccccccm.", "..cccccccd..", "..ccccccdd.."};
static const char *const BODY_BACK[] = {"...KKKKKK...", "..CCKkkkcc..", ".cCCckkccdd.", ".cCCckkccdd.",
                                        ".cCccccccdd.", ".mCcccdcccm.", "..cccdcccd..", "..cccdccdd.."};
static const char *const LEGS_FRONT[4][4] = {
        {"...pp..qq...", "...pp..qq...", "...pp..qq...", "..bbb..bbb.."},
        {"...pp..qq...", "...pp..bbb..", "...pp.......", "..bbb......."},
        {"...pp..qq...", "...pp..qq...", "...pp..qq...", "..bbb..bbb.."},
        {"...pp..qq...", "..bbb..qq...", ".......qq...", ".......bbb.."},
};
static const char *const HEAD_SIDE[] = {"....PP......", "...HHHH.....", "..HHHHHH....", "..hhhhhh....", "..rrssss....",
                                        "..rsssss....", "..rsssess...", "..rssssss...", "...Ssss....."};
static const char *const BODY_SIDE[] = {"...KKKK.....", "...kkkkk....", "...cccCC....", "...ccccC....",
                                        "...ccccC....", "...ccccC....", "...cccccC...", "...ccccdd..."};
static const char *const LEGS_SIDE[4][4] = {
        {"....qq.pp...", "...qq...pp..", "...qq...pp..", "..bbb...bbb."},
        {"....qqpp....", "....q.pp....", "...bb.pp....", "......bbb..."},
        {"....pp.qq...", "...pp...qq..", "...pp...qq..", "..bbb...bbb."},
        {"....ppqq....", "....p.qq....", "...bb.qq....", "......bbb..."},
};
static const char *const LEGS_SIT[] = {"...ppppppp..", "........pp..", "........pp..", ".......bbb.."};
static const char *const LEGS_SKATE[2][5] = {
        {"...qq.pp....", ".qq...pp....", "qb....pp....", ".....bbbb...", "....wwwwww.."},
        {"....qqpp....", "...qq.pp....", "..qb..pp....", ".....bbbb...", "....wwwwww.."},
};
static const int ARM_BACK[4][2] = {{4, 11}, {3, 12}, {2, 13}, {2, 14}},
                 ARM_DOWN[4][2] = {{5, 11}, {5, 12}, {5, 13}, {5, 14}};
static const int ARM_FWD[4][2] = {{6, 11}, {7, 12}, {8, 13}, {9, 13}},
                 ARM_OUT[4][2] = {{4, 11}, {3, 11}, {2, 11}, {1, 11}};

#define DEF(v, d) ((v) ? (v) : K(d))
void scn_look_make(const scn_look_in *o, scn_look *L)
{
    scol hat = DEF(o->hat, 0xa03028), hair = DEF(o->hair, 0x3a2214), skin = DEF(o->skin, 0xe8b090),
         scarf = DEF(o->scarf, 0xd8a030);
    scol coat = DEF(o->coat, 0x2a3a6a), pants = DEF(o->pants, 0x2a2630);
    L->P = o->pom ? o->pom : o->hat2 ? o->hat2 : K(0xe8e0d0);
    L->H = hat;
    L->h = o->hat2 ? o->hat2 : sc_mix(hat, K(0xffffff), 0.25f);
    L->r = hair;
    L->R = sc_mix(hair, K(0x000000), 0.35f);
    L->s = skin;
    L->S = sc_mix(skin, K(0x6a3a30), 0.35f);
    L->e = K(0x1a1020);
    L->k = scarf;
    L->Kl = sc_mix(scarf, K(0xffffff), 0.3f);
    L->c = coat;
    L->C = sc_mix(coat, K(0xffffff), 0.18f);
    L->d = sc_mix(coat, K(0x000000), 0.32f);
    L->m = o->mitt ? o->mitt : scarf;
    L->p = pants;
    L->q = sc_mix(pants, K(0x000000), 0.3f);
    L->b = DEF(o->boots, 0x3a2416);
    L->w = K(0xc8d4e0);
    L->kind = o->kind;
    L->outline = DEF(o->outline, 0x0c0a16);
    if (L->kind == SCN_NONE) {
        L->P = 0;
        L->H = L->r;
        L->h = L->r;
    }
    if (L->kind == SCN_HOOD) {
        L->P = 0;
        L->H = L->c;
        L->h = L->C;
    }
    if (L->kind == SCN_CAP) {
        L->P = 0;
        L->H = DEF(o->hat, 0x2a2a34);
        L->h = L->H;
    }
}
static scol look_col(const scn_look *L, char ch)
{
    switch (ch) {
    case 'P':
        return L->P;
    case 'H':
        return L->H;
    case 'h':
        return L->h;
    case 'r':
        return L->r;
    case 'R':
        return L->R;
    case 's':
        return L->s;
    case 'S':
        return L->S;
    case 'e':
        return L->e;
    case 'k':
        return L->k;
    case 'K':
        return L->Kl;
    case 'c':
        return L->c;
    case 'C':
        return L->C;
    case 'd':
        return L->d;
    case 'm':
        return L->m;
    case 'p':
        return L->p;
    case 'q':
        return L->q;
    case 'b':
        return L->b;
    case 'w':
        return L->w;
    }
    return 0;
}
void scn_stamp(scn_surf *s, const char *const *rows, int nrows, int x0, int y0, const scn_look *L, bool flip)
{
    for (int y = 0; y < nrows; y++) {
        int n = (int)strlen(rows[y]);
        for (int x = 0; x < n; x++) {
            char ch = rows[y][flip ? n - 1 - x : x];
            if (ch == '.') {
                continue;
            }
            scol c = look_col(L, ch);
            if (c) {
                scn_pxi(s, x0 + x, y0 + y, sc_565(c));
            }
        }
    }
}
void scn_outline(scn_surf *s, scol col)
{
    int w = s->w, h = s->h;
    uint16_t *src = scn_alloc(w * h * 2);
    if (!src) {
        return;
    }
    memcpy(src, s->d, (size_t)(w * h * 2));
    uint16_t p = sc_565(col);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (src[y * w + x]) {
                continue;
            }
            if ((x > 0 && src[y * w + x - 1]) || (x < w - 1 && src[y * w + x + 1]) || (y > 0 && src[(y - 1) * w + x]) ||
                (y < h - 1 && src[(y + 1) * w + x])) {
                s->d[y * w + x] = p;
            }
        }
    }
    scn_free(src);
}
static void hood(scn_surf *s, const scn_look *L, int bob, bool side)
{
    if (L->kind != SCN_HOOD) {
        return;
    }
    for (int r = 3; r <= 8; r++) {
        if (side) {
            scn_pxi(s, 2, 1 + r + bob, sc_565(L->c));
        } else {
            scn_pxi(s, 3, 1 + r + bob, sc_565(L->C));
            scn_pxi(s, 10, 1 + r + bob, sc_565(L->d));
        }
    }
}
static void arm(scn_surf *s, const int (*a)[2], int bob, const scn_look *L)
{
    for (int i = 0; i < 4; i++) {
        scn_pxi(s, 1 + a[i][0], 1 + a[i][1] + bob - 1 + 1, sc_565(i == 3 ? L->m : L->d));
    }
}
bool scn_people(const scn_look_in *o, scn_person *P)
{
    memset(P, 0, sizeof *P);
    scn_look_make(o, &P->look);
    const scn_look *L = &P->look;
    for (int f = 0; f < 4; f++) {
        int bob = (f == 1 || f == 3) ? 0 : 1;
        for (int sd = 0; sd < 2; sd++) {
            scn_surf *s = scn_surf_new(14, 24);
            if (!s) {
                return false;
            }
            scn_stamp(s, LEGS_FRONT[sd == 0 ? f : (f + 2) % 4], 4, 1, 18, L, false);
            scn_stamp(s, sd == 0 ? BODY_FRONT : BODY_BACK, 8, 1, 10 + bob, L, false);
            int y0 = 10 + bob;
            if (f == 1 || f == 3) {
                int ua = f == 1 ? 2 : 11, da = f == 1 ? 11 : 2;
                scn_pxi(s, ua, y0 + 4, sc_565(L->m));
                scn_pxi(s, ua, y0 + 5, 0);
                scn_pxi(s, da, y0 + 5, sc_565(da == 2 ? L->c : L->d));
                scn_pxi(s, da, y0 + 6, sc_565(L->m));
            }
            scn_stamp(s, sd == 0 ? HEAD_FRONT : HEAD_BACK, 9, 1, 1 + bob, L, false);
            hood(s, L, bob, false);
            scn_outline(s, L->outline);
            if (sd == 0) {
                P->front[f] = s;
            } else {
                P->back[f] = s;
            }
        }
        scn_surf *s = scn_surf_new(14, 24);
        if (!s) {
            return false;
        }
        scn_stamp(s, LEGS_SIDE[f], 4, 1, 18, L, false);
        scn_stamp(s, BODY_SIDE, 8, 1, 10 + bob, L, false);
        if (f % 2 == 0) {
            scn_pxi(s, 3, 12 + bob, sc_565(L->k));
            scn_pxi(s, 3, 13 + bob, sc_565(L->k));
        } else {
            scn_pxi(s, 3, 12 + bob, sc_565(L->k));
            scn_pxi(s, 2, 13 + bob, sc_565(L->k));
        }
        arm(s, f == 0 ? ARM_BACK : f == 2 ? ARM_FWD : ARM_DOWN, bob, L);
        scn_stamp(s, HEAD_SIDE, 9, 1, 1 + bob, L, false);
        hood(s, L, bob, true);
        scn_outline(s, L->outline);
        P->side[f] = s;
    }
    for (int f = 0; f < 2; f++) {
        scn_surf *s = scn_surf_new(14, 25);
        if (!s) {
            return false;
        }
        scn_stamp(s, LEGS_SKATE[f], 5, 1, 18, L, false);
        scn_stamp(s, BODY_SIDE, 8, 1, 10, L, false);
        for (int i = 0; i < 4; i++) {
            scn_pxi(s, 1 + ARM_OUT[i][0], 1 + ARM_OUT[i][1], sc_565(i == 3 ? L->m : L->d));
        }
        scn_pxi(s, 3, 12, sc_565(L->k));
        scn_pxi(s, 2, 12 + f, sc_565(L->k));
        scn_stamp(s, HEAD_SIDE, 9, 1, 1, L, false);
        hood(s, L, 0, true);
        scn_outline(s, L->outline);
        P->skate[f] = s;
    }
    for (int f = 0; f < 2; f++) {
        scn_surf *s = scn_surf_new(14, 24);
        if (!s) {
            return false;
        }
        static const int A0[4][2] = {{5, 11}, {6, 12}, {7, 13}, {8, 14}},
                         A1[4][2] = {{6, 11}, {7, 12}, {8, 12}, {9, 12}};
        scn_stamp(s, LEGS_SIT, 4, 1, 18, L, false);
        scn_stamp(s, BODY_SIDE, 8, 1, 10, L, false);
        const int (*a)[2] = f ? A1 : A0;
        for (int i = 0; i < 4; i++) {
            scn_pxi(s, 1 + a[i][0], 1 + a[i][1], sc_565(i == 3 ? L->m : L->d));
        }
        scn_pxi(s, 3, 12, sc_565(L->k));
        scn_pxi(s, 3, 13, sc_565(L->k));
        scn_stamp(s, HEAD_SIDE, 9, 1, 1, L, false);
        hood(s, L, 0, true);
        scn_outline(s, L->outline);
        P->sit[f] = s;
    }
    return true;
}
static scn_surf *cut_child(const scn_surf *s)
{
    scn_surf *d = scn_surf_new(14, 19);
    if (!d) {
        return NULL;
    }
    for (int y = 0, yy = 0; y < 24; y++) {
        if (y == 9 || y == 13 || y == 14 || y == 20 || y == 21) {
            continue;
        }
        memcpy(d->d + yy * 14, s->d + y * 14, 28);
        yy++;
    }
    return d;
}
bool scn_child(const scn_look_in *o, scn_person *P)
{
    scn_person A;
    if (!scn_people(o, &A)) {
        return false;
    }
    memset(P, 0, sizeof *P);
    P->look = A.look;
    for (int f = 0; f < 4; f++) {
        if (!(P->front[f] = cut_child(A.front[f])) || !(P->back[f] = cut_child(A.back[f])) ||
            !(P->side[f] = cut_child(A.side[f]))) {
            return false;
        }
        scn_surf_del(A.front[f]);
        scn_surf_del(A.back[f]);
        scn_surf_del(A.side[f]);
    }
    for (int f = 0; f < 2; f++) {
        P->skate[f] = A.skate[f];
        scn_surf_del(A.sit[f]);
    }
    return true;
}
bool scn_mini(const scn_look_in *o, scn_surf *fr[2])
{
    scn_look L;
    scn_look_make(o, &L);
    static const char *const R0[] = {"..rr...", ".rsss..", "..ss...", ".cccc..", "ccccc..",
                                     ".cccc..", ".cccc..", "..pp...", "..pp...", "..bb..."};
    static const char *const R1[] = {"..rr...", ".rsss..", "..ss...", ".cccc..", ".ccccc.",
                                     ".cccc..", ".cccc..", ".p..p..", "p....p.", "b....b."};
    bool hatc = L.kind == SCN_BEANIE || L.kind == SCN_CAP;
    for (int f = 0; f < 2; f++) {
        scn_surf *s = scn_surf_new(9, 12);
        if (!s) {
            return false;
        }
        char buf[10][8];
        const char *rows[10];
        for (int y = 0; y < 10; y++) {
            const char *src = f ? R1[y] : R0[y];
            for (int x = 0; x < 8; x++) {
                buf[y][x] = (src[x] == 'r' && hatc) ? 'H' : src[x];
            }
            rows[y] = buf[y];
        }
        scn_stamp(s, rows, 10, 1, 1, &L, false);
        scn_outline(s, L.outline);
        fr[f] = s;
    }
    return true;
}
scn_surf *scn_warm(const scn_surf *src, float k)
{
    scn_surf *o = scn_surf_new(src->w, src->h);
    if (!o) {
        return NULL;
    }
    for (int i = 0, n = src->w * src->h; i < n; i++) {
        o->d[i] = src->d[i] ? sc_565(sc_mix(sc_888(src->d[i]), K(0xffc070), k)) : 0;
    }
    return o;
}
void scn_draw_person(scn_surf *s, const scn_surf *fr, float x, float fy, bool flip)
{
    scn_blit(s, fr, (float)sc_round(x - (float)fr->w / 2), (float)sc_round(fy - (float)fr->h + 1), flip);
}

// ---------------------------------------------------------------- the frame in bands
static inline uint16_t px_scale(uint16_t p, int k)
{
    uint16_t v = (uint16_t)((p >> 8) | (p << 8));
    int r = ((v >> 11) & 31) * k >> 8, g = ((v >> 5) & 63) * k >> 8, b = (v & 31) * k >> 8;
    v = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((v >> 8) | (v << 8));
}
static inline uint16_t px_mix(uint16_t a, uint16_t b, int k)
{
    uint16_t va = (uint16_t)((a >> 8) | (a << 8)), vb = (uint16_t)((b >> 8) | (b << 8));
    int ra = (va >> 11) & 31, ga = (va >> 5) & 63, ba = va & 31, rb = (vb >> 11) & 31, gb = (vb >> 5) & 63,
        bb = vb & 31;
    int r = ra + ((rb - ra) * k >> 8), g = ga + ((gb - ga) * k >> 8), bl = ba + ((bb - ba) * k >> 8);
    uint16_t v = (uint16_t)((r << 11) | (g << 5) | bl);
    return (uint16_t)((v >> 8) | (v << 8));
}
void scn_render(const scn_def_t *S, float t, float L, int board, uint16_t *band, int band_rows, uint16_t *fb, int fw,
                int fh, int fade, const uint16_t *freeze, int mix)
{
    int vx = 0, vy = 0;
    S->prep(t, L, &vx, &vy);
    // (the big pixels of a scene with no big view: the middle of its 3.49 view)
    if (board == SCN_349B && !S->big) {
        vx += (SCN_VW[SCN_349] - SCN_VW[SCN_349B]) / 2 + S->big_dx;
        vy += (SCN_VH[SCN_349] - SCN_VH[SCN_349B]) / 2 + S->big_dy;
    }
    int vw = SCN_VW[board], vh = SCN_VH[board], sc = SCN_VS[board];
    scn_surf out = {vw, band_rows, vx, vy, band};
    uint16_t row2[2 * 320];  // a doubled row (the Watcher)
    for (int y0 = 0; y0 < vh; y0 += band_rows) {
        int bh = vh - y0 < band_rows ? vh - y0 : band_rows;
        out.h = bh;
        out.oy = vy + y0;
        memset(band, 0, (size_t)vw * bh * 2);
        S->draw(&out);
        for (int y = 0; y < bh; y++) {
            uint16_t *r = band + y * vw;
            int fy = (y0 + y) * sc;
            if (fy >= fh) {
                break;
            }
            int n = vw * sc > fw ? fw / sc : vw;
            if (mix > 0 && freeze) {
                const uint16_t *f = freeze + fy * fw;
                for (int x = 0; x < n; x++) {
                    r[x] = px_mix(f[x * sc], r[x], mix);
                }
            }
            if (fade < 256) {
                int k = fade < 0 ? 0 : fade;
                for (int x = 0; x < n; x++) {
                    r[x] = px_scale(r[x], k);
                }
            }
            if (sc == 1) {
                memcpy(fb + fy * fw, r, (size_t)n * 2);
            } else {
                for (int x = 0; x < n && x < 320; x++) {
                    row2[2 * x] = row2[2 * x + 1] = r[x];
                }
                memcpy(fb + fy * fw, row2, (size_t)n * 4);
                if (fy + 1 < fh) {
                    memcpy(fb + (fy + 1) * fw, row2, (size_t)n * 4);
                }
            }
        }
    }
}

// ---------------------------------------------------------------- a frame at 1.5x (scn_render15)
// the mean of two stored pixels, each channel (rounded down; the mean of two equal pixels is the same pixel)
static inline uint16_t px_avg(uint16_t a, uint16_t b)
{
    uint16_t va = (uint16_t)((a >> 8) | (a << 8)), vb = (uint16_t)((b >> 8) | (b << 8));
    uint16_t v = (uint16_t)((va & vb) + (((va ^ vb) & 0xF7DEu) >> 1));
    return (uint16_t)((v >> 8) | (v << 8));
}
// one world row into n screen pixels: s = the row from the first column of a pair of world pixels, ph = the place
// (0..2) of the first screen pixel in its group of three (the first pixel, the mean, the second pixel)
static void row15(uint16_t *d, const uint16_t *s, int ph, int n)
{
    int x = 0;
    if (ph == 1) {
        d[x++] = px_avg(s[0], s[1]);
        if (x < n) {
            d[x++] = s[1];
        }
        s += 2;
    } else if (ph == 2) {
        d[x++] = s[1];
        s += 2;
    }
    for (; x + 3 <= n; x += 3, s += 2) {
        d[x] = s[0];
        d[x + 1] = px_avg(s[0], s[1]);
        d[x + 2] = s[1];
    }
    if (x < n) {
        d[x++] = s[0];
    }
    if (x < n) {
        d[x] = px_avg(s[0], s[1]);
    }
}
static inline int div3_floor(int v)
{
    return v >= 0 ? v / 3 : -((2 - v) / 3);
}
void scn_render15(const scn_def_t *S, float t, float L, int cx, int cy, uint16_t *buf, int buf_px, uint16_t *fb, int fw,
                  int fh, int fade)
{
    int vx = 0, vy = 0;
    S->prep(t, L, &vx, &vy);
    // the world window: the pairs of columns and rows that the screen shows (the view starts on an even world point)
    int kx = div3_floor(cx), ky = div3_floor(cy), phx = cx - 3 * kx, phy = cy - 3 * ky;
    int bw = 2 * ((phx + fw - 1) / 3) + 2, vh = 2 * ((phy + fh - 1) / 3) + 2;
    uint16_t *rows = buf, *band = buf + 2 * fw;  // rows: the even row of a pair (later the mean row), the odd row
    int band_rows = (buf_px - 2 * fw) / bw;
    if (band_rows < 1) {
        return;
    }
    scn_surf out = {bw, band_rows, 2 * kx, 2 * ky, band};
    for (int y0 = 0; y0 < vh; y0 += band_rows) {
        int bh = vh - y0 < band_rows ? vh - y0 : band_rows;
        out.h = bh;
        out.oy = 2 * ky + y0;
        memset(band, 0, (size_t)bw * bh * 2);
        S->draw(&out);
        for (int y = 0; y < bh; y++) {
            uint16_t *r = band + y * bw;
            if (fade < 256) {
                int k = fade < 0 ? 0 : fade;
                for (int x = 0; x < bw; x++) {
                    r[x] = px_scale(r[x], k);
                }
            }
            int p = (y0 + y) >> 1,
                sy = 3 * p - phy;  // the pair of this world row, and the screen row of its first pixel
            if (!((y0 + y) & 1)) {
                row15(rows, r, phx, fw);
                if (sy >= 0 && sy < fh) {
                    memcpy(fb + sy * fw, rows, (size_t)fw * 2);
                }
            } else {
                uint16_t *odd = rows + fw;
                row15(odd, r, phx, fw);
                if (sy + 1 >= 0 && sy + 1 < fh) {
                    for (int x = 0; x < fw; x++) {
                        rows[x] = px_avg(rows[x], odd[x]);
                    }
                    memcpy(fb + (sy + 1) * fw, rows, (size_t)fw * 2);
                }
                if (sy + 2 >= 0 && sy + 2 < fh) {
                    memcpy(fb + (sy + 2) * fw, odd, (size_t)fw * 2);
                }
            }
        }
    }
}
