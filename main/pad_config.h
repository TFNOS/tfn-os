#pragma once

#include <stdbool.h>
#include <stdint.h>

// Settings of the GAMEPAD app: 6 profiles, the active profile and the Bluetooth name.
// They stay in NVS (keys "pad_*"). The web page /pad edits them (wifi.c calls the
// functions below); the app reads them (a change is visible through the version).

#define PAD_PROFILES    6
#define PAD_NAME_MAX    12    // characters of a profile name
#define PAD_BT_NAME_MAX 29    // characters of the Bluetooth name
#define PAD_JSON_MAX    2048  // a buffer of this size holds pad_config_json()

// the values stay the same in NVS: a new mode goes at the end
enum {
    PAD_MODE_RETRO,
    PAD_MODE_BUTTONS,
    PAD_MODE_MOTION,
    PAD_MODE_MOVE,
    PAD_MODES
};
enum {
    PAD_MOVE_HOLD,
    PAD_MOVE_LATCH
};
enum {
    PAD_TOUCH_HALVES,
    PAD_TOUCH_PAD,
    PAD_TOUCH_ONE,
    PAD_TOUCHES
};

typedef struct {
    char name[PAD_NAME_MAX + 1];
    uint8_t mode;            // PAD_MODE_*
    uint8_t dirs;            // 4 or 8 directions (RETRO tilt, BUTTONS d-pad)
    uint8_t dead;            // dead zone of the tilt, degrees (0..30)
    uint8_t range;           // tilt for a full move, degrees (5..60, more than dead)
    uint8_t autofire;        // RETRO: shots per second while FIRE is held, 0 = off (0..20)
    uint8_t fire;            // RETRO: the button of FIRE (1..16)
    uint8_t usey;            // MOTION: 1 = forward/back tilt moves the stick Y too
    uint8_t invx, invy;      // 1 = invert left/right, forward/back
    uint8_t touch;           // MOTION: PAD_TOUCH_*
    uint8_t bl, br;          // MOTION: buttons of the left and right half (or the tap) 0..16
    uint8_t gu, gd, gl, gr;  // jerk up, down, left, right -> button 0..16 (0 = none)
    uint8_t gk, gs, gn;      // knock on the back, hard shake, snap -> button 0..16
    uint8_t hg;              // jerk strength necessary, g x 10 (13..60)
    uint8_t click;           // 1 = a click sound for the screen buttons
    uint8_t mstyle;          // MOVE: PAD_MOVE_HOLD or PAD_MOVE_LATCH
    uint8_t msens;           // MOVE: 1 (big moves only) .. 9 (small moves)
} pad_profile_t;

// NVS -> memory (call it at the app init). Missing or bad values get the defaults.
void pad_config_load(void);
// All 6 profiles, the active one and the Bluetooth name as JSON. Returns the length.
// Strings are escaped (also < > &), so the text is safe in a page.
int pad_config_json(char *buf, int len);
// An application/x-www-form-urlencoded body from the web page. Fields: name (the
// Bluetooth name), active (0..5), and p<i>_<key> for the profile fields (the keys of
// the JSON). Missing fields keep their value. A bad value rejects the whole body
// (returns false, nothing changes). Else it saves to NVS and applies at once.
bool pad_config_set_form(const char *body, int len);
// The full HTML page for "/pad".
const char *pad_config_page(void);

// For the app
const pad_profile_t *pad_profile(int i);
int pad_active(void);
void pad_set_active(int i);          // saves to NVS (the app opens with it the next time)
void pad_set_move_style(int style);  // the MOVE style of the active profile; saves to NVS
const char *pad_bt_name(void);
// A new Bluetooth name from bt_pad_set_name() (an app, for example the panel of BT KEYS): it is saved, so it stays after
// a restart, and the web page /pad shows it. The same name, or a name that is not printable ASCII: no change.
void pad_config_keep_bt_name(const char *name);
uint32_t pad_config_version(void);  // +1 for each change
