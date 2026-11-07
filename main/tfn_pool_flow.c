// The pool service with no ESP-IDF call (tfn_pool_flow.h): the rules of the pools (P4 stage 2, sections 2.2 to 2.7
// of the design) on the formats of tfn_pool_core.h. Two locks of the caller: the CONTROL lock (tfn_pool.c) around
// each function of the JOIN, the pool changes and the request header; the HOT lock (io->hot_lock) around the short
// parts that read or change the state of the packet path (the frames, the UDP tails, the beacons). The slow work (a
// signature, a file write, the NVS) runs outside the HOT lock, so the WiFi task never waits for it.
#include "tfn_pool_flow.h"
#include <stdio.h>
#include <string.h>

#define NV_KEY   "pk"   // the pool key of the current epoch (32)
#define NV_EPOCH "pke"  // the epoch of that key (u32)
#define NV_WK    "wk"   // the request key of the pool manager (32)
#define NV_BOX   "bx"   // the private box key (32; tfn_id.c makes it)
#define NV_FLOOR "rv"   // the floor of the roster version (u32)
#define NV_CTR   "lc"   // the next free counter block (u32)

static bool eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d |= a[i] ^ b[i];
    }
    return d == 0;
}

static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) {
        *v++ = 0;
    }
}

static void hot_lock(tpf_t *p)
{
    if (p->io->hot_lock) {
        p->io->hot_lock();
    }
}

static void hot_unlock(tpf_t *p)
{
    if (p->io->hot_unlock) {
        p->io->hot_unlock();
    }
}

int tpf_http(const char *code)
{
    static const char *const C409[] = {"wait", "no_join", "busy", "time", "none", "id"};
    if (!code) {
        return 200;
    }
    if (!strcmp(code, "form")) {
        return 400;
    }
    if (!strcmp(code, "auth")) {
        return 401;
    }
    if (!strcmp(code, "nvs") || !strcmp(code, "file")) {
        return 500;
    }
    for (size_t i = 0; i < sizeof C409 / sizeof C409[0]; i++) {
        if (!strcmp(code, C409[i])) {
            return 409;
        }
    }
    return 403;
}

const char *tpf_text(const char *code)
{
    static const char *const T[][2] = {
            {"form", "a bad request"},
            {"auth", "only the pool manager (the request header), the USB console or the screen"},
            {"wait", "the board waits for ALLOW"},
            {"no_join", "the JOIN screen of the board is not open"},
            {"busy", "a JOIN runs, or the board has a pool"},
            {"token", "no token JOIN (a wrong token, a pool, or not a LEGACY build)"},
            {"commit", "the commitment is not right"},
            {"sig", "the signature is not right"},
            {"admin", "not an admin key of the pool"},
            {"old", "not newer than the pool data of the board"},
            {"other", "another pool"},
            {"member", "the roster does not list this board"},
            {"env", "the envelope does not open"},
            {"digits", "the digits are not right"},
            {"no", "NO on the board"},
            {"time", "time out"},
            {"none", "the board has no pool"},
            {"child", "a CHILD board cannot leave on its screen"},
            {"id", "the board has no device ID yet"},
            {"nvs", "the NVS is full"},
            {"file", "the roster file is not written (is /data full?)"},
    };
    for (size_t i = 0; code && i < sizeof T / sizeof T[0]; i++) {
        if (!strcmp(code, T[i][0])) {
            return T[i][1];
        }
    }
    return code ? code : "";
}

void tpf_mid(const tpf_t *p, const uint8_t pk[65], uint8_t id[8])
{
    tfn_pool_dev_id(p->io->ops, pk, id);
}

// The core result of a roster check as a code of the contract
static const char *rule_code(int rc)
{
    switch (rc) {
    case TFN_POOL_R_OK:
    case TFN_POOL_R_REMOVED:
        return NULL;
    case TFN_POOL_E_SIG:
        return "sig";
    case TFN_POOL_E_ADMIN:
        return "admin";
    case TFN_POOL_E_POOL:
        return "other";
    case TFN_POOL_E_OLD:
        return "old";
    case TFN_POOL_E_SELF:
        return "member";
    default:
        return "form";
    }
}

