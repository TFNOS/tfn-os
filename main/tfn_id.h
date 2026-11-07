#pragma once
// The device identity of the board: a P-256 key pair that the board makes one time. The NVS namespace "tfnid" keeps
// it (its own namespace: store_make_room() of settings.c moves only the keys of the namespace "arcade"):
//   sk = the private scalar (32 bytes), pk = the public point (65 bytes: 0x04, X, Y),
//   xs = the signature of the X25519 statement (64 bytes: r, s).
// The private key never goes out of tfn_id.c: not to the web, the log, the console or a backup file.
// THE ID = the first 8 bytes of SHA-256("TFNID1" || pk), as 16 hex digits in 4 groups: "3F2A-91C0-5B7E-0D44". Only
// the signing key gives the ID: a new VPN key does not change it.
// THE X25519 STATEMENT: xs = ECDSA P-256 with SHA-256, by sk, over "TFNX1" || the 8 ID bytes || the 32 bytes of the
// VPN public key (vpn.h). It binds the VPN key to the ID. The board makes it again when the VPN key changes.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFN_ID_PK_LEN 65  // the public point: 0x04, X, Y
#define TFN_ID_LEN    20  // the ID text and its end zero
#define TFN_ID_XS_LEN 64  // the signature of the X25519 statement: r, s

// At the start, after the WiFi start (main.c): reads the key from the NVS. A short task makes the key at the first
// start (when the radio is on: true random numbers), and makes the X25519 statement when it is missing or old.
void tfn_id_init(void);
const char *tfn_id_str(void);     // the ID text; "" = no ID yet
const uint8_t *tfn_id_pk(void);   // the public point (TFN_ID_PK_LEN bytes); NULL = no ID yet
const uint8_t *tfn_id_xs(void);   // the X25519 statement (TFN_ID_XS_LEN bytes); NULL = none
const char *tfn_id_x25519(void);  // the VPN public key that xs binds (base64); "" = none
// The web route GET /api/id: {"id", "pk" (hex), "x25519", "xs" (hex), "alg": "p256"} (web_server.c).
void tfn_id_register(void *server);
const uint8_t *tfn_id_raw(void);  // the 8 ID bytes; NULL = no ID yet

// THE BOX KEY of the pool (tfn_pool.h): an X25519 key pair that the task of the start makes one time (after the WiFi
// start). The NVS namespace "tfnpool" keeps the private part "bx" (32 bytes) and "bxs" = the signature by the device
// key of "TFNBX1" || the 8 ID bytes || the public part. The manager encrypts the pool secrets to the public part.
const uint8_t *tfn_id_box(void);      // the public part (32 bytes); NULL = none yet
const uint8_t *tfn_id_box_sig(void);  // bxs (TFN_ID_XS_LEN bytes); NULL = none yet
// A signature (r || s) of msg by the device key. Only for a msg that starts with "TFNBX1" or "TFNJ1" (the box key
// and the JOIN of a pool); false for all other messages, or with no ID. It reads the NVS (not an interrupt).
bool tfn_id_sign(const uint8_t *msg, size_t n, uint8_t sig[TFN_ID_XS_LEN]);

// ---- the ID math (no ESP-IDF call: the host test tests/unit/test_tfn_id.c uses it too)

// A SHA-256 function: the board gives the PSA one, the host test its own copy.
typedef void (*tfn_id_sha256_fn)(const uint8_t *data, size_t n, uint8_t out[32]);

// The 8 ID bytes of the public point pk.
static inline void tfn_id_bytes(const uint8_t pk[TFN_ID_PK_LEN], tfn_id_sha256_fn sha256, uint8_t id[8])
{
    uint8_t msg[6 + TFN_ID_PK_LEN], d[32];
    for (int i = 0; i < 6; i++) {
        msg[i] = (uint8_t)"TFNID1"[i];
    }
    for (int i = 0; i < TFN_ID_PK_LEN; i++) {
        msg[6 + i] = pk[i];
    }
    sha256(msg, sizeof msg, d);
    for (int i = 0; i < 8; i++) {
        id[i] = d[i];
    }
}

// The text of the 8 ID bytes: 16 hex digits (capital letters) in 4 groups of 4, with "-" between the groups.
static inline void tfn_id_text(const uint8_t id[8], char out[TFN_ID_LEN])
{
    static const char HEX[] = "0123456789ABCDEF";
    int k = 0;
    for (int i = 0; i < 8; i++) {
        if (i && !(i & 1)) {
            out[k++] = '-';
        }
        out[k++] = HEX[id[i] >> 4];
        out[k++] = HEX[id[i] & 15];
    }
    out[k] = 0;
}
