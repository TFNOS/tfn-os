// Board support for the Waveshare ESP32-S3-Touch-LCD-3.49 (V1 and V2).
//
// LCD:   AXS15231B, 172x640, QSPI (CS 9, CLK 10, D0..D3 11..14)
// Touch: AXS15231B touch part, I2C1 (SDA 17, SCL 18), address 0x3B
// IO:    TCA9554 at 0x20 on I2C0 (SDA 47, SCL 48)
//
//            V1                  V2
// LCD RST    GPIO21              EXIO5
// LCD TE     EXIO5               GPIO21
// LCD BL     GPIO8  (PWM)        GPIO42 (PWM)
// EXIO INT   GPIO42              GPIO8
//
// Both: EXIO1 = BL_EN, EXIO6 = SYS_EN (battery power latch), EXIO7 = speaker amplifier
// (NS4150B) on, GPIO16 = PWR key (low = down).

#include "sdkconfig.h"
#if CONFIG_TFN_TARGET_WS349  // board_cores3.c and board_amoled.c are the other drivers

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "sdkconfig.h"
#include "board.h"
#include "files.h"
#include "board_sd.h"

static const char *TAG = "board";

#define PIN_I2C0_SDA GPIO_NUM_47
#define PIN_I2C0_SCL GPIO_NUM_48
#define PIN_TP_SDA   GPIO_NUM_17
#define PIN_TP_SCL   GPIO_NUM_18

#define PIN_LCD_CS  GPIO_NUM_9
#define PIN_LCD_CLK GPIO_NUM_10
#define PIN_LCD_D0  GPIO_NUM_11
#define PIN_LCD_D1  GPIO_NUM_12
#define PIN_LCD_D2  GPIO_NUM_13
#define PIN_LCD_D3  GPIO_NUM_14

#define PIN_V1_LCD_RST GPIO_NUM_21
#define PIN_V1_BL      GPIO_NUM_8
#define PIN_V2_BL      GPIO_NUM_42
#define PIN_PWR_KEY    GPIO_NUM_16
#define PIN_BOOT_KEY   GPIO_NUM_0  // the BOOT key (low = down): a long press opens CHAT (main.c)

#define TCA_ADDR       0x20
#define TCA_REG_INPUT  0
#define TCA_REG_OUTPUT 1
#define TCA_REG_CONFIG 3  // bit = 1: input, bit = 0: output

#define EXIO_BL_EN      (1u << 1)
#define EXIO_V2_LCD_RST (1u << 5)
#define EXIO_SYS_EN     (1u << 6)
#define EXIO_SPK_EN     (1u << 7)

#define TOUCH_ADDR 0x3B

#define LCD_HOST     SPI3_HOST
#define CHUNK_ROWS   32
#define CHUNK_PIXELS (LCD_W * CHUNK_ROWS)
#define CHUNK_BYTES  (CHUNK_PIXELS * 2)

// AXS15231B QSPI opcodes
#define QSPI_OP_CMD   0x02u
#define QSPI_OP_COLOR 0x32u

static board_ver_t s_ver;
static i2c_master_bus_handle_t s_bus0, s_bus1;
static i2c_master_dev_handle_t s_tca, s_tp;
static uint8_t s_tca_out = 0xFF;
static uint8_t s_tca_cfg = 0xFF;
static gpio_num_t s_bl_pin;

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_dma_free;
static uint16_t *s_dma_buf[2];
// The flush task sends one frame buffer while the app draws into the other one.
static QueueHandle_t s_flush_q;
typedef struct {
    const uint16_t *fb;
    int orient;
} frame_t;
static SemaphoreHandle_t s_flush_idle;  // given when the task does not read a frame buffer
static void flush_task(void *arg);

// ---------------------------------------------------------------- I2C / TCA9554

static void i2c_init(void)
{
    i2c_master_bus_config_t bc = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = PIN_I2C0_SDA,
            .scl_io_num = PIN_I2C0_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_bus0));

    bc.i2c_port = I2C_NUM_1;
    bc.sda_io_num = PIN_TP_SDA;
    bc.scl_io_num = PIN_TP_SCL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &s_bus1));

    i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = TCA_ADDR,
            .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus0, &dc, &s_tca));

    dc.device_address = TOUCH_ADDR;
    dc.scl_speed_hz = 300000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus1, &dc, &s_tp));
}

