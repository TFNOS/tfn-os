// SCENES: the pixel-art engine (a C copy of the parts of the JavaScript scene engine that the scenes use). The hashes and the noise give the same values as the JS, so the patterns match the web page.
//
// Colours in the code: K(0xRRGGBB) = 0xFFRRGGBB; 0 = no pixel. Pixels in a surface: RGB565 in the LCD byte order
// (the bytes swapped), 0 = no pixel (a real black is stored as a very dark green, SC_BLACK565).
// A surface has an ORIGIN (ox, oy): every draw call takes WORLD coordinates and clips to the surface. The frame of a
// scene covers only the window of the camera, so a scene draws only the pixels that the board shows.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// ---------------------------------------------------------------- the boards (views)
enum {
    SCN_349,
    SCN_S3,
    SCN_WT,
    SCN_349B,
    SCN_BOARDS
};  // 640 x 172, 320 x 240 (also the AMOLED), 206 x 206 (2x, round),
// 320 x 86 (2x: the BIG PIXELS of the 3.49)
extern const int SCN_VW[SCN_BOARDS], SCN_VH[SCN_BOARDS], SCN_VS[SCN_BOARDS];

// ---------------------------------------------------------------- colours
typedef uint32_t scol;
#define K(h)        ((scol)(0xFF000000u | (uint32_t)(h)))
#define SC_BLACK565 0x2000u  // swapped 0x0020: the stored value of a real black
static inline int sc_r(scol c)
{
    return (int)((c >> 16) & 255);
}
static inline int sc_g(scol c)
{
    return (int)((c >> 8) & 255);
}
static inline int sc_b(scol c)
{
    return (int)(c & 255);
}
scol sc_pack(float r, float g, float b);  // rounded and clamped, as PX2.pack
scol sc_mix(scol a, scol b, float t);
scol sc_mulc(scol a, float k);
scol sc_addc(scol a, scol b, float k);
// a colour -> the stored pixel (LCD order), and back
static inline uint16_t sc_565(scol c)
{
    if (!c) {
        return 0;
    }
    uint16_t v = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
    if (!v) {
        v = 0x0020;
    }
    return (uint16_t)((v >> 8) | (v << 8));
}
// mix a stored pixel toward a colour by k / 256 (integer: a float mix for each pixel is slow on the board)
static inline uint16_t scn_mixp(uint16_t p, scol c, int k)
{
    uint16_t v = (uint16_t)((p >> 8) | (p << 8));
    int r = ((v >> 8) & 0xF8) + 4, g = ((v >> 3) & 0xFC) + 2, b = ((v << 3) & 0xF8) + 4;  // the middle of the 565 step
    r += ((sc_r(c) - r) * k + 128) >> 8;
    g += ((sc_g(c) - g) * k + 128) >> 8;
    b += ((sc_b(c) - b) * k + 128) >> 8;
    v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    if (!v) {
        v = 0x0020;
    }
    return (uint16_t)((v >> 8) | (v << 8));
}
static inline scol sc_888(uint16_t p)
{
    if (!p) {
        return 0;
    }
    uint16_t v = (uint16_t)((p >> 8) | (p << 8));
    uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}
// a ramp of n colours over the key colours (PX2.ramp); out gets n colours
void sc_ramp(const scol *keys, int nk, int n, scol *out);
// a colour of a ramp at level v, ordered dither between two steps (k = the dither width: 1 full, 0 flat)
scol sc_pick(const scol *rp, int n, float v, int x, int y, float k);

