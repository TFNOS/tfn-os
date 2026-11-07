#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/link.h.
#include "tfn/link.h"

// A small radio link between boards (ESP-NOW): broadcast packets on the WiFi channel now.
// The link puts the two boards on one channel by itself (link_recv): a board on a WiFi network stays on the channel of
// its network, a board with no network searches for the other board (link_meet.h in the OS).
// Each packet has a magic number and a version, so other ESP-NOW traffic does not come in.

#include <stdbool.h>
#include <stdint.h>

bool link_ready(void);
// A link in use: the link is on, it is not QUIET, and a packet of another board came in the last
// LINK_USE_MS. Then a game of 2 BOARDS (or a talk) runs on this channel: the WiFi does not scan the other channels
// (wifi.c), and the manager does not read the board (the beacon of lan.c and /api/boards say "linked").
// An app that keeps the link on with no other board (CHESS against the computer) is not "in use".
#define LINK_USE_MS 5000
bool link_in_use(void);
// The state of the radio and the statistics of the WiFi driver in the log (a diagnosis: the console "wifistat"; link_send
// calls it once when each send is refused for 1 s).
void link_diag(const char *why);
// QUIET (WALKIE with the screen off, no voice, the friend on the LAN / VPN): the WiFi power save comes back while the
// link is on; the LAN copy of each packet carries the link (an ESP-NOW packet can come late or not at all).
// link_start() and link_stop() end it.
void link_quiet(bool quiet);
bool link_quiet_on(void);
// The sends since the start and the sends that the radio refused (the tests log them)
void link_stats(uint32_t *sent, uint32_t *failed);
// The send queue of the link (link.c): the frames in flight now and at most, the frames that wait, the frames that a
// full queue dropped, the send callbacks, the probes (a send with a full count and an old callback) and the restarts of
// a stuck radio (each one a deinit + init of the WiFi driver)
typedef struct {
    int in, in_max, wait;
    uint32_t dropped, cb_ok, cb_fail, probes, restarts;
} link_tx_t;
void link_tx_stats(link_tx_t *t);
// Put the radio on channel ch now (the console tests). The meeting of link_recv can move it again.
bool link_force_channel(int ch);
// LEARN A FRIEND is open (kind = the kind of its packets of FRIENDS_LINK_APP; -1 = it closed): a LEARN packet of a board
// that is not a friend yet is a peer of the meeting (link_meet.h), so the two boards stay on one channel. link_stop()
// ends it.
void link_meet_learn(int kind);
// THE LISTEN OF THE MENU (main.c each frame: on = the menu shows): ESP-NOW receives the name beacons of friends while no
// app has the link, so a radio invite of a friend's lobby comes in (friends.c, the notice of lan.c). Only while the
// WiFi is on and not on an AP: then the radio listens all the time already (no AP = no power save), so the listen
// costs no battery. The radio then rests on the meeting channel (link_meet.h) when it can move. link_start() stops it.
void link_listen(bool on);
// the channel of the link of an app (0 = no link): wifi.c keeps a 2-board match on it
int link_pair_channel(void);
