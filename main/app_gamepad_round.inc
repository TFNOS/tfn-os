// GAMEPAD on a round panel (BOARD_ROUND, APP_ROUND_HI): the drawing of the round layout (the values in app_gamepad.c,
// the part BOARD_ROUND). The top cap has the link state and the profile, the middle band the link help, the controls
// fill the wide middle of the circle, the bottom cap has SELECT and START or the web line. The text uses the smooth
// fonts (uikit.h). Only app_gamepad.c includes this file.

// The middle of the frame and a text line in the middle.
#define GR_CX (gfx_w / 2)
static void gr_line(const kfont_t *f, int base, const char *s, uint16_t c)
{
    ui_text_c(f, GR_CX, base, s, c);
}

// The width of the circle at the height y (the chord), less a margin at each end.
static int gr_chord(int y, int margin)
{
    int r = gfx_w / 2, dy = y - gfx_h / 2;
    if (dy * dy >= r * r) {
        return 0;
    }
    return 2 * (int)sqrtf((float)(r * r - dy * dy)) - 2 * margin;
}

// The largest font (of f, then KF_TEXT, then KF_LABEL) that puts s in w px.
static const kfont_t *gr_fit(const kfont_t *f, const char *s, int w)
{
    if (ui_text_w(f, s) <= w) {
        return f;
    }
    return ui_text_w(&KF_TEXT, s) <= w ? &KF_TEXT : &KF_LABEL;
}

// A box with a word in the middle (round corners).
static void gr_button(int x, int y, int w, int h, const char *s, uint16_t fill, uint16_t tc, const kfont_t *f)
{
    ui_rrect(x, y, w, h, h / 3, fill);
    f = gr_fit(f, s, w - 12);
    ui_text_c(f, x + w / 2, y + h / 2 + f->asc / 2, s, tc);
}

// The top cap: the link state (the rune, a lamp, a word), then the profile box (BUTTONS: < name >; the tilt modes:
// the name and an arrow down = a tap opens the panel).
static void draw_top(void)
{
    gfx_fill_rect(0, 0, SCR_W, TOP_H, rgb(16, 18, 40));
    gfx_fill_rect(0, TOP_H - 1, SCR_W, 1, rgb(50, 56, 100));
    pad_state_t st = bt_pad_state();
    uint16_t dot;
    const char *s;
    switch (st) {
    case PAD_READY:
        dot = rgb(80, 255, 120);
        s = "CONNECTED";
        break;
    case PAD_CONNECTED:
        dot = rgb(255, 210, 60);
        s = "PAIRING";
        break;
    case PAD_ADVERTISING:
        dot = ((s_now / 400) & 1) ? rgb(80, 160, 255) : rgb(30, 60, 110);
        s = "VISIBLE";
        break;
    case PAD_FAILED:
        dot = rgb(255, 70, 70);
        s = "BT ERROR";
        break;
    default:
        dot = rgb(120, 120, 140);
        s = "STARTING";
        break;
    }
    int cy = GR(36), sw = ui_text_w(&KF_TEXT, s), x0 = GR_CX - (sw + GR(44)) / 2;
    bt_rune(x0 + 5, cy, 1, rgb(110, 170, 255));
    gfx_fill_circle(x0 + GR(26), cy, GR(6), dot);
    ui_text(&KF_TEXT, x0 + GR(44), cy + KF_TEXT.asc / 2, s, c_text);

    bool tilt = mode_now() != PAD_MODE_BUTTONS;
    char t[32];
    snprintf(t, sizeof(t), "%d. %s", pad_active() + 1, s_p.name);
    int by = GR(58), bh = GR(46), bw = NEXT_X - PREV_X;
    ui_rbox(PREV_X, by, bw, bh, bh / 2, rgb(30, 34, 66), rgb(60, 70, 120), 1);
    int nw = bw - 2 * ARROW_W;
    const kfont_t *f = gr_fit(&KF_BIG, t, nw);
    ui_text_c(f, GR_CX, by + bh / 2 + f->asc / 2, t, c_text);
    if (!tilt) {
        ui_text_c(&KF_BIG, PREV_X + ARROW_W / 2, by + bh / 2 + KF_BIG.asc / 2, "<", c_accent);
        ui_text_c(&KF_BIG, NEXT_X - ARROW_W / 2, by + bh / 2 + KF_BIG.asc / 2, ">", c_accent);
        if (no_tilt()) {
            gr_line(&KF_TEXT, TOP_H + GR(4) + KF_TEXT.asc, "NO TILT: BUTTONS ONLY", rgb(255, 120, 120));
        }
        return;
    }
    // the arrow down at the right end: the panel (MENU)
    int ax = NEXT_X - ARROW_W / 2, ay = by + bh / 2 - GR(4);
    for (int j = 0; j < GR(8); j++) {
        gfx_fill_rect(ax - GR(8) + j, ay + j, 2 * (GR(8) - j) + 1, 1, rgb(160, 220, 255));
    }
}

