// TFN OS SDK, the PC side of the raw ESP-IDF names (sdk/docs/raw.md): a short copy of the ESP-IDF 6.1 WiFi types and
// functions of the extra ABI names (main/tfn_abi_extra.json). mk_app.py compiles an app on the PC only to draw its
// menu icon: this header and raw_stubs.c let an app with raw names compile there. The board uses the real header.
// Only the fields of the types that the examples read are here; add a field (with its real name) when you need it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifndef ESP_OK
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif
#define ESP_ERR_WIFI_BASE 0x3000
#define ESP_ERR_WIFI_NOT_STARTED (ESP_ERR_WIFI_BASE + 3)

typedef enum {
    WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_ENTERPRISE, WIFI_AUTH_WPA2_ENTERPRISE = WIFI_AUTH_ENTERPRISE, WIFI_AUTH_WPA3_PSK, WIFI_AUTH_WPA2_WPA3_PSK,
} wifi_auth_mode_t;
typedef enum { WIFI_SCAN_TYPE_ACTIVE = 0, WIFI_SCAN_TYPE_PASSIVE } wifi_scan_type_t;
typedef enum { WIFI_SECOND_CHAN_NONE = 0, WIFI_SECOND_CHAN_ABOVE, WIFI_SECOND_CHAN_BELOW } wifi_second_chan_t;
typedef struct { uint32_t min, max; } wifi_active_scan_time_t;
typedef struct { wifi_active_scan_time_t active; uint32_t passive; } wifi_scan_time_t;
typedef struct {
    uint8_t *ssid;
    uint8_t *bssid;
    uint8_t channel;
    bool show_hidden;
    wifi_scan_type_t scan_type;
    wifi_scan_time_t scan_time;
} wifi_scan_config_t;
typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second);
