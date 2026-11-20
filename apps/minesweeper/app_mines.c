// MINESWEEPER: a garden Minesweeper (a store app for the 3.49). Eight gardens, one for each season, with
// DIGBY the mole. A tap on the board aims (a loupe shows the tiles under the finger); the big DIG and FLAG buttons act
// (or the BOOT key digs). No board needs a guess. Surprises under the tiles: coins, a chest, the mole that shows a safe
// tile, the clover that saves you from one mine, a cat that sleeps on a safe tile. MANUAL.md "MINESWEEPER".
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "ms_music.h"

#include "ms_core.inc"
#include "ms_art.inc"
#include "ms_board.inc"

// ================================================================ the state
enum {
    ST_TITLE,
    ST_PLAY,
    ST_BOOM,
    ST_WIN
};
static int s_state = ST_TITLE, s_g, s_sel, s_unlock;
static float s_state_t, s_wall, s_time;  // the time in the state, the clock of the frames, the time of the game
static bool s_timer;
static int s_coins, s_shield, s_total_coins, s_level_coins;
static int s_best[NGARD], s_star[NGARD], s_new_stars;
static bool s_new_best;
static int s_bx, s_by;  // the top left corner of the board on the screen
static int HUDW = 150;  // the width of the HUD at the left (92 on a narrow frame)
static const char *key_n(const char *p, int i)
{
    static char k[2][10];
    static int w;
    w ^= 1;
    snprintf(k[w], sizeof k[w], "%s%d", p, i);
    return k[w];
}

// ---- the sound
static bool s_sound = true;
static void voice(float d_ms, float len_ms, float f0, float f1, int wave, int vol)
{
    if (!s_sound) {
        return;
    }
    audio_voice_t v = {0};
    v.delay = (uint32_t)(d_ms * AUDIO_SAMPLE_RATE / 1000);
    v.dur = (uint32_t)(len_ms * AUDIO_SAMPLE_RATE / 1000);
    v.f0 = f0;
    v.f1 = f1;
    v.wave = (uint8_t)wave;
    v.vol = (uint8_t)clampi(vol, 1, 100);
    v.shape = AUDIO_PERC;
    audio_play(&v);
}
static void sfx_aim(void)
{
    voice(0, 18, 2200, 2200, W_SQUARE, 6);
}
static void sfx_dig(void)
{
    voice(0, 70, 160, 70, W_NOISE, 30);
    voice(0, 90, 220, 110, W_SINE, 34);
}
static float s_pop_last;
static void sfx_pop(int depth)
{
    if (s_wall - s_pop_last < 0.03f) {
        return;
    }
    s_pop_last = s_wall;
    voice(0, 40, 520 + depth * 60, 700 + depth * 70, W_TRI, 14);
}
static void sfx_flag(bool on)
{
    voice(0, 70, on ? 600 : 900, on ? 1100 : 500, W_SQUARE, 16);
    voice(30, 40, on ? 1400 : 400, on ? 1400 : 400, W_TRI, 10);
}
static void sfx_bump(void)
{
    voice(0, 90, 180, 150, W_SQUARE, 18);
}
static void sfx_coin(void)
{
    voice(0, 70, 1976, 1976, W_SQUARE, 16);
    voice(70, 160, 2637, 2637, W_SQUARE, 16);
}
static void sfx_shield(void)
{
    for (int i = 0; i < 5; i++) {
        voice(i * 50, 120, 1046 * (1 + i * 0.25f), 1046 * (1 + i * 0.25f), W_TRI, 18);
    }
}
static void sfx_mole(void)
{
    voice(0, 110, 700, 1300, W_SINE, 26);
    voice(120, 120, 1300, 800, W_SINE, 22);
}
static void sfx_meow(void)
{
    voice(0, 380, 620, 900, W_SAW, 14);
    voice(180, 260, 900, 560, W_SINE, 18);
}
static void sfx_fuse(void)
{
    voice(0, 500, 3000, 2600, W_NOISE, 10);
}
static void sfx_boom(void)
{
    voice(0, 900, 700, 40, W_NOISE, 85);
    voice(0, 700, 110, 30, W_SINE, 80);
}
static void sfx_fizz(void)
{
    voice(0, 600, 4000, 900, W_NOISE, 16);
}
static void sfx_chest(void)
{
    for (int i = 0; i < 6; i++) {
        voice(i * 60, 110, 784 * (1 + i * 0.2f), 784 * (1 + i * 0.2f), W_SQUARE, 14);
    }
}
// ---- the music
static float s_jingle_t;
static int s_loop = -1;
static int music_loop(void)
{
    return s_state == ST_TITLE ? MSM_TITLE : s_g < 4 ? MSM_DAY : MSM_NIGHT;
}
static void jingle(int track, int ms)
{
    if (!s_sound) {
        return;
    }
    midi_bg_play_track(track, 0, false);
    s_jingle_t = ms / 1000.0f - 0.03f;
    s_loop = -1;
}
static void music_step(float dt)
{
    if (!s_sound) {
        return;
    }
    if (s_jingle_t > 0) {
        s_jingle_t -= dt;
        if (s_jingle_t > 0) {
            return;
        }
    }
    int want = music_loop();
    if (want != s_loop) {
        midi_bg_play_track(want, 0, true);
        s_loop = want;
    }
}

