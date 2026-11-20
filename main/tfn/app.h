#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "input.h"
#include "esp_timer.h"
// strlcpy of its own: an app file can call only the names of the app ABI (strlcpy is not one), and the C
// library of the host tests on Windows has none
#include <string.h>
static inline size_t tfn_host_strlcpy(char *d, const char *s, size_t n)
{
    size_t k = strlen(s);
    if (n) {
        size_t c = k < n - 1 ? k : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return k;
}
#define strlcpy tfn_host_strlcpy

// The time for the animated menu icons, in seconds (it starts again after 1000 s).
static inline float icon_time(void)
{
    return (float)((esp_timer_get_time() / 1000) % 1000000) / 1000.0f;
}

// One screen of the arcade: the menu, a game, or the settings.
typedef struct app {
    const char *name;
    bool landscape;                          // 640x172 if true, else 172x640
    void (*init)(void);                      // once at start-up, optional
    void (*enter)(int64_t now);              // each time the app opens
    void (*leave)(void);                     // optional
    void (*input)(const input_event_t *ev);  // logical coordinates
    void (*update)(int64_t now);
    void (*render)(int64_t now);
    void (*draw_icon)(int x, int y, int size);  // menu icon, landscape logical coordinates
    bool (*awake)(void);                        // optional: true = keep the screen on (music plays...), at most
                                                // AWAKE_MAX_MS (main.c) after the last touch, key or movement
    bool (*background)(void);                   // optional: true = run update() while the screen is off (the
                                                // speaker and the radio stay on; no render)
    bool (*keep_on)(void);                      // optional: true = the screen saver never turns the screen off
                                                // (SCENES with its SCREEN SAVER set to OFF)
    bool native;                                // it has a layout for a big native panel (gfx_native(), the AMOLED)
    uint8_t round;                              // it has a layout for a round panel (gfx_round(), the Watcher):
                                                // APP_ROUND; APP_ROUND_HI = the frame of the panel (gfx_w x gfx_h:
                                                // 466 x 466 on the AMOLED-1.75C, no scale; 412 on the Watcher)
    bool round_fit;                             // a round panel, no round layout: show the whole CoreS3 frame at
                                                // 1:1 inside the circle (not the zoom that cuts its corners)
    bool wheel;                                 // it reads the wheel itself (input_wheel*); else the wheel
                                                // is a left/right tilt and its button a tap in the middle
    bool key;                                   // it reads the BOOT key of the 3.49 itself (app_key_*: a jump in
                                                // a platform game); the key opens no CHAT in it
    bool run;                                   // the settings panel (sysui.h): the app runs on under the panel
                                                // (a timer, a music tool, a call); false = it stops there, and its
                                                // clock stops too (also true while a radio link is on: main.c)
    uint8_t needs;                              // APP_NEEDS_*: the menu hides the app if the board has not got it
} app_t;
// app_t.round (one byte, as the bool before it: the old app files keep 0 / 1)
#define APP_ROUND    1
#define APP_ROUND_HI 2

// What an app needs from the board.
#define APP_NEEDS_BAR 0x01  // the 172 x 640 bar screen (its layout is for that shape only)
#define APP_NEEDS_IMU 0x02  // the motion sensor (no use without it)

// The needs that this board can give (APP_NEEDS_* bits).
uint8_t app_board_caps(void);

// The menu: an app goes back to it with app_open(&app_menu).
extern const app_t app_menu;

// Open an app (in main.c). The switch happens before the next frame.
void app_open(const app_t *app);
// Open an app with an argument (app_launch_arg() of that app gives it), and an app by its menu name (an installed app
// file first; NULL: none)
void app_open_arg(const app_t *app, const char *arg);
const app_t *app_find(const char *name);
const char *app_launch_arg(void);
// THE POCKET MODE of an app (ON AIR: the user puts the board in a pocket and listens): ms > 0 = the screen
// goes off after ms with no touch or key, whatever SCREEN SLEEP says, and a movement or the music does not wake it (a
// pocket moves all the time); a touch or a key wakes it. 0 = the normal screen saver. An app change sets 0.
void app_screen_pocket(int ms);
// The web page (the manager): the screen on now (as a touch), the screen state ("on", "dim", "off"), and the
// names of all apps (returns the count).
void app_screen_wake(void);
const char *app_screen_state(void);
// The open app waits for a time (an alarm at a set time): on = STANDBY does not power the board off on the battery (the
// setting of SETUP), the light sleep stays. app_t.background still decides when update() runs (main.c asks it each
// 0.1 s with the screen off). A switch of the app turns it off.
void app_standby_hold(bool on);
// The BOOT key of the 3.49 for an app with app_t.key: the presses since the last call, and the key held now (0 and
// false on a board without the key, and in an app without app_t.key)
int app_key_presses(void);
bool app_key_down(void);
