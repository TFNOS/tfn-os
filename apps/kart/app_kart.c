// INK KARTS (GAMES): a kart race in 3D with its own core rule, THE ROAD REMEMBERS (a kart racing game for the round
// AMOLED-1.75C). Each kart lays a glowing ribbon of ink in its colour on the road.
// Your own ink of the lap before = a BOOST; the ink of a rival = SLICK (less grip, less speed); the newest ink wins.
// So the line that you drive now is your fast lane on the next lap. The BLOT (the one button): a wide splash of your
// ink when your meter is full (it fills on your own ink).
// - 6 karts (the person + 5 computer racers), 3 laps, 3 tracks (MEADOW RING, SUNSET DUNES, NEON BAY), 3 levels.
// - Steer: the tilt (turn the board like a wheel; the position at the start = zero) or a finger on the left / right
//   half of the screen; the Watcher: its wheel or the touch. The kart drives by itself.
// - The look: the CUBBY engine (kt_r3d.inc, kt_mesh.inc, kt_hud.inc: smooth 3D in tiles on both cores), the track and
//   its world (kt_track.inc), the karts and the rule (kt_kart.inc), the race, the camera, the HUD (kt_game.inc).
// - Layouts: the round frame (412 x 412: the 1.75C and the Watcher; the main layout), the 3.49 bar (640 x 172), the
//   320 x 240 frame (CoreS3, AMOLED-1.8), the Tab5.
// - Tests: the PC tests make the pictures of each screen, and a bot drives whole races by the real touch.
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
#ifndef APP_ROUND_HI
#define APP_ROUND_HI 2  // (an older main has app_t.round as a bool)
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
#include "tfsfx/tfsfx.h"
#include "kt_sfx.h"
#include "kt_music.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

static const char *TAG = "kart";
#define KT_KEY "kart_"  // (the manifest: saves=kart_)

// INK KARTS: the start of a race (a short fanfare)
static const tune_note_t KT_OPEN[] = {
        JN(0, 110, J_E5, W_SAW, 26),
        JN(120, 110, J_G5, W_SAW, 26),
        JN(240, 110, J_B5, W_SAW, 26),
        JN(360, 320, J_E6, JHOLD(W_SAW), 28),
        JN(0, 200, J_E3, W_SQUARE, 26),
        JN(240, 200, J_B3, W_SQUARE, 26),
        JN(360, 340, J_E4, JHOLD(W_SQUARE), 24),
};

// the time of the stages (us, summed): core 0 and core 1
static int64_t s_pf[2][16];
#define PF_T() esp_timer_get_time()

#include "kt_r3d.inc"
#include "kt_mesh.inc"
#include "kt_hud.inc"
static float fatan2(float y, float x)
{
    float ax = absf(x), ay = absf(y);
    if (ax < 1e-9f && ay < 1e-9f) {
        return 0;
    }
    float a = ax > ay ? ay / ax : ax / ay, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0) {
        r = PI_F - r;
    }
    if (y < 0) {
        r = -r;
    }
    return r;
}
#include "kt_track.inc"
#include "kt_kart.inc"
#include "kt_game.inc"

static bool s_ok;
static int64_t s_last;

// ---------------------------------------------------------------- the music (kt_music.c, libs/midi_bg)
static bool s_music = true;
static int s_mus_cur = -1;
static void music_to(int track)
{
    if (!s_music) {
        return;
    }
    if (track < 0) {
        if (s_mus_cur >= 0) {
            midi_bg_stop(400);
        }
        s_mus_cur = -1;
        return;
    }
    if (s_mus_cur == track) {
        return;
    }
    if (s_mus_cur < 0) {
        midi_bg_start();
    }
    midi_bg_play_track(track, 0, true);
    s_mus_cur = track;
}

