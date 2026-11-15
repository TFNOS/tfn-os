// SETUP (landscape): a list of rows that scrolls up and down: volume, brightness,
// sound, rotation, screen sleep, WiFi, hotspot, web page, tilt test and about.
// Drag up or down to scroll (a short drag of about 3 mm goes one row); tap a
// control to change it. The BACK strip at the left (or the PWR key) goes to the menu.
// Pages: the WiFi page (scan, password keyboard) and the QR page (join the
// hotspot, open the web page).
// Two layouts: the 640 x 172 bar screen (one line in each row) and the 320 x 240
// CoreS3 screen (two lines in each row: the name, then the controls). A board with
// no IMU does not show the AUTO ROTATE and TILT TEST rows.
// The parts: app_settings_wifi.c (the WiFi and QR pages), app_settings_draw.c (the drawing of the list).

#include "app_settings_priv.h"
#include "tfn/theme.h"

// the rows that this board shows (R_*), from the top
uint8_t set_rows[N_ROWS];
int set_nrows;

int set_page;

// the list: the position to draw (px, ks_pos(&s_ks)), the momentum scroll (libs/kscroll), the touch
float set_scroll;
static kscroll_t s_ks;
static bool s_touch, s_drag, s_bar_drag;
static bool s_ksd;  // this touch goes to the scroll (not a slider, not BACK)
static int s_x0, s_y0;
static int64_t s_prev;
int set_active = CTRL_NONE;
static bool s_changed_volume;

// the web password row: the keyboard types into s_wpw (never shown on the list), s_kbd_web = the keyboard is for it
static char s_wpw[WEB_PASS_MAX + 1];
bool set_kbd_web;
#if CONFIG_TFN_DEVICE_PROTECTION
static bool s_kbd_admin;
#endif

static int64_t s_about_at = -1000000;

uint16_t set_c_text, set_c_dim, set_c_box, set_c_box_on, set_c_accent, set_c_card, set_c_card2, set_c_line;

// ---------------------------------------------------------------- the list

// top of a row on the screen now (i: the place in the list, 0 = the top row)
int set_row_y(int i)
{
    return SET_LIST_Y + i * ROW_H - (int)lroundf(set_scroll) + 2;
}

static int row_at(int y)
{
    int i = (int)floorf((y - SET_LIST_Y + set_scroll - 2) / ROW_H);
    return (i >= 0 && i < set_nrows) ? i : -1;
}

// the control of a VOLUME or BRIGHTNESS row (r) at (x, y): - , the bar, + (y0, h: the band of the controls)
static int ctrl_at_level(int x, int y, int y0, int h, int r)
{
    bool v = r == R_VOLUME;
    if (ui_hit(x, y, MINUS_X - 4, y0, BTN_W + 4, h, 0)) {
        return v ? CTRL_VOL_MINUS : CTRL_BRI_MINUS;
    }
    if (ui_hit(x, y, BAR_X - 4, y0, BAR_W + 8, h, 0)) {
        return v ? CTRL_VOL_BAR : CTRL_BRI_BAR;
    }
    if (ui_hit(x, y, PLUS_X, y0, BTN_W + 4, h, 0)) {
        return v ? CTRL_VOL_PLUS : CTRL_BRI_PLUS;
    }
    return CTRL_NONE;
}

// the control of a row with one wide button at the right (RIGHT_X .. RIGHT_X + RIGHT_W), or CTRL_NONE
static int ctrl_right(int r)
{
    switch (r) {
    case R_SOUND:
        return CTRL_SOUND;
    case R_UISND:
        return CTRL_UISND;
    case R_USBSND:
        return CTRL_USBSND;
    case R_ROTATE:
        return CTRL_ROTATE;
    case R_LANG:
        return CTRL_LANG;
    case R_DEV:
        return CTRL_DEV;
    case R_MOVE:
        return CTRL_MOVE;
    case R_SKEY:
        return CTRL_SKEY;
    case R_WIFI:
        return CTRL_WIFI;
    case R_WEB:
        return CTRL_WEB;
    case R_WEBPASS:
        return CTRL_WEBPASS;
    case R_FRIENDS:
        return CTRL_FRIENDS;
    case R_NOTIFY:
        return CTRL_NOTIFY;
    case R_STANDBY:
        return CTRL_STANDBY;
    case R_PUB:
        return CTRL_PUB;
    case R_POOL:
        return CTRL_POOL;
    case R_STORE:
        return CTRL_STORE;
    case R_TCAL:
        return CTRL_TCAL;
    case R_TOUCH:
        return CTRL_TOUCH;
    default:
        return CTRL_NONE;
    }
}

