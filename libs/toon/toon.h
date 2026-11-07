#pragma once
// TOON: the shared engine of FIGHT (and later QUEST).
// - A small vector rasterizer that draws into a low-resolution buffer (no anti-aliasing).
// - A camera with parallax layers, zoom, roll and shake.
// - Cartoon puppets: parts on a skeleton, blended poses, squash and stretch, hair and ribbons.
// - The rooftop stage (toon_stage.c).
// - The VHS tape filter: the low-resolution buffer to the frame buffer.

#include <stdbool.h>
#include <stdint.h>
#include "tfn/board.h"  // (BOARD_UI_TABLET)

// ---------------------------------------------------------------- colors
// Native RGB565 (not byte-swapped). The VHS filter swaps the bytes for the LCD.
#define TC(h) ((uint16_t)(((((h) >> 16) & 0xF8) << 8) | ((((h) >> 8) & 0xFC) << 3) | (((h) & 0xFF) >> 3)))
typedef uint16_t tcol_t;
typedef struct {
    tcol_t c[4];
} tramp_t;  // dark, shadow, mid, light

// ---------------------------------------------------------------- raster
typedef struct {
    float a, b, c, d, e, f;
} tmat_t;  // x' = a x + c y + e, y' = b x + d y + f

typedef struct {
    tcol_t *px;
    int w, h;
    tmat_t m;
    uint8_t alpha;  // 255 = solid; less = blend (for later: the quest light)
    bool clip;      // true = draw only where the mask has the current mark
} tcanvas_t;
extern tcanvas_t T;

#define T_MAX_W 640  // the largest canvas (QUEST draws at the full screen resolution)
#define T_MAX_H 240
#define T_LO_W  320  // the low-resolution buffer of FIGHT and the VHS filter
#if BOARD_UI_TABLET
#define T_LO_H 180  // (the Tab5: FIGHT fills its 640 x 360 frame, 2 times this buffer)
#else
#define T_LO_H 120
#endif

void t_begin(tcol_t *buf, int w, int h);
void t_identity(void);
void t_translate(float x, float y);
void t_scale(float sx, float sy);
void t_rotate(float a);
void t_push(void);
void t_pop(void);
float t_unit(void);                                 // screen pixels for one unit now
void t_xf(float x, float y, float *ox, float *oy);  // transform a point

void t_clear(tcol_t c);
void t_poly(const float *xy, int n, tcol_t c);  // any simple polygon (even-odd)
void t_rect(float x, float y, float w, float h, tcol_t c);
void t_ellipse(float cx, float cy, float rx, float ry, tcol_t c);
void t_capsule(float ax, float ay, float bx, float by, float r1, float r2, tcol_t c);
void t_line(float ax, float ay, float bx, float by, float w, tcol_t c);
void t_arc(float cx, float cy, float r, float a0, float a1, float w, tcol_t c);
void t_tri(float ax, float ay, float bx, float by, float cx, float cy, tcol_t c);
// Screen-space helpers (no transform), in buffer pixels.
void t_srect(int x, int y, int w, int h, tcol_t c);
// The clip mask: t_mask_poly() marks a polygon; t_clip(true) draws only inside it.
void t_mask_begin(void);
void t_mask_poly(const float *xy, int n);
void t_mask_ellipse(float cx, float cy, float rx, float ry);
static inline void t_clip(bool on)
{
    T.clip = on;
}

// ---------------------------------------------------------------- text (5 x 7 pixel font)
typedef tcol_t (*trow_fn)(int row, void *ctx);
int t_text_w(const char *s, int sc);
// Screen space, in buffer pixels. outline: 1 dark pixel around; shadow: a color or 0.
void t_text(const char *s, int x, int y, int sc, trow_fn col, void *ctx, bool outline, tcol_t shadow);
// With the current transform (for rotated words). x, y = top-left in units.
void t_text_xf(const char *s, float x, float y, float sc, trow_fn col, void *ctx, tcol_t shadow);

// ---------------------------------------------------------------- camera and parallax
typedef struct {
    float x, y, z, roll, sx, sy;
} tcam_t;
typedef struct {
    int W, H;      // view size in units at zoom 1 (= buffer pixels when ps = 1)
    float ps;      // buffer pixels per unit
    int iw, ih;    // buffer size
    float base_y;  // the camera y of the wide shot
} tview_t;
void t_view_init(tview_t *v, int W, int H, float ps);
void t_layer(const tview_t *v, const tcam_t *cam, float p);  // p = parallax (0 far .. 1 the fight)
void t_proj(const tview_t *v, const tcam_t *cam, float p, float x, float y, float *sx, float *sy);
void t_cam_clamp(const tview_t *v, tcam_t *cam, float half_w);

// ---------------------------------------------------------------- puppets
enum {
    EX_NONE,
    EX_GRIT,
    EX_HURT,
    EX_FURY,
    EX_SHOUT,
    EX_KO,
    EX_SMILE
};
typedef struct {
    float to, hd, fua, ffa, bua, bfa, fth, fsh, bth, bsh, ffo, bfo, air;  // degrees (air 0..1)
    float fal, bal, fll;                                                  // limb stretch (1 = normal)
    uint8_t ex;
} tpose_t;
void t_pose_blend(const tpose_t *a, const tpose_t *b, float u, tpose_t *out);

