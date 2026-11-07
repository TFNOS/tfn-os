// 2 BOARDS for the smaller games (see net2.h).
//
// The packets (link.c, kinds):
//   1 HELLO   {version, info u16, the friends list (count u8, MACs), speed u8, flags u8}
//             version 2 adds the speed and the flags after the friends list. A version 1 HELLO (an
//             old board, the manager as a WALKIE friend) has none: then the lower MAC is the HOST, so
//             a new board and an old board still agree.
//   2 STATE   {host_ms u32, n u8, the state of the app (n bytes), last u16, count u8,
//              then count events: {len u8, bytes}}
//   3 BYE
//   4 DUO     {down u8, presses u8, down_ms u32, up_ms u32}  (times of the HOST)
//   8..       the packets of the app

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "gfx.h"
#include "link.h"
#include "friends.h"
#include "wifi.h"
#include "net2.h"
#include "tfn/theme.h"

static const char *N2TAG = "net2";

enum {
    N2K_HELLO = 1,
    N2K_STATE = 2,
    N2K_BYE = 3,
    N2K_DUO = 4
};
enum {
    N2EV_SFX = 1,
    N2EV_TONE = 2
};
#define N2_LOST_MS   3000
#define N2_EVQ_N     32
#define N2_EV_MAX    24
#define N2_HELLO_VER 2
// The speed class of this board in the HELLO: the faster board is the HOST, because the HOST runs
// the game (a CoreS3 host ran PUCK at 85% of the real time and the client puck jumped 11.6 times a second). The
// order of the frame speed: 3.49 > AMOLED > CoreS3 > Watcher. Equal classes: the lower MAC is the HOST.
// 0 = not known (a version 1 HELLO): the lower MAC is the HOST.
#ifndef N2_SPEED
#define N2_SPEED BOARD_NET_SPEED
#endif
// the flags of the HELLO
#define N2HF_WAIT 1  // (A6) this board is the CLIENT and waits for the first state: the HOST must not end

static uint8_t n2s_app;
static uint16_t n2s_info, n2s_peer_info;
static n2_role_t n2s_role;
static bool n2s_fail;
static uint8_t n2s_peer[6];
static int64_t n2s_seen, n2s_paired_at, n2s_hello_at, n2s_lost_at;
static bool n2s_paired_ev;
// the CLIENT
static bool n2s_have;                       // a state came
static uint16_t n2s_ev_done;                // the last event that was played
static int64_t n2s_last_host, n2s_last_rx;  // the last state: its HOST time, the local time it came
// the HOST: the events
typedef struct {
    uint16_t seq;
    uint32_t frame;
    uint8_t n;
    uint8_t b[N2_EV_MAX];
} n2_ev_t;
static n2_ev_t n2s_evq[N2_EVQ_N];
static uint16_t n2s_ev_seq;
static uint32_t n2s_frame;
static bool n2s_ev_on;
// the HOST: the button of the CLIENT
typedef struct __attribute__((packed)) {
    uint8_t down, presses;
    uint32_t down_ms, up_ms;
} n2_duo_t;
static n2_duo_t n2s_duo;
static bool n2s_duo_have;
static uint8_t n2s_duo_seen;
// the CLIENT: the presses of its button (net2_duo_send; a new count at each pairing)
static uint8_t n2s_presses;
// the health of the link in a game (a log line each 5 s)
static uint32_t n2s_rx, n2s_tx0, n2s_fail0;
static int64_t n2s_stat_at, n2s_gap_max, n2s_rx_last;

static int64_t n2_now(void)
{
    return esp_timer_get_time() / 1000;
}

void net2_open(uint8_t app_id, uint16_t info)
{
    friends_choose(-1);
    n2s_app = app_id;
    n2s_info = info;
    n2s_role = N2_LOBBY;
    n2s_fail = !link_start();
    n2s_hello_at = 0;
    n2s_lost_at = 0;
    n2s_have = false;
    n2s_duo_have = false;
    n2s_ev_on = false;
}

void net2_close(void)
{
    // BYE two times: one lost packet made the other board wait 3 s for the end (duel and TEAM BUILDING send two)
    if (n2s_role == N2_HOST || n2s_role == N2_CLIENT) {
        link_send(n2s_app, N2K_BYE, NULL, 0);
        link_send(n2s_app, N2K_BYE, NULL, 0);
    }
    if (n2s_role != N2_OFF) {
        link_stop();
    }
    n2s_role = N2_OFF;
    n2s_ev_on = false;
}

