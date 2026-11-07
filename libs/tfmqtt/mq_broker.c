// tfmqtt: the broker (see tfmqtt.h). No stored sessions: each link is a clean session (the CONNACK says "no session"
// also to a client with clean = false). QoS 2 in (PUBREC / PUBREL / PUBCOMP), at most QoS 1 out (a SUBACK grants 0
// or 1). The retained messages and the will messages stay in RAM until the app closes the broker.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfmqtt.h"

#define REC_MAX 8  // the QoS 2 ids of one client that wait for a PUBREL

typedef struct {
    char f[MQ_TOPIC_MAX + 1];
    uint8_t qos;
} sub_t;

typedef struct {
    bool used, up, dead;
    int dead_why;
    char id[64];
    uint16_t ka, next_id;
    int64_t t_rx, t_conn;
    sub_t sub[MQ_BRK_SUBS];
    int nsub;
    char *will_topic;
    uint8_t *will_msg;
    int will_len, will_qos;
    bool will_retain;
    uint16_t rec[REC_MAX];
    uint32_t n_in, n_out;
    mq_rx_t rx;
} cl_t;

typedef struct {
    char topic[MQ_TOPIC_MAX + 1];
    uint8_t *msg;  // NULL = a free place
    int len, qos;
    uint32_t seq;
} ret_t;

struct mq_brk {
    mq_brk_cfg_t cfg;
    cl_t cl[MQ_BRK_CLIENTS];
    ret_t ret[MQ_BRK_RETAIN];
    uint32_t seq, auto_id;
    uint8_t out[MQ_MAX_PKT + 8];
};

const char *mq_gone_text(int why)
{
    switch (why) {
    case MQ_GONE_BYE:
        return "disconnect";
    case MQ_GONE_CLOSED:
        return "the link closed";
    case MQ_GONE_PROTO:
        return "a bad packet";
    case MQ_GONE_TIME:
        return "keep-alive time";
    case MQ_GONE_TAKEN:
        return "the same id connected again";
    case MQ_GONE_BIG:
        return "a packet too long";
    case MQ_GONE_FULL:
        return "no room to send";
    default:
        return "?";
    }
}

static void bev(mq_brk_t *b, int e, int slot, const char *t, const void *m, int n, int a, int x, int c)
{
    if (b->cfg.event) {
        b->cfg.event(b->cfg.user, e, slot, t, m, n, a, x, c);
    }
}

// a send to a client; a fault marks it dead (reap() drops it: a drop can send a will message to other clients)
static void csend(mq_brk_t *b, int s, const void *d, int n, int why)
{
    cl_t *k = &b->cl[s];
    if (k->dead || n <= 0) {
        return;
    }
    if (!b->cfg.send || !b->cfg.send(b->cfg.user, s, d, n)) {
        k->dead = true;
        k->dead_why = why;
    }
}

static void kill(mq_brk_t *b, int s, int why)
{
    if (b->cl[s].dead) {
        return;
    }
    b->cl[s].dead = true;
    b->cl[s].dead_why = why;
}

mq_brk_t *mq_brk_new(const mq_brk_cfg_t *cfg)
{
    mq_brk_t *b = calloc(1, sizeof *b);
    if (!b) {
        return NULL;
    }
    b->cfg = *cfg;
    return b;
}

static void free_will(cl_t *k)
{
    free(k->will_topic);
    free(k->will_msg);
    k->will_topic = NULL;
    k->will_msg = NULL;
    k->will_len = 0;
}

void mq_brk_free(mq_brk_t *b)
{
    if (!b) {
        return;
    }
    for (int i = 0; i < MQ_BRK_CLIENTS; i++) {
        free_will(&b->cl[i]);
    }
    for (int i = 0; i < MQ_BRK_RETAIN; i++) {
        free(b->ret[i].msg);
    }
    free(b);
}

int mq_brk_open(mq_brk_t *b, int64_t now)
{
    for (int i = 0; i < MQ_BRK_CLIENTS; i++) {
        cl_t *k = &b->cl[i];
        if (k->used) {
            continue;
        }
        memset(k, 0, sizeof *k);
        k->used = true;
        k->next_id = 1;
        k->t_conn = k->t_rx = now;
        mq_rx_init(&k->rx);
        return i;
    }
    return -1;
}

// ---------------------------------------------------------------- the retained messages

