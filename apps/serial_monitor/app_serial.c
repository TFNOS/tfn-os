// SERIAL MONITOR: a serial terminal for a device on the pins of the board (an Arduino, an ESP board, a GPS module, a
// USB-UART adapter).
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_serial at the end of this file).
// The memory: the file statics; the rings s_ln and s_hx in PSRAM (the first enter makes them; they stay while the
// app file is loaded); the UART driver of ser_port.c (open to close).
//
// The baud and the format (data bits, parity, stop bits), the common settings, AUTO
// (it finds the baud of a device that sends text), TEXT or HEX, the times of the lines, SEND (text or hex, the line
// end), a SELF TEST of the UART, a DEMO device. The app keeps the last settings.
// THE LAYERS (each one knows only the layer under it):
//   libs/rfpulse   rfp_board_uart(): the pins of each board kind (RX, TX, GND); rfp_sketch.inc: the wiring sketch
//   ser_port.c     the UART of the board (the ESP-IDF driver, ABI rev 14) and the demo device (the bits of a line)
//   this app       the terminal (the lines and the raw bytes), the settings, the screens (ser_draw.inc)
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfdraw/tfdraw.h"
#include "rfpulse/rfp_board.h"
#include "ser_port.h"

static bool s_he;  // the language: false English, true Hebrew
#include "ser_text.inc"
#include "ser_fonts.inc"

#define BAR BOARD_BAR_SCREEN
static int64_t s_now;     // ms
static int64_t s_open_t;  // ms: the open of the port (the times of the lines start here)
static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000;
}

// ================================================================ the settings (saved)
static ser_cfg_t s_cfg = {115200, 8, 'N', 10, false};
static int s_end = 1;  // the line end of SEND: 0 none, 1 LF, 2 CR, 3 CR+LF
static int s_view;     // 0 TEXT, 1 HEX
static bool s_times, s_echo = true;
static int s_src = SER_PINS;
static bool s_send_hex;

static const int BAUDS[] = {300,   1200,  2400,  4800,   9600,   14400,  19200,  31250,
                            38400, 57600, 74880, 115200, 230400, 250000, 460800, 921600};
#define NBAUD ((int)(sizeof BAUDS / sizeof BAUDS[0]))
typedef struct {
    const char *name;
    int baud;
    uint8_t bits;
    char parity;
    uint8_t stop;
} preset_t;
static const preset_t PRESETS[] = {
        {"ARDUINO", 9600, 8, 'N', 10},  {"ESP32", 115200, 8, 'N', 10},        {"ESP8266 BOOT", 74880, 8, 'N', 10},
        {"GPS NMEA", 9600, 8, 'N', 10}, {"MODBUS RTU", 9600, 8, 'E', 10},     {"MIDI", 31250, 8, 'N', 10},
        {"DMX", 250000, 8, 'N', 20},    {"RASPBERRY PI", 115200, 8, 'N', 10},
};
#define NPRESET ((int)(sizeof PRESETS / sizeof PRESETS[0]))

static void fmt_name(char *o, int n, const ser_cfg_t *c)
{
    snprintf(o, (size_t)n, "%d%c%s", c->bits, c->parity, c->stop == 15 ? "1.5" : c->stop == 20 ? "2" : "1");
}

static void save(void)
{
    store_set("ser_baud", (uint32_t)s_cfg.baud);
    store_set("ser_fmt", (uint32_t)(s_cfg.bits |
                                    (s_cfg.parity == 'E'   ? 1
                                     : s_cfg.parity == 'O' ? 2
                                                           : 0)
                                            << 4 |
                                    (s_cfg.stop == 15   ? 1
                                     : s_cfg.stop == 20 ? 2
                                                        : 0)
                                            << 6 |
                                    (s_cfg.inv ? 1 : 0) << 8));
    store_set("ser_end", (uint32_t)s_end);
    store_set("ser_view", (uint32_t)(s_view | (s_times ? 2 : 0) | (s_echo ? 0 : 4) | (s_send_hex ? 8 : 0)));
}

