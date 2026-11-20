// HOURGLASS: a real timer in the form of an hourglass (UTIL).
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_hourglass at the end of this
// file); while the material runs or the alarm rings, update also runs with the screen off (hg_background).
// The memory: the file statics; the two screen pictures s_room and s_full (PSRAM) come at enter and go at leave.
//
// The mechanics:
// - The glass is a solid of revolution: each bulb has a profile r(z) (z = the distance from the neck), so the
//   volume of a slice is pi r^2 dz. The level of the material follows from the VOLUME (tables), so it falls slowly
//   where the bulb is wide and fast near the neck.
// - Sand flows at a constant volume rate (Beverloo's law: the rate through an orifice does not depend on the height
//   of the sand over it), so the volume in the top falls linearly with the time. A liquid flows by Torricelli's law
//   (the rate goes with the square root of the head), so it slows down; the table G(u) gives the drain time of each
//   fill so that a full drain takes exactly the set time.
// - Sand forms a funnel (a crater) in the top and a cone at its angle of repose in the bottom; a liquid has level
//   surfaces that stay level when the board tilts (sand surfaces do not move: the angle of repose holds them).
// - The stream falls along the real gravity. The flow slows when the hourglass tilts and stops when it lies on its
//   side (past about 70 degrees). Flat on a table, the hourglass stands (the table mode).
// - Turn the board over: the ends change places; the material falls to the neck side of its new bulb, then runs
//   again. Turned over in the middle of a run, the time left = the time that ran (as a real hourglass).
// The Watcher has no motion sensor: a push of the wheel turns the hourglass over (an animation), a turn sets the time.
//
// The file is compiled with -Os; the row drawing is -O2.
#pragma GCC optimize("Os", "fast-math")

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"

#if defined(__GNUC__) && !defined(__clang__)
#define HOT __attribute__((optimize("O2")))
#else
#define HOT
#endif

static const char *TAG = "hourglass";

// ---------------------------------------------------------------- the layout of each board

#if BOARD_BAR_SCREEN
#define SCR_W  172
#define SCR_H  640
#define CX     86   // the axis of the hourglass
#define NECK_Y 340  // the neck
#define BULB_L 236  // the inner length of each bulb (the neck to the inner end)
#define R_MAX  54   // the widest inner radius
#define NECK_R 2.5f
#define GT     3  // the glass thickness
#define CAP_H  22
#define CAP_W  76  // the half width of the caps
#define POST_X 68  // the side posts: the offset from the axis
#define POST_R 5
#define TIME_X (SCR_W / 2)
#define TIME_Y 46  // the baseline of the time
#define HINT_Y 66
#elif BOARD_UI_TABLET
// the Tab5 stands upright: the 360 x 640 frame; the time at the top, a big hourglass under it
#define SCR_W  360
#define SCR_H  640
#define CX     180
#define NECK_Y 370
#define BULB_L 222
#define R_MAX  84
#define NECK_R 3.5f
#define GT     4
#define CAP_H  26
#define CAP_W  116
#define POST_X 100
#define POST_R 7
#define TIME_X (SCR_W / 2)
#define TIME_Y 58
#define HINT_Y 84
#elif BOARD_ROUND
// The round frame of the panel (APP_ROUND_HI): 412 x 412 on the Watcher, 466 x 466 on the AMOLED-1.75C. The numbers
// are for 412; RI() and RF scale them (on the Watcher they stay the same). The tables of the bulb have the size of
// the biggest frame (BULB_LM, R_MAXM).
#define RF      ((float)gfx_w / 412.0f)
#define RI(v)   ((int)(v) * gfx_w / 412)
#define SCR_W   gfx_w
#define SCR_H   gfx_h
#define CX      RI(168)
#define NECK_Y  RI(206)
#define BULB_L  RI(132)
#define BULB_LM 156  // (BULB_L at 480 px)
#define R_MAX   RI(50)
#define R_MAXM  60
#define NECK_R  (2.5f * RF)
#define GT      RI(3)
#define CAP_H   RI(14)
#define CAP_W   RI(66)
#define POST_X  RI(60)
#define POST_R  RI(5)
#define TIME_X  RI(322)
#define TIME_Y  RI(214)
#define HINT_Y  RI(240)
#elif BOARD_STANDS_LAND && !defined(HG_PORTRAIT)
// the CoreS3 stands in landscape: the hourglass stands in the 320 x 240 frame, the time at the right
#define HG_LAND 1
#define SCR_W   320
#define SCR_H   240
#define CX      112
#define NECK_Y  118
#define BULB_L  92
#define R_MAX   34
#define NECK_R  2.0f
#define GT      2
#define CAP_H   10
#define CAP_W   48
#define POST_X  43
#define POST_R  4
#define TIME_X  248
#define TIME_Y  128
#define HINT_Y  150
#else
// a portrait 240 x 320 frame (the AMOLED: the board stands upright)
#define SCR_W  240
#define SCR_H  320
#define CX     92
#define NECK_Y 158
#define BULB_L 116
#define R_MAX  40
#define NECK_R 2.0f
#define GT     2
#define CAP_H  14
#define CAP_W  56
#define POST_X 50
#define POST_R 4
#define TIME_X 202
#define TIME_Y 150
#define HINT_Y 172
#endif
#ifndef HG_LAND
#define HG_LAND 0
#endif
#ifndef BULB_LM
#define BULB_LM BULB_L
#define R_MAXM  R_MAX
#define RF      1.0f
#define RI(v)   (v)
#endif
#if BOARD_ROUND
#define TIME_HIT_H RI(70)  // the tap zone of the time (clock / timer switch): 6.2 mm on the Watcher
#else
#define TIME_HIT_H 60
#endif
#define HG_BASE (HG_LAND ? ORIENT_LAND_A : ORIENT_PORTRAIT)  // the upright orientation, and turned over
#define HG_FLIP (HG_LAND ? ORIENT_LAND_B : ORIENT_PORTRAIT_FLIP)
#define TOP_END (NECK_Y - BULB_L)       // the inner end of the top bulb
#define BASE_Y  (NECK_Y + BULB_L)       // the inner end of the bottom bulb
#define CAP_T0  (TOP_END - GT - CAP_H)  // the top cap: CAP_T0 .. TOP_END - GT
#define CAP_B1  (BASE_Y + GT + CAP_H)   // the bottom cap: BASE_Y + GT .. CAP_B1
#define TABLE_Y CAP_B1                  // the table top: the hourglass stands on it
#define SIN_E   0.22f                   // the view from a little above: a circle is an ellipse of this ratio
#define FILL    0.60f                   // the material fills this part of one bulb

// ---------------------------------------------------------------- the designs and the materials

enum {
    P_ROUND,
    P_PEAR,
    P_CONE,
    P_CAPSULE
};
enum {
    POSTS_NONE,
    POSTS_TURNED,
    POSTS_RODS,
    POSTS_COLUMNS
};

typedef struct {
    const char *name;
    uint8_t prof, posts;
    uint8_t wall0[3], wall1[3], glow[3];  // the wall (top, bottom), the light behind the hourglass
    uint8_t table0[3], table1[3];         // the table (near, far)
    uint8_t lo[3], hi[3];                 // the frame material (shadow, light)
} design_t;

static const design_t DESIGNS[] = {
        {"OAK",
         P_ROUND,
         POSTS_TURNED,
         {52, 36, 28},
         {26, 18, 14},
         {120, 82, 50},
         {84, 54, 32},
         {54, 34, 20},
         {58, 32, 16},
         {170, 112, 64}},
        {"BRASS",
         P_PEAR,
         POSTS_RODS,
         {26, 32, 54},
         {10, 12, 24},
         {70, 84, 130},
         {44, 46, 54},
         {24, 26, 32},
         {112, 76, 22},
         {255, 222, 120}},
        {"MODERN",
         P_CONE,
         POSTS_NONE,
         {206, 208, 214},
         {170, 172, 180},
         {250, 250, 252},
         {228, 228, 232},
         {196, 196, 202},
         {20, 20, 24},
         {96, 98, 108}},
        {"MARBLE",
         P_CAPSULE,
         POSTS_COLUMNS,
         {22, 50, 40},
         {8, 22, 18},
         {80, 120, 90},
         {30, 30, 34},
         {12, 12, 14},
         {150, 150, 146},
         {250, 248, 240}},
};
#define N_DESIGNS ((int)(sizeof(DESIGNS) / sizeof(DESIGNS[0])))

typedef struct {
    const char *name;
    uint8_t dark[3], base[3], light[3];
    uint8_t glint;  // sparkles (0 = none)
    float repose;   // the angle of repose, degrees (a liquid: 0)
} mat_t;

static const mat_t MATS[] = {
        {"SAND", {150, 120, 78}, {226, 204, 156}, {252, 240, 210}, 0, 34},
        {"GOLD", {120, 74, 14}, {222, 168, 54}, {255, 236, 150}, 1, 32},
        {"ONYX", {14, 14, 18}, {54, 52, 60}, {120, 118, 130}, 1, 33},
        {"ROSE", {150, 70, 90}, {232, 150, 168}, {255, 214, 222}, 0, 34},
        {"WATER", {20, 70, 150}, {60, 140, 230}, {190, 230, 255}, 0, 0},
};
#define N_MATS  ((int)(sizeof(MATS) / sizeof(MATS[0])))
#define M_WATER 4

static const uint16_t DURS[] = {30, 60, 120, 180, 300, 600, 900, 1200, 1800, 2700, 3600};  // seconds
#define N_DURS ((int)(sizeof(DURS) / sizeof(DURS[0])))
enum {
    AL_OFF,
    AL_ONCE,
    AL_REPEAT
};
static const char *const ALARMS[] = {"OFF", "ONCE", "REPEAT"};
enum {
    SN_OFF,
    SN_SOFT,
    SN_LOUD
};
static const char *const SOUNDS[] = {"OFF", "SOFT", "LOUD"};

// The sound of the falling material (the panel SOUND): a soft hiss (a noise drone; its rate is the colour) and the
// single grains that land on the pile (short voices, at the moments when the picture shows a grain bounce). A liquid:
// a trickle, the bubbles that rise in the top (a blip that goes up) and the drops of a thin jet (a plink).
typedef struct {
    float bed_hz;                        // the colour of the hiss (the noise rate)
    uint8_t bed_vol;                     // its level at LOUD and a full stream
    uint8_t tick_wave;                   // the grains: W_NOISE (a dry click) or W_SINE (a metal ting)
    uint16_t tick_f0, tick_f1;           // the pitch range of a grain (a random pitch in it)
    uint8_t tick_ms0, tick_ms1;          // its length range
    uint8_t tick_vol, tick_lp, tick_hp;  // its level and filters
    uint8_t tick_rate;                   // grains a second at a full stream
} snd_mat_t;

static const snd_mat_t SND[] = {
        {2400, 9, W_NOISE, 3000, 6500, 3, 7, 10, 0, 140, 24},     // SAND: a dry, even hiss with fine clicks
        {3300, 7, W_SINE, 5200, 9200, 6, 16, 6, 0, 0, 20},        // GOLD: a bright hiss with small metal tings
        {1250, 11, W_NOISE, 1400, 3200, 5, 12, 13, 150, 60, 15},  // ONYX: coarse and dark, fewer and louder grains
        {4300, 6, W_NOISE, 5500, 9500, 2, 4, 7, 0, 200, 11},      // ROSE: a very fine, silky whisper
        {700, 8, W_SINE, 320, 640, 22, 40, 9, 0, 0, 0},           // WATER: a trickle; the blips come from the bubbles
};

// ---------------------------------------------------------------- the state

static struct {
    int des, mat, dur, alarm, sound;  // the choices (saved)
    float bed_sent_vol, bed_sent_t;   // the hiss: the level sent to the drone, the time of the last send
    bool clock;                       // the time shows (saved)
    float ua;                         // the part of the material in the physical end A (0..1)
    bool top_a;                       // the end A is the top now
    float p;                          // (not saved) the flow clock is ua / G
    bool was_running;
    float settle_t, settle_len;  // the material falls to its rest after a turn
    float off_top0, off_bot0;    // the start offsets of that fall (px, toward the top of the screen)
    float flip_t;                // the squash turn (the Watcher, START): 0 = none, else the time in it
    bool flip_reset;             // the squash turn ends with all the material in the top
    float crater;                // the funnel of the top (0..1): it grows after a turn
    float alpha;                 // the tilt of the gravity (radians, + = the right side down)
    float flow_f;                // the flow factor of the tilt (1 = upright)
    float pile_x;                // the apex of the bottom cone (px from the axis)
    bool alarm_on;               // the alarm sounds (until a tap or a turn)
    float alarm_t, alarm_next, alarm_total;
    bool panel;
    int focus;      // the panel row of the wheel
    float toast_t;  // the time value shows for a moment (the wheel)
    float t, last_s;
    int64_t last_ms, left_at_ms;
    float g_py;  // the filtered physical gravity along the long side
    bool entered_once;
} s_st;

