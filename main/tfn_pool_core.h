#pragma once
// TFN OS pools: the core with no ESP-IDF call (tfn_pool_core.c). It has the formats and the rules of a pool: the
// roster file and its checks, the keys from the pool key, SipHash-2-4, the link v4 frame and the UDP v2 packets with
// their replay windows, the 'A' 'P' and 'A' 'J' beacons, the JOIN messages and the 6 digits, the secret envelope, the
// X-TFN-Auth header, the receive table of the RELAXED and STRICT modes, the send counter and the legacy peer rule.
// The crypto comes from the caller (tfn_pool_ops_t): tfn_pool.c gives the PSA functions, the host test its own
// copies. All numbers on the wire and in the file are little endian.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFN_POOL_ID_LEN     16  // the pool ID
#define TFN_POOL_KEY_LEN    32  // the pool key, wk, the box key, a seal key
#define TFN_POOL_PK_LEN     65  // a P-256 public point: 0x04, X, Y
#define TFN_POOL_DEV_LEN    8   // a device ID: SHA-256("TFNID1" || pk), the first 8 bytes
#define TFN_POOL_KID_LEN    8   // an admin key ID: SHA-256(pk), the first 8 bytes
#define TFN_POOL_MAC_LEN    6   // a radio MAC
#define TFN_POOL_NAME_MAX   24  // the pool name: 1 .. 24 bytes of UTF-8
#define TFN_POOL_MNAME_LEN  16  // a member name: UTF-8, padded with zero bytes to 16
#define TFN_POOL_URL_MAX    63  // a manager address ("http://...")
#define TFN_POOL_MEMBERS    17  // 16 boards and the manager
#define TFN_POOL_ADMINS     2   // key A, then key R
#define TFN_POOL_FILE_MAX   4096
#define TFN_POOL_MAGIC      "TFNPOOL1"
#define TFN_POOL_MAGIC_LEN  8
#define TFN_POOL_X25519_LEN 32
#define TFN_POOL_NONCE_LEN  12  // a ChaCha20-Poly1305 nonce
#define TFN_POOL_AEAD_TAG   16
#define TFN_POOL_BN_LEN     16  // the board nonce of the web header

// The roster record types (type u8, length u16, value). A type from 0x80 up is skipped; another unknown type is a fault.
#define TFN_POOL_T_ID       0x01  // pool ID (16)
#define TFN_POOL_T_NAME     0x02  // pool name (1 .. 24)
#define TFN_POOL_T_VER      0x03  // version (u32)
#define TFN_POOL_T_EPOCH    0x04  // key epoch (u32)
#define TFN_POOL_T_FLAGS    0x05  // flags (u32)
#define TFN_POOL_T_ADMIN    0x06  // admin key (65): 1 or 2 records, A first
#define TFN_POOL_T_MEMBER   0x07  // ID (8), pk (65), MAC (6), role, flags, name (16): 97 bytes
#define TFN_POOL_T_URLS     0x08  // the LAN URL, one zero byte, the VPN URL (0 .. 63 bytes each)
#define TFN_POOL_T_TIME     0x09  // the time of the signature (u64)
#define TFN_POOL_T_SIG      0x7F  // key ID (8) + r || s (64), over all bytes before this record; the last record
#define TFN_POOL_MEMBER_LEN 97
#define TFN_POOL_SIG_LEN    72

#define TFN_POOL_ROLE_MEMBER  0
#define TFN_POOL_ROLE_MANAGER 1
#define TFN_POOL_ROLE_CHILD   2

#define TFN_POOL_F_LEGACY_BEACON 0x1u  // the roster flag: the members send the plain beacon too