static void load(void)
{
    const uint32_t b = store_get("ser_baud", 115200);
    s_cfg.baud = b >= 50 && b <= 5000000 ? (int)b : 115200;
    const uint32_t f = store_get("ser_fmt", 8);
    s_cfg.bits = (uint8_t)((f & 15) >= 5 && (f & 15) <= 8 ? (f & 15) : 8);
    s_cfg.parity = ((f >> 4) & 3) == 1 ? 'E' : ((f >> 4) & 3) == 2 ? 'O' : 'N';
    s_cfg.stop = ((f >> 6) & 3) == 1 ? 15 : ((f >> 6) & 3) == 2 ? 20 : 10;
    s_cfg.inv = (f >> 8) & 1;
    s_end = (int)(store_get("ser_end", 1) & 3);
    const uint32_t v = store_get("ser_view", 0);
    s_view = v & 1;
    s_times = v & 2;
    s_echo = !(v & 4);
    s_send_hex = v & 8;
}

// ================================================================ the terminal
// The lines (TEXT): a ring of LN lines of at most LW bytes (a longer line goes on in the next one). The raw bytes
// (HEX): a ring of HX bytes. Both only grow while the app is open.
#define LN 600
#define LW 120
enum {
    D_RX,
    D_TX,
    D_NOTE
};
typedef struct {
    uint8_t dir, len;
    bool open;  // the line has no end yet (more bytes can come)
    bool cont;  // it goes on from the line before (a long line)
    int32_t t;  // ms after the open
    char s[LW];
} line_t;
static line_t *s_ln;    // [LN] (PSRAM)
static uint32_t s_nln;  // all lines since the clear (the newest = s_nln - 1)
static bool s_cr;       // the last byte was a CR (a CR LF is one end)
#define HX 16384
static uint8_t *s_hx;  // [HX] (PSRAM)
static uint32_t s_nhx;
static bool s_paused;                    // PAUSE: the bytes still come into the rings; the view stays
static uint32_t s_pause_ln, s_pause_hx;  // the counts at the pause
static float s_back;                     // the view: the rows over the end (0 = the newest rows)
static bool s_follow = true;
static int s_rows_prev;
static uint32_t s_rate_b, s_rate_b0;
static int64_t s_rate_t;
static float s_rate;  // bytes a second

static line_t *ln_at(uint32_t i)
{
    return &s_ln[i % LN];
}

static line_t *ln_new(int dir)
{
    line_t *l = ln_at(s_nln++);
    memset(l, 0, offsetof(line_t, s));
    l->dir = (uint8_t)dir;
    l->open = dir == D_RX;
    l->t = (int32_t)(s_now - s_open_t);
    return l;
}

// the open line of the device (a new one if the last line is closed or not of the device)
static line_t *rx_line(void)
{
    if (s_nln) {
        line_t *l = ln_at(s_nln - 1);
        if (l->dir == D_RX && l->open) {
            return l;
        }
    }
    return ln_new(D_RX);
}

static void rx_end(void)
{
    if (s_nln) {
        line_t *l = ln_at(s_nln - 1);
        if (l->dir == D_RX) {
            l->open = false;
        }
    }
}

static void rx_bytes(const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        s_hx[s_nhx++ % HX] = b[i];
        const uint8_t c = b[i];
        if (c == '\n') {
            if (!s_cr) {
                if (!s_nln || !ln_at(s_nln - 1)->open || ln_at(s_nln - 1)->dir != D_RX) {
                    ln_new(D_RX);
                }
                rx_end();
            }
            s_cr = false;
            continue;
        }
        if (c == '\r') {
            if (!s_nln || !ln_at(s_nln - 1)->open || ln_at(s_nln - 1)->dir != D_RX) {
                ln_new(D_RX);
            }
            rx_end();
            s_cr = true;
            continue;
        }
        s_cr = false;
        line_t *l = rx_line();
        if (l->len >= LW) {
            l->open = false;
            l = ln_new(D_RX);
            l->cont = true;
        }
        l->s[l->len++] = (char)c;
    }
    s_rate_b += (uint32_t)n;
}

static void note(const char *s)
{
    rx_end();
    line_t *l = ln_new(D_NOTE);
    l->len = (uint8_t)snprintf(l->s, LW, "%s", s);
    if (l->len >= LW) {
        l->len = LW - 1;
    }
}

