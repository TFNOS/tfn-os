// tfmqtt: the packets, the topics and the client of MQTT 3.1.1 (see tfmqtt.h). The broker is in mq_broker.c.
#include "tfmqtt.h"
#include <stdlib.h>
#include <string.h>

const char *mq_rc_text(int rc)
{
    switch (rc) {
    case MQ_RC_OK:
        return "accepted";
    case MQ_RC_VERSION:
        return "the broker does not know this MQTT version";
    case MQ_RC_ID:
        return "the broker refused the client id";
    case MQ_RC_UNAVAILABLE:
        return "the broker is not available";
    case MQ_RC_LOGIN:
        return "wrong user name or password";
    case MQ_RC_AUTH:
        return "not authorized";
    default:
        return "refused";
    }
}

const char *mq_type_name(int t)
{
    static const char *const N[] = {"?",       "CONNECT",  "CONNACK",    "PUBLISH", "PUBACK",      "PUBREC",
                                    "PUBREL",  "PUBCOMP",  "SUBSCRIBE",  "SUBACK",  "UNSUBSCRIBE", "UNSUBACK",
                                    "PINGREQ", "PINGRESP", "DISCONNECT", "?"};
    return N[t & 15];
}

const char *mq_lost_text(int why)
{
    switch (why) {
    case MQ_LOST_PROTO:
        return "the broker sent a bad packet";
    case MQ_LOST_PING:
        return "no answer to the ping";
    case MQ_LOST_BIG:
        return "a packet is too long";
    case MQ_LOST_TIME:
        return "no answer to the connect";
    default:
        return "the link is lost";
    }
}

// ================================================================ the stream framer

static void rx_pop(mq_rx_t *r);

void mq_rx_init(mq_rx_t *r)
{
    r->n = r->used = 0;
    r->bad = false;
}

int mq_rx_feed(mq_rx_t *r, const void *d, int n)
{
    rx_pop(r);
    if (r->bad || n <= 0) {
        return n > 0 ? n : 0;
    }
    int room = (int)sizeof r->buf - r->n;
    if (n > room) {
        n = room;
    }
    memcpy(r->buf + r->n, d, (size_t)n);
    r->n += n;
    return n;
}

// the remaining length at b[1..]: the length, *hl = the bytes of the fixed header; 0 = not all bytes yet; -1 = bad
static int rem_len(const uint8_t *b, int n, int *len, int *hl)
{
    int v = 0, mul = 1;
    for (int i = 1; i <= 4; i++) {
        if (i >= n) {
            return 0;
        }
        v += (b[i] & 127) * mul;
        mul *= 128;
        if (!(b[i] & 128)) {
            *len = v;
            *hl = i + 1;
            return 1;
        }
    }
    return -1;
}

// the packet that mq_rx_next gave last goes out of the buffer
static void rx_pop(mq_rx_t *r)
{
    if (!r->used) {
        return;
    }
    memmove(r->buf, r->buf + r->used, (size_t)(r->n - r->used));
    r->n -= r->used;
    r->used = 0;
}

bool mq_rx_next(mq_rx_t *r, mq_pkt_t *p)
{
    rx_pop(r);
    if (r->bad || r->n <= 0) {
        return false;
    }
    int len, hl;
    const int k = rem_len(r->buf, r->n, &len, &hl);
    if (k < 0 || (k > 0 && hl + len > (int)sizeof r->buf)) {
        r->bad = true;
        return false;
    }
    if (k == 0 || r->n < hl + len) {
        return false;
    }
    p->type = r->buf[0] >> 4;
    p->flags = r->buf[0] & 15;
    p->len = len;
    p->body = r->buf + hl;
    r->used = hl + len;
    return true;
}

// ================================================================ the encoders

