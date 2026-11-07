// The unit test of the steps of an update over the network (main/ota_flow.c) with a fake flash (a RAM slot and a
// fake boot slot) and a fake network (blocks of a set size, a cut, a socket fault): a good signed image, the wrong
// kind tag, no tag, a cut body, a bad signature, no trailer, an old sec_ver, a socket fault, a second update while
// one runs, the faults of the flash, the modes REPORT / ENFORCE / OFF, the vectors of fw_sign.py (argv[1]), the
// rule of the mark of a new firmware, and the ENFORCE cases 1-8 of the design (P4 stage 1, 5.3) on one fake board:
// the update, the restart, the self-test of the new firmware, the mark or the rollback, and minsv.
// Build and run: python tests/unit/run.py
#include "ota_flow.h"
#include "ota_test_util.h"

#define LEN  70000
#define SLOT (256 * 1024)

typedef struct {
    // the network
    const uint8_t *body;
    int len, pos;
    int chunk;   // the bytes of one receive at most
    int cut_at;  // the sender closes at this offset (-1: never)
    int err_at;  // a socket fault at this offset (-1: never)
    // the flash
    uint8_t slot[SLOT];
    int written;
    bool begun, aborted, ended;
    int boot;  // the fake boot slot: 0 = the old firmware, 1 = the new slot
    int begin_err, write_err_at, end_err, boot_err;
    // the board
    bool busy, claim_ok, claimed, trial_ok, alloc_ok;
    int releases, wakes;
    ota_flow_mem_t *mem;
    int step;  // the last step (-1: none)
    int steps_bad;
    // the hash
    bool hash_ok, hash_on;
    int hash_starts, hash_ends;
    uint8_t *hashed;
    int nhash;
    // the signature
    bool sig_seen, sig_acc;
    ota_sig_result_t sig;
} fake_t;

static bool f_busy(void *c)
{
    return ((fake_t *)c)->busy;
}

static bool f_claim(void *c)
{
    fake_t *f = c;
    f->claimed = f->claim_ok;
    return f->claim_ok;
}

static void f_release(void *c)
{
    fake_t *f = c;
    f->releases++;
    f->claimed = false;
}

static bool f_trial(void *c)
{
    return ((fake_t *)c)->trial_ok;
}

static void f_wake(void *c)
{
    ((fake_t *)c)->wakes++;
}

static ota_flow_mem_t *f_alloc(void *c)
{
    fake_t *f = c;
    f->mem = f->alloc_ok ? malloc(sizeof *f->mem) : NULL;
    return f->mem;
}

static void f_free(void *c, ota_flow_mem_t *m)
{
    fake_t *f = c;
    CHECK(m == f->mem, "free of another memory");
    free(m);
    f->mem = NULL;
}

static int f_recv(void *c, char *dst, int want)
{
    fake_t *f = c;
    CHECK(want > 0, "recv want %d", want);
    if (f->err_at >= 0 && f->pos >= f->err_at) {
        return OTA_FLOW_RECV_ERR;
    }
    if (f->cut_at >= 0 && f->pos >= f->cut_at) {
        return 0;
    }
    int n = want < f->chunk ? want : f->chunk;
    if (n > f->len - f->pos) {
        n = f->len - f->pos;
    }
    if (f->err_at >= 0 && f->pos + n > f->err_at) {
        n = f->err_at - f->pos;
    }
    if (f->cut_at >= 0 && f->pos + n > f->cut_at) {
        n = f->cut_at - f->pos;
    }
    memcpy(dst, f->body + f->pos, (size_t)n);
    f->pos += n;
    return n;
}

static void f_step(void *c, int step, int done, int total)
{
    fake_t *f = c;
    bool ok = step == OTA_STEP_ERASE   ? f->step == -1 && done == 0
              : step == OTA_STEP_WRITE ? f->step == OTA_STEP_ERASE && f->begun
                                       : (f->step == OTA_STEP_WRITE || f->step == OTA_STEP_PROGRESS) &&
                                                 done == f->written && done <= total;
    if (!ok) {
        f->steps_bad++;
    }
    f->step = step;
}

static int f_begin(void *c, int total)
{
    fake_t *f = c;
    if (f->begin_err) {
        return f->begin_err;
    }
    CHECK(total <= SLOT, "begin %d", total);
    memset(f->slot, 0xFF, SLOT);  // (the erase)
    f->begun = true;
    return 0;
}

static int f_write(void *c, const char *p, int n)
{
    fake_t *f = c;
    CHECK(f->begun && !f->aborted && !f->ended, "write in the wrong state");
    if (f->write_err_at > 0 && f->written + n > f->write_err_at) {
        return 0x1503;
    }
    CHECK(f->written + n <= SLOT && n > 0, "write %d at %d", n, f->written);
    memcpy(f->slot + f->written, p, (size_t)n);
    f->written += n;
    return 0;
}

static void f_abort(void *c)
{
    fake_t *f = c;
    CHECK(f->begun && !f->ended, "abort with no handle");
    f->aborted = true;
}

static bool f_hash_start(void *c)
{
    fake_t *f = c;
    f->hash_starts++;
    if (!f->hash_ok) {
        f->hash_ends++;
        return false;
    }
    f->hash_on = true;
    f->nhash = 0;
    return true;
}

static bool f_hash_update(void *c, const uint8_t *p, int n)
{
    fake_t *f = c;
    CHECK(f->hash_on && f->nhash + n <= LEN + OTA_SIG_TRAILER, "hash update");
    memcpy(f->hashed + f->nhash, p, (size_t)n);
    f->nhash += n;
    return true;
}

static bool f_hash_finish(void *c, uint8_t out[32])
{
    fake_t *f = c;
    CHECK(f->hash_on, "hash finish with no start");
    f->hash_on = false;
    f->hash_ends++;
    hc_sha256(f->hashed, (size_t)f->nhash, out);
    return true;
}

static void f_hash_abort(void *c)
{
    fake_t *f = c;
    CHECK(f->hash_on, "hash abort with no start");
    f->hash_on = false;
    f->hash_ends++;
}

static void f_on_sig(void *c, const ota_sig_result_t *r, bool accepted)
{
    fake_t *f = c;
    f->sig_seen = true;
    f->sig = *r;
    f->sig_acc = accepted;
}

static int f_end(void *c)
{
    fake_t *f = c;
    CHECK(f->begun && !f->aborted && !f->ended, "end in the wrong state");
    f->ended = true;
    return f->end_err;
}

static int f_set_boot(void *c)
{
    fake_t *f = c;
    CHECK(f->ended, "boot slot before the end");
    if (f->boot_err) {
        return f->boot_err;
    }
    f->boot = 1;
    return 0;
}

