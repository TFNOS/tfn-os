// SERIAL MONITOR: the port (see ser_port.h). The demo device and its line (the bits in time) are pure C; the UART of
// the board is under ESP_PLATFORM.
// The task: the app task calls all the ser_* functions. The memory: the file statics; the UART driver and its ring
// in internal RAM (ser_open installs it, ser_close deletes it).
#include "ser_port.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "rfpulse/rfp_board.h"

static int s_psrc = -1;
static ser_cfg_t s_pcfg;
static ser_stats_t s_pst;

const char *ser_err(int e)
{
    switch (e) {
    case SER_OK:
        return "OK";
    case SER_ENOPINS:
        return "THIS BOARD HAS NO PINS FOR A SERIAL DEVICE";
    case SER_EDRIVER:
        return "THE UART DRIVER DID NOT START";
    case SER_ECONFIG:
        return "THE UART DID NOT TAKE THE SETTINGS";
    case SER_EMEM:
        return "NO MEMORY";
    default:
        return "?";
    }
}

bool ser_has_pins(void)
{
    return rfp_board_uart()->data_gpio >= 0 && rfp_board_uart()->tx_gpio >= 0;
}
bool ser_is_open(void)
{
    return s_psrc >= 0;
}
int ser_source(void)
{
    return s_psrc;
}
void ser_stats(ser_stats_t *s)
{
    *s = s_pst;
}

// ================================================================ the demo device and its line
// The device prints at 115200 8N1. Each byte goes on the line at a time (ns): a start bit (0), 8 data bits (the
// lowest first), a stop bit (1); the line is high (1) between the bytes. The receiver of the app looks for a falling
// edge, then samples the middle of each bit at ITS baud: the same steps as a UART.
#define DEV_BAUD 115200
#define DQ       4096
static struct {
    int64_t ts;
    uint8_t b;
} s_dq[DQ];
static uint32_t s_dq_w, s_dq_r;           // r: the oldest byte that the receiver can still need
static int64_t s_dev_free;                // the time when the line of the device is free (ns)
static int64_t s_rx_t;                    // the receiver: it looks for a start bit after this time (ns)
static int64_t s_next_line, s_next_rssi;  // (ns)
static int s_line_n;
static bool s_led;
static char s_cmd[64];
static int s_cmd_n;
static uint8_t s_rxq[4096];  // the bytes that the receiver made, for ser_read
static int s_rxq_n;
static const int64_t DEV_BIT = 1000000000LL / DEV_BAUD;

static int64_t now_ns(void)
{
    return esp_timer_get_time() * 1000;
}

static void dev_send_at(const char *s, int64_t at)
{
    if (s_dev_free < at) {
        s_dev_free = at;
    }
    for (; *s; s++) {
        if (s_dq_w - s_dq_r >= DQ) {
            return;  // (the receiver is far behind: the rest goes)
        }
        s_dq[s_dq_w % DQ].ts = s_dev_free;
        s_dq[s_dq_w % DQ].b = (uint8_t)*s;
        s_dq_w++;
        s_dev_free += 10 * DEV_BIT;
    }
}
static void dev_send(const char *s)
{
    dev_send_at(s, now_ns());
}

static void dev_banner(int64_t at)
{
    dev_send_at("\r\nets Jul 29 2019 12:21:46\r\n\r\nrst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)\r\n"
                "configsip: 0, SPIWP:0xee\r\nmode:DIO, clock div:1\r\nentry 0x403c98b8\r\n"
                "I (312) demo: TFN DEMO DEVICE 1.0 (115200 8N1)\r\nI (315) demo: type help\r\n",
                at);
}

// the bit k (0 = the start bit .. 9 = the stop bit) of a byte on the line of the device
static int dev_bit(uint8_t b, int k)
{
    return k == 0 ? 0 : k == 9 ? 1 : (b >> (k - 1)) & 1;
}

// the level of the line at the time t
static int line_at(int64_t t)
{
    for (uint32_t i = s_dq_r; i != s_dq_w; i++) {
        const int64_t ts = s_dq[i % DQ].ts;
        if (t < ts) {
            return 1;
        }
        if (t < ts + 10 * DEV_BIT) {
            return dev_bit(s_dq[i % DQ].b, (int)((t - ts) / DEV_BIT));
        }
    }
    return 1;
}

