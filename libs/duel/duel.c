// The match of two boards for the turn games (duel.h). The packets (app id of the game):
//   INV    the host invites (each 0.5 s): the number of the match
//   JOIN   a friend joins (each 0.3 s until the match is on): the host's MAC
//   OK     the host takes it (an answer to each JOIN): the friend's MAC, the number
//   MSG    a message: to, its number, the data (again each 0.3 s until the ACK)
//   ACK    to, the number (also each 1 s with the number 0: "I am here")
//   BYE    to: the end
//   RE     a standing match: to, the match, my random number, the number of the other that I saw (each 0.5 s)
//   END    to, the match, why: the match that you want back ended here
// No packet from the other board for 6 s: DUEL_GONE (a standing match: DUEL_REJOIN, it waits).
#include <string.h>
#include "tfn/duel.h"
#include "esp_random.h"
#include "tfn/friends.h"
#include "tfn/link.h"

#define D_VER 1
enum {
    K_INV = 1,
    K_JOIN,
    K_OK,
    K_MSG,
    K_ACK,
    K_BYE,
    K_RE,
    K_END
};
#define QN     8
#define MAXINV 4
static struct {
    uint8_t app, me[6], peer[6];
    int st;
    bool host, keep, fresh;
    uint32_t seed;
    uint32_t nonce, peer_nonce;  // (the rejoin: my number, the one of the other)
    int64_t now, sent_at, beat_at, heard;
    // out: a queue, one message in the air
    struct {
        uint8_t d[DUEL_MSG_MAX];
        uint8_t n;
    } q[QN];
    int qh, qn;
    uint16_t out_seq;  // the number of the message at the head of the queue
    // in
    uint16_t in_next;  // the number of the next message to take
    struct {
        uint8_t d[DUEL_MSG_MAX];
        uint8_t n;
    } rx[QN];
    int rh, rn;
    struct {
        uint8_t mac[6];
        int64_t seen;
        uint32_t seed;
    } inv[MAXINV];
    int ninv;
    uint8_t ask_mac[6];
    uint32_t ask_id;
    bool asked;
    int ended;
    int re_ans;
    int64_t re_at;  // (the RE answers while the line is on: re_ans since line_on, the last one)
} D;

static void put_mac(uint8_t *p, const uint8_t m[6])
{
    memcpy(p, m, 6);
}
static bool for_me(const uint8_t *p)
{
    return memcmp(p, D.me, 6) == 0;
}
static void send0(int kind, const uint8_t *data, int n)
{
    uint8_t b[64];
    b[0] = D_VER;
    if (n > 0) {
        memcpy(b + 1, data, (size_t)n);
    }
    link_send(D.app, (uint8_t)kind, b, n + 1);
}
static void send_to_mac(const uint8_t mac[6], int kind, const uint8_t *data, int n)
{
    uint8_t b[60];
    put_mac(b, mac);
    if (n > 0) {
        memcpy(b + 6, data, (size_t)n);
    }
    send0(kind, b, n + 6);
}
static void send_to(int kind, const uint8_t *data, int n)
{
    send_to_mac(D.peer, kind, data, n);
}
static void reset_line(void)
{
    D.qh = D.qn = 0;
    D.out_seq = 1;
    D.in_next = 1;
    D.rh = D.rn = 0;
    D.sent_at = 0;
    D.beat_at = 0;
}
static void line_on(void)
{
    D.st = DUEL_ON;
    D.heard = D.now;
    D.fresh = true;
    D.re_ans = 0;
    reset_line();
}
static void send_re(void)
{
    uint8_t b[12];
    memcpy(b, &D.seed, 4);
    memcpy(b + 4, &D.nonce, 4);
    memcpy(b + 8, &D.peer_nonce, 4);
    send_to(K_RE, b, 12);
}
static void away(void)
{
    D.st = DUEL_REJOIN;
    D.nonce = esp_random() | 1;
    D.peer_nonce = 0;
    D.sent_at = 0;
    reset_line();
}

