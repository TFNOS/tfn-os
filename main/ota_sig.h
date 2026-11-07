#pragma once

// The signature of a firmware update: the trailer "TFNOTA1" (ota_sig.c). Pure C with no ESP-IDF call: the caller
// gives the SHA-256 and the ECDSA P-256 check as functions (ota.c: PSA; tests/unit: a host copy).
//
// The signed file = the ESP-IDF image as the build makes it + OTA_SIG_TRAILER bytes. All numbers are little endian.
//   0    8  magic "TFNOTA1\0"
//   8    2  the trailer format (1)
//   10   2  flags (0 now; the check does not read them)
//   12   4  image_len = the file length - OTA_SIG_TRAILER
//   16  32  image_sha256 = the SHA-256 of the file bytes [0, image_len)
//   48  16  kind ("lcd349", "cores3" ... zero fill)
//   64  32  version (the version of the app description at 0x30 of the image, zero fill)
//   96   4  sec_ver (the security version: the board refuses a value lower than its minsv)
//   100  8  t (the time of the signature, Unix seconds; only information)
//   108  1  nsig (1 .. OTA_SIG_MAX)
//   109 19  zero
//   128 72 x OTA_SIG_MAX  each signature: key_id (8) + r || s (64)
//   416 608 zero
// Each signature = ECDSA P-256 with SHA-256 over the bytes [0, OTA_SIG_SIGNED) of the trailer. These bytes hold the
// image hash, so the signature covers the full image. key_id = the first 8 bytes of the SHA-256 of the 65-byte key.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OTA_SIG_TRAILER  1024  // the trailer bytes at the end of the file
#define OTA_SIG_SIGNED   128   // the signed part of the trailer
#define OTA_SIG_MAX      4     // the signatures in one trailer
#define OTA_SIG_KEYS     4     // the keys in the key list of the firmware
#define OTA_SIG_KEY_LEN  65    // an uncompressed P-256 point: 0x04, X, Y
#define OTA_SIG_ID_LEN   8     // a key ID
#define OTA_SIG_KIND_LEN 16    // the kind field
#define OTA_SIG_VER_LEN  32    // the version field

// The mode of the check (the build chooses it): OFF = no check; REPORT = check and report, refuse nothing;
// ENFORCE = refuse each image that does not pass.
typedef enum {
    OTA_SIG_OFF,
    OTA_SIG_REPORT,
    OTA_SIG_ENFORCE
} ota_sig_mode_t;

// The result of a check, in the order of the checks. The code of each one: ota_sig_code().
typedef enum {
    OTA_SIG_OK,    // "ok": a trusted key signed the image
    OTA_SIG_NONE,  // "sig_none": no trailer
    OTA_SIG_FORM,  // "sig_form": a trailer with a wrong form (format, image_len, nsig, the zero bytes, a cut trailer)
    OTA_SIG_HASH,  // "sig_hash": the image hash is not the hash in the trailer
    OTA_SIG_KIND,  // "sig_kind": the trailer is for another board kind or another version
    OTA_SIG_KEY,   // "sig_key": no signature of a key of the key list
    OTA_SIG_BAD,   // "sig_bad": a key of the list is named, but its signature is wrong
    OTA_SIG_OLD,   // "sig_old": a good signature, but sec_ver is lower than minsv
    OTA_SIG_ERR    // "sig_err": the board could not do the check (no memory for the hash)
} ota_sig_res_t;

// The crypto functions of the caller. Each one returns false on a fault.
typedef struct {
    // the SHA-256 of data[0, n) into out
    bool (*sha256)(const uint8_t *data, size_t n, uint8_t out[32]);
    // true: sig (r || s) is a good ECDSA P-256 / SHA-256 signature of msg[0, n) by the key pub
    bool (*verify)(const uint8_t pub[OTA_SIG_KEY_LEN], const uint8_t *msg, size_t n, const uint8_t sig[64]);
} ota_sig_crypto_t;

// What the board expects of a trailer.
typedef struct {
    const uint8_t (*keys)[OTA_SIG_KEY_LEN];  // the key list (an entry that does not start with 0x04 is empty)
    int nkeys;
    const char *kind;     // BOARD_KIND_NAME
    const char *version;  // the version at 0x30 of the image (up to 32 characters)
    uint32_t minsv;       // the lowest security version that the board takes
} ota_sig_want_t;

typedef struct {
    ota_sig_res_t res;
    int key;                         // the index in the key list of the key that verified (-1: none)
    uint8_t key_id[OTA_SIG_ID_LEN];  // its key ID (all zero: none)
    bool has_sv;                     // the trailer form is good: sec_ver is the value of the trailer
    uint32_t sec_ver;
} ota_sig_result_t;

// The check of a trailer tr[0, n) of a file of total bytes. hash = the SHA-256 of the file bytes
// [0, total - OTA_SIG_TRAILER), or NULL when the board could not make it. The result goes into out (also on a fault).
ota_sig_res_t ota_sig_check(const uint8_t *tr, size_t n, uint32_t total, const uint8_t hash[32],
                            const ota_sig_want_t *want, const ota_sig_crypto_t *crypto, ota_sig_result_t *out);