// ---------------------------------------------------------------- the glass profile and the volume tables

static float s_r[BULB_LM + 1];       // the inner radius at the distance z from the neck
static int s_ri[BULB_LM + 1];        // its integer half width (the interior pixels: |x| <= s_ri)
static float s_vflat[BULB_LM + 2];   // the top: the volume under a flat surface at the height z
static float s_vfun[BULB_LM + 2];    // the top: the volume under a funnel with its rim at the height z
static float s_vbflat[BULB_LM + 2];  // the bottom: the volume under a level surface at the height h
#define PILE_N (BULB_L + 2 * R_MAX + 4)
static float s_vpile[BULB_LM + 2 * R_MAXM + 4];  // the bottom: the volume under a cone with its apex at the height h
static float s_vbulb, s_vtot;
static float s_tan;  // tan of the angle of repose of the material
#define NG 128
static float s_g[NG + 1];  // the drain time of the fill u = k / NG (0..1, a liquid; sand: u)
static int s_tables_for = -1;

static float profile(int prof, float u)
{
    float n = NECK_R / R_MAX, p;
    switch (prof) {
    case P_PEAR:  // a long concave taper, the widest part near the end
        p = u < 0.80f ? powf(u / 0.80f, 0.80f) : 1.0f - 0.32f * powf((u - 0.80f) / 0.20f, 2.0f);
        break;
    case P_CONE:  // straight sides, a round end
        p = u < 0.86f ? u / 0.86f : 1.0f - 0.18f * powf((u - 0.86f) / 0.14f, 2.0f);
        break;
    case P_CAPSULE: {  // a short taper, straight walls, a round end
        float a = u < 0.30f ? u / 0.30f : 1.0f;
        p = sqrtf(a * (2.0f - a));
        if (u > 0.88f) {
            p *= 1.0f - 0.25f * powf((u - 0.88f) / 0.12f, 2.0f);
        }
        p *= 0.92f;
        break;
    }
    default: {  // round bulbs: a smooth taper to the widest part, a round end
        float a = u < 0.56f ? u / 0.56f : 1.0f;
        p = powf(sinf(a * 1.5707963f), 0.85f);
        if (u > 0.56f) {
            p = 1.0f - 0.28f * powf((u - 0.56f) / 0.44f, 2.2f);
        }
        break;
    }
    }
    return n + (1.0f - n) * (p < 0 ? 0 : p);
}

// the drain time table of a liquid: the head over the neck is the height of the flat top surface
static float level_of(const float *v, int n, float vol);

static void tables_build(void)
{
    int key = s_st.des * 16 + s_st.mat;
    if (key == s_tables_for) {
        return;
    }
    s_tables_for = key;
    const design_t *d = &DESIGNS[s_st.des];
    for (int z = 0; z <= BULB_L; z++) {
        s_r[z] = R_MAX * profile(d->prof, (float)z / BULB_L);
        s_ri[z] = (int)s_r[z];
    }
    float rep = MATS[s_st.mat].repose;
    s_tan = tanf((rep > 0 ? rep : 34.0f) * 0.0174533f);
    // the top: flat and funnel surfaces at each height z (the slice z .. z + 1 has the radius s_r[z])
    s_vflat[0] = 0;
    for (int z = 0; z <= BULB_L; z++) {
        s_vflat[z + 1] = s_vflat[z] + 3.14159f * s_r[z] * s_r[z];
    }
    s_vbulb = s_vflat[BULB_L];
    s_vtot = FILL * s_vbulb;
    for (int zc = 0; zc <= BULB_L + 1; zc++) {
        float R = s_r[zc > BULB_L ? BULB_L : zc], z0 = (float)zc - R * s_tan, v = 0;
        for (int z = 0; z < zc && z <= BULB_L; z++) {
            float rin = ((float)z + 0.5f - z0) / s_tan;
            if (rin < 0) {
                rin = 0;
            }
            if (rin > s_r[z]) {
                rin = s_r[z];
            }
            v += 3.14159f * (s_r[z] * s_r[z] - rin * rin);
        }
        s_vfun[zc] = v;
    }
    // the bottom: h = the height above the inner end; the slice at the height h has the radius s_r[BULB_L - h]
    s_vbflat[0] = 0;
    for (int h = 0; h <= BULB_L; h++) {
        float r = s_r[BULB_L - h];
        s_vbflat[h + 1] = s_vbflat[h] + 3.14159f * r * r;
    }
    for (int k = 0; k < PILE_N; k++) {
        float hp = (float)k, v = 0;
        for (int h = 0; h <= BULB_L && h < k; h++) {
            float r = s_r[BULB_L - h], rc = (hp - (float)h - 0.5f) / s_tan;
            if (rc > r) {
                rc = r;
            }
            if (rc > 0) {
                v += 3.14159f * rc * rc;
            }
        }
        s_vpile[k] = v;
    }
    // the drain time: dt = dV / sqrt(head); sand: linear
    s_g[0] = 0;
    for (int k = 1; k <= NG; k++) {
        float u = ((float)k - 0.5f) / NG;
        float w = 1.0f;
        if (s_st.mat == M_WATER) {
            float head = level_of(s_vflat, BULB_L + 2, u * s_vtot) + 3.0f;  // (+ the neck length)
            w = 1.0f / sqrtf(head);
        }
        s_g[k] = s_g[k - 1] + w;
    }
    for (int k = 1; k <= NG; k++) {
        s_g[k] /= s_g[NG];
    }
}

// the height where a monotonic volume table reaches vol (linear between the entries)
static float level_of(const float *v, int n, float vol)
{
    if (vol <= 0) {
        return 0;
    }
    int lo = 0, hi = n - 1;
    if (vol >= v[hi]) {
        return (float)hi;
    }
    while (hi - lo > 1) {
        int m = (lo + hi) / 2;
        if (v[m] < vol) {
            lo = m;
        } else {
            hi = m;
        }
    }
    float dv = v[hi] - v[lo];
    return (float)lo + (dv > 0 ? (vol - v[lo]) / dv : 0);
}

// the drain time of the fill u (0..1 of the time) and its inverse
static float g_of(float u)
{
    if (u <= 0) {
        return 0;
    }
    if (u >= 1) {
        return 1;
    }
    float f = u * NG;
    int k = (int)f;
    return s_g[k] + (s_g[k + 1] - s_g[k]) * (f - (float)k);
}

static float g_inv(float g)
{
    if (g <= 0) {
        return 0;
    }
    if (g >= 1) {
        return 1;
    }
    int lo = 0, hi = NG;
    while (hi - lo > 1) {
        int m = (lo + hi) / 2;
        if (s_g[m] < g) {
            lo = m;
        } else {
            hi = m;
        }
    }
    float d = s_g[hi] - s_g[lo];
    return ((float)lo + (d > 0 ? (g - s_g[lo]) / d : 0)) / NG;
}

static inline float u_top(void)
{
    return s_st.top_a ? s_st.ua : 1.0f - s_st.ua;
}
static inline void set_u_top(float u)
{
    s_st.ua = s_st.top_a ? u : 1.0f - u;
}

// the time left in seconds (with the flow of an upright hourglass)
static float time_left(void)
{
    return (float)DURS[s_st.dur] * g_of(u_top());
}

// ---------------------------------------------------------------- colours

typedef struct {
    int r, g, b;
} rgb_t;

