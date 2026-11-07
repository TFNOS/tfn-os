// THE TAB5 KEYBOARD (M5Stack "Tab5 Keyboard", SKU A164): an STM32 on I2C, address
// 0x6D, on the Ext.Port1 of the Tab5 (SDA GPIO0, SCL GPIO1). The register map: github m5stack/M5Unit-KEYBOARD
// src/unit/unit_Tab5Keyboard.hpp The keyboard runs in its NORMAL mode (register
// 0x10 = 0): each event at 0x20 is one byte, [7] press / release, [6:4] the row, [3:0] the column of the 5 x 14 matrix
// (0xFF = empty). (The HID mode gives only the presses: no key repeat.) tab5_kbd_map.h (the same file as in
// M5PaperDOS-X) gives the HID key of each place; the Sym layer of M5Stack, and these keys of our own (as in DOS):
// Sym+1..0 = F1..F10, Sym+- = F11, Sym++ = F12, Sym+arrows = PgUp / PgDn / Home / End, Sym+Del = Ins, Sym+Aa = Caps Lock.
// Aa is a Shift while it is down; Ctrl and Alt are the modifiers. A key that stays down repeats (REP_WAIT, REP_STEP).
// The INT line (GPIO50) is not used: the SERIAL MONITOR and the IR/RF apps use pins 49/50 of the same port. A task
// reads the queue each 15 ms and puts the INK_* keys (input.h) in the key queue of input.c. With no keyboard it tries
// again each 2 s (the keyboard can come at any time). Only the Tab5 builds it.
#include "sdkconfig.h"
#include "board.h"
#if BOARD_KBD_I2C
#include <stdint.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input.h"
#include "tab5_kbd.h"
#include "tab5_kbd_map.h"

static const char *TAG = "tab5kbd";
#define KB_ADDR       0x6D
#define KB_SDA        GPIO_NUM_0
#define KB_SCL        GPIO_NUM_1
#define REG_INT_STAT  0x01
#define REG_EVENT_NUM 0x02
#define REG_MODE      0x10
#define REG_KEY_EVENT 0x20
#define REG_VERSION   0xFE
#define REP_WAIT      450  // ms before a held key repeats
#define REP_STEP      45   // ms between the repeats
#define NKEYS         (TAB5_KBD_ROWS * TAB5_KBD_COLS)

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static volatile bool s_present;
static volatile int s_version = -1;
static bool s_down[NKEYS];
static bool s_caps;
static int s_rep = -1, s_rep_key, s_rep_mods;  // the key that repeats (its place, its INK_* key and INM_* bits)
static int64_t s_rep_at;

static bool rd(uint8_t reg, uint8_t *out, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, n, 30) == ESP_OK;
}
static bool wr(uint8_t reg, uint8_t v)
{
    uint8_t b[2] = {reg, v};
    return i2c_master_transmit(s_dev, b, 2, 30) == ESP_OK;
}

