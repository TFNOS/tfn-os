// DIRT KINGS (GAMES): an off road race of monster trucks, as Super Off Road. The design: a season of 8 arenas, the art
// of Blender, and the steering with the board as a real wheel.
//
// - THE LOOK: each arena is one Blender picture from straight above (apps/dirt_kings/tools/dk_arena.py: the track with
//   its banks, the jumps, the mud, the tyre walls, the stands, the flood lights; 4 grounds: dirt, clay, sand, night),
//   14 px a unit. The trucks (4 colours x 64 headings) and the pick-ups are Blender sprites with alpha (dk_data.c).
// - THE VIEW TURNS: hold the board up as a wheel. The truck of the player always points away from the player (up, to
//   the gravity): the arena turns under it. The board draws the turned (and zoomed) arena in each frame from a copy in
//   PSRAM in tiles of 8 x 8 px (a turned row then reads near pixels: 19 ms for the full 3.49 screen on one core, the
//   probe measure); the two cores draw a band each.
// - THE WHEEL: the angle of the board in the plane of the screen sets how sharply the truck turns (as the wheel of a
//   car: straight board, straight truck). A board that lies flat: its tilt steers, and the view does not turn.
// - THE RACE: 4 trucks (you and 3 bots), 4 laps. The truck speeds up by itself; a finger on the screen brakes (a long
//   brake when it stands: back). NITRO: the bolt at the right, or the BOOT key. The banks slow you and push you back,
//   the infield slows you more, the mud makes you slide, a jump throws you in the air (a landing at an angle slows you).
//   Money bags and nitro cans show on the track.
// - THE SEASON: 8 arenas. After each race: the prize, the points, then THE GARAGE: tyres (grip), shocks (the landings),
//   engine (acceleration), top speed, nitro cans. After 8 races the next season starts with faster bots.
// - Coordinates: the track units (x right, y up; the arena is DK_ARENA_W x DK_ARENA_H with the walls at its edges).
//   Headings in radians from +x to +y.
// - THE 2.5D VERSION (DK_FIXED, apps/dirt_kings/app_dirt25.c: "DIRT KINGS 2.5D"):
//   the arena does not turn. It is one Blender picture from the front at an angle (the stands at the far side), the
//   trucks are sprites from the same angle at 32 headings, a truck in the air rises over its shadow. Before the start
//   the whole arena shows; after GO the camera zooms in on the truck of the player and follows it; a small map in a
//   corner shows the track and the trucks. The same race, garage and season (their own saves: DK_KEY "d5_").
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "jingle/jingle.h"
// DIRT KINGS: the opening and the start of a race (in place of the start tune of the system): power chords of a rock band,
// the drums, a crash
static const tune_note_t DK_OPEN[] = {
        JN(0, 130, J_E3, W_SAW, 26),
        JN(0, 130, J_B3, W_SAW, 22),
        JN(160, 130, J_E3, W_SAW, 26),
        JN(160, 130, J_B3, W_SAW, 22),
        JN(320, 130, J_G3, W_SAW, 26),
        JN(320, 130, J_D4, W_SAW, 22),
        JN(480, 260, J_A3, W_SAW, 26),
        JN(480, 260, J_E4, W_SAW, 22),
        JN(780, 500, J_E3, JHOLD(W_SAW), 26),
        JN(780, 500, J_B3, JHOLD(W_SAW), 22),
        JN(780, 500, J_E4, JHOLD(W_SQUARE), 12),
        JN(0, 130, J_E2, W_SQUARE, 30),
        JN(160, 130, J_E2, W_SQUARE, 30),
        JN(320, 130, J_G2, W_SQUARE, 30),
        JN(480, 260, J_A2, W_SQUARE, 30),
        JN(780, 500, J_E2, JHOLD(W_SQUARE), 30),
        JKICK(0),
        JSNARE(160),
        JKICK(320),
        JSNARE(480),
        JKICK(640),
        JKICK(780),
        JCRASH(780),
};
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/friends.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/link.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#ifndef DK_FIXED
#define DK_FIXED 0  // 1: the 2.5D version (app_dirt25.c)
#define DK_SYM   app_dirt
#define DK_NAME  "DIRT KINGS"
#define DK_KEY   "dk_"
#endif
#if DK_FIXED
#include "dk25_data.h"
#else
#include "dk_data.h"
#endif
#ifdef ESP_PLATFORM
#include "tfn/tjpgd_rom.h"
#endif
#include "tfsfx/tfsfx.h"
#include "dk_sfx.h"

static const char *TAG = "dirt";
#define SW  gfx_w
#define SH  gfx_h
#define BAR BOARD_BAR_SCREEN
// the Tab5 (the frame 640 x 360): the screens out of the race have their own layout (T5 boxes); the 320 x 240 boxes
// of the CoreS3 would fill only the top left quarter
#if BOARD_UI_TABLET
#define T5 1
#else
#define T5 0
#endif
// R_RND: the round panels (app_t.round = APP_ROUND_HI): the game draws a square frame of 310 x 310 px (libs/rndout
// scales it out into the circle, 1.5 times on the 1.75C; gfx_w = gfx_h = 310 while the app runs its code). The arena
// fills the circle; the chips of the places are on the arc at the top, the small map and NITRO in the circle; the
// screens out of the race have their own boxes in the circle.
#if BOARD_ROUND && !BAR && !T5
#define R_RND 1
#include "rndout/rndout.h"
static rndo_t s_rnd;
// the half width of the circle at the rows y0 .. y1 (the narrower end)
static int rnd_half(int y0, int y1)
{
    float c = SH / 2.0f, R = SW / 2.0f, a = fabsf(y0 - c), b = fabsf(y1 - c), dy = a > b ? a : b;
    return dy >= R ? 0 : (int)sqrtf(R * R - dy * dy);
}
#else
#define R_RND 0
#endif
#define PI_F   3.14159265f
#define NTRUCK 4
#define LAPS   4

static float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}
static float frand(float a, float b)
{
    return a + (b - a) * (float)(esp_random() & 0xFFFF) / 65535.0f;
}
static float wrap_pi(float a)
{
    while (a > PI_F) {
        a -= 2 * PI_F;
    }
    while (a < -PI_F) {
        a += 2 * PI_F;
    }
    return a;
}
static int s_lang;
#define TR(en, he) (s_lang ? (he) : (en))

// ---------------------------------------------------------------- the sound effects (MP3 in dk_sfx.c, libs/tfsfx)
// (the sources: apps/dirt_kings/sfx/SOURCES.md; the menus keep the short tones of sfx_play)
enum {
    FX_LAND,
    FX_TYRE,
    FX_CRASH,
    FX_NITRO,
    FX_COIN,
    FX_PICKUP,
    FX_LAP,
    FX_MUD,
    FX_WIN,
    FX_LOSE,
    FX_N
};
static const char *const FXN[FX_N] = {"land", "tyre", "crash", "nitro", "coin", "pickup", "lap", "mud", "win", "lose"};
static void fx(int id, int vol, float var)
{
    tfsfx_play_var(id, vol, var);
}
static void sound_off(void);

