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
#include "esp_log.h"

// TFN OS: no installed apps (the menu has the apps of this build; an app file of the store is Xtensa code)
void tfn_init(void) { ESP_LOGI("tfn", "the simulator: no installed apps (each app of main/apps.json is built in)"); }
bool tfn_safe_mode(void) { return false; }
void tfn_tick(void) {}
void tfn_icon_cache_free(void) {}   // (no icon files of installed apps here)
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
