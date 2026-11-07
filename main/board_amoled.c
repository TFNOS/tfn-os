// Board support for the Waveshare ESP32-S3-Touch-AMOLED-1.8. It has the same API as board.c
// (see board.h). The build uses this file in place of board.c when CONFIG_TFN_TARGET_AMOLED18
// is on.
//
// Screen: SH8601 AMOLED, 368 x 448 (portrait), QSPI (CS 12, CLK 11, D0..D3 4..7), no backlight
//         (the brightness is the command 51h). The controller cannot exchange rows and columns.
// Touch:  FT3168 at 0x38 (the FT5x06 register map), INT 21. Native frame = the panel frame.
// I2C:    one bus (SDA 15, SCL 14): FT3168 0x38, TCA9554 0x20, AXP2101 0x34, ES8311 0x18,
//         QMI8658 0x6B, PCF85063 0x51.
// IO:     TCA9554 P0, P1, P2 = the reset lines of the screen and the touch (low, then high).
// Power:  AXP2101. This code does not change its rails (the start-up values power the board);
//         it reads the battery and the PWR key, and it stops the power.
// Sound:  ES8311 (speaker and microphone, audio.c); the amplifier on GPIO46 (high = on).
// SD:     SDMMC 1-bit (CLK 2, CMD 1, D0 3). BOOT key: GPIO0 (low = down; a short push = back).
//
// The picture: the apps draw the CoreS3 layout (the physical frame of gfx.h is 240 x 320). The
// flush task scales it by 7/5 to 336 x 448 with a smooth (bilinear) filter and turns it for the
// orientation in the same pass; columns 16..351 of the panel get the picture, and the two bands
// of 16 columns stay black. Two index tables (for the panel columns and rows) give the place in
// the logical frame, so one loop does all four orientations.

#include "tfn_util.h"
#include "sdkconfig.h"

#if CONFIG_TFN_TARGET_AMOLED18

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "board.h"
#include "board_i2c.h"
#include "pmic_axp2101.h"
#include "files.h"
#include "gfx.h"
#include "board_sd.h"
#include "settings.h"
#include "notify.h"

_Static_assert(LCD_W == 240 && LCD_H == 320, "the AMOLED uses the CoreS3 frame: LCD_W 240, LCD_H 320 in board.h");

static const char *TAG = "board";

#define PIN_I2C_SDA GPIO_NUM_15
#define PIN_I2C_SCL GPIO_NUM_14
#define PIN_LCD_CS  GPIO_NUM_12
#define PIN_LCD_CLK GPIO_NUM_11
#define PIN_LCD_D0  GPIO_NUM_4
#define PIN_LCD_D1  GPIO_NUM_5
#define PIN_LCD_D2  GPIO_NUM_6
#define PIN_LCD_D3  GPIO_NUM_7
#define PIN_TP_INT  GPIO_NUM_21
#define PIN_PA      GPIO_NUM_46
#define PIN_BOOT    GPIO_NUM_0
#define PIN_SD_CLK  GPIO_NUM_2
#define PIN_SD_CMD  GPIO_NUM_1
#define PIN_SD_D0   GPIO_NUM_3

#define TCA_ADDR 0x20
#define TP_ADDR  0x38

#define PKEY_SHORT 0x08
#define PKEY_ALL   0x0F

#define TCA_OUT    0x01
#define TCA_CFG    0x03  // bit = 1: input
#define TCA_RESETS 0x07  // P0, P1, P2

#define TP_TD_STATUS 0x02
#define TP_CTRL      0x86  // 1 = go to the monitor mode after the time in 0x87 with no touch (then no I2C answer)
#define TP_MON_TIME  0x87  // the idle time before the monitor mode, s (the chip: 10; 255 = the longest)
#define TP_PMODE     0xA5  // the power mode: 0 = active
#define TP_EVENT_UP  1

// the panel and the picture on it
#define PANEL_W       368
#define PANEL_H       448
#define PIC_X0        16   // the first panel column of the picture
#define PIC_W         336  // 240 * 7 / 5
#define PIC_H         448  // 320 * 7 / 5
#define LCD_HOST      SPI2_HOST
#define LCD_PCLK_HZ   (CONFIG_TFN_LCD_PCLK_MHZ * 1000 * 1000)
#define QSPI_OP_CMD   0x02u
#define QSPI_OP_COLOR 0x32u
// (8 rows: with 16 rows the two DMA chunks, the band and the scaled rows take about 49 KB of internal RAM. The AMOLED
// then has about 1 KB free, the WiFi loses packets ("mem fail") and the SHA-256 check of an update cannot start. 8 rows
// take about 26 KB)
#define CHUNK_ROWS   8
#define CHUNK_PIXELS (PIC_W * CHUNK_ROWS)
#define CHUNK_BYTES  (CHUNK_PIXELS * 2)
#define CHUNKS       (PIC_H / CHUNK_ROWS)  // 56

