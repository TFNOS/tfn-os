// TFN COMPANION: the start, the main loop (the radio frames, the clock of the core, the BOOT key, the LED) and a
// console on the UART for the tests (help: "help").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "comp_core.h"
#include "comp_hw.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#endif
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "irdecode/irdecode.h"

static const char *TAG = "comp";
static tc_info_t s_chip;
static uint32_t s_wire_hash = 1;

static void core_log(const char *line)
{
    ESP_LOGI(TAG, "%s", line);
}

// each send in the log: the pin, the carrier, the count and the first level times (a check against the remote)
static int tx_logged(int pin, uint32_t carrier_hz, bool inv, const int32_t *d, int n)
{
    const int e = hw_tx_send(pin, carrier_hz, inv, d, n);
    char b[160];
    int k = snprintf(b, sizeof b, "send GPIO %d %lu Hz %d times:", pin, (unsigned long)carrier_hz, n);
    for (int i = 0; i < n && i < 14 && k < (int)sizeof b - 8; i++) {
        k += snprintf(b + k, sizeof b - (size_t)k, " %ld", (long)d[i]);
    }
    ESP_LOGI(TAG, "%s (%d)", b, e);
    return e;
}

static const cc_io_t IO = {
        .send = hw_send,
        .set_channel = hw_set_channel,
        .now_ms = hw_now_ms,
        .random = hw_random,
        .save = hw_save,
        .rx_open = hw_rx_open,
        .rx_close = hw_rx_close,
        .rx_read = hw_rx_read,
        .tx_send = tx_logged,
        .tx_idle = hw_tx_idle,
        .out_set = hw_out_set,
        .in_open = hw_in_open,
        .in_read = hw_rx_level,
        .adc_read = hw_adc_read,
        .pin_free = hw_pin_free,
        .ota_begin = hw_ota_begin,
        .ota_write = hw_ota_write,
        .ota_end = hw_ota_end,
        .ota_abort = hw_ota_abort,
        .ota_valid = hw_ota_valid,
        .restart = hw_restart,
        .led = hw_led,
        .log = core_log,
};

// the LED of the board, when no module uses its pin
static void led_update(void)
{
    const cc_saved_t *s = cc_saved();
    const uint32_t h = tc_wire_hash(s->wire, s->nwire);
    if (h == s_wire_hash) {
        return;
    }
    s_wire_hash = h;
    int led = s_chip.led == 0xFF ? -1 : s_chip.led;
    for (int i = 0; i < s->nwire; i++) {
        if (s->wire[i].pin == led) {
            led = -1;
        }
    }
    hw_led_pin(led);
}

// ---------------------------------------------------------------- the console
static const tc_wire_t *find_role(int role)
{
    const cc_saved_t *s = cc_saved();
    for (int i = 0; i < s->nwire; i++) {
        if (s->wire[i].role == role) {
            return &s->wire[i];
        }
    }
    return NULL;
}

// a pin with a receiver module in the wiring (the console never drives it)
static bool wired_rx(int pin)
{
    const cc_saved_t *s = cc_saved();
    for (int i = 0; i < s->nwire; i++) {
        if (s->wire[i].pin == pin && tc_role_rx(s->wire[i].role)) {
            return true;
        }
    }
    return false;
}

static int role_of(const char *n)
{
    static const char *const R[TC_R_N] = {"",      "irrx", "irtx",   "315rx",  "315tx",   "433rx", "433tx",
                                          "relay", "out",  "button", "motion", "contact", "analog"};
    for (int i = 1; i < TC_R_N; i++) {
        if (!strcmp(n, R[i])) {
            return i;
        }
    }
    return 0;
}