static int ctrl_at(int x, int y)
{
#if BOARD_ROUND
    if (SET_RND) {
        // (the round list: BACK on the bottom cap; no row on the caps)
        if (y >= SET_LIST_B) {
            return y >= SET_BACK_Y - 6 && abs(x - SCR_W / 2) < SR(90) ? CTRL_BACK : CTRL_NONE;
        }
        if (y < SET_LIST_Y) {
            return CTRL_NONE;
        }
    } else if (x < BACK_W + 4) {
        return CTRL_BACK;
    }
#else
    if (x < BACK_W + 4) {
        return CTRL_BACK;
    }
#endif
    int i = row_at(y);
    if (i < 0) {
        return CTRL_NONE;
    }
    int r = set_rows[i];
    int y0 = set_row_y(i) + CTL_Y - 4, h = CTL_H + 8;
    if (y < set_row_y(i) || y >= set_row_y(i) + ROW_H - 4) {
        return CTRL_NONE;  // the gap between rows
    }
    if (ctrl_right(r) != CTRL_NONE) {
        return ui_hit(x, y, RIGHT_X, y0, RIGHT_W, h, 0) ? ctrl_right(r) : CTRL_NONE;
    }
    switch (r) {
    case R_VOLUME:
    case R_BRIGHT:
        return ctrl_at_level(x, y, y0, h, r);
    case R_SLEEP:
        if (ui_hit(x, y, SLP_PREV_X, y0, SLP_NEXT_X - SLP_PREV_X, h, 0)) {
            return CTRL_SLEEP_PREV;
        }
        if (ui_hit(x, y, SLP_NEXT_X, y0, SLP_NEXT_W, h, 0)) {
            return CTRL_SLEEP_NEXT;
        }
        break;
    case R_POWER:
        if (ui_hit(x, y, SLP_PREV_X, y0, SLP_NEXT_X - SLP_PREV_X, h, 0)) {
            return CTRL_PWOFF_PREV;
        }
        if (ui_hit(x, y, SLP_NEXT_X, y0, SLP_NEXT_W, h, 0)) {
            return CTRL_PWOFF_NEXT;
        }
        break;
    case R_DEEP:
        if (ui_hit(x, y, SLP_PREV_X, y0, SLP_NEXT_X - SLP_PREV_X, h, 0)) {
            return CTRL_DEEP_PREV;
        }
        if (ui_hit(x, y, SLP_NEXT_X, y0, SLP_NEXT_W, h, 0)) {
            return CTRL_DEEP_NEXT;
        }
        break;
    case R_HOTSPOT:
        if (ui_hit(x, y, HS_PREV_X, y0, BTN_W + 2, h, 0)) {
            return CTRL_HS_PREV;
        }
        if (ui_hit(x, y, HS_NEXT_X, y0, BTN_W + 2, h, 0)) {
            return CTRL_HS_NEXT;
        }
        if (ui_hit(x, y, RIGHT_X, y0, RIGHT_W, h, 0)) {
            return CTRL_HS_START;
        }
#if SET_WIDE
        if (ui_hit(x, y, HSQR_X, set_row_y(i) + HSQR_Y, HSQR_W, HSQR_H, 0) && wifi_hotspot_on()) {
            return CTRL_HS_QR;
        }
#endif
        // (320 x 240: the name line is too low for a finger. QR CODE of WEB PAGE opens the same page.)
        break;
    case R_TILT:
        if (ui_hit(x, y, TEXT_X - 8, set_row_y(i), TILT_CX + TILT_R + 8 - TEXT_X, ROW_H - 4, 0)) {
            return CTRL_TILT;
        }
        break;
    case R_ASCALE:
        if (ui_hit(x, y, MINUS_X - 4, y0, BTN_W + 4, h, 0)) {
            return CTRL_AS_MINUS;
        }
        if (ui_hit(x, y, BAR_X - 4, y0, BAR_W + 8, h, 0)) {
            return CTRL_AS_MODE;
        }
        if (ui_hit(x, y, PLUS_X, y0, BTN_W + 4, h, 0)) {
            return CTRL_AS_PLUS;
        }
        break;
    default:
        break;
    }
    return CTRL_NONE;
}