typedef struct {
    uint8_t *o;
    int max, n;
    bool full;
} wr_t;
static void w8(wr_t *w, int v)
{
    if (w->n >= w->max) {
        w->full = true;
        return;
    }
    w->o[w->n++] = (uint8_t)v;
}
static void w16(wr_t *w, int v)
{
    w8(w, v >> 8);
    w8(w, v & 255);
}
static void wbytes(wr_t *w, const void *d, int n)
{
    if (n <= 0) {
        return;
    }
    if (w->n + n > w->max) {
        w->full = true;
        return;
    }
    memcpy(w->o + w->n, d, (size_t)n);
    w->n += n;
}
static void wstr(wr_t *w, const char *s)
{
    const int n = s ? (int)strlen(s) : 0;
    w16(w, n);
    wbytes(w, s, n);
}
static int len_bytes(int len)
{
    return len < 128 ? 1 : len < 16384 ? 2 : len < 2097152 ? 3 : 4;
}
// the fixed header for a body of len bytes
static void whead(wr_t *w, int b0, int len)
{
    w8(w, b0);
    do {
        int d = len & 127;
        len >>= 7;
        w8(w, d | (len ? 128 : 0));
    } while (len);
}
static int wdone(wr_t *w)
{
    return w->full ? 0 : w->n;
}
static bool has(const char *s)
{
    return s && *s;
}

int mq_enc_connect(uint8_t *o, int max, const mq_connect_t *c)
{
    const bool will = c->will_topic && *c->will_topic;
    int len = 10 + 2 + (int)strlen(c->id ? c->id : "");
    if (will) {
        len += 2 + (int)strlen(c->will_topic) + 2 + c->will_len;
    }
    if (has(c->user)) {
        len += 2 + (int)strlen(c->user);
    }
    if (has(c->user) && has(c->pass)) {
        len += 2 + (int)strlen(c->pass);  // (3.1.1: no password without a user)
    }
    wr_t w = {o, max, 0, false};
    whead(&w, MQ_CONNECT << 4, len);
    wstr(&w, "MQTT");
    w8(&w, 4);
    int fl = c->clean ? 0x02 : 0;
    if (will) {
        fl |= 0x04 | ((c->will_qos & 3) << 3) | (c->will_retain ? 0x20 : 0);
    }
    if (has(c->user)) {
        fl |= 0x80;
    }
    if (has(c->user) && has(c->pass)) {
        fl |= 0x40;
    }
    w8(&w, fl);
    w16(&w, c->keepalive);
    wstr(&w, c->id ? c->id : "");
    if (will) {
        wstr(&w, c->will_topic);
        w16(&w, c->will_len);
        wbytes(&w, c->will_msg, c->will_len);
    }
    if (has(c->user)) {
        wstr(&w, c->user);
    }
    if (has(c->user) && has(c->pass)) {
        wstr(&w, c->pass);
    }
    return wdone(&w);
}

int mq_enc_connack(uint8_t *o, int max, bool session, int rc)
{
    wr_t w = {o, max, 0, false};
    whead(&w, MQ_CONNACK << 4, 2);
    w8(&w, session ? 1 : 0);
    w8(&w, rc);
    return wdone(&w);
}

int mq_enc_publish(uint8_t *o, int max, const char *topic, const void *msg, int len, int qos, bool retain, bool dup,
                   uint16_t id)
{
    if (len < 0) {
        len = 0;
    }
    const int body = 2 + (int)strlen(topic) + (qos ? 2 : 0) + len;
    if (1 + len_bytes(body) + body > max) {
        return 0;
    }
    wr_t w = {o, max, 0, false};
    whead(&w, (MQ_PUBLISH << 4) | (dup ? 8 : 0) | ((qos & 3) << 1) | (retain ? 1 : 0), body);
    wstr(&w, topic);
    if (qos) {
        w16(&w, id);
    }
    wbytes(&w, msg, len);
    return wdone(&w);
}

int mq_enc_ack(uint8_t *o, int max, int type, uint16_t id)
{
    wr_t w = {o, max, 0, false};
    whead(&w, (type << 4) | (type == MQ_PUBREL ? 2 : 0), 2);
    w16(&w, id);
    return wdone(&w);
}