// ---------------------------------------------------------------- the arena picture (PSRAM, tiles of 8 x 8)
#define TX ((DK_AW + 7) / 8)  // the tiles across
#define TY ((DK_AH + 7) / 8)  // the tiles down
static uint16_t *s_tex;       // TX * TY tiles of 64 px
static int s_tex_arena = -1;
static inline uint32_t tidx(uint32_t u, uint32_t v)
{
#if DK_FIXED
    return v * DK_AW + u;  // (the picture never turns: plain rows)
#else
    return (((v >> 3) * TX + (u >> 3)) << 6) | ((v & 7) << 3) | (u & 7);
#endif
}
#ifdef ESP_PLATFORM
typedef struct {
    const uint8_t *p;
    uint32_t n, pos;
} jsrc_t;
static UINT j_in(JDEC *jd, BYTE *buf, UINT n)
{
    jsrc_t *s = (jsrc_t *)jd->device;
    if (s->pos + n > s->n) {
        n = s->n - s->pos;
    }
    if (buf) {
        memcpy(buf, s->p + s->pos, n);
    }
    s->pos += n;
    return n;
}
static UINT j_out(JDEC *jd, void *bm, JRECT *r)
{
    (void)jd;
    const uint8_t *c = (const uint8_t *)bm;
    for (int y = r->top; y <= r->bottom; y++) {
        for (int x = r->left; x <= r->right; x++, c += 3) {
            if (y < DK_AH && x < DK_AW) {
                s_tex[tidx(x, y)] = rgb(c[0], c[1], c[2]);
            }
        }
    }
    return 1;
}
static bool tex_load(int k)
{
    const dk_arena_t *A = &DK_ARENAS[k];
    void *work = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    jsrc_t src = {A->jpg, A->jpg_n, 0};
    JDEC jd;
    JRESULT r = work ? jd_prepare(&jd, j_in, work, 4096, &src) : JDR_MEM1;
    if (r == JDR_OK) {
        r = jd_decomp(&jd, j_out, 0);
    }
    heap_caps_free(work);
    return r == JDR_OK;
}
#else
// the PC tests: the raw pictures of tools/mk_data.py (the raw folder)
static bool tex_load(int k)
{
    char name[96];
    snprintf(name, sizeof name, DK_FIXED ? "raw25/arena_%d.raw" : "raw/arena_%d.raw", k);
    FILE *fp = fopen(name, "rb");
    if (!fp) {
        snprintf(name, sizeof name, DK_FIXED ? "w258_dirt/raw25/arena_%d.raw" : "w258_dirt/raw/arena_%d.raw", k);
        fp = fopen(name, "rb");
    }
    uint16_t *row = malloc(DK_AW * 2);
    bool ok = fp && row;
    for (int y = 0; ok && y < DK_AH; y++) {
        ok = fread(row, 2, DK_AW, fp) == DK_AW;
        for (int x = 0; ok && x < DK_AW; x++) {
            s_tex[tidx(x, y)] = row[x];
        }
    }
    if (fp) {
        fclose(fp);
    }
    free(row);
    if (!ok) {
        for (int y = 0; y < DK_AH; y++) {
            for (int x = 0; x < DK_AW; x++) {
                s_tex[tidx(x, y)] = ((x >> 5) ^ (y >> 5)) & 1 ? rgb(150, 110, 70) : rgb(110, 80, 50);
            }
        }
    }
    return true;
}
#endif
static bool arena_load(int k)
{
    if (!s_tex) {
        s_tex = heap_caps_malloc((size_t)TX * TY * 64 * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_tex) {
        return false;
    }
    if (s_tex_arena == k) {
        return true;
    }
    int64_t t0 = esp_timer_get_time();
    bool ok = tex_load(k);
    s_tex_arena = ok ? k : -1;
    ESP_LOGI(TAG, "arena %d (%s): %s, %d ms", k, DK_ARENAS[k].name, ok ? "ok" : "FAILED",
             (int)((esp_timer_get_time() - t0) / 1000));
    return ok;
}

// ---------------------------------------------------------------- the track (line points, the surface of a place)
static const dk_arena_t *s_A;
static int s_n;                           // the points of the line
static float *s_px, *s_py, *s_tx, *s_ty;  // the points and the unit tangents (PSRAM)
static float s_step;                      // the length between two points
static int s_start, s_jump[2], s_mud[2];  // (line points; -1 = none)
static void track_build(int k)
{
    s_A = &DK_ARENAS[k];
    s_n = s_A->n;
    if (!s_px) {
        s_px = heap_caps_malloc(sizeof(float) * 4 * 1024, MALLOC_CAP_SPIRAM);
        if (!s_px) {
            return;
        }
        s_py = s_px + 1024;
        s_tx = s_px + 2048;
        s_ty = s_px + 3072;
    }
    if (s_n > 1024) {
        s_n = 1024;
    }
    for (int i = 0; i < s_n; i++) {
        s_px[i] = s_A->pts[2 * i] / 64.0f;
        s_py[i] = s_A->pts[2 * i + 1] / 64.0f;
    }
    for (int i = 0; i < s_n; i++) {
        int a = (i + s_n - 1) % s_n, b = (i + 1) % s_n;
        float dx = s_px[b] - s_px[a], dy = s_py[b] - s_py[a], l = sqrtf(dx * dx + dy * dy);
        s_tx[i] = l > 0 ? dx / l : 1;
        s_ty[i] = l > 0 ? dy / l : 0;
    }
    s_step = s_A->len / s_n;
    s_start = (int)(s_A->start * s_n) % s_n;
    for (int j = 0; j < 2; j++) {
        s_jump[j] = s_A->jump[j] >= 0 ? (int)(s_A->jump[j] * s_n) % s_n : -1;
    }
    s_mud[0] = s_A->mud[0] >= 0 ? (int)(s_A->mud[0] * s_n) % s_n : -1;
    s_mud[1] = s_A->mud[1] >= 0 ? (int)(s_A->mud[1] * s_n) % s_n : -1;
}
static inline int wrapi(int i)
{
    i %= s_n;
    return i < 0 ? i + s_n : i;
}
// the nearest line point to (x, y), looked for near the point i0 (r points each way; r = 0: all)
static int nearest(float x, float y, int i0, int r)
{
    int best = i0;
    float bd = 1e9f;
    int a = r ? -r : 0, b = r ? r : s_n - 1;
    for (int k = a; k <= b; k++) {
        int i = r ? wrapi(i0 + k) : k;
        float dx = x - s_px[i], dy = y - s_py[i], d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}
// the side distance from the line at the point i (+ = to the left of the way)
static float side_of(float x, float y, int i)
{
    return -(x - s_px[i]) * s_ty[i] + (y - s_py[i]) * s_tx[i];
}
// the line points from a to b, forward (0 .. s_n - 1)
static inline int fwd(int a, int b)
{
    return wrapi(b - a);
}
static bool in_mud(int i)
{
    if (s_mud[0] < 0) {
        return false;
    }
    return fwd(s_mud[0], i) <= fwd(s_mud[0], s_mud[1]);
}
// the height of the ground of the track at the point i (the ramp of a jump: up 3.5 units, down 1.4)
static float ramp_h(int i)
{
    float h = 0;
    for (int j = 0; j < 2; j++) {
        if (s_jump[j] < 0) {
            continue;
        }
        int d = fwd(s_jump[j], i);
        if (d > s_n / 2) {
            d -= s_n;
        }
        float u = d * s_step;
        if (u >= -3.5f && u <= 0) {
            h = fmaxf(h, 1.4f * (u + 3.5f) / 3.5f);
        } else if (u > 0 && u <= 1.4f) {
            h = fmaxf(h, 1.4f * (1 - u / 1.4f));
        }
    }
    return h;
}
// the curvature of the line at the point i (1 / radius; + = a left turn)
static float curv_at(int i)
{
    int a = wrapi(i - 6), b = wrapi(i + 6);
    float h0 = atan2f(s_ty[a], s_tx[a]), h1 = atan2f(s_ty[b], s_tx[b]);
    return wrap_pi(h1 - h0) / (12 * s_step);
}

// ---------------------------------------------------------------- the trucks
enum {
    SURF_TRACK,
    SURF_BERM,
    SURF_FIELD,
    SURF_MUD
};
typedef struct {
    float x, y, h;  // the place, the heading
    float vx, vy;   // the speed (units/s)
    float z, vz;    // in the air
    float steer;    // -1 .. 1 (+ = right)
    bool brake, air;
    int idx, lap, place;
    bool half, done;
    float prog;    // laps + the part of the lap
    float done_t;  // the race time at the finish
    int col;       // the colour (0 red, 1 blue, 2 yellow, 3 white)
    bool bot;
    bool remote;  // a friend race: another board drives it (its packets move it here)
    float skill, lane, lane_t;
    int nitro;
    float nitro_t, bump_t, back_t, wrong_t, spin_t, stuck_t;
    int surf;
    float slip;  // the side speed (the sound and the dust)
} truck_t;
static truck_t s_tr[NTRUCK];
#define IS_ME(T) ((T) == &s_tr[0] && !(T)->bot)  // the truck of this board (a friend race: [0] too)
static const char *const NAMES[NTRUCK] = {"YOU", "BRUTUS", "GOLDIE", "GHOST"};

// the upgrades of the player (0..5 each), the money, the nitro cans
enum {
    UP_TYRE,
    UP_SHOCK,
    UP_ENGINE,
    UP_SPEED,
    UP_N
};
static int s_up[UP_N], s_cans, s_money, s_race_k, s_tier;
static int s_pts[NTRUCK];  // the points of the season
static const int PRICE[5] = {200, 350, 550, 800, 1100};
#define CAN_PRICE 120
#define MAX_CANS  9

static float vmax_of(const truck_t *T)
{
    if (!T->bot) {
        return 9.0f + 0.55f * s_up[UP_SPEED];
    }
    return (9.0f + 0.2f * s_race_k + 0.6f * s_tier) * T->skill;
}
static float acc_of(const truck_t *T)
{
    if (!T->bot) {
        return 6.5f + 0.9f * s_up[UP_ENGINE];
    }
    return (6.5f + 0.35f * s_race_k + 0.8f * s_tier) * T->skill;
}
static float grip_of(const truck_t *T)
{
    if (!T->bot) {
        return 6.0f + 0.8f * s_up[UP_TYRE];
    }
    return 6.0f + 0.3f * s_race_k + 0.6f * s_tier;
}
static int shocks_of(const truck_t *T)
{
    return T->bot ? 1 + s_tier : s_up[UP_SHOCK];
}

// ---------------------------------------------------------------- the pick-ups
#define NITEM 3
typedef struct {
    bool on;
    int kind;
    float x, y, t;
    uint8_t id;
} item_t;  // kind 0 money, 1 nitro (id: a friend race)
static item_t s_item[NITEM];
static float s_item_wait;

// ---------------------------------------------------------------- the particles (dust, mud, the nitro flame)
#define NPART 64
typedef struct {
    float x, y, vx, vy, t, life, r;
    uint16_t c;
} part_t;
static part_t s_part[NPART];
static int s_part_i;
static void puff(float x, float y, float vx, float vy, float life, float r, uint16_t c)
{
    part_t *p = &s_part[s_part_i++ % NPART];
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->t = 0;
    p->life = life;
    p->r = r;
    p->c = c;
}

// ---------------------------------------------------------------- the game state
enum {
    GS_TITLE,
    GS_COUNT,
    GS_RACE,
    GS_RESULT,
    GS_GARAGE,
    GS_SEASON,
    GS_LOBBY,
    GS_ROOM
};
static int s_gs;
static float s_gt;  // the time in the state
static float s_race_t;
static int s_order[NTRUCK];  // the trucks by place
static int s_prize, s_pickup_money;
static bool s_touch_brake, s_nitro_key;
// THE TOUCH STEERING (STEERING > TOUCH in the panel, and each board with no motion sensor): a finger left or right of the
// middle steers (more to the side: a sharper turn); a finger in the middle brakes
static bool s_touch_ctl, s_touch_on;
static float s_touch_x;
static float s_clock;  // the time of the race steps (s): the friend race moves the other trucks on
static int64_t s_last, s_now_ms;
static float s_shake;
static bool s_demo;  // the title: the bots race (truck 0 too)
static int s_garage_sel = -1;
static float s_msg_t;
static char s_msg[40];

// the wheel: the angle of the board (radians, + = turned to the right), its weight (0 = the board lies flat)
static float s_wheel, s_wheel0, s_wheel_w, s_view_rot;
static float s_tilt_steer;

// ---------------------------------------------------------------- the camera
static float s_cam_x, s_cam_y, s_psi, s_zoom = 1.0f, s_anchor_x, s_anchor_y;

static void msg(const char *t, float secs)
{
    snprintf(s_msg, sizeof s_msg, "%s", t);
    s_msg_t = secs;
}

// ---------------------------------------------------------------- the saves
static void save_all(void)
{
    store_set(DK_KEY "money", (uint32_t)s_money);
    uint32_t up = 0;
    for (int i = 0; i < UP_N; i++) {
        up |= (uint32_t)(s_up[i] & 7) << (3 * i);
    }
    up |= (uint32_t)(s_cans & 15) << 12;
    store_set(DK_KEY "up", up);
    store_set(DK_KEY "race", (uint32_t)(s_race_k | (s_tier << 8)));
    uint32_t pts = 0;
    for (int i = 0; i < NTRUCK; i++) {
        pts |= (uint32_t)(s_pts[i] & 0xFF) << (8 * i);
    }
    store_set(DK_KEY "pts", pts);
}
static void load_all(void)
{
    s_money = (int)store_get(DK_KEY "money", 300);
    uint32_t up = store_get(DK_KEY "up", 3u << 12);
    for (int i = 0; i < UP_N; i++) {
        s_up[i] = (int)((up >> (3 * i)) & 7) % 6;
    }
    s_cans = (int)((up >> 12) & 15);
    if (s_cans > MAX_CANS) {
        s_cans = MAX_CANS;
    }
    uint32_t r = store_get(DK_KEY "race", 0);
    s_race_k = (int)(r & 0xFF) % DK_NARENA;
    s_tier = (int)((r >> 8) & 0xFF);
    if (s_tier > 9) {
        s_tier = 9;
    }
    uint32_t pts = store_get(DK_KEY "pts", 0);
    for (int i = 0; i < NTRUCK; i++) {
        s_pts[i] = (int)((pts >> (8 * i)) & 0xFF);
    }
}
static void new_season(bool keep_garage)
{
    s_race_k = 0;
    memset(s_pts, 0, sizeof s_pts);
    if (!keep_garage) {
        s_tier = 0;
        s_money = 300;
        memset(s_up, 0, sizeof s_up);
        s_cans = 3;
    } else if (s_tier < 9) {
        s_tier++;
    }
    save_all();
}

// ---------------------------------------------------------------- the race: the start
static void place_on_grid(truck_t *T, int slot)
{
    // the grid: two rows behind the start line, 2 trucks a row
    int i = wrapi(s_start - 6 - (slot / 2) * (int)(3.2f / s_step));
    float side = (slot & 1) ? -1.1f : 1.1f;
    T->x = s_px[i] - s_ty[i] * side;
    T->y = s_py[i] + s_tx[i] * side;
    T->h = atan2f(s_ty[i], s_tx[i]);
    T->vx = T->vy = 0;
    T->z = T->vz = 0;
    T->air = false;
    T->idx = i;
    T->lap = 0;
    T->half = false;
    T->done = false;
    T->prog = -(float)fwd(i, s_start) / s_n;
    T->steer = 0;
    T->brake = false;
    T->nitro_t = T->bump_t = T->back_t = T->wrong_t = T->spin_t = 0;
}
static void race_init(int k, bool demo)
{
    arena_load(k);
    track_build(k);
    s_demo = demo;
    // the start places: the player last (as in Super Off Road the order of the last race; here: last)
    int slot[NTRUCK] = {3, 0, 1, 2};
    for (int t = 0; t < NTRUCK; t++) {
        truck_t *T = &s_tr[t];
        memset(T, 0, sizeof *T);
        T->col = t;
        T->bot = demo || t > 0;
        T->skill = t == 0 ? 0.97f : (t == 1 ? 0.95f : t == 2 ? 0.92f : 0.89f);
        T->lane = frand(-1.0f, 1.0f);
        T->nitro = T->bot ? 2 + s_tier / 2 : s_cans;
        place_on_grid(T, slot[t]);
    }
    memset(s_item, 0, sizeof s_item);
    memset(s_part, 0, sizeof s_part);
    s_item_wait = 3.0f;
    s_race_t = 0;
    s_pickup_money = 0;
    s_cam_x = s_tr[0].x;
    s_cam_y = s_tr[0].y;
    s_psi = s_tr[0].h - PI_F / 2;
    s_zoom = 1.15f;
}

// ---------------------------------------------------------------- the physics of one truck
static void surface(truck_t *T)
{
    float d = fabsf(side_of(T->x, T->y, T->idx));
    if (d <= DK_TW / 2) {
        T->surf = in_mud(T->idx) ? SURF_MUD : SURF_TRACK;
    } else if (d <= DK_TW / 2 + DK_BERM) {
        T->surf = SURF_BERM;
    } else {
        T->surf = SURF_FIELD;
    }
}
static void truck_step(truck_t *T, float dt, bool racing)
{
    float c = cosf(T->h), s = sinf(T->h);
    float vf = T->vx * c + T->vy * s;   // along the heading
    float vl = -T->vx * s + T->vy * c;  // to the left
    float vmax = vmax_of(T), acc = acc_of(T), grip = grip_of(T);
    if (T->nitro_t > 0) {
        vmax *= 1.4f;
        acc *= 2.0f;
        T->nitro_t -= dt;
    }
    if (!T->air) {
        int was = T->surf;
        surface(T);
        if (IS_ME(T) && !s_demo && T->surf == SURF_MUD && was != SURF_MUD && fabsf(vf) > 2.0f) {
            fx(FX_MUD, (int)clampf(45 + fabsf(vf) * 4, 45, 90), 0.1f);
        }
        float cap = 1.0f;
        if (T->surf == SURF_BERM) {
            cap = 0.8f;
        } else if (T->surf == SURF_FIELD) {
            cap = 0.55f;
            grip *= 0.7f;
        } else if (T->surf == SURF_MUD) {
            cap = 0.45f;
            grip *= 0.3f;
        }
        // the engine (the truck speeds up by itself), the brake, the back gear after a long brake at a stop
        float a = 0;
        if (racing) {
            if (T->brake) {
                if (vf > 0.6f) {
                    a = -18.0f;
                } else {
                    T->back_t += dt;
                    a = T->back_t > 0.5f ? (vf > -3.0f ? -4.0f : 0) : -vf * 6;
                }
            } else {
                T->back_t = 0;
                float top = vmax * cap;
                a = vf < top ? acc * (1.0f - clampf(vf / top, 0, 1)) + 1.0f : -9.0f;
                if (vf < 0) {
                    a = 12.0f;
                }
            }
        } else {
            a = -vf * 2.0f;
        }
        vf += a * dt;
        // the grip: the side speed goes away (less on mud: a slide)
        vl *= expf(-grip * dt);
        T->slip = fabsf(vl);
        // the steer: the turn rate grows with the speed; a spin after a bad landing turns the truck by itself
        float turn = 2.4f * clampf(fabsf(vf) / 2.5f, 0, 1) * (1.0f - 0.25f * clampf(fabsf(vf) / vmax, 0, 1));
        float yaw = -T->steer * turn * (vf >= 0 ? 1 : -1);
        if (T->spin_t > 0) {
            T->spin_t -= dt;
            yaw += 5.0f * T->spin_t;
        }
        T->h = wrap_pi(T->h + yaw * dt);
        // the bank pushes the truck back to the track
        if (T->surf == SURF_BERM) {
            float sd = side_of(T->x, T->y, T->idx);
            float push = sd > 0 ? -1.0f : 1.0f;
            T->x += -s_ty[T->idx] * push * 2.5f * dt * 0;  // (the push acts on the speed below)
            T->vx += -s_ty[T->idx] * (-push) * 0.0f;
            T->vx += s_ty[T->idx] * push * 7.0f * dt * -1.0f;
            T->vy += s_tx[T->idx] * push * 7.0f * dt;
        }
        c = cosf(T->h);
        s = sinf(T->h);
    }
    if (!T->air || T->air) {
        if (!T->air) {
            T->vx = vf * c - vl * s;
            T->vy = vf * s + vl * c;
        }
    }
    T->x += T->vx * dt;
    T->y += T->vy * dt;
    // the line point, the lap
    int old = T->idx;
    T->idx = nearest(T->x, T->y, T->idx, 24);
    // the jump: over the top of a ramp, on the track, fast enough: the truck flies
    if (!T->air) {
        for (int j = 0; j < 2; j++) {
            if (s_jump[j] < 0) {
                continue;
            }
            int a = fwd(old, s_jump[j]), b = fwd(T->idx, s_jump[j]);
            bool over = a < 30 && (b > s_n - 30 || b == 0) && a != 0;
            if (over && fabsf(side_of(T->x, T->y, T->idx)) < DK_TW / 2 + 0.6f && vf > 3.0f) {
                T->air = true;
                T->z = 1.4f;
                T->vz = 0.42f * vf + 0.8f;
            }
        }
        if (!T->air) {
            T->z = ramp_h(T->idx) * (fabsf(side_of(T->x, T->y, T->idx)) < DK_TW / 2 + DK_BERM ? 1.0f : 0.0f);
        }
    } else {
        T->vz -= 20.0f * dt;
        T->z += T->vz * dt;
        float g = ramp_h(T->idx);
        if (T->z <= g) {
            T->z = g;
            T->air = false;
            // the landing: at an angle to the track it costs speed (the shocks help)
            float off = fabsf(wrap_pi(T->h - atan2f(s_ty[T->idx], s_tx[T->idx])));
            if (off > PI_F / 2) {
                off = PI_F - off;
            }
            float loss = clampf((off - 0.35f) * 1.2f, 0, 0.7f) * (1.0f - 0.15f * shocks_of(T));
            float k = 1.0f - fmaxf(0.05f, loss) - 0.06f * fmaxf(0, 4 - shocks_of(T)) / 4;
            T->vx *= k;
            T->vy *= k;
            if (loss > 0.25f) {
                T->spin_t = 0.35f;
            }
            if (IS_ME(T) && !s_demo) {
                // (a hard landing is louder; a landing at an angle also hits the body)
                fx(FX_LAND, (int)clampf(50 - T->vz * 7, 50, 100), 0.08f);
                if (loss > 0.25f) {
                    fx(FX_CRASH, 55, 0.1f);
                }
                s_shake = 0.3f;
            }
            for (int q = 0; q < 6; q++) {
                puff(T->x, T->y, frand(-2, 2), frand(-2, 2), 0.7f, 0.7f, rgb(200, 170, 130));
            }
        }
    }
    // the walls round the arena (the tyres; the concrete at the far side): the truck bounces back
    const float m = 1.2f;
    bool hit = false, hard = false;  // hard: the concrete wall
    if (T->x < m) {
        T->x = m;
        if (T->vx < 0) {
            T->vx *= -0.35f;
            T->vy *= 0.7f;
            hit = true;
        }
    }
    if (T->x > DK_ARENA_W - m) {
        T->x = DK_ARENA_W - m;
        if (T->vx > 0) {
            T->vx *= -0.35f;
            T->vy *= 0.7f;
            hit = true;
        }
    }
    if (T->y < m) {
        T->y = m;
        if (T->vy < 0) {
            T->vy *= -0.35f;
            T->vx *= 0.7f;
            hit = true;
        }
    }
    if (T->y > DK_ARENA_H - 1.0f) {
        T->y = DK_ARENA_H - 1.0f;
        if (T->vy > 0) {
            T->vy *= -0.35f;
            T->vx *= 0.7f;
            hit = hard = true;
        }
    }
    if (hit && T->bump_t <= 0) {
        T->bump_t = 0.3f;
        if (IS_ME(T)) {
            // the tyres: a soft thud (and a crash when it is fast); the concrete: a crash
            float sp = sqrtf(T->vx * T->vx + T->vy * T->vy) / 0.5f;
            int vol = (int)clampf(45 + sp * 5, 45, 100);
            if (!hard) {
                fx(FX_TYRE, vol, 0.12f);
            }
            if (hard || sp > 9) {
                fx(FX_CRASH, hard ? vol : vol * 3 / 5, 0.1f);
            }
            s_shake = 0.25f;
        }
    }
    if (T->bump_t > 0) {
        T->bump_t -= dt;
    }
    // the marshal: the truck turns only when it moves, so the
    // nose in a fence or a corner could hold it there. A truck that is nearly still for 1.6 s in the race (not on the
    // brake: that is the back gear) goes back onto the line of the track at its place, facing forward.
    const float spd = sqrtf(T->vx * T->vx + T->vy * T->vy);
    if (racing && !T->air && !T->done && !T->brake && !T->remote && spd < 1.2f) {
        T->stuck_t += dt;
    } else {
        T->stuck_t = 0;
    }
    if (T->stuck_t > 1.6f) {
        T->stuck_t = 0;
        for (int q = 0; q < 8; q++) {
            puff(T->x, T->y, frand(-2, 2), frand(-2, 2), 0.6f, 0.6f, rgb(210, 180, 140));
        }
        T->x = s_px[T->idx] + s_ty[T->idx] * frand(-0.6f, 0.6f);
        T->y = s_py[T->idx] - s_tx[T->idx] * frand(-0.6f, 0.6f);
        T->h = atan2f(s_ty[T->idx], s_tx[T->idx]);
        T->vx = s_tx[T->idx] * 2.0f;
        T->vy = s_ty[T->idx] * 2.0f;
        T->spin_t = 0;
        if (IS_ME(T) && !s_demo) {
            fx(FX_PICKUP, 60, 0.05f);
        }
    }
    // the laps: over the start line forward, after the half of the lap
    int ps = fwd(s_start, old), ns = fwd(s_start, T->idx);
    if (ns > s_n * 2 / 5 && ns < s_n * 3 / 5) {
        T->half = true;
    }
    if (ps > s_n - 40 && ns < 40) {
        if (T->half && T->lap < LAPS) {
            T->lap++;
            T->half = false;
            if (IS_ME(T) && racing && T->lap < LAPS) {
                fx(FX_LAP, 85, 0);
            }
            if (T->lap >= LAPS && !T->done) {
                T->done = true;
                T->done_t = s_race_t;
            }
        }
    }
    T->prog = T->lap + (float)fwd(s_start, T->idx) / s_n - (T->half || fwd(s_start, T->idx) < s_n / 2 ? 0 : 1);
    if (T->done) {
        T->prog = LAPS + 1.0f - T->done_t / 10000.0f;
    }
    // the way: against the line for a time = WRONG WAY
    float along = T->vx * s_tx[T->idx] + T->vy * s_ty[T->idx];
    T->wrong_t = along < -1.5f ? T->wrong_t + dt : 0;
    // the dust behind a fast truck off the track, the mud, a slide
    float sp = sqrtf(T->vx * T->vx + T->vy * T->vy);
    if (!T->air && (esp_random() & 7) == 0 && sp > 3 && (T->surf != SURF_TRACK || T->slip > 2.0f)) {
        uint16_t col = T->surf == SURF_MUD ? rgb(70, 50, 30) : rgb(210, 180, 140);
        puff(T->x - c * 1.2f, T->y - s * 1.2f, -T->vx * 0.2f + frand(-1, 1), -T->vy * 0.2f + frand(-1, 1), 0.6f, 0.5f,
             col);
    }
    if (T->nitro_t > 0 && (esp_random() & 1)) {
        puff(T->x - c * 1.5f, T->y - s * 1.5f, -c * 4 + frand(-1, 1), -s * 4 + frand(-1, 1), 0.3f, 0.45f,
             (esp_random() & 1) ? rgb(120, 200, 255) : rgb(255, 170, 60));
    }
}

// two trucks that touch push each other apart and change speed along the line between them
static void trucks_touch(void)
{
    for (int a = 0; a < NTRUCK; a++) {
        for (int b = a + 1; b < NTRUCK; b++) {
            truck_t *A = &s_tr[a], *B = &s_tr[b];
            if (A->remote && B->remote) {
                continue;
            }
            if (fabsf(A->z - B->z) > 0.8f) {
                continue;
            }
            float dx = B->x - A->x, dy = B->y - A->y, d = sqrtf(dx * dx + dy * dy);
            if (d >= 2.0f || d < 1e-4f) {
                continue;
            }
            // (the weights: 1 and 1; a truck of another board 0, the other one 2)
            float wa = A->remote ? 0 : B->remote ? 2 : 1, wb = 2 - wa;
            float nx = dx / d, ny = dy / d, o = (2.0f - d) / 2;
            A->x -= nx * o * wa;
            A->y -= ny * o * wa;
            B->x += nx * o * wb;
            B->y += ny * o * wb;
            float va = A->vx * nx + A->vy * ny, vb = B->vx * nx + B->vy * ny;
            if (va - vb > 0) {
                float j = (va - vb) * 0.65f;
                A->vx -= nx * j * wa;
                A->vy -= ny * j * wa;
                B->vx += nx * j * wb;
                B->vy += ny * j * wb;
                if ((IS_ME(A) || IS_ME(B)) && A->bump_t <= 0 && B->bump_t <= 0 && va - vb > 1.5f) {
                    fx(FX_CRASH, (int)clampf(50 + (va - vb) * 6, 50, 100), 0.12f);
                    s_shake = 0.2f;
                    A->bump_t = B->bump_t = 0.3f;
                }
            }
        }
    }
}

// ---------------------------------------------------------------- the bots
static void bot_drive(truck_t *T, float dt)
{
    float c = cosf(T->h), s = sinf(T->h);
    float v = T->vx * c + T->vy * s;
    T->lane_t -= dt;
    if (T->lane_t <= 0) {
        T->lane += frand(-0.8f, 0.8f);
        T->lane = clampf(T->lane, -1.3f, 1.3f);
        T->lane_t = frand(2, 5);
    }
    int look = 14 + (int)(fmaxf(v, 0) * 1.6f);
    int t = wrapi(T->idx + look);
    float tx = s_px[t] - s_ty[t] * T->lane, ty = s_py[t] + s_tx[t] * T->lane;
    float want = atan2f(ty - T->y, tx - T->x);
    float err = wrap_pi(want - T->h);
    T->steer = clampf(-err * 2.4f, -1, 1);
    // the speed: slow down for the turns ahead (the sharpest turn of the next part)
    float kmax = 0;
    for (int k = 8; k < 60; k += 4) {
        kmax = fmaxf(kmax, fabsf(curv_at(wrapi(T->idx + k + (int)(v * 0.6f)))));
    }
    float lat = 15.0f * T->skill + 1.5f * s_tier;
    float vsafe = kmax > 1e-3f ? sqrtf(lat / kmax) : 99.0f;
    T->brake = v > vsafe * 1.08f && v > 3.0f;
    // the nitro on a long straight part
    if (T->nitro > 0 && T->nitro_t <= 0 && kmax < 0.05f && v > 5.0f && (esp_random() % 200) == 0) {
        T->nitro--;
        T->nitro_t = 1.6f;
    }
    // stuck at a wall: back up a little
    float sp = sqrtf(T->vx * T->vx + T->vy * T->vy);
    if (sp < 0.8f && s_race_t > 3) {
        T->back_t += dt;
        if (T->back_t > 1.2f) {
            T->brake = true;
        }
        if (T->back_t > 2.6f) {
            T->back_t = 0;
        }
    }
}

// ---------------------------------------------------------------- the race: each step
static void places(void)
{
    for (int i = 0; i < NTRUCK; i++) {
        s_order[i] = i;
    }
    for (int i = 0; i < NTRUCK; i++) {
        for (int j = i + 1; j < NTRUCK; j++) {
            if (s_tr[s_order[j]].prog > s_tr[s_order[i]].prog) {
                int t = s_order[i];
                s_order[i] = s_order[j];
                s_order[j] = t;
            }
        }
    }
    for (int i = 0; i < NTRUCK; i++) {
        s_tr[s_order[i]].place = i + 1;
    }
}
static bool dn_client(void);
static void dn_took(uint8_t id);
static uint8_t s_item_id;
static void items_step(float dt, bool racing)
{
    if (!racing) {
        return;
    }
    if (dn_client()) {
        // (a friend race, not the host: the host puts the pick-ups; this board takes them for its truck only)
        truck_t *T = &s_tr[0];
        for (int i = 0; i < NITEM; i++) {
            item_t *I = &s_item[i];
            if (!I->on) {
                continue;
            }
            I->t += dt;
            float dx = T->x - I->x, dy = T->y - I->y;
            if (dx * dx + dy * dy < 1.6f && T->z < 0.8f && !T->done) {
                I->on = false;
                dn_took(I->id);
                if (I->kind == 1) {
                    if (T->nitro < MAX_CANS) {
                        T->nitro++;
                    }
                    fx(FX_PICKUP, 85, 0);
                    msg(TR("+1 NITRO", "+1 ניטרו"), 1.0f);
                } else {
                    s_pickup_money += 100;
                    fx(FX_COIN, 85, 0.04f);
                    msg("+$100", 1.0f);
                }
            }
        }
        return;
    }
    int alive = 0;
    for (int i = 0; i < NITEM; i++) {
        alive += s_item[i].on;
    }
    s_item_wait -= dt;
    if (alive < 2 && s_item_wait <= 0) {
        for (int i = 0; i < NITEM; i++) {
            if (!s_item[i].on) {
                int p = (int)(esp_random() % s_n);
                float sd = frand(-1.4f, 1.4f);
                s_item[i].on = true;
                s_item[i].kind = (esp_random() % 3) == 0 ? 1 : 0;
                s_item[i].x = s_px[p] - s_ty[p] * sd;
                s_item[i].y = s_py[p] + s_tx[p] * sd;
                s_item[i].t = 0;
                s_item[i].id = ++s_item_id ? s_item_id : ++s_item_id;
                break;
            }
        }
        s_item_wait = frand(4, 8);
    }
    for (int i = 0; i < NITEM; i++) {
        item_t *I = &s_item[i];
        if (!I->on) {
            continue;
        }
        I->t += dt;
        for (int t = 0; t < NTRUCK; t++) {
            truck_t *T = &s_tr[t];
            if (T->remote) {
                continue;  // (its board takes it, and tells the host)
            }
            float dx = T->x - I->x, dy = T->y - I->y;
            if (dx * dx + dy * dy < 1.6f && T->z < 0.8f) {
                I->on = false;
                if (I->kind == 1) {
                    T->nitro = T->nitro < MAX_CANS ? T->nitro + 1 : T->nitro;
                }
                if (IS_ME(T)) {
                    if (I->kind == 0) {
                        s_pickup_money += 100;
                        fx(FX_COIN, 85, 0.04f);
                        msg("+$100", 1.0f);
                    } else {
                        fx(FX_PICKUP, 85, 0);
                        msg(TR("+1 NITRO", "+1 ניטרו"), 1.0f);
                    }
                }
                break;
            }
        }
    }
}
static void race_finish(void);
static bool dn_race(void);
static void remote_step(truck_t *T, int t, float dt);
static void race_step(float dt)
{
    bool racing = s_gs == GS_RACE || s_demo;
    if (racing) {
        s_race_t += dt;
    }
    s_clock += dt;
    for (int t = 0; t < NTRUCK; t++) {
        truck_t *T = &s_tr[t];
        if (T->remote) {
            remote_step(T, t, dt);
            continue;
        }
        if (T->bot) {
            bot_drive(T, dt);
        } else if (s_touch_ctl) {
            float d = s_touch_x - SW / 2.0f;
            bool mid = fabsf(d) < SW * 0.08f;
            T->steer = s_touch_on && !mid ? clampf((d - (d > 0 ? 1 : -1) * SW * 0.08f) / (SW * 0.26f), -1, 1) : 0;
            T->brake = s_touch_on && mid;
        } else {
            T->steer = s_wheel_w * clampf(s_wheel / 0.7f, -1, 1) + (1 - s_wheel_w) * s_tilt_steer;
            if (fabsf(T->steer) < 0.05f) {
                T->steer = 0;
            }
            T->brake = s_touch_brake;
        }
        truck_step(T, dt, racing && !T->done);
    }
    trucks_touch();
    items_step(dt, racing);
    for (int i = 0; i < NPART; i++) {
        part_t *p = &s_part[i];
        if (p->t >= p->life) {
            continue;
        }
        p->t += dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vx *= 0.95f;
        p->vy *= 0.95f;
    }
    places();
    if (s_demo) {
        // the title: the race goes on and on
        for (int t = 0; t < NTRUCK; t++) {
            if (s_tr[t].done) {
                s_tr[t].done = false;
                s_tr[t].lap = 0;
            }
        }
        return;
    }
    if (s_gs == GS_RACE && s_tr[0].done && !dn_race()) {
        race_finish();
    }
}

// ---------------------------------------------------------------- the race: the end, the prize, the points
static const int PRIZE[4] = {400, 250, 150, 60};
static const int POINTS[4] = {9, 6, 3, 1};
static void race_finish(void)
{
    // the trucks still on the way get their places by their progress now
    places();
    int place = s_tr[0].place;
    s_prize = PRIZE[place - 1] + s_pickup_money;
    s_money += s_prize;
    for (int t = 0; t < NTRUCK; t++) {
        s_pts[t] += POINTS[s_tr[t].place - 1];
    }
    s_cans = s_tr[0].nitro;
    s_gs = GS_RESULT;
    s_gt = 0;
    sound_off();  // (the engine stops under the jingle)
    fx(place == 1 ? FX_WIN : FX_LOSE, 90, 0);
    ESP_LOGI(TAG, "race %d (%s): place %d, time %.1f s, prize %d", s_race_k, s_A->name, place, (double)s_race_t,
             s_prize);
    // the next race of the season (a new season after the last one: on the result screen)
    s_race_k++;
    if (s_race_k >= DK_NARENA) {
        s_gs = GS_SEASON;
        s_race_k = 0;
    }
    save_all();
    if (s_gs == GS_SEASON) {
        // (the store gets the next season now: the points go to zero and the bots get faster. A player who leaves on
        // the season screen with no tap on NEXT then does not keep the old points and the old season. The screen still
        // shows the points of this season; NEXT does new_season(true), which writes the same values.)
        int keep_pts[NTRUCK], keep_tier = s_tier;
        memcpy(keep_pts, s_pts, sizeof keep_pts);
        memset(s_pts, 0, sizeof s_pts);
        if (s_tier < 9) {
            s_tier++;
        }
        save_all();
        memcpy(s_pts, keep_pts, sizeof s_pts);
        s_tier = keep_tier;
    }
    s_race_k = s_gs == GS_SEASON ? DK_NARENA - 1 : s_race_k;
}

// ---------------------------------------------------------------- the wheel and the camera
static void wheel_read(float dt)
{
    float gx = 0, gy = 0;
    if (imu_ok()) {
        imu_gravity_logical(&gx, &gy);
    }
    float mag = sqrtf(gx * gx + gy * gy);
    float w = clampf((mag - 0.35f) / 0.3f, 0, 1);
    float a = mag > 0.2f ? atan2f(gx, gy) : 0;
    float rel = wrap_pi(a - s_wheel0);
    float k = clampf(dt / 0.08f, 0, 1);
    s_wheel += (clampf(rel, -1.4f, 1.4f) - s_wheel) * k;
    s_wheel_w += (w - s_wheel_w) * k;
    float tx = 0, ty = 0;
    imu_tilt_logical(&tx, &ty);
    s_tilt_steer = clampf(tx / 0.35f, -1, 1);
    if (fabsf(s_wheel) < 0.05f) {
        s_wheel *= 0.5f;
    }
}
static void wheel_center(void)
{
    float gx = 0, gy = 0;
    if (imu_ok()) {
        imu_gravity_logical(&gx, &gy);
    }
    float a = sqrtf(gx * gx + gy * gy) > 0.6f ? atan2f(gx, gy) : 0;
    s_wheel0 = clampf(a, -0.35f, 0.35f);
    imu_set_level();
}
#if DK_FIXED
static float s_cu, s_cv;  // the picture point at the centre of the screen
// a place of the track (x, y, height z) in the picture
static inline void project(float x, float y, float z, float *u, float *v)
{
    *u = DK_U0 + x * DK_PPU;
    *v = (DK_VTOP - ((y - DK_YC) * DK_SP + z * DK_CP)) * DK_PPU;
}
static float zoom_fit(void)
{
    return fminf((float)SW / DK_AW, (float)SH / DK_AH);
}
static float zoom_follow(void)
{
    return BAR ? 1.2f : 1.0f;
}
static void follow_point(float *u, float *v)
{
    truck_t *P = &s_tr[0];
    project(P->x + P->vx * 0.35f, P->y + P->vy * 0.35f, 0, u, v);
}
static void clamp_view(float z, float *u, float *v)
{
    float hw = SW / 2.0f / z, hh = SH / 2.0f / z;
    *u = DK_AW > 2 * hw ? clampf(*u, hw, DK_AW - hw) : DK_AW / 2.0f;
    *v = DK_AH > 2 * hh ? clampf(*v, hh, DK_AH - hh) : DK_AH / 2.0f;
}
static void camera_step(float dt)
{
    s_view_rot = 0;
    s_anchor_x = SW / 2.0f;
    s_anchor_y = SH / 2.0f;
    truck_t *P = &s_tr[0];
    float sp = sqrtf(P->vx * P->vx + P->vy * P->vy);
    float zf = zoom_follow() - 0.15f * clampf(sp / 12.0f, 0, 1);
    float zt, ut, vt;
    if (s_gs == GS_COUNT) {
        // the kick-off: the whole arena
        zt = zoom_fit();
        ut = DK_AW / 2.0f;
        vt = DK_AH / 2.0f;
    } else if (s_demo) {
        zt = BAR ? 0.6f : 0.55f;
        follow_point(&ut, &vt);
    } else {
        // after GO: zoom in on the truck in 1.2 s, then follow it
        float t = clampf(s_race_t / 1.2f, 0, 1);
        t = t * t * (3 - 2 * t);
        follow_point(&ut, &vt);
        zt = zoom_fit() + (zf - zoom_fit()) * t;
        ut = DK_AW / 2.0f + (ut - DK_AW / 2.0f) * t;
        vt = DK_AH / 2.0f + (vt - DK_AH / 2.0f) * t;
        if (t >= 1) {
            float k = clampf(dt / 0.1f, 0, 1);
            s_zoom += (zt - s_zoom) * clampf(dt / 0.6f, 0, 1);
            if (!R_RND) {
                clamp_view(s_zoom, &ut, &vt);  // (the round frame: the truck stays in the middle of the circle)
            }
            s_cu += (ut - s_cu) * k;
            s_cv += (vt - s_cv) * k;
            return;
        }
    }
    s_zoom = zt;
    clamp_view(s_zoom, &ut, &vt);
    s_cu = ut;
    s_cv = vt;
}
static void to_screen3(float x, float y, float z, float *sx, float *sy)
{
    float u, v;
    project(x, y, z, &u, &v);
    *sx = s_anchor_x + (u - s_cu) * s_zoom;
    *sy = s_anchor_y + (v - s_cv) * s_zoom;
}
static void to_screen(float x, float y, float *sx, float *sy)
{
    to_screen3(x, y, 0, sx, sy);
}
#else
static void camera_step(float dt)
{
    truck_t *P = &s_tr[0];
    float sp = sqrtf(P->vx * P->vx + P->vy * P->vy);
    float view = s_demo || s_touch_ctl ? 0 : s_wheel * s_wheel_w;  // the screen turns with the board
    s_view_rot = view;
    // psi: the turn of the arena on the screen (the truck points up to the gravity)
    float want = P->h - PI_F / 2 - view;
    float k = clampf(dt / 0.12f, 0, 1);
    s_psi = wrap_pi(s_psi + wrap_pi(want - s_psi) * k);
    float zt = (BAR ? 1.0f : 1.05f) - 0.28f * clampf(sp / 12.0f, 0, 1);
    if (s_gs == GS_COUNT) {
        zt = 1.15f;
    }
    s_zoom += (zt - s_zoom) * clampf(dt / 0.6f, 0, 1);
    s_cam_x += (P->x - s_cam_x) * clampf(dt / 0.05f, 0, 1);
    s_cam_y += (P->y - s_cam_y) * clampf(dt / 0.05f, 0, 1);
    // the anchor: the truck low on the screen (to the gravity), more track ahead
    float ux = -sinf(view), uy = -cosf(view);
    float hx = SW / 2.0f, hy = SH / 2.0f;
    float tx = fabsf(ux) > 1e-3f ? hx / fabsf(ux) : 1e9f, ty = fabsf(uy) > 1e-3f ? hy / fabsf(uy) : 1e9f;
    float t = fminf(tx, ty);
    float D = fmaxf(0, fminf(t - (BAR ? 34 : 50), t * 0.62f));
    s_anchor_x = hx - ux * D;
    s_anchor_y = hy - uy * D;
}
// a place of the arena on the screen
static void to_screen(float x, float y, float *sx, float *sy)
{
    float sc = DK_PPU * s_zoom;
    float dx = (x - s_cam_x) * sc, dy = -(y - s_cam_y) * sc;
    float c = cosf(s_psi), s = sinf(s_psi);
    *sx = s_anchor_x + c * dx - s * dy;
    *sy = s_anchor_y + s * dx + c * dy;
}
#endif

// ---------------------------------------------------------------- drawing: the turned arena (two cores)
static int s_split;
static int64_t s_us[2];
static int s_band_y0[2], s_band_y1[2];
static uint16_t s_ground;
#if DK_FIXED
static void draw_rows(int y0, int y1)
{
    float k = 1.0f / s_zoom;  // picture px for one screen px
    int W = SW;
    int32_t du = (int32_t)(k * 65536);
    float u0 = s_cu - s_anchor_x * k;
    // the screen px whose picture point is in the picture: a <= x <= b
    int a = (int)ceilf(-u0 / k), b = (int)floorf((DK_AW - 1 - u0) / k);
    if (a < 0) {
        a = 0;
    }
    if (b > W - 1) {
        b = W - 1;
    }
    for (int y = y0; y < y1; y++) {
        float v = s_cv + (y - s_anchor_y) * k;
        uint16_t *dst = g_fb + y * W;
        int xl = 0, xr = W;  // the columns that show (the round frame: only the span in the circle)
#if R_RND
        {
            int hw = rnd_half(y, y + 1) + 2;
            xl = W / 2 - hw < 0 ? 0 : W / 2 - hw;
            xr = W / 2 + hw > W ? W : W / 2 + hw;
        }
#endif
        int ra = a > xl ? a : xl, rb = b < xr - 1 ? b : xr - 1;
        if (v < 0 || v >= DK_AH || rb < ra) {
            for (int x = xl; x < xr; x++) {
                dst[x] = s_ground;
            }
            continue;
        }
        const uint16_t *row = s_tex + (int)v * DK_AW;
        for (int x = xl; x < ra; x++) {
            dst[x] = s_ground;
        }
        for (int x = rb + 1; x < xr; x++) {
            dst[x] = s_ground;
        }
        int32_t u = (int32_t)((u0 + ra * k) * 65536);
        for (int x = ra; x <= rb; x++, u += du) {
            uint32_t uu = (uint32_t)u >> 16;
            dst[x] = row[uu < DK_AW ? uu : DK_AW - 1];
        }
    }
}
#else
static void draw_rows(int y0, int y1)
{
    float sc = DK_PPU * s_zoom, k = DK_PPU / sc;  // picture px for one screen px
    float c = cosf(s_psi), s = sinf(s_psi);
    float U0 = (s_cam_x + DK_OX) * DK_PPU, V0 = (DK_OY - s_cam_y) * DK_PPU;
    int32_t du = (int32_t)(k * c * 65536), dv = (int32_t)(-k * s * 65536);
    int W = SW;
    for (int y = y0; y < y1; y++) {
        float rx = -s_anchor_x, ry = y - s_anchor_y;
        float u0 = U0 + k * (c * rx + s * ry), v0 = V0 + k * (-s * rx + c * ry);
        uint16_t *dst = g_fb + y * W;
        // the screen px whose picture point is in the picture: a <= x < b
        float a = 0, b = W;
        float du_f = k * c, dv_f = -k * s;
        if (fabsf(du_f) < 1e-6f) {
            if (u0 < 0 || u0 >= DK_AW - 1) {
                b = -1;
            }
        } else {
            float x0 = (0 - u0) / du_f, x1 = (DK_AW - 1 - u0) / du_f;
            if (x0 > x1) {
                float t = x0;
                x0 = x1;
                x1 = t;
            }
            a = fmaxf(a, x0);
            b = fminf(b, x1);
        }
        if (fabsf(dv_f) < 1e-6f) {
            if (v0 < 0 || v0 >= DK_AH - 1) {
                b = -1;
            }
        } else {
            float x0 = (0 - v0) / dv_f, x1 = (DK_AH - 1 - v0) / dv_f;
            if (x0 > x1) {
                float t = x0;
                x0 = x1;
                x1 = t;
            }
            a = fmaxf(a, x0);
            b = fminf(b, x1);
        }
        int ia = (int)ceilf(a), ib = (int)floorf(b);
        if (ia < 0) {
            ia = 0;
        }
        if (ib > W - 1) {
            ib = W - 1;
        }
        if (ib < ia) {
            for (int x = 0; x < W; x++) {
                dst[x] = s_ground;
            }
            continue;
        }
        for (int x = 0; x < ia; x++) {
            dst[x] = s_ground;
        }
        for (int x = ib + 1; x < W; x++) {
            dst[x] = s_ground;
        }
        int32_t u = (int32_t)((u0 + du_f * ia) * 65536), v = (int32_t)((v0 + dv_f * ia) * 65536);
        const uint16_t *tex = s_tex;
        for (int x = ia; x <= ib; x++, u += du, v += dv) {
            uint32_t uu = (uint32_t)u >> 16, vv = (uint32_t)v >> 16;
            if (uu >= DK_AW) {
                uu = DK_AW - 1;
            }
            if (vv >= DK_AH) {
                vv = DK_AH - 1;
            }
            dst[x] = tex[tidx(uu, vv)];
        }
    }
}
#endif
static void draw_band(int b)
{
    int64_t t0 = esp_timer_get_time();
    draw_rows(s_band_y0[b], s_band_y1[b]);
    s_us[b] = esp_timer_get_time() - t0;
}
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static TaskHandle_t s_helper;
static SemaphoreHandle_t s_done;
static volatile bool s_helper_quit;
static void helper_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_helper_quit) {
            break;
        }
        draw_band(0);
        xSemaphoreGive(s_done);
    }
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}
static void helper_start(void)
{
    if (s_helper) {
        return;
    }
    s_done = xSemaphoreCreateBinary();
    s_helper_quit = false;
    if (!s_done || xTaskCreatePinnedToCore(helper_task, "dk_draw", 3072, NULL, 3, &s_helper, 1) != pdPASS) {
        s_helper = NULL;
        ESP_LOGW(TAG, "no helper task: one core draws");
    }
}
static void helper_stop(void)
{
    if (!s_helper) {
        return;
    }
    s_helper_quit = true;
    xTaskNotifyGive(s_helper);
    xSemaphoreTake(s_done, portMAX_DELAY);
    vSemaphoreDelete(s_done);
    s_helper = NULL;
    s_done = NULL;
}
#endif
static int64_t s_frame_us, s_frame_n, s_frame_sum;
static void draw_arena(void)
{
    if (!s_tex || s_tex_arena < 0) {
        gfx_fill(rgb(60, 40, 28));
        return;
    }
    s_ground = s_A ? s_A->ground : rgb(60, 40, 28);
    if (s_split < 20 || s_split > SH - 20) {
        s_split = SH / 2;
    }
    s_band_y0[0] = 0;
    s_band_y1[0] = s_split;
    s_band_y0[1] = s_split;
    s_band_y1[1] = SH;
    int64_t t0 = esp_timer_get_time();
#ifdef ESP_PLATFORM
    if (s_helper) {
        xTaskNotifyGive(s_helper);
        draw_band(1);
        xSemaphoreTake(s_done, portMAX_DELAY);
    } else {
        draw_band(0);
        draw_band(1);
    }
#else
    draw_band(0);
    draw_band(1);
#endif
    if (s_us[0] > s_us[1] * 11 / 10 && s_split > 30) {
        s_split -= 2;
    } else if (s_us[1] > s_us[0] * 11 / 10 && s_split < SH - 30) {
        s_split += 2;
    }
    s_frame_sum += esp_timer_get_time() - t0;
    if (++s_frame_n >= 120) {
        s_frame_us = s_frame_sum / s_frame_n;
        ESP_LOGI(TAG, "the arena: %d us a frame (split %d)", (int)s_frame_us, s_split);
        s_frame_n = s_frame_sum = 0;
    }
}

