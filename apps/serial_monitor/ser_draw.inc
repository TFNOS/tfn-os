// SERIAL MONITOR: THE SCREENS (they read the state of app_serial.c; the touch targets of each frame go into s_hit[],
// and the next tap uses them).
// The task: the app task (render and input of app_serial.c, which includes this file).
// The memory: the file statics of this part (the touch targets); no heap.
//
//   THE WIRING  the connector of this board and a serial device (TX to RX, RX to TX, GND), the state of the RX line
//   TERMINAL    the state line (the source, the baud and the format, the bytes, the rate, the errors), the lines (TEXT)
//               or the bytes (HEX), the times; a drag shows older rows (LIVE goes back to the end)
//   SETTINGS    the baud (AUTO, TYPE), the format, the common settings, the line end, the view, SELF TEST, the source
//   SEND        the line (the keyboard), TEXT or HEX, the line end, the lines sent before

// THE ROUND FRAME (APP_ROUND_HI): the state line (or BACK and the title) in the top cap of the circle, the keys in a
// row in the bottom cap. The terminal fills the band between the caps (the bigger mono face); the rows of the chips of
// SETTINGS and SEND follow the chord of the circle down to its bottom. rl_origin.h moves each draw call of this file
// (also the sketch) right by rl_ox, and the touch targets move with them. The Rubik faces are KF_TEXT at the least.
#include "rndlay/rndlay.h"
#include "rndlay/rl_origin.h"
#include "rfpulse/rfp_sketch.inc"

#define C_BG    rgb(7, 10, 16)
#define C_PANEL rgb(15, 21, 32)
#define C_ROW   rgb(11, 15, 23)
#define C_LINE  rgb(30, 40, 56)
#define C_TEXT  rgb(228, 234, 242)
#define C_DIM   rgb(120, 134, 154)
#define C_FAINT rgb(70, 82, 100)
#define C_ACC   rgb(255, 196, 70)   // the colour of the app (an amber terminal)
#define C_TXC   rgb(200, 140, 255)  // the lines that the board sent
#define C_NOTE  rgb(255, 160, 60)
#define C_OK    rgb(70, 220, 120)
#define C_BAD   rgb(255, 86, 80)
#define C_BTN   rgb(26, 36, 52)
#define C_BTN_E rgb(44, 58, 80)
#define C_HEXA  rgb(150, 210, 255)  // the text column of HEX

// ---------------------------------------------------------------- the layout
static int s_W, s_H, s_top_h;
static bool s_narrow;
static int s_tx0, s_ty0, s_tx1, s_ty1;  // the terminal area
static int s_lh;                        // the height of a row of the terminal
static float s_set_scroll;              // SETTINGS (px)

static void layout(void)
{
    s_W = gfx_w;
    s_H = gfx_h;
    s_narrow = s_W < 480;
    s_top_h = BAR ? 26 : 30;
    s_tx0 = 4;
    s_ty0 = s_top_h + 3;
    s_tx1 = s_narrow ? s_W - 4 : s_W - 108;
    s_ty1 = s_narrow ? s_H - 48 : s_H - 2;
    rl_ox = 0;
    rl_big = gfx_round() ? 2 : 0;  // (the round frame: KF_TEXT at the least)
    if (gfx_round()) {
        s_top_h = rl_t_top();
        rl_ox = gfx_w / 2 - rl_half(s_top_h + 16, 12);
        s_W = gfx_w - 2 * rl_ox;
        s_H = rl_t_bot();
        s_narrow = true;
        s_ty0 = s_top_h + 3;
        s_ty1 = s_H - 4;
        // (the terminal: the chord at the top of the band, a little wider than the box)
        const int tw = 2 * rl_half(s_ty0 + 6, 8);
        s_tx0 = gfx_w / 2 - tw / 2 - rl_ox;
        s_tx1 = s_tx0 + tw;
    }
}

// ---------------------------------------------------------------- the touch targets
enum {
    H_CONNECTED,
    H_DEMO,
    H_SET,
    H_SEND,
    H_PAUSE,
    H_LIVE,
    H_BACK,
    H_BAUD,
    H_AUTO,
    H_TYPE,
    H_BITS,
    H_PAR,
    H_STOP,
    H_PRESET,
    H_END,
    H_VIEW,
    H_TIMES,
    H_ECHO,
    H_INV,
    H_TEST,
    H_WIRE,
    H_SRC,
    H_LINE,
    H_MODE,
    H_HIST,
    H_DOSEND
};
typedef struct {
    int16_t x, y, w, h, id, arg;
} hit_t;
#define HITN 96
static hit_t s_hit[HITN];
static int s_nhit;
static void hit(int x, int y, int w, int h, int id, int arg)
{
    if (s_nhit < HITN) {
        x += rl_ox;  // (the round frame: the place on the screen)
        s_hit[s_nhit++] = (hit_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)id, (int16_t)arg};
    }
}
static int hit_at(int x, int y)
{
    for (int i = s_nhit - 1; i >= 0; i--) {
        const hit_t *h = &s_hit[i];
        if (x >= h->x - 4 && x < h->x + h->w + 4 && y >= h->y - 4 && y < h->y + h->h + 4) {
            return i;
        }
    }
    return -1;
}