// The check of a roster of the pool manager with the stored roster (the trust of an update)
static int check_update(tpf_t *p, const uint8_t *rb, size_t n, tfn_pool_roster_t *out)
{
    const tfn_pool_trust_t t = {.join = false,
                                .pool = p->r.pool,
                                .admin = (const uint8_t (*)[TFN_POOL_PK_LEN])p->r.admin,
                                .nadmin = p->r.nadmin,
                                .floor = p->rv,
                                .self = p->me.id};
    return tfn_pool_roster_check(p->io->ops, &t, rb, n, out);
}

// The check of a JOIN roster: the admin key of the JOIN must sign it (no version floor)
static int check_join(tpf_t *p, const uint8_t *rb, size_t n, const uint8_t apk[65], tfn_pool_roster_t *out)
{
    const tfn_pool_trust_t t = {
            .join = true, .admin = (const uint8_t (*)[TFN_POOL_PK_LEN])apk, .nadmin = 1, .self = p->me.id};
    return tfn_pool_roster_check(p->io->ops, &t, rb, n, out);
}

// A roster that only its own signature protects (the load of the file, the repair of a damaged file with the request
// key): a JOIN check with the admin keys of the roster itself, then the version floor.
static int check_self(tpf_t *p, const uint8_t *rb, size_t n, const uint8_t *self, tfn_pool_roster_t *out)
{
    int rc = tfn_pool_roster_parse(rb, n, out);
    if (rc) {
        return rc;
    }
    uint8_t keys[TFN_POOL_ADMINS][TFN_POOL_PK_LEN];
    memcpy(keys, out->admin, sizeof keys);
    const tfn_pool_trust_t t = {
            .join = true, .admin = (const uint8_t (*)[TFN_POOL_PK_LEN])keys, .nadmin = out->nadmin, .self = self};
    rc = tfn_pool_roster_check(p->io->ops, &t, rb, n, out);
    if ((rc == TFN_POOL_R_OK || rc == TFN_POOL_E_SELF) && p->rv && out->ver < p->rv) {
        rc = TFN_POOL_E_OLD;
    }
    return rc;
}

// ---------------------------------------------------------------- the load

static void load(tpf_t *p)
{
    const tpf_io_t *io = p->io;
    p->rn = io->file_read(p->rb, sizeof p->rb);
    if (!p->rn) {
        return;
    }
    p->state = TFN_POOL_DAMAGED;
    p->wk_on = io->nvs_get(NV_WK, p->wk, 32);
    static const uint8_t NO_ID[8];
    const int rc = check_self(p, p->rb, p->rn, p->me.ok ? p->me.id : NO_ID, &p->r);
    int slot = tfn_pool_roster_find(&p->r, p->me.id);
    if (rc == TFN_POOL_E_SELF && !p->me.ok) {
        slot = tfn_pool_roster_slot(&p->r, p->me.mac);  // (no device ID yet at this start: the MAC finds the slot)
    }
    uint8_t key[32];
    uint32_t epoch = 0, lc = 0;
    if ((rc != TFN_POOL_R_OK && rc != TFN_POOL_E_SELF) || slot < 0 || !io->nvs_get(NV_KEY, key, 32) ||
        !io->nvs_get_u32(NV_EPOCH, &epoch)) {
        return;
    }
    tfn_pool_keys_t k;
    tfn_pool_derive(io->ops, key, p->r.pool, epoch, &k);
    wipe(key, sizeof key);
    tfn_pool_rx_init(&p->rx, &p->r, &k, NULL);
    wipe(&k, sizeof k);
    io->nvs_get_u32(NV_CTR, &lc);
    tfn_pool_ctr_init(&p->ctr, lc);
    p->my_slot = slot;
    p->state = TFN_POOL_OK;
}

void tpf_init(tpf_t *p, const tpf_io_t *io, const tpf_me_t *me, bool legacy)
{
    memset(p, 0, sizeof *p);
    p->io = io;
    p->me = *me;
    p->legacy = legacy;
    p->my_slot = -1;
    io->nvs_get_u32(NV_FLOOR, &p->rv);
    load(p);
    p->gen = 1;
}

void tpf_me(tpf_t *p, const tpf_me_t *me)
{
    p->me = *me;
}

