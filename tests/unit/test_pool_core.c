// The unit test of the pool core (main/tfn_pool_core.c): the RFC and paper vectors of the crypto parts, the roster
// parse (each field wrong once, a cut file, a second signature, the unknown types, 18 members), the rules of 2.3,
// link v4 (each byte changed once, a cut frame, 230 and 231 data bytes), the replay window, the previous key hour,
// the RELAXED / STRICT table, the SAS, the web header, the beacons, the envelope, the send counter, the legacy peer
// rule, the vector file of the manager (tests/unit/vectors/pool_v1.json: the two sides agree byte for byte), and a
// loop of random and changed inputs for each parser.
// The crypto: SHA-256 and ECDSA P-256 of host_crypto.c; HMAC, ChaCha20-Poly1305 and X25519 here (slow, only for
// tests). Build and run: python tests/unit/run.py
// Arguments: [the vector file] [the number of inputs of each parser loop, default 1000000]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_crypto.h"
#include "tfn_pool_core.h"

static int s_fail, s_n;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        s_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            s_fail++;                                                                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                        \
        }                                                                                                              \
    } while (0)

// ---------------------------------------------------------------- the host crypto of the test

static void sha256(const void *d, size_t n, uint8_t out[32])
{
    hc_sha256(d, n, out);
}

static void hmac(const uint8_t *k, size_t kn, const uint8_t *const *p, const size_t *pn, int np, uint8_t out[32])
{
    uint8_t kb[64] = {0}, ih[32];
    if (kn > 64) {
        hc_sha256(k, kn, kb);
    } else if (kn) {
        memcpy(kb, k, kn);
    }
    size_t tot = 64;
    for (int i = 0; i < np; i++) {
        tot += pn[i];
    }
    uint8_t *b = malloc(tot);
    for (int i = 0; i < 64; i++) {
        b[i] = kb[i] ^ 0x36;
    }
    size_t at = 64;
    for (int i = 0; i < np; i++) {
        if (pn[i]) {
            memcpy(b + at, p[i], pn[i]);
        }
        at += pn[i];
    }
    hc_sha256(b, tot, ih);
    free(b);
    uint8_t ob[96];
    for (int i = 0; i < 64; i++) {
        ob[i] = kb[i] ^ 0x5c;
    }
    memcpy(ob + 64, ih, 32);
    hc_sha256(ob, sizeof ob, out);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#define R32(x, n) ((x) << (n) | (x) >> (32 - (n)))
static void qr(uint32_t *x, int a, int b, int c, int d)
{
    x[a] += x[b];
    x[d] = R32(x[d] ^ x[a], 16);
    x[c] += x[d];
    x[b] = R32(x[b] ^ x[c], 12);
    x[a] += x[b];
    x[d] = R32(x[d] ^ x[a], 8);
    x[c] += x[d];
    x[b] = R32(x[b] ^ x[c], 7);
}

static void chacha_block(const uint8_t key[32], uint32_t ctr, const uint8_t nonce[12], uint8_t out[64])
{
    uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574}, x[16];
    for (int i = 0; i < 8; i++) {
        s[4 + i] = rd32(key + 4 * i);
    }
    s[12] = ctr;
    for (int i = 0; i < 3; i++) {
        s[13 + i] = rd32(nonce + 4 * i);
    }
    memcpy(x, s, sizeof x);
    for (int i = 0; i < 10; i++) {
        qr(x, 0, 4, 8, 12);
        qr(x, 1, 5, 9, 13);
        qr(x, 2, 6, 10, 14);
        qr(x, 3, 7, 11, 15);
        qr(x, 0, 5, 10, 15);
        qr(x, 1, 6, 11, 12);
        qr(x, 2, 7, 8, 13);
        qr(x, 3, 4, 9, 14);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t v = x[i] + s[i];
        for (int j = 0; j < 4; j++) {
            out[4 * i + j] = (uint8_t)(v >> (8 * j));
        }
    }
}

static void chacha_xor(const uint8_t key[32], uint32_t ctr, const uint8_t nonce[12], const uint8_t *in, size_t n,
                       uint8_t *out)
{
    uint8_t ks[64];
    for (size_t i = 0; i < n; i++) {
        if (i % 64 == 0) {
            chacha_block(key, (uint32_t)(ctr + i / 64), nonce, ks);
        }
        out[i] = in[i] ^ ks[i % 64];
    }
}

static void add1305(uint32_t h[17], const uint32_t c[17])
{
    uint32_t u = 0;
    for (int j = 0; j < 17; j++) {
        u += h[j] + c[j];
        h[j] = u & 255;
        u >>= 8;
    }
}

// Poly1305 (the 8-bit limbs of TweetNaCl)
static void poly1305(const uint8_t key[32], const uint8_t *m, size_t n, uint8_t out[16])
{
    static const uint32_t MINUSP[17] = {5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 252};
    uint32_t x[17], r[17] = {0}, h[17] = {0}, c[17], g[17];
    for (int j = 0; j < 16; j++) {
        r[j] = key[j];
    }
    r[3] &= 15, r[4] &= 252, r[7] &= 15, r[8] &= 252, r[11] &= 15, r[12] &= 252, r[15] &= 15;
    while (n > 0) {
        memset(c, 0, sizeof c);
        size_t j;
        for (j = 0; j < 16 && j < n; j++) {
            c[j] = m[j];
        }
        c[j] = 1;
        m += j;
        n -= j;
        add1305(h, c);
        for (int i = 0; i < 17; i++) {
            x[i] = 0;
            for (int k = 0; k < 17; k++) {
                x[i] += h[k] * (k <= i ? r[i - k] : 320 * r[i + 17 - k]);
            }
        }
        memcpy(h, x, sizeof h);
        uint32_t u = 0;
        for (int k = 0; k < 16; k++) {
            u += h[k];
            h[k] = u & 255;
            u >>= 8;
        }
        u += h[16];
        h[16] = u & 3;
        u = 5 * (u >> 2);
        for (int k = 0; k < 16; k++) {
            u += h[k];
            h[k] = u & 255;
            u >>= 8;
        }
        u += h[16];
        h[16] = u;
    }
    memcpy(g, h, sizeof g);
    add1305(h, MINUSP);
    uint32_t s = -(h[16] >> 7);
    for (int j = 0; j < 17; j++) {
        h[j] ^= s & (g[j] ^ h[j]);
    }
    for (int j = 0; j < 16; j++) {
        c[j] = key[j + 16];
    }
    c[16] = 0;
    add1305(h, c);
    for (int j = 0; j < 16; j++) {
        out[j] = (uint8_t)h[j];
    }
}

// the tag of RFC 8439 2.8: Poly1305 over aad | pad | ct | pad | len(aad) | len(ct)
static void aead_tag(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *ct,
                     size_t n, uint8_t tag[16])
{
    uint8_t pk[64];
    chacha_block(key, 0, nonce, pk);
    size_t pa = (an + 15) / 16 * 16, pc = (n + 15) / 16 * 16;
    uint8_t *b = calloc(1, pa + pc + 16);
    if (an) {
        memcpy(b, aad, an);
    }
    if (n) {
        memcpy(b + pa, ct, n);
    }
    for (int i = 0; i < 8; i++) {
        b[pa + pc + i] = (uint8_t)((uint64_t)an >> (8 * i));
        b[pa + pc + 8 + i] = (uint8_t)((uint64_t)n >> (8 * i));
    }
    poly1305(pk, b, pa + pc + 16, tag);
    free(b);
}

static bool aead_seal(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *pt,
                      size_t n, uint8_t *ct, uint8_t tag[16])
{
    chacha_xor(key, 1, nonce, pt, n, ct);
    aead_tag(key, nonce, aad, an, ct, n, tag);
    return true;
}

static bool aead_open(const uint8_t key[32], const uint8_t nonce[12], const uint8_t *aad, size_t an, const uint8_t *ct,
                      size_t n, const uint8_t tag[16], uint8_t *pt)
{
    uint8_t t[16];
    aead_tag(key, nonce, aad, an, ct, n, t);
    if (memcmp(t, tag, 16) != 0) {
        return false;
    }
    chacha_xor(key, 1, nonce, ct, n, pt);
    return true;
}

// X25519 (the field code of TweetNaCl: 16 limbs of 16 bits)
typedef int64_t gf[16];

static void car25519(int64_t *o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}

static void sel25519(int64_t *p, int64_t *q, int b)
{
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const int64_t *n)
{
    gf m, t;
    memcpy(t, n, sizeof t);
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void fe_add(int64_t *o, const int64_t *a, const int64_t *b)
{
    for (int i = 0; i < 16; i++) {
        o[i] = a[i] + b[i];
    }
}

static void fe_sub(int64_t *o, const int64_t *a, const int64_t *b)
{
    for (int i = 0; i < 16; i++) {
        o[i] = a[i] - b[i];
    }
}

static void fe_mul(int64_t *o, const int64_t *a, const int64_t *b)
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
    car25519(o);
    car25519(o);
}

static void fe_inv(int64_t *o, const int64_t *in)
{
    gf c;
    memcpy(c, in, sizeof c);
    for (int a = 253; a >= 0; a--) {
        fe_mul(c, c, c);
        if (a != 2 && a != 4) {
            fe_mul(c, c, in);
        }
    }
    memcpy(o, c, sizeof c);
}

static bool x25519(const uint8_t priv[32], const uint8_t pub[32], uint8_t out[32])
{
    static const gf K121665 = {0xDB41, 1};
    uint8_t z[32];
    int64_t x[80];
    gf a, b, c, d, e, f;
    memcpy(z, priv, 32);
    z[31] = (uint8_t)((z[31] & 127) | 64);
    z[0] &= 248;
    for (int i = 0; i < 16; i++) {
        x[i] = pub[2 * i] + ((int64_t)pub[2 * i + 1] << 8);
    }
    x[15] &= 0x7fff;
    for (int i = 0; i < 16; i++) {
        b[i] = x[i];
        d[i] = a[i] = c[i] = 0;
    }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r);
        sel25519(c, d, r);
        fe_add(e, a, c);
        fe_sub(a, a, c);
        fe_add(c, b, d);
        fe_sub(b, b, d);
        fe_mul(d, e, e);
        fe_mul(f, a, a);
        fe_mul(a, c, a);
        fe_mul(c, b, e);
        fe_add(e, a, c);
        fe_sub(a, a, c);
        fe_mul(b, a, a);
        fe_sub(c, d, f);
        fe_mul(a, c, K121665);
        fe_add(a, a, d);
        fe_mul(c, c, a);
        fe_mul(a, d, f);
        fe_mul(d, b, x);
        fe_mul(b, e, e);
        sel25519(a, b, r);
        sel25519(c, d, r);
    }
    for (int i = 0; i < 16; i++) {
        x[i + 16] = a[i];
        x[i + 32] = c[i];
    }
    fe_inv(x + 32, x + 32);
    fe_mul(x + 16, x + 16, x + 32);
    pack25519(out, x + 16);
    return true;
}

static bool p256_verify(const uint8_t pk[65], const uint8_t *msg, size_t n, const uint8_t sig[64])
{
    return hc_p256_verify(pk, msg, n, sig);
}

static const tfn_pool_ops_t OPS = {sha256, hmac, p256_verify, aead_seal, aead_open, x25519};

// ---------------------------------------------------------------- small helpers

static int hexv(char c)
{
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

// hex text into out (cap bytes); the byte count, or -1
static int unhex(const char *s, uint8_t *out, int cap)
{
    int n = (int)strlen(s);
    if (n % 2 || n / 2 > cap) {
        return -1;
    }
    for (int i = 0; i < n / 2; i++) {
        int a = hexv(s[2 * i]), b = hexv(s[2 * i + 1]);
        if (a < 0 || b < 0) {
            return -1;
        }
        out[i] = (uint8_t)(a << 4 | b);
    }
    return n / 2;
}

static uint64_t s_rng = 0x9E3779B97F4A7C15ULL;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 7;
    s_rng ^= s_rng << 17;
    return (uint32_t)(s_rng >> 16);
}

static void fill(uint8_t *p, size_t n, uint8_t seed)
{
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)(seed + i * 7);
    }
}

// ---------------------------------------------------------------- the known answers (RFC and paper)

