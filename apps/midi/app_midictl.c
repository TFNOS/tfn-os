// MIDI: the board as a MIDI controller (Bluetooth LE MIDI, see ble_midi.c, or USB MIDI, see usb_midi.c).
// The task: the app task of TFN OS calls the hooks of app_midictl; the MIDI link of the firmware sends the messages.
// The memory: the file statics only (no heap); the presets stay in NVS.
//
// Four sliders and an XY pad (and the tilt of the board) send controller messages to a computer, a phone or a
// synthesizer: volume, pan, pitch bend, modulation, cutoff, resonance and more.
// - The sliders are absolute: the value goes to the finger at once. Each finger is one control,
//   so two or more fingers move two or more sliders at the same time. Fast moves up and down
//   are sent in steps of 4 ms with the BLE-MIDI timestamps, so the other side plays them back
//   with the same timing.
// - Each control can also move by itself: a pattern (sine, triangle, saw, square, sample and
//   hold, or a recorded gesture) at a free rate or in time with the tempo (tap or MIDI clock).
//   The finger moves the middle of the pattern; the pattern runs also with no finger.
// - The tilt of the board (two axes) can send two more controls: the start position is the
//   middle, the range is 10 to 60 degrees, LOCK keeps the values.
// - Five presets, kept in NVS. EDIT opens the settings of each control, and RESET PRESET and BLUETOOTH DISCONNECT
//   (each needs a second tap: TAP AGAIN).
// - SIMPLE mode (the default): one big slider with a role (CUTOFF, RESO, PITCH, MOD, VOLUME),
//   FREE (it stays) or TO CENTER (it goes back to the middle). No patterns and no tempo.
// - The output: Bluetooth LE MIDI or USB MIDI (usb_midi.c; the console port is away while the
//   app is open with USB).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/imu.h"
#include "tfn/audio.h"
#include "tfn/settings.h"
#include "tfn/board.h"
#include "tfn/ble_midi.h"
#include "tfn/usb_midi.h"
#include "aatext/aatext.h"
// THE TEXT: the Bungee faces of the firmware in place of the 5 x 7 dot font (libs/aatext); the round view (mc_rv.inc)
// draws its text with them too.
#define gfx_text           aat_text
#define gfx_text_width     aat_w
#define gfx_text_center    aat_text_center
#define gfx_text_center_in aat_text_center_in
#define gfx_clip_bottom    aat_clip_bottom
#define gfx_clip_reset     aat_clip_reset
#include "mc_rv.inc"

// ---------------------------------------------------------------- layout

#if BOARD_UI_TABLET
// (the Tab5, 640 x 360: its own layout, with no stretch. The top bar is the bar of the 3.49 (the same width), 56 px
//  high. PLAY: 4 sliders and a square XY pad. EDIT: 4 x 2 tiles of 154 x 108 and a row of RESET + DISCONNECT. The
//  editor: 4 rows of 64-84 px, laid out for this frame (build). The text is scale 2 and 3.)
#define MC_WIDE 1
#define MC_TAB5 1
#define SW      640
#define SH      360
#define TOP_H   56
#define MAIN_Y  62
#define MAIN_B  356
#define SL_X0   4
#define SL_W    78
#define SL_STEP 84
#define XY_W    (MAIN_B - MAIN_Y)  // the XY pad is square (294 x 294), at the right
#define XY_X    (SW - 4 - XY_W)
#define ST_X    394
#define ST_Y    6
#define ST_W    (SW - ST_X - 86)
#define ST_H    TOP_H
#define MODE_X  558
#define MODE_Y  1
#define MODE_W  80
#define MODE_H  (TOP_H - 2)
static const int16_t SB_X[] = {0, 60, 164, 278}, SB_W[] = {56, 100, 110, 112};
#define BIG_Y   62
#define BIG_B   262
#define ROLE_Y  270
#define ROLE_H  86
#define TILE_X0 4  // the EDIT grid: 4 x 2 tiles, and a row of RESET + DISCONNECT under them
#define TILE_W  154
#define TILE_SX 158
#define TILE_H  108
#define TILE_SY 114
static const int16_t ACT_X[] = {4, 322}, ACT_Y[] = {290, 290}, ACT_W = 314, ACT_H = 66;
static const int16_t TB_X[] = {0, 60, 140, 226, 312}, TB_W[] = {56, 76, 82, 82, 76};
static const int16_t TAB_X[] = {400, 520}, TAB_W = 116;
#define TITLE_X 62
#elif BOARD_BAR_SCREEN
#define MC_WIDE 1
#define MC_TAB5 0
#define SW      640
#define SH      172
#define TOP_H   40
#define MAIN_Y  42  // the sliders and the XY pad
#define MAIN_B  170
#define SL_X0   4
#define SL_W    96
#define SL_STEP 100
#define XY_X    404
#define XY_W    232
#define ST_X    394  // the status and the monitor (in the top bar)
#define ST_Y    4
#define ST_W    (SW - ST_X - 86)
#define ST_H    TOP_H
#define MODE_X  558  // ADVANCED: the MODE button (back to SIMPLE)
#define MODE_Y  1
#define MODE_W  80
#define MODE_H  38
// SIMPLE: the top bar (BACK, MODE, OUTPUT, SLIDER), the big slider, the row of roles
static const int16_t SB_X[] = {0, 60, 164, 278}, SB_W[] = {56, 100, 110, 112};
#define BIG_Y   44
#define BIG_B   124
#define ROLE_Y  128
#define ROLE_H  42
#define TILE_X0 4  // the EDIT grid: 4 x 2 tiles, and a column of RESET + DISCONNECT at the right
#define TILE_W  122
#define TILE_SX 126
#define TILE_H  60
#define TILE_SY 64
static const int16_t ACT_X[] = {514, 514}, ACT_Y[] = {58, 114}, ACT_W = 122,
                     ACT_H = 52;  // (10 px from the tiles, 19 px under MODE)
static const int16_t TB_X[] = {0, 60, 140, 226, 312}, TB_W[] = {56, 76, 82, 82, 76};
static const int16_t TAB_X[] = {400, 520}, TAB_W = 116;
#define TITLE_X 62
#elif BOARD_ROUND
// THE ROUND LAYOUT (APP_ROUND_HI): the 320 x 240 layout of the CoreS3 in the round view (mc_rv.inc: the frame at the
// row 40 of a square of 320 on the panel). Each row stays in its chord: the bar (BACK, PRESET, TAP, TILT) in the chord
// of the rows 0 .. 44 (x 54 .. 266), the status in the top cap, EDIT and MODE on the bottom arc, the sliders and the
// XY pad in the wide middle. The editor: its title in the top cap, its rows in the chord (build).
#define MC_WIDE 0
#define MC_TAB5 0
#define MC_RND  1
#define SW      320
#define SH      240
#define TOP_H   44
#define MAIN_Y  46
#define MAIN_B  196
#define SL_X0   24
#define SL_W    48
#define SL_STEP 50
#define XY_X    226
#define XY_W    70
#define ST_X    60  // the status and the monitor: two lines in the top cap
#define ST_Y    (-24)
#define ST_W    200
#define ST_H    40
#define MODE_X  162  // ADVANCED: the MODE button (the bottom arc)
#define MODE_Y  200
#define MODE_W  90
#define MODE_H  36
static const int16_t SB_X[] = {56, 96, 168, 110}, SB_W[] = {38, 70, 96, 100};  // (SLIDER: the bottom arc)
#define BIG_Y   48
#define BIG_B   144
#define ROLE_Y  147
#define ROLE_H  48
#define TILE_X0 24
#define TILE_W  66
#define TILE_SX 69
#define TILE_H  46
#define TILE_SY 48
static const int16_t ACT_X[] = {40, 162}, ACT_Y[] = {148, 148}, ACT_W = 118, ACT_H = 44;
static const int16_t TB_X[] = {56, 96, 160, 206, 68}, TB_W[] = {38, 62, 44, 56, 90};  // (EDIT: the bottom arc)
#define EDIT_Y  200
static const int16_t TAB_X[] = {100, 182}, TAB_W = 80;
#define TITLE_X 62
#else
// (the thumb: the top bar 44 px, BACK 56 px, the TILT halves 42 px = 4.6 mm on the AMOLED)
#define MC_WIDE 0
#define MC_TAB5 0
#define SW      320
#define SH      240
#define TOP_H   44
#define MAIN_Y  46
#define MAIN_B  198
#define SL_X0   2
#define SL_W    48
#define SL_STEP 52
#define XY_X    210
#define XY_W    108
#define ST_X    0  // the status and the monitor (the strip at the bottom)
#define ST_Y    (MAIN_B + 4)
#define ST_W    (SW - 90)
#define ST_H    (SH - MAIN_B)
#define MODE_X  232  // ADVANCED: the MODE button (back to SIMPLE)
#define MODE_Y  (MAIN_B + 2)
#define MODE_W  86
#define MODE_H  38
static const int16_t SB_X[] = {0, 58, 146, 234}, SB_W[] = {56, 86, 86, 86};
#define BIG_Y   48
#define BIG_B   144
#define ROLE_Y  147
#define ROLE_H  50
#define TILE_X0 2  // the EDIT grid: 4 x 2 tiles, and a row of RESET + DISCONNECT under them
#define TILE_W  76
#define TILE_SX 79
#define TILE_H  46
#define TILE_SY 48
static const int16_t ACT_X[] = {2, 117}, ACT_Y[] = {150, 150}, ACT_W = 113,
                     ACT_H = 46;  // (10 px under the tiles, left of MODE)
static const int16_t TB_X[] = {0, 58, 116, 174, 260}, TB_W[] = {56, 56, 56, 84, 60};
static const int16_t TAB_X[] = {152, 236}, TAB_W = 82;
#define TITLE_X 62
#endif
#define SL_T (MAIN_Y + 8)  // the track of a slider: value 1 at SL_T, 0 at SL_B
#define SL_B (MAIN_B - 8)
#define XY_M 8  // the margin of the XY pad (the values 0 and 1)
#ifndef MC_RND
#define MC_RND 0
#endif
#if MC_RND
#define BIG_X 22  // the big slider of SIMPLE (the round view: in the chord)
#define BIG_W 276
#define CHW   9  // the width of a character of the scale 1 in the frame (the round view: 2 px on the panel)
#else
#define BIG_X  4  // the big slider of SIMPLE
#define BIG_W  (SW - 8)
#define CHW    6
#define EDIT_Y 0
#endif
#define BIG_M 14  // the margin of the big slider (the values 0 and 1)
enum {
    SB_BACK,
    SB_MODE,
    SB_OUT,
    SB_SPRING,
    SB_N
};

enum {
    TB_BACK,
    TB_PRE,
    TB_TAP,
    TB_TILT,
    TB_EDIT,
    TB_N
};

#define BG     rgb(10, 12, 20)
#define PANEL  rgb(28, 32, 48)
#define FRAME  rgb(64, 70, 96)
#define DIM    rgb(130, 136, 160)
#define WHITE  rgb(255, 255, 255)
#define ACCENT rgb(40, 110, 220)

// ---------------------------------------------------------------- the controls

#define N_LANES   8
#define N_TOUCH   6  // the lanes that fingers move: S1..S4, X, Y
#define L_X       4
#define L_Y       5
#define L_TX      6  // the tilt left / right
#define L_TY      7  // the tilt forward / back
#define N_PRESETS 5
#define REC_MS    10  // a recorded gesture: one sample each 10 ms, up to 6 s
#define REC_MAX   600

enum {
    K_CC,
    K_BEND,
    K_PRESS,
    K_SUST,
    K_CUSTOM,
    K_OFF
};
typedef struct {
    const char *name;
    uint8_t kind, cc;
    bool mid;  // the rest value is the middle (pan, bend)
} target_t;
enum {
    T_MOD,
    T_VOL,
    T_PAN,
    T_EXPR,
    T_BEND,
    T_CUT,
    T_RESO,
    T_SUST,
    T_REV,
    T_CHO,
    T_ATK,
    T_REL,
    T_BREATH,
    T_PRESS,
    T_CC,
    T_OFF,
    N_TARGETS
};
static const target_t TARGETS[N_TARGETS] = {
        {"MOD", K_CC, 1, false},     {"VOLUME", K_CC, 7, false},     {"PAN", K_CC, 10, true},
        {"EXPR", K_CC, 11, false},   {"BEND", K_BEND, 0, true},      {"CUTOFF", K_CC, 74, false},
        {"RESO", K_CC, 71, false},   {"SUSTAIN", K_SUST, 64, false}, {"REVERB", K_CC, 91, false},
        {"CHORUS", K_CC, 93, false}, {"ATTACK", K_CC, 73, false},    {"RELEASE", K_CC, 72, false},
        {"BREATH", K_CC, 2, false},  {"PRESS", K_PRESS, 0, false},   {"CC#", K_CUSTOM, 20, false},
        {"OFF", K_OFF, 0, false},
};

