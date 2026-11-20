// CHESS CLOCK: a clock for two players.
// The task: the app task of TFN OS calls enter, update, input and render (app_chessclock at the end of this file).
// The memory: the file statics of this file; no heap.
//
//  - The 3.49 (and the Tab5) lies flat between the players: each half of the long screen is the key of one player, the
//    far half turned 180 degrees. The CoreS3 (and the 320 x 240 frame) stands: the two halves at the left and right.
//  - A tap on your half ends your move: your clock stops (the increment of the mode goes on it) and the other starts.
//    Before the first move, the first tap starts the clock of the other player (as on a real clock: black starts it).
//  - The modes: FISCHER (+ the increment after each move), BRONSTEIN (+ the time that the move took, at most the
//    increment), SUDDEN DEATH (no increment). The presets 1+0, 3+2, 5+0, 10+0, 15+10, 90+30 and an own control.
//  - Under 10 s: the tenths, in red, and a soft tick each second. At 0: the flag on that half and a sound.
//  - The middle: PAUSE / GO on, and SETUP (when it stops) or RESET (a second tap within 2.5 s: the times go).
// The digits: Inter (the faces of CLOCK, libs/interfont/ck_fonts.inc).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "interfont/ck_fonts.inc"

// ---------------------------------------------------------------- the controls
typedef struct {
    uint16_t min, inc;
} preset_t;
static const preset_t PRE[] = {{1, 0}, {3, 2}, {5, 0}, {10, 0}, {15, 10}, {90, 30}};
#define NPRE 6
enum {
    MODE_FISCHER,
    MODE_BRONSTEIN,
    MODE_SUDDEN,
    MODE_N
};
static const char *const MODE_NAME[MODE_N][2] = {
        {"FISCHER", "פישר"}, {"BRONSTEIN", "ברונשטיין"}, {"SUDDEN DEATH", "מוות פתאומי"}};
static int s_pre = 2, s_own_min = 5, s_own_inc = 3, s_mode = MODE_FISCHER, s_lang = 1, s_sound = 1;
static const char *T(const char *en, const char *he)
{
    return s_lang ? he : en;
}
static int ctl_min(void)
{
    return s_pre < NPRE ? PRE[s_pre].min : s_own_min;
}
static int ctl_inc(void)
{
    return s_mode == MODE_SUDDEN ? 0 : s_pre < NPRE ? PRE[s_pre].inc : s_own_inc;
}

// ---------------------------------------------------------------- the game
enum {
    ST_SETUP,
    ST_READY,
    ST_RUN,
    ST_PAUSE,
    ST_FLAG
};
static int s_st;
static int64_t s_left[2];  // the time of each player (ms)
static int s_turn = -1;    // whose clock runs (-1: none yet)
static int s_moves[2];
static int64_t s_move_t0;  // the start of the move of now (Bronstein; it moves on in a pause)
static int64_t s_now, s_last;
static int s_flag = -1;
static int s_tick_sec = -1;    // the last second that ticked (under 10 s)
static int64_t s_again_until;  // RESET waits for its second tap until this time

static void reset_clock(void)
{
    s_left[0] = s_left[1] = (int64_t)ctl_min() * 60000;
    s_moves[0] = s_moves[1] = 0;
    s_turn = -1;
    s_flag = -1;
    s_st = ST_READY;
    s_again_until = 0;
}

static void beep(int hz, int ms, int vol)
{
    if (s_sound) {
        sfx_tone((uint16_t)hz, (uint16_t)hz, (uint16_t)ms, W_SINE, (uint8_t)vol);
    }
}

// a tap on the half of player p
static void half_tap(int p)
{
    if (s_st == ST_READY) {  // the first tap starts the clock of the other player
        s_turn = 1 - p;
        s_move_t0 = s_now;
        s_st = ST_RUN;
        sfx_ui(SFX_CLICK);
        return;
    }
    if (s_st != ST_RUN || p != s_turn) {
        return;  // (not your clock: nothing)
    }
    int64_t used = s_now - s_move_t0, inc = (int64_t)ctl_inc() * 1000;
    if (s_mode == MODE_FISCHER) {
        s_left[p] += inc;
    } else if (s_mode == MODE_BRONSTEIN) {
        s_left[p] += used < inc ? used : inc;
    }
    s_moves[p]++;
    s_turn = 1 - p;
    s_move_t0 = s_now;
    s_tick_sec = -1;
    sfx_ui(SFX_CLICK);
}

