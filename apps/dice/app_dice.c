// DICE (UTIL): a dice app that the user shakes or taps to throw.
// The task: the app task of TFN OS calls the hooks of app_dice; the sounds load in the task of libs/tfsfx.
// The memory: the file statics; the background picture s_bg (PSRAM) comes at enter and goes at leave.
//
// Three modes: ONE DIE (a d6), TWO DICE (2d6 and their sum), D&D (the set d4, d6, d8, d10, d12, d20 and d100 =
// two d10: the tens and the ones). In D&D the user makes the throw with big chips: a tap adds one die of that kind, a
// hold removes one, CLEAR removes all. A tap (or a flick) on the tray or a shake of the board throws the dice.
// THE LOOK: each kind has its true solid (dz_solid.inc: a tetrahedron, a cube, an octahedron, a pentagonal
// trapezohedron, a dodecahedron, an icosahedron), shaded, with soft edges and the number on its faces; the dice jump,
// tumble, bounce on the felt and off the walls and off each other, and roll to a stop. A natural 20 on a d20 gets
// confetti and a gold glow; a natural 1 turns the die grey.
// FAIR: esp_random() gives each result before the dice move (all faces have the same chance). Then the throw runs two
// times: first fast and hidden (the motion with no result), then on the screen with a small extra turn, spread over
// the first 60 % of the flight, so that the die stops with its result on top.
// THE SOUNDS: 9 CC0 MP3 effects in the app file (dz_sfx.c, sfx/SOURCES.md; libs/tfsfx).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfdraw/tfdraw.h"
#include "tfsfx/tfsfx.h"
#include "dz_sfx.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include "dz_solid.inc"

// ---------------------------------------------------------------- the kinds of dice
enum {
    M_ONE,
    M_TWO,
    M_DND,
    M_N
};
enum {
    K4,
    K6,
    K8,
    K10,
    K12,
    K20,
    K100,
    KN
};
static const uint8_t K_SO[KN] = {SO_D4, SO_D6, SO_D8, SO_D10, SO_D12, SO_D20, SO_D10};
static const uint8_t K_SIDES[KN] = {4, 6, 8, 10, 12, 20, 100};
// the colours: index KN = the tens die of a d100 (ivory), KN + 1 = its ones die (charcoal)
static const uint8_t K_COL[KN + 2][3] = {
        {24, 196, 250}, {226, 38, 52},   {52, 96, 236},   {146, 64, 226}, {246, 128, 20},
        {232, 40, 140}, {236, 226, 204}, {236, 226, 204}, {58, 58, 72},
};
// the size of each kind (a d4 is a small solid: it gets more size)
static const float K_SIZE[KN] = {1.18f, 0.92f, 1.0f, 1.0f, 1.0f, 1.06f, 0.95f};
#define MAXD 12  // the most dice on the tray (a d100 is two)
#define MAXK 9   // the most dice of one kind in the D&D set

typedef struct {
    look_t lk;
    uint8_t kind;  // K*
    uint8_t part;  // 0; a d100: 1 = the tens die, 2 = the ones die
    uint8_t val;   // the number on top (1..faces)
    bool moving, nospin, nat20, nat1;
    float x, y, h, vx, vy, vh;  // the place on the tray (px), the height over the felt (px), the speeds
    v3 w;                       // the turn speed (rad/s, the world: x right, y up, z to the camera)
    quat q0, rot, corr, qt, q;  // the start, the turn of the throw, the extra turn, the end, the turn now
    float t, tend, r;           // the time in the throw, the time of the stop (the first run), the radius px
    float ax, ay, gx, gy;       // the tidy move after a throw: from, to
} die_t;
static die_t s_dice[MAXD];
static int s_nd;

// ---------------------------------------------------------------- the layout of each board kind
typedef struct {
    int16_t x, y, w, h;
} rc_t;
typedef struct {
    rc_t tab[M_N], tray, tray_dd, pan, pan_dd, chip[KN + 1];
    bool side;     // the result panel at the side (else a bar over the tray)
    bool round;    // the round panel: a round tray; the tabs, the chips and the history are discs on the arcs
    rc_t hist[3];  // (round) the discs of the last results at the right arc
    const kfont_t *f_tab, *f_hint, *f_spec, *f_small;
    const mfont_t *f_hist, *f_chip, *f_badge;
    float tot_k;  // the scale of the big total (1 = the caps of MF_N34, 35 px)
    int tab_r, badge_r;
} lay_t;
static lay_t s_ly;
static inline bool in_rc(const rc_t *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}
static inline rc_t mk_rc(int x, int y, int w, int h)
{
    rc_t r = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    return r;
}

// the box of a disc of radius r at the angle a (degrees; 0 = right, 90 = down) on a ring of radius rr round the centre
static rc_t ring_rc(float a, float rr, float r)
{
    float x = gfx_w * 0.5f + rr * fcos(a * 0.017453f), y = gfx_h * 0.5f + rr * fsin(a * 0.017453f);
    return mk_rc((int)(x - r), (int)(y - r), (int)(2 * r), (int)(2 * r));
}
// the round panel (the 1.75C 466 x 466, the Watcher 412): a round felt tray in the middle; the 3 modes are discs on
// the left arc, the last 3 results discs on the right arc, the total in the top cap, the D&D chips on the bottom arc
static void lay_round(void)
{
    float k = gfx_w / 466.0f;
    for (int i = 0; i < M_N; i++) {
        s_ly.tab[i] = ring_rc(235 - 30 * i, 190 * k, 34 * k);
    }
    for (int i = 0; i < 3; i++) {
        s_ly.hist[i] = ring_rc(-55 + 30 * i, 190 * k, 30 * k);
    }
    int tr = (int)(145 * k), dr = (int)(118 * k);
    s_ly.tray = mk_rc(gfx_w / 2 - tr, (int)(262 * k) - tr, 2 * tr, 2 * tr);
    s_ly.tray_dd = mk_rc(gfx_w / 2 - dr, (int)(222 * k) - dr, 2 * dr, 2 * dr);
    s_ly.pan = mk_rc(gfx_w / 2 - (int)(100 * k), (int)(22 * k), (int)(200 * k), (int)(78 * k));
    s_ly.pan_dd = s_ly.pan;
    for (int i = 0; i <= KN; i++) {
        s_ly.chip[i] = ring_rc(150 - i * (120.0f / KN), 190 * k, 27 * k);
    }
    s_ly.round = true;
    s_ly.f_tab = &KF_NAME;
    s_ly.f_hint = &KF_BIG;
    s_ly.f_spec = &KF_NAME;
    s_ly.f_small = &KF_LABEL;
    s_ly.f_hist = &MF_N20;
    s_ly.f_chip = &MF_N16;
    s_ly.f_badge = &MF_LABEL;
    s_ly.tot_k = 1.2f * k;
    s_ly.tab_r = (int)(19 * k);
    s_ly.badge_r = 10;
}

static void lay_init(void)
{
    memset(&s_ly, 0, sizeof s_ly);
    if (gfx_round()) {
        lay_round();
        return;
    }
#if BOARD_BAR_SCREEN
    // the 3.49 (640 x 172): the modes at the left, the tray, the result and the history at the right; the D&D chips
    // in a row under the tray
    for (int i = 0; i < M_N; i++) {
        s_ly.tab[i] = mk_rc(4, 4 + i * 56, 104, 52);
    }
    s_ly.tray = mk_rc(112, 4, 394, 164);
    s_ly.tray_dd = mk_rc(112, 4, 394, 116);
    s_ly.pan = mk_rc(510, 4, 126, 164);
    s_ly.pan_dd = mk_rc(510, 4, 126, 116);
    for (int i = 0; i <= KN; i++) {
        s_ly.chip[i] = mk_rc(112 + i * 66, 124, 62, 44);
    }
    s_ly.side = true;
    s_ly.f_tab = &KF_TEXT;
    s_ly.f_hint = &KF_TEXT;
    s_ly.f_spec = &KF_NAME;
    s_ly.f_small = &KF_LABEL;
    s_ly.f_hist = &MF_N16;
    s_ly.f_chip = &MF_N16;
    s_ly.f_badge = &MF_LABEL;
    s_ly.tot_k = 1.0f;
    s_ly.tab_r = 14;
    s_ly.badge_r = 10;
#elif BOARD_UI_TABLET
    // the Tab5 (640 x 360): the same plan as the 3.49, bigger
    for (int i = 0; i < M_N; i++) {
        s_ly.tab[i] = mk_rc(8, 8 + i * 116, 116, 110);
    }
    s_ly.tray = mk_rc(132, 8, 364, 344);
    s_ly.tray_dd = mk_rc(132, 8, 364, 262);
    s_ly.pan = mk_rc(504, 8, 128, 344);
    s_ly.pan_dd = mk_rc(504, 8, 128, 262);
    for (int i = 0; i <= KN; i++) {
        s_ly.chip[i] = mk_rc(132 + i * 63, 278, 59, 74);
    }
    s_ly.side = true;
    s_ly.f_tab = &KF_BIG;
    s_ly.f_hint = &KF_BIG;
    s_ly.f_spec = &KF_TEXT;
    s_ly.f_small = &KF_NAME;
    s_ly.f_hist = &MF_N20;
    s_ly.f_chip = &MF_N20;
    s_ly.f_badge = &MF_N16;
    s_ly.tot_k = 1.7f;
    s_ly.tab_r = 28;
    s_ly.badge_r = 13;
#else
    // the 320 x 240 frame (CoreS3, AMOLED x1.4, Watcher round_fit): the modes at the left, a result bar over the
    // tray; the D&D chips in two rows of 4 under the tray
    for (int i = 0; i < M_N; i++) {
        s_ly.tab[i] = mk_rc(4, 4 + i * 78, 62, 74);
    }
    s_ly.pan = mk_rc(70, 4, 246, 38);
    s_ly.pan_dd = s_ly.pan;
    s_ly.tray = mk_rc(70, 46, 246, 190);
    s_ly.tray_dd = mk_rc(70, 46, 246, 96);
    for (int i = 0; i <= KN; i++) {
        s_ly.chip[i] = mk_rc(70 + (i % 4) * 62, 146 + (i / 4) * 47, 58, 44);
    }
    s_ly.side = false;
    s_ly.f_tab = &KF_NAME;
    s_ly.f_hint = &KF_TEXT;
    s_ly.f_spec = &KF_NAME;
    s_ly.f_small = &KF_LABEL;
    s_ly.f_hist = &MF_N16;
    s_ly.f_chip = &MF_N16;
    s_ly.f_badge = &MF_LABEL;
    s_ly.tot_k = 0.8f;
    s_ly.tab_r = 15;
    s_ly.badge_r = 10;
#endif
}