// ---------------------------------------------------------------- drawing: the sprites (decoded, scaled, alpha)
#define SPMAX 96
static uint16_t *s_spc;  // the decoded sprite (internal RAM): colours
static uint8_t *s_spa;   // and alpha
static inline uint16_t sw16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
static inline void blend(uint16_t *d, uint16_t src, int a)
{
    uint16_t x = sw16(*d), y = sw16(src);
    int r = (x >> 11), g = (x >> 5) & 63, b = x & 31;
    r += (((y >> 11) - r) * a) >> 8;
    g += ((((y >> 5) & 63) - g) * a) >> 8;
    b += (((y & 31) - b) * a) >> 8;
    *d = sw16((uint16_t)((r << 11) | (g << 5) | b));
}
// a box at alpha a (0..255), its corners cut round by r (the ABI has no ui_rrect_a)
static void rect_a(int x, int y, int w, int h, int r, uint16_t c, int a)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        int cut = 0;
        int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
        if (dy > 0) {
            cut = r - (int)sqrtf((float)(r * r - dy * dy));
        }
        uint16_t *row = g_fb + yy * SW;
        for (int i = cut; i < w - cut; i++) {
            int xx = x + i;
            if (xx >= 0 && xx < SW) {
                blend(&row[xx], c, a);
            }
        }
    }
}
// a line of text from x (the ABI has ui_text_c only)
static void text_l(const kfont_t *f, int x, int base, const char *t, uint16_t c)
{
    ui_text_c(f, x + ui_text_w(f, t) / 2, base, t, c);
}
static bool sprite_decode(int k, int *w, int *h)
{
    const dk_sprite_t *S = &DK_SPRITES[k];
    if (S->w > SPMAX || S->h > SPMAX || !s_spc) {
        return false;
    }
    const uint8_t *base = DK_SPR + S->off;
    const uint16_t *pal = DK_PAL[S->pal];
    for (int y = 0; y < S->h; y++) {
        uint16_t ro = (uint16_t)(base[2 * y] | (base[2 * y + 1] << 8));
        const uint8_t *p = base + ro;
        int x = 0;
        while (x < S->w) {
            int t = *p >> 6, n = (*p & 63) + 1;
            p++;
            for (int j = 0; j < n && x < S->w; j++, x++) {
                int o = y * SPMAX + x;
                if (t == 0) {
                    s_spa[o] = 0;
                    continue;
                }
                s_spc[o] = pal[p[0]];
                if (t == 1) {
                    s_spa[o] = 255;
                    p += 1;
                } else {
                    s_spa[o] = p[1];
                    p += 2;
                }
            }
        }
    }
    *w = S->w;
    *h = S->h;
    return true;
}
// sprite k with its centre at (cx, cy), f screen px for one sprite px, alpha a (0..256)
static void sprite_draw(int k, float cx, float cy, float f, int alpha)
{
    int w, h;
    if (!sprite_decode(k, &w, &h)) {
        return;
    }
    const dk_sprite_t *S = &DK_SPRITES[k];
    int x0 = (int)floorf(cx - S->ax * f), y0 = (int)floorf(cy - S->ay * f);
    int dw = (int)(w * f + 0.5f), dh = (int)(h * f + 0.5f);
    float inv = 1.0f / f;
    for (int j = 0; j < dh; j++) {
        int y = y0 + j;
        if (y < 0 || y >= SH) {
            continue;
        }
        int sy = (int)(j * inv);
        if (sy >= h) {
            sy = h - 1;
        }
        uint16_t *row = g_fb + y * SW;
        for (int i = 0; i < dw; i++) {
            int x = x0 + i;
            if (x < 0 || x >= SW) {
                continue;
            }
            int sx = (int)(i * inv);
            if (sx >= w) {
                sx = w - 1;
            }
            int a = s_spa[sy * SPMAX + sx];
            if (!a) {
                continue;
            }
            a = a * alpha >> 8;
            if (a >= 250) {
                row[x] = s_spc[sy * SPMAX + sx];
            } else {
                blend(&row[x], s_spc[sy * SPMAX + sx], a);
            }
        }
    }
}
#if DK_FIXED
// a soft oval at alpha a (the shadows)
static void oval_a(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    int y0 = (int)(cy - ry), y1 = (int)(cy + ry);
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= SH) {
            continue;
        }
        float t = (y - cy) / ry, w = rx * sqrtf(fmaxf(0, 1 - t * t));
        int x0 = (int)(cx - w), x1 = (int)(cx + w);
        uint16_t *row = g_fb + y * SW;
        for (int x = x0; x <= x1; x++) {
            if (x >= 0 && x < SW) {
                blend(&row[x], c, a);
            }
        }
    }
}
static void draw_things(void)
{
    float sc = DK_PPU * s_zoom;  // screen px for one unit
    float f = sc / DK_SPPU;
    for (int i = 0; i < NITEM; i++) {
        item_t *I = &s_item[i];
        if (!I->on) {
            continue;
        }
        float sx, sy, gx, gy;
        to_screen3(I->x, I->y, 0.45f + 0.12f * sinf(I->t * 5), &sx, &sy);
        to_screen(I->x, I->y, &gx, &gy);
        if (sx < -30 || sx > SW + 30 || sy < -30 || sy > SH + 30) {
            continue;
        }
        oval_a(gx, gy, 0.45f * sc, 0.45f * sc * DK_SP, rgb(0, 0, 0), 80);
        sprite_draw(I->kind ? DK_SP_NITRO : DK_SP_MONEY, sx, sy, f * 0.8f, 256);
    }
    for (int i = 0; i < NPART; i++) {
        part_t *p = &s_part[i];
        if (p->t >= p->life) {
            continue;
        }
        float sx, sy;
        to_screen3(p->x, p->y, 0.3f + p->t, &sx, &sy);
        float u = p->t / p->life;
        ui_circle((int)sx, (int)sy, (int)(p->r * sc * (0.6f + u)), p->c, (int)(150 * (1 - u)));
    }
    // the far trucks first (the depth of the view), each over its shadow on the ground
    int ord[NTRUCK];
    for (int i = 0; i < NTRUCK; i++) {
        ord[i] = i;
    }
    for (int i = 0; i < NTRUCK; i++) {
        for (int j = i + 1; j < NTRUCK; j++) {
            if (s_tr[ord[j]].y > s_tr[ord[i]].y) {
                int t = ord[i];
                ord[i] = ord[j];
                ord[j] = t;
            }
        }
    }
    for (int q = 0; q < NTRUCK; q++) {
        truck_t *T = &s_tr[ord[q]];
        float gx, gy, sx, sy;
        to_screen(T->x, T->y, &gx, &gy);
        to_screen3(T->x, T->y, T->z, &sx, &sy);
        if (sx < -60 || sx > SW + 60 || sy < -60 || sy > SH + 60) {
            continue;
        }
        oval_a(gx + 0.15f * sc, gy + 0.1f * sc, 1.25f * sc, 1.25f * sc * DK_SP, rgb(0, 0, 0), T->air ? 70 : 100);
        int k = (int)lroundf(T->h / (2 * PI_F) * DK_NHEAD) % DK_NHEAD;
        if (k < 0) {
            k += DK_NHEAD;
        }
        sprite_draw(DK_SP_TRUCK(T->col, k), sx, sy, f, 256);
    }
}
static const uint16_t *col_of(int c);
// the small map in a corner: the track from above, a dot for each truck (the player's dot blinks)
#define MAP_H (BAR ? 62 : 58)
#define MAP_W ((int)(MAP_H * DK_ARENA_W / DK_ARENA_H))
#define MAP_X (R_RND ? 34 : 6)  // (the round frame: in the circle, at the left)
#define MAP_Y (SH - MAP_H - (R_RND ? 70 : 6))
static void map_xy(float x, float y, int *mx, int *my)
{
    *mx = MAP_X + 3 + (int)(x / DK_ARENA_W * (MAP_W - 6));
    *my = MAP_Y + 3 + (int)((1 - y / DK_ARENA_H) * (MAP_H - 6));
}
static void draw_map(void)
{
    rect_a(MAP_X, MAP_Y, MAP_W, MAP_H, 6, rgb(10, 10, 14), 150);
    for (int i = 0; i < s_n; i += 5) {
        int mx, my;
        map_xy(s_px[i], s_py[i], &mx, &my);
        gfx_fill_rect(mx, my, 2, 2, rgb(200, 160, 110));
    }
    if (s_start >= 0) {
        int mx, my;
        map_xy(s_px[s_start], s_py[s_start], &mx, &my);
        gfx_fill_rect(mx - 1, my - 1, 3, 3, rgb(255, 255, 255));
    }
    for (int t = NTRUCK - 1; t >= 0; t--) {
        truck_t *T = &s_tr[t];
        int mx, my;
        map_xy(T->x, T->y, &mx, &my);
        if (IS_ME(T) && ((int)(s_race_t * 4) & 1)) {
            gfx_fill_rect(mx - 3, my - 3, 7, 7, rgb(255, 255, 255));
        }
        gfx_fill_rect(mx - 2, my - 2, 5, 5, *col_of(T->col));
    }
}
#else
static void draw_things(void)
{
    float sc = DK_PPU * s_zoom;
    // the pick-ups (a bob)
    for (int i = 0; i < NITEM; i++) {
        item_t *I = &s_item[i];
        if (!I->on) {
            continue;
        }
        float sx, sy;
        to_screen(I->x, I->y, &sx, &sy);
        if (sx < -30 || sx > SW + 30 || sy < -30 || sy > SH + 30) {
            continue;
        }
        float f = sc / DK_SPPU * (1.0f + 0.08f * sinf(I->t * 6));
        ui_circle((int)sx + 2, (int)sy + 3, (int)(0.45f * sc), rgb(0, 0, 0), 70);
        sprite_draw(I->kind ? DK_SP_NITRO : DK_SP_MONEY, sx, sy, f, 256);
    }
    // the dust under the trucks
    for (int i = 0; i < NPART; i++) {
        part_t *p = &s_part[i];
        if (p->t >= p->life) {
            continue;
        }
        float sx, sy;
        to_screen(p->x, p->y, &sx, &sy);
        float u = p->t / p->life;
        ui_circle((int)sx, (int)sy, (int)(p->r * sc * (0.6f + u)), p->c, (int)(150 * (1 - u)));
    }
    // the trucks: those on the ground first, then those in the air; a shadow under each
    int ord[NTRUCK];
    for (int i = 0; i < NTRUCK; i++) {
        ord[i] = i;
    }
    for (int i = 0; i < NTRUCK; i++) {
        for (int j = i + 1; j < NTRUCK; j++) {
            if (s_tr[ord[j]].z < s_tr[ord[i]].z) {
                int t = ord[i];
                ord[i] = ord[j];
                ord[j] = t;
            }
        }
    }
    for (int q = 0; q < NTRUCK; q++) {
        truck_t *T = &s_tr[ord[q]];
        float sx, sy;
        to_screen(T->x, T->y, &sx, &sy);
        if (sx < -60 || sx > SW + 60 || sy < -60 || sy > SH + 60) {
            continue;
        }
        // the shadow: away to the lower right of the arena picture (the sun), further when the truck is high
        float ox = (0.25f + 0.35f * T->z), oy = -(0.25f + 0.35f * T->z), shx, shy;
        to_screen(T->x + ox, T->y + oy, &shx, &shy);
        ui_circle((int)shx, (int)shy, (int)(1.15f * sc), rgb(0, 0, 0), T->air ? 70 : 95);
        // the picture of the heading on the screen: k = (heading - psi) as a part of a turn
        float a = T->h - s_psi;
        int k = (int)lroundf(a / (2 * PI_F) * DK_NHEAD) % DK_NHEAD;
        if (k < 0) {
            k += DK_NHEAD;
        }
        float f = sc / DK_SPPU * (1.0f + 0.12f * T->z);
        sprite_draw(DK_SP_TRUCK(T->col, k), sx, sy, f, 256);
    }
}
#endif

