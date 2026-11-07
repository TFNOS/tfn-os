// RFPULSE: THE WIRING SKETCH of a device on the connector of the board (rfp_board.h), for the apps that use the pins
// (SERIAL MONITOR, I2C SCANNER). Each app includes this file (static functions: each app file has its own copy). The
// app gives the connector, a device (a box with up to 4 pins, each pin with the role of the pad that it goes to) and
// the faces; the sketch draws the connector as the user sees it (the 3.49 pads, the M5-Bus, the J2 and J9 sockets),
// the device, and one coloured wire from each pin to its pad. (IR DECODER and RF DECODER have their own sketches with
// two modules; this one has the same look.)
// Before the include: tfdraw.h, mfont.h, kfont.h, uikit.h, rfp_board.h, and BOARD_BAR_SCREEN.
#include <stdio.h>

typedef struct {
    const char *name;  // the name on the device ("TX", "SDA", "VCC")
    uint8_t role;      // the role of the pad that its wire goes to (RFP_PAD_*)
} sk_pin_t;

typedef struct {
    const char *name;  // the name of the device ("A SERIAL DEVICE", "AN I2C MODULE")
    const sk_pin_t *pins;
    int npins;  // 4 at most
} sk_dev_t;

#define SK_BG    tfd_rgb(6, 9, 14)
#define SK_PCB   tfd_rgb(24, 58, 112)
#define SK_PCB_E tfd_rgb(48, 92, 160)
#define SK_PAD   tfd_rgb(214, 178, 96)
#define SK_DIM   tfd_rgb(110, 125, 145)
#define SK_AXIS  tfd_rgb(150, 165, 185)
#define SK_TEXT  tfd_rgb(225, 232, 240)
#define SK_SOCK  tfd_rgb(24, 24, 28)
#define SK_MOD   tfd_rgb(28, 30, 36)

// The size of the sketch: the Tab5 (its frame of 640 x 360 has px of 0.17 mm) draws the socket, the device, the wires
// and the words 1.5 times bigger (SKV: a length, SKF: a float length, SK_KF: the face of the words); the others: 1.
#if BOARD_UI_TABLET
#define SK_N  3
#define SK_D  2
#define SK_KF (&KF_NAME)
#else
#define SK_N  1
#define SK_D  1
#define SK_KF (&KF_LABEL)
#endif
#define SKV(v) ((v) * SK_N / SK_D)
#define SKF(v) ((v) * (float)SK_N / SK_D)

static uint16_t sk_role_col(int role)
{
    switch (role) {
    case RFP_PAD_3V3:
        return tfd_rgb(255, 84, 70);
    case RFP_PAD_GND:
        return tfd_rgb(170, 178, 190);
    case RFP_PAD_DATA:
    case RFP_PAD_RX:
        return tfd_rgb(255, 214, 64);
    case RFP_PAD_TX:
        return tfd_rgb(226, 120, 255);
    case RFP_PAD_SDA:
        return tfd_rgb(80, 220, 140);
    case RFP_PAD_SCL:
        return tfd_rgb(80, 190, 255);
    default:
        return SK_PAD;
    }
}

static int sk_find(const rfp_conn_t *k, int role)
{
    for (int i = 0; i < k->rows * k->cols; i++) {
        if (k->pads[i].role == role) {
            return i;
        }
    }
    return -1;
}

static void sk_seg(int x0, int y0, int x1, int y1, uint16_t c)
{
    tfd_line((float)x0, (float)y0, (float)x1, (float)y1, SKF(2.6f), c, 256);
}

