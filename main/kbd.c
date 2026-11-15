// On-screen keyboard: a text line with CANCEL / SHOW / OK, then 4 rows of 10 keys.
// The bar screen (640 x 172) has the text and the buttons on one line. The 320 x 240
// screen has the text line, then a line of buttons, then the keys.
//
// THE KEY PREVIEW: after 0.2 s a zoom-in copy of the pressed key shows. The user can slide a fat finger across the
// keys, and the key under the finger at the release is the chosen key. There is no loupe (a loupe needs a first tap to
// open big keys and a second tap to type, which is slow).
//   1. A finger on a key lights the key at once. When the finger stays 0.2 s (PV_DELAY_MS), a big copy of the key
//      shows above it (THE PREVIEW), joined to the key by a stem, so that the thumb does not hide it.
//   2. The finger can slide over the keys: the lit key and the preview follow it. A small margin (PV_HYST px) keeps
//      the key at a border, so that the preview does not flicker between two keys.
//   3. The key under the finger when it comes off the glass is typed (also for a quick tap, before the preview).
//      A finger that slides off the keys (up into the text line) types nothing.
//   4. OK asks "<the question>?" and CANCEL (with typed text) asks "DROP THE TEXT?": NO at the left, YES at the right.
//      CANCEL, SHOW and OK act on a tap that starts and ends on them.
//   5. A long press on DEL does not clear the text (a slow tap of 350 ms is not a command).
// Every keyboard of the firmware is this one (SETUP > WIFI, FILES, FRIENDS, HOME 2.0).
// HEBREW (WIKI): kbd_set_hebrew(true) after kbd_open() adds the Hebrew letters (the Israeli layout, with the
// final letters): the SYM key goes abc -> HEB -> #+= -> abc. The text is UTF-8: DEL takes one whole letter, and Hebrew
// shows in the Rubik font (uikit). kbd_set_ok_question("") makes OK act at once (a search: no question).

#include <math.h>
#include <stdio.h>
#include "tfn/theme.h"
#include <string.h>
#include "audio.h"
#include "board.h"
#include "gfx.h"
#include "kbd.h"
#include "kfont.h"
#include "uikit.h"

#if BOARD_BAR_SCREEN
#define KB_W    640
#define KB_H    172
#define KEY_W   64
#define KEY_H   32
#define TOP_H   44  // the top line: CANCEL, the text, SHOW, OK (44 px high)
#define KEY_GAP 2   // the space at each side of a key face
#define BTN_Y   0
#define BTN_H   44
#define CAN_X   0  // CANCEL at the far left, OK at the far right
#define CAN_W   100
#define SHOW_X  444
#define SHOW_W  92
#define OK_X    542
#define OK_W    98
#define FIELD_X 106
#define FIELD_W 332
#define FIELD_Y 16
#define PV_W    88  // the preview: 88 x 56 px (11.7 x 7.4 mm); over the top row it is 40 px high
#define PV_H    56
#define PV_HYST 4
#elif BOARD_UI_TABLET
// the Tab5 frame 640 x 360 (0.17 mm a px): the layout of the 320 x 240 frame, 2 times as wide and about
// 1.5 times as high. The keys 64 x 60 px (11 x 10 mm: a thumb), bigger text and buttons.
#define KB_W    640
#define KB_H    360
#define KEY_W   64
#define KEY_H   60
#define TOP_H   118  // the text line (y 20 .. 48) and the buttons (y 54 .. 114)
#define KEY_GAP 2
#define BTN_Y   54
#define BTN_H   60
#define CAN_X   0
#define CAN_W   200
#define SHOW_X  220
#define SHOW_W  200
#define OK_X    440
#define OK_W    200
#define FIELD_X 6
#define FIELD_W 628
#define FIELD_Y 20
#define PV_W    96  // the preview: 96 x 84 px (16 x 14 mm)
#define PV_H    84
#define PV_HYST 5
#define KB_BIG  1  // the text scales: one step bigger
#elif BOARD_ROUND
// The round panels: in the native round frame of an app (APP_ROUND_HI: 466 x 466, 412 on the Watcher; KR() scales the
// values of 466) the title and the text are on the top cap, the 4 rows of keys go across the wide middle of the circle
// (KEY_X0 .. KEY_X0 + 10 keys), CANCEL, SHOW and OK are on the bottom cap, and the text uses the smooth fonts. In the
// 320 x 240 frame (an app with round_fit) the keyboard is the one of that frame (the values after ':').
#define KB_RND  gfx_round()
#define KR(v)   ((v) * gfx_w / 466)
#define KB_W    (KB_RND ? gfx_w : 320)
#define KB_H    (KB_RND ? gfx_h : 240)
#define KEY_W   (KB_RND ? KR(38) : 32)
#define KEY_H   (KB_RND ? KR(52) : 39)
#define KEY_X0  (KB_RND ? (KB_W - 10 * KEY_W) / 2 : 0)
#define TOP_H   (KB_RND ? KR(150) : 82)  // the top of the keys
#define KEY_GAP (KB_RND ? 2 : 1)
#define BTN_Y   (KB_RND ? KR(370) : 37)  // the buttons: the bottom cap
#define BTN_H   (KB_RND ? KR(54) : 44)
#define CAN_X   0  // (the round frame: round_btn() has the places of the buttons)
#define CAN_W   100
#define SHOW_X  110
#define SHOW_W  100
#define OK_X    220
#define OK_W    100
#define FIELD_X (KB_RND ? KR(63) : 4)
#define FIELD_W (KB_RND ? KR(340) : 312)
#define FIELD_Y (KB_RND ? KR(84) : 14)
#define PV_W    (KB_RND ? KR(64) : 60)
#define PV_H    (KB_RND ? KR(62) : 56)
#define PV_HYST (KB_RND ? 4 : 3)
#else
#define KB_W    320
#define KB_H    240
#define KEY_W   32
#define KEY_H   39
#define TOP_H   82  // the text line (y 14 .. 34) and the buttons (y 37 .. 81)
#define KEY_GAP 1
#define BTN_Y   37
#define BTN_H   44
#define CAN_X   0
#define CAN_W   100
#define SHOW_X  110
#define SHOW_W  100
#define OK_X    220
#define OK_W    100
#define FIELD_X 4
#define FIELD_W 312  // the text field: x 4 .. 316
#define FIELD_Y 14
#define PV_W    60  // the preview: 60 x 56 px (CoreS3 7.7 x 7.2 mm)
#define PV_H    56
#define PV_HYST 3
#endif
#ifndef KB_BIG
#define KB_BIG 0
#endif
#ifndef KEY_X0
#define KEY_X0 0  // the left end of the keys
#endif
#define PV_DELAY_MS 200  // the preview shows when the finger stays this long
#define PV_GAP      4    // between the preview and its key (the stem)

