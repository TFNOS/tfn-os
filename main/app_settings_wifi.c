// SETUP: the WiFi page (the scan, the password, the questions, the saved networks) and the QR page.
#include "app_settings_priv.h"
#include "tfn/theme.h"

wifi_ap_t set_list[16];
int set_nlist, set_list_top;
bool set_list_on;  // the list of networks was asked for (SCAN, or no saved network): else the state
static int s_wifi_press =
        -1;  // -1 none, 0..3 list row, 10 back, 11 scan, 12 forget, 13 up, 14 down, 15 connect, 16 auto
char set_pass[65];
char set_pick[33];
static char s_okq[48];  // the question of OK on the keyboard ("JOIN <name>?")
int set_wfocus = -1;    // the wheel (the Watcher): the network in focus (an index of s_list), -1 = none
// the question page (PAGE_ASK): JOIN an open network, or FORGET the saved one
enum {
    ASK_JOIN,
    ASK_FORGET,
    ASK_FORGET_ONE,
    ASK_JOIN_SAVED
};
static char s_ask_name[33];  // ASK_FORGET_ONE, ASK_JOIN_SAVED: the saved network (SAVED)
int set_ask_kind, set_ask_hot = -1, set_ask_focus = -1;

// the QR page
int set_qr_press = CTRL_NONE;

// ---------------------------------------------------------------- WiFi page

// DISCONNECT / CONNECT: a saved network and no scan list on the page
static bool wconn_shown(void)
{
    return set_nlist == 0 && wifi_saved() && !wifi_scanning();
}

// AUTO: the network of the page is saved (it can join by itself or only by hand)
static bool wauto_shown(void)
{
    char pw[65];
    bool saved = wconn_shown() && wifi_saved_pass(wifi_ssid(), pw, sizeof pw);
    memset(pw, 0, sizeof pw);
    return saved;
}

static void wconn_draw(void)
{
    if (wauto_shown()) {
        const bool hot = s_wifi_press == 16;
        gfx_fill_rect(WAUTO_X, WCONN_Y, WAUTO_W, WCONN_H, hot ? set_c_accent : set_c_box);
        gfx_text_center_in(WAUTO_X, WAUTO_W, WCONN_Y + WCONN_H / 2 - 7,
                           wifi_saved_auto(wifi_ssid()) ? "AUTO: ON" : "AUTO: OFF", 2,
                           hot ? THEME_SETUP_TEXT_HOT : set_c_text);
    }
    if (!wconn_shown()) {
        return;
    }
    const bool hot = s_wifi_press == 15;
    const wifi_state_t st = wifi_state();
    const char *t = st == WIFI_IDLE ? "CONNECT" : "DISCONNECT";
    gfx_fill_rect(WCONN_X, WCONN_Y, WCONN_W, WCONN_H, hot ? set_c_accent : set_c_box);
    gfx_text_center_in(WCONN_X, WCONN_W, WCONN_Y + WCONN_H / 2 - 7, t, 2, hot ? THEME_SETUP_TEXT_HOT : set_c_text);
}

#if BOARD_ROUND
static int wifi_at_round(int x, int y);
static int saved_at_round(int x, int y);
#endif

static int wifi_at(int x, int y)
{
#if BOARD_ROUND
    if (SET_RND) {
        return wifi_at_round(x, y);
    }
#endif
    if (y < WTOP_H) {
        if (x < WBACK_W + 4) {
            return 10;
        }
        if (x >= WSCAN_X - 4 && x < WSCAN_X + WSCAN_W + 4) {
            return 11;
        }
        if (x >= WFORGET_X - 4 && wifi_saved()) {
            return 12;  // (no zone when FORGET is not drawn)
        }
        return -1;
    }
    if (wconn_shown() && x >= WCONN_X - 4 && x < WCONN_X + WCONN_W + 4 && y >= WCONN_Y - 4 &&
        y < WCONN_Y + WCONN_H + 4) {
        return 15;
    }
    if (wauto_shown() && x >= WAUTO_X - 4 && x < WAUTO_X + WAUTO_W + 4 && y >= WCONN_Y - 4 &&
        y < WCONN_Y + WCONN_H + 4) {
        return 16;
    }
    if (x >= ARROW_X) {
        return y < LIST_Y + ARROW_H ? 13 : 14;
    }
    int r = (y - LIST_Y) / LIST_H;
    return (y >= LIST_Y && r < N_LIST && set_list_top + r < set_nlist) ? r : -1;
}

static void ask_open(int kind)
{
    set_ask_kind = kind;
    set_ask_hot = -1;
    set_ask_focus = set_wfocus >= 0 ? 0 : -1;  // the wheel: the focus starts on NO
    set_page = PAGE_ASK;
    sfx_ui(SFX_TICK);
}

