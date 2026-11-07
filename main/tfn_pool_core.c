// TFN OS pools: the core with no ESP-IDF call (tfn_pool_core.h). The service tfn_pool.c gives the crypto (PSA) and
// keeps the state; the host test tests/unit/test_pool_core.c runs this file as it is, with its own crypto.
#include "tfn_pool_core.h"
#include <stdio.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

// a compare with no early end (the time does not tell where two tags differ)
static bool same_ct(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d |= (uint8_t)(a[i] ^ b[i]);
    }
    return d == 0;
}

static bool all_zero(const uint8_t *p, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d |= p[i];
    }
    return d == 0;
}

const char *tfn_pool_err_text(int e)
{
    static const char *const T[] = {"ok",
                                    "removed",
                                    "file size",
                                    "no magic",
                                    "cut record",
                                    "unknown type",
                                    "bad length",
                                    "too many",
                                    "missing",
                                    "bad value",
                                    "after the signature",
                                    "duplicate",
                                    "not an admin key",
                                    "bad signature",
                                    "another pool",
                                    "old version",
                                    "not a member"};
    int i = e >= 0 ? e : 1 - e;
    return i < (int)(sizeof T / sizeof T[0]) ? T[i] : "?";
}

// ---------------------------------------------------------------- the roster parse

// a text of at most n bytes that ends at the first zero byte
static void text_of(const uint8_t *s, size_t n, char *out)
{
    size_t k = 0;
    while (k < n && s[k]) {
        k++;
    }
    memcpy(out, s, k);
    out[k] = 0;
}

static int rec_member(tfn_pool_roster_t *r, const uint8_t *v, size_t len)
{
    if (len != TFN_POOL_MEMBER_LEN) {
        return TFN_POOL_E_LEN;
    }
    if (r->nm >= TFN_POOL_MEMBERS) {
        return TFN_POOL_E_COUNT;
    }
    tfn_pool_member_t *m = &r->m[r->nm];
    memcpy(m->id, v, 8);
    memcpy(m->pk, v + 8, 65);
    memcpy(m->mac, v + 73, 6);
    m->role = v[79];
    m->flags = v[80];
    if (m->pk[0] != 0x04 || m->role > TFN_POOL_ROLE_CHILD) {
        return TFN_POOL_E_VALUE;
    }
    text_of(v + 81, TFN_POOL_MNAME_LEN, m->name);
    for (int i = 0; i < r->nm; i++) {
        if (!memcmp(r->m[i].id, m->id, 8) || !memcmp(r->m[i].mac, m->mac, 6)) {
            return TFN_POOL_E_DUP;
        }
    }
    r->nm++;
    return 0;
}

// the LAN URL, one zero byte, the VPN URL (each 0 .. 63 bytes)
static int rec_urls(tfn_pool_roster_t *r, const uint8_t *v, size_t len)
{
    const uint8_t *z = memchr(v, 0, len);
    if (!z || memchr(z + 1, 0, len - (size_t)(z + 1 - v)) || (size_t)(z - v) > TFN_POOL_URL_MAX ||
        len - (size_t)(z + 1 - v) > TFN_POOL_URL_MAX) {
        return TFN_POOL_E_VALUE;
    }
    text_of(v, (size_t)(z - v), r->lan);
    text_of(z + 1, len - (size_t)(z + 1 - v), r->vpn);
    return 0;
}

// the exact length of each fixed record (0 = not fixed)
static size_t rec_fixed(int t)
{
    switch (t) {
    case TFN_POOL_T_ID:
        return TFN_POOL_ID_LEN;
    case TFN_POOL_T_VER:
    case TFN_POOL_T_EPOCH:
    case TFN_POOL_T_FLAGS:
        return 4;
    case TFN_POOL_T_ADMIN:
        return TFN_POOL_PK_LEN;
    case TFN_POOL_T_TIME:
        return 8;
    case TFN_POOL_T_SIG:
        return TFN_POOL_SIG_LEN;
    default:
        return 0;
    }
}

