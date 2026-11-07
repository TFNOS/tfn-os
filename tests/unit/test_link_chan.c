// The unit test of the channel of the link (main/link.c with main/link_meet.c): the meeting of two boards as link.c
// does it with the radio (the fakes of tests/unit/fake_link/). It checks: a board with no AP goes to the meeting
// channel and visits the channels (first its own last channel); each search step sends the last packet of the app and
// the last name beacon again on the new channel, as the same frames (the same packet id); a friend on an AP takes this
// board to its channel; a board in the same app (a new friend) does it too, another board does not; a friend in another
// app moves it only while it sends for no app or when it is the chosen friend, and its name beacon while a lobby of this
// board waits; LEARN A FRIEND (link_meet_learn) makes a
// LEARN packet of a board that is not a friend a peer, but not its name beacon; link_set_channel() changes nothing; the
// hold after a packet of a peer; a board on an AP and a QUIET link move nothing; the answer to a new peer; the wait of a
// lobby (friends_lobby_waits, friends_lobby_age); the radio invite (the app in the name beacon of a lobby that waits,
// the listen of the menu, one notice for each friend and app in FRIENDS_RADIO_GAP_MS); the reads of the channel of the
// radio (on an AP at most one each 500 ms; one when the radio can move again).
// Build and run: python tests/unit/run.py.
#include <stdio.h>
#include <string.h>
#include "fake_board.h"
#include "friends.h"
#include "lan.h"
#include "link.h"
#include "link_meet.h"
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

#define CAP_N 64
static struct {
    int kind;
    int n;
    uint8_t p[260];
} s_cap[CAP_N];
static int s_ncap;

static void on_out(int kind, uint32_t ip, const uint8_t *p, int n)
{
    (void)ip;
    if (s_ncap < CAP_N) {
        s_cap[s_ncap].kind = kind;
        s_cap[s_ncap].n = n;
        memcpy(s_cap[s_ncap].p, p, (size_t)n);
        s_ncap++;
    }
}

// the ESP-NOW frames of app with the channel byte (bits 0-3) ch since the last reset
static int sent_on(uint8_t app, int ch)
{
    int k = 0;
    for (int i = 0; i < s_ncap; i++) {
        k += s_cap[i].kind == 'E' && s_cap[i].p[3] == app && (s_cap[i].p[6] & 0x0F) == ch;
    }
    return k;
}

#define USE_WAIT_MS 3100  // (more than the 3 s of a send of an app in link.c: the app is not "sent for" after it)
static const uint8_t ME[6] = {0x02, 0, 0, 0, 0, 0x01};
static const uint8_t FRIEND[6] = {0x02, 0, 0, 0, 0, 0x02};
static const uint8_t STRANGER[6] = {0x0a, 0, 0, 0, 0, 0x20};
static uint16_t s_id = 500;

static void ms(int t)
{
    fk_now_us += (int64_t)t * 1000;
}

// a v3 frame of app from mac by radio, byte 6 = chb
static void in(const uint8_t mac[6], uint8_t app, uint8_t chb)
{
    const uint16_t id = s_id++;
    uint8_t f[11] = {'A', 'L', 3, app, 1, 2, chb, (uint8_t)id, (uint8_t)(id >> 8), 0x11, 0x22};
    fk_espnow_in(mac, f, sizeof f, -40);
}

static void poll(void)
{
    link_msg_t m;
    while (link_recv(&m)) {
    }
}