// ---------------------------------------------------------------- the crypto of the caller
typedef struct {
    void (*sha256)(const void *d, size_t n, uint8_t out[32]);
    // HMAC-SHA256 with the key k over the parts p[0] || p[1] || ... (np parts)
    void (*hmac)(const uint8_t *k, size_t kn, const uint8_t *const *p, const size_t *pn, int np, uint8_t out[32]);
    // ECDSA P-256 / SHA-256: true = sig (r || s) is a good signature of msg by pk
    bool (*p256_verify)(const uint8_t pk[TFN_POOL_PK_LEN], const uint8_t *msg, size_t n, const uint8_t sig[64]);
    // ChaCha20-Poly1305 (RFC 8439). seal: ct (n bytes) and the tag. open: false = a wrong tag (pt is then not valid)
    bool (*aead_seal)(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *pt,
                      size_t n, uint8_t *ct, uint8_t tag[16]);
    bool (*aead_open)(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *ct,
                      size_t n, const uint8_t tag[16], uint8_t *pt);
    // X25519 (RFC 7748): out = priv * pub; false = the operation failed
    bool (*x25519)(const uint8_t priv[32], const uint8_t pub[32], uint8_t out[32]);
} tfn_pool_ops_t;

// ---------------------------------------------------------------- the results
// The receive codes of a sealed packet (also the index of tfn_pool_rx_t count[]: ok, other, who, bad, old).
enum {
    TFN_POOL_RX_OK = 0,
    TFN_POOL_DROP_OTHER = 1,  // no pool here, or the pool tag byte is not ours
    TFN_POOL_DROP_WHO = 2,    // the sender MAC is not a roster MAC
    TFN_POOL_DROP_BAD = 3,    // a bad form or a wrong tag
    TFN_POOL_DROP_OLD = 4,    // the counter is old or seen
    TFN_POOL_NCOUNT = 5,
};

// The results of the roster parse and check: 0 = take, 1 = this board was removed, below 0 = refuse.
enum {
    TFN_POOL_R_OK = 0,
    TFN_POOL_R_REMOVED = 1,   // a good roster of the stored pool with no entry for this board
    TFN_POOL_E_SIZE = -1,     // the file is too short or too long
    TFN_POOL_E_MAGIC = -2,    // no "TFNPOOL1" at the start
    TFN_POOL_E_CUT = -3,      // a record goes past the end of the file
    TFN_POOL_E_TYPE = -4,     // an unknown record type below 0x80
    TFN_POOL_E_LEN = -5,      // a value with a wrong length
    TFN_POOL_E_COUNT = -6,    // a record type too many times
    TFN_POOL_E_MISSING = -7,  // a necessary record is not there
    TFN_POOL_E_VALUE = -8,    // a bad value (a role, a point, the URL record)
    TFN_POOL_E_AFTER = -9,    // bytes after the signature record (also a second signature)
    TFN_POOL_E_DUP = -10,     // two members with the same ID or MAC, or two same admin keys
    TFN_POOL_E_ADMIN = -11,   // the signing key is not a trusted admin key
    TFN_POOL_E_SIG = -12,     // no admin key of the roster has the key ID, or the signature does not verify
    TFN_POOL_E_POOL = -13,    // no stored pool, or another pool ID
    TFN_POOL_E_OLD = -14,     // the version is not higher than the floor
    TFN_POOL_E_SELF = -15,    // a JOIN roster with no entry for this board
};
const char *tfn_pool_err_text(int e);

// ---------------------------------------------------------------- the roster
typedef struct {
    uint8_t id[TFN_POOL_DEV_LEN];
    uint8_t pk[TFN_POOL_PK_LEN];
    uint8_t mac[TFN_POOL_MAC_LEN];
    uint8_t role;
    uint8_t flags;
    char name[TFN_POOL_MNAME_LEN + 1];
} tfn_pool_member_t;

typedef struct {
    uint8_t pool[TFN_POOL_ID_LEN];
    char name[TFN_POOL_NAME_MAX + 1];
    uint32_t ver, epoch, flags;
    uint64_t time;
    uint8_t admin[TFN_POOL_ADMINS][TFN_POOL_PK_LEN];
    int nadmin;
    tfn_pool_member_t m[TFN_POOL_MEMBERS];
    int nm;
    char lan[TFN_POOL_URL_MAX + 1], vpn[TFN_POOL_URL_MAX + 1];  // "" = none
    uint8_t kid[TFN_POOL_KID_LEN];                              // the signing key
    uint8_t sig[64];
    size_t signed_len;  // the signature covers the bytes [0, signed_len)
} tfn_pool_roster_t;