static const ota_flow_io_t IO = {
        .busy = f_busy,
        .claim = f_claim,
        .release = f_release,
        .trial = f_trial,
        .wake = f_wake,
        .alloc = f_alloc,
        .free_mem = f_free,
        .recv = f_recv,
        .step = f_step,
        .begin = f_begin,
        .write = f_write,
        .abort = f_abort,
        .hash_start = f_hash_start,
        .hash_update = f_hash_update,
        .hash_finish = f_hash_finish,
        .hash_abort = f_hash_abort,
        .on_sig = f_on_sig,
        .end = f_end,
        .set_boot = f_set_boot,
};

static uint8_t g_keys[OTA_SIG_KEYS][65];
static fake_t g_f;

static void fake_init(const uint8_t *body, int len, int chunk)
{
    uint8_t *h = g_f.hashed ? g_f.hashed : malloc(SLOT);
    memset(&g_f, 0, sizeof g_f);
    g_f.hashed = h;
    g_f.body = body;
    g_f.len = len;
    g_f.chunk = chunk;
    g_f.cut_at = -1;
    g_f.err_at = -1;
    g_f.claim_ok = g_f.trial_ok = g_f.alloc_ok = g_f.hash_ok = true;
    g_f.step = -1;
}

static ota_flow_res_t run(ota_sig_mode_t mode, const uint8_t (*keys)[65], uint32_t minsv, ota_flow_out_t *o)
{
    ota_flow_cfg_t cfg = {.tag = "@ARCADE-BOARD:lcd349@",
                          .slot_size = SLOT,
                          .mode = mode,
                          .check = mode == OTA_SIG_OFF ? NULL : ota_sig_check,
                          .crypto = &TU_CRYPTO,
                          .want = {.keys = keys, .nkeys = OTA_SIG_KEYS, .kind = "lcd349", .minsv = minsv}};
    ota_flow_res_t r = ota_flow_run(&IO, &g_f, &cfg, g_f.len, o);
    CHECK(r == o->res, "the result and out.res");
    CHECK(!g_f.mem, "the work memory is free");
    CHECK(!g_f.hash_on && g_f.hash_starts == g_f.hash_ends, "the hash context is free (%d starts, %d ends)",
          g_f.hash_starts, g_f.hash_ends);
    CHECK(!g_f.steps_bad, "the order of the steps");
    // a fault after the claim releases it; a good update keeps it (the board restarts)
    bool claimed = r != OTA_FLOW_SLOT && r != OTA_FLOW_BUSY && r != OTA_FLOW_SIZE && r != OTA_FLOW_CLAIM;
    CHECK(g_f.releases == (claimed && r != OTA_FLOW_OK ? 1 : 0), "releases %d for %d", g_f.releases, r);
    CHECK(g_f.boot == (r == OTA_FLOW_OK), "the boot slot for result %d", r);
    CHECK(!g_f.aborted || (r != OTA_FLOW_OK && r != OTA_FLOW_END && r != OTA_FLOW_BOOT), "abort for result %d", r);
    return r;
}

// a good update: the slot has the full body, the hash covers the image part only
static void check_written(const char *what)
{
    CHECK(g_f.written == g_f.len && !memcmp(g_f.slot, g_f.body, (size_t)g_f.len), "%s: the slot = the body", what);
}

static uint8_t *signed_body(const char *kind, const char *tagkind, uint32_t sv, int nsig, const int *keys, int tag_at)
{
    uint8_t *b = tu_image(LEN, tagkind, "1.0.97", tag_at, 11);
    tu_trailer(b + LEN, b, LEN, kind, "1.0.97", sv, nsig, keys);
    return b;
}

