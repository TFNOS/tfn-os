// A small radio link between boards with ESP-NOW broadcasts (see link.h).
//
// Packet: 'A' 'L' version app kind len channel id(2), then len bytes of data.
// Each packet goes by ESP-NOW (broadcast) and, on a WiFi network, by UDP to each arcade board on
// the LAN (lan.c). The receive paths (the ESP-NOW callback in the WiFi task, the LAN task) check
// the header, drop a packet that came already by the other path (the id of the sender), and put
// it in a queue. The app reads the queue with link_recv().
//
// A board in a pool (tfn_pool.h) sends link v4: 'A' 'L' 4 app kind len channel, the pool tag, a counter (4), the data,
// then a SipHash tag (8); at most 250 bytes with LINK_DATA_MAX data bytes. The pool service checks the tag, the roster
// MAC and the counter (the counter also drops the second copy of LAN + radio). A pool board also sends a v3 copy while
// a legacy peer of the same app is active (friends_legacy_on). A board with no pool sends and takes only v3.
//
// THE CHANNEL (link_meet.h): link_recv() moves the radio. A board with no WiFi network searches for the other board
// (the meeting channel and the visits), and two boards that hear each other agree on one channel. On each arrival of a
// search step the last packet of the app and the last name beacon go again, so the visited board hears this board at
// once. The apps do not search: link_set_channel() changes nothing.
//
// THE SEND QUEUE: ESP-NOW keeps only a few frames that wait for the air and refuses the next ones
// (ESP_ERR_ESPNOW_NO_MEM). At most TX_MAX frames are in flight (sent, with no send callback yet); the next frames wait
// in a small queue and go at the next send or poll of the app (a full queue drops its oldest frame). A radio with frames
// in flight that gets no send callback and refuses each send for TX_STUCK_MS is stuck: link_recover() restarts it.

#include <string.h>
#include "arc_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "wifi.h"
#include "link.h"
#include "friends.h"
#include "lan.h"
#include "tfn_pool.h"
#include "link_meet.h"

static const char *TAG = "link";

#define LINK_MAGIC0  'A'
#define LINK_MAGIC1  'L'
#define LINK_VERSION 3  // 2: byte 6 = the channel of the sender; 3: bytes 7, 8 = the packet id
// byte 6: bits 0-3 = the channel, bit 5 = a v3 copy of a v4 frame (a pool board, the legacy peer rule), bit 6 = the
// sender is QUIET (link_quiet: send it the LAN copies too), bit 7 = the sender is on a WiFi network (a fixed channel).
// A board of the same pool drops a v3 copy (the v4 frame has the same packet), and no board counts it as a legacy
// peer (else two pool boards keep their copies on for ever). An older firmware ignores bit 5.
#define CH_COPY       0x20
#define CH_QUIET      0x40
#define CH_WIFI       0x80
#define QUIET_PEER_MS 2000  // a QUIET packet of a peer in this time: each sent packet goes by the LAN too
#define HDR           9
#define V4            4  // the version of a link v4 frame (a pool)
#define FRAME_MAX     TFN_POOL_FRAME_MAX
_Static_assert(HDR + LINK_DATA_MAX <= FRAME_MAX && TFN_POOL_FRAME_HDR + LINK_DATA_MAX + TFN_POOL_TAG_LEN <= FRAME_MAX,
               "a v3 frame and a v4 frame of LINK_DATA_MAX bytes fit in one ESP-NOW packet");
#define DEDUP_SENDERS 8
#define DEDUP_IDS     32
#define QUEUE_LEN     24

static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static QueueHandle_t s_rx;
static bool s_on;
static int s_ch;                   // the channel now (link_start, link_set_channel)
static bool s_quiet;               // link_quiet(): the WiFi power save while the link is on
static uint16_t s_id;              // the id of the next packet that this board sends
static uint32_t s_sent, s_failed;  // link_stats
static volatile uint32_t
        s_peer_ms;  // link_in_use: the local time (ms) of the last packet of another board in the queue
static volatile uint32_t s_espnow_ms;  // the last ESP-NOW packet of an app that this board sends for (the LAN fallback)
static volatile uint32_t
        s_quiet_peer_ms;  // the last packet of a QUIET peer (its radio sleeps: it needs the LAN copies)
