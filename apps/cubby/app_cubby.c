// CUBBY (GAMES): a 3D island game in the spirit of Mario 64, with its own core: CUBBY, a small cleaning
// robot on treads, scoops up junk, presses it into a cube in his body and builds his way up. Each kind of junk makes
// its own cube: cans = a TIN cube (a step), bed springs = a SPRING cube (a launch), balloons = a BALLOON cube (a lift;
// in the air it stays where he drops it). He can hop (a tap of JUMP); a held JUMP is the big jump, and at its top the
// time goes slow for a moment: he can drop a cube under his treads, catch a thing, or steer. The first map is a
// concept test: SUNDROP COVE, three SUNDROPS (one for each cube) and fourteen sparks.
// - The look: smooth 3D on both cores (cb_r3d.inc: full colour in each corner, a fine dither, soft edges), radiant
//   light (cb_mesh.inc), the island (cb_world.inc), the robot and the models (cb_robot.inc), the play (cb_game.inc).
// - The thumbs: the left thumb drives (a floating stick); JUMP and ACT at the right; a drag on the right side turns the
//   camera; a small tilt of the board looks round (the panel: TILT LOOK).
// - Tests: a host build with the pictures and a bot that gets all three SUNDROPS by touch.
// a memory barrier: memw on the Xtensa chips (ESP32-S3), a fence on RISC-V (the ESP32-P4 of the Tab5)
#if defined(__XTENSA__)
#define MEMW() __asm__ volatile("memw")
#else
#define MEMW() __sync_synchronize()
#endif
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
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/friends.h"
#include "tfn/link.h"
#include "tfn/mfont.h"
#include "tfn/net2.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "tfsfx/tfsfx.h"
#include "cb_sfx.h"
#include "cb_music.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

static const char *TAG = "cubby";
#define CB_KEY "cubby_"  // (the manifest: saves=cubby_)

// CUBBY: the start of a play: a bright little fanfare (a toy marimba and a tuba)
static const tune_note_t CB_GO[] = {
        JN(0, 90, J_G5, W_TRI, 34),
        JN(100, 90, J_C6, W_TRI, 34),
        JN(200, 90, J_E6, W_TRI, 34),
        JN(300, 260, J_G6, JHOLD(W_TRI), 36),
        JN(0, 180, J_C3, W_SQUARE, 30),
        JN(200, 180, J_G3, W_SQUARE, 30),
        JN(300, 300, J_C4, JHOLD(W_SQUARE), 28),
};

static bool s_tilt_on = true;
// the time of the stages (us, summed; core 0 and core 1 apart): 0 sky and sea, 1 solids, 2 shadows, 3 lights,
// 4 glows, 5 soft edges, 6 copy; 8 the robot, 9 the models, 10 the world, 11 the bins, 12 the rows
static int64_t s_pf[2][16];
#define PF_T() esp_timer_get_time()

#include "cb_r3d.inc"
#include "cb_mesh.inc"
#include "cb_world.inc"
#include "cb_robot.inc"
#include "cb_motor.inc"
#include "cb_game.inc"
#include "cb_life.inc"
#include "cb_hud.inc"

enum {
    GS_TITLE,
    GS_PLAY
};
static int s_gs;
static bool s_ok;
static int64_t s_last;
static int s_bench, s_bench_f;  // THE BENCH (cb_update): 0 off, else 1 + the view; the frame of the view
static void play_start(void);

// ---------------------------------------------------------------- the music (cb_music.c, libs/midi_bg)
static bool s_music = true;
static int s_mus_cur = -1;  // the loop that plays (CBM_TITLE, CBM_ISLAND; -1 none)
static float s_mus_jt;      // a jingle: the ms left, then the island loop again
static float s_mus_back;    // the beat of the island loop to go back to (the start of its phrase)
static void music_to(int track)
{
    s_mus_jt = 0;
    if (!s_music || s_mus_cur == track) {
        return;
    }
    midi_bg_play_track(track, 0, true);
    s_mus_cur = track;
}
static void music_jingle(int j, int ms)
{
    if (!s_music) {
        return;
    }
    if (s_mus_cur == CBM_ISLAND && s_mus_jt <= 0) {
        midi_bg_state_t st;
        midi_bg_state(&st);
        s_mus_back = 16.0f * (float)ifloor(st.beat / 16.0f);
    }
    midi_bg_play_track(j, 0, false);
    s_mus_cur = -1;
    s_mus_jt = (float)ms;
}
static void music_service(float rdt)
{
    if (!s_music) {
        return;
    }
    midi_bg_service();
    if (s_mus_jt > 0 && (s_mus_jt -= rdt * 1000) <= 0) {
        s_mus_jt = 0;
        if (s_gs == GS_PLAY) {
            midi_bg_play_track(CBM_ISLAND, s_mus_back, true);
            s_mus_cur = CBM_ISLAND;
        } else {
            music_to(CBM_TITLE);
        }
    }
}
#include "cb_net.inc"

