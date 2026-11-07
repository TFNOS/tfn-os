// MQTT: an MQTT 3.1.1 client and broker for the home network: the screens, the settings and the topic table.
// The task: the app task of TFN OS calls the hooks of app_mqtt; the network runs in the task of mq_net.c.
// The memory: the file statics; the topic table and the stream (PSRAM) come at the first enter and stay for the
// next open; the account stays in the secret store of the app.
//
// THE MODES:
//   CLIENT  connect to a broker (a public test broker, or an address from the keyboard or the board page /app/mqtt),
//           subscribe to topic filters, see each topic with its last value (TOPICS) or each message (STREAM), open a
//           topic (its whole message: text, JSON with indents, or hex; a chart of the number values), PUBLISH
//   BROKER  this board is the broker (port 1883): the clients of the network connect to it; the screen shows its
//           address, the clients, and the traffic (the app can also publish into it)
// THE LAYERS (each one knows only the layer under it):
//   libs/tfmqtt  the packets, the topic rules, the client and the broker as state machines; pure C (the PC tests)
//   mq_net.c     one task of the app: the sockets (tfnet for the client and TLS, lwip for the broker), two rings
//   this app     the screens (mq_draw.inc), the settings, the topic table
// The account (the address, the port, TLS, the user name, the password, the topics) is in the secret store of the app:
// the board page /app/mqtt (main/tfn_web.c) and the keyboard of the app write the same values.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/watch.h"
#include "tfn/uikit.h"
#include "tfn/watch.h"
#include "tfn/wifi.h"
#include "tfdraw/tfdraw.h"
#include "mq_net.h"

static bool s_he;  // the language: false English, true Hebrew
#include "mq_text.inc"
#include "mq_fonts.inc"

#define BAR BOARD_BAR_SCREEN
static int64_t s_now;  // ms
static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000;
}

// ================================================================ the brokers
typedef struct {
    const char *host;
    const char *subs;  // the subscriptions until the user changes them
} preset_t;
static const preset_t PRE[] = {
        {"broker.hivemq.com", "tfn/#"},  // (tfn/#: the boards with this app see the messages of each other)
        {"broker.emqx.io", "tfn/#"},
        {"test.mosquitto.org", "$SYS/broker/#"},  // (the broker gives its own numbers; some networks block it)
};
#define NPRE     ((int)(sizeof PRE / sizeof PRE[0]))
#define SRV_PAGE NPRE    // the address of the board page or of the keyboard
static int s_srv = -1;   // the broker in use (-1 = none chosen)
static int s_mode = -1;  // 0 CLIENT, 1 BROKER (-1 = the first open: the choice)
static bool s_blogin;    // the broker of the board asks for the user name and password
static char s_cid[24];   // the client id: tfn-xxxxxx

// the account of the board page (the secret store)
typedef struct {
    char host[96], user[64], pass[64];
    int port;
    bool tls;
} acct_t;
static acct_t s_acct;
static int64_t s_acct_t;

static void acct_read(acct_t *a)
{
    memset(a, 0, sizeof *a);
    char v[16];
    tfn_secret_get("host", a->host, sizeof a->host);
    tfn_secret_get("user", a->user, sizeof a->user);
    tfn_secret_get("pass", a->pass, sizeof a->pass);
    a->port = tfn_secret_get("port", v, sizeof v) > 0 ? atoi(v) : 0;
    a->tls = tfn_secret_get("tls", v, sizeof v) > 0 && !strcmp(v, "YES");
    if (a->port <= 0 || a->port > 65535) {
        a->port = a->tls ? 8883 : 1883;
    }
    for (char *p = a->host; *p; p++) {  // (a pasted "mqtt://host:1883" or a space at the end)
        if (*p == ' ') {
            *p = 0;
        }
    }
    if (!strncmp(a->host, "mqtt://", 7)) {
        memmove(a->host, a->host + 7, strlen(a->host + 7) + 1);
    }
    if (!strncmp(a->host, "mqtts://", 8)) {
        memmove(a->host, a->host + 8, strlen(a->host + 8) + 1);
        a->tls = true;
    }
    char *c = strchr(a->host, ':');
    if (c) {
        if (atoi(c + 1) > 0) {
            a->port = atoi(c + 1);
        }
        *c = 0;
    }
}

