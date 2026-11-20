// MURANO (GAMES): glass blowing in a workshop in Venice, in 1740. The glass on the end of the pipe is alive:
// it sags to the floor when the pipe stops turning, and it gets stiff as it cools. Make the shape of the order (the
// sketch on the card) before the glass sets.
//
// - THE INPUTS: TURN = a swipe up or down on the pipe (the pipe keeps its spin; the Watcher wheel turns it too). BLOW =
//   (a rub up and down turns it too, as a gaffer rolls it to and fro). BLOW =
//   hold the brass mouthpiece (the right end; the BOOT key of the 3.49 too; a button on the 320 x 240 boards). SWING =
//   tilt the board: the end of the pipe down makes the glass longer, up makes it shorter and wider. PINCH = hold a
//   finger on the glass: the jacks make a neck there (only while it turns). REHEAT = hold the furnace (the left edge;
//   a button on the 320 x 240 boards): the glass goes into the glory hole and gets soft again (a big piece does not go
//   in). SELL = tap the order card two times: the piece cracks off the pipe.
// - THE HEAT is the clock: the glass cools all the time, a thin wall first; cool glass is stiff, and too cold glass
//   cracks under the jacks or the breath. Too thin a wall bursts; a long piece that swings down falls off the pipe.
// - THE DAYS: one new skill each day (1 BLOW, the apprentice turns; 2 TURN; 3 SWING; 4 PINCH; 5 ALL), then MARKET
//   days with random orders. The match (the shared area of the two side views) gives the stars and the ducats.
// - THE REVEAL: the piece cools in the oven overnight; the next morning it stands in the window at dawn, and its real
//   colour shows (hot glass of each colour glows the same orange).
// - THE DEMO: 12 s with nobody on the title: the demo gaffer (bot_input) plays an order; a touch ends it.
// - The world: mu_world.inc (the workshop, painted at enter); the glass: mu_glass.inc (the model and its pixels).
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "jingle/jingle.h"
// MURANO: a new order (in place of the start tune of the system): a short baroque cadence in D minor, a harpsichord
static const tune_note_t MU_GO[] = {
        JN(0, 110, J_D5, W_PULSE, 20),          JN(110, 110, J_F5, W_PULSE, 20), JN(220, 110, J_A5, W_PULSE, 20),
        JN(330, 110, J_Gb5, W_PULSE, 20),       JN(440, 110, J_G5, W_PULSE, 20), JN(550, 110, J_E5, W_PULSE, 20),
        JN(660, 480, J_D5, JHOLD(W_PULSE), 20), JN(0, 220, J_D3, W_TRI, 50),     JN(220, 220, J_A2, W_TRI, 50),
        JN(440, 220, J_Bb2, W_TRI, 50),         JN(550, 110, J_A2, W_TRI, 50),   JN(660, 480, J_D3, JHOLD(W_TRI), 52),
};
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "midi_bg/midi_bg.h"
#include "mu_music.h"
#include "tfsfx/tfsfx.h"
#include "mu_sfx.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static const char *TAG = "murano";
#define SW     gfx_w
#define SH     gfx_h
#define MU_KEY "murano_"  // (the manifest: saves=murano_)

#include "mu_world.inc"
#include "mu_glass.inc"

static int s_lang;
#define TR(en, he) (s_lang ? (he) : (en))

// ---------------------------------------------------------------- the sound effects (MP3 in mu_sfx.c, libs/tfsfx)
// (the sources: apps/murano/sfx/SOURCES.md; the menus keep the short tones of sfx_play)
enum {
    FX_TINK,
    FX_BURST,
    FX_DROP,
    FX_CRACK,
    FX_JACKS,
    FX_WHOOSH,
    FX_COINS,
    FX_N
};
static const char *const FXN[FX_N] = {"tink", "burst", "drop", "crack", "jacks", "whoosh", "coins"};
static void fx(int id, int vol)
{
    tfsfx_play_var(id, vol, 0.06f);
}

// ---------------------------------------------------------------- the music (mu_music.c, libs/midi_bg)
static bool s_music = true;
static int s_mus_track = -1;
static float s_mus_back_t;
static int s_mus_after = -1;
static void mus_loop(int tr)
{
    if (!s_music || s_mus_track == tr) {
        return;
    }
    s_mus_track = tr;
    s_mus_back_t = 0;
    midi_bg_play_track(tr, 0, true);
}
static void mus_jingle(int j, int ms, int after)
{
    if (!s_music) {
        return;
    }
    midi_bg_play_track(j, 0, false);
    s_mus_track = -1;
    s_mus_back_t = ms / 1000.0f;
    s_mus_after = after;
}
static void mus_step(float dt)
{
    if (s_music) {
        midi_bg_service();
    }
    if (s_mus_back_t > 0 && (s_mus_back_t -= dt) <= 0) {
        if (s_mus_after >= 0) {
            mus_loop(s_mus_after);
        } else {
            midi_bg_stop(300);
        }
    }
}

// ---------------------------------------------------------------- the state
enum {
    GS_TITLE,
    GS_PLAY
};
static int s_gs;
static float s_gt;  // the time in the state
static glass_t *G;  // (PSRAM: about 10 KB)
static bot_t s_bot;
static bool s_demo, s_demo_ended;
static float s_idle_t;
static int s_day = 1;       // the day of the order now: 1..5 or DAY_MARKET
static int s_sel = 1;       // the day that the title shows
static int s_unlocked = 1;  // the days open: 1..6 (6 = the market)
static uint8_t s_stars[DAY_MARKET + 1];
static int s_ducats;
static bool s_paid;
static bool s_appr;     // APPRENTICE: he turns the pipe on each day (a setting)
static float s_sell_t;  // the first tap on the card: the second one in this time sells
static int64_t s_last;
static float s_wt;                  // the world time (the fire, the water)
static int64_t s_fps_t, s_draw_us;  // the frames a second and the time of the drawing (the log)
static int s_load, s_load_ms;       // the painting of the workshop at the open (load_step)
static int s_fps_n;

static void save_all(void)
{
    if (s_demo) {
        return;
    }
    store_set(MU_KEY "open", (uint32_t)s_unlocked);
    store_set(MU_KEY "duc", (uint32_t)s_ducats);
    uint32_t st = 0;
    for (int d = 1; d <= DAY_MARKET; d++) {
        st |= (uint32_t)(s_stars[d] & 3) << (2 * d);
    }
    store_set(MU_KEY "stars", st);
}
static void load_all(void)
{
    s_unlocked = clampi((int)store_get(MU_KEY "open", 1), 1, DAY_MARKET);
    s_ducats = (int)store_get(MU_KEY "duc", 0);
    uint32_t st = store_get(MU_KEY "stars", 0);
    for (int d = 1; d <= DAY_MARKET; d++) {
        s_stars[d] = (uint8_t)((st >> (2 * d)) & 3);
    }
}

// ---------------------------------------------------------------- the layouts
typedef struct {
    int x, y, w, h;
} box_t;
static inline bool in_box(box_t b, int x, int y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}
static inline bool longl(void)
{
    return SW >= 600;
}  // the 3.49 and the Tab5: the pipe fits the screen
// The round frame (gfx_round(): 466 x 466 on the 1.75C, 412 on the Watcher): the room at 1:1 fills the circle (the
// rafters over it); the order card in the top cap, BLOW and REHEAT on the bottom arc, the title in the middle.
static inline bool rl(void)
{
    return gfx_round();
}
static box_t card_box(void)
{
    if (rl()) {
        // (the round: a bigger card with KF_NAME text; the top cap has the room)
        box_t b = {SW / 2 - SW * 25 / 100, SH * 10 / 100, SW * 50 / 100, 74};
        return b;
    }
    box_t b = {SW - (longl() ? 204 : 158), 4, longl() ? 200 : 154, 52};
    return b;
}
static box_t blow_btn(void)
{
    if (rl()) {
        box_t b = {SW * 70 / 100, SH * 66 / 100, SW * 16 / 100, SW * 16 / 100};
        return b;
    }
    box_t b = {SW - 66, SH - 66, 62, 62};
    return b;
}
static box_t heat_btn(void)
{
    if (rl()) {
        box_t b = {SW * 9 / 100, SH * 66 / 100, SW * 21 / 100, SH * 11 / 100};
        return b;
    }
    box_t b = {4, SH - 50, 86, 46};
    return b;
}
static box_t chip_box(int d)
{
    if (longl()) {
        // five day chips and a wide MARKET chip
        int w = 48, gap = 4, wm = 84, x0 = SW - 5 * (w + gap) - wm - 8, y = SH >= 300 ? SH / 2 - 10 : 64;
        box_t b = {x0 + (d - 1) * (w + gap), y, d == DAY_MARKET ? wm : w, 46};
        return b;
    }
    if (rl()) {
        int w = SW * 21 / 100, gap = SW / 100, h = SH * 11 / 100;
        box_t b = {SW / 2 - (3 * w + 2 * gap) / 2 + ((d - 1) % 3) * (w + gap),
                   SH * 37 / 100 + ((d - 1) / 3) * (h + gap), w, h};
        return b;
    }
    box_t b = {8 + ((d - 1) % 3) * 102, 92 + ((d - 1) / 3) * 50, 98, 46};
    return b;
}
static box_t play_box(void)
{
    if (longl()) {
        box_t c = chip_box(1), m = chip_box(DAY_MARKET), b = {c.x, c.y + 52, m.x + m.w - c.x, 44};
        return b;
    }
    if (rl()) {
        box_t b = {SW / 2 - SW * 35 / 100, SH * 66 / 100, SW * 70 / 100, SH * 12 / 100};
        return b;
    }
    box_t b = {8, SH - 50, SW - 16, 44};
    return b;
}
static box_t res_box(int k)  // the result: NEXT, AGAIN, THE TITLE
{
    if (longl()) {
        box_t b = {SW - 214, (SH >= 300 ? SH - 170 : 18) + k * 50, 206, 44};
        return b;
    }
    if (rl()) {
        // (NEXT ORDER wide, then AGAIN and THE TITLE side by side, under the plate of the result)
        int w = SW * 25 / 100, h = SH * 11 / 100, y = SH * 61 / 100;
        box_t b = k == 0 ? (box_t){SW / 2 - w, y, 2 * w, h} : (box_t){SW / 2 + (k == 1 ? -w - 3 : 3), y + h + 6, w, h};
        return b;
    }
    box_t b = {SW - 96, 58 + k * 50, 92, 44};
    return b;
}