// one record: its rules and its value into r
static int rec_one(tfn_pool_roster_t *r, int t, const uint8_t *v, size_t len, uint32_t *seen)
{
    if (t < TFN_POOL_T_ID || t > TFN_POOL_T_TIME) {
        return TFN_POOL_E_TYPE;
    }
    size_t fix = rec_fixed(t);
    if (fix && len != fix) {
        return TFN_POOL_E_LEN;
    }
    bool once = t != TFN_POOL_T_ADMIN && t != TFN_POOL_T_MEMBER;
    if (once && (*seen & (1u << t))) {
        return TFN_POOL_E_COUNT;
    }
    *seen |= 1u << t;
    switch (t) {
    case TFN_POOL_T_ID:
        memcpy(r->pool, v, len);
        return 0;
    case TFN_POOL_T_NAME:
        if (len < 1 || len > TFN_POOL_NAME_MAX) {
            return TFN_POOL_E_LEN;
        }
        text_of(v, len, r->name);
        return 0;
    case TFN_POOL_T_VER:
        r->ver = le32(v);
        return 0;
    case TFN_POOL_T_EPOCH:
        r->epoch = le32(v);
        return 0;
    case TFN_POOL_T_FLAGS:
        r->flags = le32(v);
        return 0;
    case TFN_POOL_T_ADMIN:
        if (r->nadmin >= TFN_POOL_ADMINS) {
            return TFN_POOL_E_COUNT;
        }
        if (v[0] != 0x04) {
            return TFN_POOL_E_VALUE;
        }
        if (r->nadmin && !memcmp(r->admin[0], v, TFN_POOL_PK_LEN)) {
            return TFN_POOL_E_DUP;
        }
        memcpy(r->admin[r->nadmin++], v, TFN_POOL_PK_LEN);
        return 0;
    case TFN_POOL_T_MEMBER:
        return rec_member(r, v, len);
    case TFN_POOL_T_URLS:
        return rec_urls(r, v, len);
    case TFN_POOL_T_TIME:
        r->time = le64(v);
        return 0;
    default:
        return TFN_POOL_E_TYPE;
    }
}

int tfn_pool_roster_parse(const uint8_t *f, size_t n, tfn_pool_roster_t *r)
{
    memset(r, 0, sizeof *r);
    if (!f || n < TFN_POOL_MAGIC_LEN + 3 || n > TFN_POOL_FILE_MAX) {
        return TFN_POOL_E_SIZE;
    }
    if (memcmp(f, TFN_POOL_MAGIC, TFN_POOL_MAGIC_LEN) != 0) {
        return TFN_POOL_E_MAGIC;
    }
    uint32_t seen = 0;
    size_t p = TFN_POOL_MAGIC_LEN;
    while (p < n) {
        if (n - p < 3) {
            return TFN_POOL_E_CUT;
        }
        int t = f[p];
        size_t len = (size_t)f[p + 1] | (size_t)f[p + 2] << 8;
        if (len > n - p - 3) {
            return TFN_POOL_E_CUT;
        }
        const uint8_t *v = f + p + 3;
        if (t == TFN_POOL_T_SIG) {
            if (len != TFN_POOL_SIG_LEN) {
                return TFN_POOL_E_LEN;
            }
            if (p + 3 + len != n) {
                return TFN_POOL_E_AFTER;
            }
            memcpy(r->kid, v, TFN_POOL_KID_LEN);
            memcpy(r->sig, v + TFN_POOL_KID_LEN, 64);
            r->signed_len = p;
            seen |= 1u << 31;
            break;
        }
        if (t < 0x80) {
            int e = rec_one(r, t, v, len, &seen);
            if (e) {
                return e;
            }
        }
        p += 3 + len;
    }
    const uint32_t need = 1u << TFN_POOL_T_ID | 1u << TFN_POOL_T_NAME | 1u << TFN_POOL_T_VER | 1u << TFN_POOL_T_EPOCH |
                          1u << TFN_POOL_T_FLAGS | 1u << TFN_POOL_T_ADMIN | 1u << TFN_POOL_T_MEMBER |
                          1u << TFN_POOL_T_TIME | 1u << 31;
    return (seen & need) == need ? 0 : TFN_POOL_E_MISSING;
}

int tfn_pool_roster_slot(const tfn_pool_roster_t *r, const uint8_t mac[TFN_POOL_MAC_LEN])
{
    for (int i = 0; r && mac && i < r->nm; i++) {
        if (!memcmp(r->m[i].mac, mac, TFN_POOL_MAC_LEN)) {
            return i;
        }
    }
    return -1;
}

int tfn_pool_roster_find(const tfn_pool_roster_t *r, const uint8_t id[TFN_POOL_DEV_LEN])
{
    for (int i = 0; r && id && i < r->nm; i++) {
        if (!memcmp(r->m[i].id, id, TFN_POOL_DEV_LEN)) {
            return i;
        }
    }
    return -1;
}

void tfn_pool_key_id(const tfn_pool_ops_t *o, const uint8_t pk[TFN_POOL_PK_LEN], uint8_t kid[TFN_POOL_KID_LEN])
{
    uint8_t h[32];
    o->sha256(pk, TFN_POOL_PK_LEN, h);
    memcpy(kid, h, TFN_POOL_KID_LEN);
}

void tfn_pool_dev_id(const tfn_pool_ops_t *o, const uint8_t pk[TFN_POOL_PK_LEN], uint8_t id[TFN_POOL_DEV_LEN])
{
    uint8_t m[6 + TFN_POOL_PK_LEN], h[32];
    memcpy(m, "TFNID1", 6);
    memcpy(m + 6, pk, TFN_POOL_PK_LEN);
    o->sha256(m, sizeof m, h);
    memcpy(id, h, TFN_POOL_DEV_LEN);
}