const uint8_t *tpf_bn(tpf_t *p)
{
    if (!p->bn_on) {
        p->io->random(p->bn, sizeof p->bn);
        p->bn_on = true;
    }
    return p->bn;
}

// ---------------------------------------------------------------- the changes of the pool

// The new state after a good roster: the secrets into the NVS first, then the file, then the swap in RAM.
// key = NULL: the pool key does not change. wk = NULL: the request key does not change.
static const char *commit(tpf_t *p, const uint8_t *rb, size_t rn, const tfn_pool_roster_t *r, int slot,
                          const uint8_t *key, uint32_t epoch, const uint8_t *wk, bool join)
{
    const tpf_io_t *io = p->io;
    tfn_pool_keys_t nk;
    bool have = false;
    if (key) {
        if (!io->nvs_set(NV_KEY, key, 32) || !io->nvs_set_u32(NV_EPOCH, epoch)) {
            return "nvs";
        }
        tfn_pool_derive(io->ops, key, r->pool, epoch, &nk);
        have = true;
    } else if (!p->rx.on) {
        // (a damaged roster file: the pool key is still in the NVS)
        uint8_t old[32];
        have = io->nvs_get(NV_KEY, old, 32) && io->nvs_get_u32(NV_EPOCH, &epoch);
        if (have) {
            tfn_pool_derive(io->ops, old, r->pool, epoch, &nk);
        }
        wipe(old, sizeof old);
        if (!have) {
            return "env";  // (no pool key: the manager sends op=key)
        }
    }
    const bool fresh = join || !p->rx.on;
    const bool new_key = have && (fresh || memcmp(&nk, &p->rx.key[0], sizeof nk) != 0);
    if (wk && !io->nvs_set(NV_WK, wk, 32)) {
        return "nvs";
    }
    if (new_key) {
        io->nvs_set_u32(NV_CTR, 0);  // (a new key: the counter starts again at 0)
    }
    io->nvs_set_u32(NV_FLOOR, r->ver);
    if ((rb != p->rb || rn != p->rn) && !io->file_write(rb, rn)) {
        return "file";
    }
    if (join) {
        io->mgr_clear();
    }
    hot_lock(p);
    if (fresh) {
        tfn_pool_rx_init(&p->rx, r, &nk, NULL);
    } else {
        tfn_pool_rx_roster(&p->rx, r);
        if (new_key) {
            tfn_pool_rx_rekey(&p->rx, &nk);
            p->prev_until = io->now_ms() + TPF_PREV_MS;
        }
    }
    if (new_key) {
        tfn_pool_ctr_init(&p->ctr, 0);
    }
    p->r = *r;
    if (rb != p->rb) {
        memcpy(p->rb, rb, rn);
    }
    p->rn = rn;
    p->my_slot = slot;
    p->rv = r->ver;
    if (wk) {
        memcpy(p->wk, wk, 32);
        p->wk_on = true;
    }
    p->state = TFN_POOL_OK;
    p->gen++;
    hot_unlock(p);
    wipe(&nk, sizeof nk);
    return NULL;
}

// The pool data goes (LEAVE, a removal). The floors (rv, lc) and the box key stay.
static void erase(tpf_t *p)
{
    const tpf_io_t *io = p->io;
    io->file_erase();
    io->nvs_erase(NV_KEY);
    io->nvs_erase(NV_EPOCH);
    io->nvs_erase(NV_WK);
    hot_lock(p);
    wipe(&p->rx, sizeof p->rx);
    wipe(p->wk, sizeof p->wk);
    memset(&p->r, 0, sizeof p->r);
    p->rn = 0;
    p->wk_on = false;
    p->my_slot = -1;
    p->state = TFN_POOL_NONE;
    p->gen++;
    hot_unlock(p);
}

// ---------------------------------------------------------------- the JOIN

static bool join_live(const tpf_t *p)
{
    return p->j.step >= TFN_POOL_JOIN_WAIT && p->j.step <= TFN_POOL_JOIN_ALLOWED;
}

static const char *fail(tpf_t *p, const char *why)
{
    p->j.step = TFN_POOL_JOIN_FAIL;
    p->j.why = why;
    wipe(p->j.eb_sk, sizeof p->j.eb_sk);
    return why;
}

