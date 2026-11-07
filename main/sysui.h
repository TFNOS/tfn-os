#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/sysui.h.
#include "tfn/sysui.h"

#include <stdbool.h>
#include <stdint.h>
#include "input.h"
// The system layer over all apps: a message from the manager (big text, English and Hebrew), the locate
// sound (loud beeps and a flashing screen), the start picture (splash), a frequency sweep of the speaker.
// main.c calls sysui_update() and sysui_draw() each frame and gives each touch to sysui_input() first.

// A message on the screen for secs seconds (0 = until a tap). UTF-8; safe from any task (the web server).
void sysui_message(const char *utf8, int secs);
// The locate sound for secs seconds (0 = stop), at hz (0 = the saved frequency, store "loc_hz"). Safe from any task.
void sysui_locate(int secs, int hz);
// The locate tone now: the saved one, else the default of the board kind.
int sysui_locate_hz(void);
// After sysui_ask (not for a call): a tap out of its two buttons or the PWR key closes it (done(-1)); a close mark shows.
void sysui_ask_closable(void);
// After sysui_ask: the first button is a consent (a trust grant, a new permission, the developer mode on). It acts only
// for the rule of a consent (input.h): a tap by a finger on the panel, of a touch that came down INPUT_ARM_MS or more
// after the question showed, and whose IN_DOWN the question got. A tap of the console or of the web on it gives no
// answer: the question stays as it is (its line with the data too), a sound comes, and a note under the line says
// "A REMOTE TAP CANNOT <first button>" (two lines in a narrow frame). A finger that came down too soon gives no answer
// (its next tap answers). The second button (the safe answer) acts for each tap. In the gap between the two buttons
// the nearer button takes a tap, and the second button takes the middle.
void sysui_ask_finger(void);
// The PWR key (main.c): true when it closed a closable question.
bool sysui_ask_back(void);
// After sysui_ask: the circle shows radio waves in place of the first letter of the title (a question of the radio).
void sysui_ask_radio(void);
// The question that shows now is the question of done (its callback): a part that closes its own question with
// sysui_ask_close() does not close another question (a WALKIE call) that came after it.
bool sysui_ask_is(void (*done)(int));
// Each frame (the app task).
void sysui_update(int64_t now_ms);
void sysui_draw(void);
// A touch: true = the system layer used it (the app must not get it).
bool sysui_input(const input_event_t *ev);
// The start picture: /data/splash.jpg when store "splash_on" is 1; it shows for store "splash_s" seconds (a touch ends
// it). Call once at start, after the first app has set the screen.
void sysui_splash(void);
// The speaker sweep: a sine tone at each frequency from f0 to f1 (step df, ms each), the level of the microphone for
// each (out[] gets n values); false = no microphone. Blocks for about n * ms (the web server task).
bool sysui_sweep(int f0, int f1, int df, int ms, float *out, int nmax, int *n);

int sysui_panel_tick(int64_t now_ms, bool allow);
// (main.c) a touch (logical): true = the panel took it (also the rest of a touch that began on the panel)
bool sysui_panel_input(const input_event_t *ev);
// (main.c) the frozen picture: a dark copy of g_fb (after a render of the app); no PSRAM: no copy (a plain panel)
void sysui_panel_snap(void);
// (main.c) the panel over the frame: live = the app drew this frame (it runs), else the frozen picture under it
void sysui_panel_draw(bool live);
// (main.c) an app opens: the panel goes at once (no save), the grip shows for 4 s
void sysui_panel_reset(int64_t now_ms);
// (main.c) the grip over the frame of the app (after its render), while the zone is on
void sysui_grip_draw(int64_t now_ms);
// (main.c) a key while the panel shows: SK_BOOT (the 3.49), SK_PUSH (a short push of the wheel button), SK_HOLD (a hold of
// it); the Watcher hold with no panel opens it with MENU in the ring. True = the panel used the key.
enum {
    SK_BOOT,
    SK_PUSH,
    SK_HOLD
};
bool sysui_panel_key(int key);
// The pull zone of the frame now (logical px): x0 <= x < x1, y0 <= y < y1 (tests, the grip).
void sysui_zone_logical(int *x0, int *y0, int *x1, int *y1);

// (main.c) true once after an item changed the app while the panel shows: an app that stops under the panel draws its
// frame again for the picture under the panel (sysui_panel_snap)
bool sysui_panel_redraw(void);
