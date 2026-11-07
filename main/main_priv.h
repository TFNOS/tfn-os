#pragma once
// The private header of the core of TFN OS: main.c (the start, the main loop, the app list, the switch of apps),
// main_saver.c (the screen saver), main_keys.c (the keys of the board) and main_selftest.c (the self-test).
// Only these files include it.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "tfn_util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "app.h"
#include "sysui.h"
#include "notify.h"
#include "evlog.h"
#include "watch.h"
#include "tfn_app.h"
#include "walkie_call.h"
#include "web_chat.h"
#include "usb_audio.h"
#include "audio.h"
#include "board.h"
#include "gfx.h"
#include "imu.h"
#include "input.h"
#include "tab5_kbd.h"
#include "music.h"
#include "settings.h"
#include "web_auth.h"
#include "device_guard.h"
#include "friends.h"
#include "web_walkie.h"
#include "webcam.h"
#include "ota.h"
#include "layout.h"
#include "vpn.h"
#include "ha.h"
#include "stocks.h"
#include "storage.h"
#include "wifi.h"
#include "link.h"
#include "net2.h"
#include "duel.h"
#include "console.h"
#include "bt_pad.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "export.h"
#include "tunes.h"
#include "power.h"
#include "alarm.h"
#include "oref.h"
#include "rf_native.h"

#define PWR_HOLD_MS    2000
#define PWR_SHORT_MS   30     // shorter pushes are contact bounce
#define ROTATE_HOLD_MS 400    // the menu turns after the board is on its other side this long
#define DIM_TO_OFF_MS  15000  // time from dim to off
#define AWAKE_MAX_MS                                                                                                   \
    120000                    // app_t.awake keeps the screen on this long after the last touch, key or movement
                              // (10 min kept an untouched AMOLED on for a long time and used half its battery)
#define DIM_LOW_LEVEL 40      // at this brightness or less the screen goes off at once
#define MOVE_G        0.12f   // a change of the gravity direction this big is activity
#define MOVE_KEEP_MS  300000  // WAKE ON MOVE OFF: a move keeps a lit screen on this long after the last touch or key

enum {
    SAVER_ON,
    SAVER_DIM,
    SAVER_OFF
};

// (main_saver.c) the screen saver, the wake, the motion
extern int mai_saver;
extern int64_t mai_active_at;
extern int64_t mai_touch_at;
extern int64_t mai_hand_at;
extern bool mai_key_off;
extern bool mai_cam_on;
extern bool mai_bg_on;
extern float mai_ref_g[3];
extern volatile bool mai_wake_req;
extern int mai_pocket_ms;
bool mai_app_background(void);
void mai_deep_saver(bool on);
void mai_wake(int64_t t, const char *why);
void mai_check_motion(int64_t t);
void mai_saver_update(int64_t t);

// (main.c) the app now
extern const app_t *mai_app;
bool mai_app_is(const char *name);
bool mai_app_runs(void);

// (main_keys.c) the keys, the pull rule of the settings panel
extern int mai_key_presses;
void mai_pwr_key_poll(int64_t t);
void mai_boot_key_poll(int64_t t);
bool mai_pull_allow(void);
bool tennis_two_players(void);

#if CONFIG_TFN_SELFTEST
// (main_selftest.c)
void mai_selftest(int64_t t);
#endif
