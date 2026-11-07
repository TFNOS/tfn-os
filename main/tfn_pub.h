#pragma once
// TFN OS: the trusted publisher list of the app store. The keys that can sign an app:
//   - the built-in keys of the build (main/tfn_defaults.h: TFN_STORE_PUBKEY_BYTES, TFN_STORE_PUBKEY2_BYTES);
//   - up to TFN_PUB_MAX keys that the board owner adds ON THE SCREEN (NVS namespace "tfnpub", key "list").
// The flags of an added key: STORE (its signature in the file "sig" is enough), DEV (it can sign as a developer in
// the file "sigs"; the app also needs a STORE signature), TEST (a developer key that the board owner trusts alone).
// Only the built-in keys sign the permission policy. A web request or a card file only starts the prompt; the key goes
// into the list when the board owner taps ALLOW, or with the line "pub allow <key ID>" on the USB console. A remove can
// come from the screen, the console or the web (with the web password).
//
// main/tfn_pub_core.c has the parts with no ESP-IDF call (the list, the wait state, the parsers, the check of an app);
// tests/unit/test_pub.c tests them. main/tfn_pub.c has the NVS, the hash and the ECDSA, the prompt, /api/pub and the
// console "pub". main/tfn_pub_page.c is SETUP > TRUSTED PUBLISHERS.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tfn/input.h"

#define TFN_PUB_MAX       8       // the keys that the board owner can add
#define TFN_PUB_NAME      16      // the name of a key: 15 letters and the end
#define TFN_PUB_PK        65      // an uncompressed P-256 point (the first byte 0x04)
#define TFN_PUB_ID        8       // the key ID: the first 8 bytes of the SHA-256 of the 65-byte key
#define TFN_PUB_BUILTIN   2       // the built-in keys
#define TFN_PUB_SIGS_MAX  4       // the lines of the file "sigs" that the board reads
#define TFN_PUB_SIGS_FILE 1024    // the biggest file "sigs" that the board reads
#define TFN_PUB_WAIT_MS   120000  // a prompt stays 2 minutes
#define TFN_PUB_BLOB      (5 + TFN_PUB_MAX * (TFN_PUB_PK + TFN_PUB_NAME + 1))  // the NVS blob

enum {
    TFN_PUB_STORE = 1,
    TFN_PUB_DEV = 2,
    TFN_PUB_TEST = 4,
    TFN_PUB_FLAGS = 7
};

// the result of the check of an app (tfn_pub_check)
enum {
    TFN_PUB_NONE,          // no good signature
    TFN_PUB_OK_BUILTIN,    // "sig" verifies with a built-in key (the store apps of today)
    TFN_PUB_OK_STORE,      // "sig" verifies with an added STORE key
    TFN_PUB_OK_TEST,       // a "sigs" line of an added TEST key verifies
    TFN_PUB_NOT_REVIEWED,  // only a "sigs" line of an added DEV key verifies: no store review
};

typedef struct {
    uint8_t pk[TFN_PUB_PK];
    char name[TFN_PUB_NAME];
    uint8_t flags;
} tfn_pub_t;

typedef struct {
    tfn_pub_t e[TFN_PUB_MAX];
    int n;
} tfn_pub_list_t;

// one line of the file "sigs": "TFNSIG2 <key ID, 16 hex digits> <r || s, 128 hex digits>"
typedef struct {
    uint8_t id[TFN_PUB_ID];
    uint8_t sig[64];
} tfn_pub_sig_t;

// a key that waits for the tap of the board owner. seq: the number of the wait. Each start gives the next number (never
// 0), and the end of a wait keeps it: an answer for an old wait never gets to a new one, also not to a new wait of the
// same key with other flags or another name.
typedef struct {
    bool on;
    tfn_pub_t p;
    int64_t until_ms;
    uint32_t seq;
} tfn_pub_wait_t;

// The hash and the signature check (tfn_pub.c gives the PSA ones; the host test gives its own).
typedef struct {
    void (*sha256)(const void *d, size_t n, uint8_t out[32]);
    // a signature (r || s) of msg with the key pk
    bool (*verify)(const uint8_t pk[TFN_PUB_PK], const uint8_t *msg, size_t n, const uint8_t sig[64]);
    // the signature of "sig" with a built-in key (the path of the store apps of today, with its cache)
    bool (*builtin_ok)(const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl);
    // the built-in keys (an all-zero key = no key)
    const uint8_t *builtin[TFN_PUB_BUILTIN];
} tfn_pub_ops_t;