// the pull-down panel: SELECT and START (big), CALIBRATE, the profile, CLOSE (all inside the circle)
static void draw_drawer(void)
{
    if (s_drawer <= 0.01f && !s_pulling) {
        return;
    }
    int dy = drawer_dy();
    int h = SCR_H + dy;
    if (h < 2) {
        h = 2;
    }
    gfx_fill_rect(0, 0, SCR_W, h, rgb(14, 16, 36));
    gfx_fill_rect(0, h - 2, SCR_W, 2, rgb(80, 100, 170));
    bool tilt = mode_now() != PAD_MODE_BUTTONS;
    uint16_t on = rgb(255, 220, 60);
    bool sel = s_screen & bit(BTN_SELECT), sta = s_screen & bit(BTN_START);
    gr_line(&KF_TEXT, DR_SS_Y + dy - GR(14), "HOLD TO PRESS", c_dim);
    gr_button(DR_SEL_X, DR_SS_Y + dy, DR_SS_W, DR_SS_H, "SELECT", sel ? on : rgb(52, 58, 92), sel ? 0 : c_text,
              &KF_BIG);
    gr_button(DR_STA_X, DR_SS_Y + dy, DR_SS_W, DR_SS_H, "START", sta ? on : rgb(52, 58, 92), sta ? 0 : c_text, &KF_BIG);
    if (tilt) {
        int cw = cal_w();
        gr_button(DR_CAL_X, DR_CAL_Y + dy, cw, DR_CAL_H, "CALIBRATE", rgb(36, 60, 100), rgb(160, 220, 255), &KF_BIG);
        if (mode_now() == PAD_MODE_MOVE) {
            int sx = DR_CAL_X + cw + 4, sw = DR_CAL_W - cw - 4;
            gr_button(sx, DR_CAL_Y + dy, sw, DR_CAL_H, s_p.mstyle == PAD_MOVE_LATCH ? "LATCH" : "HOLD",
                      rgb(60, 44, 100), rgb(255, 210, 120), &KF_TEXT);
        }
    }
    // < profile > (each half is an arrow)
    char t[32];
    snprintf(t, sizeof(t), "%d. %s", pad_active() + 1, s_p.name);
    int py = DR_PRO_Y + dy, mid = py + DR_PRO_H / 2;
    ui_rrect(DR_PRO_X, py, DR_PRO_W, DR_PRO_H, DR_PRO_H / 3, rgb(30, 34, 66));
    ui_text_c(&KF_BIG, DR_PRO_X + GR(16), mid + KF_BIG.asc / 2, "<", c_accent);
    ui_text_c(&KF_BIG, DR_PRO_X + DR_PRO_W - GR(16), mid + KF_BIG.asc / 2, ">", c_accent);
    const kfont_t *f = gr_fit(&KF_TEXT, t, DR_PRO_W - GR(64));
    ui_text_c(f, DR_PRO_X + DR_PRO_W / 2, mid + f->asc / 2, t, c_text);
    // CLOSE: the bottom of the circle
    gfx_fill_rect(0, DR_CLOSE_Y + dy, SCR_W, SCR_H - DR_CLOSE_Y - 2, rgb(24, 28, 56));
    gr_line(&KF_BIG, DR_CLOSE_Y + dy + GR(60), "CLOSE", rgb(160, 220, 255));
    gr_line(&KF_TEXT, DR_CLOSE_Y + dy + GR(92), "(OR DRAG UP)", c_dim);
}

