// SETUP: the drawing of the main list (the controls, each row, the scroll bar) and the Tab5 text.
#include "app_settings_priv.h"
#include "tfn/theme.h"
#include "tfn_id.h"

// the about row: values read once a second
uint32_t set_free_int, set_free_ps, set_free_fs;
float set_chip_c = NAN;  // the temperature of the chip (board_chip_temp, once a second)

#if BOARD_UI_SMOOTH_TEXT
static const kfont_t *t5_font(int sc)
{
    return sc <= 1 ? &KF_LABEL : sc == 2 ? &KF_TEXT : sc == 3 ? &KF_BIG : &KF_TITLE;
}
static int t5_base(int y, int sc)
{
    return y + (7 * sc + t5_font(sc)->asc * 3 / 4) / 2;
}
void set_t5_text(int x, int y, const char *s, int sc, uint16_t c)
{
    ui_text(t5_font(sc), x, t5_base(y, sc), s, c);
}
int set_t5_text_width(const char *s, int sc)
{
    return ui_text_w(t5_font(sc), s);
}
void set_t5_text_center(int y, const char *s, int sc, uint16_t c)
{
    ui_text_c(t5_font(sc), gfx_w / 2, t5_base(y, sc), s, c);
}
void set_t5_text_center_in(int x, int w, int y, const char *s, int sc, uint16_t c)
{
    ui_text_c(t5_font(sc), x + w / 2, t5_base(y, sc), s, c);
}
#endif

// ---------------------------------------------------------------- drawing the list

static void button(int x, int y, int w, int h, const char *label, int scale, bool hot, bool on)
{
    gfx_fill_rect(x, y, w, h, hot ? set_c_accent : (on ? set_c_box_on : set_c_box));
    gfx_frame_rect(x, y, w, h, 1, set_c_line);
    gfx_text_center_in(x, w, y + (h - 7 * scale) / 2, label, scale, hot ? THEME_SETUP_TEXT_HOT : set_c_text);
}

// max: the end of the bar. vol: the VOLUME bar (0..AUDIO_VOL_MAX): the text is the value, a mark at 100, and the part
// over 100 (the boost) in orange
static void bar_v(int y, int value, int max, bool hot, bool vol)
{
    char buf[8];
    int fill = value * BAR_W / max;
    gfx_fill_rect(BAR_X, y + BAR_DY, BAR_W, 22, set_c_box);
    const int f100 = vol ? 100 * BAR_W / max : BAR_W;
    gfx_fill_rect(BAR_X, y + BAR_DY, fill < f100 ? fill : f100, 22, hot ? set_c_accent : THEME_SETUP_BAR);
    if (fill > f100) {
        gfx_fill_rect(BAR_X + f100, y + BAR_DY, fill - f100, 22, THEME_SETUP_BOOST);
    }
    if (vol) {
        gfx_fill_rect(BAR_X + f100 - 1, y + BAR_DY - 3, 2, 28, set_c_text);
    }
    gfx_frame_rect(BAR_X, y + BAR_DY, BAR_W, 22, 1, set_c_line);
    snprintf(buf, sizeof(buf), "%d%%", vol ? value : value * 100 / max);
    gfx_text_center_in(BAR_X, BAR_W, y + BAR_DY + 4, buf, 2, set_c_text);
}
static void bar(int y, int value, int max, bool hot)
{
    bar_v(y, value, max, hot, false);
}

// an ON/OFF switch: a pill with a knob
// the second line of SOUND OUT (the CoreS3): the headphones that did not start tell why
// the same in two short lines (the BOX rows: the text must end before the button at the right)
static void usbsnd_sub2(int m, const char **a, const char **b)
{
    *a = "SPEAKER, COMPUTER";
    *b = "OR USB-C HEADPHONES";
    if (m != 2) {
        return;
    }
    if (usb_phones_state() == UP_FAILED) {
        *a = "THE HEADPHONES";
        *b = "DID NOT START";
    } else if (usb_phones_cable()) {
        *a = "UNPLUG THE PC OR";
        *b = "CHARGER CABLE";
    } else if (usb_phones_state() == UP_PLAYING) {
        *a = "THE SOUND GOES TO";
        *b = "THE HEADPHONES";
    } else {
        *a = "PLUG IN THE USB-C";
        *b = "HEADPHONES";
    }
}
// STORE SOURCE: the setting (the button) and the source of the store now, in two short lines (AUTO: the manager
// when the board knows a manager, else the static store)
static const char *store_name(void)
{
    static const char *const N[3] = {"AUTO", "MANAGER", "STATIC"};
    int m = tfn_sstore_mode();
    return N[m >= TFN_SS_AUTO && m <= TFN_SS_STATIC ? m : TFN_SS_AUTO];
}
static void store_lines(const char **a, const char **b)
{
    *a = tfn_sstore_mode() == TFN_SS_AUTO ? "AUTO, NOW FROM" : "ONLY FROM";
    *b = tfn_sstore_active() ? "THE STATIC STORE" : "THE MANAGER";
}
static void store_btn(int cy)
{
    const char *n = store_name();
    int sc = gfx_text_width(n, 2) <= RIGHT_W - 8 ? 2 : 1;  // (the big text when it fits in the button)
    button(RIGHT_X, cy, RIGHT_W, CTL_H, n, sc, set_active == CTRL_STORE, true);
}
static const char *usbsnd_sub(int m)
{
    if (m == 2 && usb_phones_state() == UP_FAILED) {
        return "THE USB DEVICE DOES NOT WORK";
    }
    if (m == 2 && usb_phones_cable()) {
        return "UNPLUG THE PC OR CHARGER CABLE";
    }
    if (m == 2 && usb_phones_state() == UP_PLAYING) {
        return "THE SOUND GOES TO THE HEADPHONES";
    }
    if (m == 2) {
        return "PLUG IN THE USB-C HEADPHONES";
    }
    return "THE SPEAKER, A COMPUTER (USB), USB-C HEADPHONES";
}
static void toggle(int y, bool on, bool hot)
{
    int x = RIGHT_X, w = RIGHT_W;
    gfx_fill_rect(x, y, w, CTL_H, hot ? set_c_accent : (on ? set_c_box_on : set_c_box));
    gfx_frame_rect(x, y, w, CTL_H, 1, set_c_line);
    int kx = on ? x + w - 38 : x + 4;
    gfx_fill_rect(kx, y + 4, 34, CTL_H - 8, THEME_SETUP_KNOB);
    gfx_text_center_in(on ? x : x + 38, w - 38, y + CTL_H / 2 - 7, on ? "ON" : "OFF", 2,
                       hot ? THEME_SETUP_TEXT_HOT : set_c_text);
}

static void arrow_btn(int x, int y, bool left, bool hot)
{
    button(x, y, BTN_W, CTL_H, left ? "<" : ">", 2, hot, false);
}