// ================================================================ the subscriptions (a comma list in the secret store)
#define SUBN 16
static char s_subs[SUBN][MQ_TOPIC_MAX + 1];
static int s_nsub;
static char s_page_topics[420];  // the "topics" of the board page that the list has now
static const char *subs_key(int srv)
{
    static char k[16];
    if (srv >= SRV_PAGE) {
        return "topics";
    }
    snprintf(k, sizeof k, "t%d", srv);
    return k;
}

static void subs_load(void)
{
    char v[420] = "";
    s_nsub = 0;
    s_page_topics[0] = 0;
    tfn_secret_get("topics", s_page_topics, sizeof s_page_topics);
    if (s_srv < 0) {
        return;
    }
    if (tfn_secret_get(subs_key(s_srv), v, sizeof v) <= 0) {
        snprintf(v, sizeof v, "%s", s_srv < NPRE ? PRE[s_srv].subs : "#");
    }
    for (char *p = v, *e; p && *p && s_nsub < SUBN; p = e ? e + 1 : NULL) {
        e = strchr(p, ',');
        if (e) {
            *e = 0;
        }
        while (*p == ' ') {
            p++;
        }
        char *z = p + strlen(p);
        while (z > p && z[-1] == ' ') {
            *--z = 0;
        }
        if (mq_filter_ok(p)) {
            snprintf(s_subs[s_nsub++], MQ_TOPIC_MAX + 1, "%s", p);
        }
    }
}

static void subs_save(void)
{
    char v[420];
    int k = 0;
    v[0] = 0;
    for (int i = 0; i < s_nsub && k < (int)sizeof v - 2; i++) {
        k += snprintf(v + k, sizeof v - (size_t)k, "%s%s", i ? "," : "", s_subs[i]);
    }
    if (k >= (int)sizeof v) {
        k = (int)sizeof v - 1;
    }
    tfn_secret_set(subs_key(s_srv), s_nsub ? v : " ");  // (" ": the user removed all; not the default again)
    if (s_srv >= SRV_PAGE) {
        snprintf(s_page_topics, sizeof s_page_topics, "%s", s_nsub ? v : " ");
    }
}

// ================================================================ the topics and the stream
#define TOPN   128
#define TOPMSG 1024
#define HISTN  48
typedef struct {
    bool used;
    char topic[MQ_TOPIC_MAX + 1];
    uint8_t msg[TOPMSG];
    int len, full;
    uint8_t qos;
    bool retain;
    uint32_t count;
    int64_t t;
    float hist[HISTN];  // the number values (a ring)
    int nh, hi;
} top_t;
static top_t *s_top;           // [TOPN] (PSRAM)
static int16_t s_order[TOPN];  // the used topics in the order of their names
static int s_ntop;
static bool s_topfull;  // a topic went out for a new one

#define STRN 160
enum {
    SK_MSG,
    SK_JOIN,
    SK_LEAVE,
    SK_SUB,
    SK_NOTE
};
typedef struct {
    uint8_t kind, qos;
    bool retain;
    int8_t slot;
    int full, subs;
    int64_t t;
    char topic[MQ_TOPIC_MAX + 1];
    char text[72];  // the start of the message, or the line of the event
} sent_t;
static sent_t *s_str;    // [STRN] ring (PSRAM)
static uint32_t s_nstr;  // all entries since the clear (the newest = s_nstr - 1)
static uint32_t s_nmsg;  // the messages since the clear
static uint32_t s_rate_n, s_rate_n0;
static int64_t s_rate_t;
static float s_rate;  // messages a second

static mqn_status_t s_st;
static mqn_client_t s_cl[MQ_BRK_CLIENTS];
static int s_last_state = -1;
static bool s_wifi;

// the screens
enum {
    SC_START,
    SC_SERVERS,
    SC_MAIN,
    SC_TOPIC,
    SC_PUB,
    SC_SUBS,
    SC_KBD
};
static int s_scr;
static int s_view;                 // CLIENT: 0 TOPICS, 1 STREAM; BROKER (narrow): 0 CLIENTS, 1 TRAFFIC
static int s_sel = -1;             // the open topic (a slot of s_top)
static float s_scroll, s_dscroll;  // the list, the text of A TOPIC
static int s_dtext_h;              // the height of that text (the render sets it)
static int s_kbd_for, s_kbd_back;  // the field of the keyboard, the screen after it
static char s_kbuf[420];
static char s_pub_topic[MQ_TOPIC_MAX + 1], s_pub_msg[257];
static int s_pub_qos;
static bool s_pub_ret;
static bool s_apply_wait;  // the network task of an earlier open still ends: start later
static int64_t s_toast_t;
static char s_toast[96];
enum {
    KB_HOST,
    KB_PORT,
    KB_TOPIC,
    KB_MSG,
    KB_FILTER
};

