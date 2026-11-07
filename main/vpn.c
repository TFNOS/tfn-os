// VPN: WireGuard to the hub (see vpn.h). The component: trombik/esp_wireguard (BSD).

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_netif_sntp.h"
#include "lwip/inet.h"
#include "nvs.h"
#include "esp_wireguard.h"
#include "vpn.h"
#include "wifi.h"
#include "tfn_defaults.h"

static const char *TAG = "vpn";

// the x25519 function of the component (src/crypto/refc/x25519.c)
int x25519(unsigned char out[32], const unsigned char scalar[32], const unsigned char base[32], int clamp);

static vpn_cfg_t s_cfg;
static char s_priv64[48], s_pub64[48];
static volatile vpn_state_t s_state = VPN_OFF;
static volatile bool s_restart;
static bool s_keys;  // s_priv64 and s_pub64 hold the key pair (written one time, then only read)
static int s_making;
static volatile uint32_t s_my_ip, s_hub_ip;

// ---------------------------------------------------------------- base64

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void b64_encode(const uint8_t *in, int n, char *out)
{
    int k = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[k++] = B64[(v >> 18) & 63];
        out[k++] = B64[(v >> 12) & 63];
        out[k++] = i + 1 < n ? B64[(v >> 6) & 63] : '=';
        out[k++] = i + 2 < n ? B64[v & 63] : '=';
    }
    out[k] = 0;
}

// a WireGuard key: 44 characters of base64 (32 bytes)
static bool key_ok(const char *s)
{
    if (strlen(s) != 44 || s[43] != '=') {
        return false;
    }
    for (int i = 0; i < 43; i++) {
        if (!strchr(B64, s[i])) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- the settings

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("vpn", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "cfg", &s_cfg, sizeof s_cfg);
        nvs_commit(h);
        nvs_close(h);
    }
}

// The public key (s_pub64) of the private key s_priv64.
static void pub_of_priv(void)
{
    uint8_t priv[32] = {0}, pub[32];
    int k = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (int i = 0; i < 43 && k < 32; i++) {
        acc = acc << 6 | (uint32_t)(strchr(B64, s_priv64[i]) - B64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            priv[k++] = (uint8_t)(acc >> bits);
        }
    }
    static const unsigned char base[32] = {9};
    x25519(pub, priv, base, 1);
    memset(priv, 0, sizeof priv);
    b64_encode(pub, 32, s_pub64);
    __atomic_store_n(&s_keys, true, __ATOMIC_RELEASE);
}

