// The unit test of the trusted publisher list (main/tfn_pub_core.c, tfn_pub.h): the key ID (against a value of Python
// hashlib, the same SHA-256 as sdk/tools/mk_app.py --dev-key), the list (add, remove, the NVS blob), the wait state of
// a new key, the parsers of the files "sigs" and the key file, the check of an app, and a fuzz of the parsers and the
// check (random changes of good files). The ECDSA is a fake here: a signature = SHA-256(key || msg) and SHA-256(msg).
// Build and run: python tests/unit/run.py
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn_pub.h"

static int s_fail, s_n;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        s_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            s_fail++;                                                                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                        \
        }                                                                                                              \
    } while (0)

// ---------------------------------------------------------------- a small SHA-256 (FIPS 180-4)
static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
#define ROR(x, n) ((x) >> (n) | (x) << (32 - (n)))

static void sha_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], a[8];
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(a, h, sizeof a);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 =
                a[7] + (ROR(a[4], 6) ^ ROR(a[4], 11) ^ ROR(a[4], 25)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + K[i] + w[i];
        uint32_t t2 = (ROR(a[0], 2) ^ ROR(a[0], 13) ^ ROR(a[0], 22)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        memmove(a + 1, a, 7 * sizeof a[0]);
        a[4] += t1;
        a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) {
        h[i] += a[i];
    }
}

static void sha256(const void *d, size_t n, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint8_t *p = d;
    size_t left = n;
    uint8_t b[128];
    for (; left >= 64; left -= 64, p += 64) {
        sha_block(h, p);
    }
    memset(b, 0, sizeof b);
    memcpy(b, p, left);
    b[left] = 0x80;
    size_t tot = left + 9 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) {
        b[tot - 1 - i] = (uint8_t)(bits >> (8 * i));
    }
    sha_block(h, b);
    if (tot == 128) {
        sha_block(h, b + 64);
    }
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)h[i];
    }
}

// ---------------------------------------------------------------- the fake ECDSA
static void fake_sign(const uint8_t pk[65], const uint8_t *m, size_t n, uint8_t sig[64])
{
    uint8_t *b = malloc(65 + n + 1);
    memcpy(b, pk, 65);
    memcpy(b + 65, m, n);
    sha256(b, 65 + n, sig);
    sha256(m, n, sig + 32);
    free(b);
}
static int s_verifies;
static bool fake_verify(const uint8_t pk[65], const uint8_t *m, size_t n, const uint8_t sig[64])
{
    uint8_t s[64];
    s_verifies++;
    fake_sign(pk, m, n, s);
    return !memcmp(s, sig, 64);
}
static uint8_t BUILTIN1[65], BUILTIN2[65];  // (key 2 all zeros: no key)
static bool fake_builtin(const uint8_t *m, size_t n, const uint8_t *sig, size_t sl)
{
    return sl == 64 && fake_verify(BUILTIN1, m, n, sig);
}
static tfn_pub_ops_t OPS = {sha256, fake_verify, fake_builtin, {BUILTIN1, BUILTIN2}};