// ---------------------------------------------------------------- the layout
static int s_w, s_h, s_mid;
static bool s_rnd;  // the round panel: player 0 = the bottom half, player 1 = the top half (turned), the buttons in
                    // a band across the middle
static bool flat(void)
{
    return s_w >= 3 * s_h || s_w >= 600;
}  // (the 3.49 and the Tab5 lie flat: the far half turns)
static void layout(void)
{
    s_w = gfx_w;
    s_h = gfx_h;
    s_mid = s_w >= 600 ? 72 : 40;  // (the 320 frame: 40 px = 5.1 mm, the time needs the room)
    s_rnd = gfx_round();
    if (s_rnd) {
        s_mid = s_w * 84 / 466;
    }
}
// the half-width of the circle of the round panel at the row y
static int chord_half(int y)
{
    int r = s_w / 2, dy = y - s_h / 2;
    return dy * dy >= r * r ? 0 : (int)sqrtf((float)(r * r - dy * dy));
}
// (round) button i of n in a row from y to y + h that follows the circle
static void row_btn(int n, int i, int y, int h, int *x, int *w)
{
    int a = chord_half(y), b = chord_half(y + h), half = (a < b ? a : b) - s_w * 18 / 466, gap = 8;
    *w = (2 * half - gap * (n - 1)) / n;
    *x = s_w / 2 - half + i * (*w + gap);
}
// the half of player p: player 0 at the left; player 1 at the right (turned on a flat board)
static void half_rect(int p, int *x, int *w)
{
    *w = (s_w - s_mid) / 2;
    *x = p ? s_w - *w : 0;
}
// the two buttons of the middle: 0 = PAUSE / GO ON (the top), 1 = SETUP or RESET (the bottom)
static void mid_btn(int b, int *y, int *h)
{
    if (s_rnd) {  // (a disc in the middle band: the box of the disc)
        *h = s_mid - 8;
        *y = s_h / 2 - *h / 2;
        return;
    }
    *h = s_h / 2 - 6;
    *y = b ? s_h / 2 + 3 : 3;
}
static int mid_at(int x, int y)
{
    if (s_rnd) {
        if (y < (s_h - s_mid) / 2 - 4 || y >= (s_h + s_mid) / 2 + 4) {
            return -1;
        }
        return x < s_w / 2 ? 0 : 1;
    }
    int mx = (s_w - s_mid) / 2;
    if (x < mx || x >= mx + s_mid) {
        return -1;
    }
    return y < s_h / 2 ? 0 : 1;
}