static bool n2s_bye;  // the last end of a link was a BYE
bool net2_last_bye(void)
{
    return n2s_bye;
}
const uint8_t *net2_peer(void)
{
    return n2s_peer;
}
n2_role_t net2_role(void)
{
    return n2s_role;
}
bool net2_failed(void)
{
    return n2s_fail;
}
uint16_t net2_peer_info(void)
{
    return n2s_peer_info;
}

static void n2_hello(void)
{
    uint8_t h[3 + FRIENDS_HELLO_MAX + 2] = {N2_HELLO_VER, (uint8_t)(n2s_info & 255), (uint8_t)(n2s_info >> 8)};
    int n = friends_hello_list(h + 3);  // FRIENDS: the boards that this board accepts
    // after the list (an old board reads only the list): the speed class, the flags
    h[3 + n] = (uint8_t)N2_SPEED;
    h[3 + n + 1] = (uint8_t)(n2s_role == N2_CLIENT && !n2s_have ? N2HF_WAIT : 0);
    link_send(n2s_app, N2K_HELLO, h, 3 + n + 2);
}

// the speed class and the flags of a HELLO (0, 0: a version 1 HELLO)
static void n2_hello_tail(const link_msg_t *m, int *speed, int *flags)
{
    *speed = *flags = 0;
    if (m->len < 4 || m->data[0] < 2) {
        return;
    }
    int t = 3 + 1 + 6 * m->data[3];
    if (t + 2 > m->len) {
        return;
    }
    *speed = m->data[t];
    *flags = m->data[t + 1];
}

// The rule of the HOST: the faster class, then the lower MAC. Both boards use the same rule on the HELLO of the other
// board, so they agree. With a board of speed 0 (an old board: it uses the MAC rule) the MAC decides.
static bool n2_host_rule(const uint8_t me[6], const uint8_t peer[6], int peer_speed)
{
    if (peer_speed && peer_speed != N2_SPEED) {
        return N2_SPEED > peer_speed;
    }
    return memcmp(me, peer, 6) < 0;
}

static void n2_pair(const uint8_t *mac, bool host, int64_t now)
{
    ESP_LOGI(N2TAG, "2 BOARDS (%c): %s, the other board %02X:%02X:%02X, channel %d", n2s_app, host ? "HOST" : "CLIENT",
             mac[3], mac[4], mac[5], link_channel());
    memcpy(n2s_peer, mac, 6);
    n2s_role = host ? N2_HOST : N2_CLIENT;
    n2s_seen = n2s_paired_at = now;
    n2s_have = false;
    n2s_hello_at = 0;
    n2s_duo_have = false;
    n2s_duo_seen = 0;
    n2s_presses = 0;
    n2s_paired_ev = true;
    n2s_stat_at = 0;
    n2s_rx_last = 0;
}

static void n2_lost(void)
{
    ESP_LOGI(N2TAG, "2 BOARDS (%c): the link ended (the last packet %d ms ago)", n2s_app, (int)(n2_now() - n2s_seen));
    net2_close();
    n2s_lost_at = n2_now();
    if (!n2s_lost_at) {
        n2s_lost_at = 1;
    }
}

// the CLIENT: one state packet (the state for the app, then the events)
static void n2_client_state(const link_msg_t *m, int64_t now, n2_rx_fn rx, n2_ev_fn evf)
{
    if (m->len < 5) {
        return;
    }
    uint32_t hms;
    memcpy(&hms, m->data, 4);
    int n = m->data[4];
    if (5 + n + 3 > m->len) {
        return;
    }
    // A state can come late, after a newer one (the LAN copy of a packet whose ESP-NOW copy was
    // lost). An older state does not go to the app (the puck would jump back) and does not change the time of the HOST
    // (net2_host_ms); its new events play below.
    bool old = n2s_have && (int32_t)(hms - (uint32_t)n2s_last_host) < 0;
    if (!old) {
        n2s_last_host = hms;
        n2s_last_rx = m->rx_ms ? m->rx_ms : now;
        if (rx) {
            rx(N2K_STATE, m->data + 5, n);
        }
    }
    const uint8_t *e = m->data + 5 + n;
    uint16_t last = (uint16_t)(e[0] | e[1] << 8);
    int cnt = e[2], p = 5 + n + 3;
    if (!n2s_have) {
        n2s_ev_done = (uint16_t)(last - cnt);
        n2s_have = true;
    }
    for (int k = 0; k < cnt; k++) {
        if (p >= m->len) {
            break;
        }
        int len = m->data[p];
        if (len < 1 || len > N2_EV_MAX || p + 1 + len > m->len) {
            break;
        }
        const uint8_t *b = m->data + p + 1;
        uint16_t seq = (uint16_t)(last - cnt + 1 + k);
        if ((int16_t)(seq - n2s_ev_done) > 0) {
            n2s_ev_done = seq;
            if (b[0] == N2EV_SFX && len >= 2) {
                sfx_play((sfx_t)b[1]);
            } else if (b[0] == N2EV_TONE && len >= 9) {
                uint16_t f0, f1, ms;
                memcpy(&f0, b + 1, 2);
                memcpy(&f1, b + 3, 2);
                memcpy(&ms, b + 5, 2);
                sfx_tone(f0, f1, ms, (wave_t)b[7], b[8]);
            } else if (b[0] >= 16 && evf) {
                evf(b, len);
            }
        }
        p += 1 + len;
    }
}