static void make_key(uint8_t pk[65], int seed)
{
    pk[0] = 4;
    for (int i = 1; i < 65; i++) {
        pk[i] = (uint8_t)(seed * 31 + i * 7);
    }
}
static tfn_pub_t make_pub(int seed, const char *name, int flags)
{
    tfn_pub_t p;
    memset(&p, 0, sizeof p);
    make_key(p.pk, seed);
    tfn_pub_name_set(p.name, name);
    p.flags = (uint8_t)flags;
    return p;
}
static void hex(char *o, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++) {
        sprintf(o + 2 * i, "%02x", b[i]);
    }
}
// a "sigs" line of the key pk over m
static int sigs_line(char *o, const uint8_t pk[65], const uint8_t *m, size_t n)
{
    uint8_t id[8], sig[64];
    tfn_pub_key_id(&OPS, pk, id);
    fake_sign(pk, m, n, sig);
    memcpy(o, "TFNSIG2 ", 8);
    hex(o + 8, id, 8);
    o[24] = ' ';
    hex(o + 25, sig, 64);
    o[153] = '\n';
    o[154] = 0;
    return 154;
}
// a manifest with publisher= of the key pk (NULL: none)
static int manifest(char *o, const uint8_t *pk)
{
    int k = sprintf(o, "id=demo\nname=DEMO\nver=1.0.0\nkind=lcd349\n");
    if (pk) {
        uint8_t id[8];
        tfn_pub_key_id(&OPS, pk, id);
        k += sprintf(o + k, "publisher=");
        hex(o + k, id, 8);
        k += 16;
        o[k++] = '\n';
    }
    k += sprintf(o + k, "elf_sha256=00\n");
    return k;
}

// ---------------------------------------------------------------- the tests
static void test_id(void)
{
    uint8_t pk[65], id[8], back[8], h[32];
    char t[20];
    sha256("abc", 3, h);
    CHECK(h[0] == 0xba && h[1] == 0x78 && h[31] == 0xad);  // (the FIPS vector)
    pk[0] = 4;
    for (int i = 1; i < 65; i++) {
        pk[i] = (uint8_t)i;
    }
    tfn_pub_key_id(&OPS, pk, id);
    tfn_pub_id_text(id, t);
    CHECK(!strcmp(t, "0ED3-A6AB-957F-F6F5"));  // (Python: hashlib.sha256(pk).hexdigest()[:16])
    CHECK(tfn_pub_id_parse(t, back) && !memcmp(back, id, 8));
    CHECK(tfn_pub_id_parse("0ed3a6ab957ff6f5", back) && !memcmp(back, id, 8));
    CHECK(tfn_pub_id_parse("0ED3 A6AB 957F F6F5", back));
    CHECK(!tfn_pub_id_parse("0ED3-A6AB-957F-F6F", back));
    CHECK(!tfn_pub_id_parse("0ED3-A6AB-957F-F6F55", back));
    CHECK(!tfn_pub_id_parse("0ED3-A6AB-957F-F6FX", back));
    CHECK(!tfn_pub_id_parse("", back));
    CHECK(!tfn_pub_id_parse(NULL, back));
}

static void test_words(void)
{
    char b[24], n[TFN_PUB_NAME];
    CHECK(tfn_pub_flags_parse("store") == TFN_PUB_STORE);
    CHECK(tfn_pub_flags_parse("DEV,test") == (TFN_PUB_DEV | TFN_PUB_TEST));
    CHECK(tfn_pub_flags_parse("store+dev+test") == TFN_PUB_FLAGS);
    CHECK(tfn_pub_flags_parse("") == -1);
    CHECK(tfn_pub_flags_parse("admin") == -1);
    CHECK(tfn_pub_flags_parse("dev,") == TFN_PUB_DEV);
    CHECK(tfn_pub_flags_parse("devx") == -1);
    tfn_pub_flags_text(TFN_PUB_FLAGS, b, sizeof b);
    CHECK(!strcmp(b, "STORE+DEV+TEST"));
    tfn_pub_flags_text(TFN_PUB_STORE | TFN_PUB_TEST, b, sizeof b);
    CHECK(!strcmp(b, "STORE+TEST"));
    tfn_pub_flags_text(TFN_PUB_DEV, b, sizeof b);
    CHECK(!strcmp(b, "DEV"));
    CHECK(tfn_pub_name_set(n, "Tester dev-1") && !strcmp(n, "TESTER DEV-1"));
    CHECK(!tfn_pub_name_set(n, ""));
    CHECK(!tfn_pub_name_set(n, "A2345678901234567"));
    CHECK(tfn_pub_name_set(n, "A23456789012345"));
    CHECK(!tfn_pub_name_set(n, "BAD\"NAME"));
    CHECK(!tfn_pub_name_set(n, "BAD/NAME"));
}