static void print_info(void)
{
    const cc_saved_t *s = cc_saved();
    cc_state_t st;
    cc_state(&st);
    uint8_t mac[6];
    hw_my_mac(mac);
    printf("CHIP %s rev %d fw %d MAC %02X:%02X:%02X:%02X:%02X:%02X\n", tc_chip_name(s_chip.chip), s_chip.rev, CC_FW,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("PAIRED %d MASTER %02X:%02X:%02X:%02X:%02X:%02X \"%s\" CH %d SEARCH %d PAIRWAIT %d\n", s->paired,
           s->master[0], s->master[1], s->master[2], s->master[3], s->master[4], s->master[5], s->mname, st.ch,
           st.searching, st.pair_wait);
    printf("FRAMES rx %lu badtag %lu old %lu tx %lu sends %lu captures %lu\n", (unsigned long)st.rx_frames,
           (unsigned long)st.rx_bad_tag, (unsigned long)st.rx_old, (unsigned long)st.tx_frames, (unsigned long)st.sends,
           (unsigned long)st.captures);
    for (int i = 0; i < s->nwire; i++) {
        printf("WIRE GPIO %d %s%s carrier %d\n", s->wire[i].pin, tc_role_name(s->wire[i].role),
               (s->wire[i].flags & TC_WF_INVERT) ? " INVERT" : "", s->wire[i].carrier_khz);
    }
}

static void cmd_wire(int argc, char **argv)
{
    const cc_saved_t *s = cc_saved();
    tc_wire_t w[TC_WIRE_MAX];
    int n = s->nwire;
    memcpy(w, s->wire, sizeof w);
    if (argc >= 2 && !strcmp(argv[1], "clear")) {
        n = 0;
    } else if (argc >= 3 && !strcmp(argv[1], "del")) {
        const int pin = atoi(argv[2]);
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (w[i].pin != pin) {
                w[k++] = w[i];
            }
        }
        n = k;
    } else if (argc >= 3) {
        tc_wire_t e = {(uint8_t)atoi(argv[1]), (uint8_t)role_of(argv[2]), 0, 0};
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "inv")) {
                e.flags |= TC_WF_INVERT;
            } else {
                e.carrier_khz = (uint8_t)atoi(argv[i]);
            }
        }
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (w[i].pin != e.pin) {
                w[k++] = w[i];
            }
        }
        n = k;
        if (n < TC_WIRE_MAX) {
            w[n++] = e;
        }
    } else {
        print_info();
        return;
    }
    uint8_t err[TC_WIRE_MAX], warn[TC_WIRE_MAX];
    if (!cc_wire_set(w, n, err, warn)) {
        for (int i = 0; i < n; i++) {
            printf("WIRE GPIO %d: %s\n", w[i].pin, tc_err_name(err[i]));
        }
        printf("WIRE FAIL\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        if (warn[i]) {
            printf("WIRE GPIO %d: warning%s%s\n", w[i].pin, (warn[i] & TC_W_STRAP) ? " STRAPPING PIN" : "",
                   (warn[i] & TC_W_LED) ? " BOARD LED" : "");
        }
    }
    led_update();
    printf("WIRE OK\n");
}

