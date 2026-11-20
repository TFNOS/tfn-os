// RF DECODER: THE SCREENS (they only read the state of app_rfdec.c).
// The task: the app task (render and input of app_rfdec.c, which includes this file).
// The memory: the file statics of this part (the layout, the touch targets); no heap.
//
//   THE WIRING  the connector of this board as the user sees it (rfp_board.h: the pads and their roles), the receiver
//               module (VCC, DATA, DATA, GND), three coloured wires, the live state of the pin, CONNECTED and DEMO
//   LIVE        the raw pulses of the last 120 ms, and the list of the codes (the newest first)
//   A CODE      the pulses of its last packet with the bits, the value (hex, decimal, binary, tri-state), the timing

#define C_BG    rgb(6, 9, 14)
#define C_PANEL rgb(12, 18, 28)
#define C_LINE  rgb(30, 42, 58)
#define C_TEXT  rgb(225, 232, 240)
#define C_DIM   rgb(110, 125, 145)
#define C_AXIS  rgb(150, 165, 185)
#define C_PCB   rgb(24, 58, 112)
#define C_PCB_E rgb(48, 92, 160)
#define C_PAD   rgb(214, 178, 96)
#define C_MOD   rgb(32, 112, 52)
#define C_RED   rgb(255, 84, 70)
#define C_GNDW  rgb(170, 178, 190)
#define C_DATAW rgb(255, 214, 64)
#define C_OK    rgb(70, 220, 120)
#define C_WARN  rgb(255, 160, 60)
#define C_BTN   rgb(24, 34, 50)
#define C_GO    rgb(36, 150, 80)

static uint16_t role_col(int role)
{
    return role == RFP_PAD_3V3 ? C_RED : role == RFP_PAD_GND ? C_GNDW : role == RFP_PAD_DATA ? C_DATAW : C_PAD;
}

static void text_c(const mfont_t *f, int cx, int base, const char *s, uint16_t c)
{
    mf_text_c(f, cx, base, s, c, 255, 0);
}

// a polyline wire: down (or up) from the pad to a lane, along the lane, then to the pin of the module
static void wire(int x0, int y0, int lane, int x1, int y1, uint16_t c)
{
    tfd_line((float)x0, (float)y0, (float)x0, (float)lane, 3, c, 256);
    tfd_line((float)x0, (float)lane, (float)x1, (float)lane, 3, c, 256);
    tfd_line((float)x1, (float)lane, (float)x1, (float)y1, 3, c, 256);
    tfd_disc((float)x0, (float)lane, 2.2f, c, 256);
    tfd_disc((float)x1, (float)lane, 2.2f, c, 256);
}

// The receiver module (the MX-05 family): a green board, the coil, the chip; 4 pins VCC, DATA, DATA, GND (left to right,
// the parts toward the user). pins_up: the pins at the top (the sketch of the 320 x 240 frame). The x of each pin.
static void draw_module(int x, int y, int w, int h, bool pins_up, int px[4], int *pin_y)
{
    tfd_rrect(x, y, w, h, 4, C_MOD, 256);
    tfd_rrect(x + 2, y + 2, w - 4, h - 4, 3, rgb(40, 132, 62), 120);
    // the coil (a copper spring) and the chip
    const int cy = pins_up ? y + h - 20 : y + 12;
    for (int i = 0; i < 7; i++) {
        tfd_rect(x + 8 + i * 4, cy, 2, 10, rgb(214, 140, 60), 256);
    }
    tfd_rect(x + 8, cy + 4, 28, 2, rgb(214, 140, 60), 200);
    tfd_rrect(x + w - 30, cy, 20, 12, 2, rgb(20, 22, 26), 256);
    static const char *const PN[4] = {"VCC", "DATA", "DATA", "GND"};
    const int pitch = (w - 32) / 3;
    *pin_y = pins_up ? y - 9 : y + h + 9;
    const int ly = pins_up ? y + 12 : y + h - 6;
    for (int i = 0; i < 4; i++) {
        px[i] = x + 16 + i * pitch;
        int py0 = pins_up ? y - 9 : y + h;
        tfd_rect(px[i] - 2, py0, 4, 9, rgb(200, 200, 210), 256);
    }
    // the labels: VCC, one DATA over a bracket of the two DATA pins (they are one line: use one), GND
    const int bx = pins_up ? y + 3 : y + h - 3;
    tfd_rect(px[1], bx, px[2] - px[1] + 1, 1, C_DATAW, 220);
    tfd_rect(px[1], pins_up ? y : bx, 1, 3, C_DATAW, 220);
    tfd_rect(px[2], pins_up ? y : bx, 1, 3, C_DATAW, 220);
    for (int i = 0; i < 3; i++) {
        int cx = i == 0 ? px[0] : i == 1 ? (px[1] + px[2]) / 2 : px[3];
        const char *t = i == 0 ? PN[0] : i == 1 ? PN[1] : PN[3];
        int tw = mf_width(&RF_AX, t, 0);
        int yy = pins_up ? ly + 4 : ly - 3;
        tfd_rect(cx - tw / 2 - 2, yy - 9, tw + 4, 11, rgb(16, 60, 28), 220);
        text_c(&RF_AX, cx, yy, t, i == 0 ? C_RED : i == 2 ? C_GNDW : C_DATAW);
    }
}

// The pads of a connector in a box: the centre of each pad (rows * cols)
typedef struct {
    int x[32], y[32];
} padpos_t;

// THE WIRING: the legend texts of the three wires ("<pad> > VCC", "<pad> > GND", "<pad> > DATA")
static void wire_legend_text(const rfp_conn_t *k, char leg[3][32])
{
    for (int i = 0; i < k->rows * k->cols; i++) {
        const rfp_pad_t *p = &k->pads[i];
        if (p->role == RFP_PAD_3V3) {
            snprintf(leg[0], 32, "%s > VCC", p->label);
        }
        if (p->role == RFP_PAD_GND) {
            snprintf(leg[1], 32, "%s > GND", p->label);
        }
        if (p->role == RFP_PAD_DATA) {
            snprintf(leg[2], 32, "%s > DATA", p->label);
        }
    }
}