static void test_list(void)
{
    tfn_pub_list_t l, l2;
    char msg[128];
    uint8_t id[8], blob[TFN_PUB_BLOB];
    memset(&l, 0, sizeof l);
    tfn_pub_t a = make_pub(1, "ALPHA", TFN_PUB_STORE), b = make_pub(2, "BETA", TFN_PUB_DEV);
    CHECK(tfn_pub_add(&OPS, &l, &a, msg, sizeof msg) && l.n == 1);
    CHECK(!tfn_pub_add(&OPS, &l, &a, msg, sizeof msg) && strstr(msg, "already"));
    CHECK(tfn_pub_add(&OPS, &l, &b, msg, sizeof msg) && l.n == 2);
    tfn_pub_t bi = make_pub(0, "SAME", TFN_PUB_STORE);
    memcpy(bi.pk, BUILTIN1, 65);
    CHECK(!tfn_pub_add(&OPS, &l, &bi, msg, sizeof msg) && strstr(msg, "built-in"));
    tfn_pub_t bad = make_pub(3, "BAD", TFN_PUB_DEV);
    bad.pk[0] = 2;
    CHECK(!tfn_pub_add(&OPS, &l, &bad, msg, sizeof msg));
    bad = make_pub(3, "BAD", 0);
    CHECK(!tfn_pub_add(&OPS, &l, &bad, msg, sizeof msg));
    bad = make_pub(3, "BAD", 8);
    CHECK(!tfn_pub_add(&OPS, &l, &bad, msg, sizeof msg));
    bad = make_pub(3, "BAD", TFN_PUB_DEV);
    bad.name[0] = 0;
    CHECK(!tfn_pub_add(&OPS, &l, &bad, msg, sizeof msg));
    for (int i = 10; l.n < TFN_PUB_MAX; i++) {
        tfn_pub_t p = make_pub(i, "MORE", TFN_PUB_DEV);
        CHECK(tfn_pub_add(&OPS, &l, &p, msg, sizeof msg));
    }
    tfn_pub_t full = make_pub(99, "FULL", TFN_PUB_DEV);
    CHECK(!tfn_pub_add(&OPS, &l, &full, msg, sizeof msg) && strstr(msg, "full"));
    // the blob
    size_t n = tfn_pub_pack(&l, blob, sizeof blob);
    CHECK(n == TFN_PUB_BLOB);
    CHECK(tfn_pub_unpack(&l2, blob, n) && l2.n == l.n && !memcmp(&l2.e[0], &l.e[0], sizeof l.e[0]));
    CHECK(!tfn_pub_unpack(&l2, blob, n - 1) && l2.n == 0);
    blob[0] = 'X';
    CHECK(!tfn_pub_unpack(&l2, blob, n));
    blob[0] = 'T';
    blob[4] = 9;
    CHECK(!tfn_pub_unpack(&l2, blob, n));
    blob[4] = (uint8_t)l.n;
    blob[5 + 65 + 16] = 0;  // (the flags of key 1: none)
    CHECK(!tfn_pub_unpack(&l2, blob, n));
    n = tfn_pub_pack(&l, blob, sizeof blob);
    memcpy(blob + 5 + 82, blob + 5, 65);  // (the same key twice)
    CHECK(!tfn_pub_unpack(&l2, blob, n));
    n = tfn_pub_pack(&l, blob, sizeof blob);
    memset(blob + 5 + 65, 'A', 16);  // (a name with no end)
    CHECK(!tfn_pub_unpack(&l2, blob, n));
    CHECK(tfn_pub_pack(&l, blob, 10) == 0);
    tfn_pub_list_t e;
    memset(&e, 0, sizeof e);
    n = tfn_pub_pack(&e, blob, sizeof blob);
    CHECK(n == 5 && tfn_pub_unpack(&l2, blob, n) && l2.n == 0);
    // remove
    tfn_pub_key_id(&OPS, a.pk, id);
    CHECK(tfn_pub_remove(&OPS, &l, id) && l.n == TFN_PUB_MAX - 1 && tfn_pub_find(&OPS, &l, id) < 0);
    CHECK(!tfn_pub_remove(&OPS, &l, id));
    tfn_pub_key_id(&OPS, b.pk, id);
    CHECK(tfn_pub_find(&OPS, &l, id) == 0);
}

