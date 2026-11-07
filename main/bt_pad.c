// The board as a Bluetooth LE gamepad, keyboard, media keys and mouse (see bt_pad.h). The HID part comes from the
// ESP-IDF esp_hid component (NimBLE host); this file starts the stack, advertises,
// keeps the connection state and sends the reports. The same stack also holds the
// BLE-MIDI service (ble_midi.c): the MIDI app switches the advertising to MIDI.

#include "sdkconfig.h"
#if !CONFIG_BT_ENABLED
// ---------------------------------------------------------------- no Bluetooth (the Tab5: the ESP32-P4 has no radio)
#include "bt_pad.h"
bool bt_pad_start(void)
{
    return false;
}
void bt_pad_stop(void)
{
}
void bt_pad_tick(int64_t now_ms)
{
    (void)now_ms;
}
pad_state_t bt_pad_state(void)
{
    return PAD_FAILED;
}
const char *bt_pad_name(void)
{
    return "NO BLUETOOTH";
}
void bt_pad_set_name(const char *name)
{
    (void)name;
}
void bt_pad_send(const pad_report_t *r)
{
    (void)r;
}
uint32_t bt_pad_sent(void)
{
    return 0;
}
void bt_pad_forget(void)
{
}
void bt_kbd_report(uint8_t mods, const uint8_t keys[6])
{
    (void)mods;
    (void)keys;
}
void bt_kbd_tap(uint8_t mods, uint8_t key)
{
    (void)mods;
    (void)key;
}
int bt_kbd_type(const char *text)
{
    (void)text;
    return 0;
}
uint8_t bt_kbd_leds(void)
{
    return 0;
}
void bt_media_tap(uint16_t usage)
{
    (void)usage;
}
void bt_mouse_move(int dx, int dy, int wheel)
{
    (void)dx;
    (void)dy;
    (void)wheel;
}
void bt_mouse_buttons(uint8_t buttons)
{
    (void)buttons;
}
bool bt_pad_boot(void)
{
    return false;
}
void bt_pad_adv_midi(bool on)
{
    (void)on;
}
const char *bt_pad_midi_name(void)
{
    return "TFN MIDI";
}
#else
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
#include "settings.h"
#include "esp_attr.h"  // (EXT_RAM_BSS_ATTR: the ring of the reports in PSRAM)
#include "bt_pad.h"
#include "ble_midi.h"
#include "app.h"         // (app_current: the app that owns the stack)
#include "pad_config.h"  // (pad_config_keep_bt_name: the saved name)

static const char *TAG = "pad";

#define PAD_NAME      "TFN PAD"
#define REPORT_ID     1  // the gamepad
#define REPORT_KBD    2  // the keyboard (BT KEYS)
#define REPORT_MEDIA  3  // the media keys
#define REPORT_MOUSE  4  // the mouse
#define REPORT_LEN    7
#define REPEAT_US     200000  // send the state again after this time with no change

