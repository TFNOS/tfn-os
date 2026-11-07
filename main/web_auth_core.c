// THE WEB PASSWORD, the plain part: see web_auth_core.h. No ESP-IDF here (the host test compiles it alone); the caller
// (main/web_auth.c) holds its lock around these calls.
#include "web_auth_core.h"
#include <stdio.h>
#include <string.h>

bool wa_eq(const void *a, const void *b, size_t n)
{
    const volatile uint8_t *x = a, *y = b;
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d |= (uint8_t)(x[i] ^ y[i]);
    }
    return d == 0;
}

// ---------------------------------------------------------------- the sessions

typedef struct {
    char tok[WA_TOKEN_LEN + 1];
    int64_t born, used;
} wa_sess_t;
static wa_sess_t s_sess[WA_SESS_MAX];

static bool live(const wa_sess_t *s, int64_t now)
{
    return s->tok[0] && now - s->used < WA_IDLE_MS && now - s->born < WA_LIFE_MS;
}

void wa_sess_new(const uint8_t rnd[16], int64_t now_ms, char *out)
{
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[2 * i] = HEX[rnd[i] >> 4];
        out[2 * i + 1] = HEX[rnd[i] & 15];
    }
    out[WA_TOKEN_LEN] = 0;
    // a free place (or an ended session), else the session that was used the longest time ago
    int k = 0;
    for (int i = 0; i < WA_SESS_MAX; i++) {
        if (!live(&s_sess[i], now_ms)) {
            k = i;
            break;
        }
        if (s_sess[i].used < s_sess[k].used) {
            k = i;
        }
    }
    memcpy(s_sess[k].tok, out, WA_TOKEN_LEN + 1);
    s_sess[k].born = s_sess[k].used = now_ms;
}

bool wa_sess_ok(const char *token, int64_t now_ms)
{
    if (!token || strlen(token) != WA_TOKEN_LEN) {
        return false;
    }
    bool ok = false;
    for (int i = 0; i < WA_SESS_MAX; i++) {  // (each place is compared: no early end)
        bool hit = s_sess[i].tok[0] && wa_eq(s_sess[i].tok, token, WA_TOKEN_LEN);
        if (hit && live(&s_sess[i], now_ms)) {
            s_sess[i].used = now_ms;
            ok = true;
        } else if (hit) {
            memset(&s_sess[i], 0, sizeof s_sess[i]);  // (an ended session goes)
        }
    }
    return ok;
}

void wa_sess_drop(const char *token)
{
    if (!token || strlen(token) != WA_TOKEN_LEN) {
        return;
    }
    for (int i = 0; i < WA_SESS_MAX; i++) {
        if (s_sess[i].tok[0] && wa_eq(s_sess[i].tok, token, WA_TOKEN_LEN)) {
            memset(&s_sess[i], 0, sizeof s_sess[i]);
        }
    }
}

void wa_sess_clear(void)
{
    memset(s_sess, 0, sizeof s_sess);
}

int wa_sess_count(int64_t now_ms)
{
    int n = 0;
    for (int i = 0; i < WA_SESS_MAX; i++) {
        n += live(&s_sess[i], now_ms);
    }
    return n;
}

// ---------------------------------------------------------------- the limit of the tries

typedef struct {
    uint32_t ip;
    int fails;
    int64_t last;
    bool used;
} wa_try_t;
static wa_try_t s_try[WA_IP_MAX];

static wa_try_t *try_of(uint32_t ip, bool make, int64_t now)
{
    int k = -1;
    for (int i = 0; i < WA_IP_MAX; i++) {
        if (s_try[i].used && s_try[i].ip == ip) {
            return &s_try[i];
        }
    }
    if (!make) {
        return NULL;
    }
    for (int i = 0; i < WA_IP_MAX; i++) {  // a free place, else the address with the oldest try
        if (!s_try[i].used) {
            k = i;
            break;
        }
        if (k < 0 || s_try[i].last < s_try[k].last) {
            k = i;
        }
    }
    s_try[k] = (wa_try_t){.ip = ip, .fails = 0, .last = now, .used = true};
    return &s_try[k];
}