static void test_kat(void)
{
    uint8_t key[16], msg[256], b[256], o[64];
    fill(key, 0, 0);
    for (int i = 0; i < 16; i++) {
        key[i] = (uint8_t)i;
    }
    for (int i = 0; i < 256; i++) {
        msg[i] = (uint8_t)i;
    }
    CHECK(tfn_pool_siphash(key, msg, 15, NULL, 0) == 0xa129ca6149be45e5ULL);  // the paper, appendix A
    CHECK(tfn_pool_siphash(key, NULL, 0, NULL, 0) == 0x726fdb47dd0e0e31ULL);  // the reference vector of length 0
    for (int n = 0; n < 70; n++) {
        uint64_t one = tfn_pool_siphash(key, msg, (size_t)n, NULL, 0);
        for (int k = 0; k <= n; k++) {
            CHECK(tfn_pool_siphash(key, msg, (size_t)k, msg + k, (size_t)(n - k)) == one);
        }
    }
    // RFC 5869 A.1
    uint8_t ikm[22], salt[13], info[10];
    memset(ikm, 0x0b, sizeof ikm);
    for (int i = 0; i < 13; i++) {
        salt[i] = (uint8_t)i;
    }
    for (int i = 0; i < 10; i++) {
        info[i] = (uint8_t)(0xf0 + i);
    }
    CHECK(unhex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", b, 64) == 42);
    CHECK(tfn_pool_hkdf(&OPS, salt, 13, ikm, 22, info, 10, o, 42) && !memcmp(o, b, 42));
    CHECK(!tfn_pool_hkdf(&OPS, salt, 13, ikm, 22, info, 10, o, 255 * 32 + 1));
    // RFC 7748 6.1
    uint8_t ap[32], bp[32], apub[32], bpub[32], sh[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", ap, 32);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", bp, 32);
    unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bpub, 32);
    unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", sh, 32);
    uint8_t nine[32] = {9};
    x25519(ap, nine, apub);
    unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", b, 32);
    CHECK(!memcmp(apub, b, 32));
    CHECK(x25519(ap, bpub, o) && !memcmp(o, sh, 32));
    CHECK(x25519(bp, apub, o) && !memcmp(o, sh, 32));
    // RFC 8439 2.8.2
    static const char PT[] = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the "
                             "future, sunscreen would be it.";
    uint8_t k32[32], nonce[12], aad[12], ct[128], tag[16], back[128];
    for (int i = 0; i < 32; i++) {
        k32[i] = (uint8_t)(0x80 + i);
    }
    unhex("070000004041424344454647", nonce, 12);
    unhex("50515253c0c1c2c3c4c5c6c7", aad, 12);
    aead_seal(k32, nonce, aad, 12, (const uint8_t *)PT, 114, ct, tag);
    unhex("d31a8d34648e60db7b86afbc53ef7ec2", b, 16);
    CHECK(!memcmp(ct, b, 16));
    unhex("1ae10b594f09e26a7e902ecbd0600691", b, 16);
    CHECK(!memcmp(tag, b, 16));
    CHECK(aead_open(k32, nonce, aad, 12, ct, 114, tag, back) && !memcmp(back, PT, 114));
    tag[3] ^= 1;
    CHECK(!aead_open(k32, nonce, aad, 12, ct, 114, tag, back));
}

// ---------------------------------------------------------------- the roster builder of the test

typedef struct {
    uint8_t t;
    int n;
    uint8_t v[160];
} trec_t;

typedef struct {
    trec_t r[48];
    int n;
} tros_t;

// the P-256 keys of the test: [0] admin A, [1] admin R, [2] a TEST key, [3..7] boards
static uint8_t s_d[8][32], s_pk[8][65];

static void keys_init(void)
{
    for (int i = 0; i < 8; i++) {
        memset(s_d[i], 0, 32);
        s_d[i][0] = 0x11;
        s_d[i][5] = (uint8_t)(0x30 + i);
        s_d[i][31] = (uint8_t)(i + 1);
        CHECK(hc_p256_pub(s_d[i], s_pk[i]));
    }
}

static void tr_add(tros_t *x, uint8_t t, const void *v, int n)
{
    trec_t *r = &x->r[x->n++];
    r->t = t;
    r->n = n;
    memcpy(r->v, v, (size_t)n);
}

static void tr_u32(tros_t *x, uint8_t t, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    tr_add(x, t, b, 4);
}

static void member_rec(uint8_t out[97], int key, const uint8_t mac[6], uint8_t role, const char *name)
{
    memset(out, 0, 97);
    tfn_pool_dev_id(&OPS, s_pk[key], out);
    memcpy(out + 8, s_pk[key], 65);
    memcpy(out + 73, mac, 6);
    out[79] = role;
    memcpy(out + 81, name, strlen(name));
}

static const uint8_t POOL_ID[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const uint8_t MGR_MAC[6] = {0xFE, 0x4D, 0x47, 0x52, 0x00, 0x01};

static void board_mac(int i, uint8_t mac[6])
{
    const uint8_t m[6] = {0x28, 0x84, 0x85, 0x91, 0x27, (uint8_t)(0x50 + i)};
    memcpy(mac, m, 6);
}

// the good roster: the pool, version ver, admins A and R, the manager (key A) and boards 3, 4, 5 (5 is a CHILD)
static void tr_good(tros_t *x, const uint8_t *pool, uint32_t ver, int nboards)
{
    uint8_t m[97], mac[6];
    memset(x, 0, sizeof *x);
    tr_add(x, TFN_POOL_T_ID, pool, 16);
    tr_add(x, TFN_POOL_T_NAME, "FAMILY", 6);
    tr_u32(x, TFN_POOL_T_VER, ver);
    tr_u32(x, TFN_POOL_T_EPOCH, 3);
    tr_u32(x, TFN_POOL_T_FLAGS, TFN_POOL_F_LEGACY_BEACON);
    tr_add(x, TFN_POOL_T_ADMIN, s_pk[0], 65);
    tr_add(x, TFN_POOL_T_ADMIN, s_pk[1], 65);
    member_rec(m, 0, MGR_MAC, TFN_POOL_ROLE_MANAGER, "MANAGER");
    tr_add(x, TFN_POOL_T_MEMBER, m, 97);
    for (int i = 0; i < nboards; i++) {
        board_mac(i, mac);
        member_rec(m, 3 + i, mac, i == 2 ? TFN_POOL_ROLE_CHILD : TFN_POOL_ROLE_MEMBER, i ? "BOARD" : "DESK");
        tr_add(x, TFN_POOL_T_MEMBER, m, 97);
    }
    tr_add(x, TFN_POOL_T_URLS, "http://192.0.2.10:8080", 23);  // the LAN URL, the zero byte, no VPN URL
    uint8_t t[8] = {0x80, 0xE1, 0x52, 0x65};
    tr_add(x, TFN_POOL_T_TIME, t, 8);
    tr_add(x, 0x99, "later", 5);
}

// the file bytes; signer >= 0: the signature record of that key at the end
static size_t tr_write(const tros_t *x, uint8_t *out, int signer)
{
    size_t p = 8;
    memcpy(out, TFN_POOL_MAGIC, 8);
    for (int i = 0; i < x->n; i++) {
        out[p] = x->r[i].t;
        out[p + 1] = (uint8_t)x->r[i].n;
        out[p + 2] = (uint8_t)(x->r[i].n >> 8);
        memcpy(out + p + 3, x->r[i].v, (size_t)x->r[i].n);
        p += 3 + (size_t)x->r[i].n;
    }
    if (signer >= 0) {
        uint8_t k[32] = {0};
        k[0] = 0x2B;
        k[17] = (uint8_t)p;
        k[31] = (uint8_t)(signer + 7);
        out[p] = TFN_POOL_T_SIG;
        out[p + 1] = TFN_POOL_SIG_LEN;
        out[p + 2] = 0;
        tfn_pool_key_id(&OPS, s_pk[signer], out + p + 3);
        CHECK(hc_p256_sign(s_d[signer], k, out, p, out + p + 11));
        p += 3 + TFN_POOL_SIG_LEN;
    }
    return p;
}

// the file with no real signature (64 zero bytes): enough for the parse
static size_t tr_raw(const tros_t *x, uint8_t *out)
{
    size_t p = tr_write(x, out, -1);
    memset(out + p, 0, 3 + TFN_POOL_SIG_LEN);
    out[p] = TFN_POOL_T_SIG;
    out[p + 1] = TFN_POOL_SIG_LEN;
    return p + 3 + TFN_POOL_SIG_LEN;
}

static int parse_of(const tros_t *x)
{
    static uint8_t f[TFN_POOL_FILE_MAX * 2];
    static tfn_pool_roster_t r;
    return tfn_pool_roster_parse(f, tr_raw(x, f), &r);
}

// the index of the first record of type t
static int tr_find(const tros_t *x, uint8_t t)
{
    for (int i = 0; i < x->n; i++) {
        if (x->r[i].t == t) {
            return i;
        }
    }
    return -1;
}

static void tr_del(tros_t *x, int i)
{
    memmove(&x->r[i], &x->r[i + 1], sizeof x->r[0] * (size_t)(x->n - i - 1));
    x->n--;
}

// ---------------------------------------------------------------- the roster parse

static void test_parse_fields(void)
{
    static tros_t x;
    static const uint8_t TYPES[] = {TFN_POOL_T_ID,     TFN_POOL_T_NAME,  TFN_POOL_T_VER,
                                    TFN_POOL_T_EPOCH,  TFN_POOL_T_FLAGS, TFN_POOL_T_ADMIN,
                                    TFN_POOL_T_MEMBER, TFN_POOL_T_URLS,  TFN_POOL_T_TIME};
    tr_good(&x, POOL_ID, 5, 3);
    CHECK(parse_of(&x) == 0);
    for (size_t k = 0; k < sizeof TYPES; k++) {
        int i = tr_find(&x, TYPES[k]);
        // a value one byte longer, then one byte shorter: a fixed record = a bad length; the name stays good; the
        // URL record gets a second zero byte, then loses its zero byte
        int want = TYPES[k] == TFN_POOL_T_NAME ? 0 : TYPES[k] == TFN_POOL_T_URLS ? TFN_POOL_E_VALUE : TFN_POOL_E_LEN;
        tr_good(&x, POOL_ID, 5, 3);
        x.r[i].n++;
        CHECK(parse_of(&x) == want);
        tr_good(&x, POOL_ID, 5, 3);
        x.r[i].n--;
        CHECK(parse_of(&x) == want);
        // the record missing
        tr_good(&x, POOL_ID, 5, 3);
        tr_del(&x, i);
        if (TYPES[k] == TFN_POOL_T_URLS || TYPES[k] == TFN_POOL_T_ADMIN || TYPES[k] == TFN_POOL_T_MEMBER) {
            CHECK(parse_of(&x) == 0);  // optional, or one of two (of four) stays
        } else {
            CHECK(parse_of(&x) == TFN_POOL_E_MISSING);
        }
        // the record two times
        tr_good(&x, POOL_ID, 5, 3);
        x.r[x.n] = x.r[i];
        x.n++;
        CHECK(parse_of(&x) == (TYPES[k] == TFN_POOL_T_MEMBER ? TFN_POOL_E_DUP : TFN_POOL_E_COUNT));
    }
    // all members out, all admins out
    tr_good(&x, POOL_ID, 5, 0);
    tr_del(&x, tr_find(&x, TFN_POOL_T_MEMBER));
    CHECK(parse_of(&x) == TFN_POOL_E_MISSING);
    tr_good(&x, POOL_ID, 5, 3);
    tr_del(&x, tr_find(&x, TFN_POOL_T_ADMIN));
    tr_del(&x, tr_find(&x, TFN_POOL_T_ADMIN));
    CHECK(parse_of(&x) == TFN_POOL_E_MISSING);
    // the name: 1 and 24 bytes are good, 0 and 25 are not
    for (int n = 0; n <= 25; n++) {
        tr_good(&x, POOL_ID, 5, 3);
        int i = tr_find(&x, TFN_POOL_T_NAME);
        memset(x.r[i].v, 'N', 25);
        x.r[i].n = n;
        CHECK(parse_of(&x) == (n >= 1 && n <= 24 ? 0 : TFN_POOL_E_LEN));
    }
}

static void test_parse_values(void)
{
    static tros_t x;
    static uint8_t f[TFN_POOL_FILE_MAX];
    static tfn_pool_roster_t r;
    int im;
#define MEMBER_BAD(off, val, want)                                                                                     \
    tr_good(&x, POOL_ID, 5, 3);                                                                                        \
    im = tr_find(&x, TFN_POOL_T_MEMBER) + 1;                                                                           \
    x.r[im].v[off] = (val);                                                                                            \
    CHECK(parse_of(&x) == (want))
    MEMBER_BAD(79, 3, TFN_POOL_E_VALUE);  // role 3
    MEMBER_BAD(79, 2, 0);                 // role CHILD
    MEMBER_BAD(8, 2, TFN_POOL_E_VALUE);   // pk not 0x04
    MEMBER_BAD(73, 0x29, 0);              // a group MAC: the roster is signed, the parse takes it
    MEMBER_BAD(80, 0xFF, 0);              // the member flags: any value
    MEMBER_BAD(95, 'X', 0);               // a letter after the end zero of the name: not in the name
    tr_good(&x, POOL_ID, 5, 3);
    im = tr_find(&x, TFN_POOL_T_MEMBER) + 1;
    memcpy(x.r[im].v + 81, "ABCDEFGHIJKLMNOP", 16);  // a name of 16 bytes with no end zero
    tfn_pool_roster_parse(f, tr_raw(&x, f), &r);
    CHECK(!strcmp(r.m[1].name, "ABCDEFGHIJKLMNOP"));
    tr_good(&x, POOL_ID, 5, 3);
    im = tr_find(&x, TFN_POOL_T_MEMBER);
    memcpy(x.r[im + 1].v + 73, x.r[im].v + 73, 6);
    CHECK(parse_of(&x) == TFN_POOL_E_DUP);  // the same MAC
    tr_good(&x, POOL_ID, 5, 3);
    memcpy(x.r[im + 2].v, x.r[im].v, 8);
    CHECK(parse_of(&x) == TFN_POOL_E_DUP);  // the same ID
    tr_good(&x, POOL_ID, 5, 3);
    int ia = tr_find(&x, TFN_POOL_T_ADMIN);
    x.r[ia].v[0] = 0x02;
    CHECK(parse_of(&x) == TFN_POOL_E_VALUE);
    tr_good(&x, POOL_ID, 5, 3);
    memcpy(x.r[ia + 1].v, x.r[ia].v, 65);
    CHECK(parse_of(&x) == TFN_POOL_E_DUP);
    tr_good(&x, POOL_ID, 5, 3);
    x.r[x.n] = x.r[ia];
    x.r[x.n].v[64] ^= 1;
    x.n++;
    CHECK(parse_of(&x) == TFN_POOL_E_COUNT);  // 3 admin keys
    tr_good(&x, POOL_ID, 5, 3);
    memcpy(x.r[tr_find(&x, TFN_POOL_T_NAME)].v, "\xD7\xA9\xD7\x9C", 4);  // Hebrew letters
    CHECK(parse_of(&x) == 0);
    // the URL record: the LAN URL, one zero byte, the VPN URL
    static const struct {
        const char *v;
        int n, want;
        const char *lan, *vpn;
    } U[] = {{"\x00", 1, 0, "", ""},
             {"http://x\x00", 9, 0, "http://x", ""},
             {"\x00http://v", 9, 0, "", "http://v"},
             {"http://a:1\x00http://b:2", 21, 0, "http://a:1", "http://b:2"},
             {"http://x", 8, TFN_POOL_E_VALUE, "", ""},
             {"a\x00b\x00", 4, TFN_POOL_E_VALUE, "", ""},
             {"\x00\x00", 2, TFN_POOL_E_VALUE, "", ""},
             {"", 0, TFN_POOL_E_VALUE, "", ""}};
    for (size_t k = 0; k < sizeof U / sizeof U[0]; k++) {
        tr_good(&x, POOL_ID, 5, 3);
        int iu = tr_find(&x, TFN_POOL_T_URLS);
        memcpy(x.r[iu].v, U[k].v, (size_t)U[k].n);
        x.r[iu].n = U[k].n;
        int e = tfn_pool_roster_parse(f, tr_raw(&x, f), &r);
        CHECK(e == U[k].want && (e || (!strcmp(r.lan, U[k].lan) && !strcmp(r.vpn, U[k].vpn))));
    }
    for (int side = 0; side < 2; side++) {
        for (int len = 63; len <= 64; len++) {
            tr_good(&x, POOL_ID, 5, 3);
            int iu = tr_find(&x, TFN_POOL_T_URLS);
            memset(x.r[iu].v, 'u', (size_t)len + 1);
            x.r[iu].v[side ? 0 : len] = 0;
            x.r[iu].n = len + 1;
            CHECK(parse_of(&x) == (len == 63 ? 0 : TFN_POOL_E_VALUE));  // 63 bytes good, 64 not
        }
    }
#undef MEMBER_BAD
}

static void test_parse_form(void)
{
    static tros_t x;
    static uint8_t f[TFN_POOL_FILE_MAX + 512];
    static tfn_pool_roster_t r;
    tr_good(&x, POOL_ID, 5, 3);
    size_t n = tr_raw(&x, f);
    CHECK(tfn_pool_roster_parse(f, n, &r) == 0);
    CHECK(r.ver == 5 && r.epoch == 3 && r.flags == 1 && r.nadmin == 2 && r.nm == 4 && r.time == 0x6552E180);
    CHECK(!strcmp(r.name, "FAMILY") && !strcmp(r.m[1].name, "DESK") && !strcmp(r.lan, "http://192.0.2.10:8080"));
    CHECK(!strcmp(r.vpn, "") && r.m[0].role == TFN_POOL_ROLE_MANAGER && r.m[3].role == TFN_POOL_ROLE_CHILD);
    CHECK(r.signed_len == n - 3 - TFN_POOL_SIG_LEN && !memcmp(r.pool, POOL_ID, 16));
    // each cut of the file
    for (size_t k = 0; k < n; k++) {
        CHECK(tfn_pool_roster_parse(f, k, &r) < 0);
    }
    // the magic, the size
    f[3] ^= 0x20;
    CHECK(tfn_pool_roster_parse(f, n, &r) == TFN_POOL_E_MAGIC);
    f[3] ^= 0x20;
    CHECK(tfn_pool_roster_parse(NULL, n, &r) == TFN_POOL_E_SIZE);
    CHECK(tfn_pool_roster_parse(f, TFN_POOL_FILE_MAX + 1, &r) == TFN_POOL_E_SIZE);
    // bytes after the signature; a second signature record
    memset(f + n, 0, 80);
    CHECK(tfn_pool_roster_parse(f, n + 1, &r) == TFN_POOL_E_AFTER);
    memcpy(f + n, f + n - 75, 75);
    CHECK(tfn_pool_roster_parse(f, n + 75, &r) == TFN_POOL_E_AFTER);
    // a signature record of another length; no signature
    f[n - 74] = 71;
    CHECK(tfn_pool_roster_parse(f, n - 1, &r) == TFN_POOL_E_LEN);
    CHECK(tfn_pool_roster_parse(f, tr_write(&x, f, -1), &r) == TFN_POOL_E_MISSING);
    // the unknown types: below 0x80 refused, from 0x80 up skipped (also an empty one)
    static const int T[] = {0x00, 0x0A, 0x50, 0x7E, 0x80, 0xC3, 0xFF};
    for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
        tr_good(&x, POOL_ID, 5, 3);
        tr_add(&x, (uint8_t)T[k], "zz", T[k] == 0xC3 ? 0 : 2);
        CHECK(parse_of(&x) == (T[k] < 0x80 ? TFN_POOL_E_TYPE : 0));
    }
    // 16 boards + the manager = 17 members: good; 18: refused
    tr_good(&x, POOL_ID, 5, 4);
    uint8_t m[97];
    for (int i = 0; i < 12; i++) {
        uint8_t mac[6];
        board_mac(10 + i, mac);
        member_rec(m, 7, mac, 0, "X");
        m[0] = (uint8_t)(0xA0 + i);  // own IDs (the parse does not check them against the key)
        tr_add(&x, TFN_POOL_T_MEMBER, m, 97);
    }
    CHECK(parse_of(&x) == 0);
    board_mac(40, m + 73);
    m[0] = 0xEE;
    tr_add(&x, TFN_POOL_T_MEMBER, m, 97);
    CHECK(parse_of(&x) == TFN_POOL_E_COUNT);
    // a record length past the end
    tr_good(&x, POOL_ID, 5, 3);
    n = tr_raw(&x, f);
    f[8 + 1] = 0xFF;
    f[8 + 2] = 0xFF;
    CHECK(tfn_pool_roster_parse(f, n, &r) == TFN_POOL_E_CUT);
    CHECK(strcmp(tfn_pool_err_text(TFN_POOL_E_SELF), "not a member") == 0);
    CHECK(strcmp(tfn_pool_err_text(TFN_POOL_R_REMOVED), "removed") == 0);
}