enum {
    P_HOLD,
    P_SINE,
    P_TRI,
    P_SAW,
    P_SQR,
    P_SH,
    P_REC,
    N_PATS
};
static const char *const PAT_NAMES[N_PATS] = {"HOLD", "SINE", "TRI", "SAW", "SQUARE", "S&H", "REC"};

// the rates: 16 free rates (Hz) and 16 note lengths (one cycle, in beats)
static const float RATE_HZ[16] = {0.1f, 0.2f, 0.33f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4, 5, 6, 8, 10, 12, 16};
static const char *const RATE_NAMES[16] = {"0.1 HZ", "0.2 HZ", "0.33 HZ", "0.5 HZ", "0.75 HZ", "1 HZ",
                                           "1.5 HZ", "2 HZ",   "3 HZ",    "4 HZ",   "5 HZ",    "6 HZ",
                                           "8 HZ",   "10 HZ",  "12 HZ",   "16 HZ"};
static const float SYNC_BEATS[16] = {16,       8,    4,        3,      2,     1.5f,     1,      0.75f,
                                     2.0f / 3, 0.5f, 1.0f / 3, 0.375f, 0.25f, 1.0f / 6, 0.125f, 1.0f / 12};
static const char *const SYNC_NAMES[16] = {"4 BARS", "2 BARS", "1 BAR", "1/2.",  "1/2",  "1/4.",  "1/4",  "1/8.",
                                           "1/4T",   "1/8",    "1/8T",  "1/16.", "1/16", "1/16T", "1/32", "1/32T"};
#define BEAT_WRAP 48.0f  // all the note lengths fit a whole number of times in 48 beats

static const char *const LANE_NAMES[N_LANES] = {"S1", "S2", "S3", "S4", "X", "Y", "TILT X", "TILT Y"};
static const int16_t LANE_HUE[N_LANES] = {0, 35, 120, 195, 280, 320, 160, 60};
static const char *const PRESET_NAMES[N_PRESETS] = {"PLAY", "MIXER", "FILTER", "WOBBLE", "GESTURE"};
#define N_ROLES 5
static const uint8_t ROLE_TGT[N_ROLES] = {T_CUT, T_RESO, T_BEND, T_MOD, T_VOL};
static const char *const ROLE_NAMES[N_ROLES] = {"CUTOFF", "RESO", "PITCH", "MOD", "VOLUME"};

// The settings of one control (one uint32 in NVS).
typedef struct {
    uint8_t tgt, cc, pat, rate, depth, ch;  // rate: also the tilt range (10 + 5 * rate degrees)
    bool sync, spring, inv;
} lcfg_t;

// The state of one control.
typedef struct {
    float c;         // the middle: the finger, the spring or the tilt (0..1)
    float out;       // the value after the pattern and INVERT (0..1)
    float phase;     // the phase of the free rate (0..1)
    int32_t cyc;     // the cycle count of the free rate
    int32_t sh_cyc;  // the cycle of the S&H value
    float sh;        // the S&H value (-1..1)
    int32_t sent;    // the last value sent (-1 = none)
    bool held;       // a finger holds the control
    bool springing;  // the value goes back to the rest value
    bool rec, loop;  // recording; a recorded gesture plays
    uint16_t rec_n;
    int64_t rec_t0, loop_t0;
    float tilt_f;  // the filtered tilt (degrees)
} lane_t;

typedef struct {
    bool on;
    bool fresh;            // it touched in this frame: no earlier position
    int16_t x, y, px, py;  // now and at the last frame (logical)
    int8_t ctl;            // 0..3 a slider, L_X the XY pad, -1 nothing
} finger_t;

enum {
    PG_PLAY,
    PG_GRID,
    PG_EDIT
};
enum {
    TAB_TARGET,
    TAB_MOTION
};

static lcfg_t s_cfg[N_LANES];
static lane_t s_lane[N_LANES];
static uint16_t s_rec[N_TOUCH][REC_MAX];
static finger_t s_f[INPUT_MAX_POINTS];
static int8_t s_owner[N_TOUCH];  // the finger that holds the control, -1 none
static int s_preset;
static bool s_ready;
static int s_page, s_tab, s_edit;
static int64_t s_t_prev, s_frame_t0;
static float s_beat;     // the beat position (0..BEAT_WRAP)
static int s_bpm = 120;  // the tap tempo
static int64_t s_taps[8];
static int s_ntap;
static int64_t s_tap_flash;
static uint32_t s_starts;
static bool s_tilt, s_lock;
static int s_learn = -1;  // LEARN: only this control sends
static int64_t s_learn_t0;
static bool s_link;
static bool s_simple = true;  // SIMPLE mode (the default)
static int s_role;            // SIMPLE: the role of the big slider
static bool s_center;         // SIMPLE: the slider goes back to the middle
static bool s_touched;        // SIMPLE: nothing goes out before the first touch
static bool s_usb;            // the output: USB MIDI (else Bluetooth)
static uint32_t s_rng = 0x1234567u;
// the monitor: the last messages
static uint8_t s_mon[3][3];
static int s_mon_n;
static int64_t s_down_t;  // the time of the last touch (TAP: the tempo counts the press, it acts at the lift)
// THE TWO ACTIONS THAT CANNOT BE UNDONE: they are buttons in the EDIT grid, and each needs a second tap in ARM_MS
// ("TAP AGAIN"). No long press does them (a thumb rests on the screen).
enum {
    ACT_RESET,
    ACT_DISC,
    ACT_N
};
#define ARM_MS 2500  // the second tap must come in this time
#define ARM_MIN_MS                                                                                                     \
    500                 // (a second tap sooner than this does nothing: a thumb that tapped a tile and missed
                        //  it, then tapped again at once, must not do it)
static int s_arm = -1;  // the action that waits for its second tap (-1 = none)
static int64_t s_arm_t;
static int s_done = -1;  // the action that was done (a short "DONE" on its button)
static int64_t s_done_t;

// ---------------------------------------------------------------- helpers

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static float rnd01(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return (float)(s_rng >> 8) / 16777216.0f;
}

static bool tilt_avail(void)
{
    return (app_board_caps() & APP_NEEDS_IMU) && imu_ok();
}

static uint16_t lane_col(int l, int v)
{
    return hsv(LANE_HUE[l], 200, v);
}

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static float rest_value(int l)
{
    if (s_simple) {
        return 0.5f;  // TO CENTER: the middle for each role
    }
    return TARGETS[s_cfg[l].tgt].mid ? 0.5f : 0.0f;
}

static uint8_t cc_of(const lcfg_t *c)
{
    return TARGETS[c->tgt].kind == K_CUSTOM ? c->cc : TARGETS[c->tgt].cc;
}

// ---------------------------------------------------------------- presets (NVS)

static uint32_t cfg_pack(const lcfg_t *c)
{
    return 0x80000000u | c->tgt | (uint32_t)c->cc << 5 | (uint32_t)c->pat << 12 | (uint32_t)c->rate << 15 |
           (uint32_t)c->depth << 19 | (uint32_t)c->sync << 23 | (uint32_t)c->spring << 24 | (uint32_t)c->inv << 25 |
           (uint32_t)c->ch << 26;
}

static lcfg_t cfg_unpack(uint32_t w)
{
    lcfg_t c = {
            .tgt = w & 31,
            .cc = (w >> 5) & 127,
            .pat = (w >> 12) & 7,
            .rate = (w >> 15) & 15,
            .depth = (w >> 19) & 15,
            .sync = (w >> 23) & 1,
            .spring = (w >> 24) & 1,
            .inv = (w >> 25) & 1,
            .ch = (w >> 26) & 15,
    };
    if (c.tgt >= N_TARGETS) {
        c.tgt = T_OFF;
    }
    if (c.pat >= N_PATS) {
        c.pat = P_HOLD;
    }
    return c;
}

static lcfg_t lc(int tgt, int pat, int rate, int depth, bool sync, bool spring, int ch)
{
    lcfg_t c = {.tgt = (uint8_t)tgt,
                .cc = 20,
                .pat = (uint8_t)pat,
                .rate = (uint8_t)rate,
                .depth = (uint8_t)depth,
                .ch = (uint8_t)ch,
                .sync = sync,
                .spring = spring,
                .inv = false};
    return c;
}

#define TR 4  // the tilt range 30 degrees
static void preset_default(int p, lcfg_t *c)
{
    switch (p) {
    case 0:  // PLAY: the usual controls of a synthesizer
        c[0] = lc(T_MOD, P_HOLD, 5, 15, false, false, 0);
        c[1] = lc(T_VOL, P_HOLD, 5, 15, false, false, 0);
        c[2] = lc(T_CUT, P_HOLD, 5, 15, false, false, 0);
        c[3] = lc(T_RESO, P_HOLD, 5, 15, false, false, 0);
        c[4] = lc(T_BEND, P_HOLD, 5, 15, false, true, 0);
        c[5] = lc(T_EXPR, P_HOLD, 5, 15, false, false, 0);
        c[6] = lc(T_PAN, P_HOLD, TR, 15, false, false, 0);
        c[7] = lc(T_MOD, P_HOLD, TR, 15, false, false, 0);
        break;
    case 1:  // MIXER: the volume of channels 1 to 4
        for (int i = 0; i < 4; i++) {
            c[i] = lc(T_VOL, P_HOLD, 5, 15, false, false, i);
        }
        c[4] = lc(T_PAN, P_HOLD, 5, 15, false, false, 0);
        c[5] = lc(T_REV, P_HOLD, 5, 15, false, false, 0);
        c[6] = lc(T_PAN, P_HOLD, TR, 15, false, false, 0);
        c[7] = lc(T_CHO, P_HOLD, TR, 15, false, false, 0);
        break;
    case 2:  // FILTER: a slow sine on the cutoff
        c[0] = lc(T_CUT, P_SINE, 3, 7, false, false, 0);
        c[1] = lc(T_RESO, P_HOLD, 5, 15, false, false, 0);
        c[2] = lc(T_ATK, P_HOLD, 5, 15, false, false, 0);
        c[3] = lc(T_REL, P_HOLD, 5, 15, false, false, 0);
        c[4] = lc(T_CUT, P_HOLD, 5, 15, false, false, 0);
        c[5] = lc(T_RESO, P_HOLD, 5, 15, false, false, 0);
        c[6] = lc(T_CUT, P_HOLD, TR, 15, false, false, 0);
        c[7] = lc(T_RESO, P_HOLD, TR, 15, false, false, 0);
        break;
    case 3:  // WOBBLE: patterns in time with the tempo
        c[0] = lc(T_CUT, P_SQR, 9, 15, true, false, 0);
        c[1] = lc(T_MOD, P_TRI, 6, 15, true, false, 0);
        c[2] = lc(T_PAN, P_SINE, 2, 15, true, false, 0);
        c[3] = lc(T_VOL, P_SH, 12, 7, true, false, 0);
        c[4] = lc(T_BEND, P_HOLD, 5, 15, false, true, 0);
        c[5] = lc(T_MOD, P_HOLD, 5, 15, false, false, 0);
        c[6] = lc(T_PAN, P_HOLD, TR, 15, false, false, 0);
        c[7] = lc(T_CUT, P_HOLD, TR, 15, false, false, 0);
        break;
    default:  // GESTURE: each control records the finger and plays it again
        c[0] = lc(T_MOD, P_REC, 5, 15, false, false, 0);
        c[1] = lc(T_CUT, P_REC, 5, 15, false, false, 0);
        c[2] = lc(T_PAN, P_REC, 5, 15, false, false, 0);
        c[3] = lc(T_VOL, P_REC, 5, 15, false, false, 0);
        c[4] = lc(T_CUT, P_REC, 5, 15, false, false, 0);
        c[5] = lc(T_RESO, P_REC, 5, 15, false, false, 0);
        c[6] = lc(T_BEND, P_HOLD, TR, 15, false, false, 0);
        c[7] = lc(T_MOD, P_HOLD, TR, 15, false, false, 0);
        break;
    }
}

static void lane_reset(int l)
{
    lane_t *L = &s_lane[l];
    const lcfg_t *C = &s_cfg[l];
    memset(L, 0, sizeof(*L));
    L->sent = -1;
    L->sh_cyc = -1;
    // the start value: the middle for pan, bend and the patterns; volume 100; else 0
    L->c = TARGETS[C->tgt].mid || (C->pat != P_HOLD && C->pat != P_REC) ? 0.5f : C->tgt == T_VOL ? 100.0f / 127 : 0.0f;
    L->out = C->inv ? 1 - L->c : L->c;
}

static void key_of(int p, int l, char *k)
{
    snprintf(k, 12, "mi%d_%d", p, l);
}

static void preset_load(int p)
{
    lcfg_t def[N_LANES];
    preset_default(p, def);
    for (int l = 0; l < N_LANES; l++) {
        char k[12];
        key_of(p, l, k);
        uint32_t w = store_get(k, 0);
        s_cfg[l] = (w & 0x80000000u) ? cfg_unpack(w) : def[l];
        lane_reset(l);
    }
}

