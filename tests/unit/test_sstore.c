// The unit test of the static store (main/tfn_sstore_core.c, tfn_sstore.h) with the sample store of
// sdk/tools/store_index.py (tests/unit/sstore_sample.py makes it, with a TEST key): the signature of the index with
// the built-in keys and the added STORE keys (the real ECDSA P-256 of host_crypto.c), the parser of index.json (the
// apps of each board kind, the filters of the ABI, the ABI rev and the permission classes, the descriptions, the
// files of a package), the check of a file, the check of the package signature (main/tfn_pub_core.c: the same check
// as the install), the faults of a damaged index, the escapes of JSON, the URL helpers and a fuzz of the parser.
// Build and run: python tests/unit/run.py (the argument is the folder of the sample: tests/unit/sstore)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_crypto.h"
#include "tfn_sstore.h"

#define TEST_ABI 12345  // the ABI of the sample (sstore_sample.py ABI)
#define MAXAPPS  8
#define FUZZ_N   20000

static int s_fail, s_n;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        s_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            s_fail++;                                                                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                        \
        }                                                                                                              \
    } while (0)

typedef struct {
    uint8_t *b;
    size_t n;
} blob_t;

static blob_t s_index, s_sig, s_manifest, s_psig;
static uint8_t s_pub[TFN_PUB_PK], s_other[TFN_PUB_PK], s_none[TFN_PUB_PK];

static blob_t load(const char *dir, const char *name)
{
    char p[512];
    blob_t r = {0};
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) {
        printf("FAIL: no file %s (run tests/unit/sstore_sample.py)\n", p);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    r.n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    r.b = malloc(r.n + 1);
    if (!r.b || fread(r.b, 1, r.n, f) != r.n) {
        exit(1);
    }
    r.b[r.n] = 0;
    fclose(f);
    return r;
}

static void sha(const void *d, size_t n, uint8_t out[32])
{
    hc_sha256(d, n, out);
}
static bool verify(const uint8_t pk[65], const uint8_t *m, size_t n, const uint8_t sig[64])
{
    return hc_p256_verify(pk, m, n, sig);
}
static const uint8_t *s_b1 = s_none, *s_b2 = s_none;
static bool builtin_ok(const uint8_t *m, size_t n, const uint8_t *sig, size_t sl)
{
    return sl == 64 && ((s_b1[0] == 4 && verify(s_b1, m, n, sig)) || (s_b2[0] == 4 && verify(s_b2, m, n, sig)));
}
static tfn_pub_ops_t ops(const uint8_t *b1, const uint8_t *b2)
{
    s_b1 = b1;
    s_b2 = b2;
    tfn_pub_ops_t o = {sha, verify, builtin_ok, {b1, b2}};
    return o;
}

static uint32_t fake_perms(const char *list, bool *unknown)
{
    *unknown = strstr(list, "future") != NULL;
    return (strstr(list, "net") ? 1u : 0u) | (strstr(list, "storage") ? 2u : 0u);
}
static void *fake_alloc(size_t n)
{
    return malloc(n);
}

// parse a copy of the index (the parser changes its text)
static int parse(const blob_t *j, const char *kind, int abi, int rev, const uint8_t *pk, tfn_cat_t *out,
                 tfn_ss_pkg_t *pkg, char *msg)
{
    tfn_ss_opts_t op = {kind, abi, rev, fake_alloc, fake_perms};
    char *c = malloc(j->n + 1);
    memcpy(c, j->b, j->n);
    c[j->n] = 0;
    int n = tfn_ss_parse(c, j->n, &op, pk, out, pkg, MAXAPPS, msg, 160);
    free(c);
    return n;
}

static int find(const tfn_cat_t *c, int n, const char *id)
{
    for (int i = 0; i < n; i++) {
        if (!strcmp(c[i].id, id)) {
            return i;
        }
    }
    return -1;
}