static void hit_do(int i)
{
    const hit_t *h = &s_hit[i];
    switch (h->id) {
    case H_CONNECTED:
        store_set("ser_wired", 1);
        s_scr = SC_TERM;
        break;
    case H_DEMO:
        set_src(SER_DEMO);
        s_scr = SC_TERM;
        break;
    case H_SET:
        s_scr = SC_SET;
        break;
    case H_SEND:
        s_scr = SC_SEND;
        break;
    case H_PAUSE:
        s_paused = !s_paused;
        s_pause_ln = s_nln;
        s_pause_hx = s_nhx;
        break;
    case H_LIVE:
        s_back = 0;
        s_follow = true;
        s_paused = false;
        break;
    case H_BACK:
        s_scr = SC_TERM;
        break;
    case H_BAUD:
        s_cfg.baud = BAUDS[h->arg];
        apply_cfg();
        break;
    case H_AUTO:
        s_scr = SC_TERM;
        auto_start();
        break;
    case H_TYPE:
        kbd_go(1);
        break;
    case H_BITS:
        s_cfg.bits = (uint8_t)h->arg;
        apply_cfg();
        break;
    case H_PAR:
        s_cfg.parity = (char)h->arg;
        apply_cfg();
        break;
    case H_STOP:
        s_cfg.stop = (uint8_t)h->arg;
        apply_cfg();
        break;
    case H_PRESET: {
        const preset_t *p = &PRESETS[h->arg];
        s_cfg.baud = p->baud;
        s_cfg.bits = p->bits;
        s_cfg.parity = p->parity;
        s_cfg.stop = p->stop;
        apply_cfg();
        break;
    }
    case H_END:
        s_end = h->arg;
        save();
        break;
    case H_VIEW:
        s_view = h->arg;
        s_back = 0;
        s_follow = true;
        save();
        break;
    case H_TIMES:
        s_times = h->arg;
        save();
        break;
    case H_ECHO:
        s_echo = h->arg;
        save();
        break;
    case H_INV:
        s_cfg.inv = h->arg;
        apply_cfg();
        break;
    case H_TEST:
        self_test();
        break;
    case H_WIRE:
        if (s_src != SER_PINS) {
            set_src(SER_PINS);
        }
        s_scr = SC_WIRE;
        break;
    case H_SRC:
        set_src(h->arg);
        break;
    case H_LINE:
        kbd_go(0);
        break;
    case H_MODE:
        s_send_hex = h->arg;
        save();
        break;
    case H_HIST:
        if (h->arg < s_nhist) {
            char b[96];
            snprintf(b, sizeof b, "%s", s_hist[h->arg]);
            snprintf(s_line, sizeof s_line, "%s", b);
            send_text(b);
        }
        break;
    case H_DOSEND:
        if (s_line[0] || !s_send_hex) {
            send_text(s_line);
        }
        break;
    }
}

// ---------------------------------------------------------------- small parts

static void button(int x, int y, int w, int h, const char *label, bool hot, int id, int arg)
{
    tfd_rrect(x, y, w, h, 8, hot ? C_ACC : C_BTN, 256);
    if (!hot) {
        tfd_rrect(x, y, w, 1, 0, C_BTN_E, 256);
    }
    const kfont_t *f = ui_text_w(&KF_NAME, label) <= w - 10 ? &KF_NAME : &KF_LABEL;
    tfd_text_c(f, x + w / 2, y + h / 2 + f->asc / 2 - 1, label, hot ? C_BG : C_TEXT, w - 8);
    hit(x, y, w, h, id, arg);
}

// a chip of a choice (a number or a technical name in the mono face, a word in the Rubik face): its width
static int chip_w(const char *s, bool word)
{
    return (word ? ui_text_w(&KF_LABEL, s) : mf_width(&SER_NUM, s, 0)) + 20;
}
static void chip(int x, int y, const char *s, bool word, bool on, int id, int arg)
{
    const bool rd = gfx_round();  // (the round frame: a chip of 44 px for KF_TEXT)
    const int w = chip_w(s, word), h = rd ? 44 : 30;
    tfd_rrect(x, y, w, h, h / 2, on ? C_ACC : C_BTN, 256);
    if (word) {
        tfd_text_c(&KF_LABEL, x + w / 2, y + (rd ? 29 : 20), s, on ? C_BG : C_TEXT, w);
    } else {
        mf_text(&SER_NUM, x + 10, y + (rd ? 28 : 20), s, on ? C_BG : C_TEXT, 255, 0);
    }
    hit(x, y, w, h, id, arg);
}