#define KEY_LATCH_MS 150
#define KEY_POLL_MS  30

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_axp, s_tca, s_tp;
static volatile uint32_t s_key_until;
static uint32_t s_key_polled;
static bool s_boot_down;

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_dma_free;
static uint16_t *s_dma_buf[2];
static QueueHandle_t s_flush_q;
typedef struct {
    const uint16_t *fb;
    int orient;
} frame_t;
static SemaphoreHandle_t s_flush_idle;
static uint8_t s_bright = 0;  // the 51h value now
static bool s_spk_on = true, s_scr_sleep;

// ---------------------------------------------------------------- I2C

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
    s_axp = add_dev(s_bus, AXP_ADDR);
    s_tca = add_dev(s_bus, TCA_ADDR);
    s_tp = add_dev(s_bus, TP_ADDR);
}

board_ver_t board_version(void)
{
    return BOARD_V2;
}

i2c_master_bus_handle_t board_i2c0(void)
{
    return s_bus;
}

// ---------------------------------------------------------------- AXP2101, TCA9554
static uint8_t s_pmu_on, s_pmu_off;  // the sources of the last turn-on and turn-off (AXP2101 0x20, 0x21) at the start
static int s_pmu_rst;                // the reset reason of the ESP32 at the start
static bool s_a3_off;                // the guard turned A3V3 (ALDO1) off
static uint32_t s_guard_ms;          // the time of the last check of the guard (0 = not started)

static void axp_init(void)
{
    axp2101_init_basic(s_axp, &s_pmu_on, &s_pmu_off);
    s_pmu_rst = (int)esp_reset_reason();
    board_pmu_log(0);
}

