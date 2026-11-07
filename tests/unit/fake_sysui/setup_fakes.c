// The test of the question of the system layer (test_sysui_ask.c) also compiles main/app_settings.c (SETUP): the
// developer mode asks a consent question. This file gives the other parts of SETUP (its pages, the drawing of the
// list) and the parts of the OS that main/app_settings.c uses, with no effect. The developer mode is a value of this
// file (tfn_dev_mode, tfn_set_dev_mode). The test file gives the parts that the system layer also uses (the clock,
// the screen, the sounds, the store).
#include "app_settings_priv.h"

static bool s_dev;  // the developer mode

bool tfn_dev_mode(void)
{
    return s_dev;
}

void tfn_set_dev_mode(bool on)
{
    s_dev = on;
}

// ---------------------------------------------------------------- the other parts of SETUP
wifi_ap_t set_list[16];
bool set_list_on;
int set_nlist, set_list_top, set_qr_press, set_wfocus;
char set_pass[65];
char set_pick[33];
float set_chip_c;
uint32_t set_free_int, set_free_ps, set_free_fs;

void set_ask_input(const input_event_t *ev)
{
    (void)ev;
}

void set_ask_render(void)
{
}

void set_list_render(int64_t now)
{
    (void)now;
}

void set_qr_input(const input_event_t *ev)
{
    (void)ev;
}

void set_qr_render(int64_t now)
{
    (void)now;
}

void set_saved_input(const input_event_t *ev)
{
    (void)ev;
}

void set_saved_render(void)
{
}

void set_wifi_input(const input_event_t *ev)
{
    (void)ev;
}

void set_wifi_render(int64_t now)
{
    (void)now;
}

// ---------------------------------------------------------------- the pages of SETUP
bool calib_input(const input_event_t *ev, int64_t now)
{
    (void)ev;
    (void)now;
    return false;
}

void calib_render(int64_t now)
{
    (void)now;
}

void calib_tilt_open(void)
{
}

void calib_touch_open(void)
{
}

void calib_update(int64_t now)
{
    (void)now;
}

bool friends_page_input(const input_event_t *ev, int64_t now)
{
    (void)ev;
    (void)now;
    return false;
}

void friends_page_leave(void)
{
}

void friends_page_open(void)
{
}

void friends_page_render(int64_t now)
{
    (void)now;
}

void friends_page_update(int64_t now)
{
    (void)now;
}

bool notify_page_input(const input_event_t *ev, int64_t now)
{
    (void)ev;
    (void)now;
    return false;
}

void notify_page_open(void)
{
}

void notify_page_render(int64_t now)
{
    (void)now;
}

void notify_page_update(int64_t now)
{
    (void)now;
}

bool pool_page_input(const input_event_t *ev, int64_t now)
{
    (void)ev;
    (void)now;
    return false;
}

void pool_page_leave(void)
{
}

void pool_page_open(void)
{
}

void pool_page_render(int64_t now)
{
    (void)now;
}

void pool_page_update(int64_t now)
{
    (void)now;
}

bool tfn_pub_page_input(const input_event_t *ev, int64_t now)
{
    (void)ev;
    (void)now;
    return false;
}

void tfn_pub_page_open(void)
{
}

void tfn_pub_page_render(int64_t now)
{
    (void)now;
}

void tfn_pub_page_update(int64_t now)
{
    (void)now;
}

// ---------------------------------------------------------------- the OS
const app_t app_menu = {.name = "MENU"};

void app_open(const app_t *a)
{
    (void)a;
}

uint8_t app_board_caps(void)
{
    return APP_NEEDS_IMU;
}

void audio_set_mute(bool mute)
{
    (void)mute;
}

void audio_set_ui_sounds(bool on)
{
    (void)on;
}

bool audio_ui_sounds(void)
{
    return true;
}

float board_chip_temp(void)
{
    return 40.0f;
}

float fsin(float x)
{
    return sinf(x);
}

float imu_accel_scale(void)
{
    return 1.0f;
}

bool imu_accel_scale_auto(void)
{
    return true;
}

void imu_set_accel_scale(float k)
{
    (void)k;
}

void imu_set_level(void)
{
}

void input_tap_cancel(void)
{
}

int kbd_input(const input_event_t *ev)
{
    (void)ev;
    return KBD_NONE;
}

void kbd_open(const char *title, char *buf, int max, bool secret)
{
    (void)title;
    (void)buf;
    (void)max;
    (void)secret;
}

void kbd_render(int64_t now)
{
    (void)now;
}

void kbd_set_ok_question(const char *q)
{
    (void)q;
}

int power_off_choice(void)
{
    return 0;
}

void power_set_off_choice(int c)
{
    (void)c;
}

int settings_lang_all(void)
{
    return 0;
}

void settings_lang_all_set(int lang)
{
    (void)lang;
}

void settings_save(void)
{
}

void sfx_play(sfx_t sfx)
{
    (void)sfx;
}

uint64_t storage_free(void)
{
    return 4096 * 1024;
}

void store_set(const char *key, uint32_t value)
{
    (void)key;
    (void)value;
}

int tfn_sstore_mode(void)
{
    return 0;
}

bool tfn_sstore_set(int mode, const char *url, char *msg, int len)
{
    (void)mode;
    (void)url;
    (void)msg;
    (void)len;
    return true;
}

const char *tfn_sstore_url(void)
{
    return "";
}

int usb_audio_mode(void)
{
    return 0;
}

void usb_audio_set_mode(int mode)
{
    (void)mode;
}

bool usb_phones_supported(void)
{
    return false;
}

bool web_auth_on(void)
{
    return false;
}

bool web_auth_set(const char *pw, char *msg, int len)
{
    (void)pw;
    (void)msg;
    (void)len;
    return false;
}

void wifi_connect_try(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
}

hotspot_mode_t wifi_hotspot_mode(void)
{
    return (hotspot_mode_t)0;
}

bool wifi_hotspot_on(void)
{
    return false;
}

void wifi_hotspot_set_mode(hotspot_mode_t m)
{
    (void)m;
}

void wifi_hotspot_start(void)
{
}

void wifi_hotspot_stop(void)
{
}

bool wifi_saved(void)
{
    return true;
}

void wifi_scan(void)
{
}

int wifi_scan_results(wifi_ap_t *out, int max)
{
    (void)out;
    (void)max;
    return 0;
}

bool wifi_scanning(void)
{
    return false;
}

bool wifi_start(void)
{
    return true;
}

wifi_state_t wifi_state(void)
{
    return (wifi_state_t)0;
}

const char *wifi_web_ip(void)
{
    return "192.0.2.10";
}
