// The unit test of the pool service (main/tfn_pool_flow.c) with a fake NVS, a fake roster file, fake time and the host
// crypto: the JOIN with the 6 digits, the token JOIN (only in a LEGACY build, only with no pool), a wrong commitment, a
// wrong signature of the manager, ALLOW with wrong digits, the time limits, a second JOIN while one runs, the request
// header, op=set (a new version, an old version, a foreign key, a removal), op=key with the box key, the previous key
// hour, the v4 frames, the UDP tails, the beacons, LEAVE, a restart and a damaged file.
// The board (B1) and a second member (B2) each have their own fake store.
// Build and run: python tests/unit/run.py
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_crypto.h"
#include "tfn_pool_flow.h"

static int s_fails, s_checks;

static void check(bool ok, const char *fmt, ...)
{
    s_checks++;
    if (!ok) {
        va_list ap;
        va_start(ap, fmt);
        printf("FAIL ");
        vprintf(fmt, ap);
        printf("\n");
        va_end(ap);
        s_fails++;
    }
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32le(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

// ---------------------------------------------------------------- the host crypto that host_crypto.c does not have

static void hmac256(const uint8_t *key, size_t kn, const uint8_t *m, size_t n, uint8_t out[32])
{
    uint8_t k[64] = {0}, ih[32];
    if (kn > 64) {
        hc_sha256(key, kn, k);
    } else {
        memcpy(k, key, kn);
    }
    uint8_t *b = malloc(64 + (n > 32 ? n : 32));
    for (int i = 0; i < 64; i++) {
        b[i] = k[i] ^ 0x36;
    }
    memcpy(b + 64, m, n);
    hc_sha256(b, 64 + n, ih);
    for (int i = 0; i < 64; i++) {
        b[i] = k[i] ^ 0x5c;
    }
    memcpy(b + 64, ih, 32);
    hc_sha256(b, 96, out);
    free(b);
}

// X25519 (RFC 7748) in 16 limbs of 16 bits
typedef int64_t gf[16];

static void car(gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += 65536;
        const int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}

static void sel(gf p, gf q, int b)
{
    const int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        const int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack(uint8_t *o, const gf n)
{
    gf m, t;
    memcpy(t, n, sizeof t);
    car(t);
    car(t);
    car(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void fadd(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; i++) {
        o[i] = a[i] + b[i];
    }
}

static void fsub(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; i++) {
        o[i] = a[i] - b[i];
    }
}

static void fmul(gf o, const gf a, const gf b)
{
    int64_t t[31] = {0};
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (int i = 0; i < 15; i++) {
        t[i] += 38 * t[i + 16];
    }
    memcpy(o, t, 16 * sizeof t[0]);
    car(o);
    car(o);
}

static void finv(gf o, const gf in)
{
    gf c;
    memcpy(c, in, sizeof c);
    for (int a = 253; a >= 0; a--) {
        fmul(c, c, c);
        if (a != 2 && a != 4) {
            fmul(c, c, in);
        }
    }
    memcpy(o, c, sizeof c);
}

static void x25519_raw(uint8_t q[32], const uint8_t n[32], const uint8_t pt[32])
{
    static const gf K121665 = {0xDB41, 1};
    uint8_t z[32];
    gf x, a = {1}, b, c = {0}, d = {1}, e, f;
    memcpy(z, n, 32);
    z[31] = (uint8_t)((n[31] & 127) | 64);
    z[0] &= 248;
    for (int i = 0; i < 16; i++) {
        x[i] = pt[2 * i] + ((int64_t)pt[2 * i + 1] << 8);
    }
    x[15] &= 0x7fff;
    memcpy(b, x, sizeof b);
    for (int i = 254; i >= 0; --i) {
        const int r = (z[i >> 3] >> (i & 7)) & 1;
        sel(a, b, r);
        sel(c, d, r);
        fadd(e, a, c);
        fsub(a, a, c);
        fadd(c, b, d);
        fsub(b, b, d);
        fmul(d, e, e);
        fmul(f, a, a);
        fmul(a, c, a);
        fmul(c, b, e);
        fadd(e, a, c);
        fsub(a, a, c);
        fmul(b, a, a);
        fsub(c, d, f);
        fmul(a, c, K121665);
        fadd(a, a, d);
        fmul(c, c, a);
        fmul(a, d, f);
        fmul(d, b, x);
        fmul(b, e, e);
        sel(a, b, r);
        sel(c, d, r);
    }
    finv(c, c);
    fmul(a, a, c);
    pack(q, a);
}

static bool x25519_pub(const uint8_t sk[32], uint8_t pk[32])
{
    static const uint8_t BASE[32] = {9};
    x25519_raw(pk, sk, BASE);
    return true;
}

static bool x25519(const uint8_t sk[32], const uint8_t peer[32], uint8_t out[32])
{
    x25519_raw(out, sk, peer);
    return true;
}

// ChaCha20 (RFC 8439)
#define QR(a, b, c, d)                                                                                                 \
    do {                                                                                                               \
        a += b;                                                                                                        \
        d ^= a;                                                                                                        \
        d = d << 16 | d >> 16;                                                                                         \
        c += d;                                                                                                        \
        b ^= c;                                                                                                        \
        b = b << 12 | b >> 20;                                                                                         \
        a += b;                                                                                                        \
        d ^= a;                                                                                                        \
        d = d << 8 | d >> 24;                                                                                          \
        c += d;                                                                                                        \
        b ^= c;                                                                                                        \
        b = b << 7 | b >> 25;                                                                                          \
    } while (0)

static void chacha_block(const uint8_t key[32], uint32_t ctr, const uint8_t nonce[12], uint8_t out[64])
{
    uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574}, x[16];
    for (int i = 0; i < 8; i++) {
        s[4 + i] = rd32le(key + 4 * i);
    }
    s[12] = ctr;
    for (int i = 0; i < 3; i++) {
        s[13 + i] = rd32le(nonce + 4 * i);
    }
    memcpy(x, s, sizeof x);
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[8], x[12]);
        QR(x[1], x[5], x[9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8], x[13]);
        QR(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; i++) {
        wr32le(out + 4 * i, x[i] + s[i]);
    }
}

// Poly1305 (RFC 8439) in 5 limbs of 26 bits
static void poly1305(const uint8_t key[32], const uint8_t *m, size_t n, uint8_t tag[16])
{
    const uint32_t M26 = 0x3ffffff;
    const uint32_t r0 = rd32le(key) & 0x3ffffff, r1 = (rd32le(key + 3) >> 2) & 0x3ffff03;
    const uint32_t r2 = (rd32le(key + 6) >> 4) & 0x3ffc0ff, r3 = (rd32le(key + 9) >> 6) & 0x3f03fff;
    const uint32_t r4 = (rd32le(key + 12) >> 8) & 0x00fffff;
    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0, c;
    while (n) {
        uint8_t b[17] = {0};
        const size_t l = n < 16 ? n : 16;
        memcpy(b, m, l);
        b[l] = 1;
        h0 += rd32le(b) & M26;
        h1 += (rd32le(b + 3) >> 2) & M26;
        h2 += (rd32le(b + 6) >> 4) & M26;
        h3 += (rd32le(b + 9) >> 6) & M26;
        h4 += (rd32le(b + 12) >> 8) | (uint32_t)b[16] << 24;
        uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
        d1 += d0 >> 26;
        h0 = (uint32_t)d0 & M26;
        d2 += d1 >> 26;
        h1 = (uint32_t)d1 & M26;
        d3 += d2 >> 26;
        h2 = (uint32_t)d2 & M26;
        d4 += d3 >> 26;
        h3 = (uint32_t)d3 & M26;
        c = (uint32_t)(d4 >> 26);
        h4 = (uint32_t)d4 & M26;
        h0 += c * 5;
        h1 += h0 >> 26;
        h0 &= M26;
        m += l;
        n -= l;
    }
    c = h1 >> 26;
    h1 &= M26;
    h2 += c;
    c = h2 >> 26;
    h2 &= M26;
    h3 += c;
    c = h3 >> 26;
    h3 &= M26;
    h4 += c;
    c = h4 >> 26;
    h4 &= M26;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= M26;
    h1 += c;
    uint32_t g0 = h0 + 5, g1, g2, g3, g4;
    c = g0 >> 26;
    g0 &= M26;
    g1 = h1 + c;
    c = g1 >> 26;
    g1 &= M26;
    g2 = h2 + c;
    c = g2 >> 26;
    g2 &= M26;
    g3 = h3 + c;
    c = g3 >> 26;
    g3 &= M26;
    g4 = h4 + c - (1u << 26);
    const uint32_t mask = (g4 >> 31) - 1;  // (all ones: take g = h - p)
    h0 = (h0 & ~mask) | (g0 & mask);
    h1 = (h1 & ~mask) | (g1 & mask);
    h2 = (h2 & ~mask) | (g2 & mask);
    h3 = (h3 & ~mask) | (g3 & mask);
    h4 = (h4 & ~mask) | (g4 & mask);
    uint32_t w[4] = {h0 | h1 << 26, h1 >> 6 | h2 << 20, h2 >> 12 | h3 << 14, h3 >> 18 | h4 << 8};
    uint64_t f = 0;
    for (int i = 0; i < 4; i++) {
        f += (uint64_t)w[i] + rd32le(key + 16 + 4 * i);
        wr32le(tag + 4 * i, (uint32_t)f);
        f >>= 32;
    }
}

static void aead_tag(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *ct,
                     size_t n, uint8_t tag[16])
{
    uint8_t blk[64];
    chacha_block(key, 0, nonce, blk);
    const size_t pa = (an + 15) / 16 * 16, pc = (n + 15) / 16 * 16;
    uint8_t *m = calloc(1, pa + pc + 16);
    memcpy(m, aad, an);
    memcpy(m + pa, ct, n);
    wr32le(m + pa + pc, (uint32_t)an);
    wr32le(m + pa + pc + 8, (uint32_t)n);
    poly1305(blk, m, pa + pc + 16, tag);
    free(m);
}

static void chacha_xor(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *in, size_t n, uint8_t *out)
{
    uint8_t blk[64];
    for (size_t i = 0; i < n; i++) {
        if (!(i & 63)) {
            chacha_block(key, (uint32_t)(1 + i / 64), nonce, blk);
        }
        out[i] = in[i] ^ blk[i & 63];
    }
}

static bool aead_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *in,
                      size_t n, uint8_t *out)
{
    chacha_xor(key, nonce, in, n, out);
    aead_tag(key, nonce, aad, an, out, n, out + n);
    return true;
}

