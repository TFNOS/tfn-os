#pragma once

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

typedef enum {
    N2_OFF,
    N2_LOBBY,
    N2_HOST,
    N2_CLIENT
} n2_role_t;
typedef enum {
    N2_EV_NONE,
    N2_EV_PAIRED,
    N2_EV_LOST,
    N2_EV_PEER_LOBBY
} n2_event_t;

#define N2_K_APP 8  // the packet kinds of the app start here

// The app opens the lobby (app_id: one letter for each game; info: a number for the other board,
// for example the screen width) and closes the link (BYE when it is paired).
void net2_open(uint8_t app_id, uint16_t info);
void net2_close(void);
n2_role_t net2_role(void);

// The packets of the app (CLIENT: the state of the HOST; HOST: the inputs of the CLIENT), and the
// events of the app (CLIENT: type >= 16).
typedef void (*n2_rx_fn)(uint8_t kind, const uint8_t *data, int len);
typedef void (*n2_ev_fn)(const uint8_t *b, int len);
#define N2_K_STATE 2
// Once per frame, before the game logic. Returns an event: paired, lost, or "the other board is
// in the lobby again" (it left the game: the app goes to its title or its start).
n2_event_t net2_poll(int64_t now, n2_rx_fn rx, n2_ev_fn ev);

// HOST: the state of this frame (at most N2_STATE_MAX bytes); the events go with it.
#define N2_STATE_MAX 180
void net2_send_state(const void *data, int len, int64_t now);
// CLIENT: an input packet of the app (kind >= N2_K_APP).
void net2_send(uint8_t kind, const void *data, int len);
// CLIENT: a time of the HOST in the local time (the same view: a state shows the HOST time when it came).
int64_t net2_local_ms(int64_t host_ms);

// HOST: the effects. While recording is on (in a game), each event goes to the CLIENT.
void net2_rec(bool on);
void net2_ev(const uint8_t *b, int n);  // an event of the app: b[0] = its type (>= 16), n <= 24
void net2_sfx(sfx_t id);                // play here, and on the CLIENT

// The button of the other board in the 2-button games (duo.h).
// CLIENT: send the button of this board (each frame). HOST: put it into b (after duo_poll; b->cpu
// must be true, so that duo_poll does not read the touch for it).
void net2_duo_send(const duo_btn_t *mine);
void net2_duo_feed(duo_btn_t *b, int64_t now);

// Drawing (gfx): the lobby box and the message of a lost link (3 s).
void net2_draw_lobby(const char *game);
// A tap in the lobby: true if it chose a friend (the app then does nothing more with it).
bool net2_lobby_tap(int x, int y);
// For the own lobbies of PONG and FIGHT: may this board pair with mac now (the friend rule), and
// has the user a choice now (a friend, or the ADD badge of a new board; then net2_draw_lobby
// shows it)? Their lobbies also send the name beacon (friends_beacon).
bool net2_lobby_accept(const uint8_t mac[6]);
bool net2_lobby_choosing(void);
void net2_draw_lost(int64_t now);
