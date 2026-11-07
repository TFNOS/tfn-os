// GAMEPAD: the board as a Bluetooth LE game controller (bt_pad.c), landscape (640 x 172).
// A profile (pad_config.c, 5 profiles, the web page /pad edits them) sets the mode:
//  - RETRO: hold the board in the air and tilt it to move (4 or 8 directions, to the
//    hat and to the left stick). A touch anywhere = FIRE (with auto fire if set).
//  - RETRO MOVE: the same, but the direction comes from the MOTION of the board, not its
//    angle: move it left, right, forward or back (pad_move.h). HOLD: the direction is down
//    while the board moves; LATCH: it stays down until the next move (a lift lets it go).
//  - BUTTONS: a d-pad at the left, A B X Y at the right, SELECT and START in the middle.
//    Two fingers at once work when the panel gives 2 points.
//  - MOTION: tilt = the analog left stick. The screen = two buttons (the halves), one
//    button, or a touchpad that moves the right stick.
// In all modes, gestures can press buttons: a jerk up / down / left / right, a knock on
// the back, a hard shake (4 hits in 0.8 s) and a snap (a fast move that stops).
// Top strip: CENTER (the neutral pose), < profile >, the link state. A long touch on the
// top strip forgets the paired computers (after a YES).
// Buttons: A = 1, B = 2, X = 3, Y = 4, SELECT = 9, START = 10.
// CoreS3 (320 x 240): the same controls with a smaller layout, and the link help at the
// bottom. A board with no IMU (CoreS3-SE) runs each profile in the BUTTONS mode.
// Tab5 (640 x 360): the wide layout of the 3.49 with more height, bigger controls and 2 times bigger text.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "uikit.h"
#include "tfn_util.h"
#include "app.h"
#include "audio.h"
#include "board.h"
#include "bt_pad.h"
#include "gfx.h"
#include "imu.h"
#include "input.h"
#include "pad_config.h"
#include "pad_move.h"
#include "settings.h"
#include "wifi.h"
#include "esp_log.h"

static const char *TAG = "pad";

#if BOARD_BAR_SCREEN
#define SCR_W     640
#define SCR_H     172
#define TOP_H     36   // (the thumb, round 3: the profile halves are 152 x 36 px)
#define CENTER_W  96   // the CENTER button at the top left
#define PREV_X    150  // < profile >
#define NEXT_X    450
#define ARROW_W   44
#define WEB_Y     161
#define DPAD_X    104
#define DPAD_Y    98
#define DPAD_ARM  56  // from the centre to the end of an arm
#define DPAD_W    36
#define DPAD_ZONE 210  // touches at x < DPAD_ZONE are on the d-pad
#define ABXY_X    536
#define ABXY_Y    98
#define ABXY_OFF  36
#define ABXY_R    21
#define SEL_X     272
#define STA_X     322
#define SS_Y      124
#define SS_W      46
#define SS_H      30  // (a hit of 42 px)
#define BODY_RX   98  // the body of the pad (BUTTONS mode)
#define BODY_RY   70
#define BODY_Y    40
#elif BOARD_UI_TABLET
// The Tab5 (640 x 360): the wide layout of the 3.49, with more height. The controls are bigger (a thumb is about
// 58 px here), the text is 2 times bigger, and the link help is a line at the bottom.
#define SCR_W     640
#define SCR_H     360
#define TOP_H     56
#define TOP_TS    2  // the text scale of the top strip
#define CENTER_W  100
#define PREV_X    150
#define NEXT_X    450
#define ARROW_W   52
#define WEB_Y     345
#define DPAD_X    140
#define DPAD_Y    178
#define DPAD_ARM  92
#define DPAD_W    58
#define DPAD_ZONE 250
#define ABXY_X    508
#define ABXY_Y    178
#define ABXY_OFF  60
#define ABXY_R    32
#define BTN_HIT   62  // a touch this near to A B X Y presses the nearest one
#define SEL_X     258
#define STA_X     342
#define SS_Y      96
#define SS_W      80
#define SS_H      44
#define BODY_RX   150
#define BODY_RY   98
#define BODY_Y    76
#define BODY_H    190
#define HELP_Y    284
#define HELP_S    2  // the text scale of the link help
#else
// CoreS3 (320 x 240)
#define SCR_W     320
#define SCR_H     240
#define TOP_H     40
#define CENTER_W  64
#define PREV_X    64
#define NEXT_X    250
#define ARROW_W   40
#define WEB_Y     230
#define DPAD_X    62
#define DPAD_Y    128
#define DPAD_ARM  52
#define DPAD_W    36
#define DPAD_ZONE 114
#define ABXY_X    262
#define ABXY_Y    128
#define ABXY_OFF  36
#define ABXY_R    20
#define SEL_X     116
#define STA_X     160
#define SS_Y      86
#define SS_W      40
#define SS_H      30  // (a hit of 42 px = 4.7 mm on the AMOLED)
#define BODY_RX   60
#define BODY_RY   52
#define BODY_Y    78
#define HELP_Y    192  // the link help: two or three lines at the bottom
#endif
// The pull-down panel (all at the fully open place; the panel slides down from the top)
#if BOARD_BAR_SCREEN
#define DR_SEL_X   16  // SELECT and START: big, held = the button is down
#define DR_STA_X   192
#define DR_SS_Y    34
#define DR_SS_W    160
#define DR_SS_H    84
#define DR_CAL_X   372  // CALIBRATE
#define DR_CAL_Y   34
#define DR_CAL_W   252
#define DR_CAL_H   44
#define DR_PRO_X   372  // < profile >
#define DR_PRO_Y   82
#define DR_PRO_W   252
#define DR_PRO_H   44
#define DR_CLOSE_Y 132  // the CLOSE bar at the bottom of the panel
#elif BOARD_UI_TABLET
#define DR_SEL_X   20
#define DR_STA_X   214
#define DR_SS_Y    64
#define DR_SS_W    180
#define DR_SS_H    150
#define DR_CAL_X   414
#define DR_CAL_Y   64
#define DR_CAL_W   206
#define DR_CAL_H   68
#define DR_PRO_X   414
#define DR_PRO_Y   146
#define DR_PRO_W   206
#define DR_PRO_H   68
#define DR_CLOSE_Y 262
#else
#define DR_SEL_X   10
#define DR_STA_X   166
#define DR_SS_Y    40
#define DR_SS_W    144
#define DR_SS_H    84
#define DR_CAL_X   10
#define DR_CAL_Y   132
#define DR_CAL_W   144
#define DR_CAL_H   44
#define DR_PRO_X   166
#define DR_PRO_Y   132
#define DR_PRO_W   144
#define DR_PRO_H   44
#define DR_CLOSE_Y 186
#endif
// (the values of the 3.49 and the CoreS3, when the layout above does not set them)
#ifndef TOP_TS
#define TOP_TS 1
#endif
#ifndef BTN_HIT
#define BTN_HIT 46
#endif
#ifndef BODY_H
#define BODY_H 108
#endif
#ifndef HELP_S
#define HELP_S 1
#endif
#define DR_PULL_PX 12     // a drag of this much from the top strip pulls the panel
#define HYST_DEG   8.0f   // the old direction stays this far past a border
#define GEST_MS    140    // a gesture holds its button down this long
#define SHAKE_WIN  800    // ms: SHAKE_HITS hits in this time = a hard shake
#define SHAKE_HITS 4      // (a jerk and the stop at its end are already 2 hits)
#define SHAKE_HOLD 400    // ms: the shake button stays down after the last hit
#define REBOUND_MS 300    // ms: a hit opposite to the last jerk is the stop of that jerk
#define PAD_TP_R   60.0f  // touchpad: this far from the start point = full stick
#define TAP_MS     250

enum {
    BTN_A = 1,
    BTN_B = 2,
    BTN_X = 3,
    BTN_Y = 4,
    BTN_SELECT = 9,
    BTN_START = 10
};

static const int8_t DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int8_t DY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

static pad_profile_t s_p;  // a copy of the active profile
static uint32_t s_ver;
static int s_frames;
static bool s_start_req;
static int64_t s_now;

static pad_report_t s_rep;
static uint16_t s_screen, s_prev_screen;  // buttons from the screen (for the clicks)
static int s_dir = -1;                    // the direction now (hat 0..7), -1 = none
static bool s_tilt_on;                    // RETRO: the tilt is past the dead zone
static float s_sx, s_sy;                  // the left stick, -1..1 (for the picture)
static float s_rx, s_ry;                  // the right stick, -1..1
static float s_deg_x, s_deg_y;            // the tilt in degrees

static int64_t s_gest_until[17];
static int64_t s_hits[8];
static int s_nhits;
static int64_t s_shake_until;
static int s_last_jerk = -1;  // 0 up, 1 down, 2 left, 3 right
static int64_t s_last_jerk_at;

static int s_npt;  // touch points now (logical, play area only)
static int16_t s_ptx[2], s_pty[2];
static bool s_tp_on, s_tp_moved;  // touchpad
static int s_tp_x0, s_tp_y0;
static int64_t s_tp_t0;
static bool s_fire;  // RETRO: FIRE is held
static int64_t s_fire_t0;
static pad_move_t s_mv;   // RETRO MOVE: the detector
static int s_prev_npt;    // RETRO MOVE: a change of the touches = a jerk
static float s_jx, s_jy;  // the joystick picture (RETRO, RETRO MOVE)

static bool s_confirm;  // the "forget pairings" question is open
// the pull-down panel: 0 = closed, 1 = open
static float s_drawer, s_drawer_to;
static bool s_pulling;  // a finger pulls the panel now
static int s_pull_y0;
static int64_t s_drawer_at;
static char s_pop[28];
static int64_t s_pop_at;
static uint16_t s_pop_c;

static uint16_t c_text, c_dim, c_accent;

// ---------------------------------------------------------------- helpers

static void pop(const char *t, uint16_t c)
{
    snprintf(s_pop, sizeof(s_pop), "%s", t);
    s_pop_at = s_now;
    s_pop_c = c;
}

// The direction (hat value: 0 = up, clockwise to 7 = up-left) of the vector (x right,
// y down), with 4 or 8 directions. Near a border the old direction stays.
static int dir_of(float x, float y, int dirs, int old)
{
    return pm_dir_of(x, y, dirs, old, HYST_DEG);
}

// degrees of tilt from a tilt value in g
static float tilt_deg(float t)
{
    return asinf(tfn_clampf(t, -1, 1)) * 57.29578f;
}

// an analog axis from a tilt in degrees: dead zone, then full at the range
static float axis(float deg)
{
    float a = fabsf(deg);
    if (a <= s_p.dead) {
        return 0;
    }
    float v = (a - s_p.dead) / (float)(s_p.range - s_p.dead);
    v = tfn_clampf(v, 0, 1);
    return deg < 0 ? -v : v;
}

static int8_t to_axis(float v)
{
    return (int8_t)lroundf(tfn_clampf(v, -1, 1) * 127.0f);
}

static uint16_t bit(int b)
{
    return (b >= 1 && b <= 16) ? (uint16_t)(1u << (b - 1)) : 0;
}

// The mode that runs: a board with no IMU runs the tilt modes as BUTTONS.
static bool no_tilt(void)
{
    return s_p.mode != PAD_MODE_BUTTONS && !(app_board_caps() & APP_NEEDS_IMU);
}

static int mode_now(void)
{
    return no_tilt() ? PAD_MODE_BUTTONS : s_p.mode;
}

static void load_profile(void)
{
    s_p = *pad_profile(pad_active());
    s_ver = pad_config_version();
    s_dir = -1;
    s_tilt_on = false;
    s_tp_on = false;
    memset(s_gest_until, 0, sizeof(s_gest_until));
    pad_move_reset(&s_mv);
}

// ---------------------------------------------------------------- the screen controls

// The d-pad direction of a vector (x right, y down). Round 3, the thumb: with 8 directions the 4 main directions have
// sectors of 60 degrees and the diagonals sectors of 30 degrees, so that a thumb that aims at an arm seldom gets a
// diagonal; the old direction stays HYST_DEG past its border. With 4 directions: sectors of 90 degrees (dir_of).
static int dpad_dir(float x, float y, int dirs, int old)
{
    if (dirs == 4) {
        return dir_of(x, y, dirs, old);
    }
    float a = atan2f(x, -y) * 57.29578f;  // 0 = up, 90 = right
    if (a < 0) {
        a += 360.0f;
    }
    int card = (int)floorf((a + 45.0f) / 90.0f) % 4;  // the nearest main direction
    float dc = fabsf(fmodf(a - card * 90.0f + 540.0f, 360.0f) - 180.0f);
    int d = dc <= 30.0f ? card * 2 : ((int)floorf(a / 90.0f) % 4) * 2 + 1;
    if (old >= 0 && old < 8) {
        float half = (old & 1) ? 15.0f : 30.0f;
        float diff = fabsf(fmodf(a - old * 45.0f + 540.0f, 360.0f) - 180.0f);
        if (diff < half + HYST_DEG) {
            d = old;
        }
    }
    return d;
}

// the d-pad direction of a touch (-1 = the centre, -2 = not on the d-pad)
static int dpad_at(int x, int y)
{
    if (x >= DPAD_ZONE) {
        return -2;
    }
    float dx = (float)(x - DPAD_X), dy = (float)(y - DPAD_Y);
    if (dx * dx + dy * dy < 14 * 14) {
        return -1;
    }
    return dpad_dir(dx, dy, s_p.dirs, s_dir);
}

