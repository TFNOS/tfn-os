// LEVEL: the board is a long bar, so it becomes a spirit level, a ruler and an angle meter.
// The task: the app task of TFN OS calls init, enter, input, update and render (app_level at the end of this file).
// The memory: the file statics of this file (the colours and the body rows that init makes); no heap.
//
// - LEVEL tab: the pose of the board selects the vial (with hysteresis):
//   on a long edge (screen vertical, long side horizontal) = a long vial for the long axis;
//   upright (long side vertical) = PLUMB, a short vial across the board (portrait screen);
//   flat (screen up) = a round BULLSEYE vial with two axes.
//   The bubble moves to the HIGH side with a soft spring. Under 0.1 degree the vial glows
//   green and a soft beep sounds once. Tap the number to change the unit (degrees, percent,
//   mm per m). HOLD keeps the reading. CAL: put the board on a surface, tap CAL, turn the
//   board 180 degrees end for end, tap CAL again: this gives the zero offset of the pose.
//   A long touch on CAL clears it.
// - RULER tab: millimetres on the top edge, inches on the bottom edge, and a red line that
//   you drag. CAL: drag the red mark to 80 mm of a real ruler, then tap DONE.
// - ANGLE tab: tap ZERO on the first surface, then put the board on the second surface:
//   the screen shows the angle between them. With no ZERO, it measures from "level on
//   the long edge".
// The gravity comes from imu_gravity_phys(): physical frame, +x right, +y down along the
// long side, +z into the screen (screen up = +z).
//
// CoreS3 (320 x 240, a 2.0 inch screen): the same tabs and poses. The long side is the 320 px
// side. Landscape: the content at the left (x 0..218) and a column of controls at the right
// (the speaker, the 3 tabs, HOLD and CAL). Portrait (PLUMB): the controls in two rows at the
// bottom. The ruler is about 40.8 mm long, so the scale calibration uses a 30 mm mark.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/settings.h"

#define RAD2DEG   57.2957795f
#define DEG2RAD   0.0174532925f
#define LP_TAU    0.25f  // s: low-pass of the gravity
#define LEVEL_ON  0.1f   // degrees: "level" (green, beep)
#define LEVEL_OFF 0.15f
#define BUB_A0    2.5f   // degrees: the bubble goes most of the way at this angle
#define SPRING_W  10.0f  // rad/s
#define SPRING_Z  0.6f
#define CAL_SEC   0.8f  // a calibration reading averages this long
#if BOARD_BAR_SCREEN
#define PPM_DEF     7.476f  // px for each mm (the screen is about 85.6 mm long)
#define CAL_MM      80
#define CAL_MM_TEXT "80"
#define LAND_W      640  // the landscape frame
#define LAND_H      172
#define PORT_W      172  // the portrait frame
#define PORT_H      640
#elif BOARD_UI_TABLET
// The Tab5: the 640 x 360 frame, 0.1725 mm a px (the panel shows each px 2 x 2). Its own layout below.
#define PPM_DEF     5.797f
#define CAL_MM      80
#define CAL_MM_TEXT "80"
#define LAND_W      640
#define LAND_H      360
#define PORT_W      360
#define PORT_H      640
#define PPM_MIN     5.0f
#define PPM_MAX     6.6f
#define LV_S        2  // the text of the buttons and the speaker: 2 times
#elif BOARD_ROUND
// THE ROUND PANEL (APP_ROUND_HI: the 1.75C 466 x 466, 0.095 mm a px; the Watcher 412 x 412): a round yellow plate.
// The level, the ruler and the protractor fill the circle; the tabs on the bottom arc, HOLD and CAL on the side
// arcs, the speaker on the left arc. RU(): a value of the 466 layout on this panel.
#define RU(v)       ((v) * BOARD_ROUND_HI / 466)
#define PPM_DEF     (BOARD_ROUND_HI_MM_PX > 0 ? 1.0f / BOARD_ROUND_HI_MM_PX : 1.0f / BOARD_NATIVE_MM_PX)
#define CAL_MM      30
#define CAL_MM_TEXT "30"
#define LAND_W      BOARD_ROUND_HI
#define LAND_H      BOARD_ROUND_HI
#define PORT_W      BOARD_ROUND_HI
#define PORT_H      BOARD_ROUND_HI
#define PPM_MIN     (PPM_DEF * 0.85f)
#define PPM_MAX     (PPM_DEF * 1.15f)
#define LV_S        2  // the text of the buttons and the speaker: 2 times
#else
// The 2.0 inch 320 x 240 panel: an active area of about 40.8 x 30.6 mm (0.1275 mm pixels).
#define PPM_DEF     7.843f
#define CAL_MM      30
#define CAL_MM_TEXT "30"
#define LAND_W      320
#define LAND_H      240
#define PORT_W      240
#define PORT_H      320
#endif
#ifndef PPM_MIN
#define PPM_MIN 6.5f
#define PPM_MAX 8.5f
#endif
#ifndef LV_S
#define LV_S 1
#endif

enum {
    TAB_LEVEL,
    TAB_RULER,
    TAB_ANGLE,
    N_TABS
};
enum {
    POSE_EDGE,
    POSE_PLUMB,
    POSE_FLAT
};
enum {
    U_DEG,
    U_PCT,
    U_MMM,
    N_UNITS
};
enum {
    B_NONE = -1,
    B_TAB0 = 0,
    B_TAB1,
    B_TAB2,
    B_HOLD,
    B_CAL,
    B_SOUND,
    B_READOUT
};
enum {
    CAL_IDLE,
    CAL_FIRST,
    CAL_WAIT,
    CAL_SECOND
};

static const char *const TAB_NAMES[N_TABS] = {"LEVEL", "RULER", "ANGLE"};
static const char *const UNIT_NAMES[N_UNITS] = {"DEGREES", "PERCENT", "MM PER M"};
static const char *const CAL_KEYS[4] = {"lvl_ce", "lvl_cp", "lvl_cfx", "lvl_cfy"};

static int s_tab, s_pose = POSE_EDGE;
static orient_t s_land = ORIENT_LAND_A, s_port = ORIENT_PORTRAIT;
static float s_g[3];  // low-pass gravity, unit length
static bool s_g_init;
static float s_ang[2];  // shown angles, degrees (FLAT: x and y)
static float s_hold_ang[2];
static bool s_hold;
static float s_bub[2], s_bub_v[2];  // bubble: -1..1 along the vial (FLAT: x and y)
static int s_unit;
static bool s_sound = true;
static bool s_level_now;
static float s_stab_ref, s_stab_t;
static float s_cal[4];  // offsets in degrees (edge, plumb, flat x, flat y)
static int s_cal_step;
static float s_cal_t, s_cal_acc[2], s_cal_r1[2];
static int s_cal_n;
static int s_cal_pose;
static char s_msg[56];
static int64_t s_msg_until;
static float s_ppm = PPM_DEF;
static float s_ruler_x = LAND_W / 2;
static bool s_ruler_cal;
static float s_mark_x;
static bool s_zero_set;
static float s_g0[3];
static int s_axis = -1;  // the device axis for the sign of ANGLE
static float s_rel;      // ANGLE: signed angle, degrees
static int s_press = B_NONE;
#define CAL_RESET_MS 1500      // the hold of CAL that resets the calibration of the tab
static int64_t s_cal_hold_t0;  // a hold of CAL started (0 = none)
static bool s_cal_hold_done;
static int64_t s_last, s_now;

// colours, made in init
static uint16_t s_row_land[LAND_H];  // the body, row by row (landscape)
static uint16_t s_row_port[PORT_W];  // the body across its width (portrait)
static uint16_t s_ink, s_ink_soft, s_panel, s_lcd, s_lcd_dim, s_green, s_amber, s_red;

// thin brushed lines on the metal: fixed, so the picture does not shimmer
#define N_BRUSH 48
static struct {
    int16_t a, b, len;
    int8_t tone;
} s_brush[N_BRUSH];

// ---------------------------------------------------------------- the angle math

// The long axis against the horizontal: positive = the physical +y end is lower.
static float edge_angle_phys(const float *g)
{
    return atan2f(g[1], sqrtf(g[0] * g[0] + g[2] * g[2])) * RAD2DEG;
}

// Upright board: the lean in the screen plane: positive = the physical +x side is lower.
static float plumb_angle_phys(const float *g)
{
    return atan2f(g[0], sqrtf(g[1] * g[1] + g[2] * g[2])) * RAD2DEG;
}

// Flat board (screen up or down): the two tilts.
static void flat_angles_phys(const float *g, float *ax, float *ay)
{
    float z = fabsf(g[2]);
    *ax = atan2f(g[0], z) * RAD2DEG;
    *ay = atan2f(g[1], z) * RAD2DEG;
}

// The pose from the gravity, with hysteresis.
static int pose_next(int pose, const float *g)
{
    float ax = fabsf(g[0]), ay = fabsf(g[1]), az = fabsf(g[2]);
    switch (pose) {
    case POSE_FLAT:
        if (az < 0.64f) {
            return ay > ax ? POSE_PLUMB : POSE_EDGE;
        }
        return POSE_FLAT;
    case POSE_EDGE:
        if (az > 0.77f) {
            return POSE_FLAT;
        }
        if (ay > ax + 0.30f) {
            return POSE_PLUMB;
        }
        return POSE_EDGE;
    default:
        if (az > 0.77f) {
            return POSE_FLAT;
        }
        if (ax > ay + 0.30f) {
            return POSE_EDGE;
        }
        return POSE_PLUMB;
    }
}

// Raw angles of a pose (before the calibration), physical frame.
static void pose_raw(int pose, const float *g, float *a)
{
    a[1] = 0;
    if (pose == POSE_EDGE) {
        a[0] = edge_angle_phys(g);
    } else if (pose == POSE_PLUMB) {
        a[0] = plumb_angle_phys(g);
    } else {
        flat_angles_phys(g, &a[0], &a[1]);
    }
}

// Shown angles: the raw angles minus the calibration, turned to the logical frame of o.
// EDGE and PLUMB: positive = the logical right end is lower. FLAT: x positive = the right
// side is lower, y positive = the bottom side is lower.
static void pose_shown(int pose, orient_t o, const float *g, const float *cal, float *out)
{
    float a[2];
    pose_raw(pose, g, a);
    if (pose == POSE_EDGE) {
        float v = a[0] - cal[0];
        out[0] = o == ORIENT_LAND_B ? -v : v;
        out[1] = 0;
    } else if (pose == POSE_PLUMB) {
        float v = a[0] - cal[1];
        out[0] = o == ORIENT_PORTRAIT_FLIP ? -v : v;
        out[1] = 0;
    } else {
        float x = a[0] - cal[2], y = a[1] - cal[3];
        // logical x = physical y in LAND_A, -y in LAND_B; logical y = -x in LAND_A, x in LAND_B
        if (o == ORIENT_LAND_B) {
            out[0] = -y;
            out[1] = x;
        } else {
            out[0] = y;
            out[1] = -x;
        }
    }
}

// A value in a unit. Returns false if it is too large to show.
static bool unit_value(float deg, int unit, float *v)
{
    if (unit == U_DEG) {
        *v = deg;
        return true;
    }
    if (fabsf(deg) >= 89.0f) {
        return false;
    }
    float t = tanf(deg * DEG2RAD) * (unit == U_PCT ? 100.0f : 1000.0f);
    if (fabsf(t) > 9999.0f) {
        return false;
    }
    *v = t;
    return true;
}

static void unit_text(float deg, int unit, char *buf, int len)
{
    float v;
    if (!unit_value(fabsf(deg), unit, &v)) {
        snprintf(buf, len, ">9999");
        return;
    }
    if (unit == U_PCT && v < 10.0f) {
        snprintf(buf, len, "%.2f", v);
    } else if (unit == U_MMM && v >= 100.0f) {
        snprintf(buf, len, "%.0f", v);
    } else {
        snprintf(buf, len, "%.1f", v);
    }
}