// THE WIRING of the M5-BUS (the 320 x 240 frame): the pads, the module, the wires and the legend
static void wire_bus30(const rfp_conn_t *k, char leg[3][32])
{
    // THE M5-BUS as the user sees it: the BACK of the CoreS3, the USB-C at the right. Two
    // columns: LEFT the odd pins, RIGHT the even pins, pin 1 (GND) at the top left. The module at the right; a wire
    // of a LEFT pin crosses the RIGHT column in the gap between two pads
    const int pitch = (s_H - 54) / k->rows, x0l = 42, x0r = 84, y0 = 46;
    const int bx0 = 26, bx1 = 100, by0 = y0 - pitch / 2 - 2, by1 = y0 + (k->rows - 1) * pitch + pitch / 2 + 2;
    mf_text_c(&RF_AX, (bx0 + bx1) / 2, by1 + 11, "M5-BUS", C_DIM, 255, 0);
    tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, 4, rgb(24, 24, 28), 256);
    float m[6] = {(float)bx0 - 2, (float)by0 - 2, (float)bx0 + 8, (float)by0 - 2, (float)bx0 - 2, (float)by0 + 8};
    tfd_poly(m, 3, C_TEXT, 256);
    int px[32], py[32], vcc = -1, gnd = -1, dat = -1;
    for (int i = 0; i < k->rows * k->cols && i < 32; i++) {
        const rfp_pad_t *p = &k->pads[i];
        px[i] = (i & 1) ? x0r : x0l;  // (pads[] row by row: (1, 2), (3, 4) ...; odd at the left)
        py[i] = y0 + (i / 2) * pitch;
        bool hot = p->role != RFP_PAD_OTHER;
        if (hot) {
            tfd_disc((float)px[i], (float)py[i], 6.5f, role_col(p->role), 120);
        }
        tfd_disc((float)px[i], (float)py[i], hot ? 4.6f : 3.8f, hot ? role_col(p->role) : C_PAD, 256);
        tfd_disc((float)px[i], (float)py[i], 1.6f, rgb(10, 10, 12), 256);
        int tw = mf_width(&RF_AX, p->label, 0);
        mf_text(&RF_AX, (i & 1) ? x0r + 10 : x0l - 10 - tw, py[i] + 4, p->label, hot ? role_col(p->role) : C_AXIS, 255,
                0);
        if (p->role == RFP_PAD_3V3) {
            vcc = i;
        }
        if (p->role == RFP_PAD_GND) {
            gnd = i;
        }
        if (p->role == RFP_PAD_DATA) {
            dat = i;
        }
    }
    int mx[4], my;
    draw_module(150, 40, 128, 46, false, mx, &my);
    tfd_text_c(&KF_LABEL, 214, 127, TX(T_TWO_DATA), C_DIM, 160);
    const int idx[3] = {vcc, dat, gnd}, pin[3] = {0, 1, 3};
    const uint16_t col[3] = {C_RED, C_DATAW, C_GNDW};
    for (int w = 0; w < 3; w++) {
        int i = idx[w];
        if (i < 0) {
            continue;
        }
        const int xl = 116 + w * 8, yl = my + 6 + w * 6;
        int y = py[i];
        if (!(i & 1)) {  // a LEFT pin: right to the middle, down into the gap, then across
            tfd_line((float)px[i], (float)y, (float)(x0l + x0r) / 2, (float)y, 2.5f, col[w], 256);
            tfd_line((float)(x0l + x0r) / 2, (float)y, (float)(x0l + x0r) / 2, (float)y + pitch / 2.0f, 2.5f, col[w],
                     256);
            y += pitch / 2;
            tfd_line((float)(x0l + x0r) / 2, (float)y, (float)xl, (float)y, 2.5f, col[w], 256);
        } else {
            tfd_line((float)px[i], (float)y, (float)xl, (float)y, 2.5f, col[w], 256);
        }
        tfd_line((float)xl, (float)y, (float)xl, (float)yl, 2.5f, col[w], 256);
        tfd_line((float)xl, (float)yl, (float)mx[pin[w]], (float)yl, 2.5f, col[w], 256);
        tfd_line((float)mx[pin[w]], (float)yl, (float)mx[pin[w]], (float)my, 2.5f, col[w], 256);
    }
    for (int w = 0; w < 3; w++) {  // (the label of a RIGHT pin again, over its wire)
        int i = idx[w];
        if (i < 0 || !(i & 1)) {
            continue;
        }
        int tw = mf_width(&RF_AX, k->pads[i].label, 0);
        tfd_rect(x0r + 8, py[i] - 6, tw + 4, 12, C_BG, 256);
        mf_text(&RF_AX, x0r + 10, py[i] + 4, k->pads[i].label, col[w], 255, 0);
    }
    const uint16_t lc[3] = {C_RED, C_GNDW, C_DATAW};
    for (int i = 0; i < 3; i++) {
        int lx = 136, lyy = 166 + i * 20;
        tfd_disc((float)lx, (float)lyy - 4, 4, lc[i], 256);
        mf_text(&RF_SM, lx + 8, lyy, leg[i], C_TEXT, 255, 0);
    }
    tfd_text_c(&KF_LABEL, 214, 141, k->where[s_he ? 1 : 0], C_DIM, 176);  // (the view: where the USB-C is)
}

// THE WIRING of the 3.49: the back of the board (the PCB, the two rows of pads at the battery end, the card slot, the
// USB-C port). The centre of each pad goes into pp, the box of the board into b (x0, y0, x1, y1).
static void wire_board_349(const rfp_conn_t *k, int n, padpos_t *pp, int b[4])
{
    const int bx0 = 6, by0 = 26, bx1 = BAR ? 384 : s_W - 10, by1 = by0 + (BAR ? 98 : 104);
    b[0] = bx0;
    b[1] = by0;
    b[2] = bx1;
    b[3] = by1;
    tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, 8, C_PCB, 256);
    tfd_rrect(bx0, by0, bx1 - bx0, 3, 2, C_PCB_E, 256);
    const int pitch = BAR ? 24 : 25, px0 = bx0 + 22;
    for (int i = 0; i < n; i++) {
        pp->x[i] = px0 + (i % k->cols) * pitch;
        pp->y[i] = i < k->cols ? by0 + 14 : by1 - 14;
    }
    // the card slot and the USB-C port (the landmarks), and the side of the board
    const int sx = px0 + 11 * pitch + 10;
    tfd_rrect(sx, by0 + (by1 - by0) / 2 - 4, bx1 - sx - 34, (by1 - by0) / 2 - 4, 3, rgb(150, 156, 166), 230);
    text_c(&RF_AX, sx + (bx1 - sx - 34) / 2, by0 + (by1 - by0) * 3 / 4 + 4, "SD", rgb(40, 44, 52));
    tfd_rrect(bx1 - 10, by0 + (by1 - by0) / 2 - 16, 16, 32, 5, rgb(190, 196, 206), 256);
    tfd_rrect(bx1 - 7, by0 + (by1 - by0) / 2 - 10, 10, 20, 3, rgb(60, 64, 72), 256);
    text_c(&RF_AX, bx1 - 30, by0 + (by1 - by0) / 2 - 16, "USB-C", C_TEXT);
    text_c(&RF_SM, (px0 + sx) / 2 + 40, by0 + (by1 - by0) / 2 + 4, s_he ? "גב הלוח" : "THE BACK", rgb(120, 150, 200));
}

