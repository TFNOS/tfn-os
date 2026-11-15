// Board support for the M5Stack CoreS3-SE. It has the same API as board.c (see board.h).
// The build uses this file in place of board.c when CONFIG_TFN_TARGET_CORES3SE is on.
//
// LCD:   ILI9342C or ILI9342E, 320x240 (the native frame is landscape), SPI
//        (SCK 36, MOSI 37, DC 35, CS 3). There is no reset GPIO: AW9523B P1_1 resets it.
//        GPIO35 is also the MISO line of the SD card. This code does not use MISO.
// Touch: FT6336U at 0x38. The native touch frame is the native LCD frame (M5GFX).
// I2C:   one internal bus (SDA 12, SCL 11): AXP2101 0x34, AW9523B 0x58, FT6336U 0x38,
//        AW88298 0x36, ES7210 0x40, BM8563 0x51.
// Power: AXP2101. DLDO1 = backlight, ALDO1 = AW88298 (1.8 V), ALDO2 = ES7210 (3.3 V),
//        ALDO4 = SD card (3.3 V). The PWR key connects to the AXP2101 only, and the
//        AXP2101 IRQ line does not go to the ESP32. This code reads the IRQ status on I2C.
// IO:    AW9523B. P0_0 = touch reset, P0_2 = AW88298 reset (high = on), P1_1 = LCD reset,
//        P1_2 = touch INT (input), P1_7 = SY7088 boost on. The AW9523B INTN goes to GPIO21.
//
// The two LCD versions need different init lists. The touch firmware ID tells the
// version (0x12 = ILI9342E, other values = ILI9342C), as in the ESP-BSP.
//
// Orientation. The native frame (column c 0..319, row r 0..239) is the normal landscape
// view of M5 (the view of the M5GFX and ESP-BSP demos). The physical frame of gfx.h
// (LCD_W x LCD_H = 240 x 320) is this view turned 90 degrees: px = 239 - r, py = c.
//   ORIENT_LAND_A        the normal landscape view (logical x = c, y = r)
//   ORIENT_LAND_B        the landscape view turned 180 degrees
//   ORIENT_PORTRAIT      the board turned 90 degrees clockwise from the normal view
//   ORIENT_PORTRAIT_FLIP the board turned 90 degrees counter-clockwise
// The LCD turns the frame (MADCTL), so the flush task sends the rows with no change.

#include "tfn_util.h"
#include "sdkconfig.h"

#if CONFIG_TFN_TARGET_CORES3SE

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/rtc_io.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "board.h"
#include "board_i2c.h"
#include "pmic_axp2101.h"
#include "files.h"
#include "gfx.h"
#include "board_sd.h"
#include "sd_dma.h"

extern int64_t g_touch2_until_ms;  // (console.c "touch2"): log each touch read with the ids and the events

_Static_assert(LCD_W == 240 && LCD_H == 320, "the CoreS3-SE needs LCD_W 240 and LCD_H 320 in board.h");

static const char *TAG = "board";

#define PIN_I2C_SDA  GPIO_NUM_12
#define PIN_I2C_SCL  GPIO_NUM_11
#define PIN_LCD_SCK  GPIO_NUM_36
#define PIN_LCD_MOSI GPIO_NUM_37
#define PIN_LCD_DC   GPIO_NUM_35
#define PIN_LCD_CS   GPIO_NUM_3
#define PIN_SYS_INT  GPIO_NUM_21  // AW9523B INTN (low = an input changed)
#define PIN_SD_CS    GPIO_NUM_4   // the microSD card on the LCD bus (MISO = GPIO35)

#define AW_ADDR  0x58
#define TP_ADDR  0x38
#define AMP_ADDR 0x36

// AXP2101 registers

#define LDO_DLDO1  0x80
#define LDO_ALDO3  0x04  // the camera: off until the web camera needs it (board_camera_power)
#define LDO_ON     0x3F  // ALDO1..4, BLDO1..2 (as M5Unified); DLDO1 is the backlight
#define PKEY_SHORT 0x08  // IRQ status 1: PWR key short press
#define PKEY_ALL   0x0F  // short press, long press, and the two edges

// AW9523B registers
#define AW_OUT0  0x02  // output levels of port 0, port 1 = 0x03
#define AW_DIR0  0x04  // bit = 1: input, bit = 0: output (port 1 = 0x05)
#define AW_DIR1  0x05
#define AW_INT0  0x06  // bit = 1: no interrupt for this pin (port 1 = 0x07)
#define AW_INT1  0x07
#define AW_IN0   0x00  // a read of the inputs releases INTN (port 1 = 0x01)
#define AW_IN1   0x01
#define AW_ID    0x10  // 0x23
#define AW_GCR   0x11  // bit 4: port 0 push-pull
#define AW_MODE0 0x12  // bit = 1: GPIO mode (port 1 = 0x13)
#define AW_MODE1 0x13