static void toast(const char *s)
{
    snprintf(s_toast, sizeof s_toast, "%s", s);
    s_toast_t = s_now;
}

// ---------------------------------------------------------------- the topic table

static int top_find(const char *t)
{
    for (int i = 0; i < TOPN; i++) {
        if (s_top[i].used && !strcmp(s_top[i].topic, t)) {
            return i;
        }
    }
    return -1;
}

static void order_remove(int slot)
{
    for (int i = 0; i < s_ntop; i++) {
        if (s_order[i] == slot) {
            memmove(&s_order[i], &s_order[i + 1], sizeof(s_order[0]) * (size_t)(s_ntop - i - 1));
            s_ntop--;
            return;
        }
    }
}

static void order_insert(int slot)
{
    int i = 0;
    while (i < s_ntop && strcmp(s_top[s_order[i]].topic, s_top[slot].topic) < 0) {
        i++;
    }
    memmove(&s_order[i + 1], &s_order[i], sizeof(s_order[0]) * (size_t)(s_ntop - i));
    s_order[i] = (int16_t)slot;
    s_ntop++;
}

// a number in the whole message (with spaces round it): true and *v. (Its own reader: the ABI has no strtod, and the
// double type of the S3 has no hardware: the reader uses float only.)
static bool as_number(const uint8_t *m, int n, float *v)
{
    if (n <= 0 || n > 24) {
        return false;
    }
    int i = 0;
    while (i < n && m[i] == ' ') {
        i++;
    }
    int e = n;
    while (e > i && (m[e - 1] == ' ' || m[e - 1] == '\r' || m[e - 1] == '\n')) {
        e--;
    }
    bool neg = false, dig = false;
    if (i < e && (m[i] == '-' || m[i] == '+')) {
        neg = m[i++] == '-';
    }
    float x = 0, scale = 1;
    bool frac = false;
    for (; i < e; i++) {
        const uint8_t c = m[i];
        if (c >= '0' && c <= '9') {
            dig = true;
            if (frac) {
                scale *= 0.1f;
                x += (float)(c - '0') * scale;
            } else {
                x = x * 10.0f + (float)(c - '0');
            }
        } else if (c == '.' && !frac) {
            frac = true;
        } else {
            break;
        }
    }
    if (!dig) {
        return false;
    }
    if (i < e && (m[i] == 'e' || m[i] == 'E')) {
        i++;
        bool en = false;
        int ex = 0;
        if (i < e && (m[i] == '-' || m[i] == '+')) {
            en = m[i++] == '-';
        }
        if (i >= e) {
            return false;
        }
        for (; i < e && m[i] >= '0' && m[i] <= '9'; i++) {
            ex = ex * 10 + (m[i] - '0');
        }
        if (ex > 38) {
            return false;
        }
        for (int k = 0; k < ex; k++) {
            x = en ? x * 0.1f : x * 10.0f;
        }
    }
    if (i != e || x > 3.0e38f) {
        return false;
    }
    *v = neg ? -x : x;
    return true;
}

static void top_put(const char *t, const uint8_t *m, int n, int full, int qos, bool ret)
{
    int k = top_find(t);
    if (k < 0) {
        for (int i = 0; i < TOPN && k < 0; i++) {
            if (!s_top[i].used) {
                k = i;
            }
        }
        if (k < 0) {  // (full: the topic with the oldest message goes)
            k = 0;
            for (int i = 1; i < TOPN; i++) {
                if (s_top[i].t < s_top[k].t && i != s_sel) {
                    k = i;
                }
            }
            order_remove(k);
            s_topfull = true;
        }
        top_t *z = &s_top[k];
        memset(z, 0, offsetof(top_t, msg));
        z->nh = z->hi = 0;
        z->count = 0;
        z->used = true;
        snprintf(z->topic, sizeof z->topic, "%s", t);
        order_insert(k);
    }
    top_t *z = &s_top[k];
    z->len = n < TOPMSG ? n : TOPMSG;
    memcpy(z->msg, m, (size_t)z->len);
    z->full = full;
    z->qos = (uint8_t)qos;
    z->retain = ret;
    z->count++;
    z->t = s_now;
    float v;
    if (as_number(m, n, &v)) {
        z->hist[z->hi] = v;
        z->hi = (z->hi + 1) % HISTN;
        if (z->nh < HISTN) {
            z->nh++;
        }
    }
}

