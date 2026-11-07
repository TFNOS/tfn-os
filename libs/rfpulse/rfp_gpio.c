// RFPULSE: a receiver module on a pin of the board (rfp_board.h): an interrupt at each edge puts the time of the level
// that ended into a ring in internal RAM; the reader takes them out. Two receivers use this code:
//   rfp_gpio  the RF receiver on the data pin (rfp_board_conn()). The pin has the internal pull-down: with nothing on
//             it the line stays low (RFP_LINE_QUIET). A super-regenerative receiver with no signal gives a stream of
//             noise pulses (thousands of edges a second): that tells that a receiver is on the pin (RFP_LINE_ALIVE).
//   rfp_ir    the IR receiver on the IR pin (rfp_board_ir()). The output of the module is low while it sees the
//             carrier: the pin has the pull-up, and the levels go the other way round (> 0 = a mark).
#include "rfpulse.h"
#include "rfp_board.h"

#ifdef ESP_PLATFORM
#include <string.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"

#define RING 2048  // a power of 2 (8 KB of internal RAM while the receiver is open)

static const rfp_caps_t CAPS = {
        .id = "gpio",
        .name = {"THE RECEIVER ON THE PINS", "המקלט על הפינים"},
        .mods = RFP_MOD_OOK | RFP_MOD_ASK,
        .flags = RFP_F_WIRED,
        .nfreq = 2,
        .freq_khz = {433920, 315000},
};

static const rfp_caps_t CAPS_IR = {
        .id = "ir",
        .name = {"THE IR RECEIVER ON THE PINS", "מקלט ה-IR על הפינים"},
        .mods = RFP_MOD_OOK,
        .flags = RFP_F_WIRED | RFP_F_IR,
        .nfreq = 1,
        .freq_khz = {38},
};

typedef struct {
    int pin;
    int inv;  // 1: the levels the other way round (an IR receiver), and the pull-up
    bool ir;  // the pin of rfp_board_ir() (else of rfp_board_conn())
    bool open;
    int32_t *ring;
    volatile uint32_t head, tail, lost, edges;
    volatile int64_t last_t;
    volatile int last_lvl;  // the level on the pin (not inverted)
    uint32_t win_edges;     // the edge count at the start of the window of the line state
    int64_t win_t;
    int line;
} ctx_t;
static ctx_t s_ctx = {.inv = 0, .ir = false};
static ctx_t s_ctx_ir = {.inv = 1, .ir = true};

static void IRAM_ATTR edge_isr(void *arg)
{
    ctx_t *c = arg;
    const int64_t t = esp_timer_get_time();
    const int lvl = gpio_ll_get_level(GPIO_LL_GET_HW(GPIO_PORT_0), (uint32_t)c->pin);
    if (lvl == c->last_lvl) {
        return;  // (two edges in one interrupt: the level did not change for us)
    }
    int64_t d = t - c->last_t;
    if (d > 0x3FFFFFFF) {
        d = 0x3FFFFFFF;
    }
    const uint32_t h = c->head;
    if (h - c->tail < RING) {
        c->ring[h & (RING - 1)] = (c->last_lvl ^ c->inv) ? (int32_t)d : -(int32_t)d;
        c->head = h + 1;
    } else {
        c->lost++;
    }
    c->last_t = t;
    c->last_lvl = lvl;
    c->edges++;
}

static int g_open(void *x)
{
    ctx_t *c = x;
    const rfp_conn_t *k = c->ir ? rfp_board_ir() : rfp_board_conn();
    if (k->data_gpio < 0) {
        return -2;
    }
    if (c->open) {
        return 0;
    }
    c->ring = heap_caps_malloc(sizeof(int32_t) * RING, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!c->ring) {
        return -3;
    }
    c->pin = k->data_gpio;
    c->head = c->tail = c->lost = c->edges = 0;
    gpio_config_t gc = {.pin_bit_mask = 1ULL << c->pin,
                        .mode = GPIO_MODE_INPUT,
                        .pull_down_en = c->inv ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
                        .pull_up_en = c->inv ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
                        .intr_type = GPIO_INTR_ANYEDGE};
    if (gpio_config(&gc) != ESP_OK) {
        heap_caps_free(c->ring);
        c->ring = NULL;
        return -4;
    }
    c->last_t = esp_timer_get_time();
    c->last_lvl = gpio_get_level(c->pin);
    c->win_t = c->last_t;
    c->win_edges = 0;
    c->line = (c->last_lvl ^ c->inv) ? RFP_LINE_HIGH : RFP_LINE_QUIET;
    esp_err_t e = gpio_install_isr_service(0);  // (the touch task has it already: ESP_ERR_INVALID_STATE)
    if ((e != ESP_OK && e != ESP_ERR_INVALID_STATE) || gpio_isr_handler_add(c->pin, edge_isr, c) != ESP_OK) {
        gpio_reset_pin(c->pin);
        heap_caps_free(c->ring);
        c->ring = NULL;
        return -5;
    }
    c->open = true;
    return 0;
}

static int g_read(void *x, int32_t *dur, int max)
{
    ctx_t *c = x;
    if (!c->open) {
        return -1;
    }
    int n = 0;
    while (n < max && c->tail != c->head) {
        dur[n++] = c->ring[c->tail & (RING - 1)];
        c->tail++;
    }
    return n;
}

static void g_stats(void *x, rfp_stats_t *s)
{
    ctx_t *c = x;
    memset(s, 0, sizeof(*s));
    s->now_us = esp_timer_get_time();
    if (!c->open) {
        return;
    }
    s->edges = c->edges;
    s->lost = c->lost;
    s->level = gpio_get_level(c->pin) ^ c->inv;
    s->last_edge_us = c->last_t;
    // the state of the line: the edges of the last 0.5 s (an IR remote: a few frames are enough)
    if (s->now_us - c->win_t >= 500000) {
        uint32_t e = c->edges - c->win_edges;
        c->line = e >= (c->inv ? 6u : 20u) ? RFP_LINE_ALIVE : s->level ? RFP_LINE_HIGH : RFP_LINE_QUIET;
        c->win_edges = c->edges;
        c->win_t = s->now_us;
    }
    s->line = c->line;
}

static void g_close(void *x)
{
    ctx_t *c = x;
    if (!c->open) {
        return;
    }
    gpio_isr_handler_remove(c->pin);
    gpio_reset_pin(c->pin);
    c->open = false;
    heap_caps_free(c->ring);
    c->ring = NULL;
}

static const char *g_error(int e)
{
    switch (e) {
    case -2:
        return "this board has no pin for a module";
    case -3:
        return "no memory";
    case -4:
        return "the pin did not start";
    case -5:
        return "the interrupt of the pin did not start";
    default:
        return "?";
    }
}

static const rfp_ops_t OPS = {g_open, g_read, g_stats, g_close, g_error};

bool rfp_gpio(rfp_src_t *out)
{
    if (rfp_board_conn()->data_gpio < 0) {
        return false;
    }
    out->caps = &CAPS;
    out->ops = &OPS;
    out->ctx = &s_ctx;
    return true;
}

bool rfp_ir(rfp_src_t *out)
{
    if (rfp_board_ir()->data_gpio < 0) {
        return false;
    }
    out->caps = &CAPS_IR;
    out->ops = &OPS;
    out->ctx = &s_ctx_ir;
    return true;
}

#else  // the PC: no pins

bool rfp_gpio(rfp_src_t *out)
{
    (void)out;
    return false;
}

bool rfp_ir(rfp_src_t *out)
{
    (void)out;
    return false;
}

#endif
