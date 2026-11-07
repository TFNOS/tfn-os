// The console command "rf" (tests of the native radio, rf_native.h):
//   rf boot [app]            a radio start (that opens the app)      rf exit     a normal start
//   rf open <MHz> [gain] [80|16] [20|40]                             rf close
//   rf cap [pairs]           one capture: the DC, the RMS, the peaks of a 1024-point FFT, the time
//   rf scan <MHz> <MHz> <step MHz> [gain]                             the peak and the floor of each LO
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "rf_native.h"
#include "rfdecode.h"
#include "rfpulse.h"
#include "rfp_board.h"
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "soc/gpio_reg.h"
#include "sdkconfig.h"
#include "board.h"
#include "sdr_fft.h"
#include "wifi.h"

static const char *TAG = "rf";
#define NFFT 1024
// A part of rf_console with a stack frame of more than 256 bytes: GCC does not inline it into the small frame of
// rf_console by itself. With this mark the stack of the console task stays as one frame.
#define RF_INLINE static inline __attribute__((always_inline))

static rfn_cfg_t s_cfg = {.lo_hz = 2440000000u, .rate = RFN_RATE_80M, .width = 40, .gain = 48, .filter = 0};
static int16_t *s_iq;
// the work buffers in PSRAM, made at the first use (static arrays took 33 KB of internal RAM on each board)
static struct {
    float re[NFFT], im[NFFT], t[NFFT], db[NFFT];
    double acc[NFFT];
} *W;

static bool buf(void)
{
    if (!s_iq) {
        s_iq = heap_caps_malloc(RFN_MAX_PAIRS * 4, MALLOC_CAP_SPIRAM);
    }
    if (!W) {
        W = heap_caps_calloc(1, sizeof(*W), MALLOC_CAP_SPIRAM);
    }
    return s_iq != NULL && W != NULL;
}

// The power of each bin (dB of the full scale, the centre at NFFT / 2) of the first NFFT pairs (a Hann window)
static void spectrum(const int16_t *iq, float *db)
{
    float *re = W->re, *im = W->im;
    double mi = 0, mq = 0;
    for (int k = 0; k < NFFT; k++) {
        mi += iq[2 * k];
        mq += iq[2 * k + 1];
    }
    mi /= NFFT;
    mq /= NFFT;
    for (int k = 0; k < NFFT; k++) {
        float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * k / NFFT);
        re[k] = (float)(iq[2 * k] - mi) * w;
        im[k] = (float)(iq[2 * k + 1] - mq) * w;
    }
    for (int i = 1, j = 0; i < NFFT; i++) {
        int bit = NFFT >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }
    for (int len = 2; len <= NFFT; len <<= 1) {
        float a = -2.0f * (float)M_PI / len;
        for (int i = 0; i < NFFT; i += len) {
            for (int k = 0; k < len / 2; k++) {
                float c = cosf(a * k), s = sinf(a * k);
                float xr = re[i + k + len / 2] * c - im[i + k + len / 2] * s;
                float xi = re[i + k + len / 2] * s + im[i + k + len / 2] * c;
                re[i + k + len / 2] = re[i + k] - xr;
                im[i + k + len / 2] = im[i + k] - xi;
                re[i + k] += xr;
                im[i + k] += xi;
            }
        }
    }
    const float full = 512.0f * NFFT * 0.5f;  // a full-scale tone with the Hann window
    for (int k = 0; k < NFFT; k++) {
        int b = (k + NFFT / 2) % NFFT;
        float p = (re[k] * re[k] + im[k] * im[k]) / (full * full);
        db[b] = 10.0f * log10f(p + 1e-12f);
    }
}

// The end of a pin test: an input with the pull-down. (gpio_reset_pin() turns the pull-up on: a transmitter on the
// pin then gets a weak HIGH and can send all the time.)
static void pin_off(int pin)
{
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
}