static esp_err_t tca_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_tca, b, sizeof(b), 50);
}

static esp_err_t tca_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_tca, &reg, 1, val, 1, 50);
}

static void tca_set(uint8_t mask, bool level)
{
    if (level) {
        s_tca_out |= mask;
    } else {
        s_tca_out &= ~mask;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(tca_write(TCA_REG_OUTPUT, s_tca_out));
}

static void tca_make_outputs(uint8_t mask)
{
    s_tca_cfg &= ~mask;
    ESP_ERROR_CHECK_WITHOUT_ABORT(tca_write(TCA_REG_CONFIG, s_tca_cfg));
}

// ---------------------------------------------------------------- version detect

// The EXIO_INT line has a 10k pull-up to 3V3. The LCD_BL line goes through
// about 49k to a node near GND. With the internal pull-downs on, the INT pin
// reads 1 and the BL pin reads 0. V1: INT = GPIO42. V2: INT = GPIO8.
static board_ver_t detect_version(void)
{
#if CONFIG_TFN_BOARD_V1
    return BOARD_V1;
#elif CONFIG_TFN_BOARD_V2
    return BOARD_V2;
#else
    uint8_t dummy;
    tca_read(TCA_REG_INPUT, &dummy);  // a read releases a pending INT

    gpio_config_t gc = {
            .pin_bit_mask = (1ULL << GPIO_NUM_8) | (1ULL << GPIO_NUM_42),
            .mode = GPIO_MODE_INPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gc);
    esp_rom_delay_us(3000);
    int l8 = gpio_get_level(GPIO_NUM_8);
    int l42 = gpio_get_level(GPIO_NUM_42);
    gpio_reset_pin(GPIO_NUM_8);
    gpio_reset_pin(GPIO_NUM_42);

    ESP_LOGI(TAG, "detect: GPIO8=%d GPIO42=%d", l8, l42);
    if (l8 && !l42) {
        return BOARD_V2;
    }
    if (!l8 && l42) {
        return BOARD_V1;
    }
    ESP_LOGW(TAG, "board revision not clear, use V2 (set it in menuconfig if wrong)");
    return BOARD_V2;
#endif
}

board_ver_t board_version(void)
{
    return s_ver;
}

i2c_master_bus_handle_t board_i2c0(void)
{
    return s_bus0;
}

// ---------------------------------------------------------------- backlight

static void backlight_init(void)
{
    ledc_timer_config_t tc = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_8_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 50000,
            .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tc));

    // The PWM is inverted: a high level pulls the LED driver feedback up and dims the LEDs.
    ledc_channel_config_t cc = {
            .gpio_num = s_bl_pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .duty = 255,
            .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&cc));
}

static bool s_spk_on = true;  // the state that board_speaker() asks for
static bool s_scr_sleep;
static bool s_spk_hold;  // board_speaker_hold(): the amplifier stays on in the screen sleep
void board_speaker(bool on)
{
    s_spk_on = on;
    if (!s_scr_sleep || s_spk_hold) {
        tca_set(EXIO_SPK_EN, on);
    }
}

void board_speaker_hold(bool hold)
{
    s_spk_hold = hold;
    if (s_scr_sleep) {
        tca_set(EXIO_SPK_EN, hold ? s_spk_on : 0);
    }
}

void board_backlight(uint8_t level)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 255 - level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    tca_set(EXIO_BL_EN, level > 0);
}

// ---------------------------------------------------------------- LCD

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

