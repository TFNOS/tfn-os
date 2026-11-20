// IR DECODER: THE SCREENS (they only read the state of app_irdec.c).
// The task: the app task (render and input of app_irdec.c, which includes this file).
// The memory: the file statics of this part (the layout, the touch targets); no heap.
//
//   THE WIRING  the connector of this board as the user sees it (rfp_board_ir(): the pads and their roles), the IR
//               receiver module (DATA, VCC, GND) and the IR LED module (TX, VCC, GND), six coloured wires, the live
//               state of the pin, CONNECTED and DEMO
//   LIVE        the raw marks of the last 160 ms, and the list of the codes (the newest first)
//   A CODE      the pulses of its last frame, the protocol, the address and the command, the raw data, the bits, the
//               timing, the raw times; SEND

#define C_BG    rgb(6, 9, 14)
#define C_PANEL rgb(12, 18, 28)
#define C_LINE  rgb(30, 42, 58)
#define C_TEXT  rgb(225, 232, 240)
#define C_DIM   rgb(110, 125, 145)
#define C_AXIS  rgb(150, 165, 185)
#define C_PCB   rgb(24, 58, 112)
#define C_PCB_E rgb(48, 92, 160)
#define C_PAD   rgb(214, 178, 96)
#define C_MOD   rgb(28, 30, 36)
#define C_RED   rgb(255, 84, 70)
#define C_GNDW  rgb(170, 178, 190)
#define C_DATAW rgb(255, 214, 64)
#define C_TXW   rgb(226, 120, 255)
#define C_OK    rgb(70, 220, 120)
#define C_WARN  rgb(255, 160, 60)
#define C_BTN   rgb(24, 34, 50)
#define C_GO    rgb(36, 150, 80)

// the Tab5: LIVE and A CODE in the bigger faces (F_SM: the small text, F_MD: the values, F_LB: the words)
#if BOARD_UI_TABLET
#define T5   1
#define F_SM (&IR_MD)
#define F_MD (&IR_BIG)
#define F_LB (&KF_NAME)
#else
#define T5   0
#define F_SM (&IR_SM)
#define F_MD (&IR_MD)
#define F_LB (&KF_LABEL)
#endif

static uint16_t role_col(int role)
{
    switch (role) {
    case RFP_PAD_3V3:
        return C_RED;
    case RFP_PAD_GND:
        return C_GNDW;
    case RFP_PAD_DATA:
        return C_DATAW;
    case RFP_PAD_TX:
        return C_TXW;
    default:
        return C_PAD;
    }
}

static void text_c(const mfont_t *f, int cx, int base, const char *s, uint16_t c)
{
    mf_text_c(f, cx, base, s, c, 255, 0);
}

static void seg(int x0, int y0, int x1, int y1, uint16_t c)
{
    tfd_line((float)x0, (float)y0, (float)x1, (float)y1, 2.6f, c, 256);
}

// A module: a dark board, the part (the dome of the receiver, or the clear LED), its name on the far side from the
// pins, and 3 pins with their names (left to right as on a KY-022 / KY-005 board: the signal, VCC, GND). led: the LED
// module (TX). pins_up: the pins at the top. The x of each pin and the y of the pin ends.
static void draw_module(bool led, int x, int y, int w, int h, bool pins_up, int px[3], int *pin_y)
{
    tfd_rrect(x, y, w, h, 4, C_MOD, 256);
    tfd_rrect(x + 2, y + 2, w - 4, h - 4, 3, rgb(44, 48, 58), 140);
    const int pitch = (w - 32) / 2;
    *pin_y = pins_up ? y - 9 : y + h + 9;
    for (int i = 0; i < 3; i++) {
        px[i] = x + 16 + i * pitch;
        tfd_rect(px[i] - 2, pins_up ? y - 9 : y + h, 4, 9, rgb(200, 200, 210), 256);
    }
    // the part, in the middle
    const int cx = x + w / 2, cy = y + h / 2 + (pins_up ? 2 : -2);
    if (led) {
        tfd_disc((float)cx, (float)cy, 7, rgb(150, 120, 210), 200);
        tfd_disc((float)cx - 2, (float)cy - 2, 2.5f, rgb(240, 230, 255), 230);
        tfd_rect(cx - 9, cy + (pins_up ? -10 : 6), 18, 3, rgb(150, 120, 210), 160);
    } else {
        tfd_rrect(cx - 10, cy - 7, 20, 14, 3, rgb(10, 10, 12), 256);
        tfd_disc((float)cx, (float)cy, 5, rgb(40, 40, 46), 256);
        tfd_disc((float)cx - 1.5f, (float)cy - 1.5f, 1.6f, rgb(120, 120, 130), 200);
    }
    // the names of the pins (the pin side) and of the module (the other side)
    static const char *const PN[2][3] = {{"DATA", "VCC", "GND"}, {"TX", "VCC", "GND"}};
    const uint16_t pc[3] = {led ? C_TXW : C_DATAW, C_RED, C_GNDW};
    for (int i = 0; i < 3; i++) {
        text_c(&IR_AX, px[i], pins_up ? y + 12 : y + h - 4, PN[led][i], pc[i]);
    }
    tfd_text_c(&KF_LABEL, cx, pins_up ? y + h - 4 : y + 14, TX(led ? T_LED : T_RECEIVER), C_TEXT, w - 6);
}

// The six wires: from the pad of each role to the pin of a module. The lanes go by the x of the pins (a deeper lane for
// a pin further right: a wire does not cross the end of a wire before it).
typedef struct {
    int pad;   // the index of the pad
    int x, y;  // the end of the pin
    uint16_t c;
} wire_t;

static int find_role(const rfp_conn_t *k, int role)
{
    for (int i = 0; i < k->rows * k->cols; i++) {
        if (k->pads[i].role == role) {
            return i;
        }
    }
    return -1;
}