// ---------------------------------------------------------------- the state
static int s_mode;
static uint8_t s_set[KN];  // the D&D set: the count of each kind
static bool s_he, s_snd = true, s_shake_on = true;
static uint16_t *s_bg;  // the still picture (the background, the tray), PSRAM
static int s_bg_mode = -1;
static int64_t s_last, s_idle_t, s_set_dirty;
static uint32_t s_rng;  // the random numbers of the motion (not of the results)
static float s_acc;     // the time of the motion that is not run yet
static bool s_throwing, s_rattle;
static bool s_res_keep;  // the result before a rattle (a knock that is no shake gives it back)
static bool s_arr;       // small D&D dice: after the stop they go to a tidy grid (their numbers on tags)
static int64_t s_arr_t0;
#define ARR_WAIT 350
#define ARR_MS   420
static float s_rattle_t, s_calm_t, s_shake_snd_t, s_throw_t;
static int s_landed;  // the dice that landed in this throw (the first one plays the throw sound)
// the touch
static bool s_down, s_long_done;
static int s_dx, s_dy, s_zone;
static int64_t s_long_next;
enum {
    Z_NONE,
    Z_TRAY,
    Z_TAB = 10,
    Z_CHIP = 20
};
// the result and the history
typedef struct {
    int16_t total;
    uint8_t nat, nd;
    char spec[24];
    char parts[24];
} res_t;
#define HN 8
static res_t s_res, s_hist[HN];
static bool s_has_res;
static int s_nh;
static int64_t s_res_t;
static uint8_t s_tot_mask[48 * 160];
// the effects
static int64_t s_fx_t;
static int s_fx;  // 0, 20 (a natural 20) or 1 (a natural 1)
typedef struct {
    float x, y, vx, vy, a, va, life;
    uint16_t c;
} conf_t;
#define NCONF 44
static conf_t s_conf[NCONF];
static int64_t s_nudge_t;         // a tap with no dice in the D&D set: the chips pulse
static int64_t s_clr_t = -10000;  // CLEAR is armed (the first tap); a second tap in 2.5 s clears the set
#define CLR_MS 2500

static const char *T(const char *en, const char *he)
{
    return s_he ? he : en;
}
static float frnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (float)(s_rng >> 8) / 16777216.0f;
}
// a fair result 0..n-1 (no modulo bias)
static int fair(int n)
{
    uint32_t lim = 0xFFFFFFFFu - (0xFFFFFFFFu % (uint32_t)n);
    uint32_t v;
    do {
        v = esp_random();
    } while (v >= lim);
    return (int)(v % (uint32_t)n);
}

// ---------------------------------------------------------------- the sounds
enum {
    FX_SHAKE,
    FX_THROW,
    FX_DIE1,
    FX_DIE2,
    FX_ADD,
    FX_DEL,
    FX_TAB,
    FX_WIN,
    FX_LOSE,
    FX_N
};
static const char *const FXN[FX_N] = {"shake", "throw", "die1", "die2", "add", "del", "tab", "win", "lose"};
static void fx(int id, int vol, float var)
{
    if (s_snd) {
        tfsfx_play_var(id, vol, var);
    }
}

// ---------------------------------------------------------------- the dice of the mode
static const rc_t *tray(void)
{
    return s_mode == M_DND ? &s_ly.tray_dd : &s_ly.tray;
}

static int set_n(void)
{
    int n = 0;
    for (int k = 0; k < KN; k++) {
        n += s_set[k];
    }
    return n;
}
static int dnd_count(void)
{
    int n = 0;
    for (int k = 0; k < KN; k++) {
        n += s_set[k] * (k == K100 ? 2 : 1);
    }
    return n;
}
static int faces_of(const die_t *d)
{
    return s_sol[d->lk.so].nf;
}
static void die_look(die_t *d, int kind, int part)
{
    d->kind = (uint8_t)kind;
    d->part = (uint8_t)part;
    d->lk.so = K_SO[kind];
    d->lk.lset = kind == K6 ? LS_PIP : part == 1 ? LS_TENS : part == 2 ? LS_ONES : LS_NUM;
    d->lk.col = K_COL[part == 1 ? KN : part == 2 ? KN + 1 : kind];
    d->lk.ink = part == 1 ? rgb(40, 34, 52) : rgb(255, 255, 255);
    d->lk.tint = 0;
}
// the radius and the rest places of n dice in the tray: a grid with the biggest cells
static float grid(int n, float *px, float *py)
{
    const rc_t *t = tray();
    rc_t sq;
    if (s_ly.round) {  // (the square inside the round felt)
        int sd = t->w * 70 / 100;
        sq = mk_rc(t->x + (t->w - sd) / 2, t->y + (t->h - sd) / 2, sd, sd);
        t = &sq;
    }
    if (n <= 0) {
        return 10;
    }
    float best = 0;
    int bc = 1;
    for (int c = 1; c <= n; c++) {
        int rows = (n + c - 1) / c;
        float cell = fminf((float)t->w / c, (float)t->h / rows);
        if (cell > best) {
            best = cell;
            bc = c;
        }
    }
    int rows = (n + bc - 1) / bc;
    float cw = (float)t->w / bc, ch = (float)t->h / rows;
    for (int i = 0; i < n; i++) {
        int row = i / bc, col = i % bc, inrow = row == rows - 1 ? n - row * bc : bc;
        float x0 = t->x + (t->w - inrow * cw) / 2;
        px[i] = x0 + (col + 0.5f) * cw;
        py[i] = t->y + (row + 0.5f) * ch;
    }
    float r = best * 0.39f;
    if (r > t->h * 0.36f) {
        r = t->h * 0.36f;
    }
    return r;
}
// the dice of the D&D set (the big dice first: d100, d20, d12 ... d4); the count of the dice
static int set_kinds(int *kinds, int *parts)
{
    int n = 0;
    for (int k = KN - 1; k >= 0; k--) {
        for (int i = 0; i < s_set[k] && n < MAXD; i++) {
            if (k != K100) {
                kinds[n] = k;
                parts[n++] = 0;
                continue;
            }
            if (n + 2 > MAXD) {
                break;
            }
            kinds[n] = K100;
            parts[n++] = 1;
            kinds[n] = K100;
            parts[n++] = 2;
        }
    }
    return n;
}
// make the dice of the mode; keep the turn of the dice that stay; a new die falls in (drop = true)
static void dice_make(bool drop)
{
    static die_t old[MAXD];  // (static: not on the stack of the app task)
    int nold = s_nd;
    memcpy(old, s_dice, sizeof old);
    int kinds[MAXD], parts[MAXD], n = 0;
    if (s_mode == M_ONE) {
        kinds[n] = K6;
        parts[n++] = 0;
    } else if (s_mode == M_TWO) {
        kinds[n] = K6;
        parts[n++] = 0;
        kinds[n] = K6;
        parts[n++] = 0;
    } else {
        n = set_kinds(kinds, parts);
    }
    float px[MAXD], py[MAXD];
    float r = grid(n, px, py);
    bool used[MAXD] = {0};
    s_arr = false;
    for (int i = 0; i < n; i++) {
        die_t *d = &s_dice[i];
        // the same kind as a die of the last set: keep its turn (its number stays)
        int from = -1;
        for (int j = 0; j < nold; j++) {
            if (!used[j] && old[j].kind == kinds[i] && old[j].part == parts[i]) {
                from = j;
                break;
            }
        }
        memset(d, 0, sizeof *d);
        die_look(d, kinds[i], parts[i]);
        if (from >= 0) {
            used[from] = true;
            d->q = old[from].qt;
            d->val = old[from].val;
            d->lk.tint = old[from].lk.tint;
        } else {
            d->val = (uint8_t)(1 + fair(faces_of(d)));
            d->q = rest_of(d->lk.so, d->val, (frnd() - 0.5f) * 1.0f);
        }
        d->qt = d->q0 = d->q;
        d->rot = d->corr = Q_ONE;
        d->x = px[i] + (frnd() - 0.5f) * r * 0.25f;
        d->y = py[i] + (frnd() - 0.5f) * r * 0.25f;
        d->r = r * K_SIZE[kinds[i]];
        if (drop && from < 0) {
            // (a new die falls onto the felt and bounces, with no turn)
            d->h = r * 2.6f;
            d->moving = d->nospin = true;
        }
    }
    s_nd = n;
}

// ---------------------------------------------------------------- the motion
#define DT_SIM (1.0f / 120.0f)
#define G_K    55.0f  // the gravity: 55 radii a second a second
#define T_MAX  3.2f

static void bounce_snd(die_t *e, float imp)
{
    float k = imp / (e->r * 16.0f);
    if (k > 1) {
        k = 1;
    }
    if (k < 0.12f) {
        return;
    }
    int vol = (int)(18 + 70 * k);
    if (s_landed++ == 0 && s_nd > 1) {
        fx(FX_THROW, vol, 0.06f);
    } else {
        fx((s_rng >> 9) & 1 ? FX_DIE1 : FX_DIE2, vol, 0.10f);
    }
}

// the die hits the felt on an edge: the turn goes part of the way to a roll, with a kick
static void die_kick(die_t *e, float imp, uint32_t *rng)
{
    float R = e->r;
    v3 roll = v3m(e->vy / R, e->vx / R, e->w.z * 0.6f);
    e->w = v3add(v3mul(e->w, 0.45f), v3mul(roll, 0.55f));
    *rng = *rng * 1664525u + 1013904223u;
    float kx = (float)((*rng >> 8) & 1023) / 1023.0f - 0.5f;
    *rng = *rng * 1664525u + 1013904223u;
    float ky = (float)((*rng >> 8) & 1023) / 1023.0f - 0.5f;
    e->w.x += kx * imp / R * 0.9f;
    e->w.y += ky * imp / R * 0.9f;
}

// a die in the air: the gravity, and a bounce when it comes down onto the felt
static void die_fall(die_t *e, float dt, uint32_t *rng, bool live)
{
    float R = e->r;
    e->vh -= G_K * R * dt;
    e->h += e->vh * dt;
    if (!(e->h <= 0)) {
        return;
    }
    e->h = 0;
    float imp = -e->vh;
    if (imp > 2.2f * R) {
        e->vh = imp * 0.40f;
        e->vx *= 0.78f;
        e->vy *= 0.78f;
        if (!e->nospin) {
            die_kick(e, imp, rng);
        }
        if (live) {
            bounce_snd(e, imp);
        }
    } else {
        e->vh = 0;
        if (live && imp > R) {
            bounce_snd(e, imp);
        }
    }
}

// on the felt: it rolls, and the felt slows it
static void die_roll(die_t *e, float dt)
{
    float R = e->r;
    float sp = sqrtf(e->vx * e->vx + e->vy * e->vy);
    float ns = sp - (2.5f * R + 2.2f * sp) * dt;
    if (ns < 0) {
        ns = 0;
    }
    if (sp > 1e-4f) {
        e->vx *= ns / sp;
        e->vy *= ns / sp;
    }
    if (!e->nospin) {
        v3 roll = v3m(e->vy / R, e->vx / R, 0);
        float k = 9.0f * dt;
        e->w.x += (roll.x - e->w.x) * k;
        e->w.y += (roll.y - e->w.y) * k;
        e->w.z -= e->w.z * 3.5f * dt;
    }
}