// a text for one line: the printable start of a message (the other bytes as '.')
static void preview(char *o, int max, const uint8_t *m, int n)
{
    int k = 0;
    for (int i = 0; i < n && k < max - 1; i++) {
        const uint8_t c = m[i];
        if (c == '\n' || c == '\r' || c == '\t') {
            if (k && o[k - 1] != ' ') {
                o[k++] = ' ';
            }
        } else if (c >= 32 && c < 127) {
            o[k++] = (char)c;
        } else if (c >= 0xC0 && i + 1 < n && k < max - 4) {  // (a whole UTF-8 letter, if it fits)
            int l = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
            if (i + l > n || k + l >= max) {
                break;
            }
            memcpy(o + k, m + i, (size_t)l);
            k += l;
            i += l - 1;
        } else if (c < 0x80 || c >= 0xC0) {
            o[k++] = '.';
        }
    }
    o[k] = 0;
}

static sent_t *str_new(int kind)
{
    sent_t *e = &s_str[s_nstr % STRN];
    memset(e, 0, sizeof *e);
    e->kind = (uint8_t)kind;
    e->t = s_now;
    e->slot = -1;
    s_nstr++;
    return e;
}

static void clear_all(void)
{
    if (s_top) {
        memset(s_top, 0, sizeof(top_t) * TOPN);
    }
    s_ntop = 0;
    s_nstr = 0;
    s_nmsg = 0;
    s_rate_n = s_rate_n0 = 0;
    s_rate = 0;
    s_sel = -1;
    s_scroll = s_dscroll = 0;
    s_topfull = false;
    if (s_scr == SC_TOPIC) {
        s_scr = SC_MAIN;
    }
}

// ---------------------------------------------------------------- the link

static bool server_ok(void)
{
    return s_srv >= 0 && s_srv < NPRE ? true : s_srv == SRV_PAGE && s_acct.host[0];
}
static const char *server_name(void)
{
    return s_srv >= 0 && s_srv < NPRE ? PRE[s_srv].host : s_srv == SRV_PAGE ? s_acct.host : "";
}

static void apply(void)
{
    s_apply_wait = false;
    s_last_state = -1;
    bool ok = true;
    if (s_mode == 1) {
        ok = mqn_broker_start(1883, s_blogin ? s_acct.user : NULL, s_blogin ? s_acct.pass : NULL);
    } else if (s_mode == 0 && server_ok()) {
        mqn_server_t sv = {0};
        if (s_srv < NPRE) {
            snprintf(sv.host, sizeof sv.host, "%s", PRE[s_srv].host);
            sv.port = 1883;
        } else {
            snprintf(sv.host, sizeof sv.host, "%s", s_acct.host);
            sv.port = s_acct.port;
            sv.tls = s_acct.tls;
            snprintf(sv.user, sizeof sv.user, "%s", s_acct.user);
            snprintf(sv.pass, sizeof sv.pass, "%s", s_acct.pass);
        }
        snprintf(sv.id, sizeof sv.id, "%s", s_cid);
        sv.keepalive = 30;
        const char *f[SUBN];
        uint8_t q[SUBN];
        for (int i = 0; i < s_nsub; i++) {
            f[i] = s_subs[i];
            q[i] = 1;
        }
        ok = mqn_client_start(&sv, f, q, s_nsub);
        memset(&sv, 0, sizeof sv);
    } else {
        mqn_stop();
    }
    if (!ok) {
        s_apply_wait = true;  // (update tries again: the task of an earlier open still ends, or no memory)
    }
}

static void set_mode(int m)
{
    if (m == s_mode && s_scr != SC_START) {
        return;
    }
    s_mode = m;
    store_set("mqtt_mode", (uint32_t)m);
    clear_all();
    s_view = 0;
    s_scr = m == 0 && !server_ok() ? SC_SERVERS : SC_MAIN;
    apply();
}