// What the board trusts when a roster comes (section 2.3 of the design).
typedef struct {
    bool join;                                // a JOIN (the 6 digits or the token): the admin key of the JOIN must sign
    const uint8_t *pool;                      // the stored pool ID (16), or NULL: no pool
    const uint8_t (*admin)[TFN_POOL_PK_LEN];  // the keys that can sign: the admins of the stored roster, or the
    int nadmin;                               // admin key of the JOIN
    uint32_t floor;                           // tfnpool/rv
    const uint8_t *self;                      // the device ID of this board (8)
} tfn_pool_trust_t;

// The form only (no crypto): the TLV records and their rules. 0 or a TFN_POOL_E_* code.
int tfn_pool_roster_parse(const uint8_t *f, size_t n, tfn_pool_roster_t *r);
// The parse, the signature and the rules of 2.3, in this order: the form; the signature (the key ID names an admin
// key of the NEW roster, and it verifies); then a JOIN: the signer is a key of t->admin (else ADMIN), no version
// floor, this board is a member (else SELF); else: the stored pool ID (else POOL), the signer is a key of t->admin
// (the stored admins, else ADMIN), the version is higher than the floor (else OLD), this board is a member (else
// REMOVED). The result: TFN_POOL_R_OK, TFN_POOL_R_REMOVED or an error.
int tfn_pool_roster_check(const tfn_pool_ops_t *o, const tfn_pool_trust_t *t, const uint8_t *f, size_t n,
                          tfn_pool_roster_t *r);
int tfn_pool_roster_slot(const tfn_pool_roster_t *r, const uint8_t mac[TFN_POOL_MAC_LEN]);  // the slot, or -1
int tfn_pool_roster_find(const tfn_pool_roster_t *r, const uint8_t id[TFN_POOL_DEV_LEN]);   // the slot, or -1
void tfn_pool_key_id(const tfn_pool_ops_t *o, const uint8_t pk[TFN_POOL_PK_LEN], uint8_t kid[TFN_POOL_KID_LEN]);
void tfn_pool_dev_id(const tfn_pool_ops_t *o, const uint8_t pk[TFN_POOL_PK_LEN], uint8_t id[TFN_POOL_DEV_LEN]);

// ---------------------------------------------------------------- the keys from the pool key
// HKDF-SHA256 (RFC 5869) with the hmac of the caller. An empty salt = 32 zero bytes. False: on > 255 * 32.
bool tfn_pool_hkdf(const tfn_pool_ops_t *o, const uint8_t *salt, size_t sn, const uint8_t *ikm, size_t in,
                   const uint8_t *info, size_t fn, uint8_t *out, size_t on);

typedef struct {
    uint32_t epoch;
    uint8_t link[16];    // "TFN link4": the SipHash key of link v4 and the UDP v2 packets
    uint8_t ptag;        // "TFN ptag": the pool tag byte
    uint8_t beacon[32];  // "TFN beacon1": the ChaCha20-Poly1305 key of the 'A' 'P' beacon
    uint8_t hint[4];     // "TFN hint": the pool hint of the 'A' 'P' beacon
} tfn_pool_keys_t;
// HKDF(ikm = the pool key, salt = the pool ID, info = text || epoch u32) for each key
void tfn_pool_derive(const tfn_pool_ops_t *o, const uint8_t key[TFN_POOL_KEY_LEN], const uint8_t pool[TFN_POOL_ID_LEN],
                     uint32_t epoch, tfn_pool_keys_t *k);

// SipHash-2-4 of a || b (the 64-bit result; the tag bytes are its little-endian bytes)
uint64_t tfn_pool_siphash(const uint8_t key[16], const uint8_t *a, size_t an, const uint8_t *b, size_t bn);

// ---------------------------------------------------------------- link v4 and the UDP v2 packets
// v4: 'A' 'L' 4 app kind len ch | pool tag | counter (4) | data (len) | SipHash(MAC || bytes before the tag) (8)
#define TFN_POOL_V4_HDR      12
#define TFN_POOL_V4_TAG      8
#define TFN_POOL_V4_DATA_MAX 230  // LINK_DATA_MAX
#define TFN_POOL_V4_MAX      250  // the ESP-NOW v1 limit
#define TFN_POOL_V4_VERSION  4
// UDP v2: the body of version 1 (its version byte = 2) | pool tag | counter (4) | SipHash(from || bytes before) (8).
// The call = 'A' 'C' 2 from(6) to(6) op; the invite = 'A' 'I' 2 from(6) app(20). The sender MAC = "from".
#define TFN_POOL_UDP_TRAIL  13
#define TFN_POOL_UDP_CALL   16  // the body of a call
#define TFN_POOL_UDP_INVITE 29  // the body of an invite