int mq_enc_subscribe(uint8_t *o, int max, uint16_t id, const char *const *f, const uint8_t *qos, int n)
{
    int len = 2;
    for (int i = 0; i < n; i++) {
        len += 2 + (int)strlen(f[i]) + 1;
    }
    wr_t w = {o, max, 0, false};
    whead(&w, (MQ_SUBSCRIBE << 4) | 2, len);
    w16(&w, id);
    for (int i = 0; i < n; i++) {
        wstr(&w, f[i]);
        w8(&w, qos[i] & 3);
    }
    return wdone(&w);
}

int mq_enc_suback(uint8_t *o, int max, uint16_t id, const uint8_t *codes, int n)
{
    wr_t w = {o, max, 0, false};
    whead(&w, MQ_SUBACK << 4, 2 + n);
    w16(&w, id);
    wbytes(&w, codes, n);
    return wdone(&w);
}

int mq_enc_unsubscribe(uint8_t *o, int max, uint16_t id, const char *const *f, int n)
{
    int len = 2;
    for (int i = 0; i < n; i++) {
        len += 2 + (int)strlen(f[i]);
    }
    wr_t w = {o, max, 0, false};
    whead(&w, (MQ_UNSUBSCRIBE << 4) | 2, len);
    w16(&w, id);
    for (int i = 0; i < n; i++) {
        wstr(&w, f[i]);
    }
    return wdone(&w);
}

int mq_enc_simple(uint8_t *o, int max, int type)
{
    wr_t w = {o, max, 0, false};
    whead(&w, type << 4, 0);
    return wdone(&w);
}

// ================================================================ the topics

bool mq_topic_ok(const char *t)
{
    const size_t n = t ? strlen(t) : 0;
    if (n == 0 || n > MQ_TOPIC_MAX) {
        return false;
    }
    return !strpbrk(t, "+#");
}

bool mq_filter_ok(const char *f)
{
    const size_t n = f ? strlen(f) : 0;
    if (n == 0 || n > MQ_TOPIC_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        const bool start = i == 0 || f[i - 1] == '/';
        const bool end = i + 1 == n || f[i + 1] == '/';
        if (f[i] == '+' && !(start && end)) {
            return false;
        }
        if (f[i] == '#' && !(start && i + 1 == n)) {
            return false;
        }
    }
    return true;
}

bool mq_match(const char *f, const char *t)
{
    if (t[0] == '$' && (f[0] == '+' || f[0] == '#')) {
        return false;
    }
    for (;;) {
        if (f[0] == '#') {
            return true;
        }
        const char *fe = strchr(f, '/'), *te = strchr(t, '/');  // the end of this level
        if (!fe) {
            fe = f + strlen(f);
        }
        if (!te) {
            te = t + strlen(t);
        }
        const bool plus = fe - f == 1 && f[0] == '+';
        if (!plus && (fe - f != te - t || memcmp(f, t, (size_t)(fe - f)))) {
            return false;
        }
        if (*fe == 0) {
            return *te == 0;
        }
        if (*te == 0) {
            return strcmp(fe, "/#") == 0;  // ("a/#" also matches "a")
        }
        f = fe + 1;
        t = te + 1;
    }
}

bool mq_get_u16(const mq_pkt_t *p, int *at, uint16_t *v)
{
    if (*at + 2 > p->len) {
        return false;
    }
    *v = (uint16_t)(p->body[*at] << 8 | p->body[*at + 1]);
    *at += 2;
    return true;
}

bool mq_get_str(const mq_pkt_t *p, int *at, char *out, int max)
{
    uint16_t n;
    if (!mq_get_u16(p, at, &n)) {
        return false;
    }
    if (*at + n > p->len || n >= max) {
        return false;
    }
    memcpy(out, p->body + *at, n);
    out[n] = 0;
    if (memchr(out, 0, n)) {
        return false;  // (a 0 in an MQTT string is not allowed)
    }
    *at += n;
    return true;
}