// the admin key of the list with the key ID kid, or -1
static int admin_of(const tfn_pool_ops_t *o, const uint8_t (*keys)[TFN_POOL_PK_LEN], int n, const uint8_t *kid)
{
    for (int i = 0; keys && i < n; i++) {
        uint8_t k[TFN_POOL_KID_LEN];
        tfn_pool_key_id(o, keys[i], k);
        if (!memcmp(k, kid, TFN_POOL_KID_LEN)) {
            return i;
        }
    }
    return -1;
}

int tfn_pool_roster_check(const tfn_pool_ops_t *o, const tfn_pool_trust_t *t, const uint8_t *f, size_t n,
                          tfn_pool_roster_t *r)
{
    int e = tfn_pool_roster_parse(f, n, r);
    if (e) {
        return e;
    }
    // the signature: the key ID names an admin key of the NEW roster, and the signature verifies with it
    int a = admin_of(o, (const uint8_t (*)[TFN_POOL_PK_LEN])r->admin, r->nadmin, r->kid);
    if (a < 0 || !o->p256_verify(r->admin[a], f, r->signed_len, r->sig)) {
        return TFN_POOL_E_SIG;
    }
    bool trusted = t && admin_of(o, t->admin, t->nadmin, r->kid) >= 0;
    if (t && t->join) {
        if (!trusted) {
            return TFN_POOL_E_ADMIN;  // not the admin key of the JOIN
        }
        return tfn_pool_roster_find(r, t->self) >= 0 ? TFN_POOL_R_OK : TFN_POOL_E_SELF;
    }
    if (!t || !t->pool || memcmp(t->pool, r->pool, TFN_POOL_ID_LEN) != 0) {
        return TFN_POOL_E_POOL;
    }
    if (!trusted) {
        return TFN_POOL_E_ADMIN;  // not an admin key of the stored roster
    }
    if (r->ver <= t->floor) {
        return TFN_POOL_E_OLD;
    }
    return tfn_pool_roster_find(r, t->self) >= 0 ? TFN_POOL_R_OK : TFN_POOL_R_REMOVED;
}

// ---------------------------------------------------------------- HKDF and the keys

bool tfn_pool_hkdf(const tfn_pool_ops_t *o, const uint8_t *salt, size_t sn, const uint8_t *ikm, size_t in,
                   const uint8_t *info, size_t fn, uint8_t *out, size_t on)
{
    static const uint8_t ZERO[32];
    uint8_t prk[32], t[32];
    if (on > 255 * 32) {
        return false;
    }
    const uint8_t *p1[1] = {ikm};
    size_t n1[1] = {in};
    o->hmac(sn ? salt : ZERO, sn ? sn : sizeof ZERO, p1, n1, 1, prk);
    size_t done = 0;
    for (uint8_t i = 1; done < on; i++) {
        uint8_t c = i;
        const uint8_t *p[3] = {t, info, &c};
        size_t pn[3] = {i > 1 ? sizeof t : 0, fn, 1};
        o->hmac(prk, sizeof prk, p, pn, 3, t);
        size_t k = on - done < 32 ? on - done : 32;
        memcpy(out + done, t, k);
        done += k;
    }
    return true;
}

static void derive_one(const tfn_pool_ops_t *o, const uint8_t *key, const uint8_t *pool, uint32_t epoch,
                       const char *text, uint8_t *out, size_t on)
{
    uint8_t info[32];
    size_t tn = strlen(text);
    memcpy(info, text, tn);
    put32(info + tn, epoch);
    tfn_pool_hkdf(o, pool, TFN_POOL_ID_LEN, key, TFN_POOL_KEY_LEN, info, tn + 4, out, on);
}

void tfn_pool_derive(const tfn_pool_ops_t *o, const uint8_t key[TFN_POOL_KEY_LEN], const uint8_t pool[TFN_POOL_ID_LEN],
                     uint32_t epoch, tfn_pool_keys_t *k)
{
    memset(k, 0, sizeof *k);
    k->epoch = epoch;
    derive_one(o, key, pool, epoch, "TFN link4", k->link, sizeof k->link);
    derive_one(o, key, pool, epoch, "TFN ptag", &k->ptag, 1);
    derive_one(o, key, pool, epoch, "TFN beacon1", k->beacon, sizeof k->beacon);
    derive_one(o, key, pool, epoch, "TFN hint", k->hint, sizeof k->hint);
}

// ---------------------------------------------------------------- SipHash-2-4

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

static void sip_round(uint64_t v[4])
{
    v[0] += v[1];
    v[1] = ROTL(v[1], 13);
    v[1] ^= v[0];
    v[0] = ROTL(v[0], 32);
    v[2] += v[3];
    v[3] = ROTL(v[3], 16);
    v[3] ^= v[2];
    v[0] += v[3];
    v[3] = ROTL(v[3], 21);
    v[3] ^= v[0];
    v[2] += v[1];
    v[1] = ROTL(v[1], 17);
    v[1] ^= v[2];
    v[2] = ROTL(v[2], 32);
}

