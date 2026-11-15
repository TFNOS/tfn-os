// The AXP2101 power chip: the parts that are the same on the CoreS3, the AMOLED-1.8 and the AMOLED-1.75C
// (pmic_axp2101.h).
#include "pmic_axp2101.h"
#include "board_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

bool axp2101_wait_id(i2c_master_dev_handle_t axp)
{
    uint8_t v = 0;
    for (int i = 0; i < 5; i++) {
        if (reg_read(axp, AXP_CHIP_ID, &v, 1) == ESP_OK && v == 0x4A) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (v != 0x4A) {
        ESP_LOGE(TAG, "AXP2101 does not answer (ID 0x%02X)", v);
    }
    return v == 0x4A;
}

void axp2101_init_basic(i2c_master_dev_handle_t axp, uint8_t *pmu_on, uint8_t *pmu_off)
{
    axp2101_wait_id(axp);
    // (the sources of the last turn-on and turn-off, before any change: the console "pmu" shows them again)
    reg_read(axp, 0x20, pmu_on, 1);
    reg_read(axp, 0x21, pmu_off, 1);
    reg_write(axp, AXP_PKEY_TIME, 0x00);  // long press IRQ after 1 s, power off after 4 s
    reg_write(axp, AXP_ADC_EN, 0x1F);     // the battery, TS, VBUS, VSYS voltages and the die temperature
    reg_write(axp, AXP_IRQ_ST1, 0xFF);    // remove old key events
}

static int s_vbus = -1;  // 0x00 bit 5: VBUS good (a charger or a PC); -1 before the first read

int axp2101_vbus(i2c_master_dev_handle_t axp)
{
    axp2101_battery(axp, NULL, NULL);
    return s_vbus;
}

int axp2101_battery(i2c_master_dev_handle_t axp, bool *charging, int *mv)
{
    static int64_t s_at;
    static int s_pct = -1, s_mv;
    static bool s_chg;
    int64_t now = esp_timer_get_time();
    if (s_at == 0 || now - s_at > 2000000) {
        s_at = now;
        uint8_t st[2], v[2], pct;
        if (reg_read(axp, 0x00, st, 2) == ESP_OK && reg_read(axp, 0x34, v, 2) == ESP_OK &&
            reg_read(axp, 0xA4, &pct, 1) == ESP_OK) {
            bool present = st[0] & 0x08;
            s_vbus = (st[0] & 0x20) ? 1 : 0;
            s_chg = ((st[1] >> 5) & 0x03) == 0x01;
            s_mv = ((v[0] & 0x3F) << 8) | v[1];
            s_pct = present ? (pct > 100 ? 100 : pct) : -1;
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

int axp2101_adc14(i2c_master_dev_handle_t axp, uint8_t reg)
{
    uint8_t b[2] = {0, 0};
    if (reg_read(axp, reg, b, 2) != ESP_OK) {
        return -1;
    }
    return ((b[0] & 0x3F) << 8) | b[1];
}
