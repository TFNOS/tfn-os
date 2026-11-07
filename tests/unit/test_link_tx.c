// The unit test of the send queue of the link (main/link.c, the fakes of tests/unit/fake_link/): at most TX_MAX (6)
// frames in flight; the next frames wait in their order and go when the send callbacks come (at a send or a poll of the
// app); a full queue drops its oldest frame; a refusal of ESP-NOW (ESP_ERR_ESPNOW_NO_MEM) keeps the frame; a count
// that a channel change made old does not stop the sends (the probe after 250 ms with no callback); a stuck radio (no
// callback and each send refused for 1 s; 3 s while QUIET) gets a restart of the whole WiFi driver, and the first frame
// that waits goes at once; at most one recovery in 5 s.
// Build and run: python tests/unit/run.py.
#include <stdio.h>
#include <string.h>
#include "fake_board.h"
#include "friends.h"
#include "lan.h"
#include "link.h"
#include "pool_fake.h"

static int s_fail, s_n;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        s_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            s_fail++;                                                                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                        \
        }                                                                                                              \
    } while (0)

static const uint8_t ME[6] = {0x02, 0, 0, 0, 0, 0x01};
static int s_out;            // the ESP-NOW frames that went out
static uint8_t s_last_data;  // the first data byte of the last frame that went out

static void on_out(int kind, uint32_t ip, const uint8_t *p, int n)
{
    (void)ip;
    if (kind == 'E' && n > 9) {
        s_out++;
        s_last_data = p[9];
    }
}

static void ms(int t)
{
    fk_now_us += (int64_t)t * 1000;
}

static void poll(void)
{
    link_msg_t m;
    while (link_recv(&m)) {
    }
}

static bool send_b(uint8_t b)
{
    return link_send('Z', 1, &b, 1);
}

static void start(void)
{
    fk_wifi = 2;  // (an AP: the meeting does not move the radio)
    fk_channel = 6;
    fk_now_auto = 0;
    fk_now_lim = 8;
    fk_now_stuck = 0;
    fk_restarts = fk_full_restarts = 0;
    CHECK(link_start());
    s_out = 0;
}

// at most 6 in flight; the next ones wait and go in their order with the callbacks
static void test_cap(void)
{
    start();
    for (int i = 0; i < 9; i++) {
        CHECK(send_b((uint8_t)i));
    }
    link_tx_t t;
    link_tx_stats(&t);
    CHECK(s_out == 6 && t.in == 6 && t.wait == 3 && fk_now_inflight == 6);
    fk_now_done(2);
    poll();
    CHECK(s_out == 8 && s_last_data == 7);
    fk_now_done(6);
    poll();
    link_tx_stats(&t);
    CHECK(s_out == 9 && s_last_data == 8 && t.wait == 0 && t.dropped == 0);
    // a new frame goes after the frames that wait (the order stays)
    for (int i = 0; i < 8; i++) {
        send_b((uint8_t)(20 + i));
    }
    CHECK(s_out == 14);  // (frame 8 is in flight: 5 more go, 3 wait)
    fk_now_done(8);
    send_b(30);
    CHECK(s_out == 18 && s_last_data == 30);
    fk_now_done(8);
    poll();
    link_stop();
}

// a full queue drops its oldest frame
static void test_drop(void)
{
    start();
    for (int i = 0; i < 6 + 6 + 2; i++) {
        send_b((uint8_t)i);
    }
    link_tx_t t;
    link_tx_stats(&t);
    CHECK(t.wait == 6 && t.dropped >= 2);
    fk_now_done(6);
    poll();
    CHECK(s_out == 12 && s_last_data == 13);
    link_tx_stats(&t);
    CHECK(t.wait == 0);
    fk_now_done(8);
    link_stop();
}

