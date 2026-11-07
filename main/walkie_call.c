// WALKIE calls (walkie_call.h): the call packets over the LAN and the VPN, the setting of the board that gets a call,
// the question on the screen with the ring.
#include "walkie_call.h"
#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#else  // (the host tests: one task)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m)        ((void)(m))
#define portEXIT_CRITICAL(m)         ((void)(m))
#endif
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"
#include "audio.h"
#include "friends.h"
#include "lan.h"
#include "net2.h"
#include "settings.h"
#include "sysui.h"
#include "tfn_pool.h"

static const char *WC_TAG = "wcall";
enum {
    OP_CALL = 1,
    OP_CANCEL,
    OP_DECLINE,
    OP_BUSY,
    OP_WAKE
};
#define CALL_EVERY_MS  1500
#define CALL_MAX_MS    30000
#define ASK_GONE_MS    5000   // the question closes when no CALL came for this long (the caller stopped)
#define ANSWER_KEEP_MS 10000  // the answer waits this long for WALKIE

static int64_t wc_now(void)
{
    return esp_timer_get_time() / 1000;
}

int walkie_call_mode(void)
{
    int m = (int)store_get("wk_call", WK_CALL_ASK);
    return m >= WK_CALL_OPEN && m <= WK_CALL_IGNORE ? m : WK_CALL_ASK;
}
void walkie_call_set_mode(int m)
{
    if (m >= WK_CALL_OPEN && m <= WK_CALL_IGNORE) {
        store_set("wk_call", (uint32_t)m);
    }
}
bool walkie_call_ring(void)
{
    return store_get("wk_ring", 1) != 0;
}
void walkie_call_set_ring(bool on)
{
    store_set("wk_ring", on ? 1 : 0);
}

// ---------------------------------------------------------------- the packets in (the lan task) -> the app task
#define QN 8
typedef struct {
    uint8_t mac[6];
    uint8_t op;
} wc_inq_t;
static wc_inq_t wc_q[QN];
static int wc_qh, wc_qt;
static portMUX_TYPE wc_mux = portMUX_INITIALIZER_UNLOCKED;
static void wc_on_call(const uint8_t from[6], uint8_t op)
{
    portENTER_CRITICAL(&wc_mux);
    int n = (wc_qh + 1) % QN;
    if (n != wc_qt) {
        memcpy(wc_q[wc_qh].mac, from, 6);
        wc_q[wc_qh].op = op;
        wc_qh = n;
    }
    portEXIT_CRITICAL(&wc_mux);
}
static bool wc_take(wc_inq_t *out)
{
    bool ok = false;
    portENTER_CRITICAL(&wc_mux);
    if (wc_qt != wc_qh) {
        *out = wc_q[wc_qt];
        wc_qt = (wc_qt + 1) % QN;
        ok = true;
    }
    portEXIT_CRITICAL(&wc_mux);
    return ok;
}
void walkie_call_init(void)
{
    lan_set_call_rx(wc_on_call);
}

static void wc_send(int f, uint8_t op)
{
    if (f >= 0 && f < friends_count()) {
        lan_send_call(friends_get(f)->mac, op);
    }
}

// ---------------------------------------------------------------- the caller
static wkc_state_t wc_st;
static int wc_fi = -1;
static int64_t wc_t0, wc_sent, wc_st_at;

void walkie_call_start(int i)
{
    if (i < 0 || i >= friends_count()) {
        return;
    }
    if (wc_st == WKC_CALLING && wc_fi != i) {
        wc_send(wc_fi, OP_CANCEL);
    }
    wc_fi = i;
    wc_st = WKC_CALLING;
    wc_t0 = wc_st_at = wc_now();
    wc_sent = 0;
    friends_choose(i);  // (the HELLO of the lobby names only this friend: the call by radio)
    ESP_LOGI(WC_TAG, "call %s", friends_get(i)->name);
}
void walkie_call_stop(void)
{
    if (wc_st == WKC_CALLING) {
        wc_send(wc_fi, OP_CANCEL);
        ESP_LOGI(WC_TAG, "call stopped");
    }
    wc_st = WKC_IDLE;
    wc_st_at = wc_now();
    friends_choose(FRIENDS_NOBODY);
}
void walkie_call_done(void)
{
    if (wc_st == WKC_CALLING) {
        ESP_LOGI(WC_TAG, "call answered (%d ms)", (int)(wc_now() - wc_t0));
    }
    wc_st = WKC_IDLE;
}
wkc_state_t walkie_call_state(int *friend_idx, int64_t *since_ms)
{
    if (friend_idx) {
        *friend_idx = wc_fi;
    }
    if (since_ms) {
        *since_ms = wc_st_at;
    }
    return wc_st;
}
static void wc_ended(wkc_state_t st, int64_t now)
{
    wc_st = st;
    wc_st_at = now;
    friends_choose(FRIENDS_NOBODY);
    ESP_LOGI(WC_TAG, "call ended: %s", st == WKC_BUSY ? "busy" : st == WKC_DECLINED ? "not now" : "no answer");
}