static int cmpf(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static float median(const float *db)
{
    float *t = W->t;
    memcpy(t, db, sizeof(float) * NFFT);
    qsort(t, NFFT, sizeof(float), cmpf);
    return t[NFFT / 2];
}

static void cap(int n)
{
    if (!buf()) {
        return;
    }
    uint8_t g = 0;
    int64_t t0 = esp_timer_get_time();
    int r = rfn_capture(s_iq, n, &g);
    int64_t t1 = esp_timer_get_time();
    if (r < 0) {
        ESP_LOGE(TAG, "cap: %s", rfn_error(r));
        return;
    }
    double si = 0, sq = 0, pi = 0, pq = 0;
    int mx = 0;
    for (int k = 0; k < n; k++) {
        int i = s_iq[2 * k], q = s_iq[2 * k + 1];
        si += i;
        sq += q;
        pi += (double)i * i;
        pq += (double)q * q;
        if (abs(i) > mx) {
            mx = abs(i);
        }
        if (abs(q) > mx) {
            mx = abs(q);
        }
    }
    ESP_LOGI(TAG, "cap %d pairs in %d us: gain idx %u, DC %.1f %.1f, RMS %.1f %.1f, max %d", n, (int)(t1 - t0), g,
             si / n, sq / n, sqrt(pi / n), sqrt(pq / n), mx);
    if (n < NFFT) {
        return;
    }
    float *db = W->db;
    int64_t t2 = esp_timer_get_time();
    spectrum(s_iq, db);
    int64_t t3 = esp_timer_get_time();
    const float span = (float)rfn_rate_hz(s_cfg.rate) / 1e6f;
    float fl = median(db);
    ESP_LOGI(TAG, "fft %d us, floor %.1f dBFS", (int)(t3 - t2), fl);
    // 32 groups: the highest bin of each
    char line[32 * 6 + 8];
    int o = 0;
    for (int gi = 0; gi < 32; gi++) {
        float m = -200;
        for (int k = gi * (NFFT / 32); k < (gi + 1) * (NFFT / 32); k++) {
            if (db[k] > m) {
                m = db[k];
            }
        }
        o += snprintf(line + o, sizeof(line) - o, "%4d ", (int)lroundf(m));
    }
    ESP_LOGI(TAG, "groups (%.1f MHz each): %s", span / 32, line);
    for (int p = 0; p < 5; p++) {
        int bi = 0;
        for (int k = 0; k < NFFT; k++) {
            if (db[k] > db[bi]) {
                bi = k;
            }
        }
        if (db[bi] < fl + 6) {
            break;
        }
        ESP_LOGI(TAG, "peak %+.3f MHz (%.3f MHz): %.1f dBFS", (bi - NFFT / 2) * span / NFFT,
                 s_cfg.lo_hz / 1e6 + (bi - NFFT / 2) * span / NFFT, db[bi]);
        for (int k = bi - 4; k <= bi + 4; k++) {
            if (k >= 0 && k < NFFT) {
                db[k] = -200;
            }
        }
    }
}

// rf (no word): the state of the radio and of the internal heap
static void rf_info(void)
{
    // (the WiFi state as a number: no network name and no address in a test log)
    ESP_LOGI(TAG, "rf: %s, %s, internal heap %u of %u, wifi state %d",
             rfn_available() ? "native radio" : "no native radio",
             rfn_radio_boot() ? "a radio start" : "a normal start",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL), (int)wifi_state());
}

// rf open|set <MHz> [gain] [80|16] [20|40]: open the radio, or change its settings
static void rf_open_set(const char *a)
{
    char *f = strtok(NULL, " \t\r\n"), *g = strtok(NULL, " \t\r\n"), *r = strtok(NULL, " \t\r\n"),
         *w = strtok(NULL, " \t\r\n");
    if (f) {
        s_cfg.lo_hz = (uint32_t)(atof(f) * 1e6 + 0.5);
    }
    if (g) {
        s_cfg.gain = (uint8_t)atoi(g);
    }
    if (r) {
        s_cfg.rate = atoi(r) == 16 ? RFN_RATE_16M : RFN_RATE_80M;
    }
    if (w) {
        s_cfg.width = atoi(w) == 20 ? 20 : 40;
    }
    uint32_t lo = 0;
    int64_t t0 = esp_timer_get_time();
    int e = !strcmp(a, "open") ? rfn_open(&s_cfg) : rfn_set(&s_cfg, &lo);
    ESP_LOGI(TAG, "%s %.3f MHz gain %u rate %lu width %u: %s (%d us)", a, s_cfg.lo_hz / 1e6, s_cfg.gain,
             (unsigned long)rfn_rate_hz(s_cfg.rate), s_cfg.width, rfn_error(e), (int)(esp_timer_get_time() - t0));
}

// rf tune <MHz>: the fast tune (rfn_tune)
static void rf_tune(void)
{
    char *f = strtok(NULL, " \t\r\n");
    if (!f) {
        return;
    }
    uint32_t lo = 0;
    int64_t t0 = esp_timer_get_time();
    int e = rfn_tune((uint32_t)(atof(f) * 1e6 + 0.5), &lo);
    s_cfg.lo_hz = lo;
    ESP_LOGI(TAG, "tune %s MHz: %s, LO %lu Hz (%d us)", f, rfn_error(e), (unsigned long)lo,
             (int)(esp_timer_get_time() - t0));
}

