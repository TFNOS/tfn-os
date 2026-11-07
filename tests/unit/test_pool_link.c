// The unit test of link v4 and the LAN packets of the pools (design P4 stage 2, step 5): main/link.c, main/lan.c,
// main/friends.c and main/walkie_call.c with fake ESP-IDF and OS parts (tests/unit/fake_link/) and a fake pool
// service on the pool core (fake_link/pool_fake.c). One copy of the link and LAN code; each pool board has its own
// keys, counters and windows in the fake service (the board of a call = fk_mac).
// It checks: a board with no pool sends v3 and drops v4, 'A' 'P', the calls and the invites of version 2; the v4
// frames, the call v2, the invite v2, the 'A' 'P' and 'A' 'J' beacons are byte for byte the packets of the manager
// (tests/unit/vectors/pool_v1.json); the v4 receive (a replay, a changed bit, another pool, not a member, the second
// copy by the LAN); the legacy peer rule (the v3 copies for 5 s); the RELAXED and STRICT rules of the link, the table
// of the boards and WAKE; the pins of the roster IDs.
// Build and run: python tests/unit/run.py. Argument: the vector file.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_board.h"
#include "friends.h"
#include "lan.h"
#include "link.h"
#include "pool_fake.h"
#include "walkie_call.h"

static int s_fail, s_n;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        s_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            s_fail++;                                                                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                        \
        }                                                                                                              \
    } while (0)

// ---------------------------------------------------------------- the vector file (a small reader)
static char *s_vec;

static const char *find(const char *from, const char *needle)
{
    const char *p = from ? strstr(from, needle) : NULL;
    if (!p) {
        printf("FAIL: no %s in the vector file\n", needle);
        exit(1);
    }
    return p;
}

