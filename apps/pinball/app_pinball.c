// PINBALL: a real pinball table on the portrait screen (3.49: 172 x 640, a long table;
// CoreS3: 240 x 320, a short wide table).
//
// - Touch the left half of the screen for the left flipper, the right half for the right
//   flipper; two fingers work both flippers. The thumbs rest on the apron (the panel below
//   the flippers), so they do not hide the flippers or the ball.
// - Plunger: hold a finger on the screen to pull it, release it to shoot. The shot can
//   light the blinking top lane (SKILL SHOT). The ball shoots by itself after some seconds.
// - The table: 3 top lanes (all lit = a higher bonus multiplier; a flipper press moves the
//   lit lanes), 3 pop bumpers, a left orbit with a spinner, a ramp, a bank of drop
//   targets, a saucer, slingshots, inlanes and outlanes.
// - Drop all targets: LOCK is lit. A ball in the saucer is then locked. Two locks start a
//   3-ball MULTIBALL; in multiball the ramp gives a JACKPOT.
// - Missions (ramps, spins, lanes, targets, bumpers, orbits, combos): each one done gives
//   big points; each third mission starts a multiball; the fourth gives an extra ball.
// - A real knock or shake of the board nudges the balls. Too many nudges: WARNING, then
//   TILT (the flippers are dead until the ball drains, and the ball has no bonus).
// - 3 balls, a ball save after each launch, a bonus count at the end of each ball.
//
// The static table (walls, art, the apron) is drawn once into a buffer in PSRAM; each
// frame copies it and draws only the parts that change. The physics runs in steps of
// 1 ms, so a ball never goes through a flipper or a thin wall at any frame rate.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/tunes.h"

// ---------------------------------------------------------------- the two table sizes

#if BOARD_BAR_SCREEN
#define SCR_W       172
#define SCR_H       640
#define DMD_H       40   // the score display at the top
#define APRON_Y     560  // the apron (thumb pads) from here to the bottom
#define BALL_R      5.0f
#define GRAV        900.0f  // px/s2 down the table
#define MAX_V       1900.0f
#define FLIP_LEN    26.0f
#define FLIP_R0     4.5f   // the radius at the pivot
#define FLIP_R1     2.5f   // the radius at the tip
#define FLIP_W_UP   30.0f  // rad/s
#define FLIP_W_DOWN 20.0f
#define FLIP_REST   0.52f  // 30 degrees below the horizontal
#define FLIP_UP     0.49f  // 28 degrees above the horizontal
#define BUMP_R      12.0f
#define BUMP_KICK   650.0f
#define SLING_KICK  420.0f
// the plunger: the pull (0..1) to the launch speed, in parts (measured on the table):
// a short pull feeds the bumpers, a middle pull the top lanes, a full pull the orbit
#define PLUNGE_N 4
static const float PLUNGE_P[PLUNGE_N] = {0.0f, 0.25f, 0.80f, 1.0f};
static const float PLUNGE_V[PLUNGE_N] = {895.0f, 942.0f, 978.0f, 1150.0f};
#define RAMP_V   430.0f  // px/s along the ramp
#define NUDGE_V  140.0f
#define DRAIN_Y  612.0f
#define SAUCER_R 8.0f
#define N_DROPS  4
#define TXT_BIG  2  // text scale of the big messages
#elif BOARD_UI_TABLET
// The Tab5 (360 x 640): the CoreS3 table 1.5 times bigger (all lengths and speeds, so the play is the same), TAB_OY px
// lower: a tall score display above it and a tall apron (big thumb pads) below it. table_scale() moves the table.
#define SCR_W       360
#define SCR_H       640
#define DMD_H       110
#define APRON_Y     488
#define TAB_OY      80.0f
#define BALL_R      9.0f
#define GRAV        780.0f
#define MAX_V       1950.0f
#define FLIP_LEN    45.0f
#define FLIP_R0     7.5f
#define FLIP_R1     4.5f
#define FLIP_W_UP   24.0f
#define FLIP_W_DOWN 16.0f
#define FLIP_REST   0.49f
#define FLIP_UP     0.47f
#define BUMP_R      19.5f
#define BUMP_KICK   690.0f
#define SLING_KICK  450.0f
#define PLUNGE_N    4
static const float PLUNGE_P[PLUNGE_N] = {0.0f, 0.15f, 0.90f, 1.0f};
static const float PLUNGE_V[PLUNGE_N] = {840.0f, 952.5f, 1192.5f, 1237.5f};
#define RAMP_V      450.0f
#define NUDGE_V     150.0f
#define DRAIN_Y     530.0f
#define SAUCER_R    13.5f
#define N_DROPS     3
#define TXT_BIG     3
#elif BOARD_ROUND
// THE ROUND LAYOUT (APP_ROUND_HI: the 466 frame of the AMOLED-1.75C, the 412 frame of the Watcher). The CoreS3 table,
// KT times bigger (1.3 at 466; all lengths and speeds, so the play is the same), in the middle of the circle: its
// round top corners, the plunger and the flippers are in the circle. The score display is in the top cap, the apron
// (the thumb pads) in the bottom cap. table_scale() moves the table (as on the Tab5).
#if BOARD_ROUND_HI >= 466
#define RF 466  // (the frame size is a number of the build: the table buffer and the speeds use it)
#else
#define RF 412
#endif
#define KT          (1.3f * RF / 466.0f)
#define ROX         (RF / 2.0f - 120.5f * KT)           // the CoreS3 x 0 of the table
#define ROY         (RF * 66.0f / 466.0f - 25.0f * KT)  // the CoreS3 y 0 of the table
#define SCR_W       RF
#define SCR_H       RF
#define DMD_H       (RF * 62 / 466)
#define APRON_Y     ((int)(ROY + 272.0f * KT))
#define TAB_OY      ROY
#define BALL_R      (6.0f * KT)
#define GRAV        (520.0f * KT)
#define MAX_V       (1300.0f * KT)
#define FLIP_LEN    (30.0f * KT)
#define FLIP_R0     (5.0f * KT)
#define FLIP_R1     (3.0f * KT)
#define FLIP_W_UP   24.0f
#define FLIP_W_DOWN 16.0f
#define FLIP_REST   0.49f
#define FLIP_UP     0.47f
#define BUMP_R      (13.0f * KT)
#define BUMP_KICK   (460.0f * KT)
#define SLING_KICK  (300.0f * KT)
#define PLUNGE_N    4
static const float PLUNGE_P[PLUNGE_N] = {0.0f, 0.15f, 0.90f, 1.0f};
static const float PLUNGE_V[PLUNGE_N] = {560.0f * KT, 635.0f * KT, 795.0f * KT, 825.0f * KT};
#define RAMP_V      (300.0f * KT)
#define NUDGE_V     (100.0f * KT)
#define DRAIN_Y     (ROY + 300.0f * KT)
#define SAUCER_R    (9.0f * KT)
#define N_DROPS     3
#define TXT_BIG     2
#include "tfn/mfont.h"
#include "tfn/uikit.h"
// the size of the pixel font -> a smooth face of about 1.5 times that size (the round frame); the box of the pixel
// text (y = its top, 7 * sc px high) keeps its place
static int rnd_text_width(const char *s, int sc)
{
    return sc >= 4 ? mf_width(&MF_N34, s, 0) : ui_text_w(sc <= 1 ? &KF_NAME : sc == 2 ? &KF_BIG : &KF_TITLE, s);
}
static void rnd_text_c(int cx, int y, const char *s, int sc, uint16_t c)
{
    if (sc >= 4) {
        mf_text_c(&MF_N34, cx, y + (7 * sc + 35) / 2, s, c, 255, 0);
        return;
    }
    const kfont_t *f = sc <= 1 ? &KF_NAME : sc == 2 ? &KF_BIG : &KF_TITLE;
    ui_text_c(f, cx, y + (7 * sc + f->asc) / 2, s, c);
}
#define gfx_text_width(s, sc)                 rnd_text_width(s, sc)
#define gfx_text(x, y, s, sc, c)              rnd_text_c((x) + rnd_text_width(s, sc) / 2, y, s, sc, c)
#define gfx_text_center_in(x, w, y, s, sc, c) rnd_text_c((x) + (w) / 2, y, s, sc, c)
#define gfx_text_center(y, s, sc, c)          rnd_text_c(gfx_w / 2, y, s, sc, c)
#else
#define SCR_W       240
#define SCR_H       320
#define DMD_H       22
#define APRON_Y     272
#define BALL_R      6.0f
#define GRAV        520.0f
#define MAX_V       1300.0f
#define FLIP_LEN    30.0f
#define FLIP_R0     5.0f
#define FLIP_R1     3.0f
#define FLIP_W_UP   24.0f
#define FLIP_W_DOWN 16.0f
#define FLIP_REST   0.49f
#define FLIP_UP     0.47f
#define BUMP_R      13.0f
#define BUMP_KICK   460.0f
#define SLING_KICK  300.0f
// a short pull feeds the bumpers; then the right, middle and left lanes; a full pull the orbit
#define PLUNGE_N    4
static const float PLUNGE_P[PLUNGE_N] = {0.0f, 0.15f, 0.90f, 1.0f};
static const float PLUNGE_V[PLUNGE_N] = {560.0f, 635.0f, 795.0f, 825.0f};
#define RAMP_V      300.0f
#define NUDGE_V     100.0f
#define DRAIN_Y     300.0f
#define SAUCER_R    9.0f
#define N_DROPS     3
#define TXT_BIG     2
#endif

// K(v): a CoreS3 length or speed on the Tab5 (1.5 times); TS(s): a CoreS3 text scale on the Tab5 (1 becomes 2).
// On the other boards they do not change the value.
#if BOARD_UI_TABLET
#define K(v)  ((v) * 3 / 2)
#define TS(s) ((s) == 1 ? 2 : (s) * 3 / 2)
#elif BOARD_ROUND
#define K(v)  ((v) * KT)
#define TS(s) (s)  // (the gfx_text names go to the smooth fonts)
#else
#define K(v)   (v)
#define TS(s)  (s)
#define TAB_OY 0.0f
#endif
#define SUB_DT    0.001f  // physics step (s)
#define MAX_SUB   40
#define MAX_BALLS 3
#define MAX_SEGS  110
#define MAX_CIRCS 24
#define MAX_PATH  16
#define N_PART    64
#define N_POP     8
#define N_TRAIL   5
#define E_WALL    0.45f  // restitution
#define E_FLIP    0.22f
#define E_POST    0.60f
#define FRICTION  0.10f  // Coulomb friction at an impact
#define SAVE_S    7.0f   // ball save after each launch
#define AUTO_S    4.0f   // the ball in the shooter lane shoots by itself
#define BALLS     3
#define TAU       6.2831853f

// ---------------------------------------------------------------- table parts

enum {
    SG_WALL,
    SG_SLING,
    SG_DROP,
    SG_GATE,
    SG_LIP
};  // segment kinds
enum {
    CK_POST,
    CK_BUMPER
};  // circle kinds
enum {
    B_FREE,
    B_SHOOTER,
    B_RAMP,
    B_SAUCER
};  // ball modes

typedef struct {
    float ax, ay, bx, by;  // end points
    float r;               // half of the thickness
    float nx, ny;          // the side normal (sling: out of the triangle; gate: the solid side)
    float x0, y0, x1, y1;  // the bounding box with the ball radius
    uint8_t kind, id;
} seg_t;

typedef struct {
    float x, y, r;
    uint8_t kind, id;
} circ_t;

typedef struct {
    float px, py;    // the pivot
    float ang, w;    // the angle (rad, y down) and the angular speed
    float rest, up;  // the two end angles
    float dir;       // -1: the left flipper (up = a smaller angle); +1: the right one
    bool held;
} flipper_t;

typedef struct {
    bool on;
    uint8_t mode;
    float x, y, vx, vy;
    float px, py;    // the position before the step (sensor lines)
    float t;         // ramp: the distance on the path; saucer: the hold time
    float still;     // seconds at a low speed (ball search)
    float no_catch;  // the saucer does not catch the ball for this time (after a kick)
    float tx[N_TRAIL], ty[N_TRAIL];
    bool fresh;  // shot from the plunger, no lane yet (skill shot)
} ball_t;

typedef struct {
    float x, y, vx, vy, life;
    uint16_t c;
} part_t;
typedef struct {
    float x, y, life;
    char txt[12];
    uint16_t c;
} pop_t;

// the geometry of the table (built at the first start)
static seg_t s_seg[MAX_SEGS];
static int s_nseg;
static circ_t s_circ[MAX_CIRCS];
static int s_ncirc;
static float s_path_x[MAX_PATH], s_path_y[MAX_PATH], s_path_len[MAX_PATH];
static int s_npath;
static float s_ramp_vx, s_ramp_vy;  // the speed when the ball leaves the ramp
static flipper_t s_flip[2];

// places of the table that the game logic and the drawing use (set in table_build)
static float s_lane_x[4], s_lane_y0, s_lane_y1;  // 4 lane guides make 3 lanes
static float s_lane_sense_y;
static float s_bump_x[3], s_bump_y[3];
static float s_drop_x[N_DROPS], s_drop_y, s_drop_w;
static float s_saucer_x, s_saucer_y;
static float s_orbit_x0, s_orbit_x1, s_orbit_y, s_spin_y;  // the left orbit lane
static float s_ramp_x0, s_ramp_x1, s_ramp_sense_y, s_ramp_lip_y;
static float s_shoot_x0, s_shoot_x1, s_shoot_top, s_plunger_y;
static float s_out_l0, s_out_l1, s_out_r0, s_out_r1, s_out_y;
static float s_sling[2][6];         // the 3 corners of each slingshot
static float s_play_x0, s_play_x1;  // the playfield (without the shooter lane)

// ---------------------------------------------------------------- the game state

typedef enum {
    G_TITLE,
    G_PLAY,
    G_BONUS,
    G_OVER
} gstate_t;
typedef enum {
    M_RAMP,
    M_SPIN,
    M_LANES,
    M_BANK,
    M_BUMP,
    M_ORBIT,
    M_COMBO,
    N_MISSIONS
} mission_t;
static const uint8_t MISSION_GOAL[N_MISSIONS] = {2, 15, 1, 1, 10, 1, 1};
// the short names: the size-2 line of the 3.49 display, the gap between the thumb pads of the CoreS3
static const char *const MISSION_SHORT[N_MISSIONS] = {"RAMPS",   "SPINS",  "LANES", "BANKS",
                                                      "BUMPERS", "ORBITS", "COMBOS"};

static gstate_t s_state;
static ball_t s_ball[MAX_BALLS];
static part_t s_part[N_PART];
static pop_t s_pop[N_POP];
static int64_t s_prev_ms, s_state_at;
static float s_time;  // game time (s)

