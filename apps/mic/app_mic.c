// MIC: the board as a USB microphone for a computer.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_mic at the end of this file);
// the USB sound runs in the tasks of main/usb_audio.c.
// The memory: the file statics of this file; no heap.
//
// A USB-C cable to a PC or a Mac: the computer finds the sound input "TFN SOUND" with no driver. The source: the microphone of the board, the sound of
// the board (games, music, ON AIR), or both. The screen: the state of the link, a level meter, the three sources.
// main/usb_audio.c has the USB part; while the app is open the console port of the board is not on the computer.
// When the app closes, USB AUDIO closes too, except with the setting SOUND TO USB (then the sound of the board stays).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfn/usb_audio.h"

static int s_lang = 1;  // 0 English, 1 Hebrew
static int s_src = UA_SRC_MIC;
static float s_lvl, s_peak;  // the meter (dB 0..1), the peak mark
static int s_press = -1;     // the button under the finger

static const int SRCS[3] = {UA_SRC_MIC, UA_SRC_BOARD, UA_SRC_MIC | UA_SRC_BOARD};
static const char *const BTN[3][2] = {{"MIC", "מיקרופון"}, {"BOARD SOUND", "צלילי הלוח"}, {"BOTH", "שניהם"}};
// (Hebrew: the text engine keeps the order of an English phrase in a Hebrew line, "TFN SOUND": libs/bidi/bidi.h)
static const char *T(const char *en, const char *he)
{
    return s_lang ? he : en;
}

// ---- the layout: from the size of the frame (640 x 172 on the 3.49, 320 x 240 on the others)
static int s_l_stat, s_l_sub, s_l_my, s_l_mh, s_l_by, s_l_bh;
static void layout(void)
{
    int H = gfx_h;
    if (gfx_w >= 3 * gfx_h) {
        s_l_stat = 40;
        s_l_sub = 62;
        s_l_my = 74;
        s_l_mh = 22;
        s_l_by = 106;
        s_l_bh = 60;
    }
#if BOARD_UI_TABLET
    // (the Tab5, 640 x 360: a big title at the top, a tall meter, big buttons)
    else if (gfx_w >= 600) {
        s_l_stat = H * 16 / 100;
        s_l_sub = H * 27 / 100;
        s_l_my = H * 33 / 100;
        s_l_mh = H * 17 / 100;
        s_l_by = H * 57 / 100;
        s_l_bh = H * 40 / 100;
    }
#endif
    else {
        s_l_stat = H * 22 / 100;
        s_l_sub = H * 33 / 100;
        s_l_my = H * 40 / 100;
        s_l_mh = H * 11 / 100;
        s_l_by = H * 56 / 100;
        s_l_bh = H * 40 / 100;
    }
}
// the round panel: the level meter is an arc along the edge; a microphone and the state in the middle; the three
// sources in a row under them (the row follows the circle)
static void btn_rect(int i, int *x, int *y, int *w, int *h)
{
    if (gfx_round()) {
        int W = gfx_w, R = W / 2, y0 = W * 300 / 466, h0 = W * 70 / 466, dy = y0 + h0 - gfx_h / 2;
        int half = (int)sqrtf((float)(R * R - dy * dy)) - W * 16 / 466, gap = 8, bw = (2 * half - 2 * gap) / 3;
        int k = s_lang ? 2 - i : i;
        *x = W / 2 - half + k * (bw + gap);
        *y = y0;
        *w = bw;
        *h = h0;
        return;
    }
    int gap = 8, bw = (gfx_w - 4 * gap) / 3;
    // (Hebrew: the first button at the right)
    int k = s_lang ? 2 - i : i;
    *x = gap + k * (bw + gap);
    *y = s_l_by;
    *w = bw;
    *h = s_l_bh;
}

// ---- the panel: the language
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const pitem_t ITEMS[] = {{PI_PICK, 0, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "mic_lang"}};
static void item_set(int id, int v)
{
    if (id == 0) {
        s_lang = v;
    }
}
static const pitems_t PANEL = {ITEMS, 1, NULL, NULL, item_set, NULL, NULL};

static void enter(int64_t now)
{
    (void)now;
    s_lang = store_get("mic_lang", 1) ? 1 : 0;
    s_src = (int)store_get("mic_src", UA_SRC_MIC);
    if (s_src < 1 || s_src > 3) {
        s_src = UA_SRC_MIC;
    }
    s_lvl = s_peak = 0;
    s_press = -1;
    layout();
    sysui_panel_items(&PANEL);
    usb_audio_open(s_src);
}

