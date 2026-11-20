// COMPANIONS (UTIL): the TFN COMPANIONS of this board (small ESP32 boards with no screen: companion/ of the repo).
// Pair a companion (a press of its BOOT key), give it a name, tell which module is on which GPIO (the companion checks
// the pins of its chip and keeps the wiring), see the live signal of a receiver, send a test pulse on a transmitter,
// forget it. The service of TFN OS (tfn/companion.h) keeps the list; CONTROL and the other apps use the same list.
// The UI: libs/tapui (sizes in mm, grids that fill the room, a long press = the menu of an item).
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "tapui/tapui.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/companion.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"

static bool s_he;
#include "cm_text.inc"

// the tunes of the app (the notes one after the other)
static const tune_note_t TUNE_FAIL[] = {{0, 260, 64, -5, W_TRI | TUNE_HOLD, 16}};
static const tune_note_t TUNE_WAIT[] = {{0, 160, 81, 0, W_SINE | TUNE_HOLD, 14}};
static const tune_note_t TUNE_PAIRED[] = {
        {0, 100, 84, 0, W_SINE, 15}, {110, 100, 88, 0, W_SINE, 15}, {220, 360, 96, 0, W_SINE | TUNE_HOLD, 14}};

enum {
    S_LIST,
    S_ADD,
    S_PAIR,
    S_COMP,
    S_MODULE,
    S_ROLE,
    S_PIN,
    S_UPDATE  // the update of the firmware of a companion by radio (cm_update.inc)
};
static int s_scr;
static int s_ci = -1;      // the companion now
static int s_mod = -1;     // the module now (an index of its wiring)
static int s_role;         // the role of the module that the user adds
static int s_change = -1;  // CHANGE PIN: the module whose pin changes (-1: a new module)
static int64_t s_now;
static bool s_from_remote;  // CONTROL opened this app: BACK of the list goes back to CONTROL

// a wiring change that waits for the companion
static struct {
    bool on;
    int back;  // the screen after the answer
} s_save;

// the keyboard
static bool s_typing;
static char s_kbuf[17];

static void go(int scr)
{
    if (s_scr == S_MODULE && scr != S_MODULE && s_ci >= 0) {
        tcm_watch(s_ci, 0, false);
    }
    s_scr = scr;
    tu_scroll_reset();
}

static const tcm_comp_t *cur(void)
{
    return tcm_get(s_ci);
}

// the colour of a role: a receiver green, a transmitter orange, a switch output blue, an input or a sensor grey
static int role_style(int r)
{
    switch (tc_role_class(r)) {
    case TC_C_PULSE_RX:
        return TU_K_ACC;
    case TC_C_PULSE_TX:
        return TU_K_HOT;
    case TC_C_OUT:
        return TU_K_PRI;
    default:
        return TU_K_NORMAL;
    }
}

static uint16_t role_colour(int r)
{
    switch (tc_role_class(r)) {
    case TC_C_PULSE_RX:
        return TU_ACC;
    case TC_C_PULSE_TX:
        return TU_HOT;
    case TC_C_OUT:
        return TU_PRI;
    default:
        return TU_DIM;
    }
}

// the word of the state of a switch module ("ON", "PRESSED", "MOTION", "CLOSED" ...)
static const char *state_word(int role, int v)
{
    static const char *const W[][4] = {{"PRESSED", "UP", "לחוץ", "משוחרר"},
                                       {"MOTION", "STILL", "תנועה", "שקט"},
                                       {"CLOSED", "OPEN", "סגור", "פתוח"}};
    const int k = role == TC_R_BUTTON ? 0 : role == TC_R_MOTION ? 1 : role == TC_R_CONTACT ? 2 : -1;
    if (k < 0) {
        return T(v ? T_ON : T_OFF);
    }
    return W[k][(v ? 0 : 1) + (s_he ? 2 : 0)];
}

static void mac_tail(const uint8_t *m, char *o, int n)
{
    snprintf(o, (size_t)n, "%02X%02X", m[4], m[5]);
}

// the modules of a companion as one line ("IR RECEIVER, IR LED")
static void modules_line(const tcm_comp_t *c, char *o, int n)
{
    o[0] = 0;
    if (!c->has_info || !c->info.nwire) {
        snprintf(o, (size_t)n, "%s", tc_chip_name(c->chip));
        return;
    }
    int k = 0;
    for (int i = 0; i < c->info.nwire && k < n - 2; i++) {
        k += snprintf(o + k, (size_t)(n - k), "%s%s", i ? ", " : "", role_name(c->info.wire[i].role));
    }
}

// ---------------------------------------------------------------- the wiring
static void wiring_send(const tc_wire_t *w, int n, int back)
{
    if (!tcm_config(s_ci, w, n)) {
        return;
    }
    s_save.on = true;
    s_save.back = back;
}

static void wiring_tick(void)
{
    if (!s_save.on || tcm_req_type(s_ci) != TCM_R_CONFIG || tcm_req(s_ci) == TCM_BUSY) {
        return;
    }
    s_save.on = false;
    uint8_t err[TC_WIRE_MAX], warn[TC_WIRE_MAX];
    int n = 0;
    const bool ok = tcm_cfg_result(s_ci, err, warn, &n);
    if (ok) {
        tu_toast(T(T_SAVED), TU_ACC);
        go(s_save.back);
        return;
    }
    audio_tune(TUNE_FAIL, 1);
    if (!n) {
        tu_toast(T(T_NO_ANSWER), TU_BAD);
        return;
    }
    for (int i = 0; i < n; i++) {
        if (err[i]) {
            tu_toast(err_name(err[i]), TU_BAD);
        }
    }
}