enum {
    M_LOWER,
    M_UPPER,
    M_SYM1,
    M_SYM2,
    M_HEB
};

// special keys in the layout strings
#define K_SHIFT '\x01'
#define K_DEL   '\x02'
#define K_SYM   '\x03'
#define K_SPACE '\x04'

static const char *const ROWS[4][4] = {
        [M_LOWER] = {"1234567890", "qwertyuiop", "asdfghjkl\x02", "\x01zxcvbnm\x03\x04"},
        [M_UPPER] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL\x02", "\x01ZXCVBNM\x03\x04"},
        [M_SYM1] = {"1234567890", "!@#$%^&*()", "-_=+/:;'\"\x02", "\x01,.?<>|~\x03\x04"},
        [M_SYM2] = {"1234567890", "[]{}\\`$#@&", "!?.,;:'\"_\x02", "\x01+-*/=%^\x03\x04"},
};

// the Hebrew set: one UTF-8 string a key (the special keys as one byte: DEL 0x02, SYM 0x03, SPACE 0x04)
static const char *const HEB[4][10] = {
        {"/", "'", "ק", "ר", "א", "ט", "ו", "ן", "ם", "פ"},
        {"ש", "ד", "ג", "כ", "ע", "י", "ח", "ל", "ך", "ף"},
        {"ז", "ס", "ב", "ה", "נ", "מ", "צ", "ת", "ץ", "\x02"},
        {",", ".", "-", "\"", "?", "\x04", "\x04", "\x04", "\x04", "\x03"},
};
static bool s_heb_ok;  // kbd_set_hebrew(): the Hebrew set is on the SYM key

static const char *s_title;
static char *s_buf;
static int s_max;
static bool s_secret, s_show;
static int s_mode;
static int s_press_r = -1, s_press_c = -1, s_press_top = -1;  // the key under the finger; the top button touched
static int s_focus = -1;  // the wheel: 0..39 a key (row by row), 40 OK, 41 SHOW, 42 CANCEL; -1 = not shown
// one touch
static bool s_key_touch;  // the touch started on the keys (its release types the key under the finger)
static int64_t s_down_t;  // when it started (the preview comes PV_DELAY_MS later)
// the question of OK / CANCEL
static int s_ask;  // 0 = none, 1 = OK, 2 = CANCEL
static int s_ask_hot = -1, s_ask_focus = -1;
static const char *s_okq;
static char s_start[72];  // the text at the open (CANCEL asks only when the text changed)

void kbd_open(const char *title, char *buf, int max, bool secret)
{
    s_title = title;
    s_buf = buf;
    s_max = max;
    s_secret = secret;
    s_show = false;
    s_mode = M_LOWER;
    s_focus = -1;
    s_key_touch = false;
    s_ask = 0;
    s_ask_hot = s_ask_focus = -1;
    s_okq = NULL;
    s_heb_ok = false;
    s_press_r = s_press_c = s_press_top = -1;
    snprintf(s_start, sizeof s_start, "%s", buf ? buf : "");
}

void kbd_set_hebrew(bool on)
{
    s_heb_ok = on;
    if (on) {
        s_mode = M_HEB;  // (a Hebrew search starts with the Hebrew letters)
    }
}

// the key at a row and a column as a string (a special key: one byte under ' ')
static const char *key_str(int r, int c)
{
    static char one[2];
    if (s_mode == M_HEB) {
        return HEB[r][c];
    }
    one[0] = ROWS[s_mode][r][c];
    one[1] = 0;
    return one;
}
static bool ascii_only(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s >= 0x80) {
            return false;
        }
    }
    return true;
}