static void sip_word(uint64_t v[4], uint64_t m)
{
    v[3] ^= m;
    sip_round(v);
    sip_round(v);
    v[0] ^= m;
}

uint64_t tfn_pool_siphash(const uint8_t key[16], const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
    uint64_t k0 = le64(key), k1 = le64(key + 8);
    uint64_t v[4] = {k0 ^ 0x736f6d6570736575ULL, k1 ^ 0x646f72616e646f6dULL, k0 ^ 0x6c7967656e657261ULL,
                     k1 ^ 0x7465646279746573ULL};
    size_t tot = an + bn;
    uint64_t m = 0;
    int mb = 0;
    for (size_t i = 0; i < tot; i++) {
        uint8_t c = i < an ? a[i] : b[i - an];
        m |= (uint64_t)c << (8 * mb);
        if (++mb == 8) {
            sip_word(v, m);
            m = 0;
            mb = 0;
        }
    }
    sip_word(v, m | (uint64_t)(tot & 0xFF) << 56);
    v[2] ^= 0xFF;
    for (int i = 0; i < 4; i++) {
        sip_round(v);
    }
    return v[0] ^ v[1] ^ v[2] ^ v[3];
}

// ---------------------------------------------------------------- link v4 and the UDP v2 packets

// the tag of the bytes [0, n) from the sender mac
static void tag_of(const uint8_t link[16], const uint8_t *mac, const uint8_t *p, size_t n, uint8_t out[8])
{
    put64(out, tfn_pool_siphash(link, mac, TFN_POOL_MAC_LEN, p, n));
}

int tfn_pool_v4_seal(const tfn_pool_keys_t *k, const uint8_t mac[TFN_POOL_MAC_LEN], uint32_t ctr, uint8_t *f, int len,
                     int cap)
{
    if (!k || !mac || !f || len < TFN_POOL_V4_HDR || len - TFN_POOL_V4_HDR > TFN_POOL_V4_DATA_MAX ||
        cap < len + TFN_POOL_V4_TAG || f[0] != 'A' || f[1] != 'L' || f[2] != TFN_POOL_V4_VERSION ||
        f[5] != len - TFN_POOL_V4_HDR) {
        return -1;
    }
    f[7] = k->ptag;
    put32(f + 8, ctr);
    tag_of(k->link, mac, f, (size_t)len, f + len);
    return len + TFN_POOL_V4_TAG;
}

// the body length of a UDP v2 packet ('A' 'C' 2 or 'A' 'I' 2), or 0
static int udp_body(const uint8_t *p, int n)
{
    if (!p || n < 3 || p[0] != 'A' || p[2] != 2) {
        return 0;
    }
    return p[1] == 'C' ? TFN_POOL_UDP_CALL : p[1] == 'I' ? TFN_POOL_UDP_INVITE : 0;
}

int tfn_pool_udp2_seal(const tfn_pool_keys_t *k, uint32_t ctr, uint8_t *p, int len, int cap)
{
    if (!k || udp_body(p, len) != len || cap < len + TFN_POOL_UDP_TRAIL) {
        return -1;
    }
    p[len] = k->ptag;
    put32(p + len + 1, ctr);
    tag_of(k->link, p + 3, p, (size_t)len + 5, p + len + 5);
    return len + TFN_POOL_UDP_TRAIL;
}

static void rx_set_macs(tfn_pool_rx_t *rx, const tfn_pool_roster_t *r)
{
    rx->n = r ? r->nm : 0;
    for (int i = 0; i < rx->n; i++) {
        memcpy(rx->mac[i], r->m[i].mac, TFN_POOL_MAC_LEN);
        rx->role[i] = r->m[i].role;
    }
}

void tfn_pool_rx_init(tfn_pool_rx_t *rx, const tfn_pool_roster_t *r, const tfn_pool_keys_t *cur,
                      const tfn_pool_keys_t *prev)
{
    memset(rx, 0, sizeof *rx);
    rx_set_macs(rx, r);
    rx->key[0] = *cur;
    if (prev) {
        rx->key[1] = *prev;
        rx->prev_on = true;
    }
    rx->on = true;
}

void tfn_pool_rx_roster(tfn_pool_rx_t *rx, const tfn_pool_roster_t *r)
{
    tfn_pool_win_t w[2][TFN_POOL_MEMBERS];
    memset(w, 0, sizeof w);
    for (int i = 0; r && i < r->nm; i++) {
        int s = tfn_pool_rx_slot(rx, r->m[i].mac);
        if (s >= 0) {
            w[0][i] = rx->win[0][s];
            w[1][i] = rx->win[1][s];
        }
    }
    memcpy(rx->win, w, sizeof w);
    memset(rx->mac, 0, sizeof rx->mac);
    rx_set_macs(rx, r);
}

