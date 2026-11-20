// BT KEYS: the board as a Bluetooth keyboard, mouse and media keys for a computer, a phone or a TV.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_btkeys at the end of this file);
// the Bluetooth link runs in the tasks of main/bt_pad.c.
// The memory: the file statics of this file; no heap.
//
// One Bluetooth LE device (main/bt_pad.c: the gamepad, the keyboard with its LEDs, the media keys and a mouse with a
// wheel); pair it one time ("TFN PAD").
// Four pages (the tabs at the top; on the 3.49 in a strip at the left, so that each tab is 36 px high and each row of
// keys 43 px: the bar of 26 px at the top was too small for a thumb):
//  - MOUSE: a touchpad. One finger moves the pointer (faster when it moves fast); a tap = a left click; a tap, then a
//    touch and a move = a drag; two fingers that move = the wheel; a tap of two fingers = a right click. The strip at
//    its right side = the wheel too. The buttons L and R at the right (hold L with one thumb and move with the other).
//    AIR MOUSE (the panel): the tilt of the board moves the pointer.
//  - KEYS: a full keyboard (letters, ?123 = the numbers and the signs, the arrows, ESC, TAB, CTRL, ALT, WIN). As each
//    keyboard of TFN OS: a key shows above the finger after 0.2 s, the finger can slide to the next key, the key
//    types when the finger comes off. CTRL, ALT, WIN and SHIFT act on the next key; a hold of 0.6 s locks them.
//    LABELS (the panel): the letters of the Hebrew layout on the keys (the computer types them with its Hebrew layout).
//    The key EN / עב: one tap changes the letters on the keys AND sends
//    the keys that change the layout of the computer (the panel: SWITCH KEYS = ALT+SHIFT for Windows, WIN+SPACE, or
//    CTRL+SPACE for a Mac), so that the computer types Hebrew too.
//  - BOTH: the touchpad with a row of keys under it (ESC, TAB, BACKSPACE, SPACE, ENTER, the arrows).
//  - MEDIA: PREVIOUS, PLAY / PAUSE, NEXT, VOLUME DOWN, MUTE, VOLUME UP.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/bt_pad.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"

// ---------------------------------------------------------------- the settings
static int s_lang = 1, s_labels, s_air, s_speed = 1, s_switch;
static const char *T(const char *en, const char *he)
{
    return s_lang ? he : en;
}
enum {
    PG_MOUSE,
    PG_KEYS,
    PG_BOTH,
    PG_MEDIA,
    PG_N
};
static int s_page;
static const char *const TAB[PG_N][2] = {{"MOUSE", "עכבר"}, {"KEYS", "מקלדת"}, {"BOTH", "שניהם"}, {"MEDIA", "מדיה"}};

// ---------------------------------------------------------------- the layout (640 x 172 on the 3.49, 320 x 240)
static int s_w, s_h, s_bar, s_side_w, s_row;
static int s_strip;  // the 3.49: the width of the strip of the tabs at the left (0: the tabs are in the bar at the top)
#define STRIP_TAB_H 36  // the 3.49: the height of a tab of the strip (4.8 mm); the state of the link under the tabs
static bool s_wide;
static bool s_rnd;    // the round frame (APP_ROUND_HI): the tabs in the top cap, each row of keys as wide as the circle
static int rs(int v)  // a size of the round layout of 466, scaled to this frame (the Watcher: 412)
{
    return v * s_w / 466;
}
static void layout(void)
{
    s_w = gfx_w;
    s_h = gfx_h;
    s_rnd = gfx_round();
    s_wide = s_w >= 3 * s_h;
    s_strip = s_wide && !s_rnd ? 88 : 0;
    s_bar = s_strip ? 0 : 34;
    s_side_w = s_wide ? 132 : 70;  // the L and R buttons
    s_row = 40;                    // the key row of BOTH
    if (s_rnd) {
        s_bar = rs(106);
        s_strip = 0;
        s_row = rs(64);
    }
}
// the half width of the circle at the row y, less a margin m (the round frame)
static int rnd_half(int y, int m)
{
    const int R = s_w / 2 - m, dy = y - s_h / 2;
    int h = 0;
    while ((h + 1) * (h + 1) + dy * dy <= R * R) {
        h++;
    }
    return h;
}
// the left end and the width of a row of keys from y to y + h: the whole frame, or the chord of the circle at the
// narrower end of the row
static void row_span(int y, int h, int *x, int *w)
{
    if (!s_rnd) {
        *x = s_strip;
        *w = s_w - s_strip;
        return;
    }
    const int a = rnd_half(y, 6), b = rnd_half(y + h, 6), hw = a < b ? a : b;
    *x = s_w / 2 - hw;
    *w = 2 * hw;
}
typedef struct {
    int x, y, w, h;
} box_t;
static bool in(const box_t *b, int x, int y)
{
    return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}
static box_t tab_box(int i)
{
    if (s_rnd) {  // (four tabs in the top cap, 48 px high)
        const int tw = rs(72), g = 4, x0 = s_w / 2 - (4 * tw + 3 * g) / 2;
        return (box_t){x0 + i * (tw + g), rs(54), tw, 48};
    }
    if (s_strip) {  // (the 3.49: the strip at the left)
        return (box_t){2, 2 + i * STRIP_TAB_H, s_strip - 6, STRIP_TAB_H - 4};
    }
    int tw = s_wide ? 86 : 54;
    return (box_t){2 + i * (tw + 2), 2, tw, s_bar - 4};
}
// the touch zone of a tab: the whole cell of the tab with no gap (the strip: its full width; the bar: its full height)
static bool tab_hit(int i, int x, int y)
{
    if (s_rnd) {
        const box_t b = tab_box(i);
        return in(&b, x, y);
    }
    if (s_strip) {
        return x < s_strip && y >= (i ? i * STRIP_TAB_H : -40) && y < (i + 1) * STRIP_TAB_H;
    }
    const box_t b = tab_box(i);
    return y < s_bar && x >= b.x - 1 && x < b.x + b.w + 1;
}
static box_t pad_box(void)
{
    if (s_rnd) {  // (the pad in the middle; L and R in the bottom cap; BOTH: the row of keys under the pad)
        return s_page == PG_BOTH ? (box_t){rs(44), s_bar, s_w - 2 * rs(44), rs(200)}
                                 : (box_t){rs(40), s_bar, s_w - 2 * rs(40), rs(236)};
    }
    if (s_page == PG_BOTH) {
        return (box_t){s_strip, s_bar, s_w - s_strip, s_h - s_bar - s_row};
    }
    return (box_t){s_strip, s_bar, s_w - s_side_w - s_strip, s_h - s_bar};
}
static box_t lr_box(int r)
{
    if (s_rnd) {
        const int w = rs(104);
        return (box_t){r ? s_w / 2 + 4 : s_w / 2 - 4 - w, rs(350), w, rs(58)};
    }
    int h = (s_h - s_bar) / 2;
    return (box_t){s_w - s_side_w + 4, s_bar + r * h + 2, s_side_w - 6, h - 4};
}