// A B X Y, SELECT, START of a touch (button number, 0 = none)
static int button_at(int x, int y)
{
    if (y >= SS_Y - 6 && y < SS_Y + SS_H + 6) {
        // (a dead band of 6 px between the two: a thumb on the gap presses neither, never both)
        int mid = (SEL_X + SS_W + STA_X) / 2;
        if (x >= SEL_X - 6 && x < mid - 3) {
            return BTN_SELECT;
        }
        if (x >= mid + 3 && x < STA_X + SS_W + 6) {
            return BTN_START;
        }
        if (x >= mid - 3 && x < mid + 3) {
            return 0;
        }
    }
    static const int16_t BX[4] = {ABXY_X, ABXY_X + ABXY_OFF, ABXY_X - ABXY_OFF, ABXY_X};
    static const int16_t BY[4] = {ABXY_Y + ABXY_OFF, ABXY_Y, ABXY_Y, ABXY_Y - ABXY_OFF};
    static const uint8_t BN[4] = {BTN_A, BTN_B, BTN_X, BTN_Y};
    int best = 0, bd = BTN_HIT * BTN_HIT;
    for (int i = 0; i < 4; i++) {
        int dx = x - BX[i], dy = y - BY[i];
        if (dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = BN[i];
        }
    }
    return best;
}

// the touch points of the play area, in logical coordinates
static void read_points(void)
{
    int16_t px[2], py[2];
    int n = input_points(px, py, 2);
    s_npt = 0;
    for (int i = 0; i < n && i < 2; i++) {
        int x, y;
        gfx_touch_to_logical(px[i], py[i], &x, &y);
        if (y < TOP_H) {
            continue;  // the top strip is for the menu
        }
        s_ptx[s_npt] = (int16_t)x;
        s_pty[s_npt] = (int16_t)y;
        s_npt++;
    }
}

// ---------------------------------------------------------------- gestures

static void gesture(int b, const char *name)
{
    if (b < 1 || b > 16) {
        return;
    }
    s_gest_until[b] = s_now + GEST_MS;
    char t[28];
    snprintf(t, sizeof(t), "%s: B%d", name, b);
    pop(t, hsv(b * 37, 160, 255));
}

static void read_gestures(void)
{
    bool move = mode_now() == PAD_MODE_MOVE;
    imu_hit_t h;
    while (imu_pop_hit(&h)) {
        if (h.g * 10.0f < s_p.hg) {
            continue;
        }
        if (move) {
            // RETRO MOVE: each move is a direction, so only the knock is a gesture
            if (fabsf(h.z) > fabsf(h.x) && fabsf(h.z) > fabsf(h.y)) {
                gesture(s_p.gk, "KNOCK");
            }
            continue;
        }
        // count the hits of the last SHAKE_WIN ms
        int64_t t = s_now;
        int k = 0;
        for (int i = 0; i < s_nhits; i++) {
            if (t - s_hits[i] < SHAKE_WIN) {
                s_hits[k++] = s_hits[i];
            }
        }
        s_nhits = k;
        if (s_nhits < 8) {
            s_hits[s_nhits++] = t;
        }
        bool shaking = s_shake_until > t;
        if (s_nhits >= SHAKE_HITS) {
            if (s_p.gs && !shaking) {
                gesture(s_p.gs, "SHAKE");
            }
            if (s_p.gs) {
                s_shake_until = t + SHAKE_HOLD;
            }
            continue;
        }
        if (shaking) {
            continue;
        }
        float ax = fabsf(h.x), ay = fabsf(h.y), az = fabsf(h.z);
        if (az > ax && az > ay) {
            gesture(s_p.gk, "KNOCK");
            continue;
        }
        int j = ax > ay ? (h.x > 0 ? 3 : 2) : (h.y < 0 ? 0 : 1);
        // the stop at the end of a jerk comes back the other way: it is not a new jerk
        static const int8_t OPP[4] = {1, 0, 3, 2};
        if (s_last_jerk >= 0 && OPP[s_last_jerk] == j && t - s_last_jerk_at < REBOUND_MS) {
            continue;
        }
        s_last_jerk = j;
        s_last_jerk_at = t;
        static const char *const NAME[4] = {"JERK UP", "JERK DOWN", "JERK LEFT", "JERK RIGHT"};
        const uint8_t btn[4] = {s_p.gu, s_p.gd, s_p.gl, s_p.gr};
        gesture(btn[j], NAME[j]);
    }
    imu_snap_t sn;
    while (imu_pop_snap(&sn)) {
        if (!move) {
            gesture(s_p.gn, "SNAP");
        }
    }
    if (s_p.gs && s_shake_until > s_now) {
        s_gest_until[s_p.gs] = s_shake_until;
    }
}

// ---------------------------------------------------------------- the modes

// The stick (RETRO and MOTION). The board is the stick of a classic one-button joystick: the user
// holds it at any angle that is comfortable, and that angle is "straight up". From there,
// forward (away from the body) = UP, toward the body = DOWN, left = LEFT, right = RIGHT.
// The calibration finds the frame of the user, at any angle and in any grip. The user holds the
// board and does not see the screen, so each step has a sound:
// 1. SETTLE: SETTLE_MS to take the comfortable pose (a tick each 0.5 s). Then the board must be
//    still for STILL_MS (at most STILL_MAX_MS more): the gravity direction d is "straight up".
//    A GO beep.
// 2. The gesture (at most CAL_GEST_MS):
//    - RETRO and MOTION: a tilt forward (FWD_MIN for FWD_MS): F = the direction of the tilt;
//    - RETRO MOVE: a short move forward (about 5 cm): F = the direction of the top speed in the
//      plane of the floor, when the move ends. An error if the board tilts too much during the
//      move (a tilt is not a move), or if the move goes mostly up or down.
// 3. GOOD (two notes up) or ERROR (two low notes; the reason on the screen and in the log). After
//    an error the old calibration stays.
// R = d x F = right. The tilt is the change of the gravity direction: right = (g - d).R, up =
// -(g - d).F. The calibration is kept in NVS (pad_c0..pad_c5): the next start needs none.
#define SETTLE_MS    2000
#define STILL_MS     500
#define STILL_MAX_MS 3000
#define STILL_DG     0.03f  // the gravity direction may move this much (unit vector) and count as still
#define FWD_MIN      0.17f  // about 10 degrees
#define FWD_MS       120
#define CAL_GEST_MS  4000
#define JC_START     1.0f   // m/s2: the calibration: a jolt this strong is the move (5 cm gave 2-4)
#define JC_MAX_MS    400    // the push of the calibration move: at most this long
#define MV_TILT_MAX  0.20f  // the gravity direction may change this much in the move (about 11 degrees)

enum {
    CAL_OFF,
    CAL_SETTLE,
    CAL_GESTURE
};
static int s_cal;      // the calibration step now
static bool s_cal_ok;  // s_d, s_f, s_r are valid
static float s_d[3], s_f[3], s_r[3];
static float s_avg[3];  // SETTLE: the gravity direction, filtered
static int s_still_ms, s_fwd_ms, s_cal_ms;
static int64_t s_cal_last, s_ready_at, s_calerr_at;
static const char *s_calerr;
static float s_mv_peak[3], s_mv_top, s_mv_vup, s_mv_tilt;  // RETRO MOVE: the move of the calibration
static bool s_mv_on;
static int s_mv_ms;
static float s_mv_vcal;  // RETRO MOVE: the top jolt of the calibration move, m/s2 (sets the threshold)
// RETRO MOVE: the jolt detector. The linear acceleration has a slow offset (about 0.5 m/s2 at rest):
// it is learned while the board is calm and taken away. A move = a push (the first jolt: its
// direction) and then a brake (a jolt the other way: it ends the move, it is not a new move).
static float s_jb[3];       // the offset (physical frame)
static int s_jst;           // 0 calm, 1 the push, 2 the brake
static int s_jdir, s_jout;  // the direction of the move; the direction to send
static float s_jpeak;
static int64_t s_jt0, s_jquiet, s_jlast;
static bool s_jlift;
static bool s_jarm = true;  // a new move needs the jolt to drop first
static float s_jraw[3];     // (the log)
static float s_jv[3];       // the short speed (m/s, physical frame): the direction of a move
#define J_CALM    0.35f     // m/s2: under this the board is calm (the speed sum starts again)
#define J_CALM_MS 120
#define J_VSTOP   0.04f  // m/s: a speed against the jolt over this = the jolt is a stop
static int s_jcalm_ms;
volatile int g_pad_cal_req;        // the console: "padcal" starts a calibration
volatile int64_t g_pad_log_until;  // the console: "padlog N": the live values for N s

static void norm3(float *v)
{
    float m = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (m > 1e-6f) {
        v[0] /= m;
        v[1] /= m;
        v[2] /= m;
    }
}

static void gravity_unit(float g[3])
{
    imu_gravity_phys(&g[0], &g[1], &g[2]);
    norm3(g);
}

// F from a direction: the part of v at right angles to d; then R = d x F
static void set_forward(const float *v)
{
    float k = v[0] * s_d[0] + v[1] * s_d[1] + v[2] * s_d[2];
    for (int i = 0; i < 3; i++) {
        s_f[i] = v[i] - k * s_d[i];
    }
    norm3(s_f);
    s_r[0] = s_d[1] * s_f[2] - s_d[2] * s_f[1];
    s_r[1] = s_d[2] * s_f[0] - s_d[0] * s_f[2];
    s_r[2] = s_d[0] * s_f[1] - s_d[1] * s_f[0];
}

static const char *const CAL_KEY[6] = {"pad_c0", "pad_c1", "pad_c2", "pad_c3", "pad_c4", "pad_c5"};

static void cal_save(void)
{
    for (int i = 0; i < 6; i++) {
        float v = i < 3 ? s_d[i] : s_f[i - 3];
        uint32_t u;
        memcpy(&u, &v, 4);
        store_set(CAL_KEY[i], u);
    }
    uint32_t u;
    memcpy(&u, &s_mv_vcal, 4);
    store_set("pad_cm", u);
    store_set("pad_cv", 1);
}

static void cal_load(void)
{
    s_cal_ok = false;
    if (store_get("pad_cv", 0) != 1) {
        return;
    }
    float v[6];
    for (int i = 0; i < 6; i++) {
        uint32_t u = store_get(CAL_KEY[i], 0);
        memcpy(&v[i], &u, 4);
        if (!(v[i] >= -1.01f && v[i] <= 1.01f)) {
            return;  // also NaN
        }
    }
    memcpy(s_d, v, 12);
    norm3(s_d);
    set_forward(v + 3);
    uint32_t um = store_get("pad_cm", 0);
    memcpy(&s_mv_vcal, &um, 4);
    if (!(s_mv_vcal >= 0.5f && s_mv_vcal <= 20.0f)) {
        s_mv_vcal = 0;  // (an old speed value: none)
    }
    s_cal_ok = fabsf(s_d[0] * s_d[0] + s_d[1] * s_d[1] + s_d[2] * s_d[2] - 1) < 0.01f;
}

// the sounds of the calibration (also when the screen is out of sight)
static void cal_tone(float f, int ms, int delay_ms, uint8_t vol)
{
    audio_voice_t v = {
            .delay = (uint32_t)delay_ms * AUDIO_SAMPLE_RATE / 1000,
            .dur = (uint32_t)ms * AUDIO_SAMPLE_RATE / 1000,
            .f0 = f,
            .f1 = f,
            .wave = W_SQUARE,
            .vol = vol,
            .attack_ms = 4,
            .shape = AUDIO_PERC,
    };
    audio_play(&v);
}
static void snd_go(void)
{
    cal_tone(988, 220, 0, 30);
}
static void snd_good(void)
{
    cal_tone(784, 120, 0, 26);
    cal_tone(1175, 220, 130, 26);
}
static void snd_err(void)
{
    cal_tone(330, 200, 0, 30);
    cal_tone(220, 320, 230, 30);
}

// the speed sum of a move: zero while the board is calm, then the plain sum of the acceleration
static bool jolt_sum(const float *m, int dt)
{
    float mag = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    if (mag < J_CALM) {
        s_jcalm_ms += dt;
    } else {
        s_jcalm_ms = 0;
    }
    if (s_jcalm_ms >= J_CALM_MS) {
        s_jv[0] = s_jv[1] = s_jv[2] = 0;
        return true;
    }
    for (int i = 0; i < 3; i++) {
        s_jv[i] += m[i] * (float)dt / 1000.0f;
    }
    return false;
}
// the direction of a move at its jolt j (a vector): the speed if it is against the jolt (a stop),
// else the jolt (a push)
static void jolt_way(const float *j, float *out)
{
    float vj = s_jv[0] * j[0] + s_jv[1] * j[1] + s_jv[2] * j[2];
    float vn = sqrtf(s_jv[0] * s_jv[0] + s_jv[1] * s_jv[1] + s_jv[2] * s_jv[2]);
    bool stop = vn > J_VSTOP && vj < 0;
    for (int i = 0; i < 3; i++) {
        out[i] = stop ? s_jv[i] : j[i];
    }
}
static float move_v_on_calc(void);
static void cal_start(void)
{
    if (!imu_ok()) {
        return;
    }
    s_cal = CAL_SETTLE;
    s_still_ms = s_fwd_ms = s_cal_ms = 0;
    s_cal_last = s_now;
    s_calerr = NULL;
    gravity_unit(s_avg);
    s_dir = -1;
    s_tilt_on = false;
    ESP_LOGI(TAG, "padcal: settle (%s)", mode_now() == PAD_MODE_MOVE ? "MOVE" : "TILT");
}

static void cal_fail(const char *why)
{
    s_cal = CAL_OFF;
    s_calerr = why;
    s_calerr_at = s_now;
    snd_err();
    ESP_LOGW(TAG, "padcal: ERROR %s", why);
}

static void cal_done(const float *v)
{
    set_forward(v);
    s_cal_ok = true;
    s_cal = CAL_OFF;
    s_ready_at = s_now;
    s_calerr = NULL;
    cal_save();
    snd_good();
    pad_move_reset(&s_mv);
    s_jst = 0;
    s_jout = -1;
    s_jquiet = s_now + 600;  // (the brake of the calibration move is not a move)
    ESP_LOGI(TAG, "padcal: GOOD d=(%.2f %.2f %.2f) F=(%.2f %.2f %.2f)", s_d[0], s_d[1], s_d[2], s_f[0], s_f[1], s_f[2]);
}