static void test_search(void)
{
    fk_wifi = 0;  // (no AP)
    fk_channel = 3;
    CHECK(link_start());
    poll();
    CHECK(fk_channel == LINK_MEET_CH);  // (at once to the meeting channel)
    s_ncap = 0;
    CHECK(link_send('Z', 1, "hi", 2));
    friends_beacon(fk_now_us / 1000);
    CHECK(sent_on('Z', LINK_MEET_CH) == 1 && sent_on(FRIENDS_LINK_APP, LINK_MEET_CH) == 1);
    CHECK(s_ncap == 2);
    uint8_t first[2][16];
    int first_n[2];
    for (int i = 0; i < 2; i++) {
        first_n[i] = s_cap[i].n;
        memcpy(first[i], s_cap[i].p, (size_t)(s_cap[i].n < 16 ? s_cap[i].n : 16));
    }
    ms(LINK_MEET_WAIT_MS - 10);
    poll();
    CHECK(fk_channel == LINK_MEET_CH);
    s_ncap = 0;
    ms(LINK_MEET_JITTER + 10);
    poll();
    CHECK(fk_channel == 3);  // (the first visit: its own last channel)
    // (the last packets again on channel 3: the same frames, so the other board drops a second copy)
    CHECK(s_ncap == 2);
    for (int i = 0; i < 2 && i < s_ncap; i++) {
        CHECK(s_cap[i].n == first_n[i] && !memcmp(s_cap[i].p, first[i], (size_t)(first_n[i] < 16 ? first_n[i] : 16)));
    }
    ms(LINK_MEET_VISIT_MS);
    poll();
    CHECK(fk_channel == LINK_MEET_CH);
    // a packet older than 1.5 s does not go again
    ms(2000);
    s_ncap = 0;
    poll();
    CHECK(fk_channel != LINK_MEET_CH && s_ncap == 0);
    // link_set_channel of an app changes nothing
    const int was = fk_channel;
    CHECK(link_set_channel(9) && fk_channel == was);
    CHECK(!link_set_channel(0) && !link_set_channel(14));
}

static void in_kind(const uint8_t mac[6], uint8_t app, uint8_t kind, uint8_t chb);

static void test_agree(void)
{
    // a friend on an AP on channel 6 (byte 6 = 6 + CH_WIFI) in the same app (a match): this board goes there and stays
    // there
    CHECK(link_send('Z', 1, "hi", 2));
    in(FRIEND, 'Z', 0x80 | 6);
    poll();
    CHECK(fk_channel == 6);
    ms(2000);
    CHECK(link_send('Z', 1, "hi", 2));  // (a match sends all the time: no visit in the hold)
    ms(LINK_MEET_HOLD_MS - 2100);
    poll();
    CHECK(fk_channel == 6);  // (the hold)
    ms(200);
    poll();
    CHECK(fk_channel != 6);  // (no packet for the hold time: the search again)
    // another board in another app (not a friend, not an app of this board): no move, no hold
    link_force_channel(4);
    poll();
    in(STRANGER, 'Q', 0x80 | 9);
    poll();
    CHECK(fk_channel != 9);
    // a board in the same app (a new friend): it counts
    CHECK(link_send('Z', 1, "hi", 2));
    in(STRANGER, 'Z', 0x80 | 9);
    poll();
    CHECK(fk_channel == 9);
    // this board sends for an app: a friend in another app does not count (other pairs near it), the chosen friend does
    ms(LINK_MEET_HOLD_MS + 10);
    link_force_channel(LINK_MEET_CH);
    CHECK(link_send('Z', 1, "hi", 2));
    in(FRIEND, 'Q', 0x80 | 11);
    poll();
    CHECK(fk_channel != 11);
    friends_choose(friends_find(FRIEND));
    in(FRIEND, 'Q', 0x80 | 11);
    poll();
    CHECK(fk_channel == 11);
    friends_choose(-1);
    // the name beacon of a friend (a title, on an AP on channel 3): a peer only while a lobby of this board waits
    ms(LINK_MEET_HOLD_MS + 10);
    link_force_channel(LINK_MEET_CH);
    CHECK(link_send('Z', 1, "hi", 2));
    in_kind(FRIEND, FRIENDS_LINK_APP, FRIENDS_K_NAME, 0x80 | 3);
    poll();
    CHECK(fk_channel != 3);
    friends_invite(fk_now_us / 1000);
    in_kind(FRIEND, FRIENDS_LINK_APP, FRIENDS_K_NAME, 0x80 | 3);
    poll();
    CHECK(fk_channel == 3);
    ms(FRIENDS_WAIT_MS + 10);
    ms(USE_WAIT_MS);
    // two boards with no AP: to the meeting channel
    ms(LINK_MEET_HOLD_MS + 10);
    link_force_channel(7);
    in(FRIEND, 'Q', 7);
    poll();
    CHECK(fk_channel == LINK_MEET_CH);
    // a QUIET link: no search (the radio sleeps)
    ms(LINK_MEET_HOLD_MS + 10);
    link_force_channel(5);
    link_quiet(true);
    poll();
    CHECK(fk_channel == 5);
    link_quiet(false);
}