// ================================================================ the effects
// the particles (the chips of a tile, dust, sparks, fire, smoke, confetti, and the things in the air of the garden)
enum {
    P_CHIP,
    P_DUST,
    P_SPARK,
    P_FIRE,
    P_SMOKE,
    P_CONF,
    P_AMB
};
typedef struct {
    float x, y, vx, vy, life, max;
    uint16_t c;
    uint8_t kind, size;
} part_t;
#define NPART 240
static part_t s_p[NPART];
static int s_np;
static void part(float x, float y, float vx, float vy, float life, uint16_t c, int kind, int size)
{
    if (s_np >= NPART) {
        return;
    }
    part_t *p = &s_p[s_np++];
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->life = p->max = life;
    p->c = c;
    p->kind = (uint8_t)kind;
    p->size = (uint8_t)size;
}
static int s_namb;
static void amb_spawn(bool any_y)
{
    const garden_t *G = &GARD[s_g];
    float x = rndf() * SW, y = any_y ? rndf() * SH : -4;
    uint16_t c;
    float vx = 0, vy = 0;
    switch (G->amb) {
    case AMB_PETAL:
        c = rndf() < 0.5f ? rgb3(G->deco0) : rgbc(255, 250, 250);
        vx = 10 + rndf() * 10;
        vy = 8 + rndf() * 8;
        break;
    case AMB_SUN:
        c = rgbc(255, 230, 120);
        vx = 4;
        vy = -3 + rndf() * 6;
        if (!any_y) {
            y = rndf() * SH;
        }
        break;
    case AMB_SEA:
        c = rgbc(255, 255, 255);
        vx = 18 + rndf() * 10;
        vy = -2;
        if (!any_y) {
            x = -4;
            y = rndf() * SH * 0.5f;
        }
        break;
    case AMB_LEAF:
        c = rndf() < 0.5f ? rgb3(G->deco0) : rgb3(G->deco1);
        vx = 14 + rndf() * 10;
        vy = 14 + rndf() * 8;
        break;
    case AMB_SNOW:
        c = rgbc(255, 255, 255);
        vx = -4 + rndf() * 8;
        vy = 12 + rndf() * 10;
        break;
    case AMB_SAND:
        c = rgbc(250, 226, 170);
        vx = 60 + rndf() * 40;
        vy = -2 + rndf() * 4;
        if (!any_y) {
            x = -4;
            y = rndf() * SH;
        }
        break;
    case AMB_FIREFLY:
        c = rgbc(220, 255, 120);
        vx = -6 + rndf() * 12;
        vy = -6 + rndf() * 12;
        if (!any_y) {
            y = rndf() * SH;
        }
        break;
    default:
        c = rndf() < 0.5f ? rgbc(255, 140, 40) : rgbc(255, 220, 90);
        vx = -4 + rndf() * 8;
        vy = -14 - rndf() * 14;
        if (!any_y) {
            y = SH + 4;
        }
        break;
    }
    part(x, y, vx, vy, 99, c, P_AMB, G->amb == AMB_PETAL || G->amb == AMB_LEAF ? 2 : 1);
    s_namb++;
}
static void parts_step(float dt)
{
    int w = 0;
    s_namb = 0;
    for (int i = 0; i < s_np; i++) {
        part_t *p = &s_p[i];
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        if (p->kind == P_CHIP || p->kind == P_CONF) {
            p->vy += 260 * dt;
        }
        if (p->kind == P_SPARK) {
            p->vy += 120 * dt;
        }
        if (p->kind == P_SMOKE || p->kind == P_DUST) {
            p->vx *= 1 - dt * 2;
            p->vy *= 1 - dt * 2;
        }
        if (p->kind == P_AMB) {
            int a = GARD[s_g].amb;
            if (a == AMB_PETAL || a == AMB_LEAF || a == AMB_SNOW) {
                p->vx += sinf(s_wall * 2 + i) * 12 * dt;
            }
            if (a == AMB_FIREFLY || a == AMB_SUN) {
                p->vx += (rndf() - 0.5f) * 40 * dt;
                p->vy += (rndf() - 0.5f) * 40 * dt;
                p->vx = clampf(p->vx, -12, 12);
                p->vy = clampf(p->vy, -12, 12);
            }
            if (p->x < -8 || p->x > SW + 8 || p->y < -10 || p->y > SH + 8) {
                continue;
            }
            s_namb++;
        }
        if (p->life <= 0) {
            continue;
        }
        s_p[w++] = *p;
    }
    s_np = w;
    while (s_namb < 26 && s_np < NPART - 40) {
        amb_spawn(false);
    }
}
static void parts_draw(int ox, int oy)
{
    for (int i = 0; i < s_np; i++) {
        part_t *p = &s_p[i];
        int x = (int)p->x + ox, y = (int)p->y + oy;
        float f = p->life / p->max;
        if (p->kind == P_SMOKE) {
            ui_circle(x, y, (int)(p->size * (1.6f - f)), p->c, (int)(140 * f));
            continue;
        }
        if (p->kind == P_FIRE) {
            ui_circle(x, y, (int)(p->size * (0.4f + f)), f > 0.6f ? rgbc(255, 240, 160) : p->c, (int)(230 * f));
            continue;
        }
        if (p->kind == P_DUST) {
            ui_circle(x, y, (int)(p->size * (1.4f - f * 0.6f)), p->c, (int)(150 * f));
            continue;
        }
        int s = p->size;
        uint16_t c = p->c;
        if (p->kind == P_AMB && GARD[s_g].amb == AMB_FIREFLY) {
            float b = 0.5f + 0.5f * sinf(s_wall * 4 + i);
            ui_circle(x, y, 3, c, (int)(90 * b));
            c = mix565(rgbc(60, 80, 40), c, (int)(32 * b));
        }
        if (p->kind == P_AMB && GARD[s_g].amb == AMB_EMBER) {
            ui_circle(x, y, 2, c, 90);
        }
        if (p->kind == P_SPARK && f < 0.4f) {
            c = mix565(c, rgbc(255, 80, 20), 16);
        }
        for (int j = 0; j < s; j++) {
            if (y + j < 0 || y + j >= SH) {
                continue;
            }
            uint16_t *d = g_fb + (y + j) * SW;
            for (int k = 0; k < s; k++) {
                if (x + k >= 0 && x + k < SW) {
                    d[x + k] = c;
                }
            }
        }
    }
}
// the things that fly to the HUD (a coin, a clover) or to a tile (the sparkle of the mole)
enum {
    FL_COIN,
    FL_CLOVER,
    FL_HINT
};
typedef struct {
    float x0, y0, x1, y1, t0, dur;
    int kind, target;
    bool on;
} fly_t;
#define NFLY 24
static fly_t s_fly[NFLY];
static void fly(int kind, float x0, float y0, float x1, float y1, float delay, float dur, int target)
{
    for (int i = 0; i < NFLY; i++) {
        if (!s_fly[i].on) {
            fly_t *f = &s_fly[i];
            f->x0 = x0;
            f->y0 = y0;
            f->x1 = x1;
            f->y1 = y1;
            f->t0 = s_wall + delay;
            f->dur = dur;
            f->kind = kind;
            f->target = target;
            f->on = true;
            return;
        }
    }
}
// short texts that float up (PHEW!, +1, SAFE HERE!)
typedef struct {
    float x, y, t0;
    const char *s;
    uint16_t c;
    bool on;
} floater_t;
static floater_t s_fl[6];
static void floater(float x, float y, const char *s, uint16_t c)
{
    for (int i = 0; i < 6; i++) {
        if (!s_fl[i].on) {
            s_fl[i] = (floater_t){x, y, s_wall, s, c, true};
            return;
        }
    }
}
static float s_shake;
// the mole of a hint on a tile, the chest that opens, the bomb that fizzles (the clover)
typedef struct {
    int kind, c, r;
    float t0;
    bool on, acted;
} spot_t;
enum {
    SP_MOLE,
    SP_CHEST,
    SP_DUD
};
#define NSPOT 6
static spot_t s_spot[NSPOT];
static void spot(int kind, int c, int r)
{
    for (int i = 0; i < NSPOT; i++) {
        if (!s_spot[i].on) {
            s_spot[i] = (spot_t){kind, c, r, s_wall, true, false};
            return;
        }
    }
}
// the cat: it walks in at the left, along the row of a safe tile, and sleeps on it
typedef struct {
    int st, c, r;
    float x, y, t;
    bool came;
} cat_t;
static cat_t s_cat;
// DIGBY
static int s_mood;
static float s_mood_t;
static void mood(int m, float t)
{
    s_mood = m;
    s_mood_t = t;
}

static inline int cell_x(int c)
{
    return s_bx + c * CELL;
}
static inline int cell_y(int r)
{
    return s_by + r * CELL;
}
// the targets of the coins and the clover in the HUD (see layout())
static int COIN_X = 96, COIN_Y = 54, CLOVER_X = 96, CLOVER_Y = 80;

// ================================================================ the aim and the fingers
static int s_ac = -1, s_ar = -1, s_cand = -1;
static float s_cand_t, s_loupe_t;
static bool s_board_finger;
enum {
    R_NONE,
    R_BOARD,
    R_DIG,
    R_FLAG
};
typedef struct {
    bool on;
    int x, y, role;
} fing_t;
static fing_t s_f[2];
static int s_press;  // the button that a finger holds now (for its picture)
// the buttons DIG and FLAG: side by side on the 3.49, DIG over FLAG on a narrow frame (see layout())
#define BTN_H 60
#define BTN_W 68
static int BTN_X[2] = {6, 78}, BTN_Y[2] = {108, 108};
static int btn_at(int x, int y)
{
    for (int b = 0; b < 2; b++) {
        if (x >= BTN_X[b] - 4 && x < BTN_X[b] + BTN_W + 4 && y >= BTN_Y[b] - 4 &&
            y <= BTN_Y[b] + BTN_H + (NARROW && b == 0 ? 0 : 4)) {
            return b ? R_FLAG : R_DIG;
        }
    }
    return R_NONE;
}
// the places of the HUD for this frame (enter())
static void layout(void)
{
    SW = gfx_w;
    SH = gfx_h;
    CX = SW / 2;
    if (RND) {
        // (the round frame: the board in the middle of the circle; the sign in the top cap, the coins and the clover
        // on the top arc, DIG and FLAG on the bottom arc, DIGBY in the bottom cap)
        HUDW = 0;
        COIN_X = SW * 22 / 100;
        COIN_Y = SH * 18 / 100;
        CLOVER_X = SW * 78 / 100 - 30;
        CLOVER_Y = SH * 18 / 100;
        BTN_X[0] = SW * 10 / 100;
        BTN_X[1] = SW - SW * 10 / 100 - BTN_W;
        BTN_Y[0] = BTN_Y[1] = SH * 66 / 100;
    } else if (NARROW) {
        HUDW = 92;
        COIN_X = 64;
        COIN_Y = 44;
        CLOVER_X = 64;
        CLOVER_Y = 68;
        BTN_X[0] = BTN_X[1] = 12;
        BTN_Y[0] = 94;
        BTN_Y[1] = 158;
    } else {
        HUDW = 150;
        COIN_X = 96;
        COIN_Y = 54;
        CLOVER_X = 96;
        CLOVER_Y = 80;
        BTN_X[0] = 6;
        BTN_X[1] = 78;
        BTN_Y[0] = BTN_Y[1] = 108;
        if (BIG) {
            BTN_Y[0] = BTN_Y[1] = SH - BTN_H - 14;  // (the Tab5: DIG and FLAG at the bottom, near the thumb)
        }
    }
}
static int cell_at(int x, int y)
{
    int c = (int)floorf((x - s_bx) / (float)CELL), r = (int)floorf((y - s_by) / (float)CELL);
    return inb(c, r) ? r * MAXC + c : -1;
}
static void act_dig(void);
static void act_flag(void);
static void aim_set(int i)
{
    if (i < 0 || (i % MAXC == s_ac && i / MAXC == s_ar)) {
        return;
    }
    s_ac = i % MAXC;
    s_ar = i / MAXC;
    sfx_aim();
}
static void fingers(void)
{
    int16_t xs[2], ys[2];
    int n = input_points(xs, ys, 2);
    int px[2], py[2];
    bool used[2] = {false, false};
    for (int i = 0; i < n; i++) {
        gfx_touch_to_logical(xs[i], ys[i], &px[i], &py[i]);
    }
    // (each finger of the last frame takes the nearest point; no point near = the finger came up)
    for (int f = 0; f < 2; f++) {
        if (!s_f[f].on) {
            continue;
        }
        int best = -1, bd = 90 * 90;
        for (int i = 0; i < n; i++) {
            if (!used[i]) {
                int dx = px[i] - s_f[f].x, dy = py[i] - s_f[f].y;
                if (dx * dx + dy * dy < bd) {
                    bd = dx * dx + dy * dy;
                    best = i;
                }
            }
        }
        if (best >= 0) {
            used[best] = true;
            s_f[f].x = px[best];
            s_f[f].y = py[best];
            continue;
        }
        // up: a button acts when the finger leaves it on the button
        s_f[f].on = false;
        if ((s_f[f].role == R_DIG || s_f[f].role == R_FLAG) && btn_at(s_f[f].x, s_f[f].y) == s_f[f].role) {
            if (s_f[f].role == R_DIG) {
                act_dig();
            } else {
                act_flag();
            }
        }
        if (s_f[f].role == R_BOARD) {
            s_loupe_t = 1.0f;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!used[i]) {
            for (int f = 0; f < 2; f++) {
                if (!s_f[f].on) {
                    s_f[f].on = true;
                    s_f[f].x = px[i];
                    s_f[f].y = py[i];
                    s_f[f].role = px[i] >= HUDW ? R_BOARD : btn_at(px[i], py[i]);
                    if (RND) {
                        // (the round frame: the buttons are round the board)
                        int b = btn_at(px[i], py[i]);
                        s_f[f].role = b != R_NONE ? b : R_BOARD;
                    }
                    if (s_f[f].role == R_BOARD) {
                        int c = cell_at(px[i], py[i]);
                        if (c >= 0) {
                            aim_set(c);
                            s_cand = c;
                            s_cand_t = s_wall;
                        }
                    }
                    break;
                }
            }
        }
    }
    // a finger on the board moves the aim (a new tile must stay under it for 70 ms: a finger that rolls off the glass
    // does not move it)
    s_board_finger = false;
    s_press = R_NONE;
    for (int f = 0; f < 2; f++) {
        if (!s_f[f].on) {
            continue;
        }
        if (s_f[f].role == R_BOARD) {
            s_board_finger = true;
            int c = cell_at(s_f[f].x, s_f[f].y);
            if (c >= 0 && c != s_ar * MAXC + s_ac) {
                if (c != s_cand) {
                    s_cand = c;
                    s_cand_t = s_wall;
                } else if (s_wall - s_cand_t >= 0.07f) {
                    aim_set(c);
                }
            }
        } else if (s_f[f].role != R_NONE && btn_at(s_f[f].x, s_f[f].y) == s_f[f].role) {
            s_press = s_f[f].role;
        }
    }
}