// one calibration step for each frame
static void cal_step(void)
{
    if (s_cal == CAL_OFF) {
        return;
    }
    int dt = (int)(s_now - s_cal_last);
    s_cal_last = s_now;
    if (dt < 0 || dt > 200) {
        dt = 0;
    }
    s_cal_ms += dt;
    float g[3];
    gravity_unit(g);
    if (s_cal == CAL_SETTLE) {
        // a soft tick each 0.5 s while the user takes the pose
        if (s_cal_ms < SETTLE_MS && (s_cal_ms / 500) != ((s_cal_ms - dt) / 500)) {
            sfx_ui(SFX_TICK);
        }
        float dx = g[0] - s_avg[0], dy = g[1] - s_avg[1], dz = g[2] - s_avg[2];
        for (int i = 0; i < 3; i++) {
            s_avg[i] += 0.15f * (g[i] - s_avg[i]);
        }
        if (dx * dx + dy * dy + dz * dz < STILL_DG * STILL_DG && imu_shake() < 0.15f) {
            s_still_ms += dt;
        } else {
            s_still_ms = 0;
        }
        {
            float la[3];
            imu_linacc_phys(la);
            for (int i = 0; i < 3; i++) {
                s_jb[i] += (la[i] - s_jb[i]) * 0.1f;
            }
        }
        if (s_cal_ms >= SETTLE_MS && s_still_ms >= STILL_MS) {
            s_jv[0] = s_jv[1] = s_jv[2] = 0;
            memcpy(s_d, s_avg, 12);
            norm3(s_d);
            s_cal = CAL_GESTURE;
            s_cal_ms = s_fwd_ms = 0;
            s_mv_on = false;
            s_mv_top = s_mv_vup = s_mv_tilt = 0;
            snd_go();
            ESP_LOGI(TAG, "padcal: GO d=(%.2f %.2f %.2f)", s_d[0], s_d[1], s_d[2]);
        } else if (s_cal_ms >= SETTLE_MS + STILL_MAX_MS) {
            cal_fail("NOT STILL");
        }
        return;
    }
    // the gesture
    float dv[3];
    for (int i = 0; i < 3; i++) {
        dv[i] = g[i] - s_d[i];
    }
    float k = dv[0] * s_d[0] + dv[1] * s_d[1] + dv[2] * s_d[2];
    float px = dv[0] - k * s_d[0], py = dv[1] - k * s_d[1], pz = dv[2] - k * s_d[2];
    float tilt = sqrtf(px * px + py * py + pz * pz);
    if (mode_now() == PAD_MODE_MOVE) {
        // the jolt in the plane of the floor (at right angles to d), and up / down
        float la[3];
        imu_linacc_phys(la);
        for (int i = 0; i < 3; i++) {
            la[i] -= s_jb[i];
        }
        float ad = la[0] * s_d[0] + la[1] * s_d[1] + la[2] * s_d[2];
        float h[3] = {la[0] - ad * s_d[0], la[1] - ad * s_d[1], la[2] - ad * s_d[2]};
        float ah = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
        if (tilt > s_mv_tilt) {
            s_mv_tilt = tilt;
        }
        jolt_sum(la, dt);
        if (s_now <= g_pad_log_until) {
            ESP_LOGI(TAG, "PC %lld jolt %.2f up %.2f tilt %.3f top %.2f on %d", (long long)s_now, ah, ad, tilt,
                     s_mv_top, s_mv_on);
        }
        if (!s_mv_on && ah > JC_START) {
            // the jolt: the direction of the move now (the jolt, or the speed if this is the stop)
            float w[3];
            jolt_way(h, w);
            float wd = w[0] * s_d[0] + w[1] * s_d[1] + w[2] * s_d[2];
            for (int i = 0; i < 3; i++) {
                s_mv_peak[i] = w[i] - wd * s_d[i];
            }
            s_mv_on = true;
            s_mv_ms = 0;
            s_mv_top = 0;
            s_mv_vup = fabsf(ad);
        }
        if (s_mv_on) {
            // the top jolt of the move (the threshold), until the board is calm (or 600 ms)
            s_mv_ms += dt;
            if (ah > s_mv_top) {
                s_mv_top = ah;
            }
            if (s_mv_ms >= 600 || (s_mv_ms > 150 && s_jcalm_ms > 0)) {
                ESP_LOGI(TAG, "padcal: jolt top %.2f m/s2, up %.2f, tilt %.2f", s_mv_top, s_mv_vup, s_mv_tilt);
                if (s_mv_tilt > MV_TILT_MAX) {
                    cal_fail("TILTED, MOVE IT");
                } else if (s_mv_vup > 1.2f * s_mv_top) {
                    cal_fail("UP OR DOWN");
                } else {
                    s_mv_vcal = s_mv_top;
                    cal_done(s_mv_peak);
                    ESP_LOGI(TAG, "padcal: the jolt threshold is now %.2f m/s2", move_v_on_calc());
                }
                return;
            }
        }
    } else {
        if (tilt > FWD_MIN) {
            s_fwd_ms += dt;
        } else {
            s_fwd_ms = 0;
        }
        if (s_fwd_ms >= FWD_MS) {
            cal_done(dv);
            return;
        }
    }
    if (s_cal_ms > CAL_GEST_MS) {
        cal_fail(mode_now() == PAD_MODE_MOVE ? (s_mv_on ? "TOO SMALL" : "NO MOVE") : "NO TILT");
    }
}

// the console "padlog": the live values (10 lines a second)
static void pad_log(float tx, float ty)
{
    if (s_now > g_pad_log_until) {
        return;
    }
    static int64_t last;
    if (s_now - last < 100) {
        return;
    }
    last = s_now;
    float v[3] = {0, 0, 0}, vx = 0, vy = 0;
    imu_velocity_phys(v);
    if (s_cal_ok) {
        vx = v[0] * s_r[0] + v[1] * s_r[1] + v[2] * s_r[2];
        vy = -(v[0] * s_f[0] + v[1] * s_f[1] + v[2] * s_f[2]);
    }
    ESP_LOGI(TAG, "PL %lld mode %d cal %d ok %d tilt %.1f %.1f deg v %.2f %.2f m/s dir %d", (long long)s_now,
             mode_now(), s_cal, s_cal_ok, tilt_deg(tx), tilt_deg(ty), vx, vy, s_dir);
}

static void tilt_read(float *tx, float *ty)
{
    *tx = *ty = 0;
    if (imu_ok() && s_cal_ok && s_cal == CAL_OFF) {
        float g[3], dv[3];
        gravity_unit(g);
        for (int i = 0; i < 3; i++) {
            dv[i] = g[i] - s_d[i];
        }
        *tx = dv[0] * s_r[0] + dv[1] * s_r[1] + dv[2] * s_r[2];
        *ty = -(dv[0] * s_f[0] + dv[1] * s_f[1] + dv[2] * s_f[2]);
    }
    if (s_p.invx) {
        *tx = -*tx;
    }
    if (s_p.invy) {
        *ty = -*ty;
    }
    s_deg_x = tilt_deg(*tx);
    s_deg_y = tilt_deg(*ty);
    pad_log(*tx, *ty);
}

static void touch_fire(pad_report_t *r);

static void mode_retro(pad_report_t *r)
{
    float tx, ty;
    tilt_read(&tx, &ty);
    float mag = tilt_deg(sqrtf(tx * tx + ty * ty));
    // the dead zone with a little hysteresis
    float on = s_p.dead + 1.5f, off = s_p.dead - 1.5f;
    if (off < 0.5f) {
        off = 0.5f;
    }
    if (on < 1.5f) {
        on = 1.5f;
    }
    if (mag > on) {
        s_tilt_on = true;
    }
    if (mag < off) {
        s_tilt_on = false;
    }
    s_dir = s_tilt_on ? dir_of(tx, ty, s_p.dirs, s_dir) : -1;
    if (s_dir >= 0) {
        r->hat = (uint8_t)s_dir;
        r->x = (int8_t)(DX[s_dir] * 127);
        r->y = (int8_t)(DY[s_dir] * 127);
    }
    float m = tfn_clampf(mag / s_p.range, 0, 1);
    float len = sqrtf(tx * tx + ty * ty);
    s_sx = len > 0.001f ? tx / len * m : 0;
    s_sy = len > 0.001f ? ty / len * m : 0;
    s_jx = s_sx;
    s_jy = s_sy;
    touch_fire(r);
}

// RETRO MOVE: the direction comes from the motion of the board (pad_move.h), in the same
// frame of the user as RETRO (the calibration), so "forward" is the user's forward in any
// grip. The speed comes from the IMU task (imu_velocity_phys, 200 Hz).
#define MOVE_V_FULL 1.0f  // m/s: the full stick in the picture

// the jolt for a move: 45 % of the top jolt of the user's calibration move (with none: 1.2 m/s2),
// between 0.7 and 3 m/s2; the sensitivity changes it by 15 % for each step from 5
static float move_v_on(void);
static float move_v_on_calc(void)
{
    return move_v_on();
}
static float move_v_on(void)
{
    float base = s_mv_vcal > 0 ? 0.45f * s_mv_vcal : 1.2f;
    if (base < 0.7f) {
        base = 0.7f;
    }
    if (base > 3.0f) {
        base = 3.0f;
    }
    return base * powf(1.15f, 5.0f - (float)s_p.msens);
}

static void mode_move(pad_report_t *r)
{
    float ax = 0, ay = 0, aup = 0, jm[3] = {0, 0, 0};
    bool calm = true;
    int dt = (int)(s_now - s_jlast);
    s_jlast = s_now;
    if (dt < 0 || dt > 200) {
        dt = 0;
    }
    if (imu_ok() && s_cal_ok && s_cal == CAL_OFF) {
        float la[3];
        imu_linacc_phys(la);
        float m[3] = {la[0] - s_jb[0], la[1] - s_jb[1], la[2] - s_jb[2]};
        // the offset follows the angle of the hand: always learned, faster when the board is calm
        // (0.4 s) than in a move (1.5 s: a jolt of 0.15 s stays)
        float tau = (s_jst == 0 && sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]) < 0.6f) ? 400.0f : 1500.0f;
        for (int i = 0; i < 3; i++) {
            s_jb[i] += (la[i] - s_jb[i]) * fminf(1.0f, (float)dt / tau);
        }
        s_jraw[0] = la[0], s_jraw[1] = la[1], s_jraw[2] = la[2];
        ax = m[0] * s_r[0] + m[1] * s_r[1] + m[2] * s_r[2];
        ay = -(m[0] * s_f[0] + m[1] * s_f[1] + m[2] * s_f[2]);
        aup = -(m[0] * s_d[0] + m[1] * s_d[1] + m[2] * s_d[2]);
        calm = jolt_sum(m, dt);
        for (int i = 0; i < 3; i++) {
            jm[i] = m[i];
        }
    }
    float vx = s_jv[0] * s_r[0] + s_jv[1] * s_r[1] + s_jv[2] * s_r[2];
    float vy = -(s_jv[0] * s_f[0] + s_jv[1] * s_f[1] + s_jv[2] * s_f[2]);
    if (s_p.invx) {
        vx = -vx;
    }
    if (s_p.invy) {
        vy = -vy;
    }
    if (s_p.invx) {
        ax = -ax;
    }
    if (s_p.invy) {
        ay = -ay;
    }
    // a finger that touches or leaves the screen jerks the board: no new move for a moment
    if (s_npt != s_prev_npt) {
        s_jquiet = s_now + 150;
    }
    s_prev_npt = s_npt;
    float on = move_v_on(), ah = sqrtf(ax * ax + ay * ay);
    static const float UX[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
    static const float UY[8] = {-1, -0.7071f, 0, 0.7071f, 1, 0.7071f, 0, -0.7071f};
    bool latch = s_p.mstyle == PAD_MOVE_LATCH;
    if (s_jst == 0) {
        // the next move: only after a calm moment
        if (!s_jarm && calm && s_now >= s_jquiet) {
            s_jarm = true;
        }
        if (s_jarm && ah > on && s_now >= s_jquiet && fabsf(aup) < 1.2f * ah) {
            float w[3];
            jolt_way(jm, w);
            float qx = w[0] * s_r[0] + w[1] * s_r[1] + w[2] * s_r[2];
            float qy = -(w[0] * s_f[0] + w[1] * s_f[1] + w[2] * s_f[2]);
            if (s_p.invx) {
                qx = -qx;
            }
            if (s_p.invy) {
                qy = -qy;
            }
            // a hand move is never quite straight: a diagonal only when the two parts are near
            if (s_p.dirs != 4) {
                if (fabsf(qx) < 0.6f * fabsf(qy)) {
                    qx = 0;
                } else if (fabsf(qy) < 0.6f * fabsf(qx)) {
                    qy = 0;
                }
            }
            s_jdir = pm_dir_of(qx, qy, s_p.dirs, -1, 0);
            s_jst = 1;
            s_jt0 = s_now;
            s_jout = s_jdir;
            s_jarm = false;
        } else if (latch && !s_jlift && fabsf(aup) > 1.5f * on && fabsf(aup) > 1.5f * ah) {
            s_jlift = true;  // a lift or a drop lets the latched direction go
            s_jout = -1;
        }
        if (s_jlift && fabsf(aup) < 0.4f * on) {
            s_jlift = false;
        }
    } else {
        // the move lasts until the board is calm again (its stop is part of it)
        if ((calm && s_now - s_jt0 > 150) || s_now - s_jt0 > 800) {
            s_jst = 0;
            s_jquiet = s_now + 150;
            if (!latch) {
                s_jout = -1;
            }
        }
    }
    s_dir = s_jout;
    if (s_dir >= 0) {
        r->hat = (uint8_t)s_dir;
        r->x = (int8_t)(DX[s_dir] * 127);
        r->y = (int8_t)(DY[s_dir] * 127);
    }
    if (s_now <= g_pad_log_until) {
        static int64_t lastm;
        if (s_now - lastm >= 20) {
            lastm = s_now;
            ESP_LOGI(TAG,
                     "PM %lld a %.2f %.2f up %.2f v %.3f %.3f st %d dir %d on %.2f raw %.2f %.2f %.2f b %.2f %.2f %.2f",
                     (long long)s_now, ax, ay, aup, vx, vy, s_jst, s_dir, on, s_jraw[0], s_jraw[1], s_jraw[2], s_jb[0],
                     s_jb[1], s_jb[2]);
        }
    }
    // the picture: the live jolt (left) and the direction that the computer gets (the joystick)
    s_sx = tfn_clampf(ax / (2 * on), -1, 1);
    s_sy = tfn_clampf(ay / (2 * on), -1, 1);
    s_jx = s_dir >= 0 ? DX[s_dir] * 0.85f : 0;
    s_jy = s_dir >= 0 ? DY[s_dir] * 0.85f : 0;
    touch_fire(r);
}

