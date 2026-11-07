// MQTT: the network task on the board (see mq_net.h): the sockets of the client (tfnet, TLS) and of the broker (lwip).
// The task: the app task calls mqn_*; one task "mqtt" (core 0) runs what the app asked last (the client, the broker,
// or nothing) for the life of the open app.
// The memory: the rings and the requests (PSRAM) come at the first start and stay (a task of an earlier open can
// still read them); the task frees its own contexts and the queues of the broker when it ends.
//
// A new request goes into s_want[] (two copies: the app writes the copy that the task does not read) and s_gen
// changes; the task closes its link and starts the new one.
#include "mq_net.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "tfnet/tfnet.h"

#define EVN  48  // the events that wait for the app
#define CMDN 16  // the commands that wait for the task
#define SUBN 16  // the subscriptions of the client

typedef struct {
    uint8_t kind;  // 1 publish, 2 subscribe, 3 unsubscribe
    uint8_t qos;
    bool retain;
    char topic[MQ_TOPIC_MAX + 1];
    int len;
    uint8_t msg[MQN_MSG_MAX];
} cmd_t;

typedef struct {
    int mode;  // 0 nothing, 1 the client, 2 the broker
    mqn_server_t srv;
    char subs[SUBN][MQ_TOPIC_MAX + 1];
    uint8_t qos[SUBN];
    int nsub;
    int port;
    char user[64], pass[64];
} want_t;

static want_t *s_want;  // [2] (PSRAM)
static volatile uint32_t s_gen;
static mqn_ev_t *s_ev;  // [EVN] (PSRAM)
static cmd_t *s_cmd;    // [CMDN] (PSRAM)
static volatile uint32_t s_ev_w, s_ev_r, s_cmd_w, s_cmd_r;
static volatile bool s_quit, s_alive, s_cancel;
static TaskHandle_t s_task;
static TaskHandle_t volatile s_orphan;

// the status and the clients of the broker: the task writes, the app reads (a sequence number around each write)
static volatile uint32_t s_seq;
static mqn_status_t s_st;
static mqn_client_t s_cl[MQ_BRK_CLIENTS];

static int64_t ms(void)
{
    return esp_timer_get_time() / 1000;
}
// (one writer: a plain store; an atomic add would need __atomic_fetch_add_4, a name that the ABI has not got)
static void st_begin(void)
{
    __atomic_store_n(&s_seq, s_seq + 1, __ATOMIC_RELEASE);
}
static void st_end(void)
{
    __atomic_store_n(&s_seq, s_seq + 1, __ATOMIC_RELEASE);
}

void mqn_status(mqn_status_t *o)
{
    for (int i = 0; i < 20; i++) {
        uint32_t a = __atomic_load_n(&s_seq, __ATOMIC_ACQUIRE);
        if (a & 1) {
            vTaskDelay(1);
            continue;
        }
        *o = s_st;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&s_seq, __ATOMIC_ACQUIRE) == a) {
            return;
        }
    }
    *o = s_st;
}

int mqn_clients(mqn_client_t *out, int max)
{
    if (max > MQ_BRK_CLIENTS) {
        max = MQ_BRK_CLIENTS;
    }
    for (int i = 0; i < 20; i++) {
        uint32_t a = __atomic_load_n(&s_seq, __ATOMIC_ACQUIRE);
        if (a & 1) {
            vTaskDelay(1);
            continue;
        }
        memcpy(out, s_cl, sizeof(mqn_client_t) * (size_t)max);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&s_seq, __ATOMIC_ACQUIRE) == a) {
            break;
        }
    }
    return max;
}

// ---------------------------------------------------------------- the rings

static mqn_ev_t *ev_slot(void)  // (the task) the next free event, or NULL (the app is slow: the event goes)
{
    uint32_t w = s_ev_w, r = __atomic_load_n(&s_ev_r, __ATOMIC_ACQUIRE);
    if (w - r >= EVN) {
        return NULL;
    }
    mqn_ev_t *e = &s_ev[w % EVN];
    memset(e, 0, offsetof(mqn_ev_t, msg));
    e->t = ms();
    return e;
}
static void ev_put(void)
{
    __atomic_store_n(&s_ev_w, s_ev_w + 1, __ATOMIC_RELEASE);
}