// ---------------------------------------------------------------- the keys
// a key: its label (English, Hebrew label or NULL), its width in units, what it does
enum {
    KA_KEY,
    KA_MOD,
    KA_LAYER,
    KA_ASCII,
    KA_LANG
};
typedef struct {
    const char *lab, *he;
    float u;
    uint8_t act, code;
} bkey_t;
#define K(l, h, c)  {l, h, 1, KA_KEY, c}
#define KW(l, u, c) {l, NULL, u, KA_KEY, c}
#define KM(l, u, m) {l, NULL, u, KA_MOD, m}
#define KS(l)       {l, NULL, 1, KA_ASCII, 0}
static const bkey_t L0[] = {K("q", "/", 0x14), K("w", "'", 0x1A), K("e", "ק", 0x08), K("r", "ר", 0x15),
                            K("t", "א", 0x17), K("y", "ט", 0x1C), K("u", "ו", 0x18), K("i", "ן", 0x0C),
                            K("o", "ם", 0x12), K("p", "פ", 0x13), KW("<-", 1, 0x2A)};
static const bkey_t L1[] = {K("a", "ש", 0x04), K("s", "ד", 0x16),   K("d", "ג", 0x07), K("f", "כ", 0x09),
                            K("g", "ע", 0x0A), K("h", "י", 0x0B),   K("j", "ח", 0x0D), K("k", "ל", 0x0E),
                            K("l", "ך", 0x0F), KW("ENTER", 2, 0x28)};
static const bkey_t L2[] = {KM("SHIFT", 1.5f, BT_MOD_LSHIFT),
                            K("z", "ז", 0x1D),
                            K("x", "ס", 0x1B),
                            K("c", "ב", 0x06),
                            K("v", "ה", 0x19),
                            K("b", "נ", 0x05),
                            K("n", "מ", 0x11),
                            K("m", "צ", 0x10),
                            K(",", "ת", 0x36),
                            K(".", "ץ", 0x37),
                            {"?123", NULL, 1.5f, KA_LAYER, 0}};
static const bkey_t L3[] = {KW("ESC", 1, 0x29),        KW("TAB", 1, 0x2B),          KM("CTRL", 1, BT_MOD_LCTRL),
                            KM("ALT", 1, BT_MOD_LALT), {"EN", NULL, 1, KA_LANG, 0}, KW("SPACE", 2, 0x2C),
                            KW("LEFT", 1, 0x50),       KW("UP", 1, 0x52),           KW("DOWN", 1, 0x51),
                            KW("RIGHT", 1, 0x4F)};
static const bkey_t S0[] = {KS("1"), KS("2"), KS("3"), KS("4"), KS("5"),          KS("6"),
                            KS("7"), KS("8"), KS("9"), KS("0"), KW("<-", 1, 0x2A)};
static const bkey_t S1[] = {KS("-"), KS("/"), KS(":"), KS(";"), KS("("),
                            KS(")"), KS("$"), KS("&"), KS("@"), KW("ENTER", 2, 0x28)};
static const bkey_t S2[] = {KM("WIN", 1.5f, BT_MOD_LGUI),
                            KS("\""),
                            KS("'"),
                            KS("="),
                            KS("+"),
                            KS("?"),
                            KS("!"),
                            KS("*"),
                            KS("#"),
                            KS("%"),
                            {"abc", NULL, 1.5f, KA_LAYER, 0}};
static const bkey_t S3[] = {
        KS("_"),           KS("["), KS("]"), KS("\\"), KW("SPACE", 3, 0x2C), KS("<"), KS(">"), KW("TAB", 1, 0x2B),
        KW("DEL", 1, 0x4C)};
// (the small screens: the bottom row of the letters with no TAB, it is on the ?123 layer)
static const bkey_t L3N[] = {KW("ESC", 1.2f, 0x29),
                             KM("CTRL", 1.3f, BT_MOD_LCTRL),
                             KM("ALT", 1.2f, BT_MOD_LALT),
                             {"EN", NULL, 1.1f, KA_LANG, 0},
                             KW("SPACE", 2.2f, 0x2C),
                             KW("LEFT", 1, 0x50),
                             KW("UP", 1, 0x52),
                             KW("DOWN", 1, 0x51),
                             KW("RIGHT", 1, 0x4F)};
static const bkey_t B0[] = {KW("ESC", 1, 0x29),   KW("TAB", 1, 0x2B),   KW("<-", 1, 0x2A),
                            KW("SPACE", 2, 0x2C), KW("ENTER", 2, 0x28), KW("LEFT", 1, 0x50),
                            KW("UP", 1, 0x52),    KW("DOWN", 1, 0x51),  KW("RIGHT", 1, 0x4F)};
typedef struct {
    const bkey_t *k;
    int n;
} krow_t;
#define ROWOF(a) {a, (int)(sizeof(a) / sizeof(a[0]))}
static const krow_t LET[4] = {ROWOF(L0), ROWOF(L1), ROWOF(L2), ROWOF(L3)};
static const krow_t LETN[4] = {ROWOF(L0), ROWOF(L1), ROWOF(L2), ROWOF(L3N)};
static const krow_t SYM[4] = {ROWOF(S0), ROWOF(S1), ROWOF(S2), ROWOF(S3)};
static const krow_t BOTHROW[1] = {ROWOF(B0)};
static bool s_sym;              // the layer ?123
static uint8_t s_mods, s_lock;  // the modifiers for the next key; the locked ones