// ---------------------------------------------------------------- the sky and the sea
static int8_t SINT[256];
// the colour of the sea at a distance: turquoise near the island, a deep blue farther, the haze at the horizon
static void sea_colour(float dist, float *o)
{
    static const float K[][4] = {{0, 70, 190, 182},   {9, 44, 148, 192},   {22, 52, 120, 172},
                                 {40, 118, 158, 194}, {70, 176, 200, 222}, {170, 186, 208, 226}};
    int n = (int)(sizeof K / sizeof K[0]);
    if (dist <= K[0][0]) {
        o[0] = K[0][1];
        o[1] = K[0][2];
        o[2] = K[0][3];
        return;
    }
    for (int i = 0; i + 1 < n; i++) {
        if (dist < K[i + 1][0]) {
            float t = (dist - K[i][0]) / (K[i + 1][0] - K[i][0]);
            for (int c = 0; c < 3; c++) {
                o[c] = K[i][c + 1] + (K[i + 1][c + 1] - K[i][c + 1]) * t;
            }
            return;
        }
    }
    o[0] = K[n - 1][1];
    o[1] = K[n - 1][2];
    o[2] = K[n - 1][3];
}
// the sky by the height of the view ray (the sine of its angle): a warm haze at the horizon, a vivid blue up high
static void sky_colour(float e, float *o)
{
    static const float K[][4] = {{-0.02f, 240, 222, 196},
                                 {0.03f, 236, 214, 190},
                                 {0.09f, 186, 208, 226},
                                 {0.24f, 120, 168, 214},
                                 {0.55f, 72, 124, 196}};
    int n = (int)(sizeof K / sizeof K[0]);
    if (e <= K[0][0]) {
        o[0] = K[0][1];
        o[1] = K[0][2];
        o[2] = K[0][3];
        return;
    }
    for (int i = 0; i + 1 < n; i++) {
        if (e < K[i + 1][0]) {
            float t = (e - K[i][0]) / (K[i + 1][0] - K[i][0]);
            for (int c = 0; c < 3; c++) {
                o[c] = K[i][c + 1] + (K[i + 1][c + 1] - K[i][c + 1]) * t;
            }
            return;
        }
    }
    o[0] = K[n - 1][1];
    o[1] = K[n - 1][2];
    o[2] = K[n - 1][3];
}
// the clouds: soft puffs far away (an azimuth, a height of the view, a size)
typedef struct {
    float az, el, sz;
} cloud_t;
static const cloud_t CLOUDS[] = {
        {0.3f, 0.10f, 0.05f}, {1.2f, 0.16f, 0.065f}, {2.1f, 0.08f, 0.045f}, {2.9f, 0.13f, 0.06f},
        {3.8f, 0.09f, 0.05f}, {4.6f, 0.18f, 0.07f},  {5.5f, 0.11f, 0.055f},
};
// a phase in steps of the wave table (any size) to 16.16 fixed point in [0, 256)
static inline uint32_t wave_fix(float ph)
{
    ph -= 256.0f * (float)ifloor(ph * (1.0f / 256.0f));
    return (uint32_t)(ph * 65536.0f);
}
// the data of each row of the frame for the sky and the sea (made once a frame, before the tiles)
typedef struct {
    uint16_t pat[8];
    uint32_t p1, dp1, p2, dp2;
    int32_t br, bg, bb;
    int16_t ai, gl;
    uint8_t sea;
} row_t;
// the colours of the near sea for each row: 16 wave levels, then the 2 top levels in the path of the sun; 8 dither
// steps each (the colour of a sea pixel is one read)
#define SEA_LV 14
typedef struct {
    float x, y, r;
    int cr, cg, cb, a;
    bool tint;  // (the colour mixes toward cr, cg, cb; else it adds)
} sglow_t;
#define NSKY 48
// the sky and the sea of a frame (two sets: the build set s_bg, the draw set s_bgd): the rows, the colours of the
// near sea, the column of the glints of the sun on the sea, the sun and the clouds on the screen
typedef struct {
    row_t row[VH];
    uint16_t seat[VH][SEA_LV * 4];
    float gx, gw;
    sglow_t sky[NSKY];
    int nsky;
} bgset_t;
static bgset_t s_bgs[2];
static bgset_t *s_bg = &s_bgs[0], *s_bgd = &s_bgs[0];
static void sky_glow(float x, float y, float r, int cr, int cg, int cb, int a)
{
    if (s_bg->nsky >= NSKY || x + r / KXF < 0 || x - r / KXF > VW || y + r < 0 || y - r > VH) {
        return;
    }
    s_bg->sky[s_bg->nsky++] = (sglow_t){x, y, r, cr, cg, cb, a, false};
}
// a glow of the sunset: the sky and the sea under it mix toward the colour (an added light on the bright haze of the
// horizon gives only white)
static void sky_tint(float x, float y, float r, int cr, int cg, int cb, int a)
{
    int n = s_bg->nsky;
    sky_glow(x, y, r, cr, cg, cb, a);
    if (s_bg->nsky > n) {
        s_bg->sky[n].tint = true;
    }
}
// the tint of a tile: only the px that nothing covers (z 0); the weight a (1 - d^2 / r^2)
static void tint_add(tile_t *t, const sglow_t *g)
{
    float rx = g->r * (1.0f / KXF);
    if (g->x + rx < t->x0 || g->x - rx >= t->x1 || g->y + g->r < t->y0 || g->y - g->r >= t->y1) {
        return;
    }
    float ir2 = frcp(g->r * g->r);
    int ya = clampi((int)(g->y - g->r), t->y0, t->y1), yb = clampi((int)(g->y + g->r) + 1, t->y0, t->y1);
    int tr = g->cr >> 3, tg = g->cg >> 2, tb = g->cb >> 3;
    for (int y = ya; y < yb; y++) {
        float dy = y + 0.5f - g->y, q0 = 1 - dy * dy * ir2;
        if (q0 <= 0) {
            continue;
        }
        float hw = fsqrt(q0) * rx;
        int xa = clampi((int)(g->x - hw), t->x0, t->x1), xb = clampi((int)(g->x + hw) + 1, t->x0, t->x1);
        if (xa >= xb) {
            continue;
        }
        // (q = 1 - d^2 / r^2 in 16.16, by its first and second steps along the row, as glow_add; the weight a q^2)
        float dx0 = (xa + 0.5f - g->x) * KXF;
        int32_t q = (int32_t)((q0 - dx0 * dx0 * ir2) * 65536.0f),
                dq = (int32_t)(-(2 * dx0 * KXF + KXF * KXF) * ir2 * 65536.0f),
                ddq = (int32_t)(-2.0f * KXF * KXF * ir2 * 65536.0f);
        uint16_t *h = tile_px(t, xa, y);
        for (int x = xa; x < xb; x++, h += 2, q += dq, dq += ddq) {
            if (h[1] != 0 || q <= 0) {
                continue;
            }
            int qq = q >> 8;
            int k = (g->a * qq * qq) >> 16;
            uint32_t p = h[0];
            int rr = (int)(p >> 11), gg = (int)((p >> 5) & 63), bb = (int)(p & 31);
            rr += ((tr - rr) * k) >> 8;
            gg += ((tg - gg) * k) >> 8;
            bb += ((tb - bb) * k) >> 8;
            h[0] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
    }
}
static void rows_prepare(void)
{
    s_bg->gx = s_bg->gw = 0;
    {
        v3 lh = vnorm(V3(L_SUN.x, 0, L_SUN.z));
        float cx = vdot(C.R, lh), cz = vdot(C.F, lh);
        if (cz > 0.05f) {
            s_bg->gx = C.cx + C.fx * cx * frcp(cz);
            s_bg->gw = VW * 0.2f;
        }
    }
    uint32_t tw = (uint32_t)(s_wtime * 1000);
    float ifl = frcp(C.f), iflx = frcp(C.fx);
    for (int y = 0; y < VH; y++) {
        row_t *R_ = &s_bg->row[y];
        float vy = (C.cy - (y + 0.5f)) * ifl;
        v3 d = V3(C.F.x + C.U.x * vy, C.F.y + C.U.y * vy, C.F.z + C.U.z * vy);
        float e = d.y * frsqrt(vdot(d, d));
        if (e > -0.004f) {
            float o[3];
            sky_colour(e, o);
            const uint8_t *dt = DITH[y & 7];
            uint32_t r = (uint32_t)(o[0] * (247.0f / 255.0f * 256)), g = (uint32_t)(o[1] * (251.0f / 255.0f * 256)),
                     b = (uint32_t)(o[2] * (247.0f / 255.0f * 256));
            for (int k = 0; k < 8; k++) {
                uint32_t q = dt[k];
                uint32_t p = ((r + (q << 3)) >> 11) << 11 | ((g + (q << 2)) >> 10) << 5 | ((b + (q << 3)) >> 11);
                R_->pat[k] = (uint16_t)p;
            }
            R_->sea = 0;
            continue;
        }
        // the sea: the world point of each pixel of the row (a line), the waves and the glints of the sun
        float t = (SEA_Y - C.eye.y) * frcp(d.y);
        float hd = t * fsqrt(d.x * d.x + d.z * d.z);
        float base[3];
        sea_colour(hd, base);
        float u0 = (0.5f - C.cx) * iflx;
        float wx = C.eye.x + t * (d.x + C.R.x * u0), wz = C.eye.z + t * (d.z + C.R.z * u0);
        float dwx = t * C.R.x * iflx, dwz = t * C.R.z * iflx;
        // (the phases in steps of the table, 256 = a full wave; wrapped before the change to fixed point)
        R_->p1 = wave_fix((wx * 1.6f + wz * 0.6f) * 40.743665f) + tw * 3200u;
        R_->dp1 = wave_fix((dwx * 1.6f + dwz * 0.6f) * 40.743665f);
        R_->p2 = wave_fix((wz * 1.25f - wx * 0.8f) * 40.743665f) - tw * 2600u;
        R_->dp2 = wave_fix((dwz * 1.25f - dwx * 0.8f) * 40.743665f);
        R_->ai = (int16_t)(clampf(1 - hd * (1.0f / 48.0f), 0, 1) * 18);
        R_->gl = (int16_t)(clampf(1 - hd * (1.0f / 120.0f), 0.15f, 1) * 90);
        R_->br = (int32_t)(clampf(base[0], 0, 230) * 256);
        R_->bg = (int32_t)(clampf(base[1], 0, 240) * 256);
        R_->bb = (int32_t)(clampf(base[2], 0, 235) * 256);
        const uint8_t *dt = DITH[y & 7];
        for (int k = 0; k < 8; k++) {
            uint32_t q = dt[k];
            uint32_t p = (((uint32_t)R_->br + (q << 3)) >> 11) << 11 | (((uint32_t)R_->bg + (q << 2)) >> 10) << 5 |
                         (((uint32_t)R_->bb + (q << 3)) >> 11);
            R_->pat[k] = (uint16_t)p;
        }
        R_->sea = 1;
        if (R_->ai > 0) {
            uint16_t *tab = s_bg->seat[y];
            for (int L = 0; L < SEA_LV; L++) {
                int lv = L < 12 ? L : L - 2, w = (lv * 128 + 64) / 3 - 256;
                int sh = (w * R_->ai) >> 1;
                int r = R_->br + sh * 3 / 4, g = R_->bg + sh, b = R_->bb + sh;
                if (w > 196) {
                    int k = 22 + (L >= 12 ? R_->gl : 0);
                    k = (k * (w - 196)) >> 2;
                    r += k * 34;  // (the gold of the sunset)
                    g += k * 24;
                    b += k * 10;
                }
                r = clampi(r, 0, 63000);
                g = clampi(g, 0, 64000);
                b = clampi(b, 0, 63000);
                for (int k = 0; k < 4; k++) {
                    uint32_t q = dt[k];
                    uint32_t p = (((uint32_t)r + (q << 3)) >> 11) << 11 | (((uint32_t)g + (q << 2)) >> 10) << 5 |
                                 (((uint32_t)b + (q << 3)) >> 11);
                    tab[L * 4 + k] = (uint16_t)p;
                }
            }
        }
    }
    // THE SUNSET (round 3: a radiant sunset over one part of the sea): the sun low on the horizon in the
    // direction of the light (south-west, so the shadows agree), a wide warm glow along the horizon (five glows side by
    // side), a pink glow over it; the gold path of the sun on the sea (the glints) points to it
    s_bg->nsky = 0;
    {
        float az0 = fatan2(L_SUN.x, L_SUN.z);
        for (int k = -1; k <= 3; k++) {
            if (k == 2) {
                continue;
            }
            float az = az0 + (k < 3 ? k * 0.36f : 0), el = k < 3 ? 0.035f : 0.16f;
            v3 d = V3(fsin(az) * fcos(el), fsin(el), fcos(az) * fcos(el));
            v3 q = V3(vdot(C.R, d), vdot(C.U, d), vdot(C.F, d));
            if (q.z < 0.05f) {
                continue;
            }
            float iz = frcp(q.z);
            float sx = C.cx + C.fx * q.x * iz, sy = C.cy - C.f * q.y * iz;
            int ak = k < 0 ? -k : k;
            if (k == 3) {
                sky_tint(sx, sy, C.f * 0.8f, 236, 104, 150, 150);  // (the pink over it)
            } else {
                sky_tint(sx, sy, C.f * (0.66f - 0.1f * ak), 255, 112 + 20 * ak, 48, 235 - 45 * ak);
            }
            if (k == 0) {
                sky_glow(sx, sy, C.f * 0.16f, 255, 170, 80, 200);  // (the sun)
                sky_glow(sx, sy, C.f * 0.06f, 255, 236, 180, 256);
            }
        }
    }
    for (int k = 0; k < (int)(sizeof CLOUDS / sizeof CLOUDS[0]); k++) {
        const cloud_t *c = &CLOUDS[k];
        for (int p = 0; p < 5; p++) {
            float az = c->az + (p - 2) * c->sz * 0.9f,
                  el = c->el + ((p & 1) ? c->sz * 0.35f : 0) - (p == 2 ? -c->sz * 0.2f : 0);
            v3 d = V3(fsin(az) * fcos(el), fsin(el), fcos(az) * fcos(el));
            v3 q = V3(vdot(C.R, d), vdot(C.U, d), vdot(C.F, d));
            if (q.z < 0.05f) {
                continue;
            }
            float iz = frcp(q.z);
            float sx = C.cx + C.fx * q.x * iz, sy = C.cy - C.f * q.y * iz,
                  r = C.f * c->sz * (p == 2 ? 1.3f : 0.95f) * iz;
            sky_glow(sx, sy, r, 255, 252, 246, 150);
            sky_glow(sx, sy - r * 0.15f, r * 0.6f, 255, 255, 255, 120);
        }
    }
}
// the sky and the sea of a tile: only the pixels that the solids did not cover (z = 0)
static void bg_tile(tile_t *t)
{
    int w = t->x1 - t->x0;
    const bgset_t *B = s_bgd;
    int gxa = B->gw > 0 ? (int)(B->gx - B->gw * 0.5f) : 1 << 20, gxb = B->gw > 0 ? (int)(B->gx + B->gw * 0.5f) : -1;
    for (int y = t->y0; y < t->y1; y++) {
        const row_t *R_ = &B->row[y];
        uint32_t *row = t->p + (y - t->y0) * TLW;
        // (the px of the row to fill: the round stops at the circle; xs even from the tile start, for the pairs)
        int xs = t->x0, xe = t->x1;
#if CB_ROUND
        if (xs < s_rcl[y]) {
            xs = t->x0 + ((s_rcl[y] - t->x0) & ~1);
        }
        if (xe > s_rcr[y]) {
            xe = s_rcr[y];
        }
        if (xs >= xe) {
            continue;
        }
#endif
        // (a px that nothing covers is all 0: the z 0 and no colour; the word takes the colour, the z stays 0)
        if (!R_->sea || R_->ai == 0) {
            // (the sky, and the far sea with no waves: a dither pattern)
            for (int x = xs - t->x0; x < xe - t->x0; x++) {
                if (!row[x]) {
                    row[x] = R_->pat[(t->x0 + x) & 7];
                }
            }
            continue;
        }
        // (the near sea: one wave value for each pair of px, its colour from the table of the row; the widths of the
        // tiles are even)
        uint32_t p1 = R_->p1 + R_->dp1 * (uint32_t)xs, p2 = R_->p2 + R_->dp2 * (uint32_t)xs;
        uint32_t dp1 = R_->dp1 * 2, dp2 = R_->dp2 * 2;
        const uint16_t *tab = B->seat[y];
        for (int x = xs; x < xe; x += 2, p1 += dp1, p2 += dp2) {
            uint32_t *w2 = row + (x - t->x0);
            if (w2[0] && w2[1]) {
                continue;
            }
            int L = ((SINT[(p1 >> 16) & 255] + SINT[(p2 >> 16) & 255] + 256) * 3) >> 7;
            if (L >= 10 && x > gxa && x < gxb) {
                L += 2;
            }
            const uint16_t *tl = tab + L * 4;
            if (!w2[0]) {
                w2[0] = tl[x & 3];
            }
            if (!w2[1]) {
                w2[1] = tl[(x + 1) & 3];
            }
        }
    }
    for (int k = 0; k < B->nsky; k++) {
        const sglow_t *g = &B->sky[k];
        if (g->tint) {
            tint_add(t, g);
        } else {
            glow_add(t, g->x, g->y, g->r, -1, g->cr, g->cg, g->cb, g->a);
        }
    }
}

// ---------------------------------------------------------------- the list of a frame
static rig_t s_rdraw;
static int s_order[WMAXC], s_norder;
// the far chunks of the island go to core 1 from s_isl_split on (the border follows the times of the two cores)
static int s_isl_split;
static int64_t s_isl_t1;
static void rows_prepare(void);
static void world_kick(void);
// the start of a list: the lanes, the glows, the order of the chunks near to far (before core 1 starts on the island)
static void list_begin(void)
{
    s_glb->n = 0;
    s_lframe++;
    lanes_reset();
    {
        int n = 0;
        static float zc[WMAXC];
        for (int k = 0; k < s_nch; k++) {
            const chunk_t *ch = &s_ch[k];
            // (a prop: its fine version near the eye, its coarse version far)
            if (ch->lod != LOD_ALL) {
                // (a wall or a small terrace: its own distance)
                float dx = ch->anc.x - C.eye.x, dy = ch->anc.y - C.eye.y, dz = ch->anc.z - C.eye.z;
                float ld = ch->lod >= LOD_WFINE ? WLOD_D : LOD_D;
                bool near = dx * dx + dy * dy + dz * dz < ld * ld;
                if (near != (ch->lod == LOD_FINE || ch->lod == LOD_WFINE)) {
                    continue;
                }
            }
            v3 cc = xf_pt(&C.w2c, ch->c);
            if (!cam_sees(cc, ch->rad)) {
                continue;  // (only the chunks in the view go into the order)
            }
            if (cc.z - ch->rad > (ch->lod == LOD_COARSE ? PROP_D : ch->kind != CK_SOLID ? RING_D : FAR_D)) {
                continue;  // (too far: the haze)
            }
            float z = cc.z;
            int j = n++;
            while (j > 0 && zc[j - 1] > z) {
                zc[j] = zc[j - 1];
                s_order[j] = s_order[j - 1];
                j--;
            }
            zc[j] = z;
            s_order[j] = k;
        }
        s_norder = n;
    }
}
// the list of a frame on this core (lane 0): the robot, the things and the near chunks; world_kick() starts the far
// chunks and the rows (lane 1) on core 1 when the pipeline does not do that
// (parts of list_build: inline, so the machine code is the same; the size check of tests/style)
static inline __attribute__((always_inline)) void list_machines(xf_t *xp)
{
    // the machines and the plates; the lamps on the posts (one for each plate: green = down)
    for (int m = 0; m < NMECH; m++) {
        const mech_t *q = &s_mech[m];
        if (q->kind == MK_BRIDGE) {
            // (the drawbridge turns round its hinge at the south end: up = closed, flat = open)
            float k = clampf((q->h - q->hc) / (float)(q->ho - q->hc), 0, 1);
            xf_make(xp, V3(q->i0, q->ho, q->j0), 0, -(1 - k) * PI_F * 0.5f, 0, V3(1, 1, 1));
            dl_model(&s_mod[M_BRIDGE], xp, 1, 0);
        } else if (q->h > q->ho + 0.02f) {  // (open: all under the ground)
            xf_make(xp, V3(q->i0, q->h, q->j0), 0, 0, 0, V3(1, 1, 1));
            dl_model(&s_mod[q->kind == MK_GATE ? M_GATE : M_DOOR], xp, 1, 0);
        }
    }
    {
        // (the lamps: the gate, the door, the bridge; two posts each)
        static const float LAMP[NMECH][2][3] = {{{14.8f, 13.9f, 30.5f}, {17.2f, 13.9f, 30.5f}},
                                                {{47.5f, 13.9f, 23.8f}, {47.5f, 13.9f, 26.2f}},
                                                {{47.8f, 4.9f, 36.8f}, {50.2f, 4.9f, 36.8f}}};
        for (int m = 0; m < NMECH; m++) {
            const mech_t *q = &s_mech[m];
            for (int s = 0; s < 2; s++) {
                bool on = s_plate[q->pl[q->np > 1 ? s : 0]].on;
                v3 c = V3(LAMP[m][s][0], LAMP[m][s][1], LAMP[m][s][2]);
                dl_glow(c, on ? 0.55f : 0.32f, on ? AC_CYAN : AC_PINK, on ? 220 : 120);
            }
        }
    }
    for (int p = 0; p < NPLATE; p++) {
        const plate_t *pl = &s_plate[p];
        float y = s_H[pl->i][pl->j];
        if (xf_pt(&C.w2c, V3(pl->i + 0.5f, y, pl->j + 0.5f)).z > THING_D) {
            continue;  // (far: a few px)
        }
        xf_make(xp, V3(pl->i, y, pl->j), 0, 0, 0, V3(1, 1, 1));
        dl_model(&s_mod[M_PLATE], xp, 1, 0);
        xf_make(xp, V3(pl->i, y - 0.06f * pl->push, pl->j), 0, 0, 0, V3(1, 1, 1));
        dl_model(&s_mod[pl->on ? M_PLATE_ON : M_PLATE_UP], xp, 1, 0);
        if (pl->on) {
            dl_glow(V3(pl->i + 0.5f, y + 0.15f, pl->j + 0.5f), 0.5f, AC_CYAN, 110);
        }
    }
}

static inline __attribute__((always_inline)) void list_drops(xf_t *xp)
{
    // the SUNDROPS: a glowing drop, its rays and its halo; a found one goes up and fades
    for (int k = 0; k < NDROP; k++) {
        const drop_t *d = &s_drop[k];
        float up = d->got ? d->t * 2.5f : 0;
        if (d->got && d->t > 1.2f) {
            continue;
        }
        float s = d->got ? 1 - d->t / 1.2f : 1 + 0.06f * fsin(s_rt * 3 + k);
        v3 c = V3(d->p.x, d->p.y + 0.12f * fsin(s_rt * 1.8f + k) + up, d->p.z);
        xf_make(xp, c, s_rt * 1.4f + k, 0, 0, V3(s, s, s));
        dl_model(&s_mod[M_SUNDROP], xp, 1, 0);
        dl_glow(c, 0.9f * s, AC_GOLD, 170);
        // the rays (light triangles in the plane of the view)
        v3 cc = xf_pt(&C.w2c, c);
        if (cc.z > 0.5f) {
            int m = pv_add_cam(cc, 170 * s, 0, 0);
            for (int r = 0; r < 7; r++) {
                float a = r * TAU_F / 7 + s_rt * 0.6f, len = (0.95f + 0.25f * fsin(s_rt * 2.3f + r)) * s, w = 0.07f;
                v3 tip = V3(cc.x + fcos(a) * len, cc.y + fsin(a) * len, cc.z);
                v3 sa = V3(cc.x - fsin(a) * w, cc.y + fcos(a) * w, cc.z),
                   sb = V3(cc.x + fsin(a) * w, cc.y - fcos(a) * w, cc.z);
                int t = pv_add_cam(tip, 0, 0, 0), ia = pv_add_cam(sa, 60, 0, 0), ib = pv_add_cam(sb, 60, 0, 0);
                dl_tri(m, ia, t, DK_ADD, AC_GOLD);
                dl_tri(m, t, ib, DK_ADD, AC_GOLD);
            }
        }
        if (!d->got) {
            float g2 = floor_at(c.x, c.z, c.y, NULL);
            if (g2 > -10) {
                dl_shadow(V3(c.x, g2 + 0.02f, c.z), 0.3f, 0.3f, 0, 0.2f);
            }
        }
    }
}

static void list_build(float alpha, bool kick)
{
    if (kick) {
        world_kick();
    }
    s_ntri_drawn = 0;
    bool title = s_gs == GS_TITLE;
    // the robot (near: first, so fewer pixels go under it)
    s_rdraw = R;
    s_rdraw.p = vlerp(P.p0, P.p, alpha);
    s_rdraw.yaw = P.yaw0 + wrap_pi(P.yaw - P.yaw0) * alpha;
    int64_t pa = PF_T();
    if (!(P.act == A_SPLASH && P.act_t > 0.5f && P.act_t < 1.1f)) {
        robot_draw(&s_rdraw);
    }
    if (s_fr.on && s_fr.seen && s_fr.p.y > SEA_Y - 0.6f) {
        robot_draw_who(&s_fr.r, 1);
        float fy = floor_at(s_fr.p.x, s_fr.p.z, s_fr.p.y + 0.02f, NULL);
        if (fy > -10) {
            dl_shadow(V3(s_fr.p.x, fy + 0.02f, s_fr.p.z), 0.62f, 0.66f, s_fr.yaw,
                      clampf(0.55f - (s_fr.p.y - fy) * 0.06f, 0.15f, 0.55f));
        }
    }
    s_pf[0][8] += PF_T() - pa;
    pa = PF_T();
    // the shadow of the robot on the ground under it
    int fl;
    float gy = floor_at(s_rdraw.p.x, s_rdraw.p.z, s_rdraw.p.y + 0.02f, &fl);
    if (gy > -10) {
        float h = s_rdraw.p.y - gy;
        dl_shadow(V3(s_rdraw.p.x, gy + 0.02f, s_rdraw.p.z), 0.62f + h * 0.05f, 0.66f + h * 0.05f, s_rdraw.yaw,
                  clampf(0.55f - h * 0.06f, 0.15f, 0.55f));
    }
    // the cubes of the stacks
    xf_t x;
    for (int i = 0; i < GX; i++) {
        for (int j = 0; j < GZ; j++) {
            int n = s_nstk[i][j];
            if (!n) {
                continue;
            }
            for (int k = 0; k < n; k++) {
                float s = 1;
                if (k == n - 1 && s_pop[i][j] < 1) {
                    float q = s_pop[i][j];
                    s = 0.8f + 0.2f * q + 0.12f * fsin(q * PI_F);
                }
                xf_make(&x, V3(i + 0.5f, s_H[i][j] + k + 0.5f, j + 0.5f), 0, 0, 0, V3(s, s, s));
                float coil = (k == n - 1) ? s_coil[i][j] : 1;
                draw_cube_model(s_stk[i][j][k], &x, coil, 1, 0, s_t + i);
            }
            if (s_H[i][j] > -5) {
                dl_shadow(V3(i + 0.5f, s_H[i][j] + 0.015f, j + 0.5f), 0.68f, 0.68f, 0, 0.3f);
            }
        }
    }
    for (int k = 0; k < NFLO; k++) {
        const flo_t *f = &s_flo[k];
        if (!f->on) {
            continue;
        }
        float s = f->pop < 0.3f ? 0.8f + 0.2f * f->pop / 0.3f + 0.1f * fsin(f->pop / 0.3f * PI_F) : 1;
        xf_make(&x, V3(f->i + 0.5f, f->y + 0.5f + fsin(s_t * 2 + k) * 0.02f, f->j + 0.5f), fsin(s_t * 0.7f + k) * 0.05f,
                0, 0, V3(s, s, s));
        draw_cube_model(CB_FLOAT, &x, 1, 1, 0, s_t + k);
        float g2 = cell_top(f->i, f->j);
        if (cell_water(f->i, f->j)) {
            g2 = SEA_Y;
        }
        dl_shadow(V3(f->i + 0.5f, g2 + 0.02f, f->j + 0.5f), 0.6f, 0.6f, 0,
                  clampf(0.4f - (f->y - g2) * 0.05f, 0.1f, 0.4f));
    }
    if (s_toss.on) {
        // (the thrown cube: an arc up over the higher end, one turn about its side axis, a little bigger in the air)
        float u = clampf(s_toss.t / s_toss.dur, 0, 1);
        v3 a = s_toss.a, b = s_toss.b;
        float h = FMAX(a.y, b.y) + 0.55f - (a.y + (b.y - a.y) * 0.5f);
        v3 c = V3(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u + 4 * h * u * (1 - u), a.z + (b.z - a.z) * u);
        float sc = 0.82f + 0.18f * smooth01(u * 2);
        xf_make(&x, c, s_toss.yaw, -TAU_F * u, 0, V3(sc, sc, sc));
        draw_cube_model(s_toss.kind, &x, 1, 1, 0, s_t);
        float g2 = floor_at(c.x, c.z, c.y, NULL);
        if (g2 > -10) {
            dl_shadow(V3(c.x, g2 + 0.02f, c.z), 0.5f, 0.5f, 0, 0.25f);
        }
    }
    for (int k = 0; k < NFALL; k++) {
        const fall_t *q = &s_fall[k];
        if (!q->on) {
            continue;
        }
        xf_make(&x, V3(q->i + 0.5f, q->y + 0.5f, q->j + 0.5f), 0, 0, 0, V3(1, 1, 1));
        draw_cube_model(q->kind, &x, 1, 1, 0, s_t);
    }
    list_machines(&x);
    // the junk piles
    for (int k = 0; k < NPILE; k++) {
        const pile_t *pl = &s_pile[k];
        float pu = pl->pulse, s = 1 - 0.25f * pu * (pu > 0.7f ? (1 - pu) / 0.3f : 1);
        float pz = xf_pt(&C.w2c, V3(pl->x, pl->y, pl->z)).z;
        if (pz > THING_D) {
            continue;  // (far: in the haze)
        }
        bool pfar = pz > 10.0f;  // (far: the models with fewer triangles)
        if (pl->kind == CB_TIN) {
            xf_make(&x, V3(pl->x, pl->y, pl->z), 0.4f, 0, 0, V3(s, s, s));
            dl_model(&s_mod[pfar ? M_PILE_TIN_LO : M_PILE_TIN], &x, 1, 0);
        } else if (pl->kind == CB_SPRING) {
            xf_make(&x, V3(pl->x, pl->y, pl->z), -0.3f, 0, 0, V3(s, s, s));
            dl_model(&s_mod[pfar ? M_PILE_SPRING_LO : M_PILE_SPRING], &x, 1, 0);
        } else {
            xf_make(&x, V3(pl->x, pl->y, pl->z), 0, 0, 0, V3(1, 1, 1));
            dl_model(&s_mod[M_POST], &x, 1, 0);
            static const int BM[5] = {M_BALLOON_P, M_BALLOON_Y, M_BALLOON_G, M_BALLOON_B, M_BALLOON_P};
            for (int b = 0; b < 5; b++) {
                float a = b * 1.26f, bx = pl->x + fcos(a) * 0.35f + fsin(s_t * 1.1f + b) * 0.06f,
                      bz = pl->z + fsin(a) * 0.35f;
                float by = pl->y + 1.65f + 0.18f * (b & 1) + fsin(s_t * 1.6f + b * 1.7f) * 0.06f;
                xf_make(&x, V3(bx, by, bz), 0, fsin(s_t + b) * 0.1f, 0, V3(s, s, s));
                dl_model(&s_mod[BM[b]], &x, 1, 0);
                float sl = by - 0.25f - (pl->y + 1.1f);
                xf_make(&x, V3(bx, by - 0.25f, bz), 0, 0, 0, V3(1, sl, 1));
                dl_model(&s_mod[M_STRING], &x, 1, 0);
            }
        }
        dl_shadow(V3(pl->x, pl->y + 0.015f, pl->z), 0.7f, 0.6f, 0, 0.35f);
    }
    list_drops(&x);
    // the sparks: small gold nuts that turn
    for (int k = 0; k < NSPARK; k++) {
        const drop_t *d = &s_spark[k];
        if (d->got) {
            continue;
        }
        v3 c = V3(d->p.x, d->p.y + 0.08f * fsin(s_rt * 2.4f + k), d->p.z);
        // (far away a spark is a few px: only its glow; farther: nothing)
        float sz = xf_pt(&C.w2c, c).z;
        if (sz > THING_D) {
            continue;
        }
        if (sz < 13.0f) {
            xf_make(&x, c, s_rt * 3.0f + k, 0, 0, V3(1.4f, 1.4f, 1.4f));
            dl_model(&s_mod[M_SPARK], &x, 1, 0);
        }
        dl_glow(c, 0.38f, AC_GOLD, 90);
    }
    life_draw();  // (the crabs, the butterflies, the fish)
    // the gulls
    for (int k = 0; k < 3; k++) {
        float a = s_rt * (0.18f + 0.05f * k) + k * 2.1f, rr = 14 + k * 5;
        v3 c = V3(33 + fcos(a) * rr, 10.5f + k * 1.6f + fsin(s_rt * 0.7f + k) * 0.6f, 29 + fsin(a) * rr);
        float flap = fsin(s_rt * 9 + k * 2) * 1.4f;
        xf_make(&x, c, a + PI_F, 0, (k & 1) ? 0.2f : -0.2f, V3(1, flap, 1));
        dl_model(&s_mod[M_BIRD], &x, 1, 0);
    }
    // the cube that the robot can put down: a glowing ghost on its cell
    if (!title && P.carry && P.ground && P.act == A_NONE) {
        int i, j;
        float top;
        front_cell(&i, &j);
        if (place_ok(i, j, &top)) {
            float pu = 0.5f + 0.5f * fsin(s_rt * 6);
            int str = (int)(40 + 50 * pu);
            v3 lo = V3(i + 0.04f, top + 0.02f, j + 0.04f), hi = V3(i + 0.96f, top + 0.98f, j + 0.96f);
            int id[8];
            for (int c = 0; c < 8; c++) {
                id[c] = pv_add_cam(
                        xf_pt(&C.w2c, V3((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z)),
                        (c & 2) ? str * 0.4f : str, 0, 0);
            }
            static const uint8_t FQ[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 2, 6, 4},
                                             {1, 5, 7, 3}, {0, 4, 5, 1}, {2, 3, 7, 6}};
            int col = P.carry == CB_TIN ? AC_WHITE : P.carry == CB_SPRING ? AC_PINK : AC_CYAN;
            for (int f = 0; f < 6; f++) {
                dl_tri(id[FQ[f][0]], id[FQ[f][1]], id[FQ[f][2]], DK_ADD, col);
                dl_tri(id[FQ[f][0]], id[FQ[f][2]], id[FQ[f][3]], DK_ADD, col);
            }
        }
    }
    // the particles
    for (int k = 0; k < NPART; k++) {
        const part_t *q = &s_part[k];
        if (q->life <= 0) {
            continue;
        }
        float a = q->life / q->max;
        if (q->kind == PT_BIT || q->kind == PT_CONF) {
            v3 cc = xf_pt(&C.w2c, q->p);
            if (cc.z < 0.3f) {
                continue;
            }
            float s = q->size, sp = s_rt * 9 + k;
            float ca = fcos(sp) * s, sa = fsin(sp) * s;
            int i0 = pv_add_cam(V3(cc.x - ca, cc.y - sa, cc.z), q->r, q->g, q->b);
            int i1 = pv_add_cam(V3(cc.x + sa, cc.y - ca, cc.z), q->r, q->g, q->b);
            int i2 = pv_add_cam(V3(cc.x + ca, cc.y + sa, cc.z), q->r * 0.8f, q->g * 0.8f, q->b * 0.8f);
            int i3 = pv_add_cam(V3(cc.x - sa, cc.y + ca, cc.z), q->r * 0.8f, q->g * 0.8f, q->b * 0.8f);
            dl_tri(i0, i2, i1, DK_SOLID_NM, 0);
            dl_tri(i0, i3, i2, DK_SOLID_NM, 0);
            dl_tri(i0, i1, i2, DK_SOLID_NM, 0);
            dl_tri(i0, i2, i3, DK_SOLID_NM, 0);
        } else {
            int col = q->kind == PT_STAR ? AC_GOLD : q->kind == PT_SPLASH ? AC_CYAN : AC_WHITE;
            float sz = q->size * (q->kind == PT_STEAM ? 1 + (1 - a) * 2 : q->kind == PT_DUST ? 1 + (1 - a) * 1.2f : 1);
            dl_glow(q->p, sz * 2.2f, col, (int)(a * (q->kind == PT_DUST ? 120 : 200)));
        }
    }
    s_pf[0][9] += PF_T() - pa;
    // the near chunks of the island (lane 0)
    if (s_isl_split > s_norder) {
        s_isl_split = s_norder;
    }
    for (int k = 0; k < s_isl_split; k++) {
        dl_chunk(&s_lane[0], &s_ch[s_order[k]]);
    }
}
// the list of the island (lane 1: core 1 when it is free)
// the far chunks of the island (lane 1) and the rows of the sky and the sea: the work of core 1 while core 0 makes the
// robot, the things and the near chunks (lane 0). s_isl_split: the first far chunk (it follows the times of the cores)
static void world_list(void)
{
    int64_t pa = PF_T();
    for (int k = s_isl_split; k < s_norder; k++) {
        dl_chunk(&s_lane[1], &s_ch[s_order[k]]);
    }
    rows_prepare();
    s_isl_t1 = PF_T() - pa;
    s_pf[1][10] += s_isl_t1;
}

// ---------------------------------------------------------------- the tiles of the two cores
static uint32_t *s_tbuf[2];  // the px of the tile of each core (internal RAM if free; on 32 bytes)
static void *s_tbuf_mem[2];  // (the blocks of the buffers)
static bool s_tint_ram;
// tile k (the bins, the sky and the sea, the solids, the shadows, the lights, the soft edges) into the frame
static void tile_render(int k, int core)
{
    tile_t t;
    t.p = s_tbuf[core];
    t.core = core;
    t.x0 = (k % TCOLS) * TLW;
    t.y0 = (k / TCOLS) * TLH;
    t.x1 = t.x0 + TLW < VW ? t.x0 + TLW : VW;
    t.y1 = t.y0 + TLH < VH ? t.y0 + TLH : VH;
    uint64_t *cy = s_cy[core];
    uint32_t c0 = cy_now();
    memset(t.p, 0, sizeof(uint32_t) * TLW * TLH);
    memset(t.e, 0, sizeof t.e);
    uint32_t c1 = cy_now();
    cy[CY_CLEAR] += c1 - c0;
    dl_draw(&t, k, DK_SOLID, DK_SOLID_NM);
    c0 = cy_now();
    cy[CY_SOLID] += c0 - c1;
    bg_tile(&t);
    c1 = cy_now();
    cy[CY_BG] += c1 - c0;
    dl_draw(&t, k, DK_DARK, DK_DARK);
    c0 = cy_now();
    cy[CY_DARK] += c0 - c1;
    dl_draw(&t, k, DK_ADD, DK_ADD);
    c1 = cy_now();
    cy[CY_ADD] += c1 - c0;
    for (int q = 0; q < s_gld->n; q++) {
        const glow_t *g = &s_gld->g[q];
        glow_add(&t, g->x, g->y, g->r, g->z, ADD_COL[g->col][0], ADD_COL[g->col][1], ADD_COL[g->col][2], g->a);
    }
    c0 = cy_now();
    cy[CY_GLOW] += c0 - c1;
    edges_soften(&t);
    c1 = cy_now();
    cy[CY_EDGE] += c1 - c0;
    tile_out_hud(&t);
    cy[CY_OUT] += cy_now() - c1;
}
// the tiles of one core: of each 16 tiles in a row, core 0 takes s_share and core 1 the others (spread: neighbours
// have about the same work); s_share follows the times of the two cores (core 1 also sends the frames to the screen)
static int s_share = 8;
static inline bool tile_core0(int k)
{
    return ((k * 7) & 15) < s_share;
}
#if CB_ROUND
// (the round: a tile all out of the circle is not drawn; its px never show)
static bool tile_out_of_circle(int k)
{
    const float x0 = (k % TCOLS) * TLW * KX, y0 = (k / TCOLS) * TLH * KY, x1 = x0 + TLW * KX, y1 = y0 + TLH * KY,
                c = OW * 0.5f;
    const float nx = c < x0 ? x0 : c > x1 ? x1 : c, ny = c < y0 ? y0 : c > y1 ? y1 : c;
    return (nx - c) * (nx - c) + (ny - c) * (ny - c) > (c + 1) * (c + 1);
}
#endif
static void tiles_of(int core)
{
    for (int k = 0; k < NTILE; k++) {
        if (tile_core0(k) != (core == 0)) {
            continue;
        }
#if CB_ROUND
        if (tile_out_of_circle(k)) {
            continue;  // (its px do not show on the round panel: no PSRAM writes)
        }
#endif
        tile_render(k, core);
    }
}
static int64_t s_t1_us, s_tb_us, s_ts_us, s_tl_us;
// THE PIPELINE: set s_setb is made now, set s_setd is drawn now (the list of the frame before)
static int s_setb, s_setd;
static bool s_have_d;  // the draw set holds a list
static void sets_point(void)
{
    s_pv = s_pvm[s_setb];
    s_dt = s_dtm[s_setb];
    s_bin = s_binm[s_setb];
    s_bin_at = s_bin_atm[s_setb];
    s_glb = &s_gls[s_setb];
    s_hudb = &s_huds[s_setb];
    s_bg = &s_bgs[s_setb];
    s_pvd = s_pvm[s_setd];
    s_dtd = s_dtm[s_setd];
    s_bind = s_binm[s_setd];
    s_bin_atd = s_bin_atm[s_setd];
    s_gld = &s_gls[s_setd];
    s_hudd = &s_huds[s_setd];
    s_bgd = &s_bgs[s_setd];
}
// the set that was made becomes the draw set; the next list goes into the other set (pipe) or into the same set
static void sets_done(bool pipe)
{
    s_setd = s_setb;
    if (pipe) {
        s_setb ^= 1;
    }
    s_have_d = true;
    sets_point();
}
#ifdef ESP_PLATFORM
static TaskHandle_t s_wk_task;
static SemaphoreHandle_t s_wk_go, s_wk_done;
static volatile bool s_wk_quit;
static volatile int s_wk_job;  // 0 the tiles of core 1, 1 the list of the island, 2 both (the pipeline)
static int s_wk_frames;
static volatile bool s_l0_done;  // (the pipeline: core 0 has made its part of the new list)
static int64_t s_bins_us;
static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_wk_go, portMAX_DELAY);
        if (s_wk_quit) {
            break;
        }
        int64_t t0 = esp_timer_get_time();
        if (s_wk_job == 1) {
            world_list();
        } else if (s_wk_job == 2) {
            // (the pipeline: the island and the rows of the build set (little PSRAM traffic while the screen task reads
            // the frame before), then the tiles of the draw set)
            tiles_of(1);
            world_list();
            // (the bins of the new list, when the part of core 0 is done)
            while (!s_l0_done) {
                MEMW();
            }
            __sync_synchronize();
            int64_t tb0 = esp_timer_get_time();
            bins_build();
            s_t1_us = esp_timer_get_time() - t0;
            s_bins_us = esp_timer_get_time() - tb0;
            if (++s_wk_frames % 30 == 0) {
                vTaskDelay(1);
            }
        } else {
            tiles_of(1);
            s_t1_us = esp_timer_get_time() - t0;
            // (each 30 frames a short sleep: the idle task of core 1 must run, or the task watchdog warns)
            if (++s_wk_frames % 30 == 0) {
                vTaskDelay(1);
            }
        }
        xSemaphoreGive(s_wk_done);
    }
    xSemaphoreGive(s_wk_done);
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
#endif
#define WORKER_STACK 3072
static void worker_start(void)
{
#ifdef ESP_PLATFORM
    if (s_wk_task) {
        return;
    }
    s_wk_quit = false;
    if (!s_wk_go) {
        s_wk_go = xSemaphoreCreateBinary();
    }
    if (!s_wk_done) {
        s_wk_done = xSemaphoreCreateBinary();
    }
    bool istack = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= WORKER_STACK + 14000;
    if (!s_wk_go || !s_wk_done ||
        xTaskCreatePinnedToCoreWithCaps(worker, "cubby", WORKER_STACK, NULL, 3, &s_wk_task, 1,
                                        istack ? MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
                                               : MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_wk_task = NULL;
    }
#endif
}
static void worker_stop(void)
{
#ifdef ESP_PLATFORM
    if (s_wk_task) {
        s_wk_quit = true;
        xSemaphoreGive(s_wk_go);
        xSemaphoreTake(s_wk_done, pdMS_TO_TICKS(500));
        vTaskDeleteWithCaps(s_wk_task);
        s_wk_task = NULL;
    }
    // (each open of the app file starts with its statics at zero: the semaphores of this open go now)
    if (s_wk_go) {
        vSemaphoreDelete(s_wk_go);
        s_wk_go = NULL;
    }
    if (s_wk_done) {
        vSemaphoreDelete(s_wk_done);
        s_wk_done = NULL;
    }
#endif
}
static bool s_world_busy;
// the list of the island: on core 1 (it ends in world_wait), or here when there is no core 1 task
static void world_kick(void)
{
#ifdef ESP_PLATFORM
    if (s_wk_task) {
        s_wk_job = 1;
        s_world_busy = true;
        xSemaphoreGive(s_wk_go);
        return;
    }
#endif
    world_list();
}
// the end of the job of core 1: never go on while it still works (a long wait: one log line, then wait on)
static void core1_wait(const char *what)
{
#ifdef ESP_PLATFORM
    if (xSemaphoreTake(s_wk_done, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "core 1 is slow (%s)", what);
        xSemaphoreTake(s_wk_done, portMAX_DELAY);
    }
#else
    (void)what;
#endif
}
static void world_wait(void)
{
#ifdef ESP_PLATFORM
    if (s_world_busy) {
        core1_wait("the island");
        s_world_busy = false;
    }
#endif
}
static void frame_draw(void)
{
    int64_t t0 = esp_timer_get_time();
    float alpha = clampf(s_acc * (1.0f / STEP_DT), 0, 1);
    list_begin();
#ifdef ESP_PLATFORM
    if (s_wk_task && s_have_d) {
        // THE PIPELINE: core 1 draws its tiles of the frame before, then makes the island and the rows of this frame;
        // this core makes the robot and the things of this frame, then draws its tiles of the frame before. The
        // screen shows the world one frame late; the list work runs at the same time as the tiles.
#if !CB_ROUND
        s_isl_split = 0;
#endif
        s_l0_done = false;
        s_wk_job = 2;
        xSemaphoreGive(s_wk_go);
        list_build(alpha, false);
        __sync_synchronize();
        s_l0_done = true;
        int64_t tl = esp_timer_get_time();
        tiles_of(0);
        int64_t t_0 = esp_timer_get_time() - t0;
        core1_wait("the frame");
        int64_t t1 = esp_timer_get_time();
        s_pf[0][11] += s_bins_us;      // (the bins: on core 1)
        s_pf[0][10] += t1 - t0 - t_0;  // (the wait for core 1)
        // (the share of the tiles: the two cores end at the same time)
        if (t_0 + 1200 < s_t1_us && s_share < 15) {
            s_share++;
        } else if (s_t1_us + 1200 < t_0 && s_share > 1) {
            s_share--;
        }
#if CB_ROUND
        // (the round: the flush of the 466 frame runs on core 1; when this core waits for it, the near chunks of the
        // island come here (lane 0) and core 1 makes fewer)
        if (t1 - t0 - t_0 > 1500 && s_isl_split < s_norder) {
            s_isl_split++;
        } else if (t_0 > s_t1_us + 1500 && s_isl_split > 0) {
            s_isl_split--;
        }
#endif
        s_tl_us += tl - t0;
        s_tb_us += t1 - tl;
        s_ts_us += t_0 > s_t1_us ? t_0 - s_t1_us : s_t1_us - t_0;
        sets_done(true);
        return;
    }
#endif
    list_build(alpha, true);
    int64_t tb = PF_T();
    world_wait();
    int64_t tw = PF_T();
    s_pf[0][10] += tw - tb;  // (the wait for core 1)
    // the border of the near and the far chunks: core 0 takes more when it waited, less when core 1 waited
    if (tw - tb > 400 && s_isl_split < s_norder) {
        s_isl_split++;
    } else if (tw - tb < 50 && s_isl_t1 + 1500 < tb - t0 && s_isl_split > 0) {
        s_isl_split--;
    }
    bins_build();
    int64_t t1 = esp_timer_get_time();
    s_pf[0][11] += t1 - tw;
    s_tl_us += t1 - t0;
    // (one set: the list of this frame is drawn now; with core 1 the next frame starts the pipeline)
    s_setd = s_setb;
    sets_point();
#ifdef ESP_PLATFORM
    if (s_wk_task) {
        sets_done(true);
        s_wk_job = 0;
        xSemaphoreGive(s_wk_go);
        tiles_of(0);
        int64_t t_0 = esp_timer_get_time() - t1;
        core1_wait("the tiles");
        if (t_0 + 1500 < s_t1_us && s_share < 14) {
            s_share++;
        } else if (s_t1_us + 1500 < t_0 && s_share > 2) {
            s_share--;
        }
        s_tb_us += esp_timer_get_time() - t1;
        s_ts_us += t_0 > s_t1_us ? t_0 - s_t1_us : s_t1_us - t_0;  // (the wait of the faster core)
        return;
    }
#endif
    tiles_of(0);
    tiles_of(1);
    s_tb_us += esp_timer_get_time() - t1;
    sets_done(false);
}

// ---------------------------------------------------------------- the HUD
#if CB_WIDE
#define HUD_IX  22
#define HUD_IY  20
#define HUD_GAP 26
#elif CB_ROUND
// (the round: the SUNDROPS and the sparks in the middle of the top arc)
#define HUD_IX  (OW / 2 - OW * 112 / 466)
#define HUD_IY  (OH * 12 / 100)
#define HUD_GAP (OW * 29 / 466)
#else
#define HUD_IX  20
#define HUD_IY  18
#define HUD_GAP 24
#endif
#if CB_ROUND
#define HS       1.5f  // (the round: the small HUD shapes 1.5 times bigger: the px of the 1.75C are 0.095 mm)
#define HUD_FONT MF_LABEL
#else
#define HS       1.0f
#define HUD_FONT MF_SMALL
#endif
// the HUD of the play: the SUNDROPS, the sparks, JUMP, ACT, the stick, the slow time (shapes in the tiles)
static void hud_shapes(void)
{
    for (int k = 0; k < NDROP; k++) {
        float cx = HUD_IX + k * HUD_GAP, cy = HUD_IY;
        if (s_drop[k].got) {
            hud_disc(cx, cy, 12 * HS, 0xFFAA28, 90);
            hud_disc(cx, cy, 9 * HS, 0xFFC432, 255);
            hud_disc(cx - 3 * HS, cy - 3 * HS, 4 * HS, 0xFFF6BE, 255);
        } else {
            hud_disc(cx, cy, 9 * HS, 0x142846, 110);
            hud_ring(cx, cy, 9.5f * HS, 2, 0xFFE6AA, 210);
        }
    }
    float sx = HUD_IX + NDROP * HUD_GAP;
    hud_disc(sx, HUD_IY, 6 * HS, 0xFFC83C, 255);
    hud_disc(sx, HUD_IY, 2 * HS, 0x965A14, 255);
    // JUMP: a dark ring and a white ring (seen on the bright and the dark scene); a gold fill while it is held
    bool jd = P.jump_held || (!P.ground && P.big && s_slow > 0.1f);
    if (jd) {
        hud_disc(JX, JY, JR, 0xFFD25A, 150);
    }
    hud_ring(JX, JY, JR + 1.5f, 2.5f, 0x14284A, 120);
    hud_ring(JX, JY, JR, 3, 0xFFFFFF, 220);
    if (P.jump_held && P.jump_t >= 0) {
        float k = clampf(P.jump_t / HOLD_BIG, 0, 1);
        float a0 = -PI_F * 0.5f, a1 = a0 + TAU_F * k;
        int n = (int)(12 * k) + 1;
        for (int i = 0; i < n; i++) {
            float u0 = a0 + (a1 - a0) * i / n, u1 = a0 + (a1 - a0) * (i + 1) / n, rr = JR - 7;
            hud_line(JX + fcos(u0) * rr, JY + fsin(u0) * rr, JX + fcos(u1) * rr, JY + fsin(u1) * rr, 5, 0xFFF0A0, 255);
        }
    }
    for (int k = 0; k < 2; k++) {
        float yy = JY - 4 + k * 10;
        hud_line(JX - 12, yy + 6, JX, yy - 2, 4, 0xFFFFFF, 255);
        hud_line(JX, yy - 2, JX + 12, yy + 6, 4, 0xFFFFFF, 255);
    }
    // ACT: the colour and the icon tell what it does now (scoop, place, pick up); no action = only a faint ring
    int ak = act_kind();
    uint32_t ac = ak == 1 ? 0x5ADC78 : ak == 2 ? 0x5ABEFF : ak == 3 ? 0xFFAA5A : 0x142850;
    if (ak) {
        hud_disc(AX, AY, AR, ac, 190);
    }
    hud_ring(AX, AY, AR + 1.5f, 2.5f, 0x14284A, 110);
    hud_ring(AX, AY, AR, 3, 0xFFFFFF, ak ? 230 : 110);
    float cs = AR * 0.62f;
    uint32_t ic = ak ? 0xFFFFFF : 0x96A0BE;
    hud_rect(AX - cs / 2, AY - cs / 2 + 4, cs, cs, ic, 255);
    hud_rect(AX - cs / 2 + 3, AY - cs / 2 + 7, cs - 6, cs - 6, ak ? ac : 0x50607E, 255);
    if (ak == 1) {
        for (int i = 0; i < 3; i++) {
            hud_disc(AX - 8 + i * 8, AY - cs / 2 - 4 - (i & 1) * 4, 3, 0xFFFFFF, 255);
        }
    }
    if (ak == 2 || ak == 3) {
        float ty = AY - cs / 2 - 7, tip = ak == 2 ? ty + 5 : ty - 6, tail = ak == 2 ? ty - 6 : ty + 5;
        hud_line(AX, tail, AX, tip, 3.5f, 0xFFFFFF, 255);
        hud_line(AX - 5, tip + (ak == 2 ? -5 : 5), AX, tip, 3.5f, 0xFFFFFF, 255);
        hud_line(AX + 5, tip + (ak == 2 ? -5 : 5), AX, tip, 3.5f, 0xFFFFFF, 255);
    }
    // the stick: its ring where the thumb is down (and a faint hint in the first seconds)
    bool any = false;
    for (int k = 0; k < NTOUCH; k++) {
        const touch_t *t = &s_tc[k];
        if (!t->on || t->role != TR_STICK) {
            continue;
        }
        any = true;
        hud_ring(t->bx, t->by, STICK_R + 7, 2.5f, 0x14284A, 110);
        hud_ring(t->bx, t->by, STICK_R + 6, 2.5f, 0xFFFFFF, 170);
        float dx = t->x - t->bx, dy = t->y - t->by, d = fsqrt(dx * dx + dy * dy);
        if (d > STICK_R) {
            dx *= STICK_R / d;
            dy *= STICK_R / d;
        }
        hud_disc(t->bx + dx, t->by + dy, 16, 0xFFFFFF, 190);
    }
    if (!any && s_hint_t < 14) {
        int a = (int)(110 * clampf((14 - s_hint_t) / 3, 0, 1));
#if CB_WIDE
        float hx = 110, hy = 112 + CB_DY;
#elif CB_ROUND
        float hx = OW * 0.26f, hy = OH * 0.66f;
#else
        float hx = 70, hy = 180;
#endif
        hud_ring(hx, hy, STICK_R + 6, 2.5f, 0xFFFFFF, a);
        hud_disc(hx, hy, 14, 0xFFFFFF, a);
    }
    // the slow time: dark bars at the top and the bottom with a gold line
    if (s_slow > 0.02f) {
        float h = s_slow * (OH / 14);
        hud_dark(0, 0, OW, h, 110);
        hud_dark(0, OH - h, OW, h, 110);
        hud_rect(0, h, OW, 1, 0xFFD26E, 255);
        hud_rect(0, OH - h - 1, OW, 1, 0xFFD26E, 255);
    }
}
// the text of the HUD (onto the frame, after the tiles)
static void hud_text(void)
{
    char buf[24];
    if (s_fr.on && s_fr.seen) {
        float sx, sy, z;
        if (world_proj(V3(s_fr.p.x, s_fr.p.y + 1.8f, s_fr.p.z), &sx, &sy, &z) && z < 28 && sy > 12 && sy < VH - 4) {
            mf_text_oc(&MF_SMALL, (int)(sx * KX), (int)(sy * KY), s_fr.name, rgb(150, 250, 255), rgb(10, 40, 80), 1, 1);
        }
    }
    if (s_left_t >= 0) {
        mf_text_oc(&MF_N20, OW / 2, OH / 2 - 24, "FRIEND LEFT", rgb(255, 255, 255), rgb(30, 50, 90), 2, 1);
    }
    snprintf(buf, sizeof buf, "%d/%d", s_nsparks, NSPARK);
    mf_text_o(&HUD_FONT, HUD_IX + NDROP * HUD_GAP + (int)(10 * HS), HUD_IY + (int)(5 * HS), buf, rgb(255, 255, 255),
              rgb(30, 40, 70), 1, 1);
#if CB_WIDE
    int cy = 64 + CB_DY / 2;
#elif CB_ROUND
    int cy = OH * 30 / 100;
#else
    int cy = 92;
#endif
    if (s_win_t > 1.4f && s_win_t < 9) {
        float k = smooth01((s_win_t - 1.4f) / 0.5f) * (1 - smooth01((s_win_t - 8.3f) / 0.7f));
        if (k > 0.03f) {
            mf_text_oc(&MF_N28, OW / 2, cy, "SUNDROP ISLE", rgb(255, 230, 120), rgb(110, 50, 20), 2, 2);
            snprintf(buf, sizeof buf, "%d/%d  +  %d/%d", NDROP, NDROP, s_nsparks, NSPARK);
            mf_text_oc(&MF_N20, OW / 2, cy + 30, buf, rgb(255, 255, 255), rgb(30, 50, 90), 2, 1);
        }
    } else if (s_drop_name >= 0 && s_drop_name_t > 0.6f && s_drop_name_t < 3.6f && s_win_t < 0) {
        // (the name of the SUNDROP that the robot found, and the count)
        mf_text_oc(&MF_N20, OW / 2, cy, DROP_NAME[s_drop_name], rgb(255, 230, 120), rgb(110, 50, 20), 2, 1);
        snprintf(buf, sizeof buf, "%d/%d", s_ndrops, NDROP);
        mf_text_oc(&MF_N20, OW / 2, cy + 26, buf, rgb(255, 255, 255), rgb(30, 50, 90), 2, 1);
    }
}
#if CB_WIDE
#define TITLE_CY  (70 + CB_DY / 2)
#define TITLE_SY  (100 + CB_DY / 2)
#define FRIEND_BX (OW / 2 + 128)
#elif CB_ROUND
#define TITLE_CY  (OH * 28 / 100)
#define TITLE_SY  (OH * 28 / 100 + 32)
#define FRIEND_BX (OW / 2 + OW * 100 / 466)
#else
#define TITLE_CY  96
#define TITLE_SY  128
#define FRIEND_BX (OW / 2 + 96)
#endif
#define FRIEND_BY (TITLE_SY + 30)
static void title_shapes(void)
{
    float px = OW / 2, py = TITLE_SY + 30, pa = 150 + 100 * fsin(s_rt * 3);
    hud_disc(px, py, 20, 0x14284A, (int)(pa * 0.5f));
    hud_ring(px, py, 20, 2.5f, 0xFFFFFF, (int)pa);
    hud_line(px - 4, py - 8, px + 7, py, 4, 0xFFFFFF, 255);
    hud_line(px + 7, py, px - 4, py + 8, 4, 0xFFFFFF, 255);
    hud_line(px - 4, py - 8, px - 4, py + 8, 4, 0xFFFFFF, 255);
    // 2 BOARDS: two small robot heads in a ring
    if (s_net == N2_OFF) {
        float fx = FRIEND_BX, fy = FRIEND_BY;
        hud_disc(fx, fy, 20, 0x14284A, 75);
        hud_ring(fx, fy, 20, 2.5f, 0xFFFFFF, 200);
        hud_disc(fx - 6, fy + 1, 6.5f, 0xFFB42C, 255);
        hud_disc(fx + 6, fy + 1, 6.5f, 0x2FD8E6, 255);
        hud_disc(fx - 6, fy, 2.5f, 0x14284A, 255);
        hud_disc(fx + 6, fy, 2.5f, 0x14284A, 255);
    }
}
static void title_text(void)
{
    mf_text_oc(&MF_N34, OW / 2, TITLE_CY, "CUBBY", rgb(255, 196, 50), rgb(120, 50, 10), 3, 4);
    mf_text_oc(&MF_LABEL, OW / 2, TITLE_SY, "SUNDROP ISLE", rgb(255, 255, 255), rgb(30, 60, 110), 1, 2);
    if (s_net == N2_OFF) {
        mf_text_oc(&HUD_FONT, FRIEND_BX, FRIEND_BY + 34, "2 BOARDS", rgb(255, 255, 255), rgb(30, 60, 110), 1, 1);
    }
}

// ---------------------------------------------------------------- the panel
enum {
    MI_TILT,
    MI_SOUND,
    MI_MUSIC,
    MI_RESTART,
    MI_TITLE
};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_TILT, 2, 0, {"TILT LOOK", "מבט בהטיה"}, ONOFF_V, CB_KEY "tilt"},
        {PI_PICK, MI_SOUND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, CB_KEY "snd"},
        {PI_PICK, MI_MUSIC, 2, 0, {"MUSIC", "מוזיקה"}, ONOFF_V, CB_KEY "mus"},
        {PI_ACT2, MI_RESTART, 0, 0, {"NEW START", "התחלה חדשה"}, NULL, NULL},
        {PI_ACT, MI_TITLE, 0, 0, {"THE TITLE", "למסך הפתיחה"}, NULL, NULL},
};
static void play_start(void);
static bool item_shown(int id)
{
    return (id == MI_RESTART || id == MI_TITLE) ? s_gs == GS_PLAY : true;
}
static void item_set(int id, int v)
{
    if (id == MI_TILT) {
        s_tilt_on = !(v & 1);
    } else if (id == MI_SOUND) {
        s_sound = !(v & 1);
    } else if (id == MI_MUSIC) {
        bool on = !(v & 1);
        if (on == s_music) {
            return;
        }
        s_music = on;
        s_mus_cur = -1;
        if (on) {
            midi_bg_start();
            music_to(s_gs == GS_PLAY ? CBM_ISLAND : CBM_TITLE);
        } else {
            midi_bg_stop(300);
        }
    } else if (id == MI_RESTART) {
        net_event(NE_RESTART, 0, 0, 0, 0);
        play_start();
    } else if (id == MI_TITLE) {
        net_stop();
        game_reset();
        s_gs = GS_TITLE;
        music_to(CBM_TITLE);
    }
}
static void item_opened(void)
{
    audio_drone(0, 0, 0, W_TRI);
    audio_drone(1, 0, 0, W_NOISE);
    s_mot_vol = 0;
}
static void item_resume(void)
{
    imu_set_level();
    s_last = 0;
}
static const pitems_t PANEL = {ITEMS, 5, item_shown, NULL, item_set, item_opened, item_resume};