// ---------------------------------------------------------------- the board that gets a call
static int wc_answer = -1, wc_ask_fi = -1, wc_ring_ph = -1;
static int64_t wc_answer_at, wc_ask_last, wc_ring_t0;

int walkie_call_take_answer(void)
{
    int a = wc_answer;
    wc_answer = -1;
    return a;
}
static void wc_answer_to(int f, int64_t now)
{
    wc_answer = f;
    wc_answer_at = now;
    if (app_current() != &app_walkie) {
        app_open(&app_walkie);
    }
}
static void wc_ask_done(int yes)
{
    int f = wc_ask_fi;
    wc_ask_fi = -1;
    if (f < 0 || f >= friends_count()) {
        return;
    }
    if (yes == 1) {
        ESP_LOGI(WC_TAG, "answered %s", friends_get(f)->name);
        wc_answer_to(f, wc_now());
    } else if (yes == 0) {
        ESP_LOGI(WC_TAG, "not now: %s", friends_get(f)->name);
        wc_send(f, OP_DECLINE);
    }
}
static void wc_incoming(int f, int64_t now)
{
    const app_t *a = app_current();
    bool in_walkie = a == &app_walkie || a == &app_talkie;  // (TALKIE: the same engine)
    // a talk, or a match with another board (app_in_match: 2 BOARDS, a turn game, PONG, FIGHT, TEAM BUILDING, BOULEVARD)
    if ((in_walkie && walkie_paired()) || (!in_walkie && app_in_match())) {
        wc_send(f, OP_BUSY);  // (the caller may be the peer: it ignores BUSY then)
        return;
    }
    if (wc_ask_fi == f) {
        wc_ask_last = now;
        return;
    }  // the same call: the question stays
    if (wc_answer == f) {
        return;  // WALKIE opens
    }
    if (in_walkie) {  // WALKIE waits in its lobby: it answers at once
        app_screen_wake();
        wc_answer_to(f, now);
        return;
    }
    int m = walkie_call_mode();
    if (m == WK_CALL_IGNORE) {
        wc_send(f, OP_DECLINE);
        return;
    }
    if (wc_ask_fi >= 0 || sysui_active()) {
        wc_send(f, OP_BUSY);
        return;
    }  // a question or a message shows now
    app_screen_wake();
    ESP_LOGI(WC_TAG, "call from %s (%s)", friends_get(f)->name, m == WK_CALL_OPEN ? "open" : "ask");
    if (m == WK_CALL_OPEN) {
        // (a call must show a notice on the board of the friend, not only the talk screen)
        char msg[64];
        snprintf(msg, sizeof msg, "%s CALLS YOU ON WALKIE", friends_get(f)->name);
        sysui_message(msg, 3);
        if (walkie_call_ring()) {
            sfx_tone(1050, 1300, 300, W_SQUARE, 40);
        }
        wc_answer_to(f, now);
        return;
    }
    wc_ask_fi = f;
    wc_ask_last = wc_ring_t0 = now;
    wc_ring_ph = -1;
    sysui_ask(friends_get(f)->name, "CALLS YOU ON WALKIE", "ANSWER", "NOT NOW", 0, wc_ask_done);
}
// WAKE: the manager opens TALKIE (when a user speaks, TALKIE opens on the board of the friend and plays the voice)
static int64_t wc_wake_at;
static void wc_wake(int f, int64_t now)
{
    const app_t *a = app_current();
    bool in_walkie = a == &app_walkie || a == &app_talkie;
    if ((in_walkie && walkie_paired()) || (!in_walkie && app_in_match())) {
        wc_send(f, OP_BUSY);  // (a talk with another friend, or a match with a board)
        return;
    }
    // the answer: WALKIE / TALKIE pairs ONLY with the manager (the real test: a woken TALKIE paired with another
    // friend in TALKIE, and the voice of the manager went nowhere)
    if (in_walkie) {
        app_screen_wake();
        wc_answer = f;
        wc_answer_at = now;
        return;
    }  // (in its lobby)
    if (walkie_call_mode() == WK_CALL_IGNORE) {
        wc_send(f, OP_DECLINE);
        return;
    }
    if (now - wc_wake_at < 3000) {
        return;  // (TALKIE opens now; the manager sends WAKE each 1 s)
    }
    wc_wake_at = now;
    if (wc_ask_fi >= 0) {
        sysui_ask_close();  // (the question of another call)
    }
    ESP_LOGI(WC_TAG, "wake from %s: TALKIE opens", friends_get(f)->name);
    app_screen_wake();
    wc_answer = f;
    wc_answer_at = now;
    app_open(&app_talkie);
}

