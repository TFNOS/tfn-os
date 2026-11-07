#pragma once
// tfmqtt: MQTT 3.1.1 for the apps. Pure C, no ESP-IDF: the PC tests run all of it. A module of the firmware: its
// functions are ABI names (an app includes this header and calls the firmware); no static state.
//   - the packets: the stream framer (mq_rx_*), the encoders, the topic rules (mq_topic_ok, mq_filter_ok, mq_match)
//   - a client (mq_cli_*): the state of one link to a broker; the caller moves the bytes (mq_cli_t.send)
//   - a broker (mq_brk_*, mq_broker.c): up to MQ_BRK_CLIENTS links, the subscriptions, the retained messages, the
//     will messages, QoS 0, 1 and 2 in, QoS 0 and 1 out; the caller moves the bytes (mq_brk_cfg_t.send / .drop)
// The sockets are in the app (apps/mqtt/mq_net.c: a task of the app calls these functions; the functions do not wait).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MQ_MAX_PKT   4096  // the longest packet that a link takes (a longer one closes the link)
#define MQ_TOPIC_MAX 128   // the longest topic or filter (bytes, with no 0)

enum {
    MQ_CONNECT = 1,
    MQ_CONNACK,
    MQ_PUBLISH,
    MQ_PUBACK,
    MQ_PUBREC,
    MQ_PUBREL,
    MQ_PUBCOMP,
    MQ_SUBSCRIBE,
    MQ_SUBACK,
    MQ_UNSUBSCRIBE,
    MQ_UNSUBACK,
    MQ_PINGREQ,
    MQ_PINGRESP,
    MQ_DISCONNECT
};
// the CONNACK return codes
enum {
    MQ_RC_OK = 0,
    MQ_RC_VERSION,
    MQ_RC_ID,
    MQ_RC_UNAVAILABLE,
    MQ_RC_LOGIN,
    MQ_RC_AUTH
};
const char *mq_rc_text(int rc);
const char *mq_type_name(int type);

// ================================================================ the stream framer
typedef struct {
    uint8_t buf[MQ_MAX_PKT];
    int n;     // the bytes in buf
    int used;  // the bytes of the packet that mq_rx_next gave last
    bool bad;  // a packet longer than MQ_MAX_PKT or a bad length: close the link
} mq_rx_t;
typedef struct {
    uint8_t type, flags;  // the 4 bits of each
    int len;              // the remaining length
    const uint8_t *body;  // len bytes (in the mq_rx_t: use it before the next feed or next)
} mq_pkt_t;

void mq_rx_init(mq_rx_t *r);
// Gives up to n bytes into the framer; returns the bytes that it took (it takes less when its buffer is full: call
// mq_rx_next, then give the rest again).
int mq_rx_feed(mq_rx_t *r, const void *d, int n);
// The next whole packet: true and *p; false = no whole packet yet (or r->bad).
bool mq_rx_next(mq_rx_t *r, mq_pkt_t *p);

// ================================================================ the encoders (> 0 = the bytes in o; 0 = no room)
typedef struct {
    const char *id;           // the client id ("" = the broker gives one, with clean = true)
    const char *user, *pass;  // NULL or "" = none
    const char *will_topic;   // NULL = no will message
    const void *will_msg;
    int will_len, will_qos;
    bool will_retain;
    bool clean;
    uint16_t keepalive;  // seconds (0 = no keep-alive)
} mq_connect_t;
int mq_enc_connect(uint8_t *o, int max, const mq_connect_t *c);
int mq_enc_connack(uint8_t *o, int max, bool session, int rc);
int mq_enc_publish(uint8_t *o, int max, const char *topic, const void *msg, int len, int qos, bool retain, bool dup,
                   uint16_t id);
