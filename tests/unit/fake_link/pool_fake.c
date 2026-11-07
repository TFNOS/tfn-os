// The link test: a fake pool service (pool_fake.h) with the API of tfn_pool.h on the pool core
#include "tfn_pool.h"
#include <string.h>
#include "fake_board.h"
#include "pool_fake.h"

typedef struct {
    bool used, on, strict, plain, join;
    uint8_t mac[6];
    tfn_pool_keys_t keys;
    tfn_pool_rx_t rx;
    uint32_t ctr;
    uint32_t udp[TFN_POOL_CNT_N];  // the counts of the UDP checks
    uint8_t bplain[43], bpkt[78];
    uint8_t jmac[6], jid[8];
    char jname[17];
} pf_board_t;
static pf_board_t s_b[PF_BOARDS];
static tfn_pool_roster_t s_roster;
static struct {
    uint8_t pkt[78], plain[43];
} s_bin[4];
static int s_nbin;

void pf_reset(void)
{
    memset(s_b, 0, sizeof s_b);
    memset(&s_roster, 0, sizeof s_roster);
    s_nbin = 0;
}

static void rx_load(pf_board_t *b)
{
    if (b->on) {
        tfn_pool_rx_init(&b->rx, &s_roster, &b->keys, NULL);
    } else {
        memset(&b->rx, 0, sizeof b->rx);
    }
}

void pf_board(int i, const uint8_t mac[6], const pf_keys_t *k, bool strict, bool plain)
{
    pf_board_t *b = &s_b[i];
    b->used = true;
    memcpy(b->mac, mac, 6);
    b->on = k != NULL;
    b->strict = strict;
    b->plain = plain;
    memset(&b->keys, 0, sizeof b->keys);
    if (k) {
        b->keys.epoch = 2;
        memcpy(b->keys.link, k->link, 16);
        b->keys.ptag = k->ptag;
        memcpy(b->keys.beacon, k->beacon, 32);
        memcpy(b->keys.hint, k->hint, 4);
    }
    rx_load(b);
}

void pf_roster_add(const uint8_t mac[6], int role, const uint8_t id[8], const char *name)
{
    tfn_pool_member_t *m = &s_roster.m[s_roster.nm++];
    memcpy(m->mac, mac, 6);
    memcpy(m->id, id, 8);
    m->role = (uint8_t)role;
    strncpy(m->name, name, sizeof m->name - 1);
    for (int i = 0; i < PF_BOARDS; i++) {
        if (s_b[i].used && s_b[i].on) {
            tfn_pool_rx_roster(&s_b[i].rx, &s_roster);
        }
    }
}

void pf_set_ctr(int b, uint32_t next)
{
    s_b[b].ctr = next;
}

void pf_counts(int b, uint32_t out[5])
{
    for (int i = 0; i < 5; i++) {
        out[i] = s_b[b].rx.count[i] + s_b[b].udp[i];
    }
}

void pf_beacon(int b, const uint8_t plain[43], const uint8_t pkt[78])
{
    memcpy(s_b[b].bplain, plain, 43);
    memcpy(s_b[b].bpkt, pkt, 78);
}

void pf_beacon_in(const uint8_t pkt[78], const uint8_t plain[43])
{
    memcpy(s_bin[s_nbin].pkt, pkt, 78);
    memcpy(s_bin[s_nbin].plain, plain, 43);
    s_nbin++;
}

void pf_join(int b, const uint8_t mac[6], const uint8_t id[8], const char *name)
{
    s_b[b].join = name != NULL;
    if (name) {
        memcpy(s_b[b].jmac, mac, 6);
        memcpy(s_b[b].jid, id, 8);
        strncpy(s_b[b].jname, name, 16);
    }
}

int pf_udp_seal(int b, uint8_t *p, int len, int cap)
{
    return tfn_pool_udp2_seal(&s_b[b].keys, s_b[b].ctr++, p, len, cap);
}

int pf_frame_seal(int b, uint8_t *f, int len, int cap)
{
    return tfn_pool_v4_seal(&s_b[b].keys, s_b[b].mac, s_b[b].ctr++, f, len, cap);
}

// the board of this call (fk_mac), or NULL
static pf_board_t *me(void)
{
    for (int i = 0; i < PF_BOARDS; i++) {
        if (s_b[i].used && !memcmp(s_b[i].mac, fk_mac, 6)) {
            return &s_b[i];
        }
    }
    return NULL;
}

static pf_board_t *pool(void)
{
    pf_board_t *b = me();
    return b && b->on ? b : NULL;
}

// ---------------------------------------------------------------- the API of tfn_pool.h
void tfn_pool_init(void)
{
}

void tfn_pool_register(void *server)
{
    (void)server;
}

void con_pool(char *cmd)
{
    (void)cmd;
}

bool tfn_pool_on(void)
{
    return pool() != NULL;
}

bool tfn_pool_strict(void)
{
    pf_board_t *b = pool();
    return b && b->strict;
}

bool tfn_pool_legacy(void)
{
    return true;  // (a family build)
}

tfn_pool_state_t tfn_pool_state(void)
{
    return pool() ? TFN_POOL_OK : TFN_POOL_NONE;
}

uint8_t tfn_pool_ptag(void)
{
    pf_board_t *b = pool();
    return b ? b->keys.ptag : 0;
}

const char *tfn_pool_name(void)
{
    return pool() ? "TEST" : "";
}