static void module_add(int pin)
{
    const tcm_comp_t *c = cur();
    tc_wire_t w[TC_WIRE_MAX];
    int n = c->info.nwire;
    memcpy(w, c->info.wire, sizeof w);
    if (s_change >= 0 && s_change < n) {
        w[s_change].pin = (uint8_t)pin;
        wiring_send(w, n, S_COMP);
        return;
    }
    if (n >= TC_WIRE_MAX) {
        return;
    }
    w[n++] = (tc_wire_t){(uint8_t)pin, (uint8_t)s_role, 0, s_role == TC_R_IR_TX ? 38 : 0};
    wiring_send(w, n, S_COMP);
}

static void module_remove(void)
{
    const tcm_comp_t *c = cur();
    tc_wire_t w[TC_WIRE_MAX];
    int n = 0;
    for (int i = 0; i < c->info.nwire; i++) {
        if (i != s_mod) {
            w[n++] = c->info.wire[i];
        }
    }
    wiring_send(w, n, S_COMP);
}

#include "cm_update.inc"

// ---------------------------------------------------------------- the questions
static void on_forget(int yes)
{
    if (yes == 1 && s_ci >= 0) {
        tcm_forget(s_ci);
        s_ci = -1;
        go(S_LIST);
    }
}

static void on_remove(int yes)
{
    if (yes == 1) {
        module_remove();
    }
}

static void ask_forget(int ci)
{
    s_ci = ci;
    sysui_ask(tcm_get(ci)->name, T(T_FORGET_Q), T(T_FORGET), T(T_CANCEL), 0, on_forget);
}

static void rename_open(int ci)
{
    s_ci = ci;
    snprintf(s_kbuf, sizeof s_kbuf, "%s", tcm_get(ci)->name);
    kbd_open(T(T_NAME), s_kbuf, sizeof s_kbuf, false);
    kbd_set_hebrew(s_he);
    s_typing = true;
}

// ---------------------------------------------------------------- the drawing
static void waves(int cx, int cy, int r, uint16_t c)
{
    const float t = (float)(s_now % 1400) / 1400.0f;
    for (int k = 0; k < 3; k++) {
        float f = t + (float)k / 3.0f;
        f -= (float)(int)f;
        ui_ring(cx, cy, (int)((float)r * (0.3f + 0.7f * f)), TU_MM(0.5f) + 1, c, (int)(255 * (1.0f - f)));
    }
    ui_circle(cx, cy, r / 5, c, 256);
}

