// SIMON (GAMES): the memory game of four coloured pads.
// The task: the app task of TFN OS calls the hooks of app_simon.
// The memory: the file statics; the two console pictures and the pad map (PSRAM) come at enter and go at leave.
//
// The console plays a sequence of lights and tones; the player presses it back; each round adds one more step.
// The tones and the speeds are those of the original toy (G#3, B3, D#4, G#4; the steps get faster after 5 and 13). A GOAL of 8, 14, 20 or 31 steps
// wins; a best score for each goal. 2 PLAYERS: each player presses all of the sequence back, then adds one new colour,
// and gives the board to the other player.
// THE LOOK: the console is drawn once at the start into two pictures (all pads dark, all pads lit: a colour gradient,
// an emboss at the edges, a soft spot of light) and a map of the pads; each frame copies the dark picture and mixes the
// lit pixels of each pad by its light level. The 3.49: a long capsule of four pads with the count at the left; a 320 x
// 240 frame: the round console with the count in the black hub.
// THE TOUCH: a pad acts when the finger comes DOWN (no wait for a tap), its tone holds while the finger stays.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "jingle/jingle.h"
// the opening of SIMON: the four tones of the pads up an A major chord, then all of them at once
static const tune_note_t SIMON_OPEN[] = {
        JN(0, 150, J_E4, W_SQUARE, 22),          JN(180, 150, J_A4, W_SQUARE, 22),
        JN(360, 150, J_Db5, W_SQUARE, 22),       JN(540, 150, J_E5, W_SQUARE, 22),
        JN(760, 520, J_A4, JHOLD(W_SQUARE), 12), JN(760, 520, J_Db5, JHOLD(W_SQUARE), 12),
        JN(760, 520, J_E5, JHOLD(W_PULSE), 16),  JN(760, 600, J_A5, W_SINE, 30),
        JN(0, 700, J_A2, JHOLD(W_TRI), 40),      JN(760, 520, J_A2, JHOLD(W_TRI), 50),
};
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"

static int s_lang = 1;
#define T(en, he) (s_lang ? (he) : (en))

// ---------------------------------------------------------------- the pads: green, red, yellow, blue
static const uint8_t PAD_RGB[4][3] = {{40, 205, 90}, {235, 52, 52}, {250, 210, 40}, {48, 120, 250}};
static const float PAD_HZ[4] = {207.65f, 246.94f, 311.13f, 415.30f};  // (the original toy: G#3, B3, D#4, G#4)
static int K;  // 0 the 3.49 capsule, 1 the round console (320 x 240)
static int s_w, s_h;
static uint16_t *s_dim, *s_lit;  // the console with all pads dark / lit
static uint8_t *s_id;            // 0 the background, 1..4 a pad, 5 the hub, 6 the body
static int s_bx0[4], s_by0[4], s_bx1[4], s_by1[4], s_pcx[4], s_pcy[4], s_prad[4];
// the round console (320 x 240; THE ROUND LAYOUT: in the round frame of the panel, app_t.round =
// APP_ROUND_HI, the console is the whole circle: round_geom() sets the sizes from the frame)
static int s_rcx = 160, s_rcy = 120, s_rbody = 119, s_rout = 113, s_rin = 47, s_rhub = 42, s_gap = 4;
#define RCX    s_rcx
#define RCY    s_rcy
#define R_BODY s_rbody
#define R_OUT  s_rout
#define R_IN   s_rin
#define R_HUB  s_rhub
#define GAP    s_gap
static void round_geom(void)
{
    if (!gfx_round()) {
        s_rcx = 160;
        s_rcy = 120;
        s_rbody = 119;
        s_rout = 113;
        s_rin = 47;
        s_rhub = 42;
        s_gap = 4;
        return;
    }
    s_rcx = s_w / 2;
    s_rcy = s_h / 2;
    s_rbody = s_w / 2 - 1;
    s_rout = s_w * 47 / 100;
    s_rin = s_w * 21 / 100;
    s_rhub = s_w * 19 / 100;
    s_gap = s_w / 60;
}
// the capsule (3.49): the body and the pads (a stadium each), the text at the left of PX0
#define PX0 104

static void *ps_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
#else
    return malloc(n);