static void tx_line(const char *s)
{
    if (!s_echo) {
        return;
    }
    rx_end();
    line_t *l = ln_new(D_TX);
    l->len = (uint8_t)snprintf(l->s, LW, "%s", s);
    if (l->len >= LW) {
        l->len = LW - 1;
    }
}

static void clear_all(void)
{
    s_nln = 0;
    s_nhx = 0;
    s_cr = false;
    s_back = 0;
    s_follow = true;
    s_rows_prev = 0;
    s_paused = false;
}

// ================================================================ the port
static int s_err;
static ser_stats_t s_st;
static void toast(const char *s);

static void port_open(void)
{
    s_open_t = s_now;
    s_err = ser_open(s_src, &s_cfg);
    if (s_err != SER_OK && s_src == SER_PINS) {
        note(ser_err(s_err));
        return;
    }
    if (s_src == SER_DEMO) {
        note(TX(T_DEMO_NOTE));
    }
}

static void apply_cfg(void)
{
    save();
    if (ser_is_open()) {
        ser_config(&s_cfg);
    }
}

static void set_src(int src)
{
    s_src = src == SER_DEMO || !ser_has_pins() ? SER_DEMO : SER_PINS;
    store_set("ser_src", (uint32_t)s_src);
    clear_all();
    port_open();
}

// ---------------------------------------------------------------- AUTO: the baud of a device that sends text
// Each baud of the list until 24 bytes came, or 2.5 s (the first 60 ms go: a byte that the change cut). A device can
// be quiet for a second or more between its lines: a short window at the right baud can see no byte. No byte at all
// at the first baud in 2.5 s = NO DATA (a quiet line gives no bytes at any baud). The score: the share of the bytes
// that are text (letters, digits, signs, CR, LF, TAB), less 3 for each frame error. The best score over 0.85 wins.
static const int AUTO_LIST[] = {115200, 9600, 57600, 38400, 19200,  230400, 74880, 460800,
                                921600, 4800, 2400,  1200,  250000, 31250,  14400, 300};
#define NAUTO ((int)(sizeof AUTO_LIST / sizeof AUTO_LIST[0]))
static int s_auto = -1;  // the index of the baud that AUTO tries now (-1 = no AUTO)
static int64_t s_auto_t;
static int s_auto_good, s_auto_all, s_auto_best = -1;
static uint32_t s_auto_fe;
static float s_auto_score, s_auto_best_score;
static ser_cfg_t s_auto_old;

static void auto_try(int i)
{
    s_auto = i;
    s_auto_t = s_now;
    s_auto_good = s_auto_all = 0;
    ser_cfg_t c = s_cfg;
    c.baud = AUTO_LIST[i];
    c.bits = 8;
    c.parity = 'N';
    c.stop = 10;
    ser_config(&c);
    ser_stats(&s_st);
    s_auto_fe = s_st.frame;
}

static void auto_start(void)
{
    if (!ser_is_open()) {
        return;
    }
    s_auto_old = s_cfg;
    s_auto_best = -1;
    s_auto_best_score = 0;
    auto_try(0);
}

static void auto_bytes(const uint8_t *b, int n)
{
    if (s_now - s_auto_t < 60) {
        return;
    }
    for (int i = 0; i < n; i++) {
        const uint8_t c = b[i];
        s_auto_good += (c >= 32 && c < 127) || c == '\r' || c == '\n' || c == '\t';
        s_auto_all++;
    }
}

