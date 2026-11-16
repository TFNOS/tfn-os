// BACKGAMMON (GAMES): backgammon ("shesh besh") on a wood and felt board with checkers made in Blender (tools/mk_art.py
// -> bg_art.c: the board of each frame with numpy, the checkers). The rules and the computer: bg_engine.inc. VS COMPUTER
// (EASY, MEDIUM, HARD: HARD looks at the answers of all 21 rolls), 2 PLAYERS on this board, 2 BOARDS (libs/duel, the
// id 'B': the host plays white). The checkers are white and black. A game: the opening roll (one die each), then
// turns; a single game, a GAMMON (2) or a BACKGAMMON (3); the score of the session.
// THE TOUCH: tap ROLL (or the board), or shake the board (SHAKE TO ROLL); drag a checker to its new point (or to the
// tray at the right, to bear off), or tap the checker, then its new point. The dice may go in any order: a step is
// legal when the rest of the dice can still make a whole turn. UNDO takes back the steps of this turn. The turn ends
// by itself after its last step. HINTS (the settings panel) shows the checkers that can move and their new points.
// THE SKINS: WOOD (the default: black and red points on oak), BEACH, CLUB (the green felt). The sounds:
// 9 CC0 MP3 effects in the app file (bg_sfx.c, sfx/SOURCES.md; libs/tfsfx).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "jingle/jingle.h"
// the opening of BACKGAMMON: a tavern of the Levant: an oud-like plucked line in D (a raised third), a darbuka
static const tune_note_t BG_OPEN[] = {
        JN(0, 160, J_D4, W_SAW, 26),
        JN(150, 120, J_Eb4, W_SAW, 24),
        JN(270, 180, J_Gb4, W_SAW, 26),
        JN(450, 120, J_G4, W_SAW, 24),
        JN(570, 160, J_A4, W_SAW, 26),
        JSL(730, 260, J_Bb4, -1, W_SAW, 24),
        JN(1000, 500, J_D5, JHOLD(W_SAW), 22),
        JN(1000, 500, J_A4, JHOLD(W_TRI), 16),
        JN(0, 900, J_D3, JHOLD(W_TRI), 40),
        JN(1000, 500, J_D3, JHOLD(W_TRI), 45),
        JKICK(0),
        JHAT(225),
        JSNARE(300),
        JHAT(450),
        JKICK(600),
        JSNARE(750),
        JHAT(900),
        JKICK(1000),
};
#include "tfn/board.h"
#include "tfn/duel.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "miniz.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "tfsfx/tfsfx.h"
#include "bg_sfx.h"
static const char *TAG = "backgammon";
#include "bg_engine.inc"

static int s_lang = 1;
#define T(en, he) (s_lang ? (he) : (en))
static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// ---------------------------------------------------------------- THE GEOMETRY (the same numbers as tools/mk_art.py)
#if BOARD_UI_TABLET
// (the Tab5 640 x 360: GEO[2], the board at the right, the panel at the left; its art: tools/mk_art_tab5.py)
static const struct {
    int16_t w, h, fr, pw, ph, barw, ck;
} GEO[3] = {{546, 172, 6, 40, 74, 24, 28}, {312, 196, 4, 22, 86, 16, 20}, {528, 352, 8, 36, 150, 32, 34}};
#include "bg_art_tab5.inc"
#elif BOARD_ROUND
// (the round panels: GEO[3] the AMOLED-1.75C 466 x 466, GEO[4] the Watcher 412 x 412: the board fits in the circle,
// the status on the top arc, ROLL / UNDO on the bottom arc; the art: tools/mk_art_round.py. GEO[2] is not used here.)
static const struct {
    int16_t w, h, fr, pw, ph, barw, ck;
} GEO[5] = {{546, 172, 6, 40, 74, 24, 28},
            {312, 196, 4, 22, 86, 16, 20},
            {312, 196, 4, 22, 86, 16, 20},
            {400, 236, 5, 28, 104, 20, 26},
            {354, 208, 4, 25, 92, 16, 24}};
#include "bg_art_round.inc"
#else
static const struct {
    int16_t w, h, fr, pw, ph, barw, ck;
} GEO[2] = {{546, 172, 6, 40, 74, 24, 28}, {312, 196, 4, 22, 86, 16, 20}};
#endif
static int K;  // 0 the 3.49, 1 a narrow frame (320 x 240), 2 the Tab5, 3 / 4 the round 466 / 412
#define RND (K >= 3)
static int OX, OY;  // the top left of the board on the screen
#define G_ (GEO[K])

// ---------------------------------------------------------------- the art (bg_art.c)
extern const uint8_t BG_ART[];
extern const uint32_t BG_ART_LEN;
static uint8_t *s_art;
static int s_skin;  // 0 WOOD, 1 BEACH, 2 CLUB (the panel item BOARD)
static uint16_t *s_board, *s_ckc[2];
static uint8_t *s_cka[2];
static void *bg_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
#else
    return malloc(n);
#endif
}
static bool art_load(void)
{
    free(s_art);
    s_art = NULL;
    s_board = NULL;
    const uint8_t *b = BG_ART;
    uint32_t len = BG_ART_LEN;
#if BOARD_UI_TABLET
    if (K == 2) {
        b = BG_ART_T5;
        len = sizeof BG_ART_T5;
    }
#endif
#if BOARD_ROUND && !BOARD_UI_TABLET
    // (the round: one picture of the whole circle, gfx_w x gfx_h)
    if (RND) {
        b = BG_ART_R;
        len = sizeof BG_ART_R;
    }
#endif
    if (len < 5 || memcmp(b, "BGA1", 4)) {
        return false;
    }
    size_t at = 5;
    for (int k = 0; k < b[4]; k++) {
        const int kind = b[at], w = b[at + 1] | b[at + 2] << 8, h = b[at + 3] | b[at + 4] << 8, ck = b[at + 5];
        const uint32_t zl = b[at + 6] | b[at + 7] << 8 | b[at + 8] << 16 | (uint32_t)b[at + 9] << 24;
        at += 10;
        if (kind == (K | s_skin << 4) && w == (RND ? gfx_w : G_.w) && h == (RND ? gfx_h : G_.h) && ck == G_.ck) {
            const size_t out = (size_t)w * h * 2 + 2 * ((size_t)ck * ck * 3);
            s_art = bg_alloc(out);
            tinfl_decompressor *d = malloc(sizeof(tinfl_decompressor));
            if (!s_art || !d) {
                free(d);
                return false;
            }
            tinfl_init(d);
            size_t in = zl, got = out;
            const int st = tinfl_decompress(d, b + at, &in, s_art, s_art, &got,
                                            TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            free(d);
            if (st != TINFL_STATUS_DONE || got != out) {
                return false;
            }
            uint8_t *q = s_art;
            s_board = (uint16_t *)q;
            q += (size_t)w * h * 2;
            for (int i = 0; i < 2; i++) {
                s_ckc[i] = (uint16_t *)q;
                q += (size_t)ck * ck * 2;
                s_cka[i] = q;
                q += (size_t)ck * ck;
            }
            return true;
        }
        at += zl;
    }
    return false;
}

// ---------------------------------------------------------------- the game
enum {
    G_TITLE,
    G_WAIT,
    G_OPEN,
    G_ROLL,
    G_DICE,
    G_MOVE,
    G_DONE,
    G_OVER,
    G_SHAKE
};
enum {
    MODE_CPU,
    MODE_TWO,
    MODE_NET
};
static struct {
    int g, mode, level, my_side, side;  // the state, the mode, the level, my colour (0 light, 1 red), the side to move
    bgpos_t pos, turn0;                 // the position; the one at the start of this turn (UNDO)
    int d[2], open_d[2];                // the dice; the opening dice (one each)
    int used[4], nused, ndice;          // the dice of this turn (2 or 4) and which are used
    bgstep_t steps[4];
    int nsteps, most;               // the steps taken; the most steps of this roll
    int sel;                        // the selected source (0..23, 24 the bar, -1 none)
    float t;                        // the time in the state
    int score[2], last_sc, winner;  // the score of the session; the last game: its points, its winner
    char note[48];
    float note_t;
} B;
static bgturn_t s_tt[2][BG_MAXT];
static bgpos_t s_ee[2][BG_MAXT];

// a slide of one checker (a step of the computer or of the friend, or mine); fx, fy >= 0: it starts at a point of the
// screen (the drop of a drag) and not at its point
static struct {
    bool on;
    bgstep_t s;
    int side;
    float t, len;
    int fx, fy;
} A;
#define SLIDE_S 0.28f
// the checker that is hit: it flies from its point to the bar
static struct {
    bool on;
    int side;
    float t;
    int x0, y0;
} HF;
// the drag of a checker: the source, the finger, the moved flag (a small move is still a tap)
static struct {
    bool on, moved;
    int src, x, y, x0, y0;
    float snd_t;
} D;
// a checker that goes back to its point after a drop on no target
static struct {
    bool on;
    int side, src;
    float t;
    int x0, y0;
} RB;
// the two dice in the air: the place, the speed, the height, the turn, the face (they fall, slide, bounce, settle)
typedef struct {
    float x, y, vx, vy, z, vz, a, va;
    int face;
    bool rest;
} die_t;
static die_t s_die[2];
static float s_shake_t, s_calm_t;  // the shake of the board: its time, the time with no shake
static bgstep_t s_lastmv[4];       // the last turn of the other side (it shows on my turn)
static int s_nlast, s_last_side = -1;
static bgstep_t s_queue[4];  // the steps that wait for their slide (the computer, the friend)
static int s_nq;
static int s_q_side;

// ---------------------------------------------------------------- the sounds
static bool s_snd = true;
static void voice(float d_ms, float len_ms, float f0, float f1, int wave, int vol)
{
    if (!s_snd) {
        return;
    }
    audio_voice_t v = {0};
    v.delay = (uint32_t)(d_ms * AUDIO_SAMPLE_RATE / 1000);
    v.dur = (uint32_t)(len_ms * AUDIO_SAMPLE_RATE / 1000);
    v.f0 = f0;
    v.f1 = f1;
    v.wave = (uint8_t)wave;
    v.vol = (uint8_t)vol;
    v.shape = AUDIO_PERC;
    audio_play(&v);
}
static void snd_click(void)
{
    voice(0, 16, 1700, 1500, W_TRI, 14);
}
enum {
    FX_SHAKE,
    FX_THROW1,
    FX_THROW2,
    FX_BOUNCE,
    FX_PLACE,
    FX_STACK,
    FX_SLIDE,
    FX_HIT,
    FX_OFF,
    FX_N
};
static const char *const FXN[FX_N] = {"shake", "throw1", "throw2", "bounce", "place", "stack", "slide", "hit", "off"};
static void fx(int id, int vol, float var)
{
    if (s_snd) {
        tfsfx_play_var(id, vol, var);
    }
}
static void snd_place(bool stack)
{
    fx(stack ? FX_STACK : FX_PLACE, 80, 0.06f);
}
static void snd_hit(void)
{
    fx(FX_HIT, 90, 0.05f);
}
static void snd_off(void)
{
    fx(FX_OFF, 80, 0.05f);
}
static void snd_land(void)
{
    fx(FX_THROW1, 85, 0.08f);
}
static void snd_bad(void)
{
    voice(0, 70, 240, 180, W_SQUARE, 16);
}
static void snd_end(bool won)
{
    static const float UP[4] = {523, 659, 784, 1046}, DN[4] = {392, 349, 311, 262};
    for (int i = 0; i < 4; i++) {
        voice(i * 130, 260, won ? UP[i] : DN[i], won ? UP[i] : DN[i], W_TRI, 24);
    }
}

// ---------------------------------------------------------------- the legal next steps (any order of the dice)
static int max_more(const bgpos_t *b, int side, const int *dice, int nd)
{
    int best = 0;
    for (int k = 0; k < nd; k++) {
        if (k > 0 && dice[k] == dice[k - 1]) {
            continue;
        }
        for (int from = 24; from >= 0; from--) {
            bgstep_t s;
            if (!bg_step_ok(b, side, from, dice[k], &s)) {
                continue;
            }
            bgpos_t q = *b;
            bg_apply(&q, side, &s);
            int rest[4], nr = 0;
            for (int j = 0; j < nd; j++) {
                if (j != k) {
                    rest[nr++] = dice[j];
                }
            }
            const int m = 1 + max_more(&q, side, rest, nr);
            if (m > best) {
                best = m;
            }
            if (best == nd) {
                return best;
            }
        }
    }
    return best;
}
static int rest_dice(int *out)
{
    int n = 0;
    for (int i = 0; i < B.ndice; i++) {
        if (!B.used[i]) {
            out[n++] = B.d[i < 2 ? i : 0];
        }
    }
    // (sorted high first, as bg_turns)
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && out[j] > out[j - 1]; j--) {
            const int t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    }
    return n;
}
// the legal next steps now (from any source, or from src >= 0)
static int next_steps(bgstep_t *out, int src)
{
    int dice[4];
    const int nd = rest_dice(dice);
    int n = 0;
    if (B.nsteps >= B.most) {
        return 0;
    }
    for (int k = 0; k < nd; k++) {
        if (k > 0 && dice[k] == dice[k - 1]) {
            continue;
        }
        for (int from = 24; from >= 0; from--) {
            if (src >= 0 && from != src) {
                continue;
            }
            bgstep_t s;
            if (!bg_step_ok(&B.pos, B.side, from, dice[k], &s)) {
                continue;
            }
            bgpos_t q = B.pos;
            bg_apply(&q, B.side, &s);
            int rest[4], nr = 0;
            for (int j = 0; j < nd; j++) {
                if (j != k) {
                    rest[nr++] = dice[j];
                }
            }
            if (B.nsteps + 1 + max_more(&q, B.side, rest, nr) < B.most) {
                continue;
            }
            // (only one die of two can be used: the higher one when it can)
            if (B.most == 1 && B.ndice == 2 && B.d[0] != B.d[1] && dice[k] != (B.d[0] > B.d[1] ? B.d[0] : B.d[1])) {
                bool hi = false;
                bgstep_t t2;
                for (int f = 24; f >= 0; f--) {
                    if (bg_step_ok(&B.pos, B.side, f, B.d[0] > B.d[1] ? B.d[0] : B.d[1], &t2)) {
                        hi = true;
                    }
                }
                if (hi) {
                    continue;
                }
            }
            if (n < 64) {
                out[n++] = s;
            }
        }
    }
    return n;
}