static void retain(mq_brk_t *b, const char *t, const void *m, int n, int qos)
{
    int at = -1, fr = -1, old = -1;
    for (int i = 0; i < MQ_BRK_RETAIN; i++) {
        ret_t *r = &b->ret[i];
        if (!r->msg) {
            if (fr < 0) {
                fr = i;
            }
            continue;
        }
        if (!strcmp(r->topic, t)) {
            at = i;
        }
        if (old < 0 || r->seq < b->ret[old].seq) {
            old = i;
        }
    }
    if (n == 0) {  // (an empty retained message deletes the retained message of the topic)
        if (at >= 0) {
            free(b->ret[at].msg);
            b->ret[at].msg = NULL;
        }
        return;
    }
    if (n > MQ_BRK_RETAIN_LEN) {
        return;
    }
    if (at < 0) {
        at = fr >= 0 ? fr : old;  // (full: the oldest goes)
    }
    if (at < 0) {
        return;
    }
    uint8_t *copy = malloc((size_t)n);
    if (!copy) {
        return;
    }
    memcpy(copy, m, (size_t)n);
    ret_t *r = &b->ret[at];
    free(r->msg);
    snprintf(r->topic, sizeof r->topic, "%s", t);
    r->msg = copy;
    r->len = n;
    r->qos = qos > 1 ? 1 : qos;
    r->seq = ++b->seq;
}

int mq_brk_retained(const mq_brk_t *b)
{
    int n = 0;
    for (int i = 0; i < MQ_BRK_RETAIN; i++) {
        n += b->ret[i].msg != NULL;
    }
    return n;
}

// ---------------------------------------------------------------- the routing

static uint16_t cid(cl_t *k)
{
    uint16_t id = k->next_id++;
    if (!k->next_id) {
        k->next_id = 1;
    }
    return id ? id : cid(k);
}

static void deliver(mq_brk_t *b, int s, const char *t, const void *m, int n, int qos, bool ret)
{
    cl_t *k = &b->cl[s];
    const int len = mq_enc_publish(b->out, sizeof b->out, t, m, n, qos, ret, false, qos ? cid(k) : 0);
    if (len <= 0) {
        return;  // (a message longer than the buffer of a link: the broker took it, so it
    }
    // cannot be: MQ_MAX_PKT limits both)
    csend(b, s, b->out, len, MQ_GONE_FULL);
    if (!k->dead) {
        k->n_out++;
    }
}

// A message to each client with a matching filter (once for each client, with the highest QoS of its filters).
static int route(mq_brk_t *b, int from, const char *t, const void *m, int n, int qos, bool ret)
{
    if (ret) {
        retain(b, t, m, n, qos);
    }
    int got = 0;
    for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
        cl_t *k = &b->cl[s];
        if (!k->used || !k->up || k->dead) {
            continue;
        }
        int best = -1;
        for (int i = 0; i < k->nsub; i++) {
            if (mq_match(k->sub[i].f, t) && k->sub[i].qos > best) {
                best = k->sub[i].qos;
            }
        }
        if (best < 0) {
            continue;
        }
        deliver(b, s, t, m, n, qos < best ? qos : best, false);
        got++;
    }
    bev(b, MQ_BEV_PUBLISH, from, t, m, n, qos, ret, got);
    return got;
}

// ---------------------------------------------------------------- the end of a link

static void drop(mq_brk_t *b, int s, int why, bool close_sock)
{
    cl_t *k = &b->cl[s];
    if (!k->used) {
        return;
    }
    const bool up = k->up;
    k->up = false;
    k->used = false;  // (first: a will message must not go to this client)
    if (up) {
        bev(b, MQ_BEV_GONE, s, k->id, NULL, 0, why, 0, 0);
    }
    if (up && why != MQ_GONE_BYE && k->will_topic) {
        route(b, s, k->will_topic, k->will_msg, k->will_len, k->will_qos, k->will_retain);
    }
    free_will(k);
    if (close_sock && b->cfg.drop) {
        b->cfg.drop(b->cfg.user, s);
    }
}

// the clients that a fault marked
static void reap(mq_brk_t *b)
{
    for (bool again = true; again;) {
        again = false;
        for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
            if (b->cl[s].used && b->cl[s].dead) {
                drop(b, s, b->cl[s].dead_why, true);
                again = true;
            }
        }
    }
}

void mq_brk_closed(mq_brk_t *b, int slot)
{
    if (slot < 0 || slot >= MQ_BRK_CLIENTS) {
        return;
    }
    drop(b, slot, b->cl[slot].dead ? b->cl[slot].dead_why : MQ_GONE_CLOSED, false);
    reap(b);
}

