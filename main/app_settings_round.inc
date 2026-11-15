// SETUP on a round panel (BOARD_ROUND, the native round frame): the pages WIFI, SAVED and QR (main/set_round.h has the
// shared look: the title on the top cap, the content in the middle box, BACK on the bottom cap). The zones have the same
// numbers as the 320 x 240 pages, so the input code stays one. Only app_settings_wifi.c includes this file.

// ---- WIFI: SCAN and SAVED under the head, 3 network rows and the arrows, or the state with CONNECT and AUTO
#define RW_BTN_Y  RS(108)
#define RW_BTN_H  RS(46)
#define RW_LIST_Y RS(164)
#define RW_ROW_H  RS(56)
#define RW_ROW_W  (RB_W - RS(58))
#define RW_ARR_X  (RB_X + RB_W - RS(52))
#define RW_ARR_W  RS(52)
#define RW_CONN_Y RS(316)
#define RW_CONN_H RS(50)

// the buttons of the WIFI page: 0 SCAN, 1 SAVED; 2 AUTO, 3 CONNECT
static void rw_btn(int i, int *x, int *y, int *w, int *h)
{
    *w = RS(150);
    *h = i < 2 ? RW_BTN_H : RW_CONN_H;
    *y = i < 2 ? RW_BTN_Y : RW_CONN_Y;
    *x = (i & 1) ? gfx_w / 2 + RS(6) : gfx_w / 2 - RS(6) - *w;
}

static int wifi_at_round(int x, int y)
{
    int bx, by, bw, bh;
    if (rr_back_at(x, y)) {
        return 10;
    }
    rw_btn(0, &bx, &by, &bw, &bh);
    if (ui_hit(x, y, bx, by, bw, bh, 4)) {
        return 11;
    }
    rw_btn(1, &bx, &by, &bw, &bh);
    if (wifi_saved() && ui_hit(x, y, bx, by, bw, bh, 4)) {
        return 12;
    }
    rw_btn(3, &bx, &by, &bw, &bh);
    if (wconn_shown() && ui_hit(x, y, bx, by, bw, bh, 4)) {
        return 15;
    }
    rw_btn(2, &bx, &by, &bw, &bh);
    if (wauto_shown() && ui_hit(x, y, bx, by, bw, bh, 4)) {
        return 16;
    }
    if (set_nlist == 0 || y < RW_LIST_Y || y >= RW_LIST_Y + N_LIST * RW_ROW_H) {
        return -1;
    }
    if (x >= RW_ARR_X - 4) {
        return y < RW_LIST_Y + N_LIST * RW_ROW_H / 2 ? 13 : 14;
    }
    int r = (y - RW_LIST_Y) / RW_ROW_H;
    return (r < N_LIST && set_list_top + r < set_nlist) ? r : -1;
}

// the up and the down arrow at the right of the rows (press: the zone that is down now)
static void rw_arrows(int press)
{
    int h = N_LIST * RW_ROW_H / 2;
    for (int k = 0; k < 2; k++) {
        int y = RW_LIST_Y + k * h;
        ui_rrect(RW_ARR_X, y, RW_ARR_W, h - 4, RS(10), press == 13 + k ? set_c_accent : set_c_box);
        int cx = RW_ARR_X + RW_ARR_W / 2, cy = y + h / 2 - 2;
        for (int j = 0; j < RS(9); j++) {
            int yy = k ? cy + RS(4) - j : cy - RS(4) + j;
            gfx_fill_rect(cx - j, yy, 2 * j + 1, 1, set_c_text);
        }
    }
}

static void signal_bars(int x, int y, int rssi);