int tfn_pool_my_role(void)
{
    return pool() ? tfn_pool_role(tfn_pool_member(fk_mac)) : -1;
}

uint32_t tfn_pool_gen(void)
{
    return 0;
}

bool tfn_pool_plain_beacon(void)
{
    pf_board_t *b = pool();
    return b ? b->plain : tfn_pool_legacy();
}

int tfn_pool_member(const uint8_t mac[6])
{
    return pool() ? tfn_pool_roster_slot(&s_roster, mac) : -1;
}

int tfn_pool_role(int slot)
{
    return pool() && slot >= 0 && slot < s_roster.nm ? s_roster.m[slot].role : -1;
}

int tfn_pool_members(void)
{
    return pool() ? s_roster.nm : 0;
}

bool tfn_pool_member_get(int slot, tfn_pool_member_t *out)
{
    memset(out, 0, sizeof *out);
    if (!pool() || slot < 0 || slot >= s_roster.nm) {
        return false;
    }
    memcpy(out->id, s_roster.m[slot].id, 8);
    memcpy(out->mac, s_roster.m[slot].mac, 6);
    out->role = s_roster.m[slot].role;
    out->flags = s_roster.m[slot].flags;
    memcpy(out->name, s_roster.m[slot].name, sizeof out->name);
    return true;
}

int tfn_pool_frame_seal(uint8_t *frame, int len, int cap)
{
    pf_board_t *b = pool();
    return b ? tfn_pool_v4_seal(&b->keys, b->mac, b->ctr++, frame, len, cap) : -1;
}

int tfn_pool_frame_open(const uint8_t mac[6], const uint8_t *frame, int len)
{
    pf_board_t *b = me();
    if (!b || !b->on) {
        if (b) {
            b->rx.count[TFN_POOL_CNT_OTHER]++;
        }
        return TFN_POOL_DROP_OTHER;
    }
    int slot;
    return tfn_pool_v4_open(&b->rx, mac, frame, len, &slot);  // (the same numbers: 0 ok, 1 other ... 4 old)
}

int tfn_pool_udp_seal(uint8_t *pkt, int len, int cap)
{
    pf_board_t *b = pool();
    return b ? tfn_pool_udp2_seal(&b->keys, b->ctr++, pkt, len, cap) : -1;
}

int tfn_pool_udp_open(const uint8_t mac[6], const uint8_t *pkt, int len)
{
    pf_board_t *b = pool();
    if (!b) {
        return TFN_POOL_DROP_OTHER;
    }
    int slot;
    int r = len >= 9 && !memcmp(mac, pkt + 3, 6) ? tfn_pool_udp2_open(&b->rx, pkt, len, &slot) : TFN_POOL_DROP_BAD;
    b->udp[r]++;
    return r;
}

void tfn_pool_counts(uint32_t out[TFN_POOL_CNT_N])
{
    pf_board_t *b = me();
    for (int i = 0; i < TFN_POOL_CNT_N; i++) {
        out[i] = b ? b->rx.count[i] + b->udp[i] : 0;
    }
}

bool tfn_pool_beacon_seal(const uint8_t *plain, int n, uint8_t *out, int *on)
{
    pf_board_t *b = pool();
    *on = 0;
    if (!b || n != 43) {
        return false;
    }
    if (memcmp(plain, b->bplain, 43)) {
        memset(out, 0xEE, 78);  // (not the plain part of the vector: the test sees this packet)
    } else {
        memcpy(out, b->bpkt, 78);
    }
    *on = 78;
    return true;
}

bool tfn_pool_beacon_open(const uint8_t *pkt, int n, uint8_t *plain, int *pn)
{
    *pn = 0;
    if (!pool() || n != 78) {
        return false;
    }
    for (int i = 0; i < s_nbin; i++) {
        if (!memcmp(pkt, s_bin[i].pkt, 78)) {
            memcpy(plain, s_bin[i].plain, 43);
            *pn = 43;
            return true;
        }
    }
    return false;
}

int tfn_pool_join_beacon(uint8_t *out, int cap)
{
    pf_board_t *b = me();
    if (!b || !b->join || cap < TFN_POOL_JOIN_LEN) {
        return 0;
    }
    tfn_pool_jbeacon_make(b->jmac, b->jid, b->jname, out);
    return TFN_POOL_JOIN_LEN;
}

bool tfn_pool_web_ok(const char *method, const char *uri, const char *hdr)
{
    (void)method;
    (void)uri;
    (void)hdr;
    return false;
}

bool tfn_pool_req_mgr(void *req)
{
    (void)req;
    return false;
}

bool tfn_pool_join_start(void)
{
    return false;
}

void tfn_pool_join_stop(void)
{
}

tfn_pool_join_t tfn_pool_join_step(void)
{
    return TFN_POOL_JOIN_OFF;
}

int tfn_pool_join_left_s(void)
{
    return 0;
}

const char *tfn_pool_join_why(void)
{
    return "";
}

bool tfn_pool_join_sas(char digits[TFN_POOL_SAS_LEN], char mgr[17])
{
    digits[0] = 0;
    mgr[0] = 0;
    return false;
}

bool tfn_pool_join_allow(const char *digits)
{
    (void)digits;
    return false;
}

void tfn_pool_join_no(void)
{
}

bool tfn_pool_leave(bool by_screen)
{
    (void)by_screen;
    return false;
}