// ---------------------------------------------------------------- the frame
static uint16_t *s_bgc;             // a copy of the frame under the glass (the cold glass bends it)
static float s_ir[PMAX];            // 1 / r of the glass along the axis (each frame)
static uint16_t s_hot565[12];       // the colours of the glow, packed for the frame
static int s_pass_i[TN_N][3 * TT];  // the colour that cool glass lets through, of 256 (s_pass_t in integers)
static int s_bgc_w;
static c3 bg_screen(int X, int Y)
{
    return unpk(s_bgc[clampi(Y, 0, SH - 1) * s_bgc_w + clampi(X, 0, SW - 1)]);
}
static float s_cam_x, s_cam_y;
static int s_cam_z = 1;
static bool s_cam_init;
typedef struct {
    float tipX, tipY, ax, ay, pivX, pivY, mX, mY, L;
} scr_t;
static scr_t s_scr;  // the places of the pipe on the screen (for the touch)
typedef struct {
    float x, y, vx, vy, life;
} ember_t;
#define NEMB 48
static ember_t s_emb[NEMB];
static int s_nemb;

static inline void fb_set(int X, int Y, c3 c)
{
    if ((unsigned)X < (unsigned)SW && (unsigned)Y < (unsigned)SH) {
        g_fb[Y * SW + X] = pk(c);
    }
}
static inline void fb_set16(int X, int Y, uint16_t c)
{
    if ((unsigned)X < (unsigned)SW && (unsigned)Y < (unsigned)SH) {
        g_fb[Y * SW + X] = c;
    }
}