#define AW0_TP_RST     (1u << 0)
#define AW0_BUS_OUT_EN (1u << 1)  // the 5 V of the boost to BUS_OUT (and not BUS_OUT to VBUS)
#define AW0_AMP_RST    (1u << 2)
#define AW0_USB_OTG_EN (1u << 5)  // BUS_OUT to the USB-C (VUSB), and not VUSB to VBUS (no charge)
#define AW1_CAM_RST    (1u << 0)
#define AW1_LCD_RST    (1u << 1)
#define AW1_TP_INT     (1u << 2)
#define AW1_BOOST_EN   (1u << 7)

// FT6336U registers
#define TP_MODE      0x00
#define TP_TD_STATUS 0x02  // number of points, then 6 bytes for each point
#define TP_FIRMID    0xA6
#define TP_FIRMID_E  0x12  // the touch firmware of the boards with the ILI9342E
#define TP_EVENT_UP  1     // event of a point: 0 down, 1 up, 2 contact

// AW88298 (16-bit registers, high byte first)
#define AMP_SYSCTRL2 0x05
#define AMP_HMUTE    (1u << 4)

// The native LCD frame
#define NATIVE_W 320
#define NATIVE_H 240

// MADCTL bits of the ILI9342
#define MAD_MY  0x80  // turn the native rows
#define MAD_MX  0x40  // turn the native columns
#define MAD_MV  0x20  // exchange the rows and the columns
#define MAD_BGR 0x08

// 40 MHz is the clock of M5GFX and of the ESP-BSP: a frame takes about 31 ms. The CoreS3
// build uses 80 MHz (sdkconfig.defaults.cores3se): tested on a board with the ILI9342E.
// 60 or 80 MHz can work (the pins go through the GPIO matrix), but the ILI9342
// datasheet does not give these values. Do a test on the board before a change.
#define LCD_HOST     SPI3_HOST
#define LCD_PCLK_HZ  (CONFIG_TFN_LCD_PCLK_MHZ * 1000 * 1000)  // menuconfig, default 40
#define LCD_PIXELS   (LCD_W * LCD_H)
#define CHUNKS       20                     // even: the chunks use the two buffers in turn
#define CHUNK_PIXELS (LCD_PIXELS / CHUNKS)  // 12 rows of 320 or 16 rows of 240
#define CHUNK_BYTES  (CHUNK_PIXELS * 2)

#define KEY_LATCH_MS 150  // board_pwr_key_down() is true for this time after a push
#define KEY_POLL_MS  30   // read the AXP2101 IRQ status not more often than this

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_axp, s_aw, s_tp, s_amp;
static uint8_t s_aw_out[2];        // output levels of the AW9523B ports
static uint8_t s_ldo_en = LDO_ON;  // the value of AXP_LDO_EN
static bool s_panel_e;             // true: ILI9342E, false: ILI9342C

static volatile uint32_t s_key_until;  // board_pwr_key_down() is true until this time (ms)
static uint32_t s_key_polled;
static bool s_strip_down;

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_dma_free;
static uint16_t *s_dma_buf[2];
static uint8_t s_madctl;  // the MADCTL value in the LCD now
// The flush task sends one frame buffer while the app draws into the other one.
static QueueHandle_t s_flush_q;
typedef struct {
    const uint16_t *fb;
    int orient;
} frame_t;
static SemaphoreHandle_t s_flush_idle;  // given when the task does not read a frame buffer
static void flush_task(void *arg);

// ---------------------------------------------------------------- pure functions

// The MADCTL value for an orientation (orient_t of gfx.h). The frame buffer goes to the
// LCD row after row, from the logical (0, 0). With MV = 0, the column counter (CASET)
// gives the native column and the page counter (RASET) gives the native row. With MV = 1,
// the column counter gives the native row and the page counter gives the native column.
// MX turns the native columns (c = 319 - c), MY turns the native rows (r = 239 - r).
static uint8_t lcd_madctl(int orient)
{
    switch (orient) {
    case ORIENT_PORTRAIT:
        return MAD_MV | MAD_MY | MAD_BGR;  // c = y, r = 239 - x
    case ORIENT_PORTRAIT_FLIP:
        return MAD_MV | MAD_MX | MAD_BGR;  // c = 319 - y, r = x
    case ORIENT_LAND_A:
        return MAD_BGR;  // c = x, r = y
    default:
        return MAD_MY | MAD_MX | MAD_BGR;  // c = 319 - x, r = 239 - y
    }
}