static bool verify(const uint8_t pk[65], const uint8_t *m, size_t n, const uint8_t sig[64])
{
    return hc_p256_verify(pk, m, n, sig);
}

// A P-256 signature with a nonce from the hash of the key and the message (a test only).
static bool sign_with(const uint8_t d[32], const uint8_t *m, size_t n, uint8_t sig[64])
{
    uint8_t *b = malloc(32 + n), k[32];
    memcpy(b, d, 32);
    memcpy(b + 32, m, n);
    hc_sha256(b, 32 + n, k);
    free(b);
    k[0] &= 0x7f;  // (below the order of the group)
    return hc_p256_sign(d, k, m, n, sig);
}

static void hkdf32(const uint8_t *ikm, size_t in, const uint8_t *salt, size_t sn, const char *info, uint8_t out[32])
{
    uint8_t prk[32], m[64];
    const size_t il = strlen(info);
    hmac256(salt, sn, ikm, in, prk);
    memcpy(m, info, il);
    m[il] = 1;
    hmac256(prk, 32, m, il + 1, out);
}

// ---------------------------------------------------------------- the fake boards

typedef struct {
    char key[8];
    uint8_t v[64];
    size_t n;
    bool on;
} nv_t;

typedef struct {
    uint8_t d[32];  // the device key
    uint8_t bx_sk[32];
    tpf_me_t me;
    nv_t nv[16];
    uint8_t file[TPF_ROSTER_MAX];
    size_t fn;
    bool nvs_full;
    char token[32];
    int mgr_clear;
    char left[32];
    tpf_t p;
} board_t;

static board_t *B;  // the board of the call now
static int64_t s_now = 1000;
static uint32_t s_rng = 12345;
static uint8_t s_rq[128];  // random bytes that the next random() calls give first (the vector test)
static size_t s_rqn, s_rqi;
static uint8_t s_last_msg[256];  // the last message that the device key signed
static size_t s_last_n;
static const uint8_t *s_fixed_sig;  // a signature that sign() gives (the vector test)

static nv_t *nv_find(const char *key, bool make)
{
    for (int i = 0; i < 16; i++) {
        if (B->nv[i].on && !strcmp(B->nv[i].key, key)) {
            return &B->nv[i];
        }
    }
    for (int i = 0; make && i < 16; i++) {
        if (!B->nv[i].on) {
            snprintf(B->nv[i].key, sizeof B->nv[i].key, "%s", key);
            B->nv[i].on = true;
            return &B->nv[i];
        }
    }
    return NULL;
}

static bool f_nvs_get(const char *key, void *p, size_t n)
{
    nv_t *v = nv_find(key, false);
    if (!v || v->n != n) {
        return false;
    }
    memcpy(p, v->v, n);
    return true;
}

static bool f_nvs_set(const char *key, const void *p, size_t n)
{
    nv_t *v = B->nvs_full ? NULL : nv_find(key, true);
    if (!v || n > sizeof v->v) {
        return false;
    }
    memcpy(v->v, p, n);
    v->n = n;
    return true;
}

static void f_nvs_erase(const char *key)
{
    nv_t *v = nv_find(key, false);
    if (v) {
        memset(v, 0, sizeof *v);
    }
}

static bool f_get_u32(const char *key, uint32_t *x)
{
    uint8_t b[4];
    if (!f_nvs_get(key, b, 4)) {
        return false;
    }
    *x = rd32le(b);
    return true;
}

static bool f_set_u32(const char *key, uint32_t x)
{
    uint8_t b[4];
    wr32le(b, x);
    return f_nvs_set(key, b, 4);
}

static size_t f_file_read(uint8_t *p, size_t max)
{
    if (B->fn > max) {
        return 0;
    }
    memcpy(p, B->file, B->fn);
    return B->fn;
}

static bool f_file_write(const uint8_t *p, size_t n)
{
    memcpy(B->file, p, n);
    B->fn = n;
    return true;
}

static void f_file_erase(void)
{
    B->fn = 0;
}

static void f_random(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s_rqi < s_rqn) {
            p[i] = s_rq[s_rqi++];
            continue;
        }
        s_rng ^= s_rng << 13;
        s_rng ^= s_rng >> 17;
        s_rng ^= s_rng << 5;
        p[i] = (uint8_t)s_rng;
    }
}

static int64_t f_now(void)
{
    return s_now;
}

static bool f_sign(const uint8_t *m, size_t n, uint8_t sig[64])
{
    if (n <= sizeof s_last_msg) {
        memcpy(s_last_msg, m, n);
        s_last_n = n;
    }
    if (s_fixed_sig) {
        memcpy(sig, s_fixed_sig, 64);
        return true;
    }
    return sign_with(B->d, m, n, sig);
}

static bool f_token_ok(const char *t)
{
    return B->token[0] && !strcmp(B->token, t);
}

static void f_mgr_clear(void)
{
    B->mgr_clear++;
    B->token[0] = 0;
}

static void f_left(const char *pool)
{
    snprintf(B->left, sizeof B->left, "%s", pool);
}

// The crypto table of the core (tfn_pool_ops_t) on the host copies above
static void ops_sha256(const void *d, size_t n, uint8_t out[32])
{
    hc_sha256(d, n, out);
}