// THE WIRING of an other connector: the box of the pads (rows x cols) and the mark of pin 1. As wire_board_349.
static void wire_board_conn(const rfp_conn_t *k, int n, padpos_t *pp, int b[4])
{
    const int pitch = k->cols > 10 ? (s_W - 40) / k->cols : 34;
    const int w = pitch * k->cols;
    const int bx0 = (s_W - w) / 2 - 8, bx1 = bx0 + w + 16, by0 = BAR ? 40 : 46, by1 = by0 + 2 * pitch + 6;
    b[0] = bx0;
    b[1] = by0;
    b[2] = bx1;
    b[3] = by1;
    tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, 4, rgb(24, 24, 28), 256);
    for (int i = 0; i < n; i++) {
        pp->x[i] = bx0 + 8 + pitch / 2 + (i % k->cols) * pitch;
        pp->y[i] = by0 + 3 + pitch / 2 + (i / k->cols) * pitch;
    }
    // pin 1: a mark
    float m[6] = {(float)bx0 - 2, (float)by0 - 2, (float)bx0 + 8, (float)by0 - 2, (float)bx0 - 2, (float)by0 + 8};
    tfd_poly(m, 3, C_TEXT, 256);
    tfd_text_s(&KF_LABEL, 8, s_W - 8, by1 + (BAR ? 30 : 34), k->where[s_he ? 1 : 0], C_DIM, s_he);
}

// THE WIRING: the pads and their labels (the top row: under the pads in the 3.49 as the silkscreen; the others:
// outside)
static void wire_pads(const rfp_conn_t *k, int n, const padpos_t *pp)
{
    for (int i = 0; i < n; i++) {
        const rfp_pad_t *p = &k->pads[i];
        bool hot = p->role != RFP_PAD_OTHER;
        if (hot) {
            tfd_disc((float)pp->x[i], (float)pp->y[i], 8.5f, role_col(p->role), 110);
        }
        tfd_disc((float)pp->x[i], (float)pp->y[i], hot ? 6.0f : 5.0f, hot ? role_col(p->role) : C_PAD, 256);
        tfd_disc((float)pp->x[i], (float)pp->y[i], 2.2f, k->shape == RFP_SHAPE_349 ? C_PCB : rgb(10, 10, 12), 256);
        bool top = i < k->cols;
        int ly = k->shape == RFP_SHAPE_349 ? (top ? pp->y[i] + 19 : pp->y[i] - 11)
                                           : (top ? pp->y[i] - 12 : pp->y[i] + 20);
        text_c(&RF_AX, pp->x[i], ly, p->label, hot ? role_col(p->role) : C_AXIS);
    }
}

// THE WIRING: the module and the three wires from the pads (b: the box of wire_board_349 or wire_board_conn)
static void wire_module(const rfp_conn_t *k, int n, const padpos_t *pp, const int b[4])
{
    const int bx0 = b[0], by1 = b[3];
    int mx[4], my;
    int vcc = -1, gnd = -1, dat = -1;
    for (int i = 0; i < n; i++) {
        if (k->pads[i].role == RFP_PAD_3V3) {
            vcc = i;
        }
        if (k->pads[i].role == RFP_PAD_GND) {
            gnd = i;
        }
        if (k->pads[i].role == RFP_PAD_DATA) {
            dat = i;
        }
    }
    const int idx[3] = {vcc, dat, gnd}, pin[3] = {0, 1, 3};
    const uint16_t col[3] = {C_RED, C_DATAW, C_GNDW};
    if (BAR) {
        draw_module(394, 30, 128, 54, false, mx, &my);
        // the lanes under the board, then up to the pins of the module (VCC pin 0, DATA pin 1, GND pin 3)
        const int base = by1 + 8;
        for (int w = 0; w < 3; w++) {
            int i = idx[w];
            if (i < 0) {
                continue;
            }
            int x0 = pp->x[i], y0 = pp->y[i] + 7;
            if (i < k->cols) {  // a pad of the top row: half a pad step to the right, then down in the gap
                x0 += (pp->x[1] - pp->x[0]) / 2;  // between two columns (not over the pad under it)
                tfd_line((float)pp->x[i], (float)pp->y[i], (float)x0, (float)pp->y[i], 3, col[w], 256);
                y0 = pp->y[i];
            }
            wire(x0, y0, base + w * 6, mx[pin[w]], my, col[w]);
        }
        return;
    }
    const int mxl = 12, myt = s_H - 92;
    draw_module(mxl, myt, 128, 50, true, mx, &my);
    tfd_text_c(&KF_LABEL, mxl + 64, myt + 66, TX(T_TWO_DATA), C_DIM, 128);
    const int base = by1 + (k->shape == RFP_SHAPE_349 ? 8 : 40);
    for (int w = 0; w < 3; w++) {
        int i = idx[w];
        if (i < 0) {
            continue;
        }
        int x0 = pp->x[i];
        int y0 = pp->y[i] + 7;
        if (i < k->cols && k->shape != RFP_SHAPE_349) {  // the top row: out at the left end first
            tfd_line((float)x0, (float)pp->y[i], (float)bx0 - 4 - w * 4, (float)pp->y[i], 3, col[w], 256);
            x0 = bx0 - 4 - w * 4;
        } else if (i < k->cols) {  // the top row of the 3.49: down in the gap between two columns
            x0 += (pp->x[1] - pp->x[0]) / 2;
            tfd_line((float)pp->x[i], (float)pp->y[i], (float)x0, (float)pp->y[i], 3, col[w], 256);
            y0 = pp->y[i];
        }
        wire(x0, y0, base + w * 6, mx[pin[w]], my, col[w]);
    }
}

// THE WIRING of the 3.49 or of an other connector in a box: the board, the pads, the module, the wires, the legend
static void wire_box(const rfp_conn_t *k, char leg[3][32])
{
    padpos_t pp;
    int b[4];  // the box of the connector (or the board): x0, y0, x1, y1
    const int n = k->rows * k->cols;
    if (k->shape == RFP_SHAPE_349) {
        wire_board_349(k, n, &pp, b);
    } else {
        wire_board_conn(k, n, &pp, b);
    }
    wire_pads(k, n, &pp);
    wire_module(k, n, &pp, b);
    // the legend
    const uint16_t lc[3] = {C_RED, C_GNDW, C_DATAW};
    for (int i = 0; i < 3; i++) {
        int lx = BAR ? s_W / 2 + 10 + i * 102 : 150, lyy = BAR ? 15 : s_H - 84 + i * 18;
        tfd_disc((float)lx, (float)lyy - 4, 4, lc[i], 256);
        mf_text(&RF_SM, lx + 8, lyy, leg[i], C_TEXT, 255, 0);
    }
}