// THE POWER LOG (to find a fault where the board drops off the USB and the WiFi together and starts again from zero,
// with no log line before it): the reset reason of the ESP32, the sources of the last turn-on and turn-off of the AXP2101 (at
// the start), and now: the input current limit, the status, the voltages (VBUS, VSYS, the battery), the die
// temperature. secs > 0: once each second for that time (the console "pmu N").
static const char *rst_name(int r)
{
    static const char *const N[] = {"?",        "power-on", "external",     "software",  "panic", "int-wdt",
                                    "task-wdt", "wdt",      "deep-sleep",   "brownout",  "sdio",  "usb",
                                    "jtag",     "efuse",    "power-glitch", "cpu-lockup"};
    return r >= 0 && r < (int)(sizeof N / sizeof N[0]) ? N[r] : "?";
}
void board_pmu_log(int secs)
{
    static const char *const ON[] = {"key", "battery", "vbus", "charge", "irq", "en"};
    static const char *const OFF[] = {"key-long",  "software", "pwron-high", "vsys-low",
                                      "vbus-high", "dcdc-low", "dcdc-high",  "hot"};
    char on[64] = "", off[96] = "";
    for (int i = 0; i < 6; i++) {
        if (s_pmu_on & (1 << i)) {
            strlcat(on, ON[i], sizeof on), strlcat(on, " ", sizeof on);
        }
    }
    for (int i = 0; i < 8; i++) {
        if (s_pmu_off & (1 << i)) {
            strlcat(off, OFF[i], sizeof off), strlcat(off, " ", sizeof off);
        }
    }
    ESP_LOGW(TAG, "pmu: the start: ESP32 reset %d (%s); AXP2101 on 0x%02X (%s) off 0x%02X (%s); A3V3 (sound) %s",
             s_pmu_rst, rst_name(s_pmu_rst), s_pmu_on, on, s_pmu_off, off, s_a3_off ? "OFF (the guard)" : "on");
    if (secs > 0) {
        // the raw registers: the power path, the charger, the die temperature limit (0x10..0x2F, 0x60..0x6F) and the
        // rails (0x80..0x9F)
        static const uint8_t FROM[] = {0x10, 0x60, 0x80};
        static const uint8_t LEN[] = {32, 16, 32};
        for (int k = 0; k < 3; k++) {
            uint8_t r[32];
            char h[3 * 32 + 1];
            if (reg_read(s_axp, FROM[k], r, LEN[k]) != ESP_OK) {
                continue;
            }
            for (int i = 0; i < LEN[k]; i++) {
                snprintf(h + 3 * i, 4, "%02X ", r[i]);
            }
            ESP_LOGW(TAG, "pmu: regs 0x%02X: %s", FROM[k], h);
        }
    }
    // the load of each core in the last second (100 % - the time of its idle task): the heat of the power chip comes
    // from the current of the board
    uint64_t idle0 = ulTaskGetIdleRunTimeCounterForCore(0), idle1 = ulTaskGetIdleRunTimeCounterForCore(1);
    int64_t tw = esp_timer_get_time();
    for (int t = 0; t <= secs; t++) {
        const uint64_t i0 = ulTaskGetIdleRunTimeCounterForCore(0), i1 = ulTaskGetIdleRunTimeCounterForCore(1);
        const int64_t now = esp_timer_get_time(), dw = now - tw > 0 ? now - tw : 1;
        const int busy0 = 100 - (int)((i0 - idle0) * 100 / (uint64_t)dw),
                  busy1 = 100 - (int)((i1 - idle1) * 100 / (uint64_t)dw);
        idle0 = i0, idle1 = i1, tw = now;
        if (t > 0) {
            ESP_LOGW(TAG, "pmu: busy core0 %d %%, core1 %d %%", busy0, busy1);
        }
        uint8_t st1 = 0, st2 = 0, lim = 0, offen = 0;
        reg_read(s_axp, 0x00, &st1, 1);
        reg_read(s_axp, 0x01, &st2, 1);
        reg_read(s_axp, 0x16, &lim, 1);
        reg_read(s_axp, 0x22, &offen, 1);
        static const int LIM_MA[] = {100, 500, 900, 1000, 1500, 2000, 0, 0};
        static const char *const CHG[] = {"tri", "pre", "cc", "cv", "done", "not", "?", "?"};
        static const char *const DIR[] = {"standby", "charge", "discharge", "?"};
        const int tr = axp2101_adc14(s_axp, 0x3C);
        ESP_LOGW(TAG,
                 "pmu: vbus %d mV, vsys %d mV, bat %d mV; vbus good %d, input limited %d, bat present %d, vindpm %d; "
                 "charge %s, current %s; input limit %d mA; off-enable 0x%02X; die %d C; up %lld s",
                 axp2101_adc14(s_axp, 0x38), axp2101_adc14(s_axp, 0x3A), axp2101_adc14(s_axp, 0x34), (st1 >> 5) & 1,
                 st1 & 1, (st1 >> 3) & 1, (st2 >> 3) & 1, CHG[st2 & 7], DIR[(st2 >> 5) & 3], LIM_MA[lim & 7], offen,
                 tr >= 0 ? 22 + (7274 - tr) / 20 : -99, esp_timer_get_time() / 1000000);
        if (t < secs) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

// THE A3V3 GUARD: ALDO1 of the AXP2101 = A3V3, the analog supply of the ES8311 and the microphone. A
// short on it (a damaged part) heats the AXP2101 about 3-5 C each second to its 135 C turn-off, also with an idle
// firmware. Each second: the die temperature; at GUARD_C the guard turns ALDO1 off (no sound, no microphone; the board
// stays on and cool) and keeps it off at the next starts (store pmu_a3off). A healthy board stays far below GUARD_C.
#define GUARD_C 95
static void a3_set(bool on)
{
    uint8_t v = 0;
    if (reg_read(s_axp, AXP_LDO_EN, &v, 1) != ESP_OK) {
        return;
    }
    reg_write(s_axp, AXP_LDO_EN, on ? (v | 0x01) : (v & ~0x01));
    s_a3_off = !on;
}
void board_pmu_a3(bool on)
{
    store_set("pmu_a3off", on ? 0 : 1);
    a3_set(on);
    ESP_LOGW(TAG, "pmu: A3V3 (sound, microphone) %s", on ? "on (the guard is ready again)" : "off");
}
static void pmu_guard(uint32_t now)
{
    if (s_guard_ms && now - s_guard_ms < 1000) {
        return;
    }
    if (!s_guard_ms && store_get("pmu_a3off", 0)) {
        a3_set(false);
        ESP_LOGW(TAG, "pmu: A3V3 (sound, microphone) stays off: the guard turned it off before (console: pmu a3 on)");
    }
    s_guard_ms = now ? now : 1;
    if (s_a3_off) {
        return;
    }
    const int tr = axp2101_adc14(s_axp, 0x3C);
    const int die = tr >= 0 ? 22 + (7274 - tr) / 20 : 0;
    if (tr < 0 || die < GUARD_C) {
        return;
    }
    a3_set(false);
    store_set("pmu_a3off", 1);
    ESP_LOGE(TAG, "pmu: THE POWER CHIP IS AT %d C: A3V3 (sound, microphone) OFF", die);
    notify_post("SYSTEM", "POWER CHIP HOT", "Sound and microphone are off to protect the board.", "");
}

// The reset lines of the screen and the touch: low for 200 ms, then high.
static void tca_resets(void)
{
    uint8_t out = 0xFF, cfg = 0xFF;
    reg_read(s_tca, TCA_OUT, &out, 1);
    reg_read(s_tca, TCA_CFG, &cfg, 1);
    reg_write(s_tca, TCA_OUT, out & (uint8_t)~TCA_RESETS);
    reg_write(s_tca, TCA_CFG, cfg & (uint8_t)~TCA_RESETS);
    vTaskDelay(pdMS_TO_TICKS(200));
    reg_write(s_tca, TCA_OUT, out | TCA_RESETS);
    vTaskDelay(pdMS_TO_TICKS(150));
}

// ---------------------------------------------------------------- the screen

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dma_free, &woken);
    return woken == pdTRUE;
}

static void lcd_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, (int)((QSPI_OP_CMD << 24) | ((uint32_t)cmd << 8)), data, len));
}