// the help text of the link: up to three lines from the baseline y (the middle band of the circle)
static void draw_link_help(int x, int w, int y)
{
    (void)x;
    (void)w;
    char t[48];
    int lh = KF_TEXT.line - 2;
    switch (bt_pad_state()) {
    case PAD_ADVERTISING:
        gr_line(&KF_TEXT, y, "OPEN BLUETOOTH ON YOUR", c_dim);
        gr_line(&KF_TEXT, y + lh, "COMPUTER OR PHONE AND CHOOSE", c_dim);
        snprintf(t, sizeof(t), "%s", bt_pad_name());
        gr_line(&KF_TEXT, y + 2 * lh, t, rgb(120, 190, 255));
        break;
    case PAD_CONNECTED:
        gr_line(&KF_TEXT, y + lh / 2, "PAIRING... ACCEPT IT ON THE", c_dim);
        gr_line(&KF_TEXT, y + lh * 3 / 2, "COMPUTER OR PHONE", c_dim);
        break;
    case PAD_READY:
        snprintf(t, sizeof(t), "REPORTS %lu", (unsigned long)bt_pad_sent());
        gr_line(&KF_TEXT, y + lh, t, rgb(90, 140, 110));
        break;
    case PAD_FAILED:
        gr_line(&KF_TEXT, y + lh / 2, "BLUETOOTH DID NOT START", rgb(255, 120, 120));
        gr_line(&KF_TEXT, y + lh * 3 / 2, "(NOT ENOUGH MEMORY?)", c_dim);
        break;
    default:
        gr_line(&KF_TEXT, y + lh, "STARTING BLUETOOTH...", c_dim);
        break;
    }
}

// the web line on the bottom cap (BUTTONS: SELECT and START are there, so the line is at the bottom edge)
static void draw_web_line(int x, int w)
{
    (void)x;
    (void)w;
    char t[48];
    int y = mode_now() == PAD_MODE_BUTTONS ? SS_Y + SS_H + GR(30) : WEB_Y;
    int cw = gr_chord(y - KF_LABEL.asc, GR(8));
    if (wifi_web_ip()) {
        snprintf(t, sizeof(t), "WEB SETUP: http://%s/pad", wifi_web_ip());
        if (ui_text_w(&KF_LABEL, t) > cw) {
            snprintf(t, sizeof(t), "http://%s/pad", wifi_web_ip());
        }
        gr_line(gr_fit(&KF_TEXT, t, cw), y, t, rgb(110, 120, 170));
    } else {
        gr_line(gr_fit(&KF_TEXT, "WEB: WIFI OR HOTSPOT", cw), y, "WEB: WIFI OR HOTSPOT", rgb(80, 86, 130));
    }
}

// RETRO and RETRO MOVE: the lamps and the tilt at the left, the joystick in the middle, FIRE at the right
static void draw_retro(void)
{
    draw_compass(DPAD_X, DPAD_Y, GR(72));
    draw_stick(DPAD_X, DPAD_Y, GR(46), s_sx, s_sy, stick_ring(), s_dir >= 0 ? rgb(255, 220, 60) : rgb(150, 160, 200));
    if (mode_now() == PAD_MODE_MOVE) {
        ui_text_c(&KF_TEXT, DPAD_X, DPAD_Y + GR(100), s_p.mstyle == PAD_MOVE_LATCH ? "LATCH" : "HOLD",
                  rgb(255, 210, 120));
    }
    // the joystick, it leans to the direction
    int bx = GR_CX, by = DPAD_Y + GR(56);
    gfx_fill_ellipse(bx, by + 4, GR(44), GR(12), rgb(8, 8, 14));
    gfx_fill_ellipse(bx, by, GR(42), GR(12), rgb(40, 40, 46));
    gfx_fill_ellipse(bx, by - 1, GR(36), GR(8), rgb(24, 24, 28));
    gfx_fill_circle(bx - GR(30), by - 2, GR(7), s_fire ? rgb(255, 80, 60) : rgb(170, 30, 30));
    int kx = bx + (int)(s_jx * GR(28)), ky = by - GR(52) + (int)(s_jy * GR(12));
    line(bx, by - 2, kx, ky + 6, GR(6), rgb(30, 30, 34));
    line(bx, by - 2, kx, ky + 6, 2, rgb(70, 70, 80));
    gfx_fill_circle(kx, ky, GR(12), rgb(20, 20, 24));
    gfx_fill_circle(kx - 3, ky - 3, GR(4), rgb(110, 110, 120));
    draw_link_help(0, SCR_W, HELP_Y);
    // FIRE
    bool f = s_fire;
    int r = GR(54);
    uint16_t fc = f ? rgb(255, 90, 60) : rgb(140, 30, 30);
    if (f) {
        gfx_fill_circle(ABXY_X, ABXY_Y, r + GR(10), rgb(90, 20, 20));
    }
    gfx_fill_circle(ABXY_X, ABXY_Y + 5, r, rgb(60, 10, 10));
    gfx_fill_circle(ABXY_X, ABXY_Y, r, fc);
    gfx_fill_circle(ABXY_X - r * 3 / 10, ABXY_Y - r * 3 / 10, r / 4, f ? rgb(255, 180, 160) : rgb(200, 70, 70));
    ui_text_c(&KF_BIG, ABXY_X, ABXY_Y + KF_BIG.asc / 2, "FIRE", c_text);
    char t[24];
    if (s_p.autofire) {
        snprintf(t, sizeof(t), "AUTO %d/S", s_p.autofire);
    } else {
        snprintf(t, sizeof(t), "TOUCH ANYWHERE");
    }
    ui_text_c(&KF_TEXT, ABXY_X - GR(10), ABXY_Y + r + GR(30), t, c_dim);
}