// a message to a board in the lobby: a call of a friend, the choice of the HOST, the pairing
static void n2_lobby_msg(const link_msg_t *m, const uint8_t me[6], int64_t now, n2_rx_fn rx, n2_ev_fn evf)
{
    if (m->kind != N2K_HELLO && m->kind != N2K_STATE) {
        return;
    }
    // FRIENDS: only the chosen friend (or any friend); with two or more friends near and
    // none chosen, the user picks one first (net2_draw_lobby shows them)
    // WALKIE with nobody chosen: a HELLO that names only this board is a call from that friend: take it
    // (or a TALKIE: the "auto" flag 0x8000 in its info, and its list has this board)
    if (m->kind == N2K_HELLO && friends_chosen() == FRIENDS_NOBODY && m->len >= 10) {
        bool call = m->data[3] == 1 && !memcmp(m->data + 4, me, 6);
        if (!call && (m->data[2] & 0x80)) {
            for (int k = 0; k < m->data[3] && 4 + 6 * (k + 1) <= m->len; k++) {
                if (!memcmp(m->data + 4 + 6 * k, me, 6)) {
                    call = true;
                }
            }
        }
        int fi = call ? friends_find(m->mac) : -1;
        if (fi >= 0) {
            friends_choose(fi);
        }
    }
    if (!net2_lobby_accept(m->mac)) {
        return;
    }
    if (m->kind == N2K_HELLO && (m->len < 3 || !friends_hello_ok(m->data + 3, m->len - 3))) {
        return;  // it does not accept this board
    }
    if (m->kind == N2K_HELLO && m->len >= 3) {
        n2s_peer_info = (uint16_t)(m->data[1] | m->data[2] << 8);
    }
    // the faster board is the HOST (n2_host_rule); a board that sends states already plays as the HOST
    int sp, fl;
    n2_hello_tail(m, &sp, &fl);
    bool host = m->kind == N2K_HELLO && n2_host_rule(me, m->mac, sp);
    n2_pair(m->mac, host, now);
    if (!host && m->kind == N2K_STATE) {
        n2_client_state(m, now, rx, evf);
    }
}