static void set_volume(int v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > AUDIO_VOL_MAX) {
        v = AUDIO_VOL_MAX;  // (over 100: the boost of the mix, audio_set_volume)
    }
    g_settings.volume = (uint8_t)v;
    audio_set_volume(v);
    s_changed_volume = true;
}

static void set_brightness(int v)
{
    if (v < 10) {
        v = 10;
    }
    if (v > 255) {
        v = 255;
    }
    g_settings.brightness = (uint8_t)v;
    board_backlight((uint8_t)v);
}

static void bar_drag(int ctrl, int x)
{
    int f = x - BAR_X;
    if (f < 0) {
        f = 0;
    }
    if (f > BAR_W) {
        f = BAR_W;
    }
    if (ctrl == CTRL_VOL_BAR) {
        set_volume(f * AUDIO_VOL_MAX / BAR_W);
    } else {
        set_brightness(10 + f * 245 / BAR_W);
    }
}

static void open_wifi_page(void)
{
    set_page = PAGE_WIFI;
    set_wfocus = -1;
    wifi_start();
    // the page opens on the state of the saved network (its address, DISCONNECT / CONNECT, AUTO); the list of
    // networks comes with SCAN. With no saved network: the scan at once.
    set_nlist = 0;
    set_list_top = 0;
    set_list_on = !wifi_saved() && wifi_state() != WIFI_CONNECTED;
    if (set_list_on) {
        wifi_scan();
    }
}

// the answer of the question of the DEVELOPER MODE (1 = TURN ON: a finger on the panel, sysui_ask_finger)
static void dev_answer(int a)
{
    if (a == 1) {
        tfn_set_dev_mode(true);
        sfx_ui(SFX_CLICK);
    }
}

// DEVELOPER MODE (on / off), or ADMIN ACCESS of a protected build (lock, or the keyboard for the web password). ON is a
// consent (an app with no signature runs native code with each permission class of its own manifest): a question, and
// only a tap by a finger on the panel turns it on (the rule of input.h; a tap of the web or of the console does not).
// OFF acts at once (the safe way).
static void do_ctrl_dev(void)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    if (GUARD_BUILD) {
        if (device_guard_local_admin()) {
            device_guard_local_lock();
            sysui_message("ADMIN ACCESS LOCKED", 3);
        } else {
            memset(s_wpw, 0, sizeof s_wpw);
            s_kbd_admin = true;
            set_kbd_web = false;
            kbd_open("ADMIN: YOUR WEB PASSWORD", s_wpw, sizeof s_wpw, true);
            set_page = PAGE_KBD;
        }
        return;
    }
#endif
    if (tfn_dev_mode()) {
        tfn_set_dev_mode(false);
        sfx_ui(SFX_CLICK);
        return;
    }
    sysui_ask("TURN ON THE DEVELOPER MODE?", "APPS WITH NO SIGNATURE CAN RUN", "TURN ON", "NO", 20, dev_answer);
    sysui_ask_closable();
    sysui_ask_finger();
    sfx_ui(SFX_TICK);
}

// WEB PASSWORD: the keyboard for a new web password (a protected build: only with the admin access)
static void do_ctrl_webpass(void)
{
    if (GUARD_BUILD && !device_guard_local_admin()) {
        sysui_message("UNLOCK ADMIN ACCESS FIRST", 4);
        return;
    }
    // the web password (web_auth.h): a new one, or an empty text = no password. The keyboard hides it (SHOW)
    s_wpw[0] = 0;
    kbd_open(GUARD_BUILD     ? "NEW ADMIN / WEB PASSWORD"
             : web_auth_on() ? "NEW WEB PASSWORD (EMPTY: NONE)"
                             : "WEB PASSWORD (4 OR MORE)",
             s_wpw, sizeof s_wpw, true);
    kbd_set_ok_question(web_auth_on() ? "CHANGE THE WEB PASSWORD?" : "SET THE WEB PASSWORD?");
    set_kbd_web = true;
    set_page = PAGE_KBD;
    sfx_ui(SFX_CLICK);
}