static void test_files(void)
{
    char t[1200], pkx[131];
    tfn_pub_sig_t s[TFN_PUB_SIGS_MAX];
    uint8_t m[] = "id=x\n", pk[65], id[8];
    tfn_pub_t p;
    make_key(pk, 5);
    int k = sigs_line(t, pk, m, 5);
    CHECK(tfn_pub_sigs_parse(t, (size_t)k, s, 4) == 1);
    tfn_pub_key_id(&OPS, pk, id);
    CHECK(!memcmp(s[0].id, id, 8) && fake_verify(pk, m, 5, s[0].sig));
    // CRLF, a line with no end, a bad line between, more lines than the limit
    t[153] = '\r';
    t[154] = '\n';
    k = 155;
    k += sprintf(t + k, "TFNSIG3 future line\nTFNSIG2 zz\n");
    for (int i = 0; i < 5; i++) {
        k += sigs_line(t + k, pk, m, 5);
    }
    CHECK(tfn_pub_sigs_parse(t, (size_t)k, s, 4) == 4);
    tfn_pub_sig_t s16[16];
    CHECK(tfn_pub_sigs_parse(t, (size_t)k - 1, s16, 16) == 6);  // (the last line with no "\n")
    t[8] = 'G';
    CHECK(tfn_pub_sigs_parse(t, 155, s, 4) == 0);
    CHECK(tfn_pub_sigs_parse("", 0, s, 4) == 0);
    CHECK(tfn_pub_sigs_parse(NULL, 10, s, 4) == 0);
    // the manifest
    char mf[256];
    uint8_t id2[8];
    int ml = manifest(mf, pk);
    tfn_pub_key_id(&OPS, pk, id2);
    CHECK(tfn_pub_manifest_pub(mf, (size_t)ml, id) && !memcmp(id, id2, 8));
    ml = manifest(mf, NULL);
    CHECK(!tfn_pub_manifest_pub(mf, (size_t)ml, id));
    CHECK(!tfn_pub_manifest_pub("xpublisher=0011223344556677\n", 28, id));
    CHECK(tfn_pub_manifest_pub("publisher=0011223344556677", 26, id) && id[7] == 0x77);
    CHECK(!tfn_pub_manifest_pub("publisher=00112233445566", 24, id));
    // the key file
    hex(pkx, pk, 65);
    k = sprintf(t, "TFNPUB1\nname=Tester\nflags=dev,test\nkey=%s\n", pkx);
    CHECK(tfn_pub_file_parse(t, (size_t)k, &p) && !strcmp(p.name, "TESTER") &&
          p.flags == (TFN_PUB_DEV | TFN_PUB_TEST) && !memcmp(p.pk, pk, 65));
    k = sprintf(t, "TFNPUB1\r\nkey=%s\r\nname=X\r\n", pkx);
    CHECK(tfn_pub_file_parse(t, (size_t)k, &p) && p.flags == TFN_PUB_DEV);
    k = sprintf(t, "TFNPUB1\nname=X\nflags=root\nkey=%s\n", pkx);
    CHECK(!tfn_pub_file_parse(t, (size_t)k, &p));
    k = sprintf(t, "TFNPUB1\nname=X\n");
    CHECK(!tfn_pub_file_parse(t, (size_t)k, &p));
    k = sprintf(t, "TFNPUB2\nname=X\nkey=%s\n", pkx);
    CHECK(!tfn_pub_file_parse(t, (size_t)k, &p));
    pkx[0] = '0';
    pkx[1] = '2';  // (not an uncompressed point)
    k = sprintf(t, "TFNPUB1\nname=X\nkey=%s\n", pkx);
    CHECK(!tfn_pub_file_parse(t, (size_t)k, &p));
    hex(pkx, pk, 65);
    k = sprintf(t, "  %s \r\n", pkx);
    CHECK(tfn_pub_hex_key(t, (size_t)k, p.pk));
    CHECK(!tfn_pub_hex_key(t, (size_t)k - 6, p.pk));
}