static void lane_save(int l)
{
    char k[12];
    key_of(s_preset, l, k);
    store_set(k, cfg_pack(&s_cfg[l]));
}

// ---------------------------------------------------------------- MIDI out

static void put(uint8_t a, uint8_t b, uint8_t c, int len, int64_t t)
{
    if (!s_link) {
        return;  // no link: nothing goes out (the next change sends)
    }
    uint8_t m[3] = {a, b, c};
    if (s_usb) {
        usb_midi_send(m, len);
    } else {
        ble_midi_send(m, len, (uint32_t)t);
    }
    memmove(s_mon[1], s_mon[0], sizeof(s_mon[0]) * 2);
    memcpy(s_mon[0], m, 3);
    if (s_mon_n < 3) {
        s_mon_n++;
    }
}

// Send the value v (0..1) of control l if it changed (with a small hysteresis).
static void lane_send(int l, float v, int64_t t)
{
    lane_t *L = &s_lane[l];
    const lcfg_t *C = &s_cfg[l];
    if (s_learn >= 0 && s_learn != l) {
        return;  // LEARN: the other controls are silent
    }
    if (s_simple && !s_touched) {
        return;  // SIMPLE: the synthesizer keeps its sound until a touch
    }
    uint8_t ch = C->ch & 15;
    switch (TARGETS[C->tgt].kind) {
    case K_OFF:
        return;
    case K_BEND: {
        int q = (int)(v * 16383 + 0.5f);
        if (q == L->sent) {
            return;
        }
        if (L->sent >= 0 && abs(q - L->sent) < 4 && q != 8192 && q != 0 && q != 16383) {
            return;
        }
        L->sent = q;
        put((uint8_t)(0xE0 | ch), (uint8_t)(q & 127), (uint8_t)(q >> 7), 3, t);
        return;
    }
    case K_SUST: {
        bool on = L->sent == 127 ? v > 0.45f : v > 0.55f;
        int q = on ? 127 : 0;
        if (q == L->sent) {
            return;
        }
        L->sent = q;
        put((uint8_t)(0xB0 | ch), 64, (uint8_t)q, 3, t);
        return;
    }
    default: {
        float x = v * 127;
        int q = (int)(x + 0.5f);
        if (q == L->sent) {
            return;
        }
        if (L->sent >= 0 && fabsf(x - (float)L->sent) < 0.65f) {
            return;
        }
        L->sent = q;
        if (TARGETS[C->tgt].kind == K_PRESS) {
            put((uint8_t)(0xD0 | ch), (uint8_t)q, 0, 2, t);
        } else {
            put((uint8_t)(0xB0 | ch), cc_of(C), (uint8_t)q, 3, t);
        }
        return;
    }
    }
}

// ---------------------------------------------------------------- the patterns

// The pattern wave (-1..1) of control l now.
static float wave(int l)
{
    lane_t *L = &s_lane[l];
    const lcfg_t *C = &s_cfg[l];
    float ph;
    int32_t cyc;
    if (C->sync) {
        float x = s_beat / SYNC_BEATS[C->rate];
        cyc = (int32_t)floorf(x);
        ph = x - (float)cyc;
    } else {
        ph = L->phase;
        cyc = L->cyc;
    }
    switch (C->pat) {
    case P_SINE:
        return fsin(ph * 6.2831853f);
    case P_TRI:
        return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
    case P_SAW:
        return 2 * ph - 1;
    case P_SQR:
        return ph < 0.5f ? 1.0f : -1.0f;
    case P_SH:
        if (cyc != L->sh_cyc) {
            L->sh_cyc = cyc;
            L->sh = rnd01() * 2 - 1;
        }
        return L->sh;
    default:
        return 0;
    }
}

// A recorded gesture at time t (0..1), the samples joined by straight lines.
static float rec_play(int l, int64_t t)
{
    lane_t *L = &s_lane[l];
    int64_t d = t - L->loop_t0;
    if (d < 0) {
        d = 0;
    }
    int64_t len = (int64_t)L->rec_n * REC_MS;
    float pos = (float)(d % len) / REC_MS;
    int i = (int)pos;
    float f = pos - (float)i;
    if (i >= L->rec_n) {
        i = L->rec_n - 1;
    }
    int j = i + 1 < L->rec_n ? i + 1 : 0;
    return (s_rec[l][i] + (s_rec[l][j] - s_rec[l][i]) * f) / 65535.0f;
}

static void rec_add(int l, int64_t t)
{
    lane_t *L = &s_lane[l];
    int64_t idx = (t - L->rec_t0) / REC_MS;
    if (idx < 0) {
        idx = 0;
    }
    uint16_t v = (uint16_t)(L->c * 65535 + 0.5f);
    while (L->rec_n <= idx && L->rec_n < REC_MAX) {
        s_rec[l][L->rec_n++] = v;
    }
    if (L->rec_n >= REC_MAX) {
        L->rec = false;  // 6 s: the take is full
    }
}

// The value of control l at time t: the middle, the pattern, INVERT, LEARN.
static float lane_value(int l, int64_t t)
{
    lane_t *L = &s_lane[l];
    const lcfg_t *C = &s_cfg[l];
    float v = L->c;
    if (l < N_TOUCH) {
        if (C->pat == P_REC) {
            if (!L->held && L->loop && L->rec_n > 1) {
                v = rec_play(l, t);
            }
        } else if (C->pat != P_HOLD) {
            v = L->c + wave(l) * (C->depth + 1) / 32.0f;
        }
    }
    v = clampf(v, 0, 1);
    if (C->inv) {
        v = 1 - v;
    }
    if (s_learn == l && t - s_learn_t0 < 1000) {
        // LEARN: one sweep up and down in 1 s, so that the other side sees a clear move
        float p = (float)(t - s_learn_t0) / 1000;
        v = p < 0.5f ? p * 2 : 2 - p * 2;
    }
    return v;
}

// ---------------------------------------------------------------- fingers

static int hit_ctl(int x, int y)
{
    if (s_simple) {
        return in_rect(x, y, BIG_X - 2, BIG_Y, BIG_W + 4, BIG_B - BIG_Y) ? 0 : -1;
    }
    if (y < MAIN_Y || y >= MAIN_B) {
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        int sx = SL_X0 + i * SL_STEP;
        if (x >= sx - 2 && x < sx + SL_W + 2) {
            return i;
        }
    }
    if (x >= XY_X - 2 && x < XY_X + XY_W + 4) {
        return L_X;
    }
    return -1;
}

static float finger_val(int l, const finger_t *F, float a)
{
    float x = F->px + (F->x - F->px) * a;
    float y = F->py + (F->y - F->py) * a;
    float v;
    if (s_simple) {
        return clampf((x - BIG_X - BIG_M) / (float)(BIG_W - 2 * BIG_M), 0, 1);
    }
    if (l < 4) {
        v = (SL_B - y) / (float)(SL_B - SL_T);
    } else if (l == L_X) {
        v = (x - XY_X - XY_M) / (float)(XY_W - 2 * XY_M);
    } else {
        v = (MAIN_B - XY_M - y) / (float)(MAIN_B - MAIN_Y - 2 * XY_M);
    }
    return clampf(v, 0, 1);
}

static void lane_grab(int l, int f)
{
    lane_t *L = &s_lane[l];
    s_owner[l] = (int8_t)f;
    L->held = true;
    if (s_simple) {
        s_touched = true;
    }
    L->springing = false;
    if (s_cfg[l].pat == P_REC) {
        // a new take: the old loop stops, the finger records
        L->rec = true;
        L->loop = false;
        L->rec_n = 0;
        L->rec_t0 = s_frame_t0;
    }
}

static void lane_release(int l)
{
    lane_t *L = &s_lane[l];
    const lcfg_t *C = &s_cfg[l];
    s_owner[l] = -1;
    L->held = false;
    if (C->pat == P_REC) {
        L->rec = false;
        if (L->rec_n >= 10) {  // 100 ms or more: the gesture plays in a loop
            L->loop = true;
            L->loop_t0 = s_frame_t0;
        }
    }
    L->springing = C->spring && !L->loop;
}

static void finger_up(int f)
{
    for (int l = 0; l < N_TOUCH; l++) {
        if (s_owner[l] == f) {
            lane_release(l);
        }
    }
    s_f[f].on = false;
}

static void finger_down(int f, int x, int y)
{
    finger_t *F = &s_f[f];
    F->on = true;
    F->fresh = true;
    F->x = F->px = (int16_t)x;
    F->y = F->py = (int16_t)y;
    F->ctl = (int8_t)hit_ctl(x, y);
    // the newest finger on a control takes it
    if (F->ctl >= 0 && F->ctl < 4) {
        lane_grab(F->ctl, f);
    } else if (F->ctl == L_X) {
        lane_grab(L_X, f);
        lane_grab(L_Y, f);
    }
}

// Follow the fingers: each touch point of this frame goes to the nearest finger of the last
// frame (the nearest pair first); a point with no finger is a new finger.
static void track_fingers(void)
{
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    int lx[INPUT_MAX_POINTS], ly[INPUT_MAX_POINTS];
    int n = s_page == PG_PLAY ? input_points(px, py, INPUT_MAX_POINTS) : 0;
    if (n < 0) {
        n = 0;
    }
    if (n > INPUT_MAX_POINTS) {
        n = INPUT_MAX_POINTS;
    }
    for (int i = 0; i < n; i++) {
        gfx_touch_to_logical(px[i], py[i], &lx[i], &ly[i]);
#if MC_RND
        rvm_pt(&lx[i], &ly[i]);
#endif
    }
    bool used[INPUT_MAX_POINTS] = {false}, kept[INPUT_MAX_POINTS] = {false};
    for (;;) {
        int best = 200 * 200, bf = -1, bp = -1;
        for (int f = 0; f < INPUT_MAX_POINTS; f++) {
            if (!s_f[f].on || kept[f]) {
                continue;
            }
            for (int p = 0; p < n; p++) {
                if (used[p]) {
                    continue;
                }
                int dx = lx[p] - s_f[f].x, dy = ly[p] - s_f[f].y;
                int d = 4 * dx * dx + dy * dy;  // a slider moves up and down: x counts more
                if (d < best) {
                    best = d;
                    bf = f;
                    bp = p;
                }
            }
        }
        if (bf < 0) {
            break;
        }
        kept[bf] = used[bp] = true;
        s_f[bf].x = (int16_t)lx[bp];
        s_f[bf].y = (int16_t)ly[bp];
    }
    for (int f = 0; f < INPUT_MAX_POINTS; f++) {
        if (s_f[f].on && !kept[f]) {
            finger_up(f);
        }
    }
    for (int p = 0; p < n; p++) {
        if (used[p]) {
            continue;
        }
        for (int f = 0; f < INPUT_MAX_POINTS; f++) {
            if (!s_f[f].on) {
                finger_down(f, lx[p], ly[p]);
                break;
            }
        }
    }
}

// ---------------------------------------------------------------- update

static float out_clock_bpm(void)
{
    return s_usb ? usb_midi_clock_bpm() : ble_midi_clock_bpm();
}

static bool out_linked(void)
{
    return s_usb ? usb_midi_state() == UM_LINKED : ble_midi_state() == BM_LINKED;
}

static float cur_bpm(void)
{
    float clk = out_clock_bpm();
    return clk > 0 ? clk : (float)s_bpm;
}

// One sub-step of a touch control at time t (ds ms after the last sub-step; a = the part of
// the frame for the finger position).
static void lane_step(int l, int64_t t, float ds, float a)
{
    lane_t *L = &s_lane[l];
    const lcfg_t *C = &s_cfg[l];
    if (!C->sync) {
        L->phase += RATE_HZ[C->rate] * ds / 1000;
        if (L->phase >= 1) {
            L->phase -= floorf(L->phase);
            L->cyc++;
        }
    }
    // a new finger: its position is known only at the end of the frame
    if (L->held && s_owner[l] >= 0 && s_f[s_owner[l]].fresh && a < 1) {
        return;
    }
    if (L->held && s_owner[l] >= 0) {
        L->c = finger_val(l, &s_f[s_owner[l]], a);
        if (C->pat == P_REC && L->rec) {
            rec_add(l, t);
        }
    } else if (L->springing) {
        float rest = rest_value(l);
        L->c = rest + (L->c - rest) * expf(-ds / 35.0f);
        if (fabsf(L->c - rest) < 0.002f) {
            L->c = rest;
            L->springing = false;
        }
    }
    L->out = lane_value(l, t);
    lane_send(l, L->out, t);
}