#endif
}
static bool in_stadium(float x, float y, float x0, float x1, float y0, float y1)
{
    const float r = (y1 - y0) / 2, cy = (y0 + y1) / 2;
    if (y < y0 || y > y1) {
        return false;
    }
    if (x < x0 + r) {
        return (x - x0 - r) * (x - x0 - r) + (y - cy) * (y - cy) <= r * r;
    }
    if (x > x1 - r) {
        return (x - x1 + r) * (x - x1 + r) + (y - cy) * (y - cy) <= r * r;
    }
    return true;
}
static int part_at(int x, int y)
{
    if (K) {
        const float dx = x + 0.5f - RCX, dy = y + 0.5f - RCY, d = sqrtf(dx * dx + dy * dy);
        if (d > R_BODY) {
            return 0;
        }
        if (d < R_HUB) {
            return 5;
        }
        if (d > R_OUT || d < R_IN || fabsf(dx) < GAP || fabsf(dy) < GAP) {
            return 6;
        }
        return dy < 0 ? (dx < 0 ? 1 : 2) : (dx < 0 ? 3 : 4);
    }
    const float fx = x + 0.5f, fy = y + 0.5f;
    if (!in_stadium(fx, fy, PX0 + 2, s_w - 4, 4, s_h - 4)) {
        return 0;
    }
    if (!in_stadium(fx, fy, PX0 + 10, s_w - 12, 12, s_h - 12)) {
        return 6;
    }
    const float w = (s_w - 12 - (PX0 + 10)) / 4.0f;
    const int k = (int)((fx - (PX0 + 10)) / w);
    const float in = fx - (PX0 + 10) - k * w;
    if (k < 0 || k > 3 || in < GAP || in > w - GAP) {
        return 6;
    }
    return k + 1;
}
static uint16_t rgbf(float r, float g, float b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return rgb((int)r, (int)g, (int)b);
}
static bool prep(void)
{
    s_dim = ps_alloc((size_t)s_w * s_h * 2);
    s_lit = ps_alloc((size_t)s_w * s_h * 2);
    s_id = ps_alloc((size_t)s_w * s_h);
    if (!s_dim || !s_lit || !s_id) {
        return false;
    }
    long sx[4] = {0}, sy[4] = {0}, sn[4] = {0};
    for (int p = 0; p < 4; p++) {
        s_bx0[p] = s_w;
        s_by0[p] = s_h;
        s_bx1[p] = 0;
        s_by1[p] = 0;
    }
    for (int y = 0; y < s_h; y++) {
        for (int x = 0; x < s_w; x++) {
            const int id = part_at(x, y);
            s_id[y * s_w + x] = (uint8_t)id;
            if (id >= 1 && id <= 4) {
                const int p = id - 1;
                sx[p] += x;
                sy[p] += y;
                sn[p]++;
                if (x < s_bx0[p]) {
                    s_bx0[p] = x;
                }
                if (y < s_by0[p]) {
                    s_by0[p] = y;
                }
                if (x > s_bx1[p]) {
                    s_bx1[p] = x;
                }
                if (y > s_by1[p]) {
                    s_by1[p] = y;
                }
            }
        }
    }
    for (int p = 0; p < 4; p++) {
        if (!sn[p]) {
            return false;
        }
        s_pcx[p] = (int)(sx[p] / sn[p]);
        s_pcy[p] = (int)(sy[p] / sn[p]);
        const int bw = s_bx1[p] - s_bx0[p], bh = s_by1[p] - s_by0[p];
        s_prad[p] = (bw > bh ? bw : bh) * 6 / 10;
    }
    for (int y = 0; y < s_h; y++) {
        for (int x = 0; x < s_w; x++) {
            const int i = y * s_w + x, id = s_id[i];
            // the light comes from the top left: an emboss at the edges of each part
            const int ul = x >= 3 && y >= 3 ? s_id[i - 3 * s_w - 3] : 0,
                      dr = x + 3 < s_w && y + 3 < s_h ? s_id[i + 3 * s_w + 3] : 0;
            const float em = (ul != id ? 0.35f : 0) - (dr != id ? 0.3f : 0);
            const float u = (float)y / s_h, fall = 1.12f - 0.4f * u;
            if (id == 0) {
                const float dx = (x - s_w / 2.0f) / s_w, dy = (y - s_h / 2.0f) / s_h,
                            v = 1.0f - (dx * dx + dy * dy) * 1.6f;
                s_dim[i] = s_lit[i] = rgbf(14 * v + 4, 16 * v + 4, 24 * v + 6);
                continue;
            }
            if (id == 6 || id == 5) {
                const float b = id == 5 ? 0.35f : 1.0f;
                const float f = fall * b + em * (id == 5 ? 0.5f : 1.0f);
                s_dim[i] = s_lit[i] = rgbf(44 * f, 46 * f, 54 * f);
                continue;
            }
            const int p = id - 1;
            const float r = PAD_RGB[p][0], g = PAD_RGB[p][1], bl = PAD_RGB[p][2];
            const float dx = (float)(x - s_pcx[p]) / s_prad[p], dy = (float)(y - s_pcy[p]) / s_prad[p];
            float spot = 1.0f - sqrtf(dx * dx + dy * dy);
            spot = spot < 0 ? 0 : spot * spot;
            const float fd = 0.42f * fall + em * 0.6f, fl = 0.95f * fall + em * 0.5f;
            s_dim[i] = rgbf(r * fd, g * fd, bl * fd);
            const float wl = spot * 0.6f;
            s_lit[i] = rgbf(r * fl * (1 - wl) + 255 * wl, g * fl * (1 - wl) + 255 * wl, bl * fl * (1 - wl) + 255 * wl);
        }
    }
    return true;
}

// ---------------------------------------------------------------- the sounds
static bool s_snd = true;
static void tone(int p, float secs)
{
    if (!s_snd) {
        return;
    }
    audio_voice_t v = {0};
    v.dur = (uint32_t)(secs * AUDIO_SAMPLE_RATE);
    v.f0 = v.f1 = PAD_HZ[p];
    v.wave = W_TRI;
    v.vol = 46;
    v.attack_ms = 6;
    v.shape = AUDIO_SUSTAIN;
    v.group = (uint8_t)(p + 1);
    audio_play(&v);
    v.f0 = v.f1 = PAD_HZ[p] * 2;
    v.wave = W_SINE;
    v.vol = 14;
    audio_play(&v);
}
static void tone_off(int p)
{
    if (s_snd) {
        audio_release_group((uint8_t)(p + 1), 40);
    }
}
static void buzz(void)
{
    if (!s_snd) {
        return;
    }
    audio_voice_t v = {0};
    v.dur = (uint32_t)(1.1f * AUDIO_SAMPLE_RATE);
    v.f0 = v.f1 = 42;
    v.wave = W_SQUARE;
    v.vol = 40;
    v.attack_ms = 4;
    v.shape = AUDIO_SUSTAIN;
    v.lp = 90;
    v.group = 9;
    audio_play(&v);
    v.f0 = 84;
    v.f1 = 80;
    v.wave = W_SAW;
    v.vol = 22;
    audio_play(&v);
}
static void fanfare(void)
{
    if (!s_snd) {
        return;
    }
    for (int i = 0; i < 12; i++) {
        audio_voice_t v = {0};
        v.delay = (uint32_t)(i * 0.09f * AUDIO_SAMPLE_RATE);
        v.dur = (uint32_t)((i == 11 ? 0.6f : 0.12f) * AUDIO_SAMPLE_RATE);
        v.f0 = v.f1 = PAD_HZ[i % 4] * (i >= 8 ? 2 : 1);
        v.wave = W_TRI;
        v.vol = 34;
        v.shape = AUDIO_PERC;
        audio_play(&v);
    }
}

