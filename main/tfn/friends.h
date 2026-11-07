#pragma once

// FRIENDS: the boards that this board knows (SETTINGS > FRIENDS). Two boards "learn" each other
// once: with LEARN A FRIEND, or with the ADD badge in a lobby (each board adds the other one).
// The games for 2 boards and the WALKIE pair only with a friend, at once when it is near; the user
// picks the friend when more than one is near. A board that is not a friend gets an ADD badge.
// "Near": a packet of the arcade from that board came in the last FRIENDS_NEAR_MS (link.c tells).
// The name beacon: each lobby sends the name of its board each second (link app 'F', kind 3).

#include <stdbool.h>
#include <stdint.h>
#include <strings.h>

#define FRIENDS_OLD_MAX 8  // the friends in the blob "list" (the layout of an older firmware)

int friends_find(const uint8_t mac[6]);  // the index, or -1

// a lobby, each frame: sends the name beacon (each 1 s)
void friends_beacon(int64_t now);
// a lobby that waits for a friend (2 BOARDS, an invite, an open table), each frame: the friends on the LAN and the VPN
// that have not got this app open get a notice "<name> WANTS TO PLAY WITH YOU" (lan.c; at most one for each friend and
// app in 90 s). A tap on it opens the app with the argument "friends" (app_launch_arg()).
void friends_invite(int64_t now);

// The HELLO of a lobby: the boards that this board accepts now ({n, n MACs}; n = 0 while the user
// must pick a friend). A board pairs only when the HELLO of the other board has its MAC. At most 8 MACs (the buffers
// of the app files): the friends in the game first, then the others.
#define FRIENDS_HELLO_MAX (1 + 6 * FRIENDS_OLD_MAX)
int friends_hello_list(uint8_t *out);  // returns the bytes
bool friends_hello_ok(const uint8_t *list, int len);

// The name of a board for the screen: a friend's name, else "A BOARD"
const char *friends_name_of(const uint8_t mac[6]);

// A friend name with no factory word: "TFN 2F95" -> "2F95" (also "ARCADE 2F95" of an older firmware); a name that
// a person set stays as it is.
static inline const char *friends_short_name(const char *n)
{
    if (!strncasecmp(n, "TFN ", 4) && n[4]) {
        return n + 4;
    }
    if (!strncasecmp(n, "ARCADE ", 7) && n[7]) {
        return n + 7;
    }
    return n;
}