// f[0..6] = 'A' 'L' 4 app kind len ch, the data at f + 12, len = 12 + the data length. Writes the pool tag, the
// counter and the tag. The new length (len + 8), or -1 (a bad form, or cap too small).
int tfn_pool_v4_seal(const tfn_pool_keys_t *k, const uint8_t mac[TFN_POOL_MAC_LEN], uint32_t ctr, uint8_t *f, int len,
                     int cap);
// p = the body of a call or an invite (len bytes). Adds the trailer: the new length (len + 13), or -1.
int tfn_pool_udp2_seal(const tfn_pool_keys_t *k, uint32_t ctr, uint8_t *p, int len, int cap);

// The replay window of one sender: the highest counter and the 64 counters below it.
typedef struct {
    uint32_t hi;
    uint64_t seen;  // bit d = the counter hi - d came
    bool on;        // false: the first good frame sets the floor
} tfn_pool_win_t;

// The receive state of a pool: the roster MACs (fixed slots), the keys, a window for each slot and key, the counts.
typedef struct {
    bool on;                 // a pool is loaded
    bool prev_on;            // the previous key is in its hour
    tfn_pool_keys_t key[2];  // [0] the current key, [1] the previous key
    int n;
    uint8_t mac[TFN_POOL_MEMBERS][TFN_POOL_MAC_LEN];
    uint8_t role[TFN_POOL_MEMBERS];
    tfn_pool_win_t win[2][TFN_POOL_MEMBERS];
    uint32_t count[TFN_POOL_NCOUNT];  // the v4 frames: ok, other, who, bad, old
} tfn_pool_rx_t;

// Clears all; prev = NULL: no previous key.
void tfn_pool_rx_init(tfn_pool_rx_t *rx, const tfn_pool_roster_t *r, const tfn_pool_keys_t *cur,
                      const tfn_pool_keys_t *prev);
// A new roster: a MAC that stays keeps its windows; a new MAC starts with empty windows.
void tfn_pool_rx_roster(tfn_pool_rx_t *rx, const tfn_pool_roster_t *r);
// A new key: the current key and its windows become the previous ones (prev_on = true; the caller ends the hour
// with prev_on = false). The same key again changes nothing.
void tfn_pool_rx_rekey(tfn_pool_rx_t *rx, const tfn_pool_keys_t *k);
int tfn_pool_rx_slot(const tfn_pool_rx_t *rx, const uint8_t mac[TFN_POOL_MAC_LEN]);  // the slot, or -1
// The window rule: true = the counter is new (and it is now seen)
bool tfn_pool_win_take(tfn_pool_win_t *w, uint32_t ctr);

// The checks of 2.4.2 in their order: the form ('A' 'L' 4, 8 bytes or more), the pool tag byte (the current key, or
// the previous key in its hour), the length, the roster MAC, the tag, the window. TFN_POOL_RX_OK (slot = the roster
// slot) or a TFN_POOL_DROP_* code (a bad form = BAD). v4_open counts in rx->count. A frame changes a window only when
// its tag is good. udp_open: the same order; the sender MAC is the "from" field; the same window as the frames.
int tfn_pool_v4_open(tfn_pool_rx_t *rx, const uint8_t mac[TFN_POOL_MAC_LEN], const uint8_t *f, int n, int *slot);
int tfn_pool_udp2_open(tfn_pool_rx_t *rx, const uint8_t *p, int n, int *slot);

// ---------------------------------------------------------------- the send counter (tfnpool/lc)
#define TFN_POOL_CTR_BLOCK 65536u
typedef struct {
    uint32_t lc;    // the next free block (tfnpool/lc)
    uint32_t next;  // the next counter of the open block
    uint32_t end;   // the end of the open block
    bool open;
} tfn_pool_ctr_t;
void tfn_pool_ctr_init(tfn_pool_ctr_t *c, uint32_t lc);  // at the start, and with lc = 0 at a new key epoch
// The next counter. *write = true: write *lc to tfnpool/lc BEFORE the packet goes out. False: no block is left in
// this key epoch (no v4 send until a new key).
bool tfn_pool_ctr_next(tfn_pool_ctr_t *c, uint32_t *ctr, bool *write, uint32_t *lc);