static void wifi_render_round(void)
{
    gfx_fill(THEME_SETUP_BG);
    char buf[80];
    wifi_state_t st = wifi_state();
    uint16_t sc = set_wifi_line(buf, sizeof(buf));
    rr_head("WIFI", buf, set_c_accent, sc);
    int x, y, w, h;
    rw_btn(0, &x, &y, &w, &h);
    rr_btn(x, y, w, h, wifi_scanning() ? "..." : "SCAN", s_wifi_press == 11 ? set_c_accent : set_c_box,
           s_wifi_press == 11 ? THEME_SETUP_TEXT_HOT : set_c_text);
    if (wifi_saved()) {
        rw_btn(1, &x, &y, &w, &h);
        rr_btn(x, y, w, h, "SAVED", s_wifi_press == 12 ? set_c_accent : set_c_box,
               s_wifi_press == 12 ? THEME_SETUP_TEXT_HOT : set_c_text);
    }
    rr_back("< BACK", s_wifi_press == 10, set_c_box, set_c_accent, set_c_text, THEME_SETUP_TEXT_HOT);
    if (set_nlist == 0) {
        if (st == WIFI_CONNECTED) {
            rr_line(&KF_TEXT, RS(196), "OPEN THIS PAGE IN A BROWSER:", set_c_text);
            snprintf(buf, sizeof(buf), "http://%s", wifi_ip());
            rr_line(&KF_BIG, RS(236), buf, set_c_accent);
            rr_line(&KF_LABEL, RS(264), "ON THE SAME NETWORK", set_c_dim);
            rr_line(&KF_LABEL, RS(284), "EXPORTS, MP3, MIDI, GAMEPAD", set_c_dim);
        } else {
            rr_line(&KF_TEXT, RS(236), wifi_scanning() ? "LOOKING FOR NETWORKS..." : "TAP SCAN TO FIND NETWORKS",
                    set_c_dim);
        }
        if (wauto_shown()) {
            rw_btn(2, &x, &y, &w, &h);
            rr_btn(x, y, w, h, wifi_saved_auto(wifi_ssid()) ? "AUTO: ON" : "AUTO: OFF",
                   s_wifi_press == 16 ? set_c_accent : set_c_box,
                   s_wifi_press == 16 ? THEME_SETUP_TEXT_HOT : set_c_text);
        }
        if (wconn_shown()) {
            rw_btn(3, &x, &y, &w, &h);
            rr_btn(x, y, w, h, st == WIFI_IDLE ? "CONNECT" : "DISCONNECT",
                   s_wifi_press == 15 ? set_c_accent : set_c_box,
                   s_wifi_press == 15 ? THEME_SETUP_TEXT_HOT : set_c_text);
        }
        return;
    }
    for (int r = 0; r < N_LIST && set_list_top + r < set_nlist; r++) {
        const wifi_ap_t *ap = &set_list[set_list_top + r];
        int ry = RW_LIST_Y + r * RW_ROW_H, my = ry + (RW_ROW_H - 4) / 2;
        bool hot = s_wifi_press == r;
        uint16_t fg = hot ? THEME_SETUP_TEXT_HOT : set_c_text;
        ui_rrect(RB_X, ry, RW_ROW_W, RW_ROW_H - 4, RS(10), hot ? set_c_accent : THEME_SETUP_ROW);
        if (set_wfocus == set_list_top + r) {
            ui_rbox(RB_X, ry, RW_ROW_W, RW_ROW_H - 4, RS(10), hot ? set_c_accent : THEME_SETUP_ROW, set_c_accent, 3);
        }
        int bx = RB_X + RW_ROW_W - RS(40), nw = bx - RB_X - RS(48);
        const kfont_t *f = rr_fit(&KF_TEXT, ap->ssid, nw);
        ui_text(f, RB_X + RS(12), my + f->asc / 2, ap->ssid, fg);
        if (ap->secure) {
            gfx_ring(bx - RS(22), my - 3, 5, 3, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
            gfx_fill_rect(bx - RS(22) - 6, my - 2, 13, 9, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
        } else {
            ui_text_c(&KF_LABEL, bx - RS(22), my + 6, "OPEN", hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
        }
        signal_bars(bx, my - 8, ap->rssi);
    }
    rw_arrows(s_wifi_press);
}

// ---- SAVED: 3 rows of the saved networks (the name, then AUTO and FORGET), the arrows at the right
#define RV_LIST_Y RS(108)
#define RV_ROW_H  RS(88)
#define RV_PILL_Y RS(40)  // the pills in a row: y + RV_PILL_Y .. + RV_PILL_H
#define RV_PILL_H RS(42)

// the pills of a saved row: 1 AUTO, 2 FORGET (x, w)
static void rv_pill(int c, int *x, int *w)
{
    int right = RB_X + RW_ROW_W - RS(8);
    *w = c == 2 ? RS(110) : RS(130);
    *x = c == 2 ? right - *w : right - RS(110) - RS(8) - *w;
}

static int saved_at_round(int x, int y)
{
    if (rr_back_at(x, y)) {
        return 10;
    }
    int top = RV_LIST_Y, bot = RV_LIST_Y + N_LIST * RV_ROW_H;
    if (y < top || y >= bot) {
        return -1;
    }
    if (x >= RW_ARR_X - 4) {
        return y < top + N_LIST * RV_ROW_H / 2 ? 13 : 14;
    }
    int r = (y - top) / RV_ROW_H, ry = top + r * RV_ROW_H;
    if (r >= N_LIST || set_sv_top + r >= wifi_saved_count()) {
        return -1;
    }
    int c = 0;
    for (int k = 1; k <= 2; k++) {
        int px, pw;
        rv_pill(k, &px, &pw);
        if (y >= ry + RV_PILL_Y - 4 && x >= px - 4 && x < px + pw + 4) {
            c = k;
        }
    }
    return 100 + 3 * (set_sv_top + r) + c;
}

static void saved_render_round(void)
{
    gfx_fill(THEME_SETUP_BG);
    int n = wifi_saved_count();
    char buf[64];
    snprintf(buf, sizeof buf, "SAVED WIFI (%d)", n);
    rr_head(buf, "AUTO OFF: JOINS ONLY BY HAND", set_c_accent, set_c_dim);
    if (set_sv_top > 0 && set_sv_top + N_LIST > n) {
        set_sv_top = n > N_LIST ? n - N_LIST : 0;  // (after a FORGET)
    }
    const bool cur_on = wifi_state() == WIFI_CONNECTED;
    for (int r = 0; r < N_LIST && set_sv_top + r < n; r++) {
        int i = set_sv_top + r, y = RV_LIST_Y + r * RV_ROW_H;
        const char *name = wifi_saved_name(i);
        const bool cur = cur_on && !strcmp(name, wifi_ssid()), au = wifi_saved_auto(name);
        const bool hn = set_sv_press == 100 + 3 * i, ha = set_sv_press == 101 + 3 * i, hf = set_sv_press == 102 + 3 * i;
        ui_rrect(RB_X, y, RW_ROW_W, RV_ROW_H - 6, RS(12), hn ? set_c_accent : THEME_SETUP_ROW);
        const kfont_t *f = rr_fit(&KF_TEXT, name, RW_ROW_W - RS(20));
        ui_text(f, RB_X + RS(12), y + RS(28), name, hn ? THEME_SETUP_TEXT_HOT : cur ? THEME_SETUP_OK : set_c_text);
        int px, pw;
        rv_pill(1, &px, &pw);
        rr_btn(px, y + RV_PILL_Y, pw, RV_PILL_H, au ? "AUTO: ON" : "AUTO: OFF",
               ha   ? set_c_accent
               : au ? THEME_SETUP_AUTO_ON
                    : set_c_box,
               ha ? THEME_SETUP_TEXT_HOT : set_c_text);
        rv_pill(2, &px, &pw);
        rr_btn(px, y + RV_PILL_Y, pw, RV_PILL_H, "FORGET", hf ? set_c_accent : THEME_SETUP_FORGET,
               hf ? THEME_SETUP_TEXT_HOT : set_c_text);
    }
    if (n == 0) {
        rr_line(&KF_TEXT, RS(236), "NO SAVED WIFI", set_c_dim);
    }
    int h = N_LIST * RV_ROW_H / 2;
    for (int k = 0; k < 2; k++) {
        int y = RV_LIST_Y + k * h;
        ui_rrect(RW_ARR_X, y, RW_ARR_W, h - 6, RS(10), set_sv_press == 13 + k ? set_c_accent : set_c_box);
        int cx = RW_ARR_X + RW_ARR_W / 2, cy = y + h / 2 - 3;
        for (int j = 0; j < RS(9); j++) {
            gfx_fill_rect(cx - j, k ? cy + RS(4) - j : cy - RS(4) + j, 2 * j + 1, 1, set_c_text);
        }
    }
    rr_back("< BACK", set_sv_press == 10, set_c_box, set_c_accent, set_c_text, THEME_SETUP_TEXT_HOT);
}

// ---- QR: one or two codes in the middle of the circle, their captions under them; START HOTSPOT with no page
#define RQ_START_X (gfx_w / 2 - RS(110))
#define RQ_START_Y RS(250)
#define RQ_START_W RS(220)
#define RQ_START_H RS(54)

static int qr_ctrl_round(int x, int y)
{
    if (rr_back_at(x, y)) {
        return CTRL_BACK;
    }
    if (!wifi_web_ip() && ui_hit(x, y, RQ_START_X, RQ_START_Y, RQ_START_W, RQ_START_H, 4)) {
        return CTRL_QR_START;
    }
    return CTRL_NONE;
}

// a code with its caption under it, at the middle cx (max: the largest side)
static void rq_column(int cx, int max, const char *code, const char *l1, const char *l2, const char *l3, uint16_t c3)
{
    int px = 0;
    for (int p = 6; p >= 2 && !px; p--) {
        int s = qr_side(p, code);
        px = s > 0 && s <= max ? p : 0;
    }
    int side = px ? qr_side(px, code) : max, y = RS(112);
    if (px) {
        qr_draw(cx - side / 2, y, px, code);
    }
    int ty = y + side + RS(26), w = 2 * RS(110);
    ui_text_c(rr_fit(&KF_TEXT, l1, w), cx, ty, l1, set_c_accent);
    ui_text_c(rr_fit(&KF_LABEL, l2, w), cx, ty + RS(20), l2, set_c_dim);
    ui_text_c(rr_fit(&KF_TEXT, l3, w), cx, ty + RS(44), l3, c3);
}

static void qr_render_round(void)
{
    gfx_fill(THEME_SETUP_BG);
    char join[80], url[64], ip[24];
    const char *web = wifi_web_ip();
    if (wifi_hotspot_on()) {
        snprintf(join, sizeof(join), "WIFI:S:%s;T:nopass;;", wifi_hotspot_ssid());
        snprintf(url, sizeof(url), "http://%s/", wifi_hotspot_ip());
        snprintf(ip, sizeof(ip), "%s", wifi_hotspot_ip());
        rr_line(&KF_TEXT, RS(84), "THE WEB PAGE", set_c_text);
        rq_column(gfx_w / 2 - RS(92), RS(150), join, "1. JOIN", "THE WIFI (NO PASSWORD)", wifi_hotspot_ssid(),
                  set_c_text);
        rq_column(gfx_w / 2 + RS(92), RS(150), url, "2. OPEN", "THE PAGE IN A BROWSER", ip, set_c_accent);
    } else if (web) {
        snprintf(url, sizeof(url), "http://%s/", web);
        snprintf(ip, sizeof(ip), "%s", web);
        rr_line(&KF_TEXT, RS(84), "THE WEB PAGE", set_c_text);
        rq_column(gfx_w / 2, RS(170), url, "OPEN THE PAGE", "IN A BROWSER, ON THE SAME WIFI", ip, set_c_accent);
    } else {
        rr_head("NO WEB PAGE NOW", "", set_c_text, set_c_dim);
        rr_line(&KF_TEXT, RS(170), "CONNECT TO WIFI,", set_c_dim);
        rr_line(&KF_TEXT, RS(198), "OR START THE HOTSPOT:", set_c_dim);
        bool hot = set_qr_press == CTRL_QR_START;
        rr_btn(RQ_START_X, RQ_START_Y, RQ_START_W, RQ_START_H, "START HOTSPOT", hot ? set_c_accent : set_c_box_on,
               hot ? THEME_SETUP_TEXT_HOT : set_c_text);
    }
    rr_back("< BACK", set_qr_press == CTRL_BACK, set_c_box, set_c_accent, set_c_text, THEME_SETUP_TEXT_HOT);
}