// ================================================================ the client

void mq_cli_init(mq_cli_t *c)
{
    void *u = c->user;
    bool (*s)(mq_cli_t *, const void *, int) = c->send;
    void (*e)(mq_cli_t *, int, const char *, const void *, int, int, int, int) = c->event;
    memset(c, 0, sizeof *c);
    c->user = u;
    c->send = s;
    c->event = e;
    c->next_id = 1;
    mq_rx_init(&c->rx);
}

static void ev(mq_cli_t *c, int e, const char *t, const void *m, int n, int a, int b, int c2)
{
    if (c->event) {
        c->event(c, e, t, m, n, a, b, c2);
    }
}

static bool tx(mq_cli_t *c, const void *d, int n, int64_t now)
{
    if (n <= 0 || c->state == MQ_CLI_DOWN || !c->send) {
        return false;
    }
    if (!c->send(c, d, n)) {
        c->state = MQ_CLI_DOWN;
        ev(c, MQ_EV_LOST, NULL, NULL, 0, 0, 0, 0);
        return false;
    }
    c->t_tx = now;
    return true;
}

static void lost(mq_cli_t *c, int why)
{
    if (c->state == MQ_CLI_DOWN) {
        return;
    }
    c->state = MQ_CLI_DOWN;
    ev(c, MQ_EV_LOST, NULL, NULL, 0, why, 0, 0);
}

static uint16_t new_id(mq_cli_t *c)
{
    uint16_t id = c->next_id++;
    if (!c->next_id) {
        c->next_id = 1;
    }
    return id ? id : new_id(c);
}

bool mq_cli_start(mq_cli_t *c, const mq_connect_t *k, int64_t now)
{
    mq_cli_free(c);
    mq_cli_init(c);
    c->keepalive = k->keepalive;
    const int n = mq_enc_connect(c->tx, sizeof c->tx, k);
    if (n <= 0) {
        return false;
    }
    c->state = MQ_CLI_WAIT;
    c->t_conn = c->t_rx = now;
    return tx(c, c->tx, n, now);
}

static void got_publish(mq_cli_t *c, const mq_pkt_t *p, int64_t now)
{
    const int qos = (p->flags >> 1) & 3;
    int at = 0;
    char topic[MQ_TOPIC_MAX + 1];
    uint16_t id = 0;
    if (qos == 3 || !mq_get_str(p, &at, topic, sizeof topic) || (qos && !mq_get_u16(p, &at, &id))) {
        lost(c, MQ_LOST_PROTO);
        return;
    }
    uint8_t a[4];
    if (qos == 2) {
        // QoS 2: the message once; the copy of an id that waits for its PUBREL is a resend
        bool seen = false;
        int fr = -1;
        for (int i = 0; i < MQ_CLI_REC; i++) {
            if (c->rec[i] == id) {
                seen = true;
            }
            if (!c->rec[i] && fr < 0) {
                fr = i;
            }
        }
        if (!seen && fr >= 0) {
            c->rec[fr] = id;
        }
        if (!seen) {
            c->n_in++;
            ev(c, MQ_EV_MSG, topic, p->body + at, p->len - at, qos, p->flags & 1, (p->flags >> 3) & 1);
        }
        tx(c, a, mq_enc_ack(a, sizeof a, MQ_PUBREC, id), now);
        return;
    }
    c->n_in++;
    ev(c, MQ_EV_MSG, topic, p->body + at, p->len - at, qos, p->flags & 1, (p->flags >> 3) & 1);
    if (qos == 1) {
        tx(c, a, mq_enc_ack(a, sizeof a, MQ_PUBACK, id), now);
    }
}