static void auto_step(void)
{
    if (s_auto < 0 || (s_now - s_auto_t < 2500 && s_auto_all < 24)) {
        return;
    }
    ser_stats(&s_st);
    if (s_auto == 0 && s_auto_all == 0) {  // (no byte in 2.5 s: nothing sends on the line)
        s_cfg = s_auto_old;
        s_auto = -1;
        apply_cfg();
        note(TX(T_NODATA));
        toast(TX(T_NODATA));
        return;
    }
    const uint32_t fe = s_st.frame - s_auto_fe;
    s_auto_score = s_auto_all >= 8 ? ((float)s_auto_good - 3.0f * (float)fe) / (float)s_auto_all : 0;
    if (s_auto_score > s_auto_best_score) {
        s_auto_best_score = s_auto_score;
        s_auto_best = s_auto;
    }
    char b[96];
    if (s_auto_score > 0.97f || s_auto + 1 >= NAUTO) {  // (a clean text at once, or the end of the list)
        if (s_auto_best >= 0 && s_auto_best_score > 0.85f) {
            s_cfg.baud = AUTO_LIST[s_auto_best];
            s_cfg.bits = 8;
            s_cfg.parity = 'N';
            s_cfg.stop = 10;
            snprintf(b, sizeof b, TX(T_FOUND), s_cfg.baud);
            sfx_ui(SFX_CLICK);
        } else {
            s_cfg = s_auto_old;
            snprintf(b, sizeof b, "%s", TX(s_auto_best >= 0 || s_auto_all ? T_NOTFOUND : T_NODATA));
        }
        s_auto = -1;
        apply_cfg();
        clear_all();
        note(b);
        toast(b);
        return;
    }
    auto_try(s_auto + 1);
}

// ---------------------------------------------------------------- SEND
#define HISTN 6
static char s_hist[HISTN][96];
static int s_nhist;
static char s_line[96];

// "01 0a FF" -> bytes; -1 = not hex
static int parse_hex(const char *s, uint8_t *o, int max)
{
    int n = 0, half = -1;
    for (; *s; s++) {
        int v;
        if (*s >= '0' && *s <= '9') {
            v = *s - '0';
        } else if (*s >= 'a' && *s <= 'f') {
            v = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'F') {
            v = *s - 'A' + 10;
        } else if (*s == ' ' || *s == ',' || *s == ':') {
            if (half >= 0) {
                return -1;  // (one digit alone)
            }
            continue;
        } else if (*s == 'x' || *s == 'X') {  // (0x41)
            if (half == 0) {
                half = -1;
                continue;
            }
            return -1;
        } else {
            return -1;
        }
        if (half < 0) {
            half = v;
        } else {
            if (n >= max) {
                return -1;
            }
            o[n++] = (uint8_t)(half << 4 | v);
            half = -1;
        }
    }
    return half >= 0 ? -1 : n;
}

static void send_text(const char *s)
{
    if (!ser_is_open()) {
        toast(TX(T_NOTOPEN));
        return;
    }
    uint8_t b[110];
    int n;
    char shown[LW];
    if (s_send_hex) {
        n = parse_hex(s, b, sizeof b);
        if (n < 0) {
            toast(TX(T_BADHEX));
            return;
        }
        int k = 0;
        for (int i = 0; i < n && k < LW - 4; i++) {
            k += snprintf(shown + k, (size_t)(LW - k), "%02X ", b[i]);
        }
        shown[k] = 0;
    } else {
        n = (int)strlen(s);
        if (n > 96) {
            n = 96;
        }
        memcpy(b, s, (size_t)n);
        if (s_end == 2 || s_end == 3) {
            b[n++] = '\r';
        }
        if (s_end == 1 || s_end == 3) {
            b[n++] = '\n';
        }
        snprintf(shown, sizeof shown, "%s", s);
    }
    ser_write(b, n);
    tx_line(shown);
    // the history: the newest first, no copy of the same line
    int at = -1;
    for (int i = 0; i < s_nhist; i++) {
        if (!strcmp(s_hist[i], s)) {
            at = i;
        }
    }
    if (at < 0) {
        at = s_nhist < HISTN ? s_nhist++ : HISTN - 1;
    }
    memmove(s_hist[1], s_hist[0], sizeof s_hist[0] * (size_t)at);
    snprintf(s_hist[0], sizeof s_hist[0], "%s", s);
    s_follow = true;
    s_back = 0;
}

// ================================================================ the screens
enum {
    SC_WIRE,
    SC_TERM,
    SC_SET,
    SC_SEND,
    SC_KBD
};
static int s_scr;
static int s_kbd_for;  // 0 = the line of SEND, 1 = a baud
static char s_kbuf[100];
static int64_t s_toast_t;
static char s_toast[96];
static void toast(const char *s)
{
    snprintf(s_toast, sizeof s_toast, "%s", s);
    s_toast_t = s_now;
}