// the wires of the two modules (pins rx[3], tx[3]: signal, VCC, GND), in the order of the x of their pins
static int make_wires(const rfp_conn_t *k, const int rx[3], int ry, const int lx[3], int ly, wire_t *w)
{
    const int v = find_role(k, RFP_PAD_3V3), g = find_role(k, RFP_PAD_GND), d = find_role(k, RFP_PAD_DATA),
              t = find_role(k, RFP_PAD_TX);
    const int pad[6] = {d, v, g, t, v, g};
    const uint16_t col[6] = {C_DATAW, C_RED, C_GNDW, C_TXW, C_RED, C_GNDW};
    int n = 0;
    for (int i = 0; i < 6; i++) {
        if (pad[i] < 0) {
            continue;
        }
        w[n].pad = pad[i];
        w[n].x = i < 3 ? rx[i] : lx[i - 3];
        w[n].y = i < 3 ? ry : ly;
        w[n].c = col[i];
        n++;
    }
    for (int i = 1; i < n; i++) {  // (by the x of the pin)
        for (int j = i; j > 0 && w[j].x < w[j - 1].x; j--) {
            wire_t tmp = w[j];
            w[j] = w[j - 1];
            w[j - 1] = tmp;
        }
    }
    return n;
}

typedef struct {
    int x[32], y[32];
} padpos_t;

static void draw_pads(const rfp_conn_t *k, const padpos_t *pp, bool silk)
{
    for (int i = 0; i < k->rows * k->cols; i++) {
        const rfp_pad_t *p = &k->pads[i];
        bool hot = p->role != RFP_PAD_OTHER;
        if (hot) {
            tfd_disc((float)pp->x[i], (float)pp->y[i], 8.5f, role_col(p->role), 110);
        }
        tfd_disc((float)pp->x[i], (float)pp->y[i], hot ? 6.0f : 5.0f, hot ? role_col(p->role) : C_PAD, 256);
        tfd_disc((float)pp->x[i], (float)pp->y[i], 2.2f, silk ? C_PCB : rgb(10, 10, 12), 256);
        bool top = i < k->cols;
        int ly = silk ? (top ? pp->y[i] + 19 : pp->y[i] - 11) : (top ? pp->y[i] - 12 : pp->y[i] + 20);
        if (!silk) {  // (a socket: the wires of the top row go up past the numbers)
            int tw = mf_width(&IR_AX, p->label, 0);
            tfd_rect(pp->x[i] - tw / 2 - 2, ly - 9, tw + 4, 11, rgb(24, 24, 28), 256);
        }
        text_c(&IR_AX, pp->x[i], ly, p->label, hot ? role_col(p->role) : C_AXIS);
    }
}

// THE WIRING of the M5-BUS (the 320 x 240 frame): the pads, the two modules, the wires and the legend
static void wire_bus30(const rfp_conn_t *k)
{
    // THE M5-BUS as the user sees it: the BACK of the CoreS3, the USB-C at the right. Two
    // columns: LEFT the odd pins, RIGHT the even pins, pin 1 (GND) at the top left; the two modules at the right
    const int pitch = (s_H - 54) / k->rows, x0l = 42, x0r = 84, y0 = 46;
    const int bx0 = 26, bx1 = 100, by0 = y0 - pitch / 2 - 2, by1 = y0 + (k->rows - 1) * pitch + pitch / 2 + 2;
    mf_text_c(&IR_AX, (bx0 + bx1) / 2, by1 + 11, "M5-BUS", C_DIM, 255, 0);
    tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, 4, rgb(24, 24, 28), 256);
    float m[6] = {(float)bx0 - 2, (float)by0 - 2, (float)bx0 + 8, (float)by0 - 2, (float)bx0 - 2, (float)by0 + 8};
    tfd_poly(m, 3, C_TEXT, 256);
    int px[32], py[32];
    for (int i = 0; i < k->rows * k->cols && i < 32; i++) {
        px[i] = (i & 1) ? x0r : x0l;  // (pads[] row by row: (1, 2), (3, 4) ...; odd at the left)
        py[i] = y0 + (i / 2) * pitch;
    }
    wire_t w[6];
    int rx[3], ry, lx[3], ly;
    draw_module(false, 126, 44, 92, 50, false, rx, &ry);
    draw_module(true, 222, 44, 92, 50, false, lx, &ly);
    const int nw = make_wires(k, rx, ry, lx, ly, w);
    for (int j = 0; j < nw; j++) {
        const int i = w[j].pad, xl = 106 + j * 4, yl = ry + 5 + j * 5;
        int y = py[i];
        if (!(i & 1)) {  // a LEFT pin: right to the middle, down into the gap, then across
            seg(px[i], y, (x0l + x0r) / 2, y, w[j].c);
            seg((x0l + x0r) / 2, y, (x0l + x0r) / 2, y + pitch / 2, w[j].c);
            y += pitch / 2;
            seg((x0l + x0r) / 2, y, xl, y, w[j].c);
        } else {
            seg(px[i], y, xl, y, w[j].c);
        }
        seg(xl, y, xl, yl, w[j].c);
        seg(xl, yl, w[j].x, yl, w[j].c);
        seg(w[j].x, yl, w[j].x, w[j].y, w[j].c);
    }
    for (int i = 0; i < k->rows * k->cols && i < 32; i++) {  // (the pads over the wires)
        const rfp_pad_t *p = &k->pads[i];
        bool hot = p->role != RFP_PAD_OTHER;
        if (hot) {
            tfd_disc((float)px[i], (float)py[i], 6.5f, role_col(p->role), 120);
        }
        tfd_disc((float)px[i], (float)py[i], hot ? 4.6f : 3.8f, hot ? role_col(p->role) : C_PAD, 256);
        tfd_disc((float)px[i], (float)py[i], 1.6f, rgb(10, 10, 12), 256);
        int tw = mf_width(&IR_AX, p->label, 0);
        int lxx = (i & 1) ? x0r + 10 : x0l - 10 - tw;
        if (i & 1) {
            tfd_rect(lxx - 2, py[i] - 6, tw + 4, 12, C_BG, 256);
        }
        mf_text(&IR_AX, lxx, py[i] + 4, p->label, hot ? role_col(p->role) : C_AXIS, 255, 0);
    }
    // the legend: the pad of each wire
    char b[40];
    const int roles[4] = {RFP_PAD_3V3, RFP_PAD_GND, RFP_PAD_DATA, RFP_PAD_TX};
    static const char *const RN[4] = {"VCC", "GND", "DATA", "TX"};
    for (int r = 0; r < 4; r++) {
        int i = find_role(k, roles[r]);
        if (i < 0) {
            continue;
        }
        snprintf(b, sizeof b, "%s > %s", k->pads[i].label, RN[r]);
        const int lxx = 132, lyy = 166 + r * 17;
        tfd_disc((float)lxx, (float)lyy - 4, 4, role_col(roles[r]), 256);
        mf_text(&IR_SM, lxx + 8, lyy, b, C_TEXT, 255, 0);
    }
    tfd_text_c(&KF_LABEL, 220, 146, k->where[s_he ? 1 : 0], C_DIM, 190);  // (the view: where the USB-C is)
}

