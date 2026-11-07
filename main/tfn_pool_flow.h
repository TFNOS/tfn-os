#pragma once
// The pool service with no ESP-IDF call (tfn_pool_flow.c): the load of the pool, the JOIN with the 6 digits and the
// JOIN with the store token, op=set, op=key, LEAVE, the request header of the pool manager, link v4, the UDP v2 tail
// and the beacons. tfn_pool.c gives the board functions (the NVS, the file, PSA, the time) in tpf_io_t and keeps the
// one tpf_t of the board in PSRAM behind its lock. The host test tests/unit/test_pool_flow.c gives fakes.
// The pure formats (the roster, SipHash, the counter window, the 6 digits, the request MAC text): tfn_pool_core.h.
// The contract with the manager (sdk/tools/pool.py): the "notes" of tests/unit/vectors/pool_v1.json.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tfn_pool.h"
#include "tfn_pool_core.h"

#define TPF_ROSTER_MAX TFN_POOL_FILE_MAX  // the bytes of the roster file (about 1.9 KB for 16 boards)
#define TPF_JOIN_MS    (10 * 60 * 1000)   // the JOIN screen sends 'A' 'J' at most this long
#define TPF_STEP_MS    (2 * 60 * 1000)    // each step after op=hello (reveal, the 6 digits, op=set)
#define TPF_PREV_MS    (60 * 60 * 1000)   // the receiver takes frames of the previous pool key this long
#define TPF_WEB_SEEN   64                 // the counters of the requests that the board keeps
#define TPF_ENV_PLAIN  (4 + 32 + 32)      // epoch, pool key, request key
#define TPF_ENV_LEN    TFN_POOL_ENV_LEN   // the envelope: nonce, the sealed plain part, tag

// The functions of the board. The NVS functions use the namespace "tfnpool".
typedef struct {
    const tfn_pool_ops_t *ops;                                 // SHA-256, HMAC, ECDSA P-256, ChaCha20-Poly1305, X25519
    bool (*x25519_pub)(const uint8_t sk[32], uint8_t pk[32]);  // the public part of an X25519 key
    void (*random)(uint8_t *p, size_t n);                      // true random numbers (the radio is on)
    int64_t (*now_ms)(void);
    bool (*sign)(const uint8_t *m, size_t n, uint8_t sig[64]);  // the device key (tfn_id_sign)
    bool (*nvs_get)(const char *key, void *p, size_t n);        // a blob of exactly n bytes
    bool (*nvs_set)(const char *key, const void *p, size_t n);
    void (*nvs_erase)(const char *key);
    bool (*nvs_get_u32)(const char *key, uint32_t *v);
    bool (*nvs_set_u32)(const char *key, uint32_t v);
    size_t (*file_read)(uint8_t *p, size_t max);  // the roster file: its bytes, 0 = no file
    bool (*file_write)(const uint8_t *p, size_t n);
    void (*file_erase)(void);
    bool (*token_ok)(const char *tok);  // true: the store token is set and it is tok (tfn_store.c)
    void (*mgr_clear)(void);            // a JOIN: the old manager values (tfn/lan, tfn/vpn, tfn/tok) go
    void (*left)(const char *pool);     // the roster removed this board (a notice)
    void (*hot_lock)(void);             // the lock of the packet path (NULL: none)
    void (*hot_unlock)(void);
} tpf_io_t;

// The values of this board (tfn_id.c). ok = false: no device ID yet.
typedef struct {
    bool ok;
    uint8_t id[8];
    uint8_t pk[65];
    uint8_t mac[6];
    char name[17];
    bool box;  // bx and bxs hold the box key
    uint8_t bx[32];
    uint8_t bxs[64];
} tpf_me_t;

typedef struct {
    tfn_pool_join_t step;
    bool token;          // the JOIN with the store token (no person, no 6 digits)
    int64_t until;       // the end of the JOIN screen
    int64_t step_until;  // the end of the step
    uint8_t apk[65];     // the admin key of op=hello
    char mname[17];      // the name of the manager (else its ID)
    uint8_t c[32];       // the commitment
    uint8_t eb_sk[32], eb[32], nb[16];
    uint8_t em[32], nm[16];
    char sas[TFN_POOL_SAS_LEN];
    const char *why;  // the code of TFN_POOL_JOIN_FAIL
} tpf_join_state_t;