static void lcd_window(int x0, int x1, int y0, int y1)
{
    lcd_cmd(0x2A, (uint8_t[]){(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1}, 4);
    lcd_cmd(0x2B, (uint8_t[]){(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1}, 4);
}

// Black on the whole panel (the bands at the sides stay black after this).
static void lcd_clear(void)
{
    lcd_window(0, PANEL_W - 1, 0, PANEL_H - 1);
    const int rows = 7, bytes = PANEL_W * rows * 2;  // (448 / 7 = 64; 5152 bytes fit a DMA buffer)
    for (int c = 0; c < PANEL_H / rows; c++) {
        xSemaphoreTake(s_dma_free, portMAX_DELAY);
        uint16_t *dst = s_dma_buf[c & 1];
        memset(dst, 0, bytes);
        uint32_t cmd = (QSPI_OP_COLOR << 24) | ((c == 0 ? 0x2Cu : 0x3Cu) << 8);
        esp_lcd_panel_io_tx_color(s_io, (int)cmd, dst, bytes);
    }
}

// The bilinear scale. A colour in the "spread" form 00000GGGGGG00000RRRRR000000BBBBB has room
// for the sums of two colours times a weight of 0..32.
#define SPREAD_MASK 0x07E0F81Fu
static inline uint32_t spread(uint16_t c)
{
    return ((uint32_t)c | ((uint32_t)c << 16)) & SPREAD_MASK;
}
static inline uint32_t mix(uint32_t a, uint32_t b, uint32_t w)  // w 0..32: the part of b
{
    return ((a * (32 - w) + b * w) >> 5) & SPREAD_MASK;
}

static int s_col_of[LCD_W], s_row_of[LCD_H];  // the place in fb of the physical (px, py)
#define BAND_MAX (CHUNK_ROWS * 5 / 7 + 3)     // physical rows for one chunk (16 rows: 14; 8 rows: 8)
static uint16_t *s_band;                      // BAND_MAX x LCD_W, colours (bytes not swapped)
static uint32_t *s_hrow;                      // BAND_MAX x PIC_W, scaled rows in spread form
static bool s_land;

static void tables_init(void)
{
    s_band = heap_caps_malloc(BAND_MAX * LCD_W * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_hrow = heap_caps_malloc(BAND_MAX * PIC_W * sizeof(uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(s_band && s_hrow);
}

// The place in the logical frame of each physical point, for an orientation (the inverse of
// gfx_touch_to_logical): idx = s_row_of[py] + s_col_of[px].
static void tables_orient(int orient)
{
    s_land = orient == ORIENT_LAND_A || orient == ORIENT_LAND_B;
    for (int px = 0; px < LCD_W; px++) {
        switch (orient) {
        case ORIENT_PORTRAIT:
            s_col_of[px] = px;
            break;
        case ORIENT_PORTRAIT_FLIP:
            s_col_of[px] = LCD_W - 1 - px;
            break;
        case ORIENT_LAND_A:
            s_col_of[px] = (LCD_W - 1 - px) * LCD_H;
            break;
        default:
            s_col_of[px] = px * LCD_H;
            break;
        }
    }
    for (int py = 0; py < LCD_H; py++) {
        switch (orient) {
        case ORIENT_PORTRAIT:
            s_row_of[py] = py * LCD_W;
            break;
        case ORIENT_PORTRAIT_FLIP:
            s_row_of[py] = (LCD_H - 1 - py) * LCD_W;
            break;
        case ORIENT_LAND_A:
            s_row_of[py] = py;
            break;
        default:
            s_row_of[py] = LCD_H - 1 - py;
            break;
        }
    }
}

// The physical rows py0 .. py0 + n - 1 into s_band, then scaled to PIC_W into s_hrow. In
// landscape a physical row is a logical column: the outer loop goes along the logical rows, so
// the reads from PSRAM come in memory order.
static void band_load(const uint16_t *fb, int py0, int n)
{
    if (s_land) {
        for (int px = 0; px < LCD_W; px++) {
            const uint16_t *src = fb + s_col_of[px];
            for (int k = 0; k < n; k++) {
                s_band[k * LCD_W + px] = src[s_row_of[py0 + k]];
            }
        }
    } else {
        for (int k = 0; k < n; k++) {
            const uint16_t *row = fb + s_row_of[py0 + k];
            uint16_t *dst = s_band + k * LCD_W;
            for (int px = 0; px < LCD_W; px++) {
                dst[px] = row[s_col_of[px]];
            }
        }
    }
    // 5 source pixels make 7 picture pixels, with the same weights in each group (x * 5 / 7)
    for (int k = 0; k < n; k++) {
        const uint16_t *row = s_band + k * LCD_W;
        uint32_t *out = s_hrow + k * PIC_W;
        uint32_t a0 = spread(__builtin_bswap16(row[0]));
        for (int g = 0; g < LCD_W / 5; g++, row += 5, out += 7) {
            uint32_t a1 = spread(__builtin_bswap16(row[1]));
            uint32_t a2 = spread(__builtin_bswap16(row[2]));
            uint32_t a3 = spread(__builtin_bswap16(row[3]));
            uint32_t a4 = spread(__builtin_bswap16(row[4]));
            uint32_t a5 = g + 1 < LCD_W / 5 ? spread(__builtin_bswap16(row[5])) : a4;
            out[0] = a0;
            out[1] = mix(a0, a1, 22);
            out[2] = mix(a1, a2, 13);
            out[3] = mix(a2, a3, 4);
            out[4] = mix(a2, a3, 27);
            out[5] = mix(a3, a4, 18);
            out[6] = mix(a4, a5, 9);
            a0 = a5;
        }
    }
}

static void lcd_send(const uint16_t *fb, int orient)
{
    static int s_orient = -1;
    if (orient != s_orient) {
        tables_orient(orient);
        s_orient = orient;
    }
    lcd_window(PIC_X0, PIC_X0 + PIC_W - 1, 0, PIC_H - 1);
    for (int c = 0; c < CHUNKS; c++) {
        int oy0 = c * CHUNK_ROWS;
        int py0 = oy0 * 5 / 7, py1 = ((oy0 + CHUNK_ROWS - 1) * 5) / 7 + 1;
        if (py1 > LCD_H - 1) {
            py1 = LCD_H - 1;
        }
        band_load(fb, py0, py1 - py0 + 1);
        xSemaphoreTake(s_dma_free, portMAX_DELAY);
        uint16_t *dst = s_dma_buf[c & 1];
        for (int r = 0; r < CHUNK_ROWS; r++) {
            int n = (oy0 + r) * 5, py = n / 7;
            uint32_t w = (uint32_t)((n % 7) * 32 / 7);
            const uint32_t *l0 = s_hrow + (py - py0) * PIC_W;
            uint16_t *o = dst + r * PIC_W;
            // two pixels in each 32-bit store (the bytes of each pixel swapped for the bus)
            uint32_t *o2 = (uint32_t *)o;
            if (w && py + 1 <= py1) {
                const uint32_t *l1 = l0 + PIC_W;
                for (int x = 0; x < PIC_W; x += 2) {
                    uint32_t v0 = mix(l0[x], l1[x], w), v1 = mix(l0[x + 1], l1[x + 1], w);
                    v0 = (v0 | (v0 >> 16)) & 0xFFFF;
                    v1 = (v1 | (v1 >> 16)) & 0xFFFF;
                    uint32_t p = v0 | (v1 << 16);
                    o2[x >> 1] = ((p & 0x00FF00FFu) << 8) | ((p >> 8) & 0x00FF00FFu);
                }
            } else {
                for (int x = 0; x < PIC_W; x += 2) {
                    uint32_t v0 = l0[x], v1 = l0[x + 1];
                    v0 = (v0 | (v0 >> 16)) & 0xFFFF;
                    v1 = (v1 | (v1 >> 16)) & 0xFFFF;
                    uint32_t p = v0 | (v1 << 16);
                    o2[x >> 1] = ((p & 0x00FF00FFu) << 8) | ((p >> 8) & 0x00FF00FFu);
                }
            }
        }
        uint32_t cmd = (QSPI_OP_COLOR << 24) | ((c == 0 ? 0x2Cu : 0x3Cu) << 8);
        esp_lcd_panel_io_tx_color(s_io, (int)cmd, dst, CHUNK_BYTES);
    }
}

// ---- the native frame (an app with a native layout): the whole panel, turned, not scaled

#define NAT_ROWS 7  // 448 / 7 = 64 chunks of 5152 bytes (the DMA buffer: 5376)
_Static_assert(PANEL_W * NAT_ROWS * 2 <= CHUNK_BYTES, "a native chunk must fit a DMA buffer");
_Static_assert(PANEL_W * 7 * 2 <= CHUNK_BYTES && PANEL_H % 7 == 0 && PANEL_H % NAT_ROWS == 0,
               "lcd_clear and the native chunks");
static int s_ncol_of[PANEL_W], s_nrow_of[PANEL_H];

static void tables_native(int orient)
{
    for (int x = 0; x < PANEL_W; x++) {
        switch (orient) {
        case ORIENT_PORTRAIT:
            s_ncol_of[x] = x;
            break;
        case ORIENT_PORTRAIT_FLIP:
            s_ncol_of[x] = PANEL_W - 1 - x;
            break;
        case ORIENT_LAND_A:
            s_ncol_of[x] = (PANEL_W - 1 - x) * PANEL_H;
            break;
        default:
            s_ncol_of[x] = x * PANEL_H;
            break;
        }
    }
    for (int y = 0; y < PANEL_H; y++) {
        switch (orient) {
        case ORIENT_PORTRAIT:
            s_nrow_of[y] = y * PANEL_W;
            break;
        case ORIENT_PORTRAIT_FLIP:
            s_nrow_of[y] = (PANEL_H - 1 - y) * PANEL_W;
            break;
        case ORIENT_LAND_A:
            s_nrow_of[y] = y;
            break;
        default:
            s_nrow_of[y] = PANEL_H - 1 - y;
            break;
        }
    }
}

static void lcd_send_native(const uint16_t *fb, int orient)
{
    static int s_o = -1;
    if (orient != s_o) {
        tables_native(orient);
        s_o = orient;
    }
    bool land = orient == ORIENT_LAND_A || orient == ORIENT_LAND_B;
    lcd_window(0, PANEL_W - 1, 0, PANEL_H - 1);
    for (int c = 0; c < PANEL_H / NAT_ROWS; c++) {
        xSemaphoreTake(s_dma_free, portMAX_DELAY);
        uint16_t *dst = s_dma_buf[c & 1];
        const int *rows = s_nrow_of + c * NAT_ROWS;
        if (land) {
            // a panel row is a logical column: the inner loop goes along a logical row (memory order)
            for (int x = 0; x < PANEL_W; x++) {
                const uint16_t *src = fb + s_ncol_of[x];
                for (int k = 0; k < NAT_ROWS; k++) {
                    dst[k * PANEL_W + x] = src[rows[k]];
                }
            }
        } else {
            for (int k = 0; k < NAT_ROWS; k++) {
                const uint16_t *row = fb + rows[k];
                uint16_t *o = dst + k * PANEL_W;
                for (int x = 0; x < PANEL_W; x++) {
                    o[x] = row[s_ncol_of[x]];
                }
            }
        }
        uint32_t cmd = (QSPI_OP_COLOR << 24) | ((c == 0 ? 0x2Cu : 0x3Cu) << 8);
        esp_lcd_panel_io_tx_color(s_io, (int)cmd, dst, PANEL_W * NAT_ROWS * 2);
    }
}

static void flush_task(void *arg)
{
    frame_t f;
    for (;;) {
        xQueueReceive(s_flush_q, &f, portMAX_DELAY);
        static bool s_was_native;
        bool native = f.orient & ORIENT_NATIVE;
        if (native) {
            lcd_send_native(f.fb, f.orient & 0xFF);
        } else {
            if (s_was_native) {
                lcd_clear();  // the bands at the sides: black again
            }
            lcd_send(f.fb, f.orient);
        }
        s_was_native = native;
        xSemaphoreGive(s_flush_idle);
    }
}

void board_lcd_flush(const uint16_t *fb, int orient)
{
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    frame_t f = {fb, orient};
    xQueueSend(s_flush_q, &f, portMAX_DELAY);
}

static void lcd_init(void)
{
    spi_bus_config_t bus = {
            .sclk_io_num = PIN_LCD_CLK,
            .data0_io_num = PIN_LCD_D0,
            .data1_io_num = PIN_LCD_D1,
            .data2_io_num = PIN_LCD_D2,
            .data3_io_num = PIN_LCD_D3,
            .data4_io_num = -1,
            .data5_io_num = -1,
            .data6_io_num = -1,
            .data7_io_num = -1,
            .max_transfer_sz = CHUNK_BYTES > PANEL_W * 16 ? CHUNK_BYTES : PANEL_W * 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io = {
            .cs_gpio_num = PIN_LCD_CS,
            .dc_gpio_num = GPIO_NUM_NC,
            .spi_mode = 0,
            .pclk_hz = LCD_PCLK_HZ,
            .trans_queue_depth = 10,
            .on_color_trans_done = on_color_done,
            .lcd_cmd_bits = 32,
            .lcd_param_bits = 8,
            .flags.quad_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &s_io));
    s_dma_free = xSemaphoreCreateCounting(2, 2);
    for (int i = 0; i < 2; i++) {
        s_dma_buf[i] = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        assert(s_dma_buf[i]);
    }
    tables_init();

    // the list of the Waveshare demo (esp_lcd_sh8601), with RGB565
    lcd_cmd(0x11, NULL, 0);  // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x44, (uint8_t[]){0x01, 0xD1}, 2);  // tear scan line
    lcd_cmd(0x35, (uint8_t[]){0x00}, 1);        // tear effect on
    lcd_cmd(0x53, (uint8_t[]){0x20}, 1);        // brightness control on
    lcd_cmd(0x3A, (uint8_t[]){0x55}, 1);        // RGB565
    lcd_cmd(0x36, (uint8_t[]){0x00}, 1);        // MADCTL: RGB order
    lcd_cmd(0x51, (uint8_t[]){0x00}, 1);        // dark until the first frame
    lcd_clear();
    lcd_cmd(0x29, NULL, 0);  // display on
    vTaskDelay(pdMS_TO_TICKS(10));

    s_flush_q = xQueueCreate(1, sizeof(frame_t));
    s_flush_idle = xSemaphoreCreateBinary();
    xSemaphoreGive(s_flush_idle);
    xTaskCreatePinnedToCore(flush_task, "lcd", 3072, NULL, 7, NULL, 1);
}

// The brightness (51h). The command goes between two frames.
void board_backlight(uint8_t level)
{
    if (!s_flush_idle) {
        return;
    }
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    s_bright = level;
    if (!s_scr_sleep) {
        lcd_cmd(0x51, &s_bright, 1);
    }
    xSemaphoreGive(s_flush_idle);
}

// ---------------------------------------------------------------- speaker

static bool s_spk_hold;  // board_speaker_hold(): the amplifier stays on in the screen sleep
void board_speaker(bool on)
{
    s_spk_on = on;
    if (!s_scr_sleep || s_spk_hold) {
        gpio_set_level(PIN_PA, on);
    }
}

void board_speaker_hold(bool hold)
{
    s_spk_hold = hold;
    if (s_scr_sleep) {
        gpio_set_level(PIN_PA, hold && s_spk_on);
    }
}

void board_screen_sleep(bool sleep)
{
    if (sleep == s_scr_sleep) {
        return;
    }
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    if (sleep) {
        lcd_cmd(0x28, NULL, 0);  // display off (the touch is another chip)
        lcd_cmd(0x10, NULL, 0);  // sleep in
        gpio_set_level(PIN_PA, s_spk_hold && s_spk_on);
    } else {
        lcd_cmd(0x11, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        lcd_cmd(0x29, NULL, 0);
        lcd_cmd(0x51, &s_bright, 1);
        gpio_set_level(PIN_PA, s_spk_on);
    }
    s_scr_sleep = sleep;
    xSemaphoreGive(s_flush_idle);
}

// ---------------------------------------------------------------- keys

// A short push of PWR (AXP2101) or of BOOT (GPIO0) gives a KEY_LATCH_MS pulse (back). A push of
// 4 s on PWR stops the power (the AXP2101 does it).
bool board_boot_key_down(void)
{
    return false;
}  // (no second key for CHAT on this board)

bool board_pwr_key_down(void)
{
    uint32_t now = tfn_now_ms32();
    pmu_guard(now);
    if (now - s_key_polled >= KEY_POLL_MS) {
        s_key_polled = now;
        uint8_t st;
        if (reg_read(s_axp, AXP_IRQ_ST1, &st, 1) == ESP_OK && (st & PKEY_ALL)) {
            reg_write(s_axp, AXP_IRQ_ST1, st & PKEY_ALL);
            if (st & PKEY_SHORT) {
                s_key_until = now + KEY_LATCH_MS;
            }
        }
    }
    bool boot = gpio_get_level(PIN_BOOT) == 0;
    if (boot && !s_boot_down) {
        s_key_until = now + KEY_LATCH_MS;
    }
    s_boot_down = boot;
    return (int32_t)(s_key_until - now) > 0;
}

// ---------------------------------------------------------------- touch

// The FT3168 report from TD_STATUS: [0] = points, then 6 bytes for each point. The panel point
// (tx 0..367, ty 0..447) goes to the physical frame of gfx.h (240 x 320) by the inverse scale.
// (the console "tpdiag": the reads of each second, by result)
static struct {
    int64_t until, t0, last_ok;
    uint32_t ok, inv, tmo, oth, touched;
} s_tpd;
void board_tp_diag(int secs)
{
    const int64_t now = esp_timer_get_time() / 1000;
    s_tpd = (typeof(s_tpd)){now + (secs > 120 ? 120 : secs) * 1000, now, now, 0, 0, 0, 0, 0};
    ESP_LOGI(TAG, "tpdiag: %d s", secs);
}
int board_tp_reg(int reg, int val)
{
    uint8_t v = 0;
    const esp_err_t err = val >= 0 ? reg_write(s_tp, (uint8_t)reg, (uint8_t)val) : reg_read(s_tp, (uint8_t)reg, &v, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tp reg 0x%02X: %s", reg, esp_err_to_name(err));
        return -1;
    }
    return val >= 0 ? val : v;
}
static void tp_diag_count(esp_err_t err, int points)
{
    const int64_t now = esp_timer_get_time() / 1000;
    if (err == ESP_OK) {
        s_tpd.ok++;
        s_tpd.last_ok = now;
        if (points) {
            s_tpd.touched++;
        }
    } else if (err == ESP_ERR_INVALID_RESPONSE) {
        s_tpd.inv++;
    } else if (err == ESP_ERR_TIMEOUT) {
        s_tpd.tmo++;
    } else {
        s_tpd.oth++;
    }
    if (now - s_tpd.t0 >= 1000) {
        if (err != ESP_OK) {
            uint8_t t = 0;
            const esp_err_t e2 = reg_read(s_tca, TCA_OUT, &t, 1);
            ESP_LOGI(TAG, "tpd: no answer: the TCA9554 on the same bus %s (0x%02X)", esp_err_to_name(e2), t);
        }
        ESP_LOGI(
                TAG,
                "tpd: ok %lu, no answer %lu, timeout %lu, other %lu; with a finger %lu; the last good read %lld ms ago",
                (unsigned long)s_tpd.ok, (unsigned long)s_tpd.inv, (unsigned long)s_tpd.tmo, (unsigned long)s_tpd.oth,
                (unsigned long)s_tpd.touched, (long long)(now - s_tpd.last_ok));
        s_tpd.t0 = now;
        s_tpd.ok = s_tpd.inv = s_tpd.tmo = s_tpd.oth = s_tpd.touched = 0;
        if (now > s_tpd.until) {
            s_tpd.until = 0;
            ESP_LOGI(TAG, "tpdiag: end");
        }
    }
}
int board_touch_points(int *x, int *y, int max)
{
    uint8_t buf[1 + 6 * 2];
    static uint32_t s_errors;
    esp_err_t err = reg_read(s_tp, TP_TD_STATUS, buf, sizeof(buf));
    if (s_tpd.until) {
        tp_diag_count(err, err == ESP_OK ? (buf[0] & 0x0F) : 0);
    }
    static esp_err_t s_last_err;
    if (s_tpd.until && (err == ESP_OK) != (s_last_err == ESP_OK)) {
        ESP_LOGI(TAG, "tpd: %s at %lld ms", err == ESP_OK ? "answers again" : "no answer",
                 (long long)(esp_timer_get_time() / 1000));
    }
    s_last_err = err;
    // the chip stays active: in its monitor mode it does not answer on I2C (a touch after 13.5 s of rest was slow or
    // lost). 0x86 = 0, the longest idle time (0x87 = 255) and the active mode (0xA5 = 0): written after a good read and
    // again each 10 s (with 255 alone it still went silent once after about 4 minutes with no touch).
    static bool s_active;
    static int64_t s_active_at;
    if (err == ESP_OK) {
        const int64_t now = esp_timer_get_time();
        if (!s_active || now - s_active_at > 10000000) {
            uint8_t v = 0xFF;
            s_active_at = now;
            uint8_t t = 0;
            const esp_err_t e1 = reg_read(s_tp, TP_CTRL, &v, 1), e2 = reg_read(s_tp, TP_MON_TIME, &t, 1);
            const uint8_t v0 = v, t0 = t;
            // (written each time: a write starts the idle time of the chip again)
            if (e1 == ESP_OK) {
                reg_write(s_tp, TP_CTRL, 0);
                v = 0xFF;
                reg_read(s_tp, TP_CTRL, &v, 1);
            }
            if (e2 == ESP_OK) {
                reg_write(s_tp, TP_MON_TIME, 255);
                t = 0;
                reg_read(s_tp, TP_MON_TIME, &t, 1);
            }
            reg_write(s_tp, TP_PMODE, 0);
            if (s_tpd.until) {
                ESP_LOGI(TAG, "tpd: check 0x86 %d -> %d, 0x87 %d -> %d", v0, v, t0, t);
            }
            if (!s_active && v == 0 && t == 255) {
                ESP_LOGI(TAG, "touch: the chip stays active (0x86 = 0, 0x87 = 255)");
            }
            s_active = v == 0 && t == 255;
        }
    }
    if (err != ESP_OK) {
        if ((s_errors++ % 500) == 0) {
            ESP_LOGW(TAG, "touch read failed: %s (%lu times)", esp_err_to_name(err), (unsigned long)s_errors);
        }
        return 0;
    }
    int points = buf[0] & 0x0F;
    if (points > 2) {
        points = 0;
    }
    int n = 0;
    for (int i = 0; i < points && n < max; i++) {
        const uint8_t *p = buf + 1 + 6 * i;
        if ((p[0] >> 6) == TP_EVENT_UP) {
            continue;
        }
        int tx = ((p[0] & 0x0F) << 8) | p[1];
        int ty = ((p[2] & 0x0F) << 8) | p[3];
        int px = (tx - PIC_X0) * 5 / 7, py = ty * 5 / 7;
        x[n] = px < 0 ? 0 : px >= LCD_W ? LCD_W - 1 : px;
        y[n] = py < 0 ? 0 : py >= LCD_H ? LCD_H - 1 : py;
#if CONFIG_TFN_TOUCH_DEBUG
        ESP_LOGI(TAG, "touch %d/%d raw=(%d,%d) screen=(%d,%d)", i + 1, points, tx, ty, x[n], y[n]);
#endif
        n++;
    }
    return n;
}

// ---------------------------------------------------------------- battery, power

int board_battery(bool *charging, int *mv)
{
    return axp2101_battery(s_axp, charging, mv);
}

// the FT3168 INT line (low while it has a touch report)
int board_touch_int_gpio(void)
{
    return PIN_TP_INT;
}
void board_touch_int_ack(void)
{
}

void board_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    lcd_cmd(0x28, NULL, 0);
    lcd_cmd(0x10, NULL, 0);
    gpio_set_level(PIN_PA, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t v;
    if (reg_read(s_axp, AXP_PMU_CFG, &v, 1) != ESP_OK) {
        v = 0x30;
    }
    reg_write(s_axp, AXP_PMU_CFG, v | 0x01);
    vTaskDelay(pdMS_TO_TICKS(500));
    // still here (USB power): sleep until BOOT is pushed
    ESP_LOGW(TAG, "AXP2101 power off failed: deep sleep, BOOT wakes the board");
    esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_BOOT, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------- the microSD card

esp_err_t board_sd_card_mount(const esp_vfs_fat_mount_config_t *mc, sdmmc_card_t **card)
{
    return board_sd_mount_sdmmc_1bit(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, mc, card);
}

// ---------------------------------------------------------------- init

void board_init(void)
{
    board_sd_init();
    gpio_config_t out = {.pin_bit_mask = 1ULL << PIN_PA, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&out);
    gpio_set_level(PIN_PA, 1);
    gpio_config_t in = {.pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&in);
    i2c_init();
    axp_init();
    tca_resets();
    lcd_init();
    ESP_LOGI(TAG, "AMOLED 1.8: screen 368 x 448 (the picture 336 x 448 from 240 x 320), %d MHz",
             CONFIG_TFN_LCD_PCLK_MHZ);
}

#endif  // CONFIG_TFN_TARGET_AMOLED18