// A game pad: 16 buttons, a hat switch (4 bits + 4 bits padding), X, Y, Z, Rz (8 bits each).
static const uint8_t REPORT_MAP[] = {
        0x05,
        0x01,  // Usage Page (Generic Desktop)
        0x09,
        0x05,  // Usage (Game Pad)
        0xA1,
        0x01,  // Collection (Application)
        0x85,
        REPORT_ID,  //   Report ID
        0x05,
        0x09,  //   Usage Page (Button)
        0x19,
        0x01,  //   Usage Minimum (1)
        0x29,
        0x10,  //   Usage Maximum (16)
        0x15,
        0x00,  //   Logical Minimum (0)
        0x25,
        0x01,  //   Logical Maximum (1)
        0x75,
        0x01,  //   Report Size (1)
        0x95,
        0x10,  //   Report Count (16)
        0x81,
        0x02,  //   Input (Data, Var, Abs)
        0x05,
        0x01,  //   Usage Page (Generic Desktop)
        0x09,
        0x39,  //   Usage (Hat switch)
        0x15,
        0x00,  //   Logical Minimum (0)
        0x25,
        0x07,  //   Logical Maximum (7)
        0x35,
        0x00,  //   Physical Minimum (0)
        0x46,
        0x3B,
        0x01,  //   Physical Maximum (315)
        0x65,
        0x14,  //   Unit (degrees)
        0x75,
        0x04,  //   Report Size (4)
        0x95,
        0x01,  //   Report Count (1)
        0x81,
        0x42,  //   Input (Data, Var, Abs, Null state)
        0x65,
        0x00,  //   Unit (none)
        0x35,
        0x00,  //   Physical Minimum (0)   (the physical range is a global item: the 0..315 of
        0x45,
        0x00,  //   Physical Maximum (0)    the hat would scale the sticks and the MOUSE of report 4 too.
               //                           Windows would make a move of +25 / 0 into +189 / +157: the pointer
               //                           goes to the corner. 0 / 0 = the physical range is the logical range)
        0x75,
        0x04,  //   Report Size (4)
        0x95,
        0x01,  //   Report Count (1)
        0x81,
        0x03,  //   Input (Const): padding
        0x09,
        0x30,  //   Usage (X)
        0x09,
        0x31,  //   Usage (Y)
        0x09,
        0x32,  //   Usage (Z)
        0x09,
        0x35,  //   Usage (Rz)
        0x15,
        0x81,  //   Logical Minimum (-127)
        0x25,
        0x7F,  //   Logical Maximum (127)
        0x75,
        0x08,  //   Report Size (8)
        0x95,
        0x04,  //   Report Count (4)
        0x81,
        0x02,  //   Input (Data, Var, Abs)
        0xC0,  // End Collection

        // the keyboard (report 2): 8 modifier bits, a reserved byte, 6 keys; out: the 5 LEDs (NUM, CAPS, SCROLL, ...)
        0x05,
        0x01,
        0x09,
        0x06,
        0xA1,
        0x01,
        0x85,
        REPORT_KBD,
        0x05,
        0x07,
        0x19,
        0xE0,
        0x29,
        0xE7,
        0x15,
        0x00,
        0x25,
        0x01,
        0x75,
        0x01,
        0x95,
        0x08,
        0x81,
        0x02,
        0x95,
        0x01,
        0x75,
        0x08,
        0x81,
        0x01,
        0x95,
        0x05,
        0x75,
        0x01,
        0x05,
        0x08,
        0x19,
        0x01,
        0x29,
        0x05,
        0x91,
        0x02,
        0x95,
        0x01,
        0x75,
        0x03,
        0x91,
        0x01,
        0x95,
        0x06,
        0x75,
        0x08,
        0x15,
        0x00,
        0x25,
        0x65,
        0x05,
        0x07,
        0x19,
        0x00,
        0x29,
        0x65,
        0x81,
        0x00,
        0xC0,

        // the media keys (report 3): one consumer usage of 16 bits (0 = none)
        0x05,
        0x0C,
        0x09,
        0x01,
        0xA1,
        0x01,
        0x85,
        REPORT_MEDIA,
        0x15,
        0x00,
        0x26,
        0xFF,
        0x03,
        0x19,
        0x00,
        0x2A,
        0xFF,
        0x03,
        0x75,
        0x10,
        0x95,
        0x01,
        0x81,
        0x00,
        0xC0,

        // the mouse (report 4): 3 buttons, X, Y, the wheel (relative, -127..127)
        0x05,
        0x01,
        0x09,
        0x02,
        0xA1,
        0x01,
        0x85,
        REPORT_MOUSE,
        0x09,
        0x01,
        0xA1,
        0x00,
        0x05,
        0x09,
        0x19,
        0x01,
        0x29,
        0x03,
        0x15,
        0x00,
        0x25,
        0x01,
        0x95,
        0x03,
        0x75,
        0x01,
        0x81,
        0x02,
        0x95,
        0x01,
        0x75,
        0x05,
        0x81,
        0x03,
        0x05,
        0x01,
        0x09,
        0x30,
        0x09,
        0x31,
        0x09,
        0x38,
        0x15,
        0x81,
        0x25,
        0x7F,
        0x75,
        0x08,
        0x95,
        0x03,
        0x81,
        0x06,
        0xC0,
        0xC0,
};
// (a change of the map above: a new number here; the hosts that know the old map get a "service changed" indication)
#define MAP_VERSION   3       // (3: the physical range ends after the hat)

static esp_hid_raw_report_map_t s_maps[] = {
        {.data = REPORT_MAP, .len = sizeof(REPORT_MAP)},
};