static void tilt_step(int64_t now, int dt)
{
    if (!tilt_avail() || !s_tilt) {
        return;
    }
    float t[2];
    imu_tilt_logical(&t[0], &t[1]);
    for (int k = 0; k < 2; k++) {
        int l = L_TX + k;
        lane_t *L = &s_lane[l];
        float deg = asinf(clampf(t[k], -1, 1)) * 57.29578f;
        L->tilt_f += (deg - L->tilt_f) * ((float)dt / ((float)dt + 40.0f));
        if (!s_lock) {
            float range = 10.0f + 5.0f * (s_cfg[l].rate > 10 ? 10 : s_cfg[l].rate);
            L->c = clampf(0.5f + L->tilt_f / (2 * range), 0, 1);
        }
        if (s_learn == l) {
            continue;  // the sub-steps send the LEARN sweep
        }
        L->out = lane_value(l, now);
        lane_send(l, L->out, now);
    }
}

static void mc_update(int64_t now)
{
    int dt = (int)(now - s_t_prev);
    if (dt < 0) {
        dt = 0;
    }
    if (dt > 250) {
        dt = 250;  // a long frame: the steps cover only the last 250 ms
    }
    s_frame_t0 = now - dt;
    if (s_arm >= 0 && (now - s_arm_t > ARM_MS || s_simple || s_page != PG_GRID)) {
        s_arm = -1;  // "TAP AGAIN" ends
    }
    s_link = out_linked();
    for (int f = 0; f < INPUT_MAX_POINTS; f++) {
        s_f[f].px = s_f[f].x;
        s_f[f].py = s_f[f].y;
        s_f[f].fresh = false;
    }
    track_fingers();
    // MIDI Start or Continue from the other side: the beat starts again
    uint32_t st = s_usb ? usb_midi_starts() : ble_midi_starts();
    if (st != s_starts) {
        s_starts = st;
        s_beat = 0;
    }
    float bpm = cur_bpm();
    // sub-steps of about 4 ms: fast finger moves and fast patterns keep their shape
    int n = dt / 4;
    if (n < 1) {
        n = 1;
    }
    if (n > 8) {
        n = 8;
    }
    for (int k = 1; k <= n; k++) {
        float a = (float)k / (float)n;
        int64_t t = s_frame_t0 + (int64_t)dt * k / n;
        float ds = (float)dt / (float)n;
        s_beat += ds * bpm / 60000.0f;
        if (s_beat >= BEAT_WRAP) {
            s_beat -= BEAT_WRAP;
        }
        for (int l = 0; l < N_TOUCH; l++) {
            lane_step(l, t, ds, a);
        }
        if (s_learn >= L_TX) {
            s_lane[s_learn].out = lane_value(s_learn, t);
            lane_send(s_learn, s_lane[s_learn].out, t);
        }
    }
    tilt_step(now, dt);
    if (s_learn >= 0 && now - s_learn_t0 > 1500) {
        s_learn = -1;
        for (int l = 0; l < N_LANES; l++) {
            s_lane[l].sent = -1;  // all controls send again
        }
    }
    if (s_usb) {
        usb_midi_poll();
    } else {
        ble_midi_flush();
    }
    s_t_prev = now;
}

// ---------------------------------------------------------------- the editor elements

enum {
    E_TGT = 0,  // + target (16)
    E_CCM = 16,
    E_CCV,
    E_CCP,
    E_CHM,
    E_CHV,
    E_CHP,
    E_PAT = 24,  // + pattern (7)
    E_RTM = 31,
    E_RTV,
    E_RTP,
    E_DPM,
    E_DPV,
    E_DPP,
    E_SYNC,
    E_SPRING,
    E_INV,
    E_LEARN,
    E_LEVEL,
    E_NOTE,
    E_PREVIEW,
};
typedef struct {
    int16_t x, y, w, h;
    uint8_t id;
} el_t;
#define MAX_EL 32

static bool el_tap(int id)
{
    return id != E_CCV && id != E_CHV && id != E_RTV && id != E_DPV && id != E_NOTE && id != E_PREVIEW;
}

static int add(el_t *e, int n, int x, int y, int w, int h, int id)
{
    e[n] = (el_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (uint8_t)id};
    return n + 1;
}

static int build(el_t *e)
{
    int n = 0;
    bool tilt = s_edit >= L_TX;
#if MC_TAB5
    // the Tab5: 4 rows (y 62, 132, 202, 272), 64 px high (the last one 80-84 px)
    if (s_tab == TAB_TARGET) {
        for (int i = 0; i < N_TARGETS; i++) {
            n = add(e, n, 4 + (i % 6) * 106, 62 + (i / 6) * 70, 102, 64, E_TGT + i);
        }
        n = add(e, n, 4, 272, 70, 80, E_CCM);
        n = add(e, n, 78, 272, 160, 80, E_CCV);
        n = add(e, n, 242, 272, 70, 80, E_CCP);
        n = add(e, n, 328, 272, 70, 80, E_CHM);
        n = add(e, n, 402, 272, 160, 80, E_CHV);
        n = add(e, n, 566, 272, 70, 80, E_CHP);
        return n;
    }
    if (tilt) {
        n = add(e, n, 4, 62, 632, 64, E_NOTE);
        n = add(e, n, 270, 132, 182, 64, E_LEVEL);
    } else {
        for (int i = 0; i < N_PATS; i++) {
            n = add(e, n, 4 + i * 90, 62, 86, 64, E_PAT + i);
        }
        n = add(e, n, 270, 132, 56, 64, E_DPM);
        n = add(e, n, 328, 132, 140, 64, E_DPV);
        n = add(e, n, 470, 132, 56, 64, E_DPP);
        n = add(e, n, 532, 132, 104, 64, E_SYNC);
        n = add(e, n, 4, 202, 200, 64, E_SPRING);
    }
    n = add(e, n, 4, 132, 56, 64, E_RTM);
    n = add(e, n, 62, 132, 140, 64, E_RTV);
    n = add(e, n, 204, 132, 56, 64, E_RTP);
    n = add(e, n, 208, 202, 200, 64, E_INV);
    n = add(e, n, 412, 202, 224, 64, E_LEARN);
    n = add(e, n, 4, 272, 632, 84, E_PREVIEW);
    return n;
#else
    if (s_tab == TAB_TARGET) {
#if MC_WIDE
        for (int i = 0; i < N_TARGETS; i++) {
            n = add(e, n, 4 + (i % 8) * 79, 42 + (i / 8) * 42, 75, 38, E_TGT + i);
        }
        n = add(e, n, 4, 128, 56, 40, E_CCM);
        n = add(e, n, 62, 128, 130, 40, E_CCV);
        n = add(e, n, 194, 128, 56, 40, E_CCP);
        n = add(e, n, 270, 128, 56, 40, E_CHM);
        n = add(e, n, 328, 128, 130, 40, E_CHV);
        n = add(e, n, 460, 128, 56, 40, E_CHP);
#else
        for (int i = 0; i < N_TARGETS; i++) {
            n = add(e, n, 2 + (i % 4) * 79, 46 + (i / 4) * 39, 76, 36, E_TGT + i);
        }
        n = add(e, n, 2, 203, 40, 36, E_CCM);
        n = add(e, n, 44, 203, 64, 36, E_CCV);
        n = add(e, n, 110, 203, 40, 36, E_CCP);
        n = add(e, n, 162, 203, 40, 36, E_CHM);
        n = add(e, n, 204, 203, 64, 36, E_CHV);
        n = add(e, n, 270, 203, 40, 36, E_CHP);
#endif
        return n;
    }
#if MC_WIDE
    if (tilt) {
        n = add(e, n, 4, 42, 632, 40, E_NOTE);
        n = add(e, n, 448, 86, 90, 38, E_LEVEL);
    } else {
        for (int i = 0; i < N_PATS; i++) {
            n = add(e, n, 4 + i * 90, 42, 86, 40, E_PAT + i);
        }
        n = add(e, n, 226, 86, 44, 38, E_DPM);
        n = add(e, n, 272, 86, 120, 38, E_DPV);
        n = add(e, n, 394, 86, 44, 38, E_DPP);
        n = add(e, n, 448, 86, 90, 38, E_SYNC);
        n = add(e, n, 4, 128, 120, 40, E_SPRING);
    }
    n = add(e, n, 4, 86, 44, 38, E_RTM);
    n = add(e, n, 50, 86, 120, 38, E_RTV);
    n = add(e, n, 172, 86, 44, 38, E_RTP);
    n = add(e, n, 544, 86, 92, 38, E_LEARN);
    n = add(e, n, 128, 128, 120, 40, E_INV);
    n = add(e, n, 256, 128, 380, 40, E_PREVIEW);
#else
    // (the rows start under the top bar of 44 px)
    if (tilt) {
        n = add(e, n, 2, 46, 316, 36, E_NOTE);
        n = add(e, n, 2, 85, 155, 36, E_LEVEL);
        n = add(e, n, 161, 85, 157, 36, E_LEARN);
    } else {
        for (int i = 0; i < N_PATS; i++) {
            n = add(e, n, 2 + (i % 4) * 79, 46 + (i / 4) * 39, 76, 36, E_PAT + i);
        }
        n = add(e, n, 239, 85, 79, 36, E_LEARN);
        n = add(e, n, 162, 124, 40, 37, E_DPM);
        n = add(e, n, 204, 124, 72, 37, E_DPV);
        n = add(e, n, 278, 124, 40, 37, E_DPP);
        n = add(e, n, 2, 163, 102, 37, E_SYNC);
        n = add(e, n, 108, 163, 104, 37, E_SPRING);
    }
    n = add(e, n, 2, 124, 40, 37, E_RTM);
    n = add(e, n, 44, 124, 72, 37, E_RTV);
    n = add(e, n, 118, 124, 40, 37, E_RTP);
    n = add(e, n, 216, 163, 102, 37, E_INV);
    n = add(e, n, 2, 204, 316, 34, E_PREVIEW);
#endif
    return n;
#endif
}

#if MC_RND
// the round view: the rows of the editor (y 46 .. 240) go to the rows 46 .. 220, and each row into its chord
static int build_round(el_t *e)
{
    int n = build(e);
    for (int i = 0; i < n; i++) {
        int y0 = MAIN_Y + (e[i].y - MAIN_Y) * 174 / 194, y1 = MAIN_Y + (e[i].y + e[i].h - MAIN_Y) * 174 / 194;
        int half = rvm_ch2(y0, y1 - y0) - 3;
        int x0 = 160 - half + e[i].x * half / 160, x1 = 160 - half + (e[i].x + e[i].w) * half / 160;
        e[i] = (el_t){(int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0), e[i].id};
    }
    return n;
}
#define build build_round
#endif

// ---------------------------------------------------------------- input

// t = the time of the press (the beat), now = the lift (the tap acts at the lift: the TAP button is near the pull zone)
static void tap_tempo(int64_t t, int64_t now)
{
    if (t > now || now - t > 1000) {
        t = now;  // (no press time of this touch)
    }
    if (s_ntap && t - s_taps[s_ntap - 1] > 2000) {
        s_ntap = 0;  // a pause: a new count
    }
    if (s_ntap == 8) {
        memmove(s_taps, s_taps + 1, sizeof(s_taps[0]) * 7);
        s_ntap = 7;
    }
    s_taps[s_ntap++] = t;
    if (s_ntap >= 2) {
        float avg = (float)(s_taps[s_ntap - 1] - s_taps[0]) / (float)(s_ntap - 1);
        if (avg > 0) {
            int bpm = (int)(60000.0f / avg + 0.5f);
            s_bpm = bpm < 30 ? 30 : bpm > 300 ? 300 : bpm;
            store_set("mid_bpm", (uint32_t)s_bpm);
        }
    }
    s_beat = (float)(now - t) * (float)s_bpm / 60000.0f;  // the press was the beat
    s_tap_flash = now;
}

// ---- RESET (this preset back to its defaults) and DISCONNECT (the Bluetooth link ends) in the EDIT grid
static bool disc_ok(void)
{
    return !s_usb && (ble_midi_state() == BM_LINKED || ble_midi_state() == BM_CONNECTED);
}

static int act_hit(int x, int y)
{
    if (s_simple || s_page != PG_GRID) {
        return -1;
    }
    for (int a = 0; a < ACT_N; a++) {
        if (in_rect(x, y, ACT_X[a], ACT_Y[a], ACT_W, ACT_H)) {
            return a;
        }
    }
    return -1;
}

static void act_do(int a, int64_t t)
{
    if (a == ACT_RESET) {
        lcfg_t def[N_LANES];
        preset_default(s_preset, def);
        for (int l = 0; l < N_LANES; l++) {
            s_cfg[l] = def[l];
            lane_save(l);
            lane_reset(l);
        }
    } else {
        ble_midi_disconnect();
    }
    s_done = a;
    s_done_t = t;
    sfx_play(SFX_BACK);
}