// ---------------------------------------------------------------- the places on the screen
static bool view_flip(void)
{
    return B.mode == MODE_NET && B.my_side == 1;
}
static int v_of(int i)
{
    return view_flip() ? 23 - i : i;
}
// the column of a point (0..11 from the left) and its row (true = the top)
static void col_of(int i, int *k, bool *top)
{
    const int v = v_of(i);
    *top = v >= 12;
    *k = *top ? v - 12 : 11 - v;
}
static int col_x(int k)
{
    return OX + G_.fr + k * G_.pw + (k >= 6 ? G_.barw : 0);
}
// the middle of checker j (0 = the first) of a point with n checkers
static void stack_xy(int i, int j, int n, int *cx, int *cy)
{
    int k;
    bool top;
    col_of(i, &k, &top);
    const int ck = G_.ck, ph = G_.ph;
    const int step = n <= 1 ? ck : (ck * (n - 1) <= ph - ck ? ck : (ph - ck) / (n - 1));
    *cx = col_x(k) + G_.pw / 2;
    *cy = top ? OY + G_.fr + ck / 2 + j * step : OY + G_.h - G_.fr - ck / 2 - j * step;
}
static int bar_x(void)
{
    return OX + G_.fr + 6 * G_.pw + G_.barw / 2;
}
static int tray_x(void)
{
    return OX + 2 * G_.fr + 12 * G_.pw + G_.barw;
}
static int tray_w(void)
{
    return G_.w - (2 * G_.fr + 12 * G_.pw + G_.barw) - G_.fr / 2;
}
// the place of the bar checker j of a side (light's in the top half, red's in the bottom half: where they enter)
static void bar_xy(int side, int j, int *cx, int *cy)
{
    const bool top = (side == 0) != view_flip();
    *cx = bar_x();
    *cy = top ? OY + G_.h / 2 - G_.ck / 2 - 4 - j * (G_.ck / 3) : OY + G_.h / 2 + G_.ck / 2 + 4 + j * (G_.ck / 3);
}
// the tray: the top half the side that is not at the bottom
static bool tray_top(int side)
{
    return (side == 1) != view_flip();
}