// A new ephemeral key E_b and the nonce N_b.
static bool ephemeral(tpf_t *p)
{
    p->io->random(p->j.eb_sk, 32);
    p->io->random(p->j.nb, 16);
    return p->io->x25519_pub(p->j.eb_sk, p->j.eb);
}

const char *tpf_join_start(tpf_t *p)
{
    tpf_join_step(p);
    if (p->state == TFN_POOL_OK) {
        return "busy";
    }
    if (!p->me.ok) {
        return "id";
    }
    if (join_live(p) && p->j.step != TFN_POOL_JOIN_WAIT) {
        return "busy";
    }
    wipe(&p->j, sizeof p->j);
    if (!ephemeral(p)) {
        return fail(p, "env");
    }
    p->j.step = TFN_POOL_JOIN_WAIT;
    p->j.until = p->io->now_ms() + TPF_JOIN_MS;
    return NULL;
}

void tpf_join_stop(tpf_t *p)
{
    wipe(&p->j, sizeof p->j);
    p->j.step = TFN_POOL_JOIN_OFF;
}

tfn_pool_join_t tpf_join_step(tpf_t *p)
{
    const int64_t now = p->io->now_ms();
    if (p->j.step == TFN_POOL_JOIN_WAIT && now > p->j.until) {
        fail(p, "time");
    } else if (join_live(p) && p->j.step != TFN_POOL_JOIN_WAIT && now > p->j.step_until) {
        fail(p, "time");
    }
    return p->j.step;
}

static void next_step(tpf_t *p, tfn_pool_join_t s)
{
    p->j.step = s;
    p->j.step_until = p->io->now_ms() + TPF_STEP_MS;
}

// The token JOIN (design 2.7.2): only in a LEGACY build, only with a token and no pool.
static const char *token_join(tpf_t *p, const char *auth)
{
    if (!p->legacy || p->state != TFN_POOL_NONE || !p->io->token_ok(auth)) {
        return "token";
    }
    if (join_live(p) && p->j.step != TFN_POOL_JOIN_WAIT) {
        return "busy";
    }
    wipe(&p->j, sizeof p->j);
    if (!ephemeral(p)) {
        return fail(p, "env");
    }
    p->j.token = true;
    p->j.until = p->io->now_ms() + TPF_JOIN_MS;
    return NULL;
}

const char *tpf_hello(tpf_t *p, const uint8_t apk[65], const uint8_t c[32], const char *name, const char *auth,
                      tpf_hello_t *out)
{
    tpf_join_step(p);
    if (!p->me.ok) {
        return "id";
    }
    if (apk[0] != 4) {
        return "form";
    }
    if (auth) {
        const char *e = token_join(p, auth);
        if (e) {
            return e;
        }
    } else if (p->j.step != TFN_POOL_JOIN_WAIT) {
        return join_live(p) ? "busy" : "no_join";
    }
    memcpy(p->j.apk, apk, 65);
    if (name && name[0]) {
        snprintf(p->j.mname, sizeof p->j.mname, "%s", name);
    } else {
        uint8_t mid[8];
        tpf_mid(p, apk, mid);
        for (int i = 0; i < 8; i++) {
            snprintf(p->j.mname + 2 * i, 3, "%02X", mid[i]);
        }
    }
    memcpy(p->j.c, c, 32);
    uint8_t m[TFN_POOL_JMSG_MAX];
    const int n = tfn_pool_join_msg(c, p->j.eb, p->j.nb, NULL, NULL, m);
    memset(out, 0, sizeof *out);
    if (!p->io->sign(m, (size_t)n, out->sig)) {
        return fail(p, "id");
    }
    memcpy(out->eb, p->j.eb, 32);
    memcpy(out->nb, p->j.nb, 16);
    memcpy(out->pk, p->me.pk, 65);
    memcpy(out->id, p->me.id, 8);
    if (p->me.box) {
        memcpy(out->bx, p->me.bx, 32);
        memcpy(out->bxs, p->me.bxs, 64);
    }
    next_step(p, TFN_POOL_JOIN_REVEAL);
    return NULL;
}