// a tap on an action button: the first tap asks "TAP AGAIN", the second tap (in ARM_MS) does it
static void act_tap(int a, int64_t t)
{
    if (a == ACT_DISC && !disc_ok()) {
        s_arm = -1;
        sfx_play(SFX_BUMP);  // (no Bluetooth link)
        return;
    }
    if (s_arm == a && t - s_arm_t < ARM_MIN_MS) {
        return;
    }
    if (s_arm == a && t - s_arm_t <= ARM_MS) {
        s_arm = -1;
        act_do(a, t);
        return;
    }
    s_arm = a;
    s_arm_t = t;
    s_done = -1;
    sfx_play(SFX_CLICK);
}

static void set_preset(int p)
{
    for (int f = 0; f < INPUT_MAX_POINTS; f++) {
        if (s_f[f].on) {
            finger_up(f);
        }
    }
    s_preset = p;
    store_set("mid_pre", (uint32_t)p);
    preset_load(p);
    s_learn = -1;
}

// The other side must not keep a bent pitch or a held sustain (at a mode change, an output
// change and the end of the app).
static void out_neutral(void)
{
    s_link = out_linked();
    s_learn = -1;
    for (int l = 0; l < N_LANES; l++) {
        const lcfg_t *C = &s_cfg[l];
        uint8_t ch = C->ch & 15;
        if (TARGETS[C->tgt].kind == K_BEND && s_lane[l].sent >= 0 && s_lane[l].sent != 8192) {
            put((uint8_t)(0xE0 | ch), 0, 64, 3, s_t_prev);
            s_lane[l].sent = 8192;
        }
        if (TARGETS[C->tgt].kind == K_SUST && s_lane[l].sent == 127) {
            put((uint8_t)(0xB0 | ch), 64, 0, 3, s_t_prev);
            s_lane[l].sent = 0;
        }
    }
    if (s_usb) {
        usb_midi_poll();
    } else {
        ble_midi_flush();
    }
}

// SIMPLE: control 0 is the big slider; the other controls are off.
static void simple_load(void)
{
    for (int l = 0; l < N_LANES; l++) {
        s_cfg[l] = lc(T_OFF, P_HOLD, 5, 15, false, false, 0);
        lane_reset(l);
    }
    s_cfg[0] = lc(ROLE_TGT[s_role], P_HOLD, 5, 15, false, s_center, 0);
    lane_reset(0);
    s_lane[0].c = s_lane[0].out = 0.5f;
    s_touched = false;
}

static void out_open(void)
{
    if (s_usb) {
        usb_midi_open();
    } else {
        ble_midi_open();
    }
}

static void out_close(void)
{
    if (s_usb) {
        usb_midi_close();
    } else {
        ble_midi_close();
    }
}

static void tilt_on(bool on)
{
    s_tilt = on;
    s_lock = false;
    if (on) {
        imu_set_level();  // the start position is the middle
        for (int l = L_TX; l <= L_TY; l++) {
            s_lane[l].tilt_f = 0;
            s_lane[l].c = 0.5f;
        }
    }
}

static void set_mode(bool simple)
{
    for (int f = 0; f < INPUT_MAX_POINTS; f++) {
        if (s_f[f].on) {
            finger_up(f);
        }
    }
    out_neutral();
    s_simple = simple;
    store_set("mid_simple", simple ? 1 : 0);
    s_page = PG_PLAY;
    tilt_on(false);
    if (simple) {
        simple_load();
    } else {
        preset_load(s_preset);
    }
}

static void set_out(bool usb)
{
    if (usb == s_usb) {
        return;
    }
    out_neutral();
    out_close();
    s_usb = usb;
    store_set("mid_out", usb ? 1 : 0);
    out_open();
    for (int l = 0; l < N_LANES; l++) {
        s_lane[l].sent = -1;  // the values go out again on the new link
    }
    s_mon_n = 0;
}

static int grid_lanes(int8_t *map)
{
    int n = tilt_avail() ? N_LANES : N_TOUCH;
    for (int i = 0; i < n; i++) {
        map[i] = (int8_t)i;
    }
    return n;
}

static void cfg_changed(int l, bool resend)
{
    if (resend) {
        s_lane[l].sent = -1;
    }
    lane_save(l);
}

static void edit_tap(int id, bool lng)
{
    int l = s_edit;
    lcfg_t *C = &s_cfg[l];
    lane_t *L = &s_lane[l];
    int step = lng ? 10 : 1;
    if (id >= E_TGT && id < E_TGT + N_TARGETS) {
        C->tgt = (uint8_t)(id - E_TGT);
        cfg_changed(l, true);
    } else if (id == E_CCM || id == E_CCP) {
        // the CC buttons change any controller target to a free CC number
        const target_t *T = &TARGETS[C->tgt];
        if (T->kind != K_CUSTOM) {
            C->cc = T->kind == K_CC || T->kind == K_SUST ? T->cc : C->cc;
            C->tgt = T_CC;
        }
        int v = C->cc + (id == E_CCP ? step : -step);
        C->cc = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        cfg_changed(l, true);
    } else if (id == E_CHM || id == E_CHP) {
        C->ch = (uint8_t)((C->ch + (id == E_CHP ? 1 : 15)) & 15);
        cfg_changed(l, true);
    } else if (id >= E_PAT && id < E_PAT + N_PATS) {
        C->pat = (uint8_t)(id - E_PAT);
        L->loop = L->rec = false;
        L->rec_n = 0;
        L->phase = 0;
        cfg_changed(l, false);
    } else if (id == E_RTM || id == E_RTP) {
        int max = l >= L_TX ? 10 : 15;
        int v = C->rate + (id == E_RTP ? 1 : -1);
        C->rate = (uint8_t)(v < 0 ? 0 : v > max ? max : v);
        cfg_changed(l, false);
    } else if (id == E_DPM || id == E_DPP) {
        int v = C->depth + (id == E_DPP ? 1 : -1);
        C->depth = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
        cfg_changed(l, false);
    } else if (id == E_SYNC) {
        C->sync = !C->sync;
        cfg_changed(l, false);
    } else if (id == E_SPRING) {
        C->spring = !C->spring;
        cfg_changed(l, false);
    } else if (id == E_INV) {
        C->inv = !C->inv;
        cfg_changed(l, false);
    } else if (id == E_LEARN) {
        s_learn = l;
        s_learn_t0 = s_t_prev;
        L->sent = -1;
    } else if (id == E_LEVEL) {
        imu_set_level();
        for (int k = L_TX; k <= L_TY; k++) {
            s_lane[k].tilt_f = 0;
        }
    } else {
        return;
    }
    sfx_play(SFX_CLICK);
}

// a touch on the EDIT page of a control: the top bar (BACK, the two tabs) or a button of the page
static void input_edit(int x, int y, bool tap, bool lng)
{
    if (!tap && !lng) {
        return;
    }
    if (y < TOP_H) {
        if (!tap) {
            return;
        }
        if (x < TB_X[0] + TB_W[0]) {
            s_page = PG_GRID;
            sfx_play(SFX_BACK);
        }
        for (int i = 0; i < 2; i++) {
            if (x >= TAB_X[i] && x < TAB_X[i] + TAB_W) {
                s_tab = i;
                sfx_play(SFX_CLICK);
            }
        }
        return;
    }
    el_t e[MAX_EL];
    int n = build(e);
    for (int i = 0; i < n; i++) {
        if (el_tap(e[i].id) && in_rect(x, y, e[i].x, e[i].y, e[i].w, e[i].h)) {
            bool cc = e[i].id == E_CCM || e[i].id == E_CCP;
            if (tap || cc) {
                edit_tap(e[i].id, lng);
            }
            return;
        }
    }
}

// a tap on the top bar of the SIMPLE mode
static void simple_bar(int x)
{
    int b = -1;
    for (int i = 0; i < (MC_RND ? SB_SPRING : SB_N); i++) {
        if (x >= SB_X[i] && x < SB_X[i] + SB_W[i]) {
            b = i;
        }
    }
    if (MC_RND && x < 0) {
        b = SB_SPRING;  // (the round view: SLIDER is on the bottom arc)
    }
    switch (b) {
    case SB_BACK:
        sfx_play(SFX_BACK);
        app_open(&app_menu);
        break;
    case SB_MODE:
        set_mode(false);
        sfx_play(SFX_CLICK);
        break;
    case SB_OUT:
        set_out(!s_usb);
        sfx_play(SFX_CLICK);
        break;
    case SB_SPRING:
        s_center = !s_center;
        store_set("mid_center", s_center ? 1 : 0);
        s_cfg[0].spring = s_center;
        if (s_center && !s_lane[0].held) {
            s_lane[0].springing = true;
        }
        sfx_play(SFX_CLICK);
        break;
    default:
        break;
    }
}

// a touch in the SIMPLE mode: the top bar or the row of the roles
static void input_simple(int x, int y, bool tap)
{
    if (!tap) {
        return;
    }
    if (y < TOP_H) {
        simple_bar(x);
        return;
    }
#if MC_RND
    if (in_rect(x, y, SB_X[SB_SPRING], EDIT_Y, SB_W[SB_SPRING], MODE_H)) {
        simple_bar(-1);
        return;
    }
#endif
    if (y >= ROLE_Y && y < ROLE_Y + ROLE_H) {
        int r = (x - BIG_X) * N_ROLES / BIG_W;
        if (r >= 0 && r < N_ROLES && r != s_role) {
            out_neutral();
            s_role = r;
            store_set("mid_role", (uint32_t)r);
            simple_load();
            sfx_play(SFX_CLICK);
        }
    }
}

// a touch on the top bar of the full mode (TAP, BACK, PRESET, TILT, EDIT)
static void input_bar(const input_event_t *ev, int x, bool tap)
{
    int b = -1;
    for (int i = 0; i < (MC_RND ? TB_EDIT : TB_N); i++) {
        if (x >= TB_X[i] && x < TB_X[i] + TB_W[i]) {
            b = i;
        }
    }
    if (!tap) {
        return;
    }
    switch (b) {
    case TB_TAP:
        tap_tempo(s_down_t, ev->t_ms);  // the time of the press, the place of the lift
        break;
    case TB_BACK:
        sfx_play(SFX_BACK);
        app_open(&app_menu);
        break;
    case TB_PRE:
        set_preset((s_preset + 1) % N_PRESETS);
        sfx_play(SFX_CLICK);
        break;
    case TB_TILT:
        if (!tilt_avail()) {
            break;
        }
        if (!s_tilt) {
            tilt_on(true);
        } else if (x < TB_X[TB_TILT] + TB_W[TB_TILT] / 2) {
            tilt_on(false);
        } else {
            s_lock = !s_lock;
        }
        sfx_play(SFX_CLICK);
        break;
    case TB_EDIT:
        s_page = s_page == PG_PLAY ? PG_GRID : PG_PLAY;
        sfx_play(SFX_CLICK);
        break;
    default:
        break;
    }
}

// a tap on the GRID page: an action button, or a tile that opens the EDIT page of its control
static void input_grid(const input_event_t *ev, int x, int y)
{
    int a = act_hit(x, y);
    if (a >= 0) {
        act_tap(a, ev->t_ms);
        return;
    }
    int8_t map[N_LANES];
    int n = grid_lanes(map);
    for (int i = 0; i < n; i++) {
        if (in_rect(x, y, TILE_X0 + (i % 4) * TILE_SX, MAIN_Y + (i / 4) * TILE_SY, TILE_W, TILE_H)) {
            s_edit = map[i];
            s_page = PG_EDIT;
            s_tab = TAB_TARGET;
            sfx_play(SFX_CLICK);
            return;
        }
    }
}

static void mc_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
#if MC_RND
    rvm_pt(&x, &y);
    if (ev->kind == IN_TAP && !s_simple && s_page != PG_EDIT &&
        in_rect(x, y, TB_X[TB_EDIT], EDIT_Y, TB_W[TB_EDIT], TOP_H - 8)) {
        s_page = s_page == PG_PLAY ? PG_GRID : PG_PLAY;  // (EDIT is on the bottom arc)
        sfx_play(SFX_CLICK);
        return;
    }
#endif
    bool tap = ev->kind == IN_TAP, lng = ev->kind == IN_LONG;
    if (ev->kind == IN_DOWN) {
        s_down_t = ev->t_ms;
    }
    if (tap && s_arm >= 0 && act_hit(x, y) != s_arm) {
        s_arm = -1;  // a tap on another place: no "TAP AGAIN"
    }
    if (s_page == PG_EDIT) {
        input_edit(x, y, tap, lng);
        return;
    }
    if (s_simple) {
        input_simple(x, y, tap);
        return;
    }
    if (tap && in_rect(x, y, MODE_X, MODE_Y, MODE_W, MODE_H)) {
        set_mode(true);
        sfx_play(SFX_CLICK);
        return;
    }
    // (no long press in the full mode: DISCONNECT and RESET are buttons of the EDIT grid with a second tap)
    if (y < TOP_H) {
        input_bar(ev, x, tap);
        return;
    }
    if (s_page == PG_GRID && tap) {
        input_grid(ev, x, y);
    }
}

// ---------------------------------------------------------------- drawing