// one frame from a receiver pin (the console; the core does the same for the master)
// (drain: first throw away the old level times; the self test keeps the ones of its own send)
static int capture(int pin, int ms, int32_t *d, int max, bool drain)
{
    static int32_t tmp[256];
    int32_t idle;
    bool act;
    while (drain && hw_rx_read(pin, tmp, 256, &idle, &act) > 0) {
    }
    int n = 0;
    const uint32_t t0 = hw_now_ms();
    while (hw_now_ms() - t0 < (uint32_t)ms) {
        const int k = hw_rx_read(pin, tmp, 256, &idle, &act);
        for (int i = 0; i < k; i++) {
            if (n == 0 && tmp[i] < 0) {
                continue;
            }
            if (tmp[i] < 0 && -tmp[i] >= IRD_GAP_US) {
                if (n >= 4) {
                    return n;
                }
                n = 0;
                continue;
            }
            if (n < max) {
                d[n++] = tmp[i];
            }
        }
        if (n >= 4 && !act && idle >= IRD_GAP_US) {
            return n;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return n >= 4 ? n : 0;
}

static void print_decode(const int32_t *d, int n)
{
    printf("LEVELS %d:", n);
    for (int i = 0; i < n; i++) {
        printf(" %ld", (long)d[i]);
    }
    printf("\n");
    static ird_code_t c;
    if (!ird_decode(d, n, false, &c)) {
        printf("DECODE none\n");
        return;
    }
    char hx[80];
    ird_raw_hex(&c, hx, sizeof hx);
    printf("DECODE %s address 0x%lX command 0x%lX bits %d raw %s%s\n", ird_name(c.proto), (unsigned long)c.address,
           (unsigned long)c.command, c.nbits, hx, (c.flags & IRD_F_REPEAT) ? " REPEAT" : "");
}

static int proto_of(const char *n)
{
    for (int p = 1; p < IRD_P_PULSE_DISTANCE; p++) {
        const char *a = ird_name(p);
        int i = 0;
        while (a[i] && n[i] && (a[i] | 0x20) == (n[i] | 0x20)) {
            i++;
        }
        if (!a[i] && !n[i]) {
            return p;
        }
    }
    return 0;
}

static int send_code(int proto, uint32_t addr, uint32_t cmd, int nbits, int pin)
{
    static ird_code_t c;
    static int32_t d[1400];
    ird_make(proto, addr, cmd, 0, nbits, &c);
    const int n = ird_encode_press(&c, d, 1400);
    if (n <= 0) {
        printf("SEND no frame\n");
        return -1;
    }
    const tc_wire_t *w = NULL;
    const cc_saved_t *s = cc_saved();
    for (int i = 0; i < s->nwire; i++) {
        if (s->wire[i].pin == pin) {
            w = &s->wire[i];
        }
    }
    const bool inv = w && (w->flags & TC_WF_INVERT);
    const int e = hw_tx_send(pin, c.carrier_hz ? c.carrier_hz : 38000, inv, d, n);
    printf("SEND %s %d level times on GPIO %d: %d\n", ird_name(proto), n, pin, e);
    return e;
}

static void cmd_selftest(int argc, char **argv)
{
    const tc_wire_t *rx = find_role(TC_R_IR_RX), *tx = find_role(TC_R_IR_TX);
    if (!rx || !tx) {
        printf("SELFTEST needs an IR receiver and an IR LED in the wiring\n");
        return;
    }
    const int proto = argc > 1 ? proto_of(argv[1]) : IRD_P_NEC;
    int32_t idle;
    bool act;
    static int32_t tmp[256], d[700];
    while (hw_rx_read(rx->pin, tmp, 256, &idle, &act) > 0) {
    }
    send_code(proto ? proto : IRD_P_NEC, 0x04, 0x08, proto == IRD_P_SONY ? 12 : 0, tx->pin);
    const int n = capture(rx->pin, 300, d, 700, false);
    if (!n) {
        printf("SELFTEST FAIL: the receiver saw nothing\n");
        return;
    }
    print_decode(d, n);
}

static void cmd(char *line)
{
    char *argv[24];
    int argc = 0;
    for (char *t = strtok(line, " \t"); t && argc < 24; t = strtok(NULL, " \t")) {
        argv[argc++] = t;
    }
    if (!argc) {
        return;
    }
    const char *c = argv[0];
    if (!strcmp(c, "help")) {
        printf("info | wire [<pin> <irrx|irtx|315rx|315tx|433rx|433tx|relay|out|button|motion|contact|analog> [inv] "
               "[khz] | del <pin> | clear]\nmods | set <pin> <0|1> [ms]\n"
               "rxstat | pin <pin> | watch <pin> <secs> | cap <pin> [secs] | send <proto> <addr> <cmd> [pin] | "
               "selftest "
               "[proto]\n"
               "boot | hold | forget | restart\n");
    } else if (!strcmp(c, "info")) {
        print_info();
    } else if (!strcmp(c, "wire")) {
        cmd_wire(argc, argv);
    } else if (!strcmp(c, "watch") && argc >= 2) {
        const int pin = atoi(argv[1]);
        const int secs = argc > 2 ? atoi(argv[2]) : 5;
        const bool own = hw_rx_open(pin, true, false);
        static int32_t tmp[256];
        uint32_t edges = 0;
        for (int s = 0; s < secs * 5; s++) {
            vTaskDelay(pdMS_TO_TICKS(200));
            int32_t idle;
            bool act;
            const int k = hw_rx_read(pin, tmp, 256, &idle, &act);
            edges += (uint32_t)k;
            printf("WATCH GPIO %d level %d active %d edges %lu new %d\n", pin, hw_rx_level(pin), act,
                   (unsigned long)edges, k);
        }
        (void)own;
    } else if (!strcmp(c, "cap") && argc >= 2) {
        const int pin = atoi(argv[1]);
        const int secs = argc > 2 ? atoi(argv[2]) : 10;
        hw_rx_open(pin, true, false);
        static int32_t d[700];
        printf("CAP GPIO %d: point a remote and press a key (%d s)\n", pin, secs);
        const int n = capture(pin, secs * 1000, d, 700, true);
        if (!n) {
            printf("CAP none\n");
        } else {
            print_decode(d, n);
        }
    } else if (!strcmp(c, "send") && argc >= 4) {
        const tc_wire_t *tx = find_role(TC_R_IR_TX);
        const int pin = argc > 4 ? atoi(argv[4]) : (tx ? tx->pin : -1);
        const int proto = proto_of(argv[1]);
        if (pin < 0 || !proto) {
            printf("SEND needs a protocol and an IR LED pin\n");
        } else {
            send_code(proto, (uint32_t)strtoul(argv[2], NULL, 0), (uint32_t)strtoul(argv[3], NULL, 0),
                      proto == IRD_P_SONY ? 12 : 0, pin);
        }
    } else if (!strcmp(c, "pin") && argc >= 2) {
        // the level of a pin with no pull, the pull-up and the pull-down (a wiring check)
        const int pin = atoi(argv[1]);
        static const gpio_pull_mode_t M[3] = {GPIO_FLOATING, GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY};
        static const char *const N[3] = {"FLOAT", "PULLUP", "PULLDOWN"};
        hw_rx_close(pin);
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        for (int k = 0; k < 3; k++) {
            gpio_set_pull_mode(pin, M[k]);
            vTaskDelay(pdMS_TO_TICKS(20));
            int hi = 0;
            for (int i = 0; i < 1000; i++) {
                hi += gpio_get_level(pin);
                esp_rom_delay_us(100);
            }
            printf("PIN GPIO %d %s: high %d of 1000\n", pin, N[k], hi);
        }
        // (the wiring again: a receiver on this pin opens again)
        const cc_saved_t *sv = cc_saved();
        tc_wire_t w[TC_WIRE_MAX];
        memcpy(w, sv->wire, sizeof w);
        cc_wire_set(w, sv->nwire, NULL, NULL);
    } else if ((!strcmp(c, "ledtest") || !strcmp(c, "ledon")) && argc >= 2 && wired_rx(atoi(argv[1]))) {
        printf("%s GPIO %s: refused, a receiver is on this pin\n", c, argv[1]);
    } else if (!strcmp(c, "ledtest") && argc >= 2) {
        // the LED with another peripheral (LEDC: 38 kHz, 1 ms on, 2 ms off, 100 times), the edges of the receiver
        const int pin = atoi(argv[1]);
        const tc_wire_t *rx = find_role(TC_R_IR_RX);
        ledc_timer_config_t tc = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                  .duty_resolution = LEDC_TIMER_8_BIT,
                                  .timer_num = LEDC_TIMER_0,
                                  .freq_hz = 38000,
                                  .clk_cfg = LEDC_AUTO_CLK};
        ledc_channel_config_t cc = {.gpio_num = pin,
                                    .speed_mode = LEDC_LOW_SPEED_MODE,
                                    .channel = LEDC_CHANNEL_0,
                                    .timer_sel = LEDC_TIMER_0,
                                    .duty = 0,
                                    .hpoint = 0};
        if (ledc_timer_config(&tc) != ESP_OK || ledc_channel_config(&cc) != ESP_OK) {
            printf("LEDTEST: no LEDC\n");
            return;
        }
        static int32_t tmp[256];
        int32_t idle;
        bool act;
        uint32_t edges = 0;
        while (rx && hw_rx_read(rx->pin, tmp, 256, &idle, &act) > 0) {
        }
        for (int k = 0; k < 100; k++) {
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 85);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            esp_rom_delay_us(1000);
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            esp_rom_delay_us(2000);
            if (rx) {
                edges += (uint32_t)hw_rx_read(rx->pin, tmp, 256, &idle, &act);
            }
        }
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        hw_tx_idle(pin, false);
        printf("LEDTEST GPIO %d: the receiver saw %lu edges (about 200 = the LED shines)\n", pin, (unsigned long)edges);
    } else if (!strcmp(c, "ledon") && argc >= 2) {
        // the carrier for some seconds (a phone camera shows the light of a working IR LED)
        const int pin = atoi(argv[1]);
        const int secs = argc > 2 ? atoi(argv[2]) : 10;
        static int32_t d[2];
        d[0] = 30000;
        d[1] = -100;
        for (int k = 0; k < secs * 33; k++) {
            hw_tx_send(pin, 38000, false, d, 2);
        }
        printf("LEDON done\n");
    } else if (!strcmp(c, "capn") && argc >= 2) {
        // each frame of a receiver for some seconds: its level times, the shortest one and its decode
        const int pin = atoi(argv[1]);
        const int secs = argc > 2 ? atoi(argv[2]) : 60;
        static int32_t d[700];
        printf("CAPN GPIO %d for %d s: press the keys of the remote\n", pin, secs);
        const uint32_t t0 = hw_now_ms();
        int nf = 0;
        while (hw_now_ms() - t0 < (uint32_t)secs * 1000) {
            const int n = capture(pin, 1000, d, 700, false);
            if (n) {
                int32_t mn = 1 << 30;
                for (int i = 0; i < n; i++) {
                    const int32_t a = d[i] < 0 ? -d[i] : d[i];
                    mn = a < mn ? a : mn;
                }
                printf("FRAME %d: %d times, the shortest %ld us\n", ++nf, n, (long)mn);
                print_decode(d, n);
            }
        }
        printf("CAPN done\n");
    } else if (!strcmp(c, "isrtest") && argc >= 2) {
        // the capture path of a receiver pin: the pin pulls itself low and lets go (open drain: no fight with the
        // module) 50 times; the edge interrupt must record about 100 level times
        const int pin = atoi(argv[1]);
        static int32_t tmp[256];
        int32_t idle;
        bool act;
        while (hw_rx_read(pin, tmp, 256, &idle, &act) > 0) {
        }
        gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT_OD);
        for (int k = 0; k < 50; k++) {
            gpio_set_level(pin, 0);
            esp_rom_delay_us(500);
            gpio_set_level(pin, 1);
            esp_rom_delay_us(500);
        }
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        int n = 0, k;
        while ((k = hw_rx_read(pin, tmp, 256, &idle, &act)) > 0) {
            n += k;
        }
        printf("ISRTEST GPIO %d: %d level times recorded (about 100 = the capture path works)\n", pin, n);
    } else if (!strcmp(c, "blink") && argc >= 2) {
        // the LED steady on (no carrier), 1 s on and 1 s off: slow enough for a phone camera
        const int pin = atoi(argv[1]);
        if (wired_rx(pin)) {
            printf("BLINK GPIO %d: refused, a receiver is on this pin\n", pin);
            return;
        }
        const int secs = argc > 2 ? atoi(argv[2]) : 60;
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
        gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_2 /* (about 20 mA: a long time with no resistor) */);
        for (int k = 0; k < secs; k++) {
            gpio_set_level(pin, !(k & 1));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        hw_tx_idle(pin, false);
        printf("BLINK done\n");
    } else if (!strcmp(c, "pinout") && argc >= 2) {
        // the pin as an output: the level that the pin reads back (a LED with no resistor pulls a HIGH output down)
        const int pin = atoi(argv[1]);
        if (wired_rx(pin)) {
            // (never drive the output of a receiver module: its output stage is weak)
            printf("PINOUT GPIO %d: refused, a receiver is on this pin\n", pin);
            return;
        }
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT_OUTPUT);
        for (int drive = 0; drive < 4; drive++) {
            gpio_set_drive_capability(pin, (gpio_drive_cap_t)drive);
            gpio_set_level(pin, 1);
            esp_rom_delay_us(200);
            int hi = 0;
            for (int i = 0; i < 200; i++) {
                hi += gpio_get_level(pin);
            }
            gpio_set_level(pin, 0);
            printf("PINOUT GPIO %d drive %d: HIGH out reads high %d of 200\n", pin, drive, hi);
        }
        hw_tx_idle(pin, false);
    } else if (!strcmp(c, "rxstat")) {
        hw_rx_diag();
    } else if (!strcmp(c, "mods")) {
        // the switch and analog modules: GPIO, role, value, count
        tc_state_t st[TC_WIRE_MAX];
        const int n = cc_states(st, TC_WIRE_MAX);
        const cc_saved_t *s = cc_saved();
        for (int i = 0; i < n; i++) {
            int role = 0;
            for (int k = 0; k < s->nwire; k++) {
                role = s->wire[k].pin == st[i].pin ? s->wire[k].role : role;
            }
            printf("MOD GPIO %d %s value %u count %u\n", st[i].pin, tc_role_name(role), st[i].v, st[i].cnt);
        }
        printf("MODS %d\n", n);
    } else if (!strcmp(c, "set")) {
        // set <pin> <0|1> [ms]: a switch output (only a pin with the role RELAY or OUTPUT)
        const bool ok = argc >= 3 && cc_set(atoi(argv[1]), atoi(argv[2]), argc > 3 ? atoi(argv[3]) : 0);
        printf("%s\n", ok ? "SET OK" : "SET: not a switch output of the wiring");
    } else if (!strcmp(c, "selftest")) {
        cmd_selftest(argc, argv);
    } else if (!strcmp(c, "boot")) {
        cc_boot_press();
        printf("BOOT pressed\n");
    } else if (!strcmp(c, "hold") || !strcmp(c, "forget")) {
        cc_boot_hold();
        printf("FORGET OK\n");
    } else if (!strcmp(c, "restart")) {
        esp_restart();
    } else {
        printf("? (help)\n");
    }
}