static bool s_restart;    // link_send restarts the radio (link_start keeps the chosen friend)
// THE LISTEN OF THE MENU (link_listen): ESP-NOW is on with no app link, and only the name beacons of friends come in
// (friends.c: a beacon of a lobby that waits is a radio invite). No queue, no meeting, no send.
static bool s_listen;
#define LISTEN_CH_MS 2000  // the listen puts the radio on the meeting channel again at most in this time
// THE MEETING (link_meet.h): the state, and the last packets that a search step sends again on its new channel (the
// last packet of an app and the last packet of FRIENDS: a lobby sends both). A copy keeps the frames as they went (the
// same packet id and pool counter): the other board drops a second copy, so a packet that is not repeatable (a new
// round, an ADD) does not come two times.
static link_meet_t s_meet;
static portMUX_TYPE s_meet_mux = portMUX_INITIALIZER_UNLOCKED;
#define MEET_COPY_MS 1500  // a packet older than this does not go again
typedef struct {
    uint32_t ms;              // the time of the send (0 = none)
    uint8_t n[2];             // the length of each frame (0 = none): a v3 copy for a legacy peer, the main frame
    uint8_t f[2][FRAME_MAX];  // the frames
} meet_copy_t;
static meet_copy_t s_copy[2];  // [0] the app, [1] FRIENDS
// THE SEND QUEUE (see the top). A channel change loses the callbacks of the frames in flight, so the count can be too
// high: after TX_PROBE_MS with no callback one frame goes anyway (ESP_OK shows that the count was old).
#define TX_MAX            6     // frames in flight at most (ESP-NOW refuses at about 8)
#define TX_QLEN           6     // frames that wait
#define TX_PROBE_MS       250   // no callback for this time: one frame goes anyway
#define TX_STUCK_MS       1000  // no callback and each send refused for this time: the radio is stuck
#define TX_STUCK_QUIET_MS 3000  // the same while the radio saves power (QUIET: the frames wait for the wake)
#define TX_RECOVER_GAP_MS 5000  // at most one recovery in this time
static portMUX_TYPE s_tx_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int s_tx_in;          // frames in flight
static volatile uint32_t s_tx_cb_ms;  // the last send callback (or the first send with no frame in flight)
static uint32_t s_tx_bad_ms;          // the first refusal since the last accepted send (0 = none)
static struct {
    uint8_t n;
    uint8_t f[FRAME_MAX];
} s_txq[TX_QLEN];
static int s_txq_head, s_txq_n;
static link_tx_t s_txs;                 // link_tx_stats
static volatile int s_learn_kind = -1;  // link_meet_learn: the kind of a LEARN packet (-1 = LEARN is not open)
static volatile bool s_answer;          // a new peer with no AP: meet_tick sends the last packets again (link_meet.h)
#define MEET_ANSWER_GAP_MS 300          // at most one answer in this time
#define CH_READ_MS         500          // on an AP: the channel of the radio is read again at most in this time
#define LAN_FALLBACK_MS    400          // no such packet for this long: the sent packets go by the LAN too
// the apps that this board sent for (the last 4; link_send): only a packet of one of them makes the link "in use"
#define USE_APPS    4
#define USE_SEND_MS 3000
static volatile struct {
    uint8_t app;
    uint32_t ms;
} s_txapp[USE_APPS];
static bool sent_for(uint8_t app, uint32_t now)
{
    for (int i = 0; i < USE_APPS; i++) {
        if (s_txapp[i].app == app && s_txapp[i].ms && now - s_txapp[i].ms < USE_SEND_MS) {
            return true;
        }
    }
    return false;
}
static void note_send(uint8_t app, uint32_t now)
{
    int k = 0;
    for (int i = 0; i < USE_APPS; i++) {
        if (s_txapp[i].app == app) {
            k = i;
            break;
        }
        if (s_txapp[i].ms < s_txapp[k].ms) {
            k = i;  // (else the oldest)
        }
    }
    s_txapp[k].app = app;
    s_txapp[k].ms = now ? now : 1;
}

// the last packet ids of each sender (a packet can come by ESP-NOW and by the LAN)
static struct {
    uint8_t mac[6];
    uint16_t id[DEDUP_IDS];
    uint8_t n, k;
} s_dedup[DEDUP_SENDERS];
static uint8_t s_dedup_next;
static portMUX_TYPE s_dedup_mux = portMUX_INITIALIZER_UNLOCKED;

// true if this packet came before (and notes it)
static bool seen_before(const uint8_t mac[6], uint16_t id)
{
    bool dup = false;
    portENTER_CRITICAL(&s_dedup_mux);
    int k = -1;
    for (int i = 0; i < DEDUP_SENDERS && k < 0; i++) {
        if (s_dedup[i].n && !memcmp(s_dedup[i].mac, mac, 6)) {
            k = i;
        }
    }
    if (k < 0) {
        k = s_dedup_next;
        s_dedup_next = (uint8_t)((s_dedup_next + 1) % DEDUP_SENDERS);
        memcpy(s_dedup[k].mac, mac, 6);
        s_dedup[k].n = s_dedup[k].k = 0;
    }
    for (int i = 0; i < s_dedup[k].n && !dup; i++) {
        dup = s_dedup[k].id[i] == id;
    }
    if (!dup) {
        s_dedup[k].id[s_dedup[k].k] = id;
        s_dedup[k].k = (uint8_t)((s_dedup[k].k + 1) % DEDUP_IDS);
        if (s_dedup[k].n < DEDUP_IDS) {
            s_dedup[k].n++;
        }
    }
    portEXIT_CRITICAL(&s_dedup_mux);
    return dup;
}