// the answer of the question page: 1 = YES
void set_ask_answer(int a)
{
    set_page = set_ask_kind == ASK_FORGET_ONE || set_ask_kind == ASK_JOIN_SAVED ? PAGE_SAVED : PAGE_WIFI;
    set_ask_hot = set_ask_focus = -1;
    if (a != 1) {
        sfx_ui(SFX_BACK);
        return;
    }
    if (set_ask_kind == ASK_JOIN_SAVED) {
        set_page = PAGE_WIFI;  // (the state of the new connection)
    }
    if (set_ask_kind == ASK_FORGET_ONE) {
        wifi_forget_name(s_ask_name);
        if (wifi_saved_count() > 0) {
            set_page = PAGE_SAVED;
        }
        sfx_play(SFX_CUT);
    } else if (set_ask_kind == ASK_JOIN_SAVED) {
        char pw[65] = "";
        wifi_saved_pass(s_ask_name, pw, sizeof pw);
        wifi_connect_try(s_ask_name, pw);
        memset(pw, 0, sizeof pw);
        set_nlist = 0;
        set_list_on = false;
        sfx_play(SFX_CLICK);
    } else if (set_ask_kind == ASK_FORGET) {
        wifi_forget();
        sfx_play(SFX_CUT);
    } else {
        wifi_connect_try(set_pick, "");
        set_nlist = 0;
        set_list_on = false;
        set_wfocus = -1;
        sfx_ui(SFX_CLICK);
    }
}

void set_ask_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN || ev->kind == IN_MOVE) {
        set_ask_hot = kbd_ask_at(ev->x, ev->y);
    } else if (ev->kind == IN_UP) {
        set_ask_hot = -1;
    } else if (ev->kind == IN_TAP) {
        int a = kbd_ask_at(ev->x, ev->y);
        if (a >= 0) {
            set_ask_answer(a);
        }
    }
}

void set_ask_render(void)
{
    char l1[64], l2[64];
    if (set_ask_kind == ASK_FORGET_ONE) {
        snprintf(l1, sizeof l1, "FORGET %.32s?", s_ask_name);
        snprintf(l2, sizeof l2, "ITS PASSWORD GOES. THE OTHER SAVED WIFI STAY");
    } else if (set_ask_kind == ASK_JOIN_SAVED) {
        snprintf(l1, sizeof l1, "JOIN %.32s?", s_ask_name);
        snprintf(l2, sizeof l2, "WITH ITS SAVED PASSWORD");
    } else if (set_ask_kind == ASK_FORGET) {
        snprintf(l1, sizeof l1, "FORGET %.32s?", wifi_ssid());
        int n = wifi_saved_count() - 1;
        if (n > 0) {
            snprintf(l2, sizeof l2, "THE BOARD LEAVES IT. %d OTHER SAVED WIFI STAY", n);
        } else {
            snprintf(l2, sizeof l2, "THE BOARD LEAVES THIS WIFI");
        }
    } else {
        snprintf(l1, sizeof l1, "JOIN %.32s?", set_pick);
        if (wifi_saved() && strcmp(wifi_ssid(), set_pick)) {
            snprintf(l2, sizeof l2, "NO PASSWORD. %.20s STAYS SAVED", wifi_ssid());
        } else {
            snprintf(l2, sizeof l2, "AN OPEN NETWORK (NO PASSWORD)");
        }
    }
    kbd_ask_render(l1, l2, set_ask_hot, set_ask_focus);
}

// ---------------------------------------------------------------- SAVED: the saved networks, each with AUTO and FORGET

int set_sv_top, set_sv_press = -1;  // the first row shown; the pressed zone (-1 none)

#if BOARD_ROUND
#include "set_round.h"
#include "app_settings_round.inc"
#endif

static void saved_open(void)
{
    if (GUARD_BUILD && !device_guard_local_admin()) {
        sysui_message("UNLOCK ADMIN ACCESS TO VIEW SAVED WIFI", 4);
        return;
    }
    set_sv_top = 0;
    set_sv_press = -1;
    set_page = PAGE_SAVED;
    sfx_play(SFX_CLICK);
}

// the zone at a point: 10 back, 13 up, 14 down, 100 + 3 * row + (0 the name, 1 AUTO, 2 FORGET); -1 none
static int saved_at(int x, int y)
{
#if BOARD_ROUND
    if (SET_RND) {
        return saved_at_round(x, y);
    }
#endif
    if (y < WTOP_H) {
        return x < WBACK_W + 4 ? 10 : -1;
    }
    if (x >= ARROW_X) {
        return y < LIST_Y + ARROW_H ? 13 : 14;
    }
    int r = (y - LIST_Y) / LIST_H;
    if (y < LIST_Y || r >= N_LIST || set_sv_top + r >= wifi_saved_count()) {
        return -1;
    }
    int c = x >= SV_FORGET_X - 3 ? 2 : x >= SV_AUTO_X - 3 ? 1 : 0;
    return 100 + 3 * (set_sv_top + r) + c;
}

