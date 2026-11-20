// TFN OS SDK, the PC side of the raw ESP-IDF names (sdk/docs/raw.md): a stub for each extra ABI name of
// main/tfn_abi_extra.json. mk_app.py links them into the icon tool of an app (the PC draws only the menu icon, so
// these never run there). Each stub gives the error of "the radio is off". sdk/tools/abi_gen.py warns about an extra
// name that has no stub here. A test that needs data gives its own versions.
// The names that tests/host/stubs.c also has (it is in the icon library of sdk/tools/host/build.sh) are weak here: the
// full PC versions of stubs.c win, and a link with no stubs.c still finds these.
#include <string.h>
#include "esp_wifi.h"
bool device_guard_setup_allowed(void) { return true; }
int device_guard_accounts_read(char *out,size_t cap) { (void)out;(void)cap;return -2; }
int device_guard_accounts_write(const char *text,size_t len) { (void)text;(void)len;return -2; }

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block) { (void)config; (void)block; return ESP_ERR_WIFI_NOT_STARTED; }
esp_err_t esp_wifi_scan_stop(void) { return ESP_ERR_WIFI_NOT_STARTED; }
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number) { *number = 0; return ESP_ERR_WIFI_NOT_STARTED; }
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records) { (void)ap_records; *number = 0; return ESP_ERR_WIFI_NOT_STARTED; }
esp_err_t esp_wifi_clear_ap_list(void) { return ESP_ERR_WIFI_NOT_STARTED; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info) { memset(ap_info, 0, sizeof *ap_info); return ESP_ERR_WIFI_NOT_STARTED; }
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second) { *primary = 0; *second = WIFI_SECOND_CHAN_NONE; return ESP_ERR_WIFI_NOT_STARTED; }

// Firmware-owned SDK capture sessions: the host only needs to link the icon.
#include "wifi_capture.h"
int32_t wifi_capture_begin(const wifi_capture_config_t *config, wifi_capture_rx_cb_t callback, void *context)
{ (void)config; (void)callback; (void)context; return ESP_ERR_WIFI_NOT_STARTED; }
int32_t wifi_capture_end(void) { return ESP_OK; }
int32_t wifi_capture_deauth(const uint8_t client[6]) { (void)client; return ESP_ERR_WIFI_NOT_STARTED; }

// The notifications and the open at an item (main/notify.h, main/app.h) and SAVE TO (main/export.h).
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
__attribute__((weak)) uint32_t tfn_notify(const char *title, const char *text, const char *link) { (void)title; (void)text; (void)link; return 0; }
__attribute__((weak)) const char *app_launch_arg(void) { return ""; }
__attribute__((weak)) void export_path(const char *sub, const char *name, bool rd, char *out, int len) { (void)rd; snprintf(out, len, "/data/%s/%s", sub, name); }
__attribute__((weak)) bool export_place_ask(void) { return false; }
__attribute__((weak)) void export_place_set(bool card) { (void)card; }
__attribute__((weak)) int tfn_secret_get(const char *key, char *out, int max) { (void)key; (void)out; (void)max; return -1; }
__attribute__((weak)) bool tfn_secret_set(const char *key, const char *value) { (void)key; (void)value; return false; }
__attribute__((weak)) void audio_alert(const void *notes, int n, int vol) { (void)notes; (void)n; (void)vol; }
void app_standby_hold(int on) { (void)on; }

// USB AUDIO (main/usb_audio.h): the MIC app. On the PC: no USB, the device stays off.
void usb_audio_open(int src) { (void)src; }
void usb_audio_close(void) {}
void usb_audio_set_src(int src) { (void)src; }
int usb_audio_src(void) { return 0; }
int usb_audio_state(void) { return 0; }
bool usb_audio_streaming(void) { return false; }
float usb_audio_level(void) { return 0; }
uint32_t usb_audio_sent(void) { return 0; }
uint32_t usb_audio_dropped(void) { return 0; }
bool usb_audio_setting(void) { return false; }