// the index with each copy of one text changed (b NULL: the text is not in the index)
static blob_t edit(const char *from, const char *to)
{
    blob_t r = {0};
    size_t fl = strlen(from), tl = strlen(to), k = 0;
    for (const char *p = (const char *)s_index.b; (p = strstr(p, from)); p += fl) {
        k++;
    }
    if (!k) {
        return r;
    }
    r.b = malloc(s_index.n + k * tl + 1);
    const char *s = (const char *)s_index.b;
    for (const char *p; (p = strstr(s, from)); s = p + fl) {
        memcpy(r.b + r.n, s, (size_t)(p - s));
        r.n += (size_t)(p - s);
        memcpy(r.b + r.n, to, tl);
        r.n += tl;
    }
    memcpy(r.b + r.n, s, strlen(s) + 1);
    r.n += strlen(s);
    return r;
}

static void key_tests(void)
{
    char who[TFN_PUB_NAME];
    uint8_t pk[TFN_PUB_PK];
    tfn_pub_list_t l;
    memset(&l, 0, sizeof l);
    tfn_pub_ops_t o = ops(s_pub, s_none);
    CHECK(tfn_ss_index_key(&o, &l, s_index.b, s_index.n, s_sig.b, s_sig.n, pk, who) == TFN_PUB_OK_BUILTIN);
    CHECK(!memcmp(pk, s_pub, TFN_PUB_PK) && who[0] == 0);
    o = ops(s_other, s_pub);  // (built-in key 2)
    CHECK(tfn_ss_index_key(&o, &l, s_index.b, s_index.n, s_sig.b, s_sig.n, pk, who) == TFN_PUB_OK_BUILTIN);
    o = ops(s_other, s_none);
    CHECK(tfn_ss_index_key(&o, &l, s_index.b, s_index.n, s_sig.b, s_sig.n, pk, who) == TFN_PUB_NONE);
    // an added key: only with the flag STORE
    memcpy(l.e[0].pk, s_pub, TFN_PUB_PK);
    tfn_pub_name_set(l.e[0].name, "community");
    l.e[0].flags = TFN_PUB_DEV | TFN_PUB_TEST;
    l.n = 1;
    CHECK(tfn_ss_index_key(&o, &l, s_index.b, s_index.n, s_sig.b, s_sig.n, pk, who) == TFN_PUB_NONE);
    l.e[0].flags = TFN_PUB_STORE;
    CHECK(tfn_ss_index_key(&o, &l, s_index.b, s_index.n, s_sig.b, s_sig.n, pk, who) == TFN_PUB_OK_STORE);
    CHECK(!strcmp(who, "COMMUNITY") && !memcmp(pk, s_pub, TFN_PUB_PK));
    // a changed index, a short signature, no signature
    s_index.b[100] ^= 1;
    CHECK(tfn_ss_index_key(&o, &l, s_index.b, s_index.n, s_sig.b, s_sig.n, pk, who) == TFN_PUB_NONE);
    s_index.b[100] ^= 1;
    CHECK(tfn_ss_index_key(&o, &l, s_index.b, s_index.n, s_sig.b, 63, pk, who) == TFN_PUB_NONE);
    CHECK(tfn_ss_index_key(&o, &l, s_index.b, s_index.n, NULL, 64, pk, who) == TFN_PUB_NONE);
}