// the first falling edge of the line after the time t (-1: none before the end of the known line)
static int64_t next_fall(int64_t t)
{
    for (uint32_t i = s_dq_r; i != s_dq_w; i++) {
        const int64_t ts = s_dq[i % DQ].ts;
        if (ts + 10 * DEV_BIT <= t) {
            continue;
        }
        const uint8_t b = s_dq[i % DQ].b;
        for (int k = 0; k < 10; k++) {
            const int64_t tk = ts + k * DEV_BIT;
            if (tk <= t) {
                continue;
            }
            const int prev = k == 0 ? 1 : dev_bit(b, k - 1);
            if (prev == 1 && dev_bit(b, k) == 0) {
                return tk;
            }
        }
    }
    return -1;
}

static void rx_put(uint8_t b)
{
    if (s_rxq_n < (int)sizeof s_rxq) {
        s_rxq[s_rxq_n++] = b;
    } else {
        s_pst.overflow++;
    }
}

// the receiver of the app on the line, up to the time limit
static void rx_run(int64_t limit)
{
    const int64_t bit = 1000000000LL / (s_pcfg.baud > 0 ? s_pcfg.baud : 1);
    const int par = s_pcfg.parity != 'N';
    const int nbits = s_pcfg.bits < 5 ? 5 : s_pcfg.bits > 8 ? 8 : s_pcfg.bits;
    for (int guard = 0; guard < 20000; guard++) {
        const int64_t te = next_fall(s_rx_t);
        if (te < 0) {  // (no edge: the line is quiet up to its known end)
            if (s_dev_free < limit && s_rx_t < limit) {
                s_rx_t = limit;
            }
            break;
        }
        // (the bytes that end before this edge: no sample needs them; line_at and next_fall start after them)
        while (s_dq_r != s_dq_w && s_dq[s_dq_r % DQ].ts + 10 * DEV_BIT <= te) {
            s_dq_r++;
        }
        const int64_t end = te + (int64_t)(1 + nbits + par) * bit + bit / 2;
        if (end > limit) {
            break;  // (not all of the frame is on the line yet)
        }
        if (line_at(te + bit / 2) != 0) {  // (a glitch: no start bit)
            s_rx_t = te + bit / 2;
            continue;
        }
        int v = 0, ones = 0;
        for (int j = 0; j < nbits; j++) {
            const int l = line_at(te + (int64_t)(1 + j) * bit + bit / 2);
            v |= l << j;
            ones += l;
        }
        if (par) {
            const int p = line_at(te + (int64_t)(1 + nbits) * bit + bit / 2);
            const bool ok = s_pcfg.parity == 'E' ? ((ones + p) & 1) == 0 : ((ones + p) & 1) == 1;
            if (!ok) {
                s_pst.parity++;
            }
        }
        const int stop = line_at(te + (int64_t)(1 + nbits + par) * bit + bit / 2);
        if (!stop) {
            s_pst.frame++;
            if (v == 0) {
                s_pst.brk++;
            }
        }
        rx_put((uint8_t)v);
        s_pst.rx++;
        s_rx_t = te + (int64_t)(1 + nbits + par) * bit + bit / 2;
    }
    while (s_dq_r != s_dq_w && s_dq[s_dq_r % DQ].ts + 10 * DEV_BIT < s_rx_t - 1000000) {
        s_dq_r++;
    }
}

static void dev_status(char *o, int n)
{
    const int up = (int)(esp_timer_get_time() / 1000000);
    snprintf(o, (size_t)n, "{\"t\":%d.%d,\"hum\":%d,\"lux\":%d,\"led\":%s,\"up\":%d}\r\n", 21 + (s_line_n / 7) % 3,
             (s_line_n * 3) % 10, 44 + s_line_n % 5, 300 + (s_line_n * 37) % 90, s_led ? "true" : "false", up);
}

static void demo_tick(void)
{
    const int64_t now = now_ns();
    char b[128];
    if (now >= s_next_line) {
        s_next_line = now + 1000000000LL;
        dev_status(b, sizeof b);
        dev_send(b);
        s_line_n++;
    }
    if (now >= s_next_rssi) {
        s_next_rssi = now + 5300000000LL;
        snprintf(b, sizeof b, "I (%d) wifi: rssi -%d dBm\r\n", (int)(now / 1000000), 55 + s_line_n % 9);
        dev_send(b);
    }
    rx_run(now - 2000000);  // (2 ms back: a frame that ends now is on the line)
}