// A native touch point (tx 0..319, ty 0..239) in the physical frame of gfx.h.
static void touch_native_to_phys(int tx, int ty, int *px, int *py)
{
    *px = LCD_W - 1 - ty;
    *py = tx;
}

// Backlight level 1..255 to the DLDO1 value: 2.5 V .. 3.3 V, as M5GFX.
static uint8_t backlight_code(uint8_t level)
{
    return (uint8_t)((level + 641) >> 5);
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
    s_aw = add_dev(s_bus, AW_ADDR);
    s_tp = add_dev(s_bus, TP_ADDR);
    s_amp = add_dev(s_bus, AMP_ADDR);
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

// The values of M5Unified for the CoreS3. The voltages come before the enable bits.
static void axp_init(void)
{
    uint8_t v = 0;
    axp2101_wait_id(s_axp);
    // A short voltage drop (USB inrush) must not stop the power: the chip restarts instead.
    if (reg_read(s_axp, AXP_DCDC_UVP, &v, 1) == ESP_OK) {
        reg_write(s_axp, AXP_DCDC_UVP, v & ~0x1F);
    }
    static const uint8_t init[][2] = {
            {AXP_ALDO1_V, AXP_MV(1800)},        // AW88298
            {AXP_ALDO2_V, AXP_MV(3300)},        // ES7210
            {AXP_ALDO3_V, AXP_MV(3300)},        // camera (the SE has no camera)
            {AXP_ALDO4_V, AXP_MV(3300)},        // SD card
            {AXP_LDO_EN, LDO_ON & ~LDO_ALDO3},  // backlight off until the first frame; the camera off
            {AXP_PKEY_TIME, 0x00},              // long press IRQ after 1 s, power off after 4 s
            {AXP_CHGLED, 0x11},                 // the charger controls the charge LED
            {AXP_PMU_CFG, 0x30},
            {AXP_ADC_EN, 0x0F},
            {AXP_IRQ_ST0, 0xFF},  // remove old events (for example the power-on push)
            {AXP_IRQ_ST1, 0xFF},
            {AXP_IRQ_ST2, 0xFF},
    };
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); i++) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_axp, init[i][0], init[i][1]));
    }
    s_ldo_en = LDO_ON & ~LDO_ALDO3;
}

void board_backlight(uint8_t level)
{
    if (level) {
        reg_write(s_axp, AXP_DLDO1_V, backlight_code(level));
        s_ldo_en |= LDO_DLDO1;
    } else {
        s_ldo_en &= (uint8_t)~LDO_DLDO1;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_axp, AXP_LDO_EN, s_ldo_en));
}

// ---------------------------------------------------------------- AW9523B

static void aw_set(int port, uint8_t mask, bool level)
{
    if (level) {
        s_aw_out[port] |= mask;
    } else {
        s_aw_out[port] &= (uint8_t)~mask;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, (uint8_t)(AW_OUT0 + port), s_aw_out[port]));
}

// The values of M5GFX and M5Unified. Keep the levels of the pins that this code does not own.
static void aw_init(void)
{
    uint8_t id = 0;
    for (int i = 0; i < 5; i++) {
        if (reg_read(s_aw, AW_ID, &id, 1) == ESP_OK && id == 0x23) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (id != 0x23) {
        ESP_LOGE(TAG, "AW9523B does not answer (ID 0x%02X)", id);
    }
    for (int p = 0; p < 2; p++) {
        if (reg_read(s_aw, (uint8_t)(AW_OUT0 + p), &s_aw_out[p], 1) != ESP_OK) {
            s_aw_out[p] = 0;
        }
    }
    s_aw_out[0] |= AW0_TP_RST | AW0_AMP_RST;
    s_aw_out[1] |= AW1_LCD_RST | AW1_BOOST_EN;  // (the camera reset stays low: no power)
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_OUT0, s_aw_out[0]));
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_OUT0 + 1, s_aw_out[1]));
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_DIR0, 0x18));   // P0_3, P0_4 are inputs
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_DIR1, 0x0C));   // P1_2, P1_3 are inputs
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_GCR, 0x10));    // port 0 push-pull
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_MODE0, 0xFF));  // GPIO mode, not LED mode
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_MODE1, 0xFF));
    // only the touch INT (P1_2) pulls INTN (GPIO21) low: input.c waits for it while the screen is off
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_INT0, 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_aw, AW_INT1, (uint8_t)~AW1_TP_INT));
}