static uint32_t s_score, s_best;
static bool s_new_best;
static int s_ball_no, s_extra;  // the ball number (1..3); extra balls to play
static bool s_extra_given;
static float s_save_t;            // ball save time left
static bool s_save_armed;         // the next launch starts the ball save (once a ball)
static float s_danger, s_warn_t;  // tilt
static bool s_tilted;
static bool s_lane_lit[3];
static int s_skill_lane;
static float s_skill_t;
static bool s_drop_up[N_DROPS];
static float s_drop_reset_t;
static bool s_lock_lit;
static int s_locks;
static int s_mult;  // the bonus multiplier 1..5
static int s_mission, s_mission_n, s_mission_goal, s_missions_done;
static int s_n_ramps, s_n_lanesets, s_n_banks, s_n_bumps, s_n_spins, s_n_orbits;
static float s_combo_t;
static int s_combo;
static bool s_multiball;
static int s_jackpots;
static int s_queue;  // balls that wait for the auto plunger
static float s_queue_t;
static float s_plunger;  // the pull 0..1
static bool s_pulling;
static float s_auto_t;      // time of the ball on the plunger
static float s_shoot_wait;  // multiball: time of a ball in the shooter lane
static float s_spin_ang, s_spin_v;
static float s_bump_flash[3], s_sling_flash[2], s_ramp_flash, s_saucer_flash;
static float s_bump_cool[3], s_sling_cool[2];
static bool s_prev_l, s_prev_r;
static bool s_touch;    // a finger on the screen (this frame)
static bool s_attract;  // the title runs a demo ball

// the big message (on the table) and the small message (on the display)
static char s_msg[2][28];
static float s_msg_t;
static uint16_t s_msg_c;
static int s_bonus_step;
static uint32_t s_bonus_total, s_bonus_shown;
static float s_bonus_t;

static uint16_t *s_bg;  // the static table layer (PSRAM)
static bool s_bg_ok;

// ---------------------------------------------------------------- helpers

static float frand(float a, float b)
{
    return a + (b - a) * (float)(esp_random() % 10000) / 10000.0f;
}

static float clampf(float v, float a, float b)
{
    return v < a ? a : (v > b ? b : v);
}

static void voice(uint32_t ms, float f0, float f1, uint8_t wave, uint8_t vol, uint8_t lp)
{
    audio_voice_t v = {
            .dur = (uint32_t)(AUDIO_SAMPLE_RATE * ms / 1000),
            .f0 = f0,
            .f1 = f1,
            .wave = wave,
            .vol = vol,
            .attack_ms = 1,
            .shape = AUDIO_PERC,
            .lp = lp,
    };
    audio_play(&v);
}

static void snd_flipper(void)
{
    voice(40, 140, 60, W_TRI, 70, 0);
    voice(25, 900, 300, W_NOISE, 14, 120);
}

static void snd_bumper(int i)
{
    static const float F[3] = {1250, 1050, 1450};
    voice(60, F[i % 3], F[i % 3] * 0.45f, W_SQUARE, 20, 0);
    voice(30, 3000, 800, W_NOISE, 16, 0);
}

static void snd_sling(void)
{
    voice(45, 600, 220, W_SQUARE, 18, 0);
    voice(40, 2500, 500, W_NOISE, 18, 0);
}

static void snd_bell(float f, uint8_t vol)
{
    voice(260, f, f, W_SINE, vol, 0);
    voice(180, f * 2.0f, f * 2.0f, W_SINE, (uint8_t)(vol / 3), 0);
}

static void snd_target(void)
{
    voice(50, 300, 120, W_TRI, 70, 0);
    voice(30, 1800, 600, W_NOISE, 18, 0);
}

static void snd_arp(float base, int n, int step_ms, uint8_t vol)
{
    static const float R[6] = {1.0f, 1.26f, 1.5f, 2.0f, 2.52f, 3.0f};
    for (int i = 0; i < n && i < 6; i++) {
        audio_voice_t v = {
                .delay = (uint32_t)(AUDIO_SAMPLE_RATE * (i * step_ms) / 1000),
                .dur = (uint32_t)(AUDIO_SAMPLE_RATE * 110 / 1000),
                .f0 = base * R[i],
                .f1 = base * R[i],
                .wave = W_PULSE,
                .vol = vol,
                .attack_ms = 1,
                .shape = AUDIO_PERC,
        };
        audio_play(&v);
    }
}

static void fmt_score(char *out, size_t n, uint32_t v)
{
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)v);
    int len = (int)strlen(tmp), k = 0;
    for (int i = 0; i < len && k < (int)n - 1; i++) {
        out[k++] = tmp[i];
        int left = len - 1 - i;
        if (left > 0 && left % 3 == 0 && k < (int)n - 1) {
            out[k++] = ',';
        }
    }
    out[k] = 0;
}

static void message(const char *a, const char *b, uint16_t c, float secs)
{
    snprintf(s_msg[0], sizeof(s_msg[0]), "%s", a ? a : "");
    snprintf(s_msg[1], sizeof(s_msg[1]), "%s", b ? b : "");
    s_msg_c = c;
    s_msg_t = secs;
}

static void popup(float x, float y, const char *txt, uint16_t c)
{
    int best = 0;
    for (int i = 0; i < N_POP; i++) {
        if (s_pop[i].life <= 0) {
            best = i;
            break;
        }
        if (s_pop[i].life < s_pop[best].life) {
            best = i;
        }
    }
    pop_t *p = &s_pop[best];
    p->x = x;
    p->y = y;
    p->life = 0.9f;
    p->c = c;
    snprintf(p->txt, sizeof(p->txt), "%s", txt);
}

static void sparks(float x, float y, int n, uint16_t c, float v)
{
    for (int k = 0; k < n; k++) {
        for (int i = 0; i < N_PART; i++) {
            if (s_part[i].life > 0) {
                continue;
            }
            float a = frand(0, TAU), s = frand(0.3f, 1.0f) * v;
            s_part[i] = (part_t){x, y, fcos(a) * s, fsin(a) * s, frand(0.25f, 0.5f), c};
            break;
        }
    }
}

static void add_score(uint32_t pts, float x, float y)
{
    if (s_tilted || s_state != G_PLAY || s_attract) {
        return;
    }
    s_score += pts;
    if (x >= 0) {
        char t[12];
        snprintf(t, sizeof(t), "%lu", (unsigned long)pts);
        popup(x, y, t, rgb(255, 240, 160));
    }
}

// ---------------------------------------------------------------- table building

static void add_seg(float ax, float ay, float bx, float by, float r, uint8_t kind, uint8_t id)
{
    if (s_nseg >= MAX_SEGS) {
        return;
    }
    seg_t *s = &s_seg[s_nseg++];
    s->ax = ax;
    s->ay = ay;
    s->bx = bx;
    s->by = by;
    s->r = r;
    s->kind = kind;
    s->id = id;
    float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy);
    s->nx = l > 0 ? -dy / l : 0;  // the left normal of a -> b (y down: points "up" for a -> b to the right)
    s->ny = l > 0 ? dx / l : -1;
    float m = BALL_R + r + 1;
    s->x0 = fminf(ax, bx) - m;
    s->x1 = fmaxf(ax, bx) + m;
    s->y0 = fminf(ay, by) - m;
    s->y1 = fmaxf(ay, by) + m;
}

static void add_circ(float x, float y, float r, uint8_t kind, uint8_t id)
{
    if (s_ncirc >= MAX_CIRCS) {
        return;
    }
    s_circ[s_ncirc++] = (circ_t){x, y, r, kind, id};
}

// an arc of wall segments: centre, radius, from angle a0 to a1 (rad, y down), n parts
static void add_arc(float cx, float cy, float rad, float a0, float a1, int n)
{
    float px = cx + rad * cosf(a0), py = cy + rad * sinf(a0);
    for (int i = 1; i <= n; i++) {
        float a = a0 + (a1 - a0) * i / n;
        float x = cx + rad * cosf(a), y = cy + rad * sinf(a);
        add_seg(px, py, x, y, 1.5f, SG_WALL, 0);
        px = x;
        py = y;
    }
}

// a slingshot: back corner top (bx, ty), back corner bottom (bx, by), front corner (fx, fy)
static void add_sling(int side, float bx, float ty, float by, float fx, float fy)
{
    float *c = s_sling[side];
    c[0] = bx;
    c[1] = ty;
    c[2] = bx;
    c[3] = by;
    c[4] = fx;
    c[5] = fy;
    add_seg(bx, ty, bx, by, 1.5f, SG_WALL, 0);
    add_seg(bx, by, fx, fy, 1.5f, SG_WALL, 0);
    add_seg(fx, fy, bx, ty, 2.0f, SG_SLING, (uint8_t)side);
    // the normal of the face: away from the back side
    seg_t *f = &s_seg[s_nseg - 1];
    float mx = (fx + bx) / 2 - bx;
    if (f->nx * mx < 0) {
        f->nx = -f->nx;
        f->ny = -f->ny;
    }
    add_circ(bx, ty, 2.5f, CK_POST, 0);
    add_circ(bx, by, 2.5f, CK_POST, 0);
    add_circ(fx, fy, 2.5f, CK_POST, 0);
}

static void path_point(float x, float y)
{
    if (s_npath >= MAX_PATH) {
        return;
    }
    s_path_x[s_npath] = x;
    s_path_y[s_npath] = y;
    s_path_len[s_npath] = 0;
    if (s_npath > 0) {
        float dx = x - s_path_x[s_npath - 1], dy = y - s_path_y[s_npath - 1];
        s_path_len[s_npath] = s_path_len[s_npath - 1] + sqrtf(dx * dx + dy * dy);
    }
    s_npath++;
}

static void flipper_setup(flipper_t *f, float px, float py, float dir)
{
    f->px = px;
    f->py = py;
    f->dir = dir;
    if (dir < 0) {
        f->rest = FLIP_REST;
        f->up = -FLIP_UP;
    } else {
        f->rest = 3.14159265f - FLIP_REST;
        f->up = 3.14159265f + FLIP_UP;
    }
    f->ang = f->rest;
    f->w = 0;
    f->held = false;
}

#if BOARD_UI_TABLET || BOARD_ROUND
// The Tab5: move the CoreS3 table to the big frame (x 1.5 times; y 1.5 times + TAB_OY). The directions do not change.
// The round frame: KT times, in the middle of the circle (ROX, ROY).
#if BOARD_ROUND
#define TSC    KT
#define TX(v)  ((v) * KT + ROX)
#define TYY(v) ((v) * KT + ROY)
#define TXW(v) ((v) * KT)  // a width
#else
#define TSC    1.5f
#define TX(v)  ((v) * 1.5f)
#define TYY(v) ((v) * 1.5f + TAB_OY)
#define TXW(v) TX(v)
#endif
static void table_scale(void)
{
    for (int i = 0; i < s_nseg; i++) {
        seg_t *s = &s_seg[i];
        s->ax = TX(s->ax);
        s->ay = TYY(s->ay);
        s->bx = TX(s->bx);
        s->by = TYY(s->by);
        s->r *= TSC;
        float m = BALL_R + s->r + 1;
        s->x0 = fminf(s->ax, s->bx) - m;
        s->x1 = fmaxf(s->ax, s->bx) + m;
        s->y0 = fminf(s->ay, s->by) - m;
        s->y1 = fmaxf(s->ay, s->by) + m;
    }
    for (int i = 0; i < s_ncirc; i++) {
        s_circ[i].x = TX(s_circ[i].x);
        s_circ[i].y = TYY(s_circ[i].y);
        s_circ[i].r *= TSC;
    }
    for (int i = 0; i < s_npath; i++) {
        s_path_x[i] = TX(s_path_x[i]);
        s_path_y[i] = TYY(s_path_y[i]);
        s_path_len[i] *= TSC;
    }
    s_ramp_vx *= TSC;
    s_ramp_vy *= TSC;
    for (int k = 0; k < 2; k++) {
        s_flip[k].px = TX(s_flip[k].px);
        s_flip[k].py = TYY(s_flip[k].py);
        for (int j = 0; j < 6; j += 2) {
            s_sling[k][j] = TX(s_sling[k][j]);
            s_sling[k][j + 1] = TYY(s_sling[k][j + 1]);
        }
    }
    for (int i = 0; i < 4; i++) {
        s_lane_x[i] = TX(s_lane_x[i]);
    }
    s_lane_y0 = TYY(s_lane_y0);
    s_lane_y1 = TYY(s_lane_y1);
    s_lane_sense_y = TYY(s_lane_sense_y);
    for (int i = 0; i < 3; i++) {
        s_bump_x[i] = TX(s_bump_x[i]);
        s_bump_y[i] = TYY(s_bump_y[i]);
    }
    for (int i = 0; i < N_DROPS; i++) {
        s_drop_x[i] = TX(s_drop_x[i]);
    }
    s_drop_y = TYY(s_drop_y);
    s_drop_w = TXW(s_drop_w);
    s_saucer_x = TX(s_saucer_x);
    s_saucer_y = TYY(s_saucer_y);
    s_orbit_x0 = TX(s_orbit_x0);
    s_orbit_x1 = TX(s_orbit_x1);
    s_orbit_y = TYY(s_orbit_y);
    s_spin_y = TYY(s_spin_y);
    s_ramp_x0 = TX(s_ramp_x0);
    s_ramp_x1 = TX(s_ramp_x1);
    s_ramp_sense_y = TYY(s_ramp_sense_y);
    s_ramp_lip_y = TYY(s_ramp_lip_y);
    s_shoot_x0 = TX(s_shoot_x0);
    s_shoot_x1 = TX(s_shoot_x1);
    s_shoot_top = TYY(s_shoot_top);
    s_plunger_y = TYY(s_plunger_y);
    s_out_l0 = TX(s_out_l0);
    s_out_l1 = TX(s_out_l1);
    s_out_r0 = TX(s_out_r0);
    s_out_r1 = TX(s_out_r1);
    s_out_y = TYY(s_out_y);
    s_play_x0 = TX(s_play_x0);
    s_play_x1 = TX(s_play_x1);
}
#endif

