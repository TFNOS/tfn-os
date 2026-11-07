#pragma once
// TFN OS: the board side of the store (main/tfn_store.c): the manager store, or the static store (main/tfn_sstore.h).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The apps of one catalog (an app after the last place
// is not in the list, with no message, so the limit must stay above the number of store packages). The users keep the catalog in PSRAM.
#define TFN_CAT_MAX 192

typedef struct {
    char id[24], name[24], ver[16], group[12], emoji[16];
    uint32_t code, bytes, ram;
    uint32_t perms;  // the permission classes of the package (tfn_perms_text)
    uint8_t state;   // TFN_ST_*: the state of the app in the profile of this board
    char *desc;      // the description of the app in the store: "English\0Hebrew\0" in PSRAM, NULL = none
                     // (the code that uses the catalog frees it: tfn_store_cat_free)
} tfn_cat_t;

enum {
    TFN_ST_AVAILABLE,
    TFN_ST_RECOMMENDED,
    TFN_ST_REQUIRED
};

// The description of a catalog app: English (he = false) or Hebrew. When that language has none: the other one. "" = none.
static inline const char *tfn_store_desc(const tfn_cat_t *c, bool he)
{
    if (!c || !c->desc) {
        return "";
    }
    const char *en = c->desc, *h = en + strlen(en) + 1;
    if (he) {
        return h[0] ? h : en;
    }
    return en[0] ? en : h;
}

// Free the descriptions of n catalog apps.
static inline void tfn_store_cat_free(tfn_cat_t *c, int n)
{
    for (int i = 0; i < n; i++) {
        free(c[i].desc);
        c[i].desc = NULL;
    }
}

void tfn_store_init(void);
// The manager config (op=mgr of /api/apps): its addresses on the LAN and on the VPN, the token of this board and the
// apps that the profile requires. The first manager sets the token; after that a change needs it (auth).
bool tfn_store_set_mgr(const char *lan, const char *vpn, const char *tok, const char *auth, const char *req, char *msg,
                       int len);
bool tfn_store_configured(void);
bool tfn_store_tok_ok(const char *tok);         // true when no token is set yet, or it is the token
bool tfn_store_required(const char *id);        // the profile requires it: the board does not remove it
int tfn_store_required_json(char *o, int max);  // ["a","b"]
const char *tfn_store_profile(void);            // the profile name of the last catalog ("" = none yet)
// The catalog of this board (the apps of its profile and its board kind), from the manager on the LAN or the VPN, or
// from the static store (tfn_sstore_active).
// Returns the count (-1 = no answer: msg says why). The app task or a worker task (it waits for the network).
// Each app with a description gets c->desc (free it with tfn_store_cat_free).
int tfn_store_catalog(tfn_cat_t *out, int max, char *msg, int len);
// Download the files of an app from the source of the catalog and install them (tfn_install). progress(0..100) can be NULL.
bool tfn_store_install(const char *id, int place, void (*progress)(int pct), char *msg, int len);
// The icon.bin of a store app in PSRAM (free() it), or NULL.
uint8_t *tfn_store_icon(const char *id, size_t *len);
// Tests: only the VPN address of the manager (the console "apps vpn 1").
void tfn_store_vpn_only(bool on);
