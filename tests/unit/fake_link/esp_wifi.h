// A small copy of the ESP-IDF WiFi API for the link test
#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    WIFI_PS_NONE,
    WIFI_PS_MIN_MODEM,
    WIFI_PS_MAX_MODEM,
} wifi_ps_type_t;
typedef enum {
    WIFI_MODE_NULL,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
} wifi_mode_t;
typedef enum {
    WIFI_IF_STA,
    WIFI_IF_AP,
} wifi_interface_t;
typedef enum {
    WIFI_SECOND_CHAN_NONE,
} wifi_second_chan_t;
typedef struct {
    int8_t rssi;
} wifi_ap_record_t;

esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *ps);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t ps);
esp_err_t esp_wifi_set_channel(uint8_t ch, wifi_second_chan_t second);
esp_err_t esp_wifi_get_channel(uint8_t *ch, wifi_second_chan_t *second);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap);
