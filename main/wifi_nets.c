// WiFi: the saved networks (the NVS list, AUTO JOIN, the network to join after a scan).
#include "wifi_priv.h"

static const char *TAG = "wifi";

wnet_t *wif_nets;  // PSRAM, WIFI_SAVED_MAX places (NULL = not read yet)
int wif_nnet;
// AUTO JOIN of each saved network (s_manual[i] goes with s_nets[i]): a manual network stays saved but the board joins
// it only by hand (SETUP or the console), never at the start or in a new try. The NVS blob "manual" (namespace "wifi")
// keeps the names of the manual networks (an older firmware does not read it: all its networks are AUTO). Default:
// AUTO, as before.
bool wif_manual[WIFI_SAVED_MAX];
char wif_bad[33];  // the network that failed last ("" = none)

// ---------------------------------------------------------------- the saved networks

bool wif_nets_load(void)
{
    if (wif_nets) {
        return true;
    }
    wif_nets = heap_caps_calloc(WIFI_SAVED_MAX, sizeof(wnet_t), MALLOC_CAP_SPIRAM);
    if (!wif_nets) {
        return false;
    }
    wif_nnet = 0;
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) {
        return true;
    }
    size_t len = WIFI_SAVED_MAX * sizeof(wnet_t);
    if (nvs_get_blob(h, "nets", wif_nets, &len) == ESP_OK && len % sizeof(wnet_t) == 0) {
        wif_nnet = (int)(len / sizeof(wnet_t));
    } else {  // (the firmware before the list: one network)
        size_t l1 = sizeof(wif_nets[0].ssid), l2 = sizeof(wif_nets[0].pass);
        if (nvs_get_str(h, "ssid", wif_nets[0].ssid, &l1) == ESP_OK && wif_nets[0].ssid[0]) {
            nvs_get_str(h, "pass", wif_nets[0].pass, &l2);
            wif_nnet = 1;
        }
    }
    for (int i = 0; i < wif_nnet; i++) {  // (a bad blob: no text without its end)
        wif_nets[i].ssid[sizeof(wif_nets[i].ssid) - 1] = 0;
        wif_nets[i].pass[sizeof(wif_nets[i].pass) - 1] = 0;
    }
    char man[WIFI_SAVED_MAX][33];
    size_t ml = sizeof man;
    memset(wif_manual, 0, sizeof wif_manual);
    if (nvs_get_blob(h, "manual", man, &ml) == ESP_OK && ml % 33 == 0) {
        for (int k = 0; k < (int)(ml / 33); k++) {
            man[k][32] = 0;
            for (int i = 0; i < wif_nnet; i++) {
                if (!strcmp(wif_nets[i].ssid, man[k])) {
                    wif_manual[i] = true;
                }
            }
        }
    }
    nvs_close(h);
    return true;
}

void wif_nets_save(void)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    if (wif_nnet > 0) {
        nvs_set_blob(h, "nets", wif_nets, wif_nnet * sizeof(wnet_t));
        nvs_set_str(h, "ssid", wif_nets[0].ssid);
        nvs_set_str(h, "pass", wif_nets[0].pass);
        char man[WIFI_SAVED_MAX][33];
        int k = 0;
        memset(man, 0, sizeof man);
        for (int i = 0; i < wif_nnet; i++) {
            if (wif_manual[i]) {
                snprintf(man[k++], 33, "%s", wif_nets[i].ssid);
            }
        }
        if (k) {
            nvs_set_blob(h, "manual", man, (size_t)k * 33);
        } else {
            nvs_erase_key(h, "manual");
        }
    } else {
        nvs_erase_all(h);
    }
    nvs_commit(h);
    nvs_close(h);
}

int wif_nets_find(const char *ssid)
{
    for (int i = 0; i < wif_nnet; i++) {
        if (!strcmp(wif_nets[i].ssid, ssid)) {
            return i;
        }
    }
    return -1;
}