bool mqn_pop(mqn_ev_t *e)
{
    if (!s_ev) {
        return false;
    }
    uint32_t r = s_ev_r, w = __atomic_load_n(&s_ev_w, __ATOMIC_ACQUIRE);
    if (r == w) {
        return false;
    }
    const mqn_ev_t *s = &s_ev[r % EVN];
    memcpy(e, s, offsetof(mqn_ev_t, msg) + (size_t)(s->len > 0 ? s->len : 0));
    __atomic_store_n(&s_ev_r, r + 1, __ATOMIC_RELEASE);
    return true;
}

static void note(int code)  // (the app gives the text in its language)
{
    mqn_ev_t *e = ev_slot();
    if (!e) {
        return;
    }
    e->kind = MQN_E_NOTE;
    e->a = code;
    ev_put();
}

static bool cmd_put(int kind, const char *t, const void *m, int n, int qos, bool ret)
{
    if (!s_cmd || !s_alive || !t || strlen(t) > MQ_TOPIC_MAX) {
        return false;
    }
    uint32_t w = s_cmd_w, r = __atomic_load_n(&s_cmd_r, __ATOMIC_ACQUIRE);
    if (w - r >= CMDN) {
        return false;
    }
    if (n < 0) {
        n = 0;
    }
    if (n > MQN_MSG_MAX) {
        n = MQN_MSG_MAX;
    }
    cmd_t *c = &s_cmd[w % CMDN];
    c->kind = (uint8_t)kind;
    c->qos = (uint8_t)qos;
    c->retain = ret;
    snprintf(c->topic, sizeof c->topic, "%s", t);
    c->len = n;
    if (n) {
        memcpy(c->msg, m, (size_t)n);
    }
    __atomic_store_n(&s_cmd_w, w + 1, __ATOMIC_RELEASE);
    return true;
}
static cmd_t *cmd_get(void)
{
    uint32_t r = s_cmd_r, w = __atomic_load_n(&s_cmd_w, __ATOMIC_ACQUIRE);
    return r == w ? NULL : &s_cmd[r % CMDN];
}
static void cmd_done(void)
{
    __atomic_store_n(&s_cmd_r, s_cmd_r + 1, __ATOMIC_RELEASE);
}

bool mqn_publish(const char *t, const void *m, int n, int qos, bool ret)
{
    return cmd_put(1, t, m, n, qos, ret);
}
bool mqn_subscribe(const char *f, int qos)
{
    return cmd_put(2, f, NULL, 0, qos, false);
}
bool mqn_unsubscribe(const char *f)
{
    return cmd_put(3, f, NULL, 0, 0, false);
}

// ---------------------------------------------------------------- the client

typedef struct {
    want_t w;
    mq_cli_t *c;
    void *tr;
    uint8_t buf[1024];
} cctx_t;

static bool cli_send(mq_cli_t *c, const void *d, int n)
{
    cctx_t *x = c->user;
    return x->tr && tn_tr_write(x->tr, d, n) == n;
}

