#pragma once
// The pool of the board (tfn_pool.c): a pool ID, a pool key and a signed ROSTER (the members). The roster is the file
// /data/tfn_pool.bin; the NVS namespace "tfnpool" keeps only the secrets and the floors. One pool for each board.
// link.c, lan.c, friends.c, walkie_call.c, web_auth.c and pool_page.c use this API. A board with no pool gets the
// "no pool" answers: tfn_pool_on() false, tfn_pool_member() -1, the seal functions -1 or false.
// The formats and the pure checks are in tfn_pool_core.h: the roles TFN_POOL_ROLE_*, the drop codes TFN_POOL_DROP_*,
// TFN_POOL_BEACON_LEN, TFN_POOL_NAME_MAX and the member tfn_pool_member_t come from there.
#include <stdbool.h>
#include <stdint.h>
#include "tfn_pool_core.h"

#define TFN_POOL_FRAME_MAX    250  // a link v4 frame on the air: at most the ESP-NOW v1 payload
#define TFN_POOL_FRAME_HDR    12   // 'A' 'L' 4 app kind len ch ptag counter(4); the data starts here
#define TFN_POOL_TAG_LEN      8    // the SipHash-2-4 tag at the end of a v4 frame and of a v2 UDP packet
#define TFN_POOL_UDP_TAIL     13   // the tail of a call / invite v2: pool tag (1), counter (4), tag (8)
#define TFN_POOL_BEACON_PLAIN 43   // the plain part of 'A' 'P': MAC 6, name 16, app 12, flags 1, ID 8
#define TFN_POOL_JOIN_LEN     33   // 'A' 'J' 1, MAC 6, ID 8, name 16
#define TFN_POOL_MEMBERS_MAX  17   // 16 boards and the manager
#define TFN_POOL_SAS_LEN      7    // the 6 digits of the pairing and the end zero

// The state of the pool (GET /api/pool "state")
typedef enum {
    TFN_POOL_NONE,     // no pool
    TFN_POOL_JOIN,     // the JOIN screen is open (the board has no pool)
    TFN_POOL_OK,       // a good pool
    TFN_POOL_DAMAGED,  // a file or a key that does not verify: the board works as TFN_POOL_NONE
} tfn_pool_state_t;

// tfn_pool_counts(): the order of the 5 counts
enum {
    TFN_POOL_CNT_OK,
    TFN_POOL_CNT_OTHER,
    TFN_POOL_CNT_WHO,
    TFN_POOL_CNT_BAD,
    TFN_POOL_CNT_OLD,
    TFN_POOL_CNT_N,
};

// The steps of a JOIN (the screen POOL > JOIN, design 2.7.1)
typedef enum {
    TFN_POOL_JOIN_OFF,      // no JOIN runs
    TFN_POOL_JOIN_WAIT,     // the board sends 'A' 'J' and waits for the manager (op=hello)
    TFN_POOL_JOIN_REVEAL,   // the board answered op=hello and waits for op=reveal
    TFN_POOL_JOIN_ASK,      // the 6 digits: the board waits for ALLOW or NO
    TFN_POOL_JOIN_ALLOWED,  // ALLOW: the board waits for the roster and the secrets (op=set)
    TFN_POOL_JOIN_DONE,     // the board is in the pool
    TFN_POOL_JOIN_FAIL,     // a wrong value, NO, or the time limit: tfn_pool_join_why() tells the cause
} tfn_pool_join_t;

// ---- the start and the web, the console

// At the start, after tfn_id_init() (main.c): reads the roster file and the NVS secrets, makes the keys of the pool
// key (in PSRAM). No pool, or a damaged one: the board works as "no pool".
void tfn_pool_init(void);
// The web routes GET and POST /api/pool (web_server.c).
void tfn_pool_register(void *server);
// The console command "pool" (console.c): "pool" = the state, "pool allow <digits>", "pool no", "pool leave",
// "pool join" (the JOIN of the screen, for the tests and the onboarding).
void con_pool(char *cmd);

// ---- the state (all the tasks; cheap: no lock, no NVS)

bool tfn_pool_on(void);      // a good pool (state TFN_POOL_OK)
bool tfn_pool_strict(void);  // CONFIG_TFN_POOL_STRICT (release D)
bool tfn_pool_legacy(void);  // CONFIG_TFN_POOL_LEGACY (an old network of boards)
tfn_pool_state_t tfn_pool_state(void);
uint8_t tfn_pool_ptag(void);      // the pool tag byte (0 with no pool)
const char *tfn_pool_name(void);  // the pool name ("" = no pool)
int tfn_pool_my_role(void);       // the role of this board in the roster (-1 = no pool)
uint32_t tfn_pool_gen(void);      // +1 at each change of the roster or of the pool (also a LEAVE)
// The plain beacon 'A' 'B' (lan.c): true with no pool in a LEGACY build, or in a pool with the roster flag
// LEGACY_BEACON. A pool board sends 'A' 'P' too (tfn_pool_beacon_seal).
bool tfn_pool_plain_beacon(void);

// ---- the roster (design 2.2.1)