const char *tpf_reveal(tpf_t *p, const uint8_t em[32], const uint8_t nm[16], const uint8_t sig[64])
{
    if (tpf_join_step(p) != TFN_POOL_JOIN_REVEAL) {
        return "no_join";
    }
    uint8_t c[32], m[TFN_POOL_JMSG_MAX];
    tfn_pool_commit(p->io->ops, em, nm, c);
    if (!eq(c, p->j.c, 32)) {
        return fail(p, "commit");
    }
    const int n = tfn_pool_join_msg(p->j.c, p->j.eb, p->j.nb, em, nm, m);
    if (!p->io->ops->p256_verify(p->j.apk, m, (size_t)n, sig)) {
        return fail(p, "sig");
    }
    memcpy(p->j.em, em, 32);
    memcpy(p->j.nm, nm, 16);
    snprintf(p->j.sas, sizeof p->j.sas, "%06u", (unsigned)tfn_pool_sas(p->io->ops, em, p->j.eb, nm, p->j.nb));
    next_step(p, p->j.token ? TFN_POOL_JOIN_ALLOWED : TFN_POOL_JOIN_ASK);
    return NULL;
}

const char *tpf_allow(tpf_t *p, const char *digits)
{
    if (tpf_join_step(p) != TFN_POOL_JOIN_ASK) {
        return "no_join";
    }
    const int32_t v = digits ? tfn_pool_sas_parse(digits) : -1;  // (no digits: as wrong digits)
    char d[16];
    snprintf(d, sizeof d, "%06ld", (long)(v < 0 ? 0 : v));
    if (v < 0 || !eq((const uint8_t *)d, (const uint8_t *)p->j.sas, 6)) {
        return fail(p, "digits");
    }
    next_step(p, TFN_POOL_JOIN_ALLOWED);
    return NULL;
}

void tpf_no(tpf_t *p)
{
    if (join_live(p)) {
        fail(p, "no");
    }
}

// ---------------------------------------------------------------- op=set, op=key, LEAVE

// The secret envelope (design 2.7.3) with the seal key of X25519(sk, peer) and the salt
static bool env_open(tpf_t *p, const uint8_t sk[32], const uint8_t peer[32], const uint8_t *salt, size_t sn,
                     const uint8_t *env, const uint8_t *rb, size_t rn, uint32_t *epoch, uint8_t key[32], uint8_t wk[32])
{
    uint8_t k[32];
    const bool ok = tfn_pool_seal_key(p->io->ops, sk, peer, salt, sn, k) &&
                    tfn_pool_env_open(p->io->ops, k, rb, rn, env, TPF_ENV_LEN, epoch, key, wk);
    wipe(k, sizeof k);
    return ok;
}

static const char *set_join(tpf_t *p, const uint8_t *rb, size_t rn, const uint8_t *env)
{
    tfn_pool_roster_t *r = &p->tmp;
    const int rc = check_join(p, rb, rn, p->j.apk, r);
    if (rc != TFN_POOL_R_OK) {
        return fail(p, rule_code(rc));
    }
    uint8_t key[32], wk[32];
    uint32_t epoch = 0;
    if (!env_open(p, p->j.eb_sk, p->j.em, p->j.c, 32, env, rb, rn, &epoch, key, wk)) {
        return fail(p, "env");
    }
    const char *e = commit(p, rb, rn, r, tfn_pool_roster_find(r, p->me.id), key, epoch, wk, true);
    wipe(key, sizeof key);
    wipe(wk, sizeof wk);
    if (e) {
        return fail(p, e);
    }
    wipe(&p->j, sizeof p->j);
    p->j.step = TFN_POOL_JOIN_DONE;
    return NULL;
}

// A roster of the pool manager (no JOIN) into p->tmp: TFN_POOL_R_OK, TFN_POOL_R_REMOVED, or a fault. *same = the
// stored bytes again.
static int roster_eval(tpf_t *p, const uint8_t *rb, size_t rn, bool *same)
{
    *same = p->state == TFN_POOL_OK && rn == p->rn && !memcmp(rb, p->rb, rn);
    if (*same) {
        p->tmp = p->r;
        return TFN_POOL_R_OK;
    }
    if (p->state == TFN_POOL_OK) {
        return check_update(p, rb, rn, &p->tmp);
    }
    if (p->state == TFN_POOL_DAMAGED && p->wk_on) {
        // (the request key proves the pool manager: the roster that it signed repairs the damaged file)
        const int rc = check_self(p, rb, rn, p->me.id, &p->tmp);
        return rc == TFN_POOL_E_SELF ? TFN_POOL_R_REMOVED : rc;
    }
    return TFN_POOL_E_POOL;
}