// The angle between two gravity vectors (degrees, 0..180) with a sign: the sign follows
// the device axis that the turn goes around (kept while the turn goes on).
static float rel_angle(const float *g0, const float *g, int *axis)
{
    float d = g0[0] * g[0] + g0[1] * g[1] + g0[2] * g[2];
    if (d > 1) {
        d = 1;
    }
    if (d < -1) {
        d = -1;
    }
    float a = acosf(d) * RAD2DEG;
    float n[3] = {g0[1] * g[2] - g0[2] * g[1], g0[2] * g[0] - g0[0] * g[2], g0[0] * g[1] - g0[1] * g[0]};
    if (a < 1.0f) {
        *axis = -1;
    }
    if (*axis < 0 && a > 2.0f) {
        int k = 0;
        for (int i = 1; i < 3; i++) {
            if (fabsf(n[i]) > fabsf(n[k])) {
                k = i;
            }
        }
        *axis = k;
    }
    if (*axis >= 0 && n[*axis] < 0) {
        a = -a;
    }
    return a;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void message(const char *m)
{
    snprintf(s_msg, sizeof(s_msg), "%s", m);
    s_msg_until = s_now + 3000;
}

static int cal_index(int pose)
{
    return pose == POSE_EDGE ? 0 : pose == POSE_PLUMB ? 1 : 2;
}

static void save_cal(void)
{
    for (int i = 0; i < 4; i++) {
        int32_t v = (int32_t)lroundf(s_cal[i] * 1000.0f);
        store_set(CAL_KEYS[i], (uint32_t)v);
    }
}

// ---------------------------------------------------------------- app

static uint16_t mix(uint16_t a_r, uint16_t a_g, uint16_t a_b, float k, int r, int g, int b)
{
    return rgb((uint8_t)(a_r + (r - a_r) * k), (uint8_t)(a_g + (g - a_g) * k), (uint8_t)(a_b + (b - a_b) * k));
}

static void level_init(void)
{
    s_ink = rgb(30, 26, 18);
    s_ink_soft = rgb(110, 84, 20);
    s_panel = rgb(24, 28, 34);
    s_lcd = rgb(190, 255, 170);
    s_lcd_dim = rgb(90, 140, 96);
    s_green = rgb(80, 255, 110);
    s_amber = rgb(255, 190, 60);
    s_red = rgb(235, 40, 40);
    // the body: yellow aluminium, dark milled edges, a light band near the top
    for (int yy = 0; yy < LAND_H; yy++) {
        int y = yy * 172 / LAND_H;  // the profile of the 172 px body
        uint16_t c;
        if (y < 3 || y >= 169) {
            c = rgb(96, 78, 30);
        } else if (y < 6) {
            c = rgb(255, 236, 150);  // the bevel catches the light
        } else if (y >= 164) {
            c = rgb(150, 112, 18);
        } else {
            float k = (float)(y - 6) / 158.0f;
            float band = expf(-(y - 22) * (y - 22) / 60.0f) * 0.5f;
            c = mix(255, 214, 46, k * 0.75f, 214, 150, 10);
            if (band > 0.02f) {
                int r = 255, gg = (int)(214 + (240 - 214) * band), b = (int)(46 + (120 - 46) * band);
                c = mix(r, (uint16_t)gg, (uint16_t)b, k * 0.75f, 214, 150, 10);
            }
        }
        s_row_land[yy] = c;
    }
    // portrait: the same body across its width (a round profile: light in the middle)
    for (int xx = 0; xx < PORT_W; xx++) {
        int x = xx * 172 / PORT_W;
        uint16_t c;
        if (x < 3 || x >= 169) {
            c = rgb(96, 78, 30);
        } else if (x < 6 || x >= 164) {
            c = x < 6 ? rgb(255, 236, 150) : rgb(150, 112, 18);
        } else {
            float d = fabsf((x - 60) / 110.0f);
            c = mix(255, 226, 90, clampf(d, 0, 1), 212, 148, 10);
        }
        s_row_port[xx] = c;
    }
    uint32_t r = 12345;
    for (int i = 0; i < N_BRUSH; i++) {
        r = r * 1103515245u + 12345u;
        s_brush[i].a = (int16_t)((r >> 8) % LAND_W);
        r = r * 1103515245u + 12345u;
        s_brush[i].b = (int16_t)(8 + (r >> 8) % (LAND_H - 16));
        r = r * 1103515245u + 12345u;
        s_brush[i].len = (int16_t)(40 + (r >> 8) % 160);
        s_brush[i].tone = (int8_t)(((r >> 20) & 1) ? 1 : -1);
    }
    s_unit = (int)store_get("lvl_unit", U_DEG);
    if (s_unit >= N_UNITS) {
        s_unit = U_DEG;
    }
    s_sound = store_get("lvl_snd", 1) != 0;
    for (int i = 0; i < 4; i++) {
        float v = (int32_t)store_get(CAL_KEYS[i], 0) / 1000.0f;
        s_cal[i] = (v > -10.0f && v < 10.0f) ? v : 0;
    }
    float ppm = store_get("lvl_ppm", (uint32_t)(PPM_DEF * 10000)) / 10000.0f;
    s_ppm = (ppm >= PPM_MIN && ppm <= PPM_MAX) ? ppm : PPM_DEF;
}

static void level_enter(int64_t now)
{
    s_last = s_now = now;
    s_g_init = false;
    s_tab = TAB_LEVEL;
    s_hold = false;
    s_cal_step = CAL_IDLE;
    s_ruler_cal = false;
    s_press = B_NONE;
    s_msg_until = 0;
    s_level_now = false;
    s_stab_t = 0;
    orient_t o = gfx_orient();
    if (orient_is_landscape(o)) {
        s_land = o;
    } else {
        s_port = o;
    }
}

// ---------------------------------------------------------------- layout and buttons

static bool portrait_now(void)
{
    return !orient_is_landscape(gfx_orient());
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

#if BOARD_BAR_SCREEN
// landscape: a column of buttons at the right; portrait: rows at the bottom
static void tab_rect(int i, int *x, int *y, int *w, int *h)
{
    if (portrait_now()) {
        *x = 6 + i * 54;
        *y = 588;
        *w = 52;
        *h = 44;
    } else {
        *x = 492 + i * 50;  // 46 wide with a gap of 4 (the hit zones do not overlap)
        *y = 48;
        *w = 46;
        *h = 44;
    }
}

static void btn_rect(int b, int *x, int *y, int *w, int *h)
{
    if (portrait_now()) {
        *y = 534;
        *w = 76;
        *h = 44;
        *x = b == B_HOLD ? 8 : 88;
    } else {
        *y = 100;
        *w = 68;
        *h = 44;
        *x = b == B_HOLD ? 492 : 568;
    }
}

static int hit(int x, int y)
{
    int bx, by, bw, bh;
    for (int i = 0; i < N_TABS; i++) {
        tab_rect(i, &bx, &by, &bw, &bh);
        if (in_rect(x, y, bx - 2, by - 3, bw + 4, bh + 5)) {
            return B_TAB0 + i;
        }
    }
    bool has_hold = s_tab != TAB_RULER;
    for (int b = B_HOLD; b <= B_CAL; b++) {
        if (b == B_HOLD && !has_hold) {
            continue;
        }
        btn_rect(b, &bx, &by, &bw, &bh);
        if (in_rect(x, y, bx - 2, by - 3, bw + 4, bh + 6)) {
            return b;
        }
    }
    if (portrait_now()) {
        if (in_rect(x, y, 124, 4, 44, 44)) {
            return B_SOUND;
        }
        if (in_rect(x, y, 8, 244, 156, 124)) {
            return B_READOUT;
        }
    } else {
        if (in_rect(x, y, 594, 0, 46, 44)) {
            return B_SOUND;
        }
        if (s_tab != TAB_RULER && in_rect(x, y, 8, 10, 172, 106)) {
            return B_READOUT;
        }
    }
    return B_NONE;
}
#elif BOARD_UI_TABLET
// the Tab5: landscape: the content at the left (x 0 .. 440) and a column of big buttons at the right; portrait:
// two rows at the bottom (HOLD and CAL, then the tabs). The buttons are 62 px (10.7 mm) high or more.
#define COL_X 452
#define COL_W 180
static void tab_rect(int i, int *x, int *y, int *w, int *h)
{
    if (portrait_now()) {
        *x = 16 + i * 112;
        *y = 496;
        *w = 104;
        *h = 96;
    } else {
        *x = COL_X;
        *y = 72 + i * 70;
        *w = COL_W;
        *h = 62;
    }
}

static void btn_rect(int b, int *x, int *y, int *w, int *h)
{
    if (portrait_now()) {
        *y = 392;
        *w = 160;
        *h = 84;
        *x = b == B_HOLD ? 16 : 184;
    } else {
        *y = 284;
        *h = 66;
        *w = s_tab == TAB_RULER ? COL_W : (COL_W - 6) / 2;
        *x = b == B_HOLD || s_tab == TAB_RULER ? COL_X : COL_X + COL_W - *w;
    }
}

static int hit(int x, int y)
{
    int bx, by, bw, bh;
    for (int i = 0; i < N_TABS; i++) {
        tab_rect(i, &bx, &by, &bw, &bh);
        if (in_rect(x, y, bx - 3, by - 3, bw + 6, bh + 6)) {
            return B_TAB0 + i;
        }
    }
    bool has_hold = s_tab != TAB_RULER;
    for (int b = B_HOLD; b <= B_CAL; b++) {
        if (b == B_HOLD && !has_hold) {
            continue;
        }
        btn_rect(b, &bx, &by, &bw, &bh);
        if (in_rect(x, y, bx - 3, by - 3, bw + 6, bh + 6)) {
            return b;
        }
    }
    if (portrait_now()) {
        if (in_rect(x, y, 290, 0, 70, 70)) {
            return B_SOUND;
        }
        if (in_rect(x, y, 16, 222, 328, 150)) {
            return B_READOUT;
        }
    } else {
        if (in_rect(x, y, 552, 0, 88, 64)) {
            return B_SOUND;
        }
        if (s_tab != TAB_RULER && in_rect(x, y, 0, 0, COL_X - 8, LAND_H)) {
            return B_READOUT;
        }
    }
    return B_NONE;
}
#elif BOARD_ROUND
// the half width of the circle at the row y
static int rchord(int y)
{
    int r = LAND_W / 2, dy = y - LAND_H / 2;
    return dy * dy >= r * r ? 0 : (int)sqrtf((float)(r * r - dy * dy));
}
// the 3 tabs: a row on the bottom arc
static void tab_rect(int i, int *x, int *y, int *w, int *h)
{
    int gap = RU(6), half = rchord(RU(430)) - RU(6);
    *y = RU(384);
    *h = RU(46);
    *w = (2 * half - 2 * gap) / N_TABS;
    *x = LAND_W / 2 - half + i * (*w + gap);
}

// HOLD on the left arc, CAL on the right arc (the ruler: CAL at the top right of its reading)
static void btn_rect(int b, int *x, int *y, int *w, int *h)
{
    if (s_tab == TAB_RULER) {
        *x = RU(318);
        *y = RU(80);
        *w = RU(80);
        *h = RU(50);
        return;
    }
    *x = b == B_HOLD ? RU(14) : RU(376);
    *y = RU(232);
    *w = RU(76);
    *h = RU(62);
}
#define SND_RX RU(30)  // the speaker: on the left arc over HOLD
#define SND_RY RU(158)

static int hit(int x, int y)
{
    int bx, by, bw, bh;
    for (int i = 0; i < N_TABS; i++) {
        tab_rect(i, &bx, &by, &bw, &bh);
        if (in_rect(x, y, bx - 2, by - 2, bw + 4, bh + 4)) {
            return B_TAB0 + i;
        }
    }
    for (int b = B_HOLD; b <= B_CAL; b++) {
        if (b == B_HOLD && s_tab == TAB_RULER) {
            continue;
        }
        btn_rect(b, &bx, &by, &bw, &bh);
        if (in_rect(x, y, bx - 2, by - 2, bw + 4, bh + 4)) {
            return b;
        }
    }
    if (in_rect(x, y, SND_RX - 4, SND_RY - 4, RU(64), RU(60))) {
        return B_SOUND;
    }
    if (s_tab != TAB_RULER && in_rect(x, y, RU(96), RU(50), RU(274), RU(326))) {
        return B_READOUT;
    }
    return B_NONE;
}
#else
// landscape: a column at the right (the speaker, the 3 tabs, HOLD and CAL side by side);
// portrait: two rows at the bottom (HOLD and CAL, then the tabs)
#define COL_X  226
#define COL_W  92
#define COL_DY 0
#define SND_X  (COL_X + COL_W - 46)
static void tab_rect(int i, int *x, int *y, int *w, int *h)
{
    if (portrait_now()) {
        *x = 8 + i * 76;
        *y = 274;
        *w = 72;
        *h = 42;
    } else {
        *x = COL_X;
        *y = 48 - COL_DY + i * 46;
        *w = COL_W;
        *h = 42;  // (the hit zone is 2 px more on each side: 46)
    }
}

static void btn_rect(int b, int *x, int *y, int *w, int *h)
{
    if (portrait_now()) {
        *y = 226;
        *w = 108;
        *h = 42;
        *x = b == B_HOLD ? 8 : 124;
    } else {
        // HOLD and CAL side by side under the tabs (the ruler has no HOLD: CAL takes the row)
        *y = 190 - COL_DY;
        *h = 42;
        *w = s_tab == TAB_RULER ? COL_W : (COL_W - 4) / 2;
        *x = b == B_HOLD || s_tab == TAB_RULER ? COL_X : COL_X + COL_W - *w;
    }
}

static int hit(int x, int y)
{
    int bx, by, bw, bh;
    for (int i = 0; i < N_TABS; i++) {
        tab_rect(i, &bx, &by, &bw, &bh);
        if (in_rect(x, y, bx - 2, by - 2, bw + 4, bh + 4)) {
            return B_TAB0 + i;
        }
    }
    bool has_hold = s_tab != TAB_RULER;
    for (int b = B_HOLD; b <= B_CAL; b++) {
        if (b == B_HOLD && !has_hold) {
            continue;
        }
        btn_rect(b, &bx, &by, &bw, &bh);
        if (in_rect(x, y, bx - 2, by - 2, bw + 4, bh + 4)) {
            return b;
        }
    }
    if (portrait_now()) {
        if (in_rect(x, y, 186, 0, 54, 44)) {
            return B_SOUND;
        }
        if (in_rect(x, y, 8, 118, 224, 106)) {
            return B_READOUT;
        }
    } else {
        if (in_rect(x, y, SND_X, 0, 46, 44)) {
            return B_SOUND;
        }
        if (s_tab != TAB_RULER && in_rect(x, y, 0, 0, COL_X - 6, 240)) {
            return B_READOUT;
        }
    }
    return B_NONE;
}
#endif

// ---------------------------------------------------------------- input

static void cal_cancel(const char *why)
{
    if (s_cal_step != CAL_IDLE) {
        s_cal_step = CAL_IDLE;
        if (why) {
            message(why);
        }
    }
}

static void press_cal(void)
{
    if (s_tab == TAB_RULER) {
        if (!s_ruler_cal) {
            s_ruler_cal = true;
            s_mark_x = CAL_MM * s_ppm;
            message("DRAG THE RED MARK TO " CAL_MM_TEXT " MM OF A RULER");
        } else {
            float ppm = s_mark_x / CAL_MM;
            s_ruler_cal = false;
            if (ppm < PPM_MIN || ppm > PPM_MAX) {
                message("NOT SAVED: THE MARK IS TOO FAR");
            } else {
                s_ppm = ppm;
                store_set("lvl_ppm", (uint32_t)lroundf(ppm * 10000.0f));
                message("RULER SCALE SAVED");
            }
        }
        return;
    }
    if (s_tab == TAB_ANGLE) {
        memcpy(s_g0, s_g, sizeof(s_g0));
        s_zero_set = true;
        s_axis = -1;
        message("ZERO SET: NOW PUT IT ON THE OTHER SURFACE");
        return;
    }
    // LEVEL: the two readings of the flip method
    if (s_cal_step == CAL_IDLE) {
        s_cal_step = CAL_FIRST;
        s_cal_pose = s_pose;
        s_cal_t = 0;
        s_cal_n = 0;
        s_cal_acc[0] = s_cal_acc[1] = 0;
        message("CAL 1 OF 2: KEEP IT STILL...");
    } else if (s_cal_step == CAL_WAIT) {
        s_cal_step = CAL_SECOND;
        s_cal_t = 0;
        s_cal_n = 0;
        s_cal_acc[0] = s_cal_acc[1] = 0;
        message("CAL 2 OF 2: KEEP IT STILL...");
    }
}

static void long_cal(void)
{
    if (s_tab == TAB_RULER) {
        s_ppm = PPM_DEF;
        s_ruler_cal = false;
        store_set("lvl_ppm", (uint32_t)lroundf(PPM_DEF * 10000.0f));
        message("RULER SCALE RESET");
    } else if (s_tab == TAB_ANGLE) {
        s_zero_set = false;
        s_axis = -1;
        message("ZERO CLEARED: FROM LEVEL ON THE EDGE");
    } else {
        cal_cancel(NULL);
        int k = cal_index(s_pose);
        s_cal[k] = 0;
        if (s_pose == POSE_FLAT) {
            s_cal[3] = 0;
        }
        save_cal();
        message("CALIBRATION CLEARED");
    }
    sfx_play(SFX_CUT);
}

static void ruler_drag(int x)
{
    float v = clampf((float)x, 0, LAND_W - 1);
    if (s_ruler_cal) {
        s_mark_x = v;
    } else {
        s_ruler_x = v;
    }
}

static void level_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    int b = hit(x, y);
    switch (ev->kind) {
    case IN_DOWN:
        s_press = b;
        s_cal_hold_t0 = b == B_CAL ? (s_now > 0 ? s_now : 1) : 0;
        if (s_tab == TAB_RULER && b == B_NONE) {
            ruler_drag(x);
        }
        break;
    case IN_MOVE:
        if (s_tab == TAB_RULER && s_press == B_NONE) {
            ruler_drag(x);
        }
        if (b != s_press) {
            s_press = s_press == B_NONE ? B_NONE : -2;
        }
        break;
    case IN_UP:
        s_press = B_NONE;
        s_cal_hold_t0 = 0;
        break;
    case IN_LONG:
        break;  // (the reset of CAL is a hold of 1.5 s: level_update)
    case IN_TAP:
        s_press = B_NONE;
        if (s_cal_hold_done) {
            s_cal_hold_done = false;  // (this touch reset the calibration: no tap)
            break;
        }
        if (b >= B_TAB0 && b <= B_TAB2) {
            if (s_tab != b - B_TAB0) {
                cal_cancel("CAL STOPPED");
                s_ruler_cal = false;
                s_tab = b - B_TAB0;
                s_hold = false;
            }
            sfx_play(SFX_TICK);
        } else if (b == B_HOLD) {
            s_hold = !s_hold;
            memcpy(s_hold_ang, s_ang, sizeof(s_hold_ang));
            sfx_play(SFX_CLICK);
        } else if (b == B_CAL) {
            press_cal();
            sfx_play(SFX_CLICK);
        } else if (b == B_SOUND) {
            s_sound = !s_sound;
            store_set("lvl_snd", s_sound);
            sfx_play(SFX_TICK);
        } else if (s_tab == TAB_LEVEL) {
            // the number or any free place: the next unit
            s_unit = (s_unit + 1) % N_UNITS;
            store_set("lvl_unit", (uint32_t)s_unit);
            sfx_play(SFX_TICK);
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- update

static void spring(float *p, float *v, float target, float dt)
{
    // in steps of 10 ms or less, so a slow frame does not make it wild
    while (dt > 0) {
        float h = dt > 0.01f ? 0.01f : dt;
        float a = SPRING_W * SPRING_W * (target - *p) - 2.0f * SPRING_Z * SPRING_W * *v;
        *v += a * h;
        *p += *v * h;
        dt -= h;
    }
}

static void read_gravity(float dt)
{
    float g[3];
    imu_gravity_phys(&g[0], &g[1], &g[2]);
    float n = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    if (n < 0.2f) {
        return;  // free fall or no data: keep the last value
    }
    for (int i = 0; i < 3; i++) {
        g[i] /= n;
    }
    if (!s_g_init) {
        memcpy(s_g, g, sizeof(g));
        s_g_init = true;
        s_pose = fabsf(g[2]) > 0.7f ? POSE_FLAT : (fabsf(g[1]) > fabsf(g[0]) ? POSE_PLUMB : POSE_EDGE);
        return;
    }
    float k = dt / (LP_TAU + dt);
    for (int i = 0; i < 3; i++) {
        s_g[i] += (g[i] - s_g[i]) * k;
    }
    n = sqrtf(s_g[0] * s_g[0] + s_g[1] * s_g[1] + s_g[2] * s_g[2]);
    if (n > 0.01f) {
        for (int i = 0; i < 3; i++) {
            s_g[i] /= n;
        }
    }
}

static void pick_orient(void)
{
    // the sides, with hysteresis: logical down must follow the gravity
    if (s_g[0] < -0.35f) {
        s_land = ORIENT_LAND_A;
    } else if (s_g[0] > 0.35f) {
        s_land = ORIENT_LAND_B;
    }
    if (s_g[1] > 0.35f) {
        s_port = ORIENT_PORTRAIT;
    } else if (s_g[1] < -0.35f) {
        s_port = ORIENT_PORTRAIT_FLIP;
    }
    orient_t want = (s_tab == TAB_LEVEL && s_pose == POSE_PLUMB) ? s_port : s_land;
    if (gfx_orient() != want) {
        gfx_set_orient(want);
        s_bub[0] = s_bub[1] = 0;
        s_bub_v[0] = s_bub_v[1] = 0;
    }
}

static void cal_update(float dt)
{
    if (s_cal_step == CAL_IDLE) {
        return;
    }
    if (s_pose != s_cal_pose) {
        // turning end for end keeps the pose; another pose stops the calibration
        cal_cancel("CAL STOPPED: THE POSE CHANGED");
        return;
    }
    if (s_cal_step == CAL_WAIT) {
        return;
    }
    float a[2];
    pose_raw(s_pose, s_g, a);
    s_cal_acc[0] += a[0];
    s_cal_acc[1] += a[1];
    s_cal_n++;
    s_cal_t += dt;
    if (s_cal_t < CAL_SEC) {
        return;
    }
    float r0 = s_cal_acc[0] / s_cal_n, r1 = s_cal_acc[1] / s_cal_n;
    if (s_cal_step == CAL_FIRST) {
        s_cal_r1[0] = r0;
        s_cal_r1[1] = r1;
        s_cal_step = CAL_WAIT;
        message("TURN IT 180 END FOR END, TAP CAL");
        sfx_play(SFX_BEEP);
        return;
    }
    // the surface angle changes its sign after the turn, the offset does not
    float e0 = (s_cal_r1[0] + r0) * 0.5f, e1 = (s_cal_r1[1] + r1) * 0.5f;
    s_cal_step = CAL_IDLE;
    if (fabsf(e0) > 5.0f || fabsf(e1) > 5.0f) {
        message("NOT SAVED: OFFSET OVER 5 DEGREES");
        sfx_play(SFX_BUMP);
        return;
    }
    int k = cal_index(s_pose);
    s_cal[k] = e0;
    if (s_pose == POSE_FLAT) {
        s_cal[3] = e1;
    }
    save_cal();
    char m[48];
    snprintf(m, sizeof(m), "CALIBRATED: OFFSET %+.2f", e0);
    message(m);
    sfx_play(SFX_COIN);
}

static void long_cal(void);

static void level_update(int64_t now)
{
    if (s_cal_hold_t0 && s_press == B_CAL && now - s_cal_hold_t0 >= CAL_RESET_MS) {
        s_cal_hold_t0 = 0;
        s_cal_hold_done = true;
        s_press = B_NONE;
        long_cal();
    }
    if (s_press != B_CAL) {
        s_cal_hold_t0 = 0;
    }
    float dt = (now - s_last) / 1000.0f;
    s_last = s_now = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    if (!imu_ok()) {
        return;
    }
    read_gravity(dt);
    if (!s_g_init) {
        return;
    }
    int pose = pose_next(s_pose, s_g);
    if (pose != s_pose) {
        s_pose = pose;
        s_level_now = false;
        s_hold = false;
    }
    pick_orient();
    cal_update(dt);

    float a[2];
    pose_shown(s_pose, gfx_orient(), s_g, s_cal, a);
    if (!s_hold) {
        memcpy(s_ang, a, sizeof(a));
    }
    const float *show = s_hold ? s_hold_ang : s_ang;

    // the bubble goes to the high side
    float t[2];
    if (s_pose == POSE_FLAT) {
        float r = sqrtf(show[0] * show[0] + show[1] * show[1]);
        float k = r > 1e-4f ? tanhf(r / BUB_A0) / r : 1.0f / BUB_A0;
        t[0] = -show[0] * k;
        t[1] = -show[1] * k;
    } else {
        t[0] = -tanhf(show[0] / BUB_A0);
        t[1] = 0;
    }
    spring(&s_bub[0], &s_bub_v[0], t[0], dt);
    spring(&s_bub[1], &s_bub_v[1], t[1], dt);

    // steady: the reading moved less than 0.05 degree for 0.6 s
    float m = s_pose == POSE_FLAT ? sqrtf(a[0] * a[0] + a[1] * a[1]) : a[0];
    if (fabsf(m - s_stab_ref) > 0.05f) {
        s_stab_ref = m;
        s_stab_t = 0;
    } else {
        s_stab_t += dt;
    }

    // level: a soft beep once
    float mag = s_pose == POSE_FLAT ? sqrtf(show[0] * show[0] + show[1] * show[1]) : fabsf(show[0]);
    if (s_tab == TAB_LEVEL && !s_hold) {
        if (!s_level_now && mag < LEVEL_ON) {
            s_level_now = true;
            if (s_sound) {
                sfx_tone(1568, 1568, 70, W_SINE, 22);
            }
        } else if (s_level_now && mag > LEVEL_OFF) {
            s_level_now = false;
        }
    }

    // angle meter
    float g0[3];
    if (s_zero_set) {
        memcpy(g0, s_g0, sizeof(g0));
    } else {
        // "level on the bottom long edge" of the screen now
        g0[0] = s_land == ORIENT_LAND_B ? 1.0f : -1.0f;
        g0[1] = g0[2] = 0;
    }
    float rel = rel_angle(g0, s_g, &s_axis);
    if (!(s_hold && s_tab == TAB_ANGLE)) {
        s_rel = rel;
    }
}

// ---------------------------------------------------------------- drawing helpers

static void seg(float x0, float y0, float x1, float y1, int t, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy));
    if (n < 1) {
        n = 1;
    }
    for (int i = 0; i <= n; i++) {
        float f = (float)i / n;
        gfx_fill_rect((int)(x0 + dx * f) - t / 2, (int)(y0 + dy * f) - t / 2, t, t, c);
    }
}

static void degree_sign(int x, int y, int scale, uint16_t c)
{
    int r = scale + 2;
    gfx_ring(x + r, y + r, r, r - (scale > 2 ? 2 : 1), c);
}

static void emboss(int x, int y, const char *t, int scale)
{
    gfx_text(x + 1, y + 1, t, scale, rgb(255, 240, 170));
    gfx_text(x, y, t, scale, rgb(120, 90, 16));
}

#if BOARD_BAR_SCREEN  // the CoreS3 body has no room for the screws
static void screw(int x, int y)
{
    gfx_fill_circle(x + 1, y + 1, 6, rgb(120, 92, 20));
    gfx_fill_circle(x, y, 6, rgb(150, 150, 156));
    gfx_fill_circle(x - 1, y - 1, 4, rgb(205, 205, 212));
    gfx_fill_rect(x - 4, y - 1, 9, 2, rgb(70, 70, 76));
}
#endif

static void body_landscape(void)
{
    for (int y = 0; y < LAND_H; y++) {
        gfx_fill_rect(0, y, LAND_W, 1, s_row_land[y]);
    }
    for (int i = 0; i < N_BRUSH; i++) {
        uint16_t c = s_row_land[s_brush[i].b];
        // a little lighter or darker than the row
        uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
        int r = (sw >> 11) & 31, g = (sw >> 5) & 63, b = sw & 31;
        r = r + s_brush[i].tone * 2;
        g = g + s_brush[i].tone * 3;
        r = r < 0 ? 0 : r > 31 ? 31 : r;
        g = g < 0 ? 0 : g > 63 ? 63 : g;
        uint16_t n = (uint16_t)((r << 11) | (g << 5) | b);
        gfx_fill_rect(s_brush[i].a, s_brush[i].b, s_brush[i].len, 1, (uint16_t)((n >> 8) | (n << 8)));
    }
#if BOARD_ROUND
    // the round plate: a dark milled rim
    gfx_ring(LAND_W / 2, LAND_H / 2, LAND_W / 2, LAND_W / 2 - RU(8), rgb(96, 78, 30));
    gfx_ring(LAND_W / 2, LAND_H / 2, LAND_W / 2 - RU(8), LAND_W / 2 - RU(11), rgb(255, 236, 150));
    return;
#endif
    // end caps (grey rubber)
    for (int k = 0; k < 2; k++) {
        int x = k ? LAND_W - 8 : 0;
        gfx_fill_rect(x, 0, 8, LAND_H, rgb(58, 60, 66));
        gfx_fill_rect(k ? LAND_W - 8 : 6, 0, 2, LAND_H, rgb(90, 94, 100));
    }
}

static void body_portrait(void)
{
    for (int y = 0; y < PORT_H; y++) {
        memcpy(g_fb + y * PORT_W, s_row_port, sizeof(s_row_port));
    }
    for (int i = 0; i < N_BRUSH / 2; i++) {
        int x = 8 + s_brush[i].b % (PORT_W - 16);
        uint16_t c = s_row_port[x];
        uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
        int g = ((sw >> 5) & 63) + s_brush[i].tone * 3;
        g = g < 0 ? 0 : g > 63 ? 63 : g;
        uint16_t n = (uint16_t)((sw & ~(63 << 5)) | (g << 5));
        gfx_fill_rect(x, s_brush[i].a, 1, s_brush[i].len, (uint16_t)((n >> 8) | (n << 8)));
    }
    gfx_fill_rect(0, 0, PORT_W, 8, rgb(58, 60, 66));
    gfx_fill_rect(0, PORT_H - 8, PORT_W, 8, rgb(58, 60, 66));
    gfx_fill_rect(0, 6, PORT_W, 2, rgb(90, 94, 100));
    gfx_fill_rect(0, PORT_H - 8, PORT_W, 2, rgb(90, 94, 100));
}

// a dark recessed window with a bevel
static void recess(int x, int y, int w, int h)
{
    gfx_fill_rect(x - 2, y - 2, w + 4, h + 4, rgb(255, 236, 150));
    gfx_fill_rect(x - 2, y - 2, w + 2, h + 2, rgb(120, 90, 16));
    gfx_fill_rect(x, y, w, h, rgb(20, 22, 20));
}

static void panel(int x, int y, int w, int h)
{
    gfx_fill_rect(x - 2, y - 2, w + 4, h + 4, rgb(255, 236, 150));
    gfx_fill_rect(x - 2, y - 2, w + 2, h + 2, rgb(110, 82, 14));
    gfx_fill_rect(x, y, w, h, s_panel);
    gfx_fill_rect(x, y, w, 2, rgb(14, 16, 20));
}

static void button(int x, int y, int w, int h, const char *label, bool on, bool hot)
{
    uint16_t top = hot ? rgb(255, 255, 255) : (on ? rgb(110, 230, 140) : rgb(96, 100, 110));
    uint16_t face = hot ? rgb(230, 230, 240) : (on ? rgb(40, 150, 70) : rgb(48, 52, 60));
    gfx_fill_rect(x, y + 2, w, h, rgb(110, 82, 14));  // shadow on the metal
    gfx_fill_rect(x, y, w, h, face);
    gfx_fill_rect(x, y, w, 2, top);
    gfx_text_center_in(x, w, y + (h - 7 * LV_S) / 2, label, LV_S, hot ? rgb(0, 0, 0) : rgb(255, 255, 255));
}

static void draw_buttons(void)
{
    int x, y, w, h;
    for (int i = 0; i < N_TABS; i++) {
        tab_rect(i, &x, &y, &w, &h);
        button(x, y, w, h, TAB_NAMES[i], s_tab == i, s_press == B_TAB0 + i);
    }
    if (s_tab != TAB_RULER) {
        btn_rect(B_HOLD, &x, &y, &w, &h);
        button(x, y, w, h, "HOLD", s_hold, s_press == B_HOLD);
    }
    btn_rect(B_CAL, &x, &y, &w, &h);
    const char *cal = "CAL";
    bool on = false;
    if (s_tab == TAB_ANGLE) {
        cal = "ZERO";
        on = s_zero_set;
    } else if (s_tab == TAB_RULER && s_ruler_cal) {
        cal = "DONE";
        on = true;
    } else if (s_tab == TAB_LEVEL && s_cal_step != CAL_IDLE) {
        cal = s_cal_step == CAL_WAIT ? "CAL 2" : "...";
        on = true;
    }
    button(x, y, w, h, cal, on, s_press == B_CAL);
    if (s_cal_hold_t0 && s_press == B_CAL && s_now - s_cal_hold_t0 > 250) {
        // the fill of the hold that resets (it shows after a normal tap time)
        int f = (int)((s_now - s_cal_hold_t0) * (w - 4) / CAL_RESET_MS);
        if (f > w - 4) {
            f = w - 4;
        }
        gfx_fill_rect(x + 2, y + h - 7, f, 5, s_red);
    }
    // the speaker
#if BOARD_BAR_SCREEN
    int sx = portrait_now() ? 132 : 604, sy = portrait_now() ? 16 : 12;
#elif BOARD_UI_TABLET
    int sx = portrait_now() ? 300 : 572, sy = portrait_now() ? 14 : 10;
#elif BOARD_ROUND
    int sx = SND_RX, sy = SND_RY;
#else
    int sx = portrait_now() ? 200 : SND_X + 8, sy = portrait_now() ? 12 : 10 - COL_DY / 2;
#endif
    uint16_t sc = s_sound ? rgb(40, 40, 46) : rgb(150, 120, 50);
    const int k = LV_S;  // (the Tab5: the speaker 2 times)
    gfx_fill_rect(sx, sy + 6 * k, 6 * k, 8 * k, sc);
    for (int i = 0; i < 8 * k; i++) {
        gfx_fill_rect(sx + 6 * k + i, sy + 6 * k - i, 1, 8 * k + 2 * i, sc);
    }
    if (s_sound) {
        // two sound waves
        gfx_fill_rect(sx + 17 * k, sy + 6 * k, 2 * k, 8 * k, sc);
        gfx_fill_rect(sx + 22 * k, sy + 2 * k, 2 * k, 16 * k, sc);
    } else {
        seg((float)sx + 16 * k, (float)sy + 4 * k, (float)sx + 24 * k, (float)sy + 16 * k, 2 * k, s_red);
        seg((float)sx + 24 * k, (float)sy + 4 * k, (float)sx + 16 * k, (float)sy + 16 * k, 2 * k, s_red);
    }
}

// A horizontal glass vial (a capsule) with fluid, marks and the bubble.
// bub: -1..1 along the vial; v: bubble speed (for the stretch).
static void vial(int cx, int cy, int w, int h, float bub, float v, bool level)
{
    int R = h / 2;
    // the glass edge, then the fluid row by row (a round tube: light at the top)
    for (int j = -2; j < h + 2; j++) {
        float dy = j - h / 2.0f + 0.5f;
        float rr = (float)(R + 2) * (R + 2) - dy * dy;
        if (rr < 0) {
            continue;
        }
        int hw = (w / 2 - R) + (int)sqrtf(rr);
        gfx_fill_rect(cx - hw, cy - h / 2 + j, 2 * hw, 1, rgb(60, 70, 50));
    }
    for (int j = 0; j < h; j++) {
        float dy = j - h / 2.0f + 0.5f;
        float rr = (float)R * R - dy * dy;
        if (rr < 0) {
            continue;
        }
        int hw = (w / 2 - R) + (int)sqrtf(rr);
        float k = (float)j / h;
        float shade = 1.0f - fabsf(k - 0.38f) * 1.1f;
        if (shade < 0.25f) {
            shade = 0.25f;
        }
        uint16_t c = level ? rgb((uint8_t)(120 * shade), (uint8_t)(255 * shade), (uint8_t)(90 * shade))
                           : rgb((uint8_t)(200 * shade), (uint8_t)(235 * shade), (uint8_t)(40 * shade));
        gfx_fill_rect(cx - hw, cy - h / 2 + j, 2 * hw, 1, c);
    }
    // the bubble: it floats at the top of the curved tube and drops a little to the ends
    float stretch = 1.0f + clampf(fabsf(v) * 0.25f, 0, 0.35f);
    int brx = (int)(h * 0.62f * stretch), bry = (int)(h * 0.30f / sqrtf(stretch));
    float travel = w / 2.0f - R - brx * 0.6f;
    int bx = cx + (int)(bub * travel);
    int by = cy - h / 2 + bry + 3 + (int)(bub * bub * 4);
    gfx_fill_ellipse(bx, by + 1, brx + 1, bry + 1, level ? rgb(60, 160, 60) : rgb(150, 170, 40));
    gfx_fill_ellipse(bx, by, brx, bry, level ? rgb(200, 255, 200) : rgb(245, 255, 200));
    gfx_fill_ellipse(bx - brx / 3, by - bry / 3, brx / 3, bry / 3 > 1 ? bry / 3 : 1, rgb(255, 255, 255));
    // marks: the bubble sits between the two middle lines when level
    int gap = (int)(h * 0.62f) + 4;
    for (int s = -1; s <= 1; s += 2) {
        gfx_fill_rect(cx + s * gap - 1, cy - h / 2 + 2, 2, h - 4, rgb(20, 20, 20));
        gfx_fill_rect(cx + s * (gap + 22), cy - h / 2 + 6, 1, h - 12, rgb(40, 50, 30));
    }
    // glass light
    gfx_fill_rect(cx - w / 2 + R, cy - h / 2 + 3, w - 2 * R, 2, rgb(255, 255, 230));
    gfx_fill_rect(cx - w / 2 + R + 10, cy + h / 2 - 5, w - 2 * R - 20, 1, rgb(230, 255, 180));
}

// a round dish for the flat board: bub = the bubble, -1..1 in x and y
static void bullseye(int cx, int cy, int r, float bx, float by, bool level)
{
    gfx_fill_circle(cx + 2, cy + 2, r + 4, rgb(120, 90, 16));
    gfx_fill_circle(cx, cy, r + 4, rgb(60, 70, 50));
    for (int k = 0; k < 8; k++) {
        int rr = r - k * r / 9;
        float s = 0.45f + 0.07f * k;
        gfx_fill_circle(cx - k / 2, cy - k / 2, rr,
                        level ? rgb((uint8_t)(120 * s), (uint8_t)(255 * s), (uint8_t)(90 * s))
                              : rgb((uint8_t)(200 * s), (uint8_t)(235 * s), (uint8_t)(40 * s)));
    }
#if BOARD_UI_TABLET || BOARD_ROUND
    // (the Tab5: a dish of r 126; the round panel: r 112; the ring, the bubble and the light in proportion)
    gfx_ring(cx, cy, 28, 25, rgb(20, 20, 20));
    gfx_ring(cx, cy, r * 2 / 3, r * 2 / 3 - 2, rgb(40, 50, 30));
    gfx_fill_rect(cx - r + 6, cy, 2 * r - 12, 2, rgb(40, 50, 30));
    gfx_fill_rect(cx, cy - r + 6, 2, 2 * r - 12, rgb(40, 50, 30));
    float len = sqrtf(bx * bx + by * by);
    if (len > 1) {
        bx /= len;
        by /= len;
    }
    int px = cx + (int)(bx * (r - 26)), py = cy + (int)(by * (r - 26));
    gfx_fill_circle(px + 2, py + 2, 22, level ? rgb(60, 160, 60) : rgb(150, 170, 40));
    gfx_fill_circle(px, py, 20, level ? rgb(200, 255, 200) : rgb(245, 255, 200));
    gfx_fill_circle(px - 7, py - 7, 6, rgb(255, 255, 255));
    gfx_fill_ellipse(cx - r / 3, cy - r / 2, r / 4, 6, rgb(255, 255, 230));
#else
    gfx_ring(cx, cy, 16, 14, rgb(20, 20, 20));
    gfx_ring(cx, cy, r * 2 / 3, r * 2 / 3 - 1, rgb(40, 50, 30));
    gfx_fill_rect(cx - r + 4, cy, 2 * r - 8, 1, rgb(40, 50, 30));
    gfx_fill_rect(cx, cy - r + 4, 1, 2 * r - 8, rgb(40, 50, 30));
    float len = sqrtf(bx * bx + by * by);
    if (len > 1) {
        bx /= len;
        by /= len;
    }
    int px = cx + (int)(bx * (r - 14)), py = cy + (int)(by * (r - 14));
    gfx_fill_circle(px + 1, py + 1, 12, level ? rgb(60, 160, 60) : rgb(150, 170, 40));
    gfx_fill_circle(px, py, 11, level ? rgb(200, 255, 200) : rgb(245, 255, 200));
    gfx_fill_circle(px - 4, py - 4, 3, rgb(255, 255, 255));
    gfx_fill_ellipse(cx - r / 3, cy - r / 2, r / 4, 3, rgb(255, 255, 230));
#endif
}

// the number in a unit, with the degree sign
static void value_text(int x, int y, int w, float deg, int unit, int scale, uint16_t c)
{
    char buf[16];
    unit_text(deg, unit, buf, sizeof(buf));
    int tw = gfx_text_width(buf, scale);
    int tx = x + (w - tw - (unit == U_DEG ? 3 * scale : 0)) / 2;
    gfx_text(tx, y, buf, scale, c);
    if (unit == U_DEG) {
        degree_sign(tx + tw + 2, y, scale, c);
    }
}

// a small arrow head: right = it points to the right (x is its back)
static void small_tri(int x, int y, bool right, uint16_t c)
{
    for (int i = 0; i < 6; i++) {
        int h = 12 - 2 * i;
        gfx_fill_rect(right ? x + i : x - i, y - h / 2, 1, h, c);
    }
}

// the reading of one axis, its unit and the side to raise
#if BOARD_UI_TABLET || BOARD_ROUND
// the Tab5 and the round panel: the same parts, bigger (the number 8 times, the texts 2 times)
static void readout(int x, int y, int w, float deg, bool level, bool big)
{
    int h = big ? 150 : 110;
    panel(x, y, w, h);
    gfx_text_center_in(x, w, y + 10, UNIT_NAMES[s_unit], 2, s_lcd_dim);
    value_text(x, y + 34, w, deg, s_unit, big ? 8 : 6, level ? s_green : s_lcd);
    int ty = y + (big ? 100 : 84);
    if (level) {
        gfx_text_center_in(x, w, ty, "LEVEL", 3, s_green);
    } else {
        bool right_low = deg > 0;
        bool plumb = s_pose == POSE_PLUMB;
        bool to_right = plumb ? !right_low : right_low;
        const char *t =
                plumb ? (right_low ? "MOVE TOP LEFT" : "MOVE TOP RIGHT") : (right_low ? "RAISE RIGHT" : "RAISE LEFT");
        gfx_text_center_in(x, w, ty + 4, t, 2, s_amber);
        int tw = gfx_text_width(t, 2);
        if (to_right) {
            small_tri(x + (w + tw) / 2 + 6, ty + 11, true, s_amber);
        } else {
            small_tri(x + (w - tw) / 2 - 6, ty + 11, false, s_amber);
        }
    }
    if (big) {
        bool steady = s_stab_t > 0.6f;
        gfx_fill_circle(x + 16, y + h - 16, 5, steady ? s_green : s_amber);
        gfx_text(x + 28, y + h - 23, s_hold ? "HOLD" : (steady ? "STEADY" : "WAIT"), 2, s_hold ? s_red : s_lcd_dim);
    }
}
#else
static void readout(int x, int y, int w, float deg, bool level, bool big)
{
    panel(x, y, w, big ? 98 : 72);
    gfx_text_center_in(x, w, y + 6, UNIT_NAMES[s_unit], 1, s_lcd_dim);
    value_text(x, y + 20, w, deg, s_unit, big ? 5 : 4, level ? s_green : s_lcd);
    int ty = y + (big ? 66 : 54);
    if (level) {
        gfx_text_center_in(x, w, ty, "LEVEL", 2, s_green);
    } else {
        // positive = the right end is lower: raise it. On an upright board that means the
        // top leans to the right: move the top to the left.
        bool right_low = deg > 0;
        bool plumb = s_pose == POSE_PLUMB;
        bool to_right = plumb ? !right_low : right_low;
        const char *t =
                plumb ? (right_low ? "MOVE TOP LEFT" : "MOVE TOP RIGHT") : (right_low ? "RAISE RIGHT" : "RAISE LEFT");
        gfx_text_center_in(x, w, ty + 4, t, 1, s_amber);
        int tw = gfx_text_width(t, 1);
        if (to_right) {
            small_tri(x + (w + tw) / 2 + 4, ty + 7, true, s_amber);
        } else {
            small_tri(x + (w - tw) / 2 - 4, ty + 7, false, s_amber);
        }
    }
    if (big) {
        bool steady = s_stab_t > 0.6f;
        gfx_fill_circle(x + 10, y + 88, 3, steady ? s_green : s_amber);
        gfx_text(x + 18, y + 85, s_hold ? "HOLD" : (steady ? "STEADY" : "WAIT"), 1, s_hold ? s_red : s_lcd_dim);
    }
}
#endif

// millimetre ticks along an edge (top = true: from the top down)
static void mm_ticks(int x0, int x1, int y, bool down, bool numbers)
{
    for (int i = 0;; i++) {
        int x = (int)lroundf(i * s_ppm);
        if (x > x1) {
            break;
        }
        if (x < x0) {
            continue;
        }
#if BOARD_UI_TABLET || BOARD_ROUND
        int len = i % 10 == 0 ? 32 : (i % 5 == 0 ? 22 : 13);
        gfx_fill_rect(x, down ? y : y - len, i % 10 == 0 ? 2 : 1, len, s_ink);
        if (numbers && i % 10 == 0 && i > 0) {
            char t[16];
            snprintf(t, sizeof(t), "%d", i / 10);
            gfx_text(x + 5, down ? y + 18 : y - 32, t, 2, s_ink);
        }
#else
        int len = i % 10 == 0 ? 18 : (i % 5 == 0 ? 12 : 7);
        gfx_fill_rect(x, down ? y : y - len, 1, len, s_ink);
        if (numbers && i % 10 == 0 && i > 0) {
            char t[16];
            snprintf(t, sizeof(t), "%d", i / 10);
            gfx_text(x + 3, down ? y + 10 : y - 17, t, 1, s_ink);
        }
#endif
    }
}

static void inch_ticks(int x0, int x1, int y)
{
    float pin = s_ppm * 25.4f / 16.0f;
    for (int i = 0;; i++) {
        int x = (int)lroundf(i * pin);
        if (x > x1) {
            break;
        }
        if (x < x0) {
            continue;
        }
#if BOARD_UI_TABLET || BOARD_ROUND
        int len = i % 16 == 0 ? 34 : (i % 8 == 0 ? 26 : (i % 4 == 0 ? 19 : (i % 2 == 0 ? 13 : 8)));
        gfx_fill_rect(x, y - len, i % 16 == 0 ? 2 : 1, len, s_ink);
        if (i % 16 == 0 && i > 0) {
            char t[16];
            snprintf(t, sizeof(t), "%d", i / 16);
            gfx_text(x + 5, y - 34, t, 2, s_ink);
        }
#else
        int len = i % 16 == 0 ? 20 : (i % 8 == 0 ? 15 : (i % 4 == 0 ? 11 : (i % 2 == 0 ? 8 : 5)));
        gfx_fill_rect(x, y - len, 1, len, s_ink);
        if (i % 16 == 0 && i > 0) {
            char t[16];
            snprintf(t, sizeof(t), "%d", i / 16);
            gfx_text(x + 3, y - 22, t, 1, s_ink);
        }
#endif
    }
}

static void draw_message(void)
{
    if (s_now >= s_msg_until) {
        return;
    }
#if BOARD_UI_TABLET || BOARD_ROUND
    int lim = BOARD_ROUND ? gfx_w * 82 / 100 : gfx_w - 8;   // (the round panel: in the wide middle band)
    int ms = gfx_text_width(s_msg, 2) + 24 <= lim ? 2 : 1;  // (a long text in portrait: the scale 1)
    int w = gfx_text_width(s_msg, ms) + 24;
    if (w > lim) {
        w = lim;
    }
#if BOARD_ROUND
    int x = (gfx_w - w) / 2, y = s_tab == TAB_RULER ? RU(312) : RU(178);
#else
    int x = (gfx_w - w) / 2, y = portrait_now() ? 204 : (s_tab == TAB_RULER ? 262 : 166);
#endif
    gfx_fill_rect(x, y, w, 30, rgb(30, 30, 36));
    gfx_frame_rect(x, y, w, 30, 2, s_amber);
    gfx_text_center_in(x, w, y + 15 - 7 * ms / 2, s_msg, ms, rgb(255, 255, 255));
#else
    int w = gfx_text_width(s_msg, 1) + 16;
    if (w > gfx_w - 8) {
        w = gfx_w - 8;
    }
#if BOARD_BAR_SCREEN
    int x = (gfx_w - w) / 2, y = portrait_now() ? 400 : 120;
#else
    // between the parts of the content (on the ruler: under the reading)
    int x = (gfx_w - w) / 2, y = portrait_now() ? 100 : (s_tab == TAB_RULER ? 152 : 106);
#endif
    gfx_fill_rect(x, y, w, 18, rgb(30, 30, 36));
    gfx_frame_rect(x, y, w, 18, 1, s_amber);
    gfx_text_center_in(x, w, y + 6, s_msg, 1, rgb(255, 255, 255));
#endif
}

// ---------------------------------------------------------------- the screens

static const float *shown(void)
{
    return s_hold ? s_hold_ang : s_ang;
}

static bool level_glow(void)
{
    const float *a = shown();
    float m = s_pose == POSE_FLAT ? sqrtf(a[0] * a[0] + a[1] * a[1]) : fabsf(a[0]);
    return m < LEVEL_ON || (s_level_now && m < LEVEL_OFF);
}

#if BOARD_BAR_SCREEN
static void draw_edge(void)
{
    body_landscape();
    bool lv = level_glow();
    readout(10, 14, 166, shown()[0], lv, true);
    // the vial window
    if (lv) {
        gfx_fill_rect(184, 18, 300, 92, s_green);
    }
    recess(188, 22, 292, 84);
    vial(334, 64, 262, 46, s_bub[0], s_bub_v[0], lv);
    // the scale and the name, printed on the metal
    mm_ticks(188, 480, 140, false, false);
    emboss(18, 132, "TFN PRO-LEVEL", 1);
    emboss(18, 146, "0.1 DEG", 1);
    screw(176, 150);
    screw(496, 150);
}

static void draw_flat(void)
{
    body_landscape();
    bool lv = level_glow();
    const float *a = shown();
    panel(10, 14, 166, 98);
    gfx_text_center_in(10, 166, 20, UNIT_NAMES[s_unit], 1, s_lcd_dim);
    char buf[16];
    for (int k = 0; k < 2; k++) {
        int y = 36 + k * 28;
        gfx_text(18, y, k ? "Y" : "X", 3, s_lcd_dim);
        char num[12];
        unit_text(a[k], s_unit, num, sizeof(num));
        snprintf(buf, sizeof(buf), "%s%s", a[k] <= -0.05f ? "-" : "", num);
        int tw = gfx_text_width(buf, 3);
        uint16_t c = lv ? s_green : s_lcd;
        gfx_text(160 - tw - (s_unit == U_DEG ? 10 : 0), y, buf, 3, c);
        if (s_unit == U_DEG) {
            degree_sign(152, y, 2, c);
        }
    }
    bool steady = s_stab_t > 0.6f;
    gfx_fill_circle(20, 102, 3, steady ? s_green : s_amber);
    gfx_text(28, 99, s_hold ? "HOLD" : (lv ? "LEVEL" : (steady ? "STEADY" : "WAIT")), 1,
             s_hold ? s_red : (lv ? s_green : s_lcd_dim));
    if (lv) {
        gfx_fill_circle(334, 66, 60, s_green);
    }
    bullseye(334, 66, 52, s_bub[0], s_bub[1], lv);
    emboss(200, 134, "BULLSEYE", 1);
    emboss(18, 132, "TFN PRO-LEVEL", 1);
    mm_ticks(400, 480, 160, false, false);
    screw(176, 150);
    screw(496, 150);
}

static void draw_plumb(void)
{
    body_portrait();
    bool lv = level_glow();
    gfx_text_center_in(0, 172, 60, "PLUMB", 3, rgb(120, 90, 16));
    gfx_text_center_in(0, 172, 92, "LONG SIDE UPRIGHT", 1, s_ink_soft);
    if (lv) {
        gfx_fill_rect(6, 142, 160, 96, s_green);
    }
    recess(10, 146, 152, 88);
    vial(86, 190, 136, 40, s_bub[0], s_bub_v[0], lv);
    readout(8, 250, 156, shown()[0], lv, true);
    // millimetres down the left edge
    for (int i = 0;; i++) {
        int y = 380 + (int)lroundf(i * s_ppm);
        if (y > 508) {
            break;
        }
        int len = i % 10 == 0 ? 18 : (i % 5 == 0 ? 12 : 7);
        gfx_fill_rect(6, y, len, 1, s_ink);
    }
    screw(150, 480);
    screw(150, 400);
}

static void draw_ruler(void)
{
    body_landscape();
    for (int y = 0; y < 172; y++) {
        gfx_fill_rect(0, y, 8, 1, s_row_land[y]);  // the zero end is open
    }
    // millimetres on the top edge, inches on the bottom edge
    mm_ticks(0, 639, 0, true, true);
    inch_ticks(0, 639, 172);
    gfx_text(12, 26, "CM", 1, s_ink_soft);
    gfx_text(12, 140, "IN", 1, s_ink_soft);
    // the reading
    float x = s_ruler_cal ? s_mark_x : s_ruler_x;
    float mm = x / s_ppm;
    panel(14, 50, 250, 72);
    char buf[32];
    if (s_ruler_cal) {
        gfx_text(22, 56, "SCALE: MARK AT 80 MM", 1, s_amber);
        snprintf(buf, sizeof(buf), "%.3f PX/MM", x / CAL_MM);
        gfx_text(22, 74, buf, 2, s_lcd);
        gfx_text(22, 100, "TAP DONE TO SAVE", 1, s_lcd_dim);
    } else {
        snprintf(buf, sizeof(buf), "%.1f", mm);
        gfx_text(22, 58, buf, 4, s_lcd);
        gfx_text(26 + gfx_text_width(buf, 4), 72, "MM", 2, s_lcd_dim);
        snprintf(buf, sizeof(buf), "%.2f CM   %.3f IN", mm / 10.0f, mm / 25.4f);
        gfx_text(22, 100, buf, 1, s_lcd_dim);
    }
    // the red line, with a mark at each edge
    int lx = (int)lroundf(x);
    gfx_fill_rect(lx - 1, 0, 2, 172, s_red);
    for (int i = 0; i < 6; i++) {
        gfx_fill_rect(lx - 6 + i, 36 + i, 12 - 2 * i, 1, s_red);
        gfx_fill_rect(lx - 6 + i, 135 - i, 12 - 2 * i, 1, s_red);
    }
}

#elif BOARD_UI_TABLET
// the Tab5 screens: the content in x 0 .. 440 (landscape) or over the two rows of buttons (portrait)
static void draw_edge(void)
{
    body_landscape();
    bool lv = level_glow();
    emboss(20, 16, "TFN PRO-LEVEL", 2);
    // the vial window
    if (lv) {
        gfx_fill_rect(10, 40, 430, 130, s_green);
    }
    recess(16, 46, 418, 118);
    vial(225, 105, 394, 84, s_bub[0], s_bub_v[0], lv);
    readout(14, 180, 424, shown()[0], lv, true);
    // the scale and the name, printed on the metal
    mm_ticks(16, 438, LAND_H - 1, false, false);
    emboss(330, 16, "0.1 DEG", 2);
}

static void draw_flat(void)
{
    body_landscape();
    bool lv = level_glow();
    const float *a = shown();
    emboss(20, 14, "BULLSEYE", 2);
    if (lv) {
        gfx_fill_circle(150, 196, 140, s_green);
    }
    bullseye(150, 196, 126, s_bub[0], s_bub[1], lv);
    panel(296, 40, 144, 300);
    gfx_text_center_in(296, 144, 50, UNIT_NAMES[s_unit], 2, s_lcd_dim);
    char buf[16];
    for (int k = 0; k < 2; k++) {
        int y = 82 + k * 104;
        gfx_text(308, y, k ? "Y" : "X", 3, s_lcd_dim);
        char num[12];
        unit_text(a[k], s_unit, num, sizeof(num));
        snprintf(buf, sizeof(buf), "%s%s", a[k] <= -0.05f ? "-" : "", num);
        int sc = gfx_text_width(buf, 4) <= 112 ? 4 : 3;
        int tw = gfx_text_width(buf, sc);
        uint16_t c = lv ? s_green : s_lcd;
        gfx_text(428 - tw - (s_unit == U_DEG ? 14 : 0), y + 34, buf, sc, c);
        if (s_unit == U_DEG) {
            degree_sign(418, y + 34, 3, c);
        }
    }
    bool steady = s_stab_t > 0.6f;
    gfx_fill_circle(312, 318, 5, steady ? s_green : s_amber);
    gfx_text(324, 311, s_hold ? "HOLD" : (lv ? "LEVEL" : (steady ? "STEADY" : "WAIT")), 2,
             s_hold ? s_red : (lv ? s_green : s_lcd_dim));
}

static void draw_plumb(void)
{
    body_portrait();
    bool lv = level_glow();
    gfx_text_center_in(0, 290, 24, "PLUMB", 5, rgb(120, 90, 16));
    gfx_text_center_in(0, 290, 72, "LONG SIDE UPRIGHT", 2, s_ink_soft);
    if (lv) {
        gfx_fill_rect(14, 98, 332, 112, s_green);
    }
    recess(20, 104, 320, 100);
    vial(180, 154, 296, 66, s_bub[0], s_bub_v[0], lv);
    readout(16, 222, 328, shown()[0], lv, true);
}

static void draw_ruler(void)
{
    body_landscape();
    for (int y = 0; y < LAND_H; y++) {
        gfx_fill_rect(0, y, 8, 1, s_row_land[y]);  // the zero end is open
    }
    // millimetres on the top edge, inches on the bottom edge
    mm_ticks(0, LAND_W - 1, 0, true, true);
    inch_ticks(0, LAND_W - 1, LAND_H);
    gfx_text(14, 58, "CM", 2, s_ink_soft);
    gfx_text(14, 288, "IN", 2, s_ink_soft);
    // the reading
    float x = s_ruler_cal ? s_mark_x : s_ruler_x;
    float mm = x / s_ppm;
    panel(16, 104, 420, 146);
    char buf[32];
    if (s_ruler_cal) {
        gfx_text(30, 118, "SCALE: MARK AT " CAL_MM_TEXT " MM", 2, s_amber);
        snprintf(buf, sizeof(buf), "%.3f PX/MM", x / CAL_MM);
        gfx_text(30, 152, buf, 4, s_lcd);
        gfx_text(30, 222, "TAP DONE TO SAVE", 2, s_lcd_dim);
    } else {
        snprintf(buf, sizeof(buf), "%.1f", mm);
        gfx_text(30, 120, buf, 8, s_lcd);
        gfx_text(40 + gfx_text_width(buf, 8), 148, "MM", 3, s_lcd_dim);
        snprintf(buf, sizeof(buf), "%.2f CM  %.3f IN", mm / 10.0f, mm / 25.4f);
        gfx_text(30, 222, buf, 2, s_lcd_dim);
    }
    // the red line, with a mark at each edge
    int lx = (int)lroundf(x);
    gfx_fill_rect(lx - 1, 0, 3, LAND_H, s_red);
    for (int i = 0; i < 10; i++) {
        gfx_fill_rect(lx - 10 + i, 64 + i, 21 - 2 * i, 1, s_red);
        gfx_fill_rect(lx - 10 + i, 295 - i, 21 - 2 * i, 1, s_red);
    }
}

#elif BOARD_ROUND
// THE ROUND PANEL: the vial across the middle, the reading under it
static void draw_edge(void)
{
    body_landscape();
    bool lv = level_glow();
    emboss(LAND_W / 2 - gfx_text_width("PRO-LEVEL", 2) / 2, RU(64), "PRO-LEVEL", 2);
    if (lv) {
        gfx_fill_rect(RU(50), RU(98), RU(366), RU(108), s_green);
    }
    recess(RU(56), RU(104), RU(354), RU(96));
    vial(RU(233), RU(152), RU(326), RU(58), s_bub[0], s_bub_v[0], lv);
    readout(RU(96), RU(214), RU(274), shown()[0], lv, true);
}

// the bullseye fills the middle of the circle; the X and Y readings under it
static void draw_flat(void)
{
    body_landscape();
    bool lv = level_glow();
    const float *a = shown();
    int cx = LAND_W / 2, cy = RU(194), r = RU(112);
    if (lv) {
        gfx_fill_circle(cx, cy, r + RU(12), s_green);
    }
    bullseye(cx, cy, r, s_bub[0], s_bub[1], lv);
    char buf[16];
    for (int k = 0; k < 2; k++) {
        int x0 = k ? LAND_W / 2 + RU(6) : RU(70), w = LAND_W / 2 - RU(76), y = RU(324);
        panel(x0, y, w, RU(42));
        char num[12];
        unit_text(a[k], s_unit, num, sizeof(num));
        snprintf(buf, sizeof(buf), "%s %s%s", k ? "Y" : "X", a[k] <= -0.05f ? "-" : "", num);
        uint16_t c = lv ? s_green : s_lcd;
        int tw = gfx_text_width(buf, 3), tx = x0 + (w - tw - (s_unit == U_DEG ? 9 : 0)) / 2;
        gfx_text(tx, y + RU(10), buf, 3, c);
        if (s_unit == U_DEG) {
            degree_sign(tx + tw + 2, y + RU(10), 2, c);
        }
    }
    bool steady = s_stab_t > 0.6f;
    const char *st = s_hold ? "HOLD" : (lv ? "LEVEL" : (steady ? "STEADY" : "WAIT"));
    int tw = gfx_text_width(st, 2);
    gfx_fill_circle(LAND_W / 2 - tw / 2 - 10, RU(74), 5, steady ? s_green : s_amber);
    gfx_text(LAND_W / 2 - tw / 2 + 2, RU(74) - 7, st, 2, s_hold ? s_red : (lv ? s_green : s_ink_soft));
}

static void draw_plumb(void)
{
    draw_edge();  // (no portrait frame on the round panel)
}

// the millimetres and the inches along the middle row (the zero at the left edge of the board)
static void draw_ruler(void)
{
    body_landscape();
    int ym = RU(212), yi = RU(312);
    mm_ticks(0, LAND_W - 1, ym, true, true);
    inch_ticks(0, LAND_W - 1, yi);
    gfx_text(RU(60), RU(256), "CM", 2, s_ink_soft);
    gfx_text(RU(60), RU(274), "IN", 2, s_ink_soft);
    float x = s_ruler_cal ? s_mark_x : s_ruler_x;
    float mm = x / s_ppm;
    int px = RU(70), pw = RU(240);
    panel(px, RU(74), pw, RU(104));
    char buf[32];
    if (s_ruler_cal) {
        gfx_text_center_in(px, pw, RU(84), "MARK AT " CAL_MM_TEXT " MM", 2, s_amber);
        snprintf(buf, sizeof(buf), "%.2f PX/MM", x / CAL_MM);
        gfx_text_center_in(px, pw, RU(112), buf, 3, s_lcd);
        gfx_text_center_in(px, pw, RU(150), "TAP DONE", 2, s_lcd_dim);
    } else {
        snprintf(buf, sizeof(buf), "%.1f", mm);
        int tw = gfx_text_width(buf, 6);
        gfx_text(px + (pw - tw - RU(30)) / 2, RU(84), buf, 6, s_lcd);
        gfx_text(px + (pw + tw - RU(30)) / 2 + 6, RU(108), "MM", 2, s_lcd_dim);
        snprintf(buf, sizeof(buf), "%.2f CM %.3f IN", mm / 10.0f, mm / 25.4f);
        gfx_text_center_in(px, pw, RU(146), buf, 2, s_lcd_dim);
    }
    int lx = (int)lroundf(x);
    gfx_fill_rect(lx - 1, 0, 3, LAND_H, s_red);
    for (int i = 0; i < 8; i++) {
        gfx_fill_rect(lx - 8 + i, ym + RU(40) + i, 16 - 2 * i, 1, s_red);
        gfx_fill_rect(lx - 8 + i, yi - RU(40) - i, 16 - 2 * i, 1, s_red);
    }
}

#else
static void draw_edge(void)
{
    body_landscape();
    bool lv = level_glow();
    emboss(12, 12, "TFN PRO-LEVEL", 1);
    // the vial window
    if (lv) {
        gfx_fill_rect(6, 28, 208, 80, s_green);
    }
    recess(10, 32, 200, 72);
    vial(110, 68, 184, 44, s_bub[0], s_bub_v[0], lv);
    readout(8, 114, 204, shown()[0], lv, true);
    // the scale and the name, printed on the metal
    mm_ticks(10, 214, 239, false, false);
    emboss(150, 12, "0.1 DEG", 1);
}

static void draw_flat(void)
{
    body_landscape();
    bool lv = level_glow();
    const float *a = shown();
    emboss(12, 8, "BULLSEYE", 1);
    if (lv) {
        gfx_fill_circle(110, 70, 58, s_green);
    }
    bullseye(110, 70, 50, s_bub[0], s_bub[1], lv);
    panel(8, 134, 204, 98);
    gfx_text_center_in(8, 204, 140, UNIT_NAMES[s_unit], 1, s_lcd_dim);
    char buf[16];
    for (int k = 0; k < 2; k++) {
        int y = 156 + k * 28;
        gfx_text(18, y, k ? "Y" : "X", 3, s_lcd_dim);
        char num[12];
        unit_text(a[k], s_unit, num, sizeof(num));
        snprintf(buf, sizeof(buf), "%s%s", a[k] <= -0.05f ? "-" : "", num);
        int tw = gfx_text_width(buf, 3);
        uint16_t c = lv ? s_green : s_lcd;
        gfx_text(196 - tw - (s_unit == U_DEG ? 10 : 0), y, buf, 3, c);
        if (s_unit == U_DEG) {
            degree_sign(188, y, 2, c);
        }
    }
    bool steady = s_stab_t > 0.6f;
    gfx_fill_circle(18, 222, 3, steady ? s_green : s_amber);
    gfx_text(26, 219, s_hold ? "HOLD" : (lv ? "LEVEL" : (steady ? "STEADY" : "WAIT")), 1,
             s_hold ? s_red : (lv ? s_green : s_lcd_dim));
}

static void draw_plumb(void)
{
    body_portrait();
    bool lv = level_glow();
    gfx_text_center_in(0, 186, 14, "PLUMB", 3, rgb(120, 90, 16));
    gfx_text_center_in(0, 186, 40, "LONG SIDE UPRIGHT", 1, s_ink_soft);
    if (lv) {
        gfx_fill_rect(10, 52, 220, 64, s_green);
    }
    recess(14, 56, 212, 56);
    vial(120, 84, 188, 38, s_bub[0], s_bub_v[0], lv);
    readout(10, 124, 220, shown()[0], lv, true);
}

static void draw_ruler(void)
{
    body_landscape();
    for (int y = 0; y < LAND_H; y++) {
        gfx_fill_rect(0, y, 8, 1, s_row_land[y]);  // the zero end is open
    }
    // millimetres on the top edge, inches on the bottom edge
    mm_ticks(0, LAND_W - 1, 0, true, true);
    inch_ticks(0, LAND_W - 1, LAND_H);
    gfx_text(12, 30, "CM", 1, s_ink_soft);
    gfx_text(12, 204, "IN", 1, s_ink_soft);
    // the reading
    float x = s_ruler_cal ? s_mark_x : s_ruler_x;
    float mm = x / s_ppm;
    panel(10, 70, 206, 76);
    char buf[32];
    if (s_ruler_cal) {
        gfx_text(18, 78, "SCALE: MARK AT " CAL_MM_TEXT " MM", 1, s_amber);
        snprintf(buf, sizeof(buf), "%.3f PX/MM", x / CAL_MM);
        gfx_text(18, 96, buf, 2, s_lcd);
        gfx_text(18, 124, "TAP DONE TO SAVE", 1, s_lcd_dim);
    } else {
        snprintf(buf, sizeof(buf), "%.1f", mm);
        gfx_text(18, 80, buf, 4, s_lcd);
        gfx_text(22 + gfx_text_width(buf, 4), 94, "MM", 2, s_lcd_dim);
        snprintf(buf, sizeof(buf), "%.2f CM  %.3f IN", mm / 10.0f, mm / 25.4f);
        gfx_text(18, 126, buf, 1, s_lcd_dim);
    }
    // the red line, with a mark at each edge
    int lx = (int)lroundf(x);
    gfx_fill_rect(lx - 1, 0, 2, LAND_H, s_red);
    for (int i = 0; i < 6; i++) {
        gfx_fill_rect(lx - 6 + i, 36 + i, 12 - 2 * i, 1, s_red);
        gfx_fill_rect(lx - 6 + i, 203 - i, 12 - 2 * i, 1, s_red);
    }
}

#endif

// the part of a half disc from angle 0 (right) to th degrees, as rows
static void sector(int cx, int cy, int r, float th, uint16_t c)
{
    if (th <= 0) {
        return;
    }
    float ct = th >= 179.9f ? -1e9f : fcos(th * DEG2RAD) / (fsin(th * DEG2RAD) + 1e-6f);
    for (int h = 1; h <= r; h++) {
        int hw = (int)sqrtf((float)(r * r - h * h));
        int x0 = th >= 179.9f ? cx - hw : (int)(cx + h * ct);
        if (x0 < cx - hw) {
            x0 = cx - hw;
        }
        int x1 = cx + hw;
        if (x1 > x0) {
            gfx_fill_rect(x0, cy - h, x1 - x0, 1, c);
        }
    }
}

#if BOARD_BAR_SCREEN
static void draw_angle(void)
{
    body_landscape();
    float a = s_rel;
    float th = fabsf(a);
    // the protractor: a half disc, the turned part, the scale and the needle
    int cx = 334, cy = 162, R = 140;
    for (int h = 0; h <= R + 3; h++) {
        int hw = (int)sqrtf((float)((R + 3) * (R + 3) - h * h));
        gfx_fill_rect(cx - hw, cy - h, 2 * hw, 1, rgb(110, 82, 14));
    }
    for (int h = 0; h <= R; h++) {
        int hw = (int)sqrtf((float)(R * R - h * h));
        gfx_fill_rect(cx - hw, cy - h, 2 * hw, 1, rgb(250, 246, 228));
    }
    sector(cx, cy, R - 1, th, rgb(170, 220, 255));
    for (int d = 0; d <= 180; d += 5) {
        float c = fcos(d * DEG2RAD), s = fsin(d * DEG2RAD);
        int len = d % 30 == 0 ? 16 : (d % 10 == 0 ? 10 : 6);
        seg(cx + c * R, cy - s * R, cx + c * (R - len), cy - s * (R - len), d % 30 == 0 ? 2 : 1, s_ink);
        if (d % 30 == 0) {
            char t[16];
            snprintf(t, sizeof(t), "%d", d);
            int tw = gfx_text_width(t, 1);
            int ly = (int)(cy - s * (R - 28)) - 3;
            if (d == 0 || d == 180) {
                ly -= 9;
            }
            gfx_text((int)(cx + c * (R - 28)) - tw / 2, ly, t, 1, s_ink);
        }
    }
    float c = fcos(th * DEG2RAD), s = fsin(th * DEG2RAD);
    seg((float)cx, (float)cy, cx + R - 4, (float)cy, 2, rgb(40, 90, 200));
    seg((float)cx, (float)cy, cx + c * (R - 4), cy - s * (R - 4), 3, s_red);
    gfx_fill_circle(cx, cy, 7, rgb(60, 60, 66));
    gfx_fill_circle(cx, cy, 3, rgb(200, 200, 210));
    // the reading
    panel(10, 14, 166, 98);
    gfx_text_center_in(10, 166, 20, s_zero_set ? "FROM YOUR ZERO" : "FROM LEVEL EDGE", 1, s_lcd_dim);
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%.1f", a < -0.05f ? "-" : "", th);
    int tw = gfx_text_width(buf, 4);
    gfx_text(93 - tw / 2 - 6, 40, buf, 4, s_lcd);
    degree_sign(93 + tw / 2 - 4, 40, 4, s_lcd);
    snprintf(buf, sizeof(buf), "180 - A = %.1f", 180.0f - th);
    gfx_text_center_in(10, 166, 82, buf, 1, s_lcd_dim);
    gfx_text_center_in(10, 166, 98, s_hold ? "HOLD" : "TAP ZERO ON SURFACE 1", 1, s_hold ? s_red : s_lcd_dim);
}

#elif BOARD_UI_TABLET
static void draw_angle(void)
{
    body_landscape();
    float a = s_rel;
    float th = fabsf(a);
    // the protractor: a half disc, the turned part, the scale and the needle
    int cx = 222, cy = 352, R = 196;
    for (int h = 0; h <= R + 4; h++) {
        int hw = (int)sqrtf((float)((R + 4) * (R + 4) - h * h));
        gfx_fill_rect(cx - hw, cy - h, 2 * hw, 1, rgb(110, 82, 14));
    }
    for (int h = 0; h <= R; h++) {
        int hw = (int)sqrtf((float)(R * R - h * h));
        gfx_fill_rect(cx - hw, cy - h, 2 * hw, 1, rgb(250, 246, 228));
    }
    sector(cx, cy, R - 1, th, rgb(170, 220, 255));
    for (int d = 0; d <= 180; d += 5) {
        float c = fcos(d * DEG2RAD), s = fsin(d * DEG2RAD);
        int len = d % 30 == 0 ? 26 : (d % 10 == 0 ? 17 : 9);
        seg(cx + c * R, cy - s * R, cx + c * (R - len), cy - s * (R - len), d % 30 == 0 ? 3 : 2, s_ink);
        if (d % 30 == 0) {
            char t[16];
            snprintf(t, sizeof(t), "%d", d);
            int tw = gfx_text_width(t, 2);
            int ly = (int)(cy - s * (R - 46)) - 7;
            if (d == 0 || d == 180) {
                ly -= 18;
            }
            gfx_text((int)(cx + c * (R - 46)) - tw / 2, ly, t, 2, s_ink);
        }
    }
    float c = fcos(th * DEG2RAD), s = fsin(th * DEG2RAD);
    seg((float)cx, (float)cy, cx + R - 6, (float)cy, 3, rgb(40, 90, 200));
    seg((float)cx, (float)cy, cx + c * (R - 6), cy - s * (R - 6), 5, s_red);
    gfx_fill_circle(cx, cy, 12, rgb(60, 60, 66));
    gfx_fill_circle(cx, cy, 5, rgb(200, 200, 210));
    // the reading
    panel(16, 10, 420, 128);
    gfx_text_center_in(16, 420, 18, s_zero_set ? "FROM YOUR ZERO" : "FROM LEVEL EDGE", 2, s_lcd_dim);
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%.1f", a < -0.05f ? "-" : "", th);
    int tw = gfx_text_width(buf, 6);
    gfx_text(cx - tw / 2 - 10, 40, buf, 6, s_lcd);
    degree_sign(cx + tw / 2 - 6, 40, 6, s_lcd);
    snprintf(buf, sizeof(buf), "180 - A = %.1f", 180.0f - th);
    gfx_text_center_in(16, 420, 92, buf, 2, s_lcd_dim);
    gfx_text_center_in(16, 420, 114, s_hold ? "HOLD" : "TAP ZERO ON SURFACE 1", 2, s_hold ? s_red : s_lcd_dim);
}

#elif BOARD_ROUND
// THE ROUND PANEL: the protractor across the middle, the angle in the top cap, the help under it
static void draw_angle(void)
{
    body_landscape();
    float a = s_rel;
    float th = fabsf(a);
    int cx = LAND_W / 2, cy = RU(318), R = RU(160);
    for (int h = 0; h <= R + 4; h++) {
        int hw = (int)sqrtf((float)((R + 4) * (R + 4) - h * h));
        gfx_fill_rect(cx - hw, cy - h, 2 * hw, 1, rgb(110, 82, 14));
    }
    for (int h = 0; h <= R; h++) {
        int hw = (int)sqrtf((float)(R * R - h * h));
        gfx_fill_rect(cx - hw, cy - h, 2 * hw, 1, rgb(250, 246, 228));
    }
    sector(cx, cy, R - 1, th, rgb(170, 220, 255));
    for (int d = 0; d <= 180; d += 5) {
        float c = fcos(d * DEG2RAD), s = fsin(d * DEG2RAD);
        int len = d % 30 == 0 ? 20 : (d % 10 == 0 ? 13 : 7);
        seg(cx + c * R, cy - s * R, cx + c * (R - len), cy - s * (R - len), d % 30 == 0 ? 3 : 1, s_ink);
        if (d % 30 == 0) {
            char t[16];
            snprintf(t, sizeof(t), "%d", d);
            int tw = gfx_text_width(t, 2);
            int ly = (int)(cy - s * (R - 38)) - 7;
            if (d == 0 || d == 180) {
                ly -= 16;
            }
            gfx_text((int)(cx + c * (R - 38)) - tw / 2, ly, t, 2, s_ink);
        }
    }
    float c = fcos(th * DEG2RAD), s = fsin(th * DEG2RAD);
    seg((float)cx, (float)cy, cx + R - 6, (float)cy, 3, rgb(40, 90, 200));
    seg((float)cx, (float)cy, cx + c * (R - 6), cy - s * (R - 6), 4, s_red);
    gfx_fill_circle(cx, cy, 10, rgb(60, 60, 66));
    gfx_fill_circle(cx, cy, 4, rgb(200, 200, 210));
    // the reading in the top cap
    int px = RU(100), pw = RU(266);
    panel(px, RU(40), pw, RU(110));
    gfx_text_center_in(px, pw, RU(48), s_zero_set ? "FROM YOUR ZERO" : "FROM LEVEL EDGE", 2, s_lcd_dim);
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%.1f", a < -0.05f ? "-" : "", th);
    int tw = gfx_text_width(buf, 7);
    gfx_text(LAND_W / 2 - tw / 2 - 8, RU(76), buf, 7, s_lcd);
    degree_sign(LAND_W / 2 + tw / 2 - 4, RU(76), 6, s_lcd);
    snprintf(buf, sizeof(buf), "180 - A = %.1f", 180.0f - th);
    gfx_text_center_in(0, LAND_W, RU(330), buf, 2, s_ink_soft);
    gfx_text_center_in(0, LAND_W, RU(354), s_hold ? "HOLD" : "TAP ZERO ON SURFACE 1", 2, s_hold ? s_red : s_ink_soft);
}

#else
static void draw_angle(void)
{
    body_landscape();
    float a = s_rel;
    float th = fabsf(a);
    // the protractor: a half disc, the turned part, the scale and the needle
    int cx = 110, cy = 234, R = 100;
    for (int h = 0; h <= R + 3; h++) {
        int hw = (int)sqrtf((float)((R + 3) * (R + 3) - h * h));
        gfx_fill_rect(cx - hw, cy - h, 2 * hw, 1, rgb(110, 82, 14));
    }
    for (int h = 0; h <= R; h++) {
        int hw = (int)sqrtf((float)(R * R - h * h));
        gfx_fill_rect(cx - hw, cy - h, 2 * hw, 1, rgb(250, 246, 228));
    }
    sector(cx, cy, R - 1, th, rgb(170, 220, 255));
    for (int d = 0; d <= 180; d += 5) {
        float c = fcos(d * DEG2RAD), s = fsin(d * DEG2RAD);
        int len = d % 30 == 0 ? 14 : (d % 10 == 0 ? 9 : 5);
        seg(cx + c * R, cy - s * R, cx + c * (R - len), cy - s * (R - len), d % 30 == 0 ? 2 : 1, s_ink);
        if (d % 30 == 0) {
            char t[16];
            snprintf(t, sizeof(t), "%d", d);
            int tw = gfx_text_width(t, 1);
            int ly = (int)(cy - s * (R - 24)) - 3;
            if (d == 0 || d == 180) {
                ly -= 9;
            }
            gfx_text((int)(cx + c * (R - 24)) - tw / 2, ly, t, 1, s_ink);
        }
    }
    float c = fcos(th * DEG2RAD), s = fsin(th * DEG2RAD);
    seg((float)cx, (float)cy, cx + R - 4, (float)cy, 2, rgb(40, 90, 200));
    seg((float)cx, (float)cy, cx + c * (R - 4), cy - s * (R - 4), 3, s_red);
    gfx_fill_circle(cx, cy, 7, rgb(60, 60, 66));
    gfx_fill_circle(cx, cy, 3, rgb(200, 200, 210));
    // the reading
    panel(8, 8, 204, 106);
    gfx_text_center_in(8, 204, 14, s_zero_set ? "FROM YOUR ZERO" : "FROM LEVEL EDGE", 1, s_lcd_dim);
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%.1f", a < -0.05f ? "-" : "", th);
    int tw = gfx_text_width(buf, 5);
    gfx_text(110 - tw / 2 - 6, 30, buf, 5, s_lcd);
    degree_sign(110 + tw / 2 - 4, 30, 5, s_lcd);
    snprintf(buf, sizeof(buf), "180 - A = %.1f", 180.0f - th);
    gfx_text_center_in(8, 204, 78, buf, 1, s_lcd_dim);
    gfx_text_center_in(8, 204, 96, s_hold ? "HOLD" : "TAP ZERO ON SURFACE 1", 1, s_hold ? s_red : s_lcd_dim);
}

#endif

static void level_render(int64_t now)
{
    s_now = now;
    if (!imu_ok() || !s_g_init) {
        if (portrait_now()) {
            body_portrait();
        } else {
            body_landscape();
        }
#if BOARD_BAR_SCREEN
        gfx_text_center(70, imu_ok() ? "READING THE SENSOR" : "NO MOTION SENSOR", 2, s_ink);
#elif BOARD_UI_TABLET
        gfx_text_center_in(0, portrait_now() ? PORT_W : COL_X - 8, 160,
                           imu_ok() ? "READING THE SENSOR" : "NO MOTION SENSOR", 2, s_ink);
#elif BOARD_ROUND
        gfx_text_center(RU(200), imu_ok() ? "READING THE SENSOR" : "NO MOTION SENSOR", 2, s_ink);
#else
        gfx_text_center_in(0, portrait_now() ? PORT_W : 220, 100, imu_ok() ? "READING THE SENSOR" : "NO MOTION SENSOR",
                           1, s_ink);
#endif
        draw_buttons();
        return;
    }
    switch (s_tab) {
    case TAB_RULER:
        if (portrait_now()) {
            body_portrait();
        } else {
            draw_ruler();
        }
        break;
    case TAB_ANGLE:
        if (portrait_now()) {
            body_portrait();
        } else {
            draw_angle();
        }
        break;
    default:
        if (portrait_now()) {
            draw_plumb();
        } else if (s_pose == POSE_FLAT) {
            draw_flat();
        } else {
            draw_edge();
        }
        break;
    }
    draw_buttons();
    draw_message();
}

#include "tficon/tficon.h"
// the menu icon: a spirit level; its bubble slides from side to side and rests in the middle, where the marks light
static void level_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x3df08a), tfi_hex(0x0b7a3a));
    float u = s / 100.0f, ph = tfi_ph(2);
    int lx = x + (int)(12 * u), ly = y + (int)(36 * u), lw = (int)(76 * u), lh = (int)(30 * u);
    tfi_drop(lx, ly, lw, lh, (int)(6 * u), (int)(4 * u), 80);
    tfi_gloss_box(lx, ly, lw, lh, (int)(6 * u), tfi_hex(0xffe066), tfi_hex(0xf5a300));
    // the vial
    int vx = x + (int)(26 * u), vy = ly + (int)(7 * u), vw = (int)(48 * u), vh = (int)(16 * u);
    tfi_rbox(vx, vy, vw, vh, vh / 2, tfi_hex(0x2d6a1e), tfi_hex(0x1a4a10), 256);
    tfi_rbox(vx + (int)(1.5f * u), vy + (int)(1.5f * u), vw - (int)(3 * u), vh - (int)(3 * u), vh / 2,
             tfi_hex(0xe8ff9a), tfi_hex(0x9ee03a), 256);
    // the bubble: x = sin^3, so it rests near the middle
    float sw = fsin(ph * 6.2831853f), bx = sw * sw * sw * 13 * u, ok = 1 - tfi_step(1.5f, 4, (bx < 0 ? -bx : bx) / u);
    float cx = x + 50 * u + bx, cy = vy + vh / 2.0f;
    tfi_rbox((int)(cx - 8 * u), (int)(cy - 5 * u), (int)(16 * u), (int)(10 * u), (int)(5 * u), rgb(255, 255, 255),
             tfi_hex(0xd8f8b0), 256);
    tfd_disc(cx - 3 * u, cy - 2 * u, 1.8f * u, rgb(255, 255, 255), 255);
    // the marks
    uint16_t mk = tfd_mix(tfi_hex(0x2d6a1e), tfi_hex(0x00c853), (int)(256 * ok));
    int mw = (int)(2 * u) > 1 ? (int)(2 * u) : 1;
    tfd_rect(x + (int)(40 * u), vy - (int)(2 * u), mw, vh + (int)(4 * u), mk, 256);
    tfd_rect(x + (int)(59 * u), vy - (int)(2 * u), mw, vh + (int)(4 * u), mk, 256);
    // the scale on the bar
    for (int k = 0; k < 9; k++) {
        tfd_rect(lx + (int)((8 + k * 7.5f) * u), ly + lh - (int)((k & 1 ? 4 : 6) * u), mw, (int)((k & 1 ? 3 : 5) * u),
                 tfi_hex(0x8a5a00), 200);
    }
    tfi_end();
}

// a level often lies still with no touch: the screen must stay on
static bool level_awake(void)
{
    return true;
}

const app_t app_level = {
        .name = "LEVEL",
        .landscape = true,
        .needs = APP_NEEDS_IMU,
        .init = level_init,
        .enter = level_enter,
        .input = level_input,
        .update = level_update,
        .render = level_render,
        .draw_icon = level_icon,
        .awake = level_awake,
        .round = APP_ROUND_HI,
};