// rf shape [n]: the mean noise spectrum of n captures (the shape of the filter): 64 groups, the mean power of each in dB
RF_INLINE void rf_shape(void)
{
    char *c = strtok(NULL, " \t\r\n");
    int n = c ? atoi(c) : 50;
    if (!buf()) {
        return;
    }
    float *db = W->db;
    double *acc = W->acc;
    memset(acc, 0, sizeof(double) * NFFT);
    int got = 0;
    for (int r = 0; r < n; r++) {
        if (rfn_capture(s_iq, NFFT, NULL) < 0) {
            continue;
        }
        spectrum(s_iq, db);
        for (int k = 0; k < NFFT; k++) {
            acc[k] += pow(10.0, db[k] / 10.0);
        }
        got++;
    }
    if (!got) {
        return;
    }
    char line[64 * 5 + 8];
    int o = 0;
    for (int g = 0; g < 64; g++) {
        double m = 0;
        for (int k = g * 16; k < g * 16 + 16; k++) {
            m += acc[k] / got;
        }
        o += snprintf(line + o, sizeof(line) - o, "%d ", (int)lround(10.0 * log10(m / 16 + 1e-12)));
    }
    ESP_LOGI(TAG, "shape %d (%.2f MHz each): %s", got, rfn_rate_hz(s_cfg.rate) / 64e6, line);
}

// rf fft [n]: the time of sdr_fft (libs/sdrdsp) of n points: the buffer in PSRAM, then in internal RAM
static void rf_fft(void)
{
    char *c = strtok(NULL, " \t\r\n");
    int n = c ? atoi(c) : 2048;
    sdr_fft_t f;
    if (!sdr_fft_init(&f, n)) {
        return;
    }
    for (int where = 0; where < 2; where++) {
        float *x = heap_caps_malloc(sizeof(float) * 2 * n, where ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM);
        if (!x) {
            ESP_LOGI(TAG, "fft %d: no %s memory", n, where ? "internal" : "PSRAM");
            continue;
        }
        for (int k = 0; k < 2 * n; k++) {
            x[k] = (float)(k % 7) - 3.0f;
        }
        int64_t t0 = esp_timer_get_time();
        for (int r = 0; r < 10; r++) {
            sdr_fft(&f, x);
        }
        ESP_LOGI(TAG, "fft %d, the buffer in %s: %d us", n, where ? "internal RAM" : "PSRAM",
                 (int)((esp_timer_get_time() - t0) / 10));
        free(x);
    }
    sdr_fft_free(&f);
}

static void rf_tx(int pin);