// the walls of the tray: the die bounces back (a knock sound near the felt)
static void die_walls(die_t *e, const rc_t *t, bool live)
{
    float R = e->r;
    float m = R * 0.92f + 2;
    bool hit = false;
    if (s_ly.round) {
        // the round felt: the die stays in the circle; it bounces off the wall along the normal
        float cx = t->x + t->w * 0.5f, cy = t->y + t->h * 0.5f, lim = t->w * 0.5f - m * 1.05f;
        float dx = e->x - cx, dy = e->y - cy, d = sqrtf(dx * dx + dy * dy);
        if (d > lim && d > 0.01f) {
            float nx = dx / d, ny = dy / d, vn = e->vx * nx + e->vy * ny;
            e->x = cx + nx * lim;
            e->y = cy + ny * lim;
            if (vn > 0) {
                e->vx -= 1.55f * vn * nx;
                e->vy -= 1.55f * vn * ny;
                hit = true;
            }
        }
    }
    if (e->x < t->x + m) {
        e->x = t->x + m;
        if (e->vx < 0) {
            e->vx = -e->vx * 0.55f;
            hit = true;
        }
    }
    if (e->x > t->x + t->w - m) {
        e->x = t->x + t->w - m;
        if (e->vx > 0) {
            e->vx = -e->vx * 0.55f;
            hit = true;
        }
    }
    if (e->y < t->y + m) {
        e->y = t->y + m;
        if (e->vy < 0) {
            e->vy = -e->vy * 0.55f;
            hit = true;
        }
    }
    if (e->y > t->y + t->h - m) {
        e->y = t->y + t->h - m;
        if (e->vy > 0) {
            e->vy = -e->vy * 0.55f;
            hit = true;
        }
    }
    if (hit && !e->nospin) {
        e->w.z += (e->vx - e->vy) / R * 0.3f;
        if (live && e->h < R) {
            float v = sqrtf(e->vx * e->vx + e->vy * e->vy);
            if (v > 3 * R) {
                fx(FX_DIE2, (int)(10 + 40 * fminf(1, v / (R * 12))), 0.12f);
            }
        }
    }
}

// one step of one moving die
static void die_step(die_t *e, const rc_t *t, uint32_t *rng, bool live, bool stop)
{
    const float dt = DT_SIM;
    float R = e->r;
    e->t += dt;
    bool ground = e->h <= 0 && e->vh <= 0;
    if (!ground) {
        die_fall(e, dt, rng, live);
    }
    e->x += e->vx * dt;
    e->y += e->vy * dt;
    if (e->h <= 0 && e->vh <= 0) {
        die_roll(e, dt);
    }
    die_walls(e, t, live);
    // the turn
    float wl = v3len(e->w);
    if (wl > 1e-5f) {
        e->rot = qunit(qmul(qaxis(v3mul(e->w, 1.0f / wl), wl * dt), e->rot));
    }
    // the stop
    if (stop) {
        float sp = sqrtf(e->vx * e->vx + e->vy * e->vy);
        if ((e->h <= 0 && e->vh <= 0 && sp < 0.3f * R && wl < 1.4f) || e->t > T_MAX) {
            e->moving = false;
            e->tend = e->t;
            e->vx = e->vy = e->vh = 0;
            e->h = 0;
            e->w = v3m(0, 0, 0);
        }
    }
}

// two dice that touch push each other (only near the felt: a die in the air goes over)
static void dice_push(die_t *a, die_t *b, bool live)
{
    if (!a->moving && !b->moving) {
        return;
    }
    if (a->h > a->r * 0.8f || b->h > b->r * 0.8f) {
        return;
    }
    float dx = b->x - a->x, dy = b->y - a->y, dd = dx * dx + dy * dy, md = (a->r + b->r) * 0.86f;
    if (dd >= md * md || dd < 1e-6f) {
        return;
    }
    float dist = sqrtf(dd), nx = dx / dist, ny = dy / dist, over = md - dist;
    // a die at rest does not move: the moving one goes round it
    float ka = a->moving ? (b->moving ? 0.5f : 1.0f) : 0, kb = b->moving ? (a->moving ? 0.5f : 1.0f) : 0;
    a->x -= nx * over * ka;
    a->y -= ny * over * ka;
    b->x += nx * over * kb;
    b->y += ny * over * kb;
    float rv = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
    if (!(rv < 0)) {
        return;
    }
    float j2 = -rv * 0.8f;
    if (a->moving && b->moving) {
        a->vx -= nx * j2;
        a->vy -= ny * j2;
        b->vx += nx * j2;
        b->vy += ny * j2;
    } else if (a->moving) {
        a->vx -= nx * j2 * 2;
        a->vy -= ny * j2 * 2;
    } else {
        b->vx += nx * j2 * 2;
        b->vy += ny * j2 * 2;
    }
    if (live && -rv > 4 * a->r) {
        fx(FX_DIE1, (int)(12 + 40 * fminf(1, -rv / (a->r * 14))), 0.15f);
    }
}

// one step of all dice; live = on the screen (sounds); stop = the dice may stop (not in a rattle)
static void sim_step(die_t *d, int n, uint32_t *rng, bool live, bool stop)
{
    const rc_t *t = tray();
    for (int i = 0; i < n; i++) {
        if (d[i].moving) {
            die_step(&d[i], t, rng, live, stop);
        }
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            dice_push(&d[i], &d[j], live);
        }
    }
}

// the turn on the screen now: the turn of the throw, with the extra turn spread over the first 60 % of the flight
static void die_pose(die_t *d)
{
    if (!d->moving) {
        d->q = d->qt;
        return;
    }
    float s = d->tend > 0 ? d->t / (0.6f * d->tend) : 0;
    if (s > 1) {
        s = 1;
    }
    s = s * s * (3 - 2 * s);
    d->q = qmul(qnlerp(Q_ONE, d->corr, s), qmul(d->rot, d->q0));
}

static void spec_text(char *out, int max);
static void result_done(int64_t now);

// THE THROW: the results (fair) first, then the motion from where the dice are now. dir: the flick (0, 0 = none).
// fresh = false: no new results (a knock that was no shake): the dice only fall back onto the numbers that they had.
static void throw_start(float dirx, float diry, int64_t now, bool fresh)
{
    if (!s_nd) {
        return;
    }
    s_rng ^= esp_random();
    float dl = sqrtf(dirx * dirx + diry * diry);
    const rc_t *t = tray();
    float base = (t->w + t->h) * 0.55f;
    for (int i = 0; i < s_nd; i++) {
        die_t *d = &s_dice[i];
        die_pose(d);
        if (fresh) {
            d->val = (uint8_t)(1 + fair(faces_of(d)));
        }
        d->qt = rest_of(d->lk.so, d->val, (frnd() - 0.5f) * 1.1f);
        d->q0 = d->q;
        d->rot = d->corr = Q_ONE;
        d->t = d->tend = 0;
        d->moving = true;
        d->nospin = false;
        d->nat20 = d->nat1 = false;
        d->lk.tint = 0;
        if (d->h < 0) {
            d->h = 0;
        }
        float R = d->r;
        if (!fresh) {
            // (a small hop and a slow turn: the die finds its last number again)
            if (d->vh < 4 * R) {
                d->vh = 4 * R;
            }
            d->w = v3add(v3mul(d->w, 0.3f), v3mul(v3unit(v3m(frnd() - 0.5f, frnd() - 0.5f, frnd() - 0.5f)), 4));
            continue;
        }
        d->vh = (12.5f + 4.5f * frnd()) * R;
        float a;
        if (dl > 1) {
            a = atan2f(diry, dirx) + (frnd() - 0.5f) * 0.8f;
        } else {
            a = frnd() * 6.2831853f;
        }
        float sp = base * (0.55f + 0.5f * frnd()) * (dl > 1 ? 1.25f : 1.0f);
        d->vx = fcos(a) * sp;
        d->vy = fsin(a) * sp;
        v3 ax = v3unit(v3m(frnd() - 0.5f, frnd() - 0.5f, frnd() - 0.5f));
        d->w = v3mul(ax, 11.0f + 11.0f * frnd());
    }
    // the hidden first run: where each die stops and how it is turned then
    static die_t cp[MAXD];
    memcpy(cp, s_dice, sizeof cp);
    uint32_t r1 = s_rng;
    for (int k = 0; k < (int)(T_MAX / DT_SIM) + 4; k++) {
        bool any = false;
        for (int i = 0; i < s_nd; i++) {
            any |= cp[i].moving;
        }
        if (!any) {
            break;
        }
        sim_step(cp, s_nd, &r1, false, true);
    }
    for (int i = 0; i < s_nd; i++) {
        die_t *d = &s_dice[i];
        quat qend = qmul(cp[i].rot, d->q0);
        d->corr = qmul(d->qt, qconj(qend));
        d->tend = cp[i].tend > 0.05f ? cp[i].tend : 0.05f;
    }
    s_throwing = fresh;
    s_rattle = false;
    s_acc = 0;
    s_landed = 0;
    s_throw_t = 0;
    if (fresh) {
        s_has_res = false;
    }
    s_fx = 0;
    (void)now;
}

// all dice stopped: the total, the history, the natural 20 or 1
static void result_done(int64_t now)
{
    res_t r;
    memset(&r, 0, sizeof r);
    int total = 0, p = 0;
    bool n20 = false, n1 = false;
    for (int i = 0; i < s_nd; i++) {
        die_t *d = &s_dice[i];
        int v = d->val;
        if (d->kind == K100) {
            if (d->part == 1) {
                int tens = (v - 1) * 10, ones = (i + 1 < s_nd && s_dice[i + 1].part == 2) ? s_dice[i + 1].val - 1 : 0;
                total += tens + ones == 0 ? 100 : tens + ones;
            }
            continue;
        }
        total += v;
        if (d->kind == K20 && v == 20) {
            d->nat20 = true;
            n20 = true;
        }
        if (d->kind == K20 && v == 1) {
            d->nat1 = true;
            n1 = true;
        }
        if (s_mode == M_TWO) {
            p += snprintf(r.parts + p, sizeof r.parts - (size_t)p, i ? " + %d" : "%d", v);
        }
    }
    r.total = (int16_t)total;
    r.nat = n20 ? 20 : n1 ? 1 : 0;
    r.nd = (uint8_t)(s_mode == M_ONE ? 1 : s_mode == M_TWO ? 2 : set_n());
    spec_text(r.spec, sizeof r.spec);
    s_res = r;
    s_has_res = true;
    s_res_t = now;
    // the history: the newest first
    memmove(&s_hist[1], &s_hist[0], sizeof(res_t) * (HN - 1));
    s_hist[0] = r;
    if (s_nh < HN) {
        s_nh++;
    }
    char b[8];
    snprintf(b, sizeof b, "%d", total);
    mask_text(s_tot_mask, 160, 48, b, false);
    s_fx = r.nat;
    s_fx_t = now;
    if (n20) {
        fx(FX_WIN, 80, 0);
        // the confetti comes out of the first natural 20
        for (int i = 0; i < s_nd; i++) {
            if (s_dice[i].nat20) {
                static const uint8_t CC[6][3] = {{255, 214, 60},  {255, 90, 120}, {80, 200, 255},
                                                 {120, 255, 140}, {255, 150, 40}, {200, 120, 255}};
                for (int k = 0; k < NCONF; k++) {
                    conf_t *c = &s_conf[k];
                    float a = frnd() * 6.2831853f, sp = s_dice[i].r * (3 + 6 * frnd());
                    c->x = s_dice[i].x;
                    c->y = s_dice[i].y;
                    c->vx = fcos(a) * sp;
                    c->vy = fsin(a) * sp - s_dice[i].r * 4;
                    c->a = frnd() * 6.28f;
                    c->va = (frnd() - 0.5f) * 14;
                    c->life = 1.4f + 0.6f * frnd();
                    c->c = rgb(CC[k % 6][0], CC[k % 6][1], CC[k % 6][2]);
                }
                break;
            }
        }
    } else if (n1) {
        fx(FX_LOSE, 75, 0);
    }
}