static void set_server(int k)
{
    s_srv = k;
    store_set("mqtt_srv", (uint32_t)(k + 1));
    subs_load();
    clear_all();
    s_scr = SC_MAIN;
    apply();
}

// the state as a line for the board page (the app says: ...)
static void page_status(void)
{
    if (s_st.state == s_last_state) {
        return;
    }
    s_last_state = s_st.state;
    char b[160];
    if (s_mode == 1) {
        snprintf(b, sizeof b, s_st.state == MQN_LISTEN ? "THE BROKER LISTENS ON PORT %d" : "THE BROKER IS OFF",
                 s_st.port);
    } else if (s_st.state == MQN_UP) {
        snprintf(b, sizeof b, "CONNECTED TO %s", server_name());
    } else if (s_st.state == MQN_FAIL) {
        snprintf(b, sizeof b, "REFUSED BY %.60s: %.80s", server_name(), s_st.why);
    } else if (s_st.state == MQN_RETRY) {
        snprintf(b, sizeof b, "NOT CONNECTED: %s", s_st.why);
    } else {
        return;
    }
    tfn_secret_set("status", b);
}

// the events of the network task into the table, the stream and the counters
static void pump(void)
{
    static mqn_ev_t e;  // (2 KB: not on the stack of the app task)
    for (int k = 0; k < 64 && mqn_pop(&e); k++) {
        sent_t *s;
        switch (e.kind) {
        case MQN_E_MSG:
        case MQN_E_SEEN:
            top_put(e.topic, e.msg, e.len, e.full, e.qos, e.retain);
            s = str_new(SK_MSG);
            s->qos = e.qos;
            s->retain = e.retain;
            s->full = e.full;
            s->slot = e.kind == MQN_E_SEEN ? e.slot : -1;
            s->subs = e.kind == MQN_E_SEEN ? e.a : -1;
            snprintf(s->topic, sizeof s->topic, "%s", e.topic);
            preview(s->text, sizeof s->text, e.msg, e.len);
            s_nmsg++;
            s_rate_n++;
            break;
        case MQN_E_JOIN:
        case MQN_E_LEAVE:
            s = str_new(e.kind == MQN_E_JOIN ? SK_JOIN : SK_LEAVE);
            s->slot = e.slot;
            snprintf(s->topic, sizeof s->topic, "%s", e.topic);
            snprintf(s->text, sizeof s->text, "%s", e.text);
            break;
        case MQN_E_SUB:
            s = str_new(SK_SUB);
            s->slot = e.slot;
            s->qos = (uint8_t)e.a;
            snprintf(s->topic, sizeof s->topic, "%s", e.topic);
            break;
        case MQN_E_SUBACK:
            if (e.a == 0x80) {
                toast(TX(T_SUBREFUSED));
            }
            break;
        case MQN_E_NOTE:
            s = str_new(SK_NOTE);
            if (e.a == MQN_N_REFUSED) {
                snprintf(s->text, sizeof s->text, TX(e.qos == MQ_RC_LOGIN ? T_N_LOGIN : T_N_REFUSED), e.topic);
            } else {
                snprintf(s->text, sizeof s->text, "%s", TX(e.a == MQN_N_FULL ? T_N_FULL : T_N_NOSOCK));
            }
            break;
        }
    }
}

// ================================================================ the settings panel
enum {
    PI_LANG,
    PI_MODE,
    PI_SRV,
    PI_TYPE,
    PI_VIEW,
    PI_LOGIN,
    PI_RETRY,
    PI_CLEAR
};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const MODE_VALS[] = {"CLIENT", "לקוח", "BROKER", "ברוקר"};
static const char *const SRV_VALS[] = {"broker.hivemq.com",  "broker.hivemq.com",  "broker.emqx.io", "broker.emqx.io",
                                       "test.mosquitto.org", "test.mosquitto.org", "THE BOARD PAGE", "דף הלוח"};