// SCREEN SLEEP and POWER OFF: < and the value box (a tap on the box goes to the next value)
static void value_ctl(int cy, const char *val, int c_prev, int c_next)
{
    bool hot = set_active == c_next;
    arrow_btn(SLP_PREV_X, cy, true, set_active == c_prev);
    gfx_fill_rect(SLP_NEXT_X, cy, SLP_NEXT_W, CTL_H, hot ? set_c_accent : set_c_box);
    gfx_frame_rect(SLP_NEXT_X, cy, SLP_NEXT_W, CTL_H, 1, set_c_line);
    gfx_text_center_in(SLP_NEXT_X, SLP_NEXT_W - 20, cy + CTL_H / 2 - 7, val, 2,
                       hot ? THEME_SETUP_TEXT_HOT : set_c_text);
    gfx_text(SLP_NEXT_X + SLP_NEXT_W - 18, cy + CTL_H / 2 - 7, ">", 2, hot ? THEME_SETUP_TEXT_HOT : set_c_dim);
}
static void sleep_ctl(int cy)
{
    value_ctl(cy, SLEEP_NAMES[g_settings.sleep], CTRL_SLEEP_PREV, CTRL_SLEEP_NEXT);
}
static void pwoff_ctl(int cy)
{
    value_ctl(cy, POWER_OFF_NAMES[power_off_choice()], CTRL_PWOFF_PREV, CTRL_PWOFF_NEXT);
}

// HOTSPOT: < mode > and START / STOP
static void hotspot_ctl(int cy, bool on)
{
    hotspot_mode_t m = wifi_hotspot_mode();
    arrow_btn(HS_PREV_X, cy, true, set_active == CTRL_HS_PREV);
    if (HS_NEXT_X - HS_PREV_X - BTN_W >= 30) {  // (the Watcher: the mode is on the name line)
        gfx_text_center_in(HS_PREV_X + BTN_W + 2, HS_NEXT_X - HS_PREV_X - BTN_W - 4, cy + CTL_H / 2 - 7,
                           HOTSPOT_MODE_NAMES[m], 2, m == HOTSPOT_OFF ? set_c_dim : set_c_accent);
    }
    arrow_btn(HS_NEXT_X, cy, false, set_active == CTRL_HS_NEXT);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, on ? "STOP" : "START", 2, set_active == CTRL_HS_START, on);
}

// WEB PAGE: QR CODE, or START HOTSPOT when there is no web page
static void web_ctl(int cy, bool page)
{
    if (page) {
        button(RIGHT_X, cy, RIGHT_W, CTL_H, "QR CODE", 2, set_active == CTRL_WEB, false);
        return;
    }
    bool hot = set_active == CTRL_WEB;
    gfx_fill_rect(RIGHT_X, cy, RIGHT_W, CTL_H, hot ? set_c_accent : set_c_box_on);
    gfx_frame_rect(RIGHT_X, cy, RIGHT_W, CTL_H, 1, set_c_line);
    gfx_text_center_in(RIGHT_X, RIGHT_W, cy + CTL_H / 2 - 14, "START", 2, hot ? THEME_SETUP_TEXT_HOT : set_c_text);
    gfx_text_center_in(RIGHT_X, RIGHT_W, cy + CTL_H / 2 + 6, "HOTSPOT", 1, hot ? THEME_SETUP_TEXT_HOT : set_c_text);
}

// TILT SCALE: - , the value box (AUTO or MANUAL and the scale; a tap: AUTO / MANUAL), +
static void ascale_ctl(int y, int cy)
{
    char buf[24];
    bool hot = set_active == CTRL_AS_MODE, au = imu_accel_scale_auto();
    button(MINUS_X, cy, BTN_W, CTL_H, "-", 2, set_active == CTRL_AS_MINUS, false);
    gfx_fill_rect(BAR_X, cy, BAR_W, CTL_H, hot ? set_c_accent : (au ? set_c_box_on : set_c_box));
    gfx_frame_rect(BAR_X, cy, BAR_W, CTL_H, 1, set_c_line);
    snprintf(buf, sizeof(buf), "%s X%.2f", au ? "AUTO" : "SET", (double)imu_accel_scale());
    gfx_text_center_in(BAR_X, BAR_W, cy + CTL_H / 2 - 7, buf, 2, hot ? THEME_SETUP_TEXT_HOT : set_c_text);
    button(PLUS_X, cy, BTN_W, CTL_H, "+", 2, set_active == CTRL_AS_PLUS, false);
    (void)y;
}

// TILT TEST: a ring with a ball that shows the tilt, center at (TILT_CX, ccy)
static void tilt_ctl(int ccy)
{
    int cx = TILT_CX;
    gfx_ring(cx, ccy, TILT_R, TILT_R - 2, set_active == CTRL_TILT ? set_c_accent : set_c_line);
    gfx_fill_rect(cx - TILT_R + 4, ccy, 2 * TILT_R - 8, 1, set_c_box);
    gfx_fill_rect(cx, ccy - TILT_R + 4, 1, 2 * TILT_R - 8, set_c_box);
    if (imu_ok()) {
        float gx, gy;
        imu_tilt_logical(&gx, &gy);
        float m = sqrtf(gx * gx + gy * gy);
        if (m > 1.0f) {
            gx /= m;
            gy /= m;
        }
        int dx = (int)(gx * (TILT_R - 7)), dy = (int)(gy * (TILT_R - 7));
        gfx_fill_circle(cx + dx, ccy + dy, 5, THEME_SETUP_BALL);
        gfx_fill_rect(cx + dx - 2, ccy + dy - 2, 2, 2, THEME_SETUP_BALL_SHINE);
    } else {
        gfx_text_center_in(cx - 40, 80, ccy - 3, "NO IMU", 1, THEME_SETUP_TILT_FAULT);
    }
}

// The ABOUT row: the device ID (tfn_id.h) at the right end of the line at y. left = the right end of the text before
// it on that line: with too little room, or with no ID yet, the row shows no ID.
static void about_id(int y, int left, char *buf, size_t bsz)
{
    if (!tfn_id_str()[0]) {
        return;
    }
    snprintf(buf, bsz, "ID %s", tfn_id_str());
    const int x = LIST_X + LIST_W - 8 - gfx_text_width(buf, 1);
    if (x >= left + 8) {
        gfx_text(x, y, buf, 1, set_c_dim);
    }
}