static char s_name[30] = PAD_NAME;
static const char *const MIDI_NAME = "TFN MIDI";
static volatile bool s_midi_adv;  // advertise the MIDI service (the MIDI app is open)

static esp_hid_device_config_t s_cfg = {
        .vendor_id = 0x16C0,
        .product_id = 0x05E1,
        .version = 0x0100,
        .device_name = s_name,
        .manufacturer_name = "TFN",
        .serial_number = "0001",
        .report_maps = s_maps,
        .report_maps_len = 1,
};

static esp_hidd_dev_t *s_dev;
static volatile pad_state_t s_state = PAD_OFF;
static const app_t *s_owner;  // the app that started the stack (bt_pad_tick stops it after that app)
static int64_t s_gone_at;     // the time when the owning app closed (0: it is open)
static bool s_ram_fail;       // PAD_FAILED came from the check of the RAM: the next start tries again
static struct ble_hs_adv_fields s_fields;
static struct ble_hs_adv_fields s_mfields, s_mrsp, s_norsp;  // MIDI: the UUID; its scan response: the name
static ble_uuid128_t s_midi_uuid = {.u = {.type = BLE_UUID_TYPE_128}};
static ble_uuid16_t s_hid_uuid = BLE_UUID16_INIT(0x1812);
static uint8_t s_last[REPORT_LEN];
static int64_t s_last_us;
static uint32_t s_sent;

// ---- the sender: the reports wait in a ring, and a task sends them only while NimBLE has free buffers
// (with an empty msys pool, esp_hidd asserts: a restart). The gamepad and the mouse are states: only the newest one
// waits (the motion of the mouse adds up). In the boot protocol (a BIOS, some TVs) only the keyboard (8 bytes) and the
// mouse (3 bytes) go: the other reports have no boot form, and esp_hidd asserts on them.
#define TXQ           128
typedef struct {
    uint8_t id, len, d[8];
} txr_t;
static txr_t s_txq[TXQ] EXT_RAM_BSS_ATTR;  // (PSRAM: the BLE start needs the internal RAM)
static volatile uint32_t s_tw, s_tr;
static portMUX_TYPE s_txlock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_txtask;
static volatile bool s_tx_quit;  // (bt_pad_stop: the sender ends; its stack was in the middle of the free RAM)
static volatile bool s_boot;
static volatile uint8_t s_leds;
static int32_t s_mdx, s_mdy, s_mwh;
static uint8_t s_mbtn;
static volatile bool s_mdirty, s_pad_dirty;
static uint8_t s_pad_now[REPORT_LEN];
static volatile int64_t s_adv_retry_us;  // (an advertising start that failed: the sender task tries again at this time)
static int s_adv_fails;                  // (the failed starts in a row: only the first goes to the log)
static void adv_start(void);