// The device: a dark box, its name, and its pins on one long side (pins_up: at the top). px[] = the x of each pin,
// *pin_y = the y of the pin ends.
static void sk_device(const sk_dev_t *d, const mfont_t *fax, int x, int y, int w, int h, bool pins_up, int px[4],
                      int *pin_y)
{
    tfd_rrect(x, y, w, h, 5, SK_MOD, 256);
    tfd_rrect(x + 2, y + 2, w - 4, h - 4, 4, tfd_rgb(44, 48, 58), 140);
    const int n = d->npins, pitch = n > 1 ? (w - SKV(28)) / (n - 1) : 0;
    *pin_y = pins_up ? y - SKV(9) : y + h + SKV(9);
    for (int i = 0; i < n; i++) {
        px[i] = x + SKV(14) + i * pitch;
        tfd_rect(px[i] - SKV(2), pins_up ? y - SKV(9) : y + h, SKV(4), SKV(9), tfd_rgb(200, 200, 210), 256);
        const int tw = mf_width(fax, d->pins[i].name, 0);
        mf_text(fax, px[i] - tw / 2, pins_up ? y + SKV(13) : y + h - SKV(5), d->pins[i].name,
                sk_role_col(d->pins[i].role), 255, 0);
    }
    tfd_text_c(SK_KF, x + w / 2, pins_up ? y + h - SKV(6) : y + SKV(16), d->name, SK_TEXT, w - 8);
}

typedef struct {
    int pad, x, y;
    uint16_t c;
} sk_wire_t;

// the wires of the device pins, in the order of the x of their pins
static int sk_wires(const rfp_conn_t *k, const sk_dev_t *d, const int px[4], int py, sk_wire_t *w)
{
    int n = 0;
    for (int i = 0; i < d->npins; i++) {
        const int pad = sk_find(k, d->pins[i].role);
        if (pad < 0) {
            continue;
        }
        w[n].pad = pad;
        w[n].x = px[i];
        w[n].y = py;
        w[n].c = sk_role_col(d->pins[i].role);
        n++;
    }
    return n;
}

static void sk_label(const mfont_t *fax, int cx, int base, const char *s, uint16_t c, bool box)
{
    const int tw = mf_width(fax, s, 0);
    if (box) {
        tfd_rect(cx - tw / 2 - 2, base - SKV(9), tw + 4, SKV(11), SK_SOCK, 256);
    }
    mf_text(fax, cx - tw / 2, base, s, c, 255, 0);
}