static void ops_hmac(const uint8_t *k, size_t kn, const uint8_t *const *p, const size_t *pn, int np, uint8_t out[32])
{
    size_t n = 0;
    for (int i = 0; i < np; i++) {
        n += pn[i];
    }
    uint8_t *b = malloc(n + 1);
    n = 0;
    for (int i = 0; i < np; i++) {
        memcpy(b + n, p[i], pn[i]);
        n += pn[i];
    }
    hmac256(k, kn, b, n, out);
    free(b);
}

static bool ops_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *pt,
                     size_t n, uint8_t *ct, uint8_t tag[16])
{
    chacha_xor(key, nonce, pt, n, ct);
    aead_tag(key, nonce, aad, an, ct, n, tag);
    return true;
}

static bool ops_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *ct,
                     size_t n, const uint8_t tag[16], uint8_t *pt)
{
    uint8_t t[16];
    aead_tag(key, nonce, aad, an, ct, n, t);
    if (memcmp(t, tag, 16)) {
        return false;
    }
    chacha_xor(key, nonce, ct, n, pt);
    return true;
}

static const tfn_pool_ops_t OPS = {ops_sha256, ops_hmac, verify, ops_seal, ops_open, x25519};

static const tpf_io_t IO = {
        .ops = &OPS,
        .x25519_pub = x25519_pub,
        .random = f_random,
        .now_ms = f_now,
        .sign = f_sign,
        .nvs_get = f_nvs_get,
        .nvs_set = f_nvs_set,
        .nvs_erase = f_nvs_erase,
        .nvs_get_u32 = f_get_u32,
        .nvs_set_u32 = f_set_u32,
        .file_read = f_file_read,
        .file_write = f_file_write,
        .file_erase = f_file_erase,
        .token_ok = f_token_ok,
        .mgr_clear = f_mgr_clear,
        .left = f_left,
};

static void device_id(const uint8_t pk[65], uint8_t id[8])
{
    uint8_t m[71], d[32];
    memcpy(m, "TFNID1", 6);
    memcpy(m + 6, pk, 65);
    hc_sha256(m, sizeof m, d);
    memcpy(id, d, 8);
}

// A board: the device key from the seed, the MAC 02 00 00 00 00 <seed>, a box key in its NVS.
static void board_make(board_t *b, uint8_t seed)
{
    memset(b, 0, sizeof *b);
    B = b;
    memset(b->d, seed, 32);
    hc_p256_pub(b->d, b->me.pk);
    device_id(b->me.pk, b->me.id);
    const uint8_t mac[6] = {2, 0, 0, 0, 0, seed};
    memcpy(b->me.mac, mac, 6);
    snprintf(b->me.name, sizeof b->me.name, "BOARD %u", seed);
    b->me.ok = true;
    memset(b->bx_sk, seed ^ 0x5a, 32);
    x25519_pub(b->bx_sk, b->me.bx);
    uint8_t m[6 + 8 + 32];
    memcpy(m, "TFNBX1", 6);
    memcpy(m + 6, b->me.id, 8);
    memcpy(m + 14, b->me.bx, 32);
    sign_with(b->d, m, sizeof m, b->me.bxs);
    b->me.box = true;
    f_nvs_set("bx", b->bx_sk, 32);
}

static void boot(board_t *b, bool legacy)
{
    B = b;
    tpf_init(&b->p, &IO, &b->me, legacy);
}

// ---------------------------------------------------------------- the fake manager

typedef struct {
    uint8_t id[8], pk[65], mac[6];
    uint8_t role;
    const char *name;
} mem_t;

typedef struct {
    uint8_t d[32], pk[65];
    uint8_t pool[16];
    uint32_t ver, epoch;
    uint8_t key[32];
    uint8_t wk[8][32];  // the request key of each board (by its seed)
    mem_t m[8];
    int n;
    uint64_t ctr;
} mgr_t;

static size_t put(uint8_t *o, size_t k, int t, const void *v, size_t l)
{
    o[k] = (uint8_t)t;
    o[k + 1] = (uint8_t)l;
    o[k + 2] = (uint8_t)(l >> 8);
    memcpy(o + k + 3, v, l);
    return k + 3 + l;
}

// The roster file of the manager m, signed by the key d (pk = its public key, an admin key of the roster or not).
static size_t roster(const mgr_t *m, const uint8_t d[32], const uint8_t pk[65], const uint8_t pool[16], uint32_t ver,
                     uint8_t *o)
{
    uint8_t v[97], kid[32], sig[64];
    memcpy(o, "TFNPOOL1", 8);
    size_t k = 8;
    k = put(o, k, 0x01, pool, 16);
    k = put(o, k, 0x02, "TEST POOL", 9);
    wr32le(v, ver);
    k = put(o, k, 0x03, v, 4);
    wr32le(v, m->epoch);
    k = put(o, k, 0x04, v, 4);
    wr32le(v, 0);
    k = put(o, k, 0x05, v, 4);
    k = put(o, k, 0x06, pk, 65);
    for (int i = 0; i < m->n; i++) {
        memset(v, 0, sizeof v);
        memcpy(v, m->m[i].id, 8);
        memcpy(v + 8, m->m[i].pk, 65);
        memcpy(v + 73, m->m[i].mac, 6);
        v[79] = m->m[i].role;
        memcpy(v + 81, m->m[i].name, strlen(m->m[i].name));
        k = put(o, k, 0x07, v, 97);
    }
    memset(v, 0, 8);
    k = put(o, k, 0x09, v, 8);
    hc_sha256(pk, 65, kid);
    sign_with(d, o, k, sig);
    memcpy(v, kid, 8);
    memcpy(v + 8, sig, 64);
    return put(o, k, 0x7F, v, 72);
}

static size_t mgr_roster(const mgr_t *m, uint8_t *o)
{
    return roster(m, m->d, m->pk, m->pool, m->ver, o);
}

static void mgr_make(mgr_t *m, uint8_t seed)
{
    memset(m, 0, sizeof *m);
    memset(m->d, seed, 32);
    hc_p256_pub(m->d, m->pk);
    memset(m->pool, seed + 1, 16);
    m->ver = 1;
    m->epoch = 1;
    memset(m->key, seed + 2, 32);
    m->ctr = 1730000000000ull;
    mem_t *x = &m->m[m->n++];
    device_id(m->pk, x->id);
    memcpy(x->pk, m->pk, 65);
    const uint8_t mac[6] = {0xFE, 0x4D, 0x47, 0x52, 0x00, 0x01};
    memcpy(x->mac, mac, 6);
    x->role = TFN_POOL_ROLE_MANAGER;
    x->name = "MANAGER";
}

static void mgr_add(mgr_t *m, const board_t *b, uint8_t role)
{
    mem_t *x = &m->m[m->n++];
    memcpy(x->id, b->me.id, 8);
    memcpy(x->pk, b->me.pk, 65);
    memcpy(x->mac, b->me.mac, 6);
    x->role = role;
    x->name = b->me.name;
    memset(m->wk[b->me.mac[5] & 7], 0x40 + b->me.mac[5], 32);
}

static void mgr_drop(mgr_t *m, const board_t *b)
{
    for (int i = 0; i < m->n; i++) {
        if (!memcmp(m->m[i].id, b->me.id, 8)) {
            m->m[i] = m->m[--m->n];
        }
    }
}

// The envelope: nonce || ChaCha20-Poly1305(k, epoch || key || wk, aad = SHA-256(roster)) || tag
static void env_make(const uint8_t k[32], const uint8_t *rb, size_t rn, uint32_t epoch, const uint8_t key[32],
                     const uint8_t wk[32], uint8_t env[TPF_ENV_LEN])
{
    uint8_t plain[TPF_ENV_PLAIN], aad[32];
    wr32le(plain, epoch);
    memcpy(plain + 4, key, 32);
    memcpy(plain + 36, wk, 32);
    hc_sha256(rb, rn, aad);
    memset(env, 0x33, 12);
    aead_seal(k, env, aad, 32, plain, TPF_ENV_PLAIN, env + 12);
}

