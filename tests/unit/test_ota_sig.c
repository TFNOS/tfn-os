// The unit test of the trailer check of the signed OTA (main/ota_sig.c): the host crypto, each field wrong one time,
// a cut trailer, nsig 0 and 5, two signatures (one bad, one good), the modes, and the vectors that sdk/tools/fw_sign.py
// signed (the folder in argv[1], made by tests/unit/test_ota_sig.py).
// Build and run: python tests/unit/run.py
#include "ota_test_util.h"

#define LEN 70000

static uint8_t g_keys[OTA_SIG_KEYS][65];  // the key list of the board: test keys 0 and 1
static uint8_t g_tr[OTA_SIG_TRAILER];
static uint8_t g_hash[32];

static ota_sig_res_t run(const uint8_t *tr, size_t n, uint32_t minsv, ota_sig_result_t *r)
{
    ota_sig_want_t w = {.keys = g_keys, .nkeys = OTA_SIG_KEYS, .kind = "lcd349", .version = "1.0.97", .minsv = minsv};
    return ota_sig_check(tr, n, LEN + OTA_SIG_TRAILER, g_hash, &w, &TU_CRYPTO, r);
}

static void crypto_self_test(void)
{
    static const uint8_t ABC[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                    0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                    0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    uint8_t h[32];
    hc_sha256((const uint8_t *)"abc", 3, h);
    CHECK(!memcmp(h, ABC, 32), "SHA-256 of abc");
    // 1 x G = the generator
    static const uint8_t GX[4] = {0x6b, 0x17, 0xd1, 0xf2};
    uint8_t one[32] = {0}, pub[65], d[32], sig[64], k[32];
    one[31] = 1;
    CHECK(hc_p256_pub(one, pub) && !memcmp(pub + 1, GX, 4), "1 G = G");
    tu_key(0, d, pub);
    memset(k, 0x5a, 32);
    CHECK(hc_p256_sign(d, k, (const uint8_t *)"text", 4, sig), "sign");
    CHECK(hc_p256_verify(pub, (const uint8_t *)"text", 4, sig), "verify a good signature");
    CHECK(!hc_p256_verify(pub, (const uint8_t *)"texT", 4, sig), "another text");
    sig[63] ^= 1;
    CHECK(!hc_p256_verify(pub, (const uint8_t *)"text", 4, sig), "a changed s");
    sig[63] ^= 1;
    uint8_t pub2[65];
    tu_key(1, d, pub2);
    CHECK(!hc_p256_verify(pub2, (const uint8_t *)"text", 4, sig), "another key");
    pub[64] ^= 1;
    CHECK(!hc_p256_verify(pub, (const uint8_t *)"text", 4, sig), "a point that is not on the curve");
}

// the trailer of the test image, signed by the keys given, into g_tr
static void make(const char *kind, const char *ver, uint32_t sv, int nsig, const int *keys, const uint8_t *img)
{
    tu_trailer(g_tr, img, LEN, kind, ver, sv, nsig, keys);
}

static void field_tests(void)
{
    uint8_t d[32];
    tu_key(0, d, g_keys[0]);
    tu_key(1, d, g_keys[1]);
    uint8_t *img = tu_image(LEN, "lcd349", "1.0.97", 30000, 7);
    hc_sha256(img, LEN, g_hash);
    ota_sig_result_t r;
    uint8_t id1[8];
    CHECK(ota_sig_key_id(&TU_CRYPTO, g_keys[1], id1), "key ID");

    const int k0[] = {0}, k1[] = {1}, kx[] = {-1}, k01[] = {0, 1}, kx1[] = {-1, 1};
    make("lcd349", "1.0.97", 0, 1, k0, img);
    CHECK(run(g_tr, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_OK && r.key == 0 && r.has_sv, "good, key 0: %s",
          ota_sig_code(r.res));
    char t[24];
    ota_sig_text(&r, t, sizeof t);
    CHECK(!strncmp(t, "ok:", 3) && strlen(t) == 19, "last_sig text %s", t);
    make("lcd349", "1.0.97", 0, 1, k1, img);
    CHECK(run(g_tr, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_OK && r.key == 1 && !memcmp(r.key_id, id1, 8),
          "good, key 1 only");
    make("lcd349", "1.0.97", 0, 1, kx, img);
    CHECK(run(g_tr, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_KEY && r.key == -1, "a key that is not in the list");
    ota_sig_text(&r, t, sizeof t);
    CHECK(!strcmp(t, "sig_key"), "last_sig text %s", t);

    // two signatures: the first one bad, the second one good; an unknown key, then a good one
    make("lcd349", "1.0.97", 0, 2, k01, img);
    g_tr[128 + 8 + 40] ^= 0x01;
    CHECK(run(g_tr, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_OK && r.key == 1, "first bad, second good");
    g_tr[200 + 8 + 40] ^= 0x01;
    CHECK(run(g_tr, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_BAD, "both bad");
    make("lcd349", "1.0.97", 0, 2, kx1, img);
    g_verifies = 0;
    CHECK(run(g_tr, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_OK && r.key == 1 && g_verifies == 1,
          "an unknown key is not verified, then the good one (%d checks)", g_verifies);

    // each field wrong one time
    make("lcd349", "1.0.97", 5, 1, k0, img);
    uint8_t good[OTA_SIG_TRAILER];
    memcpy(good, g_tr, sizeof good);
    struct {
        int off;
        uint8_t x;
        ota_sig_res_t want;
        const char *what;
    } F[] = {
            {0, 0x01, OTA_SIG_NONE, "magic"},
            {7, 0x01, OTA_SIG_NONE, "the zero of the magic"},
            {8, 0x03, OTA_SIG_FORM, "format"},
            {9, 0x01, OTA_SIG_FORM, "format high byte"},
            {12, 0x01, OTA_SIG_FORM, "image_len"},
            {15, 0x80, OTA_SIG_FORM, "image_len high byte"},
            {16, 0x01, OTA_SIG_HASH, "image_sha256 first byte"},
            {47, 0x01, OTA_SIG_HASH, "image_sha256 last byte"},
            {48, 0x01, OTA_SIG_KIND, "kind"},
            {54, 0x41, OTA_SIG_KIND, "kind: a letter after the zero"},
            {63, 0x41, OTA_SIG_KIND, "kind: the last byte"},
            {64, 0x01, OTA_SIG_KIND, "version"},
            {95, 0x41, OTA_SIG_KIND, "version: the last byte"},
            {96, 0x01, OTA_SIG_BAD, "sec_ver (signed)"},
            {100, 0x01, OTA_SIG_BAD, "t (signed)"},
            {108, 0x01, OTA_SIG_FORM, "nsig 0"},
            {108, 0x04, OTA_SIG_FORM, "nsig 5"},
            {109, 0x01, OTA_SIG_FORM, "the zero bytes 109"},
            {127, 0x01, OTA_SIG_FORM, "the zero bytes 127"},
            {128, 0x01, OTA_SIG_KEY, "key_id"},
            {136, 0x01, OTA_SIG_BAD, "r"},
            {199, 0x01, OTA_SIG_BAD, "s"},
            {500, 0x01, OTA_SIG_OK, "the unsigned zero area (not read)"},
            {10, 0x01, OTA_SIG_BAD, "flags (not read, but signed)"},
    };
    for (size_t i = 0; i < sizeof F / sizeof F[0]; i++) {
        memcpy(g_tr, good, sizeof good);
        g_tr[F[i].off] ^= F[i].x;
        ota_sig_res_t got = run(g_tr, OTA_SIG_TRAILER, 0, &r);
        CHECK(got == F[i].want, "%s: %s, not %s", F[i].what, ota_sig_code(got), ota_sig_code(F[i].want));
    }
    // nsig 5 with the value set (not only a bit change)
    memcpy(g_tr, good, sizeof good);
    g_tr[108] = 5;
    CHECK(run(g_tr, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_FORM, "nsig = 5");
    g_tr[108] = 0;
    CHECK(run(g_tr, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_FORM, "nsig = 0");

    // the security version
    memcpy(g_tr, good, sizeof good);
    CHECK(run(g_tr, OTA_SIG_TRAILER, 5, &r) == OTA_SIG_OK && r.sec_ver == 5, "sec_ver 5, minsv 5");
    CHECK(run(g_tr, OTA_SIG_TRAILER, 6, &r) == OTA_SIG_OLD && r.sec_ver == 5, "sec_ver 5, minsv 6");
    ota_sig_text(&r, t, sizeof t);
    CHECK(!strcmp(t, "sig_old"), "sig_old text %s", t);

    // a cut trailer, no trailer, a file total that is not image_len + 1024, no hash
    CHECK(run(g_tr, 1000, 0, &r) == OTA_SIG_FORM, "a cut trailer");
    CHECK(run(g_tr, 4, 0, &r) == OTA_SIG_NONE, "4 bytes");
    CHECK(run(NULL, 0, 0, &r) == OTA_SIG_NONE, "no trailer");
    CHECK(run(img + 100, OTA_SIG_TRAILER, 0, &r) == OTA_SIG_NONE && !r.has_sv, "image bytes as a trailer");
    ota_sig_want_t w = {.keys = g_keys, .nkeys = 4, .kind = "lcd349", .version = "1.0.97"};
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER + 1, g_hash, &w, &TU_CRYPTO, &r) == OTA_SIG_FORM,
          "the file is one byte longer");
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, NULL, &w, &TU_CRYPTO, &r) == OTA_SIG_ERR &&
                  r.has_sv,
          "no hash (the board had no memory)");
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, 100, g_hash, &w, &TU_CRYPTO, &r) == OTA_SIG_FORM, "total < 1024");

    // the key list: empty, only empty entries, key 1 in slot 4
    w.nkeys = 0;
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, g_hash, &w, &TU_CRYPTO, &r) == OTA_SIG_KEY,
          "no key in the list");
    uint8_t list[OTA_SIG_KEYS][65] = {{0}};
    memcpy(list[3], g_keys[0], 65);
    w.keys = list;
    w.nkeys = 4;
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, g_hash, &w, &TU_CRYPTO, &r) == OTA_SIG_OK &&
                  r.key == 3,
          "the key in slot 4");
    w.nkeys = 3;
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, g_hash, &w, &TU_CRYPTO, &r) == OTA_SIG_KEY,
          "nkeys 3 does not read slot 4");

    // the version: 32 characters fill the field; 33 never match
    const char *v32 = "1.0.97-abcdefghijklmnopqrstuvwxy";
    make("lcd349", v32, 0, 1, k0, img);
    w.keys = g_keys;
    w.nkeys = 4;
    w.version = v32;
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, g_hash, &w, &TU_CRYPTO, &r) == OTA_SIG_OK,
          "a version of 32 characters");
    w.version = "1.0.97-abcdefghijklmnopqrstuvwxyz";
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, g_hash, &w, &TU_CRYPTO, &r) == OTA_SIG_KIND,
          "a version of 33 characters");
    w.version = "1.0.9";
    CHECK(ota_sig_check(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, g_hash, &w, &TU_CRYPTO, &r) == OTA_SIG_KIND,
          "a shorter version");
    // the peek
    make("lcd349", "1.0.97", 7, 1, k0, img);
    uint32_t sv = 0;
    CHECK(ota_sig_peek(g_tr, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, &sv) && sv == 7, "peek sec_ver");
    CHECK(!ota_sig_peek(img, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, &sv), "peek of no trailer");
    free(img);
}