typedef struct {
    const char *name;
    tramp_t skin, hair, top, pants, acc;
    char emblem;         // the symbol on the shirt: p pi, t tau, s sigma, d delta, r root, h theta, o omega, i infinity
    uint8_t hair_style;  // T_HS_*
    uint8_t extra;       // T_X_* bits
    float scale;         // the body size (0 or 1 = normal)
} tlook_t;

enum {
    T_HS_PONY,
    T_HS_SPIKY,
    T_HS_BALD,
    T_HS_AFRO,
    T_HS_MOHAWK,
    T_HS_LONG
};
#define T_X_BAND    1  // a headband with two tails
#define T_X_GLASSES 2
#define T_X_BEARD   4
#define T_X_CAP     8
#define T_X_HANDS   16  // bare hands (no boxing gloves)
#define T_X_NOSASH  32  // no belt and no belt tails
#define T_HAIR_N    6
#define T_BAND_N    5
#define T_SASH_N    4
typedef struct {
    // set by the game
    float x, hip_y;  // world position of the hip (y up is negative)
    int face;        // +1 looks right, -1 looks left
    float rot;       // radians (knock-down)
    float sx, sy;    // squash and stretch
    bool air;        // the pivot of the squash: the hip (air) or the floor
    const tlook_t *look;
    tpose_t pose;
    // made by t_puppet_solve()
    float P[16];     // the pose in radians (internal)
    float j[20][2];  // joints, local frame (see toon.c)
    float head_a;
    float hair[T_HAIR_N][2], band1[T_BAND_N][2], band2[T_BAND_N][2], sash[T_SASH_N][2];
    float head_w[2];  // the head center in the world
} tpuppet_t;

// Joint ids for t_puppet_joint()
enum {
    TJ_FSH,
    TJ_BSH,
    TJ_HEAD,
    TJ_FEL,
    TJ_FHA,
    TJ_BEL,
    TJ_BHA,
    TJ_FHIP,
    TJ_BHIP,
    TJ_FKN,
    TJ_FAN,
    TJ_BKN,
    TJ_BAN,
    TJ_FTOE,
    TJ_KNOT
};

// The hip height that puts the feet on the floor for this pose (when not in the air).
float t_pose_hip(const tpose_t *p);
// Make the joints. vx, vy: the speed of the hip (for the hair). t: seconds (the hair waves).
void t_puppet_solve(tpuppet_t *pp, float vx, float vy, float t);
void t_puppet_joint(const tpuppet_t *pp, int j, float *wx, float *wy);
void t_puppet_draw(const tpuppet_t *pp, float pxs);  // pxs = buffer pixels per unit now

// ---------------------------------------------------------------- stage (toon_stage.c)
typedef struct {
    float t;         // seconds (lights, stars, neon)
    float cheer;     // 0..1.3, the people on the roof
    float pigeon_t;  // seconds since the pigeon left (< 0: it sits)
    int stage;       // T_STAGE_*
} tstage_t;
enum {
    T_STAGE_ROOF,
    T_STAGE_SHUK,
    T_STAGE_PORT,
    T_STAGE_N
};
extern int t_stage_mask;  // bit 0 sky, 1 far, 2 near roofs, 3 lights, 4 roof wall, 5 floor
void t_stage_draw(const tview_t *v, const tcam_t *cam, const tstage_t *st);
void t_shadow(float x, float h);

// ---------------------------------------------------------------- the VHS filter
typedef struct {
    float t;       // seconds (noise, tracking band)
    float glitch;  // 0..1: torn lines (hits)
    int roll;      // output rows: the vertical hold slips (the K.O.)
    float flash;   // 0..1: white
    float fade;    // 0..1: 1 = normal
} tvhs_t;
// src: the low-resolution buffer (w x h); dst: the frame buffer (2w x 2h, byte-swapped).
// With t_two_cores(true), core 1 makes the odd rows (call it in enter, and false in leave).
void t_two_cores(bool on);
// The same work in two parts: begin starts core 1 on the rows; draw the next frame on core 0 (into
// another buffer); then end makes the rows that are left and waits for core 1.
void t_vhs_begin(const tcol_t *src, int w, int h, uint16_t *dst, const tvhs_t *o);
void t_vhs_end(void);
// Other looks for special moments (same buffers as t_vhs): the clean picture (each pixel 2 x 2),
// and black and white comic ink (halftone dots).
void t_post_clean(const tcol_t *src, int w, int h, uint16_t *dst);
void t_post_ink(const tcol_t *src, int w, int h, uint16_t *dst);
// More looks (FIGHT: one for each fighter): 0 a speed smear, 1 old film (sepia, scratches), 2 an RGB
// split, 3 a heat camera, 4 black and white with the red kept, 5 cycling hues. t = seconds (for the motion).
enum {
    T_FX_SMEAR,
    T_FX_SEPIA,
    T_FX_RGB,
    T_FX_HEAT,
    T_FX_NOIR,
    T_FX_HUE
};
void t_post_fx(const tcol_t *src, int w, int h, uint16_t *dst, int kind, float t);
void t_vhs(const tcol_t *src, int w, int h, uint16_t *dst, const tvhs_t *o);
