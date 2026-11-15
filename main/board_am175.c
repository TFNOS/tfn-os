// Board support for the Waveshare ESP32-S3-Touch-AMOLED-1.75C. It has the same API as board.c (see
// board.h). The build uses this file in place of board.c when CONFIG_TFN_TARGET_AMOLED175 is on. The pins and the
// panel start list come from the Waveshare BSP (waveshare/esp32_s3_touch_amoled_1_75c 3.0.0).
//
// Screen: CO5300 AMOLED, 466 x 466 round, QSPI (CS 12, CLK 38, D0..D3 4..7, RESET GPIO1), no backlight (the brightness
//         is the command 51h). The panel RAM is wider than the glass: the first visible column is 6 (GAP_X).
// Touch:  CST9217 at 0x5A (INT 11, RESET 2), the X and Y axes mirrored (the BSP).
// I2C:    one bus (SDA 15, SCL 14): CST9217 0x5A, AXP2101 0x34, ES8311 0x18, ES7210 0x40, QMI8658 0x6B, RTC 0x51.
// Power:  AXP2101. This code does not change its rails (the start-up values power the board); it reads the battery and
//         the PWR key, and it stops the power.
// Sound:  ES8311 (speaker) and ES7210 (two microphones) on I2S (audio.c); the amplifier on GPIO46 (high = on).
// No SD card. BOOT key: GPIO0 (low = down; a short push = back, as PWR).
//
// THE PICTURE: every app must look good on the panel from the start. The panel is a circle of 466 px:
// - a round layout (app_t.round, the native frame of the Watcher: 412 x 412): scaled by 466 / 412 to the whole circle;
// - the other apps (the CoreS3 frame, 240 x 320 or 320 x 240): all of the frame shows, scaled to 280 x 372 (it touches
//   the circle at its corners), and the rest of the circle is THE AMBIENT BORDER: the colours at the edges of the app,
//   spread out to the edge of the circle, soft, darker to the outside, with a thin dark line round the picture
//   ("FIT + AMBIENT").
// Both frames turn with the orientation (the motion sensor), as on the AMOLED-1.8. Only the rows of the circle go to the
// panel (each chunk of rows gets the window of its widest row).
// BOARD_TOUCH_LOGICAL: board_touch_points() gives logical points of the frame now (the scale, the turn, the border).

#include "tfn_util.h"
#include "sdkconfig.h"

#if CONFIG_TFN_TARGET_AMOLED175

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
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
#include "gfx.h"
#include "settings.h"

_Static_assert(LCD_W == 240 && LCD_H == 320, "the AMOLED-1.75C uses the CoreS3 frame: LCD_W 240, LCD_H 320 in board.h");
_Static_assert(BOARD_NATIVE_W == 412 && BOARD_NATIVE_H == 412, "the round frame of the AMOLED-1.75C is 412 x 412");

static const char *TAG = "board";

#define PIN_I2C_SDA GPIO_NUM_15
#define PIN_I2C_SCL GPIO_NUM_14
#define PIN_LCD_CS  GPIO_NUM_12
#define PIN_LCD_CLK GPIO_NUM_38
#define PIN_LCD_D0  GPIO_NUM_4
#define PIN_LCD_D1  GPIO_NUM_5
#define PIN_LCD_D2  GPIO_NUM_6
#define PIN_LCD_D3  GPIO_NUM_7
#define PIN_LCD_RST GPIO_NUM_1
#define PIN_TP_INT  GPIO_NUM_11
#define PIN_TP_RST  GPIO_NUM_2
#define PIN_PA      GPIO_NUM_46
#define PIN_BOOT    GPIO_NUM_0

#define TP_ADDR 0x5A

#define PKEY_SHORT 0x08
#define PKEY_ALL   0x0F

#define TP_ACK 0xAB  // byte 6 of a good report

// the panel and the pictures on it
#define PANEL         466
#define GAP_X         6               // the first visible column of the panel RAM
#define NAT           BOARD_NATIVE_W  // the round frame: 412 x 412
#define PIC_X0        94              // the CoreS3 frame (physical 240 x 320) at 280 x 372
#define PIC_Y0        47
#define PIC_W         280
#define PIC_H         372
#define LINE_W        2  // the dark line round the picture
#define LCD_HOST      SPI2_HOST
#define LCD_PCLK_HZ   (CONFIG_TFN_LCD_PCLK_MHZ * 1000 * 1000)
#define QSPI_OP_CMD   0x02u
#define QSPI_OP_COLOR 0x32u
#define CHUNK_ROWS    6                         // (6, not 8: this keeps internal RAM free for the Bluetooth)
#define CHUNK_BYTES   (PANEL * CHUNK_ROWS * 2)  // 5592 bytes
#define CHUNKS        ((PANEL + CHUNK_ROWS - 1) / CHUNK_ROWS)

#define KEY_LATCH_MS 150
#define KEY_POLL_MS  30

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_axp, s_tp;
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
static volatile int s_shown = -1;
static int64_t s_wait_us;
static int64_t s_t_band,
        s_t_hrow;  // (the flush log: the time of the band loads and the row scales)                   // (the flush log: the time spent waiting for a free DMA buffer)           // the orient (with ORIENT_NATIVE) of the last frame: the touch maps by it

void board_round_fit(bool on)
{
    (void)on;  // (all of the frame always shows here: the ambient border)
}

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

// ---------------------------------------------------------------- AXP2101
static uint8_t s_pmu_on, s_pmu_off;  // the sources of the last turn-on and turn-off (AXP2101 0x20, 0x21) at the start
static int s_pmu_rst;                // the reset reason of the ESP32 at the start

static void axp_init(void)
{
    axp2101_init_basic(s_axp, &s_pmu_on, &s_pmu_off);
    s_pmu_rst = (int)esp_reset_reason();
    board_pmu_log(0);
}

