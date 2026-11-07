// USB AUDIO on TinyUSB (see usb_audio.h). The host tests get a small stand-in (no USB).
//
// The path of the sound: the microphone task (10 ms blocks) and the audio task (5 ms blocks) give mono samples at
// AUDIO_SAMPLE_RATE (24 kHz). The source that is on gives the pace: with the sound of the board, the audio task (the
// microphone samples wait in a small ring and the board block mixes them in); with only the microphone, the microphone
// task. Each block goes up to 48 kHz (a sample between each two) and into the FIFO of the IN endpoint; TinyUSB sends
// 48 samples in each 1 ms frame of the USB bus.

#include <math.h>
#include <string.h>
#include "audio.h"
#include "settings.h"
#include "usb_audio.h"
#include "usb_phones.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"  // (EXT_RAM_BSS_ATTR: the buffers in PSRAM: the BLE start needs the internal RAM)
#else
#define EXT_RAM_BSS_ATTR
#endif

#define UA_RATE 48000
#define UA_RING 1024  // the microphone ring (24 kHz samples, a power of 2: 42 ms)

static volatile bool s_open, s_failed, s_stream;
static volatile int s_src;
static volatile float s_level;
static volatile uint32_t s_sent, s_dropped;
static int16_t s_ring[UA_RING] EXT_RAM_BSS_ATTR;
static volatile uint32_t s_rw, s_rr;  // the microphone ring: write, read
static int16_t s_last;                // the last sample of the last block (the step up to 48 kHz)

int usb_audio_src(void)
{
    return s_src;
}
bool usb_audio_streaming(void)
{
    return s_open && s_stream;
}
float usb_audio_level(void)
{
    return s_open ? s_level : 0;
}
uint32_t usb_audio_sent(void)
{
    return s_sent;
}
uint32_t usb_audio_dropped(void)
{
    return s_dropped;
}
int usb_audio_mode(void)
{
    int m = (int)store_get("snd_usb", 0);
    return m == 2 && !usb_phones_supported() ? 0 : m > 2 ? 0 : m;
}
bool usb_audio_setting(void)
{
    return usb_audio_mode() == 1;
}

static uint16_t ua_write(const int16_t *s, int n);
static bool ua_ready(void);
static bool ua_mounted(void);

// one block at 24 kHz to the computer: up to 48 kHz, the level, the FIFO
static void send_block(const int16_t *s, int n)
{
    static int16_t up[2 * 240] EXT_RAM_BSS_ATTR;
    if (n > 240) {
        n = 240;
    }
    int64_t sum = 0;
    int16_t prev = s_last;
    for (int i = 0; i < n; i++) {
        up[2 * i] = (int16_t)(((int32_t)prev + s[i]) / 2);
        up[2 * i + 1] = s[i];
        prev = s[i];
        sum += (int32_t)s[i] * s[i];
    }
    s_last = prev;
    s_level = sqrtf((float)sum / (n > 0 ? n : 1)) / 32768.0f;
    if (!ua_ready()) {
        return;
    }
    uint16_t w = ua_write(up, 2 * n);
    s_sent += w / 2;
    if (w < 4 * n) {
        s_dropped += (uint32_t)(2 * n - w / 2);
    }
}

// the microphone task: alone, it sends; with the sound of the board, it fills the ring
static void mic_cb(const int16_t *s, int n)
{
    int src = s_src;
    if (!(src & UA_SRC_MIC)) {
        return;
    }
    if (!(src & UA_SRC_BOARD)) {
        send_block(s, n);
        return;
    }
    uint32_t w = s_rw;
    for (int i = 0; i < n; i++) {
        s_ring[(w + i) & (UA_RING - 1)] = s[i];
    }
    s_rw = w + (uint32_t)n;
    if (s_rw - s_rr > UA_RING) {
        s_rr = s_rw - UA_RING;  // (a full ring: the old samples go)
    }
}

// the audio task: the sound of the board (and the microphone from the ring)
static void board_cb(const int16_t *s, int n)
{
    int src = s_src;
    if (!(src & UA_SRC_BOARD)) {
        return;
    }
    if (s_stream && !ua_mounted()) {
        s_stream = false;
        audio_speaker_off(false);
    }  // (the cable went out)
    static int16_t mix[240] EXT_RAM_BSS_ATTR;
    if (n > 240) {
        n = 240;
    }
    if (src & UA_SRC_MIC) {
        uint32_t r = s_rr, avail = s_rw - r;
        for (int i = 0; i < n; i++) {
            int32_t m = s[i];
            if (avail > 0) {
                m += s_ring[r & (UA_RING - 1)];
                r++;
                avail--;
            }
            mix[i] = (int16_t)(m > 32767 ? 32767 : m < -32767 ? -32767 : m);
        }
        s_rr = r;
    } else {
        memcpy(mix, s, (size_t)n * 2);
    }
    send_block(mix, n);
}

