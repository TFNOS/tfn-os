// USB-C HEADPHONES (see usb_phones.h). Only the CoreS3 has the code; the other boards and the host tests get a
// stand-in that is always off.
//
// The parts: the USB host library (espressif/usb) in its task; the USB audio host driver (espressif/usb_host_uac) with
// its own task; the task of this file takes the events of the driver from a queue (a device comes: open it, find a
// format of 16 bits, start it; a device goes: close it). The sound: the output tap of audio.c (5 ms blocks at 24 kHz,
// mono) goes up to the rate of the device (a straight line between two samples), mono or stereo, and into the buffer of
// the driver (no wait: a full buffer drops the block).

#include "usb_phones.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#include "board.h"  // (BOARD_USB_PHONES: without it the test below is false on the CoreS3 too)
#endif

#if defined(ESP_PLATFORM) && BOARD_USB_PHONES

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"
#include "audio.h"
#include "board.h"
#include "settings.h"

static const char *TAG = "usb_phones";

typedef struct {
    int kind;
    uint8_t addr, iface;
    uac_host_device_handle_t dev;
} up_evt_t;  // kind: 1 a device, 2 it went
static QueueHandle_t s_q;
static volatile up_state_t s_state = UP_OFF;
static volatile bool s_quit;
static uac_host_device_handle_t s_dev;
static volatile bool s_play;
static uint32_t s_rate;
static uint8_t s_ch;
static char s_name[32];
static const char *s_why = "";
static TaskHandle_t s_lib_task, s_task;
static volatile bool s_cable;      // a cable gives 5 V: the task waits
static uint32_t s_writes, s_werr;  // the blocks that went to the device, and the ones that failed
static usb_host_client_handle_t
        s_cli;  // a client of our own: it sees each device (also one that the UAC driver does not take)

// the log of the events (the web API shows it: no USB console while the headphones are in)
#define EV_N 24
static char s_ev[EV_N][112];
static int s_nev;
static portMUX_TYPE s_ev_lock = portMUX_INITIALIZER_UNLOCKED;
static void ev_put(const char *b)
{
    char line[112];
    snprintf(line, sizeof line, "%lld %.80s", (long long)(esp_timer_get_time() / 1000), b);
    for (char *c = line; *c; c++) {
        if ((unsigned char)*c < 32) {
            *c = ' ';
        }
    }
    portENTER_CRITICAL(&s_ev_lock);
    if (s_nev == EV_N) {
        memmove(s_ev[0], s_ev[1], sizeof s_ev[0] * (EV_N - 1));
        s_nev--;
    }
    memcpy(s_ev[s_nev++], line, sizeof line);
    portEXIT_CRITICAL(&s_ev_lock);
}
static void ev(const char *fmt, ...)
{
    char b[88];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    ev_put(b);
    ESP_LOGI(TAG, "%s", b);
}
// the lines of the ESP log of the USB host library and of the UAC driver also go into the event log (they tell why an
// enumeration fails; the board has no USB console while the headphones are in)
static vprintf_like_t s_log_prev;
static volatile bool s_enum_fail;  // the log of the library: an enumeration failed (the task tries again)
static int log_hook(const char *fmt, va_list ap)
{
    char b[120];
    va_list ap2;
    va_copy(ap2, ap);
    vsnprintf(b, sizeof b, fmt, ap2);
    va_end(ap2);
    // (no line of this file: ev() writes it already; the ANSI colour codes go)
    if (!strstr(b, "usb_phones") && !strstr(b, "usb power out") &&
        (strstr(b, "USB") || strstr(b, "ENUM") || strstr(b, "HUB") || strstr(b, "uac") || strstr(b, "UAC") ||
         strstr(b, "usb") || strstr(b, "HCD") || strstr(b, "EXT_"))) {
        char *p = b;
        if (*p == 27) {
            while (*p && *p != 'm') {
                p++;
            }
            if (*p) {
                p++;
            }
        }
        char *e = strchr(p, 27);
        if (e) {
            *e = 0;
        }
        ev_put(p);
        if (strstr(p, "ENUM") && strstr(p, "FAILED")) {
            s_enum_fail = true;
        }
    }
    return s_log_prev ? s_log_prev(fmt, ap) : vprintf(fmt, ap);
}