void tfn_pool_rx_rekey(tfn_pool_rx_t *rx, const tfn_pool_keys_t *k)
{
    if (!memcmp(&rx->key[0], k, sizeof *k)) {
        return;
    }
    rx->key[1] = rx->key[0];
    memcpy(rx->win[1], rx->win[0], sizeof rx->win[0]);
    memset(rx->win[0], 0, sizeof rx->win[0]);
    rx->key[0] = *k;
    rx->prev_on = true;
}

int tfn_pool_rx_slot(const tfn_pool_rx_t *rx, const uint8_t mac[TFN_POOL_MAC_LEN])
{
    for (int i = 0; mac && i < rx->n; i++) {
        if (!memcmp(rx->mac[i], mac, TFN_POOL_MAC_LEN)) {
            return i;
        }
    }
    return -1;
}

bool tfn_pool_win_take(tfn_pool_win_t *w, uint32_t ctr)
{
    if (!w->on || ctr > w->hi) {
        uint32_t d = w->on ? ctr - w->hi : 64;
        w->seen = d >= 64 ? 1 : (w->seen << d | 1);
        w->hi = ctr;
        w->on = true;
        return true;
    }
    uint32_t d = w->hi - ctr;
    if (d >= 64 || (w->seen >> d & 1)) {
        return false;
    }
    w->seen |= (uint64_t)1 << d;
    return true;
}

// the common part of the v4 and UDP checks: the pool tag at pt, the counter at pc, the tag at the end
static int sealed_check(tfn_pool_rx_t *rx, const uint8_t *mac, const uint8_t *p, int n, int pt, int pc, int *slot)
{
    bool c0 = rx->key[0].ptag == p[pt];
    bool c1 = rx->prev_on && rx->key[1].ptag == p[pt];
    int s = tfn_pool_rx_slot(rx, mac);
    if (s < 0) {
        return TFN_POOL_DROP_WHO;
    }
    int k = -1;
    for (int i = 0; i < 2 && k < 0; i++) {
        uint8_t t[8];
        if (i ? c1 : c0) {
            tag_of(rx->key[i].link, mac, p, (size_t)n - 8, t);
            k = same_ct(t, p + n - 8, 8) ? i : -1;
        }
    }
    if (k < 0) {
        return TFN_POOL_DROP_BAD;
    }
    if (!tfn_pool_win_take(&rx->win[k][s], le32(p + pc))) {
        return TFN_POOL_DROP_OLD;
    }
    if (slot) {
        *slot = s;
    }
    return TFN_POOL_RX_OK;
}

static int v4_check(tfn_pool_rx_t *rx, const uint8_t *mac, const uint8_t *f, int n, int *slot)
{
    if (!rx->on) {
        return TFN_POOL_DROP_OTHER;
    }
    if (!f || !mac || n < 8 || f[0] != 'A' || f[1] != 'L' || f[2] != TFN_POOL_V4_VERSION) {
        return TFN_POOL_DROP_BAD;
    }
    if (rx->key[0].ptag != f[7] && !(rx->prev_on && rx->key[1].ptag == f[7])) {
        return TFN_POOL_DROP_OTHER;
    }
    if (f[5] > TFN_POOL_V4_DATA_MAX || n != TFN_POOL_V4_HDR + TFN_POOL_V4_TAG + f[5]) {
        return TFN_POOL_DROP_BAD;
    }
    return sealed_check(rx, mac, f, n, 7, 8, slot);
}

int tfn_pool_v4_open(tfn_pool_rx_t *rx, const uint8_t mac[TFN_POOL_MAC_LEN], const uint8_t *f, int n, int *slot)
{
    int r = v4_check(rx, mac, f, n, slot);
    rx->count[r]++;
    return r;
}

int tfn_pool_udp2_open(tfn_pool_rx_t *rx, const uint8_t *p, int n, int *slot)
{
    if (!rx->on) {
        return TFN_POOL_DROP_OTHER;
    }
    int b = udp_body(p, n);
    if (!b || n < b + 1) {
        return TFN_POOL_DROP_BAD;
    }
    if (rx->key[0].ptag != p[b] && !(rx->prev_on && rx->key[1].ptag == p[b])) {
        return TFN_POOL_DROP_OTHER;
    }
    if (n != b + TFN_POOL_UDP_TRAIL) {
        return TFN_POOL_DROP_BAD;
    }
    return sealed_check(rx, p + 3, p, n, b, b + 1, slot);
}

// ---------------------------------------------------------------- the send counter

void tfn_pool_ctr_init(tfn_pool_ctr_t *c, uint32_t lc)
{
    memset(c, 0, sizeof *c);
    c->lc = lc;
}

