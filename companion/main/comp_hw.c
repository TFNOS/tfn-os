// THE COMPANION ON THE CHIP: the radio (ESP-NOW), the receiver pins (an interrupt at each edge), the transmitter pins
// (the RMT with the carrier), the LED, the BOOT key and the NVS. comp_core.c has the logic.
#include "comp_hw.h"
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal/gpio_ll.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "comp";

// ---------------------------------------------------------------- the radio
static QueueHandle_t s_rxq;
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || len < 2 || len > 250 || data[0] != 'C' || data[1] != 'P') {
        return;
    }
    hw_rx_t r;
    memcpy(r.mac, info->src_addr, 6);
    r.len = (uint8_t)len;
    memcpy(r.d, data, (size_t)len);
    xQueueSend(s_rxq, &r, 0);
}

static void add_peer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) {
        return;
    }
    esp_now_peer_info_t p;
    memset(&p, 0, sizeof p);
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;  // (the channel now)
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    if (esp_now_add_peer(&p) != ESP_OK) {
        // (the peer list is full: the oldest unicast peer goes)
        esp_now_peer_info_t q;
        if (esp_now_fetch_peer(true, &q) == ESP_OK) {
            do {
                if (memcmp(q.peer_addr, BCAST, 6)) {
                    esp_now_del_peer(q.peer_addr);
                    break;
                }
            } while (esp_now_fetch_peer(false, &q) == ESP_OK);
        }
        esp_now_add_peer(&p);
    }
}

void hw_send(const uint8_t *mac, const uint8_t *d, int n)
{
    if (!mac) {
        mac = BCAST;
    }
    add_peer(mac);
    esp_err_t e = esp_now_send(mac, d, (size_t)n);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send: %s", esp_err_to_name(e));
    }
}

void hw_set_channel(int ch)
{
    esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
}

bool hw_rx_pop(hw_rx_t *r, int wait_ms)
{
    return xQueueReceive(s_rxq, r, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void hw_radio_start(void)
{
    s_rxq = xQueueCreate(16, sizeof(hw_rx_t));
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));
    add_peer(BCAST);
}