static void demo_command(void)
{
    char b[160];
    s_cmd[s_cmd_n] = 0;
    char *c = s_cmd;
    while (*c == ' ') {
        c++;
    }
    const int64_t at = now_ns() + 20000000;  // (the device answers after 20 ms)
    if (!*c) {
        return;
    }
    if (!strcmp(c, "help")) {
        dev_send_at("commands: help, status, led on, led off, reboot\r\n", at);
    } else if (!strcmp(c, "status")) {
        dev_status(b, sizeof b);
        dev_send_at(b, at);
    } else if (!strcmp(c, "led on") || !strcmp(c, "led off")) {
        s_led = c[5] == 'n';
        dev_send_at(s_led ? "OK LED ON\r\n" : "OK LED OFF\r\n", at);
    } else if (!strcmp(c, "reboot")) {
        dev_banner(at + 300000000);
    } else {
        snprintf(b, sizeof b, "? unknown command: %.40s (type help)\r\n", c);
        dev_send_at(b, at);
    }
}

static void demo_write(const uint8_t *d, int n)
{
    for (int i = 0; i < n; i++) {
        if (d[i] == '\r' || d[i] == '\n') {
            if (s_cmd_n) {
                demo_command();
            }
            s_cmd_n = 0;
        } else if (s_cmd_n < (int)sizeof s_cmd - 1) {
            s_cmd[s_cmd_n++] = (char)d[i];
        }
    }
}

static void demo_open(void)
{
    const int64_t now = now_ns();
    s_dq_w = s_dq_r = 0;
    s_dev_free = now;
    s_rx_t = now;
    s_rxq_n = 0;
    s_line_n = 0;
    s_cmd_n = 0;
    s_led = false;
    dev_banner(now + 200000000);
    s_next_line = now + 1500000000LL;
    s_next_rssi = now + 3700000000LL;
}

static int demo_read(uint8_t *b, int max)
{
    demo_tick();
    const int n = s_rxq_n < max ? s_rxq_n : max;
    memcpy(b, s_rxq, (size_t)n);
    memmove(s_rxq, s_rxq + n, (size_t)(s_rxq_n - n));
    s_rxq_n -= n;
    return n;
}

// ================================================================ the UART of the board
#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define UPORT UART_NUM_1
#define RXBUF 8192  // (the ring of the driver in internal RAM: 70 ms at 921600)
static QueueHandle_t s_q;

static int hw_config(const ser_cfg_t *c)
{
    uart_config_t u;
    memset(&u, 0, sizeof u);
    u.baud_rate = c->baud;
    u.data_bits = c->bits <= 5   ? UART_DATA_5_BITS
                  : c->bits == 6 ? UART_DATA_6_BITS
                  : c->bits == 7 ? UART_DATA_7_BITS
                                 : UART_DATA_8_BITS;
    u.parity = c->parity == 'E' ? UART_PARITY_EVEN : c->parity == 'O' ? UART_PARITY_ODD : UART_PARITY_DISABLE;
    u.stop_bits = c->stop == 15 ? UART_STOP_BITS_1_5 : c->stop == 20 ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
    u.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    u.source_clk = UART_SCLK_DEFAULT;
    if (uart_param_config(UPORT, &u) != ESP_OK) {
        return SER_ECONFIG;
    }
    uart_set_line_inverse(UPORT, c->inv ? (UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV) : 0);
    return SER_OK;
}