// FIRE: any touch in the play area (RETRO and RETRO MOVE)
static void touch_fire(pad_report_t *r)
{
    bool fire = s_npt > 0;
    if (fire && !s_fire) {
        s_fire_t0 = s_now;
    }
    s_fire = fire;
    if (fire) {
        bool on_now = true;
        if (s_p.autofire) {
            int64_t period = 1000 / s_p.autofire;
            on_now = ((s_now - s_fire_t0) % period) < period / 2;
        }
        if (on_now) {
            r->buttons |= bit(s_p.fire);
        }
        s_screen |= bit(s_p.fire);
    }
}

static void mode_buttons(pad_report_t *r)
{
    int dir = -1;
    bool any_dpad = false;
    for (int i = 0; i < s_npt; i++) {
        int d = dpad_at(s_ptx[i], s_pty[i]);
        if (d >= -1) {
            any_dpad = true;
            if (d >= 0) {
                dir = d;
            }
            continue;
        }
        int b = button_at(s_ptx[i], s_pty[i]);
        if (b) {
            r->buttons |= bit(b);
            s_screen |= bit(b);
        }
    }
    s_dir = any_dpad ? dir : -1;
    if (s_dir >= 0) {
        r->hat = (uint8_t)s_dir;
        r->x = (int8_t)(DX[s_dir] * 127);
        r->y = (int8_t)(DY[s_dir] * 127);
        s_screen |= 0x8000;  // a d-pad press: a marker for the click only
    }
    s_sx = s_dir >= 0 ? DX[s_dir] : 0;
    s_sy = s_dir >= 0 ? DY[s_dir] : 0;
}

static void mode_motion(pad_report_t *r)
{
    float tx, ty;
    tilt_read(&tx, &ty);
    s_sx = axis(s_deg_x);
    s_sy = s_p.usey ? axis(s_deg_y) : 0;
    r->x = to_axis(s_sx);
    r->y = to_axis(s_sy);
    s_rx = s_ry = 0;
    switch (s_p.touch) {
    case PAD_TOUCH_PAD:
        if (s_npt > 0) {
            if (!s_tp_on) {
                s_tp_on = true;
                s_tp_moved = false;
                s_tp_x0 = s_ptx[0];
                s_tp_y0 = s_pty[0];
                s_tp_t0 = s_now;
            }
            float dx = (s_ptx[0] - s_tp_x0) / PAD_TP_R, dy = (s_pty[0] - s_tp_y0) / PAD_TP_R;
            if (fabsf(dx) * PAD_TP_R > 10 || fabsf(dy) * PAD_TP_R > 10) {
                s_tp_moved = true;
            }
            s_rx = tfn_clampf(dx, -1, 1);
            s_ry = tfn_clampf(dy, -1, 1);
            r->z = to_axis(s_rx);
            r->rz = to_axis(s_ry);
        } else if (s_tp_on) {
            s_tp_on = false;
            // a short touch with no move = a tap: the left button
            if (!s_tp_moved && s_now - s_tp_t0 < TAP_MS && s_p.bl) {
                s_gest_until[s_p.bl] = s_now + GEST_MS;
                s_screen |= bit(s_p.bl);
            }
        }
        break;
    case PAD_TOUCH_ONE:
        if (s_npt > 0 && s_p.bl) {
            r->buttons |= bit(s_p.bl);
            s_screen |= bit(s_p.bl);
        }
        break;
    default:
        for (int i = 0; i < s_npt; i++) {
            int b = s_ptx[i] < SCR_W / 2 ? s_p.bl : s_p.br;
            r->buttons |= bit(b);
            s_screen |= bit(b);
        }
        break;
    }
}

// ---------------------------------------------------------------- app

static void gp_init(void)
{
    c_text = rgb(255, 255, 255);
    c_dim = rgb(140, 150, 190);
    c_accent = rgb(255, 220, 60);
    pad_config_load();
}

static void gp_enter(int64_t now)
{
    s_now = now;
    s_frames = 0;
    s_start_req = true;  // after the first frame: the screen shows "STARTING"
    s_confirm = false;
    s_drawer = s_drawer_to = 0;
    s_pulling = false;
    s_pull_y0 = 9999;
    s_drawer_at = now;
    s_screen = s_prev_screen = 0;
    s_nhits = 0;
    s_shake_until = 0;
    s_last_jerk = -1;
    s_fire = false;
    s_pop_at = 0;
    s_prev_npt = 0;
    s_jx = s_jy = 0;
    load_profile();
    imu_set_level();
    cal_load();
    s_cal = CAL_OFF;
    imu_hits_clear();
    imu_snaps_clear();
    s_rep = (pad_report_t){0, PAD_HAT_CENTER, 0, 0, 0, 0};
}

// ---------------------------------------------------------------- the pull-down panel

static bool drawer_open(void)
{
    return s_drawer > 0.02f || s_pulling;
}

// the panel moves this far down (px); the parts are at their open place + this
static int drawer_dy(void)
{
    return (int)((s_drawer - 1.0f) * SCR_H);
}

// SELECT and START of the panel: from the touch points, so that both can be held
static void drawer_buttons(pad_report_t *r)
{
    if (s_drawer < 0.9f) {
        return;
    }
    int16_t px[2], py[2];
    int n = input_points(px, py, 2);
    for (int i = 0; i < n && i < 2; i++) {
        int x, y;
        gfx_touch_to_logical(px[i], py[i], &x, &y);
        if (ui_hit(x, y, DR_SEL_X, DR_SS_Y, DR_SS_W, DR_SS_H, 0)) {
            r->buttons |= bit(BTN_SELECT);
            s_screen |= bit(BTN_SELECT);
        } else if (ui_hit(x, y, DR_STA_X, DR_SS_Y, DR_SS_W, DR_SS_H, 0)) {
            r->buttons |= bit(BTN_START);
            s_screen |= bit(BTN_START);
        }
    }
}

// the width of CALIBRATE in the panel (RETRO MOVE: the style button is at its right)
static int cal_w(void)
{
    return mode_now() == PAD_MODE_MOVE ? DR_CAL_W * 6 / 10 : DR_CAL_W;
}

// the touch events of the open panel
static void drawer_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        s_pull_y0 = ev->y;
        return;
    }
    if (ev->kind == IN_MOVE && ev->y < s_pull_y0 - 30) {
        s_drawer_to = 0;  // a drag up closes the panel
        return;
    }
    if (ev->kind != IN_TAP || s_drawer < 0.9f) {
        return;
    }
    int x = ev->x, y = ev->y;
    bool tilt = mode_now() != PAD_MODE_BUTTONS;
    if (y >= DR_CLOSE_Y) {
        s_drawer_to = 0;
        sfx_ui(SFX_BACK);
    } else if (tilt && ui_hit(x, y, DR_CAL_X, DR_CAL_Y, cal_w(), DR_CAL_H, 0)) {
        s_drawer_to = 0;  // the calibration needs the board free of the panel
        cal_start();
        sfx_ui(SFX_CLICK);
    } else if (mode_now() == PAD_MODE_MOVE &&
               ui_hit(x, y, DR_CAL_X + cal_w() + 4, DR_CAL_Y, DR_CAL_W - cal_w() - 4, DR_CAL_H, 0)) {
        pad_set_move_style(s_p.mstyle == PAD_MOVE_LATCH ? PAD_MOVE_HOLD : PAD_MOVE_LATCH);
        sfx_ui(SFX_TICK);
    } else if (ui_hit(x, y, DR_PRO_X, DR_PRO_Y, DR_PRO_W / 2, DR_PRO_H, 0)) {
        pad_set_active((pad_active() + PAD_PROFILES - 1) % PAD_PROFILES);
        sfx_ui(SFX_TICK);
    } else if (ui_hit(x, y, DR_PRO_X + DR_PRO_W / 2, DR_PRO_Y, DR_PRO_W / 2, DR_PRO_H, 0)) {
        pad_set_active((pad_active() + 1) % PAD_PROFILES);
        sfx_ui(SFX_TICK);
    }
}

static void gp_leave(void)
{
    // no button may stay down on the computer
    s_rep = (pad_report_t){0, PAD_HAT_CENTER, 0, 0, 0, 0};
    bt_pad_send(&s_rep);
}