// The sketch in the box x0..x1, y0..y1 (BAR: the 3.49 frame; else the 320 x 240 frame). he: the Hebrew texts.
static void sk_draw(const rfp_conn_t *k, const sk_dev_t *d, const mfont_t *fax, int x0, int y0, int x1, int y1, bool he)
{
    const int n = k->rows * k->cols;
    if (k->shape == RFP_SHAPE_NONE || !k->pads) {
        tfd_text_c(&KF_NAME, (x0 + x1) / 2, (y0 + y1) / 2 - 6, k->name[he ? 1 : 0], SK_TEXT, x1 - x0 - 10);
        tfd_text_c(&KF_LABEL, (x0 + x1) / 2, (y0 + y1) / 2 + 16, k->where[he ? 1 : 0], SK_DIM, x1 - x0 - 10);
        return;
    }
    int px[32], py[32], dpx[4], dpy;
    sk_wire_t w[4];
    int nw;
    if (k->shape == RFP_SHAPE_BUS30) {
        // the M5-Bus as the user sees it (the back, the USB-C at the right): two columns, the odd pins at the left,
        // pin 1 at the top left; the device at the right
        const int pitch = (y1 - y0 - 20) / k->rows, xl = x0 + 36, xr = x0 + 78, yt = y0 + 12;
        const int bx0 = x0 + 20, bx1 = x0 + 94, by0 = yt - pitch / 2 - 2,
                  by1 = yt + (k->rows - 1) * pitch + pitch / 2 + 2;
        tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, 4, SK_SOCK, 256);
        float m[6] = {(float)bx0 - 2, (float)by0 - 2, (float)bx0 + 8, (float)by0 - 2, (float)bx0 - 2, (float)by0 + 8};
        tfd_poly(m, 3, SK_TEXT, 256);
        for (int i = 0; i < n && i < 32; i++) {
            px[i] = (i & 1) ? xr : xl;
            py[i] = yt + (i / 2) * pitch;
        }
        const int dx = x0 + 128, dw = x1 - dx - 6;
        sk_device(d, fax, dx, y0 + 6, dw, 46, false, dpx, &dpy);
        nw = sk_wires(k, d, dpx, dpy, w);
        for (int j = 0; j < nw; j++) {
            const int i = w[j].pad, xlane = bx1 + 6 + j * 5, ylane = dpy + 6 + j * 5;
            int y = py[i];
            if (!(i & 1)) {  // a LEFT pin: to the middle, down into the gap, then right
                sk_seg(px[i], y, (xl + xr) / 2, y, w[j].c);
                sk_seg((xl + xr) / 2, y, (xl + xr) / 2, y + pitch / 2, w[j].c);
                y += pitch / 2;
                sk_seg((xl + xr) / 2, y, xlane, y, w[j].c);
            } else {
                sk_seg(px[i], y, xlane, y, w[j].c);
            }
            sk_seg(xlane, y, xlane, ylane, w[j].c);
            sk_seg(xlane, ylane, w[j].x, ylane, w[j].c);
            sk_seg(w[j].x, ylane, w[j].x, w[j].y, w[j].c);
        }
        for (int i = 0; i < n && i < 32; i++) {  // the pads over the wires
            const rfp_pad_t *p = &k->pads[i];
            const bool hot = p->role != RFP_PAD_OTHER;
            if (hot) {
                tfd_disc((float)px[i], (float)py[i], 6.5f, sk_role_col(p->role), 120);
            }
            tfd_disc((float)px[i], (float)py[i], hot ? 4.6f : 3.8f, hot ? sk_role_col(p->role) : SK_PAD, 256);
            tfd_disc((float)px[i], (float)py[i], 1.6f, tfd_rgb(10, 10, 12), 256);
            const int tw = mf_width(fax, p->label, 0);
            const int lx = (i & 1) ? xr + 9 : xl - 9 - tw;
            if (i & 1) {
                tfd_rect(lx - 2, py[i] - 6, tw + 4, 12, SK_BG, 256);
            }
            mf_text(fax, lx, py[i] + 4, p->label, hot ? sk_role_col(p->role) : SK_AXIS, 255, 0);
        }
        tfd_text_c(&KF_LABEL, (dx + x1) / 2, dpy + 6 + 4 * 5 + 14, k->where[he ? 1 : 0], SK_DIM, x1 - dx);
        return;
    }
    // the 3.49 pads (silk) or a socket of 2 rows (J2, J9)
    const bool silk = k->shape == RFP_SHAPE_349;
    int bx0, by0, bx1, by1;
    if (silk) {
        bx0 = x0;
        by0 = y0;
        bx1 = x0 + 300;
        by1 = by0 + 96;
        tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, 8, SK_PCB, 256);
        tfd_rrect(bx0, by0, bx1 - bx0, 3, 2, SK_PCB_E, 256);
        const int pitch = 24, px0 = bx0 + 20;
        for (int i = 0; i < n; i++) {
            px[i] = px0 + (i % k->cols) * pitch;
            py[i] = i < k->cols ? by0 + 14 : by1 - 14;
        }
        tfd_rrect(bx1 - 10, by0 + (by1 - by0) / 2 - 16, 16, 32, 5, tfd_rgb(190, 196, 206), 256);
        tfd_rrect(bx1 - 7, by0 + (by1 - by0) / 2 - 10, 10, 20, 3, tfd_rgb(60, 64, 72), 256);
        sk_label(fax, bx1 - 30, by0 + (by1 - by0) / 2 + 4, "USB-C", SK_TEXT, false);
        tfd_text_c(&KF_LABEL, px0 + 5 * pitch, by0 + (by1 - by0) / 2 + 5, he ? "גב הלוח" : "THE BACK",
                   tfd_rgb(120, 150, 200), 140);
    } else {
        const int pitch = SKV(32), ww = pitch * k->cols;
        bx0 = x0 + 6;
        bx1 = bx0 + ww + SKV(16);
        by0 = y0 + SKV(18);  // (room over the socket for the wires of its top row)
        by1 = by0 + 2 * pitch + SKV(6);
        tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, SKV(4), SK_SOCK, 256);
        for (int i = 0; i < n; i++) {
            px[i] = bx0 + SKV(8) + pitch / 2 + (i % k->cols) * pitch;
            py[i] = by0 + SKV(3) + pitch / 2 + (i / k->cols) * pitch;
        }
        float m[6] = {(float)bx0 - 2, (float)by0 - 2, (float)bx0 + SKV(8),
                      (float)by0 - 2, (float)bx0 - 2, (float)by0 + SKV(8)};
        tfd_poly(m, 3, SK_TEXT, 256);
    }
    // the device: at the right of the connector, its pins at the bottom; the wires go under both
    const int dx = bx1 + SKV(18), dw = x1 - dx - 4 < SKV(220) ? x1 - dx - 4 : SKV(220);
    sk_device(d, fax, dx, by0 + 2, dw, SKV(50), false, dpx, &dpy);
    nw = sk_wires(k, d, dpx, dpy, w);
    const int half = (px[1] - px[0]) / 2;
    for (int j = 0; j < nw; j++) {
        const int i = w[j].pad, lane = by1 + SKV(7) + j * SKV(5);
        int xa = px[i], ya = py[i] + SKV(7);
        if (i < k->cols && silk) {  // the top row of the 3.49: half a pad step right, then down in the gap
            xa += half;
            sk_seg(px[i], py[i], xa, py[i], w[j].c);
            ya = py[i];
        } else if (i < k->cols) {  // the top row of a socket: up over the socket, then out at the left
            const int yt = by0 - SKV(5) - j * SKV(3);
            xa = bx0 - SKV(6) - j * SKV(4);
            sk_seg(px[i], py[i], px[i], yt, w[j].c);
            sk_seg(px[i], yt, xa, yt, w[j].c);
            ya = yt;
        }
        sk_seg(xa, ya, xa, lane, w[j].c);
        sk_seg(xa, lane, w[j].x, lane, w[j].c);
        sk_seg(w[j].x, lane, w[j].x, w[j].y, w[j].c);
        tfd_disc((float)xa, (float)lane, SKF(2.0f), w[j].c, 256);
        tfd_disc((float)w[j].x, (float)lane, SKF(2.0f), w[j].c, 256);
    }
    for (int i = 0; i < n; i++) {  // the pads over the wires
        const rfp_pad_t *p = &k->pads[i];
        const bool hot = p->role != RFP_PAD_OTHER;
        if (hot) {
            tfd_disc((float)px[i], (float)py[i], SKF(8.5f), sk_role_col(p->role), 110);
        }
        tfd_disc((float)px[i], (float)py[i], hot ? SKF(6.0f) : SKF(5.0f), hot ? sk_role_col(p->role) : SK_PAD, 256);
        tfd_disc((float)px[i], (float)py[i], SKF(2.2f), silk ? SK_PCB : tfd_rgb(10, 10, 12), 256);
        const bool top = i < k->cols;
        const int ly = silk ? (top ? py[i] + 19 : py[i] - 11) : (top ? py[i] - SKV(12) : py[i] + SKV(20));
        sk_label(fax, px[i], ly, p->label, hot ? sk_role_col(p->role) : SK_AXIS, !silk);
    }
    if (!silk) {
        tfd_text_c(SK_KF, (x0 + x1) / 2, by1 + SKV(7 + 4 * 5 + 16), k->where[he ? 1 : 0], SK_DIM, x1 - x0 - 10);
    }
}