typedef struct {
    const tpf_io_t *io;
    tpf_me_t me;
    bool legacy;  // CONFIG_TFN_POOL_LEGACY: the token JOIN
    tfn_pool_state_t state;
    tfn_pool_roster_t r;
    tfn_pool_roster_t tmp;  // a new roster before its commit
    uint8_t rb[TPF_ROSTER_MAX];
    size_t rn;
    int my_slot;
    uint32_t rv;         // the floor of the roster version
    tfn_pool_rx_t rx;    // the keys ([0] now, [1] the previous one), the roster MACs, the windows, the counts
    int64_t prev_until;  // the end of the hour of the previous key
    tfn_pool_ctr_t ctr;  // the send counter
    uint8_t wk[32];
    bool wk_on;
    uint8_t bn[16];
    bool bn_on;
    uint64_t web_seen[TPF_WEB_SEEN];  // the good counters of the requests of this start (web_take)
    int web_n;
    uint64_t web_last;  // the counter of the last good request (the handler of the same request checks it)
    uint32_t gen;
    tpf_join_state_t j;
} tpf_t;

// The answer to op=hello
typedef struct {
    uint8_t eb[32], nb[16], pk[65], id[8], bx[32], bxs[64], sig[64];
} tpf_hello_t;

void tpf_init(tpf_t *p, const tpf_io_t *io, const tpf_me_t *me, bool legacy);  // reads the file and the NVS
void tpf_me(tpf_t *p, const tpf_me_t *me);  // new values of this board (the task of tfn_id.c)
const uint8_t *tpf_bn(tpf_t *p);            // the board nonce of this start
// The ID of the pool manager in the request header: the device ID formula on its admin key.
void tpf_mid(const tpf_t *p, const uint8_t pk[65], uint8_t id[8]);

// Each function below returns NULL (good) or a code of the contract: "form", "auth", "wait", "no_join", "busy",
// "token", "commit", "sig", "admin", "old", "other", "member", "env", "digits", "no", "time", "none", "child", "id",
// "nvs", "file". tpf_http() gives the HTTP status of a code, tpf_text() a text for the screen and the console.
int tpf_http(const char *code);
const char *tpf_text(const char *code);

const char *tpf_join_start(tpf_t *p);
void tpf_join_stop(tpf_t *p);
tfn_pool_join_t tpf_join_step(tpf_t *p);  // (it ends a step after its time)
const char *tpf_hello(tpf_t *p, const uint8_t apk[65], const uint8_t c[32], const char *name, const char *auth,
                      tpf_hello_t *out);
const char *tpf_reveal(tpf_t *p, const uint8_t em[32], const uint8_t nm[16], const uint8_t sig[64]);
// digits: the 6 digits that the screen showed, or the digits of the console line; other digits or NULL end the JOIN
const char *tpf_allow(tpf_t *p, const char *digits);
void tpf_no(tpf_t *p);
// op=set: the roster (rl bytes); env = the envelope of a JOIN (TPF_ENV_LEN bytes) or NULL. mgr = the request has a
// good header of the pool manager. *what = "take", "same" or "removed".
const char *tpf_set(tpf_t *p, const uint8_t *rb, size_t rl, const uint8_t *env, size_t en, bool mgr, const char **what);
// op=key: em = the ephemeral key E_m, env = the envelope (TPF_ENV_LEN) sealed to the box key; rb = a new roster (rl
// bytes; NULL: the stored one). The roster first, then the key; the envelope fits the roster that the board keeps.
const char *tpf_key(tpf_t *p, const uint8_t em[32], const uint8_t *env, size_t en, const uint8_t *rb, size_t rl,
                    bool mgr, const char **what);
const char *tpf_leave(tpf_t *p, bool by_screen);

// The header X-TFN-Auth: "<manager ID hex>.<counter>.<HMAC hex>"
bool tpf_web_ok(tpf_t *p, const char *method, const char *uri, const char *hdr);
bool tpf_req_mgr(tpf_t *p, const char *method, const char *uri, const char *hdr);
// The counters of this start again (a test: last = 0 is empty; else each counter up to last is old)
void tpf_web_reset(tpf_t *p, uint64_t last);
// op=mgr of /api/apps (tfn_web.c): NULL = it can go on to the token rules of tfn_store.c; "auth" = refuse it.
// mgr = the request has the header of the pool manager.
const char *tpf_mgr_ok(const tpf_t *p, bool mgr);

int tpf_frame_seal(tpf_t *p, uint8_t *f, int len, int cap);
int tpf_frame_open(tpf_t *p, const uint8_t mac[6], const uint8_t *f, int len);
int tpf_udp_seal(tpf_t *p, uint8_t *b, int len, int cap);
int tpf_udp_open(tpf_t *p, const uint8_t mac[6], const uint8_t *b, int len);
bool tpf_beacon_seal(tpf_t *p, const uint8_t *plain, int n, uint8_t *out, int *on);
bool tpf_beacon_open(tpf_t *p, const uint8_t *pkt, int n, uint8_t *plain, int *pn);
int tpf_join_beacon(tpf_t *p, uint8_t *out, int cap);

const char *tpf_role_name(int role);
const char *tpf_step_name(tfn_pool_join_t s);  // off, wait, reveal, ask, allowed, done, fail
const char *tpf_join_web(tfn_pool_join_t s);   // the "join" of GET /api/pool: none, hello, wait, allowed