#if SET_WIDE
// the DEVELOPER MODE row (a protected build: ADMIN ACCESS) and ON / OFF
static void row_dev(int y, int cy)
{
    gfx_text(TEXT_X, y + 12, GUARD_BUILD ? "ADMIN ACCESS" : "DEVELOPER MODE", 2, set_c_text);
    gfx_text(TEXT_X, y + 32, GUARD_BUILD ? "UNLOCK WITH YOUR WEB PASSWORD" : "APP FILES WITH NO SIGNATURE CAN RUN", 1,
             set_c_dim);
    toggle(cy, GUARD_BUILD ? device_guard_local_admin() : tfn_dev_mode(), set_active == CTRL_DEV);
}
// the TRUSTED PUBLISHERS row: the count of the keys and OPEN
static void row_pub(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 12, "TRUSTED PUBLISHERS", 2, set_c_text);
    snprintf(buf, bsz, "%d BUILT-IN STORE KEY%s, %d ADDED", tfn_pub_builtin_count(),
             tfn_pub_builtin_count() == 1 ? "" : "S", tfn_pub_count());
    gfx_text(TEXT_X, y + 32, buf, 1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_PUB, false);
}
// the POOL row: the state of the pool (pool_page.h) and OPEN
static void row_pool(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 12, "POOL", 2, set_c_text);
    pool_page_row_text(buf, (int)bsz);
    gfx_text(TEXT_X, y + 32, buf, 1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_POOL, false);
}
// the STORE SOURCE row: the source of the store now and AUTO / MANAGER / STATIC
static void row_store(int y, int cy, char *buf, size_t bsz)
{
    const char *a, *b;
    store_lines(&a, &b);
    gfx_text(TEXT_X, y + 12, "STORE SOURCE", 2, set_c_text);
    snprintf(buf, bsz, "%s %s", a, b);
    gfx_text(TEXT_X, y + 32, buf, 1, set_c_dim);
    store_btn(cy);
}
// the TILT SCALE row: the value of the sensor and - , AUTO / SET, +
static void row_ascale(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 12, "TILT SCALE", 2, set_c_text);
    snprintf(buf, bsz, "SENSOR %.2f G STILL", (double)imu_raw_g());
    gfx_text(TEXT_X, y + 32, buf, 1, set_c_dim);
    ascale_ctl(y, cy);
}
// the WIFI row: the state in one line and NETWORKS / SET UP
static void row_wifi(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 12, "WIFI", 2, set_c_text);
    uint16_t sc = set_wifi_line(buf, bsz);
    gfx_text(TEXT_X, y + 32, buf, 1, sc);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, wifi_state() == WIFI_CONNECTED ? "NETWORKS" : "SET UP", 2,
           set_active == CTRL_WIFI, false);
}
// the TILT TEST row: the text and the ring with the ball
static void row_tilt(int y)
{
    gfx_text(TEXT_X, y + 12, "TILT TEST", 2, set_c_text);
    gfx_text(TEXT_X, y + 32, "TAP: THIS POSITION IS LEVEL", 1, set_c_dim);
    tilt_ctl(y + (ROW_H - 4) / 2);
}
// the TILT CALIBRATE row: the state and OPEN
static void row_tcal(int y, int cy)
{
    gfx_text(TEXT_X, y + 12, "TILT CALIBRATE", 2, set_c_text);
    gfx_text(TEXT_X, y + 32, imu_accel_cal_on() ? "ON: THE 6 SIDES ARE MEASURED" : "OFF. FOR A SENSOR THAT READS WRONG",
             1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_TCAL, false);
}
// the WAKE ON MOVE row: the state and ON / OFF
static void row_move(int y, int cy)
{
    bool on = store_get("wake_mv", 1) != 0;
    gfx_text(TEXT_X, y + 12, "WAKE ON MOVE", 2, set_c_text);
    gfx_text(TEXT_X, y + 32,
             on ? "A MOVE LIGHTS THE SCREEN AND KEEPS IT ON" : "ONLY A TOUCH OR A KEY (A BOARD IN A BAG STAYS DARK)", 1,
             set_c_dim);
    toggle(cy, on, set_active == CTRL_MOVE);
}
// the SCREEN KEY row: the state and ON / OFF
static void row_skey(int y, int cy)
{
    bool on = store_get("scr_key", 0) != 0;
    gfx_text(TEXT_X, y + 12, "SCREEN KEY", 2, set_c_text);
    gfx_text(TEXT_X, y + 32, on ? "BOOT: THE SCREEN OFF AND ON. PWR IN THE MENU: OFF" : "OFF: THE KEYS ACT AS BEFORE",
             1, set_c_dim);
    toggle(cy, on, set_active == CTRL_SKEY);
}
// the WEB PAGE row: the address and QR CODE (or START HOTSPOT)
static void row_web(int y, int cy, char *buf, size_t bsz)
{
    const char *ip = wifi_web_ip();
    gfx_text(TEXT_X, y + 8, "WEB PAGE", 2, set_c_text);
    if (ip) {
        snprintf(buf, bsz, "http://%s", ip);
        gfx_text(TEXT_X, y + 30, buf, 2, set_c_accent);
    } else {
        gfx_text(TEXT_X, y + 30, "NO WEB PAGE NOW", 2, set_c_dim);
    }
    web_ctl(cy, ip != NULL);
}
// the WEB PASSWORD row: the state and SET / CHANGE
static void row_webpass(int y, int cy)
{
    bool on = web_auth_on();
    gfx_text(TEXT_X, y + 12, "WEB PASSWORD", 2, set_c_text);
    gfx_text(TEXT_X, y + 32,
             on ? "ON: THE WEB PAGE AND ITS API NEED IT" : "OFF: EACH DEVICE ON THE WIFI CAN USE THE PAGE", 1,
             on ? THEME_SETUP_OK : THEME_SETUP_WARN);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, on ? "CHANGE" : "SET", 2, set_active == CTRL_WEBPASS, false);
}
// the FRIENDS row: the count, the name of this board and OPEN
static void row_friends(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 12, "FRIENDS", 2, set_c_text);
    snprintf(buf, bsz, "%d FRIEND%s. THIS BOARD: %s", friends_count(), friends_count() == 1 ? "" : "S",
             GUARD_BUILD && !device_guard_local_admin() ? "ADMIN ACCESS REQUIRED" : friends_my_name());
    gfx_text(TEXT_X, y + 32, buf, 1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_FRIENDS, false);
}
// the NOTIFICATIONS row: the count of the notices and OPEN
static void row_notify(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 12, "NOTIFICATIONS", 2, set_c_text);
    int un = notify_unread(NULL);
    if (!notify_all_on()) {
        snprintf(buf, bsz, "ALL OFF");
    }
#if BOARD_UI_SMOOTH_TEXT
    // (the Tab5: the font is wider than the dot font; the long text went under OPEN)
    else {
        snprintf(buf, bsz, "%d NOTICE%s, %d UNREAD", notify_count(), notify_count() == 1 ? "" : "S", un);
    }
#else
    else {
        snprintf(buf, bsz, "%d NOTICE%s, %d UNREAD. THE APPS, SOUND, DO NOT DISTURB", notify_count(),
                 notify_count() == 1 ? "" : "S", un);
    }