// THE WIRING: the board or the box of the connector. The centre of each pad goes into pp, the box into b (x0, y0, x1,
// y1). silk: the back of the 3.49 (the PCB, the two rows of pads at the battery end, the USB-C port).
static void wire_board(const rfp_conn_t *k, int n, bool silk, padpos_t *pp, int b[4])
{
    if (silk) {
        const int bx0 = 6, by0 = 26, bx1 = BAR ? 318 : s_W - 10, by1 = by0 + (BAR ? 98 : 104);
        b[0] = bx0;
        b[1] = by0;
        b[2] = bx1;
        b[3] = by1;
        tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, 8, C_PCB, 256);
        tfd_rrect(bx0, by0, bx1 - bx0, 3, 2, C_PCB_E, 256);
        const int pitch = 24, px0 = bx0 + 22;
        for (int i = 0; i < n; i++) {
            pp->x[i] = px0 + (i % k->cols) * pitch;
            pp->y[i] = i < k->cols ? by0 + 14 : by1 - 14;
        }
        tfd_rrect(bx1 - 10, by0 + (by1 - by0) / 2 - 16, 16, 32, 5, rgb(190, 196, 206), 256);
        tfd_rrect(bx1 - 7, by0 + (by1 - by0) / 2 - 10, 10, 20, 3, rgb(60, 64, 72), 256);
        text_c(&IR_AX, bx1 - 26, by0 + (by1 - by0) / 2 + 4, "USB-C", C_TEXT);
        text_c(&IR_SM, px0 + 5 * pitch, by0 + (by1 - by0) / 2 + 4, s_he ? "גב הלוח" : "THE BACK", rgb(120, 150, 200));
        return;
    }
    const int pitch = k->cols > 10 ? (s_W - 40) / k->cols : 34;
    const int ww = pitch * k->cols;
    const int bx0 = (s_W - ww) / 2 - 8, bx1 = bx0 + ww + 16;
    const int by0 = BAR ? 40 : 56;  // (room over the socket for the wires of its top row)
    const int by1 = by0 + 2 * pitch + 6;
    b[0] = bx0;
    b[1] = by0;
    b[2] = bx1;
    b[3] = by1;
    tfd_rrect(bx0, by0, bx1 - bx0, by1 - by0, 4, rgb(24, 24, 28), 256);
    for (int i = 0; i < n; i++) {
        pp->x[i] = bx0 + 8 + pitch / 2 + (i % k->cols) * pitch;
        pp->y[i] = by0 + 3 + pitch / 2 + (i / k->cols) * pitch;
    }
    float m[6] = {(float)bx0 - 2, (float)by0 - 2, (float)bx0 + 8, (float)by0 - 2, (float)bx0 - 2, (float)by0 + 8};
    tfd_poly(m, 3, C_TEXT, 256);
}

// THE WIRING: the two modules and the six wires from the pads (b: the box of wire_board)
static void wire_modules(const rfp_conn_t *k, bool silk, const padpos_t *pp, const int b[4])
{
    const int bx0 = b[0], by0 = b[1], by1 = b[3];
    wire_t w[6];
    int rx[3], ry, lx[3], ly;
    if (BAR) {
        draw_module(false, 328, 28, 94, 52, false, rx, &ry);
        draw_module(true, 428, 28, 94, 52, false, lx, &ly);
    } else {
        draw_module(false, 10, s_H - 52, 100, 48, true, rx, &ry);
        draw_module(true, 118, s_H - 52, 100, 48, true, lx, &ly);
    }
    const int nw = make_wires(k, rx, ry, lx, ly, w);
    const int base = by1 + (BAR ? 6 : 22);
    const int half = (pp->x[1] - pp->x[0]) / 2;
    for (int j = 0; j < nw; j++) {
        const int i = w[j].pad, lane = base + j * (BAR ? 4 : 5);
        int x0 = pp->x[i], y0 = pp->y[i] + 7;
        if (i < k->cols && silk) {  // the top row of the 3.49: half a pad step right, then down in the gap
            x0 += half;
            seg(pp->x[i], pp->y[i], x0, pp->y[i], w[j].c);
            y0 = pp->y[i];
        } else if (i < k->cols) {  // the top row of a socket: up over the socket, then out at the left
            const int yt = by0 - 5 - j * 3;
            x0 = bx0 - 6 - j * 4;
            seg(pp->x[i], pp->y[i], pp->x[i], yt, w[j].c);
            seg(pp->x[i], yt, x0, yt, w[j].c);
            y0 = yt;
        }
        seg(x0, y0, x0, lane, w[j].c);
        seg(x0, lane, w[j].x, lane, w[j].c);
        seg(w[j].x, lane, w[j].x, w[j].y, w[j].c);
        tfd_disc((float)x0, (float)lane, 2.0f, w[j].c, 256);
        tfd_disc((float)w[j].x, (float)lane, 2.0f, w[j].c, 256);
    }
}