// the text of the throw: "D6", "2D6", "2D20+D8"
static void spec_text(char *out, int max)
{
    out[0] = 0;
    if (s_mode == M_ONE) {
        snprintf(out, (size_t)max, "D6");
        return;
    }
    if (s_mode == M_TWO) {
        snprintf(out, (size_t)max, "2D6");
        return;
    }
    int p = 0;
    for (int k = KN - 1; k >= 0 && p < max - 1; k--) {
        if (!s_set[k]) {
            continue;
        }
        char one[12];
        if (s_set[k] > 1) {
            snprintf(one, sizeof one, "%s%dD%d", p ? "+" : "", s_set[k], K_SIDES[k]);
        } else {
            snprintf(one, sizeof one, "%sD%d", p ? "+" : "", K_SIDES[k]);
        }
        p += snprintf(out + p, (size_t)(max - p), "%s", one);
    }
}

// ---------------------------------------------------------------- the still picture (the background and the tray)
static uint32_t hash2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

// the round tray of the round panel: a wood ring, then the felt with a soft light and a dark edge
static void bg_round(uint16_t *dst, const rc_t *t)
{
    float cx = t->x + t->w * 0.5f, cy = t->y + t->h * 0.5f, R = t->w * 0.5f, rim = R * 0.06f + 3;
    // the dark rim of the circle of the panel (a soft vignette)
    tfd_ring(gfx_w * 0.5f, gfx_h * 0.5f, gfx_w * 0.5f - 6, 14, rgb(40, 30, 70), 160);
    tfd_disc(cx + 3, cy + 5, R + 3, rgb(6, 6, 14), 160);
    tfd_disc(cx, cy, R + 1, rgb(40, 20, 8), 256);
    tfd_disc(cx, cy, R, rgb(122, 70, 30), 256);
    tfd_ring(cx, cy, R - rim * 0.35f, rim * 0.4f, rgb(160, 98, 46), 150);
    float fr = R - rim;
    tfd_disc(cx, cy, fr + 1, rgb(214, 172, 84), 256);
    float lx = cx, ly = cy - fr * 0.12f;
    for (int y = (int)(cy - fr); y <= (int)(cy + fr); y++) {
        if (y < 0 || y >= gfx_h) {
            continue;
        }
        uint16_t *row = &dst[y * gfx_w];
        float dy = y - cy, hw2 = fr * fr - dy * dy;
        if (hw2 <= 0) {
            continue;
        }
        float hw = sqrtf(hw2);
        for (int x = (int)(cx - hw + 1); x <= (int)(cx + hw - 1); x++) {
            float ex = (x - lx) / fr, ey = (y - ly) / fr, d2 = ex * ex + ey * ey;
            float l = 1.0f - 0.5f * (d2 > 1.4f ? 1.4f : d2);
            float dx = x - cx, e = fr - sqrtf(dx * dx + dy * dy);  // (the inner shadow of the wall)
            if (e < 12) {
                l *= 0.6f + 0.033f * e;
            }
            int n = (int)(hash2(x, y) & 15) - 8;
            row[x] = tfd_rgb((int)(10 + 14 * l) + n / 3, (int)(46 + 74 * l) + n, (int)(34 + 46 * l) + n / 2);
        }
    }
}

static void bg_make(void)
{
    if (s_bg_mode == s_mode && s_bg) {
        return;
    }
    s_bg_mode = s_mode;
    uint16_t *dst = s_bg ? s_bg : g_fb;
    tfd_target(dst, gfx_w, gfx_h);
    tfd_noclip();
    // the room: a dark blue to violet
    tfd_vgrad(0, 0, gfx_w, gfx_h, rgb(16, 18, 38), rgb(26, 14, 40));
    const rc_t *t = tray();
    if (s_ly.round) {
        bg_round(dst, t);
        tfd_screen();
        return;
    }
    // the wood rim of the tray, then the felt with a soft light in the middle and a dark edge
    int rim = gfx_w >= 600 && gfx_h >= 300 ? 7 : 5;
    tfd_rrect(t->x - 1, t->y - 1, t->w + 2, t->h + 2, 14, rgb(40, 20, 8), 256);
    tfd_rrect(t->x, t->y, t->w, t->h, 13, rgb(122, 70, 30), 256);
    tfd_rrect(t->x + 2, t->y + 2, t->w - 4, t->h / 2, 11, rgb(160, 98, 46), 120);
    int fx0 = t->x + rim, fy0 = t->y + rim, fw = t->w - 2 * rim, fh = t->h - 2 * rim;
    tfd_rrect(fx0 - 1, fy0 - 1, fw + 2, fh + 2, 10, rgb(214, 172, 84), 256);
    float cx = fx0 + fw * 0.5f, cy = fy0 + fh * 0.45f, rx = fw * 0.62f, ry = fh * 0.75f;
    for (int y = fy0; y < fy0 + fh; y++) {
        uint16_t *row = &dst[y * gfx_w];
        float dy = (y - cy) / ry;
        int ey = y - fy0 < fh - 1 - (y - fy0) ? y - fy0 : fh - 1 - (y - fy0);
        for (int x = fx0; x < fx0 + fw; x++) {
            // the round corners of the felt
            int ex = x - fx0 < fw - 1 - (x - fx0) ? x - fx0 : fw - 1 - (x - fx0);
            const int cr = 9;
            if (ex < cr && ey < cr) {
                float ddx = cr - ex - 0.5f, ddy = cr - ey - 0.5f;
                if (ddx * ddx + ddy * ddy > cr * cr) {
                    continue;
                }
            }
            float dx = (x - cx) / rx, d2 = dx * dx + dy * dy;
            float l = 1.0f - 0.55f * (d2 > 1.4f ? 1.4f : d2);
            // the inner shadow of the wall
            int e = ex < ey ? ex : ey;
            if (e < 10) {
                l *= 0.62f + 0.038f * e;
            }
            int n = (int)(hash2(x, y) & 15) - 8;
            row[x] = tfd_rgb((int)(10 + 14 * l) + n / 3, (int)(46 + 74 * l) + n, (int)(34 + 46 * l) + n / 2);
        }
    }
    // the plate of the result panel
    const rc_t *p = s_mode == M_DND ? &s_ly.pan_dd : &s_ly.pan;
    tfd_rrect(p->x, p->y, p->w, p->h, 12, rgb(30, 32, 58), 256);
    tfd_rrect(p->x + 2, p->y + 2, p->w - 4, p->h - 4, 10, rgb(20, 22, 42), 256);
    tfd_screen();
}

// ---------------------------------------------------------------- the panel items
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
enum {
    MI_SND,
    MI_SHAKE,
    MI_LANG,
    MI_CLR
};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_SND, 2, 0, {"SOUND", "צליל"}, ONOFF_V, "dice_snd"},
        {PI_PICK, MI_SHAKE, 2, 0, {"SHAKE TO THROW", "ניעור לזריקה"}, ONOFF_V, "dice_shk"},
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_V, "dice_lang"},
        {PI_ACT2, MI_CLR, 0, 0, {"CLEAR HISTORY", "מחק היסטוריה"}, NULL, NULL},
};
static bool item_shown(int id)
{
    return id != MI_SHAKE || (app_board_caps() & APP_NEEDS_IMU);
}
static void item_set(int id, int v)
{
    if (id == MI_SND) {
        s_snd = v == 0;
    }
    if (id == MI_SHAKE) {
        s_shake_on = v == 0;
    }
    if (id == MI_LANG) {
        s_he = v == 1;
    }
    if (id == MI_CLR) {
        s_nh = 0;
        s_has_res = false;
    }
}
static const pitems_t PANEL = {ITEMS, 4, item_shown, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- open, close
static void set_load(void)
{
    uint32_t v = store_get("dice_set", 0xFFFFFFFFu);
    if (v == 0xFFFFFFFFu) {
        memset(s_set, 0, sizeof s_set);
        s_set[K20] = 1;
        return;
    }
    for (int k = 0; k < KN; k++) {
        s_set[k] = (uint8_t)((v >> (4 * k)) & 15);
        if (s_set[k] > MAXK) {
            s_set[k] = MAXK;
        }
    }
}
static void set_save(void)
{
    uint32_t v = 0;
    for (int k = 0; k < KN; k++) {
        v |= (uint32_t)s_set[k] << (4 * k);
    }
    store_set("dice_set", v);
    s_set_dirty = 0;
}

static void init(void)
{
    sol_build();
    s_view = qmul(qaxis(v3m(1, 0, 0), -0.34f), qaxis(v3m(0, 1, 0), -0.20f));
}

static void enter(int64_t now)
{
    init();
    mask_build();
    lay_init();
    s_he = store_get("dice_lang", 0) == 1;
    s_snd = store_get("dice_snd", 0) == 0;
    s_shake_on = store_get("dice_shk", 0) == 0;
    s_mode = (int)(store_get("dice_mode", M_ONE) % M_N);
    set_load();
    s_rng = esp_random() | 1;
    if (!s_bg) {
        s_bg = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM);
    }
    s_bg_mode = -1;
    s_nd = 0;
    dice_make(false);
    s_throwing = s_rattle = false;
    s_has_res = false;
    s_fx = 0;
    s_down = false;
    s_last = s_idle_t = now;
    s_set_dirty = 0;
    s_clr_t = now - 10000;
    tfsfx_load_mem(DZ_SFX_DATA, DZ_SFX_LEN, FXN, FX_N < DZ_SFX_N ? FX_N : DZ_SFX_N, 46);
    sysui_panel_items(&PANEL);
}

static void leave(void)
{
    if (s_set_dirty) {
        set_save();
    }
    tfsfx_free();
    if (s_bg) {
        heap_caps_free(s_bg);
        s_bg = NULL;
    }
}