// ---------------------------------------------------------------- the rules of 2.3

static void test_rules(void)
{
    static tros_t x;
    static uint8_t f[TFN_POOL_FILE_MAX];
    static tfn_pool_roster_t r;
    uint8_t self[8], other_pool[16];
    const uint8_t (*ar)[65] = (const uint8_t (*)[65])s_pk;  // A, R
    const uint8_t (*tk)[65] = (const uint8_t (*)[65]) & s_pk[2];
    tfn_pool_dev_id(&OPS, s_pk[4], self);  // this board = board 1 (key 4)
    memset(other_pool, 0x77, 16);
    tfn_pool_trust_t t = {false, POOL_ID, ar, 2, 4, self};
    tr_good(&x, POOL_ID, 5, 3);
    size_t n = tr_write(&x, f, 0);
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_R_OK);
    n = tr_write(&x, f, 1);
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_R_OK);  // key R signs
    t.floor = 5;
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_E_OLD);  // the version floor
    t.floor = 4;
    n = tr_write(&x, f, 2);
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_E_SIG);  // a TEST key: not an admin of the roster
    n = tr_write(&x, f, 0);
    f[n - 1] ^= 1;
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_E_SIG);
    f[n - 1] ^= 1;
    f[20] ^= 1;  // a signed byte (in the pool name)
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_E_SIG);
    f[20] ^= 1;
    t.nadmin = 1;
    n = tr_write(&x, f, 1);
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_E_ADMIN);  // R is not in the stored list
    t.nadmin = 2;
    // another pool; no stored pool and no JOIN
    tr_good(&x, other_pool, 9, 3);
    n = tr_write(&x, f, 0);
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_E_POOL);
    tfn_pool_trust_t none = {false, NULL, ar, 2, 0, self};
    CHECK(tfn_pool_roster_check(&OPS, &none, f, n, &r) == TFN_POOL_E_POOL);
    CHECK(tfn_pool_roster_check(&OPS, NULL, f, n, &r) == TFN_POOL_E_POOL);
    // the board is removed: a good roster of the stored pool with no entry for it
    tr_good(&x, POOL_ID, 6, 3);
    tr_del(&x, tr_find(&x, TFN_POOL_T_MEMBER) + 2);
    n = tr_write(&x, f, 0);
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_R_REMOVED);
    t.floor = 6;
    CHECK(tfn_pool_roster_check(&OPS, &t, f, n, &r) == TFN_POOL_E_OLD);  // an old removal does not erase
    t.floor = 4;
    // JOIN: the admin key of the JOIN signs, and it is an admin of the new roster
    tr_good(&x, other_pool, 1, 3);
    memcpy(x.r[tr_find(&x, TFN_POOL_T_ADMIN) + 1].v, s_pk[2], 65);  // admins A and T
    n = tr_write(&x, f, 2);
    tfn_pool_trust_t j = {true, POOL_ID, tk, 1, 9, self};
    CHECK(tfn_pool_roster_check(&OPS, &j, f, n, &r) == TFN_POOL_R_OK);  // a JOIN: no version floor
    j.pool = NULL;
    CHECK(tfn_pool_roster_check(&OPS, &j, f, n, &r) == TFN_POOL_R_OK);
    j.admin = ar;
    j.nadmin = 2;
    CHECK(tfn_pool_roster_check(&OPS, &j, f, n, &r) == TFN_POOL_E_ADMIN);  // T is not the JOIN key
    j.admin = tk;
    j.nadmin = 1;
    tr_good(&x, other_pool, 1, 3);  // admins A and R: T signs but is not in the new roster
    n = tr_write(&x, f, 2);
    CHECK(tfn_pool_roster_check(&OPS, &j, f, n, &r) == TFN_POOL_E_SIG);
    tr_good(&x, POOL_ID, 3, 3);  // a JOIN into the stored pool with an old version: no floor in a JOIN
    j.pool = POOL_ID;
    j.admin = ar;
    j.nadmin = 2;
    n = tr_write(&x, f, 0);
    CHECK(tfn_pool_roster_check(&OPS, &j, f, n, &r) == TFN_POOL_R_OK);
    tr_good(&x, POOL_ID, 3, 1);  // a JOIN roster with no entry for this board
    n = tr_write(&x, f, 0);
    CHECK(tfn_pool_roster_check(&OPS, &j, f, n, &r) == TFN_POOL_E_SELF);
    CHECK(tfn_pool_roster_slot(&r, MGR_MAC) == 0 && tfn_pool_roster_find(&r, self) == -1);
    // a bad form comes first
    f[0] = 'X';
    CHECK(tfn_pool_roster_check(&OPS, &j, f, n, &r) == TFN_POOL_E_MAGIC);
}

// ---------------------------------------------------------------- link v4, the UDP packets, the windows

static tfn_pool_keys_t s_k1, s_k2, s_kx;  // epoch 1 and 2 of the pool, and a key of another pool

static void keys_make(void)
{
    uint8_t pk[32];
    fill(pk, 32, 9);
    tfn_pool_derive(&OPS, pk, POOL_ID, 1, &s_k1);
    tfn_pool_derive(&OPS, pk, POOL_ID, 2, &s_k2);
    pk[0] ^= 1;
    tfn_pool_derive(&OPS, pk, POOL_ID, 1, &s_kx);
    if (s_kx.ptag == s_k1.ptag) {
        s_kx.ptag ^= 0x5A;  // a stranger pool with another tag byte
    }
    CHECK(s_k1.epoch == 1 && memcmp(s_k1.link, s_k2.link, 16) && memcmp(s_k1.hint, s_kx.hint, 4));
}

static int make_frame(const tfn_pool_keys_t *k, const uint8_t *mac, uint32_t ctr, int dn, uint8_t *f)
{
    f[0] = 'A';
    f[1] = 'L';
    f[2] = 4;
    f[3] = 0x50;
    f[4] = 3;
    f[5] = (uint8_t)dn;
    f[6] = 0x81;
    for (int i = 0; i < dn; i++) {
        f[12 + i] = (uint8_t)(ctr + i);
    }
    return tfn_pool_v4_seal(k, mac, ctr, f, 12 + dn, 260);
}

static void rx_make(tfn_pool_rx_t *rx, int nboards)
{
    static tros_t x;
    static uint8_t f[TFN_POOL_FILE_MAX];
    static tfn_pool_roster_t r;
    tr_good(&x, POOL_ID, 5, nboards);
    tfn_pool_roster_parse(f, tr_raw(&x, f), &r);
    tfn_pool_rx_init(rx, &r, &s_k1, NULL);
}