static void leave(void)
{
    // SOUND TO USB: the sound of the board stays on the USB; else the console port comes back
    if (usb_audio_setting()) {
        usb_audio_set_src(UA_SRC_BOARD);
    } else {
        usb_audio_close();
    }
}

static void input(const input_event_t *ev)
{
    int hit = -1;
    for (int i = 0; i < 3; i++) {
        int x, y, w, h;
        btn_rect(i, &x, &y, &w, &h);
        if (ev->x >= x && ev->x < x + w && ev->y >= y - 6 && ev->y < y + h + 6) {
            hit = i;
        }
    }
    if (ev->kind == IN_DOWN) {
        s_press = hit;
    } else if (ev->kind == IN_UP) {
        s_press = -1;
    } else if (ev->kind == IN_TAP && hit >= 0) {
        s_src = SRCS[hit];
        store_set("mic_src", (uint32_t)s_src);
        usb_audio_set_src(s_src);
        if (!(s_src & UA_SRC_BOARD)) {
            sfx_play(SFX_CLICK);  // (with the board's sound, a click goes to the computer too)
        }
    }
}

static void update(int64_t now)
{
    (void)now;
    float l = usb_audio_level();
    float db = l > 0.00001f ? 20.0f * log10f(l) : -100;
    float v = (db + 60) / 60;
    if (v < 0) {
        v = 0;
    }
    if (v > 1) {
        v = 1;
    }
    s_lvl = v > s_lvl ? v : s_lvl * 0.85f + v * 0.15f;
    s_peak = v > s_peak ? v : s_peak - 0.008f;
    if (s_peak < 0) {
        s_peak = 0;
    }
}

// a line of text in at most 2 lines of the font f, centred at cx (the break at the space nearest to the middle)
static void text2(const kfont_t *f, int cx, int base, int maxw, const char *s, uint16_t col)
{
    if (ui_text_w(f, s) <= maxw) {
        ui_text_c(f, cx, base, s, col);
        return;
    }
    int n = (int)strlen(s), best = -1;
    for (int i = 0; i < n; i++) {
        if (s[i] == ' ' && (best < 0 || (i - n / 2) * (i - n / 2) < (best - n / 2) * (best - n / 2))) {
            best = i;
        }
    }
    if (best < 0 || best > 63) {
        ui_text_c(f, cx, base, s, col);
        return;
    }
    char a[64];
    memcpy(a, s, (size_t)best);
    a[best] = 0;
    ui_text_c(f, cx, base, a, col);
    ui_text_c(f, cx, base + f->line, s + best + 1, col);
}

// (round) the meter: discs along an arc of 210 degrees over the top; green, yellow, red; the peak is white
static void meter_round(void)
{
    int W = gfx_w;
    float cx = W * 0.5f, cy = gfx_h * 0.5f, rr = W * 0.5f - W * 26 / 466.0f, dr = W * 9.5f / 466;
    const int N = 36;
    int lit = (int)(s_lvl * N + 0.5f), pk = (int)(s_peak * (N - 1) + 0.5f);
    for (int i = 0; i < N; i++) {
        float t = (float)i / (N - 1), a = (165 + 210 * t) * 0.0174533f;
        int x = (int)(cx + rr * fcos(a)), y = (int)(cy + rr * fsin(a));
        uint16_t c = t < 0.7f ? rgb(70, 210, 110) : t < 0.88f ? rgb(240, 200, 60) : rgb(240, 80, 60);
        if (i == pk && s_peak > 0.01f) {
            c = rgb(255, 255, 255);
        } else if (i >= lit) {
            c = ui_mix(rgb(24, 26, 44), c, 50);
        }
        ui_circle(x, y, (int)dr, c, 256);
    }
}

// (round) a studio microphone in the top of the middle; its grille lights with the level
static void mic_glyph(int cx, int cy, int u, bool on, int64_t now)
{
    uint16_t body = rgb(220, 224, 236), dark = rgb(80, 88, 110);
    gfx_fill_rect(cx - u, cy + u * 21, u * 2, u * 8, body);
    ui_rrect(cx - u * 9, cy + u * 28, u * 18, u * 3, u, body);
    ui_rrect(cx - u * 10, cy - u * 20, u * 20, u * 36, u * 10, body);
    for (int k = 0; k < 4; k++) {
        gfx_fill_rect(cx - u * 7, cy - u * 13 + k * u * 6, u * 14, u, dark);
    }
    if (on && ((now / 500000) & 1)) {
        ui_circle(cx + u * 22, cy - u * 16, u * 4, rgb(255, 60, 50), 256);  // (a red dot: on air)
    }
}