bool usb_phones_supported(void)
{
    return true;
}
bool usb_phones_cable(void)
{
    return s_cable;
}
up_state_t usb_phones_state(void)
{
    return s_state;
}
const char *usb_phones_name(void)
{
    return s_state == UP_PLAYING ? s_name : "";
}
const char *usb_phones_why(void)
{
    return s_state == UP_FAILED ? s_why : "";
}

// the output tap: 24 kHz mono -> the rate and the channels of the device
static void tap(const int16_t *s, int n)
{
    if (!s_play || !s_dev) {
        return;
    }
    static int16_t out[2 * 480];
    static int16_t prev;
    static uint32_t frac;  // the place between two input samples (16.16)
    uint32_t step = (uint32_t)(((uint64_t)AUDIO_SAMPLE_RATE << 16) / s_rate);
    // the gain: the mix of audio.c comes before the volume of the speaker codec (the codec does the VOLUME of the
    // board; the mix of the CoreS3 has +6 dB already), so it is low; here the VOLUME of SETUP (0..100) gives 0 .. 3 x,
    // with the soft limit of the mixer (so the maximum volume is not too low)
    // (a VOLUME over 100 is in the mix already: the boost of audio_set_volume)
    int32_t gq8 = (int32_t)(g_settings.volume > 100 ? 100 : g_settings.volume) * 768 / 100;
    int k = 0;
    for (;;) {
        uint32_t i = frac >> 16;
        if ((int)i >= n) {
            break;
        }
        int32_t a = i == 0 ? prev : s[i - 1], b = s[i], f = (int32_t)(frac & 0xFFFF);
        int32_t m = ((a + (((b - a) * f) >> 16)) * gq8) >> 8;
        if (m > 24000) {
            m = 24000 + (m - 24000) / 4;
        }
        if (m < -24000) {
            m = -24000 + (m + 24000) / 4;
        }
        int16_t v = (int16_t)(m > 32767 ? 32767 : m < -32767 ? -32767 : m);
        if (k >= 480) {
            break;
        }
        out[s_ch * k] = v;
        if (s_ch == 2) {
            out[2 * k + 1] = v;
        }
        k++;
        frac += step;
    }
    frac -= (uint32_t)n << 16;
    prev = s[n - 1];
    if (uac_host_device_write(s_dev, (uint8_t *)out, (uint32_t)(k * s_ch * 2), 0) == ESP_OK) {
        s_writes++;
    } else {
        s_werr++;
    }
}

