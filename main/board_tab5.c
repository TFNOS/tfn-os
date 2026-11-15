// Board support for the M5Stack Tab5 (ESP32-P4). It has the same API as board.c (see board.h). The build uses this file
// in place of board.c when CONFIG_TFN_TARGET_TAB5 is on. The pins and the chips come from M5GFX and M5Unified (m5stack, the Tab5 parts), the start tables of the panels
// from M5GFX (main/tab5_panels.inc).
//
// Screen: 720 x 1280 MIPI-DSI, 2 lanes, one of three panels (the touch chip tells which): ILI9881C (touch GT911 at
//         0x14), ST7121 (the ST touch at 0x55 says version 1) or ST7123 (version 3). The DSI PHY: LDO channel 3,
//         2.5 V. Backlight: GPIO22 (PWM). The DPI driver keeps two frame buffers in PSRAM.
// I2C:    SDA 31, SCL 32: the IO expanders PI4IOE5V6408 0x43 and 0x44, the touch, ES8388 0x10 (the speaker),
//         ES7210 0x40 (the microphones), BMI270 0x68, INA226 0x41 (the battery), RX8130 0x32 (the clock).
// IO:     0x43: P0 the antenna, P1 SPK_EN, P2 EXT5V_EN, P4 LCD_RST, P5 TP_RST, P6 CAM_RST, P7 HP_DET (in).
//         0x44: P0 WLAN_PWR_EN (the C6), P3 USB5V_EN, P4 PWROFF_PULSE, P5 nCHG_QC_EN, P6 CHG_STAT (in), P7 CHG_EN.
// SD:     SDMMC slot 0, 4-bit, the IOMUX pins (CLK 43, CMD 44, D0..D3 39..42), LDO channel 4. The WiFi C6 is on
//         slot 1 (esp_hosted).
//
// The picture: the apps draw the frame of the Tab5 (the physical frame of gfx.h is 360 x 640, the shape of the
// panel). The PPA (the pixel processing accelerator of the P4) turns it for the orientation, scales it 2 times (the
// whole 720 x 1280 panel) and swaps the bytes, into the back frame buffer. Then the panel shows that buffer. Which PPA
// turn, mirror and order of the scales give each orientation is
// found at the start (a small test picture, all the choices, compared with the CPU way); an orientation with no match
// takes the CPU way (slow).

#include "sdkconfig.h"

#if CONFIG_TFN_TARGET_TAB5

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/ppa.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "board.h"
#include "board_i2c.h"
#include "files.h"
#include "gfx.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "bootloader_common.h"
#include "boot_req.h"

_Static_assert(LCD_W == 360 && LCD_H == 640, "the Tab5 frame: LCD_W 360, LCD_H 640 in board.h");

static const char *TAG = "board";

#include "tab5_panels.inc"
#include "board_sd.h"
#include "sd_dma.h"
#include "evlog.h"

#define PIN_I2C_SDA GPIO_NUM_31
#define PIN_I2C_SCL GPIO_NUM_32
#define PIN_TP_INT  GPIO_NUM_23
#define PIN_BL      GPIO_NUM_22

#define IO1_ADDR   0x43
#define IO2_ADDR   0x44
#define INA_ADDR   0x41
#define GT911_ADDR 0x14
#define ST_TP_ADDR 0x55
// the PI4IOE5V6408 registers
#define PI_ID        0x01
#define PI_DIR       0x03  // 1 = output
#define PI_OUT       0x05
#define PI_HIZ       0x07  // 1 = the output is high impedance
#define PI_IN_DEF    0x09
#define PI_PULL_EN   0x0B
#define PI_PULL_UP   0x0D  // 1 = pull up
#define PI_IN        0x0F
#define PI_INT_MASK  0x11
#define IO1_SPK      0x02
#define IO2_PWROFF   0x10
#define IO2_USB5V    0x08
#define IO2_CHG_STAT 0x40

// the panel and the picture on it
#define PANEL_W 720
#define PANEL_H 1280
// the picture: 2 times on both axes (the 360 x 640 frame = the whole panel). A stretch of the CoreS3 frame
// 3 x 4 does not look good, and the bands of a 3 x 3 picture waste the screen, so the apps draw the
// shape of the panel)
#define SX     2
#define SY     2
#define PIC_W  (LCD_W * SX)             // 720
#define PIC_H  (LCD_H * SY)             // 1280
#define PIC_Y0 ((PANEL_H - PIC_H) / 2)  // 0

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_io1, s_io2, s_ina, s_tp;
static uint8_t s_io1_out, s_io2_out;
enum {
    TP_NONE,
    TP_GT911,
    TP_ST
};
static int s_tp_kind;
static const char *s_panel_name = "none";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_dbi;
static uint16_t *s_fb[2];            // the two frame buffers of the DPI driver (720 x 1280, RGB565, CPU order)
static int s_back;                   // the buffer that the next frame goes into
static SemaphoreHandle_t s_fb_done;  // the panel took the last buffer: the other one is free
static ppa_client_handle_t s_ppa;
static QueueHandle_t s_flush_q;
static SemaphoreHandle_t s_flush_idle;
typedef struct {
    const uint16_t *fb;
    int orient;
} frame_t;
static uint8_t s_bright;
static bool s_spk_on = true, s_spk_hold, s_scr_sleep;

// ---------------------------------------------------------------- I2C, the IO expanders

static void io_set(i2c_master_dev_handle_t dev, uint8_t *out, uint8_t bits, bool on)
{
    *out = on ? (uint8_t)(*out | bits) : (uint8_t)(*out & ~bits);
    reg_write(dev, PI_OUT, *out);
}