static void kbd_go(int what)
{
    s_kbd_for = what;
    if (what == 0) {
        snprintf(s_kbuf, sizeof s_kbuf, "%s", s_line);
    } else {
        snprintf(s_kbuf, sizeof s_kbuf, "%d", s_cfg.baud);
    }
    s_scr = SC_KBD;
    kbd_open(what == 0 ? TXE(T_KBD_LINE) : TXE(T_KBD_BAUD), s_kbuf, what == 0 ? 96 : 9, false);
    kbd_set_ok_question(NULL);
}

static void kbd_done(bool ok)
{
    s_scr = s_kbd_for == 0 ? SC_SEND : SC_SET;
    tfd_screen();
    if (!ok) {
        return;
    }
    if (s_kbd_for == 0) {
        strlcpy(s_line, s_kbuf, sizeof s_line);
        return;
    }
    const int b = atoi(s_kbuf);
    if (b < 50 || b > 5000000) {
        toast(TX(T_BADBAUD));
        return;
    }
    s_cfg.baud = b;
    apply_cfg();
}

static void self_test(void)
{
    if (s_src != SER_PINS || !ser_is_open()) {
        return;
    }
    char m[96];
    const int r = ser_selftest(m, sizeof m);
    note(TX(r == 1 ? T_ST_OK : T_ST_FAIL));  // (the text of the result alone: a Hebrew line with Latin parts
    note(m);                                 //  can change its order; the bytes on their own line)
    toast(TX(r == 1 ? T_ST_OK : T_ST_FAIL));
    s_scr = SC_TERM;
}

// ================================================================ the settings panel
enum {
    PI_LANG,
    PI_SRC,
    PI_VIEW,
    PI_SET,
    PI_WIRE,
    PI_TEST,
    PI_CLEAR
};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const SRC_VALS[] = {"THE PINS", "הפינים", "DEMO", "הדגמה"};
static const char *const VIEW_VALS[] = {"TEXT", "טקסט", "HEX", "HEX"};
static pitem_t s_items[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, "ser_lang"},
        {PI_PICK, PI_SRC, 2, 0, {"SOURCE", "מקור"}, SRC_VALS, NULL},
        {PI_PICK, PI_VIEW, 2, 0, {"VIEW", "תצוגה"}, VIEW_VALS, NULL},
        {PI_ACT, PI_SET, 0, 0, {"SETTINGS", "הגדרות"}, NULL, NULL},
        {PI_ACT, PI_WIRE, 0, 0, {"THE WIRING", "החיווט"}, NULL, NULL},
        {PI_ACT, PI_TEST, 0, 0, {"SELF TEST", "בדיקה עצמית"}, NULL, NULL},
        {PI_ACT2, PI_CLEAR, 0, 0, {"CLEAR THE SCREEN", "ניקוי המסך"}, NULL, NULL},
};
#define NITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))

static bool item_shown(int id)
{
    if (id == PI_SRC || id == PI_WIRE) {
        return ser_has_pins();
    }
    if (id == PI_TEST) {
        return ser_has_pins() && s_src == SER_PINS;
    }
    return true;
}
static int item_get(int id)
{
    return id == PI_SRC ? s_src == SER_DEMO : id == PI_VIEW ? s_view : 0;
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_LANG:
        s_he = v == 1;
        break;
    case PI_SRC:
        set_src(v ? SER_DEMO : SER_PINS);
        s_scr = SC_TERM;
        break;
    case PI_VIEW:
        s_view = v & 1;
        s_back = 0;
        s_follow = true;
        save();
        break;
    case PI_SET:
        s_scr = SC_SET;
        break;
    case PI_WIRE:
        if (s_src != SER_PINS) {
            set_src(SER_PINS);
        }
        s_scr = SC_WIRE;
        break;
    case PI_TEST:
        self_test();
        break;
    case PI_CLEAR:
        clear_all();
        break;
    }
}
static bool s_down, s_drag;
static void panel_opened(void)
{
    s_down = s_drag = false;
}
static const pitems_t PANEL = {s_items, NITEMS, item_shown, item_get, item_set, panel_opened, panel_opened};