static void basic_cases(void)
{
    ota_flow_out_t o;
    const int k0[] = {0}, k1[] = {1};
    uint8_t *good = signed_body("lcd349", "lcd349", 0, 1, k0, 30000);
    const int chunks[] = {8192, 1460, 97, 5000, 1};
    for (size_t i = 0; i < sizeof chunks / sizeof chunks[0]; i++) {
        fake_init(good, LEN + OTA_SIG_TRAILER, chunks[i]);
        CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_OK, "good, blocks of %d", chunks[i]);
        check_written("good");
        CHECK(g_f.nhash == LEN && !memcmp(g_f.hashed, good, LEN), "the hash covers [0, len - 1024) (%d)", g_f.nhash);
        CHECK(o.checked && o.sig.res == OTA_SIG_OK && o.sig.key == 0 && g_f.sig_seen && g_f.sig_acc,
              "good: the signature %s", ota_sig_code(o.sig.res));
        CHECK(!strcmp(o.ver, "1.0.97") && g_f.wakes == 1 && g_f.step == OTA_STEP_PROGRESS, "good: the version");
    }
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    CHECK(run(OTA_SIG_ENFORCE, g_keys, 0, &o) == OTA_FLOW_OK && o.sig.res == OTA_SIG_OK, "good, ENFORCE");

    // a tag across two blocks (the last 40 bytes of a block are kept)
    uint8_t *across = signed_body("lcd349", "lcd349", 0, 1, k1, 8192 - 10);
    fake_init(across, LEN + OTA_SIG_TRAILER, 8192);
    CHECK(run(OTA_SIG_ENFORCE, g_keys, 0, &o) == OTA_FLOW_OK && o.sig.key == 1, "a tag across two blocks, key 1");

    // the wrong kind tag, no tag
    uint8_t *other = signed_body("cores3", "cores3", 0, 1, k0, 30000);
    fake_init(other, LEN + OTA_SIG_TRAILER, 8192);
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_TAG && !strcmp(o.other, "cores3") && g_f.aborted && !o.checked,
          "the tag of another kind: %d %s", o.res, o.other);
    uint8_t *notag = signed_body("lcd349", NULL, 0, 1, k0, 0);
    fake_init(notag, LEN + OTA_SIG_TRAILER, 8192);
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_TAG && !o.other[0] && g_f.aborted, "no tag");

    // a cut body, a socket fault in the middle, a cut in the first block
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.cut_at = 40000;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_RECV && o.recv == 0 && o.done == 40000 && g_f.aborted &&
                  !g_f.ended && !o.checked,
          "a cut body: %d done %d", o.res, o.done);
    fake_init(good, LEN + OTA_SIG_TRAILER, 1460);
    g_f.err_at = 50000;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_RECV && o.recv == OTA_FLOW_RECV_ERR && g_f.aborted,
          "a socket fault");
    fake_init(good, LEN + OTA_SIG_TRAILER, 16);
    g_f.cut_at = 40;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_RECV && o.done == 40 && !g_f.begun && g_f.step == -1,
          "a cut first block");
    // the trailer itself cut: the last 100 bytes do not come
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.cut_at = LEN + OTA_SIG_TRAILER - 100;
    CHECK(run(OTA_SIG_ENFORCE, g_keys, 0, &o) == OTA_FLOW_RECV && g_f.aborted && !o.checked, "a cut trailer");

    // a bad signature, no trailer, an old sec_ver: REPORT takes them, ENFORCE refuses them
    uint8_t *bad = signed_body("lcd349", "lcd349", 0, 1, k0, 30000);
    bad[LEN + 128 + 8 + 50] ^= 0x01;
    uint8_t *none = tu_image(LEN + OTA_SIG_TRAILER, "lcd349", "1.0.97", 30000, 3);
    uint8_t *old = signed_body("lcd349", "lcd349", 1, 1, k0, 30000);
    struct {
        const uint8_t *b;
        uint32_t minsv;
        ota_sig_res_t want;
    } R[] = {{bad, 0, OTA_SIG_BAD}, {none, 0, OTA_SIG_NONE}, {old, 2, OTA_SIG_OLD}};
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++) {
        fake_init(R[i].b, LEN + OTA_SIG_TRAILER, 8192);
        CHECK(run(OTA_SIG_REPORT, g_keys, R[i].minsv, &o) == OTA_FLOW_OK && o.sig.res == R[i].want && g_f.sig_acc,
              "REPORT takes %s", ota_sig_code(R[i].want));
        check_written("REPORT");
        fake_init(R[i].b, LEN + OTA_SIG_TRAILER, 8192);
        CHECK(run(OTA_SIG_ENFORCE, g_keys, R[i].minsv, &o) == OTA_FLOW_SIG && o.sig.res == R[i].want && !g_f.sig_acc &&
                      g_f.aborted && !g_f.ended,
              "ENFORCE refuses %s", ota_sig_code(R[i].want));
    }
    // sec_ver 1, minsv 1: taken
    fake_init(old, LEN + OTA_SIG_TRAILER, 8192);
    CHECK(run(OTA_SIG_ENFORCE, g_keys, 1, &o) == OTA_FLOW_OK, "sec_ver = minsv");
    // OFF: no hash, no check
    fake_init(none, LEN + OTA_SIG_TRAILER, 8192);
    CHECK(run(OTA_SIG_OFF, g_keys, 0, &o) == OTA_FLOW_OK && !o.checked && !g_f.sig_seen && !g_f.hash_starts,
          "OFF: no check");
    // no hash context (no memory): REPORT takes it with sig_err, ENFORCE refuses
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.hash_ok = false;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_OK && o.sig.res == OTA_SIG_ERR, "no hash, REPORT");
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.hash_ok = false;
    CHECK(run(OTA_SIG_ENFORCE, g_keys, 0, &o) == OTA_FLOW_SIG && o.sig.res == OTA_SIG_ERR, "no hash, ENFORCE");
    // no key in the list
    static const uint8_t none_keys[OTA_SIG_KEYS][65];
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    CHECK(run(OTA_SIG_REPORT, none_keys, 0, &o) == OTA_FLOW_OK && o.sig.res == OTA_SIG_KEY, "an empty key list");
    free(bad);
    free(none);
    free(old);
    free(other);
    free(notag);
    free(across);
    free(good);
}

static void board_cases(void)
{
    ota_flow_out_t o;
    const int k0[] = {0};
    uint8_t *good = signed_body("lcd349", "lcd349", 0, 1, k0, 30000);
    // a second update while one runs; the claim lost to another update
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.busy = true;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_BUSY && !g_f.claimed && !g_f.pos, "busy");
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.claim_ok = false;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_CLAIM && !g_f.pos, "the claim");
    // the sizes, no slot
    fake_init(good, OTA_FLOW_MIN - 1, 8192);
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_SIZE, "too small");
    fake_init(good, SLOT + 1, 8192);
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_SIZE, "too big");
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    ota_flow_cfg_t cfg = {.tag = "@ARCADE-BOARD:lcd349@", .slot_size = 0};
    CHECK(ota_flow_run(&IO, &g_f, &cfg, g_f.len, &o) == OTA_FLOW_SLOT && !g_f.claimed, "no slot");
    // the RAM trial, the work memory
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.trial_ok = false;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_RAM && !g_f.wakes && !g_f.pos, "the RAM trial");
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.alloc_ok = false;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_MEM && !g_f.pos, "no work memory");
    // not an image
    uint8_t *notimg = malloc(LEN + OTA_SIG_TRAILER);
    memcpy(notimg, good, LEN + OTA_SIG_TRAILER);
    notimg[0] = 0x7F;
    fake_init(notimg, LEN + OTA_SIG_TRAILER, 8192);
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_IMAGE && !g_f.begun && !g_f.aborted, "not an image");
    // the faults of the flash
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.begin_err = 0x101;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_BEGIN && o.err == 0x101 && !g_f.aborted, "the erase fails");
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.write_err_at = 20000;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_WRITE && o.err == 0x1503 && o.done == 16384 && g_f.aborted,
          "a write fails: done %d", o.done);
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.end_err = 0x1503;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_END && o.err == 0x1503 && !g_f.aborted && o.checked,
          "the end check fails");
    fake_init(good, LEN + OTA_SIG_TRAILER, 8192);
    g_f.boot_err = 0x102;
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_BOOT && o.err == 0x102, "the boot slot fails");
    free(notimg);
    free(good);
}

// the vectors of fw_sign.py through the whole update: REPORT gives the code of the vector, ENFORCE takes only "ok"
static int vector_cases(const char *dir)
{
    uint8_t keys[OTA_SIG_KEYS][65];
    tu_case_t cases[64];
    int n = tu_load_cases(dir, cases, 64);
    if (!tu_load_keys(dir, keys) || n <= 0) {
        CHECK(0, "no vectors in %s", dir);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        long len = 0;
        uint8_t *b = tu_read(dir, cases[i].file, &len);
        if (!b) {
            CHECK(0, "read %s", cases[i].file);
            continue;
        }
        bool other = !strcmp(cases[i].file, "wrong_kind.bin");  // (the tag of cores3: the tag check stops it first)
        ota_flow_out_t o;
        fake_init(b, (int)len, 4096);
        ota_flow_res_t r = run(OTA_SIG_REPORT, keys, cases[i].minsv, &o);
        if (other) {
            CHECK(r == OTA_FLOW_TAG && !strcmp(o.other, "cores3"), "vector %s: the tag", cases[i].file);
        } else {
            CHECK(r == OTA_FLOW_OK && !strcmp(ota_sig_code(o.sig.res), cases[i].code), "vector %s REPORT: %s, not %s",
                  cases[i].file, ota_sig_code(o.sig.res), cases[i].code);
        }
        fake_init(b, (int)len, 8192);
        r = run(OTA_SIG_ENFORCE, keys, cases[i].minsv, &o);
        bool take = !strcmp(cases[i].code, "ok");
        CHECK((r == OTA_FLOW_OK) == take && (take || g_f.boot == 0), "vector %s ENFORCE: result %d", cases[i].file, r);
        free(b);
    }
    return n;
}