// the round frame: the top cap. BACK and the title, or the dot and the baud (a tap: SETTINGS), then the source and the
// counters (rl_ox = 0 here: the cap uses the whole width)
static void top_cap(const char *back_title, uint16_t dc, const char *baud, const char *line2, uint16_t c2)
{
    const int ox = rl_ox, W = gfx_w;
    rl_ox = 0;
    gfx_fill_rect(0, 0, W, s_top_h, C_PANEL);
    const int lw = 2 * rl_half(s_top_h, 2);
    gfx_fill_rect(W / 2 - lw / 2, s_top_h, lw, 1, C_LINE);
    if (back_title) {
        const int bw = W * 104 / 466, by = W * 22 / 466;
        tfd_rrect(W / 2 - bw / 2, by, bw, 32, 16, C_BTN, 256);
        tfd_text_c(&KF_NAME, W / 2, by + 22, TX(T_BACK), C_TEXT, bw - 8);
        hit(W / 2 - bw / 2 - 10, 0, bw + 20, by + 40, H_BACK, 0);
        tfd_text_c(&KF_NAME, W / 2, s_top_h - 12, back_title, C_TEXT, 2 * rl_half(s_top_h - 26, 20));
    } else {
        const int bw = mf_width(&SER_NUM, baud, 0), y = W * 50 / 466;
        tfd_disc((float)(W / 2 - bw / 2 - 14), (float)(y - 6), 4.5f, dc, 256);
        mf_text(&SER_NUM, W / 2 - bw / 2, y, baud, C_ACC, 255, 0);
        hit(W / 2 - bw / 2 - 30, 0, bw + 60, y + 8, H_SET, 0);
        tfd_text_c(&KF_LABEL, W / 2, W * 77 / 466, line2, c2, 2 * rl_half(W * 60 / 466, 24));
    }
    rl_ox = ox;
}
static void top_bar(const char *back_title)
{
    if (gfx_round() && back_title) {
        top_cap(back_title, 0, NULL, NULL, 0);
        return;
    }
    tfd_rect(0, 0, s_W, s_top_h, C_PANEL, 256);
    tfd_rect(0, s_top_h, s_W, 1, C_LINE, 256);
    const int base = s_top_h / 2 + 6;
    if (back_title) {
        tfd_rrect(4, 3, 64, s_top_h - 6, 6, C_BTN, 256);
        tfd_text_c(&KF_LABEL, 36, base - 1, TX(T_BACK), C_TEXT, 60);
        hit(0, 0, 80, s_top_h + 4, H_BACK, 0);
        tfd_text_l(&KF_NAME, 80, base, back_title, C_TEXT, s_W - 90);
        return;
    }
    // the dot: green while bytes come, grey when quiet, red when the port did not open
    const bool fresh = s_rate > 0.5f;
    const uint16_t dc = !ser_is_open() ? C_BAD : s_paused ? C_NOTE : fresh ? C_OK : C_FAINT;
    tfd_disc(12, (float)s_top_h / 2, 4.5f, dc, 256);
    char b[64], f[12];
    fmt_name(f, sizeof f, &s_cfg);
    snprintf(b, sizeof b, "%d %s", s_cfg.baud, f);
    const int bw = mf_width(&SER_NUM, b, 0);
    mf_text(&SER_NUM, 24, base, b, C_ACC, 255, 0);
    const char *src = s_src == SER_DEMO ? TX(T_DEMO) : rfp_board_uart()->data_pin;
    char sb[40];
    if (s_src == SER_DEMO) {
        snprintf(sb, sizeof sb, "%s", src);
    } else {
        snprintf(sb, sizeof sb, "RX %s  TX %s", rfp_board_uart()->data_pin, rfp_board_uart()->tx_pin);
    }
    hit(0, 0, 24 + bw + 10, s_top_h + 2, H_SET, 0);
    // the counters at the right end (the rate, the errors)
    char all[96];
    const uint32_t err = s_st.frame + s_st.parity + s_st.overflow;
    int k = s_st.rx >= 10240 ? snprintf(all, sizeof all, "%.1fK  %.0f B/s", (double)s_st.rx / 1024.0, (double)s_rate)
                             : snprintf(all, sizeof all, "%u B  %.0f B/s", (unsigned)s_st.rx, (double)s_rate);
    if (err) {
        snprintf(all + k, sizeof all - (size_t)k, "  ERR %u", (unsigned)err);
    }
    if (gfx_round()) {
        char l2[140];
        snprintf(l2, sizeof l2, "%s   %s", sb, all);
        top_cap(NULL, dc, b, l2, err ? C_NOTE : C_DIM);
        return;
    }
    const int rw = mf_width(&SER_M11, all, 0);
    mf_text(&SER_M11, s_W - 8 - rw, base - 1, all, err ? C_NOTE : C_DIM, 255, 0);
    // the source between them, if it fits
    const int sx = 24 + bw + 12, sw = ui_text_w(&KF_LABEL, sb);
    if (sx + sw < s_W - 16 - rw) {
        tfd_text_l(&KF_LABEL, sx, base - 1, sb, C_DIM, sw + 2);
    }
}

static void toast_draw(void)
{
    if (!s_toast[0] || s_now - s_toast_t > 2600) {
        return;
    }
    const int w = ui_text_w(&KF_NAME, s_toast) + 28;
    const int ww = w > s_W - 20 ? s_W - 20 : w;
    const int cx = s_narrow ? s_W / 2 : (s_W - 104) / 2;
    const int y = gfx_round() ? s_H - 44 : s_narrow ? s_H - 86 : s_top_h + 8;
    const int a = s_now - s_toast_t > 2200 ? (int)(256 * (2600 - (s_now - s_toast_t)) / 400) : 256;
    tfd_rrect(cx - ww / 2, y, ww, 34, 10, rgb(36, 54, 78), a);
    tfd_text_c(&KF_NAME, cx, y + 23, s_toast, C_TEXT, ww - 16);
}

// the column of buttons at the right end (the 3.49) or the bar at the bottom (the 320 x 240 frame)
static void side_buttons(const char *const *l, const int *id, const bool *hot, int n)
{
    if (gfx_round()) {  // (the keys in a row in the bottom cap; rl_ox = 0 here: the screen points)
        const int ox = rl_ox, y = rl_t_key();
        rl_ox = 0;
        rl_t_bcap(y - 6, C_PANEL, C_LINE);
        for (int i = 0; i < n; i++) {
            int x, w;
            rl_t_keyx(i, n, &x, &w);
            button(x, y, w, rl_t_kh(), l[i], hot[i], id[i], 0);
        }
        rl_ox = ox;
    } else if (s_narrow) {
        const int g = 6, w = (s_W - (n + 1) * g) / n, y = s_H - 42;
        tfd_rect(0, y - 4, s_W, 46, C_PANEL, 256);
        for (int i = 0; i < n; i++) {
            button(g + i * (w + g), y, w, 38, l[i], hot[i], id[i], 0);
        }
    } else {
        const int x = s_W - 100, y0 = s_top_h + 4, g = 4;
        const int h = (s_H - y0 - 2 - (n - 1) * g) / n;
        tfd_rect(s_W - 104, s_top_h + 1, 104, s_H - s_top_h - 1, C_PANEL, 256);
        for (int i = 0; i < n; i++) {
            button(x, y0 + i * (h + g), 96, h, l[i], hot[i], id[i], 0);
        }
    }
}

