// The MAC of the board: the identity that the manager, the friends, the store and the LAN use.
// The ESP32-S3: the WiFi station MAC. The ESP32-P4 of the Tab5 has no WiFi of its own (the radio is the C6
// through esp_hosted, and esp_read_mac(ESP_MAC_WIFI_STA) fails there): its base MAC of the eFuse (the MAC of its USB).
#pragma once
#ifndef ESP_PLATFORM
// (the host tests: the stub of esp_read_mac, as the code called it before)
#define arc_mac(m)    esp_read_mac((m), ESP_MAC_WIFI_STA)
#define arc_mac_ap(m) esp_read_mac((m), ESP_MAC_WIFI_SOFTAP)
#else
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_mac.h"

static inline void arc_mac(uint8_t mac[6])
{
#if CONFIG_IDF_TARGET_ESP32P4
    esp_efuse_mac_get_default(mac);
#else
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
}
// the MAC of the hotspot (its name ARCADE-XXXX): the S3 SoftAP MAC; the P4: the base MAC
static inline void arc_mac_ap(uint8_t mac[6])
{
#if CONFIG_IDF_TARGET_ESP32P4
    esp_efuse_mac_get_default(mac);
#else
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
#endif
}
#endif
