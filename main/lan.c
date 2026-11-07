// LAN: the arcade boards on the same WiFi network (see lan.h).

#include <string.h>
#include <strings.h>
#include "tfn_util.h"
#include "arc_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "app.h"
#include "friends.h"
#include "lan.h"
#include "link.h"
#include "notify.h"
#include "settings.h"
#include "wifi.h"
#include "vpn.h"
#include "tfn_pool.h"
#include <stdio.h>

static const char *TAG = "lan";

#define BEACON_MS 2000
// the beacon: 'A' 'B', version, MAC (6), name (16), app (12), then flags (1): bit 0 = "linked" (a link in
// use, link_in_use: the manager does not read the board and takes no picture of its screen). A receiver reads at
// least B_LEN bytes, so the old boards and the old manager ignore the flags.
#define B_MAGIC0  'A'
#define B_MAGIC1  'B'
#define B_VERSION 1
#define B_LEN     (3 + 6 + 16 + 12)
#define B_FLAGS   B_LEN
#define BF_LINKED 1
// a link packet: 'A' 'D', MAC (6), the link packet
#define D_MAGIC0 'A'
#define D_MAGIC1 'D'
// the list of the hub (the manager): 'A' 'R', version, count, then for each board: MAC (6),
// tunnel address (4), name (16), app (12)
#define R_MAGIC1 'R'
#define R_ENTRY  38
// a WALKIE call: 'A' 'C', version, MAC of the sender (6), MAC of the board it is for (6), the step
#define C_MAGIC1 'C'
#define C_LEN    16
// an invite (the friend who is invited gets a notification): 'A' 'I', version, MAC
// of the sender (6), the menu name of the app (I_NAME bytes, 0 at the end). Only to the friends that have not got
// that app open. The board of a friend shows a notice; a tap opens the app at its friends lobby ("friends").
#define I_MAGIC1 'I'
#define I_NAME   20
#define I_LEN    (3 + 6 + I_NAME)
#define I_GAP_MS 90000  // the sender: one invite for each app and each friend in this time
// the pools (tfn_pool.h): the call and the invite of version 2 (the body of version 1 and TFN_POOL_UDP_TAIL bytes),
// the encrypted beacon 'A' 'P' and the JOIN beacon 'A' 'J' (only the manager reads it)
#define P_MAGIC1 'P'
#define J_MAGIC1 'J'
#define V_SEALED 2
_Static_assert(B_LEN + 1 - 3 + 8 == TFN_POOL_BEACON_PLAIN, "the plain part of 'A' 'P' = the beacon fields + the ID");