// the time of each part of a frame (us, summed for the log of each 5 s): the fire, the room, the light, the copy, the
// glass, the pipe, the hand and the tongs, the HUD
enum {
    PT_FIRE,
    PT_ROOM,
    PT_LIGHT,
    PT_COPY,
    PT_GLASS,
    PT_PIPE,
    PT_HAND,
    PT_HUD,
    PT_N
};
static int64_t s_pt[PT_N], s_pt_t;
static inline void pt_mark(int k)
{
    int64_t t = esp_timer_get_time();
    s_pt[k] += t - s_pt_t;
    s_pt_t = t;
}
static void draw_scene(float dt)
{
    s_pt_t = esp_timer_get_time();
    glass_t *g = G;
    const prof_t *P = &g->prof;
    float L = P->L;
    bool reveal = g->phase == PH_REVEAL && g->reveal_t > 0.55f;
    for (int a = 0; a < P->n; a++) {
        s_ir[a] = P->r[a] > 0.01f ? 1 / P->r[a] : 0;
    }
    if (!s_hot565[11]) {
        for (int k = 0; k < 12; k++) {
            s_hot565[k] = pk(CR(HOT, k));
        }
        for (int n = 0; n < TN_N; n++) {
            for (int c = 0; c < 3; c++) {
                for (int i = 0; i < TT; i++) {
                    s_pass_i[n][c * TT + i] = (int)(s_pass_t[n][c][i] * 256);
                }
            }
        }
    }
    animate_world(s_wt);
    pt_mark(PT_FIRE);
    // the pipe: it turns about the hand; the swing tilts it a little on the screen
    float slide_x = g->slide * SLIDE_PX;
    float pivX = PIVOT_X - slide_x, pivY = PIPE_Y;
    float av = g->slide > 0.02f ? 0 : g->tilt * 0.09f;
    float ax = -fcos(av), ay = fsin(av);
    float len = PIVOT_X - TIP_X;
    float tipX = pivX + ax * len, tipY = pivY + ay * len;
    float gcx = tipX + ax * L * 0.5f, gcy = tipY + ay * L * 0.5f;
    // the camera
    int zoom = 1;
    float tx, ty;
    if (reveal) {
        zoom = (L + 30) * 2 <= SH && longl() ? 2 : 1;
        tx = WCX - SW / zoom / 2.0f;
        ty = WSILL + 8 - SH / (float)zoom;
    } else if (longl()) {
        tx = gcx - (g->slide > 0.02f ? 160 + (1 - g->slide) * 10 : 170);
        ty = gcy * 0.55f + pivY * 0.45f - SH / 2.0f + 6;
    } else {
        tx = gcx - (rl() ? SW * 0.42f : 128);  // (the round frame: the glass a bit left of the middle)
        ty = gcy * 0.7f + pivY * 0.3f - SH / 2.0f;
    }
    tx = clampf(tx, 0, WW - SW / (float)zoom);
    ty = clampf(ty, (float)-s_top, WH0 - SH / (float)zoom);
    if (!s_cam_init || s_cam_z != zoom) {
        s_cam_x = tx;
        s_cam_y = ty;
        s_cam_init = true;
        s_cam_z = zoom;
    }
    float ck = fminf(1, (dt > 0 ? dt : 0.03f) * 4);
    s_cam_x += (tx - s_cam_x) * ck;
    s_cam_y += (ty - s_cam_y) * ck;
    float fx0 = s_cam_x, fy0 = s_cam_y;
    if (g->shake > 0) {
        fx0 += (rnd() - 0.5f) * g->shake * 6;
        fy0 += (rnd() - 0.5f) * g->shake * 4;
    }
    int x0 = clampi((int)lroundf(fx0), 0, WW - SW / zoom), y0 = clampi((int)lroundf(fy0), -s_top, WH0 - SH / zoom);
    s_scr.tipX = tipX - x0;
    s_scr.tipY = tipY - y0;
    s_scr.ax = ax;
    s_scr.ay = ay;
    s_scr.pivX = pivX - x0;
    s_scr.pivY = pivY - y0;
    s_scr.L = L;
    s_scr.mX = tipX - ax * (len + 34) - x0;
    s_scr.mY = tipY - ay * (len + 34) - y0;
    // 1. the room (in the reveal: the night, then a dissolve to the next morning)
    float dawn_k = reveal ? clampf((g->reveal_t - 3.2f) / 0.6f, 0, 1) : 0;
    int dw = DAWN_X1 - DAWN_X0;
    for (int Y = 0; Y < SH; Y++) {
        int yy = y0 + Y / zoom;
        const uint16_t *row = s_world + (yy + s_top) * WW;
        uint16_t *out = g_fb + Y * SW;
        if (zoom == 1 && dawn_k <= 0) {
            memcpy(out, row + x0, (size_t)SW * 2);
            if (yy > 36 && yy < HOLE_Y - HOLE_R + 4 && x0 < 196) {
                // the hot air over the glory hole
                int sh = (int)lroundf(fsin(yy * 0.55f + s_wt * 7) * 0.9f);
                if (sh) {
                    for (int xx = 104; xx < 196; xx++) {
                        int X = xx - x0;
                        if (X >= 0 && X < SW) {
                            out[X] = row[xx + sh];
                        }
                    }
                }
            }
            continue;
        }
        for (int X = 0; X < SW; X++) {
            int xx = x0 + X / zoom;
            uint16_t c = row[xx];
            if (dawn_k > 0 && xx >= DAWN_X0 && xx < DAWN_X1 && yy >= 0 && yy < DAWN_Y1 &&
                (dawn_k >= 1 || bay(X >> 1, Y >> 1) < dawn_k)) {
                c = s_dawn[yy * dw + xx - DAWN_X0];
            }
            out[X] = c;
        }
    }
    pt_mark(PT_ROOM);
    bool work = g->phase == PH_WORK || (g->phase == PH_REVEAL && !reveal);
    meas_t mm = measure(g);
    // 2. the light of the hot glass on the room (flat rings)
    float glow_a =
            sstep(0.12f, 0.85f, mm.tavg) * (g->slide > 0.6f ? 0 : 1) * (work || reveal ? 1 : 0) * (1 - dawn_k) * 0.75f;
    if (glow_a > 0.02f) {
        float lx = reveal ? (WCX - x0) * zoom : gcx - x0, ly = reveal ? (WSILL - L * 0.5f - y0) * zoom : gcy - y0;
        float R = (14 + mm.rmax * 1.05f) * (reveal ? zoom : 1);
        for (int Y = clampi((int)(ly - R), 0, SH); Y < clampi((int)(ly + R), 0, SH); Y++) {
            for (int X = clampi((int)(lx - R), 0, SW); X < clampi((int)(lx + R), 0, SW); X++) {
                float dd = ((X - lx) * (X - lx) + (Y - ly) * (Y - ly)) / (R * R);
                float l = glow_a / (1 + dd * 2.2f);
                int lv = (int)(l * 4);
                if (lv <= 0) {
                    continue;
                }
                // (+ l * (120, 48, 10) in RGB565, l = lv / 4 * 0.6)
                uint16_t v = sw16(g_fb[Y * SW + X]);
                int r5 = (v >> 11) + lv * 9 / 4, g6 = ((v >> 5) & 63) + lv * 7 / 4, b5 = (v & 31) + (lv >> 1);
                g_fb[Y * SW + X] = sw16(
                        (uint16_t)(((r5 > 31 ? 31 : r5) << 11) | ((g6 > 63 ? 63 : g6) << 5) | (b5 > 31 ? 31 : b5)));
            }
        }
    }
    pt_mark(PT_LIGHT);
    // 3. the sparks of the glory hole
    if (rnd() < 0.5f && s_nemb < NEMB) {
        ember_t *e = &s_emb[s_nemb++];
        e->x = HOLE_X + (rnd() - 0.5f) * 50;
        e->y = HOLE_Y - 30;
        e->vx = (rnd() - 0.5f) * 10;
        e->vy = -14 - rnd() * 22;
        e->life = 1.5f + rnd() * 2.5f;
    }
    int w = 0;
    for (int i = 0; i < s_nemb; i++) {
        ember_t *e = &s_emb[i];
        e->x += e->vx * dt + fsin(s_wt * 3 + e->y * 0.1f) * 0.15f;
        e->y += e->vy * dt;
        e->life -= dt;
        if (e->life <= 0 || e->y < -s_top) {
            continue;
        }
        s_emb[w++] = *e;
        fb_set((int)lroundf((e->x - x0) * zoom), (int)lroundf((e->y - y0) * zoom),
               CR(HOT, e->life > 2   ? 10
                       : e->life > 1 ? 8
                                     : 5));
    }
    s_nemb = w;
    // the box of the glass on the screen; a copy of the frame there (the cold glass shows the room through it)
    int bx0, bx1, by0, by1;
    if (reveal) {
        bx0 = clampi((int)((WCX - 80 - x0) * zoom), 0, SW);
        bx1 = clampi((int)((WCX + 80 - x0) * zoom), 0, SW);
        by0 = clampi((int)((WSILL - L - 10 - y0) * zoom), 0, SH);
        by1 = clampi((int)((WSILL + 4 - y0) * zoom), 0, SH);
    } else {
        // (the box: the glass along the axis, its largest radius and its largest sag; not more: each pixel costs)
        float ex = tipX + ax * (L + 2), ey = tipY + ay * (L + 2),
              gy = g->phase == PH_DROP ? fminf(140, 150 * g->phase_t * g->phase_t) : 0;
        float vm = 0;
        for (int a = 0; a < P->n; a++) {
            vm = fmaxf(vm, fabsf(P->v[a]));
        }
        float mg = mm.rmax + vm + 3, off = g->phase == PH_REVEAL ? g->reveal_t * 30 : 0;
        bx0 = clampi((int)(fminf(tipX, ex) - mg - off - x0), 0, SW);
        bx1 = clampi((int)(fmaxf(tipX, ex) + mg - x0) + 1, 0, SW);
        by0 = clampi((int)(fminf(tipY, ey) - mg - y0), 0, SH);
        by1 = clampi((int)(fmaxf(tipY, ey) + mg + gy - y0) + 1, 0, SH);
    }
    if (reveal || mm.tmin < 0.86f) {  // (hot glass shows no room through it: no copy)
        for (int Y = by0; Y < by1; Y++) {
            memcpy(s_bgc + Y * SW + bx0, g_fb + Y * SW + bx0, (size_t)(bx1 - bx0) * 2);
        }
    }
    const tint_t *tn = &TINTS[g->O.tint];
    pt_mark(PT_COPY);
    // 4. the glass
    if (reveal) {
        // the finished piece stands on the window sill: the mouth up
        float mx = WCX, my = WSILL + 1 - L + P->a0 * 0;
        for (int Y = by0; Y < by1; Y += zoom) {
            for (int X = bx0 - bx0 % zoom; X < bx1; X += zoom) {
                float wxp = x0 + X / zoom + 0.5f, wyp = y0 + Y / zoom + 0.5f;
                c3 c;
                if (!glass_px(P, wyp - my, wxp - mx, bg_screen, tn, X, Y, &c)) {
                    continue;
                }
                uint16_t p = pk(c);
                for (int j = 0; j < zoom; j++) {
                    for (int i = 0; i < zoom; i++) {
                        fb_set16(X + i, Y + j, p);
                    }
                }
            }
        }
    } else if (g->phase != PH_BURST || g->phase_t < 0.06f) {
        float gy = g->phase == PH_DROP ? fminf(140, 150 * g->phase_t * g->phase_t) : 0;
        float off = g->phase == PH_REVEAL ? g->reveal_t * 30 : 0;
        // each column: only the rows of the glass. Hot glass (T >= 0.85: it shows no room through it) takes the short
        // way: the tables of 1 / r and of the glow, the packed colours of the dither; cooler glass takes glass_px
        float rmx = 0, vmx = 0;
        for (int a = 0; a < P->n; a++) {
            rmx = fmaxf(rmx, P->r[a]);
            vmx = fmaxf(vmx, fabsf(P->v[a]));
        }
        float span = rmx + vmx + 2;
        for (int X = bx0; X < bx1; X++) {
            float rx = X + x0 + 0.5f - tipX - ax * off;
            float ac = rx * ax;  // (a at the axis row of this column)
            if (ac < -span * 0.2f || ac > P->L + span * 0.2f) {
                continue;
            }
            float yc = tipY + gy + ay * ac - y0;  // the axis at this column
            for (int Y = clampi((int)(yc - span), by0, by1); Y < clampi((int)(yc + span) + 1, by0, by1); Y++) {
                float ry = Y + y0 + 0.5f - tipY - gy;
                float a = rx * ax + ry * ay, d = rx * ay - ry * ax;
                int ia = (int)a;
                if (a < 0 || ia >= P->n - 1) {
                    continue;
                }
                float T = P->T[ia];
                if (T >= 0.85f && P->a0 == 0) {
                    float r = P->r[ia], e = d - P->v[ia], ae = fabsf(e);
                    if (ae > r || r < 0.6f) {
                        continue;
                    }
                    float u = e * s_ir[ia], ri = P->ri[ia];
                    float nz = sqrtf(fmaxf(0, 1 - u * u));
                    float t = 2 * r * nz - (ae < ri ? 2 * sqrtf(ri * ri - e * e) : 0);
                    int ti = (int)(t * 2);
                    float em = s_glow_t[ti < 0 ? 0 : ti < TT ? ti : TT - 1] * (0.42f + 0.58f * T);
                    if (ae > r - 1.05f) {
                        em *= 0.75f;
                    } else if (u < -0.47f && u > -0.63f) {
                        em += 0.08f;
                    }
                    float f = clampf(em, 0, 1) * 11;
                    int hi = (int)f;
                    if (f - hi > bay(X, Y)) {
                        hi++;
                    }
                    g_fb[Y * SW + X] = s_hot565[hi > 11 ? 11 : hi];
                    continue;
                }
                if (P->a0 == 0 && !tn->milk) {
                    // cooler glass: the room through it and the glow over it, in integers (the short way of glass_px)
                    float r = P->r[ia], e = d - P->v[ia], ae = fabsf(e);
                    if (ae > r || r < 0.6f) {
                        continue;
                    }
                    float u = e * s_ir[ia], ri = P->ri[ia];
                    float nz = sqrtf(fmaxf(0, 1 - u * u));
                    float tin = ae < ri ? 2 * sqrtf(ri * ri - e * e) : 0;
                    float t = fmaxf(0, 2 * r * nz - tin);
                    int ti = (int)(t * 2);
                    if (ti > TT - 1) {
                        ti = TT - 1;
                    }
                    int sx = clampi(X + (int)(u * 2.2f), 0, SW - 1), sy = clampi(Y - (int)(u * nz * 4), 0, SH - 1);
                    uint16_t b = sw16(s_bgc[sy * SW + sx]);
                    const int *ps = s_pass_i[g->O.tint];
                    int p = t >= 26 ? 41 : (int)(t * 41 / 26);  // (0.16 * 256 * p)
                    int cr = (((b >> 11) << 3) * ps[ti] >> 8) + ((int)tn->body[0] * p >> 8);
                    int cg = ((((b >> 5) & 63) << 2) * ps[TT + ti] >> 8) + ((int)tn->body[1] * p >> 8);
                    int cb = (((b & 31) << 3) * ps[2 * TT + ti] >> 8) + ((int)tn->body[2] * p >> 8);
                    bool edge = ae > r - 1.05f;
                    float span2 = fmaxf(1, P->L), am = a / span2;
                    float ends = sstep(1, 7, a) * sstep(0, 9, P->L - a);
                    int g8 = (int)(sstep(0.12f, 0.85f, T) * 256);
                    int k = 0, wr = 0, wg = 0, wb = 0;  // a highlight: its colour and its weight (of 256)
                    if (u < -0.5f && u > -0.6f && am > 0.18f && am < 0.42f) {
                        wr = 250;
                        wg = 252;
                        wb = 248;
                        k = 235;
                    } else if (u < -0.47f && u > -0.63f && ends > 0.5f) {
                        wr = 228;
                        wg = 234;
                        wb = 230;
                        k = 159;
                    } else if (u > 0.56f && u < 0.68f && ends > 0.5f) {
                        wr = 253;
                        wg = 154;
                        wb = 39;
                        k = 102;
                    }
                    if (tin > 0 && fabsf(ae - ri) < 0.9f) {
                        wr = (int)tn->body[0];
                        wg = (int)tn->body[1];
                        wb = (int)tn->body[2];
                        k = k > 90 ? k : 90;
                    }
                    k = k * (256 - g8) >> 8;
                    cr += (wr - cr) * k >> 8;
                    cg += (wg - cg) * k >> 8;
                    cb += (wb - cb) * k >> 8;
                    if (edge) {
                        int ke = 179 * (256 - g8) >> 8;
                        cr += ((int)(tn->body[0] * 0.25f) - cr) * ke >> 8;
                        cg += ((int)(tn->body[1] * 0.25f) - cg) * ke >> 8;
                        cb += ((int)(tn->body[2] * 0.25f) - cb) * ke >> 8;
                    }
                    if (g8 > 0) {
                        float em = s_glow_t[ti] * (0.42f + 0.58f * T) * (edge ? 0.75f : 1);
                        float f = clampf(em, 0, 1) * 11;
                        int hi = (int)f;
                        if (f - hi > bay(X, Y)) {
                            hi++;
                        }
                        const uint8_t *h = HOT[hi > 11 ? 11 : hi];
                        cr += (h[0] - cr) * g8 >> 8;
                        cg += (h[1] - cg) * g8 >> 8;
                        cb += (h[2] - cb) * g8 >> 8;
                    }
                    g_fb[Y * SW + X] =
                            rgb((uint8_t)clampi(cr, 0, 255), (uint8_t)clampi(cg, 0, 255), (uint8_t)clampi(cb, 0, 255));
                    continue;
                }
                c3 c;
                if (glass_px(P, a, d, bg_screen, tn, X, Y, &c)) {
                    fb_set(X, Y, c);
                }
            }
        }
        if (g->phase == PH_CRACK) {
            for (int k = 0; k < 5; k++) {
                float a = L * (0.2f + 0.15f * k), e = -P->r[clampi((int)a, 0, P->n - 1)] * 0.6f;
                for (int s2 = 0; s2 < 14; s2++) {
                    fb_set((int)lroundf(tipX + ax * a + ay * e - x0), (int)lroundf(tipY + ay * a - ax * e - y0),
                           crgb(235, 240, 236));
                    a += (h2(k, s2) - 0.5f) * 3;
                    e += 1.4f;
                }
            }
        }
    }
    pt_mark(PT_GLASS);
    if (!reveal) {
        // the pipe (and the brass mouthpiece after the hand): for each column only the rows near the axis
        float a_end = -(len + 30);
        float mk_c[48], mk_s[48];  // the turn marks: one each 11 px
        for (int k = 0; k < 48; k++) {
            float ph = g->theta + k * 1.9f;
            mk_c[k] = fcos(ph);
            mk_s[k] = fsin(ph);
        }
        float pxa = tipX - x0, pxb = tipX + ax * (a_end - 8) - x0;
        for (int X = clampi((int)(fminf(pxa, pxb) - 2), 0, SW); X < clampi((int)(fmaxf(pxa, pxb) + 2), 0, SW); X++) {
            float rx = X + x0 + 0.5f - tipX;
            float yc = tipY + ay * (rx / ax) - y0;
            for (int Y = clampi((int)yc - 6, 0, SH); Y < clampi((int)yc + 7, 0, SH); Y++) {
                float ry = Y + y0 + 0.5f - tipY;
                float a = rx * ax + ry * ay, e = rx * ay - ry * ax;
                if (a > 1 || a < a_end - 7) {
                    continue;
                }
                if (a < a_end) {
                    if (fabsf(e) > 4.2f) {
                        continue;
                    }
                    fb_set(X, Y,
                           FLAT(BRASS,
                                0.62f - e / 4.2f * 0.4f - (a < a_end - 5 ? 0.25f : 0) + (g->P > 0.1f ? 0.15f : 0)));
                    continue;
                }
                if (fabsf(e) > 3.2f) {
                    continue;
                }
                if (g->phase == PH_REVEAL && a > -4) {
                    continue;
                }
                float u = e / 3.2f, v = 0.5f - u * 0.36f;
                if (u < -0.4f && u > -0.75f) {
                    v = 0.86f;
                }
                if (fabsf(u) > 0.82f) {
                    v = 0.12f;
                }
                c3 c = FLAT(STEEL, v);
                int k = (int)(-a / 11);
                if (a <= 0 && k < 48 && mk_c[k] > 0.35f && fabsf(e - mk_s[k] * 2.6f) < 0.7f && (-a - k * 11) < 2.5f) {
                    c = CR(STEEL, 1);
                }
                // the end of the pipe is hot
                float hot = clampf(1 + a / 24, 0, 1) * mm.tavg * (g->phase == PH_WORK ? 1 : 0.5f);
                if (hot > 0.05f) {
                    c = cmix(c, DITH(HOT, 0.25f + hot * 0.45f, X, Y), hot);
                }
                fb_set(X, Y, c);
            }
        }
        pt_mark(PT_PIPE);
        // the front of the furnace covers the glass in the glory hole
        if (g->slide > 0.02f) {
            for (int Y = by0; Y < by1; Y++) {
                for (int X = bx0; X < bx1; X++) {
                    int xx = x0 + X, yy = y0 + Y;
                    if (!is_furnace(xx, yy) || fabsf((float)(yy - PIPE_Y)) <= 3.5f) {
                        continue;
                    }
                    float hd = len2((float)(xx - HOLE_X), (float)(yy - HOLE_Y));
                    if (hd < HOLE_R) {
                        continue;
                    }
                    g_fb[Y * SW + X] = s_world[(yy + s_top) * WW + xx];
                }
            }
        }
        // the jacks: tongs with two steel blades; the spring of the tongs is down and to the right; the tips glow
        if (g->jack_on || g->jack_gap < 50) {
            float ja = g->jack_a, gp = fminf(g->jack_gap, 46);
            float cxj = tipX + ax * ja - x0, cyj = tipY + ay * ja - y0, vj = P->v[clampi((int)ja, 0, P->n - 1)];
            float tx1 = cxj + ay * (vj - gp), ty1 = cyj - ax * (vj - gp), bx1_ = cxj + ay * (vj + gp),
                  by1_ = cyj - ax * (vj + gp);
            float jx = cxj - ax * 30 + ay * (vj + gp + 40), jy = cyj - ay * 30 - ax * (vj + gp + 40);
            float seg[3][5] = {{tx1, ty1, jx, jy, 1}, {bx1_, by1_, jx, jy, 1}, {jx, jy, jx + 26, jy + 18, 0}};
            for (int s2 = 0; s2 < 3; s2++) {
                float xa = seg[s2][0], ya = seg[s2][1], xb = seg[s2][2], yb = seg[s2][3];
                int n = (int)ceilf(len2(xb - xa, yb - ya));
                if (n < 1) {
                    continue;
                }
                float nx = -(yb - ya) / n, ny = (xb - xa) / n;
                for (int k = 0; k <= n; k++) {
                    float x = xa + (xb - xa) * k / n, y = ya + (yb - ya) * k / n;
                    for (int q = -1; q <= 1; q++) {
                        c3 c = q < 0 ? CR(STEEL, 7) : q == 0 ? CR(STEEL, 4) : CR(STEEL, 1);
                        if (seg[s2][4] > 0 && k < 4) {
                            c = CR(HOT, q < 0 ? 9 : 6);
                        }
                        fb_set((int)lroundf(x + nx * q), (int)lroundf(y + ny * q), c);
                    }
                }
            }
            for (int q = 0; q < 14; q++) {
                float t = q / 13.0f * (float)M_PI;
                fb_set((int)lroundf(jx + 26 + fsin(t) * 5), (int)lroundf(jy + 18 - fcos(t) * 5),
                       CR(STEEL, q < 7 ? 7 : 3));
            }
        }
        // the hand of the gaffer
        int hk = clampi((int)lroundf(1 + fsin(g->theta * 0.5f)), 0, 2);
        int hx = (int)lroundf(pivX - x0) - HAND_OX, hy = (int)lroundf(pivY - y0) - HAND_OY;
        if (s_hand[hk]) {
            for (int y = 0; y < HAND_H; y++) {
                for (int x = 0; x < HAND_W; x++) {
                    if (s_hand_a[hk][y * HAND_W + x]) {
                        fb_set16(hx + x, hy + y, s_hand[hk][y * HAND_W + x]);
                    }
                }
            }
        }
    }
    pt_mark(PT_HAND);
    // 5. the shards and the sparks
    for (int i = 0; i < g->nparts; i++) {
        const part_t *p = &g->parts[i];
        int X = (int)lroundf(tipX + ax * p->a + ay * p->d - x0), Y = (int)lroundf(tipY + ay * p->a - ax * p->d - y0);
        if (p->spark) {
            fb_set(X, Y, CR(HOT, 10));
        } else {
            fb_set(X, Y, p->life > 1 ? CR(HOT, 8) : crgb(200, 210, 220));
            if (p->life > 0.9f) {
                fb_set(X + 1, Y, CR(HOT, 6));
            }
        }
    }
}