// THE WIRING of the 3.49 or of a socket in a box: the board, the modules, the wires, the pads, the view
static void wire_box(const rfp_conn_t *k)
{
    padpos_t pp;
    int b[4];
    const int n = k->rows * k->cols;
    const bool silk = k->shape == RFP_SHAPE_349;
    wire_board(k, n, silk, &pp, b);
    wire_modules(k, silk, &pp, b);
    draw_pads(k, &pp, silk);
    if (!silk) {  // (the view, on a dark box: it is over the wires)
        const int by1 = b[3];
        const char *wt = k->where[s_he ? 1 : 0];
        int tw2 = ui_text_w(&KF_LABEL, wt) + 10;
        if (tw2 > s_W - 20) {
            tw2 = s_W - 20;
        }
        tfd_rrect(s_W / 2 - tw2 / 2, by1 + 3, tw2, 16, 4, C_BG, 235);
        tfd_text_c(&KF_LABEL, s_W / 2, by1 + 15, wt, C_DIM, s_W - 24);
    }
}

// THE WIRING: the state of the pin and the buttons CONNECTED and DEMO
static void wire_state(const rfp_conn_t *k)
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
    const int sy = BAR ? s_H - 4 : 32, sx1 = s_bx[0] - 8;
    if (BAR) {
        tfd_disc(12.0f, (float)sy - 5, 5, sc, 256);
        tfd_text_s(&KF_LABEL, 22, sx1, sy, st, sc, s_he);
    } else if (k->shape == RFP_SHAPE_BUS30) {
        tfd_text_c(&KF_LABEL, 214, sy, st, sc, 196);
    } else {
        int tw = ui_text_w(&KF_LABEL, st) + 10;
        if (tw > s_W - 20) {
            tw = s_W - 20;
        }
        tfd_rrect(s_W / 2 - tw / 2, sy - 13, tw, 17, 4, C_BG, 235);
        tfd_text_c(&KF_LABEL, s_W / 2, sy, st, sc, s_W - 20);
    }
    // the buttons
    const int ty = gfx_round() ? 8 : 5;  // (the base line under the middle of a key: KF_TEXT in the round frame)
    if (src_index(false) >= 0) {
        tfd_rrect(s_bx[0], s_by[0], s_bw[0], s_bh[0], 8, s_seen ? C_GO : C_BTN, 256);
        tfd_text_c(&KF_LABEL, s_bx[0] + s_bw[0] / 2, s_by[0] + s_bh[0] / 2 + ty, TX(T_CONNECTED), C_TEXT, s_bw[0] - 6);
    }
    tfd_rrect(s_bx[1], s_by[1], s_bw[1], s_bh[1], 8, C_BTN, 256);
    tfd_text_c(&KF_LABEL, s_bx[1] + s_bw[1] / 2, s_by[1] + s_bh[1] / 2 + ty, TX(T_DEMO), C_TEXT, s_bw[1] - 6);
}

// THE WIRING
static void draw_wire_screen(void)
{
    const rfp_conn_t *k = rfp_board_ir();
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    if (gfx_round()) {  // (the round frame: the title in the top cap, the panel of the keys in the bottom cap)
        rl_cap(rl_oy, C_BG, C_PANEL, C_LINE, &KF_TEXT, TX(T_CONNECT), C_TEXT);
        rl_t_bcap(rl_t_key() - 6, C_PANEL, C_LINE);
    } else {
        tfd_text_s(&KF_LABEL, 8, BAR ? 290 : s_W - 8, 16, TX(T_CONNECT), C_TEXT,
                   s_he);  // (the 3.49: left of the panel grip)
    }
    if (k->shape == RFP_SHAPE_NONE) {
        tfd_text_c(&KF_LABEL, s_W / 2, s_H / 2, TX(T_NOPINS), C_WARN, s_W - 20);
    } else if (k->shape == RFP_SHAPE_BUS30) {
        wire_bus30(k);
    } else {
        wire_box(k);
    }
    wire_state(k);
}

// ---------------------------------------------------------------- LIVE
static uint16_t proto_col(int p)
{
    switch (p) {
    case IRD_P_NEC:
    case IRD_P_ONKYO:
    case IRD_P_APPLE:
        return rgb(70, 150, 255);
    case IRD_P_SAMSUNG:
    case IRD_P_SAMSUNG48:
        return rgb(90, 210, 220);
    case IRD_P_SONY:
        return rgb(255, 120, 120);
    case IRD_P_RC5:
    case IRD_P_RC6:
        return rgb(220, 110, 240);
    case IRD_P_LG:
        return rgb(255, 110, 170);
    case IRD_P_PULSE_DISTANCE:
    case IRD_P_PULSE_WIDTH:
        return rgb(255, 170, 60);
    case IRD_P_UNKNOWN:
        return rgb(140, 140, 150);
    default:
        return rgb(120, 220, 120);
    }
}

// a short name for the chip of a row
static const char *chip_name(int p)
{
    switch (p) {
    case IRD_P_DENON_K:
        return "DENON K";
    case IRD_P_SHARP_K:
        return "SHARP K";
    case IRD_P_JVC_K:
        return "JVC K";
    case IRD_P_MITSUBISHI_K:
        return "MITSU K";
    case IRD_P_PULSE_DISTANCE:
        return "PULSE DIST";
    case IRD_P_PULSE_WIDTH:
        return "PULSE WIDTH";
    case IRD_P_SAMSUNG48:
        return "SAMSUNG48";
    default:
        return ird_name(p);
    }
}