// STORE SOURCE: the next source (AUTO, MANAGER, STATIC). The URL of the static store stays as it is (an own URL
// stays; the default URL stays the default of the build).
static void do_ctrl_store(void)
{
    if (GUARD_BUILD && !device_guard_local_admin()) {
        sysui_message("UNLOCK ADMIN ACCESS FIRST", 4);
        return;
    }
    const char *u = tfn_sstore_url();
    char url[TFN_SS_URL], msg[64];
    snprintf(url, sizeof url, "%s", strcmp(u, TFN_STATIC_STORE_URL) ? u : "");
    if (tfn_sstore_set((tfn_sstore_mode() + 1) % (TFN_SS_STATIC + 1), url, msg, sizeof msg)) {
        sfx_ui(SFX_CLICK);
    } else {
        sfx_play(SFX_BUMP);
        sysui_message("THE STORE SOURCE IS NOT SAVED", 4);
    }
}

// the controls that open a page of SETUP: FRIENDS, NOTIFICATIONS, TRUSTED PUBLISHERS, POOL, the two calibrations.
// false = c is not one of them.
static bool do_ctrl_page(int c)
{
    switch (c) {
    case CTRL_FRIENDS:
    case CTRL_POOL:
        if (GUARD_BUILD && !device_guard_local_admin()) {
            sysui_message("UNLOCK ADMIN ACCESS FIRST", 4);
            return true;
        }
        if (c == CTRL_FRIENDS) {
            friends_page_open();
            set_page = PAGE_FRIENDS;
        } else {
            pool_page_open();
            set_page = PAGE_POOL;
        }
        break;
    case CTRL_NOTIFY:
        notify_page_open();
        set_page = PAGE_NOTIFY;
        break;
    case CTRL_STANDBY:
        standby_page_open();
        set_page = PAGE_STANDBY;
        break;
    case CTRL_PUB:
        tfn_pub_page_open();
        set_page = PAGE_PUB;
        break;
    case CTRL_TCAL:
        calib_tilt_open();
        set_page = PAGE_CALIB;
        break;
    case CTRL_TOUCH:
        calib_touch_open();
        set_page = PAGE_CALIB;
        break;
    default:
        return false;
    }
    sfx_ui(SFX_CLICK);
    return true;
}

static void do_ctrl(int c)
{
    if (do_ctrl_page(c)) {
        settings_save();
        return;
    }
    switch (c) {
    case CTRL_BACK:
        sfx_ui(SFX_BACK);
        app_open(&app_menu);
        return;
    case CTRL_VOL_MINUS:
        set_volume(g_settings.volume - 10);
        break;
    case CTRL_VOL_PLUS:
        set_volume(g_settings.volume + 10);
        break;
    case CTRL_BRI_MINUS:
        set_brightness(g_settings.brightness - 25);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_BRI_PLUS:
        set_brightness(g_settings.brightness + 25);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_SOUND:
        g_settings.mute = !g_settings.mute;
        audio_set_mute(g_settings.mute);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_UISND:  // UI SOUNDS: the ticks and the clicks of the menus
        audio_set_ui_sounds(!audio_ui_sounds());
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_USBSND:  // SOUND TO USB: the board as a USB sound input for a computer
        usb_audio_set_mode(usb_phones_supported() ? (usb_audio_mode() + 1) % 3 : usb_audio_mode() ? 0 : 1);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_ROTATE:
        g_settings.auto_rotate = !g_settings.auto_rotate;
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_LANG:  // APP LANGUAGE: all apps (each app can then set its own in its panel)
        settings_lang_all_set(settings_lang_all() == 0 ? 1 : 0);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_MOVE:  // WAKE ON MOVE (main.c check_motion): OFF = only a touch or a key lights the screen
        store_set("wake_mv", store_get("wake_mv", 1) ? 0 : 1);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_SKEY:  // SCREEN KEY (main.c screen_key): ON = a short press of the key turns the screen off
        store_set("scr_key", store_get("scr_key", 0) ? 0 : 1);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_DEV:  // TFN OS: app files with no signature can run
        do_ctrl_dev();
        break;
    case CTRL_SLEEP_PREV:
        g_settings.sleep = (uint8_t)((g_settings.sleep + SLEEP_CHOICES - 1) % SLEEP_CHOICES);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_SLEEP_NEXT:
        g_settings.sleep = (uint8_t)((g_settings.sleep + 1) % SLEEP_CHOICES);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_PWOFF_PREV:
        power_set_off_choice((power_off_choice() + POWER_OFF_CHOICES - 1) % POWER_OFF_CHOICES);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_PWOFF_NEXT:
        power_set_off_choice((power_off_choice() + 1) % POWER_OFF_CHOICES);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_DEEP_PREV:
        power_set_deep_choice((power_deep_choice() + POWER_DEEP_CHOICES - 1) % POWER_DEEP_CHOICES);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_DEEP_NEXT:
        power_set_deep_choice((power_deep_choice() + 1) % POWER_DEEP_CHOICES);
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_WIFI:
        open_wifi_page();
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_HS_PREV:
        wifi_hotspot_set_mode((hotspot_mode_t)((wifi_hotspot_mode() + HOTSPOT_MODES - 1) % HOTSPOT_MODES));
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_HS_NEXT:
        wifi_hotspot_set_mode((hotspot_mode_t)((wifi_hotspot_mode() + 1) % HOTSPOT_MODES));
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_HS_START:
        if (wifi_hotspot_on()) {
            wifi_hotspot_stop();
        } else {
            wifi_hotspot_start();
        }
        sfx_play(SFX_SWITCH);
        break;
    case CTRL_HS_QR:
        set_page = PAGE_QR;
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_WEB:
        // with no web page now, this button starts the hotspot first
        if (!wifi_web_ip()) {
            wifi_hotspot_start();
        }
        set_page = PAGE_QR;
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_WEBPASS:
        do_ctrl_webpass();
        break;
    case CTRL_STORE:
        do_ctrl_store();
        break;
    case CTRL_TILT:
        imu_set_level();
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_AS_MINUS:
    case CTRL_AS_PLUS:
        // a manual scale, from the scale now in use, in steps of 0.01
        imu_set_accel_scale(imu_accel_scale() + (c == CTRL_AS_PLUS ? 0.01f : -0.01f));
        sfx_ui(SFX_CLICK);
        break;
    case CTRL_AS_MODE:
        // AUTO -> MANUAL at the scale now in use; MANUAL -> AUTO
        imu_set_accel_scale(imu_accel_scale_auto() ? imu_accel_scale() : 0);
        sfx_ui(SFX_CLICK);
        break;
    default:
        return;
    }
    settings_save();
}