void kbd_set_ok_question(const char *q)
{
    s_okq = q;
}

int kbd_state(void)
{
    return s_ask ? 2 : (s_key_touch && s_press_r >= 0) ? 1 : 0;
}

// ---------------------------------------------------------------- the question page

#if BOARD_BAR_SCREEN
#define ASK_Y 56
#define ASK_W 220  // NO: x 0 .. 220, YES: x 420 .. 640
#elif BOARD_UI_TABLET
#define ASK_Y 130
#define ASK_W 250  // NO: x 0 .. 250, YES: x 390 .. 640
#elif BOARD_ROUND
#define ASK_Y (KB_RND ? KR(226) : 96)
#define ASK_H KR(110)  // (the round frame: NO and YES side by side in the middle of the circle)
#define ASK_W (KB_RND ? KR(183) : 124)
#else
#define ASK_Y 96
#define ASK_W 124  // NO: x 0 .. 124, YES: x 196 .. 320
#endif

int kbd_ask_at(int x, int y)
{
#if BOARD_ROUND
    if (KB_RND) {
        if (y < ASK_Y - 6 || y >= ASK_Y + ASK_H + 6) {
            return -1;
        }
        return x < KB_W / 2 ? 0 : 1;
    }
#endif
    if (y < ASK_Y) {
        return -1;
    }
    if (x < ASK_W) {
        return 0;
    }
    if (x >= KB_W - ASK_W) {
        return 1;
    }
    return -1;
}

static void ask_line(int y, const char *s, int big, uint16_t c)
{
    if (!s || !s[0]) {
        return;
    }
    if (!ascii_only(s)) {
        ui_text_c(big >= 2 ? &KF_BIG : &KF_NAME, KB_W / 2, y + (big >= 2 ? 20 : 14), s, c);
        return;
    }
    int sc = gfx_text_width(s, big) <= KB_W - 8 ? big : 1;
    gfx_text_center_in(0, KB_W, y, s, sc, c);
}

#if BOARD_ROUND
// The question on a round panel: the two lines over the middle, NO and YES side by side.
static void ask_render_round(const char *l1, const char *l2, int hot, int focus)
{
    gfx_fill(THEME_KBD_BG);
    static const kfont_t *const F1[] = {&KF_BIG, &KF_TEXT, &KF_NAME}, *const F2[] = {&KF_TEXT, &KF_NAME, &KF_LABEL};
    if (l1 && l1[0]) {
        ui_text_c(ui_text_fit(F1, 3, l1, KR(340)), KB_W / 2, KR(150), l1, THEME_KBD_ASK_TEXT);
    }
    if (l2 && l2[0]) {
        ui_text_c(ui_text_fit(F2, 3, l2, KR(380)), KB_W / 2, KR(190), l2, THEME_KBD_ASK_TEXT_DIM);
    }
    for (int a = 0; a < 2; a++) {
        int x = a ? KB_W / 2 + KR(5) : KB_W / 2 - KR(5) - ASK_W;
        bool on = hot == a;
        uint16_t bg = on ? THEME_KBD_HOT : (a ? THEME_KBD_ASK_YES : THEME_KBD_ASK_NO);
        ui_rbox(x, ASK_Y, ASK_W, ASK_H, KR(16), bg, focus == a ? THEME_KBD_FOCUS : THEME_KBD_ASK_EDGE,
                focus == a ? 3 : 1);
        ui_text_c(&KF_TITLE, x + ASK_W / 2, ASK_Y + ASK_H / 2 + KF_TITLE.asc / 2, a ? "YES" : "NO",
                  on ? THEME_KBD_TEXT_HOT : THEME_KBD_TEXT);
    }
}
#endif

void kbd_ask_render(const char *l1, const char *l2, int hot, int focus)
{
#if BOARD_ROUND
    if (KB_RND) {
        ask_render_round(l1, l2, hot, focus);
        return;
    }
#endif
    gfx_fill(THEME_KBD_BG);
#if BOARD_BAR_SCREEN
    ask_line(8, l1, 2, THEME_KBD_ASK_TEXT);
    ask_line(32, l2, 1, THEME_KBD_ASK_TEXT_DIM);
#elif KB_BIG
    ask_line(24, l1, 3, THEME_KBD_ASK_TEXT);
    ask_line(76, l2, 2, THEME_KBD_ASK_TEXT_DIM);
#else
    ask_line(16, l1, 2, THEME_KBD_ASK_TEXT);
    ask_line(44, l2, 1, THEME_KBD_ASK_TEXT_DIM);
#endif
    for (int a = 0; a < 2; a++) {
        int x = a ? KB_W - ASK_W : 0, w = ASK_W, y = ASK_Y + 4, h = KB_H - ASK_Y - 4;
        bool on = hot == a;
        uint16_t bg = on ? THEME_KBD_HOT : (a ? THEME_KBD_ASK_YES : THEME_KBD_ASK_NO);
        gfx_fill_rect(x + 2, y, w - 4, h - 2, bg);
        gfx_frame_rect(x + 2, y, w - 4, h - 2, 1, THEME_KBD_ASK_EDGE);
        gfx_text_center_in(x, w, y + h / 2 - 7 * (3 + KB_BIG) / 2, a ? "YES" : "NO", 3 + KB_BIG,
                           on ? THEME_KBD_TEXT_HOT : THEME_KBD_TEXT);
        if (focus == a) {
            gfx_frame_rect(x + 2, y, w - 4, h - 2, 3, THEME_KBD_FOCUS);
        }
    }
}