#define SCALE_CAP (MC_TAB5 ? 3 : 2)
static int s_scale_cap = SCALE_CAP;  // the largest text scale of the buttons

static int fit_scale(const char *s, int w)
{
    for (int k = s_scale_cap; k >= 2; k--) {
        if (gfx_text_width(s, k) <= w - 4) {
            return k;
        }
    }
    return 1;
}

// Text with a dark shadow (it stays clear on the bars and on the thumb), centered in x..x+w.
static void stext(int x, int w, int y, const char *s, int scale, uint16_t c)
{
    gfx_text_center_in(x + 1, w, y + 1, s, scale, rgb(0, 0, 0));
    gfx_text_center_in(x, w, y, s, scale, c);
}

static void button(int x, int y, int w, int h, const char *a, const char *b, uint16_t bg, uint16_t fg)
{
    gfx_fill_rect(x, y, w, h, bg);
    gfx_frame_rect(x, y, w, h, 1, FRAME);
    if (b) {
        int s2 = fit_scale(b, w);
#if MC_TAB5
        // the Tab5: the label too in scale 2 when it fits
        int la = gfx_text_width(a, 2) <= w - 4 ? 2 : 1, gap = 7 * la + 5;
        int y0 = y + (h - (gap + 7 * s2)) / 2;
        gfx_text_center_in(x, w, y0, a, la, DIM);
        gfx_text_center_in(x, w, y0 + gap, b, s2, fg);
#else
        int y0 = y + (h - (10 + 7 * s2)) / 2;
        gfx_text_center_in(x, w, y0, a, 1, DIM);
        gfx_text_center_in(x, w, y0 + 10, b, s2, fg);
#endif
    } else {
        int s = fit_scale(a, w);
        gfx_text_center_in(x, w, y + (h - 7 * s) / 2, a, s, fg);
    }
}

// The value text of control l (what the other side gets).
static void value_text(int l, char *buf, int len)
{
    const lcfg_t *C = &s_cfg[l];
    float v = s_lane[l].out;
    switch (TARGETS[C->tgt].kind) {
    case K_OFF:
        snprintf(buf, len, "--");
        break;
    case K_BEND:
        snprintf(buf, len, "%+d", (int)(v * 16383 + 0.5f) - 8192);
        break;
    case K_SUST:
        snprintf(buf, len, "%s", s_lane[l].sent == 127 ? "ON" : "OFF");
        break;
    default:
        snprintf(buf, len, "%d", (int)(v * 127 + 0.5f));
        break;
    }
}

// The motion of control l in two parts: the pattern and its rate ("" if none).
static void motion_parts(int l, const char **pat, const char **rate)
{
    const lcfg_t *C = &s_cfg[l];
    static char deg[12];
    *rate = "";
    if (l >= L_TX) {
        snprintf(deg, sizeof(deg), "%d DEG", 10 + 5 * (C->rate > 10 ? 10 : C->rate));
        *pat = deg;
    } else if (C->pat == P_HOLD) {
        *pat = C->spring ? "SPRING" : "";
    } else if (C->pat == P_REC) {
        *pat = s_lane[l].rec ? "REC..." : s_lane[l].loop ? "LOOP" : "REC";
    } else {
        *pat = PAT_NAMES[C->pat];
        *rate = C->sync ? SYNC_NAMES[C->rate] : RATE_NAMES[C->rate];
    }
}

static void motion_text(int l, char *buf, int len)
{
    const char *a, *b;
    motion_parts(l, &a, &b);
    snprintf(buf, len, "%s%s%s", a, b[0] ? " " : "", b);
}

static void status_line(char *buf, int len, uint16_t *col)
{
    char peer[24];
    if (s_usb) {
        switch (usb_midi_state()) {
        case UM_OFF:
            snprintf(buf, len, "USB OFF");
            *col = DIM;
            break;
        case UM_WAIT:
            snprintf(buf, len, "USB: NO COMPUTER");
            *col = rgb(250, 200, 60);
            break;
        case UM_LINKED:
            snprintf(buf, len, "USB LINKED: TFN MIDI");
            *col = rgb(80, 230, 120);
            break;
        default:
            snprintf(buf, len, "USB FAILED");
            *col = rgb(255, 80, 80);
            break;
        }
        return;
    }
    switch (ble_midi_state()) {
    case BM_OFF:
        snprintf(buf, len, "BLUETOOTH OFF");
        *col = DIM;
        break;
    case BM_STARTING:
        snprintf(buf, len, "BLUETOOTH STARTS...");
        *col = rgb(250, 200, 60);
        break;
    case BM_ADVERTISING:
        snprintf(buf, len, "BT READY: \"%s\"", ble_midi_name());
        *col = rgb(250, 200, 60);
        break;
    case BM_CONNECTED:
        snprintf(buf, len, "BT: OPEN THE MIDI PORT");
        *col = rgb(120, 190, 255);
        break;
    case BM_LINKED:
        ble_midi_peer(peer, sizeof(peer));
        snprintf(buf, len, "BT LINKED %s", peer);
        *col = rgb(80, 230, 120);
        break;
    default:
        snprintf(buf, len, "BLUETOOTH FAILED");
        *col = rgb(255, 80, 80);
        break;
    }
}

static void monitor_line(char *buf, int len)
{
    int p = snprintf(buf, len, "#%lu", (unsigned long)(s_usb ? usb_midi_sent() : ble_midi_sent()));
    for (int i = 0; i < s_mon_n && p < len; i++) {
        const uint8_t *m = s_mon[i];
        int ch = (m[0] & 15) + 1;
        switch (m[0] & 0xF0) {
        case 0xB0:
            p += snprintf(buf + p, len - p, " CC%d=%d/%d", m[1], m[2], ch);
            break;
        case 0xE0:
            p += snprintf(buf + p, len - p, " PB%+d/%d", (m[1] | m[2] << 7) - 8192, ch);
            break;
        default:
            p += snprintf(buf + p, len - p, " AT=%d/%d", m[1], ch);
            break;
        }
    }
}

static void draw_status(void)
{
    char a[48], b[64];
    uint16_t col;
#if MC_RND
    // the round view: two lines in the middle of the top cap, each as long as its chord
    status_line(a, sizeof(a), &col);
    monitor_line(b, sizeof(b));
    int ma = (2 * rvm_ch(ST_Y) - 24) / CHW, mb = (2 * rvm_ch(ST_Y + 13) - 12) / CHW;
    a[ma < (int)sizeof(a) - 1 ? ma : (int)sizeof(a) - 1] = 0;
    b[mb < (int)sizeof(b) - 1 ? mb : (int)sizeof(b) - 1] = 0;
    int aw = gfx_text_width(a, 1);
    gfx_fill_circle(160 - aw / 2 - 8, ST_Y + 3, 3, col);
    gfx_text_center_in(0, SW, ST_Y, a, 1, col);
    gfx_text_center_in(0, SW, ST_Y + 13, b, 1, DIM);
    return;
#endif
    gfx_fill_rect(ST_X, MC_WIDE ? 0 : MAIN_B, ST_W, ST_H, rgb(16, 18, 28));
    status_line(a, sizeof(a), &col);
    monitor_line(b, sizeof(b));
    int maxc = (ST_W - 16) / 6;
    a[maxc < (int)sizeof(a) - 1 ? maxc : (int)sizeof(a) - 1] = 0;
    b[maxc < (int)sizeof(b) - 1 ? maxc : (int)sizeof(b) - 1] = 0;
#if MC_TAB5
    // the Tab5: the state in scale 2 when it fits, the monitor below it
    int as = gfx_text_width(a, 2) <= ST_W - 18 ? 2 : 1;
    gfx_fill_circle(ST_X + 6, ST_Y + 3 * as, 3 * as - 1, col);
    gfx_text(ST_X + 15, ST_Y, a, as, col);
    gfx_text(ST_X + 4, ST_Y + 20, b, 1, DIM);
#else
    gfx_fill_circle(ST_X + 6, ST_Y + 3, 3, col);
    gfx_text(ST_X + 13, ST_Y, a, 1, col);
    gfx_text(ST_X + 4, ST_Y + 11, b, 1, DIM);
#endif
#if MC_WIDE
    char c[48];
    snprintf(c, sizeof(c), "MTU %d IN %lu", ble_midi_mtu(), (unsigned long)ble_midi_received());
    if (!s_usb && (ble_midi_state() == BM_LINKED || ble_midi_state() == BM_CONNECTED)) {
        gfx_text(ST_X + 4, ST_Y + (MC_TAB5 ? 32 : 22), c, 1, rgb(80, 86, 110));
    }
#endif
}

static void draw_topbar(int64_t now)
{
    char a[24], b[16];
    int h = TOP_H - 2;
    gfx_fill_rect(0, 0, MC_WIDE ? ST_X : SW, TOP_H, BG);
    button(TB_X[TB_BACK], 1, TB_W[TB_BACK], h, "<", NULL, PANEL, WHITE);
    snprintf(a, sizeof(a), "PRESET %d", s_preset + 1);
    button(TB_X[TB_PRE], 1, TB_W[TB_PRE], h, a, PRESET_NAMES[s_preset], PANEL, WHITE);
    // TAP: the tempo; it flashes on each beat
    float clk = out_clock_bpm();
    snprintf(b, sizeof(b), "%d", (int)(cur_bpm() + 0.5f));
    float fr = s_beat - floorf(s_beat);
    bool flash = fr < 0.12f || now - s_tap_flash < 80;
    button(TB_X[TB_TAP], 1, TB_W[TB_TAP], h, clk > 0 ? "CLOCK" : "TAP", b, flash ? rgb(70, 60, 30) : PANEL,
           clk > 0 ? rgb(80, 230, 120) : WHITE);
    // TILT: off / on with LOCK (hidden with no IMU)
    int tx = TB_X[TB_TILT], tw = TB_W[TB_TILT];
    if (!tilt_avail()) {
        gfx_fill_rect(tx, 1, tw, h, BG);
    } else if (!s_tilt) {
        button(tx, 1, tw, h, "TILT", "OFF", PANEL, DIM);
    } else {
        int hw = tw / 2;
        button(tx, 1, hw - 1, h, "TILT", "ON", rgb(20, 60, 50), WHITE);
        button(tx + hw, 1, tw - hw, h, "LOCK", s_lock ? "ON" : "OFF", s_lock ? rgb(80, 50, 20) : PANEL,
               s_lock ? rgb(255, 190, 80) : DIM);
        // two small bars: the tilt values
        for (int k = 0; k < 2; k++) {
            int bw = (int)(s_lane[L_TX + k].out * (hw - 9));
            gfx_fill_rect(tx + 3, h - 6 + k * 3, hw - 7, 2, rgb(30, 36, 50));
            gfx_fill_rect(tx + 3, h - 6 + k * 3, bw, 2, lane_col(L_TX + k, 255));
        }
    }
#if MC_RND
    button(TB_X[TB_EDIT], EDIT_Y, TB_W[TB_EDIT], MODE_H, s_page == PG_PLAY ? "EDIT" : "PLAY", NULL,
           s_page == PG_PLAY ? PANEL : ACCENT, WHITE);
#else
    button(TB_X[TB_EDIT], 1, TB_W[TB_EDIT], h, s_page == PG_PLAY ? "EDIT" : "PLAY", NULL,
           s_page == PG_PLAY ? PANEL : ACCENT, WHITE);
#endif
    button(MODE_X, MODE_Y, MODE_W, MODE_H, "MODE", "ADVANCED", PANEL, WHITE);
    // the 1-px gaps between the buttons are in the BG fill
}