// THE WIRING: the state of the pin and the buttons CONNECTED and DEMO
static void wire_state(void)
{
    const char *st;
    uint16_t sc;
    if (!s_open) {
        st = TX(T_FAIL);
        sc = C_RED;
    } else if (s_st.line == RFP_LINE_ALIVE) {
        st = TX(T_ALIVE);
        sc = C_OK;
    } else if (s_st.line == RFP_LINE_HIGH) {
        st = TX(T_HIGH);
        sc = C_WARN;
    } else {
        st = TX(T_QUIET);
        sc = C_DIM;
    }
    char b[96];
    if (s_open && s_st.line == RFP_LINE_ALIVE && BAR) {
        snprintf(b, sizeof b, "%s: %u %s", st, (unsigned)s_rate, TX(T_EDGES));
    } else if (s_open && s_st.line == RFP_LINE_ALIVE) {
        snprintf(b, sizeof b, "%s (%u/S)", st, (unsigned)s_rate);
    } else {
        snprintf(b, sizeof b, "%s", st);
    }
    const int sy = BAR ? s_H - 6 : s_H - 4, sx1 = s_bx[0] - 8;
    tfd_disc(BAR ? 12.0f : (float)s_W - 14, (float)sy - 5, 5, sc, 256);
    if (BAR) {
        tfd_text_s(&KF_LABEL, 22, sx1, sy, b, sc, s_he);
    } else {
        tfd_text_c(&KF_LABEL, s_W / 2, 30 + 2, b, sc, s_W - 20);
    }
    // the buttons
    const int ty = gfx_round() ? 8 : 5;  // (the base line under the middle of a key: KF_TEXT in the round frame)
    if (src_index(false) >= 0) {
        bool ok = s_open && s_st.line == RFP_LINE_ALIVE;
        tfd_rrect(s_bx[0], s_by[0], s_bw[0], s_bh[0], 8, ok ? C_GO : C_BTN, 256);
        tfd_text_c(&KF_LABEL, s_bx[0] + s_bw[0] / 2, s_by[0] + s_bh[0] / 2 + ty, TX(T_CONNECTED), C_TEXT, s_bw[0] - 6);
    }
    tfd_rrect(s_bx[1], s_by[1], s_bw[1], s_bh[1], 8, C_BTN, 256);
    tfd_text_c(&KF_LABEL, s_bx[1] + s_bw[1] / 2, s_by[1] + s_bh[1] / 2 + ty, TX(T_DEMO), C_TEXT, s_bw[1] - 6);
}

// THE WIRING
static void draw_wire_screen(void)
{
    const rfp_conn_t *k = rfp_board_conn();
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    // the title and the legend of the three wires (the round frame: the title in the top cap, the panel of the keys)
    if (gfx_round()) {
        rl_cap(rl_oy, C_BG, C_PANEL, C_LINE, &KF_TEXT, TX(T_CONNECT), C_TEXT);
        rl_t_bcap(rl_t_key() - 6, C_PANEL, C_LINE);
    } else {
        tfd_text_s(&KF_LABEL, 8, BAR ? s_W / 2 : s_W - 8, 16, TX(T_CONNECT), C_TEXT, s_he);
    }
    char leg[3][32] = {"", "", ""};
    wire_legend_text(k, leg);
    if (k->shape == RFP_SHAPE_NONE) {
        tfd_text_c(&KF_LABEL, s_W / 2, s_H / 2, TX(T_NOPINS), C_WARN, s_W - 20);
    } else if (k->shape == RFP_SHAPE_BUS30) {
        wire_bus30(k, leg);
    } else {
        wire_box(k, leg);
    }
    wire_state();
}

// ---------------------------------------------------------------- LIVE
static uint16_t kind_col(int kind)
{
    switch (kind) {
    case RFD_K_RCSWITCH:
        return rgb(70, 150, 255);
    case RFD_K_PWM:
        return rgb(255, 170, 60);
    case RFD_K_PPM:
        return rgb(120, 220, 120);
    case RFD_K_MANCH:
        return rgb(220, 110, 240);
    case RFD_K_NRZ:
        return rgb(90, 210, 220);
    default:
        return rgb(140, 140, 150);
    }
}

// the Tab5: LIVE and A CODE in the bigger faces (F_SM: the small text, F_MD: the values, F_LB: the words)
#if BOARD_UI_TABLET
#define T5   1
#define F_SM (&RF_MD)
#define F_MD (&RF_BIG)
#define F_LB (&KF_NAME)
#else
#define T5   0
#define F_SM (&RF_SM)
#define F_MD (&RF_MD)
#define F_LB (&KF_LABEL)
#endif

static void kind_label(const rfd_code_t *c, char *b, int n)
{
    if (c->kind == RFD_K_RCSWITCH) {
        snprintf(b, (size_t)n, "RCSWITCH P%d", c->proto);
    } else {
        snprintf(b, (size_t)n, "%s", rfd_kind_name(c->kind));
    }
}

static void draw_strip(void)
{
    const int y0 = s_strip_y0, y1 = s_strip_y1, hi = y0 + 7, lo = y1 - 7;
    tfd_rect(0, y0, s_W, y1 - y0, C_PANEL, 256);
    const float win = 120000.0f, upp = win / s_W;  // the last 120 ms
    for (int t = 0; t <= 120; t += 10) {
        int x = s_W - 1 - (int)(t * 1000.0f / upp);
        tfd_rect(x, y1 - 3, 1, 3, C_LINE, 256);
    }
    if (!s_open) {
        return;
    }
    // the level now (since the last edge), then the level times backwards
    float x = (float)(s_W - 1);
    int64_t span = s_st.now_us - s_raw_end;
    int lvl = s_st.level;
    float xl = x - span / upp;
    const uint16_t c = C_DATAW;
    tfd_line(xl, (float)(lvl ? hi : lo), x, (float)(lvl ? hi : lo), 1.6f, c, 256);
    x = xl;
    uint32_t i = s_rawn;
    int guard = 0;
    while (x > 0 && i > 0 && s_rawn - i < RAWN && guard++ < RAWN) {
        int32_t d = s_raw[(--i) & (RAWN - 1)];
        float xn = x - (float)(d < 0 ? -d : d) / upp;
        int y = d > 0 ? hi : lo;
        tfd_line(xn < 0 ? 0 : xn, (float)y, x, (float)y, 1.6f, c, 256);
        tfd_line(x, (float)hi, x, (float)lo, 1.0f, c, 200);  // the edge
        x = xn;
    }
    tfd_rect(2, y0 + 1, mf_width(&RF_AX, "120 MS", 0) + 6, 12, C_PANEL, 230);
    mf_text(&RF_AX, 5, y0 + 10, "120 MS", C_DIM, 255, 0);
}