// The data packs (sdk/tools/store_index.py "packs" at the root and "pack" in an app): the board parser skips both
// keys, at the end of the index (the sample) and at the start of the root and of an app.
static void pack_tests(void)
{
    tfn_cat_t c[MAXAPPS];
    tfn_ss_pkg_t p[MAXAPPS];
    char msg[160];
    const char *ix = (const char *)s_index.b;
    CHECK(strstr(ix, "\n \"packs\": [") && strstr(ix, "\"pack\": \"demo\"") && strstr(ix, "\"dir\": \"demo\""));
    int n = parse(&s_index, "lcd349", TEST_ABI, 20, s_pub, c, p, msg);
    CHECK(n == 1 && !strcmp(c[0].id, "demo") && msg[0] == 0);
    tfn_store_cat_free(c, n);
    blob_t b = edit("{\n \"tfn_store\": 1,", "{\n \"packs\": [{\"name\": \"x\", \"dir\": \"x\", \"files\": [{\"path\": "
                                             "\"a/b.bin\", \"size\": 1, \"sha256\": \"00\"}], \"size\": 1}],\n"
                                             " \"tfn_store\": 1,");
    CHECK(b.b != NULL);
    if (b.b) {
        n = parse(&b, "lcd349", TEST_ABI, 20, s_pub, c, p, msg);
        CHECK(n == 1 && !strcmp(c[0].id, "demo"));
        tfn_store_cat_free(c, n);
        free(b.b);
    }
    b = edit("\"id\": \"demo\",", "\"pack\": {\"name\": [1, {\"a\": null}]}, \"id\": \"demo\",");
    CHECK(b.b != NULL);
    if (b.b) {
        n = parse(&b, "cores3", TEST_ABI, 20, s_pub, c, p, msg);
        CHECK(n == 1 && !strcmp(c[0].id, "demo") && c[0].code == 26123);
        tfn_store_cat_free(c, n);
        free(b.b);
    }
}

static void parse_tests(void)
{
    tfn_cat_t c[MAXAPPS];
    tfn_ss_pkg_t p[MAXAPPS];
    char msg[160];
    // the test board: demo only (watchy needs rev 99, abix is of another ABI, fut has a newer class)
    int n = parse(&s_index, "lcd349", TEST_ABI, 20, s_pub, c, p, msg);
    CHECK(n == 1 && !strcmp(c[0].id, "demo") && !strcmp(p[0].id, "demo"));
    if (n == 1) {
        uint8_t h[32];
        CHECK(!strcmp(c[0].name, "DEMO") && !strcmp(c[0].ver, "1.2.3") && c[0].code == 26123);
        CHECK(!strcmp(c[0].group, "UTIL") && !strcmp(c[0].emoji, "\xf0\x9f\x8e\xb2") && c[0].ram == 1000);
        CHECK(c[0].perms == 3 && c[0].state == TFN_ST_AVAILABLE && c[0].bytes > 3000);
        CHECK(!strcmp(tfn_store_desc(&c[0], false), "A demo app: \"quotes\" and a back slash \\ in the text."));
        CHECK(!strcmp(tfn_store_desc(&c[0], true), "\xd7\x9e\xd7\xa9\xd7\x97\xd7\xa7 \xd7\x9c\xd7\x91\xd7\x93\xd7\x99"
                                                   "\xd7\xa7\xd7\x94"));
        sha(s_manifest.b, s_manifest.n, h);
        CHECK(p[0].f[TFN_SS_F_MANIFEST].size == s_manifest.n && !memcmp(p[0].f[TFN_SS_F_MANIFEST].sha, h, 32));
        CHECK(p[0].f[TFN_SS_F_SIG].size == 64 && p[0].f[TFN_SS_F_ELF].size == c[0].bytes);
        CHECK(p[0].f[TFN_SS_F_ICON].size == 400 && p[0].f[TFN_SS_F_WATCH].size == 0);
    }
    tfn_store_cat_free(c, n);
    n = parse(&s_index, "lcd349", TEST_ABI, 99, s_pub, c, p, msg);  // a newer rev: watchy too
    CHECK(n == 2 && find(c, n, "watchy") >= 0 && p[find(c, n, "watchy")].f[TFN_SS_F_WATCH].size == 700);
    tfn_store_cat_free(c, n);
    n = parse(&s_index, "lcd349", 0, 20, s_pub, c, p, msg);  // (ABI 0: no ABI check, as the manager)
    CHECK(n == 2 && find(c, n, "abix") >= 0);
    tfn_store_cat_free(c, n);
    n = parse(&s_index, "cores3", TEST_ABI, 20, s_pub, c, p, msg);
    CHECK(n == 1 && !strcmp(c[0].id, "demo"));
    tfn_store_cat_free(c, n);
    n = parse(&s_index, "am175", TEST_ABI, 20, s_pub, c, p, msg);  // a kind with no package
    CHECK(n == 0);
    // places: one place only
    tfn_ss_opts_t op = {"lcd349", 0, 99, fake_alloc, fake_perms};
    char *cp = malloc(s_index.n + 1);
    memcpy(cp, s_index.b, s_index.n + 1);
    n = tfn_ss_parse(cp, s_index.n, &op, s_pub, c, p, 1, msg, sizeof msg);
    CHECK(n == 1 && strstr(msg, "more apps"));
    tfn_store_cat_free(c, n);
    free(cp);
    // another key than the key of the signature
    CHECK(parse(&s_index, "lcd349", TEST_ABI, 20, s_other, c, p, msg) == -1 && strstr(msg, "another key"));
    pack_tests();
}