static void draw_slider(int i)
{
    int x = SL_X0 + i * SL_STEP, y = MAIN_Y, w = SL_W, h = MAIN_B - MAIN_Y;
    const lane_t *L = &s_lane[i];
    const lcfg_t *C = &s_cfg[i];
    bool off = TARGETS[C->tgt].kind == K_OFF;
    gfx_fill_rect(x, y, w, h, PANEL);
    int span = SL_B - SL_T;
    int vy = SL_B - (int)(L->out * span + 0.5f);
    uint16_t col = lane_col(i, L->held ? 200 : 140);
    if (!off) {
        if (TARGETS[C->tgt].mid) {
            int my = (SL_B + SL_T) / 2;
            int y0 = vy < my ? vy : my, y1 = vy < my ? my : vy;
            gfx_fill_rect(x + 3, y0, w - 6, y1 - y0 + 1, col);
            gfx_fill_rect(x + 3, my, w - 6, 1, DIM);
        } else {
            gfx_fill_rect(x + 3, vy, w - 6, MAIN_B - 3 - vy, col);
        }
        // the middle of the pattern (where the finger put it)
        if (C->pat != P_HOLD) {
            float c = C->inv ? 1 - L->c : L->c;
            int cy = SL_B - (int)(c * span + 0.5f);
            gfx_fill_rect(x + 3, cy - 1, w - 6, 3, WHITE);
        }
    }
    gfx_frame_rect(x, y, w, h, L->held ? 2 : 1, L->held ? WHITE : FRAME);
    const char *name = C->tgt == T_CC ? "CC" : TARGETS[C->tgt].name;
    char t[16];
    if (C->tgt == T_CC) {
        snprintf(t, sizeof(t), "CC%d", C->cc);
        name = t;
    }
    int s = fit_scale(name, w);
    stext(x, w, y + 5, name, s, WHITE);
    char v[16];
    value_text(i, v, sizeof(v));
    stext(x, w, MAIN_B - 7 * fit_scale(v, w) - 5, v, fit_scale(v, w), WHITE);
#if MC_TAB5
    // the Tab5: a tall slider; the pattern and the rate on two lines, in scale 2 when they fit
    const char *pa, *ra;
    motion_parts(i, &pa, &ra);
    int ms = gfx_text_width(pa, 2) <= w - 4 && gfx_text_width(ra, 2) <= w - 4 ? 2 : 1;
    stext(x, w, y + 7 * s + 12, pa, ms, rgb(220, 220, 235));
    stext(x, w, y + 7 * s + 16 + 7 * ms, ra, ms, rgb(220, 220, 235));
#elif BOARD_BAR_SCREEN
    char m[24];
    motion_text(i, m, sizeof(m));
    stext(x, w, y + 7 * s + 9, m, 1, rgb(220, 220, 235));
#else
    // a narrow slider: the pattern and the rate on two lines
    const char *pa, *ra;
    motion_parts(i, &pa, &ra);
    stext(x, w, y + 7 * s + 9, pa, 1, rgb(220, 220, 235));
    stext(x, w, y + 7 * s + 19, ra, 1, rgb(220, 220, 235));
#endif
}

static void draw_xy(void)
{
    int x = XY_X, y = MAIN_Y, w = XY_W, h = MAIN_B - MAIN_Y;
    const lane_t *LX = &s_lane[L_X], *LY = &s_lane[L_Y];
    bool held = LX->held;
    gfx_fill_rect(x, y, w, h, PANEL);
    for (int k = 1; k < 4; k++) {
        gfx_fill_rect(x + XY_M + (w - 2 * XY_M) * k / 4, y + 2, 1, h - 4, k == 2 ? FRAME : rgb(40, 46, 64));
        gfx_fill_rect(x + 2, y + XY_M + (h - 2 * XY_M) * k / 4, w - 4, 1, k == 2 ? FRAME : rgb(40, 46, 64));
    }
    int px = x + XY_M + (int)(LX->out * (w - 2 * XY_M) + 0.5f);
    int py = MAIN_B - XY_M - (int)(LY->out * (h - 2 * XY_M) + 0.5f);
    gfx_fill_rect(px, y + 2, 1, h - 4, lane_col(L_X, 160));
    gfx_fill_rect(x + 2, py, w - 4, 1, lane_col(L_Y, 160));
    const int dr = MC_TAB5 ? 6 : 0;  // (the Tab5: a bigger dot for the bigger pad)
    gfx_fill_circle(px, py, (held ? 12 : 9) + dr, lane_col(L_X, 230));
    gfx_fill_circle(px, py, (held ? 6 : 4) + dr / 2, WHITE);
    gfx_frame_rect(x, y, w, h, held ? 2 : 1, held ? WHITE : FRAME);
    char a[24], v[12];
    for (int k = 0; k < 2; k++) {
        int l = L_X + k;
        const lcfg_t *C = &s_cfg[l];
        value_text(l, v, sizeof(v));
        if (C->tgt == T_CC) {
            snprintf(a, sizeof(a), "%s CC%d %s", LANE_NAMES[l], C->cc, v);
        } else {
            snprintf(a, sizeof(a), "%s %s %s", LANE_NAMES[l], TARGETS[C->tgt].name, v);
        }
        const int ts = MC_TAB5 ? 2 : 1, lh = MC_TAB5 ? 18 : 11;  // (the Tab5: scale 2 text)
        int mw = (w - 8) / (CHW * ts);
        if ((int)strlen(a) > mw) {
            a[mw] = 0;
        }
        // both names at the top: the dot is often at the bottom (a value of 0)
        gfx_text(x + 6, y + 6 + (1 - k) * lh, a, ts, rgb(0, 0, 0));
        gfx_text(x + 5, y + 5 + (1 - k) * lh, a, ts, lane_col(l, 255));
    }
}

static void draw_grid(void)
{
    int8_t map[N_LANES];
    int n = grid_lanes(map);
    for (int i = 0; i < n; i++) {
        int l = map[i];
        int x = TILE_X0 + (i % 4) * TILE_SX, y = MAIN_Y + (i / 4) * TILE_SY;
        const lcfg_t *C = &s_cfg[l];
        gfx_fill_rect(x, y, TILE_W, TILE_H, PANEL);
        gfx_fill_rect(x, y, 5, TILE_H, lane_col(l, 230));
        gfx_frame_rect(x, y, TILE_W, TILE_H, 1, FRAME);
        char t[24], m[24], c[24];
        if (C->tgt == T_CC) {
            snprintf(t, sizeof(t), "CC%d", C->cc);
        } else {
            snprintf(t, sizeof(t), "%s", TARGETS[C->tgt].name);
        }
        motion_text(l, m, sizeof(m));
        snprintf(c, sizeof(c), "CH %d%s", C->ch + 1, C->inv ? " INV" : "");
        int mw = (TILE_W - 12) / CHW;
        if ((int)strlen(m) > mw) {
            m[mw] = 0;
        }
#if MC_TAB5
        // a tile of 154 x 108: the name and the channel, the target (scale 3), the motion
        mw = (TILE_W - 18) / 12;
        if ((int)strlen(m) > mw) {
            m[mw] = 0;
        }
        gfx_text(x + 12, y + 10, LANE_NAMES[l], 2, lane_col(l, 255));
        gfx_text(x + TILE_W - 6 - gfx_text_width(c, 2), y + 10, c, 2, DIM);
        gfx_text(x + 12, y + 38, t, fit_scale(t, TILE_W - 12), WHITE);
        gfx_text(x + 12, y + 78, m, 2, rgb(220, 220, 235));
#elif BOARD_BAR_SCREEN
        gfx_text(x + 11, y + 6, LANE_NAMES[l], 1, lane_col(l, 255));
        gfx_text(x + 11, y + 18, t, 2, WHITE);
        gfx_text(x + 11, y + 37, m, 1, rgb(220, 220, 235));
        gfx_text(x + 11, y + 48, c, 1, DIM);
#else
        // a tile of 46 px: the name and the channel, the target, the motion (INV in front of it)
        snprintf(c, sizeof(c), "CH%d", C->ch + 1);
        gfx_text(x + 10, y + 5, LANE_NAMES[l], 1, lane_col(l, 255));
        gfx_text(x + TILE_W - 4 - gfx_text_width(c, 1), y + 5, c, 1, DIM);
        gfx_text(x + 10, y + 16, t, fit_scale(t, TILE_W - 8), WHITE);
        char m2[32];
        snprintf(m2, sizeof(m2), "%s%s", C->inv ? "INV " : "", m);
        m2[mw < (int)sizeof(m2) - 1 ? mw : (int)sizeof(m2) - 1] = 0;
        gfx_text(x + 10, y + 36, m2, 1, rgb(220, 220, 235));
#endif
    }
    if (n < N_LANES) {
        // no IMU: the place of the two tilt tiles shows how to use the editor
        int x = TILE_X0 + 2 * TILE_SX, y = MAIN_Y + TILE_SY;
        const int hs = MC_TAB5 ? 2 : 1;  // (the Tab5: scale 2)
        gfx_text(x + 6, y + 10, "TAP A CONTROL", hs, DIM);
        gfx_text(x + 6, y + 10 + 12 * hs, "TO SET IT", hs, DIM);
    }
    // RESET and DISCONNECT: a first tap asks TAP AGAIN (red, a bar that gets shorter), a second tap does it
    for (int a = 0; a < ACT_N; a++) {
        int x = ACT_X[a], y = ACT_Y[a];
        bool arm = s_arm == a, done = s_done == a && s_t_prev - s_done_t < 1500;
        bool off = a == ACT_DISC && !disc_ok();
        const char *top = a == ACT_RESET ? "RESET PRESET" : "BLUETOOTH";
        char low[24];
        if (arm) {
            snprintf(low, sizeof(low), "TAP AGAIN");
        } else if (done) {
            snprintf(low, sizeof(low), "DONE");
        } else if (a == ACT_RESET) {
            snprintf(low, sizeof(low), "%s", PRESET_NAMES[s_preset]);
        } else {
            snprintf(low, sizeof(low), "%s", off ? (s_usb ? "USB OUTPUT" : "NO LINK") : "DISCONNECT");
        }
        uint16_t bg = arm ? rgb(150, 36, 40) : done ? rgb(30, 80, 50) : PANEL;
        button(x, y, ACT_W, ACT_H, top, low, bg, off ? DIM : arm ? rgb(255, 230, 120) : WHITE);
        if (arm) {
            int left = ARM_MS - (int)(s_t_prev - s_arm_t);
            if (left < 0) {
                left = 0;
            }
            gfx_fill_rect(x + 3, y + ACT_H - 6, (ACT_W - 6) * left / ARM_MS, 3, rgb(255, 200, 90));
        }
    }
}

static void draw_el(const el_t *e, int64_t now)
{
    int l = s_edit;
    const lcfg_t *C = &s_cfg[l];
    const lane_t *L = &s_lane[l];
    int id = e->id;
    char a[40], b[24];
    uint16_t on = ACCENT;
    if (id >= E_TGT && id < E_TGT + N_TARGETS) {
        bool sel = C->tgt == id - E_TGT;
        s_scale_cap = 2;  // (the Tab5: one text size in the row; scale 3 does not fit all the names)
        button(e->x, e->y, e->w, e->h, TARGETS[id - E_TGT].name, NULL, sel ? on : PANEL, WHITE);
        s_scale_cap = SCALE_CAP;
        return;
    }
    if (id >= E_PAT && id < E_PAT + N_PATS) {
        bool sel = C->pat == id - E_PAT;
        s_scale_cap = 2;
        button(e->x, e->y, e->w, e->h, PAT_NAMES[id - E_PAT], NULL, sel ? on : PANEL, WHITE);
        s_scale_cap = SCALE_CAP;
        return;
    }
    bool rate_used = l >= L_TX || (C->pat != P_HOLD && C->pat != P_REC);
    bool depth_used = C->pat != P_HOLD && C->pat != P_REC;
    switch (id) {
    case E_CCM:
    case E_RTM:
    case E_DPM:
    case E_CHM:
        button(e->x, e->y, e->w, e->h, "-", NULL, PANEL,
               (id == E_RTM && !rate_used) || (id == E_DPM && !depth_used) ? DIM : WHITE);
        break;
    case E_CCP:
    case E_RTP:
    case E_DPP:
    case E_CHP:
        button(e->x, e->y, e->w, e->h, "+", NULL, PANEL,
               (id == E_RTP && !rate_used) || (id == E_DPP && !depth_used) ? DIM : WHITE);
        break;
    case E_CCV: {
        int k = TARGETS[C->tgt].kind;
        if (k == K_CC || k == K_SUST || k == K_CUSTOM) {
            snprintf(b, sizeof(b), "%d", cc_of(C));
        } else {
            snprintf(b, sizeof(b), "--");
        }
        button(e->x, e->y, e->w, e->h, "CC", b, BG, k == K_CUSTOM ? WHITE : DIM);
        break;
    }
    case E_CHV:
        snprintf(b, sizeof(b), "%d", C->ch + 1);
        button(e->x, e->y, e->w, e->h, "CHANNEL", b, BG, WHITE);
        break;
    case E_RTV:
        if (l >= L_TX) {
            snprintf(b, sizeof(b), "%d DEG", 10 + 5 * (C->rate > 10 ? 10 : C->rate));
        } else {
            snprintf(b, sizeof(b), "%s", C->sync ? SYNC_NAMES[C->rate] : RATE_NAMES[C->rate]);
        }
        button(e->x, e->y, e->w, e->h, l >= L_TX ? "RANGE" : "RATE", b, BG, rate_used ? WHITE : DIM);
        break;
    case E_DPV:
        snprintf(b, sizeof(b), "%d%%", (C->depth + 1) * 100 / 16);
        button(e->x, e->y, e->w, e->h, "DEPTH", b, BG, depth_used ? WHITE : DIM);
        break;
    case E_SYNC:
        button(e->x, e->y, e->w, e->h, "SYNC", C->sync ? "TEMPO" : "FREE", C->sync ? on : PANEL,
               rate_used ? WHITE : DIM);
        break;
    case E_SPRING:
        button(e->x, e->y, e->w, e->h, "SPRING", C->spring ? "ON" : "OFF", C->spring ? on : PANEL, WHITE);
        break;
    case E_INV:
        button(e->x, e->y, e->w, e->h, "INVERT", C->inv ? "ON" : "OFF", C->inv ? on : PANEL, WHITE);
        break;
    case E_LEARN: {
        bool act = s_learn == l;
        button(e->x, e->y, e->w, e->h, "SOLO SWEEP", "LEARN", act ? rgb(120, 70, 20) : PANEL, WHITE);
        break;
    }
    case E_LEVEL:
        button(e->x, e->y, e->w, e->h, "MIDDLE = NOW", "LEVEL", PANEL, WHITE);
        break;
    case E_NOTE:
        gfx_fill_rect(e->x, e->y, e->w, e->h, BG);
        snprintf(a, sizeof(a), "%s", l == L_TX ? "TILT LEFT / RIGHT" : "TILT FORWARD / BACK");
#if MC_TAB5
        gfx_text(e->x + 8, e->y + 10, a, 2, lane_col(l, 255));
        gfx_text(e->x + 8, e->y + 36, "THE START POSITION IS THE MIDDLE", 2, DIM);
#else
        gfx_text(e->x + 4, e->y + 6, a, 1, lane_col(l, 255));
        gfx_text(e->x + 4, e->y + 20, "THE START POSITION IS THE MIDDLE", 1, DIM);
#endif
        break;
    case E_PREVIEW: {
        // the value now: a bar, and the pattern middle
        gfx_fill_rect(e->x, e->y, e->w, e->h, BG);
#if MC_TAB5
        int bx = e->x + 4, bw = e->w - 8, by = e->y + e->h - 36, bh = 30;  // (the Tab5: a thick bar)
#else
        int bx = e->x + 4, bw = e->w - 8, by = e->y + e->h - 12, bh = 8;
#endif
        gfx_fill_rect(bx, by, bw, bh, rgb(30, 36, 50));
        gfx_fill_rect(bx, by, (int)(L->out * bw), bh, lane_col(l, 230));
        value_text(l, b, sizeof(b));
        motion_text(l, a, sizeof(a));
        char c[96];
        snprintf(c, sizeof(c), "NOW %s  %s", b, a);
#if MC_TAB5
        gfx_text(bx, e->y + 8, c, gfx_text_width(c, 3) <= bw ? 3 : 2, WHITE);  // (the Tab5: scale 3, else 2)
#else
        gfx_text(bx, e->y + 4, c, 1, WHITE);
#endif
        (void)now;
        break;
    }
    default:
        break;
    }
}