// ---------------------------------------------------------------- the text on the frame
// (the ABI of the app files has ui_text_c and ui_text_w, not ui_text or ui_rrect_a: these two do their work here)
static inline void blend(uint16_t *d, uint16_t src, int a)
{
    uint16_t x = sw16(*d), y = sw16(src);
    int r = (x >> 11), g = (x >> 5) & 63, b = x & 31;
    r += (((y >> 11) - r) * a) >> 8;
    g += ((((y >> 5) & 63) - g) * a) >> 8;
    b += (((y & 31) - b) * a) >> 8;
    *d = sw16((uint16_t)((r << 11) | (g << 5) | b));
}
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
static void text_l(const kfont_t *f, int x, int base, const char *t, uint16_t c)
{
    ui_text_c(f, x + ui_text_w(f, t) / 2, base, t, c);
}
static uint16_t col(c3 c)
{
    return pk(c);
}
static void plate(int x, int y, int w, int h, int a)
{
    rect_a(x, y, w, h, 6, rgb(10, 7, 5), a);
}
static void text_plate_c(const kfont_t *f, int cx, int base, const char *t, uint16_t c)
{
    int w = ui_text_w(f, t) + 16;
    plate(cx - w / 2, base - f->asc - 5, w, f->line + 4, 190);
    ui_text_c(f, cx, base, t, c);
}
static void pill(int cx, int y, const char *t, bool on)
{
    const kfont_t *f = rl() ? &KF_NAME : &KF_LABEL;  // (the round: the bigger face)
    int w = ui_text_w(f, t) + 14, h = f->line + 4;
    int x = clampi(cx - w / 2, 2, SW - w - 2);
    y = clampi(y, 2, SH - h - 2);
    rect_a(x, y, w, h, h / 2, on ? rgb(70, 30, 8) : rgb(10, 7, 5), 205);
    ui_text_c(f, x + w / 2, y + f->asc + 2, t, on ? col(CR(HOT, 10)) : col(CR(PARCH, 5)));
}
static const char *msg_text(int m)
{
    switch (m) {
    case M_BURST:
        return TR("TOO THIN: IT BURST", "דק מדי: זה התפוצץ");
    case M_DROP:
        return TR("TOO LONG: IT FELL", "ארוך מדי: זה נפל");
    case M_CRACK:
        return TR("TOO COLD: IT CRACKED", "קר מדי: זה נסדק");
    case M_SAG:
        return TR("KEEP IT TURNING, OR IT SAGS", "תמשיך לסובב, אחרת זה צונח");
    case M_STIFF:
        return TR("IT GETS STIFF: REHEAT IT", "זה מתקשה: חמם שוב");
    case M_FLAT:
        return TR("TURN IT, OR THE NECK GOES FLAT", "סובב, אחרת הצוואר יתפחס");
    case M_BIG:
        return TR("TOO BIG FOR THE GLORY HOLE", "גדול מדי לפתח הכבשן");
    case M_JACKS_LATER:
        return TR("THE JACKS COME ON DAY 4", "הצבת מגיעה ביום 4");
    }
    return "";
}
static uint16_t *s_paper;  // the paper of the order card (painted one time)
static void draw_card(void)
{
    const glass_t *g = G;
    box_t b = card_box();
    if (!s_paper) {
        s_paper = heap_caps_malloc((size_t)b.w * b.h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_paper) {
            return;
        }
        for (int y = 0; y < b.h; y++) {
            for (int x = 0; x < b.w; x++) {
                float v = 0.82f + 0.1f * (n2(x / 6.0f, y / 5.0f) - 0.5f);
                if (x == 0 || y == 0 || x == b.w - 1 || y == b.h - 1) {
                    v = 0.45f;
                }
                s_paper[y * b.w + x] = pk(FLAT(PARCH, v));
            }
        }
    }
    for (int y = 0; y < b.h; y++) {
        memcpy(g_fb + (b.y + y) * SW + b.x, s_paper + y * b.w, (size_t)b.w * 2);
    }
    // the sketch: the order in ink (the mouth up), and the glass now in red chalk
    const prof_t *O = &g->O.P, *P = &g->prof;
    float sc = (rl() ? 52.0f : 40.0f) / fmaxf(O->L, 44);
    int sx = b.x + (rl() ? 33 : 24), sy = b.y + (rl() ? 9 : 5);
    uint16_t ink = rgb(36, 24, 14), chalk = rgb(168, 52, 30);
    for (int a = 0; a < O->n; a++) {
        float r = O->r[a] * sc;
        int y = (int)lroundf(sy + a * sc);
        fb_set16((int)lroundf(sx - r), y, ink);
        fb_set16((int)lroundf(sx + r), y, ink);
        if (a == O->n - 1) {
            for (int x = (int)-r; x <= r; x++) {
                fb_set16(sx + x, y, ink);
            }
        }
    }
    for (int a = 0; a < P->n; a += 2) {
        float r = P->r[a] * sc, v = P->v[a] * sc;
        int y = (int)lroundf(sy + a * sc);
        if (y > b.y + b.h - 3) {
            break;
        }
        fb_set16((int)lroundf(sx + v - r), y, chalk);
        fb_set16((int)lroundf(sx + v + r), y, chalk);
    }
    int tx = b.x + (rl() ? 68 : 50), tw = b.w - (rl() ? 74 : 56);
    // (the lines of the card: KF_NAME on the round, KF_LABEL on the other frames)
    const kfont_t *cf = rl() ? &KF_NAME : &KF_LABEL;
    const int L1 = rl() ? 22 : 13, L2 = rl() ? 44 : 30, L3 = rl() ? 65 : 45;
    char t[64];
    if (g->O.day > NDAYS) {
        snprintf(t, sizeof t, "%s", TR("MARKET DAY", "יום שוק"));
    } else if (s_lang) {
        snprintf(t, sizeof t, "יום %d %s", g->O.day, g->O.skill[1]);  // (no colon: the bidi of a Hebrew line)
    } else {
        snprintf(t, sizeof t, "DAY %d: %s", g->O.day, g->O.skill[0]);
    }
    // (a Hebrew line stands at the right end of the text place)
#define CARD_TEXT(base, str, c) text_l(cf, s_lang ? b.x + b.w - 6 - ui_text_w(cf, str) : tx, base, str, c)
    CARD_TEXT(b.y + L1, t, rgb(120, 40, 20));
    bool small = !longl();
    if (small) {
        snprintf(t, sizeof t, "%s %d%%", TR("MATCH", "התאמה"), (int)lroundf(g->score * 100));
    } else {
        snprintf(t, sizeof t, "%d%%", (int)lroundf(g->score * 100));
    }
    uint16_t mc = g->score >= 0.74f ? rgb(30, 96, 40) : ink;
    if (!small) {
        text_l(&KF_LABEL, s_lang ? tx : b.x + b.w - 6 - ui_text_w(&KF_LABEL, t), b.y + 13, t, mc);
    }
    if (s_sell_t > 0) {
        CARD_TEXT(b.y + L2, TR("TAP AGAIN:", "הקש שוב"), rgb(30, 96, 40));
        CARD_TEXT(b.y + L3, TR("SELL IT", "כדי למכור"), rgb(30, 96, 40));
    } else {
        // the order: one line, or two lines at a space
        const char *w = small ? g->O.shortw[s_lang] : g->O.what[s_lang];
        char l1[48], l2[48];
        snprintf(l1, sizeof l1, "%s", w);
        l2[0] = 0;
        if (ui_text_w(cf, l1) > tw) {
            int n = (int)strlen(w), cut = -1;
            for (int i = 0; i < n; i++) {
                if (w[i] == ' ') {
                    char tmp[48];
                    snprintf(tmp, sizeof tmp, "%.*s", i, w);
                    if (ui_text_w(cf, tmp) <= tw) {
                        cut = i;
                    }
                }
            }
            if (cut > 0) {
                snprintf(l1, sizeof l1, "%.*s", cut, w);
                snprintf(l2, sizeof l2, "%s", w + cut + 1);
            }
        }
        CARD_TEXT(b.y + L2, l1, ink);
        if (small) {
            CARD_TEXT(b.y + L3, t, mc);
        } else if (l2[0]) {
            CARD_TEXT(b.y + L3, l2, ink);
        }
#undef CARD_TEXT
    }
    // the heat: the colour of the glass from cold to white hot
    meas_t mm = measure(g);
    int hy = b.y + b.h + 3;
    for (int x = 0; x < b.w; x++) {
        float v = x / (float)(b.w - 1);
        fb_set(b.x + x, hy, DITH(HOT, 0.15f + v * 0.85f, b.x + x, hy));
        fb_set(b.x + x, hy + 1, DITH(HOT, 0.15f + v * 0.85f, b.x + x, hy + 1));
    }
    int hx = b.x + (int)lroundf(clampf(mm.tavg, 0, 1) * (b.w - 1));
    for (int k = -3; k <= 3; k++) {
        fb_set(hx + k, hy + 3 + abs(k), CR(PARCH, 6));
    }
    for (int k = 0; k < 4; k++) {
        fb_set(hx, hy + 2 + k, CR(PARCH, 6));
    }
}
static void draw_labels(void)
{
    // the inputs now (the demo shows its hands), or the help at the start of an order
    const glass_t *g = G;
    const gin_t *i = &g->last;
    bool help = !s_demo && g->t < 9;
    // (a message of the glass, for example KEEP IT TURNING, stands at the top: then the labels there wait or move)
    bool msg_on = g->msg_t > 0 && g->msg != M_NONE;
    if (longl()) {
        if (i->blow || help) {
            pill((int)fminf(s_scr.mX, SW - 40), (int)s_scr.mY + 12,
                 help && !i->blow ? TR("HOLD: BLOW", "החזק: נשוף") : TR("BLOW", "נשוף"), i->blow);
        }
        if (g->flick_t > 0 || help) {
            pill((int)s_scr.pivX - 4, (int)s_scr.pivY + 30,
                 help && g->flick_t <= 0 ? TR("SWIPE: TURN", "החלק: סובב") : TR("TURN", "סובב"), g->flick_t > 0);
        }
        if (help || (i->reheat && g->slide > 0.05f)) {
            pill(i->reheat && !msg_on ? 150 - (int)s_cam_x : 44, i->reheat && !msg_on ? 6 : SH / 2 - 8,
                 i->reheat ? TR("REHEAT", "חמם") : TR("HOLD: REHEAT", "החזק: חמם"), i->reheat);
        }
    } else if (g->flick_t > 0 || help) {
        pill(SW / 2, rl() ? SH * 855 / 1000 : SH - 24,
             help && g->flick_t <= 0 ? TR("SWIPE: TURN", "החלק: סובב") : TR("TURN", "סובב"), g->flick_t > 0);
    }
    // (the round: under the heat bar of the card)
    int py = longl() ? (int)s_scr.tipY - 70 : rl() ? card_box().y + card_box().h + 12 : 64;
    // (the label of the jacks waits under a message: the tongs on the glass show the pinch)
    if (!msg_on && i->jack) {
        pill((int)(s_scr.tipX + s_scr.ax * i->jack_a), py, TR("PINCH", "צבוט"), true);
    } else if (!msg_on && help && g->O.jacks) {
        pill(longl() ? (int)(s_scr.tipX + s_scr.ax * 40) : SW / 2 - 40, py,
             TR("HOLD THE GLASS: PINCH", "החזק את הזכוכית: צבוט"), false);
    }
    int ly = longl() ? SH - 18 : rl() ? SH * 79 / 100 : SH - 80;
    if (i->tilt != 0 && g->O.tilt) {
        pill(longl() ? 96 : SW / 2, ly,
             i->tilt > 0 ? TR("TILT DOWN: LONGER", "הטה למטה: ארוך יותר")
                         : TR("TILT UP: SHORTER", "הטה למעלה: קצר יותר"),
             true);
    } else if (help && g->O.tilt) {
        pill(longl() ? 96 : SW / 2, ly, TR("TILT: LONGER / SHORTER", "הטיה: ארוך / קצר"), false);
    }
}
static void draw_buttons(void)
{
    if (longl()) {
        return;
    }
    const glass_t *g = G;
    box_t b = blow_btn();
    ui_circle(b.x + b.w / 2, b.y + b.h / 2, b.w / 2, g->last.blow ? col(CR(HOT, 7)) : col(CR(BRASS, 3)), 230);
    ui_ring(b.x + b.w / 2, b.y + b.h / 2, b.w / 2 - 1, 3, col(CR(BRASS, 6)), 255);
    const kfont_t *bf = rl() ? &KF_NAME : &KF_LABEL;  // (the round: the bigger face)
    ui_text_c(bf, b.x + b.w / 2, b.y + b.h / 2 + (rl() ? 6 : 5), TR("BLOW", "נשוף"), rgb(255, 244, 220));
    box_t h = heat_btn();
    ui_rbox(h.x, h.y, h.w, h.h, 10, g->last.reheat ? col(CR(HOT, 6)) : rgb(60, 26, 12), col(CR(HOT, 6)), 2);
    ui_text_c(bf, h.x + h.w / 2, h.y + h.h / 2 + (rl() ? 6 : 5), TR("REHEAT", "חמם"), rgb(255, 230, 190));
}
static void draw_reveal_text(void)
{
    const glass_t *g = G;
    if (g->reveal_t <= 0.55f) {
        return;
    }
    const tint_t *tn = &TINTS[g->O.tint];
    int x = 10, y = 8;
    if (rl()) {
        x = SW / 2 - 100;  // (the plate in the middle of the top cap; a wide plate moves to the left below)
        y = SH * 12 / 100;
    }
    char t[64];
    if (g->reveal_t > 4.6f) {
        static const char *const R_EN[] = {"TRY AGAIN", "GOOD", "VERY GOOD", "MASTER WORK"};
        static const char *const R_HE[] = {"נסה שוב", "טוב", "טוב מאוד", "עבודת מאסטר"};
        const char *head = TR(R_EN[g->stars], R_HE[g->stars]);
        char pay[64];
        if (g->stars) {
            snprintf(pay, sizeof pay, "%s: %d %s", g->O.who[s_lang], g->stars * 6 + 4, TR("DUCATS", "דוקטים"));
        } else {
            snprintf(pay, sizeof pay, "%s", TR("NOT SOLD", "לא נמכר"));
        }
        int w = ui_text_w(&KF_BIG, head) + 20;
        if (w < 200) {
            w = 200;
        }
        if (ui_text_w(&KF_LABEL, pay) + 20 > w) {
            w = ui_text_w(&KF_LABEL, pay) + 20;
        }
        if (rl()) {
            x = SW / 2 - w / 2 + 4;
        }
        plate(x - 4, y - 2, w, 92, 200);
        text_l(&KF_BIG, x + 6, y + KF_BIG.asc + 2, head, col(CR(PARCH, 6)));
        int sy = y + KF_BIG.line + 6;
        for (int k = 0; k < 3; k++) {
            int cx = x + 14 + k * 22, cy = sy + 8;
            ui_circle(cx, cy, 8, k < g->stars ? col(CR(LAMP, 3)) : rgb(60, 50, 40), 255);
            ui_circle(cx - 2, cy - 2, 3, k < g->stars ? col(CR(LAMP, 4)) : rgb(80, 70, 60), 255);
        }
        snprintf(t, sizeof t, "%d%% %s", (int)lroundf(g->final * 100), TR("MATCH", "התאמה"));
        text_l(&KF_LABEL, x + 80, sy + 13, t, col(CR(LAMP, 3)));
        text_l(&KF_LABEL, x + 6, sy + 36, pay, col(CR(PARCH, 5)));
        // the buttons: the next day, again, the title (not in the demo)
        if (!s_demo && g->reveal_t > 5.2f) {
            const char *names[3][2] = {{"NEXT ORDER", "להזמנה הבאה"}, {"AGAIN", "שוב"}, {"THE TITLE", "למסך הפתיחה"}};
            for (int k = 0; k < 3; k++) {
                box_t b = res_box(k);
                ui_rbox(b.x, b.y, b.w, b.h, 10, k == 0 ? rgb(120, 52, 16) : rgb(40, 26, 18), col(CR(HOT, 6)), 2);
                bool big = longl() || (rl() && ui_text_w(&KF_NAME, names[k][s_lang]) < b.w - 12);
                ui_text_c(big ? &KF_NAME : &KF_LABEL, b.x + b.w / 2, b.y + b.h / 2 + (big ? 7 : 5), names[k][s_lang],
                          rgb(255, 236, 200));
            }
        }
    } else {
        const char *l1 = g->reveal_t < 3.2f ? TR("IT COOLS IN THE OVEN", "זה מתקרר בתנור")
                                            : TR("THE NEXT MORNING", "למחרת בבוקר");
        const char *l2 = g->reveal_t < 3.2f ? TR("OVERNIGHT", "כל הלילה") : tn->en;
        if (s_lang && g->reveal_t >= 3.2f) {
            l2 = tn->he;
        }
        int w = ui_text_w(&KF_NAME, l1);
        if (ui_text_w(&KF_NAME, l2) > w) {
            w = ui_text_w(&KF_NAME, l2);
        }
        if (rl()) {
            x = SW / 2 - (w + 20) / 2 + 4;
        }
        plate(x - 4, y - 2, w + 20, KF_NAME.line * 2 + 12, 200);
        text_l(&KF_NAME, x + 6, y + KF_NAME.asc + 2, l1, col(CR(PARCH, 5)));
        text_l(&KF_NAME, x + 6, y + KF_NAME.asc + KF_NAME.line + 4, l2, col(CR(PARCH, 6)));
    }
}
static void draw_hud(void)
{
    const glass_t *g = G;
    if (g->phase == PH_REVEAL) {
        draw_reveal_text();
        return;
    }
    draw_card();
    if (g->phase == PH_WORK) {
        draw_labels();
        draw_buttons();
    }
    if (g->msg_t > 0 && g->msg != M_NONE) {
        const char *m = msg_text(g->msg);
        int cx = longl() ? (int)(SW * 0.36f) : SW / 2 - 40;
        int by = longl() ? 8 + KF_NAME.asc : 70;
        if (rl()) {
            cx = SW / 2;
            by = card_box().y + card_box().h + 18 + KF_NAME.asc + 5;
        } else if (!longl()) {
            // (the plate must not cover the heat bar under the card. A message that fits stands left of the
            // card; a wider one goes under the mark of the heat bar)
            int w = ui_text_w(&KF_NAME, m), cl = card_box().x;
            if (w + 20 <= cl - 4) {
                cx = w / 2 + 12;
            } else {
                cx = clampi(cx, w / 2 + 12, SW - w / 2 - 12);
                by = card_box().y + card_box().h + 13 + KF_NAME.asc + 5;
            }
        }
        text_plate_c(&KF_NAME, cx, by, m, g->phase == PH_WORK ? col(CR(PARCH, 6)) : col(CR(LAMP, 3)));
    }
    if (s_demo) {
        const char *dm = TR("DEMO: TOUCH TO PLAY", "הדגמה: גע כדי לשחק");
        if (rl()) {
            text_plate_c(&KF_LABEL, SW / 2, SH * 945 / 1000, dm, col(CR(PARCH, 5)));
        } else {
            text_plate_c(&KF_LABEL, ui_text_w(&KF_LABEL, dm) / 2 + 12, SH - 10, dm, col(CR(PARCH, 5)));
        }
    }
}
static void draw_title(void)
{
    // the name, the days and the ducats
    int lx = longl() ? 16 : 10, base = longl() ? (SH >= 300 ? SH / 2 - 40 : 40) : 36;
    if (rl()) {
        lx = SW / 2 - ui_text_w(&KF_TITLE, "MURANO") / 2;
        base = SH * 19 / 100;
    }
    plate(lx - 8, base - KF_TITLE.asc - 8, ui_text_w(&KF_TITLE, "MURANO") + 18, KF_TITLE.line + KF_LABEL.line + 16,
          170);
    text_l(&KF_TITLE, lx, base, "MURANO", col(CR(PARCH, 6)));
    text_l(&KF_LABEL, lx, base + KF_LABEL.line + 2, TR("GLASS OF VENICE, 1740", "זכוכית ונציה, 1740"), col(CR(HOT, 8)));
    char t[48];
    snprintf(t, sizeof t, "%d %s", s_ducats, TR("DUCATS", "דוקטים"));
    if (rl()) {
        text_plate_c(&KF_NAME, SW / 2, SH * 31 / 100, t, col(CR(LAMP, 3)));
    } else if (longl()) {
        text_plate_c(&KF_LABEL, lx + 60, SH - 10, t, col(CR(LAMP, 3)));
    } else {
        text_l(&KF_LABEL, SW - ui_text_w(&KF_LABEL, t) - 10, 24, t, col(CR(LAMP, 3)));
    }
    for (int d = 1; d <= DAY_MARKET; d++) {
        box_t b = chip_box(d);
        bool open = d <= s_unlocked, sel = d == s_sel;
        ui_rbox(b.x, b.y, b.w, b.h, 9,
                sel    ? rgb(120, 52, 16)
                : open ? rgb(40, 26, 18)
                       : rgb(22, 16, 13),
                sel ? col(CR(HOT, 8)) : col(CR(HOT, 4)), sel ? 3 : 1);
        if (d <= NDAYS) {
            snprintf(t, sizeof t, "%s %d", longl() ? "" : TR("DAY", "יום"), d);
        } else {
            snprintf(t, sizeof t, "%s", TR("MARKET", "שוק"));
        }
        ui_text_c((longl() && d <= NDAYS) || rl() ? &KF_NAME : &KF_LABEL, b.x + b.w / 2,
                  b.y + (longl() ? 22
                         : rl()  ? 23
                                 : 20),
                  t, open ? rgb(255, 236, 200) : rgb(110, 96, 86));
        for (int k = 0; k < 3 && open && d <= NDAYS; k++) {
            ui_circle(b.x + b.w / 2 - 12 + k * 12, b.y + b.h - 10, 4,
                      k < s_stars[d] ? col(CR(LAMP, 3)) : rgb(70, 56, 44), 255);
        }
    }
    box_t p = play_box();
    ui_rbox(p.x, p.y, p.w, p.h, 12, rgb(150, 62, 16), col(CR(HOT, 8)), 2);
    const char *what = s_sel <= NDAYS ? ORDERS[s_sel - 1].what[s_lang] : TR("A RANDOM ORDER", "הזמנה אקראית");
    snprintf(t, sizeof t, "%s: %s", TR("WORK", "לעבודה"), what);
    const kfont_t *f = ui_text_w(&KF_NAME, t) < p.w - 16 ? &KF_NAME : &KF_LABEL;
    ui_text_c(f, p.x + p.w / 2, p.y + p.h / 2 + 7, t, rgb(255, 244, 220));
}