// The rule of the mark of a new firmware (ota_tick): the board checks its own trailer in its slot. ENFORCE + pending:
// a bad own trailer = no mark (the bootloader goes back); a good one = the mark and minsv = max(minsv, sec_ver).
static void mark_cases(void)
{
    const int k0[] = {0};
    uint8_t *own = signed_body("lcd349", "lcd349", 3, 1, k0, 30000);
    uint8_t h[32];
    hc_sha256(own, LEN, h);
    ota_sig_want_t w = {.keys = g_keys, .nkeys = OTA_SIG_KEYS, .kind = "lcd349", .version = "1.0.97", .minsv = 1};
    ota_sig_result_t r;
    ota_sig_check(own + LEN, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, h, &w, &TU_CRYPTO, &r);
    CHECK(ota_sig_mark_ok(OTA_SIG_ENFORCE, true, r.res) && ota_sig_new_minsv(1, &r) == 3, "a good own trailer");
    own[LEN + 150] ^= 1;
    ota_sig_check(own + LEN, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, h, &w, &TU_CRYPTO, &r);
    CHECK(!ota_sig_mark_ok(OTA_SIG_ENFORCE, true, r.res) && ota_sig_new_minsv(1, &r) == 1, "a bad own trailer");
    CHECK(ota_sig_mark_ok(OTA_SIG_REPORT, true, r.res), "REPORT marks also with a bad own trailer");
    CHECK(ota_sig_mark_ok(OTA_SIG_ENFORCE, false, r.res), "a USB firmware is not pending: no rule");
    free(own);
}

// ---------------------------------------------------------------- the ENFORCE cases of the design (5.3)
// One fake board in ENFORCE: its minsv, the slot that runs (0 = the old firmware, 1 = the new one), and "bad" (the
// slot that the bootloader went back from).
typedef struct {
    uint32_t minsv;
    int runs;
    int bad;
    int64_t mark_at;  // the time of the mark (ms after the start; -1: none)
    int64_t back_at;  // the time of the rollback (-1: none)
} board_t;

// The new firmware after the restart (the model of self_test_tick in ota.c): a step each 50 ms up to 120 s. The
// firmware checks its own trailer (in the slot at image_len) with its own key list one time at 20 s, then asks
// ota_sig_self_test. web_at = the time of the /api/ota registration (-1: never). crash = the rollback test build
// (one changed signed byte, as OTA_CRASH_TEST in ota.c).
static void new_firmware(board_t *b, ota_sig_mode_t mode, const uint8_t *slot, int total, int web_at, bool crash)
{
    b->runs = 1;
    b->mark_at = b->back_at = -1;
    bool own_done = false;
    ota_sig_result_t own = {.res = OTA_SIG_ERR, .key = -1};
    for (int64_t now = 0; now <= 120000; now += 50) {
        if (now >= OTA_SELF_MARK_MS && !own_done && mode == OTA_SIG_ENFORCE) {
            own_done = true;
            uint8_t tr[OTA_SIG_TRAILER], h[32];
            memcpy(tr, slot + total - OTA_SIG_TRAILER, OTA_SIG_TRAILER);
            if (crash) {
                tr[100] ^= 0x01;
            }
            hc_sha256(slot, (size_t)(total - OTA_SIG_TRAILER), h);
            char ver[33];
            snprintf(ver, sizeof ver, "%.32s", (const char *)slot + OTA_FLOW_DESC_VER);
            ota_sig_want_t w = {
                    .keys = g_keys, .nkeys = OTA_SIG_KEYS, .kind = "lcd349", .version = ver, .minsv = b->minsv};
            ota_sig_check(tr, OTA_SIG_TRAILER, (uint32_t)total, h, &w, &TU_CRYPTO, &own);
        }
        const char *why;
        ota_self_t d = ota_sig_self_test(mode, true, now, own.res, web_at >= 0 && now >= web_at, &why);
        if (d == OTA_SELF_WAIT) {
            continue;
        }
        if (d == OTA_SELF_BACK) {
            b->back_at = now;
            b->bad = 1;
            b->runs = 0;
            return;
        }
        if (d == OTA_SELF_MARK) {
            b->mark_at = now;
            b->minsv = ota_sig_new_minsv(b->minsv, &own);  // (minsv: only after the mark)
        }
        return;
    }
}

// one update over the network in ENFORCE to the board b; on a good update the board restarts into the new firmware
static ota_flow_res_t enforce_update(board_t *b, const uint8_t *body, int len, int web_at, bool crash,
                                     ota_flow_out_t *o)
{
    fake_init(body, len, 4096);
    ota_flow_res_t r = run(OTA_SIG_ENFORCE, g_keys, b->minsv, o);
    if (r == OTA_FLOW_OK) {
        new_firmware(b, OTA_SIG_ENFORCE, g_f.slot, g_f.written, web_at, crash);
    } else {
        CHECK(!g_f.boot && !g_f.ended && (r != OTA_FLOW_SIG || g_f.aborted), "a refused update: no slot switch");
    }
    return r;
}