// ================================================================ the game
static void garden_start(int g)
{
    s_g = g;
    board_clear(g);
    int w = s_cols * CELL, h = s_rows * CELL;
    s_bx = HUDW + (SW - HUDW - w) / 2;
    s_by = (SH - h) / 2;
    sprites_make(&GARD[g]);
    bg_paint(g);
    s_state = ST_PLAY;
    s_state_t = 0;
    s_time = 0;
    s_timer = false;
    s_coins = 0;
    s_shield = 0;
    s_level_coins = 0;
    s_ac = s_cols / 2;
    s_ar = s_rows / 2;
    s_loupe_t = 0;
    memset(s_fly, 0, sizeof s_fly);
    memset(s_fl, 0, sizeof s_fl);
    memset(s_spot, 0, sizeof s_spot);
    memset(&s_cat, 0, sizeof s_cat);
    memset(s_f, 0, sizeof s_f);
    s_np = 0;
    for (int i = 0; i < 18; i++) {
        amb_spawn(true);
    }
    s_shake = 0;
    mood(MOOD_IDLE, 0);
}
static void title_go(void)
{
    s_state = ST_TITLE;
    s_state_t = 0;
    s_g = s_sel;
    sprites_make(&GARD[s_sel]);
    bg_paint(s_sel);
    s_np = 0;
    for (int i = 0; i < 18; i++) {
        amb_spawn(true);
    }
}
static void lose(void)
{
    s_state = ST_BOOM;
    s_state_t = 0;
    s_timer = false;
    cell_t *e = &B[s_boom_r][s_boom_c];
    e->st = CS_OPEN;
    e->t_open = s_now;
    e->popped = 1;
    float x = cell_x(s_boom_c) + 12, y = cell_y(s_boom_r) + 12;
    for (int i = 0; i < 26; i++) {
        float a = rndf() * 6.283f, v = 40 + rndf() * 160;
        part(x, y, cosf(a) * v, sinf(a) * v - 40, 0.4f + rndf() * 0.5f, rgbc(255, 150 + rndi(0, 80), 40), P_FIRE,
             4 + rndi(0, 4));
    }
    for (int i = 0; i < 16; i++) {
        float a = rndf() * 6.283f, v = 60 + rndf() * 140;
        part(x, y, cosf(a) * v, sinf(a) * v - 80, 0.8f + rndf() * 0.5f, rgb3(GARD[s_g].top), P_CHIP, 3);
    }
    for (int i = 0; i < 8; i++) {
        part(x + rndf() * 16 - 8, y + rndf() * 10 - 5, rndf() * 20 - 10, -20 - rndf() * 20, 1.4f + rndf(),
             rgbc(110, 104, 104), P_SMOKE, 10 + rndi(0, 6));
    }
    s_shake = 0.55f;
    sfx_boom();
    mood(MOOD_SOOT, 99);
    jingle(MSM_J_BOOM, MS_J_BOOM_MS);
}
static void win(void)
{
    s_state = ST_WIN;
    s_state_t = 0;
    s_timer = false;
    // the flags on all the mines (they become flowers)
    for (int r = 0; r < s_rows; r++) {
        for (int c = 0; c < s_cols; c++) {
            if (B[r][c].mine && B[r][c].st != CS_FLAG) {
                B[r][c].st = CS_FLAG;
                s_flags++;
                B[r][c].t_flag = s_now;
            }
        }
    }
    int t10 = (int)(s_time * 10);
    const garden_t *G = &GARD[s_g];
    int stars = 1 + (s_time <= g_par(s_g)) + (s_time <= g_par(s_g) * 0.6f);
    (void)G;
    s_new_stars = stars;
    s_new_best = !s_best[s_g] || t10 < s_best[s_g];
    if (s_new_best) {
        s_best[s_g] = t10;
        store_set(key_n("ms_b", s_g), (uint32_t)t10);
    }
    if (stars > s_star[s_g]) {
        s_star[s_g] = stars;
        store_set(key_n("ms_s", s_g), (uint32_t)stars);
    }
    if (s_g + 1 > s_unlock && s_g + 1 < NGARD) {
        s_unlock = s_g + 1;
        store_set("ms_unlock", (uint32_t)s_unlock);
    }
    s_total_coins += s_coins;
    store_set("ms_coins", (uint32_t)s_total_coins);
    mood(MOOD_CHEER, 99);
    if (s_g == NGARD - 1) {
        jingle(MSM_J_ALL, MS_J_ALL_MS);
    } else {
        jingle(MSM_J_CLEAR, MS_J_CLEAR_MS);
    }
}
static void act_dig(void)
{
    if (s_state != ST_PLAY || s_ac < 0) {
        return;
    }
    bool first = s_first;
    int res = dig(s_g, s_ac, s_ar);
    if (first && !s_first) {
        s_timer = true;
    }
    switch (res) {
    case DG_OK:
        sfx_dig();
        mood(MOOD_HAPPY, 0.5f);
        if (s_cat.st == 2 && s_cat.c == s_ac && s_cat.r == s_ar) {
            s_cat.st = 3;
            s_cat.t = 0;
            sfx_meow();
        }
        break;
    case DG_MINE:
        if (s_shield > 0) {
            // the clover saves: the bomb comes out, its fuse goes out; a flag goes on it
            s_shield--;
            cell_t *e = &B[s_boom_r][s_boom_c];
            e->st = CS_FLAG;
            s_flags++;
            e->t_flag = s_now + 1.2f;
            spot(SP_DUD, s_boom_c, s_boom_r);
            sfx_fuse();
            mood(MOOD_OOPS, 1.6f);
        } else {
            lose();
        }
        break;
    case DG_FLAGGED:
        sfx_bump();
        B[s_ar][s_ac].t_flag = s_now;
        mood(MOOD_LOOK, 0.6f);
        break;
    case DG_NOTYET:
        sfx_bump();
        mood(MOOD_OOPS, 0.8f);
        break;
    }
}
static void act_flag(void)
{
    if (s_state != ST_PLAY || s_ac < 0 || s_first) {
        if (s_state == ST_PLAY) {
            sfx_bump();
        }
        return;
    }
    int v = flag_toggle(s_ac, s_ar);
    if (v) {
        sfx_flag(v > 0);
        if (v > 0) {
            mood(MOOD_SALUTE, 0.6f);
        }
    } else {
        sfx_bump();
    }
}
// the surprise of a tile that opened now
static void surprise(int c, int r)
{
    cell_t *e = &B[r][c];
    e->done = 1;
    float x = cell_x(c) + 12, y = cell_y(r) + 12;
    switch (e->item) {
    case IT_COIN:
        fly(FL_COIN, x, y, COIN_X + 12, COIN_Y + 12, 0, 0.7f, 1);
        break;
    case IT_CLOVER:
        fly(FL_CLOVER, x, y, CLOVER_X + 12, CLOVER_Y + 12, 0, 0.9f, 0);
        sfx_shield();
        break;
    case IT_CHEST:
        spot(SP_CHEST, c, r);
        sfx_chest();
        for (int i = 0; i < 8; i++) {
            fly(FL_COIN, x, y - 6, COIN_X + 12, COIN_Y + 12, 0.45f + i * 0.09f, 0.7f, 1);
        }
        break;
    case IT_MOLE:
        spot(SP_MOLE, c, r);
        sfx_mole();
        break;
    }
}
static void cat_step(float dt)
{
    if (s_cat.st == 0 && !s_cat.came && s_g >= 1 && s_timer && s_time > 22) {
        int h = hint_pick();
        s_cat.came = true;
        if (h >= 0) {
            s_cat.st = 1;
            s_cat.c = h % MAXC;
            s_cat.r = h / MAXC;
            s_cat.x = RND ? s_bx - 30 : HUDW - 30;
            s_cat.y = cell_y(s_cat.r) - 2;
            sfx_meow();
        }
    }
    if (s_cat.st == 1) {
        float tx = cell_x(s_cat.c) - 2;
        s_cat.x += 46 * dt;
        if (s_cat.x >= tx) {
            s_cat.x = tx;
            s_cat.st = 2;
            B[s_cat.r][s_cat.c].hint = 2;
        }
        if (B[s_cat.r][s_cat.c].st == CS_OPEN) {
            s_cat.st = 3;
        }
    }
    if (s_cat.st == 2 && B[s_cat.r][s_cat.c].st == CS_OPEN) {
        s_cat.st = 3;
        s_cat.t = 0;
    }
    if (s_cat.st == 3) {
        s_cat.t += dt;
        s_cat.x += 140 * dt;
        s_cat.y += (s_cat.t - 0.35f) * 300 * dt;
        if (s_cat.x > SW + 30) {
            s_cat.st = 0;
        }
    }
}
static void game_step(float dt)
{
    s_now += dt;
    if (s_timer) {
        s_time += dt;
    }
    // the tiles of the wave that open now: their chips, their dust, their pop, their surprise
    for (int r = 0; r < s_rows; r++) {
        for (int c = 0; c < s_cols; c++) {
            cell_t *e = &B[r][c];
            if (e->st != CS_OPEN || e->popped || e->t_open > s_now) {
                continue;
            }
            e->popped = 1;
            float x = cell_x(c) + 12, y = cell_y(r) + 10;
            const garden_t *G = &GARD[s_g];
            for (int k = 0; k < 3; k++) {
                part(x + rndf() * 14 - 7, y + rndf() * 8 - 4, rndf() * 120 - 60, -60 - rndf() * 90,
                     0.5f + rndf() * 0.3f, k == 2 ? rgb3(G->topl) : rgb3(G->top), P_CHIP, 2 + (k == 0));
            }
            part(x, y + 6, rndf() * 20 - 10, -10, 0.5f, rgb3k(G->dirt, 90), P_DUST, 6);
            sfx_pop((int)((e->t_open - s_now + 1) * 10) & 7);
            if (e->item && !e->done) {
                surprise(c, r);
            }
        }
    }
    // the things that fly
    for (int i = 0; i < NFLY; i++) {
        fly_t *f = &s_fly[i];
        if (!f->on || s_wall < f->t0 + f->dur) {
            continue;
        }
        f->on = false;
        if (f->kind == FL_COIN) {
            s_coins += f->target;
            sfx_coin();
        } else if (f->kind == FL_CLOVER) {
            s_shield++;
        } else if (f->kind == FL_HINT && f->target >= 0) {
            cell_t *e = &B[f->target / MAXC][f->target % MAXC];
            if (e->st == CS_COV) {
                e->hint = 1;
                floater(f->x1, f->y1 - 14, TX(T_SAFE), rgbc(200, 255, 190));
            }
            for (int k = 0; k < 10; k++) {
                part(f->x1, f->y1, rndf() * 120 - 60, rndf() * 120 - 80, 0.5f, rgbc(255, 255, 200), P_SPARK, 2);
            }
        }
    }
    // the spots: the mole sends a sparkle to a safe tile at 0.7 s; the bomb of the clover fizzles at 1.0 s
    for (int i = 0; i < NSPOT; i++) {
        spot_t *s = &s_spot[i];
        if (!s->on) {
            continue;
        }
        float t = s_wall - s->t0, x = cell_x(s->c) + 12, y = cell_y(s->r) + 4;
        if (s->kind == SP_MOLE && t > 0.7f && !s->acted) {
            s->acted = true;
            int h = hint_pick();
            if (h >= 0) {
                fly(FL_HINT, x, y, cell_x(h % MAXC) + 12, cell_y(h / MAXC) + 12, 0, 0.6f, h);
            }
        }
        if (s->kind == SP_DUD && t > 1.0f && !s->acted) {
            s->acted = true;
            sfx_fizz();
            floater(x, y - 10, TX(T_PHEW), rgbc(190, 255, 170));
            for (int k = 0; k < 6; k++) {
                part(x + rndf() * 10 - 5, y, rndf() * 16 - 8, -20 - rndf() * 20, 1.2f, rgbc(170, 170, 170), P_SMOKE, 6);
            }
        }
        if (t > 2.2f) {
            s->on = false;
        }
    }
    cat_step(dt);
    if (board_won() && s_now > s_wave_end + 0.2f) {
        win();
    }
}

