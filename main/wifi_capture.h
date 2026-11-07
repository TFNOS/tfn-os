#pragma once
#include <stdint.h>
#include <stddef.h>

// Installed-app capture ABI. All functions return ESP_OK (0) or an esp_err_t.
// One caller/session at a time. begin/end/deauth must be serialized by the app.
typedef struct {
    uint8_t bssid[6];
    uint8_t channel;
    uint8_t reserved;
} wifi_capture_config_t;

// Frame bytes exclude FCS; valid only during callback. Callback runs in the Wi-Fi
// task: copy to a bounded queue, never block/allocate/write files here.
typedef void (*wifi_capture_rx_cb_t)(void *context, const uint8_t *frame, size_t length, int rssi, int64_t time_us);
int32_t wifi_capture_begin(const wifi_capture_config_t *config, wifi_capture_rx_cb_t callback, void *context);
// Idempotent. Drains callbacks before returning; restores normal Wi-Fi intent.
int32_t wifi_capture_end(void);
// One request, one selected unicast client; no broadcast. Minimum 1 s spacing.
// Returns local TX result only, never a guarantee of over-air delivery/reconnect.
int32_t wifi_capture_deauth(const uint8_t client[6]);