static const char *const VIEW_VALS[] = {"TOPICS", "נושאים", "STREAM", "זרם"};
static const char *const LOGIN_VALS[] = {"OPEN", "פתוח", "THE PAGE LOGIN", "הכניסה מהדף"};
static pitem_t s_items[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, "mqtt_lang"},
        {PI_PICK, PI_MODE, 2, 0, {"MODE", "מצב"}, MODE_VALS, NULL},
        {PI_PICK, PI_SRV, NPRE + 1, 0, {"BROKER", "ברוקר"}, SRV_VALS, NULL},
        {PI_ACT, PI_TYPE, 0, 0, {"TYPE AN ADDRESS", "הקלדת כתובת"}, NULL, NULL},
        {PI_PICK, PI_VIEW, 2, 0, {"VIEW", "תצוגה"}, VIEW_VALS, NULL},
        {PI_PICK, PI_LOGIN, 2, 0, {"BROKER LOGIN", "כניסה לברוקר"}, LOGIN_VALS, "mqtt_blogin"},
        {PI_ACT, PI_RETRY, 0, 0, {"CONNECT AGAIN", "חיבור מחדש"}, NULL, NULL},
        {PI_ACT2, PI_CLEAR, 0, 0, {"CLEAR THE LIST", "ניקוי הרשימה"}, NULL, NULL},
};
#define NITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))

static void kbd_go(int what, int back);

static bool item_shown(int id)
{
    switch (id) {
    case PI_SRV:
    case PI_TYPE:
    case PI_VIEW:
        return s_mode == 0;
    case PI_LOGIN:
        return s_mode == 1;
    case PI_RETRY:
        return s_mode == 0 && server_ok();
    default:
        return true;
    }
}
static int item_get(int id)
{
    switch (id) {
    case PI_MODE:
        return s_mode == 1;
    case PI_SRV:
        return s_srv < 0 ? 0 : s_srv;
    case PI_VIEW:
        return s_view;
    default:
        return 0;
    }
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_LANG:
        s_he = v == 1;
        break;
    case PI_MODE:
        set_mode(v ? 1 : 0);
        break;
    case PI_SRV:
        if (v == SRV_PAGE && !s_acct.host[0]) {
            toast(TX(T_PAGE_NONE));
            s_scr = SC_SERVERS;
        } else {
            set_server(v);
        }
        break;
    case PI_TYPE:
        kbd_go(KB_HOST, SC_MAIN);
        break;
    case PI_VIEW:
        s_view = v & 1;
        s_scroll = 0;
        break;
    case PI_LOGIN:
        s_blogin = v == 1;
        apply();
        break;
    case PI_RETRY:
        apply();
        break;
    case PI_CLEAR:
        clear_all();
        break;
    }
}
static bool s_down, s_drag;
static void panel_opened(void)
{
    s_down = s_drag = false;
}
static const pitems_t PANEL = {s_items, NITEMS, item_shown, item_get, item_set, panel_opened, panel_opened};

// ================================================================ the keyboard

static void kbd_go(int what, int back)
{
    s_kbd_for = what;
    s_kbd_back = back;
    const char *title = TEXT[T_KBD_HOST][0];  // (English: the keyboard font has no Hebrew letters)
    int max = 96;
    s_kbuf[0] = 0;
    switch (what) {
    case KB_HOST:
        snprintf(s_kbuf, 96, "%s", s_acct.host);
        break;
    case KB_PORT:
        title = TEXT[T_KBD_PORT][0];
        snprintf(s_kbuf, 8, "%d", s_acct.port);
        max = 8;
        break;
    case KB_TOPIC:
        title = TEXT[T_KBD_TOPIC][0];
        snprintf(s_kbuf, sizeof s_kbuf, "%s", s_pub_topic);
        max = MQ_TOPIC_MAX + 1;
        break;
    case KB_MSG:
        title = TEXT[T_KBD_MSG][0];
        snprintf(s_kbuf, sizeof s_kbuf, "%s", s_pub_msg);
        max = (int)sizeof s_pub_msg;
        break;
    case KB_FILTER:
        title = TEXT[T_KBD_FILTER][0];
        max = MQ_TOPIC_MAX + 1;
        break;
    }
    s_scr = SC_KBD;
    kbd_open(title, s_kbuf, max, false);
    kbd_set_ok_question(NULL);
}

static bool sub_add(const char *f)
{
    if (!mq_filter_ok(f)) {
        toast(TX(T_BADFILTER));
        return false;
    }
    for (int i = 0; i < s_nsub; i++) {
        if (!strcmp(s_subs[i], f)) {
            return true;
        }
    }
    if (s_nsub >= SUBN) {
        toast(TX(T_FULL));
        return false;
    }
    snprintf(s_subs[s_nsub++], MQ_TOPIC_MAX + 1, "%s", f);
    subs_save();
    mqn_subscribe(f, 1);
    return true;
}