// WAKE on a board in a pool (design 2.8.2): a good call v2 of a roster MANAGER; a call v1 of a friend only in RELAXED,
// or in STRICT of a legacy friend (a manager on an older firmware). A board with no pool: any friend.
static bool wc_wake_ok(const uint8_t mac[6], bool sealed)
{
    if (!tfn_pool_on()) {
        return true;
    }
    return friends_pool_take(sealed ? FRIENDS_IN_WAKE2 : FRIENDS_IN_WAKE1, mac, true, tfn_pool_strict(),
                             tfn_pool_role(tfn_pool_member(mac)));
}

// the ring: two short tones each 2.4 s (on the loud band of the 3.49 speaker)
static void wc_ring(int64_t now)
{
    if (!walkie_call_ring()) {
        return;
    }
    int ph = (int)((now - wc_ring_t0) % 2400), step = ph < 320 ? 0 : ph < 640 ? 1 : 2;
    if (step == wc_ring_ph) {
        return;
    }
    wc_ring_ph = step;
    if (step == 0) {
        sfx_tone(1050, 1050, 280, W_SQUARE, 40);
    } else if (step == 1) {
        sfx_tone(1300, 1300, 280, W_SQUARE, 40);
    }
}

void walkie_call_update(int64_t now)
{
    wc_inq_t q;
    while (wc_take(&q)) {
        int f = friends_find(q.mac);
        if (f < 0) {
            continue;  // only friends call
        }
        const bool sealed = (q.op & LAN_CALL_SEALED) != 0;  // (a good call v2 of the pool, lan.c)
        switch (q.op & (uint8_t)~LAN_CALL_SEALED) {
        case OP_CALL:
            wc_incoming(f, now);
            break;
        case OP_WAKE:
            if (wc_wake_ok(q.mac, sealed)) {
                wc_wake(f, now);
            }
            break;
        case OP_CANCEL:
            if (wc_ask_fi == f) {
                sysui_ask_close();
            }
            if (wc_answer == f && app_current() != &app_walkie) {
                wc_answer = -1;
            }
            break;
        case OP_DECLINE:
        case OP_BUSY:
            if (wc_st == WKC_CALLING && wc_fi == f) {
                wc_ended(q.op == OP_BUSY ? WKC_BUSY : WKC_DECLINED, now);
            }
            break;
        }
    }
    if (wc_st == WKC_CALLING) {
        if (wc_fi < 0 || wc_fi >= friends_count()) {
            wc_st = WKC_IDLE;
        } else if (now - wc_t0 > CALL_MAX_MS) {
            wc_send(wc_fi, OP_CANCEL);
            wc_ended(WKC_NO_ANSWER, now);
        } else if (now - wc_sent >= CALL_EVERY_MS) {
            wc_sent = now;
            wc_send(wc_fi, OP_CALL);
        }
    }
    if (wc_ask_fi >= 0) {
        if (now - wc_ask_last > ASK_GONE_MS) {
            ESP_LOGI(WC_TAG, "the caller stopped");
            sysui_ask_close();
        } else {
            wc_ring(now);
        }
    }
    if (wc_answer >= 0 && now - wc_answer_at > ANSWER_KEEP_MS) {
        wc_answer = -1;
    }
}
