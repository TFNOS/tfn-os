// The fake board of the link test (test_pool_link.c): the parts of ESP-IDF, FreeRTOS, lwIP and the OS that link.c,
// lan.c, friends.c and walkie_call.c use. The test sets these values and reads what the code sent.
#pragma once
#include <stdbool.h>
#include <stdint.h>

extern int64_t fk_now_us;        // esp_timer_get_time()
extern uint8_t fk_mac[6];        // the MAC of this board (arc_mac, link_my_mac)
extern int fk_wifi;              // wifi_state(): 0 = off, 1 = connecting, 2 = connected, 3 = idle, 4 = failed
extern int fk_channel;           // esp_wifi_get_channel()
extern int fk_get_ch_n;          // the calls of esp_wifi_get_channel()
extern uint32_t fk_hub;          // vpn_hub_ip() (network byte order; 0 = none)
extern const char *fk_app_name;  // the name of app_current() (NULL = the menu)

// each frame that the code gives to esp_now_send, and each UDP datagram that it gives to sendto
typedef void (*fk_out_fn)(int kind, uint32_t ip, const uint8_t *p, int n);  // kind: 'E' ESP-NOW, 'U' UDP
extern fk_out_fn fk_out;
// the UDP datagrams that recvfrom gives to the lan task (fk_lan_run: the task runs until the list is empty)
void fk_lan_push(uint32_t ip, const uint8_t *p, int n);
void fk_lan_run(void);
// THE SEND PATH OF ESP-NOW: fk_now_auto 1 (the default) = the send callback of each frame comes at the next
// esp_now_send; 0 = the callbacks come only with fk_now_done(n). fk_now_lim = the frames in flight that ESP-NOW takes
// (more: ESP_ERR_ESPNOW_NO_MEM). fk_now_stuck: 0 = no fault, 1 = each send refused until a restart of the radio, 2 =
// until a restart of the whole driver (wifi_radio_restart full), 3 = always. fk_now_inflight = the frames with no
// callback; fk_restarts / fk_full_restarts = the calls of wifi_radio_restart.
extern int fk_now_auto, fk_now_lim, fk_now_stuck, fk_now_inflight, fk_restarts, fk_full_restarts;
void fk_now_done(int n);
// the ESP-NOW receive function that link.c registered (NULL: none)
void fk_espnow_in(const uint8_t mac[6], const uint8_t *p, int n, int rssi);
// the log of the OS calls (sysui, app_open, notify, sfx): one text line each
typedef void (*fk_log_fn)(const char *line);
extern fk_log_fn fk_log;
