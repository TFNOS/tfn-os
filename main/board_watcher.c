// Board support for the Seeed SenseCAP Watcher (ESP32-S3, 32 MB flash, 8 MB octal PSRAM). It has
// the same API as board.c (see board.h). The build uses this file in place of board.c when
// CONFIG_TFN_TARGET_WATCHER is on. The pins and the power order come from the Seeed BSP
// (components/sensecap-watcher).
//
// Screen: SPD2010, 412 x 412 round, QSPI on SPI3 (CS 45, CLK 7, D0..D3 9, 1, 14, 13), backlight
//         GPIO8 (LEDC). The vendor init list comes from the component espressif/esp_lcd_spd2010.
//         A window must start at a column that is a multiple of 4 and end at 4n + 3.
// Touch:  SPD2010 touch at 0x53 on its own I2C bus (I2C1: SDA 39, SCL 38), read by polling.
// I2C0:   SDA 47, SCL 48: PCA9535 IO expander 0x21, ES8311 0x18, ES7243(E) 0x13/0x14, RTC 0x51.
// IO:     PCA9535 P0 = inputs (P0.0 charging, P0.2 USB power, P0.3 knob button, P0.4 SD card
//         in), P1 = power switches (P1.0 SD, P1.1 LCD, P1.2 SYSTEM (the power latch), P1.3 the AI
//         chip, P1.4 the speaker amplifier, P1.5 battery present (input), P1.6 Grove, P1.7 the
//         battery ADC).
// Sound:  ES8311 speaker and ES7243 microphone on I2S0 (audio.c); the amplifier is P1.4.
// SD:     SPI2 (CLK 4, MOSI 5, MISO 6, CS 46); the Himax AI chip shares the bus (CS 21: kept high).
// Wheel:  a quadrature encoder (A GPIO41, B GPIO42) on the PCNT unit, and its button (P0.3, low =
//         down; board_pwr_key_down()). main.c: a short push = the main action, a hold = back.
// No IMU: imu.c makes the wheel a left/right tilt.
//
// The picture (BOARD_ROUND, BOARD_TOUCH_LOGICAL in board.h). The Watcher has no motion sensor, so
// each frame is shown upright, and the touch points go back as logical points of that frame:
// - a round layout (the native frame, ORIENT_NATIVE): the whole 412 x 412 panel, no scale;
// - the other apps (the CoreS3 frame, 320 x 240 or 240 x 320): zoomed by 412 / 320 with a bilinear
//   filter to 412 x 309 (or 309 x 412), so the picture fills the width of the circle. The corners
//   of the picture are outside the circle.

#include "tfn_util.h"
#include "sdkconfig.h"

#if CONFIG_TFN_TARGET_WATCHER

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/sdspi_host.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_spd2010.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "board.h"
#include "board_i2c.h"
#include "files.h"
#include "gfx.h"
#include "board_sd.h"
#include "sd_dma.h"

_Static_assert(LCD_W == 240 && LCD_H == 320, "the Watcher uses the CoreS3 frame: LCD_W 240, LCD_H 320 in board.h");

static const char *TAG = "board";

#define PIN_I2C_SDA  GPIO_NUM_47
#define PIN_I2C_SCL  GPIO_NUM_48
#define PIN_TP_SDA   GPIO_NUM_39
#define PIN_TP_SCL   GPIO_NUM_38
#define PIN_EXP_INT  GPIO_NUM_2
#define PIN_LCD_CS   GPIO_NUM_45
#define PIN_LCD_CLK  GPIO_NUM_7
#define PIN_LCD_D0   GPIO_NUM_9
#define PIN_LCD_D1   GPIO_NUM_1
#define PIN_LCD_D2   GPIO_NUM_14
#define PIN_LCD_D3   GPIO_NUM_13
#define PIN_LCD_BL   GPIO_NUM_8
#define PIN_SPI_CLK  GPIO_NUM_4
#define PIN_SPI_MOSI GPIO_NUM_5
#define PIN_SPI_MISO GPIO_NUM_6
#define PIN_SD_CS    GPIO_NUM_46
#define PIN_AI_CS    GPIO_NUM_21
#define PIN_WHEEL_A  GPIO_NUM_41
#define PIN_WHEEL_B  GPIO_NUM_42
#define BAT_ADC_CHAN ADC_CHANNEL_2  // GPIO3

#define EXP_ADDR 0x21
#define TP_ADDR  0x53