// this board sends for an app now (not only the name beacon of FRIENDS): a lobby or a match
static bool sends_app(uint32_t now)
{
    for (int i = 0; i < USE_APPS; i++) {
        if (s_txapp[i].app != FRIENDS_LINK_APP && s_txapp[i].ms && now - s_txapp[i].ms < USE_SEND_MS) {
            return true;
        }
    }
    return false;
}

// A PEER of the meeting: while LEARN A FRIEND is open, only a LEARN packet (of a board that is not a friend yet). Else
// a board in an app that this board sends for (also a board that is not a friend yet), the friend that the lobby looks
// for, any friend while this board sends for no app (the title of a turn game listens for an invite), or the name
// beacon of a friend while a lobby of this board waits (the friend is in a title or a lobby: it can join; a match sends
// no beacon). The name beacon of a board that is not a friend and the
// packets of other pairs in other apps are not peers: they would keep the search on one channel.
static bool meet_peer(const link_msg_t *m)
{
    const uint32_t now = (uint32_t)m->rx_ms;
    if (s_learn_kind >= 0) {
        // (LEARN looks for a new board: the friends in their apps would take the radio to their channels)
        return m->app == FRIENDS_LINK_APP && m->kind == s_learn_kind;
    }
    if (m->app != FRIENDS_LINK_APP && sent_for(m->app, now)) {
        return true;
    }
    const int f = friends_find(m->mac);
    if (f < 0) {
        return false;
    }
    const bool beacon = m->app == FRIENDS_LINK_APP && m->kind == FRIENDS_K_NAME;
    return f == friends_chosen() || !sends_app(now) || (beacon && friends_lobby_waits());
}

// A packet by radio: a peer stops the search and gives its channel to the agreement (link_meet.h)
static void meet_heard(const link_msg_t *m, uint8_t chb)
{
    if (!meet_peer(m)) {
        return;
    }
    const bool my_ap = wifi_state() == WIFI_CONNECTED;
    portENTER_CRITICAL(&s_meet_mux);
    const bool answer = link_meet_heard(&s_meet, chb & 0x0F, (chb & CH_WIFI) != 0, my_ap, (uint32_t)m->rx_ms);
    portEXIT_CRITICAL(&s_meet_mux);
    if (answer) {
        s_answer = true;
    }
}

// a good frame (v3 or v4): data[3..6] = app kind len channel, the app data at data + hdr
static void take(const uint8_t mac[6], const uint8_t *data, int hdr, int8_t rssi, bool lan)
{
    const int n = data[5];
    link_msg_t m;
    memset(&m, 0, sizeof(m));
    memcpy(m.mac, mac, 6);
    m.app = data[3];
    m.kind = data[4];
    m.len = (uint8_t)n;
    memcpy(m.data, data + hdr, (size_t)n);
    m.ch = lan ? 0 : data[6];
    m.rssi = rssi;
    m.rx_ms = esp_timer_get_time() / 1000;
    if (!s_on) {
        if (!lan) {
            friends_on_packet(m.mac, m.app, m.kind, m.data, m.len, m.rssi);  // (the listen of the menu: only FRIENDS)
        }
        return;
    }
    if (!lan) {
        meet_heard(&m, data[6]);
    }
    if (friends_on_packet(m.mac, m.app, m.kind, m.data, m.len, m.rssi)) {
        return;  // FRIENDS: near; a name beacon
    }
    if (sent_for(m.app, (uint32_t)m.rx_ms)) {
        s_peer_ms = (uint32_t)m.rx_ms;
        if (!lan) {
            s_espnow_ms = (uint32_t)m.rx_ms;  // (ESP-NOW of the game works: no LAN copy)
        }
        if (data[6] & CH_QUIET) {
            s_quiet_peer_ms = (uint32_t)m.rx_ms;
        }
        friends_game_seen(m.mac);  // (FRIENDS: this friend is in the same game or lobby)
    }
    xQueueSend(s_rx, &m, 0);  // full: the packet is lost (as on the radio)
}

// a v3 frame on a board in a pool: a copy of a member (CH_COPY) is dropped; the receive rule (STRICT drops a forged
// member and a keyed friend of another pool). A v3 frame with no CH_COPY that this board takes comes from a legacy
// peer: the send rule makes v3 copies of that app for 5 s.
static bool pool_v3_ok(const uint8_t mac[6], uint8_t app, uint8_t ch)
{
    const int role = tfn_pool_role(tfn_pool_member(mac));
    if ((ch & CH_COPY) && role >= 0) {
        return false;
    }
    if (!friends_pool_take(FRIENDS_IN_V3, mac, true, tfn_pool_strict(), role)) {
        return false;
    }
    if (!(ch & CH_COPY)) {
        friends_legacy_note(app, (uint32_t)(esp_timer_get_time() / 1000));
    }
    return true;
}