// ---------------------------------------------------------------- the input of the play (each frame)
static int s_turn_y = -1, s_turn_x;  // the finger that turns the pipe: its last place (-1: none)
static void play_input(float dt, gin_t *in)
{
    memset(in, 0, sizeof *in);
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    int n = input_points(px, py, INPUT_MAX_POINTS);
    in->blow = app_key_down();
    int best = -1, bd = 1 << 30, ty = 0, tx = 0;
    for (int k = 0; k < n; k++) {
        int x, y;
        gfx_touch_to_logical(px[k], py[k], &x, &y);
        if (in_box(card_box(), x, y)) {
            continue;
        }
        if (longl()) {
            if (x >= fmaxf(s_scr.pivX + 16, SW - 84)) {
                in->blow = true;
                continue;
            }
            if (x < 56 && G->O.day >= 1) {
                in->reheat = true;
                continue;
            }
        } else {
            if (in_box(blow_btn(), x, y)) {
                in->blow = true;
                continue;
            }
            if (in_box(heat_btn(), x, y)) {
                in->reheat = true;
                continue;
            }
        }
        // on the glass: the jacks
        float rx = x - s_scr.tipX, ry = y - s_scr.tipY, a = rx * s_scr.ax + ry * s_scr.ay,
              d = rx * s_scr.ay - ry * s_scr.ax;
        const prof_t *P = &G->prof;
        int ia = clampi((int)a, 0, P->n - 1);
        if (G->O.jacks && a > -2 && a < P->L + 4 && fabsf(d - P->v[ia]) < P->r[ia] + 10) {
            in->jack = true;
            in->jack_a = fmaxf(2, a);
            continue;
        }
        // else: the turn (the finger nearest to the last turn finger)
        int dd = s_turn_y < 0 ? 0 : (x - s_turn_x) * (x - s_turn_x) + (y - s_turn_y) * (y - s_turn_y);
        if (dd < bd) {
            bd = dd;
            best = k;
            tx = x;
            ty = y;
        }
    }
    if (best >= 0) {
        // (a rub up and down turns the pipe too: a gaffer rolls it to and fro on the arms of the bench; only the speed
        // of the finger counts)
        if (s_turn_y >= 0 && bd < 60 * 60) {
            in->flick = fminf(3, fabsf((float)(ty - s_turn_y)) * 0.09f);
        }
        s_turn_x = tx;
        s_turn_y = ty;
    } else {
        s_turn_y = -1;
    }
    int wh = input_wheel();
    if (wh) {
        in->flick += abs(wh) * 1.6f;
    }
    // the swing: the tilt of the board (the left side down = the end of the pipe down = longer)
    float tlx = 0, tly = 0;
    if (imu_ok()) {
        imu_tilt_logical(&tlx, &tly);
    }
    float t = -tlx;
    t = fabsf(t) < 0.07f ? 0 : (t - (t > 0 ? 0.07f : -0.07f)) / 0.22f;  // (a dead zone of about 4 degrees)
    in->tilt = clampf(t, -1, 1);
    (void)dt;
}