// One JOIN of the manager (design 2.7.1). bad: 1 = a wrong commitment, 2 = a wrong sig_m, 3 = wrong digits, 4 = ALLOW
// with no digits (NULL). ALLOW: by_console = the digits of the console line (the manager's digits), else the digits
// that the screen showed (main/pool_page.c gives its copy of tfn_pool_join_sas()).
// Returns the code of the first step that failed, NULL = the board is in the pool.
static const char *join(board_t *b, mgr_t *m, const char *auth, int bad, bool by_console)
{
    B = b;
    uint8_t em_sk[32], em[32], nm[16], c[32], cm[6 + 32 + 16], sig[64], s[133];
    memset(em_sk, 0x21 + bad, 32);
    x25519_pub(em_sk, em);
    memset(nm, 0x17, 16);
    memcpy(cm, "TFNJC1", 6);
    memcpy(cm + 6, em, 32);
    memcpy(cm + 38, nm, 16);
    hc_sha256(cm, sizeof cm, c);
    tpf_hello_t h;
    const char *e = tpf_hello(&b->p, m->pk, c, "MGR", auth, &h);
    if (e) {
        return e;
    }
    // the board: its ID, its key and sig_b over "TFNJ1" || C || E_b || N_b
    memcpy(s, "TFNJ1", 5);
    memcpy(s + 5, c, 32);
    memcpy(s + 37, h.eb, 32);
    memcpy(s + 69, h.nb, 16);
    check(!memcmp(h.id, b->me.id, 8) && hc_p256_verify(h.pk, s, 85, h.sig), "hello: sig_b verifies");
    memcpy(s + 85, em, 32);
    memcpy(s + 117, nm, 16);
    sign_with(m->d, s, sizeof s, sig);
    if (bad == 1) {
        nm[0] ^= 1;
    }
    if (bad == 2) {
        sig[5] ^= 1;
    }
    if ((e = tpf_reveal(&b->p, em, nm, sig)) != NULL) {
        return e;
    }
    // the 6 digits: u32 little endian of SHA-256("TFNSAS1" || E_m || E_b || N_m || N_b) mod 1000000
    uint8_t sm[7 + 96], d[32];
    char want[8];
    memcpy(sm, "TFNSAS1", 7);
    memcpy(sm + 7, em, 32);
    memcpy(sm + 39, h.eb, 32);
    memcpy(sm + 71, nm, 16);
    memcpy(sm + 87, h.nb, 16);
    hc_sha256(sm, sizeof sm, d);
    snprintf(want, sizeof want, "%06u", (unsigned)(rd32le(d) % 1000000u));
    if (!auth) {
        check(!strcmp(b->p.j.sas, want), "the 6 digits of the board %s = the manager %s", b->p.j.sas, want);
        if (bad == 3) {
            want[0] = want[0] == '9' ? '0' : (char)(want[0] + 1);
        }
        char sp[9], shown[TFN_POOL_SAS_LEN];
        snprintf(sp, sizeof sp, "%.3s %.3s", want, want + 3);
        memcpy(shown, b->p.j.sas, sizeof shown);
        if ((e = tpf_allow(&b->p, bad == 4 ? NULL : by_console ? sp : shown)) != NULL) {
            return e;
        }
    }
    uint8_t rb[TPF_ROSTER_MAX], sh[32], k[32], env[TPF_ENV_LEN];
    const size_t rn = mgr_roster(m, rb);
    x25519(em_sk, h.eb, sh);
    hkdf32(sh, 32, c, 32, "TFNSEAL1", k);
    env_make(k, rb, rn, m->epoch, m->key, m->wk[b->me.mac[5] & 7], env);
    const char *what = "";
    return tpf_set(&b->p, rb, rn, env, sizeof env, false, &what);
}

// The header X-TFN-Auth of the manager for the board b
static void header(mgr_t *m, board_t *b, const char *method, const char *uri, char out[128])
{
    uint8_t msg[300], mac[32], mid[8];
    const uint64_t c = ++m->ctr;
    memcpy(msg, "TFNWEB1", 7);
    memcpy(msg + 7, tpf_bn(&b->p), 16);
    for (int i = 0; i < 8; i++) {
        msg[23 + i] = (uint8_t)(c >> (8 * i));
    }
    const size_t ml = strlen(method), ul = strlen(uri);
    memcpy(msg + 31, method, ml);
    msg[31 + ml] = ' ';
    memcpy(msg + 32 + ml, uri, ul);
    hmac256(m->wk[b->me.mac[5] & 7], 32, msg, 32 + ml + ul, mac);
    device_id(m->pk, mid);
    int k = 0;
    for (int i = 0; i < 8; i++) {
        k += snprintf(out + k, 4, "%02x", mid[i]);
    }
    k += snprintf(out + k, 24, ".%llu.", (unsigned long long)c);
    for (int i = 0; i < 32; i++) {
        k += snprintf(out + k, 4, "%02x", mac[i]);
    }
}

// A request of the manager with its header: the gate (tpf_web_ok) and the check of the handler (tpf_req_mgr)
static bool signed_req(mgr_t *m, board_t *b, const char *uri)
{
    char h[128];
    header(m, b, "POST", uri, h);
    return tpf_web_ok(&b->p, "POST", uri, h) && tpf_req_mgr(&b->p, "POST", uri, h);
}

// op=set of the manager (the request header) with the roster of now
static const char *push(mgr_t *m, board_t *b, const char **what)
{
    uint8_t rb[TPF_ROSTER_MAX];
    const size_t rn = mgr_roster(m, rb);
    B = b;
    return tpf_set(&b->p, rb, rn, NULL, 0, signed_req(m, b, "/api/pool?op=set"), what);
}

// op=key of the manager: the roster of now and its pool key, sealed to the box key of the board
static const char *push_key(mgr_t *m, board_t *b, const char **what)
{
    uint8_t rb[TPF_ROSTER_MAX], em_sk[32], em[32], sh[32], k[32], env[TPF_ENV_LEN];
    const size_t rn = mgr_roster(m, rb);
    B = b;
    memset(em_sk, 0x66, 32);
    x25519_pub(em_sk, em);
    x25519(em_sk, b->me.bx, sh);
    hkdf32(sh, 32, b->me.id, 8, "TFNSEAL1", k);
    env_make(k, rb, rn, m->epoch, m->key, m->wk[b->me.mac[5] & 7], env);
    return tpf_key(&b->p, em, env, sizeof env, rb, rn, signed_req(m, b, "/api/pool?op=key"), what);
}

static int frame(uint8_t *f, int n)
{
    f[0] = 'A';
    f[1] = 'L';
    f[2] = 4;
    f[3] = 'P';
    f[4] = 1;
    f[5] = (uint8_t)n;
    f[6] = 0;
    memset(f + 7, 0, 5);
    for (int i = 0; i < n; i++) {
        f[12 + i] = (uint8_t)i;
    }
    return 12 + n;
}

// ---------------------------------------------------------------- the tests

static board_t s_a, s_b, s_c;
static mgr_t s_m;