int mq_enc_ack(uint8_t *o, int max, int type, uint16_t id);  // PUBACK, PUBREC, PUBREL, PUBCOMP, UNSUBACK
int mq_enc_subscribe(uint8_t *o, int max, uint16_t id, const char *const *filters, const uint8_t *qos, int n);
int mq_enc_suback(uint8_t *o, int max, uint16_t id, const uint8_t *codes, int n);
int mq_enc_unsubscribe(uint8_t *o, int max, uint16_t id, const char *const *filters, int n);
int mq_enc_simple(uint8_t *o, int max, int type);  // PINGREQ, PINGRESP, DISCONNECT

// ================================================================ the topics
bool mq_topic_ok(const char *t);   // a topic of a PUBLISH: 1..MQ_TOPIC_MAX bytes, no '+' or '#'
bool mq_filter_ok(const char *f);  // a filter of a SUBSCRIBE: '+' = one whole level, '#' = the last level only
bool mq_match(const char *filter, const char *topic);  // (a filter that starts with '+' or '#' does not match "$...")

// A string of the packet: a 16-bit length, then the bytes. *at moves on. false = the packet ends first.
// out gets up to max-1 bytes and a 0 (a longer string is false).
bool mq_get_str(const mq_pkt_t *p, int *at, char *out, int max);
bool mq_get_u16(const mq_pkt_t *p, int *at, uint16_t *v);

// ================================================================ the client
enum {
    MQ_CLI_IDLE,
    MQ_CLI_WAIT,
    MQ_CLI_UP,
    MQ_CLI_DOWN
};
enum {               // the events of the client (mq_cli_t.event)
    MQ_EV_UP,        // CONNACK 0 (a = the session present flag)
    MQ_EV_REFUSED,   // CONNACK with a code (a = the code)
    MQ_EV_MSG,       // a message: topic, msg, len, a = QoS, b = retain, c = dup
    MQ_EV_SUBACK,    // a = the packet id, b = the first return code (0x80 = refused)
    MQ_EV_UNSUBACK,  // a = the packet id
    MQ_EV_PUBDONE,   // a = the packet id of a QoS 1 message that the broker has
    MQ_EV_PONG,      // a = the time of the trip (ms)
    MQ_EV_LOST       // the link must close: a = the reason (MQ_LOST_*)
};
enum {
    MQ_LOST_PROTO = 1,
    MQ_LOST_PING,
    MQ_LOST_BIG,
    MQ_LOST_TIME
};
const char *mq_lost_text(int why);

#define MQ_CLI_INFLIGHT 8  // the QoS 1 messages that wait for a PUBACK
#define MQ_CLI_REC      8  // the QoS 2 ids from the broker that wait for a PUBREL

typedef struct mq_cli mq_cli_t;
struct mq_cli {
    // the caller sets these
    void *user;
    bool (*send)(mq_cli_t *c, const void *d, int n);  // all the bytes, or false (the link is lost)
    void (*event)(mq_cli_t *c, int ev, const char *topic, const void *msg, int len, int a, int b, int c2);
    // the state
    int state;
    uint16_t keepalive, next_id;
    int64_t t_tx, t_rx, t_ping, t_conn;  // ms
    struct {
        uint16_t id;
        int64_t t;
        int n;
        uint8_t *pkt;
    } fly[MQ_CLI_INFLIGHT];
    uint16_t rec[MQ_CLI_REC];
    mq_rx_t rx;
    uint8_t tx[MQ_MAX_PKT];
    uint32_t n_in, n_out;  // the messages
};

