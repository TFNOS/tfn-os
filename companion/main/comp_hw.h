#pragma once
// THE COMPANION ON THE CHIP (comp_hw.c): the functions that comp_core.c gets in cc_io_t, and the start.
#include <stdbool.h>
#include <stdint.h>
#include "comp_core.h"

typedef struct {
    uint8_t mac[6];
    uint8_t len;
    uint8_t d[250];
} hw_rx_t;

void hw_nvs_init(void);
void hw_radio_start(void);
bool hw_rx_pop(hw_rx_t *r, int wait_ms);
void hw_send(const uint8_t *mac, const uint8_t *d, int n);
void hw_set_channel(int ch);
void hw_my_mac(uint8_t mac[6]);
uint32_t hw_now_ms(void);
void hw_random(uint8_t *p, int n);
void hw_save(const cc_saved_t *s);
bool hw_load(cc_saved_t *s);
bool hw_rx_open(int pin, bool inv, bool ir);
void hw_rx_close(int pin);
int hw_rx_read(int pin, int32_t *d, int max, int32_t *idle_us, bool *active);
int hw_rx_level(int pin);
// the state of each receiver in the console (rxstat): the edges, the RMT frames, the restarts of a stuck channel
void hw_rx_diag(void);
int hw_tx_send(int pin, uint32_t carrier_hz, bool inv, const int32_t *d, int n);
void hw_tx_idle(int pin, bool inv);
// the switch and analog modules
void hw_out_set(int pin, bool level);
void hw_in_open(int pin, bool pullup);
int hw_adc_read(int pin);  // mV (-1: no ADC on this pin)
void hw_pin_free(int pin);
// the update by radio (esp_ota on the other app slot)
int hw_ota_begin(uint32_t size);
int hw_ota_write(const uint8_t *d, int n);
int hw_ota_end(void);
void hw_ota_abort(void);
void hw_ota_valid(void);
void hw_restart(void);
void hw_led_pin(int pin);
void hw_led(bool on);
int hw_boot_pin(void);
void hw_boot_init(void);
bool hw_boot_down(void);
void hw_chip(tc_info_t *in);
