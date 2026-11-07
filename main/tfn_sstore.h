#pragma once
// TFN OS: the static store. A web host with no server code (GitHub Pages of the public release) keeps a signed index
// and the app files; sdk/tools/store_index.py makes them:
//   <url>index.json                 the list of the apps (the form "tfn_store": 1)
//   <url>index.sig                  64 bytes: the ECDSA P-256 / SHA-256 signature (r || s) of index.json
//   <url>apps/<id>/<kind>/<file>    the files of each package, as the install takes them
// A trusted STORE key of the board must sign the index: a built-in key (main/tfn_defaults.h) or an added key with the
// flag STORE (SETUP > TRUSTED PUBLISHERS, main/tfn_pub.h). The index gives the size and the SHA-256 of each file,
// and the install checks the file "sig" of the package again (main/tfn_scan.c), as for the manager store.
//
// The source of the store (NVS namespace "tfn": "ssrc", "surl"): AUTO = the manager when the board knows a manager
// (main/tfn_store.c, a board with a LAN manager), else the static store; MGR = the manager only; STATIC = the static store
// only. With no setting: TFN_STORE_SOURCE_DEFAULT and TFN_STATIC_STORE_URL (main/tfn_defaults.h).
//
// main/tfn_sstore_core.c has the parts with no ESP-IDF call (the URL check, the signature check of the index, the
// parser of index.json, the check of a file); tests/unit/test_sstore.c tests them. main/tfn_sstore.c has the setting,
// the HTTPS requests (a short task with its stack in PSRAM), the catalog, the install and the icons. All memory of
// the static store is in PSRAM.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tfn_pub.h"
#include "tfn_store.h"

#define TFN_SS_FORMAT    1                  // the form of index.json ("tfn_store")
#define TFN_SS_NFILES    5                  // the files of a package (TFN_SS_FILE_NAMES)
#define TFN_SS_URL       160                // the longest URL of a store (with the end)
#define TFN_SS_INDEX_MAX (2 * 1024 * 1024)  // the biggest index.json that the board reads
#define TFN_SS_FILE_MAX  (4 * 1024 * 1024)  // the biggest file of a package that the board reads

// the names of the files of a package, in the order of the install (sdk/tools/store_index.py FILES)
#define TFN_SS_FILE_NAMES {"manifest.txt", "app.elf", "icon.bin", "sig", "watch.elf"}
enum {
    TFN_SS_F_MANIFEST,
    TFN_SS_F_ELF,
    TFN_SS_F_ICON,
    TFN_SS_F_SIG,
    TFN_SS_F_WATCH
};

// the source of the store
enum {
    TFN_SS_AUTO,
    TFN_SS_MGR,
    TFN_SS_STATIC
};

typedef struct {
    uint32_t size;  // 0 = the package has no such file
    uint8_t sha[32];
} tfn_ss_file_t;

// the files of one package of this board kind (from the last catalog)
typedef struct {
    char id[24];
    tfn_ss_file_t f[TFN_SS_NFILES];
} tfn_ss_pkg_t;

// what the parser needs from the board
typedef struct {
    const char *kind;  // the board kind (tfn_kind)
    int abi, rev;      // the ABI of the board (tfn_abi, tfn_abi_rev): a package of another ABI or a newer rev is out
    // the descriptions of an app ("English\0Hebrew\0"): the board gives PSRAM
    void *(*alloc)(size_t n);
    // the permission classes of a list "a,b" (tfn_perms_parse); unknown = a class of a newer firmware
    uint32_t (*perms)(const char *list, bool *unknown);
} tfn_ss_opts_t;

// ---------------------------------------------------------------- the core (tfn_sstore_core.c)
// an HTTPS URL of a store: "https://", no space, no quote, no back slash, no '?' or '#'; false = not good
bool tfn_ss_url_ok(const char *u);
// base + rel into out (one '/' between them); false = too long
bool tfn_ss_join(char *out, size_t max, const char *base, const char *rel);
// the trusted STORE key that signed the index (the built-in keys, then the added keys with the flag STORE): the result
// TFN_PUB_OK_BUILTIN, TFN_PUB_OK_STORE or TFN_PUB_NONE; pk = the key, who = the name of an added key
int tfn_ss_index_key(const tfn_pub_ops_t *o, const tfn_pub_list_t *l, const uint8_t *msg, size_t n, const uint8_t *sig,
                     size_t sl, uint8_t pk[TFN_PUB_PK], char who[TFN_PUB_NAME]);
// Parse index.json (after the signature check) into out and pkg (max places each). It changes the text (the strings
// get their real bytes in place). pk = the key that signed it: the index must name the same key. Returns the number
// of apps of this board kind, or -1 (msg says why). Free the descriptions with tfn_store_cat_free.
int tfn_ss_parse(char *j, size_t n, const tfn_ss_opts_t *op, const uint8_t pk[TFN_PUB_PK], tfn_cat_t *out,
                 tfn_ss_pkg_t *pkg, int max, char *msg, int len);
// the size and the SHA-256 of a file are the values of the index
bool tfn_ss_file_ok(const tfn_pub_ops_t *o, const tfn_ss_file_t *want, const uint8_t *d, size_t n);
// the name of file i of a package (TFN_SS_FILE_NAMES)
const char *tfn_ss_file_name(int i);

// ---------------------------------------------------------------- the board (tfn_sstore.c)
int tfn_sstore_mode(void);         // TFN_SS_AUTO, TFN_SS_MGR or TFN_SS_STATIC
const char *tfn_sstore_url(void);  // the URL of the static store (with a '/' at the end)
bool tfn_sstore_active(void);      // the store of this board is the static store now
// the source and the URL (url NULL or "" = the default URL); false = not good (msg says why)
bool tfn_sstore_set(int mode, const char *url, char *msg, int len);
// the catalog, the install and the icon of tfn_store.h, from the static store
int tfn_sstore_catalog(tfn_cat_t *out, int max, char *msg, int len);
bool tfn_sstore_install(const char *id, int place, void (*progress)(int pct), char *msg, int len);
uint8_t *tfn_sstore_icon(const char *id, size_t *len);
