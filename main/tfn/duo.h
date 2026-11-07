#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "board.h"

// Two-player games with one button at each end of the landscape screen (640 x 172).
// The players sit side by side: player 0 has the LEFT end, player 1 the RIGHT end.
// The buttons come from the touch task (input_ends), with exact press and release
// times, at any frame rate. A player can also be the computer (cpu = true): then
// the game presses and releases that button with duo_cpu_press/duo_cpu_release.

#if BOARD_BAR_SCREEN
#define DUO_BTN_W 84  // drawn width of each end button (the touch zone is wider)
#else
// CoreS3 (320 x 240): each button is a pad in a lower corner. The touch zone is still all
// of the left or right 120 px, so a thumb can tap low and the game stays in view.
#define DUO_BTN_W 60  // drawn width of each corner pad
#define DUO_BTN_H 56  // drawn height of each corner pad (at the bottom)
#endif

typedef struct {
    bool down;        // held now
    bool pressed;     // went down since the last duo_poll()
    bool released;    // went up since the last duo_poll()
    int64_t down_ms;  // time of the last press (same clock as the app "now")
    int64_t up_ms;    // time of the last release
    int32_t held_ms;  // while down: now - down_ms; after a release: the length of that press
    bool cpu;         // the computer plays this button
    uint32_t seen;    // (internal) presses counted so far
} duo_btn_t;

// 2 BOARDS: player p is this board, and both end buttons are its button (-1: the usual two
// players on one board). The other player gets its button from the radio (cpu = true).
void duo_set_local(int p);
// At enter and at a new game: forget old presses. Keeps the cpu flags.
void duo_reset(duo_btn_t b[2], int64_t now);
// Once per frame, before the game logic.
void duo_poll(duo_btn_t b[2], int64_t now);
// For a cpu player (call from update, before the game logic reads the button).
void duo_cpu_press(duo_btn_t *b, int64_t now);
void duo_cpu_release(duo_btn_t *b, int64_t now);

// Player colours: v = brightness 0..255.
uint16_t duo_color(int player, int v);
// A default end button: glow 0..1 (for example the power of a charge), label under it.
void duo_draw_button(int player, const duo_btn_t *b, const char *label, float glow);
// 2 BOARDS: both end buttons of this board are the button of its player (local): both in its colour
void duo_draw_net(int local, const duo_btn_t *b, const char *label, float glow);
// True if the panel has shown that it sees two fingers at once. If not, the games
// tell the waiting player to lift the thumb.
bool duo_multitouch(void);
