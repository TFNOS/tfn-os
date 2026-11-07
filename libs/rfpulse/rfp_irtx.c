// RFPULSE: THE INFRARED LED (rfpulse.h: rfp_ir_send). The RMT peripheral sends the level times on the TX pin of
// rfp_board_ir() with the carrier (the hardware makes the carrier: the times are exact with WiFi and the screen on).
// The channel lives only for one send: no RMT memory and no interrupt while the app does not send. After the send the
// pin stays an output at LOW (an IR LED module with a transistor: the LED stays off).
#include "rfpulse.h"
#include "rfp_board.h"

#ifdef ESP_PLATFORM
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_heap_caps.h"

#define TICK_MAX 32767  // the longest half of an RMT symbol (15 bits, 1 tick = 1 us)

static void set_half(rmt_symbol_word_t *s, int h, uint32_t dur, int lvl)
{
    rmt_symbol_word_t *w = &s[h >> 1];
    if (h & 1) {
        w->duration1 = dur;
        w->level1 = (uint32_t)lvl;
    } else {
        w->duration0 = dur;
        w->level0 = (uint32_t)lvl;
    }
}

static void led_off(int pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

int rfp_ir_send(const int32_t *d, int n, uint32_t carrier_hz)
{
    const rfp_conn_t *k = rfp_board_ir();
    if (k->tx_gpio < 0) {
        return -2;
    }
    if (!d || n <= 0 || n > 4000) {
        return -3;
    }
    // the halves of the symbols: a time longer than TICK_MAX takes more halves of the same level
    int halves = 0;
    for (int i = 0; i < n; i++) {
        const int32_t v = d[i] < 0 ? -d[i] : d[i];
        halves += v ? (v + TICK_MAX - 1) / TICK_MAX : 0;
    }
    if (!halves) {
        return -3;
    }
    const int nsym = (halves + 1) / 2;
    rmt_symbol_word_t *sym = heap_caps_calloc((size_t)nsym, sizeof(*sym), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!sym) {
        return -4;
    }
    int h = 0;
    for (int i = 0; i < n; i++) {
        int32_t rem = d[i] < 0 ? -d[i] : d[i];
        while (rem > 0) {
            const int32_t part = rem > TICK_MAX ? TICK_MAX : rem;
            set_half(sym, h++, (uint32_t)part, d[i] > 0);
            rem -= part;
        }
    }
    if (h & 1) {
        set_half(sym, h, 10, 0);  // (a duration of 0 ends the send: a short space fills the last symbol)
    }
    rmt_channel_handle_t ch = NULL;
    rmt_encoder_handle_t enc = NULL;
    rmt_tx_channel_config_t cfg = {
            .gpio_num = k->tx_gpio,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,
            .mem_block_symbols = 192,  // (4 blocks: a frame of up to 96 bits with no refill)
            .trans_queue_depth = 1,
    };
    int err = 0;
    if (rmt_new_tx_channel(&cfg, &ch) != ESP_OK) {
        cfg.mem_block_symbols = 48;  // (an other user of the RMT has blocks: one block, with refills)
        if (rmt_new_tx_channel(&cfg, &ch) != ESP_OK) {
            free(sym);
            return -5;
        }
    }
    rmt_carrier_config_t cc = {.frequency_hz = carrier_hz ? carrier_hz : 38000, .duty_cycle = 0.33f};
    rmt_copy_encoder_config_t ec;
    memset(&ec, 0, sizeof(ec));  // (an empty struct in ESP-IDF 6.1: no {0})
    if (rmt_apply_carrier(ch, &cc) != ESP_OK || rmt_new_copy_encoder(&ec, &enc) != ESP_OK || rmt_enable(ch) != ESP_OK) {
        err = -6;
    } else {
        gpio_set_drive_capability(k->tx_gpio, GPIO_DRIVE_CAP_3);
        rmt_transmit_config_t tc = {.loop_count = 0};
        if (rmt_transmit(ch, enc, sym, sizeof(*sym) * (size_t)nsym, &tc) != ESP_OK) {
            err = -7;
        } else if (rmt_tx_wait_all_done(ch, 3000) != ESP_OK) {
            err = -8;
        }
        rmt_disable(ch);
    }
    if (enc) {
        rmt_del_encoder(enc);
    }
    rmt_del_channel(ch);
    free(sym);
    led_off(k->tx_gpio);
    return err;
}

#else  // the PC: no LED

int rfp_ir_send(const int32_t *d, int n, uint32_t carrier_hz)
{
    (void)d;
    (void)n;
    (void)carrier_hz;
    return -2;
}

#endif

const char *rfp_ir_send_error(int e)
{
    switch (e) {
    case 0:
        return "sent";
    case -2:
        return "this board has no pin for an IR LED";
    case -3:
        return "no level times";
    case -4:
        return "no memory";
    case -5:
        return "no free RMT channel";
    case -6:
        return "the RMT channel did not start";
    case -7:
        return "the RMT did not send";
    case -8:
        return "the RMT did not stop in time";
    default:
        return "?";
    }
}
