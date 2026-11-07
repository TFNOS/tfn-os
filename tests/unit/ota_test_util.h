#pragma once

// The shared parts of the unit tests of the signed OTA (test_ota_sig.c, test_ota_flow.c): the host crypto as
// ota_sig_crypto_t, test keys, a small image with a board tag, a signed trailer, and the reader of the vector folder
// that tests/unit/test_ota_sig.py makes (the files that sdk/tools/fw_sign.py signed).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_crypto.h"
#include "ota_sig.h"

static int g_fails;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            g_fails++;                                                                                                 \
            printf("FAIL %d: ", __LINE__);                                                                             \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

static bool tu_sha256(const uint8_t *d, size_t n, uint8_t out[32])
{
    hc_sha256(d, n, out);
    return true;
}

static int g_verifies;  // the number of signature checks (the test of the key lookup)
static bool tu_verify(const uint8_t pub[65], const uint8_t *msg, size_t n, const uint8_t sig[64])
{
    g_verifies++;
    return hc_p256_verify(pub, msg, n, sig);
}

static const ota_sig_crypto_t TU_CRYPTO = {.sha256 = tu_sha256, .verify = tu_verify};

// test key i: the private scalar = SHA-256("tfn test key" i), its public point
static void tu_key(int i, uint8_t d[32], uint8_t pub[65])
{
    char t[32];
    snprintf(t, sizeof t, "tfn test key %d", i);
    hc_sha256((const uint8_t *)t, strlen(t), d);
    if (!hc_p256_pub(d, pub)) {
        printf("FAIL: test key %d\n", i);
        exit(1);
    }
}

// a small image: 0xE9, the version at 0x30, the tag "@ARCADE-BOARD:<kind>@" at tag_at, other bytes with no '@'
static uint8_t *tu_image(int len, const char *kind, const char *ver, int tag_at, uint32_t seed)
{
    uint8_t *b = malloc((size_t)len + OTA_SIG_TRAILER);
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        uint8_t v = (uint8_t)(seed >> 16);
        b[i] = v == '@' ? 'A' : v;
    }
    b[0] = 0xE9;
    memset(b + 0x30, 0, 32);
    memcpy(b + 0x30, ver, strlen(ver));
    if (kind) {
        char tag[40];
        int n = snprintf(tag, sizeof tag, "@ARCADE-BOARD:%s@", kind);
        memcpy(b + tag_at, tag, (size_t)n);
    }
    return b;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

// the trailer of img[0, len) at tr: signed by the test keys keys[0, nsig) (with -1: a key that is not a test key)
static void tu_trailer(uint8_t *tr, const uint8_t *img, int len, const char *kind, const char *ver, uint32_t sv,
                       int nsig, const int *keys)
{
    memset(tr, 0, OTA_SIG_TRAILER);
    memcpy(tr, "TFNOTA1", 8);
    tr[8] = 1;
    put32(tr + 12, (uint32_t)len);
    hc_sha256(img, (size_t)len, tr + 16);
    memcpy(tr + 48, kind, strlen(kind));
    memcpy(tr + 64, ver, strlen(ver));
    put32(tr + 96, sv);
    put32(tr + 100, 1700000000u);
    tr[108] = (uint8_t)nsig;
    for (int i = 0; i < nsig && i < OTA_SIG_MAX; i++) {
        uint8_t d[32], pub[65], id[32], k[32], seed[33];
        tu_key(keys[i] < 0 ? 99 : keys[i], d, pub);
        hc_sha256(pub, 65, id);
        memcpy(seed, d, 32);
        seed[32] = (uint8_t)i;
        hc_sha256(seed, 33, k);  // (a nonce for a test: fixed from the key)
        uint8_t *s = tr + 128 + 72 * i;
        memcpy(s, id, 8);
        if (!hc_p256_sign(d, k, tr, 128, s + 8)) {
            printf("FAIL: sign\n");
            exit(1);
        }
    }
}

// ---------------------------------------------------------------- the vector folder of test_ota_sig.py
typedef struct {
    char file[64], kind[16], code[16];
    unsigned minsv;
} tu_case_t;

static int hexval(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// keys.txt: the key list of the board (4 lines: 130 hex digits or "-")
static bool tu_load_keys(const char *dir, uint8_t keys[OTA_SIG_KEYS][65])
{
    char p[512], line[300];
    snprintf(p, sizeof p, "%.400s/keys.txt", dir);
    FILE *f = fopen(p, "r");
    if (!f) {
        return false;
    }
    memset(keys, 0, OTA_SIG_KEYS * 65);
    int k = 0;
    while (k < OTA_SIG_KEYS && fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            continue;
        }
        for (int i = 0; i < 65 && line[0] != '-'; i++) {
            int a = hexval(line[2 * i]), b = hexval(line[2 * i + 1]);
            if (a < 0 || b < 0) {
                fclose(f);
                return false;
            }
            keys[k][i] = (uint8_t)(a * 16 + b);
        }
        k++;
    }
    fclose(f);
    return k == OTA_SIG_KEYS;
}

// cases.txt: one case on each line (the count; up to max)
static int tu_load_cases(const char *dir, tu_case_t *c, int max)
{
    char p[512], line[300];
    snprintf(p, sizeof p, "%.400s/cases.txt", dir);
    FILE *f = fopen(p, "r");
    if (!f) {
        return -1;
    }
    int n = 0;
    while (n < max && fgets(line, sizeof line, f)) {
        if (line[0] == '#') {
            continue;
        }
        if (sscanf(line, "%63s %15s %u %15s", c[n].file, c[n].kind, &c[n].minsv, c[n].code) == 4) {
            n++;
        }
    }
    fclose(f);
    return n;
}

// a whole file (the length in *len)
static uint8_t *tu_read(const char *dir, const char *name, long *len)
{
    char p[512];
    snprintf(p, sizeof p, "%.400s/%.63s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)*len + 1);
    if (fread(b, 1, (size_t)*len, f) != (size_t)*len) {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}