static inline uint16_t pack(int r, int g, int b)
{
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

static inline rgb_t unpack(uint16_t v)
{
    uint16_t c = (uint16_t)((v >> 8) | (v << 8));
    rgb_t o = {(c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3};
    return o;
}

static inline rgb_t mixc(rgb_t a, rgb_t b, int k)  // k 0..256 toward b
{
    rgb_t o = {a.r + ((b.r - a.r) * k >> 8), a.g + ((b.g - a.g) * k >> 8), a.b + ((b.b - a.b) * k >> 8)};
    return o;
}

static inline rgb_t c3(const uint8_t *c)
{
    rgb_t o = {c[0], c[1], c[2]};
    return o;
}

// a pixel of the frame buffer mixed toward white (a reflection on the glass), k 0..255
static inline uint16_t whiten(uint16_t v, int k)
{
    uint16_t c = (uint16_t)((v >> 8) | (v << 8));
    int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    r += (31 - r) * k >> 8;
    g += (63 - g) * k >> 8;
    b += (31 - b) * k >> 8;
    c = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((c >> 8) | (c << 8));
}

// half and half of two frame pixels (the liquid over the background)
static inline uint16_t half565(uint16_t a, uint16_t b)
{
    uint16_t x = (uint16_t)((a >> 8) | (a << 8)), y = (uint16_t)((b >> 8) | (b << 8));
    uint16_t c = (uint16_t)(((x >> 1) & 0x7BEF) + ((y >> 1) & 0x7BEF));
    return (uint16_t)((c >> 8) | (c << 8));
}

static uint16_t s_pal[32];              // the material: 0 dark .. 16 base .. 31 light
static uint16_t s_water_tint, s_wl[4];  // the liquid colours for half565: the surface, 4 depths
static int8_t s_nz[256];                // the grain noise
static uint8_t s_hlu[65];               // the glass reflection over u = x / r (-1 .. 1): white 0..255
static uint8_t s_shu[65];               // the body shade over u
static uint8_t s_shc[65];               // the cone shade over v (-1 left .. 1 right)
static uint16_t s_k_text, s_k_dim, s_k_gold, s_k_ink, s_k_plate, s_k_line;

static void palette_build(void)
{
    const mat_t *m = &MATS[s_st.mat];
    rgb_t d = c3(m->dark), b = c3(m->base), l = c3(m->light);
    for (int i = 0; i < 32; i++) {
        rgb_t c = i < 16 ? mixc(d, b, i * 256 / 16) : mixc(b, l, (i - 16) * 256 / 15);
        s_pal[i] = pack(c.r, c.g, c.b);
    }
    s_water_tint = pack(b.r, b.g, b.b);
    for (int i = 0; i < 4; i++) {
        rgb_t c = mixc(b, d, i * 256 / 4);
        s_wl[i] = pack(c.r, c.g, c.b);
    }
}

static void luts_build(void)
{
    uint32_t h = 0x2545F491u;
    for (int i = 0; i < 256; i++) {
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        s_nz[i] = (int8_t)((int)(h % 5) - 2);
    }
    for (int i = 0; i <= 64; i++) {
        float u = (float)i / 32.0f - 1.0f, a = 0;
        float s1 = (u + 0.55f) / 0.10f, s2 = (u - 0.63f) / 0.045f;
        a += 0.36f * expf(-s1 * s1 * s1 * s1);  // the wide streak at the left
        a += 0.16f * expf(-s2 * s2);            // the thin streak at the right
        s_hlu[i] = (uint8_t)(a * 255.0f);
        float lit = cosf(asinf(u < -1 ? -1 : (u > 1 ? 1 : u)) + 0.55f);
        if (lit < 0) {
            lit = 0;
        }
        s_shu[i] = (uint8_t)(9.0f + 14.0f * powf(lit, 0.7f) + (fabsf(u) > 0.9f ? -2.0f : 0.0f));
        s_shc[i] = (uint8_t)(12.0f + 10.0f * (0.5f - 0.5f * u) * (1.0f - 0.3f * u * u));
    }
    s_k_text = rgb(242, 236, 220);
    s_k_dim = rgb(170, 164, 150);
    s_k_gold = rgb(250, 204, 92);
    s_k_ink = rgb(10, 10, 14);
    s_k_plate = rgb(22, 24, 32);
    s_k_line = rgb(90, 96, 116);
}

static inline int lut_u(int x, int ri)  // the LUT index of x in the half width ri
{
    int i = ri > 0 ? 32 + x * 32 / ri : 32;
    return i < 0 ? 0 : (i > 64 ? 64 : i);
}

// the bright spot of the glass in each bulb (white 0..255)
static inline int spot(int x, int y)
{
    int z = y < NECK_Y ? NECK_Y - y : y - NECK_Y;
    float sz = (float)(z - (int)(BULB_L * 0.70f)) / (R_MAX * 0.30f), sx = ((float)x + R_MAX * 0.40f) / (R_MAX * 0.10f);
    float q = sz * sz + sx * sx;
    return q >= 1 ? 0 : (int)(150.0f * (1.0f - q) * (1.0f - q));
}
#define SPOT_Z0 ((int)(BULB_L * 0.70f) - (int)(R_MAX * 0.30f) - 1)
#define SPOT_Z1 ((int)(BULB_L * 0.70f) + (int)(R_MAX * 0.30f) + 1)

// ---------------------------------------------------------------- the static picture

static uint16_t *s_room, *s_full;  // the room only; the room with the hourglass and the empty glass
static int s_cache_for = -1;

// a pixel of the table (y >= TABLE_Y): the colour, the wood grain or the marble veins, the edge, the shadow of the
// hourglass
static rgb_t room_table(int x, int y, rgb_t t0, rgb_t t1)
{
    int k = (SCR_H - 1 - TABLE_Y) > 0 ? (y - TABLE_Y) * 256 / (SCR_H - TABLE_Y) : 0;
    rgb_t c = mixc(t1, t0, k);
    if (s_st.des == 0) {  // wood planks: grain lines
        float g = sinf((float)x * 0.09f + sinf((float)y * 0.7f + (float)x * 0.013f) * 2.0f);
        c = mixc(c, (rgb_t){c.r * 7 / 10, c.g * 7 / 10, c.b * 7 / 10}, (int)(fabsf(g) < 0.12f ? 120 : 0));
    } else if (s_st.des == 3) {  // black marble: thin white veins
        float v = fabsf(sinf((float)x * 0.05f + (float)y * 0.21f + 3.0f * sinf((float)x * 0.017f)));
        if (v < 0.05f) {
            c = mixc(c, (rgb_t){200, 200, 196}, 70);
        }
    }
    if (y == TABLE_Y) {
        c = mixc(c, (rgb_t){255, 255, 255}, 50);  // the edge of the table
    }
    // the shadow of the hourglass on the table
    float sx = (float)(x - CX - 4) / (CAP_W + 10), sy = (float)(y - TABLE_Y - 2) / 7.0f;
    float q = sx * sx + sy * sy;
    if (q < 1) {
        c = mixc(c, (rgb_t){0, 0, 0}, (int)(150 * (1 - q)));
    }
    return c;
}

static void build_room(void)
{
    const design_t *d = &DESIGNS[s_st.des];
    rgb_t w0 = c3(d->wall0), w1 = c3(d->wall1), gl = c3(d->glow), t0 = c3(d->table0), t1 = c3(d->table1);
    float gr = (float)SCR_H * 0.48f;
    for (int y = 0; y < SCR_H; y++) {
        uint16_t *row = s_room + y * SCR_W;
        for (int x = 0; x < SCR_W; x++) {
            rgb_t c;
            if (y < TABLE_Y) {
                c = mixc(w0, w1, y * 256 / TABLE_Y);
                float dx = (float)(x - CX) * 1.3f, dy = (float)(y - NECK_Y), dd = sqrtf(dx * dx + dy * dy) / gr;
                if (dd < 1) {
                    float k = (1 - dd) * (1 - dd);
                    c = mixc(c, gl, (int)(k * 150));
                }
            } else {
                c = room_table(x, y, t0, t1);
            }
            // a soft vignette
            float vx = (float)(x - SCR_W / 2) / (SCR_W * 0.75f), vy = (float)(y - SCR_H / 2) / (SCR_H * 0.75f);
            float v = vx * vx + vy * vy;
            if (v > 0.2f) {
                c = mixc(c, (rgb_t){0, 0, 0}, (int)((v - 0.2f) * 160 > 110 ? 110 : (v - 0.2f) * 160));
            }
            row[x] = pack(c.r, c.g, c.b);
        }
    }
}

// a round post or rod (a vertical cylinder) from y0 to y1; prof = its radius at each row; dim = behind the glass
static void draw_post(uint16_t *img, int cx, int y0, int y1, int style, bool dim)
{
    const design_t *d = &DESIGNS[s_st.des];
    rgb_t lo = c3(d->lo), hi = c3(d->hi);
    for (int y = y0; y < y1; y++) {
        float t = (float)(y - y0) / (float)(y1 - y0), rr = POST_R;
        if (style == POSTS_TURNED) {
            float b1 = (t - 0.06f) / 0.025f, b2 = (t - 0.94f) / 0.025f, b3 = (t - 0.5f) / 0.06f;
            rr = POST_R * (0.70f + 0.50f * expf(-b1 * b1) + 0.50f * expf(-b2 * b2) + 0.30f * expf(-b3 * b3));
        } else if (style == POSTS_RODS) {
            rr = POST_R * 0.55f;
            float b1 = (t - 0.03f) / 0.02f, b2 = (t - 0.97f) / 0.02f;
            rr += POST_R * 0.65f * (expf(-b1 * b1) + expf(-b2 * b2));
        }
        if (dim) {
            rr *= 0.8f;
        }
        int ir = (int)(rr + 0.5f);
        for (int dx = -ir; dx <= ir; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= SCR_W) {
                continue;
            }
            float u = (float)dx / (rr + 0.5f);
            if (u < -1 || u > 1) {
                continue;
            }
            float lit = cosf(asinf(u) + 0.6f);
            if (lit < 0) {
                lit = 0;
            }
            float k = 0.25f + 0.75f * lit;
            if (style == POSTS_COLUMNS) {
                k *= 0.85f + 0.15f * cosf(u * 9.0f);  // the fluting
            }
            if (style == POSTS_TURNED) {
                k *= 0.92f + 0.08f * sinf((float)y * 0.9f + u * 3.0f);  // the grain
            }
            rgb_t c = mixc(lo, hi, (int)(k * 256));
            if (style == POSTS_RODS && u > -0.55f && u < -0.30f) {
                c = mixc(c, (rgb_t){255, 255, 240}, 150);
            }
            if (dim) {
                c = mixc(c, (rgb_t){0, 0, 0}, 120);
            }
            // the soft edge of the cylinder
            float e = rr + 0.5f - fabsf((float)dx);
            if (e < 1) {
                rgb_t b = unpack(img[y * SCR_W + x]);
                c = mixc(b, c, (int)(e * 256));
            }
            img[y * SCR_W + x] = pack(c.r, c.g, c.b);
        }
    }
}

static void draw_cap(uint16_t *img, int y0, int y1, bool top)
{
    const design_t *d = &DESIGNS[s_st.des];
    rgb_t lo = c3(d->lo), hi = c3(d->hi);
    int h = y1 - y0;
    for (int y = y0; y < y1; y++) {
        float t = (float)(y - y0) / (float)h;
        float w = CAP_W * (0.94f + 0.06f * sinf(t * 3.14159f));
        if (s_st.des == 2) {
            w = CAP_W * 0.86f;  // MODERN: a plain disc
        }
        int iw = (int)w;
        for (int dx = -iw - 1; dx <= iw + 1; dx++) {
            int x = CX + dx;
            if (x < 0 || x >= SCR_W) {
                continue;
            }
            float u = (float)dx / w;
            float e = w + 0.5f - fabsf((float)dx);
            if (e <= 0) {
                continue;
            }
            if (u > 1) {
                u = 1;
            }
            if (u < -1) {
                u = -1;
            }
            float lit = cosf(asinf(u) + 0.6f);
            if (lit < 0) {
                lit = 0;
            }
            float k = 0.22f + 0.78f * lit;
            // the vertical shape: a rounded edge at the top and the bottom of the cap
            float vt = sinf(t * 3.14159f);
            k *= 0.70f + 0.30f * vt;
            bool groove = fabsf(t - (top ? 0.62f : 0.38f)) < 0.07f;
            if (groove && s_st.des != 2) {
                k *= 0.62f;
            }
            rgb_t c = mixc(lo, hi, (int)(k * 256));
            if (s_st.des == 0) {  // wood grain
                float g = sinf((float)dx * 0.25f + sinf((float)y * 0.6f) * 1.5f);
                c = mixc(c, lo, fabsf(g) < 0.15f ? 90 : 0);
            } else if (s_st.des == 1) {  // polished brass: a white stripe and a dark band
                if (u > -0.50f && u < -0.32f) {
                    c = mixc(c, (rgb_t){255, 250, 225}, 160);
                }
                if (u > 0.25f && u < 0.45f) {
                    c = mixc(c, lo, 120);
                }
            } else if (s_st.des == 2) {  // MODERN: a glossy edge line
                if (fabsf(t - (top ? 0.9f : 0.1f)) < 0.1f) {
                    c = mixc(c, (rgb_t){200, 204, 214}, 120);
                }
            } else {  // marble veins
                float v = fabsf(
                        sinf((float)dx * 0.11f + (float)y * 0.37f + 2.5f * sinf((float)dx * 0.05f + (float)y * 0.2f)));
                if (v < 0.07f) {
                    c = mixc(c, (rgb_t){110, 110, 112}, 150);
                }
            }
            if (e < 1) {
                c = mixc(unpack(img[y * SCR_W + x]), c, (int)(e * 256));
            }
            img[y * SCR_W + x] = pack(c.r, c.g, c.b);
        }
    }
    // the top face of the upper cap (seen from a little above): a lighter ellipse
    if (top) {
        float rx = CAP_W * 0.94f, ry = CAP_W * SIN_E * 0.35f;
        for (int y = (int)(y0 - ry); y <= y0; y++) {
            if (y < 0) {
                continue;
            }
            float q = (float)(y - y0) / ry, hw = rx * sqrtf(1 - q * q > 0 ? 1 - q * q : 0);
            for (int x = (int)(CX - hw); x <= (int)(CX + hw); x++) {
                if (x < 0 || x >= SCR_W) {
                    continue;
                }
                rgb_t c = mixc(lo, hi, 200);
                if (s_st.des == 1) {
                    c = mixc(c, (rgb_t){255, 248, 220}, 60);
                }
                img[y * SCR_W + x] = pack(c.r, c.g, c.b);
            }
        }
    }
}

static void build_full(void)
{
    const design_t *d = &DESIGNS[s_st.des];
    memcpy(s_full, s_room, (size_t)SCR_W * SCR_H * 2);
    // the back post (seen through the glass)
    if (d->posts != POSTS_NONE) {
        draw_post(s_full, CX, CAP_T0 + CAP_H / 2, CAP_B1 - CAP_H / 2, d->posts, true);
    }
    // the glass: the room seen through it, the reflections, the wall
    rgb_t rim = {214, 232, 240}, edge = {70, 100, 120};
    for (int y = TOP_END - GT; y <= BASE_Y + GT; y++) {
        int z = y < NECK_Y ? NECK_Y - y : y - NECK_Y;
        bool end = z > BULB_L;  // the closing wall of a bulb
        int zz = end ? BULB_L : z;
        int ri = s_ri[zz], gt = GT + (z <= 3 ? 2 : 0);  // (the joint at the neck is thicker)
        uint16_t *row = s_full + y * SCR_W;
        for (int dx = -ri - gt; dx <= ri + gt; dx++) {
            int x = CX + dx;
            if (x < 0 || x >= SCR_W) {
                continue;
            }
            rgb_t c = unpack(row[x]);
            int ax = dx < 0 ? -dx : dx;
            if (end || ax > ri) {
                // the glass wall: light, a little transparent; the outer pixel softer
                int k = ax == ri + gt ? 80 : 120;
                if (end) {
                    k = 110;
                }
                c = mixc(c, rim, k);
            } else {
                c = mixc(c, (rgb_t){200, 220, 230}, 22);  // the glass in front of the room
                int lu = lut_u(dx, ri);
                float uu = (float)dx / (ri > 0 ? ri : 1);
                if (uu > 0.86f || uu < -0.86f) {
                    c = mixc(c, edge, (int)((fabsf(uu) - 0.86f) / 0.14f * 90));
                }
                c = mixc(c, (rgb_t){255, 255, 255}, s_hlu[lu]);
                if (z >= SPOT_Z0 && z <= SPOT_Z1) {
                    c = mixc(c, (rgb_t){255, 255, 255}, spot(dx, y));
                }
            }
            row[x] = pack(c.r, c.g, c.b);
        }
    }
    // the side posts and the caps
    if (d->posts != POSTS_NONE) {
        draw_post(s_full, CX - POST_X, CAP_T0 + CAP_H / 2, CAP_B1 - CAP_H / 2, d->posts, false);
        draw_post(s_full, CX + POST_X, CAP_T0 + CAP_H / 2, CAP_B1 - CAP_H / 2, d->posts, false);
    }
    draw_cap(s_full, CAP_T0, TOP_END - GT, true);
    draw_cap(s_full, BASE_Y + GT + 1, CAP_B1, false);
}

static void caches_build(void)
{
    int key = s_st.des;
    if (key == s_cache_for && s_room && s_full) {
        return;
    }
    s_cache_for = key;
    build_room();
    build_full();
}

// ---------------------------------------------------------------- the shape of the material now

static struct {
    bool liquid, flowing;
    float zc, R, ycap, rimh;  // the top: the rim height above the neck, its radius, its row
    int Ri;
    float rim[R_MAXM + 3];     // the half height of the top face ellipse at |x|
    float hp, yapex, ax, cot;  // the bottom cone: the apex height above the inner end, its row
    float ytop, ybot, tilt;    // a liquid: the surface rows, the slope of the level (tan)
    float off_top, off_bot;    // the fall after a turn
    int t8;
} s_ge;

static float vtop_at(float zc, float c)
{
    if (zc <= 0) {
        return 0;
    }
    if (zc >= BULB_L + 1) {
        zc = BULB_L + 1;
    }
    int k = (int)zc;
    float f = zc - (float)k;
    int k1 = k + 1 > BULB_L + 1 ? BULB_L + 1 : k + 1;
    float vf = s_vflat[k] + (s_vflat[k1] - s_vflat[k]) * f, vu = s_vfun[k] + (s_vfun[k1] - s_vfun[k]) * f;
    return vf + (vu - vf) * c;
}

static float settle_off(float off0, float t)
{
    // the material falls from its old place with the gravity of the picture (it does not move before the turn ends)
    const float g = 6000.0f;
    float tl = sqrtf(2.0f * off0 / g);
    if (off0 <= 0 || t >= tl) {
        return 0;
    }
    float k = t / tl;
    return off0 * (1.0f - k * k);
}

static void geometry(void)
{
    float ut = u_top(), ub = 1.0f - ut;
    s_ge.liquid = MATS[s_st.mat].repose <= 0;
    s_ge.t8 = (int)(s_st.t * 12.0f);
    if (s_ge.liquid) {
        float z = level_of(s_vflat, BULB_L + 2, ut * s_vtot);
        s_ge.ytop = (float)NECK_Y - z;
        s_ge.zc = z;
        s_ge.R = s_r[(int)z > BULB_L ? BULB_L : (int)z];
        float h = level_of(s_vbflat, BULB_L + 2, ub * s_vtot);
        s_ge.ybot = (float)BASE_Y - h;
        float a = s_st.alpha;
        if (a > 0.7f) {
            a = 0.7f;
        }
        if (a < -0.7f) {
            a = -0.7f;
        }
        s_ge.tilt = tanf(a);
    } else {
        // the top: the rim height zc where the volume (flat + funnel) matches
        float v = ut * s_vtot, lo = 0, hi = BULB_L + 1;
        for (int i = 0; i < 18; i++) {
            float m = 0.5f * (lo + hi);
            if (vtop_at(m, s_st.crater) < v) {
                lo = m;
            } else {
                hi = m;
            }
        }
        s_ge.zc = 0.5f * (lo + hi);
        int zi = (int)s_ge.zc > BULB_L ? BULB_L : (int)s_ge.zc;
        s_ge.R = s_r[zi];
        s_ge.ycap = (float)NECK_Y - s_ge.zc;
        s_ge.Ri = (int)s_ge.R;
        for (int x = 0; x <= s_ge.Ri + 1 && x < R_MAX + 3; x++) {
            float q = s_ge.R * s_ge.R - (float)(x * x);
            s_ge.rim[x] = q > 0 ? sqrtf(q) * SIN_E : 0;
        }
        s_ge.rimh = s_ge.R * SIN_E;
        // the bottom: the apex height of the cone that holds the volume
        s_ge.hp = level_of(s_vpile, PILE_N, ub * s_vtot);
        s_ge.yapex = (float)BASE_Y + 1.0f - s_ge.hp;
        s_ge.ax = s_st.pile_x;
        s_ge.cot = 1.0f / s_tan;
    }
    s_ge.flowing = ut > 0 && s_st.settle_t <= 0 && s_st.flip_t <= 0 && s_st.flow_f > 0.02f;
    s_ge.off_top = s_st.settle_t > 0 ? settle_off(s_st.off_top0, s_st.settle_t) : 0;
    s_ge.off_bot = s_st.settle_t > 0 ? settle_off(s_st.off_bot0, s_st.settle_t) : 0;
}

// ---------------------------------------------------------------- the rows of the material

static inline uint16_t glass_over(uint16_t c, int x, int y, int lu)
{
    int k = s_hlu[lu];
    int z = y < NECK_Y ? NECK_Y - y : y - NECK_Y;
    if (z >= SPOT_Z0 && z <= SPOT_Z1) {
        k += spot(x, y);
    }
    return k > 6 ? whiten(c, k > 255 ? 255 : k) : c;
}

static inline uint16_t grain_px(int lvl, int x, int ry)
{
    lvl += s_nz[(x * 3 + ry * 37) & 255];
    if (MATS[s_st.mat].glint && ((x * 13 + ry * 7 + s_ge.t8) & 255) == 0) {
        lvl = 31;
    }
    return s_pal[lvl < 0 ? 0 : (lvl > 31 ? 31 : lvl)];
}

// the top bulb at the display row sy; the material is at its rest row ry (a fall after a turn moves it)
static void HOT top_row(uint16_t *dst, int sy, int ry)
{
    int zd = NECK_Y - sy, zr = NECK_Y - ry;
    if (zr < 0 || zr > BULB_L || zd < 0 || zd > BULB_L) {
        return;
    }
    int ri = s_ri[zd] < s_ri[zr] ? s_ri[zd] : s_ri[zr];
    int rid = s_ri[zd];
    if (s_ge.liquid) {
        float t = s_ge.tilt;
        for (int x = -ri; x <= ri; x++) {
            float ys = s_ge.ytop + (float)x * t;
            float ax = (float)(x < 0 ? -x : x);
            float m = ri > 2 && ax > ri - 3 ? (ax - (ri - 3)) * 0.8f : 0;  // the meniscus at the glass
            ys -= m;
            if ((float)ry < ys - 1.0f) {
                continue;
            }
            uint16_t bg = dst[CX + x];
            uint16_t c;
            if ((float)ry < ys + 1.5f) {
                c = whiten(half565(bg, s_water_tint), 110);  // the surface line
            } else {
                int k = (int)(((float)ry - ys) * (1.0f / 22.0f) + (float)((x ^ ry) & 3) * 0.25f);
                c = half565(bg, s_wl[k > 3 ? 3 : k]);
            }
            dst[CX + x] = glass_over(c, x, sy, lut_u(x, rid));
        }
        return;
    }
    float yc = s_ge.ycap;
    if ((float)ry < yc - s_ge.rimh - 1) {
        return;
    }
    bool below = (float)ry >= yc + s_ge.rimh;
    float inv_r = s_ge.R > 0.5f ? 1.0f / s_ge.R : 0, inv_e = s_ge.rimh > 0.3f ? 1.0f / s_ge.rimh : 0;
    for (int x = -ri; x <= ri; x++) {
        int ax = x < 0 ? -x : x, lu = lut_u(x, rid);
        int lvl = -1;
        if (below) {
            lvl = s_shu[lu];
        } else if (ax <= s_ge.Ri) {
            float fr = yc + s_ge.rim[ax], bk = yc - s_ge.rim[ax];
            if ((float)ry >= fr) {
                lvl = s_shu[lu];
            } else if ((float)ry >= bk) {
                // the top face: lighter; the funnel darker to its middle; the hole where the material goes down
                float ex = (float)x * inv_r, ey = ((float)ry - yc) * inv_e, e = sqrtf(ex * ex + ey * ey);
                float l = 25.0f - s_st.crater * (1.0f - e) * 11.0f + (ey < 0 ? 1.5f : -1.5f) * s_st.crater * (1.0f - e);
                if (s_ge.flowing && e < 0.09f + 0.05f * s_st.crater) {
                    l = 4;
                }
                lvl = (int)l;
            }
        } else if ((float)ry > yc) {
            lvl = s_shu[lu];
        }
        if (lvl < 0) {
            continue;
        }
        dst[CX + x] = glass_over(grain_px(lvl, x, ry), x, sy, lu);
    }
}

static void HOT bottom_row(uint16_t *dst, int sy, int ry)
{
    int zd = sy - NECK_Y, zr = ry - NECK_Y;
    if (zr < 0 || zr > BULB_L || zd < 0 || zd > BULB_L) {
        return;
    }
    int ri = s_ri[zd] < s_ri[zr] ? s_ri[zd] : s_ri[zr];
    int rid = s_ri[zd];
    if (s_ge.liquid) {
        float t = s_ge.tilt;
        for (int x = -ri; x <= ri; x++) {
            float ys = s_ge.ybot + (float)x * t;
            float ax = (float)(x < 0 ? -x : x);
            ys -= ri > 2 && ax > ri - 3 ? (ax - (ri - 3)) * 0.8f : 0;
            if ((float)ry < ys - 1.0f) {
                continue;
            }
            uint16_t bg = dst[CX + x], c;
            if ((float)ry < ys + 1.5f) {
                c = whiten(half565(bg, s_water_tint), 110);
            } else {
                int k = (int)(((float)ry - ys) * (1.0f / 22.0f) + (float)((x ^ ry) & 3) * 0.25f);
                c = half565(bg, s_wl[k > 3 ? 3 : k]);
            }
            dst[CX + x] = glass_over(c, x, sy, lut_u(x, rid));
        }
        return;
    }
    float d = (float)ry - s_ge.yapex;
    if (d <= 0) {
        return;
    }
    float w = d * s_ge.cot, ax = s_ge.ax;
    int x0 = (int)floorf(ax - w), x1 = (int)ceilf(ax + w);
    if (x0 < -ri) {
        x0 = -ri;
    }
    if (x1 > ri) {
        x1 = ri;
    }
    // where the cone is wider than the glass, the front of the material is the body at the glass
    float over = w - ((float)ri + fabsf(ax));
    int tb = over <= 0 ? 0 : (over >= 6 ? 256 : (int)(over * 256 / 6));
    float inv_w = w > 0.5f ? 1.0f / w : 0;
    for (int x = x0; x <= x1; x++) {
        int lu = lut_u(x, rid);
        float v = ((float)x - ax) * inv_w;
        int lv = (int)(32 + v * 32);
        lv = lv < 0 ? 0 : (lv > 64 ? 64 : lv);
        int lvl = (s_shc[lv] * (256 - tb) + s_shu[lu] * tb) >> 8;
        if (d < 5) {
            lvl += 2;
        }
        if (ry != sy && BASE_Y - ry < 14 && (int)((x * 7 + ry * 13 + s_ge.t8 * 5) & 15) < 15 - (BASE_Y - ry)) {
            continue;
        }
        dst[CX + x] = glass_over(grain_px(lvl, x, ry), x, sy, lu);
    }
}

// the material of the source row sy, drawn into the display row dst
static void mat_row(uint16_t *dst, int sy)
{
    if (sy < TOP_END || sy > BASE_Y) {
        return;
    }
    if (sy < NECK_Y - 2) {
        top_row(dst, sy, sy + (int)s_ge.off_top);
    } else if (sy > NECK_Y + 2) {
        bottom_row(dst, sy, sy + (int)s_ge.off_bot);
    } else if (s_ge.flowing) {
        int z = sy < NECK_Y ? NECK_Y - sy : sy - NECK_Y, ri = s_ri[z];
        for (int x = -ri; x <= ri; x++) {
            dst[CX + x] = s_ge.liquid ? whiten(s_water_tint, 60) : s_pal[24];
        }
    }
}

// ---------------------------------------------------------------- the stream and the particles

enum {
    PK_NONE,
    PK_SPLASH,
    PK_SLIDE,
    PK_FUNNEL,
    PK_BUBBLE,
    PK_RIPPLE
};
typedef struct {
    float x, y, vx, vy, a;
    uint8_t kind;
} part_t;
#define N_PARTS 72
static part_t s_parts[N_PARTS];
static float s_spawn_acc, s_bubble_acc, s_ripple_acc, s_funnel_acc;
static uint32_t s_rng = 0x1234567u;

static inline uint32_t rnd(void)
{
    uint32_t r = s_rng;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return s_rng = r;
}
static inline float frnd(float a, float b)
{
    return a + (b - a) * (float)(rnd() & 0xFFFF) / 65535.0f;
}

static part_t *part_new(int kind)
{
    for (int i = 0; i < N_PARTS; i++) {
        if (s_parts[i].kind == PK_NONE) {
            memset(&s_parts[i], 0, sizeof(part_t));
            s_parts[i].kind = (uint8_t)kind;
            return &s_parts[i];
        }
    }
    return NULL;
}

static inline void px_put(int x, int y, uint16_t c)
{
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H) {
        g_fb[y * SCR_W + x] = c;
    }
}