// ---------------------------------------------------------------- the packets of a client

static void got_connect(mq_brk_t *b, int s, const mq_pkt_t *p, int64_t now)
{
    cl_t *k = &b->cl[s];
    int at = 0;
    char name[8];
    uint8_t a[4];
    if (!mq_get_str(p, &at, name, sizeof name) || at + 4 > p->len) {
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    const int level = p->body[at], fl = p->body[at + 1];
    at += 2;
    uint16_t ka;
    mq_get_u16(p, &at, &ka);
    const bool v311 = !strcmp(name, "MQTT") && level == 4, v31 = !strcmp(name, "MQIsdp") && level == 3;
    if (!strcmp(name, "MQTT") && level != 4) {  // (MQTT 5 and others: the code of 3.1.1, then the end)
        csend(b, s, a, mq_enc_connack(a, sizeof a, false, MQ_RC_VERSION), MQ_GONE_PROTO);
        bev(b, MQ_BEV_REFUSE, s, "", NULL, 0, MQ_RC_VERSION, 0, 0);
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    if ((!v311 && !v31) || (fl & 1) || ((fl >> 3) & 3) == 3) {
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    const bool clean = fl & 2, will = fl & 4, usr = fl & 0x80, pwd = fl & 0x40;
    char id[96];
    if (!mq_get_str(p, &at, id, sizeof id)) {
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    int rc = MQ_RC_OK;
    if (strlen(id) >= sizeof k->id || (!id[0] && !clean)) {
        rc = MQ_RC_ID;
    }
    char wt[MQ_TOPIC_MAX + 1];
    uint16_t wl = 0;
    const uint8_t *wm = NULL;
    if (will) {
        if (!mq_get_str(p, &at, wt, sizeof wt) || !mq_get_u16(p, &at, &wl) || at + wl > p->len) {
            kill(b, s, MQ_GONE_PROTO);
            return;
        }
        wm = p->body + at;
        at += wl;
        if (!mq_topic_ok(wt)) {
            kill(b, s, MQ_GONE_PROTO);
            return;
        }
    }
    char un[96] = "", pw[96] = "";
    if (usr && !mq_get_str(p, &at, un, sizeof un)) {
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    if (pwd && !mq_get_str(p, &at, pw, sizeof pw)) {
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    const char *cu = b->cfg.user_name, *cp = b->cfg.password ? b->cfg.password : "";
    if (rc == MQ_RC_OK && cu && *cu && (strcmp(un, cu) || strcmp(pw, cp))) {
        rc = MQ_RC_LOGIN;
    }
    memset(pw, 0, sizeof pw);
    if (rc != MQ_RC_OK) {
        csend(b, s, a, mq_enc_connack(a, sizeof a, false, rc), MQ_GONE_PROTO);
        bev(b, MQ_BEV_REFUSE, s, id, NULL, 0, rc, 0, 0);
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    if (!id[0]) {
        snprintf(id, sizeof id, "auto-%u", (unsigned)++b->auto_id);
    }
    for (int o = 0; o < MQ_BRK_CLIENTS; o++) {  // (the same id on another link: the old link goes)
        if (o != s && b->cl[o].used && b->cl[o].up && !strcmp(b->cl[o].id, id)) {
            kill(b, o, MQ_GONE_TAKEN);
        }
    }
    snprintf(k->id, sizeof k->id, "%s", id);
    k->ka = ka;
    if (will) {
        k->will_topic = malloc(strlen(wt) + 1);
        k->will_msg = malloc(wl ? wl : 1);
        if (k->will_topic && k->will_msg) {
            strcpy(k->will_topic, wt);
            memcpy(k->will_msg, wm, wl);
            k->will_len = wl;
            k->will_qos = (fl >> 3) & 3;
            k->will_retain = fl & 0x20;
        } else {
            free_will(k);
        }
    }
    k->up = true;
    k->t_conn = now;
    csend(b, s, a, mq_enc_connack(a, sizeof a, false, MQ_RC_OK), MQ_GONE_FULL);
    bev(b, MQ_BEV_CONNECT, s, k->id, NULL, 0, clean, ka, 0);
}

static void got_publish(mq_brk_t *b, int s, const mq_pkt_t *p)
{
    cl_t *k = &b->cl[s];
    const int qos = (p->flags >> 1) & 3;
    int at = 0;
    char t[MQ_TOPIC_MAX + 1];
    uint16_t id = 0;
    uint8_t a[4];
    if (qos == 3 || !mq_get_str(p, &at, t, sizeof t) || !mq_topic_ok(t) || (qos && !mq_get_u16(p, &at, &id))) {
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    if (qos == 2) {
        int fr = -1;
        for (int i = 0; i < REC_MAX; i++) {
            if (k->rec[i] == id) {  // (a resend of a message that the broker has: only the PUBREC)
                csend(b, s, a, mq_enc_ack(a, sizeof a, MQ_PUBREC, id), MQ_GONE_FULL);
                return;
            }
            if (!k->rec[i] && fr < 0) {
                fr = i;
            }
        }
        if (fr < 0) {
            kill(b, s, MQ_GONE_FULL);
            return;
        }
        k->rec[fr] = id;
    }
    k->n_in++;
    // (the packet is in the buffer of this link: the route sends from b->out, so it stays valid)
    route(b, s, t, p->body + at, p->len - at, qos, p->flags & 1);
    if (qos == 1) {
        csend(b, s, a, mq_enc_ack(a, sizeof a, MQ_PUBACK, id), MQ_GONE_FULL);
    }
    if (qos == 2) {
        csend(b, s, a, mq_enc_ack(a, sizeof a, MQ_PUBREC, id), MQ_GONE_FULL);
    }
}

static void got_subscribe(mq_brk_t *b, int s, const mq_pkt_t *p)
{
    cl_t *k = &b->cl[s];
    int at = 0;
    uint16_t id;
    if (p->flags != 2 || !mq_get_u16(p, &at, &id) || at >= p->len) {
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    uint8_t codes[16];
    char news[16][MQ_TOPIC_MAX + 1];
    int n = 0;
    while (at < p->len) {
        char f[MQ_TOPIC_MAX + 1];
        if (n >= 16 || !mq_get_str(p, &at, f, sizeof f) || at >= p->len || p->body[at] > 2) {
            kill(b, s, MQ_GONE_PROTO);
            return;
        }
        const int want = p->body[at++];
        uint8_t code = 0x80;
        if (mq_filter_ok(f)) {
            int i = 0;
            while (i < k->nsub && strcmp(k->sub[i].f, f)) {
                i++;
            }
            if (i == k->nsub && k->nsub < MQ_BRK_SUBS) {
                snprintf(k->sub[k->nsub++].f, MQ_TOPIC_MAX + 1, "%s", f);
            }
            if (i < k->nsub) {
                code = (uint8_t)(want > 1 ? 1 : want);
                k->sub[i].qos = code;
            }
        }
        codes[n] = code;
        snprintf(news[n], sizeof news[n], "%s", f);
        n++;
    }
    uint8_t a[24];
    csend(b, s, a, mq_enc_suback(a, sizeof a, id, codes, n), MQ_GONE_FULL);
    for (int j = 0; j < n; j++) {
        bev(b, MQ_BEV_SUB, s, news[j], NULL, 0, codes[j], 0, 0);
        if (codes[j] == 0x80) {
            continue;
        }
        for (int i = 0; i < MQ_BRK_RETAIN && !k->dead; i++) {  // the retained messages of the new filter
            ret_t *r = &b->ret[i];
            if (r->msg && mq_match(news[j], r->topic)) {
                deliver(b, s, r->topic, r->msg, r->len, r->qos < codes[j] ? r->qos : codes[j], true);
            }
        }
    }
}

static void got_unsubscribe(mq_brk_t *b, int s, const mq_pkt_t *p)
{
    cl_t *k = &b->cl[s];
    int at = 0;
    uint16_t id;
    if (p->flags != 2 || !mq_get_u16(p, &at, &id) || at >= p->len) {
        kill(b, s, MQ_GONE_PROTO);
        return;
    }
    while (at < p->len) {
        char f[MQ_TOPIC_MAX + 1];
        if (!mq_get_str(p, &at, f, sizeof f)) {
            kill(b, s, MQ_GONE_PROTO);
            return;
        }
        for (int i = 0; i < k->nsub; i++) {
            if (!strcmp(k->sub[i].f, f)) {
                k->sub[i] = k->sub[--k->nsub];
                bev(b, MQ_BEV_UNSUB, s, f, NULL, 0, 0, 0, 0);
                break;
            }
        }
    }
    uint8_t a[4];
    csend(b, s, a, mq_enc_ack(a, sizeof a, MQ_UNSUBACK, id), MQ_GONE_FULL);
}

static void got(mq_brk_t *b, int s, const mq_pkt_t *p, int64_t now)
{
    cl_t *k = &b->cl[s];
    int at = 0;
    uint16_t id;
    uint8_t a[4];
    if (!k->up) {
        if (p->type == MQ_CONNECT) {
            got_connect(b, s, p, now);
        } else {
            kill(b, s, MQ_GONE_PROTO);  // (the first packet must be the CONNECT)
        }
        return;
    }
    switch (p->type) {
    case MQ_PUBLISH:
        got_publish(b, s, p);
        break;
    case MQ_PUBREL:
        if (!mq_get_u16(p, &at, &id)) {
            kill(b, s, MQ_GONE_PROTO);
            break;
        }
        for (int i = 0; i < REC_MAX; i++) {
            if (k->rec[i] == id) {
                k->rec[i] = 0;
            }
        }
        csend(b, s, a, mq_enc_ack(a, sizeof a, MQ_PUBCOMP, id), MQ_GONE_FULL);
        break;
    case MQ_PUBACK:  // (the broker keeps no copy: nothing to do)
    case MQ_PUBREC:
    case MQ_PUBCOMP:
        break;
    case MQ_SUBSCRIBE:
        got_subscribe(b, s, p);
        break;
    case MQ_UNSUBSCRIBE:
        got_unsubscribe(b, s, p);
        break;
    case MQ_PINGREQ:
        csend(b, s, a, mq_enc_simple(a, sizeof a, MQ_PINGRESP), MQ_GONE_FULL);
        break;
    case MQ_DISCONNECT:
        free_will(k);
        kill(b, s, MQ_GONE_BYE);
        break;
    default:
        kill(b, s, MQ_GONE_PROTO);
        break;  // (a second CONNECT, or a packet of a broker)
    }
}

void mq_brk_in(mq_brk_t *b, int s, const void *d, int n, int64_t now)
{
    if (s < 0 || s >= MQ_BRK_CLIENTS || !b->cl[s].used) {
        return;
    }
    cl_t *k = &b->cl[s];
    const uint8_t *p8 = d;
    k->t_rx = now;
    while (n > 0 && !k->dead) {
        const int took = mq_rx_feed(&k->rx, p8, n);
        p8 += took;
        n -= took;
        mq_pkt_t p;
        while (!k->dead && mq_rx_next(&k->rx, &p)) {
            got(b, s, &p, now);
        }
        if (k->rx.bad || (took == 0 && n > 0)) {
            kill(b, s, MQ_GONE_BIG);
        }
    }
    reap(b);
}

void mq_brk_tick(mq_brk_t *b, int64_t now)
{
    for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
        cl_t *k = &b->cl[s];
        if (!k->used) {
            continue;
        }
        if (!k->up && now - k->t_conn > 10000) {
            kill(b, s, MQ_GONE_TIME);
        }
        if (k->up && k->ka && now - k->t_rx > (int64_t)k->ka * 1500) {
            kill(b, s, MQ_GONE_TIME);
        }
    }
    reap(b);
}

int mq_brk_publish(mq_brk_t *b, const char *t, const void *m, int n, int qos, bool ret)
{
    if (!mq_topic_ok(t) || n < 0 || n > MQ_MAX_PKT - MQ_TOPIC_MAX - 16) {
        return -1;
    }
    const int got = route(b, -1, t, m, n, qos > 1 ? 1 : qos, ret);
    reap(b);
    return got;
}

void mq_brk_info(const mq_brk_t *b, int s, mq_brk_info_t *o)
{
    memset(o, 0, sizeof *o);
    if (s < 0 || s >= MQ_BRK_CLIENTS) {
        return;
    }
    const cl_t *k = &b->cl[s];
    o->used = k->used;
    o->up = k->up;
    snprintf(o->id, sizeof o->id, "%s", k->id);
    o->keepalive = k->ka;
    o->nsub = k->nsub;
    o->n_in = k->n_in;
    o->n_out = k->n_out;
    o->t_rx = k->t_rx;
    o->t_conn = k->t_conn;
}

int mq_brk_sub_at(const mq_brk_t *b, int s, int i, char *out, int max)
{
    if (s < 0 || s >= MQ_BRK_CLIENTS || i < 0 || i >= b->cl[s].nsub) {
        return -1;
    }
    snprintf(out, (size_t)max, "%s", b->cl[s].sub[i].f);
    return b->cl[s].sub[i].qos;
}