static int s_sock = -1;
volatile bool g_lan_vpn_only;  // tests (console "vpnonly 1"): only the tunnel, as in two houses
// a board: its LAN address (lip, lseen) and its tunnel address (vip, vseen)
typedef struct {
    lan_peer_t p;
    uint32_t lip, vip;
    int64_t lseen, vseen;
    bool friend;  // a friend of this board (a full table gives a friend the place of another board)
} peer_t;
static peer_t s_peer[LAN_PEERS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static void (*s_rx)(const uint8_t mac[6], const uint8_t *pkt, int len);
static void (*s_call_rx)(const uint8_t from[6], uint8_t op);
static uint8_t s_mac[6];

// WALKIE and TALKIE (and the manager, which says WALKIE) speak the same protocol
static bool walkie_family(const char *n)
{
    return !strncmp(n, "WALKIE", 6) || !strncmp(n, "TALKIE", 6);
}

// a board in a pool: the receive rule of a packet from mac (in = FRIENDS_IN_*)
static bool pool_take(int in, const uint8_t mac[6])
{
    return friends_pool_take(in, mac, true, tfn_pool_strict(), tfn_pool_role(tfn_pool_member(mac)));
}

static void peer_seen(const uint8_t mac[6], uint32_t ip, bool vpn, const char *name, const char *app)
{
    if (tfn_pool_on() && !pool_take(FRIENDS_IN_PEER, mac)) {
        return;  // (STRICT: a board that is not a member and not a friend is not in the table)
    }
    int64_t t = tfn_now_ms();
    const bool fr = friends_find(mac) >= 0;
    portENTER_CRITICAL(&s_mux);
    int k = -1;
    for (int i = 0; i < LAN_PEERS && k < 0; i++) {
        if (s_peer[i].p.seen && !memcmp(s_peer[i].p.mac, mac, 6)) {
            k = i;
        }
    }
    for (int i = 0; i < LAN_PEERS && k < 0; i++) {
        if (!s_peer[i].p.seen || t - s_peer[i].p.seen > LAN_GONE_MS) {
            k = i;
        }
    }
    // the table is full: a friend takes the place of the board that is not a friend and was seen first (else a friend
    // could never get a place: each board sends a beacon each 2 s and keeps its place)
    if (k < 0 && fr) {
        for (int i = 0; i < LAN_PEERS; i++) {
            if (!s_peer[i].friend && (k < 0 || s_peer[i].p.seen < s_peer[k].p.seen)) {
                k = i;
            }
        }
    }
    if (k >= 0) {
        peer_t *q = &s_peer[k];
        if (memcmp(q->p.mac, mac, 6) || !q->p.seen) {
            memset(q, 0, sizeof *q);
            memcpy(q->p.mac, mac, 6);
        }
        if (vpn) {
            q->vip = ip;
            q->vseen = t;
        } else {
            q->lip = ip;
            q->lseen = t;
        }
        q->friend = fr;
        if (name && name[0]) {
            memcpy(q->p.name, name, 15);
            q->p.name[15] = 0;
        }
        if (app) {
            memcpy(q->p.app, app, 11);
            q->p.app[11] = 0;
        }
        q->p.seen = t;
    }
    portEXIT_CRITICAL(&s_mux);
}

int lan_peers(lan_peer_t *out, int max)
{
    int64_t t = tfn_now_ms();
    int n = 0;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < LAN_PEERS && n < max; i++) {
        peer_t *q = &s_peer[i];
        if (!q->p.seen || t - q->p.seen > LAN_GONE_MS) {
            continue;
        }
        out[n] = q->p;
        bool lan = !g_lan_vpn_only && q->lseen && t - q->lseen <= LAN_GONE_MS;
        if (g_lan_vpn_only && !(q->vseen && t - q->vseen <= LAN_GONE_MS)) {
            continue;
        }
        out[n].ip = lan ? q->lip : q->vip;
        out[n].vpn = !lan;
        n++;
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}

void lan_set_rx(void (*cb)(const uint8_t mac[6], const uint8_t *pkt, int len))
{
    s_rx = cb;
}

void lan_send_link(const uint8_t *pkt, int len)
{
    if (s_sock < 0 || wifi_state() != WIFI_CONNECTED || len <= 0 || len > 250) {
        return;  // (the VPN needs the WiFi too)
    }
    uint8_t buf[2 + 6 + 250];
    buf[0] = D_MAGIC0;
    buf[1] = D_MAGIC1;
    memcpy(buf + 2, s_mac, 6);
    memcpy(buf + 8, pkt, (size_t)len);
    // only the addresses (this runs in the app task for each packet of a game: no copy of the whole table on its stack)
    uint32_t ip[LAN_PEERS];
    int n = 0;
    const app_t *me = app_current();
    const int64_t t = tfn_now_ms();
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < LAN_PEERS; i++) {
        const peer_t *q = &s_peer[i];
        if (!q->p.seen || t - q->p.seen > LAN_GONE_MS) {
            continue;
        }
        // only to the boards with the same app open (their beacon says it): a game sends about 70 packets each
        // second, and copies to all the boards on the LAN and the VPN fill the radio queue, so the ESP-NOW
        // packets of the game are refused and the link ends
        if (me && q->p.app[0] && strncmp(q->p.app, me->name, sizeof q->p.app - 1) &&
            !(walkie_family(q->p.app) && walkie_family(me->name))) {
            continue;  // (WALKIE and TALKIE: one family)
        }
        const bool lan = !g_lan_vpn_only && q->lseen && t - q->lseen <= LAN_GONE_MS;
        if (g_lan_vpn_only && !(q->vseen && t - q->vseen <= LAN_GONE_MS)) {
            continue;
        }
        ip[n++] = lan ? q->lip : q->vip;
    }
    portEXIT_CRITICAL(&s_mux);
    for (int i = 0; i < n; i++) {
        struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(LAN_PORT), .sin_addr.s_addr = ip[i]};
        sendto(s_sock, buf, (size_t)(8 + len), 0, (struct sockaddr *)&to, sizeof to);
    }
}