static void design_cases(void)
{
    ota_flow_out_t o;
    const int k0[] = {0}, k1[] = {1}, kx[] = {-1};
    board_t b = {.minsv = 0, .runs = 0, .bad = -1};
    // 1. an unsigned image: refused, sig_none, the old firmware runs
    uint8_t *unsigned_img = tu_image(LEN + OTA_SIG_TRAILER, "lcd349", "1.0.97", 30000, 21);
    CHECK(enforce_update(&b, unsigned_img, LEN + OTA_SIG_TRAILER, 0, false, &o) == OTA_FLOW_SIG &&
                  o.sig.res == OTA_SIG_NONE && b.runs == 0 && b.minsv == 0,
          "5.3 case 1: an unsigned image: %d %s", o.res, ota_sig_code(o.sig.res));
    // 2. a bad signature: refused, sig_bad
    uint8_t *badsig = signed_body("lcd349", "lcd349", 1, 1, k0, 30000);
    badsig[LEN + 128 + 8 + 20] ^= 0x40;
    CHECK(enforce_update(&b, badsig, LEN + OTA_SIG_TRAILER, 0, false, &o) == OTA_FLOW_SIG && o.sig.res == OTA_SIG_BAD &&
                  b.runs == 0,
          "5.3 case 2: a bad signature: %s", ota_sig_code(o.sig.res));
    // ... and a signature of a key that is not in the list: sig_key
    uint8_t *otherkey = signed_body("lcd349", "lcd349", 1, 1, kx, 30000);
    CHECK(enforce_update(&b, otherkey, LEN + OTA_SIG_TRAILER, 0, false, &o) == OTA_FLOW_SIG && o.sig.res == OTA_SIG_KEY,
          "5.3 case 2b: a key out of the list: %s", ota_sig_code(o.sig.res));
    // 3. the image of another kind with a good signature: the tag stops it (code "kind"); a trailer of another kind
    // on an image with the own tag: sig_kind
    uint8_t *cores3 = signed_body("cores3", "cores3", 1, 1, k0, 30000);
    CHECK(enforce_update(&b, cores3, LEN + OTA_SIG_TRAILER, 0, false, &o) == OTA_FLOW_TAG &&
                  !strcmp(o.other, "cores3") && !o.checked && b.runs == 0,
          "5.3 case 3: another kind by the tag");
    uint8_t *kindtr = signed_body("cores3", "lcd349", 1, 1, k0, 30000);
    CHECK(enforce_update(&b, kindtr, LEN + OTA_SIG_TRAILER, 0, false, &o) == OTA_FLOW_SIG && o.sig.res == OTA_SIG_KIND,
          "5.3 case 3b: a trailer of another kind: %s", ota_sig_code(o.sig.res));
    // 4. a cut body: the sender stops (net_closed), a cut file (the trailer is not at the end: sig_none), a wrong
    // image length in the trailer (sig_form)
    uint8_t *good1 = signed_body("lcd349", "lcd349", 1, 1, k0, 30000);
    fake_init(good1, LEN + OTA_SIG_TRAILER, 4096);
    g_f.cut_at = 50000;
    CHECK(run(OTA_SIG_ENFORCE, g_keys, b.minsv, &o) == OTA_FLOW_RECV && o.recv == 0 && !g_f.boot,
          "5.3 case 4: the sender stops");
    CHECK(enforce_update(&b, good1, LEN + OTA_SIG_TRAILER - 300, 0, false, &o) == OTA_FLOW_SIG &&
                  o.sig.res == OTA_SIG_NONE,
          "5.3 case 4b: a cut file: %s", ota_sig_code(o.sig.res));
    uint8_t *form = signed_body("lcd349", "lcd349", 1, 1, k0, 30000);
    form[LEN + 12] ^= 0x04;
    CHECK(enforce_update(&b, form, LEN + OTA_SIG_TRAILER, 0, false, &o) == OTA_FLOW_SIG && o.sig.res == OTA_SIG_FORM,
          "5.3 case 4c: a wrong image length: %s", ota_sig_code(o.sig.res));
    CHECK(b.runs == 0 && b.minsv == 0 && b.bad == -1, "cases 1-4: the old firmware runs, minsv 0");
    // 6. signed by key 2 of the list only (sec_ver 1): taken; the self-test passes at 20 s, the mark, minsv 1
    uint8_t *key2 = signed_body("lcd349", "lcd349", 1, 1, k1, 30000);
    CHECK(enforce_update(&b, key2, LEN + OTA_SIG_TRAILER, 3000, false, &o) == OTA_FLOW_OK && o.sig.key == 1 &&
                  b.runs == 1 && b.mark_at == OTA_SELF_MARK_MS && b.minsv == 1,
          "5.3 case 6: key 2: %d key %d mark %d minsv %u", o.res, o.sig.key, (int)b.mark_at, b.minsv);
    // 7. signed by key 1 (sec_ver 1): taken, pending, marked after 20 s, minsv 1 (the web server starts late: the
    // mark waits for it)
    CHECK(enforce_update(&b, good1, LEN + OTA_SIG_TRAILER, 41000, false, &o) == OTA_FLOW_OK && o.sig.key == 0 &&
                  b.mark_at == 41000 && b.minsv == 1,
          "5.3 case 7: key 1: mark %d minsv %u", (int)b.mark_at, b.minsv);
    // 5. sec_ver 0 after minsv 1: refused, sig_old (the board stays on the firmware of case 7)
    uint8_t *sv0 = signed_body("lcd349", "lcd349", 0, 1, k0, 30000);
    CHECK(enforce_update(&b, sv0, LEN + OTA_SIG_TRAILER, 0, false, &o) == OTA_FLOW_SIG && o.sig.res == OTA_SIG_OLD &&
                  b.minsv == 1,
          "5.3 case 5: sec_ver 0 after minsv 1: %s", ota_sig_code(o.sig.res));
    // 8. a signed crash-test build: taken, then its self-test fails at 20 s: back to the old firmware, minsv stays
    uint8_t *sv2 = signed_body("lcd349", "lcd349", 2, 1, k0, 30000);
    CHECK(enforce_update(&b, sv2, LEN + OTA_SIG_TRAILER, 0, true, &o) == OTA_FLOW_OK && b.runs == 0 && b.bad == 1 &&
                  b.back_at == OTA_SELF_MARK_MS && b.mark_at == -1 && b.minsv == 1,
          "5.3 case 8: the crash test: runs %d bad %d back %d minsv %u", b.runs, b.bad, (int)b.back_at, b.minsv);
    // the same image with no crash test: the mark, minsv 2
    CHECK(enforce_update(&b, sv2, LEN + OTA_SIG_TRAILER, 0, false, &o) == OTA_FLOW_OK &&
                  b.mark_at == OTA_SELF_MARK_MS && b.minsv == 2,
          "sec_ver 2: minsv %u", b.minsv);
    // the web server never starts: back at 90 s, no mark, minsv stays
    uint8_t *sv3 = signed_body("lcd349", "lcd349", 3, 1, k0, 30000);
    CHECK(enforce_update(&b, sv3, LEN + OTA_SIG_TRAILER, -1, false, &o) == OTA_FLOW_OK &&
                  b.back_at == OTA_SELF_WEB_MS && b.minsv == 2 && b.runs == 0,
          "no web server: back %d minsv %u", (int)b.back_at, b.minsv);
    // REPORT (the release that runs now): an unsigned image is taken and marked at 20 s with no self-test
    board_t r = {.minsv = 0, .bad = -1};
    fake_init(unsigned_img, LEN + OTA_SIG_TRAILER, 4096);
    CHECK(run(OTA_SIG_REPORT, g_keys, 0, &o) == OTA_FLOW_OK && o.sig.res == OTA_SIG_NONE, "REPORT takes sig_none");
    new_firmware(&r, OTA_SIG_REPORT, g_f.slot, g_f.written, -1, true);
    CHECK(r.runs == 1 && r.mark_at == OTA_SELF_MARK_MS && r.minsv == 0, "REPORT: the mark at 20 s, no self-test");
    free(unsigned_img);
    free(badsig);
    free(otherkey);
    free(cores3);
    free(kindtr);
    free(good1);
    free(form);
    free(key2);
    free(sv0);
    free(sv2);
    free(sv3);
}