// ---------------------------------------------------------------- drawing: the HUD
static const uint16_t *col_of(int c)
{
    static uint16_t C[4];
    C[0] = rgb(216, 52, 42);
    C[1] = rgb(46, 111, 216);
    C[2] = rgb(242, 194, 30);
    C[3] = rgb(232, 232, 236);
    return &C[c & 3];
}
static void bolt(int x, int y, int s, uint16_t c)
{
    // a small lightning: two slanted boxes
    for (int j = 0; j < s; j++) {
        int xx = j < s / 2 ? x + s / 2 - j / 2 : x + s / 2 - (j - s / 2) / 2 - 1;
        gfx_fill_rect(xx, y + j, s / 3 + 1, 1, c);
    }
}
#define NITRO_X (SW - (R_RND ? 72 : 38))  // (the round frame: in the circle)
// (TOUCH: under the chips of the places, at the right; at the bottom right the thumb of a full right turn would hit it.
// THE WHEEL: at the bottom right, no finger steers there)
#define NITRO_Y (s_touch_ctl ? (BAR ? 23 : R_RND ? 70 : 25) + 6 + NITRO_R : SH - (R_RND ? 84 : 36))
#define NITRO_R 28
static const char *const PLACE_EN[4] = {"1ST", "2ND", "3RD", "4TH"};
static const char *const PLACE_HE[4] = {"1", "2", "3", "4"};
static void draw_hud(void)
{
    // a chip for each truck in the order of the places: its colour, the place, the lap, the nitro cans (and the money
    // of the player)
    int n = NTRUCK, gap = 3;
    int cw = (SW - 6 - gap * (n - 1)) / n, ch = BAR ? 20 : 22, x0 = 3, y0 = 3;
#if R_RND
    {
        // (the round frame: the row of the chips on the arc at the top, as wide as the circle there)
        y0 = 40;
        int hw = rnd_half(y0, y0 + ch) - 4;
        cw = (2 * hw - gap * (n - 1)) / n;
        x0 = SW / 2 - hw;
    }
#endif
    for (int i = 0; i < n; i++) {
        truck_t *T = &s_tr[s_order[i]];
        int x = x0 + i * (cw + gap), y = y0;
        uint16_t c = *col_of(T->col);
        rect_a(x, y, cw, ch, 5, rgb(12, 14, 22), 190);
        ui_rbox(x + 3, y + 3, 5, ch - 6, 2, c, c, 1);
        char b[24];
        const char *pl = s_lang || !BAR ? PLACE_HE[i] : PLACE_EN[i];  // (a narrow chip: the number only)
        text_l(&KF_LABEL, x + 12, y + ch - 6, pl, i == 0 ? rgb(255, 236, 160) : rgb(235, 238, 245));
        int lx = x + 12 + ui_text_w(&KF_LABEL, pl) + 6;
        // the lap (3.49: "2/4"; the 320 x 240 frame: "L2"), the nitro bolts, the money of the player
        if (BAR) {
            snprintf(b, sizeof b, "%d/%d", T->lap < LAPS ? T->lap + 1 : LAPS, LAPS);
        } else {
            snprintf(b, sizeof b, "L%d", T->lap < LAPS ? T->lap + 1 : LAPS);
        }
        text_l(&KF_LABEL, lx, y + ch - 6, b, IS_ME(T) ? rgb(255, 255, 255) : rgb(200, 210, 230));
        int bx = lx + ui_text_w(&KF_LABEL, b) + 6;
        for (int k = 0; k < T->nitro && k < (BAR ? 3 : 1) && cw >= 64; k++) {  // (a narrow chip: no bolt)
            bolt(bx + k * 8, y + 4, 12, rgb(90, 210, 255));
        }
        if (!BAR && T->nitro && cw >= 64) {
            snprintf(b, sizeof b, "%d", T->nitro);
            text_l(&KF_LABEL, bx + 10, y + ch - 6, b, rgb(90, 210, 255));
        } else if (BAR && T->nitro > 3) {
            snprintf(b, sizeof b, "+%d", T->nitro - 3);
            text_l(&KF_LABEL, bx + 26, y + ch - 6, b, rgb(90, 210, 255));
        }
        if (IS_ME(T) && BAR) {
            snprintf(b, sizeof b, "$%d", s_money + s_pickup_money);
            ui_text_c(&KF_LABEL, x + cw - 5 - ui_text_w(&KF_LABEL, b) / 2, y + ch - 6, b, rgb(140, 230, 140));
        }
    }
#if DK_FIXED
    if (s_gs == GS_RACE || s_gs == GS_COUNT) {
        draw_map();
    }
#endif
    // the NITRO place
    truck_t *P = &s_tr[0];
    bool can = P->nitro > 0 && P->nitro_t <= 0;
    ui_circle(NITRO_X, NITRO_Y, NITRO_R, rgb(20, 60, 110), can ? 170 : 90);
    ui_ring(NITRO_X, NITRO_Y, NITRO_R, 2, rgb(140, 220, 255), can ? 230 : 110);
    bolt(NITRO_X - 8, NITRO_Y - 14, 20, can ? rgb(120, 220, 255) : rgb(70, 110, 140));
    char b[8];
    snprintf(b, sizeof b, "%d", P->nitro);
    ui_text_c(&KF_LABEL, NITRO_X + 14, NITRO_Y + 18, b, rgb(220, 240, 255));
    // the messages: the count, WRONG WAY, a pick-up
    if (s_gs == GS_COUNT) {
        int k = 3 - (int)s_gt;
        char t[8];
        snprintf(t, sizeof t, "%d", k > 0 ? k : 1);
        ui_text_c(&KF_TITLE, SW / 2, SH / 2 + 18, t, rgb(255, 230, 120));
        // (the wheel is not plain to see: one line while the count runs. The 320 x 240 frame: two lines on a dark plate
        // over the small map and the NITRO button; one line there was cut at the edges and covered the map.)
        if (R_RND) {
            // (the round frame: the two lines under the count, in the wide middle of the circle)
            const char *l1 = s_touch_ctl ? TR("A FINGER LEFT OR RIGHT STEERS,", "אצבע בצד מסובבת,")
                                         : TR("HOLD THE BOARD UP", "החזיקו את הלוח");
            const char *l2 = s_touch_ctl ? TR("THE MIDDLE BRAKES", "באמצע בולמת") : TR("AS A WHEEL", "כמו הגה");
            int pw = ui_text_w(&KF_LABEL, l1) + 20;
            rect_a(SW / 2 - pw / 2, SH / 2 + 34, pw, 40, 8, rgb(10, 10, 14), 170);
            ui_text_c(&KF_LABEL, SW / 2, SH / 2 + 50, l1, rgb(255, 255, 255));
            ui_text_c(&KF_LABEL, SW / 2, SH / 2 + 67, l2, rgb(255, 255, 255));
        } else if (BAR) {
            ui_text_c(&KF_LABEL, SW / 2, SH - 10,
                      s_touch_ctl
                              ? TR("A FINGER LEFT OR RIGHT STEERS, THE MIDDLE BRAKES", "אצבע בצד מסובבת, באמצע בולמת")
                              : TR("HOLD THE BOARD UP AS A WHEEL", "החזיקו את הלוח כמו הגה"),
                      rgb(255, 255, 255));
        } else {
            const char *l1 = s_touch_ctl ? TR("A FINGER LEFT OR RIGHT STEERS,", "אצבע בצד מסובבת,")
                                         : TR("HOLD THE BOARD UP", "החזיקו את הלוח");
            const char *l2 = s_touch_ctl ? TR("THE MIDDLE BRAKES", "באמצע בולמת") : TR("AS A WHEEL", "כמו הגה");
            int pw = ui_text_w(&KF_LABEL, l1) + 20;
            rect_a(SW / 2 - pw / 2, SH - 104, pw, 40, 8, rgb(10, 10, 14), 170);
            ui_text_c(&KF_LABEL, SW / 2, SH - 88, l1, rgb(255, 255, 255));
            ui_text_c(&KF_LABEL, SW / 2, SH - 71, l2, rgb(255, 255, 255));
        }
    } else if (s_gs == GS_RACE && s_race_t < 1.0f) {
        ui_text_c(&KF_TITLE, SW / 2, SH / 2 + 18, TR("GO!", "סע!"), rgb(140, 255, 140));
    }
    if (dn_race() && P->done && s_gs == GS_RACE) {
        ui_text_c(&KF_BIG, SW / 2, SH / 2 + 10, TR("OVER THE LINE! WAIT FOR THE OTHERS", "סיימת! מחכים לאחרים"),
                  rgb(255, 236, 160));
    } else if (P->wrong_t > 1.0f && s_gs == GS_RACE) {
        ui_text_c(&KF_BIG, SW / 2, SH / 2 + 34, TR("WRONG WAY", "כיוון הפוך"), rgb(255, 90, 70));
    }
    if (s_msg_t > 0) {
        ui_text_c(&KF_BIG, SW / 2, BAR ? 60 : R_RND ? 100 : 70, s_msg, rgb(255, 240, 160));
    }
    if (P->lap == LAPS - 1 && s_gs == GS_RACE && fwd(s_start, P->idx) < s_n / 12) {
        ui_text_c(&KF_BIG, SW / 2, BAR ? 60 : R_RND ? 100 : 70, TR("LAST LAP", "הקפה אחרונה"), rgb(255, 200, 90));
    }
}