// the network goes to the first place (a new one pushes out the oldest of WIFI_SAVED_MAX); no flash write when it is
// already first with this password
void wif_nets_put_first(const char *ssid, const char *pass)
{
    if (!ssid[0] || !wif_nets_load()) {
        return;
    }
    int i = wif_nets_find(ssid);
    if (i == 0 && !strcmp(wif_nets[0].pass, pass)) {
        return;
    }
    bool man = i >= 0 && wif_manual[i];
    if (i < 0) {
        i = wif_nnet < WIFI_SAVED_MAX ? wif_nnet++ : WIFI_SAVED_MAX - 1;
    }
    memmove(&wif_nets[1], &wif_nets[0], i * sizeof(wnet_t));
    memmove(&wif_manual[1], &wif_manual[0], i * sizeof(bool));
    wif_manual[0] = man;
    memset(&wif_nets[0], 0, sizeof(wnet_t));
    snprintf(wif_nets[0].ssid, sizeof(wif_nets[0].ssid), "%s", ssid);
    snprintf(wif_nets[0].pass, sizeof(wif_nets[0].pass), "%s", pass);
    wif_nets_save();
    ESP_LOGI(TAG, "saved the network %s (%d saved)", ssid, wif_nnet);
}

// the first saved network with AUTO JOIN (-1: none)
int wif_first_auto(void)
{
    for (int i = 0; i < wif_nnet; i++) {
        if (!wif_manual[i]) {
            return i;
        }
    }
    return -1;
}

int wif_count_auto(void)
{
    int n = 0;
    for (int i = 0; i < wif_nnet; i++) {
        n += !wif_manual[i];
    }
    return n;
}

// SCAN_DONE after wifi_autoconnect: the first saved network in the scan (the one that failed last only when no other
// one is there); none in the scan (a hidden network): the first saved network
void wif_auto_pick(void)
{
    int best = -1;
    for (int i = 0; i < wif_nnet && best < 0; i++) {
        if (wif_manual[i] || !strcmp(wif_nets[i].ssid, wif_bad)) {
            continue;
        }
        for (int k = 0; k < wif_nap; k++) {
            if (!strcmp(wif_aps[k].ssid, wif_nets[i].ssid)) {
                best = i;
                break;
            }
        }
    }
    if (best < 0) {
        int b = wif_nets_find(wif_bad);
        if (b >= 0 && wif_manual[b]) {
            b = -1;
        }
        for (int k = 0; b >= 0 && k < wif_nap; k++) {
            if (!strcmp(wif_aps[k].ssid, wif_bad)) {
                best = b;
                break;
            }
        }
    }
    ESP_LOGI(TAG, "%d networks in the scan, %d saved: %s", wif_nap, wif_nnet,
             best >= 0 ? wif_nets[best].ssid : "none of them");
    for (int i = 0; i < wif_nnet && best < 0; i++) {  // (none in the scan: the first one that did not fail last)
        if (!wif_manual[i] && strcmp(wif_nets[i].ssid, wif_bad)) {
            best = i;
        }
    }
    if (best < 0) {
        best = wif_first_auto();
    }
    if (best >= 0) {
        wifi_connect(wif_nets[best].ssid, wif_nets[best].pass, false);
    }
}

bool wifi_saved_auto(const char *ssid)
{
    int i = wif_nets_load() ? wif_nets_find(ssid) : -1;
    return i >= 0 && !wif_manual[i];
}

bool wifi_saved_set_auto(const char *ssid, bool on)
{
    int i = wif_nets_load() ? wif_nets_find(ssid) : -1;
    if (i < 0) {
        return false;
    }
    if (wif_manual[i] != !on) {
        wif_manual[i] = !on;
        wif_nets_save();
    }
    ESP_LOGI(TAG, "%s: %s", ssid, on ? "AUTO JOIN" : "joins only by hand");
    return true;
}

int wifi_saved_count(void)
{
    return wif_nets_load() ? wif_nnet : 0;
}

bool wifi_saved_pass(const char *ssid, char *pass, int len)
{
    int i = wif_nets_load() ? wif_nets_find(ssid) : -1;
    if (i < 0) {
        return false;
    }
    snprintf(pass, len, "%s", wif_nets[i].pass);
    return true;
}

const char *wifi_saved_name(int i)
{
    return wif_nets_load() && i >= 0 && i < wif_nnet ? wif_nets[i].ssid : "";
}
