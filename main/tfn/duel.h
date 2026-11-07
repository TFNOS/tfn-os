#pragma once

// A match of two boards (friends) for the turn games (BATTLESHIP, CHESS): the invite, the join, and a line of messages
// in order with no loss (each message goes again each 0.3 s until the other board says that it has it). The link of
// link.c (ESP-NOW and its LAN / VPN copies to the boards with the same app open); the app id keeps the games apart.
// A STANDING match (CHESS): the game waits for the players, even if they move to a different game and return,
// until one of them stops the mutual game. It also works remotely. Call duel_keep(true); a board that
// is quiet or leaves the app is AWAY (DUEL_REJOIN), not gone; duel_rejoin() connects the two boards again (each one
// sends REJOIN with a random number; the line is on when each one has seen the number of the other).
#include <stdbool.h>
#include <stdint.h>

enum {
    DUEL_OFF,
    DUEL_INVITE,
    DUEL_JOIN,
    DUEL_ON,
    DUEL_GONE,
    DUEL_REJOIN
};
#define DUEL_MSG_MAX 40  // bytes in one message

void duel_open(uint8_t app);                           // the game opens (the link starts)
void duel_close(void);                                 // the game closes (BYE, the link stops)
void duel_leave(void);                                 // the game closes, a standing match waits (no BYE)
void duel_invite(void);                                // this board invites: its friends see it in their list
void duel_join(const uint8_t mac[6]);                  // join the invite of that friend
void duel_stop(void);                                  // the end of a match, or no more invite (BYE)
void duel_keep(bool on);                               // a standing match (see above)
void duel_rejoin(const uint8_t peer[6], uint32_t id);  // back to the standing match id with that board (keep on)
void duel_poll(int64_t now_ms);                        // each frame: the packets, the sends again, the time out
int duel_state(void);                                  // DUEL_*
bool duel_host(void);                                  // this board invited (in the match: it is the host)
const uint8_t *duel_peer(void);                        // the other board
uint32_t duel_seed(void);                              // the number of the match (the host makes it)
int duel_invites(uint8_t mac[][6], int max);           // the friends that invite this board now
bool duel_send(const void *data, int len);             // false: the queue is full or the match is not on
bool duel_recv(void *data, int *len);                  // the next message in order
bool duel_fresh(void);                          // true once after each time the line comes on (the app syncs then)
bool duel_asked(uint8_t mac[6], uint32_t *id);  // a friend wants back a match that this board has not now (once)
void duel_say_ended(const uint8_t mac[6], uint32_t id, uint8_t why);  // tell it: that match ended (why: app value)
int duel_ended(void);  // the standing match ended on the other board: its why + 1 (0: no)