// the rows of the page now, and the box of a key
static const krow_t *rows_now(int *nr, int *y0, int *rh)
{
    if (s_page == PG_KEYS) {
        *nr = 4;
        *y0 = s_rnd ? rs(118) : s_bar;
        *rh = s_rnd ? rs(72) : (s_h - s_bar) / 4;
        return s_sym ? SYM : s_wide ? LET : LETN;
    }
    if (s_page == PG_BOTH) {
        *nr = 1;
        *y0 = s_rnd ? s_bar + rs(206) : s_h - s_row;
        *rh = s_row;
        return BOTHROW;
    }
    *nr = 0;
    *y0 = 0;
    *rh = 0;
    return NULL;
}
static bool key_at(int x, int y, const bkey_t **kk, box_t *kb)
{
    int nr, y0, rh;
    const krow_t *R = rows_now(&nr, &y0, &rh);
    for (int r = 0; r < nr; r++) {
        if (y < y0 + r * rh || y >= y0 + (r + 1) * rh) {
            continue;
        }
        float tot = 0;
        for (int i = 0; i < R[r].n; i++) {
            tot += R[r].k[i].u;
        }
        int rx, rw;
        row_span(y0 + r * rh, rh, &rx, &rw);
        float u = rw / tot, xx = (float)rx;
        for (int i = 0; i < R[r].n; i++) {
            int x0 = (int)xx, x1 = (int)(xx + R[r].k[i].u * u);
            if (x >= x0 && x < x1) {
                if (kk) {
                    *kk = &R[r].k[i];
                }
                if (kb) {
                    *kb = (box_t){x0, y0 + r * rh, x1 - x0, rh};
                }
                return true;
            }
            xx += R[r].k[i].u * u;
        }
    }
    return false;
}

// a key acts (at the lift, or each repeat of a held key)
static void key_act(const bkey_t *k)
{
    if (k->act == KA_LAYER) {
        s_sym = !s_sym;
        return;
    }
    if (k->act == KA_LANG) {
        // the letters on the keys, and the layout of the computer (its own switch keys)
        s_labels = !s_labels;
        store_set("bk_lab", (uint32_t)s_labels);
        static const uint8_t SW_MOD[3] = {BT_MOD_LALT | BT_MOD_LSHIFT, BT_MOD_LGUI, BT_MOD_LCTRL};
        static const uint8_t SW_KEY[3] = {0, 0x2C, 0x2C};
        bt_kbd_tap(SW_MOD[s_switch % 3], SW_KEY[s_switch % 3]);
        s_mods = 0;
        return;
    }
    if (k->act == KA_MOD) {
        if (s_lock & k->code) {
            s_lock &= (uint8_t)~k->code;
            s_mods &= (uint8_t)~k->code;
        } else {
            s_mods ^= k->code;
        }
        return;
    }
    if (k->act == KA_ASCII) {
        bt_kbd_type(k->lab);
    } else {
        bt_kbd_tap(s_mods | s_lock, k->code);
    }
    s_mods = 0;  // (the one-shot modifiers end; the locked ones stay)
}

// ---------------------------------------------------------------- the touch: keys and buttons (events)
static const bkey_t *s_kdown;  // the key under the finger
static box_t s_kbox;
static int64_t s_kt0, s_krep;  // the touch on the key; the next repeat
static bool s_krepeated;
static int64_t s_now;
static int s_media_down = -1;
static int64_t s_mod_t0;

static box_t media_box(int i)
{
    if (s_rnd) {  // (3 x 2 keys in the middle box of the circle)
        const int cw = rs(112), ch = rs(100), x0 = s_w / 2 - 3 * cw / 2, y0 = rs(130);
        return (box_t){x0 + (i % 3) * cw + 3, y0 + (i / 3) * ch + 3, cw - 6, ch - 6};
    }
    int cw = (s_w - s_strip - 8) / 3, ch = (s_h - s_bar - 8) / 2;
    return (box_t){s_strip + 4 + (i % 3) * cw + 2, s_bar + 4 + (i / 3) * ch + 2, cw - 4, ch - 4};
}
static const uint16_t MEDIA[6] = {0xB6, 0xCD, 0xB5, 0xEA, 0xE2, 0xE9};
static const char *const MEDIA_T[6][2] = {{"PREVIOUS", "הקודם"}, {"PLAY / PAUSE", "הפעלה / עצירה"},
                                          {"NEXT", "הבא"},       {"VOLUME -", "עוצמה -"},
                                          {"MUTE", "השתקה"},     {"VOLUME +", "עוצמה +"}};

static bool s_naming;  // the keyboard of the name is open (the panel item DEVICE NAME)
static char s_nbuf[30];
static void name_done(void);
static void input(const input_event_t *ev)
{
    if (s_naming) {
        int r = kbd_input(ev);
        if (r == KBD_OK) {
            name_done();
        } else if (r == KBD_CANCEL) {
            s_naming = false;
        }
        return;
    }
    int x = ev->x, y = ev->y;
    if (ev->kind == IN_TAP && (s_strip ? x < s_strip : y < s_bar)) {
        for (int i = 0; i < PG_N; i++) {
            if (tab_hit(i, x, y)) {
                s_page = i;
                store_set("bk_page", (uint32_t)i);
                sfx_play(SFX_CLICK);
            }
        }
        return;
    }
    if (s_page == PG_MEDIA) {
        if (ev->kind == IN_DOWN) {
            s_media_down = -1;
            for (int i = 0; i < 6; i++) {
                box_t b = media_box(i);
                if (in(&b, x, y)) {
                    s_media_down = i;
                }
            }
        } else if (ev->kind == IN_UP) {
            if (s_media_down >= 0) {
                box_t b = media_box(s_media_down);
                if (in(&b, x, y)) {
                    bt_media_tap(MEDIA[s_media_down]);
                }
            }
            s_media_down = -1;
        }
        return;
    }
    if (s_page != PG_KEYS && s_page != PG_BOTH) {
        return;
    }
    const bkey_t *k = NULL;
    box_t kb;
    bool on_key = key_at(x, y, &k, &kb);
    if (ev->kind == IN_DOWN) {
        s_kdown = on_key ? k : NULL;
        s_kbox = kb;
        s_kt0 = s_now;
        s_krep = s_now + 500;
        s_krepeated = false;
        if (s_kdown && s_kdown->act == KA_MOD) {
            s_mod_t0 = s_now;
        }
    } else if (ev->kind == IN_MOVE && s_kdown) {
        // the slide: the key under the finger now
        if (on_key && k != s_kdown) {
            s_kdown = k;
            s_kbox = kb;
            s_kt0 = s_now - 200;
            s_krep = s_now + 500;
            s_krepeated = false;
        }
    } else if (ev->kind == IN_UP) {
        if (s_kdown) {
            if (s_kdown->act == KA_MOD && s_now - s_mod_t0 >= 600) {
                s_lock |= s_kdown->code;  // (a hold locks it)
            } else if (!s_krepeated) {
                key_act(s_kdown);
            }
        }
        s_kdown = NULL;
    }
}