static void test_ap(void)
{
    // this board on an AP: no search and no agreement
    fk_wifi = 2;
    fk_channel = 4;
    link_stop();
    CHECK(link_start());
    for (int i = 0; i < 20; i++) {
        ms(500);
        poll();
    }
    CHECK(fk_channel == 4);
    in(FRIEND, 'Q', 0x80 | 11);
    in(FRIEND, 'Q', 2);
    poll();
    CHECK(fk_channel == 4);
    // the answer: a friend with no AP that is new (no packet for 1 s) gets the last packets again at once (it can be
    // on a visit of its search); a second packet in the same second does not
    CHECK(link_send('Z', 1, "hi", 2));
    ms(LINK_MEET_ANSWER_MS + 10);
    s_ncap = 0;
    in(FRIEND, 'Z', 4);
    poll();
    CHECK(sent_on('Z', 4) == 1);
    s_ncap = 0;
    ms(400);
    in(FRIEND, 'Z', 4);
    poll();
    CHECK(sent_on('Z', 4) == 0);
    link_stop();
    fk_wifi = 0;
}

static void test_lobby_wait(void)
{
    CHECK(!friends_lobby_waits() && friends_lobby_age() == 0);
    friends_invite(1);  // (the clock of the app is not the clock of the wait)
    CHECK(friends_lobby_waits() && friends_lobby_age() == 0);
    ms(1000);
    friends_invite(2);
    CHECK(friends_lobby_waits() && friends_lobby_age() == 1000);
    ms(FRIENDS_WAIT_MS + 10);
    CHECK(!friends_lobby_waits() && friends_lobby_age() == 0);
    friends_invite(3);
    CHECK(friends_lobby_waits() && friends_lobby_age() == 0);
    ms(FRIENDS_WAIT_MS + 10);
}

// a frame of app with kind from mac by radio, byte 6 = chb
static void in_kind(const uint8_t mac[6], uint8_t app, uint8_t kind, uint8_t chb)
{
    const uint16_t id = s_id++;
    uint8_t f[13] = {'A', 'L', 3, app, kind, 4, chb, (uint8_t)id, (uint8_t)(id >> 8), 'N', 'E', 'W', 0};
    fk_espnow_in(mac, f, sizeof f, -40);
}

static void test_learn(void)
{
    // LEARN A FRIEND: a LEARN packet of a board that is not a friend yet (on an AP on channel 6) takes this board there
    // and holds the search; with LEARN closed it does not; the name beacon (kind 3) never does
    fk_wifi = 0;
    fk_channel = 4;
    CHECK(link_start());
    poll();
    CHECK(fk_channel == LINK_MEET_CH);
    in_kind(STRANGER, FRIENDS_LINK_APP, 1, 0x80 | 6);
    poll();
    CHECK(fk_channel == LINK_MEET_CH);
    link_meet_learn(1);
    in_kind(STRANGER, FRIENDS_LINK_APP, 3, 0x80 | 6);
    poll();
    CHECK(fk_channel == LINK_MEET_CH);
    in_kind(STRANGER, FRIENDS_LINK_APP, 1, 0x80 | 6);
    poll();
    CHECK(fk_channel == 6);
    for (int i = 0; i < 6; i++) {
        ms(500);
        in_kind(STRANGER, FRIENDS_LINK_APP, 1, 0x80 | 6);
        poll();
        CHECK(fk_channel == 6);  // (a LEARN board on an AP: the pair stays, also with the visits of LINK_MEET_F_SEEK)
        ms(LINK_MEET_VISIT_MS + 10);
        poll();
    }
    // two LEARN boards with no AP: they stay on the meeting channel
    link_force_channel(LINK_MEET_CH);
    in_kind(STRANGER, FRIENDS_LINK_APP, 1, LINK_MEET_CH);
    poll();
    CHECK(fk_channel == LINK_MEET_CH);
    // link_stop ends LEARN: the next link does not take the LEARN packets
    link_stop();
    CHECK(link_start());
    poll();
    in_kind(STRANGER, FRIENDS_LINK_APP, 1, 0x80 | 9);
    poll();
    CHECK(fk_channel != 9);
    link_stop();
}