static void console_poll(void)
{
    static char line[256];
    static int n;
    uint8_t b[64];
    int k = uart_read_bytes(UART_NUM_0, b, sizeof b, 0);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    // (a board with only the USB of the chip: the console is the USB serial port)
    if (k <= 0) {
        k = usb_serial_jtag_read_bytes(b, sizeof b, 0);
    }
#endif
    for (int i = 0; i < k; i++) {
        if (b[i] == '\r' || b[i] == '\n') {
            if (n) {
                line[n] = 0;
                n = 0;
                cmd(line);
            }
        } else if (n < (int)sizeof line - 1) {
            line[n++] = (char)b[i];
        }
    }
}

// ---------------------------------------------------------------- the BOOT key
static void boot_poll(void)
{
    static uint32_t down_at;
    static bool down, held;
    const uint32_t t = hw_now_ms();
    if (hw_boot_down()) {
        if (!down) {
            down = true;
            held = false;
            down_at = t;
        } else if (!held && t - down_at >= 10000) {
            held = true;
            cc_boot_hold();
        }
    } else if (down) {
        down = false;
        if (!held && t - down_at >= 30) {
            cc_boot_press();
        }
    }
}

void app_main(void)
{
    hw_nvs_init();
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    usb_serial_jtag_driver_config_t ucfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&ucfg);
#endif
    hw_chip(&s_chip);
    hw_radio_start();
    hw_boot_init();
    cc_saved_t sv;
    hw_load(&sv);
    uint8_t mac[6];
    hw_my_mac(mac);
    cc_init(&IO, &sv, &s_chip, mac);
    led_update();
    ESP_LOGI(TAG, "TFN COMPANION %d on %s, MAC %02X:%02X:%02X:%02X:%02X:%02X (console: help)", CC_FW,
             tc_chip_name(s_chip.chip), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    uint32_t tick_at = 0;
    for (;;) {
        hw_rx_t r;
        if (hw_rx_pop(&r, 5)) {
            cc_rx(r.mac, r.d, r.len);
        }
        const uint32_t t = hw_now_ms();
        if (t - tick_at >= 10) {
            tick_at = t;
            cc_tick();
            boot_poll();
            console_poll();
            led_update();
        }
    }
}