// a v4 frame: the pool service checks it and counts the result (another pool, not a member, a bad tag, a counter that came)
static void deliver_v4(const uint8_t mac[6], const uint8_t *data, int len, int8_t rssi, bool lan)
{
    const int n = data[5];
    if (n > LINK_DATA_MAX || TFN_POOL_FRAME_HDR + n + TFN_POOL_TAG_LEN > len) {
        return;
    }
    if (tfn_pool_frame_open(mac, data, len) != 0) {
        return;
    }
    take(mac, data, TFN_POOL_FRAME_HDR, rssi, lan);
}

// a packet from either path (lan = true: by UDP; then the channel byte means nothing here)
static void deliver(const uint8_t mac[6], const uint8_t *data, int len, int8_t rssi, bool lan)
{
    if (!((s_on && s_rx) || s_listen) || len < HDR || data[0] != LINK_MAGIC0 || data[1] != LINK_MAGIC1) {
        return;
    }
    if (data[2] == V4) {
        deliver_v4(mac, data, len, rssi, lan);
        return;
    }
    if (data[2] != LINK_VERSION) {
        return;
    }
    int n = data[5];
    if (n > LINK_DATA_MAX || HDR + n > len) {
        return;
    }
    if (tfn_pool_on() && !pool_v3_ok(mac, data[3], data[6])) {
        return;
    }
    if (seen_before(mac, (uint16_t)(data[7] | data[8] << 8))) {
        return;
    }
    take(mac, data, HDR, rssi, lan);
}

volatile bool g_link_no_lan;    // tests (console "lancopy 0"): no LAN copy of the sent packets (ESP-NOW only)
volatile bool g_link_lan_only;  // tests (console "lanonly 1"): ignore the ESP-NOW packets

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || g_link_lan_only) {
        return;
    }
    deliver(info->src_addr, data, len, info->rx_ctrl ? (int8_t)info->rx_ctrl->rssi : 0, false);
}

static void on_lan(const uint8_t mac[6], const uint8_t *data, int len)
{
    deliver(mac, data, len, 0, true);
}

// the radio is free to change its channel: no WiFi network and no connect (the radio must stay on the channel of the
// AP), no scan, no phone on the hotspot (the hotspot moves with the radio)
static bool radio_free(void)
{
    const wifi_state_t w = wifi_state();
    return w != WIFI_CONNECTED && w != WIFI_CONNECTING && !wifi_scanning() &&
           !(wifi_hotspot_on() && wifi_hotspot_clients() > 0);
}

static void listen_stop(void)
{
    if (!s_listen) {
        return;
    }
    s_listen = false;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    ESP_LOGI(TAG, "the listen of the menu stops");
}

void link_listen(bool on)
{
    if (s_on) {
        return;  // (an app has the link)
    }
    const wifi_state_t w = wifi_state();
    if (!on || w == WIFI_OFF || w == WIFI_CONNECTED) {
        listen_stop();  // (a radio that is off or that sleeps between the beacons of its AP: no listen, no cost)
        return;
    }
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    static uint32_t s_ch_at;
    if (!s_listen) {
        if (esp_now_init() != ESP_OK) {
            return;
        }
        esp_now_register_recv_cb(on_recv);
        s_listen = true;
        s_ch_at = now - LISTEN_CH_MS;
        ESP_LOGI(TAG, "the listen of the menu starts (radio invites of friends)");
    }
    if (now - s_ch_at < LISTEN_CH_MS) {
        return;
    }
    s_ch_at = now;
    // the meeting channel: a lobby that waits comes back to it between its visits (link_meet.h)
    if (radio_free() && link_channel() != LINK_MEET_CH) {
        esp_wifi_set_channel(LINK_MEET_CH, WIFI_SECOND_CHAN_NONE);
    }
}

// the send callback (the WiFi task): one frame in flight less
static void on_sent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    (void)info;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    portENTER_CRITICAL(&s_tx_mux);
    if (s_tx_in > 0) {
        s_tx_in--;
    }
    s_tx_cb_ms = now;
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_txs.cb_ok++;
    } else {
        s_txs.cb_fail++;
    }
    portEXIT_CRITICAL(&s_tx_mux);
}

// no frame in flight (a new ESP-NOW, a new channel); all = also no frame that waits
static void tx_reset(bool all)
{
    portENTER_CRITICAL(&s_tx_mux);
    s_tx_in = 0;
    s_tx_cb_ms = (uint32_t)(esp_timer_get_time() / 1000);
    portEXIT_CRITICAL(&s_tx_mux);
    s_tx_bad_ms = 0;
    if (all) {
        s_txq_head = s_txq_n = 0;
    }
}

int link_pair_channel(void)
{
    return s_on ? s_ch : 0;
}