// The form check only (no hash, no signature): true when tr[0, n) is a trailer of a good form; *sec_ver = its value.
bool ota_sig_peek(const uint8_t *tr, size_t n, uint32_t total, uint32_t *sec_ver);
// The key ID of a key (false: an empty entry, or a hash fault)
bool ota_sig_key_id(const ota_sig_crypto_t *crypto, const uint8_t pub[OTA_SIG_KEY_LEN], uint8_t id[OTA_SIG_ID_LEN]);
// "ok", "sig_none" ... (the codes of the log, the JSON and the answer of an update)
const char *ota_sig_code(ota_sig_res_t res);
// A short reason in words for the log
const char *ota_sig_why(ota_sig_res_t res);
// "off", "report", "enforce"
const char *ota_sig_mode_name(ota_sig_mode_t mode);
// The text of last_sig: "ok:<key ID in 16 hex digits>", or the code of the fault. Returns the length.
int ota_sig_text(const ota_sig_result_t *r, char *b, size_t n);
// The 16 hex digits of a key ID into b (17 bytes)
void ota_sig_id_hex(const uint8_t id[OTA_SIG_ID_LEN], char b[17]);
// true: the update goes on with this result (OFF and REPORT: always; ENFORCE: only OTA_SIG_OK)
bool ota_sig_accept(ota_sig_mode_t mode, ota_sig_res_t res);
// The rule before a new firmware marks itself good: true = mark it. pending = the firmware came by OTA and is not
// marked yet; own = the check of its own trailer. ENFORCE marks a pending firmware only with a good own trailer.
bool ota_sig_mark_ok(ota_sig_mode_t mode, bool pending, ota_sig_res_t own);
// The new minsv after a firmware marks itself good: the higher one of minsv and a good own sec_ver
uint32_t ota_sig_new_minsv(uint32_t minsv, const ota_sig_result_t *own);

// The self-test of a new firmware before its mark (ota_tick in ENFORCE). A firmware that came by OTA starts in the
// pending state. It marks itself good only when its own trailer verifies with its own key list and the web server
// has the /api/ota handler. Else it goes back to the old firmware (a rollback), and so a firmware that cannot take a
// next update never stays on a board. When no rollback is possible, ota_sig_back_plan() tells what it does.
#define OTA_SELF_MARK_MS 20000  // the first time of the mark (ms after the start)
#define OTA_SELF_WEB_MS  90000  // the longest wait for the web server (ms after the start); then the self-test fails

typedef enum {
    OTA_SELF_NONE,  // no self-test: the firmware is not pending (a USB flash, or the mark is done)
    OTA_SELF_WAIT,  // not yet: before OTA_SELF_MARK_MS, or the web server does not run yet
    OTA_SELF_MARK,  // mark the firmware good (then minsv)
    OTA_SELF_BACK   // the self-test failed: go back to the old firmware when possible (ota_sig_back_plan)
} ota_self_t;

// The decision of the self-test at now_ms after the start. own = the check of the own trailer (only ENFORCE reads
// it; the caller does the check at OTA_SELF_MARK_MS or later). web = the /api/ota handler is registered. *why (when
// why is not NULL) = the cause of OTA_SELF_WAIT and OTA_SELF_BACK, "" for the other results.
ota_self_t ota_sig_self_test(ota_sig_mode_t mode, bool pending, int64_t now_ms, ota_sig_res_t own, bool web,
                             const char **why);

// The end of a failed self-test. A rollback is good only when the old firmware can run and take a next update. Else
// a restart only starts the same failed self-test again (a restart loop), or starts a firmware that is not TFN OS:
// then this firmware runs on. A firmware that has /api/ota marks itself good, so the bootloader keeps it after a next
// restart (minsv does not change: only a passed self-test changes minsv). The update check stays in the mode of the
// build: a next update must pass it.
typedef enum {
    OTA_BACK_ROLLBACK,   // back to the old firmware (esp_ota_mark_app_invalid_rollback_and_reboot)
    OTA_BACK_KEEP_MARK,  // no rollback: this firmware runs on and marks itself good
    OTA_BACK_KEEP        // no rollback and no /api/ota: this firmware runs on, pending, with no restart
} ota_back_t;

// rollback = ESP-IDF can go back (esp_ota_check_rollback_is_possible), other_tfn = the other app slot has a TFN OS
// image of this board kind (its board tag), web = the /api/ota handler is registered
ota_back_t ota_sig_back_plan(bool rollback, bool other_tfn, bool web);
// "rollback", "keep_mark", "keep"
const char *ota_sig_back_name(ota_back_t b);

// Step 1 of the change of the layout (layout.c) copies the firmware from ota_1 to ota_0. The trailer holds
// image_len and the hash of the bytes [0, image_len) only. So a copy of the bytes [0, image_len + OTA_SIG_TRAILER)
// to the same offsets of ota_0 verifies the same as the source. The bytes to copy: image_len + OTA_SIG_TRAILER when
// after[0, n) (the bytes directly after the image) is a trailer of a good form for this image, else image_len.
uint32_t ota_sig_copy_len(const uint8_t *after, size_t n, uint32_t image_len);
// Step 1 again? false when ota_0 has a copy of this same build (o0_same) that did not start well (o0_bad: the
// bootloader or a rollback marked it invalid): a new copy fails the same way, and the board restarts for ever.
bool ota_sig_copy_again(bool o0_bad, bool o0_same);