static void table_build(void)
{
    s_nseg = s_ncirc = s_npath = 0;
#if BOARD_BAR_SCREEN
    // walls: the left wall, the top arch, the right wall
    s_play_x0 = 3;
    s_play_x1 = 153;
    add_seg(3, 130, 3, 600, 1.5f, SG_WALL, 0);
    add_arc(86, 130, 83, 3.14159265f, TAU, 28);
    add_seg(169, 130, 169, 600, 1.5f, SG_WALL, 0);
    // the shooter lane: the inner wall, a one-way gate at the top, the plunger stop
    s_shoot_x0 = 153;
    s_shoot_x1 = 169;
    s_shoot_top = 172;
    s_plunger_y = 552;
    add_seg(153, 172, 153, 600, 1.5f, SG_WALL, 0);
    add_circ(153, 172, 2.0f, CK_POST, 0);
    add_seg(169, 156, 153, 172, 1.5f, SG_GATE, 0);
    add_seg(153, 552, 169, 552, 1.5f, SG_WALL, 0);
    // the top lanes
    s_lane_x[0] = 44;
    s_lane_x[1] = 66;
    s_lane_x[2] = 88;
    s_lane_x[3] = 110;
    s_lane_y0 = 80;
    s_lane_y1 = 102;
    s_lane_sense_y = 94;
    for (int i = 0; i < 4; i++) {
        add_seg(s_lane_x[i], s_lane_y0, s_lane_x[i], s_lane_y1, 1.5f, SG_WALL, 0);
        add_circ(s_lane_x[i], s_lane_y0, 2.0f, CK_POST, 0);
    }
    // the pop bumpers
    s_bump_x[0] = 57;
    s_bump_y[0] = 152;
    s_bump_x[1] = 99;
    s_bump_y[1] = 152;
    s_bump_x[2] = 78;
    s_bump_y[2] = 194;
    // the left orbit lane with the spinner
    s_orbit_x0 = 3;
    s_orbit_x1 = 22;
    s_orbit_y = 215;
    s_spin_y = 275;
    add_seg(22, 208, 22, 345, 1.5f, SG_WALL, 0);
    add_circ(22, 208, 2.5f, CK_POST, 0);
    add_circ(22, 345, 2.5f, CK_POST, 0);
    add_seg(3, 392, 13, 412, 1.5f, SG_WALL, 0);  // turns an orbit ball to the inlane
    // the ramp entrance (two guides and the lip)
    s_ramp_x0 = 130;
    s_ramp_x1 = 153;
    s_ramp_lip_y = 298;
    s_ramp_sense_y = 322;
    add_seg(130, 298, 130, 345, 1.5f, SG_WALL, 0);
    add_seg(130, 298, 153, 298, 1.5f, SG_LIP, 0);
    add_circ(130, 345, 2.5f, CK_POST, 0);
    add_circ(130, 298, 2.0f, CK_POST, 0);
    // the ramp path: up the right side, over the top, down the left side to the left inlane
    path_point(141, 322);
    path_point(141, 250);
    path_point(140, 170);
    path_point(136, 112);
    path_point(112, 74);
    path_point(70, 64);
    path_point(34, 84);
    path_point(14, 130);
    path_point(11, 210);
    path_point(11, 400);
    path_point(24, 462);
    s_ramp_vx = 30;
    s_ramp_vy = 240;
    // the saucer and the drop targets
    s_saucer_x = 78;
    s_saucer_y = 292;
    s_drop_y = 380;
    s_drop_w = 16;
    s_drop_x[0] = 44;
    s_drop_x[1] = 63;
    s_drop_x[2] = 82;
    s_drop_x[3] = 101;
    // the slingshots, the inlanes and the outlanes
    add_sling(0, 32, 425, 470, 47, 490);
    add_sling(1, 121, 425, 470, 106, 490);
    add_circ(17, 455, 2.5f, CK_POST, 0);
    add_seg(17, 455, 17, 506, 1.5f, SG_WALL, 0);
    add_seg(17, 506, 43, 528, 1.5f, SG_WALL, 0);
    // the right side has no outlane (a ball on the long shooter wall would fall into it):
    // one wide inlane leads to the right flipper
    add_seg(153, 494, 110, 528, 1.5f, SG_WALL, 0);
    s_out_l0 = 3;
    s_out_l1 = 17;
    s_out_r0 = 0;
    s_out_r1 = 0;
    s_out_y = 545;
    flipper_setup(&s_flip[0], 46, 532, -1);
    flipper_setup(&s_flip[1], 107, 532, +1);
#else
    s_play_x0 = 3;
    s_play_x1 = 221;
    // walls: the left wall, two round top corners and the top wall, the right wall
    add_seg(3, 75, 3, 300, 1.5f, SG_WALL, 0);
    add_arc(53, 75, 50, 3.14159265f, 4.712389f, 10);
    add_seg(53, 25, 188, 25, 1.5f, SG_WALL, 0);
    add_arc(188, 75, 50, 4.712389f, TAU, 10);
    add_seg(238, 75, 238, 300, 1.5f, SG_WALL, 0);
    // the shooter lane
    s_shoot_x0 = 221;
    s_shoot_x1 = 238;
    s_shoot_top = 112;
    s_plunger_y = 268;
    add_seg(221, 112, 221, 300, 1.5f, SG_WALL, 0);
    add_circ(221, 112, 2.0f, CK_POST, 0);
    add_seg(238, 96, 221, 113, 1.5f, SG_GATE, 0);
    add_seg(221, 268, 238, 268, 1.5f, SG_WALL, 0);
    // the top lanes
    s_lane_x[0] = 76;
    s_lane_x[1] = 101;
    s_lane_x[2] = 126;
    s_lane_x[3] = 151;
    s_lane_y0 = 43;
    s_lane_y1 = 61;
    s_lane_sense_y = 55;
    for (int i = 0; i < 4; i++) {
        add_seg(s_lane_x[i], s_lane_y0, s_lane_x[i], s_lane_y1, 1.5f, SG_WALL, 0);
        add_circ(s_lane_x[i], s_lane_y0, 2.0f, CK_POST, 0);
    }
    // the pop bumpers
    s_bump_x[0] = 84;
    s_bump_y[0] = 88;
    s_bump_x[1] = 128;
    s_bump_y[1] = 88;
    s_bump_x[2] = 106;
    s_bump_y[2] = 122;
    // the left orbit lane with the spinner
    s_orbit_x0 = 3;
    s_orbit_x1 = 26;
    s_orbit_y = 100;
    s_spin_y = 134;
    add_seg(26, 94, 26, 150, 1.5f, SG_WALL, 0);
    add_circ(26, 94, 2.5f, CK_POST, 0);
    add_circ(26, 150, 2.5f, CK_POST, 0);
    add_seg(3, 156, 10, 166, 1.5f, SG_WALL, 0);  // turns an orbit ball to the inlane
    // the ramp entrance
    // the ramp mouth: a channel that leans toward the left flipper (the shot comes at about 65 degrees)
    s_ramp_x0 = 158;
    s_ramp_x1 = 181;
    s_ramp_lip_y = 112;
    s_ramp_sense_y = 132;
    add_seg(150, 152, 166, 112, 1.5f, SG_WALL, 0);
    add_seg(172, 152, 188, 112, 1.5f, SG_WALL, 0);
    add_seg(166, 112, 188, 112, 1.5f, SG_LIP, 0);
    add_circ(150, 152, 2.5f, CK_POST, 0);
    add_circ(172, 152, 2.5f, CK_POST, 0);
    path_point(170, 132);
    path_point(181, 100);
    path_point(205, 66);
    path_point(184, 36);
    path_point(140, 31);
    path_point(64, 31);
    path_point(26, 50);
    path_point(12, 92);
    path_point(12, 170);
    path_point(28, 192);
    s_ramp_vx = 25;
    s_ramp_vy = 160;
    // the saucer and the drop targets
    s_saucer_x = 60;
    s_saucer_y = 150;
    s_drop_y = 180;
    s_drop_w = 18;
    s_drop_x[0] = 92;
    s_drop_x[1] = 113;
    s_drop_x[2] = 134;
    // the slingshots, the inlanes and the outlanes
    add_sling(0, 38, 176, 202, 56, 216);
    add_sling(1, 186, 176, 202, 168, 216);
    add_circ(20, 186, 2.5f, CK_POST, 0);
    add_seg(20, 186, 20, 214, 1.5f, SG_WALL, 0);
    add_seg(20, 214, 73, 249, 1.5f, SG_WALL, 0);
    // the right side has no outlane: one wide inlane leads to the right flipper
    add_seg(221, 204, 151, 249, 1.5f, SG_WALL, 0);
    s_out_l0 = 3;
    s_out_l1 = 20;
    s_out_r0 = 0;
    s_out_r1 = 0;
    s_out_y = 252;
    flipper_setup(&s_flip[0], 76, 252, -1);
    flipper_setup(&s_flip[1], 148, 252, +1);
#if BOARD_UI_TABLET || BOARD_ROUND
    table_scale();
#endif
#endif
    for (int i = 0; i < 3; i++) {
        add_circ(s_bump_x[i], s_bump_y[i], BUMP_R, CK_BUMPER, (uint8_t)i);
    }
    for (int i = 0; i < N_DROPS; i++) {
        add_seg(s_drop_x[i] - s_drop_w / 2 + K(2), s_drop_y, s_drop_x[i] + s_drop_w / 2 - K(2), s_drop_y, K(2.5f),
                SG_DROP, (uint8_t)i);
    }
    // the gate is solid only for a ball that comes down from above it; the ramp lip only
    // for a ball that comes up from below it (a ball from above falls through the entrance)
    for (int i = 0; i < s_nseg; i++) {
        seg_t *s = &s_seg[i];
        if ((s->kind == SG_GATE && s->ny > 0) || (s->kind == SG_LIP && s->ny < 0)) {
            s->nx = -s->nx;
            s->ny = -s->ny;
        }
    }
}

// ---------------------------------------------------------------- physics

static void flipper_tip(const flipper_t *f, float *x, float *y)
{
    // (the table sine, not sinf/cosf: this runs 2 times for each ball in each 1 ms step, and sinf is
    // slow in an app file)
    *x = f->px + FLIP_LEN * fcos(f->ang);
    *y = f->py + FLIP_LEN * fsin(f->ang);
}

static void flipper_step(flipper_t *f, float dt)
{
    bool up = f->held && !s_tilted;
    float target = up ? f->up : f->rest;
    float speed = up ? FLIP_W_UP : FLIP_W_DOWN;
    float d = target - f->ang;
    float step = speed * dt;
    if (fabsf(d) <= step) {
        f->ang = target;
        f->w = 0;
    } else {
        f->w = d > 0 ? speed : -speed;
        f->ang += f->w * dt;
    }
}

static void ev_bumper(int i, float x, float y);
static void ev_sling(int i, float x, float y);
static void ev_drop(int i);

// The closest point Q on the segment a-b to p; t = 0..1 along it.
static void closest(float ax, float ay, float bx, float by, float px, float py, float *qx, float *qy, float *t)
{
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float u = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    u = clampf(u, 0, 1);
    *qx = ax + u * dx;
    *qy = ay + u * dy;
    *t = u;
}

// Push the ball out of a contact (normal n, the depth) and bounce it.
// vx, vy of the surface point (moving flipper); e = restitution. Returns the speed into it.
static float bounce(ball_t *b, float nx, float ny, float qx, float qy, float rr, float svx, float svy, float e)
{
    b->x = qx + nx * rr;
    b->y = qy + ny * rr;
    float rvx = b->vx - svx, rvy = b->vy - svy;
    float vn = rvx * nx + rvy * ny;
    if (vn >= 0) {
        return 0;
    }
    // Coulomb friction from the impact (a ball that rests on a slope still rolls freely)
    float tx = rvx - vn * nx, ty = rvy - vn * ny;
    float vt = sqrtf(tx * tx + ty * ty);
    if (vt > 1e-3f) {
        float k = 1.0f - fminf(1.0f, FRICTION * (1.0f + e) * -vn / vt);
        tx *= k;
        ty *= k;
    }
    rvx = tx - e * vn * nx;
    rvy = ty - e * vn * ny;
    b->vx = rvx + svx;
    b->vy = rvy + svy;
    return -vn;
}

static void collide(ball_t *b)
{
    // segments
    for (int i = 0; i < s_nseg; i++) {
        seg_t *s = &s_seg[i];
        if (b->x < s->x0 || b->x > s->x1 || b->y < s->y0 || b->y > s->y1) {
            continue;
        }
        if (s->kind == SG_DROP && !s_drop_up[s->id]) {
            continue;
        }
        float qx, qy, t;
        closest(s->ax, s->ay, s->bx, s->by, b->x, b->y, &qx, &qy, &t);
        float dx = b->x - qx, dy = b->y - qy, d2 = dx * dx + dy * dy, rr = BALL_R + s->r;
        if (d2 >= rr * rr) {
            continue;
        }
        float d = sqrtf(d2), nx, ny;
        if (d > 1e-4f) {
            nx = dx / d;
            ny = dy / d;
        } else {
            nx = s->nx;
            ny = s->ny;
        }
        if (s->kind == SG_GATE || s->kind == SG_LIP) {
            // one way: only a ball on the solid side that moves into the gate
            if (dx * s->nx + dy * s->ny < 0 || b->vx * s->nx + b->vy * s->ny > 0) {
                continue;
            }
        }
        float vin = bounce(b, nx, ny, qx, qy, rr, 0, 0, E_WALL);
        if ((t <= 0.0f || t >= 1.0f) && ny < -0.95f && fabsf(b->vx) < K(20)) {
            b->vx += nx >= 0 ? K(25) : -K(25);  // a ball on top of a wall end falls off
        }
        if (s->kind == SG_SLING && vin > K(90) && nx * s->nx + ny * s->ny > 0.5f && !s_tilted &&
            s_sling_cool[s->id] <= 0) {
            // the kick sets the speed away from the face (it does not add), with a small spread
            // in speed and angle as a real slingshot: no endless ping-pong
            float a = frand(-0.12f, 0.12f), kx = s->nx * cosf(a) - s->ny * sinf(a),
                  ky = s->nx * sinf(a) + s->ny * cosf(a);
            float kick = SLING_KICK * frand(0.88f, 1.12f);
            float vn = b->vx * kx + b->vy * ky;
            if (vn < kick) {
                b->vx += (kick - vn) * kx;
                b->vy += (kick - vn) * ky;
            }
            s_sling_cool[s->id] = 0.08f;
            ev_sling(s->id, qx, qy);
        } else if (s->kind == SG_DROP && vin > K(60)) {
            ev_drop(s->id);
        }
    }
    // posts and bumpers
    for (int i = 0; i < s_ncirc; i++) {
        circ_t *c = &s_circ[i];
        float dx = b->x - c->x, dy = b->y - c->y, rr = BALL_R + c->r, d2 = dx * dx + dy * dy;
        if (d2 >= rr * rr) {
            continue;
        }
        float d = sqrtf(d2), nx = d > 1e-4f ? dx / d : 0, ny = d > 1e-4f ? dy / d : -1;
        if (c->kind == CK_BUMPER) {
            bounce(b, nx, ny, c->x, c->y, rr, 0, 0, E_POST);
            if (!s_tilted && s_bump_cool[c->id] <= 0) {
                // the bumper kicks: the speed away from it is at least BUMP_KICK (+-10 %)
                float kick = BUMP_KICK * frand(0.9f, 1.1f);
                float vn = b->vx * nx + b->vy * ny;
                if (vn < kick) {
                    b->vx += (kick - vn) * nx;
                    b->vy += (kick - vn) * ny;
                }
                s_bump_cool[c->id] = 0.06f;
                ev_bumper(c->id, c->x + nx * c->r, c->y + ny * c->r);
            }
        } else {
            bounce(b, nx, ny, c->x, c->y, rr, 0, 0, E_POST);
            if (ny < -0.95f && fabsf(b->vx) < K(20)) {
                b->vx += nx >= 0 ? K(25) : -K(25);  // a ball on top of a post falls off
            }
        }
    }
    // flippers (tapered)
    for (int k = 0; k < 2; k++) {
        flipper_t *f = &s_flip[k];
        float tx, ty;
        flipper_tip(f, &tx, &ty);
        float qx, qy, t;
        closest(f->px, f->py, tx, ty, b->x, b->y, &qx, &qy, &t);
        float fr = FLIP_R0 + (FLIP_R1 - FLIP_R0) * t;
        float dx = b->x - qx, dy = b->y - qy, rr = BALL_R + fr, d2 = dx * dx + dy * dy;
        if (d2 >= rr * rr) {
            continue;
        }
        float d = sqrtf(d2), nx, ny;
        if (d > 1e-4f) {
            nx = dx / d;
            ny = dy / d;
        } else {
            nx = 0;
            ny = -1;
        }
        // the speed of the flipper at the contact point: w x r
        float svx = -f->w * (qy - f->py), svy = f->w * (qx - f->px);
        bounce(b, nx, ny, qx, qy, rr, svx, svy, E_FLIP);
    }
}