// ---------------------------------------------------------------- the touchpad (all the fingers, each frame)
static bool s_f1, s_f2;          // one finger, two fingers on the pad
static float s_fx, s_fy, s_f2y;  // the last place (of the first finger; the middle of two)
static int64_t s_ft0;            // the touch of the first finger
static float s_travel;
static bool s_two_moved, s_was_two;
static int64_t s_last_tap, s_click_up;  // the last tap (a tap, then a drag); the release of a click
static bool s_drag;
static uint8_t s_side;                 // the L and R buttons (fingers on them now)
static float s_wheel_acc, s_mx, s_my;  // the parts of the wheel and of the motion
static float s_tx0, s_ty0;             // the zero of the AIR MOUSE

static void pad_step(int64_t now)
{
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    int n = input_points(px, py, INPUT_MAX_POINTS);
    box_t pb = pad_box();
    int np = 0;
    float ax = 0, ay = 0, bx = 0, by = 0;
    uint8_t side = 0;
    bool strip = false;
    for (int i = 0; i < n; i++) {
        int x, y;
        gfx_touch_to_logical(px[i], py[i], &x, &y);
        if (s_page == PG_MOUSE) {
            for (int r = 0; r < 2; r++) {
                box_t b = lr_box(r);
                if (in(&b, x, y)) {
                    side |= (uint8_t)(1 << r);
                }
            }
        }
        if (!in(&pb, x, y)) {
            continue;
        }
        if (np == 0) {
            ax = (float)x;
            ay = (float)y;
            strip = x >= pb.x + pb.w - (s_wide ? 34 : 26);
        } else if (np == 1) {
            bx = (float)x;
            by = (float)y;
        }
        np++;
    }
    s_side = side;
    float k = (s_speed == 0 ? 1.0f : s_speed == 1 ? 1.6f : 2.4f) * (s_wide ? 1.0f : 1.5f);
    if (np >= 2) {
        // two fingers: the wheel (the middle moves up or down); a short touch of both = a right click
        float my = (ay + by) * 0.5f;
        if (!s_f2) {
            s_f2 = true;
            s_f2y = my;
            s_two_moved = false;
            s_was_two = true;
        }
        float d = my - s_f2y;
        if (fabsf(d) > 2) {
            s_two_moved = true;
        }
        s_wheel_acc += -d / (s_wide ? 10.0f : 14.0f);
        s_f2y = my;
    } else if (s_f2 && np < 2) {
        s_f2 = false;
        if (!s_two_moved && now - s_ft0 < 350) {
            bt_mouse_buttons(2);
            s_click_up = now + 40;
        }
    }
    if (np == 1 && !s_f2) {
        if (!s_f1) {
            s_f1 = true;
            s_fx = ax;
            s_fy = ay;
            s_ft0 = now;
            s_travel = 0;
            if (!s_was_two && now - s_last_tap < 300 && !strip) {
                s_drag = true;
                bt_mouse_buttons(1);
            }  // (tap, then drag)
        } else if (!s_was_two) {
            float dx = ax - s_fx, dy = ay - s_fy;
            s_travel += fabsf(dx) + fabsf(dy);
            if (strip) {
                s_wheel_acc += -dy / (s_wide ? 6.0f : 8.0f);
            } else {
                float sp = sqrtf(dx * dx + dy * dy);
                float g = k * (1.0f + 0.06f * sp);  // (a fast finger goes further)
                s_mx += dx * g;
                s_my += dy * g;
            }
            s_fx = ax;
            s_fy = ay;
        }
    } else if (np == 0) {
        if (s_f1 && !s_was_two) {
            if (s_drag) {
                s_drag = false;
                bt_mouse_buttons(0);
            } else if (s_travel < 10 && now - s_ft0 < 250 && !strip) {
                bt_mouse_buttons(1);
                s_click_up = now + 40;
                s_last_tap = now;
            }
        }
        s_f1 = false;
        s_was_two = false;
    }
    // the AIR MOUSE: the tilt moves the pointer (a dead zone; faster with more tilt)
    if (s_air && s_page == PG_MOUSE && imu_ok()) {
        float tx, ty;
        imu_tilt_logical(&tx, &ty);
        float vx = tx - s_tx0, vy = ty - s_ty0, dz = 0.04f;
        vx = fabsf(vx) < dz ? 0 : vx - (vx > 0 ? dz : -dz);
        vy = fabsf(vy) < dz ? 0 : vy - (vy > 0 ? dz : -dz);
        s_mx += vx * 60 * k;
        s_my += vy * 60 * k;
    }
    int ix = (int)s_mx, iy = (int)s_my, iw = (int)s_wheel_acc;
    s_mx -= ix;
    s_my -= iy;
    s_wheel_acc -= iw;
    if (ix || iy || iw) {
        bt_mouse_move(ix, iy, iw);
    }
    // the buttons: the side buttons, a drag, a click that ends after 40 ms
    uint8_t b = side;
    if (s_drag) {
        b |= 1;
    }
    if (s_click_up) {
        if (now >= s_click_up) {
            s_click_up = 0;
        } else {
            return;  // (the click is down: it ends later)
        }
    }
    bt_mouse_buttons(b);
}

