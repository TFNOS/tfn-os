// SUMMER CUP (GAMES): a physics game of summer events on a California coast (scooter tricks, surfing and more). Made for the round AMOLED-1.75C first (412 x 412, only the
// circle shows), with layouts for the 3.49 bar (640 x 172) and the 320 x 240 frame.
// One body and one physics in three events, each with its own core rule:
//   THE BOWL (scooter): the pump. Stand up at the bottom of the bowl = more speed (the work of the legs against the
//                       push of the bowl); crouch high on the wall costs little. The air: a swipe up throws a flip, a
//                       hold tucks (a faster spin), the release opens (a slower spin); the wheels must meet the wall.
//   THE PEAK (surf):    the wave in the picture is a slice of a wave that breaks along its length; the speed of the
//                       surfer along the wave chooses the stage of the break in the slice (slow: the lip falls on the
//                       surfer; fast: a flat shoulder). The face lifts the board, a drop down the face gives speed.
//                       Hold (crouch) in the barrel = the tube.
//   SKIM (skimboard):   thin water is fast. A tap drops the board; it glides only on a thin film of water (dry sand
//                       stops it, deep water plows); the next small wave is the ramp of a launch.
// MANUAL.md "SUMMER CUP".
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tfn/app.h"
#ifndef APP_ROUND_HI
#define APP_ROUND_HI 2  // (app_t.round = 2 means a round layout; an older main has a bool there)
#endif
#include "tfn/audio.h"
#include "jingle/jingle.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "midi_bg/midi_bg.h"
#include "sc_music.h"
#include "tfsfx/tfsfx.h"
#include "sc_sfx.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static const char *TAG = "summer";
#define SW     gfx_w
#define SH     gfx_h
#define SC_KEY "summer_"  // (the manifest: saves=summer_)
#define GRAV   9.81f

static int s_lang;
#define TR(en, he) (s_lang ? (he) : (en))

#include "sc_gfx.inc"
#include "sc_scene.inc"
#include "sc_body.inc"

// ---------------------------------------------------------------- the sound effects (MP3 in sc_sfx.c, libs/tfsfx)
// (the sources: apps/summer/sfx/SOURCES.md)
enum {
    FX_SPLASH,
    FX_SPLASH2,
    FX_WHOOSH,
    FX_CLANK,
    FX_THUD,
    FX_SLAP,
    FX_MEDAL,
    FX_GRIND,
    FX_POP,
    FX_N
};
static const char *const FXN[FX_N] = {"splash", "splash2", "whoosh", "clank", "thud", "slap", "medal", "grind", "pop"};
static bool s_demo;  // the title: the game plays itself behind the menu (quiet: only the music)
static void fx(int id, int vol)
{
    if (!s_demo) {
        tfsfx_play_var(id, vol, 0.07f);
    }
}
static void fx_rate(int id, int vol, float rate)
{
    if (!s_demo) {
        tfsfx_play(id, vol, rate);
    }
}
// the noise of the wheels (ch 0) and of the sea (ch 1): quiet in the demo
static void drone(int ch, float hz, int vol)
{
    audio_drone(ch, hz, s_demo ? 0 : vol, W_NOISE);
}

// ---------------------------------------------------------------- the music (sc_music.c, libs/midi_bg)
static bool s_music = true;
static int s_mus_track = -1;
static float s_mus_back_t;
static int s_mus_after = -1;
static void mus_loop(int tr)
{
    if (!SC_HAVE_MUSIC || !s_music || s_mus_track == tr) {
        return;
    }
    s_mus_track = tr;
    s_mus_back_t = 0;
    midi_bg_play_track(tr, 0, true);
}
static void mus_jingle(int j, int ms, int after)
{
    if (!SC_HAVE_MUSIC || !s_music) {
        return;
    }
    midi_bg_play_track(j, 0, false);
    s_mus_track = -1;
    s_mus_back_t = ms / 1000.0f;
    s_mus_after = after;
}
static void mus_step(float dt)
{
    if (SC_HAVE_MUSIC && s_music) {
        midi_bg_service();
    }
    if (s_mus_back_t > 0 && (s_mus_back_t -= dt) <= 0) {
        if (s_mus_after >= 0) {
            mus_loop(s_mus_after);
        } else if (SC_HAVE_MUSIC) {
            midi_bg_stop(300);
        }
    }
}