void duel_open(uint8_t app)
{
    memset(&D, 0, sizeof D);
    D.app = app;
    link_start();
    link_my_mac(D.me);
}
void duel_close(void)
{
    duel_stop();
    link_stop();
    D.st = DUEL_OFF;
}
void duel_leave(void)
{
    link_stop();
    D.st = DUEL_OFF;
}
void duel_invite(void)
{
    D.st = DUEL_INVITE;
    D.host = true;
    D.seed = esp_random() | 1;
    D.sent_at = 0;
    D.keep = false;
    reset_line();
}
void duel_join(const uint8_t mac[6])
{
    memcpy(D.peer, mac, 6);
    D.st = DUEL_JOIN;
    D.host = false;
    D.sent_at = 0;
    D.heard = D.now;
    D.keep = false;
    for (int i = 0; i < D.ninv; i++) {
        if (!memcmp(D.inv[i].mac, mac, 6)) {
            D.seed = D.inv[i].seed;
        }
    }
    reset_line();
}
void duel_stop(void)
{
    if (D.st == DUEL_ON || D.st == DUEL_JOIN || D.st == DUEL_REJOIN) {
        send_to(K_BYE, NULL, 0);
        send_to(K_BYE, NULL, 0);
    }
    D.st = DUEL_OFF;
    D.keep = false;
}
void duel_keep(bool on)
{
    D.keep = on;
}
void duel_rejoin(const uint8_t peer[6], uint32_t id)
{
    memcpy(D.peer, peer, 6);
    D.seed = id;
    D.keep = true;
    D.ended = 0;
    away();
}
int duel_state(void)
{
    return D.st;
}
bool duel_host(void)
{
    return D.host;
}
const uint8_t *duel_peer(void)
{
    return D.peer;
}
uint32_t duel_seed(void)
{
    return D.seed;
}
bool duel_fresh(void)
{
    const bool f = D.fresh;
    D.fresh = false;
    return f;
}
bool duel_asked(uint8_t mac[6], uint32_t *id)
{
    if (!D.asked) {
        return false;
    }
    D.asked = false;
    memcpy(mac, D.ask_mac, 6);
    *id = D.ask_id;
    return true;
}
void duel_say_ended(const uint8_t mac[6], uint32_t id, uint8_t why)
{
    uint8_t b[5];
    memcpy(b, &id, 4);
    b[4] = why;
    for (int k = 0; k < 3; k++) {
        send_to_mac(mac, K_END, b, 5);
    }
}
int duel_ended(void)
{
    const int e = D.ended;
    D.ended = 0;
    return e;
}
int duel_invites(uint8_t mac[][6], int max)
{
    int n = 0;
    for (int i = 0; i < D.ninv && n < max; i++) {
        memcpy(mac[n++], D.inv[i].mac, 6);
    }
    return n;
}
bool duel_send(const void *data, int len)
{
    if (D.st != DUEL_ON || D.qn == QN || len < 0 || len > DUEL_MSG_MAX) {
        return false;
    }
    const int k = (D.qh + D.qn) % QN;
    memcpy(D.q[k].d, data, (size_t)len);
    D.q[k].n = (uint8_t)len;
    if (D.qn++ == 0) {
        D.sent_at = 0;  // (the head goes at once)
    }
    return true;
}
bool duel_recv(void *data, int *len)
{
    if (!D.rn) {
        return false;
    }
    memcpy(data, D.rx[D.rh].d, D.rx[D.rh].n);
    *len = D.rx[D.rh].n;
    D.rh = (D.rh + 1) % QN;
    D.rn--;
    return true;
}
static void on_packet(const link_msg_t *m)
{
    if (m->app != D.app || m->len < 1 || m->data[0] != D_VER || friends_find(m->mac) < 0) {
        return;
    }
    const uint8_t *p = m->data + 1;
    const int n = m->len - 1;
    const bool from_peer = !memcmp(m->mac, D.peer, 6);
    switch (m->kind) {
    case K_INV:
        if (n >= 4) {
            int i = 0;
            while (i < D.ninv && memcmp(D.inv[i].mac, m->mac, 6)) {
                i++;
            }
            if (i == D.ninv) {
                if (D.ninv == MAXINV) {
                    return;
                }
                D.ninv++;
                memcpy(D.inv[i].mac, m->mac, 6);
            }
            D.inv[i].seen = D.now;
            memcpy(&D.inv[i].seed, p, 4);
            if (D.st == DUEL_JOIN && from_peer) {
                D.heard = D.now;
            }
        }
        break;
    case K_JOIN:
        if (n >= 6 && for_me(p) && (D.st == DUEL_INVITE || (D.st == DUEL_ON && D.host && from_peer))) {
            if (D.st == DUEL_INVITE) {
                memcpy(D.peer, m->mac, 6);
                line_on();
            }
            D.heard = D.now;
            uint8_t b[4];
            memcpy(b, &D.seed, 4);
            send_to(K_OK, b, 4);
        }
        break;
    case K_OK:
        if (n >= 10 && for_me(p) && D.st == DUEL_JOIN && from_peer) {
            memcpy(&D.seed, p + 6, 4);
            line_on();
        }
        break;
    case K_MSG:
        if (n >= 9 && for_me(p) && D.st == DUEL_ON && from_peer) {
            D.heard = D.now;
            const uint16_t seq = (uint16_t)(p[6] | (p[7] << 8));
            const int len = p[8];
            if (seq == D.in_next && len <= DUEL_MSG_MAX && n >= 9 + len && D.rn < QN) {
                const int k = (D.rh + D.rn) % QN;
                memcpy(D.rx[k].d, p + 9, (size_t)len);
                D.rx[k].n = (uint8_t)len;
                D.rn++;
                D.in_next++;
            }
            if ((int16_t)(seq - D.in_next) < 0) {
                uint8_t b[2] = {(uint8_t)seq, (uint8_t)(seq >> 8)};
                send_to(K_ACK, b, 2);
            }  // (it has it: say so)
        }
        break;
    case K_ACK:
        if (n >= 8 && for_me(p) && D.st == DUEL_ON && from_peer) {
            D.heard = D.now;
            const uint16_t seq = (uint16_t)(p[6] | (p[7] << 8));
            if (seq && D.qn && seq == D.out_seq) {
                D.qh = (D.qh + 1) % QN;
                D.qn--;
                D.out_seq++;
                D.sent_at = 0;
            }
        }
        break;
    case K_BYE:
        if (n >= 6 && for_me(p) && (D.st == DUEL_ON || D.st == DUEL_JOIN) && from_peer) {
            if (D.keep) {
                away();
            } else {
                D.st = DUEL_GONE;
            }
        }
        for (int i = 0; i < D.ninv; i++) {
            if (!memcmp(D.inv[i].mac, m->mac, 6)) {
                D.inv[i] = D.inv[--D.ninv];
                break;
            }
        }
        break;
    case K_RE:
        if (n >= 18 && for_me(p)) {
            uint32_t id, nonce, seen;
            memcpy(&id, p + 6, 4);
            memcpy(&nonce, p + 10, 4);
            memcpy(&seen, p + 14, 4);
            const bool mine = D.keep && from_peer && id == D.seed;
            if (mine && D.st == DUEL_REJOIN) {
                D.peer_nonce = nonce;
                if (seen == D.nonce) {
                    line_on();  // (each one has seen the other)
                }
                send_re();
            } else if (mine && D.st == DUEL_ON) {
                if (nonce != D.peer_nonce) {
                    away();
                    D.peer_nonce = nonce;
                    send_re();
                }  // (it came back anew: again)
                else {
                    // answer a board that has not seen this number (it waits in REJOIN), and an RE that repeats (the
                    // answer of line_on was lost) at most 2 times. Without this limit, two boards that are both on
                    // answer each RE of the other, a packet in each frame, for as long as no packet is lost
                    D.heard = D.now;
                    if (seen != D.nonce || (D.re_ans < 2 && D.now - D.re_at >= 400)) {
                        D.re_ans++;
                        D.re_at = D.now;
                        send_re();
                    }
                }
            } else if (!mine) {
                memcpy(D.ask_mac, m->mac, 6);
                D.ask_id = id;
                D.asked = true;
            }
        }
        break;
    case K_END:
        if (n >= 11 && for_me(p) && D.keep && from_peer) {
            uint32_t id;
            memcpy(&id, p + 6, 4);
            if (id == D.seed) {
                D.ended = p[10] + 1;
                D.st = DUEL_OFF;
                D.keep = false;
            }
        }
        break;
    }
}
void duel_poll(int64_t now)
{
    D.now = now;
    link_msg_t m;
    for (int guard = 0; guard < 40 && link_recv(&m); guard++) {
        on_packet(&m);
    }
    // the invites that are old
    int k = 0;
    for (int i = 0; i < D.ninv; i++) {
        if (now - D.inv[i].seen < 2000) {
            D.inv[k++] = D.inv[i];
        }
    }
    D.ninv = k;
    if (D.st == DUEL_INVITE && now - D.sent_at >= 500) {
        D.sent_at = now;
        uint8_t b[4];
        memcpy(b, &D.seed, 4);
        send0(K_INV, b, 4);
        friends_beacon(now);
        friends_invite(now);
    } else if (D.st == DUEL_JOIN) {
        if (now - D.sent_at >= 300) {
            D.sent_at = now;
            send_to(K_JOIN, NULL, 0);
        }
        if (now - D.heard > 6000) {
            D.st = DUEL_GONE;
        }
    } else if (D.st == DUEL_REJOIN) {
        if (now - D.sent_at >= 500) {
            D.sent_at = now;
            send_re();
            friends_beacon(now);
        }
    } else if (D.st == DUEL_ON) {
        if (D.qn && now - D.sent_at >= 300) {
            D.sent_at = now;
            uint8_t b[3 + DUEL_MSG_MAX];
            b[0] = (uint8_t)D.out_seq;
            b[1] = (uint8_t)(D.out_seq >> 8);
            b[2] = D.q[D.qh].n;
            memcpy(b + 3, D.q[D.qh].d, D.q[D.qh].n);
            send_to(K_MSG, b, 3 + D.q[D.qh].n);
        }
        if (now - D.beat_at >= 1000) {
            D.beat_at = now;
            const uint8_t b[2] = {0, 0};
            send_to(K_ACK, b, 2);
        }
        if (now - D.heard > 6000) {
            if (D.keep) {
                away();
            } else {
                D.st = DUEL_GONE;
            }
        }
    } else if (now - D.beat_at >= 500) {
        D.beat_at = now;
        friends_beacon(now);
    }
}