// ---------------------------------------------------------------- THE WIRING

static void draw_wire(void)
{
    const rfp_conn_t *k = rfp_board_uart();
    static const sk_pin_t PINS[3] = {{"TX", RFP_PAD_RX}, {"RX", RFP_PAD_TX}, {"GND", RFP_PAD_GND}};
    const sk_dev_t dev = {TX(T_DEVICE), PINS, 3};
    // the title (the 3.49: left of the grip of the panel; the round frame: in the top cap), the levels
    if (gfx_round()) {
        top_cap(NULL, 0, "", "", 0);
        s_nhit = 0;  // (no SETTINGS key in the cap of THE WIRING)
        tfd_text_c(&KF_NAME, s_W / 2, s_top_h - 16, TX(T_CONNECT), C_TEXT, 2 * rl_half(s_top_h - 18, 8));
    } else {
        tfd_text_s(&KF_LABEL, 8, BAR ? 290 : s_W - 8, 17, TX(T_CONNECT), C_TEXT, s_he);
    }
    const char *st;
    uint16_t sc;
    if (!ser_is_open()) {
        st = ser_err(s_err);
        sc = C_BAD;
    } else if (s_st.rx > 0) {
        st = TX(T_RX_DATA);
        sc = C_OK;
    } else if (s_st.level == 0) {
        st = TX(T_RX_LOW);
        sc = C_NOTE;
    } else {
        st = TX(T_RX_HIGH);
        sc = C_DIM;
    }
    const bool bus = k->shape == RFP_SHAPE_BUS30;
    if (BAR) {
        sk_draw(k, &dev, &SER_M11, 6, 30, s_W - 110, s_H - 22, s_he);
        tfd_text_s(&KF_LABEL, 340, s_W - 112, 17, TX(T_LEVELS), C_NOTE, s_he);
        const int tw = ui_text_w(&KF_LABEL, st) + 26;
        tfd_rrect(4, s_H - 19, tw < s_W - 116 ? tw : s_W - 116, 18, 5, C_BG, 235);
        tfd_disc(13, (float)s_H - 10, 4.5f, sc, 256);
        tfd_text_s(&KF_LABEL, 22, s_W - 112, s_H - 5, st, sc, s_he);
    } else if (bus) {
        sk_draw(k, &dev, &SER_M11, 0, s_top_h - 2, s_W, s_H - 4, s_he);
        tfd_text_c(&KF_LABEL, 222, 142, TX(T_LEVELS), C_NOTE, 190);
        tfd_text_c(&KF_LABEL, 222, 160, st, sc, 190);
    } else {
#if BOARD_UI_TABLET
        // the Tab5: the sketch 1.5 times bigger (rfp_sketch.inc) at the left of the keys, the words in the bigger face
        sk_draw(k, &dev, &SER_M13, 24, 64, s_W - 108, s_H - 90, s_he);
        tfd_text_c(&KF_NAME, (s_W - 104) / 2, s_H - 64, TX(T_LEVELS), C_NOTE, s_W - 116);
        tfd_text_c(&KF_NAME, (s_W - 104) / 2, s_H - 36, st, sc, s_W - 116);
#else
        if (gfx_round()) {  // (the sketch in the band, the two notes under it)
            sk_draw(k, &dev, &SER_M13, 0, s_top_h + 4, s_W, s_H - 54, s_he);
            tfd_text_c(&KF_LABEL, s_W / 2, s_H - 30, TX(T_LEVELS), C_NOTE, s_W - 12);
            tfd_text_c(&KF_LABEL, s_W / 2, s_H - 6, st, sc, s_W - 12);
        } else {
            sk_draw(k, &dev, &SER_M11, 4, 24, s_W - 4, s_H - 70, s_he);
            tfd_text_c(&KF_LABEL, s_W / 2, s_H - 72, TX(T_LEVELS), C_NOTE, s_W - 12);
            tfd_text_c(&KF_LABEL, s_W / 2, s_H - 54, st, sc, s_W - 12);
        }
#endif
    }
    const char *l[2] = {TX(T_CONNECTED), TX(T_DEMO)};
    const int id[2] = {H_CONNECTED, H_DEMO};
    const bool hot[2] = {s_st.rx > 0, false};
    if (bus && s_narrow && !gfx_round()) {  // (the M5-Bus takes the left side: the buttons under the texts)
        button(130, s_H - 70, 184, 30, l[0], hot[0], id[0], 0);
        button(130, s_H - 36, 184, 30, l[1], false, id[1], 0);
    } else {
        side_buttons(l, id, hot, 2);
    }
}

// ---------------------------------------------------------------- TERMINAL

static int s_cols;
static const mfont_t *term_font(void)
{
    if (gfx_round()) {  // (the round frame: the biggest mono face that gives 8 bytes a row of HEX)
        return 39 * 9 + 4 <= s_tx1 - s_tx0 ? &SER_NUM : &SER_M13;
    }
    return s_narrow || s_view == 1 ? &SER_M11 : &SER_M13;
}

