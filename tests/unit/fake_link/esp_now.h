// A small copy of the ESP-NOW API for the link test
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi.h"

#define ESP_ERR_ESPNOW_NO_MEM 0x3067

typedef struct {
    int rssi;
} wifi_pkt_rx_ctrl_t;
typedef struct {
    uint8_t *src_addr;
    uint8_t *des_addr;
    wifi_pkt_rx_ctrl_t *rx_ctrl;
} esp_now_recv_info_t;
typedef struct {
    uint8_t peer_addr[6];
    uint8_t channel;
    wifi_interface_t ifidx;
    bool encrypt;
} esp_now_peer_info_t;
typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t *info, const uint8_t *data, int len);
typedef struct {
    int tx_status;
} esp_now_send_info_t;
typedef enum {
    ESP_NOW_SEND_SUCCESS = 0,
    ESP_NOW_SEND_FAIL,
} esp_now_send_status_t;
typedef void (*esp_now_send_cb_t)(const esp_now_send_info_t *info, esp_now_send_status_t status);

esp_err_t esp_now_init(void);
esp_err_t esp_now_deinit(void);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
esp_err_t esp_now_unregister_recv_cb(void);
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer);
bool esp_now_is_peer_exist(const uint8_t *mac);
esp_err_t esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len);
const char *esp_err_to_name(esp_err_t e);
