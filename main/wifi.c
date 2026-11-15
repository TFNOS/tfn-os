// WiFi station and a small web server: the files in /data (exports, samples),
// MIDI songs for TILES (upload, list, delete) and the gamepad setup page.

// The parts: wifi_web.c (the board pages of wifi_web_register), wifi_nets.c (the saved networks).

#include "wifi_priv.h"
#include "friends.h"

static const char *TAG = "wifi";
// a network that SETUP joins (wifi_connect_try): it goes into the NVS only when it connects
static char s_pend_ssid[33], s_pend_pass[65];
static volatile bool s_pend;

static volatile bool s_auto;  // wifi_autoconnect waits for a scan: SCAN_DONE picks the network

static volatile wifi_state_t s_state = WIFI_OFF;
static volatile bool s_scanning;
#if CONFIG_TFN_EXTRA_WIFI_CAPTURE
static volatile bool s_capture_active;  // exclusive SDK capture session
#else
#define s_capture_active false  // (the extra WiFi capture is not in this build)
#endif
static char s_ip[20];
static char s_ssid[33];
static int s_retry;
static volatile bool s_user_off;    // wifi_disconnect(): no automatic connect until wifi_reconnect() or a connect
static volatile int s_last_ch;      // the channel of the last connection (0 = not known): the try in a game
static volatile bool s_game_leave;  // a connect on another channel than the 2-board match: the board leaves that AP

// hotspot
#define AP_IP "192.168.4.1"
const char *const HOTSPOT_MODE_NAMES[HOTSPOT_MODES] = {"OFF", "AUTO", "ON"};
static esp_netif_t *s_ap_netif;
static volatile bool s_ap_on;
static volatile int s_ap_clients;
static char s_ap_ssid[20];
static void hotspot_up(void);
static void hotspot_down(void);
// THE 2-MINUTE HOTSPOT OF AUTO. AUTO opens the hotspot ONE time after
// each start-up (no saved network, or the first failed connection), and it closes 2 min later. A phone on it keeps it
// open: it closes 2 min after the last phone left. The tries of each 60 s do not open it again. The START button, the
// console "hotspot on", the mode ON and a new choice of AUTO in SETUP open it with no limit (START, ON) or with a new
// window of 2 min (AUTO).
#define AP_AUTO_US (120LL * 1000000)
static bool s_ap_auto_done;       // AUTO opened its window after this start-up
static volatile bool s_ap_timed;  // the hotspot that is on now closes by the timer
static esp_timer_handle_t s_ap_timer;
static volatile int64_t s_ap_end_us;  // when the timer closes it (esp_timer time)
static void ap_timer_arm(void);
static void hotspot_auto_up(void);

wifi_ap_t wif_aps[MAX_AP];
int wif_nap;

// ---------------------------------------------------------------- web server

