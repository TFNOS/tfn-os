#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "input.h"
// The system layer over all apps: a message from the manager (big text, English and Hebrew), the locate
// sound (loud beeps and a flashing screen), the start picture (splash), a frequency sweep of the speaker.
// main.c calls sysui_update() and sysui_draw() each frame and gives each touch to sysui_input() first.

// A question over any app (a WALKIE call): a title (UTF-8, for example a name), one line, two buttons; secs = 0: until
// an answer. done(1) = the first button, done(0) = the second, done(-1) = closed (sysui_ask_close, or the time).
// The app task only.
void sysui_ask(const char *title, const char *line, const char *yes, const char *no, int secs, void (*done)(int));
void sysui_ask_close(void);
// The layer is on the screen (a message, the locate sound or a question).
bool sysui_active(void);

// ---- the settings panel ----
// A pull down from the middle of the top edge (the zone and its guard: input.h) brings the panel down over any app:
// VOLUME and BRIGHTNESS (they change at once; the save comes at the close), RESUME and MENU (SETUP in the menu). On the
// Watcher a 600 ms hold of the wheel button also opens it (main.c). The grip (a small handle at the top middle) shows for
// 4 s when an app opens and after each close. While the panel shows, main.c freezes the app (no update, no render, its
// clock stops) or lets it run with no touches (app_t.run, a radio link). The app task only.
void sysui_panel_open(void);  // an app button opens the panel (it comes down in the next frames)
bool sysui_panel_on(void);    // the panel is on the screen (it moves, or it is open)
// (main.c) each frame, before the touches: the zone (allow = a pull may start now), the finger of a pull, the wheel, the
// motion. PANEL_START = the panel began to show in this frame; PANEL_END = it went away in this frame.
enum {
    PANEL_NONE,
    PANEL_START,
    PANEL_END
};

// ---- the items of an app in the panel (the options of a game are ONLY in the
// panel). An app gives a short list of its own items; the panel shows them as chips under VOLUME and BRIGHTNESS (a
// page; more items than the chips of the layout: a MORE chip turns the pages). The app task only.
//   PI_PICK  a choice of 2-4 values (a tap = the next value): ZOOM, LANGUAGE, TILT, LEVEL
//   PI_ACT   a button that acts at once (a safe action), then the panel closes: THE TITLE of a game that waits
//   PI_ACT2  a button with a loss (RESTART, QUIT, NEW GAME, RESIGN): a still hold of 1 s (a fill shows it) acts; a short
//            tap only shows HOLD (a thumb that misses a neighbour, also twice, loses nothing). The Watcher wheel: two
//            pushes (the first shows PUSH AGAIN). Then the panel closes.
// Each chip acts when the finger comes off the glass on it. Text: English and Hebrew (the panel speaks the language of
// the app: sysui.c app_hebrew()).
enum {
    PI_PICK,
    PI_ACT,
    PI_ACT2
};
typedef struct {
    uint8_t kind;         // PI_*
    uint8_t id;           // the number of the item for the app (the callbacks get it)
    uint8_t n;            // PI_PICK: the number of values (2..4)
    uint8_t def;          // PI_PICK with a key: the value when the store has no key
    const char *name[2];  // the name: English, Hebrew (UTF-8)
    const char *const
            *vals;    // PI_PICK: the names of the values, two for each: vals[2 * v] English, vals[2 * v + 1] Hebrew
    const char *key;  // PI_PICK: the store key of the value (the panel reads it, and writes it before set()); NULL:
                      // the value comes from get()
} pitem_t;
typedef struct {
    const pitem_t *items;
    uint8_t n;
    bool (*shown)(int id);  // the items that show now (NULL: all), for example RESTART only in a level
    int (*get)(int id);     // the value of a PI_PICK with no key
    void (*set)(int id,
                int v);  // a PI_PICK has the value v (the store key is written already); a PI_ACT / PI_ACT2 acts (v 0)
    void (*opened)(void);  // the panel begins to show (NULL: nothing): for example a sound of the app stops
    void (*resume)(void);  // the panel went away (back to the app): for example a tilt game takes the zero again
} pitems_t;
// The app (in its enter(), or at any time): its items (NULL: none). An app change clears them (before the enter of the
// next app). The set must stay (a static const).
void sysui_panel_items(const pitems_t *set);