// ---------------------------------------------------------------- the touch
static int zone_at(int x, int y)
{
    for (int i = 0; i < M_N; i++) {
        if (in_rc(&s_ly.tab[i], x, y)) {
            return Z_TAB + i;
        }
    }
    if (s_mode == M_DND) {
        for (int i = 0; i <= KN; i++) {
            if (in_rc(&s_ly.chip[i], x, y)) {
                return Z_CHIP + i;
            }
        }
    }
    if (in_rc(tray(), x, y)) {
        return Z_TRAY;
    }
    // (the result panel also throws: a big target)
    if (in_rc(s_mode == M_DND ? &s_ly.pan_dd : &s_ly.pan, x, y)) {
        return Z_TRAY;
    }
    return Z_NONE;
}
static void set_mode(int m, int64_t now)
{
    if (m == s_mode) {
        return;
    }
    s_mode = m;
    store_set("dice_mode", (uint32_t)m);
    s_nd = 0;
    s_throwing = s_rattle = false;
    dice_make(true);
    for (int i = 0; i < s_nd; i++) {
        // (all dice of a new mode fall in)
        s_dice[i].h = s_dice[i].r * (2.2f + 0.8f * frnd());
        s_dice[i].moving = s_dice[i].nospin = true;
    }
    s_has_res = false;
    s_fx = 0;
    fx(FX_TAB, 60, 0.04f);
    (void)now;
}
static void set_changed(int64_t now)
{
    s_set_dirty = now;
    s_throwing = s_rattle = false;
    for (int i = 0; i < s_nd; i++) {
        if (s_dice[i].moving && !s_dice[i].nospin) {
            s_dice[i].moving = false;
            die_pose(&s_dice[i]);
        }
    }
    dice_make(true);
    s_has_res = false;
    s_fx = 0;
}
static void chip_add(int k, int64_t now)
{
    if (s_set[k] >= MAXK || dnd_count() + (k == K100 ? 2 : 1) > MAXD) {
        fx(FX_DEL, 30, 0.1f);
        return;
    }
    s_set[k]++;
    fx(FX_ADD, 70, 0.08f);
    set_changed(now);
}
static void chip_del(int k, int64_t now)
{
    if (!s_set[k]) {
        return;
    }
    s_set[k]--;
    fx(FX_DEL, 70, 0.08f);
    set_changed(now);
}

static void input(const input_event_t *ev)
{
    int64_t now = ev->t_ms;
    s_idle_t = now;
    if (ev->kind == IN_DOWN) {
        s_down = true;
        s_long_done = false;
        s_dx = ev->x;
        s_dy = ev->y;
        s_zone = zone_at(ev->x, ev->y);
        return;
    }
    if (ev->kind == IN_LONG) {
        // a hold on a chip removes one die of that kind (and one more each 0.32 s while the finger stays)
        if (s_zone >= Z_CHIP && s_zone < Z_CHIP + KN && zone_at(ev->x, ev->y) == s_zone) {
            chip_del(s_zone - Z_CHIP, now);
            s_long_done = true;
            s_long_next = now + 320;
        }
        return;
    }
    if (ev->kind == IN_UP) {
        s_down = false;
        if (s_zone == Z_TRAY && !s_long_done) {
            // a tap or a flick on the tray throws (not while the dice are still in the air)
            float dx = (float)(ev->x - s_dx), dy = (float)(ev->y - s_dy);
            if (s_mode == M_DND && !s_nd) {
                s_nudge_t = now;
                fx(FX_DEL, 40, 0.1f);
                return;
            }
            if (s_throwing && s_throw_t < 0.35f) {
                return;
            }
            bool flick = dx * dx + dy * dy > 24 * 24;
            throw_start(flick ? dx : 0, flick ? dy : 0, now, true);
        }
        return;
    }
    if (ev->kind == IN_TAP) {
        int z = zone_at(ev->x, ev->y);
        if (z != s_zone || s_long_done) {
            return;
        }
        if (z >= Z_TAB && z < Z_TAB + M_N) {
            set_mode(z - Z_TAB, now);
        } else if (z >= Z_CHIP && z < Z_CHIP + KN) {
            chip_add(z - Z_CHIP, now);
        } else if (z == Z_CHIP + KN && dnd_count()) {
            // (CLEAR is next to a chip: a thumb that misses must not lose the set, so it needs two taps)
            if (now - s_clr_t < CLR_MS) {
                memset(s_set, 0, sizeof s_set);
                fx(FX_DEL, 70, 0);
                set_changed(now);
                s_clr_t = -10000;
            } else {
                s_clr_t = now;
                fx(FX_TAB, 50, 0);
            }
        }
    }
}

// ---------------------------------------------------------------- the update
// a shake starts: the dice leave their numbers and rattle in the tray
static void rattle_start(void)
{
    s_rattle = true;
    s_throwing = false;
    s_rattle_t = s_calm_t = 0;
    s_shake_snd_t = 0;
    for (int i = 0; i < s_nd; i++) {
        die_t *d = &s_dice[i];
        die_pose(d);
        d->q0 = d->q;
        d->rot = d->corr = Q_ONE;
        d->t = 0;
        d->moving = true;
        d->nospin = false;
        d->lk.tint = 0;
    }
    s_res_keep = s_has_res;
    s_has_res = false;
    s_fx = 0;
}

// one frame of the rattle (sh = the shake of the board); the throw starts when the shake ends
static void rattle_step(int64_t now, float dt, float sh)
{
    s_idle_t = now;
    s_rattle_t += dt;
    s_calm_t = sh < 0.45f ? s_calm_t + dt : 0;
    s_shake_snd_t -= dt;
    if (s_shake_snd_t <= 0) {
        fx(FX_SHAKE, 70, 0.05f);
        s_shake_snd_t = tfsfx_ms(FX_SHAKE) > 0 ? tfsfx_ms(FX_SHAKE) / 1000.0f - 0.08f : 1.1f;
    }
    float ax = 0, ay = 0, az = 0;
    imu_linear_logical(&ax, &ay, &az);
    for (int i = 0; i < s_nd; i++) {
        die_t *d = &s_dice[i];
        float R = d->r;
        // the board moves under the dice: they lag behind it, and they jump at each stroke
        d->vx -= ax * 900.0f * dt;
        d->vy -= ay * 900.0f * dt;
        if (d->h <= 0 && frnd() < (0.6f + sh) * dt * 6) {
            d->vh = (5 + 6 * frnd()) * R;
            d->vx += (frnd() - 0.5f) * R * 14;
            d->vy += (frnd() - 0.5f) * R * 14;
            d->w = v3add(d->w, v3mul(v3unit(v3m(frnd() - 0.5f, frnd() - 0.5f, frnd() - 0.5f)), 8 + 8 * frnd()));
        }
        float sp = sqrtf(d->vx * d->vx + d->vy * d->vy), mx = R * 16;
        if (sp > mx) {
            d->vx *= mx / sp;
            d->vy *= mx / sp;
        }
    }
    s_acc += dt;
    for (int k = 0; k < 12 && s_acc >= DT_SIM; k++, s_acc -= DT_SIM) {
        sim_step(s_dice, s_nd, &s_rng, true, false);
    }
    if (s_acc > DT_SIM) {
        s_acc = 0;
    }
    for (int i = 0; i < s_nd; i++) {
        s_dice[i].q = qmul(s_dice[i].rot, s_dice[i].q0);
    }
    // (a short knock is no shake: the dice fall back, no throw)
    if (s_calm_t > 0.14f || s_rattle_t > 4.0f) {
        s_rattle = false;
        if (s_rattle_t - s_calm_t >= 0.3f) {
            throw_start(0, 0, now, true);
        } else {
            throw_start(0, 0, now, false);
            s_has_res = s_res_keep;
        }
    }
}

// one frame of the throw (and of the dice that fall in); at the end the result, and the tidy move of a big D&D set
static void throw_step(int64_t now, float dt)
{
    bool any = false;
    for (int i = 0; i < s_nd; i++) {
        any |= s_dice[i].moving;
    }
    if (any) {
        s_idle_t = now;
        s_acc += dt;
        s_throw_t += dt;
        for (int k = 0; k < 12 && s_acc >= DT_SIM; k++, s_acc -= DT_SIM) {
            sim_step(s_dice, s_nd, &s_rng, true, true);
        }
        if (s_acc > DT_SIM) {
            s_acc = 0;
        }
        for (int i = 0; i < s_nd; i++) {
            die_pose(&s_dice[i]);
        }
    } else if (s_throwing) {
        s_throwing = false;
        result_done(now);
        if (s_mode == M_DND && s_nd && s_dice[0].r < 44) {
            float px[MAXD], py[MAXD];
            grid(s_nd, px, py);
            for (int i = 0; i < s_nd; i++) {
                s_dice[i].ax = s_dice[i].x;
                s_dice[i].ay = s_dice[i].y;
                s_dice[i].gx = px[i];
                s_dice[i].gy = py[i];
            }
            s_arr = true;
            s_arr_t0 = now;
        }
    }
}

// the tidy move: the dice glide to the places of the grid
static void tidy_step(int64_t now)
{
    s_idle_t = now;
    if (s_throwing || s_rattle) {
        s_arr = false;
        return;
    }
    float u = (float)(now - s_arr_t0 - ARR_WAIT) / ARR_MS;
    if (u < 0) {
        u = 0;
    }
    if (u >= 1) {
        u = 1;
        s_arr = false;
    }
    u = u * u * (3 - 2 * u);
    for (int i = 0; i < s_nd; i++) {
        s_dice[i].x = s_dice[i].ax + (s_dice[i].gx - s_dice[i].ax) * u;
        s_dice[i].y = s_dice[i].ay + (s_dice[i].gy - s_dice[i].ay) * u;
    }
}

// the effects: the confetti and the grey of a natural 1
static void fx_step(int64_t now, float dt)
{
    if (s_fx && now - s_fx_t < 2600) {
        s_idle_t = now;
    }
    for (int k = 0; k < NCONF; k++) {
        conf_t *c = &s_conf[k];
        if (c->life <= 0) {
            continue;
        }
        c->life -= dt;
        c->vy += 420.0f * dt;
        c->vx *= 1 - 0.8f * dt;
        c->x += c->vx * dt;
        c->y += c->vy * dt;
        c->a += c->va * dt;
    }
    for (int i = 0; i < s_nd; i++) {
        if (s_dice[i].nat1 && s_fx == 1 && s_dice[i].lk.tint < 1) {
            s_dice[i].lk.tint = fminf(1, (float)(now - s_fx_t) / 600.0f);
        }
    }
}