static void ball_pair(ball_t *a, ball_t *b)
{
    float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy, rr = 2 * BALL_R;
    if (d2 >= rr * rr || d2 < 1e-6f) {
        return;
    }
    float d = sqrtf(d2), nx = dx / d, ny = dy / d, push = (rr - d) / 2;
    a->x -= nx * push;
    a->y -= ny * push;
    b->x += nx * push;
    b->y += ny * push;
    float vn = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
    if (vn >= 0) {
        return;
    }
    float j = -vn * 0.95f;
    a->vx -= j * nx;
    a->vy -= j * ny;
    b->vx += j * nx;
    b->vy += j * ny;
}

// ---------------------------------------------------------------- game events

static void mission_new(void);
static void start_multiball(void);

static void mission_add(mission_t m, int n)
{
    if (s_attract || s_state != G_PLAY || s_tilted || s_mission != (int)m) {
        return;
    }
    s_mission_n += n;
    if (s_mission_n < s_mission_goal) {
        return;
    }
    s_missions_done++;
    uint32_t pts = 10000u * (uint32_t)s_missions_done;
    add_score(pts, -1, 0);
    char b[22];
    snprintf(b, sizeof(b), "+%lu", (unsigned long)pts);
    message("MISSION DONE", b, rgb(120, 255, 140), 2.0f);
    snd_arp(523, 6, 70, 22);
    if (s_missions_done % 3 == 0) {
        start_multiball();
    } else if (s_missions_done == 4 && !s_extra_given) {
        s_extra_given = true;
        s_extra++;
        message("EXTRA BALL", "SHOOT AGAIN", rgb(255, 210, 60), 2.2f);
        snd_arp(392, 6, 90, 24);
    }
    mission_new();
}

static void mission_new(void)
{
    int m;
    do {
        m = (int)(esp_random() % N_MISSIONS);
    } while (m == s_mission && N_MISSIONS > 1);
    s_mission = m;
    s_mission_n = 0;
    s_mission_goal = MISSION_GOAL[m] * (1 + s_missions_done / 3);
    if (m == M_LANES || m == M_BANK) {
        s_mission_goal = 1 + s_missions_done / 4;
    }
}

static void combo_shot(float x, float y)
{
    if (s_combo_t > 0) {
        s_combo++;
        uint32_t pts = 1500u * (uint32_t)s_combo;
        add_score(pts, x, y - 10);
        char a[22];
        snprintf(a, sizeof(a), "COMBO X%d", s_combo + 1);
        message(a, NULL, rgb(255, 160, 40), 1.2f);
        mission_add(M_COMBO, 1);
    } else {
        s_combo = 0;
    }
    s_combo_t = 3.0f;
}

static void start_multiball(void)
{
    if (s_multiball || s_attract) {
        return;
    }
    s_multiball = true;
    s_jackpots = 0;
    int on = 0;
    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_ball[i].on) {
            on++;
        }
    }
    s_queue = MAX_BALLS - on;
    s_queue_t = 0.6f;
    s_save_t = 10.0f;
    s_locks = 0;
    message("MULTIBALL", "RAMP = JACKPOT", rgb(255, 80, 200), 2.5f);
    snd_arp(392, 6, 60, 24);
    snd_arp(784, 6, 60, 16);
}

static void ev_bumper(int i, float x, float y)
{
    s_bump_flash[i] = 0.18f;
    snd_bumper(i);
    sparks(x, y, 4, rgb(255, 230, 120), 160);
    add_score(100, -1, 0);
    s_n_bumps++;
    mission_add(M_BUMP, 1);
}

static void ev_sling(int i, float x, float y)
{
    s_sling_flash[i] = 0.12f;
    snd_sling();
    sparks(x, y, 3, rgb(255, 255, 255), 120);
    add_score(30, -1, 0);
}

static void ev_drop(int i)
{
    if (!s_drop_up[i]) {
        return;
    }
    s_drop_up[i] = false;
    snd_target();
    sparks(s_drop_x[i], s_drop_y, 5, rgb(255, 120, 60), 140);
    add_score(750, s_drop_x[i], s_drop_y - 8);
    for (int k = 0; k < N_DROPS; k++) {
        if (s_drop_up[k]) {
            return;
        }
    }
    // the bank is down
    s_n_banks++;
    add_score(5000, -1, 0);
    if (!s_multiball) {
        s_lock_lit = true;
        message("LOCK IS LIT", "HIT THE SAUCER", rgb(120, 220, 255), 1.8f);
    } else {
        message("TARGETS", "+5000", rgb(255, 140, 60), 1.2f);
    }
    snd_arp(659, 4, 60, 20);
    mission_add(M_BANK, 1);
    s_drop_reset_t = s_multiball ? 2.0f : 0;  // in multiball the bank comes back at once
}

static void ev_lane(int i)
{
    bool skill = false;
    for (int k = 0; k < MAX_BALLS; k++) {
        if (s_ball[k].on && s_ball[k].fresh) {
            s_ball[k].fresh = false;
            if (s_skill_t > 0 && i == s_skill_lane) {
                skill = true;
            }
        }
    }
    s_skill_t = 0;
    if (skill) {
        add_score(10000, -1, 0);
        message("SKILL SHOT", "+10000", rgb(255, 255, 120), 2.0f);
        snd_arp(880, 6, 50, 22);
    }
    if (s_lane_lit[i]) {
        add_score(100, -1, 0);
        voice(60, 1500, 1500, W_SINE, 30, 0);
        return;
    }
    s_lane_lit[i] = true;
    add_score(500, s_lane_x[i] + 11, s_lane_y1);
    snd_bell(1320, 30);
    if (s_lane_lit[0] && s_lane_lit[1] && s_lane_lit[2]) {
        s_lane_lit[0] = s_lane_lit[1] = s_lane_lit[2] = false;
        s_n_lanesets++;
        add_score(3000, -1, 0);
        if (s_mult < 5) {
            s_mult++;
        }
        char a[22];
        snprintf(a, sizeof(a), "BONUS X%d", s_mult);
        message(a, "LANES COMPLETE", rgb(255, 230, 80), 1.6f);
        snd_arp(784, 5, 60, 22);
        mission_add(M_LANES, 1);
    }
}

static void ev_spinner(float speed)
{
    int spins = 1 + (int)(speed / K(160));
    s_spin_v = fminf(60, speed / K(12));
    s_n_spins += spins;
    add_score(20u * (uint32_t)spins, -1, 0);
    mission_add(M_SPIN, spins);
    for (int i = 0; i < spins && i < 5; i++) {
        audio_voice_t v = {
                .delay = (uint32_t)(AUDIO_SAMPLE_RATE * (i * 45) / 1000),
                .dur = (uint32_t)(AUDIO_SAMPLE_RATE * 18 / 1000),
                .f0 = 2400,
                .f1 = 1800,
                .wave = W_PULSE,
                .vol = 12,
                .attack_ms = 1,
                .shape = AUDIO_PERC,
        };
        audio_play(&v);
    }
}

static void ev_orbit(void)
{
    s_n_orbits++;
    add_score(1500, s_orbit_x1 + 4, s_orbit_y);
    snd_bell(990, 26);
    mission_add(M_ORBIT, 1);
    combo_shot(s_orbit_x1 + 4, s_orbit_y);
}

static void ev_ramp_in(ball_t *b)
{
    b->mode = B_RAMP;
    b->t = 0;
    s_ramp_flash = 0.6f;
    s_n_ramps++;
    voice(500, 300, 1200, W_SAW, 12, 90);
    if (s_multiball) {
        uint32_t pts = 25000u + 5000u * (uint32_t)s_jackpots;
        s_jackpots++;
        add_score(pts, -1, 0);
        char b2[22];
        snprintf(b2, sizeof(b2), "+%lu", (unsigned long)pts);
        message("JACKPOT", b2, rgb(255, 60, 60), 2.0f);
        snd_arp(523, 6, 55, 26);
    } else {
        add_score(1000, s_path_x[0], s_path_y[0] - 12);
    }
    mission_add(M_RAMP, 1);
    combo_shot(s_path_x[0], s_path_y[0]);
}

static void ev_saucer(ball_t *b)
{
    b->mode = B_SAUCER;
    b->t = 0;
    b->x = s_saucer_x;
    b->y = s_saucer_y;
    b->vx = b->vy = 0;
    s_saucer_flash = 0.5f;
    voice(80, 180, 70, W_TRI, 80, 0);
    add_score(2500, s_saucer_x, s_saucer_y - 12);
    if (s_lock_lit && !s_multiball && !s_attract) {
        s_lock_lit = false;
        s_locks++;
        for (int i = 0; i < N_DROPS; i++) {
            s_drop_up[i] = true;
        }
        if (s_locks >= 2) {
            start_multiball();
        } else {
            message("BALL LOCKED", "ONE MORE LOCK", rgb(120, 220, 255), 1.8f);
            snd_arp(440, 4, 80, 22);
            // the locked ball stays: a new ball comes to the plunger
            b->on = false;
            s_queue++;
            s_queue_t = 1.0f;
        }
    }
}

// ---------------------------------------------------------------- balls

static int ball_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_ball[i].on) {
            n++;
        }
    }
    return n;
}

static ball_t *ball_new_in_shooter(void)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        ball_t *b = &s_ball[i];
        if (b->on) {
            continue;
        }
        memset(b, 0, sizeof(*b));
        b->on = true;
        b->mode = B_SHOOTER;
        b->x = (s_shoot_x0 + s_shoot_x1) / 2;
        b->y = s_plunger_y - BALL_R - 1.5f;
        for (int k = 0; k < N_TRAIL; k++) {
            b->tx[k] = b->x;
            b->ty[k] = b->y;
        }
        return b;
    }
    return NULL;
}

static float plunge_speed(float p)
{
    p = clampf(p, 0, 1);
    for (int i = 1; i < PLUNGE_N; i++) {
        if (p <= PLUNGE_P[i]) {
            float u = (p - PLUNGE_P[i - 1]) / (PLUNGE_P[i] - PLUNGE_P[i - 1]);
            return PLUNGE_V[i - 1] + (PLUNGE_V[i] - PLUNGE_V[i - 1]) * u;
        }
    }
    return PLUNGE_V[PLUNGE_N - 1];
}

static void launch(ball_t *b, float power)
{
    b->mode = B_FREE;
    b->vx = 0;
    b->vy = -plunge_speed(power);
    b->fresh = true;
    if (s_save_armed) {
        s_save_armed = false;
        if (s_save_t < SAVE_S) {
            s_save_t = SAVE_S;
        }
    }
    s_skill_t = 4.0f;
    voice(90, 200, 900, W_NOISE, 22, 60);
    voice(70, 120, 60, W_TRI, 70, 0);
}

// a ball that rests in the shooter lane, alone on the table: the plunger controls it
static ball_t *plunger_ball(void)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_ball[i].on && s_ball[i].mode == B_SHOOTER) {
            return &s_ball[i];
        }
    }
    return NULL;
}

static void ball_lost(ball_t *b)
{
    b->on = false;
    if (s_attract) {
        return;
    }
    if (s_save_t > 0 && !s_tilted) {
        message("BALL SAVED", NULL, rgb(120, 255, 200), 1.5f);
        snd_arp(660, 3, 70, 20);
        s_queue++;
        s_queue_t = 0.8f;
        return;
    }
    voice(500, 400, 90, W_SQUARE, 18, 0);
    if (ball_count() + s_queue == 1 && s_multiball) {
        s_multiball = false;
        message("MULTIBALL OVER", NULL, rgb(200, 200, 255), 1.4f);
    }
}

static void path_pos(float t, float *x, float *y)
{
    if (s_npath < 2) {
        *x = s_path_x[0];
        *y = s_path_y[0];
        return;
    }
    for (int i = 1; i < s_npath; i++) {
        if (t <= s_path_len[i] || i == s_npath - 1) {
            float seg = s_path_len[i] - s_path_len[i - 1];
            float u = seg > 0 ? clampf((t - s_path_len[i - 1]) / seg, 0, 1) : 1;
            *x = s_path_x[i - 1] + (s_path_x[i] - s_path_x[i - 1]) * u;
            *y = s_path_y[i - 1] + (s_path_y[i] - s_path_y[i - 1]) * u;
            return;
        }
    }
}