static void test_wait(void)
{
    tfn_pub_list_t l;
    tfn_pub_wait_t w;
    char msg[128];
    memset(&l, 0, sizeof l);
    memset(&w, 0, sizeof w);
    tfn_pub_t a = make_pub(1, "ALPHA", TFN_PUB_DEV), b = make_pub(2, "BETA", TFN_PUB_TEST);
    CHECK(tfn_pub_wait_start(&w, &OPS, &l, &a, 1000, msg, sizeof msg) && l.n == 0);  // (no change before the tap)
    CHECK(w.seq == 1);  // (the number of the wait: each start gives the next one, an end keeps it)
    CHECK(!tfn_pub_wait_start(&w, &OPS, &l, &b, 2000, msg, sizeof msg) && strstr(msg, "waits") && w.seq == 1);
    CHECK(tfn_pub_wait_live(&w, 1000 + TFN_PUB_WAIT_MS - 1));
    CHECK(!tfn_pub_wait_live(&w, 1000 + TFN_PUB_WAIT_MS) && !w.on && w.seq == 1);
    CHECK(!tfn_pub_wait_answer(&w, &OPS, &l, true, 1000 + TFN_PUB_WAIT_MS, msg, sizeof msg) && l.n == 0);
    CHECK(tfn_pub_wait_start(&w, &OPS, &l, &b, 5000, msg, sizeof msg) && w.seq == 2);
    CHECK(!tfn_pub_wait_answer(&w, &OPS, &l, false, 6000, msg, sizeof msg) && l.n == 0 && !w.on && w.seq == 2);
    CHECK(tfn_pub_wait_start(&w, &OPS, &l, &b, 7000, msg, sizeof msg) && w.seq == 3);
    CHECK(!tfn_pub_wait_answer(&w, &OPS, &l, true, 7000 + TFN_PUB_WAIT_MS, msg, sizeof msg) && l.n == 0);
    CHECK(tfn_pub_wait_start(&w, &OPS, &l, &b, 9000, msg, sizeof msg) && w.seq == 4);
    CHECK(tfn_pub_wait_answer(&w, &OPS, &l, true, 9500, msg, sizeof msg) && l.n == 1 && !w.on && w.seq == 4);
    CHECK(!tfn_pub_wait_start(&w, &OPS, &l, &b, 9600, msg, sizeof msg) && strstr(msg, "already"));
    CHECK(!tfn_pub_wait_answer(&w, &OPS, &l, true, 9700, msg, sizeof msg) && l.n == 1);
    // the number after the last one is 1 again (0 is no wait)
    w.seq = UINT32_MAX;
    CHECK(tfn_pub_wait_start(&w, &OPS, &l, &a, 10000, msg, sizeof msg) && w.seq == 1);
}