void lan_set_call_rx(void (*cb)(const uint8_t from[6], uint8_t op))
{
    s_call_rx = cb;
}

// a call or an invite of version 1 in b (n bytes) for the board to: a roster member of the pool of this board gets
// version 2 (the byte 2 and the tail of the pool). Returns the bytes to send.
static int seal_udp(uint8_t *b, int n, int cap, const uint8_t to[6])
{
    if (!tfn_pool_on() || tfn_pool_member(to) < 0) {
        return n;
    }
    b[2] = V_SEALED;
    const int m = tfn_pool_udp_seal(b, n, cap);
    if (m > 0) {
        return m;
    }
    b[2] = 1;  // (the pool went away now)
    return n;
}

bool lan_send_call(const uint8_t to[6], uint8_t op)
{
    if (s_sock < 0 || wifi_state() != WIFI_CONNECTED) {
        return false;
    }
    lan_peer_t p[LAN_PEERS];
    int n = lan_peers(p, LAN_PEERS);
    for (int i = 0; i < n; i++) {
        if (memcmp(p[i].mac, to, 6)) {
            continue;
        }
        uint8_t b[C_LEN + TFN_POOL_UDP_TAIL];
        b[0] = D_MAGIC0;
        b[1] = C_MAGIC1;
        b[2] = 1;
        memcpy(b + 3, s_mac, 6);
        memcpy(b + 9, to, 6);
        b[15] = op;
        const int n = seal_udp(b, C_LEN, sizeof b, to);
        struct sockaddr_in dst = {.sin_family = AF_INET, .sin_port = htons(LAN_PORT), .sin_addr.s_addr = p[i].ip};
        return sendto(s_sock, b, (size_t)n, 0, (struct sockaddr *)&dst, sizeof dst) == n;
    }
    return false;
}

void lan_send_invite(const char *app)
{
    if (s_sock < 0 || wifi_state() != WIFI_CONNECTED || !app || !app[0]) {
        return;
    }
    // the last invites (a friend and an app): no second invite in I_GAP_MS (a lobby that opens again, a link restart)
    static struct {
        uint8_t mac[6];
        char app[I_NAME];
        int64_t at;
    } sent[LAN_PEERS];
    const int64_t t = tfn_now_ms();
    lan_peer_t p[LAN_PEERS];
    int n = lan_peers(p, LAN_PEERS);
    for (int i = 0; i < n; i++) {
        if (friends_find(p[i].mac) < 0) {
            continue;
        }
        if (p[i].app[0] && !strncmp(p[i].app, app, sizeof p[i].app - 1)) {
            continue;  // (it is in the app already)
        }
        int k = -1, old = 0;
        for (int j = 0; j < LAN_PEERS; j++) {
            if (!memcmp(sent[j].mac, p[i].mac, 6) && !strncmp(sent[j].app, app, I_NAME - 1)) {
                k = j;
                break;
            }
            if (sent[j].at < sent[old].at) {
                old = j;
            }
        }
        if (k >= 0 && t - sent[k].at < I_GAP_MS) {
            continue;
        }
        if (k < 0) {
            k = old;
        }
        memcpy(sent[k].mac, p[i].mac, 6);
        snprintf(sent[k].app, I_NAME, "%s", app);
        sent[k].at = t;
        uint8_t b[I_LEN + TFN_POOL_UDP_TAIL];
        memset(b, 0, sizeof b);
        b[0] = D_MAGIC0;
        b[1] = I_MAGIC1;
        b[2] = 1;
        memcpy(b + 3, s_mac, 6);
        strncpy((char *)b + 9, app, I_NAME - 1);
        const int bn = seal_udp(b, I_LEN, sizeof b, p[i].mac);
        struct sockaddr_in dst = {.sin_family = AF_INET, .sin_port = htons(LAN_PORT), .sin_addr.s_addr = p[i].ip};
        sendto(s_sock, b, (size_t)bn, 0, (struct sockaddr *)&dst, sizeof dst);
        ESP_LOGI(TAG, "invite to %s: %s", p[i].name, app);
    }
}