static void test_v4(void)
{
    static tfn_pool_rx_t rx;
    uint8_t f[260], g[260], mac[6], stranger[6] = {0x2A, 1, 2, 3, 4, 5};
    int slot = -1;
    board_mac(1, mac);
    rx_make(&rx, 3);
    CHECK(make_frame(&s_k1, mac, 100, 230, f) == 250);  // 230 data bytes = 250 bytes
    CHECK(make_frame(&s_k1, mac, 100, 231, g) == -1);   // 231 refused
    CHECK(tfn_pool_v4_seal(&s_k1, mac, 1, f, 12, 19) == -1);
    int n = make_frame(&s_k1, mac, 100, 40, f);
    CHECK(n == 60 && f[7] == s_k1.ptag && f[8] == 100);
    // each byte changed once: dropped, and the window does not move
    for (int i = 0; i < n; i++) {
        memcpy(g, f, (size_t)n);
        g[i] ^= (uint8_t)(1 + i % 255);
        int r = tfn_pool_v4_open(&rx, mac, g, n, &slot);
        CHECK(r == (i == 7 ? TFN_POOL_DROP_OTHER : TFN_POOL_DROP_BAD));
    }
    for (int i = 0; i < 6; i++) {
        uint8_t m2[6];
        memcpy(m2, mac, 6);
        m2[i] ^= 0x10;
        CHECK(tfn_pool_v4_open(&rx, m2, f, n, &slot) == TFN_POOL_DROP_WHO);
    }
    for (int k = 0; k < n; k++) {
        CHECK(tfn_pool_v4_open(&rx, mac, f, k, &slot) != TFN_POOL_RX_OK);  // a cut frame
    }
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_RX_OK && slot == 2);
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_DROP_OLD);  // the LAN copy of the same frame
    CHECK(tfn_pool_v4_open(&rx, stranger, f, n, &slot) == TFN_POOL_DROP_WHO);
    n = make_frame(&s_kx, mac, 101, 10, g);
    CHECK(tfn_pool_v4_open(&rx, mac, g, n, &slot) == TFN_POOL_DROP_OTHER);  // another pool
    g[7] = s_k1.ptag;
    CHECK(tfn_pool_v4_open(&rx, mac, g, n, &slot) == TFN_POOL_DROP_BAD);  // another pool with our tag byte
    CHECK(rx.count[TFN_POOL_RX_OK] == 1 && rx.count[TFN_POOL_DROP_OLD] == 1 && rx.count[TFN_POOL_DROP_WHO] == 7);
    static tfn_pool_rx_t off;
    memset(&off, 0, sizeof off);
    CHECK(tfn_pool_v4_open(&off, mac, f, 60, &slot) == TFN_POOL_DROP_OTHER);  // no pool here
    // a v4 frame of 250 bytes passes
    n = make_frame(&s_k1, mac, 5000, 230, f);
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_RX_OK);
    // the UDP v2 packets: a call 'A' 'C' 2 from to op (the sender MAC = from)
    uint8_t p[64] = {'A', 'C', 2};
    memcpy(p + 3, mac, 6);
    memcpy(p + 9, MGR_MAC, 6);
    p[15] = 5;
    CHECK(tfn_pool_udp2_seal(&s_k1, 6000, p, 16, 28) == -1);
    CHECK(tfn_pool_udp2_seal(&s_k1, 6000, p, 15, 64) == -1);  // a call has 16 bytes
    n = tfn_pool_udp2_seal(&s_k1, 6000, p, 16, 64);
    CHECK(n == 29 && p[16] == s_k1.ptag);
    for (int i = 0; i < n; i++) {
        memcpy(g, p, (size_t)n);
        g[i] ^= 0x40;
        int want = i == 16 ? TFN_POOL_DROP_OTHER : i >= 3 && i < 9 ? TFN_POOL_DROP_WHO : TFN_POOL_DROP_BAD;
        CHECK(tfn_pool_udp2_open(&rx, g, n, &slot) == want);
    }
    for (int k = 0; k < n; k++) {
        CHECK(tfn_pool_udp2_open(&rx, p, k, &slot) != TFN_POOL_RX_OK);  // a cut packet
    }
    CHECK(tfn_pool_udp2_open(&rx, p, n, &slot) == TFN_POOL_RX_OK && slot == 2);
    CHECK(tfn_pool_udp2_open(&rx, p, n, &slot) == TFN_POOL_DROP_OLD);
    memcpy(g, p, (size_t)n);
    board_mac(0, g + 3);
    CHECK(tfn_pool_udp2_open(&rx, g, n, &slot) == TFN_POOL_DROP_BAD);  // another member as "from": the tag binds it
    n = make_frame(&s_k1, mac, 6000, 8, f);
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_DROP_OLD);  // one window for the frames and the calls
    // an invite 'A' 'I' 2 from app(20)
    uint8_t q[64] = {'A', 'I', 2};
    memcpy(q + 3, mac, 6);
    memcpy(q + 9, "PONG", 4);
    CHECK(tfn_pool_udp2_seal(&s_k1, 6001, q, 29, 64) == 42);
    CHECK(tfn_pool_udp2_open(&rx, q, 42, &slot) == TFN_POOL_RX_OK);
    q[1] = 'X';
    CHECK(tfn_pool_udp2_seal(&s_k1, 1, q, 29, 64) == -1 && tfn_pool_udp2_open(&rx, q, 42, &slot) == TFN_POOL_DROP_BAD);
    p[2] = 1;
    CHECK(tfn_pool_udp2_seal(&s_k1, 1, p, 16, 64) == -1);  // a version 1 body
}

static void test_window(void)
{
    tfn_pool_win_t w;
    memset(&w, 0, sizeof w);
    CHECK(tfn_pool_win_take(&w, 1000));  // the first frame sets the floor
    CHECK(!tfn_pool_win_take(&w, 1000));
    for (uint32_t c = 1001; c < 1100; c++) {
        CHECK(tfn_pool_win_take(&w, c));  // in order
    }
    CHECK(!tfn_pool_win_take(&w, 1099) && !tfn_pool_win_take(&w, 1050));  // duplicates
    CHECK(tfn_pool_win_take(&w, 1110));                                   // a gap
    CHECK(tfn_pool_win_take(&w, 1105) && tfn_pool_win_take(&w, 1101));    // out of order
    CHECK(!tfn_pool_win_take(&w, 1105));
    CHECK(!tfn_pool_win_take(&w, 1110 - 63));  // 63 back: in the window, seen
    CHECK(tfn_pool_win_take(&w, 1100));        // in the window, not seen
    CHECK(!tfn_pool_win_take(&w, 1110 - 64));  // 64 back: too old
    CHECK(!tfn_pool_win_take(&w, 3));
    CHECK(tfn_pool_win_take(&w, 70000));  // a jump forward (a restart of the sender)
    CHECK(!tfn_pool_win_take(&w, 1111));
    CHECK(tfn_pool_win_take(&w, 69999) && tfn_pool_win_take(&w, 70000 - 63) && !tfn_pool_win_take(&w, 70000 - 64));
    CHECK(tfn_pool_win_take(&w, UINT32_MAX) && !tfn_pool_win_take(&w, UINT32_MAX));
    memset(&w, 0, sizeof w);
    CHECK(tfn_pool_win_take(&w, 200) && tfn_pool_win_take(&w, 200 - 63) && !tfn_pool_win_take(&w, 200 - 64));
}

static void test_rekey(void)
{
    static tfn_pool_rx_t rx;
    static tros_t x;
    static uint8_t rf[TFN_POOL_FILE_MAX];
    static tfn_pool_roster_t r;
    uint8_t f[260], mac[6], mac0[6];
    int slot, n;
    board_mac(1, mac);
    board_mac(0, mac0);
    rx_make(&rx, 3);
    n = make_frame(&s_k1, mac, 500, 8, f);
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_RX_OK);
    tfn_pool_rx_rekey(&rx, &s_k2);
    CHECK(rx.prev_on);
    n = make_frame(&s_k2, mac, 0, 8, f);  // a new epoch starts the counter at 0
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_RX_OK);
    n = make_frame(&s_k1, mac, 501, 8, f);  // the previous key in its hour
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_RX_OK);
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_DROP_OLD);
    n = make_frame(&s_k1, mac, 500, 8, f);
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_DROP_OLD);  // the old window stays with the old key
    tfn_pool_rx_rekey(&rx, &s_k2);                                        // the same key again: no change
    CHECK(rx.prev_on && rx.key[1].epoch == 1);
    rx.prev_on = false;  // the hour ends
    n = make_frame(&s_k1, mac, 502, 8, f);
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) ==
          (s_k1.ptag == s_k2.ptag ? TFN_POOL_DROP_BAD : TFN_POOL_DROP_OTHER));
    // a new roster: board 1 stays (its window stays), board 0 goes and comes back (a new window)
    n = make_frame(&s_k2, mac0, 10, 8, f);
    CHECK(tfn_pool_v4_open(&rx, mac0, f, n, &slot) == TFN_POOL_RX_OK && slot == 1);
    tr_good(&x, POOL_ID, 6, 3);
    tr_del(&x, tr_find(&x, TFN_POOL_T_MEMBER) + 1);  // board 0 out
    tfn_pool_roster_parse(rf, tr_raw(&x, rf), &r);
    tfn_pool_rx_roster(&rx, &r);
    CHECK(rx.n == 3 && tfn_pool_rx_slot(&rx, mac) == 1 && tfn_pool_rx_slot(&rx, mac0) == -1);
    CHECK(tfn_pool_v4_open(&rx, mac0, f, n, &slot) == TFN_POOL_DROP_WHO);
    n = make_frame(&s_k2, mac, 0, 8, f);
    CHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_DROP_OLD && rx.n == 3);
    tr_good(&x, POOL_ID, 7, 3);
    tfn_pool_roster_parse(rf, tr_raw(&x, rf), &r);
    tfn_pool_rx_roster(&rx, &r);
    n = make_frame(&s_k2, mac0, 10, 8, f);
    CHECK(tfn_pool_v4_open(&rx, mac0, f, n, &slot) == TFN_POOL_RX_OK && slot == 1);
    tfn_pool_rx_init(&rx, &r, &s_k2, &s_k1);
    CHECK(rx.prev_on && rx.key[1].epoch == 1 && rx.count[0] == 0);
}

// ---------------------------------------------------------------- the beacons

static void test_beacons(void)
{
    static tfn_pool_rx_t rx;
    uint8_t plain[43], back[43], pkt[78], g[78], nonce[12], mac[6], id[8];
    char name[17];
    fill(plain, 43, 3);
    fill(nonce, 12, 77);
    rx_make(&rx, 3);
    CHECK(tfn_pool_ap_seal(&OPS, &s_k1, nonce, plain, pkt));
    CHECK(pkt[0] == 'A' && pkt[1] == 'P' && pkt[2] == 1 && !memcmp(pkt + 3, s_k1.hint, 4) &&
          !memcmp(pkt + 7, nonce, 12));
    CHECK(tfn_pool_ap_open(&OPS, &rx, pkt, 78, back) == TFN_POOL_RX_OK && !memcmp(back, plain, 43));
    for (int i = 0; i < 78; i++) {
        memcpy(g, pkt, 78);
        g[i] ^= 0x01;
        CHECK(tfn_pool_ap_open(&OPS, &rx, g, 78, back) == (i >= 3 && i < 7 ? TFN_POOL_DROP_OTHER : TFN_POOL_DROP_BAD));
    }
    CHECK(tfn_pool_ap_open(&OPS, &rx, pkt, 77, back) == TFN_POOL_DROP_BAD);
    CHECK(tfn_pool_ap_seal(&OPS, &s_kx, nonce, plain, g));
    CHECK(tfn_pool_ap_open(&OPS, &rx, g, 78, back) == TFN_POOL_DROP_OTHER);  // another pool
    tfn_pool_rx_rekey(&rx, &s_k2);
    CHECK(tfn_pool_ap_open(&OPS, &rx, pkt, 78, back) == TFN_POOL_RX_OK);  // the previous key in its hour
    rx.prev_on = false;
    CHECK(tfn_pool_ap_open(&OPS, &rx, pkt, 78, back) != TFN_POOL_RX_OK);
    // 'A' 'J' 1
    uint8_t jm[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02}, jid[8] = {1, 2, 3, 4, 5, 6, 7, 8}, jb[33];
    tfn_pool_jbeacon_make(jm, jid, "BOARD2", jb);
    CHECK(jb[0] == 'A' && jb[1] == 'J' && jb[2] == 1 && jb[17] == 'B' && jb[23] == 0);
    CHECK(tfn_pool_jbeacon_parse(jb, 33, mac, id, name) && !memcmp(mac, jm, 6) && !memcmp(id, jid, 8));
    CHECK(!strcmp(name, "BOARD2") && !tfn_pool_jbeacon_parse(jb, 32, mac, id, name));
    tfn_pool_jbeacon_make(jm, jid, "ABCDEFGHIJKLMNO\xD7\xA9", jb);  // a long name: no cut letter
    CHECK(tfn_pool_jbeacon_parse(jb, 33, mac, id, name) && !strcmp(name, "ABCDEFGHIJKLMNO"));
    jb[2] = 2;
    CHECK(!tfn_pool_jbeacon_parse(jb, 33, mac, id, name));
}

// ---------------------------------------------------------------- the JOIN, the envelope, the web header