// ---------------------------------------------------------------- the core (tfn_pub_core.c)
void tfn_pub_key_id(const tfn_pub_ops_t *o, const uint8_t pk[TFN_PUB_PK], uint8_t id[TFN_PUB_ID]);
bool tfn_pub_key_set(const uint8_t pk[TFN_PUB_PK]);  // a real key (the first byte 0x04)
// "3F2A-91C0-5B7E-0D44" (out: 20 bytes)
void tfn_pub_id_text(const uint8_t id[TFN_PUB_ID], char out[20]);
// 16 hex digits; '-' and ' ' between them are allowed
bool tfn_pub_id_parse(const char *s, uint8_t id[TFN_PUB_ID]);
// "store", "dev", "test", joined with ',' or '+' (any case); -1 = not good
int tfn_pub_flags_parse(const char *s);
void tfn_pub_flags_text(int flags, char *out, int len);  // "STORE+DEV"
// a name: 1 .. 15 letters A-Z a-z 0-9 space - _ . (the list keeps it in capitals); false = not good
bool tfn_pub_name_set(char out[TFN_PUB_NAME], const char *s);
// the place of the key with this ID in the list, or -1
int tfn_pub_find(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const uint8_t id[TFN_PUB_ID]);
// can p go into the list (a real key, a name, flags, a free place, not a key of the list or a built-in key)
bool tfn_pub_add_ok(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const tfn_pub_t *p, char *msg, int len);
bool tfn_pub_add(const tfn_pub_ops_t *o, tfn_pub_list_t *l, const tfn_pub_t *p, char *msg, int len);
bool tfn_pub_remove(const tfn_pub_ops_t *o, tfn_pub_list_t *l, const uint8_t id[TFN_PUB_ID]);
// the NVS blob: "TPL1", the count, then each key (65), name (16), flags (1). unpack: false = a bad blob (the list is
// empty then)
size_t tfn_pub_pack(const tfn_pub_list_t *l, uint8_t *out, size_t max);
bool tfn_pub_unpack(tfn_pub_list_t *l, const uint8_t *b, size_t n);
// the lines of the file "sigs" into out (max lines); a line that is not good is skipped
int tfn_pub_sigs_parse(const char *t, size_t n, tfn_pub_sig_t *out, int max);
// the value of "publisher=" of manifest.txt (a key ID); false = none
bool tfn_pub_manifest_pub(const char *m, size_t n, uint8_t id[TFN_PUB_ID]);
// a key file (the card, or the body of POST /api/pub): "TFNPUB1", "name=", "flags=", "key=<130 hex digits>"
bool tfn_pub_file_parse(const char *t, size_t n, tfn_pub_t *p);
// 130 hex digits (spaces and line ends allowed around them) into a key; false = not a key
bool tfn_pub_hex_key(const char *t, size_t n, uint8_t pk[TFN_PUB_PK]);
// the wait state: start (false: a key waits already, or p cannot go into the list), the time (false: nothing waits
// now), the answer of the board owner (true: the key is in the list now)
bool tfn_pub_wait_start(tfn_pub_wait_t *w, const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const tfn_pub_t *p,
                        int64_t now_ms, char *msg, int len);
bool tfn_pub_wait_live(tfn_pub_wait_t *w, int64_t now_ms);
bool tfn_pub_wait_answer(tfn_pub_wait_t *w, const tfn_pub_ops_t *o, tfn_pub_list_t *l, bool allow, int64_t now_ms,
                         char *msg, int len);
// the ALLOW of the USB console ("pub allow <key ID>"): true when the key that waits has this ID (it is in the list
// now). Another ID, or nothing waits: false, and the wait does not change.
bool tfn_pub_wait_allow_id(tfn_pub_wait_t *w, const tfn_pub_ops_t *o, tfn_pub_list_t *l, const uint8_t id[TFN_PUB_ID],
                           int64_t now_ms, char *msg, int len);
// the check of an app: manifest.txt, the file "sig" (NULL = none) and the file "sigs" (NULL = none). who: the name
// of the key that passed (TFN_PUB_OK_STORE, _OK_TEST, _NOT_REVIEWED), else "".
int tfn_pub_check(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const uint8_t *m, size_t ml, const uint8_t *sig,
                  size_t sl, const char *sigs, size_t gl, char who[TFN_PUB_NAME]);

// ---------------------------------------------------------------- the board (tfn_pub.c)
void tfn_pub_init(void);  // tfn_init: the list from NVS
// the check of an installed app (tfa_read_entry), with the keys of the board
int tfn_pub_app_check(const uint8_t *m, size_t ml, const uint8_t *sig, size_t sl, const char *sigs, size_t gl,
                      char who[TFN_PUB_NAME]);
int tfn_pub_count(void);                  // the added keys
bool tfn_pub_get(int i, tfn_pub_t *out);  // a copy of added key i
int tfn_pub_builtin_count(void);          // the built-in keys that are set
bool tfn_pub_builtin_id(int i, uint8_t id[TFN_PUB_ID]);
// the key that waits for the tap, its seconds left, and the number of its wait (seq can be NULL)
bool tfn_pub_waiting(tfn_pub_t *out, int *secs, uint32_t *seq);
// a key from the web or a card: it waits for the tap of the board owner (the prompt shows on the app task)
bool tfn_pub_request(const tfn_pub_t *p, char *msg, int len);
bool tfn_pub_request_card(char *msg, int len);  // the first key file in /sdcard/pub/ that is not in the list
bool tfn_pub_remove_id(const uint8_t id[TFN_PUB_ID], char *msg, int len);
// the tap of the board owner on ALLOW / NO (the app task) for the wait with the number seq (the wait that the screen
// showed: tfn_pub_waiting): when that wait ended, a new wait gets no answer (false), also a new wait of the same key
bool tfn_pub_answer(bool allow, uint32_t seq, char *msg, int len);
// "pub allow <key ID>" of the USB console: the ALLOW of the key that waits, when it has this ID. The USB cable counts
// as the tap (a person with the cable can write any firmware). The web has no way to it.
bool tfn_pub_allow_id(const uint8_t id[TFN_PUB_ID], char *msg, int len);
void tfn_pub_key_id_of(const tfn_pub_t *p, uint8_t id[TFN_PUB_ID]);
void tfn_pub_tick(void);             // the app task, each frame: the prompt
void tfn_pub_web_register(void *h);  // GET / POST /api/pub
void con_pub(char *cmd);             // the console "pub"

// ---------------------------------------------------------------- SETUP > TRUSTED PUBLISHERS (tfn_pub_page.c)
void tfn_pub_page_open(void);
void tfn_pub_page_update(int64_t now);
bool tfn_pub_page_input(const input_event_t *ev, int64_t now);  // true when the page closes (BACK)
void tfn_pub_page_render(int64_t now);