static void tx_push(uint8_t id, const uint8_t *d, int len)
{
    taskENTER_CRITICAL(&s_txlock);
    if (s_tw - s_tr < TXQ) {
        txr_t *r = &s_txq[s_tw % TXQ];
        r->id = id;
        r->len = (uint8_t)len;
        memcpy(r->d, d, (size_t)len);
        s_tw++;
    }
    taskEXIT_CRITICAL(&s_txlock);
    if (s_txtask) {
        xTaskNotifyGive(s_txtask);
    }
}
// one report now; false = no free buffer (try again later)
static bool tx_one(uint8_t id, const uint8_t *d, int len)
{
    if (s_state != PAD_READY || !s_dev) {
        return true;  // (no secure link: the report goes away)
    }
    if (s_boot && id != REPORT_KBD && id != REPORT_MOUSE) {
        return true;
    }
    if (s_boot && id == REPORT_MOUSE) {
        len = 3;
    }
    if (os_msys_num_free() < 4) {
        return false;
    }
    if (esp_hidd_dev_input_set(s_dev, 0, id, (uint8_t *)d, (size_t)len) == ESP_OK) {
        s_sent++;
    }
    return true;
}
static int clamp127(int32_t v)
{
    return v > 127 ? 127 : v < -127 ? -127 : (int)v;
}
static void tx_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        if (s_tx_quit) {
            s_txtask = NULL;
            vTaskDelete(NULL);
        }
        while (s_tr != s_tw) {
            txr_t r = s_txq[s_tr % TXQ];
            if (!tx_one(r.id, r.d, r.len)) {
                break;
            }
            s_tr++;
        }
        if (s_tr != s_tw) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        // an advertising start that failed (a PC can connect 0.3 s after the start and the link can fail at
        // once; then a new start finds the old link not yet free: BLE_HS_ENOMEM, and the board is never visible)
        if (s_adv_retry_us && esp_timer_get_time() >= s_adv_retry_us) {
            s_adv_retry_us = 0;
            if (s_state == PAD_ADVERTISING || s_state == PAD_STARTING) {
                adv_start();
            }
        }
        if (s_mdirty) {
            taskENTER_CRITICAL(&s_txlock);
            int dx = clamp127(s_mdx), dy = clamp127(s_mdy), wh = clamp127(s_mwh);
            uint8_t b = s_mbtn;
            taskEXIT_CRITICAL(&s_txlock);
            uint8_t m[4] = {b, (uint8_t)dx, (uint8_t)dy, (uint8_t)wh};
            if (tx_one(REPORT_MOUSE, m, 4)) {
                taskENTER_CRITICAL(&s_txlock);
                s_mdx -= dx;
                s_mdy -= dy;
                s_mwh -= wh;
                s_mdirty = s_mdx || s_mdy || s_mwh || s_mbtn != b;
                taskEXIT_CRITICAL(&s_txlock);
            }
        }
        if (s_pad_dirty) {
            uint8_t p[REPORT_LEN];
            taskENTER_CRITICAL(&s_txlock);
            memcpy(p, s_pad_now, sizeof(p));
            s_pad_dirty = false;
            taskEXIT_CRITICAL(&s_txlock);
            if (!tx_one(REPORT_ID, p, REPORT_LEN)) {
                s_pad_dirty = true;
            }
        }
        if (s_mdirty || s_pad_dirty) {
            vTaskDelay(pdMS_TO_TICKS(8));  // (about 120 reports a second at most)
        }
    }
}

static int gap_event(struct ble_gap_event *event, void *arg);

static void adv_start(void)
{
    if (ble_gap_adv_active()) {
        return;
    }
    struct ble_gap_adv_params p = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
            .itvl_min = BLE_GAP_ADV_ITVL_MS(30),
            .itvl_max = BLE_GAP_ADV_ITVL_MS(50),
    };
    // the gamepad: flags, appearance, name, HID UUID; MIDI: flags and the 128-bit UUID (21 bytes),
    // the name in the scan response (the 31 bytes do not hold both)
    int rc = ble_gap_adv_set_fields(s_midi_adv ? &s_mfields : &s_fields);
    if (rc == 0) {
        rc = ble_gap_adv_rsp_set_fields(s_midi_adv ? &s_mrsp : &s_norsp);
    }
    if (rc == 0) {
        rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    }
    // BLE_HS_EALREADY: the other event (GAP or HID) started it at the same time
    if (rc == 0 || rc == BLE_HS_EALREADY) {
        s_state = PAD_ADVERTISING;
        if (s_adv_fails) {
            ESP_LOGI(TAG, "advertising again after %d failed starts", s_adv_fails);
        }
        s_adv_fails = 0;
    } else {
        if (!s_adv_fails++) {
            ESP_LOGE(TAG, "advertising failed: %d (the board tries again each second)", rc);
        }
        s_adv_retry_us = esp_timer_get_time() + 1000000;
    }
}