// ---------------------------------------------------------------- step 1 of the change of the layout (pure parts)
// ota_sig_copy_len: the trailer goes with the image only when it is a trailer of a good form for this image length;
// ota_sig_copy_again; ota_sig_back_plan.
static void copy_len_cases(void)
{
    const int k0[] = {0};
    uint8_t *img = signed_body("lcd349", "lcd349", 1, 1, k0, 30000);
    CHECK(ota_sig_copy_len(img + LEN, OTA_SIG_TRAILER, LEN) == LEN + OTA_SIG_TRAILER, "copy len: a signed image");
    CHECK(ota_sig_copy_len(img + LEN, OTA_SIG_TRAILER - 1, LEN) == LEN, "copy len: a cut trailer");
    CHECK(ota_sig_copy_len(img + LEN, OTA_SIG_TRAILER, LEN - 16) == LEN - 16, "copy len: another image length");
    CHECK(ota_sig_copy_len(img + LEN, OTA_SIG_TRAILER, UINT32_MAX - 10) == UINT32_MAX - 10, "copy len: no overflow");
    uint8_t ff[OTA_SIG_TRAILER];
    memset(ff, 0xFF, sizeof ff);
    CHECK(ota_sig_copy_len(ff, sizeof ff, LEN) == LEN, "copy len: erased flash after the image");
    CHECK(ota_sig_copy_len(NULL, 0, LEN) == LEN, "copy len: nothing");
    img[LEN + 3] ^= 1;  // (the magic)
    CHECK(ota_sig_copy_len(img + LEN, OTA_SIG_TRAILER, LEN) == LEN, "copy len: a wrong magic");
    img[LEN + 3] ^= 1;
    img[LEN + 120] = 1;  // (a byte that must be zero: a wrong form)
    CHECK(ota_sig_copy_len(img + LEN, OTA_SIG_TRAILER, LEN) == LEN, "copy len: a wrong form");
    free(img);
    CHECK(ota_sig_copy_again(false, false) && ota_sig_copy_again(true, false) && ota_sig_copy_again(false, true) &&
                  !ota_sig_copy_again(true, true),
          "copy again: not after a bad copy of the same build");
    CHECK(ota_sig_back_plan(true, true, true) == OTA_BACK_ROLLBACK &&
                  ota_sig_back_plan(true, true, false) == OTA_BACK_ROLLBACK &&
                  ota_sig_back_plan(true, false, true) == OTA_BACK_KEEP_MARK &&
                  ota_sig_back_plan(false, true, true) == OTA_BACK_KEEP_MARK &&
                  ota_sig_back_plan(false, false, true) == OTA_BACK_KEEP_MARK &&
                  ota_sig_back_plan(false, false, false) == OTA_BACK_KEEP,
          "the back plan");
    CHECK(!strcmp(ota_sig_back_name(OTA_BACK_KEEP_MARK), "keep_mark"), "the back plan name");
}

// ---------------------------------------------------------------- the change of the layout in ENFORCE (a model)
// A fake board with the old table: two app slots, the two otadata entries of ESP-IDF (the slot, the state, the
// sequence number), the bootloader with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE (bootloader_utility.c: a PENDING entry
// becomes ABORTED; no valid entry = ota_0; a slot with no image = the other slot), the ESP-IDF calls
// (esp_ota_set_boot_partition, esp_ota_check_rollback_is_possible, the marks), and the firmware in ENFORCE built for
// the slim table: the self-test of ota.c (ota_sig_self_test, self_back with ota_sig_back_plan), layout_tick (a
// restart after the mark) and step 1 of layout.c (copy_allowed, copy_to_ota0).
enum {
    E_EMPTY,  // (seq 0xFFFFFFFF)
    E_NEW,
    E_PEND,
    E_VALID,
    E_INVALID,
    E_ABORTED
};

typedef struct {
    int slot, state;
    uint32_t seq;
} ent_t;

typedef struct {
    uint8_t img[2][SLOT];
    ent_t e[2];
    uint32_t minsv;
    int run;           // the slot that runs
    int boots;         // the starts
    int restarts;      // the restarts by the firmware (esp_restart and the rollback)
    int fails;         // the failed self-tests
    const char *self;  // "self" of /api/boards
    const char *lay;   // layout_state()
    bool done;         // the firmware runs from ota_0 and is good: step 2 of the change can start
    // the code of the build
    bool lay_fixed;   // step 1 copies the trailer and checks the copy (else: the image only, no check)
    bool back_fixed;  // self_back with ota_sig_back_plan (else: the rollback call, then esp_restart)
    bool guard;       // copy_allowed (else: step 1 at each start from ota_1)
} fdev_t;

static fdev_t *g_d;

static bool ent_valid(const ent_t *e)
{
    return e->state != E_EMPTY && e->state != E_INVALID && e->state != E_ABORTED;
}

static int ent_active(fdev_t *d)
{
    int a = -1;
    for (int i = 0; i < 2; i++) {
        if (ent_valid(&d->e[i]) && (a < 0 || d->e[i].seq > d->e[a].seq)) {
            a = i;
        }
    }
    return a;
}

static bool img_ok(fdev_t *d, int s)
{
    return d->img[s][0] == 0xE9;
}

// esp_ota_get_state_partition: the state of the entry of the slot with the highest sequence (-1: no entry)
static int slot_state(fdev_t *d, int s)
{
    int k = -1;
    for (int i = 0; i < 2; i++) {
        if (d->e[i].state != E_EMPTY && d->e[i].slot == s && (k < 0 || d->e[i].seq > d->e[k].seq)) {
            k = i;
        }
    }
    return k < 0 ? -1 : d->e[k].state;
}