static int hw_open(const ser_cfg_t *c)
{
    const rfp_conn_t *k = rfp_board_uart();
    if (k->data_gpio < 0 || k->tx_gpio < 0) {
        return SER_ENOPINS;
    }
    if (uart_is_driver_installed(UPORT)) {
        uart_driver_delete(UPORT);
    }
    if (uart_driver_install(UPORT, RXBUF, 0, 24, &s_q, 0) != ESP_OK) {
        s_q = NULL;
        return SER_EDRIVER;
    }
    int e = hw_config(c);
    if (e == SER_OK &&
        uart_set_pin(UPORT, k->tx_gpio, k->data_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        e = SER_ECONFIG;
    }
    if (e != SER_OK) {
        uart_driver_delete(UPORT);
        s_q = NULL;
        return e;
    }
    gpio_pullup_en(k->data_gpio);  // (no device: the RX line stays high, no noise)
    uart_flush_input(UPORT);
    return SER_OK;
}

static void hw_close(void)
{
    const rfp_conn_t *k = rfp_board_uart();
    uart_driver_delete(UPORT);
    s_q = NULL;
    gpio_reset_pin(k->tx_gpio);  // (the pins go back to inputs: a device on them sees no driver)
    gpio_reset_pin(k->data_gpio);
}

static void hw_events(void)
{
    uart_event_t ev;
    while (s_q && xQueueReceive(s_q, &ev, 0) == pdTRUE) {
        switch (ev.type) {
        case UART_FRAME_ERR:
            s_pst.frame++;
            break;
        case UART_PARITY_ERR:
            s_pst.parity++;
            break;
        case UART_BREAK:
            s_pst.brk++;
            break;
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            s_pst.overflow++;
            uart_flush_input(UPORT);
            xQueueReset(s_q);
            return;
        default:
            break;
        }
    }
}

static int hw_read(uint8_t *b, int max)
{
    hw_events();
    size_t have = 0;
    if (uart_get_buffered_data_len(UPORT, &have) != ESP_OK || !have) {
        return 0;
    }
    const int n = uart_read_bytes(UPORT, b, have < (size_t)max ? (uint32_t)have : (uint32_t)max, 0);
    return n > 0 ? n : 0;
}

int ser_selftest(char *msg, int max)
{
    if (s_psrc != SER_PINS) {
        return -1;
    }
    static const char T[] = "TFN SELF TEST 0123456789 ABCDEFGHIJ\r\n";
    const int n = (int)sizeof T - 1;
    uart_set_loop_back(UPORT, true);
    uart_flush_input(UPORT);
    hw_events();
    uart_write_bytes(UPORT, T, (size_t)n);
    uart_wait_tx_done(UPORT, pdMS_TO_TICKS(500));
    char got[64];
    int k = 0;
    for (int tries = 0; tries < 30 && k < n; tries++) {
        const int r = uart_read_bytes(UPORT, (uint8_t *)got + k, (uint32_t)(n - k), pdMS_TO_TICKS(20));
        if (r > 0) {
            k += r;
        }
    }
    uart_set_loop_back(UPORT, false);
    hw_events();
    s_pst.tx += (uint32_t)n;
    got[k] = 0;
    for (int i = 0; i < k; i++) {
        if (got[i] == '\r' || got[i] == '\n') {
            got[i] = ' ';
        }
    }
    snprintf(msg, (size_t)max, "%d / %d: %.40s", k, n, got);
    return k == n && !memcmp(got, T, (size_t)n - 2) ? 1 : 0;
}

static int hw_level(void)
{
    return gpio_get_level(rfp_board_uart()->data_gpio);
}
static int hw_write(const void *b, int n)
{
    return uart_write_bytes(UPORT, b, (size_t)n);
}

#else  // the PC: the pins give no data (the PC tests drive the demo)
static int hw_config(const ser_cfg_t *c)
{
    (void)c;
    return SER_OK;
}
static int hw_open(const ser_cfg_t *c)
{
    (void)c;
    return ser_has_pins() ? SER_OK : SER_ENOPINS;
}
static void hw_close(void)
{
}
static int hw_read(uint8_t *b, int max)
{
    (void)b;
    (void)max;
    return 0;
}
static int hw_write(const void *b, int n)
{
    (void)b;
    return n;
}
static int hw_level(void)
{
    return 1;
}
int ser_selftest(char *msg, int max)
{
    snprintf(msg, (size_t)max, "THE PC HAS NO UART");
    return s_psrc == SER_PINS ? 1 : -1;
}
#endif

// ================================================================ the interface
int ser_open(int src, const ser_cfg_t *c)
{
    ser_close();
    memset(&s_pst, 0, sizeof s_pst);
    s_pst.level = -1;
    s_pcfg = *c;
    if (src == SER_DEMO) {
        demo_open();
    } else {
        const int e = hw_open(c);
        if (e != SER_OK) {
            return e;
        }
    }
    s_psrc = src;
    return SER_OK;
}

void ser_close(void)
{
    if (s_psrc == SER_PINS) {
        hw_close();
    }
    s_psrc = -1;
}

int ser_config(const ser_cfg_t *c)
{
    s_pcfg = *c;
    if (s_psrc == SER_PINS) {
        return hw_config(c);
    }
    if (s_psrc == SER_DEMO) {
        s_rx_t = now_ns();  // (the receiver starts again at the next start bit)
    }
    return SER_OK;
}

int ser_read(uint8_t *b, int max)
{
    int n = 0;
    if (s_psrc == SER_DEMO) {
        n = demo_read(b, max);
    } else if (s_psrc == SER_PINS) {
        n = hw_read(b, max);
        s_pst.rx += (uint32_t)n;
        s_pst.level = hw_level();
    }
    return n;
}

int ser_write(const void *b, int n)
{
    if (n <= 0) {
        return 0;
    }
    if (s_psrc == SER_DEMO) {
        demo_write(b, n);
    } else if (s_psrc == SER_PINS) {
        n = hw_write(b, n);
    } else {
        return 0;
    }
    if (n > 0) {
        s_pst.tx += (uint32_t)n;
    }
    return n;
}