// ---------------------------------------------------------------- the screens out of the race
typedef struct {
    int x, y, w, h;
} box_t;
static bool in_box(box_t b, int x, int y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}
static void button(box_t b, const char *t, uint16_t fill, bool on)
{
    ui_rbox(b.x, b.y, b.w, b.h, 10, on ? fill : rgb(40, 42, 52), rgb(255, 236, 200), 2);
    ui_text_c(&KF_NAME, b.x + b.w / 2, b.y + b.h / 2 + 7, t, rgb(255, 255, 255));
}
#if BAR
static const box_t B_RACE = {438, 30, 190, 58}, B_GARAGE = {438, 98, 92, 52}, B_FRIENDS = {536, 98, 92, 52};
static const box_t B_NEXT = {470, 112, 160, 52};
#elif T5
static const box_t B_RACE = {160, 208, 320, 66}, B_GARAGE = {160, 284, 154, 58}, B_FRIENDS = {326, 284, 154, 58};
static const box_t B_NEXT = {440, 284, 180, 58};
#elif R_RND
// (the round frame: in the circle of the square frame of 310 px)
static const box_t B_RACE = {45, 164, 220, 48}, B_GARAGE = {43, 218, 108, 42}, B_FRIENDS = {159, 218, 108, 42};
static const box_t B_NEXT = {80, 244, 150, 44};
#else
static const box_t B_RACE = {20, 132, 280, 50}, B_GARAGE = {20, 186, 136, 46}, B_FRIENDS = {164, 186, 136, 46};
static const box_t B_NEXT = {160, 186, 150, 46};
#endif
static void shade(int x, int y, int w, int h, int a)
{
    // (a blend of each pixel of a big box costs about 10 ms: the darken of the firmware is fast)
    gfx_darken_rect(x, y, w, h, a > 170 ? 2 : 1);
}
static void draw_title(void)
{
    char b[64];
#if BAR
    shade(6, 6, 420, 160, 150);
    text_l(&KF_TITLE, 20, 58, "DIRT KINGS", rgb(242, 194, 30));
    snprintf(b, sizeof b, TR("SEASON %d   RACE %d OF %d", "עונה %d   מרוץ %d מתוך %d"), s_tier + 1, s_race_k + 1,
             DK_NARENA);
    text_l(&KF_NAME, 22, 92, b, rgb(235, 235, 240));
    snprintf(b, sizeof b, "%s", DK_ARENAS[s_race_k].name);
    text_l(&KF_BIG, 22, 126, b, rgb(255, 210, 150));
    snprintf(b, sizeof b, "$%d   %s %d", s_money, TR("NITRO", "ניטרו"), s_cans);
    text_l(&KF_NAME, 22, 154, b, rgb(150, 230, 150));
#elif T5
    shade(100, 20, 440, 176, 150);
    ui_text_c(&KF_TITLE, SW / 2, 74, "DIRT KINGS", rgb(242, 194, 30));
    snprintf(b, sizeof b, TR("SEASON %d · RACE %d/%d", "עונה %d · מרוץ %d/%d"), s_tier + 1, s_race_k + 1, DK_NARENA);
    ui_text_c(&KF_NAME, SW / 2, 110, b, rgb(235, 235, 240));
    ui_text_c(&KF_BIG, SW / 2, 148, DK_ARENAS[s_race_k].name, rgb(255, 210, 150));
    snprintf(b, sizeof b, "$%d   %s %d", s_money, TR("NITRO", "ניטרו"), s_cans);
    ui_text_c(&KF_NAME, SW / 2, 180, b, rgb(150, 230, 150));
#else
    const int ty = R_RND ? 30 : 0;  // (the round frame: down into the circle)
    shade(R_RND ? 40 : 8, 8 + ty, R_RND ? 230 : 304, 118, 150);
    ui_text_c(&KF_TITLE, SW / 2, 52 + ty, "DIRT KINGS", rgb(242, 194, 30));
    snprintf(b, sizeof b, TR("SEASON %d · RACE %d/%d", "עונה %d · מרוץ %d/%d"), s_tier + 1, s_race_k + 1, DK_NARENA);
    ui_text_c(&KF_NAME, SW / 2, 78 + ty, b, rgb(235, 235, 240));
    ui_text_c(&KF_BIG, SW / 2, 104 + ty, DK_ARENAS[s_race_k].name, rgb(255, 210, 150));
    snprintf(b, sizeof b, "$%d   %s %d", s_money, TR("NITRO", "ניטרו"), s_cans);
    ui_text_c(&KF_LABEL, SW / 2, 122 + ty, b, rgb(150, 230, 150));
#endif
    button(B_RACE, TR("RACE", "למרוץ"), rgb(190, 60, 40), true);
    button(B_GARAGE, TR("GARAGE", "מוסך"), rgb(60, 90, 150), true);
    button(B_FRIENDS, TR("FRIENDS", "חברים"), rgb(130, 70, 150), true);
}
static const char *truck_name(int t);
static void draw_result(void)
{
    shade(6, 6, SW - 12, SH - 12, 200);
    char b[64];
    int y0 = BAR ? 34 : T5 ? 50 : R_RND ? 70 : 40, lx = R_RND ? 50 : 20;  // (the round frame: in the circle)
    text_l(&KF_BIG, lx, y0, s_gs == GS_SEASON ? TR("THE SEASON IS OVER", "העונה נגמרה") : TR("THE RESULT", "התוצאה"),
           rgb(242, 194, 30));
    int lh = BAR ? 30 : T5 ? 50 : 32;
    // the order: the last race; the end of a season: the points of the season (a tie: the order of the last race)
    int ord[NTRUCK];
    for (int i = 0; i < NTRUCK; i++) {
        ord[i] = s_order[i];
    }
    if (s_gs == GS_SEASON) {
        for (int i = 1; i < NTRUCK; i++) {
            for (int j = i; j > 0 && s_pts[s_tr[ord[j]].col] > s_pts[s_tr[ord[j - 1]].col]; j--) {
                const int t = ord[j];
                ord[j] = ord[j - 1];
                ord[j - 1] = t;
            }
        }
    }
    for (int i = 0; i < NTRUCK; i++) {
        truck_t *T = &s_tr[ord[i]];
        int y = y0 + 12 + i * lh;
        ui_rbox(lx, y, 8, lh - 8, 2, *col_of(T->col), *col_of(T->col), 1);
        snprintf(b, sizeof b, "%s  %s", s_lang ? PLACE_HE[i] : PLACE_EN[i], truck_name(ord[i]));
        text_l(T5 ? &KF_BIG : &KF_NAME, lx + 16, y + lh - (T5 ? 16 : 12), b,
               IS_ME(T) ? rgb(255, 236, 160) : rgb(230, 230, 236));
        if (dn_race()) {
            continue;
        }
        snprintf(b, sizeof b, TR("%d PTS", "%d נק'"), s_pts[T->col]);
        text_l(T5 ? &KF_BIG : &KF_NAME,
               BAR     ? 250
               : T5    ? 300
               : R_RND ? 205
                       : 200,
               y + lh - (T5 ? 16 : 12), b, rgb(200, 210, 230));
    }
    snprintf(b, sizeof b, TR("PRIZE $%d", "פרס $%d"), s_prize);
    if (BAR) {
        text_l(&KF_BIG, 470, 64, b, rgb(150, 230, 150));
    } else if (T5) {
        text_l(&KF_BIG, 440, 92, b, rgb(150, 230, 150));
    } else {
        text_l(&KF_NAME, lx + (R_RND ? 10 : 0), R_RND ? 236 : 226, b, rgb(150, 230, 150));
    }
    if (s_gs == GS_SEASON) {
        int best = 0;
        for (int t = 1; t < NTRUCK; t++) {
            if (s_pts[t] > s_pts[best]) {
                best = t;
            }
        }
        const char *w = best == 0 ? TR("YOU ARE THE KING!", "אתה המלך!")
                                  : TR("NEXT SEASON: FASTER BOTS", "בעונה הבאה: בוטים מהירים");
        if (BAR) {
            text_l(&KF_LABEL, 440, 96, w, rgb(255, 210, 150));
        } else if (T5) {
            text_l(ui_text_w(&KF_NAME, w) <= SW - 450 ? &KF_NAME : &KF_LABEL, 440, 128, w, rgb(255, 210, 150));
        } else {
            text_l(&KF_LABEL, lx, R_RND ? 214 : 200, w, rgb(255, 210, 150));
        }
    }
    button(B_NEXT, TR("NEXT", "הבא"), rgb(60, 140, 70), s_gt > 1.0f);
}
// the garage: 5 cards (tyres, shocks, engine, top speed, a nitro can) and RACE
static const char *const UP_EN[5] = {"TYRES", "SHOCKS", "ENGINE", "TOP SPEED", "NITRO CAN"};
static const char *const UP_HE[5] = {"צמיגים", "בולמים", "מנוע", "מהירות", "ניטרו"};
static box_t garage_card(int i)
{
#if BAR
    box_t b = {6 + i * 104, 30, 98, 96};
#elif T5
    box_t b = {20 + (i % 3) * 204, i < 3 ? 64 : 204, 192, 128};
#elif R_RND
    box_t b = {31 + (i % 3) * 84, i < 3 ? 74 : 146, 80, 66};  // (the round frame: in the circle)
#else
    box_t b = {6 + (i % 3) * 104, i < 3 ? 34 : 112, 98, 72};
#endif
    return b;
}
#if BAR
static const box_t B_GRACE = {534, 30, 100, 96};
#elif T5
static const box_t B_GRACE = {428, 204, 192, 128};
#elif R_RND
static const box_t B_GRACE = {199, 146, 80, 66};
#else
static const box_t B_GRACE = {214, 112, 100, 72};
#endif
static int card_price(int i)
{
    if (i < UP_N) {
        return s_up[i] < 5 ? PRICE[s_up[i]] : -1;
    }
    return s_cans < MAX_CANS ? CAN_PRICE : -1;
}
static void draw_garage(void)
{
    gfx_fill(rgb(22, 20, 26));
    char b[48];
    snprintf(b, sizeof b, TR("THE GARAGE    $%d", "המוסך    $%d"), s_money);
    if (T5) {
        text_l(&KF_BIG, 20, 44, b, rgb(150, 230, 150));
    } else if (R_RND) {
        ui_text_c(&KF_NAME, SW / 2, 56, b, rgb(150, 230, 150));  // (the round frame: at the top of the circle)
    } else {
        text_l(&KF_NAME, 10, BAR ? 22 : 24, b, rgb(150, 230, 150));
    }
    for (int i = 0; i < 5; i++) {
        box_t c = garage_card(i);
        int pr = card_price(i);
        bool can = pr > 0 && pr <= s_money;
        ui_rbox(c.x, c.y, c.w, c.h, 9, i == s_garage_sel ? rgb(70, 60, 30) : rgb(36, 36, 46),
                can ? rgb(242, 194, 30) : rgb(90, 90, 100), 2);
        ui_text_c(T5 ? &KF_NAME : &KF_LABEL, c.x + c.w / 2, c.y + (T5 ? 32 : 20), s_lang ? UP_HE[i] : UP_EN[i],
                  rgb(240, 240, 245));
        // the level: 5 marks (the nitro card: the cans)
        int lv = i < UP_N ? s_up[i] : s_cans, mx = i < UP_N ? 5 : MAX_CANS;
        int mw = (c.w - 16) / mx;
        for (int k = 0; k < mx; k++) {
            gfx_fill_rect(c.x + 8 + k * mw,
                          c.y + (BAR  ? 36
                                 : T5 ? 50
                                      : 30),
                          mw - 2, T5 ? 12 : 8, k < lv ? rgb(242, 194, 30) : rgb(70, 70, 80));
        }
        if (pr > 0) {
            snprintf(b, sizeof b, "$%d", pr);
        } else {
            snprintf(b, sizeof b, "%s", TR("MAX", "מקסימום"));
        }
        ui_text_c(T5 ? &KF_BIG : &KF_NAME, c.x + c.w / 2,
                  c.y + c.h -
                          (BAR  ? 14
                           : T5 ? 22
                                : 10),
                  b, can ? rgb(150, 230, 150) : rgb(150, 150, 160));
    }
    ui_rbox(B_GRACE.x, B_GRACE.y, B_GRACE.w, B_GRACE.h, 10, rgb(190, 60, 40), rgb(255, 236, 200), 2);
    ui_text_c(T5 ? &KF_BIG : &KF_NAME, B_GRACE.x + B_GRACE.w / 2, B_GRACE.y + B_GRACE.h / 2 + (T5 ? 10 : 7),
              TR("RACE", "למרוץ"), rgb(255, 255, 255));
    if (BAR) {
        text_l(&KF_LABEL, 10, 160, TR("TAP A CARD: BUY", "נגיעה בכרטיס: קנייה"), rgb(170, 170, 180));
    } else if (T5) {
        text_l(&KF_NAME, 20, 354, TR("TAP A CARD: BUY", "נגיעה בכרטיס: קנייה"), rgb(170, 170, 180));
    } else if (R_RND) {
        ui_text_c(&KF_LABEL, SW / 2, 236, TR("TAP A CARD: BUY", "נגיעה בכרטיס: קנייה"), rgb(170, 170, 180));
    } else {
        text_l(&KF_LABEL, 10, 210, TR("TAP A CARD: BUY", "נגיעה בכרטיס: קנייה"), rgb(170, 170, 180));
    }
}
static void buy(int i)
{
    int pr = card_price(i);
    if (pr <= 0 || pr > s_money) {
        sfx_play(SFX_BACK);
        return;
    }
    s_money -= pr;
    if (i < UP_N) {
        s_up[i]++;
    } else {
        s_cans++;
    }
    save_all();
    fx(FX_COIN, 80, 0);
}