// the main value of a code: the address and the command, the raw data, or the hash
static void code_value(const ird_code_t *c, char *b, int n)
{
    if (c->flags & IRD_F_REPEAT && c->proto != IRD_P_JVC && c->proto != IRD_P_DENON && c->proto != IRD_P_SHARP) {
        snprintf(b, (size_t)n, "REPEAT");
    } else if (c->proto == IRD_P_BOSEWAVE || c->proto == IRD_P_WHYNTER) {
        snprintf(b, (size_t)n, "C 0x%X", (unsigned)c->command);
    } else if (ird_has_fields(c->proto)) {
        snprintf(b, (size_t)n, "A 0x%X  C 0x%X", (unsigned)c->address, (unsigned)c->command);
    } else if (c->proto == IRD_P_UNKNOWN) {
        snprintf(b, (size_t)n, "HASH 0x%08X", (unsigned)c->hash);
    } else {
        ird_raw_hex(c, b, n);
    }
}

static void draw_strip(void)
{
    const int y0 = s_strip_y0, y1 = s_strip_y1, hi = y0 + 7, lo = y1 - 7;
    tfd_rect(0, y0, s_W, y1 - y0, C_PANEL, 256);
    const float win = 160000.0f, upp = win / s_W;  // the last 160 ms (one NEC frame is 68 ms)
    for (int t = 0; t <= 160; t += 10) {
        int x = s_W - 1 - (int)(t * 1000.0f / upp);
        tfd_rect(x, y1 - 3, 1, 3, C_LINE, 256);
    }
    if (!s_open) {
        return;
    }
    float x = (float)(s_W - 1);
    int64_t span = s_st.now_us - s_raw_end;
    int lvl = s_st.level;
    float xl = x - span / upp;
    const uint16_t c = C_TXW;
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
    tfd_rect(2, y0 + 1, mf_width(&IR_AX, "160 MS", 0) + 6, 12, C_PANEL, 230);
    mf_text(&IR_AX, 5, y0 + 10, "160 MS", C_DIM, 255, 0);
}

// the round frame: the top cap of LIVE. The source and the mode, then the dot of the line, the rate and the count
static void live_cap_r(void)
{
    const int W = gfx_w, top = s_top_h;
    gfx_fill_rect(0, 0, W, top, C_PANEL);
    const int lw = rl_half(top, 2);
    gfx_fill_rect(W / 2 - lw, top, 2 * lw, 1, C_LINE);
    char b[48];
    snprintf(b, sizeof b, "%s", cur_demo() ? "DEMO" : "IR");
    const char *md = MODE_VALS[2 * s_mode];
    const int w1 = mf_width(&IR_BIG, b, 0), w2 = mf_width(&IR_MD, md, 0), x1 = W / 2 - (w1 + 12 + w2) / 2;
    mf_text(&IR_BIG, x1, W * 50 / 466, b, C_TEXT, 255, 0);
    mf_text(&IR_MD, x1 + w1 + 12, W * 50 / 466, md, C_AXIS, 255, 0);
    const uint16_t lc = !s_open                       ? C_RED
                        : s_st.line == RFP_LINE_ALIVE ? C_OK
                        : s_st.line == RFP_LINE_HIGH  ? C_WARN
                                                      : C_DIM;
    snprintf(b, sizeof b, "%u/S  %d", (unsigned)s_rate, s_nent);
    const int w = mf_width(&IR_MD, b, 0);
    tfd_disc((float)(W / 2 - w / 2 - 6), (float)(W * 77 / 466 - 5), 4.5f, lc, 256);
    mf_text(&IR_MD, W / 2 - w / 2 + 6, W * 77 / 466, b, C_AXIS, 255, 0);
}