// ---------------------------------------------------------------- speaker

// The AW88298 hard mute bit. audio.c sets the amplifier up (SYSCTRL2 = 0x0008, not muted);
// this function changes only the mute bit. The AW88298 reset line (P0_2) stays high.
static bool s_spk_on = true;  // the state that board_speaker() asks for
static bool s_scr_sleep;
static void amp_mute(bool on);
static bool s_spk_hold;  // board_speaker_hold(): the amplifier stays on in the screen sleep
void board_speaker(bool on)
{
    s_spk_on = on;
    if (!s_scr_sleep || s_spk_hold) {
        amp_mute(!on);
    }
}

void board_speaker_hold(bool hold)
{
    s_spk_hold = hold;
    if (s_scr_sleep) {
        amp_mute(hold ? !s_spk_on : true);
    }
}
static void amp_mute(bool mute)
{
    bool on = !mute;
    uint8_t reg = AMP_SYSCTRL2;
    uint8_t v[2];
    if (reg_read(s_amp, reg, v, sizeof(v)) != ESP_OK) {
        ESP_LOGW(TAG, "AW88298 does not answer");
        return;
    }
    uint16_t val = (uint16_t)((v[0] << 8) | v[1]);
    val = on ? (uint16_t)(val & ~AMP_HMUTE) : (uint16_t)(val | AMP_HMUTE);
    uint8_t b[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_master_transmit(s_amp, b, sizeof(b), I2C_TIMEOUT_MS));
}

// ---------------------------------------------------------------- touch version

// The FT6336U needs about 300 ms after its reset. Returns true for the ILI9342E.
static bool detect_panel_e(void)
{
    uint8_t id = 0;
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 5 && err != ESP_OK; i++) {
        if (i) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        err = reg_write(s_tp, TP_MODE, 0x00);  // working mode
        if (err == ESP_OK) {
            err = reg_read(s_tp, TP_FIRMID, &id, 1);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch firmware ID not read (%s): use the ILI9342C list", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "touch firmware ID 0x%02X: LCD %s", id, id == TP_FIRMID_E ? "ILI9342E" : "ILI9342C");
    return id == TP_FIRMID_E;
}

// ---------------------------------------------------------------- LCD

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[15];
} lcd_init_t;

// ILI9342C: the list of Panel_ILI9342 in M5GFX (LovyanGFX).
static const lcd_init_t INIT_C[] = {
        {0xC8, 3, {0xFF, 0x93, 0x42}},  // Set EXTC: open the extended commands
        {0xC0, 2, {0x12, 0x12}},        // power control 1
        {0xC1, 1, {0x03}},              // power control 2
        {0xC5, 1, {0xF2}},              // VCOM control 1
        {0xB0, 1, {0xE0}},              // RGB interface signal control
        {0xF6, 3, {0x01, 0x00, 0x00}},  // interface control
        {0xE0, 15, {0x00, 0x0C, 0x11, 0x04, 0x11, 0x08, 0x37, 0x89, 0x4C, 0x06, 0x0C, 0x0A, 0x2E, 0x34, 0x0F}},
        {0xE1, 15, {0x00, 0x0B, 0x11, 0x05, 0x13, 0x09, 0x33, 0x67, 0x48, 0x07, 0x0E, 0x0B, 0x2E, 0x33, 0x0F}},
        {0xB6, 4, {0x08, 0x82, 0x1D, 0x04}},  // display function control
};

// ILI9342E: the list of Panel_ILI9342E in M5GFX (the ESP-BSP list has the same values).
// The ILI9342C list must not go to this panel: C8h is a VCOM register here.
static const lcd_init_t INIT_E[] = {
        {0xDD, 1, {0x01}},  // Set EXTC
        {0xD5, 1, {0x00}},
        {0xB1, 1, {0x22}},  // frame rate
        {0xC8, 1, {0x38}},  // power control 3 (VCOM)
        {0xCB, 1, {0x1C}},
        {0xC9, 1, {0x1A}},
        {0xCA, 1, {0x1A}},
        {0xB7, 4, {0x5A, 0x41, 0x11, 0x19}},
        {0xE4, 15, {0x04, 0x08, 0x11, 0x06, 0x12, 0x07, 0x3A, 0x76, 0x47, 0x07, 0x0F, 0x0A, 0x11, 0x19, 0x05}},
        {0xE5, 15, {0x02, 0x03, 0x07, 0x06, 0x12, 0x07, 0x36, 0x5F, 0x48, 0x06, 0x10, 0x0C, 0x16, 0x14, 0x09}},
};

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dma_free, &woken);
    return woken == pdTRUE;
}