// ---------------------------------------------------------------- the state
enum {
    EV_BOWL,
    EV_PEAK,
    EV_SKIM,
    EV_N
};
enum {
    ST_TITLE,
    ST_INTRO,
    ST_PLAY,
    ST_RESULT,
    ST_CUPEND
};
static int s_st;    // ST_*
static int s_ev;    // the event now
static bool s_cup;  // the three events in a row
static int s_cup_sc[EV_N], s_cup_med[EV_N];
static float s_st_t;  // the time in the state (real s)
static int64_t s_last;
static float s_score;           // the score of the event now
static float s_ev_t, s_ev_len;  // the time of the event and its length (game s)
static int s_demo_ev;
static uint32_t s_best[EV_N], s_med[EV_N], s_best_cup, s_med_cup;
static int s_new_med;  // the medal of the last event (0 none, 1 bronze, 2 silver, 3 gold)
static bool s_new_best;
static bool s_tilt_on = true;               // the panel: TILT (no sensor: the game leans by itself)
static float s_tscale = 1, s_tscale_t = 1;  // the slow motion (now, the aim)
static float s_slow_hold;                   // the time that the aim stays slow (real s)

// the medal limits of each event (made with test bots)
static const int MEDAL[EV_N][3] = {
        {8000, 22000, 40000},  // THE BOWL
        {6000, 15000, 24000},  // THE PEAK
        {3000, 7000, 11500},   // SKIM
};
static int medal_of(int ev, float sc)
{
    int m = 0;
    for (int k = 0; k < 3; k++) {
        if (sc >= MEDAL[ev][k]) {
            m = k + 1;
        }
    }
    return m;
}

// ---------------------------------------------------------------- the input (all events)
static bool s_down;  // a finger is on the glass
static float s_dx0, s_dy0, s_lx, s_ly;
static int64_t s_down_ms;
static bool s_swiped;
static int s_swipe;            // a swipe to use: 1 up, 2 left, 3 right, 4 down (the event takes it)
static bool s_press;           // a new touch to use (the event takes it)
static float s_lean;           // -1..1: the tilt (or the auto lean)
static float s_swipe_px = 50;  // the travel of a swipe (about 5 mm on each board)
// the virtual input of the demo and of the bots: the same paths as the touch
static bool s_ai_hold;

static void in_down(float x, float y, int64_t t)
{
    s_down = true;
    s_dx0 = s_lx = x;
    s_dy0 = s_ly = y;
    s_down_ms = t;
    s_swiped = false;
    s_press = true;
}
static void in_move(float x, float y)
{
    s_lx = x;
    s_ly = y;
    if (!s_down || s_swiped) {
        return;
    }
    float dx = x - s_dx0, dy = y - s_dy0;
    if (dx * dx + dy * dy < s_swipe_px * s_swipe_px) {
        return;
    }
    s_swiped = true;
    if (fabsf(dy) >= fabsf(dx)) {
        s_swipe = dy < 0 ? 1 : 4;
    } else {
        s_swipe = dx < 0 ? 2 : 3;
    }
}
static void in_up(void)
{
    s_down = false;
}
static inline bool hold_now(void)
{
    return s_down || s_ai_hold;
}