static void test_join_errors(void)
{
    board_t *a = &s_a;
    board_make(a, 1);
    boot(a, false);
    mgr_make(&s_m, 0x70);
    mgr_add(&s_m, a, TFN_POOL_ROLE_MEMBER);
    check(join(a, &s_m, NULL, 0, false) && !strcmp(join(a, &s_m, NULL, 0, false), "no_join"),
          "no JOIN screen: op=hello = no_join");
    check(!tpf_join_start(&a->p) && a->p.j.step == TFN_POOL_JOIN_WAIT, "the JOIN screen opens");
    uint8_t jb[40];
    check(tpf_join_beacon(&a->p, jb, sizeof jb) == TFN_POOL_JOIN_LEN && jb[1] == 'J', "'A' 'J' while the screen waits");
    const char *e = join(a, &s_m, NULL, 1, false);
    check(e && !strcmp(e, "commit") && a->p.j.step == TFN_POOL_JOIN_FAIL, "a wrong commitment: %s", e ? e : "-");
    tpf_join_start(&a->p);
    e = join(a, &s_m, NULL, 2, false);
    check(e && !strcmp(e, "sig") && a->p.j.step == TFN_POOL_JOIN_FAIL, "a wrong sig_m: %s", e ? e : "-");
    tpf_join_start(&a->p);
    e = join(a, &s_m, NULL, 3, true);
    check(e && !strcmp(e, "digits") && a->p.state == TFN_POOL_NONE, "wrong digits on the console: %s", e ? e : "-");
    tpf_join_start(&a->p);
    e = join(a, &s_m, NULL, 4, false);
    check(e && !strcmp(e, "digits") && a->p.state == TFN_POOL_NONE && a->p.j.step == TFN_POOL_JOIN_FAIL,
          "ALLOW with no digits: the JOIN fails: %s", e ? e : "-");
    // the time limits: the screen (10 min), a step (2 min)
    tpf_join_start(&a->p);
    s_now += TPF_JOIN_MS + 1;
    check(tpf_join_step(&a->p) == TFN_POOL_JOIN_FAIL && !strcmp(a->p.j.why, "time"), "the JOIN screen ends at 10 min");
    tpf_join_start(&a->p);
    uint8_t c[32] = {0};
    tpf_hello_t h;
    check(!tpf_hello(&a->p, s_m.pk, c, "", NULL, &h), "op=hello");
    // a second JOIN while one runs
    e = tpf_hello(&a->p, s_m.pk, c, "", NULL, &h);
    check(e && !strcmp(e, "busy"), "a second op=hello while one runs: %s", e ? e : "-");
    e = tpf_join_start(&a->p);
    check(e && !strcmp(e, "busy"), "a second JOIN screen while one runs: %s", e ? e : "-");
    uint8_t rb[TPF_ROSTER_MAX], env[TPF_ENV_LEN] = {0};
    const char *what;
    e = tpf_set(&a->p, rb, mgr_roster(&s_m, rb), env, sizeof env, false, &what);
    check(e && !strcmp(e, "wait"), "op=set before ALLOW = wait: %s", e ? e : "-");
    s_now += TPF_STEP_MS + 1;
    uint8_t z[64] = {0};
    e = tpf_reveal(&a->p, z, z, z);
    check(e && !strcmp(e, "no_join") && !strcmp(a->p.j.why, "time"), "a step ends at 2 min: %s", e ? e : "-");
    tpf_join_start(&a->p);
    tpf_no(&a->p);
    check(a->p.j.step == TFN_POOL_JOIN_FAIL && !strcmp(a->p.j.why, "no"), "NO on the board ends the JOIN");
    check(a->p.state == TFN_POOL_NONE && a->mgr_clear == 0, "no pool after the faults");
}

static void test_token_join(void)
{
    board_t *c = &s_c;
    board_make(c, 3);
    snprintf(c->token, sizeof c->token, "tok-123");
    boot(c, false);
    const char *e = join(c, &s_m, "tok-123", 0, false);
    check(e && !strcmp(e, "token"), "the token JOIN in a public build: %s", e ? e : "-");
    boot(c, true);
    e = join(c, &s_m, "wrong", 0, false);
    check(e && !strcmp(e, "token"), "a wrong token: %s", e ? e : "-");
    mgr_t m2;
    mgr_make(&m2, 0x30);
    mgr_add(&m2, c, TFN_POOL_ROLE_MEMBER);
    e = join(c, &m2, "tok-123", 0, false);
    check(!e && c->p.state == TFN_POOL_OK && c->mgr_clear == 1 && !c->token[0], "the token JOIN: %s", e ? e : "ok");
    snprintf(c->token, sizeof c->token, "tok-123");
    e = join(c, &m2, "tok-123", 0, false);
    check(e && !strcmp(e, "token"), "no token JOIN with a pool: %s", e ? e : "-");
}

static void test_join_ok(void)
{
    board_t *a = &s_a, *b = &s_b;
    B = a;
    tpf_join_start(&a->p);
    const char *e = join(a, &s_m, NULL, 0, false);
    check(!e && a->p.state == TFN_POOL_OK && a->p.j.step == TFN_POOL_JOIN_DONE, "JOIN with the finger: %s",
          e ? e : "ok");
    uint8_t v[32];
    check(f_nvs_get("pk", v, 32) && !memcmp(v, s_m.key, 32) && f_nvs_get("wk", v, 32) && a->fn > 0 && a->mgr_clear == 1,
          "the pool key, the request key, the file, the manager of before cleared");
    // the second board: a JOIN with the console digits, then the new roster to the first board
    board_make(b, 2);
    boot(b, false);
    mgr_add(&s_m, b, TFN_POOL_ROLE_CHILD);
    s_m.ver = 2;
    tpf_join_start(&b->p);
    e = join(b, &s_m, NULL, 0, true);
    check(!e && b->p.state == TFN_POOL_OK, "JOIN with the console digits: %s", e ? e : "ok");
    const char *what = "";
    e = push(&s_m, a, &what);
    check(!e && !strcmp(what, "take") && a->p.r.nm == 3 && a->p.rv == 2, "the new roster: %s %s", e ? e : "ok", what);
    e = push(&s_m, a, &what);
    check(!e && !strcmp(what, "same"), "the same roster again = same: %s", e ? e : what);
}

static void test_header(void)
{
    board_t *a = &s_a;
    B = a;
    char h[128];
    header(&s_m, a, "GET", "/api/boards", h);
    check(tpf_web_ok(&a->p, "GET", "/api/boards", h), "a good header");
    check(!tpf_web_ok(&a->p, "GET", "/api/boards", h), "the same header again (a replay)");
    header(&s_m, a, "GET", "/api/boards", h);
    check(!tpf_web_ok(&a->p, "GET", "/api/apps", h), "a header of another uri");
    tpf_web_reset(&a->p, s_m.ctr + 100);  // (each counter up to this one is old)
    header(&s_m, a, "GET", "/api/boards", h);
    check(!tpf_web_ok(&a->p, "GET", "/api/boards", h), "a counter under the window");
    tpf_web_reset(&a->p, 0);
    s_m.ctr += 200;
    s_m.ctr += 20;
    header(&s_m, a, "GET", "/api/boards", h);
    h[20] ^= 1;
    check(!tpf_web_ok(&a->p, "GET", "/api/boards", h), "a changed header");
    const char *what;
    uint8_t rb[TPF_ROSTER_MAX];
    const char *e = tpf_set(&a->p, rb, mgr_roster(&s_m, rb), NULL, 0, false, &what);
    check(e && !strcmp(e, "auth"), "op=set with no header = auth: %s", e ? e : "-");
}

static void test_rules(void)
{
    board_t *a = &s_a;
    B = a;
    const char *what = "", *e;
    s_m.ver = 1;
    e = push(&s_m, a, &what);
    check(e && !strcmp(e, "old"), "an older roster: %s", e ? e : "-");
    // a roster that a test key signs (the key is in that roster, not in the stored one)
    uint8_t xd[32], xpk[65], rb[TPF_ROSTER_MAX];
    memset(xd, 0x99, 32);
    hc_p256_pub(xd, xpk);
    size_t rn = roster(&s_m, xd, xpk, s_m.pool, 9, rb);
    e = tpf_set(&a->p, rb, rn, NULL, 0, signed_req(&s_m, a, "/api/pool?op=set"), &what);
    check(e && !strcmp(e, "admin"), "a roster of a key that is not an admin key: %s", e ? e : "-");
    uint8_t other[16];
    memset(other, 0xEE, 16);
    rn = roster(&s_m, s_m.d, s_m.pk, other, 9, rb);
    e = tpf_set(&a->p, rb, rn, NULL, 0, signed_req(&s_m, a, "/api/pool?op=set"), &what);
    check(e && !strcmp(e, "other"), "a roster of another pool: %s", e ? e : "-");
    rn = mgr_roster(&s_m, rb);
    rb[rn - 3] ^= 1;
    e = tpf_set(&a->p, rb, rn, NULL, 0, signed_req(&s_m, a, "/api/pool?op=set"), &what);
    check(e && !strcmp(e, "sig"), "a bad signature: %s", e ? e : "-");
    check(a->p.state == TFN_POOL_OK && a->p.rv == 2, "the pool stays after the refused rosters");
}