// esp_ota_set_boot_partition (esp_rewrite_ota_data)
static void set_boot(fdev_t *d, int s)
{
    int a = ent_active(d);
    if (a < 0) {
        d->e[0] = (ent_t){.slot = s, .state = E_NEW, .seq = (uint32_t)s + 1};
        return;
    }
    if (d->e[a].slot == s) {
        d->e[a].state = E_NEW;
        return;
    }
    uint32_t q = d->e[a].seq + 1;
    while ((q - 1) % 2 != (uint32_t)s) {
        q++;
    }
    d->e[1 - a] = (ent_t){.slot = s, .state = E_NEW, .seq = q};
}

static bool rollback_possible(fdev_t *d)
{
    int a = ent_active(d);
    if (a < 0) {
        return false;
    }
    const ent_t *l = &d->e[1 - a];
    return ent_valid(l) && img_ok(d, l->slot);
}

// the bootloader: the slot that starts
static void bootloader(fdev_t *d)
{
    for (int i = 0; i < 2; i++) {
        if (d->e[i].state == E_PEND) {
            d->e[i].state = E_ABORTED;
        }
    }
    int a = ent_active(d), s = 0;
    if (a >= 0) {
        s = d->e[a].slot;
        if (d->e[a].state == E_NEW) {
            d->e[a].state = E_PEND;
        }
    }
    if (!img_ok(d, s)) {
        s = 1 - s;
    }
    d->run = s;
    d->boots++;
}

// the check of the trailer of the image in slot s (part_check of ota.c): the trailer directly after the image
static ota_sig_result_t g_own;
static ota_sig_res_t dev_check(fdev_t *d, int s)
{
    uint8_t h[32];
    hc_sha256(d->img[s], LEN, h);
    char ver[33];
    snprintf(ver, sizeof ver, "%.32s", (const char *)d->img[s] + OTA_FLOW_DESC_VER);
    ota_sig_want_t w = {.keys = g_keys, .nkeys = OTA_SIG_KEYS, .kind = "lcd349", .version = ver, .minsv = d->minsv};
    return ota_sig_check(d->img[s] + LEN, OTA_SIG_TRAILER, LEN + OTA_SIG_TRAILER, h, &w, &TU_CRYPTO, &g_own);
}

// slot_is_tfn of ota.c: the board tag of this kind in the image
static bool dev_tfn(fdev_t *d, int s)
{
    bool own = false;
    char other[16] = "";
    ota_flow_scan_tags("@ARCADE-BOARD:lcd349@", (const char *)d->img[s], LEN, &own, other);
    return img_ok(d, s) && own;
}

// one start of the firmware in ENFORCE; false = it runs on with no restart (the end of the model)
static bool dev_start(fdev_t *d)
{
    int st = slot_state(d, d->run);
    bool pend = st == E_PEND;
    bool valid = st < 0 || st == E_VALID;  // (app_valid of layout.c; no entry = a USB flash)
    // layout_boot: step 1 from ota_1 (never at a pending start)
    if (valid && d->run == 1) {
        const ent_t *bad = NULL;
        for (int i = 0; i < 2; i++) {
            if ((d->e[i].state == E_INVALID || d->e[i].state == E_ABORTED) && d->e[i].slot == 0) {
                bad = &d->e[i];
            }
        }
        bool same = !memcmp(d->img[0], d->img[1], LEN);
        if (d->guard && !ota_sig_copy_again(bad != NULL, same)) {
            d->lay = "copyfail";
            return false;
        }
        uint32_t all = d->lay_fixed ? ota_sig_copy_len(d->img[1] + LEN, OTA_SIG_TRAILER, LEN) : LEN;
        if (d->lay_fixed && dev_check(d, 1) != OTA_SIG_OK) {
            d->lay = "copyfail";  // (the own trailer of ota_1 does not verify: no copy)
            return false;
        }
        memset(d->img[0], 0xFF, SLOT);  // (the erase)
        memcpy(d->img[0], d->img[1], all);
        if (d->lay_fixed && dev_check(d, 0) != OTA_SIG_OK) {
            d->lay = "copyfail";
            return false;
        }
        set_boot(d, 0);
        d->restarts++;
        return true;
    }
    if (!pend) {
        d->done = valid && d->run == 0;
        return false;
    }
    // the self-test at 20 s (the web server runs)
    const char *why;
    ota_self_t r = ota_sig_self_test(OTA_SIG_ENFORCE, true, OTA_SELF_MARK_MS, dev_check(d, d->run), true, &why);
    int a = ent_active(d);
    if (r == OTA_SELF_MARK) {
        d->e[a].state = E_VALID;
        d->minsv = ota_sig_new_minsv(d->minsv, &g_own);  // (sig_after_mark)
        d->self = "pass";
        d->restarts++;  // (layout_tick: the firmware is good, a restart for the change)
        return true;
    }
    d->fails++;
    bool can = rollback_possible(d);
    if (!d->back_fixed) {
        if (can) {
            d->e[a].state = E_INVALID;
        }
        d->restarts++;  // (the rollback, or esp_restart in the pending state)
        return true;
    }
    ota_back_t b = ota_sig_back_plan(can, can && dev_tfn(d, d->e[1 - a].slot), true);
    if (b == OTA_BACK_ROLLBACK) {
        d->e[a].state = E_INVALID;
        d->self = "back";
        d->restarts++;
        return true;
    }
    CHECK(b == OTA_BACK_KEEP_MARK, "a firmware with /api/ota keeps and marks");
    d->e[a].state = E_VALID;
    d->self = "kept";
    return false;
}

// the release (image + trailer) by an update into ota_1, from the old firmware in ota_0; then the starts up to 40
static void dev_run(fdev_t *d, const uint8_t *rel, int rel_len)
{
    memset(d->img[1], 0xFF, SLOT);
    memcpy(d->img[1], rel, (size_t)rel_len);
    set_boot(d, 1);
    for (int i = 0; i < 40; i++) {
        bootloader(d);
        if (!dev_start(d)) {
            return;
        }
    }
}

// a new board: the old firmware 1.0.40 (no trailer) in ota_0 by a USB flash (the otadata is empty)
static void dev_new(fdev_t *d, bool lay_fixed, bool back_fixed, bool guard)
{
    memset(d, 0, sizeof *d);
    uint8_t *old = tu_image(LEN, "lcd349", "1.0.40", 20000, 5);
    memset(d->img[0], 0xFF, SLOT);
    memset(d->img[1], 0xFF, SLOT);
    memcpy(d->img[0], old, LEN);
    free(old);
    d->minsv = 0;
    d->self = d->lay = "";
    d->lay_fixed = lay_fixed;
    d->back_fixed = back_fixed;
    d->guard = guard;
}

