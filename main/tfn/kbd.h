#pragma once

#include <stdbool.h>
#include "input.h"

// A small on-screen keyboard for the landscape screen (640 x 172, or 320 x 240 on the CoreS3-SE).
// Key sets: abc, ABC (SHIFT), and two sets of signs (SYM).
// THE KEY PREVIEW: a finger on a key lights it; after 0.2 s a big copy of the key shows above it. The finger
// can slide over the keys, and the key under the finger at the release is typed. OK and CANCEL (CANCEL only with typed
// text) ask first, on a question page (NO at the left, YES at the right).

enum {
    KBD_NONE,
    KBD_OK,
    KBD_CANCEL
};

// buf keeps the text (max - 1 characters). secret: show '*' until SHOW is on.
void kbd_open(const char *title, char *buf, int max, bool secret);
// The question of OK (call it after kbd_open; the text is not copied). NULL: "USE THIS TEXT?". "": OK acts at once.
void kbd_set_ok_question(const char *q);
// The Hebrew letters on the SYM key (abc -> HEB -> #+=), and the keyboard opens with them (call it after kbd_open).
void kbd_set_hebrew(bool on);
// Returns KBD_OK or KBD_CANCEL when the user is done (after YES on the question).
int kbd_input(const input_event_t *ev);
// The wheel (the Watcher): steps move the focus over the keys and the buttons; a push presses the key in
// focus. Returns as kbd_input().
int kbd_wheel(int steps, int presses);
void kbd_render(int64_t now);

// The state (for the tests and the callers): 0 = the keys, 1 = a finger on a key (its preview), 2 = the question of
// OK / CANCEL.
int kbd_state(void);