static void ball_step(ball_t *b, float dt)
{
    if (b->mode == B_SHOOTER) {
        b->vx = b->vy = 0;
        return;
    }
    if (b->mode == B_RAMP) {
        b->t += RAMP_V * dt;
        path_pos(b->t, &b->x, &b->y);
        if (b->t >= s_path_len[s_npath - 1]) {
            b->mode = B_FREE;
            b->vx = s_ramp_vx;
            b->vy = s_ramp_vy;
            voice(60, 900, 900, W_SINE, 24, 0);
        }
        return;
    }
    if (b->mode == B_SAUCER) {
        b->t += dt;
        if (b->t > 1.1f) {
            b->mode = B_FREE;
            b->vx = -0.35f * BUMP_KICK;
            b->vy = 0.45f * BUMP_KICK;
            b->no_catch = 0.6f;
            voice(60, 140, 60, W_TRI, 80, 0);
        }
        return;
    }
    b->px = b->x;
    b->py = b->y;
    if (b->no_catch > 0) {
        b->no_catch -= dt;
    }
    b->vy += GRAV * dt;
    b->x += b->vx * dt;
    b->y += b->vy * dt;
    collide(b);
    float v2 = b->vx * b->vx + b->vy * b->vy;
    if (v2 > MAX_V * MAX_V) {
        float k = MAX_V / sqrtf(v2);
        b->vx *= k;
        b->vy *= k;
    }
    // keep the ball inside the table (a safety net)
    if (b->x < BALL_R) {
        b->x = BALL_R + 1;
        b->vx = fabsf(b->vx);
    }
    if (b->x > SCR_W - BALL_R) {
        b->x = SCR_W - BALL_R - 1;
        b->vx = -fabsf(b->vx);
    }
    if (b->y < DMD_H + BALL_R) {
        b->y = DMD_H + BALL_R + 1;
        b->vy = fabsf(b->vy);
    }

    // sensor lines (crossed in this step)
    float x = b->x, y = b->y, py = b->py;
    if ((py - s_lane_sense_y) * (y - s_lane_sense_y) <= 0 && y > py) {
        for (int i = 0; i < 3; i++) {
            if (x > s_lane_x[i] && x < s_lane_x[i + 1]) {
                ev_lane(i);
            }
        }
    }
    if (x > s_orbit_x0 && x < s_orbit_x1) {
        if ((py - s_spin_y) * (y - s_spin_y) <= 0 && py != y) {
            ev_spinner(fabsf(b->vy));
        }
        if (py > s_orbit_y && y <= s_orbit_y && b->vy < -K(250)) {
            ev_orbit();
        }
    }
    if (x > s_ramp_x0 && x < s_ramp_x1 && py > s_ramp_sense_y && y <= s_ramp_sense_y && b->vy < -K(220)) {
        ev_ramp_in(b);
        return;
    }
    float sdx = x - s_saucer_x, sdy = y - s_saucer_y;
    if (b->no_catch <= 0 && sdx * sdx + sdy * sdy < SAUCER_R * SAUCER_R && v2 < 0.45f * MAX_V * 0.45f * MAX_V) {
        ev_saucer(b);
        return;
    }
    if (py < s_out_y && y >= s_out_y && ((x > s_out_l0 && x < s_out_l1) || (x > s_out_r0 && x < s_out_r1))) {
        add_score(500, -1, 0);
        if (!s_attract) {
            message("OUTLANE", NULL, rgb(255, 120, 120), 1.0f);
        }
    }
    // a ball that the plunger shoots goes back to the shooter lane if it does not leave it
    if (x > s_shoot_x0 && y > s_shoot_top + K(20) && fabsf(b->vy) < K(20) && y > s_plunger_y - BALL_R - K(4)) {
        b->mode = B_SHOOTER;
        b->y = s_plunger_y - BALL_R - 1.5f;
    }
    if (y > DRAIN_Y) {
        ball_lost(b);
    }
}

// ---------------------------------------------------------------- the static table layer

static void thick_line(float x0, float y0, float x1, float y1, int w, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    int n = (int)l + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        int x = (int)(x0 + dx * t + 0.5f), y = (int)(y0 + dy * t + 0.5f);
        if (w <= 1) {
            gfx_fill_rect(x, y, 1, 1, c);
        } else {
            gfx_fill_rect(x - w / 2, y - w / 2, w, w, c);
        }
    }
}

static void fill_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c)
{
    // sort by y
    if (y1 < y0) {
        float t = y0;
        y0 = y1;
        y1 = t;
        t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y2 < y0) {
        float t = y0;
        y0 = y2;
        y2 = t;
        t = x0;
        x0 = x2;
        x2 = t;
    }
    if (y2 < y1) {
        float t = y1;
        y1 = y2;
        y2 = t;
        t = x1;
        x1 = x2;
        x2 = t;
    }
    for (int y = (int)ceilf(y0); y <= (int)y2; y++) {
        float xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0 + 1e-6f);
        float xb =
                y < y1 ? x0 + (x1 - x0) * (y - y0) / (y1 - y0 + 1e-6f) : x1 + (x2 - x1) * (y - y1) / (y2 - y1 + 1e-6f);
        if (xa > xb) {
            float t = xa;
            xa = xb;
            xb = t;
        }
        gfx_fill_rect((int)xa, y, (int)(xb - xa) + 1, 1, c);
    }
}

// the box of thumb pad s (0 = left, 1 = right) on the apron
static void pad_rect(int s, int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    // (the bottom cap of the circle: two pads beside the middle)
    *w = RF * 88 / 466;
    *h = RF * 38 / 466;
    *x = s ? RF / 2 + RF * 28 / 466 : RF / 2 - RF * 28 / 466 - *w;
    *y = APRON_Y + RF * 8 / 466;
#else
    *w = SCR_W * 2 / 5;
    *h = SCR_H - APRON_Y - K(14);
    *x = s ? SCR_W - *w - 4 : 4;
    *y = APRON_Y + K(8);
#endif
}

static void draw_static(void)
{
    // the playfield: a deep blue to purple gradient with soft light rays from the bumpers
    for (int y = 0; y < SCR_H; y += 2) {
        int k = y * 255 / SCR_H;
        gfx_fill_rect(0, y, SCR_W, 2, rgb((uint8_t)(14 + k / 6), (uint8_t)(10 + k / 14), (uint8_t)(48 - k / 10)));
    }
    float cx = s_bump_x[2], cy = s_bump_y[2];
    for (int i = 0; i < 16; i++) {
        float a = i * TAU / 16;
        thick_line(cx, cy, cx + fcos(a) * SCR_W, cy + fsin(a) * SCR_W, 1, rgb(34, 24, 74));
    }
    // stars on the art
    uint32_t seed = 12345;
    for (int i = 0; i < 70; i++) {
        seed = seed * 1103515245u + 12345u;
        int x = (int)((seed >> 8) % SCR_W);
        seed = seed * 1103515245u + 12345u;
        int y = DMD_H + (int)((seed >> 8) % (APRON_Y - DMD_H));
        gfx_fill_rect(x, y, 1, 1, rgb(90, 80, 150));
    }
    // the shooter lane and the orbit lane: darker
    gfx_fill_rect((int)s_shoot_x0 + 1, (int)s_shoot_top, (int)(s_shoot_x1 - s_shoot_x0) - 1, APRON_Y - (int)s_shoot_top,
                  rgb(10, 8, 30));
    // the inserts that do not change: arrows to the ramp and the orbit
    for (int k = 0; k < 3; k++) {
        int ax = (int)((s_ramp_x0 + s_ramp_x1) / 2), ay = (int)s_ramp_sense_y + K(34) + k * K(9);
        fill_tri(ax - K(6), ay + K(6), ax + K(6), ay + K(6), ax, ay, rgb(60, 30, 40));
        int ox = (int)((s_orbit_x0 + s_orbit_x1) / 2), oy = (int)s_spin_y + K(30) + k * K(9);
        fill_tri(ox - K(5), oy + K(5), ox + K(5), oy + K(5), ox, oy, rgb(30, 50, 60));
    }
    // the drop target slots and the saucer
    for (int i = 0; i < N_DROPS; i++) {
        gfx_fill_rect((int)(s_drop_x[i] - s_drop_w / 2), (int)s_drop_y - 1, (int)s_drop_w, K(3), rgb(6, 6, 12));
    }
    gfx_fill_circle((int)s_saucer_x, (int)s_saucer_y, (int)SAUCER_R + K(3), rgb(130, 130, 150));
    gfx_fill_circle((int)s_saucer_x, (int)s_saucer_y, (int)SAUCER_R, rgb(4, 4, 8));
    // the bumper bases
    for (int i = 0; i < 3; i++) {
        gfx_fill_circle((int)s_bump_x[i], (int)s_bump_y[i] + K(2), (int)BUMP_R + K(2), rgb(8, 6, 20));
    }
    // the slingshots: white plastic with a red rubber face
    for (int s = 0; s < 2; s++) {
        float *c = s_sling[s];
        fill_tri(c[0], c[1], c[2], c[3], c[4], c[5], rgb(220, 220, 235));
        fill_tri(c[0] + (s ? -K(3) : K(3)), c[1] + K(8), c[2] + (s ? -K(3) : K(3)), c[3] - K(2),
                 c[4] + (s ? K(4) : -K(4)), c[5] - K(6), rgb(70, 160, 255));
    }
    // the walls: a dark edge and a bright metal line
    for (int i = 0; i < s_nseg; i++) {
        seg_t *s = &s_seg[i];
        if (s->kind == SG_DROP || s->kind == SG_GATE || s->kind == SG_LIP) {
            continue;
        }
        uint16_t c = s->kind == SG_SLING ? rgb(230, 50, 60) : rgb(170, 175, 200);
        thick_line(s->ax, s->ay, s->bx, s->by, K(3), rgb(30, 30, 50));
        thick_line(s->ax, s->ay, s->bx, s->by, s->kind == SG_SLING ? K(2) : 1, c);
    }
    for (int i = 0; i < s_ncirc; i++) {
        circ_t *c = &s_circ[i];
        if (c->kind == CK_POST) {
            gfx_fill_circle((int)c->x, (int)c->y, (int)c->r + 1, rgb(230, 60, 70));
            gfx_fill_circle((int)c->x, (int)c->y, 1, rgb(255, 200, 200));
        }
    }
    // the gate: a thin bright wire; the ramp mouth: a bright edge
    for (int i = 0; i < s_nseg; i++) {
        if (s_seg[i].kind == SG_GATE) {
            thick_line(s_seg[i].ax, s_seg[i].ay, s_seg[i].bx, s_seg[i].by, 1, rgb(255, 220, 120));
        }
        if (s_seg[i].kind == SG_LIP) {
            thick_line(s_seg[i].ax, s_seg[i].ay, s_seg[i].bx, s_seg[i].by, 2, rgb(120, 200, 255));
        }
    }
    // the ramp: two silver rails on the path, with ties
    for (int i = 1; i < s_npath; i++) {
        float x0 = s_path_x[i - 1], y0 = s_path_y[i - 1], x1 = s_path_x[i], y1 = s_path_y[i];
        float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
        if (l < 1) {
            continue;
        }
        float nx = -dy / l * (BALL_R + 2), ny = dx / l * (BALL_R + 2);
        for (float t = 0; t < l; t += K(7)) {
            float x = x0 + dx * t / l, y = y0 + dy * t / l;
            thick_line(x - nx, y - ny, x + nx, y + ny, 1, rgb(60, 70, 100));
        }
        thick_line(x0 + nx, y0 + ny, x1 + nx, y1 + ny, 1, rgb(200, 210, 235));
        thick_line(x0 - nx, y0 - ny, x1 - nx, y1 - ny, 1, rgb(200, 210, 235));
    }
    // the spinner frame
    gfx_fill_rect((int)s_orbit_x0 + 1, (int)s_spin_y - 1, K(2), K(3), rgb(200, 200, 220));
    gfx_fill_rect((int)s_orbit_x1 - K(3), (int)s_spin_y - 1, K(2), K(3), rgb(200, 200, 220));
    // lane letters
    static const char *const L[3] = {"P", "I", "N"};
    for (int i = 0; i < 3; i++) {
        int x = (int)((s_lane_x[i] + s_lane_x[i + 1]) / 2);
        gfx_text_center_in(x - K(6), K(12), (int)s_lane_y1 + K(12), L[i], TS(1), rgb(120, 110, 170));
    }
    // the score display: a dark panel with a fine dot grid
    gfx_fill_rect(0, 0, SCR_W, DMD_H, rgb(16, 10, 6));
    for (int y = 1; y < DMD_H - 1; y += 2) {
        for (int x = 1; x < SCR_W - 1; x += 2) {
            gfx_fill_rect(x, y, 1, 1, rgb(36, 20, 8));
        }
    }
    gfx_fill_rect(0, DMD_H - 2, SCR_W, 2, rgb(80, 80, 110));
    // the apron: a metal panel with two thumb pads
    gfx_fill_rect(0, APRON_Y, SCR_W, SCR_H - APRON_Y, rgb(34, 36, 52));
    gfx_fill_rect(0, APRON_Y, SCR_W, 2, rgb(150, 155, 185));
    for (int s = 0; s < 2; s++) {
        int x, py, pad_w, pad_h;
        pad_rect(s, &x, &py, &pad_w, &pad_h);
        gfx_fill_rect(x, py, pad_w, pad_h, rgb(22, 24, 36));
        gfx_frame_rect(x, py, pad_w, pad_h, 1, rgb(90, 95, 130));
        gfx_ring(x + pad_w / 2, py + pad_h / 2, pad_h / 3 + 2, pad_h / 3, rgb(70, 75, 110));
        gfx_text_center_in(x, pad_w, py + pad_h / 2 - 3 * TS(1), s ? "R" : "L", TS(1), rgb(110, 115, 160));
    }
}

static void build_bg(void)
{
    if (s_bg_ok) {
        return;
    }
    if (!s_bg) {
        s_bg = heap_caps_malloc((size_t)SCR_W * SCR_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_bg) {
            return;
        }
    }
    if (gfx_w != SCR_W || gfx_h != SCR_H) {
        return;
    }
    uint16_t *fb = g_fb;
    g_fb = s_bg;
    gfx_clip_reset();
    draw_static();
    g_fb = fb;
    s_bg_ok = true;
}

// ---------------------------------------------------------------- game flow

static void reset_table_lights(void)
{
    for (int i = 0; i < N_DROPS; i++) {
        s_drop_up[i] = true;
    }
    s_lane_lit[0] = s_lane_lit[1] = s_lane_lit[2] = false;
    s_lock_lit = false;
}

static void ball_start(void)
{
    for (int i = 0; i < MAX_BALLS; i++) {
        s_ball[i].on = false;
    }
    s_tilted = false;
    s_danger = 0;
    s_multiball = false;
    s_queue = 0;
    s_mult = 1;
    s_locks = 0;
    s_combo_t = 0;
    s_save_t = 0;
    s_save_armed = true;
    reset_table_lights();
    s_n_ramps = s_n_lanesets = s_n_banks = s_n_bumps = s_n_spins = s_n_orbits = 0;
    ball_new_in_shooter();
    s_auto_t = 0;
    s_plunger = 0;
    s_pulling = false;
    s_skill_lane = (int)(esp_random() % 3);
    imu_hits_clear();
}