static void face_button(int x, int y, const char *l, uint16_t c, bool on)
{
    if (on) {
        ui_circle(x, y, ABXY_R + 6, rgb(255, 255, 255), 255);
    }
    ui_circle(x + 1, y + 3, ABXY_R, rgb(10, 10, 18), 255);
    ui_circle(x, y, ABXY_R, on ? c : rgb(40, 44, 60), 255);
    ui_ring(x, y, ABXY_R - 2, 3, c, 255);
    ui_text_c(&KF_BIG, x, y + KF_BIG.asc / 2, l, on ? rgb(0, 0, 0) : c);
}

static void pill(int x, int y, const char *l, bool on)
{
    ui_rrect(x, y, SS_W, SS_H, SS_H / 2, on ? rgb(255, 220, 60) : rgb(50, 54, 76));
    const kfont_t *f = gr_fit(&KF_TEXT, l, SS_W - 8);
    ui_text_c(f, x + SS_W / 2, y + SS_H / 2 + f->asc / 2, l, on ? rgb(0, 0, 0) : c_text);
}

// BUTTONS: the body of a pad across the middle of the circle, the d-pad, A B X Y, SELECT and START below
static void draw_buttons_mode(void)
{
    uint16_t body = rgb(30, 32, 56);
    gfx_fill_ellipse(DPAD_X, DPAD_Y + 8, BODY_RX, BODY_RY, body);
    gfx_fill_ellipse(ABXY_X, ABXY_Y + 8, BODY_RX, BODY_RY, body);
    gfx_fill_rect(DPAD_X, BODY_Y, ABXY_X - DPAD_X, BODY_H, body);
    gfx_fill_rect(DPAD_X, BODY_Y, ABXY_X - DPAD_X, 3, rgb(56, 60, 96));
    draw_dpad();
    uint16_t on = s_rep.buttons;
    face_button(ABXY_X, ABXY_Y - ABXY_OFF, "Y", rgb(250, 210, 60), on & bit(BTN_Y));
    face_button(ABXY_X - ABXY_OFF, ABXY_Y, "X", rgb(80, 150, 255), on & bit(BTN_X));
    face_button(ABXY_X + ABXY_OFF, ABXY_Y, "B", rgb(255, 80, 80), on & bit(BTN_B));
    face_button(ABXY_X, ABXY_Y + ABXY_OFF, "A", rgb(90, 220, 110), on & bit(BTN_A));
    pill(SEL_X, SS_Y, "SELECT", on & bit(BTN_SELECT));
    pill(STA_X, SS_Y, "START", on & bit(BTN_START));
    draw_link_help(0, SCR_W, HELP_Y);
}

// the button of a touch half (MOTION, HALVES): "B<n>" big and TOUCH under it, at the top of the half
static void gr_half(int cx, int b, bool lit)
{
    char t[8];
    if (b) {
        snprintf(t, sizeof(t), "B%d", b);
    } else {
        snprintf(t, sizeof(t), "-");
    }
    int y = GR(176);
    ui_text_c(&KF_TITLE, cx, y, t, lit ? c_accent : rgb(90, 100, 150));
    ui_text_c(&KF_TEXT, cx, y + GR(26), "TOUCH", c_dim);
}