// ---------------------------------------------------------------- the game
enum {
    G_TITLE,
    G_SHOW,
    G_INPUT,
    G_ADD,
    G_FAIL,
    G_WIN,
    G_CARD
};
enum {
    MODE_SOLO,
    MODE_TWO
};
#define SEQ_MAX 64
static const int GOALS[4] = {8, 14, 20, 31};
static struct {
    int g, mode, goal_i, fast;
    uint8_t seq[SEQ_MAX];
    int len, idx;   // the steps of the sequence; the next step to show or to press
    int player;     // 2 PLAYERS: 0 or 1
    float t, wait;  // the time in the state; the time with no press (the limit)
    int lit_p;      // the pad that the console shows now (-1 none)
    int held;       // the pad under the finger (-1 none)
    float held_t;
    int wrong, right;  // the end: the pad that was pressed, the pad that was right
    int score, best;
    bool new_best, slow;
} s_g;
static float s_L[4];  // the light of each pad (0..1)
static struct {
    int x, y;
    float t;
    uint16_t c;
} s_rip[4];
static int s_rip_n;
static uint32_t s_rng;
static int rnd4(void)
{
    s_rng = (s_rng * 1664525u + 1013904223u) ^ esp_random();
    return (int)((s_rng >> 13) & 3);
}
static float step_on(void)
{
    const float f = s_g.fast ? 0.72f : 1.0f;
    return (s_g.len <= 5 ? 0.42f : s_g.len <= 13 ? 0.32f : 0.22f) * f;
}
static float step_gap(void)
{
    return s_g.fast ? 0.06f : 0.08f;
}
static char s_bkey[8];
static const char *best_key(void)
{
    snprintf(s_bkey, sizeof s_bkey, "sm_b%d%s", s_g.goal_i, s_g.fast ? "f" : "");
    return s_bkey;
}
static void show_start(void)
{
    s_g.g = G_SHOW;
    s_g.idx = 0;
    s_g.t = -0.7f;
    s_g.lit_p = -1;
}
static void game_new(int mode)
{
    s_g.mode = mode;
    s_g.len = 0;
    s_g.idx = 0;
    s_g.player = 0;
    s_g.held = -1;
    s_g.lit_p = -1;
    s_g.new_best = false;
    s_g.slow = false;
    s_g.best = (int)store_get(best_key(), 0);
    if (mode == MODE_SOLO) {
        s_g.seq[s_g.len++] = (uint8_t)rnd4();
        show_start();
    } else {
        s_g.g = G_ADD;
        s_g.t = 0;
        s_g.wait = 0;
    }
}
static void fail(int pressed, bool slow)
{
    s_g.g = G_FAIL;
    s_g.t = 0;
    s_g.wrong = pressed;
    s_g.right = s_g.seq[s_g.idx];
    s_g.slow = slow;
    if (pressed >= 0) {
        tone_off(pressed);
    }
    s_g.held = -1;
    buzz();
    s_g.score = s_g.mode == MODE_SOLO ? s_g.len - 1 : s_g.len;
    if (s_g.mode == MODE_SOLO && s_g.score > s_g.best) {
        s_g.best = s_g.score;
        s_g.new_best = true;
        store_set(best_key(), s_g.best);
    }
}
static void press(int p, int x, int y)
{
    s_g.held = p;
    s_g.held_t = 0;
    s_g.wait = 0;
    s_L[p] = 1;
    tone(p, 1.5f);
    if (s_rip_n < 4) {
        s_rip[s_rip_n].x = x;
        s_rip[s_rip_n].y = y;
        s_rip[s_rip_n].t = 0;
        s_rip[s_rip_n].c = rgb(255, 255, 255);
        s_rip_n++;
    }
    if (s_g.g == G_ADD) {
        if (s_g.len < SEQ_MAX) {
            s_g.seq[s_g.len++] = (uint8_t)p;
        }
        // the other player now presses all of it back
        s_g.player ^= 1;
        s_g.idx = 0;
        s_g.g = G_INPUT;
        s_g.t = -0.6f;
        return;
    }
    if (p != s_g.seq[s_g.idx]) {
        fail(p, false);
        return;
    }
    s_g.idx++;
    if (s_g.idx < s_g.len) {
        return;
    }
    if (s_g.mode == MODE_TWO) {
        s_g.g = G_ADD;
        s_g.t = 0;
        return;
    }
    if (s_g.len >= GOALS[s_g.goal_i]) {
        s_g.g = G_WIN;
        s_g.t = 0;
        s_g.score = s_g.len;
        fanfare();
        if (s_g.score > s_g.best) {
            s_g.best = s_g.score;
            s_g.new_best = true;
            store_set(best_key(), s_g.best);
        }
        return;
    }
    // the next round: one more step (a short wait after the last press)
    s_g.seq[s_g.len++] = (uint8_t)rnd4();
    show_start();
    s_g.t = -0.9f;
}