static void update(int64_t now)
{
    float dt = (float)(now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    // the hold of a chip: one more die out each 0.32 s
    if (s_down && s_long_done && s_zone >= Z_CHIP && s_zone < Z_CHIP + KN && now >= s_long_next) {
        chip_del(s_zone - Z_CHIP, now);
        s_long_next = now + 320;
    }
    if (s_set_dirty && now - s_set_dirty > 1500) {
        set_save();
    }
    // THE SHAKE: the dice rattle in the tray while the board shakes; they go when the shake ends
    float sh = (s_shake_on && (app_board_caps() & APP_NEEDS_IMU)) ? imu_shake() : 0;
    bool can = s_nd > 0 && !sysui_panel_on();
    if (!s_rattle && can && sh > 1.1f && !(s_throwing && s_throw_t < 0.4f)) {
        rattle_start();
    }
    if (s_rattle) {
        rattle_step(now, dt, sh);
    } else {
        throw_step(now, dt);
    }
    if (s_arr) {
        tidy_step(now);
    }
    fx_step(now, dt);
    if (s_has_res && now - s_res_t < 1200) {
        s_idle_t = now;
    }
    if (s_down) {
        s_idle_t = now;
    }
#ifdef ESP_PLATFORM
    // nothing moves and nobody touches: about 7 frames a second (the battery)
    if (now - s_idle_t > 3000) {
        vTaskDelay(pdMS_TO_TICKS(140));
    }
#endif
}

// ---------------------------------------------------------------- the drawing
static void shadow(const die_t *d)
{
    float R = d->r, h = d->h > 0 ? d->h : 0;
    float k = 1 + 0.1f * h / R;
    float sx = d->x + 0.10f * R + 0.32f * h, sy = d->y + 0.20f * R + 0.45f * h;
    int a = (int)(110 / (1 + h / R));
    tfd_disc(sx, sy, R * 0.92f * k, rgb(0, 10, 4), a / 2);
    tfd_disc(sx, sy, R * 0.72f * k, rgb(0, 10, 4), a / 2);
}
static float die_scale(const die_t *d)
{
    return 1.0f + 0.15f * (d->h > 0 ? d->h : 0) / d->r;
}

static void star(float x, float y, float s, uint16_t c, int a)
{
    tfd_line(x - s, y, x + s, y, s * 0.28f, c, a);
    tfd_line(x, y - s, x, y + s, s * 0.28f, c, a);
}

static void draw_dice(int64_t now)
{
    const rc_t *t = tray();
    tfd_clip(t->x + 2, t->y + 2, t->x + t->w - 2, t->y + t->h - 2);
    for (int i = 0; i < s_nd; i++) {
        shadow(&s_dice[i]);
    }
    // the glow of a natural 20 (under the die)
    float ft = (float)(now - s_fx_t) / 1000.0f;
    for (int i = 0; i < s_nd; i++) {
        if (s_dice[i].nat20 && s_fx == 20) {
            float p = 0.75f + 0.25f * fsin(ft * 6);
            tfd_glow(s_dice[i].x, s_dice[i].y, s_dice[i].r * 1.9f, rgb(255, 200, 60), (int)(150 * p));
        }
    }
    // the dice low to high (a die in the air is over the others)
    int ord[MAXD];
    for (int i = 0; i < s_nd; i++) {
        ord[i] = i;
    }
    for (int i = 1; i < s_nd; i++) {
        for (int j = i; j > 0 && s_dice[ord[j - 1]].h > s_dice[ord[j]].h; j--) {
            int tt = ord[j];
            ord[j] = ord[j - 1];
            ord[j - 1] = tt;
        }
    }
    for (int k = 0; k < s_nd; k++) {
        const die_t *d = &s_dice[ord[k]];
        die_draw(&d->lk, d->x, d->y, d->r * die_scale(d), d->q);
    }
    // small dice of D&D: after the tidy move, the number of each die on a tag at its lower right corner
    if (s_mode == M_DND && s_has_res && !s_throwing && !s_rattle && s_nd && s_dice[0].r < 44) {
        int al = (int)((s_last - s_res_t - ARR_WAIT - ARR_MS / 2) * 256 / 250);
        if (al > 256) {
            al = 256;
        }
        const mfont_t *f = s_ly.f_badge;
        for (int i = 0; i < s_nd && al > 0; i++) {
            const die_t *d = &s_dice[i];
            char b[6];
            if (d->part == 1) {
                snprintf(b, sizeof b, "%d0", d->val - 1);
            } else if (d->part == 2) {
                snprintf(b, sizeof b, "%d", d->val - 1);
            } else {
                snprintf(b, sizeof b, "%d", d->val);
            }
            int h = f->asc + 8, w = mf_width(f, b, 0) + 10;
            if (w < h) {
                w = h;
            }
            int x = (int)(d->x + d->r * 0.70f - w / 2.0f), y = (int)(d->y + d->r * 0.62f - h / 2.0f);
            if (x + w > t->x + t->w - 3) {
                x = t->x + t->w - 3 - w;
            }
            if (y + h > t->y + t->h - 3) {
                y = t->y + t->h - 3 - h;
            }
            const uint8_t *kc = d->lk.col;
            tfd_rrect(x, y, w, h, h / 2, rgb(kc[0], kc[1], kc[2]), al);
            tfd_rrect(x + 2, y + 2, w - 4, h - 4, h / 2 - 2, rgb(12, 12, 26), al);
            uint16_t c = d->nat20 ? rgb(255, 214, 70) : d->nat1 ? rgb(230, 120, 120) : rgb(255, 255, 255);
            mf_text_c(f, x + w / 2, y + h / 2 + f->asc / 2, b, c, al > 255 ? 255 : al, 0);
        }
    }
    // the sparks round a natural 20
    for (int i = 0; i < s_nd; i++) {
        if (s_dice[i].nat20 && s_fx == 20 && ft < 3.0f) {
            int a = ft < 2.2f ? 230 : (int)(230 * (3.0f - ft) / 0.8f);
            for (int s = 0; s < 6; s++) {
                float an = ft * 1.8f + s * 1.047f, rr = s_dice[i].r * (1.25f + 0.12f * fsin(ft * 5 + s));
                star(s_dice[i].x + fcos(an) * rr, s_dice[i].y + fsin(an) * rr,
                     s_dice[i].r * 0.13f * (1 + 0.4f * fsin(ft * 9 + s * 2)), rgb(255, 236, 150), a);
            }
        }
    }
    tfd_noclip();
    // the confetti (it may fly out of the tray)
    for (int k = 0; k < NCONF; k++) {
        const conf_t *c = &s_conf[k];
        if (c->life <= 0) {
            continue;
        }
        float s = 3.2f * (gfx_w >= 600 && gfx_h >= 300 ? 1.6f : 1.0f), ca = fcos(c->a), sa = fsin(c->a);
        float p[8] = {c->x + ca * s - sa * s * 0.5f, c->y + sa * s + ca * s * 0.5f, c->x - ca * s - sa * s * 0.5f,
                      c->y - sa * s + ca * s * 0.5f, c->x - ca * s + sa * s * 0.5f, c->y - sa * s - ca * s * 0.5f,
                      c->x + ca * s + sa * s * 0.5f, c->y + sa * s - ca * s * 0.5f};
        tfd_poly(p, 4, c->c, c->life > 0.4f ? 256 : (int)(c->life / 0.4f * 256));
    }
}

// a word on a dark plate in the middle of the tray (NAT 20!, NAT 1, the hint)
static void banner(const char *s, const kfont_t *f, uint16_t col, float dy, int alpha)
{
    const rc_t *t = tray();
    int w = ui_text_w(f, s) + 28, h = f == &KF_BIG ? 40 : 30;
    int x = t->x + t->w / 2 - w / 2, y = t->y + (s_ly.round ? t->h / 6 : 12) + (int)dy;
    tfd_rrect(x, y, w, h, h / 2, rgb(10, 10, 20), alpha * 200 / 256);
    if (alpha > 128) {
        ui_text_c(f, t->x + t->w / 2, y + h / 2 + (f == &KF_BIG ? 9 : 6), s, col);
    }
}

static void draw_total(float cx, float cy, float k, uint16_t col)
{
    // the pop of a new result: from big to its size, with a small bounce
    float t = (float)(s_last - s_res_t) / 1000.0f, pop = 1;
    if (t < 0.45f) {
        float u = t / 0.45f;
        pop = 1 + 0.7f * (1 - u) * (1 - u) * fcos(u * 9);
    }
    float s = k * pop;
    mask_draw(s_tot_mask, 160, 48, cx + 2.5f * k, cy + 2.5f * k, s, 0, 0, s, rgb(4, 4, 10), 200);
    mask_draw(s_tot_mask, 160, 48, cx, cy, s, 0, 0, s, col, 256);
}

static const kfont_t *fit(const kfont_t *a, const kfont_t *b, const char *s, int w)
{
    return ui_text_w(a, s) <= w ? a : b;
}

// the text of a throw that fits w px: the text ("2D20+D8"), or the count of the dice ("7 DICE")
static const char *spec_fit(const char *spec, int nd, const kfont_t *f, int w, char *buf, int max)
{
    if (ui_text_w(f, spec) <= w) {
        return spec;
    }
    snprintf(buf, (size_t)max, s_he ? "%d קוביות" : "%d DICE", nd);
    return buf;
}

// the result of the round panel: the total in the top cap with the dice of the throw under it (or the hint); the
// last 3 results in discs on the right arc
static void panel_round(uint16_t tc, const char *spec)
{
    const rc_t *p = &s_ly.pan;
    int cx = p->x + p->w / 2;
    char fb1[24];
    int room = (int)(gfx_w * 0.5f);
    if (s_has_res) {
        float ty = p->y + 24 * s_ly.tot_k;
        draw_total((float)cx, ty, s_ly.tot_k, tc);
        const char *sub =
                s_res.parts[0] ? s_res.parts : spec_fit(s_res.spec, s_res.nd, s_ly.f_spec, room, fb1, sizeof fb1);
        ui_text_c(s_ly.f_spec, cx, p->y + p->h, sub, rgb(170, 184, 230));
    } else {
        const char *h = s_mode == M_DND && !s_nd ? T("ADD DICE", "הוסיפו קוביות") : T("TAP OR SHAKE", "הקישו או נערו");
        int hroom = 2 * (cx - (s_ly.tab[0].x + s_ly.tab[0].w) - 6);  // (between the first tab and its mirror)
        const kfont_t *f = fit(s_ly.f_hint, &KF_TEXT, h, hroom);
        ui_text_c(f, cx, p->y + p->h / 2 + f->asc / 2, h, rgb(255, 226, 140));
        if (s_nd) {
            ui_text_c(s_ly.f_spec, cx, p->y + p->h, spec_fit(spec, set_n(), s_ly.f_spec, room, fb1, sizeof fb1),
                      rgb(150, 160, 210));
        }
    }
    int first = s_has_res ? 1 : 0;
    for (int n = 0; n < 3; n++) {
        const rc_t *r = &s_ly.hist[n];
        float hx = r->x + r->w * 0.5f, hy = r->y + r->h * 0.5f, hr = r->w * 0.5f;
        int i = first + n;
        if (i >= s_nh) {
            tfd_ring(hx, hy, hr - 2, 2, rgb(44, 46, 80), 200);
            continue;
        }
        const res_t *q = &s_hist[i];
        uint16_t c = q->nat == 20 ? rgb(255, 210, 80) : q->nat == 1 ? rgb(200, 100, 100) : rgb(220, 226, 255);
        int fade = 255 - n * 50;
        tfd_disc(hx, hy, hr, ui_mix(rgb(16, 18, 38), rgb(64, 68, 110), fade), 256);
        tfd_disc(hx, hy, hr - 2, rgb(24, 26, 48), 256);
        char b[8];
        snprintf(b, sizeof b, "%d", q->total);
        const mfont_t *f = mf_width(s_ly.f_hist, b, 0) <= r->w - 10 ? s_ly.f_hist : &MF_N16;
        mf_text_c(f, (int)hx, (int)hy + f->asc / 2, b, c, fade, 0);
    }
}

static void draw_panel(void)
{
    const rc_t *p = s_mode == M_DND ? &s_ly.pan_dd : &s_ly.pan;
    uint16_t tc = s_res.nat == 20 ? rgb(255, 214, 70) : s_res.nat == 1 ? rgb(200, 90, 90) : rgb(255, 255, 255);
    char spec[24], fb1[24];
    spec_text(spec, sizeof spec);
    if (s_ly.round) {
        panel_round(tc, spec);
        return;
    }
    if (s_ly.side) {
        int cx = p->x + p->w / 2;
        // the top block: the total and the dice of the throw (or the hint)
        int top = (int)(42 * s_ly.tot_k) + s_ly.f_spec->line + 12;
        if (s_has_res) {
            int ty = p->y + 6 + (int)(21 * s_ly.tot_k);
            draw_total((float)cx, (float)ty, s_ly.tot_k, tc);
            const char *sub = s_res.parts[0] ? s_res.parts
                                             : spec_fit(s_res.spec, s_res.nd, s_ly.f_spec, p->w - 10, fb1, sizeof fb1);
            ui_text_c(s_ly.f_spec, cx, ty + (int)(17.5f * s_ly.tot_k) + 6 + s_ly.f_spec->asc, sub, rgb(170, 184, 230));
        } else {
            const char *a = s_mode == M_DND && !s_nd ? T("ADD", "הוסיפו") : T("TAP", "הקישו");
            const char *b = s_mode == M_DND && !s_nd ? T("DICE", "קוביות") : T("OR SHAKE", "או נערו");
            const kfont_t *fa = fit(s_ly.f_hint, s_ly.f_small, a, p->w - 8),
                          *fb = fit(s_ly.f_hint, s_ly.f_small, b, p->w - 8);
            bool sp = s_nd && s_nh == 0;
            int hh = fa->line + fb->line + (sp ? s_ly.f_spec->line : 0);
            int y0 = p->y + (top - hh) / 2 + fa->asc + 2;
            ui_text_c(fa, cx, y0, a, rgb(255, 226, 140));
            ui_text_c(fb, cx, y0 + fb->line, b, rgb(255, 226, 140));
            if (sp) {
                ui_text_c(s_ly.f_spec, cx, y0 + fb->line + s_ly.f_spec->line,
                          spec_fit(spec, set_n(), s_ly.f_spec, p->w - 10, fb1, sizeof fb1), rgb(150, 160, 210));
            }
        }
        // the history (the older results)
        int hy = p->y + top + 6, row = s_ly.f_hist->line + 2;
        int first = s_has_res ? 1 : 0, n = 0;
        if (s_nh > first) {
            tfd_line(p->x + 10, hy - 4, p->x + p->w - 10, hy - 4, 1.2f, rgb(60, 64, 100), 256);
        }
        for (int i = first; i < s_nh && hy + row <= p->y + p->h - 2; i++, n++) {
            const res_t *r = &s_hist[i];
            char b[8];
            snprintf(b, sizeof b, "%d", r->total);
            int fade = 255 - n * 40;
            if (fade < 90) {
                fade = 90;
            }
            uint16_t c = r->nat == 20 ? rgb(255, 210, 80) : r->nat == 1 ? rgb(200, 100, 100) : rgb(220, 226, 255);
            mf_text(s_ly.f_hist, p->x + 10, hy + s_ly.f_hist->asc, b, c, fade, 0);
            int room = p->w - 26 - mf_width(s_ly.f_hist, b, 0);
            const char *sp = spec_fit(r->spec, r->nd, s_ly.f_small, room, fb1, sizeof fb1);
            int sw = ui_text_w(s_ly.f_small, sp);
            if (sw <= room) {
                ui_text_c(s_ly.f_small, p->x + p->w - 10 - sw / 2, hy + s_ly.f_hist->asc, sp,
                          ui_mix(rgb(30, 32, 58), rgb(150, 160, 210), fade));
            }
            hy += row;
        }
    } else {
        // the bar of the 320 x 240 frame: the history at the left, the total at the right
        int cy = p->y + p->h / 2;
        int hx1 = p->x + 10;  // the end of the history
        int x = p->x + 10, first = s_has_res ? 1 : 0;
        for (int i = first, n = 0; i < s_nh && n < 3; i++, n++) {
            char b[8];
            snprintf(b, sizeof b, "%d", s_hist[i].total);
            int w = mf_width(s_ly.f_hist, b, 0);
            uint16_t c = s_hist[i].nat == 20  ? rgb(255, 210, 80)
                         : s_hist[i].nat == 1 ? rgb(200, 100, 100)
                                              : rgb(200, 206, 240);
            mf_text(s_ly.f_hist, x, cy + s_ly.f_hist->asc / 2, b, c, 230 - n * 60, 0);
            x += w + 12;
            hx1 = x;
        }
        if (s_has_res) {
            char b[8];
            snprintf(b, sizeof b, "%d", s_res.total);
            float w = mf_width(&MF_N34, b, 0) * s_ly.tot_k;
            float tx = p->x + p->w - 10 - w / 2;
            draw_total(tx, (float)cy, s_ly.tot_k, tc);
            int room = (int)(tx - w / 2) - 10 - hx1;
            const char *sub =
                    s_res.parts[0] ? s_res.parts : spec_fit(s_res.spec, s_res.nd, s_ly.f_spec, room, fb1, sizeof fb1);
            int sw = ui_text_w(s_ly.f_spec, sub);
            if (sw <= room) {
                ui_text_c(s_ly.f_spec, (int)(tx - w / 2) - 10 - sw / 2, cy + s_ly.f_spec->asc / 2, sub,
                          rgb(170, 184, 230));
            }
        } else {
            const char *h =
                    s_mode == M_DND && !s_nd ? T("ADD DICE", "הוסיפו קוביות") : T("TAP OR SHAKE", "הקישו או נערו");
            int room = p->x + p->w - 10 - hx1;
            const kfont_t *f = fit(s_ly.f_hint, s_ly.f_small, h, room);
            ui_text_c(f, p->x + p->w - 10 - ui_text_w(f, h) / 2, cy + f->asc / 2, h, rgb(255, 226, 140));
        }
    }
}

// the icon of a mode in its tab: small dice
static void tab_icon(int m, float cx, float cy, float r)
{
    static const uint8_t RED[3] = {226, 38, 52};
    look_t lk = {SO_D6, LS_PIP, RED, rgb(255, 255, 255), 0};
    quat tilt = qmul(qaxis(v3m(1, 0, 0), 0.35f), qaxis(v3m(0, 1, 0), 0.45f));
    if (m == M_ONE) {
        die_draw(&lk, cx, cy, r, qmul(tilt, rest_of(SO_D6, 5, 0.2f)));
    } else if (m == M_TWO) {
        die_draw(&lk, cx - r * 0.52f, cy + r * 0.16f, r * 0.76f, qmul(tilt, rest_of(SO_D6, 3, -0.3f)));
        die_draw(&lk, cx + r * 0.56f, cy - r * 0.14f, r * 0.76f, qmul(tilt, rest_of(SO_D6, 4, 0.4f)));
    } else {
        look_t l20 = {SO_D20, LS_NUM, K_COL[K20], rgb(255, 255, 255), 0};
        die_draw(&l20, cx, cy, r * 1.05f, qmul(qaxis(v3m(1, 0, 0), 0.25f), rest_of(SO_D20, 20, 0.1f)));
    }
}

static void draw_tabs(void)
{
    static const char *const NM[M_N][2] = {{"1 DIE", "קובייה"}, {"2 DICE", "2 קוביות"}, {"D&D", "D&D"}};
    for (int i = 0; i < M_N; i++) {
        const rc_t *r = &s_ly.tab[i];
        bool on = i == s_mode;
        if (s_ly.round) {
            // a disc with the dice of the mode
            float cx = r->x + r->w * 0.5f, cy = r->y + r->h * 0.5f, rr = r->w * 0.5f;
            tfd_disc(cx, cy, rr, on ? rgb(255, 196, 80) : rgb(44, 46, 80), 256);
            tfd_disc(cx, cy, rr - 3, on ? rgb(84, 56, 150) : rgb(30, 32, 58), 256);
            if (on) {
                tfd_disc(cx, cy - rr * 0.3f, rr * 0.62f, rgb(120, 86, 200), 110);
            }
            tab_icon(i, cx, cy, (float)s_ly.tab_r);
            continue;
        }
        if (on) {
            tfd_rrect(r->x, r->y, r->w, r->h, 12, rgb(255, 196, 80), 256);
            tfd_rrect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, 10, rgb(84, 56, 150), 256);
            tfd_rrect(r->x + 2, r->y + 2, r->w - 4, (r->h - 4) / 2, 10, rgb(120, 86, 200), 140);
        } else {
            tfd_rrect(r->x, r->y, r->w, r->h, 12, rgb(44, 46, 80), 256);
            tfd_rrect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, 10, rgb(30, 32, 58), 256);
        }
        const char *s = T(NM[i][0], NM[i][1]);
        uint16_t tc = on ? rgb(255, 240, 200) : rgb(170, 176, 214);
        tfd_clip(r->x + 2, r->y + 2, r->x + r->w - 2, r->y + r->h - 2);
        if (r->h < 60) {
            // (the 3.49: the dice at the left, the name at the right)
            float ir = (float)s_ly.tab_r;
            int x0 = r->x + 10 + (int)(2.9f * ir), x1 = r->x + r->w - 4;
            const kfont_t *f = fit(s_ly.f_tab, s_ly.f_small, s, x1 - x0);
            tab_icon(i, r->x + 6 + ir * 1.45f, r->y + r->h / 2.0f, ir);
            ui_text_c(f, (x0 + x1) / 2, r->y + r->h / 2 + f->asc / 2, s, tc);
        } else {
            float ir = (float)s_ly.tab_r;
            tab_icon(i, r->x + r->w / 2.0f, r->y + r->h * 0.40f, ir);
            const kfont_t *f = fit(s_ly.f_tab, s_ly.f_small, s, r->w - 6);
            ui_text_c(f, r->x + r->w / 2, r->y + r->h - 8, s, tc);
        }
        tfd_noclip();
    }
}