// the visible text of a part of a line: a byte that is not a letter becomes a middle dot (0xB7 in the face)
static void vis(char *o, const char *s, int n)
{
    for (int i = 0; i < n; i++) {
        const uint8_t c = (uint8_t)s[i];
        o[i] = c >= 32 && c < 127 ? (char)c : c == '\t' ? ' ' : (char)0xB7;
    }
    o[n] = 0;
}

static int prefix_cols(const line_t *l)
{
    return (s_times ? 10 : 0) + (l->dir == D_RX ? 0 : 2);
}
static int line_rows(const line_t *l)
{
    if (l->dir == D_NOTE && tfd_heb(l->s)) {
        return 1;
    }
    const int p = prefix_cols(l), room = s_cols - p > 8 ? s_cols - p : 8;
    return l->len <= room ? 1 : 1 + (l->len - 1) / room;
}

static void draw_text(void)
{
    const mfont_t *f = term_font();
    const int adv = mf_width(f, "M", 0);
    s_lh = f == &SER_NUM ? 21 : f == &SER_M13 ? 16 : 14;
    s_cols = (s_tx1 - s_tx0) / adv;
    const uint32_t end = s_paused ? s_pause_ln : s_nln;
    const uint32_t first = end > LN ? end - LN : 0;
    int total = 0;
    for (uint32_t i = first; i < end; i++) {
        total += line_rows(ln_at(i));
    }
    const int vis_rows = (s_ty1 - s_ty0) / s_lh;
    if (!s_follow && total > s_rows_prev) {
        s_back += (float)(total - s_rows_prev);  // (the view stays on its rows)
    }
    s_rows_prev = total;
    const float mx = (float)(total - vis_rows);
    if (s_back > mx) {
        s_back = mx > 0 ? mx : 0;
    }
    int top = total - vis_rows - (int)(s_back + 0.5f);
    if (top < 0) {
        top = 0;
    }
    // the first line on the screen
    uint32_t i = first;
    int row = 0;
    while (i < end && row + line_rows(ln_at(i)) <= top) {
        row += line_rows(ln_at(i++));
    }
    int y = s_ty0 + f->asc + 2;
    char b[LW + 4], ts[16];
    for (; i < end && y <= s_ty1; i++) {
        const line_t *l = ln_at(i);
        const int p = prefix_cols(l), room = s_cols - p > 8 ? s_cols - p : 8;
        const uint16_t c = l->dir == D_TX ? C_TXC : l->dir == D_NOTE ? C_NOTE : C_TEXT;
        if (l->dir == D_NOTE && tfd_heb(l->s)) {  // (a note of the app in Hebrew: the Rubik face, at the right)
            if (row++ >= top && y <= s_ty1) {
                tfd_text_r(&KF_LABEL, s_tx1 - 4, y, l->s, c, s_tx1 - s_tx0 - 8);
                y += s_lh;
            }
            continue;
        }
        for (int r = 0, at = 0; r < line_rows(l); r++, at += room) {
            if (row++ < top) {
                continue;
            }
            if (y > s_ty1) {
                break;
            }
            int x = s_tx0;
            if (r == 0) {
                if (s_times && !l->cont) {
                    snprintf(ts, sizeof ts, "%5d.%03d", (int)(l->t / 1000) % 100000, (int)(l->t % 1000));
                    mf_text(f, x, y, ts, C_FAINT, 255, 0);
                }
                if (s_times) {
                    x += 10 * adv;
                }
                if (l->dir != D_RX) {
                    const char m[2] = {(char)(l->dir == D_TX ? 0xBB : 0xB7), 0};
                    mf_text(f, x, y, m, c, 255, 0);
                    x += 2 * adv;
                }
            } else {
                x += p * adv;
            }
            const int n = l->len - at < room ? l->len - at : room;
            vis(b, l->s + at, n > 0 ? n : 0);
            mf_text(f, x, y, b, c, 255, 0);
            y += s_lh;
        }
    }
    if (!total) {
        const int cx = (s_tx0 + s_tx1) / 2, cy = (s_ty0 + s_ty1) / 2;
        tfd_text_c(&KF_NAME, cx, cy - 4, ser_is_open() ? TX(T_WAIT_DATA) : ser_err(s_err), C_DIM, s_tx1 - s_tx0 - 10);
    }
}

static void draw_hex(void)
{
    const mfont_t *f = gfx_round() ? term_font() : &SER_M11;
    const int adv = mf_width(f, "M", 0);
    const int bpr = s_narrow ? 8 : 16;
    s_lh = f == &SER_NUM ? 21 : f == &SER_M13 ? 16 : 14;
    const uint32_t end = s_paused ? s_pause_hx : s_nhx;
    const uint32_t first = end > HX ? (end - HX + bpr - 1) / bpr * bpr : 0;
    const int total = (int)((end - first + bpr - 1) / bpr);
    const int vis_rows = (s_ty1 - s_ty0) / s_lh;
    if (!s_follow && total > s_rows_prev) {
        s_back += (float)(total - s_rows_prev);
    }
    s_rows_prev = total;
    const float mx = (float)(total - vis_rows);
    if (s_back > mx) {
        s_back = mx > 0 ? mx : 0;
    }
    int top = total - vis_rows - (int)(s_back + 0.5f);
    if (top < 0) {
        top = 0;
    }
    char b[96];
    int y = s_ty0 + f->asc + 2;
    for (int r = top; r < total && y <= s_ty1; r++, y += s_lh) {
        const uint32_t at = first + (uint32_t)r * (uint32_t)bpr;
        snprintf(b, sizeof b, "%04X", (unsigned)(at & 0xFFFF));
        mf_text(f, s_tx0, y, b, C_FAINT, 255, 0);
        int x = s_tx0 + 6 * adv;
        char asc[20];
        int na = 0;
        for (int j = 0; j < bpr; j++) {
            if (at + (uint32_t)j >= end) {
                break;
            }
            const uint8_t v = s_hx[(at + (uint32_t)j) % HX];
            snprintf(b, sizeof b, "%02X", v);
            mf_text(f, x + j * 3 * adv + (j >= bpr / 2 ? adv / 2 : 0), y, b, C_TEXT, 255, 0);
            asc[na++] = v >= 32 && v < 127 ? (char)v : (char)0xB7;
        }
        asc[na] = 0;
        mf_text(f, x + bpr * 3 * adv + adv, y, asc, C_HEXA, 255, 0);
    }
    if (!total) {
        tfd_text_c(&KF_NAME, (s_tx0 + s_tx1) / 2, (s_ty0 + s_ty1) / 2 - 4, TX(T_WAIT_DATA), C_DIM, s_tx1 - s_tx0 - 10);
    }
}