static void sub_remove(int i)
{
    if (i < 0 || i >= s_nsub) {
        return;
    }
    mqn_unsubscribe(s_subs[i]);
    memmove(s_subs[i], s_subs[i + 1], (size_t)(s_nsub - i - 1) * sizeof s_subs[0]);
    s_nsub--;
    subs_save();
}

static void kbd_done(bool ok)
{
    s_scr = s_kbd_back;
    tfd_screen();
    if (!ok) {
        return;
    }
    switch (s_kbd_for) {
    case KB_HOST:
        if (!s_kbuf[0]) {
            return;
        }
        tfn_secret_set("host", s_kbuf);
        acct_read(&s_acct);
        kbd_go(KB_PORT, SC_MAIN);
        s_kbd_back = SC_MAIN;
        return;
    case KB_PORT: {
        const int p = atoi(s_kbuf);
        if (p > 0 && p < 65536) {
            char v[8];
            snprintf(v, sizeof v, "%d", p);
            tfn_secret_set("port", v);
            tfn_secret_set("tls", p == 8883 || p == 8884 || p == 8886 ? "YES" : "NO");
        }
        acct_read(&s_acct);
        if (s_mode != 0) {
            s_mode = 0;
        }
        set_server(SRV_PAGE);
        return;
    }
    case KB_TOPIC:
        if (s_kbuf[0] && !mq_topic_ok(s_kbuf)) {
            toast(TX(T_BADTOPIC));
            return;
        }
        strlcpy(s_pub_topic, s_kbuf, sizeof s_pub_topic);
        return;
    case KB_MSG:
        strlcpy(s_pub_msg, s_kbuf, sizeof s_pub_msg);
        return;
    case KB_FILTER:
        if (s_kbuf[0]) {
            sub_add(s_kbuf);
        }
        return;
    }
}

static void publish_now(void)
{
    if (!s_pub_topic[0]) {
        toast(TX(T_NOTOPIC));
        return;
    }
    if (!mq_topic_ok(s_pub_topic)) {
        toast(TX(T_BADTOPIC));
        return;
    }
    const bool up = s_mode == 1 ? s_st.state == MQN_LISTEN : s_st.state == MQN_UP;
    if (!up || !mqn_publish(s_pub_topic, s_pub_msg, (int)strlen(s_pub_msg), s_pub_qos, s_pub_ret)) {
        toast(TX(T_NOTUP));
        return;
    }
    sfx_ui(SFX_CLICK);
    toast(TX(T_SENT));
}

// ================================================================ the screens and the touches
#include "mq_draw.inc"

static int s_dy0;
static float s_scroll0;
static int s_hit_down = -1;

static void input(const input_event_t *ev)
{
    if (s_scr == SC_KBD) {
        const int r = kbd_input(ev);
        if (r == KBD_OK) {
            kbd_done(true);
        } else if (r == KBD_CANCEL) {
            kbd_done(false);
        }
        return;
    }
    const int x = ev->x, y = ev->y;
    float *scr = s_scr == SC_TOPIC ? &s_dscroll : &s_scroll;
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_drag = false;
        s_dy0 = y;
        s_scroll0 = *scr;
        s_hit_down = hit_at(x, y);
        break;
    case IN_MOVE:
        if (s_down && abs(y - s_dy0) > 8 && y > s_top_h) {
            s_drag = true;
            s_hit_down = -1;
            input_tap_cancel();
        }
        if (s_drag) {
            *scr = s_scroll0 - (float)(y - s_dy0);
            if (*scr < 0) {
                *scr = 0;  // (the render limits the other end)
            }
        }
        break;
    case IN_UP:
        s_down = s_drag = false;
        break;
    case IN_TAP: {
        const int h = hit_at(x, y);
        if (h >= 0) {
            hit_do(h);
        }
        break;
    }
    default:
        break;
    }
}

// ================================================================ enter / leave / update
static void *ps(size_t n)
{
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}