static int s_lost_why;
static bool s_refused_hard;
static void cli_event(mq_cli_t *c, int ev, const char *topic, const void *m, int n, int a, int b, int c2)
{
    cctx_t *x = c->user;
    mqn_ev_t *e;
    switch (ev) {
    case MQ_EV_UP:
        ESP_LOGI("mqtt", "client: up, %d subscriptions", x->w.nsub);
        st_begin();
        s_st.state = MQN_UP;
        s_st.t_up = ms();
        s_st.why[0] = 0;
        st_end();
        for (int i = 0; i < x->w.nsub; i++) {
            mq_cli_subscribe(c, x->w.subs[i], x->w.qos[i]);
        }
        break;
    case MQ_EV_REFUSED:
        st_begin();
        snprintf(s_st.why, sizeof s_st.why, "%s", mq_rc_text(a));
        st_end();
        s_refused_hard = a != MQ_RC_UNAVAILABLE;
        break;
    case MQ_EV_LOST:
        s_lost_why = a;
        break;
    case MQ_EV_MSG:
        if (c->n_in <= 3 || c->n_in % 100 == 0) {
            ESP_LOGI("mqtt", "client: message %u (%s, %d B)", (unsigned)c->n_in, topic, n);
        }
        if (!(e = ev_slot())) {
            break;
        }
        e->kind = MQN_E_MSG;
        e->qos = (uint8_t)a;
        e->retain = b;
        snprintf(e->topic, sizeof e->topic, "%s", topic);
        e->full = n;
        e->len = n < MQN_MSG_MAX ? n : MQN_MSG_MAX;
        memcpy(e->msg, m, (size_t)e->len);
        ev_put();
        st_begin();
        s_st.n_in = c->n_in;
        st_end();
        break;
    case MQ_EV_SUBACK:
        ESP_LOGI("mqtt", "client: SUBACK %d code 0x%02x", a, b);
        if (!(e = ev_slot())) {
            break;
        }
        e->kind = MQN_E_SUBACK;
        e->a = b;
        ev_put();
        break;
    case MQ_EV_PONG:
        st_begin();
        s_st.ping_ms = a;
        st_end();
        break;
    default:
        break;
    }
}

static bool changed(uint32_t gen)
{
    return s_quit || s_gen != gen;
}