static const char *removed(tpf_t *p, const char **what)
{
    char name[sizeof p->r.name];
    snprintf(name, sizeof name, "%s", p->state == TFN_POOL_OK ? p->r.name : p->tmp.name);
    p->io->nvs_set_u32(NV_FLOOR, p->tmp.ver);
    p->rv = p->tmp.ver;
    erase(p);
    p->io->left(name);
    *what = "removed";
    return NULL;
}

const char *tpf_set(tpf_t *p, const uint8_t *rb, size_t rl, const uint8_t *env, size_t en, bool mgr, const char **what)
{
    *what = "";
    if (rl < 8 || rl > TPF_ROSTER_MAX || (env && en != TPF_ENV_LEN)) {
        return "form";
    }
    const tfn_pool_join_t s = tpf_join_step(p);
    if (env && s == TFN_POOL_JOIN_ALLOWED) {
        const char *e = set_join(p, rb, rl, env);
        *what = e ? "" : "take";
        return e;
    }
    if (env && (s == TFN_POOL_JOIN_REVEAL || s == TFN_POOL_JOIN_ASK)) {
        return "wait";
    }
    if (!mgr) {
        return "auth";
    }
    bool same;
    const int rc = roster_eval(p, rb, rl, &same);
    if (rc == TFN_POOL_R_REMOVED) {
        return removed(p, what);
    }
    if (rc != TFN_POOL_R_OK) {
        return rule_code(rc);
    }
    if (same) {
        *what = "same";
        return NULL;
    }
    const char *e = commit(p, rb, rl, &p->tmp, tfn_pool_roster_find(&p->tmp, p->me.id), NULL, 0, NULL, false);
    *what = e ? "" : "take";
    return e;
}

const char *tpf_key(tpf_t *p, const uint8_t em[32], const uint8_t *env, size_t en, const uint8_t *rb, size_t rl,
                    bool mgr, const char **what)
{
    *what = "";
    if (!mgr) {
        return "auth";
    }
    if (en != TPF_ENV_LEN || (rb && (rl < 8 || rl > TPF_ROSTER_MAX))) {
        return "form";
    }
    bool same = true;
    if (rb) {
        const int rc = roster_eval(p, rb, rl, &same);
        if (rc == TFN_POOL_R_REMOVED) {
            return removed(p, what);
        }
        if (rc != TFN_POOL_R_OK) {
            return rule_code(rc);
        }
    } else if (p->state != TFN_POOL_OK) {
        return "none";
    } else {
        p->tmp = p->r;
        rb = p->rb;
        rl = p->rn;
    }
    uint8_t bx[32], key[32], wk[32];
    uint32_t ep = 0;
    const bool ok = p->io->nvs_get(NV_BOX, bx, 32) && env_open(p, bx, em, p->me.id, 8, env, rb, rl, &ep, key, wk);
    wipe(bx, sizeof bx);
    const char *e = "env";
    if (ok) {
        e = p->rx.on && ep < p->rx.key[0].epoch
                    ? "old"
                    : commit(p, rb, rl, &p->tmp, tfn_pool_roster_find(&p->tmp, p->me.id), key, ep, wk, false);
    }
    wipe(key, sizeof key);
    wipe(wk, sizeof wk);
    *what = e ? "" : same ? "same" : "take";
    return e;
}

const char *tpf_leave(tpf_t *p, bool by_screen)
{
    if (p->state == TFN_POOL_NONE) {
        return "none";
    }
    if (by_screen && p->state == TFN_POOL_OK && p->my_slot >= 0 && p->r.m[p->my_slot].role == TFN_POOL_ROLE_CHILD) {
        return "child";
    }
    erase(p);
    return NULL;
}

// ---------------------------------------------------------------- the requests of the pool manager