#endif
    gfx_text(TEXT_X, y + 32, buf, 1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_NOTIFY, false);
}
// the TEMPERATURE row: the temperature of the chip in green, yellow or red
static void row_temp(int y, char *buf, size_t bsz)
{
#if BOARD_UI_SMOOTH_TEXT
    // An info line with the temperature. The value is in the Rubik face at the right (green, yellow from
    // 60 C, red from 75 C), a degree ring and C.
    gfx_text(TEXT_X, y + 12, "TEMPERATURE", 2, set_c_text);
    gfx_text(TEXT_X, y + 32, "THE CHIP (ESP32-P4), READ EACH SECOND", 1, set_c_dim);
    const bool ok = !isnan(set_chip_c);
    if (ok) {
        snprintf(buf, bsz, "%.1f", (double)set_chip_c);
    } else {
        snprintf(buf, bsz, "--");
    }
    const uint16_t tc = !ok               ? set_c_dim
                        : set_chip_c < 60 ? THEME_SETUP_LEVEL_OK
                        : set_chip_c < 75 ? THEME_SETUP_LEVEL_WARN
                                          : THEME_SETUP_LEVEL_HIGH;
    const int cw = ui_text_w(&KF_BIG, "C"), tw = ui_text_w(&KF_BIG, buf), base = y + (ROW_H - 4) / 2 + KF_BIG.asc / 2;
    const int cx = RIGHT_X + RIGHT_W - cw, rx = cx - 7, tx = rx - 7 - tw;
    ui_text(&KF_BIG, tx, base, buf, tc);
    ui_ring(rx, base - KF_BIG.asc + 4, 4, 2, tc, 255);
    ui_text(&KF_BIG, cx, base, "C", tc);
#else
    // The temperature on the 3.49 and the other ESP32-S3 boards (green, yellow from 60 C, red from 75 C)
    gfx_text(TEXT_X, y + 12, "TEMPERATURE", 2, set_c_text);
    gfx_text(TEXT_X, y + 32, "THE CHIP (ESP32-S3), READ EACH SECOND", 1, set_c_dim);
    const bool ok = !isnan(set_chip_c);
    if (ok) {
        snprintf(buf, bsz, "%.1f C", (double)set_chip_c);
    } else {
        snprintf(buf, bsz, "--");
    }
    const uint16_t tc = !ok               ? set_c_dim
                        : set_chip_c < 60 ? THEME_SETUP_LEVEL_OK
                        : set_chip_c < 75 ? THEME_SETUP_LEVEL_WARN
                                          : THEME_SETUP_LEVEL_HIGH;
    gfx_text(RIGHT_X + RIGHT_W - gfx_text_width(buf, 3), y + (ROW_H - 4) / 2 - 10, buf, 3, tc);
#endif
}
// the ABOUT row: the firmware, the memory, the files, the time up, the addresses
static void row_about(int y, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 4, "ABOUT", 2, set_c_text);
    snprintf(buf, bsz, "FIRMWARE %s (%s)", ota_version(), ota_slot());
    gfx_text(TEXT_X + 76, y + 8, buf, 1, set_c_text);
    about_id(y + 8, TEXT_X + 76 + gfx_text_width(buf, 1), buf, bsz);
    snprintf(buf, bsz, "MEMORY %lu KB + %lu KB   FILES %lu.%lu MB FREE", (unsigned long)(set_free_int / 1024),
             (unsigned long)(set_free_ps / 1024), (unsigned long)(set_free_fs / 1024),
             (unsigned long)(set_free_fs % 1024 * 10 / 1024));
#if BOARD_UI_SMOOTH_TEXT
    // (the Tab5: the lines of the font are taller than the dot lines: two columns of two lines, 16 px apart)
    snprintf(buf, bsz, "MEMORY %lu KB + %lu KB", (unsigned long)(set_free_int / 1024),
             (unsigned long)(set_free_ps / 1024));
    gfx_text(TEXT_X, y + 22, buf, 1, set_c_dim);
    snprintf(buf, bsz, "FILES %lu.%lu MB FREE", (unsigned long)(set_free_fs / 1024),
             (unsigned long)(set_free_fs % 1024 * 10 / 1024));
    gfx_text(TEXT_X, y + 38, buf, 1, set_c_dim);
    const int x2 = TEXT_X + 220;
#else
    gfx_text(TEXT_X, y + 22, buf, 1, set_c_dim);
#endif
    int64_t up = esp_timer_get_time() / 1000000;
    snprintf(buf, bsz, "UP %d:%02d:%02d   GAMEPAD NAME: %s", (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60),
             bt_pad_name());
#if BOARD_UI_SMOOTH_TEXT
    gfx_text(x2, y + 22, buf, 1, set_c_dim);
#else
    gfx_text(TEXT_X, y + 32, buf, 1, set_c_dim);
#endif
    char a[24] = "-", b[24] = "OFF";
    if (wifi_state() == WIFI_CONNECTED) {
        snprintf(a, sizeof(a), "%s", wifi_ip());
    }
    if (wifi_hotspot_on()) {
        snprintf(b, sizeof(b), "%s", wifi_hotspot_ip());
    }
    snprintf(buf, bsz, "WIFI %s   HOTSPOT %s", a, b);
#if BOARD_UI_SMOOTH_TEXT
    gfx_text(x2, y + 38, buf, 1, set_c_dim);
#else
    gfx_text(TEXT_X, y + 42, buf, 1, set_c_dim);
#endif
}

// the rows with a button at the right that opens a page or changes one setting
static void row_open(int r, int y, int cy, char *buf, size_t bsz)
{
    if (r == R_DEV) {
        row_dev(y, cy);
        return;
    }
    (r == R_PUB ? row_pub : r == R_POOL ? row_pool : r == R_STORE ? row_store : row_notify)(y, cy, buf, bsz);
}