static void drv_cb(uint8_t addr, uint8_t iface, const uac_host_driver_event_t event, void *arg)
{
    (void)arg;
    if (event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
        up_evt_t e = {1, addr, iface, NULL};
        xQueueSend(s_q, &e, 0);
    } else if (event == UAC_HOST_DRIVER_EVENT_RX_CONNECTED) {
        up_evt_t e = {4, addr, iface, NULL};  // (a microphone part: only the log)
        xQueueSend(s_q, &e, 0);
    }
}
// our client: a new device (its address)
static void cli_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    (void)arg;
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        up_evt_t e = {3, msg->new_dev.address, 0, NULL};
        xQueueSend(s_q, &e, 0);
    }
}
// the log line of a new device: VID:PID, its class, and each interface (class.subclass.protocol); an audio interface
// with the protocol 0x20 is USB Audio Class 2 (the UAC driver takes only UAC 1)
static void dev_describe(uint8_t addr)
{
    usb_device_handle_t h;
    if (usb_host_device_open(s_cli, addr, &h) != ESP_OK) {
        ev("device %u: no open", addr);
        return;
    }
    const usb_device_desc_t *dd = NULL;
    const usb_config_desc_t *cd = NULL;
    usb_device_info_t di;
    char b[64] = "";
    int k = 0, uac1 = 0, uac2 = 0;
    if (usb_host_get_device_descriptor(h, &dd) == ESP_OK && usb_host_get_active_config_descriptor(h, &cd) == ESP_OK &&
        cd) {
        const uint8_t *p = (const uint8_t *)cd, *end = p + cd->wTotalLength;
        while (p + 2 <= end && p[0] >= 2) {
            if (p[1] == 4 && p + 9 <= end) {  // an interface descriptor
                if (p[5] == 1 && p[6] == 2) {
                    if (p[7] == 0x20) {
                        uac2++;
                    } else {
                        uac1++;
                    }
                }  // audio streaming
                if (k < (int)sizeof b - 12 && p[3] == 0) {
                    k += snprintf(b + k, sizeof b - k, " %02x.%02x.%02x", p[5], p[6], p[7]);
                }
            }
            p += p[0];
        }
        int sp = usb_host_device_info(h, &di) == ESP_OK ? di.speed : -1;
        ev("device %u: %04x:%04x speed %d, streaming UAC1 %d UAC2 %d;%s", addr, dd->idVendor, dd->idProduct, sp, uac1,
           uac2, b);
    } else {
        ev("device %u: no descriptors", addr);
    }
    usb_host_device_close(s_cli, h);
}
static void dev_cb(uac_host_device_handle_t dev, const uac_host_device_event_t ev, void *arg)
{
    (void)arg;
    if (ev == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        up_evt_t e = {2, 0, 0, dev};
        xQueueSend(s_q, &e, 0);
    }
}

// a format of 16 bits, mono or stereo; the rate: 48 kHz, 24 kHz, 44.1 kHz or 32 kHz if the device has it, else its
// first rate
static bool pick(uac_host_device_handle_t dev, int nalt, uint8_t *ch, uint32_t *rate)
{
    static const uint32_t WANT[] = {48000, 24000, 44100, 32000};
    for (int w = 0; w <= 4; w++) {
        for (int alt = 1; alt <= nalt; alt++) {
            uac_host_dev_alt_param_t p;
            if (uac_host_get_device_alt_param(dev, (uint8_t)alt, &p) != ESP_OK) {
                continue;
            }
            if (p.bit_resolution != 16 || p.channels < 1 || p.channels > 2) {
                continue;
            }
            if (w == 4) {
                *ch = p.channels;
                *rate = p.sample_freq_type ? p.sample_freq[0] : p.sample_freq_lower;
                return *rate >= 8000;
            }
            bool has = false;
            if (p.sample_freq_type) {
                for (int i = 0; i < p.sample_freq_type && i < UAC_FREQ_NUM_MAX; i++) {
                    has |= p.sample_freq[i] == WANT[w];
                }
            } else {
                has = WANT[w] >= p.sample_freq_lower && WANT[w] <= p.sample_freq_upper;
            }
            if (has) {
                *ch = p.channels;
                *rate = WANT[w];
                return true;
            }
        }
    }
    return false;
}

