// The simulator: the I2S channels are only names; the sound goes through the codec device (sim_audio.c).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

typedef struct sim_i2s_chan *i2s_chan_handle_t;
typedef enum { I2S_NUM_0, I2S_NUM_1, I2S_NUM_AUTO } i2s_port_t;
typedef enum { I2S_ROLE_MASTER, I2S_ROLE_SLAVE } i2s_role_t;
typedef enum { I2S_DATA_BIT_WIDTH_8BIT = 8, I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_DATA_BIT_WIDTH_24BIT = 24,
               I2S_DATA_BIT_WIDTH_32BIT = 32 } i2s_data_bit_width_t;
typedef enum { I2S_SLOT_MODE_MONO = 1, I2S_SLOT_MODE_STEREO = 2 } i2s_slot_mode_t;
typedef struct {
    i2s_port_t id;
    i2s_role_t role;
    uint32_t dma_desc_num;
    uint32_t dma_frame_num;
    bool auto_clear;
    bool auto_clear_before_cb;
    int intr_priority;
} i2s_chan_config_t;
typedef struct { uint32_t sample_rate_hz; int clk_src; int mclk_multiple; } i2s_std_clk_config_t;
typedef struct { i2s_data_bit_width_t data_bit_width; i2s_slot_mode_t slot_mode; } i2s_std_slot_config_t;
typedef struct {
    gpio_num_t mclk, bclk, ws, dout, din;
    struct { uint32_t mclk_inv : 1, bclk_inv : 1, ws_inv : 1; } invert_flags;
} i2s_std_gpio_config_t;
typedef struct {
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;

#define I2S_CHANNEL_DEFAULT_CONFIG(port, r) {.id = (port), .role = (r), .dma_desc_num = 6, .dma_frame_num = 240}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) {.sample_rate_hz = (rate), .clk_src = 0, .mclk_multiple = 256}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) {.data_bit_width = (bits), .slot_mode = (mode)}
#define I2S_STD_MSB_SLOT_DEFAULT_CONFIG(bits, mode) {.data_bit_width = (bits), .slot_mode = (mode)}

static inline esp_err_t i2s_new_channel(const i2s_chan_config_t *c, i2s_chan_handle_t *tx, i2s_chan_handle_t *rx)
{
    (void)c;
    if (tx) *tx = (i2s_chan_handle_t)(intptr_t)1;
    if (rx) *rx = (i2s_chan_handle_t)(intptr_t)2;
    return ESP_OK;
}
static inline esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h, const i2s_std_config_t *c) { (void)h; (void)c; return ESP_OK; }
static inline esp_err_t i2s_channel_enable(i2s_chan_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t i2s_channel_disable(i2s_chan_handle_t h) { (void)h; return ESP_OK; }
static inline esp_err_t i2s_del_channel(i2s_chan_handle_t h) { (void)h; return ESP_OK; }
// the events of a channel (audio.c counts the underruns of the speaker; the simulator has none)
typedef struct { void *dma_buf; size_t size; } i2s_event_data_t;
typedef bool (*i2s_isr_callback_t)(i2s_chan_handle_t h, i2s_event_data_t *e, void *u);
typedef struct { i2s_isr_callback_t on_recv, on_recv_q_ovf, on_sent, on_send_q_ovf; } i2s_event_callbacks_t;
static inline esp_err_t i2s_channel_register_event_callback(i2s_chan_handle_t h, const i2s_event_callbacks_t *c, void *u)
{
    (void)h; (void)c; (void)u;
    return ESP_OK;
}
