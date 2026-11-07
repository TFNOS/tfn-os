// The link test: a small copy of the WiFi API of the OS (main/wifi.h)
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WIFI_OFF,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_IDLE,
    WIFI_FAILED,
} wifi_state_t;
wifi_state_t wifi_state(void);
bool wifi_start(void);
void wifi_radio_restart(int ch, bool full);
bool wifi_hotspot_on(void);
bool wifi_scanning(void);
int wifi_hotspot_clients(void);
void wifi_ps_apply(void);