// At the start: the key of the NVS. A board with no key makes it later (keys_make), after the WiFi start: the true
// random numbers of ESP-IDF need the radio on. The keys that exist do not change.
static void keys_init(void)
{
    nvs_handle_t h;
    size_t n = sizeof s_priv64;
    if (nvs_open("vpn", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    const bool ok = nvs_get_str(h, "priv", s_priv64, &n) == ESP_OK && key_ok(s_priv64);
    nvs_close(h);
    if (ok) {
        pub_of_priv();
    }
}

// A new key pair, one time, when the radio is on (the id task of tfn_id.c, the VPN task, a read of the public key).
static void keys_make(void)
{
    if (__atomic_load_n(&s_keys, __ATOMIC_ACQUIRE) || wifi_state() == WIFI_OFF ||
        __atomic_exchange_n(&s_making, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open("vpn", NVS_READWRITE, &h) == ESP_OK) {
        uint8_t priv[32];
        esp_fill_random(priv, sizeof priv);
        priv[0] &= 248;
        priv[31] = (priv[31] & 127) | 64;
        b64_encode(priv, 32, s_priv64);
        memset(priv, 0, sizeof priv);
        if (nvs_set_str(h, "priv", s_priv64) == ESP_OK && nvs_commit(h) == ESP_OK) {
            ESP_LOGI(TAG, "a new key pair");
            pub_of_priv();
        }
        nvs_close(h);
    }
    __atomic_store_n(&s_making, 0, __ATOMIC_RELEASE);
}

void vpn_init(void)
{
    memset(&s_cfg, 0, sizeof s_cfg);
    nvs_handle_t h;
    if (nvs_open("vpn", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof s_cfg;
        if (nvs_get_blob(h, "cfg", &s_cfg, &n) != ESP_OK || n != sizeof s_cfg) {
            memset(&s_cfg, 0, sizeof s_cfg);
        }
        nvs_close(h);
    }
    if (!s_cfg.port) {
        s_cfg.port = 51820;
    }
    if (!s_cfg.hub_ip[0]) {
        snprintf(s_cfg.hub_ip, sizeof s_cfg.hub_ip, TFN_VPN_HUB_IP);
    }
    s_cfg.host[sizeof s_cfg.host - 1] = s_cfg.hub_key[sizeof s_cfg.hub_key - 1] = 0;
    s_cfg.ip[sizeof s_cfg.ip - 1] = s_cfg.hub_ip[sizeof s_cfg.hub_ip - 1] = 0;
    keys_init();
}

void vpn_get(vpn_cfg_t *out)
{
    *out = s_cfg;
}

bool vpn_set(const vpn_cfg_t *c)
{
    struct in_addr a;
    if (c->on &&
        (!c->host[0] || !key_ok(c->hub_key) || !inet_aton(c->ip, &a) || !inet_aton(c->hub_ip, &a) || !c->port)) {
        return false;
    }
    s_cfg = *c;
    save();
    s_restart = true;
    ESP_LOGI(TAG, "settings: %s, hub %s:%u, this board %s", s_cfg.on ? "on" : "off", s_cfg.host, s_cfg.port, s_cfg.ip);
    return true;
}

const char *vpn_public_key(void)
{
    keys_make();
    return __atomic_load_n(&s_keys, __ATOMIC_ACQUIRE) ? s_pub64 : "";
}

vpn_state_t vpn_state(void)
{
    return s_state;
}

const char *vpn_state_name(vpn_state_t s)
{
    static const char *const N[] = {"off",        "waiting for WiFi", "waiting for the time",
                                    "connecting", "connected",        "no answer from the hub"};
    return s <= VPN_ERROR ? N[s] : "?";
}

uint32_t vpn_hub_ip(void)
{
    return s_state == VPN_UP ? s_hub_ip : 0;
}

uint32_t vpn_my_ip(void)
{
    return s_state == VPN_UP ? s_my_ip : 0;
}

bool vpn_is_tunnel_ip(uint32_t ip)
{
    uint32_t mine = s_my_ip;
    return mine && (ip & htonl(0xFFFFFF00)) == (mine & htonl(0xFFFFFF00));
}

// ---------------------------------------------------------------- the task

static wireguard_config_t s_wg;
static wireguard_ctx_t s_ctx;
static bool s_wg_on;

static void wg_down(void)
{
    if (!s_wg_on) {
        return;
    }
    esp_wireguard_disconnect(&s_ctx);
    s_wg_on = false;
    ESP_LOGI(TAG, "down");
}

static bool wg_up(void)
{
    static wireguard_config_t c = ESP_WIREGUARD_CONFIG_DEFAULT();
    c.private_key = s_priv64;
    c.public_key = s_cfg.hub_key;
    c.allowed_ip = s_cfg.ip;
    c.allowed_ip_mask = "255.255.255.0";
    c.endpoint = s_cfg.host;
    c.port = s_cfg.port;
    c.persistent_keepalive = 25;
    // a new local port for each start: after a restart the router can keep the old entry of the
    // same port (seen with the path through the own public address of the router), and drop the
    // new handshakes
    c.listen_port = 40000 + (int)(esp_random() % 20000);
    s_wg = c;
    memset(&s_ctx, 0, sizeof s_ctx);
    esp_err_t e = esp_wireguard_init(&s_wg, &s_ctx);
    if (e == ESP_OK) {
        e = esp_wireguard_connect(&s_ctx);
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "connect to %s:%u: %s", s_cfg.host, s_cfg.port, esp_err_to_name(e));
        return false;
    }
    s_wg_on = true;
    struct in_addr a;
    s_my_ip = inet_aton(s_cfg.ip, &a) ? a.s_addr : 0;
    s_hub_ip = inet_aton(s_cfg.hub_ip, &a) ? a.s_addr : 0;
    ESP_LOGI(TAG, "connecting to %s:%u as %s", s_cfg.host, s_cfg.port, s_cfg.ip);
    return true;
}

static void vpn_task(void *arg)
{
    bool sntp = false;
    int64_t since = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t t = esp_log_timestamp();
        if (s_restart) {
            s_restart = false;
            wg_down();
        }
        if (!s_cfg.on) {
            wg_down();
            s_state = VPN_OFF;
            continue;
        }
        if (wifi_state() != WIFI_CONNECTED) {
            wg_down();
            s_state = VPN_WAIT_WIFI;
            continue;
        }
        if (!sntp) {
            esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_netif_sntp_init(&sc);
            sntp = true;
        }
        if (time(NULL) < 1700000000) {
            s_state = VPN_WAIT_TIME;
            continue;
        }
        if (!s_wg_on) {
            keys_make();
            if (!__atomic_load_n(&s_keys, __ATOMIC_ACQUIRE)) {
                s_state = VPN_ERROR;  // (no key: the NVS does not keep it)
                continue;
            }
            s_state = wg_up() ? VPN_CONNECTING : VPN_ERROR;
            since = t;
            continue;
        }
        if (esp_wireguardif_peer_is_up(&s_ctx) == ESP_OK) {
            if (s_state != VPN_UP) {
                ESP_LOGI(TAG, "up: this board %s, the hub %s", s_cfg.ip, s_cfg.hub_ip);
            }
            s_state = VPN_UP;
            since = t;
        } else if (t - since > 30000) {
            // no handshake for 30 s: start again (a new address of the hub name, a new NAT path)
            ESP_LOGW(TAG, "no answer from the hub %s:%u", s_cfg.host, s_cfg.port);
            wg_down();
            s_state = VPN_ERROR;
        }
    }
}

void vpn_start_task(void)
{
    static bool started;
    if (started) {
        return;
    }
    started = true;
    xTaskCreatePinnedToCore(vpn_task, "vpn", 4096, NULL, 4, NULL, 0);
}