// the 2D outline of each kind on its chip (unit size)
static void chip_shape(int k, float cx, float cy, float s, uint16_t c, int a)
{
    float p[12];
    int n = 0;
    if (k == K4) {
        float q[] = {0, -1, 0.95f, 0.72f, -0.95f, 0.72f};
        memcpy(p, q, sizeof q);
        n = 3;
    } else if (k == K6) {
        tfd_rrect((int)(cx - s * 0.78f), (int)(cy - s * 0.78f), (int)(s * 1.56f), (int)(s * 1.56f), (int)(s * 0.3f), c,
                  a);
        return;
    } else if (k == K8) {
        float q[] = {0, -1.05f, 0.82f, 0, 0, 1.05f, -0.82f, 0};
        memcpy(p, q, sizeof q);
        n = 4;
    } else if (k == K10 || k == K100) {
        float q[] = {0, -1.05f, 0.92f, 0.12f, 0, 0.95f, -0.92f, 0.12f};
        memcpy(p, q, sizeof q);
        n = 4;
    } else if (k == K12) {
        for (int i = 0; i < 5; i++) {
            p[2 * i] = fcos(-1.5708f + i * 1.2566f);
            p[2 * i + 1] = fsin(-1.5708f + i * 1.2566f) + 0.08f;
        }
        n = 5;
    } else {
        for (int i = 0; i < 6; i++) {
            p[2 * i] = fcos(-1.5708f + i * 1.0472f) * 1.02f;
            p[2 * i + 1] = fsin(-1.5708f + i * 1.0472f) * 1.02f;
        }
        n = 6;
    }
    for (int i = 0; i < n; i++) {
        p[2 * i] = cx + p[2 * i] * s;
        p[2 * i + 1] = cy + p[2 * i + 1] * s;
    }
    tfd_poly(p, n, c, a);
}