static void test_join(void)
{
    uint8_t em_k[32], eb_k[32], em[32], eb[32], nm[16], nb[16], c[32], k1[32], k2[32], nine[32] = {9};
    fill(em_k, 32, 1);
    fill(eb_k, 32, 2);
    fill(nm, 16, 3);
    fill(nb, 16, 4);
    x25519(em_k, nine, em);
    x25519(eb_k, nine, eb);
    tfn_pool_commit(&OPS, em, nm, c);
    uint8_t m[54], h[32];
    memcpy(m, "TFNJC1", 6);
    memcpy(m + 6, em, 32);
    memcpy(m + 38, nm, 16);
    hc_sha256(m, 54, h);
    CHECK(!memcmp(c, h, 32));
    uint32_t sas = tfn_pool_sas(&OPS, em, eb, nm, nb);
    CHECK(sas < 1000000 && sas != tfn_pool_sas(&OPS, eb, em, nm, nb));
    char t[8];
    tfn_pool_sas_text(482913, t);
    CHECK(!strcmp(t, "482 913"));
    tfn_pool_sas_text(7, t);
    CHECK(!strcmp(t, "000 007"));
    CHECK(tfn_pool_sas_parse("482913") == 482913 && tfn_pool_sas_parse("482 913") == 482913);
    CHECK(tfn_pool_sas_parse("000007") == 7 && tfn_pool_sas_parse("48291") == -1 &&
          tfn_pool_sas_parse("4829130") == -1);
    CHECK(tfn_pool_sas_parse("48 2913") == -1 && tfn_pool_sas_parse("482  913") == -1 && tfn_pool_sas_parse("") == -1);
    CHECK(tfn_pool_sas_parse("482a13") == -1 && tfn_pool_sas_parse(NULL) == -1 && tfn_pool_sas_parse(" 482913") == -1);
    uint8_t jm[TFN_POOL_JMSG_MAX];
    CHECK(tfn_pool_join_msg(c, eb, nb, NULL, NULL, jm) == 85 && !memcmp(jm, "TFNJ1", 5) && !memcmp(jm + 69, nb, 16));
    CHECK(tfn_pool_join_msg(c, eb, nb, em, nm, jm) == 133 && !memcmp(jm + 85, em, 32) && !memcmp(jm + 117, nm, 16));
    uint8_t bx[46], id[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    tfn_pool_box_msg(id, eb, bx);
    CHECK(!memcmp(bx, "TFNBX1", 6) && !memcmp(bx + 6, id, 8) && !memcmp(bx + 14, eb, 32));
    // the two sides make the same seal key; a low-order point gives all zero bytes: refused
    CHECK(tfn_pool_seal_key(&OPS, eb_k, em, c, 32, k1) && tfn_pool_seal_key(&OPS, em_k, eb, c, 32, k2));
    CHECK(!memcmp(k1, k2, 32));
    uint8_t zero[32] = {0};
    CHECK(!tfn_pool_seal_key(&OPS, eb_k, zero, c, 32, k2));
    // the envelope fits only its roster
    uint8_t roster[100], env[96], pkey[32], wk[32], pk2[32], wk2[32], nonce[12];
    uint32_t ep = 0;
    fill(roster, 100, 5);
    fill(pkey, 32, 6);
    fill(wk, 32, 7);
    fill(nonce, 12, 8);
    CHECK(tfn_pool_env_seal(&OPS, k1, nonce, roster, 100, 0x01020304, pkey, wk, env));
    CHECK(tfn_pool_env_open(&OPS, k1, roster, 100, env, 96, &ep, pk2, wk2));
    CHECK(ep == 0x01020304 && !memcmp(pk2, pkey, 32) && !memcmp(wk2, wk, 32));
    roster[50] ^= 1;
    CHECK(!tfn_pool_env_open(&OPS, k1, roster, 100, env, 96, &ep, pk2, wk2));
    roster[50] ^= 1;
    for (int i = 0; i < 96; i++) {
        env[i] ^= 0x80;
        CHECK(!tfn_pool_env_open(&OPS, k1, roster, 100, env, 96, &ep, pk2, wk2));
        env[i] ^= 0x80;
    }
    CHECK(!tfn_pool_env_open(&OPS, k1, roster, 100, env, 95, &ep, pk2, wk2));
}

static void test_web(void)
{
    static tros_t x;
    static uint8_t f[TFN_POOL_FILE_MAX];
    static tfn_pool_roster_t r;
    uint8_t wk[32], bn[16], mid[8], bid[8], id[8], mac[32];
    uint64_t ctr, last = 0;
    char h[TFN_POOL_AUTH_MAX], g[TFN_POOL_AUTH_MAX];
    fill(wk, 32, 1);
    fill(bn, 16, 2);
    tr_good(&x, POOL_ID, 5, 3);
    tfn_pool_roster_parse(f, tr_raw(&x, f), &r);
    tfn_pool_dev_id(&OPS, s_pk[0], mid);  // the manager
    tfn_pool_dev_id(&OPS, s_pk[3], bid);  // a board (no MANAGER role)
    int n = tfn_pool_auth_make(&OPS, mid, wk, bn, 1762300000123ULL, "POST", "/api/pool?op=set", h, sizeof h);
    CHECK(n == 16 + 1 + 13 + 1 + 64 && (int)strlen(h) == n);
    CHECK(tfn_pool_auth_make(&OPS, mid, wk, bn, 1, "GET", "/", g, 50) == -1);
    CHECK(tfn_pool_auth_parse(h, id, &ctr, mac) && !memcmp(id, mid, 8) && ctr == 1762300000123ULL);
    CHECK(tfn_pool_auth_check(&OPS, &r, wk, bn, "POST", "/api/pool?op=set", h, &last) && last == 1762300000123ULL);
    CHECK(!tfn_pool_auth_check(&OPS, &r, wk, bn, "POST", "/api/pool?op=set", h, &last));  // the same counter again
    last = 0;
    CHECK(!tfn_pool_auth_check(&OPS, &r, wk, bn, "GET", "/api/pool?op=set", h, &last));  // another method
    CHECK(!tfn_pool_auth_check(&OPS, &r, wk, bn, "POST", "/api/pool?op=key", h, &last));
    bn[0] ^= 1;
    CHECK(!tfn_pool_auth_check(&OPS, &r, wk, bn, "POST", "/api/pool?op=set", h, &last));  // a new start
    bn[0] ^= 1;
    for (int i = 0; i < n; i++) {
        memcpy(g, h, (size_t)n + 1);
        g[i] = g[i] == '1' ? '2' : '1';
        CHECK(!tfn_pool_auth_check(&OPS, &r, wk, bn, "POST", "/api/pool?op=set", g, &last));
    }
    CHECK(last == 0);
    tfn_pool_auth_make(&OPS, bid, wk, bn, 5, "GET", "/api/pool", h, sizeof h);
    CHECK(!tfn_pool_auth_check(&OPS, &r, wk, bn, "GET", "/api/pool", h, &last));  // not a MANAGER
    static const char *const BAD[] = {
            "",
            "0011223344556677.5.",
            "001122334455667.5.00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
            "0011223344556677.05.00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
            "0011223344556677..00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
            "0011223344556677.18446744073709551616.00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff",
            "0011223344556677.5.00112233445566778899aabbccddeeff00112233445566778899aabbccddeef",
            "0011223344556677.5.00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff0",
            "0011223344556677.5.00112233445566778899aabbccddeeff00112233445566778899aabbccddeefg",
            "0011223344556677 .5.00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"};
    for (size_t k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
        CHECK(!tfn_pool_auth_parse(BAD[k], id, &ctr, mac));
    }
    CHECK(tfn_pool_auth_parse("0011223344556677.18446744073709551615.00112233445566778899aabbccddeeff00112233445566"
                              "778899aabbccddeeff",
                              id, &ctr, mac) &&
          ctr == UINT64_MAX && mac[31] == 0xFF);
    CHECK(!tfn_pool_auth_parse("0011223344556677.5.00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF",
                               id, &ctr, mac));  // upper case hex: not the form of the manager
    CHECK(!tfn_pool_auth_parse(NULL, id, &ctr, mac));
}

// ---------------------------------------------------------------- the receive table, the counter, the legacy rule

static void test_take(void)
{
    // in: V3, PEER, WAKE1, WAKE2; the peer: member, manager, friend, keyed_other; the result: RELAXED, then STRICT
    static const struct {
        int in;
        bool member, manager, fr, keyed;
        bool relaxed, strict;
    } T[] = {
            {TFN_POOL_IN_V3, true, false, true, false, true, false},      // v3 with a roster MAC
            {TFN_POOL_IN_V3, false, false, true, true, true, false},      // a keyed friend of another pool
            {TFN_POOL_IN_V3, false, false, true, false, true, true},      // a legacy friend
            {TFN_POOL_IN_V3, false, false, false, false, true, true},     // a stranger
            {TFN_POOL_IN_PEER, false, false, false, false, true, false},  // not a member, not a friend
            {TFN_POOL_IN_PEER, true, false, false, false, true, true},
            {TFN_POOL_IN_PEER, false, false, true, true, true, true},
            {TFN_POOL_IN_WAKE1, true, true, true, false, true, false},   // WAKE v1 from the manager MAC
            {TFN_POOL_IN_WAKE1, false, false, true, false, true, true},  // an old manager: a legacy friend
            {TFN_POOL_IN_WAKE1, false, false, false, false, false, false},
            {TFN_POOL_IN_WAKE2, true, true, true, false, true, true},
            {TFN_POOL_IN_WAKE2, true, false, true, false, false, false},  // a member that is not a MANAGER
    };
    for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
        for (int strict = 0; strict < 2; strict++) {
            tfn_pool_peer_t p = {true, strict, T[k].member, T[k].manager, T[k].fr, T[k].keyed};
            CHECK(tfn_pool_take(T[k].in, &p) == (strict ? T[k].strict : T[k].relaxed));
            // no pool: as today (all packets; WAKE v1 from a friend; no WAKE v2)
            tfn_pool_peer_t q = {false, strict, false, false, T[k].fr, T[k].keyed};
            bool today = T[k].in == TFN_POOL_IN_WAKE1 ? T[k].fr : T[k].in != TFN_POOL_IN_WAKE2;
            CHECK(tfn_pool_take(T[k].in, &q) == today);
        }
    }
    tfn_pool_peer_t p = {true, true, true, true, true, false};
    CHECK(!tfn_pool_take(99, &p));
}

static void test_ctr_legacy(void)
{
    tfn_pool_ctr_t c;
    uint32_t v = 0, lc = 0;
    bool w = false;
    tfn_pool_ctr_init(&c, 131072);
    CHECK(tfn_pool_ctr_next(&c, &v, &w, &lc) && w && lc == 196608 && v == 131072);
    for (uint32_t i = 1; i < TFN_POOL_CTR_BLOCK; i++) {
        CHECK(tfn_pool_ctr_next(&c, &v, &w, &lc) && !w && v == 131072 + i);
    }
    CHECK(tfn_pool_ctr_next(&c, &v, &w, &lc) && w && lc == 262144 && v == 196608);  // the next block
    tfn_pool_ctr_init(&c, UINT32_MAX - 2 * TFN_POOL_CTR_BLOCK + 1);
    CHECK(tfn_pool_ctr_next(&c, &v, &w, &lc) && w && lc == UINT32_MAX - TFN_POOL_CTR_BLOCK + 1);
    c.next = c.end;
    CHECK(!tfn_pool_ctr_next(&c, &v, &w, &lc));  // no block left in this epoch
    tfn_pool_ctr_init(&c, 0);
    CHECK(tfn_pool_ctr_next(&c, &v, &w, &lc) && v == 0 && lc == TFN_POOL_CTR_BLOCK);
    // the legacy peer rule: 5 s after the last v3 packet of the app
    tfn_pool_legacy_t l;
    memset(&l, 0, sizeof l);
    CHECK(!tfn_pool_legacy_on(&l, 'P', 0));
    tfn_pool_legacy_note(&l, 'P', 1000);
    CHECK(tfn_pool_legacy_on(&l, 'P', 6000) && !tfn_pool_legacy_on(&l, 'P', 6001) &&
          !tfn_pool_legacy_on(&l, 'F', 1000));
    tfn_pool_legacy_note(&l, 'P', 0xFFFFF000u);
    CHECK(tfn_pool_legacy_on(&l, 'P', 0x00000100u));  // the ms clock goes round
    for (int i = 0; i < 20; i++) {
        tfn_pool_legacy_note(&l, (uint8_t)i, 2000000u + (uint32_t)i);
    }
    CHECK(tfn_pool_legacy_on(&l, 19, 2000020u) && tfn_pool_legacy_on(&l, 12, 2000020u) &&
          !tfn_pool_legacy_on(&l, 11, 2000020u));  // the 8 newest apps stay
}

// ---------------------------------------------------------------- the vector file (tests/unit/vectors/pool_v1.json)
// The file of the manager side (tests/unit/test_pool_crypto.py writes it with sdk/tools/pool.py). The test makes each
// value again with the core and the host crypto, and compares the bytes.

typedef struct jv {
    char t;  // 'o' object, 'a' list, 's' text, 'n' number, 'b' true or false, 'z' null
    const char *key;
    const char *s;
    long long num;
    struct jv *kid, *next;
} jv_t;

static jv_t *s_jv;
static int s_jn, s_jcap;

static void jws(char **p)
{
    while (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t') {
        (*p)++;
    }
}

// the 4 hex digits of a \u escape at s, or -1
static long hex4(const char *s)
{
    long v = 0;
    for (int i = 0; i < 4; i++) {
        int h = hexv(s[i]);
        if (h < 0) {
            return -1;
        }
        v = v << 4 | h;
    }
    return v;
}

