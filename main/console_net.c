// The console commands of the network: WiFi, the LAN, the friends, the VPN, the web password, the hotspot (the table: console.c).
#include "console_priv.h"

static const char *TAG = "console";

// "wifi"
void con_wifi(char *cmd)
{
    char *a = strtok(NULL, " \t\r\n");
    char *b = strtok(NULL, "\r\n");
    if (a && !strcmp(a, "forget")) {
        if (b) {
            wifi_forget_name(b);
        } else {
            wifi_forget();
        }
        ESP_LOGI(TAG, "wifi: forgotten");
    } else if (a && !strcmp(a, "disconnect")) {
        wifi_disconnect();
        ESP_LOGI(TAG, "wifi: disconnected (the network stays saved)");
    } else if (a && !strcmp(a, "reconnect")) {
        wifi_reconnect();
        ESP_LOGI(TAG, "wifi: connecting to the saved network");
    } else if (a && !strcmp(a, "forgetall")) {
        wifi_forget_all();
        ESP_LOGI(TAG, "wifi: all networks forgotten");
    } else if (a && !strcmp(a, "auto") && b) {  // wifi auto <name> on|off (the last word)
        char *sp = strrchr(b, ' ');
        bool on = sp && !strcmp(sp + 1, "on");
        if (!sp || (!on && strcmp(sp + 1, "off"))) {
            ESP_LOGI(TAG, "use: wifi auto <name> on|off");
        } else {
            *sp = 0;
            ESP_LOGI(TAG, "wifi: %s",
                     wifi_saved_set_auto(b, on) ? (on ? "AUTO JOIN on" : "AUTO JOIN off (by hand)")
                                                : "not a saved network");
        }
    } else if (a && !strcmp(a, "list")) {  // (the names only, never the passwords)
        for (int i = 0; i < wifi_saved_count(); i++) {
            ESP_LOGI(TAG, "wifi: saved %d: %s%s", i + 1, wifi_saved_name(i),
                     wifi_saved_auto(wifi_saved_name(i)) ? "" : " (by hand)");
        }
        ESP_LOGI(TAG, "wifi: %d saved", wifi_saved_count());
    } else if (a) {
        wifi_connect(a, b ? b : "", true);
        ESP_LOGI(TAG, "wifi: connecting to %s", a);
    } else {
        ESP_LOGI(TAG, "use: wifi <name> <password> | wifi disconnect | wifi reconnect | wifi auto <name> on|off | "
                      "wifi forget [name] | wifi forgetall | wifi list");
    }
}

// "friends"
void con_friends(char *cmd)
{
    // FRIENDS: the list and who is near; "friends clear" removes all, "friends rm N" friend N (tests)
    char *a = strtok(NULL, " \t\r\n");
    if (a && !strcmp(a, "clear")) {
        while (friends_count()) {
            friends_remove(friends_count() - 1);
        }
    }
    if (a && !strcmp(a, "rm")) {
        char *n = strtok(NULL, " \t\r\n");
        if (n) {
            friends_remove(atoi(n));
        }
    }
    int near[FRIENDS_MAX], nn = friends_near(near, FRIENDS_MAX);
    ESP_LOGI(TAG, "friends: this board %s, %d friends, %d near", friends_my_name(), friends_count(), nn);
    for (int i = 0; i < friends_count(); i++) {
        const friend_t *f = friends_get(i);
        ESP_LOGI(TAG, "friend %d %s %02X%02X", i, f->name, f->mac[4], f->mac[5]);
    }
    friend_new_t nb[FRIENDS_NEW_MAX];
    int k = friends_new_near(nb, FRIENDS_NEW_MAX);
    for (int i = 0; i < k; i++) {
        ESP_LOGI(TAG, "new board near: %s", nb[i].name);
    }
}

// "lan"
void con_lan(char *cmd)
{
    // the arcade boards on this WiFi network (lan.c)
    static lan_peer_t p[LAN_PEERS];
    int n = lan_peers(p, LAN_PEERS);
    ESP_LOGI(TAG, "lan: %d boards", n);
    for (int i = 0; i < n; i++) {
        uint32_t ip = p[i].ip;
        ESP_LOGI(TAG, "lan board %s %u.%u.%u.%u app %s%s", p[i].name, (unsigned)(ip & 255), (unsigned)((ip >> 8) & 255),
                 (unsigned)((ip >> 16) & 255), (unsigned)(ip >> 24), p[i].app, p[i].vpn ? " (VPN)" : "");
    }
}

// "lancopy"
void con_lancopy(char *cmd)
{
    // tests: 0 = no LAN copy of the sent link packets (ESP-NOW only), 1 = both (the default)
    extern volatile bool g_link_no_lan;
    char *a = strtok(NULL, " \t\r\n");
    g_link_no_lan = a && !atoi(a);
    ESP_LOGI(TAG, "lancopy %d", (int)!g_link_no_lan);
}

