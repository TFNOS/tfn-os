// I2C SCANNER: THE SCREENS (they read the state of app_i2c.c; the touch targets of each frame go into s_hit[]).
// The task: the app task (render and input of app_i2c.c, which includes this file).
// The memory: the file statics of this part (the touch targets); no heap.
//
//   SCAN    the state line (the bus, the scans), the map of the addresses (i2cdetect style) and the list of the
//           devices (the 320 x 240 frame: LIST or MAP), THE WIRING, LIVE
//   A DEVICE  its addresses, the chip of the board or the chip that the ID registers name, the candidates,
//           IDENTIFY, READ THE REGISTERS (a map of 16 x 16 values)
//   THE WIRING  the connector of the bus and an I2C module (SDA, SCL, VCC, GND)

// THE ROUND FRAME (APP_ROUND_HI): the state line (or BACK and the title) in the top cap of the circle, the keys in a
// row in the bottom cap, the content in the band between them. The list rows follow the chord of the circle (each row
// gets its own origin rl_ox), the map of the addresses fills the band, the page of a device and the wiring use a
// middle column of the band. rl_origin.h moves each draw call of this file (also the sketch) right by rl_ox, and the
// touch targets move with them. The Rubik faces are KF_TEXT at the least (rl_big 2).
#include "rndlay/rndlay.h"
#include "rndlay/rl_origin.h"
#include "rfpulse/rfp_sketch.inc"

#define C_BG    rgb(7, 10, 16)
#define C_PANEL rgb(15, 21, 32)
#define C_ROW   rgb(11, 15, 23)
#define C_LINE  rgb(30, 40, 56)
#define C_TEXT  rgb(228, 234, 242)
#define C_DIM   rgb(120, 134, 154)
#define C_FAINT rgb(60, 72, 90)
#define C_ACC   rgb(80, 220, 140)   // the colour of the app (the green of SDA)
#define C_BOARD rgb(110, 170, 255)  // a chip of the board
#define C_NEWC  rgb(255, 210, 80)
#define C_WARN  rgb(255, 160, 60)
#define C_BAD   rgb(255, 86, 80)
#define C_BTN   rgb(26, 36, 52)
#define C_BTN_E rgb(44, 58, 80)

static int s_W, s_H, s_top_h;
static int s_kb;  // the height of the keys under the content of the narrow frame (0: the round frame, keys in the cap)
static bool s_narrow;

static void layout(void)
{
    s_W = gfx_w;
    s_H = gfx_h;
    s_narrow = s_W < 480;
    s_top_h = BAR ? 26 : (s_narrow ? 30 : 44);  // (the Tab5, 640 x 360: a bar of 7.5 mm, a BACK key for a thumb)
    s_kb = 48;
    rl_ox = 0;
    rl_big = gfx_round() ? 2 : 0;  // (the round frame: KF_TEXT at the least)
    if (gfx_round()) {
        s_top_h = rl_t_top();
        rl_ox = gfx_w / 2 - rl_half(s_top_h + 16, 12);
        s_W = gfx_w - 2 * rl_ox;
        s_H = rl_t_bot();
        s_kb = 0;
        s_narrow = true;
    }
}