// ---------------------------------------------------------------- the setup screen
static int setup_cols(void)
{
    return s_w >= 600 ? 7 : 4;
}
// the preset buttons 0..6 (6 = OWN), the mode (7), the own minutes - + (8, 9), the own increment - + (10, 11), START (12)
// (round) the rows of the setup: 3 presets, 4 presets (OWN last), the mode, the own values, START
static void setup_btn_round(int i, int *x, int *y, int *w, int *h)
{
    int k = s_w, row, n, j;
    if (i < 3) {
        row = 0, n = 3, j = i;
    } else if (i < 7) {
        row = 1, n = 4, j = i - 3;
    } else if (i == 7) {
        row = 2, n = 1, j = 0;
    } else if (i < 12) {
        row = 3, n = 4, j = i - 8;
    } else {
        row = 4, n = 1, j = 0;
    }
    static const int Y[5] = {98, 158, 218, 278, 346}, H[5] = {54, 54, 54, 54, 62};
    *y = Y[row] * k / 466;
    *h = H[row] * k / 466;
    row_btn(n, j, *y, *h, x, w);
}
static void setup_btn(int i, int *x, int *y, int *w, int *h)
{
    if (s_rnd) {
        setup_btn_round(i, x, y, w, h);
        return;
    }
    int cols = setup_cols(), top = s_w >= 600 ? 26 : 30, gap = 6;
    int bh = s_w >= 600 ? 50 : 40;
    if (i < 7) {
        int r = i / cols, c = i % cols;
        *w = (s_w - gap * (cols + 1)) / cols;
        *h = bh;
        *x = gap + c * (*w + gap);
        *y = top + r * (bh + gap);
        return;
    }
    int rows = (7 + cols - 1) / cols;
    int y2 = top + rows * (bh + gap) + 2;
    int h2 = s_h - y2 - gap;
    if (s_w >= 600) {  // one row: MODE, MIN - +, INC - +, START
        static const int X[6] = {6, 198, 268, 346, 416, 0}, WD[6] = {186, 66, 66, 66, 66, 0};
        if (i == 12) {
            *x = s_w - gap - 140;
            *y = y2;
            *w = 140;
            *h = h2;
            return;
        }
        *x = X[i - 7];
        *y = y2;
        *w = WD[i - 7];
        *h = h2;
        return;
    }
    // the 320 x 240 frame: MODE and START, then the own values in a row
    int rh = (h2 - gap) / 2;
    if (i == 7) {
        *x = gap;
        *y = y2;
        *w = 180;
        *h = rh;
        return;
    }
    if (i == 12) {
        *x = 192;
        *y = y2;
        *w = s_w - 192 - gap;
        *h = rh;
        return;
    }
    int k = i - 8, bw = (s_w - gap * 5) / 4;
    *x = gap + k * (bw + gap);
    *y = y2 + rh + gap;
    *w = bw;
    *h = rh;
}
static int setup_at(int x, int y)
{
    for (int i = 0; i <= 12; i++) {
        if (s_pre != NPRE && i >= 8 && i <= 11) {
            continue;
        }
        int bx, by, bw, bh;
        setup_btn(i, &bx, &by, &bw, &bh);
        if (x >= bx - 3 && x < bx + bw + 3 && y >= by - 3 && y < by + bh + 3) {
            return i;
        }
    }
    return -1;
}
static void setup_tap(int i)
{
    if (i < 0) {
        return;
    }
    sfx_ui(SFX_TICK);
    if (i < 7) {
        s_pre = i;
    } else if (i == 7) {
        s_mode = (s_mode + 1) % MODE_N;
    } else if (i == 8 && s_own_min > 1) {
        s_own_min -= s_own_min > 30 ? 5 : 1;
    } else if (i == 9 && s_own_min < 180) {
        s_own_min += s_own_min >= 30 ? 5 : 1;
    } else if (i == 10 && s_own_inc > 0) {
        s_own_inc--;
    } else if (i == 11 && s_own_inc < 60) {
        s_own_inc++;
    } else if (i == 12) {
        store_set("cc_pre", (uint32_t)s_pre);
        store_set("cc_mode", (uint32_t)s_mode);
        store_set("cc_omin", (uint32_t)s_own_min);
        store_set("cc_oinc", (uint32_t)s_own_inc);
        reset_clock();
    }
}

// ---------------------------------------------------------------- the panel
static const char *const ONOFF_V[] = {"OFF", "כבוי", "ON", "פועל"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
enum {
    PI_SETUP,
    PI_SOUND,
    PI_LANG
};
static const pitem_t ITEMS[] = {
        {PI_ACT, PI_SETUP, 0, 0, {"TIME CONTROL", "קצב המשחק"}, NULL, NULL},
        {PI_PICK, PI_SOUND, 2, 1, {"SOUND", "צליל"}, ONOFF_V, "cc_snd"},
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "cc_lang"},
};
static bool item_shown(int id)
{
    return id != PI_SETUP || s_st != ST_RUN;
}
static void item_set(int id, int v)
{
    if (id == PI_SETUP) {
        s_st = ST_SETUP;
    } else if (id == PI_SOUND) {
        s_sound = v;
    } else if (id == PI_LANG) {
        s_lang = v;
    }
}
static void item_opened(void)
{
    if (s_st == ST_RUN) {
        s_st = ST_PAUSE;
    }
}  // (the panel pauses the clock)
static const pitems_t PANEL = {ITEMS, 3, item_shown, NULL, item_set, item_opened, NULL};