static void enter(int64_t now)
{
    s_now = ms_now();
    (void)now;
    s_he = store_get("mqtt_lang", 1) == 1;  // (Hebrew when nothing is saved)
    s_blogin = store_get("mqtt_blogin", 0) == 1;
    uint32_t cid = store_get("mqtt_cid", 0);
    if (!cid) {
        cid = (esp_random() & 0xFFFFFF) | 0x1000000;
        store_set("mqtt_cid", cid);
    }
    snprintf(s_cid, sizeof s_cid, "tfn-%06x", (unsigned)(cid & 0xFFFFFF));
    if (!s_top) {
        s_top = ps(sizeof(top_t) * TOPN);
    }
    if (!s_str) {
        s_str = ps(sizeof(sent_t) * STRN);
    }
    acct_read(&s_acct);
    s_acct_t = s_now;
    const uint32_t m = store_get("mqtt_mode", 0xFF);
    s_mode = m == 0 ? 0 : m == 1 ? 1 : -1;
    const int sv = (int)store_get("mqtt_srv", 0) - 1;
    s_srv = sv >= 0 && sv <= SRV_PAGE ? sv : -1;
    if (s_srv == SRV_PAGE && !s_acct.host[0]) {
        s_srv = -1;
    }
    subs_load();
    clear_all();
    s_view = 0;
    s_toast[0] = 0;
    s_down = s_drag = false;
    s_pub_qos = 0;
    s_pub_ret = false;
    if (!s_pub_topic[0]) {
        snprintf(s_pub_topic, sizeof s_pub_topic, "tfn/%s/hello", s_cid);
    }
    if (!s_pub_msg[0]) {
        snprintf(s_pub_msg, sizeof s_pub_msg, "hello");
    }
    memset(&s_st, 0, sizeof s_st);
    s_last_state = -1;
    s_wifi = wifi_state() == WIFI_CONNECTED;
    s_rate_t = s_now;
    sysui_panel_items(&PANEL);
    if (!s_top || !s_str) {
        s_scr = SC_MAIN;
        s_mode = 0;
        s_srv = -1;
        toast(TX(T_NOMEM));
        return;
    }
    if (s_mode < 0) {
        s_scr = SC_START;
    } else if (s_mode == 0 && !server_ok()) {
        s_scr = SC_SERVERS;
    } else {
        s_scr = SC_MAIN;
        apply();
    }
}

static void leave(void)
{
    mqn_stop();
    store_set("mqtt_lang", s_he);
    memset(&s_acct, 0, sizeof s_acct);  // (the password)
}

static void update(int64_t now)
{
    (void)now;
    s_now = ms_now();
    if (s_apply_wait) {
        static int64_t t;
        if (s_now - t > 500) {
            t = s_now;
            apply();
        }
    }
    pump();
    mqn_status(&s_st);
    if (s_mode == 1) {
        mqn_clients(s_cl, MQ_BRK_CLIENTS);
    }
    page_status();
    if (s_now - s_rate_t >= 2000) {
        s_rate = (float)(s_rate_n - s_rate_n0) * 1000.0f / (float)(s_now - s_rate_t);
        s_rate_n0 = s_rate_n;
        s_rate_t = s_now;
        s_wifi = wifi_state() == WIFI_CONNECTED;
    }
    if (s_now - s_acct_t > (s_scr == SC_SERVERS ? 1000 : 3000) &&
        s_scr != SC_KBD) {  // (the board page can change the account)
        s_acct_t = s_now;
        acct_t a;
        acct_read(&a);
        const bool chg = strcmp(a.host, s_acct.host) || strcmp(a.user, s_acct.user) || strcmp(a.pass, s_acct.pass) ||
                         a.port != s_acct.port || a.tls != s_acct.tls;
        s_acct = a;
        memset(&a, 0, sizeof a);
        if (chg && ((s_mode == 0 && s_srv == SRV_PAGE) || (s_mode == 1 && s_blogin))) {
            if (s_mode == 0) {
                subs_load();
            }
            if (s_mode == 0 && !server_ok()) {
                s_scr = SC_SERVERS;
            }
            apply();
        }
        char all[420] = "";  // (the topics of the board page)
        tfn_secret_get("topics", all, sizeof all);
        if (strcmp(all, s_page_topics) && s_mode == 0 && s_srv == SRV_PAGE) {
            subs_load();
            apply();
        }
    }
}

static bool awake(void)
{
    return true;
}  // (a tool that one watches: the screen stays on)
static bool background(void)
{
    return s_mode == 1 || (s_mode == 0 && server_ok());
}  // (the list stays current)

const app_t app_mqtt = {
        .name = "MQTT",
        .landscape = true,
        .round_fit = true,
        .run = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
        .background = background,
};