bool link_start(void)
{
    if (s_on) {
        return true;
    }
    listen_stop();  // (the app link takes ESP-NOW)
    if (wifi_state() == WIFI_OFF && !wifi_start()) {
        return false;
    }
    if (!s_rx) {
        // in PSRAM: 24 packets of 250 bytes are too much for the internal RAM
        s_rx = xQueueCreateWithCaps(QUEUE_LEN, sizeof(link_msg_t), MALLOC_CAP_SPIRAM);
        if (!s_rx) {
            return false;
        }
    }
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_init: %s", esp_err_to_name(err));
        return false;
    }
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_sent);
    tx_reset(!s_restart);  // (a restart keeps the frames that wait)
    lan_set_rx(on_lan);    // the same packets from the boards on the LAN
    // the broadcast peer on the channel now (0), on the interface that the WiFi uses
    wifi_mode_t mode = WIFI_MODE_STA;
    esp_wifi_get_mode(&mode);
    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = 0;
    peer.ifidx = mode == WIFI_MODE_AP ? WIFI_IF_AP : WIFI_IF_STA;
    peer.encrypt = false;
    if (!esp_now_is_peer_exist(BCAST)) {
        err = esp_now_add_peer(&peer);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_now_add_peer: %s", esp_err_to_name(err));
            esp_now_deinit();
            return false;
        }
    }
    // With the power save on, the radio sleeps between beacons and broadcasts come late or
    // not at all. The WiFi driver can refuse (for example with Bluetooth on): then it stays.
    esp_wifi_set_ps(WIFI_PS_NONE);
    xQueueReset(s_rx);
    s_peer_ms = (uint32_t)(esp_timer_get_time() / 1000) - 60000;
    s_espnow_ms = s_quiet_peer_ms = s_peer_ms;
    // a new lobby or game: no friend is chosen yet (PONG and FIGHT kept the friend of an earlier lobby and then
    // waited for it for ever; net2_open does the same; WALKIE sets its own choice after the start)
    if (!s_restart) {
        friends_choose(-1);
    }
    s_on = true;
    s_quiet = false;
    s_ch = link_channel();
    if (!s_restart) {
        // (a restart of the radio in a match keeps the meeting: the board stays on its agreed channel)
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        uint8_t me[6];
        link_my_mac(me);
        const uint32_t seed = (uint32_t)esp_timer_get_time() ^ ((uint32_t)me[4] << 8 | me[5]) << 16;
        portENTER_CRITICAL(&s_meet_mux);
        link_meet_reset(&s_meet, s_ch, now, seed);
        memset(s_copy, 0, sizeof s_copy);
        portEXIT_CRITICAL(&s_meet_mux);
        s_answer = false;
    }
    ESP_LOGI(TAG, "on, channel %d", s_ch);
    return true;
}

void link_stop(void)
{
    if (!s_on) {
        return;
    }
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    s_on = false;
    s_quiet = false;
    if (!s_restart) {
        s_learn_kind = -1;
        tx_reset(true);
    }
    wifi_ps_apply();  // (the power save of the screen and of a transfer)
    if (s_rx) {
        xQueueReset(s_rx);
    }
}

bool link_ready(void)
{
    return s_on;
}

bool link_in_use(void)
{
    if (!s_on || s_quiet) {
        return false;
    }
    uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    return t - s_peer_ms < LINK_USE_MS;
}

void link_quiet(bool quiet)
{
    if (!s_on) {
        quiet = false;
    }
    if (quiet == s_quiet) {
        return;
    }
    s_quiet = quiet;
    wifi_ps_apply();  // (phones on the hotspot: no change; a transfer: no power save)
    ESP_LOGI(TAG, "%s",
             quiet ? "quiet: the radio saves power, the LAN carries the link" : "the radio listens all the time");
}

bool link_quiet_on(void)
{
    return s_on && s_quiet;
}

// the first 7 bytes of a frame and the app data at buf + hdr
static void frame_head(uint8_t *buf, uint8_t ver, uint8_t app, uint8_t kind, const void *data, int len, int hdr)
{
    buf[0] = LINK_MAGIC0;
    buf[1] = LINK_MAGIC1;
    buf[2] = ver;
    buf[3] = app;
    buf[4] = kind;
    buf[5] = (uint8_t)len;
    buf[6] = (uint8_t)((s_ch & 0x0F) | (wifi_state() == WIFI_CONNECTED ? CH_WIFI : 0) | (s_quiet ? CH_QUIET : 0));
    if (len) {
        memcpy(buf + hdr, data, (size_t)len);
    }
}

// a v3 frame in buf: returns its length
static int frame_v3(uint8_t *buf, uint8_t app, uint8_t kind, const void *data, int len)
{
    frame_head(buf, LINK_VERSION, app, kind, data, len, HDR);
    uint16_t id = s_id++;
    buf[7] = (uint8_t)id;
    buf[8] = (uint8_t)(id >> 8);
    return HDR + len;
}

static bool send_frame(const uint8_t *buf, int n);

// the copy of a frame for the search steps (meet_resend): slot 0 the app, 1 FRIENDS; i 0 the v3 copy, 1 the main frame
static void meet_keep(int slot, int i, const uint8_t *buf, int n)
{
    portENTER_CRITICAL(&s_meet_mux);
    s_copy[slot].n[i] = (uint8_t)n;
    memcpy(s_copy[slot].f[i], buf, (size_t)n);
    portEXIT_CRITICAL(&s_meet_mux);
}