static void new_game(int64_t now)
{
    s_attract = false;
    s_state = G_PLAY;
    s_state_at = now;
    s_score = 0;
    s_new_best = false;
    s_ball_no = 1;
    s_extra = 0;
    s_extra_given = false;
    s_missions_done = 0;
    s_mission = -1;
    mission_new();
    ball_start();
    message("BALL 1", "HOLD, LET GO", rgb(255, 200, 80), 2.5f);
    tune_play(TUNE_PINBALL);
}

static void end_of_ball(int64_t now)
{
    if (s_tilted) {
        s_bonus_total = 0;
    } else {
        s_bonus_total = (1000u * (uint32_t)s_n_ramps + 2000u * (uint32_t)s_n_lanesets + 3000u * (uint32_t)s_n_banks +
                         50u * (uint32_t)s_n_bumps + 100u * (uint32_t)s_n_spins + 1000u * (uint32_t)s_n_orbits) *
                        (uint32_t)s_mult;
    }
    s_bonus_shown = 0;
    s_bonus_t = 0;
    s_bonus_step = 0;
    s_state = G_BONUS;
    s_state_at = now;
}

static void next_ball(int64_t now)
{
    s_score += s_bonus_total;
    if (s_extra > 0) {
        s_extra--;
        ball_start();
        s_state = G_PLAY;
        message("SHOOT AGAIN", NULL, rgb(255, 210, 60), 2.0f);
        return;
    }
    if (s_ball_no >= BALLS) {
        s_state = G_OVER;
        s_state_at = now;
        if (s_score > s_best) {
            s_best = s_score;
            s_new_best = true;
            store_set("pin_best", s_best);
        }
        voice(700, 330, 110, W_SQUARE, 18, 0);
        return;
    }
    s_ball_no++;
    ball_start();
    s_state = G_PLAY;
    char a[22];
    snprintf(a, sizeof(a), "BALL %d", s_ball_no);
    message(a, NULL, rgb(255, 200, 80), 1.6f);
}

static void start_attract(void)
{
    s_attract = true;
    for (int i = 0; i < MAX_BALLS; i++) {
        s_ball[i].on = false;
    }
    reset_table_lights();
    s_tilted = false;
    s_queue = 0;
}

// the computer flippers (title demo; tests): flip when a ball is close above a flipper
static void auto_flip(void)
{
    for (int k = 0; k < 2; k++) {
        flipper_t *f = &s_flip[k];
        float tx, ty;
        flipper_tip(f, &tx, &ty);
        bool want = false;
        for (int i = 0; i < MAX_BALLS; i++) {
            ball_t *b = &s_ball[i];
            if (!b->on || b->mode != B_FREE) {
                continue;
            }
            float lo = fminf(f->px, tx) - K(2), hi = fmaxf(f->px, tx) + K(2);
            if (b->x > lo && b->x < hi && b->y > f->py - K(26) && b->y < f->py + K(16) && b->vy > -K(50)) {
                want = true;
            }
        }
        f->held = want;
    }
}

static void attract_step(void)
{
    if (ball_count() == 0) {
        ball_t *b = ball_new_in_shooter();
        if (b) {
            launch(b, frand(0.3f, 1.0f));
        }
    }
    auto_flip();
}

// ---------------------------------------------------------------- app

static void pin_init(void)
{
    s_best = store_get("pin_best", 0);
    table_build();
}

// the settings panel: RESTART (a hold of 1 s, only in a game: the game is lost)
static const pitem_t PIN_ITEMS[] = {
        {PI_ACT2, 0, 0, 0, {"RESTART", "מחדש"}, NULL, NULL},
};
static bool pin_item_shown(int id)
{
    (void)id;
    return s_state == G_PLAY || s_state == G_BONUS;
}
static void pin_item_set(int id, int v)
{
    (void)id;
    (void)v;
    new_game(s_prev_ms);
}
static const pitems_t PIN_PANEL = {PIN_ITEMS, 1, pin_item_shown, NULL, pin_item_set, NULL, NULL};

static void pin_enter(int64_t now)
{
    sysui_panel_items(&PIN_PANEL);
    s_prev_ms = now;
    s_state = G_TITLE;
    s_state_at = now;
    s_msg_t = 0;
    memset(s_pop, 0, sizeof(s_pop));
    memset(s_part, 0, sizeof(s_part));
    s_prev_l = s_prev_r = false;
    start_attract();
    build_bg();
    imu_hits_clear();
}

// The table layer (220 KB of PSRAM on the 3.49) goes at the leave; the next enter draws it again
static void pin_leave(void)
{
    if (s_bg) {
        heap_caps_free(s_bg);
    }
    s_bg = NULL;
    s_bg_ok = false;
}

static void pin_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    int64_t now = ev->t_ms;
    if (s_state == G_TITLE) {
        new_game(now);
    } else if (s_state == G_OVER && now - s_state_at > 900) {
        new_game(now);
    }
}

static void read_touch(void)
{
    int16_t px[2], py[2];
    int n = input_points(px, py, 2);
    bool l = false, r = false;
    for (int i = 0; i < n; i++) {
        int x, y;
        gfx_touch_to_logical(px[i], py[i], &x, &y);
        if (x < SCR_W / 2) {
            l = true;
        } else {
            r = true;
        }
    }
    s_touch = n > 0;
    ball_t *pb = plunger_ball();
    bool plunge = pb && ball_count() == 1 && s_queue == 0;
    if (plunge) {
        // the touch pulls the plunger; the release shoots
        if (s_touch) {
            s_pulling = true;
            s_auto_t = 0;
        } else if (s_pulling) {
            s_pulling = false;
            launch(pb, s_plunger);
            s_plunger = 0;
        }
        l = r = false;
    } else {
        s_pulling = false;
        s_plunger = 0;
    }
    // a flipper press moves the lit lanes (a lane change)
    if (!s_tilted) {
        if (l && !s_prev_l) {
            snd_flipper();
            bool t = s_lane_lit[0];
            s_lane_lit[0] = s_lane_lit[1];
            s_lane_lit[1] = s_lane_lit[2];
            s_lane_lit[2] = t;
        }
        if (r && !s_prev_r) {
            snd_flipper();
            bool t = s_lane_lit[2];
            s_lane_lit[2] = s_lane_lit[1];
            s_lane_lit[1] = s_lane_lit[0];
            s_lane_lit[0] = t;
        }
    }
    s_prev_l = l;
    s_prev_r = r;
    s_flip[0].held = l;
    s_flip[1].held = r;
}

static void nudge(void)
{
    imu_hit_t h;
    while (imu_pop_hit(&h)) {
        if (s_state != G_PLAY || s_tilted) {
            continue;
        }
        float l = sqrtf(h.x * h.x + h.y * h.y);
        if (l < 0.3f) {
            continue;
        }
        // the table moves with the board; the balls stay: they move against the knock
        float dx = -h.x / l, dy = -h.y / l, k = NUDGE_V * clampf(h.g / 1.6f, 0.6f, 1.4f);
        for (int i = 0; i < MAX_BALLS; i++) {
            if (s_ball[i].on && s_ball[i].mode == B_FREE) {
                s_ball[i].vx += dx * k;
                s_ball[i].vy += dy * k;
            }
        }
        s_danger += 1.0f;
        if (s_danger >= 3.0f) {
            s_tilted = true;
            message("TILT", "NO BONUS", rgb(255, 40, 40), 3.0f);
            voice(900, 110, 100, W_SQUARE, 28, 0);
            s_save_t = 0;
        } else if (s_danger >= 2.0f) {
            s_warn_t = 1.5f;
            message("WARNING", "LESS SHAKE!", rgb(255, 150, 40), 1.5f);
            voice(250, 220, 220, W_SQUARE, 20, 0);
        } else {
            voice(40, 90, 60, W_TRI, 60, 0);
        }
    }
}