static void draw_row(int i, int64_t now)
{
    int r = set_rows[i];
    int y = set_row_y(i);
    if (y + ROW_H < 0 || y >= SCR_H) {
        return;
    }
    gfx_fill_rect(LIST_X, y, LIST_W, ROW_H - 4, (i & 1) ? set_c_card2 : set_c_card);
    gfx_fill_rect(LIST_X, y, 3, ROW_H - 4, set_c_line);
    int cy = y + CTL_Y;
    char buf[96];
    switch (r) {
    case R_VOLUME:
    case R_BRIGHT: {
        bool v = r == R_VOLUME;
        gfx_text(TEXT_X, y + 20, v ? "VOLUME" : "BRIGHTNESS", 2, set_c_text);
        button(MINUS_X, cy, BTN_W, CTL_H, "-", 2, set_active == (v ? CTRL_VOL_MINUS : CTRL_BRI_MINUS), false);
        if (v) {
            bar_v(y, g_settings.volume, AUDIO_VOL_MAX, set_active == CTRL_VOL_BAR, true);
        } else {
            bar(y, g_settings.brightness - 10, 245, set_active == CTRL_BRI_BAR);
        }
        button(PLUS_X, cy, BTN_W, CTL_H, "+", 2, set_active == (v ? CTRL_VOL_PLUS : CTRL_BRI_PLUS), false);
        break;
    }
    case R_SOUND:
        gfx_text(TEXT_X, y + 12, "SOUND", 2, set_c_text);
        gfx_text(TEXT_X, y + 32, "ALL SOUND AND MUSIC", 1, set_c_dim);
        toggle(cy, !g_settings.mute, set_active == CTRL_SOUND);
        break;
    case R_UISND:
        gfx_text(TEXT_X, y + 12, "UI SOUNDS", 2, set_c_text);
        gfx_text(TEXT_X, y + 32, "THE TICKS AND CLICKS OF THE MENUS", 1, set_c_dim);
        toggle(cy, audio_ui_sounds(), set_active == CTRL_UISND);
        break;
    case R_USBSND:
        if (usb_phones_supported()) {  // (the CoreS3: three places for the sound)
            static const char *const M[3] = {"SPEAKER", "COMPUTER", "HEADPHONES"};
            int m = usb_audio_mode();
            gfx_text(TEXT_X, y + 12, "SOUND OUT", 2, set_c_text);
            gfx_text(TEXT_X, y + 32, usbsnd_sub(m), 1, set_c_dim);
            button(RIGHT_X, cy, RIGHT_W, CTL_H, M[m], 1, set_active == CTRL_USBSND, m != 0);
            break;
        }
        gfx_text(TEXT_X, y + 12, "SOUND TO USB", 2, set_c_text);
        gfx_text(TEXT_X, y + 32, "TO A COMPUTER, NOT THE SPEAKER", 1, set_c_dim);
        toggle(cy, usb_audio_setting(), set_active == CTRL_USBSND);
        break;
    case R_ROTATE:
        gfx_text(TEXT_X, y + 12, "AUTO ROTATE", 2, set_c_text);
        gfx_text(TEXT_X, y + 32, "TURN THE PICTURE WITH THE BOARD", 1, set_c_dim);
        toggle(cy, g_settings.auto_rotate, set_active == CTRL_ROTATE);
        break;
    case R_LANG:
        gfx_text(TEXT_X, y + 12, "APP LANGUAGE", 2, set_c_text);
        gfx_text(TEXT_X, y + 32, "ALL APPS; EACH APP CAN CHANGE IT IN ITS PANEL", 1, set_c_dim);
        button(RIGHT_X, cy, RIGHT_W, CTL_H, settings_lang_all() == 0 ? "ENGLISH" : "HEBREW", 1, set_active == CTRL_LANG,
               true);
        break;
    case R_SLEEP:
        gfx_text(TEXT_X, y + 12, "SCREEN SLEEP", 2, set_c_text);
        gfx_text(TEXT_X, y + 32, "DIM, THEN OFF, WITH NO ACTIVITY", 1, set_c_dim);
        sleep_ctl(cy);
        break;
    case R_POWER:
        gfx_text(TEXT_X, y + 12, "POWER OFF", 2, set_c_text);
        gfx_text(TEXT_X, y + 32, "SCREEN OFF THIS LONG, ON BATTERY", 1, set_c_dim);
        pwoff_ctl(cy);
        break;
    case R_MOVE:
        row_move(y, cy);
        break;
    case R_SKEY:
        row_skey(y, cy);
        break;
    case R_WIFI:
        row_wifi(y, cy, buf, sizeof(buf));
        break;
    case R_HOTSPOT: {
        bool on = wifi_hotspot_on();
        gfx_text(TEXT_X, y + 6, "HOTSPOT", 2, set_c_text);
        if (on) {
            int n = wifi_hotspot_clients();
            snprintf(buf, sizeof(buf), "%s  %d PHONE%s", wifi_hotspot_ssid(), n, n == 1 ? "" : "S");
            gfx_text(TEXT_X, y + 26, buf, 1, THEME_SETUP_OK);
            int left = wifi_hotspot_left_s();  // the 2-minute hotspot of AUTO (no phone on it)
            if (left >= 0 && n == 0) {
                snprintf(buf, sizeof(buf), "TAP HERE: QR CODE  OFF IN %d:%02d", left / 60, left % 60);
            } else {
                snprintf(buf, sizeof(buf), "TAP HERE: QR CODE");
            }
            gfx_text(TEXT_X, y + 38, buf, 1, set_c_dim);
        } else {
            gfx_text(TEXT_X, y + 26, "OWN OPEN WIFI FOR THE", 1, set_c_dim);
            gfx_text(TEXT_X, y + 38, "WEB PAGE (NO WIFI NEAR)", 1, set_c_dim);
        }
        hotspot_ctl(cy, on);
        break;
    }
    case R_WEB:
        row_web(y, cy, buf, sizeof(buf));
        break;
    case R_WEBPASS:
        row_webpass(y, cy);
        break;
    case R_FRIENDS:
        row_friends(y, cy, buf, sizeof(buf));
        break;
    case R_DEV:
    case R_NOTIFY:
    case R_PUB:
    case R_POOL:
    case R_STORE:
        row_open(r, y, cy, buf, sizeof(buf));
        break;
    case R_TILT:
        row_tilt(y);
        break;
    case R_TCAL:
        row_tcal(y, cy);
        break;
    case R_TOUCH: {
        float tm[6];
        gfx_text(TEXT_X, y + 12, "TOUCH TEST", 2, set_c_text);
        gfx_text(TEXT_X, y + 32, input_touch_cal_get(tm) ? "CALIBRATION: ON" : "CALIBRATION: OFF", 1, set_c_dim);
        button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_TOUCH, false);
        break;
    }
    case R_ASCALE:
        row_ascale(y, cy, buf, sizeof(buf));
        break;
    case R_TEMP:
        row_temp(y, buf, sizeof(buf));
        break;
    case R_MEM: {
        // The free RAM (a full PSRAM stops the load of a big app: "NO MEMORY FOR ...")
        gfx_text(TEXT_X, y + 12, "FREE MEMORY", 2, set_c_text);
        snprintf(buf, sizeof(buf), "PSRAM AT THE RIGHT; INTERNAL RAM %lu KB", (unsigned long)(set_free_int / 1024));
        gfx_text(TEXT_X, y + 32, buf, 1, set_c_dim);
        snprintf(buf, sizeof(buf), "%lu.%lu MB", (unsigned long)(set_free_ps / 1048576),
                 (unsigned long)(set_free_ps % 1048576 * 10 / 1048576));
        const uint16_t mc = set_free_ps >= 2048u * 1024  ? THEME_SETUP_LEVEL_OK
                            : set_free_ps >= 768u * 1024 ? THEME_SETUP_LEVEL_WARN
                                                         : THEME_SETUP_LEVEL_HIGH;
        gfx_text(RIGHT_X + RIGHT_W - gfx_text_width(buf, 3), y + (ROW_H - 4) / 2 - 10, buf, 3, mc);
        break;
    }
    default:
        row_about(y, buf, sizeof(buf));
        break;
    }
}
#else
// a short note at the right end of the name line
static void note(int y, const char *s, uint16_t c)
{
    gfx_text(NOTE_R - gfx_text_width(s, 1), y + 10, s, 1, c);
}