static void list_input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        s_touch = true;
        s_drag = false;
        s_x0 = ev->x;
        s_y0 = ev->y;
        set_active = ctrl_at(ev->x, ev->y);
        s_bar_drag = set_active == CTRL_VOL_BAR || set_active == CTRL_BRI_BAR;
        if (s_bar_drag) {
            bar_drag(set_active, ev->x);
        }
        // the touch goes to the scroll (a touch that stops a glide lights no row and gives no tap)
        s_ksd = !s_bar_drag && set_active != CTRL_BACK;
        if (s_ksd && ks_down(&s_ks, ev->y, ev->t_ms)) {
            set_active = CTRL_NONE;
        }
        break;
    case IN_MOVE:
        if (s_bar_drag) {
            bar_drag(set_active, ev->x);
            break;
        }
        if (s_ksd && ks_move(&s_ks, ev->y, ev->t_ms)) {
            s_drag = true;
            input_tap_cancel();  // (a scroll: no tap on a row at the lift)
            set_active = CTRL_NONE;
        }
        if (s_drag) {
            set_scroll = ks_pos(&s_ks);
        } else if (abs(ev->x - s_x0) > DRAG_PX || abs(ev->y - s_y0) > DRAG_PX) {
            set_active = ctrl_at(ev->x, ev->y) == set_active ? set_active : CTRL_NONE;
        }
        break;
    case IN_UP:
        s_touch = false;
        if (s_ksd) {
            ks_up(&s_ks, ev->t_ms);
        }
        if (s_changed_volume) {
            sfx_play(SFX_COIN);  // hear the new volume (the full level: not a UI sound)
            s_changed_volume = false;
        }
        if (s_bar_drag) {
            settings_save();
        }
        s_drag = false;
        s_bar_drag = false;
        set_active = CTRL_NONE;
        break;
    case IN_TAP: {
        if (s_ksd && !ks_tap_ok(&s_ks)) {
            break;
        }
        int c = ctrl_at(ev->x, ev->y);
        if (c != CTRL_VOL_BAR && c != CTRL_BRI_BAR) {
            do_ctrl(c);
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------- app

static void set_init(void)
{
    set_c_text = THEME_SETUP_TEXT;
    set_c_dim = THEME_SETUP_TEXT_DIM;
    set_c_box = THEME_SETUP_BOX;
    set_c_box_on = THEME_SETUP_BOX_ON;
    set_c_accent = THEME_SETUP_ACCENT;
    set_c_card = THEME_SETUP_CARD;
    set_c_card2 = THEME_SETUP_CARD2;
    set_c_line = THEME_SETUP_LINE;
    // with no IMU, AUTO ROTATE and the TILT TEST have no use
    bool imu = (app_board_caps() & APP_NEEDS_IMU) != 0;
    set_nrows = 0;
    for (int r = 0; r < N_ROWS; r++) {
        if (!imu && (r == R_ROTATE || r == R_TILT || r == R_ASCALE || r == R_TCAL || r == R_MOVE)) {
            continue;
        }
        if (BOARD_WHEEL && (r == R_TILT || r == R_ASCALE || r == R_TCAL)) {
            continue;  // (the Watcher: no motion sensor)
        }
        if (GUARD_BUILD && r == R_PUB) {
            continue;  // (a protected build: its own app key, device_guard.h)
        }
        if (BOARD_WHEEL && (r == R_USBSND || r == R_SKEY)) {
            continue;  // (the Watcher: its USB-C goes to a USB-UART
        }
        // converter, not to the chip; the wheel is no screen key)
        set_rows[set_nrows++] = (uint8_t)r;
    }
}

static void set_enter(int64_t now)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    s_kbd_admin = set_kbd_web = false;
    memset(s_wpw, 0, sizeof s_wpw);
    memset(set_pass, 0, sizeof set_pass);
#endif
    set_page = PAGE_MAIN;
    set_active = CTRL_NONE;
    set_qr_press = CTRL_NONE;
    s_changed_volume = false;
    s_touch = s_drag = s_bar_drag = false;
    set_scroll = 0;
    ks_init(&s_ks, SET_LIST_B - SET_LIST_Y, 0);  // (set_update sets the length of the list)
    s_ksd = false;
    s_prev = now;
    s_about_at = -1000000;
    imu_set_level();
}

static void set_leave(void)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    s_kbd_admin = set_kbd_web = false;
    memset(s_wpw, 0, sizeof s_wpw);
    memset(set_pass, 0, sizeof set_pass);
#endif
    friends_page_leave();
    if (set_page == PAGE_POOL) {
        pool_page_leave();  // (a JOIN of the screen ends with the screen)
    }
    settings_save();
}