// ---------------------------------------------------------------- the keys

static bool key_at(int x, int y, int *r, int *c)
{
    if (y < TOP_H) {
        return false;
    }
#if BOARD_ROUND
    if (KB_RND && y >= TOP_H + 4 * KEY_H + 4) {
        return false;  // (the round frame: the buttons are under the keys)
    }
#endif
    *r = (y - TOP_H) / KEY_H;
    *c = (x - KEY_X0) / KEY_W;
    if (*r > 3) {
        *r = 3;  // (the 320 x 240 frame: 2 px under the last row)
    }
    if (*c < 0) {
        *c = 0;
    }
    if (*c > 9) {
        *c = 9;
    }
    return *r >= 0;
}

// the key under a finger that slides: the key that is lit now stays while the finger is less than PV_HYST px past
// its border (no flicker at a border)
static void track_key(int x, int y)
{
    if (s_press_r >= 0) {
        int kx = KEY_X0 + s_press_c * KEY_W, ky = TOP_H + s_press_r * KEY_H;
        if (x >= kx - PV_HYST && x < kx + KEY_W + PV_HYST && y >= ky - PV_HYST && y < ky + KEY_H + PV_HYST) {
            return;
        }
    }
    int r, c;
    if (key_at(x, y, &r, &c)) {
        s_press_r = r;
        s_press_c = c;
    } else {
        s_press_r = s_press_c = -1;  // off the keys (up into the text line): the release types nothing
    }
}

#if BOARD_ROUND
// the buttons of the bottom cap: CANCEL, (SHOW), OK side by side in the middle (i = 0, 1, 2)
static void round_btn(int i, int *x, int *w)
{
    int n = s_secret ? 3 : 2, bw = s_secret ? KR(82) : KR(108), g = KR(5), x0 = KB_W / 2 - (n * bw + (n - 1) * g) / 2;
    int k = i == 0 ? 0 : (i == 1 ? 1 : n - 1);
    *x = x0 + k * (bw + g);
    *w = bw;
}
#endif

// top buttons: 0 = CANCEL, 1 = SHOW, 2 = OK
static int top_at(int x, int y)
{
#if BOARD_ROUND
    if (KB_RND) {
        if (y < BTN_Y - 4 || y >= BTN_Y + BTN_H + 8) {
            return -1;
        }
        for (int i = 0; i < 3; i++) {
            int bx, bw;
            round_btn(i, &bx, &bw);
            if ((i != 1 || s_secret) && x >= bx - 3 && x < bx + bw + 3) {
                return i;
            }
        }
        return -1;
    }
#endif
#if BOARD_BAR_SCREEN
    if (y >= TOP_H) {
        return -1;
    }
#else
    if (y < BTN_Y - 2 || y >= TOP_H) {
        return -1;
    }
#endif
    if (x < CAN_X + CAN_W + 3) {
        return 0;
    }
    if (s_secret && x >= SHOW_X - 3 && x < SHOW_X + SHOW_W + 3) {
        return 1;
    }
    if (x >= OK_X - 3) {
        return 2;
    }
    return -1;
}

static int press_top(int t);
static void press_key(int r, int c);

// ---------------------------------------------------------------- input

static int ask_answer(int a)
{
    int what = s_ask;
    s_ask = 0;
    s_ask_hot = s_ask_focus = -1;
    if (a != 1) {
        sfx_ui(SFX_BACK);
        return KBD_NONE;
    }
    sfx_play(what == 1 ? SFX_CLICK : SFX_BACK);
    return what == 1 ? KBD_OK : KBD_CANCEL;
}

// A key of a keyboard (IN_KEY, the Tab5 keyboard): a letter types at the end, BACKSPACE deletes one letter, ENTER = OK and
// ESC = CANCEL at once (no question: a keyboard user expects that); on the question page ENTER = YES, ESC = NO.
static int key_input(int key)
{
    if (s_ask) {
        if (key == INK_ENTER) {
            return ask_answer(1);
        }
        if (key == INK_ESC) {
            return ask_answer(0);
        }
        return KBD_NONE;
    }
    if (key == INK_ENTER) {
        sfx_play(SFX_CLICK);
        return KBD_OK;
    }
    if (key == INK_ESC) {
        sfx_ui(SFX_BACK);
        return KBD_CANCEL;
    }
    int len = (int)strlen(s_buf);
    if (key == INK_BKSP) {
        while (len > 0 && ((unsigned char)s_buf[len - 1] & 0xC0) == 0x80) {
            len--;  // (one whole UTF-8 letter)
        }
        if (len > 0) {
            len--;
        }
        s_buf[len] = 0;
        sfx_ui(SFX_TICK);
    } else if (key >= 32 && key < 127 && len + 1 < s_max) {
        s_buf[len] = (char)key;
        s_buf[len + 1] = 0;
        sfx_ui(SFX_TICK);
    }
    return KBD_NONE;
}