static void draw_auto(void)
{
    if (s_auto < 0) {
        return;
    }
    const int w = s_narrow ? s_W - 30 : 380, h = 78;
    const int x = (s_narrow ? s_W : s_W - 104) / 2 - w / 2, y = (s_ty0 + s_ty1) / 2 - h / 2;
    tfd_rrect(x, y, w, h, 12, rgb(24, 32, 48), 245);
    char b[64];
    snprintf(b, sizeof b, TX(T_TRYING), AUTO_LIST[s_auto]);
    tfd_text_c(&KF_NAME, x + w / 2, y + 26, b, C_TEXT, w - 20);
    tfd_rrect(x + 16, y + 42, w - 32, 10, 5, C_BTN, 256);
    tfd_rrect(x + 16, y + 42, (w - 32) * (s_auto + 1) / NAUTO, 10, 5, C_ACC, 256);
    char s[48];
    snprintf(s, sizeof s, "%d / %d", s_auto + 1, NAUTO);
    const mfont_t *mf = gfx_round() ? &SER_M13 : &SER_M11;
    mf_text(mf, x + w / 2 - mf_width(mf, s, 0) / 2, y + 70, s, C_DIM, 255, 0);
}

static void draw_term(void)
{
    s_lh = 14;
    if (s_view == 1) {
        draw_hex();
    } else {
        draw_text();
    }
    if (!s_follow || s_paused) {      // LIVE: back to the end of the rows
        const bool rd = gfx_round();  // (the round frame: a key of 48 px)
        const int w = rd ? 100 : 84, h = rd ? 48 : 32, x = s_tx1 - w - 6, y = s_ty1 - h - 4;
        tfd_rrect(x, y, w, h, h / 2, C_ACC, 240);
        tfd_text_c(&KF_LABEL, x + w / 2, y + (rd ? 31 : 21), TX(T_LIVE), C_BG, w - 8);
        hit(x, y, w, h, H_LIVE, 0);
    }
    draw_auto();
    top_bar(NULL);
    const char *l[3] = {TX(T_SETTINGS), TX(T_SEND), s_paused ? TX(T_RUN) : TX(T_PAUSE)};
    const int id[3] = {H_SET, H_SEND, H_PAUSE};
    const bool hot[3] = {false, true, s_paused};
    side_buttons(l, id, hot, 3);
}

// ---------------------------------------------------------------- SETTINGS (a list that scrolls)

typedef struct {
    int x0, x1, x, y, h;
} flow_t;
// the round frame: the start and the end of the row at f->y follow the chord of the circle (the points of the origin)
static void flow_row(flow_t *f)
{
    if (gfx_round()) {
        int x, w;
        rl_t_row(f->y, 44, s_top_h, gfx_h - 16, 18, &x, &w);
        f->x0 = x - rl_ox;
        f->x1 = x + w - rl_ox;
    }
}
static void flow_label(flow_t *f, const char *s)
{
    const bool rd = gfx_round();
    f->y += f->h ? f->h + 10 : 6;
    flow_row(f);
    f->x = f->x0;
    tfd_text_s(&KF_LABEL, f->x0 + 2, f->x1 - 2, f->y + (rd ? 18 : 13), s, C_DIM, s_he);
    f->y += rd ? 26 : 18;
    flow_row(f);
    f->x = f->x0;
    f->h = 0;
}
// the place of the next chip of width w (a new row when it does not fit)
static void flow_next(flow_t *f, int w, int *x, int *y)
{
    const int ch = gfx_round() ? 44 : 30;
    if (f->x + w > f->x1 && f->x > f->x0) {
        f->y += ch + 6;
        flow_row(f);
        f->x = f->x0;
    }
    *x = f->x;
    *y = f->y;
    f->x += w + 6;
    f->h = ch;
}
static void fchip(flow_t *f, const char *s, bool word, bool on, int id, int arg)
{
    int x, y;
    flow_next(f, chip_w(s, word), &x, &y);
    if (y + 30 >= s_top_h && y < (gfx_round() ? gfx_h : s_H)) {
        chip(x, y, s, word, on, id, arg);
    }
}
static void fgap(flow_t *f)
{
    f->x += 14;
}