static void lcd_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, cmd, data, len));
}

// The CS of the SD card high (no card command), with no pin reservation (the SD driver
// reserves the pin when it mounts a card).
static void sd_cs_high(void)
{
    gpio_set_direction(PIN_SD_CS, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(PIN_SD_CS, GPIO_PULLUP_ONLY);
    gpio_set_level(PIN_SD_CS, 1);
}

static void lcd_init(void)
{
    spi_bus_config_t bus = {
            .mosi_io_num = PIN_LCD_MOSI,
            .miso_io_num = PIN_LCD_DC,  // the MISO of the SD card; for the LCD it is the DC output
            .sclk_io_num = PIN_LCD_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .data4_io_num = -1,
            .data5_io_num = -1,
            .data6_io_num = -1,
            .data7_io_num = -1,
            .max_transfer_sz = CHUNK_BYTES,
    };
    // the CS of the SD card high first: a card must not read the LCD data
    sd_cs_high();
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {
            .cs_gpio_num = PIN_LCD_CS,
            .dc_gpio_num = PIN_LCD_DC,
            .spi_mode = 0,
            .pclk_hz = LCD_PCLK_HZ,
            .trans_queue_depth = 10,
            .on_color_trans_done = on_color_done,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &s_io));

    s_dma_free = xSemaphoreCreateCounting(2, 2);
    for (int i = 0; i < 2; i++) {
        s_dma_buf[i] = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        assert(s_dma_buf[i]);
    }

    const lcd_init_t *list = s_panel_e ? INIT_E : INIT_C;
    size_t n = s_panel_e ? sizeof(INIT_E) / sizeof(INIT_E[0]) : sizeof(INIT_C) / sizeof(INIT_C[0]);
    for (size_t i = 0; i < n; i++) {
        lcd_cmd(list[i].cmd, list[i].data, list[i].len);
    }
    s_madctl = lcd_madctl(ORIENT_LAND_A);
    lcd_cmd(0x38, NULL, 0);               // idle mode off
    lcd_cmd(0x3A, (uint8_t[]){0x55}, 1);  // COLMOD: RGB565
    lcd_cmd(0x36, &s_madctl, 1);          // MADCTL
    lcd_cmd(0x21, NULL, 0);               // inversion on (the CoreS3 panels need it)
    lcd_cmd(0x11, NULL, 0);               // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    lcd_cmd(0x29, NULL, 0);  // display on
    vTaskDelay(pdMS_TO_TICKS(20));

    s_flush_q = xQueueCreate(1, sizeof(frame_t));
    s_flush_idle = xSemaphoreCreateBinary();
    xSemaphoreGive(s_flush_idle);
    xTaskCreatePinnedToCore(flush_task, "lcd", 3072, NULL, 7, NULL, 1);
}

static void lcd_send(const uint16_t *fb, int orient)
{
    bool land = orient == ORIENT_LAND_A || orient == ORIENT_LAND_B;
    int w = land ? LCD_H : LCD_W;  // the logical size
    int h = land ? LCD_W : LCD_H;
    uint8_t mad = lcd_madctl(orient);
    if (mad != s_madctl) {
        lcd_cmd(0x36, &mad, 1);  // the LCD turns the frame from here
        s_madctl = mad;
    }
    lcd_cmd(0x2A, (uint8_t[]){0, 0, (uint8_t)((w - 1) >> 8), (uint8_t)((w - 1) & 0xFF)}, 4);
    lcd_cmd(0x2B, (uint8_t[]){0, 0, (uint8_t)((h - 1) >> 8), (uint8_t)((h - 1) & 0xFF)}, 4);

    // The logical frame is one row after the other, the same order as the LCD writes
    // the window. Each chunk is a part of the frame buffer with no change.
    for (int c = 0; c < CHUNKS; c++) {
        xSemaphoreTake(s_dma_free, portMAX_DELAY);
        uint16_t *dst = s_dma_buf[c & 1];
        memcpy(dst, fb + c * CHUNK_PIXELS, CHUNK_BYTES);
        esp_lcd_panel_io_tx_color(s_io, c == 0 ? 0x2C : -1, dst, CHUNK_BYTES);  // RAMWR, then more data
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

// ---------------------------------------------------------------- keys

static void key_latch(void)
{
    s_key_until = tfn_now_ms32() + KEY_LATCH_MS;
}

// A short push of the PWR key gives a KEY_LATCH_MS pulse (the app then goes back).
// The AXP2101 reports the short push after the release. A push of 4 s stops the power
// (the AXP2101 does it). A touch below the LCD gives the same pulse.
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
                key_latch();
            }
        }
    }
    return (int32_t)(s_key_until - now) > 0;
}

