#pragma once
// Bounded authorization state, independent of the chip and crypto library.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DG_CHALLENGE_BYTES 32
#define DG_CHALLENGE_MS    10000u
#define DG_USB_IDLE_MS     60000u
#define DG_USB_MAX_MS      900000u
#define DG_LOCAL_MS        120000u
typedef struct {
    uint8_t nonce[DG_CHALLENGE_BYTES];
    uint64_t challenge_until, retry_after, usb_started, usb_last, local_until;
    unsigned failures;
    bool pending, usb;
} dg_state_t;
static inline void dg_lock(dg_state_t *s)
{
    unsigned failures = s->failures;
    uint64_t retry = s->retry_after;
    memset(s, 0, sizeof *s);
    s->failures = failures;
    s->retry_after = retry;
}
static inline bool dg_challenge(dg_state_t *s, const uint8_t nonce[DG_CHALLENGE_BYTES], uint64_t now)
{
    if (now < s->retry_after) {
        return false;
    }
    memcpy(s->nonce, nonce, DG_CHALLENGE_BYTES);
    s->challenge_until = now + DG_CHALLENGE_MS;
    s->pending = true;
    return true;
}
// Taking a challenge consumes it before signature verification, also on failure.
static inline bool dg_take(dg_state_t *s, uint64_t now, uint8_t nonce[DG_CHALLENGE_BYTES])
{
    bool valid = s->pending && now < s->challenge_until && now >= s->retry_after;
    s->pending = false;
    if (valid) {
        memcpy(nonce, s->nonce, DG_CHALLENGE_BYTES);
    }
    memset(s->nonce, 0, sizeof s->nonce);
    return valid;
}
static inline void dg_result(dg_state_t *s, bool success, uint64_t now)
{
    s->usb = success;
    if (success) {
        s->failures = 0;
        s->retry_after = 0;
        s->usb_started = s->usb_last = now;
    } else {
        if (s->failures < 5) {
            s->failures++;
        }
        s->retry_after = now + (1000u << (s->failures - 1));
    }
}
static inline bool dg_usb(dg_state_t *s, bool connected, uint64_t now)
{
    if (!connected || !s->usb || now < s->usb_started || now - s->usb_started >= DG_USB_MAX_MS ||
        now - s->usb_last >= DG_USB_IDLE_MS) {
        s->usb = false;
        return false;
    }
    s->usb_last = now;
    return true;
}
static inline bool dg_local(const dg_state_t *s, uint64_t now)
{
    return s->local_until && now < s->local_until;
}
static inline bool dg_public_command(const char *cmd, const char *args)
{
    if (args && args[0]) {
        return false;
    }
    return !strcmp(cmd, "ota") || !strcmp(cmd, "webpass") || !strcmp(cmd, "df") || !strcmp(cmd, "wake") ||
           !strcmp(cmd, "help");
}
static inline bool dg_private_path(const char *path)
{
    return path && !strncmp(path, "/data", 5) && (!path[5] || path[5] == '/');
}
static inline bool dg_maintenance_path(const char *hex, char *path, size_t cap)
{
    if (!hex) {
        return false;
    }
    size_t n = strlen(hex);
    if (!n || (n & 1) || n / 2 >= cap) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned c = (uint8_t)hex[i], v = c >= '0' && c <= '9'   ? c - '0'
                                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                 : 16;
        if (v == 16) {
            return false;
        }
        if (!(i & 1)) {
            path[i / 2] = (char)(v << 4);
        } else {
            path[i / 2] = (char)((uint8_t)path[i / 2] | v);
        }
    }
    n /= 2;
    path[n] = 0;
    for (size_t i = 0; i < n; i++) {
        if ((uint8_t)path[i] < 32 || path[i] == 127 || path[i] == '\\' || path[i] == ':') {
            return false;
        }
    }
    if (!(dg_private_path(path) || (!strncmp(path, "/sdcard", 7) && (!path[7] || path[7] == '/')))) {
        return false;
    }
    for (size_t begin = 1; begin < n;) {
        size_t end = begin;
        while (end < n && path[end] != '/') {
            end++;
        }
        if (end == begin || (end - begin == 1 && path[begin] == '.') ||
            (end - begin == 2 && path[begin] == '.' && path[begin + 1] == '.')) {
            return false;
        }
        begin = end + 1;
    }
    if (n > 1 && path[n - 1] == '/') {
        path[n - 1] = 0;
    }
    return true;
}