static void mode_tests(void)
{
    for (int res = OTA_SIG_OK; res <= OTA_SIG_ERR; res++) {
        CHECK(ota_sig_accept(OTA_SIG_OFF, (ota_sig_res_t)res), "OFF takes %s", ota_sig_code((ota_sig_res_t)res));
        CHECK(ota_sig_accept(OTA_SIG_REPORT, (ota_sig_res_t)res), "REPORT takes %s", ota_sig_code((ota_sig_res_t)res));
        CHECK(ota_sig_accept(OTA_SIG_ENFORCE, (ota_sig_res_t)res) == (res == OTA_SIG_OK), "ENFORCE and %s",
              ota_sig_code((ota_sig_res_t)res));
        CHECK(ota_sig_mark_ok(OTA_SIG_REPORT, true, (ota_sig_res_t)res), "REPORT marks");
        CHECK(ota_sig_mark_ok(OTA_SIG_ENFORCE, false, (ota_sig_res_t)res), "ENFORCE, not pending: marks");
        CHECK(ota_sig_mark_ok(OTA_SIG_ENFORCE, true, (ota_sig_res_t)res) == (res == OTA_SIG_OK),
              "ENFORCE, pending, own %s", ota_sig_code((ota_sig_res_t)res));
        CHECK(strlen(ota_sig_why((ota_sig_res_t)res)) > 10, "a reason");
    }
    CHECK(!strcmp(ota_sig_code((ota_sig_res_t)99), "sig_err"), "an unknown result");
    CHECK(!strcmp(ota_sig_mode_name(OTA_SIG_REPORT), "report") && !strcmp(ota_sig_mode_name(OTA_SIG_OFF), "off") &&
                  !strcmp(ota_sig_mode_name(OTA_SIG_ENFORCE), "enforce"),
          "the mode names");
    ota_sig_result_t own = {.res = OTA_SIG_OK, .sec_ver = 3};
    CHECK(ota_sig_new_minsv(1, &own) == 3, "minsv up to 3");
    CHECK(ota_sig_new_minsv(5, &own) == 5, "minsv never down");
    own.res = OTA_SIG_BAD;
    CHECK(ota_sig_new_minsv(1, &own) == 1, "a bad own trailer does not change minsv");
    CHECK(ota_sig_new_minsv(1, NULL) == 1, "no own result");
}