// the taps of the audio module follow the source; the speaker is quiet while the computer reads the board's sound
static void taps_update(void)
{
    int src = s_open ? s_src : 0;
    if (src & UA_SRC_MIC) {
        audio_mic_start();
    }
    audio_mic_tap((src & UA_SRC_MIC) ? mic_cb : NULL);
    audio_out_tap((src & UA_SRC_BOARD) ? board_cb : NULL);
    audio_speaker_off((src & UA_SRC_BOARD) && s_stream);
}

void usb_audio_set_src(int src)
{
    int old = s_src;
    s_src = src & (UA_SRC_MIC | UA_SRC_BOARD);
    if ((old & UA_SRC_MIC) && !(s_src & UA_SRC_MIC)) {
        audio_mic_stop();
    }
    s_rr = s_rw;
    taps_update();
}

void usb_audio_set_mode(int mode)
{
    store_set("snd_usb", (uint32_t)mode);
    mode = usb_audio_mode();
    if (mode != 2) {
        usb_phones_close();
    }
    if (mode == 1 && !s_open) {
        usb_audio_open(UA_SRC_BOARD);
    } else if (mode == 1) {
        usb_audio_set_src(s_src | UA_SRC_BOARD);
    } else if (s_open) {
        usb_audio_close();  // (mode 2: the close opens the headphones)
    } else if (mode == 2) {
        usb_phones_open();
    }
}

void usb_audio_boot(void)
{
    int m = usb_audio_mode();
    if (m == 1) {
        usb_audio_open(UA_SRC_BOARD);
    } else if (m == 2) {
        usb_phones_open();
    }
}

#ifdef ESP_PLATFORM

#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "sdkconfig.h"  // (CONFIG_IDF_TARGET_ESP32S3: the PHY switch below)
#include "hal/usb_serial_jtag_ll.h"

static const char *TAG = "usb_audio";

// the size of the IN endpoint: 49 samples of 2 bytes (48 kHz: 48 in a frame, one more for the clock drift); the
// compile definitions of the tinyusb component (main/CMakeLists.txt) give the same numbers to the driver
#define EP_SZ 98
#define EP_IN 0x81
enum {
    ITF_AC = 0,
    ITF_AS,
    ITF_COUNT
};
#define CFG_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO20_MIC_ONE_CH_DESC_LEN)

static const tusb_desc_device_t DEV = {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = 0x0200,
        // (an IAD: the class is in the interface association)
        .bDeviceClass = TUSB_CLASS_MISC,
        .bDeviceSubClass = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor = 0x303A,   // Espressif
        .idProduct = 0x4010,  // (an Espressif test number: TinyUSB audio)
        .bcdDevice = 0x0100,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1,
};
static const uint8_t CFG[] = {
        TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CFG_LEN, 0, 100),
        TUD_AUDIO20_MIC_ONE_CH_DESCRIPTOR(ITF_AC, 4, 2, 16, EP_IN, EP_SZ),
};
static const char *STR[] = {
        (const char[]){0x09, 0x04},  // English
        "TFN",
        "TFN SOUND",
        "ARC002",
        "TFN SOUND",
};

// the controls that the computer reads and sets (the volume and the mute have no effect: the sound is the board's)
static bool s_mute[2];
static int16_t s_vol[2];
static uint32_t s_freq = UA_RATE;
static uint8_t s_clk_valid = 1;

static bool ua_mounted(void)
{
    return tud_mounted();
}
static bool ua_ready(void)
{
    return s_open && tud_mounted() && s_stream;
}
static uint16_t ua_write(const int16_t *s, int n)
{
    return tud_audio_write(s, (uint16_t)(n * 2));
}

void usb_audio_open(int src)
{
    if (s_open) {
        usb_audio_set_src(src);
        return;
    }
    usb_phones_close();  // (one user of the USB PHY at a time)
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.device = &DEV;
    cfg.descriptor.full_speed_config = CFG;
    cfg.descriptor.string = STR;
    cfg.descriptor.string_count = sizeof(STR) / sizeof(STR[0]);
    esp_err_t e = tinyusb_driver_install(&cfg);
    s_failed = e != ESP_OK;
    s_open = !s_failed;
    s_stream = false;
    if (s_failed) {
        ESP_LOGE(TAG, "TinyUSB did not start: %s", esp_err_to_name(e));
        return;
    }
    ESP_LOGI(TAG, "open: source %d (the console port goes away)", src);
    usb_audio_set_src(src);
}