// ---------------------------------------------------------------- the panel
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const LAB_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_V[] = {"OFF", "כבוי", "ON", "פועל"};
static const char *const SPEED_V[] = {"SLOW", "איטי", "NORMAL", "רגיל", "FAST", "מהיר"};
static const char *const SWITCH_V[] = {"ALT+SHIFT", "ALT+SHIFT", "WIN+SPACE", "WIN+SPACE", "CTRL+SPACE", "CTRL+SPACE"};
enum {
    PI_LANG,
    PI_LAB,
    PI_SWITCH,
    PI_AIR,
    PI_SPEED,
    PI_NAME,
    PI_FORGET
};
static const pitem_t ITEMS[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "bk_lang"},
        {PI_PICK, PI_LAB, 2, 0, {"KEY LABELS", "אותיות על המקשים"}, LAB_V, "bk_lab"},
        {PI_PICK, PI_SWITCH, 3, 0, {"SWITCH KEYS", "מקשי החלפת שפה"}, SWITCH_V, "bk_sw"},
        {PI_PICK, PI_AIR, 2, 0, {"AIR MOUSE", "עכבר הטיה"}, ONOFF_V, "bk_air"},
        {PI_PICK, PI_SPEED, 3, 1, {"SPEED", "מהירות"}, SPEED_V, "btk_speed"},
        {PI_ACT, PI_NAME, 0, 0, {"DEVICE NAME", "שם המכשיר"}, NULL, NULL},
        {PI_ACT2, PI_FORGET, 0, 0, {"FORGET PAIRED", "שכח מכשירים"}, NULL, NULL},
};
static bool item_shown(int id)
{
    return id != PI_AIR || imu_ok();
}
// THE NAME: the name that the computer shows. The keyboard opens with the current name; OK saves
// it (bt_pad_set_name: the firmware keeps it, GAMEPAD and the web page /pad use the same name). A computer that is
// paired shows the name of its pairing until it pairs again.
static void name_open(void)
{
    snprintf(s_nbuf, sizeof(s_nbuf), "%s", bt_pad_name());
    kbd_open("BLUETOOTH NAME", s_nbuf, sizeof(s_nbuf), false);  // (the name is ASCII: the keyboard has no Hebrew)
    kbd_set_ok_question(NULL);
    s_naming = true;
}
static void name_done(void)
{
    s_naming = false;
    int n = (int)strlen(s_nbuf);
    while (n > 0 && s_nbuf[n - 1] == ' ') {
        s_nbuf[--n] = 0;
    }
    int a = 0;
    while (s_nbuf[a] == ' ') {
        a++;
    }
    if (s_nbuf[a]) {
        bt_pad_set_name(s_nbuf + a);
    }
}
static void item_set(int id, int v)
{
    if (id == PI_LANG) {
        s_lang = v;
    } else if (id == PI_LAB) {
        s_labels = v;
    } else if (id == PI_SWITCH) {
        s_switch = v;
    } else if (id == PI_AIR) {
        s_air = v;
        imu_tilt_logical(&s_tx0, &s_ty0);
    } else if (id == PI_SPEED) {
        s_speed = v;
    } else if (id == PI_NAME) {
        name_open();
    } else if (id == PI_FORGET) {
        bt_pad_forget();
    }
}
static void item_resume(void)
{
    imu_tilt_logical(&s_tx0, &s_ty0);
}
static const pitems_t PANEL = {ITEMS, 7, item_shown, NULL, item_set, NULL, item_resume};

// ---------------------------------------------------------------- enter, update, leave
static void enter(int64_t now)
{
    s_now = now;
    layout();
    s_lang = store_get("bk_lang", 1) ? 1 : 0;
    s_labels = (int)store_get("bk_lab", 0);
    s_switch = (int)(store_get("bk_sw", 0) % 3);
    s_air = (int)store_get("bk_air", 0);
    s_speed = (int)(store_get("btk_speed", 1) % 3);
    s_page = (int)(store_get("bk_page", PG_MOUSE) % PG_N);
    s_sym = false;
    s_mods = s_lock = 0;
    s_kdown = NULL;
    s_f1 = s_f2 = s_drag = false;
    s_side = 0;
    s_naming = false;
    imu_tilt_logical(&s_tx0, &s_ty0);
    sysui_panel_items(&PANEL);
    bt_pad_start();
}
static void update(int64_t now)
{
    s_now = now;
    if (s_naming) {
        return;  // (the keyboard of the name: no pointer, no key repeat)
    }
    if (s_page == PG_MOUSE || s_page == PG_BOTH) {
        pad_step(now);
    }
    // a held key repeats (BACKSPACE, the arrows, SPACE, the letters): after 0.5 s, each 80 ms
    if (s_kdown && s_kdown->act != KA_MOD && s_kdown->act != KA_LAYER && now >= s_krep) {
        key_act(s_kdown);
        s_krepeated = true;
        s_krep = now + 80;
    }
}
static void leave(void)
{
    bt_mouse_buttons(0);  // (no button stays down on the computer)
}
static bool awake(void)
{
    return bt_pad_state() == PAD_READY;
}