void hw_my_mac(uint8_t mac[6])
{
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

// ---------------------------------------------------------------- the clock, the random numbers
uint32_t hw_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void hw_random(uint8_t *p, int n)
{
    esp_fill_random(p, (size_t)n);  // (the radio is on: true random numbers)
}

// ---------------------------------------------------------------- the NVS
void hw_save(const cc_saved_t *s)
{
    nvs_handle_t h;
    if (nvs_open("comp", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_blob(h, "state", s, sizeof *s);
    nvs_commit(h);
    nvs_close(h);
}

bool hw_load(cc_saved_t *s)
{
    nvs_handle_t h;
    memset(s, 0, sizeof *s);
    if (nvs_open("comp", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = sizeof *s;
    const bool ok = nvs_get_blob(h, "state", s, &n) == ESP_OK && n == sizeof *s;
    nvs_close(h);
    if (!ok) {
        memset(s, 0, sizeof *s);
    }
    return ok;
}

void hw_nvs_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

// ---------------------------------------------------------------- the receiver pins
// AN IR RECEIVER: the RMT records the level times in hardware (the WiFi can delay an interrupt by about 200 us: one late
// edge turned an NEC frame into UNKNOWN). Each frame that ends (RX_IDLE_US with no edge) goes into the ring, then a
// space of RX_IDLE_US (the end of the frame for the reader). A RF receiver (its noise has no end) and a chip with no
// free RMT channel: an interrupt at each edge.
#define RING       1024  // a power of 2
#define NRX        4
#define RX_IDLE_US 12000
#if SOC_RMT_SUPPORT_DMA
#define RX_MEM 1024  // (the DMA buffer)
#elif CONFIG_IDF_TARGET_ESP32
#define RX_MEM 384  // (6 of the 8 blocks: 768 level times; a transmitter takes a block of the last channels)
#else
#define RX_MEM (SOC_RMT_MEM_WORDS_PER_CHANNEL * 2)
#endif
#define RX_SYM 512  // the symbols of one frame (1024 level times)
typedef struct {
    int pin;  // -1: free
    bool inv;
    int32_t *ring;
    volatile uint32_t head, tail, lost;
    volatile int64_t last_t;
    volatile int last_lvl;
    // the RMT (an IR receiver)
    rmt_channel_handle_t rmt;
    rmt_symbol_word_t *sym;
    volatile bool done;  // a frame is in the ring: the next receive waits for the reader
    int32_t pend;        // the level time that the next half of the same level joins
    // the guard of the RMT channel: the GPIO interrupt of the same pin records each edge
    volatile int64_t edge_t;
    volatile uint32_t edges, frames;
    int64_t armed_t;
    uint32_t restarts;
} rx_t;
static rx_t s_rx[NRX] = {{.pin = -1}, {.pin = -1}, {.pin = -1}, {.pin = -1}};
static bool s_isr_on;

static void IRAM_ATTR edge_isr(void *arg)
{
    rx_t *c = arg;
    const int64_t t = esp_timer_get_time();
    const int lvl = gpio_ll_get_level(GPIO_LL_GET_HW(GPIO_PORT_0), (uint32_t)c->pin);
    if (lvl == c->last_lvl) {
        return;
    }
    int64_t d = t - c->last_t;
    if (d > 0x3FFFFFFF) {
        d = 0x3FFFFFFF;
    }
    const uint32_t h = c->head;
    if (h - c->tail < RING) {
        // the level that ended: a mark when it was the active level
        const bool mark = (c->last_lvl == 0) == c->inv;
        c->ring[h & (RING - 1)] = mark ? (int32_t)d : -(int32_t)d;
        c->head = h + 1;
    } else {
        c->lost++;
    }
    c->last_t = t;
    c->last_lvl = lvl;
}

// the edges of a pin with an RMT receive channel: only the time and the count (the RMT records the level times)
static void IRAM_ATTR kick_isr(void *arg)
{
    rx_t *c = arg;
    c->edge_t = esp_timer_get_time();
    c->edges++;
}

static void IRAM_ATTR ring_put(rx_t *c, int32_t v)
{
    const uint32_t h = c->head;
    if (h - c->tail < RING) {
        c->ring[h & (RING - 1)] = v;
        c->head = h + 1;
    } else {
        c->lost++;
    }
}

static void IRAM_ATTR half_put(rx_t *c, uint32_t dur, uint32_t lvl)
{
    if (!dur) {
        return;
    }
    const bool mark = (lvl == 0) == c->inv;
    const int32_t v = mark ? (int32_t)dur : -(int32_t)dur;
    if (c->pend && (c->pend > 0) == mark) {
        c->pend += v;
        return;
    }
    if (c->pend) {
        ring_put(c, c->pend);
    }
    c->pend = v;
}

// the end of a frame of the RMT (the ISR of the RMT)
static bool IRAM_ATTR rmt_done(rmt_channel_handle_t ch, const rmt_rx_done_event_data_t *e, void *arg)
{
    (void)ch;
    rx_t *c = arg;
    c->pend = 0;
    for (size_t i = 0; i < e->num_symbols; i++) {
        half_put(c, e->received_symbols[i].duration0, e->received_symbols[i].level0);
        half_put(c, e->received_symbols[i].duration1, e->received_symbols[i].level1);
    }
    if (c->pend > 0) {
        ring_put(c, c->pend);  // (the last mark; the space after it is the idle time)
    }
    c->pend = 0;
    ring_put(c, -RX_IDLE_US);
    c->last_t = esp_timer_get_time();
    c->frames++;
    c->done = true;
    return false;
}

static const rmt_receive_config_t RX_CFG = {
        .signal_range_min_ns = 1000,                 // (a glitch filter: a shorter pulse is no edge)
        .signal_range_max_ns = RX_IDLE_US * 1000UL,  // (no edge for this time: the frame ends)
};

static void rmt_arm(rx_t *c)
{
    c->done = false;
    c->armed_t = esp_timer_get_time();
    if (rmt_receive(c->rmt, c->sym, RX_SYM * sizeof(rmt_symbol_word_t), &RX_CFG) != ESP_OK) {
        c->done = true;  // (the next read tries again)
    }
}

// an IR receiver on the RMT: false = no free channel (then the interrupt path)
static bool rmt_open(rx_t *c, int pin)
{
    if (!c->sym) {
        c->sym = heap_caps_malloc(RX_SYM * sizeof(rmt_symbol_word_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!c->sym) {
            return false;
        }
    }
    rmt_rx_channel_config_t cfg = {
            .gpio_num = pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,
            .mem_block_symbols = RX_MEM,
#if SOC_RMT_SUPPORT_DMA
            .flags.with_dma = true,
#endif
    };
    if (rmt_new_rx_channel(&cfg, &c->rmt) != ESP_OK) {
        c->rmt = NULL;
        return false;
    }
    rmt_rx_event_callbacks_t cbs = {.on_recv_done = rmt_done};
    if (rmt_rx_register_event_callbacks(c->rmt, &cbs, c) != ESP_OK || rmt_enable(c->rmt) != ESP_OK) {
        rmt_del_channel(c->rmt);
        c->rmt = NULL;
        return false;
    }
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    c->edge_t = 0;
    c->edges = c->frames = c->restarts = 0;
    gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(pin, kick_isr, c);
    gpio_intr_enable(pin);
    rmt_arm(c);
    return true;
}

// a stuck channel: edges after the arm, 100 ms with no edge (a frame ends after RX_IDLE_US) and no done
static void rmt_guard(rx_t *c)
{
    const int64_t t = esp_timer_get_time();
    const int64_t e = c->edge_t;
    if (c->done || e <= c->armed_t || t - e < 100000) {
        return;
    }
    rmt_disable(c->rmt);
    rmt_enable(c->rmt);
    c->restarts++;
    ESP_LOGW(TAG, "IR receiver on GPIO %d: the RMT channel was stuck (no frame end): it starts again (%lu)", c->pin,
             (unsigned long)c->restarts);
    rmt_arm(c);
}

static rx_t *rx_of(int pin)
{
    for (int i = 0; i < NRX; i++) {
        if (s_rx[i].pin == pin) {
            return &s_rx[i];
        }
    }
    return NULL;
}

bool hw_rx_open(int pin, bool inv, bool ir)
{
    if (rx_of(pin)) {
        return true;
    }
    rx_t *c = rx_of(-1);
    if (!c) {
        return false;
    }
    if (!c->ring) {
        c->ring = heap_caps_malloc(RING * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!c->ring) {
            return false;
        }
    }
    if (!s_isr_on) {
        gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        s_isr_on = true;
    }
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    // the pull to the quiet level: an IR receiver idles HIGH (an open-collector output), a RF receiver idles LOW
    gpio_set_pull_mode(pin, inv ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
    c->inv = inv;
    c->head = c->tail = c->lost = 0;
    c->last_t = esp_timer_get_time();
    c->last_lvl = gpio_get_level(pin);
    c->pin = pin;
    if (ir && rmt_open(c, pin)) {
        ESP_LOGI(TAG, "IR receiver on GPIO %d (the RMT, %s active)", pin, inv ? "LOW" : "HIGH");
        return true;
    }
    gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(pin, edge_isr, c);
    gpio_intr_enable(pin);
    ESP_LOGI(TAG, "receiver on GPIO %d (%s active)", pin, inv ? "LOW" : "HIGH");
    return true;
}

void hw_rx_close(int pin)
{
    rx_t *c = rx_of(pin);
    if (!c) {
        return;
    }
    gpio_intr_disable(pin);
    gpio_isr_handler_remove(pin);
    if (c->rmt) {
        rmt_disable(c->rmt);
        rmt_del_channel(c->rmt);
        c->rmt = NULL;
    }
    gpio_reset_pin(pin);
    c->pin = -1;
}

int hw_rx_read(int pin, int32_t *d, int max, int32_t *idle_us, bool *active)
{
    rx_t *c = rx_of(pin);
    if (!c) {
        *idle_us = 0;
        *active = false;
        return 0;
    }
    int n = 0;
    while (n < max && c->tail != c->head) {
        d[n++] = c->ring[c->tail & (RING - 1)];
        c->tail++;
    }
    const int64_t idle = esp_timer_get_time() - c->last_t;
    *idle_us = idle > 0x3FFFFFFF ? 0x3FFFFFFF : (int32_t)idle;
    *active = c->rmt ? false : (c->last_lvl == 0) == c->inv;
    if (c->rmt && c->done && c->tail == c->head) {
        rmt_arm(c);  // (the frame is read: the RMT waits for the next one)
    } else if (c->rmt) {
        rmt_guard(c);
    }
    return n;
}

void hw_rx_diag(void)
{
    const int64_t t = esp_timer_get_time();
    for (int i = 0; i < NRX; i++) {
        const rx_t *c = &s_rx[i];
        if (c->pin < 0) {
            continue;
        }
        printf("RX GPIO %d %s level %d edges %lu frames %lu restarts %lu done %d armed %lld ms ago, last edge %lld ms "
               "ago, ring %lu lost %lu\n",
               c->pin, c->rmt ? "RMT" : "ISR", gpio_get_level(c->pin), (unsigned long)c->edges,
               (unsigned long)c->frames, (unsigned long)c->restarts, c->done, (long long)((t - c->armed_t) / 1000),
               c->edge_t ? (long long)((t - c->edge_t) / 1000) : -1LL, (unsigned long)(c->head - c->tail),
               (unsigned long)c->lost);
    }
}

int hw_rx_level(int pin)
{
    return gpio_get_level(pin);
}

// ---------------------------------------------------------------- the transmitter pins (rfp_irtx.c, with a pin)
#define TICK_MAX 32767

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

void hw_tx_idle(int pin, bool inv)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, inv ? 1 : 0);
}

int hw_tx_send(int pin, uint32_t carrier_hz, bool inv, const int32_t *d, int n)
{
    if (!d || n <= 0 || n > 4000) {
        return -3;
    }
    int halves = 0;
    for (int i = 0; i < n; i++) {
        const int32_t v = d[i] < 0 ? -d[i] : d[i];
        halves += v ? (v + TICK_MAX - 1) / TICK_MAX : 0;
    }
    if (!halves) {
        return -3;
    }
    const int nsym = (halves + 2) / 2;
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
            .gpio_num = pin,
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 1000000,
            .mem_block_symbols = 192,
            .trans_queue_depth = 1,
            .flags.invert_out = inv,
    };
    int err = 0;
    if (rmt_new_tx_channel(&cfg, &ch) != ESP_OK) {
        cfg.mem_block_symbols = 64;
        if (rmt_new_tx_channel(&cfg, &ch) != ESP_OK) {
            free(sym);
            return -5;
        }
    }
    rmt_copy_encoder_config_t ec;
    memset(&ec, 0, sizeof(ec));
    bool ok = rmt_new_copy_encoder(&ec, &enc) == ESP_OK;
    if (ok && carrier_hz) {
        rmt_carrier_config_t cc = {.frequency_hz = carrier_hz, .duty_cycle = 0.33f};
        ok = rmt_apply_carrier(ch, &cc) == ESP_OK;
    }
    if (!ok || rmt_enable(ch) != ESP_OK) {
        err = -6;
    } else {
        gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_3);
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
    hw_tx_idle(pin, inv);
    return err;
}

// ---------------------------------------------------------------- the LED and the BOOT key
static int s_led = -1;

// ---------------------------------------------------------------- the update by radio
static esp_ota_handle_t s_ota_h;
static const esp_partition_t *s_ota_part;

int hw_ota_begin(uint32_t size)
{
    hw_ota_abort();
    s_ota_part = esp_ota_get_next_update_partition(NULL);
    if (!s_ota_part || size > s_ota_part->size) {
        ESP_LOGW(TAG, "update: no slot for %lu bytes", (unsigned long)size);
        s_ota_part = NULL;
        return -1;
    }
    // (the sequential writes erase each sector when the data comes: no long erase before the first ACK)
    const esp_err_t e = esp_ota_begin(s_ota_part, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_h);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "update: esp_ota_begin %s", esp_err_to_name(e));
        s_ota_part = NULL;
        return -1;
    }
    return 0;
}

int hw_ota_write(const uint8_t *d, int n)
{
    return s_ota_part && esp_ota_write(s_ota_h, d, (size_t)n) == ESP_OK ? 0 : -1;
}

int hw_ota_end(void)
{
    if (!s_ota_part) {
        return -1;
    }
    const esp_partition_t *p = s_ota_part;
    s_ota_part = NULL;
    esp_err_t e = esp_ota_end(s_ota_h);  // (it checks the image: the chip, the segments, the hash)
    if (e == ESP_OK) {
        e = esp_ota_set_boot_partition(p);
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "update: %s", esp_err_to_name(e));
        return -1;
    }
    return 0;
}

void hw_ota_abort(void)
{
    if (s_ota_part) {
        esp_ota_abort(s_ota_h);
        s_ota_part = NULL;
    }
}

void hw_ota_valid(void)
{
    esp_ota_img_states_t st;
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "update: the new firmware works (it reached its master)");
    }
}

