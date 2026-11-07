#pragma once

#include <stdbool.h>
#include <stdint.h>

// WiFi station, started on demand. When it is connected, a small web server
// lists the files in /data (exports, samples) for download in a browser.

typedef enum {
    WIFI_OFF,
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED
} wifi_state_t;

wifi_state_t wifi_state(void);
const char *wifi_ip(void);  // "" if not connected

const char *wifi_hotspot_ssid(void);  // for example "TFN-2F95"

// The address of the web page for the screens: the WiFi address, else the hotspot
// address, else NULL (no web page now).
const char *wifi_web_ip(void);