static void wait_retry(uint32_t gen, int secs)
{
    for (int i = secs * 10; i > 0 && !changed(gen); i--) {
        if (i % 10 == 0) {
            st_begin();
            s_st.retry_s = i / 10;
            st_end();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// a SUBSCRIBE that went out: the list of the link keeps the filter and its QoS (for the next link)
static void cli_sub(cctx_t *x, const cmd_t *c)
{
    if (mq_cli_subscribe(x->c, c->topic, c->qos) <= 0) {
        return;
    }
    int i = 0;
    while (i < x->w.nsub && strcmp(x->w.subs[i], c->topic)) {
        i++;
    }
    if (i == x->w.nsub && i < SUBN) {
        snprintf(x->w.subs[x->w.nsub++], MQ_TOPIC_MAX + 1, "%s", c->topic);
    }
    if (i < SUBN) {
        x->w.qos[i] = c->qos;
    }
}

// an UNSUBSCRIBE: the filter goes from the list of the link
static void cli_unsub(cctx_t *x, const cmd_t *c)
{
    mq_cli_unsubscribe(x->c, c->topic);
    for (int i = 0; i < x->w.nsub; i++) {
        if (!strcmp(x->w.subs[i], c->topic)) {
            x->w.nsub--;
            memmove(x->w.subs[i], x->w.subs[x->w.nsub], MQ_TOPIC_MAX + 1);
            x->w.qos[i] = x->w.qos[x->w.nsub];
            break;
        }
    }
}

// the commands of the app (publish, subscribe, unsubscribe) while the link is up
static void cli_cmds(cctx_t *x)
{
    for (cmd_t *c; x->c->state == MQ_CLI_UP && (c = cmd_get());) {
        if (c->kind == 1) {
            mq_cli_publish(x->c, c->topic, c->msg, c->len, c->qos, c->retain, ms());
        } else if (c->kind == 2) {
            cli_sub(x, c);
        } else if (c->kind == 3) {
            cli_unsub(x, c);
        }
        cmd_done();
    }
}

static void run_client(cctx_t *x, uint32_t gen)
{
    int backoff = 2;
    while (!changed(gen)) {
        st_begin();
        s_st.state = MQN_DIAL;
        s_st.ping_ms = -1;
        st_end();
        tn_url_t u = {0};
        u.tls = x->w.srv.tls;
        u.port = x->w.srv.port;
        snprintf(u.host, sizeof u.host, "%s", x->w.srv.host);
        snprintf(u.path, sizeof u.path, "/");
        int err = 0;
        s_cancel = false;
        ESP_LOGI("mqtt", "client: connect to %s:%d%s", u.host, u.port, u.tls ? " (TLS)" : "");
        x->tr = tn_tr_open(&u, 8000, &s_cancel, &err);
        if (changed(gen)) {
            break;
        }
        if (!x->tr) {
            st_begin();
            s_st.state = MQN_RETRY;
            snprintf(s_st.why, sizeof s_st.why, "%s",
                     err == TN_EDNS    ? "no such broker name (DNS)"
                     : err == TN_ETLS  ? "the TLS link failed"
                     : err == TN_ETIME ? "no answer from the broker"
                                       : "the broker refused the link");
            st_end();
            wait_retry(gen, backoff);
            backoff = backoff < 30 ? backoff * 2 : 30;
            continue;
        }
        mq_connect_t k = {0};
        k.id = x->w.srv.id;
        k.user = x->w.srv.user;
        k.pass = x->w.srv.pass;
        k.clean = true;
        k.keepalive = x->w.srv.keepalive ? x->w.srv.keepalive : 30;
        s_lost_why = 0;
        s_refused_hard = false;
        x->c->user = x;
        x->c->send = cli_send;
        x->c->event = cli_event;
        mq_cli_start(x->c, &k, ms());
        st_begin();
        s_st.state = MQN_WAIT;
        st_end();
        while (!changed(gen) && x->c->state != MQ_CLI_DOWN) {
            int n = tn_tr_read(x->tr, x->buf, sizeof x->buf, 50);
            if (n > 0) {
                mq_cli_in(x->c, x->buf, n, ms());
            } else if (n == 0 || (n < 0 && n != TN_ETIME)) {
                if (x->c->state != MQ_CLI_DOWN) {
                    x->c->state = MQ_CLI_DOWN;
                }
                if (!s_lost_why) {
                    s_lost_why = -1;
                }
                break;
            }
            cli_cmds(x);
            mq_cli_tick(x->c, ms());
            st_begin();
            s_st.n_out = x->c->n_out;
            st_end();
        }
        const bool was_up = x->c->state != MQ_CLI_WAIT && s_st.state == MQN_UP;
        if (changed(gen)) {
            mq_cli_stop(x->c);  // (a DISCONNECT: no will message)
        } else {
            mq_cli_free(x->c);
        }
        tn_tr_close(x->tr);
        x->tr = NULL;
        if (changed(gen)) {
            break;
        }
        st_begin();
        ESP_LOGI("mqtt", "client: the link ended (%s)", s_lost_why > 0 ? mq_lost_text(s_lost_why) : "closed");
        if (s_refused_hard) {
            s_st.state = MQN_FAIL;
        } else {
            s_st.state = MQN_RETRY;
            if (s_lost_why > 0) {
                snprintf(s_st.why, sizeof s_st.why, "%s", mq_lost_text(s_lost_why));
            } else if (!s_st.why[0] || was_up) {
                snprintf(s_st.why, sizeof s_st.why, "the broker closed the link");
            }
        }
        st_end();
        if (s_refused_hard) {  // (a wrong login: no new try until the app asks again)
            while (!changed(gen)) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;
        }
        if (was_up) {
            backoff = 2;
        }
        wait_retry(gen, backoff);
        backoff = backoff < 30 ? backoff * 2 : 30;
    }
}

// ---------------------------------------------------------------- the broker

// Each client has an output queue (PSRAM): the broker never waits for one client. A send goes into the queue, and the
// loop sends from it when the socket has room (select). A client whose queue is full (it does not read: 16 KB wait)
// goes. (A send with a time limit of 300 ms does not work: a 3.49 in WiFi power save misses it in a
// burst of 60 messages and loses its link, and the wait stops all the other clients too.)
#define OUTQ 16384
typedef struct {
    int lfd;
    int fd[MQ_BRK_CLIENTS];
    char addr[MQ_BRK_CLIENTS][20];
    uint8_t *oq[MQ_BRK_CLIENTS];  // [OUTQ] each
    int oqn[MQ_BRK_CLIENTS];
    bool bad[MQ_BRK_CLIENTS];  // the socket failed: the loop closes it
    mq_brk_t *b;
    uint8_t buf[1024];
} bctx_t;

// the bytes of the queue that the socket takes now (no wait); false = the socket failed
static bool flush(bctx_t *x, int s)
{
    while (x->oqn[s] > 0) {
        const int r = send(x->fd[s], x->oq[s], x->oqn[s], MSG_DONTWAIT);
        if (r > 0) {
            memmove(x->oq[s], x->oq[s] + r, (size_t)(x->oqn[s] - r));
            x->oqn[s] -= r;
            continue;
        }
        int so = 0;  // (no room now, or a fault: the socket knows; the ABI has no errno)
        socklen_t sl = sizeof so;
        getsockopt(x->fd[s], SOL_SOCKET, SO_ERROR, &so, &sl);
        return so == 0;
    }
    return true;
}

static bool brk_send(void *u, int slot, const void *d, int n)
{
    bctx_t *x = u;
    if (x->fd[slot] < 0 || x->bad[slot]) {
        return false;
    }
    if (x->oqn[slot] + n > OUTQ) {
        return false;  // (the client does not read: it goes)
    }
    memcpy(x->oq[slot] + x->oqn[slot], d, (size_t)n);
    x->oqn[slot] += n;
    if (!flush(x, slot)) {
        x->bad[slot] = true;  // (the loop closes it: not here, inside a call of the broker)
    }
    return true;
}

static void brk_drop(void *u, int slot)
{
    bctx_t *x = u;
    if (x->fd[slot] >= 0) {
        flush(x, slot);  // (the last packet, a CONNACK with a code, goes out if it can)
        lwip_close(x->fd[slot]);
    }
    x->fd[slot] = -1;
    x->oqn[slot] = 0;
    x->bad[slot] = false;
}

static void brk_event(void *u, int ev, int slot, const char *text, const void *m, int n, int a, int b, int c)
{
    bctx_t *x = u;
    mqn_ev_t *e = ev_slot();
    if (!e) {
        return;
    }
    e->slot = (int8_t)slot;
    switch (ev) {
    case MQ_BEV_CONNECT:
        ESP_LOGI("mqtt", "broker: %s connected (slot %d)", text, slot);
        e->kind = MQN_E_JOIN;
        snprintf(e->topic, sizeof e->topic, "%s", text);
        snprintf(e->text, sizeof e->text, "%s", slot >= 0 ? x->addr[slot] : "");
        break;
    case MQ_BEV_REFUSE:
        e->kind = MQN_E_NOTE;
        e->a = MQN_N_REFUSED;
        e->qos = (uint8_t)a;
        snprintf(e->topic, sizeof e->topic, "%s", text);
        break;
    case MQ_BEV_GONE:
        ESP_LOGI("mqtt", "broker: %s left: %s", text, mq_gone_text(a));
        e->kind = MQN_E_LEAVE;
        snprintf(e->topic, sizeof e->topic, "%s", text);
        snprintf(e->text, sizeof e->text, "%s", mq_gone_text(a));
        break;
    case MQ_BEV_PUBLISH:
        e->kind = MQN_E_SEEN;
        e->qos = (uint8_t)a;
        e->retain = b;
        e->a = c;
        snprintf(e->topic, sizeof e->topic, "%s", text);
        e->full = n;
        e->len = n < MQN_MSG_MAX ? n : MQN_MSG_MAX;
        memcpy(e->msg, m, (size_t)e->len);
        break;
    case MQ_BEV_SUB:
        e->kind = MQN_E_SUB;
        e->a = a;
        snprintf(e->topic, sizeof e->topic, "%s", text);
        break;
    default:
        return;  // (UNSUB: no line)
    }
    ev_put();
}

static void snapshot(bctx_t *x)
{
    st_begin();
    int n = 0;
    for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
        mq_brk_info_t i;
        mq_brk_info(x->b, s, &i);
        mqn_client_t *o = &s_cl[s];
        o->up = i.used && i.up;
        snprintf(o->id, sizeof o->id, "%s", i.id);
        snprintf(o->addr, sizeof o->addr, "%s", x->addr[s]);
        o->nsub = i.nsub;
        o->n_in = i.n_in;
        o->n_out = i.n_out;
        o->t_conn = i.t_conn;
        n += o->up;
    }
    s_st.clients = n;
    s_st.retained = mq_brk_retained(x->b);
    st_end();
}

// the start of the broker: the queues, the broker of the library and the socket that listens.
// false = it cannot start (the state shows why; nothing stays open)
static bool brk_start(bctx_t *x, const want_t *w)
{
    for (int i = 0; i < MQ_BRK_CLIENTS; i++) {
        x->fd[i] = -1;
        x->oqn[i] = 0;
        x->bad[i] = false;
        if (!x->oq[i]) {
            x->oq[i] = heap_caps_malloc(OUTQ, MALLOC_CAP_SPIRAM);
        }
    }
    st_begin();
    memset(s_cl, 0, sizeof s_cl);
    s_st.port = w->port;
    st_end();
    mq_brk_cfg_t cfg = {0};
    cfg.user = x;
    cfg.send = brk_send;
    cfg.drop = brk_drop;
    cfg.event = brk_event;
    cfg.user_name = w->user;
    cfg.password = w->pass;
    bool mem = true;
    for (int i = 0; i < MQ_BRK_CLIENTS; i++) {
        mem &= x->oq[i] != NULL;
    }
    x->b = mem ? mq_brk_new(&cfg) : NULL;
    x->lfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)w->port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    int one = 1;
    if (x->lfd >= 0) {
        setsockopt(x->lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    }
    if (!x->b || x->lfd < 0 || bind(x->lfd, (struct sockaddr *)&a, sizeof a) || listen(x->lfd, 4)) {
        st_begin();
        s_st.state = MQN_FAIL;
        snprintf(s_st.why, sizeof s_st.why, !x->b ? "no memory" : x->lfd < 0 ? "no free socket" : "the port is in use");
        st_end();
        if (x->lfd >= 0) {
            lwip_close(x->lfd);
        }
        mq_brk_free(x->b);
        x->b = NULL;
        return false;
    }
    return true;
}

// a new client at the socket that listens: a slot of the broker, or no room (it goes)
static void brk_accept(bctx_t *x, int64_t now, bool *warned)
{
    const int one = 1;
    struct sockaddr_in pa;
    socklen_t pl = sizeof pa;
    int fd = accept(x->lfd, (struct sockaddr *)&pa, &pl);
    if (fd >= 0) {
        const int s = mq_brk_open(x->b, now);
        if (s < 0) {
            lwip_close(fd);
            note(MQN_N_FULL);
        } else {
            x->fd[s] = fd;
            inet_ntop(AF_INET, &pa.sin_addr, x->addr[s], sizeof x->addr[s]);
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        }
        *warned = false;
    } else if (!*warned) {
        note(MQN_N_NOSOCK);
        *warned = true;
    }
}

// the sockets of the clients that select marked: send from the queue, read into the broker
static void brk_io(bctx_t *x, fd_set *rs, fd_set *ws, int64_t now)
{
    for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
        const int fd = x->fd[s];
        if (fd < 0) {
            continue;
        }
        if (FD_ISSET(fd, ws) && !flush(x, s)) {
            x->bad[s] = true;
        }
        if (!FD_ISSET(fd, rs) || x->bad[s]) {
            continue;
        }
        int n = recv(fd, x->buf, sizeof x->buf, 0);
        if (n > 0) {
            mq_brk_in(x->b, s, x->buf, n, now);
        } else {
            x->bad[s] = true;
        }
    }
}

// the sockets that failed (a read of 0 bytes, an error): the will message of the client goes out
static void brk_close_bad(bctx_t *x)
{
    for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
        if (x->bad[s] && x->fd[s] >= 0) {
            lwip_close(x->fd[s]);
            x->fd[s] = -1;
            x->oqn[s] = 0;
            x->bad[s] = false;
            mq_brk_closed(x->b, s);
        }
    }
}