// ---------------------------------------------------------------- numbers (the same results as the JS)
static inline float sc_clamp(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float sc_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
static inline float sc_smooth(float a, float b, float x)
{
    float t = sc_clamp((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
// floorf() and roundf() are slow library calls on the ESP32-S3: these are inline (for |x| < 2^31)
static inline int sc_floor(float x)
{
    int i = (int)x;
    return i - (x < (float)i);
}
static inline float sc_fract(float x)
{
    return x - (float)sc_floor(x);
}
static inline int sc_round(float x)
{
    return sc_floor(x + 0.5f);
}  // Math.round
// sinf() and cosf() are slow library calls on the board: a fast sine (error < 0.001; a pixel position can move by one
// pixel where it is on the edge of a rounding)
static inline float sc_sin(float x)
{
    float q = x * 0.15915494f;       // turns
    q -= (float)sc_floor(q + 0.5f);  // -0.5 .. 0.5
    float y = q * 6.2831853f;        // -pi .. pi
    // sin on [-pi, pi]: fold to [-pi/2, pi/2], then an odd polynomial of degree 7
    if (y > 1.5707963f) {
        y = 3.1415927f - y;
    } else if (y < -1.5707963f) {
        y = -3.1415927f - y;
    }
    float y2 = y * y;
    return y * (1.0f + y2 * (-0.16666667f + y2 * (0.0083333310f + y2 * -0.00019840874f)));
}
static inline float sc_cos(float x)
{
    return sc_sin(x + 1.5707963f);
}
static inline int sc_imin(int a, int b)
{
    return a < b ? a : b;
}
static inline int sc_imax(int a, int b)
{
    return a > b ? a : b;
}
static inline int sc_mod(int a, int n)
{
    int m = a % n;
    return m < 0 ? m + n : m;
}  // ((a % n) + n) % n
float sc_bay(int x, int y);  // the 4 x 4 Bayer level (0..1) of a pixel
uint32_t sc_h2u(int a, int b);
static inline float sc_h2(int a, int b)
{
    return (float)sc_h2u(a, b) * (1.0f / 4294967296.0f);
}
float sc_h3(int a, int b, int c);
float sc_vn(float x, int s);
float sc_vn2(float x, float y, int s);
float sc_fbm2(float x, float y, int s, int oct);
typedef struct {
    uint32_t s;
} sc_rng_t;  // PX2.rng (mulberry32)
static inline void sc_rng_init(sc_rng_t *r, int seed)
{
    r->s = (uint32_t)seed;
}
float sc_rnd(sc_rng_t *r);

// ---------------------------------------------------------------- memory: everything a scene allocates goes to a pool
// (PSRAM); scn_pool_free() frees it all when the scene ends. A temporary buffer: scn_free() it at once.
void *scn_alloc(int bytes);  // zeroed; NULL when there is no memory (scn_oom is set)
void scn_free(void *p);
void scn_pool_free(void);
extern bool scn_oom;                       // an allocation failed since the last scn_pool_free()
extern int scn_pool_bytes, scn_pool_peak;  // the bytes in the pool now, and the most since scn_pool_free()
// a long build: call it now and then (scn_surf_new and scn_layer_make do); it sleeps 10 ms at most each 100 ms
void scn_yield(void);

// ---------------------------------------------------------------- surfaces
typedef struct {
    int w, h, ox, oy;  // the size and the world point of pixel (0, 0)
    uint16_t *d;       // w * h stored pixels (0 = no pixel)
} scn_surf;
scn_surf *scn_surf_new(int w, int h);  // in the pool, all pixels 0; NULL = no memory
void scn_surf_del(scn_surf *s);        // a temporary surface
void scn_clear(scn_surf *s, scol c);
static inline void scn_pxi(scn_surf *s, int x, int y, uint16_t p)  // integer world point, a stored pixel
{
    x -= s->ox;
    y -= s->oy;
    if ((unsigned)x < (unsigned)s->w && (unsigned)y < (unsigned)s->h) {
        s->d[y * s->w + x] = p;
    }
}
static inline void scn_px(scn_surf *s, float x, float y, scol c)
{
    scn_pxi(s, sc_round(x), sc_round(y), sc_565(c));
}
static inline uint16_t scn_geti(const scn_surf *s, int x, int y)
{
    x -= s->ox;
    y -= s->oy;
    return ((unsigned)x < (unsigned)s->w && (unsigned)y < (unsigned)s->h) ? s->d[y * s->w + x] : 0;
}
static inline scol scn_get(const scn_surf *s, float x, float y)
{
    return sc_888(scn_geti(s, sc_round(x), sc_round(y)));
}
void scn_rect(scn_surf *s, float x, float y, float w, float h, scol c);
void scn_hl(scn_surf *s, float x0, float x1, float y, scol c);
void scn_vl(scn_surf *s, float x, float y0, float y1, scol c);
void scn_line(scn_surf *s, float x0, float y0, float x1, float y1, scol c);
void scn_ell(scn_surf *s, float cx, float cy, float rx, float ry, scol c);
static inline void scn_disk(scn_surf *s, float cx, float cy, float r, scol c)
{
    scn_ell(s, cx, cy, r, r, c);
}
void scn_ring(scn_surf *s, float cx, float cy, float r, scol c);
void scn_poly(scn_surf *s, const float *xy, int n, scol c);  // xy = x0, y0, x1, y1, ... (n points)
// the same with a colour for each pixel: f(x, y, ctx) -> a colour, or 0 to leave the pixel
typedef scol (*scn_pixfn)(int x, int y, void *ctx);
void scn_poly_fn(scn_surf *s, const float *xy, int n, scn_pixfn f, void *ctx);
void scn_ell_fn(scn_surf *s, float cx, float cy, float rx, float ry, scn_pixfn f, void *ctx);
void scn_fill_fn(scn_surf *s, float x0, float y0, float x1, float y1, scn_pixfn f, void *ctx);
// change the pixels that are there: f(colour, x, y, ctx) -> colour
typedef scol (*scn_tintfn)(scol c, int x, int y, void *ctx);
void scn_tint(scn_surf *s, float x0, float y0, float x1, float y1, scn_tintfn f, void *ctx);
// copy a sprite (its origin is not used) with its top-left at world dx, dy; flip = mirror left-right
void scn_blit(scn_surf *s, const scn_surf *src, float dx, float dy, bool flip);
// additive light: a soft disc of colour col, strength k, in dithered steps (PX2 glow); sy = the y scale (0 = 1)
void scn_glow(scn_surf *s, float cx, float cy, float r, scol col, float k, float sy);
// blend a colour into a pixel (a = 0..1), stepped by the dither
void scn_blend(scn_surf *s, float x, float y, scol c, float a);
// flat rings of light (pix.halo): rings = {radius, strength} pairs, the biggest first; sy = the y scale (0 = 1)
void scn_halo(scn_surf *s, float cx, float cy, const float (*rings)[2], int n, scol col, float sy);
// a 3 x 5 font (PX2.text3); returns the width
int scn_text3(scn_surf *s, const char *str, float x, float y, scol c, int sc);
// a pixel-art pine (pix.pine): pal = 5 colours {outline, dark, mid, light, rim} or one colour (pal1 != 0)
void scn_pine(scn_surf *s, float x, float base, float h, const scol *pal, scol pal1, float wide, int light, int seed);

// ---------------------------------------------------------------- still layers: 8-bit indices + a palette
typedef struct {
    int x0, y0, w, h;  // the layer point of index (0, 0), and the size
    uint8_t *d;        // 0 = no pixel
    uint16_t pal[256];
    int npal;
} scn_layer;
// make a layer from the pixels of src in the layer columns [cx0, cx1) (src coordinates with its origin; the layer is
// cropped to its pixels); NULL = no memory. More than 255 colours: the nearest colour (a warning in the log).
scn_layer *scn_layer_make(const scn_surf *src, int cx0, int cx1);
// draw the layer into s: the world pixel (x, y) gets the layer pixel (x + shx, y + shy) (PX2 compose: shx = PAD - vx * (1 - p))
void scn_layer_draw(scn_surf *s, const scn_layer *L, int shx, int shy);
// the same with no holes (0 = the first colour): a full background
void scn_layer_draw_full(scn_surf *s, const scn_layer *L, int shx, int shy);

// ---------------------------------------------------------------- people (engine2_px.js)
typedef struct {
    scol P, H, h, r, R, s, S, e, k, Kl, c, C, d, m, p, q, b, w, outline;
    int kind;  // SCN_BEANIE, SCN_HOOD, SCN_NONE, SCN_CAP
} scn_look;
enum {
    SCN_BEANIE,
    SCN_HOOD,
    SCN_NONE,
    SCN_CAP
};
// the look of a person (pix.look): 0 = the default of each colour
typedef struct {
    int kind;
    scol pom, hat, hat2, hair, skin, scarf, coat, mitt, pants, boots, outline;
} scn_look_in;
void scn_look_make(const scn_look_in *o, scn_look *L);
typedef struct {
    scn_surf *front[4], *back[4], *side[4], *skate[2], *sit[2];
    scn_look look;
} scn_person;
bool scn_people(const scn_look_in *o, scn_person *P);  // false = no memory
bool scn_child(const scn_look_in *o, scn_person *P);   // front, back, side are 14 x 19; skate as an adult
bool scn_mini(const scn_look_in *o, scn_surf *fr[2]);  // 9 x 12, two walk frames
// a warm copy of a sprite (in the light of a lamp): every colour moves toward #ffc070 by k
scn_surf *scn_warm(const scn_surf *src, float k);
// draw a frame with its feet at (x, fy); flip = face left (pix.drawPerson)
void scn_draw_person(scn_surf *s, const scn_surf *fr, float x, float fy, bool flip);
// a dark line round a sprite (on the free pixels next to it)
void scn_outline(scn_surf *s, scol col);
void scn_stamp(scn_surf *s, const char *const *rows, int nrows, int x0, int y0, const scn_look *L, bool flip);

// ---------------------------------------------------------------- a profile of a frame (the speed work): scn_prof(i) adds the time
// since the last mark to slot i; the app prints the slots each 5 s
#define SCN_PROF_N 16
extern int64_t scn_prof_us[SCN_PROF_N];
extern const char *scn_prof_name[SCN_PROF_N];
void scn_prof_start(void);
void scn_prof(int i, const char *name);

// ---------------------------------------------------------------- the scenes
// A frame is drawn in BANDS of rows in internal RAM (a write to the PSRAM frame costs about 0.1 us a pixel, so each
// pixel is written there once): prep() once a frame (the camera and everything that moves: positions, phases, the
// values of each column), then draw() once for each band. draw() must only draw (the same picture for any band
// height) and should skip what is not in the band (scn_rows).
typedef struct {
    const char *key, *name;
    float dur;                 // the length of its music piece, in seconds
    bool (*build)(int board);  // paint the layers (once, at the start); false = no memory
    void (*prep)(float t, float L, int *vx,
                 int *vy);        // once a frame: the camera (the world point of the view's top left)
    void (*draw)(scn_surf *out);  // draw the band out (out->ox, out->oy are set by the engine)
    void (*done)(void);           // optional: before the pool is freed
    bool big;                     // the scene is drawn for SCN_349B (its build and prep take that view);
                                  // a scene with no big view shows the middle of its SCN_349 view at 2x
    int8_t big_dx, big_dy;        // that part of the SCN_349 view: moved from the middle (world px)
} scn_def_t;
// does the band have any row of [y0, y1] (world rows)?
static inline bool scn_rows(const scn_surf *s, float y0, float y1)
{
    return y1 >= (float)s->oy && y0 < (float)(s->oy + s->h);
}
// does the band have any pixel of the box [x0, x1] x [y0, y1]?
static inline bool scn_box(const scn_surf *s, float x0, float y0, float x1, float y1)
{
    return y1 >= (float)s->oy && y0 < (float)(s->oy + s->h) && x1 >= (float)s->ox && x0 < (float)(s->ox + s->w);
}
// draw a frame of a scene into a frame buffer (fb, fw x fh, the LCD byte order): prep, then the bands in band (bw x bh
// pixels; internal RAM), each written once at scale sc (1 or 2). fade = 0..256 (256 = full light); mix > 0: the band is
// mixed from the frozen frame (the same size as fb) by mix / 256.
// board SCN_349B with a scene that has no big view: its SCN_349 camera, the middle 320 x 86 of it (build it for SCN_349)
void scn_render(const scn_def_t *S, float t, float L, int board, uint16_t *band, int band_rows, uint16_t *fb, int fw,
                int fh, int fade, const uint16_t *freeze, int mix);
// a frame at 1.5x (the close view of STATION X on the 3.49): each 2 x 2 world pixels give 3 x 3 screen
// pixels, and the middle row and the middle column are the mean of their two neighbours (thus the dither of the light
// makes no stripes). The screen pixel (X, Y) shows the pixel (cx + X, cy + Y) of the whole world at 1.5x, so the
// camera moves in steps of one screen pixel. prep() is called as in scn_render, but its camera is not used. buf: buf_px
// pixels of work memory (internal RAM): two screen rows and the band (at least one row of fw * 2 / 3 + 4 pixels).
void scn_render15(const scn_def_t *S, float t, float L, int cx, int cy, uint16_t *buf, int buf_px, uint16_t *fb, int fw,
                  int fh, int fade);
// the range of the camera x of a path over the scene (for the crop of the layers): path(t, L, &vx, &vy)
void scn_path_range(void (*path)(float t, float L, int *vx, int *vy), float L, int *vx0, int *vx1, int *vy0, int *vy1);
// the layer columns that a parallax layer needs: x in [vx0, vx1 + w) of the world, shift = pad - vx * (1 - p)
void scn_par_cols(int pad, float p, int vx0, int vx1, int vw, int *cx0, int *cx1);