// ---------------------------------------------------------------- the states
static void order_start(int day)
{
    s_day = day;
    s_gs = GS_PLAY;
    s_gt = 0;
    s_paid = false;
    s_sell_t = 0;
    s_rng ^= (uint32_t)esp_random();
    glass_gather(G, day);
    G->auto_roll = s_appr;
    memset(&s_bot, 0, sizeof s_bot);
    s_turn_y = -1;
    imu_set_level();
    mus_loop(MUM_WORK);
}
static void title_go(void)
{
    s_gs = GS_TITLE;
    s_gt = 0;
    s_idle_t = 0;
    if (s_sel > s_unlocked) {
        s_sel = s_unlocked;
    }
    glass_gather(G, s_sel <= NDAYS ? s_sel : 4);
    G->auto_roll = true;
    G->no_cool = true;
    mus_loop(MUM_TITLE);
}
static void demo_start(void)
{
    s_demo = true;
    static const uint8_t DEMO_DAYS[5] = {4, 1, 3, 5, 2};
    static int k;
    order_start(DEMO_DAYS[k++ % 5]);
}
static void demo_end(void)
{
    s_demo = false;
    s_demo_ended = true;
    title_go();
}
static void pay(void)
{
    // the end of the reveal: the stars, the ducats, the next day
    if (s_paid || s_demo) {
        return;
    }
    s_paid = true;
    const glass_t *g = G;
    if (g->stars > 0) {
        fx(FX_COINS, 70);
        s_ducats += g->stars * 6 + 4;
        if (s_day <= NDAYS && g->stars > s_stars[s_day]) {
            s_stars[s_day] = (uint8_t)g->stars;
        }
        if (s_day < DAY_MARKET && s_day + 1 > s_unlocked) {
            s_unlocked = s_day + 1;
        }
    }
    save_all();
}