static void render_round(int64_t now, const char *a, const char *b, uint16_t ca, bool on)
{
    int W = gfx_w;
    meter_round();
    mic_glyph(W / 2, W * 100 / 466, 2, on, now);
    int tw = W * 76 / 100, two = ui_text_w(&KF_BIG, a) > tw;
    text2(&KF_BIG, W / 2, W * (two ? 200 : 214) / 466, tw, a, ca);
    text2(&KF_TEXT, W / 2, W * (two ? 262 : 250) / 466, tw, b, rgb(180, 176, 200));
    for (int i = 0; i < 3; i++) {
        int x, y, w, h;
        btn_rect(i, &x, &y, &w, &h);
        bool sel = s_src == SRCS[i];
        ui_rrect(x, y, w, h, 16, sel ? rgb(60, 130, 210) : s_press == i ? rgb(60, 60, 90) : rgb(36, 36, 60));
        if (sel) {
            ui_rrect(x + 8, y + 4, w - 16, 3, 2, rgb(150, 200, 255));
        }
        const char *s = BTN[i][s_lang];
        uint16_t c = sel ? rgb(255, 255, 255) : rgb(200, 196, 220);
        if (ui_text_w(&KF_TEXT, s) <= w - 12 || !strchr(s, ' ')) {
            ui_text_c(&KF_TEXT, x + w / 2, y + h / 2 + 7, s, c);
        } else {
            text2(&KF_TEXT, x + w / 2, y + h / 2 - KF_TEXT.line / 2 + 7, w - 8, s, c);
        }
    }
}

static void render(int64_t now)
{
    int W = gfx_w, H = gfx_h;
    // the background: a dark blue, a little lighter at the top
    for (int y = 0; y < H; y++) {
        gfx_fill_rect(0, y, W, 1,
                      rgb((uint8_t)(10 + 14 * (H - y) / H), (uint8_t)(12 + 12 * (H - y) / H),
                          (uint8_t)(26 + 20 * (H - y) / H)));
    }
    // the state of the link
    ua_state_t st = usb_audio_state();
    bool on = usb_audio_streaming();
    const char *a, *b;
    uint16_t ca = rgb(240, 236, 255);
    if (st == UA_FAILED) {
        a = T("USB IS BUSY", "ה-USB תפוס");
        b = T("CLOSE THE MIDI APP AND OPEN MIC AGAIN", "סגרו את MIDI ופתחו שוב את MIC");
        ca = rgb(255, 140, 120);
    } else if (st != UA_LINKED) {
        a = T("CONNECT TO A COMPUTER", "חברו למחשב");
        b = T("A USB-C CABLE TO A PC OR A MAC", "כבל USB-C למחשב PC או Mac");
    } else if (!on) {
        a = T("READY: TFN SOUND", "מוכן: TFN SOUND");
        b = T("CHOOSE IT AS THE MICROPHONE ON THE COMPUTER", "בחרו בו כמיקרופון במחשב");
        ca = rgb(150, 230, 170);
    } else {
        a = T("THE COMPUTER HEARS THE BOARD", "המחשב שומע את הלוח");
        b = T("TFN SOUND IS ON", "TFN SOUND פועל");
        ca = rgb(255, 200, 90);
    }
    if (gfx_round()) {
        render_round(now, a, b, ca, on);
        return;
    }
#if BOARD_UI_TABLET
    const kfont_t *fa = ui_text_w(&KF_TITLE, a) <= W - 16 ? &KF_TITLE : &KF_BIG, *fb = &KF_TEXT;
#else
    const kfont_t *fa = ui_text_w(&KF_BIG, a) <= W - 16 ? &KF_BIG : &KF_TEXT, *fb = &KF_LABEL;
#endif
    ui_text_c(fa, W / 2, s_l_stat, a, ca);
    ui_text_c(fb, W / 2, s_l_sub, b, rgb(180, 176, 200));
    if (on && ((now / 500000) & 1)) {
        ui_circle(14, s_l_stat - 10, 6, rgb(255, 60, 50), 256);  // (a red dot: on air)
    }
    // the level meter: green, then yellow, then red; the peak mark
    int mx = 12, mw = W - 24;
    ui_rrect(mx - 2, s_l_my - 2, mw + 4, s_l_mh + 4, 6, rgb(30, 30, 50));
    int n = (int)(s_lvl * mw);
    for (int x = 0; x < n; x += 6) {
        float t = (float)x / mw;
        uint16_t c = t < 0.7f ? rgb(70, 210, 110) : t < 0.88f ? rgb(240, 200, 60) : rgb(240, 80, 60);
        gfx_fill_rect(mx + x, s_l_my, x + 4 < n ? 4 : n - x, s_l_mh, c);
    }
    gfx_fill_rect(mx + (int)(s_peak * (mw - 3)), s_l_my, 3, s_l_mh, rgb(255, 255, 255));
    // the three sources
    for (int i = 0; i < 3; i++) {
        int x, y, w, h;
        btn_rect(i, &x, &y, &w, &h);
        bool sel = s_src == SRCS[i];
        ui_rrect(x, y, w, h, 12, sel ? rgb(60, 130, 210) : s_press == i ? rgb(60, 60, 90) : rgb(36, 36, 60));
        if (sel) {
            ui_rrect(x + 4, y + 3, w - 8, 3, 2, rgb(150, 200, 255));
        }
        const char *s = BTN[i][s_lang];
#if BOARD_UI_TABLET
        const kfont_t *f = ui_text_w(&KF_BIG, s) <= w - 12 ? &KF_BIG : &KF_TEXT;
        int dy = f == &KF_BIG ? 9 : 7;
#else
        const kfont_t *f = ui_text_w(&KF_TEXT, s) <= w - 12 ? &KF_TEXT : &KF_LABEL;
        int dy = 7;
#endif
        ui_text_c(f, x + w / 2, y + h / 2 + dy, s, sel ? rgb(255, 255, 255) : rgb(200, 196, 220));
    }
}