// the stream width: a long timer has a narrow neck
static int stream_w(void)
{
    int T = DURS[s_st.dur];
    return T <= 60 ? 3 : (T <= 300 ? 2 : 1);
}

// the bottom cone surface at x (px from the axis)
static float cone_y(float x)
{
    float y = s_ge.yapex + fabsf(x - s_ge.ax) * s_tan;
    return y > BASE_Y ? (float)BASE_Y : y;
}

// the stream at the row y (px from the axis): it falls along the gravity from the neck; where it reaches the glass it
// runs down along the wall (it never leaves the glass)
static float stream_x(int y)
{
    float a = s_st.alpha > 1.2f ? 1.2f : (s_st.alpha < -1.2f ? -1.2f : s_st.alpha);
    float x = (float)(y - (NECK_Y + 3)) * tanf(a);
    int zb = y - NECK_Y;
    zb = zb < 0 ? 0 : (zb > BULB_L ? BULB_L : zb);
    float lim = (float)s_ri[zb] - 1.0f - (float)stream_w() * 0.5f;
    if (lim < 0) {
        lim = 0;
    }
    return x > lim ? lim : (x < -lim ? -lim : x);
}

// where the stream meets the material in the bottom (the row; *lx = its x from the axis)
static float landing(float *lx)
{
    int y = NECK_Y + 3;
    for (; y < BASE_Y; y++) {
        float x = stream_x(y);
        float surf = s_ge.liquid ? s_ge.ybot + x * s_ge.tilt : cone_y(x);
        if ((float)y >= surf) {
            break;
        }
    }
    *lx = stream_x(y);
    return (float)y;
}