// The power log (the console "pmu [N]", as on the AMOLED-1.8): the reset reason, the sources of the last turn-on and
// turn-off of the AXP2101, then the voltages, the charger and the die temperature (once each second for N s).
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
    ESP_LOGW(TAG, "pmu: the start: ESP32 reset %d; AXP2101 on 0x%02X (%s) off 0x%02X (%s)", s_pmu_rst, s_pmu_on, on,
             s_pmu_off, off);
    for (int t = 0; t < secs; t++) {
        uint8_t st1 = 0, st2 = 0;
        reg_read(s_axp, 0x00, &st1, 1);
        reg_read(s_axp, 0x01, &st2, 1);
        static const char *const CHG[] = {"tri", "pre", "cc", "cv", "done", "not", "?", "?"};
        const int tr = axp2101_adc14(s_axp, 0x3C);
        ESP_LOGW(TAG, "pmu: vbus %d mV, vsys %d mV, bat %d mV; vbus good %d, bat present %d; charge %s; die %d C",
                 axp2101_adc14(s_axp, 0x38), axp2101_adc14(s_axp, 0x3A), axp2101_adc14(s_axp, 0x34), (st1 >> 5) & 1,
                 (st1 >> 3) & 1, CHG[st2 & 7], tr >= 0 ? 22 + (7274 - tr) / 20 : -99);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void board_pmu_a3(bool on)
{
    (void)on;  // (the heat guard of the AMOLED-1.8 only)
}

// ---------------------------------------------------------------- the screen: commands

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

// a window of panel columns x0..x1 (glass columns: the gap is added here) and rows y0..y1
static void lcd_window(int x0, int x1, int y0, int y1)
{
    x0 += GAP_X;
    x1 += GAP_X;
    lcd_cmd(0x2A, (uint8_t[]){(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1}, 4);
    lcd_cmd(0x2B, (uint8_t[]){(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1}, 4);
}

// ---------------------------------------------------------------- the pictures

// A colour in the "spread" form 00000GGGGGG00000RRRRR000000BBBBB has room for a weighted sum (weights 0..32).
#define SPREAD_MASK 0x07E0F81Fu
static inline uint32_t spread(uint16_t c)
{
    return ((uint32_t)c | ((uint32_t)c << 16)) & SPREAD_MASK;
}
static inline uint32_t mix(uint32_t a, uint32_t b, uint32_t w)  // w 0..32: the part of b
{
    return ((a * (32 - w) + b * w) >> 5) & SPREAD_MASK;
}
static inline uint32_t dim(uint32_t a, uint32_t w)  // w 0..32
{
    return ((a * w) >> 5) & SPREAD_MASK;
}
static inline uint16_t unspread_bus(uint32_t v)  // to RGB565, the bytes swapped for the bus
{
    return __builtin_bswap16((uint16_t)((v | (v >> 16)) & 0xFFFF));
}
// two spread colours to two pixels in one 32-bit word, the bytes of each pixel swapped for the bus
static inline uint32_t pair_bus(uint32_t v0, uint32_t v1)
{
    const uint32_t p = ((v0 | (v0 >> 16)) & 0xFFFF) | ((v1 | (v1 >> 16)) << 16);
    return ((p & 0x00FF00FFu) << 8) | ((p >> 8) & 0x00FF00FFu);
}
// a row of n (even) output pixels: the vertical mix of the scaled rows a and b with the weight wy
static void mix_row(uint16_t *o, const uint32_t *a, const uint32_t *b, uint32_t wy, int n)
{
    uint32_t *o2 = (uint32_t *)o;
    if (wy == 0) {
        for (int x = 0; x < n; x += 2) {
            o2[x >> 1] = pair_bus(a[x], a[x + 1]);
        }
    } else {
        for (int x = 0; x < n; x += 2) {
            o2[x >> 1] = pair_bus(mix(a[x], b[x], wy), mix(a[x + 1], b[x + 1], wy));
        }
    }
}

// the window of each chunk of rows: the columns of its widest row of the circle (an even start, an even width)
static int16_t s_cx0[CHUNKS] EXT_RAM_BSS_ATTR, s_cx1[CHUNKS] EXT_RAM_BSS_ATTR;

static void chord_init(void)
{
    for (int c = 0; c < CHUNKS; c++) {
        int lo = PANEL, hi = 0;
        for (int y = c * CHUNK_ROWS; y < (c + 1) * CHUNK_ROWS && y < PANEL; y++) {
            const float dy = (float)y + 0.5f - PANEL / 2.0f, r = PANEL / 2.0f;
            const float hw = dy * dy < r * r ? sqrtf(r * r - dy * dy) : 0;
            int x0 = (int)floorf(PANEL / 2.0f - hw) - 1, x1 = (int)ceilf(PANEL / 2.0f + hw);
            if (x0 < lo) {
                lo = x0;
            }
            if (x1 > hi) {
                hi = x1;
            }
        }
        lo = lo < 0 ? 0 : lo & ~1;
        hi = hi > PANEL - 1 ? PANEL - 1 : hi | 1;
        s_cx0[c] = (int16_t)lo;
        s_cx1[c] = (int16_t)hi;
    }
}

// The bilinear tables: for each output column (row), the first source column (row) and the weight of the next one.
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
// (the tables of the flush are in PSRAM: the internal RAM is short; the cache holds the parts in use)
static int16_t s_nx[PANEL] EXT_RAM_BSS_ATTR, s_ny[PANEL] EXT_RAM_BSS_ATTR;  // the round frame: 412 -> 466
static uint8_t s_nwx[PANEL] EXT_RAM_BSS_ATTR, s_nwy[PANEL] EXT_RAM_BSS_ATTR;
static int16_t s_fx[PIC_W] EXT_RAM_BSS_ATTR, s_fy[PIC_H] EXT_RAM_BSS_ATTR;  // the CoreS3 frame: 240 -> 280, 320 -> 372
static uint8_t s_fwx[PIC_W] EXT_RAM_BSS_ATTR, s_fwy[PIC_H] EXT_RAM_BSS_ATTR;

// The place in the logical frame of each physical point, for an orientation (the inverse of the logical turn):
// idx = s_row_of[py] + s_col_of[px]. One pair for the CoreS3 frame (w x h = 240 x 320), one for the round frame.
static int s_col_of[PANEL] EXT_RAM_BSS_ATTR, s_row_of[PANEL] EXT_RAM_BSS_ATTR;  // (PANEL: the hi round frame)
static int s_tab_orient = -1;
static int s_base, s_sx, s_sy;    // the same as a line: idx = s_base + px * s_sx + py * s_sy
static bool s_border_all = true;  // the next frame of the 320 x 240 frame sends all of the border
static void tables_orient(int orient, int w, int h)
{
    switch (orient) {
    case ORIENT_PORTRAIT:
        s_base = 0;
        s_sx = 1;
        s_sy = w;
        break;
    case ORIENT_PORTRAIT_FLIP:
        s_base = (h - 1) * w + (w - 1);
        s_sx = -1;
        s_sy = -w;
        break;
    case ORIENT_LAND_A:
        s_base = (w - 1) * h;
        s_sx = -h;
        s_sy = 1;
        break;
    default:
        s_base = h - 1;
        s_sx = h;
        s_sy = -1;
        break;
    }
    for (int px = 0; px < w; px++) {
        switch (orient) {
        case ORIENT_PORTRAIT:
            s_col_of[px] = px;
            break;
        case ORIENT_PORTRAIT_FLIP:
            s_col_of[px] = w - 1 - px;
            break;
        case ORIENT_LAND_A:
            s_col_of[px] = (w - 1 - px) * h;
            break;
        default:
            s_col_of[px] = px * h;
            break;
        }
    }
    for (int py = 0; py < h; py++) {
        switch (orient) {
        case ORIENT_PORTRAIT:
            s_row_of[py] = py * w;
            break;
        case ORIENT_PORTRAIT_FLIP:
            s_row_of[py] = (h - 1 - py) * w;
            break;
        case ORIENT_LAND_A:
            s_row_of[py] = py;
            break;
        default:
            s_row_of[py] = h - 1 - py;
            break;
        }
    }
}

// THE BAND (one PSRAM read for each pixel with a stride of a frame row takes about 45 ms a frame in a
// landscape orientation): before each chunk, the physical source rows a..b of the chunk go into s_band in the order of
// the memory (a physical row of a landscape frame is a logical column: the outer loop goes along the logical rows, so
// each read is a short run of neighbour pixels).
#define BAND_ROWS 10
static uint32_t *s_band;  // BAND_ROWS x NAT, the spread form
static int s_band_a = -1, s_band_n;
static uint32_t s_sum[CHUNKS] EXT_RAM_BSS_ATTR;  // the check sum of the band of each chunk in the last frame
static bool s_sum_ok;                            // false: no chunk is skipped (a new mode, a new orientation)
// the band of rows a..b; returns its check sum (the pixels and the place of the band)
static uint32_t band_load(const uint16_t *fb, int a, int b, int w)
{
    uint32_t sum = 0x811C9DC5u ^ (uint32_t)(a * 977 + b);
    const int64_t t0 = esp_timer_get_time();
    int n = b - a + 1;
    assert(n > 0 && n <= BAND_ROWS);
    const int o = s_tab_orient & 0xFF, sx = s_sx, sy = s_sy;
    if (o == ORIENT_LAND_A || o == ORIENT_LAND_B) {
        // (a physical row is a logical column: along each logical row, the n neighbour pixels of the band)
        const uint16_t *p0 = fb + s_base + a * sy;
        for (int px = 0; px < w; px++, p0 += sx) {
            const uint16_t *p = p0;
            uint32_t *d = s_band + px;
            for (int k = 0; k < n; k++, p += sy, d += w) {
                const uint16_t v = *p;
                sum = ((sum << 5) | (sum >> 27)) + v;
                *d = spread(__builtin_bswap16(v));
            }
        }
    } else {
        for (int k = 0; k < n; k++) {
            const uint16_t *p = fb + s_base + (a + k) * sy;
            uint32_t *d = s_band + k * w;
            for (int px = 0; px < w; px++, p += sx) {
                const uint16_t v = *p;
                sum = ((sum << 5) | (sum >> 27)) + v;
                d[px] = spread(__builtin_bswap16(v));
            }
        }
    }
    s_band_a = a;
    s_band_n = n;
    s_t_band += esp_timer_get_time() - t0;
    return sum;
}

// the physical source row r (from the band), scaled across to `out` columns, in the spread form. Two rows stay cached
// (the next output row needs the row below).
static uint32_t *s_hrow[2];  // PANEL each
static int s_hrow_src[2];
static const uint32_t *hrow(int r, int w, int out, const int16_t *zx, const uint8_t *zw)
{
    int slot = s_hrow_src[0] == r ? 0 : s_hrow_src[1] == r ? 1 : -1;
    if (slot >= 0) {
        return s_hrow[slot];
    }
    slot = s_hrow_src[0] == r - 1 ? 1 : 0;
    const int64_t t0 = esp_timer_get_time();
    assert(r >= s_band_a && r < s_band_a + s_band_n);
    const uint32_t *src = s_band + (r - s_band_a) * w;
    uint32_t *o = s_hrow[slot];
    for (int i = 0; i < out; i++) {
        const int k = zx[i];
        o[i] = mix(src[k], src[k + 1], zw[i]);
    }
    s_hrow_src[slot] = r;
    s_t_hrow += esp_timer_get_time() - t0;
    return o;
}

// ---- THE AMBIENT BORDER. A grid of AG_W x AG_H cells of the physical CoreS3 frame (each 20 x 20 px; the mean of 16
// samples), blurred twice (3 x 3) and mixed with the grid of the frames before (a quarter of the new one: no flicker).
// The colour of a border point = the grid at the nearest point of the picture (its clamped frame position), so the
// edge colours spread straight out; four strips (top, bottom, left, right) hold them for each frame. The border is
// darker away from the picture.
#define AG_W    12
#define AG_H    16
#define AG_CELL 20
static float s_ag[AG_H][AG_W][3] EXT_RAM_BSS_ATTR;
static bool s_ag_ok;
static uint32_t s_top[PANEL] EXT_RAM_BSS_ATTR, s_bot[PANEL] EXT_RAM_BSS_ATTR;  // spread form, full strength
static uint32_t s_left[PIC_H] EXT_RAM_BSS_ATTR, s_right[PIC_H] EXT_RAM_BSS_ATTR;
static uint8_t s_fade_y[PANEL] EXT_RAM_BSS_ATTR,
        s_fade_x[PANEL] EXT_RAM_BSS_ATTR;  // 0..32: the strength at the distance

static void ambient_grid(const uint16_t *fb)
{
    static float g[AG_H][AG_W][3] EXT_RAM_BSS_ATTR, t[AG_H][AG_W][3] EXT_RAM_BSS_ATTR;  // (static: a small stack)
    for (int cy = 0; cy < AG_H; cy++) {
        for (int cx = 0; cx < AG_W; cx++) {
            int r = 0, gg = 0, b = 0;
            for (int sy = 0; sy < 4; sy++) {
                const int py = cy * AG_CELL + 2 + sy * 5;
                for (int sx = 0; sx < 4; sx++) {
                    const int px = cx * AG_CELL + 2 + sx * 5;
                    const uint16_t c = __builtin_bswap16(fb[s_row_of[py] + s_col_of[px]]);
                    r += c >> 11;
                    gg += (c >> 5) & 63;
                    b += c & 31;
                }
            }
            g[cy][cx][0] = r * (255.0f / 31 / 16);
            g[cy][cx][1] = gg * (255.0f / 63 / 16);
            g[cy][cx][2] = b * (255.0f / 31 / 16);
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int cy = 0; cy < AG_H; cy++) {
            for (int cx = 0; cx < AG_W; cx++) {
                for (int k = 0; k < 3; k++) {
                    float s = 0;
                    int n = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            const int y = cy + dy, x = cx + dx;
                            if (y < 0 || y >= AG_H || x < 0 || x >= AG_W) {
                                continue;
                            }
                            s += g[y][x][k];
                            n++;
                        }
                    }
                    t[cy][cx][k] = s / n;
                }
            }
        }
        memcpy(g, t, sizeof g);
    }
    const float keep = s_ag_ok ? 0.75f : 0.0f;
    for (int cy = 0; cy < AG_H; cy++) {
        for (int cx = 0; cx < AG_W; cx++) {
            for (int k = 0; k < 3; k++) {
                s_ag[cy][cx][k] = s_ag[cy][cx][k] * keep + g[cy][cx][k] * (1 - keep);
            }
        }
    }
    s_ag_ok = true;
}

// the grid colour at the physical frame point (fx, fy) (bilinear between the cell centres), in the spread form
static uint32_t ambient_at(float fx, float fy)
{
    float gx = fx / AG_CELL - 0.5f, gy = fy / AG_CELL - 0.5f;
    gx = gx < 0 ? 0 : gx > AG_W - 1 ? AG_W - 1 : gx;
    gy = gy < 0 ? 0 : gy > AG_H - 1 ? AG_H - 1 : gy;
    int x0 = (int)gx, y0 = (int)gy;
    if (x0 > AG_W - 2) {
        x0 = AG_W - 2;
    }
    if (y0 > AG_H - 2) {
        y0 = AG_H - 2;
    }
    const float ax = gx - x0, ay = gy - y0;
    float c[3];
    for (int k = 0; k < 3; k++) {
        const float a = s_ag[y0][x0][k] + (s_ag[y0][x0 + 1][k] - s_ag[y0][x0][k]) * ax;
        const float b = s_ag[y0 + 1][x0][k] + (s_ag[y0 + 1][x0 + 1][k] - s_ag[y0 + 1][x0][k]) * ax;
        c[k] = a + (b - a) * ay;
    }
    const uint16_t v = (uint16_t)((((int)c[0] >> 3) << 11) | (((int)c[1] >> 2) << 5) | ((int)c[2] >> 3));
    return spread(v);
}

// the frame position of a panel column (row) of the picture, clamped to the frame
static float frame_x(int x)
{
    float f = (x - PIC_X0 + 0.5f) * LCD_W / PIC_W;
    return f < 0 ? 0 : f > LCD_W - 1 ? LCD_W - 1 : f;
}
static float frame_y(int y)
{
    float f = (y - PIC_Y0 + 0.5f) * LCD_H / PIC_H;
    return f < 0 ? 0 : f > LCD_H - 1 ? LCD_H - 1 : f;
}

static void ambient_strips(void)
{
    for (int x = 0; x < PANEL; x++) {
        s_top[x] = ambient_at(frame_x(x), 0);
        s_bot[x] = ambient_at(frame_x(x), LCD_H - 1);
    }
    for (int y = 0; y < PIC_H; y++) {
        s_left[y] = ambient_at(0, frame_y(PIC_Y0 + y));
        s_right[y] = ambient_at(LCD_W - 1, frame_y(PIC_Y0 + y));
    }
}

// the strength of the border at the distance from the picture: 0.62 next to it, 0.22 at the edge of the circle
static void fade_init(void)
{
    for (int i = 0; i < PANEL; i++) {
        const int dy = i < PIC_Y0 ? PIC_Y0 - i : i >= PIC_Y0 + PIC_H ? i - (PIC_Y0 + PIC_H - 1) : 0;
        const int dx = i < PIC_X0 ? PIC_X0 - i : i >= PIC_X0 + PIC_W ? i - (PIC_X0 + PIC_W - 1) : 0;
        const float fy = 0.62f - 0.40f * (dy / (float)PIC_Y0), fx = 0.62f - 0.40f * (dx / (float)PIC_X0);
        s_fade_y[i] = (uint8_t)(32 * (fy < 0.22f ? 0.22f : fy));
        s_fade_x[i] = (uint8_t)(32 * (fx < 0.22f ? 0.22f : fx));
    }
}

// the next free DMA buffer (the two take turns; the wait for it = the SPI time, in the flush log)
static unsigned s_dma_i;
static uint16_t *dma_next(void)
{
    const int64_t tw = esp_timer_get_time();
    xSemaphoreTake(s_dma_free, portMAX_DELAY);
    s_wait_us += esp_timer_get_time() - tw;
    return s_dma_buf[s_dma_i++ & 1];
}
// the buffer of the last dma_next() is not sent: it is free again (the next dma_next() gives it)
static void dma_unget(void)
{
    s_dma_i--;
    xSemaphoreGive(s_dma_free);
}

static void send_rect(int x0, int x1, int y0, int y1, uint16_t *dst)
{
    const int64_t tw = esp_timer_get_time();
    lcd_window(x0, x1, y0, y1);  // (it waits for the colour transfer before it)
    s_wait_us += esp_timer_get_time() - tw;
    const int bytes = (x1 - x0 + 1) * (y1 - y0 + 1) * 2;
    esp_lcd_panel_io_tx_color(s_io, (int)((QSPI_OP_COLOR << 24) | (0x2Cu << 8)), dst, bytes);
}
static void send_chunk(int c, uint16_t *dst)
{
    const int y0 = c * CHUNK_ROWS, y1 = y0 + CHUNK_ROWS - 1 < PANEL - 1 ? y0 + CHUNK_ROWS - 1 : PANEL - 1;
    send_rect(s_cx0[c], s_cx1[c], y0, y1, dst);
}

// the round frame (412 x 412, turned) scaled to the circle
static void lcd_send_native(const uint16_t *fb, int orient)
{
    if (s_tab_orient != (orient | ORIENT_NATIVE)) {
        tables_orient(orient, NAT, NAT);
        s_tab_orient = orient | ORIENT_NATIVE;
        s_sum_ok = false;
    }
    s_hrow_src[0] = s_hrow_src[1] = -1;
    for (int c = 0; c < CHUNKS; c++) {
        const int x0 = s_cx0[c], x1 = s_cx1[c], w = x1 - x0 + 1, y0 = c * CHUNK_ROWS;
        const int ylast = y0 + CHUNK_ROWS - 1 < PANEL - 1 ? y0 + CHUNK_ROWS - 1 : PANEL - 1;
        const uint32_t sum = band_load(fb, s_ny[y0], s_ny[ylast] + 1, NAT);
        if (s_sum_ok && sum == s_sum[c]) {
            continue;  // (the same pixels as in the last frame: nothing to send)
        }
        s_sum[c] = sum;
        uint16_t *dst = dma_next();
        for (int r = 0; r < CHUNK_ROWS && y0 + r < PANEL; r++) {
            const int y = y0 + r, sy = s_ny[y];
            const uint32_t *a = hrow(sy, NAT, PANEL, s_nx, s_nwx), *b = hrow(sy + 1, NAT, PANEL, s_nx, s_nwx);
            const uint32_t wy = s_nwy[y];
            mix_row(dst + r * w, a + x0, b + x0, wy, w);
        }
        send_chunk(c, dst);
    }
    s_sum_ok = true;
}

// THE ROUND FRAME AT THE PANEL RESOLUTION (app_t.round = APP_ROUND_HI): 466 x 466, copied 1:1 (no scale).
// For each chunk: ONE pass copies its source pixels into the DMA buffer (the turn by the pointer steps of the
// orientation) and makes their check sum; a chunk with the same pixels as in the last frame is not sent. (A sum pass
// and a copy pass read the frame in PSRAM two times: a flush 35 ms.)
// In a turned orientation (LAND_A / LAND_B) a panel row is a logical column, so a chunk of panel rows reads one pixel
// of each logical row (a stride of a frame row). There each chunk is a STRIP of CHUNK_ROWS panel COLUMNS (= logical
// rows: each read goes along the memory). The panel has no row / column exchange (its MADCTL has only MY, MX and RGB).
// The circle is the same along both axes: strip c needs the rows s_cx0[c]..s_cx1[c].
static void lcd_send_native_hi(const uint16_t *fb, int orient)
{
    const int key = orient | ORIENT_NATIVE | ORIENT_RHI;
    if (s_tab_orient != key) {
        tables_orient(orient, PANEL, PANEL);
        s_tab_orient = key;
        s_sum_ok = false;
    }
    const int sx = s_sx, sy = s_sy;
    // (OFF. A panel of the 1.75C went dark and came back only after a battery cut. The cause is not proven, but
    // these narrow column windows were the only new kind of panel transfer, and nobody saw them on the glass.
    // Turn them on again only after a check on the glass.)
    const bool strips = false && (orient == ORIENT_LAND_A || orient == ORIENT_LAND_B);
    for (int c = 0; strips && c < CHUNKS; c++) {
        const int x0 = c * CHUNK_ROWS, x1 = x0 + CHUNK_ROWS - 1 < PANEL - 1 ? x0 + CHUNK_ROWS - 1 : PANEL - 1;
        const int w = x1 - x0 + 1, y0 = s_cx0[c], y1 = s_cx1[c];
        uint16_t *dst = dma_next();
        const int64_t t0 = esp_timer_get_time();
        uint32_t sum = 0x811C9DC5u ^ (uint32_t)c;
        for (int k = 0; k < w; k++) {
            const uint16_t *p = fb + s_base + (x0 + k) * sx + y0 * sy;
            uint16_t *o = dst + k;
            for (int y = y0; y <= y1; y++, p += sy, o += w) {
                const uint16_t v = *p;
                sum = ((sum << 5) | (sum >> 27)) + v;
                *o = v;
            }
        }
        s_t_band += esp_timer_get_time() - t0;
        if (s_sum_ok && sum == s_sum[c]) {  // (the same pixels as in the last frame: nothing to send)
            dma_unget();
            continue;
        }
        s_sum[c] = sum;
        send_rect(x0, x1, y0, y1, dst);
    }
    for (int c = 0; !strips && c < CHUNKS; c++) {
        const int x0 = s_cx0[c], x1 = s_cx1[c], w = x1 - x0 + 1, y0 = c * CHUNK_ROWS;
        const int y1 = y0 + CHUNK_ROWS - 1 < PANEL - 1 ? y0 + CHUNK_ROWS - 1 : PANEL - 1;
        uint16_t *dst = dma_next();
        const int64_t t0 = esp_timer_get_time();
        uint32_t sum = 0x811C9DC5u ^ (uint32_t)c;
        for (int y = y0; y <= y1; y++) {
            const uint16_t *p = fb + s_base + y * sy + x0 * sx;
            uint16_t *o = dst + (y - y0) * w;
            for (int x = 0; x < w; x++, p += sx) {
                const uint16_t v = *p;
                sum = ((sum << 5) | (sum >> 27)) + v;
                o[x] = v;
            }
        }
        s_t_band += esp_timer_get_time() - t0;
        if (s_sum_ok && sum == s_sum[c]) {  // (the same pixels as in the last frame: nothing to send)
            dma_unget();
            continue;
        }
        s_sum[c] = sum;
        send_rect(x0, x1, y0, y1, dst);
    }
    s_sum_ok = true;
}

// the CoreS3 frame (turned) at 280 x 372 in the middle, the ambient border round it
static void lcd_send_frame(const uint16_t *fb, int orient)
{
    if (s_tab_orient != orient) {
        tables_orient(orient, LCD_W, LCD_H);
        s_tab_orient = orient;
        s_ag_ok = false;  // (a new orientation: the border starts again from this frame)
        s_border_all = true;
        s_sum_ok = false;
    }
    ambient_grid(fb);
    ambient_strips();
    s_hrow_src[0] = s_hrow_src[1] = -1;
    // THE BORDER IN TURN: in each frame only a quarter of the chunks (c % 4 == the turn) sends its full row of the circle
    // (the border, the line, the picture); the other chunks send only the picture columns, or nothing outside the
    // picture rows. A new mode or orientation sends all (s_border_all). The border changes slowly (the grid mixes a
    // quarter of the new colours in each frame), so the turn is not seen.
    static unsigned s_turn;
    s_turn++;
    for (int c = 0; c < CHUNKS; c++) {
        int y0 = c * CHUNK_ROWS, y1 = y0 + CHUNK_ROWS - 1 < PANEL - 1 ? y0 + CHUNK_ROWS - 1 : PANEL - 1;
        const bool full = s_border_all || (c & 3) == (int)(s_turn & 3);
        if (!full) {
            // (only the picture rows of the chunk, only the picture columns)
            if (y0 < PIC_Y0) {
                y0 = PIC_Y0;
            }
            if (y1 > PIC_Y0 + PIC_H - 1) {
                y1 = PIC_Y0 + PIC_H - 1;
            }
            if (y0 > y1) {
                continue;
            }
        }
        const int x0 = full ? s_cx0[c] : PIC_X0, x1 = full ? s_cx1[c] : PIC_X0 + PIC_W - 1, w = x1 - x0 + 1;
        {
            const int ya = y0 > PIC_Y0 ? y0 : PIC_Y0, yb = y1 < PIC_Y0 + PIC_H - 1 ? y1 : PIC_Y0 + PIC_H - 1;
            if (ya <= yb) {
                const uint32_t sum = band_load(fb, s_fy[ya - PIC_Y0], s_fy[yb - PIC_Y0] + 1, LCD_W);
                const bool same = s_sum_ok && sum == s_sum[c];
                s_sum[c] = sum;
                if (same && !full) {
                    continue;  // (the same pixels as in the last frame: nothing to send)
                }
            }
        }
        uint16_t *dst = dma_next();
        for (int r = 0; y0 + r <= y1; r++) {
            const int y = y0 + r;
            uint16_t *o = dst + r * w;
            const bool line_row =
                    (y >= PIC_Y0 - LINE_W && y < PIC_Y0) || (y >= PIC_Y0 + PIC_H && y < PIC_Y0 + PIC_H + LINE_W);
            if (y < PIC_Y0 || y >= PIC_Y0 + PIC_H) {
                const uint32_t *strip = y < PIC_Y0 ? s_top : s_bot;
                const uint32_t fy = s_fade_y[y];
                for (int x = 0; x < w; x++) {
                    const int px = x0 + x;
                    if (line_row && px >= PIC_X0 - LINE_W && px < PIC_X0 + PIC_W + LINE_W) {
                        o[x] = 0;
                    } else {
                        o[x] = unspread_bus(dim(strip[px], fy < s_fade_x[px] ? fy : s_fade_x[px]));
                    }
                }
                continue;
            }
            const int k = y - PIC_Y0, sy = s_fy[k];
            const uint32_t *a = hrow(sy, LCD_W, PIC_W, s_fx, s_fwx), *b = hrow(sy + 1, LCD_W, PIC_W, s_fx, s_fwx);
            const uint32_t wy = s_fwy[k];
            if (!full) {
                mix_row(o, a, b, wy, PIC_W);
                continue;
            }
            mix_row(o + (PIC_X0 - x0), a, b, wy, PIC_W);
            for (int x = 0; x < w; x++) {
                const int px = x0 + x;
                if (px >= PIC_X0 && px < PIC_X0 + PIC_W) {
                    x = PIC_X0 + PIC_W - x0 - 1;  // (the picture is done: mix_row above)
                } else if (px >= PIC_X0 - LINE_W && px < PIC_X0 + PIC_W + LINE_W) {
                    o[x] = 0;
                } else {
                    o[x] = unspread_bus(dim(px < PIC_X0 ? s_left[k] : s_right[k], s_fade_x[px]));
                }
            }
        }
        send_rect(x0, x1, y0, y1, dst);
    }
    s_border_all = false;
    s_sum_ok = true;
}

static void flush_task(void *arg)
{
    frame_t f;
    int64_t t_log = esp_timer_get_time(), busy = 0;
    int frames = 0;
    for (;;) {
        xQueueReceive(s_flush_q, &f, portMAX_DELAY);
        const int64_t t0 = esp_timer_get_time();
        if (f.orient & ORIENT_RHI) {
            lcd_send_native_hi(f.fb, f.orient & 0xFF);
        } else if (f.orient & ORIENT_NATIVE) {
            lcd_send_native(f.fb, f.orient & 0xFF);
        } else {
            lcd_send_frame(f.fb, f.orient & 0xFF);
        }
        s_shown = f.orient;
        // (each 10 s: the frames and the mean time of one flush; the speed of the scale and the border)
        const int64_t t1 = esp_timer_get_time();
        busy += t1 - t0;
        frames++;
        if (t1 - t_log >= 10000000) {
            ESP_LOGI(TAG,
                     "lcd: %d frames in %d s (%s), a flush %.1f ms (%.1f ms of it waits for the SPI; the band %.1f, "
                     "the row scale %.1f), orient %d",
                     frames, (int)((t1 - t_log) / 1000000),
                     (f.orient & ORIENT_RHI)      ? "the round frame 466 (1:1)"
                     : (f.orient & ORIENT_NATIVE) ? "the round frame"
                                                  : "the 320 x 240 frame + border",
                     busy / 1000.0 / frames, s_wait_us / 1000.0 / frames, s_t_band / 1000.0 / frames,
                     s_t_hrow / 1000.0 / frames, f.orient & 0xFF);
            s_wait_us = s_t_band = s_t_hrow = 0;
            t_log = t1;
            busy = 0;
            frames = 0;
        }
        xSemaphoreGive(s_flush_idle);
        vTaskDelay(1);  // the lower tasks of this core (touch, idle) get time
    }
}

void board_lcd_flush(const uint16_t *fb, int orient)
{
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    frame_t f = {fb, orient};
    xQueueSend(s_flush_q, &f, portMAX_DELAY);
}

// black on the whole panel (the start)
static void lcd_clear(void)
{
    for (int c = 0; c < CHUNKS; c++) {
        uint16_t *dst = dma_next();
        memset(dst, 0, CHUNK_BYTES);
        send_chunk(c, dst);
    }
}

static void lcd_init(void)
{
    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_LCD_RST, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

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
    for (int i = 0; i < 2; i++) {
        s_hrow[i] = heap_caps_malloc(PANEL * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    s_band = heap_caps_malloc(BAND_ROWS * NAT * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(s_hrow[0] && s_hrow[1] && s_band);
    chord_init();
    zoom_table(s_nx, s_nwx, PANEL, NAT);
    zoom_table(s_ny, s_nwy, PANEL, NAT);
    zoom_table(s_fx, s_fwx, PIC_W, LCD_W);
    zoom_table(s_fy, s_fwy, PIC_H, LCD_H);
    fade_init();

    // the start list of the Waveshare BSP (CO5300), RGB565; the brightness stays 0 until the first frame
    lcd_cmd(0xFE, (uint8_t[]){0x20}, 1);
    lcd_cmd(0x19, (uint8_t[]){0x10}, 1);
    lcd_cmd(0x1C, (uint8_t[]){0xA0}, 1);
    lcd_cmd(0xFE, (uint8_t[]){0x00}, 1);
    lcd_cmd(0xC4, (uint8_t[]){0x80}, 1);
    lcd_cmd(0x3A, (uint8_t[]){0x55}, 1);  // RGB565
    lcd_cmd(0x35, (uint8_t[]){0x00}, 1);  // tear effect on
    lcd_cmd(0x53, (uint8_t[]){0x20}, 1);  // brightness control on
    lcd_cmd(0x51, (uint8_t[]){0x00}, 1);  // dark until the first frame
    lcd_cmd(0x63, (uint8_t[]){0xFF}, 1);
    lcd_cmd(0x11, NULL, 0);  // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
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

// A short push of PWR (AXP2101) or of BOOT (GPIO0) gives a KEY_LATCH_MS pulse (back). A push of 4 s on PWR stops the
// power (the AXP2101 does it).
bool board_boot_key_down(void)
{
    return false;
}  // (no second key for CHAT on this board)

bool board_pwr_key_down(void)
{
    uint32_t now = tfn_now_ms32();
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

// The CST9217 report (the Waveshare component esp_lcd_touch_cst9217): write the register 0xD000, then read 5 bytes for
// each point and 5 more; byte 6 = 0xAB, byte 5 & 0x7F = the points; point i at 5 i (+ 2 after the first): byte 0 & 0x0F
// = 6 (down), x = b1 << 4 | b3 >> 4, y = b2 << 4 | b3 & 15 (panel px, mirrored on both axes).
// The console "tpdiag N": the reads of each second by result; "tpreg" is not on this chip (16-bit registers).
static struct {
    int64_t until, t0;
    uint32_t ok, bad, err, touched;
} s_tpd;
void board_tp_diag(int secs)
{
    const int64_t now = esp_timer_get_time() / 1000;
    s_tpd.until = now + (secs > 120 ? 120 : secs) * 1000;
    s_tpd.t0 = now;
    s_tpd.ok = s_tpd.bad = s_tpd.err = s_tpd.touched = 0;
    ESP_LOGI(TAG, "tpdiag: %d s", secs);
}

static void tp_reset(void)
{
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));
}

// a panel point (glass px) to the logical point of the frame now; false = outside the picture by much (no touch)
// held: the finger was on the picture in the last read: then it follows the finger anywhere on the glass (clamped to
// the edge of the picture), as at the screen edge of the CoreS3 (a pull out into the border must not lose the finger,
// for example in GUMMY SIEGE)
static bool panel_to_logical(int tx, int ty, int *lx, int *ly, bool held)
{
    const int shown = s_shown;
    if (shown < 0) {
        return false;
    }
    const bool nat = shown & ORIENT_NATIVE;
    const int o = shown & 0xFF;
    int W, H, px, py;
    if (shown & ORIENT_RHI) {
        W = H = PANEL;  // (the hi round frame: the panel, 1:1)
        px = tx;
        py = ty;
    } else if (nat) {
        W = H = NAT;
        px = (tx * NAT + NAT / 2) / PANEL;
        py = (ty * NAT + NAT / 2) / PANEL;
    } else {
        W = LCD_W;
        H = LCD_H;
        px = ((tx - PIC_X0) * LCD_W + LCD_W / 2) / PIC_W;
        py = ((ty - PIC_Y0) * LCD_H + LCD_H / 2) / PIC_H;
        // a finger on the border near the picture is a touch at its edge (a thumb on a button at the edge)
        const int slack = 24;
        if (!held && (px < -slack || px >= W + slack || py < -slack || py >= H + slack)) {
            return false;
        }
    }
    px = px < 0 ? 0 : px >= W ? W - 1 : px;
    py = py < 0 ? 0 : py >= H ? H - 1 : py;
    switch (o) {
    case ORIENT_PORTRAIT:
        *lx = px;
        *ly = py;
        break;
    case ORIENT_PORTRAIT_FLIP:
        *lx = W - 1 - px;
        *ly = H - 1 - py;
        break;
    case ORIENT_LAND_A:
        *lx = py;
        *ly = W - 1 - px;
        break;
    default:
        *lx = H - 1 - py;
        *ly = px;
        break;
    }
    return true;
}

static int s_prev_n;  // the points of the last good read (a held finger: panel_to_logical)
int board_touch_points(int *x, int *y, int max)
{
    uint8_t reg[2] = {0xD0, 0x00}, buf[15];
    esp_err_t err = i2c_master_transmit(s_tp, reg, 2, I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = i2c_master_receive(s_tp, buf, sizeof buf, I2C_TIMEOUT_MS);
    }
    const int64_t now = esp_timer_get_time() / 1000;
    static uint32_t s_errors;
    if (err != ESP_OK) {
        if (s_tpd.until) {
            s_tpd.err++;
        }
        if ((s_errors++ % 500) == 0) {
            ESP_LOGW(TAG, "touch read failed: %s (%lu times)", esp_err_to_name(err), (unsigned long)s_errors);
        }
        if ((s_errors % 50) == 0) {
            tp_reset();
        }
        return 0;
    }
    int n = 0;
    if (s_tpd.until) {
        // (tpdiag: each new report, raw; the same bytes again are not logged)
        static uint8_t s_last[15];
        if (memcmp(s_last, buf, sizeof buf)) {
            memcpy(s_last, buf, sizeof buf);
            ESP_LOGI(TAG, "tpraw: %02X %02X %02X %02X %02X | %02X %02X | %02X %02X %02X %02X %02X %02X %02X %02X",
                     buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
                     buf[12], buf[13], buf[14]);
        }
    }
    if (buf[6] != TP_ACK) {
        if (s_tpd.until) {
            s_tpd.bad++;
        }
    } else {
        if (s_tpd.until) {
            s_tpd.ok++;
        }
        int points = buf[5] & 0x7F;
        if (points > 2) {
            points = 2;
        }
        for (int i = 0; i < points && n < max; i++) {
            const uint8_t *p = buf + i * 5 + (i ? 2 : 0);
            if ((p[0] & 0x0F) != 0x06) {
                continue;
            }
            int tx = (p[1] << 4) | (p[3] >> 4), ty = (p[2] << 4) | (p[3] & 0x0F);
            tx = PANEL - 1 - tx;  // (the BSP: mirror x, mirror y)
            ty = PANEL - 1 - ty;
            const bool in = panel_to_logical(tx, ty, &x[n], &y[n], s_prev_n > n);
            if (s_tpd.until) {
                static int s_ltx = -1, s_lty = -1;
                if (abs(tx - s_ltx) > 3 || abs(ty - s_lty) > 3) {
                    s_ltx = tx, s_lty = ty;
                    ESP_LOGI(TAG, "tppt: point %d of %d status %X panel (%d, %d) -> logical (%d, %d)%s, frame 0x%X",
                             i + 1, points, p[0] & 0x0F, tx, ty, in ? x[n] : -1, in ? y[n] : -1, in ? "" : " (outside)",
                             s_shown);
                }
            }
            if (!in) {
                continue;
            }
            n++;
        }
        if (s_tpd.until && n) {
            s_tpd.touched++;
        }
    }
    if (s_tpd.until && now - s_tpd.t0 >= 1000) {
        ESP_LOGI(TAG, "tpd: ok %lu, no report %lu, read faults %lu; with a finger %lu", (unsigned long)s_tpd.ok,
                 (unsigned long)s_tpd.bad, (unsigned long)s_tpd.err, (unsigned long)s_tpd.touched);
        s_tpd.t0 = now;
        s_tpd.ok = s_tpd.bad = s_tpd.err = s_tpd.touched = 0;
        if (now > s_tpd.until) {
            s_tpd.until = 0;
            ESP_LOGI(TAG, "tpdiag: end");
        }
    }
    s_prev_n = n;
    return n;
}

// the CST9217 INT line (low while it has a touch report)
int board_touch_int_gpio(void)
{
    return PIN_TP_INT;
}
void board_touch_int_ack(void)
{
}

// ---------------------------------------------------------------- battery, power

int board_battery(bool *charging, int *mv)
{
    return axp2101_battery(s_axp, charging, mv);
}

int board_ext_power(void)
{
    return axp2101_vbus(s_axp);
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

// ---------------------------------------------------------------- no SD card on this board

bool board_sd_mount(void)
{
    return false;
}
bool board_sd_check(void)
{
    return false;
}
bool board_sd_mounted(void)
{
    return false;
}
bool board_sd_info(uint32_t *total_kb, uint32_t *free_kb)
{
    (void)total_kb;
    (void)free_kb;
    return false;
}
bool board_sd_format(void)
{
    return false;
}

// ---------------------------------------------------------------- init

void board_init(void)
{
    gpio_config_t out = {.pin_bit_mask = (1ULL << PIN_PA) | (1ULL << PIN_TP_RST), .mode = GPIO_MODE_OUTPUT};
    gpio_config(&out);
    gpio_set_level(PIN_PA, 1);
    gpio_config_t in = {.pin_bit_mask = (1ULL << PIN_BOOT) | (1ULL << PIN_TP_INT),
                        .mode = GPIO_MODE_INPUT,
                        .pull_up_en = GPIO_PULLUP_ENABLE};
    gpio_config(&in);
    i2c_init();
    axp_init();
    tp_reset();
    lcd_init();
    ESP_LOGI(TAG,
             "AMOLED 1.75C: screen 466 x 466 round (the round frame 412 x 412, the CoreS3 frame at 280 x 372 + the "
             "ambient border), %d MHz",
             CONFIG_TFN_LCD_PCLK_MHZ);
}

#endif  // CONFIG_TFN_TARGET_AMOLED175
