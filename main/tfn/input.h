#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IN_DOWN,  // finger on the panel
    IN_MOVE,  // finger moved (sent each poll while it moves)
    IN_UP,    // finger off the panel
    IN_TAP,   // short touch with little travel (sent after IN_UP)
    IN_LONG,  // finger held still for LONG_PRESS_MS (sent while the finger is down)
    IN_KEY,   // a key of a keyboard (the Tab5 keyboard): x = the key (INK_*), y = the modifiers (INM_*);
              // an app that has no use for keys does nothing with it (the apps with app_t.wheel also get the arrows
              // as wheel steps and Enter as a wheel push)
} input_kind_t;

// THE KEYS OF IN_KEY (x): a letter, a digit or a sign is its ASCII code (Shift already in it); then these
enum {
    INK_BKSP = 8,
    INK_TAB = 9,
    INK_ENTER = 13,
    INK_ESC = 27,
    INK_DEL = 127,
    INK_UP = 0x101,
    INK_DOWN,
    INK_LEFT,
    INK_RIGHT,
    INK_HOME,
    INK_END,
    INK_PGUP,
    INK_PGDN,
    INK_INS,
    INK_F1 = 0x111,  // INK_F1 .. INK_F1 + 11 = F12
};
// the modifiers of IN_KEY (y)
enum {
    INM_SHIFT = 1,
    INM_CTRL = 2,
    INM_ALT = 4,
    INM_GUI = 8
};

typedef struct {
    input_kind_t kind;
    int16_t x, y;  // physical coordinates in the queue; main.c changes them to logical
    int64_t t_ms;
} input_event_t;

#define LONG_PRESS_MS 350

// The drag of an app wins (the tap rule is 1.8 mm): an app calls it in its input
// function when it starts its own drag (a scroll, a move) for this touch. Then the IN_TAP and the IN_LONG of this
// touch do not come out of input_get(). The next IN_DOWN clears it.
void input_tap_cancel(void);

// ---- two end buttons (two-player games) ----
// The touch task also keeps the state of two big buttons: the first END_PX physical
// rows at each short end of the screen. It uses its own 10 ms clock, so press and
// release times are exact to about 10 ms, at any frame rate.
#define INPUT_END_PX 120

typedef struct {
    bool down;
    int64_t down_ms;   // time of the last press
    int64_t up_ms;     // time of the last release
    uint32_t presses;  // +1 for each press
} input_end_t;

// [0] = physical top end (y < INPUT_END_PX), [1] = physical bottom end.
void input_ends(input_end_t out[2]);
// True after the panel reported two fingers at the same time. If it is false, the
// panel may see only one finger: a thumb that rests on the other button can hide a press.
bool input_multitouch(void);

// The touch points now (physical coordinates, up to max, the panel gives 2 at most).
// For screens with many buttons at once (the gamepad). Returns the count.
int input_points(int16_t *x, int16_t *y, int max);

// The most touch points that input_points() can give (the panel may give fewer).
#define INPUT_MAX_POINTS 5

// ---- the wheel (BOARD_WHEEL: the SenseCAP Watcher) ----
// An app with app_t.wheel reads these. For the other apps main.c makes the button a tap in the
// middle of the screen, and imu.c makes the wheel a left/right tilt. A hold of the button = back.
bool input_has_wheel(void);
int input_wheel(void);          // steps since the last call (+ = clockwise); 0 with no wheel
int input_wheel_presses(void);  // short pushes of the button since the last call
bool input_wheel_down(void);    // the button is down now (after the power-on push)
