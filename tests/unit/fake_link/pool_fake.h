// The link test: a fake pool service (the API of tfn_pool.h) on the pool core, for more than one board in one
// program. The board of each call = the board whose MAC is fk_mac (fake_board.h); a MAC with no board = no pool.
// The keys come from the vector file of the manager (the derive part); the beacons give and take the packets of
// the vector file (the crypto of the beacon is in the test of the core).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PF_BOARDS 6
// keys: link (16), ptag, beacon (32), hint (4) of one pool key
typedef struct {
    uint8_t link[16];
    uint8_t ptag;
    uint8_t beacon[32];
    uint8_t hint[4];
} pf_keys_t;

void pf_reset(void);
// board b: its MAC, its keys (NULL = no pool), STRICT, the roster flag LEGACY_BEACON
void pf_board(int b, const uint8_t mac[6], const pf_keys_t *k, bool strict, bool plain);
// the roster of all pool boards (the same pool): MAC, role, device ID
void pf_roster_add(const uint8_t mac[6], int role, const uint8_t id[8], const char *name);
void pf_set_ctr(int b, uint32_t next);  // the next send counter of board b
void pf_counts(int b, uint32_t out[5]);
// the beacon of board b: the expected plain part (43) and the packet (78); the packets that open: pkt -> plain
void pf_beacon(int b, const uint8_t plain[43], const uint8_t pkt[78]);
void pf_beacon_in(const uint8_t pkt[78], const uint8_t plain[43]);
// the JOIN beacon of board b (MAC, ID, name); off with name = NULL
void pf_join(int b, const uint8_t mac[6], const uint8_t id[8], const char *name);
// a UDP v2 packet of board b (its keys and counter): body (len bytes, the version byte 2) -> the new length or -1
int pf_udp_seal(int b, uint8_t *p, int len, int cap);
// a v4 frame of board b (f = the 7 first bytes and the data at 12; len = 12 + the data) -> the new length or -1
int pf_frame_seal(int b, uint8_t *f, int len, int cap);