// The frames, the UDP tails and the beacons between a (the sender) and b
static void test_packets(void)
{
    board_t *a = &s_a, *b = &s_b;
    uint8_t f[260], g[260];
    B = a;
    int n = tpf_frame_seal(&a->p, f, frame(f, 5), sizeof f);
    check(n == 25, "a frame of 5 data bytes = 25 bytes");
    B = b;
    check(tpf_frame_open(&b->p, a->me.mac, f, n) == 0, "the frame opens on the other member");
    check(tpf_frame_open(&b->p, a->me.mac, f, n) == TFN_POOL_DROP_OLD, "the same frame again = old");
    memcpy(g, f, n);
    g[13] ^= 1;
    check(tpf_frame_open(&b->p, a->me.mac, g, n) == TFN_POOL_DROP_BAD, "a changed bit = bad");
    check(tpf_frame_open(&b->p, s_c.me.mac, f, n) == TFN_POOL_DROP_WHO, "a MAC out of the roster = who");
    g[13] ^= 1;
    g[7] ^= 0x80;
    check(tpf_frame_open(&b->p, a->me.mac, g, n) == TFN_POOL_DROP_OTHER, "another pool tag = other");
    B = a;
    check(tpf_frame_seal(&a->p, f, frame(f, 230), sizeof f) == 250, "230 data bytes = 250 bytes");
    check(tpf_frame_seal(&a->p, f, frame(f, 231), sizeof f) < 0, "231 data bytes: refused");
    // the UDP call v2: 'A' 'C' 2 from(6) to(6) op
    uint8_t u[40] = {'A', 'C', 2};
    memcpy(u + 3, a->me.mac, 6);
    memcpy(u + 9, b->me.mac, 6);
    u[15] = 5;
    n = tpf_udp_seal(&a->p, u, 16, sizeof u);
    B = b;
    check(n == 29 && tpf_udp_open(&b->p, a->me.mac, u, n) == 0, "the call v2 opens");
    check(tpf_udp_open(&b->p, a->me.mac, u, n) == TFN_POOL_DROP_OLD, "the call v2 again = old");
    check(tpf_udp_open(&b->p, a->me.mac, u, n - 1) == TFN_POOL_DROP_BAD, "a cut call = bad");
    // the beacon 'A' 'P'
    uint8_t plain[TFN_POOL_BP_LEN], out[TFN_POOL_BEACON_LEN], back[TFN_POOL_BP_LEN];
    memset(plain, 0x41, sizeof plain);
    int on = 0, pn = 0;
    B = a;
    check(tpf_beacon_seal(&a->p, plain, sizeof plain, out, &on) && on == 78, "the beacon is 78 bytes");
    B = b;
    check(tpf_beacon_open(&b->p, out, on, back, &pn) && pn == 43 && !memcmp(back, plain, 43), "the beacon opens");
    out[30] ^= 1;
    check(!tpf_beacon_open(&b->p, out, on, back, &pn), "a changed beacon does not open");
    uint32_t c[TFN_POOL_CNT_N];
    memcpy(c, b->p.rx.count, sizeof c);
    check(c[TFN_POOL_CNT_OK] == 2 && c[TFN_POOL_CNT_OLD] == 2 && c[TFN_POOL_CNT_WHO] == 1, "the counts of the drops");
}

// op=key: a new pool key (a removal), the previous key in its hour, the removed board
static void test_key(void)
{
    board_t *a = &s_a, *b = &s_b;
    uint8_t f[64];
    B = a;
    const int n = tpf_frame_seal(&a->p, f, frame(f, 3), sizeof f);  // (a frame of the old key)
    mgr_drop(&s_m, a);
    s_m.ver = 3;
    s_m.epoch = 2;
    memset(s_m.key, 0x77, 32);
    const char *what = "";
    const char *e = push_key(&s_m, b, &what);
    check(!e && b->p.rx.key[0].epoch == 2 && b->p.r.nm == 2, "op=key: the new roster and the new key: %s",
          e ? e : what);
    check(tpf_frame_open(&b->p, a->me.mac, f, n) == TFN_POOL_DROP_WHO, "the removed board is not in the roster");
    e = push(&s_m, a, &what);
    check(!e && !strcmp(what, "removed") && a->p.state == TFN_POOL_NONE && !strcmp(a->left, "TEST POOL"),
          "the roster without the board: it left the pool (%s)", e ? e : what);
    uint32_t rv = 0;
    check(f_get_u32("rv", &rv) && rv == 3 && a->fn == 0, "the floor stays, the file goes");
    // the previous key: a member that did not get the new key yet
    mgr_add(&s_m, a, TFN_POOL_ROLE_MEMBER);
    s_m.ver = 4;
    s_m.epoch = 2;
    B = a;
    tpf_join_start(&a->p);
    e = join(a, &s_m, NULL, 0, false);
    check(!e && a->p.state == TFN_POOL_OK && a->p.rx.key[0].epoch == 2, "the JOIN again: %s", e ? e : "ok");
    e = push(&s_m, b, &what);
    check(!e, "the roster with the board again: %s", e ? e : what);
    s_m.ver = 5;
    s_m.epoch = 3;
    memset(s_m.key, 0x78, 32);
    e = push_key(&s_m, b, &what);
    check(!e && b->p.rx.key[0].epoch == 3 && b->p.rx.prev_on, "the key epoch 3: %s", e ? e : what);
    B = a;
    const int m = tpf_frame_seal(&a->p, f, frame(f, 3), sizeof f);  // (the key of epoch 2)
    B = b;
    check(tpf_frame_open(&b->p, a->me.mac, f, m) == 0, "a frame of the previous key in its hour");
    s_now += TPF_PREV_MS + 1;
    B = a;
    const int k = tpf_frame_seal(&a->p, f, frame(f, 3), sizeof f);
    B = b;
    check(tpf_frame_open(&b->p, a->me.mac, f, k) == TFN_POOL_DROP_OTHER, "after the hour: other");
    s_m.epoch = 2;
    e = push_key(&s_m, b, &what);
    check(e && !strcmp(e, "old"), "an older key epoch: %s", e ? e : "-");
}

static void test_restart(void)
{
    board_t *b = &s_b;
    boot(b, false);
    check(b->p.state == TFN_POOL_OK && b->p.rx.key[0].epoch == 5 && b->p.r.nm == 3, "the pool after a restart");
    const char *e = tpf_leave(&b->p, true);
    check(e && !strcmp(e, "child"), "a CHILD cannot leave on its screen: %s", e ? e : "-");
    b->file[40] ^= 1;
    boot(b, false);
    check(b->p.state == TFN_POOL_DAMAGED, "a damaged file = damaged");
    const char *what = "";
    e = push(&s_m, b, &what);
    check(!e && b->p.state == TFN_POOL_OK && b->p.rx.key[0].epoch == 5, "the manager repairs the file: %s",
          e ? e : what);
    e = tpf_leave(&b->p, false);
    check(!e && b->p.state == TFN_POOL_NONE && b->fn == 0, "LEAVE on the console");
    boot(b, false);
    check(b->p.state == TFN_POOL_NONE, "no pool after LEAVE and a restart");
}