int kbd_input(const input_event_t *ev)
{
    if (ev->kind == IN_KEY) {
        return key_input(ev->x);
    }
    if (s_ask) {
        if (ev->kind == IN_DOWN || ev->kind == IN_MOVE) {
            s_ask_hot = kbd_ask_at(ev->x, ev->y);
        } else if (ev->kind == IN_UP) {
            s_ask_hot = -1;
        } else if (ev->kind == IN_TAP) {
            int a = kbd_ask_at(ev->x, ev->y);
            if (a >= 0) {
                return ask_answer(a);
            }
        }
        return KBD_NONE;
    }
    switch (ev->kind) {
    case IN_DOWN: {
        s_press_top = top_at(ev->x, ev->y);
        s_press_r = s_press_c = -1;
        int r, c;
        s_key_touch = s_press_top < 0 && key_at(ev->x, ev->y, &r, &c);
        if (s_key_touch) {
            s_press_r = r;
            s_press_c = c;
            s_down_t = ev->t_ms;
            if (s_focus >= 0) {
                s_focus = r * 10 + c;  // the wheel goes on from the key that the finger chose
            }
        }
        return KBD_NONE;
    }
    case IN_MOVE:
        if (s_key_touch) {
            track_key(ev->x, ev->y);
        }
        return KBD_NONE;
    case IN_UP:
        if (s_key_touch) {
            track_key(ev->x, ev->y);  // (the last point of the finger)
            int r = s_press_r, c = s_press_c;
            s_key_touch = false;
            s_press_r = s_press_c = -1;
            if (r >= 0) {
                if (s_focus >= 0) {
                    s_focus = r * 10 + c;
                }
                press_key(r, c);
            }
        } else {
            s_press_r = s_press_c = -1;
        }
        return KBD_NONE;
    case IN_TAP:
        break;
    default:
        return KBD_NONE;  // (IN_LONG: nothing; a long press on DEL does not clear the text)
    }
    // a tap: only the top buttons (the keys typed at the release); the tap must start on the same button
    int t = top_at(ev->x, ev->y), was = s_press_top;
    s_press_top = -1;
    if (t < 0 || t != was) {
        return KBD_NONE;
    }
    if (s_focus >= 0) {
        s_focus = 42 - t;
    }
    return press_top(t);
}

// the wheel: the focus goes around the 40 keys, then OK, SHOW (only for a secret) and CANCEL. On the question page it
// goes between NO and YES.
int kbd_wheel(int steps, int presses)
{
    if (s_ask) {
        if (steps) {
            s_ask_focus = s_ask_focus < 0 ? 0 : (s_ask_focus + (steps < 0 ? -steps : steps)) % 2;
            sfx_ui(SFX_TICK);
        }
        for (int i = 0; i < presses; i++) {
            if (s_ask_focus >= 0) {
                return ask_answer(s_ask_focus);
            }
        }
        return KBD_NONE;
    }
    if (steps) {
        if (s_focus < 0) {
            s_focus = 10;  // the first wheel step shows the focus on 'q'
        } else {
            for (int i = 0; i < (steps < 0 ? -steps : steps); i++) {
                s_focus = (s_focus + (steps > 0 ? 1 : 42)) % 43;
                if (s_focus == 41 && !s_secret) {
                    s_focus = steps > 0 ? 42 : 40;
                }
            }
        }
        sfx_ui(SFX_TICK);
    }
    for (int i = 0; i < presses; i++) {
        if (s_focus < 0) {
            continue;
        }
        if (s_focus >= 40) {
            int r = press_top(42 - s_focus);
            if (r != KBD_NONE || s_ask) {
                return r;
            }
        } else {
            press_key(s_focus / 10, s_focus % 10);
        }
    }
    return KBD_NONE;
}

static int press_top(int t)
{
    if (t == 1) {
        s_show = !s_show;
        sfx_ui(SFX_TICK);
        return KBD_NONE;
    }
    if (t == 0 && !strcmp(s_buf, s_start)) {
        sfx_ui(SFX_BACK);
        return KBD_CANCEL;
    }  // nothing typed: no question
    if (t == 2 && s_okq && !s_okq[0]) {
        sfx_play(SFX_CLICK);
        return KBD_OK;
    }  // ("": OK with no question)
    s_ask = t == 2 ? 1 : 2;
    s_ask_hot = -1;
    s_ask_focus = s_focus >= 0 ? 0 : -1;  // the wheel: the focus starts on NO
    sfx_ui(SFX_TICK);
    return KBD_NONE;
}

static void press_key(int r, int c)
{
    const char *ks = key_str(r, c);
    char k = ks[0];
    int len = (int)strlen(s_buf);
    switch (k) {
    case K_SHIFT:
        s_mode = s_mode == M_LOWER ? M_UPPER : (s_mode == M_UPPER ? M_LOWER : (s_mode == M_SYM1 ? M_SYM2 : M_SYM1));
        break;
    case K_SYM:
        if (s_heb_ok) {
            s_mode = s_mode == M_HEB ? M_SYM1 : (s_mode == M_SYM1 || s_mode == M_SYM2) ? M_LOWER : M_HEB;
        } else {
            s_mode = (s_mode == M_SYM1 || s_mode == M_SYM2) ? M_LOWER : M_SYM1;
        }
        break;
    case K_DEL:
        while (len > 0 && ((unsigned char)s_buf[len - 1] & 0xC0) == 0x80) {
            len--;  // (one whole UTF-8 letter)
        }
        if (len > 0) {
            len--;
        }
        s_buf[len] = 0;
        break;
    case K_SPACE:
        ks = " ";
        /* fall through */
    default:
        if (len + (int)strlen(ks) < s_max) {
            memcpy(s_buf + len, ks, strlen(ks) + 1);
        }
        // one capital, then back to small letters
        if (s_mode == M_UPPER) {
            s_mode = M_LOWER;
        }
        break;
    }
    sfx_ui(SFX_TICK);
}