void hw_restart(void)
{
    esp_restart();
}

// ---------------------------------------------------------------- the switch and analog modules
void hw_out_set(int pin, bool level)
{
    gpio_set_level(pin, level);
}

void hw_in_open(int pin, bool pullup)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, pullup ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
}

void hw_pin_free(int pin)
{
    gpio_reset_pin(pin);
}

// the ADC: one unit (ADC1), its calibration (the curve or the line of the chip; none: the raw scale)
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static bool s_adc_tried;

int hw_adc_read(int pin)
{
    adc_unit_t unit;
    adc_channel_t ch;
    if (adc_oneshot_io_to_channel(pin, &unit, &ch) != ESP_OK || unit != ADC_UNIT_1) {
        return -1;
    }
    if (!s_adc_tried) {
        s_adc_tried = true;
        const adc_oneshot_unit_init_cfg_t uc = {.unit_id = ADC_UNIT_1};
        if (adc_oneshot_new_unit(&uc, &s_adc) != ESP_OK) {
            s_adc = NULL;
        }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        const adc_cali_curve_fitting_config_t cc = {
                .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
        if (adc_cali_create_scheme_curve_fitting(&cc, &s_cali) != ESP_OK) {
            s_cali = NULL;
        }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        const adc_cali_line_fitting_config_t lc = {
                .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
        if (adc_cali_create_scheme_line_fitting(&lc, &s_cali) != ESP_OK) {
            s_cali = NULL;
        }
#endif
    }
    if (!s_adc) {
        return -1;
    }
    static uint64_t s_cfg;  // the channels with their config
    if (!(s_cfg & (1ull << ch))) {
        const adc_oneshot_chan_cfg_t cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT};
        if (adc_oneshot_config_channel(s_adc, ch, &cfg) != ESP_OK) {
            return -1;
        }
        s_cfg |= 1ull << ch;
    }
    int raw = 0;
    if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) {
        return -1;
    }
    int mv = 0;
    if (s_cali && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
        return mv;
    }
    return raw * 3300 / 4095;
}