// the vectors of fw_sign.py: the C check gives the code that the Python side expects
static int vector_tests(const char *dir)
{
    uint8_t keys[OTA_SIG_KEYS][65];
    tu_case_t cases[64];
    int n = tu_load_cases(dir, cases, 64);
    if (!tu_load_keys(dir, keys) || n <= 0) {
        CHECK(0, "no vectors in %s (run tests/unit/test_ota_sig.py)", dir);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        long len = 0;
        uint8_t *b = tu_read(dir, cases[i].file, &len);
        if (!b || len < 0x60 + OTA_SIG_TRAILER) {
            CHECK(0, "read %s", cases[i].file);
            free(b);
            continue;
        }
        char ver[33];
        snprintf(ver, sizeof ver, "%.32s", (const char *)b + 0x30);
        uint8_t h[32];
        hc_sha256(b, (size_t)(len - OTA_SIG_TRAILER), h);
        ota_sig_want_t w = {.keys = keys, .nkeys = 4, .kind = cases[i].kind, .version = ver, .minsv = cases[i].minsv};
        ota_sig_result_t r;
        ota_sig_check(b + len - OTA_SIG_TRAILER, OTA_SIG_TRAILER, (uint32_t)len, h, &w, &TU_CRYPTO, &r);
        CHECK(!strcmp(ota_sig_code(r.res), cases[i].code), "vector %s: %s, not %s", cases[i].file, ota_sig_code(r.res),
              cases[i].code);
        free(b);
    }
    return n;
}