// ---------------------------------------------------------------- the beacons
// 'A' 'P' 1 | hint (4) | nonce (12) | ChaCha20-Poly1305 of the plain part (43) | tag (16); AAD = the first 7 bytes
#define TFN_POOL_BEACON_LEN 78
#define TFN_POOL_BP_LEN     43  // the plain part:
#define TFN_POOL_BP_MAC     0   //   MAC (6)
#define TFN_POOL_BP_NAME    6   //   name (16)
#define TFN_POOL_BP_APP     22  //   app (12)
#define TFN_POOL_BP_FLAGS   34  //   flags (1)
#define TFN_POOL_BP_ID      35  //   device ID (8)
bool tfn_pool_ap_seal(const tfn_pool_ops_t *o, const tfn_pool_keys_t *k, const uint8_t nonce[12],
                      const uint8_t plain[TFN_POOL_BP_LEN], uint8_t out[TFN_POOL_BEACON_LEN]);
// TFN_POOL_RX_OK, TFN_POOL_DROP_OTHER (not our hint, or no pool) or TFN_POOL_DROP_BAD (a bad form or tag). The current
// key, then the previous key in its hour.
int tfn_pool_ap_open(const tfn_pool_ops_t *o, const tfn_pool_rx_t *rx, const uint8_t *p, int n,
                     uint8_t plain[TFN_POOL_BP_LEN]);

// 'A' 'J' 1 | MAC (6) | device ID (8) | name (16, zero padded): plain, while the JOIN screen is open
#define TFN_POOL_JBEACON_LEN 33
void tfn_pool_jbeacon_make(const uint8_t mac[6], const uint8_t id[8], const char *name,
                           uint8_t out[TFN_POOL_JBEACON_LEN]);
bool tfn_pool_jbeacon_parse(const uint8_t *p, int n, uint8_t mac[6], uint8_t id[8], char name[17]);

// ---------------------------------------------------------------- the JOIN (2.7.1) and the envelope (2.7.3)
// C = SHA-256("TFNJC1" || E_m_pub || N_m)
void tfn_pool_commit(const tfn_pool_ops_t *o, const uint8_t em[32], const uint8_t nm[16], uint8_t c[32]);
// u32 LE of SHA-256("TFNSAS1" || E_m_pub || E_b_pub || N_m || N_b)[0..4], mod 1 000 000
uint32_t tfn_pool_sas(const tfn_pool_ops_t *o, const uint8_t em[32], const uint8_t eb[32], const uint8_t nm[16],
                      const uint8_t nb[16]);
void tfn_pool_sas_text(uint32_t sas, char out[8]);  // "482 913"
int32_t tfn_pool_sas_parse(const char *s);          // "482913" or "482 913": the value, or -1
// The signed JOIN message: "TFNJ1" || C || E_b_pub || N_b (85 bytes: sig_b), and with em and nm also
// || E_m_pub || N_m (133 bytes: sig_m). The length.
#define TFN_POOL_JMSG_MAX 133
int tfn_pool_join_msg(const uint8_t c[32], const uint8_t eb[32], const uint8_t nb[16], const uint8_t *em,
                      const uint8_t *nm, uint8_t out[TFN_POOL_JMSG_MAX]);
// The box key statement "TFNBX1" || ID || bx_pub (46 bytes) that the device key signs (bxs)
#define TFN_POOL_BXMSG_LEN 46
void tfn_pool_box_msg(const uint8_t id[8], const uint8_t bx[32], uint8_t out[TFN_POOL_BXMSG_LEN]);
// K = HKDF(X25519(priv, peer), salt, "TFNSEAL1"), 32 bytes. JOIN: salt = C; op=key: salt = the board ID.
// False: X25519 fails or gives all zero bytes.
bool tfn_pool_seal_key(const tfn_pool_ops_t *o, const uint8_t priv[32], const uint8_t peer[32], const uint8_t *salt,
                       size_t sn, uint8_t key[32]);