n2_event_t net2_poll(int64_t now, n2_rx_fn rx, n2_ev_fn evf)
{
    if (n2s_role == N2_OFF || n2s_fail) {
        return N2_EV_NONE;
    }
    n2s_frame++;
    uint8_t me[6];
    link_my_mac(me);
    if (n2s_role == N2_LOBBY) {
        if (now - n2s_hello_at > 250) {
            n2s_hello_at = now;
            n2_hello();
        }
        friends_beacon(now);  // FRIENDS: the name, for the ADD badge
        friends_invite(now);  // (a notice on the boards of the friends that are not in the app)
    }
    n2s_paired_ev = false;
    link_msg_t m;
    int guard = 0;
    while (guard++ < 32 && link_recv(&m)) {
        if (m.app != n2s_app || !memcmp(m.mac, me, 6)) {
            continue;
        }
        if (n2s_role == N2_LOBBY) {
            n2_lobby_msg(&m, me, now, rx, evf);
            continue;
        }
        if (memcmp(m.mac, n2s_peer, 6)) {
            continue;
        }
        if (m.rx_ms > n2s_seen) {
            n2s_seen = m.rx_ms;
        }
        n2s_rx++;
        if (n2s_rx_last && m.rx_ms - n2s_rx_last > n2s_gap_max) {
            n2s_gap_max = m.rx_ms - n2s_rx_last;
        }
        if (m.rx_ms > n2s_rx_last) {
            n2s_rx_last = m.rx_ms;
        }
        if (m.kind == N2K_BYE) {
            n2_lost();
            n2s_bye = true;
            return N2_EV_LOST;
        }
        if (m.kind == N2K_HELLO) {
            if (m.len >= 3) {
                n2s_peer_info = (uint16_t)(m.data[1] | m.data[2] << 8);
            }
            // the HOST: a HELLO of the CLIENT before its first state is normal; later it means
            // that the other board is in the lobby again. The CLIENT: the HOST is in the lobby.
            if (n2s_role == N2_CLIENT) {
                // a HELLO that the HOST sent before it paired can come after the pairing (the LAN, the VPN): the same
                // grace of 1 s as the HOST has, until the first state
                if (!n2s_have && now - n2s_paired_at <= 1000) {
                    continue;
                }
                n2s_role = N2_LOBBY;
                n2s_hello_at = 0;
                n2s_have = false;
                return N2_EV_PEER_LOBBY;
            }
            // A6: the CLIENT says "paired, I wait for the first state" (its first states were lost): the match goes on
            int sp, fl;
            n2_hello_tail(&m, &sp, &fl);
            if (fl & N2HF_WAIT) {
                continue;
            }
            if (now - n2s_paired_at > 1000) {
                return N2_EV_PEER_LOBBY;
            }
            continue;
        }
        if (n2s_role == N2_CLIENT && m.kind == N2K_STATE) {
            n2_client_state(&m, now, rx, evf);
        } else if (n2s_role == N2_HOST && m.kind == N2K_DUO && m.len >= sizeof(n2_duo_t)) {
            memcpy(&n2s_duo, m.data, sizeof n2s_duo);
            n2s_duo_have = true;
        } else if (m.kind >= N2_K_APP && rx) {
            rx(m.kind, m.data, m.len);
        }
    }
    if ((n2s_role == N2_HOST || n2s_role == N2_CLIENT) && now - n2s_seen > N2_LOST_MS) {
        n2_lost();
        n2s_bye = false;
        return N2_EV_LOST;
    }
    if ((n2s_role == N2_HOST || n2s_role == N2_CLIENT) && now - n2s_stat_at >= 5000) {
        uint32_t tx, fail;
        link_stats(&tx, &fail);
        if (n2s_stat_at) {
            ESP_LOGI(N2TAG,
                     "2 BOARDS (%c): in 5 s %u packets from the other board (the longest gap %d ms), %u sent, %u "
                     "refused, channel %d",
                     n2s_app, (unsigned)n2s_rx, (int)n2s_gap_max, (unsigned)(tx - n2s_tx0),
                     (unsigned)(fail - n2s_fail0), link_channel());
        }
        n2s_stat_at = now;
        n2s_rx = 0;
        n2s_gap_max = 0;
        n2s_tx0 = tx;
        n2s_fail0 = fail;
    }
    // the CLIENT: HELLO until the first state (the HOST can miss the HELLO of the lobby)
    if (n2s_role == N2_CLIENT && !n2s_have && now - n2s_hello_at > 250) {
        n2s_hello_at = now;
        n2_hello();
    }
    return n2s_paired_ev ? N2_EV_PAIRED : N2_EV_NONE;
}

void net2_send_state(const void *data, int len, int64_t now)
{
    if (n2s_role != N2_HOST || len < 0 || len > N2_STATE_MAX) {
        return;
    }
    uint8_t buf[LINK_DATA_MAX];
    uint32_t hms = (uint32_t)now;
    memcpy(buf, &hms, 4);
    buf[4] = (uint8_t)len;
    memcpy(buf + 5, data, (size_t)len);
    int n = 5 + len;
    // the events of the last 3 frames (the newest ones if they do not all fit)
    int room = LINK_DATA_MAX - n - 3, used = 0, first = 0;
    for (int k = 0; k < N2_EVQ_N && n2s_ev_seq; k++) {
        const n2_ev_t *e = &n2s_evq[(uint16_t)(n2s_ev_seq - k) % N2_EVQ_N];
        if (e->seq != (uint16_t)(n2s_ev_seq - k) || n2s_frame - e->frame > 2 || used + 1 + e->n > room) {
            break;
        }
        used += 1 + e->n;
        first = k + 1;
    }
    buf[n] = (uint8_t)(n2s_ev_seq & 255);
    buf[n + 1] = (uint8_t)(n2s_ev_seq >> 8);
    buf[n + 2] = (uint8_t)first;
    n += 3;
    for (int k = first - 1; k >= 0; k--) {
        const n2_ev_t *e = &n2s_evq[(uint16_t)(n2s_ev_seq - k) % N2_EVQ_N];
        buf[n++] = e->n;
        memcpy(buf + n, e->b, e->n);
        n += e->n;
    }
    link_send(n2s_app, N2K_STATE, buf, n);
}