// the damaged copies of the index: each one gives -1
static void fault_tests(void)
{
    // (edit() changes each copy of the text; the first one is in the app abix, of the kind lcd349)
    static const char *const CASES[][2] = {
            {"\"tfn_store\": 1", "\"tfn_store\": 2"},
            {"\"apps/demo/lcd349/\"", "\"apps/demo/../x/\""},
            {"\"id\": \"demo\"", "\"id\": \"De mo\""},
            {"fut", "demo"},  // (an id two times)
            {"\"manifest.txt\": {", "\"manifest.txx\": {"},
            {"\"size\": 64", "\"size\": -64"},
            {"\"size\": 64", "\"size\": 6.4"},
            {"\"code\": 26123", "\"code\": \"26123\""},
            {"\"apps\": [", "\"apps\": {"},
            {"\"key_id\": \"", "\"key_id\": \"x"},
            {"\n}", "\n},"},
            {"\n}", "\n} {}"},
    };
    tfn_cat_t c[MAXAPPS];
    tfn_ss_pkg_t p[MAXAPPS];
    char msg[160];
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        blob_t e = edit(CASES[i][0], CASES[i][1]);
        CHECK(e.b != NULL);
        if (e.b) {
            int n = parse(&e, "lcd349", TEST_ABI, 99, s_pub, c, p, msg);
            if (n != -1) {
                printf("FAIL: the damaged index %d (%s) gives %d\n", (int)i, CASES[i][1], n);
            }
            CHECK(n == -1 && msg[0]);
            free(e.b);
        }
    }
    blob_t cut = {s_index.b, s_index.n / 2};
    CHECK(parse(&cut, "lcd349", TEST_ABI, 20, s_pub, c, p, msg) == -1);
    blob_t none = {(uint8_t *)"", 0};
    CHECK(parse(&none, "lcd349", TEST_ABI, 20, s_pub, c, p, msg) == -1);
}