// an invite of a friend: a notice (the lan task, or the app task for a radio invite: lan_invite_notice). A new invite
// of the same friend to the same app takes the place of the old notice.
static void on_invite(const uint8_t from[6], const char *app_name)
{
    const app_t *a = app_find(app_name);
    if (!a || friends_find(from) < 0) {
        return;
    }
    // (the app is open: its lobby shows the friend; by the name: a running app file has another app_t than the one
    // that app_find gives)
    const app_t *cur = app_current();
    if (cur == a || (cur && cur->name && !strcasecmp(cur->name, a->name))) {
        return;
    }
    static struct {
        uint8_t mac[6];
        const app_t *a;
        uint32_t id;
    } last[4];
    static int next;
    for (int i = 0; i < 4; i++) {
        if (last[i].id && last[i].a == a && !memcmp(last[i].mac, from, 6)) {
            notify_remove(last[i].id);
            last[i].id = 0;
        }
    }
    const char *who = friends_name_of(from);
    if (!who || !who[0]) {
        who = "A FRIEND";
    }
    const bool he = settings_lang_all() == 1;
    char title[NT_TITLE], text[NT_TEXT];
    if (he) {
        snprintf(title, sizeof title, "%s רוצה לשחק איתך", who);
        snprintf(text, sizeof text, "%s פתח את %s עם חברים. הקישו כדי להצטרף.", who, a->name);
    } else {
        snprintf(title, sizeof title, "%s WANTS TO PLAY WITH YOU", who);
        snprintf(text, sizeof text, "%s OPENED %s WITH FRIENDS. TAP TO JOIN.", who, a->name);
    }
    uint32_t id = notify_post(a->name, title, text, "friends");
    ESP_LOGI(TAG, "invite of %s: %s (notice %lu)", who, a->name, (unsigned long)id);
    if (!id) {
        return;
    }
    memcpy(last[next].mac, from, 6);
    last[next].a = a;
    last[next].id = id;
    next = (next + 1) % 4;
}

void lan_invite_notice(const uint8_t from[6], const char *app_name)
{
    on_invite(from, app_name);
}

static void send_to(uint32_t to, const uint8_t *b, int n)
{
    struct sockaddr_in dst = {.sin_family = AF_INET, .sin_port = htons(LAN_PORT), .sin_addr.s_addr = to};
    sendto(s_sock, b, (size_t)n, 0, (struct sockaddr *)&dst, sizeof dst);
}

// the beacons: the plain beacon 'A' 'B' (tfn_pool_plain_beacon), the encrypted 'A' 'P' of a pool board (the plain
// part = the fields of 'A' 'B' after its 3 first bytes, and the device ID), the JOIN beacon 'A' 'J' while the JOIN
// screen waits for a manager
static void send_beacon(uint32_t to)
{
    uint8_t b[B_LEN + 1 + 8];
    memset(b, 0, sizeof b);
    b[0] = B_MAGIC0;
    b[1] = B_MAGIC1;
    b[2] = B_VERSION;
    memcpy(b + 3, s_mac, 6);
    strncpy((char *)b + 9, friends_my_name(), 15);
    const app_t *a = app_current();
    if (a) {
        strncpy((char *)b + 25, a->name, 11);
    }
    b[B_FLAGS] = link_in_use() ? BF_LINKED : 0;
    if (tfn_pool_plain_beacon()) {
        send_to(to, b, B_LEN + 1);
    }
    if (tfn_pool_on()) {
        tfn_pool_member_t me;
        if (tfn_pool_member_get(tfn_pool_member(s_mac), &me)) {
            memcpy(b + B_LEN + 1, me.id, 8);
        }
        uint8_t p[TFN_POOL_BEACON_LEN];
        int pn = 0;
        if (tfn_pool_beacon_seal(b + 3, TFN_POOL_BEACON_PLAIN, p, &pn) && pn > 0 && pn <= (int)sizeof p) {
            send_to(to, p, pn);
        }
    }
    uint8_t j[TFN_POOL_JOIN_LEN];
    const int jn = tfn_pool_join_beacon(j, sizeof j);
    if (jn > 0 && jn <= (int)sizeof j) {
        send_to(to, j, jn);
    }
}