// ---- the sound of the falling material

static bool sound_on(void)
{
    if (s_st.sound == SN_OFF) {
        return false;
    }
    const char *st = app_screen_state();
    return !(st && st[0] == 'o' && st[1] == 'f');  // (silent while the screen is off)
}

static float sound_level(void)
{
    return s_st.sound == SN_LOUD ? 1.0f : 0.5f;
}

// a single grain lands (delay: where in this frame, ms)
static void grain_tick(int delay_ms)
{
    const snd_mat_t *m = &SND[s_st.mat];
    int ms = m->tick_ms0 + (int)(rnd() % (uint32_t)(m->tick_ms1 - m->tick_ms0 + 1));
    float f = (float)m->tick_f0 + (float)(m->tick_f1 - m->tick_f0) * (float)(rnd() & 1023) / 1023.0f;
    int vol = (int)((float)m->tick_vol * sound_level() * (0.6f + 0.4f * (float)(rnd() & 255) / 255.0f) + 0.5f);
    if (vol < 1) {
        vol = 1;
    }
    audio_voice_t v = {.delay = (uint32_t)(AUDIO_SAMPLE_RATE / 1000 * delay_ms),
                       .dur = (uint32_t)(AUDIO_SAMPLE_RATE / 1000 * ms),
                       .f0 = f,
                       .f1 = m->tick_wave == W_SINE ? f * 0.97f : f,
                       .wave = m->tick_wave,
                       .vol = (uint8_t)vol,
                       .attack_ms = 1,
                       .shape = AUDIO_PERC,
                       .lp = m->tick_lp,
                       .hp = m->tick_hp};
    audio_play(&v);
}

// a bubble rises in the top liquid (a blip that goes up), or a drop falls into the bottom liquid (a plink)
static void water_blip(bool drop, int delay_ms)
{
    const snd_mat_t *m = &SND[M_WATER];
    float f = drop ? 1300.0f + (float)(rnd() & 511)
                   : (float)m->tick_f0 + (float)(m->tick_f1 - m->tick_f0) * (float)(rnd() & 1023) / 1023.0f;
    int ms = drop ? 28 : m->tick_ms0 + (int)(rnd() % (uint32_t)(m->tick_ms1 - m->tick_ms0 + 1));
    int vol = (int)((float)m->tick_vol * sound_level() * (drop ? 0.8f : 1.0f) + 0.5f);
    audio_voice_t v = {.delay = (uint32_t)(AUDIO_SAMPLE_RATE / 1000 * delay_ms),
                       .dur = (uint32_t)(AUDIO_SAMPLE_RATE / 1000 * ms),
                       .f0 = f,
                       .f1 = drop ? f * 0.7f : f * 2.3f,
                       .wave = W_SINE,
                       .vol = (uint8_t)(vol < 1 ? 1 : vol),
                       .attack_ms = 3,
                       .shape = AUDIO_PERC,
                       .lp = 0,
                       .hp = 0};
    audio_play(&v);
}

// the hiss: its level follows the stream (a long timer has a narrow neck: a softer hiss); a small wobble of the
// colour each frame makes it move like real grains
static void bed_update(float dt, float q)
{
    const snd_mat_t *m = &SND[s_st.mat];
    float want = 0;
    if (s_ge.flowing && sound_on()) {
        float k = s_ge.liquid ? 0.35f + 0.65f * q : (float)stream_w() / 3.0f;
        if (u_top() < 0.004f) {
            k *= 0.4f;  // the last grains
        }
        want = (float)m->bed_vol * sound_level() * k * s_st.flow_f;
    }
    s_st.bed_sent_t += dt;
    if (want <= 0.2f) {
        if (s_st.bed_sent_vol > 0) {
            audio_drone(0, m->bed_hz, 0, W_NOISE);
        }
        s_st.bed_sent_vol = 0;
        return;
    }
    if (s_st.bed_sent_t < 0.06f && fabsf(want - s_st.bed_sent_vol) < 0.5f) {
        return;
    }
    s_st.bed_sent_t = 0;
    s_st.bed_sent_vol = want;
    float hz = m->bed_hz * (0.9f + 0.2f * (float)(rnd() & 255) / 255.0f);
    audio_drone(0, hz, (int)(want + 0.5f), W_NOISE);
}

static void parts_update(float dt)
{
    float lx = 0, ly = landing(&lx);
    float q = 1.0f;
    if (s_ge.liquid) {
        q = sqrtf((s_ge.zc + 3.0f) / (BULB_L * 0.7f + 3.0f));
    }
    bed_update(dt, q);
    bool snd = sound_on() && s_ge.flowing;
    int frame_ms = (int)(dt * 1000.0f);
    if (frame_ms < 1) {
        frame_ms = 1;
    }
    if (s_ge.flowing) {
        if (s_ge.liquid) {
            s_bubble_acc += dt * 7.0f * q;
            while (s_bubble_acc > 1) {
                s_bubble_acc -= 1;
                part_t *p = part_new(PK_BUBBLE);
                if (p) {
                    p->x = frnd(-1, 1);
                    p->y = NECK_Y - 4;
                    p->a = frnd(1.3f, 3.2f);
                    p->vx = frnd(0, 6.28f);
                }
                if (snd && (rnd() & 7) < 5) {
                    water_blip(false, (int)(rnd() % (uint32_t)frame_ms));
                }
            }
            s_ripple_acc += dt * 6.0f;
            while (s_ripple_acc > 1) {
                s_ripple_acc -= 1;
                part_t *p = part_new(PK_RIPPLE);
                if (p) {
                    p->x = lx;
                    p->y = ly;
                }
                if (snd && q <= 0.45f) {
                    water_blip(true, (int)(rnd() % (uint32_t)frame_ms));  // a thin jet: drops
                }
            }
        } else {
            float spawn_rate = 8.0f + 6.0f * stream_w();
            // the grains that the speaker plays: the rate of the material, less for a narrow stream
            float tick_p = (float)SND[s_st.mat].tick_rate * ((float)stream_w() / 3.0f) * s_st.flow_f / spawn_rate;
            s_spawn_acc += dt * spawn_rate;
            while (s_spawn_acc > 1) {
                s_spawn_acc -= 1;
                part_t *p = part_new(PK_SPLASH);
                if (p) {
                    p->x = lx;
                    p->y = ly - 1;
                    p->vx = frnd(-45, 45);
                    p->vy = frnd(-80, -25);
                }
                if (snd && (float)(rnd() & 1023) < tick_p * 1024.0f) {
                    grain_tick((int)(rnd() % (uint32_t)frame_ms));
                }
            }
            s_funnel_acc += dt * 9.0f;
            while (s_funnel_acc > 1) {
                s_funnel_acc -= 1;
                part_t *p = part_new(PK_FUNNEL);
                if (p) {
                    p->x = frnd(0.25f, 2.9f);   // the angle on the far half of the face
                    p->y = frnd(0.65f, 0.95f);  // the radius (a part of R)
                }
            }
        }
    } else {
        s_spawn_acc = s_bubble_acc = s_ripple_acc = s_funnel_acc = 0;
    }
    for (int i = 0; i < N_PARTS; i++) {
        part_t *p = &s_parts[i];
        switch (p->kind) {
        case PK_SPLASH:
            p->vy += 520.0f * dt;
            p->x += p->vx * dt;
            p->y += p->vy * dt;
            p->a += dt;
            {
                int zb = (int)p->y - NECK_Y;
                zb = zb < 0 ? 0 : (zb > BULB_L ? BULB_L : zb);
                float lim = (float)s_ri[zb] - 1.0f;
                if (p->x > lim) {
                    p->x = lim, p->vx = -fabsf(p->vx) * 0.5f;  // it bounces off the glass
                }
                if (p->x < -lim) {
                    p->x = -lim, p->vx = fabsf(p->vx) * 0.5f;
                }
            }
            if (p->vy > 0 && p->y >= cone_y(p->x)) {
                p->kind = PK_SLIDE;  // it lands and rolls down the cone
                p->y = cone_y(p->x);
                p->vx = (p->x >= s_ge.ax ? 1.0f : -1.0f) * frnd(20, 45);
                p->a = frnd(0.25f, 0.8f);
            }
            if (p->a > 1.2f) {
                p->kind = PK_NONE;
            }
            break;
        case PK_SLIDE: {
            p->x += p->vx * dt;
            p->vx *= 1.0f - 2.0f * dt;
            p->y = cone_y(p->x);
            p->a -= dt;
            int zb = (int)p->y - NECK_Y;
            if (p->a <= 0 || zb < 0 || zb > BULB_L || fabsf(p->x) >= s_ri[zb]) {
                p->kind = PK_NONE;
            }
            break;
        }
        case PK_FUNNEL:
            p->y -= dt * 0.55f;
            if (p->y < 0.12f || !s_ge.flowing) {
                p->kind = PK_NONE;
            }
            break;
        case PK_BUBBLE: {
            p->y -= dt * (45.0f + 22.0f * p->a);
            p->vx += dt * 7.0f;
            float ys = s_ge.ytop + p->x * s_ge.tilt;
            if (p->y - p->a < ys) {
                p->kind = PK_NONE;
            }
            break;
        }
        case PK_RIPPLE:
            p->a += dt / 0.9f;
            if (p->a >= 1) {
                p->kind = PK_NONE;
            }
            break;
        default:
            break;
        }
    }
}

