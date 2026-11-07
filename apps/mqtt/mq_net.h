#pragma once
// MQTT: the network part of the app (mq_net.c on the board; the PC tests give their own). One task of the app runs
// the client OR the broker (libs/tfmqtt) on the sockets.
// The task: the app task gives commands (mqn_publish ...) and takes the events (mqn_pop) through two rings with one
// writer and one reader each: no lock, no wait.
// The memory: mq_net.c keeps the rings and the state (PSRAM).
#include <stdbool.h>
#include <stdint.h>
#include "tfmqtt/tfmqtt.h"

#define MQN_MSG_MAX 2048  // the bytes of a message that the app gets (the event keeps the full length)

typedef struct {
    char host[96];
    int port;
    bool tls;
    char user[64], pass[64];
    char id[40];  // the client id
    uint16_t keepalive;
} mqn_server_t;

enum {
    MQN_OFF,
    MQN_DIAL,
    MQN_WAIT,
    MQN_UP,
    MQN_RETRY,
    MQN_LISTEN,
    MQN_FAIL
};
enum {
    MQN_E_MSG = 1,  // the client: a message (topic, msg, len, full, qos, retain)
    MQN_E_SUBACK,   // the client: a = the packet id, b = the code (0x80 = refused)
    MQN_E_SEEN,     // the broker: a message that went through it (slot = the sender, -1 = the
                    // app; a = the subscribers that got it)
    MQN_E_JOIN,     // the broker: a client came (slot, topic = its id, text = its address)
    MQN_E_LEAVE,    // the broker: a client went (slot, topic = its id, text = the reason)
    MQN_E_SUB,      // the broker: a client subscribed (slot, topic = the filter, a = the QoS)
    MQN_E_NOTE      // the broker: a note (a = MQN_N_*; REFUSED: topic = the client id, qos = the
                    // code of the CONNACK)
};
enum {
    MQN_N_FULL = 1,
    MQN_N_NOSOCK,
    MQN_N_REFUSED
};

typedef struct {
    uint8_t kind, qos;
    bool retain;
    int8_t slot;
    int a;
    int len, full;  // the bytes in msg; the bytes of the whole message
    int64_t t;      // ms (esp_timer)
    char topic[MQ_TOPIC_MAX + 1];
    char text[64];
    uint8_t msg[MQN_MSG_MAX];
} mqn_ev_t;

typedef struct {
    int state;
    char why[96];  // the last fault (MQN_RETRY, MQN_FAIL)
    int64_t t_up;  // ms
    uint32_t n_in, n_out;
    int ping_ms;            // the last trip of a ping (-1 = none)
    int port;               // the broker: its port
    int clients, retained;  // the broker
    int retry_s;            // MQN_RETRY: the seconds to the next try
} mqn_status_t;

typedef struct {
    bool up;
    char id[40];
    char addr[20];
    int nsub;
    uint32_t n_in, n_out;
    int64_t t_conn;
} mqn_client_t;

// Start the client (with the subscriptions) or the broker. The part that runs stops first. false = no memory or no task.
bool mqn_client_start(const mqn_server_t *s, const char *const *subs, const uint8_t *qos, int nsub);
bool mqn_broker_start(int port, const char *user, const char *pass);
void mqn_stop(void);  // (the task ends by itself; the app can leave at once)
// The commands (false = the ring is full, or the part does not run)
bool mqn_publish(const char *topic, const void *msg, int len, int qos, bool retain);
bool mqn_subscribe(const char *filter, int qos);
bool mqn_unsubscribe(const char *filter);
bool mqn_pop(mqn_ev_t *e);
void mqn_status(mqn_status_t *o);
int mqn_clients(mqn_client_t *out, int max);  // the broker: the slots (out[i] for the slot i), max <= MQ_BRK_CLIENTS