// PCA9535 registers and pins (bit n of the 16-bit port: P0.n = n, P1.n = 8 + n)
#define EXP_IN0     0x00
#define EXP_OUT0    0x02
#define EXP_OUT1    0x03
#define EXP_CFG0    0x06
#define EXP_CFG1    0x07
#define IO_CHARGING (1u << 0)
#define IO_VBUS     (1u << 2)
#define IO_KNOB_BTN (1u << 3)
#define IO_SD_IN    (1u << 4)
#define IO_TP_INT   (1u << 5)  // the touch chip has data (low)
#define IO_AI_SYNC  (1u << 6)  // the Himax AI chip has data (high)
#define IO_AI_RST   (1u << 7)  // the reset of the Himax AI chip (low = reset)
#define P1_SD       (1u << 0)
#define P1_LCD      (1u << 1)
#define P1_SYSTEM   (1u << 2)
#define P1_AI       (1u << 3)
#define P1_PA       (1u << 4)
#define P1_BAT_DET  (1u << 5)  // an input
#define P1_GROVE    (1u << 6)
#define P1_BAT_ADC  (1u << 7)
// (the idle power: the AI chip and the Grove port are off; board_ai_power() for the web camera)
#define P1_START (P1_SD | P1_LCD | P1_SYSTEM | P1_PA | P1_BAT_ADC)

// the panel and the picture on it
#define PANEL       412
#define LCD_HOST    SPI3_HOST
#define SD_HOST     SPI2_HOST
#define LCD_PCLK_HZ (CONFIG_TFN_LCD_PCLK_MHZ * 1000 * 1000)
#define CHUNK_ROWS  12                        // full panel rows in one DMA buffer
#define CHUNK_BYTES (PANEL * CHUNK_ROWS * 2)  // 9888 bytes
#define ZOOM_W      PANEL                     // the zoomed CoreS3 frame: 412 x 309
#define ZOOM_H      (PANEL * 240 / 320)
#define ORIENT_FIT  0x200  // (frame_t.orient) the CoreS3 frame at 1:1 in the circle

static volatile bool s_fit;  // app_t.round_fit of the open app

void board_round_fit(bool on)
{
    s_fit = on;
}
#define WHEEL_COUNTS 4  // encoder counts for one step (click) of the wheel

#define KEY_POLL_MS 20

static i2c_master_bus_handle_t s_bus, s_tbus;
static i2c_master_dev_handle_t s_exp, s_tp;
static uint8_t s_out1;  // the PCA9535 P1 outputs now
static SemaphoreHandle_t s_exp_mx;

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_dma_free;
static uint16_t *s_dma_buf[2];
static QueueHandle_t s_flush_q;
typedef struct {
    const uint16_t *fb;
    int orient;
} frame_t;
static SemaphoreHandle_t s_flush_idle;
static uint8_t s_bright;
static bool s_spk_on = true, s_scr_sleep, s_spk_hold;

// ---------------------------------------------------------------- I2C, the IO expander

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
    s_exp = add_dev(s_bus, EXP_ADDR);
    i2c_master_bus_config_t tc = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = PIN_TP_SDA,
            .scl_io_num = PIN_TP_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&tc, &s_tbus));
    s_tp = add_dev(s_tbus, TP_ADDR);
}

board_ver_t board_version(void)
{
    return BOARD_V2;
}

i2c_master_bus_handle_t board_i2c0(void)
{
    return s_bus;
}

static void exp_set(uint8_t mask, bool on)
{
    xSemaphoreTake(s_exp_mx, portMAX_DELAY);
    s_out1 = on ? (s_out1 | mask) : (s_out1 & (uint8_t)~mask);
    reg_write(s_exp, EXP_OUT1, s_out1);
    xSemaphoreGive(s_exp_mx);
}

static bool exp_inputs(uint16_t *v)
{
    uint8_t b[2];
    xSemaphoreTake(s_exp_mx, portMAX_DELAY);
    esp_err_t e = reg_read(s_exp, EXP_IN0, b, 2);
    xSemaphoreGive(s_exp_mx);
    if (e != ESP_OK) {
        return false;
    }
    *v = (uint16_t)(b[0] | (b[1] << 8));
    return true;
}

// The power order of the Seeed BSP: all P1 switches off but SYSTEM (the power latch stays on, so
// a restart on the battery does not cut the power), 100 ms, then all on. The LCD power cycle is
// also its reset (the panel has no reset line).
static void exp_init(void)
{
    s_exp_mx = xSemaphoreCreateMutex();
    s_out1 = P1_SYSTEM;
    reg_write(s_exp, EXP_OUT1, s_out1);
    reg_write(s_exp, EXP_CFG0, 0xFF);        // P0: all inputs
    reg_write(s_exp, EXP_CFG1, P1_BAT_DET);  // P1: outputs, but P1.5
    vTaskDelay(pdMS_TO_TICKS(100));
    s_out1 = P1_START;
    if (reg_write(s_exp, EXP_OUT1, s_out1) != ESP_OK) {
        ESP_LOGE(TAG, "the PCA9535 does not answer");
    }
    vTaskDelay(pdMS_TO_TICKS(50));
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
    esp_lcd_panel_io_tx_param(s_io, (int)((0x02u << 24) | ((uint32_t)cmd << 8)), data, len);
}