// ================================================================ the drawing
static void draw_cell(int c, int r, int x, int y, bool big)
{
    cell_t *e = &B[r][c];
    void (*draw)(const spr_t *, int, int) = big ? spr_draw2 : spr_draw;
    int k = big ? 2 : 1;
    bool open = e->st == CS_OPEN && e->t_open <= s_now;
    if (!open) {
        draw(&S_COV[e->var % NCOV], x, y);
        if (e->st == CS_FLAG) {
            bool bloom = s_state == ST_WIN && e->mine;
            if (bloom) {
                float t = s_state_t - 0.25f - ((c * 7 + r * 3) % 11) * 0.06f;
                draw(t < 0 ? &S_FLAG[0] : &S_BLOOM[t < 0.15f ? 0 : t < 0.3f ? 1 : 2], x, y - 3 * k);
            } else {
                float dt = s_now - e->t_flag;
                int lift = dt >= 0 && dt < 0.25f ? (int)((0.25f - dt) * 30) : 0;
                if (dt >= 0) {
                    draw(&S_FLAG[((int)(s_wall * 5) + c + r) % 3], x, y - 3 * k - lift * k);
                }
            }
            if (s_state == ST_BOOM && !e->mine && s_state_t > 1.2f) {
                draw(&S_X, x, y);
            }
        } else if (e->hint == 1) {
            draw(&S_SPROUT, x, y - (int)(fabsf(sinf(s_wall * 3 + c)) * 2) * k);
        }
        if (s_state == ST_BOOM && e->mine && e->st != CS_FLAG) {
            int i = r * MAXC + c, order = (int)(hash2(i, 5) % 40);
            if (s_state_t > 0.8f + order * 0.05f) {
                draw(&S_BOMB[((int)(s_wall * 8) + i) & 1], x, y - 2 * k);
            }
        }
        return;
    }
    draw(&S_OPEN[e->var % 3], x, y);
    // (the shade of the tile above)
    if (!big && r > 0 && B[r - 1][c].st != CS_OPEN) {
        shade_box(x, y, CELL, 4, rgbc(20, 14, 10), 9);
    }
    if (e->mine) {
        draw(&S_CRATER, x, y);
        return;
    }
    if (e->n) {
        float dt = s_now - e->t_open;
        int up = dt < 0.15f ? (int)((0.15f - dt) * 30) : 0;
        draw(&S_NUM[e->n], x + 4 * k, y + 3 * k - up * k);
    }
}
static void board_draw(int ox, int oy)
{
    const garden_t *G = &GARD[s_g];
    int w = s_cols * CELL, h = s_rows * CELL, x0 = s_bx + ox, y0 = s_by + oy;
    if (RND) {
        // (the round frame: a round frame of wood round the round board)
        uint16_t fr = rgb3(G->frame), frl = add565(fr, 40, 36, 30);
        int cx = x0 + w / 2, cy = y0 + h / 2, rr = w / 2 + 6;
        ui_ring(cx + 3, cy + 4, rr + 2, 6, rgbc(10, 6, 4), 110);
        ui_ring(cx, cy, rr, 5, fr, 255);
        ui_ring(cx, cy, rr - 3, 2, frl, 255);
        for (int r = 0; r < s_rows; r++) {
            for (int c = 0; c < s_cols; c++) {
                if (!s_hole[r][c]) {
                    draw_cell(c, r, x0 + c * CELL, y0 + r * CELL, false);
                }
            }
        }
        goto spots;
    }
    // the frame and its shadow
    shade_box(x0 + 4, y0 + h, w, 4, rgbc(10, 6, 4), 12);
    shade_box(x0 + w, y0 + 4, 4, h, rgbc(10, 6, 4), 12);
    uint16_t fr = rgb3(G->frame), frl = add565(fr, 40, 36, 30), frd = rgb3k(G->frame, 60);
    for (int i = -3; i < w + 3; i++) {
        for (int k = 1; k <= 3; k++) {
            int yy = y0 - k, yb = y0 + h + k - 1;
            uint16_t c = k == 3 ? frl : fr;
            if (yy >= 0 && x0 + i >= 0 && x0 + i < SW) {
                g_fb[yy * SW + x0 + i] = c;
            }
            if (yb < SH && x0 + i >= 0 && x0 + i < SW) {
                g_fb[yb * SW + x0 + i] = k == 3 ? frd : fr;
            }
        }
    }
    for (int j = 0; j < h; j++) {
        for (int k = 1; k <= 3; k++) {
            int yy = y0 + j;
            if (yy < 0 || yy >= SH) {
                continue;
            }
            if (x0 - k >= 0) {
                g_fb[yy * SW + x0 - k] = k == 3 ? frl : fr;
            }
            if (x0 + w + k - 1 < SW) {
                g_fb[yy * SW + x0 + w + k - 1] = k == 3 ? frd : fr;
            }
        }
    }
    for (int r = 0; r < s_rows; r++) {
        for (int c = 0; c < s_cols; c++) {
            draw_cell(c, r, x0 + c * CELL, y0 + r * CELL, false);
        }
    }
spots:
    // the spots on the board
    for (int i = 0; i < NSPOT; i++) {
        spot_t *s = &s_spot[i];
        if (!s->on) {
            continue;
        }
        float t = s_wall - s->t0;
        int x = cell_x(s->c) + ox, y = cell_y(s->r) + oy;
        if (s->kind == SP_MOLE) {
            spr_draw(&S_MOLE[t < 0.15f ? 0 : t < 0.3f ? 1 : t < 1.8f ? 2 : t < 2.0f ? 1 : 0], x, y - 4);
        } else if (s->kind == SP_CHEST) {
            int lift = (int)(fminf(t, 0.3f) * 30);
            spr_draw(&S_CHEST[t > 0.4f], x, y - lift);
            if (t > 0.4f && t < 1.2f) {
                ui_circle(x + 12, y - lift + 6, 10, rgbc(255, 230, 120), (int)(120 * (1.2f - t)));
            }
        } else if (s->kind == SP_DUD) {
            int lift = (int)(fminf(t, 0.25f) * 50);
            spr_draw(&S_BOMB[t < 1.0f ? ((int)(s_wall * 10) & 1) : 1], x, y - lift);
            if (t < 1.0f) {
                ui_circle(x + 20, y - lift, 3 + (int)(s_wall * 30) % 3, rgbc(255, 220, 90), 200);
            } else {
                spr_draw(&S_CLOVER, x - 10, y - lift - 14);
            }
        }
    }
    // the cat
    if (s_cat.st) {
        int f = s_cat.st == 1   ? ((int)(s_wall * 6) & 1)
                : s_cat.st == 2 ? (B[s_cat.r][s_cat.c].st == CS_FLAG ? 2 : 3)
                                : 0;
        spr_draw(&S_CAT[f], (int)s_cat.x + ox, (int)s_cat.y + oy);
        if (s_cat.st == 2 && f == 3 && fmodf(s_wall, 2.0f) < 1.2f) {
            ui_text_c(&KF_LABEL, (int)s_cat.x + ox + 26, (int)s_cat.y + oy - 2 - (int)(fmodf(s_wall, 2.0f) * 6), "z",
                      rgbc(255, 255, 255));
        }
    }
}
static void aim_draw(void)
{
    if (s_ac < 0 || s_state != ST_PLAY) {
        return;
    }
    int x = cell_x(s_ac), y = cell_y(s_ar), p = (int)(fabsf(sinf(s_wall * 5)) * 2);
    uint16_t c = rgbc(255, 250, 200), k = rgbc(30, 20, 10);
    for (int pass = 0; pass < 2; pass++) {
        uint16_t col = pass ? c : k;
        int o = pass ? 0 : 1, x0 = x - 2 - p + o, y0 = y - 2 - p + o, x1 = x + CELL + 1 + p + o,
            y1 = y + CELL + 1 + p + o;
        for (int i = 0; i < 7; i++) {
            for (int t = 0; t < 2; t++) {
                int pts[8][2] = {{x0 + i, y0 + t}, {x0 + t, y0 + i}, {x1 - i, y0 + t}, {x1 - t, y0 + i},
                                 {x0 + i, y1 - t}, {x0 + t, y1 - i}, {x1 - i, y1 - t}, {x1 - t, y1 - i}};
                for (int q = 0; q < 8; q++) {
                    int xx = pts[q][0], yy = pts[q][1];
                    if (xx >= 0 && yy >= 0 && xx < SW && yy < SH) {
                        g_fb[yy * SW + xx] = col;
                    }
                }
            }
        }
    }
}
// the loupe: the 3 x 3 tiles round the aim at 2 x, at the side away from the finger
static void loupe_draw(void)
{
    if (s_ac < 0 || s_state != ST_PLAY || (!s_board_finger && s_loupe_t <= 0)) {
        return;
    }
    int fx = 0;
    for (int f = 0; f < 2; f++) {
        if (s_f[f].on && s_f[f].role == R_BOARD) {
            fx = s_f[f].x;
        }
    }
    if (!s_board_finger) {
        fx = cell_x(s_ac) + 12;
    }
    int size = 3 * CELL * 2, lx = fx > HUDW + (SW - HUDW) / 2 ? HUDW + 8 : SW - size - 12, ly = (SH - size) / 2;
    if (RND) {
        // (the round frame: in the top cap or the bottom cap, away from the finger)
        int fy = cell_y(s_ar) + 12;
        for (int f = 0; f < 2; f++) {
            if (s_f[f].on && s_f[f].role == R_BOARD) {
                fy = s_f[f].y;
            }
        }
        lx = SW / 2 - size / 2;
        ly = fy > SH / 2 ? SH * 9 / 100 : SH - SH * 9 / 100 - size;
    } else if (NARROW) {
        int fy = cell_y(s_ar) + 12;
        for (int f = 0; f < 2; f++) {
            if (s_f[f].on && s_f[f].role == R_BOARD) {
                fy = s_f[f].y;
            }
        }
        ly = fy > SH / 2 ? 8 : SH - size - 8;
    }
    plate(lx - 6, ly - 6, size + 12, size + 12, 10, rgbc(20, 14, 10), 26);
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            int c = s_ac + dc, r = s_ar + dr, x = lx + (dc + 1) * CELL * 2, y = ly + (dr + 1) * CELL * 2;
            if (inb(c, r)) {
                draw_cell(c, r, x, y, true);
            } else {
                shade_box(x, y, CELL * 2, CELL * 2, rgbc(30, 24, 20), 30);
            }
        }
    }
    int x = lx + CELL * 2, y = ly + CELL * 2;
    for (int t = 0; t < 3; t++) {
        for (int i = -t; i < CELL * 2 + t; i++) {
            uint16_t c = t == 1 ? rgbc(255, 250, 200) : rgbc(30, 20, 10);
            int pts[4][2] = {
                    {x + i, y - t - 1}, {x + i, y + CELL * 2 + t}, {x - t - 1, y + i}, {x + CELL * 2 + t, y + i}};
            for (int q = 0; q < 4; q++) {
                int xx = pts[q][0], yy = pts[q][1];
                if (xx >= 0 && yy >= 0 && xx < SW && yy < SH) {
                    g_fb[yy * SW + xx] = c;
                }
            }
        }
    }
}
static void text_o(const kfont_t *f, int cx, int base, const char *s, uint16_t col)
{
    uint16_t k = rgbc(30, 18, 10);
    ui_text_c(f, cx - 1, base, s, k);
    ui_text_c(f, cx + 1, base, s, k);
    ui_text_c(f, cx, base - 1, s, k);
    ui_text_c(f, cx, base + 1, s, k);
    ui_text_c(f, cx, base, s, col);
}
static void flies_draw(int ox, int oy)
{
    for (int i = 0; i < NFLY; i++) {
        fly_t *f = &s_fly[i];
        if (!f->on || s_wall < f->t0) {
            continue;
        }
        float t = clampf((s_wall - f->t0) / f->dur, 0, 1), e = t * t * (3 - 2 * t);
        float x = lerpf(f->x0, f->x1, e), y = lerpf(f->y0, f->y1, e) - sinf(t * 3.1416f) * 40;
        const spr_t *s = f->kind == FL_COIN     ? &S_COIN[(int)(s_wall * 12 + i) & 3]
                         : f->kind == FL_CLOVER ? &S_CLOVER
                                                : NULL;
        if (s) {
            spr_draw(s, (int)x - 12 + ox, (int)y - 12 + oy);
        } else {
            ui_circle((int)x + ox, (int)y + oy, 6, rgbc(255, 255, 200), 120);
            ui_circle((int)x + ox, (int)y + oy, 3, rgbc(255, 255, 255), 255);
        }
    }
    for (int i = 0; i < 6; i++) {
        floater_t *f = &s_fl[i];
        if (!f->on) {
            continue;
        }
        float t = s_wall - f->t0;
        if (t > 1.4f) {
            f->on = false;
            continue;
        }
        text_o(&KF_LABEL, clampi((int)f->x, HUDW + 50, SW - 60), clampi((int)(f->y - t * 18), 16, SH - 4), f->s, f->c);
    }
}
static void button_draw(int which, int x, int y, bool pressed, int icon, bool dim)
{
    int dy = pressed ? 3 : 0;
    uint16_t base = which == R_DIG ? rgbc(150, 100, 58) : rgbc(150, 70, 60), top = add565(base, 40, 34, 28),
             bot = rgb3k((uint8_t[3]){150, 100, 58}, 55);
    if (!pressed) {
        ui_rrect(x, y + 4, BTN_W, BTN_H - 4, 10, bot);
    }
    ui_rrect(x, y + dy, BTN_W, BTN_H - 4, 10, dim ? mix565(base, rgbc(90, 90, 90), 16) : base);
    ui_rrect(x + 3, y + dy + 2, BTN_W - 6, 6, 3, dim ? mix565(top, rgbc(90, 90, 90), 16) : top);
    icon_draw(icon, x + (BTN_W - 42) / 2, y + dy + 7, dim);
}
static void hud_draw(void)
{
    const garden_t *G = &GARD[s_g];
    (void)G;
    char b[24];
    if (RND) {
        // (the round HUD: the sign in the top cap, the coins and the clover on the top arc, DIGBY in the bottom cap)
        int sx = SW / 2 - 72, sy = SH * 4 / 100;
        plate(sx, sy, 144, 38, 8, rgbc(30, 20, 12), 22);
        spr_draw(&S_BOMB[((int)(s_wall * 4)) & 1], sx + 4, sy + 6);
        snprintf(b, sizeof b, "%d", s_mines - s_flags);
        text_o(&KF_BIG, sx + 50, sy + 29, b, rgbc(255, 236, 200));
        int t = (int)s_time;
        snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
        ui_text_c(&KF_LABEL, sx + 110, sy + 24, b, rgbc(255, 236, 200));
        plate(COIN_X - 24, COIN_Y - 4, 72, 46, 8, rgbc(30, 20, 12), 16);
        spr_draw(&S_COIN[((int)(s_wall * 6)) & 3], COIN_X - 20, COIN_Y);
        snprintf(b, sizeof b, "%d", s_coins);
        text_o(&KF_TEXT, COIN_X + 26, COIN_Y + 19, b, rgbc(255, 220, 90));
        if (s_shield > 0) {
            plate(CLOVER_X - 24, CLOVER_Y - 4, 72, 46, 8, rgbc(30, 20, 12), 16);
            spr_draw(&S_CLOVER, CLOVER_X - 20, CLOVER_Y);
            snprintf(b, sizeof b, "x%d", s_shield);
            text_o(&KF_TEXT, CLOVER_X + 26, CLOVER_Y + 19, b, rgbc(180, 255, 160));
        }
        s_mk = 2;
        mole_draw(SW / 2 - 22, SH - SH * 4 / 100 - 46, s_mood, s_wall);
        s_mk = 3;
    } else if (NARROW) {
        // (the narrow HUD: the sign; DIGBY with the coins and the clover; DIG over FLAG)
        plate(2, 4, HUDW - 4, 36, 8, rgbc(30, 20, 12), 22);
        spr_draw(&S_BOMB[((int)(s_wall * 4)) & 1], 4, 10);
        snprintf(b, sizeof b, "%d", s_mines - s_flags);
        text_o(&KF_BIG, 42, 33, b, rgbc(255, 236, 200));
        int t = (int)s_time;
        snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
        ui_text_c(&KF_LABEL, 74, 27, b, rgbc(255, 236, 200));
        s_mk = 2;
        mole_draw(0, 44, s_mood, s_wall);
        s_mk = 3;
        plate(46, COIN_Y - 2, HUDW - 48, 48, 8, rgbc(30, 20, 12), 16);
        spr_draw(&S_COIN[((int)(s_wall * 6)) & 3], COIN_X - 18, COIN_Y);
        snprintf(b, sizeof b, "%d", s_coins);
        text_o(&KF_LABEL, COIN_X + 14, COIN_Y + 17, b, rgbc(255, 220, 90));
        if (s_shield > 0) {
            spr_draw(&S_CLOVER, CLOVER_X - 18, CLOVER_Y);
            snprintf(b, sizeof b, "x%d", s_shield);
            text_o(&KF_LABEL, CLOVER_X + 14, CLOVER_Y + 17, b, rgbc(180, 255, 160));
        }
    } else {
        // the sign: the mines that are left, the time
        plate(4, 4, 142, 36, 8, rgbc(30, 20, 12), 22);
        spr_draw(&S_BOMB[((int)(s_wall * 4)) & 1], 8, 10);
        snprintf(b, sizeof b, "%d", s_mines - s_flags);
        text_o(&KF_BIG, 54, 33, b, rgbc(255, 236, 200));
        int t = (int)s_time;
        snprintf(b, sizeof b, "%d:%02d", t / 60, t % 60);
        ui_text_c(&KF_LABEL, 116, 27, b, rgbc(255, 236, 200));
        // DIGBY, the coins, the clover
        mole_draw(4, 42, s_mood, s_wall);
        plate(76, COIN_Y - 2, 70, 52, 8, rgbc(30, 20, 12), 16);
        spr_draw(&S_COIN[((int)(s_wall * 6)) & 3], COIN_X - 18, COIN_Y);
        snprintf(b, sizeof b, "%d", s_coins);
        text_o(&KF_TEXT, COIN_X + 28, COIN_Y + 19, b, rgbc(255, 220, 90));
        if (s_shield > 0) {
            spr_draw(&S_CLOVER, CLOVER_X - 18, CLOVER_Y);
            snprintf(b, sizeof b, "x%d", s_shield);
            text_o(&KF_TEXT, CLOVER_X + 28, CLOVER_Y + 19, b, rgbc(180, 255, 160));
        }
    }
    // the buttons: DIG (or the clear of the neighbours of a number) and FLAG
    bool cov = s_ac >= 0 && B[s_ar][s_ac].st != CS_OPEN, chord = s_ac >= 0 && can_chord(s_ac, s_ar);
    bool can_dig = s_state == ST_PLAY && s_ac >= 0 && ((cov && B[s_ar][s_ac].st == CS_COV) || chord);
    bool can_flag = s_state == ST_PLAY && cov && !s_first;
    button_draw(R_DIG, BTN_X[0], BTN_Y[0], s_press == R_DIG, chord ? 2 : 0, !can_dig);
    button_draw(R_FLAG, BTN_X[1], BTN_Y[1], s_press == R_FLAG, 1, !can_flag);
}
static void star_draw(int cx, int cy, int r, bool on)
{
    // a five-point star from 10 points, filled row by row
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        float a = -1.5708f + i * 0.6283f, rr = i & 1 ? r * 0.45f : r;
        px[i] = cx + cosf(a) * rr;
        py[i] = cy + sinf(a) * rr;
    }
    uint16_t c = on ? rgbc(255, 210, 50) : rgbc(70, 60, 60), k = rgbc(40, 24, 10);
    for (int y = cy - r - 1; y <= cy + r + 1; y++) {
        if (y < 0 || y >= SH) {
            continue;
        }
        float xs[10];
        int n = 0;
        for (int i = 0; i < 10; i++) {
            float ax = px[i], ay = py[i], bx = px[(i + 1) % 10], by = py[(i + 1) % 10], yc = y + 0.5f;
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc)) {
                xs[n++] = ax + (yc - ay) * (bx - ax) / (by - ay);
            }
        }
        for (int i = 0; i + 1 < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (xs[j] < xs[i]) {
                    float t = xs[i];
                    xs[i] = xs[j];
                    xs[j] = t;
                }
            }
        }
        for (int i = 0; i + 1 < n; i += 2) {
            for (int x = (int)xs[i]; x <= (int)xs[i + 1]; x++) {
                if (x >= 0 && x < SW) {
                    g_fb[y * SW + x] = (x == (int)xs[i] || x == (int)xs[i + 1]) ? k : c;
                }
            }
        }
    }
}
static void arrow(int x, int y, int dir)
{
    for (int i = 0; i < 14; i++) {
        int h = dir > 0 ? 14 - i : i;
        for (int j = -h; j <= h; j++) {
            int xx = x + i * 1, yy = y + j;
            if (xx >= 0 && xx < SW && yy >= 0 && yy < SH) {
                g_fb[yy * SW + xx] = abs(j) == h ? rgbc(40, 24, 10) : rgbc(255, 244, 220);
            }
        }
    }
}
static void title_draw(void)
{
    const garden_t *G = &GARD[s_sel];
    char b[64];
    if (NARROW || RND) {
        // (one column: the name, the garden, its stars and best time, DIGBY, TAP TO PLAY; the arrows at the sides;
        // the round frame: the same column in the middle of the circle)
        const int ty = RND ? (SH - 240) / 2 : 0, ax = RND ? SW * 8 / 100 : 10;
        // (the round frame: bigger faces for the small lines (KF_LABEL had letters of about 1.2 mm on the 1.75C), and
        // the rows a little lower to make room for them)
        const int r = RND ? 1 : 0;
        const kfont_t *fs = r ? &KF_TEXT : &KF_LABEL;
        const kfont_t *ft = ui_text_w(&KF_TITLE, TX(T_TITLE)) <= SW - 16 ? &KF_TITLE : &KF_BIG;
        text_o(ft, CX, 46 + ty, TX(T_TITLE), rgbc(255, 240, 200));
        snprintf(b, sizeof b, "%s %d", TX(T_GARDEN), s_sel + 1);
        text_o(fs, CX, (r ? 72 : 66) + ty, b, rgbc(255, 255, 255));
        text_o(&KF_TEXT, CX, (r ? 96 : 88) + ty, G->name[s_lang ? 1 : 0], rgbc(255, 226, 150));
        for (int i = 0; i < 3; i++) {
            star_draw(CX - 30 + i * 30, (r ? 114 : 106) + ty, 10, s_star[s_sel] > i);
        }
        if (s_best[s_sel]) {
            snprintf(b, sizeof b, "%s %d:%02d.%d", TX(T_BEST), s_best[s_sel] / 600, s_best[s_sel] / 10 % 60,
                     s_best[s_sel] % 10);
            text_o(fs, CX, (r ? 140 : 130) + ty, b, rgbc(255, 244, 220));
        }
        if (s_sel > 0) {
            arrow(ax, (r ? 96 : 88) + ty, -1);
        }
        if (s_sel < s_unlock) {
            arrow(SW - 14 - ax, (r ? 96 : 88) + ty, 1);
        }
        const int my = r ? 8 : 0;  // (DIGBY and the coins: lower in the round frame)
        mole_draw(CX - 33, 134 + my + ty, s_state_t < 1.5f ? MOOD_CHEER : MOOD_IDLE, s_wall);
        if ((int)(s_wall * 2) & 1) {
            text_o(r ? &KF_BIG : &KF_LABEL, CX, (r ? 226 : 212) + ty, TX(T_PLAY), rgbc(255, 230, 140));
        }
        // (the hint: KF_TEXT when it fits the circle at its height)
        const int hy = (r ? 252 : 232) + ty;
        const int hw = r ? 2 * (int)sqrtf((float)(CX * CX - (hy - SH / 2) * (hy - SH / 2))) - 24 : SW;
        text_o(r && ui_text_w(&KF_TEXT, TX(T_HINT)) <= hw ? &KF_TEXT : &KF_LABEL, CX, hy, TX(T_HINT),
               rgbc(240, 236, 226));
        // (the coins of all the gardens: left of DIGBY)
        if (s_total_coins) {
            snprintf(b, sizeof b, "%d", s_total_coins);
            spr_draw(&S_COIN[0], CX - 96, 158 + my + ty);
            text_o(fs, CX - 84, 196 + my + ty, b, rgbc(255, 220, 90));
        }
        return;
    }
    const int ty = BIG ? (SH - 172) / 2 : 0;  // (the Tab5: the title in the middle of the height)
    mole_draw(30, 70 + ty, s_state_t < 1.5f ? MOOD_CHEER : MOOD_IDLE, s_wall);
    text_o(&KF_TITLE, 400, 36 + ty, TX(T_TITLE), rgbc(255, 240, 200));
    snprintf(b, sizeof b, "%s %d", TX(T_GARDEN), s_sel + 1);
    text_o(&KF_LABEL, 400, 58 + ty, b, rgbc(255, 255, 255));
    text_o(&KF_TEXT, 400, 78 + ty, G->name[s_lang ? 1 : 0], rgbc(255, 226, 150));
    for (int i = 0; i < 3; i++) {
        star_draw(370 + i * 30, 94 + ty, 10, s_star[s_sel] > i);
    }
    if (s_best[s_sel]) {
        snprintf(b, sizeof b, "%s %d:%02d.%d", TX(T_BEST), s_best[s_sel] / 600, s_best[s_sel] / 10 % 60,
                 s_best[s_sel] % 10);
        text_o(&KF_LABEL, 400, 122 + ty, b, rgbc(255, 244, 220));
    }
    if (s_sel > 0) {
        arrow(236, 78 + ty, -1);
    }
    if (s_sel < s_unlock) {
        arrow(550, 78 + ty, 1);
    }
    if ((int)(s_wall * 2) & 1) {
        text_o(&KF_LABEL, 400, 142 + ty, TX(T_PLAY), rgbc(255, 230, 140));
    }
    text_o(&KF_LABEL, 400, 164 + ty, TX(T_HINT), rgbc(240, 236, 226));
    if (s_total_coins) {
        snprintf(b, sizeof b, "%d", s_total_coins);
        spr_draw(&S_COIN[0], 8, 6);
        text_o(&KF_LABEL, 50, 23, b, rgbc(255, 220, 90));
    }
}
static void card_draw(void)
{
    const int oy = (SH - 172) / 2,
              pw = RND      ? SW * 64 / 100
                   : NARROW ? SW - HUDW - 12
                            : 260;  // (a narrow frame: lower, and in the board area)
    if (s_state == ST_BOOM && s_state_t > 1.6f) {
        if (RND) {
            plate(SW * 18 / 100, 46 + oy, SW * 64 / 100, 74, 12, rgbc(20, 10, 8), 24);
        } else if (NARROW) {
            plate(HUDW + 6, 46 + oy, SW - HUDW - 12, 74, 12, rgbc(20, 10, 8), 24);
        } else {
            plate(HUDW + 70, 46, SW - HUDW - 140, 74, 12, rgbc(20, 10, 8), 24);
        }
        text_o(&KF_BIG, (HUDW + SW) / 2, 82 + oy, TX(T_BOOM), rgbc(255, 170, 90));
        if (s_state_t > 2.2f) {
            text_o(&KF_LABEL, (HUDW + SW) / 2, 106 + oy, TX(T_AGAIN), rgbc(255, 244, 220));
        }
    }
    if (s_state == ST_WIN && s_state_t > 1.4f) {
        char b[48];
        // (a smaller plate that lets the flowers of the board show round it)
        int cx = (HUDW + SW) / 2;
        plate(cx - pw / 2, 44 + oy, pw, 84, 12, rgbc(20, 14, 8), 20);
        const char *ct = s_g == NGARD - 1 ? TX(T_ALL) : TX(T_CLEAR);
        text_o(ui_text_w(&KF_BIG, ct) <= pw - 12 ? &KF_BIG : &KF_TEXT, cx, 72 + oy, ct, rgbc(255, 230, 140));
        for (int i = 0; i < 3; i++) {
            float t = s_state_t - 1.6f - i * 0.25f;
            if (t > 0) {
                star_draw(cx - 34 + i * 34, 92 + oy, t < 0.15f ? 15 : 11, s_new_stars > i);
            }
        }
        int t10 = (int)(s_time * 10);
        snprintf(b, sizeof b, "%d:%02d.%d%s%s", t10 / 600, t10 / 10 % 60, t10 % 10, s_new_best ? "  " : "",
                 s_new_best ? TX(T_BEST) : "");
        text_o(&KF_LABEL, cx, 118 + oy, b, rgbc(255, 244, 220));
        if (s_state_t > 2.6f && s_g < NGARD - 1) {
            text_o(&KF_LABEL, cx, 150 + oy, TX(T_NEXT), rgbc(200, 255, 190));
        }
    }
}