// ---------------------------------------------------------------- the app
static void *cb_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) {
        memset(p, 0, n);
    }
    return p;
}
static void cb_free_all(void)
{
    hmask_free();
    for (int k = 0; k < 2; k++) {
        heap_caps_free(s_binm[k]);
        s_binm[k] = NULL;
        heap_caps_free(s_pvm[k]);
        s_pvm[k] = NULL;
        heap_caps_free(s_dtm[k]);
        s_dtm[k] = NULL;
    }
    s_bin = s_bind = NULL;
    s_pv = s_pvd = NULL;
    s_dt = s_dtd = NULL;
    heap_caps_free(s_dq_mem);
    s_dq_mem = NULL;
    DQ = DQ_PS;
    for (int k = 0; k < 2; k++) {
        heap_caps_free(s_tbuf_mem[k]);
        s_tbuf_mem[k] = NULL;
        s_tbuf[k] = NULL;
    }
    heap_caps_free(s_bv);
    s_bv = NULL;
    heap_caps_free(s_bt);
    s_bt = NULL;
    heap_caps_free(s_wv);
    s_wv = NULL;
    heap_caps_free(s_wt);
    s_wt = NULL;
    heap_caps_free(s_mv);
    s_mv = NULL;
    heap_caps_free(s_mt);
    s_mt = NULL;
}
static void play_start(void)
{
    game_reset();
    if (s_fr.on && s_net == N2_CLIENT) {
        P.p = V3(8.6f, 0, 3.3f);
        P.p0 = P.p;
        P.safe = P.p;
    }
    s_gs = GS_PLAY;
    P.act = A_BOOT;
    P.act_t = 0;
    s_hint_t = 0;
    imu_set_level();
    if (s_sound) {
        JPLAY(CB_GO);
    }
    s_mus_cur = -1;
    music_to(CBM_ISLAND);
}
static int s_fps_n;
static int64_t s_fps_t0;
static void cb_enter(int64_t now)
{
    s_ok = false;
#if CB_ROUND
    // (the round layout draws into a frame of OW x OH: another frame size would write out of it)
    if (gfx_w != OW || gfx_h != OH) {
        ESP_LOGE(TAG, "the frame is %d x %d, not %d x %d", gfx_w, gfx_h, OW, OH);
        return;
    }
    circle_make();
    s_isl_split = 0;
#endif
    for (int k = 0; k < 2; k++) {
        s_binm[k] = cb_alloc(sizeof(uint16_t) * BINMAX);
        s_pvm[k] = cb_alloc(sizeof(pv_t) * PMAXV);
        s_dtm[k] = cb_alloc(sizeof(dt_t) * DMAXT);
    }
    s_setb = s_setd = 0;
    s_have_d = false;
    sets_point();
    // the tiles of the two cores: internal RAM when enough stays free for the system, else PSRAM (slower)
    s_tint_ram = false;
#ifdef ESP_PLATFORM
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= 2 * (TLW * TLH * 4 + 32) + 22000) {
        s_tbuf_mem[0] = heap_caps_malloc(TLW * TLH * 4 + 32, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        s_tbuf_mem[1] = heap_caps_malloc(TLW * TLH * 4 + 32, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        s_tint_ram = s_tbuf_mem[0] && s_tbuf_mem[1];
    }
#endif
    for (int k = 0; k < 2; k++) {
        if (!s_tbuf_mem[k]) {
            s_tbuf_mem[k] = cb_alloc(TLW * TLH * 4 + 32);
        }
        if (s_tbuf_mem[k]) {
            s_tbuf[k] = (uint32_t *)(((uintptr_t)s_tbuf_mem[k] + 31) & ~(uintptr_t)31);
        }
    }
    s_bv = cb_alloc(sizeof(bv_t) * BMAXV);
    s_bt = cb_alloc(sizeof(tri_t) * BMAXT);
    s_wv = cb_alloc(sizeof(wv_t) * WMAXV);
    s_wt = cb_alloc(sizeof(tri_t) * WMAXT);
    s_mv = cb_alloc(sizeof(mv_t) * MMAXV);
    s_mt = cb_alloc(sizeof(tri_t) * MMAXT);
    if (!s_binm[0] || !s_binm[1] || !s_pvm[0] || !s_pvm[1] || !s_dtm[0] || !s_dtm[1] || !s_tbuf[0] || !s_tbuf[1] ||
        !s_bv || !s_bt || !s_wv || !s_wt || !s_mv || !s_mt) {
        ESP_LOGE(TAG, "no memory");
        cb_free_all();
        return;
    }
    int64_t t0 = esp_timer_get_time();
    (void)t0;
    L_SUN = vnorm(V3(-0.5f, 0.62f, -0.6f));
    tone_init();
    // (the dither table in internal RAM: each px of a triangle reads it)
    s_dq_mem = heap_caps_malloc(sizeof(dq_t) * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    DQ = s_dq_mem ? (dq_t *)s_dq_mem : DQ_PS;
    dq_init();
    for (int i = 0; i < 256; i++) {
        SINT[i] = (int8_t)(fsin(i * TAU_F / 256) * 127);
    }
    s_mnv = s_mnt = 0;
    robot_build();
    // (the models that do not turn: the light baked now)
    for (int m = M_LAMP_TIN; m < M_COUNT; m++) {
        if (m != M_BODY_F && m != M_HATCH_F) {
            model_bake(&s_mod[m]);
        }
    }
    s_gsat = GRADE_SAT;  // (the island: the soft grade; the robot and the things: HOT_SAT)
    world_build();
    s_gsat = HOT_SAT;
    // (the builder is not necessary after the build)
    heap_caps_free(s_bv);
    s_bv = NULL;
    heap_caps_free(s_bt);
    s_bt = NULL;
    ESP_LOGI(TAG, "built in %d ms: world %d corners %d triangles %d chunks, models %d corners %d triangles",
             (int)((esp_timer_get_time() - t0) / 1000), s_wnv, s_wnt, s_nch, s_mnv, s_mnt);
    s_tilt_on = !store_get(CB_KEY "tilt", 0);
    if (store_get(CB_KEY "bench", 0)) {
        store_set(CB_KEY "bench", 0);
        s_bench = 1;
        s_bench_f = 0;
    }
    s_sound = !store_get(CB_KEY "snd", 0);
    s_music = !store_get(CB_KEY "mus", 0);
    midi_bg_init(CB_TRACKS, CB_TRACK_COUNT, CB_MUSIC_VOICES);
    midi_bg_volume(55);
    s_mus_cur = -1;
    s_mus_jt = 0;
    if (s_music) {
        midi_bg_start();
        music_to(CBM_TITLE);
    }
    motor_make();  // (the samples of the motor and the cleats)
    static const char *const *names = FX_NAMES;
    tfsfx_load_mem(CB_SFX_DATA, CB_SFX_LEN, names, FX_N < CB_SFX_N ? FX_N : CB_SFX_N, 46);
    sysui_panel_items(&PANEL);
    game_reset();
    s_gs = GS_TITLE;
    s_rt = 0;
    s_t = 0;
    s_last = now;
    s_fps_n = 0;
    s_fps_t0 = 0;
    s_tl_us = s_tb_us = s_ts_us = 0;
    worker_start();
    imu_set_level();
    ESP_LOGI(TAG, "tiles in %s", s_tint_ram ? "internal RAM" : "PSRAM");

    s_ok = true;
}
static void cb_leave(void)
{
    net_stop();
    worker_stop();
    audio_drone(0, 0, 0, W_TRI);
    audio_drone(1, 0, 0, W_NOISE);
    motor_end();
    midi_bg_end();
    tfsfx_free();
    cb_free_all();
    s_ok = false;
}
static void cb_input_ev(const input_event_t *ev)
{
    if (!s_ok) {
        return;
    }
    if (s_gs != GS_TITLE || ev->kind != IN_TAP) {
        return;
    }
    if (s_net == N2_LOBBY) {
        // (a tap away from the friends stops the lobby)
        if (!net2_lobby_tap(ev->x, ev->y)) {
            net_stop();
        }
        return;
    }
    float dx = ev->x - FRIEND_BX, dy = ev->y - FRIEND_BY;
    if (s_net == N2_OFF && dx * dx + dy * dy < 44 * 44) {
        net_lobby_open();
        return;
    }
    play_start();
}
// a slow tap (a press of 350 - 900 ms) on the title acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void cb_input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    cb_input_ev(ev);
    if (slow && (s_gs == GS_TITLE)) {
        cb_input_ev(&tap);
    }
}
// THE BENCH (a test of the speed; the console: "store cubby_bench 1", then "open CUBBY"): fixed views of the island
// (the robot at 3 places, the camera from 4 sides; then the title), BENCH_FR frames each; the log gives the mean time
// of a frame of each view (from frame BENCH_SKIP on), then the mean of all. The views are the same in each build.
#define BENCH_FR   50
#define BENCH_SKIP 15
#define BENCH_NV   13
static const float BENCH_AT[3][2] = {{33.0f, 7.0f}, {21.0f, 22.0f}, {42.0f, 31.0f}};
static int64_t s_bench_prev, s_bench_sum, s_bench_rsum, s_bench_usum;
static int s_bench_n;
static float s_bench_ms[BENCH_NV];
static void bench_update(void)
{
    int v = s_bench - 1;
    if (s_bench_f == 0) {
        s_cam_init = false;
        s_bench_sum = 0;
        s_bench_n = 0;
        s_bench_rsum = 0;
        s_bench_usum = 0;
    }
    if (v < 12) {
        s_gs = GS_PLAY;
        float x = BENCH_AT[v / 4][0], z = BENCH_AT[v / 4][1];
        P.p = V3(x + 0.5f, floor_at(x + 0.5f, z + 0.5f, 99, NULL), z + 0.5f);
        P.p0 = P.p;
        P.v = V3(0, 0, 0);
        P.yaw = P.yaw0 = (v & 3) * (PI_F * 0.5f) + 0.4f;
        P.speed = 0;
        s_cam_yaw = P.yaw;
    } else {
        s_gs = GS_TITLE;
        s_rt = 3.0f;
    }
}
static void bench_frame(int64_t now)
{
    if (s_bench_prev && s_bench_f >= BENCH_SKIP) {
        s_bench_sum += now - s_bench_prev;
        s_bench_n++;
    }
    if (s_bench_f == BENCH_SKIP) {
        memset(s_cy, 0, sizeof s_cy);
        memset(s_cn, 0, sizeof s_cn);
        memset(s_pf, 0, sizeof s_pf);
    }
    s_bench_prev = now;
    if (++s_bench_f < BENCH_FR) {
        return;
    }
    int v = s_bench - 1;
    s_bench_ms[v] = s_bench_n ? s_bench_sum * 0.001f / s_bench_n : 0;
    {
        int n = s_bench_n > 0 ? s_bench_n : 1;
        float kn = 1.0f / (1000.0f * n);
        (void)kn;
#define CYS(i) (int)((s_cy[0][i] + s_cy[1][i]) * kn)
#define CNS(i) (int)((s_cn[0][i] + s_cn[1][i]) / (uint32_t)n)
        ESP_LOGI(TAG,
                 "CUBBY BENCH view %d: %.1f ms (render %.1f, update %.1f) | kc solid %d (setup %d, rows+px %d), bg %d, "
                 "edge %d, out %d, dark+add+glow %d | "
                 "mcorn %d (%d), mtri %d, ccorn %d (%d), ctri %d (%d) | px %d, setups %d | robot %.1f, things %.1f, "
                 "island %.1f, bins %.1f ms",
                 v, s_bench_ms[v], s_bench_rsum * 0.001f / n, s_bench_usum * 0.001f / n, CYS(CY_SOLID), CYS(CY_SETUP),
                 CYS(CY_ROWS), CYS(CY_BG), CYS(CY_EDGE), CYS(CY_OUT), CYS(CY_DARK) + CYS(CY_ADD) + CYS(CY_GLOW),
                 CYS(CY_MCORN), CNS(CN_MCORN), CYS(CY_MTRI), CYS(CY_CCORN), CNS(CN_CCORN), CYS(CY_CTRI), CNS(CN_CTRI),
                 CNS(CN_PX), CNS(CN_SETUP), s_pf[0][8] / 1000.0f / n, s_pf[0][9] / 1000.0f / n,
                 s_pf[1][10] / 1000.0f / n, s_pf[0][11] / 1000.0f / n);
#undef CYS
#undef CNS
    }
    s_bench_f = 0;
    if (++s_bench > BENCH_NV) {
        float sp = 0;
        for (int k = 0; k < 12; k++) {
            sp += s_bench_ms[k];
        }
        sp /= 12;
        (void)sp;
        ESP_LOGI(TAG, "CUBBY BENCH: play %.1f ms (%.1f fps), title %.1f ms (%.1f fps)", sp, 1000.0f / sp,
                 s_bench_ms[12], 1000.0f / s_bench_ms[12]);
        s_bench = 0;
        game_reset();
        s_gs = GS_TITLE;
        music_to(CBM_TITLE);
    }
}
static void cb_update(int64_t now)
{
    if (!s_ok) {
        return;
    }
    float rdt = s_last ? (now - s_last) * 0.001f : 0.016f;
    s_last = now;
    if (rdt > 0.1f) {
        rdt = 0.1f;
    }
    if (rdt < 0) {
        rdt = 0;
    }
    s_rt += rdt;
    if (s_gs == GS_PLAY) {
        s_hint_t += rdt;
    }
    net_frame(rdt);
    music_service(rdt);
    if (s_bench) {
        bench_update();
    }
    int64_t tu = PF_T();
    play_update(rdt, s_gs == GS_TITLE);
    if (s_bench) {
        bench_update();
    }
    camera_step(rdt, s_gs == GS_TITLE);
    s_pf[0][13] += PF_T() - tu;
    if (s_bench && s_bench_f >= BENCH_SKIP) {
        s_bench_usum += PF_T() - tu;
    }
}
static void cb_render(int64_t now)
{
    if (!s_ok) {
        gfx_fill(rgb(20, 30, 60));
        gfx_text_center(OH / 2 - 4, "NO MEMORY", 2, rgb(255, 255, 255));
        return;
    }
    int64_t tr0 = esp_timer_get_time();
    if (s_bench) {
        bench_frame(tr0);
    }
    int64_t th = PF_T();
    s_hudb->n = 0;
    if (s_gs == GS_TITLE) {
        title_shapes();
    } else {
        hud_shapes();
    }
    s_pf[0][14] += PF_T() - th;
    frame_draw();
    th = PF_T();
    if (s_gs == GS_TITLE) {
        title_text();
    } else {
        hud_text();
    }
    if (s_net == N2_LOBBY) {
        net2_draw_lobby("CUBBY");
    }
    s_pf[0][14] += PF_T() - th;
    if (s_bench && s_bench_f >= BENCH_SKIP) {
        s_bench_rsum += esp_timer_get_time() - tr0;
    }
    // the speed in the log, each 5 s
    s_fps_n++;
    if (!s_fps_t0) {
        s_fps_t0 = now;
    }
    if (now - s_fps_t0 >= 5000 && !s_bench) {
        float sec = (now - s_fps_t0) * 0.001f;
        ESP_LOGI(TAG,
                 "CUBBY: %.1f fps, list %.1f ms, tiles %.1f ms, wait %.1f ms, %d triangles, %d corners, %d bins, %d "
                 "lost",
                 s_fps_n / sec, s_tl_us / 1000.0f / s_fps_n, s_tb_us / 1000.0f / s_fps_n, s_ts_us / 1000.0f / s_fps_n,
                 s_lane[0].dn + s_lane[1].dn - DT_HALF, s_lane[0].pn + s_lane[1].pn - PV_HALF, s_bin_at[NTILE],
                 s_lane[0].dropped + s_lane[1].dropped + s_bin_lost);
        (void)sec;
        {
            int n = s_fps_n > 0 ? s_fps_n : 1;
            int64_t (*f)[16] = s_pf;
            (void)n;
            (void)f;
            ESP_LOGI(TAG,
                     "CUBBY parts (ms a frame): robot %.1f, things %.1f, island on core 1 %.1f, the wait for it %.1f, "
                     "bins %.1f, update %.1f, HUD %.1f",
                     f[0][8] / 1000.0f / n, f[0][9] / 1000.0f / n, f[1][10] / 1000.0f / n, f[0][10] / 1000.0f / n,
                     f[0][11] / 1000.0f / n, f[0][13] / 1000.0f / n, f[0][14] / 1000.0f / n);
            memset(s_pf, 0, sizeof s_pf);
            // (the cycles of each part in thousands a frame, core 0 + core 1; the counts a frame)
            float kn = 1.0f / (1000.0f * n);
            (void)kn;
#define CYS(i) (int)((s_cy[0][i] + s_cy[1][i]) * kn)
#define CNS(i) (int)((s_cn[0][i] + s_cn[1][i]) / (uint32_t)n)
            ESP_LOGI(TAG,
                     "CUBBY kcycles: clear %d, solid %d, bg %d, dark %d, add %d, glow %d, edge %d, out %d | model "
                     "corners %d (%d), model tris %d (%d), "
                     "chunk corners %d (%d), chunk tris %d (%d) | px %d, setups %d, rows %d | setup %d, rows+px %d",
                     CYS(CY_CLEAR), CYS(CY_SOLID), CYS(CY_BG), CYS(CY_DARK), CYS(CY_ADD), CYS(CY_GLOW), CYS(CY_EDGE),
                     CYS(CY_OUT), CYS(CY_MCORN), CNS(CN_MCORN), CYS(CY_MTRI), CNS(CN_MTRI), CYS(CY_CCORN),
                     CNS(CN_CCORN), CYS(CY_CTRI), CNS(CN_CTRI), CNS(CN_PX), CNS(CN_SETUP), CNS(CN_ROWS), CYS(CY_SETUP),
                     CYS(CY_ROWS));
#undef CYS
#undef CNS
            memset(s_cy, 0, sizeof s_cy);
            memset(s_cn, 0, sizeof s_cn);
            if (s_fr.on) {
                ESP_LOGI(TAG,
                         "CUBBY 2 BOARDS: %d packets of the friend, the friend at %.2f %.2f %.2f, events in %d, out "
                         "%d, SUNDROPS %d, sparks %d, hash %08x",
                         s_net_rx_n, s_fr.p.x, s_fr.p.y, s_fr.p.z, s_in_done, s_nout, s_ndrops, s_nsparks,
                         (unsigned)world_hash());
            }
            s_net_rx_n = 0;
            if (s_music) {
                midi_bg_state_t ms;
                midi_bg_state(&ms);
                ESP_LOGI(TAG, "CUBBY music: %s bar %d, %d voices, notes %d, dropped %d, thinned %d, load %d ms",
                         ms.name ? ms.name : "-", ms.bar, ms.voices, ms.notes, ms.dropped, ms.thinned,
                         midi_bg_load_ms());
            }
        }
        s_fps_n = 0;
        s_fps_t0 = now;
        s_tl_us = s_tb_us = s_ts_us = 0;
    }
}
#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set in each app; all static and unused gives no warning)
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
// the icon: CUBBY on the beach of SUNDROP COVE: he hops on his treads (a squash when he lands), his big eyes look up at
// the SUNDROP and blink; a tin cube that he made stands next to him (a loop of 2 s)
static void cb_icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s * 62 / 100, ic_g(64, 160, 224), ic_g(192, 224, 224));
    ic_dband(x, y, s, s * 62 / 100, s * 70 / 100, ic_g(32, 160, 192), ic_g(64, 192, 192));
    ic_dband(x, y, s, s * 70 / 100, s, ic_g(224, 208, 128), ic_g(192, 160, 96));
    // the SUNDROP: a gold drop that floats and shines
    float sx = x + 80 * u, sy = y + 22 * u + 2.5f * u * fsin(an);
    tfd_disc(sx, sy, 9 * u, ic_g(224, 224, 128), 70);
    float drop[] = {sx, sy - 9 * u,    sx + 5.6f * u, sy + 1 * u,    sx + 3.4f * u, sy + 5.4f * u,
                    sx, sy + 6.6f * u, sx - 3.4f * u, sy + 5.4f * u, sx - 5.6f * u, sy + 1 * u};
    tfd_poly(drop, 6, ic_g(224, 160, 32), 256);
    tfd_disc(sx - 1.6f * u, sy + 1 * u, 2.2f * u, ic_g(224, 240, 160), 256);
    // the tin cube (three faces)
    float qx = x + 18 * u, qy = y + 84 * u, q = 9 * u;
    ic_oval(qx + 2 * u, qy + 1 * u, 12 * u, 3 * u, ic_g(128, 96, 64), 200);
    float ct[] = {qx, qy - 2 * q, qx + q, qy - 1.5f * q, qx, qy - q, qx - q, qy - 1.5f * q};
    float cl[] = {qx - q, qy - 1.5f * q, qx, qy - q, qx, qy, qx - q, qy - 0.5f * q};
    float cr[] = {qx + q, qy - 1.5f * q, qx, qy - q, qx, qy, qx + q, qy - 0.5f * q};
    tfd_poly(ct, 4, ic_g(224, 224, 224), 256);
    tfd_poly(cl, 4, ic_g(160, 160, 176), 256);
    tfd_poly(cr, 4, ic_g(96, 96, 128), 256);
    tfd_line(qx - q * 0.7f, qy - 1.05f * q, qx - q * 0.15f, qy - 0.75f * q, 1.2f * u, ic_g(224, 96, 64), 256);
    // CUBBY: a hop in the first 0.8 s, a squash when he lands
    float hop = ph < 0.4f ? fsin(ph / 0.4f * 3.14159f) : 0,
          sq = ph > 0.4f && ph < 0.6f ? fsin((ph - 0.4f) / 0.2f * 3.14159f) : 0;
    float bx = x + 52 * u, gy = y + 90 * u, lift = hop * 12 * u, wk = 1 + 0.08f * sq, hk = 1 - 0.1f * sq;
    ic_oval(bx, gy, (20 - 6 * hop) * u, (3.4f - hop) * u, ic_g(128, 96, 64), 220);
    float by = gy - lift;
    // (the treads and the wheels that turn)
    tfd_rrect((int)(bx - 21 * u * wk), (int)(by - 11 * u), (int)(42 * u * wk), (int)(11 * u), (int)(5.5f * u),
              ic_g(32, 32, 48), 256);
    for (int k = 0; k < 4; k++) {
        float wx = bx + (-14 + k * 9.4f) * u * wk, wy = by - 5.5f * u, a = an * 2 + k;
        tfd_disc(wx, wy, 3.6f * u, ic_g(96, 96, 112), 256);
        tfd_disc(wx + fcos(a) * 2 * u, wy + fsin(a) * 2 * u, 1 * u, ic_g(32, 32, 48), 256);
    }
    // (the body: the dark rim, the yellow box, the shade at the right, the orange band, the shine)
    float bw = 34 * u * wk, bh = 26 * u * hk, bt = by - 10 * u - bh;
    tfd_rrect((int)(bx - bw / 2 - 1.2f * u), (int)(bt - 1.2f * u), (int)(bw + 2.4f * u), (int)(bh + 2.4f * u),
              (int)(6 * u), ic_g(96, 48, 0), 256);
    tfd_rrect((int)(bx - bw / 2), (int)bt, (int)bw, (int)bh, (int)(5 * u), ic_g(224, 160, 32), 256);
    tfd_rrect((int)(bx - bw / 2), (int)bt, (int)(bw * 0.78f), (int)(bh * 0.86f), (int)(5 * u), ic_g(224, 192, 64), 256);
    tfd_rect((int)(bx - bw / 2), (int)(bt + bh * 0.66f), (int)bw, (int)(5 * u), ic_g(224, 112, 64), 256);
    tfd_rrect((int)(bx - bw / 2 + 3 * u), (int)(bt + 3 * u), (int)(9 * u), (int)(4 * u), (int)(2 * u),
              ic_g(224, 240, 192), 220);
    // (the neck and the two big eyes: they look up at the SUNDROP in the air, and blink once)
    float ey = bt - 7 * u, look = 0.3f + 0.7f * hop;
    tfd_line(bx, bt, bx, ey + 2 * u, 3 * u, ic_g(96, 96, 128), 256);
    bool blink = ph > 0.78f && ph < 0.86f;
    for (int k = -1; k <= 1; k += 2) {
        float ex = bx + k * 9 * u;
        tfd_disc(ex, ey, 9 * u, ic_g(64, 64, 96), 256);
        tfd_disc(ex, ey, 7.6f * u, ic_g(224, 240, 240), 256);
        if (blink) {
            tfd_rect((int)(ex - 7 * u), (int)(ey - 1 * u), (int)(14 * u), (int)(2.4f * u), ic_g(32, 32, 64), 256);
            continue;
        }
        float ix = ex + 2.2f * u * look, iy = ey - 2.4f * u * look;
        tfd_disc(ix, iy, 4.6f * u, ic_g(16, 32, 64), 256);
        tfd_disc(ix, iy, 2.6f * u, ic_g(96, 224, 240), 256);
        tfd_disc(ix - 1.4f * u, iy - 1.6f * u, 1.3f * u, ic_g(224, 240, 240), 256);
    }
    ic_end();
}
static bool cb_awake(void)
{
    return s_gs == GS_PLAY || s_net != N2_OFF;
}

const app_t app_cubby = {
        .name = "CUBBY",
        .landscape = true,
        .round = APP_ROUND_HI,  // (a round panel: the 3D in the whole circle, the HUD along it)
        .enter = cb_enter,
        .leave = cb_leave,
        .input = cb_input,
        .update = cb_update,
        .render = cb_render,
        .draw_icon = cb_icon,
        .awake = cb_awake,
};