board_ver_t board_version(void)
{
    return BOARD_V2;
}
i2c_master_bus_handle_t board_i2c0(void)
{
    return s_bus;
}

// The expanders (the values of M5GFX and M5Unified): the LCD and the touch held in reset, then the touch reset high and
// the LCD reset an input with its pull-up (that ends the reset). The touch INT line is high during the reset: the GT911
// then takes the address 0x14.
static void io_init(void)
{
    gpio_config_t g = {.pin_bit_mask = 1ULL << PIN_TP_INT, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&g);
    gpio_set_level(PIN_TP_INT, 1);
    uint8_t id = 0;
    if (reg_read(s_io1, PI_ID, &id, 1) != ESP_OK) {
        ESP_LOGE(TAG, "the IO expander 0x43 does not answer");
    }
    s_io1_out = 0x46;  // SPK_EN, EXT5V_EN, CAM_RST high; LCD and touch reset low
    reg_write(s_io1, PI_OUT, s_io1_out);
    reg_write(s_io1, PI_DIR, 0x7F);
    reg_write(s_io1, PI_HIZ, 0x00);
    reg_write(s_io1, PI_PULL_UP, 0x7F);
    reg_write(s_io1, PI_PULL_EN, 0x7F);
    s_io2_out = 0x81;  // CHG_EN, WLAN_PWR_EN (USB 5 V out: off)
    reg_write(s_io2, PI_DIR, 0xB9);
    reg_write(s_io2, PI_HIZ, 0x06);
    reg_write(s_io2, PI_PULL_UP, 0xB9);
    reg_write(s_io2, PI_PULL_EN, 0xF9);
    reg_write(s_io2, PI_IN_DEF, 0x40);
    reg_write(s_io2, PI_INT_MASK, 0xBF);
    reg_write(s_io2, PI_OUT, s_io2_out);
    vTaskDelay(pdMS_TO_TICKS(10));
    reg_write(s_io1, PI_DIR, 0x6F);  // LCD_RST: an input (pulled up)
    s_io1_out = 0x76;
    reg_write(s_io1, PI_OUT, s_io1_out);  // TP_RST high
    gpio_set_direction(PIN_TP_INT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_TP_INT, GPIO_PULLUP_ONLY);  // (the touch INT: low while a report waits)
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void i2c_init(void)
{
    i2c_master_bus_config_t bc = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = PIN_I2C_SDA,
            .scl_io_num = PIN_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_bus));
    s_io1 = add_dev(s_bus, IO1_ADDR);
    s_io2 = add_dev(s_bus, IO2_ADDR);
    s_ina = add_dev(s_bus, INA_ADDR);
}

// ---------------------------------------------------------------- the touch chip tells the panel