// the round frame: the top cap of LIVE. The receiver (or DEMO), the decoder, then the dot of the line, the rate and
// the count
static void live_cap_r(void)
{
    const int W = gfx_w, top = s_top_h;
    gfx_fill_rect(0, 0, W, top, C_PANEL);
    const int lw = rl_half(top, 2);
    gfx_fill_rect(W / 2 - lw, top, 2 * lw, 1, C_LINE);
    const rfp_src_t *sr = cur();
    char b[96], m[64];
    if (sr && (sr->caps->flags & RFP_F_DEMO)) {
        snprintf(b, sizeof b, "DEMO");
    } else {
        snprintf(b, sizeof b, "%s MHZ", s_freq ? "315" : "433.92");
    }
    snprintf(m, sizeof m, "%s%s%s", MODE_VALS[2 * s_mode], s_mode <= RFD_MODE_GENERIC && s_coding ? " / " : "",
             s_mode <= RFD_MODE_GENERIC && s_coding ? rfd_kind_name(s_coding) : "");
    const int w1 = mf_width(&RF_BIG, b, 0), w2 = mf_width(&RF_MD, m, 0), x1 = W / 2 - (w1 + 12 + w2) / 2;
    mf_text(&RF_BIG, x1, W * 50 / 466, b, C_TEXT, 255, 0);
    mf_text(&RF_MD, x1 + w1 + 12, W * 50 / 466, m, C_AXIS, 255, 0);
    const uint16_t lc = !s_open                       ? C_RED
                        : s_st.line == RFP_LINE_ALIVE ? C_OK
                        : s_st.line == RFP_LINE_HIGH  ? C_WARN
                                                      : C_DIM;
    snprintf(b, sizeof b, "%u/S  %d", (unsigned)s_rate, s_nent);
    const int w = mf_width(&RF_MD, b, 0);
    tfd_disc((float)(W / 2 - w / 2 - 6), (float)(W * 77 / 466 - 5), 4.5f, lc, 256);
    mf_text(&RF_MD, W / 2 - w / 2 + 6, W * 77 / 466, b, C_AXIS, 255, 0);
}

// the round frame: LIVE in the whole circle. The rows of the codes follow the chord down to the bottom of the circle;
// then the cap and the strip of the raw pulses (its own origin and width: the chord) over the rows that go up
static void draw_live_r(void)
{
    const int W = gfx_w, ly0 = s_list_y0, ly1 = s_H, rh = s_row_h;
    gfx_fill(C_BG);
    tfd_clip(0, ly0, W, ly1);
    if (!s_nent) {
        tfd_text_c(&KF_LABEL, W / 2, (ly0 + ly1) / 2, s_open ? TX(T_WAIT) : TX(T_FAIL), C_DIM, W * 7 / 10);
    }
    char k[24], h[40], d[48];
    for (int i = 0; i < s_nent; i++) {
        const int y = ly0 + i * rh - (int)s_scroll;
        if (y + rh < ly0 || y > ly1) {
            continue;
        }
        int x, w;
        rl_t_row(y, rh, ly0, ly1, 12, &x, &w);
        const ent_t *e = &s_ent[i];
        const bool fresh = now_us() - e->t_last < 600000;
        tfd_rrect(x, y + 3, w, rh - 6, 8, fresh ? rgb(22, 38, 56) : C_PANEL, 256);
        kind_label(&e->c, k, sizeof k);
        const int tw = mf_width(&RF_MD, k, 0) + 16, cw = tw > 86 ? tw : 86;
        tfd_rrect(x + 8, y + 9, cw, rh - 18, 6, kind_col(e->c.kind), 70);
        text_c(&RF_MD, x + 8 + cw / 2, y + rh / 2 + 5, k, kind_col(e->c.kind));
        if (e->c.kind == RFD_K_RAW) {
            snprintf(h, sizeof h, "%d LEVELS", e->c.nbits);
        } else {
            rfd_hex(&e->c, h, sizeof h);
        }
        const int vx = x + 8 + cw + 12;
        mf_text(&RF_BIG, vx, y + rh / 2 + 7, h, C_TEXT, 255, 0);
        const int age = (int)((now_us() - e->t_last) / 1000000);
        snprintf(d, sizeof d, "x%d  %ds", e->reps, age < 0 ? 0 : age);
        const int dw = mf_width(&RF_MD, d, 0);
        if (vx + mf_width(&RF_BIG, h, 0) + 12 < x + w - 12 - dw) {  // (only when it has room)
            mf_text(&RF_MD, x + w - 12 - dw, y + rh / 2 + 5, d, e->reps > 1 ? C_TEXT : C_DIM, 255, 0);
        }
    }
    tfd_noclip();
    gfx_fill_rect(0, 0, W, ly0, C_BG);
    live_cap_r();
    int sx, sw;
    rl_t_row(s_strip_y0, s_strip_y1 - s_strip_y0, s_strip_y0, s_strip_y1, 10, &sx, &sw);
    rl_ox = sx;
    s_W = sw;
    draw_strip();
    rl_ox = 0;
    s_W = W;
}