// a board in a pool: the roster pins the device ID of each member that is a friend (friends.c writes the NVS only when
// a pin changes)
static void pool_pin(void)
{
    if (!tfn_pool_on()) {
        return;
    }
    const int n = tfn_pool_members();
    for (int s = 0; s < n; s++) {
        tfn_pool_member_t m;
        if (tfn_pool_member_get(s, &m)) {
            friends_pin(m.mac, m.id);
        }
    }
}

// an 'A' 'P' beacon: a member of the pool of this board (a good tag)
static void on_pool_beacon(const uint8_t *buf, int n, uint32_t ip, bool vpn)
{
    uint8_t p[TFN_POOL_BEACON_PLAIN];
    int pn = 0;
    if (!tfn_pool_beacon_open(buf, n, p, &pn) || pn != TFN_POOL_BEACON_PLAIN || !memcmp(p, s_mac, 6)) {
        return;
    }
    char name[16], app[12];
    memcpy(name, p + 6, 15);
    name[15] = 0;
    memcpy(app, p + 22, 11);
    app[11] = 0;
    peer_seen(p, ip, vpn, name, app);
}

// a WALKIE call: version 1, or version 2 of the pool (n >= C_LEN). The receive function gets the op with
// LAN_CALL_SEALED for a good version 2 call.
static void on_call(const uint8_t *buf, int n, uint32_t ip, bool vpn)
{
    if (memcmp(buf + 9, s_mac, 6) || !memcmp(buf + 3, s_mac, 6)) {
        return;  // (not for this board)
    }
    uint8_t op = buf[15];
    if (buf[2] == V_SEALED) {
        if (tfn_pool_udp_open(buf + 3, buf, n) != 0) {
            return;  // (the pool service counts it)
        }
        op |= LAN_CALL_SEALED;
    } else if (tfn_pool_on() && !pool_take(FRIENDS_IN_V3, buf + 3)) {
        return;
    }
    peer_seen(buf + 3, ip, vpn, NULL, NULL);
    void (*cb)(const uint8_t *, uint8_t) = s_call_rx;
    if (cb && (buf[2] == V_SEALED || !(op & LAN_CALL_SEALED))) {
        cb(buf + 3, op);  // (a version 1 op with the high bit: no call)
    }
}

// an invite: version 1, or version 2 of the pool (n >= I_LEN)
static void on_invite_pkt(const uint8_t *buf, int n, uint32_t ip, bool vpn)
{
    if (!memcmp(buf + 3, s_mac, 6)) {
        return;
    }
    if (buf[2] == V_SEALED) {
        if (tfn_pool_udp_open(buf + 3, buf, n) != 0) {
            return;
        }
    } else if (tfn_pool_on() && !pool_take(FRIENDS_IN_V3, buf + 3)) {
        return;
    }
    peer_seen(buf + 3, ip, vpn, NULL, NULL);
    char app[I_NAME];
    memcpy(app, buf + 9, I_NAME - 1);
    app[I_NAME - 1] = 0;
    on_invite(buf + 3, app);
}

static void lan_rx(const uint8_t *buf, int n, uint32_t ip);