static void http_start(void);
// a new try after WIFI_FAILED: 5 s after a failed JOIN of SETUP the saved network again; else each 60 s
// A link in use (a game of 2 BOARDS, link_in_use): no try now. A try scans all 13 channels, about
// 2 s off the channel of the game, and many tries each minute end the link ("THE OTHER BOARD IS GONE"). The timer
// looks again each 5 s, so the try comes about 5 s after the link ends. A lobby that waits: no try for LOBBY_HOLD_MS,
// then one try each LOBBY_TRY_US, only on the channel of the last connection (no scan of the other channels: the
// search of the link goes on between the tries; a WiFi on another channel comes back after the lobby). An app that
// has the link (link_ready: a title or a list of a 2-board game, where the board listens for the invite of a friend)
// also gets only these tries: a full try is 4 scans of all channels, about 10 s with no radio for the friend.
#define RETRY_HOLD_US 5000000
#define LOBBY_HOLD_MS 30000
#define LOBBY_TRY_US  30000000
static esp_timer_handle_t s_retry_timer;
static void retry_later(int64_t us);
static void connect_on(int ch);
static void retry_cb(void *arg)
{
    (void)arg;
    if (s_state != WIFI_FAILED || s_capture_active) {
        return;
    }
    // (a lobby that waits: the radio searches for the friend, link.c; only for LOBBY_HOLD_MS, then the WiFi comes back
    // for the invites by the LAN and the copies to far friends)
    if (link_in_use() || (friends_lobby_waits() && friends_lobby_age() < LOBBY_HOLD_MS)) {
        retry_later(RETRY_HOLD_US);
        return;
    }
    if ((friends_lobby_waits() || link_ready()) && s_last_ch > 0) {
        ESP_LOGI(TAG, "a 2-board screen is open: one try of the network on channel %d", s_last_ch);
        s_retry = 1;  // (the disconnect event makes no more tries)
        s_state = WIFI_CONNECTING;
        connect_on(s_last_ch);
        return;
    }
    s_pend = false;
    memset(s_pend_pass, 0, sizeof s_pend_pass);
    ESP_LOGI(TAG, "a new try of the saved network");
    wifi_autoconnect();
}
static void retry_later(int64_t us)
{
    if (!s_retry_timer) {
        const esp_timer_create_args_t a = {.callback = retry_cb, .name = "wifi_retry"};
        if (esp_timer_create(&a, &s_retry_timer) != ESP_OK) {
            return;
        }
    }
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, us);
}
static volatile int64_t
        s_restart_ms;  // wifi_radio_restart: the time of the last restart (its own disconnect is not a fault)
static volatile bool s_restart_eat;  // wifi_radio_restart stopped an association: its one disconnect is not a fault
// one more connect of the network that is set now: ch = 0 scans all channels, ch 1..13 only that channel
static void connect_on(int ch)
{
    wifi_config_t wc;
    if (esp_wifi_get_config(WIFI_IF_STA, &wc) == ESP_OK && wc.sta.channel != (uint8_t)ch) {
        wc.sta.channel = (uint8_t)ch;
        esp_wifi_set_config(WIFI_IF_STA, &wc);
    }
    esp_wifi_connect();
}