// the UTF-8 bytes of the letter u at w; the next write place
static char *put_utf8(char *w, long u)
{
    if (u < 0x80) {
        *w++ = (char)u;
    } else if (u < 0x800) {
        *w++ = (char)(0xC0 | u >> 6);
        *w++ = (char)(0x80 | (u & 0x3F));
    } else if (u < 0x10000) {
        *w++ = (char)(0xE0 | u >> 12);
        *w++ = (char)(0x80 | (u >> 6 & 0x3F));
        *w++ = (char)(0x80 | (u & 0x3F));
    } else {
        *w++ = (char)(0xF0 | u >> 18);
        *w++ = (char)(0x80 | (u >> 12 & 0x3F));
        *w++ = (char)(0x80 | (u >> 6 & 0x3F));
        *w++ = (char)(0x80 | (u & 0x3F));
    }
    return w;
}

// a text at *p (after its quote), in place; the escapes \" \\ \/ \n \t and \uXXXX (also a surrogate pair)
static char *jstr(char **p)
{
    char *o = *p, *w = *p, *r = *p;
    while (*r && *r != '"') {
        long u = r[0] == '\\' && r[1] == 'u' ? hex4(r + 2) : -1;
        if (u >= 0xD800 && u < 0xDC00 && r[6] == '\\' && r[7] == 'u') {
            long lo = hex4(r + 8);
            if (lo >= 0xDC00 && lo < 0xE000) {
                w = put_utf8(w, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                r += 12;
                continue;
            }
        }
        if (u >= 0) {
            w = put_utf8(w, u);
            r += 6;
        } else if (*r == '\\' && r[1]) {
            r++;
            *w++ = *r == 'n' ? '\n' : *r == 't' ? '\t' : *r;
            r++;
        } else {
            *w++ = *r++;
        }
    }
    if (*r != '"') {
        return NULL;
    }
    *p = r + 1;
    *w = 0;
    return o;
}

static jv_t *jval(char **p, int depth)
{
    jws(p);
    if (s_jn >= s_jcap || depth > 32) {
        return NULL;
    }
    jv_t *v = &s_jv[s_jn++];
    memset(v, 0, sizeof *v);
    char c = **p;
    if (c == '{' || c == '[') {
        char end = c == '{' ? '}' : ']';
        v->t = c == '{' ? 'o' : 'a';
        (*p)++;
        jv_t **tail = &v->kid;
        jws(p);
        if (**p == end) {
            (*p)++;
            return v;
        }
        for (;;) {
            const char *key = NULL;
            if (c == '{') {
                jws(p);
                if (**p != '"') {
                    return NULL;
                }
                (*p)++;
                key = jstr(p);
                jws(p);
                if (!key || **p != ':') {
                    return NULL;
                }
                (*p)++;
            }
            jv_t *k = jval(p, depth + 1);
            if (!k) {
                return NULL;
            }
            k->key = key;
            *tail = k;
            tail = &k->next;
            jws(p);
            if (**p == ',') {
                (*p)++;
            } else if (**p == end) {
                (*p)++;
                return v;
            } else {
                return NULL;
            }
        }
    }
    if (c == '"') {
        (*p)++;
        v->t = 's';
        v->s = jstr(p);
        return v->s ? v : NULL;
    }
    if (!strncmp(*p, "true", 4) || !strncmp(*p, "false", 5) || !strncmp(*p, "null", 4)) {
        v->t = c == 'n' ? 'z' : 'b';
        v->num = c == 't';
        *p += c == 'f' ? 5 : 4;
        return v;
    }
    char *e;
    v->t = 'n';
    v->num = strtoll(*p, &e, 10);
    if (e == *p) {
        return NULL;
    }
    *p = e;
    return v;
}

static const jv_t *jk(const jv_t *o, const char *k)
{
    for (const jv_t *c = o ? o->kid : NULL; c; c = c->next) {
        if (c->key && !strcmp(c->key, k)) {
            return c;
        }
    }
    return NULL;
}

static long long jnum(const jv_t *o, const char *k)
{
    const jv_t *v = jk(o, k);
    return v ? v->num : -1;
}

static const char *js(const jv_t *o, const char *k)
{
    const jv_t *v = jk(o, k);
    return v && v->t == 's' ? v->s : "";
}

// a hex field into b (cap bytes): the length, or -1
static int jh(const jv_t *o, const char *k, uint8_t *b, int cap)
{
    return unhex(js(o, k), b, cap);
}

static int s_vn;  // the vector checks
#define VCHECK(c, sec, name)                                                                                           \
    do {                                                                                                               \
        s_vn++;                                                                                                        \
        if (!(c)) {                                                                                                    \
            s_fail++;                                                                                                  \
            printf("FAIL vector %s %s: %s\n", sec, name, #c);                                                          \
        }                                                                                                              \
    } while (0)
#define EACH(v, list) for (const jv_t *v = (list) ? (list)->kid : NULL; v; v = v->next)

static void vec_crypto(const jv_t *d)
{
    static uint8_t a[2048], b[2048], c[2048], e[2048];
    const jv_t *x = jk(d, "siphash");
    int i = 0;
    jh(x, "k", a, 16);
    for (int k = 0; k < 256; k++) {
        b[k] = (uint8_t)k;
    }
    EACH(o, jk(x, "out"))
    {
        uint8_t t[8];
        uint64_t v = tfn_pool_siphash(a, b, (size_t)i++, NULL, 0);
        for (int k = 0; k < 8; k++) {
            t[k] = (uint8_t)(v >> (8 * k));
        }
        VCHECK(unhex(o->s, c, 8) == 8 && !memcmp(t, c, 8), "siphash", "out");
    }
    VCHECK(i == 64, "siphash", "count");
    x = jk(d, "x25519");
    EACH(o, jk(x, "rfc7748_mul"))
    {
        jh(o, "scalar", a, 32), jh(o, "u", b, 32), jh(o, "out", c, 32);
        VCHECK(x25519(a, b, e) && !memcmp(e, c, 32), "x25519", "mul");
    }
    EACH(o, jk(x, "rfc7748_iter"))
    {
        uint8_t k[32] = {9}, u[32] = {9}, t[32];
        for (long long n = jnum(o, "n"); n > 0; n--) {
            x25519(k, u, t);
            memcpy(u, k, 32);
            memcpy(k, t, 32);
        }
        VCHECK(jh(o, "out", c, 32) == 32 && !memcmp(k, c, 32), "x25519", "iter");
    }
    const jv_t *dh = jk(x, "rfc7748_dh");
    uint8_t nine[32] = {9}, ap[32], bp[32], apub[32], bpub[32], sh[32];
    jh(dh, "alice_priv", ap, 32), jh(dh, "bob_priv", bp, 32), jh(dh, "alice_pub", apub, 32);
    jh(dh, "bob_pub", bpub, 32), jh(dh, "shared", sh, 32);
    x25519(ap, nine, a);
    x25519(bp, nine, b);
    x25519(ap, bpub, c);
    VCHECK(!memcmp(a, apub, 32) && !memcmp(b, bpub, 32) && !memcmp(c, sh, 32), "x25519", "dh");
    x = jk(d, "chacha20");
    const jv_t *bl = jk(x, "block");
    uint8_t key[32], nonce[12];
    jh(bl, "k", key, 32), jh(bl, "nonce", nonce, 12), jh(bl, "out", c, 64);
    chacha_block(key, (uint32_t)jnum(bl, "counter"), nonce, a);
    VCHECK(!memcmp(a, c, 64), "chacha20", "block");
    bl = jk(x, "enc");
    jh(bl, "k", key, 32), jh(bl, "nonce", nonce, 12);
    int pn = jh(bl, "plain", a, 1024);
    VCHECK(jh(bl, "out", c, 1024) == pn, "chacha20", "enc length");
    chacha_xor(key, (uint32_t)jnum(bl, "counter"), nonce, a, (size_t)pn, e);
    VCHECK(!memcmp(e, c, (size_t)pn), "chacha20", "enc");
    EACH(o, jk(d, "poly1305"))
    {
        int mn = jh(o, "msg", b, 1024);
        jh(o, "k", key, 32), jh(o, "tag", c, 16);
        poly1305(key, b, (size_t)mn, e);
        VCHECK(!memcmp(e, c, 16), "poly1305", "tag");
    }
    EACH(o, jk(d, "aead"))
    {
        uint8_t tag[16];
        jh(o, "k", key, 32), jh(o, "nonce", nonce, 12);
        int an = jh(o, "aad", a, 256), ln = jh(o, "plain", b, 1024), cn = jh(o, "ct", c, 1100);
        cn += jh(o, "tag", c + cn, 16);  // the tag after the ciphertext
        aead_seal(key, nonce, a, (size_t)an, b, (size_t)ln, e, tag);
        VCHECK(cn == ln + 16 && !memcmp(e, c, (size_t)ln) && !memcmp(tag, c + ln, 16), "aead", "seal");
        VCHECK(aead_open(key, nonce, a, (size_t)an, c, (size_t)ln, c + ln, e) && !memcmp(e, b, (size_t)ln), "aead",
               "open");
    }
    EACH(o, jk(d, "hkdf"))
    {
        int in = jh(o, "ikm", a, 256), sn = jh(o, "salt", b, 256), fn = jh(o, "info", c, 256);
        int on = (int)jnum(o, "n");
        uint8_t okm[512], want[512], prk[32], wp[32];
        const uint8_t *pp[1] = {a};
        size_t pl[1] = {(size_t)in};
        hmac(b, (size_t)sn, pp, pl, 1, prk);
        VCHECK(jh(o, "prk", wp, 32) == 32 && !memcmp(prk, wp, 32), "hkdf", "prk");
        VCHECK(jh(o, "okm", want, 512) == on &&
                       tfn_pool_hkdf(&OPS, b, (size_t)sn, a, (size_t)in, c, (size_t)fn, okm, (size_t)on) &&
                       !memcmp(okm, want, (size_t)on),
               "hkdf", "okm");
    }
}

// the keys of an epoch from the list "derive"
static bool vec_keys(const jv_t *d, long long epoch, tfn_pool_keys_t *k)
{
    EACH(o, jk(d, "derive"))
    {
        if (jnum(o, "epoch") == epoch) {
            uint8_t pk[32], pool[16];
            jh(o, "pool_k", pk, 32), jh(o, "pool", pool, 16);
            tfn_pool_derive(&OPS, pk, pool, (uint32_t)epoch, k);
            return true;
        }
    }
    return false;
}

static void vec_keys_ids(const jv_t *d)
{
    EACH(o, jk(d, "ids"))
    {
        uint8_t pk[65], id[8], kid[8], a[8], b[8];
        jh(o, "pub", pk, 65), jh(o, "device_id", id, 8), jh(o, "kid", kid, 8);
        tfn_pool_dev_id(&OPS, pk, a);
        tfn_pool_key_id(&OPS, pk, b);
        VCHECK(!memcmp(a, id, 8) && !memcmp(b, kid, 8), "ids", js(o, "name"));
    }
    EACH(o, jk(d, "derive"))
    {
        tfn_pool_keys_t k;
        uint8_t link[16], beacon[32], hint[4];
        vec_keys(d, jnum(o, "epoch"), &k);
        jh(o, "link", link, 16), jh(o, "beacon", beacon, 32), jh(o, "hint", hint, 4);
        VCHECK(!memcmp(k.link, link, 16) && k.ptag == jnum(o, "ptag") && !memcmp(k.beacon, beacon, 32) &&
                       !memcmp(k.hint, hint, 4),
               "derive", "keys");
    }
}

static void vec_roster_file(const jv_t *d)
{
    static uint8_t f[TFN_POOL_FILE_MAX + 8];
    static tfn_pool_roster_t r;
    uint8_t signer[65], b[65], kid[8];
    const jv_t *x = jk(d, "roster"), *fl = jk(x, "fields");
    int n = jh(x, "file", f, sizeof f);
    jh(x, "signer", signer, 65);
    VCHECK(tfn_pool_roster_parse(f, (size_t)n, &r) == 0, "roster", "parse");
    VCHECK(jh(x, "kid", kid, 8) == 8 && !memcmp(r.kid, kid, 8) && r.signed_len == (size_t)jnum(x, "signed_len"),
           "roster", "signature record");
    VCHECK(jh(fl, "pool", b, 16) == 16 && !memcmp(r.pool, b, 16) && !strcmp(r.name, js(fl, "name")), "roster", "id");
    VCHECK(r.ver == jnum(fl, "v") && r.epoch == jnum(fl, "epoch") && r.flags == jnum(fl, "flags") &&
                   (long long)r.time == jnum(fl, "time"),
           "roster", "numbers");
    VCHECK(!strcmp(r.lan, js(fl, "lan")) && !strcmp(r.vpn, js(fl, "vpn")), "roster", "urls");
    int i = 0;
    EACH(a, jk(fl, "admins"))
    {
        VCHECK(i < r.nadmin && unhex(a->s, b, 65) == 65 && !memcmp(r.admin[i], b, 65), "roster", "admin");
        i++;
    }
    VCHECK(i == r.nadmin, "roster", "admin count");
    i = 0;
    EACH(m, jk(fl, "members"))
    {
        uint8_t id[8], pk[65], mac[6];
        jh(m, "id", id, 8), jh(m, "pk", pk, 65), jh(m, "mac", mac, 6);
        VCHECK(i < r.nm && !memcmp(r.m[i].id, id, 8) && !memcmp(r.m[i].pk, pk, 65) && !memcmp(r.m[i].mac, mac, 6) &&
                       r.m[i].role == jnum(m, "role") && r.m[i].flags == jnum(m, "flags") &&
                       !strcmp(r.m[i].name, js(m, "name")),
               "roster", "member");
        i++;
    }
    VCHECK(i == r.nm, "roster", "member count");
    // the signature: a JOIN with the signer as the JOIN key and the last member as this board
    tfn_pool_trust_t t = {true, NULL, (const uint8_t (*)[65])signer, 1, 0, r.m[r.nm - 1].id};
    static tfn_pool_roster_t r2;
    VCHECK(tfn_pool_roster_check(&OPS, &t, f, (size_t)n, &r2) == TFN_POOL_R_OK, "roster", "signature");
    x = jk(d, "roster_min");
    n = jh(x, "file", f, sizeof f);
    jh(x, "signer", signer, 65);
    VCHECK(tfn_pool_roster_parse(f, (size_t)n, &r) == 0 && !r.lan[0] && !r.vpn[0], "roster_min", "parse");
    t.self = r.m[0].id;
    VCHECK(tfn_pool_roster_check(&OPS, &t, f, (size_t)n, &r2) == TFN_POOL_R_OK, "roster_min", "signature");
    EACH(o, jk(d, "roster_parse"))
    {
        n = jh(o, "file", f, sizeof f);
        int e = tfn_pool_roster_parse(f, (size_t)(n < 0 ? 0 : n), &r);
        if (n < 0) {
            e = TFN_POOL_E_SIZE;  // over the size of the buffer of the test (size_4097 has 4097 bytes)
            n = (int)strlen(js(o, "file")) / 2;
            VCHECK(n > TFN_POOL_FILE_MAX, "roster_parse", js(o, "name"));
        }
        VCHECK((e == 0) == (jk(o, "ok") && jk(o, "ok")->num), "roster_parse", js(o, "name"));
    }
}

static int rule_code(const char *s)
{
    static const struct {
        const char *name;
        int code;
    } R[] = {{"take", TFN_POOL_R_OK},     {"removed", TFN_POOL_R_REMOVED}, {"sig", TFN_POOL_E_SIG},
             {"admin", TFN_POOL_E_ADMIN}, {"other", TFN_POOL_E_POOL},      {"old", TFN_POOL_E_OLD},
             {"member", TFN_POOL_E_SELF}};
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) {
        if (!strcmp(R[i].name, s)) {
            return R[i].code;
        }
    }
    return 99;  // "form": any parse fault
}

static void vec_rules(const jv_t *d)
{
    static uint8_t f[TFN_POOL_FILE_MAX + 8], sf[TFN_POOL_FILE_MAX + 8];
    static tfn_pool_roster_t r, st;
    EACH(o, jk(d, "roster_rules"))
    {
        uint8_t own[8], ja[65];
        int n = jh(o, "new", f, sizeof f);
        int sn = jh(o, "stored", sf, sizeof sf);
        jh(o, "own_id", own, 8);
        bool has = sn > 0 && tfn_pool_roster_parse(sf, (size_t)sn, &st) == 0;
        bool join = jh(o, "join_admin", ja, 65) == 65;
        tfn_pool_trust_t t = {join,
                              has ? st.pool : NULL,
                              join ? (const uint8_t (*)[65])ja : (const uint8_t (*)[65])st.admin,
                              join ? 1 : (has ? st.nadmin : 0),
                              (uint32_t)jnum(o, "rv"),
                              own};
        int got = tfn_pool_roster_check(&OPS, &t, f, (size_t)n, &r);
        int want = rule_code(js(o, "want"));
        bool ok = want == 99 ? (got < 0 && got >= TFN_POOL_E_DUP) : got == want;
        if (!ok) {
            printf("  roster_rules %s: got %s\n", js(o, "name"), tfn_pool_err_text(got));
        }
        VCHECK(ok, "roster_rules", js(o, "name"));
    }
}

static int drop_code(const char *s)
{
    return !strcmp(s, "ok")      ? TFN_POOL_RX_OK
           : !strcmp(s, "other") ? TFN_POOL_DROP_OTHER
           : !strcmp(s, "who")   ? TFN_POOL_DROP_WHO
           : !strcmp(s, "old")   ? TFN_POOL_DROP_OLD
                                 : TFN_POOL_DROP_BAD;  // "bad" and "form"
}

// an rx with these MACs as the roster and the keys of this epoch (prev: the keys of the epoch before, or NULL)
static void vec_rx(tfn_pool_rx_t *rx, const uint8_t (*macs)[6], int n, const tfn_pool_keys_t *k,
                   const tfn_pool_keys_t *prev)
{
    static tfn_pool_roster_t r;
    memset(&r, 0, sizeof r);
    for (int i = 0; i < n && i < TFN_POOL_MEMBERS; i++) {
        memcpy(r.m[i].mac, macs[i], 6);
        r.nm = i + 1;
    }
    tfn_pool_rx_init(rx, &r, k, prev);
}

static void vec_frames(const jv_t *d)
{
    static tfn_pool_rx_t rx;
    uint8_t f[300], want[300], mac[6], macs[TFN_POOL_MEMBERS][6];
    tfn_pool_keys_t k, kp;
    EACH(o, jk(d, "frames"))
    {
        VCHECK(vec_keys(d, jnum(o, "epoch"), &k), "frames", "epoch");
        jh(o, "mac", mac, 6);
        int dn = jh(o, "data", f + 12, 240), wn = jh(o, "frame", want, 300);
        f[0] = 'A', f[1] = 'L', f[2] = 4, f[3] = (uint8_t)jnum(o, "app"), f[4] = (uint8_t)jnum(o, "kind");
        f[5] = (uint8_t)dn, f[6] = (uint8_t)jnum(o, "ch");
        int n = tfn_pool_v4_seal(&k, mac, (uint32_t)jnum(o, "ctr"), f, 12 + dn, 300);
        VCHECK(n == wn && !memcmp(f, want, (size_t)n), "frames", js(o, "name"));
    }
    const jv_t *x = jk(d, "frame_open");
    int nm = 0;
    EACH(m, jk(x, "members"))
    {
        unhex(m->s, macs[nm++], 6);
    }
    vec_keys(d, jnum(x, "epoch"), &k);
    vec_keys(d, jnum(x, "epoch") - 1, &kp);
    EACH(o, jk(x, "cases"))
    {
        int n = jh(o, "frame", f, 300), slot = -1;
        jh(o, "mac", mac, 6);
        vec_rx(&rx, (const uint8_t (*)[6])macs, nm, &k, NULL);
        VCHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == drop_code(js(o, "want")), "frame_open", js(o, "name"));
        if (!strcmp(js(o, "name"), "prev_key")) {
            vec_rx(&rx, (const uint8_t (*)[6])macs, nm, &k, &kp);  // in the hour of the previous key
            VCHECK(tfn_pool_v4_open(&rx, mac, f, n, &slot) == TFN_POOL_RX_OK, "frame_open", "prev_key in its hour");
        }
    }
    EACH(o, jk(d, "window"))
    {
        tfn_pool_win_t w;
        memset(&w, 0, sizeof w);
        const jv_t *ok = jk(o, "ok")->kid;
        EACH(c, jk(o, "ctr"))
        {
            VCHECK(ok && tfn_pool_win_take(&w, (uint32_t)c->num) == (ok->num != 0), "window", js(o, "name"));
            ok = ok ? ok->next : NULL;
        }
    }
}