// rf pin|tx|probe: the 315 / 433 MHz modules (RF DECODER): only the free pins of docs/headers.md
//   rf pin <gpio>                the level with the pull-up and with the pull-down (a transmitter input loads
//                                the pin: LOW with the pull-up; nothing on the pin: HIGH)
//   rf tx <gpio> <dev> [reps]    one burst of a demo device on a transmitter: 0 RCSwitch P1, 1 P6, 2 PPM, 3 PWM,
//                                4 Manchester, 9 the carrier test (200 ms on, 200 ms off)
//   rf probe [seconds]           find a hole of the socket: all the free pins with the pull-up; touch a wire
//                                from the hole to GND, and the log names the GPIO that goes LOW
static void rf_gpio_cmd(const char *a)
{
    char *g = strtok(NULL, " \t\r\n");
    int pin = g ? atoi(g) : -1;
    static const int FREE[] = BOARD_FREE_PINS;
    const int nfree = sizeof(FREE) / sizeof(FREE[0]);
    if (!strcmp(a, "probe")) {
        int secs = pin < 1 ? 30 : pin > 120 ? 120 : pin;
        int last[8] = {0};
        for (int i = 0; i < nfree && FREE[i] >= 0; i++) {
            gpio_reset_pin(FREE[i]);
            gpio_set_direction(FREE[i], GPIO_MODE_INPUT);
            gpio_set_pull_mode(FREE[i], GPIO_PULLUP_ONLY);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
        for (int i = 0; i < nfree && FREE[i] >= 0; i++) {
            last[i] = gpio_get_level(FREE[i]);
        }
        ESP_LOGI(TAG, "rf probe: %d s; touch a hole with a wire to GND", secs);
        int64_t t0 = esp_timer_get_time();
        while (esp_timer_get_time() - t0 < secs * 1000000LL) {
            for (int i = 0; i < nfree && FREE[i] >= 0; i++) {
                int v = gpio_get_level(FREE[i]);
                if (v != last[i]) {
                    ESP_LOGI(TAG, "rf probe: GPIO %d %s at %d ms", FREE[i], v ? "HIGH (released)" : "LOW (touched)",
                             (int)((esp_timer_get_time() - t0) / 1000));
                }
                last[i] = v;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        for (int i = 0; i < nfree && FREE[i] >= 0; i++) {
            pin_off(FREE[i]);
        }
        ESP_LOGI(TAG, "rf probe: end");
        return;
    }
    bool ok = false;
    for (int i = 0; i < nfree; i++) {
        ok |= FREE[i] == pin;
    }
    if (!ok) {
        ESP_LOGE(TAG, "rf %s: GPIO %d is not a free pin of this board", a, pin);
        return;
    }
    if (!strcmp(a, "pin")) {
        gpio_reset_pin(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
        esp_rom_delay_us(2000);
        int up = gpio_get_level(pin);
        gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
        esp_rom_delay_us(2000);
        int down = gpio_get_level(pin);
        // the voltage through each weak pull (about 45 kOhm), on the ADC1 pins (GPIO 1 to 10): a transmitter input
        // (a transistor base through a resistor) pulls the pull-up voltage down, but the digital level stays 1
        int mv[2] = {-1, -1};
        adc_oneshot_unit_handle_t u = NULL;
        adc_oneshot_unit_init_cfg_t uc = {.unit_id = ADC_UNIT_1};
        adc_oneshot_chan_cfg_t cc = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
        adc_cali_handle_t cal = NULL;
        adc_cali_curve_fitting_config_t kc = {
                .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
        if (pin >= 1 && pin <= 10 && adc_oneshot_new_unit(&uc, &u) == ESP_OK) {
            adc_channel_t ch = (adc_channel_t)(pin - 1);  // (ESP32-S3: ADC1 channel n = GPIO n + 1)
            kc.chan = ch;
            if (adc_oneshot_config_channel(u, ch, &cc) == ESP_OK &&
                adc_cali_create_scheme_curve_fitting(&kc, &cal) == ESP_OK) {
                for (int k = 0; k < 2; k++) {
                    gpio_set_pull_mode(pin, k ? GPIO_PULLDOWN_ONLY : GPIO_PULLUP_ONLY);
                    esp_rom_delay_us(2000);
                    int sum = 0, raw = 0, v = 0;
                    for (int i = 0; i < 16; i++) {
                        adc_oneshot_read(u, ch, &raw);
                        adc_cali_raw_to_voltage(cal, raw, &v);
                        sum += v;
                    }
                    mv[k] = sum / 16;
                }
                adc_cali_delete_scheme_curve_fitting(cal);
            }
            adc_oneshot_del_unit(u);
        }
        pin_off(pin);
        ESP_LOGI(TAG, "rf pin %d: pull-up %d, pull-down %d: %s; ADC %d mV with the pull-up, %d mV with the pull-down",
                 pin, up, down,
                 up && !down    ? "nothing pulls the pin (no load)"
                 : !up && !down ? "a load pulls the pin LOW (a module input)"
                 : up && down   ? "something drives the pin HIGH"
                                : "?",
                 mv[0], mv[1]);
        return;
    }
    rf_tx(pin);
}

// rf tx <gpio> <dev> [reps]: send the repeats of a demo device on a transmitter
static void rf_tx(int pin)
{
    char *dv = strtok(NULL, " \t\r\n"), *rp = strtok(NULL, " \t\r\n");
    int dev = dv ? atoi(dv) : 0, reps = rp ? atoi(rp) : 6;
    if (reps < 1) {
        reps = 1;
    }
    if (reps > 20) {
        reps = 20;
    }
    int32_t *one = heap_caps_malloc(sizeof(int32_t) * RFD_MAX_DUR, MALLOC_CAP_SPIRAM);  // (not a static array: RAM)
    if (!one) {
        return;
    }
    uint8_t bits[16];
    int n = 0;
    switch (dev) {
    case 1:
        n = rfd_enc_rcswitch(6, 0xA1B2C3, 24, 450, one, RFD_MAX_DUR);
        break;
    case 2: {
        static const uint8_t B[] = {0x8A, 0x31, 0x5C, 0x02, 0x70};
        memcpy(bits, B, sizeof(B));
        n = rfd_enc_ppm(bits, 36, 500, 1000, 2000, 9000, one, RFD_MAX_DUR);
        break;
    }
    case 3: {
        static const uint8_t B[] = {0xC3, 0x5A, 0x0F, 0x96};
        memcpy(bits, B, sizeof(B));
        n = rfd_enc_pwm(bits, 32, 300, 900, 7000, one, RFD_MAX_DUR);
        break;
    }
    case 4: {
        static const uint8_t B[] = {0xF0, 0x0D, 0x2B, 0xE1, 0x44, 0x7A};
        memcpy(bits, B, sizeof(B));
        n = rfd_enc_manch(bits, 48, 250, 6000, one, RFD_MAX_DUR);
        break;
    }
    case 9:
        one[0] = 200000;
        one[1] = -200000;
        n = 2;
        break;  // (the carrier test: 200 ms on, 200 ms off)
    default:
        n = rfd_enc_rcswitch(1, 0x5D5D51, 24, 340, one, RFD_MAX_DUR);
        break;
    }
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    int64_t t0 = esp_timer_get_time();
    // (a sync low first, so a receiver sees the start of the first repeat; the times by a busy wait. Each repeat
    // runs with the interrupts of this core off: an interrupt of the WiFi or the screen stretched a level, and the
    // Manchester code (250 us) did not decode on the receiver. Not for a repeat of 100 ms or more: the carrier
    // test, and the interrupt watchdog)
    int64_t span = 0;
    for (int i = 0; i < n; i++) {
        span += one[i] > 0 ? one[i] : -one[i];
    }
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    esp_rom_delay_us(12000);
    for (int r = 0; r < reps; r++) {
        if (span < 100000) {
            portENTER_CRITICAL(&mux);
        }
        for (int i = 0; i < n; i++) {
            gpio_set_level(pin, one[i] > 0);
            esp_rom_delay_us((uint32_t)(one[i] > 0 ? one[i] : -one[i]));
        }
        if (span < 100000) {
            portEXIT_CRITICAL(&mux);
        }
    }
    gpio_set_level(pin, 0);  // (the transmitter off)
    free(one);
    ESP_LOGI(TAG, "rf tx %d: device %d, %d repeats of %d levels in %d ms", pin, dev, reps, n,
             (int)((esp_timer_get_time() - t0) / 1000));
}

// rf scope [ms]: the data pin of the receiver, polled in a tight loop (no interrupt): the histogram of the
// highs and the lows, the high time in percent and the first 48 level times. It checks the edge interrupt.
RF_INLINE void rf_scope(void)
{
    char *mv = strtok(NULL, " \t\r\n");
    int ms = mv ? atoi(mv) : 30;
    if (ms < 1 || ms > 200) {
        ms = 30;
    }
    const int pin = rfp_board_conn()->data_gpio;
    if (pin < 0) {
        return;
    }
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLDOWN_ONLY);
    enum {
        NMAX = 8192
    };
    int32_t *lv = heap_caps_malloc(NMAX * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    if (!lv) {
        return;
    }
    const uint32_t mhz = (uint32_t)(esp_clk_cpu_freq() / 1000000), span = (uint32_t)ms * 1000u * mhz;
    int n = 0, lvl = (REG_READ(GPIO_IN_REG) >> pin) & 1;
    uint32_t c0 = esp_cpu_get_cycle_count(), last = c0, high = 0;
    while (n < NMAX) {
        uint32_t c = esp_cpu_get_cycle_count();
        if (c - c0 >= span) {
            break;
        }
        int v = (REG_READ(GPIO_IN_REG) >> pin) & 1;
        if (v != lvl) {
            int32_t d = (int32_t)((c - last) / mhz);
            lv[n++] = lvl ? d : -d;
            if (lvl) {
                high += c - last;
            }
            last = c;
            lvl = v;
        }
    }
    uint32_t total = esp_cpu_get_cycle_count() - c0;
    if (lvl) {
        high += esp_cpu_get_cycle_count() - last;
    }
    pin_off(pin);
    static const int EDGE_US[8] = {5, 10, 20, 50, 100, 400, 1600, 1 << 30};
    uint32_t hist[2][8] = {{0}};
    for (int i = 0; i < n; i++) {
        int32_t v = lv[i] < 0 ? -lv[i] : lv[i];
        int b = 0;
        while (v >= EDGE_US[b]) {
            b++;
        }
        hist[lv[i] > 0][b]++;
    }
    ESP_LOGI(TAG, "rf scope: GPIO %d, %d ms, %d levels, high %d%% of the time", pin, (int)(total / mhz / 1000), n,
             (int)((uint64_t)high * 100 / (total ? total : 1)));
    for (int h = 1; h >= 0; h--) {
        ESP_LOGI(TAG, "rf scope: %s <5 %u, <10 %u, <20 %u, <50 %u, <100 %u, <400 %u, <1600 %u, more %u",
                 h ? "highs" : "lows ", (unsigned)hist[h][0], (unsigned)hist[h][1], (unsigned)hist[h][2],
                 (unsigned)hist[h][3], (unsigned)hist[h][4], (unsigned)hist[h][5], (unsigned)hist[h][6],
                 (unsigned)hist[h][7]);
    }
    char line[400];
    int o = 0;
    for (int i = 0; i < n && i < 48 && o < (int)sizeof line - 12; i++) {
        o += snprintf(line + o, sizeof line - o, "%d ", (int)lv[i]);
    }
    line[o] = 0;
    ESP_LOGI(TAG, "rf scope: %s", line);
    free(lv);
}

// The sizes of rf rx: the raw level times at the end, the read buffer, the codes in the count
enum {
    NRAWD = 150000,
    NBUF = 1024,
    NCODE = 16
};

// One code of rf rx and its count
typedef struct {
    rfd_code_t c;
    int n;
} rf_seen_t;

// The buffers of rf rx and its settings
typedef struct {
    rfp_src_t *src;
    rfd_slicer_t *sl;
    rfd_packet_t *p;
    int32_t *dur;
    rf_seen_t *seen;
    int32_t *rawd;
    int secs, mode;
    bool verbose;
} rf_rx_t;

// rf rx: listen for the time of the command, then write the counts and the codes in the log
RF_INLINE void rf_rx_listen(const rf_rx_t *x)
{
    rfp_src_t *src = x->src;
    rfd_slicer_t *sl = x->sl;
    rfd_packet_t *p = x->p;
    int32_t *dur = x->dur;
    rf_seen_t *seen = x->seen;
    int32_t *rawd = x->rawd;
    const int secs = x->secs, mode = x->mode;
    const bool verbose = x->verbose;
    int nrawd = 0;
    int nseen = 0, npk = 0;
    // the raw level times: a histogram of the highs and the lows, and the first 64 times after a level of
    // more than 2 ms (a sync gap or a long pulse: maybe a packet of the transmitter)
    static const int EDGE_US[10] = {20, 50, 100, 200, 400, 800, 1600, 3200, 6400, 1 << 30};
    uint32_t hist[2][10] = {{0}};
    int32_t raw[64];
    int nraw = -1;
    rfp_stats_t st;
    rfd_slicer_init(sl, 80, 4300);
    ESP_LOGI(TAG, "rf rx: %d s, mode %d", secs, mode);
    int64_t t0 = esp_timer_get_time();
    while (esp_timer_get_time() - t0 < secs * 1000000LL) {
        int n;
        while ((n = src->ops->read(src->ctx, dur, NBUF)) > 0) {
            for (int i = 0; i < n; i++) {
                if (rawd && nrawd < NRAWD) {
                    rawd[nrawd++] = dur[i];
                }
                int32_t v = dur[i] < 0 ? -dur[i] : dur[i];
                int b = 0;
                while (v >= EDGE_US[b]) {
                    b++;
                }
                hist[dur[i] > 0][b]++;
                if (nraw >= 0 && nraw < 64) {
                    raw[nraw++] = dur[i];
                }
                if (nraw < 0 && v > 2000) {
                    nraw = 0;
                }
            }
            src->ops->stats(src->ctx, &st);
            rfd_slicer_feed(sl, dur, n, st.last_edge_us);
        }
        src->ops->stats(src->ctx, &st);
        if (!st.level) {
            rfd_slicer_idle(sl, (int32_t)(st.now_us - st.last_edge_us), st.now_us);
        }
        while (rfd_slicer_pop(sl, p)) {
            rfd_code_t c;
            npk++;
            if (verbose) {
                char ln[300];
                ESP_LOGI(TAG, "rf pk %d: n %d, gap before %d, after %d", npk, p->n, (int)p->gap_before,
                         (int)p->gap_after);
                for (int i0 = 0; i0 < p->n; i0 += 32) {
                    int o = 0;
                    for (int i = i0; i < p->n && i < i0 + 32; i++) {
                        o += snprintf(ln + o, sizeof ln - o, "%d ", (int)p->d[i]);
                    }
                    ESP_LOGI(TAG, "rf pd %d: %s", npk, ln);
                }
            }
            if (!rfd_decode(p, mode, 0, &c) || c.kind == RFD_K_RAW) {
                continue;
            }
            if (c.kind != RFD_K_RCSWITCH && (c.score < 0.75f || c.nbits < 8)) {
                continue;
            }
            int k = 0;
            while (k < nseen && !rfd_same(&seen[k].c, &c)) {
                k++;
            }
            if (k == nseen && nseen < NCODE) {
                seen[nseen++].c = c;
            }
            if (k < NCODE) {
                seen[k].n++;
            }
            if (k < NCODE && seen[k].n == 1) {
                char hex[40];
                rfd_hex(&c, hex, sizeof hex);
                ESP_LOGI(TAG, "rf rx: %s P%d %d bits %s, %d/%d us, gap %d us, fit %.2f (%d levels)",
                         rfd_kind_name(c.kind), c.proto, c.nbits, hex, c.t_short, c.t_long, (int)c.t_gap, c.score,
                         p->n);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    src->ops->stats(src->ctx, &st);
    src->ops->close(src->ctx);
    ESP_LOGI(TAG, "rf rx: end: %d packets, %u edges, %u lost, %u dropped", npk, (unsigned)st.edges, (unsigned)st.lost,
             (unsigned)sl->dropped);
    for (int h = 1; h >= 0; h--) {
        ESP_LOGI(TAG,
                 "rf rx: %s <20 %u, <50 %u, <100 %u, <200 %u, <400 %u, <800 %u, <1600 %u, <3200 %u, <6400 %u, "
                 "more %u",
                 h ? "highs" : "lows ", (unsigned)hist[h][0], (unsigned)hist[h][1], (unsigned)hist[h][2],
                 (unsigned)hist[h][3], (unsigned)hist[h][4], (unsigned)hist[h][5], (unsigned)hist[h][6],
                 (unsigned)hist[h][7], (unsigned)hist[h][8], (unsigned)hist[h][9]);
    }
    char line[400];
    int o = 0;
    for (int i = 0; i < (nraw < 0 ? 0 : nraw) && o < (int)sizeof line - 12; i++) {
        o += snprintf(line + o, sizeof line - o, "%d ", (int)raw[i]);
    }
    line[o] = 0;
    ESP_LOGI(TAG, "rf rx: raw after a long level: %s", line);
    for (int i0 = 0; rawd && i0 < nrawd; i0 += 32) {
        char ln[300];
        int o2 = 0;
        for (int i = i0; i < nrawd && i < i0 + 32; i++) {
            o2 += snprintf(ln + o2, sizeof ln - o2, "%d ", (int)rawd[i]);
        }
        ESP_LOGI(TAG, "rf rw %d: %s", i0, ln);
        if ((i0 / 32) % 64 == 63) {
            vTaskDelay(1);  // (the log task and the USB catch up)
        }
    }
    if (rawd) {
        ESP_LOGI(TAG, "rf rw end: %d values", nrawd);
    }
    for (int k = 0; k < nseen; k++) {
        char hex[40];
        rfd_hex(&seen[k].c, hex, sizeof hex);
        ESP_LOGI(TAG, "rf rx: code %s P%d %d bits %s x%d", rfd_kind_name(seen[k].c.kind), seen[k].c.proto,
                 seen[k].c.nbits, hex, seen[k].n);
    }
}

// rf rx [seconds] [mode] [v]: the receiver module on the data pin of this board (rfpulse), the slicer and the
// decoders of RF DECODER (mode: 0 AUTO, 1 RCSWITCH, 2 GENERIC). The log: each new code (not RAW, a good fit),
// then the count of each code. v: also each packet with its level times (rf pk, rf pd);
// r: all the raw level times (rf rw) at the end, for the slicer tests on the PC
static void rf_rx(void)
{
    char *sv = strtok(NULL, " \t\r\n"), *mv = strtok(NULL, " \t\r\n");
    char *vv = strtok(NULL, " \t\r\n");
    int secs = sv ? atoi(sv) : 20, mode = mv ? atoi(mv) : RFD_MODE_AUTO;
    const bool verbose = vv && vv[0] == 'v';  // (each packet: the gaps and all its level times, for the PC tests)
    const bool rawdump = vv && vv[0] == 'r';  // (all the level times before the slicer, at the end: rf rw)
    int32_t *rawd = rawdump ? heap_caps_malloc(NRAWD * sizeof(int32_t), MALLOC_CAP_SPIRAM) : NULL;
    if (secs < 1 || secs > 300) {
        secs = 20;
    }
    rfp_src_t src;
    if (!rfp_gpio(&src)) {
        ESP_LOGE(TAG, "rf rx: this board has no data pin");
        return;
    }
    rfd_slicer_t *sl = heap_caps_calloc(1, sizeof(*sl), MALLOC_CAP_SPIRAM);
    rfd_packet_t *p = heap_caps_calloc(1, sizeof(*p), MALLOC_CAP_SPIRAM);
    int32_t *dur = heap_caps_calloc(NBUF, sizeof(int32_t), MALLOC_CAP_SPIRAM);
    rf_seen_t *seen = heap_caps_calloc(NCODE, sizeof(*seen), MALLOC_CAP_SPIRAM);
    int e = (sl && p && dur && seen) ? src.ops->open(src.ctx) : -1;
    if (e < 0) {
        ESP_LOGE(TAG, "rf rx: open failed (%d)", e);
    } else {
        const rf_rx_t x = {.src = &src,
                           .sl = sl,
                           .p = p,
                           .dur = dur,
                           .seen = seen,
                           .rawd = rawd,
                           .secs = secs,
                           .mode = mode,
                           .verbose = verbose};
        rf_rx_listen(&x);
    }
    free(rawd);
    free(sl);
    free(p);
    free(dur);
    free(seen);
}

// rf cap [pairs]: one capture
static void rf_cap(void)
{
    char *n = strtok(NULL, " \t\r\n");
    int k = n ? atoi(n) : RFN_MAX_PAIRS;
    cap(k < 1 ? 1 : k > RFN_MAX_PAIRS ? RFN_MAX_PAIRS : k);
}

// rf scan <MHz> <MHz> <step MHz> [gain]: the peak and the floor of each LO
static void rf_scan(void)
{
    char *f0 = strtok(NULL, " \t\r\n"), *f1 = strtok(NULL, " \t\r\n"), *st = strtok(NULL, " \t\r\n"),
         *g = strtok(NULL, " \t\r\n");
    if (!f0 || !f1 || !st || !buf()) {
        return;
    }
    if (g) {
        s_cfg.gain = (uint8_t)atoi(g);
    }
    float *db = W->db;
    for (double f = atof(f0); f <= atof(f1) + 1e-6; f += atof(st)) {
        s_cfg.lo_hz = (uint32_t)(f * 1e6 + 0.5);
        int64_t t0 = esp_timer_get_time();
        int e = rfn_set(&s_cfg, NULL);
        int64_t t1 = esp_timer_get_time();
        if (e == RFN_OK) {
            e = rfn_capture(s_iq, NFFT, NULL);
        }
        if (e < 0) {
            ESP_LOGE(TAG, "scan %.1f: %s", f, rfn_error(e));
            continue;
        }
        spectrum(s_iq, db);
        int bi = 0;
        for (int k = 0; k < NFFT; k++) {
            if (k != NFFT / 2 && db[k] > db[bi]) {
                bi = k;
            }
        }
        const float span = (float)rfn_rate_hz(s_cfg.rate) / 1e6f;
        ESP_LOGI(TAG, "scan %.1f MHz (tune %d us): floor %.1f, peak %.1f dBFS at %.3f MHz", f, (int)(t1 - t0),
                 median(db), db[bi], f + (bi - NFFT / 2) * span / NFFT);
    }
}

void rf_console(char *args)
{
    char *a = strtok(args, " \t\r\n");
    if (!a) {
        rf_info();
    } else if (!strcmp(a, "boot")) {
        rfn_restart(true, strtok(NULL, "\r\n"));
    } else if (!strcmp(a, "exit")) {
        rfn_restart(false, NULL);
    } else if (!strcmp(a, "open") || !strcmp(a, "set")) {
        rf_open_set(a);
    } else if (!strcmp(a, "tune")) {
        rf_tune();
    } else if (!strcmp(a, "shape")) {
        rf_shape();
    } else if (!strcmp(a, "fft")) {
        rf_fft();
    } else if (!strcmp(a, "pin") || !strcmp(a, "tx") || !strcmp(a, "probe")) {
        rf_gpio_cmd(a);
    } else if (!strcmp(a, "scope")) {
        rf_scope();
    } else if (!strcmp(a, "rx")) {
        rf_rx();
    } else if (!strcmp(a, "close")) {
        rfn_close();
    } else if (!strcmp(a, "cap")) {
        rf_cap();
    } else if (!strcmp(a, "scan")) {
        rf_scan();
    } else {
        ESP_LOGI(TAG, "rf: boot [app] | exit | open <MHz> [gain] [80|16] [20|40] | set ... | close | cap [n] | scan a "
                      "b step [gain]");
    }
}