static void draw_list(void)
{
    tu_begin(0, s_now);
    const int n = tcm_count();
    const int rh = tu_row_h() + TU_MM(1);
    int y = T_.by0 - (int)tu_scroll();
    char b[48];
    snprintf(b, sizeof b, "+  %s", T(T_ADD));
    if (!n) {
        // no companion: the text and a big ADD key, both in full (before, the key went under the bottom edge of the
        // bar and of the 320 x 240 frame)
        tu_empty(T(T_EMPTY), b, TU_ID(TU_H_ITEM, 900));
        s_tu_content = 0;
        tu_chrome(T(T_APP), s_from_remote, NULL, 0, NULL, 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        const tcm_comp_t *c = tcm_get(i);
        char sub[64];
        modules_line(c, sub, sizeof sub);
        tu_card(y, rh - TU_MM(1.4f), c->name, sub, T(tcm_online(i) ? T_ONLINE : T_OFFLINE),
                tcm_online(i) ? TU_ACC : TU_FAINT, tcm_online(i) ? TU_ACC : TU_FAINT, TU_ID(TU_H_ITEM, i), true);
        y += rh;
    }
    // the card of ADD
    int x, w;
    const int ah = rh - TU_MM(1.4f);
    tu_row_x(y, ah, &x, &w);
    tu_key(x, y, w, ah, b, TU_K_GHOST, TU_ID(TU_H_ITEM, 900), true);
    y += rh;
    s_tu_content = (float)(y + (int)tu_scroll() - T_.by0);
    tu_chrome(T(T_APP), s_from_remote, NULL, 0, NULL, 0);
}

static void draw_add(void)
{
    tu_begin(0, s_now);
    tcm_found_t f[TCM_FOUND_MAX];
    const int n = tcm_found(f, TCM_FOUND_MAX);
    const int rh = tu_row_h() + TU_MM(1);
    int y = T_.by0 - (int)tu_scroll();
    if (!n) {
        const int r = (T_.by1 - T_.by0) / 5;
        waves(gfx_w / 2, T_.by0 + r + TU_MM(1), r, TU_PRI);
        tu_para(T_.f_key, T(T_SEARCHING), T_.bx0, T_.bx1 - T_.bx0, T_.by0 + 2 * r + TU_MM(3), T_.by1, TU_DIM);
    } else {
        y = tu_para(T_.f_small, T(T_FOUND_TIP), T_.bx0, T_.bx1 - T_.bx0, y + TU_MM(0.5f), T_.by1, TU_DIM) + TU_MM(1);
    }
    for (int i = 0; i < n; i++) {
        char t[32], s4[8], end[16];
        mac_tail(f[i].mac, s4, sizeof s4);
        snprintf(t, sizeof t, "%s  %s", tc_chip_name(f[i].chip), s4);
        snprintf(end, sizeof end, "%d dBm", f[i].rssi);
        tu_card(y, rh - TU_MM(1.4f), t, NULL, end, TU_DIM, TU_PRI, TU_ID(TU_H_ITEM, i), true);
        y += rh;
    }
    s_tu_content = (float)(y + (int)tu_scroll() - T_.by0);
    tu_chrome(T(T_ADD), true, NULL, 0, NULL, 0);
}

static void draw_pair(void)
{
    const int st = tcm_pair_state();
    tu_begin(st == -1 ? 1 : 0, s_now);
    const int r = (T_.by1 - T_.by0) * 3 / 10;
    if (st == -1) {
        tu_para(T_.f_key, T(T_PAIR_FAIL), T_.bx0, T_.bx1 - T_.bx0, (T_.by0 + T_.by1) / 2 - T_.f_key->line, T_.by1,
                TU_WARN);
    } else if (T_.fk == TU_FK_BAR) {
        waves(T_.bx0 + r + TU_MM(4), (T_.by0 + T_.by1) / 2, r, st == 3 ? TU_HOT : TU_PRI);
        const int x = T_.bx0 + 2 * r + TU_MM(8);
        int y = tu_para(T_.f_big, T(st == 3 ? T_PRESS_BOOT : T_CALLING), x, T_.bx1 - x, T_.by0 + TU_MM(4), T_.by1,
                        st == 3 ? TU_TEXT : TU_DIM);
        if (st == 3) {
            tu_para(T_.f_small, T(T_BOOT_TIP), x, T_.bx1 - x, y + TU_MM(1), T_.by1, TU_DIM);
        }
    } else {
        waves(gfx_w / 2, T_.by0 + r + TU_MM(1), r, st == 3 ? TU_HOT : TU_PRI);
        int y = tu_para(T_.f_big, T(st == 3 ? T_PRESS_BOOT : T_CALLING), T_.bx0, T_.bx1 - T_.bx0,
                        T_.by0 + 2 * r + TU_MM(3), T_.by1, st == 3 ? TU_TEXT : TU_DIM);
        if (st == 3) {
            tu_para(T_.f_small, T(T_BOOT_TIP), T_.bx0, T_.bx1 - T_.bx0, y + TU_MM(0.5f), T_.by1, TU_DIM);
        }
    }
    tu_chrome(T(T_PAIRING), true, NULL, 0, NULL, 0);
    if (st == -1) {
        tu_act(0, T(T_AGAIN), TU_K_PRI);
    }
}

static void draw_comp(void)
{
    const tcm_comp_t *c = cur();
    if (!c) {
        go(S_LIST);
        return;
    }
    tu_begin(T_.fk == TU_FK_ROUND ? 1 : 0, s_now);  // (the round frame: MORE in the bottom cap)
    const int rh = tu_row_h() + TU_MM(1);
    int y = T_.by0 - (int)tu_scroll();
    if (!c->has_info) {
        tu_para(T_.f_key, T(T_WAITING), T_.bx0, T_.bx1 - T_.bx0, y + TU_MM(2), T_.by1, TU_DIM);
        y += rh;
    } else {
        if (!c->info.nwire) {
            y = tu_para(T_.f_small, T(T_NO_MODULES), T_.bx0, T_.bx1 - T_.bx0, y + TU_MM(1), T_.by1, TU_DIM) +
                TU_MM(1.5f);
        }
        for (int k = 0; k < c->info.nwire; k++) {
            char e[16];
            snprintf(e, sizeof e, "GPIO %d", c->info.wire[k].pin);
            tu_card(y, rh - TU_MM(1.4f), role_name(c->info.wire[k].role), NULL, e, TU_DIM,
                    role_colour(c->info.wire[k].role), TU_ID(TU_H_ITEM, k), true);
            y += rh;
        }
        if (c->info.nwire < TC_WIRE_MAX) {
            int x, w;
            const int ah = rh - TU_MM(1.4f);
            tu_row_x(y, ah, &x, &w);
            char b[48];
            snprintf(b, sizeof b, "+  %s", T(T_ADD_MODULE));
            tu_key(x, y, w, ah, b, c->info.nwire ? TU_K_GHOST : TU_K_PRI, TU_ID(TU_H_ITEM, 900), true);
            y += rh;
        }
        // a newer firmware file for this chip: the UPDATE key (or how to get there)
        int nfw = 0;
        if (best_fw(c, &nfw) && nfw > c->info.fw) {
            if (c->info.fw >= 3) {
                int x, w;
                const int ah = rh - TU_MM(1.4f);
                tu_row_x(y, ah, &x, &w);
                char b[48];
                snprintf(b, sizeof b, T(T_UPDATE), nfw);
                tu_key(x, y, w, ah, b, TU_K_PRI, TU_ID(TU_H_ITEM, 901), true);
                y += rh;
            } else {
                y = tu_para(T_.f_small, T(T_UPD_USB), T_.bx0, T_.bx1 - T_.bx0, y, T_.by1 + 2000, TU_WARN) + TU_MM(1);
            }
        }
        char line[64], m4[8];
        mac_tail(c->mac, m4, sizeof m4);
        snprintf(line, sizeof line, "%s  %s   %s %d", tc_chip_name(c->chip), m4, T(T_FIRMWARE), c->info.fw);
        y += TU_MM(0.5f);
        int x, w;
        tu_row_x(y, T_.f_tiny->line, &x, &w);
        tu_text_c(T_.f_tiny, x + w / 2, w, y + T_.f_tiny->asc, line, TU_FAINT);
        y += T_.f_tiny->line + TU_MM(1);
    }
    s_tu_content = (float)(y + (int)tu_scroll() - T_.by0);
    const bool on = tcm_online(s_ci);
    tu_chrome(c->name, true, TU_MORE, TU_K_NORMAL, s_save.on ? T(T_SAVING) : T(on ? T_ONLINE : T_OFFLINE),
              s_save.on ? TU_WARN
              : on      ? TU_ACC
                        : TU_FAINT);
    if (T_.fk == TU_FK_ROUND) {
        tu_act(0, T(T_MORE), TU_K_NORMAL);
    }
}

// the live signal: the last level times as a square wave (the newest at the right)
static void wave_draw(const tcm_comp_t *c, int x, int y, int w, int h)
{
    ui_rbox(x, y, w, h, TU_MM(1.5f), TU_SHADE, TU_LINE, 1);
    if (c->watch_n <= 0) {
        return;
    }
    const int32_t Q = 20000;  // (a long quiet shows as 20 ms)
    int64_t span = 0;
    for (int i = 0; i < c->watch_n; i++) {
        const int32_t a = c->watch_d[i] < 0 ? -c->watch_d[i] : c->watch_d[i];
        span += a > Q ? Q : a;
    }
    const int iw = w - TU_MM(2);
    int64_t us_px = span / (iw > 0 ? iw : 1);
    if (us_px < 30) {
        us_px = 30;
    }
    int px = x + w - TU_MM(1);
    const int top = y + TU_MM(1.2f), bot = y + h - TU_MM(1.2f);
    for (int i = c->watch_n - 1; i >= 0 && px > x + TU_MM(1); i--) {
        const int32_t a = c->watch_d[i] < 0 ? -c->watch_d[i] : c->watch_d[i];
        int len = (int)((a > Q ? Q : a) / us_px);
        if (len < 1) {
            len = 1;
        }
        const int x0 = px - len < x + TU_MM(1) ? x + TU_MM(1) : px - len;
        gfx_fill_rect(x0, c->watch_d[i] > 0 ? top : bot - 2, px - x0 + 1, 3, c->watch_d[i] > 0 ? TU_HOT : TU_ACC);
        gfx_fill_rect(x0, top, 2, bot - top, TU_LINE);
        px = x0;
    }
}

// the live test of a switch or analog module: the value now, the keys of an output, the "active low" key. The bar: the
// value at the side, the keys next to it; the other frames: the value on top (smaller when the band is short)
static void draw_switch(const tcm_comp_t *c, const tc_wire_t *wi, int cls)
{
    (void)c;
    int v = 0, cnt = 0;
    const bool has = tcm_value(s_ci, wi->pin, &v, &cnt);
    const bool bar = T_.fk == TU_FK_BAR;
    const int band = T_.by1 - T_.by0;
    int y = T_.by0 + TU_MM(1) - (int)tu_scroll();
    int bx, bw, bh;
    // the column of the keys and the text (the bar: right of the value box)
    int cx0 = T_.bx0, cx1 = T_.bx1;
    if (bar) {
        bw = (T_.bx1 - T_.bx0) * 36 / 100;
        bh = band - TU_MM(2);
        bx = T_.he ? T_.bx1 - bw : T_.bx0;
        if (T_.he) {
            cx1 = bx - TU_MM(2);
        } else {
            cx0 = bx + bw + TU_MM(2);
        }
    } else {
        bh = band * 34 / 100;
        bh = bh > TU_MM(12) ? TU_MM(12) : bh < TU_MM(8) ? TU_MM(8) : bh;
        tu_row_x(y, bh, &bx, &bw);
    }
    // the value in a box: a lamp colour for an active switch
    char big[24];
    uint16_t col = TU_DIM, fill = TU_SURF;
    if (!has) {
        snprintf(big, sizeof big, "%s", "...");
    } else if (cls == TC_C_ADC) {
        snprintf(big, sizeof big, "%d mV", v);
        col = TU_TEXT;
    } else {
        snprintf(big, sizeof big, "%s", state_word(wi->role, v));
        col = v ? TU_BG : TU_DIM;
        fill = v ? (cls == TC_C_OUT ? TU_PRI : TU_ACC) : TU_SURF;
    }
    const int by = y;  // (the box scrolls with the keys: the length of the list does not change with the scroll)
    ui_rbox(bx, by, bw, bh, TU_MM(2), fill, TU_LINE, 1);
    const kfont_t *fb = bh >= TU_MM(11) ? T_.f_big : T_.f_key;
    int ty = by + (bh - fb->line) / 2 + fb->asc;
    if (cls == TC_C_IN && has) {
        ty -= T_.f_small->line / 2;  // (the count under the word)
    }
    tu_text_c(fb, bx + bw / 2, bw, ty, big, col);
    if (cls == TC_C_IN && has) {
        char line[32];
        snprintf(line, sizeof line, "%s  %d", T(T_COUNT), cnt);
        tu_text_c(T_.f_small, bx + bw / 2, bw, ty + fb->line - fb->asc + T_.f_small->asc, line, v ? TU_BG : TU_DIM);
    }
    if (has && cls == TC_C_ADC) {
        // a bar under the value (0 - 3300 mV)
        const int lw = (bw - TU_MM(4)) * (v > 3300 ? 3300 : v) / 3300;
        gfx_fill_rect(bx + TU_MM(2), by + bh - TU_MM(2), lw, TU_MM(0.8f), TU_ACC);
    }
    if (!bar) {
        y += bh + TU_MM(1.5f);
    }
    const char *tip = !has              ? T(T_NO_VALUE)
                      : cls == TC_C_OUT ? T(T_OUT_TIP)
                      : cls == TC_C_IN  ? T(T_IN_TIP)
                                        : T(T_ADC_TIP);
    int x, w;
    if (cls == TC_C_OUT) {
        // ON | OFF | PRESS
        const int kh = TU_MM(bar ? 8.5f : 9.5f), g = TU_MM(1.4f);
        if (bar) {
            x = cx0;
            w = cx1 - cx0;
        } else {
            tu_row_x(y, kh, &x, &w);
        }
        const int kw = (w - 2 * g) / 3;
        const char *lab[3] = {T(T_ON), T(T_OFF), T(T_PRESS)};
        for (int k = 0; k < 3; k++) {
            const int at = T_.he ? 2 - k : k;
            tu_key(x + at * (kw + g), y, kw, kh, lab[k], k == 0 ? TU_K_PRI : TU_K_NORMAL, TU_ID(TU_H_SUB, k + 1), true);
        }
        y += kh + TU_MM(1.4f);
    }
    if (cls != TC_C_ADC) {
        // the active level: many relay boards are on at LOW
        const int kh = TU_MM(bar ? 7.5f : 9);
        if (bar) {
            x = cx0;
            w = cx1 - cx0;
        } else {
            tu_row_x(y, kh, &x, &w);
        }
        const int kw = w > TU_MM(50) ? TU_MM(50) : w;
        const bool inv = wi->flags & TC_WF_INVERT;
        const bool low = tc_role_pullup(wi->role) ? !inv : inv;
        tu_key(T_.he ? x + w - kw : x, y, kw, kh, T(low ? T_INV_YES : T_INV_NO), TU_K_GHOST, TU_ID(TU_H_SUB, 4),
               !s_save.on);
        y += kh + TU_MM(1.2f);
    }
    y = tu_para(T_.f_small, tip, cx0, cx1 - cx0, y, T_.by1 + 2000, TU_DIM);
    const int end = bar && by + bh > y ? by + bh : y;  // (the bar: the box beside the keys can be the longer one)
    s_tu_content = (float)(end + (int)tu_scroll() - T_.by0);
}

static void draw_module(void)
{
    const tcm_comp_t *c = cur();
    if (!c || !c->has_info || s_mod < 0 || s_mod >= c->info.nwire) {
        go(S_COMP);
        return;
    }
    const tc_wire_t *wi = &c->info.wire[s_mod];
    const bool rx = tc_role_rx(wi->role);
    const int cls = tc_role_class(wi->role);
    tu_begin(2, s_now);
    if (cls == TC_C_OUT || cls == TC_C_IN || cls == TC_C_ADC) {
        draw_switch(c, wi, cls);
    } else if (rx) {
        const int wh = (T_.by1 - T_.by0) * 55 / 100;
        int x, w;
        tu_row_x(T_.by0 + TU_MM(1), wh, &x, &w);
        wave_draw(c, x, T_.by0 + TU_MM(1), w, wh);
        char line[64];
        const bool live = c->watch_pin == wi->pin && c->watch_ms;
        if (live) {
            snprintf(line, sizeof line, "%s  %lu     %s", T(T_EDGES), (unsigned long)c->watch_edges,
                     T(c->watch_level ? T_ACTIVE : T_IDLE));
        }
        int y = T_.by0 + wh + TU_MM(2.5f);
        if (live) {
            tu_text_c(T_.f_key, (T_.bx0 + T_.bx1) / 2, T_.bx1 - T_.bx0, y + T_.f_key->asc, line, TU_ACC);
            y += T_.f_key->line + TU_MM(1);
        }
        const bool stuck = live && c->watch_level && c->watch_edges == 0;
        tu_para(T_.f_small, T(stuck ? T_STUCK : T_LIVE_TIP), T_.bx0, T_.bx1 - T_.bx0, y, T_.by1,
                stuck ? TU_WARN : TU_DIM);
    } else {
        const int bh = TU_MM(13);
        int x, w;
        const int y = T_.by0 + TU_MM(2);
        tu_row_x(y, bh, &x, &w);
        const int kw = w > TU_MM(60) ? TU_MM(60) : w;
        tu_key(x + (w - kw) / 2, y, kw, bh, T(T_PULSE), TU_K_HOT, TU_ID(TU_H_SUB, 0), true);
        tu_para(T_.f_small, T(T_PULSE_TIP), T_.bx0, T_.bx1 - T_.bx0, y + bh + TU_MM(2), T_.by1, TU_DIM);
    }
    char title[48];
    snprintf(title, sizeof title, "%s   GPIO %d", role_name(wi->role), wi->pin);
    tu_chrome(title, true, NULL, 0, s_save.on ? T(T_SAVING) : NULL, TU_WARN);
    tu_act(0, T(T_CHANGE_PIN), TU_K_NORMAL);
    tu_act(1, T(T_REMOVE), TU_K_BAD);
}

static void draw_role(void)
{
    tu_begin(0, s_now);
    tu_grid_t g = tu_grid(TC_R_N - 1, 24, 9, 16, T_.by0);
    for (int k = 0; k < TC_R_N - 1; k++) {
        const int r = ROLE_ORDER[k];
        int x, y, w, h;
        if (tu_cell(&g, k, &x, &y, &w, &h)) {
            tu_key(x, y, w, h, role_name(r), role_style(r), TU_ID(TU_H_ITEM, r), true);
        }
    }
    tu_chrome(T(T_WHAT_ROLE), true, NULL, 0, NULL, 0);
}

static void draw_pin(void)
{
    tu_begin(0, s_now);
    const tcm_comp_t *c = cur();
    int pins[TC_GPIO_MAX], np = 0;
    for (int gp = 0; gp < c->info.ngpio; gp++) {
        if (!(c->info.pins[gp] & TC_P_OK)) {
            continue;
        }
        bool used = false;
        for (int k = 0; k < c->info.nwire; k++) {
            used = used || (c->info.wire[k].pin == gp && k != s_change);
        }
        const bool out = tc_role_tx(s_role) || tc_role_class(s_role) == TC_C_OUT;
        const bool adc_bad = s_role == TC_R_ANALOG && !tc_pin_adc(c->chip, gp);
        if (!used && !((c->info.pins[gp] & TC_P_IN_ONLY) && out) && !adc_bad) {
            pins[np++] = gp;
        }
    }
    const int note = tu_para(T_.f_tiny, T(tc_role_pullup(s_role) ? T_PULL_TIP : T_STRAP), T_.bx0, T_.bx1 - T_.bx0,
                             T_.by0 - (int)tu_scroll(), T_.by1, TU_WARN);
    tu_grid_t g = tu_grid(np, 9, 8, 11, note + TU_MM(1) + (int)tu_scroll());
    for (int k = 0; k < np; k++) {
        int x, y, w, h;
        if (!tu_cell(&g, k, &x, &y, &w, &h)) {
            continue;
        }
        char t[8];
        snprintf(t, sizeof t, "%d", pins[k]);
        const bool led_ok = tc_role_class(s_role) == TC_C_OUT && (c->info.pins[pins[k]] & TC_P_LED);
        const bool no_pull = tc_role_pullup(s_role) && (c->info.pins[pins[k]] & TC_P_IN_ONLY);
        const bool warn = (!led_ok && (c->info.pins[pins[k]] & (TC_P_STRAP | TC_P_LED | TC_P_WARN))) || no_pull;
        tu_key(x, y, w, h, t, warn ? TU_K_WARN : TU_K_NORMAL, TU_ID(TU_H_ITEM, pins[k]), true);
    }
    tu_chrome(role_name(s_role), true, NULL, 0, s_save.on ? T(T_SAVING) : NULL, TU_WARN);
}

static void render(int64_t now)
{
    if (s_typing) {
        kbd_render(now);
        return;
    }
    s_now = esp_timer_get_time() / 1000;
    switch (s_scr) {
    case S_LIST:
        draw_list();
        break;
    case S_ADD:
        draw_add();
        break;
    case S_PAIR:
        draw_pair();
        break;
    case S_COMP:
        draw_comp();
        break;
    case S_MODULE:
        draw_module();
        break;
    case S_ROLE:
        draw_role();
        break;
    case S_PIN:
        draw_pin();
        break;
    case S_UPDATE:
        draw_update();
        break;
    default:
        break;
    }
    tu_menu_draw();
    tu_end();
}

// ---------------------------------------------------------------- the input
static void back(void)
{
    switch (s_scr) {
    case S_LIST:
        if (s_from_remote) {
            const app_t *a = app_find("CONTROL");
            if (a) {
                app_open(a);
            }
        }
        break;
    case S_ADD:
    case S_COMP:
        go(S_LIST);
        break;
    case S_PAIR:
        tcm_pair_cancel();
        go(S_ADD);
        break;
    case S_MODULE:
    case S_ROLE:
        go(S_COMP);
        break;
    case S_PIN:
        go(s_change >= 0 ? S_MODULE : S_ROLE);
        break;
    case S_UPDATE:
        up_leave();
        break;
    default:
        break;
    }
}

static void comp_menu(int ci)
{
    s_ci = ci;
    tu_menu_open(TU_ID(TU_H_ITEM, ci), tcm_get(ci)->name);
    tu_menu_add(T(T_RENAME), TU_K_NORMAL);
    tu_menu_add(T(T_FORGET), TU_K_BAD);
    // (the same version again, or another one: a repair of a companion)
    static char radio[40];
    int fw = 0;
    const tcm_comp_t *c = tcm_get(ci);
    if (best_fw(c, &fw) && c->has_info && c->info.fw >= 3) {
        snprintf(radio, sizeof radio, T(T_FW_RADIO), fw);
        tu_menu_add(radio, TU_K_NORMAL);
    }
}

static void on_tap(int id)
{
    const int k = TU_KIND(id), i = TU_IDX(id);
    if (k == TU_H_BACK) {
        back();
        return;
    }
    switch (s_scr) {
    case S_LIST:
        if (k == TU_H_ITEM && i == 900) {
            go(S_ADD);
        } else if (k == TU_H_ITEM && i < tcm_count()) {
            s_ci = i;
            tcm_info(i);
            img_scan();
            go(S_COMP);
        }
        break;
    case S_ADD:
        if (k == TU_H_ITEM) {
            tcm_found_t f[TCM_FOUND_MAX];
            if (i < tcm_found(f, TCM_FOUND_MAX)) {
                tcm_pair_start(f[i].mac, comp_my_name());
                go(S_PAIR);
            }
        }
        break;
    case S_PAIR:
        if (k == TU_H_ACT) {
            go(S_ADD);
        }
        break;
    case S_COMP:
        if (k == TU_H_TOPKEY || k == TU_H_ACT) {
            comp_menu(s_ci);
        } else if (k == TU_H_ITEM && i == 901) {
            up_start();
        } else if (k == TU_H_ITEM && i == 900) {
            s_change = -1;
            go(S_ROLE);
        } else if (k == TU_H_ITEM) {
            s_mod = i;
            const tc_wire_t *w = &cur()->info.wire[i];
            if (tc_role_rx(w->role)) {
                tcm_watch(s_ci, w->pin, true);
            }
            go(S_MODULE);
        }
        break;
    case S_MODULE:
        if (k == TU_H_SUB && i >= 1 && i <= 3) {
            // a switch output: ON, OFF, PRESS (0.5 s)
            tcm_set(s_ci, cur()->info.wire[s_mod].pin, i != 2, i == 3 ? 500 : 0);
        } else if (k == TU_H_SUB && i == 4 && !s_save.on) {
            tc_wire_t w[TC_WIRE_MAX];
            const int n = cur()->info.nwire;
            memcpy(w, cur()->info.wire, sizeof w);
            w[s_mod].flags ^= TC_WF_INVERT;
            wiring_send(w, n, S_MODULE);
        } else if (k == TU_H_SUB) {
            tcm_pulse(s_ci, cur()->info.wire[s_mod].pin, 300);
            tu_toast(T(T_SENT), TU_HOT);
        } else if (k == TU_H_ACT && i == 0) {
            s_change = s_mod;
            s_role = cur()->info.wire[s_mod].role;
            go(S_PIN);
        } else if (k == TU_H_ACT && i == 1) {
            char q[64];
            snprintf(q, sizeof q, "%s   GPIO %d", role_name(cur()->info.wire[s_mod].role), cur()->info.wire[s_mod].pin);
            sysui_ask(q, T(T_REMOVE_Q), T(T_REMOVE), T(T_CANCEL), 0, on_remove);
        }
        break;
    case S_ROLE:
        if (k == TU_H_ITEM) {
            s_role = i;
            go(S_PIN);
        }
        break;
    case S_PIN:
        if (k == TU_H_ITEM && !s_save.on) {
            module_add(i);
        }
        break;
    case S_UPDATE:
        if (k == TU_H_ACT) {
            up_leave();
        }
        break;
    default:
        break;
    }
}

static void on_menu(int owner, int row)
{
    const int ci = TU_IDX(owner);
    if (ci < 0 || ci >= tcm_count()) {
        return;
    }
    if (row == 0) {
        rename_open(ci);
    } else if (row == 1) {
        ask_forget(ci);
    } else {
        s_ci = ci;
        up_start();
    }
}

static void input(const input_event_t *ev)
{
    if (s_typing) {
        const int r = kbd_input(ev);
        if (r == KBD_OK || r == KBD_CANCEL) {
            s_typing = false;
            if (r == KBD_OK && s_kbuf[0] && s_ci >= 0) {
                tcm_rename(s_ci, s_kbuf);
            }
            if (s_scr == S_PAIR) {
                go(S_COMP);  // (a new companion: its modules next)
            }
        }
        return;
    }
    const tu_ev_t e = tu_input(ev);
    switch (e.kind) {
    case TU_E_TAP:
        on_tap(e.id);
        break;
    case TU_E_LONG:
        if (s_scr == S_LIST && TU_KIND(e.id) == TU_H_ITEM && TU_IDX(e.id) < tcm_count()) {
            comp_menu(TU_IDX(e.id));
        }
        break;
    case TU_E_MENU:
        on_menu(e.owner, e.id);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- enter / leave / update
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_VALS[] = {"OFF", "כבוי", "ON", "פעיל"};
// NEARBY ALERTS: the notice and the sound when a companion comes near (the service of TFN OS reads the key)
static const pitem_t ITEMS[] = {
        {PI_PICK, 0, 2, 0, {"LANGUAGE", "שפה"}, LANG_VALS, "comps_lang"},
        {PI_PICK, 1, 2, 1, {"NEARBY ALERTS", "התראת קרבה"}, ONOFF_VALS, COMP_ALERTS_KEY},
};
static void item_set(int id, int v)
{
    if (id == 0) {
        s_he = v == 1;
        tu_lang(s_he);
    }
}
static const pitems_t PANEL = {ITEMS, 2, NULL, NULL, item_set, NULL, NULL};

static void enter(int64_t now)
{
    (void)now;
    s_now = esp_timer_get_time() / 1000;
    s_he = store_get("comps_lang", 0) == 1;
    tu_init(s_he);
    comp_open();
    img_scan();
    s_typing = false;
    s_save.on = false;
    s_ci = -1;
    const char *arg = app_launch_arg();
    s_from_remote = arg && !strncmp(arg, "control", 7);
    s_scr = S_LIST;
    if (arg && strstr(arg, "add")) {
        go(S_ADD);
    }
    sysui_panel_items(&PANEL);
}

static void leave(void)
{
    if (s_scr == S_MODULE && s_ci >= 0) {
        tcm_watch(s_ci, 0, false);
        comp_poll();
    }
    up_free();
    comp_close();
}

// THE SOUNDS of the live test and of the pairing (the boards do not vibrate)
static void sounds_tick(void)
{
    static uint32_t edges_was;
    static int64_t tick_t;
    static int pair_was;
    // the live signal of a receiver: a short tick for each burst of edges (as a Geiger counter), so the ear finds a
    // loose wire while the eyes are on the breadboard
    if (s_scr == S_MODULE && s_ci >= 0 && s_ci < tcm_count() && tcm_get(s_ci)->watch_pin >= 0) {
        const uint32_t e = tcm_get(s_ci)->watch_edges;
        if (e > edges_was && s_now - tick_t > 70) {
            sfx_tone(2093, 2093, 14, W_SQUARE, 7);
            tick_t = s_now;
        }
        edges_was = e;
    } else {
        edges_was = 0;
    }
    // a switch input or output: a click at each change of its value (a high one for active, a low one for not)
    static int val_was = -1;
    int v = -1;
    if (s_scr == S_MODULE && s_ci >= 0 && s_ci < tcm_count() && s_mod >= 0 && s_mod < cur()->info.nwire &&
        tc_role_class(cur()->info.wire[s_mod].role) != TC_C_ADC &&
        tcm_value(s_ci, cur()->info.wire[s_mod].pin, &v, NULL)) {
        if (val_was >= 0 && v != val_was) {
            sfx_tone(v ? 1568 : 784, v ? 1568 : 784, 25, W_SQUARE, 8);
        }
        val_was = v;
    } else {
        val_was = -1;
    }
    // the pairing: the companion waits for its BOOT key = one note; paired = the rising notes; no answer = low
    const int st = s_scr == S_PAIR ? tcm_pair_state() : 0;
    if (st != pair_was) {
        if (st == 3) {
            audio_tune(TUNE_WAIT, 1);
        } else if (st == 2) {
            audio_tune(TUNE_PAIRED, 3);
        } else if (st == -1) {
            audio_tune(TUNE_FAIL, 1);
        }
        pair_was = st;
    }
}

static void update(int64_t now)
{
    (void)now;
    s_now = esp_timer_get_time() / 1000;
    comp_poll();
    tu_update(s_now);
    wiring_tick();
    sounds_tick();
    up_tick();
    if (s_scr == S_PAIR && tcm_pair_state() == 2 && !s_typing) {
        // a new companion: its name (the default: COMPANION and the end of its MAC), then its modules
        s_ci = tcm_pair_index();
        tcm_pair_cancel();
        char t4[8];
        mac_tail(tcm_get(s_ci)->mac, t4, sizeof t4);
        snprintf(s_kbuf, sizeof s_kbuf, "%s %s", s_he ? "מלווה" : "COMPANION", t4);
        tcm_rename(s_ci, s_kbuf);
        kbd_open(T(T_NAME), s_kbuf, sizeof s_kbuf, false);
        kbd_set_hebrew(s_he);
        s_typing = true;
        s_scr = S_PAIR;
    }
}

static bool awake(void)
{
    return s_scr == S_ADD || s_scr == S_PAIR || s_scr == S_MODULE || s_scr == S_UPDATE;
}

// the menu icon: a small board with an antenna and its waves
static void icon(int x, int y, int s)
{
    gfx_fill_rect(x, y, s, s, rgb(16, 22, 36));
    const int bw = s * 46 / 100, bh = s * 34 / 100, bx = x + s * 27 / 100, by = y + s * 50 / 100;
    ui_rbox(bx, by, bw, bh, s / 16 + 1, rgb(28, 92, 80), rgb(80, 210, 176), s > 60 ? 2 : 1);
    ui_rrect(bx + bw * 30 / 100, by + bh * 30 / 100, bw * 40 / 100, bh * 40 / 100, 2, rgb(18, 24, 36));
    for (int k = 0; k < 4; k++) {
        gfx_fill_rect(bx + bw * (12 + 25 * k) / 100, by + bh, s / 30 + 1, s / 14 + 1, rgb(200, 210, 220));
    }
    const int ax = bx + bw * 80 / 100, ay = by;
    gfx_fill_rect(ax, ay - s * 22 / 100, s / 28 + 1, s * 22 / 100, rgb(200, 210, 220));
    const float t = icon_time();
    for (int k = 0; k < 3; k++) {
        float f = t * 0.8f + (float)k / 3.0f;
        f -= (float)(int)f;
        ui_ring(ax, ay - s * 22 / 100, (int)((float)s * (0.06f + 0.3f * f)), s > 60 ? 3 : 2, rgb(80, 210, 176),
                (int)(255 * (1 - f)));
    }
}

const app_t app_companions = {
        .name = "COMPANIONS",
        .landscape = true,
        .round = APP_ROUND_HI,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
};
