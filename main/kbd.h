#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/kbd.h.
#include "tfn/kbd.h"

#include <stdbool.h>
#include "input.h"

// A small on-screen keyboard for the landscape screen (640 x 172, or 320 x 240 on the CoreS3-SE).
// Key sets: abc, ABC (SHIFT), and two sets of signs (SYM).
// THE KEY PREVIEW: a finger on a key lights it; after 0.2 s a big copy of the key shows above it. The finger
// can slide over the keys, and the key under the finger at the release is typed. OK and CANCEL (CANCEL only with typed
// text) ask first, on a question page (NO at the left, YES at the right).

// THE QUESTION PAGE (also for the callers, for example SETUP > WIFI: JOIN, FORGET): l1 (big when it fits), l2 (small,
// can be NULL or ""), then two big answers far apart: NO at the left, YES at the right (the 3.49: 220 px wide each,
// 200 px apart; the 320 x 240 frame: 124 px wide each, 72 px apart: from the middle of NO to YES 11.9 mm on the Watcher).
// hot: the answer under the finger (-1: none); focus: the answer of the wheel (-1: none).
void kbd_ask_render(const char *l1, const char *l2, int hot, int focus);
// The answer at a point: 0 = NO, 1 = YES, -1 = none.
int kbd_ask_at(int x, int y);