void set_saved_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        set_sv_press = saved_at(ev->x, ev->y);
        return;
    }
    if (ev->kind == IN_UP) {
        set_sv_press = -1;
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    int z = saved_at(ev->x, ev->y), n = wifi_saved_count();
    if (z == 10) {
        set_page = PAGE_WIFI;
        sfx_play(SFX_BACK);
    } else if (z == 13) {
        if (set_sv_top > 0) {
            set_sv_top--;
        }
    } else if (z == 14) {
        if (set_sv_top + N_LIST < n) {
            set_sv_top++;
        }
    } else if (z >= 100) {
        int i = (z - 100) / 3, c = (z - 100) % 3;
        snprintf(s_ask_name, sizeof s_ask_name, "%s", wifi_saved_name(i));
        if (c == 1) {  // AUTO JOIN of this network
            bool on = !wifi_saved_auto(s_ask_name);
            wifi_saved_set_auto(s_ask_name, on);
            sfx_play(SFX_CLICK);
        } else if (c == 2) {
            ask_open(ASK_FORGET_ONE);  // FORGET <name>? NO / YES
        } else if (!(wifi_state() == WIFI_CONNECTED && !strcmp(wifi_ssid(), s_ask_name))) {
            ask_open(ASK_JOIN_SAVED);  // JOIN <name>? (its saved password; also a network with AUTO OFF)
        }
    }
}

void set_saved_render(void)
{
#if BOARD_ROUND
    if (SET_RND) {
        saved_render_round();
        return;
    }
#endif
    gfx_fill(THEME_SETUP_BG);
    gfx_fill_rect(0, 2, WBACK_W, WTOP_H - 4, set_sv_press == 10 ? set_c_accent : set_c_box);
    gfx_text_center_in(0, WBACK_W, WTOP_H / 2 - 7, "< BACK", 2, set_sv_press == 10 ? THEME_SETUP_TEXT_HOT : set_c_text);
    int n = wifi_saved_count();
    char buf[64];
    snprintf(buf, sizeof buf, "SAVED WIFI (%d)", n);
    gfx_text(WBACK_W + 12, WTOP_H / 2 - 7, buf, 2, set_c_accent);
#if SET_WIDE
    gfx_text(WBACK_W + 12 + gfx_text_width(buf, 2) + 14, WTOP_H / 2 - 3, "AUTO OFF: JOINS ONLY BY HAND", 1, set_c_dim);
#endif
    if (set_sv_top > 0 && set_sv_top + N_LIST > n) {
        set_sv_top = n > N_LIST ? n - N_LIST : 0;  // (after a FORGET)
    }
    const bool cur_on = wifi_state() == WIFI_CONNECTED;
    for (int r = 0; r < N_LIST && set_sv_top + r < n; r++) {
        int i = set_sv_top + r, y = LIST_Y + r * LIST_H, my = y + (LIST_H - 3) / 2;
        const char *name = wifi_saved_name(i);
        const bool cur = cur_on && !strcmp(name, wifi_ssid()), au = wifi_saved_auto(name);
        const bool hn = set_sv_press == 100 + 3 * i, ha = set_sv_press == 101 + 3 * i, hf = set_sv_press == 102 + 3 * i;
        gfx_fill_rect(0, y, SV_AUTO_X - 4, LIST_H - 3, hn ? set_c_accent : THEME_SETUP_ROW);
        // the name: big when it fits, else small, else cut
        int ts = gfx_text_width(name, 2) <= SV_NAME_W ? 2 : 1, max = SV_NAME_W / (ts * 6);
        snprintf(buf, sizeof buf, "%.*s", max < 32 ? max : 32, name);
        gfx_text(SV_TS == 2 ? 10 : 6, my - 4 * ts + 1, buf, ts,
                 hn    ? THEME_SETUP_TEXT_HOT
                 : cur ? THEME_SETUP_OK
                       : set_c_text);
        gfx_fill_rect(SV_AUTO_X, y, SV_AUTO_W, LIST_H - 3, ha ? set_c_accent : au ? THEME_SETUP_AUTO_ON : set_c_box);
        gfx_text_center_in(SV_AUTO_X, SV_AUTO_W, my - 4 * SV_TS + 1, au ? "AUTO: ON" : "AUTO: OFF", SV_TS,
                           ha ? THEME_SETUP_TEXT_HOT : set_c_text);
        gfx_fill_rect(SV_FORGET_X, y, SV_FORGET_W, LIST_H - 3, hf ? set_c_accent : THEME_SETUP_FORGET);
        gfx_text_center_in(SV_FORGET_X, SV_FORGET_W, my - 4 * SV_TS + 1, "FORGET", SV_TS,
                           hf ? THEME_SETUP_TEXT_HOT : set_c_text);
    }
    if (n == 0) {
        gfx_text_center(LIST_Y + 40, "NO SAVED WIFI", 2, set_c_dim);
    }
    gfx_fill_rect(ARROW_X, LIST_Y, ARROW_W, ARROW_H - 3, set_sv_press == 13 ? set_c_accent : set_c_box);
    gfx_text_center_in(ARROW_X, ARROW_W, LIST_Y + ARROW_H / 2 - 8, "^", 2, set_c_text);
    gfx_fill_rect(ARROW_X, LIST_Y + ARROW_H, ARROW_W, ARROW_H - 3, set_sv_press == 14 ? set_c_accent : set_c_box);
    gfx_text_center_in(ARROW_X, ARROW_W, LIST_Y + ARROW_H + ARROW_H / 2 - 8, "v", 2, set_c_text);
}