// one wait of select (50 ms at most) on the socket that listens and on the clients; the result of select
static int brk_select(bctx_t *x, fd_set *rs, fd_set *ws)
{
    FD_ZERO(rs);
    FD_ZERO(ws);
    FD_SET(x->lfd, rs);
    int mx = x->lfd;
    for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
        if (x->fd[s] >= 0) {
            FD_SET(x->fd[s], rs);
            if (x->oqn[s] > 0) {
                FD_SET(x->fd[s], ws);
            }
            if (x->fd[s] > mx) {
                mx = x->fd[s];
            }
        }
    }
    struct timeval tv = {0, 50000};
    return lwip_select(mx + 1, rs, ws, NULL, &tv);
}

static void run_broker(bctx_t *x, uint32_t gen, const want_t *w)
{
    if (!brk_start(x, w)) {
        while (!changed(gen)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        return;
    }
    st_begin();
    s_st.state = MQN_LISTEN;
    s_st.t_up = ms();
    s_st.why[0] = 0;
    st_end();
    ESP_LOGI("mqtt", "broker: listens on port %d%s", w->port, w->user[0] ? " (login)" : "");
    int64_t t_tick = 0, t_snap = 0;
    bool warned = false;
    while (!changed(gen)) {
        fd_set rs, ws;
        int r = brk_select(x, &rs, &ws);
        const int64_t now = ms();
        if (r > 0 && FD_ISSET(x->lfd, &rs)) {
            brk_accept(x, now, &warned);
        }
        if (r > 0) {
            brk_io(x, &rs, &ws, now);
        }
        brk_close_bad(x);
        for (cmd_t *c; (c = cmd_get());) {
            if (c->kind == 1) {
                mq_brk_publish(x->b, c->topic, c->msg, c->len, c->qos, c->retain);
            }
            cmd_done();
        }
        if (now - t_tick > 500) {
            t_tick = now;
            mq_brk_tick(x->b, now);
        }
        if (now - t_snap > 250) {
            t_snap = now;
            snapshot(x);
        }
    }
    for (int s = 0; s < MQ_BRK_CLIENTS; s++) {
        if (x->fd[s] >= 0) {
            lwip_close(x->fd[s]);
            x->fd[s] = -1;
        }
    }
    lwip_close(x->lfd);
    mq_brk_free(x->b);
    x->b = NULL;
    st_begin();
    memset(s_cl, 0, sizeof s_cl);
    s_st.clients = s_st.retained = 0;
    st_end();
}

// ---------------------------------------------------------------- the task

static void net_task(void *arg)
{
    cctx_t *cx = heap_caps_calloc(1, sizeof *cx, MALLOC_CAP_SPIRAM);
    bctx_t *bx = heap_caps_calloc(1, sizeof *bx, MALLOC_CAP_SPIRAM);
    mq_cli_t *cli = heap_caps_calloc(1, sizeof *cli, MALLOC_CAP_SPIRAM);
    uint32_t gen = s_gen - 1;
    while (!s_quit && cx && bx && cli) {
        if (s_gen == gen) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        gen = __atomic_load_n(&s_gen, __ATOMIC_ACQUIRE);
        cx->w = s_want[gen & 1];
        cx->c = cli;
        while (cmd_get()) {
            cmd_done();  // (the commands for the link that ended go)
        }
        st_begin();
        memset(&s_st, 0, sizeof s_st);
        s_st.ping_ms = -1;
        s_st.state = MQN_OFF;
        st_end();
        if (cx->w.mode == 1) {
            run_client(cx, gen);
        } else if (cx->w.mode == 2) {
            run_broker(bx, gen, &cx->w);
        }
    }
    memset(cx, 0, sizeof *cx);  // (the password)
    heap_caps_free(cx);
    for (int i = 0; bx && i < MQ_BRK_CLIENTS; i++) {
        heap_caps_free(bx->oq[i]);
    }
    heap_caps_free(bx);
    heap_caps_free(cli);
    s_alive = false;
    for (;;) {  // (mqn_stop deletes the task; a late task deletes itself)
        if (s_orphan) {
            TaskHandle_t me = s_orphan;
            s_orphan = NULL;
            vTaskDeleteWithCaps(me);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static bool task_up(void)
{
    if (!s_want) {
        s_want = heap_caps_calloc(2, sizeof(want_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_ev) {
        s_ev = heap_caps_calloc(EVN, sizeof(mqn_ev_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_cmd) {
        s_cmd = heap_caps_calloc(CMDN, sizeof(cmd_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_want || !s_ev || !s_cmd) {
        return false;
    }
    if (s_task) {
        return true;
    }
    if (s_alive) {
        return false;  // (the task of an earlier open still ends: it shares the rings)
    }
    s_quit = false;
    s_alive = true;
    s_ev_w = s_ev_r = s_cmd_w = s_cmd_r = 0;
    if (xTaskCreatePinnedToCoreWithCaps(net_task, "mqtt", 8192, NULL, 3, &s_task, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
        s_alive = false;
        return false;
    }
    return true;
}

static bool ask(const want_t *w)
{
    if (!task_up()) {
        return false;
    }
    const uint32_t next = s_gen + 1;
    s_want[next & 1] = *w;
    s_cancel = true;  // (a connect that waits stops)
    __atomic_store_n(&s_gen, next, __ATOMIC_RELEASE);
    return true;
}

bool mqn_client_start(const mqn_server_t *s, const char *const *subs, const uint8_t *qos, int nsub)
{
    static want_t w;
    memset(&w, 0, sizeof w);
    w.mode = 1;
    w.srv = *s;
    for (int i = 0; i < nsub && i < SUBN; i++) {
        snprintf(w.subs[i], sizeof w.subs[i], "%s", subs[i]);
        w.qos[i] = qos ? qos[i] : 0;
        w.nsub++;
    }
    const bool ok = ask(&w);
    memset(&w, 0, sizeof w);
    return ok;
}

bool mqn_broker_start(int port, const char *user, const char *pass)
{
    static want_t w;
    memset(&w, 0, sizeof w);
    w.mode = 2;
    w.port = port;
    snprintf(w.user, sizeof w.user, "%s", user ? user : "");
    snprintf(w.pass, sizeof w.pass, "%s", pass ? pass : "");
    const bool ok = ask(&w);
    memset(&w, 0, sizeof w);
    return ok;
}

void mqn_stop(void)
{
    if (!s_task) {
        return;
    }
    s_quit = true;
    s_cancel = true;
    for (int i = 0; i < 100 && s_alive; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!s_alive) {
        vTaskDeleteWithCaps(s_task);
    } else {
        s_orphan = s_task;  // (a TLS connect that waits: the task deletes itself at its end)
    }
    s_task = NULL;
    if (s_want) {
        memset(s_want, 0, 2 * sizeof(want_t));
    }
}

#else  // the PC (the host fuzz, the simulator): no network; the PC tests of MQTT give their own

bool mqn_client_start(const mqn_server_t *s, const char *const *subs, const uint8_t *qos, int nsub)
{
    (void)s;
    (void)subs;
    (void)qos;
    (void)nsub;
    return true;
}
bool mqn_broker_start(int port, const char *user, const char *pass)
{
    (void)port;
    (void)user;
    (void)pass;
    return true;
}
void mqn_stop(void)
{
}
bool mqn_publish(const char *t, const void *m, int n, int qos, bool ret)
{
    (void)t;
    (void)m;
    (void)n;
    (void)qos;
    (void)ret;
    return false;
}
bool mqn_subscribe(const char *f, int qos)
{
    (void)f;
    (void)qos;
    return false;
}
bool mqn_unsubscribe(const char *f)
{
    (void)f;
    return false;
}
bool mqn_pop(mqn_ev_t *e)
{
    (void)e;
    return false;
}
void mqn_status(mqn_status_t *o)
{
    memset(o, 0, sizeof *o);
    o->ping_ms = -1;
}
int mqn_clients(mqn_client_t *out, int max)
{
    memset(out, 0, sizeof(*out) * (size_t)max);
    return max;
}

#endif