// ---------------------------------------------------------------- the drawing
static inline uint16_t mix16(uint16_t d, uint16_t s, int a)
{
    uint32_t dd = (uint16_t)(d >> 8 | d << 8), ss = (uint16_t)(s >> 8 | s << 8);
    dd = (dd | dd << 16) & 0x07E0F81F;
    ss = (ss | ss << 16) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * (uint32_t)a * 2) >> 5)) & 0x07E0F81F;
    r = (r | (r >> 16)) & 0xFFFF;
    return (uint16_t)(r >> 8 | r << 8);
}
// a checker; lift (px): it is in the air (a softer shadow under it, further down)
static void checker_l(int side, int cx, int cy, int lift)
{
    const int ck = G_.ck, x0 = cx - ck / 2, y0 = cy - ck / 2;
    ui_circle(cx + 1 + lift / 3, cy + 2 + lift, ck * 9 / 20 + lift / 4, rgb(0, 0, 0), lift ? 60 : 80);
    for (int j = 0; j < ck; j++) {
        const int yy = y0 + j;
        if (yy < 0 || yy >= gfx_h) {
            continue;
        }
        uint16_t *d = g_fb + yy * gfx_w;
        const uint16_t *c = s_ckc[side] + j * ck;
        const uint8_t *a = s_cka[side] + j * ck;
        for (int i = 0; i < ck; i++) {
            const int xx = x0 + i;
            if (xx < 0 || xx >= gfx_w || !a[i]) {
                continue;
            }
            d[xx] = a[i] >= 15 ? c[i] : mix16(d[xx], c[i], a[i] + (a[i] > 7));
        }
    }
}
static void checker(int side, int cx, int cy)
{
    checker_l(side, cx, cy, 0);
}
// a die seen from above, turned by ang (rad), at the height z (px: the shadow moves away and the die is bigger); the
// face is lit from the top left; the pips are holes with a shade
static void die3d(float cx, float cy, int s0, float ang, float z, int v, bool dim, bool red)
{
    const float s = s0 * (1.0f + z * 0.012f), hs = s * 0.5f, r = s * 0.2f;
    const float ca = cosf(ang), sa = sinf(ang);
    const int ext = (int)(s * 0.75f) + 2;
    static const int8_t P[7][6][2] = {{{0}},
                                      {{2, 2}},
                                      {{1, 1}, {3, 3}},
                                      {{1, 1}, {2, 2}, {3, 3}},
                                      {{1, 1}, {3, 1}, {1, 3}, {3, 3}},
                                      {{1, 1}, {3, 1}, {2, 2}, {1, 3}, {3, 3}},
                                      {{1, 1}, {3, 1}, {1, 2}, {3, 2}, {1, 3}, {3, 3}}};
    const int sx = (int)(2 + z * 0.35f), sy = (int)(3 + z * 0.6f);
    for (int pass = 0; pass < 2; pass++) {
        const float ox = pass ? 0 : (float)sx, oy = pass ? 0 : (float)sy;
        for (int j = -ext; j <= ext; j++) {
            const int yy = (int)(cy + oy) + j;
            if (yy < 0 || yy >= gfx_h) {
                continue;
            }
            uint16_t *d = g_fb + yy * gfx_w;
            for (int i = -ext; i <= ext; i++) {
                const int xx = (int)(cx + ox) + i;
                if (xx < 0 || xx >= gfx_w) {
                    continue;
                }
                // (the point in the frame of the die)
                const float u = i * ca + j * sa, w = -i * sa + j * ca;
                const float qx = fabsf(u) - (hs - r), qy = fabsf(w) - (hs - r);
                const float dx = qx > 0 ? qx : 0, dy = qy > 0 ? qy : 0, dd = dx * dx + dy * dy;
                if (dd > r * r) {
                    continue;
                }
                if (!pass) {
                    d[xx] = mix16(d[xx], rgb(0, 0, 0), z > 2 ? 6 : 9);
                    continue;
                }
                const bool rim = dd > (r - 1.2f) * (r - 1.2f) && (dx > 0 || dy > 0);
                const float lt = (-u - w) / s;                                    // (the light: the top left)
                int R = red ? 40 : 246, G = red ? 40 : 242, B2 = red ? 46 : 232;  // (red = the die of BLACK: black)
                const int sh = (int)(lt * 34);
                R += sh;
                G += sh;
                B2 += sh;
                if (rim) {
                    R = R * 3 / 4;
                    G = G * 3 / 4;
                    B2 = B2 * 3 / 4;
                }
                // the pips
                for (int k = 0; k < v && k < 6; k++) {
                    const float px = (P[v][k][0] - 2) * s / 4.0f, py = (P[v][k][1] - 2) * s / 4.0f,
                                pr = s * 0.1f + 0.6f;
                    const float e2 = (u - px) * (u - px) + (w - py) * (w - py);
                    if (e2 < pr * pr) {
                        const bool shade = (u - px) + (w - py) < -pr * 0.3f;  // (the upper side of a hole is in shade)
                        R = red ? (shade ? 200 : 255) : (shade ? 10 : 34);
                        G = red ? (shade ? 200 : 255) : (shade ? 10 : 30);
                        B2 = red ? (shade ? 200 : 255) : (shade ? 16 : 40);
                    }
                }
                if (R < 0) {
                    R = 0;
                }
                if (G < 0) {
                    G = 0;
                }
                if (B2 < 0) {
                    B2 = 0;
                }
                uint16_t c =
                        rgb((uint8_t)(R > 255 ? 255 : R), (uint8_t)(G > 255 ? 255 : G), (uint8_t)(B2 > 255 ? 255 : B2));
                d[xx] = dim ? mix16(c, rgb(20, 20, 20), 9) : c;
            }
        }
    }
}
static void die_face(int x, int y, int s, int v, bool dim, bool red)
{
    const uint16_t face = red ? rgb(40, 40, 46) : rgb(250, 247, 238), pip = red ? rgb(255, 255, 255) : rgb(30, 30, 40);
    ui_rrect(x + 1, y + 2, s, s, s / 5, rgb(0, 0, 0));
    ui_rrect(x, y, s, s, s / 5, face);
    ui_rrect(x + 2, y + 1, s - 4, s / 4, s / 6, red ? rgb(90, 90, 100) : rgb(255, 255, 255));
    static const int8_t P[7][6][2] = {{{0}},
                                      {{2, 2}},
                                      {{1, 1}, {3, 3}},
                                      {{1, 1}, {2, 2}, {3, 3}},
                                      {{1, 1}, {3, 1}, {1, 3}, {3, 3}},
                                      {{1, 1}, {3, 1}, {2, 2}, {1, 3}, {3, 3}},
                                      {{1, 1}, {3, 1}, {1, 2}, {3, 2}, {1, 3}, {3, 3}}};
    for (int k = 0; k < v && k < 6; k++) {
        ui_circle(x + P[v][k][0] * s / 4, y + P[v][k][1] * s / 4, s / 10 + 1, pip, 255);
    }
    if (dim) {
        for (int j = 0; j < s; j++) {
            const int yy = y + j;
            if (yy < 0 || yy >= gfx_h) {
                continue;
            }
            for (int i = 0; i < s; i++) {
                const int xx = x + i;
                if (xx >= 0 && xx < gfx_w) {
                    g_fb[yy * gfx_w + xx] = mix16(g_fb[yy * gfx_w + xx], rgb(20, 20, 20), 9);
                }
            }
        }
    }
}
static void draw_board(void)
{
    if (RND) {
        // (the round: the picture of the whole circle; only the part of each row in the circle)
        const int r = gfx_w / 2;
        for (int j = 0; j < gfx_h; j++) {
            const int dy = j - gfx_h / 2;
            int hw = r * r - dy * dy;
            if (hw <= 0) {
                continue;
            }
            hw = (int)sqrtf((float)hw) + 2;
            const int x0 = gfx_w / 2 - hw < 0 ? 0 : gfx_w / 2 - hw,
                      x1 = gfx_w / 2 + hw > gfx_w ? gfx_w : gfx_w / 2 + hw;
            memcpy(g_fb + j * gfx_w + x0, s_board + j * gfx_w + x0, (size_t)(x1 - x0) * 2);
        }
        return;
    }
    for (int j = 0; j < G_.h; j++) {
        memcpy(g_fb + (OY + j) * gfx_w + OX, s_board + j * G_.w, (size_t)G_.w * 2);
    }
}
// the size of a die (px)
static int die_s(void)
{
    return K == 2 ? 40 : K == 1 ? 18 : K == 3 ? 24 : K == 4 ? 21 : 24;
}
// the position to draw (a slide: the position before it, without the sliding checker)
static void draw_checkers(const bgpos_t *p, int skip_from, int skip_side)
{
    for (int i = 0; i < 24; i++) {
        const int v = p->p[i];
        if (!v) {
            continue;
        }
        const int side = v > 0 ? 0 : 1;
        int n = v > 0 ? v : -v;
        if (i == skip_from && side == skip_side) {
            n--;
        }
        if (D.on && D.moved && i == D.src && side == B.side) {
            n--;  // (the dragged checker)
        }
        if (RB.on && i == RB.src && side == RB.side) {
            n--;  // (the one that goes back)
        }
        const int shown = n;
        for (int j = 0; j < shown; j++) {
            int cx, cy;
            stack_xy(i, j, shown, &cx, &cy);
            checker(side, cx, cy);
        }
        if (shown > 5) {
            int cx, cy;
            stack_xy(i, shown - 1, shown, &cx, &cy);
            char b[4];
            snprintf(b, sizeof b, "%d", shown);
            ui_text_c(&KF_LABEL, cx, cy + 5, b, side ? rgb(255, 255, 255) : rgb(40, 40, 46));
        }
    }
    for (int s = 0; s < 2; s++) {
        int n = p->bar[s] - (skip_from == 24 && skip_side == s) - (HF.on && HF.side == s) -
                (D.on && D.moved && D.src == 24 && B.side == s) - (RB.on && RB.src == 24 && RB.side == s);
        for (int j = 0; j < n; j++) {
            int cx, cy;
            bar_xy(s, j, &cx, &cy);
            checker(s, cx, cy);
        }
        // the tray: one thin slab for each checker that is off
        const int tx = tray_x() + 2, tw = tray_w() - 4, slab = K == 2 || K == 3 ? 6 : K == 4 ? 5 : 4;
        for (int j = 0; j < p->off[s]; j++) {
            const int y = tray_top(s) ? OY + G_.fr + 2 + j * (slab + 1) : OY + G_.h - G_.fr - 2 - slab - j * (slab + 1);
            ui_rrect(tx, y, tw, slab, 2, s ? rgb(44, 44, 50) : rgb(236, 234, 226));
        }
    }
}
static bgstep_t s_next[64], s_all[64], s_all_tmp[64];
static int s_nnext, s_nall;
static int mine_now(void);
// the legal steps of now from all sources (the marks): made again after each change of the position or the dice
static void refresh_all(void)
{
    s_nall = B.g == G_MOVE ? next_steps(s_all, -1) : 0;
}
static bool s_hints = true;  // the panel item HINTS
static void draw_last(void)
{
    // the last turn of the other side, on my turn before my first step: a soft ring where each checker came to, a
    // dot where it left
    if (s_last_side < 0 || !mine_now() || B.side == s_last_side || B.nsteps || B.mode == MODE_TWO) {
        return;
    }
    for (int i = 0; i < s_nlast; i++) {
        const bgstep_t *t = &s_lastmv[i];
        int cx, cy;
        if (t->from < 24) {
            const int c = bg_cnt(&B.pos, t->from, s_last_side);
            stack_xy(t->from, c, c + 1, &cx, &cy);
            ui_circle(cx, cy, G_.ck / 6 + 1, rgb(255, 255, 255), 110);
        }
        if (t->to >= 0) {
            const int c = bg_cnt(&B.pos, t->to, s_last_side);
            if (c) {
                stack_xy(t->to, c - 1, c, &cx, &cy);
                ui_ring(cx, cy, G_.ck / 2, 2, rgb(255, 255, 255), 140);
            }
        }
    }
}
static void draw_marks(void)
{
    if (!mine_now() || B.g != G_MOVE || A.on) {
        return;
    }
    if (D.on && D.moved) {
        return;
    }
    // the selected checker: a gold ring (always)
    if (B.sel >= 0 && !s_hints) {
        int cx, cy;
        if (B.sel == 24) {
            bar_xy(B.side, B.pos.bar[B.side] - 1, &cx, &cy);
        } else {
            const int c = bg_cnt(&B.pos, B.sel, B.side);
            stack_xy(B.sel, c - 1, c, &cx, &cy);
        }
        ui_ring(cx, cy, G_.ck / 2, 3, rgb(255, 225, 90), 255);
    }
    if (!s_hints) {
        return;
    }
    // the sources that can move: a soft ring on their top checker; the selected one: a gold ring; its targets
    for (int i = 0; i < s_nall; i++) {
        bool first = true;
        for (int k = 0; k < i; k++) {
            if (s_all[k].from == s_all[i].from) {
                first = false;
            }
        }
        if (!first) {
            continue;
        }
        int cx, cy;
        if (s_all[i].from == 24) {
            bar_xy(B.side, B.pos.bar[B.side] - 1, &cx, &cy);
        } else {
            const int c = bg_cnt(&B.pos, s_all[i].from, B.side);
            stack_xy(s_all[i].from, c - 1, c, &cx, &cy);
        }
        ui_ring(cx, cy, G_.ck / 2, 3, s_all[i].from == B.sel ? rgb(255, 225, 90) : rgb(110, 235, 130),
                s_all[i].from == B.sel ? 255 : 210);
    }
    if (B.sel < 0) {
        return;
    }
    for (int i = 0; i < s_nnext; i++) {
        const bgstep_t *s = &s_next[i];
        if (s->to < 0) {
            const int ty = tray_top(B.side) ? OY + G_.fr + 4 : OY + G_.h / 2 + 4;
            ui_rrect(tray_x() + 2, ty, tray_w() - 4, G_.h / 2 - G_.fr - 8, 4, rgb(120, 230, 120));
            continue;
        }
        const int c = bg_cnt(&B.pos, s->to, B.side);
        int cx, cy;
        stack_xy(s->to, c, c + 1, &cx, &cy);
        ui_circle(cx, cy, G_.ck / 3, s->hit ? rgb(255, 120, 80) : rgb(120, 230, 120), 230);
    }
}
static void draw_dice(void)
{
    const int s = die_s(), cy = OY + G_.h / 2 - s / 2;
    // (the dice of the side to move on its half: the right half for the one at the bottom)
    const int half_x =
            (B.side == 0) != view_flip() ? OX + G_.fr + 6 * G_.pw + G_.barw + 3 * G_.pw : OX + G_.fr + 3 * G_.pw;
    if (B.g == G_OPEN) {
        if (B.open_d[0]) {
            die_face(OX + G_.fr + 3 * G_.pw - s / 2 + (view_flip() ? 0 : 6 * G_.pw + G_.barw), cy, s, B.open_d[0],
                     false, false);
        }
        if (B.open_d[1]) {
            die_face(OX + G_.fr + 3 * G_.pw - s / 2 + (view_flip() ? 6 * G_.pw + G_.barw : 0), cy, s, B.open_d[1],
                     false, true);
        }
        return;
    }
    if (B.g == G_SHAKE) {
        // the dice in the hand: they jump at the edge of the board while the board shakes
        const float k = s_shake_t * 40;
        for (int i = 0; i < 2; i++) {
            die3d(half_x - s * 0.7f + i * s * 1.4f + sinf(k + i * 2) * 3, OY + G_.h / 2 + cosf(k * 1.3f + i) * 4, s,
                  sinf(k * 0.7f + i) * 0.6f, 6, 1 + (int)(k + i * 3) % 6, false, B.side == 1);
        }
        return;
    }
    if (B.g != G_DICE && B.g != G_MOVE && B.g != G_DONE) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        const die_t *e = &s_die[i];
        bool dim = false;
        if (B.g != G_DICE) {
            if (B.ndice == 4) {
                int u = 0;
                for (int k = 0; k < 4; k++) {
                    u += B.used[k];
                }
                dim = i == 0 ? u >= 3 : u >= 4;
            } else {
                dim = B.used[i];
            }
        }
        die3d(e->x, e->y, s, e->a, e->z, B.g == G_DICE && !e->rest ? e->face : B.d[i], dim, B.side == 1);
    }
}
// the two ends of the slide on the screen
static void slide_ends(int *x0, int *y0, int *x1, int *y1)
{
    if (A.fx >= 0) {
        *x0 = A.fx;
        *y0 = A.fy;
    } else if (A.s.from == 24) {
        bar_xy(A.side, B.pos.bar[A.side] - 1, x0, y0);
    } else {
        const int c = bg_cnt(&B.pos, A.s.from, A.side);
        stack_xy(A.s.from, c - 1, c, x0, y0);
    }
    if (A.s.to < 0) {
        *x1 = tray_x() + tray_w() / 2;
        *y1 = tray_top(A.side) ? OY + G_.fr + 10 : OY + G_.h - G_.fr - 10;
    } else {
        const int c = bg_cnt(&B.pos, A.s.to, A.side);
        stack_xy(A.s.to, c, c + 1, x1, y1);
    }
}
static void draw_slide(void)
{
    if (!A.on) {
        return;
    }
    int x0, y0, x1, y1;
    slide_ends(&x0, &y0, &x1, &y1);
    const float f = A.t / A.len, e = f * f * (3 - 2 * f);
    const float dist = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    const int lift = (int)(sinf(f * 3.14159f) * (A.fx >= 0 ? 3 : 4 + dist * 0.06f));
    checker_l(A.side, (int)(x0 + (x1 - x0) * e), (int)(y0 + (y1 - y0) * e) - lift, lift);
}
// the hit checker that flies to the bar; the dragged checker; a checker that goes back after a bad drop
static void draw_flights(void)
{
    if (HF.on) {
        int bx, by;
        bar_xy(HF.side, B.pos.bar[HF.side] - 1, &bx, &by);
        const float f = HF.t / 0.3f, e = f * f * (3 - 2 * f);
        const int lift = (int)(sinf(f * 3.14159f) * 8);
        checker_l(HF.side, (int)(HF.x0 + (bx - HF.x0) * e), (int)(HF.y0 + (by - HF.y0) * e) - lift, lift);
    }
    if (RB.on) {
        int x1, y1;
        if (RB.src == 24) {
            bar_xy(RB.side, B.pos.bar[RB.side] - 1, &x1, &y1);
        } else {
            const int c = bg_cnt(&B.pos, RB.src, RB.side);
            stack_xy(RB.src, c - 1, c, &x1, &y1);
        }
        const float f = RB.t / 0.18f, e = f * f * (3 - 2 * f);
        checker_l(RB.side, (int)(RB.x0 + (x1 - RB.x0) * e), (int)(RB.y0 + (y1 - RB.y0) * e), (int)((1 - e) * 4));
    }
    if (D.on && D.moved) {
        checker_l(B.side, D.x, D.y - G_.ck * 2 / 5, 5);
    }
}
// the buttons
enum {
    BT_CPU,
    BT_TWO,
    BT_INVITE,
    BT_JOIN,
    BT_CANCEL,
    BT_ROLL,
    BT_UNDO
};
typedef struct {
    int16_t x, y, w, h;
} rect_t;
static rect_t s_btn[8];
static int s_btn_id[8], s_nbtn;
static void button(int id, int x, int y, int w, int h, const char *label, uint16_t col)
{
    ui_rrect(x, y, w, h, 10, col);
    ui_rrect(x + 2, y + 2, w - 4, 4, 2, rgb(255, 255, 255));
    const kfont_t *f = ui_text_w(&KF_TEXT, label) <= w - 10 ? &KF_TEXT : &KF_LABEL;
    ui_text_c(f, x + w / 2, y + h / 2 + (f == &KF_TEXT ? 7 : 5), label, rgb(255, 255, 255));
    if (s_nbtn < 8) {
        s_btn[s_nbtn] = (rect_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
        s_btn_id[s_nbtn++] = id;
    }
}
static int button_at(int x, int y)
{
    for (int i = 0; i < s_nbtn; i++) {
        const rect_t r = s_btn[i];
        if (x >= r.x - 5 && x < r.x + r.w + 5 && y >= r.y - 5 && y < r.y + r.h + 5) {
            return s_btn_id[i];
        }
    }
    return -1;
}
static uint8_t s_inv[3][6];
static int s_ninv;
static bool s_shake_on = true;  // the panel item SHAKE TO ROLL
// the pip count of a side: the steps that its checkers still need to bear off
static int pips(int side)
{
    int n = B.pos.bar[side] * 25;
    for (int i = 0; i < 24; i++) {
        n += bg_cnt(&B.pos, i, side) * (side ? 24 - i : i + 1);
    }
    return n;
}
static const char *side_name(int s)
{
    return s ? T("BLACK", "השחורים") : T("WHITE", "הלבנים");
}
static void draw_panel(void)
{
    // the 3.49: a column at the left (0..OX); a narrow frame: a bar under the board
    char b[64];
    const char *line = "";
    if (B.g == G_OVER) {
        snprintf(b, sizeof b, "%s",
                 B.mode == MODE_TWO ? (B.winner ? T("BLACK WINS", "השחורים ניצחו") : T("WHITE WINS", "הלבנים ניצחו"))
                 : B.winner == B.my_side ? T("YOU WIN!", "ניצחתם!")
                                         : T("YOU LOSE", "הפסדתם"));
        line = b;
    } else if (B.note_t > 0) {
        line = B.note;
    } else if (B.g == G_OPEN) {
        line = T("THE FIRST ROLL", "הגלגול הראשון");
    } else if (B.mode == MODE_TWO) {
        snprintf(b, sizeof b, "%s: %s", side_name(B.side), B.g == G_ROLL ? T("ROLL", "גלגול") : T("MOVE", "מהלך"));
        line = b;
    } else if (mine_now()) {
        line = B.g == G_ROLL || B.g == G_SHAKE ? T("YOUR ROLL", "תורכם לגלגל") : T("YOUR MOVE", "התור שלכם");
    } else {
        line = B.mode == MODE_CPU ? T("THE COMPUTER", "המחשב") : T("YOUR FRIEND", "החבר");
    }
    const bool show_roll = mine_now() && B.g == G_ROLL;
    const bool show_undo = mine_now() && B.g == G_MOVE && B.nsteps > 0;
    char sc[32];
    snprintf(sc, sizeof sc, "%d - %d", B.score[B.mode == MODE_TWO ? 0 : B.my_side],
             B.score[B.mode == MODE_TWO ? 1 : !B.my_side]);
    if (RND) {
        // the round: the status and the score on the top arc, ROLL / UNDO (or the pips) on the bottom arc
        const int cx = gfx_w / 2, u = gfx_w;  // (u: the sizes scale with the circle)
        const uint16_t lc = B.g == G_OVER ? rgb(255, 214, 90) : rgb(255, 236, 190);
        const int ly = OY - u * 44 / 466, aw = u * 290 / 466;
        ui_text_c(ui_text_w(&KF_BIG, line) <= aw ? &KF_BIG : &KF_TEXT, cx, ly, line, lc);
        const int sy = OY - u * 12 / 466;
        ui_text_c(&KF_TEXT, cx, sy, sc, rgb(240, 240, 245));
        const int sw = ui_text_w(&KF_TEXT, sc) / 2 + u * 14 / 466;
        // (the names at the two sides of the score; ui_text_c only: the ABI of the app files has no ui_text)
        const char *nl = B.mode == MODE_TWO ? T("WHITE", "לבנים") : T("YOU", "אתם");
        const char *nr = B.mode == MODE_TWO ? T("BLACK", "שחורים") : T("THEY", "הם");
        ui_text_c(&KF_NAME, cx - sw - ui_text_w(&KF_NAME, nl) / 2, sy, nl, rgb(170, 180, 200));
        ui_text_c(&KF_NAME, cx + sw + ui_text_w(&KF_NAME, nr) / 2, sy, nr, rgb(170, 180, 200));
        const int by = OY + G_.h + u * 10 / 466, bw = u * 210 / 466, bh = u * 52 / 466;
        if (show_roll) {
            button(BT_ROLL, cx - bw / 2, by, bw, bh,
                   s_shake_on ? T("ROLL / SHAKE", "גלגול / ניעור") : T("ROLL", "גלגול"), rgb(70, 140, 80));
        }
        if (show_undo) {
            button(BT_UNDO, cx - bw / 2, by, bw, bh, T("UNDO", "ביטול"), rgb(90, 100, 120));
        }
        if (B.g == G_OVER) {
            if (B.last_sc > 1) {
                ui_text_c(&KF_TEXT, cx, by + u * 22 / 466,
                          B.last_sc == 3 ? T("x3 POINTS", "3 נקודות") : T("x2 POINTS", "2 נקודות"), rgb(255, 160, 90));
            }
            if (B.t > 1.0f) {
                ui_text_c(&KF_TEXT, cx, by + u * 48 / 466, T("TAP: AGAIN", "הקישו: שוב"), rgb(200, 205, 215));
            }
        } else if (!show_roll && !show_undo && B.g >= G_OPEN) {
            char pp[48];
            snprintf(pp, sizeof pp, "%s  %d - %d", T("PIPS", "צעדים"), pips(B.mode == MODE_TWO ? 0 : B.my_side),
                     pips(B.mode == MODE_TWO ? 1 : !B.my_side));
            ui_text_c(&KF_TEXT, cx, by + u * 26 / 466, pp, rgb(190, 196, 210));
        }
    } else if (K == 2) {
        // the Tab5: the column at the left (0..OX, 112 px): the status, the score, a big ROLL or UNDO
        const int cx = OX / 2;
        const uint16_t lc = B.g == G_OVER ? rgb(255, 214, 90) : rgb(255, 236, 190);
        if (ui_text_w(&KF_TEXT, line) <= OX - 8) {
            ui_text_c(&KF_TEXT, cx, 34, line, lc);
        } else {
            char two[64];
            snprintf(two, sizeof two, "%s", line);
            const int len = (int)strlen(two);
            int cut = -1;
            for (int i = 0; i < len; i++) {
                if (two[i] == ' ' && (cut < 0 || abs(i - len / 2) < abs(cut - len / 2))) {
                    cut = i;
                }
            }
            const kfont_t *f = &KF_TEXT;
            if (cut > 0) {
                two[cut] = 0;
                if (ui_text_w(f, two) > OX - 8 || ui_text_w(f, two + cut + 1) > OX - 8) {
                    f = &KF_LABEL;
                }
                ui_text_c(f, cx, 24, two, lc);
                ui_text_c(f, cx, 46, two + cut + 1, lc);
            } else {
                ui_text_c(&KF_LABEL, cx, 34, line, lc);
            }
        }
        if (B.g == G_OVER && B.last_sc > 1) {
            ui_text_c(&KF_LABEL, cx, 72, B.last_sc == 3 ? T("x3 POINTS", "3 נקודות") : T("x2 POINTS", "2 נקודות"),
                      rgb(255, 160, 90));
        }
        ui_text_c(&KF_LABEL, cx, 104,
                  B.mode == MODE_TWO ? T("WHITE - BLACK", "לבנים - שחורים") : T("YOU - THEY", "אתם - הם"),
                  rgb(170, 180, 200));
        ui_text_c(&KF_BIG, cx, 136, sc, rgb(240, 240, 245));
        if (show_roll) {
            button(BT_ROLL, 6, 172, OX - 12, 120, T("ROLL", "גלגול"), rgb(70, 140, 80));
        }
        if (show_undo) {
            button(BT_UNDO, 6, 172, OX - 12, 120, T("UNDO", "ביטול"), rgb(90, 100, 120));
        }
        if (B.g == G_OVER && B.t > 1.0f) {
            ui_text_c(&KF_LABEL, cx, 320, T("TAP: AGAIN", "הקישו: שוב"), rgb(200, 205, 215));
        }
    } else if (!K) {
        const int cx = OX / 2;
        const uint16_t lc = B.g == G_OVER ? rgb(255, 214, 90) : rgb(255, 236, 190);
        if (ui_text_w(&KF_TEXT, line) <= OX - 6) {
            ui_text_c(&KF_TEXT, cx, 20, line, lc);
        } else if (ui_text_w(&KF_LABEL, line) <= OX - 6) {
            ui_text_c(&KF_LABEL, cx, 18, line, lc);
        } else {
            // (two lines: cut at the space nearest the middle)
            char two[64];
            snprintf(two, sizeof two, "%s", line);
            const int len = (int)strlen(two);
            int cut = -1;
            for (int i = 0; i < len; i++) {
                if (two[i] == ' ' && (cut < 0 || abs(i - len / 2) < abs(cut - len / 2))) {
                    cut = i;
                }
            }
            if (cut > 0) {
                two[cut] = 0;
                ui_text_c(&KF_LABEL, cx, 12, two, lc);
                ui_text_c(&KF_LABEL, cx, 26, two + cut + 1, lc);
            } else {
                ui_text_c(&KF_LABEL, cx, 18, line, lc);
            }
        }
        if (B.g == G_OVER && B.last_sc > 1) {
            ui_text_c(&KF_LABEL, cx, 38, B.last_sc == 3 ? T("x3 POINTS", "3 נקודות") : T("x2 POINTS", "2 נקודות"),
                      rgb(255, 160, 90));
        }
        ui_text_c(&KF_LABEL, cx, 52,
                  B.mode == MODE_TWO ? T("WHITE - BLACK", "לבנים - שחורים") : T("YOU - THEY", "אתם - הם"),
                  rgb(170, 180, 200));
        ui_text_c(&KF_TEXT, cx, 70, sc, rgb(240, 240, 245));
        if (!show_roll && !show_undo && B.g >= G_OPEN && B.g != G_OVER) {
            char pp[32];
            snprintf(pp, sizeof pp, "%d - %d", pips(B.mode == MODE_TWO ? 0 : B.my_side),
                     pips(B.mode == MODE_TWO ? 1 : !B.my_side));
            ui_text_c(&KF_LABEL, cx, 92, T("PIPS", "צעדים"), rgb(150, 160, 180));
            ui_text_c(&KF_LABEL, cx, 108, pp, rgb(200, 205, 215));
        }
        if (show_roll) {
            button(BT_ROLL, 4, 82, OX - 8, 50, s_shake_on ? T("ROLL / SHAKE", "גלגול / ניעור") : T("ROLL", "גלגול"),
                   rgb(70, 140, 80));
        }
        if (show_undo) {
            button(BT_UNDO, 4, 118, OX - 8, 46, T("UNDO", "ביטול"), rgb(90, 100, 120));
        }
        if (B.g == G_OVER && B.t > 1.0f) {
            ui_text_c(&KF_LABEL, cx, 160, T("TAP: AGAIN", "הקישו: שוב"), rgb(200, 205, 215));
        }
    } else {
        const int y0 = OY + G_.h;
        ui_text_c(B.g == G_OVER ? &KF_TEXT : &KF_LABEL, 70, y0 + 20, line,
                  B.g == G_OVER ? rgb(255, 214, 90) : rgb(255, 236, 190));
        ui_text_c(&KF_LABEL, 70, y0 + 38, sc, rgb(200, 205, 215));
        if (B.g == G_OVER && B.last_sc > 1) {
            ui_text_c(&KF_LABEL, 160, y0 + 38, B.last_sc == 3 ? T("x3", "x3") : T("x2", "x2"), rgb(255, 160, 90));
        }
        if (show_roll) {
            button(BT_ROLL, gfx_w - 124, y0 + 4, 118, gfx_h - y0 - 8, T("ROLL", "גלגול"), rgb(70, 140, 80));
        }
        if (show_undo) {
            button(BT_UNDO, gfx_w - 124, y0 + 4, 118, gfx_h - y0 - 8, T("UNDO", "ביטול"), rgb(90, 100, 120));
        }
        if (B.g == G_OVER && B.t > 1.0f) {
            ui_text_c(&KF_LABEL, gfx_w - 66, y0 + 26, T("TAP: AGAIN", "הקישו: שוב"), rgb(200, 205, 215));
        }
    }
}
static void draw_title(void)
{
    for (int j = 0; j < gfx_h; j++) {
        for (int i = 0; i < gfx_w; i++) {
            g_fb[j * gfx_w + i] = mix16(g_fb[j * gfx_w + i], rgb(0, 0, 0), 9);
        }
    }
    int pw = K == 2 ? 360 : K ? gfx_w - 40 : 300, ph = K == 2 ? 240 : K ? 200 : 164;
    if (RND) {
        pw = gfx_w * 300 / 466;
        ph = gfx_w * 252 / 466;
    }  // (the round: a box in the middle of the circle)
    const int px = (gfx_w - pw) / 2, py = (gfx_h - ph) / 2;
    ui_rrect(px, py, pw, ph, 14, rgb(22, 30, 26));
    ui_rrect(px + 3, py + 3, pw - 6, ph - 6, 12, rgb(34, 56, 44));
    const int cx = gfx_w / 2;
    if (RND) {
        if (B.note_t > 0) {
            ui_text_c(&KF_TEXT, cx, py + gfx_w * 42 / 466, B.note, rgb(255, 180, 150));
        } else {
            ui_text_c(&KF_TITLE, cx, py + gfx_w * 48 / 466, T("BACKGAMMON", "שש בש"), rgb(255, 228, 170));
        }
    } else if (B.note_t > 0) {
        ui_text_c(&KF_TEXT, cx, py + 30, B.note, rgb(255, 180, 150));
    } else {
        ui_text_c(&KF_BIG, cx, py + 32, T("BACKGAMMON", "שש בש"), rgb(255, 228, 170));
    }
    const int bw = pw - 40,
              bh = RND      ? gfx_w * 50 / 466
                   : K == 2 ? 50
                   : K      ? 38
                            : 34,
              gap = RND      ? gfx_w * 10 / 466
                    : K == 2 ? 10
                    : K      ? 8
                             : 6;
    int by = py + (RND ? gfx_w * 66 / 466 : K == 2 ? 52 : 44);
    button(BT_CPU, px + 20, by, bw, bh, T("VS COMPUTER", "נגד המחשב"), rgb(70, 140, 80));
    by += bh + gap;
    button(BT_TWO, px + 20, by, bw, bh, T("2 PLAYERS", "2 שחקנים"), rgb(150, 100, 50));
    by += bh + gap;
    if (s_ninv) {
        button(BT_JOIN, px + 20, by, bw, bh, T("JOIN YOUR FRIEND", "הצטרפו לחבר"), rgb(60, 110, 190));
    } else {
        button(BT_INVITE, px + 20, by, bw, bh, T("2 BOARDS", "2 לוחות"), rgb(60, 110, 190));
    }
}
static void render(int64_t now)
{
    (void)now;
    s_nbtn = 0;
    gfx_fill(rgb(20, 24, 22));
    if (!s_board) {
        ui_text_c(&KF_LABEL, gfx_w / 2, gfx_h / 2, "NO MEMORY", rgb(255, 200, 180));
        return;
    }
    draw_board();
    draw_checkers(&B.pos, A.on ? A.s.from : -9, A.on ? A.side : -1);
    if (A.on && A.s.hit) { /* (the hit checker stays until the slide ends) */
    }
    draw_last();
    draw_dice();
    draw_marks();
    draw_slide();
    draw_flights();
    if (B.g == G_TITLE) {
        draw_title();
        return;
    }
    if (B.g == G_WAIT) {
        for (int j = 0; j < gfx_h; j++) {
            for (int i = 0; i < gfx_w; i++) {
                g_fb[j * gfx_w + i] = mix16(g_fb[j * gfx_w + i], rgb(0, 0, 0), 10);
            }
        }
        ui_text_c(&KF_TEXT, gfx_w / 2, gfx_h / 2 - 30, T("WAITING FOR A FRIEND", "מחכים לחבר"), rgb(255, 236, 190));
        ui_text_c(&KF_LABEL, gfx_w / 2, gfx_h / 2 - 10,
                  T("ON THE OTHER BOARD: BACKGAMMON > JOIN", "בלוח השני: שש בש > הצטרפו"), rgb(200, 205, 215));
        button(BT_CANCEL, gfx_w / 2 - 70, gfx_h / 2 + 8, 140, 38, T("CANCEL", "ביטול"), rgb(150, 60, 50));
        return;
    }
    draw_panel();
}

// ---------------------------------------------------------------- the turns
static int mine_now(void)
{
    if (B.g < G_OPEN || B.g == G_OVER || A.on || s_nq) {
        return 0;
    }
    if (B.mode == MODE_TWO) {
        return 1;
    }
    return B.side == B.my_side;
}
static void net_send(const uint8_t *b, int n)
{
    if (B.mode == MODE_NET) {
        duel_send(b, n);
    }
}
enum {
    NM_OPEN = 1,
    NM_ROLL,
    NM_TURN
};
static uint32_t s_seed;
static int die(void)
{
    s_seed = (s_seed * 1664525u + 1013904223u) ^ esp_random();
    return (int)((s_seed >> 16) % 6) + 1;
}
static void note(const char *t, float secs)
{
    snprintf(B.note, sizeof B.note, "%s", t);
    B.note_t = secs;
}
static void begin_moves(void)
{
    B.ndice = B.d[0] == B.d[1] ? 4 : 2;
    memset(B.used, 0, sizeof B.used);
    B.nsteps = 0;
    B.sel = -1;
    B.turn0 = B.pos;
    B.most = bg_turns(&B.pos, B.side, B.d[0], B.d[1], s_tt[0], s_ee[0], BG_MAXT) ? s_tt[0][0].n : 0;
    B.g = G_MOVE;
    B.t = 0;
    // (2 BOARDS: the turn of the friend ends only with its message, also a turn with no move)
    if (!B.most && !(B.mode == MODE_NET && B.side != B.my_side)) {
        note(T("NO MOVE", "אין מהלך"), 1.4f);
        B.g = G_DONE;
        B.t = 0;
    }
    refresh_all();
}
// the throw: the dice come in from the edge of the half of the side to move (low, fast), fall, bounce, slide, settle
static void throw_dice(void)
{
    const int s = die_s();
    const bool right = (B.side == 0) != view_flip();
    const float hx0 = right ? OX + G_.fr + 6 * G_.pw + G_.barw : OX + G_.fr, hw = 6 * G_.pw;
    for (int i = 0; i < 2; i++) {
        die_t *e = &s_die[i];
        e->x = right ? hx0 + hw - s * 0.6f : hx0 + s * 0.6f;
        e->y = OY + G_.h * (0.62f + i * 0.12f);
        e->vx = (right ? -1 : 1) * (hw * (1.5f + (die() - 1) * 0.12f) + i * 20);
        e->vy = -(G_.h * (0.5f + (die() - 1) * 0.06f)) + i * 30;
        e->z = 26 + i * 4;
        e->vz = 10;
        e->a = (float)(die() - 1);
        e->va = (right ? -1 : 1) * (9 + die());
        e->face = die();
        e->rest = false;
    }
}
static void roll(int d1, int d2, bool send)
{
    B.d[0] = d1;
    B.d[1] = d2;
    B.g = G_DICE;
    B.t = 0;
    throw_dice();
    if (send) {
        const uint8_t m[3] = {NM_ROLL, (uint8_t)d1, (uint8_t)d2};
        net_send(m, 3);
    }
}
static volatile int s_ai;  // 0 idle, 1 thinks, 2 the turn is ready
static bgturn_t s_ai_turn;
static void ai_think_now(void)
{
    uint32_t seed = esp_random();
    s_bg_tt[0] = s_tt[0];
    s_bg_ee[0] = s_ee[0];
    s_bg_tt[1] = s_tt[1];
    s_bg_ee[1] = s_ee[1];
    if (!bg_pick(&B.pos, B.side, B.d[0], B.d[1], B.level, &seed, &s_ai_turn)) {
        s_ai_turn.n = 0;
    }
    s_ai = 2;
}
#ifdef ESP_PLATFORM
static TaskHandle_t s_ai_task;
static volatile bool s_ai_go, s_ai_quit;
static void ai_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_ai_quit) {
            break;
        }
        if (s_ai_go) {
            s_ai_go = false;
            ai_think_now();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_ai_quit = false;
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
static void ai_start(void)
{
    s_ai = 1;
    s_ai_go = true;
}
#else
static void ai_start(void)
{
    s_ai = 1;
    ai_think_now();
}
#endif
static void game_over_check(void)
{
    int sc;
    const int w = bg_winner(&B.pos, &sc);
    if (w < 0) {
        return;
    }
    B.g = G_OVER;
    B.t = 0;
    B.winner = w;
    B.last_sc = sc;
    B.score[w] += sc;
    snd_end(B.mode == MODE_TWO || w == B.my_side);
}
// one step: the slide, then the position
static void step_go_at(const bgstep_t *s, int side, int sx, int sy)
{
    A.on = true;
    A.s = *s;
    A.side = side;
    A.t = 0;
    A.fx = sx;
    A.fy = sy;
    int x0, y0, x1, y1;
    slide_ends(&x0, &y0, &x1, &y1);
    const float dist = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    A.len = sx >= 0 ? 0.12f : 0.2f + dist / (float)G_.w * 0.35f;
    if (sx < 0) {
        fx(FX_SLIDE, 45, 0.1f);
    }
}
static void step_go(const bgstep_t *s, int side)
{
    step_go_at(s, side, -1, -1);
}
static void step_land(void)
{
    A.on = false;
    // (the checker that is hit: it flies from the point to the bar)
    if (A.s.hit && A.s.to >= 0) {
        int hx, hy;
        stack_xy(A.s.to, 0, 1, &hx, &hy);
        HF.on = true;
        HF.side = !A.side;
        HF.t = 0;
        HF.x0 = hx;
        HF.y0 = hy;
    }
    const bool stack = A.s.to >= 0 && bg_cnt(&B.pos, A.s.to, A.side) > 0;
    if (B.mode != MODE_TWO && A.side != B.my_side && s_nlast < 4) {
        s_lastmv[s_nlast++] = A.s;  // (the marks of its last move)
    }
    bg_apply(&B.pos, A.side, &A.s);
    if (A.s.hit) {
        snd_hit();
    } else if (A.s.to < 0) {
        snd_off();
    } else {
        snd_place(stack);
    }
    game_over_check();
    refresh_all();
}
// my step (from the touch): mark the die, slide
static void my_step_at(const bgstep_t *s, int sx, int sy)
{
    for (int i = 0; i < B.ndice; i++) {
        if (!B.used[i] && B.d[i < 2 ? i : 0] == s->die) {
            B.used[i] = 1;
            break;
        }
    }
    B.steps[B.nsteps++] = *s;
    B.sel = -1;
    step_go_at(s, B.side, sx, sy);
}
static void my_step(const bgstep_t *s)
{
    my_step_at(s, -1, -1);
}
static void end_turn(void)
{
    if (B.mode == MODE_NET && B.side == B.my_side) {
        uint8_t m[2 + 4 * 3];
        m[0] = NM_TURN;
        m[1] = (uint8_t)B.nsteps;
        for (int i = 0; i < B.nsteps; i++) {
            m[2 + 3 * i] = (uint8_t)B.steps[i].from;
            m[3 + 3 * i] = (uint8_t)B.steps[i].to;
            m[4 + 3 * i] = B.steps[i].die;
        }
        net_send(m, 2 + 3 * B.nsteps);
    }
    B.side = !B.side;
    if (B.side != B.my_side) {
        s_nlast = 0;  // (the other side starts a turn: its last move is new)
    }
    s_last_side = !B.my_side;
    B.g = G_ROLL;
    B.t = 0;
}
static void game_new(int mode)
{
    // (the score is of one session of one mode)
    if (mode != B.mode || B.g == G_TITLE || B.g == G_WAIT) {
        B.score[0] = B.score[1] = 0;
    }
    B.mode = mode;
    bg_start(&B.pos);
    if (mode == MODE_NET) {
        B.my_side = duel_host() ? 0 : 1;
    } else if (mode == MODE_TWO) {
        B.my_side = 0;
    } else {
        B.my_side = store_get("bg_side", 0) ? 1 : 0;
    }
    B.g = G_OPEN;
    B.t = 0;
    B.open_d[0] = B.open_d[1] = 0;
    s_nq = 0;
    A.on = false;
    s_ai = 0;
    B.note_t = 0;
    HF.on = D.on = RB.on = false;
    s_last_side = -1;
    s_nlast = 0;
}
// the opening roll: one die each (again when they are equal); the higher one starts with these two dice
static void opening(int a, int b, bool send)
{
    B.open_d[0] = a;
    B.open_d[1] = b;
    B.t = 0;
    if (send) {
        const uint8_t m[3] = {NM_OPEN, (uint8_t)a, (uint8_t)b};
        net_send(m, 3);
    }
    snd_land();
}
// a message of the friend: true = done with it (used or old), false = keep it until this board is in that state
static bool net_msg(const uint8_t *m, int n)
{
    if (m[0] == NM_OPEN && n >= 3) {
        if (B.g == G_OVER) {
            game_new(MODE_NET);  // (the friend began the next game first)
        }
        if (B.g != G_OPEN) {
            return false;
        }
        opening(m[1], m[2], false);
        return true;
    }
    if (m[0] == NM_ROLL && n >= 3) {
        if (B.g != G_ROLL || B.side == B.my_side) {
            return false;
        }
        roll(m[1], m[2], false);
        return true;
    }
    if (m[0] == NM_TURN && n >= 2) {
        if (B.g != G_MOVE || B.side == B.my_side || s_nq || A.on) {
            return false;
        }
        // (each step is checked again at its time: bg_step_ok gives the hit)
        for (int i = 0; i < m[1] && i < 4 && 4 + 3 * i < n; i++) {
            s_queue[s_nq].from = (int8_t)m[2 + 3 * i];
            s_queue[s_nq].to = (int8_t)m[3 + 3 * i];
            s_queue[s_nq].die = m[4 + 3 * i];
            s_queue[s_nq].hit = 0;
            s_nq++;
        }
        s_q_side = B.side;
        if (!s_nq) {
            note(T("NO MOVE", "אין מהלך"), 1.4f);
            B.g = G_DONE;
            B.t = 0;
        }
        return true;
    }
    return true;
}
static uint8_t s_pm[64];  // the message that waits (the order of the messages stays)
static int s_pn;
static void net_poll(int64_t now)
{
    duel_poll(now);
    const int ds = duel_state();
    if (B.g == G_WAIT && ds == DUEL_ON) {
        s_pn = 0;
        game_new(MODE_NET);
        return;
    }
    if (B.mode == MODE_NET && B.g != G_TITLE && B.g != G_OVER && ds == DUEL_GONE) {
        B.g = G_TITLE;
        note(T("YOUR FRIEND LEFT", "החבר יצא"), 4);
        return;
    }
    for (int k = 0; k < 8; k++) {
        if (!s_pn) {
            int n = sizeof s_pm;
            if (!duel_recv(s_pm, &n) || n < 1) {
                break;
            }
            s_pn = n;
        }
        if (!net_msg(s_pm, s_pn)) {
            break;
        }
        s_pn = 0;
    }
}

// ---------------------------------------------------------------- the touch
static int point_at(int x, int y)
{
    // the bar, the tray, a point column (the top or the bottom half)
    if (x >= tray_x() && x < OX + G_.w) {
        return -1;  // (the tray: -1 = off)
    }
    if (abs(x - bar_x()) <= G_.barw / 2 + 2) {
        return 24;
    }
    for (int k = 0; k < 12; k++) {
        const int cx = col_x(k);
        if (x < cx || x >= cx + G_.pw) {
            continue;
        }
        const bool top = y < OY + G_.h / 2;
        int v = top ? 12 + k : 11 - k;
        return view_flip() ? 23 - v : v;
    }
    return -2;
}
// the legal target of the selected source that is nearest to the finger (the point column, or the tray); -2: none
// near. (A fat finger: the nearest target within 1.3 columns of the finger, on the half of the board under it.)
static int drop_target(int x, int y)
{
    int best = -2;
    float bd = 1e9f;
    for (int i = 0; i < s_nnext; i++) {
        const bgstep_t *t = &s_next[i];
        float d;
        if (t->to < 0) {
            if (x < tray_x() - G_.pw / 2) {
                continue;
            }
            d = 0;
        } else {
            int k;
            bool top;
            col_of(t->to, &k, &top);
            if (top != (y < OY + G_.h / 2)) {
                continue;
            }
            d = fabsf((float)(x - (col_x(k) + G_.pw / 2)));
            if (d > G_.pw * 1.3f) {
                continue;
            }
        }
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}
static void tap_board(int x, int y)
{
    if (B.g == G_ROLL && mine_now()) {
        roll(die(), die(), true);
        return;
    }
    if (B.g != G_MOVE || !mine_now()) {
        return;
    }
    const int pt = point_at(x, y);
    if (pt == -2 || (pt == -1 && B.sel < 0)) {
        B.sel = -1;
        return;
    }
    if (B.sel >= 0) {
        // a target of the selected source
        for (int i = 0; i < s_nnext; i++) {
            if (s_next[i].to == pt || (pt == -1 && s_next[i].to < 0)) {
                my_step(&s_next[i]);
                return;
            }
        }
        if (pt != B.sel && (pt < 0 || pt >= 24 || !bg_cnt(&B.pos, pt, B.side) || !next_steps(s_all_tmp, pt))) {
            // (the round: the points are 2.4-2.7 mm wide; a fat finger next to a legal target gets it, as a drop)
            const int t = RND ? drop_target(x, y) : -1;
            if (t >= 0) {
                const bgstep_t st = s_next[t];
                my_step(&st);
                return;
            }
            snd_bad();
            return;
        }
    }
    // a source: a point with my checker that can move (the bar first)
    bgstep_t all[64];
    const int src = B.pos.bar[B.side] ? 24 : pt;
    if (src < 0) {
        B.sel = -1;
        return;
    }
    s_nnext = next_steps(all, src);
    if (s_nnext) {
        memcpy(s_next, all, sizeof(bgstep_t) * (size_t)s_nnext);
        B.sel = src;
        snd_click();
        // (one target only: the step goes at once)
        bool one = true;
        for (int i = 1; i < s_nnext; i++) {
            if (s_next[i].to != s_next[0].to) {
                one = false;
            }
        }
        if (one && src == pt && s_hints) {
            my_step(&s_next[0]);
            return;
        }
        return;
    }
    if (pt >= 0 && pt < 24 && bg_cnt(&B.pos, pt, B.side)) {
        snd_bad();
    }
    B.sel = -1;
}
static void undo(void)
{
    if (A.on || !B.nsteps) {
        return;
    }
    B.pos = B.turn0;
    memset(B.used, 0, sizeof B.used);
    B.nsteps = 0;
    B.sel = -1;
    refresh_all();
    snd_click();
}
static void do_button(int id)
{
    switch (id) {
    case BT_CPU:
        game_new(MODE_CPU);
        snd_click();
        break;
    case BT_TWO:
        game_new(MODE_TWO);
        snd_click();
        break;
    case BT_INVITE:
        B.mode = MODE_NET;
        s_pn = 0;
        duel_invite();
        B.g = G_WAIT;
        snd_click();
        break;
    case BT_JOIN:
        if (s_ninv) {
            B.mode = MODE_NET;
            s_pn = 0;
            duel_join(s_inv[0]);
            B.g = G_WAIT;
            snd_click();
        }
        break;
    case BT_CANCEL:
        duel_stop();
        B.g = G_TITLE;
        snd_click();
        break;
    case BT_ROLL:
        if (B.g == G_ROLL && mine_now()) {
            roll(die(), die(), true);
        }
        break;
    case BT_UNDO:
        undo();
        break;
    }
}
// the drag: a finger down on a checker that can move picks it up after a move of 6 px; the drop on a legal target
// moves it (the nearest target to the finger); a drop elsewhere sends it back
static void drag_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        D.on = false;
        if (B.g != G_MOVE || !mine_now() || ev->x < OX || ev->y < OY || ev->y >= OY + G_.h) {
            return;
        }
        const int pt = point_at(ev->x, ev->y);
        const int src = B.pos.bar[B.side] ? 24 : pt;
        if (src < 0 || (src < 24 && !bg_cnt(&B.pos, src, B.side))) {
            return;
        }
        if (B.pos.bar[B.side] && pt != 24 && abs(ev->x - bar_x()) > G_.barw * 2) {
            return;
        }
        bgstep_t all[64];
        const int n = next_steps(all, src);
        if (!n) {
            return;
        }
        D.on = true;
        D.moved = false;
        D.src = src;
        D.x = D.x0 = ev->x;
        D.y = D.y0 = ev->y;
        D.snd_t = 0;
        return;
    }
    if (!D.on) {
        return;
    }
    if (ev->kind == IN_MOVE) {
        D.x = ev->x;
        D.y = ev->y;
        if (!D.moved && abs(D.x - D.x0) + abs(D.y - D.y0) > 6) {
            D.moved = true;
            input_tap_cancel();
            s_nnext = next_steps(s_next, D.src);
            B.sel = D.src;
            fx(FX_SLIDE, 50, 0.1f);
        }
        return;
    }
    if (ev->kind == IN_UP) {
        const bool moved = D.moved;
        D.on = false;
        if (!moved) {
            return;
        }
        const int t = drop_target(ev->x, ev->y);
        const int lx = ev->x, ly = ev->y - G_.ck * 2 / 5;
        if (t >= 0) {
            const bgstep_t st = s_next[t];
            my_step_at(&st, lx, ly);
            return;
        }
        // (no target: back to its point)
        RB.on = true;
        RB.side = B.side;
        RB.src = D.src;
        RB.t = 0;
        RB.x0 = lx;
        RB.y0 = ly;
        B.sel = -1;
        snd_bad();
    }
}
static void input(const input_event_t *ev)
{
    drag_input(ev);
    if (ev->kind != IN_TAP) {
        return;
    }
    const int id = button_at(ev->x, ev->y);
    if (id >= 0) {
        do_button(id);
        return;
    }
    if (B.g == G_OVER && B.t > 1.0f) {
        game_new(B.mode == MODE_NET ? MODE_NET : B.mode);
        return;
    }
    if (B.g >= G_OPEN && ev->x >= OX && ev->y >= OY && ev->y < OY + G_.h) {
        tap_board(ev->x, ev->y);
    }
}