static void draw_set(void)
{
    const int x1 = s_narrow ? s_W - 6 : s_W - 6;
    flow_t f = {8, x1, 8, s_top_h + 2 - (int)s_set_scroll, 0};
    char b[48];
    flow_label(&f, TX(T_BAUD));
    for (int i = 0; i < NBAUD; i++) {
        snprintf(b, sizeof b, "%d", BAUDS[i]);
        fchip(&f, b, false, s_cfg.baud == BAUDS[i], H_BAUD, i);
    }
    fchip(&f, TX(T_AUTO), true, false, H_AUTO, 0);
    bool other = true;
    for (int i = 0; i < NBAUD; i++) {
        other &= s_cfg.baud != BAUDS[i];
    }
    if (other) {
        snprintf(b, sizeof b, "%s %d", TX(T_TYPE), s_cfg.baud);
    } else {
        snprintf(b, sizeof b, "%s", TX(T_TYPE));
    }
    fchip(&f, b, true, other, H_TYPE, 0);
    flow_label(&f, TX(T_FORMAT));
    for (int v = 5; v <= 8; v++) {
        snprintf(b, sizeof b, "%d", v);
        fchip(&f, b, false, s_cfg.bits == v, H_BITS, v);
    }
    fgap(&f);
    fchip(&f, "N", false, s_cfg.parity == 'N', H_PAR, 'N');
    fchip(&f, "E", false, s_cfg.parity == 'E', H_PAR, 'E');
    fchip(&f, "O", false, s_cfg.parity == 'O', H_PAR, 'O');
    fgap(&f);
    fchip(&f, "1", false, s_cfg.stop == 10, H_STOP, 10);
    fchip(&f, "1.5", false, s_cfg.stop == 15, H_STOP, 15);
    fchip(&f, "2", false, s_cfg.stop == 20, H_STOP, 20);
    flow_label(&f, TX(T_COMMON));
    for (int i = 0; i < NPRESET; i++) {
        const preset_t *p = &PRESETS[i];
        char fm[12];
        const ser_cfg_t c = {p->baud, p->bits, p->parity, p->stop, false};
        fmt_name(fm, sizeof fm, &c);
        snprintf(b, sizeof b, "%s %d %s", p->name, p->baud, fm);
        const bool on =
                s_cfg.baud == p->baud && s_cfg.bits == p->bits && s_cfg.parity == p->parity && s_cfg.stop == p->stop;
        fchip(&f, b, true, on, H_PRESET, i);
    }
    flow_label(&f, TX(T_LINE_END));
    static const char *const END[4] = {"NONE", "LF", "CR", "CR+LF"};
    for (int i = 0; i < 4; i++) {
        fchip(&f, i ? END[i] : (s_he ? "בלי" : "NONE"), i == 0, s_end == i, H_END, i);
    }
    flow_label(&f, TX(T_VIEW));
    fchip(&f, TX(T_TEXT), true, s_view == 0, H_VIEW, 0);
    fchip(&f, "HEX", false, s_view == 1, H_VIEW, 1);
    fgap(&f);
    snprintf(b, sizeof b, "%s: %s", TX(T_TIMES), TX(s_times ? T_ON : T_OFF));
    fchip(&f, b, true, s_times, H_TIMES, !s_times);
    snprintf(b, sizeof b, "%s: %s", TX(T_ECHO), TX(s_echo ? T_ON : T_OFF));
    fchip(&f, b, true, s_echo, H_ECHO, !s_echo);
    snprintf(b, sizeof b, "%s: %s", TX(T_INVERT), TX(s_cfg.inv ? T_ON : T_OFF));
    fchip(&f, b, true, s_cfg.inv, H_INV, !s_cfg.inv);
    if (ser_has_pins()) {
        flow_label(&f, TX(T_SOURCE));
        fchip(&f, TX(T_PINS), true, s_src == SER_PINS, H_SRC, SER_PINS);
        fchip(&f, TX(T_DEMO), true, s_src == SER_DEMO, H_SRC, SER_DEMO);
        fgap(&f);
        fchip(&f, TX(T_WIRING), true, false, H_WIRE, 0);
        if (s_src == SER_PINS) {
            fchip(&f, TX(T_SELFTEST), true, false, H_TEST, 0);
        }
    }
    // the end of the list: the scroll limit (the round frame: the list goes down to the bottom of the circle)
    const int content = f.y + 40 + (int)s_set_scroll - s_top_h;
    const float mx = (float)(content - ((gfx_round() ? gfx_h - 60 : s_H) - s_top_h));
    if (s_set_scroll > mx) {
        s_set_scroll = mx > 0 ? mx : 0;
    }
    top_bar(TX(T_SETTINGS));
}

// ---------------------------------------------------------------- SEND