// GAP events of our advertising (the HID layer also gets them through its listener).
static int gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    ble_midi_gap_event(event);  // the MIDI link: connection, subscription, MTU
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_state = PAD_CONNECTED;
            ESP_LOGI(TAG, "connected");
            // ask for the secure link at once: HID reports need it, and a computer that came for
            // the MIDI service only (no encryption) never starts it by itself. With old keys that
            // do not match, the pairing runs again (BLE_GAP_EVENT_REPEAT_PAIRING).
            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0) {
                ESP_LOGW(TAG, "security request: %d", rc);
            }
        } else {
            ESP_LOGW(TAG, "a connection failed: %d", event->connect.status);
            adv_start();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (%d)", event->disconnect.reason);
        s_state = PAD_ADVERTISING;
        adv_start();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_state == PAD_ADVERTISING) {
            adv_start();
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            s_state = PAD_READY;
            ESP_LOGI(TAG, "secure link: ready");
            // a new report map: a host that keeps the old one gets "service changed" (one time for each version)
            if (store_get("bt_mapv", 0) != MAP_VERSION) {
                ble_svc_gatt_changed(0x0001, 0xFFFF);
                store_set("bt_mapv", MAP_VERSION);
                ESP_LOGI(TAG, "the report map changed (version %d): service changed", MAP_VERSION);
            }
            // a short connection interval, as a keyboard or a mouse asks (a computer keeps 30-50 ms for a new
            // device, and the keys feel slow): 11.25-15 ms, no latency, 4 s supervision. The computer
            // chooses; BLE_GAP_EVENT_CONN_UPDATE logs the result.
            struct ble_gap_upd_params up = {.itvl_min = 9, .itvl_max = 12, .latency = 0, .supervision_timeout = 400};
            int urc = ble_gap_update_params(event->enc_change.conn_handle, &up);
            if (urc != 0) {
                ESP_LOGW(TAG, "connection interval request: %d", urc);
            }
            if (s_txtask) {
                xTaskNotifyGive(s_txtask);
            }
        }
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "connection interval %.2f ms, latency %d (status %d)", desc.conn_itvl * 1.25f,
                     desc.conn_latency, event->conn_update.status);
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // the computer forgot the old bond: forget it too and pair again
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        // "just works": accept a number comparison if the computer asks for one
        struct ble_sm_io io = {0};
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            io.action = BLE_SM_IOACT_NUMCMP;
            io.numcmp_accept = 1;
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        return 0;
    }
    default:
        return 0;
    }
}

static void hidd_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch ((esp_hidd_event_t)id) {
    case ESP_HIDD_START_EVENT:
        ble_svc_gap_device_name_set(s_midi_adv ? MIDI_NAME : s_name);
        ble_svc_gap_device_appearance_set(ESP_HID_APPEARANCE_GENERIC);
        adv_start();
        break;
    case ESP_HIDD_CONNECT_EVENT:
        if (s_state < PAD_CONNECTED) {
            s_state = PAD_CONNECTED;
        }
        s_boot = false;  // (a new link starts in the report protocol)
        break;
    case ESP_HIDD_PROTOCOL_MODE_EVENT: {
        esp_hidd_event_data_t *p = (esp_hidd_event_data_t *)event_data;
        s_boot = p->protocol_mode.protocol_mode == ESP_HID_PROTOCOL_MODE_BOOT;
        ESP_LOGI(TAG, "protocol: %s", s_boot ? "BOOT" : "REPORT");
        break;
    }
    case ESP_HIDD_OUTPUT_EVENT: {
        // the LEDs of the keyboard (NUM 1, CAPS 2, SCROLL 4) from the host
        esp_hidd_event_data_t *p = (esp_hidd_event_data_t *)event_data;
        if (p->output.length >= 1 && (p->output.report_id == REPORT_KBD || s_boot)) {
            s_leds = p->output.data[0];
        }
        break;
    }
    case ESP_HIDD_DISCONNECT_EVENT:
        s_state = PAD_ADVERTISING;
        adv_start();
        break;
    default:
        break;
    }
}

static void host_task(void *param)
{
    nimble_port_run();  // returns only after nimble_port_stop()
    nimble_port_freertos_deinit();
}

void ble_store_config_init(void);
int ble_gatts_conn_can_alloc(void);  // (NimBLE, not in a public header: see bt_pad_stop)
#define BLE_NEED_FREE 40000  // the internal RAM that the BLE start needs (measured: see bt_pad_start)
#define BLE_NEED_BLOCK                                                                                                 \
    16000   // (a start with a block of 21504 bytes works on ESP-IDF 6.1. After a stop
            // the largest block can be only 17-19 KB, so a limit of 21000 or more refuses
            // the second start. 4 starts with a block of 17408 bytes worked on a 3.49
            // board, with no failed memory request in the log (main.c alloc_failed))