// MOTION: the stick (the tilt) at the left, the touch at the right, the gestures in the middle
static void draw_motion(void)
{
    uint16_t on = s_rep.buttons;
    char t[24];
    int gx = GR_CX, gy = GR(180);  // the gesture list (the middle column)
    if (s_p.touch == PAD_TOUCH_HALVES) {
        // the two halves of the circle are buttons
        int hy = TOP_H, hh = SCR_H - TOP_H;
        bool l = s_p.bl && (on & bit(s_p.bl)), r = s_p.br && (on & bit(s_p.br));
        if (l) {
            gfx_fill_rect(0, hy, SCR_W / 2, hh, rgb(30, 50, 80));
        }
        if (r) {
            gfx_fill_rect(SCR_W / 2, hy, SCR_W / 2, hh, rgb(30, 50, 80));
        }
        gfx_fill_rect(SCR_W / 2, hy + 6, 1, hh - 12, rgb(60, 66, 110));
        gr_half(GR(150), s_p.bl, l);
        gr_half(GR(316), s_p.br, r);
        gx = GR(316);
        gy = GR(250);
    } else if (s_p.touch == PAD_TOUCH_PAD) {
        int px = GR(276), py = GR(150), pw = GR(150), ph = GR(170);
        ui_rbox(px, py, pw, ph, GR(14), rgb(24, 28, 54), rgb(60, 70, 120), 1);
        ui_text_c(&KF_LABEL, px + pw / 2, py + GR(20), "TOUCHPAD", c_dim);
        int cx = px + pw / 2, cy = py + ph / 2 + GR(8);
        gfx_fill_rect(cx - pw / 2 + GR(14), cy, pw - GR(28), 1, rgb(50, 56, 96));
        gfx_fill_rect(cx, cy - ph / 2 + GR(30), 1, ph - GR(44), rgb(50, 56, 96));
        gfx_fill_circle(cx + (int)(s_rx * (pw / 2 - GR(16))), cy + (int)(s_ry * (ph / 2 - GR(30))), GR(11),
                        s_tp_on ? c_accent : rgb(90, 100, 150));
        if (s_p.bl) {
            snprintf(t, sizeof(t), "TAP = B%d", s_p.bl);
            ui_text_c(&KF_TEXT, cx, py + ph + GR(24), t, c_dim);
        }
    } else {
        if (s_p.bl) {
            snprintf(t, sizeof(t), "TOUCH = B%d", s_p.bl);
        } else {
            snprintf(t, sizeof(t), "TOUCH: -");
        }
        ui_text_c(&KF_BIG, GR(350), GR(250), t, (s_p.bl && (on & bit(s_p.bl))) ? c_accent : rgb(90, 100, 150));
        ui_text_c(&KF_TEXT, GR(350), GR(278), "ANYWHERE", c_dim);
    }
    draw_stick(DPAD_X, GR(250), GR(62), s_sx, s_sy, stick_ring(), rgb(90, 220, 255));
    snprintf(t, sizeof(t), "%+.0f %+.0f DEG", s_deg_x, s_p.usey ? s_deg_y : 0.0f);
    ui_text_c(&KF_TEXT, DPAD_X + GR(10), GR(340), t, c_dim);
    // the gesture list
    static const char *const GN[7] = {"UP", "DOWN", "LEFT", "RIGHT", "KNOCK", "SHAKE", "SNAP"};
    const uint8_t gb[7] = {s_p.gu, s_p.gd, s_p.gl, s_p.gr, s_p.gk, s_p.gs, s_p.gn};
    for (int i = 0; i < 7; i++) {
        if (!gb[i]) {
            continue;
        }
        snprintf(t, sizeof(t), "%s B%d", GN[i], gb[i]);
        bool lit = s_gest_until[gb[i]] > s_now;
        ui_text_c(&KF_LABEL, gx, gy, t, lit ? c_accent : rgb(100, 110, 160));
        gy += KF_LABEL.line;
    }
    if (s_p.touch != PAD_TOUCH_HALVES) {
        draw_link_help(0, SCR_W, HELP_Y);
    }
}