static void vec_udp_beacon(const jv_t *d)
{
    static tfn_pool_rx_t rx;
    uint8_t p[300], want[300], macs[3][6];
    tfn_pool_keys_t k;
    const jv_t *x = jk(d, "udp"), *c = jk(x, "call"), *iv = jk(x, "invite");
    vec_keys(d, jnum(x, "epoch"), &k);
    p[0] = 'A', p[1] = 'C', p[2] = 2;
    jh(c, "from", p + 3, 6), jh(c, "to", p + 9, 6);
    p[15] = (uint8_t)jnum(c, "op");
    int wn = jh(c, "pkt", want, 300);
    int n = tfn_pool_udp2_seal(&k, (uint32_t)jnum(c, "ctr"), p, TFN_POOL_UDP_CALL, 300);
    VCHECK(n == wn && !memcmp(p, want, (size_t)n), "udp", "call");
    memset(p, 0, sizeof p);
    p[0] = 'A', p[1] = 'I', p[2] = 2;
    jh(iv, "from", p + 3, 6);
    memcpy(p + 9, js(iv, "app"), strlen(js(iv, "app")));
    wn = jh(iv, "pkt", want, 300);
    n = tfn_pool_udp2_seal(&k, (uint32_t)jnum(iv, "ctr"), p, TFN_POOL_UDP_INVITE, 300);
    VCHECK(n == wn && !memcmp(p, want, (size_t)n), "udp", "invite");
    EACH(o, jk(x, "open"))
    {
        n = jh(o, "pkt", p, 300);
        // the roster: the two senders and the "from" of this packet (the vectors have no roster: no WHO case)
        jh(c, "from", macs[0], 6), jh(iv, "from", macs[1], 6);
        memcpy(macs[2], n >= 9 ? p + 3 : macs[0], 6);
        vec_rx(&rx, (const uint8_t (*)[6])macs, 3, &k, NULL);
        VCHECK(tfn_pool_udp2_open(&rx, p, n, NULL) == drop_code(js(o, "want")), "udp open", js(o, "name"));
    }
    x = jk(d, "beacon");
    vec_keys(d, jnum(x, "epoch"), &k);
    EACH(o, jk(x, "list"))
    {
        uint8_t plain[43] = {0}, nonce[12], pkt[78], wp[43];
        jh(o, "mac", plain, 6);
        memcpy(plain + 6, js(o, "name"), strlen(js(o, "name")));
        memcpy(plain + 22, js(o, "app"), strlen(js(o, "app")));
        plain[34] = (uint8_t)jnum(o, "flags");
        jh(o, "id", plain + 35, 8);
        jh(o, "nonce", nonce, 12);
        VCHECK(jh(o, "plain", wp, 43) == 43 && !memcmp(plain, wp, 43), "beacon", "plain");
        VCHECK(tfn_pool_ap_seal(&OPS, &k, nonce, plain, pkt) && jh(o, "pkt", want, 78) == 78 && !memcmp(pkt, want, 78),
               "beacon", "seal");
    }
    uint8_t one[1][6] = {{2, 0, 0, 0, 0, 1}};
    vec_rx(&rx, (const uint8_t (*)[6])one, 1, &k, NULL);
    EACH(o, jk(x, "open"))
    {
        uint8_t plain[43];
        n = jh(o, "pkt", p, 300);
        VCHECK(tfn_pool_ap_open(&OPS, &rx, p, n, plain) == drop_code(js(o, "want")), "beacon open", js(o, "name"));
    }
    x = jk(d, "join_pkt");
    uint8_t mac[6], id[8], pkt[33], m2[6], i2[8];
    char name[17];
    jh(x, "mac", mac, 6), jh(x, "id", id, 8);
    tfn_pool_jbeacon_make(mac, id, js(x, "name"), pkt);
    VCHECK(jh(x, "pkt", want, 33) == 33 && !memcmp(pkt, want, 33), "join_pkt", "make");
    VCHECK(tfn_pool_jbeacon_parse(want, 33, m2, i2, name) && !memcmp(m2, mac, 6) && !memcmp(i2, id, 8) &&
                   !strcmp(name, js(x, "name")),
           "join_pkt", "parse");
}