static void kbd_done(int r)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    if (s_kbd_admin) {
        if (r == KBD_OK || r == KBD_CANCEL) {
            bool ok = r == KBD_OK && device_guard_local_unlock(s_wpw);
            memset(s_wpw, 0, sizeof s_wpw);
            s_kbd_admin = false;
            set_page = PAGE_MAIN;
            if (r == KBD_OK) {
                sysui_message(ok ? "ADMIN ACCESS: 2 MINUTES" : "PASSWORD REFUSED OR WAIT REQUIRED", 4);
            }
        }
        return;
    }
#endif
    if (set_kbd_web) {  // the web password row
        if (r == KBD_OK) {
            char msg[64];
            if (web_auth_set(s_wpw, msg, sizeof msg)) {
                sfx_play(SFX_COIN);
            } else {
                sfx_play(SFX_BUMP);
            }
            sysui_message(msg, 4);
        }
        if (r == KBD_OK || r == KBD_CANCEL) {
            memset(s_wpw, 0, sizeof s_wpw);
            set_kbd_web = false;
            set_page = PAGE_MAIN;
        }
        return;
    }
    if (r == KBD_OK) {
        wifi_connect_try(set_pick, set_pass);
        if (GUARD_BUILD) {
            memset(set_pass, 0, sizeof set_pass);
        }
        set_nlist = 0;
        set_list_on = false;
        set_wfocus = -1;
        set_page = PAGE_WIFI;
    } else if (r == KBD_CANCEL) {
        if (GUARD_BUILD) {
            memset(set_pass, 0, sizeof set_pass);
        }
        set_page = PAGE_WIFI;
    }
}