void mq_cli_init(mq_cli_t *c);
// The CONNECT (after the socket is up). false = the packet does not fit or the send failed.
bool mq_cli_start(mq_cli_t *c, const mq_connect_t *k, int64_t now_ms);
// The bytes from the socket.
void mq_cli_in(mq_cli_t *c, const void *d, int n, int64_t now_ms);
// Each 100 ms or so: the PINGREQ, the resends of QoS 1, the time limit of the CONNACK.
void mq_cli_tick(mq_cli_t *c, int64_t now_ms);
// > 0 = the packet id (QoS 1), 0 = sent (QoS 0), < 0 = an error (-1 the topic, -2 too long, -3 full, -4 the link)
int mq_cli_publish(mq_cli_t *c, const char *topic, const void *msg, int len, int qos, bool retain, int64_t now_ms);
int mq_cli_subscribe(mq_cli_t *c, const char *filter, int qos);  // the packet id, or < 0
int mq_cli_unsubscribe(mq_cli_t *c, const char *filter);
void mq_cli_stop(mq_cli_t *c);  // DISCONNECT (no will message), then free the state
void mq_cli_free(mq_cli_t *c);  // the messages that wait (no packet)

// ================================================================ the broker (mq_broker.c)
#define MQ_BRK_CLIENTS    8
#define MQ_BRK_SUBS       16    // the filters of one client
#define MQ_BRK_RETAIN     48    // the retained messages
#define MQ_BRK_RETAIN_LEN 1024  // the longest retained message (bytes)

enum {               // the events of the broker (mq_brk_cfg_t.event)
    MQ_BEV_CONNECT,  // slot, text = the client id, a = clean, b = keep-alive
    MQ_BEV_REFUSE,   // slot, text = the client id, a = the code
    MQ_BEV_GONE,     // slot, text = the client id, a = the reason (MQ_GONE_*)
    MQ_BEV_PUBLISH,  // slot (-1 = the app), text = the topic, msg, len, a = QoS, b = retain,
                     // c = the subscribers that got it
    MQ_BEV_SUB,      // slot, text = the filter, a = the granted QoS (0x80 = refused)
    MQ_BEV_UNSUB     // slot, text = the filter
};
enum {
    MQ_GONE_BYE = 1,
    MQ_GONE_CLOSED,
    MQ_GONE_PROTO,
    MQ_GONE_TIME,
    MQ_GONE_TAKEN,
    MQ_GONE_BIG,
    MQ_GONE_FULL
};
const char *mq_gone_text(int why);

typedef struct mq_brk mq_brk_t;
typedef struct {
    void *user;
    bool (*send)(void *user, int slot, const void *d, int n);  // false = the link is lost (the broker drops it)
    void (*drop)(void *user, int slot);                        // close the socket of the slot
    void (*event)(void *user, int ev, int slot, const char *text, const void *msg, int len, int a, int b, int c);
    const char *user_name, *password;  // NULL or "" = any client may connect
} mq_brk_cfg_t;

typedef struct {
    bool used, up;  // a socket; a CONNECT that the broker took
    char id[64];
    uint16_t keepalive;
    int nsub;
    uint32_t n_in, n_out;
    int64_t t_rx, t_conn;
} mq_brk_info_t;

mq_brk_t *mq_brk_new(const mq_brk_cfg_t *cfg);  // NULL = no memory
void mq_brk_free(mq_brk_t *b);
// A new socket: the slot (0 .. MQ_BRK_CLIENTS-1), or -1 = full (close the socket).
int mq_brk_open(mq_brk_t *b, int64_t now_ms);
void mq_brk_in(mq_brk_t *b, int slot, const void *d, int n, int64_t now_ms);
void mq_brk_closed(mq_brk_t *b, int slot);      // the socket closed (or failed): the will message goes out
void mq_brk_tick(mq_brk_t *b, int64_t now_ms);  // the keep-alive limits (1.5 x) and the CONNECT time limit
// A message from the app itself (it is not a client): the number of subscribers that got it, or < 0.
int mq_brk_publish(mq_brk_t *b, const char *topic, const void *msg, int len, int qos, bool retain);
void mq_brk_info(const mq_brk_t *b, int slot, mq_brk_info_t *out);
int mq_brk_retained(const mq_brk_t *b);
int mq_brk_sub_at(const mq_brk_t *b, int slot, int i, char *out, int max);  // the filter i of a slot: its QoS, or -1