bool tfn_pool_ctr_next(tfn_pool_ctr_t *c, uint32_t *ctr, bool *write, uint32_t *lc)
{
    *write = false;
    if (!c->open || c->next == c->end) {
        uint32_t start = c->open ? c->end : c->lc;
        if (start > UINT32_MAX - TFN_POOL_CTR_BLOCK) {
            return false;
        }
        c->next = start;
        c->end = start + TFN_POOL_CTR_BLOCK;
        c->lc = c->end;
        c->open = true;
        *write = true;
        *lc = c->lc;
    }
    *ctr = c->next++;
    return true;
}

// ---------------------------------------------------------------- the beacons

bool tfn_pool_ap_seal(const tfn_pool_ops_t *o, const tfn_pool_keys_t *k, const uint8_t nonce[12],
                      const uint8_t plain[TFN_POOL_BP_LEN], uint8_t out[TFN_POOL_BEACON_LEN])
{
    out[0] = 'A';
    out[1] = 'P';
    out[2] = 1;
    memcpy(out + 3, k->hint, 4);
    memcpy(out + 7, nonce, TFN_POOL_NONCE_LEN);
    return o->aead_seal(k->beacon, nonce, out, 7, plain, TFN_POOL_BP_LEN, out + 19, out + 19 + TFN_POOL_BP_LEN);
}

int tfn_pool_ap_open(const tfn_pool_ops_t *o, const tfn_pool_rx_t *rx, const uint8_t *p, int n,
                     uint8_t plain[TFN_POOL_BP_LEN])
{
    if (!rx || !rx->on) {
        return TFN_POOL_DROP_OTHER;
    }
    if (!p || n != TFN_POOL_BEACON_LEN || p[0] != 'A' || p[1] != 'P' || p[2] != 1) {
        return TFN_POOL_DROP_BAD;
    }
    bool hit = false;
    for (int i = 0; i < (rx->prev_on ? 2 : 1); i++) {
        const tfn_pool_keys_t *k = &rx->key[i];
        if (memcmp(p + 3, k->hint, 4) != 0) {
            continue;
        }
        hit = true;
        if (o->aead_open(k->beacon, p + 7, p, 7, p + 19, TFN_POOL_BP_LEN, p + 19 + TFN_POOL_BP_LEN, plain)) {
            return TFN_POOL_RX_OK;
        }
    }
    return hit ? TFN_POOL_DROP_BAD : TFN_POOL_DROP_OTHER;
}

void tfn_pool_jbeacon_make(const uint8_t mac[6], const uint8_t id[8], const char *name,
                           uint8_t out[TFN_POOL_JBEACON_LEN])
{
    memset(out, 0, TFN_POOL_JBEACON_LEN);
    out[0] = 'A';
    out[1] = 'J';
    out[2] = 1;
    memcpy(out + 3, mac, 6);
    memcpy(out + 9, id, 8);
    size_t n = name ? strlen(name) : 0;
    if (n > TFN_POOL_MNAME_LEN) {
        n = TFN_POOL_MNAME_LEN;
        while (n && ((uint8_t)name[n] & 0xC0) == 0x80) {
            n--;  // do not cut a UTF-8 letter
        }
    }
    if (n) {
        memcpy(out + 17, name, n);
    }
}

bool tfn_pool_jbeacon_parse(const uint8_t *p, int n, uint8_t mac[6], uint8_t id[8], char name[17])
{
    if (!p || n != TFN_POOL_JBEACON_LEN || p[0] != 'A' || p[1] != 'J' || p[2] != 1) {
        return false;
    }
    memcpy(mac, p + 3, 6);
    memcpy(id, p + 9, 8);
    text_of(p + 17, TFN_POOL_MNAME_LEN, name);
    return true;
}

// ---------------------------------------------------------------- the JOIN and the envelope

void tfn_pool_commit(const tfn_pool_ops_t *o, const uint8_t em[32], const uint8_t nm[16], uint8_t c[32])
{
    uint8_t m[6 + 32 + 16];
    memcpy(m, "TFNJC1", 6);
    memcpy(m + 6, em, 32);
    memcpy(m + 38, nm, 16);
    o->sha256(m, sizeof m, c);
}

uint32_t tfn_pool_sas(const tfn_pool_ops_t *o, const uint8_t em[32], const uint8_t eb[32], const uint8_t nm[16],
                      const uint8_t nb[16])
{
    uint8_t m[7 + 32 + 32 + 16 + 16], h[32];
    memcpy(m, "TFNSAS1", 7);
    memcpy(m + 7, em, 32);
    memcpy(m + 39, eb, 32);
    memcpy(m + 71, nm, 16);
    memcpy(m + 87, nb, 16);
    o->sha256(m, sizeof m, h);
    return le32(h) % 1000000u;
}

void tfn_pool_sas_text(uint32_t sas, char out[8])
{
    snprintf(out, 8, "%03u %03u", (unsigned)(sas / 1000 % 1000), (unsigned)(sas % 1000));
}

