#pragma once
// THE WEB PASSWORD, the plain part (no ESP-IDF): the sessions of the logged-in browsers, the limit of the tries, the
// parse of an HTTP Basic header and of the page after the login. main/web_auth.c is the rest (the password in NVS,
// the gate of each web handler, the pages). The host test tests/web_auth/ compiles this file alone.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WA_TOKEN_LEN   32                         // a session: 16 random bytes as 32 hex digits (the cookie tfn_s)
#define WA_SESS_MAX    8                          // the newest sessions; a 9th login drops the oldest one
#define WA_IDLE_MS     (24LL * 3600 * 1000)       // a session ends after a day with no request
#define WA_LIFE_MS     (30LL * 24 * 3600 * 1000)  // and 30 days after the login
#define WA_FREE_TRIES  5                          // wrong passwords in a row from one address before the wait
#define WA_WAIT0_MS    30000                      // the first wait; each next wrong password doubles it
#define WA_WAIT_MAX_MS (15 * 60 * 1000)
#define WA_IP_MAX      8  // the addresses that the limit remembers (the oldest goes)

// a == b for n bytes, in a time that does not depend on the bytes
bool wa_eq(const void *a, const void *b, size_t n);

// The sessions. new: a token from 16 random bytes (out: WA_TOKEN_LEN + 1). ok: the token is a session now (and its
// idle time starts again). drop: the token is no session. clear: no sessions (a new password).
void wa_sess_new(const uint8_t rnd[16], int64_t now_ms, char *out);
bool wa_sess_ok(const char *token, int64_t now_ms);
void wa_sess_drop(const char *token);
void wa_sess_clear(void);
int wa_sess_count(int64_t now_ms);

// The limit of the tries, for each client address (ip: 4 bytes of IPv4, or a hash of an IPv6 address).
// wait: 0 = the address can try now, else the ms to wait. fail / pass: the result of a try.
int64_t wa_wait_ms(uint32_t ip, int64_t now_ms);
void wa_fail(uint32_t ip, int64_t now_ms);
void wa_pass(uint32_t ip);
void wa_tries_clear(void);

// The password of an "Authorization: Basic <base64 of user:password>" header (the name is not used): true and
// the password in out (max with the end 0). False: not a Basic header, or a bad one.
bool wa_basic_password(const char *header, char *out, size_t max);

// The page to go to after the login (the field next of the form): only a path of this board ("/x", not "//host",
// no control characters). out = "/" when it is not one.
void wa_next_path(const char *next, char *out, size_t max);

// The value of a field of an application/x-www-form-urlencoded body (with the %xx and '+' decoded): true when found.
bool wa_form_field(const char *body, const char *name, char *out, size_t max);