void hw_led_pin(int pin)
{
    s_led = pin;
    if (pin >= 0) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        gpio_set_level(pin, 0);
    }
}

void hw_led(bool on)
{
    if (s_led >= 0) {
        gpio_set_level(s_led, on);
    }
}

int hw_boot_pin(void)
{
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
    return 9;
#else
    return 0;
#endif
}

void hw_boot_init(void)
{
    const int p = hw_boot_pin();
    gpio_reset_pin(p);
    gpio_set_direction(p, GPIO_MODE_INPUT);
    gpio_set_pull_mode(p, GPIO_PULLUP_ONLY);
}

bool hw_boot_down(void)
{
    return gpio_get_level(hw_boot_pin()) == 0;
}

// ---------------------------------------------------------------- the chip
void hw_chip(tc_info_t *in)
{
    memset(in, 0, sizeof *in);
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    switch (ci.model) {
    case CHIP_ESP32:
        in->chip = TC_CHIP_ESP32;
        in->led = 2;
        break;
    case CHIP_ESP32S3:
        in->chip = TC_CHIP_S3;
        in->led = 48;
        break;
    case CHIP_ESP32C3:
        in->chip = TC_CHIP_C3;
        in->led = 8;
        break;
    case CHIP_ESP32C6:
        in->chip = TC_CHIP_C6;
        in->led = 8;
        break;
    case CHIP_ESP32S2:
        in->chip = TC_CHIP_S2;
        in->led = 0xFF;
        break;
    default:
        in->led = 0xFF;
        break;
    }
    in->rev = (uint8_t)(ci.revision / 100 * 10 + ci.revision % 100);
    in->fw = CC_FW;
    in->ngpio = (uint8_t)tc_chip_gpios(in->chip);
    for (int g = 0; g < in->ngpio; g++) {
        in->pins[g] = tc_pin_flags(in->chip, g);
    }
}