// the WiFi state in two short lines (the button is at the right of them)
static uint16_t wifi_lines(char *a, char *b, int len)
{
    b[0] = 0;
    switch (wifi_state()) {
    case WIFI_CONNECTED:
        snprintf(a, len, "ON %.*s", NAME_N, wifi_ssid());
        snprintf(b, len, "%s", wifi_ip());
        return THEME_SETUP_OK;
    case WIFI_CONNECTING:
        snprintf(a, len, "CONNECTING TO");
        snprintf(b, len, "%.*s...", NAME_N - 3, wifi_ssid());
        return set_c_dim;
    case WIFI_FAILED:
        snprintf(a, len, "COULD NOT CONNECT");
        snprintf(b, len, "TO %.*s", NAME_N - 3, wifi_ssid());
        return THEME_SETUP_FAULT;
    default:
        snprintf(a, len, "%s", wifi_saved() ? "SAVED, NOT" : "NOT CONNECTED");
        if (wifi_saved()) {
            snprintf(b, len, "CONNECTED");
        }
        return set_c_dim;
    }
}

// the WIFI row: the state in two lines and NETWORKS / SET UP
static void row_wifi(int y, int cy, char *buf, char *b2, size_t b2sz)
{
    gfx_text(TEXT_X, y + 6, "WIFI", 2, set_c_text);
    uint16_t sc = wifi_lines(buf, b2, b2sz);
    gfx_text(TEXT_X, cy + 10, buf, 1, sc);
    gfx_text(TEXT_X, cy + 22, b2, 1, sc);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, wifi_state() == WIFI_CONNECTED ? "NETWORKS" : "SET UP", 2,
           set_active == CTRL_WIFI, false);
}
// the HOTSPOT row: the name, the state, the mode and START / STOP
static void row_hotspot(int y, int cy, char *buf, size_t bsz)
{
    bool on = wifi_hotspot_on();
    gfx_text(TEXT_X, y + 6, "HOTSPOT", 2, set_c_text);
    if (on) {
        int n = wifi_hotspot_clients();
        int left = wifi_hotspot_left_s();  // the 2-minute hotspot of AUTO (no phone on it)
        if (left >= 0 && n == 0) {
            snprintf(buf, bsz, "%.12s  OFF IN %d:%02d", wifi_hotspot_ssid(), left / 60, left % 60);
        } else {
            snprintf(buf, bsz, "%.12s  %d PHONE%s", wifi_hotspot_ssid(), n, n == 1 ? "" : "S");
        }
        note(y, buf, THEME_SETUP_OK);
    } else {
        note(y, BOARD_ROUND ? HOTSPOT_MODE_NAMES[wifi_hotspot_mode()] : "OWN OPEN WIFI", set_c_dim);
    }
    hotspot_ctl(cy, on);
}
// the WEB PAGE row: the address and QR CODE (or START HOTSPOT)
static void row_web(int y, int cy, char *buf, size_t bsz)
{
    const char *ip = wifi_web_ip();
    gfx_text(TEXT_X, y + 6, "WEB PAGE", 2, set_c_text);
    if (ip) {
        snprintf(buf, bsz, "http://%s", ip);
        gfx_text(TEXT_X, cy + 10, buf, 1, set_c_accent);
        gfx_text(TEXT_X, cy + 22, "OPEN IN A BROWSER", 1, set_c_dim);
    } else {
        gfx_text(TEXT_X, cy + 16, "NO WEB PAGE NOW", 1, set_c_dim);
    }
    web_ctl(cy, ip != NULL);
}
// the WEB PASSWORD row: the state and SET / CHANGE
static void row_webpass(int y, int cy)
{
    bool on = web_auth_on();
    uint16_t c = on ? THEME_SETUP_OK : THEME_SETUP_WARN;
    gfx_text(TEXT_X, y + 6, "WEB PASSWORD", 2, set_c_text);
    gfx_text(TEXT_X, cy + 10, on ? "ON: THE PAGE" : "OFF: OPEN TO", 1, c);
    gfx_text(TEXT_X, cy + 22, on ? "NEEDS IT" : "THE WIFI", 1, c);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, on ? "CHANGE" : "SET", 2, set_active == CTRL_WEBPASS, false);
}
// the FRIENDS row: the count, the name of this board and OPEN
static void row_friends(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 6, "FRIENDS", 2, set_c_text);
    snprintf(buf, bsz, "%d FRIEND%s", friends_count(), friends_count() == 1 ? "" : "S");
    gfx_text(TEXT_X, cy + 10, buf, 1, set_c_dim);
    gfx_text(TEXT_X, cy + 22, GUARD_BUILD && !device_guard_local_admin() ? "ADMIN ACCESS REQUIRED" : friends_my_name(),
             1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_FRIENDS, false);
}
// the NOTIFICATIONS row: the count of the notices, the unread ones and OPEN
static void row_notify(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 6, "NOTIFICATIONS", 2, set_c_text);
    if (!notify_all_on()) {
        snprintf(buf, bsz, "ALL OFF");
    } else {
        snprintf(buf, bsz, "%d NOTICE%s", notify_count(), notify_count() == 1 ? "" : "S");
    }
    gfx_text(TEXT_X, cy + 10, buf, 1, set_c_dim);
    snprintf(buf, bsz, "%d UNREAD", notify_unread(NULL));
    gfx_text(TEXT_X, cy + 22, buf, 1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_NOTIFY, false);
}
// the TRUSTED PUBLISHERS row: the count of the keys and OPEN
static void row_pub(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 6, "PUBLISHERS", 2, set_c_text);
    snprintf(buf, bsz, "%d BUILT IN", tfn_pub_builtin_count());
    gfx_text(TEXT_X, cy + 10, buf, 1, set_c_dim);
    snprintf(buf, bsz, "%d ADDED", tfn_pub_count());
    gfx_text(TEXT_X, cy + 22, buf, 1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_PUB, false);
}
// the POOL row: the state of the pool (pool_page.h) and OPEN
static void row_pool(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 6, "POOL", 2, set_c_text);
    pool_page_row_text(buf, (int)bsz);
    gfx_text(TEXT_X, cy + 10, buf, 1, set_c_dim);
    gfx_text(TEXT_X, cy + 22, "BOARDS, MANAGER", 1, set_c_dim);
    button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_POOL, false);
}
// the STORE SOURCE row: the source of the store now and AUTO / MANAGER / STATIC
static void row_store(int y, int cy, char *buf, size_t bsz)
{
    const char *a, *b;
    (void)buf;
    (void)bsz;
    store_lines(&a, &b);
    gfx_text(TEXT_X, y + 6, "STORE SOURCE", 2, set_c_text);
    gfx_text(TEXT_X, cy + 10, a, 1, set_c_dim);
    gfx_text(TEXT_X, cy + 22, b, 1, set_c_dim);
    store_btn(cy);
}
// the TEMPERATURE row: the temperature of the chip in green, yellow or red
static void row_temp(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 6, "TEMPERATURE", 2, set_c_text);
    gfx_text(TEXT_X, cy + 10, "THE CHIP", 1, set_c_dim);
    const bool ok = !isnan(set_chip_c);
    if (ok) {
        snprintf(buf, bsz, "%.1f C", (double)set_chip_c);
    } else {
        snprintf(buf, bsz, "--");
    }
    const uint16_t tc = !ok               ? set_c_dim
                        : set_chip_c < 60 ? THEME_SETUP_LEVEL_OK
                        : set_chip_c < 75 ? THEME_SETUP_LEVEL_WARN
                                          : THEME_SETUP_LEVEL_HIGH;
    gfx_text(RIGHT_X + RIGHT_W - gfx_text_width(buf, 2), cy + 4, buf, 2, tc);
}
// the FREE MEMORY row: the free PSRAM in green, yellow or red
static void row_mem(int y, int cy, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 6, "FREE MEMORY", 2, set_c_text);
    snprintf(buf, bsz, "PSRAM; INTERNAL %lu KB", (unsigned long)(set_free_int / 1024));
    gfx_text(TEXT_X, cy + 10, buf, 1, set_c_dim);
    snprintf(buf, bsz, "%lu.%lu MB", (unsigned long)(set_free_ps / 1048576),
             (unsigned long)(set_free_ps % 1048576 * 10 / 1048576));
    const uint16_t mc = set_free_ps >= 2048u * 1024  ? THEME_SETUP_LEVEL_OK
                        : set_free_ps >= 768u * 1024 ? THEME_SETUP_LEVEL_WARN
                                                     : THEME_SETUP_LEVEL_HIGH;
    gfx_text(RIGHT_X + RIGHT_W - gfx_text_width(buf, 2), cy + 4, buf, 2, mc);
}
// the ABOUT row: the firmware, the memory, the files, the time up, the addresses
static void row_about(int y, char *buf, size_t bsz)
{
    gfx_text(TEXT_X, y + 6, "ABOUT", 2, set_c_text);
    about_id(y + 10, TEXT_X + gfx_text_width("ABOUT", 2), buf, bsz);
#if BOARD_UI_SMOOTH_TEXT
    const int ap = 11, a0 = 21;  // (the smooth small font needs 11 px between two lines; the dot font 10; the
                                 // last line must end where the dot font ended: the list stops there)
#else
    const int ap = 10, a0 = 23;
#endif
    // (the firmware name can be long, a DEV build: a line of its own, not at the right of the title)
    snprintf(buf, bsz, "FIRMWARE %s", ota_version());
    gfx_text(TEXT_X, y + a0 + 0 * ap, buf, 1, set_c_text);
    snprintf(buf, bsz, "MEMORY %lu KB + %lu KB", (unsigned long)(set_free_int / 1024),
             (unsigned long)(set_free_ps / 1024));
    gfx_text(TEXT_X, y + a0 + 1 * ap, buf, 1, set_c_dim);
    int64_t up = esp_timer_get_time() / 1000000;
    snprintf(buf, bsz, "FILES %lu.%lu MB FREE   UP %d:%02d:%02d", (unsigned long)(set_free_fs / 1024),
             (unsigned long)(set_free_fs % 1024 * 10 / 1024), (int)(up / 3600), (int)(up / 60 % 60), (int)(up % 60));
    gfx_text(TEXT_X, y + a0 + 2 * ap, buf, 1, set_c_dim);
    snprintf(buf, bsz, "GAMEPAD: %.29s", bt_pad_name());
    gfx_text(TEXT_X, y + a0 + 3 * ap, buf, 1, set_c_dim);
    char a[24] = "-", b[24] = "OFF";
    if (wifi_state() == WIFI_CONNECTED) {
        snprintf(a, sizeof(a), "%s", wifi_ip());
    }
    if (wifi_hotspot_on()) {
        snprintf(b, sizeof(b), "%s", wifi_hotspot_ip());
    }
    snprintf(buf, bsz, "WIFI %s  HOTSPOT %s", a, b);
    gfx_text(TEXT_X, y + a0 + 4 * ap, buf, 1, set_c_dim);
}