// The HMAC of the header with the request key of this board (the counter is not checked here)
static bool hdr_good(tpf_t *p, const char *method, const char *uri, const char *hdr, uint64_t *ctr)
{
    uint8_t id[8], mac[32], want[32];
    if (!p->wk_on || !p->bn_on || !method || !uri || !tfn_pool_auth_parse(hdr, id, ctr, mac)) {
        return false;
    }
    if (p->state == TFN_POOL_OK) {
        const int s = tfn_pool_roster_find(&p->r, id);  // (the ID must be a MANAGER of the roster)
        if (s < 0 || p->r.m[s].role != TFN_POOL_ROLE_MANAGER) {
            return false;
        }
    }
    tfn_pool_auth_mac(p->io->ops, p->wk, p->bn, *ctr, method, uri, want);
    return eq(want, mac, 32);
}

// The counter window of the requests: the last TPF_WEB_SEEN good counters. A counter takes when it is not in the set
// and it is higher than the lowest one of the set (when the set is full): two requests of the manager that pass each
// other on the way both take, and no counter takes two times (each counter that is no longer in the set is lower than
// the lowest one in the set).
static bool web_take(tpf_t *p, uint64_t c)
{
    int lo = 0;
    for (int i = 0; i < p->web_n; i++) {
        if (p->web_seen[i] == c) {
            return false;
        }
        lo = p->web_seen[i] < p->web_seen[lo] ? i : lo;
    }
    if (p->web_n < TPF_WEB_SEEN) {
        p->web_seen[p->web_n++] = c;
        return true;
    }
    if (c <= p->web_seen[lo]) {
        return false;
    }
    p->web_seen[lo] = c;
    return true;
}

bool tpf_web_ok(tpf_t *p, const char *method, const char *uri, const char *hdr)
{
    uint64_t c = 0;
    if (!hdr_good(p, method, uri, hdr, &c) || !c || !web_take(p, c)) {
        return false;
    }
    p->web_last = c;
    return true;
}

bool tpf_req_mgr(tpf_t *p, const char *method, const char *uri, const char *hdr)
{
    uint64_t c = 0;
    return p->web_last && hdr_good(p, method, uri, hdr, &c) && c == p->web_last;
}

void tpf_web_reset(tpf_t *p, uint64_t last)
{
    p->web_n = 0;
    p->web_last = 0;
    if (last) {
        for (int i = 0; i < TPF_WEB_SEEN; i++) {
            p->web_seen[i] = last;  // (a full set: each counter up to last is old)
        }
        p->web_n = TPF_WEB_SEEN;
    }
}

const char *tpf_mgr_ok(const tpf_t *p, bool mgr)
{
    if (p->state == TFN_POOL_OK || p->state == TFN_POOL_DAMAGED) {
        return mgr ? NULL : "auth";  // (a pool board: only its pool manager)
    }
    return p->legacy ? NULL : "auth";  // (no pool: the store token as before; a public build: no trust on first use)
}

// ---------------------------------------------------------------- link v4, the UDP tail, the beacons

// The packet path (the HOT lock is held): the end of the hour of the previous key
static void prev_hour(tpf_t *p)
{
    if (p->rx.prev_on && p->io->now_ms() >= p->prev_until) {
        p->rx.prev_on = false;
    }
}

// The next counter of a send (the HOT lock is held). The NVS gets the new block BEFORE the packet goes out.
static bool next_ctr(tpf_t *p, uint32_t *c)
{
    bool write = false;
    uint32_t lc = 0;
    if (!tfn_pool_ctr_next(&p->ctr, c, &write, &lc)) {
        return false;  // (the counter space of this key is used: the manager makes a new key)
    }
    if (write && !p->io->nvs_set_u32(NV_CTR, lc)) {
        p->ctr.open = false;  // (no new block in the NVS: no send; the next send tries again)
        p->ctr.lc -= TFN_POOL_CTR_BLOCK;
        return false;
    }
    return true;
}

int tpf_frame_seal(tpf_t *p, uint8_t *f, int len, int cap)
{
    int r = -1;
    uint32_t c;
    hot_lock(p);
    if (p->state == TFN_POOL_OK && p->rx.on && len >= TFN_POOL_V4_HDR && len + TFN_POOL_V4_TAG <= cap &&
        len + TFN_POOL_V4_TAG <= TFN_POOL_V4_MAX && next_ctr(p, &c)) {
        r = tfn_pool_v4_seal(&p->rx.key[0], p->me.mac, c, f, len, cap);
    }
    hot_unlock(p);
    return r;
}