// The ST touch (16-bit registers): the firmware version at 0x0000 (1 = ST7121, 3 = ST7123). It needs some tens of ms
// after a reset; the GT911 (the ILI9881C panel) only answers its address (a read moves its pointer).
static int st_read(uint16_t reg, uint8_t *buf, size_t n)
{
    uint8_t r[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_transmit_receive(s_tp, r, 2, buf, n, I2C_TIMEOUT_MS) == ESP_OK;
}
static int touch_detect(void)
{
    i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ST_TP_ADDR, .scl_speed_hz = I2C_HZ};
    i2c_master_bus_add_device(s_bus, &dc, &s_tp);
    for (int i = 0; i < 60; i++) {
        uint8_t v = 0;
        if (st_read(0x0000, &v, 1)) {
            if (v == 1 || v == 3) {
                // (the start reads of M5GFX Touch_ST7123::init: the revision and the block of the size)
                uint8_t rev[4] = {0}, info[5] = {0};
                st_read(0x000C, rev, 4);
                st_read(0x0005, info, 5);
                ESP_LOGI(TAG, "touch: ST, firmware %d (%d.%d.%d.%d), max x %d, max y %d, %d points", v, rev[0], rev[1],
                         rev[2], rev[3], info[0] << 8 | info[1], info[2] << 8 | info[3], info[4]);
                return v;
            }
        } else if (i2c_master_probe(s_bus, GT911_ADDR, I2C_TIMEOUT_MS) == ESP_OK) {
            i2c_master_bus_rm_device(s_tp);
            s_tp = add_dev(s_bus, GT911_ADDR);
            ESP_LOGI(TAG, "touch: GT911");
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGW(TAG, "touch: no answer (ST or GT911)");
    return -1;
}

// ---------------------------------------------------------------- the screen

static void send_table(const uint8_t *t)
{
    while (t[0]) {
        esp_lcd_panel_io_tx_param(s_dbi, t[1], t[0] > 1 ? &t[2] : NULL, t[0] - 1);
        t += t[0] + 1;
    }
}

// ---- the numbers of the display for the event log (evlog.h; they help to find a cyan flicker after a wake). Each second
// one line: the refreshes of the panel (vsync), the frames sent, the slowest frame, the waits for the panel that timed
// out, the PPA faults, and the frames whose sample pixels are cyan: in the app frame (software) and in the buffer that
// the panel reads (the copy). Cyan on the screen with clean samples = the panel read (a DPI underrun: ESP-IDF
// esp_lcd_panel_dpi.c says that the picture "may already become blue" then).
volatile uint32_t g_ev_screen_bytes;  // (web_walkie.c: the bytes of /api/screen pictures)
static volatile uint32_t s_ev_tp_reads, s_ev_tp_fing, s_ev_tp_max_us;  // the touch reads (board_touch_points)
static volatile uint32_t s_ev_vsync, s_ev_flush, s_ev_max_us, s_ev_to, s_ev_ppaf, s_ev_cyan_src, s_ev_cyan_dst;
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *ctx)
{
    s_ev_vsync++;
    return false;
}
static inline bool ev_cyan(uint16_t v)  // v: RGB565 (the high byte first in the value)
{
    int r = v >> 11, g = (v >> 5) & 63, b = v & 31;
    return r < 8 && g > 36 && b > 18;
}
// 5 pixels of the app frame (the byte order of the screen: swapped) and of the panel buffer (RGB565, after the copy)
static int ev_src_cyan(const uint16_t *fb, int w, int h)
{
    static const int PX[5][2] = {{50, 50}, {10, 10}, {90, 10}, {10, 90}, {90, 90}};
    int n = 0;
    for (int i = 0; i < 5; i++) {
        n += ev_cyan(__builtin_bswap16(fb[(PX[i][1] * h / 100) * w + PX[i][0] * w / 100]));
    }
    return n;
}
static int ev_dst_cyan(uint16_t *dst)
{
    static const int PX[5][2] = {{50, 50}, {10, 10}, {90, 10}, {10, 90}, {90, 90}};
    int n = 0;
    for (int i = 0; i < 5; i++) {
        uint16_t *p = dst + (PIC_Y0 + PX[i][1] * PIC_H / 100) * PANEL_W + PX[i][0] * PIC_W / 100;
        uintptr_t a = (uintptr_t)p & ~(uintptr_t)127;
        esp_cache_msync((void *)a, 128, ESP_CACHE_MSYNC_FLAG_DIR_M2C);  // (the PPA wrote it by DMA)
        n += ev_cyan(*p);
    }
    return n;
}
static void ev_second(void *arg)
{
    static int quiet;
    uint32_t vs = s_ev_vsync, fl = s_ev_flush, mx = s_ev_max_us, to = s_ev_to, pf = s_ev_ppaf, cs = s_ev_cyan_src,
             cd = s_ev_cyan_dst;
    s_ev_vsync = s_ev_flush = s_ev_max_us = s_ev_to = s_ev_ppaf = s_ev_cyan_src = s_ev_cyan_dst = 0;
    uint32_t web = g_ev_screen_bytes;
    g_ev_screen_bytes = 0;
    uint32_t tr = s_ev_tp_reads, tf = s_ev_tp_fing, tm = s_ev_tp_max_us;
    s_ev_tp_reads = s_ev_tp_fing = s_ev_tp_max_us = 0;
    bool odd = to || pf || cs || cd || (vs && (vs < 50 || vs > 70)) || mx > 80000;
    // (the screen off and nothing odd: one line a minute)
    if (s_scr_sleep && !odd && ++quiet < 60) {
        return;
    }
    quiet = 0;
    // (short: a line of the ring has 112 bytes; MANUAL.md "THE EVENT LOG OF THE TAB5" names the fields)
    evlog_add("t5: vs %lu fl %lu mx %lu to %lu pf %lu cy %lu/%lu bl %d web %luK tp %lu/%lu %lums%s%s",
              (unsigned long)vs, (unsigned long)fl, (unsigned long)(mx / 1000), (unsigned long)to, (unsigned long)pf,
              (unsigned long)cs, (unsigned long)cd, (int)s_bright, (unsigned long)(web / 1024), (unsigned long)tf,
              (unsigned long)tr, (unsigned long)(tm / 1000), s_scr_sleep ? " SLEEP" : "", odd ? " ODD" : "");
}

static bool IRAM_ATTR on_fb_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_fb_done, &woken);
    return woken == pdTRUE;
}

// The PPA choice of each orientation (gfx.h ORIENT_*): -1 = the CPU way; else rot | mirror_x << 2 | mirror_y << 3.
static int s_ppa_way[4] = {-1, -1, -1, -1};
static const ppa_srm_rotation_angle_t ROT[4] = {PPA_SRM_ROTATION_ANGLE_0, PPA_SRM_ROTATION_ANGLE_90,
                                                PPA_SRM_ROTATION_ANGLE_180, PPA_SRM_ROTATION_ANGLE_270};

// The place in the logical frame (w x h, as the app draws it) of the physical point (px, py) of a pw x ph physical frame:
// the same rules as board_amoled.c (tables_orient).
static int log_index(int orient, int px, int py, int pw, int ph)
{
    switch (orient) {
    case ORIENT_PORTRAIT:
        return py * pw + px;
    case ORIENT_PORTRAIT_FLIP:
        return (ph - 1 - py) * pw + (pw - 1 - px);
    case ORIENT_LAND_A:
        return (pw - 1 - px) * ph + py;
    default:
        return px * ph + (ph - 1 - py);
    }
}