// a chip of the round panel: a disc (CLEAR: a red disc with a cross; "SURE?" over it when it waits for the second tap)
static void chip_round(int i, const rc_t *r, int64_t now, float nudge)
{
    float cx = r->x + r->w * 0.5f, cy = r->y + r->h * 0.5f, rr = r->w * 0.5f;
    if (i == KN) {
        bool any = dnd_count() > 0, armed = any && now - s_clr_t < CLR_MS;
        tfd_disc(cx, cy, rr, armed ? rgb(255, 120, 120) : any ? rgb(150, 60, 70) : rgb(56, 46, 62), 256);
        tfd_disc(cx, cy, rr - 2, armed ? rgb(190, 40, 60) : any ? rgb(70, 26, 40) : rgb(34, 30, 46), 256);
        uint16_t c = any ? rgb(255, 200, 200) : rgb(120, 110, 130);
        float a = rr * 0.32f;
        tfd_line(cx - a, cy - a, cx + a, cy + a, 3.5f, c, 256);
        tfd_line(cx - a, cy + a, cx + a, cy - a, 3.5f, c, 256);
        if (armed) {
            const char *s = T("SURE?", "בטוח?");
            int w = ui_text_w(&KF_NAME, s) + 16, h = 28;
            int bx = (int)(cx + rr) - w, by = r->y - h - 4;
            tfd_rrect(bx, by, w, h, 12, rgb(190, 40, 60), 256);
            ui_text_c(&KF_NAME, bx + w / 2, by + h / 2 + KF_NAME.asc / 2 - 1, s, rgb(255, 255, 255));
        }
        return;
    }
    int n = s_set[i];
    const uint8_t *kc = K_COL[i == K100 ? KN : i];
    uint16_t col = rgb(kc[0], kc[1], kc[2]);
    uint16_t edge = n ? col : rgb(52, 56, 92);
    if (nudge > 0) {
        edge = ui_mix(edge, rgb(255, 230, 140), (int)(nudge * 255));
    }
    tfd_disc(cx, cy, rr, edge, 256);
    tfd_disc(cx, cy, rr - 2.5f, n ? rgb(36, 36, 64) : rgb(26, 28, 50), 256);
    float s = rr * 0.62f;
    uint16_t fill = n ? col : ui_mix(rgb(26, 28, 50), col, 150);
    chip_shape(i, cx + s * 0.08f, cy + s * 0.1f, s, rgb(0, 0, 0), 120);
    chip_shape(i, cx, cy, s, fill, 256);
    chip_shape(i, cx - s * 0.12f, cy - s * 0.16f, s * 0.62f, rgb(255, 255, 255), 50);
    char b[6];
    if (i == K100) {
        snprintf(b, sizeof b, "%%");
    } else {
        snprintf(b, sizeof b, "%d", K_SIDES[i]);
    }
    uint16_t ink = i == K100 ? rgb(40, 34, 52) : rgb(255, 255, 255);
    int base = (int)(cy + s_ly.f_chip->asc / 2.0f) + (i == K4 ? (int)(s * 0.22f) : 0);
    mf_text_c(s_ly.f_chip, (int)cx, base, b, ink, n ? 255 : 170, 0);
    if (n) {
        int br = s_ly.badge_r;
        float bx = cx + rr * 0.72f, by = cy - rr * 0.72f;
        tfd_disc(bx, by, br + 1.5f, rgb(20, 16, 30), 256);
        tfd_disc(bx, by, (float)br, rgb(255, 214, 70), 256);
        char c[4];
        snprintf(c, sizeof c, "%d", n);
        mf_text_c(s_ly.f_badge, (int)bx, (int)by + s_ly.f_badge->asc / 2, c, rgb(40, 24, 0), 255, 0);
    }
}

static void draw_chips(int64_t now)
{
    float nudge = now - s_nudge_t < 900 ? fsin((float)(now - s_nudge_t) / 900.0f * 3.1416f) : 0;
    for (int i = 0; i <= KN; i++) {
        const rc_t *r = &s_ly.chip[i];
        if (s_ly.round) {
            chip_round(i, r, now, nudge);
            continue;
        }
        if (i == KN) {
            // CLEAR
            bool any = dnd_count() > 0, armed = any && now - s_clr_t < CLR_MS;
            tfd_rrect(r->x, r->y, r->w, r->h, 12,
                      armed ? rgb(255, 120, 120)
                      : any ? rgb(150, 60, 70)
                            : rgb(56, 46, 62),
                      256);
            tfd_rrect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, 10,
                      armed ? rgb(190, 40, 60)
                      : any ? rgb(70, 26, 40)
                            : rgb(34, 30, 46),
                      256);
            const char *s = armed ? T("SURE?", "בטוח?") : T("CLEAR", "נקה");
            const kfont_t *f = fit(s_ly.f_spec, s_ly.f_small, s, r->w - 8);
            ui_text_c(f, r->x + r->w / 2, r->y + r->h / 2 + f->asc / 2, s,
                      any ? rgb(255, 200, 200) : rgb(120, 110, 130));
            continue;
        }
        int n = s_set[i];
        const uint8_t *kc = K_COL[i == K100 ? KN : i];
        uint16_t col = rgb(kc[0], kc[1], kc[2]);
        uint16_t edge = n ? col : rgb(52, 56, 92);
        if (nudge > 0) {
            edge = ui_mix(edge, rgb(255, 230, 140), (int)(nudge * 255));
        }
        tfd_rrect(r->x, r->y, r->w, r->h, 12, edge, 256);
        tfd_rrect(r->x + 2, r->y + 2, r->w - 4, r->h - 4, 10, n ? rgb(36, 36, 64) : rgb(26, 28, 50), 256);
        float s = fminf(r->h * 0.36f, r->w * 0.29f), cx = r->x + r->w * 0.45f,
              cy = r->y + r->h * (r->h > 60 ? 0.60f : 0.5f);
        uint16_t fill = n ? col : ui_mix(rgb(26, 28, 50), col, 150);
        chip_shape(i, cx + s * 0.08f, cy + s * 0.1f, s, rgb(0, 0, 0), 120);
        chip_shape(i, cx, cy, s, fill, 256);
        chip_shape(i, cx - s * 0.12f, cy - s * 0.16f, s * 0.62f, rgb(255, 255, 255), 50);
        char b[6];
        if (i == K100) {
            snprintf(b, sizeof b, "%%");
        } else {
            snprintf(b, sizeof b, "%d", K_SIDES[i]);
        }
        uint16_t ink = i == K100 ? rgb(40, 34, 52) : rgb(255, 255, 255);
        int base = (int)(cy + s_ly.f_chip->asc / 2.0f) + (i == K4 ? (int)(s * 0.22f) : 0);
        mf_text_c(s_ly.f_chip, (int)cx, base, b, ink, n ? 255 : 170, 0);
        if (n) {
            int br = s_ly.badge_r, bx = r->x + r->w - br - 3, by = r->y + br + 3;
            tfd_disc((float)bx, (float)by, (float)br + 1.5f, rgb(20, 16, 30), 256);
            tfd_disc((float)bx, (float)by, (float)br, rgb(255, 214, 70), 256);
            char c[4];
            snprintf(c, sizeof c, "%d", n);
            mf_text_c(s_ly.f_badge, bx, by + s_ly.f_badge->asc / 2, c, rgb(40, 24, 0), 255, 0);
        }
    }
}

static void render(int64_t now)
{
    // the still picture: a copy from PSRAM (no buffer: it is drawn into the frame each time)
    if (!s_bg) {
        s_bg_mode = -1;
    }
    bg_make();
    if (s_bg) {
        memcpy(g_fb, s_bg, (size_t)gfx_w * gfx_h * 2);
    }
    tfd_screen();
    tfd_noclip();
    draw_tabs();
    if (s_mode == M_DND) {
        draw_chips(now);
    }
    draw_dice(now);
    draw_panel();
    float ft = (float)(now - s_fx_t) / 1000.0f;
    if (s_fx == 20 && ft < 2.4f) {
        float in = ft < 0.25f ? ft / 0.25f : 1;
        int a = ft > 2.0f ? (int)(256 * (2.4f - ft) / 0.4f) : 256;
        banner(T("NAT 20!", "20 טבעי!"), &KF_BIG, rgb(255, 220, 90), -(1 - in) * 10, a);
    } else if (s_fx == 1 && ft < 2.4f) {
        int a = ft > 2.0f ? (int)(256 * (2.4f - ft) / 0.4f) : 256;
        banner(T("NAT 1", "1 טבעי"), &KF_BIG, rgb(220, 120, 120), ft * 5, a);
    }
}

// ---------------------------------------------------------------- the menu icon: two dice that tumble
static void draw_icon(int x, int y, int s)
{
    sol_build();
    if (!s_mask_ok) {
        mask_build();
    }
    if (s_view.w == 0) {
        init();
    }
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
    // the felt
    for (int j = 0; j < s; j++) {
        float d = (float)(j - s / 2) / (s / 2);
        tfd_hspan(x, x + s, y + j,
                  rgb((uint8_t)(10 + 10 * (1 - d * d)), (uint8_t)(56 + 50 * (1 - d * d)),
                      (uint8_t)(40 + 30 * (1 - d * d))),
                  256);
    }
    float t = icon_time();
    static const uint8_t RED[3] = {226, 38, 52}, BLUE[3] = {52, 96, 236};
    look_t a = {SO_D6, LS_PIP, RED, rgb(255, 255, 255), 0}, b = {SO_D20, LS_NUM, K_COL[K20], rgb(255, 255, 255), 0};
    (void)BLUE;
    for (int k = 0; k < 2; k++) {
        float ph = t * 3.14159f + k * 1.5708f;  // one bounce each second
        float hop = fabsf(fsin(ph));
        float gy = y + s * (k ? 0.56f : 0.62f);
        float cx = x + s * (k ? 0.70f : 0.29f), cy = gy - hop * s * 0.16f;
        float r = s * (k ? 0.23f : 0.20f) * (1 + 0.08f * hop);
        tfd_disc(cx + s * 0.03f + hop * s * 0.05f, gy + s * 0.07f, r * 0.85f, rgb(0, 20, 10), 110);
        quat q = qmul(qaxis(v3unit(v3m(0.6f, 1, k ? -0.4f : 0.3f)), t * 3.14159f * (k ? -1 : 1)), Q_ONE);
        die_draw(k ? &b : &a, cx, cy, r, q);
    }
    tfd_noclip();
}

const app_t app_dice = {
        .name = "DICE",
        .landscape = true,
        .init = init,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = draw_icon,
        .round = APP_ROUND_HI,
};