static void HOT draw_stream(void)
{
    if (!s_ge.flowing) {
        return;
    }
    float lx = 0, ly = landing(&lx);
    int y0 = NECK_Y + 3, y1 = (int)ly;
    float ut = u_top();
    if (s_ge.liquid) {
        float q = sqrtf((s_ge.zc + 3.0f) / (BULB_L * 0.7f + 3.0f));
        float w = stream_w() * (0.6f + 0.9f * q);
        int brk = q > 0.45f ? y1 : y0 + (int)((float)(y1 - y0) * (0.25f + q));  // a thin jet breaks into drops
        uint16_t jc = whiten(s_water_tint, 70);
        for (int y = y0; y < y1; y++) {
            float xc = CX + stream_x(y);
            if (y > brk) {
                int ph = (y - brk + (int)(s_st.t * 160.0f)) % 9;
                if (ph > 2) {
                    continue;
                }
            }
            int xa = (int)(xc - w * 0.5f), xb = (int)(xc + w * 0.5f);
            for (int x = xa; x <= xb; x++) {
                px_put(x, y, x == xa ? whiten(jc, 120) : jc);
            }
        }
        return;
    }
    int w = stream_w();
    bool last = ut < 0.004f;  // the last grains: a broken stream
    for (int y = y0; y < y1; y++) {
        if (last && (rnd() & 3)) {
            continue;
        }
        int xc = (int)(CX + stream_x(y) + 0.5f);
        for (int k = 0; k < w; k++) {
            int x = xc - w / 2 + k;
            px_put(x, y, s_pal[20 + s_nz[(y * 7 + k + s_ge.t8 * 5) & 255] * 2]);
        }
    }
    // the sparkles of single grains, closer near the neck (slower grains are closer together)
    int n = (y1 - y0) / 3;
    for (int i = 0; i < n; i++) {
        float r = (float)(rnd() & 1023) / 1023.0f;
        int y = y0 + (int)((float)(y1 - y0) * r * r);
        int x = (int)(CX + stream_x(y) + 0.5f) + (int)(rnd() % 3) - 1;
        px_put(x, y, s_pal[30]);
    }
}

// a bubble of the liquid: a ring of 16 points and a light point
static void HOT draw_bubble(const part_t *p)
{
    int cx = CX + (int)(p->x + sinf(p->vx) * 1.5f), cy = (int)p->y, r = (int)(p->a + 0.5f);
    for (int a = 0; a < 16; a++) {
        int x = cx + (int)lroundf(cosf(a * 0.3927f) * (float)r), y = cy + (int)lroundf(sinf(a * 0.3927f) * (float)r);
        if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H) {
            g_fb[y * SCR_W + x] = whiten(g_fb[y * SCR_W + x], 150);
        }
    }
    px_put(cx - r / 2, cy - r / 2, rgb(255, 255, 255));
}

// a ripple on the surface of the liquid: an ellipse of 40 points, only inside the bottom bulb
static void HOT draw_ripple(const part_t *p)
{
    float rx = 3.0f + p->a * R_MAX * 0.75f, ry = rx * SIN_E;
    int k = (int)(140.0f * (1.0f - p->a));
    for (int a = 0; a < 40; a++) {
        float s = sinf(a * 0.157f), c = cosf(a * 0.157f);
        int x = CX + (int)(p->x + c * rx), y = (int)(p->y + 1.0f + s * ry);
        int zb = y - NECK_Y;
        if (zb < 0 || zb > BULB_L || abs(x - CX) > s_ri[zb]) {
            continue;
        }
        g_fb[y * SCR_W + x] = whiten(g_fb[y * SCR_W + x], k);
    }
}