// ---------------------------------------------------------------- the camera with springs, the shake
static float s_ctx, s_cty, s_ctz = 40;  // the aims of the camera
static float s_cvx, s_cvy;              // (the speed of the camera)
static float s_shake;                   // the shake (px), it falls
static float s_zb = 40;                 // the base zoom of the layout (px of a metre)
static void cam_snap(void)
{
    cam_x = s_ctx;
    cam_y = s_cty;
    cam_z = s_ctz;
    s_cvx = s_cvy = 0;
}
static void cam_step(float dt)
{
    // a critically damped spring for x and y (soft start and stop), a first order step for the zoom
    float k = 9.0f, d = 2 * 3.0f;
    s_cvx += ((s_ctx - cam_x) * k - s_cvx * d) * dt;
    s_cvy += ((s_cty - cam_y) * k - s_cvy * d) * dt;
    cam_x += s_cvx * dt;
    cam_y += s_cvy * dt;
    float a = dt * 2.6f;
    if (a > 1) {
        a = 1;
    }
    cam_z += (s_ctz - cam_z) * a;
    s_shake *= 1 - fminf(1, dt * 5);
    s_shx = s_shake * fsin(s_t * 63);
    s_shy = s_shake * fcos(s_t * 51);
}
static void shake(float px)
{
    if (s_shake < px) {
        s_shake = px;
    }
}
static void slowmo(float scale, float hold)
{
    s_tscale_t = scale;
    if (s_slow_hold < hold) {
        s_slow_hold = hold;
    }
}

// ---------------------------------------------------------------- particles (spray, foam, sand, sparks)
enum {
    PK_SPRAY,
    PK_FOAM,
    PK_SAND,
    PK_SPARK,
    PK_DUST
};
typedef struct {
    float x, y, vx, vy, life, max, r;
    uint8_t kind;
} part_t;
#define NPART 220
static part_t s_part[NPART];
static int s_npart;
static void part_add(int kind, float x, float y, float vx, float vy, float life, float r)
{
    if (s_npart >= NPART) {
        return;
    }
    part_t *p = &s_part[s_npart++];
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->life = p->max = life;
    p->r = r;
    p->kind = (uint8_t)kind;
}
static void parts_step(float dt)
{
    for (int i = 0; i < s_npart; i++) {
        part_t *p = &s_part[i];
        p->life -= dt;
        if (p->life <= 0) {
            s_part[i--] = s_part[--s_npart];
            continue;
        }
        float g = p->kind == PK_FOAM ? 0.6f : p->kind == PK_DUST ? -0.2f : GRAV;
        float drag = p->kind == PK_FOAM ? 2.5f : p->kind == PK_DUST ? 1.5f : 0.4f;
        p->vx -= p->vx * drag * dt;
        p->vy -= g * dt + p->vy * drag * dt * 0.5f;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
    }
}
static void parts_draw(void)
{
    for (int i = 0; i < s_npart; i++) {
        part_t *p = &s_part[i];
        float f = p->life / p->max;
        int a = (int)(256 * (f < 0.3f ? f / 0.3f : 1));
        uint16_t c;
        switch (p->kind) {
        case PK_SPRAY:
            c = rgb(236, 250, 255);
            a = a * 3 / 4;
            break;
        case PK_FOAM:
            c = rgb(250, 252, 255);
            a = a * 3 / 4;
            break;
        case PK_SAND:
            c = rgb(214, 184, 140);
            break;
        case PK_SPARK:
            c = rgb(255, 230, 140);
            break;
        default:
            c = rgb(220, 214, 200);
            a /= 2;
            break;
        }
        float r = p->r * (p->kind == PK_DUST ? 2 - f : 1);
        wdisc(p->x, p->y, r, c, a);
    }
}

// ---------------------------------------------------------------- the trick text and the points that float up
static char s_tk_txt[2][48];
static float s_tk_t;  // the time of the trick text (it shows while > 0)
static uint16_t s_tk_col;
static int s_tk_pts;
static void trick_say(const char *l1, const char *l2, int pts, uint16_t c)
{
    snprintf(s_tk_txt[0], sizeof s_tk_txt[0], "%s", l1);
    snprintf(s_tk_txt[1], sizeof s_tk_txt[1], "%s", l2 ? l2 : "");
    s_tk_t = 2.2f;
    s_tk_col = c;
    s_tk_pts = pts;
#ifdef SC_DEBUG
    if (!s_demo) {
        printf("  [ev %d %.1f s] %s / %s %d\n", s_ev, s_ev_t, l1, l2 ? l2 : "", pts);
    }
#endif
}
static void add_score(float pts)
{
    s_score += pts;
}