void set_wifi_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        s_wifi_press = wifi_at(ev->x, ev->y);
        return;
    }
    if (ev->kind == IN_UP) {
        s_wifi_press = -1;
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    int t = wifi_at(ev->x, ev->y);
    switch (t) {
    case 10:
        set_page = PAGE_MAIN;
        sfx_ui(SFX_BACK);
        break;
    case 11:
        wifi_scan();
        set_nlist = 0;
        set_list_on = true;
        set_list_top = 0;
        set_wfocus = -1;
        sfx_ui(SFX_CLICK);
        break;
    case 12:
        saved_open();  // SAVED: each saved network with AUTO and FORGET
        break;
    case 15:  // DISCONNECT (the network stays saved) / CONNECT again
        if (wifi_state() == WIFI_IDLE) {
            wifi_reconnect();
        } else {
            wifi_disconnect();
        }
        sfx_play(SFX_CLICK);
        break;
    case 16: {  // AUTO JOIN of the saved network: on = at the start and in each new try
        bool on = !wifi_saved_auto(wifi_ssid());
        wifi_saved_set_auto(wifi_ssid(), on);
        char m[80];
        snprintf(m, sizeof m, on ? "%.32s: JOINS BY ITSELF" : "%.32s: ONLY BY HAND", wifi_ssid());
        sysui_message(m, 2);
        sfx_play(SFX_CLICK);
        break;
    }
    case 13:
        if (set_list_top > 0) {
            set_list_top--;
        }
        break;
    case 14:
        if (set_list_top + N_LIST < set_nlist) {
            set_list_top++;
        }
        break;
    default:
        if (t >= 0 && t < N_LIST) {
            set_wifi_pick(set_list_top + t);
        }
        break;
    }
}

// a network from the list: the keyboard for its password, or the question JOIN (an open network)
void set_wifi_pick(int i)
{
    const wifi_ap_t *ap = &set_list[i];
    snprintf(set_pick, sizeof(set_pick), "%s", ap->ssid);
    if (ap->secure) {
        if (GUARD_BUILD) {
            bool saved = wifi_saved_pass(ap->ssid, set_pass, sizeof set_pass);
            memset(set_pass, 0, sizeof set_pass);
            if (saved) {
                snprintf(s_ask_name, sizeof s_ask_name, "%s", ap->ssid);
                ask_open(ASK_JOIN_SAVED);
                return;
            }
        }
        set_pass[0] = 0;
        if (!GUARD_BUILD) {
            wifi_saved_pass(ap->ssid, set_pass, sizeof(set_pass));
        }
        static char title[48];
        snprintf(title, sizeof(title), "PASSWORD FOR %s", ap->ssid);
        kbd_open(title, set_pass, sizeof(set_pass), GUARD_BUILD);
        set_kbd_web = false;
        snprintf(s_okq, sizeof s_okq, "JOIN %.32s?", ap->ssid);
        kbd_set_ok_question(s_okq);
        set_page = PAGE_KBD;
        sfx_ui(SFX_CLICK);
    } else {
        ask_open(ASK_JOIN);
    }
}

static void signal_bars(int x, int y, int rssi)
{
    int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    for (int b = 0; b < 4; b++) {
        int h = 4 + b * 4;
        gfx_fill_rect(x + b * 6, y + 16 - h, 4, h, b < bars ? THEME_SETUP_SIGNAL_ON : THEME_SETUP_SIGNAL_OFF);
    }
}