static void got(mq_cli_t *c, const mq_pkt_t *p, int64_t now)
{
    int at = 0;
    uint16_t id = 0;
    uint8_t a[4];
    if (c->state == MQ_CLI_WAIT && p->type != MQ_CONNACK) {
        lost(c, MQ_LOST_PROTO);
        return;
    }
    switch (p->type) {
    case MQ_CONNACK:
        if (c->state != MQ_CLI_WAIT || p->len != 2) {
            lost(c, MQ_LOST_PROTO);
            return;
        }
        if (p->body[1] != MQ_RC_OK) {
            c->state = MQ_CLI_DOWN;
            ev(c, MQ_EV_REFUSED, NULL, NULL, 0, p->body[1], 0, 0);
            return;
        }
        c->state = MQ_CLI_UP;
        ev(c, MQ_EV_UP, NULL, NULL, 0, p->body[0] & 1, 0, 0);
        return;
    case MQ_PUBLISH:
        got_publish(c, p, now);
        return;
    case MQ_PUBACK:
        if (!mq_get_u16(p, &at, &id)) {
            lost(c, MQ_LOST_PROTO);
            return;
        }
        for (int i = 0; i < MQ_CLI_INFLIGHT; i++) {
            if (c->fly[i].pkt && c->fly[i].id == id) {
                free(c->fly[i].pkt);
                c->fly[i].pkt = NULL;
                ev(c, MQ_EV_PUBDONE, NULL, NULL, 0, id, 0, 0);
            }
        }
        return;
    case MQ_PUBREL:
        if (!mq_get_u16(p, &at, &id)) {
            lost(c, MQ_LOST_PROTO);
            return;
        }
        for (int i = 0; i < MQ_CLI_REC; i++) {
            if (c->rec[i] == id) {
                c->rec[i] = 0;
            }
        }
        tx(c, a, mq_enc_ack(a, sizeof a, MQ_PUBCOMP, id), now);
        return;
    case MQ_SUBACK:
        if (!mq_get_u16(p, &at, &id) || p->len < 3) {
            lost(c, MQ_LOST_PROTO);
            return;
        }
        ev(c, MQ_EV_SUBACK, NULL, NULL, 0, id, p->body[2], 0);
        return;
    case MQ_UNSUBACK:
        if (!mq_get_u16(p, &at, &id)) {
            lost(c, MQ_LOST_PROTO);
            return;
        }
        ev(c, MQ_EV_UNSUBACK, NULL, NULL, 0, id, 0, 0);
        return;
    case MQ_PINGRESP:
        if (c->t_ping) {
            ev(c, MQ_EV_PONG, NULL, NULL, 0, (int)(now - c->t_ping), 0, 0);
        }
        c->t_ping = 0;
        return;
    case MQ_PUBREC:  // (the client sends QoS 0 and 1 only: an answer for QoS 2 is a fault)
    case MQ_PUBCOMP:
    default:
        lost(c, MQ_LOST_PROTO);
        return;
    }
}

void mq_cli_in(mq_cli_t *c, const void *d, int n, int64_t now)
{
    const uint8_t *b = d;
    c->t_rx = now;
    while (n > 0 && c->state != MQ_CLI_DOWN) {
        int k = mq_rx_feed(&c->rx, b, n);
        b += k;
        n -= k;
        mq_pkt_t p;
        while (c->state != MQ_CLI_DOWN && mq_rx_next(&c->rx, &p)) {
            got(c, &p, now);
        }
        if (c->rx.bad) {
            lost(c, MQ_LOST_BIG);
            return;
        }
        if (k == 0 && n > 0) {
            lost(c, MQ_LOST_BIG);
            return;
        }
    }
}