// ---------------------------------------------------------------- enter, update, input
static void enter(int64_t now)
{
    s_now = s_last = now;
    layout();
    s_lang = store_get("cc_lang", 1) ? 1 : 0;
    s_sound = store_get("cc_snd", 1) ? 1 : 0;
    s_pre = (int)(store_get("cc_pre", 2) % (NPRE + 1));
    s_mode = (int)(store_get("cc_mode", MODE_FISCHER) % MODE_N);
    s_own_min = (int)store_get("cc_omin", 5);
    s_own_inc = (int)store_get("cc_oinc", 3);
    if (s_own_min < 1 || s_own_min > 180) {
        s_own_min = 5;
    }
    if (s_own_inc < 0 || s_own_inc > 60) {
        s_own_inc = 3;
    }
    reset_clock();
    sysui_panel_items(&PANEL);
}

static void update(int64_t now)
{
    int64_t dt = now - s_last;
    s_last = s_now = now;
    if (dt < 0) {
        dt = 0;
    }
    if (s_st == ST_PAUSE) {
        s_move_t0 += dt;  // (a pause is not a part of the move)
    }
    if (s_st != ST_RUN || s_turn < 0) {
        return;
    }
    s_left[s_turn] -= dt;
    if (s_left[s_turn] <= 0) {
        s_left[s_turn] = 0;
        s_flag = s_turn;
        s_st = ST_FLAG;
        if (s_sound) {
            sfx_tone(880, 440, 500, W_SQUARE, 50);
            sfx_tone(660, 330, 700, W_SQUARE, 40);
        }
        return;
    }
    int sec = (int)(s_left[s_turn] / 1000);
    if (sec < 10 && sec != s_tick_sec) {  // a soft tick each second under 10 s
        s_tick_sec = sec;
        beep(1400, 18, 14);
    }
}