static void draw_live(void)
{
    if (gfx_round()) {
        draw_live_r();
        return;
    }
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    // the top bar: the receiver and the module, the decoder, the state of the line
    tfd_rect(0, 0, s_W, s_top_h, C_PANEL, 256);
    const rfp_src_t *sr = cur();
    char b[96];
    if (sr && (sr->caps->flags & RFP_F_DEMO)) {
        snprintf(b, sizeof b, "DEMO");
    } else {
        snprintf(b, sizeof b, "%s MHZ", s_freq ? "315" : "433.92");
    }
    mf_text(F_MD, 6, BAR ? 16 : T5 ? 28 : 18, b, C_TEXT, 255, 0);
    int x = 6 + mf_width(F_MD, b, 0) + 14;
    snprintf(b, sizeof b, "%s%s%s", MODE_VALS[2 * s_mode], s_mode <= RFD_MODE_GENERIC && s_coding ? " / " : "",
             s_mode <= RFD_MODE_GENERIC && s_coding ? rfd_kind_name(s_coding) : "");
    mf_text(F_SM, x, BAR ? 15 : T5 ? 27 : 17, b, C_AXIS, 255, 0);
    uint16_t lc = !s_open ? C_RED : s_st.line == RFP_LINE_ALIVE ? C_OK : s_st.line == RFP_LINE_HIGH ? C_WARN : C_DIM;
    snprintf(b, sizeof b, "%u/S  %d", (unsigned)s_rate, s_nent);
    int w = mf_width(F_SM, b, 0);
    mf_text(F_SM, s_W - 8 - w, BAR ? 15 : T5 ? 27 : 17, b, C_AXIS, 255, 0);
    tfd_disc((float)(s_W - 18 - w), (float)(BAR ? 11 : T5 ? 21 : 13), T5 ? 6.0f : 4.5f, lc, 256);
    draw_strip();
    // the list
    const int ly0 = s_list_y0;
    tfd_clip(0, ly0, s_W, s_H);
    if (!s_nent) {
        tfd_text_c(F_LB, s_W / 2, ly0 + (s_H - ly0) / 2 + 5, s_open ? TX(T_WAIT) : TX(T_FAIL), C_DIM, s_W - 20);
    }
    for (int i = 0; i < s_nent; i++) {
        int y = ly0 + i * s_row_h - (int)s_scroll;
        if (y + s_row_h < ly0 || y > s_H) {
            continue;
        }
        const ent_t *e = &s_ent[i];
        bool fresh = now_us() - e->t_last < 600000;
        tfd_rrect(4, y + 2, s_W - 8, s_row_h - 4, 6, fresh ? rgb(22, 38, 56) : C_PANEL, 256);
        char k[24], h[40], d[48];
        kind_label(&e->c, k, sizeof k);
        const int chip_w = BAR ? 112 : T5 ? 170 : 92;
        tfd_rrect(10, y + 8, chip_w, s_row_h - 16, 5, kind_col(e->c.kind), 70);
        text_c(F_SM, 10 + chip_w / 2, y + s_row_h / 2 + F_SM->asc / 2, k, kind_col(e->c.kind));
        if (e->c.kind == RFD_K_RAW) {
            snprintf(h, sizeof h, "%d LEVELS", e->c.nbits);
        } else {
            rfd_hex(&e->c, h, sizeof h);
        }
        mf_text(F_MD, 10 + chip_w + 10, y + s_row_h / 2 + (T5 ? 8 : 6), h, C_TEXT, 255, 0);
        if (BAR || T5) {
            if (e->c.kind == RFD_K_RCSWITCH) {
                snprintf(d, sizeof d, "%d BIT  %d US", e->c.nbits, e->c.t_short);
            } else if (e->c.kind == RFD_K_RAW) {
                snprintf(d, sizeof d, "%d / %d CLUSTERS", e->c.nclust_h, e->c.nclust_l);
            } else {
                snprintf(d, sizeof d, "%d BIT  %d/%d US", e->c.nbits, e->c.t_short, e->c.t_long);
            }
            mf_text(F_SM, T5 ? 390 : 330, y + s_row_h / 2 + (T5 ? 6 : 5), d, C_AXIS, 255, 0);
        }
        int age = (int)((now_us() - e->t_last) / 1000000);
        snprintf(d, sizeof d, "x%d  %ds", e->reps, age < 0 ? 0 : age);
        int dw = mf_width(F_SM, d, 0);
        mf_text(F_SM, s_W - 14 - dw, y + s_row_h / 2 + (T5 ? 6 : 5), d, e->reps > 1 ? C_TEXT : C_DIM, 255, 0);
    }
    tfd_noclip();
}

// ---------------------------------------------------------------- A CODE
// the round frame: a line of a name (left) and a value (in two lines when it is long, cut at a group) in the column
// x0 .. x1; the next base line
static int info_line_r(int x0, int x1, int y, const char *name, char *v, uint16_t c)
{
    const int vx = x0 + (name[0] ? mf_width(&RF_MD, name, 0) + 12 : 0);
    if (name[0]) {
        mf_text(&RF_MD, x0, y, name, C_DIM, 255, 0);
    }
    char *cut = NULL;
    if (mf_width(&RF_MD, v, 0) > x1 - vx) {
        for (char *q = v; *q; q++) {
            if (*q != ' ') {
                continue;
            }
            *q = 0;
            const bool fits = mf_width(&RF_MD, v, 0) <= x1 - vx;
            *q = ' ';
            if (!fits) {
                break;
            }
            cut = q;
        }
    }
    if (cut) {
        *cut = 0;
        mf_text(&RF_MD, vx, y, v, c, 255, 0);
        y += 22;
        mf_text(&RF_MD, vx, y, cut + 1, c, 255, 0);
    } else {
        mf_text(&RF_MD, vx, y, v, c, 255, 0);
    }
    return y + 25;
}

