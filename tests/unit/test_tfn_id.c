// The unit test of the ID math of main/tfn_id.h: a known public key gives a known ID text. The expected texts come from
// Python: hashlib.sha256(b"TFNID1" + pk), the first 8 bytes in 4 groups of 4 hex digits.
// Build and run: python tests/unit/run.py
#include <stdio.h>
#include <string.h>
#include "tfn_id.h"

// ---- a small SHA-256 (FIPS 180-4) for the host: the board uses the PSA one

static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], v[8];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, h, sizeof v);
    for (int i = 0; i < 64; i++) {
        const uint32_t t1 =
                v[7] + (ror(v[4], 6) ^ ror(v[4], 11) ^ ror(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] + w[i];
        const uint32_t t2 =
                (ror(v[0], 2) ^ ror(v[0], 13) ^ ror(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        memmove(v + 1, v, 7 * sizeof v[0]);
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) {
        h[i] += v[i];
    }
}

static void sha256(const uint8_t *data, size_t n, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t last[128] = {0};
    size_t full = n / 64 * 64, rest = n - full;
    for (size_t i = 0; i < full; i += 64) {
        block(h, data + i);
    }
    memcpy(last, data + full, rest);
    last[rest] = 0x80;
    const size_t len = rest < 56 ? 64 : 128;
    const uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) {
        last[len - 1 - i] = (uint8_t)(bits >> (8 * i));
    }
    block(h, last);
    if (len == 128) {
        block(h, last + 64);
    }
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)h[i];
    }
}

// ---- the checks

static int s_fails;

static void hex_of(const uint8_t *d, int n, char *out)
{
    for (int i = 0; i < n; i++) {
        sprintf(out + 2 * i, "%02x", d[i]);
    }
}

static void check_sha(const char *msg, size_t n, const char *want)
{
    uint8_t d[32];
    char got[65];
    sha256((const uint8_t *)msg, n, d);
    hex_of(d, 32, got);
    if (strcmp(got, want)) {
        printf("FAIL sha256 of %zu bytes: %s, not %s\n", n, got, want);
        s_fails++;
    }
}

static void check_id(const uint8_t pk[TFN_ID_PK_LEN], const char *want)
{
    uint8_t id[8];
    char got[TFN_ID_LEN];
    memset(got, 'x', sizeof got);
    tfn_id_bytes(pk, sha256, id);
    tfn_id_text(id, got);
    if (strcmp(got, want) || strlen(got) != TFN_ID_LEN - 1) {
        printf("FAIL id: %.20s, not %s\n", got, want);
        s_fails++;
    }
}

int main(void)
{
    // the SHA-256 copy of the test: the FIPS 180-2 example and a message of more than one block
    check_sha("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    static char a1000[1000];
    memset(a1000, 'a', sizeof a1000);
    check_sha(a1000, sizeof a1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");

    // the public key = the generator point G of P-256 (0x04, Gx, Gy)
    static const uint8_t G[TFN_ID_PK_LEN] = {
            0x04, 0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47, 0xF8, 0xBC, 0xE6, 0xE5, 0x63, 0xA4, 0x40, 0xF2,
            0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0, 0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96, 0x4F,
            0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B, 0x8E, 0xE7, 0xEB, 0x4A, 0x7C, 0x0F, 0x9E, 0x16, 0x2B, 0xCE,
            0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE, 0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5,
    };
    check_id(G, "E4C0-7774-E8C0-DBFC");
    // the bytes 0x04, 1, 2 .. 64
    uint8_t p[TFN_ID_PK_LEN];
    p[0] = 4;
    for (int i = 1; i < TFN_ID_PK_LEN; i++) {
        p[i] = (uint8_t)i;
    }
    check_id(p, "198B-3918-AA25-2809");
    // one changed byte gives another ID
    p[64] ^= 1;
    uint8_t id[8];
    char t[TFN_ID_LEN];
    tfn_id_bytes(p, sha256, id);
    tfn_id_text(id, t);
    if (!strcmp(t, "198B-3918-AA25-2809")) {
        printf("FAIL id: a changed key gives the same ID\n");
        s_fails++;
    }
    printf("TFN ID %s\n", s_fails ? "FAILED" : "OK");
    return s_fails ? 1 : 0;
}