// ---------------------------------------------------------------- the air (all events)
// The body spins round its centre of mass with the angular momentum L (in the units of the open body): w = L / I, and
// I falls to 0.32 in the tuck. A swipe up throws a flip (more L); the lean adds a small torque. A swipe to the side
// turns the deck or the board (one full turn); it must be back under the feet at the landing.
typedef struct {
    bool on;
    float t, ang0, ang, w, L, tuck, back, y0, ymax, vx, vy, x, y;
    int throws, whips;
    float whip, whip_v;
} air_t;
static air_t A;
#define FLIP_L 3.7f
static void air_start(float x, float y, float vx, float vy, float ang, float w0, float back)
{
    memset(&A, 0, sizeof A);
    A.on = true;
    A.x = x;
    A.y = y;
    A.vx = vx;
    A.vy = vy;
    A.ang0 = A.ang = ang;
    A.L = w0;
    A.back = back;
    A.y0 = A.ymax = y;
    fx_rate(FX_WHOOSH, 45, 0.9f);
}
static void air_step(float dt, float lean)
{
    float tt = hold_now() ? 1 : 0;
    A.tuck += (tt - A.tuck) * fminf(1, dt * 10);
    if (s_swipe == 1 && A.throws < 2) {
        A.L += A.back * (A.throws ? FLIP_L * 0.7f : FLIP_L);
        A.throws++;
        fx_rate(FX_WHOOSH, 60, 1.2f);
    }
    if ((s_swipe == 2 || s_swipe == 3) && fabsf(A.whip_v) < 0.1f) {
        A.whip_v = (s_swipe == 3 ? 1 : -1) * 2 * (float)M_PI / 0.40f;
        fx_rate(FX_WHOOSH, 40, 1.6f);
    }
    s_swipe = 0;
    A.L += lean * 2.4f * dt;
    float I = 1 - 0.68f * A.tuck;
    A.w = A.L / I;
    A.ang += A.w * dt;
    if (A.whip_v != 0) {
        float before = A.whip;
        A.whip += A.whip_v * dt;
        float turn = 2 * (float)M_PI;
        if (fabsf(A.whip) >= turn * (float)(A.whips + 1) && fabsf(before) < turn * (float)(A.whips + 1)) {
            A.whips++;
            A.whip = 0;
            A.whip_v = 0;
        }
    }
    A.t += dt;
    if (A.y > A.ymax) {
        A.ymax = A.y;
    }
}
// the result of a landing on a slope whose normal has the angle target
enum {
    LAND_CLEAN,
    LAND_SKETCHY,
    LAND_FALL
};
static float s_land_err;
static int air_land(float target)
{
    float err = wrap_pi(A.ang - target);
    float wh = fabsf(A.whip);
    s_land_err = err;
    A.on = false;
    if (wh > 0.9f) {
        return LAND_FALL;  // (the deck is not under the feet)
    }
    if (fabsf(err) < 0.52f && wh < 0.35f) {
        return LAND_CLEAN;
    }
    if (fabsf(err) < 0.96f) {
        return LAND_SKETCHY;
    }
    return LAND_FALL;
}
// the name and the points of the air that ended now (land = LAND_*); base = the points of the height or the speed
static int air_score(int land, float base, const char *board_trick_en, const char *board_trick_he)
{
    float rot = (A.ang - A.ang0) * (A.back >= 0 ? 1 : -1);
    int flips = (int)floorf(rot / (2 * (float)M_PI) + 0.5f);
    char l1[48] = "";
    int pts = (int)base;
    if (flips >= 1 || flips <= -1) {
        int n = flips < 0 ? -flips : flips;
        static const int FP[4] = {0, 1000, 2600, 5000};
        pts += FP[n > 3 ? 3 : n];
        if (flips > 0) {
            snprintf(l1, sizeof l1, "%s",
                     n == 1   ? TR("BACKFLIP", "סלטה אחורית")
                     : n == 2 ? TR("DOUBLE BACKFLIP", "סלטה כפולה")
                              : TR("TRIPLE BACKFLIP", "סלטה משולשת"));
        } else {
            snprintf(l1, sizeof l1, "%s",
                     n == 1 ? TR("FRONTFLIP", "סלטה קדמית") : TR("DOUBLE FRONTFLIP", "סלטה קדמית כפולה"));
        }
    }
    if (A.whips > 0) {
        static const int WP[4] = {0, 500, 1200, 2000};
        pts += WP[A.whips > 3 ? 3 : A.whips];
        char w[32];
        if (A.whips == 1) {
            snprintf(w, sizeof w, "%s", TR(board_trick_en, board_trick_he));
        } else {
            snprintf(w, sizeof w, "%s x%d", TR(board_trick_en, board_trick_he), A.whips);
        }
        if (l1[0]) {
            size_t n = strlen(l1);
            snprintf(l1 + n, sizeof l1 - n, " + %s", w);
            pts = pts * 13 / 10;
        } else {
            snprintf(l1, sizeof l1, "%s", w);
        }
    }
    if (!l1[0]) {
        snprintf(l1, sizeof l1, "%s", TR("AIR", "אוויר"));
    }
    const char *l2;
    uint16_t c;
    if (land == LAND_CLEAN) {
        pts = pts * 3 / 2;
        l2 = TR("CLEAN", "נקי");
        c = rgb(255, 230, 90);
    } else if (land == LAND_SKETCHY) {
        pts = pts * 7 / 10;
        l2 = TR("SKETCHY", "כמעט");
        c = rgb(255, 170, 110);
    } else {
        pts = 0;
        l2 = TR("WIPEOUT", "נפילה");
        c = rgb(255, 110, 100);
    }
    trick_say(l1, l2, pts, c);
    add_score((float)pts);
    return pts;
}