// ---------------------------------------------------------------- the touch targets
enum {
    H_DEV,
    H_WIRE,
    H_LIVE,
    H_SCAN,
    H_VIEW,
    H_BUS,
    H_BACK,
    H_IDENT,
    H_REGS
};
typedef struct {
    int16_t x, y, w, h, id, arg;
} hit_t;
#define HITN 64
static hit_t s_hit[HITN];
static int s_nhit;
static void hit(int x, int y, int w, int h, int id, int arg)
{
    if (s_nhit < HITN) {
        x += rl_ox;  // (the round frame: the place on the screen)
        s_hit[s_nhit++] = (hit_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)id, (int16_t)arg};
    }
}
static void hit_in(int x, int y, int w, int h, int id, int arg, int y0, int y1)
{
    const int a = y < y0 ? y0 : y, b = y + h > y1 ? y1 : y + h;
    if (b - a >= 26) {
        hit(x, a, w, b - a, id, arg);
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
    case H_DEV:
        open_dev((uint8_t)h->arg);
        break;
    case H_WIRE:
        s_scr = SC_WIRE;
        break;
    case H_LIVE:
        s_live = !s_live;
        store_set("i2c_live", (uint32_t)s_live);
        ib_live(s_live);
        if (s_live) {
            ib_scan();
        }
        break;
    case H_SCAN:
        ib_scan();
        s_scr = SC_SCAN;
        break;
    case H_VIEW:
        s_view ^= 1;
        s_scroll = 0;
        break;
    case H_BUS:
        set_bus((s_busi + 1) % s_nbus);
        break;
    case H_BACK:
        s_scr = SC_SCAN;
        s_scroll = 0;
        break;
    case H_IDENT:
        if (!s_pend) {
            ask_ident(s_dev);
            s_ident = -3;
        }
        break;
    case H_REGS:
        if (!s_pend) {
            ib_regs(s_dev);
            s_pend = 2;
            s_pend_addr = s_dev;
            s_regs_wait = true;
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

// the round frame: the top cap. BACK and the title, or the dot, the bus (a tap: the next bus) and the count of the
// devices (rl_ox = 0 here: the cap uses the whole width)
static void top_cap(const char *back_title, uint16_t dc, const char *bn, const char *cnt)
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
        mf_text_c(&I2C_NUM, W / 2, s_top_h - 12, back_title, C_TEXT, 255, 0);
    } else if (bn) {
        const int y = W * 68 / 466, room = 2 * rl_half(y - 16, 20) - 80;
        int bw = ui_text_w(&KF_NAME, bn);
        bw = bw < room ? bw : room;
        const int cw = mf_width(&I2C_NUM, cnt, 0) + 12, x0 = W / 2 - (bw + 10 + cw) / 2;
        tfd_disc((float)(x0 - 12), (float)(y - 6), 4.5f, dc, 256);
        tfd_text_l(&KF_NAME, x0, y, bn, C_TEXT, room);
        if (s_nbus > 1) {
            hit(x0 - 24, 0, bw + 40, y + 10, H_BUS, 0);
        }
        tfd_rrect(x0 + bw + 10, y - 15, cw, 19, 9, rgb(20, 60, 40), 256);
        mf_text(&I2C_NUM, x0 + bw + 16, y, cnt, C_ACC, 255, 0);
    }
    rl_ox = ox;
}
static void top_bar(const char *back_title)
{
    if (gfx_round() && back_title) {
        top_cap(back_title, 0, NULL, NULL);
        return;
    }
    tfd_rect(0, 0, s_W, s_top_h, C_PANEL, 256);
    tfd_rect(0, s_top_h, s_W, 1, C_LINE, 256);
    const int base = s_top_h / 2 + 6;
    if (back_title) {
        tfd_rrect(4, 3, 64, s_top_h - 6, 6, C_BTN, 256);
        tfd_text_c(&KF_LABEL, 36, base - 1, TX(T_BACK), C_TEXT, 60);
        hit(0, 0, 80, s_top_h + 4, H_BACK, 0);
        mf_text(&I2C_NUM, 80, base, back_title, C_TEXT, 255, 0);
        return;
    }
    // the dot (it pulses during a scan), the name of the bus (a tap: the next bus), the count, the time of a scan
    const bool busy = s_ib.state == IB_SCAN;
    const uint16_t dc = !s_open ? C_BAD : busy ? C_NEWC : s_live ? C_ACC : C_FAINT;
    tfd_disc(12, (float)s_top_h / 2, 4.5f, dc, 256);
    const char *bn = bus() ? bus()->name[s_he ? 1 : 0] : "-";
    char r[64];
    if (!s_open) {
        snprintf(r, sizeof r, "%s", "-");
    } else {
        snprintf(r, sizeof r, "0x%02X..0x%02X  %d MS", 0x08, 0x77, s_ib.scan_ms);
    }
    const int rw = s_narrow ? 0 : mf_width(&I2C_M11, r, 0) + 16;
    if (!s_narrow) {
        mf_text(&I2C_M11, s_W - 8 - (rw - 16), base - 1, r, C_DIM, 255, 0);
    }
    char c[48];
    snprintf(c, sizeof c, "%d", s_nlist);
    if (gfx_round()) {
        top_cap(NULL, dc, bn, c);
        return;
    }
    const int cw = mf_width(&I2C_NUM, c, 0);
    const int room = s_W - 24 - 8 - rw - cw - 12;
    int bw = ui_text_w(&KF_NAME, bn);
    if (bw > room) {
        bw = room;
    }
    tfd_text_l(&KF_NAME, 24, base, bn, C_TEXT, room);
    if (s_nbus > 1) {
        hit(0, 0, 24 + bw + 10, s_top_h + 2, H_BUS, 0);
    }
    // the count of the devices: a number in a round box
    const int cx = 24 + bw + 10;
    tfd_rrect(cx, base - 15, cw + 12, 19, 9, rgb(20, 60, 40), 256);
    mf_text(&I2C_NUM, cx + 6, base, c, C_ACC, 255, 0);
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

static float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}

// a text in lines of the words that fit x0..x1 (3 lines at most), at the start side; the count of the lines
static int text_lines(const kfont_t *f, int x0, int x1, int base, int lh, const char *s, uint16_t c)
{
    char line[160];
    int n = 0;
    while (*s && n < 3) {
        int k = 0, sp = -1;
        for (; s[k] && k < (int)sizeof line - 1; k++) {
            line[k] = s[k];
            line[k + 1] = 0;
            if (ui_text_w(f, line) > x1 - x0) {
                break;
            }
            if (s[k] == ' ') {
                sp = k;
            }
        }
        if (s[k] && sp > 0) {
            k = sp;  // (cut at the last space that fits)
        }
        line[k] = 0;
        tfd_text_s(f, x0, x1, base + n * lh, line, c, s_he);
        n++;
        s += k;
        while (*s == ' ') {
            s++;
        }
    }
    return n;
}

// the name of a chip in two parts: "ES8311 (THE SPEAKER CODEC)" -> "ES8311" and "THE SPEAKER CODEC"
static void name_split(const char *s, char *a, int na, char *b, int nb)
{
    const char *p = strstr(s, " (");
    if (!p) {
        snprintf(a, (size_t)na, "%s", s);
        b[0] = 0;
        return;
    }
    snprintf(a, (size_t)na, "%.*s", (int)(p - s), s);
    snprintf(b, (size_t)nb, "%s", p + 2);
    const int l = (int)strlen(b);
    if (l && b[l - 1] == ')') {
        b[l - 1] = 0;
    }
}

// the colour of an address that answered: a chip of the board, a new one, or a module
static uint16_t addr_col(uint8_t a)
{
    if (s_now - s_t_new[a] < 3000 && s_t_new[a]) {
        return C_NEWC;
    }
    return board_chip(a) ? C_BOARD : C_ACC;
}

// ---------------------------------------------------------------- SCAN: the map and the list

// the map of the 128 addresses: 8 rows (0x00 .. 0x70) of 16 (the round frame: the bigger mono face, the numbers in
// the middle of each cell)
static void draw_map(int x0, int y0, int cw, int ch)
{
    char b[8];
    const bool rd = gfx_round();
    const mfont_t *mf = rd ? &I2C_NUM : &I2C_M11;
    const int lw = rd ? 30 : 24, hh = rd ? 18 : 12, dy = rd ? 6 : 4;
    for (int c = 0; c < 16; c++) {
        snprintf(b, sizeof b, "%X", c);
        mf_text(mf, x0 + lw + c * cw + cw / 2 - (rd ? 4 : 3), y0 + (rd ? 13 : 9), b, C_FAINT, 255, 0);
    }
    for (int r = 0; r < 8; r++) {
        const int y = y0 + hh + r * ch;
        snprintf(b, sizeof b, "%02X", r * 16);
        mf_text(mf, x0, y + ch / 2 + dy, b, C_FAINT, 255, 0);
        for (int c = 0; c < 16; c++) {
            const int a = r * 16 + c, x = x0 + lw + c * cw;
            if (a < 0x08 || a > 0x77) {
                continue;
            }
            if (found((uint8_t)a)) {
                const uint16_t col = addr_col((uint8_t)a);
                tfd_rrect(x + 1, y + 1, cw - 2, ch - 2, 3, col, 256);
                snprintf(b, sizeof b, "%02X", a);
                mf_text(mf, x + cw / 2 - (rd ? 9 : 7), y + ch / 2 + dy, b, C_BG, 255, 0);
            } else {
                tfd_disc((float)x + cw / 2.0f, (float)y + ch / 2.0f, 1.3f, C_FAINT, 256);
            }
        }
    }
}

static void draw_list(int x0, int x1, int y0, int y1)
{
    const bool rd = gfx_round();
    const int rh = rd ? 56 : 40, ox = rl_ox;
    const float mx = (float)(s_nlist * rh - (y1 - y0));
    s_scroll = clampf(s_scroll, 0, mx > 0 ? mx : 0);
    char b[16];
    for (int i = (int)(s_scroll / rh); i < s_nlist; i++) {
        const int y = y0 + i * rh - (int)s_scroll;
        if (y >= y1) {
            break;
        }
        if (rd) {  // (the round frame: the row follows the chord; its own origin)
            int rx, rw;
            rl_t_row(y, rh, y0, y1, 14, &rx, &rw);
            rl_ox = rx;
            x0 = 0;
            x1 = rw;
        }
        const uint8_t a = (uint8_t)s_list[i];
        tfd_rect(x0, y, x1 - x0, rh - 1, i & 1 ? C_ROW : C_BG, 256);
        const uint16_t col = addr_col(a);
        tfd_rrect(x0 + 4, y + 4, 4, rh - 9, 2, col, 256);
        snprintf(b, sizeof b, "0x%02X", a);
        mf_text(&I2C_NUM, x0 + (rd ? 16 : 14), y + rh / 2 + 6, b, col, 255, 0);
        const char *bc = board_chip(a), *dn = bc ? bc : db_first(a);
        const int tx = x0 + (rd ? 70 + 14 : 70);
        char n1[64], n2[96];
        name_split(dn ? dn : TX(T_UNKNOWN), n1, sizeof n1, n2, sizeof n2);
        const bool fresh = s_now - s_t_new[a] < 3000 && s_t_new[a];
        tfd_text_l(&KF_NAME, tx, y + (rd ? 24 : 18), n1, dn ? C_TEXT : C_DIM, x1 - tx - 6);
        if (bc || fresh) {
            snprintf(n2, sizeof n2, "%s", bc ? TX(T_ON_BOARD) : TX(T_NEW));
        }
        tfd_text_l(&KF_LABEL, tx, y + (rd ? 47 : 34), n2, bc ? C_BOARD : fresh ? C_NEWC : C_DIM, x1 - tx - 6);
        hit_in(x0, y, x1 - x0, rh - 1, H_DEV, a, y0, y1);
    }
    rl_ox = ox;
    if (rd) {
        x0 = 0;
        x1 = s_W;
    }
    if (!s_nlist && s_ib.scans) {
        tfd_text_c(&KF_NAME, (x0 + x1) / 2, (y0 + y1) / 2 - 6, TX(T_NONE), C_DIM, x1 - x0 - 10);
        tfd_text_c(&KF_LABEL, (x0 + x1) / 2, (y0 + y1) / 2 + 16, TX(T_NONE_HINT), C_FAINT, x1 - x0 - 10);
    } else if (!s_ib.scans) {
        tfd_text_c(&KF_NAME, (x0 + x1) / 2, (y0 + y1) / 2, s_open ? TX(T_SCANNING) : TX(T_BUSFAIL),
                   s_open ? C_DIM : C_BAD, x1 - x0 - 10);
    }
}

static void draw_scan(void)
{
    const int y0 = s_top_h + 4;
    if (gfx_round()) {  // (the map: the cells as big as the band allows; the list: from the cap to the keys)
        if (s_view == 1) {
            const int cw = (2 * rl_half(y0 + 30, 14) - 30) / 16, ch = (s_H - y0 - 30) / 8;
            const int mw = 30 + 16 * cw;
            draw_map((gfx_w - mw) / 2 - rl_ox, y0 + 6, cw, ch < cw + 8 ? ch : cw + 8);
        } else {
            draw_list(0, s_W, s_top_h + 1, s_H);
        }
    } else if (s_narrow) {
        if (s_view == 1) {
            draw_map(10, y0 + 2, 18, 18);
        } else {
            draw_list(0, s_W, y0, s_H - 48);
        }
    } else {
        draw_map(6, y0, 20, s_H >= 300 ? 36 : 16);  // (the Tab5: the map fills the height)
        tfd_rect(352, s_top_h + 1, 1, s_H - s_top_h - 1, C_LINE, 256);
        draw_list(354, s_W - 106, s_top_h + 1, s_H);
    }
    if (s_ib.err == IB_E_STUCK) {
        const int w = s_narrow ? s_W - 20 : 330, kb = s_narrow ? s_kb + 34 : 32;
        tfd_rrect(10, s_H - kb, w, gfx_round() ? 32 : 26, 6, rgb(60, 24, 20), 240);
        tfd_text_c(&KF_LABEL, 10 + w / 2, s_H - kb + (gfx_round() ? 23 : 18), ib_err(IB_E_STUCK), C_BAD, w - 10);
    }
    top_bar(NULL);
    const char *l[3];
    int id[3], n = 0;
    bool hot[3];
    if (s_narrow) {
        l[n] = s_view ? TX(T_LIST) : TX(T_MAP);
        id[n] = H_VIEW;
        hot[n++] = false;
    }
    l[n] = TX(T_WIRING);
    id[n] = H_WIRE;
    hot[n++] = false;
    l[n] = s_live ? TX(T_LIVE) : TX(T_SCAN);
    id[n] = s_live ? H_LIVE : H_SCAN;
    hot[n++] = s_live;
    side_buttons(l, id, hot, n);
}

// ---------------------------------------------------------------- A DEVICE

static void draw_dev(void)
{
    const uint8_t a = s_dev;
    const bool rd = gfx_round();
    const int x1 = s_narrow ? s_W - 8 : s_W - 112;
    const int L = rd ? 24 : 17, L1 = rd ? 25 : 18;  // (the line steps: the round frame has KF_TEXT)
    const mfont_t *mf = rd ? &I2C_NUM : &I2C_M11;
    int y = s_top_h + 6 - (int)s_scroll;
    char b[160], hx[8];
    snprintf(hx, sizeof hx, "0x%02X", a);
    // the addresses
    snprintf(b, sizeof b, TEXT[T_ADDR_W][0], hx, a << 1, (a << 1) | 1, a);
    if (rd) {  // (the round frame: two centred lines)
        snprintf(b, sizeof b, "7-BIT %s   (%d)", hx, a);
        mf_text_c(mf, s_W / 2, y + 16, b, C_DIM, 255, 0);
        snprintf(b, sizeof b, "WRITE 0x%02X   READ 0x%02X", a << 1, (a << 1) | 1);
        mf_text_c(mf, s_W / 2, y + 38, b, C_DIM, 255, 0);
        y += 48;
    } else {
        mf_text(mf, 10, y + 12, b, C_DIM, 255, 0);
        y += 22;
    }
    // what it is
    const char *bc = board_chip(a);
    if (bc) {
        tfd_text_s(&KF_LABEL, 10, x1, y + 13 + (rd ? 4 : 0), TX(T_ON_BOARD), C_BOARD, s_he);
        tfd_text_l(&KF_NAME, 10, y + 34 + (rd ? 8 : 0), bc, C_TEXT, x1 - 10);
        y += rd ? 54 : 44;
    } else if (s_ident >= 0) {
        tfd_text_s(&KF_LABEL, 10, x1, y + 13 + (rd ? 4 : 0), TX(T_IDENT), C_ACC, s_he);
        tfd_text_l(&KF_NAME, 10, y + 34 + (rd ? 8 : 0), I2C_DB[s_ident].name, C_TEXT, x1 - 10);
        snprintf(b, sizeof b, "ID 0x%02X @ 0x%02X", I2C_DB[s_ident].idval, I2C_DB[s_ident].idreg);
        mf_text(mf, 10, y + 50 + (rd ? 14 : 0), b, C_DIM, 255, 0);
        y += rd ? 74 : 58;
    } else if (s_ident == -1) {
        y += 3 + L * text_lines(&KF_LABEL, 10, x1, y + 13 + (rd ? 4 : 0), L, TX(T_NO_ID), C_DIM);
    } else if (s_ident == -3) {
        tfd_text_s(&KF_LABEL, 10, x1, y + 13, TX(T_READING), C_NEWC, s_he);
        y += 20;
    }
    // the candidates of the data base
    int nc = 0;
    for (int i = 0; i < I2C_DB_N; i++) {
        if (bc || a < I2C_DB[i].lo || a > I2C_DB[i].hi || i == s_ident) {
            continue;
        }
        if (!nc++) {
            tfd_text_s(&KF_LABEL, 10, x1, y + 14 + (rd ? 4 : 0), TX(T_MAYBE), C_DIM, s_he);
            y += L1;
        }
        tfd_text_l(&KF_LABEL, 18, y + 14 + (rd ? 4 : 0), I2C_DB[i].name, C_TEXT, x1 - 18);
        y += L1;
    }
    if (!nc && !bc && s_ident < 0) {
        tfd_text_s(&KF_LABEL, 10, x1, y + 14 + (rd ? 4 : 0), TX(T_UNKNOWN), C_DIM, s_he);
        y += L1;
    }
    y += 6;
    if (i2c_db_risky(a) && !bc && s_ident < 0) {
        y += 4 + L * text_lines(&KF_LABEL, 10, x1, y + 14 + (rd ? 4 : 0), L, TX(T_RISKY), C_WARN);
    }
    // the registers
    if (s_regs_have) {
        if (s_regs_jump) {  // (their first frame: the top of the registers goes under the bar)
            s_regs_jump = false;
            s_scroll += (float)(y - s_top_h - 4);
        }
        y += 3 + L * text_lines(&KF_LABEL, 10, x1, y + 14 + (rd ? 4 : 0), L, TX(T_REGS_HINT), C_FAINT);
        // (the round frame: the cells 2 digits + 3 px wide, the table in the middle of the circle)
        const bool big = rd && 2 * rl_half(s_top_h, 4) >= 30 + 16 * 21;  // (the Watcher frame: the small face)
        if (rd && !big) {
            mf = &I2C_M11;
        }
        const int cw = big ? 2 * 9 + 3 : rd ? 2 * 7 + 3 : s_narrow ? 17 : 21, rh = rd ? 21 : 14;
        const int x0 = rd ? (gfx_w - 16 * cw + 30) / 2 - rl_ox : 34;
        const int yo = rd ? 16 : 11;  // (the base line of a row)
        for (int c = 0; c < 16; c++) {
            snprintf(b, sizeof b, "%X", c);
            mf_text(mf, x0 + c * cw + 4, y + yo - 1, b, C_FAINT, 255, 0);
        }
        y += rd ? 20 : 14;
        for (int r = 0; r < 16; r++, y += rh) {
            if (y + rh < s_top_h || y > s_H) {
                continue;
            }
            snprintf(b, sizeof b, "%02X", r * 16);
            mf_text(mf, rd ? x0 - 30 : 10, y + yo, b, C_FAINT, 255, 0);
            for (int c = 0; c < 16; c++) {
                const int i = r * 16 + c;
                const bool ok = (s_ib.regs_ok[i >> 3] >> (i & 7)) & 1;
                if (ok) {
                    snprintf(b, sizeof b, "%02X", s_ib.regs[i]);
                } else {
                    snprintf(b, sizeof b, "--");
                }
                mf_text(mf, x0 + c * cw, y + yo, b, ok ? (s_ib.regs[i] ? C_TEXT : C_DIM) : C_FAINT, 255, 0);
            }
        }
    } else if (s_regs_wait) {
        tfd_text_s(&KF_LABEL, 10, x1, y + 14, TX(T_READING), C_NEWC, s_he);
        y += 20;
    }
    // the scroll limit
    const int content = y + (int)s_scroll - s_top_h + 10;
    const float mx = (float)(content - ((s_narrow ? s_H - s_kb : s_H) - s_top_h));
    s_scroll = clampf(s_scroll, 0, mx > 0 ? mx : 0);
    top_bar(hx);
    if (!found(a) && gfx_round()) {  // (the device went away; the round frame: under the cap)
        const int tw = ui_text_w(&KF_LABEL, TX(T_GONE)) + 16;
        tfd_rrect(s_W / 2 - tw / 2, s_top_h + 4, tw, 24, 6, rgb(70, 30, 26), 256);
        tfd_text_c(&KF_LABEL, s_W / 2, s_top_h + 21, TX(T_GONE), C_BAD, tw);
    } else if (!found(a)) {
        const int tw = ui_text_w(&KF_LABEL, TX(T_GONE)) + 16;
        tfd_rrect(s_W / 2 - tw / 2 + (s_narrow ? 40 : 0), 4, tw, s_top_h - 8, 6, rgb(70, 30, 26), 256);
        tfd_text_c(&KF_LABEL, s_W / 2 + (s_narrow ? 40 : 0), s_top_h / 2 + 5, TX(T_GONE), C_BAD, tw);
    }
    if (bc) {  // (a chip of the board: the app knows it, no IDENTIFY)
        const char *l1[1] = {TX(T_READREGS)};
        const int id1[1] = {H_REGS};
        const bool hot1[1] = {false};
        side_buttons(l1, id1, hot1, 1);
    } else {
        const char *l[2] = {TX(T_IDENTIFY), TX(T_READREGS)};
        const int id[2] = {H_IDENT, H_REGS};
        const bool hot[2] = {false, false};
        side_buttons(l, id, hot, 2);
    }
}

// ---------------------------------------------------------------- THE WIRING

static void draw_wire(void)
{
    const rfp_i2c_t *bs = bus();
    if (!bs) {
        return;
    }
    static const sk_pin_t PINS[4] = {
            {"SDA", RFP_PAD_SDA}, {"SCL", RFP_PAD_SCL}, {"VCC", RFP_PAD_3V3}, {"GND", RFP_PAD_GND}};
    const sk_dev_t dev = {s_he ? "מודול I2C" : "AN I2C MODULE", PINS, 4};
    const rfp_conn_t *k = &bs->conn;
    const bool busc = k->shape == RFP_SHAPE_BUS30;
    if (gfx_round()) {  // (the title in the top cap)
        top_cap(NULL, 0, NULL, NULL);
        tfd_text_c(&KF_NAME, s_W / 2, s_top_h - 16, TX(T_CONNECT), C_TEXT, 2 * rl_half(s_top_h - 30, 20));
    } else {
        tfd_text_s(&KF_LABEL, 8, BAR ? 290 : s_W - 8, 17, TX(T_CONNECT), C_TEXT, s_he);
    }
    const char *n1 = TX(T_PULLUPS), *n2 = bs->shared ? TX(T_SHARED) : "";
    if (BAR) {
        sk_draw(k, &dev, &I2C_M11, 6, 30, s_W - 110, s_H - 22, s_he);
        const int tw = ui_text_w(&KF_LABEL, n1) + 12;
        tfd_rrect(4, s_H - 19, tw < s_W - 116 ? tw : s_W - 116, 18, 5, C_BG, 235);
        tfd_text_s(&KF_LABEL, 10, s_W - 112, s_H - 5, n1, C_DIM, s_he);
    } else if (busc) {
        sk_draw(k, &dev, &I2C_M11, 0, s_top_h - 2, s_W, s_H - 4, s_he);
        tfd_text_c(&KF_LABEL, 222, 146, TX(T_PULLUPS_S), C_DIM, 186);  // (short: under the view text)
        if (n2[0]) {
            tfd_text_c(&KF_LABEL, 222, 162, TX(T_SHARED_S), C_WARN, 186);
        }
    } else {
#if BOARD_UI_TABLET
        // the Tab5: the sketch 1.5 times bigger (rfp_sketch.inc) at the left of the keys, the words in the bigger face
        sk_draw(k, &dev, &I2C_NUM, 4, 40, s_W - 108, s_H - 90, s_he);
        tfd_text_c(&KF_NAME, (s_W - 104) / 2, s_H - 52, TX(T_PULLUPS_S), C_DIM, s_W - 116);
        if (n2[0]) {
            tfd_text_c(&KF_NAME, (s_W - 104) / 2, s_H - 26, TX(T_SHARED_S), C_WARN, s_W - 116);
        }
#else
        if (gfx_round()) {  // (the sketch in the band, the two notes under it)
            sk_draw(k, &dev, &I2C_NUM, 0, s_top_h + 4, s_W, s_H - (n2[0] ? 52 : 28), s_he);
            tfd_text_c(&KF_LABEL, s_W / 2, s_H - (n2[0] ? 30 : 6), TX(T_PULLUPS_S), C_DIM, s_W - 12);
            if (n2[0]) {
                tfd_text_c(&KF_LABEL, s_W / 2, s_H - 6, TX(T_SHARED_S), C_WARN, s_W - 12);
            }
        } else {
            sk_draw(k, &dev, &I2C_M11, 4, 24, s_W - 4, s_H - 84, s_he);
            tfd_text_c(&KF_LABEL, s_W / 2, s_H - 66, TX(T_PULLUPS_S), C_DIM, s_W - 12);
            if (n2[0]) {
                tfd_text_c(&KF_LABEL, s_W / 2, s_H - 51, TX(T_SHARED_S), C_WARN, s_W - 12);
            }
        }
#endif
    }
    if (busc && s_narrow && !gfx_round()) {
        button(130, s_H - 70, 184, 30, TX(T_BACK), false, H_BACK, 0);
        button(130, s_H - 36, 184, 30, TX(T_SCAN), true, H_SCAN, 0);
    } else {
        const char *l[2] = {TX(T_BACK), TX(T_SCAN)};
        const int id[2] = {H_BACK, H_SCAN};
        const bool hot[2] = {false, true};
        side_buttons(l, id, hot, 2);
    }
}

// ---------------------------------------------------------------- render

static void render(int64_t now)
{
    (void)now;
    layout();
    tfd_screen();
    tfd_noclip();
    s_nhit = 0;
    if (gfx_round()) {
        gfx_fill(C_BG);  // (the whole circle: the box below covers only its middle)
    }
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    switch (s_scr) {
    case SC_DEV:
        draw_dev();
        break;
    case SC_WIRE:
        draw_wire();
        break;
    default:
        draw_scan();
        break;
    }
    toast_draw();
}

// ---------------------------------------------------------------- the menu icon
#include "rndlay/rl_origin_end.h"  // (the menu icon draws at its own points)
#include "tficon/tficon.h"
// the menu icon: the two lines of a bus with three chips; a scan pulse runs along the bus and each chip lights when
// the pulse finds it
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0x5ad67d), tfi_hex(0x0f6a33));
    float u = s / 100.0f, ph = tfi_ph(2);
    int lh = (int)(3 * u) > 1 ? (int)(3 * u) : 1;
    tfd_rect(x + (int)(12 * u), y + (int)(24 * u), (int)(76 * u), lh, tfi_hex(0xffe066), 256);
    tfd_rect(x + (int)(12 * u), y + (int)(32 * u), (int)(76 * u), lh, tfi_hex(0x7de8ff), 256);
    float px = x + (12 + ph * 76) * u;
    tfi_glow(px, y + 28 * u, 10 * u, rgb(255, 255, 255), 230);
    for (int k = 0; k < 3; k++) {
        float chx = x + (22 + k * 22) * u;
        float found = tfi_step(0, 0.04f, ph - (chx - x - 12 * u) / (76 * u)) * (1 - tfi_step(0.85f, 1.0f, ph));
        tfd_rect((int)(chx + 2 * u), y + (int)(24 * u), (int)(1.8f * u) > 1 ? (int)(1.8f * u) : 1, (int)(24 * u),
                 tfi_hex(0xffe066), 256);
        tfd_rect((int)(chx + 9 * u), y + (int)(32 * u), (int)(1.8f * u) > 1 ? (int)(1.8f * u) : 1, (int)(16 * u),
                 tfi_hex(0x7de8ff), 256);
        for (int p = 0; p < 3; p++) {
            tfd_rect((int)(chx - 1 * u), y + (int)((52 + p * 7) * u), (int)(18 * u),
                     (int)(2 * u) > 1 ? (int)(2 * u) : 1, tfi_hex(0xd0d4dc), 256);
        }
        tfi_drop((int)(chx), y + (int)(48 * u), (int)(16 * u), (int)(24 * u), (int)(3 * u), (int)(2 * u), 90);
        tfi_rbox((int)(chx), y + (int)(48 * u), (int)(16 * u), (int)(24 * u), (int)(3 * u), tfi_hex(0x4a4e5c),
                 tfi_hex(0x1a1c24), 256);
        uint16_t led = tfi_lerp(tfi_hex(0x2a4a32), tfi_hex(0x6dff8a), (int)(256 * found));
        if (found > 0.05f) {
            tfi_glow(chx + 8 * u, y + 56 * u, 9 * u, tfi_hex(0x6dff8a), (int)(200 * found));
        }
        tfi_disc(chx + 8 * u, y + 56 * u, 3 * u, led, led, 256);
    }
    tfi_end();
}
