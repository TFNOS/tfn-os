// USB MIDI on TinyUSB (see usb_midi.h). The host tests get a small stand-in (no USB).

#include <string.h>
#include "esp_timer.h"
#include "usb_midi.h"

static volatile bool s_open, s_failed;
static uint32_t s_sent, s_rx;
static int64_t s_clk_last_us;
static float s_clk_tick_us;  // filtered time between clock ticks (24 a beat)
static uint32_t s_starts;

static void rx_byte(uint8_t st)
{
    int64_t now = esp_timer_get_time();
    s_rx++;
    if (st == 0xF8) {
        int64_t d = now - s_clk_last_us;
        if (s_clk_last_us && d > 2000 && d < 200000) {
            s_clk_tick_us = s_clk_tick_us > 0 ? s_clk_tick_us + 0.1f * ((float)d - s_clk_tick_us) : (float)d;
        }
        s_clk_last_us = now;
    } else if (st == 0xFA || st == 0xFB) {
        s_starts++;
    }
}

float usb_midi_clock_bpm(void)
{
    int64_t now = esp_timer_get_time();
    if (!s_clk_last_us || now - s_clk_last_us > 1000000 || s_clk_tick_us <= 0) {
        return 0;
    }
    return 60e6f / (s_clk_tick_us * 24.0f);
}

uint32_t usb_midi_starts(void)
{
    return s_starts;
}
uint32_t usb_midi_sent(void)
{
    return s_sent;
}
uint32_t usb_midi_received(void)
{
    return s_rx;
}

#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "sdkconfig.h"  // (CONFIG_IDF_TARGET_ESP32S3: the PHY switch below)
#include "hal/usb_serial_jtag_ll.h"

static const char *TAG = "usb_midi";

enum {
    ITF_MIDI = 0,
    ITF_MIDI_STREAMING,
    ITF_COUNT
};
#define EP_OUT  0x01
#define EP_IN   0x81
#define CFG_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const tusb_desc_device_t DEV = {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor = 0x303A,   // Espressif
        .idProduct = 0x4008,  // the TinyUSB "MIDI only" number of esp_tinyusb
        .bcdDevice = 0x0100,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1,
};
static const uint8_t CFG[] = {
        TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CFG_LEN, 0, 100),
        TUD_MIDI_DESCRIPTOR(ITF_MIDI, 4, EP_OUT, EP_IN, 64),
};
static const char *STR[] = {
        (const char[]){0x09, 0x04},  // English
        "TFN",
        "TFN MIDI",
        "ARC001",
        "TFN MIDI",
};

void usb_midi_open(void)
{
    if (s_open) {
        return;
    }
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.device = &DEV;
    cfg.descriptor.full_speed_config = CFG;
    cfg.descriptor.string = STR;
    cfg.descriptor.string_count = sizeof(STR) / sizeof(STR[0]);
    esp_err_t e = tinyusb_driver_install(&cfg);
    s_failed = e != ESP_OK;
    s_open = !s_failed;
    if (s_failed) {
        ESP_LOGE(TAG, "TinyUSB did not start: %s", esp_err_to_name(e));
    }
}

void usb_midi_close(void)
{
    if (!s_open) {
        return;
    }
    s_open = false;
    tinyusb_driver_uninstall();
    // the PHY goes back to the USB Serial/JTAG controller: the console port comes back
#if CONFIG_IDF_TARGET_ESP32S3
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
#endif
}

um_state_t usb_midi_state(void)
{
    if (s_failed) {
        return UM_FAILED;
    }
    if (!s_open) {
        return UM_OFF;
    }
    return tud_mounted() ? UM_LINKED : UM_WAIT;
}

bool usb_midi_send(const uint8_t *msg, int len)
{
    if (!s_open || !tud_mounted()) {
        return false;
    }
    if (tud_midi_stream_write(0, msg, (uint32_t)len) != (uint32_t)len) {
        return false;
    }
    s_sent++;
    return true;
}

void usb_midi_poll(void)
{
    if (!s_open || !tud_mounted()) {
        return;
    }
    uint8_t p[4];
    while (tud_midi_available() && tud_midi_packet_read(p)) {
        // p[0]: the cable and the code index; p[1..3]: the message
        rx_byte(p[1]);
    }
}

#else  // the host tests

static bool s_linked_host = true;

void usb_midi_open(void)
{
    s_open = true;
}
void usb_midi_close(void)
{
    s_open = false;
}
um_state_t usb_midi_state(void)
{
    return !s_open ? UM_OFF : s_linked_host ? UM_LINKED : UM_WAIT;
}
bool usb_midi_send(const uint8_t *msg, int len)
{
    (void)msg;
    if (!s_open || len < 1) {
        return false;
    }
    s_sent++;
    return true;
}
void usb_midi_poll(void)
{
    (void)rx_byte;
}

#endif