// the round frame: A CODE in the whole circle. The kind and the bits in the top cap, the value under it, the pulses of
// the last packet in the width of the chord, the lines of the facts in a middle column, BACK in the bottom cap (a tap
// anywhere goes back)
static void draw_info_r(const ent_t *e)
{
    const rfd_code_t *c = &e->c;
    const int W = gfx_w, top = s_top_h;
    char b[200], h[48], d[32];
    gfx_fill(C_BG);
    gfx_fill_rect(0, 0, W, top, C_PANEL);
    const int lw = rl_half(top, 2);
    gfx_fill_rect(W / 2 - lw, top, 2 * lw, 1, C_LINE);
    kind_label(c, b, 40);
    mf_text_c(&RF_BIG, W / 2, W * 50 / 466, b, kind_col(c->kind), 255, 0);
    if (c->kind != RFD_K_RAW) {
        snprintf(b, sizeof b, "%d BIT  x%d", c->nbits, e->reps);
    } else {
        snprintf(b, sizeof b, "%d LEVELS  x%d", c->nbits, e->reps);
    }
    mf_text_c(&RF_MD, W / 2, W * 77 / 466, b, C_AXIS, 255, 0);
    bool dec_line = false;
    if (c->kind != RFD_K_RAW) {
        rfd_hex(c, h, sizeof h);
        rfd_dec(c, d, sizeof d);
        snprintf(b, sizeof b, "%s  %s", h, d);
        if (mf_width(&RF_BIG, b, 0) > 2 * rl_half(top + 10, 16)) {
            snprintf(b, sizeof b, "%s", h);  // (the decimal: a line below)
            dec_line = d[0] != 0;
        }
        mf_text_c(&RF_BIG, W / 2, top + 30, b, C_TEXT, 255, 0);
    }
    // the pulses of the last packet, the whole packet in the width of the chord
    const rfd_packet_t *p = &e->pkt;
    const int wy0 = top + 42, wy1 = top + 98, hi = wy0 + 6, lo = wy1 - 6;
    int x, w;
    rl_t_row(wy0, wy1 - wy0, wy0, wy1, 12, &x, &w);
    tfd_rrect(x, wy0, w, wy1 - wy0, 8, C_PANEL, 256);
    double tot = 0;
    for (int i = 0; i < p->n; i++) {
        tot += abs(p->d[i]);
    }
    if (tot > 0) {
        const double k = (w - 20) / tot;
        double xx = x + 10;
        for (int i = 0; i < p->n; i++) {
            const double xn = xx + abs(p->d[i]) * k;
            const int y = p->d[i] > 0 ? hi : lo;
            tfd_line((float)xx, (float)y, (float)xn, (float)y, 2.0f, C_DATAW, 256);
            if (i + 1 < p->n) {
                tfd_line((float)xn, (float)hi, (float)xn, (float)lo, 1.2f, C_DATAW, 200);
            }
            xx = xn;
        }
    }
    // the lines of the facts: a middle column of the band
    const int cw = 2 * rl_half(rl_t_bot(), 14), x0 = W / 2 - cw / 2, x1 = x0 + cw;
    int y = wy1 + 28;
    if (c->kind != RFD_K_RAW) {
        if (dec_line) {
            y = info_line_r(x0, x1, y, "DEC", d, C_TEXT);
        }
        rfd_binary(c, b, sizeof b);
        y = info_line_r(x0, x1, y, "BIN", b, C_TEXT);
        if (rfd_tristate(c, h, sizeof h) && c->kind == RFD_K_RCSWITCH) {
            y = info_line_r(x0, x1, y, "TRI-STATE", h, C_TEXT);
        }
    }
    if (c->kind == RFD_K_RCSWITCH) {
        snprintf(b, sizeof b, "PULSE %d US   SYNC %u US   PROTOCOL %d", c->t_short, (unsigned)c->t_gap, c->proto);
    } else if (c->kind == RFD_K_RAW) {
        int o = snprintf(b, sizeof b, "HIGH:");
        for (int i = 0; i < c->nclust_h && i < 4; i++) {
            o += snprintf(b + o, sizeof b - (size_t)o, " %d", c->ch[i]);
        }
        o += snprintf(b + o, sizeof b - (size_t)o, "   LOW:");
        for (int i = 0; i < c->nclust_l && i < 4; i++) {
            o += snprintf(b + o, sizeof b - (size_t)o, " %d", c->cl[i]);
        }
        snprintf(b + o, sizeof b - (size_t)o, "   GAP %u US", (unsigned)c->t_gap);
    } else {
        snprintf(b, sizeof b, "SHORT %d US   LONG %d US   GAP %u US   FIT %d%%", c->t_short, c->t_long,
                 (unsigned)c->t_gap, (int)(c->score * 100 + 0.5f));
    }
    info_line_r(x0, x1, y, "", b, C_AXIS);
    // BACK in the bottom cap
    const int ky = rl_t_key(), kh = rl_t_kh();
    int kx, kw;
    rl_t_keyx(0, 1, &kx, &kw);
    rl_t_bcap(ky - 6, C_PANEL, C_LINE);
    tfd_rrect(kx, ky, kw, kh, 10, C_BTN, 256);
    tfd_text_c(&KF_LABEL, kx + kw / 2, ky + kh / 2 + 8, TX(T_BACK), C_TEXT, kw - 8);
}

static void draw_info(void)
{
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    if (s_sel < 0 || s_sel >= s_nent) {
        s_scr = SC_LIVE;
        return;
    }
    const ent_t *e = &s_ent[s_sel];
    const rfd_code_t *c = &e->c;
    if (gfx_round()) {
        draw_info_r(e);
        return;
    }
    // BACK (a tap anywhere goes back; the button shows it)
    const int bw = T5 ? 108 : 84, nx = bw + 16;
    tfd_rrect(4, 4, bw, T5 ? 50 : 36, 8, C_BTN, 256);
    tfd_text_c(F_LB, 4 + bw / 2, T5 ? 35 : 27, TX(T_BACK), C_TEXT, bw - 4);
    char b[200], h[48], d[32];
    kind_label(c, b, 40);
    mf_text(F_MD, nx, T5 ? 26 : 20, b, kind_col(c->kind), 255, 0);
    int x = nx + mf_width(F_MD, b, 0) + 12;
    if (c->kind != RFD_K_RAW) {
        snprintf(b, sizeof b, "%d BIT  x%d", c->nbits, e->reps);  // (the number face has no Hebrew)
    } else {
        snprintf(b, sizeof b, "%d LEVELS  x%d", c->nbits, e->reps);
    }
    mf_text(F_SM, x, T5 ? 26 : 20, b, C_AXIS, 255, 0);
    if (c->kind != RFD_K_RAW) {
        rfd_hex(c, h, sizeof h);
        rfd_dec(c, d, sizeof d);
        snprintf(b, sizeof b, "%s  %s", h, d);
        if (mf_width(&RF_BIG, b, 0) > s_W - nx - 4) {
            snprintf(b, sizeof b, "%s", h);  // (the decimal: a line below)
        }
        mf_text(&RF_BIG, nx, T5 ? 56 : 38, b, C_TEXT, 255, 0);
    }
    // the pulses of the last packet, the whole packet in the width
    const rfd_packet_t *p = &e->pkt;
    const int wy0 = T5 ? 66 : 46, wy1 = BAR ? 82 : T5 ? 150 : 96, hi = wy0 + 4, lo = wy1 - 4;
    tfd_rect(0, wy0, s_W, wy1 - wy0, C_PANEL, 256);
    double tot = 0;
    for (int i = 0; i < p->n; i++) {
        tot += abs(p->d[i]);
    }
    if (tot > 0) {
        const double k = (s_W - 16) / tot;
        double xx = 8;
        for (int i = 0; i < p->n; i++) {
            double xn = xx + abs(p->d[i]) * k;
            int y = p->d[i] > 0 ? hi : lo;
            tfd_line((float)xx, (float)y, (float)xn, (float)y, 1.6f, C_DATAW, 256);
            if (i + 1 < p->n) {
                tfd_line((float)xn, (float)hi, (float)xn, (float)lo, 1.0f, C_DATAW, 200);
            }
            xx = xn;
        }
    }
    // the text lines
    const int lh = BAR ? 18 : T5 ? 30 : 22, vx = T5 ? 60 : 44;  // (vx: the values after DEC, BIN)
    int y = wy1 + (BAR ? 16 : T5 ? 30 : 20);
    if (c->kind != RFD_K_RAW) {
        if (mf_width(&RF_BIG, h, 0) + mf_width(&RF_BIG, d, 0) + 16 > s_W - nx - 4 && d[0]) {
            mf_text(F_SM, 8, y, "DEC", C_DIM, 255, 0);
            mf_text(F_SM, vx, y, d, C_TEXT, 255, 0);
            y += lh;
        }
        rfd_binary(c, b, sizeof b);
        mf_text(F_SM, 8, y, "BIN", C_DIM, 255, 0);
        // a long binary: cut at a group (a space) into two lines
        char *cut = NULL;
        if (mf_width(F_SM, b, 0) > s_W - vx - 8) {
            for (char *q = b; *q; q++) {
                if (*q != ' ') {
                    continue;
                }
                *q = 0;
                bool fits = mf_width(F_SM, b, 0) <= s_W - vx - 8;
                *q = ' ';
                if (!fits) {
                    break;
                }
                cut = q;
            }
        }
        if (cut) {
            *cut = 0;
            mf_text(F_SM, vx, y, b, C_TEXT, 255, 0);
            y += lh - 4;
            mf_text(F_SM, vx, y, cut + 1, C_TEXT, 255, 0);
        } else {
            mf_text(F_SM, vx, y, b, C_TEXT, 255, 0);
        }
        y += lh;
        if (rfd_tristate(c, h, sizeof h) && c->kind == RFD_K_RCSWITCH) {
            mf_text(F_SM, 8, y, "TRI-STATE", C_DIM, 255, 0);
            mf_text(F_SM, T5 ? 124 : 90, y, h, C_TEXT, 255, 0);
            y += lh;
        }
    }
    if (c->kind == RFD_K_RCSWITCH) {
        snprintf(b, sizeof b, "PULSE %d US   SYNC %u US   PROTOCOL %d", c->t_short, (unsigned)c->t_gap, c->proto);
    } else if (c->kind == RFD_K_RAW) {
        int o = snprintf(b, sizeof b, "HIGH:");
        for (int i = 0; i < c->nclust_h && i < 4; i++) {
            o += snprintf(b + o, sizeof b - (size_t)o, " %d", c->ch[i]);
        }
        o += snprintf(b + o, sizeof b - (size_t)o, "   LOW:");
        for (int i = 0; i < c->nclust_l && i < 4; i++) {
            o += snprintf(b + o, sizeof b - (size_t)o, " %d", c->cl[i]);
        }
        snprintf(b + o, sizeof b - (size_t)o, "   GAP %u US", (unsigned)c->t_gap);
    } else {
        snprintf(b, sizeof b,
                 BAR || T5 ? "SHORT %d US   LONG %d US   GAP %u US   FIT %d%%" : "%d / %d US  GAP %u  FIT %d%%",
                 c->t_short, c->t_long, (unsigned)c->t_gap, (int)(c->score * 100 + 0.5f));
    }
    mf_text(F_SM, 8, y, b, C_AXIS, 255, 0);
}