// ---------------------------------------------------------------- the sound: the engine and the dirt
// The engine has 4 gears: in a gear the pitch rises with the speed; at a shift up it falls (the drone glides to the new
// pitch, so a shift is a quick fall). UP[g] = the speed (a part of the top speed) where gear g goes up.
static const float UP[4] = {0.22f, 0.45f, 0.70f, 1.15f};
static int s_gear;
static void sound_off(void)
{
    audio_drone(0, 0, 0, W_SAW);
    audio_drone(1, 0, 0, W_NOISE);
    s_gear = 0;
}
static void sound_step(void)
{
    if (s_gs != GS_RACE && s_gs != GS_COUNT) {
        sound_off();
        return;
    }
    truck_t *P = &s_tr[0];
    float c = cosf(P->h), s = sinf(P->h);
    float v = fabsf(P->vx * c + P->vy * s);
    float r = clampf(v / vmax_of(P), 0, 1.4f);
    while (s_gear < 3 && r > UP[s_gear]) {
        s_gear++;
    }
    while (s_gear > 0 && r < UP[s_gear - 1] - 0.06f) {
        s_gear--;  // (a gap: no shifts up and down at one speed)
    }
    float lo = s_gear ? UP[s_gear - 1] : 0;
    float rpm = 0.3f + 0.7f * clampf((r - lo) / (UP[s_gear] - lo), 0, 1);
    if (P->air) {
        rpm = fminf(1.0f, rpm + 0.25f);  // (the wheels turn free in the air)
    }
    float hz = 40.0f + 95.0f * rpm + 8.0f * s_gear + (P->nitro_t > 0 ? 25 : 0);
    audio_drone(0, hz, 15 + (int)(7 * rpm), W_SAW);
    int nv = (int)clampf((P->slip - 1.0f) * 6 + (P->surf != SURF_TRACK ? 4 : 0) * (v > 2), 0, 22);
    audio_drone(1, P->surf == SURF_MUD ? 500.0f : 1800.0f, P->air ? 0 : nv, W_NOISE);
}