// ---------------------------------------------------------------- the panel
enum {
    PI_LEVEL,
    PI_SIDE,
    PI_LANG,
    PI_SND,
    PI_NEW,
    PI_TITLE,
    PI_SKIN,
    PI_HINT,
    PI_SHAKE
};
static const char *const LV_V[] = {"EASY", "קל", "MEDIUM", "בינוני", "HARD", "קשה"};
static const char *const SIDE_V[] = {"WHITE", "לבנים", "BLACK", "שחורים"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const SKIN_V[] = {"WOOD", "עץ", "BEACH", "חוף", "CLUB", "מועדון"};
static const pitem_t ITEMS[] = {
        {PI_PICK, PI_SKIN, 3, 0, {"BOARD", "לוח"}, SKIN_V, "bg_skin"},
        {PI_PICK, PI_HINT, 2, 0, {"HINTS", "רמזים"}, ONOFF_V, "bg_hint"},
        {PI_PICK, PI_SHAKE, 2, 0, {"SHAKE TO ROLL", "ניעור לגלגול"}, ONOFF_V, "bg_shake"},
        {PI_PICK, PI_LEVEL, 3, 1, {"LEVEL", "רמה"}, LV_V, "bg_lvl"},
        {PI_PICK, PI_SIDE, 2, 0, {"YOU PLAY", "אתם משחקים"}, SIDE_V, "bg_side"},
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "bg_lang"},
        {PI_PICK, PI_SND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, "bg_snd"},
        {PI_ACT2, PI_NEW, 0, 0, {"NEW GAME", "משחק חדש"}, NULL, NULL},
        {PI_ACT, PI_TITLE, 0, 0, {"THE TITLE", "המסך הראשי"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == PI_NEW) {
        return B.g >= G_OPEN && B.mode != MODE_NET;
    }
    if (id == PI_TITLE) {
        return B.g != G_TITLE;
    }
    return true;
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_LEVEL:
        B.level = v % 3;
        break;
    case PI_SIDE:
        if (B.g >= G_OPEN && B.mode == MODE_CPU) {
            game_new(MODE_CPU);
        }
        break;
    case PI_LANG:
        s_lang = v & 1;
        break;
    case PI_SND:
        s_snd = v == 0;
        break;
    case PI_SKIN:
        if (v % 3 != s_skin) {
            s_skin = v % 3;
            if (!art_load()) {
                s_skin = 0;
                if (!art_load()) {
                    ESP_LOGE(TAG, "the art did not load");
                }
            }
        }
        break;
    case PI_HINT:
        s_hints = v == 0;
        break;
    case PI_SHAKE:
        s_shake_on = v == 0;
        break;
    case PI_NEW:
        game_new(B.mode);
        break;
    case PI_TITLE:
        if (B.mode == MODE_NET) {
            duel_stop();
        }
        B.g = G_TITLE;
        bg_start(&B.pos);
        A.on = false;
        s_nq = 0;
        break;
    }
}
static const pitems_t PANEL = {ITEMS, sizeof ITEMS / sizeof ITEMS[0], item_shown, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- the app
static int64_t s_last;
static void enter(int64_t now)
{
    s_last = now;
    K = gfx_w < 400 ? 1 : 0;
#if BOARD_UI_TABLET
    if (gfx_w >= 600 && gfx_h >= 352) {
        K = 2;
    }
#endif
#if BOARD_ROUND
    if (gfx_round()) {
        K = gfx_w >= 440 ? 3 : 4;  // (the round frame: 466 the AMOLED-1.75C, 412 the Watcher)
    }
#endif
    OX = K == 1 || RND ? (gfx_w - G_.w) / 2 : gfx_w - G_.w;
    OY = K == 2 || RND ? (gfx_h - G_.h) / 2 : 0;
    s_lang = (int)store_get("bg_lang", 1) & 1;
    s_snd = store_get("bg_snd", 0) == 0;
    if (s_snd) {
        JPLAY(BG_OPEN);  // (the opening obeys SOUND OFF)
    }
    B.level = (int)store_get("bg_lvl", 1) % 3;
    s_skin = (int)store_get("bg_skin", 0) % 3;
    s_hints = store_get("bg_hint", 0) == 0;
    s_shake_on = store_get("bg_shake", 0) == 0;
    s_seed = (uint32_t)now * 2654435761u ^ esp_random();
    if (!art_load() && (s_skin = 0, !art_load())) {
        free(s_art);
        s_art = NULL;
        s_board = NULL;
        ESP_LOGE(TAG, "the art did not load");
    }
    tfsfx_load_mem(BG_SFX_DATA, BG_SFX_LEN, FXN, FX_N < BG_SFX_N ? FX_N : BG_SFX_N, 45);  // (in the app file)
    HF.on = D.on = RB.on = false;
    s_last_side = -1;
    s_nlast = 0;
    bg_start(&B.pos);
    B.g = G_TITLE;
    A.on = false;
    s_nq = 0;
    s_ai = 0;
    B.note_t = 0;
    B.score[0] = B.score[1] = 0;
    duel_open('G');  // (link app 'G': TEAM BUILDING has 'B')
    sysui_panel_items(&PANEL);
#ifdef ESP_PLATFORM
    if (!s_ai_task) {
        s_ai_quit = false;
        xTaskCreatePinnedToCoreWithCaps(ai_task, "backgammon", 16384, NULL, 2, &s_ai_task, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
#endif
}
static void leave(void)
{
#ifdef ESP_PLATFORM
    for (int k = 0; k < 500 && s_ai == 1; k++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_ai_task) {
        s_ai_quit = true;
        vTaskDelay(pdMS_TO_TICKS(60));
        vTaskDeleteWithCaps(s_ai_task);
        s_ai_task = NULL;
    }
#endif
    duel_close();
    tfsfx_free();
    free(s_art);
    s_art = NULL;
    s_board = NULL;
}
static void update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    B.t += dt;
    if (B.note_t > 0) {
        B.note_t -= dt;
    }
    // (also in the title: the invites of the friends come in duel_poll; with no poll the title never shows JOIN)
    net_poll(now);
    s_ninv = duel_invites(s_inv, 3);
    if (HF.on) {
        HF.t += dt;
        if (HF.t >= 0.3f) {
            HF.on = false;
        }
    }
    if (RB.on) {
        RB.t += dt;
        if (RB.t >= 0.18f) {
            RB.on = false;
            fx(FX_PLACE, 60, 0.05f);
        }
    }
    if (D.on && D.moved) {
        // (the sound of the slide while the finger moves the checker)
        D.snd_t += dt;
        static int lx, ly;
        if (D.snd_t > 0.3f && abs(D.x - lx) + abs(D.y - ly) > 12) {
            fx(FX_SLIDE, 40, 0.15f);
            D.snd_t = 0;
        }
        lx = D.x;
        ly = D.y;
    }
    if (A.on) {
        A.t += dt;
        if (A.t >= A.len) {
            step_land();
        }
        return;
    }
    if (B.g == G_OVER || B.g == G_TITLE || B.g == G_WAIT) {
        return;
    }
    // the steps that wait (the computer, the friend): one slide each
    if (s_nq) {
        bgstep_t s;
        if (bg_step_ok(&B.pos, s_q_side, s_queue[0].from, s_queue[0].die, &s)) {
            step_go(&s, s_q_side);
        }
        memmove(s_queue, s_queue + 1, sizeof(bgstep_t) * (size_t)(s_nq - 1));
        s_nq--;
        if (!s_nq && B.g == G_MOVE) {
            B.g = G_DONE;
            B.t = 0;
        }
        return;
    }
    switch (B.g) {
    case G_OPEN:
        // (the host or this board rolls the opening; 2 BOARDS: the host only)
        if (B.t > 0.6f && !B.open_d[0] && (B.mode != MODE_NET || duel_host())) {
            int a = die(), b = die();
            while (a == b) {
                b = die();
            }
            opening(a, b, true);
        }
        if (B.open_d[0] && B.t > 1.6f) {
            B.side = B.open_d[0] > B.open_d[1] ? 0 : 1;
            B.d[0] = B.open_d[0];
            B.d[1] = B.open_d[1];
            note(B.side == (B.mode == MODE_TWO ? 0 : B.my_side)
                         ? (B.mode == MODE_TWO ? T("WHITE STARTS", "הלבנים מתחילים") : T("YOU START", "אתם מתחילים"))
                         : (B.mode == MODE_TWO ? T("BLACK STARTS", "השחורים מתחילים") : T("THEY START", "הם מתחילים")),
                 1.6f);
            begin_moves();
        }
        break;
    case G_ROLL:
        if (B.mode == MODE_CPU && B.side != B.my_side && B.t > 0.5f) {
            roll(die(), die(), false);
        }
        // SHAKE TO ROLL: a shake of the board (my roll) takes the dice in the hand
        else if (s_shake_on && mine_now() && B.t > 0.3f && imu_shake() > 1.1f) {
            B.g = G_SHAKE;
            B.t = 0;
            s_shake_t = 0;
            s_calm_t = 0;
            fx(FX_SHAKE, 70, 0.05f);
        }
        break;
    case G_SHAKE:
        // the dice go when the shake ends (or after 1.6 s)
        s_shake_t += dt;
        s_calm_t = imu_shake() < 0.45f ? s_calm_t + dt : 0;
        if (s_calm_t > 0.12f || s_shake_t > 1.6f) {
            roll(die(), die(), true);
        }
        break;
    case G_DICE: {
        // the dice in the air: the height falls (a bounce at the floor), the board walls stop them, the friction slows
        // them; the face changes with each turn until they stop
        const int s = die_s();
        const bool right = (B.side == 0) != view_flip();
        const float x0 = (right ? OX + G_.fr + 6 * G_.pw + G_.barw : OX + G_.fr) + s * 0.7f,
                    x1 = x0 + 6 * G_.pw - s * 1.4f;
        const float y0 = OY + G_.fr + s * 0.7f, y1 = OY + G_.h - G_.fr - s * 0.7f;
        bool all = true;
        for (int i = 0; i < 2; i++) {
            die_t *e = &s_die[i];
            if (e->rest) {
                continue;
            }
            all = false;
            e->vz -= 260 * dt;
            e->z += e->vz * dt;
            if (e->z < 0) {
                e->z = 0;
                if (e->vz < -24) {
                    fx(i ? FX_BOUNCE : FX_THROW1, (int)(40 - e->vz * 0.5f > 95 ? 95 : 40 - e->vz * 0.5f), 0.1f);
                    e->vz = -e->vz * 0.42f;
                } else {
                    e->vz = 0;
                }
            }
            const float fr2 = e->z > 0 ? 0.6f : 3.2f;  // (more friction on the board)
            e->vx -= e->vx * fr2 * dt;
            e->vy -= e->vy * fr2 * dt;
            e->va -= e->va * fr2 * dt;
            e->x += e->vx * dt;
            e->y += e->vy * dt;
            e->a += e->va * dt;
            if (e->x < x0) {
                e->x = x0;
                e->vx = -e->vx * 0.5f;
                fx(FX_BOUNCE, 45, 0.15f);
            }
            if (e->x > x1) {
                e->x = x1;
                e->vx = -e->vx * 0.5f;
                fx(FX_BOUNCE, 45, 0.15f);
            }
            if (e->y < y0) {
                e->y = y0;
                e->vy = -e->vy * 0.5f;
                fx(FX_BOUNCE, 40, 0.15f);
            }
            if (e->y > y1) {
                e->y = y1;
                e->vy = -e->vy * 0.5f;
                fx(FX_BOUNCE, 40, 0.15f);
            }
            if ((int)(e->a * 1.3f) != (int)((e->a - e->va * dt) * 1.3f)) {
                e->face = die();
            }
            const float sp = fabsf(e->vx) + fabsf(e->vy) + fabsf(e->va) * 4;
            if ((sp < 18 && e->z <= 0 && e->vz == 0) || B.t > 1.4f) {
                // (at rest: the nearest square turn, the real face)
                e->rest = true;
                e->a = roundf(e->a / 1.5708f) * 1.5708f + (i ? 0.12f : -0.1f);
                e->z = 0;
            }
        }
        // (two dice at rest must not lie on each other)
        if (all || (s_die[0].rest && s_die[1].rest)) {
            die_t *a = &s_die[0], *b2 = &s_die[1];
            if (fabsf(a->x - b2->x) < s * 1.15f && fabsf(a->y - b2->y) < s * 1.15f) {
                b2->x = a->x + (a->x < (x0 + x1) / 2 ? s * 1.3f : -s * 1.3f);
            }
            begin_moves();
        }
        break;
    }
    case G_MOVE:
        if (B.mode == MODE_CPU && B.side != B.my_side) {
            if (!s_ai) {
                ai_start();
            }
            if (s_ai == 2 && B.t > 0.4f) {
                s_ai = 0;
                s_nq = 0;
                for (int i = 0; i < s_ai_turn.n; i++) {
                    s_queue[s_nq++] = s_ai_turn.s[i];
                }
                s_q_side = B.side;
                if (!s_nq) {
                    B.g = G_DONE;
                    B.t = 0;
                }
            }
        } else if (mine_now() && B.nsteps >= B.most) {
            B.g = G_DONE;
            B.t = 0;
        }
        break;
    case G_DONE:
        if (B.t > (B.note_t > 0 ? 1.2f : 0.35f)) {
            end_turn();
        }
        break;
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
// the icon: a corner of the wood board with its points and the white and black checkers; two dice roll on it: they
// tumble and bounce, then stop on 6 and 5 (a loop of 2 s)
static void ic_bg_die(float cx, float cy, float r, float ang, int face)
{
    static const float SQ[] = {-1, -0.8f, -0.8f, -1, 0.8f, -1, 1, -0.8f, 1, 0.8f, 0.8f, 1, -0.8f, 1, -1, 0.8f};
    static const int8_t PIP[7][6][2] = {{{0}},
                                        {{0, 0}},
                                        {{-1, -1}, {1, 1}},
                                        {{-1, -1}, {0, 0}, {1, 1}},
                                        {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
                                        {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}},
                                        {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}}};
    float cs = fcos(ang), sn = fsin(ang);
    ic_shape(SQ, 8, cx + r * 0.16f, cy + r * 0.24f, r * 1.04f, ang, ic_g(32, 16, 0), 150);
    ic_shape(SQ, 8, cx, cy, r, ang, ic_g(192, 176, 160), 256);
    ic_shape(SQ, 8, cx - r * 0.06f, cy - r * 0.06f, r * 0.9f, ang, ic_g(224, 224, 208), 256);
    for (int k = 0; k < face; k++) {
        float px = PIP[face][k][0] * r * 0.5f, py = PIP[face][k][1] * r * 0.5f;
        tfd_disc(cx + px * cs - py * sn, cy + px * sn + py * cs, r * 0.19f, ic_g(32, 16, 32), 256);
    }
}
static void icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f;
    // the frame of the board and the field
    ic_band(x, y, s, 0, s, ic_g(160, 96, 48), ic_g(96, 48, 16));
    tfd_rrect((int)(x + 7 * u), (int)(y + 7 * u), (int)(86 * u), (int)(86 * u), (int)(4 * u), ic_g(64, 32, 16), 256);
    tfd_rect((int)(x + 8 * u), (int)(y + 8 * u), (int)(84 * u), (int)(84 * u), ic_g(32, 96, 64), 256);
    // the points: from the top and from the bottom, dark red and cream
    for (int k = 0; k < 5; k++) {
        float px = x + 8 * u + k * 16.8f * u, w = 16.8f * u;
        float t[] = {px + 1 * u, y + 8 * u, px + w - 1 * u, y + 8 * u, px + w / 2, y + 44 * u};
        float b[] = {px + 1 * u, y + 92 * u, px + w - 1 * u, y + 92 * u, px + w / 2, y + 56 * u};
        tfd_poly(t, 3, k & 1 ? ic_g(224, 208, 160) : ic_g(160, 32, 32), 256);
        tfd_poly(b, 3, k & 1 ? ic_g(160, 32, 32) : ic_g(224, 208, 160), 256);
    }
    // the checkers: a stack of white at the top left, black at the bottom right
    for (int k = 0; k < 3; k++) {
        float cx = x + 16.4f * u, cy = y + 15 * u + k * 13 * u;
        ic_ball(cx, cy, 7.4f * u, ic_g(128, 112, 96), ic_g(224, 224, 208), ic_g(224, 240, 240));
        tfd_ring(cx - 0.4f * u, cy - 0.4f * u, 4.2f * u, 1 * u, ic_g(192, 176, 160), 256);
    }
    for (int k = 0; k < 2; k++) {
        float cx = x + 83.6f * u, cy = y + 85 * u - k * 13 * u;
        ic_ball(cx, cy, 7.4f * u, ic_g(0, 0, 0), ic_g(48, 48, 64), ic_g(160, 160, 192));
        tfd_ring(cx - 0.4f * u, cy - 0.4f * u, 4.2f * u, 1 * u, ic_g(16, 16, 32), 256);
    }
    // the dice: they fly in, tumble and bounce for 1.2 s, then rest on 6 and 5
    for (int d = 0; d < 2; d++) {
        float t = ph / 0.6f, tx = x + (d ? 62 : 40) * u, ty = y + (d ? 54 : 46) * u;
        float cx = tx, cy = ty, ang = d ? 0.35f : -0.2f;
        int face = d ? 5 : 6;
        if (t < 1) {
            float hop = fabsf(fsin(t * 3 * 3.14159f)) * (1 - t) * 14 * u;
            cx = tx - (1 - t) * 30 * u;
            cy = ty - hop;
            ang += (1 - t) * (d ? 7 : -6);
            face = 1 + ((int)(t * 9) + d * 3) % 6;
        }
        ic_bg_die(cx, cy, 8 * u, ang, face);
    }
    ic_end();
}
const app_t app_backgammon = {
        .name = "BACKGAMMON",
        .landscape = true,
        .round = APP_ROUND_HI,  // (a round panel: the board in the circle at the panel resolution)
        .round_fit = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
};