// ---------------------------------------------------------------- the sky, the skyline and the ground
static int8_t SINT[256];
static void key_colour(const float (*K)[4], int n, float v, float *o)
{
    if (v <= K[0][0]) {
        o[0] = K[0][1];
        o[1] = K[0][2];
        o[2] = K[0][3];
        return;
    }
    for (int i = 0; i + 1 < n; i++) {
        if (v < K[i + 1][0]) {
            float t = (v - K[i][0]) / (K[i + 1][0] - K[i][0]);
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
// (kt_mesh.inc needs it for the shallow water of CUBBY; the tracks have none)
static void sea_colour(float dist, float *o)
{
    key_colour(TH->gnd, 3, dist, o);
}
// the height of the skyline (radians over the horizon) at the azimuth a: the far layer (lay 0) and the near one (1)
static float sky_h(float a, int lay)
{
    const int sk = TH->skyline;
    if (sk == 0) {
        if (!lay) {
            return 0.05f + 0.022f * fsin(a * 3 + 1.1f) + 0.014f * fsin(a * 7 + 0.4f) + 0.006f * fsin(a * 17);
        }
        return 0.018f + 0.012f * fsin(a * 5 + 2.0f) + 0.006f * fsin(a * 11 + 0.7f);
    }
    if (sk == 1) {
        if (!lay) {
            float m = fsin(a * 4 + 0.6f) * 3.2f;
            return 0.03f + 0.028f * clampf(m, -1, 1) + 0.004f * fsin(a * 23);
        }
        return 0.012f + 0.01f * fsin(a * 6 + 1.3f) * fsin(a * 2.3f);
    }
    // the city: blocks of towers (the far layer), low dark blocks (the near one)
    float q = a * (lay ? 22.0f : 40.0f) / TAU_F;
    int id = ifloor(q);
    uint32_t h = (uint32_t)(id * 2654435761u) ^ (lay ? 0x5bd1e995u : 0);
    h ^= h >> 15;
    float r = (h & 1023) / 1023.0f;
    return lay ? 0.006f + 0.016f * r : 0.018f + 0.07f * r * r;
}
// the data of each row for the background (made once a frame, before the tiles)
typedef struct {
    uint16_t pat[8], fpat[8], npat[8];
    uint32_t p1, dp1, p2, dp2;
    int16_t ai;
    uint8_t gnd, win;
} row_t;
#define GND_LV 8
typedef struct {
    float x, y, r;
    int cr, cg, cb, a;
} sglow_t;
#define NSKY 40
typedef struct {
    row_t row[VH_MAX];
    uint16_t gtab[VH_MAX][GND_LV * 4];
    int16_t tf[VW_MAX], tn[VW_MAX];  // the top row of the far and the near skyline in each column
    int16_t tmin[TCOLS_MAX];         // the highest skyline row in each column of tiles (over it: only sky)
    uint8_t wc[VW_MAX];              // the city: a column with windows
    sglow_t sky[NSKY];
    int nsky;
} bgset_t;
static bgset_t s_bgs[2];
static bgset_t *s_bg = &s_bgs[0], *s_bgd = &s_bgs[0];
static void sky_glow(float x, float y, float r, int cr, int cg, int cb, int a)
{
    if (s_bg->nsky >= NSKY || x + r / KX < 0 || x - r / KX > VW || y + r < 0 || y - r > VH) {
        return;
    }
    s_bg->sky[s_bg->nsky++] = (sglow_t){x, y, r, cr, cg, cb, a};
}
static inline uint32_t wave_fix(float ph)
{
    ph -= 256.0f * (float)ifloor(ph * (1.0f / 256.0f));
    return (uint32_t)(ph * 65536.0f);
}
static void pat8(uint16_t *pat, int y, float r, float g, float b)
{
    const uint8_t *dt = DITH[y & 7];
    uint32_t R = (uint32_t)(clampf(r, 0, 255) * (247.0f / 255.0f * 256)),
             G = (uint32_t)(clampf(g, 0, 255) * (251.0f / 255.0f * 256)),
             B = (uint32_t)(clampf(b, 0, 255) * (247.0f / 255.0f * 256));
    for (int k = 0; k < 8; k++) {
        uint32_t q = dt[k];
        pat[k] = (uint16_t)(((R + (q << 3)) >> 11) << 11 | ((G + (q << 2)) >> 10) << 5 | ((B + (q << 3)) >> 11));
    }
}
typedef struct {
    float az, el, sz;
} cloud_t;
static const cloud_t CLOUDS[] = {
        {0.3f, 0.12f, 0.05f}, {1.2f, 0.17f, 0.065f}, {2.1f, 0.10f, 0.045f}, {2.9f, 0.15f, 0.06f},
        {3.8f, 0.11f, 0.05f}, {4.6f, 0.19f, 0.07f},  {5.5f, 0.13f, 0.055f},
};
static void rows_prepare(void)
{
    float ifl = frcp(C.f), iflx = frcp(C.fx);
    // the horizon row (no roll: one row for the whole width)
    float yh = C.cy + C.f * C.F.y * frcp(C.U.y > 0.2f ? C.U.y : 0.2f);
    float far0[3], far1[3], near0[3], near1[3], hz[3] = {LK.haze[0], LK.haze[1], LK.haze[2]};
    float c[3];
    col3(c, TH->far0);
    far0[0] = c[0] * 255;
    far0[1] = c[1] * 255;
    far0[2] = c[2] * 255;
    col3(c, TH->far1);
    far1[0] = c[0] * 255;
    far1[1] = c[1] * 255;
    far1[2] = c[2] * 255;
    col3(c, TH->near0);
    near0[0] = c[0] * 255;
    near0[1] = c[1] * 255;
    near0[2] = c[2] * 255;
    col3(c, TH->near1);
    near1[0] = c[0] * 255;
    near1[1] = c[1] * 255;
    near1[2] = c[2] * 255;
    // the skyline: the top row of each layer in each pair of columns (by the azimuth of the column)
    int ytop = VH;
    for (int x = 0; x < VW; x += 2) {
        float u = (x + 1.0f - C.cx) * iflx;
        v3 d = V3(C.F.x + C.R.x * u, 0, C.F.z + C.R.z * u);
        float az = fatan2(d.x, d.z);
        int16_t f = (int16_t)clampi((int)(yh - C.f * sky_h(az, 0)), -1, VH),
                nn = (int16_t)clampi((int)(yh - C.f * sky_h(az, 1)), -1, VH);
        int col = ifloor(az * 160.0f / TAU_F);
        uint8_t w = (uint8_t)(TH->skyline == 2 && ((col * 7 + (col >> 2)) % 3 == 0));
        for (int q = x; q < x + 2 && q < VW; q++) {
            s_bg->tf[q] = f;
            s_bg->tn[q] = nn;
            s_bg->wc[q] = w;
        }
        if (f < ytop) {
            ytop = f;
        }
        if (nn < ytop) {
            ytop = nn;
        }
    }
    for (int c2 = 0; c2 < TCOLS; c2++) {
        int m = VH;
        for (int x = c2 * TLW; x < (c2 + 1) * TLW && x < VW; x++) {
            m = s_bg->tf[x] < m ? s_bg->tf[x] : m;
        }
        for (int x = c2 * TLW; x < (c2 + 1) * TLW && x < VW; x++) {
            m = s_bg->tn[x] < m ? s_bg->tn[x] : m;
        }
        s_bg->tmin[c2] = (int16_t)m;
    }
    for (int y = 0; y < VH; y++) {
        row_t *R_ = &s_bg->row[y];
        float vy = (C.cy - (y + 0.5f)) * ifl;
        v3 d = V3(C.F.x + C.U.x * vy, C.F.y + C.U.y * vy, C.F.z + C.U.z * vy);
        float e = d.y * frsqrt(vdot(d, d));
        if (e > -0.002f) {
            float o[3];
            key_colour(TH->sky, 5, e, o);
            pat8(R_->pat, y, o[0], o[1], o[2]);
            R_->gnd = 0;
            R_->win = 0;
            if (y < ytop) {
                continue;  // (no skyline in this row)
            }
            // (the skyline: hazy at the horizon, its own colour higher)
            float hf = clampf((yh - y) / (C.f * 0.08f), 0, 1), hn = clampf((yh - y) / (C.f * 0.03f), 0, 1);
            float f0 = 0.55f * (1 - hf), n0 = 0.3f * (1 - hn);
            pat8(R_->fpat, y, lerpf(lerpf(far1[0], far0[0], hf), hz[0], f0),
                 lerpf(lerpf(far1[1], far0[1], hf), hz[1], f0), lerpf(lerpf(far1[2], far0[2], hf), hz[2], f0));
            pat8(R_->npat, y, lerpf(lerpf(near1[0], near0[0], hn), hz[0], n0),
                 lerpf(lerpf(near1[1], near0[1], hn), hz[1], n0), lerpf(lerpf(near1[2], near0[2], hn), hz[2], n0));
            R_->win = (uint8_t)(TH->skyline == 2 && ((int)(yh - y) % 4) < 2 && yh - y > 2);
            continue;
        }
        // the ground: the world point of each pixel of the row (a line), a mottle of two waves, the haze far away
        float t = (0 - C.eye.y) * frcp(d.y);
        float hd = t * fsqrt(d.x * d.x + d.z * d.z);
        float base[3];
        key_colour(TH->gnd, 3, hd, base);
        float hk = clampf((hd - LK.haze0) * LK.haze_d, 0, LK.haze_max);
        for (int k = 0; k < 3; k++) {
            base[k] += (hz[k] - base[k]) * hk;
        }
        float u0 = (0.5f - C.cx) * iflx;
        float wx = C.eye.x + t * (d.x + C.R.x * u0), wz = C.eye.z + t * (d.z + C.R.z * u0);
        float dwx = t * C.R.x * iflx, dwz = t * C.R.z * iflx;
        // (the phases in steps of the table, 256 = a full wave: 3.1 m and 7.4 m)
        R_->p1 = wave_fix((wx * 0.8f + wz * 0.6f) * 82.0f);
        R_->dp1 = wave_fix((dwx * 0.8f + dwz * 0.6f) * 82.0f);
        R_->p2 = wave_fix((wz * 0.9f - wx * 0.45f) * 34.6f);
        R_->dp2 = wave_fix((dwz * 0.9f - dwx * 0.45f) * 34.6f);
        R_->ai = (int16_t)(clampf(1 - hd * (1.0f / 48.0f), 0, 1) * 22 * TH->mott);
        pat8(R_->pat, y, base[0], base[1], base[2]);
        R_->gnd = 1;
        if (R_->ai > 0) {
            // (each level: the colour with 4 steps of the dither of the row, in whole numbers x 256)
            uint16_t *tab = s_bg->gtab[y];
            const uint8_t *dt = DITH[y & 7];
            int br = (int)(base[0] * (247.0f / 255.0f * 256)), bgg = (int)(base[1] * (251.0f / 255.0f * 256)),
                bb = (int)(base[2] * (247.0f / 255.0f * 256));
            int wst = (int)(R_->ai * 0.9f * 256 * 2 / (GND_LV - 1));
            for (int L = 0; L < GND_LV; L++) {
                int w = (2 * L - (GND_LV - 1)) * wst / 2;
                int r = clampi(br + w * 9 / 10, 0, 63000), g = clampi(bgg + w * 11 / 10, 0, 64000),
                    b = clampi(bb + w * 7 / 10, 0, 63000);
                for (int k = 0; k < 4; k++) {
                    uint32_t q = dt[k];
                    tab[L * 4 + k] =
                            (uint16_t)((((uint32_t)r + (q << 3)) >> 11) << 11 | (((uint32_t)g + (q << 2)) >> 10) << 5 |
                                       (((uint32_t)b + (q << 3)) >> 11));
                }
            }
        }
    }
    // the sun, the clouds (day and sunset) and the stars (the bay)
    s_bg->nsky = 0;
    v3 cs = V3(vdot(C.R, L_SUN), vdot(C.U, L_SUN), vdot(C.F, L_SUN));
    if (cs.z > 0.05f && TH->sun_glow) {
        float iz = frcp(cs.z);
        float sx = C.cx + C.fx * cs.x * iz, sy = C.cy - C.f * cs.y * iz;
        sky_glow(sx, sy, C.f * 0.26f, 255, 190, 130, TH->sun_glow / 2);
        sky_glow(sx, sy, C.f * 0.11f, 255, 236, 190, TH->sun_glow);
        sky_glow(sx, sy, C.f * 0.05f, 255, 255, 240, 256);
    }
    int ncl = TH->skyline == 2 ? 0 : (int)(sizeof CLOUDS / sizeof CLOUDS[0]);
    for (int k = 0; k < ncl; k++) {
        const cloud_t *cl = &CLOUDS[k];
        for (int p = 0; p < 2; p++) {
            float az = cl->az + (p - 0.5f) * cl->sz * 1.1f, el = cl->el + ((p & 1) ? cl->sz * 0.3f : 0);
            v3 dd = V3(fsin(az) * fcos(el), fsin(el), fcos(az) * fcos(el));
            v3 q = V3(vdot(C.R, dd), vdot(C.U, dd), vdot(C.F, dd));
            if (q.z < 0.05f) {
                continue;
            }
            float iz = frcp(q.z);
            float sx = C.cx + C.fx * q.x * iz, sy = C.cy - C.f * q.y * iz, r = C.f * cl->sz * 1.25f * iz;
            sky_glow(sx, sy, r, 255, 250, 246, TH->skyline == 1 ? 90 : 140);
        }
    }
    if (TH->skyline == 2) {
        for (int k = 0; k < 24; k++) {
            float az = k * 0.2618f + 0.13f * (k % 5), el = 0.1f + 0.05f * (k * 7 % 9);
            v3 dd = V3(fsin(az) * fcos(el), fsin(el), fcos(az) * fcos(el));
            v3 q = V3(vdot(C.R, dd), vdot(C.U, dd), vdot(C.F, dd));
            if (q.z < 0.05f) {
                continue;
            }
            float iz = frcp(q.z);
            sky_glow(C.cx + C.fx * q.x * iz, C.cy - C.f * q.y * iz, 1.6f + (k % 3) * 0.6f, 255, 240, 255, 200);
        }
    }
}
// the background of a tile: only the pixels that the solids did not cover (z = 0)
static void bg_tile(tile_t *t)
{
    int w = t->x1 - t->x0;
    const bgset_t *B = s_bgd;
    for (int y = t->y0; y < t->y1; y++) {
        const row_t *R_ = &B->row[y];
        uint32_t *row = t->p + (y - t->y0) * TLW;
        if (!R_->gnd && y < B->tmin[t->x0 / TLW]) {
            const uint16_t *pt = R_->pat;
            for (int x = 0; x < w; x++) {
                if (!row[x]) {
                    row[x] = pt[(t->x0 + x) & 7];
                }
            }
            continue;
        }
        if (!R_->gnd) {
            for (int x = 0; x < w; x++) {
                if (row[x]) {
                    continue;
                }
                int gx = t->x0 + x;
                if (y >= B->tn[gx]) {
                    row[x] = R_->npat[gx & 7];
                } else if (y >= B->tf[gx]) {
                    row[x] = (R_->win && B->wc[gx] && ((gx + y) & 1)) ? 0xFEE0 : R_->fpat[gx & 7];
                } else {
                    row[x] = R_->pat[gx & 7];
                }
            }
            continue;
        }
        if (R_->ai == 0) {
            for (int x = 0; x < w; x++) {
                if (!row[x]) {
                    row[x] = R_->pat[(t->x0 + x) & 7];
                }
            }
            continue;
        }
        // (the near ground: one wave value for each pair of px, its colour from the table of the row)
        uint32_t p1 = R_->p1 + R_->dp1 * (uint32_t)t->x0, p2 = R_->p2 + R_->dp2 * (uint32_t)t->x0;
        uint32_t dp1 = R_->dp1 * 2, dp2 = R_->dp2 * 2;
        const uint16_t *tab = B->gtab[y];
        for (int x = t->x0; x < t->x1; x += 2, p1 += dp1, p2 += dp2) {
            uint32_t *w2 = row + (x - t->x0);
            if (w2[0] && w2[1]) {
                continue;
            }
            int L = ((SINT[(p1 >> 16) & 255] + SINT[(p2 >> 16) & 255] + 256) * GND_LV) >> 9;
            if (L >= GND_LV) {
                L = GND_LV - 1;
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
        glow_add(t, g->x, g->y, g->r, -1, g->cr, g->cg, g->cb, g->a);
    }
}

// ---------------------------------------------------------------- the list of a frame
static int s_order[WMAXC], s_norder;
static int s_isl_split;
static int64_t s_isl_t1;
static void world_kick(void);
// the start of a list: the lanes, the glows, the order of the chunks near to far
static void list_begin(void)
{
    s_glb->n = 0;
    s_lframe++;
    lanes_reset();
    int n = 0;
    static float zc[WMAXC];
    for (int k = 0; k < s_nch; k++) {
        const chunk_t *ch = &s_ch[k];
        if (ch->lod != LOD_ALL) {
            float dx = ch->anc.x - C.eye.x, dy = ch->anc.y - C.eye.y, dz = ch->anc.z - C.eye.z;
            float ld = ch->lodd > 0 ? ch->lodd : LOD_D;
            bool near = dx * dx + dy * dy + dz * dz < ld * ld;
            if (near != (ch->lod == LOD_FINE)) {
                continue;
            }
        }
        v3 cc = xf_pt(&C.w2c, ch->c);
        if (!cam_sees(cc, ch->rad)) {
            continue;
        }
        if (ch->lod == LOD_COARSE && ch->lodd == 0 && cc.z > 88.0f) {
            continue;
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
// the list of this core (lane 0): the karts (near first), the ink, the effects, the near chunks
static void list_build(float alpha, bool kick)
{
    if (kick) {
        world_kick();
    }
    s_ntri_drawn = 0;
    int64_t pa = PF_T();
    int ord[NKART], n = 0;
    float dz[NKART];
    for (int i = 0; i < NKART; i++) {
        v3 c = xf_pt(&C.w2c, V3(K[i].x, 0.5f, K[i].z));
        int j = n++;
        while (j > 0 && dz[j - 1] > c.z) {
            dz[j] = dz[j - 1];
            ord[j] = ord[j - 1];
            j--;
        }
        dz[j] = c.z;
        ord[j] = i;
    }
    for (int i = 0; i < n; i++) {
        kart_draw(ord[i], alpha);
    }
    s_pf[0][8] += PF_T() - pa;
    pa = PF_T();
    ink_draw();
    effects_draw();
    s_pf[0][9] += PF_T() - pa;
    if (s_isl_split > s_norder) {
        s_isl_split = s_norder;
    }
    for (int k = 0; k < s_isl_split; k++) {
        dl_chunk(&s_lane[0], &s_ch[s_order[k]]);
    }
}
// the far chunks (lane 1) and the rows of the background: the work of core 1
static void world_list(void)
{
    int64_t pa = PF_T();
    for (int k = s_isl_split; k < s_norder; k++) {
        dl_chunk(&s_lane[1], &s_ch[s_order[k]]);
    }
    int64_t pr = PF_T();
    rows_prepare();
    s_pf[1][12] += PF_T() - pr;
    s_isl_t1 = PF_T() - pa;
    s_pf[1][10] += s_isl_t1;
}

// ---------------------------------------------------------------- the tiles of the two cores
static uint32_t *s_tbuf[2];
static void *s_tbuf_mem[2];
static bool s_tint_ram;
// THE ROUND FRAME: a tile out of the circle is not drawn (the panel shows nothing out of the circle; the fuzz checks
// only the circle of a round frame); the span of each output row in the circle (with 1 px more at each side)
static uint8_t s_tile_out[NTILE_MAX];
static void tiles_mark(void)
{
#if KT_LAY == 2
    // (the same whole-number test as gfx_in_circle(), with a radius 2 px bigger: dx^2 + dy^2 <= (r + 2)^2)
    for (int y = 0; y < OH && y < RMAX; y++) {
        int r = OW / 2 + 2, dy = y - OH / 2, q = r * r - dy * dy, h = 0;
        if (q < 0) {
            s_sx0[y] = s_sx1[y] = 0;
            continue;
        }
        while ((h + 1) * (h + 1) <= q) {
            h++;
        }
        s_sx0[y] = (int16_t)clampi(OW / 2 - h, 0, OW);
        s_sx1[y] = (int16_t)clampi(OW / 2 + h + 1, 0, OW);
    }
#endif
    for (int k = 0; k < NTILE; k++) {
        s_tile_out[k] = 0;
#if KT_LAY == 2
        int x0 = (k % TCOLS) * TLW * KX, y0 = (k / TCOLS) * TLH * KY;
        int x1 = x0 + TLW * KX, y1 = y0 + TLH * KY;
        float cx = OW * 0.5f, cy = OH * 0.5f;
        float nx = clampf(cx, (float)x0, (float)x1), ny = clampf(cy, (float)y0, (float)y1);
        float dx = nx - cx, dy = ny - cy;
        s_tile_out[k] = dx * dx + dy * dy > (OW * 0.5f + 1) * (OW * 0.5f + 1);
#endif
    }
}
static void tile_render(int k, int core)
{
    if (s_tile_out[k]) {
        return;
    }
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
static int s_share = 8;
static inline bool tile_core0(int k)
{
    return ((k * 7) & 15) < s_share;
}
static void tiles_of(int core)
{
    for (int k = 0; k < NTILE; k++) {
        if (tile_core0(k) == (core == 0)) {
            tile_render(k, core);
        }
    }
}
static int64_t s_t1_us, s_tb_us, s_ts_us, s_tl_us, s_c0_us, s_c1_us;
static int s_setb, s_setd;
static bool s_have_d;
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
static volatile int s_wk_job;  // 0 the tiles of core 1, 1 the far list, 2 both (the pipeline)
static int s_wk_frames;
static volatile bool s_l0_done;
static int64_t s_bins_us;
static volatile int64_t s_t1_end;  // (the time when core 1 ended its job)
static volatile bool s_c1_done;    // (core 1 ended the job of the pipeline: core 0 spins on it)
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
            tiles_of(1);
            world_list();
            while (!s_l0_done) {
                MEMW();
            }
            __sync_synchronize();
            int64_t tb0 = esp_timer_get_time();
            bins_build();
            s_t1_us = esp_timer_get_time() - t0;
            s_t1_end = esp_timer_get_time();
            s_bins_us = esp_timer_get_time() - tb0;
            __sync_synchronize();
            s_c1_done = true;
            if (++s_wk_frames % 20 == 0) {
                vTaskDelay(1);
            }
        } else {
            tiles_of(1);
            s_t1_us = esp_timer_get_time() - t0;
            if (++s_wk_frames % 20 == 0) {
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
        xTaskCreatePinnedToCoreWithCaps(worker, "kart", WORKER_STACK, NULL, 3, &s_wk_task, 1,
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
        core1_wait("the world");
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
        // THE PIPELINE (as CUBBY): core 1 draws its tiles of the frame before, then the far list and the rows of this
        // frame; this core makes the karts and the ink of this frame (and the nearest chunks: s_isl_split follows the
        // wait of this core for core 1), then draws its tiles of the frame before
        if (s_isl_split > s_norder) {
            s_isl_split = s_norder;
        }
        s_l0_done = false;
        s_c1_done = false;
        s_wk_job = 2;
        xSemaphoreGive(s_wk_go);
        list_build(alpha, false);
        __sync_synchronize();
        s_l0_done = true;
        int64_t tl = esp_timer_get_time();
        tiles_of(0);
        int64_t t_0 = esp_timer_get_time() - t0;
        // (a spin of at most 12 ms on the flag of core 1, then the semaphore; the semaphore is taken in both cases)
        {
            int64_t ts = esp_timer_get_time();
            while (!s_c1_done && esp_timer_get_time() - ts < 12000) {
                MEMW();
            }
        }
        core1_wait("the frame");
        int64_t t1 = esp_timer_get_time();
        s_pf[0][11] += s_bins_us;
        s_pf[0][10] += t1 - t0 - t_0;
        // (the balance: by the end of each core from the start of the frame; core 1 can start late)
        int64_t e1 = s_t1_end - t0;
        if (t_0 + 1200 < e1 && s_share < 15) {
            s_share++;
        } else if (e1 + 1200 < t_0 && s_share > 1) {
            s_share--;
        }
        // (the chunks: when core 1 ends later than this core, this core takes more of them in the next frame)
        if (t_0 + 1500 < e1 && s_isl_split < s_norder) {
            s_isl_split++;
        } else if (e1 + 1500 < t_0 && s_isl_split > 0) {
            s_isl_split--;
        }
        s_c0_us += t_0;
        s_c1_us += s_t1_us;
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
    s_pf[0][10] += tw - tb;
    if (tw - tb > 400 && s_isl_split < s_norder) {
        s_isl_split++;
    } else if (tw - tb < 50 && s_isl_t1 + 1500 < tb - t0 && s_isl_split > 0) {
        s_isl_split--;
    }
    bins_build();
    int64_t t1 = esp_timer_get_time();
    s_pf[0][11] += t1 - tw;
    s_tl_us += t1 - t0;
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
        s_ts_us += t_0 > s_t1_us ? t_0 - s_t1_us : s_t1_us - t_0;
        return;
    }
#endif
    tiles_of(0);
    tiles_of(1);
    s_tb_us += esp_timer_get_time() - t1;
    sets_done(false);
}

// ---------------------------------------------------------------- the panel
enum {
    MI_TILT,
    MI_LEVEL,
    MI_LANG,
    MI_SOUND,
    MI_MUSIC,
    MI_RESTART,
    MI_TITLE
};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const LEVEL_V[] = {"EASY", "קל", "NORMAL", "רגיל", "HARD", "קשה"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_TILT, 2, 0, {"TILT", "הטיה"}, ONOFF_V, KT_KEY "tilt"},
        {PI_PICK, MI_LEVEL, 3, 1, {"LEVEL", "רמה"}, LEVEL_V, KT_KEY "level"},
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_V, KT_KEY "lang"},
        {PI_PICK, MI_SOUND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, KT_KEY "snd"},
        {PI_PICK, MI_MUSIC, 2, 0, {"MUSIC", "מוזיקה"}, ONOFF_V, KT_KEY "mus"},
        {PI_ACT2, MI_RESTART, 0, 0, {"RESTART", "מחדש"}, NULL, NULL},
        {PI_ACT, MI_TITLE, 0, 0, {"THE TITLE", "למסך הפתיחה"}, NULL, NULL},
};
static void to_title(void)
{
    s_gs = GS_TITLE;
    s_gt = 0;
    race_setup();
    music_to(KTM_TITLE);
}
static bool item_shown(int id)
{
    return (id == MI_RESTART || id == MI_TITLE) ? s_gs != GS_TITLE : true;
}
static void item_set(int id, int v)
{
    if (id == MI_TILT) {
        s_tilt_on = !(v & 1);
    } else if (id == MI_LEVEL) {
        s_level = clampi(v, 0, 2);
    } else if (id == MI_LANG) {
        s_he = v == 1;
    } else if (id == MI_SOUND) {
        s_sound = !(v & 1);
    } else if (id == MI_MUSIC) {
        bool on = !(v & 1);
        if (on == s_music) {
            return;
        }
        if (!on) {
            music_to(-1);
            s_music = false;
        } else {
            s_music = true;
            s_mus_cur = -1;
            music_to(s_gs == GS_RACE ? KTM_RACE : KTM_TITLE);
        }
    } else if (id == MI_RESTART) {
        race_start();
    } else if (id == MI_TITLE) {
        to_title();
    }
}
static void item_opened(void)
{
    audio_drone(0, 0, 0, W_SAW);
    audio_drone(1, 0, 0, W_TRI);
}
static void item_resume(void)
{
    imu_set_level();
    s_last = 0;
}
static const pitems_t PANEL = {ITEMS, 7, item_shown, NULL, item_set, item_opened, item_resume};

// ---------------------------------------------------------------- the app
static void *kt_alloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) {
        memset(p, 0, n);
    }
    return p;
}
static void kt_free_all(void)
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
    heap_caps_free(s_tr);
    s_tr = NULL;
    heap_caps_free(s_ink);
    s_ink = NULL;
    s_track = -1;
}
static int s_fps_n;
static int64_t s_fps_t0;
static void kt_enter(int64_t now)
{
    s_ok = false;
#if KT_LAY == 2
    // (the round frame now: 412, or 466 at the panel resolution; even, at most RMAX)
    s_ow = clampi(gfx_w < gfx_h ? gfx_w : gfx_h, 240, RMAX) & ~1;
    s_k = s_ow >= 440 ? 3 : 2;
#endif
    for (int k = 0; k < 2; k++) {
        s_binm[k] = kt_alloc(sizeof(uint16_t) * BINMAX);
        s_pvm[k] = kt_alloc(sizeof(pv_t) * PMAXV);
        s_dtm[k] = kt_alloc(sizeof(dt_t) * DMAXT);
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
            s_tbuf_mem[k] = kt_alloc(TLW * TLH * 4 + 32);
        }
        if (s_tbuf_mem[k]) {
            s_tbuf[k] = (uint32_t *)(((uintptr_t)s_tbuf_mem[k] + 31) & ~(uintptr_t)31);
        }
    }
    s_bv = kt_alloc(sizeof(bv_t) * BMAXV);
    s_bt = kt_alloc(sizeof(tri_t) * BMAXT);
    s_wv = kt_alloc(sizeof(wv_t) * WMAXV);
    s_wt = kt_alloc(sizeof(tri_t) * WMAXT);
    s_mv = kt_alloc(sizeof(mv_t) * MMAXV);
    s_mt = kt_alloc(sizeof(tri_t) * MMAXT);
    s_tr = kt_alloc(sizeof(trow_t) * TMAX);
    s_ink = kt_alloc(sizeof(ink_t) * TMAX * NKART);
    if (!s_binm[0] || !s_binm[1] || !s_pvm[0] || !s_pvm[1] || !s_dtm[0] || !s_dtm[1] || !s_tbuf[0] || !s_tbuf[1] ||
        !s_bv || !s_bt || !s_wv || !s_wt || !s_mv || !s_mt || !s_tr || !s_ink) {
        ESP_LOGE(TAG, "no memory");
        kt_free_all();
        return;
    }
    int64_t t0 = esp_timer_get_time();
    (void)t0;
    tone_init();
    s_dq_mem = heap_caps_malloc(sizeof(dq_t) * 8, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    DQ = s_dq_mem ? (dq_t *)s_dq_mem : DQ_PS;
    dq_init();
    for (int i = 0; i < 256; i++) {
        SINT[i] = (int8_t)(fsin(i * TAU_F / 256) * 127);
    }
    tiles_mark();
    s_tilt_on = !store_get(KT_KEY "tilt", 0);
    s_level = clampi((int)store_get(KT_KEY "level", 1), 0, 2);
    s_he = store_get(KT_KEY "lang", 0) == 1;
    s_sound = !store_get(KT_KEY "snd", 0);
    s_music = !store_get(KT_KEY "mus", 0);
    s_sel = clampi((int)store_get(KT_KEY "racer", 0), 0, NKART - 1);
    s_demo = store_get(KT_KEY "demo", 0) != 0;
    if (s_demo) {
        store_set(KT_KEY "demo", 0);
    }
    s_tsel = clampi((int)store_get(KT_KEY "track", 0), 0, NTRACK - 1);
    track_build(s_tsel);
    ESP_LOGI(TAG,
             "built in %d ms: world %d corners %d triangles %d chunks, models %d corners %d triangles, track %d rows",
             (int)((esp_timer_get_time() - t0) / 1000), s_wnv, s_wnt, s_nch, s_mnv, s_mnt, s_ntr);
    midi_bg_init(KT_TRACKS, KT_TRACK_COUNT, KT_MUSIC_VOICES);
    midi_bg_volume(55);
    s_mus_cur = -1;
    static const char *const *names = FX_NAMES;
    tfsfx_load_mem(KT_SFX_DATA, KT_SFX_LEN, names, FX_N < KT_SFX_N ? FX_N : KT_SFX_N, 47);
    sysui_panel_items(&PANEL);
    s_rt = 0;
    s_t = 0;
    s_last = now;
    s_fps_n = 0;
    s_fps_t0 = 0;
    s_tl_us = s_tb_us = s_ts_us = 0;
    to_title();
    worker_start();
    imu_set_level();
    if (s_sound) {
        JPLAY(KT_OPEN);
    }
    ESP_LOGI(TAG, "tiles in %s", s_tint_ram ? "internal RAM" : "PSRAM");
    s_ok = true;
}
static void kt_leave(void)
{
    worker_stop();
    audio_drone(0, 0, 0, W_SAW);
    audio_drone(1, 0, 0, W_TRI);
    midi_bg_end();
    tfsfx_free();
    if (s_ok) {
        store_set(KT_KEY "racer", (uint32_t)s_sel);
        store_set(KT_KEY "track", (uint32_t)s_tsel);
    }
    kt_free_all();
    s_ok = false;
}
static void kt_input_ev(const input_event_t *ev)
{
    if (!s_ok) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (s_gs == GS_TITLE) {
        if (ev->kind != IN_TAP) {
            return;
        }
        if (in_disc(x, y, T_AL_X, T_A_Y, T_AR_R) || in_disc(x, y, T_AR_X, T_A_Y, T_AR_R)) {
            s_sel = (s_sel + (x > OW / 2 ? 1 : NKART - 1)) % NKART;
            race_setup();
            fx(FX_PICK, 70, 1.0f);
        } else if (in_rect(x, y, T_TR_X, T_TR_Y, T_W, T_H)) {
            s_tsel = (s_tsel + 1) % NTRACK;
            race_setup();
            fx(FX_PICK, 70, 1.2f);
        } else if (in_rect(x, y, T_ST_X, T_ST_Y, T_W, T_H + 4)) {
            fx(FX_PICK, 80, 0.9f);
            race_start();
        }
        return;
    }
    if (s_gs == GS_RACE && ev->kind == IN_DOWN && in_btn(x, y)) {
        kart_blot(&K[s_me]);
        return;
    }
    if (s_gs == GS_RESULTS && ev->kind == IN_TAP && s_gt > 1.2f) {
        fx(FX_PICK, 70, 1.0f);
        to_title();
    }
}
// a slow tap (a press of 350 - 900 ms) on the title and the results acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void kt_input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    kt_input_ev(ev);
    if (slow && (s_gs == GS_TITLE || s_gs == GS_RESULTS)) {
        kt_input_ev(&tap);
    }
}
static void kt_update(int64_t now)
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
    if (s_music) {
        midi_bg_service();
    }
    int64_t tu = PF_T();
    if (s_gs == GS_TITLE) {
        s_gt += rdt;
        s_t += rdt;
        s_wtime = s_t;
        if (s_demo && s_gt > 2.0f) {
            race_start();
        }
    } else {
        race_update(rdt);
    }
    // (the demo: from the results to the next track)
    if (s_demo && s_gs == GS_RESULTS && s_gt > 4.0f) {
        s_tsel = (s_tsel + 1) % NTRACK;
        to_title();
    }
    camera_step(rdt);
    s_pf[0][13] += PF_T() - tu;
}
static void kt_render(int64_t now)
{
    if (!s_ok) {
        gfx_fill(rgb(20, 30, 60));
        gfx_text_center(OH / 2 - 4, "NO MEMORY", 2, rgb(255, 255, 255));
        return;
    }
    int64_t th = PF_T();
    s_hudb->n = 0;
    if (s_gs == GS_TITLE) {
        title_shapes();
    } else if (s_gs == GS_RESULTS) {
        results_shapes();
    } else {
        race_shapes();
    }
    s_pf[0][14] += PF_T() - th;
    frame_draw();
    th = PF_T();
    if (s_gs == GS_TITLE) {
        title_text();
    } else if (s_gs == GS_RESULTS) {
        results_text();
    } else {
        race_text();
    }
    s_pf[0][14] += PF_T() - th;
    // the speed in the log, each 5 s
    s_fps_n++;
    if (!s_fps_t0) {
        s_fps_t0 = now;
    }
    if (now - s_fps_t0 >= 5000) {
        float sec = (now - s_fps_t0) * 0.001f;
        int n = s_fps_n > 0 ? s_fps_n : 1;
        (void)n;
        ESP_LOGI(TAG,
                 "KART: %.1f fps (state %d), list %.1f ms, tiles %.1f ms, wait %.1f ms, %d triangles, %d corners, %d "
                 "bins, %d lost",
                 s_fps_n / sec, s_gs, s_tl_us / 1000.0f / n, s_tb_us / 1000.0f / n, s_ts_us / 1000.0f / n,
                 s_lane[0].dn + s_lane[1].dn - DT_HALF, s_lane[0].pn + s_lane[1].pn - PV_HALF, s_bin_at[NTILE],
                 s_lane[0].dropped + s_lane[1].dropped + s_bin_lost);
        ESP_LOGI(TAG, "KART cores: share %d/16, near chunks %d of %d, core 0 %.1f ms, core 1 %.1f ms", s_share,
                 s_isl_split, s_norder, s_c0_us / 1000.0f / n, s_c1_us / 1000.0f / n);
        s_c0_us = s_c1_us = 0;
        ESP_LOGI(TAG,
                 "KART parts (ms a frame): karts %.1f, ink+fx %.1f, world on core 1 %.1f (rows %.1f), the wait %.1f, "
                 "bins %.1f, update %.1f, HUD %.1f",
                 s_pf[0][8] / 1000.0f / n, s_pf[0][9] / 1000.0f / n, s_pf[1][10] / 1000.0f / n,
                 s_pf[1][12] / 1000.0f / n, s_pf[0][10] / 1000.0f / n, s_pf[0][11] / 1000.0f / n,
                 s_pf[0][13] / 1000.0f / n, s_pf[0][14] / 1000.0f / n);
        (void)sec;
        {
            // (the cycles of each stage of the tiles in thousands a frame, core 0 + core 1; the counts a frame)
            float kn = 1.0f / (1000.0f * n);
            (void)kn;
#define CYS(i) (int)((s_cy[0][i] + s_cy[1][i]) * kn)
#define CNS(i) (int)((s_cn[0][i] + s_cn[1][i]) / (uint32_t)n)
            ESP_LOGI(TAG,
                     "KART kcycles: clear %d, solid %d, bg %d, dark %d, add %d, glow %d, edge %d, out %d | px %d, "
                     "setups %d, rows %d, glows %d, sky %d",
                     CYS(CY_CLEAR), CYS(CY_SOLID), CYS(CY_BG), CYS(CY_DARK), CYS(CY_ADD), CYS(CY_GLOW), CYS(CY_EDGE),
                     CYS(CY_OUT), CNS(CN_PX), CNS(CN_SETUP), CNS(CN_ROWS), s_gld->n, s_bgd->nsky);
#undef CYS
#undef CNS
        }
        memset(s_pf, 0, sizeof s_pf);
        memset(s_cy, 0, sizeof s_cy);
        memset(s_cn, 0, sizeof s_cn);
        s_fps_n = 0;
        s_fps_t0 = now;
        s_tl_us = s_tb_us = s_ts_us = 0;
    }
}
#include "kt_icon.inc"
static bool kt_awake(void)
{
    return s_gs != GS_TITLE && s_gs != GS_RESULTS;
}

const app_t app_kart = {
        .name = "INK KARTS",
        .landscape = true,
        .enter = kt_enter,
        .leave = kt_leave,
        .input = kt_input,
        .update = kt_update,
        .render = kt_render,
        .draw_icon = kt_icon,
        .awake = kt_awake,
        .round = APP_ROUND_HI,  // the round frame at the panel resolution (466 on the 1.75C, 412 on the Watcher)
};