// the AI of the demo (and of the PC bots: the same logic, through the real input): what to do now
typedef struct {
    bool hold;
    int swipe;
    float lean;
    bool press;
} ai_t;
static ai_t s_ai;
static float s_ai_err;   // the timing error of the AI (s): 0 = good, about 0.1 = a naive person
static float s_ai_rand;  // a random value for this air (0..1)

#include "sc_bowl.inc"
#include "sc_peak.inc"
#include "sc_skim.inc"

// ---------------------------------------------------------------- the dispatch of the events
static void ev_start(int ev)
{
    s_ev = ev;
    s_score = 0;
    s_ev_t = 0;
    s_npart = 0;
    s_tk_t = 0;
    s_rag.on = false;
    s_swipe = 0;
    s_press = false;
    s_tscale = s_tscale_t = 1;
    s_slow_hold = 0;
    s_shake = 0;
    memset(&A, 0, sizeof A);
    if (ev == EV_BOWL) {
        bowl_start();
    } else if (ev == EV_PEAK) {
        peak_start();
    } else {
        skim_start();
    }
    cam_snap();
    if (!s_demo && imu_ok()) {
        imu_set_level();
    }
}
static void ev_step(float dt)
{
    if (s_ev == EV_BOWL) {
        bowl_step(dt);
    } else if (s_ev == EV_PEAK) {
        peak_step(dt);
    } else {
        skim_step(dt);
    }
}
static void ev_render(void)
{
    if (s_ev == EV_BOWL) {
        bowl_render();
    } else if (s_ev == EV_PEAK) {
        peak_render();
    } else {
        skim_render();
    }
}
static bool ev_over(void)
{
    if (s_ev == EV_BOWL) {
        return bowl_over();
    }
    if (s_ev == EV_PEAK) {
        return peak_over();
    }
    return skim_over();
}
static void ev_ai(void)
{
    memset(&s_ai, 0, sizeof s_ai);
    if (s_ev == EV_BOWL) {
        bowl_ai(&s_ai);
    } else if (s_ev == EV_PEAK) {
        peak_ai(&s_ai);
    } else {
        skim_ai(&s_ai);
    }
}

#include "sc_ui.inc"