static void gp_input(const input_event_t *ev)
{
    if (s_confirm) {
        if (ev->kind != IN_TAP) {
            return;
        }
#if BOARD_BAR_SCREEN
        if (ev->x >= 200 && ev->x < 310 && ev->y >= 96 && ev->y < 140) {
#elif BOARD_UI_TABLET
        if (ev->x >= 160 && ev->x < 300 && ev->y >= 196 && ev->y < 260) {
#else
        if (ev->x >= 50 && ev->x < 150 && ev->y >= 130 && ev->y < 174) {
#endif
            bt_pad_forget();
            pop("PAIRINGS FORGOTTEN", rgb(255, 120, 120));
            sfx_play(SFX_CUT);
        } else {
            sfx_ui(SFX_BACK);
        }
        s_confirm = false;
        return;
    }
    // the pull: a finger that goes down on the top strip and moves down pulls the panel
    if (s_pulling) {
        if (ev->kind == IN_MOVE) {
            float f = (float)(ev->y - s_pull_y0 + TOP_H) / SCR_H;
            s_drawer = s_drawer_to = f < 0 ? 0 : (f > 1 ? 1 : f);
        } else if (ev->kind == IN_UP) {
            s_pulling = false;
            s_drawer_to = s_drawer > 0.3f ? 1.0f : 0.0f;
            if (s_drawer_to > 0) {
                sfx_play(SFX_SWITCH);
            }
        }
        return;
    }
    if (drawer_open()) {
        drawer_input(ev);
        return;
    }
    if (ev->kind == IN_DOWN) {
        s_pull_y0 = ev->y < TOP_H ? ev->y : 9999;  // only a touch on the strip can pull
        if (ev->y < TOP_H) {
            return;
        }
    }
    if (ev->kind == IN_MOVE && ev->y < TOP_H + 60 && s_pull_y0 < TOP_H && ev->y > s_pull_y0 + DR_PULL_PX) {
        s_pulling = true;
        input_tap_cancel();  // (a pull: no tap on the strip at the lift)
        return;
    }
    if (ev->y >= TOP_H) {
        return;  // the play area: the touch points do the work
    }
    if (ev->kind == IN_LONG) {
        s_confirm = true;
        sfx_play(SFX_BEEP);
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (mode_now() != PAD_MODE_BUTTONS) {
        // the tilt modes: the strip has no buttons (no press by mistake); a tap opens the panel
        s_drawer_to = 1;
        sfx_play(SFX_SWITCH);
        return;
    }
    // (the thumb, round 3: each half of the name box is an arrow)
    int mid = (PREV_X + NEXT_X + 4) / 2;
    if (ev->x >= PREV_X - 2 && ev->x < mid) {
        pad_set_active((pad_active() + PAD_PROFILES - 1) % PAD_PROFILES);
        sfx_ui(SFX_TICK);
    } else if (ev->x >= mid && ev->x < NEXT_X + 6) {
        pad_set_active((pad_active() + 1) % PAD_PROFILES);
        sfx_ui(SFX_TICK);
    }
}

static void gp_update(int64_t now)
{
    s_now = now;
    s_frames++;
    if (s_start_req && s_frames >= 2) {
        s_start_req = false;
        bt_pad_start();
    }
    if (s_ver != pad_config_version()) {
        load_profile();
    }

    pad_report_t r = {0, PAD_HAT_CENTER, 0, 0, 0, 0};
    s_prev_screen = s_screen;
    s_screen = 0;
    if (!s_pulling) {
        float k = (now - s_drawer_at) * 0.012f;
        if (k > 1) {
            k = 1;
        }
        if (k < 0) {
            k = 0;
        }
        s_drawer += (s_drawer_to - s_drawer) * k;
        if (fabsf(s_drawer_to - s_drawer) < 0.01f) {
            s_drawer = s_drawer_to;
        }
    }
    s_drawer_at = now;
    if (drawer_open() && !s_confirm) {
        // the panel: the stick and FIRE rest; SELECT and START from the panel
        s_npt = 0;
        s_dir = -1;
        s_fire = false;
        imu_hits_clear();
        imu_snaps_clear();
        drawer_buttons(&r);
    } else if (!s_confirm) {
        read_points();
        read_gestures();
        // the tilt modes need a calibration: the first time, it starts by itself
        if (mode_now() != PAD_MODE_BUTTONS && !s_cal_ok && s_cal == CAL_OFF && !s_calerr && imu_ok()) {
            cal_start();
        }
        if (g_pad_cal_req) {
            g_pad_cal_req = 0;
            if (mode_now() != PAD_MODE_BUTTONS) {
                cal_start();
            }
        }
        cal_step();
        switch (mode_now()) {
        case PAD_MODE_BUTTONS:
            mode_buttons(&r);
            break;
        case PAD_MODE_MOTION:
            mode_motion(&r);
            break;
        case PAD_MODE_MOVE:
            mode_move(&r);
            break;
        default:
            mode_retro(&r);
            break;
        }
        for (int b = 1; b <= 16; b++) {
            if (s_gest_until[b] > now) {
                r.buttons |= bit(b);
            }
        }
    } else {
        s_npt = 0;
        imu_hits_clear();
        imu_snaps_clear();
    }
    if (s_p.click && (s_screen & ~s_prev_screen)) {
        sfx_ui(SFX_TICK);
    }
    s_rep = r;
    bt_pad_send(&r);
}

static bool gp_awake(void)
{
    pad_state_t st = bt_pad_state();
    return st != PAD_FAILED;
}

// ---------------------------------------------------------------- drawing

static void line(int x0, int y0, int x1, int y1, int t, uint16_t c)
{
    int dx = x1 - x0, dy = y1 - y0;
    int n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (n == 0) {
        n = 1;
    }
    for (int i = 0; i <= n; i++) {
        gfx_fill_rect(x0 + dx * i / n - t / 2, y0 + dy * i / n - t / 2, t, t, c);
    }
}

static void rrect(int x, int y, int w, int h, int r, uint16_t c)
{
    if (r * 2 > h) {
        r = h / 2;
    }
    if (r * 2 > w) {
        r = w / 2;
    }
    gfx_fill_rect(x + r, y, w - 2 * r, h, c);
    gfx_fill_rect(x, y + r, r, h - 2 * r, c);
    gfx_fill_rect(x + w - r, y + r, r, h - 2 * r, c);
    gfx_fill_circle(x + r, y + r, r, c);
    gfx_fill_circle(x + w - r - 1, y + r, r, c);
    gfx_fill_circle(x + r, y + h - r - 1, r, c);
    gfx_fill_circle(x + w - r - 1, y + h - r - 1, r, c);
}

// the Bluetooth rune, about 10 x 16 px, centre (cx, cy)
static void bt_rune(int cx, int cy, int s, uint16_t c)
{
    int h = 8 * s, w = 4 * s;
    line(cx, cy - h, cx, cy + h, s, c);
    line(cx, cy - h, cx + w, cy - h / 2, s, c);
    line(cx + w, cy - h / 2, cx - w, cy + h / 2, s, c);
    line(cx, cy + h, cx + w, cy + h / 2, s, c);
    line(cx + w, cy + h / 2, cx - w, cy - h / 2, s, c);
}

static void draw_background(void)
{
    for (int y = 0; y < SCR_H; y += 4) {
        int k = y * 30 / SCR_H;
        gfx_fill_rect(0, y, SCR_W, 4, rgb((uint8_t)(10 + k / 3), (uint8_t)(12 + k / 3), (uint8_t)(30 + k)));
    }
}

// the pull handle of the top strip: "MENU" with a small arrow down
static void draw_pull_handle(int x, int w)
{
    rrect(x, 4, w, TOP_H - 8, 5, rgb(36, 60, 100));
    int ty = (TOP_H - 7 * TOP_TS) / 2;
    gfx_text_center_in(x, w - 10 * TOP_TS, ty, "MENU", TOP_TS, rgb(160, 220, 255));
    int ax = x + w - 12, ay = TOP_H / 2 - 2;
    for (int j = 0; j < 4; j++) {
        gfx_fill_rect(ax - 3 + j, ay + j, 7 - 2 * j, 1, rgb(160, 220, 255));
    }
}

#if BOARD_BAR_SCREEN || BOARD_UI_TABLET
static void draw_top(void)
{
    gfx_fill_rect(0, 0, SCR_W, TOP_H, rgb(16, 18, 40));
    gfx_fill_rect(0, TOP_H - 1, SCR_W, 1, rgb(50, 56, 100));
    // left: the pull handle (tilt modes) or the title
    bool tilt = mode_now() != PAD_MODE_BUTTONS;
    if (tilt) {
        draw_pull_handle(6, CENTER_W + 40);
    } else {
#if BOARD_UI_TABLET
        gfx_text(12, no_tilt() ? 12 : (TOP_H - 14) / 2, "GAMEPAD", 2, c_accent);
        if (no_tilt()) {
            gfx_text(12, 32, "NO TILT", 1, rgb(255, 120, 120));
        }
#else
        gfx_text(10, (TOP_H - 8) / 2, "GAMEPAD", 1, c_accent);
#endif
    }
    // middle: the profile (the tilt modes: its name only; < > are in the panel)
    char t[32];
    snprintf(t, sizeof(t), "%d. %s", pad_active() + 1, s_p.name);
    rrect(PREV_X, 3, NEXT_X - PREV_X, TOP_H - 6, 8, rgb(30, 34, 66));
    if (!tilt) {
        gfx_text(PREV_X + 14, (TOP_H - 12) / 2, "<", 2, c_accent);
        gfx_text(NEXT_X - 24, (TOP_H - 12) / 2, ">", 2, c_accent);
    }
#if BOARD_UI_TABLET
    {
        int nw = NEXT_X - PREV_X - 2 * ARROW_W, sc = gfx_text_width(t, 3) <= nw ? 3 : 2;
        gfx_text_center_in(PREV_X + ARROW_W, nw, (TOP_H - 7 * sc) / 2, t, sc, c_text);
    }
#else
    gfx_text_center_in(PREV_X + ARROW_W, NEXT_X - PREV_X - 2 * ARROW_W, (TOP_H - 12) / 2, t, 2, c_text);
#endif
    // right: the link state
    pad_state_t st = bt_pad_state();
    uint16_t dot;
    const char *s;
    switch (st) {
    case PAD_READY:
        dot = rgb(80, 255, 120);
        s = "CONNECTED";
        break;
    case PAD_CONNECTED:
        dot = rgb(255, 210, 60);
        s = "PAIRING";
        break;
    case PAD_ADVERTISING:
        dot = ((s_now / 400) & 1) ? rgb(80, 160, 255) : rgb(30, 60, 110);
        s = "VISIBLE";
        break;
    case PAD_FAILED:
        dot = rgb(255, 70, 70);
        s = "BT ERROR";
        break;
    default:
        dot = rgb(120, 120, 140);
        s = "STARTING";
        break;
    }
    bt_rune(NEXT_X + 22, TOP_H / 2, 1, rgb(110, 170, 255));
    gfx_fill_circle(NEXT_X + 40, TOP_H / 2, 5, dot);
#if BOARD_UI_TABLET
    gfx_text(NEXT_X + 52, (TOP_H - 14) / 2, s, 2, c_text);
#else
    gfx_text(NEXT_X + 50, (TOP_H - 8) / 2, s, 1, c_text);
#endif
}

#else
static void draw_top(void)
{
    gfx_fill_rect(0, 0, SCR_W, TOP_H, rgb(16, 18, 40));
    gfx_fill_rect(0, TOP_H - 1, SCR_W, 1, rgb(50, 56, 100));
    // left: the pull handle (tilt modes) or the title
    bool tilt = mode_now() != PAD_MODE_BUTTONS;
    if (tilt) {
        draw_pull_handle(3, CENTER_W - 6);
    } else {
        gfx_text(6, 10, "GAMEPAD", 1, c_accent);
        if (no_tilt()) {
            gfx_text(6, 22, "NO TILT", 1, rgb(255, 120, 120));
        }
    }
    // middle: the profile (the tilt modes: its name only; < > are in the panel)
    char t[32];
    snprintf(t, sizeof(t), "%d. %s", pad_active() + 1, s_p.name);
    rrect(PREV_X, 4, NEXT_X - PREV_X, TOP_H - 8, 8, rgb(30, 34, 66));
    if (!tilt) {
        gfx_text(PREV_X + 12, 13, "<", 2, c_accent);
        gfx_text(NEXT_X - 22, 13, ">", 2, c_accent);
    }
    int nw = NEXT_X - PREV_X - 2 * ARROW_W;
    int sc = gfx_text_width(t, 2) <= nw ? 2 : 1;
    gfx_text_center_in(PREV_X + ARROW_W, nw, sc == 2 ? 13 : 16, t, sc, c_text);
    // right: the link state (the rune, a lamp, and a word under them)
    pad_state_t st = bt_pad_state();
    uint16_t dot;
    const char *s;
    switch (st) {
    case PAD_READY:
        dot = rgb(80, 255, 120);
        s = "CONNECTED";
        break;
    case PAD_CONNECTED:
        dot = rgb(255, 210, 60);
        s = "PAIRING";
        break;
    case PAD_ADVERTISING:
        dot = ((s_now / 400) & 1) ? rgb(80, 160, 255) : rgb(30, 60, 110);
        s = "VISIBLE";
        break;
    case PAD_FAILED:
        dot = rgb(255, 70, 70);
        s = "BT ERROR";
        break;
    default:
        dot = rgb(120, 120, 140);
        s = "STARTING";
        break;
    }
    int cx = (NEXT_X + SCR_W) / 2;
    bt_rune(cx - 8, 14, 1, rgb(110, 170, 255));
    gfx_fill_circle(cx + 8, 14, 5, dot);
    gfx_text_center_in(NEXT_X, SCR_W - NEXT_X, 27, s, 1, c_text);
}

#endif

// the pull-down panel: SELECT and START (big), CALIBRATE, the profile, CLOSE
static void draw_drawer(void)
{
    if (s_drawer <= 0.01f && !s_pulling) {
        return;
    }
    int dy = drawer_dy();
    int h = SCR_H + dy;
    if (h < 2) {
        h = 2;
    }
    gfx_fill_rect(0, 0, SCR_W, h, rgb(14, 16, 36));
    gfx_fill_rect(0, h - 2, SCR_W, 2, rgb(80, 100, 170));
    bool tilt = mode_now() != PAD_MODE_BUTTONS;
    uint16_t on = rgb(255, 220, 60);
    // SELECT and START
    bool sel = s_screen & bit(BTN_SELECT), sta = s_screen & bit(BTN_START);
    rrect(DR_SEL_X, DR_SS_Y + dy, DR_SS_W, DR_SS_H, 14, sel ? on : rgb(52, 58, 92));
    gfx_text_center_in(DR_SEL_X, DR_SS_W, DR_SS_Y + dy + DR_SS_H / 2 - 7, "SELECT", 2, sel ? rgb(0, 0, 0) : c_text);
    rrect(DR_STA_X, DR_SS_Y + dy, DR_SS_W, DR_SS_H, 14, sta ? on : rgb(52, 58, 92));
    gfx_text_center_in(DR_STA_X, DR_SS_W, DR_SS_Y + dy + DR_SS_H / 2 - 7, "START", 2, sta ? rgb(0, 0, 0) : c_text);
    gfx_text_center_in(DR_SEL_X, DR_STA_X + DR_SS_W - DR_SEL_X, DR_SS_Y + dy - 12 * HELP_S, "HOLD TO PRESS", HELP_S,
                       c_dim);
    // CALIBRATE (the tilt modes); RETRO MOVE: the style (HOLD / LATCH) at its right
    if (tilt) {
        int cw = cal_w(), sc = gfx_text_width("CALIBRATE", 2) <= cw - 8 ? 2 : 1;
        rrect(DR_CAL_X, DR_CAL_Y + dy, cw, DR_CAL_H, 8, rgb(36, 60, 100));
        gfx_text_center_in(DR_CAL_X, cw, DR_CAL_Y + dy + DR_CAL_H / 2 - (sc == 2 ? 7 : 3), "CALIBRATE", sc,
                           rgb(160, 220, 255));
        if (mode_now() == PAD_MODE_MOVE) {
            int sx = DR_CAL_X + cw + 4, sw = DR_CAL_W - cw - 4;
            const char *st = s_p.mstyle == PAD_MOVE_LATCH ? "LATCH" : "HOLD";
            int ss = gfx_text_width(st, 2) <= sw - 8 ? 2 : 1;
            rrect(sx, DR_CAL_Y + dy, sw, DR_CAL_H, 8, rgb(60, 44, 100));
            gfx_text_center_in(sx, sw, DR_CAL_Y + dy + DR_CAL_H / 2 - (ss == 2 ? 7 : 3), st, ss, rgb(255, 210, 120));
        }
    }
    // < profile >
    char t[32];
    snprintf(t, sizeof(t), "%d. %s", pad_active() + 1, s_p.name);
    rrect(DR_PRO_X, DR_PRO_Y + dy, DR_PRO_W, DR_PRO_H, 8, rgb(30, 34, 66));
    gfx_text(DR_PRO_X + 10, DR_PRO_Y + dy + DR_PRO_H / 2 - 7, "<", 2, c_accent);
    gfx_text(DR_PRO_X + DR_PRO_W - 22, DR_PRO_Y + dy + DR_PRO_H / 2 - 7, ">", 2, c_accent);
    int nw = DR_PRO_W - 64, sc = gfx_text_width(t, 2) <= nw ? 2 : 1;
    gfx_text_center_in(DR_PRO_X + 32, nw, DR_PRO_Y + dy + DR_PRO_H / 2 - (sc == 2 ? 7 : 3), t, sc, c_text);
    // CLOSE
    gfx_fill_rect(0, DR_CLOSE_Y + dy, SCR_W, SCR_H - DR_CLOSE_Y - 2, rgb(24, 28, 56));
    gfx_text_center_in(0, SCR_W, DR_CLOSE_Y + dy + (SCR_H - DR_CLOSE_Y) / 2 - 5 * HELP_S, "CLOSE  (OR DRAG UP)", HELP_S,
                       rgb(160, 220, 255));
}

// the help text of the link, in the middle (two lines at y)
static void draw_link_help(int x, int w, int y)
{
    char t[48];
    switch (bt_pad_state()) {
    case PAD_ADVERTISING:
        gfx_text_center_in(x, w, y, "OPEN BLUETOOTH ON YOUR", HELP_S, c_dim);
        gfx_text_center_in(x, w, y + 10 * HELP_S, "COMPUTER OR PHONE AND CHOOSE", HELP_S, c_dim);
        snprintf(t, sizeof(t), "%s", bt_pad_name());
        gfx_text_center_in(x, w, y + 22 * HELP_S, t, HELP_S, rgb(120, 190, 255));
        break;
    case PAD_CONNECTED:
        gfx_text_center_in(x, w, y + 8 * HELP_S, "PAIRING... ACCEPT IT ON THE", HELP_S, c_dim);
        gfx_text_center_in(x, w, y + 18 * HELP_S, "COMPUTER OR PHONE", HELP_S, c_dim);
        break;
    case PAD_READY:
        snprintf(t, sizeof(t), "REPORTS %lu", (unsigned long)bt_pad_sent());
        gfx_text_center_in(x, w, y + 12 * HELP_S, t, HELP_S, rgb(90, 140, 110));
        break;
    case PAD_FAILED:
        gfx_text_center_in(x, w, y + 8 * HELP_S, "BLUETOOTH DID NOT START", HELP_S, rgb(255, 120, 120));
        gfx_text_center_in(x, w, y + 18 * HELP_S, "(NOT ENOUGH MEMORY?)", HELP_S, c_dim);
        break;
    default:
        gfx_text_center_in(x, w, y + 12 * HELP_S, "STARTING BLUETOOTH...", HELP_S, c_dim);
        break;
    }
}

static void draw_web_line(int x, int w)
{
    char t[48];
    if (wifi_web_ip()) {
        snprintf(t, sizeof(t), "WEB SETUP: http://%s/pad", wifi_web_ip());
        gfx_text_center_in(x, w, WEB_Y, t, HELP_S, rgb(110, 120, 170));
    } else {
        gfx_text_center_in(x, w, WEB_Y, "WIFI OR HOTSPOT: EDIT THE PROFILES ON THE WEB", HELP_S, rgb(80, 86, 130));
    }
}

// the inner ring of the stick picture, a part of the radius: the dead zone of the tilt, or
// (RETRO MOVE) the speed for a press
static float stick_ring(void)
{
    if (mode_now() == PAD_MODE_MOVE) {
        return move_v_on() / MOVE_V_FULL;
    }
    return s_p.range > 0 ? (float)s_p.dead / (float)s_p.range : 0;
}

// a stick picture: a ring, the dead zone (ring: a part of r, 0 = none), the stick dot
static void draw_stick(int cx, int cy, int r, float sx, float sy, float ring, uint16_t c)
{
    gfx_fill_circle(cx, cy, r, rgb(22, 26, 52));
    gfx_ring(cx, cy, r, r - 2, rgb(60, 70, 120));
    if (ring > 0) {
        int dr = (int)(r * ring);
        if (dr > r - 3) {
            dr = r - 3;
        }
        if (dr > 2) {
            gfx_ring(cx, cy, dr, dr - 1, rgb(70, 60, 90));
        }
    }
    gfx_fill_rect(cx - r + 4, cy, 2 * r - 8, 1, rgb(40, 46, 80));
    gfx_fill_rect(cx, cy - r + 4, 1, 2 * r - 8, rgb(40, 46, 80));
    int px = cx + (int)(sx * (r - 8)), py = cy + (int)(sy * (r - 8));
    line(cx, cy, px, py, 3, rgb(90, 100, 150));
    gfx_fill_circle(px, py, 8, c);
    gfx_fill_circle(px - 2, py - 2, 3, rgb(255, 255, 255));
}

// 8 direction lamps around (cx, cy)
static void draw_compass(int cx, int cy, int r)
{
    for (int d = 0; d < 8; d++) {
        if (s_p.dirs == 4 && (d & 1)) {
            continue;
        }
        int x = cx + DX[d] * r * ((d & 1) ? 7 : 10) / 10;
        int y = cy + DY[d] * r * ((d & 1) ? 7 : 10) / 10;
        bool on = s_dir == d;
        gfx_fill_circle(x, y, on ? 8 : 6, on ? rgb(255, 220, 60) : rgb(50, 56, 96));
    }
}

#if BOARD_BAR_SCREEN
static void draw_retro(void)
{
    // left: the direction lamps and the tilt
    draw_compass(DPAD_X, DPAD_Y, 52);
    draw_stick(DPAD_X, DPAD_Y, 34, s_sx, s_sy, stick_ring(), s_dir >= 0 ? rgb(255, 220, 60) : rgb(150, 160, 200));
    if (mode_now() == PAD_MODE_MOVE) {
        gfx_text_center_in(DPAD_X - 100, 200, 160,
                           s_p.mstyle == PAD_MOVE_LATCH ? "MOVE IT: LATCH (LIFT = STOP)" : "MOVE IT: HOLD", 1,
                           rgb(255, 210, 120));
    }
    // middle: the joystick, it leans to the direction
    int bx = 320, by = 132;
    gfx_fill_ellipse(bx, by + 6, 74, 16, rgb(8, 8, 14));
    gfx_fill_ellipse(bx, by, 72, 16, rgb(40, 40, 46));
    gfx_fill_ellipse(bx, by - 2, 64, 12, rgb(24, 24, 28));
    gfx_fill_circle(bx - 52, by - 4, 9, s_fire ? rgb(255, 80, 60) : rgb(170, 30, 30));  // the fire button
    int kx = bx + (int)(s_jx * 46), ky = by - 64 + (int)(s_jy * 18);
    line(bx, by - 4, kx, ky + 8, 7, rgb(30, 30, 34));
    line(bx, by - 4, kx, ky + 8, 3, rgb(70, 70, 80));
    gfx_fill_circle(kx, ky, 13, rgb(20, 20, 24));
    gfx_fill_circle(kx - 4, ky - 4, 4, rgb(110, 110, 120));
    draw_link_help(220, 200, 34);
    // right: FIRE
    bool f = s_fire;
    uint16_t fc = f ? rgb(255, 90, 60) : rgb(140, 30, 30);
    if (f) {
        gfx_fill_circle(ABXY_X, ABXY_Y, 50, rgb(90, 20, 20));
    }
    gfx_fill_circle(ABXY_X, ABXY_Y + 4, 40, rgb(60, 10, 10));
    gfx_fill_circle(ABXY_X, ABXY_Y, 40, fc);
    gfx_fill_circle(ABXY_X - 12, ABXY_Y - 12, 10, f ? rgb(255, 180, 160) : rgb(200, 70, 70));
    gfx_text_center_in(ABXY_X - 40, 80, ABXY_Y - 4, "FIRE", 2, c_text);
    char t[24];
    if (s_p.autofire) {
        snprintf(t, sizeof(t), "AUTO %d/S", s_p.autofire);
        gfx_text_center_in(ABXY_X - 50, 100, ABXY_Y + 46, t, 1, c_dim);
    } else {
        gfx_text_center_in(ABXY_X - 60, 120, ABXY_Y + 46, "TOUCH ANYWHERE", 1, c_dim);
    }
}

#elif BOARD_UI_TABLET
static void draw_retro(void)
{
    // left: the direction lamps and the tilt
    draw_compass(DPAD_X, DPAD_Y, 84);
    draw_stick(DPAD_X, DPAD_Y, 56, s_sx, s_sy, stick_ring(), s_dir >= 0 ? rgb(255, 220, 60) : rgb(150, 160, 200));
    if (mode_now() == PAD_MODE_MOVE) {
        gfx_text_center_in(0, 2 * DPAD_X, TOP_H + 8, s_p.mstyle == PAD_MOVE_LATCH ? "MOVE: LATCH" : "MOVE: HOLD", 2,
                           rgb(255, 210, 120));
    }
    // middle: the joystick, it leans to the direction
    int bx = 320, by = 230;
    gfx_fill_ellipse(bx, by + 8, 76, 18, rgb(8, 8, 14));
    gfx_fill_ellipse(bx, by, 72, 18, rgb(40, 40, 46));
    gfx_fill_ellipse(bx, by - 2, 62, 13, rgb(24, 24, 28));
    gfx_fill_circle(bx - 50, by - 4, 11, s_fire ? rgb(255, 80, 60) : rgb(170, 30, 30));  // the fire button
    int kx = bx + (int)(s_jx * 46), ky = by - 92 + (int)(s_jy * 20);
    line(bx, by - 4, kx, ky + 10, 9, rgb(30, 30, 34));
    line(bx, by - 4, kx, ky + 10, 4, rgb(70, 70, 80));
    gfx_fill_circle(kx, ky, 18, rgb(20, 20, 24));
    gfx_fill_circle(kx - 5, ky - 5, 5, rgb(110, 110, 120));
    draw_link_help(0, SCR_W, HELP_Y);
    // right: FIRE
    bool f = s_fire;
    uint16_t fc = f ? rgb(255, 90, 60) : rgb(140, 30, 30);
    if (f) {
        gfx_fill_circle(ABXY_X, ABXY_Y, 82, rgb(90, 20, 20));
    }
    gfx_fill_circle(ABXY_X, ABXY_Y + 6, 70, rgb(60, 10, 10));
    gfx_fill_circle(ABXY_X, ABXY_Y, 70, fc);
    gfx_fill_circle(ABXY_X - 22, ABXY_Y - 22, 16, f ? rgb(255, 180, 160) : rgb(200, 70, 70));
    gfx_text_center_in(ABXY_X - 70, 140, ABXY_Y - 10, "FIRE", 3, c_text);
    char t[24];
    if (s_p.autofire) {
        snprintf(t, sizeof(t), "AUTO %d/S", s_p.autofire);
        gfx_text_center_in(ABXY_X - 100, 200, ABXY_Y + 80, t, 2, c_dim);
    } else {
        gfx_text_center_in(ABXY_X - 100, 200, ABXY_Y + 80, "TOUCH ANYWHERE", 2, c_dim);
    }
}

#else
static void draw_retro(void)
{
    // left: the direction lamps and the tilt
    draw_compass(DPAD_X, DPAD_Y, 50);
    draw_stick(DPAD_X, DPAD_Y, 32, s_sx, s_sy, stick_ring(), s_dir >= 0 ? rgb(255, 220, 60) : rgb(150, 160, 200));
    if (mode_now() == PAD_MODE_MOVE) {
        gfx_text_center_in(0, 2 * DPAD_X, TOP_H + 6, s_p.mstyle == PAD_MOVE_LATCH ? "MOVE: LATCH" : "MOVE: HOLD", 1,
                           rgb(255, 210, 120));
    }
    // middle: the joystick, it leans to the direction
    int bx = 160, by = 168;
    gfx_fill_ellipse(bx, by + 4, 40, 10, rgb(8, 8, 14));
    gfx_fill_ellipse(bx, by, 38, 10, rgb(40, 40, 46));
    gfx_fill_ellipse(bx, by - 1, 33, 7, rgb(24, 24, 28));
    gfx_fill_circle(bx - 27, by - 2, 6, s_fire ? rgb(255, 80, 60) : rgb(170, 30, 30));  // the fire button
    int kx = bx + (int)(s_jx * 24), ky = by - 44 + (int)(s_jy * 10);
    line(bx, by - 2, kx, ky + 6, 5, rgb(30, 30, 34));
    line(bx, by - 2, kx, ky + 6, 2, rgb(70, 70, 80));
    gfx_fill_circle(kx, ky, 10, rgb(20, 20, 24));
    gfx_fill_circle(kx - 3, ky - 3, 3, rgb(110, 110, 120));
    draw_link_help(0, SCR_W, HELP_Y);
    // right: FIRE
    bool f = s_fire;
    uint16_t fc = f ? rgb(255, 90, 60) : rgb(140, 30, 30);
    if (f) {
        gfx_fill_circle(ABXY_X, ABXY_Y, 46, rgb(90, 20, 20));
    }
    gfx_fill_circle(ABXY_X, ABXY_Y + 4, 38, rgb(60, 10, 10));
    gfx_fill_circle(ABXY_X, ABXY_Y, 38, fc);
    gfx_fill_circle(ABXY_X - 11, ABXY_Y - 11, 9, f ? rgb(255, 180, 160) : rgb(200, 70, 70));
    gfx_text_center_in(ABXY_X - 38, 76, ABXY_Y - 4, "FIRE", 2, c_text);
    char t[24];
    if (s_p.autofire) {
        snprintf(t, sizeof(t), "AUTO %d/S", s_p.autofire);
        gfx_text_center_in(ABXY_X - 50, 100, ABXY_Y + 46, t, 1, c_dim);
    } else {
        gfx_text_center_in(ABXY_X - 56, 112, ABXY_Y + 46, "TOUCH ANYWHERE", 1, c_dim);
    }
}

#endif

static void draw_dpad(void)
{
    uint16_t base = rgb(40, 44, 60), lit = rgb(255, 220, 60);
    int w = DPAD_W, a = DPAD_ARM;
    gfx_fill_rect(DPAD_X - w / 2 + 2, DPAD_Y - a + 3, w, 2 * a, rgb(10, 10, 18));  // shadow
    gfx_fill_rect(DPAD_X - a + 2, DPAD_Y - w / 2 + 3, 2 * a, w, rgb(10, 10, 18));
    gfx_fill_rect(DPAD_X - w / 2, DPAD_Y - a, w, 2 * a, base);
    gfx_fill_rect(DPAD_X - a, DPAD_Y - w / 2, 2 * a, w, base);
    // lit arms (two for a diagonal)
    if (s_dir >= 0) {
        int dx = DX[s_dir], dy = DY[s_dir];
        if (dy < 0) {
            gfx_fill_rect(DPAD_X - w / 2 + 3, DPAD_Y - a + 3, w - 6, a - w / 2, lit);
        }
        if (dy > 0) {
            gfx_fill_rect(DPAD_X - w / 2 + 3, DPAD_Y + w / 2, w - 6, a - w / 2 - 3, lit);
        }
        if (dx < 0) {
            gfx_fill_rect(DPAD_X - a + 3, DPAD_Y - w / 2 + 3, a - w / 2, w - 6, lit);
        }
        if (dx > 0) {
            gfx_fill_rect(DPAD_X + w / 2, DPAD_Y - w / 2 + 3, a - w / 2 - 3, w - 6, lit);
        }
    }
    gfx_fill_circle(DPAD_X, DPAD_Y, 9, rgb(30, 32, 46));
    // small arrows at the ends of the arms
    uint16_t ac = rgb(150, 156, 180);
    for (int j = 0; j < 7; j++) {
        gfx_fill_rect(DPAD_X - j, DPAD_Y - a + 8 + j, 2 * j + 1, 1, ac);               // up
        gfx_fill_rect(DPAD_X - (6 - j), DPAD_Y + a - 14 + j, 2 * (6 - j) + 1, 1, ac);  // down
        gfx_fill_rect(DPAD_X - a + 8 + j, DPAD_Y - j, 1, 2 * j + 1, ac);               // left
        gfx_fill_rect(DPAD_X + a - 14 + j, DPAD_Y - (6 - j), 1, 2 * (6 - j) + 1, ac);  // right
    }
}

static void face_button(int x, int y, const char *l, uint16_t c, bool on)
{
    if (on) {
        gfx_fill_circle(x, y, ABXY_R + 6, rgb(255, 255, 255));
    }
    gfx_fill_circle(x + 1, y + 3, ABXY_R, rgb(10, 10, 18));
    gfx_fill_circle(x, y, ABXY_R, on ? c : rgb(40, 44, 60));
    gfx_ring(x, y, ABXY_R, ABXY_R - 3, c);
#if BOARD_UI_TABLET
    gfx_text_center_in(x - ABXY_R, 2 * ABXY_R, y - 10, l, 3, on ? rgb(0, 0, 0) : c);
#else
    gfx_text_center_in(x - ABXY_R, 2 * ABXY_R, y - 7, l, 2, on ? rgb(0, 0, 0) : c);
#endif
}

static void pill(int x, int y, const char *l, bool on)
{
    rrect(x, y, SS_W, SS_H, SS_H / 2, on ? rgb(255, 220, 60) : rgb(50, 54, 76));
#if BOARD_UI_TABLET
    gfx_text_center_in(x, SS_W, y + (SS_H - 14) / 2, l, 2, on ? rgb(0, 0, 0) : c_dim);
#else
    gfx_text_center_in(x, SS_W, y + (SS_H - 7) / 2, l, 1, on ? rgb(0, 0, 0) : c_dim);
#endif
}

static void draw_buttons_mode(void)
{
    // the body of a pad: two grips and a middle part
    gfx_fill_ellipse(DPAD_X, DPAD_Y + 6, BODY_RX, BODY_RY, rgb(30, 32, 56));
    gfx_fill_ellipse(ABXY_X, ABXY_Y + 6, BODY_RX, BODY_RY, rgb(30, 32, 56));
    gfx_fill_rect(DPAD_X, BODY_Y, ABXY_X - DPAD_X, BODY_H, rgb(30, 32, 56));
    gfx_fill_rect(DPAD_X, BODY_Y, ABXY_X - DPAD_X, 3, rgb(56, 60, 96));
    draw_dpad();
    uint16_t on = s_rep.buttons;
    face_button(ABXY_X, ABXY_Y - ABXY_OFF, "Y", rgb(250, 210, 60), on & bit(BTN_Y));
    face_button(ABXY_X - ABXY_OFF, ABXY_Y, "X", rgb(80, 150, 255), on & bit(BTN_X));
    face_button(ABXY_X + ABXY_OFF, ABXY_Y, "B", rgb(255, 80, 80), on & bit(BTN_B));
    face_button(ABXY_X, ABXY_Y + ABXY_OFF, "A", rgb(90, 220, 110), on & bit(BTN_A));
    pill(SEL_X, SS_Y, "SELECT", on & bit(BTN_SELECT));
    pill(STA_X, SS_Y, "START", on & bit(BTN_START));
#if BOARD_BAR_SCREEN
    draw_link_help(DPAD_ZONE, ABXY_X - 60 - DPAD_ZONE, 48);
#else
    draw_link_help(0, SCR_W, HELP_Y);
#endif
}

#if BOARD_BAR_SCREEN
static void draw_motion(void)
{
    uint16_t on = s_rep.buttons;
    if (s_p.touch == PAD_TOUCH_HALVES) {
        // the two halves of the screen are buttons
        if (s_p.bl && (on & bit(s_p.bl))) {
            gfx_fill_rect(0, TOP_H, SCR_W / 2, SCR_H - TOP_H, rgb(30, 50, 80));
        }
        if (s_p.br && (on & bit(s_p.br))) {
            gfx_fill_rect(SCR_W / 2, TOP_H, SCR_W / 2, SCR_H - TOP_H, rgb(30, 50, 80));
        }
        gfx_fill_rect(SCR_W / 2, TOP_H + 6, 1, SCR_H - TOP_H - 26, rgb(60, 66, 110));
        char t[16];
        if (s_p.bl) {
            snprintf(t, sizeof(t), "B%d", s_p.bl);
        } else {
            snprintf(t, sizeof(t), "-");
        }
        gfx_text_center_in(200, 110, 70, t, 3, (s_p.bl && (on & bit(s_p.bl))) ? c_accent : rgb(90, 100, 150));
        if (s_p.br) {
            snprintf(t, sizeof(t), "B%d", s_p.br);
        } else {
            snprintf(t, sizeof(t), "-");
        }
        gfx_text_center_in(450, 120, 70, t, 3, (s_p.br && (on & bit(s_p.br))) ? c_accent : rgb(90, 100, 150));
        gfx_text_center_in(200, 110, 100, "TOUCH", 1, c_dim);
        gfx_text_center_in(450, 120, 100, "TOUCH", 1, c_dim);
    } else if (s_p.touch == PAD_TOUCH_PAD) {
        rrect(400, 36, 230, 116, 12, rgb(24, 28, 54));
        gfx_frame_rect(400, 36, 230, 116, 1, rgb(60, 70, 120));
        gfx_text(408, 42, "TOUCHPAD: RIGHT STICK", 1, c_dim);
        int cx = 515, cy = 98;
        gfx_fill_rect(cx - 50, cy, 100, 1, rgb(50, 56, 96));
        gfx_fill_rect(cx, cy - 44, 1, 88, rgb(50, 56, 96));
        gfx_fill_circle(cx + (int)(s_rx * 44), cy + (int)(s_ry * 40), 9, s_tp_on ? c_accent : rgb(90, 100, 150));
        char t[24];
        if (s_p.bl) {
            snprintf(t, sizeof(t), "TAP = B%d", s_p.bl);
            gfx_text(408, 140, t, 1, c_dim);
        }
    } else {
        char t[24];
        if (s_p.bl) {
            snprintf(t, sizeof(t), "TOUCH = B%d", s_p.bl);
        } else {
            snprintf(t, sizeof(t), "TOUCH: -");
        }
        gfx_text_center_in(400, 230, 92, t, 2, (s_p.bl && (on & bit(s_p.bl))) ? c_accent : rgb(90, 100, 150));
    }
    draw_stick(DPAD_X, DPAD_Y, 52, s_sx, s_sy, stick_ring(), rgb(90, 220, 255));
    char t[24];
    snprintf(t, sizeof(t), "%+.0f %+.0f DEG", s_deg_x, s_p.usey ? s_deg_y : 0.0f);
    gfx_text_center_in(DPAD_X - 60, 120, 154, t, 1, c_dim);
    // the gesture list
    static const char *const GN[7] = {"UP", "DOWN", "LEFT", "RIGHT", "KNOCK", "SHAKE", "SNAP"};
    const uint8_t gb[7] = {s_p.gu, s_p.gd, s_p.gl, s_p.gr, s_p.gk, s_p.gs, s_p.gn};
    int y = 38;
    for (int i = 0; i < 7; i++) {
        if (!gb[i]) {
            continue;
        }
        snprintf(t, sizeof(t), "%s B%d", GN[i], gb[i]);
        bool lit = s_gest_until[gb[i]] > s_now;
        gfx_text(s_p.touch == PAD_TOUCH_PAD ? 330 : 240, y, t, 1, lit ? c_accent : rgb(100, 110, 160));
        y += 10;
    }
    if (s_p.touch != PAD_TOUCH_HALVES) {
        draw_link_help(180, 210, 118);
    }
}

#elif BOARD_UI_TABLET
static void draw_motion(void)
{
    uint16_t on = s_rep.buttons;
    char t[24];
    int gx = 250, gy = TOP_H + 16, gstep = 26;  // the gesture list
    if (s_p.touch == PAD_TOUCH_HALVES) {
        // the two halves of the screen are buttons
        int hy = TOP_H, hh = HELP_Y - 4 - TOP_H;
        if (s_p.bl && (on & bit(s_p.bl))) {
            gfx_fill_rect(0, hy, SCR_W / 2, hh, rgb(30, 50, 80));
        }
        if (s_p.br && (on & bit(s_p.br))) {
            gfx_fill_rect(SCR_W / 2, hy, SCR_W / 2, hh, rgb(30, 50, 80));
        }
        gfx_fill_rect(SCR_W / 2, hy + 8, 1, hh - 16, rgb(60, 66, 110));
        if (s_p.bl) {
            snprintf(t, sizeof(t), "B%d", s_p.bl);
        } else {
            snprintf(t, sizeof(t), "-");
        }
        gfx_text_center_in(0, SCR_W / 2, 220, t, 4, (s_p.bl && (on & bit(s_p.bl))) ? c_accent : rgb(90, 100, 150));
        if (s_p.br) {
            snprintf(t, sizeof(t), "B%d", s_p.br);
        } else {
            snprintf(t, sizeof(t), "-");
        }
        gfx_text_center_in(SCR_W / 2, SCR_W / 2, 220, t, 4,
                           (s_p.br && (on & bit(s_p.br))) ? c_accent : rgb(90, 100, 150));
        gfx_text_center_in(0, SCR_W / 2, 262, "TOUCH", 2, c_dim);
        gfx_text_center_in(SCR_W / 2, SCR_W / 2, 262, "TOUCH", 2, c_dim);
        gx = SCR_W / 2 + 20;
        gstep = 20;
    } else if (s_p.touch == PAD_TOUCH_PAD) {
        rrect(380, TOP_H + 10, 246, 210, 14, rgb(24, 28, 54));
        gfx_frame_rect(380, TOP_H + 10, 246, 210, 1, rgb(60, 70, 120));
        gfx_text(392, TOP_H + 20, "RIGHT STICK", 2, c_dim);
        int cx = 503, cy = TOP_H + 115;
        gfx_fill_rect(cx - 90, cy, 180, 1, rgb(50, 56, 96));
        gfx_fill_rect(cx, cy - 80, 1, 160, rgb(50, 56, 96));
        gfx_fill_circle(cx + (int)(s_rx * 84), cy + (int)(s_ry * 76), 14, s_tp_on ? c_accent : rgb(90, 100, 150));
        if (s_p.bl) {
            snprintf(t, sizeof(t), "TAP = B%d", s_p.bl);
            gfx_text(392, TOP_H + 198, t, 2, c_dim);
        }
    } else {
        if (s_p.bl) {
            snprintf(t, sizeof(t), "TOUCH = B%d", s_p.bl);
        } else {
            snprintf(t, sizeof(t), "TOUCH: -");
        }
        gfx_text_center_in(380, 246, 150, t, 3, (s_p.bl && (on & bit(s_p.bl))) ? c_accent : rgb(90, 100, 150));
        gfx_text_center_in(380, 246, 186, "ANYWHERE", 2, c_dim);
    }
    bool halves = s_p.touch == PAD_TOUCH_HALVES;
    int sx = halves ? SCR_W / 4 : DPAD_X - 16, sy = halves ? 136 : DPAD_Y - 12;
    draw_stick(sx, sy, halves ? 64 : 84, s_sx, s_sy, stick_ring(), rgb(90, 220, 255));
    snprintf(t, sizeof(t), "%+.0f %+.0f DEG", s_deg_x, s_p.usey ? s_deg_y : 0.0f);
    if (!halves) {
        gfx_text_center_in(sx - 110, 220, sy + 94, t, 2, c_dim);
    }
    // the gesture list
    static const char *const GN[7] = {"UP", "DOWN", "LEFT", "RIGHT", "KNOCK", "SHAKE", "SNAP"};
    const uint8_t gb[7] = {s_p.gu, s_p.gd, s_p.gl, s_p.gr, s_p.gk, s_p.gs, s_p.gn};
    for (int i = 0; i < 7; i++) {
        if (!gb[i]) {
            continue;
        }
        snprintf(t, sizeof(t), "%s B%d", GN[i], gb[i]);
        bool lit = s_gest_until[gb[i]] > s_now;
        gfx_text(gx, gy, t, 2, lit ? c_accent : rgb(100, 110, 160));
        gy += gstep;
    }
    if (s_p.touch != PAD_TOUCH_HALVES) {
        draw_link_help(0, SCR_W, HELP_Y);
    }
}

#else
static void draw_motion(void)
{
    uint16_t on = s_rep.buttons;
    char t[24];
    int gx = 128;  // the gesture list
    if (s_p.touch == PAD_TOUCH_HALVES) {
        // the two halves of the screen are buttons
        int hy = TOP_H, hh = WEB_Y - 4 - TOP_H;
        if (s_p.bl && (on & bit(s_p.bl))) {
            gfx_fill_rect(0, hy, SCR_W / 2, hh, rgb(30, 50, 80));
        }
        if (s_p.br && (on & bit(s_p.br))) {
            gfx_fill_rect(SCR_W / 2, hy, SCR_W / 2, hh, rgb(30, 50, 80));
        }
        gfx_fill_rect(SCR_W / 2, hy + 6, 1, hh - 12, rgb(60, 66, 110));
        if (s_p.bl) {
            snprintf(t, sizeof(t), "B%d", s_p.bl);
        } else {
            snprintf(t, sizeof(t), "-");
        }
        gfx_text_center_in(0, SCR_W / 2, 176, t, 3, (s_p.bl && (on & bit(s_p.bl))) ? c_accent : rgb(90, 100, 150));
        if (s_p.br) {
            snprintf(t, sizeof(t), "B%d", s_p.br);
        } else {
            snprintf(t, sizeof(t), "-");
        }
        gfx_text_center_in(SCR_W / 2, SCR_W / 2, 176, t, 3,
                           (s_p.br && (on & bit(s_p.br))) ? c_accent : rgb(90, 100, 150));
        gfx_text_center_in(0, SCR_W / 2, 204, "TOUCH", 1, c_dim);
        gfx_text_center_in(SCR_W / 2, SCR_W / 2, 204, "TOUCH", 1, c_dim);
        gx = SCR_W / 2 + 10;
    } else if (s_p.touch == PAD_TOUCH_PAD) {
        rrect(186, 44, 130, 142, 10, rgb(24, 28, 54));
        gfx_frame_rect(186, 44, 130, 142, 1, rgb(60, 70, 120));
        gfx_text(192, 50, "TOUCHPAD:", 1, c_dim);
        gfx_text(192, 60, "RIGHT STICK", 1, c_dim);
        int cx = 251, cy = 120;
        gfx_fill_rect(cx - 50, cy, 100, 1, rgb(50, 56, 96));
        gfx_fill_rect(cx, cy - 44, 1, 88, rgb(50, 56, 96));
        gfx_fill_circle(cx + (int)(s_rx * 44), cy + (int)(s_ry * 40), 9, s_tp_on ? c_accent : rgb(90, 100, 150));
        if (s_p.bl) {
            snprintf(t, sizeof(t), "TAP = B%d", s_p.bl);
            gfx_text(192, 172, t, 1, c_dim);
        }
    } else {
        if (s_p.bl) {
            snprintf(t, sizeof(t), "TOUCH = B%d", s_p.bl);
        } else {
            snprintf(t, sizeof(t), "TOUCH: -");
        }
        gfx_text_center_in(186, 130, 104, t, 2, (s_p.bl && (on & bit(s_p.bl))) ? c_accent : rgb(90, 100, 150));
        gfx_text_center_in(186, 130, 126, "ANYWHERE", 1, c_dim);
    }
    draw_stick(DPAD_X, 108, 46, s_sx, s_sy, stick_ring(), rgb(90, 220, 255));
    snprintf(t, sizeof(t), "%+.0f %+.0f DEG", s_deg_x, s_p.usey ? s_deg_y : 0.0f);
    gfx_text_center_in(2, 120, 160, t, 1, c_dim);
    // the gesture list
    static const char *const GN[7] = {"UP", "DOWN", "LEFT", "RIGHT", "KNOCK", "SHAKE", "SNAP"};
    const uint8_t gb[7] = {s_p.gu, s_p.gd, s_p.gl, s_p.gr, s_p.gk, s_p.gs, s_p.gn};
    int y = 46;
    for (int i = 0; i < 7; i++) {
        if (!gb[i]) {
            continue;
        }
        snprintf(t, sizeof(t), "%s B%d", GN[i], gb[i]);
        bool lit = s_gest_until[gb[i]] > s_now;
        gfx_text(gx, y, t, 1, lit ? c_accent : rgb(100, 110, 160));
        y += 10;
    }
    if (s_p.touch != PAD_TOUCH_HALVES) {
        draw_link_help(0, SCR_W, HELP_Y);
    }
}

#endif

#if BOARD_BAR_SCREEN
static void draw_confirm(void)
{
    gfx_darken_rect(0, 0, SCR_W, SCR_H, 2);
    rrect(150, 30, 340, 120, 14, rgb(36, 22, 40));
    gfx_frame_rect(150, 30, 340, 120, 2, rgb(255, 110, 110));
    gfx_text_center_in(150, 340, 44, "FORGET ALL PAIRED", 2, c_text);
    gfx_text_center_in(150, 340, 64, "COMPUTERS AND PHONES?", 2, c_text);
    rrect(200, 96, 110, 44, 10, rgb(200, 50, 60));
    gfx_text_center_in(200, 110, 111, "YES", 2, c_text);
    rrect(330, 96, 110, 44, 10, rgb(60, 66, 100));
    gfx_text_center_in(330, 110, 111, "NO", 2, c_text);
}

#elif BOARD_UI_TABLET
static void draw_confirm(void)
{
    gfx_darken_rect(0, 0, SCR_W, SCR_H, 2);
    rrect(110, 70, 420, 220, 18, rgb(36, 22, 40));
    gfx_frame_rect(110, 70, 420, 220, 2, rgb(255, 110, 110));
    gfx_text_center_in(110, 420, 100, "FORGET ALL PAIRED", 3, c_text);
    gfx_text_center_in(110, 420, 136, "COMPUTERS AND PHONES?", 2, c_text);
    rrect(160, 196, 140, 64, 12, rgb(200, 50, 60));
    gfx_text_center_in(160, 140, 218, "YES", 3, c_text);
    rrect(340, 196, 140, 64, 12, rgb(60, 66, 100));
    gfx_text_center_in(340, 140, 218, "NO", 3, c_text);
}

#else
static void draw_confirm(void)
{
    gfx_darken_rect(0, 0, SCR_W, SCR_H, 2);
    rrect(30, 50, 260, 140, 14, rgb(36, 22, 40));
    gfx_frame_rect(30, 50, 260, 140, 2, rgb(255, 110, 110));
    gfx_text_center_in(30, 260, 66, "FORGET ALL PAIRED", 2, c_text);
    gfx_text_center_in(30, 260, 88, "COMPUTERS AND PHONES?", 2, c_text);
    rrect(50, 130, 100, 44, 10, rgb(200, 50, 60));
    gfx_text_center_in(50, 100, 145, "YES", 2, c_text);
    rrect(170, 130, 100, 44, 10, rgb(60, 66, 100));
    gfx_text_center_in(170, 100, 145, "NO", 2, c_text);
}

#endif

static void gp_render(int64_t now)
{
    s_now = now;
    draw_background();
    switch (mode_now()) {
    case PAD_MODE_BUTTONS:
        draw_buttons_mode();
        break;
    case PAD_MODE_MOTION:
        draw_motion();
        break;
    default:
        draw_retro();
        break;
    }
    draw_top();
#if BOARD_BAR_SCREEN
    draw_web_line(200, 240);
#else
    draw_web_line(0, SCR_W);
#endif
    if (s_pop_at && now - s_pop_at < 900) {
        int w = gfx_text_width(s_pop, 2) + 20;
        int y = TOP_H + 6 + (int)((now - s_pop_at) / 60);
        rrect(SCR_W / 2 - w / 2, y, w, 22, 8, rgb(20, 22, 44));
        gfx_text_center_in(SCR_W / 2 - w / 2, w, y + 4, s_pop, 2, s_pop_c);
    }
    if (s_cal != CAL_OFF) {
        // the calibration: a big prompt for each step, with a progress bar
        int w = SCR_W - 40, h = 58, y = TOP_H + (SCR_H - TOP_H) / 2 - h / 2;
        rrect(20, y, w, h, 8, rgb(20, 44, 80));
        uint16_t c = ((now / 300) & 1) ? rgb(255, 230, 90) : rgb(255, 255, 255);
        bool still = s_cal == CAL_SETTLE, mv = mode_now() == PAD_MODE_MOVE;
        const char *l1 = still ? (s_cal_ms < SETTLE_MS ? "1/2  TAKE YOUR POSE..." : "1/2  HOLD STILL")
                         : mv  ? "2/2  MOVE IT FORWARD (5 CM)"
                               : "2/2  TILT IT FORWARD";
        const char *l2 =
                still ? "THE ANGLE YOU LIKE IS STRAIGHT UP. WAIT FOR THE BEEP" : "AWAY FROM YOU: THAT WAY IS UP";
#if BOARD_BAR_SCREEN || BOARD_UI_TABLET
        gfx_text_center_in(20, w, y + 6, l1, 2, c);
        gfx_text_center_in(20, w, y + 26, l2, 1, rgb(160, 220, 255));
#else
        (void)l2;
        gfx_text_center_in(20, w, y + 6, l1 + 5, 2, c);
        gfx_text_center_in(20, w, y + 26, still ? "WAIT FOR THE BEEP" : "AWAY FROM YOU = UP", 1, rgb(160, 220, 255));
#endif
        int ms = still ? s_cal_ms : s_cal_ms, full = still ? SETTLE_MS : CAL_GEST_MS;
        int bw = (w - 40) * (ms > full ? full : ms) / full;
        gfx_fill_rect(40, y + 42, w - 40, 6, rgb(40, 60, 100));
        gfx_fill_rect(40, y + 42, bw, 6, still ? rgb(120, 255, 160) : rgb(255, 230, 90));
    } else if (s_calerr && now - s_calerr_at < 4000) {
        int w = SCR_W - 40, h = 16 + 14 * HELP_S, y = TOP_H + 6;
        rrect(20, y, w, h, 8, rgb(80, 24, 24));
        char t[48];
        snprintf(t, sizeof t, "CALIBRATION: %s. TRY AGAIN", s_calerr);
        gfx_text_center_in(20, w, y + (h - 7 * HELP_S) / 2, t, HELP_S, rgb(255, 190, 170));
    } else if (s_ready_at && now - s_ready_at < 2500) {
        int w = SCR_W - 40, h = 30, y = TOP_H + 6;
        rrect(20, y, w, h, 8, rgb(20, 60, 40));
#if BOARD_BAR_SCREEN || BOARD_UI_TABLET
        gfx_text_center_in(20, w, y + 11,
                           mode_now() == PAD_MODE_MOVE ? "READY: MOVE IT FORWARD = UP, BACK = DOWN, LEFT / RIGHT"
                                                       : "READY: FORWARD = UP, BACK = DOWN, LEAN = LEFT / RIGHT",
                           1, rgb(160, 255, 190));
#else
        gfx_text_center_in(20, w, y + 11,
                           mode_now() == PAD_MODE_MOVE ? "READY: MOVE IT FORWARD = UP" : "READY: FORWARD = UP", 1,
                           rgb(160, 255, 190));
#endif
    }
    draw_drawer();
    if (s_confirm) {
        draw_confirm();
    }
}

#include "../libs/tficon/tficon.h"
// the menu icon: a game pad; its four buttons light one after another, the cross presses with them
static void gp_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x9b6bff), tfi_hex(0x3a1d9a));
    float u = s / 100.0f, ph = tfi_ph(2);
    float cy = y + 54 * u;
    // the body: two grips and the middle
    tfi_drop_disc(x + 29 * u, cy + 6 * u, 17 * u, 3 * u, 70);
    tfi_drop_disc(x + 71 * u, cy + 6 * u, 17 * u, 3 * u, 70);
    tfi_disc(x + 29 * u, cy + 6 * u, 17 * u, tfi_hex(0xf4f6fb), tfi_hex(0xb8c0d4), 256);
    tfi_disc(x + 71 * u, cy + 6 * u, 17 * u, tfi_hex(0xf4f6fb), tfi_hex(0xb8c0d4), 256);
    tfi_rbox(x + (int)(24 * u), (int)(cy - 14 * u), (int)(52 * u), (int)(28 * u), (int)(12 * u), rgb(255, 255, 255),
             tfi_hex(0xd2d8e6), 256);
    // the cross
    int k4 = (int)(ph * 4) & 3;
    uint16_t cr = tfi_hex(0x3a3f52);
    tfi_rbox(x + (int)(19 * u), (int)(cy - 3.5f * u), (int)(20 * u), (int)(7 * u), (int)(2 * u), cr, cr, 256);
    tfi_rbox(x + (int)(25.5f * u), (int)(cy - 10 * u), (int)(7 * u), (int)(20 * u), (int)(2 * u), cr, cr, 256);
    static const float DP[4][2] = {{0, -6}, {6, 0}, {0, 6}, {-6, 0}};
    tfd_disc(x + (29 + DP[k4][0]) * u, cy + DP[k4][1] * u, 2.2f * u, tfi_hex(0x8a90a8), 256);
    // the buttons
    static const float BP[4][2] = {{71, -8}, {79, 0}, {71, 8}, {63, 0}};
    static const uint32_t BC[4] = {0xffc400, 0xff3d57, 0x2ad46a, 0x2f8cff};
    for (int k = 0; k < 4; k++) {
        float q = ph * 4 - k;
        float lit = k == k4 ? fsin((q < 0 ? 0 : q > 1 ? 1 : q) * 3.14159f) : 0;
        uint16_t c = tfi_hex(BC[k]);
        float bx = x + BP[k][0] * u, by = cy + BP[k][1] * u + lit * 0.8f * u;
        if (lit > 0.05f) {
            tfd_glow(bx, by, 10 * u, c, (int)(200 * lit));
        }
        tfi_disc(bx, by, 4.4f * u, tfi_light(c, 60 + (int)(120 * lit)), tfi_dark(c, 50 - (int)(40 * lit)), 256);
    }
    tfi_end();
}

const app_t app_gamepad = {
        .name = "GAMEPAD",
        .landscape = true,
        .needs = 0,  // the BUTTONS mode needs only touch
        .init = gp_init,
        .enter = gp_enter,
        .leave = gp_leave,
        .input = gp_input,
        .update = gp_update,
        .render = gp_render,
        .draw_icon = gp_icon,
        .awake = gp_awake,
        .run = true,  // the settings panel: it sends on under it (no buttons down: the panel has the fingers)
};