static void draw_editor(int64_t now)
{
    int l = s_edit;
    const lcfg_t *C = &s_cfg[l];
    int h = TOP_H - 2;
    button(TB_X[0], 1, TB_W[0], h, "<", NULL, PANEL, WHITE);
#if !MC_RND
    gfx_fill_rect(TITLE_X - 2, 0, 4, TOP_H, lane_col(l, 230));
#endif
    char a[32], b[32];
    snprintf(a, sizeof(a), "%s", LANE_NAMES[l]);
    if (C->tgt == T_CC) {
        snprintf(b, sizeof(b), "CC%d CH%d", C->cc, C->ch + 1);
    } else {
        snprintf(b, sizeof(b), "%s CH%d", TARGETS[C->tgt].name, C->ch + 1);
    }
#if MC_TAB5
    gfx_text(TITLE_X + 6, 6, a, 2, lane_col(l, 255));
    gfx_text(TITLE_X + 6, 26, b, 3, WHITE);
#elif BOARD_BAR_SCREEN
    gfx_text(TITLE_X + 6, 5, a, 1, lane_col(l, 255));
    gfx_text(TITLE_X + 6, 18, b, 2, WHITE);
#elif MC_RND
    // (the round view: the name and the target in the top cap)
    gfx_text_center_in(0, SW, ST_Y, a, 1, lane_col(l, 255));
    gfx_text_center_in(0, SW, ST_Y + 13, b, 1, WHITE);
#else
    gfx_text(TITLE_X + 6, 10, a, 1, lane_col(l, 255));
    gfx_text(TITLE_X + 6, 24, b, 1, WHITE);
#endif
    static const char *const TABS[2] = {"TARGET", "MOTION"};
    for (int i = 0; i < 2; i++) {
        button(TAB_X[i], 1, TAB_W, h, TABS[i], NULL, s_tab == i ? ACCENT : PANEL, WHITE);
    }
    el_t e[MAX_EL];
    int n = build(e);
    for (int i = 0; i < n; i++) {
        draw_el(&e[i], now);
    }
}

// SIMPLE: the top bar, the big slider and the row of roles.
static void draw_simple(void)
{
    int h = TOP_H - 2;
    s_scale_cap = MC_WIDE ? SCALE_CAP : 1;  // the CoreS3: one text size in the narrow buttons
    button(SB_X[SB_BACK], 1, SB_W[SB_BACK], h, "<", NULL, PANEL, WHITE);
    button(SB_X[SB_MODE], 1, SB_W[SB_MODE], h, "MODE", "SIMPLE", PANEL, WHITE);
    button(SB_X[SB_OUT], 1, SB_W[SB_OUT], h, "OUTPUT", s_usb ? "USB" : "BLUETOOTH",
           s_usb ? rgb(40, 50, 30) : rgb(20, 36, 70), s_usb ? rgb(190, 240, 120) : rgb(140, 190, 255));
    button(SB_X[SB_SPRING], MC_RND ? EDIT_Y : 1, SB_W[SB_SPRING], MC_RND ? MODE_H : h, "SLIDER",
           s_center ? "TO CENTER" : "FREE", PANEL, WHITE);
    // the big slider: the value from the left, or from the middle (TO CENTER and PITCH)
    const lane_t *L = &s_lane[0];
    int x = BIG_X, y = BIG_Y, w = BIG_W, hh = BIG_B - BIG_Y;
    int span = w - 2 * BIG_M;
    int vx = x + BIG_M + (int)(L->out * span + 0.5f);
    int mx = x + BIG_M + span / 2;
    uint16_t col = hsv(LANE_HUE[s_role], 200, L->held ? 230 : 170);
    gfx_fill_rect(x, y, w, hh, PANEL);
    for (int k = 0; k <= 8; k++) {
        int tx = x + BIG_M + span * k / 8;
        gfx_fill_rect(tx, y + hh - (k % 4 ? 8 : 14), 1, k % 4 ? 8 : 14, FRAME);
    }
    bool mid = s_center || TARGETS[ROLE_TGT[s_role]].mid;
    if (mid) {
        int a = vx < mx ? vx : mx, b = vx < mx ? mx : vx;
        gfx_fill_rect(a, y + 4, b - a + 1, hh - 8, col);
        gfx_fill_rect(mx, y + 2, 1, hh - 4, DIM);
    } else {
        gfx_fill_rect(x + 3, y + 4, vx - x - 3, hh - 8, col);
    }
    gfx_fill_rect(vx - 5, y + 2, 11, hh - 4, WHITE);  // the thumb
    gfx_frame_rect(x, y, w, hh, L->held ? 2 : 1, L->held ? WHITE : FRAME);
    char v[16];
    if (!s_touched) {
        snprintf(v, sizeof(v), "--");
    } else {
        value_text(0, v, sizeof(v));
    }
    int sc = MC_TAB5 ? 5 : BOARD_BAR_SCREEN ? 3 : 2;
    gfx_text(x + 11, y + 9, ROLE_NAMES[s_role], sc, rgb(0, 0, 0));
    gfx_text(x + 10, y + 8, ROLE_NAMES[s_role], sc, WHITE);
    int vy = y + 8 + 7 * sc + 8;  // the value under the name (the thumb is often at the right)
    gfx_text(x + 11, vy + 1, v, sc, rgb(0, 0, 0));
    gfx_text(x + 10, vy, v, sc, WHITE);
    // the roles
    for (int i = 0; i < N_ROLES; i++) {
        int bx = BIG_X + i * BIG_W / N_ROLES, bw = BIG_W / N_ROLES - 4;
        bool on = i == s_role;
        button(bx, ROLE_Y, bw, ROLE_H, ROLE_NAMES[i], NULL, on ? hsv(LANE_HUE[i], 200, 120) : PANEL, on ? WHITE : DIM);
    }
    s_scale_cap = SCALE_CAP;
}

static void mc_render(int64_t now)
{
    gfx_fill(BG);
    if (s_simple) {
        draw_status();
        draw_simple();
        return;
    }
    if (s_page == PG_EDIT) {
        draw_editor(now);
        return;
    }
    draw_topbar(now);
    draw_status();
    if (s_page == PG_PLAY) {
        for (int i = 0; i < 4; i++) {
            draw_slider(i);
        }
        draw_xy();
    } else {
        draw_grid();
    }
}

// ---------------------------------------------------------------- app

static void mc_enter(int64_t now)
{
    if (!s_ready) {
        s_ready = true;
        uint32_t p = store_get("mid_pre", 0);
        s_preset = p < N_PRESETS ? (int)p : 0;
        uint32_t bpm = store_get("mid_bpm", 120);
        s_bpm = bpm >= 30 && bpm <= 300 ? (int)bpm : 120;
        s_simple = store_get("mid_simple", 1) != 0;
        uint32_t r = store_get("mid_role", 0);
        s_role = r < N_ROLES ? (int)r : 0;
        s_center = store_get("mid_center", 0) != 0;
        s_usb = store_get("mid_out", 0) != 0;
        preset_load(s_preset);
    }
    if (s_simple) {
        simple_load();
    }
    for (int l = 0; l < N_LANES; l++) {
        s_lane[l].held = false;
        s_lane[l].rec = false;
        s_lane[l].sent = -1;
    }
    for (int l = 0; l < N_TOUCH; l++) {
        s_owner[l] = -1;
    }
    memset(s_f, 0, sizeof(s_f));
    s_page = PG_PLAY;
    s_tilt = s_lock = false;
    s_learn = -1;
    s_t_prev = s_frame_t0 = now;
    s_ntap = 0;
    s_mon_n = 0;
    out_open();
}

static void mc_leave(void)
{
    out_neutral();
    out_close();
}

static bool mc_awake(void)
{
    return out_linked();
}

#if MC_RND
// (the menu icon draws on the real frame: no round view)
#undef gfx_fill_rect
#undef gfx_frame_rect
#undef gfx_fill_circle
#undef gfx_text_width
#undef gfx_text
#undef gfx_text_center_in
#undef gfx_text_center
#undef gfx_clip_bottom
#endif
#include "tficon/tficon.h"
// the menu icon: four faders of a controller move up and down; the light of each one follows its level
static void mc_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0xf06bff), tfi_hex(0x6a1b9a));
    float u = s / 100.0f, ph = tfi_ph(2);
    tfi_rbox(x + (int)(16 * u), y + (int)(18 * u), (int)(68 * u), (int)(66 * u), (int)(8 * u), tfi_hex(0x2a1638),
             tfi_hex(0x150a1e), 256);
    static const uint32_t FC[4] = {0xff5470, 0xffc23d, 0x3de88a, 0x40b8ff};
    for (int k = 0; k < 4; k++) {
        float fx = x + (28 + k * 15) * u;
        float lv = 0.5f + 0.42f * fsin(ph * 6.2831853f * (k == 1 || k == 2 ? 2 : 1) + k * 1.6f);
        tfi_rbox((int)(fx - 1.6f * u), y + (int)(32 * u), (int)(3.2f * u) > 1 ? (int)(3.2f * u) : 1, (int)(44 * u),
                 (int)(1.6f * u), tfi_hex(0x07040a), tfi_hex(0x07040a), 256);
        float ky = y + (73 - lv * 38) * u;
        uint16_t c = tfi_hex(FC[k]);
        tfd_disc(fx, y + 25 * u, 3 * u, tfd_mix(tfi_dark(c, 170), c, (int)(256 * lv)), 256);
        tfi_drop((int)(fx - 5.5f * u), (int)(ky - 4 * u), (int)(11 * u), (int)(8 * u), (int)(2 * u),
                 (int)(1.5f * u) > 1 ? (int)(1.5f * u) : 1, 90);
        tfi_rbox((int)(fx - 5.5f * u), (int)(ky - 4 * u), (int)(11 * u), (int)(8 * u), (int)(2 * u), tfi_light(c, 90),
                 tfi_dark(c, 40), 256);
    }
    tfi_end();
}

const app_t app_midictl = {
#if MC_RND
        .round = APP_ROUND_HI,  // (a round panel: the round view, mc_rv.inc)
#endif
        .name = "MIDI",
        .landscape = true,
        .enter = mc_enter,
        .leave = mc_leave,
        .input = mc_input,
        .update = mc_update,
        .render = mc_render,
        .draw_icon = mc_icon,
        .awake = mc_awake,
        .needs = 0,   // the sliders need only touch; the tilt shows with an IMU
        .run = true,  // the settings panel: the MIDI link and its clock go on under it
};
