// SHA-256 and ECDSA P-256 for the host unit tests (see host_crypto.h). The numbers: 8 limbs of 32 bits, the low limb
// first; the field and the scalar arithmetic in the Montgomery form; the points in Jacobian coordinates (a = -3).
#include "host_crypto.h"
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;

// ---------------------------------------------------------------- SHA-256
static const u32 K256[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static u32 ror(u32 x, int n)
{
    return x >> n | x << (32 - n);
}

static void sha_block(u32 h[8], const uint8_t *p)
{
    u32 w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        u32 s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ w[i - 15] >> 3;
        u32 s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ w[i - 2] >> 10;
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        u32 t1 = k + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        u32 t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
}

void hc_sha256(const uint8_t *data, size_t n, uint8_t out[32])
{
    u32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t i = 0;
    for (; i + 64 <= n; i += 64) {
        sha_block(h, data + i);
    }
    uint8_t last[128] = {0};
    size_t r = n - i;
    if (r) {
        memcpy(last, data + i, r);
    }
    last[r] = 0x80;
    size_t len = r + 9 <= 64 ? 64 : 128;
    u64 bits = (u64)n * 8;
    for (int j = 0; j < 8; j++) {
        last[len - 1 - j] = (uint8_t)(bits >> (8 * j));
    }
    sha_block(h, last);
    if (len == 128) {
        sha_block(h, last + 64);
    }
    for (int j = 0; j < 8; j++) {
        out[4 * j] = (uint8_t)(h[j] >> 24);
        out[4 * j + 1] = (uint8_t)(h[j] >> 16);
        out[4 * j + 2] = (uint8_t)(h[j] >> 8);
        out[4 * j + 3] = (uint8_t)h[j];
    }
}

// ---------------------------------------------------------------- 256-bit numbers
typedef struct {
    u32 m[8];    // the modulus
    u32 minv;    // -m^-1 mod 2^32
    u32 one[8];  // R mod m (R = 2^256): 1 in the Montgomery form
    u32 r2[8];   // R^2 mod m
} mod_t;

static void from_be(u32 a[8], const uint8_t *b)
{
    for (int i = 0; i < 8; i++) {
        const uint8_t *p = b + 28 - 4 * i;
        a[i] = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
    }
}

static void to_be(uint8_t *b, const u32 a[8])
{
    for (int i = 0; i < 8; i++) {
        uint8_t *p = b + 28 - 4 * i;
        p[0] = (uint8_t)(a[i] >> 24);
        p[1] = (uint8_t)(a[i] >> 16);
        p[2] = (uint8_t)(a[i] >> 8);
        p[3] = (uint8_t)a[i];
    }
}

static int cmp(const u32 a[8], const u32 b[8])
{
    for (int i = 7; i >= 0; i--) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

static bool is_zero(const u32 a[8])
{
    u32 o = 0;
    for (int i = 0; i < 8; i++) {
        o |= a[i];
    }
    return !o;
}

static u32 add(u32 r[8], const u32 a[8], const u32 b[8])
{
    u64 c = 0;
    for (int i = 0; i < 8; i++) {
        c += (u64)a[i] + b[i];
        r[i] = (u32)c;
        c >>= 32;
    }
    return (u32)c;
}

static u32 sub(u32 r[8], const u32 a[8], const u32 b[8])
{
    u64 bw = 0;
    for (int i = 0; i < 8; i++) {
        u64 d = (u64)a[i] - b[i] - bw;
        r[i] = (u32)d;
        bw = (d >> 32) & 1;
    }
    return (u32)bw;
}

static void madd(u32 r[8], const u32 a[8], const u32 b[8], const mod_t *M)
{
    u32 c = add(r, a, b);
    if (c || cmp(r, M->m) >= 0) {
        sub(r, r, M->m);
    }
}

static void msub(u32 r[8], const u32 a[8], const u32 b[8], const mod_t *M)
{
    if (sub(r, a, b)) {
        add(r, r, M->m);
    }
}

// r = a * b / R mod m (a, b < m)
static void mmul(u32 r[8], const u32 a[8], const u32 b[8], const mod_t *M)
{
    u32 t[10] = {0};
    for (int i = 0; i < 8; i++) {
        u64 c = 0;
        for (int j = 0; j < 8; j++) {
            c += (u64)t[j] + (u64)a[j] * b[i];
            t[j] = (u32)c;
            c >>= 32;
        }
        c += t[8];
        t[8] = (u32)c;
        t[9] = (u32)(c >> 32);
        u32 q = t[0] * M->minv;
        c = ((u64)t[0] + (u64)q * M->m[0]) >> 32;
        for (int j = 1; j < 8; j++) {
            c += (u64)t[j] + (u64)q * M->m[j];
            t[j - 1] = (u32)c;
            c >>= 32;
        }
        c += t[8];
        t[7] = (u32)c;
        t[8] = t[9] + (u32)(c >> 32);
    }
    u32 s[8];
    u32 bw = sub(s, t, M->m);
    memcpy(r, t[8] || !bw ? s : t, sizeof s);
}

static void mod_init(mod_t *M, const uint8_t be[32])
{
    from_be(M->m, be);
    u32 x = M->m[0];
    for (int i = 0; i < 5; i++) {
        x *= 2 - M->m[0] * x;
    }
    M->minv = 0 - x;
    u32 v[8] = {1};
    for (int i = 0; i < 512; i++) {
        madd(v, v, v, M);
        if (i == 255) {
            memcpy(M->one, v, sizeof v);
        }
    }
    memcpy(M->r2, v, sizeof v);
}

// r = a^e (a and r in the Montgomery form, e a plain number)
static void mpow(u32 r[8], const u32 a[8], const u32 e[8], const mod_t *M)
{
    u32 x[8];
    memcpy(x, M->one, sizeof x);
    for (int i = 255; i >= 0; i--) {
        mmul(x, x, x, M);
        if (e[i / 32] >> (i % 32) & 1) {
            mmul(x, x, a, M);
        }
    }
    memcpy(r, x, sizeof x);
}

// r = a^-1 (Montgomery form; a != 0)
static void minv(u32 r[8], const u32 a[8], const mod_t *M)
{
    u32 e[8], two[8] = {2};
    sub(e, M->m, two);
    mpow(r, a, e, M);
}

// ---------------------------------------------------------------- the curve
static const uint8_t P_BE[32] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const uint8_t N_BE[32] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17,
                                 0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51};
static const uint8_t B_BE[32] = {0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7, 0xb3, 0xeb, 0xbd,
                                 0x55, 0x76, 0x98, 0x86, 0xbc, 0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53,
                                 0xb0, 0xf6, 0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b};
static const uint8_t GX_BE[32] = {0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6,
                                  0xe5, 0x63, 0xa4, 0x40, 0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb,
                                  0x33, 0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96};
static const uint8_t GY_BE[32] = {0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb,
                                  0x4a, 0x7c, 0x0f, 0x9e, 0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31,
                                  0x5e, 0xce, 0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5};

typedef struct {
    u32 x[8], y[8], z[8];  // Jacobian, Montgomery form
    bool inf;
} pt_t;

static mod_t P, N;
static u32 B_M[8];
static pt_t G;

static void curve_init(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    mod_init(&P, P_BE);
    mod_init(&N, N_BE);
    u32 t[8];
    from_be(t, B_BE);
    mmul(B_M, t, P.r2, &P);
    from_be(t, GX_BE);
    mmul(G.x, t, P.r2, &P);
    from_be(t, GY_BE);
    mmul(G.y, t, P.r2, &P);
    memcpy(G.z, P.one, sizeof G.z);
    G.inf = false;
}

static void fmul(u32 r[8], const u32 a[8], const u32 b[8])
{
    mmul(r, a, b, &P);
}

static void fadd(u32 r[8], const u32 a[8], const u32 b[8])
{
    madd(r, a, b, &P);
}

static void fsub(u32 r[8], const u32 a[8], const u32 b[8])
{
    msub(r, a, b, &P);
}

static void pdbl(pt_t *r, const pt_t *a)
{
    if (a->inf || is_zero(a->y)) {
        r->inf = true;
        return;
    }
    u32 delta[8], gamma[8], beta[8], alpha[8], t1[8], t2[8];
    pt_t o = {.inf = false};
    fmul(delta, a->z, a->z);
    fmul(gamma, a->y, a->y);
    fmul(beta, a->x, gamma);
    fsub(t1, a->x, delta);
    fadd(t2, a->x, delta);
    fmul(alpha, t1, t2);
    fadd(t1, alpha, alpha);
    fadd(alpha, t1, alpha);
    fmul(o.x, alpha, alpha);
    fadd(t1, beta, beta);
    fadd(t1, t1, t1);  // 4 beta
    fadd(t2, t1, t1);  // 8 beta
    fsub(o.x, o.x, t2);
    fadd(o.z, a->y, a->z);
    fmul(o.z, o.z, o.z);
    fsub(o.z, o.z, gamma);
    fsub(o.z, o.z, delta);
    fsub(t1, t1, o.x);
    fmul(o.y, alpha, t1);
    fmul(t2, gamma, gamma);
    fadd(t2, t2, t2);
    fadd(t2, t2, t2);
    fadd(t2, t2, t2);
    fsub(o.y, o.y, t2);
    *r = o;
}

static void padd(pt_t *r, const pt_t *a, const pt_t *b)
{
    if (a->inf) {
        *r = *b;
        return;
    }
    if (b->inf) {
        *r = *a;
        return;
    }
    u32 z1z1[8], z2z2[8], u1[8], u2[8], s1[8], s2[8], h[8], rr[8], i4[8], j[8], v[8], t[8];
    fmul(z1z1, a->z, a->z);
    fmul(z2z2, b->z, b->z);
    fmul(u1, a->x, z2z2);
    fmul(u2, b->x, z1z1);
    fmul(s1, a->y, b->z);
    fmul(s1, s1, z2z2);
    fmul(s2, b->y, a->z);
    fmul(s2, s2, z1z1);
    fsub(h, u2, u1);
    fsub(rr, s2, s1);
    if (is_zero(h)) {
        if (is_zero(rr)) {
            pdbl(r, a);
        } else {
            r->inf = true;
        }
        return;
    }
    pt_t o = {.inf = false};
    fadd(rr, rr, rr);
    fadd(i4, h, h);
    fmul(i4, i4, i4);
    fmul(j, h, i4);
    fmul(v, u1, i4);
    fmul(o.x, rr, rr);
    fsub(o.x, o.x, j);
    fsub(o.x, o.x, v);
    fsub(o.x, o.x, v);
    fsub(t, v, o.x);
    fmul(o.y, rr, t);
    fmul(t, s1, j);
    fadd(t, t, t);
    fsub(o.y, o.y, t);
    fadd(t, a->z, b->z);
    fmul(t, t, t);
    fsub(t, t, z1z1);
    fsub(t, t, z2z2);
    fmul(o.z, t, h);
    *r = o;
}

static void pmul(pt_t *r, const pt_t *p, const u32 k[8])
{
    pt_t q = {.inf = true}, t;
    for (int i = 255; i >= 0; i--) {
        pdbl(&t, &q);
        q = t;
        if (k[i / 32] >> (i % 32) & 1) {
            padd(&t, &q, p);
            q = t;
        }
    }
    *r = q;
}

// the plain affine x and y of p (false: the point at infinity)
static bool affine(const pt_t *p, u32 x[8], u32 y[8])
{
    if (p->inf) {
        return false;
    }
    u32 zi[8], zi2[8], t[8], one[8] = {1};
    minv(zi, p->z, &P);
    fmul(zi2, zi, zi);
    fmul(t, p->x, zi2);
    fmul(x, t, one);
    fmul(t, p->y, zi2);
    fmul(t, t, zi);
    fmul(y, t, one);
    return true;
}

// a public key (0x04, X, Y) as a point: false when it is not on the curve
static bool load_point(pt_t *q, const uint8_t pub[65])
{
    u32 x[8], y[8], l[8], r[8], t[8];
    if (pub[0] != 0x04) {
        return false;
    }
    from_be(x, pub + 1);
    from_be(y, pub + 33);
    if (cmp(x, P.m) >= 0 || cmp(y, P.m) >= 0) {
        return false;
    }
    fmul(q->x, x, P.r2);
    fmul(q->y, y, P.r2);
    memcpy(q->z, P.one, sizeof q->z);
    q->inf = false;
    fmul(l, q->y, q->y);  // y^2
    fmul(t, q->x, q->x);
    fmul(r, t, q->x);  // x^3
    fadd(t, q->x, q->x);
    fadd(t, t, q->x);  // 3x
    fsub(r, r, t);
    fadd(r, r, B_M);
    return cmp(l, r) == 0;
}

static bool scalar(u32 a[8], const uint8_t be[32])
{
    from_be(a, be);
    return !is_zero(a) && cmp(a, N.m) < 0;
}

static void hash_scalar(u32 e[8], const uint8_t *msg, size_t n)
{
    uint8_t h[32];
    hc_sha256(msg, n, h);
    from_be(e, h);
    if (cmp(e, N.m) >= 0) {
        sub(e, e, N.m);
    }
}

bool hc_p256_pub(const uint8_t d[32], uint8_t pub[65])
{
    curve_init();
    u32 dd[8], x[8], y[8];
    pt_t q;
    if (!scalar(dd, d)) {
        return false;
    }
    pmul(&q, &G, dd);
    if (!affine(&q, x, y)) {
        return false;
    }
    pub[0] = 0x04;
    to_be(pub + 1, x);
    to_be(pub + 33, y);
    return true;
}

bool hc_p256_sign(const uint8_t d[32], const uint8_t k[32], const uint8_t *msg, size_t n, uint8_t sig[64])
{
    curve_init();
    u32 dd[8], kk[8], x[8], y[8], r[8], e[8], km[8], kinv[8], rm[8], rd[8], t[8], s[8];
    pt_t q;
    if (!scalar(dd, d) || !scalar(kk, k)) {
        return false;
    }
    pmul(&q, &G, kk);
    if (!affine(&q, x, y)) {
        return false;
    }
    memcpy(r, x, sizeof r);
    if (cmp(r, N.m) >= 0) {
        sub(r, r, N.m);
    }
    if (is_zero(r)) {
        return false;
    }
    hash_scalar(e, msg, n);
    mmul(km, kk, N.r2, &N);
    minv(kinv, km, &N);
    mmul(rm, r, N.r2, &N);
    mmul(rd, rm, dd, &N);  // r d (plain)
    madd(t, e, rd, &N);
    mmul(s, t, kinv, &N);  // (e + r d) / k (plain)
    if (is_zero(s)) {
        return false;
    }
    to_be(sig, r);
    to_be(sig + 32, s);
    return true;
}

bool hc_p256_verify(const uint8_t pub[65], const uint8_t *msg, size_t n, const uint8_t sig[64])
{
    curve_init();
    pt_t q, a, b, sum;
    u32 r[8], s[8], e[8], sm[8], w[8], u1[8], u2[8], x[8], y[8];
    if (!load_point(&q, pub) || !scalar(r, sig) || !scalar(s, sig + 32)) {
        return false;
    }
    hash_scalar(e, msg, n);
    mmul(sm, s, N.r2, &N);
    minv(w, sm, &N);
    mmul(u1, e, w, &N);  // e / s (plain)
    mmul(u2, r, w, &N);  // r / s (plain)
    pmul(&a, &G, u1);
    pmul(&b, &q, u2);
    padd(&sum, &a, &b);
    if (!affine(&sum, x, y)) {
        return false;
    }
    if (cmp(x, N.m) >= 0) {
        sub(x, x, N.m);
    }
    return cmp(x, r) == 0;
}
