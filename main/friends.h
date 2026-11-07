#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/friends.h.
#include "tfn/friends.h"

// FRIENDS: the boards that this board knows (SETTINGS > FRIENDS). Two boards "learn" each other
// once: with LEARN A FRIEND, or with the ADD badge in a lobby (each board adds the other one).
// The games for 2 boards and the WALKIE pair only with a friend, at once when it is near; the user
// picks the friend when more than one is near. A board that is not a friend gets an ADD badge.
// "Near": a packet of the arcade from that board came in the last FRIENDS_NEAR_MS (link.c tells).
// The name beacon: each lobby sends the name of its board each second (link app 'F', kind 3).

#include <stdbool.h>
#include <stdint.h>

// 16 friends (a list of 8 is full with 9 boards and the manager, and then the manager can add a friend on one side
// only). The NVS keeps the first 8 in the old blob "list" (an older firmware reads it)
// and the others in "more". Each array that an app file gives to a friends_* function has its own size (the max
// argument); the HELLO list stays at 8 MACs (FRIENDS_HELLO_MAX: PONG and FIGHT have buffers of that size).
#define FRIENDS_MAX      16
#define FRIEND_NAME      16  // with the end 0
#define FRIENDS_NEAR_MS  3000
#define FRIENDS_LINK_APP 'N'  // the link app of FRIENDS (the name beacon, LEARN A FRIEND); not 'F' (FIGHT)
#define FRIENDS_K_NAME   3    // the kind of the name beacon (a lobby or a title sends it, a match does not)

// A LOBBY WAITS for a friend: friends_invite() came in the last FRIENDS_WAIT_MS (each lobby calls it in each frame: a
// 2 BOARDS lobby, an invite of a turn game, an open table). friends_lobby_age(): the time since the first call of that
// wait (ms; 0 = no wait).
#define FRIENDS_WAIT_MS 1500
bool friends_lobby_waits(void);
uint32_t friends_lobby_age(void);

// THE RADIO INVITE (no WiFi needed): while a lobby waits, its name beacon (friends_beacon, each second and at each
// step of the search of the link) carries a 0 and the menu name of the app. A friend that hears it by radio (a lobby,
// a game, or the listen of the menu: link_listen) gets the notice "<name> WANTS TO PLAY WITH YOU" (lan.c), at most one
// for each friend and app in FRIENDS_RADIO_GAP_MS. The beacon is sealed in a pool (link v4) as each link packet, and
// the receive rule of the pool (STRICT) applies. The radio task notes the invite; the app task (radio_play.c) takes it:
// true = mac and app have it.
#define FRIENDS_APP_NAME     20  // the bytes of the menu name of an app in an invite (with the end 0)
#define FRIENDS_RADIO_GAP_MS 90000
bool friends_radio_invite_take(uint8_t mac[6], char app[FRIENDS_APP_NAME]);

typedef struct {
    uint8_t mac[6];
    char name[FRIEND_NAME];
} friend_t;

void friends_init(void);  // reads the list (at the start of the board)
int friends_count(void);
const friend_t *friends_get(int i);
const char *friends_my_name(void);
void friends_set_my_name(const char *name);
bool friends_add(const uint8_t mac[6], const char *name);
void friends_remove(int i);

// link.c: a packet of the arcade came from mac (any app)
void friends_seen(const uint8_t mac[6], int8_t rssi);
// link.c, for each packet: true = it was a name beacon (link.c does not keep it)
bool friends_on_packet(const uint8_t mac[6], uint8_t app, uint8_t kind, const uint8_t *data, int len, int8_t rssi);
// The boards near that are not friends (they sent a beacon); copies them, returns how many
#define FRIENDS_NEW_MAX 4
typedef struct {
    uint8_t mac[6];
    char name[FRIEND_NAME];
} friend_new_t;
int friends_new_near(friend_new_t *out, int max);
// the friends near now (their indices); returns how many
int friends_near(int *idx, int max);
// link.c: a packet of an app that this board sends for now came from mac (by radio or by the LAN: the friend is in the
// same game or lobby)
void friends_game_seen(const uint8_t mac[6]);
// The friends in the same game or lobby now (a packet of an app that this board sends for, in the last
// FRIENDS_NEAR_MS; the radio and the LAN). The lobbies use this, not friends_near(): a friend that only plays CHESS
// near this board is not a choice in a PONG lobby. Returns how many.
int friends_in_game(int *idx, int max);

// The lobbies: may this board pair with mac? (the chosen friend; else any friend)
bool friends_accept(const uint8_t mac[6]);
// The friend for the next game (-1 = any friend that is near; FRIENDS_NOBODY = nobody yet:
// WALKIE pairs only after a tap or a call)
#define FRIENDS_NOBODY (-2)
void friends_choose(int i);
int friends_chosen(void);

// ---- the pools (tfn_pool.h; the design P4 stage 2, sections 2.3, 2.4.4 and 2.8.2)
// A KEYED friend has a pinned device ID: the roster of a pool gave it (the NVS blob "ids": an older firmware does not
// read it). A board in a pool calls friends_pin() for each roster member; the ID stays after the board leaves the pool.
// Returns true when the pin changed.
bool friends_pin(const uint8_t mac[6], const uint8_t id[8]);
bool friends_keyed(int i, uint8_t id[8]);  // true: friend i has a pinned ID (id gets it; id can be NULL)

// The receive rule of a pool board for a packet from mac (the core rule tfn_pool_take()). The values are the same as
// TFN_POOL_IN_* of tfn_pool_core.h.
enum {
    FRIENDS_IN_V3 = 0,  // a link v3 frame, a call v1, an invite v1
    FRIENDS_IN_PEER,    // a plain beacon, a hub list entry, a UDP packet: into the peer table?
    FRIENDS_IN_WAKE1,   // WAKE in a call v1
    FRIENDS_IN_WAKE2,   // WAKE in a good call v2 of this pool
};
// pool = this board is in a pool (tfn_pool_on), strict = tfn_pool_strict(), role = the roster role of mac
// (tfn_pool_role(tfn_pool_member(mac)); -1 = not a member). With pool = false: true (WAKE1: a friend), as with no pool.
bool friends_pool_take(int in, const uint8_t mac[6], bool pool, bool strict, int role);

// The legacy peers (design 2.4.4): a board with no pool, or on an older firmware, sent a link v3 frame of app now
// (link.c). friends_legacy_on(): true = such a frame came in the last 5 s, so a pool board sends a v3 copy too.
void friends_legacy_note(uint8_t app, uint32_t now_ms);
bool friends_legacy_on(uint8_t app, uint32_t now_ms);