// the escapes of JSON and the skip of unknown values
static void escape_tests(void)
{
    char pub[TFN_PUB_PK * 2 + 1];
    for (int i = 0; i < TFN_PUB_PK; i++) {
        snprintf(pub + 2 * i, 3, "%02x", s_pub[i]);
    }
    char *j = malloc(4096);
    snprintf(j, 4096,
             "{\"x\": [1, -2.5e3, true, null, {\"a\": [[]]}], \"tfn_store\": 1, \"key_id\": \"0123456789abcdef\", "
             "\"pubkey\": \"%s\", \"apps\": [{\"kinds\": {\"lcd349\": {\"files\": {\"sig\": {\"size\": 64, "
             "\"sha256\": \"%064d\"}, \"app.elf\": {\"size\": 9, \"sha256\": \"%064d\"}, \"manifest.txt\": {\"size\": "
             "9, \"sha256\": \"%064d\"}, \"new.file\": {\"size\": 1}}, \"path\": \"apps/e_1/lcd349/\", \"perms\": [], "
             "\"zip\": {}, \"other\": [\"\\u0041\"]}}, \"id\": \"e_1\", \"name\": \"A\\u00e9\\\"\", \"ver\": \"1\", "
             "\"code\": 1, \"group\": \"G\", \"desc\": \"\\ud83c\\udfb2\\n\\/\", \"desc_he\": \"\"}]}",
             pub, 0, 0, 0);
    blob_t b = {(uint8_t *)j, strlen(j)};
    tfn_cat_t c[MAXAPPS];
    tfn_ss_pkg_t p[MAXAPPS];
    char msg[160];
    int n = parse(&b, "lcd349", 0, 0, s_pub, c, p, msg);
    CHECK(n == 1);
    if (n == 1) {
        CHECK(!strcmp(c[0].name, "A\xc3\xa9\""));
        CHECK(!strcmp(tfn_store_desc(&c[0], false), "\xf0\x9f\x8e\xb2\n/"));
        CHECK(!strcmp(tfn_store_desc(&c[0], true), "\xf0\x9f\x8e\xb2\n/"));  // (no Hebrew: the English one)
    }
    tfn_store_cat_free(c, n);
    // a lone half of a surrogate pair, a control letter, a \u0000: not good
    static const char *const BAD[] = {"\\ud83c x", "\\udfb2", "a\tb", "\\u0000", "\\x41"};
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        char *k = malloc(4096);
        char *at = strstr(j, "\\ud83c\\udfb2");
        snprintf(k, 4096, "%.*s%s%s", (int)(at - j), j, BAD[i], at + 12);
        blob_t e = {(uint8_t *)k, strlen(k)};
        CHECK(parse(&e, "lcd349", 0, 0, s_pub, c, p, msg) == -1);
        free(k);
    }
    free(j);
}

static void file_tests(void)
{
    tfn_pub_ops_t o = ops(s_pub, s_none);
    tfn_ss_file_t f = {(uint32_t)s_manifest.n, {0}};
    sha(s_manifest.b, s_manifest.n, f.sha);
    CHECK(tfn_ss_file_ok(&o, &f, s_manifest.b, s_manifest.n));
    CHECK(!tfn_ss_file_ok(&o, &f, s_manifest.b, s_manifest.n - 1));
    s_manifest.b[5] ^= 0x20;
    CHECK(!tfn_ss_file_ok(&o, &f, s_manifest.b, s_manifest.n));
    s_manifest.b[5] ^= 0x20;
    f.size = 0;
    CHECK(!tfn_ss_file_ok(&o, &f, s_manifest.b, 0));
    // the signature of the package: the check of the install (tfn_pub_check) takes it with the store key
    tfn_pub_list_t l;
    char who[TFN_PUB_NAME];
    memset(&l, 0, sizeof l);
    CHECK(tfn_pub_check(&o, &l, s_manifest.b, s_manifest.n, s_psig.b, s_psig.n, NULL, 0, who) == TFN_PUB_OK_BUILTIN);
    tfn_pub_ops_t o2 = ops(s_other, s_none);
    CHECK(tfn_pub_check(&o2, &l, s_manifest.b, s_manifest.n, s_psig.b, s_psig.n, NULL, 0, who) == TFN_PUB_NONE);
    memcpy(l.e[0].pk, s_pub, TFN_PUB_PK);
    tfn_pub_name_set(l.e[0].name, "community");
    l.e[0].flags = TFN_PUB_STORE;
    l.n = 1;
    CHECK(tfn_pub_check(&o2, &l, s_manifest.b, s_manifest.n, s_psig.b, s_psig.n, NULL, 0, who) == TFN_PUB_OK_STORE);
}

