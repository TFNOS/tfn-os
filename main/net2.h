#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/net2.h.
#include "tfn/net2.h"

// 2 BOARDS for the smaller games: two boards play one game by radio (ESP-NOW, link.c).
// - The lobby: each board sends HELLO; the faster board is the HOST (it runs the game; the speed class
//   in the HELLO: 3.49 > AMOLED > CoreS3 > Watcher; equal: the lower MAC), the other one is the CLIENT
//   (it sends its inputs and draws the state of the HOST). An old board (HELLO version 1): the lower MAC.
//   The link puts the two boards on one channel (link.c).
// - The HOST sends a state packet in each frame: the state of the game (the app packs it), the
//   time of the HOST, and the effect events of the last 3 frames (sounds, and the events of the
//   app). The CLIENT plays each event once.
// - The time of a press: the CLIENT gives each press and release in the time of the HOST, as seen
//   on its screen (the last state when the press came). So the HOST judges the press against
//   what the player saw.
// - The end of the link: BYE (the app closes), or no packet for 3 s.
// - The order: a state that is older than the last one (the HOST time) does not go to the app (its new
//   events play).

#include <stdbool.h>
#include <stdint.h>
#include "audio.h"
#include "duo.h"

bool net2_failed(void);  // the radio did not start
uint16_t net2_peer_info(void);

// CLIENT: a local time (ms) in the time of the HOST, as seen on this screen.
int64_t net2_host_ms(int64_t local_ms);

void net2_tone(uint16_t f0, uint16_t f1, uint16_t ms, wave_t w, uint8_t vol);

// The last N2_EV_LOST came from a BYE (the other board ended the link), not from a time out.
bool net2_last_bye(void);
// The MAC of the other board (while paired).
const uint8_t *net2_peer(void);