// ---------------------------------------------------------------- touch

// The report from TD_STATUS: [0] = number of points, then 6 bytes for each point:
// event (2 bits) + x high (4 bits), x low, id (4 bits) + y high (4 bits), y low, weight, area.
int board_touch_points(int *x, int *y, int max)
{
    uint8_t buf[1 + 6 * 2];
    static uint32_t s_errors;
    esp_err_t err = reg_read(s_tp, TP_TD_STATUS, buf, sizeof(buf));
    if (err != ESP_OK) {
        if ((s_errors++ % 500) == 0) {
            ESP_LOGW(TAG, "touch read failed: %s (%lu times)", esp_err_to_name(err), (unsigned long)s_errors);
        }
        return 0;
    }
    int points = buf[0] & 0x0F;
    if (g_touch2_until_ms) {
        // (console "touch2 N"): one line for each read: the time, TD_STATUS, then x,y,id,event of the two point
        // slots, as the chip sends them (native px)
        int64_t now = esp_timer_get_time() / 1000;
        if (now > g_touch2_until_ms) {
            g_touch2_until_ms = 0;
            ESP_LOGI(TAG, "t2 end");
        } else {
            char line[120];
            int k = snprintf(line, sizeof(line), "t2 %lu 0x%02X", (unsigned long)now, buf[0]);
            for (int i = 0; i < 2; i++) {
                const uint8_t *p = buf + 1 + 6 * i;
                k += snprintf(line + k, sizeof(line) - (size_t)k, " %d,%d,%d,%d", ((p[0] & 0x0F) << 8) | p[1],
                              ((p[2] & 0x0F) << 8) | p[3], p[2] >> 4, p[0] >> 6);
            }
            ESP_LOGI(TAG, "%s", line);
        }
    }
    if (points > 2) {
        points = 0;  // 0x0F: no valid data
    }
    int n = 0;
    bool strip = false;
    for (int i = 0; i < points; i++) {
        const uint8_t *p = buf + 1 + 6 * i;
        if ((p[0] >> 6) == TP_EVENT_UP) {
            continue;
        }
        int tx = ((p[0] & 0x0F) << 8) | p[1];
        int ty = ((p[2] & 0x0F) << 8) | p[3];
        if (ty >= NATIVE_H) {
            strip = true;  // below the LCD: the BACK key, not a touch point
            continue;
        }
        if (tx >= NATIVE_W) {
            tx = NATIVE_W - 1;
        }
        if (n < max) {
            touch_native_to_phys(tx, ty, &x[n], &y[n]);
#if CONFIG_TFN_TOUCH_DEBUG
            ESP_LOGI(TAG, "touch %d/%d raw=(%d,%d) screen=(%d,%d)", i + 1, points, tx, ty, x[n], y[n]);
#endif
            n++;
        }
    }
    if (strip && !s_strip_down) {
        key_latch();
    }
    s_strip_down = strip;
    return n;
}

// ---------------------------------------------------------------- power

// ---------------------------------------------------------------- battery
// The AXP2101: 0x00 bit 3 = a battery is there; 0x01 bits 6..5 = 01 while it charges;
// 0x34/0x35 = the battery voltage (mV, 14 bits); 0xA4 = the fuel gauge (%).

int board_battery(bool *charging, int *mv)
{
    return axp2101_battery(s_axp, charging, mv);
}

int board_ext_power(void)
{
    return axp2101_vbus(s_axp);
}

// ---------------------------------------------------------------- resistance meter
// Port B: G9 (a high output) feeds the reference resistor; G8 = ADC1 channel 7 reads the joint.

#define PIN_OHM_DRIVE GPIO_NUM_9
#define OHM_ADC_CH    ADC_CHANNEL_7  // GPIO8

static adc_oneshot_unit_handle_t s_ohm_adc;
static adc_cali_handle_t s_ohm_cali;
static bool s_ohm_on;