// the ALLOW of the USB console ("pub allow <key ID>"): only the ID of the key that waits
static void test_allow_id(void)
{
    tfn_pub_list_t l;
    tfn_pub_wait_t w;
    char msg[160];
    uint8_t ia[8], ib[8];
    memset(&l, 0, sizeof l);
    memset(&w, 0, sizeof w);
    tfn_pub_t a = make_pub(5, "POOLSTORE", TFN_PUB_STORE), b = make_pub(6, "OTHER", TFN_PUB_STORE);
    tfn_pub_key_id(&OPS, a.pk, ia);
    tfn_pub_key_id(&OPS, b.pk, ib);
    // nothing waits: refused
    CHECK(!tfn_pub_wait_allow_id(&w, &OPS, &l, ia, 1000, msg, sizeof msg) && strstr(msg, "no key waits") && l.n == 0);
    CHECK(tfn_pub_wait_start(&w, &OPS, &l, &a, 1000, msg, sizeof msg));
    // a wrong ID: refused, the key still waits (the tap on the screen can still decide)
    CHECK(!tfn_pub_wait_allow_id(&w, &OPS, &l, ib, 2000, msg, sizeof msg) && strstr(msg, "not") && l.n == 0 && w.on);
    char t[20];
    tfn_pub_id_text(ia, t);
    CHECK(strstr(msg, "POOLSTORE") != NULL);
    // the right ID: the key is in the list, the wait ends
    CHECK(tfn_pub_wait_allow_id(&w, &OPS, &l, ia, 3000, msg, sizeof msg) && l.n == 1 && !w.on);
    CHECK(tfn_pub_find(&OPS, &l, ia) == 0 && l.e[0].flags == TFN_PUB_STORE);
    // the same ID again: nothing waits now
    CHECK(!tfn_pub_wait_allow_id(&w, &OPS, &l, ia, 3100, msg, sizeof msg) && l.n == 1);
    // the time ended: refused
    CHECK(tfn_pub_wait_start(&w, &OPS, &l, &b, 4000, msg, sizeof msg));
    CHECK(!tfn_pub_wait_allow_id(&w, &OPS, &l, ib, 4000 + TFN_PUB_WAIT_MS, msg, sizeof msg) && l.n == 1 && !w.on);
    // a key that can no longer go into the list (the list is full now): refused, the wait ends
    CHECK(tfn_pub_wait_start(&w, &OPS, &l, &b, 5000, msg, sizeof msg));
    for (int i = 0; l.n < TFN_PUB_MAX; i++) {
        tfn_pub_t f = make_pub(40 + i, "FILL", TFN_PUB_DEV);
        CHECK(tfn_pub_add(&OPS, &l, &f, msg, sizeof msg));
    }
    CHECK(!tfn_pub_wait_allow_id(&w, &OPS, &l, ib, 5100, msg, sizeof msg) && strstr(msg, "full") && !w.on);
    // the console ID forms: dashes, spaces, small letters
    uint8_t back[8];
    CHECK(tfn_pub_id_parse(t, back) && !memcmp(back, ia, 8));
}