// ---------------------------------------------------------------- drawing

static void top_button(int x, int w, const char *label, bool hot, bool on)
{
    gfx_fill_rect(x + 2, BTN_Y + 2, w - 4, BTN_H - 4, hot ? THEME_KBD_HOT : (on ? THEME_KBD_ON : THEME_KBD_BTN));
    gfx_text_center_in(x, w, BTN_Y + BTN_H / 2 - 7 * (2 + KB_BIG) / 2, label, 2 + KB_BIG,
                       hot ? THEME_KBD_TEXT_HOT : THEME_KBD_TEXT);
}

// the text as it shows ('*' for a secret), only its end when it is long; and the cursor
static void draw_text(int x, int y, int w, int64_t now)
{
    char shown[72];
    int len = (int)strlen(s_buf);
    if (len > (int)sizeof(shown) - 1) {
        len = (int)sizeof(shown) - 1;
    }
    for (int i = 0; i < len; i++) {
        shown[i] = (s_secret && !s_show) ? '*' : s_buf[i];
    }
    shown[len] = 0;
    const char *view = shown;
    if (!ascii_only(view)) {
        // Hebrew (UTF-8): the Rubik font; a long text loses whole letters at its start until it fits
        const kfont_t *f = KB_BIG ? &KF_BIG : &KF_NAME;
        while (*view && ui_text_w(f, view) > w - 20) {
            view++;
            while (((unsigned char)*view & 0xC0) == 0x80) {
                view++;
            }
        }
        gfx_fill_rect(x, y - 2, w, 7 * (2 + KB_BIG) + 6, THEME_KBD_FIELD);
        int tw = ui_text_w(f, view);
        ui_text_c(f, x + w - 6 - tw / 2, y + f->asc - 2, view, THEME_KBD_FIELD_TEXT);
        if ((now / 500) & 1) {
            gfx_fill_rect(x + w - 10 - tw, y, 2, 8 * (2 + KB_BIG), THEME_KBD_CURSOR);
        }
        return;
    }
    const int sc = 2 + KB_BIG;
    int maxc = (w - 16) / (6 * sc);
    if ((int)strlen(view) > maxc) {
        view += strlen(view) - maxc;
    }
    gfx_fill_rect(x, y - 2, w, 7 * sc + 6, THEME_KBD_FIELD);
    gfx_text(x + 4, y + 1, view, sc, THEME_KBD_FIELD_TEXT);
    if ((now / 500) & 1) {
        gfx_fill_rect(x + 4 + gfx_text_width(view, sc) + 2, y, 2, 8 * sc, THEME_KBD_CURSOR);
    }
}

static void key_label(char k, char *lab, int n)
{
    switch (k) {
    case K_SHIFT:
        snprintf(lab, n, "%s", s_mode >= M_SYM1 ? "1/2" : "SHIFT");
        break;
    case K_DEL:
        snprintf(lab, n, "DEL");
        break;
    case K_SYM:
        if (s_heb_ok) {
            snprintf(lab, n, "%s", s_mode == M_HEB ? "#+=" : s_mode >= M_SYM1 ? "abc" : "עב");
        } else {
            snprintf(lab, n, "%s", s_mode >= M_SYM1 ? "abc" : "#+=");
        }
        break;
    case K_SPACE:
        snprintf(lab, n, "SPACE");
        break;
    default:
        lab[0] = k;
        lab[1] = 0;
        break;
    }
}

// THE PREVIEW: a big copy of the key under the finger, above the key (over the top line for the top row), joined to
// the key by a stem. The secret text shows its key too (as on a phone): the finger needs to see what it types.
static void draw_preview(void)
{
    const char *ks = key_str(s_press_r, s_press_c);
    char k = ks[0];
    char lab[8];
    if ((unsigned char)k >= ' ') {
        snprintf(lab, sizeof lab, "%s", ks);
    } else {
        key_label(k, lab, sizeof lab);
    }
    bool special = (unsigned char)k < ' ';
    int sc = (special ? 2 : 4) + KB_BIG;
    int w = PV_W, tw = gfx_text_width(lab, sc) + 16;
    if (tw > w) {
        w = tw;
    }
    int kx = KEY_X0 + s_press_c * KEY_W, ky = TOP_H + s_press_r * KEY_H;
    int x = kx + KEY_W / 2 - w / 2;
    if (x < 0) {
        x = 0;
    }
    if (x > KB_W - w) {
        x = KB_W - w;
    }
    int bot = ky - PV_GAP, top = bot - PV_H;
    if (top < 0) {
        top = 0;
    }
    int h = bot - top;
    uint16_t bg = THEME_KBD_PREVIEW, edge = THEME_KBD_PREVIEW_EDGE;
    gfx_fill_rect(kx + KEY_GAP, bot, KEY_W - 2 * KEY_GAP, ky - bot + KEY_GAP, bg);  // the stem
    gfx_fill_rect(x, top, w, h, bg);
    gfx_frame_rect(x, top, w, h, 2, edge);
#if BOARD_ROUND
    if (KB_RND) {
        const kfont_t *pf = special ? &KF_TEXT : &KF_TITLE;
        ui_text_c(pf, x + w / 2, top + h / 2 + pf->asc / 2, lab, THEME_KBD_PREVIEW_TEXT);
        return;
    }
#endif
    if (ascii_only(lab)) {
        gfx_text_center_in(x, w, top + (h - 7 * sc) / 2, lab, sc, THEME_KBD_PREVIEW_TEXT);
    } else {
        ui_text_c(&KF_TITLE, x + w / 2, top + h / 2 + 12, lab, THEME_KBD_PREVIEW_TEXT);
    }
}