// ---------------------------------------------------------------- the panel items
enum {
    MI_LANG,
    MI_APPR,
    MI_MUS,
    MI_TITLE,
    MI_AGAIN
};
static const char *const V_LANG[] = {"ENGLISH", "ENGLISH", "עברית", "עברית"};
static const char *const V_OFFON[] = {"OFF", "כבוי", "ON", "פועל"};
static const char *const V_ONOFF[] = {"ON", "פועל", "OFF", "כבוי"};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, V_LANG, MU_KEY "lang"},
        {PI_PICK, MI_APPR, 2, 0, {"APPRENTICE TURNS", "החניך מסובב"}, V_OFFON, MU_KEY "appr"},
        {PI_PICK, MI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, V_ONOFF, MU_KEY "mus"},
        {PI_ACT, MI_TITLE, 0, 0, {"THE TITLE", "למסך הפתיחה"}, NULL, NULL},
        {PI_ACT2, MI_AGAIN, 0, 0, {"NEW GATHER", "איסוף חדש"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == MI_TITLE || id == MI_AGAIN) {
        return s_gs == GS_PLAY && !s_demo;
    }
    return true;
}
static void item_set(int id, int v)
{
    if (id == MI_LANG) {
        s_lang = v & 1;
    } else if (id == MI_APPR) {
        s_appr = v & 1;
        if (G) {
            G->auto_roll = s_appr || s_gs == GS_TITLE;
        }
    } else if (id == MI_MUS) {
        s_music = !(v & 1);
        s_mus_track = -1;
        s_mus_back_t = 0;
        if (!s_music) {
            midi_bg_stop(300);
        } else {
            midi_bg_start();
            mus_loop(s_gs == GS_TITLE ? MUM_TITLE : MUM_WORK);
        }
    } else if (id == MI_TITLE) {
        title_go();
    } else if (id == MI_AGAIN) {
        order_start(s_day);
    }
}
static void item_opened(void)
{
    audio_drone(0, 0, 0, W_NOISE);
    audio_drone(1, 0, 0, W_NOISE);
}
static void item_resume(void)
{
    imu_set_level();
}
static const pitems_t PANEL = {ITEMS, 5, item_shown, NULL, item_set, item_opened, item_resume};