void net2_send(uint8_t kind, const void *data, int len)
{
    if (n2s_role == N2_HOST || n2s_role == N2_CLIENT) {
        link_send(n2s_app, kind, data, len);
    }
}

int64_t net2_host_ms(int64_t local_ms)
{
    if (n2s_role != N2_CLIENT || !n2s_have) {
        return local_ms;
    }
    return n2s_last_host + (local_ms - n2s_last_rx);
}

int64_t net2_local_ms(int64_t host_ms)
{
    if (n2s_role != N2_CLIENT || !n2s_have) {
        return host_ms;
    }
    return host_ms - n2s_last_host + n2s_last_rx;
}

void net2_rec(bool on)
{
    n2s_ev_on = on && n2s_role == N2_HOST;
}

void net2_ev(const uint8_t *b, int n)
{
    if (!n2s_ev_on || n < 1 || n > N2_EV_MAX) {
        return;
    }
    n2_ev_t *e = &n2s_evq[++n2s_ev_seq % N2_EVQ_N];
    e->seq = n2s_ev_seq;
    e->frame = n2s_frame;
    e->n = (uint8_t)n;
    memcpy(e->b, b, (size_t)n);
}

void net2_sfx(sfx_t id)
{
    sfx_play(id);
    uint8_t b[2] = {N2EV_SFX, (uint8_t)id};
    net2_ev(b, 2);
}

void net2_tone(uint16_t f0, uint16_t f1, uint16_t ms, wave_t w, uint8_t vol)
{
    sfx_tone(f0, f1, ms, w, vol);
    uint8_t b[9] = {N2EV_TONE};
    memcpy(b + 1, &f0, 2);
    memcpy(b + 3, &f1, 2);
    memcpy(b + 5, &ms, 2);
    b[7] = (uint8_t)w;
    b[8] = vol;
    net2_ev(b, 9);
}

void net2_duo_send(const duo_btn_t *mine)
{
    if (n2s_role != N2_CLIENT) {
        return;
    }
    if (mine->pressed) {
        n2s_presses++;
    }
    n2_duo_t p = {mine->down, n2s_presses, (uint32_t)net2_host_ms(mine->down_ms), (uint32_t)net2_host_ms(mine->up_ms)};
    link_send(n2s_app, N2K_DUO, &p, sizeof p);
}

void net2_duo_feed(duo_btn_t *b, int64_t now)
{
    if (n2s_role != N2_HOST || !n2s_duo_have) {
        return;
    }
    bool was = b->down;
    bool press = n2s_duo.presses != n2s_duo_seen;
    n2s_duo_seen = n2s_duo.presses;
    if (press) {
        b->pressed = true;
        b->down_ms = n2s_duo.down_ms;
    }
    b->down = n2s_duo.down != 0;
    if ((was || press) && !b->down) {
        b->released = true;
        b->up_ms = n2s_duo.up_ms;
    }
    int64_t h = b->down ? now - b->down_ms : b->up_ms - b->down_ms;
    b->held_ms = (int32_t)(h < 0 ? 0 : h > 600000 ? 600000 : h);
}