static void lan_task(void *arg)
{
    (void)arg;
    static uint8_t buf[600];
    int64_t beacon_at = 0;
    const app_t *last_app = NULL;
    bool last_linked = false;
    for (;;) {
        int64_t t = tfn_now_ms();
        if (app_current() != last_app) {
            last_app = app_current();
            beacon_at = 0;
        }  // a new app: tell the others at once
        if (link_in_use() != last_linked) {
            last_linked = !last_linked;
            beacon_at = 0;
        }  // (and a link that starts or ends)
        if (wifi_state() == WIFI_CONNECTED && t - beacon_at >= BEACON_MS) {
            beacon_at = t;
            send_beacon(htonl(INADDR_BROADCAST));  // the boards on the LAN
            uint32_t hub = vpn_hub_ip();
            if (hub) {
                send_beacon(hub);  // the hub (the manager) over the VPN
            }
            pool_pin();
        }
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = recvfrom(s_sock, buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (n >= 8) {
            lan_rx(buf, n, from.sin_addr.s_addr);
        }
    }
}

// a UDP packet from the address ip (n >= 8)
static void lan_rx(const uint8_t *buf, int n, uint32_t from_ip)
{
    bool vpn = vpn_is_tunnel_ip(from_ip);
    if (g_lan_vpn_only && !vpn) {
        return;
    }
    if (buf[0] == B_MAGIC0 && buf[1] == R_MAGIC1 && buf[2] == 1 && vpn) {
        // the list of the hub: the boards on the VPN
        int cnt = buf[3];
        for (int i = 0; i < cnt && 4 + (i + 1) * R_ENTRY <= n; i++) {
            const uint8_t *e = buf + 4 + i * R_ENTRY;
            if (!memcmp(e, s_mac, 6)) {
                continue;
            }
            uint32_t ip;
            memcpy(&ip, e + 6, 4);
            char name[16], app[12];
            memcpy(name, e + 10, 15);
            name[15] = 0;
            memcpy(app, e + 26, 11);
            app[11] = 0;
            peer_seen(e, ip, true, name, app);
        }
        return;
    }
    if (buf[0] == D_MAGIC0 && buf[1] == C_MAGIC1 && (buf[2] == 1 || buf[2] == V_SEALED) && n >= C_LEN) {
        on_call(buf, n, from_ip, vpn);  // a WALKIE call for this board
        return;
    }
    if (buf[0] == D_MAGIC0 && buf[1] == I_MAGIC1 && (buf[2] == 1 || buf[2] == V_SEALED) && n >= I_LEN) {
        on_invite_pkt(buf, n, from_ip, vpn);  // an invite of a friend
        return;
    }
    if (buf[0] == B_MAGIC0 && (buf[1] == P_MAGIC1 || buf[1] == J_MAGIC1)) {
        if (buf[1] == P_MAGIC1) {
            on_pool_beacon(buf, n, from_ip, vpn);
        }
        return;  // (the JOIN beacon is for the manager)
    }
    const uint8_t *mac = buf + (buf[1] == B_MAGIC1 ? 3 : 2);
    if (!memcmp(mac, s_mac, 6)) {
        return;  // (our own broadcast)
    }
    if (buf[0] == B_MAGIC0 && buf[1] == B_MAGIC1 && buf[2] == B_VERSION && n >= B_LEN) {
        char name[16], app[12];
        memcpy(name, buf + 9, 15);
        name[15] = 0;
        memcpy(app, buf + 25, 11);
        app[11] = 0;
        peer_seen(mac, from_ip, vpn, name, app);
    } else if (buf[0] == D_MAGIC0 && buf[1] == D_MAGIC1 && n > 8) {
        peer_seen(mac, from_ip, vpn, NULL, NULL);
        void (*cb)(const uint8_t *, const uint8_t *, int) = s_rx;
        if (cb) {
            cb(mac, buf + 8, n - 8);
        }
    }
}

void lan_start(void)
{
    if (s_sock >= 0) {
        return;
    }
    arc_mac(s_mac);
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        ESP_LOGW(TAG, "no socket");
        return;
    }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    struct timeval tv = {.tv_sec = 0, .tv_usec = 250000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(LAN_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) {
        ESP_LOGW(TAG, "bind failed");
        close(s);
        return;
    }
    s_sock = s;
    xTaskCreatePinnedToCore(lan_task, "lan", 4096, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "on, UDP port %d", LAN_PORT);
}
