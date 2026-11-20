// The simulator: the parts of the firmware that have no PC version and that tests/host/stubs.c does not give (the files of
// main/apps.json host_skip that main.c and sysui.c call): the installed apps of TFN OS (their files are Xtensa code),
// the firmware update, the flash layout, the VPN, the power modes, the serial console, the camera and the web page.
// Each one does what the board does when that part is idle.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "tfn_app.h"
#include "tfn_web.h"
#include "wifi.h"
#include "ota.h"
#include "layout.h"
#include "vpn.h"
#include "power.h"
#include "watch.h"
#include "console.h"
#include "webcam.h"
#include "web_walkie.h"
#include "evlog.h"
#include "tab5_kbd.h"
#include "tfn_speech.h"
#include "esp_log.h"

// TFN OS: no installed apps (the menu has the apps of this build; an app file of the store is Xtensa code)
void tfn_init(void) { ESP_LOGI("tfn", "the simulator: no installed apps (each app of main/apps.json is built in)"); }
bool tfn_safe_mode(void) { return false; }
void tfn_tick(void) {}
void tfn_icon_cache_free(void) {}   // (no icon files of installed apps here)
void tfn_icon_ask(bool on) { (void)on; }   // (the icon reader: no icon files here)
bool tfn_icon_missed(void) { return false; }
void tfn_icon_want(const app_t *a) { (void)a; }
// the ears (tfn_listen.h): no ears here (no TALK watcher)
#include "tfn_listen.h"
bool listen_offer(const listen_ops_t *ops) { (void)ops; return false; }
bool listen_ready(void) { return false; }
bool listen_start(const char *brain) { (void)brain; return false; }
void listen_stop(void) {}
void listen_cancel(void) {}
int listen_state(void) { return LS_OFF; }
float listen_level(void) { return 0.0f; }
void listen_text(char *out, int cap) { if (cap > 0) { out[0] = 0; } }
void listen_answer(char *out, int cap) { if (cap > 0) { out[0] = 0; } }
bool listen_ask(const char *brain, const char *text) { (void)brain; (void)text; return false; }
void listen_handler(listen_handler_fn fn) { (void)fn; }
bool tfa_listen_task_now(int *w) { (void)w; return false; }
void tfa_listen_slot_gone(int w) { (void)w; }
void tfa_listen_quiet(void) {}
bool listen_key_down(void) { return false; }
void listen_key_held(int ms) { (void)ms; }
void listen_key_up(void) {}
void listen_tick(int64_t now) { (void)now; }
void listen_overlay(void) {}
void listen_app_changed(void) {}
bool listen_shows(void) { return false; }
bool listen_awake(void) { return false; }
bool listen_calm(void) { return false; }
void listen_console(const char *a, const char *b) { (void)a; (void)b; }
// the voice (tfn_speech.h): the simulator has no voice module (an Xtensa watch.elf)
bool speech_ready(void) { return false; }
void speech_say(const char *text) { (void)text; }
void speech_stop(void) {}
bool speech_make(const char *text, const char *path) { (void)text; (void)path; return false; }
bool speech_file_ok(const char *path, const char *text) { (void)path; (void)text; return false; }
int speech_play_file(const char *path) { (void)path; return 0; }
bool speech_read_open(const speech_read_cfg_t *c) { (void)c; return false; }
void speech_read_play(bool on) { (void)on; }
void speech_read_close(void) {}
void speech_read_get(speech_read_t *st) { memset(st, 0, sizeof *st); }
#if BOARD_TTS  // (a board with no voice has the empty inline versions of tfn_speech.h)
bool speech_calm(void) { return false; }
bool speech_notices_on(void) { return false; }
void speech_set_notices(bool on) { (void)on; }
void speech_notice(const char *app, const char *title, const char *text) { (void)app; (void)title; (void)text; }
const char *speech_state(void) { return "NO VOICE IN THE SIMULATOR"; }
#endif
const app_t *tfn_resolve(const app_t *a, char *msg, int len) { (void)msg; (void)len; return a; }
void tfn_left(const app_t *a) { (void)a; }
bool tfn_same_app(const app_t *item, const app_t *running) { return item == running; }   // (no app files here)
bool tfn_inst_busy(void) { return false; }        // (no app install here)
bool tfn_web_upload_busy(void) { return false; }  // (no web server here)

// the firmware update over the network and the flash layout: nothing to do
void ota_tick(int64_t now_ms) { (void)now_ms; }
void ota_overlay(void) {}
bool ota_busy(void) { return false; }
void layout_boot(void) {}
void layout_tick(int64_t now) { (void)now; }

// the VPN: off
void vpn_init(void) {}
void vpn_start_task(void) {}

// the power modes: always on (the screen saver of main.c still works)
void power_init(void) {}
void power_mode(int mode, bool background) { (void)mode; (void)background; }
void power_wake(const char *why) { (void)why; }
bool power_tick(int64_t now_ms) { (void)now_ms; return false; }
void power_hold(bool on) { (void)on; }
void power_idle1_hold(bool hold) { (void)hold; }

// the background checks of the app files (watch.h): no installed apps
void watch_init(void) {}

// the serial console (the console of the PC shows the log)
void console_init(void) {}

// the camera page and the web page: off
bool webcam_active(void) { return false; }
void webcam_overlay(void) {}
void web_walkie_overlay(void) {}

// the power save of the WiFi while the screen is off or a transfer runs: no WiFi
void wifi_saver(bool on) { (void)on; }
void wifi_xfer(bool on) { (void)on; }

// PLAY BY RADIO and the radio invite (main/radio_play.c): no radio here
void radio_play_update(int64_t now_ms, bool menu) { (void)now_ms; (void)menu; }
bool radio_play_on(void) { return false; }

#if BOARD_EVLOG
// the event log of the board (GET /api/evlog): no web server here, so the lines go nowhere
void evlog_init(void) {}
void evlog_add(const char *fmt, ...) { (void)fmt; }
#endif

#if BOARD_KBD_I2C
// the keyboard of the Tab5 (I2C): not connected
void tab5_kbd_init(void) {}
#endif
