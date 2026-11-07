#pragma once

// Home Assistant: the client of the HOME app and of the board web page (web_ha.c).
// - The settings (NVS "ha"): the address of HA (http:// or https://), the long-lived token, and
//   the chosen entities (their order is the order of the tiles). The token never leaves the board:
//   no API gives it back, and nothing logs it.
// - The board asks HA with its template API (POST /api/template): HA sends back short text lines
//   (one entity on each line, fields with '|'), not the big state list. Actions: POST
//   /api/services/<domain>/<service>.
// All calls block (a few 100 ms on the LAN): call them from a task, not from the drawing loop.

// An extra (CONFIG_TFN_EXTRA_HA): when it is off, ha_init() is empty and the other functions are not there.
#include <stdbool.h>
#include <stdint.h>
#include "tfn_extras.h"

#define HA_MAX      24  // the chosen entities
#define HA_ID_LEN   64
#define HA_NAME_LEN 32

typedef struct {
    char id[HA_ID_LEN];      // light.living_room
    char name[HA_NAME_LEN];  // Living Room
} ha_item_t;

#if CONFIG_TFN_EXTRA_HA
void ha_init(void);  // reads NVS
#else
static inline void ha_init(void)
{
}
#endif
const char *ha_url(void);  // "" = not set
bool ha_token_set(void);
// url: "" keeps it; token: "" keeps it. Returns false for a bad address.
bool ha_set_config(const char *url, const char *token);
int ha_items(ha_item_t *out, int max);             // a copy; returns how many
void ha_set_items(const ha_item_t *items, int n);  // saves
bool ha_ready(void);                               // an address and a token

// The calls. They return the HTTP status (200 = good) or -1 (no connection); out gets the body
// (cut at max - 1).
int ha_get_api(char *out, int max);                    // GET /api/: {"message":"API running."}
int ha_template(const char *tpl, char *out, int max);  // POST /api/template
int ha_call(const char *domain, const char *service, const char *entity, const char *extra_json);

// Text that can go into a Jinja string between single quotes (letters, digits, space . _ -).
void ha_clean(char *s);