#include "ser_draw.inc"

// ================================================================ the touches
static int s_dy0;
static float s_scroll0;

static void input(const input_event_t *ev)
{
    if (s_scr == SC_KBD) {
        const int r = kbd_input(ev);
        if (r == KBD_OK) {
            kbd_done(true);
        } else if (r == KBD_CANCEL) {
            kbd_done(false);
        }
        return;
    }
    const int x = ev->x, y = ev->y;
    float *scr = s_scr == SC_SET ? &s_set_scroll : &s_back;
    const float sign = s_scr == SC_SET ? -1.0f : 1.0f;  // (the terminal: a drag down shows older rows)
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_drag = false;
        s_dy0 = y;
        s_scroll0 = *scr;
        break;
    case IN_MOVE:
        if (s_down && abs(y - s_dy0) > 8 && y > s_top_h && (s_scr == SC_TERM || s_scr == SC_SET)) {
            s_drag = true;
            input_tap_cancel();
        }
        if (s_drag) {
            const float rows = s_scr == SC_TERM ? (float)(y - s_dy0) / (float)s_lh : (float)(y - s_dy0);
            *scr = s_scroll0 + sign * rows;
            if (*scr < 0) {
                *scr = 0;
            }
            if (s_scr == SC_TERM) {
                s_follow = *scr < 0.5f;
            }
        }
        break;
    case IN_UP:
        s_down = s_drag = false;
        if (s_scr == SC_TERM && s_back < 0.5f) {
            s_back = 0;
            s_follow = true;
        }
        break;
    case IN_TAP: {
        const int h = hit_at(x, y);
        if (h >= 0) {
            hit_do(h);
        }
        break;
    }
    default:
        break;
    }
}

// ================================================================ enter / leave / update
static void *ps(size_t n)
{
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}

static void enter(int64_t now)
{
    (void)now;
    s_now = ms_now();
    s_he = store_get("ser_lang", 1) == 1;  // (Hebrew when nothing is saved)
    load();
    if (!s_ln) {
        s_ln = ps(sizeof(line_t) * LN);
    }
    if (!s_hx) {
        s_hx = ps(HX);
    }
    clear_all();
    s_toast[0] = 0;
    s_auto = -1;
    s_down = s_drag = false;
    s_rate_t = s_now;
    s_rate_b = s_rate_b0 = 0;
    s_rate = 0;
    memset(&s_st, 0, sizeof s_st);
    sysui_panel_items(&PANEL);
    if (!s_ln || !s_hx) {
        s_scr = SC_TERM;
        toast("NO MEMORY");
        return;
    }
    s_src = ser_has_pins() && store_get("ser_src", SER_PINS) == SER_PINS ? SER_PINS : SER_DEMO;
    port_open();
    s_scr = s_src == SER_PINS && store_get("ser_wired", 0) != 1 ? SC_WIRE : SC_TERM;
}

static void leave(void)
{
    ser_close();
    store_set("ser_lang", s_he);
    save();
}

static void update(int64_t now)
{
    (void)now;
    s_now = ms_now();
    if (!ser_is_open() || !s_ln) {
        return;
    }
    static uint8_t b[2048];
    int n, guard = 0;
    while ((n = ser_read(b, sizeof b)) > 0 && guard++ < 8) {
        if (s_auto >= 0) {
            auto_bytes(b, n);
        } else {
            rx_bytes(b, n);
        }
    }
    auto_step();
    ser_stats(&s_st);
    if (s_now - s_rate_t >= 1000) {
        s_rate = (float)(s_rate_b - s_rate_b0) * 1000.0f / (float)(s_now - s_rate_t);
        s_rate_b0 = s_rate_b;
        s_rate_t = s_now;
    }
}

static bool awake(void)
{
    return true;
}  // (a tool that one watches: the screen stays on)
static bool background(void)
{
    return ser_is_open();
}  // (the bytes keep coming into the rings)

const app_t app_serial = {
        .name = "SERIAL MONITOR",
        .landscape = true,
        .round = APP_ROUND_HI,  // (the round boards: the native round frame, the middle box of ser_draw.inc)
        .run = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
        .background = background,
};
