#pragma once

// LAN: the arcade boards on the same WiFi network find each other with UDP (port LAN_PORT).
// - Each board sends a beacon each 2 s (broadcast): its MAC, its name (FRIENDS) and its app.
// - The link packets of link.c also go to each board on the list (UDP unicast), so the 2 BOARDS
//   features (WALKIE, the games, FRIENDS) work over the LAN too, out of the ESP-NOW range.
// The UDP datagram of a link packet = the MAC of the sender (6 bytes) + the link packet.
// Over the VPN (vpn.c): the beacon also goes to the hub (the central manager), and the manager
// answers with the list of the boards and their tunnel addresses ('A' 'R'). A board on the LAN
// is used by its LAN address; else by its tunnel address.

#include <stdbool.h>
#include <stdint.h>

#define LAN_PORT    47474
#define LAN_PEERS   16    // (with 10 boards and the manager, 8 places are not enough)
#define LAN_GONE_MS 7000  // a board with no beacon for this long is not on the list
// a board in DEEP IDLE (its beacon says it): the longest check gap (60 s) and a slow connect
#define LAN_DEEP_GONE_MS 80000

typedef struct {
    uint8_t mac[6];
    uint32_t ip;    // network byte order: the LAN address if fresh, else the tunnel address
    bool vpn;       // ip is the tunnel address
    char name[16];  // its FRIENDS name
    char app[12];   // the app that is open on it
    int64_t seen;   // ms
} lan_peer_t;

// wifi.c: the WiFi is connected (the first call starts the socket and the task).
void lan_start(void);
// The boards on the LAN now (not this one). Returns how many.
int lan_peers(lan_peer_t *out, int max);
// link.c: send a link packet to each board on the LAN; the receive function for link packets.
void lan_send_link(const uint8_t *pkt, int len);
void lan_set_rx(void (*cb)(const uint8_t mac[6], const uint8_t *pkt, int len));
// WALKIE calls (walkie_call.c): a call packet to one board (its LAN address, else its tunnel address; false = not
// known now), and the receive function (the lan task calls it).
bool lan_send_call(const uint8_t to[6], uint8_t op);
// the last beacon of this board had the DEEP IDLE flag (its radio sleeps between the checks)
bool lan_peer_deep(const uint8_t mac[6]);
// An invite to the friends on the list (LAN and VPN) that have not got this app open: their board shows a notice that
// opens the app with the argument "friends" (link.c: a 2 BOARDS lobby starts the link). At most one for each friend
// and app in 90 s.
void lan_send_invite(const char *app);
// The notice of an invite of friend from to the app with the menu name app_name ("<name> WANTS TO PLAY WITH YOU"; a tap
// opens the app at its friends lobby). The UDP invite and the radio invite (friends_radio_invite_take) use it. No
// notice when the app is open, or from a board that is not a friend.
void lan_invite_notice(const uint8_t from[6], const char *app_name);
void lan_set_call_rx(void (*cb)(const uint8_t from[6], uint8_t op));
// The op of the receive function has this bit when the call came as a good version 2 packet of the pool of this board
// (design P4 stage 2, 2.4.5). A version 1 call never has it.
#define LAN_CALL_SEALED 0x80
// A board in a pool (tfn_pool.h) sends the calls and the invites to a roster member as version 2 ('A' 'C' 2,
// 'A' 'I' 2: the body of version 1, then the pool tag, a counter and a SipHash tag), and version 1 to the other
// boards. Its beacon is 'A' 'P' (encrypted with the pool key); the plain beacon 'A' 'B' goes out only when
// tfn_pool_plain_beacon() says so. In STRICT, a board that is not a member and not a friend does not go into the
// table of the boards.