int tpf_frame_open(tpf_t *p, const uint8_t mac[6], const uint8_t *f, int len)
{
    hot_lock(p);
    prev_hour(p);
    const int r = tfn_pool_v4_open(&p->rx, mac, f, len, NULL);
    hot_unlock(p);
    return r;
}

int tpf_udp_seal(tpf_t *p, uint8_t *b, int len, int cap)
{
    int r = -1;
    uint32_t c;
    hot_lock(p);
    if (p->state == TFN_POOL_OK && p->rx.on && len > 0 && len + TFN_POOL_UDP_TRAIL <= cap && next_ctr(p, &c)) {
        r = tfn_pool_udp2_seal(&p->rx.key[0], c, b, len, cap);
    }
    hot_unlock(p);
    return r;
}

int tpf_udp_open(tpf_t *p, const uint8_t mac[6], const uint8_t *b, int len)
{
    hot_lock(p);
    prev_hour(p);
    int r = tfn_pool_udp2_open(&p->rx, b, len, NULL);
    if (r == TFN_POOL_RX_OK && mac && len > 9 && memcmp(mac, b + 3, 6) != 0) {
        r = TFN_POOL_DROP_WHO;  // (the "from" field is not the MAC that the caller knows)
    }
    p->rx.count[r]++;
    hot_unlock(p);
    return r;
}

bool tpf_beacon_seal(tpf_t *p, const uint8_t *plain, int n, uint8_t *out, int *on)
{
    bool ok = false;
    tfn_pool_keys_t k;
    hot_lock(p);
    if (p->state == TFN_POOL_OK && p->rx.on && n == TFN_POOL_BP_LEN) {
        k = p->rx.key[0];
        ok = true;
    }
    hot_unlock(p);
    if (ok) {
        uint8_t nonce[12];
        p->io->random(nonce, sizeof nonce);
        ok = tfn_pool_ap_seal(p->io->ops, &k, nonce, plain, out);
    }
    wipe(&k, sizeof k);
    *on = ok ? TFN_POOL_BEACON_LEN : 0;
    return ok;
}

bool tpf_beacon_open(tpf_t *p, const uint8_t *pkt, int n, uint8_t *plain, int *pn)
{
    *pn = 0;
    if (n != TFN_POOL_BEACON_LEN || p->state != TFN_POOL_OK) {
        return false;
    }
    hot_lock(p);
    prev_hour(p);
    tfn_pool_rx_t *rx = &p->rx;  // (the keys only: a copy would cost 1 KB of the stack)
    const bool ok = tfn_pool_ap_open(p->io->ops, rx, pkt, n, plain) == TFN_POOL_RX_OK;
    hot_unlock(p);
    *pn = ok ? TFN_POOL_BP_LEN : 0;
    return ok;
}

int tpf_join_beacon(tpf_t *p, uint8_t *out, int cap)
{
    if (tpf_join_step(p) != TFN_POOL_JOIN_WAIT || p->j.token || cap < TFN_POOL_JBEACON_LEN || !p->me.ok) {
        return 0;
    }
    tfn_pool_jbeacon_make(p->me.mac, p->me.id, p->me.name, out);
    return TFN_POOL_JBEACON_LEN;
}

const char *tpf_role_name(int role)
{
    if (role == TFN_POOL_ROLE_MEMBER) {
        return "member";
    }
    if (role == TFN_POOL_ROLE_MANAGER) {
        return "manager";
    }
    return role == TFN_POOL_ROLE_CHILD ? "child" : "";
}

const char *tpf_step_name(tfn_pool_join_t s)
{
    static const char *const N[] = {"off", "wait", "reveal", "ask", "allowed", "done", "fail"};
    return (unsigned)s < sizeof N / sizeof N[0] ? N[s] : "?";
}

const char *tpf_join_web(tfn_pool_join_t s)
{
    if (s == TFN_POOL_JOIN_REVEAL) {
        return "hello";
    }
    if (s == TFN_POOL_JOIN_ASK) {
        return "wait";
    }
    return s == TFN_POOL_JOIN_ALLOWED ? "allowed" : "none";
}