// ESP-NOW refuses (its own limit): the frame waits and goes after a callback
static void test_refuse(void)
{
    start();
    fk_now_lim = 3;
    for (int i = 0; i < 5; i++) {
        CHECK(send_b((uint8_t)i));  // (a refused frame waits: the send is not lost)
    }
    CHECK(s_out == 3);
    fk_now_done(3);
    poll();
    CHECK(s_out == 5 && s_last_data == 4);
    fk_now_done(3);
    link_stop();
}

// a channel change loses the callbacks: the count is old, a probe after 250 ms finds it
static void test_old_count(void)
{
    start();
    for (int i = 0; i < 6; i++) {
        send_b((uint8_t)i);
    }
    fk_now_inflight = 0;  // (the frames went, the callbacks did not come)
    send_b(10);
    CHECK(s_out == 6);
    ms(100);
    poll();
    CHECK(s_out == 6);
    ms(200);
    poll();
    CHECK(s_out == 7 && s_last_data == 10);
    link_tx_t t;
    link_tx_stats(&t);
    CHECK(t.probes == 1 && t.in == 1);
    send_b(11);
    CHECK(s_out == 8);  // (the count is correct again)
    fk_now_done(8);
    link_stop();
    // a channel change of the link: no frame in flight after it
    fk_wifi = 0;
    start();
    fk_wifi = 0;
    for (int i = 0; i < 6; i++) {
        send_b((uint8_t)i);
    }
    CHECK(link_force_channel(9));
    fk_now_inflight = 0;
    send_b(12);
    CHECK(s_out == 7 && s_last_data == 12);
    fk_now_done(8);
    link_stop();
}

// a stuck radio (only a restart of the whole driver repairs it): the recovery after about 1 s
static void test_stuck(void)
{
    start();
    send_b(1);
    fk_now_stuck = 2;  // (the callback of frame 1 never comes, each send is refused)
    for (int t = 0; t < 900; t += 20) {
        send_b(2);
        ms(20);
    }
    CHECK(fk_restarts == 0);
    for (int t = 0; t < 200; t += 20) {
        send_b(3);
        ms(20);
    }
    CHECK(fk_restarts == 1 && fk_full_restarts == 1 && fk_now_stuck == 0);
    CHECK(s_last_data == 2 || s_last_data == 3);  // (the first frame that waited went at once)
    link_tx_t t;
    link_tx_stats(&t);
    CHECK(t.restarts == 1);
    fk_now_done(8);
    send_b(4);
    CHECK(s_last_data == 4);
    fk_now_done(8);
    // stuck again at once and for ever: no new recovery for 5 s, then one each 5 s
    fk_now_stuck = 3;
    for (int k = 0; k < 3000; k += 20) {
        send_b(5);
        ms(20);
    }
    CHECK(fk_restarts == 1);
    for (int k = 0; k < 2500; k += 20) {
        send_b(6);
        ms(20);
    }
    CHECK(fk_restarts == 2);
    fk_now_stuck = 0;
    link_stop();
}

// QUIET (the radio saves power): the frames wait longer for the wake; no restart before 3 s
static void test_quiet(void)
{
    ms(6000);
    start();
    link_quiet(true);
    send_b(1);
    fk_now_stuck = 1;
    for (int t = 0; t < 2000; t += 20) {
        send_b(2);
        ms(20);
    }
    CHECK(fk_restarts == 0);
    for (int t = 0; t < 1200; t += 20) {
        send_b(3);
        ms(20);
    }
    CHECK(fk_restarts == 1);
    fk_now_stuck = 0;
    fk_now_done(8);
    link_stop();
}

int main(void)
{
    fk_out = on_out;
    fk_now_us = 100000000;
    pf_reset();
    pf_board(0, ME, NULL, false, false);  // (no pool)
    memcpy(fk_mac, ME, 6);
    friends_init();
    lan_start();
    test_cap();
    test_drop();
    test_refuse();
    test_old_count();
    test_stuck();
    test_quiet();
    fk_now_auto = 1;
    printf("LINK TX %s: %d checks, %d failed\n", s_fail ? "FAIL" : "OK", s_n, s_fail);
    return s_fail ? 1 : 0;
}