// the friend buttons of the lobby: two or more friends in this game and none chosen (friends_in_game: a friend that is
// near but in another app is not a choice)
static int lobby_friends(int *near)
{
    if (!friends_count() || friends_chosen() >= 0) {
        return 0;
    }
    int n = friends_in_game(near, FRIENDS_MAX);
    if (n > 4) {
        n = 4;
    }
    return n >= 2 ? n : 0;
}
// the text unit: 2 on a big native panel (gfx_native), else 1
#if BOARD_UI_TABLET
static int lobby_u(void)
{
    return 2;
}  // (the Tab5 frame 640 x 360: the text and the buttons 2x, as the AMOLED native frame)
#else
static int lobby_u(void)
{
    return gfx_native() ? 2 : 1;
}
#endif
// a narrow screen (the 3.49 in portrait: 172 px wide): long lines wrap, the buttons are one under the other
static bool lobby_narrow(void)
{
    return gfx_w < 240;
}
#define NARROW_BH  44  // the height of a button on a narrow screen (5.9 mm on the 3.49)
#define NARROW_GAP 10
static int lobby_box_h(int nf)
{
    if (lobby_narrow()) {
        return nf ? 46 + nf * (NARROW_BH + NARROW_GAP) : 100;
    }
    return (nf ? 90 : 70) * lobby_u();
}
static int lobby_box_y(int nf)
{
    if (lobby_narrow()) {
        return gfx_h / 2 - lobby_box_h(nf) / 2 - 40;
    }
    return gfx_h / 2 - (nf ? 54 : 44) * lobby_u();
}
static void lobby_button(int i, int n, int *x, int *y, int *w, int *h)
{
    if (lobby_narrow()) {
        *x = 20;
        *w = gfx_w - 40;
        *h = NARROW_BH;
        *y = lobby_box_y(n) + 46 + i * (NARROW_BH + NARROW_GAP);
        return;
    }
    int u = lobby_u(), bw = gfx_w - 40, by = lobby_box_y(n);
    *w = (bw - 20 - (n - 1) * 8) / n;
    *h = 34 * u;
    *x = 30 + i * (*w + 8);
    *y = by + 40 * u;
}
// centred text at the biggest scale (from scale down) that fits the width w
static void lobby_text(int x, int w, int y, const char *t, int scale, uint16_t c)
{
    while (scale > 1 && gfx_text_width(t, scale) > w - 8) {
        scale--;
    }
    gfx_text_center_in(x, w, y, t, scale, c);
}
// the ADD badges: the boards near that are not friends (at most 3), under the box
static int lobby_new(friend_new_t *nb)
{
    return friends_new_near(nb, 3);
}
static void lobby_badge(int i, int n, int nf, int *x, int *y, int *w, int *h)
{
    if (lobby_narrow()) {
        *x = 10;
        *w = gfx_w - 20;
        *h = NARROW_BH;
        *y = lobby_box_y(nf) + lobby_box_h(nf) + 6 + i * (NARROW_BH + 6);
        return;
    }
    int bw = gfx_w - 40;
    *w = (bw - (n - 1) * 8) / n;
    *h = 32 * lobby_u();
    *x = 20 + i * (*w + 8);
    *y = lobby_box_y(nf) + lobby_box_h(nf) + 6 * lobby_u();
}
static char n2s_added[FRIEND_NAME];
static int64_t n2s_added_at;
static bool n2s_added_full;  // the last ADD found the list full

bool net2_lobby_accept(const uint8_t mac[6])
{
    if (!friends_accept(mac)) {
        return false;
    }
    if (friends_count() && friends_chosen() < 0) {
        int near[FRIENDS_MAX];
        if (friends_in_game(near, FRIENDS_MAX) >= 2) {
            return false;
        }
    }
    return true;
}

bool net2_lobby_choosing(void)
{
    int near[FRIENDS_MAX];
    friend_new_t nb[3];
    return lobby_friends(near) > 0 || lobby_new(nb) > 0 || (n2s_added_at && n2_now() - n2s_added_at < 5000);
}

bool net2_lobby_tap(int x, int y)
{
    int near[FRIENDS_MAX], n = lobby_friends(near);
    for (int i = 0; i < n; i++) {
        int bx, by, bw, bh;
        lobby_button(i, n, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by - 6 && y < by + bh + 6) {
            friends_choose(near[i]);
            sfx_ui(SFX_TICK);
            return true;
        }
    }
    friend_new_t nb[3];
    int k = lobby_new(nb);
    for (int i = 0; i < k; i++) {
        int bx, by, bw, bh;
        lobby_badge(i, k, n, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by - 6 && y < by + bh + 8) {
            snprintf(n2s_added, sizeof n2s_added, "%s", nb[i].name);
            n2s_added_at = n2_now();
            n2s_added_full = !friends_add(nb[i].mac, nb[i].name);
            if (n2s_added_full) {
                sfx_play(SFX_BUMP);  // the list is full (SETTINGS > FRIENDS): the lobby says it
            } else {
                sfx_ui(SFX_COIN);
            }
            return true;
        }
    }
    return false;
}