// FORGET ALL PAIRED ...? (YES at the left, NO at the right; the hit of YES is in gp_input)
static void draw_confirm(void)
{
    gfx_darken_rect(0, 0, SCR_W, SCR_H, 2);
    ui_rbox(GR(53), GR(140), GR(360), GR(196), GR(20), rgb(36, 22, 40), rgb(255, 110, 110), 2);
    gr_line(&KF_BIG, GR(190), "FORGET ALL PAIRED", c_text);
    gr_line(&KF_TEXT, GR(224), "COMPUTERS AND PHONES?", c_text);
    gr_button(GR(93), GR(256), GR(130), GR(60), "YES", rgb(200, 50, 60), c_text, &KF_BIG);
    gr_button(GR(243), GR(256), GR(130), GR(60), "NO", rgb(60, 66, 100), c_text, &KF_BIG);
}

// A box of the notes in the middle band of the circle (y = its top).
static void gr_note_box(int y, int h, uint16_t fill)
{
    int w = gr_chord(y + h, GR(14));
    ui_rrect(GR_CX - w / 2, y, w, h, GR(12), fill);
}

// The notes over the play area: the pop text, the calibration steps, the calibration fault, READY.
static void gp_round_notes(int64_t now)
{
    if (s_pop_at && now - s_pop_at < 900) {
        int w = ui_text_w(&KF_TEXT, s_pop) + GR(28), y = TOP_H + GR(8) + (int)((now - s_pop_at) / 60);
        ui_rrect(GR_CX - w / 2, y, w, GR(34), GR(10), rgb(20, 22, 44));
        gr_line(&KF_TEXT, y + GR(17) + KF_TEXT.asc / 2, s_pop, s_pop_c);
    }
    if (s_cal != CAL_OFF) {
        // the calibration: a big prompt for each step, with a progress bar
        int h = GR(110), y = gfx_h / 2 - h / 2;
        gr_note_box(y, h, rgb(20, 44, 80));
        uint16_t c = ((now / 300) & 1) ? rgb(255, 230, 90) : rgb(255, 255, 255);
        bool still = s_cal == CAL_SETTLE, mv = mode_now() == PAD_MODE_MOVE;
        const char *l1 = still ? (s_cal_ms < SETTLE_MS ? "TAKE YOUR POSE..." : "HOLD STILL")
                         : mv  ? "MOVE IT FORWARD (5 CM)"
                               : "TILT IT FORWARD";
        gr_line(&KF_TEXT, y + GR(22), still ? "STEP 1 OF 2" : "STEP 2 OF 2", rgb(160, 220, 255));
        gr_line(gr_fit(&KF_BIG, l1, gr_chord(y, GR(24))), y + GR(52), l1, c);
        gr_line(&KF_TEXT, y + GR(78), still ? "WAIT FOR THE BEEP" : "AWAY FROM YOU = UP", rgb(160, 220, 255));
        int ms = s_cal_ms, full = still ? SETTLE_MS : CAL_GEST_MS, bw = gr_chord(y + h, GR(60));
        int fw = bw * (ms > full ? full : ms) / full;
        gfx_fill_rect(GR_CX - bw / 2, y + GR(92), bw, GR(7), rgb(40, 60, 100));
        gfx_fill_rect(GR_CX - bw / 2, y + GR(92), fw, GR(7), still ? rgb(120, 255, 160) : rgb(255, 230, 90));
    } else if (s_calerr && now - s_calerr_at < 4000) {
        int h = GR(64), y = TOP_H + GR(8);
        char t[48];
        gr_note_box(y, h, rgb(80, 24, 24));
        snprintf(t, sizeof t, "CALIBRATION: %s.", s_calerr);
        gr_line(gr_fit(&KF_TEXT, t, gr_chord(y, GR(24))), y + GR(26), t, rgb(255, 190, 170));
        gr_line(&KF_TEXT, y + GR(50), "TRY AGAIN", rgb(255, 190, 170));
    } else if (s_ready_at && now - s_ready_at < 2500) {
        int h = GR(64), y = TOP_H + GR(8);
        gr_note_box(y, h, rgb(20, 60, 40));
        gr_line(&KF_TEXT, y + GR(26),
                mode_now() == PAD_MODE_MOVE ? "READY: MOVE IT FORWARD = UP" : "READY: FORWARD = UP",
                rgb(160, 255, 190));
        gr_line(&KF_TEXT, y + GR(50), "BACK = DOWN, LEAN = LEFT / RIGHT", rgb(160, 255, 190));
    }
}