int32_t tfn_pool_sas_parse(const char *s)
{
    int32_t v = 0;
    int nd = 0;
    bool sp = false;
    for (int i = 0; s && s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            if (++nd > 6) {
                return -1;
            }
            v = v * 10 + (s[i] - '0');
        } else if (s[i] == ' ' && nd == 3 && !sp) {
            sp = true;  // one space after the first 3 digits
        } else {
            return -1;
        }
    }
    return nd == 6 ? v : -1;
}

int tfn_pool_join_msg(const uint8_t c[32], const uint8_t eb[32], const uint8_t nb[16], const uint8_t *em,
                      const uint8_t *nm, uint8_t out[TFN_POOL_JMSG_MAX])
{
    memcpy(out, "TFNJ1", 5);
    memcpy(out + 5, c, 32);
    memcpy(out + 37, eb, 32);
    memcpy(out + 69, nb, 16);
    if (!em || !nm) {
        return 85;
    }
    memcpy(out + 85, em, 32);
    memcpy(out + 117, nm, 16);
    return TFN_POOL_JMSG_MAX;
}

void tfn_pool_box_msg(const uint8_t id[8], const uint8_t bx[32], uint8_t out[TFN_POOL_BXMSG_LEN])
{
    memcpy(out, "TFNBX1", 6);
    memcpy(out + 6, id, 8);
    memcpy(out + 14, bx, 32);
}

bool tfn_pool_seal_key(const tfn_pool_ops_t *o, const uint8_t priv[32], const uint8_t peer[32], const uint8_t *salt,
                       size_t sn, uint8_t key[32])
{
    uint8_t z[32];
    if (!o->x25519(priv, peer, z) || all_zero(z, sizeof z)) {
        memset(z, 0, sizeof z);
        return false;
    }
    bool ok = tfn_pool_hkdf(o, salt, sn, z, sizeof z, (const uint8_t *)"TFNSEAL1", 8, key, 32);
    memset(z, 0, sizeof z);
    return ok;
}

bool tfn_pool_env_seal(const tfn_pool_ops_t *o, const uint8_t key[32], const uint8_t nonce[12], const uint8_t *roster,
                       size_t rn, uint32_t epoch, const uint8_t pkey[32], const uint8_t wk[32],
                       uint8_t out[TFN_POOL_ENV_LEN])
{
    uint8_t aad[32], pt[68];
    o->sha256(roster, rn, aad);
    put32(pt, epoch);
    memcpy(pt + 4, pkey, 32);
    memcpy(pt + 36, wk, 32);
    memcpy(out, nonce, 12);
    bool ok = o->aead_seal(key, nonce, aad, sizeof aad, pt, sizeof pt, out + 12, out + 80);
    memset(pt, 0, sizeof pt);
    return ok;
}

bool tfn_pool_env_open(const tfn_pool_ops_t *o, const uint8_t key[32], const uint8_t *roster, size_t rn,
                       const uint8_t *env, size_t en, uint32_t *epoch, uint8_t pkey[32], uint8_t wk[32])
{
    uint8_t aad[32], pt[68];
    if (!env || en != TFN_POOL_ENV_LEN || !roster) {
        return false;
    }
    o->sha256(roster, rn, aad);
    if (!o->aead_open(key, env, aad, sizeof aad, env + 12, sizeof pt, env + 80, pt)) {
        memset(pt, 0, sizeof pt);
        return false;
    }
    *epoch = le32(pt);
    memcpy(pkey, pt + 4, 32);
    memcpy(wk, pt + 36, 32);
    memset(pt, 0, sizeof pt);
    return true;
}

// ---------------------------------------------------------------- the X-TFN-Auth header

static int hexv(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;  // lower case only (the form of the manager)
}

// exactly 2 * nb hex digits at s, then the letter end
static const char *hex_take(const char *s, uint8_t *out, size_t nb, char end)
{
    for (size_t i = 0; i < nb; i++) {
        int a = hexv(s[2 * i]);
        int b = a < 0 ? -1 : hexv(s[2 * i + 1]);
        if (b < 0) {
            return NULL;
        }
        out[i] = (uint8_t)(a << 4 | b);
    }
    return s[2 * nb] == end ? s + 2 * nb : NULL;
}

bool tfn_pool_auth_parse(const char *h, uint8_t id[8], uint64_t *ctr, uint8_t mac[32])
{
    const char *p = h ? hex_take(h, id, 8, '.') : NULL;
    if (!p) {
        return false;
    }
    p++;
    uint64_t v = 0;
    int nd = 0;
    for (; *p >= '0' && *p <= '9'; p++, nd++) {
        unsigned d = (unsigned)(*p - '0');
        if (nd >= 20 || (nd == 1 && v == 0) || v > (UINT64_MAX - d) / 10) {
            return false;  // too long, a leading zero, or over 64 bits
        }
        v = v * 10 + d;
    }
    if (!nd || *p != '.' || !hex_take(p + 1, mac, 32, 0)) {
        return false;
    }
    *ctr = v;
    return true;
}