static void input(const input_event_t *ev)
{
    if (s_st == ST_SETUP) {
        if (ev->kind == IN_TAP) {
            setup_tap(setup_at(ev->x, ev->y));
        }
        return;
    }
    int m = mid_at(ev->x, ev->y);
    if (m < 0) {
        // a half: the move ends at the touch (as the button of a real clock); the half is the whole key
        if (ev->kind == IN_DOWN) {
            half_tap(s_rnd ? (ev->y >= s_h / 2 ? 0 : 1) : (ev->x < s_w / 2 ? 0 : 1));
        }
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (m == 0) {
        if (s_st == ST_RUN) {
            s_st = ST_PAUSE;
            sfx_ui(SFX_TICK);
        } else if (s_st == ST_PAUSE) {
            s_st = ST_RUN;
            sfx_ui(SFX_TICK);
        }
    } else if (s_st == ST_READY) {
        s_st = ST_SETUP;
        sfx_ui(SFX_TICK);
    } else if (s_st == ST_PAUSE || s_st == ST_FLAG) {
        if (s_now < s_again_until) {
            reset_clock();
            sfx_ui(SFX_BACK);
        } else {
            s_again_until = s_now + 2500;
            sfx_ui(SFX_TICK);
        }
    }
}

// ---------------------------------------------------------------- the drawing
// a time: m:ss (or h:mm:ss); under 10 s s.t (the tenths); the width in px
static void time_text(int64_t ms, char *out, int n, bool *tenths)
{
    *tenths = ms < 10000;
    if (*tenths) {
        int64_t t = (ms + 99) / 100;  // (up to the next tenth: 0.0 only at the flag)
        snprintf(out, (size_t)n, "%d%d", (int)(t / 10), (int)(t % 10));
        return;
    }
    int64_t s = (ms + 999) / 1000;
    if (s >= 3600) {
        snprintf(out, (size_t)n, "%d:%02d:%02d", (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    } else {
        snprintf(out, (size_t)n, "%d:%02d", (int)(s / 60), (int)(s % 60));
    }
}
// the font steps (big to small, with a tighter spacing as a step of its own): the first in which the text fits
typedef struct {
    const mfont_t *f;
    int sp;
} face_t;
static const face_t FACES[] = {{&CK_XL, 0}, {&CK_XL, -6}, {&CK_L, 0}, {&CK_L, -4}, {&CK_M, 0}};
#define NFACE 5
static void draw_time(int cx, int base, int maxw, int64_t ms, uint16_t col)
{
    char t[16];
    bool ten;
    time_text(ms, t, sizeof t, &ten);
    if (ten) {
        // "9.8": the whole seconds, a dot, the tenth (the faces have no dot: a round one)
        char a[8], b[2] = {t[strlen(t) - 1], 0};
        snprintf(a, sizeof a, "%.*s", (int)strlen(t) - 1, t);
        for (int k = 0; k < NFACE; k++) {
            const mfont_t *f = FACES[k].f;
            int sp = FACES[k].sp, dot = f->asc / 6 + 1;
            int wa = mf_width(f, a, sp), wb = mf_width(f, b, sp), tw = wa + dot * 3 + wb;
            if (tw > maxw && k < NFACE - 1) {
                continue;
            }
            int x = cx - tw / 2;
            mf_text(f, x, base, a, col, 255, sp);
            ui_circle(x + wa + dot * 3 / 2, base - dot, dot, col, 256);
            mf_text(f, x + wa + dot * 3, base, b, col, 255, sp);
            return;
        }
    }
    for (int k = 0; k < NFACE; k++) {
        if (mf_width(FACES[k].f, t, FACES[k].sp) <= maxw || k == NFACE - 1) {
            mf_text_c(FACES[k].f, cx, base, t, col, 255, FACES[k].sp);
            return;
        }
    }
}

// turn a part of the frame by 180 degrees (the half of the far player on a flat board)
static void rot180(int x0, int y0, int w, int h)
{
    for (int y = 0; y < (h + 1) / 2; y++) {
        uint16_t *a = g_fb + (size_t)(y0 + y) * gfx_w + x0, *b = g_fb + (size_t)(y0 + h - 1 - y) * gfx_w + x0;
        int n = (y == h - 1 - y) ? w / 2 : w;
        for (int x = 0; x < n; x++) {
            uint16_t t = a[x];
            a[x] = b[w - 1 - x];
            b[w - 1 - x] = t;
        }
    }
}

// (round) the half of player p: 0 = the bottom, 1 = the top, drawn as the bottom and then turned by 180 degrees; the
// texts follow the circle (the row next to the band is wide, the cap is narrow)
static void draw_half_round(int p)
{
    int hh = (s_h - s_mid) / 2, y0 = p ? 0 : s_h - hh;
    bool on = s_turn == p && (s_st == ST_RUN || s_st == ST_PAUSE), low = s_left[p] < 10000;
    uint16_t bg = s_flag == p ? rgb(150, 26, 30)
                  : on        ? (s_st == ST_PAUSE ? rgb(70, 74, 50) : rgb(26, 104, 64))
                              : rgb(22, 24, 36);
    gfx_fill_rect(0, y0, s_w, hh, bg);
    uint16_t tc = s_flag == p            ? rgb(255, 255, 255)
                  : (low && s_turn == p) ? rgb(255, 90, 80)
                  : on                   ? rgb(255, 255, 255)
                                         : rgb(160, 168, 190);
    // (the rows from the band: the time control, the time, the moves; each row as the bottom half: rel from the band)
    int band = s_h / 2 + s_mid / 2, k = s_w;
    int r_ctl = 34 * k / 466, r_time = 124 * k / 466, r_mov = 166 * k / 466;
    char top[48];
    if (ctl_inc()) {
        snprintf(top, sizeof top, "%d+%d %s", ctl_min(), ctl_inc(),
                 s_lang ? MODE_NAME[s_mode][1] : MODE_NAME[s_mode][0]);
    } else {
        snprintf(top, sizeof top, "%d+0", ctl_min());
    }
    int dy = y0 - band;  // (the half of the top player is drawn at its own place, the rows the same)
    ui_text_c(&KF_NAME, s_w / 2, band + r_ctl + dy, top, rgb(150, 160, 190));
    draw_time(s_w / 2, band + r_time + dy, 2 * chord_half(band + r_time) - 24, s_left[p], tc);
    char line[64];
    if (s_flag == p) {
        snprintf(line, sizeof line, "%s", T("TIME OVER", "הזמן נגמר"));
    } else if (s_st == ST_READY) {
        snprintf(line, sizeof line, "%s", T("TAP TO START", "נגיעה להתחלה"));
    } else {
        snprintf(line, sizeof line, s_lang ? "מסעים %d" : "MOVES %d", s_moves[p]);
    }
    ui_text_c(&KF_TEXT, s_w / 2, band + r_mov + dy, line, s_flag == p ? rgb(255, 255, 255) : rgb(190, 196, 215));
    if (p == 1) {
        rot180(0, 0, s_w, hh);
    }
}

static void draw_half(int p)
{
    if (s_rnd) {
        draw_half_round(p);
        return;
    }
    int x, w;
    half_rect(p, &x, &w);
    bool on = s_turn == p && (s_st == ST_RUN || s_st == ST_PAUSE), low = s_left[p] < 10000;
    uint16_t bg = s_flag == p ? rgb(150, 26, 30)
                  : on        ? (s_st == ST_PAUSE ? rgb(70, 74, 50) : rgb(26, 104, 64))
                              : rgb(22, 24, 36);
    gfx_fill_rect(x, 0, w, s_h, bg);
    uint16_t tc = s_flag == p            ? rgb(255, 255, 255)
                  : (low && s_turn == p) ? rgb(255, 90, 80)
                  : on                   ? rgb(255, 255, 255)
                                         : rgb(160, 168, 190);
    int base = s_h >= 300 ? s_h / 2 + 34 : s_h >= 200 ? s_h / 2 + 14 : s_h / 2 + 26;
    draw_time(x + w / 2, base, w - 8, s_left[p], tc);
    char line[64];
    const kfont_t *sf = s_h >= 300 ? &KF_BIG : &KF_LABEL;
    bool narrow = w < 200;
    if (s_flag == p) {
        snprintf(line, sizeof line, "%s",
                 narrow ? T("TIME OVER", "הזמן נגמר") : T("FLAG: THE TIME IS OVER", "הדגל נפל: הזמן נגמר"));
    } else if (s_st == ST_READY) {
        snprintf(line, sizeof line, "%s",
                 narrow ? T("TAP TO START", "נגיעה להתחלה")
                        : T("TAP: THE OTHER CLOCK STARTS", "נגיעה: השעון השני מתחיל"));
    } else {
        snprintf(line, sizeof line, s_lang ? "מסעים %d" : "MOVES %d", s_moves[p]);
    }
    // (a line wider than its half takes the next smaller face: the big face of the Tab5 was cut at both sides)
    static const kfont_t *const LADDER[] = {&KF_BIG, &KF_TEXT, &KF_NAME, &KF_LABEL};
    const kfont_t *lf = sf;
    for (int k = 0; k < 4 && ui_text_w(lf, line) > w - 12; k++) {
        if (LADDER[k]->asc < lf->asc) {
            lf = LADDER[k];
        }
    }
    ui_text_c(lf, x + w / 2, s_h - (s_h >= 300 ? 26 : 10), line, s_flag == p ? rgb(255, 255, 255) : rgb(190, 196, 215));
    char top[48];
    if (ctl_inc()) {
        snprintf(top, sizeof top, "%d+%d %s", ctl_min(), ctl_inc(),
                 s_lang ? MODE_NAME[s_mode][1] : MODE_NAME[s_mode][0]);
    } else {
        snprintf(top, sizeof top, "%d+0", ctl_min());
    }
    ui_text_c(sf, x + w / 2, s_h >= 300 ? 34 : 16, top, rgb(150, 160, 190));
    if (p == 1 && flat()) {
        rot180(x, 0, w, s_h);
    }
}

static void draw_mid(void)
{
    int mx = (s_w - s_mid) / 2;
    if (s_rnd) {
        gfx_fill_rect(0, (s_h - s_mid) / 2, s_w, s_mid, rgb(10, 11, 18));
    } else {
        gfx_fill_rect(mx, 0, s_mid, s_h, rgb(10, 11, 18));
    }
    for (int b = 0; b < 2; b++) {
        int y, h;
        mid_btn(b, &y, &h);
        bool again = b == 1 && s_now < s_again_until;
        int cx = mx + s_mid / 2, cy = y + h / 2;
        if (s_rnd) {  // (two discs in the band, left and right of the middle)
            cx = s_w / 2 + (b ? 1 : -1) * s_w * 64 / 466;
            ui_circle(cx, cy, h / 2, again ? rgb(200, 60, 50) : rgb(44, 50, 78), 256);
        } else {
            ui_rrect(mx + 4, y, s_mid - 8, h, 10, again ? rgb(200, 60, 50) : rgb(44, 50, 78));
        }
        uint16_t c = rgb(235, 238, 250);
        if (b == 0) {
            if (s_st == ST_RUN) {  // pause: two bars
                gfx_fill_rect(cx - 9, cy - 12, 6, 24, c);
                gfx_fill_rect(cx + 3, cy - 12, 6, 24, c);
            } else if (s_st == ST_PAUSE) {  // go on: a triangle (the same both ways: a round mark)
                ui_circle(cx, cy, 11, rgb(80, 200, 120), 256);
            } else {
                ui_circle(cx, cy, 4, rgb(90, 96, 120), 256);
            }
        } else if (s_st == ST_READY) {  // setup: three lines (a list)
            for (int k = -1; k <= 1; k++) {
                gfx_fill_rect(cx - 11, cy + k * 8 - 1, 22, 3, c);
            }
        } else if (s_st == ST_PAUSE || s_st == ST_FLAG) {  // reset: a ring
            ui_ring(cx, cy, 11, 3, again ? rgb(255, 255, 255) : c, 256);
            if (again) {
                ui_text_c(&KF_LABEL, cx, y + h - (s_rnd ? 10 : 6), "?", rgb(255, 255, 255));
            }
        }
    }
}

static void draw_setup(void)
{
    gfx_fill(rgb(14, 16, 26));
    if (s_rnd) {  // (the title in the top cap: two short lines)
        ui_text_c(&KF_BIG, s_w / 2, s_w * 48 / 466, T("CHESS CLOCK", "שעון שחמט"), rgb(250, 215, 120));
        ui_text_c(&KF_TEXT, s_w / 2, s_w * 74 / 466, T("THE TIME CONTROL", "קצב המשחק"), rgb(190, 170, 110));
    } else {
        ui_text_c(&KF_NAME, s_w / 2, s_w >= 600 ? 18 : 20, T("CHESS CLOCK: THE TIME CONTROL", "שעון שחמט: קצב המשחק"),
                  rgb(250, 215, 120));
    }
    for (int i = 0; i <= 12; i++) {
        if (s_pre != NPRE && i >= 8 && i <= 11) {
            continue;
        }
        int x, y, w, h;
        setup_btn(i, &x, &y, &w, &h);
        bool sel = i < 7 && i == s_pre;
        uint16_t bg = i == 12 ? rgb(40, 140, 80) : sel ? rgb(250, 215, 30) : rgb(40, 46, 74);
        ui_rrect(x, y, w, h, 8, bg);
        char t[40];
        const kfont_t *f = &KF_NAME;
        if (i < NPRE) {
            snprintf(t, sizeof t, "%d+%d", PRE[i].min, PRE[i].inc);
        } else if (i == NPRE) {
            snprintf(t, sizeof t, s_lang ? "שלי %d+%d" : "OWN %d+%d", s_own_min, s_own_inc);
        } else if (i == 7) {
            snprintf(t, sizeof t, "%s", s_lang ? MODE_NAME[s_mode][1] : MODE_NAME[s_mode][0]);
        } else if (i >= 8 && i <= 11) {
            snprintf(t, sizeof t, "%s %c", i < 10 ? T("MIN", "דקות") : T("INC", "תוספת"), i % 2 ? '+' : '-');
            f = s_rnd ? &KF_TEXT : &KF_LABEL;
        } else {
            snprintf(t, sizeof t, "%s", T("START", "התחל"));
            f = &KF_BIG;
        }
        if (s_rnd && f == &KF_NAME && ui_text_w(&KF_BIG, t) <= w - 10) {
            f = &KF_BIG;  // (round: the big rows have room)
        }
        if (ui_text_w(f, t) > w - 6) {
            f = &KF_LABEL;
        }
        ui_text_c(f, x + w / 2, y + h / 2 + f->asc / 2 - 1, t, sel ? rgb(0, 0, 0) : rgb(240, 242, 250));
    }
}

static void render(int64_t now)
{
    s_now = now;
    if (s_st == ST_SETUP) {
        draw_setup();
        return;
    }
    draw_half(0);
    draw_half(1);
    draw_mid();
}

#include "tficon/tficon.h"
// the menu icon: a chess clock: the hand of one side turns, then its player presses the button and the other side runs
static void cc_icon_face(float cx, float cy, float r, float a, bool run, float u)
{
    tfi_disc(cx, cy, r + 2.5f * u, tfi_hex(0x3a3f4b), tfi_hex(0x1a1d24), 256);
    tfi_disc(cx, cy, r, run ? rgb(255, 255, 255) : tfi_hex(0xddd8cc), run ? rgb(255, 255, 255) : tfi_hex(0xddd8cc),
             256);
    for (int k = 0; k < 4; k++) {
        tfi_disc(cx + fcos(k * 1.5708f) * (r - 3 * u), cy + fsin(k * 1.5708f) * (r - 3 * u), 1.2f * u,
                 tfi_hex(0x3a3f4b), tfi_hex(0x3a3f4b), 256);
    }
    tfi_line(cx, cy, cx + fcos(a) * (r - 4 * u), cy + fsin(a) * (r - 4 * u), 2.4f * u, true,
             run ? tfi_hex(0xe8301c) : tfi_hex(0x2a2e38), 256);
    tfi_disc(cx, cy, 2 * u, tfi_hex(0x2a2e38), tfi_hex(0x2a2e38), 256);
}
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0xffcf6b), tfi_hex(0xc26a00));
    float u = s / 100.0f, ph = tfi_ph(2);
    bool left = ph < 0.5f;
    float q = left ? ph * 2 : ph * 2 - 1;
    // the buttons (the side that runs has its button up)
    tfi_rbox(x + (int)(22 * u), y + (int)((left ? 24 : 28) * u), (int)(16 * u), (int)(8 * u), (int)(3 * u),
             tfi_hex(0xff6a4a), tfi_hex(0xb02010), 256);
    tfi_rbox(x + (int)(62 * u), y + (int)((left ? 28 : 24) * u), (int)(16 * u), (int)(8 * u), (int)(3 * u),
             tfi_hex(0x5ab0ff), tfi_hex(0x1a5ad0), 256);
    // the body
    int bx = x + (int)(12 * u), by = y + (int)(32 * u), bw = (int)(76 * u), bh = (int)(48 * u);
    tfi_drop(bx, by, bw, bh, (int)(8 * u), (int)(3 * u), 90);
    tfi_gloss_box(bx, by, bw, bh, (int)(8 * u), tfi_hex(0x8a5a30), tfi_hex(0x4a2a10));
    cc_icon_face(x + 31 * u, y + 56 * u, 14 * u, -1.5708f + (left ? q : 1) * 1.6f, left, u);
    cc_icon_face(x + 69 * u, y + 56 * u, 14 * u, -1.5708f + (left ? 0 : q) * 1.6f, !left, u);
    tfi_end();
}

static bool awake(void)
{
    return s_st == ST_RUN;
}

const app_t app_chessclock = {
        .name = "CHESS CLOCK",
        .landscape = true,
        .enter = enter,
        .update = update,
        .input = input,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
        .run = true,  // (the panel: the clock pauses itself through item_opened)
        .round = APP_ROUND_HI,
};