static bool awake(void)
{
    return usb_audio_streaming();
}

#include "tficon/tficon.h"
// the menu icon: a studio microphone; the level bars beside it go up and down with the sound
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0xff5f8f), tfi_hex(0x9c1757));
    float u = s / 100.0f, ph = tfi_ph(2);
    float cx = x + 42 * u;
    // the stand, the mount
    tfi_rbox((int)(cx - 2 * u), y + (int)(66 * u), (int)(4 * u), (int)(14 * u), (int)(1 * u), tfi_hex(0xd8dce6),
             tfi_hex(0x8a90a0), 256);
    tfi_rbox((int)(cx - 14 * u), y + (int)(78 * u), (int)(28 * u), (int)(6 * u), (int)(3 * u), tfi_hex(0xd8dce6),
             tfi_hex(0x6a7080), 256);
    tfi_arc(cx, y + 46 * u, 18 * u, 3.6f * u, 0.15f, 2.99f, tfi_hex(0xe8ecf4), 256);
    // the capsule with its grille
    tfi_drop((int)(cx - 12 * u), y + (int)(14 * u), (int)(24 * u), (int)(44 * u), (int)(12 * u), (int)(3 * u), 90);
    tfi_gloss_box((int)(cx - 12 * u), y + (int)(14 * u), (int)(24 * u), (int)(44 * u), (int)(12 * u), tfi_hex(0xf4f6fb),
                  tfi_hex(0x8a92a8));
    for (int k = 0; k < 4; k++) {
        tfd_rect((int)(cx - 9 * u), y + (int)((22 + k * 6) * u), (int)(18 * u),
                 (int)(1.8f * u) > 1 ? (int)(1.8f * u) : 1, tfi_hex(0x5a6278), 200);
    }
    // the level bars
    for (int k = 0; k < 3; k++) {
        float lv = 0.35f + 0.6f * fabsf(fsin(ph * 6.2831853f * (k + 1) + k * 0.8f));
        int bh = (int)(40 * u * lv), bx = x + (int)((64 + k * 8) * u), by = y + (int)(76 * u) - bh;
        uint16_t c = lv > 0.8f ? tfi_hex(0xffd84a) : tfi_hex(0x6dff8a);
        tfi_rbox(bx, by, (int)(5 * u), bh, (int)(2.5f * u), tfi_light(c, 60), c, 256);
    }
    tfi_end();
}

const app_t app_mic = {
        .name = "MIC",
        .landscape = true,  // 640 x 172 on the 3.49, 320 x 240 on the other boards
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
        .round = APP_ROUND_HI,
};