// way: bits 0-1 the turn, bit 2 the mirror on x, bit 3 the mirror on y, bit 4 the two scales given the other way round
// (the PPA scales the block before or after the turn: the test finds which). div: 1 = the scale SX x SY, 2 = 1:1 (a HI
// frame, gfx.h: the buffer has the resolution of the panel)
static bool ppa_run(const uint16_t *src, int sw, int sh, uint16_t *dst, uint32_t dst_size, int dw, int dh, int way,
                    int div)
{
    const bool swap = (way >> 4) & 1;
    ppa_srm_oper_config_t op = {
            .in = {.buffer = src,
                   .pic_w = sw,
                   .pic_h = sh,
                   .block_w = sw,
                   .block_h = sh,
                   .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
            .out = {.buffer = dst,
                    .buffer_size = dst_size,
                    .pic_w = dw,
                    .pic_h = dh,
                    .srm_cm = PPA_SRM_COLOR_MODE_RGB565},
            .rotation_angle = ROT[way & 3],
            .scale_x = (float)(swap ? SY : SX) / div,
            .scale_y = (float)(swap ? SX : SY) / div,
            .mirror_x = (way >> 2) & 1,
            .mirror_y = (way >> 3) & 1,
            .byte_swap = true,
            .mode = PPA_TRANS_MODE_BLOCKING,
    };
    return ppa_do_scale_rotate_mirror(s_ppa, &op) == ESP_OK;
}

// The test: a logical picture of 4 quarters (8 x 12 physical pixels, the logical frame of each orientation), scaled by
// each PPA choice to 16 x 24 (SX, SY); the right choice gives the colour that the CPU way gives at the middle of each
// quarter. (the PPA output buffer and its size must be whole cache lines: a buffer of its own)
static void ppa_find_ways(void)
{
    enum {
        TW = 8,
        TH = 12
    };  // the physical test frame (portrait, as LCD_W x LCD_H)
    static const uint16_t QC[4] = {0x00F8, 0xE007, 0x1F00, 0xFFFF};  // red, green, blue, white (bytes swapped)
    uint16_t *in = heap_caps_aligned_calloc(128, TW * TH, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    const uint32_t OUTB = (TW * SX * TH * SY * 2 + 127) & ~127u;
    uint16_t *out = heap_caps_aligned_calloc(128, OUTB, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!in || !out) {
        free(in);
        free(out);
        return;
    }
    for (int o = 0; o < 4; o++) {
        const bool land = o == ORIENT_LAND_A || o == ORIENT_LAND_B;
        const int lw = land ? TH : TW, lh = land ? TW : TH;
        for (int y = 0; y < lh; y++) {
            for (int x = 0; x < lw; x++) {
                in[y * lw + x] = QC[(y >= lh / 2) * 2 + (x >= lw / 2)];
            }
        }
        esp_cache_msync(in, TW * TH * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        for (int way = 0; way < 32 && s_ppa_way[o] < 0; way++) {
            if ((way & 12) == 12) {
                continue;  // (both mirrors = a turn of 180: the same as another choice)
            }
            memset(out, 0, OUTB);
            esp_cache_msync(out, OUTB, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if (!ppa_run(in, lw, lh, out, OUTB, TW * SX, TH * SY, way, 1)) {
                continue;
            }
            esp_cache_msync(out, OUTB, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            bool ok = true;
            for (int q = 0; q < 4 && ok; q++) {
                const int px = (q & 1) ? TW * 3 / 4 : TW / 4, py = (q & 2) ? TH * 3 / 4 : TH / 4;
                const uint16_t want = __builtin_bswap16(in[log_index(o, px, py, TW, TH)]);
                ok = out[(py * SY + 1) * TW * SX + px * SX + 1] == want;
            }
            // (and the bottom right corner must be filled: the whole picture)
            ok = ok && out[(TH * SY - 1) * TW * SX + TW * SX - 1] != 0;
            if (ok) {
                s_ppa_way[o] = way;
            }
        }
        ESP_LOGI(TAG, "PPA: orientation %d: %s %d", o, s_ppa_way[o] >= 0 ? "way" : "no match, the CPU way",
                 s_ppa_way[o]);
    }
    free(in);
    free(out);
}

// The CPU way: each panel pixel from its logical pixel (nearest; SX x SY panel pixels for each).
static void cpu_draw(const uint16_t *fb, int orient, uint16_t *dst)
{
    for (int py = 0; py < LCD_H; py++) {
        uint16_t *r0 = dst + (PIC_Y0 + py * SY) * PANEL_W;
        for (int px = 0; px < LCD_W; px++) {
            const uint16_t c = __builtin_bswap16(fb[log_index(orient, px, py, LCD_W, LCD_H)]);
            for (int k = 0; k < SX; k++) {
                r0[px * SX + k] = c;
            }
        }
        for (int k = 1; k < SY; k++) {
            memcpy(r0 + k * PANEL_W, r0, PANEL_W * 2);
        }
    }
    esp_cache_msync(dst + PIC_Y0 * PANEL_W, PIC_W * PIC_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

// The CPU way for a HI frame: each panel pixel from its buffer pixel (1:1).
static void cpu_draw_hi(const uint16_t *fb, int orient, uint16_t *dst)
{
    for (int py = 0; py < PIC_H; py++) {
        uint16_t *r0 = dst + (PIC_Y0 + py) * PANEL_W;
        for (int px = 0; px < PIC_W; px++) {
            r0[px] = __builtin_bswap16(fb[log_index(orient, px, py, PIC_W, PIC_H)]);
        }
    }
    esp_cache_msync(dst + PIC_Y0 * PANEL_W, PIC_W * PIC_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

static void flush_task(void *arg)
{
    frame_t f;
    for (;;) {
        xQueueReceive(s_flush_q, &f, portMAX_DELAY);
        const int64_t ev_t0 = esp_timer_get_time();
        const int o = f.orient & 3;
        const bool land = o == ORIENT_LAND_A || o == ORIENT_LAND_B;
        uint16_t *dst = s_fb[s_back];                  // (the bands stay black: nothing writes there)
        const int k = (f.orient & ORIENT_HI) ? 2 : 1;  // (a HI frame: the buffer has the panel resolution)
        // (the app frame is in PSRAM, written by the CPU: to the memory before the DMA of the PPA reads it)
        esp_cache_msync((void *)f.fb, LCD_W * LCD_H * 2 * k * k,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        if (s_ppa_way[o] < 0 || !ppa_run(f.fb, (land ? LCD_H : LCD_W) * k, (land ? LCD_W : LCD_H) * k,
                                         dst + PIC_Y0 * PANEL_W, PIC_W * PIC_H * 2, PIC_W, PIC_H, s_ppa_way[o], k)) {
            if (s_ppa_way[o] >= 0) {
                s_ev_ppaf++;
            }
            if (k == 2) {
                cpu_draw_hi(f.fb, o, dst);
            } else {
                cpu_draw(f.fb, o, dst);
            }
        }
        // (the samples: before the app may draw again)
        if (ev_src_cyan(f.fb, (land ? LCD_H : LCD_W) * k, (land ? LCD_W : LCD_H) * k) >= 3) {
            s_ev_cyan_src++;
        }
        if (ev_dst_cyan(dst) >= 3) {
            s_ev_cyan_dst++;
        }
        xSemaphoreGive(s_flush_idle);  // (the app frame was read: the app can draw again)
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_W, PANEL_H,
                                  dst);  // (one of the frame buffers: the panel switches)
        // At the end of each refresh the DMA of the panel starts again with the buffer that draw_bitmap() set last, and
        // gives s_fb_done. draw_bitmap() first writes back the cache of all the buffer (1.8 MB: a long time), then sets
        // the buffer. A refresh that ended in that time started the OLD buffer again: its signal does not free the old
        // buffer. If the code took that signal as "dst shows", the next frame went into the buffer that
        // the panel read at that time (a torn picture: parts of two frames, a flicker when the picture moves fast).
        // So the signals of the time before this line go, and the wait is for the next start: it takes dst.
        xSemaphoreTake(s_fb_done, 0);
        if (xSemaphoreTake(s_fb_done, pdMS_TO_TICKS(50)) != pdTRUE) {
            s_ev_to++;  // the panel shows dst now: the
        }
        s_back ^= 1;  // other buffer is free
        uint32_t us = (uint32_t)(esp_timer_get_time() - ev_t0);
        s_ev_flush++;
        if (us > s_ev_max_us) {
            s_ev_max_us = us;
        }
    }
}

void board_lcd_flush(const uint16_t *fb, int orient)
{
    if (!s_flush_q) {
        return;
    }
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    frame_t f = {fb, orient};
    xQueueSend(s_flush_q, &f, portMAX_DELAY);
}

static void backlight_init(void)
{
    ledc_timer_config_t t = {.speed_mode = LEDC_LOW_SPEED_MODE,
                             .duty_resolution = LEDC_TIMER_8_BIT,
                             .timer_num = LEDC_TIMER_0,
                             .freq_hz = 44100,
                             .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&t);
    ledc_channel_config_t c = {.gpio_num = PIN_BL,
                               .speed_mode = LEDC_LOW_SPEED_MODE,
                               .channel = LEDC_CHANNEL_0,
                               .timer_sel = LEDC_TIMER_0,
                               .duty = 0};
    ledc_channel_config(&c);
}

void board_backlight(uint8_t level)
{
    s_bright = level;
    if (s_scr_sleep) {
        return;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static bool lcd_init(int tp_fw)
{
    // the DSI PHY power, the bus (2 lanes), the command channel
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t lc = {.chan_id = 3, .voltage_mv = 2500};
    if (esp_ldo_acquire_channel(&lc, &ldo) != ESP_OK) {
        ESP_LOGE(TAG, "the DSI LDO");
        return false;
    }
    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_lcd_dsi_bus_config_t bc = {.bus_id = 0, .num_data_lanes = 2, .lane_bit_rate_mbps = tp_fw == 1 ? 900 : 1040};
    if (esp_lcd_new_dsi_bus(&bc, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "the DSI bus");
        return false;
    }
    esp_lcd_dbi_io_config_t dc = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    if (esp_lcd_new_panel_io_dbi(bus, &dc, &s_dbi) != ESP_OK) {
        ESP_LOGE(TAG, "the DBI channel");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(80));
    // which panel (the ST touch firmware tells the ST panels; else the ID of the ILI9881C on its page 1)
    int kind = tp_fw == 1 ? 1 : tp_fw == 3 ? 3 : 0;
    if (!kind) {
        static const uint8_t page1[] = {0x98, 0x81, 0x01}, page0[] = {0x98, 0x81, 0x00};
        uint8_t id[2] = {0};
        esp_lcd_panel_io_tx_param(s_dbi, 0xFF, page1, 3);
        esp_lcd_panel_io_rx_param(s_dbi, 0x00, &id[0], 1);
        esp_lcd_panel_io_rx_param(s_dbi, 0x01, &id[1], 1);
        esp_lcd_panel_io_tx_param(s_dbi, 0xFF, page0, 3);
        ESP_LOGI(TAG, "panel ID %02X %02X", id[0], id[1]);
        kind = 9;  // (ILI9881C: the default when no ST touch answered)
    }
    // the video timing of each panel (M5GFX)
    esp_lcd_dpi_panel_config_t pc = {
            .virtual_channel = 0,
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .in_color_format = LCD_COLOR_FMT_RGB565,
            .out_color_format = LCD_COLOR_FMT_RGB565,
            .num_fbs = 2,
            .video_timing = {.h_size = PANEL_W, .v_size = PANEL_H},
    };
    esp_lcd_video_timing_t *vt = &pc.video_timing;
    if (kind == 9) {
        s_panel_name = "ILI9881C";
        pc.dpi_clock_freq_mhz = 80;
        vt->hsync_back_porch = 140;
        vt->hsync_pulse_width = 40;
        vt->hsync_front_porch = 40;
        vt->vsync_back_porch = 20;
        vt->vsync_pulse_width = 4;
        vt->vsync_front_porch = 20;
    } else if (kind == 1) {
        s_panel_name = "ST7121";
        pc.dpi_clock_freq_mhz = 70;
        vt->hsync_back_porch = 40;
        vt->hsync_pulse_width = 2;
        vt->hsync_front_porch = 40;
        vt->vsync_back_porch = 24;
        vt->vsync_pulse_width = 20;
        vt->vsync_front_porch = 200;
    } else {
        s_panel_name = "ST7123";
        pc.dpi_clock_freq_mhz = 80;
        vt->hsync_back_porch = 40;
        vt->hsync_pulse_width = 2;
        vt->hsync_front_porch = 40;
        vt->vsync_back_porch = 8;
        vt->vsync_pulse_width = 2;
        vt->vsync_front_porch = 220;
    }
    if (esp_lcd_new_panel_dpi(bus, &pc, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "the DPI panel");
        return false;
    }
    esp_lcd_dpi_panel_enable_dma2d(s_panel);
    // the start tables in the command mode, then MADCTL (RGB) and COLMOD (RGB565), then the video
    static const uint8_t madctl = 0x00, colmod = 0x55;
    if (kind == 9) {
        send_table(T5_ILI_LIST0);
        send_table(T5_ILI_LIST1);
        vTaskDelay(pdMS_TO_TICKS(120));
        send_table(T5_ILI_LIST2);
        send_table(T5_ILI_LIST3);
    } else if (kind == 1) {
        send_table(T5_ST7121_LIST0);
        send_table(T5_ST7121_LIST1);
        vTaskDelay(pdMS_TO_TICKS(80));
        send_table(T5_ST7121_LIST2);
        vTaskDelay(pdMS_TO_TICKS(800));
        send_table(T5_ST7121_LIST3);
    } else {
        send_table(T5_ST7123_LIST0);
        send_table(T5_ST7123_LIST1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    esp_lcd_panel_io_tx_param(s_dbi, 0x36, &madctl, 1);
    esp_lcd_panel_io_tx_param(s_dbi, 0x3A, &colmod, 1);
    if (esp_lcd_panel_init(s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "the DPI start");
        return false;
    }
    void *f0 = NULL, *f1 = NULL;
    esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &f0, &f1);
    s_fb[0] = f0;
    s_fb[1] = f1;
    if (!f0 || !f1) {
        ESP_LOGE(TAG, "no frame buffers");
        return false;
    }
    memset(f0, 0, PANEL_W * PANEL_H * 2);
    memset(f1, 0, PANEL_W * PANEL_H * 2);
    esp_cache_msync(f0, PANEL_W * PANEL_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_cache_msync(f1, PANEL_W * PANEL_H * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    s_fb_done = xSemaphoreCreateBinary();
    esp_lcd_dpi_panel_event_callbacks_t cbs = {.on_frame_buf_complete = on_fb_done, .on_vsync = on_vsync};
    esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL);
    s_back = 1;
    ppa_client_config_t cc = {.oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = 1};
    if (ppa_register_client(&cc, &s_ppa) == ESP_OK) {
        ppa_find_ways();
    } else {
        ESP_LOGW(TAG, "no PPA: the CPU way");
    }
    s_flush_q = xQueueCreate(1, sizeof(frame_t));
    s_flush_idle = xSemaphoreCreateBinary();
    xSemaphoreGive(s_flush_idle);
    xTaskCreatePinnedToCore(flush_task, "lcd", 4096, NULL, 7, NULL, 1);
    static esp_timer_handle_t ev_tmr;
    const esp_timer_create_args_t ta = {.callback = ev_second, .name = "evlog"};
    if (!ev_tmr && esp_timer_create(&ta, &ev_tmr) == ESP_OK) {
        esp_timer_start_periodic(ev_tmr, 1000000);
    }
    return true;
}

// ---------------------------------------------------------------- speaker, screen sleep

void board_speaker(bool on)
{
    s_spk_on = on;
    if (!s_scr_sleep || s_spk_hold) {
        io_set(s_io1, &s_io1_out, IO1_SPK, on);
    }
}
void board_speaker_hold(bool hold)
{
    s_spk_hold = hold;
    if (s_scr_sleep) {
        io_set(s_io1, &s_io1_out, IO1_SPK, hold && s_spk_on);
    }
}
void board_screen_sleep(bool sleep)
{
    if (sleep == s_scr_sleep) {
        return;
    }
    s_scr_sleep = sleep;
    evlog_add("t5: screen %s (backlight %d)", sleep ? "off" : "on", (int)s_bright);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, sleep ? 0 : s_bright);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    io_set(s_io1, &s_io1_out, IO1_SPK, sleep ? (s_spk_hold && s_spk_on) : s_spk_on);
}

// ---------------------------------------------------------------- keys (the Tab5 has no key for the apps)

bool board_boot_key_down(void)
{
    return false;
}
bool board_pwr_key_down(void)
{
    return false;
}

// ---------------------------------------------------------------- touch

// A panel point (tx 0..719, ty 0..1279, portrait) goes to the physical frame of gfx.h (240 x 320) by the inverse scale
// (a touch on a band goes to the nearest edge of the picture).
static int to_phys(int tx, int ty, int *x, int *y)
{
    int px = tx / SX, py = (ty - PIC_Y0) / SY;
    if (ty < PIC_Y0) {
        py = 0;
    }
    *x = px < 0 ? 0 : px >= LCD_W ? LCD_W - 1 : px;
    *y = py < 0 ? 0 : py >= LCD_H ? LCD_H - 1 : py;
    return 1;
}
static int64_t s_tpd_until;
void board_tp_diag(int secs)
{
    s_tpd_until = esp_timer_get_time() + (int64_t)(secs > 120 ? 120 : secs) * 1000000;
    ESP_LOGI(TAG, "tpdiag: %d s, touch %s", secs, s_tp_kind == TP_ST ? "ST" : s_tp_kind == TP_GT911 ? "GT911" : "none");
}
static void tpd_log(const char *what, esp_err_t e, const uint8_t *b, int n)
{
    static int64_t s_last;
    const int64_t now = esp_timer_get_time();
    if (!s_tpd_until || now > s_tpd_until || now - s_last < 150000) {
        return;
    }
    s_last = now;
    char h[3 * 16 + 1] = "";
    for (int i = 0; i < n && i < 16; i++) {
        snprintf(h + 3 * i, 4, "%02X ", b[i]);
    }
    ESP_LOGI(TAG, "tpd %s: %s %s", what, esp_err_to_name(e), h);
}
static int touch_points_raw(int *x, int *y, int max);
// (the event log: each read, the reads with a finger, the slowest read)
int board_touch_points(int *x, int *y, int max)
{
    const int64_t t0 = esp_timer_get_time();
    int n = touch_points_raw(x, y, max);
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    s_ev_tp_reads++;
    if (n > 0) {
        s_ev_tp_fing++;
    }
    if (us > s_ev_tp_max_us) {
        s_ev_tp_max_us = us;
    }
    return n;
}
static int touch_points_raw(int *x, int *y, int max)
{
    int n = 0;
    if (s_tp_kind == TP_GT911) {
        uint8_t r[2] = {0x81, 0x4E}, st = 0;
        if (i2c_master_transmit_receive(s_tp, r, 2, &st, 1, I2C_TIMEOUT_MS) != ESP_OK || !(st & 0x80)) {
            return 0;
        }
        int cnt = st & 0x0F;
        if (cnt > 5) {
            cnt = 0;
        }
        uint8_t buf[5 * 8];
        uint8_t r2[2] = {0x81, 0x4F};
        if (cnt && i2c_master_transmit_receive(s_tp, r2, 2, buf, cnt * 8, I2C_TIMEOUT_MS) == ESP_OK) {
            for (int i = 0; i < cnt && n < max; i++) {
                const uint8_t *p = buf + i * 8;
                n += to_phys(p[1] | p[2] << 8, p[3] | p[4] << 8, &x[n], &y[n]);
            }
        }
        uint8_t clr[3] = {0x81, 0x4E, 0};
        i2c_master_transmit(s_tp, clr, 3, I2C_TIMEOUT_MS);
    } else if (s_tp_kind == TP_ST) {
        // (as M5GFX Touch_ST7123: the status (0x0010, bit 3 = with coordinates), the count of report slots (0x0009, 10),
        // then ALL the slots in ONE read (7 bytes each). A read of only 5 slots leaves the chip on its first
        // report: one tap then looks like a finger held still for ever)
        uint8_t adv = 0;
        const int ok_adv = st_read(0x0010, &adv, 1);
        uint8_t maxt = 0;
        uint8_t rep[10 * 7] = {0};
        int got = 0;
        if (ok_adv && (adv & 0x08)) {
            st_read(0x0009, &maxt, 1);
            if (maxt > 10) {
                maxt = 10;
            }
            got = maxt && st_read(0x0014, rep, maxt * 7);
        }
        if (s_tpd_until) {
            // (the diagnosis: the status (bit 7 here: the INT line low), the slot count, the first two slots)
            uint8_t d[16] = {(uint8_t)(adv | (gpio_get_level(PIN_TP_INT) == 0 ? 0x80 : 0)), maxt};
            memcpy(d + 2, rep, 14);
            tpd_log("st", ok_adv ? ESP_OK : ESP_FAIL, d, 16);
        }
        if (!got) {
            return 0;
        }
        for (int i = 0; i < maxt && n < max; i++) {
            const uint8_t *p = rep + i * 7;
            if (!(p[0] & 0x80)) {
                continue;  // (not valid)
            }
            n += to_phys((p[0] & 0x3F) << 8 | p[1], p[2] << 8 | p[3], &x[n], &y[n]);
        }
    }
    return n;
}
int board_touch_int_gpio(void)
{
    return PIN_TP_INT;
}
void board_touch_int_ack(void)
{
}

// ---------------------------------------------------------------- battery, power

// INA226: the bus voltage (0x02, 1.25 mV a step) of the 2-cell battery; the charge from the voltage of one cell
// (3.30 V empty, 4.15 V full). CHG_STAT (0x44 P6): low while it charges (NOT CHECKED on a board).
int board_battery(bool *charging, int *mv)
{
    static int64_t s_at;
    static int s_pct = -1, s_mv;
    static bool s_chg;
    const int64_t now = esp_timer_get_time();
    if (s_at == 0 || now - s_at > 2000000) {
        s_at = now;
        uint8_t b[2], in = 0xFF;
        if (reg_read(s_ina, 0x02, b, 2) == ESP_OK) {
            const int pack = ((b[0] << 8) | b[1]) * 5 / 4;
            s_mv = pack / 2;
            const int p = (s_mv - 3300) * 100 / (4150 - 3300);
            s_pct = pack < 1000 ? -1 : p < 0 ? 0 : p > 100 ? 100 : p;
        }
        if (reg_read(s_io2, PI_IN, &in, 1) == ESP_OK) {
            s_chg = !(in & IO2_CHG_STAT);
        }
    }
    if (charging) {
        *charging = s_chg;
    }
    if (mv) {
        *mv = s_mv;
    }
    return s_pct;
}

// The external power from the battery current (INA226 shunt voltage, 0x01: signed, 2.5 uV a step). The board takes
// about 0.3..1 A from the battery when no charger is there; with a charger and a full battery the current is near 0.
// A small current = external power. A big current in the charge direction cannot be told from a discharge without a
// board test of the sign: then -1 (power.c uses the voltage trend; a charge makes the voltage rise).
#define SHUNT_EXT_UV 250
static int s_shunt_uv = INT32_MIN;
int board_bat_shunt_uv(void)
{
    uint8_t b[2];
    if (s_ina && reg_read(s_ina, 0x01, b, 2) == ESP_OK) {
        s_shunt_uv = (int16_t)((b[0] << 8) | b[1]) * 5 / 2;
    }
    return s_shunt_uv;
}

int board_ext_power(void)
{
    const int uv = board_bat_shunt_uv();
    if (uv == INT32_MIN) {
        return -1;
    }
    return (uv < 0 ? -uv : uv) < SHUNT_EXT_UV ? 1 : -1;
}

bool board_usb_power_out(bool on)
{
    io_set(s_io2, &s_io2_out, IO2_USB5V, on);
    return true;
}

// The power off: pulses on PWROFF_PULSE (M5Unified: 10 changes, 50 ms apart).
void board_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    board_backlight(0);
    io_set(s_io1, &s_io1_out, IO1_SPK, false);
    for (int i = 0; i < 10; i++) {
        io_set(s_io2, &s_io2_out, IO2_PWROFF, i & 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGW(TAG, "still on (USB power): deep sleep");
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------- the microSD card

static sd_pwr_ctrl_handle_t s_sd_pwr;

esp_err_t board_sd_card_mount(const esp_vfs_fat_mount_config_t *mc, sdmmc_card_t **card)
{
    if (!s_sd_pwr) {
        sd_pwr_ctrl_ldo_config_t lc = {.ldo_chan_id = 4};
        if (sd_pwr_ctrl_new_on_chip_ldo(&lc, &s_sd_pwr) != ESP_OK) {
            s_sd_pwr = NULL;
        }
    }
    // (one try at 20 MHz: on the P4 a failed mount clears the SDMMC host that the WiFi link to the C6 shares (slot 1);
    // a second try then failed, and the WiFi too)
    static const int FREQ[1] = {SDMMC_FREQ_DEFAULT};
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 1 && err != ESP_OK; i++) {
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.slot = SDMMC_HOST_SLOT_0;
        host.max_freq_khz = FREQ[i];
        host.pwr_ctrl_handle = s_sd_pwr;
        sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
        slot.width = 4;
        slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        sd_dma_set(&host);
        err = esp_vfs_fat_sdmmc_mount(BOARD_SD_ROOT, &host, &slot, mc, card);
        sd_dma_result(err == ESP_OK);
    }
    if (err == ESP_OK) {
        // (the host has one timeout register for both slots: a clock change of the WiFi link on slot 1 can set the
        // 100 ms of the driver again)
        board_sd_data_timeout(*card);
    }
    return err;
}

// ---------------------------------------------------------------- init

// THE BOOT MENU (Tab5Boot: TFN OS in ota_0 / ota_1, M5PaperDOS-X in ota_2, the menu in the factory slot).
// The bootloader starts the menu after each reset, except when an app asks for one start of a slot (boot_req.h). A
// restart of TFN OS (an update, a restart from SETUP) asks for the TFN slot of the next start, so it does not stop in
// the menu. (A power cut or the reset key: the menu.)
static void boot_req_tfn(void)
{
#if CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC
    const esp_partition_t *p = esp_ota_get_boot_partition();
    if (!p || (p->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 && p->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1)) {
        p = esp_ota_get_running_partition();
    }
    if (!p || (p->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 && p->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1)) {
        return;
    }
    const uint32_t slot = (uint32_t)(p->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0);
    const boot_req_t r = {.magic = BOOT_REQ_MAGIC, .slot = slot, .check = ~slot};
    memcpy(bootloader_common_get_rtc_retain_mem()->custom, &r, sizeof r);
#endif
}

// one start of the OTA slot n, now (the console "bootslot": tests of the boot menu)
static int s_boot_slot = -1;
static void boot_req_slot(void)
{
#if CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC
    const uint32_t slot = (uint32_t)s_boot_slot;
    const boot_req_t r = {.magic = BOOT_REQ_MAGIC, .slot = slot, .check = ~slot};
    memcpy(bootloader_common_get_rtc_retain_mem()->custom, &r, sizeof r);
#endif
}
void board_boot_slot(int slot)
{
    if (slot < 0 || slot > 15) {
        return;
    }
    s_boot_slot = slot;
    esp_unregister_shutdown_handler(boot_req_tfn);
    esp_register_shutdown_handler(boot_req_slot);
    esp_restart();
}

void board_init(void)
{
    esp_register_shutdown_handler(boot_req_tfn);
    esp_register_shutdown_handler(board_sd_off_at_restart);
    board_sd_init();
    i2c_init();
    io_init();
    backlight_init();
    const int fw = touch_detect();
    s_tp_kind = fw == 1 || fw == 3 ? TP_ST : fw == 0 ? TP_GT911 : TP_NONE;
    if (!lcd_init(fw)) {
        ESP_LOGE(TAG, "Tab5: NO SCREEN");
    }
    ESP_LOGI(TAG, "Tab5: panel %s, touch %s, the picture 720 x 1280 from 240 x 320", s_panel_name,
             s_tp_kind == TP_ST      ? "ST"
             : s_tp_kind == TP_GT911 ? "GT911"
                                     : "none");
}

#endif  // CONFIG_TFN_TARGET_TAB5