static void test_check(void)
{
    tfn_pub_list_t l;
    char msg[128], who[TFN_PUB_NAME], mf[256], sg[1024];
    uint8_t sig[64], id[8];
    memset(&l, 0, sizeof l);
    tfn_pub_t st = make_pub(1, "SHOP", TFN_PUB_STORE), dv = make_pub(2, "TESTER", TFN_PUB_DEV),
              ts = make_pub(3, "MYTEST", TFN_PUB_TEST), x = make_pub(4, "STRANGER", TFN_PUB_DEV);
    tfn_pub_add(&OPS, &l, &st, msg, sizeof msg);
    tfn_pub_add(&OPS, &l, &dv, msg, sizeof msg);
    tfn_pub_add(&OPS, &l, &ts, msg, sizeof msg);
    // a store app of today: "sig" of built-in key 1, no publisher, no "sigs": one check only (the cache path)
    int ml = manifest(mf, NULL);
    fake_sign(BUILTIN1, (uint8_t *)mf, (size_t)ml, sig);
    s_verifies = 0;
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, sig, 64, NULL, 0, who) == TFN_PUB_OK_BUILTIN && !who[0] &&
          s_verifies == 1);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, sig, 63, NULL, 0, who) == TFN_PUB_NONE);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, NULL, 0, NULL, 0, who) == TFN_PUB_NONE);
    // an added STORE key in "sig"
    fake_sign(st.pk, (uint8_t *)mf, (size_t)ml, sig);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, sig, 64, NULL, 0, who) == TFN_PUB_OK_STORE &&
          !strcmp(who, "SHOP"));
    // a DEV key in "sig" is not a store signature
    fake_sign(dv.pk, (uint8_t *)mf, (size_t)ml, sig);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, sig, 64, NULL, 0, who) == TFN_PUB_NONE);
    // a developer package: publisher= + "sigs" of the DEV key = NOT REVIEWED; + the store countersign = OK
    ml = manifest(mf, dv.pk);
    int gl = sigs_line(sg, dv.pk, (uint8_t *)mf, (size_t)ml);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, NULL, 0, sg, (size_t)gl, who) == TFN_PUB_NOT_REVIEWED &&
          !strcmp(who, "TESTER"));
    fake_sign(BUILTIN1, (uint8_t *)mf, (size_t)ml, sig);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, sig, 64, sg, (size_t)gl, who) == TFN_PUB_OK_BUILTIN);
    // a "sigs" line of a key that is not the publisher: no pass
    gl = sigs_line(sg, ts.pk, (uint8_t *)mf, (size_t)ml);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, NULL, 0, sg, (size_t)gl, who) == TFN_PUB_NONE);
    // a TEST key alone is enough
    ml = manifest(mf, ts.pk);
    gl = sigs_line(sg, ts.pk, (uint8_t *)mf, (size_t)ml);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, NULL, 0, sg, (size_t)gl, who) == TFN_PUB_OK_TEST &&
          !strcmp(who, "MYTEST"));
    // a changed manifest after the signature: no pass
    mf[0] = 'X';
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, NULL, 0, sg, (size_t)gl, who) == TFN_PUB_NONE);
    mf[0] = 'i';
    // a key that is not in the list: no pass
    ml = manifest(mf, x.pk);
    gl = sigs_line(sg, x.pk, (uint8_t *)mf, (size_t)ml);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, NULL, 0, sg, (size_t)gl, who) == TFN_PUB_NONE && !who[0]);
    // a removed key: no pass any more
    ml = manifest(mf, ts.pk);
    gl = sigs_line(sg, ts.pk, (uint8_t *)mf, (size_t)ml);
    tfn_pub_key_id(&OPS, ts.pk, id);
    tfn_pub_remove(&OPS, &l, id);
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, NULL, 0, sg, (size_t)gl, who) == TFN_PUB_NONE);
    // a "sigs" file over the size limit is not read
    CHECK(tfn_pub_check(&OPS, &l, (uint8_t *)mf, (size_t)ml, NULL, 0, sg, TFN_PUB_SIGS_FILE + 1, who) == TFN_PUB_NONE);
}

// ---------------------------------------------------------------- the fuzz
static uint32_t s_rng = 12345;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static size_t mutate(uint8_t *b, size_t n, size_t cap)
{
    int k = 1 + (int)(rnd() % 6);
    for (int i = 0; i < k; i++) {
        uint32_t r = rnd() % 5;
        if (r == 0 && n) {
            b[rnd() % n] = (uint8_t)rnd();
        } else if (r == 1 && n) {
            b[rnd() % n] = "\n\r -=0aF"[rnd() % 8];
        } else if (r == 2 && n) {
            n = rnd() % (n + 1);  // (cut)
        } else if (r == 3 && n < cap) {
            size_t at = rnd() % (n + 1);
            memmove(b + at + 1, b + at, n - at);
            b[at] = (uint8_t)rnd();
            n++;
        } else if (r == 4 && n > 1) {
            size_t at = rnd() % n;
            memmove(b + at, b + at + 1, n - at - 1);
            n--;
        }
    }
    return n;
}