static void vec_join(const jv_t *d)
{
    static uint8_t roster[TFN_POOL_FILE_MAX];
    uint8_t admin[65], bpk[65], bid[8], mid[8], emk[32], em[32], nm[16], ebk[32], eb[32], nb[16], bxk[32], bx[32];
    uint8_t c[32], w[512], m[TFN_POOL_JMSG_MAX], sig[64], k[32], env[96], e2[96], pk[32], wk[32], p2[32], w2[32];
    uint8_t nine[32] = {9}, t[32];
    uint32_t ep;
    const jv_t *x = jk(d, "sas");
    EACH(o, x)
    {
        jh(o, "em", em, 32), jh(o, "eb", eb, 32), jh(o, "nm", nm, 16), jh(o, "nb", nb, 16);
        char txt[8];
        uint32_t s = tfn_pool_sas(&OPS, em, eb, nm, nb);
        tfn_pool_sas_text(s, txt);
        VCHECK(s == jnum(o, "sas") && !strcmp(txt, js(o, "text")) && tfn_pool_sas_parse(txt) == (int32_t)s, "sas", txt);
    }
    x = jk(d, "join");
    jh(x, "admin", admin, 65), jh(x, "board_pk", bpk, 65), jh(x, "board_id", bid, 8), jh(x, "mid", mid, 8);
    jh(x, "em_priv", emk, 32), jh(x, "em_pub", em, 32), jh(x, "nm", nm, 16), jh(x, "eb_priv", ebk, 32);
    jh(x, "eb_pub", eb, 32), jh(x, "nb", nb, 16), jh(x, "bx_priv", bxk, 32), jh(x, "bx_pub", bx, 32);
    tfn_pool_dev_id(&OPS, bpk, w);
    tfn_pool_dev_id(&OPS, admin, w + 8);
    VCHECK(!memcmp(w, bid, 8) && !memcmp(w + 8, mid, 8), "join", "ids");
    x25519(emk, nine, w), x25519(ebk, nine, w + 32), x25519(bxk, nine, w + 64);
    VCHECK(!memcmp(w, em, 32) && !memcmp(w + 32, eb, 32) && !memcmp(w + 64, bx, 32), "join", "public keys");
    tfn_pool_box_msg(bid, bx, m);
    VCHECK(jh(x, "box_msg", w, 512) == 46 && !memcmp(m, w, 46), "join", "box_msg");
    VCHECK(jh(x, "bxs", sig, 64) == 64 && hc_p256_verify(bpk, m, 46, sig), "join", "bxs");
    tfn_pool_commit(&OPS, em, nm, c);
    VCHECK(jh(x, "c", w, 32) == 32 && !memcmp(c, w, 32), "join", "commit");
    int n = tfn_pool_join_msg(c, eb, nb, NULL, NULL, m);
    VCHECK(jh(x, "msg_b", w, 512) == n && !memcmp(m, w, (size_t)n), "join", "msg_b");
    VCHECK(jh(x, "sig_b", sig, 64) == 64 && hc_p256_verify(bpk, m, (size_t)n, sig), "join", "sig_b");
    n = tfn_pool_join_msg(c, eb, nb, em, nm, m);
    VCHECK(jh(x, "msg_m", w, 512) == n && !memcmp(m, w, (size_t)n), "join", "msg_m");
    VCHECK(jh(x, "sig_m", sig, 64) == 64 && hc_p256_verify(admin, m, (size_t)n, sig), "join", "sig_m");
    VCHECK(tfn_pool_sas(&OPS, em, eb, nm, nb) == jnum(x, "sas"), "join", "sas");
    VCHECK(jh(x, "shared", w, 32) == 32 && x25519(ebk, em, t) && !memcmp(t, w, 32) && x25519(emk, eb, t) &&
                   !memcmp(t, w, 32),
           "join", "shared");
    VCHECK(tfn_pool_seal_key(&OPS, ebk, em, c, 32, k) && jh(x, "seal_k", w, 32) == 32 && !memcmp(k, w, 32), "join",
           "seal_k (the board)");
    VCHECK(tfn_pool_seal_key(&OPS, emk, eb, c, 32, t) && !memcmp(k, t, 32), "join", "seal_k (the manager)");
    int rn = jh(x, "roster", roster, sizeof roster);
    hc_sha256(roster, (size_t)rn, t);
    VCHECK(jh(x, "roster_sha256", w, 32) == 32 && !memcmp(t, w, 32), "join", "roster_sha256");
    jh(x, "pool_k", pk, 32), jh(x, "wk", wk, 32);
    VCHECK(jh(x, "env", env, 96) == 96, "join", "env length");
    VCHECK(tfn_pool_env_seal(&OPS, k, env, roster, (size_t)rn, (uint32_t)jnum(x, "epoch"), pk, wk, e2) &&
                   !memcmp(e2, env, 96),
           "join", "env seal");
    VCHECK(tfn_pool_env_open(&OPS, k, roster, (size_t)rn, env, 96, &ep, p2, w2) && ep == jnum(x, "epoch") &&
                   !memcmp(p2, pk, 32) && !memcmp(w2, wk, 32),
           "join", "env open");
    EACH(o, jk(x, "env_open"))
    {
        int en = jh(o, "env", w, 512);
        bool want = jk(o, "ok") && jk(o, "ok")->num;
        VCHECK(tfn_pool_env_open(&OPS, k, roster, (size_t)rn, w, (size_t)(en < 0 ? 0 : en), &ep, p2, w2) == want,
               "join env_open", js(o, "name"));
    }
    x = jk(d, "rekey");
    jh(x, "board_id", bid, 8), jh(x, "bx_priv", bxk, 32), jh(x, "bx_pub", bx, 32), jh(x, "em_priv", emk, 32);
    jh(x, "em_pub", em, 32), jh(x, "pool_k", pk, 32), jh(x, "wk", wk, 32);
    rn = jh(x, "roster", roster, sizeof roster);
    VCHECK(jh(x, "shared", w, 32) == 32 && x25519(bxk, em, t) && !memcmp(t, w, 32), "rekey", "shared");
    VCHECK(tfn_pool_seal_key(&OPS, bxk, em, bid, 8, k) && jh(x, "seal_k", w, 32) == 32 && !memcmp(k, w, 32), "rekey",
           "seal_k");
    VCHECK(jh(x, "env", env, 96) == 96 && tfn_pool_env_open(&OPS, k, roster, (size_t)rn, env, 96, &ep, p2, w2) &&
                   ep == jnum(x, "epoch") && !memcmp(p2, pk, 32) && !memcmp(w2, wk, 32),
           "rekey", "env open");
}

static void vec_web(const jv_t *d)
{
    static tfn_pool_roster_t r;
    uint8_t wk[32], bn[16], mid[8], mac[32], want[32], id[8];
    char h[TFN_POOL_AUTH_MAX];
    uint64_t ctr;
    EACH(o, jk(d, "web"))
    {
        jh(o, "wk", wk, 32), jh(o, "bn", bn, 16), jh(o, "mid", mid, 8);
        ctr = (uint64_t)jnum(o, "ctr");
        tfn_pool_auth_mac(&OPS, wk, bn, ctr, js(o, "method"), js(o, "uri"), mac);
        VCHECK(jh(o, "mac", want, 32) == 32 && !memcmp(mac, want, 32), "web", "mac");
        tfn_pool_auth_make(&OPS, mid, wk, bn, ctr, js(o, "method"), js(o, "uri"), h, sizeof h);
        VCHECK(!strcmp(h, js(o, "header")), "web", "header");
        VCHECK(tfn_pool_auth_parse(js(o, "header"), id, &ctr, mac) && !memcmp(id, mid, 8) &&
                       ctr == (uint64_t)jnum(o, "ctr"),
               "web", "parse");
    }
    const jv_t *x = jk(d, "web_check");
    jh(x, "wk", wk, 32), jh(x, "bn", bn, 16), jh(x, "mid", mid, 8);
    memset(&r, 0, sizeof r);
    r.nm = 1;
    memcpy(r.m[0].id, mid, 8);
    r.m[0].role = TFN_POOL_ROLE_MANAGER;
    EACH(o, jk(x, "cases"))
    {
        uint64_t last = (uint64_t)jnum(o, "last");
        bool want_ok = jk(o, "ok") && jk(o, "ok")->num;
        bool got = tfn_pool_auth_check(&OPS, &r, wk, bn, js(x, "method"), js(x, "uri"), js(o, "header"), &last);
        VCHECK(got == want_ok && (!got || last > (uint64_t)jnum(o, "last")), "web_check", js(o, "name"));
    }
}

// the vector file: *n = the vector checks (0: no file)
static void test_vectors(const char *path, int *n)
{
    *n = 0;
    FILE *h = path ? fopen(path, "rb") : NULL;
    if (!h) {
        return;
    }
    fseek(h, 0, SEEK_END);
    long sz = ftell(h);
    fseek(h, 0, SEEK_SET);
    char *t = calloc(1, (size_t)sz + 1);
    bool good = fread(t, 1, (size_t)sz, h) == (size_t)sz;
    fclose(h);
    s_jcap = (int)sz / 2 + 16;
    s_jv = calloc((size_t)s_jcap, sizeof *s_jv);
    s_jn = 0;
    char *p = t;
    const jv_t *d = good ? jval(&p, 0) : NULL;
    CHECK(d && d->t == 'o' && !strcmp(js(d, "format"), "pool_v1"));
    if (d) {
        vec_crypto(d);
        vec_keys_ids(d);
        vec_roster_file(d);
        vec_rules(d);
        vec_frames(d);
        vec_udp_beacon(d);
        vec_join(d);
        vec_web(d);
    }
    free(s_jv);
    free(t);
    *n = s_vn;
    s_n += s_vn;
}

// ---------------------------------------------------------------- the random and changed inputs of each parser

static void mutate(uint8_t *b, int *n, int cap)
{
    int k = 1 + (int)(rnd() % 4);
    for (int i = 0; i < k; i++) {
        int w = (int)(rnd() % 8);
        if (w < 5 && *n > 0) {
            b[rnd() % (uint32_t)*n] ^= (uint8_t)(1u << (rnd() % 8));
        } else if (w == 5 && *n > 0) {
            b[rnd() % (uint32_t)*n] = (uint8_t)rnd();
        } else if (w == 6) {
            *n = (int)(rnd() % (uint32_t)(cap + 1));
        } else if (*n > 0) {
            int at = (int)(rnd() % (uint32_t)*n);
            b[at] = (uint8_t)(rnd() & 1 ? 0xFF : 0x00);
        }
    }
}

static void fuzz(long loops)
{
    static tros_t x;
    static uint8_t good[TFN_POOL_FILE_MAX], b[TFN_POOL_FILE_MAX + 16];
    static tfn_pool_roster_t r;
    static tfn_pool_rx_t rx;
    tr_good(&x, POOL_ID, 5, 4);
    int gn = (int)tr_raw(&x, good);
    long ok[7] = {0};
    for (long i = 0; i < loops; i++) {
        int n = gn;
        bool raw = rnd() % 8 == 0;
        if (raw) {
            n = (int)(rnd() % 700);
            for (int k = 0; k < n; k++) {
                b[k] = (uint8_t)rnd();
            }
            if (n >= 8 && rnd() % 2) {
                memcpy(b, TFN_POOL_MAGIC, 8);
            }
        } else {
            memcpy(b, good, (size_t)gn);
            mutate(b, &n, gn + 8);
        }
        ok[0] += tfn_pool_roster_parse(b, (size_t)n, &r) == 0;
    }
    // link v4, UDP v2, the beacon, the JOIN beacon, the web header, the envelope
    rx_make(&rx, 3);
    uint8_t mac[6], f[256], p[64], bc[78], plain[43], nonce[12] = {1}, jb[33], id[8], m2[6], env[96], ek[32] = {3};
    uint8_t wk[32] = {4}, bn[16] = {5}, roster[40] = {6}, pk[32], wk2[32], hm[32];
    char name[17], h[TFN_POOL_AUTH_MAX], hb[TFN_POOL_AUTH_MAX + 8];
    uint32_t ep;
    uint64_t ctr;
    board_mac(1, mac);
    int fn = make_frame(&s_k1, mac, 7, 60, f);
    memcpy(p, (const uint8_t[]){'A', 'C', 2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 5}, 16);
    memcpy(p + 3, mac, 6);
    int pn = tfn_pool_udp2_seal(&s_k1, 7, p, 16, 64);
    fill(plain, 43, 1);
    tfn_pool_ap_seal(&OPS, &s_k1, nonce, plain, bc);
    tfn_pool_jbeacon_make(mac, (const uint8_t *)"IDIDIDID", "DESK", jb);
    tfn_pool_env_seal(&OPS, ek, nonce, roster, sizeof roster, 9, wk, wk, env);
    tfn_pool_auth_make(&OPS, (const uint8_t *)"\1\2\3\4\5\6\7\10", wk, bn, 99, "GET", "/api/pool", h, sizeof h);
    for (long i = 0; i < loops; i++) {
        int n = fn;
        memcpy(b, f, (size_t)fn);
        mutate(b, &n, 255);
        ok[1] += tfn_pool_v4_open(&rx, mac, b, n, NULL) == TFN_POOL_RX_OK;
        n = pn;
        memcpy(b, p, (size_t)pn);
        mutate(b, &n, 63);
        ok[2] += tfn_pool_udp2_open(&rx, b, n, NULL) == TFN_POOL_RX_OK;
        n = 78;
        memcpy(b, bc, 78);
        mutate(b, &n, 80);
        ok[3] += tfn_pool_ap_open(&OPS, &rx, b, n, plain) == TFN_POOL_RX_OK;
        n = 33;
        memcpy(b, jb, 33);
        mutate(b, &n, 40);
        ok[4] += tfn_pool_jbeacon_parse(b, n, m2, id, name);
        n = (int)strlen(h);
        memcpy(hb, h, (size_t)n);
        mutate((uint8_t *)hb, &n, TFN_POOL_AUTH_MAX + 6);
        hb[n] = 0;
        ok[5] += tfn_pool_auth_parse(hb, id, &ctr, hm);
        n = 96;
        memcpy(b, env, 96);
        mutate(b, &n, 100);
        ok[6] += tfn_pool_env_open(&OPS, ek, roster, sizeof roster, b, (size_t)n, &ep, pk, wk2);
        if (i % 4096 == 0) {
            rx.win[0][2].on = false;  // the frame of the loop comes again as new
        }
    }
    CHECK(loops < 100000 || (ok[0] > 0 && ok[1] > 0 && ok[4] > 0 && ok[5] > 0));
    printf("fuzz: %ld inputs for each of 7 parsers; still good: roster %ld, v4 %ld, udp %ld, beacon %ld, join %ld, "
           "header %ld, envelope %ld\n",
           loops, ok[0], ok[1], ok[2], ok[3], ok[4], ok[5], ok[6]);
}

int main(int argc, char **argv)
{
    const char *vec = argc > 1 ? argv[1] : NULL;
    long loops = argc > 2 ? atol(argv[2]) : 1000000;
    keys_init();
    keys_make();
    test_kat();
    test_parse_fields();
    test_parse_values();
    test_parse_form();
    test_rules();
    test_v4();
    test_window();
    test_rekey();
    test_beacons();
    test_join();
    test_web();
    test_take();
    test_ctr_legacy();
    int nv = 0;
    test_vectors(vec, &nv);
    fuzz(loops);
    if (nv) {
        printf("vectors: %d checks of %s\n", nv, vec);
    } else {
        printf("vectors: no file %s (only the built-in vectors)\n", vec ? vec : "");
    }
    printf("POOL CORE %s (%d checks, %d failed)\n", s_fail ? "FAILED" : "OK", s_n, s_fail);
    return s_fail ? 1 : 0;
}