// a FRIENDS frame (kind, data d of n bytes) from mac by radio (rssi != 0) or by the LAN (rssi 0)
static void in_data(const uint8_t mac[6], uint8_t kind, uint8_t chb, const void *d, int n, int rssi)
{
    const uint16_t id = s_id++;
    uint8_t f[64] = {'A', 'L', 3, FRIENDS_LINK_APP, kind, (uint8_t)n, chb, (uint8_t)id, (uint8_t)(id >> 8)};
    memcpy(f + 9, d, (size_t)n);
    fk_espnow_in(mac, f, 9 + n, rssi);
}

// the data of the last name beacon that this board sent (NULL = none); *n = its length
static const uint8_t *last_beacon(int *n)
{
    for (int i = s_ncap - 1; i >= 0; i--) {
        if (s_cap[i].kind == 'E' && s_cap[i].p[3] == FRIENDS_LINK_APP && s_cap[i].p[4] == FRIENDS_K_NAME) {
            *n = s_cap[i].p[5];
            return s_cap[i].p + 9;
        }
    }
    return NULL;
}

static char s_notice[200];
static void on_log(const char *line)
{
    if (!strncmp(line, "notice ", 7)) {
        snprintf(s_notice, sizeof s_notice, "%s", line);
    }
}

static void test_radio_invite(void)
{
    const char *my = friends_my_name();
    const int nl = (int)strlen(my);
    // the name beacon of a lobby that waits: the name, a 0, the menu name of the app
    fk_wifi = 4;
    fk_channel = LINK_MEET_CH;
    fk_app_name = "PONG";
    CHECK(link_start());
    poll();
    ms(1100);
    friends_invite(fk_now_us / 1000);
    s_ncap = 0;
    friends_beacon(fk_now_us / 1000);
    int n = 0;
    const uint8_t *b = last_beacon(&n);
    CHECK(b && n == nl + 1 + 4 && !memcmp(b, my, (size_t)nl) && b[nl] == 0 && !memcmp(b + nl + 1, "PONG", 4));
    // no wait of a lobby (a title): the name only
    ms(2000);
    s_ncap = 0;
    friends_beacon(fk_now_us / 1000);
    b = last_beacon(&n);
    CHECK(b && n == nl);
    // WALKIE: no invite in the beacon
    fk_app_name = "WALKIE";
    ms(1100);
    friends_invite(fk_now_us / 1000);
    s_ncap = 0;
    friends_beacon(fk_now_us / 1000);
    b = last_beacon(&n);
    CHECK(b && n == nl);
    link_stop();
    ms(2000);

    // THE LISTEN OF THE MENU (no AP): a radio invite of a friend comes in; a plain beacon, a stranger or the LAN do not
    fk_app_name = NULL;
    uint8_t mac[6];
    char app[FRIENDS_APP_NAME];
    while (friends_radio_invite_take(mac, app)) {
    }
    link_listen(true);
    in_data(STRANGER, FRIENDS_K_NAME, LINK_MEET_CH, "X\0PONG", 6, -40);
    CHECK(!friends_radio_invite_take(mac, app));
    in_data(FRIEND, FRIENDS_K_NAME, LINK_MEET_CH, "FRIEND", 6, -40);
    CHECK(!friends_radio_invite_take(mac, app));
    in_data(FRIEND, FRIENDS_K_NAME, LINK_MEET_CH, "FRIEND\0PONG", 11, 0);
    CHECK(!friends_radio_invite_take(mac, app));
    in_data(FRIEND, FRIENDS_K_NAME, LINK_MEET_CH, "FRIEND\0PONG", 11, -40);
    CHECK(friends_radio_invite_take(mac, app) && !memcmp(mac, FRIEND, 6) && !strcmp(app, "PONG"));
    CHECK(!friends_radio_invite_take(mac, app));  // (taken)
    // one notice for each friend and app in FRIENDS_RADIO_GAP_MS
    ms(1000);
    in_data(FRIEND, FRIENDS_K_NAME, LINK_MEET_CH, "FRIEND\0PONG", 11, -40);
    CHECK(!friends_radio_invite_take(mac, app));
    in_data(FRIEND, FRIENDS_K_NAME, LINK_MEET_CH, "FRIEND\0CHESS", 12, -40);
    CHECK(friends_radio_invite_take(mac, app) && !strcmp(app, "CHESS"));
    ms(FRIENDS_RADIO_GAP_MS);
    in_data(FRIEND, FRIENDS_K_NAME, LINK_MEET_CH, "FRIEND\0PONG", 11, -40);
    CHECK(friends_radio_invite_take(mac, app) && !strcmp(app, "PONG"));
    // the notice opens the app at its friends lobby; none for an app that is open
    fk_log = on_log;
    s_notice[0] = 0;
    lan_invite_notice(mac, app);
    CHECK(strstr(s_notice, "PONG|FRIEND WANTS TO PLAY WITH YOU") && strstr(s_notice, "|friends"));
    s_notice[0] = 0;
    fk_app_name = "PONG";
    lan_invite_notice(mac, app);
    CHECK(!s_notice[0]);
    fk_app_name = NULL;
    fk_log = NULL;
    // on an AP (the radio sleeps between the beacons of the AP): no listen
    fk_wifi = 2;
    link_listen(true);
    ms(FRIENDS_RADIO_GAP_MS);
    in_data(FRIEND, FRIENDS_K_NAME, LINK_MEET_CH, "FRIEND\0PONG", 11, -40);
    CHECK(!friends_radio_invite_take(mac, app));
    // the link of an app takes ESP-NOW from the listen
    fk_wifi = 4;
    link_listen(true);
    CHECK(link_start());
    link_stop();
    in_data(FRIEND, FRIENDS_K_NAME, LINK_MEET_CH, "FRIEND\0PONG", 11, -40);
    CHECK(!friends_radio_invite_take(mac, app));
    link_listen(false);
}