// ---------------------------------------------------------------- the drawing
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c)
{
    // a filled triangle (rows): the arrows of the keys
    float ys[3] = {y0, y1, y2}, xs[3] = {x0, x1, x2};
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2 - i; j++) {
            if (ys[j] > ys[j + 1]) {
                float t = ys[j];
                ys[j] = ys[j + 1];
                ys[j + 1] = t;
                t = xs[j];
                xs[j] = xs[j + 1];
                xs[j + 1] = t;
            }
        }
    }
    for (int y = (int)ceilf(ys[0]); y <= (int)ys[2]; y++) {
        float a = ys[2] > ys[0] ? xs[0] + (xs[2] - xs[0]) * (y - ys[0]) / (ys[2] - ys[0]) : xs[0];
        float b = y < ys[1] ? (ys[1] > ys[0] ? xs[0] + (xs[1] - xs[0]) * (y - ys[0]) / (ys[1] - ys[0]) : xs[1])
                            : (ys[2] > ys[1] ? xs[1] + (xs[2] - xs[1]) * (y - ys[1]) / (ys[2] - ys[1]) : xs[2]);
        if (a > b) {
            float t = a;
            a = b;
            b = t;
        }
        gfx_fill_rect((int)a, y, (int)(b - a) + 1, 1, c);
    }
}
static void arrow(int cx, int cy, int dir, int s, uint16_t c)  // dir: 0 left, 1 up, 2 down, 3 right
{
    if (dir == 0) {
        tri(cx - s, cy, cx + s * 0.6f, cy - s, cx + s * 0.6f, cy + s, c);
    }
    if (dir == 3) {
        tri(cx + s, cy, cx - s * 0.6f, cy - s, cx - s * 0.6f, cy + s, c);
    }
    if (dir == 1) {
        tri(cx, cy - s, cx - s, cy + s * 0.6f, cx + s, cy + s * 0.6f, c);
    }
    if (dir == 2) {
        tri(cx, cy + s, cx - s, cy - s * 0.6f, cx + s, cy - s * 0.6f, c);
    }
}
static void key_face(const bkey_t *k, box_t b, bool down)
{
    bool mod_on = k->act == KA_MOD && ((s_mods | s_lock) & k->code);
    bool caps = k->act == KA_MOD && k->code == BT_MOD_LSHIFT && (bt_kbd_leds() & 2);
    uint16_t bg = down                                                        ? rgb(90, 150, 230)
                  : mod_on                                                    ? rgb(60, 110, 180)
                  : (k->act == KA_KEY && k->code >= 0x28) || k->act != KA_KEY ? rgb(44, 48, 72)
                                                                              : rgb(58, 62, 92);
    ui_rrect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, 6, bg);
    if ((s_lock & k->code) && k->act == KA_MOD) {
        gfx_fill_rect(b.x + 8, b.y + b.h - 7, b.w - 16, 2, rgb(255, 210, 90));
    }
    if (caps) {
        ui_circle(b.x + b.w - 9, b.y + 9, 3, rgb(120, 255, 140), 256);
    }
    int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
    uint16_t tc = rgb(235, 235, 250);
    if (k->act == KA_KEY && (k->code == 0x50 || k->code == 0x52 || k->code == 0x51 || k->code == 0x4F)) {
        arrow(cx, cy, k->code == 0x50 ? 0 : k->code == 0x52 ? 1 : k->code == 0x51 ? 2 : 3, b.h / 5, tc);
        return;
    }
    if (k->act == KA_KEY && k->code == 0x2A) {  // BACKSPACE: an arrow with a tail
        arrow(cx - b.w / 8, cy, 0, b.h / 6, tc);
        gfx_fill_rect(cx - b.w / 8, cy - 2, b.w / 4, 4, tc);
        return;
    }
    if (k->act == KA_LANG) {  // (the language that a tap gives)
        ui_text_c(&KF_TEXT, cx, cy + 7, s_labels ? "EN" : "עב", rgb(255, 214, 120));
        return;
    }
    const char *lab = k->lab;
    char up[8];
    if (k->act == KA_KEY && k->code >= 0x04 && k->code <= 0x1D && !s_labels &&
        ((s_mods | s_lock) & BT_MOD_LSHIFT || (bt_kbd_leds() & 2))) {
        snprintf(up, sizeof up, "%c", lab[0] - 32);
        lab = up;
    }
    if (s_labels && k->he) {
        // the Hebrew letter big, the Latin one small at the corner
        ui_text_c(&KF_TEXT, cx, cy + 7, k->he, tc);
        ui_text_c(&KF_LABEL, b.x + 10, b.y + 14, lab, rgb(150, 150, 175));
        return;
    }
    const kfont_t *f = strlen(lab) > 2 ? &KF_LABEL : &KF_TEXT;
    ui_text_c(f, cx, cy + (f == &KF_TEXT ? 7 : 5), lab, tc);
}
static void draw_keys(void)
{
    int nr, y0, rh;
    const krow_t *R = rows_now(&nr, &y0, &rh);
    for (int r = 0; r < nr; r++) {
        float tot = 0;
        for (int i = 0; i < R[r].n; i++) {
            tot += R[r].k[i].u;
        }
        int rx, rw;
        row_span(y0 + r * rh, rh, &rx, &rw);
        float u = rw / tot, xx = (float)rx;
        for (int i = 0; i < R[r].n; i++) {
            box_t b = {(int)xx, y0 + r * rh, (int)(xx + R[r].k[i].u * u) - (int)xx, rh};
            key_face(&R[r].k[i], b, &R[r].k[i] == s_kdown);
            xx += R[r].k[i].u * u;
        }
    }
    // the key preview: after 0.2 s, above the finger
    if (s_kdown && s_now - s_kt0 >= 200 && s_kdown->act != KA_MOD) {
        box_t b = s_kbox;
        int pw = b.w + 16 > 64 ? b.w + 16 : 64, ph = 44, px = b.x + b.w / 2 - pw / 2, py = b.y - ph - 4;
        if (py < s_bar) {
            py = s_bar;
        }
        if (px < s_strip + 2) {
            px = s_strip + 2;
        }
        if (px + pw > s_w - 2) {
            px = s_w - 2 - pw;
        }
        ui_rrect(px, py, pw, ph, 10, rgb(250, 250, 255));
        const char *lab = s_labels && s_kdown->he ? s_kdown->he : s_kdown->lab;
        ui_text_c(strlen(lab) > 2 ? &KF_TEXT : &KF_BIG, px + pw / 2, py + ph / 2 + 10, lab, rgb(20, 20, 40));
    }
}
static void draw_pad(void)
{
    box_t pb = pad_box();
    ui_rrect(pb.x + 4, pb.y + 4, pb.w - 8, pb.h - 8, 12, rgb(26, 30, 48));
    for (int y = pb.y + 16; y < pb.y + pb.h - 8; y += 18) {
        for (int x = pb.x + 16; x < pb.x + pb.w - 40; x += 18) {
            gfx_fill_rect(x, y, 2, 2, rgb(48, 54, 80));
        }
    }
    // the wheel strip at the right side of the pad
    int sw = s_wide ? 34 : 26, sx = pb.x + pb.w - sw - 4;
    gfx_fill_rect(sx, pb.y + 8, 1, pb.h - 16, rgb(60, 66, 96));
    arrow(sx + sw / 2, pb.y + 20, 1, 6, rgb(110, 116, 150));
    arrow(sx + sw / 2, pb.y + pb.h - 20, 2, 6, rgb(110, 116, 150));
    if (s_f1 && !s_was_two) {
        ui_circle((int)s_fx, (int)s_fy, 14, rgb(110, 170, 255), 120);
    }
    if (s_drag) {
        ui_text_c(&KF_LABEL, pb.x + pb.w / 2, pb.y + 18, T("DRAG", "גרירה"), rgb(255, 210, 90));
    }
    if (s_page == PG_MOUSE) {
        for (int r = 0; r < 2; r++) {
            box_t b = lr_box(r);
            bool on = s_side & (1 << r);
            ui_rrect(b.x, b.y, b.w, b.h, 10, on ? rgb(90, 150, 230) : rgb(52, 56, 84));
            ui_text_c(&KF_BIG, b.x + b.w / 2, b.y + b.h / 2 + 10, r ? "R" : "L", rgb(235, 235, 250));
        }
    }
    // not linked: the help for the pairing in the middle of the pad
    pad_state_t st = bt_pad_state();
    if (st != PAD_READY) {
        // (two lines: in a Hebrew line the words of the English name would turn round)
        const char *help =
                T("ON THE COMPUTER: ADD A BLUETOOTH DEVICE AND CHOOSE", "במחשב: הוסיפו התקן Bluetooth ובחרו את");
        if (ui_text_w(&KF_LABEL, help) <= pb.w - 12) {
            ui_text_c(&KF_LABEL, pb.x + pb.w / 2, pb.y + pb.h / 2 - 6, help, rgb(200, 200, 225));
            ui_text_c(&KF_TEXT, pb.x + pb.w / 2, pb.y + pb.h / 2 + 18, bt_pad_name(), rgb(150, 200, 255));
        } else {
            // (a narrow pad, the 320 x 240 frame: the help on two lines, cut at the space nearest to its middle)
            char l1[96], l2[96];
            const int n = (int)strlen(help);
            int cut = -1;
            for (int d = 0; d < n / 2 && cut < 0; d++) {
                if (help[n / 2 + d] == ' ') {
                    cut = n / 2 + d;
                } else if (help[n / 2 - d] == ' ') {
                    cut = n / 2 - d;
                }
            }
            if (cut < 0) {
                cut = n / 2;
            }
            snprintf(l1, sizeof l1, "%.*s", cut, help);
            snprintf(l2, sizeof l2, "%s", help + cut + (help[cut] == ' '));
            ui_text_c(&KF_LABEL, pb.x + pb.w / 2, pb.y + pb.h / 2 - 16, l1, rgb(200, 200, 225));
            ui_text_c(&KF_LABEL, pb.x + pb.w / 2, pb.y + pb.h / 2, l2, rgb(200, 200, 225));
            ui_text_c(&KF_TEXT, pb.x + pb.w / 2, pb.y + pb.h / 2 + 24, bt_pad_name(), rgb(150, 200, 255));
        }
    }
}
static void draw_media(void)
{
    for (int i = 0; i < 6; i++) {
        box_t b = media_box(i);
        ui_rrect(b.x, b.y, b.w, b.h, 12,
                 s_media_down == i ? rgb(90, 150, 230)
                 : i == 1          ? rgb(60, 90, 140)
                                   : rgb(48, 52, 80));
        int cx = b.x + b.w / 2, cy = b.y + b.h / 2 - (s_wide ? 6 : 8);
        uint16_t c = rgb(240, 240, 255);
        int s = s_wide ? 9 : 10;
        if (i == 0) {
            arrow(cx - s / 2, cy, 0, s, c);
            gfx_fill_rect(cx - s * 3 / 2 - 3, cy - s, 3, 2 * s, c);
        }
        if (i == 1) {
            arrow(cx - s / 2, cy, 3, s, c);
            gfx_fill_rect(cx + s, cy - s, 3, 2 * s, c);
            gfx_fill_rect(cx + s + 6, cy - s, 3, 2 * s, c);
        }
        if (i == 2) {
            arrow(cx + s / 2, cy, 3, s, c);
            gfx_fill_rect(cx + s * 3 / 2, cy - s, 3, 2 * s, c);
        }
        if (i >= 3) {
            gfx_fill_rect(cx - s, cy - s / 3, s / 2, 2 * s / 3, c);
            tri(cx - s / 2, cy - s / 3, cx + s / 4, cy - s, cx + s / 4, cy + s, c);
            tri(cx - s / 2, cy + s / 3, cx - s / 2, cy - s / 3, cx + s / 4, cy + s, c);
            if (i == 5) {
                gfx_fill_rect(cx + s / 2 + 4, cy - 1, 9, 3, c);
                gfx_fill_rect(cx + s / 2 + 7, cy - 4, 3, 9, c);
            }
            if (i == 3) {
                gfx_fill_rect(cx + s / 2 + 4, cy - 1, 9, 3, c);
            }
            if (i == 4) {
                tri(cx + s / 2 + 2, cy - 5, cx + s / 2 + 5, cy - 5, cx + s / 2 + 14, cy + 5, rgb(255, 120, 110));
                tri(cx + s / 2 + 2, cy + 5, cx + s / 2 + 5, cy + 5, cx + s / 2 + 14, cy - 5, rgb(255, 120, 110));
            }
        }
        ui_text_c(s_rnd && ui_text_w(&KF_TEXT, MEDIA_T[i][s_lang]) <= b.w - 6 ? &KF_TEXT : &KF_LABEL, cx, b.y + b.h - 8,
                  MEDIA_T[i][s_lang], rgb(200, 200, 225));
    }
}
static void render(int64_t now)
{
    s_now = now;
    if (s_naming) {
        kbd_render(now);
        return;
    }
    gfx_fill_rect(0, 0, s_w, s_h, rgb(14, 16, 28));
    // the tabs and the state of the link
    for (int i = 0; i < PG_N; i++) {
        box_t b = tab_box(i);
        ui_rrect(b.x, b.y, b.w, b.h, 8, i == s_page ? rgb(70, 120, 200) : rgb(34, 38, 60));
        const kfont_t *tf = (s_rnd || s_strip) && ui_text_w(&KF_TEXT, TAB[i][s_lang]) <= b.w - 6 ? &KF_TEXT : &KF_LABEL;
        ui_text_c(tf, b.x + b.w / 2, b.y + b.h / 2 + (tf == &KF_TEXT ? 6 : 5), TAB[i][s_lang], rgb(240, 240, 255));
    }
    pad_state_t st = bt_pad_state();
    uint16_t dot;
    const char *s;
    switch (st) {
    case PAD_READY:
        dot = rgb(80, 255, 120);
        s = T("CONNECTED", "מחובר");
        break;
    case PAD_CONNECTED:
        dot = rgb(255, 210, 60);
        s = T("PAIRING", "מתחבר");
        break;
    case PAD_ADVERTISING:
        dot = ((now / 400) & 1) ? rgb(80, 160, 255) : rgb(30, 60, 110);
        s = T("VISIBLE", "גלוי");
        break;
    case PAD_FAILED:
        dot = rgb(255, 80, 80);
        s = T("BT ERROR", "שגיאת BT");
        break;
    default:
        dot = rgb(120, 120, 140);
        s = T("STARTING", "מתחיל");
        break;
    }
    if (s_rnd) {  // (the state of the link at the top of the circle, over the tabs)
        const int sw = ui_text_w(&KF_TEXT, s), y = rs(40);
        ui_circle(s_w / 2 - sw / 2 - 10, y - 6, 5, dot, 256);
        ui_text_c(&KF_TEXT, s_w / 2 + 6, y, s, rgb(200, 200, 220));
    } else if (s_strip) {
        // (the 3.49: the state under the tabs; under it the dot and BOOT when the BOOT key is a key, else the name)
        const int y = PG_N * STRIP_TAB_H, cw = s_strip - 4;
        ui_text_c(&KF_LABEL, 2 + cw / 2, y + 12, s, rgb(200, 200, 220));
        const char *nm = bt_pad_boot() ? "BOOT" : bt_pad_name();
        const int nw = ui_text_w(&KF_LABEL, nm);
        if (nw + 12 <= cw) {
            ui_circle(2 + (cw - nw - 12) / 2 + 4, y + 21, 4, dot, 256);
            ui_text_c(&KF_LABEL, 2 + (cw - nw - 12) / 2 + 12 + nw / 2, y + 25, nm,
                      bt_pad_boot() ? rgb(255, 210, 90) : rgb(150, 150, 175));
        } else {
            ui_circle(2 + cw / 2, y + 21, 4, dot, 256);
        }
    } else {
        ui_circle(s_w - 10, s_bar / 2, 5, dot, 256);
    }
    if (bt_pad_boot() && !s_rnd && !s_strip) {
        ui_text_c(&KF_LABEL, tab_box(PG_N - 1).x + tab_box(0).w + 30, s_bar / 2 + 5, "BOOT", rgb(255, 210, 90));
    }
    if (s_page == PG_MOUSE || s_page == PG_BOTH) {
        draw_pad();
    }
    if (s_page == PG_KEYS || s_page == PG_BOTH) {
        draw_keys();
    }
    if (s_page == PG_MEDIA) {
        draw_media();
    }
}

