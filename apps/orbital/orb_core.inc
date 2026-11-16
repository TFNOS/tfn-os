// ORBITAL: the core (included by app_orbital.c). The screen, small math helpers, colours, a random generator and
// the noise of the planet. The world units: km and s (the mission clock); the HUD shows km, m and m/s.

#define SW    gfx_w
#define SH    gfx_h
#define PI_F  3.14159265f
#define TAU_F 6.28318531f

#ifdef ESP_PLATFORM
static void *orb_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void orb_free(void *p)
{
    heap_caps_free(p);
}
#else
static void *orb_alloc(size_t n)
{
    return malloc(n);
}
static void orb_free(void *p)
{
    free(p);
}
#endif
static inline int clampi(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}
static inline float smooth01(float t)
{
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}
static inline float absf(float v)
{
    return v < 0 ? -v : v;
}
static inline float sqf(float v)
{
    return v * v;
}
// an angle into -pi .. pi
static inline float wrap_pi(float a)
{
    if (a > 1000 || a < -1000) {
        return 0;
    }
    while (a > PI_F) {
        a -= TAU_F;
    }
    while (a < -PI_F) {
        a += TAU_F;
    }
    return a;
}
// a soft follow: the part of the way that a value goes in dt with the time constant tau
static inline float follow_k(float dt, float tau)
{
    return tau <= 0 ? 1 : 1 - expf(-dt / tau);
}

// fast math with no call of the library (in an app file sinf, atan2f and sqrtf are slow calls): fsin / fcos of gfx.h
// (a table), 1 / sqrt with three Newton steps (an error under 0.0005 %), and an atan2 with an error under 0.0002 rad
static inline float frsqrt(float x)
{
    union {
        float f;
        uint32_t i;
    } u = {x};
    u.i = 0x5f375a86u - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}
static inline float fsqrt(float x)
{
    return x > 1e-20f ? x * frsqrt(x) : 0;
}
static inline float fatan2(float y, float x)
{
    float ax = absf(x), ay = absf(y);
    if (ax < 1e-20f && ay < 1e-20f) {
        return 0;
    }
    float mn = ax < ay ? ax : ay, mx = ax < ay ? ay : ax, a = mn / mx, s = a * a;
    // (a polynomial of atan on 0..1)
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0) {
        r = PI_F - r;
    }
    if (y < 0) {
        r = -r;
    }
    return r;
}
#define sinf(x)      fsin(x)
#define cosf(x)      fcos(x)
#define atan2f(y, x) fatan2(y, x)

static uint32_t s_rng = 0x2545F491u;
static inline uint32_t rnd32(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static inline float rndf(void)
{
    return (rnd32() & 0xFFFFFF) / 16777216.0f;
}
static inline float rnds(void)
{
    return rndf() * 2 - 1;
}
static inline uint32_t hash2(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
// value noise 0..1 (smooth) that repeats each `per` cells along x (the angle round the planet)
static float vnoise_p(float x, float y, int per, int seed)
{
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    int x0 = ((ix % per) + per) % per, x1 = (x0 + 1) % per;
    float a = (hash2(x0 + seed * 131, iy) & 0xFFFF) / 65535.0f, b = (hash2(x1 + seed * 131, iy) & 0xFFFF) / 65535.0f;
    float c = (hash2(x0 + seed * 131, iy + 1) & 0xFFFF) / 65535.0f,
          d = (hash2(x1 + seed * 131, iy + 1) & 0xFFFF) / 65535.0f;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fy);
}
// a sum of octaves (0..1)
static float fbm_p(float x, float y, int per, int seed, int oct)
{
    float s = 0, w = 0.5f, tw = 0;
    for (int o = 0; o < oct; o++) {
        s += vnoise_p(x, y, per, seed + o * 7) * w;
        tw += w;
        x *= 2;
        y *= 2;
        per *= 2;
        w *= 0.5f;
    }
    return s / tw;
}

// ---- colours (rgb() of gfx.h: the byte order of the panel)
static inline uint16_t rgbc(int r, int g, int b)
{
    return rgb((uint8_t)clampi(r, 0, 255), (uint8_t)clampi(g, 0, 255), (uint8_t)clampi(b, 0, 255));
}
// a mix of two frame colours, k = 0..32 (0 = a, 32 = b)
static inline uint16_t mix565(uint16_t a, uint16_t b, int k)
{
    uint32_t x = (uint16_t)((a >> 8) | (a << 8)), y = (uint16_t)((b >> 8) | (b << 8));
    x = (x | (x << 16)) & 0x07E0F81Fu;
    y = (y | (y << 16)) & 0x07E0F81Fu;
    uint32_t r = ((x * (uint32_t)(32 - k) + y * (uint32_t)k) >> 5) & 0x07E0F81Fu;
    uint16_t o = (uint16_t)(r | (r >> 16));
    return (uint16_t)((o >> 8) | (o << 8));
}
// light that adds, k = 0..32
static inline uint16_t add565(uint16_t d, uint16_t s, int k)
{
    uint16_t D = (uint16_t)((d >> 8) | (d << 8)), S = (uint16_t)((s >> 8) | (s << 8));
    int r = (D >> 11) + (((S >> 11) * k) >> 5), g = ((D >> 5) & 63) + ((((S >> 5) & 63) * k) >> 5),
        b = (D & 31) + (((S & 31) * k) >> 5);
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
typedef struct {
    uint8_t r, g, b;
} col3_t;
static inline col3_t c3(int r, int g, int b)
{
    col3_t c = {(uint8_t)clampi(r, 0, 255), (uint8_t)clampi(g, 0, 255), (uint8_t)clampi(b, 0, 255)};
    return c;
}
static inline col3_t c3mix(col3_t a, col3_t b, float t)
{
    return c3((int)lerpf(a.r, b.r, t), (int)lerpf(a.g, b.g, t), (int)lerpf(a.b, b.b, t));
}
static inline col3_t c3mul(col3_t a, float k)
{
    return c3((int)(a.r * k), (int)(a.g * k), (int)(a.b * k));
}
static inline uint16_t c3p(col3_t c)
{
    return rgb(c.r, c.g, c.b);
}

// the colours of the look: the amber of the HUD and of the tug, the cyan of the target and the tether
#define AMB_R 255
#define AMB_G 179
#define AMB_B 102
#define CYA_R 127
#define CYA_G 227
#define CYA_B 255
#define C_AMB rgb(AMB_R, AMB_G, AMB_B)
#define C_CYA rgb(CYA_R, CYA_G, CYA_B)
#define C_GRN rgb(120, 240, 150)
#define C_RED rgb(255, 92, 70)
#define C_WHT rgb(255, 244, 228)
#define C_DIM rgb(176, 128, 82)