// the value after "key": in the text from p
static const char *value(const char *p, const char *key)
{
    char k[40];
    snprintf(k, sizeof k, "\"%s\"", key);
    p = find(p, k) + strlen(k);
    while (*p == ' ' || *p == ':' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

static int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int hex(const char *p, const char *key, uint8_t *out, int cap)
{
    p = value(p, key);
    if (*p++ != '"') {
        return -1;
    }
    int n = 0;
    while (hexv(p[0]) >= 0 && hexv(p[1]) >= 0 && n < cap) {
        out[n++] = (uint8_t)(hexv(p[0]) << 4 | hexv(p[1]));
        p += 2;
    }
    return n;
}

static long num(const char *p, const char *key)
{
    return strtol(value(p, key), NULL, 10);
}

static void keys_of(const char *p, pf_keys_t *k)
{
    hex(p, "link", k->link, 16);
    k->ptag = (uint8_t)num(p, "ptag");
    hex(p, "beacon", k->beacon, 32);
    hex(p, "hint", k->hint, 4);
}

// ---------------------------------------------------------------- what the code sent
#define CAP_N 32
static struct {
    int kind;
    uint32_t ip;
    int n;
    uint8_t p[600];
} s_cap[CAP_N];
static int s_ncap;
static char s_log[16][200];
static int s_nlog;

static void on_out(int kind, uint32_t ip, const uint8_t *p, int n)
{
    if (s_ncap < CAP_N) {
        s_cap[s_ncap].kind = kind;
        s_cap[s_ncap].ip = ip;
        s_cap[s_ncap].n = n;
        memcpy(s_cap[s_ncap].p, p, (size_t)n);
        s_ncap++;
    }
}

static void on_log(const char *line)
{
    if (s_nlog < 16) {
        snprintf(s_log[s_nlog++], sizeof s_log[0], "%s", line);
    }
}

static void cap_reset(void)
{
    s_ncap = 0;
    s_nlog = 0;
}

static bool logged(const char *start)
{
    for (int i = 0; i < s_nlog; i++) {
        if (!strncmp(s_log[i], start, strlen(start))) {
            return true;
        }
    }
    return false;
}

// the sent packets of a kind ('E', 'U') with the first 2 bytes m0 m1 (0 = any)
static int sent(int kind, char m1)
{
    int n = 0;
    for (int i = 0; i < s_ncap; i++) {
        n += s_cap[i].kind == kind && (!m1 || s_cap[i].p[1] == (uint8_t)m1);
    }
    return n;
}

static int sent_at(int kind, char m1, int k)
{
    for (int i = 0; i < s_ncap; i++) {
        if (s_cap[i].kind == kind && (!m1 || s_cap[i].p[1] == (uint8_t)m1) && !k--) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the boards
static const uint8_t MGR[6] = {0xfe, 0x4d, 0x47, 0x52, 0x00, 0x01};
static const uint8_t A[6] = {0x02, 0, 0, 0, 0, 0x01};
static const uint8_t B[6] = {0x02, 0, 0, 0, 0, 0x02};
static const uint8_t C[6] = {0x02, 0, 0, 0, 0, 0x03};
static const uint8_t D[6] = {0x02, 0, 0, 0, 0, 0x04};
static const uint8_t L[6] = {0x0a, 0, 0, 0, 0, 0x10};  // a legacy friend (no pool)
static const uint8_t S[6] = {0x0a, 0, 0, 0, 0, 0x20};  // a stranger (not a friend)
static const uint8_t K[6] = {0x0a, 0, 0, 0, 0, 0x30};  // a keyed friend of another pool
static const uint8_t NOT_MEMBER[6] = {0x02, 0, 0, 0, 0, 0x09};
static uint8_t ID_A[8], ID_MGR[8];
static const uint8_t ID_B[8] = {0xb0, 1, 2, 3, 4, 5, 6, 7}, ID_C[8] = {0xc0, 1, 2, 3, 4, 5, 6, 7};
static const uint8_t ID_K[8] = {0x4b, 1, 2, 3, 4, 5, 6, 7};
#define IP(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
static const uint32_t IP_MGR = IP(192, 0, 2, 2), IP_B = IP(192, 0, 2, 12), IP_L = IP(192, 0, 2, 20);
static const uint32_t IP_S = IP(192, 0, 2, 30), IP_HUB = IP(10, 0, 0, 1);
static pf_keys_t K2, K1;  // the pool key (epoch 2) and another key (epoch 1: the tag byte of another pool)
static uint16_t s_v3id = 1000;

static void be(const uint8_t mac[6])
{
    memcpy(fk_mac, mac, 6);
}

static void ms(int t)
{
    fk_now_us += (int64_t)t * 1000;
}

// a v3 frame of app with 2 data bytes, from mac by radio (ch = byte 6: 0x20 = a v3 copy of a pool board)
static void v3_in_ch(const uint8_t mac[6], uint8_t app, uint8_t ch)
{
    uint16_t id = s_v3id++;
    uint8_t f[11] = {'A', 'L', 3, app, 1, 2, ch, (uint8_t)id, (uint8_t)(id >> 8), 0x11, 0x22};
    fk_espnow_in(mac, f, sizeof f, -40);
}

static void v3_in(const uint8_t mac[6], uint8_t app)
{
    v3_in_ch(mac, app, 0);
}

static int recv_all(link_msg_t *last)
{
    int n = 0;
    link_msg_t m;
    while (link_recv(&m)) {
        n++;
        if (last) {
            *last = m;
        }
    }
    return n;
}

static void lan_in(uint32_t ip, const uint8_t *p, int n)
{
    fk_lan_push(ip, p, n);
    fk_lan_run();
}

// a plain beacon 'A' 'B' of mac with the name and the app
static void beacon_in(uint32_t ip, const uint8_t mac[6], const char *name, const char *app)
{
    uint8_t b[38] = {'A', 'B', 1};
    memcpy(b + 3, mac, 6);
    strncpy((char *)b + 9, name, 15);
    strncpy((char *)b + 25, app, 11);
    lan_in(ip, b, sizeof b);
}

static bool peer(const uint8_t mac[6], lan_peer_t *out)
{
    lan_peer_t p[LAN_PEERS];
    int n = lan_peers(p, LAN_PEERS);
    for (int i = 0; i < n; i++) {
        if (!memcmp(p[i].mac, mac, 6)) {
            if (out) {
                *out = p[i];
            }
            return true;
        }
    }
    return false;
}

// the pool: A, B, the manager and C in the pool of K2
static void pool_on(bool strict_a)
{
    pf_board(0, A, &K2, strict_a, false);
    pf_board(1, B, &K2, false, false);
    pf_board(2, MGR, &K2, false, false);
}

// ---------------------------------------------------------------- the tests
static void test_no_pool(void)
{
    be(A);
    cap_reset();
    CHECK(link_send('Y', 1, "hi", 2));
    const uint8_t want[] = {'A', 'L', 3, 'Y', 1, 2, 0, 0, 0, 'h', 'i'};  // (channel 0, the first id)
    CHECK(s_ncap == 1 && s_cap[0].kind == 'E' && s_cap[0].n == (int)sizeof want &&
          !memcmp(s_cap[0].p, want, sizeof want));
    uint8_t big[LINK_DATA_MAX + 1] = {0};
    CHECK(link_send('Y', 1, big, LINK_DATA_MAX));
    CHECK(s_ncap == 2 && s_cap[1].n == 9 + LINK_DATA_MAX && s_cap[1].p[2] == 3);
    CHECK(!link_send('Y', 1, big, LINK_DATA_MAX + 1));
    // a v4 frame, 'A' 'P', a call v2 and an invite v2: no effect
    be(B);
    uint8_t f[300];
    int fn = hex(find(s_vec, "\"frames\""), "frame", f, sizeof f);
    fk_espnow_in(A, f, fn, -40);
    CHECK(recv_all(NULL) == 0);
    v3_in(A, 'Y');
    link_msg_t m;
    CHECK(recv_all(&m) == 1 && m.app == 'Y' && m.len == 2 && m.data[1] == 0x22 && !memcmp(m.mac, A, 6));
    s_v3id--;
    v3_in(A, 'Y');  // (the same id: the v3 dedup drops it)
    CHECK(recv_all(NULL) == 0);
    be(A);
    cap_reset();
    uint8_t p[100];
    int pn = hex(find(s_vec, "\"beacon\""), "pkt", p, sizeof p);
    lan_in(IP_B, p, pn);
    pn = hex(find(s_vec, "\"invite\""), "pkt", p, sizeof p);
    lan_in(IP_B, p, pn);
    CHECK(!peer(B, NULL) && s_nlog == 0);
}

static void test_v4_send(void)
{
    pool_on(false);
    be(A);
    const char *fr = find(s_vec, "\"frames\"");
    uint8_t want[300];
    // "empty": board A, channel 0, counter 0 (a new start of the link on channel 0: a poll of the link before moved the
    // radio to the meeting channel, link_meet.h)
    link_stop();
    fk_channel = 0;
    CHECK(link_start());
    const char *e = find(fr, "\"empty\"");
    int wn = hex(e, "frame", want, sizeof want);
    pf_set_ctr(0, (uint32_t)num(e, "ctr"));
    cap_reset();
    CHECK(link_send((uint8_t)num(e, "app"), (uint8_t)num(e, "kind"), "", 0));
    CHECK(s_ncap == 1 && s_cap[0].n == wn && !memcmp(s_cap[0].p, want, (size_t)wn));
    // "short": channel 1 and QUIET (0x41), counter 65536
    const char *sh = find(fr, "\"short\"");
    wn = hex(sh, "frame", want, sizeof want);
    uint8_t data[LINK_DATA_MAX];
    int dn = hex(sh, "data", data, sizeof data);
    CHECK(link_force_channel(1));
    link_quiet(true);
    pf_set_ctr(0, (uint32_t)num(sh, "ctr"));
    cap_reset();
    CHECK(link_send((uint8_t)num(sh, "app"), (uint8_t)num(sh, "kind"), data, dn));
    CHECK(s_ncap == 1 && s_cap[0].n == wn && !memcmp(s_cap[0].p, want, (size_t)wn));
    link_quiet(false);
    // "max": board B, channel 6, 230 data bytes = 250 bytes, the counter 2^32 - 2
    const char *mx = find(fr, "\"max\"");
    wn = hex(mx, "frame", want, sizeof want);
    dn = hex(mx, "data", data, sizeof data);
    be(B);
    CHECK(link_force_channel(6));
    pf_set_ctr(1, (uint32_t)strtoul(value(mx, "ctr"), NULL, 10));
    cap_reset();
    CHECK(dn == LINK_DATA_MAX && link_send((uint8_t)num(mx, "app"), (uint8_t)num(mx, "kind"), data, dn));
    CHECK(s_ncap == 1 && s_cap[0].n == 250 && wn == 250 && !memcmp(s_cap[0].p, want, 250));
}

static void test_v4_recv(void)
{
    const char *fr = find(s_vec, "\"frames\"");
    uint8_t sh[64], old[64], mx[300];
    int shn = hex(find(fr, "\"short\""), "frame", sh, sizeof sh);
    int oldn = hex(find(fr, "\"prev_epoch\""), "frame", old, sizeof old);
    int mxn = hex(find(fr, "\"max\""), "frame", mx, sizeof mx);
    // board B takes the "short" frame of A one time
    pool_on(false);
    be(B);
    recv_all(NULL);
    fk_espnow_in(A, sh, shn, -40);
    link_msg_t m;
    CHECK(recv_all(&m) == 1 && m.app == 89 && m.kind == 3 && m.len == 5 && !memcmp(m.data, "hello", 5) &&
          m.ch == 0x41 && m.rssi == -40 && !memcmp(m.mac, A, 6));
    fk_espnow_in(A, sh, shn, -40);  // the same frame again
    sh[14] ^= 1;
    fk_espnow_in(A, sh, shn, -40);  // a changed bit
    sh[14] ^= 1;
    fk_espnow_in(A, old, oldn, -40);         // the tag byte of another pool
    fk_espnow_in(NOT_MEMBER, sh, shn, -40);  // not a roster MAC
    CHECK(recv_all(NULL) == 0);
    uint32_t c[5];
    pf_counts(1, c);
    CHECK(c[0] == 1 && c[1] == 1 && c[2] == 1 && c[3] == 1 && c[4] == 1);
    // board A takes the "max" frame of B by the LAN, then the radio copy drops (its counter came)
    be(A);
    recv_all(NULL);
    uint8_t d[8 + 250] = {'A', 'D'};
    memcpy(d + 2, B, 6);
    memcpy(d + 8, mx, (size_t)mxn);
    lan_in(IP_B, d, 8 + mxn);
    CHECK(recv_all(&m) == 1 && m.app == 78 && m.len == LINK_DATA_MAX && m.ch == 0 && !memcmp(m.mac, B, 6));
    fk_espnow_in(B, mx, mxn, -40);
    CHECK(recv_all(NULL) == 0);
    pf_counts(0, c);
    CHECK(c[0] == 1 && c[4] == 1);
}

static void test_legacy_copies(void)
{
    pool_on(false);
    be(A);
    cap_reset();
    CHECK(link_send('Y', 1, "x", 1));
    CHECK(s_ncap == 1 && s_cap[0].p[2] == 4);
    v3_in(L, 'Y');  // a legacy friend in the same app
    CHECK(recv_all(NULL) == 1);
    cap_reset();
    CHECK(link_send('Y', 1, "x", 1));
    CHECK(s_ncap == 2 && s_cap[0].p[2] == 3 && s_cap[0].n == 10 && s_cap[1].p[2] == 4 && s_cap[1].n == 21);
    CHECK((s_cap[0].p[6] & 0x20) && !(s_cap[1].p[6] & 0x20));  // (the v3 copy has the mark of a copy)
    cap_reset();
    CHECK(link_send('Q', 1, "x", 1));  // another app: no copy
    CHECK(s_ncap == 1 && s_cap[0].p[2] == 4);
    ms(5100);
    cap_reset();
    CHECK(link_send('Y', 1, "x", 1));  // 5 s after the last v3 frame: v4 only
    CHECK(s_ncap == 1 && s_cap[0].p[2] == 4);
    // a v3 copy of a member of this pool: dropped (its v4 frame has the packet), and not a legacy peer
    v3_in_ch(B, 'W', 0x20);
    CHECK(recv_all(NULL) == 0);
    // a v3 copy of a board that is not a member (a board of another pool): taken, but not a legacy peer
    v3_in_ch(S, 'W', 0x20);
    CHECK(recv_all(NULL) == 1);
    cap_reset();
    CHECK(link_send('W', 1, "x", 1));
    CHECK(s_ncap == 1 && s_cap[0].p[2] == 4);
    // a v3 frame of a member with no mark (a member on an older firmware): taken, a legacy peer (RELAXED)
    v3_in(B, 'W');
    CHECK(recv_all(NULL) == 1);
    cap_reset();
    CHECK(link_send('W', 1, "x", 1));
    CHECK(s_ncap == 2 && s_cap[0].p[2] == 3 && s_cap[1].p[2] == 4);
}

static void test_v3_rules(void)
{
    friends_pin(K, ID_K);
    uint8_t id[8];
    CHECK(friends_keyed(friends_find(K), id) && !memcmp(id, ID_K, 8));
    for (int strict = 0; strict < 2; strict++) {
        pool_on(strict);
        be(A);
        recv_all(NULL);
        v3_in(B, 'Z');  // a roster MAC (a member on an older firmware, or a forged member)
        CHECK(recv_all(NULL) == (strict ? 0 : 1));
        v3_in(K, 'Z');  // a keyed friend of another pool
        CHECK(recv_all(NULL) == (strict ? 0 : 1));
        v3_in(L, 'Z');  // a legacy friend
        CHECK(recv_all(NULL) == 1);
        v3_in(S, 'Z');  // a stranger (the apps check the friends)
        CHECK(recv_all(NULL) == 1);
    }
    // the rule of WAKE (the core table): a call v2 of the manager; a call v1 of a friend only in RELAXED (or a
    // legacy friend in STRICT)
    CHECK(friends_pool_take(FRIENDS_IN_WAKE2, MGR, true, true, 1));
    CHECK(!friends_pool_take(FRIENDS_IN_WAKE2, B, true, true, 0));
    CHECK(!friends_pool_take(FRIENDS_IN_WAKE1, MGR, true, true, 1));
    CHECK(friends_pool_take(FRIENDS_IN_WAKE1, MGR, true, false, 1));
    CHECK(friends_pool_take(FRIENDS_IN_WAKE1, L, true, true, -1));
    CHECK(!friends_pool_take(FRIENDS_IN_WAKE1, S, false, false, -1));  // (no pool: only a friend)
    CHECK(friends_pool_take(FRIENDS_IN_WAKE1, L, false, false, -1));
}

static void test_peer_table(void)
{
    for (int strict = 1; strict >= 0; strict--) {
        pool_on(strict);
        be(A);
        ms(8000);  // (the entries of the last round go)
        beacon_in(IP_S, S, "STRANGER", "PONG");
        beacon_in(IP_L, L, "L", "PONG");
        uint8_t r[4 + 2 * 38] = {'A', 'R', 1, 2};
        const uint8_t *who[2] = {C, D};  // a member, and a board that is not a member and not a friend
        for (int i = 0; i < 2; i++) {
            uint8_t *e = r + 4 + 38 * i;
            memcpy(e, who[i], 6);
            uint32_t tip = IP(10, 0, 0, 30 + i);
            memcpy(e + 6, &tip, 4);
            strcpy((char *)e + 10, "HUB BOARD");
        }
        lan_in(IP_HUB, r, sizeof r);
        CHECK(peer(L, NULL) && peer(C, NULL));
        CHECK(peer(S, NULL) == !strict && peer(D, NULL) == !strict);
    }
}

static void test_calls(void)
{
    pool_on(false);
    be(A);
    fk_wifi = 2;
    beacon_in(IP_MGR, MGR, "MANAGER", "");
    beacon_in(IP_L, L, "L", "CHESS");
    beacon_in(IP_B, B, "B", "PONG");
    const char *u = find(s_vec, "\"udp\"");
    const char *call = find(u, "\"call\"");
    uint8_t want[64];
    int wn = hex(call, "pkt", want, sizeof want);
    pf_set_ctr(0, (uint32_t)num(call, "ctr"));
    cap_reset();
    CHECK(lan_send_call(MGR, (uint8_t)num(call, "op")));  // a member: version 2 (the vector of the manager)
    int k = sent_at('U', 'C', 0);
    CHECK(k >= 0 && s_cap[k].ip == IP_MGR && s_cap[k].n == wn && !memcmp(s_cap[k].p, want, (size_t)wn));
    cap_reset();
    CHECK(lan_send_call(L, 1));  // not a member: version 1
    k = sent_at('U', 'C', 0);
    CHECK(k >= 0 && s_cap[k].ip == IP_L && s_cap[k].n == 16 && s_cap[k].p[2] == 1);
    cap_reset();
    lan_send_invite("CHESS");  // the manager (a member, a friend): version 2; L has CHESS open; B: version 2
    CHECK(sent('U', 'I') == 2);
    for (int i = 0; i < 2; i++) {
        k = sent_at('U', 'I', i);
        CHECK(k >= 0 && s_cap[k].n == 29 + 13 && s_cap[k].p[2] == 2);
    }
    // the invite of the manager vector (board B, PONG) comes to A one time
    const char *inv = find(u, "\"invite\"");
    uint8_t p[64];
    int pn = hex(inv, "pkt", p, sizeof p);
    cap_reset();
    lan_in(IP_B, p, pn);
    CHECK(logged("notice PONG|B WANTS TO PLAY WITH YOU"));
    cap_reset();
    lan_in(IP_B, p, pn);
    CHECK(!logged("notice PONG"));
    // a call v2 to A with the key of another pool: dropped (the count "other")
    pf_board(4, NOT_MEMBER, &K1, false, false);
    uint8_t b[29] = {'A', 'C', 2};
    memcpy(b + 3, MGR, 6);
    memcpy(b + 9, A, 6);
    b[15] = 1;
    int bn = pf_udp_seal(4, b, 16, sizeof b);
    uint32_t c0[5], c1[5];
    pf_counts(0, c0);
    cap_reset();
    lan_in(IP_MGR, b, bn);
    walkie_call_update(fk_now_us / 1000);
    pf_counts(0, c1);
    CHECK(bn == 29 && c1[1] == c0[1] + 1 && s_nlog == 0);
    fk_wifi = 0;
}

// a WAKE of the manager to A: version 1, or version 2 (sealed by the manager board)
static void wake_in(bool v2)
{
    uint8_t b[29] = {'A', 'C', 1};
    memcpy(b + 3, MGR, 6);
    memcpy(b + 9, A, 6);
    b[15] = 5;
    int n = 16;
    if (v2) {
        b[2] = 2;
        n = pf_udp_seal(2, b, 16, sizeof b);
    }
    cap_reset();
    lan_in(IP_MGR, b, n);
    ms(3100);  // (TALKIE opens at most each 3 s)
    walkie_call_update(fk_now_us / 1000);
}

static void test_wake(void)
{
    walkie_call_init();
    for (int strict = 0; strict < 2; strict++) {
        pool_on(strict);
        pf_set_ctr(2, 500);
        be(A);
        wake_in(false);
        CHECK(logged("open TALKIE") == !strict);
        wake_in(true);
        CHECK(logged("open TALKIE"));
    }
    // a replay of a call v2: dropped
    pool_on(false);
    pf_set_ctr(2, 900);
    uint8_t b[29] = {'A', 'C', 2};
    memcpy(b + 3, MGR, 6);
    memcpy(b + 9, A, 6);
    b[15] = 5;
    int n = pf_udp_seal(2, b, 16, sizeof b);
    be(A);
    for (int i = 0; i < 2; i++) {
        cap_reset();
        lan_in(IP_MGR, b, n);
        ms(3100);
        walkie_call_update(fk_now_us / 1000);
        CHECK(logged("open TALKIE") == (i == 0));
    }
    // a call v1 of a member (B): RELAXED asks, STRICT drops it (a forged member)
    for (int strict = 1; strict >= 0; strict--) {
        pool_on(strict);
        be(A);
        uint8_t c[16] = {'A', 'C', 1};
        memcpy(c + 3, B, 6);
        memcpy(c + 9, A, 6);
        c[15] = 1;
        cap_reset();
        lan_in(IP_B, c, sizeof c);
        walkie_call_update(fk_now_us / 1000);
        CHECK(logged("ask B|CALLS YOU ON WALKIE") == !strict);
        ms(5100);
        walkie_call_update(fk_now_us / 1000);  // (the question closes: no call for 5 s)
    }
    // a good WAKE v2 of a member that is not the manager: no TALKIE
    pool_on(false);
    pf_set_ctr(1, 100);
    memcpy(b + 3, B, 6);
    b[2] = 2;
    n = pf_udp_seal(1, b, 16, sizeof b);
    be(A);
    cap_reset();
    lan_in(IP_B, b, n);
    ms(3100);
    walkie_call_update(fk_now_us / 1000);
    CHECK(n == 29 && !logged("open TALKIE"));
    // no pool: WAKE v1 of a friend opens TALKIE, v2 is dropped
    pf_board(0, A, NULL, false, false);
    wake_in(true);
    CHECK(!logged("open TALKIE"));
    wake_in(false);
    CHECK(logged("open TALKIE"));
}

static void test_beacons(void)
{
    const char *bl = find(find(s_vec, "\"beacon\""), "\"list\"");
    uint8_t plain[43], pkt[78], mplain[43], mpkt[78];
    CHECK(hex(bl, "plain", plain, 43) == 43 && hex(bl, "pkt", pkt, 78) == 78);
    const char *m2 = find(bl, "\"MANAGER\"");
    CHECK(hex(m2, "plain", mplain, 43) == 43 && hex(m2, "pkt", mpkt, 78) == 78);
    for (int plain_on = 0; plain_on < 2; plain_on++) {
        pf_board(0, A, &K2, false, plain_on);
        pf_board(1, B, &K2, false, false);
        pf_beacon(0, plain, pkt);
        be(A);
        friends_set_my_name("BOARD ONE");
        fk_app_name = "PONG";
        // the link is in use (the flag 1 of the vector): A sent 'Y' and B answered
        CHECK(link_send('Y', 1, "x", 1));
        uint8_t f[40] = {'A', 'L', 4, 'Y', 1, 1, 0};
        f[12] = 7;
        be(B);
        int fn = pf_frame_seal(1, f, 13, sizeof f);
        be(A);
        fk_espnow_in(B, f, fn, -40);
        recv_all(NULL);
        fk_wifi = 2;
        fk_hub = plain_on ? IP_HUB : 0;
        cap_reset();
        fk_lan_run();
        int k = sent_at('U', 'P', 0);
        CHECK(sent('U', 'P') == (plain_on ? 2 : 1) && k >= 0 && s_cap[k].ip == 0xffffffffu && s_cap[k].n == 78 &&
              !memcmp(s_cap[k].p, pkt, 78));
        CHECK(sent('U', 'B') == (plain_on ? 2 : 0));
        if (plain_on) {
            k = sent_at('U', 'B', 0);
            CHECK(k >= 0 && s_cap[k].n == 38 && !memcmp(s_cap[k].p + 3, plain, 35));
            CHECK(sent_at('U', 'P', 1) >= 0 && s_cap[sent_at('U', 'P', 1)].ip == IP_HUB);
        }
        fk_hub = 0;
        fk_wifi = 0;
        fk_app_name = NULL;
    }
    // the 'A' 'P' of the manager: into the table of the boards with its name
    pf_beacon_in(mpkt, mplain);
    ms(8000);
    lan_in(IP_MGR, mpkt, 78);
    lan_peer_t p;
    CHECK(peer(MGR, &p) && !strcmp(p.name, "MANAGER") && p.ip == IP_MGR);
    mpkt[40] ^= 1;  // (another packet: the fake service does not open it)
    ms(8000);
    lan_in(IP_MGR, mpkt, 78);
    CHECK(!peer(MGR, NULL));
    // the JOIN beacon (a board with no pool, the JOIN screen open): the plain beacon and 'A' 'J'
    const char *j = find(s_vec, "\"join_pkt\"");
    uint8_t jid[8], jpkt[40];
    hex(j, "id", jid, 8);
    int jn = hex(j, "pkt", jpkt, sizeof jpkt);
    pf_board(3, D, NULL, false, false);
    pf_join(3, D, jid, "NEW BOARD");
    be(D);
    fk_wifi = 2;
    cap_reset();
    fk_lan_run();
    int k = sent_at('U', 'J', 0);
    CHECK(sent('U', 'B') == 1 && sent('U', 'P') == 0 && k >= 0 && s_cap[k].n == jn && jn == 33 &&
          !memcmp(s_cap[k].p, jpkt, 33));
    pf_join(3, D, NULL, NULL);
    fk_wifi = 0;
    be(A);
}

static void test_pins(void)
{
    pool_on(false);
    be(A);
    friends_remove(friends_find(B));  // (a new friend has no pin)
    CHECK(friends_add(B, "B") && !friends_keyed(friends_find(B), NULL));
    fk_wifi = 2;
    fk_lan_run();  // (the beacon time: the roster pins the IDs)
    fk_wifi = 0;
    uint8_t id[8];
    CHECK(friends_keyed(friends_find(B), id) && !memcmp(id, ID_B, 8));
    CHECK(friends_keyed(friends_find(MGR), id) && !memcmp(id, ID_MGR, 8));
    CHECK(!friends_keyed(friends_find(L), NULL));
    CHECK(!friends_pin(B, ID_B));  // (the same pin: no change, no NVS write)
    // the friends after a removed friend move down and keep their own pins
    int k = friends_find(K), b = friends_find(B), m = friends_find(MGR);
    CHECK(m < k && k < b);
    friends_remove(m);
    CHECK(friends_find(K) == k - 1 && friends_keyed(k - 1, id) && !memcmp(id, ID_K, 8));
    CHECK(friends_find(B) == b - 1 && friends_keyed(b - 1, id) && !memcmp(id, ID_B, 8));
    CHECK(friends_add(MGR, "MANAGER") && !friends_keyed(friends_find(MGR), NULL));
}

int main(int argc, char **argv)
{
    FILE *fh = fopen(argc > 1 ? argv[1] : "tests/unit/vectors/pool_v1.json", "rb");
    if (!fh) {
        printf("POOL LINK: no vector file\n");
        return 1;
    }
    fseek(fh, 0, SEEK_END);
    long fs = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    s_vec = calloc(1, (size_t)fs + 1);
    if (fread(s_vec, 1, (size_t)fs, fh) != (size_t)fs) {
        return 1;
    }
    fclose(fh);
    const char *dv = find(s_vec, "\"derive\"");
    keys_of(find(dv, "\"epoch\": 2"), &K2);
    keys_of(find(dv, "\"epoch\": 1"), &K1);
    const char *bl = find(find(s_vec, "\"beacon\""), "\"list\"");
    hex(bl, "id", ID_A, 8);
    hex(find(bl, "\"MANAGER\""), "id", ID_MGR, 8);
    CHECK(K1.ptag != K2.ptag);

    fk_out = on_out;
    fk_log = on_log;
    pf_reset();
    pf_roster_add(MGR, 1, ID_MGR, "MANAGER");
    pf_roster_add(A, 0, ID_A, "BOARD ONE");
    pf_roster_add(B, 0, ID_B, "B");
    pf_roster_add(C, 0, ID_C, "C");
    pf_board(0, A, NULL, false, false);  // (no pool yet)
    be(A);
    friends_init();
    friends_add(B, "B");
    friends_add(MGR, "MANAGER");
    friends_add(L, "L");
    friends_add(K, "K");
    lan_start();
    CHECK(link_start());

    test_no_pool();
    test_v4_send();
    test_v4_recv();
    test_legacy_copies();
    test_v3_rules();
    test_peer_table();
    test_calls();
    test_wake();
    test_beacons();
    test_pins();
    printf("POOL LINK %s: %d checks, %d failed\n", s_fail ? "FAIL" : "OK", s_n, s_fail);
    return s_fail ? 1 : 0;
}