// The next DMA buffer. One colour transfer at most runs while the next buffer fills (the window
// commands of draw_bitmap wait for it), so the two buffers take turns across all the calls.
static uint16_t *next_buf(void)
{
    static unsigned s_i;
    xSemaphoreTake(s_dma_free, portMAX_DELAY);
    return s_dma_buf[s_i++ & 1];
}

// Black on the whole panel.
static void lcd_clear(void)
{
    for (int y = 0; y < PANEL; y += CHUNK_ROWS) {
        int n = PANEL - y < CHUNK_ROWS ? PANEL - y : CHUNK_ROWS;
        uint16_t *dst = next_buf();
        memset(dst, 0, PANEL * n * 2);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, PANEL, y + n, dst);
    }
}

// The native frame: 412 x 412, the bytes of each pixel already in bus order.
static void lcd_send_native(const uint16_t *fb)
{
    for (int y = 0; y < PANEL; y += CHUNK_ROWS) {
        int n = PANEL - y < CHUNK_ROWS ? PANEL - y : CHUNK_ROWS;
        uint16_t *dst = next_buf();
        memcpy(dst, fb + y * PANEL, (size_t)PANEL * n * 2);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, PANEL, y + n, dst);
    }
}

// ---- the zoom of the CoreS3 frame (bilinear). A colour in the "spread" form
// 00000GGGGGG00000RRRRR000000BBBBB has room for a weighted sum (weights 0..32).
#define SPREAD_MASK 0x07E0F81Fu
static inline uint32_t spread(uint16_t c)
{
    return ((uint32_t)c | ((uint32_t)c << 16)) & SPREAD_MASK;
}
static inline uint32_t mix(uint32_t a, uint32_t b, uint32_t w)  // w 0..32: the part of b
{
    return ((a * (32 - w) + b * w) >> 5) & SPREAD_MASK;
}

// For each output column (row): the first source column (row) and the weight of the next one.
static int16_t s_zx[PANEL], s_zy[PANEL];
static uint8_t s_zwx[PANEL], s_zwy[PANEL];
static uint32_t s_hrow[2][PANEL];  // two source rows, scaled across, spread form
static int s_hrow_src[2] = {-1, -1};

static void zoom_table(int16_t *idx, uint8_t *w, int out, int src)
{
    for (int i = 0; i < out; i++) {
        int p = ((2 * i + 1) * src * 32) / (2 * out) - 16;  // the centre of the output pixel, 1/32 px
        if (p < 0) {
            p = 0;
        }
        int k = p >> 5, f = p & 31;
        if (k >= src - 1) {
            k = src - 2, f = 32;
        }
        idx[i] = (int16_t)k;
        w[i] = (uint8_t)f;
    }
}

// the source row r, scaled across to `out` columns (cached: two rows)
static const uint32_t *hrow(const uint16_t *fb, int sw, int r, int out)
{
    int slot = s_hrow_src[0] == r ? 0 : s_hrow_src[1] == r ? 1 : -1;
    if (slot >= 0) {
        return s_hrow[slot];
    }
    slot = s_hrow_src[0] == r - 1 ? 1 : 0;  // keep the row above (the next output row needs it)
    static uint32_t s_src[LCD_H];           // the source row in the spread form (each pixel once)
    const uint16_t *row = fb + r * sw;
    for (int i = 0; i < sw; i++) {
        s_src[i] = spread(__builtin_bswap16(row[i]));
    }
    uint32_t *o = s_hrow[slot];
    for (int i = 0; i < out; i++) {
        int k = s_zx[i];
        o[i] = mix(s_src[k], s_src[k + 1], s_zwx[i]);
    }
    s_hrow_src[slot] = r;
    return o;
}

// The CoreS3 frame at 1:1 in the middle (the bands around it are black from lcd_clear()).
static void lcd_send_fit(const uint16_t *fb, bool land)
{
    int sw = land ? LCD_H : LCD_W, sh = land ? LCD_W : LCD_H;
    int ox = (PANEL - sw) / 2, oy = (PANEL - sh) / 2;
    for (int y = 0; y < sh; y += CHUNK_ROWS) {
        int n = sh - y < CHUNK_ROWS ? sh - y : CHUNK_ROWS;
        uint16_t *dst = next_buf();
        for (int r = 0; r < n; r++) {
            uint16_t *o = dst + r * PANEL;
            const uint16_t *s = fb + (y + r) * sw;
            memset(o, 0, ox * 2);
            for (int x = 0; x < sw; x++) {
                o[ox + x] = __builtin_bswap16(s[x]);
            }
            memset(o + ox + sw, 0, (PANEL - ox - sw) * 2);
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, oy + y, PANEL, oy + y + n, dst);
    }
}