bool bt_pad_start(void)
{
    s_owner = app_current();
    s_gone_at = 0;
    // (a start that stopped for lack of RAM tries again: the stack stops, and the RAM comes back)
    if (s_state == PAD_FAILED && s_ram_fail) {
        s_state = PAD_OFF;
    }
    if (s_state != PAD_OFF) {
        return s_state != PAD_FAILED;
    }
    // The BLE controller needs internal RAM (blocks up to 30 KB). With too little, its start ends in an assert and
    // the board restarts (seen with the WiFi, the VPN and the web server on): then BT ERROR, no start.
    size_t ifree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t iblock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "BLE start: internal RAM %u free, %u in the largest block", (unsigned)ifree, (unsigned)iblock);
    if (ifree < BLE_NEED_FREE || iblock < BLE_NEED_BLOCK) {
        ESP_LOGE(TAG, "BLE start stopped: not enough internal RAM (need %u free and a block of %u)", BLE_NEED_FREE,
                 BLE_NEED_BLOCK);
        s_ram_fail = true;
        s_state = PAD_FAILED;
        return false;
    }
    s_ram_fail = false;
    s_state = PAD_STARTING;
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        err = esp_bt_controller_init(&cfg);
    }
    if (err == ESP_OK) {
        err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    }
    if (err == ESP_OK) {
        err = esp_nimble_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE start failed: %s", esp_err_to_name(err));
        s_state = PAD_FAILED;
        return false;
    }

    memset(&s_fields, 0, sizeof(s_fields));
    s_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_fields.appearance = ESP_HID_APPEARANCE_GENERIC;  // (a gamepad, a keyboard and a mouse in one)
    s_fields.appearance_is_present = 1;
    s_fields.name = (uint8_t *)s_name;
    s_fields.name_len = strlen(s_name);
    s_fields.name_is_complete = 1;
    s_fields.uuids16 = &s_hid_uuid;
    s_fields.num_uuids16 = 1;
    s_fields.uuids16_is_complete = 1;

    memcpy(s_midi_uuid.value, BLE_MIDI_SVC_UUID128, 16);
    memset(&s_mfields, 0, sizeof(s_mfields));
    s_mfields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    s_mfields.uuids128 = &s_midi_uuid;
    s_mfields.num_uuids128 = 1;
    s_mfields.uuids128_is_complete = 1;
    memset(&s_mrsp, 0, sizeof(s_mrsp));
    s_mrsp.name = (uint8_t *)MIDI_NAME;
    s_mrsp.name_len = strlen(MIDI_NAME);
    s_mrsp.name_is_complete = 1;
    memset(&s_norsp, 0, sizeof(s_norsp));

    // "just works" pairing with a bond: the board has no way to show or type a code
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    err = esp_hidd_dev_init(&s_cfg, ESP_HID_TRANSPORT_BLE, hidd_event, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HID start failed: %s", esp_err_to_name(err));
        s_state = PAD_FAILED;
        return false;
    }
    esp_hidd_dev_battery_set(s_dev, 100);
    ble_midi_register();  // the MIDI service: before the host starts
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    nimble_port_freertos_init(host_task);
    s_tx_quit = false;
    xTaskCreatePinnedToCore(tx_task, "hid_tx", 2560, NULL, 5, &s_txtask, tskNO_AFFINITY);
    ESP_LOGI(TAG, "BLE started: internal RAM %u free, %u in the largest block",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_LOGI(TAG, "started");
    return true;
}