// centred text in two lines when it is too wide for w (at the last space that fits); returns the y of the next line
static int lobby_wrap(int x, int w, int y, const char *t, uint16_t c)
{
    if (gfx_text_width(t, 1) <= w - 8) {
        gfx_text_center_in(x, w, y, t, 1, c);
        return y + 12;
    }
    char a[64];
    snprintf(a, sizeof a, "%s", t);
    int cut = -1;
    for (int i = 0; a[i]; i++) {
        if (a[i] == ' ') {
            a[i] = 0;
            if (gfx_text_width(a, 1) <= w - 8) {
                cut = i;
            }
            a[i] = ' ';
        }
    }
    if (cut < 0) {
        gfx_text_center_in(x, w, y, t, 1, c);
        return y + 12;
    }
    a[cut] = 0;
    gfx_text_center_in(x, w, y, a, 1, c);
    gfx_text_center_in(x, w, y + 12, a + cut + 1, 1, c);
    return y + 24;
}

static void draw_lobby_narrow(const char *game)
{
    int near[FRIENDS_MAX], nf = lobby_friends(near);
    int bx = 10, bw = gfx_w - 20, by = lobby_box_y(nf), bh = lobby_box_h(nf);
    gfx_fill_rect(bx, by, bw, bh, THEME_LOBBY_BG);
    gfx_frame_rect(bx, by, bw, bh, 2, THEME_LOBBY_EDGE);
    if (nf) {
        int y = lobby_wrap(bx, bw, by + 6, "FRIENDS ARE NEAR: WHO DO YOU PLAY WITH?", THEME_LOBBY_TEXT);
        lobby_wrap(bx, bw, y, "TAP A FRIEND", THEME_LOBBY_TEXT_DIM);
        for (int i = 0; i < nf; i++) {
            int x, yy, w, h;
            lobby_button(i, nf, &x, &yy, &w, &h);
            gfx_fill_rect(x, yy, w, h, THEME_LOBBY_BTN);
            gfx_frame_rect(x, yy, w, h, 1, THEME_LOBBY_BTN_EDGE);
            gfx_text_center_in(x, w, yy + h / 2 - 4, friends_get(near[i])->name, 1, THEME_LOBBY_BTN_TEXT);
        }
    } else {
        char l1[48], l2[64];
        int64_t t = n2_now();
        if (n2s_fail) {
            snprintf(l1, sizeof l1, "THE RADIO DID NOT START");
        } else if (friends_chosen() >= 0) {
            snprintf(l1, sizeof l1, "LOOKING FOR %.15s%.*s", friends_get(friends_chosen())->name, (int)(t / 400) % 4,
                     "...");
        } else {
            snprintf(l1, sizeof l1, "LOOKING FOR A FRIEND%.*s", (int)(t / 400) % 4, "...");
        }
        int y = lobby_wrap(bx, bw, by + 10, l1, THEME_LOBBY_TEXT);
        snprintf(l2, sizeof l2, strcmp(game, "WALKIE") ? "OPEN %s ON IT AND TAP 2 BOARDS" : "OPEN %s ON IT TOO", game);
        y = lobby_wrap(bx, bw, y + 4, l2, THEME_LOBBY_TEXT_DIM);
        const char *l3 = friends_count() ? "TAP TO STOP" : "NO FRIENDS YET: TAP THE BADGE. A TAP ELSE: STOP";
        if (n2s_added_at && t - n2s_added_at < 5000) {
            if (n2s_added_full) {
                snprintf(l2, sizeof l2, "THE FRIEND LIST IS FULL (%d): REMOVE ONE IN SETUP", FRIENDS_MAX);
            } else {
                snprintf(l2, sizeof l2, "%.15s ADDED. NOW ADD THIS BOARD ON IT", n2s_added);
            }
            l3 = l2;
        }
        lobby_wrap(bx, bw, y + 6, l3, THEME_LOBBY_WARN);
    }
    friend_new_t nb[3];
    int k = lobby_new(nb);
    for (int i = 0; i < k; i++) {
        int x, y, w, h;
        lobby_badge(i, k, nf, &x, &y, &w, &h);
        gfx_fill_rect(x, y, w, h, THEME_LOBBY_BADGE);
        gfx_frame_rect(x, y, w, h, 1, THEME_LOBBY_BADGE_EDGE);
        gfx_fill_rect(x + 4, y + 4, 28, h - 8, THEME_LOBBY_BADGE_NEW);
        gfx_text_center_in(x + 4, 28, y + h / 2 - 4, "NEW", 1, THEME_LOBBY_BADGE_NEW_TEXT);
        char b[40];
        snprintf(b, sizeof b, "+ ADD %.15s", nb[i].name);
        gfx_text_center_in(x + 34, w - 38, y + h / 2 - 4, b, 1, THEME_LOBBY_BADGE_TEXT);
    }
}