// the round frame: LIVE in the whole circle. The rows of the codes follow the chord down to the bottom of the circle;
// then the cap and the strip of the raw marks (its own origin and width: the chord) over the rows that go up
static void draw_live_r(void)
{
    const int W = gfx_w, ly0 = s_list_y0, ly1 = s_H, rh = s_row_h;
    gfx_fill(C_BG);
    tfd_clip(0, ly0, W, ly1);
    if (!s_nent) {
        tfd_text_c(&KF_LABEL, W / 2, (ly0 + ly1) / 2, s_open ? TX(T_WAIT) : TX(T_FAIL), C_DIM, W * 7 / 10);
    }
    char v[48], d[48];
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
        const int tw = mf_width(&IR_MD, chip_name(e->c.proto), 0) + 16, cw = tw > 86 ? tw : 86;
        const uint16_t pc = proto_col(e->c.proto);
        tfd_rrect(x + 8, y + 9, cw, rh - 18, 6, pc, 70);
        text_c(&IR_MD, x + 8 + cw / 2, y + rh / 2 + 5, chip_name(e->c.proto), pc);
        code_value(&e->c, v, sizeof v);
        const int vx = x + 8 + cw + 12;
        mf_text(&IR_BIG, vx, y + rh / 2 + 7, v, C_TEXT, 255, 0);
        const int age = (int)((now_us() - e->t_last) / 1000000);
        snprintf(d, sizeof d, "x%d  %ds", e->reps, age < 0 ? 0 : age);
        const int dw = mf_width(&IR_MD, d, 0);
        if (vx + mf_width(&IR_BIG, v, 0) + 12 < x + w - 12 - dw) {  // (only when it has room)
            mf_text(&IR_MD, x + w - 12 - dw, y + rh / 2 + 5, d, e->reps > 1 ? C_TEXT : C_DIM, 255, 0);
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
    tfd_rect(0, 0, s_W, s_top_h, C_PANEL, 256);
    char b[96];
    snprintf(b, sizeof b, "%s", cur_demo() ? "DEMO" : "IR");
    mf_text(F_MD, 6, BAR ? 16 : T5 ? 28 : 18, b, C_TEXT, 255, 0);
    int x = 6 + mf_width(F_MD, b, 0) + 14;
    mf_text(F_SM, x, BAR ? 15 : T5 ? 27 : 17, MODE_VALS[2 * s_mode], C_AXIS, 255, 0);
    uint16_t lc = !s_open ? C_RED : s_st.line == RFP_LINE_ALIVE ? C_OK : s_st.line == RFP_LINE_HIGH ? C_WARN : C_DIM;
    snprintf(b, sizeof b, "%u/S  %d", (unsigned)s_rate, s_nent);
    int w = mf_width(F_SM, b, 0);
    mf_text(F_SM, s_W - 8 - w, BAR ? 15 : T5 ? 27 : 17, b, C_AXIS, 255, 0);
    tfd_disc((float)(s_W - 18 - w), (float)(BAR ? 11 : T5 ? 21 : 13), T5 ? 6.0f : 4.5f, lc, 256);
    draw_strip();
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
        char v[48], d[48];
        const int chip_w = BAR ? 112 : T5 ? 150 : 92;
        const uint16_t pc = proto_col(e->c.proto);
        tfd_rrect(10, y + 8, chip_w, s_row_h - 16, 5, pc, 70);
        text_c(F_SM, 10 + chip_w / 2, y + s_row_h / 2 + F_SM->asc / 2, chip_name(e->c.proto), pc);
        code_value(&e->c, v, sizeof v);
        mf_text(F_MD, 10 + chip_w + 10, y + s_row_h / 2 + (T5 ? 8 : 6), v, C_TEXT, 255, 0);
        if (BAR || T5) {
            if (e->c.proto == IRD_P_UNKNOWN) {
                snprintf(d, sizeof d, "%d TIMES", e->c.nraw);
            } else {
                snprintf(d, sizeof d, "%d BIT  %u KHZ", e->c.nbits, (unsigned)(e->c.carrier_hz / 1000));
            }
            mf_text(F_SM, T5 ? 400 : 360, y + s_row_h / 2 + (T5 ? 6 : 5), d, C_AXIS, 255, 0);
        }
        int age = (int)((now_us() - e->t_last) / 1000000);
        snprintf(d, sizeof d, "x%d  %ds", e->reps, age < 0 ? 0 : age);
        int dw = mf_width(F_SM, d, 0);
        mf_text(F_SM, s_W - 14 - dw, y + s_row_h / 2 + (T5 ? 6 : 5), d, e->reps > 1 ? C_TEXT : C_DIM, 255, 0);
    }
    tfd_noclip();
}

// ---------------------------------------------------------------- A CODE
// a line of text that breaks at the spaces: it returns the y of the next line
static int text_lines(const mfont_t *f, int x, int y, int maxw, int lh, const char *s, uint16_t c)
{
    char line[200];
    while (*s) {
        int n = 0, last_sp = -1;
        while (s[n] && n < (int)sizeof(line) - 1) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (mf_width(f, line, 0) > maxw) {
                break;
            }
            if (s[n] == ' ') {
                last_sp = n;
            }
            n++;
        }
        if (s[n] && last_sp > 0) {
            n = last_sp;
        }
        line[n] = 0;
        if (y > s_itext_y0 - lh && y < s_H + lh) {
            mf_text(f, x, y, line, c, 255, 0);
        }
        y += lh;
        s += n;
        while (*s == ' ') {
            s++;
        }
    }
    return y;
}

// the round frame: the head of A CODE. BACK in the top cap with the protocol and the bits under it, the value under
// the cap, the pulses of the last frame in the width of the chord, SEND in the bottom cap
static void info_head_r(const ent_t *e)
{
    const ird_code_t *c = &e->c;
    const int W = gfx_w, top = s_top_h;
    char b[96], d[48];
    gfx_fill_rect(0, 0, W, s_itext_y0, C_BG);
    gfx_fill_rect(0, 0, W, top, C_PANEL);
    const int lw = rl_half(top, 2);
    gfx_fill_rect(W / 2 - lw, top, 2 * lw, 1, C_LINE);
    const int bw = W * 104 / 466, by = W * 14 / 466;
    tfd_rrect(W / 2 - bw / 2, by, bw, 36, 18, C_BTN, 256);
    tfd_text_c(&KF_LABEL, W / 2, by + 25, TX(T_BACK), C_TEXT, bw - 8);
    snprintf(b, sizeof b, "%s", ird_name(c->proto));
    if (c->proto == IRD_P_UNKNOWN) {
        snprintf(d, sizeof d, "%d TIMES  x%d", c->nraw, e->reps);
    } else {
        snprintf(d, sizeof d, "%d BIT  x%d", c->nbits, e->reps);
    }
    const int w1 = mf_width(&IR_BIG, b, 0), w2 = mf_width(&IR_MD, d, 0), x1 = W / 2 - (w1 + 12 + w2) / 2;
    mf_text(&IR_BIG, x1, top - 10, b, proto_col(c->proto), 255, 0);
    mf_text(&IR_MD, x1 + w1 + 12, top - 10, d, C_AXIS, 255, 0);
    code_value(c, b, sizeof b);
    const mfont_t *vf = mf_width(&IR_BIG, b, 0) <= 2 * rl_half(top + 10, 16) ? &IR_BIG : &IR_MD;
    mf_text_c(vf, W / 2, top + 28, b, C_TEXT, 255, 0);
    // the pulses of the last frame, the whole frame in the width of the chord
    const int wy0 = top + 38, wy1 = s_itext_y0 - 8, hi = wy0 + 6, lo = wy1 - 6;
    int x, w;
    rl_t_row(wy0, wy1 - wy0, wy0, wy1, 12, &x, &w);
    tfd_rrect(x, wy0, w, wy1 - wy0, 8, C_PANEL, 256);
    double tot = 0;
    for (int i = 0; i < e->n; i++) {
        tot += abs(e->d[i]);
    }
    if (tot > 0) {
        const double kx = (w - 20) / tot;
        double xx = x + 10;
        for (int i = 0; i < e->n; i++) {
            const double xn = xx + abs(e->d[i]) * kx;
            const int yy = e->d[i] > 0 ? hi : lo;
            tfd_line((float)xx, (float)yy, (float)xn, (float)yy, 2.0f, C_TXW, 256);
            if (i + 1 < e->n) {
                tfd_line((float)xn, (float)hi, (float)xn, (float)lo, 1.2f, C_TXW, 200);
            }
            xx = xn;
        }
    }
    if (can_send()) {
        const int ky = rl_t_key(), kh = rl_t_kh();
        int kx, kw;
        rl_t_keyx(0, 1, &kx, &kw);
        rl_t_bcap(ky - 6, C_PANEL, C_LINE);
        tfd_rrect(kx, ky, kw, kh, 10, rgb(110, 50, 140), 256);
        tfd_text_c(&KF_LABEL, kx + kw / 2, ky + kh / 2 + 8, TX(T_SEND), C_TEXT, kw - 8);
    }
}