bool board_ohm_start(void)
{
    if (s_ohm_on) {
        return true;
    }
    adc_oneshot_unit_init_cfg_t uc = {.unit_id = ADC_UNIT_1};
    adc_oneshot_chan_cfg_t cc = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    adc_cali_curve_fitting_config_t kc = {
            .unit_id = ADC_UNIT_1,
            .chan = OHM_ADC_CH,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_new_unit(&uc, &s_ohm_adc) != ESP_OK) {
        ESP_LOGW(TAG, "ohm meter: no ADC unit");
        return false;
    }
    if (adc_oneshot_config_channel(s_ohm_adc, OHM_ADC_CH, &cc) != ESP_OK ||
        adc_cali_create_scheme_curve_fitting(&kc, &s_ohm_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ohm meter: ADC setup failed");
        adc_oneshot_del_unit(s_ohm_adc);
        return false;
    }
    gpio_config_t io = {.pin_bit_mask = 1ULL << PIN_OHM_DRIVE, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&io);
    gpio_set_drive_capability(PIN_OHM_DRIVE, GPIO_DRIVE_CAP_3);
    gpio_set_level(PIN_OHM_DRIVE, 1);
    s_ohm_on = true;
    int mv = -1;
    board_ohm_read(&mv);
    ESP_LOGI(TAG, "ohm meter on: G9 high, G8 reads %d mV", mv);
    return true;
}

void board_ohm_stop(void)
{
    if (!s_ohm_on) {
        return;
    }
    gpio_reset_pin(PIN_OHM_DRIVE);  // an input again: no voltage on the port
    adc_cali_delete_scheme_curve_fitting(s_ohm_cali);
    adc_oneshot_del_unit(s_ohm_adc);
    s_ohm_on = false;
}

bool board_ohm_read(int *mv)
{
    if (!s_ohm_on) {
        return false;
    }
    int sum = 0, n = 0;
    for (int i = 0; i < 32; i++) {
        int raw, v;
        if (adc_oneshot_read(s_ohm_adc, OHM_ADC_CH, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(s_ohm_cali, raw, &v) == ESP_OK) {
            sum += v;
            n++;
        }
    }
    if (n == 0) {
        return false;
    }
    *mv = sum / n;
    return true;
}

void board_screen_sleep(bool sleep)
{
    if (sleep == s_scr_sleep) {
        return;
    }
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);  // no frame is on the way
    if (sleep) {
        lcd_cmd(0x28, NULL, 0);  // display off
        lcd_cmd(0x10, NULL, 0);  // sleep in
        amp_mute(s_spk_hold ? !s_spk_on : true);
    } else {
        lcd_cmd(0x11, NULL, 0);  // sleep out
        vTaskDelay(pdMS_TO_TICKS(120));
        lcd_cmd(0x29, NULL, 0);  // display on
        amp_mute(!s_spk_on);
    }
    s_scr_sleep = sleep;
    xSemaphoreGive(s_flush_idle);
}

// The camera power (webcam.c): ALDO3 and the reset line (low while the camera has no power: no current into it)
void board_camera_power(bool on)
{
    if (on) {
        s_ldo_en |= LDO_ALDO3;
        ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_axp, AXP_LDO_EN, s_ldo_en));
        vTaskDelay(pdMS_TO_TICKS(20));
        aw_set(1, AW1_CAM_RST, true);
        vTaskDelay(pdMS_TO_TICKS(20));
    } else {
        aw_set(1, AW1_CAM_RST, false);
        s_ldo_en &= (uint8_t)~LDO_ALDO3;
        ESP_ERROR_CHECK_WITHOUT_ABORT(reg_write(s_axp, AXP_LDO_EN, s_ldo_en));
    }
}

// 5 V out on the USB-C for a USB device (USB-C headphones, main/usb_phones.c): the boost (SY7088, on from the start)
// to BUS_OUT, BUS_OUT through the OTG switch U17 to VUSB. While it is on, the USB-C does not charge the battery.
// SAFETY: a cable from a PC or a charger also gives 5 V on VUSB; two sources on one line can damage the PC port or the
// board. So the 5 V goes on only when the AXP2101 sees no VBUS (0x00 bit 5) and a battery is there (bit 3: the board
// runs on it). (While the 5 V is on, U18 is off: the board cannot see a cable that comes later.)
bool board_usb_power_out(bool on)
{
    if (on) {
        uint8_t st = 0;
        if (reg_read(s_axp, 0x00, &st, 1) != ESP_OK) {
            return false;
        }
        if (st & 0x20) {
            ESP_LOGW(TAG, "usb power out: a cable gives 5 V (VBUS): no");
            return false;
        }
        if (!(st & 0x08)) {
            ESP_LOGW(TAG, "usb power out: no battery: no");
            return false;
        }
    }
    aw_set(0, AW0_BUS_OUT_EN, on);
    aw_set(0, AW0_USB_OTG_EN, on);
    return true;
}