// "wifistat"
void con_wifistat(char *cmd)
{
    // the state of the radio and the statistics of the WiFi driver (link.c link_diag)
    extern void link_diag(const char *why);
    link_diag("console");
}

// "lanonly"
void con_lanonly(char *cmd)
{
    // tests: 1 = the link ignores the ESP-NOW packets (only the LAN path), 0 = both
    extern volatile bool g_link_lan_only;
    char *a = strtok(NULL, " \t\r\n");
    g_link_lan_only = a && atoi(a);
    ESP_LOGI(TAG, "lanonly %d", (int)g_link_lan_only);
}

// "webpass"
void con_webpass(char *cmd)
{
    // the web password (web_auth.h; the USB console is the way back when it is forgotten): "webpass" = on or off,
    // "webpass set <password>" (the rest of the line, spaces too), "webpass off"
    char *a = strtok(NULL, " \t\r\n");
    char msg[64] = "";
    if (a && !strcmp(a, "set")) {
        char *p = strtok(NULL, "\r\n");
        if (!p || !web_auth_set(p, msg, sizeof msg)) {
            printf("webpass: %s\n", msg[0] ? msg : "give the password");
        }
    } else if (a && !strcmp(a, "off")) {
        web_auth_set("", msg, sizeof msg);
    }
    printf("webpass: %s\n", web_auth_on() ? "on" : "off");
}

#if CONFIG_TFN_EXTRA_VPN
// "vpn"
void con_vpn(char *cmd)
{
    // the VPN onboarding by USB (a new board gets its record on the hub at its first
    // flash): "vpn" prints the public key and the settings; "vpn set <host> <port> <hub key>
    // <ip> <hub ip>" saves them and turns the VPN on; "vpn off" turns it off
    char *a = strtok(NULL, " \t\r\n");
    vpn_cfg_t c;
    vpn_get(&c);
    if (a && !strcmp(a, "set")) {
        char *h = strtok(NULL, " \t\r\n"), *p = strtok(NULL, " \t\r\n"), *k = strtok(NULL, " \t\r\n");
        char *ip = strtok(NULL, " \t\r\n"), *hip = strtok(NULL, " \t\r\n");
        if (h && p && k && ip && hip) {
            c.on = true;
            snprintf(c.host, sizeof c.host, "%s", h);
            c.port = (uint16_t)atoi(p);
            snprintf(c.hub_key, sizeof c.hub_key, "%s", k);
            snprintf(c.ip, sizeof c.ip, "%s", ip);
            snprintf(c.hub_ip, sizeof c.hub_ip, "%s", hip);
            printf("VPN SET %s\n", vpn_set(&c) ? "OK" : "BAD (the hub, the key: 44 characters, the addresses)");
        } else {
            printf("VPN SET BAD (vpn set <host> <port> <hub key> <ip> <hub ip>)\n");
        }
        vpn_get(&c);
    } else if (a && !strcmp(a, "off")) {
        c.on = false;
        printf("VPN SET %s\n", vpn_set(&c) ? "OK" : "BAD");
    }
    uint8_t m[6] = {0};
    arc_mac(m);  // (the MAC that the manager shows)
    printf("VPN PUB %s ON %d HOST %s PORT %u IP %s HUB %s STATE %s MAC %02X%02X%02X%02X%02X%02X\n", vpn_public_key(),
           (int)c.on, c.host[0] ? c.host : "-", (unsigned)c.port, c.ip[0] ? c.ip : "-", c.hub_ip[0] ? c.hub_ip : "-",
           vpn_state_name(vpn_state()), m[0], m[1], m[2], m[3], m[4], m[5]);
}

// "vpnonly"
void con_vpnonly(char *cmd)
{
    // tests: 1 = as a board in another house: no ESP-NOW, no LAN, only the VPN tunnel
    extern volatile bool g_link_lan_only, g_lan_vpn_only;
    char *a = strtok(NULL, " \t\r\n");
    g_lan_vpn_only = g_link_lan_only = a && atoi(a);
    ESP_LOGI(TAG, "vpnonly %d", (int)g_lan_vpn_only);
}
#endif

// "hotspot"
void con_hotspot(char *cmd)
{
    char *a = strtok(NULL, " \t\r\n");
    if (a && !strcmp(a, "on")) {
        wifi_hotspot_start();
    } else if (a && !strcmp(a, "off")) {
        wifi_hotspot_stop();
    } else if (a && !strcmp(a, "auto")) {
        wifi_hotspot_set_mode(HOTSPOT_AUTO);
    }
    ESP_LOGI(TAG, "hotspot %s (%s mode), %s, %d phones", wifi_hotspot_on() ? "on" : "off",
             HOTSPOT_MODE_NAMES[wifi_hotspot_mode()], wifi_hotspot_ssid(), wifi_hotspot_clients());
}

// "ip"
void con_ip(char *cmd)
{
    static const char *const ST[] = {"off", "idle", "connecting", "connected", "failed"};
    ESP_LOGI(TAG, "wifi %s, network %s, ip %s", ST[wifi_state()], wifi_ssid(), wifi_ip());
}