// one frame that a search step can send again
static bool send_kept(int slot, int i, const uint8_t *buf, int n)
{
    if (n > 0 && n <= FRAME_MAX) {
        meet_keep(slot, i, buf, n);
    }
    return send_frame(buf, n);
}

// a board in a pool: the v4 frame, and a v3 copy while a legacy peer of the app is active. With no v4 frame (the pool
// went away now): v3.
static bool send_pool(uint8_t *buf, int slot, uint8_t app, uint8_t kind, const void *data, int len)
{
    const bool copy = friends_legacy_on(app, (uint32_t)(esp_timer_get_time() / 1000));
    bool ok = false;
    if (copy) {
        const int n3 = frame_v3(buf, app, kind, data, len);
        buf[6] |= CH_COPY;
        ok = send_kept(slot, 0, buf, n3);
    }
    frame_head(buf, V4, app, kind, data, len, TFN_POOL_FRAME_HDR);
    const int n = tfn_pool_frame_seal(buf, TFN_POOL_FRAME_HDR + len, FRAME_MAX);
    if (n > 0) {
        return send_kept(slot, 1, buf, n);
    }
    return copy ? ok : send_kept(slot, 1, buf, frame_v3(buf, app, kind, data, len));
}

bool link_send(uint8_t app, uint8_t kind, const void *data, int len)
{
    if (!s_on || len < 0 || len > LINK_DATA_MAX) {
        return false;
    }
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    note_send(app, now);
    const int slot = app == FRIENDS_LINK_APP ? 1 : 0;
    portENTER_CRITICAL(&s_meet_mux);
    s_copy[slot].ms = now ? now : 1;
    s_copy[slot].n[0] = s_copy[slot].n[1] = 0;
    portEXIT_CRITICAL(&s_meet_mux);
    uint8_t buf[FRAME_MAX];
    if (tfn_pool_on()) {
        return send_pool(buf, slot, app, kind, data, len);
    }
    return send_kept(slot, 1, buf, frame_v3(buf, app, kind, data, len));
}

// one frame to ESP-NOW now: the count of the frames in flight, and the time of the first refusal (probe = the count
// was full, but the last callback is old)
static esp_err_t tx_go(const uint8_t *buf, int n, bool probe, uint32_t now)
{
    const esp_err_t err = esp_now_send(BCAST, buf, (size_t)n);
    s_sent++;
    portENTER_CRITICAL(&s_tx_mux);
    if (err == ESP_OK) {
        if (probe || s_tx_in <= 0) {
            s_tx_in = 1;  // (a probe that went: the count was old)
            s_tx_cb_ms = now;
        } else {
            s_tx_in++;
        }
        if (s_tx_in > s_txs.in_max) {
            s_txs.in_max = s_tx_in;
        }
    } else if (err == ESP_ERR_ESPNOW_NO_MEM) {
        s_tx_in = TX_MAX;  // (ESP-NOW is full: the next frame waits for a callback or for a probe)
    }
    portEXIT_CRITICAL(&s_tx_mux);
    s_txs.probes += probe ? 1u : 0u;
    if (err == ESP_OK) {
        s_tx_bad_ms = 0;
        return err;
    }
    s_failed++;
    if (!s_tx_bad_ms) {
        s_tx_bad_ms = now ? now : 1;
    }
    static uint32_t s_err_log;
    if (now - s_err_log > 1000) {
        s_err_log = now;
        ESP_LOGW(TAG, "a send was refused: %s (%u refused)", esp_err_to_name(err), (unsigned)s_failed);
    }
    return err;
}

// a frame can go now (probe: the count is full, but the last callback is older than TX_PROBE_MS)
static bool tx_room(uint32_t now, bool *probe)
{
    portENTER_CRITICAL(&s_tx_mux);
    const int in = s_tx_in;
    const uint32_t cb = s_tx_cb_ms;
    portEXIT_CRITICAL(&s_tx_mux);
    *probe = in >= TX_MAX && now - cb >= TX_PROBE_MS;
    return in < TX_MAX || *probe;
}

// a frame waits at the end of the queue; a full queue drops its oldest frame (an older state of the app)
static void txq_put(const uint8_t *buf, int n)
{
    if (n <= 0 || n > FRAME_MAX) {
        return;
    }
    if (s_txq_n == TX_QLEN) {
        s_txq_head = (s_txq_head + 1) % TX_QLEN;
        s_txq_n--;
        s_txs.dropped++;
    }
    const int i = (s_txq_head + s_txq_n) % TX_QLEN;
    s_txq[i].n = (uint8_t)n;
    memcpy(s_txq[i].f, buf, (size_t)n);
    s_txq_n++;
}

static void txq_pop(void)
{
    s_txq_head = (s_txq_head + 1) % TX_QLEN;
    s_txq_n--;
}

static void link_recover(uint32_t now);