// ---------------------------------------------------------------- the app
static void mu_enter(int64_t now)
{
    s_top = SH > WH0 ? SH - WH0 : 0;
    s_wh = WH0 + s_top;
    if (!s_world) {
        s_world = heap_caps_malloc((size_t)WW * s_wh * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_dawn) {
        s_dawn = heap_caps_malloc((size_t)(DAWN_X1 - DAWN_X0) * DAWN_Y1 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_bgc) {
        s_bgc = heap_caps_malloc((size_t)SW * SH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    s_bgc_w = SW;
    if (!G) {
        G = heap_caps_malloc(sizeof(glass_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    for (int k = 0; k < 3; k++) {
        if (!s_hand[k]) {
            s_hand[k] = heap_caps_malloc(HAND_W * HAND_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!s_hand_a[k]) {
            s_hand_a[k] = heap_caps_malloc(HAND_W * HAND_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
    }
    bool hands = true;
    for (int k = 0; k < 3; k++) {
        hands = hands && s_hand[k] && s_hand_a[k];
    }
    if (!s_world || !s_dawn || !s_bgc || !G || !hands) {
        ESP_LOGE(TAG, "no memory");
        // (with no G, update() and input() do nothing and render() shows NO MEMORY: no step paints into a missing
        // buffer; leave() frees the others)
        heap_caps_free(G);
        G = NULL;
        return;
    }
    s_load = 0;  // (the painting of the workshop: one step in each frame, mu_update)
    s_load_ms = 0;
    s_lang = store_get(MU_KEY "lang", 0) ? 1 : 0;
    s_appr = store_get(MU_KEY "appr", 0) ? true : false;
    s_music = !store_get(MU_KEY "mus", 0);
    s_mus_track = -1;
    s_mus_back_t = 0;
    midi_bg_init(MU_TRACKS, MU_TRACK_COUNT, MU_MUSIC_VOICES);
    midi_bg_volume(55);
    if (s_music) {
        midi_bg_start();
    }
    load_all();
    s_sel = s_unlocked;
    sysui_panel_items(&PANEL);
    s_demo = false;
    s_cam_init = false;
    s_nemb = 0;
    s_fps_t = 0;
    s_fps_n = 0;
    s_draw_us = 0;
    s_gs = GS_TITLE;
    s_last = now;
}

// The painting of the workshop at the open: one step in each frame (about 0.1 to 0.5 s each on the board), behind a
// loading screen. The time of each step goes to the log.
#define LOAD_STEPS 10
static void load_step(void)
{
    int64_t t0 = esp_timer_get_time();
    switch (s_load) {
    case 1:
        paint_wall();
        break;
    case 2:
        paint_beam();
        paint_posts();
        paint_window();
        paint_canal_detail();
        paint_tools();
        paint_lamp();
        break;
    case 3:
        paint_furnace();
        break;
    case 4:
        paint_floor_things();
        paint_shelf();
        break;
    case 5:
        paint_dawn();
        break;
    case 6:
        hole_tables();
        glass_tables();
        break;
    case 7:
        make_hand(0, -0.8f);
        break;
    case 8:
        make_hand(1, 0);
        break;
    case 9:
        make_hand(2, 0.8f);
        break;
    }
    s_load_ms += (int)((esp_timer_get_time() - t0) / 1000);
    if (++s_load == LOAD_STEPS) {
        ESP_LOGI(TAG, "the workshop is painted in %d ms", s_load_ms);
        tfsfx_load_mem(MU_SFX_DATA, MU_SFX_LEN, FXN, FX_N < MU_SFX_N ? FX_N : MU_SFX_N, 44);
        title_go();
    }
}
static void draw_loading(void)
{
    gfx_fill(rgb(16, 10, 8));
    int cy = SH / 2;
    ui_text_c(&KF_TITLE, SW / 2, cy - 6, "MURANO", col(CR(PARCH, 6)));
    ui_text_c(&KF_LABEL, SW / 2, cy + 16, TR("THE FURNACE IS LIT", "מדליקים את הכבשן"), col(CR(HOT, 8)));
    int w = SW / 3, x = SW / 2 - w / 2, y = cy + 28;
    gfx_fill_rect(x, y, w, 4, rgb(50, 26, 14));
    gfx_fill_rect(x, y, w * s_load / LOAD_STEPS, 4, col(CR(HOT, 7)));
}
static void mu_leave(void)
{
    audio_drone(0, 0, 0, W_NOISE);
    audio_drone(1, 0, 0, W_NOISE);
    midi_bg_end();
    tfsfx_free();
    heap_caps_free(s_world);
    s_world = NULL;
    heap_caps_free(s_dawn);
    s_dawn = NULL;
    heap_caps_free(s_bgc);
    s_bgc = NULL;
    heap_caps_free(G);
    G = NULL;
    heap_caps_free(s_paper);
    s_paper = NULL;
    hole_free();
    for (int k = 0; k < 3; k++) {
        heap_caps_free(s_hand[k]);
        s_hand[k] = NULL;
        heap_caps_free(s_hand_a[k]);
        s_hand_a[k] = NULL;
    }
}
static void mu_input_ev(const input_event_t *ev)
{
    if (!G || s_load < LOAD_STEPS) {
        return;
    }
    s_idle_t = 0;
    if (s_demo) {
        demo_end();
        return;
    }
    if (s_demo_ended && s_gs == GS_TITLE && s_gt < 0.6f) {
        return;  // (the touch that ended the demo)
    }
    s_demo_ended = false;
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (s_gs == GS_TITLE) {
        for (int d = 1; d <= DAY_MARKET; d++) {
            if (in_box(chip_box(d), x, y)) {
                if (d <= s_unlocked) {
                    s_sel = d;
                    sfx_play(SFX_CLICK);
                    glass_gather(G, d <= NDAYS ? d : 4);
                    G->auto_roll = true;
                    G->no_cool = true;
                } else {
                    sfx_play(SFX_BACK);
                }
                return;
            }
        }
        if (in_box(play_box(), x, y)) {
            JPLAY(MU_GO);
            order_start(s_sel);
        }
        return;
    }
    if (G->phase == PH_WORK && in_box(card_box(), x, y)) {
        // sell: two taps (a loss of the work on one tap is not safe)
        if (s_sell_t > 0) {
            s_sell_t = 0;
            glass_finish(G);
            fx(FX_TINK, 80);
            mus_jingle(MUM_J_SOLD, MU_J_SOLD_MS, MUM_TITLE);
        } else {
            s_sell_t = 2.0f;
            sfx_play(SFX_CLICK);
        }
        return;
    }
    if (G->phase == PH_REVEAL && G->reveal_t > 5.2f) {
        if (in_box(res_box(0), x, y)) {
            JPLAY(MU_GO);
            int next = s_day < NDAYS ? (s_day + 1 <= s_unlocked ? s_day + 1 : s_day) : DAY_MARKET;
            order_start(next);
        } else if (in_box(res_box(1), x, y)) {
            JPLAY(MU_GO);
            order_start(s_day);
        } else if (in_box(res_box(2), x, y)) {
            sfx_play(SFX_BACK);
            s_sel = s_day <= NDAYS ? s_day : DAY_MARKET;
            title_go();
        }
    }
}
// a slow tap (a press of 350 - 900 ms) on the title and the cards of the reveal acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void mu_input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    mu_input_ev(ev);
    if (slow && (G && (s_gs == GS_TITLE || G->phase == PH_REVEAL))) {
        mu_input_ev(&tap);
    }
}
static void load_step(void);
static void mu_update(int64_t now)
{
    if (!G) {
        return;
    }
    if (s_load < LOAD_STEPS) {
        // (the first frame shows the loading screen; then one step of the painting in each frame)
        if (s_load++ > 0) {
            s_load--;
            load_step();
        }
        s_last = now;
        return;
    }
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt <= 0) {
        return;
    }
    s_gt += dt;
    s_wt += dt;
    mus_step(dt);
    if (s_sell_t > 0) {
        s_sell_t -= dt;
    }
    gin_t in;
    memset(&in, 0, sizeof in);
    if (s_gs == GS_TITLE) {
        int16_t tpx[INPUT_MAX_POINTS], tpy[INPUT_MAX_POINTS];
        if (input_points(tpx, tpy, INPUT_MAX_POINTS) > 0 || app_key_down()) {
            s_idle_t = 0;
        } else if ((s_idle_t += dt) > 12) {
            demo_start();
            return;
        }
        glass_step(G, dt, &in);
    } else {
        int phase0 = G->phase;
        if (s_demo) {
            int16_t tpx[INPUT_MAX_POINTS], tpy[INPUT_MAX_POINTS];
            if (input_points(tpx, tpy, INPUT_MAX_POINTS) > 0 || app_key_down()) {
                demo_end();
                return;
            }
            bot_input(G, &s_bot, dt, &in);
            if (in.crack) {
                fx(FX_TINK, 80);
                mus_jingle(MUM_J_SOLD, MU_J_SOLD_MS, MUM_TITLE);
            }
        } else {
            play_input(dt, &in);
        }
        bool was_in = G->slide > 0.5f;
        bool jack_was = G->jack_on;
        glass_step(G, dt, &in);
        if ((G->slide > 0.5f) != was_in) {
            fx(FX_WHOOSH, 45);
        }
        if (G->jack_on && !jack_was) {
            fx(FX_JACKS, 40);
        }
        if (G->phase != phase0) {
            if (G->phase == PH_BURST) {
                fx(FX_BURST, 90);
                mus_jingle(MUM_J_FAIL, MU_J_FAIL_MS, MUM_WORK);
            }
            if (G->phase == PH_DROP) {
                fx(FX_DROP, 90);
                mus_jingle(MUM_J_FAIL, MU_J_FAIL_MS, MUM_WORK);
            }
            if (G->phase == PH_CRACK) {
                fx(FX_CRACK, 90);
                mus_jingle(MUM_J_FAIL, MU_J_FAIL_MS, MUM_WORK);
            }
        }
        if (G->phase == PH_REVEAL && G->reveal_t > 4.6f) {
            pay();
        }
        // the end: a failure gathers again; the demo goes back to the title after its reveal
        if ((G->phase == PH_BURST || G->phase == PH_DROP || G->phase == PH_CRACK) && G->phase_t > 2.6f) {
            int d = s_day;
            bool demo = s_demo;
            order_start(d);
            s_demo = demo;
        }
        if (s_demo && G->phase == PH_REVEAL && G->reveal_t > 7.6f) {
            s_demo = false;
            title_go();
            s_idle_t = 6;
        }
    }
    // the sound of the furnace (louder in the glory hole) and of the breath
    bool live = s_gs == GS_PLAY && G->phase == PH_WORK;
    audio_drone(0, 260 + G->slide * 260, (int)(4 + (live ? G->slide * 18 : 0)), W_NOISE);
    audio_drone(1, 2400, live ? (int)(G->P * 22) : 0, W_NOISE);
}
static void mu_render(int64_t now)
{
    if (!G || !s_world) {
        gfx_fill(rgb(0, 0, 0));
        ui_text_c(&KF_NAME, SW / 2, SH / 2, "NO MEMORY", rgb(255, 200, 160));
        return;
    }
    if (s_load < LOAD_STEPS) {
        draw_loading();
        return;
    }
    static int64_t last;
    float dt = last ? (now - last) / 1000.0f : 0.03f;
    last = now;
    if (dt > 0.1f || dt < 0) {
        dt = 0.03f;
    }
    int64_t t0 = esp_timer_get_time();
    draw_scene(dt);
    s_pt_t = esp_timer_get_time();
    if (s_gs == GS_TITLE) {
        draw_title();
    } else {
        draw_hud();
    }
    pt_mark(PT_HUD);
    // the frames a second and the time of the drawing (the log, each 5 s)
    s_draw_us += esp_timer_get_time() - t0;
    s_fps_n++;
    if (!s_fps_t) {
        s_fps_t = now;
    }
    if (now - s_fps_t >= 5000) {
        int fps10 = (int)(s_fps_n * 10000 / (now - s_fps_t)), draw = (int)(s_draw_us / 1000 / s_fps_n);
        int n = s_fps_n;
        ESP_LOGI(TAG,
                 "%d.%d fps, the drawing %d ms a frame: fire %d room %d light %d copy %d glass %d pipe %d hand %d hud "
                 "%d (0.1 ms)",
                 fps10 / 10, fps10 % 10, draw, (int)(s_pt[0] / 100 / n), (int)(s_pt[1] / 100 / n),
                 (int)(s_pt[2] / 100 / n), (int)(s_pt[3] / 100 / n), (int)(s_pt[4] / 100 / n), (int)(s_pt[5] / 100 / n),
                 (int)(s_pt[6] / 100 / n), (int)(s_pt[7] / 100 / n));
        (void)n;
        memset(s_pt, 0, sizeof s_pt);
        (void)fps10;
        (void)draw;
        s_fps_t = now;
        s_fps_n = 0;
        s_draw_us = 0;
    }
}
#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set in each app that draws an icon; all static and unused is no warning)
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
// the icon: the blowpipe with the hot glass at its end, in front of the glow of the furnace. The pipe turns (the shine
// goes round the glass), the glass swells with each breath, sparks rise from the furnace (a loop of 2 s)
static void mu_icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s, ic_g(64, 32, 32), ic_g(16, 0, 16));
    // the furnace: a brick arch with its fire
    float fx = x + 70 * u, fy = y + 62 * u;
    tfd_rrect((int)(fx - 24 * u), (int)(fy - 30 * u), (int)(48 * u), (int)(60 * u), (int)(20 * u), ic_g(96, 48, 32),
              256);
    tfd_rrect((int)(fx - 15 * u), (int)(fy - 20 * u), (int)(30 * u), (int)(40 * u), (int)(14 * u), ic_g(224, 96, 32),
              256);
    tfd_rrect((int)(fx - 10 * u), (int)(fy - 13 * u), (int)(20 * u), (int)(30 * u), (int)(9 * u), ic_g(224, 176, 64),
              256);
    tfd_rrect((int)(fx - 5 * u), (int)(fy - 5 * u), (int)(10 * u), (int)(20 * u), (int)(5 * u), ic_g(224, 240, 160),
              256);
    for (int k = 0; k < 3; k++) {
        float t = fmodf(ph * 2 + k * 0.33f, 1.0f), sx = fx + (k - 1) * 7 * u + 3 * u * fsin(an * 2 + k),
              sy = fy - 18 * u - t * 26 * u;
        tfd_disc(sx, sy, (1.8f - t * 1.2f) * u + 0.5f, ic_g(224, 192, 64), 256);
    }
    // the bench under the work
    tfd_rect(x + (int)(4 * u), (int)(y + 86 * u), (int)(92 * u), (int)(5 * u), ic_g(96, 64, 32), 256);
    tfd_rect(x + (int)(4 * u), (int)(y + 86 * u), (int)(92 * u), (int)(1.5f * u), ic_g(160, 112, 64), 256);
    // the blowpipe (from the lower right to the glass)
    float gx = x + 34 * u, gy = y + 50 * u, br = 1 + 0.09f * fsin(an), gr = 19 * u * br;
    tfd_line(x + 96 * u, y + 82 * u, gx + 10 * u, gy + 7 * u, 4.6f * u, ic_g(96, 96, 112), 256);
    tfd_line(x + 96 * u, y + 81 * u, gx + 10 * u, gy + 6 * u, 1.4f * u, ic_g(224, 224, 224), 256);
    tfd_rect((int)(x + 78 * u), (int)(y + 70 * u), (int)(7 * u), (int)(6 * u), ic_g(128, 64, 32), 256);
    // the glass: the glow, the red edge, the orange body, the hot middle, the shine that goes round
    tfd_disc(gx, gy, gr + 6 * u, ic_g(160, 32, 16), 110);
    tfd_disc(gx, gy, gr, ic_g(192, 48, 16), 256);
    tfd_disc(gx - 1.5f * u, gy - 1.5f * u, gr * 0.88f, ic_g(224, 112, 32), 256);
    tfd_disc(gx - 3 * u, gy - 3 * u, gr * 0.6f, ic_g(224, 176, 64), 256);
    tfd_disc(gx - 4 * u, gy - 4 * u, gr * 0.3f, ic_g(224, 240, 160), 256);
    for (int k = 0; k < 2; k++) {
        float a = an + k * 3.14159f, f = fcos(a);
        if (f <= 0) {
            continue;
        }
        ic_oval(gx + fsin(a) * gr * 0.72f, gy - gr * 0.38f + k * gr * 0.6f, (1.2f + 2.6f * f) * u, 1.4f * u,
                ic_g(224, 240, 224), 256);
    }
    ic_end();
}
static bool mu_awake(void)
{
    return s_gs == GS_PLAY && !s_demo;
}

const app_t app_murano = {
        .name = "MURANO",
        .landscape = true,
        .key = true,
        .wheel = true,
        .round = APP_ROUND_HI,
        .enter = mu_enter,
        .leave = mu_leave,
        .input = mu_input,
        .update = mu_update,
        .render = mu_render,
        .draw_icon = mu_icon,
        .awake = mu_awake,
};