static void pin_update(int64_t now)
{
    float dt = (now - s_prev_ms) / 1000.0f;
    s_prev_ms = now;
    if (dt > 0.04f) {
        dt = 0.04f;
    }
    if (dt < 0) {
        dt = 0;
    }
    s_time += dt;
    build_bg();

    if (s_state == G_PLAY) {
        read_touch();
        nudge();
    } else {
        imu_hits_clear();
        if (s_state == G_TITLE) {
            attract_step();
        } else {
            s_flip[0].held = s_flip[1].held = false;
        }
    }

    // timers
    if (s_msg_t > 0) {
        s_msg_t -= dt;
    }
    if (s_save_t > 0) {
        s_save_t -= dt;
    }
    if (s_skill_t > 0) {
        s_skill_t -= dt;
    }
    if (s_combo_t > 0) {
        s_combo_t -= dt;
    }
    if (s_warn_t > 0) {
        s_warn_t -= dt;
    }
    if (s_danger > 0) {
        s_danger = fmaxf(0, s_danger - dt / 2.5f);
    }
    if (s_ramp_flash > 0) {
        s_ramp_flash -= dt;
    }
    if (s_saucer_flash > 0) {
        s_saucer_flash -= dt;
    }
    for (int i = 0; i < 3; i++) {
        if (s_bump_flash[i] > 0) {
            s_bump_flash[i] -= dt;
        }
        if (s_bump_cool[i] > 0) {
            s_bump_cool[i] -= dt;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (s_sling_flash[i] > 0) {
            s_sling_flash[i] -= dt;
        }
        if (s_sling_cool[i] > 0) {
            s_sling_cool[i] -= dt;
        }
    }
    if (s_drop_reset_t > 0) {
        s_drop_reset_t -= dt;
        if (s_drop_reset_t <= 0) {
            for (int i = 0; i < N_DROPS; i++) {
                s_drop_up[i] = true;
            }
        }
    }
    s_spin_ang += s_spin_v * dt;
    s_spin_v *= expf(-dt * 2.5f);
    if (s_pulling) {
        s_plunger = fminf(1.0f, s_plunger + dt / 0.9f);
    }
    // the skill shot lane moves while the ball waits
    if (plunger_ball() && ball_count() == 1 && !s_multiball) {
        if (fmodf(s_time, 0.8f) < dt) {
            s_skill_lane = (s_skill_lane + 1) % 3;
        }
    }

    // the auto plunger: balls in the queue, and a ball that waits too long
    if (s_queue > 0) {
        s_queue_t -= dt;
        if (s_queue_t <= 0 && !plunger_ball()) {
            ball_t *b = ball_new_in_shooter();
            if (b) {
                s_queue--;
                s_queue_t = 0.7f;
            }
        }
    }
    ball_t *pb = plunger_ball();
    if (pb && s_state == G_PLAY) {
        bool alone = ball_count() == 1 && s_queue == 0;
        if (!alone) {
            s_shoot_wait += dt;
            if (s_shoot_wait > 0.5f) {
                launch(pb, 0.85f);
                s_shoot_wait = 0;
            }
        } else if (!s_pulling) {
            s_auto_t += dt;
            if (s_auto_t > AUTO_S) {
                launch(pb, 0.75f);
                s_auto_t = 0;
            }
        }
    }

    // physics in steps of 1 ms
    int n = (int)(dt / SUB_DT + 0.5f);
    if (n > MAX_SUB) {
        n = MAX_SUB;
    }
    for (int k = 0; k < n; k++) {
        flipper_step(&s_flip[0], SUB_DT);
        flipper_step(&s_flip[1], SUB_DT);
        for (int i = 0; i < MAX_BALLS; i++) {
            if (s_ball[i].on) {
                ball_step(&s_ball[i], SUB_DT);
            }
        }
        for (int i = 0; i < MAX_BALLS; i++) {
            for (int j = i + 1; j < MAX_BALLS; j++) {
                if (s_ball[i].on && s_ball[j].on && s_ball[i].mode == B_FREE && s_ball[j].mode == B_FREE) {
                    ball_pair(&s_ball[i], &s_ball[j]);
                }
            }
        }
    }

    // trails, ball search
    for (int i = 0; i < MAX_BALLS; i++) {
        ball_t *b = &s_ball[i];
        if (!b->on) {
            continue;
        }
        for (int k = N_TRAIL - 1; k > 0; k--) {
            b->tx[k] = b->tx[k - 1];
            b->ty[k] = b->ty[k - 1];
        }
        b->tx[0] = b->x;
        b->ty[0] = b->y;
        if (b->mode == B_FREE && b->vx * b->vx + b->vy * b->vy < 25 * 25) {
            b->still += dt;
            if (b->still > 3.0f) {
                // ball search: a kick frees a ball that is stuck
                b->vx = frand(-200, 200);
                b->vy = -frand(250, 400);
                b->still = 0;
                if (!s_attract) {
                    message("BALL SEARCH", NULL, rgb(180, 180, 255), 1.0f);
                }
            }
        } else {
            b->still = 0;
        }
    }

    // effects
    for (int i = 0; i < N_PART; i++) {
        part_t *p = &s_part[i];
        if (p->life <= 0) {
            continue;
        }
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += GRAV * 0.3f * dt;
    }
    for (int i = 0; i < N_POP; i++) {
        if (s_pop[i].life > 0) {
            s_pop[i].life -= dt;
            s_pop[i].y -= 30 * dt;
        }
    }

    // the end of a ball
    if (s_state == G_PLAY && ball_count() == 0 && s_queue == 0) {
        end_of_ball(now);
    }
    if (s_state == G_BONUS) {
        s_bonus_t += dt;
        uint32_t target = s_bonus_t < 0.4f ? 0 : (uint32_t)(s_bonus_total * fminf(1.0f, (s_bonus_t - 0.4f) / 1.2f));
        if (target > s_bonus_shown && (int)(s_bonus_t * 20) != s_bonus_step) {
            s_bonus_step = (int)(s_bonus_t * 20);
            voice(25, 1200, 1200, W_PULSE, 10, 0);
        }
        s_bonus_shown = target;
        if (s_bonus_t > 2.4f) {
            next_ball(now);
        }
    }
}

// ---------------------------------------------------------------- drawing

static void draw_flipper(const flipper_t *f, bool lit)
{
    float tx, ty;
    flipper_tip(f, &tx, &ty);
    uint16_t edge = s_tilted ? rgb(90, 90, 100) : rgb(200, 30, 40);
    uint16_t body = s_tilted ? rgb(150, 150, 160) : (lit ? rgb(255, 255, 255) : rgb(235, 235, 240));
    for (int i = 0; i <= 8; i++) {
        float t = i / 8.0f;
        float x = f->px + (tx - f->px) * t, y = f->py + (ty - f->py) * t;
        float r = FLIP_R0 + (FLIP_R1 - FLIP_R0) * t;
        gfx_fill_circle((int)(x + 0.5f), (int)(y + 0.5f), (int)(r + 1.5f), edge);
    }
    for (int i = 0; i <= 8; i++) {
        float t = i / 8.0f;
        float x = f->px + (tx - f->px) * t, y = f->py + (ty - f->py) * t;
        float r = FLIP_R0 + (FLIP_R1 - FLIP_R0) * t;
        gfx_fill_circle((int)(x + 0.5f), (int)(y + 0.5f), (int)(r + 0.3f), body);
    }
    gfx_fill_circle((int)f->px, (int)f->py, K(2), rgb(200, 30, 40));
}

static void draw_ball(const ball_t *b)
{
    float v2 = b->vx * b->vx + b->vy * b->vy;
    if (b->mode == B_FREE && v2 > K(500) * K(500)) {
        for (int k = N_TRAIL - 1; k >= 1; k--) {
            int r = (int)(BALL_R * (1.0f - k * 0.15f));
            uint8_t g = (uint8_t)(140 - k * 20);
            gfx_fill_circle((int)b->tx[k], (int)b->ty[k], r, rgb(g / 2, g / 2, g));
        }
    }
    int x = (int)(b->x + 0.5f), y = (int)(b->y + 0.5f), r = (int)BALL_R;
    if (b->mode == B_RAMP) {
        // on the ramp: a shadow, and the ball is a little bigger (nearer)
        gfx_fill_circle(x + K(3), y + K(4), r, rgb(10, 8, 20));
        r += 1;
    }
    gfx_fill_circle(x, y, r, rgb(150, 155, 170));
    gfx_fill_circle(x - 1, y - 1, r - 1, rgb(210, 215, 230));
    gfx_fill_circle(x - r / 3, y - r / 3, r / 3 + 1, rgb(255, 255, 255));
}

static void draw_dynamic(void)
{
    float t = s_time;
    bool blink = fmodf(t, 0.5f) < 0.25f;
    // lane lights (the skill lane blinks white while the ball waits)
    for (int i = 0; i < 3; i++) {
        int x = (int)((s_lane_x[i] + s_lane_x[i + 1]) / 2), y = (int)s_lane_y1 + K(4);
        uint16_t c = s_lane_lit[i] ? rgb(255, 230, 60) : rgb(70, 60, 30);
        bool skill = (s_skill_t > 0 || plunger_ball()) && i == s_skill_lane && s_state == G_PLAY && !s_multiball;
        if (skill && blink) {
            c = rgb(255, 255, 255);
        }
        gfx_fill_circle(x, y, K(3), c);
    }
    // the bonus multiplier lights (x2..x5) above the flippers
    for (int i = 0; i < 4; i++) {
        int x = (int)((s_flip[0].px + s_flip[1].px) / 2) - K(18) + i * K(12);
        int y = (int)s_flip[0].py - K(44);
        gfx_fill_circle(x, y, K(3), s_mult >= i + 2 ? rgb(255, 140, 40) : rgb(60, 36, 20));
    }
    // the ramp arrows: jackpot (red) or combo (yellow)
    uint16_t ra = rgb(60, 30, 40);
    if (s_multiball) {
        ra = blink ? rgb(255, 40, 40) : rgb(120, 20, 20);
    } else if (s_combo_t > 0) {
        ra = rgb(255, 210, 60);
    } else if (s_ramp_flash > 0) {
        ra = rgb(255, 255, 255);
    }
    int ax = (int)((s_ramp_x0 + s_ramp_x1) / 2);
    for (int k = 0; k < 3; k++) {
        int ay = (int)s_ramp_sense_y + K(34) + k * K(9);
        if (ra != rgb(60, 30, 40)) {
            fill_tri(ax - K(6), ay + K(6), ax + K(6), ay + K(6), ax, ay, ra);
        }
    }
    if (s_combo_t > 0) {
        int ox = (int)((s_orbit_x0 + s_orbit_x1) / 2);
        for (int k = 0; k < 3; k++) {
            int oy = (int)s_spin_y + K(30) + k * K(9);
            fill_tri(ox - K(5), oy + K(5), ox + K(5), oy + K(5), ox, oy, rgb(80, 220, 255));
        }
    }
    // the saucer: LOCK lit
    if (s_lock_lit && blink) {
        gfx_ring((int)s_saucer_x, (int)s_saucer_y, (int)SAUCER_R + K(4), (int)SAUCER_R + 1, rgb(80, 220, 255));
    }
    if (s_saucer_flash > 0) {
        gfx_ring((int)s_saucer_x, (int)s_saucer_y, (int)SAUCER_R + K(7), (int)SAUCER_R + K(4), rgb(255, 255, 255));
    }
    for (int i = 0; i < s_locks && i < 2; i++) {
        gfx_fill_circle((int)s_saucer_x - K(6) + i * K(12), (int)s_saucer_y + (int)SAUCER_R + K(9), K(3),
                        rgb(80, 220, 255));
    }
    // drop targets
    for (int i = 0; i < N_DROPS; i++) {
        if (!s_drop_up[i]) {
            continue;
        }
        int x = (int)(s_drop_x[i] - s_drop_w / 2) + 1, w = (int)s_drop_w - 2;
        gfx_fill_rect(x, (int)s_drop_y - K(4), w, K(7), rgb(255, 120, 40));
        gfx_fill_rect(x, (int)s_drop_y - K(4), w, K(2), rgb(255, 210, 160));
    }
    // bumpers
    static const uint8_t BC[3][3] = {{255, 70, 120}, {70, 200, 255}, {255, 200, 50}};
    for (int i = 0; i < 3; i++) {
        int x = (int)s_bump_x[i], y = (int)s_bump_y[i], r = (int)BUMP_R;
        bool f = s_bump_flash[i] > 0;
        gfx_fill_circle(x, y, r, f ? rgb(255, 255, 255) : rgb(BC[i][0] / 3, BC[i][1] / 3, BC[i][2] / 3));
        gfx_ring(x, y, r, r - K(3), rgb(BC[i][0], BC[i][1], BC[i][2]));
        gfx_fill_circle(x, y, r / 2, f ? rgb(BC[i][0], BC[i][1], BC[i][2]) : rgb(240, 240, 250));
        if (f) {
            gfx_ring(x, y, r + K(4), r + K(2), rgb(255, 255, 220));
        }
    }
    // sling flashes
    for (int s = 0; s < 2; s++) {
        if (s_sling_flash[s] <= 0) {
            continue;
        }
        float *c = s_sling[s];
        thick_line(c[4], c[5], c[0], c[1], K(3), rgb(255, 255, 160));
    }
    // the spinner plate
    {
        float w = (s_orbit_x1 - s_orbit_x0 - 4) * fabsf(fcos(s_spin_ang));
        int cx = (int)((s_orbit_x0 + s_orbit_x1) / 2);
        gfx_fill_rect(cx - (int)(w / 2), (int)s_spin_y - 1, (int)w + 1, K(3), rgb(230, 230, 250));
    }
    // the plunger spring
    {
        int x = (int)s_shoot_x0 + K(3), w = (int)(s_shoot_x1 - s_shoot_x0) - K(5);
        int top = (int)(s_plunger_y + K(2) + s_plunger * K(10));
        int bot = APRON_Y - 1;
        if (bot > top) {
            for (int y = top; y < bot; y += K(3)) {
                gfx_fill_rect(x, y, w, 1, rgb(160, 160, 180));
            }
        }
        gfx_fill_rect(x, top - K(2), w, K(3), rgb(230, 60, 60));
    }
    // flippers and balls
    draw_flipper(&s_flip[0], s_flip[0].held);
    draw_flipper(&s_flip[1], s_flip[1].held);
    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_ball[i].on && s_ball[i].mode != B_RAMP) {
            draw_ball(&s_ball[i]);
        }
    }
    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_ball[i].on && s_ball[i].mode == B_RAMP) {
            draw_ball(&s_ball[i]);
        }
    }
    // sparks and score pops
    for (int i = 0; i < N_PART; i++) {
        part_t *p = &s_part[i];
        if (p->life > 0) {
            gfx_fill_rect((int)p->x, (int)p->y, K(2), K(2), p->c);
        }
    }
    for (int i = 0; i < N_POP; i++) {
        pop_t *p = &s_pop[i];
        if (p->life <= 0) {
            continue;
        }
        int w = gfx_text_width(p->txt, TS(1));
        int x = (int)p->x - w / 2;
        if (x < 2) {
            x = 2;
        }
        if (x + w > SCR_W - 2) {
            x = SCR_W - 2 - w;
        }
        gfx_text(x + 1, (int)p->y + 1, p->txt, TS(1), rgb(0, 0, 0));
        gfx_text(x, (int)p->y, p->txt, TS(1), p->c);
    }
    // the thumb pads light up while a finger is on them
    for (int s = 0; s < 2; s++) {
        if (!s_flip[s].held && !(s_pulling && s_touch)) {
            continue;
        }
        int x, py, pad_w, pad_h;
        pad_rect(s, &x, &py, &pad_w, &pad_h);
        gfx_frame_rect(x, py, pad_w, pad_h, 2, s_pulling ? rgb(255, 120, 60) : rgb(255, 230, 120));
    }
}

static void draw_dmd(void)
{
    uint16_t amber = rgb(255, 150, 40), dim = rgb(150, 80, 20);
    char buf[24];
#if BOARD_UI_TABLET
    // the Tab5: a tall display: one big line and one small line
    if (s_state == G_TITLE) {
        gfx_text_center(18, "PINBALL", 6, amber);
        fmt_score(buf, sizeof(buf), s_best);
        char b2[30];
        snprintf(b2, sizeof(b2), "BEST %s", buf);
        gfx_text_center(74, b2, 2, dim);
        return;
    }
    if (s_msg_t > 0 && s_msg[0][0] && fmodf(s_msg_t, 0.3f) > 0.06f) {
        int sc = gfx_text_width(s_msg[0], 4) <= SCR_W - 8 ? 4 : (gfx_text_width(s_msg[0], 3) <= SCR_W - 8 ? 3 : 2);
        gfx_text_center(s_msg[1][0] ? 18 : 34, s_msg[0], sc, s_msg_c);
        if (s_msg[1][0]) {
            gfx_text_center(74, s_msg[1], 2, s_msg_c);
        }
        return;
    }
    fmt_score(buf, sizeof(buf), s_score);
    gfx_text_center(16, buf, gfx_text_width(buf, 6) <= SCR_W - 8 ? 6 : 4, amber);
    {
        char b2[34];
        if (s_state == G_BONUS) {
            char sb[16];
            fmt_score(sb, sizeof(sb), s_bonus_shown);
            snprintf(b2, sizeof(b2), "BONUS %s X%d", sb, s_mult);
        } else {
            snprintf(b2, sizeof(b2), "BALL %d  X%d", s_ball_no, s_mult);
        }
        gfx_text_center(74, b2, 2, dim);
    }
    return;
#endif
#if BOARD_ROUND
    // the top cap: one line in the narrow top (the score), one line under it (the ball, the mission)
    const int y1 = DMD_H * 14 / 62, y2 = DMD_H * 38 / 62;
    if (s_state == G_TITLE) {
        gfx_text_center(y1, "PINBALL", 2, amber);
        return;
    }
    if (s_msg_t > 0 && s_msg[0][0] && fmodf(s_msg_t, 0.3f) > 0.06f) {
        gfx_text_center(y1, s_msg[0], gfx_text_width(s_msg[0], 2) <= SCR_W / 2 ? 2 : 1, s_msg_c);
        if (s_msg[1][0]) {
            gfx_text_center(y2, s_msg[1], 1, s_msg_c);
        }
        return;
    }
    fmt_score(buf, sizeof(buf), s_score);
    gfx_text_center(y1, buf, 2, amber);
    char b2[40];
    if (s_state == G_BONUS) {
        char sb[16];
        fmt_score(sb, sizeof(sb), s_bonus_shown);
        snprintf(b2, sizeof(b2), "BONUS %s X%d", sb, s_mult);
    } else {
        snprintf(b2, sizeof(b2), "BALL %d  X%d  %s %d/%d", s_ball_no, s_mult, MISSION_SHORT[s_mission], s_mission_n,
                 s_mission_goal);
    }
    gfx_text_center(y2, b2, 1, dim);
    return;
#endif
    if (s_state == G_TITLE) {
        gfx_text_center(DMD_H > 30 ? 4 : 4, "PINBALL", 2, amber);
        if (DMD_H > 30) {
            fmt_score(buf, sizeof(buf), s_best);
            char b2[30];
            snprintf(b2, sizeof(b2), "BEST %s", buf);
            gfx_text_center(22, b2, gfx_text_width(b2, 2) <= SCR_W - 4 ? 2 : 1, dim);
        }
        return;
    }
    if (s_msg_t > 0 && s_msg[0][0] && fmodf(s_msg_t, 0.3f) > 0.06f) {
        if (DMD_H > 30) {
            // (the second line in the size-2 font when it fits)
            gfx_text_center(4, s_msg[0], gfx_text_width(s_msg[0], 2) <= SCR_W - 4 ? 2 : 1, s_msg_c);
            if (s_msg[1][0]) {
                gfx_text_center(22, s_msg[1], gfx_text_width(s_msg[1], 2) <= SCR_W - 4 ? 2 : 1, s_msg_c);
            }
        } else {
            int w = gfx_text_width(s_msg[0], 2);
            gfx_text_center(4, s_msg[0], w <= SCR_W - 4 ? 2 : 1, s_msg_c);
        }
        return;
    }
    fmt_score(buf, sizeof(buf), s_score);
    if (DMD_H > 30) {
        gfx_text_center(4, buf, 2, amber);
        char b2[34];
        if (s_state == G_BONUS) {
            char sb[16];
            fmt_score(sb, sizeof(sb), s_bonus_shown);
            snprintf(b2, sizeof(b2), "BONUS %s X%d", sb, s_mult);
        } else {
            // (a short mission name, in the size-2 font; the apron shows the balls that are left)
            snprintf(b2, sizeof(b2), "%s %d/%d", MISSION_SHORT[s_mission], s_mission_n, s_mission_goal);
        }
        gfx_text_center(22, b2, gfx_text_width(b2, 2) <= SCR_W - 4 ? 2 : 1, dim);
    } else {
        gfx_text(4, 4, buf, 2, amber);
        snprintf(buf, sizeof(buf), "BALL %d", s_ball_no);
        gfx_text(SCR_W - 4 - gfx_text_width(buf, 1), 4, buf, 1, dim);
        if (s_state == G_BONUS) {
            char sb[16];
            fmt_score(sb, sizeof(sb), s_bonus_shown);
            snprintf(buf, sizeof(buf), "+%s", sb);
        } else {
            snprintf(buf, sizeof(buf), "X%d", s_mult);
        }
        gfx_text(SCR_W - 4 - gfx_text_width(buf, 1), 13, buf, 1, dim);
    }
}