static void render(int64_t now)
{
    s_now = now;
    tfd_screen();
    tfd_noclip();
    if (gfx_round()) {  // (the round frame: the layout of the screen now)
        layout();
    }
    if (s_scr == SC_WIRE) {
        draw_wire_screen();
    } else if (s_scr == SC_INFO) {
        draw_info();
    } else {
        draw_live();
    }
    if (s_toast[0] && now - s_toast_t < 2500) {
        int w = ui_text_w(&KF_LABEL, s_toast) + 20;
        if (w > s_W - 20) {
            w = s_W - 20;
        }
        int x = s_W / 2 - w / 2, y = gfx_round() ? rl_t_key() - rl_oy : s_H / 2 - 14;  // (the round frame: the cap)
        tfd_rrect(x, y, w, 28, 8, C_PANEL, 235);
        tfd_text_c(&KF_LABEL, s_W / 2, y + 19, s_toast, C_TEXT, w - 8);
    }
}

// ---------------------------------------------------------------- the menu icon
#include "rndlay/rl_origin_end.h"  // (the menu icon draws at its own points)
#include "tficon/tficon.h"
// the menu icon: a key fob: its button presses, its light flashes and radio waves go out; the pulses scroll
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_box_t b = tfi_tile(x, y, s, tfi_hex(0xffa040), tfi_hex(0xc43c00));
    float u = s / 100.0f, ph = tfi_ph(2);
    float press = ph < 0.2f ? 1.0f : 0.0f;
    // the waves on both sides
    tfd_clip(b.x, b.y, b.x + b.w, b.y + b.h);
    for (int k = 0; k < 2; k++) {
        float q = ph * 2 + k * 0.5f;
        q -= (float)(int)q;
        int a = tfi_qa((int)(256 * (1 - q)), 3);
        tfi_arc(x + 50 * u, y + 34 * u, (16 + q * 22) * u, 3 * u, -0.7f, 0.5f, rgb(255, 255, 255), a);
        tfi_arc(x + 50 * u, y + 34 * u, (16 + q * 22) * u, 3 * u, 2.64f, 3.84f, rgb(255, 255, 255), a);
    }
    tfd_clip(x, y, x + s, y + s);
    // the fob with its ring
    tfi_ring(x + 50 * u, y + 16 * u, 6 * u, 3 * u, tfi_hex(0xd8dce6), 256);
    int fx = x + (int)(36 * u), fy = y + (int)(20 * u), fw = (int)(28 * u), fh = (int)(46 * u);
    tfi_drop(fx, fy, fw, fh, (int)(12 * u), (int)(3 * u), 90);
    tfi_gloss_box(fx, fy, fw, fh, (int)(12 * u), tfi_hex(0x4a4e60), tfi_hex(0x15171f));
    tfi_disc(x + 50 * u, y + 28 * u, 2.6f * u, press > 0 ? tfi_hex(0xff4040) : tfi_hex(0x602020),
             press > 0 ? tfi_hex(0xff4040) : tfi_hex(0x602020), 256);
    if (press > 0) {
        tfi_disc(x + 50 * u, y + 28 * u, 5.5f * u, tfi_hex(0xff4040), tfi_hex(0xff4040), 140);
    }
    tfi_disc(x + 50 * u, y + (40 + press * 1) * u, (6.5f - press) * u, tfi_hex(0xe8ecf4), tfi_hex(0x9aa0b4), 256);
    tfi_disc(x + 50 * u, y + 55 * u, 5 * u, tfi_hex(0xe8ecf4), tfi_hex(0x9aa0b4), 256);
    // the pulses
    int wx0 = x + (int)(16 * u), wx1 = x + (int)(84 * u), wy = y + (int)(78 * u);
    for (int xx = wx0; xx < wx1; xx++) {
        float v = (xx - x) / u + ph * 60;  // (the pattern: 15 units; four patterns in the loop)
        int bit = ((int)(v / 5) * 5 + 1) % 3 == 0;
        int lw = (int)(2.4f * u) > 1 ? (int)(2.4f * u) : 1;
        tfd_rect(xx, bit ? wy - (int)(8 * u) : wy, 1, lw, rgb(255, 255, 255), 256);
        int nb = ((int)((v + 1 / u) / 5) * 5 + 1) % 3 == 0;
        if (nb != bit) {
            tfd_rect(xx, wy - (int)(8 * u), lw, (int)(8 * u) + lw, rgb(255, 255, 255), 256);
        }
    }
    tfi_end();
}
