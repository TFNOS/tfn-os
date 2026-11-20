// TFNCOMP: X25519 (RFC 7748) in pure C, after the public-domain TweetNaCl (D. J. Bernstein and others). A field
// element = 16 limbs of 16 bits in int64_t. The ladder is constant-time (no branch on a secret bit).
#include "tfncomp.h"
#include <string.h>

typedef int64_t gf[16];

static const gf K121665 = {0xDB41, 1};

static void car(gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += 65536;
        const int64_t c = o[i] >> 16;
        if (i < 15) {
            o[i + 1] += c - 1;
        } else {
            o[0] += 38 * (c - 1);
        }
        o[i] -= c * 65536;
    }
}

static void sel(gf p, gf q, int b)
{
    const int64_t c = ~((int64_t)b - 1);
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
        m[0] = t[0] - 0xFFED;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xFFFF - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xFFFF;
        }
        m[15] = t[15] - 0x7FFF - ((m[14] >> 16) & 1);
        const int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xFFFF;
        sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xFF);
        o[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xFF);
    }
}

static void unpack(gf o, const uint8_t *n)
{
    for (int i = 0; i < 16; i++) {
        o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    }
    o[15] &= 0x7FFF;
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
    int64_t t[31];
    memset(t, 0, sizeof t);
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (int i = 0; i < 15; i++) {
        t[i] += 38 * t[i + 16];
    }
    memcpy(o, t, sizeof(gf));
    car(o);
    car(o);
}

static void fsq(gf o, const gf a)
{
    fmul(o, a, a);
}

static void finv(gf o, const gf in)
{
    gf c;
    memcpy(c, in, sizeof c);
    for (int a = 253; a >= 0; a--) {
        fsq(c, c);
        if (a != 2 && a != 4) {
            fmul(c, c, in);
        }
    }
    memcpy(o, c, sizeof c);
}

void tc_x25519(uint8_t out[32], const uint8_t k[32], const uint8_t u[32])
{
    uint8_t z[32];
    gf x, a, b, c, d, e, f;
    memcpy(z, k, 32);
    z[31] = (uint8_t)((k[31] & 127) | 64);
    z[0] &= 248;
    unpack(x, u);
    for (int i = 0; i < 16; i++) {
        b[i] = x[i];
        d[i] = a[i] = c[i] = 0;
    }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        const int r = (z[i >> 3] >> (i & 7)) & 1;
        sel(a, b, r);
        sel(c, d, r);
        fadd(e, a, c);
        fsub(a, a, c);
        fadd(c, b, d);
        fsub(b, b, d);
        fsq(d, e);
        fsq(f, a);
        fmul(a, c, a);
        fmul(c, b, e);
        fadd(e, a, c);
        fsub(a, a, c);
        fsq(b, a);
        fsub(c, d, f);
        fmul(a, c, K121665);
        fadd(a, a, d);
        fmul(c, c, a);
        fmul(a, d, f);
        fmul(d, b, x);
        fsq(b, e);
        sel(a, b, r);
        sel(c, d, r);
    }
    finv(c, c);
    fmul(a, a, c);
    pack(out, a);
    memset(z, 0, sizeof z);
}

void tc_x25519_base(uint8_t pub[32], const uint8_t priv[32])
{
    static const uint8_t NINE[32] = {9};
    tc_x25519(pub, priv, NINE);
}