#if BOARD_WHEEL
// The wheel (the Watcher): it scrolls the list, moves the focus over the networks and the keys; a push selects
// the network or the key in focus. (A long push is BACK: main.c.)
static void wheel_step(int st, int pr)
{
    switch (set_page) {
    case PAGE_MAIN:
        if (st) {
            float g = ks_goal(&s_ks);
            ks_nudge(&s_ks, (float)(st * ROW_H));
            if (ks_goal(&s_ks) != g) {
                sfx_ui(SFX_TICK);
            }
        }
        break;
    case PAGE_WIFI:
        if (st && set_nlist > 0) {
            int f = set_wfocus < 0 ? set_list_top : set_wfocus + st;
            set_wfocus = f < 0 ? 0 : f >= set_nlist ? set_nlist - 1 : f;
            if (set_wfocus < set_list_top) {
                set_list_top = set_wfocus;
            }
            if (set_wfocus >= set_list_top + N_LIST) {
                set_list_top = set_wfocus - N_LIST + 1;
            }
            sfx_ui(SFX_TICK);
        }
        if (pr && set_wfocus >= 0 && set_wfocus < set_nlist) {
            set_wifi_pick(set_wfocus);
        }
        break;
    case PAGE_KBD:
        kbd_done(kbd_wheel(st, pr));
        break;
    case PAGE_SAVED:
        if (st) {
            int top = set_sv_top + st, last = wifi_saved_count() - N_LIST;
            set_sv_top = top > last ? (last > 0 ? last : 0) : top < 0 ? 0 : top;
            sfx_play(SFX_TICK);
        }
        break;
    case PAGE_ASK:
        if (st) {
            set_ask_focus = set_ask_focus < 0 ? 0 : (set_ask_focus + (st < 0 ? -st : st)) % 2;
            sfx_ui(SFX_TICK);
        }
        if (pr && set_ask_focus >= 0) {
            set_ask_answer(set_ask_focus);
        }
        break;
    default:
        break;
    }
}
#endif

static void set_input(const input_event_t *ev)
{
    switch (set_page) {
    case PAGE_WIFI:
        set_wifi_input(ev);
        return;
    case PAGE_KBD:
        kbd_done(kbd_input(ev));
        return;
    case PAGE_ASK:
        set_ask_input(ev);
        return;
    case PAGE_SAVED:
        set_saved_input(ev);
        return;
    case PAGE_QR:
        set_qr_input(ev);
        return;
    case PAGE_FRIENDS:
        if (friends_page_input(ev, ev->t_ms)) {
            set_page = PAGE_MAIN;
        }
        return;
    case PAGE_NOTIFY:
        if (notify_page_input(ev, ev->t_ms)) {
            set_page = PAGE_MAIN;
        }
        return;
    case PAGE_STANDBY:
        if (standby_page_input(ev, ev->t_ms)) {
            set_page = PAGE_MAIN;
        }
        return;
    case PAGE_PUB:
        if (tfn_pub_page_input(ev, ev->t_ms)) {
            set_page = PAGE_MAIN;
        }
        return;
    case PAGE_POOL:
        if (pool_page_input(ev, ev->t_ms)) {
            pool_page_leave();
            set_page = PAGE_MAIN;
        }
        return;
    case PAGE_CALIB:
        if (calib_input(ev, ev->t_ms)) {
            set_page = PAGE_MAIN;
        }
        return;
    default:
        list_input(ev);
        return;
    }
}

static void set_update(int64_t now)
{
    s_prev = now;
    if (GUARD_BUILD && !device_guard_local_admin() && (set_page == PAGE_FRIENDS || set_page == PAGE_SAVED)) {
        friends_page_leave();
        set_page = PAGE_MAIN;
    }
    ks_set_max(&s_ks, (float)MAX_SCROLL);
    ks_update(&s_ks, now);
    set_scroll = ks_pos(&s_ks);
#if BOARD_WHEEL
    {
        int st = input_wheel(), pr = input_wheel_presses();
        if (st || pr) {
            wheel_step(st, pr);
        }
    }
#endif
    if (set_page == PAGE_FRIENDS) {
        friends_page_update(now);
    }
    if (set_page == PAGE_NOTIFY) {
        notify_page_update(now);
    }
    if (set_page == PAGE_STANDBY) {
        standby_page_update(now);
    }
    if (set_page == PAGE_PUB) {
        tfn_pub_page_update(now);
    }
    if (set_page == PAGE_POOL) {
        pool_page_update(now);
    }
    if (set_page == PAGE_CALIB) {
        calib_update(now);
    }
    if (set_page == PAGE_WIFI && !wifi_scanning()) {
        wifi_ap_t tmp[16];
        int n = wifi_scan_results(tmp, 16);
        if (n > 0 && set_nlist == 0 && set_list_on) {
            memcpy(set_list, tmp, sizeof(tmp));
            set_nlist = n;
        }
    }
    if (now - s_about_at >= 1000) {
        s_about_at = now;
        set_free_int = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        set_free_ps = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        set_free_fs = (uint32_t)(storage_free() / 1024);
        set_chip_c = board_chip_temp();
    }
}