// the rows with a button at the right that opens a page or changes one setting
static void row_open(int r, int y, int cy, char *buf, size_t bsz)
{
    (r == R_PUB ? row_pub : r == R_POOL ? row_pool : r == R_STORE ? row_store : row_notify)(y, cy, buf, bsz);
}

static void draw_row(int i, int64_t now)
{
    int r = set_rows[i];
    int y = set_row_y(i);
    if (y + ROW_H < 0 || y >= SCR_H) {
        return;
    }
    gfx_fill_rect(LIST_X, y, LIST_W, ROW_H - 4, (i & 1) ? set_c_card2 : set_c_card);
    gfx_fill_rect(LIST_X, y, 3, ROW_H - 4, set_c_line);
    int cy = y + CTL_Y;
    char buf[96], b2[48];
    switch (r) {
    case R_VOLUME:
    case R_BRIGHT: {
        bool v = r == R_VOLUME;
        gfx_text(TEXT_X, y + 6, v ? "VOLUME" : "BRIGHTNESS", 2, set_c_text);
        button(MINUS_X, cy, BTN_W, CTL_H, "-", 2, set_active == (v ? CTRL_VOL_MINUS : CTRL_BRI_MINUS), false);
        if (v) {
            bar_v(y, g_settings.volume, AUDIO_VOL_MAX, set_active == CTRL_VOL_BAR, true);
        } else {
            bar(y, g_settings.brightness - 10, 245, set_active == CTRL_BRI_BAR);
        }
        button(PLUS_X, cy, BTN_W, CTL_H, "+", 2, set_active == (v ? CTRL_VOL_PLUS : CTRL_BRI_PLUS), false);
        break;
    }
    case R_SOUND:
        gfx_text(TEXT_X, y + 6, "SOUND", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, "ALL SOUND", 1, set_c_dim);  // (two lines: the smooth font of the 1.75C is wider)
        gfx_text(TEXT_X, cy + 22, "AND MUSIC", 1, set_c_dim);
        toggle(cy, !g_settings.mute, set_active == CTRL_SOUND);
        break;
    case R_UISND:
        gfx_text(TEXT_X, y + 6, "UI SOUNDS", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, "TICKS AND CLICKS", 1, set_c_dim);
        gfx_text(TEXT_X, cy + 22, "OF THE MENUS", 1, set_c_dim);
        toggle(cy, audio_ui_sounds(), set_active == CTRL_UISND);
        break;
    case R_USBSND:
        if (usb_phones_supported()) {  // (the CoreS3: three places for the sound)
            static const char *const M[3] = {"SPEAKER", "COMPUTER", "HEADPHONES"};
            int m = usb_audio_mode();
            const char *l1, *l2;
            usbsnd_sub2(m, &l1, &l2);
            gfx_text(TEXT_X, y + 6, "SOUND OUT", 2, set_c_text);
            gfx_text(TEXT_X, cy + 10, l1, 1, set_c_dim);
            gfx_text(TEXT_X, cy + 22, l2, 1, set_c_dim);
            button(RIGHT_X, cy, RIGHT_W, CTL_H, M[m], 1, set_active == CTRL_USBSND, m != 0);
            break;
        }
        gfx_text(TEXT_X, y + 6, "SOUND TO USB", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, "TO A COMPUTER,", 1, set_c_dim);
        gfx_text(TEXT_X, cy + 22, "NOT THE SPEAKER", 1, set_c_dim);
        toggle(cy, usb_audio_setting(), set_active == CTRL_USBSND);
        break;
    case R_ROTATE:
        gfx_text(TEXT_X, y + 6, "AUTO ROTATE", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, "TURN THE PICTURE", 1, set_c_dim);
        gfx_text(TEXT_X, cy + 22, "WITH THE BOARD", 1, set_c_dim);
        toggle(cy, g_settings.auto_rotate, set_active == CTRL_ROTATE);
        break;
    case R_LANG:
        gfx_text(TEXT_X, y + 6, "APP LANGUAGE", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, "ALL APPS; EACH APP", 1, set_c_dim);
        gfx_text(TEXT_X, cy + 22, "CAN CHANGE IT", 1, set_c_dim);
        button(RIGHT_X, cy, RIGHT_W, CTL_H, settings_lang_all() == 0 ? "ENGLISH" : "HEBREW", 1, set_active == CTRL_LANG,
               true);
        break;
    case R_DEV:
        gfx_text(TEXT_X, y + 6, GUARD_BUILD ? "ADMIN ACCESS" : "DEVELOPER", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, GUARD_BUILD ? "YOUR WEB" : "APPS WITH NO", 1, set_c_dim);
        gfx_text(TEXT_X, cy + 22, GUARD_BUILD ? "PASSWORD" : "SIGNATURE RUN", 1, set_c_dim);
        toggle(cy, GUARD_BUILD ? device_guard_local_admin() : tfn_dev_mode(), set_active == CTRL_DEV);
        break;
    case R_SLEEP:
        gfx_text(TEXT_X, y + 6, "SCREEN SLEEP", 2, set_c_text);
        note(y, "DIM, THEN OFF", set_c_dim);
        sleep_ctl(cy);
        break;
    case R_POWER:
        gfx_text(TEXT_X, y + 6, "POWER OFF", 2, set_c_text);
        note(y, "IDLE ON BATTERY", set_c_dim);
        pwoff_ctl(cy);
        break;
    case R_MOVE: {
        bool on = store_get("wake_mv", 1) != 0;
        gfx_text(TEXT_X, y + 6, "WAKE ON MOVE", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, on ? "A MOVE LIGHTS" : "ONLY A TOUCH", 1, set_c_dim);
        gfx_text(TEXT_X, cy + 22, on ? "THE SCREEN" : "OR A KEY", 1, set_c_dim);
        toggle(cy, on, set_active == CTRL_MOVE);
        break;
    }
    case R_SKEY: {
        bool on = store_get("scr_key", 0) != 0;
        gfx_text(TEXT_X, y + 6, "SCREEN KEY", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, on ? "PWR IN THE MENU" : "OFF: THE KEYS", 1, set_c_dim);
        gfx_text(TEXT_X, cy + 22, on ? "TURNS IT OFF" : "ACT AS BEFORE", 1, set_c_dim);
        toggle(cy, on, set_active == CTRL_SKEY);
        break;
    }
    case R_WIFI:
        row_wifi(y, cy, buf, b2, sizeof(b2));
        break;
    case R_HOTSPOT:
        row_hotspot(y, cy, buf, sizeof(buf));
        break;
    case R_WEB:
        row_web(y, cy, buf, sizeof(buf));
        break;
    case R_WEBPASS:
        row_webpass(y, cy);
        break;
    case R_FRIENDS:
        row_friends(y, cy, buf, sizeof(buf));
        break;
    case R_NOTIFY:
    case R_PUB:
    case R_POOL:
    case R_STORE:
        row_open(r, y, cy, buf, sizeof(buf));
        break;
    case R_TILT:
        gfx_text(TEXT_X, y + 6, "TILT TEST", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, "TAP: THIS POSITION", 1, set_c_dim);
        gfx_text(TEXT_X, cy + 22, "IS LEVEL", 1, set_c_dim);
        tilt_ctl(cy + CTL_H / 2);
        break;
    case R_TCAL:
        gfx_text(TEXT_X, y + 6, "TILT CALIB.", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, imu_accel_cal_on() ? "ON" : "OFF", 1, set_c_dim);
        button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_TCAL, false);
        break;
    case R_TOUCH: {
        float tm[6];
        gfx_text(TEXT_X, y + 6, "TOUCH TEST", 2, set_c_text);
        gfx_text(TEXT_X, cy + 10, input_touch_cal_get(tm) ? "CALIBRATED" : "NOT CALIBRATED", 1, set_c_dim);
        button(RIGHT_X, cy, RIGHT_W, CTL_H, "OPEN", 2, set_active == CTRL_TOUCH, false);
        break;
    }
    case R_ASCALE:
        gfx_text(TEXT_X, y + 6, "TILT SCALE", 2, set_c_text);
        snprintf(buf, sizeof(buf), "SENSOR %.2f G", (double)imu_raw_g());
        note(y, buf, set_c_dim);
        ascale_ctl(y, cy);
        break;
    case R_TEMP:
    case R_MEM:
        (r == R_TEMP ? row_temp : row_mem)(y, cy, buf, sizeof(buf));
        break;
    default:
        row_about(y, buf, sizeof(buf));
        break;
    }
}
#endif

void set_list_render(int64_t now)
{
    gfx_fill(THEME_SETUP_BG);
    for (int i = 0; i < set_nrows; i++) {
        draw_row(i, now);
    }
    set_back_strip(set_active == CTRL_BACK);
    gfx_text(4, 4, "SETUP", 1, set_c_accent);
    // the scroll bar
    int track = SCR_H - 8;
    int th = track * SCR_H / (set_nrows * ROW_H);
    float f = tfn_clampf(set_scroll / (float)MAX_SCROLL, 0, 1);
    gfx_fill_rect(SBAR_X, 4, 6, track, set_c_card2);
    gfx_fill_rect(SBAR_X, 4 + (int)(f * (track - th)), 6, th, set_c_line);
}