static void lcd_send_zoom(const uint16_t *fb, bool land)
{
    int sw = land ? LCD_H : LCD_W, sh = land ? LCD_W : LCD_H;
    int ow = land ? ZOOM_W : ZOOM_H, oh = land ? ZOOM_H : ZOOM_W;
    int ox = (PANEL - ow) / 2, oy = (PANEL - oh) / 2;
    static int s_tab = -1;
    if (s_tab != land) {
        zoom_table(s_zx, s_zwx, ow, sw);
        zoom_table(s_zy, s_zwy, oh, sh);
        s_tab = land;
    }
    s_hrow_src[0] = s_hrow_src[1] = -1;
    for (int y = 0; y < oh; y += CHUNK_ROWS) {
        int n = oh - y < CHUNK_ROWS ? oh - y : CHUNK_ROWS;
        uint16_t *dst = next_buf();
        for (int r = 0; r < n; r++) {
            int sy = s_zy[y + r];
            const uint32_t *a = hrow(fb, sw, sy, ow), *b = hrow(fb, sw, sy + 1, ow);
            uint32_t w = s_zwy[y + r];
            uint16_t *o = dst + r * PANEL;
            for (int x = 0; x < ox; x++) {
                o[x] = 0;
            }
            if (!(ox & 1) && !(ow & 1)) {
                // two pixels in each 32-bit store (the bytes of each pixel swapped for the bus)
                uint32_t *o2 = (uint32_t *)(o + ox);
                for (int x = 0; x < ow; x += 2) {
                    uint32_t v0 = mix(a[x], b[x], w), v1 = mix(a[x + 1], b[x + 1], w);
                    uint32_t p = ((v0 | (v0 >> 16)) & 0xFFFF) | ((v1 | (v1 >> 16)) << 16);
                    o2[x >> 1] = ((p & 0x00FF00FFu) << 8) | ((p >> 8) & 0x00FF00FFu);
                }
            } else {
                for (int x = 0; x < ow; x++) {
                    uint32_t v = mix(a[x], b[x], w);
                    o[ox + x] = __builtin_bswap16((uint16_t)((v | (v >> 16)) & 0xFFFF));
                }
            }
            for (int x = ox + ow; x < PANEL; x++) {
                o[x] = 0;
            }
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, oy + y, PANEL, oy + y + n, dst);
    }
}

static void flush_task(void *arg)
{
    frame_t f;
    int mode = -1;  // 0 zoom portrait, 1 zoom landscape, 2 native, 3 / 4 fit (1:1)
    for (;;) {
        xQueueReceive(s_flush_q, &f, portMAX_DELAY);
        int o = f.orient & 0xFF;
        bool land = o == ORIENT_LAND_A || o == ORIENT_LAND_B;
        int m = (f.orient & ORIENT_NATIVE) ? 2 : (f.orient & ORIENT_FIT) ? 3 + land : land;
        if (m != mode) {
            if (m != 2) {
                lcd_clear();  // the bands outside the zoomed picture: black
            }
            mode = m;
        }
        // (measured at 40 MHz: native 21.6 ms a frame, the zoom 22-24 ms)
        if (m == 2) {
            lcd_send_native(f.fb);
        } else if (m >= 3) {
            lcd_send_fit(f.fb, land);
        } else {
            lcd_send_zoom(f.fb, m == 1);
        }
        xSemaphoreGive(s_flush_idle);
        vTaskDelay(1);  // the lower tasks of this core (touch, idle) get time
    }
}

void board_lcd_flush(const uint16_t *fb, int orient)
{
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    frame_t f = {fb, orient | (s_fit && !(orient & ORIENT_NATIVE) ? ORIENT_FIT : 0)};
    xQueueSend(s_flush_q, &f, portMAX_DELAY);
}

static void backlight_set(uint8_t level)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, (uint32_t)level * 1023 / 255);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