// the AW9523B INTN line (P1_2, the touch INT, changed); a read of the inputs releases it
int board_touch_int_gpio(void)
{
    return PIN_SYS_INT;
}
void board_touch_int_ack(void)
{
    uint8_t v;
    reg_read(s_aw, AW_IN0, &v, 1);
    reg_read(s_aw, AW_IN1, &v, 1);
}

void board_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    board_backlight(0);
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);  // no more frames after this
    lcd_cmd(0x28, NULL, 0);                       // display off
    lcd_cmd(0x10, NULL, 0);                       // sleep in
    aw_set(0, AW0_AMP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t v;
    if (reg_read(s_axp, AXP_PMU_CFG, &v, 1) != ESP_OK) {
        v = 0x30;
    }
    reg_write(s_axp, AXP_PMU_CFG, v | 0x01);  // the AXP2101 stops all the power
    vTaskDelay(pdMS_TO_TICKS(500));

    // Still here: the AXP2101 did not stop the power. Sleep until a touch on the LCD.
    // The touch INT goes through AW9523B P1_2 to INTN (GPIO21).
    ESP_LOGW(TAG, "AXP2101 power off failed: deep sleep, a touch wakes the board");
    reg_write(s_aw, AW_INT0, 0xFF);                  // no interrupt from port 0
    reg_write(s_aw, AW_INT1, (uint8_t)~AW1_TP_INT);  // only the touch INT
    reg_read(s_aw, AW_IN0, &v, 1);                   // release INTN
    reg_read(s_aw, AW_IN1, &v, 1);
    rtc_gpio_pullup_en(PIN_SYS_INT);
    rtc_gpio_pulldown_dis(PIN_SYS_INT);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);  // keep the pull-up on
    esp_sleep_enable_ext1_wakeup_io(1ULL << PIN_SYS_INT, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

// ---------------------------------------------------------------- the microSD card
// The slot shares the SPI bus with the LCD (SCK 36, MOSI 37), CS = GPIO4, and its MISO is
// GPIO35, the DC output of the LCD. Each card command goes through sd_transaction(): it waits
// until the LCD task does not send a frame and the two DMA chunks are done, holds the LCD for
// that time, and turns GPIO35 to an input (the card drives it), then back to the DC output.

static esp_err_t sd_transaction(int slot, sdmmc_command_t *cmd)
{
    xSemaphoreTake(s_flush_idle, portMAX_DELAY);
    xSemaphoreTake(s_dma_free, portMAX_DELAY);
    xSemaphoreTake(s_dma_free, portMAX_DELAY);
    gpio_set_direction(PIN_LCD_DC, GPIO_MODE_INPUT);
    esp_err_t err = sdspi_host_do_transaction(slot, cmd);
    gpio_set_direction(PIN_LCD_DC, GPIO_MODE_OUTPUT);
    xSemaphoreGive(s_dma_free);
    xSemaphoreGive(s_dma_free);
    xSemaphoreGive(s_flush_idle);
    return err;
}

esp_err_t board_sd_card_mount(const esp_vfs_fat_mount_config_t *mc, sdmmc_card_t **card)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = LCD_HOST;
    host.max_freq_khz = 20000;
    host.do_transaction = sd_transaction;
    sdspi_device_config_t dev = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev.host_id = LCD_HOST;
    dev.gpio_cs = PIN_SD_CS;
    sd_dma_set(&host);
    esp_err_t err = esp_vfs_fat_sdspi_mount(BOARD_SD_ROOT, &host, &dev, mc, card);
    sd_dma_result(err == ESP_OK);
    return err;
}

// (the card shares the bus with the LCD: its CS line stays high when the card is not in use)
void board_sd_card_released(void)
{
    sd_cs_high();
}

// ---------------------------------------------------------------- init

void board_init(void)
{
    board_sd_init();
    i2c_init();
    axp_init();  // rails on, backlight off
    aw_init();   // touch and amplifier out of reset

    // Reset the LCD and the touch controller together.
    aw_set(1, AW1_LCD_RST, false);
    aw_set(0, AW0_TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(20));
    aw_set(1, AW1_LCD_RST, true);
    aw_set(0, AW0_TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(300));  // the LCD needs 120 ms, the FT6336U about 300 ms

    s_panel_e = detect_panel_e();
    lcd_init();
}

#endif  // CONFIG_TFN_TARGET_CORES3SE