static int64_t wait_of(const wa_try_t *t)
{
    if (t->fails < WA_FREE_TRIES) {
        return 0;
    }
    int64_t w = WA_WAIT0_MS;
    for (int i = WA_FREE_TRIES; i < t->fails && w < WA_WAIT_MAX_MS; i++) {
        w *= 2;
    }
    return w > WA_WAIT_MAX_MS ? WA_WAIT_MAX_MS : w;
}

int64_t wa_wait_ms(uint32_t ip, int64_t now_ms)
{
    const wa_try_t *t = try_of(ip, false, now_ms);
    if (!t) {
        return 0;
    }
    int64_t left = t->last + wait_of(t) - now_ms;
    return left > 0 ? left : 0;
}

void wa_fail(uint32_t ip, int64_t now_ms)
{
    wa_try_t *t = try_of(ip, true, now_ms);
    t->fails++;
    t->last = now_ms;
}

void wa_pass(uint32_t ip)
{
    wa_try_t *t = try_of(ip, false, 0);
    if (t) {
        memset(t, 0, sizeof *t);
    }
}

void wa_tries_clear(void)
{
    memset(s_try, 0, sizeof s_try);
}

// ---------------------------------------------------------------- the parse

static int b64v(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

bool wa_basic_password(const char *h, char *out, size_t max)
{
    if (!h || !out || max == 0) {
        return false;
    }
    out[0] = 0;
    while (*h == ' ') {
        h++;
    }
    if (strncmp(h, "Basic ", 6) && strncmp(h, "basic ", 6)) {
        return false;
    }
    h += 6;
    while (*h == ' ') {
        h++;
    }
    char dec[160];
    size_t n = 0;
    unsigned acc = 0;
    int bits = 0;
    for (; *h && *h != '=' && *h != ' '; h++) {
        int v = b64v(*h);
        if (v < 0) {
            return false;
        }
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n + 1 >= sizeof dec) {
                return false;
            }
            dec[n++] = (char)((acc >> bits) & 0xFF);
        }
    }
    dec[n] = 0;
    if (strlen(dec) != n) {
        return false;  // (a 0 byte inside)
    }
    const char *colon = strchr(dec, ':');
    if (!colon) {
        return false;
    }
    size_t pl = n - (size_t)(colon + 1 - dec);
    if (pl + 1 > max) {
        return false;
    }
    memcpy(out, colon + 1, pl + 1);
    memset(dec, 0, sizeof dec);
    return true;
}

void wa_next_path(const char *next, char *out, size_t max)
{
    bool ok = next && next[0] == '/' && next[1] != '/' && next[1] != '\\' && strlen(next) < max;
    for (const char *p = next; ok && *p; p++) {
        if ((unsigned char)*p < 32 || *p == 127) {
            ok = false;
        }
    }
    if (ok && strncmp(next, "/login", 6) == 0) {
        ok = false;  // (no loop back to the login)
    }
    if (max) {
        snprintf(out, max, "%s", ok ? next : "/");
    }
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool wa_form_field(const char *body, const char *name, char *out, size_t max)
{
    if (!body || !name || !out || max == 0) {
        return false;
    }
    size_t nl = strlen(name);
    for (const char *p = body; *p;) {
        const char *amp = strchr(p, '&'), *end = amp ? amp : p + strlen(p);
        if ((size_t)(end - p) > nl && !strncmp(p, name, nl) && p[nl] == '=') {
            size_t n = 0;
            for (const char *q = p + nl + 1; q < end; q++) {
                char c = *q;
                if (c == '+') {
                    c = ' ';
                } else if (c == '%' && q + 2 < end && hexv(q[1]) >= 0 && hexv(q[2]) >= 0) {
                    c = (char)(hexv(q[1]) * 16 + hexv(q[2]));
                    q += 2;
                }
                if (n + 1 >= max) {
                    return false;
                }
                out[n++] = c;
            }
            out[n] = 0;
            return true;
        }
        if (!amp) {
            break;
        }
        p = amp + 1;
    }
    out[0] = 0;
    return false;
}