static void dev_open(uint8_t addr, uint8_t iface)
{
    if (s_dev) {
        return;  // (one device)
    }
    uac_host_device_config_t dc = {.addr = addr,
                                   .iface_num = iface,
                                   .buffer_size = 16000,
                                   .buffer_threshold = 4000,
                                   .callback = dev_cb,
                                   .callback_arg = NULL};
    uac_host_device_handle_t dev = NULL;
    if (uac_host_device_open(&dc, &dev) != ESP_OK) {
        ev("UAC open failed (addr %u iface %u)", addr, iface);
        return;
    }
    uac_host_dev_info_t info;
    uint8_t ch = 2;
    uint32_t rate = 48000;
    if (uac_host_get_device_info(dev, &info) != ESP_OK || !pick(dev, info.iface_alt_num, &ch, &rate)) {
        ev("the device has no format of 16 bits (%d alt settings)", (int)info.iface_alt_num);
        uac_host_device_close(dev);
        s_why = "FORMAT";
        s_state = UP_FAILED;
        return;
    }
    int j = 0;
    for (int i = 0; i < UAC_STR_DESC_MAX_LENGTH && info.iProduct[i] && j < (int)sizeof(s_name) - 1; i++) {
        s_name[j++] = info.iProduct[i] < 128 ? (char)info.iProduct[i] : '?';
    }
    s_name[j] = 0;
    uac_host_stream_config_t sc = {.channels = ch, .bit_resolution = 16, .sample_freq = rate, .flags = 0};
    if (uac_host_device_start(dev, &sc) != ESP_OK) {
        ev("start failed (%lu Hz, %u channels)", (unsigned long)rate, ch);
        uac_host_device_close(dev);
        s_why = "FORMAT";
        s_state = UP_FAILED;
        return;
    }
    uac_host_device_set_volume(dev, 100);  // (the board does the volume: the gain in tap())
    s_rate = rate;
    s_ch = ch;
    s_dev = dev;
    s_play = true;
    s_state = UP_PLAYING;
    audio_out_tap(tap);
    audio_speaker_off(true);
    s_writes = s_werr = 0;
    ev("playing on \"%s\": %lu Hz, %u channels", s_name, (unsigned long)rate, ch);
}

static void dev_gone(uac_host_device_handle_t dev)
{
    if (dev == s_dev) {
        s_play = false;
        audio_out_tap(NULL);
        audio_speaker_off(false);
        s_dev = NULL;
        if (s_state == UP_PLAYING) {
            s_state = UP_WAIT;
        }
        ev("the device went: the speaker again");
    }
    uac_host_device_close(dev);
}