static void HOT draw_parts(void)
{
    for (int i = 0; i < N_PARTS; i++) {
        part_t *p = &s_parts[i];
        switch (p->kind) {
        case PK_SPLASH:
        case PK_SLIDE:
            px_put(CX + (int)p->x, (int)p->y, s_pal[p->kind == PK_SPLASH ? 28 : 22]);
            break;
        case PK_FUNNEL: {
            if (s_ge.liquid || s_ge.rimh < 1.5f) {
                break;
            }
            float rr = p->y * s_ge.R;
            int x = CX + (int)(rr * cosf(p->x)), y = (int)(s_ge.ycap - rr * SIN_E * sinf(p->x));
            px_put(x, y, s_pal[29]);
            break;
        }
        case PK_BUBBLE:
            draw_bubble(p);
            break;
        case PK_RIPPLE:
            draw_ripple(p);
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------- sounds

// a music-box chime (bell notes that ring out); REPEAT plays it every 3.6 s until a tap or a turn
static const tune_note_t CHIME[] = {
        {0, 900, 79, 0, W_SINE, 34},    {0, 260, 91, 0, W_TRI, 8},     {170, 900, 83, 0, W_SINE, 32},
        {170, 260, 95, 0, W_TRI, 7},    {340, 900, 86, 0, W_SINE, 32}, {340, 260, 98, 0, W_TRI, 7},
        {510, 1300, 91, 0, W_SINE, 30}, {510, 360, 103, 0, W_TRI, 6},  {760, 1500, 86, 0, W_SINE, 20},
        {760, 1500, 79, 0, W_SINE, 18},
};

static void voice(float f0, float f1, int ms, int wave, int vol, int lp, int delay_ms)
{
    audio_voice_t v = {.delay = (uint32_t)(AUDIO_SAMPLE_RATE / 1000 * delay_ms),
                       .dur = (uint32_t)(AUDIO_SAMPLE_RATE / 1000 * ms),
                       .f0 = f0,
                       .f1 = f1,
                       .wave = (uint8_t)wave,
                       .vol = (uint8_t)vol,
                       .attack_ms = 6,
                       .shape = AUDIO_PERC,
                       .lp = (uint8_t)lp};
    audio_play(&v);
}

static void snd_turn(void)
{
    const snd_mat_t *m = &SND[s_st.mat];
    if (s_st.mat == M_WATER) {
        voice(600, 200, 300, W_NOISE, 15, 30, 0);  // the water sloshes
        voice(300, 700, 50, W_SINE, 10, 0, 120);   // and gurgles
    } else {
        voice(m->bed_hz * 0.40f, m->bed_hz * 0.13f, 320, W_NOISE, 16, s_st.mat == 2 ? 35 : 55,
              0);  // the material slides
        if (m->tick_wave == W_SINE) {
            for (int i = 0; i < 3; i++) {
                voice(6000 + 900 * i, 5800 + 900 * i, 30, W_SINE, 6, 0, 60 + 70 * i);  // gold tings
            }
        }
    }
    voice(2600, 2600, 60, W_SINE, 14, 0, 30);  // the glass
}

static void snd_land(bool liquid)
{
    const snd_mat_t *m = &SND[s_st.mat];
    if (liquid) {
        voice(500, 160, 180, W_NOISE, 14, 40, 0);
    } else {
        voice(m->bed_hz * 0.17f, m->bed_hz * 0.05f, 110, W_NOISE, 18, s_st.mat == 2 ? 22 : 30, 0);
    }
}

static void chime(void)
{
    audio_tune(CHIME, (int)(sizeof(CHIME) / sizeof(CHIME[0])));
}

// ---------------------------------------------------------------- turning over

#define FLIP_LEN 0.70f
static float s_done_t;  // the DONE pulse after the end

static void alarm_stop(void)
{
    if (s_st.alarm_on) {
        audio_tune_stop();
    }
    s_st.alarm_on = false;
}

// the ends changed places: the material of each bulb falls to its new rest
static void turned(void)
{
    alarm_stop();
    memset(s_parts, 0, sizeof s_parts);
    s_st.crater = 0;
    s_st.pile_x = 0;
    s_st.settle_t = 0;
    geometry();
    float ut = u_top(), ot, ob;
    if (s_ge.liquid) {
        ot = s_ge.ytop - TOP_END;
        ob = s_ge.ybot - NECK_Y - 2;
    } else {
        ot = s_ge.ycap - s_ge.rimh - TOP_END;
        ob = s_ge.yapex - NECK_Y - 2;
    }
    s_st.off_top0 = ut > 0.001f && ot > 0 ? ot : 0;
    s_st.off_bot0 = ut < 0.999f && ob > 0 ? ob : 0;
    float mx = s_st.off_top0 > s_st.off_bot0 ? s_st.off_top0 : s_st.off_bot0;
    s_st.settle_len = sqrtf(2.0f * mx / 6000.0f) + 0.04f;
    s_st.settle_t = 1e-4f;
    snd_turn();
}

static void squash_start(bool reset)
{
    if (s_st.flip_t > 0) {
        return;
    }
    alarm_stop();
    s_st.flip_t = 1e-4f;
    s_st.flip_reset = reset;
    voice(700, 250, 420, W_NOISE, 12, 40, 0);
}

// the middle of the squash turn: the hourglass is on its edge
static void squash_mid(void)
{
    if (s_st.flip_reset) {
        // START: all the material goes to the top (it fills the top end, then falls to the neck)
        s_st.ua = s_st.top_a ? 1.0f : 0.0f;
        turned();
        s_st.off_bot0 = 0;
    } else {
        s_st.top_a = !s_st.top_a;
        turned();
    }
}

// ---------------------------------------------------------------- the board position

static void sense(float dt)
{
    if (BOARD_FIXED_ORIENT || !imu_ok()) {
        s_st.alpha = 0;
        s_st.flow_f = 1;
        return;
    }
    float px, py, pz;
    imu_gravity_phys(&px, &py, &pz);
    float k = dt * 12.0f > 1 ? 1 : dt * 12.0f;
    float up = HG_LAND ? -px : py;  // the gravity along the axis of the hourglass (+ = upright)
    s_st.g_py += (up - s_st.g_py) * k;
    orient_t o = gfx_orient(), want = o;
    if (s_st.g_py > 0.45f) {
        want = HG_BASE;
    } else if (s_st.g_py < -0.45f) {
        want = HG_FLIP;
    }
    if (want != o && s_st.flip_t <= 0) {
        gfx_set_orient(want);
        s_st.top_a = want == HG_BASE;
        turned();
    }
    // the tilt: flat on a table the hourglass stands (the table mode); upright, the real gravity
    float gx, gy;
    imu_gravity_logical(&gx, &gy);
    // (the tilt counts only when the board stands: a board that lies back gives a false direction in the screen)
    float m = sqrtf(gx * gx + gy * gy), w = (m - 0.55f) / 0.30f, a = 0;
    w = w < 0 ? 0 : (w > 1 ? 1 : w);
    w = w * w * (3 - 2 * w);
    if (w > 0) {
        a = atan2f(gx, gy) * w;
    }
    const float dz = 0.087f;  // 5 degrees: a board that stands a little crooked
    a = a > dz ? a - dz : (a < -dz ? a + dz : 0);
    float ka = dt * 8.0f > 1 ? 1 : dt * 8.0f;
    s_st.alpha += (a - s_st.alpha) * ka;
    float deg = fabsf(s_st.alpha) * 57.2958f;
    if (deg <= 25) {
        s_st.flow_f = 1;
    } else if (deg >= 70) {
        s_st.flow_f = 0;
    } else {
        float t = (deg - 25) / 45.0f;
        s_st.flow_f = 1.0f - t * t * (3 - 2 * t);
    }
}

// ---------------------------------------------------------------- the panel

// the panel: 5 rows (TIME, GLASS, FILL, ALARM, SOUND); in each row the arrows at the sides, the caption and the value
// between them; then START and CLOSE. The landscape frame: two columns (TIME, GLASS, FILL | ALARM, SOUND, the buttons)
#if HG_LAND
#define PNL_X 6
#define PNL_W 308
#define PNL_Y 8
#define ROW_H 50
#elif BOARD_BAR_SCREEN
#define PNL_X 8
#define PNL_W 156
#define PNL_Y 118
#define ROW_H 58
#elif BOARD_UI_TABLET
#define PNL_X 20
#define PNL_W 320
#define PNL_Y 120
#define ROW_H 68
#define ARW   56
#define BTN_H 62
#elif BOARD_ROUND
// (the thumb, round 3: 0.089 mm a px here, so the arrows are 52 px and the buttons 56 px = 4.6 / 5.0 mm; at 466:
// 59 and 63 px = 5.6 / 6.0 mm)
#define PNL_X RI(101)
#define PNL_W RI(210)
#define PNL_Y RI(28)
#define ROW_H RI(56)
#define ARW   RI(52)
#define BTN_H RI(56)
#else
#define PNL_X 6
#define PNL_W 228
#define PNL_Y 6
#define ROW_H 48
#endif
#define VAL_F    (&MF_N16)
#define N_ROWS   5
#define ROW_Y(r) (PNL_Y + 8 + (r) * ROW_H)
#if HG_LAND
#define BTN_Y (PNL_Y + 8 + 2 * ROW_H + 4)
#else
#define BTN_Y (ROW_Y(N_ROWS) + 4)
#endif
#ifndef BTN_H
#define BTN_H 44
#endif
#define PNL_H (BTN_Y + BTN_H + 8 - PNL_Y)
#ifndef ARW
#define ARW 40  // the arrow buttons
#endif
#define CAP_DY (12 + (ARW - 40) / 3)  // the caption and the value of a row: down from the row top
#define VAL_DY (37 + (ARW - 40) * 7 / 12)

static void save(void)
{
    store_set("hg_des", (uint32_t)s_st.des);
    store_set("hg_mat", (uint32_t)s_st.mat);
    store_set("hg_dur", (uint32_t)s_st.dur);
    store_set("hg_al", (uint32_t)s_st.alarm);
    store_set("hg_clk", s_st.clock ? 1u : 0u);
    store_set("hg_snd", (uint32_t)s_st.sound);
}

static void dur_text(char *b, int n, int i)
{
    int s = DURS[i];
    if (s < 60) {
        snprintf(b, n, "%d S", s);
    } else {
        snprintf(b, n, "%d MIN", s / 60);
    }
}

static void change(int row, int d)
{
    sfx_play(SFX_CLICK);
    switch (row) {
    case 0:
        s_st.dur += d;
        s_st.dur = s_st.dur < 0 ? 0 : (s_st.dur >= N_DURS ? N_DURS - 1 : s_st.dur);
        break;
    case 1:
        s_st.des = (s_st.des + d + N_DESIGNS) % N_DESIGNS;
        tables_build();
        caches_build();
        break;
    case 2:
        s_st.mat = (s_st.mat + d + N_MATS) % N_MATS;
        palette_build();
        tables_build();
        s_st.crater = u_top() > 0 && u_top() < 1 ? 1 : 0;
        break;
    case 3:
        s_st.alarm = (s_st.alarm + d + 3) % 3;
        if (s_st.alarm != AL_OFF) {
            chime();  // a preview
        }
        break;
    case 4:
        s_st.sound = (s_st.sound + d + 3) % 3;
        if (s_st.sound == SN_OFF) {
            audio_drone(0, SND[s_st.mat].bed_hz, 0, W_NOISE);
            s_st.bed_sent_vol = 0;
        }
        break;
    default:
        break;
    }
    save();
}

static void tri(int cx, int cy, int s, int dir, uint16_t c)  // a triangle that points left (-1) or right (+1)
{
    for (int i = -s; i <= s; i++) {
        int w = (s - (i < 0 ? -i : i)) * 3 / 2;
        if (dir > 0) {
            gfx_fill_rect(cx - s * 3 / 4, cy + i, w + 1, 1, c);  // the flat side at the left: it points right
        } else {
            gfx_fill_rect(cx + s * 3 / 4 - w, cy + i, w + 1, 1, c);
        }
    }
}

static void btn(int x, int y, int w, int h, bool focus)
{
    gfx_fill_rect(x, y, w, h, rgb(36, 40, 54));
    gfx_frame_rect(x, y, w, h, focus ? 2 : 1, focus ? s_k_gold : s_k_line);
}

static inline bool in_box(int x, int y, int bx, int by, int w, int h)
{
    return x >= bx && x < bx + w && y >= by && y < by + h;
}

// the box of a panel row (its left end, top and width) and of a button (0 START, 1 CLOSE)
static void row_box(int r, int *x, int *y, int *w)
{
#if HG_LAND
    *w = PNL_W / 2;
    *x = PNL_X + (r >= 3 ? *w : 0);
    *y = PNL_Y + 8 + (r % 3) * ROW_H;
#else
    *x = PNL_X;
    *w = PNL_W;
    *y = ROW_Y(r);
#endif
}

static void btn_box(int i, int *x, int *y, int *w)
{
#if HG_LAND
    int cw = PNL_W / 2;
    *w = (cw - 18) / 2;
    *x = PNL_X + cw + 6 + i * (*w + 6);
#else
    *w = (PNL_W - 18) / 2;
    *x = PNL_X + 6 + i * (*w + 6);
#endif
    *y = BTN_Y;
}

static void draw_panel(void)
{
    gfx_darken_rect(PNL_X, PNL_Y, PNL_W, PNL_H, 2);
    gfx_frame_rect(PNL_X, PNL_Y, PNL_W, PNL_H, 1, s_k_line);
    static const char *const CAP[] = {"TIME", "GLASS", "FILL", "ALARM", "SOUND"};
    char b[16];
    for (int r = 0; r < N_ROWS; r++) {
        int x0, y, rw;
        row_box(r, &x0, &y, &rw);
        bool f = BOARD_WHEEL && s_st.focus == r;
        btn(x0 + 6, y, ARW, ARW, false);
        btn(x0 + rw - 6 - ARW, y, ARW, ARW, false);
        tri(x0 + 6 + ARW / 2, y + ARW / 2, 8, -1, s_k_text);
        tri(x0 + rw - 6 - ARW / 2, y + ARW / 2, 8, 1, s_k_text);
        mf_text_c(&MF_TINY, x0 + rw / 2, y + CAP_DY, CAP[r], f ? s_k_gold : s_k_dim, 255, 1);
        const char *v = b;
        if (r == 0) {
            dur_text(b, sizeof b, s_st.dur);
        } else if (r == 1) {
            v = DESIGNS[s_st.des].name;
        } else if (r == 2) {
            v = MATS[s_st.mat].name;
        } else if (r == 3) {
            v = ALARMS[s_st.alarm];
        } else {
            v = SOUNDS[s_st.sound];
        }
        const mfont_t *vf = mf_width(VAL_F, v, 1) > rw - 2 * (ARW + 10) ? &MF_LABEL : VAL_F;  // (a long value)
        mf_text_oc(vf, x0 + rw / 2, y + VAL_DY, v, f ? s_k_gold : s_k_text, s_k_ink, 0, 1);
    }
    for (int i = 0; i < 2; i++) {
        int bx, by, bw;
        btn_box(i, &bx, &by, &bw);
        btn(bx, by, bw, BTN_H, BOARD_WHEEL && s_st.focus == N_ROWS + i);
        mf_text_oc(&MF_N16, bx + bw / 2, by + BTN_H / 2 + 8, i ? "CLOSE" : "START", i ? s_k_text : s_k_gold, s_k_ink, 0,
                   1);
    }
}

static void start(void)
{
    s_st.panel = false;
    squash_start(true);
}

// ---------------------------------------------------------------- the time and the hints

// a hint: '|' is a new line in the narrow column of the CoreS3 and on the Watcher; on the 3.49 one line (the cap is
// close under it)
static void draw_lines(const mfont_t *f, int cx, int y, const char *s, uint16_t c)
{
    char line[32];
    while (*s) {
        int n = 0;
        while (s[n] && (BOARD_BAR_SCREEN || s[n] != '|') && n < 31) {
            n++;
        }
        if (BOARD_BAR_SCREEN) {
            for (int i = 0; i < n; i++) {
                line[i] = s[i] == '|' ? ' ' : s[i];
            }
            line[n] = 0;
            mf_text_oc(f, cx, y, line, c, s_k_ink, 1, 1);
            return;
        }
        memcpy(line, s, (size_t)n);
        line[n] = 0;
        mf_text_oc(f, cx, y, line, c, s_k_ink, 1, 1);
        y += f->line;
        s += n + (s[n] == '|');
    }
}

static void draw_hud(void)
{
    char b[24];
    float ut = u_top();
#if BOARD_UI_TABLET
    const mfont_t *tf = &MF_N34;
#elif BOARD_BAR_SCREEN || BOARD_ROUND || HG_LAND
    const mfont_t *tf = &MF_N28;
#else
    const mfont_t *tf = &MF_N20;
#endif
    if (s_st.toast_t > 0) {
        dur_text(b, sizeof b, s_st.dur);
        mf_text_oc(tf, TIME_X, TIME_Y, b, s_k_gold, s_k_ink, 2, 1);
    } else if (s_st.clock || s_st.alarm_on || s_done_t > 0) {
        int sec = (int)ceilf(time_left() - 0.001f);
        snprintf(b, sizeof b, "%d:%02d", sec / 60, sec % 60);
        uint16_t c = ut <= 0 ? s_k_dim : s_k_text;
        if (s_st.alarm_on || s_done_t > 0) {
            c = ((int)(s_st.t * 2.5f) & 1) ? s_k_gold : s_k_text;
        }
        mf_text_oc(tf, TIME_X, TIME_Y, b, c, s_k_ink, 2, 1);
    }
    const char *h = NULL;
    if (s_st.alarm_on) {
        h = s_st.alarm == AL_REPEAT ? "TAP TO|STOP" : NULL;
    } else if (ut <= 0 && s_st.settle_t <= 0 && s_st.flip_t <= 0) {
        if (BOARD_WHEEL) {
            h = "PUSH TO|TURN OVER";
        } else if (imu_ok() && !BOARD_FIXED_ORIENT) {
            h = "TURN ME|OVER";
        } else {
            h = "TAP, THEN|START";
        }
    } else if (ut > 0 && s_st.flow_f < 0.05f && s_st.settle_t <= 0) {
        h = "ON ITS SIDE:|STOPPED";
    }
#if BOARD_UI_TABLET
    if (h) {
        draw_lines(&MF_LABEL, TIME_X, HINT_Y, h, s_k_dim);
    }
#else
    if (h) {
        draw_lines(&MF_TINY, TIME_X, HINT_Y, h, s_k_dim);
    }
#endif
}

// ---------------------------------------------------------------- update, input, render

static void hg_update(int64_t now)
{
    float dt = s_st.last_ms ? (float)(now - s_st.last_ms) / 1000.0f : 0.033f;  // (now is in ms)
    s_st.last_ms = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    s_st.t += dt;
    sense(dt);
#if BOARD_WHEEL
    int st = input_wheel(), pr = input_wheel_presses();
    if (s_st.panel) {
        if (st) {
            if (s_st.focus < N_ROWS) {
                change(s_st.focus, st > 0 ? 1 : -1);
            } else {
                s_st.focus = s_st.focus == N_ROWS ? N_ROWS + 1 : N_ROWS;
            }
        }
        if (pr) {
            if (s_st.focus < N_ROWS) {
                s_st.focus++;
            } else if (s_st.focus == N_ROWS) {
                start();
            } else {
                s_st.panel = false;
            }
        }
    } else {
        if (st) {
            s_st.dur += st > 0 ? 1 : -1;
            s_st.dur = s_st.dur < 0 ? 0 : (s_st.dur >= N_DURS ? N_DURS - 1 : s_st.dur);
            s_st.toast_t = 1.4f;
            sfx_play(SFX_CLICK);
            save();
        }
        if (pr) {
            if (s_st.alarm_on) {
                alarm_stop();
            } else {
                squash_start(false);
            }
        }
    }
#endif
    if (s_st.toast_t > 0) {
        s_st.toast_t -= dt;
    }
    if (s_done_t > 0) {
        s_done_t -= dt;
    }
    // the squash turn and the fall to the rest
    if (s_st.flip_t > 0) {
        float before = s_st.flip_t;
        s_st.flip_t += dt;
        if (before < FLIP_LEN * 0.5f && s_st.flip_t >= FLIP_LEN * 0.5f) {
            squash_mid();
        }
        if (s_st.flip_t >= FLIP_LEN) {
            s_st.flip_t = 0;
        }
    }
    if (s_st.settle_t > 0) {
        s_st.settle_t += dt;
        if (s_st.settle_t >= s_st.settle_len) {
            s_st.settle_t = 0;
            if (s_st.off_top0 > 8 || s_st.off_bot0 > 8) {
                snd_land(MATS[s_st.mat].repose <= 0);
            }
        }
    }
    // the flow
    float ut = u_top();
    if (ut > 0 && s_st.flip_t <= 0 && s_st.settle_t <= 0 && s_st.flow_f > 0) {
        float g = g_of(ut) - dt * s_st.flow_f / (float)DURS[s_st.dur];
        float nu = g <= 0 ? 0 : g_inv(g);
        set_u_top(nu);
        s_st.crater += dt / 3.0f;
        if (s_st.crater > 1) {
            s_st.crater = 1;
        }
        if (nu <= 0) {
            s_done_t = 8.0f;
            if (s_st.alarm != AL_OFF) {
                s_st.alarm_on = true;
                s_st.alarm_next = 0;
                s_st.alarm_total = 0;
            }
            app_screen_wake();
            ESP_LOGI(TAG, "done (%d s)", DURS[s_st.dur]);
        }
    }
    if (s_st.alarm_on) {
        s_st.alarm_total += dt;
        s_st.alarm_next -= dt;
        if (s_st.alarm_next <= 0) {
            chime();
            s_st.alarm_next = 3.6f;
            if (s_st.alarm == AL_ONCE) {
                s_st.alarm_on = false;
            }
        }
        if (s_st.alarm_total > 180.0f) {
            alarm_stop();
        }
    }
    geometry();
    if (s_ge.flowing && !s_ge.liquid) {
        float lx;
        landing(&lx);
        float k = dt * 0.8f > 1 ? 1 : dt * 0.8f;
        s_st.pile_x += (lx - s_st.pile_x) * k;
    }
    parts_update(dt);
}

static void hg_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (s_st.alarm_on) {
        alarm_stop();
        return;
    }
    if (!s_st.panel) {
        if (abs(x - TIME_X) < 60 && abs(y - (TIME_Y - 12)) < TIME_HIT_H / 2) {
            s_st.clock = !s_st.clock;
            sfx_play(SFX_CLICK);
            save();
        } else {
            s_st.panel = true;
            s_st.focus = 0;
            sfx_play(SFX_CLICK);
        }
        return;
    }
    // the arrows: the hit zones fill the row height (the zones of two rows touch)
    for (int r = 0; r < N_ROWS; r++) {
        int rx, ry, rw;
        row_box(r, &rx, &ry, &rw);
        int hy = ry - (ROW_H - ARW) / 2;
        if (in_box(x, y, rx, hy, ARW + 14, ROW_H)) {
            change(r, -1);
            return;
        }
        if (in_box(x, y, rx + rw - 8 - ARW, hy, ARW + 14, ROW_H)) {
            change(r, 1);
            return;
        }
    }
    int b0x, b1x, by, bw;
    btn_box(0, &b0x, &by, &bw);
    btn_box(1, &b1x, &by, &bw);
    if (in_box(x, y, b0x - 4, by - 4, bw + 7, BTN_H + 8)) {
        start();
        return;
    }
    if (in_box(x, y, b1x - 3, by - 4, bw + 7, BTN_H + 8) || !in_box(x, y, PNL_X, PNL_Y, PNL_W, PNL_H)) {
        sfx_play(SFX_CLICK);
        s_st.panel = false;
    }
}

static int s_fps_n;
static int64_t s_fps_t0, s_draw_us;

static void hg_render(int64_t now)
{
    int64_t t0 = esp_timer_get_time();
    geometry();
    bool sq = s_st.flip_t > 0;
    float s = sq ? fabsf(cosf(3.14159f * s_st.flip_t / FLIP_LEN)) : 1.0f;
#if BOARD_ROUND
    // the round panels: only the part of each row inside the circle (the corners do not show)
    static int16_t ch0[480], ch1[480];
    static int ch_w;
    if (ch_w != SCR_W) {
        ch_w = SCR_W;
        float r = SCR_W * 0.5f;
        for (int y = 0; y < SCR_H && y < 480; y++) {
            float dy = (float)y + 0.5f - r, q = r * r - dy * dy;
            int w = q > 0 ? (int)ceilf(sqrtf(q)) + 1 : 0;
            ch0[y] = (int16_t)(r - w < 0 ? 0 : r - w);
            ch1[y] = (int16_t)(r + w > SCR_W ? SCR_W : r + w);
        }
    }
#endif
    for (int y = 0; y < SCR_H; y++) {
        uint16_t *dst = g_fb + y * SCR_W;
#if BOARD_ROUND
        int a = ch0[y], e = ch1[y];
#else
        int a = 0, e = SCR_W;
#endif
        if (!sq) {
            memcpy(dst + a, s_full + y * SCR_W + a, (size_t)(e - a) * 2);
            if (y >= TOP_END && y <= BASE_Y) {
                mat_row(dst, y);
            }
            continue;
        }
        memcpy(dst + a, s_room + y * SCR_W + a, (size_t)(e - a) * 2);
        if (s < 0.015f) {
            continue;
        }
        int sy = NECK_Y + (int)floorf((float)(y - NECK_Y) / s + 0.5f);
        if (sy < CAP_T0 - 8 || sy > CAP_B1) {
            continue;
        }
        const uint16_t *f = s_full + sy * SCR_W, *r = s_room + sy * SCR_W;
        int x0 = CX - CAP_W - 2, x1 = CX + CAP_W + 2;
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > SCR_W - 1) {
            x1 = SCR_W - 1;
        }
        for (int x = x0; x <= x1; x++) {
            if (f[x] != r[x]) {
                dst[x] = f[x];
            }
        }
        mat_row(dst, sy);
    }
    if (!sq) {
        draw_stream();
        draw_parts();
    }
    s_draw_us += esp_timer_get_time() - t0;
    if (s_st.panel) {
        draw_panel();
    } else {
        draw_hud();
    }
    if (++s_fps_n >= 150) {
        if (s_fps_t0 && now > s_fps_t0) {
            ESP_LOGI(TAG, "fps %.1f, the drawing %d us a frame", 150.0 * 1000.0 / (double)(now - s_fps_t0),
                     (int)(s_draw_us / 150));
        }
        s_fps_t0 = now;
        s_fps_n = 0;
        s_draw_us = 0;
    }
}

// ---------------------------------------------------------------- enter, leave

static void hg_enter(int64_t now)
{
    size_t n = (size_t)SCR_W * SCR_H * 2;
    if (!s_room) {
        s_room = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    }
    if (!s_full) {
        s_full = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    }
    if (!s_room) {
        s_room = malloc(n);
    }
    if (!s_full) {
        s_full = malloc(n);
    }
    s_st.des = (int)(store_get("hg_des", 0) % N_DESIGNS);
    s_st.mat = (int)(store_get("hg_mat", 0) % N_MATS);
    s_st.dur = (int)store_get("hg_dur", 3);
    if (s_st.dur < 0 || s_st.dur >= N_DURS) {
        s_st.dur = 3;
    }
    s_st.alarm = (int)(store_get("hg_al", AL_ONCE) % 3);
    s_st.clock = store_get("hg_clk", 1) != 0;
    s_st.sound = (int)(store_get("hg_snd", SN_OFF) % 3);
    s_st.bed_sent_vol = 0;
    luts_build();
    palette_build();
    tables_build();
    s_cache_for = -1;
    caches_build();
    bool imu = imu_ok() && !BOARD_FIXED_ORIENT;
    if (imu) {
        float px, py, pz;
        imu_gravity_phys(&px, &py, &pz);
        float up = HG_LAND ? -px : py;
        s_st.g_py = up;
        gfx_set_orient(up < -0.3f ? HG_FLIP : HG_BASE);
    }
    if (!s_st.entered_once) {
        // the first time: all the material is in the bottom (turn the hourglass over to start it)
        s_st.entered_once = true;
        s_st.top_a = !imu || gfx_orient() == HG_BASE;
        s_st.ua = s_st.top_a ? 0.0f : 1.0f;
    } else {
        if (imu) {
            s_st.top_a = gfx_orient() == HG_BASE;
        }
        // it ran while the app was closed (upright)
        float ut = u_top();
        if (ut > 0 && s_st.left_at_ms > 0 && now > s_st.left_at_ms) {
            float g = g_of(ut) - (float)(now - s_st.left_at_ms) / 1000.0f / (float)DURS[s_st.dur];
            set_u_top(g <= 0 ? 0 : g_inv(g));
        }
    }
    s_st.crater = u_top() > 0 && u_top() < 0.98f ? 1 : 0;
    s_st.last_ms = 0;
    s_st.panel = false;
    s_st.flip_t = s_st.settle_t = 0;
    s_st.alarm_on = false;
    s_st.alpha = 0;
    s_st.flow_f = 1;
    s_st.pile_x = 0;
    s_done_t = 0;
    memset(s_parts, 0, sizeof s_parts);
    s_rng ^= esp_random() | 1;
    geometry();
    ESP_LOGI(TAG, "%s, %s, %d s, the top %.0f%%", DESIGNS[s_st.des].name, MATS[s_st.mat].name, DURS[s_st.dur],
             (double)(u_top() * 100));
}

static void hg_leave(void)
{
    alarm_stop();
    audio_drone(0, SND[s_st.mat].bed_hz, 0, W_NOISE);
    s_st.bed_sent_vol = 0;
    s_st.left_at_ms = esp_timer_get_time() / 1000;
    free(s_room);
    free(s_full);
    s_room = s_full = NULL;
    s_cache_for = -1;
}

static bool hg_background(void)
{
    return u_top() > 0 || s_st.alarm_on;
}
static bool hg_awake(void)
{
    return s_st.alarm_on || (u_top() > 0 && time_left() < 30.0f);
}

#include "tficon/tficon.h"
// the points of the hourglass, turned (and made smaller) by (ca, sa) round its middle
static void hg_icon_rot(float *p, int n, float cx, float cy, float ca, float sa)
{
    for (int k = 0; k < n; k++) {
        float dx = p[2 * k] - cx, dy = p[2 * k + 1] - cy;
        p[2 * k] = cx + dx * ca - dy * sa;
        p[2 * k + 1] = cy + dx * sa + dy * ca;
    }
}
// the menu icon: the sand runs from the top to the bottom, then the hourglass turns over
static void hg_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x4dd0ff), tfi_hex(0x1546b8));
    float u = s / 100.0f, ph = tfi_ph(2);
    float run = tfi_step(0.0f, 0.78f, ph), turn = tfi_step(0.8f, 1.0f, ph);
    // (it turns round its middle and is a little smaller while it turns: its corners stay in the tile)
    float ang = turn * 3.14159f, sc = 1 - 0.2f * fsin(turn * 3.14159f), ca = fcos(ang) * sc, sa = fsin(ang) * sc;
    float cx = x + 50 * u, cy = y + 50 * u;
    // the glass
    for (int k = -1; k <= 1; k += 2) {
        float gl[8] = {cx - 20 * u, cy + k * 30 * u, cx + 20 * u, cy + k * 30 * u, cx + 3.5f * u,
                       cy,          cx - 3.5f * u,   cy};
        hg_icon_rot(gl, 4, cx, cy, ca, sa);
        tfi_convex(gl, 4, tfi_hex(0xdff4ff), 150);
    }
    // the sand: the top part goes down, the bottom part grows (in the frame of the glass)
    float top = 1 - run, bot = run;
    if (top > 0.02f) {
        float h = 26 * u * top, w = 18 * u * top;
        float sd[6] = {cx - w, cy - 2 * u - h, cx + w, cy - 2 * u - h, cx, cy - 2 * u};
        hg_icon_rot(sd, 3, cx, cy, ca, sa);
        tfi_convex(sd, 3, tfi_hex(0xffc94a), 256);
    }
    if (bot > 0.02f) {
        float h = 22 * u * bot;
        float sd[8] = {cx - 18.5f * u,
                       cy + 29 * u,
                       cx + 18.5f * u,
                       cy + 29 * u,
                       cx + 18.5f * u - h * 0.55f,
                       cy + 29 * u - h,
                       cx - 18.5f * u + h * 0.55f,
                       cy + 29 * u - h};
        hg_icon_rot(sd, 4, cx, cy, ca, sa);
        tfi_convex(sd, 4, tfi_hex(0xf5a623), 256);
    }
    if (run > 0.01f && run < 0.99f && turn == 0) {
        tfi_line(cx, cy - 1 * u, cx, cy + 28 * u - 22 * u * bot, 1.8f * u, false, tfi_hex(0xffc94a), 256);
    }
    // the frame: a bar at the top and at the bottom, two posts
    float bars[2][8];
    for (int k = 0; k < 2; k++) {
        float yy = k ? cy + 30 * u : cy - 36 * u;
        float q[8] = {cx - 26 * u, yy, cx + 26 * u, yy, cx + 26 * u, yy + 6 * u, cx - 26 * u, yy + 6 * u};
        memcpy(bars[k], q, sizeof q);
        hg_icon_rot(bars[k], 4, cx, cy, ca, sa);
        tfi_convex(bars[k], 4, tfi_hex(0x8d4a1c), 256);
    }
    for (int k = -1; k <= 1; k += 2) {
        float p[4] = {cx + k * 23 * u, cy - 31 * u, cx + k * 23 * u, cy + 31 * u};
        hg_icon_rot(p, 2, cx, cy, ca, sa);
        tfi_line(p[0], p[1], p[2], p[3], 3 * u, false, tfi_hex(0xb0662a), 256);
    }
    tfi_end();
}

const app_t app_hourglass = {
        .name = "HOURGLASS",
        .landscape = HG_LAND,  // the CoreS3 stands in landscape; the others in portrait
        .enter = hg_enter,
        .leave = hg_leave,
        .input = hg_input,
        .update = hg_update,
        .render = hg_render,
        .draw_icon = hg_icon,
        .awake = hg_awake,
        .background = hg_background,
        .round = APP_ROUND_HI,  // the round frame of the panel (466 on the AMOLED-1.75C, 412 on the Watcher)
        .wheel = true,          // the Watcher: a turn sets the time, a push turns the hourglass over
        .run = true,            // the settings panel: the timer runs on under it
};