static void test_ch_read(void)
{
    // on an AP: the channel of the radio once, then at most each 500 ms (a frame of an app calls link_recv many times)
    fk_wifi = 2;
    fk_channel = 6;
    CHECK(link_start());
    fk_get_ch_n = 0;
    for (int i = 0; i < 40; i++) {
        poll();
    }
    CHECK(fk_get_ch_n <= 1);
    ms(600);
    for (int i = 0; i < 40; i++) {
        poll();
    }
    CHECK(fk_get_ch_n <= 2);
    // a connect (the radio cannot move; a scan moves it), then the connect fails: one read when the radio can move
    fk_wifi = 1;
    fk_channel = 9;
    poll();
    const int n0 = fk_get_ch_n;
    fk_wifi = 4;
    poll();
    CHECK(fk_get_ch_n == n0 + 1);
    for (int i = 0; i < 40; i++) {
        poll();
    }
    CHECK(fk_get_ch_n == n0 + 1);
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
    friends_add(FRIEND, "FRIEND");
    lan_start();
    test_search();
    test_agree();
    test_ap();
    test_lobby_wait();
    test_learn();
    test_radio_invite();
    test_ch_read();
    printf("LINK CHANNEL %s: %d checks, %d failed\n", s_fail ? "FAIL" : "OK", s_n, s_fail);
    return s_fail ? 1 : 0;
}