#include "dk_net.inc"

// ---------------------------------------------------------------- the states
static void title_go(void)
{
    dn_party_end();
    s_gs = GS_TITLE;
    s_gt = 0;
    race_init(s_race_k, true);
    sound_off();
}
static void race_go(void)
{
    if (dn_race()) {
        return;  // (a friend race: only the host starts it, from the room)
    }
    race_init(s_race_k, false);
    s_gs = GS_COUNT;
    s_gt = 0;
    wheel_center();
    ESP_LOGI(TAG, "race %d: %s, %d points, %.0f units a lap", s_race_k, s_A->name, s_n, (double)s_A->len);
}
static void garage_go(void)
{
    s_gs = GS_GARAGE;
    s_gt = 0;
    s_garage_sel = -1;
    sound_off();
}

// ---------------------------------------------------------------- the panel items
enum {
    DI_LANG,
    DI_CTL,
    DI_TITLE,
    DI_RESTART,
    DI_SEASON
};
static const char *const V_LANG[] = {"ENGLISH", "ENGLISH", "עברית", "עברית"};
static const char *const V_CTL[] = {"THE WHEEL", "הגה", "TOUCH", "מגע"};
static const pitem_t ITEMS[] = {
        {PI_PICK, DI_LANG, 2, 0, {"LANGUAGE", "שפה"}, V_LANG, DK_KEY "lang"},
        {PI_PICK, DI_CTL, 2, 0, {"STEERING", "היגוי"}, V_CTL, DK_KEY "ctl"},
        {PI_ACT, DI_TITLE, 0, 0, {"THE TITLE", "למסך הפתיחה"}, NULL, NULL},
        {PI_ACT2, DI_RESTART, 0, 0, {"RESTART", "מרוץ מחדש"}, NULL, NULL},
        {PI_ACT2, DI_SEASON, 0, 0, {"NEW SEASON", "עונה חדשה"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == DI_TITLE) {
        return s_gs == GS_COUNT || s_gs == GS_RACE || s_gs == GS_LOBBY || s_gs == GS_ROOM;
    }
    if (id == DI_RESTART) {
        return (s_gs == GS_COUNT || s_gs == GS_RACE) && !dn_race();
    }
    if (id == DI_CTL) {
        return imu_ok();  // (no motion sensor: the touch only)
    }
    if (id == DI_SEASON) {
        return !dn_race() && s_gs != GS_LOBBY && s_gs != GS_ROOM;
    }
    return true;
}
static void item_set(int id, int v)
{
    if (id == DI_LANG) {
        s_lang = v & 1;
    } else if (id == DI_CTL) {
        s_touch_ctl = (v & 1) || !imu_ok();
    } else if (id == DI_TITLE) {
        title_go();
    } else if (id == DI_RESTART) {
        race_go();
    } else if (id == DI_SEASON) {
        new_season(false);
        title_go();
    }
}
static void item_opened(void)
{
    sound_off();
}
static void item_resume(void)
{
    wheel_center();
}
static const pitems_t PANEL = {ITEMS, 5, item_shown, NULL, item_set, item_opened, item_resume};

// ---------------------------------------------------------------- the app
// (internal RAM only when 28 KB stays free after it for the WiFi and the system; else NULL: then PSRAM)
static void *int_alloc(size_t n)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > n + 28 * 1024
                   ? heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                   : NULL;
}
static void dk_enter(int64_t now)
{
    JPLAY(DK_OPEN);
    if (!s_spc) {
        s_spc = int_alloc(SPMAX * SPMAX * 2);
    }
    if (!s_spa) {
        s_spa = int_alloc(SPMAX * SPMAX);
    }
    if (!s_spc) {
        s_spc = heap_caps_malloc(SPMAX * SPMAX * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_spa) {
        s_spa = heap_caps_malloc(SPMAX * SPMAX, MALLOC_CAP_SPIRAM);
    }
    s_lang = store_get(DK_KEY "lang", 0) ? 1 : 0;  // (a missing key: SETUP > APP LANGUAGE gives it)
    s_touch_ctl = store_get(DK_KEY "ctl", 0) || !imu_ok();
    load_all();
    if (store_get(DK_KEY "up", 0xFFFFFFFF) == 0xFFFFFFFF) {
        new_season(false);
    }
    s_tex_arena = -1;
    sysui_panel_items(&PANEL);
    tfsfx_load_mem(DK_SFX_DATA, DK_SFX_LEN, FXN, FX_N < DK_SFX_N ? FX_N : DK_SFX_N, 42);  // (in the app file: no card)
    title_go();
#ifdef ESP_PLATFORM
    if (!R_RND) {
        helper_start();  // (the round panels: one core draws; core 1 sends the frame of 466 to the panel)
    }
#endif
    s_last = now;
    // (the argument "friends" opens the list of the races)
    if (!strcmp(app_launch_arg(), "friends")) {
        dn_lobby_go(now);
    }
}
static void dk_leave(void)
{
    dn_close();
#ifdef ESP_PLATFORM
    helper_stop();
#endif
    sound_off();
    tfsfx_free();
    heap_caps_free(s_tex);
    s_tex = NULL;
    heap_caps_free(s_px);
    s_px = NULL;
    heap_caps_free(s_spc);
    s_spc = NULL;
    heap_caps_free(s_spa);
    s_spa = NULL;
    s_tex_arena = -1;
}
static void dk_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    if (s_gs == GS_COUNT || s_gs == GS_RACE) {
        int dx = x - NITRO_X, dy = y - NITRO_Y;
        const int nr = NITRO_R + (s_touch_ctl ? 4 : 10);  // (TOUCH: the circle only: a steering thumb is near)
        bool on_nitro = dx * dx + dy * dy <= nr * nr;
        if (ev->kind == IN_DOWN) {
            if (on_nitro) {
                s_nitro_key = true;
            } else {
                s_touch_brake = true;
                s_touch_on = true;
                s_touch_x = x;
            }
        } else if (ev->kind == IN_MOVE && s_touch_on) {
            s_touch_x = x;
        } else if (ev->kind == IN_UP) {
            s_touch_brake = false;
            s_touch_on = false;
        }
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (s_gs == GS_LOBBY || s_gs == GS_ROOM) {
        dn_tap(x, y);
        return;
    }
    if (s_gs == GS_TITLE) {
        if (in_box(B_RACE, x, y)) {
            JPLAY(DK_OPEN);
            race_go();
        } else if (in_box(B_GARAGE, x, y)) {
            sfx_play(SFX_CLICK);
            garage_go();
        } else if (in_box(B_FRIENDS, x, y)) {
            sfx_play(SFX_CLICK);
            dn_lobby_go(s_now_ms);
        }
    } else if (s_gs == GS_RESULT || s_gs == GS_SEASON) {
        if (in_box(B_NEXT, x, y) && s_gt > 1.0f) {
            sfx_play(SFX_CLICK);
            if (dn_race()) {
                dn_back_to_room(s_now_ms);
                return;
            }
            if (s_gs == GS_SEASON) {
                new_season(true);
            }
            garage_go();
        }
    } else if (s_gs == GS_GARAGE) {
        if (in_box(B_GRACE, x, y)) {
            JPLAY(DK_OPEN);
            race_go();
            return;
        }
        for (int i = 0; i < 5; i++) {
            if (in_box(garage_card(i), x, y)) {
                s_garage_sel = i;
                buy(i);
            }
        }
    }
}
static void dk_update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    s_now_ms = now;
    dn_poll(now);
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt <= 0) {
        return;
    }
    s_gt += dt;
    if (s_msg_t > 0) {
        s_msg_t -= dt;
    }
    if (s_shake > 0) {
        s_shake -= dt;
    }
    wheel_read(dt);
    if (app_key_presses() > 0) {
        s_nitro_key = true;
    }
    if (s_gs == GS_COUNT && s_gt >= 3.0f) {
        s_gs = GS_RACE;
        s_gt = 0;
        sfx_play(SFX_GO);
    } else if (s_gs == GS_COUNT && (int)(s_gt + dt) != (int)s_gt) {
        sfx_play(SFX_BEEP);
    }
    if (s_nitro_key) {
        truck_t *P = &s_tr[0];
        if (s_gs == GS_RACE && P->nitro > 0 && P->nitro_t <= 0) {
            P->nitro--;
            P->nitro_t = 1.6f;
            tfsfx_play(FX_NITRO, 95, 0.85f);
        }
        s_nitro_key = false;
    }
    if (s_gs == GS_TITLE || s_gs == GS_COUNT || s_gs == GS_RACE || s_gs == GS_LOBBY || s_gs == GS_ROOM) {
        // fixed steps of the physics (stable at any frame rate)
        float h = 1.0f / 120.0f;
        while (dt > 1e-4f) {
            float st = dt > h ? h : dt;
            race_step(st);
            dt -= st;
            if (s_gs != GS_RACE && s_gs != GS_TITLE && s_gs != GS_COUNT && s_gs != GS_LOBBY && s_gs != GS_ROOM) {
                break;
            }
        }
        camera_step(1.0f / 30.0f);
    }
    sound_step();
    dn_send(now);
}
static void dk_render(int64_t now)
{
    (void)now;
    if (s_gs == GS_GARAGE) {
        draw_garage();
        return;
    }
    float shx = 0, shy = 0;
    if (s_shake > 0) {
        shx = frand(-3, 3) * s_shake * 4;
        shy = frand(-3, 3) * s_shake * 4;
    }
    s_anchor_x += shx;
    s_anchor_y += shy;
    draw_arena();
    draw_things();
    s_anchor_x -= shx;
    s_anchor_y -= shy;
    if (s_gs == GS_TITLE) {
        draw_title();
    } else if (s_gs == GS_LOBBY || s_gs == GS_ROOM) {
        dn_draw();
    } else if (s_gs == GS_RESULT || s_gs == GS_SEASON) {
        draw_result();
    } else {
        draw_hud();
    }
}
#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set is in each app; all are static and unused, so they give no warning)
#define IC_FN static __attribute__((unused))
static struct {
    uint16_t *fb;
    int w, h, c[4];
} ic_keep __attribute__((unused));
// start an icon: tfdraw draws on the screen and only in the box; ic_end gives tfdraw its old target and clip back
IC_FN void ic_begin(int x, int y, int s)
{
    ic_keep.fb = tfd_fb;
    ic_keep.w = tfd_w;
    ic_keep.h = tfd_h;
    ic_keep.c[0] = tfd_cx0;
    ic_keep.c[1] = tfd_cy0;
    ic_keep.c[2] = tfd_cx1;
    ic_keep.c[3] = tfd_cy1;
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
}
IC_FN void ic_end(void)
{
    tfd_target(ic_keep.fb, ic_keep.w, ic_keep.h);
    tfd_clip(ic_keep.c[0], ic_keep.c[1], ic_keep.c[2], ic_keep.c[3]);
}
// the rows j0 .. j1 - 1 of the round tile (the corner radius of the menu frame), a gradient from c0 to c1
IC_FN void ic_band(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    int r = s / 8 + 2;
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        uint16_t c = tfd_mix(c0, c1, j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        if (dy <= 0) {
            tfd_hspan(x, x + s, y + j, c, 256);
            continue;
        }
        int in = tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy)));
        tfd_hspan(x + in + 1, x + s - in - 1, y + j, c, 256);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, y + j, c, a);
            tfd_px(x + s - 1 - k, y + j, c, a);
        }
    }
}
// the box inside the round corners (a shape in it does not cover a corner)
IC_FN void ic_inner(int x, int y, int s)
{
    int k = (s / 8 + 2) * 3 / 10 + 1;
    tfd_clip(x + k, y + k, x + s - k, y + s - k);
}
// a filled ellipse with a smooth edge
IC_FN void ic_oval(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = fabsf(yy + 0.5f - cy), v = ry + 0.5f - d;
        if (v <= 0) {
            continue;
        }
        float q = d < ry ? d / ry : 1, hw = rx * sqrtf(1 - q * q), l = cx - hw, r = cx + hw;
        int aa = v >= 1 ? a : (int)(a * v);
        int il = tfd_ce(l), ir = tfd_fl(r);
        if (il > ir) {
            tfd_px(ir, yy, c, (int)(aa * (r - l)));
            continue;
        }
        tfd_hspan(il, ir, yy, c, aa);
        tfd_px(il - 1, yy, c, (int)(aa * (il - l)));
        tfd_px(ir, yy, c, (int)(aa * (r - ir)));
    }
}
// a ball in the light from the upper left: the dark rim, the body, the shine
IC_FN void ic_ball(float cx, float cy, float r, uint16_t dark, uint16_t body, uint16_t shine)
{
    tfd_disc(cx, cy, r, dark, 256);
    tfd_disc(cx - r * 0.1f, cy - r * 0.12f, r * 0.84f, body, 256);
    tfd_disc(cx - r * 0.34f, cy - r * 0.38f, r * 0.27f, shine, 200);
}
// a shape of n points (x, y pairs round 0, 0), turned by ang, k times bigger, at cx, cy
IC_FN void ic_shape(const float *p, int n, float cx, float cy, float k, float ang, uint16_t c, int a)
{
    float q[64], cs = fcos(ang) * k, sn = fsin(ang) * k;
    if (n > 32) {
        n = 32;
    }
    for (int i = 0; i < n; i++) {
        q[2 * i] = cx + p[2 * i] * cs - p[2 * i + 1] * sn;
        q[2 * i + 1] = cy + p[2 * i] * sn + p[2 * i + 1] * cs;
    }
    tfd_poly(q, n, c, a);
}
// A store app shows the frames of icon.bin, with at most 255 colours for all 16 frames: mk_app.py drops the low bits
// of each colour until they fit (most icons: 3 bits of red, 4 of green, 3 of blue). A colour on that grid stays as it
// is: ic_g gives the nearest one. ic_dband makes a gradient of grid colours with an ordered dither (few colours, no
// bands).
IC_FN uint16_t ic_g(int r, int g, int b)
{
    r = (r + 16) / 32 * 32;
    g = (g + 8) / 16 * 16;
    b = (b + 16) / 32 * 32;
    return rgb((uint8_t)(r > 224 ? 224 : r), (uint8_t)(g > 240 ? 240 : g), (uint8_t)(b > 224 ? 224 : b));
}
IC_FN void ic_dband(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    static const uint8_t IC_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int r0, g0, b0, r1, g1, b1, r = s / 8 + 2;
    tfd_unrgb(c0, &r0, &g0, &b0);
    tfd_unrgb(c1, &r1, &g1, &b1);
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        int yy = y + j;
        if (yy < tfd_cy0 || yy >= tfd_cy1 || (unsigned)yy >= (unsigned)tfd_h) {
            continue;
        }
        // (the colour of the row in 1/16 steps of a colour value; a grid step is 32 = 512 for red and blue, 16 = 256
        // for green)
        int t = j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0;
        int fr = r0 * 16 + ((r1 - r0) * 16 * t >> 8), fg = g0 * 16 + ((g1 - g0) * 16 * t >> 8),
            fb = b0 * 16 + ((b1 - b0) * 16 * t >> 8);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        int in = dy > 0 ? tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy))) : -1;
        uint16_t *row = tfd_fb + yy * tfd_w;
        for (int i = in + 1; i < s - in - 1; i++) {
            int xx = x + i;
            if (xx < tfd_cx0 || xx >= tfd_cx1 || (unsigned)xx >= (unsigned)tfd_w) {
                continue;
            }
            int th = IC_BAYER[(j & 3) * 4 + (i & 3)];
            int lr = (fr >> 9) + (((fr >> 5) & 15) > th), lg = (fg >> 8) + (((fg >> 4) & 15) > th),
                lb = (fb >> 9) + (((fb >> 5) & 15) > th);
            row[xx] = rgb((uint8_t)(lr > 7 ? 224 : lr * 32), (uint8_t)(lg > 15 ? 240 : lg * 16),
                          (uint8_t)(lb > 7 ? 224 : lb * 32));
        }
        uint16_t c = ic_g(fr >> 4, fg >> 4, fb >> 4);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, yy, c, a);
            tfd_px(x + s - 1 - k, yy, c, a);
        }
    }
}
// a span of one row from l to r (in pixels, with soft ends)
IC_FN void ic_span(float l, float r, int yy, uint16_t c, int a)
{
    if (r <= l) {
        return;
    }
    int il = tfd_ce(l), ir = tfd_fl(r);
    if (il > ir) {
        tfd_px(ir, yy, c, (int)(a * (r - l)));
        return;
    }
    tfd_hspan(il, ir, yy, c, a);
    tfd_px(il - 1, yy, c, (int)(a * (il - l)));
    tfd_px(ir, yy, c, (int)(a * (r - ir)));
}
// the ring of an ellipse (rx, ry: the outer radii; w: its width), made of spans: cheap also when it is big
IC_FN void ic_ering(float cx, float cy, float rx, float ry, float w, uint16_t c, int a)
{
    float ix = rx - w, iy = ry - w;
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = yy + 0.5f - cy, qo = d / ry;
        if (qo * qo >= 1) {
            continue;
        }
        float ho = rx * sqrtf(1 - qo * qo), qi = iy > 0 ? d / iy : 2;
        if (qi * qi >= 1) {
            ic_span(cx - ho, cx + ho, yy, c, a);
            continue;
        }
        float hi = ix * sqrtf(1 - qi * qi);
        ic_span(cx - ho, cx - hi, yy, c, a);
        ic_span(cx + hi, cx + ho, yy, c, a);
    }
}
// the icon: a red monster truck from the side in the dirt arena at night: it jumps over a mound (the nose comes up,
// the springs work, the big wheels turn), mud flies from its back wheel, the floodlights shine (a loop of 2 s)
static void dk_icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s * 46 / 100, ic_g(16, 16, 64), ic_g(64, 48, 112));
    ic_dband(x, y, s, s * 46 / 100, s * 62 / 100, ic_g(48, 32, 64), ic_g(32, 16, 48));
    ic_dband(x, y, s, s * 62 / 100, s, ic_g(192, 128, 64), ic_g(96, 48, 16));
    // the crowd (small heads in the stands) and the two floodlights
    for (int k = 0; k < 16; k++) {
        static const uint8_t CC[4][3] = {{224, 64, 64}, {224, 192, 64}, {96, 160, 224}, {224, 224, 224}};
        float cx = x + (5 + k * 6) * u, cy = y + (51 + (k * 7) % 5) * u;
        tfd_disc(cx, cy, 1.6f * u, ic_g(CC[k & 3][0], CC[k & 3][1], CC[k & 3][2]), 256);
    }
    for (int k = 0; k < 2; k++) {
        float lx = x + (k ? 84 : 16) * u, ly = y + 14 * u;
        tfd_line(lx, ly, lx, y + 46 * u, 1.6f * u, ic_g(64, 64, 96), 256);
        tfd_disc(lx, ly, 8 * u, ic_g(224, 224, 160), 60);
        tfd_rrect((int)(lx - 5 * u), (int)(ly - 2.5f * u), (int)(10 * u), (int)(5 * u), (int)(1.5f * u),
                  ic_g(224, 240, 192), 256);
    }
    // the mound
    ic_oval(x + 50 * u, y + 80 * u, 34 * u, 12 * u, ic_g(160, 96, 48), 256);
    ic_oval(x + 46 * u, y + 76 * u, 24 * u, 6 * u, ic_g(224, 160, 96), 256);
    // the truck: up over the mound and down, the nose up when it goes up
    float jump = fsin(an), air = 0.5f + 0.5f * jump, tx = x + 50 * u, ty = y + 60 * u - air * 9 * u;
    float tilt = -0.18f * fcos(an), cs = fcos(tilt), sn = fsin(tilt);
    ic_oval(tx, y + 88 * u, 30 * u - air * 6 * u, 3 * u, ic_g(64, 32, 16), 200);
    // (mud from the back wheel)
    for (int k = 0; k < 4; k++) {
        float t = fmodf(ph * 2 + k * 0.25f, 1.0f);
        float mx = tx - 22 * u - t * 26 * u, my = ty + 14 * u - fsin(t * 3.14159f) * 14 * u + t * 6 * u;
        tfd_disc(mx, my, (2.4f - 1.4f * t) * u, ic_g(128, 80, 32), 256);
    }
    // (the wheels; the springs to the body; the springs are longer in the air)
    float sp = 2 * u + air * 3 * u;
    for (int k = -1; k <= 1; k += 2) {
        float ax = k * 19 * u, ay = 10 * u + sp;
        float wx = tx + ax * cs - ay * sn, wy = ty + ax * sn + ay * cs;
        tfd_line(tx + ax * cs - 4 * u * sn, ty + ax * sn + 4 * u * cs, wx, wy, 2.6f * u, ic_g(224, 192, 64), 256);
        tfd_disc(wx, wy, 13 * u, ic_g(16, 16, 16), 256);
        tfd_disc(wx - 1 * u, wy - 1 * u, 11.6f * u, ic_g(48, 48, 48), 256);
        tfd_disc(wx, wy, 6 * u, ic_g(160, 160, 176), 256);
        for (int b = 0; b < 5; b++) {
            float a = an * 2 + b * 1.2566f;
            tfd_disc(wx + fcos(a) * 9.4f * u, wy + fsin(a) * 9.4f * u, 1.4f * u, ic_g(96, 96, 96), 256);
            tfd_disc(wx + fcos(a) * 3.6f * u, wy + fsin(a) * 3.6f * u, 1 * u, ic_g(64, 64, 80), 256);
        }
    }
    // (the body: the bed, the cab with its window, the stripe, the roll bar)
    static const float BODY[] = {-28, 2, -28, -6, -8, -7, -4, -18, 14, -18, 20, -8, 29, -6, 29, 4, -28, 4};
    static const float WIN[] = {-2, -16, 12, -16, 16, -8, -2, -8};
    static const float STR[] = {-28, -2, 29, -2, 29, 1, -28, 1};
    ic_shape(BODY, 9, tx + 1 * u, ty + 1.5f * u, u * 1.04f, tilt, ic_g(64, 0, 0), 256);
    ic_shape(BODY, 9, tx, ty, u, tilt, ic_g(224, 32, 32), 256);
    ic_shape(STR, 4, tx, ty, u, tilt, ic_g(224, 224, 224), 256);
    ic_shape(WIN, 4, tx, ty, u, tilt, ic_g(64, 128, 192), 256);
    static const float SHINE[] = {0, -15, 4, -15, 1, -9, -1, -9};
    ic_shape(SHINE, 4, tx, ty, u, tilt, ic_g(192, 224, 240), 256);
    tfd_line(tx + (-24 * cs + 7 * sn) * u, ty + (-24 * sn - 7 * cs) * u, tx + (-12 * cs + 7 * sn) * u,
             ty + (-12 * sn - 7 * cs) * u, 1.6f * u, ic_g(32, 32, 48), 256);
    ic_end();
}