// BT KEYS (main/bt_pad.h): on the PC no Bluetooth, the link stays off.
__attribute__((weak)) bool bt_pad_start(void) { return false; }
__attribute__((weak)) int bt_pad_state(void) { return 0; }
__attribute__((weak)) const char *bt_pad_name(void) { return "TFN PAD"; }
__attribute__((weak)) void bt_pad_set_name(const char *name) { (void)name; }
__attribute__((weak)) uint32_t bt_pad_sent(void) { return 0; }
__attribute__((weak)) void bt_pad_forget(void) {}
__attribute__((weak)) void bt_kbd_report(uint8_t mods, const uint8_t keys[6]) { (void)mods; (void)keys; }
__attribute__((weak)) void bt_kbd_tap(uint8_t mods, uint8_t key) { (void)mods; (void)key; }
__attribute__((weak)) int bt_kbd_type(const char *text) { (void)text; return 0; }
__attribute__((weak)) uint8_t bt_kbd_leds(void) { return 0; }
__attribute__((weak)) void bt_media_tap(uint16_t usage) { (void)usage; }
__attribute__((weak)) void bt_mouse_move(int dx, int dy, int wheel) { (void)dx; (void)dy; (void)wheel; }
__attribute__((weak)) void bt_mouse_buttons(uint8_t buttons) { (void)buttons; }
__attribute__((weak)) bool bt_pad_boot(void) { return false; }

// the touch log of TOUCH LAB (tfn/board.h): no reports on the PC
#include "tfn/board.h"
__attribute__((weak)) void board_touch_log(bool on) { (void)on; }
__attribute__((weak)) int board_touch_reports(touch_rep_t *out, int max) { (void)out; (void)max; return 0; }
__attribute__((weak)) int board_touch_read_len_get(void) { return 0; }

// the voice module (tfn/speech.h): no voice on the PC
#include "tfn/speech.h"
__attribute__((weak)) bool speech_offer(const speech_ops_t *ops) { (void)ops; return false; }
__attribute__((weak)) bool speech_ready(void) { return false; }
__attribute__((weak)) void speech_say(const char *text) { (void)text; }
__attribute__((weak)) void speech_stop(void) {}
__attribute__((weak)) bool speech_make(const char *text, const char *path) { (void)text; (void)path; return false; }
__attribute__((weak)) bool speech_file_ok(const char *path, const char *text) { (void)path; (void)text; return false; }
__attribute__((weak)) int speech_play_file(const char *path) { (void)path; return 0; }
__attribute__((weak)) bool speech_read_open(const speech_read_cfg_t *c) { (void)c; return false; }
__attribute__((weak)) void speech_read_play(bool on) { (void)on; }
__attribute__((weak)) void speech_read_close(void) {}
__attribute__((weak)) void speech_read_get(speech_read_t *st) { memset(st, 0, sizeof *st); }
// the ears (tfn/listen.h): no ears on the PC (an app gives a typed fallback)
#include "tfn/listen.h"
__attribute__((weak)) bool listen_offer(const listen_ops_t *ops) { (void)ops; return false; }
__attribute__((weak)) bool listen_ready(void) { return false; }
__attribute__((weak)) bool listen_start(const char *brain) { (void)brain; return false; }
__attribute__((weak)) void listen_stop(void) {}
__attribute__((weak)) void listen_cancel(void) {}
__attribute__((weak)) int listen_state(void) { return LS_OFF; }
__attribute__((weak)) float listen_level(void) { return 0.0f; }
__attribute__((weak)) void listen_text(char *out, int cap) { if (cap > 0) { out[0] = 0; } }
__attribute__((weak)) void listen_answer(char *out, int cap) { if (cap > 0) { out[0] = 0; } }
__attribute__((weak)) bool listen_ask(const char *brain, const char *text) { (void)brain; (void)text; return false; }
__attribute__((weak)) void listen_handler(listen_handler_fn fn) { (void)fn; }
// the stop of the sounds that use a memory block (tfn/audio.h): the PC has no mixer
__attribute__((weak)) void audio_stop_mem(const void *p, size_t n) { (void)p; (void)n; }
// the raw frames of the link (main/tfn/link.h): no radio
#include "tfn/link.h"
__attribute__((weak)) bool link_raw_open(uint8_t m0, uint8_t m1) { (void)m0; (void)m1; return false; }
__attribute__((weak)) void link_raw_close(void) {}
__attribute__((weak)) bool link_raw_send(const uint8_t *mac, const void *d, int n) { (void)mac; (void)d; (void)n; return false; }
__attribute__((weak)) bool link_raw_recv(link_raw_t *m) { (void)m; return false; }