static void fuzz(int rounds)
{
    tfn_pub_list_t l;
    char msg[128], who[TFN_PUB_NAME];
    memset(&l, 0, sizeof l);
    tfn_pub_t dv = make_pub(2, "TESTER", TFN_PUB_DEV | TFN_PUB_TEST);
    tfn_pub_add(&OPS, &l, &dv, msg, sizeof msg);
    char mf0[256], sg0[1024], key0[300], pkx[131];
    int ml0 = manifest(mf0, dv.pk);
    int gl0 = sigs_line(sg0, dv.pk, (uint8_t *)mf0, (size_t)ml0);
    gl0 += sigs_line(sg0 + gl0, dv.pk, (uint8_t *)mf0, (size_t)ml0);
    hex(pkx, dv.pk, 65);
    int kl0 = sprintf(key0, "TFNPUB1\nname=X\nflags=dev\nkey=%s\n", pkx);
    uint8_t blob0[TFN_PUB_BLOB];
    size_t bl0 = tfn_pub_pack(&l, blob0, sizeof blob0);
    int passes = 0;
    for (int r = 0; r < rounds; r++) {
        // (each input in its own heap block of its exact size: ASan finds a read past the end)
        uint8_t buf[1100];
        size_t n;
        tfn_pub_sig_t s[TFN_PUB_SIGS_MAX];
        tfn_pub_t p;
        tfn_pub_list_t l2;
        uint8_t id[8];
        memcpy(buf, sg0, (size_t)gl0);
        n = mutate(buf, (size_t)gl0, sizeof buf);
        char *h = malloc(n ? n : 1);
        memcpy(h, buf, n);
        tfn_pub_sigs_parse(h, n, s, TFN_PUB_SIGS_MAX);
        passes += tfn_pub_check(&OPS, &l, (uint8_t *)mf0, (size_t)ml0, NULL, 0, h, n, who) != TFN_PUB_NONE;
        free(h);
        memcpy(buf, mf0, (size_t)ml0);
        n = mutate(buf, (size_t)ml0, sizeof buf);
        h = malloc(n ? n : 1);
        memcpy(h, buf, n);
        tfn_pub_manifest_pub(h, n, id);
        tfn_pub_check(&OPS, &l, (uint8_t *)h, n, NULL, 0, sg0, (size_t)gl0, who);
        free(h);
        memcpy(buf, key0, (size_t)kl0);
        n = mutate(buf, (size_t)kl0, sizeof buf);
        h = malloc(n ? n : 1);
        memcpy(h, buf, n);
        if (tfn_pub_file_parse(h, n, &p)) {
            CHECK(tfn_pub_key_set(p.pk) && p.flags && !(p.flags & ~TFN_PUB_FLAGS) && strlen(p.name) < TFN_PUB_NAME);
        }
        tfn_pub_hex_key(h, n, p.pk);
        free(h);
        memcpy(buf, blob0, bl0);
        n = mutate(buf, bl0, sizeof buf);
        h = malloc(n ? n : 1);
        memcpy(h, buf, n);
        if (tfn_pub_unpack(&l2, (uint8_t *)h, n)) {
            CHECK(l2.n >= 0 && l2.n <= TFN_PUB_MAX);
        }
        free(h);
    }
    CHECK(passes > 0);  // (some of the changed files are still good)
    printf("fuzz: %d rounds, %d sigs files still good\n", rounds, passes);
}

int main(void)
{
    make_key(BUILTIN1, 100);
    test_id();
    test_words();
    test_list();
    test_files();
    test_wait();
    test_allow_id();
    test_check();
    const char *fr = getenv("PUB_FUZZ");  // (a longer fuzz: PUB_FUZZ=<rounds>)
    fuzz(fr ? atoi(fr) : 40000);
    printf("PUB %s (%d checks, %d failed)\n", s_fail ? "FAILED" : "OK", s_n, s_fail);
    return s_fail ? 1 : 0;
}