// ---------------------------------------------------------------- the drawing
static inline uint16_t mix16(uint16_t d, uint16_t s, int a)
{
    uint32_t dd = (uint16_t)(d >> 8 | d << 8), ss = (uint16_t)(s >> 8 | s << 8);
    dd = (dd | dd << 16) & 0x07E0F81F;
    ss = (ss | ss << 16) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * (uint32_t)a) >> 5)) & 0x07E0F81F;
    r = (r | (r >> 16)) & 0xFFFF;
    return (uint16_t)(r >> 8 | r << 8);
}
static void draw_console(void)
{
    memcpy(g_fb, s_dim, (size_t)s_w * s_h * 2);
    for (int p = 0; p < 4; p++) {
        if (s_L[p] < 0.02f) {
            continue;
        }
        const int a = (int)(s_L[p] * 32);
        // the light on the body round the pad
        ui_circle(s_pcx[p], s_pcy[p], s_prad[p] * 13 / 10, rgb(PAD_RGB[p][0], PAD_RGB[p][1], PAD_RGB[p][2]),
                  (int)(s_L[p] * 34));
        for (int y = s_by0[p]; y <= s_by1[p]; y++) {
            const uint8_t *id = s_id + y * s_w;
            uint16_t *d = g_fb + y * s_w;
            const uint16_t *lit = s_lit + y * s_w;
            const uint16_t *dim = s_dim + y * s_w;
            for (int x = s_bx0[p]; x <= s_bx1[p]; x++) {
                if (id[x] == p + 1) {
                    d[x] = a >= 32 ? lit[x] : mix16(dim[x], lit[x], a);
                }
            }
        }
    }
    for (int i = 0; i < s_rip_n; i++) {
        const float f = s_rip[i].t / 0.4f;
        ui_ring(s_rip[i].x, s_rip[i].y, 8 + (int)(f * 46), 3, s_rip[i].c, (int)((1 - f) * 150));
    }
}
static const char *state_line(void)
{
    static char b[40];
    switch (s_g.g) {
    case G_SHOW:
        return T("WATCH", "צפו");
    case G_INPUT:
        if (s_g.mode == MODE_TWO) {
            snprintf(b, sizeof b, "%s %d", T("PLAYER", "שחקן"), s_g.player + 1);
            return b;
        }
        return T("YOUR TURN", "תורכם");
    case G_ADD:
        snprintf(b, sizeof b, "%s %d: %s", T("PLAYER", "שחקן"), s_g.player + 1, T("ADD", "הוסיפו"));
        return b;
    case G_FAIL:
        return s_g.slow ? T("TOO SLOW", "לאט מדי") : T("WRONG", "טעות");
    case G_WIN:
        return T("YOU WIN!", "ניצחתם!");
    default:
        return "";
    }
}
// the buttons of the title and the card
enum {
    BT_PLAY,
    BT_TWO,
    BT_AGAIN
};
typedef struct {
    int16_t x, y, w, h;
} rect_t;
static rect_t s_btn[4];
static int s_btn_id[4], s_nbtn;
static void button(int id, int x, int y, int w, int h, const char *label, uint16_t col)
{
    ui_rrect(x + 1, y + 3, w, h, 12, rgb(0, 0, 0));
    ui_rrect(x, y, w, h, 12, col);
    ui_rrect(x + 3, y + 2, w - 6, 5, 3, rgb(255, 255, 255));
    const kfont_t *f = ui_text_w(&KF_TEXT, label) <= w - 10 ? &KF_TEXT : &KF_LABEL;
    if (gfx_round() && ui_text_w(&KF_BIG, label) <= w - 16) {
        f = &KF_BIG;  // (the round frame: bigger letters)
    }
    ui_text_c(f, x + w / 2, y + h / 2 + (f == &KF_BIG ? 10 : f == &KF_TEXT ? 7 : 5), label, rgb(255, 255, 255));
    if (s_nbtn < 4) {
        s_btn[s_nbtn] = (rect_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
        s_btn_id[s_nbtn++] = id;
    }
}
static int button_at(int x, int y)
{
    for (int i = 0; i < s_nbtn; i++) {
        const rect_t r = s_btn[i];
        if (x >= r.x - 4 && x < r.x + r.w + 4 && y >= r.y - 4 && y < r.y + r.h + 4) {
            return s_btn_id[i];
        }
    }
    return -1;
}
static void draw_info(void)
{
    char n[16];
    const int count = s_g.g == G_INPUT ? s_g.idx : s_g.g == G_SHOW ? s_g.len : s_g.g == G_ADD ? s_g.len : s_g.score;
    snprintf(n, sizeof n, "%d", count);
    const char *line = state_line();
    const float limit = s_g.g == G_INPUT && s_g.t > 0 ? 1.0f - s_g.wait / 3.0f : -1;
    if (!K) {
        const int cx = PX0 / 2;
#if BOARD_UI_TABLET
        const int oy =
                s_h > 300 ? (s_h - 172) / 2 : 0;  // (the Tab5: the column of the text in the middle of the height)
#else
        const int oy = 0;
#endif
        ui_text_c(&KF_BIG, cx, 30 + oy, T("SIMON", "סיימון"), rgb(235, 236, 245));
        ui_text_c(ui_text_w(&KF_TEXT, line) <= PX0 - 6 ? &KF_TEXT : &KF_LABEL, cx, 58 + oy, line,
                  s_g.g == G_FAIL  ? rgb(255, 120, 110)
                  : s_g.g == G_WIN ? rgb(255, 214, 90)
                                   : rgb(200, 210, 230));
        if (s_g.g != G_TITLE) {
            ui_text_c(&KF_BIG, cx, 104 + oy, n, rgb(255, 255, 255));
        }
        if (limit >= 0 && limit < 0.7f) {
            ui_rrect(10, 116 + oy, (int)((PX0 - 20) * (limit < 0 ? 0 : limit)), 5, 2, rgb(255, 150, 90));
        }
        char b[32];
        snprintf(b, sizeof b, "%s %d", T("BEST", "שיא"), s_g.best);
        if (s_g.mode == MODE_SOLO) {
            ui_text_c(&KF_LABEL, cx, 140 + oy, b, rgb(150, 160, 185));
        }
        snprintf(b, sizeof b, "%s %d", T("GOAL", "יעד"), GOALS[s_g.goal_i]);
        ui_text_c(&KF_LABEL, cx, 158 + oy, b, rgb(110, 120, 140));
        return;
    }
    // the hub
    if (s_g.g == G_TITLE) {
        return;
    }
    // (2 PLAYERS: the player above, a + when the player adds a colour)
    char who[24];
    if (s_g.mode == MODE_TWO && (s_g.g == G_INPUT || s_g.g == G_ADD)) {
        snprintf(who, sizeof who, "%s %d", T("PLAYER", "שחקן"), s_g.player + 1);
        line = who;
    }
    if (gfx_round()) {
        // (the round frame: the hub is bigger: the count in the title font, the line over it)
        ui_text_c(&KF_TITLE, RCX, RCY + R_HUB * 45 / 100, s_g.g == G_ADD ? "+" : n, rgb(255, 255, 255));
        ui_text_c(ui_text_w(&KF_TEXT, line) < R_HUB * 18 / 10 ? &KF_TEXT : &KF_LABEL, RCX, RCY - R_HUB * 30 / 100, line,
                  s_g.g == G_FAIL ? rgb(255, 120, 110) : rgb(190, 200, 220));
        if (limit >= 0 && limit < 0.7f) {
            ui_ring(RCX, RCY, R_HUB - 4, 5, rgb(255, 150, 90), (int)(255 * (1 - limit)));
        }
        return;
    }
    ui_text_c(&KF_BIG, RCX, RCY + 10, s_g.g == G_ADD ? "+" : n, rgb(255, 255, 255));
    ui_text_c(&KF_LABEL, RCX, RCY - 16, line, s_g.g == G_FAIL ? rgb(255, 120, 110) : rgb(190, 200, 220));
    if (limit >= 0 && limit < 0.7f) {
        ui_ring(RCX, RCY, R_HUB - 3, 3, rgb(255, 150, 90), (int)(255 * (1 - limit)));
    }
}
// the card of the round frame: a disc over the middle of the console, its buttons 0.10 D high (4.4 mm on the 1.75C)
static void draw_card_round(void)
{
    const int r = s_w * 34 / 100, mx = RCX, cy = RCY, bw = s_w * 44 / 100, bh = s_w / 10;
    ui_circle(mx, cy, r + 3, rgb(90, 96, 120), 255);
    ui_circle(mx, cy, r, rgb(16, 18, 26), 255);
    if (s_g.g == G_TITLE) {
        ui_text_c(&KF_TITLE, mx, cy - s_w * 13 / 100, T("SIMON", "סיימון"), rgb(255, 236, 190));
        button(BT_PLAY, mx - bw / 2, cy - s_w * 9 / 100, bw, bh, T("PLAY", "שחקו"), rgb(60, 150, 80));
        button(BT_TWO, mx - bw / 2, cy + s_w * 4 / 100, bw, bh, T("2 PLAYERS", "2 שחקנים"), rgb(60, 100, 190));
        return;
    }
    char b[48];
    if (s_g.mode == MODE_TWO) {
        snprintf(b, sizeof b, "%s %d %s", T("PLAYER", "שחקן"), (s_g.player ^ 1) + 1, T("WINS", "ניצח"));
    } else {
        snprintf(b, sizeof b, "%s %d", T("SCORE", "ניקוד"), s_g.score);
    }
    ui_text_c(ui_text_w(&KF_TITLE, b) < r * 17 / 10 ? &KF_TITLE : &KF_BIG, mx, cy - s_w * 13 / 100, b,
              s_g.g == G_WIN || s_g.new_best ? rgb(255, 214, 90) : rgb(240, 240, 245));
    if (s_g.mode == MODE_TWO) {
        snprintf(b, sizeof b, "%s %d", T("STEPS", "צעדים"), s_g.score);
    } else {
        snprintf(b, sizeof b, "%s%s %d", s_g.new_best ? T("NEW ", "") : "",
                 s_g.new_best ? T("BEST!", "שיא חדש!") : T("BEST", "שיא"), s_g.best);
    }
    ui_text_c(&KF_BIG, mx, cy - s_w * 3 / 100, b, s_g.new_best ? rgb(255, 180, 90) : rgb(170, 180, 200));
    button(BT_AGAIN, mx - bw / 2, cy + s_w * 5 / 100, bw, bh, T("AGAIN", "שוב"), rgb(60, 150, 80));
}
static void draw_card(void)
{
    if (gfx_round()) {
        draw_card_round();
        return;
    }
    const int cw = K ? 220 : 300, ch = K ? 150 : 150, cx0 = K ? (s_w - cw) / 2 : PX0 + (s_w - PX0 - cw) / 2,
              cy0 = (s_h - ch) / 2;
    ui_rbox(cx0, cy0, cw, ch, 16, rgb(16, 18, 26), rgb(90, 96, 120), 2);
    const int mx = cx0 + cw / 2;
    if (s_g.g == G_TITLE) {
        ui_text_c(&KF_BIG, mx, cy0 + 34, T("SIMON", "סיימון"), rgb(255, 236, 190));
        button(BT_PLAY, cx0 + 16, cy0 + 48, cw - 32, 42, T("PLAY", "שחקו"), rgb(60, 150, 80));
        button(BT_TWO, cx0 + 16, cy0 + 98, cw - 32, 40, T("2 PLAYERS", "2 שחקנים"), rgb(60, 100, 190));
        return;
    }
    char b[48];
    if (s_g.mode == MODE_TWO) {
        snprintf(b, sizeof b, "%s %d %s", T("PLAYER", "שחקן"), (s_g.player ^ 1) + 1, T("WINS", "ניצח"));
    } else {
        snprintf(b, sizeof b, "%s %d", T("SCORE", "ניקוד"), s_g.score);
    }
    ui_text_c(&KF_BIG, mx, cy0 + 36, b, s_g.g == G_WIN || s_g.new_best ? rgb(255, 214, 90) : rgb(240, 240, 245));
    if (s_g.mode == MODE_TWO) {
        snprintf(b, sizeof b, "%s %d", T("STEPS", "צעדים"), s_g.score);
    } else {
        snprintf(b, sizeof b, "%s%s %d", s_g.new_best ? T("NEW ", "") : "",
                 s_g.new_best ? T("BEST!", "שיא חדש!") : T("BEST", "שיא"), s_g.best);
    }
    ui_text_c(&KF_TEXT, mx, cy0 + 64, b, s_g.new_best ? rgb(255, 180, 90) : rgb(170, 180, 200));
    button(BT_AGAIN, cx0 + 16, cy0 + ch - 58, cw - 32, 42, T("AGAIN", "שוב"), rgb(60, 150, 80));
}
static void render(int64_t now)
{
    (void)now;
    s_nbtn = 0;
    if (!s_dim) {
        gfx_fill(rgb(10, 10, 14));
        ui_text_c(&KF_LABEL, gfx_w / 2, gfx_h / 2, "NO MEMORY", rgb(255, 200, 180));
        return;
    }
    draw_console();
    draw_info();
    if (s_g.g == G_TITLE || (s_g.g == G_CARD && s_g.t > 0.3f)) {
        draw_card();
    }
}

// ---------------------------------------------------------------- the input
static int pad_at(int x, int y)
{
    if (x < 0 || y < 0 || x >= s_w || y >= s_h) {
        return -1;
    }
    int id = s_id[y * s_w + x];
    if (id >= 1 && id <= 4) {
        return id - 1;
    }
    // (a finger on a gap or the rim: the nearest pad within 14 px)
    if (id == 6 || id == 0) {
        int best = -1, bd = 14 * 14;
        for (int dy = -14; dy <= 14; dy += 2) {
            for (int dx = -14; dx <= 14; dx += 2) {
                const int xx = x + dx, yy = y + dy;
                if (xx < 0 || yy < 0 || xx >= s_w || yy >= s_h) {
                    continue;
                }
                const int k = s_id[yy * s_w + xx];
                if (k >= 1 && k <= 4 && dx * dx + dy * dy < bd) {
                    bd = dx * dx + dy * dy;
                    best = k - 1;
                }
            }
        }
        return best;
    }
    return -1;
}
static void input(const input_event_t *ev)
{
    if (ev->kind == IN_TAP) {
        const int id = button_at(ev->x, ev->y);
        if (id == BT_PLAY) {
            game_new(MODE_SOLO);
        } else if (id == BT_TWO) {
            game_new(MODE_TWO);
        } else if (id == BT_AGAIN) {
            game_new(s_g.mode);
        }
        return;
    }
    if (ev->kind == IN_UP) {
        if (s_g.held >= 0) {
            tone_off(s_g.held);
            s_g.held = -1;
        }
        return;
    }
    if (ev->kind != IN_DOWN || !s_dim) {
        return;
    }
    if ((s_g.g != G_INPUT && s_g.g != G_ADD) || s_g.t < 0) {
        return;
    }
    const int p = pad_at(ev->x, ev->y);
    if (p < 0) {
        return;
    }
    if (s_g.held >= 0) {
        tone_off(s_g.held);
    }
    press(p, ev->x, ev->y);
}

// ---------------------------------------------------------------- the panel
enum {
    PI_GOAL,
    PI_SPEED,
    PI_LANG,
    PI_SND,
    PI_NEW,
    PI_TITLE
};
static const char *const GOAL_V[] = {"8", "8", "14", "14", "20", "20", "31", "31"};
static const char *const SPEED_V[] = {"NORMAL", "רגיל", "FAST", "מהיר"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const pitem_t ITEMS[] = {
        {PI_PICK, PI_GOAL, 4, 3, {"GOAL", "יעד"}, GOAL_V, "sm_goal"},
        {PI_PICK, PI_SPEED, 2, 0, {"SPEED", "מהירות"}, SPEED_V, "sm_fast"},
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "sm_lang"},
        {PI_PICK, PI_SND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, "sm_snd"},
        {PI_ACT2, PI_NEW, 0, 0, {"NEW GAME", "משחק חדש"}, NULL, NULL},
        {PI_ACT, PI_TITLE, 0, 0, {"THE TITLE", "המסך הראשי"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == PI_NEW) {
        return s_g.g != G_TITLE;
    }
    if (id == PI_TITLE) {
        return s_g.g != G_TITLE;
    }
    return true;
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_GOAL:
        s_g.goal_i = v & 3;
        s_g.best = (int)store_get(best_key(), 0);
        break;
    case PI_SPEED:
        s_g.fast = v & 1;
        s_g.best = (int)store_get(best_key(), 0);
        break;
    case PI_LANG:
        s_lang = v & 1;
        break;
    case PI_SND:
        s_snd = v == 0;
        break;
    case PI_NEW:
        game_new(s_g.mode);
        break;
    case PI_TITLE:
        s_g.g = G_TITLE;
        s_g.t = 0;
        break;
    }
}
static void opened(void)
{
    for (int p = 0; p < 4; p++) {
        tone_off(p);
    }
    s_g.held = -1;
}
static const pitems_t PANEL = {ITEMS, sizeof ITEMS / sizeof ITEMS[0], item_shown, NULL, item_set, opened, NULL};

// ---------------------------------------------------------------- the app
static int64_t s_last;
static void enter(int64_t now)
{
    s_last = now;
    s_w = gfx_w;
    s_h = gfx_h;
    K = s_w < 400 || gfx_round() ? 1 : 0;  // (the round frame: the round console)
    round_geom();
    s_lang = (int)store_get("sm_lang", 1) & 1;
    s_snd = store_get("sm_snd", 0) == 0;
    if (s_snd) {
        JPLAY(SIMON_OPEN);  // (the opening obeys SOUND OFF)
    }
    s_g.goal_i = (int)store_get("sm_goal", 3) & 3;
    s_g.fast = (int)store_get("sm_fast", 0) & 1;
    s_g.best = (int)store_get(best_key(), 0);
    s_g.g = G_TITLE;
    s_g.t = 0;
    s_g.held = -1;
    s_g.lit_p = -1;
    s_g.mode = MODE_SOLO;
    s_rng = (uint32_t)now * 2654435761u ^ esp_random();
    memset(s_L, 0, sizeof s_L);
    s_rip_n = 0;
    if (!prep()) {
        free(s_dim);
        free(s_lit);
        free(s_id);
        s_dim = s_lit = NULL;
        s_id = NULL;
    }
    sysui_panel_items(&PANEL);
}
static void leave(void)
{
    for (int p = 0; p < 4; p++) {
        tone_off(p);
    }
    free(s_dim);
    free(s_lit);
    free(s_id);
    s_dim = s_lit = NULL;
    s_id = NULL;
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
    s_g.t += dt;
    for (int i = 0; i < s_rip_n; i++) {
        s_rip[i].t += dt;
        if (s_rip[i].t >= 0.4f) {
            s_rip[i] = s_rip[--s_rip_n];
            i--;
        }
    }
    // the lights: a pad that is on goes up fast, the others fade
    float target[4] = {0, 0, 0, 0};
    if (s_g.held >= 0) {
        target[s_g.held] = 1;
        s_g.held_t += dt;
        if (s_g.held_t > 1.4f) {
            tone_off(s_g.held);
            s_g.held = -1;
        }
    }
    if (s_g.lit_p >= 0) {
        target[s_g.lit_p] = 1;
    }
    switch (s_g.g) {
    case G_TITLE: {
        // the console waits: a slow turn of soft lights
        const float ph = s_g.t * 1.3f;
        for (int p = 0; p < 4; p++) {
            const int order[4] = {0, 1, 3, 2};
            float d = fmodf(ph - order[p] * 0.25f * 4, 4.0f);
            if (d < 0) {
                d += 4;
            }
            target[p] = d < 1 ? 0.55f * (1 - d) : 0;
        }
        break;
    }
    case G_SHOW: {
        if (s_g.t < 0) {
            break;
        }
        const float on = step_on(), per = on + step_gap();
        const int k = (int)(s_g.t / per);
        if (k >= s_g.len) {
            s_g.lit_p = -1;
            s_g.g = G_INPUT;
            s_g.idx = 0;
            s_g.t = 0;
            s_g.wait = 0;
            break;
        }
        const bool lit = s_g.t - k * per < on;
        if (lit && s_g.lit_p != s_g.seq[k]) {
            s_g.lit_p = s_g.seq[k];
            tone(s_g.lit_p, on);
        }
        if (!lit) {
            s_g.lit_p = -1;
        }
        s_g.idx = k;
        break;
    }
    case G_INPUT:
    case G_ADD:
        if (s_g.t < 0) {
            break;
        }
        if (s_g.held < 0) {
            s_g.wait += dt;
        }
        if (s_g.g == G_INPUT && s_g.wait > 3.0f) {
            fail(-1, true);
        }
        break;
    case G_FAIL:
        // the pad that was right flashes 4 times
        if (s_g.t < 1.6f) {
            if (fmodf(s_g.t, 0.4f) < 0.22f) {
                target[s_g.right] = 1;
            }
        } else {
            s_g.g = G_CARD;
            s_g.t = 0;
        }
        break;
    case G_WIN:
        if (s_g.t < 1.8f) {
            const int order[4] = {0, 1, 3, 2};
            target[order[(int)(s_g.t * 10) & 3]] = 1;
        } else {
            s_g.g = G_CARD;
            s_g.t = 0;
        }
        break;
    case G_CARD:
        break;
    }
    for (int p = 0; p < 4; p++) {
        if (target[p] > s_L[p]) {
            s_L[p] = target[p] > s_L[p] + dt * 20 ? s_L[p] + dt * 20 : target[p];
        } else {
            s_L[p] -= (s_L[p] - target[p]) * (dt * 9 > 1 ? 1 : dt * 9);
        }
    }
}
#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set in each app; all static and unused is no warning)
#define IC_FN static __attribute__((unused))
static struct {
    uint16_t *fb;
    int w, h, c[4];
} s_ic_keep __attribute__((unused));
// start an icon: tfdraw draws on the screen and only in the box; ic_end gives tfdraw its last target and clip back
IC_FN void ic_begin(int x, int y, int s)
{
    s_ic_keep.fb = tfd_fb;
    s_ic_keep.w = tfd_w;
    s_ic_keep.h = tfd_h;
    s_ic_keep.c[0] = tfd_cx0;
    s_ic_keep.c[1] = tfd_cy0;
    s_ic_keep.c[2] = tfd_cx1;
    s_ic_keep.c[3] = tfd_cy1;
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
}
IC_FN void ic_end(void)
{
    tfd_target(s_ic_keep.fb, s_ic_keep.w, s_ic_keep.h);
    tfd_clip(s_ic_keep.c[0], s_ic_keep.c[1], s_ic_keep.c[2], s_ic_keep.c[3]);
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
// the icon: the round console with its four pads and the black hub; it plays a sequence: each pad lights in its turn
// (green, red, blue, yellow: a loop of 2 s), with a glow and a ring of sound
static void ic_simon_pad(float cx, float cy, float ri, float ro, int q, uint16_t c)
{
    // a quarter of a ring (q = 0 top left, 1 top right, 2 bottom right, 3 bottom left)
    float p[48];
    int n = 0;
    float a0 = 3.14159f + q * 1.5707963f;
    for (int k = 0; k <= 10; k++) {
        float a = a0 + k * 0.15708f;
        p[n++] = cx + fcos(a) * ro;
        p[n++] = cy + fsin(a) * ro;
    }
    for (int k = 10; k >= 0; k--) {
        float a = a0 + k * 0.15708f;
        p[n++] = cx + fcos(a) * ri;
        p[n++] = cy + fsin(a) * ri;
    }
    tfd_poly(p, n / 2, c, 256);
}
static void icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f;
    ic_dband(x, y, s, 0, s, ic_g(64, 48, 128), ic_g(16, 16, 48));
    static const uint8_t ON[4][3] = {{64, 224, 96}, {224, 64, 64}, {96, 160, 224}, {224, 208, 64}};
    static const uint8_t OFF[4][3] = {{0, 112, 48}, {128, 16, 32}, {16, 64, 160}, {160, 112, 0}};
    int lit = (int)(ph * 4) & 3;
    float st = ph * 4 - (int)(ph * 4), on = st < 0.75f ? 1 : 0;
    float cx = x + 50 * u, cy = y + 50 * u, ro = 42 * u, ri = 15 * u;
    // the body: a shadow, the dark ring, a light edge at the top
    tfd_disc(cx + 2 * u, cy + 3 * u, ro + 3 * u, ic_g(0, 0, 16), 160);
    tfd_disc(cx, cy, ro + 3 * u, ic_g(96, 96, 112), 256);
    tfd_disc(cx + 0.8f * u, cy + 0.8f * u, ro + 2 * u, ic_g(32, 32, 48), 256);
    for (int q = 0; q < 4; q++) {
        bool l = q == lit && on > 0;
        const uint8_t *c = l ? ON[q] : OFF[q];
        ic_simon_pad(cx, cy, ri, ro - 1.5f * u, q, ic_g(c[0], c[1], c[2]));
        // (a lighter inner part: the light falls from the upper left)
        uint16_t li = tfd_mix(ic_g(c[0], c[1], c[2]), ic_g(224, 240, 224), l ? 120 : 50);
        ic_simon_pad(cx - 1 * u, cy - 1 * u, ri + 3 * u, ro - 6 * u, q, li);
        ic_simon_pad(cx - 1 * u, cy - 1 * u, ri + 5 * u, ro - 9 * u, q, ic_g(c[0], c[1], c[2]));
    }
    // the cross between the pads
    tfd_line(cx - ro, cy, cx + ro, cy, 3.6f * u, ic_g(32, 32, 48), 256);
    tfd_line(cx, cy - ro, cx, cy + ro, 3.6f * u, ic_g(32, 32, 48), 256);
    // the glow and the ring of sound of the lit pad
    if (on > 0) {
        float dx = (lit == 0 || lit == 3) ? -1 : 1, dy = lit < 2 ? -1 : 1;
        float gx = cx + dx * 22 * u, gy = cy + dy * 22 * u;
        tfd_disc(gx, gy, 9 * u, ic_g(224, 240, 224), 110);
        tfd_ring(gx, gy, (8 + st * 22) * u, 1.6f * u, ic_g(224, 240, 224), (int)(200 * (1 - st)));
    }
    // the hub with its shine
    ic_ball(cx, cy, ri - 1 * u, ic_g(0, 0, 0), ic_g(32, 32, 48), ic_g(128, 128, 160));
    ic_end();
}
const app_t app_simon = {
        .name = "SIMON",
        .landscape = true,
        .round = APP_ROUND_HI,  // the round frame of the panel: the console is the whole circle
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
};