static void lcd_reset(void)
{
    if (s_ver == BOARD_V1) {
        gpio_config_t gc = {
                .pin_bit_mask = 1ULL << PIN_V1_LCD_RST,
                .mode = GPIO_MODE_OUTPUT,
                .pull_up_en = GPIO_PULLUP_ENABLE,
        };
        gpio_config(&gc);
        gpio_set_level(PIN_V1_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(30));
        gpio_set_level(PIN_V1_LCD_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        gpio_set_level(PIN_V1_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(30));
    } else {
        tca_set(EXIO_V2_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(30));
        tca_set(EXIO_V2_LCD_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(250));
        tca_set(EXIO_V2_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
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
            .max_transfer_sz = CHUNK_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {
            .cs_gpio_num = PIN_LCD_CS,
            .dc_gpio_num = GPIO_NUM_NC,
            .spi_mode = 3,
            .pclk_hz = CONFIG_TFN_LCD_PCLK_MHZ * 1000 * 1000,
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

    lcd_reset();

    lcd_cmd(0x11, NULL, 0);  // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x36, (uint8_t[]){0x00}, 1);  // MADCTL: normal scan
    lcd_cmd(0x3A, (uint8_t[]){0x55}, 1);  // COLMOD: RGB565
    lcd_cmd(0x29, NULL, 0);               // display on
    vTaskDelay(pdMS_TO_TICKS(20));

    s_flush_q = xQueueCreate(1, sizeof(frame_t));
    s_flush_idle = xSemaphoreCreateBinary();
    xSemaphoreGive(s_flush_idle);
    xTaskCreatePinnedToCore(flush_task, "lcd", 3072, NULL, 7, NULL, 1);
}

// Physical rows y0 .. y0 + CHUNK_ROWS - 1 from a frame buffer in the logical layout.
// Orientations as in gfx.h: 0 portrait, 1 portrait turned 180 degrees, 2 and 3 landscape.
static void copy_rows(uint16_t *dst, const uint16_t *fb, int y0, int orient)
{
    switch (orient) {
    case 0:
        memcpy(dst, fb + y0 * LCD_W, CHUNK_BYTES);
        break;
    case 1:
        for (int r = 0; r < CHUNK_ROWS; r++) {
            const uint16_t *s = fb + (LCD_H - 1 - (y0 + r)) * LCD_W + LCD_W - 1;
            uint16_t *d = dst + r * LCD_W;
            for (int x = 0; x < LCD_W; x++) {
                d[x] = *s--;
            }
        }
        break;
    case 2:
        // logical x = py, y = LCD_W - 1 - px; a logical row has LCD_H pixels.
        // Read 32 pixels in a row of the logical frame (one cache line) for each physical column.
        for (int px = 0; px < LCD_W; px++) {
            const uint16_t *s = fb + (LCD_W - 1 - px) * LCD_H + y0;
            uint16_t *d = dst + px;
            for (int r = 0; r < CHUNK_ROWS; r++) {
                d[r * LCD_W] = s[r];
            }
        }
        break;
    default:
        // logical x = LCD_H - 1 - py, y = px
        for (int px = 0; px < LCD_W; px++) {
            const uint16_t *s = fb + px * LCD_H + (LCD_H - 1 - y0);
            uint16_t *d = dst + px;
            for (int r = 0; r < CHUNK_ROWS; r++) {
                d[r * LCD_W] = s[-r];
            }
        }
        break;
    }
}

static void lcd_send(const uint16_t *fb, int orient)
{
    // In QSPI mode the AXS15231B ignores RASET. A frame starts with RAMWR (0x2C)
    // at row 0 and continues with RAMWRC (0x3C).
    lcd_cmd(0x2A, (uint8_t[]){0, 0, (LCD_W - 1) >> 8, (LCD_W - 1) & 0xFF}, 4);

    const int chunks = LCD_H / CHUNK_ROWS;
    for (int c = 0; c < chunks; c++) {
        xSemaphoreTake(s_dma_free, portMAX_DELAY);
        uint16_t *dst = s_dma_buf[c & 1];
        copy_rows(dst, fb, c * CHUNK_ROWS, orient);
        uint32_t cmd = (QSPI_OP_COLOR << 24) | ((c == 0 ? 0x2Cu : 0x3Cu) << 8);
        esp_lcd_panel_io_tx_color(s_io, (int)cmd, dst, CHUNK_BYTES);
    }
    // The last two chunks are still in flight. They use the DMA buffers only,
    // so the frame buffer is free now. The next lcd_cmd() waits for them.
}

static void flush_task(void *arg)
{
    frame_t f;
    for (;;) {
        xQueueReceive(s_flush_q, &f, portMAX_DELAY);
        lcd_send(f.fb, f.orient);
        xSemaphoreGive(s_flush_idle);
    }
}

void board_lcd_flush(const uint16_t *fb, int orient)
{
    // Wait until the task is done with the last frame, then give it this one.
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    frame_t f = {fb, orient};
    xQueueSend(s_flush_q, &f, portMAX_DELAY);
}

// ---------------------------------------------------------------- touch

// The report: [1] = number of points, then 6 bytes for each point:
// x high (4 bits) + event, x low, y high (4 bits) + id, y low, weight, area.
extern int64_t g_touch_log_until_ms;  // (console.c "touchw"): log each point with its weight and area
extern int64_t g_touch2_until_ms;     // (console.c "touch2"): log each report with the ids and the events

// the report log of TOUCH LAB (board.h): a ring that the input task fills and the app empties (PSRAM, only while on)
#define TREP_N 128
static touch_rep_t *s_trep;
static volatile int s_trep_w, s_trep_r;
static volatile bool s_trep_on;
static portMUX_TYPE s_trep_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_shifts[2], s_still_bad;  // the reports put back (shifted by 1, by 2), and the broken ones
static int s_rlen = 14;                    // the length of a touch read: 14 (2 points) or 32 (up to 5 points).
// (With 32 the chip can shift its reports and give no second finger after the first one drags;
// with 14 both thumbs come in. A 2-finger report is 14 bytes.)

void board_touch_read_len(int n)
{
    s_rlen = n == 14 ? 14 : 32;
    ESP_LOGI(TAG, "touch read length %d (put back so far: %lu shifted by 1, %lu by 2; still broken %lu)", s_rlen,
             (unsigned long)s_shifts[0], (unsigned long)s_shifts[1], (unsigned long)s_still_bad);
}

int board_touch_read_len_get(void)
{
    return s_rlen;
}

// n points from p (6 bytes each, room bytes there): each point on the panel, the ids of the chip 0..4 and each once;
// filler (for a shifted report, see board_touch_points): with one point, the place of a second point is 0xFF
static bool touch_pts_ok(const uint8_t *p, int n, int room, bool filler)
{
    if (n < 1 || 6 * n > room) {
        return false;
    }
    int ids = 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *q = p + 6 * i;
        int along = ((q[0] & 0x0F) << 8) | q[1], across = ((q[2] & 0x0F) << 8) | q[3], id = q[2] >> 4;
        if ((q[0] & 0x30) || (q[2] & 0x0E) || along >= LCD_H || across >= LCD_W) {
            return false;
        }
        if (id > 4 || (ids & (1 << id))) {
            return false;
        }
        ids |= 1 << id;
    }
    if (filler && n == 1) {
        for (int i = 6; i < 12 && i < room; i++) {
            if (p[i] != 0xFF) {
                return false;
            }
        }
    }
    return true;
}

void board_touch_log(bool on)
{
    if (on && !s_trep) {
        s_trep = heap_caps_calloc(TREP_N, sizeof(touch_rep_t), MALLOC_CAP_SPIRAM);
    }
    portENTER_CRITICAL(&s_trep_lock);
    s_trep_on = on && s_trep;
    s_trep_w = s_trep_r = 0;
    portEXIT_CRITICAL(&s_trep_lock);
    if (!on && s_trep) {
        vTaskDelay(pdMS_TO_TICKS(30));  // (the input task may be in the last write)
        portENTER_CRITICAL(&s_trep_lock);
        touch_rep_t *p = s_trep;
        s_trep = NULL;
        portEXIT_CRITICAL(&s_trep_lock);
        heap_caps_free(p);
    }
}

int board_touch_reports(touch_rep_t *out, int max)
{
    int k = 0;
    if (!s_trep) {
        return 0;
    }
    portENTER_CRITICAL(&s_trep_lock);
    while (s_trep_r != s_trep_w && k < max) {
        out[k++] = s_trep[s_trep_r];
        s_trep_r = (s_trep_r + 1) % TREP_N;
    }
    portEXIT_CRITICAL(&s_trep_lock);
    return k;
}
int board_touch_points(int *x, int *y, int max)
{
    // The read command gives the report length (byte 7). A report of 2 + 6 x 5 bytes can hold 5 points (32); the
    // 14-byte report holds 2 (s_rlen: the console "touchread"; 14 if the panel does not take the long read).
    uint8_t cmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, (uint8_t)s_rlen, 0x00, 0x00, 0x00};
    uint8_t buf[32] = {0};
    int len = s_rlen;

    static uint32_t s_errors;
    esp_err_t err = i2c_master_transmit_receive(s_tp, cmd, 11, buf, len, 20);
    if (err != ESP_OK && len == 32) {
        s_rlen = 14;
        ESP_LOGW(TAG, "touch: the long report failed, 2 points only");
        len = 14;
        cmd[7] = 14;
        err = i2c_master_transmit_receive(s_tp, cmd, sizeof(cmd), buf, len, 20);
    }
    if (err != ESP_OK) {
        if ((s_errors++ % 500) == 0) {
            ESP_LOGW(TAG, "touch read failed: %s (%lu times)", esp_err_to_name(err), (unsigned long)s_errors);
        }
        return 0;
    }
    // A SHIFTED report (seen in TOUCH LAB, in the raw bytes): the chip can drop the first byte of its report (the
    // gesture byte: "01 80 7c 00 7d 53 54 ff ff ..." = count 1 and one point) or the first two (also the count:
    // "80 7e 00 7f 62 63 ff ff ..." = one point). The count byte was then a point byte (61, 65, 129, 130, 201 ...),
    // the report was "no touch", and a thumb was lost in a drag. Put it back in its place.
    uint8_t raw0[14];
    memcpy(raw0, buf, sizeof raw0);  // (the bytes as they came, for TOUCH LAB)
    int shift = 0;
    // a good report: no touch (count 0 or 16), or 1 .. (len - 2) / 6 points on the panel with valid ids
    int cnt = buf[1];
    bool odd = !(cnt == 0 || cnt == 16 ||
                 (cnt >= 1 && cnt <= (len - 2) / 6 && touch_pts_ok(buf + 2, cnt, len - 2, false)));
    if (odd) {
        int n2 = 0;  // (shift 2: the count is lost too: 2 points, else 1)
        if (buf[0] >= 1 && buf[0] <= 5 && touch_pts_ok(buf + 1, buf[0], len - 1, true)) {
            shift = 1;
        } else if (touch_pts_ok(buf, 2, len, true)) {
            shift = 2, n2 = 2;
        } else if (touch_pts_ok(buf, 1, len, true)) {
            shift = 2, n2 = 1;
        }
        if (shift) {
            memmove(buf + shift, buf, (size_t)(len - shift));
            buf[0] = 0;
            if (shift == 2) {
                buf[1] = (uint8_t)n2;
            }
            s_shifts[shift - 1]++;
        }
    }
    bool bad = odd && !shift;  // (no shift explains it: no touch for this poll)
    int points = bad ? 0 : buf[1];
    if (bad) {
        s_still_bad++;
    }
    if (s_trep_on) {
        // one report for TOUCH LAB: as it came (no clamp: a point off the panel shows a broken report)
        touch_rep_t r = {.t_ms = (uint32_t)(esp_timer_get_time() / 1000),
                         .count = (uint8_t)(bad ? raw0[1] : points),
                         .n = 0,
                         .shift = (uint8_t)shift,
                         .bad = bad};
        memcpy(r.raw, raw0, sizeof raw0);
        int np = points > 0 && points <= 10 ? points : 0;
        if (np > (len - 2) / 6) {
            np = (len - 2) / 6;
        }
        if (np > 5) {
            np = 5;
        }
        for (int i = 0; i < np; i++) {
            const uint8_t *p = buf + 2 + 6 * i;
            int rx = ((p[0] & 0x0F) << 8) | p[1], ry = ((p[2] & 0x0F) << 8) | p[3];
            r.p[i].x = (int16_t)ry;
            r.p[i].y = (int16_t)(LCD_H - rx);
            r.p[i].id = (uint8_t)(p[2] >> 4);
            r.p[i].ev = (uint8_t)(p[0] >> 6);
        }
        r.n = (uint8_t)np;
        portENTER_CRITICAL(&s_trep_lock);
        int nw = (s_trep_w + 1) % TREP_N;
        if (s_trep && nw != s_trep_r) {  // (full: the app did not read; drop the new one)
            s_trep[s_trep_w] = r;
            s_trep_w = nw;
        }
        portEXIT_CRITICAL(&s_trep_lock);
    }
    if (g_touch2_until_ms) {
        // one line for each report: the time, the count, then x,y,id,event of each point (board pixels as below)
        int64_t now = esp_timer_get_time() / 1000;
        if (now > g_touch2_until_ms) {
            g_touch2_until_ms = 0;
            ESP_LOGI(TAG, "t2 end");
        } else {
            char line[160];
            int k = snprintf(line, sizeof(line), "t2 %lu %d", (unsigned long)now, points);
            int np = points > (len - 2) / 6 ? (len - 2) / 6 : points;
            for (int i = 0; i < np && k < (int)sizeof(line) - 24; i++) {
                const uint8_t *p = buf + 2 + 6 * i;
                int rx = ((p[0] & 0x0F) << 8) | p[1], ry = ((p[2] & 0x0F) << 8) | p[3];
                k += snprintf(line + k, sizeof(line) - (size_t)k, " %d,%d,%d,%d", ry, LCD_H - rx, p[2] >> 4, p[0] >> 6);
            }
            ESP_LOGI(TAG, "%s", line);
        }
    }
    if (points == 0 || points > 10) {
        return 0;
    }
    if (points > (len - 2) / 6) {
        points = (len - 2) / 6;  // the report holds this many
    }
    if (points > max) {
        points = max;
    }
    for (int i = 0; i < points; i++) {
        const uint8_t *p = buf + 2 + 6 * i;
        int raw_x = ((p[0] & 0x0F) << 8) | p[1];  // along the long side
        int raw_y = ((p[2] & 0x0F) << 8) | p[3];  // along the short side
        int px = raw_y;
        int py = LCD_H - raw_x;
        if (px < 0) {
            px = 0;
        }
        if (px >= LCD_W) {
            px = LCD_W - 1;
        }
        if (py < 0) {
            py = 0;
        }
        if (py >= LCD_H) {
            py = LCD_H - 1;
        }
#if CONFIG_TFN_TOUCH_DEBUG
        ESP_LOGI(TAG, "touch %d/%d raw=(%d,%d) screen=(%d,%d)", i + 1, points, raw_x, raw_y, px, py);
#endif
        if (g_touch_log_until_ms) {
            static int64_t s_tw_at;
            int64_t now = esp_timer_get_time() / 1000;
            if (now > g_touch_log_until_ms) {
                g_touch_log_until_ms = 0;
            } else if (i == 0 && now - s_tw_at >= 40) {
                s_tw_at = now;
                ESP_LOGI(TAG, "touchw: %d points, (%d,%d) event %d, weight %u, area %u", points, px, py, p[0] >> 6,
                         p[4], p[5]);
            }
        }
        x[i] = px;
        y[i] = py;
    }
    return points;
}

// ---------------------------------------------------------------- power

bool board_pwr_key_down(void)
{
    return gpio_get_level(PIN_PWR_KEY) == 0;
}

bool board_boot_key_down(void)
{
    return gpio_get_level(PIN_BOOT_KEY) == 0;
}

// ---------------------------------------------------------------- battery
// The battery voltage comes to GPIO4 (ADC1 channel 3) through a 1:3 divider (Waveshare demo
// 01_ADC_Test). The board has no charge status line.

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cali;
static bool s_adc_ok, s_adc_tried;
static float s_bat_mv;  // filtered battery voltage
static int64_t s_bat_at;

// Li-ion voltage (mV) to charge (%), a typical curve for a small cell at a low load
static int bat_percent(float mv)
{
    static const int16_t V[] = {3300, 3500, 3600, 3650, 3700, 3750, 3800, 3900, 4000, 4100, 4180};
    static const int8_t P[] = {0, 5, 12, 20, 30, 40, 50, 65, 80, 92, 100};
    if (mv <= V[0]) {
        return 0;
    }
    for (int i = 1; i < (int)(sizeof(V) / sizeof(V[0])); i++) {
        if (mv < V[i]) {
            return P[i - 1] + (int)((mv - V[i - 1]) * (P[i] - P[i - 1]) / (V[i] - V[i - 1]));
        }
    }
    return 100;
}

int board_battery(bool *charging, int *mv)
{
    if (!s_adc_tried) {
        s_adc_tried = true;
        adc_oneshot_unit_init_cfg_t uc = {.unit_id = ADC_UNIT_1};
        adc_oneshot_chan_cfg_t cc = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
        adc_cali_curve_fitting_config_t kc = {
                .unit_id = ADC_UNIT_1,
                .chan = ADC_CHANNEL_3,
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_12,
        };
        s_adc_ok = adc_oneshot_new_unit(&uc, &s_adc) == ESP_OK &&
                   adc_oneshot_config_channel(s_adc, ADC_CHANNEL_3, &cc) == ESP_OK &&
                   adc_cali_create_scheme_curve_fitting(&kc, &s_adc_cali) == ESP_OK;
        if (!s_adc_ok) {
            ESP_LOGW(TAG, "battery ADC not available");
        }
    }
    int64_t now = esp_timer_get_time();
    if (s_adc_ok && (s_bat_at == 0 || now - s_bat_at > 2000000)) {
        s_bat_at = now;
        int sum = 0, n = 0;
        for (int i = 0; i < 8; i++) {
            int raw, v;
            if (adc_oneshot_read(s_adc, ADC_CHANNEL_3, &raw) == ESP_OK &&
                adc_cali_raw_to_voltage(s_adc_cali, raw, &v) == ESP_OK) {
                sum += v;
                n++;
            }
        }
        if (n) {
            float v = 3.0f * sum / n;
            s_bat_mv = s_bat_mv == 0 ? v : s_bat_mv + 0.3f * (v - s_bat_mv);
        }
    }
    if (mv) {
        *mv = (int)s_bat_mv;
    }
    if (!s_adc_ok || s_bat_mv < 2500) {
        if (charging) {
            *charging = false;
        }
        return -1;  // no battery (or no reading)
    }
    if (charging) {
        *charging = s_bat_mv > 4250;  // above a full cell: USB power
    }
    return bat_percent(s_bat_mv);
}

void board_screen_sleep(bool sleep)
{
    // Only the amplifier. The LCD must not sleep: the touch panel is in the same chip
    // (AXS15231B), and a sleeping chip reads no touch, so a touch cannot wake the screen.
    if (sleep == s_scr_sleep) {
        return;
    }
    tca_set(EXIO_SPK_EN, sleep && !s_spk_hold ? 0 : s_spk_on);
    s_scr_sleep = sleep;
}

// no touch INT line on the 3.49 (the touch is read each 50 ms with the screen off)
int board_touch_int_gpio(void)
{
    return -1;
}
void board_touch_int_ack(void)
{
}

void board_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    board_backlight(0);
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);  // no more frames after this
    // No display off and no sleep in (0x28, 0x10): the AXS15231B does not wake from sleep in, not with its reset line
    // and not with sleep out (tested on a V2: the screen stays black and the touch part answers no read);
    // only the loss of its power helps. On the battery the power goes anyway; on USB power (the deep sleep below) a
    // sleeping chip gave a black screen at the next start, until a power cut. The backlight is off: nothing shows.
    tca_set(EXIO_SPK_EN, 0);
    tca_set(EXIO_SYS_EN, 0);

    // On battery the power goes away when the key is released.
    while (board_pwr_key_down()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    // Still here: USB power. Sleep until the PWR key is pushed again.
    esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_PWR_KEY, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------- the microSD card
// The TF slot: SDMMC 1-bit, CLK 41, CMD 39, D0 40 (as the Waveshare demo 04_SD_Card). board_sd.c has the rest.

#define PIN_SD_CLK GPIO_NUM_41
#define PIN_SD_CMD GPIO_NUM_39
#define PIN_SD_D0  GPIO_NUM_40

esp_err_t board_sd_card_mount(const esp_vfs_fat_mount_config_t *mc, sdmmc_card_t **card)
{
    return board_sd_mount_sdmmc_1bit(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, mc, card);
}

// ---------------------------------------------------------------- init

void board_init(void)
{
    board_sd_init();
    gpio_config_t key = {
            .pin_bit_mask = (1ULL << PIN_PWR_KEY) | (1ULL << PIN_BOOT_KEY),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&key);

    i2c_init();

    s_ver = detect_version();
    s_bl_pin = (s_ver == BOARD_V1) ? PIN_V1_BL : PIN_V2_BL;
    ESP_LOGI(TAG, "board revision V%d", (int)s_ver);

    // Keep the current output levels, then set the pins that this code owns.
    if (tca_read(TCA_REG_OUTPUT, &s_tca_out) != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 does not answer");
        s_tca_out = 0xFF;
    }
    s_tca_out |= EXIO_SYS_EN;  // hold the battery power latch
    s_tca_out |= EXIO_SPK_EN;  // speaker amplifier on
    s_tca_out &= ~EXIO_BL_EN;  // backlight off until the first frame
    uint8_t outs = EXIO_SYS_EN | EXIO_BL_EN | EXIO_SPK_EN;
    if (s_ver == BOARD_V2) {
        s_tca_out |= EXIO_V2_LCD_RST;
        outs |= EXIO_V2_LCD_RST;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(tca_write(TCA_REG_OUTPUT, s_tca_out));
    tca_make_outputs(outs);

    backlight_init();
    lcd_init();
}

#endif  // CONFIG_TFN_TARGET_WS349