// the frames that wait go in their order while there is room (one probe at most); then the check of a stuck radio
static void tx_pump(uint32_t now)
{
    while (s_txq_n > 0) {
        bool probe = false;
        if (!tx_room(now, &probe)) {
            break;
        }
        const esp_err_t err = tx_go(s_txq[s_txq_head].f, s_txq[s_txq_head].n, probe, now);
        if (err == ESP_ERR_ESPNOW_NO_MEM) {
            break;  // (the frame stays first)
        }
        txq_pop();  // (sent, or an error that a second try does not repair)
        if (probe) {
            break;
        }
    }
    if (!s_tx_bad_ms) {
        return;
    }
    portENTER_CRITICAL(&s_tx_mux);
    const uint32_t cb = s_tx_cb_ms;
    portEXIT_CRITICAL(&s_tx_mux);
    const uint32_t lim = s_quiet ? TX_STUCK_QUIET_MS : TX_STUCK_MS;
    if (now - s_tx_bad_ms >= lim && now - cb >= lim) {
        link_recover(now);
    }
}

// The radio is stuck: ESP-NOW off, the whole WiFi driver again (deinit + init: a stop and a start of the driver do not
// empty the flow control of ESP-NOW), ESP-NOW on again on the channel of the link (the frames that wait stay), then the
// first frame that waits tries the new radio at once. The link of the game goes on if the other board hears this board
// again within its 3 s. At most one recovery in TX_RECOVER_GAP_MS.
static void link_recover(uint32_t now)
{
    static uint32_t s_at;
    if (s_at && now - s_at < TX_RECOVER_GAP_MS) {
        return;
    }
    s_at = now ? now : 1;
    ESP_LOGE(TAG, "the radio is stuck: no send callback and each send is refused for %u ms",
             (unsigned)(now - s_tx_bad_ms));
    link_diag("stuck");
    const int ch = s_ch;
    const bool quiet = s_quiet;
    s_restart = true;
    link_stop();
    wifi_radio_restart(ch, true);
    const bool on = link_start();
    if (on) {
        link_quiet(quiet);
    }
    s_restart = false;
    s_txs.restarts++;
    if (!on) {
        return;
    }
    esp_err_t err = ESP_OK;
    if (s_txq_n > 0) {
        err = tx_go(s_txq[s_txq_head].f, s_txq[s_txq_head].n, false, (uint32_t)(esp_timer_get_time() / 1000));
        if (err != ESP_ERR_ESPNOW_NO_MEM) {
            txq_pop();
        }
    }
    if (err == ESP_ERR_ESPNOW_NO_MEM) {
        ESP_LOGE(TAG, "the radio still refuses each send after the restart");
        return;
    }
    ESP_LOGW(TAG, "the radio is back after the restart (channel %d)", s_ch);
    s_tx_bad_ms = 0;
}

// one frame: the LAN copy when necessary, then ESP-NOW now or after the frames that wait
static bool send_frame(const uint8_t *buf, int n)
{
    // the LAN copy (only on a WiFi network with other boards) only when ESP-NOW of the game does not come: always-on copies
    // jam the TX buffers of the driver. A QUIET peer (WALKIE with its screen off)
    // hears few broadcasts: it gets the LAN copy too, although its ESP-NOW packets come here.
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (!g_link_no_lan && (now - s_espnow_ms > LAN_FALLBACK_MS || now - s_quiet_peer_ms < QUIET_PEER_MS)) {
        lan_send_link(buf, n);
    }
    tx_pump(now);
    if (!s_on) {
        return false;
    }
    bool probe = false;
    if (s_txq_n == 0 && tx_room(now, &probe)) {
        const esp_err_t err = tx_go(buf, n, probe, now);
        if (err != ESP_ERR_ESPNOW_NO_MEM) {
            return err == ESP_OK;
        }
    }
    txq_put(buf, n);
    return true;
}

void link_tx_stats(link_tx_t *t)
{
    portENTER_CRITICAL(&s_tx_mux);
    *t = s_txs;
    t->in = s_tx_in;
    portEXIT_CRITICAL(&s_tx_mux);
    t->wait = s_txq_n;
}