static void layout_cases(void)
{
    const int k0[] = {0};
    uint8_t *rel = signed_body("lcd349", "lcd349", 1, 1, k0, 30000);  // (the release: sec_ver 1)
    fdev_t *d = g_d = calloc(1, sizeof *d);

    // the fault (the code of 1.0.102): step 1 copies the image only; ota_0 fails its self-test (sig_none); the
    // rollback goes to ota_1, which copies again: a restart loop
    dev_new(d, false, false, false);
    dev_run(d, rel, LEN + OTA_SIG_TRAILER);
    CHECK(d->boots >= 40 && d->fails >= 10 && !d->done, "the fault of 1.0.102: a loop (boots %d, fails %d)", d->boots,
          d->fails);

    // (a) the fix: step 1 copies the image and the trailer; the own check of the copy in ota_0 passes; ota_0 passes
    // its self-test, marks itself good: step 2 can start
    dev_new(d, true, true, true);
    dev_run(d, rel, LEN + OTA_SIG_TRAILER);
    CHECK(d->done && d->run == 0 && d->fails == 0 && !strcmp(d->self, "pass") && d->boots == 4 && d->restarts == 3 &&
                  d->minsv == 1,
          "(a) the copy with the trailer: done %d run %d fails %d boots %d", d->done, d->run, d->fails, d->boots);
    CHECK(!memcmp(d->img[0], rel, LEN + OTA_SIG_TRAILER) && dev_check(d, 0) == OTA_SIG_OK,
          "(a) ota_0 = the image + the trailer, the trailer check passes on the copy");
    CHECK(slot_state(d, 0) == E_VALID, "(a) ota_0 is marked good");

    // (b) the old copy (no trailer) where ESP-IDF cannot go back (the otadata entry of ota_1 is empty: the release was
    // flashed into ota_1 by USB): the self-test fails, no rollback, NO restart: the firmware in ota_0 runs on, "kept"
    dev_new(d, false, true, true);
    memcpy(d->img[1], rel, LEN + OTA_SIG_TRAILER);
    d->run = 1;
    d->boots = 1;
    for (int i = 0; i < 40 && dev_start(d); i++) {
        bootloader(d);
    }
    CHECK(d->fails == 1 && d->run == 0 && !strcmp(d->self, "kept") && d->restarts == 1 && d->boots == 2 &&
                  slot_state(d, 0) == E_VALID && d->minsv == 0,
          "(b) no rollback: it runs on (fails %d run %d self %s restarts %d boots %d)", d->fails, d->run, d->self,
          d->restarts, d->boots);
    // ... and a later restart (a power cut) starts the same firmware: no loop through the bootloader
    bootloader(d);
    CHECK(d->run == 0 && !dev_start(d) && d->done, "(b) a power cut: ota_0 starts again, no self-test");

    // (c) the old copy, where ESP-IDF can go back to ota_1 (a TFN OS image): the rollback (as before); then ota_1 does
    // not copy again (copy_allowed): the board runs on ota_1 with the old table, "copyfail"
    dev_new(d, false, true, true);
    dev_run(d, rel, LEN + OTA_SIG_TRAILER);
    CHECK(d->fails == 1 && !strcmp(d->self, "back") && d->run == 1 && !strcmp(d->lay, "copyfail") && d->boots == 4 &&
                  slot_state(d, 0) == E_INVALID && slot_state(d, 1) == E_VALID,
          "(c) the rollback, then no new copy (fails %d self %s run %d lay %s boots %d)", d->fails, d->self, d->run,
          d->lay, d->boots);
    // ... the same with no copy guard: the loop of the fault
    dev_new(d, false, true, false);
    dev_run(d, rel, LEN + OTA_SIG_TRAILER);
    CHECK(d->boots >= 40, "(c) with no copy guard: a loop (boots %d)", d->boots);

    // (d) ESP-IDF can go back, but the other slot (ota_0, a good entry) has no TFN OS of this kind: no rollback, the
    // new firmware in ota_1 (a bad own trailer) runs on and marks itself good; minsv does not change
    dev_new(d, true, true, true);
    memset(d->img[0] + 20000, 'x', 30);  // (the image in ota_0 has no board tag)
    d->e[0] = (ent_t){.slot = 0, .state = E_VALID, .seq = 1};
    memcpy(d->img[1], rel, LEN + OTA_SIG_TRAILER);
    d->img[1][LEN + 100] ^= 1;  // (a signed byte: sig_bad, as the crash test)
    set_boot(d, 1);
    bootloader(d);
    CHECK(!dev_start(d) && d->fails == 1 && d->run == 1 && !strcmp(d->self, "kept") && slot_state(d, 1) == E_VALID &&
                  d->restarts == 0 && d->minsv == 0,
          "(d) the other slot is not TFN OS: it runs on (run %d self %s)", d->run, d->self);
    // ... the same with a TFN OS image in ota_0: the rollback (as before)
    dev_new(d, true, true, true);
    d->e[0] = (ent_t){.slot = 0, .state = E_VALID, .seq = 1};
    memcpy(d->img[1], rel, LEN + OTA_SIG_TRAILER);
    d->img[1][LEN + 100] ^= 1;
    set_boot(d, 1);
    bootloader(d);
    CHECK(dev_start(d) && !strcmp(d->self, "back") && slot_state(d, 1) == E_INVALID, "(c2) the crash test: back");
    bootloader(d);
    CHECK(d->run == 0 && !dev_start(d), "(c2) the old firmware in ota_0 runs");

    // (e) the fixed step 1, but the firmware in ota_1 has no trailer (a USB flash of an unsigned build in ENFORCE):
    // no copy, no restart, "copyfail"
    dev_new(d, true, true, true);
    uint8_t *nosig = tu_image(LEN, "lcd349", "1.0.97", 30000, 21);
    memcpy(d->img[1], nosig, LEN);
    d->run = 1;
    CHECK(!dev_start(d) && !strcmp(d->lay, "copyfail") && d->restarts == 0 && d->img[0][0] == 0xE9 &&
                  memcmp(d->img[0], d->img[1], LEN) != 0,
          "(e) no trailer in ota_1: no copy, ota_0 stays");
    free(nosig);
    free(d);
    free(rel);
}

int main(int argc, char **argv)
{
    uint8_t d[32];
    tu_key(0, d, g_keys[0]);
    tu_key(1, d, g_keys[1]);
    basic_cases();
    board_cases();
    mark_cases();
    design_cases();
    copy_len_cases();
    layout_cases();
    int nv = argc > 1 ? vector_cases(argv[1]) : 0;
    free(g_f.hashed);
    printf("OTA FLOW %s (%d vectors of fw_sign.py)\n", g_fails ? "FAILED" : "OK", nv);
    return g_fails ? 1 : 0;
}