static void draw_info(void)
{
    const bool rd = gfx_round();
    const int W0 = s_W;
    if (rd) {  // (the round frame: the text in a middle column of the band, then the head of the round frame)
        gfx_fill(C_BG);
        s_W = 2 * rl_half(s_H, 10);
        rl_ox = gfx_w / 2 - s_W / 2;
    }
    tfd_rect(0, 0, s_W, s_H, C_BG, 256);
    if (s_sel < 0 || s_sel >= s_nent) {
        s_scr = SC_LIVE;
        s_W = W0;
        rl_ox = 0;
        return;
    }
    const ent_t *e = &s_ent[s_sel];
    const ird_code_t *c = &e->c;
    char b[320], h[100];
    // the text under the pulses (it scrolls); first, so that the top stays over it
    const mfont_t *fsm = rd ? &IR_MD : F_SM;  // (the round frame: the bigger mono face)
    const int lh = rd ? 22 : BAR ? 17 : T5 ? 26 : 19, x0 = 8, tw = s_W - 16;
    int y = s_itext_y0 + 14 - (int)s_iscroll;
    tfd_clip(0, s_itext_y0, s_W, s_H);
    if (c->proto != IRD_P_UNKNOWN && c->nbits) {
        ird_raw_hex(c, h, sizeof h);
        snprintf(b, sizeof b, "RAW DATA %s  (%s FIRST)", h, c->flags & IRD_F_MSB ? "MSB" : "LSB");
        y = text_lines(fsm, x0, y, tw, lh, b, C_TEXT);
        ird_binary(c, b, sizeof b);
        y = text_lines(fsm, x0, y, tw, lh, b, C_AXIS);
    }
    if (c->proto == IRD_P_UNKNOWN) {
        snprintf(b, sizeof b, "HASH 0x%08X  %d TIMES  (NO KNOWN PROTOCOL)", (unsigned)c->hash, c->nraw);
        y = text_lines(fsm, x0, y, tw, lh, b, C_TEXT);
    } else {
        int o = 0;
        if (c->hdr_mark) {
            o += snprintf(b + o, sizeof b - (size_t)o, "HEADER %u / %u   ", c->hdr_mark, c->hdr_space);
        }
        o += snprintf(b + o, sizeof b - (size_t)o, "ONE %u / %u   ZERO %u / %u   %u KHZ", c->one_mark, c->one_space,
                      c->zero_mark, c->zero_space, (unsigned)(c->carrier_hz / 1000));
        y = text_lines(fsm, x0, y, tw, lh, b, C_AXIS);
        o = 0;
        b[0] = 0;
        if (c->proto >= IRD_P_PANASONIC && c->proto <= IRD_P_MITSUBISHI_K) {
            o += snprintf(b + o, sizeof b - (size_t)o, "VENDOR 0x%04X   ", (unsigned)c->extra);
        }
        if (c->proto == IRD_P_RC6) {
            o += snprintf(b + o, sizeof b - (size_t)o, "MODE %u   ", (unsigned)c->extra);
        }
        if (c->flags & IRD_F_TOGGLE) {
            o += snprintf(b + o, sizeof b - (size_t)o, "TOGGLE   ");
        }
        if (c->flags & IRD_F_REPEAT) {
            o += snprintf(b + o, sizeof b - (size_t)o, "REPEAT FRAME   ");
        }
        if (c->flags & IRD_F_PARITY) {
            o += snprintf(b + o, sizeof b - (size_t)o, "THE CHECK FAILED   ");
        }
        o += snprintf(b + o, sizeof b - (size_t)o, "HASH 0x%08X", (unsigned)c->hash);
        y = text_lines(fsm, x0, y, tw, lh, b, C_AXIS);
    }
    // the raw times, as IRremote prints them (+ a mark, - a space)
    y += 4;
    if (y > s_itext_y0 - lh && y < s_H + lh) {
        tfd_text_s(F_LB, x0, s_W - 8, y, TX(T_RAW_TIMES), C_DIM, s_he);
    }
    y += lh + 2;
    {
        int o = 0;
        b[0] = 0;
        static char raw[IRD_MAX_DUR * 8];
        for (int i = 0; i < e->n && o < (int)sizeof(raw) - 10; i++) {
            o += snprintf(raw + o, sizeof raw - (size_t)o, "%+d ", (int)e->d[i]);
        }
        y = text_lines(fsm, x0, y, tw, lh, raw, C_TEXT);
    }
    s_itext_h = y + (int)s_iscroll - s_itext_y0 + 8;
    tfd_noclip();
    if (rd) {
        s_W = W0;
        rl_ox = 0;
        info_head_r(e);
        return;
    }
    // the top: BACK, the protocol, the value, SEND
    tfd_rect(0, 0, s_W, s_itext_y0, C_BG, 256);
    tfd_rrect(4, 4, s_back_w, s_back_h, 8, C_BTN, 256);
    tfd_text_c(F_LB, 4 + s_back_w / 2, T5 ? 35 : 27, TX(T_BACK), C_TEXT, s_back_w - 4);
    const int nx = s_back_w + 16;  // (the protocol and the value: at the right of BACK)
    const int right = can_send() ? s_send_x - 8 : s_W - 8;
    tfd_clip(0, 0, right, s_itext_y0);
    snprintf(b, sizeof b, "%s",
             mf_width(F_MD, ird_name(c->proto), 0) < right - nx ? ird_name(c->proto) : chip_name(c->proto));
    mf_text(F_MD, nx, T5 ? 26 : 20, b, proto_col(c->proto), 255, 0);
    int x = nx + mf_width(F_MD, b, 0) + 12;
    if (c->proto == IRD_P_UNKNOWN) {
        snprintf(b, sizeof b, "%d TIMES  x%d", c->nraw, e->reps);
    } else {
        snprintf(b, sizeof b, "%d BIT  x%d", c->nbits, e->reps);
    }
    if (x + mf_width(F_SM, b, 0) < right) {
        mf_text(F_SM, x, T5 ? 26 : 20, b, C_AXIS, 255, 0);
    }
    code_value(c, b, sizeof b);
    // (a long value: the smaller face; the RAW DATA line under the pulses has the full value)
    const mfont_t *vf = mf_width(&IR_BIG, b, 0) <= right - nx ? &IR_BIG : &IR_MD;
    mf_text(vf, nx, T5 ? 56 : 40, b, C_TEXT, 255, 0);
    tfd_noclip();
    if (can_send()) {
        tfd_rrect(s_send_x, s_send_y, s_send_w, s_send_h, 8, rgb(110, 50, 140), 256);
        tfd_text_c(F_LB, s_send_x + s_send_w / 2, s_send_y + s_send_h / 2 + 5, TX(T_SEND), C_TEXT, s_send_w - 6);
    }
    // the pulses of the last frame, the whole frame in the width
    const int wy0 = T5 ? 66 : 46, wy1 = s_itext_y0 - 2, hi = wy0 + 4, lo = wy1 - 4;
    tfd_rect(0, wy0, s_W, wy1 - wy0, C_PANEL, 256);
    double tot = 0;
    for (int i = 0; i < e->n; i++) {
        tot += abs(e->d[i]);
    }
    if (tot > 0) {
        const double kx = (s_W - 16) / tot;
        double xx = 8;
        for (int i = 0; i < e->n; i++) {
            double xn = xx + abs(e->d[i]) * kx;
            int yy = e->d[i] > 0 ? hi : lo;
            tfd_line((float)xx, (float)yy, (float)xn, (float)yy, 1.6f, C_TXW, 256);
            if (i + 1 < e->n) {
                tfd_line((float)xn, (float)hi, (float)xn, (float)lo, 1.0f, C_TXW, 200);
            }
            xx = xn;
        }
    }
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
// the menu icon: a remote control: its light flashes and infrared waves go out; the decoded pulses scroll
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_box_t b = tfi_tile(x, y, s, tfi_hex(0xff7aa8), tfi_hex(0xa3134e));
    float u = s / 100.0f, ph = tfi_ph(2);
    // the waves
    tfd_clip(b.x, b.y, b.x + b.w, b.y + b.h);
    for (int k = 0; k < 3; k++) {
        float q = ph * 2 + k / 3.0f;
        q -= (float)(int)q;
        tfi_arc(x + 34 * u, y + 30 * u, (8 + q * 40) * u, 3 * u, -1.25f, 0.1f, tfi_hex(0xffd6ec),
                tfi_qa((int)(256 * (1 - q)), 3));
    }
    tfd_clip(x, y, x + s, y + s);
    // the remote
    int rx = x + (int)(20 * u), ry = y + (int)(28 * u), rw = (int)(24 * u), rh = (int)(56 * u);
    tfi_drop(rx, ry, rw, rh, (int)(10 * u), (int)(3 * u), 90);
    tfi_gloss_box(rx, ry, rw, rh, (int)(10 * u), tfi_hex(0x4a4e60), tfi_hex(0x15171f));
    bool fl = fsin(ph * 25.13f) > 0;
    if (fl) {
        tfi_disc(x + 32 * u, y + 31 * u, 7 * u, tfi_hex(0xff5ab0), tfi_hex(0xff5ab0), 150);
    }
    tfi_disc(x + 32 * u, y + 31 * u, 3.4f * u, fl ? tfi_hex(0xffd0ea) : tfi_hex(0x80204a),
             fl ? tfi_hex(0xffd0ea) : tfi_hex(0x80204a), 256);
    static const uint32_t BC[4] = {0xff5470, 0x5dffb0, 0xffd84a, 0x5ab0ff};
    for (int k = 0; k < 4; k++) {
        tfi_disc(x + (27 + (k & 1) * 10) * u, y + (46 + (k >> 1) * 9) * u, 3 * u, tfi_hex(BC[k]), tfi_hex(BC[k]), 256);
    }
    for (int k = 0; k < 2; k++) {
        tfi_rbox(rx + (int)(5 * u), y + (int)((66 + k * 7) * u), rw - (int)(10 * u), (int)(4 * u), (int)(2 * u),
                 tfi_hex(0x8a90a4), tfi_hex(0x8a90a4), 256);
    }
    // the pulses (a square wave that scrolls)
    int wx0 = x + (int)(52 * u), wx1 = x + (int)(86 * u), wy = y + (int)(76 * u);
    for (int xx = wx0; xx < wx1; xx++) {
        float v = (xx - x) / u + ph * 30;  // (the pattern: 30 units; one pattern in the loop)
        int bit = ((int)(v / 6) * 7 + 3) % 5 < 2;
        tfd_rect(xx, bit ? wy - (int)(8 * u) : wy, 1, (int)(2.4f * u) > 1 ? (int)(2.4f * u) : 1, rgb(255, 255, 255),
                 256);
    }
    tfi_end();
}