// the state of the radio for a diagnosis (the console "wifistat"; once when the sends stay refused)
void link_diag(const char *why)
{
    wifi_ps_type_t ps = WIFI_PS_NONE;
    esp_wifi_get_ps(&ps);
    wifi_ap_record_t ap;
    const int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    ESP_LOGW(TAG,
             "diag (%s): link %s%s, channel %d, sent %u, refused %u, ps %d, AP rssi %d, internal RAM %u free (low %u, "
             "block %u)",
             why, s_on ? "on" : "off", s_quiet ? " quiet" : "", s_ch, (unsigned)s_sent, (unsigned)s_failed, (int)ps,
             rssi, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    link_tx_t t;
    link_tx_stats(&t);
    ESP_LOGW(TAG,
             "diag (%s): in flight %d (most %d), waiting %d, dropped %u, callbacks %u ok %u failed, probes %u, "
             "restarts %u",
             why, t.in, t.in_max, t.wait, (unsigned)t.dropped, (unsigned)t.cb_ok, (unsigned)t.cb_fail,
             (unsigned)t.probes, (unsigned)t.restarts);
#ifdef ESP_PLATFORM
    esp_wifi_statis_dump(WIFI_STATIS_ALL);
#endif
}

void link_stats(uint32_t *sent, uint32_t *failed)
{
    *sent = s_sent;
    *failed = s_failed;
}

// a search step arrived on a new channel: the last packets (of the last MEET_COPY_MS) go again, as the same frames
static void meet_resend(uint32_t now)
{
    for (int i = 0; i < 4; i++) {
        uint8_t f[FRAME_MAX];
        int n = 0;
        portENTER_CRITICAL(&s_meet_mux);
        const meet_copy_t *c = &s_copy[i / 2];
        if (c->ms && now - c->ms < MEET_COPY_MS && c->n[i % 2]) {
            n = c->n[i % 2];
            memcpy(f, c->f[i % 2], (size_t)n);
        }
        portEXIT_CRITICAL(&s_meet_mux);
        if (n) {
            send_frame(f, n);
        }
    }
}

// the link can change the channel of the radio now (radio_free)
static bool meet_can_move(void)
{
    return s_on && radio_free();
}

static bool link_goto(int ch)
{
    if (!s_on || ch < 1 || ch > 13) {
        return false;
    }
    if (esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        return false;
    }
    s_ch = ch;
    tx_reset(false);  // (the frames in flight of the old channel get no callback)
    return true;
}

// each poll of the app: the agreed channel, the steps of the search (link_meet.h)
static void meet_tick(void)
{
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    const bool can = meet_can_move();
    const bool ap = !can && wifi_state() == WIFI_CONNECTED;
    // the channel of the radio from the driver (on the Tab5 a call to the radio chip): the channel of the AP after a
    // connect and then each CH_READ_MS, and once when the radio can move again (a scan, a connect or the hotspot of
    // AUTO can have moved it while the link could not move it)
    static bool s_ap_was, s_can_was;
    static uint32_t s_ch_at;
    if ((ap && (!s_ap_was || now - s_ch_at >= CH_READ_MS)) || (can && !s_can_was)) {
        const int c = link_channel();
        if (c >= 1 && c <= 13) {
            s_ch = c;
        }
        s_ch_at = now;
    }
    s_ap_was = ap;
    s_can_was = can;
    static uint32_t s_answer_at;
    if (s_answer && now - s_answer_at >= MEET_ANSWER_GAP_MS) {
        s_answer = false;
        s_answer_at = now;
        meet_resend(now);  // (the new peer can be on a visit of its search: it hears this board at once)
    }
    // the flags of the step: no visits while the radio sleeps (QUIET); while no match runs, a visit also in a hold;
    // with the hotspot on, a long wait on the meeting channel (the channel of the hotspot: a phone can join it)
    unsigned flags = (can ? LINK_MEET_F_MOVE : 0u) | (s_quiet ? 0u : LINK_MEET_F_SEARCH);
    if (friends_lobby_waits() || !sends_app(now)) {
        flags |= LINK_MEET_F_SEEK;
    }
    if (wifi_hotspot_on()) {
        flags |= LINK_MEET_F_SLOW;
    }
    bool search = false;
    portENTER_CRITICAL(&s_meet_mux);
    if (s_meet.ch != s_ch) {
        link_meet_moved(&s_meet, s_ch);
    }
    const int ch = link_meet_step(&s_meet, flags, now, &search);
    portEXIT_CRITICAL(&s_meet_mux);
    if (!ch) {
        return;
    }
    const int from = s_ch;
    if (!link_goto(ch)) {
        return;
    }
    portENTER_CRITICAL(&s_meet_mux);
    link_meet_moved(&s_meet, ch);
    portEXIT_CRITICAL(&s_meet_mux);
    if (search) {
        meet_resend(now);
    } else {
        ESP_LOGI(TAG, "the other board: one channel with it, %d (from %d)", ch, from);
    }
}

bool link_recv(link_msg_t *m)
{
    if (s_on) {
        meet_tick();
    }
    if (s_on && (s_txq_n > 0 || s_tx_bad_ms)) {
        tx_pump((uint32_t)(esp_timer_get_time() / 1000));
    }
    return s_rx && xQueueReceive(s_rx, m, 0) == pdTRUE;
}

bool link_set_channel(int ch)
{
    // the link finds the channel of the other board by itself (link_meet.h): a search of an app would take the radio
    // away from the meeting, so this call changes nothing
    return s_on && ch >= 1 && ch <= 13;
}

bool link_force_channel(int ch)
{
    return link_goto(ch);
}

void link_meet_learn(int kind)
{
    s_learn_kind = s_on ? kind : -1;
}

int link_channel(void)
{
    uint8_t ch = 0;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&ch, &second) != ESP_OK) {
        return 0;
    }
    return ch;
}

void link_my_mac(uint8_t mac[6])
{
    memset(mac, 0, 6);
    arc_mac(mac);
}