// the US layout of the HID key codes 0x04 .. 0x38: the plain sign and the sign with Shift
static const char HID_LO[] = "abcdefghijklmnopqrstuvwxyz1234567890\0\0\0\0 -=[]\\\0;'`,./";
static const char HID_HI[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\0\0\0\0 _+{}|\0:\"~<>?";

static int hid_to_key(uint8_t code, bool shift)
{
    switch (code) {
    case 0x28:
        return INK_ENTER;
    case 0x29:
        return INK_ESC;
    case 0x2A:
        return INK_BKSP;
    case 0x2B:
        return INK_TAB;
    case 0x49:
        return INK_INS;
    case 0x4A:
        return INK_HOME;
    case 0x4B:
        return INK_PGUP;
    case 0x4C:
        return INK_DEL;
    case 0x4D:
        return INK_END;
    case 0x4E:
        return INK_PGDN;
    case 0x4F:
        return INK_RIGHT;
    case 0x50:
        return INK_LEFT;
    case 0x51:
        return INK_DOWN;
    case 0x52:
        return INK_UP;
    default:
        break;
    }
    if (code >= 0x3A && code <= 0x45) {
        return INK_F1 + (code - 0x3A);
    }
    if (code >= 0x04 && code <= 0x38) {
        return (unsigned char)(shift ? HID_HI : HID_LO)[code - 0x04];
    }
    return 0;
}
static bool down(int r, int c)
{
    return s_down[r * TAB5_KBD_COLS + c];
}
static bool sym(void)
{
    return down(3, 0);
}

// a press of a key that is not a modifier: its INK_* key (0: none) and the INM_* bits
static int key_of(int r, int c, int *mods)
{
    const bool ctrl = down(4, 0), alt = down(4, 1), aa = down(3, 1) && !sym();
    uint8_t hid = TAB5_KBD_BASE[r * TAB5_KBD_COLS + c][0], hmod = TAB5_KBD_BASE[r * TAB5_KBD_COLS + c][1];
    int key = 0;
    if (sym()) {
        // our Sym keys first (as in M5PaperDOS-X), then the Sym layer of M5Stack
        if (r == 0 && c >= 1 && c <= 10) {
            key = INK_F1 + (c - 1);  // Sym+1..0 = F1..F10
        } else if (r == 0 && c == 11) {
            key = INK_F1 + 10;  // Sym+- = F11
        } else if (r == 0 && c == 12) {
            key = INK_F1 + 11;  // Sym++ = F12
        } else if (r == 0 && c == 13) {
            key = INK_INS;  // Sym+Del = Ins
        } else if (hid == 0x52) {
            key = INK_PGUP;
        } else if (hid == 0x51) {
            key = INK_PGDN;
        } else if (hid == 0x50) {
            key = INK_HOME;
        } else if (hid == 0x4F) {
            key = INK_END;
        } else {
            uint8_t sh, sm;
            if (tab5_kbd_sym_hid((uint8_t)r, (uint8_t)c, &sh, &sm)) {
                hid = sh;
                hmod = sm;
            }
        }
    }
    bool shift = (hmod & 0x02) || aa;
    if (!key) {
        key = hid_to_key(hid, shift);
        if (s_caps && key >= 'a' && key <= 'z') {
            key -= 32;
        } else if (s_caps && key >= 'A' && key <= 'Z' && aa) {
            key += 32;
        }
    }
    *mods = (aa ? INM_SHIFT : 0) | (ctrl ? INM_CTRL : 0) | (alt ? INM_ALT : 0);
    return key;
}

static void key_event(uint8_t ev, int64_t now)
{
    const bool press = ev & 0x80;
    const int r = (ev >> 4) & 7, c = ev & 15;
    if (r >= TAB5_KBD_ROWS || c >= TAB5_KBD_COLS) {
        return;
    }
    const int i = r * TAB5_KBD_COLS + c;
    s_down[i] = press;
    if (!press) {
        if (i == s_rep) {
            s_rep = -1;
        }
        return;
    }
    if (TAB5_KBD_IS_SYM(r, c) || TAB5_KBD_IS_CTRL(r, c) || TAB5_KBD_IS_ALT(r, c)) {
        return;
    }
    if (TAB5_KBD_IS_AA(r, c)) {
        if (sym()) {
            s_caps = !s_caps;  // Sym+Aa = Caps Lock
        }
        return;
    }
    int mods, key = key_of(r, c, &mods);
    if (!key) {
        return;
    }
    input_key_push(key, mods);
    s_rep = i;
    s_rep_key = key;
    s_rep_mods = mods;
    s_rep_at = now + REP_WAIT;
}

static bool kb_start(void)
{
    uint8_t v = 0;
    if (!rd(REG_VERSION, &v, 1)) {
        return false;
    }
    if (!wr(REG_MODE, 0) || !wr(REG_EVENT_NUM, 0) || !wr(REG_INT_STAT, 0)) {
        return false;
    }
    memset(s_down, 0, sizeof s_down);
    s_rep = -1;
    s_version = v;
    ESP_LOGI(TAG, "the Tab5 keyboard: firmware %d, normal mode", v);
    return true;
}

static void kb_task(void *arg)
{
    (void)arg;
    int fails = 0;
    for (;;) {
        if (!s_present) {
            s_present = kb_start();
            if (!s_present) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            fails = 0;
        }
        const int64_t now = esp_timer_get_time() / 1000;
        uint8_t n = 0;
        if (!rd(REG_EVENT_NUM, &n, 1)) {
            if (++fails >= 5) {
                s_present = false;
                s_rep = -1;
                ESP_LOGW(TAG, "the Tab5 keyboard does not answer");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        fails = 0;
        if (n > 32) {
            n = 32;
        }
        for (int k = 0; k < n; k++) {
            uint8_t e = 0xFF;
            if (!rd(REG_KEY_EVENT, &e, 1) || e == 0xFF) {
                break;
            }
            key_event(e, now);
        }
        if (n) {
            wr(REG_INT_STAT, 0);
        }
        if (s_rep >= 0 && now >= s_rep_at) {
            input_key_push(s_rep_key, s_rep_mods);
            s_rep_at = now + REP_STEP;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

void tab5_kbd_init(void)
{
    if (s_bus) {
        return;
    }
    i2c_master_bus_config_t bc = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = KB_SDA,
            .scl_io_num = KB_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) {
        ESP_LOGW(TAG, "no I2C bus for the keyboard");
        s_bus = NULL;
        return;
    }
    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = KB_ADDR, .scl_speed_hz = 100000};
    if (i2c_master_bus_add_device(s_bus, &dc, &s_dev) != ESP_OK) {
        return;
    }
    xTaskCreatePinnedToCore(kb_task, "tab5kbd", 3072, NULL, 4, NULL, 0);
}

bool tab5_kbd_present(void)
{
    return s_present;
}
int tab5_kbd_version(void)
{
    return s_version;
}
#endif