static void lcd_init(void)
{
    ledc_timer_config_t lt = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_1,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&lt));
    ledc_channel_config_t lc = {
            .gpio_num = PIN_LCD_BL,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_1,
            .timer_sel = LEDC_TIMER_1,
            .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lc));

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
            .max_transfer_sz = CHUNK_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_spi_config_t io = {
            .cs_gpio_num = PIN_LCD_CS,
            .dc_gpio_num = GPIO_NUM_NC,
            .spi_mode = 3,
            .pclk_hz = LCD_PCLK_HZ,
            .trans_queue_depth = 10,
            .on_color_trans_done = on_color_done,
            .lcd_cmd_bits = 32,
            .lcd_param_bits = 8,
            .flags.quad_mode = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &s_io));
    spd2010_vendor_config_t vc = {.flags.use_qspi_interface = 1};
    esp_lcd_panel_dev_config_t pc = {
            .reset_gpio_num = GPIO_NUM_NC,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vc,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_spd2010(s_io, &pc, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    s_dma_free = xSemaphoreCreateCounting(2, 2);
    for (int i = 0; i < 2; i++) {
        s_dma_buf[i] = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        assert(s_dma_buf[i]);
    }
    lcd_clear();
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    s_flush_q = xQueueCreate(1, sizeof(frame_t));
    s_flush_idle = xSemaphoreCreateBinary();
    xSemaphoreGive(s_flush_idle);
    xTaskCreatePinnedToCore(flush_task, "lcd", 3072, NULL, 7, NULL, 1);
}

// The backlight. The change goes between two frames.
void board_backlight(uint8_t level)
{
    s_bright = level;
    if (!s_scr_sleep) {
        backlight_set(level);
    }
}

// ---------------------------------------------------------------- speaker

void board_speaker(bool on)
{
    s_spk_on = on;
    if (!s_scr_sleep || s_spk_hold) {
        exp_set(P1_PA, on);
    }
}

void board_speaker_hold(bool hold)
{
    s_spk_hold = hold;
    if (s_scr_sleep) {
        exp_set(P1_PA, hold && s_spk_on);
    }
}

static void wheel_run(bool on);

void board_screen_sleep(bool sleep)
{
    if (sleep == s_scr_sleep) {
        return;
    }
    wheel_run(!sleep);
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    if (sleep) {
        backlight_set(0);
        lcd_cmd(0x28, NULL, 0);  // display off (the touch reads on)
        lcd_cmd(0x10, NULL, 0);  // sleep in
        exp_set(P1_PA, s_spk_hold && s_spk_on);
    } else {
        lcd_cmd(0x11, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        lcd_cmd(0x29, NULL, 0);
        backlight_set(s_bright);
        exp_set(P1_PA, s_spk_on);
    }
    s_scr_sleep = sleep;
    xSemaphoreGive(s_flush_idle);
}

// ---------------------------------------------------------------- keys

// The knob button (P0.3, low = down). The expander is read at most every KEY_POLL_MS.
bool board_boot_key_down(void)
{
    return false;
}  // (no second key for CHAT on this board)

bool board_pwr_key_down(void)
{
    static uint32_t s_polled;
    static bool s_down;
    uint32_t now = tfn_now_ms32();
    if (now - s_polled >= KEY_POLL_MS) {
        s_polled = now;
        uint16_t v;
        if (exp_inputs(&v)) {
            s_down = !(v & IO_KNOB_BTN);
        }
    }
    return s_down;
}

// ---------------------------------------------------------------- the wheel

static pcnt_unit_handle_t s_wheel;

// The PCNT unit counts the four edges of each quadrature cycle (both channels), with a glitch
// filter. accum_count keeps the count past the limits.
static void wheel_init(void)
{
    pcnt_unit_config_t uc = {.low_limit = -1000, .high_limit = 1000, .flags.accum_count = true};
    if (pcnt_new_unit(&uc, &s_wheel) != ESP_OK) {
        return;
    }
    pcnt_glitch_filter_config_t fc = {.max_glitch_ns = 1000};
    pcnt_unit_set_glitch_filter(s_wheel, &fc);
    pcnt_chan_config_t ca = {.edge_gpio_num = PIN_WHEEL_A, .level_gpio_num = PIN_WHEEL_B};
    pcnt_chan_config_t cb = {.edge_gpio_num = PIN_WHEEL_B, .level_gpio_num = PIN_WHEEL_A};
    pcnt_channel_handle_t a, b;
    pcnt_new_channel(s_wheel, &ca, &a);
    pcnt_new_channel(s_wheel, &cb, &b);
    pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    gpio_set_pull_mode(PIN_WHEEL_A, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_WHEEL_B, GPIO_PULLUP_ONLY);
    pcnt_unit_add_watch_point(s_wheel, -1000);
    pcnt_unit_add_watch_point(s_wheel, 1000);
    pcnt_unit_enable(s_wheel);
    pcnt_unit_clear_count(s_wheel);
    pcnt_unit_start(s_wheel);
}

// The wheel counter stops while the screen sleeps: an enabled PCNT unit holds an APB lock, and the
// board does not go to the light sleep at all. The count stays; the knob button (the IO expander) wakes the screen.
static void wheel_run(bool on)
{
    if (!s_wheel) {
        return;
    }
    if (on) {
        pcnt_unit_enable(s_wheel);
        pcnt_unit_start(s_wheel);
    } else {
        pcnt_unit_stop(s_wheel);
        pcnt_unit_disable(s_wheel);
    }
}

int board_wheel_raw(void)
{
    int c = 0;
    if (s_wheel) {
        pcnt_unit_get_count(s_wheel, &c);
    }
    return c;
}

// The steps: the count to the nearest step (the wheel rests at a whole step between clicks).
int board_wheel_pos(void)
{
    int c = board_wheel_raw(), h = WHEEL_COUNTS / 2;
    return c >= 0 ? (c + h) / WHEEL_COUNTS : -((-c + h) / WHEEL_COUNTS);
}

// ---------------------------------------------------------------- touch (SPD2010)

static esp_err_t tp_cmd(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint8_t w[4] = {a, b, c, d};
    esp_err_t e = i2c_master_transmit(s_tp, w, 4, I2C_TIMEOUT_MS);
    esp_rom_delay_us(200);
    return e;
}

static esp_err_t tp_read(uint8_t r0, uint8_t r1, uint8_t *buf, size_t n)
{
    uint8_t w[2] = {r0, r1};
    esp_err_t e = i2c_master_transmit(s_tp, w, 2, I2C_TIMEOUT_MS);
    esp_rom_delay_us(200);
    if (e == ESP_OK) {
        e = i2c_master_receive(s_tp, buf, n, I2C_TIMEOUT_MS);
    }
    esp_rom_delay_us(200);
    return e;
}

// The SPD2010 touch protocol (from espressif/esp_lcd_touch_spd2010): the status tells if the chip
// is in its BIOS (start the CPU), in its CPU (start the point mode) or has points (read them).
// The points are panel points (0..411); they go to logical points of the current frame
// (BOARD_TOUCH_LOGICAL): the native frame = the panel; the zoomed CoreS3 frame = the inverse zoom; the fit
// frame (app_t.round_fit) = the panel point less the offset of the frame.
int board_touch_points(int *x, int *y, int max)
{
    static uint32_t s_errors, s_read_at, s_active_until;
    // Each read of the chip waits 4 x 200 us (its protocol). Read it only when its interrupt line
    // is low, while a finger is on it (150 ms after the last point), or every 100 ms (the start-up
    // steps of the chip).
    uint32_t now = tfn_now_ms32();
    if (now - s_read_at < 100 && (int32_t)(s_active_until - now) <= 0) {
        uint16_t io;
        if (exp_inputs(&io) && (io & IO_TP_INT)) {
            return 0;
        }
    }
    s_read_at = now;
    uint8_t st[4];
    if (tp_read(0x20, 0x00, st, 4) != ESP_OK) {
        if ((s_errors++ % 500) == 0) {
            ESP_LOGW(TAG, "touch read failed (%lu times)", (unsigned long)s_errors);
        }
        return 0;
    }
    bool pt = st[0] & 0x01, gesture = st[0] & 0x02, aux = st[0] & 0x08;
    bool in_bios = st[1] & 0x40, in_cpu = st[1] & 0x20, cpu_run = st[1] & 0x08;
    int len = st[2] | (st[3] << 8);
    if (in_bios) {
        tp_cmd(0x02, 0x00, 0x01, 0x00);  // clear the interrupt
        tp_cmd(0x04, 0x00, 0x01, 0x00);  // CPU start
        return 0;
    }
    if (in_cpu) {
        tp_cmd(0x50, 0x00, 0x00, 0x00);  // point mode
        tp_cmd(0x46, 0x00, 0x00, 0x00);  // touch start
        tp_cmd(0x02, 0x00, 0x01, 0x00);
        return 0;
    }
    if (cpu_run && len == 0) {
        tp_cmd(0x02, 0x00, 0x01, 0x00);
        return 0;
    }
    if (!pt && !gesture) {
        if (cpu_run && aux) {
            tp_cmd(0x02, 0x00, 0x01, 0x00);
        }
        return 0;
    }
    uint8_t d[4 + 10 * 6];
    if (len > (int)sizeof d) {
        len = sizeof d;
    }
    if (len < 4 || tp_read(0x00, 0x03, d, len) != ESP_OK) {
        return 0;
    }
    int n = 0;
    if (d[4] <= 0x0A && pt) {
        int pts = (len - 4) / 6;
        bool native = gfx_native(), land = orient_is_landscape(gfx_orient());
        int sw = native ? PANEL : land ? LCD_H : LCD_W, sh = native ? PANEL : land ? LCD_W : LCD_H;
        int ow = native ? PANEL : land ? ZOOM_W : ZOOM_H, oh = native ? PANEL : land ? ZOOM_H : ZOOM_W;
        if (!native && s_fit) {
            ow = sw, oh = sh;
        }
        int ox = (PANEL - ow) / 2, oy = (PANEL - oh) / 2;
        for (int i = 0; i < pts && n < max; i++) {
            const uint8_t *p = d + 4 + 6 * i;
            if (p[4] == 0) {
                continue;  // weight 0: the finger is up
            }
            int tx = ((p[3] & 0xF0) << 4) | p[1], ty = ((p[3] & 0x0F) << 8) | p[2];
            int lx = (tx - ox) * sw / ow, ly = (ty - oy) * sh / oh;
            x[n] = lx < 0 ? 0 : lx >= sw ? sw - 1 : lx;
            y[n] = ly < 0 ? 0 : ly >= sh ? sh - 1 : ly;
#if CONFIG_TFN_TOUCH_DEBUG
            ESP_LOGI(TAG, "touch %d/%d panel=(%d,%d) logical=(%d,%d)", i + 1, pts, tx, ty, x[n], y[n]);
#endif
            n++;
        }
    }
    if (n > 0) {
        s_active_until = now + 150;
    }
    // the end of the packet: clear the interrupt, or read the rest
    for (int k = 0; k < 4; k++) {
        uint8_t hs[8];
        if (tp_read(0xFC, 0x02, hs, 8) != ESP_OK) {
            break;
        }
        int next = hs[2] | (hs[3] << 8);
        if (hs[5] == 0x82) {
            tp_cmd(0x02, 0x00, 0x01, 0x00);
            break;
        }
        if (hs[5] != 0x00 || next <= 0) {
            break;
        }
        uint8_t rest[32];
        tp_read(0x00, 0x03, rest, next > (int)sizeof rest ? sizeof rest : next);
    }
    return n;
}

// ---------------------------------------------------------------- battery, power

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;

static int battery_mv(void)
{
    if (!s_adc) {
        adc_oneshot_unit_init_cfg_t uc = {.unit_id = ADC_UNIT_1};
        if (adc_oneshot_new_unit(&uc, &s_adc) != ESP_OK) {
            return 0;
        }
        adc_oneshot_chan_cfg_t cc = {.bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_2_5};
        adc_oneshot_config_channel(s_adc, BAT_ADC_CHAN, &cc);
        adc_cali_curve_fitting_config_t cf = {
                .unit_id = ADC_UNIT_1,
                .chan = BAT_ADC_CHAN,
                .atten = ADC_ATTEN_DB_2_5,
                .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        adc_cali_create_scheme_curve_fitting(&cf, &s_cali);
    }
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0, mv = 0;
        adc_oneshot_read(s_adc, BAT_ADC_CHAN, &raw);
        if (s_cali) {
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
        }
        sum += mv;
    }
    return sum / 8 * 82 / 20;  // the divider 62k + 20k
}

int board_battery(bool *charging, int *mv)
{
    static int64_t s_at;
    static int s_pct = -1, s_mv;
    static bool s_chg;
    int64_t now = esp_timer_get_time();
    if (s_at == 0 || now - s_at > 2000000) {
        s_at = now;
        uint16_t io;
        if (exp_inputs(&io)) {
            s_chg = (io & IO_CHARGING) != 0;  // (the BSP: high = charging)
        }
        s_mv = battery_mv();
        int p = (-1 * s_mv * s_mv + 9016 * s_mv - 19189000) / 10000;  // the BSP curve
        s_pct = s_mv < 2500 ? -1 : p > 100 ? 100 : p < 0 ? 0 : p;
    }
    if (charging) {
        *charging = s_chg;
    }
    if (mv) {
        *mv = s_mv;
    }
    return s_pct;
}

// the PCA9535 INT line (the touch INT is its P0.5; a read of the inputs in board_touch_points() releases it; the
// knob button pulls it low too)
int board_touch_int_gpio(void)
{
    return PIN_EXP_INT;
}
void board_touch_int_ack(void)
{
}

// The power latch off (P1.2). On USB power the board stays on: then deep sleep, and the IO
// expander interrupt (the knob button) wakes it.
void board_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    backlight_set(0);
    lcd_cmd(0x28, NULL, 0);
    lcd_cmd(0x10, NULL, 0);
    exp_set(P1_PA, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    exp_set(P1_SYSTEM, false);
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGW(TAG, "still on (USB power): deep sleep, the knob button wakes the board");
    exp_set(P1_SD | P1_LCD | P1_AI | P1_GROVE | P1_BAT_ADC, false);
    esp_sleep_enable_ext0_wakeup(PIN_EXP_INT, 0);
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------- the microSD card

esp_err_t board_sd_card_mount(const esp_vfs_fat_mount_config_t *mc, sdmmc_card_t **card)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST;
    host.max_freq_khz = 20000;
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.host_id = SD_HOST;
    dev.gpio_cs = PIN_SD_CS;
    sd_dma_set(&host);
    esp_err_t err = esp_vfs_fat_sdspi_mount(BOARD_SD_ROOT, &host, &dev, mc, card);
    sd_dma_result(err == ESP_OK);
    return err;
}

// The card detect line of the PCA9535 (low: a card is in the slot).
bool board_sd_card_in(void)
{
    uint16_t io;
    return exp_inputs(&io) && !(io & IO_SD_IN);
}

static void sd_bus_init(void)
{
    // the Himax AI chip is on the same bus: its CS is low while the chip has no power (board_ai_power), high when on
    gpio_config_t cs = {.pin_bit_mask = 1ULL << PIN_AI_CS, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&cs);
    gpio_set_level(PIN_AI_CS, 0);
    spi_bus_config_t bus = {
            .mosi_io_num = PIN_SPI_MOSI,
            .miso_io_num = PIN_SPI_MISO,
            .sclk_io_num = PIN_SPI_CLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
    };
    if (spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGW(TAG, "SD bus: not started");
    }
}

// ---------------------------------------------------------------- the Himax AI chip (the camera)
// webcam.c talks to it on the SD bus (SPI2, CS 21, 12 MHz, mode 0) with the SSCMA protocol of Seeed. P0.6 of the
// PCA9535 is high while the chip has data for the ESP; P0.7 is its reset line (an input in normal use).

esp_err_t board_ai_spi(spi_device_handle_t *dev)
{
    spi_device_interface_config_t c = {
            .clock_speed_hz = 12 * 1000 * 1000,
            .mode = 0,
            .spics_io_num = PIN_AI_CS,
            .queue_size = 1,
    };
    return spi_bus_add_device(SD_HOST, &c, dev);
}

bool board_ai_sync(void)
{
    uint16_t io;
    return exp_inputs(&io) && (io & IO_AI_SYNC);
}

// The power of the AI chip (webcam.c: on for the pictures, off after them). Off: its CS line low (a plain GPIO
// again: webcam.c removes its SPI device first), so no current goes into the chip without power.
void board_ai_power(bool on)
{
    gpio_config_t cs = {.pin_bit_mask = 1ULL << PIN_AI_CS, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&cs);
    if (on) {
        gpio_set_level(PIN_AI_CS, 1);
        exp_set(P1_AI, true);
        vTaskDelay(pdMS_TO_TICKS(500));  // (the chip starts its firmware; webcam.c resets it if it does not answer)
    } else {
        exp_set(P1_AI, false);
        gpio_set_level(PIN_AI_CS, 0);
    }
    ESP_LOGI(TAG, "the AI chip power %s", on ? "on" : "off");
}

// the reset of the Seeed BSP: the line low for 100 ms, then high; the chip is ready about 200 ms later
void board_ai_reset(void)
{
    xSemaphoreTake(s_exp_mx, portMAX_DELAY);
    reg_write(s_exp, EXP_OUT0, (uint8_t)~IO_AI_RST);
    reg_write(s_exp, EXP_CFG0, (uint8_t)~IO_AI_RST);  // P0.7 an output (low), the other P0 pins inputs
    xSemaphoreGive(s_exp_mx);
    vTaskDelay(pdMS_TO_TICKS(100));
    xSemaphoreTake(s_exp_mx, portMAX_DELAY);
    reg_write(s_exp, EXP_OUT0, 0xFF);
    xSemaphoreGive(s_exp_mx);
    vTaskDelay(pdMS_TO_TICKS(200));
    xSemaphoreTake(s_exp_mx, portMAX_DELAY);
    reg_write(s_exp, EXP_CFG0, 0xFF);  // all P0 pins inputs again
    xSemaphoreGive(s_exp_mx);
}

// ---------------------------------------------------------------- init

void board_init(void)
{
    board_sd_init();
    i2c_init();
    exp_init();
    sd_bus_init();
    wheel_init();
    lcd_init();
    ESP_LOGI(TAG, "SenseCAP Watcher: round screen 412 x 412 (round layouts native, the others zoomed), %d MHz",
             CONFIG_TFN_LCD_PCLK_MHZ);
}

#endif  // CONFIG_TFN_TARGET_WATCHER