// the apron centre: the mission (CoreS3) or the balls left (3.49)
static void draw_apron(void)
{
    if (s_state != G_PLAY && s_state != G_BONUS) {
        return;
    }
    int pad_w = SCR_W * 2 / 5;
    int x0 = pad_w + 6, w = SCR_W - 2 * pad_w - 12;
    char buf[24];
#if BOARD_ROUND
    int px, py, ph;
    pad_rect(0, &px, &py, &pad_w, &ph);
    x0 = px + pad_w + 6;
    w = SCR_W - 2 * x0;
    for (int i = 0; i < BALLS + s_extra - s_ball_no + 1 && i < 3; i++) {
        gfx_fill_circle(x0 + w / 2, py + 6 + i * 13, 5, rgb(200, 205, 220));
    }
    (void)buf;
    (void)ph;
#elif BOARD_BAR_SCREEN
    for (int i = 0; i < BALLS + s_extra - s_ball_no + 1 && i < 4; i++) {
        gfx_fill_circle(x0 + w / 2, APRON_Y + 18 + i * 13, 4, rgb(200, 205, 220));
    }
    (void)buf;
#else
    snprintf(buf, sizeof(buf), "%d/%d", s_mission_n, s_mission_goal);
    // (the short name: the long one went over the thumb pads)
    gfx_text_center_in(x0, w, APRON_Y + K(10) + (TS(1) - 1) * 12, MISSION_SHORT[s_mission], TS(1), rgb(170, 175, 210));
    gfx_text_center_in(x0, w, APRON_Y + K(22) + (TS(1) - 1) * 20, buf, TS(2), rgb(255, 230, 120));
#endif
    if (s_save_t > 0 && s_state == G_PLAY && fmodf(s_time, 0.4f) < 0.25f) {
        gfx_text_center_in(x0 - 6, w + 12, SCR_H - K(10) - (TS(1) - 1) * 12, "SAVE", TS(1), rgb(120, 255, 200));
    }
}

static void overlay_band(int y, int h)
{
    gfx_darken_rect(0, y, SCR_W, h, 2);
    gfx_fill_rect(0, y, SCR_W, 1, rgb(255, 150, 40));
    gfx_fill_rect(0, y + h - 1, SCR_W, 1, rgb(255, 150, 40));
}

static void draw_title(void)
{
    char buf[24];
#if BOARD_BAR_SCREEN
    int y = 220, h = 196;
#else
    int y = K(64) + (int)TAB_OY, h = K(160);
#endif
    overlay_band(y, h);
    uint16_t a = rgb(255, 150, 40), w = rgb(235, 235, 255), d = rgb(150, 150, 190);
#if BOARD_BAR_SCREEN
    gfx_text_center(y + 10, "PIN", 4, a);
    gfx_text_center(y + 44, "BALL", 4, rgb(255, 80, 160));
    // (the size-2 font, at most 14 letters in a line)
    gfx_text_center(y + 80, "HALF = FLIPPER", 2, w);
    gfx_text_center(y + 97, "HOLD, LET GO", 2, w);
    gfx_text_center(y + 114, "KNOCK: NUDGE", 2, d);
    gfx_text_center(y + 131, "TOO MUCH: TILT", 2, d);
    fmt_score(buf, sizeof(buf), s_best);
    char b2[30];
    snprintf(b2, sizeof(b2), "BEST %s", buf);
    gfx_text_center(y + 152, b2, gfx_text_width(b2, 2) <= SCR_W - 4 ? 2 : 1, rgb(255, 230, 120));
    if (fmodf(s_time, 1.0f) < 0.6f) {
        gfx_text_center(y + 170, "TAP TO PLAY", 2, w);
    }
#else
    gfx_text_center(y + K(10), "PINBALL", TS(4), a);
    gfx_text_center(y + K(50), "LEFT HALF: LEFT FLIPPER", TS(1), w);
    gfx_text_center(y + K(62), "RIGHT HALF: RIGHT FLIPPER", TS(1), w);
    gfx_text_center(y + K(74), "HOLD, THEN RELEASE: SHOOT", TS(1), w);
    gfx_text_center(y + K(86), "KNOCK THE BOARD: NUDGE", TS(1), d);
    fmt_score(buf, sizeof(buf), s_best);
    char b2[30];
    snprintf(b2, sizeof(b2), "BEST %s", buf);
    gfx_text_center(y + K(104), b2, TS(1), rgb(255, 230, 120));
    if (fmodf(s_time, 1.0f) < 0.6f) {
        gfx_text_center(y + K(124), "TAP TO PLAY", TS(2), w);
    }
#endif
}

static void draw_over(void)
{
    char buf[24], b2[30];
#if BOARD_BAR_SCREEN
    int y = 230, h = 150;
#else
    int y = K(70) + (int)TAB_OY, h = K(140);
#endif
    overlay_band(y, h);
    gfx_text_center(y + K(12), "GAME", TS(3), rgb(255, 80, 80));
    gfx_text_center(y + K(38), "OVER", TS(3), rgb(255, 80, 80));
    fmt_score(buf, sizeof(buf), s_score);
    gfx_text_center(y + K(70), buf, TS(2), rgb(255, 240, 200));
    if (s_new_best) {
        if (fmodf(s_time, 0.5f) < 0.3f) {
            gfx_text_center(y + K(92), "NEW BEST!", TS(2), rgb(255, 230, 80));
        }
    } else {
        fmt_score(buf, sizeof(buf), s_best);
        snprintf(b2, sizeof(b2), "BEST %s", buf);
        gfx_text_center(y + K(96), b2, BOARD_BAR_SCREEN && gfx_text_width(b2, 2) <= SCR_W - 4 ? 2 : TS(1),
                        rgb(200, 200, 230));
    }
    gfx_text_center(y + h - K(22), "TAP TO PLAY", TS(2), rgb(235, 235, 255));
}

static void draw_message_on_table(void)
{
    // big events also show on the table for a moment
    if (s_msg_t <= 0 || s_state != G_PLAY) {
        return;
    }
    if (strcmp(s_msg[0], "MULTIBALL") && strcmp(s_msg[0], "JACKPOT") && strcmp(s_msg[0], "TILT") &&
        strcmp(s_msg[0], "EXTRA BALL") && strcmp(s_msg[0], "SKILL SHOT")) {
        return;
    }
    if (fmodf(s_msg_t, 0.3f) < 0.08f) {
        return;
    }
    int scale = gfx_text_width(s_msg[0], TS(3)) <= SCR_W - 8 ? TS(3) : TS(2);
    int y = (APRON_Y + DMD_H) / 2 - K(20);
    gfx_darken_rect(0, y - K(6), SCR_W, 7 * scale + K(12), 1);
    gfx_text_center(y + 2, s_msg[0], scale, rgb(0, 0, 0));
    gfx_text_center(y, s_msg[0], scale, s_msg_c);
}

static void pin_render(int64_t now)
{
    (void)now;
    if (s_bg_ok && gfx_w == SCR_W && gfx_h == SCR_H) {
        memcpy(g_fb, s_bg, (size_t)SCR_W * SCR_H * sizeof(uint16_t));
    } else {
        draw_static();
    }
    draw_dynamic();
    draw_dmd();
    draw_apron();
    if (s_state == G_TITLE) {
        draw_title();
    } else if (s_state == G_OVER) {
        draw_over();
    } else {
        draw_message_on_table();
        if (s_state == G_BONUS) {
            char buf[24], b2[30];
            int y = (APRON_Y + DMD_H) / 2 - K(24);
            overlay_band(y, K(48));
            fmt_score(buf, sizeof(buf), s_bonus_shown);
            snprintf(b2, sizeof(b2), "BONUS X%d", s_mult);
            gfx_text_center(y + K(6), b2, TS(1), rgb(255, 200, 120));
            gfx_text_center(y + K(20), buf, TS(2), rgb(255, 240, 200));
        }
        ball_t *pb = plunger_ball();
        if (pb && s_state == G_PLAY && ball_count() == 1 && s_queue == 0 && !s_pulling && s_auto_t > AUTO_S - 3) {
            char b[12];
            int left = (int)(AUTO_S - s_auto_t) + 1;
            snprintf(b, sizeof(b), "%d", left < 0 ? 0 : (left > 9 ? 9 : left));
            gfx_text_center_in((int)s_shoot_x0 - 2, (int)(s_shoot_x1 - s_shoot_x0) + 4, (int)s_plunger_y - K(40), b,
                               TS(1), rgb(255, 230, 120));
        }
        if (s_pulling) {
            // the plunger power
            int x = (int)s_shoot_x0 - K(6), h = K(60), y = (int)s_plunger_y - h - K(10);
            gfx_fill_rect(x, y, K(4), h, rgb(30, 20, 20));
            int p = (int)(h * s_plunger);
            gfx_fill_rect(x, y + h - p, K(4), p, s_plunger > 0.9f ? rgb(255, 60, 60) : rgb(255, 200, 60));
        }
    }
}

// ---------------------------------------------------------------- the menu icon

// ---------------------------------------------------------------- the menu icon (smooth shapes of tfdraw)
#include "tfdraw/tfdraw.h"

// the way of the icon ball in a loop of 2 s: from the left flipper up to the three bumpers and down to the right
// flipper and back (the points of the way, the time of each one)
static const float PB_W[7][3] = {{30, 82, 0},    {32, 34, 0.35f}, {66, 30, 0.6f}, {50, 50, 0.85f},
                                 {70, 82, 1.2f}, {46, 64, 1.55f}, {30, 82, 2.0f}};
static void pin_icon_ball(float p, float *bx, float *by, int *hit)
{
    *hit = -1;
    for (int k = 0; k < 6; k++) {
        if (p <= PB_W[k + 1][2]) {
            float f = (p - PB_W[k][2]) / (PB_W[k + 1][2] - PB_W[k][2]);
            *bx = PB_W[k][0] + (PB_W[k + 1][0] - PB_W[k][0]) * f;
            *by = PB_W[k][1] + (PB_W[k + 1][1] - PB_W[k][1]) * f;
            if (f < 0.3f) {
                *hit = k;
            }
            return;
        }
    }
    *bx = PB_W[0][0];
    *by = PB_W[0][1];
}
// THE ICON: the pinball table. The silver ball goes from the left flipper up into the three bumpers, which flash and
// bounce it, then down to the right flipper, which sends it back. A loop of 2 s.
static void pin_icon(int x, int y, int s)
{
    static const float BUMP[3][2] = {{32, 30}, {68, 26}, {50, 46}};
    static const uint8_t BC[3][3] = {{255, 70, 150}, {60, 220, 255}, {255, 210, 50}};
    float t = icon_time(), u = s / 100.0f, p = fmodf(t, 2.0f);
    float bx, by;
    int hit;
    pin_icon_ball(p, &bx, &by, &hit);
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
    // (flat bands, not a smooth gradient: few colours for the icon file of the store)
    for (int k = 0; k < 6; k++) {
        tfd_rect(x, y + (s)*k / 6, s, (s) / 6 + 1, tfd_mix(rgb(40, 24, 90), rgb(90, 20, 60), k * 51), 256);
    }
    // the walls and the lanes in neon
    for (int sd = 0; sd < 2; sd++) {
        float wx = sd ? 92 : 8;
        tfd_rect((int)(x + (wx - 1) * u), (int)(y + 10 * u), (int)(2 * u), (int)(62 * u), rgb(160, 140, 255), 256);
        tfd_line(x + wx * u, y + 72 * u, x + (sd ? 72 : 28) * u, y + 86 * u, 2 * u, rgb(160, 140, 255), 256);
    }
    tfd_rrect((int)(x + 8 * u), (int)(y + 6 * u), (int)(84 * u), (int)(8 * u), (int)(4 * u), rgb(160, 140, 255), 256);
    tfd_rrect((int)(x + 10 * u), (int)(y + 8 * u), (int)(80 * u), (int)(8 * u), (int)(4 * u), rgb(40, 24, 90), 256);
    // the bumpers: a ring, a light top; the one that the ball hits flashes and grows
    for (int k = 0; k < 3; k++) {
        float fl = hit - 1 == k ? 1 : 0;
        float cx = x + BUMP[k][0] * u, cy = y + BUMP[k][1] * u, r = (8 + fl * 1.5f) * u;
        uint16_t c = tfd_rgb(BC[k][0], BC[k][1], BC[k][2]);
        if (fl > 0) {
            tfd_disc(cx, cy, r + 5 * u, c, 110);
        }
        tfd_disc(cx, cy, r + 2 * u, tfd_rgb(BC[k][0] / 3, BC[k][1] / 3, BC[k][2] / 3), 256);
        tfd_disc(cx, cy, r, c, 256);
        tfd_disc(cx, cy, r * 0.6f, fl > 0 ? rgb(255, 255, 255) : rgb(250, 240, 250), 256);
        tfd_disc(cx, cy, r * 0.3f, c, 256);
    }
    // the flippers: up when the ball comes to them
    for (int sd = 0; sd < 2; sd++) {
        bool up = sd ? (p > 1.1f && p < 1.35f) : (p < 0.2f || p > 1.9f);
        float px = x + (sd ? 72 : 28) * u, py = y + 86 * u, a = sd ? (up ? -2.6f : 2.75f) : (up ? -0.55f : 0.4f);
        float ex = px + fcos(a) * 18 * u, ey = py + fsin(a) * 18 * u;
        tfd_line(px, py, ex, ey, 5 * u, rgb(250, 250, 250), 256);
        tfd_line(px, py, ex, ey, 2.4f * u, rgb(230, 50, 60), 256);
        tfd_disc(px, py, 3.4f * u, rgb(250, 250, 250), 256);
    }
    // the ball: its trail and the shine of the steel
    for (int k = 3; k >= 1; k--) {
        float gx, gy;
        int h2;
        pin_icon_ball(fmodf(p + 2 - k * 0.04f, 2.0f), &gx, &gy, &h2);
        tfd_disc(x + gx * u, y + gy * u, (3.4f - k * 0.6f) * u, rgb(200, 200, 230), 120 - k * 30);
    }
    tfd_disc(x + bx * u, y + by * u, 4 * u, rgb(110, 116, 140), 256);
    tfd_disc(x + bx * u - 0.4f * u, y + by * u - 0.4f * u, 3.4f * u, rgb(210, 214, 230), 256);
    tfd_disc(x + bx * u - 1.3f * u, y + by * u - 1.4f * u, 1.3f * u, rgb(255, 255, 255), 256);
    tfd_noclip();
}

const app_t app_pinball = {
        .name = "PINBALL",
        .landscape = false,
        .round = APP_ROUND_HI,  // (the round boards: the round layout, see BOARD_ROUND above)
        .needs = 0,             // touch only (the nudge needs the IMU, the game does not)
        .init = pin_init,
        .enter = pin_enter,
        .leave = pin_leave,
        .input = pin_input,
        .update = pin_update,
        .render = pin_render,
        .draw_icon = pin_icon,
};