// ================================================================ the settings panel
enum {
    HI_SND,
    HI_LANG,
    HI_NEW,
    HI_TITLE
};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const pitem_t ITEMS[] = {
        {PI_PICK, HI_SND, 2, 0, {"SOUND", "צליל"}, ONOFF_V, "ms_snd"},
        {PI_PICK, HI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "ms_lang"},
        {PI_ACT2, HI_NEW, 0, 0, {"NEW BOARD", "לוח חדש"}, NULL, NULL},
        {PI_ACT, HI_TITLE, 0, 0, {"THE TITLE", "המסך הראשי"}, NULL, NULL},
};
static bool item_shown(int id)
{
    return id != HI_NEW || s_state != ST_TITLE;
}
static void item_set(int id, int v)
{
    if (id == HI_SND) {
        s_sound = v == 0;
        if (!s_sound) {
            midi_bg_stop(200);
        } else {
            s_loop = -1;
            midi_bg_start();
        }
    } else if (id == HI_LANG) {
        s_lang = v;
    } else if (id == HI_NEW) {
        garden_start(s_g);
    } else if (id == HI_TITLE) {
        s_sel = s_g;
        title_go();
    }
}
static const pitems_t PANEL = {ITEMS, 4, item_shown, NULL, item_set, NULL, NULL};