static void draw_send(void)
{
    const bool rd = gfx_round();
    const int g = 6, x1 = s_narrow ? s_W - g : s_W - 110, lh = rd ? 54 : 42;  // (the round frame: a field of 54 px)
    int y = s_top_h + (rd ? 10 : 6);
    // the line (a tap opens the keyboard)
    tfd_rrect(g, y, x1 - g, lh, 8, C_PANEL, 256);
    tfd_rrect(g, y + lh - 2, x1 - g, 2, 1, C_LINE, 256);
    char b[120];
    vis(b, s_line, (int)strlen(s_line));
    if (b[0]) {
        mf_text(&SER_NUM, g + 10, y + lh / 2 + 6, b, C_TEXT, 255, 0);
    } else {
        tfd_text_s(&KF_LABEL, g + 10, x1 - 10, y + (rd ? 34 : 27), s_send_hex ? TX(T_BADHEX) : "...", C_FAINT, s_he);
    }
    hit(g, y, x1 - g, lh, H_LINE, 0);
    y += lh + 6;
    flow_t f = {g, x1, g, y - 6, 0};
    f.h = 0;
    f.y = y;
    flow_row(&f);
    f.x = f.x0;
    fchip(&f, TX(T_TEXT), true, !s_send_hex, H_MODE, 0);
    fchip(&f, "HEX", false, s_send_hex, H_MODE, 1);
    if (!s_send_hex) {  // (the line end: one chip; a tap gives the next one)
        fgap(&f);
        static const char *const END[4] = {"NONE", "LF", "CR", "CR+LF"};
        char e[40];
        snprintf(e, sizeof e, "%s %s", s_he ? "סוף" : "END", s_end ? END[s_end] : (s_he ? "בלי" : "NONE"));
        fchip(&f, e, true, s_end != 0, H_END, (s_end + 1) & 3);
    }
    // the lines sent before, as chips (a tap sends one again)
    if (s_nhist) {
        flow_label(&f, TX(T_HISTORY));
        for (int i = 0; i < s_nhist; i++) {
            vis(b, s_hist[i], (int)strlen(s_hist[i]));
            if (strlen(b) > 22) {
                memcpy(b + 19, "...", 4);
            }
            int x, yy;
            const int w = mf_width(&SER_NUM, b, 0) + 20, ch = rd ? 44 : 30;
            flow_next(&f, w, &x, &yy);
            if (yy + ch > (rd ? s_H : s_narrow ? s_H - 48 : s_H)) {
                break;
            }
            tfd_rrect(x, yy, w, ch, ch / 2, C_ROW, 256);
            mf_text(&SER_NUM, x + 10, yy + (rd ? 28 : 20), b, C_TXC, 255, 0);
            hit(x, yy, w, ch, H_HIST, i);
        }
    }
    top_bar(TX(T_SEND));
    const char *l[1] = {TX(T_SEND)};
    const int id[1] = {H_DOSEND};
    const bool hot[1] = {true};
    side_buttons(l, id, hot, 1);
}

// ---------------------------------------------------------------- render

static void render(int64_t now)
{
    if (s_scr == SC_KBD) {
        kbd_render(now);
        return;
    }
    layout();
    tfd_screen();
    tfd_noclip();
    s_nhit = 0;
    if (gfx_round()) {
        gfx_fill(C_BG);  // (the whole circle: the box below covers only its middle)
    }
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    switch (s_scr) {
    case SC_WIRE:
        draw_wire();
        break;
    case SC_SET:
        draw_set();
        break;
    case SC_SEND:
        draw_send();
        break;
    default:
        draw_term();
        break;
    }
    toast_draw();
}

// ---------------------------------------------------------------- the menu icon
#include "rndlay/rl_origin_end.h"  // (the menu icon draws at its own points)
#include "tficon/tficon.h"
// the menu icon: a terminal window: lines of text come in at the bottom and go up; a cursor blinks; a cable
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0x8a7bff), tfi_hex(0x2c1a8a));
    float u = s / 100.0f, ph = tfi_ph(2);
    int wx = x + (int)(14 * u), wy = y + (int)(16 * u), ww = (int)(72 * u), wh = (int)(56 * u);
    tfi_drop(wx, wy, ww, wh, (int)(6 * u), (int)(3 * u), 90);
    tfi_rbox(wx, wy, ww, wh, (int)(6 * u), tfi_hex(0x1c2034), tfi_hex(0x0a0c16), 256);
    tfi_rbox(wx, wy, ww, (int)(10 * u), (int)(6 * u), tfi_hex(0x4a5070), tfi_hex(0x343a58), 256);
    tfd_rect(wx, wy + (int)(6 * u), ww, (int)(4 * u), tfi_hex(0x343a58), 256);
    static const uint32_t DC[3] = {0xff5f57, 0xffbd2e, 0x28c940};
    for (int k = 0; k < 3; k++) {
        tfi_disc(wx + (6 + k * 6) * u, wy + 5 * u, 2 * u, tfi_hex(DC[k]), tfi_hex(DC[k]), 256);
    }
    // the lines: one new line each 0.5 s, they go up smoothly
    static const uint8_t TXW[4] = {44, 30, 52, 22};
    static const uint32_t TXC[4] = {0x5dff8a, 0xffffff, 0xffd84a, 0x7ad7ff};
    float sc = ph * 4, f = sc - (int)sc;
    int n0 = (int)sc;
    float lh = 9 * u, by = wy + 48 * u;
    tfd_clip(wx, wy + (int)(12 * u), wx + ww, wy + wh - (int)(3 * u));
    for (int k = 0; k < 6; k++) {
        int id = (n0 - k + 400) % 4;
        float ly = by - k * lh - tfi_step(0, 0.3f, f) * lh + lh;
        tfi_rbox(wx + (int)(6 * u), (int)ly, (int)(TXW[id] * u), (int)(4 * u), (int)(2 * u), tfi_hex(TXC[id]),
                 tfi_hex(TXC[id]), 230);
    }
    tfd_clip(x, y, x + s, y + s);
    if (f < 0.5f) {
        tfd_rect(wx + (int)(6 * u), (int)(by + 8 * u), (int)(6 * u), (int)(5 * u), tfi_hex(0x5dff8a), 256);
    }
    // the cable
    tfd_rect(x + (int)(47 * u), wy + wh, (int)(6 * u), (int)(8 * u), tfi_hex(0x2a2e3c), 256);
    tfi_rbox(x + (int)(40 * u), wy + wh + (int)(7 * u), (int)(20 * u), (int)(8 * u), (int)(2 * u), tfi_hex(0xe0e4ec),
             tfi_hex(0x9aa0b0), 256);
    tfi_end();
}