#if BOARD_ROUND
// The text field on a round panel: the smooth font, the end of a long text, the cursor.
static void text_round(int x, int y, int w, int h, int64_t now)
{
    char shown[72];
    int len = (int)strlen(s_buf);
    len = len > (int)sizeof(shown) - 1 ? (int)sizeof(shown) - 1 : len;
    for (int i = 0; i < len; i++) {
        shown[i] = (s_secret && !s_show) ? '*' : s_buf[i];
    }
    shown[len] = 0;
    const char *view = shown;
    const kfont_t *f = &KF_BIG;
    while (*view && ui_text_w(f, view) > w - 24) {
        view++;
        while (((unsigned char)*view & 0xC0) == 0x80) {
            view++;
        }
    }
    ui_rrect(x, y, w, h, h / 3, THEME_KBD_FIELD);
    int tw = ui_text_w(f, view), base = y + h / 2 + f->asc / 2;
    bool rtl = !ascii_only(view);
    int tx = rtl ? x + w - 12 - tw : x + 12;
    ui_text(f, tx, base, view, THEME_KBD_FIELD_TEXT);
    if ((now / 500) & 1) {
        gfx_fill_rect(rtl ? tx - 4 : tx + tw + 2, base - f->asc, 2, f->asc + 4, THEME_KBD_CURSOR);
    }
}

// The keyboard on a round panel (the head comment of the part BOARD_ROUND above).
static void render_round(int64_t now)
{
    gfx_fill(THEME_KBD_BG);
    // the title: the largest font that fits the circle at its line, else its start
    {
        int dy = KR(64) - KF_TEXT.asc - KB_H / 2, r = KB_W / 2;
        int tw = 2 * (int)sqrtf((float)(r * r - dy * dy)) - KR(40);
        const kfont_t *f = ui_text_w(&KF_TEXT, s_title) <= tw ? &KF_TEXT : &KF_LABEL;
        char t[72];
        snprintf(t, sizeof t, "%s", s_title);
        for (size_t n = strlen(t); n > 3 && ui_text_w(f, t) > tw; n--) {
            t[n - 1] = 0;
            t[n - 2] = t[n - 3] = '.';
        }
        ui_text_c(f, KB_W / 2, KR(64), t, THEME_KBD_TITLE);
    }
    text_round(FIELD_X, FIELD_Y, FIELD_W, KR(50), now);
    static const char *const BT[3] = {"CANCEL", "SHOW", "OK"};
    for (int i = 0; i < 3; i++) {
        if (i == 1 && !s_secret) {
            continue;
        }
        int bx, bw;
        round_btn(i, &bx, &bw);
        bool hot = s_press_top == i, on = (i == 2) || (i == 1 && s_show), focus = s_focus == 42 - i;
        uint16_t bg = hot ? THEME_KBD_HOT : (on ? THEME_KBD_ON : THEME_KBD_BTN);
        ui_rbox(bx, BTN_Y, bw, BTN_H, BTN_H / 3, bg, focus ? THEME_KBD_FOCUS : bg, focus ? 3 : 1);
        const kfont_t *f = ui_text_w(&KF_TEXT, BT[i]) <= bw - 8 ? &KF_TEXT : &KF_LABEL;
        ui_text_c(f, bx + bw / 2, BTN_Y + BTN_H / 2 + f->asc / 2, BT[i], hot ? THEME_KBD_TEXT_HOT : THEME_KBD_TEXT);
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 10; c++) {
            const char *ks = key_str(r, c);
            char k = ks[0];
            int x = KEY_X0 + c * KEY_W, y = TOP_H + r * KEY_H;
            bool hot = r == s_press_r && c == s_press_c, special = (unsigned char)k < ' ';
            uint16_t bg = hot ? THEME_KBD_HOT : (special ? THEME_KBD_KEY_SPECIAL : THEME_KBD_KEY);
            if (k == K_SHIFT && (s_mode == M_UPPER || s_mode == M_SYM2)) {
                bg = THEME_KBD_ON;
            }
            ui_rrect(x + KEY_GAP, y + KEY_GAP, KEY_W - 2 * KEY_GAP, KEY_H - 2 * KEY_GAP, 6, bg);
            char lab[8];
            if (!special) {
                snprintf(lab, sizeof lab, "%s", ks);
            } else {
                key_label(k, lab, sizeof lab);
            }
            const kfont_t *f = special ? &KF_LABEL : &KF_BIG;
            uint16_t fg = hot ? THEME_KBD_TEXT_HOT : THEME_KBD_TEXT;
            int cx = x + KEY_W / 2, cy = y + KEY_H / 2;
            if (k == K_SHIFT && s_mode < M_SYM1) {
                // (SHIFT: an arrow up; the word is too wide for the key)
                for (int j = 0; j < 8; j++) {
                    gfx_fill_rect(cx - j, cy - 9 + j, 2 * j + 1, 1, fg);
                }
                gfx_fill_rect(cx - 3, cy - 1, 7, 8, fg);
            } else if (k == K_SPACE) {
                // (SPACE: the space mark)
                gfx_fill_rect(cx - 11, cy + 4, 23, 2, fg);
                gfx_fill_rect(cx - 11, cy - 2, 2, 6, fg);
                gfx_fill_rect(cx + 10, cy - 2, 2, 6, fg);
            } else {
                ui_text_c(f, x + KEY_W / 2, y + KEY_H / 2 + f->asc / 2, lab, fg);
            }
            if (s_focus == r * 10 + c) {
                gfx_frame_rect(x, y, KEY_W, KEY_H, 2, THEME_KBD_FOCUS);
            }
        }
    }
    if (s_key_touch && s_press_r >= 0 && now - s_down_t >= PV_DELAY_MS) {
        draw_preview();
    }
}
#endif