int tfn_pool_member(const uint8_t mac[6]);  // the roster slot of a MAC, or -1 (also -1 with no pool)
int tfn_pool_role(int slot);                // the role of a slot, or -1
int tfn_pool_members(void);                 // the count of the slots (0 with no pool)
bool tfn_pool_member_get(int slot, tfn_pool_member_t *out);

// ---- link v4 (design 2.4)

// frame: the v4 frame of the caller: bytes 0..6 ('A' 'L' 4 app kind len ch) and the data at TFN_POOL_FRAME_HDR;
// len = TFN_POOL_FRAME_HDR + the data bytes; cap = the size of the buffer. The function writes the pool tag byte, the
// next counter and the SipHash tag. Returns the new length (len + TFN_POOL_TAG_LEN), or -1 (no pool, cap too small,
// more than TFN_POOL_FRAME_MAX bytes).
int tfn_pool_frame_seal(uint8_t *frame, int len, int cap);
// The check of a v4 frame from mac (deliver of link.c): the pool tag, the roster MAC, the tag, the counter window.
// Returns 0 (take it: the data is frame[TFN_POOL_FRAME_HDR .. len - TFN_POOL_TAG_LEN)) or a TFN_POOL_DROP_* code.
// It counts the result (tfn_pool_counts).
int tfn_pool_frame_open(const uint8_t mac[6], const uint8_t *frame, int len);
// The UDP call 'A' 'C' 2 and the invite 'A' 'I' 2 (design 2.4.5): pkt = the body of version 1 (with the version byte
// 2), len its bytes. Adds the tail of TFN_POOL_UDP_TAIL bytes (from = the MAC of this board). Returns the new length,
// or -1.
int tfn_pool_udp_seal(uint8_t *pkt, int len, int cap);
// The check of a v2 UDP packet (a call of 16 bytes or an invite of 29 bytes, then the tail). The sender MAC is the
// "from" field of the packet (mac: the MAC that the caller expects, or NULL; another MAC = TFN_POOL_DROP_WHO).
// Returns 0 (the body = pkt[0 .. len - TFN_POOL_UDP_TAIL)) or a TFN_POOL_DROP_* code. It counts the result.
int tfn_pool_udp_open(const uint8_t mac[6], const uint8_t *pkt, int len);
void tfn_pool_counts(uint32_t out[TFN_POOL_CNT_N]);  // ok, other, who, bad, old (since the start)

// ---- the beacons (design 2.5)

// plain = TFN_POOL_BEACON_PLAIN bytes (MAC, name, app, flags, ID). out gets TFN_POOL_BEACON_LEN bytes, *on their
// count. false: no pool.
bool tfn_pool_beacon_seal(const uint8_t *plain, int n, uint8_t *out, int *on);
// pkt = an 'A' 'P' packet. true: our pool hint and a good tag; plain gets TFN_POOL_BEACON_PLAIN bytes (*pn).
bool tfn_pool_beacon_open(const uint8_t *pkt, int n, uint8_t *plain, int *pn);
// The 'A' 'J' packet while the JOIN screen is open (at most 10 min). Returns its length (TFN_POOL_JOIN_LEN), or 0
// when no JOIN waits for a manager.
int tfn_pool_join_beacon(uint8_t *out, int cap);

// ---- the requests of the pool manager (design 2.6)

// The check of the header X-TFN-Auth (web_auth.c). true: a good request of the pool manager (a higher counter).
bool tfn_pool_web_ok(const char *method, const char *uri, const char *hdr);
// true: req (an httpd_req_t) has the header X-TFN-Auth of the request that tfn_pool_web_ok() took last (the handler
// of op=mgr in tfn_store.c / tfn_web.c asks it: the request comes from the pool manager).
bool tfn_pool_req_mgr(void *req);
// op=mgr of /api/apps (tfn_web.c calls it first): true = go on to the token rules of tfn_store.c. A pool board takes
// op=mgr only from its pool manager (the header); a board with no pool: as before in a LEGACY build, never in a public
// build (no trust on first use).
bool tfn_pool_mgr_ok(void *req);

// ---- the screen SETUP > POOL (pool_page.c)

bool tfn_pool_join_start(void);  // the JOIN screen opens: the board starts 'A' 'J' (false: the board has a pool)
void tfn_pool_join_stop(void);   // the JOIN screen closes (also a JOIN that waits)
tfn_pool_join_t tfn_pool_join_step(void);
int tfn_pool_join_left_s(void);       // the seconds of the JOIN that are left
const char *tfn_pool_join_why(void);  // the cause of TFN_POOL_JOIN_FAIL ("" = none)
// At TFN_POOL_JOIN_ASK: the 6 digits ("482913") and the name of the manager. false: no question waits.
bool tfn_pool_join_sas(char digits[TFN_POOL_SAS_LEN], char mgr[17]);
// The answer to the question with its 6 digits: the screen gives the digits that it showed (after the rule of a
// consent, input.h), the console "pool allow <digits>" gives the digits of its line. false: no question waits, or the
// digits are not right or NULL (then the JOIN fails).
bool tfn_pool_join_allow(const char *digits);
void tfn_pool_join_no(void);
// LEAVE: the board erases its pool data (the roster file, the pool key, the request key; it keeps the floors).
// by_screen = true refuses it for a CHILD. Returns false when the board has no pool or a CHILD refuses.
bool tfn_pool_leave(bool by_screen);