// the WiFi driver again from its start (deinit + init, the mode and the power save as before): the flow control of
// ESP-NOW can stay full after a stop and a start of the driver
static bool driver_again(void)
{
    wifi_mode_t mode = WIFI_MODE_STA;
    esp_wifi_get_mode(&mode);
    wifi_ps_type_t ps = WIFI_PS_NONE;
    esp_wifi_get_ps(&ps);
    esp_wifi_deinit();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    for (int i = 0; i < 3; i++) {
        if (esp_wifi_init(&cfg) == ESP_OK) {
            esp_wifi_set_mode(mode);
            esp_wifi_set_ps(ps);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGE(TAG, "the WiFi driver did not start again (internal RAM free %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    s_state = WIFI_FAILED;
    return false;
}

// The radio is stuck (link.c: the frames in flight get no send callback and each send is refused): stop and start the
// driver (full = also its deinit + init), put the radio on the channel ch (the game), and connect again on the channel
// of the last connection (no scan).
void wifi_radio_restart(int ch, bool full)
{
    if (s_capture_active) {
        return;
    }
    const bool was = s_state == WIFI_CONNECTED || s_state == WIFI_CONNECTING;
    wifi_ap_record_t ap;
    ESP_LOGE(TAG, "the radio restarts (stuck sends)%s, channel %d, connect again %d",
             full ? ", the WiFi driver restarts (deinit + init)" : "", ch, (int)was);
    s_restart_ms = esp_timer_get_time() / 1000;
    s_restart_eat = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;  // (an association: the stop gives one disconnect)
    esp_wifi_stop();
    if (full && !driver_again()) {
        return;
    }
    esp_wifi_start();
    if (ch >= 1 && ch <= 13) {
        esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
    }
    if (was) {
        s_ip[0] = 0;
        s_retry = 0;
        s_state = WIFI_CONNECTING;
        connect_on(s_last_ch > 0 ? s_last_ch : ch >= 1 && ch <= 13 ? ch : 0);
    }
}

// DEEP IDLE and the power test (power.c): the radio stops (off = true), or starts again and connects (off = false).
// While it is off the state is WIFI_IDLE with no address (the watchers, the beacons and the apps see no network).
static bool s_sleep_was;  // the radio stopped with a connection (or a connect): it connects again at the start
bool wifi_can_sleep(void)
{
    return !s_capture_active && !s_ap_on && !s_user_off && !s_scanning && s_state == WIFI_CONNECTED;
}

void wifi_radio_sleep(bool off)
{
    if (s_capture_active) {
        return;
    }
    if (off) {
        wifi_ap_record_t ap;
        // (a check with no connect: FAILED; the next check tries again)
        s_sleep_was = s_state == WIFI_CONNECTED || s_state == WIFI_CONNECTING || s_state == WIFI_FAILED;
        s_restart_ms = esp_timer_get_time() / 1000;
        s_restart_eat = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
        s_state = WIFI_IDLE;
        s_ip[0] = 0;
        ESP_LOGI(TAG, "the radio sleeps");
        esp_wifi_stop();
        return;
    }
    ESP_LOGI(TAG, "the radio starts (connect again %d)", (int)s_sleep_was);
    esp_wifi_start();
    if (s_sleep_was) {
        s_ip[0] = 0;
        s_retry = 0;
        s_state = WIFI_CONNECTING;
        connect_on(s_last_ch > 0 ? s_last_ch : 0);
    }
}

// The web server can fail to start when an app uses most of the internal RAM.
// Then a timer tries again each 5 s while the board has a network.
static esp_timer_handle_t s_http_retry;

static void http_retry_cb(void *arg)
{
    (void)arg;
    if (s_state == WIFI_CONNECTED || s_ap_on) {
        http_start();
    }
}

static void http_retry_later(void)
{
    if (!s_http_retry) {
        const esp_timer_create_args_t a = {.callback = http_retry_cb, .name = "http_retry"};
        if (esp_timer_create(&a, &s_http_retry) != ESP_OK) {
            return;
        }
    }
    esp_timer_stop(s_http_retry);
    esp_timer_start_once(s_http_retry, 5000000);
}

static void http_start(void)
{
    if (web_server_handle()) {
        return;
    }
    esp_err_t err = web_server_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "web server did not start (%s, internal RAM free %u), try again in 5 s", esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        http_retry_later();
        return;
    }
    ESP_LOGI(TAG, "web server on http://%s/", s_ip[0] ? s_ip : AP_IP);
}

// ---------------------------------------------------------------- events

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (s_capture_active) {
        if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
            s_scanning = false;
            esp_wifi_clear_ap_list();
        }
        return;  // capture disconnect/scan events must never reconnect or change mode
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        // a scan that an installed app started (esp_wifi_scan_start, the permission wifi.scan): its result stays in
        // the driver for the app (esp_wifi_scan_get_ap_records frees it; a read here would take it from the app)
        if (!s_scanning) {
            return;
        }
        uint16_t n = MAX_AP;
        wifi_ap_record_t *rec = heap_caps_calloc(MAX_AP, sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
        wif_nap = 0;
        if (rec && esp_wifi_scan_get_ap_records(&n, rec) == ESP_OK) {
            for (int i = 0; i < n; i++) {
                if (!rec[i].ssid[0]) {
                    continue;
                }
                bool dup = false;
                for (int k = 0; k < wif_nap; k++) {
                    dup |= !strcmp(wif_aps[k].ssid, (const char *)rec[i].ssid);
                }
                if (dup) {
                    continue;
                }
                snprintf(wif_aps[wif_nap].ssid, sizeof(wif_aps[wif_nap].ssid), "%s", (const char *)rec[i].ssid);
                wif_aps[wif_nap].rssi = rec[i].rssi;
                wif_aps[wif_nap].secure = rec[i].authmode != WIFI_AUTH_OPEN;
                wif_nap++;
            }
        }
        free(rec);
        s_scanning = false;
        if (s_auto) {
            s_auto = false;
            wif_auto_pick();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip[0] = 0;
        // (the stop of wifi_radio_restart: it connects again; a later fault of that connect gets the normal tries)
        const bool eat = s_restart_eat && esp_timer_get_time() / 1000 - s_restart_ms < 1500;
        s_restart_eat = false;
        if (eat) {
            ESP_LOGI(TAG, "disconnected by the radio restart");
            return;
        }
        // the reason: 15 = a wrong password (handshake timeout), 201 = the network is not found
        ESP_LOGW(TAG, "disconnected, reason %d (try %d)", data ? ((wifi_event_sta_disconnected_t *)data)->reason : -1,
                 s_retry);
        if (s_game_leave) {
            // (the board left an AP on another channel than its match: it goes back to the channel of the match and
            // tries again after the link ends)
            s_game_leave = false;
            s_state = WIFI_FAILED;
            const int ch = link_pair_channel();
            if (ch >= 1 && ch <= 13) {
                esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
            }
            retry_later(RETRY_HOLD_US);
            return;
        }
        if (s_state == WIFI_CONNECTING || s_state == WIFI_CONNECTED) {
            // A link in use (A1) or a lobby that waits: at most one try, only on the channel of the last connection
            // (no scan of the other channels: the game or the search stays on its channel), and no hotspot (the
            // hotspot of AUTO uses channel 1). The full tries and the hotspot come after the link ends (retry_cb).
            const bool game = link_in_use();
            const bool hold = game || friends_lobby_waits() || link_ready();
            // a radio restart in the last 30 s: the AP can refuse the first connects after it (the old association
            // got no deauthentication), so 3 tries on the last channel in a hold, and the next try after 5 s
            const bool after_restart = s_restart_ms && esp_timer_get_time() / 1000 - s_restart_ms < 30000;
            const int hold_tries = after_restart ? 3 : 1;
            if (hold ? (s_retry++ < hold_tries && s_last_ch > 0) : s_retry++ < 4) {
                s_state = WIFI_CONNECTING;
                connect_on(hold ? s_last_ch : 0);
            } else {
                s_state = WIFI_FAILED;
                snprintf(wif_bad, sizeof wif_bad, "%s", s_ssid);  // (the next try takes another saved network first)
                if (wifi_hotspot_mode() == HOTSPOT_AUTO && !hold) {
                    hotspot_auto_up();  // no WiFi: the own network (2 min)
                }
                // (a JOIN that failed: back to the saved one)
                retry_later(s_pend || game || after_restart ? RETRY_HOLD_US : hold ? LOBBY_TRY_US : 60000000);
            }
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        const int ch = data ? ((wifi_event_sta_connected_t *)data)->channel : 0;
        if (ch) {
            s_last_ch = ch;
        }
        const int game = link_in_use() ? link_pair_channel() : 0;
        if (game && ch && ch != game) {
            // a 2-board match runs on another channel (a connect that started before it): the board leaves this AP
            // (it stays saved), else the other board loses it; the next try comes after the link ends
            ESP_LOGW(TAG, "the AP is on channel %d, the match on channel %d: the board leaves the AP for the match", ch,
                     game);
            s_game_leave = true;
            esp_wifi_disconnect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        s_ap_clients++;
        ESP_LOGI(TAG, "a phone joined the hotspot (%d)", s_ap_clients);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        if (s_ap_clients > 0) {
            s_ap_clients--;
        }
        if (s_ap_clients == 0 && s_ap_on && s_ap_timed) {
            ap_timer_arm();  // the last phone left: 2 min more
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        if (s_user_off || s_game_leave) {  // (an address that came after DISCONNECT: the board leaves again)
            esp_wifi_disconnect();
            return;
        }
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_state = WIFI_CONNECTED;
        s_retry = 0;
        ESP_LOGI(TAG, "connected to %s, ip %s", s_ssid, s_ip);
        wif_bad[0] = 0;
        if (s_pend && !strcmp(s_pend_ssid, s_ssid)) {
            // the new network works: now it is the first saved network (the others stay)
            wif_nets_put_first(s_pend_ssid, s_pend_pass);
            s_pend = false;
            memset(s_pend_pass, 0, sizeof s_pend_pass);
        } else if (wif_nets_load()) {
            int i = wif_nets_find(s_ssid);  // a saved network: it goes to the first place
            if (i > 0) {
                char pw[65];
                snprintf(pw, sizeof pw, "%s", wif_nets[i].pass);
                wif_nets_put_first(s_ssid, pw);
                memset(pw, 0, sizeof pw);
            }
        }
        http_start();
        lan_start();  // the arcade boards on this network (UDP beacons, link packets)
        // AUTO: the WiFi works now, so the hotspot can stop (not while a phone uses it)
        if (wifi_hotspot_mode() == HOTSPOT_AUTO && s_ap_on && s_ap_clients == 0) {
            hotspot_down();
        }
    }
}

// ---------------------------------------------------------------- API

bool wifi_start(void)
{
    if (s_state != WIFI_OFF) {
        return true;
    }
    ESP_LOGI(TAG, "start, internal RAM free %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    if (esp_netif_init() != ESP_OK) {
        return false;
    }
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "not enough memory for WiFi");
        return false;
    }
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (esp_wifi_start() != ESP_OK) {
        return false;
    }
    s_state = WIFI_IDLE;
    ESP_LOGI(TAG, "started, internal RAM free %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    return true;
}

wifi_state_t wifi_state(void)
{
    return s_state;
}

// the signal of the network in use (dBm), read at most every 2 s; 0 = not connected
int wifi_rssi(void)
{
    static int64_t s_at;
    static int s_rssi;
    if (s_state != WIFI_CONNECTED) {
        return 0;
    }
    int64_t now = esp_timer_get_time();
    if (!s_at || now - s_at > 2000000) {
        s_at = now;
        wifi_ap_record_t ap;
        s_rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    }
    return s_rssi;
}

const char *wifi_ip(void)
{
    return s_ip;
}

const char *wifi_ssid(void)
{
    if (!s_ssid[0] && wif_nets_load() && wif_nnet > 0) {
        return wif_nets[0].ssid;
    }
    return s_ssid;
}

void wifi_scan(void)
{
    if (s_capture_active) {
        return;
    }
    if (!wifi_start() || s_scanning) {
        return;
    }
    if (s_state == WIFI_CONNECTING) {
        return;  // a scan would stop the connection attempt
    }
    s_scanning = true;
    if (esp_wifi_scan_start(NULL, false) != ESP_OK) {
        s_scanning = false;
    }
}

bool wifi_scanning(void)
{
    return s_scanning;
}

int wifi_scan_results(wifi_ap_t *out, int max)
{
    int n = wif_nap < max ? wif_nap : max;
    memcpy(out, wif_aps, sizeof(wifi_ap_t) * n);
    return n;
}

void wifi_connect(const char *ssid, const char *pass, bool save)
{
    if (s_capture_active) {
        return;
    }
    s_user_off = false;
    if (save) {
        s_pend = false;  // (a direct save wins over a JOIN that waits)
    }
    s_auto = false;  // (and over an automatic pick after a scan that runs now)
    if (!wifi_start()) {
        s_state = WIFI_FAILED;
        return;
    }
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    snprintf((char *)wc.sta.ssid, sizeof(wc.sta.ssid), "%s", ssid);
    snprintf((char *)wc.sta.password, sizeof(wc.sta.password), "%s", pass ? pass : "");
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    if (save) {
        wif_nets_put_first(ssid, pass ? pass : "");
    }
    if (link_in_use() && s_last_ch > 0) {
        // (a 2-board match runs: only the channel of the last connection, no scan of the other channels; the match
        // stays on its channel, and a network on another channel comes after the link ends)
        wc.sta.channel = (uint8_t)s_last_ch;
    }
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    s_retry = 0;
    s_ip[0] = 0;
    s_state = WIFI_CONNECTING;
    esp_wifi_connect();
}
// SETUP: join a network; it becomes the saved network only when the board gets an IP address on it (a wrong network
// or a wrong password keeps the old saved network, and a restart goes back to it)
void wifi_connect_try(const char *ssid, const char *pass)
{
    if (s_capture_active) {
        return;
    }
    wifi_connect(ssid, pass, false);
    snprintf(s_pend_ssid, sizeof s_pend_ssid, "%s", ssid);
    snprintf(s_pend_pass, sizeof s_pend_pass, "%s", pass ? pass : "");
    s_pend = true;
}

// FORGET: only the network in use (or the first saved one); the other saved networks stay, and the board tries them
void wifi_forget(void)
{
    wifi_forget_name(wifi_ssid());
}

// one saved network by its name; the network in use: the board leaves it
void wifi_forget_name(const char *name)
{
    if (s_capture_active) {
        return;
    }
    char ssid[33];
    snprintf(ssid, sizeof ssid, "%s", name);
    int i = wif_nets_load() ? wif_nets_find(ssid) : -1;
    if (i >= 0) {
        memmove(&wif_nets[i], &wif_nets[i + 1], (wif_nnet - i - 1) * sizeof(wnet_t));
        memmove(&wif_manual[i], &wif_manual[i + 1], (wif_nnet - i - 1) * sizeof(bool));
        wif_manual[wif_nnet - 1] = false;
        memset(&wif_nets[--wif_nnet], 0, sizeof(wnet_t));
        wif_nets_save();
    }
    ESP_LOGI(TAG, "forgot the network %s (%d saved)", ssid, wif_nnet);
    if (strcmp(ssid, s_ssid) && s_ssid[0]) {
        return;  // (another network is in use: it stays)
    }
    s_ssid[0] = 0;
    s_pend = false;
    if (s_state != WIFI_OFF) {
        s_state = WIFI_IDLE;
        esp_wifi_disconnect();
        if (wif_nnet > 0 && !s_user_off) {
            wifi_autoconnect();  // (after DISCONNECT: no other network either)
        }
    }
}

void wifi_forget_all(void)
{
    if (s_capture_active) {
        return;
    }
    if (wif_nets_load()) {
        memset(wif_nets, 0, WIFI_SAVED_MAX * sizeof(wnet_t));
        memset(wif_manual, 0, sizeof wif_manual);
        wif_nnet = 0;
    }
    wif_nets_save();
    s_ssid[0] = 0;
    s_pend = false;
    if (s_state != WIFI_OFF) {
        s_state = WIFI_IDLE;
        esp_wifi_disconnect();
    }
}

// DISCONNECT: leave the network, keep it saved, and make no new try (also the timer of a failed one) until
// wifi_reconnect(), a connect, or the next start-up. The hotspot stays as it is.
void wifi_disconnect(void)
{
    if (s_capture_active || s_state == WIFI_OFF) {
        return;
    }
    s_user_off = true;
    s_pend = false;
    memset(s_pend_pass, 0, sizeof s_pend_pass);
    s_auto = false;
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
    }
    s_state = WIFI_IDLE;  // (first: the disconnect event then makes no new try)
    esp_wifi_disconnect();
    s_ip[0] = 0;
    ESP_LOGI(TAG, "disconnected by the user (%s stays saved)", wifi_ssid());
}

void wifi_reconnect(void)
{
    if (s_capture_active) {
        return;
    }
    ESP_LOGI(TAG, "connect again");
    int i = wif_nets_load() && s_ssid[0] ? wif_nets_find(s_ssid) : -1;
    if (i >= 0) {
        wifi_connect(wif_nets[i].ssid, wif_nets[i].pass, false);  // (the network it left, also a manual one)
    } else {
        wifi_autoconnect();
    }
}

bool wifi_user_off(void)
{
    return s_user_off;
}

bool wifi_saved(void)
{
    return wifi_ssid()[0] != 0;
}

void wifi_autoconnect(void)
{
    if (s_capture_active) {
        return;
    }
    s_user_off = false;
    bool ok = wif_nets_load() && wif_count_auto() > 0;  // (the manual networks only by hand)
    hotspot_mode_t m = wifi_hotspot_mode();
    if (m == HOTSPOT_ON) {
        hotspot_up();
        s_ap_timed = false;
    } else if (m == HOTSPOT_AUTO && !ok) {
        hotspot_auto_up();  // no saved network: 2 min for a phone (once after the start-up)
    }
    if (!ok) {
        return;
    }
    if (wif_count_auto() == 1 || !wifi_start()) {
        int i = wif_first_auto();
        wifi_connect(wif_nets[i].ssid, wif_nets[i].pass, false);
        return;
    }
    // two or more saved networks: a scan first (SCAN_DONE: auto_pick)
    s_auto = true;
    s_state = WIFI_CONNECTING;
    if (s_scanning) {
        return;  // (a scan of SETUP runs: its result comes to auto_pick too)
    }
    s_scanning = true;
    if (esp_wifi_scan_start(NULL, false) != ESP_OK) {
        s_scanning = false;
        s_auto = false;
        wifi_connect(wif_nets[0].ssid, wif_nets[0].pass, false);
    }
}

// ---------------------------------------------------------------- hotspot

static void hotspot_up(void)
{
    if (s_ap_on || !wifi_start()) {
        return;
    }
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    wifi_hotspot_ssid();
    wifi_config_t ap;
    memset(&ap, 0, sizeof(ap));
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "%s", s_ap_ssid);
    ap.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    ap.ap.authmode = WIFI_AUTH_OPEN;  // no password: an open network
    ap.ap.max_connection = 4;
    ap.ap.channel = 1;  // with a WiFi connection the AP uses its channel
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) {
        ESP_LOGE(TAG, "hotspot did not start");
        return;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);  // phones on the AP need the radio on
    s_ap_on = true;
    s_ap_clients = 0;
    ESP_LOGI(TAG, "hotspot %s on, web page http://%s/", s_ap_ssid, AP_IP);
    http_start();
}

static void hotspot_down(void)
{
    if (!s_ap_on) {
        return;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    s_ap_on = false;
    wifi_ps_apply();
    s_ap_clients = 0;
    s_ap_timed = false;
    if (s_ap_timer) {
        esp_timer_stop(s_ap_timer);
    }
    ESP_LOGI(TAG, "hotspot off");
}

// the 2 min of AUTO end: the hotspot closes (a phone on it: 2 min more; its leave also gives 2 min)
static void ap_timer_cb(void *arg)
{
    (void)arg;
    if (!s_ap_on || !s_ap_timed || s_capture_active) {
        return;
    }
    if (s_ap_clients > 0) {
        ap_timer_arm();
        return;
    }
    ESP_LOGI(TAG, "the hotspot of AUTO closes (2 min with no phone)");
    hotspot_down();
}

static void ap_timer_arm(void)
{
    if (!s_ap_timer) {
        const esp_timer_create_args_t a = {.callback = ap_timer_cb, .name = "ap_auto"};
        if (esp_timer_create(&a, &s_ap_timer) != ESP_OK) {
            return;
        }
    }
    esp_timer_stop(s_ap_timer);
    s_ap_end_us = esp_timer_get_time() + AP_AUTO_US;
    esp_timer_start_once(s_ap_timer, AP_AUTO_US);
}

// AUTO: the hotspot for 2 min, only one time after each start-up
static void hotspot_auto_up(void)
{
    if (s_ap_auto_done || s_ap_on) {
        return;
    }
    s_ap_auto_done = true;
    hotspot_up();
    if (!s_ap_on) {
        return;
    }
    s_ap_timed = true;
    ap_timer_arm();
    ESP_LOGI(TAG, "the hotspot of AUTO closes in 2 min (unless a phone is on it)");
}

int wifi_hotspot_left_s(void)
{
    if (!s_ap_on || !s_ap_timed) {
        return -1;
    }
    int64_t left = s_ap_end_us - esp_timer_get_time();
    return left > 0 ? (int)((left + 999999) / 1000000) : 0;
}

hotspot_mode_t wifi_hotspot_mode(void)
{
    uint32_t m = store_get("hotspot", HOTSPOT_AUTO);
    return m < HOTSPOT_MODES ? (hotspot_mode_t)m : HOTSPOT_AUTO;
}

void wifi_hotspot_set_mode(hotspot_mode_t m)
{
    if (s_capture_active) {
        return;
    }
    if (m >= HOTSPOT_MODES) {
        return;
    }
    store_set("hotspot", (uint32_t)m);
    if (m == HOTSPOT_ON) {
        hotspot_up();
        s_ap_timed = false;
        if (s_ap_timer) {
            esp_timer_stop(s_ap_timer);
        }
    } else if (m == HOTSPOT_OFF) {
        hotspot_down();
    } else if (s_state != WIFI_CONNECTED) {
        // AUTO with no WiFi now (the mode is set in SETUP): a new window of 2 min
        if (s_ap_on) {
            s_ap_timed = true;
            ap_timer_arm();
        } else {
            s_ap_auto_done = false;
            hotspot_auto_up();
        }
    } else if (s_ap_clients == 0) {
        hotspot_down();
    }
}

void wifi_hotspot_start(void)
{
    if (s_capture_active) {
        return;
    }
    hotspot_up();  // START: no time limit (until STOP or the next start-up)
    s_ap_timed = false;
    if (s_ap_timer) {
        esp_timer_stop(s_ap_timer);
    }
}

void wifi_hotspot_stop(void)
{
    if (s_capture_active) {
        return;
    }
    hotspot_down();
}

bool wifi_hotspot_on(void)
{
    return s_ap_on;
}

static bool s_saver_on;  // wifi_saver(): the screen is off
static bool s_xfer_on;   // wifi_xfer(): a transfer runs

// The power save of the station for the state now. A transfer (an OTA, an app install) needs the full radio: with the
// most power save (the screen off) the radio wakes only at some beacons, and the data of an OTA stopped for 60 s. A
// radio link of 2 BOARDS needs the radio on all the time (link.c), but not a QUIET WALKIE (link_quiet).
void wifi_ps_apply(void)
{
    if (s_capture_active || s_state == WIFI_OFF || s_ap_on) {
        return;
    }
    wifi_ps_type_t ps = s_saver_on ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM;
    if (s_xfer_on) {
        ps = WIFI_PS_NONE;
    } else if (link_ready()) {
        ps = link_quiet_on() ? WIFI_PS_MAX_MODEM : WIFI_PS_NONE;
    }
    esp_wifi_set_ps(ps);
}

void wifi_saver(bool on)
{
    s_saver_on = on;
    wifi_ps_apply();
}

void wifi_xfer(bool on)
{
    if (on == s_xfer_on) {
        return;
    }
    s_xfer_on = on;
    ESP_LOGI(TAG, "a transfer %s: the WiFi power save %s", on ? "runs" : "ended", on ? "is off" : "comes back");
    wifi_ps_apply();
}

const char *wifi_hotspot_ssid(void)
{
    if (!s_ap_ssid[0]) {
        uint8_t mac[6] = {0};
        arc_mac_ap(mac);
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "TFN-%02X%02X", mac[4], mac[5]);
    }
    return s_ap_ssid;
}

const char *wifi_hotspot_ip(void)
{
    return AP_IP;
}

int wifi_hotspot_clients(void)
{
    return s_ap_clients;
}

const char *wifi_web_ip(void)
{
    if (s_state == WIFI_CONNECTED && s_ip[0]) {
        return s_ip;
    }
    if (s_ap_on) {
        return AP_IP;
    }
    return NULL;
}

#if CONFIG_TFN_EXTRA_WIFI_CAPTURE
#include "wifi_capture.inc"
#else
#include "wifi_capture.h"
// The extra WiFi capture is off: the ABI names stay, and they do no capture.
int32_t wifi_capture_begin(const wifi_capture_config_t *cfg, wifi_capture_rx_cb_t cb, void *ctx)
{
    (void)cfg;
    (void)cb;
    (void)ctx;
    return ESP_ERR_NOT_SUPPORTED;
}
int32_t wifi_capture_end(void)
{
    return ESP_OK;  // (tfn_app.c calls it when an app closes)
}
int32_t wifi_capture_deauth(const uint8_t client[6])
{
    (void)client;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