void bt_pad_stop(void)
{
    if (s_state == PAD_OFF || s_state == PAD_STARTING || s_state == PAD_FAILED) {
        return;
    }
    int64_t t0 = esp_timer_get_time();
    size_t in0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_state = PAD_STARTING;  // (tx_one sends nothing; bt_pad_set_name and the MIDI switch wait)
    taskENTER_CRITICAL(&s_txlock);
    s_tr = s_tw;  // (the reports that wait go away)
    s_mdx = s_mdy = s_mwh = 0;
    s_mbtn = 0;
    s_mdirty = s_pad_dirty = false;
    taskEXIT_CRITICAL(&s_txlock);
    s_tx_quit = true;  // (the sender ends after the report that it has now)
    if (s_txtask) {
        xTaskNotifyGive(s_txtask);
    }
    for (int i = 0; i < 20 && s_txtask; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }
    // 1. the host: it ends the links and its task (the GATT data is still good for the events of the links)
    int e2 = nimble_port_stop();
    // 2. the HID device. (ESP-IDF 6.1 bug: its deinit calls ble_gatts_stop(), which frees the GATT block of
    // BLE_STATIC_TO_DYNAMIC and sets it to NULL; nimble_port_deinit() calls ble_gatts_stop() again and reads through the
    // NULL pointer: a LoadProhibited panic. ble_gatts_conn_can_alloc() makes a new empty block for that second stop.)
    esp_err_t e1 = (e2 == 0 && s_dev) ? esp_hidd_dev_deinit(s_dev) : ESP_OK;
    if (e2 == 0) {
        s_dev = NULL;
    }
    ble_gatts_conn_can_alloc();
    // 3. the host memory and the controller
    esp_err_t e3 = e2 == 0 ? nimble_port_deinit() : ESP_FAIL;
    s_boot = false;
    s_leds = 0;
    if (e2 != 0 || e3 != ESP_OK) {
        // (the stack did not stop: it stays on; a start does nothing then)
        ESP_LOGE(TAG, "BLE stop failed: hid %d, host %d, deinit %d", (int)e1, e2, (int)e3);
        s_state = PAD_ADVERTISING;
        s_tx_quit = false;
        if (!s_txtask) {
            xTaskCreatePinnedToCore(tx_task, "hid_tx", 2560, NULL, 5, &s_txtask, tskNO_AFFINITY);
        }
        return;
    }
    s_state = PAD_OFF;
    ESP_LOGI(TAG, "BLE stopped in %d ms: internal RAM %u -> %u free, %u in the largest block",
             (int)((esp_timer_get_time() - t0) / 1000), (unsigned)in0,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

void bt_pad_tick(int64_t now_ms)
{
    if (s_state == PAD_OFF || s_state == PAD_STARTING || s_state == PAD_FAILED) {
        return;
    }
    if (app_current() == s_owner) {
        s_gone_at = 0;
        return;
    }
    // (2 s: a quick switch between apps does not stop the link)
    if (!s_gone_at) {
        s_gone_at = now_ms;
    } else if (now_ms - s_gone_at >= 2000) {
        s_gone_at = 0;
        bt_pad_stop();
    }
}

pad_state_t bt_pad_state(void)
{
    return s_state;
}

const char *bt_pad_name(void)
{
    return s_name;
}

void bt_pad_set_name(const char *name)
{
    if (!name || !name[0] || !strcmp(name, s_name)) {
        return;
    }
    snprintf(s_name, sizeof(s_name), "%s", name);
    pad_config_keep_bt_name(s_name);  // (a name from an app stays after a restart)
    if (s_state == PAD_OFF || s_state == PAD_STARTING || s_state == PAD_FAILED) {
        return;
    }
    // started: the new name goes into the GAP service and the advertising
    if (s_midi_adv) {
        return;  // the MIDI app is open: the name changes when it closes
    }
    ble_svc_gap_device_name_set(s_name);
    s_fields.name = (uint8_t *)s_name;
    s_fields.name_len = strlen(s_name);
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
        adv_start();
    }
}

uint32_t bt_pad_sent(void)
{
    return s_sent;
}

void bt_pad_send(const pad_report_t *r)
{
    if (s_state != PAD_READY || !s_dev) {
        return;
    }
    uint8_t d[REPORT_LEN] = {
            (uint8_t)(r->buttons & 0xFF),
            (uint8_t)(r->buttons >> 8),
            (uint8_t)(r->hat > 7 ? PAD_HAT_CENTER : r->hat),
            (uint8_t)r->x,
            (uint8_t)r->y,
            (uint8_t)r->z,
            (uint8_t)r->rz,
    };
    int64_t now = esp_timer_get_time();
    if (!memcmp(d, s_last, sizeof(d)) && now - s_last_us < REPEAT_US) {
        return;
    }
    memcpy(s_last, d, sizeof(d));
    s_last_us = now;
    taskENTER_CRITICAL(&s_txlock);
    memcpy(s_pad_now, d, sizeof(d));
    s_pad_dirty = true;
    taskEXIT_CRITICAL(&s_txlock);
    if (s_txtask) {
        xTaskNotifyGive(s_txtask);
    }
}

// ---------------------------------------------------------------- the keyboard, the media keys, the mouse
void bt_kbd_report(uint8_t mods, const uint8_t keys[6])
{
    uint8_t d[8] = {mods, 0};
    if (keys) {
        memcpy(d + 2, keys, 6);
    }
    tx_push(REPORT_KBD, d, 8);
}
void bt_kbd_tap(uint8_t mods, uint8_t key)
{
    uint8_t k[6] = {key};
    bt_kbd_report(mods, k);
    bt_kbd_report(0, NULL);
}
// ASCII to a key of the US layout (the host turns it into its own layout): the key, and true with SHIFT
static bool ascii_key(char c, uint8_t *key, bool *shift)
{
    static const char *const PLAIN = "1234567890\n\x1b\b\t -=[]\\#;'`,./";
    static const char *const SHIFTED = "!@#$%^&*()";
    static const char *const SH2 = "_+{}|~:\"~<>?";  // the shifted forms of -=[]\#;'`,./ (# -> ~ on US)
    *shift = false;
    if (c >= 'a' && c <= 'z') {
        *key = (uint8_t)(0x04 + c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        *key = (uint8_t)(0x04 + c - 'A');
        *shift = true;
        return true;
    }
    const char *p = strchr(PLAIN, c);
    if (c && p) {
        *key = (uint8_t)(0x1E + (p - PLAIN));
        return true;
    }
    p = strchr(SHIFTED, c);
    if (c && p) {
        *key = (uint8_t)(0x1E + (p - SHIFTED));
        *shift = true;
        return true;
    }
    p = strchr(SH2, c);
    if (c && p) {
        *key = (uint8_t)(0x2D + (p - SH2));
        *shift = true;
        return true;
    }
    return false;
}
int bt_kbd_type(const char *text)
{
    int n = 0;
    for (const char *s = text; s && *s; s++) {
        uint8_t key;
        bool sh;
        if (!ascii_key(*s, &key, &sh)) {
            continue;
        }
        if (s_tw - s_tr > TXQ - 4) {
            break;  // (the ring is full: the rest waits)
        }
        bt_kbd_tap(sh ? BT_MOD_LSHIFT : 0, key);
        n++;
    }
    return n;
}
uint8_t bt_kbd_leds(void)
{
    return s_leds;
}
void bt_media_tap(uint16_t usage)
{
    uint8_t d[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)}, z[2] = {0, 0};
    tx_push(REPORT_MEDIA, d, 2);
    tx_push(REPORT_MEDIA, z, 2);
}
void bt_mouse_move(int dx, int dy, int wheel)
{
    if (!dx && !dy && !wheel) {
        return;
    }
    taskENTER_CRITICAL(&s_txlock);
    s_mdx += dx;
    s_mdy += dy;
    s_mwh += wheel;
    // (a host that does not read: the motion does not grow for ever)
    if (s_mdx > 4000) {
        s_mdx = 4000;
    }
    if (s_mdx < -4000) {
        s_mdx = -4000;
    }
    if (s_mdy > 4000) {
        s_mdy = 4000;
    }
    if (s_mdy < -4000) {
        s_mdy = -4000;
    }
    s_mdirty = true;
    taskEXIT_CRITICAL(&s_txlock);
    if (s_txtask) {
        xTaskNotifyGive(s_txtask);
    }
}
void bt_mouse_buttons(uint8_t b)
{
    taskENTER_CRITICAL(&s_txlock);
    bool ch = b != s_mbtn;
    s_mbtn = b;
    if (ch) {
        s_mdirty = true;
    }
    taskEXIT_CRITICAL(&s_txlock);
    if (ch && s_txtask) {
        xTaskNotifyGive(s_txtask);
    }
}
bool bt_pad_boot(void)
{
    return s_boot;
}

const char *bt_pad_midi_name(void)
{
    return MIDI_NAME;
}

void bt_pad_adv_midi(bool on)
{
    if (on == s_midi_adv) {
        return;
    }
    s_midi_adv = on;
    if (s_state == PAD_OFF || s_state == PAD_STARTING || s_state == PAD_FAILED) {
        return;  // the start uses it
    }
    ble_svc_gap_device_name_set(on ? MIDI_NAME : s_name);
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
        adv_start();
    }
}

void bt_pad_forget(void)
{
    if (s_state == PAD_OFF || s_state == PAD_FAILED) {
        return;
    }
    ble_store_clear();
}
#endif      // CONFIG_BT_ENABLED