// ================================================================ the app
static bool s_ok;
static int64_t s_last;
static float s_ms_sum;
static int s_ms_n;
static void enter(int64_t now)
{
    s_last = now;
    s_ok = false;
    s_err[0] = 0;
    layout();
    KEYC = rgb(255, 0, 255);
    s_lang = (int)store_get("ms_lang", 1) ? 1 : 0;
    s_sound = store_get("ms_snd", 0) == 0;
    s_unlock = clampi((int)store_get("ms_unlock", 0), 0, NGARD - 1);
    s_total_coins = (int)store_get("ms_coins", 0);
    for (int g = 0; g < NGARD; g++) {
        s_best[g] = (int)store_get(key_n("ms_b", g), 0);
        s_star[g] = (int)store_get(key_n("ms_s", g), 0);
    }
    s_sel = s_unlock;
    sysui_panel_items(&PANEL);
    s_bg = ms_alloc((size_t)SW * SH * 2);
    s_pool = ms_alloc(POOL_N);
    if (!s_bg || !s_pool) {
        snprintf(s_err, sizeof s_err, "NO MEMORY");
        return;
    }
    s_bg_g = -1;
    s_pool_at = 0;
    s_rng ^= (uint32_t)now * 2654435761u;
    title_go();
    midi_bg_init(MS_TRACKS, MS_TRACK_COUNT, MS_MUSIC_VOICES);
    midi_bg_volume(70);
    s_loop = -1;
    s_jingle_t = 0;
    if (s_sound) {
        midi_bg_start();
    }
    s_ok = true;
}
static void leave(void)
{
    midi_bg_end();
    free(s_bg);
    s_bg = NULL;
    s_bg_g = -1;
    free(s_pool);
    s_pool = NULL;
    s_ok = false;
}
static void input_ev(const input_event_t *ev)
{
    if (!s_ok || ev->kind != IN_TAP) {
        return;
    }
    if (s_state == ST_TITLE) {
        int xl0 = NARROW ? 0 : 200, xl1 = NARROW ? 64 : 290, xr = NARROW ? SW - 64 : 530;  // (the arrows)
        if (RND) {
            xl0 = 0;
            xl1 = SW * 22 / 100;
            xr = SW - SW * 22 / 100;
        }
        if (ev->x < xl1 && ev->x > xl0 && s_sel > 0) {
            s_sel--;
            sfx_aim();
            title_go();
            return;
        }
        if (ev->x > xr && s_sel < s_unlock) {
            s_sel++;
            sfx_aim();
            title_go();
            return;
        }
        sfx_dig();
        garden_start(s_sel);
        return;
    }
    if (s_state == ST_BOOM && s_state_t > 2.2f) {
        garden_start(s_g);
        return;
    }
    if (s_state == ST_WIN && s_state_t > 2.6f) {
        if (s_g + 1 < NGARD) {
            garden_start(s_g + 1);
        } else {
            s_sel = s_g;
            title_go();
        }
    }
}
// a slow tap (a press of 350 - 900 ms) on the title and the cards acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    input_ev(ev);
    if (slow && (s_state != ST_PLAY)) {
        input_ev(&tap);
    }
}
static void update(int64_t now)
{
    float dt = clampf((now - s_last) / 1000.0f, 0, 0.05f);
    s_last = now;
    if (!s_ok) {
        return;
    }
    midi_bg_service();
    s_wall += dt;
    s_state_t += dt;
    if (s_mood_t > 0) {
        s_mood_t -= dt;
        if (s_mood_t <= 0) {
            s_mood = MOOD_IDLE;
        }
    }
    if (s_loupe_t > 0) {
        s_loupe_t -= dt;
    }
    if (s_shake > 0) {
        s_shake -= dt;
    }
    if (s_state == ST_PLAY) {
        fingers();
        if (app_key_presses() > 0) {
            act_dig();
        }
        game_step(dt);
    } else if (s_state == ST_BOOM || s_state == ST_WIN) {
        s_now += dt;
        // fireworks over the garden that is clear
        if (s_state == ST_WIN && s_state_t > 0.6f && s_state_t < 6 && rndf() < dt * 2.2f) {
            float x = s_bx + rndf() * s_cols * CELL, y = 20 + rndf() * 60;
            uint16_t c = rgbc(150 + rndi(0, 105), 120 + rndi(0, 135), 120 + rndi(0, 135));
            for (int i = 0; i < 22; i++) {
                float a = i * 0.2856f, v = 60 + rndf() * 40;
                part(x, y, cosf(a) * v, sinf(a) * v, 0.9f, c, P_SPARK, 2);
            }
        }
        if (s_state == ST_BOOM && s_state_t > 0.8f) {
            // the other mines come out one by one (a small puff each)
            for (int r = 0; r < s_rows; r++) {
                for (int c = 0; c < s_cols; c++) {
                    cell_t *e = &B[r][c];
                    if (!e->mine || e->st == CS_FLAG || e->popped) {
                        continue;
                    }
                    int order = (int)(hash2(r * MAXC + c, 5) % 40);
                    if (s_state_t > 0.8f + order * 0.05f) {
                        e->popped = 1;
                        part(cell_x(c) + 12, cell_y(r) + 12, 0, -14, 0.6f, rgbc(160, 150, 150), P_SMOKE, 6);
                    }
                }
            }
        }
    }
    parts_step(dt);
    music_step(dt);
}
static void render(int64_t now)
{
    (void)now;
    if (!s_ok) {
        gfx_fill(rgbc(30, 40, 30));
        ui_text_c(&KF_LABEL, CX, 90, s_err[0] ? s_err : "...", rgbc(255, 220, 200));
        return;
    }
    int64_t a = ms_us();
    memcpy(g_fb, s_bg, (size_t)SW * SH * 2);
    if (s_state == ST_TITLE) {
        parts_draw(0, 0);
        title_draw();
    } else {
        int ox = 0, oy = 0;
        if (s_shake > 0) {
            ox = (int)(sinf(s_wall * 61) * s_shake * 9);
            oy = (int)(cosf(s_wall * 47) * s_shake * 6);
        }
        board_draw(ox, oy);
        aim_draw();
        parts_draw(ox, oy);
        if (s_state == ST_BOOM && s_state_t < 0.5f) {
            int x = cell_x(s_boom_c) + 12 + ox, y = cell_y(s_boom_r) + 12 + oy;
            float t = s_state_t / 0.5f;
            ui_circle(x, y, (int)(14 + t * 60), rgbc(255, 140, 40), (int)(200 * (1 - t)));
            ui_circle(x, y, (int)(8 + t * 36), rgbc(255, 240, 180), (int)(255 * (1 - t)));
        }
        hud_draw();
        flies_draw(0, 0);
        loupe_draw();
        card_draw();
    }
    s_ms_sum += (ms_us() - a) / 1000.0f;
    s_ms_n++;
#ifdef ESP_PLATFORM
    if (s_ms_n == 150) {
        ESP_LOGI("mines", "frame %.1f ms (draw), garden %d, %d tries", s_ms_sum / s_ms_n, s_g + 1, s_gen_tries);
        s_ms_sum = 0;
        s_ms_n = 0;
    }
#endif
}
#include "ms_icon.inc"
const app_t app_mines = {
        .name = "MINESWEEPER",
        .landscape = true,
        .round = APP_ROUND_HI,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .key = true,
};