// The points of the manager (track F): op=mgr with the request header, two requests that pass each other, op=key
// with a roster on a damaged board
static void test_manager_points(void)
{
    board_t *a = &s_a;
    B = a;
    // op=mgr of /api/apps: a pool board takes it only with the header of its pool manager
    char h[128];
    const char *uri = "/api/apps?op=mgr&lan=http://m&vpn=&newtok=t&auth=t";
    header(&s_m, a, "POST", uri, h);
    const bool mgr = tpf_web_ok(&a->p, "POST", uri, h) && tpf_req_mgr(&a->p, "POST", uri, h);
    check(a->p.state == TFN_POOL_OK && mgr && !tpf_mgr_ok(&a->p, mgr), "op=mgr with the header of the pool manager");
    check(tpf_mgr_ok(&a->p, false) != NULL, "op=mgr of a pool board with no header: refused");
    board_t none;
    memset(&none, 0, sizeof none);
    none.p.legacy = true;
    check(!tpf_mgr_ok(&none.p, false), "op=mgr of a board with no pool (LEGACY): the token rules as before");
    none.p.legacy = false;
    check(tpf_mgr_ok(&none.p, false) != NULL, "op=mgr of a board with no pool (public build): refused");
    // two requests that pass each other (the OTA upload and a poll): both take, each one only one time
    char h1[128], h2[128], h3[128];
    header(&s_m, a, "POST", "/ota", h1);
    header(&s_m, a, "GET", "/api/boards", h2);
    header(&s_m, a, "GET", "/api/apps", h3);
    check(tpf_web_ok(&a->p, "GET", "/api/boards", h2), "the second request first");
    check(tpf_web_ok(&a->p, "POST", "/ota", h1) && tpf_req_mgr(&a->p, "POST", "/ota", h1),
          "then the first request (a lower counter, not seen)");
    check(!tpf_web_ok(&a->p, "POST", "/ota", h1) && !tpf_web_ok(&a->p, "GET", "/api/boards", h2),
          "each request only one time");
    check(tpf_web_ok(&a->p, "GET", "/api/apps", h3), "the third request");
    for (int i = 0; i < TPF_WEB_SEEN + 5; i++) {
        header(&s_m, a, "GET", "/api/boards", h);
        tpf_web_ok(&a->p, "GET", "/api/boards", h);
    }
    check(!tpf_web_ok(&a->p, "GET", "/api/apps", h3), "an old request after 64 newer ones: refused");
}

// A damaged board takes op=key with a roster (the manager repairs the file and gives the key in one request)
static void test_damaged_key(void)
{
    board_t *b = &s_b;
    s_m.epoch = 4;
    s_m.ver = 6;
    memset(s_m.key, 0x79, 32);
    const char *what = "";
    const char *e = push_key(&s_m, b, &what);
    check(!e && b->p.rx.key[0].epoch == 4, "op=key before the damage: %s", e ? e : what);
    b->file[40] ^= 1;
    boot(b, false);
    check(b->p.state == TFN_POOL_DAMAGED, "a damaged file = damaged (again)");
    s_m.ver = 7;
    s_m.epoch = 5;
    memset(s_m.key, 0x7A, 32);
    e = push_key(&s_m, b, &what);
    uint8_t v[32];
    check(!e && b->p.state == TFN_POOL_OK && b->p.rx.key[0].epoch == 5 && f_nvs_get("pk", v, 32) && v[0] == 0x7A,
          "op=key with a roster on a damaged board: %s", e ? e : what);
    e = push_key(&s_m, &s_c, &what);
    check(e != NULL, "op=key of the manager of another pool: refused (%s)", e ? e : "-");
}

// ---------------------------------------------------------------- the shared vectors of the manager (pool_v1.json)
// A small JSON reader: enough for the file of tests/unit/test_pool_crypto.py.

static const char *jws(const char *s)
{
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t' || *s == ',' || *s == ':') {
        s++;
    }
    return s;
}

static const char *jskip(const char *s)
{
    s = jws(s);
    if (*s == '"') {
        for (s++; *s && *s != '"'; s++) {
            s += *s == '\\' && s[1];
        }
        return *s ? s + 1 : s;
    }
    if (*s == '{' || *s == '[') {
        const char close = *s == '{' ? '}' : ']';
        s = jws(s + 1);
        while (*s && *s != close) {
            s = jws(jskip(s));
        }
        return *s ? s + 1 : s;
    }
    while (*s && !strchr(",}] \n\r\t", *s)) {
        s++;
    }
    return s;
}

// The value of key in the object at o ('{'), or NULL
static const char *jkey(const char *o, const char *key)
{
    if (!o || *(o = jws(o)) != '{') {
        return NULL;
    }
    const size_t kl = strlen(key);
    for (const char *s = jws(o + 1); *s == '"';) {
        const bool hit = !strncmp(s + 1, key, kl) && s[1 + kl] == '"';
        const char *v = jws(jskip(s));
        if (hit) {
            return v;
        }
        s = jws(jskip(v));
    }
    return NULL;
}

// The element i of the array at a ('['), or NULL
static const char *jat(const char *a, int i)
{
    if (!a || *(a = jws(a)) != '[') {
        return NULL;
    }
    const char *s = jws(a + 1);
    for (; *s && *s != ']' && i > 0; i--) {
        s = jws(jskip(s));
    }
    return *s && *s != ']' ? s : NULL;
}

// A hex string value: its bytes (at most max), or -1
static int jhex(const char *v, uint8_t *out, int max)
{
    if (!v || *v != '"') {
        return -1;
    }
    int n = 0;
    for (v++; v[0] && v[0] != '"' && v[1] != '"' && n < max; v += 2) {
        unsigned x;
        if (sscanf(v, "%2x", &x) != 1) {
            return -1;
        }
        out[n++] = (uint8_t)x;
    }
    return *v == '"' ? n : -1;
}

static const char *jtext(const char *v, char *out, size_t max)
{
    size_t n = 0;
    for (v = v && *v == '"' ? v + 1 : ""; *v && *v != '"' && n + 1 < max; v++) {
        out[n++] = *v;
    }
    out[n] = 0;
    return out;
}

static long long jint(const char *v)
{
    return v ? strtoll(v, NULL, 10) : -1;
}

static uint8_t s_v[TPF_ROSTER_MAX];

// One field of the object o (hex) into out: exactly n bytes
static bool vget(const char *o, const char *key, uint8_t *out, int n)
{
    const int k = jhex(jkey(o, key), out, n);
    check(k == n, "vector field %s: %d bytes, not %d", key, k, n);
    return k == n;
}

static int want_code(const char *v)
{
    char w[12];
    jtext(v, w, sizeof w);
    return !strcmp(w, "ok") ? 0 : !strcmp(w, "other") ? 1 : !strcmp(w, "who") ? 2 : !strcmp(w, "old") ? 4 : 3;
}

// The JOIN of the vector "join" through the service: the hello answer, the 6 digits, op=set with the envelope.
static board_t s_v_board;

static bool vec_join(const char *root)
{
    const char *j = jkey(root, "join");
    board_t *b = &s_v_board;
    memset(b, 0, sizeof *b);
    B = b;
    uint8_t eb_sk[32], nb[16], sig_b[64], apk[65], c[32], em[32], nm[16], sig_m[64], eb[32], msg[85];
    bool ok = vget(j, "board_pk", b->me.pk, 65) && vget(j, "board_id", b->me.id, 8) && vget(j, "eb_priv", eb_sk, 32) &&
              vget(j, "nb", nb, 16) && vget(j, "sig_b", sig_b, 64) && vget(j, "admin", apk, 65) &&
              vget(j, "c", c, 32) && vget(j, "em_pub", em, 32) && vget(j, "nm", nm, 16) &&
              vget(j, "sig_m", sig_m, 64) && vget(j, "eb_pub", eb, 32) && vget(j, "msg_b", msg, 85) &&
              vget(j, "bx_priv", b->bx_sk, 32);
    if (!ok) {
        return false;
    }
    b->me.ok = true;
    f_nvs_set("bx", b->bx_sk, 32);
    boot(b, false);
    memcpy(s_rq, eb_sk, 32);  // (the ephemeral key and the nonce of the vector)
    memcpy(s_rq + 32, nb, 16);
    s_rqn = 48;
    s_rqi = 0;
    tpf_join_start(&b->p);
    s_fixed_sig = sig_b;
    tpf_hello_t h;
    const char *e = tpf_hello(&b->p, apk, c, "", NULL, &h);
    s_fixed_sig = NULL;
    check(!e && !memcmp(h.eb, eb, 32) && !memcmp(h.nb, nb, 16), "vector JOIN: op=hello (%s)", e ? e : "ok");
    check(s_last_n == 85 && !memcmp(s_last_msg, msg, 85), "vector JOIN: the message of sig_b = msg_b");
    e = tpf_reveal(&b->p, em, nm, sig_m);
    const long sas = (long)jint(jkey(j, "sas"));
    check(!e && atol(b->p.j.sas) == sas, "vector JOIN: op=reveal (%s), the 6 digits %s = %ld", e ? e : "ok", b->p.j.sas,
          sas);
    check(!tpf_allow(&b->p, b->p.j.sas), "vector JOIN: ALLOW with the digits");
    uint8_t env[TPF_ENV_LEN], key[32], wk[32], v[32];
    const int rn = jhex(jkey(j, "roster"), s_v, sizeof s_v);
    const char *what = "";
    ok = rn > 0 && vget(j, "env", env, TPF_ENV_LEN) && vget(j, "pool_k", key, 32) && vget(j, "wk", wk, 32);
    e = ok ? tpf_set(&b->p, s_v, (size_t)rn, env, sizeof env, false, &what) : "form";
    uint32_t ep = 0;
    check(!e && b->p.state == TFN_POOL_OK && f_nvs_get("pk", v, 32) && !memcmp(v, key, 32) && f_nvs_get("wk", v, 32) &&
                  !memcmp(v, wk, 32) && f_get_u32("pke", &ep) && ep == (uint32_t)jint(jkey(j, "epoch")),
          "vector JOIN: op=set with the envelope (%s)", e ? e : "ok");
    return !e;
}