static void set_render(int64_t now)
{
    switch (set_page) {
    case PAGE_WIFI:
        set_wifi_render(now);
        break;
    case PAGE_KBD:
        kbd_render(now);
        break;
    case PAGE_ASK:
        set_ask_render();
        break;
    case PAGE_SAVED:
        set_saved_render();
        break;
    case PAGE_QR:
        set_qr_render(now);
        break;
    case PAGE_FRIENDS:
        friends_page_render(now);
        break;
    case PAGE_NOTIFY:
        notify_page_render(now);
        break;
    case PAGE_STANDBY:
        standby_page_render(now);
        break;
    case PAGE_PUB:
        tfn_pub_page_render(now);
        break;
    case PAGE_POOL:
        pool_page_render(now);
        break;
    case PAGE_CALIB:
        calib_render(now);
        break;
    default:
        set_list_render(now);
        break;
    }
}

#include "../libs/tficon/tficon.h"
// a quad of a gear tooth at the angle a (half width hw at the root, ht at the tip)
static void set_icon_tooth(float cx, float cy, float a, float ri, float ro, float hw, float ht, uint16_t c)
{
    float ca = fcos(a), sa = fsin(a);
    float p[8] = {cx + ca * ri - sa * hw, cy + sa * ri + ca * hw, cx + ca * ro - sa * ht, cy + sa * ro + ca * ht,
                  cx + ca * ro + sa * ht, cy + sa * ro - ca * ht, cx + ca * ri + sa * hw, cy + sa * ri - ca * hw};
    tfi_convex(p, 4, c, 256);
}
// the menu icon: a silver gear and a small orange gear that turn together (one tooth in 2 s)
static void set_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x6a8cff), tfi_hex(0x27349a));
    float u = s / 100.0f, ph = tfi_ph(2);
    // the small gear, lower right, turns the other way
    float gx = x + 70 * u, gy = y + 69 * u, ga = -ph * 1.0472f + 0.26f;
    for (int k = 0; k < 6; k++) {
        set_icon_tooth(gx, gy, ga + k * 1.0472f, 11.5f * u, 17.5f * u, 4.4f * u, 3.2f * u, tfi_hex(0xf28a1c));
    }
    tfi_disc(gx, gy, 12.5f * u, tfi_hex(0xffc76e), tfi_hex(0xf07812), 256);
    tfi_disc(gx, gy, 4.5f * u, tfi_hex(0x8a3d00), tfi_hex(0xc06018), 256);
    // the big gear
    float cx = x + 42 * u, cy = y + 42 * u, a0 = ph * 0.7854f;
    for (int k = 0; k < 8; k++) {
        set_icon_tooth(cx, cy, a0 + k * 0.7854f, 20 * u, 28 * u, 5.6f * u, 4.2f * u, tfi_hex(0xc9d3e6));
    }
    tfi_disc(cx, cy, 21 * u, rgb(255, 255, 255), tfi_hex(0xaebad0), 256);
    tfi_disc(cx, cy, 9 * u, tfi_hex(0x4c5872), tfi_hex(0x8d99b0), 256);
    tfd_disc(cx, cy, 4 * u, tfi_hex(0xdfe6f2), 256);
    tfi_spark(cx - 10 * u, cy - 12 * u, 3 * u, 160);
    tfi_end();
}

const app_t app_settings = {
        .name = "SETUP",
        .landscape = true,
        .round_fit = !BOARD_ROUND,  // (a round panel: all pages have a round layout)
        .round = BOARD_ROUND ? APP_ROUND_HI : 0,
        .wheel = true,
        .needs = 0,  // a layout for the bar screen and one for 320 x 240
        .init = set_init,
        .enter = set_enter,
        .leave = set_leave,
        .input = set_input,
        .update = set_update,
        .render = set_render,
        .draw_icon = set_icon,
};