#include "tficon/tficon.h"
// the menu icon: a keyboard; its keys press one after another, the Bluetooth sign sends waves
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0x5c8dff), tfi_hex(0x1a3cb0));
    float u = s / 100.0f, ph = tfi_ph(2);
    int kx = x + (int)(12 * u), ky = y + (int)(44 * u), kw = (int)(76 * u), kh = (int)(40 * u);
    tfi_drop(kx, ky, kw, kh, (int)(7 * u), (int)(3 * u), 90);
    tfi_gloss_box(kx, ky, kw, kh, (int)(7 * u), tfi_hex(0xf4f6fb), tfi_hex(0xb8c2d8));
    static const uint8_t SEQ[8] = {3, 9, 14, 6, 11, 1, 16, 8};
    int hit = SEQ[(int)(ph * 8) & 7];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 6; c++) {
            int i = r * 6 + c;
            int bx = kx + (int)((5 + c * 11.2f + (r == 1 ? 3 : 0)) * u), by = ky + (int)((5 + r * 11) * u);
            int w = (int)(9 * u), h = (int)(8.5f * u);
            if (r == 2 && c >= 2 && c <= 3) {
                if (c == 3) {
                    continue;
                }
                w = (int)(20 * u);
            }
            bool d = i == hit;
            tfi_rbox(bx, by + (d ? (int)(1 * u) : 0), w, h, (int)(2 * u), d ? tfi_hex(0x5c8dff) : tfi_hex(0x5a6278),
                     d ? tfi_hex(0x2a5ad0) : tfi_hex(0x343a4c), 256);
        }
    }
    // the Bluetooth sign in a disc, with waves
    float bx = x + 50 * u, by = y + 24 * u;
    float q = ph * 2 - (int)(ph * 2);
    tfi_ring(bx, by, (10 + q * 10) * u, 2.4f * u, rgb(255, 255, 255), (int)(200 * (1 - q)));
    tfi_disc(bx, by, 11 * u, tfi_hex(0x4aa0ff), tfi_hex(0x0a5ad8), 256);
    uint16_t w = rgb(255, 255, 255);
    float lw = 1.8f * u;
    tfi_line(bx - 1 * u, by - 7 * u, bx - 1 * u, by + 7 * u, lw, true, w, 256);
    tfi_line(bx - 1 * u, by - 7 * u, bx + 4 * u, by - 3 * u, lw, true, w, 256);
    tfi_line(bx + 4 * u, by - 3 * u, bx - 5 * u, by + 3.5f * u, lw, true, w, 256);
    tfi_line(bx - 1 * u, by + 7 * u, bx + 4 * u, by + 3 * u, lw, true, w, 256);
    tfi_line(bx + 4 * u, by + 3 * u, bx - 5 * u, by - 3.5f * u, lw, true, w, 256);
    tfi_end();
}

const app_t app_btkeys = {
        .name = "BT KEYS",
        .landscape = true,  // 640 x 172 on the 3.49, 320 x 240 on the other boards
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
        .round = APP_ROUND_HI,  // (the round boards: the round layout of layout())
};