// The rule of the self-test before the mark of a new firmware (ota_sig_self_test, ENFORCE)
static void self_test_tests(void)
{
    const char *why = NULL;
    const ota_sig_mode_t modes[] = {OTA_SIG_OFF, OTA_SIG_REPORT, OTA_SIG_ENFORCE};
    for (int m = 0; m < 3; m++) {
        for (int res = OTA_SIG_OK; res <= OTA_SIG_ERR; res++) {
            for (int web = 0; web < 2; web++) {
                ota_sig_mode_t md = modes[m];
                ota_sig_res_t r = (ota_sig_res_t)res;
                // not pending (a USB flash, or the mark is done): no self-test at each time
                CHECK(ota_sig_self_test(md, false, 0, r, web, &why) == OTA_SELF_NONE && !*why &&
                              ota_sig_self_test(md, false, 200000, r, web, &why) == OTA_SELF_NONE,
                      "not pending: none (%s)", ota_sig_mode_name(md));
                // the first 20 s: wait in each mode
                CHECK(ota_sig_self_test(md, true, OTA_SELF_MARK_MS - 1, r, web, &why) == OTA_SELF_WAIT && *why,
                      "before 20 s: wait");
                ota_self_t at20 = ota_sig_self_test(md, true, OTA_SELF_MARK_MS, r, web, &why);
                ota_self_t late = ota_sig_self_test(md, true, OTA_SELF_WEB_MS, r, web, NULL);
                if (md != OTA_SIG_ENFORCE) {
                    // REPORT and OFF: the mark at 20 s as before (no self-test)
                    CHECK(at20 == OTA_SELF_MARK && late == OTA_SELF_MARK && !*why, "%s marks at 20 s",
                          ota_sig_mode_name(md));
                } else if (r != OTA_SIG_OK) {
                    CHECK(at20 == OTA_SELF_BACK && late == OTA_SELF_BACK && strstr(why, "own trailer"),
                          "ENFORCE, own %s: back at 20 s", ota_sig_code(r));
                } else if (web) {
                    CHECK(at20 == OTA_SELF_MARK && late == OTA_SELF_MARK && !*why, "ENFORCE, own ok + web: mark");
                } else {
                    CHECK(at20 == OTA_SELF_WAIT && strstr(why, "web server"), "ENFORCE, no web yet: wait");
                    CHECK(ota_sig_self_test(md, true, OTA_SELF_WEB_MS - 1, r, false, &why) == OTA_SELF_WAIT,
                          "ENFORCE, no web at 89.999 s: wait");
                    CHECK(late == OTA_SELF_BACK, "ENFORCE, no web at 90 s: back");
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    crypto_self_test();
    field_tests();
    mode_tests();
    self_test_tests();
    int nv = argc > 1 ? vector_tests(argv[1]) : 0;
    printf("OTA SIG %s (%d vectors of fw_sign.py)\n", g_fails ? "FAILED" : "OK", nv);
    return g_fails ? 1 : 0;
}