// The header vectors "web_check" with the request key of the vector JOIN
static void vec_web(const char *root)
{
    const char *w = jkey(root, "web_check");
    board_t *b = &s_v_board;
    B = b;
    char method[8], uri[64], hdr[160], name[24];
    vget(w, "bn", b->p.bn, 16);
    b->p.bn_on = true;
    jtext(jkey(w, "method"), method, sizeof method);
    jtext(jkey(w, "uri"), uri, sizeof uri);
    const char *cs = jkey(w, "cases");
    int n = 0;
    for (const char *c; (c = jat(cs, n)) != NULL; n++) {
        tpf_web_reset(&b->p, (uint64_t)jint(jkey(c, "last")));
        jtext(jkey(c, "header"), hdr, sizeof hdr);
        const bool want = *jws(jkey(c, "ok")) == 't';
        check(tpf_web_ok(&b->p, method, uri, hdr) == want, "vector web %s", jtext(jkey(c, "name"), name, sizeof name));
    }
    check(n >= 5, "vector web: %d cases", n);
}

// The receive vectors: "frame_open", the "open" cases of "udp" and "beacon" (the keys of the vector JOIN, epoch 2)
static void vec_rx(const char *root)
{
    board_t *b = &s_v_board;
    B = b;
    uint8_t mac[6], plain[TFN_POOL_BP_LEN];
    char name[24];
    const char *cs = jkey(jkey(root, "frame_open"), "cases");
    int n = 0;
    for (const char *c; (c = jat(cs, n)) != NULL; n++) {
        const int fn = jhex(jkey(c, "frame"), s_v, sizeof s_v);
        jhex(jkey(c, "mac"), mac, 6);
        check(tpf_frame_open(&b->p, mac, s_v, fn) == want_code(jkey(c, "want")), "vector frame %s",
              jtext(jkey(c, "name"), name, sizeof name));
    }
    memset(b->p.rx.win, 0, sizeof b->p.rx.win);  // (the UDP vectors use other counters of the same senders)
    for (int i = 0; (cs = jat(jkey(jkey(root, "udp"), "open"), i)) != NULL; i++) {
        const int un = jhex(jkey(cs, "pkt"), s_v, sizeof s_v);
        // (the vectors have no roster: a "from" that is not a member of the JOIN roster is WHO here)
        const bool out = un > 9 && tfn_pool_roster_slot(&b->p.r, s_v + 3) < 0;
        check(tpf_udp_open(&b->p, NULL, s_v, un) == (out ? TFN_POOL_DROP_WHO : want_code(jkey(cs, "want"))),
              "vector udp %s", jtext(jkey(cs, "name"), name, sizeof name));
    }
    for (int i = 0; (cs = jat(jkey(jkey(root, "beacon"), "open"), i)) != NULL; i++) {
        const int bn = jhex(jkey(cs, "pkt"), s_v, sizeof s_v);
        int pn = 0;
        check(tpf_beacon_open(&b->p, s_v, bn, plain, &pn) == (want_code(jkey(cs, "want")) == 0), "vector beacon %s",
              jtext(jkey(cs, "name"), name, sizeof name));
    }
    check(n > 20, "vector frames: %d cases", n);
}

// The vector "rekey": op=key with the new roster and the envelope to the box key
static void vec_rekey(const char *root)
{
    const char *k = jkey(root, "rekey");
    board_t *b = &s_v_board;
    B = b;
    uint8_t em[32], env[TPF_ENV_LEN], key[32], v[32];
    const int rn = jhex(jkey(k, "roster"), s_v, sizeof s_v);
    const char *what = "", *e = "form";
    if (rn > 0 && vget(k, "em_pub", em, 32) && vget(k, "env", env, TPF_ENV_LEN) && vget(k, "pool_k", key, 32)) {
        e = tpf_key(&b->p, em, env, sizeof env, s_v, (size_t)rn, true, &what);
    }
    check(!e && b->p.rx.key[0].epoch == (uint32_t)jint(jkey(k, "epoch")) && b->p.rx.prev_on && f_nvs_get("pk", v, 32) &&
                  !memcmp(v, key, 32),
          "vector op=key (%s %s)", e ? e : "ok", what);
}

static void vectors(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) {
        printf("(no vector file: the vector part is not run)\n");
        return;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *t = malloc((size_t)n + 1);
    t[fread(t, 1, (size_t)n, f)] = 0;
    fclose(f);
    if (vec_join(t)) {
        vec_web(t);
        vec_rx(t);
        vec_rekey(t);
    }
    free(t);
}

// ---------------------------------------------------------------- the host crypto against known values

static void crypto_self(void)
{
    // RFC 4231 test case 2
    uint8_t h[32], want[32];
    hmac256((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, h);
    jhex("\"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843\"", want, 32);
    check(!memcmp(h, want, 32), "host HMAC-SHA256 (RFC 4231)");
    // RFC 7748 6.1
    uint8_t a[32], bp[32], s1[32], s2[32];
    jhex("\"77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a\"", a, 32);
    jhex("\"de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f\"", bp, 32);
    jhex("\"4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742\"", want, 32);
    x25519(a, bp, s1);
    check(!memcmp(s1, want, 32), "host X25519 (RFC 7748)");
    x25519_pub(a, s2);
    jhex("\"8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a\"", want, 32);
    check(!memcmp(s2, want, 32), "host X25519 public key (RFC 7748)");
    // RFC 8439 2.8.2
    uint8_t key[32], nonce[12], aad[12], ct[114], tag[16];
    const char *pt = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, "
                     "sunscreen would be it.";
    for (int i = 0; i < 32; i++) {
        key[i] = (uint8_t)(0x80 + i);
    }
    jhex("\"070000004041424344454647\"", nonce, 12);
    jhex("\"50515253c0c1c2c3c4c5c6c7\"", aad, 12);
    ops_seal(key, nonce, aad, 12, (const uint8_t *)pt, 114, ct, tag);
    jhex("\"1ae10b594f09e26a7e902ecbd0600691\"", want, 16);
    check(!memcmp(tag, want, 16) && ct[0] == 0xd3 && ct[1] == 0x1a, "host ChaCha20-Poly1305 (RFC 8439)");
}

int main(int argc, char **argv)
{
    crypto_self();
    test_join_errors();
    test_token_join();
    test_join_ok();
    test_header();
    test_rules();
    test_packets();
    test_key();
    test_manager_points();
    test_damaged_key();
    test_restart();
    vectors(argc > 1 ? argv[1] : NULL);
    printf("POOL FLOW %s (%d checks, %d failed)\n", s_fails ? "FAILED" : "OK", s_checks, s_fails);
    return s_fails ? 1 : 0;
}