void usb_audio_close(void)
{
    if (!s_open) {
        return;
    }
    s_open = false;
    s_stream = false;
    usb_audio_set_src(0);
    tinyusb_driver_uninstall();
    // the PHY goes back to the USB Serial/JTAG controller: the console port comes back
#if CONFIG_IDF_TARGET_ESP32S3
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
#endif
    ESP_LOGI(TAG, "closed: sent %lu, dropped %lu", (unsigned long)s_sent, (unsigned long)s_dropped);
    if (usb_audio_mode() == 2) {
        usb_phones_open();
    }
}

ua_state_t usb_audio_state(void)
{
    if (s_failed) {
        return UA_FAILED;
    }
    if (!s_open) {
        return UA_OFF;
    }
    return tud_mounted() ? UA_LINKED : UA_WAIT;
}

// ---- the callbacks of the TinyUSB audio class (UAC2: 1 the microphone terminal, 2 the feature unit, 4 the clock)

// the computer starts (alt 1) or stops (alt 0) the stream
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *req)
{
    (void)rhport;
    uint8_t itf = TU_U16_LOW(req->wIndex), alt = TU_U16_LOW(req->wValue);
    if (itf == ITF_AS) {
        s_stream = alt != 0;
        taps_update();
    }
    return true;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *req)
{
    (void)rhport;
    if (TU_U16_LOW(req->wIndex) == ITF_AS) {
        s_stream = false;
        taps_update();
    }
    return true;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *req, uint8_t *buf)
{
    (void)rhport;
    uint8_t ch = TU_U16_LOW(req->wValue), sel = TU_U16_HIGH(req->wValue), ent = TU_U16_HIGH(req->wIndex);
    TU_VERIFY(req->bRequest == AUDIO20_CS_REQ_CUR);
    if (ent != 2 || ch > 1) {
        return false;
    }
    if (sel == AUDIO20_FU_CTRL_MUTE && req->wLength == sizeof(audio20_control_cur_1_t)) {
        s_mute[ch] = ((audio20_control_cur_1_t *)buf)->bCur;
        return true;
    }
    if (sel == AUDIO20_FU_CTRL_VOLUME && req->wLength == sizeof(audio20_control_cur_2_t)) {
        s_vol[ch] = (int16_t)((audio20_control_cur_2_t *)buf)->bCur;
        return true;
    }
    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *req)
{
    uint8_t ch = TU_U16_LOW(req->wValue), sel = TU_U16_HIGH(req->wValue), ent = TU_U16_HIGH(req->wIndex);
    if (ent == 1 && sel == AUDIO20_TE_CTRL_CONNECTOR) {
        audio20_desc_channel_cluster_t c = {
                .bNrChannels = 1, .bmChannelConfig = (audio20_channel_config_t)0, .iChannelNames = 0};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &c, sizeof(c));
    }
    if (ent == 2 && ch <= 1) {
        if (sel == AUDIO20_FU_CTRL_MUTE) {
            return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &s_mute[ch], 1);
        }
        if (sel == AUDIO20_FU_CTRL_VOLUME) {
            if (req->bRequest == AUDIO20_CS_REQ_CUR) {
                return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &s_vol[ch], sizeof(s_vol[ch]));
            }
            if (req->bRequest == AUDIO20_CS_REQ_RANGE) {
                audio20_control_range_2_n_t(1)
                        r = {.wNumSubRanges = 1, .subrange[0] = {.bMin = -90 * 256, .bMax = 0, .bRes = 256}};
                return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &r, sizeof(r));
            }
        }
        return false;
    }
    if (ent == 4) {
        if (sel == AUDIO20_CS_CTRL_SAM_FREQ) {
            if (req->bRequest == AUDIO20_CS_REQ_CUR) {
                return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &s_freq, sizeof(s_freq));
            }
            if (req->bRequest == AUDIO20_CS_REQ_RANGE) {
                audio20_control_range_4_n_t(1)
                        r = {.wNumSubRanges = 1, .subrange[0] = {.bMin = UA_RATE, .bMax = UA_RATE, .bRes = 0}};
                return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &r, sizeof(r));
            }
        }
        if (sel == AUDIO20_CS_CTRL_CLK_VALID) {
            return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &s_clk_valid, 1);
        }
    }
    return false;
}

#else  // the host tests: a computer that reads all the sound

static bool ua_mounted(void)
{
    return s_open;
}
static bool ua_ready(void)
{
    return s_open && s_stream;
}
static uint16_t ua_write(const int16_t *s, int n)
{
    (void)s;
    return (uint16_t)(n * 2);
}

void usb_audio_open(int src)
{
    if (!s_open) {
        s_open = true;
        s_stream = true;
    }
    usb_audio_set_src(src);
}
void usb_audio_close(void)
{
    if (!s_open) {
        return;
    }
    s_open = false;
    s_stream = false;
    usb_audio_set_src(0);
}
ua_state_t usb_audio_state(void)
{
    return s_open ? UA_LINKED : UA_OFF;
}

#endif