// one line about the WiFi connection
uint16_t set_wifi_line(char *buf, int len)
{
    switch (wifi_state()) {
    case WIFI_CONNECTED:
        snprintf(buf, len, "ON %s  %s", wifi_ssid(), wifi_ip());
        return THEME_SETUP_OK;
    case WIFI_CONNECTING:
        snprintf(buf, len, "CONNECTING TO %s...", wifi_ssid());
        return set_c_dim;
    case WIFI_FAILED:
        snprintf(buf, len, "COULD NOT CONNECT TO %s", wifi_ssid());
        return THEME_SETUP_FAULT;
    default:
        snprintf(buf, len, "%s", wifi_saved() ? "SAVED, NOT CONNECTED" : "NOT CONNECTED");
        return set_c_dim;
    }
}

#if SET_WIDE
void set_wifi_render(int64_t now)
{
    gfx_fill(THEME_SETUP_BG);
    gfx_fill_rect(0, 2, WBACK_W, WTOP_H - 4, s_wifi_press == 10 ? set_c_accent : set_c_box);
    gfx_text_center_in(0, WBACK_W, WTOP_H / 2 - 7, "< BACK", 2, s_wifi_press == 10 ? THEME_SETUP_TEXT_HOT : set_c_text);
    gfx_text(WBACK_W + 12, 4, "WIFI", 2, set_c_accent);

    char buf[80];
    wifi_state_t st = wifi_state();
    uint16_t sc = set_wifi_line(buf, sizeof(buf));
    buf[(WSCAN_X - WBACK_W - 16) / 6] = 0;
    gfx_text(WBACK_W + 12, 26, buf, 1, sc);

    gfx_fill_rect(WSCAN_X, 2, WSCAN_W, WTOP_H - 4, s_wifi_press == 11 ? set_c_accent : set_c_box);
    gfx_text_center_in(WSCAN_X, WSCAN_W, WTOP_H / 2 - 7, wifi_scanning() ? "..." : "SCAN", 2,
                       s_wifi_press == 11 ? THEME_SETUP_TEXT_HOT : set_c_text);
    if (wifi_saved()) {
        gfx_fill_rect(WFORGET_X, 2, SCR_W - WFORGET_X - 2, WTOP_H - 4, s_wifi_press == 12 ? set_c_accent : set_c_box);
        gfx_text_center_in(WFORGET_X, SCR_W - WFORGET_X, WTOP_H / 2 - 7, "SAVED", 2,
                           s_wifi_press == 12 ? THEME_SETUP_TEXT_HOT : set_c_text);
    }

    if (st == WIFI_CONNECTED && set_nlist == 0) {
        gfx_text_center(64, "OPEN THIS PAGE IN A BROWSER:", 2, set_c_text);
        snprintf(buf, sizeof(buf), "http://%s", wifi_ip());
        gfx_text_center(92, buf, 3, set_c_accent);
        gfx_text_center(130, "(ON THE SAME NETWORK) - EXPORTS, MP3, MIDI, GAMEPAD", 1, set_c_dim);
        wconn_draw();
        return;
    }
    if (set_nlist == 0) {
        gfx_text_center(90, wifi_scanning() ? "LOOKING FOR NETWORKS..." : "TAP SCAN TO FIND NETWORKS", 2, set_c_dim);
        wconn_draw();
        return;
    }
    for (int r = 0; r < N_LIST && set_list_top + r < set_nlist; r++) {
        const wifi_ap_t *ap = &set_list[set_list_top + r];
        int y = LIST_Y + r * LIST_H;
        bool hot = s_wifi_press == r;
        gfx_fill_rect(0, y, WLIST_W, LIST_H - 3, hot ? set_c_accent : THEME_SETUP_ROW);
        y += (LIST_H - 32) / 2;  // the row content in the middle of the taller row
        gfx_text(10, y + 8, ap->ssid, 2, hot ? THEME_SETUP_TEXT_HOT : set_c_text);
        if (ap->secure) {
            gfx_ring(526, y + 12, 5, 3, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
            gfx_fill_rect(520, y + 13, 13, 9, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
        } else {
            gfx_text(470, y + 12, "OPEN", 1, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
        }
        signal_bars(552, y + 5, ap->rssi);
    }
    // scroll arrows
    gfx_fill_rect(ARROW_X, LIST_Y, ARROW_W, ARROW_H - 3, s_wifi_press == 13 ? set_c_accent : set_c_box);
    gfx_text_center_in(ARROW_X, ARROW_W, LIST_Y + ARROW_H / 2 - 8, "^", 2, set_c_text);
    gfx_fill_rect(ARROW_X, LIST_Y + ARROW_H, ARROW_W, ARROW_H - 3, s_wifi_press == 14 ? set_c_accent : set_c_box);
    gfx_text_center_in(ARROW_X, ARROW_W, LIST_Y + ARROW_H + ARROW_H / 2 - 8, "v", 2, set_c_text);
}
#else
void set_wifi_render(int64_t now)
{
#if BOARD_ROUND
    if (SET_RND) {
        wifi_render_round();
        return;
    }
#endif
    gfx_fill(THEME_SETUP_BG);
    gfx_fill_rect(0, 2, WBACK_W, WTOP_H - 4, s_wifi_press == 10 ? set_c_accent : set_c_box);
    gfx_text_center_in(0, WBACK_W, WTOP_H / 2 - 7, "< BACK", 2, s_wifi_press == 10 ? THEME_SETUP_TEXT_HOT : set_c_text);

    gfx_fill_rect(WSCAN_X, 2, WSCAN_W, WTOP_H - 4, s_wifi_press == 11 ? set_c_accent : set_c_box);
    gfx_text_center_in(WSCAN_X, WSCAN_W, WTOP_H / 2 - 7, wifi_scanning() ? "..." : "SCAN", 2,
                       s_wifi_press == 11 ? THEME_SETUP_TEXT_HOT : set_c_text);
    if (wifi_saved()) {
        gfx_fill_rect(WFORGET_X, 2, SCR_W - WFORGET_X, WTOP_H - 4, s_wifi_press == 12 ? set_c_accent : set_c_box);
        gfx_text_center_in(WFORGET_X, SCR_W - WFORGET_X, WTOP_H / 2 - 7, "SAVED", 2,
                           s_wifi_press == 12 ? THEME_SETUP_TEXT_HOT : set_c_text);
    }

    // the state: one line below the buttons (52 letters fill the width)
    char buf[80], line[88];
    wifi_state_t st = wifi_state();
    uint16_t sc = set_wifi_line(buf, sizeof(buf));
    snprintf(line, sizeof line, "WIFI: %s", buf);
    line[(SCR_W - 4) / 6] = 0;
    gfx_text(4, WTOP_H + 4, line, 1, sc);

    if (st == WIFI_CONNECTED && set_nlist == 0) {
        gfx_text_center(76, "OPEN THIS PAGE", 2, set_c_text);
        gfx_text_center(96, "IN A BROWSER:", 2, set_c_text);
        snprintf(buf, sizeof(buf), "http://%s", wifi_ip());
        gfx_text_center(130, buf, 2, set_c_accent);
        gfx_text_center(166, "(ON THE SAME NETWORK)", 1, set_c_dim);
        gfx_text_center(180, "EXPORTS, MP3, MIDI, GAMEPAD", 1, set_c_dim);
        wconn_draw();
        return;
    }
    if (set_nlist == 0) {
        gfx_text_center(130, wifi_scanning() ? "LOOKING FOR NETWORKS..." : "TAP SCAN TO FIND NETWORKS", 2, set_c_dim);
        wconn_draw();
        return;
    }
    for (int r = 0; r < N_LIST && set_list_top + r < set_nlist; r++) {
        const wifi_ap_t *ap = &set_list[set_list_top + r];
        int y = LIST_Y + r * LIST_H;
        bool hot = s_wifi_press == r;
        uint16_t fg = hot ? THEME_SETUP_TEXT_HOT : set_c_text;
        gfx_fill_rect(0, y, WLIST_W, LIST_H - 3, hot ? set_c_accent : THEME_SETUP_ROW);
        // a long name in small letters: the row has room for 16 big ones
        int my = y + (LIST_H - 3) / 2;
        if (strlen(ap->ssid) <= 16) {
            gfx_text(8, my - 7, ap->ssid, 2, fg);
        } else {
            gfx_text(8, my - 4, ap->ssid, 1, fg);
        }
        if (ap->secure) {
            gfx_ring(222, my - 3, 5, 3, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
            gfx_fill_rect(216, my - 2, 13, 9, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
        } else {
            gfx_text(210, my + 12, "OPEN", 1, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
        }
        signal_bars(240, my - 8, ap->rssi);
        if (set_wfocus == set_list_top + r) {
            gfx_frame_rect(0, y, WLIST_W, LIST_H - 3, 3, set_c_accent);
        }
    }
    // scroll arrows
    gfx_fill_rect(ARROW_X, LIST_Y, ARROW_W, ARROW_H - 3, s_wifi_press == 13 ? set_c_accent : set_c_box);
    gfx_text_center_in(ARROW_X, ARROW_W, LIST_Y + ARROW_H / 2 - 8, "^", 2, set_c_text);
    gfx_fill_rect(ARROW_X, LIST_Y + ARROW_H, ARROW_W, ARROW_H - 3, s_wifi_press == 14 ? set_c_accent : set_c_box);
    gfx_text_center_in(ARROW_X, ARROW_W, LIST_Y + ARROW_H + ARROW_H / 2 - 8, "v", 2, set_c_text);
}
#endif

// ---------------------------------------------------------------- QR page

// the START HOTSPOT button when there is no web page
#if SET_WIDE
#define QR_OY      ((SCR_H - 172) / 2)  // the Tab5: the 172 px design in the middle of the 360 px frame (the 3.49: 0)
#define QR_START_X 252
#define QR_START_Y (100 + QR_OY)
#define QR_START_W 200
#define QR_START_H 44
#define QR_PX_MAX  4  // the largest QR module, in pixels
#else
#define QR_START_X 82
#define QR_START_Y 120
#define QR_START_W 200
#define QR_START_H 48
#define QR_PX_MAX  5
#endif

void set_qr_input(const input_event_t *ev)
{
    int c = CTRL_NONE;
#if BOARD_ROUND
    if (SET_RND) {
        c = qr_ctrl_round(ev->x, ev->y);
    } else
#endif
            if (ev->x < BACK_W + 4) {
        c = CTRL_BACK;
    } else if (!wifi_web_ip() && ui_hit(ev->x, ev->y, QR_START_X, QR_START_Y, QR_START_W, QR_START_H, 0)) {
        c = CTRL_QR_START;
    }
    if (ev->kind == IN_DOWN) {
        set_qr_press = c;
    } else if (ev->kind == IN_UP) {
        set_qr_press = CTRL_NONE;
    } else if (ev->kind == IN_TAP) {
        if (c == CTRL_BACK) {
            set_page = PAGE_MAIN;
            sfx_ui(SFX_BACK);
        } else if (c == CTRL_QR_START) {
            wifi_hotspot_start();
            sfx_play(SFX_SWITCH);
        }
    }
}

// the module size so that the code is not larger than max_side
static int qr_px(const char *text, int max_side)
{
    for (int px = QR_PX_MAX; px >= 2; px--) {
        int s = qr_side(px, text);
        if (s > 0 && s <= max_side) {
            return px;
        }
    }
    return 0;
}

#if SET_WIDE
// a code with its caption at the right: returns the x after the block
static int qr_block(int x, int px, const char *code, const char *l1, const char *l2, const char *l3, uint16_t big_c,
                    int w)
{
    int side = px ? qr_side(px, code) : 0;
    if (side > 0) {
        qr_draw(x, (SCR_H - side) / 2, px, code);
    } else {
        side = 0;
    }
    int tx = x + side + 10;
    gfx_text(tx, 30 + QR_OY, l1, 2, set_c_accent);
    gfx_text(tx, 56 + QR_OY, l2, 1, set_c_dim);
    int sc = gfx_text_width(l3, 2) <= w - side - 12 ? 2 : 1;
    gfx_text(tx, 74 + QR_OY, l3, sc, big_c);
    return x + w;
}
#else
// a code with its caption below it, in the band x .. x + w; dx moves the caption to the side
// (two columns: away from the middle, so that the two captions do not touch)
static void qr_column(int x, int w, int dx, int max_side, const char *code, const char *l1, const char *l2,
                      const char *l3, uint16_t big_c)
{
    int px = qr_px(code, max_side);
    int side = px ? qr_side(px, code) : 0;
    if (side > 0) {
        qr_draw(x + (w - side) / 2, 6, px, code);
    } else {
        side = max_side;
    }
    int ty = 6 + side + 6;
    gfx_text_center_in(x + dx, w, ty, l1, 2, set_c_accent);
    gfx_text_center_in(x + dx, w, ty + 20, l2, 1, set_c_dim);
    gfx_text_center_in(x + dx, w, ty + 32, l3, gfx_text_width(l3, 2) <= w ? 2 : 1, big_c);
}
#endif

void set_back_strip(bool hot)
{
    gfx_fill_rect(0, 8, BACK_W - 6, SCR_H - 16, hot ? set_c_accent : set_c_box);
    gfx_text_center_in(0, BACK_W - 6, SCR_H / 2 - 14, "<", 4, hot ? THEME_SETUP_TEXT_HOT : set_c_text);
}

void set_qr_render(int64_t now)
{
#if BOARD_ROUND
    if (SET_RND) {
        qr_render_round();
        return;
    }
#endif
    gfx_fill(THEME_SETUP_BG);
    set_back_strip(set_qr_press == CTRL_BACK);
    char join[80], url[64], ip[24];
    const char *web = wifi_web_ip();
#if SET_WIDE
    if (wifi_hotspot_on()) {
        // two codes: join the open hotspot, then open its page
        snprintf(join, sizeof(join), "WIFI:S:%s;T:nopass;;", wifi_hotspot_ssid());
        snprintf(url, sizeof(url), "http://%s/", wifi_hotspot_ip());
        snprintf(ip, sizeof(ip), "%s", wifi_hotspot_ip());
        int x = qr_block(BACK_W + 6, qr_px(join, SCR_H - 4), join, "1. JOIN", "THE WIFI (NO PASSWORD)",
                         wifi_hotspot_ssid(), set_c_text, 284);
        qr_block(x, qr_px(url, SCR_H - 4), url, "2. OPEN", "THE PAGE IN A BROWSER", ip, set_c_accent, SCR_W - x);
        if (wifi_state() == WIFI_CONNECTED) {
            char t[64];
            snprintf(t, sizeof(t), "ON THE HOME WIFI: %s", wifi_ip());
            gfx_text(BACK_W + 6 + 284 - 120, 160 + QR_OY, t, 1, set_c_dim);
        }
    } else if (web) {
        snprintf(url, sizeof(url), "http://%s/", web);
        snprintf(ip, sizeof(ip), "%s", web);
        int px = qr_px(url, SCR_H - 4);
        qr_block(BACK_W + 40, px, url, "OPEN THE PAGE", "IN A BROWSER, ON THE SAME WIFI:", ip, set_c_accent, 480);
        int tx = BACK_W + 40 + (px ? qr_side(px, url) : 0) + 10;
        gfx_text(tx, 104 + QR_OY, "EXPORTS, MP3, MIDI SONGS,", 1, set_c_dim);
        gfx_text(tx, 116 + QR_OY, "GAMEPAD SETUP", 1, set_c_dim);
    } else {
        gfx_text_center_in(BACK_W, SCR_W - BACK_W, 30 + QR_OY, "NO WEB PAGE NOW", 3, set_c_text);
        gfx_text_center_in(BACK_W, SCR_W - BACK_W, 66 + QR_OY, "CONNECT TO WIFI, OR START THE HOTSPOT:", 1, set_c_dim);
        bool hot = set_qr_press == CTRL_QR_START;
        gfx_fill_rect(QR_START_X, QR_START_Y, QR_START_W, QR_START_H, hot ? set_c_accent : set_c_box_on);
        gfx_text_center_in(QR_START_X, QR_START_W, QR_START_Y + 15, "START HOTSPOT", 2,
                           hot ? THEME_SETUP_TEXT_HOT : set_c_text);
    }
#else
    const int x0 = BACK_W + 4, w = SCR_W - x0;  // the band at the right of the BACK strip
    if (wifi_hotspot_on()) {
        // two codes side by side: join the open hotspot, then open its page
        snprintf(join, sizeof(join), "WIFI:S:%s;T:nopass;;", wifi_hotspot_ssid());
        snprintf(url, sizeof(url), "http://%s/", wifi_hotspot_ip());
        snprintf(ip, sizeof(ip), "%s", wifi_hotspot_ip());
        int half = w / 2;
        int side = half - 4 < SCR_H - 74 ? half - 4 : SCR_H - 74;
        qr_column(x0, half, -3, side, join, "1. JOIN", "THE WIFI (NO PASSWORD)", wifi_hotspot_ssid(), set_c_text);
        qr_column(x0 + half, w - half, 3, side, url, "2. OPEN", "THE PAGE IN A BROWSER", ip, set_c_accent);
        if (wifi_state() == WIFI_CONNECTED) {
            char t[64];
            snprintf(t, sizeof(t), "ON THE HOME WIFI: %s", wifi_ip());
            gfx_text_center_in(x0, w, SCR_H - 14, t, 1, set_c_dim);
        }
    } else if (web) {
        snprintf(url, sizeof(url), "http://%s/", web);
        snprintf(ip, sizeof(ip), "%s", web);
        qr_column(x0, w, 0, SCR_H - 88, url, "OPEN THE PAGE", "IN A BROWSER, ON THE SAME WIFI:", ip, set_c_accent);
        gfx_text_center_in(x0, w, SCR_H - 14, "EXPORTS, MP3, MIDI SONGS, GAMEPAD SETUP", 1, set_c_dim);
    } else {
        gfx_text_center_in(x0, w, 40, "NO WEB PAGE NOW", 3, set_c_text);
        gfx_text_center_in(x0, w, 84, "CONNECT TO WIFI, OR START THE HOTSPOT:", 1, set_c_dim);
        bool hot = set_qr_press == CTRL_QR_START;
        gfx_fill_rect(QR_START_X, QR_START_Y, QR_START_W, QR_START_H, hot ? set_c_accent : set_c_box_on);
        gfx_text_center_in(QR_START_X, QR_START_W, QR_START_Y + 17, "START HOTSPOT", 2,
                           hot ? THEME_SETUP_TEXT_HOT : set_c_text);
    }
#endif
}