#if R_RND
// the round panels: the app code sees the square frame (gfx_w = gfx_h = 310), the render goes out into the circle
static void dk_r_enter(int64_t now)
{
    rndo_open(&s_rnd, 310);
    s_rnd.sharp = true;  // (the 1.5 scale with copies only: the arena is a soft picture, and it is faster)
    rndo_enter(&s_rnd, false);
    dk_enter(now);
    rndo_leave(&s_rnd);
}
static void dk_r_leave(void)
{
    rndo_enter(&s_rnd, false);
    dk_leave();
    rndo_leave(&s_rnd);
    rndo_close(&s_rnd);
}
static void dk_r_input(const input_event_t *ev)
{
    input_event_t e = *ev;
    int x = e.x, y = e.y;
    rndo_point(&s_rnd, &x, &y);
    e.x = (int16_t)x;
    e.y = (int16_t)y;
    bool sq = rndo_enter(&s_rnd, false);
    dk_input(sq ? &e : ev);
    rndo_leave(&s_rnd);
}
static void dk_r_update(int64_t now)
{
    rndo_enter(&s_rnd, false);
    dk_update(now);
    rndo_leave(&s_rnd);
}
static void dk_r_render(int64_t now)
{
    if (!rndo_enter(&s_rnd, true)) {
        dk_render(now);
        return;
    }
    int64_t t0 = esp_timer_get_time();
    gfx_clip_reset();
    dk_render(now);
    rndo_leave(&s_rnd);
    gfx_clip_reset();
    int64_t t1 = esp_timer_get_time();
    rndo_out(&s_rnd);
    // the speed of the round frame in the log, each 120 frames
    static int64_t s_rr, s_ro;
    static int s_rn;
    s_rr += t1 - t0;
    s_ro += esp_timer_get_time() - t1;
    if (++s_rn >= 120) {
        ESP_LOGI(TAG, "round frame: draw %d us, scale %d us", (int)(s_rr / s_rn), (int)(s_ro / s_rn));
        s_rr = s_ro = 0;
        s_rn = 0;
    }
}
#else
#define dk_r_enter  dk_enter
#define dk_r_leave  dk_leave
#define dk_r_input  dk_input
#define dk_r_update dk_update
#define dk_r_render dk_render
#endif
const app_t DK_SYM = {
        .name = DK_NAME,
        .landscape = true,
        .key = true,
        .enter = dk_r_enter,
        .leave = dk_r_leave,
        .input = dk_r_input,
        .update = dk_r_update,
        .render = dk_r_render,
        .draw_icon = dk_icon,
        .round = APP_ROUND_HI,  // (a round panel: the square frame in the whole circle, R_RND)
};