void tfn_pool_auth_mac(const tfn_pool_ops_t *o, const uint8_t wk[32], const uint8_t bn[16], uint64_t ctr,
                       const char *method, const char *uri, uint8_t out[32])
{
    uint8_t c[8];
    put64(c, ctr);
    const uint8_t *p[6] = {(const uint8_t *)"TFNWEB1", bn, c, (const uint8_t *)method, (const uint8_t *)" ",
                           (const uint8_t *)uri};
    size_t pn[6] = {7, TFN_POOL_BN_LEN, 8, strlen(method), 1, strlen(uri)};
    o->hmac(wk, 32, p, pn, 6, out);
}

int tfn_pool_auth_make(const tfn_pool_ops_t *o, const uint8_t id[8], const uint8_t wk[32], const uint8_t bn[16],
                       uint64_t ctr, const char *method, const char *uri, char *out, int cap)
{
    static const char HX[] = "0123456789abcdef";
    uint8_t mac[32];
    char t[TFN_POOL_AUTH_MAX];
    int k = 0;
    tfn_pool_auth_mac(o, wk, bn, ctr, method, uri, mac);
    for (int i = 0; i < 8; i++) {
        t[k++] = HX[id[i] >> 4];
        t[k++] = HX[id[i] & 15];
    }
    k += snprintf(t + k, sizeof t - (size_t)k, ".%llu.", (unsigned long long)ctr);
    for (int i = 0; i < 32; i++) {
        t[k++] = HX[mac[i] >> 4];
        t[k++] = HX[mac[i] & 15];
    }
    t[k] = 0;
    if (k >= cap) {
        return -1;
    }
    memcpy(out, t, (size_t)k + 1);
    return k;
}

bool tfn_pool_auth_check(const tfn_pool_ops_t *o, const tfn_pool_roster_t *r, const uint8_t wk[32],
                         const uint8_t bn[16], const char *method, const char *uri, const char *h, uint64_t *last)
{
    uint8_t id[8], mac[32], want[32];
    uint64_t ctr;
    if (!r || !method || !uri || !tfn_pool_auth_parse(h, id, &ctr, mac)) {
        return false;
    }
    int s = tfn_pool_roster_find(r, id);
    if (s < 0 || r->m[s].role != TFN_POOL_ROLE_MANAGER || ctr <= *last) {
        return false;
    }
    tfn_pool_auth_mac(o, wk, bn, ctr, method, uri, want);
    if (!same_ct(want, mac, sizeof mac)) {
        return false;
    }
    *last = ctr;
    return true;
}

// ---------------------------------------------------------------- the receive table and the legacy peers

bool tfn_pool_take(int in, const tfn_pool_peer_t *p)
{
    // in a pool, STRICT drops a forged member and a keyed friend of another pool; the rest as today
    bool v3 = !p->pool || !p->strict || (!p->member && !p->keyed_other);
    switch (in) {
    case TFN_POOL_IN_V3:
        return v3;
    case TFN_POOL_IN_PEER:
        return !p->pool || !p->strict || p->member || p->is_friend;
    case TFN_POOL_IN_WAKE1:
        return p->is_friend && v3;
    case TFN_POOL_IN_WAKE2:
        return p->pool && p->member && p->manager;
    default:
        return false;
    }
}

void tfn_pool_legacy_note(tfn_pool_legacy_t *l, uint8_t app, uint32_t now_ms)
{
    int pick = -1;
    for (int i = 0; i < TFN_POOL_LEGACY_SLOTS && pick < 0; i++) {
        if (l->used[i] && l->app[i] == app) {
            pick = i;  // the slot of this app
        }
    }
    for (int i = 0; i < TFN_POOL_LEGACY_SLOTS && pick < 0; i++) {
        if (!l->used[i]) {
            pick = i;  // else a free slot
        }
    }
    if (pick < 0) {
        pick = 0;  // else the oldest slot
        for (int i = 1; i < TFN_POOL_LEGACY_SLOTS; i++) {
            if (now_ms - l->ms[i] > now_ms - l->ms[pick]) {
                pick = i;
            }
        }
    }
    l->used[pick] = true;
    l->app[pick] = app;
    l->ms[pick] = now_ms;
}

bool tfn_pool_legacy_on(const tfn_pool_legacy_t *l, uint8_t app, uint32_t now_ms)
{
    for (int i = 0; i < TFN_POOL_LEGACY_SLOTS; i++) {
        if (l->used[i] && l->app[i] == app && now_ms - l->ms[i] <= TFN_POOL_LEGACY_MS) {
            return true;
        }
    }
    return false;
}