static void lib_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t fl = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(200), &fl);
        if (s_quit && (fl & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)) {
            usb_host_device_free_all();
        }
        if (s_quit && (fl & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) {
            break;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    usb_host_uninstall();
    // the PHY goes back to the USB Serial/JTAG controller: the console port comes back
#if CONFIG_IDF_TARGET_ESP32S3
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
#endif
    s_lib_task = NULL;
    vTaskDelete(NULL);
}

static volatile bool s_again;  // a cable came while the host ran: stop the host, then wait for the cable to go
static void phones_task(void *arg)
{
    (void)arg;
again:
    s_again = false;
    // (our own 5 V can stay on after a restart of the board, and the power chip sees it as a cable: off first)
    board_usb_power_out(false);
    vTaskDelay(pdMS_TO_TICKS(1500));
    // a cable gives 5 V: wait until it goes out (the console stays; then the headphones get 5 V)
    bool told = false;
    while (!s_quit && !board_usb_power_out(true)) {
        if (!told) {
            ev("a cable gives 5 V: waiting until it goes out");
            told = true;
        }
        s_cable = true;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    s_cable = false;
    if (s_quit) {
        s_state = UP_OFF;
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ev("5 V out on; the USB host starts");
    if (!s_log_prev) {
        s_log_prev = esp_log_set_vprintf(log_hook);
    }
    esp_log_level_set("USBH", ESP_LOG_DEBUG);  // (the steps of the enumeration)
    esp_log_level_set("ENUM", ESP_LOG_DEBUG);
    esp_log_level_set("HUB", ESP_LOG_DEBUG);
    usb_host_config_t hc = {.skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LOWMED};
    if (usb_host_install(&hc) != ESP_OK) {
        ESP_LOGE(TAG, "the USB host did not start");
        s_why = "HOST";
        s_state = UP_FAILED;
        goto out;
    }
    // the internal PHY (the pins of the USB-C) to the USB-OTG controller. After usb_host_install the PHY
    // is still mapped to the USB-Serial-JTAG (RTC_CNTL sw_usb_phy_sel 0): the host sees the idle lines of the external
    // PHY as a full-speed device, and the first transfer fails (ENUM CHECK_SHORT_DEV_DESC, status 1)
#if CONFIG_IDF_TARGET_ESP32S3
    usb_serial_jtag_ll_phy_enable_external(true);
#endif
    ev("the PHY to the USB host (rtc usb_conf %08lx)", (unsigned long)(*(volatile uint32_t *)0x60008120));
    xTaskCreatePinnedToCore(lib_task, "usbh_lib", 4096, NULL, 5, &s_lib_task, 0);
    uac_host_driver_config_t uc = {.create_background_task = true,
                                   .task_priority = 5,
                                   .stack_size = 4096,
                                   .core_id = 0,
                                   .callback = drv_cb,
                                   .callback_arg = NULL};
    if (uac_host_install(&uc) != ESP_OK) {
        ESP_LOGE(TAG, "the UAC driver did not start");
        s_why = "HOST";
        s_state = UP_FAILED;
        s_quit = true;
        goto out;
    }
    usb_host_client_config_t cc = {.is_synchronous = false,
                                   .max_num_event_msg = 5,
                                   .async = {.client_event_callback = cli_cb, .callback_arg = NULL}};
    if (usb_host_client_register(&cc, &s_cli) != ESP_OK) {
        s_cli = NULL;
        ev("no client of our own (no device log)");
    }
    s_state = UP_WAIT;
    ev("waiting for a USB audio device");
    int tries = 0;
    s_enum_fail = false;
    while (!s_quit) {
        // an enumeration failed (the first descriptor, a transfer error): power the headphones off and on, then the
        // library enumerates them again (some devices need time after the power comes)
        if (s_enum_fail && !s_dev && tries < 5) {
            s_enum_fail = false;
            tries++;
            ev("enumeration failed: power off and on (%d of 5)", tries);
            usb_host_lib_set_root_port_power(false);
            board_usb_power_out(false);
            vTaskDelay(pdMS_TO_TICKS(1000));
            // (the 5 V of our own output takes a moment to go: the check of a cable says "VBUS" until then)
            bool on = false;
            for (int w = 0; w < 15 && !on && !s_quit; w++) {
                on = board_usb_power_out(true);
                if (!on) {
                    vTaskDelay(pdMS_TO_TICKS(300));
                }
            }
            if (!on) {
                // (5 V from outside: a PC or a charger cable. Two hosts cannot talk: the host stops, then waits)
                ev("a cable is in: the host stops until it goes out");
                s_again = true;
                s_quit = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(300));
            usb_host_lib_set_root_port_power(true);
        }
        if (s_cli) {
            usb_host_client_handle_events(s_cli, 0);
        }
        up_evt_t e;
        if (xQueueReceive(s_q, &e, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        if (e.kind == 1) {
            ev("UAC: a speaker part (addr %u iface %u)", e.addr, e.iface);
            dev_open(e.addr, e.iface);
        } else if (e.kind == 3) {
            dev_describe(e.addr);
        } else if (e.kind == 4) {
            ev("UAC: a microphone part (addr %u iface %u)", e.addr, e.iface);
        } else {
            dev_gone(e.dev);
        }
    }
    if (s_dev) {
        dev_gone(s_dev);
    }
    if (s_cli) {
        usb_host_client_deregister(s_cli);
        s_cli = NULL;
    }
    uac_host_uninstall();
out:
    while (s_lib_task) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    board_usb_power_out(false);
    if (s_again) {
        s_quit = false;
        s_state = UP_WAIT;
        goto again;
    }
    if (s_state != UP_FAILED) {
        s_state = UP_OFF;
    }
    ESP_LOGI(TAG, "closed");
    s_task = NULL;
    vTaskDelete(NULL);
}

void usb_phones_open(void)
{
    if (s_task) {
        return;
    }
    if (!s_q) {
        s_q = xQueueCreate(8, sizeof(up_evt_t));
    }
    s_quit = false;
    // (no 5 V out while a cable gives 5 V: the task waits for the cable to go out; then the USB host)
    s_why = "";
    s_state = UP_WAIT;
    xTaskCreatePinnedToCore(phones_task, "usb_phones", 4096, NULL, 4, &s_task, 0);
}

void usb_phones_close(void)
{
    if (!s_task) {
        return;
    }
    s_again = false;  // (a close is the end: no new start after a cable)
    s_quit = true;
    for (int i = 0; i < 150 && s_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

int usb_phones_report(char *out, int max)
{
    static const char *const ST[] = {"off", "wait", "playing", "failed"};
    int k = snprintf(out, max,
                     "{\"state\":\"%s\",\"why\":\"%s\",\"cable\":%s,\"name\":\"%s\",\"rate\":%lu,\"ch\":%u,"
                     "\"writes\":%lu,\"errors\":%lu,",
                     ST[s_state], s_why, s_cable ? "true" : "false", s_name, (unsigned long)s_rate, s_ch,
                     (unsigned long)s_writes, (unsigned long)s_werr);
    // the raw registers now (a device that the host does not see: is the core a host, is the port powered, does it see
    // a connection, the line state of D+ and D-): GOTGCTL, GUSBCFG, GINTSTS, HPRT of the OTG core (0x60080000); the
    // pad and the pull-downs (USB_WRAP_OTG_CONF); the PHY select (RTC_CNTL_USB_CONF); USB_SERIAL_JTAG_CONF0
#define RD(a) (*(volatile uint32_t *)(a))
    uint32_t hprt = RD(0x60080440);
    k += snprintf(out + k, max - k,
                  "\"gotgctl\":\"%08lx\",\"gusbcfg\":\"%08lx\",\"gintsts\":\"%08lx\",\"hprt\":\"%08lx\","
                  "\"port\":{\"conn\":%lu,\"ena\":%lu,\"pwr\":%lu,\"line\":%lu,\"speed\":%lu},\"wrap\":\"%08lx\",\"rtc_"
                  "usb\":\"%08lx\","
                  "\"sj_conf0\":\"%08lx\",",
                  (unsigned long)RD(0x60080000), (unsigned long)RD(0x6008000C), (unsigned long)RD(0x60080014),
                  (unsigned long)hprt, (unsigned long)(hprt & 1), (unsigned long)((hprt >> 2) & 1),
                  (unsigned long)((hprt >> 12) & 1), (unsigned long)((hprt >> 10) & 3),
                  (unsigned long)((hprt >> 17) & 3), (unsigned long)RD(0x60039000), (unsigned long)RD(0x60008120),
                  (unsigned long)RD(0x60038018));
#undef RD
    k += snprintf(out + k, max - k, "\"log\":[");
    for (int i = 0; i < s_nev && k < max - 80; i++) {
        k += snprintf(out + k, max - k, "%s\"", i ? "," : "");
        for (const char *c = s_ev[i]; *c && k < max - 8; c++) {
            out[k++] = *c == '"' || *c == '\\' ? '\'' : *c;
        }
        k += snprintf(out + k, max - k, "\"");
    }
    k += snprintf(out + k, max - k, "]}");
    return k < max ? k : max - 1;
}

#else  // the other boards and the host tests: no USB host

#include <stdio.h>
#include "board.h"

bool usb_phones_supported(void)
{
    return false;
}
void usb_phones_open(void)
{
}
void usb_phones_close(void)
{
}
up_state_t usb_phones_state(void)
{
    return UP_OFF;
}
const char *usb_phones_name(void)
{
    return "";
}
const char *usb_phones_why(void)
{
    return "";
}
// (the boards with no 5 V out on the USB-C)
__attribute__((weak)) bool board_usb_power_out(bool on)
{
    (void)on;
    return false;
}

bool usb_phones_cable(void)
{
    return false;
}
int usb_phones_report(char *out, int max)
{
    return snprintf(out, max, "{\"state\":\"none\"}");
}

#endif