// The envelope: nonce (12) | ChaCha20-Poly1305 of epoch u32 || pool key (32) || wk (32) | tag (16); AAD = SHA-256 of
// the roster file bytes.
#define TFN_POOL_ENV_LEN 96
bool tfn_pool_env_seal(const tfn_pool_ops_t *o, const uint8_t key[32], const uint8_t nonce[12], const uint8_t *roster,
                       size_t rn, uint32_t epoch, const uint8_t pkey[32], const uint8_t wk[32],
                       uint8_t out[TFN_POOL_ENV_LEN]);
bool tfn_pool_env_open(const tfn_pool_ops_t *o, const uint8_t key[32], const uint8_t *roster, size_t rn,
                       const uint8_t *env, size_t en, uint32_t *epoch, uint8_t pkey[32], uint8_t wk[32]);

// ---------------------------------------------------------------- the X-TFN-Auth header (2.6)
// "<manager ID, 16 hex digits>.<counter, decimal>.<HMAC, 64 hex digits>" (lower case hex, no leading zero);
// HMAC-SHA256(wk, "TFNWEB1" || bn (16) || counter u64 LE || method || " " || uri), uri = the request target as on
// the request line (the path and the query).
#define TFN_POOL_AUTH_MAX 104
bool tfn_pool_auth_parse(const char *h, uint8_t id[8], uint64_t *ctr, uint8_t mac[32]);
void tfn_pool_auth_mac(const tfn_pool_ops_t *o, const uint8_t wk[32], const uint8_t bn[16], uint64_t ctr,
                       const char *method, const char *uri, uint8_t out[32]);
int tfn_pool_auth_make(const tfn_pool_ops_t *o, const uint8_t id[8], const uint8_t wk[32], const uint8_t bn[16],
                       uint64_t ctr, const char *method, const char *uri, char *out, int cap);  // the length, or -1
// True: the ID is a MANAGER of the roster, the counter is above *last, the HMAC is good. Then *last = the counter.
bool tfn_pool_auth_check(const tfn_pool_ops_t *o, const tfn_pool_roster_t *r, const uint8_t wk[32],
                         const uint8_t bn[16], const char *method, const char *uri, const char *h, uint64_t *last);

// ---------------------------------------------------------------- the receive table of 2.8.2
enum {
    TFN_POOL_IN_V3 = 0,  // a link v3 frame, a call v1, an invite v1
    TFN_POOL_IN_PEER,    // a plain beacon or a hub list entry: into the peer table?
    TFN_POOL_IN_WAKE1,   // WAKE in a call v1
    TFN_POOL_IN_WAKE2,   // WAKE in a good call v2 of this pool
};
typedef struct {
    bool pool;         // this board is in a pool (state ok)
    bool strict;       // CONFIG_TFN_POOL_STRICT
    bool member;       // the sender MAC is a roster MAC
    bool manager;      // the sender has the role MANAGER
    bool is_friend;    // the sender is a friend
    bool keyed_other;  // a friend with a pinned ID that is not a member of this pool
} tfn_pool_peer_t;
// A board with no pool takes all as today (WAKE: a friend). In a pool: see the table of 2.8.2.
bool tfn_pool_take(int in, const tfn_pool_peer_t *p);

// ---------------------------------------------------------------- the legacy peer rule of 2.4.4
#define TFN_POOL_LEGACY_MS    5000u
#define TFN_POOL_LEGACY_SLOTS 8
typedef struct {
    uint8_t app[TFN_POOL_LEGACY_SLOTS];
    uint32_t ms[TFN_POOL_LEGACY_SLOTS];
    bool used[TFN_POOL_LEGACY_SLOTS];
} tfn_pool_legacy_t;
// A legacy peer sent a good v3 packet of this app now
void tfn_pool_legacy_note(tfn_pool_legacy_t *l, uint8_t app, uint32_t now_ms);
// True: send a v3 copy too (a legacy peer of this app in the last 5 s)
bool tfn_pool_legacy_on(const tfn_pool_legacy_t *l, uint8_t app, uint32_t now_ms);