void kbd_render(int64_t now)
{
    if (s_ask) {
        char l2[80];
        int len = (int)strlen(s_buf), n = 0;
        n += snprintf(l2, sizeof l2, "%s", s_ask == 1 ? "THE TEXT: " : "");
        for (int i = 0; i < len && n < (int)sizeof(l2) - 1; i++) {
            l2[n++] = (s_secret && !s_show) ? '*' : s_buf[i];
        }
        l2[n] = 0;
        kbd_ask_render(s_ask == 1 ? (s_okq ? s_okq : "USE THIS TEXT?") : "DROP THE TEXT?", l2, s_ask_hot, s_ask_focus);
        return;
    }
#if BOARD_ROUND
    if (KB_RND) {
        render_round(now);
        return;
    }
#endif
    gfx_fill(THEME_KBD_BG);
    gfx_text(FIELD_X, 3, s_title, 1 + KB_BIG, THEME_KBD_TITLE);
    draw_text(FIELD_X, FIELD_Y + 2, FIELD_W, now);
    top_button(CAN_X, CAN_W, "CANCEL", s_press_top == 0, false);
    if (s_secret) {
        top_button(SHOW_X, SHOW_W, "SHOW", s_press_top == 1, s_show);
    }
    top_button(OK_X, OK_W, "OK", s_press_top == 2, true);
    if (s_focus >= 40) {
        int fx = s_focus == 40 ? OK_X : s_focus == 41 ? SHOW_X : CAN_X;
        int fw = s_focus == 40 ? OK_W : s_focus == 41 ? SHOW_W : CAN_W;
        gfx_frame_rect(fx, BTN_Y, fw, BTN_H, 3, THEME_KBD_FOCUS);
    }

    // keys
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 10; c++) {
            const char *ks = key_str(r, c);
            char k = ks[0];
            int x = c * KEY_W, y = TOP_H + r * KEY_H;
            bool hot = r == s_press_r && c == s_press_c;
            bool special = (unsigned char)k < ' ';  // (the first byte of a UTF-8 letter is 0xC0 or more)
            uint16_t bg = hot ? THEME_KBD_HOT : (special ? THEME_KBD_KEY_SPECIAL : THEME_KBD_KEY);
            if (k == K_SHIFT && (s_mode == M_UPPER || s_mode == M_SYM2)) {
                bg = THEME_KBD_ON;
            }
            gfx_fill_rect(x + KEY_GAP, y + KEY_GAP, KEY_W - 2 * KEY_GAP, KEY_H - 2 * KEY_GAP, bg);
            uint16_t fg = hot ? THEME_KBD_TEXT_HOT : THEME_KBD_TEXT;
            char lab[8];
            if (!special) {
                snprintf(lab, sizeof lab, "%s", ks);
            } else {
                key_label(k, lab, sizeof lab);
            }
            int sc = (special ? 1 : 2) + KB_BIG;
            if (ascii_only(lab)) {
                gfx_text_center_in(x, KEY_W, y + (KEY_H - 7 * sc) / 2, lab, sc, fg);
            } else {
                ui_text_c(KB_BIG ? &KF_BIG : &KF_NAME, x + KEY_W / 2, y + KEY_H / 2 + 7, lab, fg);
            }
            if (s_focus == r * 10 + c) {
                gfx_frame_rect(x, y, KEY_W, KEY_H, 2, THEME_KBD_FOCUS);
            }
        }
    }
    // the preview of the key under the finger, 0.2 s after the touch
    if (s_key_touch && s_press_r >= 0 && now - s_down_t >= PV_DELAY_MS) {
        draw_preview();
    }
}