static void url_tests(void)
{
    char o[64];
    CHECK(tfn_ss_url_ok("https://tfnos.github.io/tfn-os/store/"));
    CHECK(tfn_ss_url_ok("https://example.org"));
    CHECK(!tfn_ss_url_ok("http://example.org/"));
    CHECK(!tfn_ss_url_ok("https://"));
    CHECK(!tfn_ss_url_ok("https:///x"));
    CHECK(!tfn_ss_url_ok("https://a b/"));
    CHECK(!tfn_ss_url_ok("https://a/?x=1"));
    CHECK(!tfn_ss_url_ok(NULL));
    char longu[TFN_SS_URL + 4];
    memset(longu, 'a', sizeof longu - 1);
    longu[sizeof longu - 1] = 0;
    memcpy(longu, "https://", 8);
    CHECK(!tfn_ss_url_ok(longu));
    CHECK(tfn_ss_join(o, sizeof o, "https://a/s/", "index.json") && !strcmp(o, "https://a/s/index.json"));
    CHECK(tfn_ss_join(o, sizeof o, "https://a/s", "/index.json") && !strcmp(o, "https://a/s/index.json"));
    CHECK(!tfn_ss_join(o, 16, "https://a/s/", "index.json"));
    CHECK(!strcmp(tfn_ss_file_name(TFN_SS_F_WATCH), "watch.elf") && !strcmp(tfn_ss_file_name(9), ""));
}

// random changes of the index: the parser must not read or write out of the buffer (ASan) and must free all
static void fuzz(void)
{
    tfn_cat_t c[MAXAPPS];
    tfn_ss_pkg_t p[MAXAPPS];
    char msg[160];
    static const char SET[] = "{}[]\",:\\u0 9-.e\n";
    unsigned seed = 1;
    int good = 0;
    for (int it = 0; it < FUZZ_N; it++) {
        blob_t b = {malloc(s_index.n + 1), s_index.n};
        memcpy(b.b, s_index.b, s_index.n);
        int k = 1 + it % 4;
        for (int i = 0; i < k; i++) {
            seed = seed * 1103515245u + 12345u;
            size_t at = (seed >> 8) % b.n;
            int how = (int)(seed >> 4) % 3;
            if (how == 0) {
                b.b[at] = (uint8_t)SET[(seed >> 16) % (sizeof SET - 1)];
            } else if (how == 1) {
                b.b[at] ^= (uint8_t)(1 << ((seed >> 20) % 8));
            } else {
                b.n = at;
            }
            if (!b.n) {
                break;
            }
        }
        int n = parse(&b, it & 1 ? "lcd349" : "cores3", it & 2 ? 0 : TEST_ABI, 99, s_pub, c, p, msg);
        CHECK(n >= -1 && n <= MAXAPPS);
        good += n >= 0;
        if (n > 0) {
            tfn_store_cat_free(c, n);
        }
        free(b.b);
    }
    printf("fuzz: %d changed copies, %d still good\n", FUZZ_N, good);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "tests/unit/sstore";
    s_index = load(dir, "index.json");
    s_sig = load(dir, "index.sig");
    s_manifest = load(dir, "demo_manifest.txt");
    s_psig = load(dir, "demo_sig");
    blob_t pt = load(dir, "store_pub.txt");
    for (int i = 0; i < TFN_PUB_PK; i++) {
        unsigned v = 0;
        sscanf((const char *)pt.b + 2 * i, "%2x", &v);
        s_pub[i] = (uint8_t)v;
    }
    free(pt.b);
    uint8_t d[32];
    memset(d, 7, sizeof d);
    hc_p256_pub(d, s_other);
    key_tests();
    parse_tests();
    fault_tests();
    escape_tests();
    file_tests();
    url_tests();
    fuzz();
    free(s_index.b);
    free(s_sig.b);
    free(s_manifest.b);
    free(s_psig.b);
    printf("SSTORE %s (%d checks, %d faults)\n", s_fail ? "FAILED" : "OK", s_n, s_fail);
    return s_fail ? 1 : 0;
}