void mq_cli_tick(mq_cli_t *c, int64_t now)
{
    if (c->state == MQ_CLI_WAIT && now - c->t_conn > 10000) {
        lost(c, MQ_LOST_TIME);
        return;
    }
    if (c->state != MQ_CLI_UP) {
        return;
    }
    const int64_t ka = (int64_t)c->keepalive * 1000;
    if (ka) {
        if (c->t_ping && now - c->t_ping > (ka < 10000 ? 10000 : ka)) {
            lost(c, MQ_LOST_PING);
            return;
        }
        if (!c->t_ping && now - c->t_tx >= ka * 3 / 4) {
            uint8_t a[2];
            if (tx(c, a, mq_enc_simple(a, sizeof a, MQ_PINGREQ), now)) {
                c->t_ping = now;
            }
        }
    }
    for (int i = 0; i < MQ_CLI_INFLIGHT; i++) {  // QoS 1: send again after 10 s, with DUP
        if (c->fly[i].pkt && now - c->fly[i].t > 10000) {
            c->fly[i].pkt[0] |= 8;
            c->fly[i].t = now;
            if (!tx(c, c->fly[i].pkt, c->fly[i].n, now)) {
                return;
            }
        }
    }
}

int mq_cli_publish(mq_cli_t *c, const char *topic, const void *msg, int len, int qos, bool retain, int64_t now)
{
    if (!mq_topic_ok(topic)) {
        return -1;
    }
    if (c->state != MQ_CLI_UP) {
        return -4;
    }
    if (qos > 1) {
        qos = 1;
    }
    int slot = -1;
    if (qos == 1) {
        for (int i = 0; i < MQ_CLI_INFLIGHT && slot < 0; i++) {
            if (!c->fly[i].pkt) {
                slot = i;
            }
        }
        if (slot < 0) {
            return -3;
        }
    }
    const uint16_t id = qos ? new_id(c) : 0;
    const int n = mq_enc_publish(c->tx, sizeof c->tx, topic, msg, len, qos, retain, false, id);
    if (n <= 0) {
        return -2;
    }
    if (qos == 1) {
        c->fly[slot].pkt = malloc((size_t)n);
        if (!c->fly[slot].pkt) {
            return -3;
        }
        memcpy(c->fly[slot].pkt, c->tx, (size_t)n);
        c->fly[slot].n = n;
        c->fly[slot].id = id;
        c->fly[slot].t = now;
    }
    if (!tx(c, c->tx, n, now)) {
        return -4;
    }
    c->n_out++;
    return qos ? id : 0;
}

int mq_cli_subscribe(mq_cli_t *c, const char *filter, int qos)
{
    if (!mq_filter_ok(filter)) {
        return -1;
    }
    if (c->state != MQ_CLI_UP) {
        return -4;
    }
    const uint16_t id = new_id(c);
    const uint8_t q = (uint8_t)(qos < 0 ? 0 : qos > 2 ? 2 : qos);
    const int n = mq_enc_subscribe(c->tx, sizeof c->tx, id, &filter, &q, 1);
    return n > 0 && tx(c, c->tx, n, c->t_tx) ? id : -4;
}

int mq_cli_unsubscribe(mq_cli_t *c, const char *filter)
{
    if (!mq_filter_ok(filter)) {
        return -1;
    }
    if (c->state != MQ_CLI_UP) {
        return -4;
    }
    const uint16_t id = new_id(c);
    const int n = mq_enc_unsubscribe(c->tx, sizeof c->tx, id, &filter, 1);
    return n > 0 && tx(c, c->tx, n, c->t_tx) ? id : -4;
}

void mq_cli_free(mq_cli_t *c)
{
    for (int i = 0; i < MQ_CLI_INFLIGHT; i++) {
        free(c->fly[i].pkt);
        c->fly[i].pkt = NULL;
    }
}

void mq_cli_stop(mq_cli_t *c)
{
    if (c->state == MQ_CLI_UP || c->state == MQ_CLI_WAIT) {
        uint8_t a[2];
        const int n = mq_enc_simple(a, sizeof a, MQ_DISCONNECT);
        if (c->send) {
            c->send(c, a, n);
        }
    }
    c->state = MQ_CLI_IDLE;
    mq_cli_free(c);
}