void net2_draw_lobby(const char *game)
{
    if (lobby_narrow()) {
        draw_lobby_narrow(game);
        return;
    }
    int near[FRIENDS_MAX], nf = lobby_friends(near), u = lobby_u();
    int bw = gfx_w - 40, bx = 20, by = lobby_box_y(nf), bh = lobby_box_h(nf);
    gfx_fill_rect(bx, by, bw, bh, THEME_LOBBY_BG);
    gfx_frame_rect(bx, by, bw, bh, 2, THEME_LOBBY_EDGE);
    if (nf) {
        // which friend: the buttons
        lobby_text(bx, bw, by + 10 * u, "FRIENDS ARE NEAR: WHO DO YOU PLAY WITH?", u, THEME_LOBBY_TEXT);
        lobby_text(bx, bw, by + 24 * u, "TAP A FRIEND", u, THEME_LOBBY_TEXT_DIM);
        for (int i = 0; i < nf; i++) {
            int x, y, w, h;
            lobby_button(i, nf, &x, &y, &w, &h);
            gfx_fill_rect(x, y, w, h, THEME_LOBBY_BTN);
            gfx_frame_rect(x, y, w, h, 1, THEME_LOBBY_BTN_EDGE);
            lobby_text(x, w, y + h / 2 - 4 * u, friends_get(near[i])->name, u, THEME_LOBBY_BTN_TEXT);
        }
    } else {
        char l1[48], l2[64];
        int64_t t = n2_now();
        if (n2s_fail) {
            snprintf(l1, sizeof l1, "THE RADIO DID NOT START");
        } else if (friends_chosen() >= 0) {
            snprintf(l1, sizeof l1, "LOOKING FOR %.15s%.*s", friends_get(friends_chosen())->name, (int)(t / 400) % 4,
                     "...");
        } else {
            snprintf(l1, sizeof l1, "LOOKING FOR A FRIEND%.*s", (int)(t / 400) % 4, "...");
        }
        lobby_text(bx, bw, by + 10 * u, l1, u, THEME_LOBBY_TEXT);
        snprintf(l2, sizeof l2, strcmp(game, "WALKIE") ? "OPEN %s ON IT AND TAP 2 BOARDS" : "OPEN %s ON IT TOO", game);
        lobby_text(bx, bw, by + 26 * u, l2, u, THEME_LOBBY_TEXT_DIM);
        const char *l3 = friends_count() ? "TAP TO STOP" : "NO FRIENDS YET: TAP THE BADGE. A TAP ELSE: STOP";
        if (n2s_added_at && t - n2s_added_at < 5000) {
            if (n2s_added_full) {
                snprintf(l2, sizeof l2, "THE FRIEND LIST IS FULL (%d): REMOVE ONE IN SETUP", FRIENDS_MAX);
            } else {
                snprintf(l2, sizeof l2, "%.15s ADDED. NOW ADD THIS BOARD ON IT", n2s_added);
            }
            l3 = l2;
        }
        lobby_text(bx, bw, by + 44 * u, l3, u, THEME_LOBBY_WARN);
    }
    // the ADD badges of the new boards
    friend_new_t nb[3];
    int k = lobby_new(nb);
    for (int i = 0; i < k; i++) {
        int x, y, w, h;
        lobby_badge(i, k, nf, &x, &y, &w, &h);
        gfx_fill_rect(x, y, w, h, THEME_LOBBY_BADGE);
        gfx_frame_rect(x, y, w, h, 1, THEME_LOBBY_BADGE_EDGE);
        gfx_fill_rect(x + 4, y + 4, 28 * u, h - 8, THEME_LOBBY_BADGE_NEW);
        gfx_text_center_in(x + 4, 28 * u, y + h / 2 - 4 * u, "NEW", u, THEME_LOBBY_BADGE_NEW_TEXT);
        char b[40];
        snprintf(b, sizeof b, "+ ADD %.15s", nb[i].name);
        lobby_text(x + 4 + 30 * u, w - 8 - 30 * u, y + h / 2 - 4 * u, b, u, THEME_LOBBY_BADGE_TEXT);
    }
}

void net2_draw_lost(int64_t now)
{
    if (!n2s_lost_at || now - n2s_lost_at > 3000) {
        return;
    }
    gfx_fill_rect(0, gfx_h / 2 - 10, gfx_w, 20, THEME_LOBBY_LOST_BG);
    gfx_text_center_in(0, gfx_w, gfx_h / 2 - 3, "THE OTHER BOARD IS GONE", 1, THEME_LOBBY_LOST_TEXT);
}
