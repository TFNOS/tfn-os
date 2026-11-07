// THE NATIVE RADIO (rf_native.h): the receiver of the ESP32-S3 as a source of raw I/Q samples.
//
// The receiver setup (the PLL tune, the forced gain, the PBUS hold of the gain stages, the filter bank) comes from
// eSpDR (github.com/h0m3us3r/eSpDR, esp32s3/src/radio.c, Zero-Clause BSD). The facts of the single capture (the
// control word, the bank of the snapshot and its owner register) come from the documents of ESPARGOS ESP-SDR
// (docs/how-it-works.md). No code of ESP-SDR (GPL-3.0) is in this file.
//
// The ESP-IDF PHY library does the power-up calibration (esp_phy_enable); this file takes the receiver after it.
// No transmit path is turned on.
#include "rf_native.h"
#include "tfn_extras.h"

#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "settings.h"

// The real receiver: the ESP32-S3 with the extra CONFIG_TFN_EXTRA_RF_NATIVE (else the "no native radio" part below).
#define RFN_REAL (CONFIG_IDF_TARGET_ESP32S3 && CONFIG_TFN_EXTRA_RF_NATIVE)

#if RFN_REAL
#include "esp_heap_caps_init.h"
#include "heap_memory_layout.h"
#include "esp_phy_init.h"
#include "esp_private/periph_ctrl.h"
#include "esp_private/regi2c_ctrl.h"
#include "esp_rom_sys.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "soc/syscon_reg.h"
#endif

static const char *TAG = "rf";

#define RF_BANK_ADDR  0x3FCD0000u
#define RF_BANK_BYTES 0x10000u

// The request of a radio start: it stays in the RTC memory over a software restart (not over a power off)
#define RF_BOOT_MAGIC 0x52464E31u  // "RFN1"
typedef struct {
    uint32_t magic, check;
    char app[28];
} rf_boot_t;
static RTC_NOINIT_ATTR rf_boot_t s_req;
static bool s_radio;
static char s_app[28];

#if RFN_REAL

// The block of the dump engine is not in the heap at the start (rfn_early gives it back in a normal start)
SOC_RESERVE_MEMORY_REGION(RF_BANK_ADDR, RF_BANK_ADDR + RF_BANK_BYTES, rf_dump);

#define REG(a) (*(volatile uint32_t *)(a))

// The dump engine (the names are the names of eSpDR and ESP-SDR; Espressif does not document them)
#define DUMP_CTRL_REG        0x60033D5Cu  // bits 0..14 the pairs, 16 the 16 Msps clock, 18 done, 19 trigger, 31 on
#define DUMP_CONFIG_REG      0x60033D90u  // the byte selectors of the dump word
#define DUMP_BANK_SELECT_REG 0x600C101Cu  // bits 0..3: the bank 0x3FCB0000 + n * 64 KB to the engine
#define DUMP_CTRL_RUN        0x80000000u
#define DUMP_CTRL_16MSPS     0x00010000u
#define DUMP_CTRL_DONE       0x00040000u
#define DUMP_CTRL_TRIGGER    0x00080000u
#define DUMP_CONFIG_IQ       0x000C2040u  // bytes 0, 1, 2, 3: the full word (I bits 0..9, Q bits 10..19)
#define DUMP_BANK_BIT        4u           // the bank 2 = 0x3FCD0000

// The baseband, the AGC and the front end
#define BB_ENABLE_REG      0x6002600Cu
#define AGC_CTRL_REG       0x6001C01Cu
#define AGC_GAIN_FORCE_REG 0x6001C02Cu
#define AGC_DISABLE_REG    0x6001C034u
#define AGC_RX_FORCE_REG   0x6001C080u
#define IQ_CORRECTION_REG  0x6000607Cu
#define FE_WIDTH_REG       0x60006100u
#define PBUS_CTRL_REG      0x60006104u
#define PBUS_MODE_REG      0x6000610Cu
#define PBUS_STATUS_REG    0x60006110u
#define PBUS_BB_GAIN_REG   0x60006118u
#define PBUS_RF_GAIN_REG   0x6000611Cu
#define RFPLL_OWNER_REG    0x6000E0C4u

// The analog (regi2c) blocks
#define I2C_RFPLL     0x62
#define I2C_SDM       0x63
#define I2C_CKGEN     0x65
#define I2C_BB_FILTER 0x67
#define CKGEN_5_6_BIT 0x10

#define PLL_MIN_HZ          2210000000u
#define PLL_MAX_HZ          2790000000u
#define PBUS_TIMEOUT_CYCLES 24000u

static bool s_open;
static rfn_cfg_t s_cfg;
static struct {
    bool owned;
    uint32_t pbus_ctrl, pbus_mode, iq_register;
    uint32_t lo_hz;
    unsigned pll_cap;
} s_rx;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t an_read(uint8_t block, uint8_t reg)
{
    return regi2c_ctrl_read_reg(block, 1, reg);
}
static void an_write(uint8_t block, uint8_t reg, uint8_t v)
{
    regi2c_ctrl_write_reg(block, 1, reg, v);
}
static void an_bits(uint8_t block, uint8_t reg, uint8_t mask, uint8_t v)
{
    uint8_t old = an_read(block, reg);
    an_write(block, reg, (uint8_t)((old & ~mask) | (v & mask)));
}

static void pll_cap(unsigned cap)
{
    an_write(I2C_RFPLL, 1, (uint8_t)cap);
    an_bits(I2C_RFPLL, 2, 0x10, (uint8_t)((cap >> 8) << 4));
}

static void pll_manual(bool on)
{
    an_bits(I2C_RFPLL, 11, 0x40, on ? 0x40 : 0);
}

// THE FAST TUNE: the capacitor code of each MHz that locked once. A known MHz: the new word and the known code, a
// check of the lock (about 50 us, not the scan of 26 ms). A sweep over the band tunes to the same centres again and
// again.
#define CAP_CACHE  96
#define CAP_NOLOCK 0xFFFFu  // (a MHz that did not lock: the code of the cache)
static struct {
    uint16_t mhz, cap;
} s_capc[CAP_CACHE];
static int s_capn, s_capw;

static void cap_keep(uint32_t hz, unsigned cap)
{
    uint16_t mhz = (uint16_t)((hz + 500000u) / 1000000u);
    for (int i = 0; i < s_capn; i++) {
        if (s_capc[i].mhz == mhz) {
            s_capc[i].cap = (uint16_t)cap;
            return;
        }
    }
    s_capc[s_capw].mhz = mhz;
    s_capc[s_capw].cap = (uint16_t)cap;
    s_capw = (s_capw + 1) % CAP_CACHE;
    if (s_capn < CAP_CACHE) {
        s_capn++;
    }
}

// The sigma-delta word, the calibration of the PLL, then a scan of the 512 VCO capacitor codes: the capacitor
// stays at the middle of the longest run of codes that lock (eSpDR tune_pll; only the normal conversion here).
static bool tune_pll(uint32_t hz)
{
    if (hz < PLL_MIN_HZ || hz > PLL_MAX_HZ) {
        return false;
    }
    const uint32_t step = 30000000u;  // LO = 30 MHz * (32 + W / 65536)
    uint64_t scaled = ((uint64_t)hz * 65536u + step / 2u) / step;
    uint32_t word = (uint32_t)(scaled - 32u * 65536u);
    s_rx.lo_hz = (uint32_t)(((uint64_t)(word + 32u * 65536u) * step + 32768u) / 65536u);

    an_bits(I2C_CKGEN, 0, CKGEN_5_6_BIT, 0);
    REG(RFPLL_OWNER_REG) |= 1u << 25;
    pll_manual(false);
    an_write(I2C_SDM, 0, 0x07);
    an_write(I2C_SDM, 3, (uint8_t)(word >> 16));
    an_write(I2C_SDM, 4, (uint8_t)(word >> 8));
    an_write(I2C_SDM, 5, (uint8_t)word);
    an_write(I2C_SDM, 0, 0x17);

    an_bits(I2C_RFPLL, 0, 0x40, 0x40);
    an_bits(I2C_RFPLL, 0, 0x20, 0x00);
    an_bits(I2C_RFPLL, 0, 0x20, 0x20);
    an_bits(I2C_RFPLL, 0, 0x40, 0x00);
    bool done = false;
    for (int i = 0; i < 100 && !done; i++) {
        esp_rom_delay_us(20);
        done = an_read(I2C_RFPLL, 7) & 2;
    }
    if (!done) {
        return false;
    }
    esp_rom_delay_us(5);

    uint8_t lo = an_read(I2C_RFPLL, 1), hi = an_read(I2C_RFPLL, 2), mode = an_read(I2C_RFPLL, 11);
    pll_manual(true);
    unsigned run0 = 0, run = 0, best0 = 0, best = 0;
    for (unsigned c = 0; c < 512; c++) {
        pll_cap(c);
        esp_rom_delay_us(20);
        if (((an_read(I2C_RFPLL, 12) >> 2) & 3) != 0) {
            run = 0;
            continue;
        }
        if (run++ == 0) {
            run0 = c;
        }
        if (run > best) {
            best0 = run0;
            best = run;
        }
    }
    an_write(I2C_RFPLL, 1, lo);
    an_bits(I2C_RFPLL, 2, 0x10, hi);
    an_bits(I2C_RFPLL, 11, 0x40, mode);
    if (!best) {
        return false;
    }
    s_rx.pll_cap = best0 + (best - 1) / 2;
    pll_cap(s_rx.pll_cap);
    pll_manual(true);
    cap_keep(hz, s_rx.pll_cap);
    return true;
}

static bool tune_fast(uint32_t hz)
{
    if (hz < PLL_MIN_HZ || hz > PLL_MAX_HZ) {
        return false;
    }
    uint16_t mhz = (uint16_t)((hz + 500000u) / 1000000u);
    int k = -1;
    for (int i = 0; i < s_capn; i++) {
        if (s_capc[i].mhz == mhz) {
            k = i;
        }
    }
    if (k < 0 || s_capc[k].cap == CAP_NOLOCK) {
        return false;
    }
    const uint32_t step = 30000000u;
    uint64_t scaled = ((uint64_t)hz * 65536u + step / 2u) / step;
    uint32_t word = (uint32_t)(scaled - 32u * 65536u);
    pll_manual(true);
    pll_cap(s_capc[k].cap);
    an_write(I2C_SDM, 0, 0x07);
    an_write(I2C_SDM, 3, (uint8_t)(word >> 16));
    an_write(I2C_SDM, 4, (uint8_t)(word >> 8));
    an_write(I2C_SDM, 5, (uint8_t)word);
    an_write(I2C_SDM, 0, 0x17);
    esp_rom_delay_us(40);
    if (((an_read(I2C_RFPLL, 12) >> 2) & 3) != 0) {
        return false;
    }
    s_rx.lo_hz = (uint32_t)(((uint64_t)(word + 32u * 65536u) * step + 32768u) / 65536u);
    s_rx.pll_cap = s_capc[k].cap;
    return true;
}

static bool pbus_write(unsigned block, unsigned index, unsigned value)
{
    uint32_t f = ((value & 511u) << 6) | ((block & 15u) << 2) | ((index & 3u) << 15);
    REG(PBUS_CTRL_REG) = (REG(PBUS_CTRL_REG) & 0xFFFE0001u) | (f & 0x1FFFCu) | 2u;
    uint32_t t0 = esp_cpu_get_cycle_count();
    while (REG(PBUS_STATUS_REG) & 0x80000000u) {
        if (esp_cpu_get_cycle_count() - t0 > PBUS_TIMEOUT_CYCLES) {
            REG(PBUS_CTRL_REG) &= ~2u;
            return false;
        }
    }
    REG(PBUS_CTRL_REG) &= ~2u;
    return true;
}

static void set_width(unsigned mhz)
{
    bool wide = mhz == 40;
    REG(FE_WIDTH_REG) = (REG(FE_WIDTH_REG) & ~0x003F0000u) | (wide ? 0x00120000u : 0);
    REG(BB_ENABLE_REG) = (REG(BB_ENABLE_REG) & ~0xCu) | (wide ? 0x4u : 0);
}

static void park(void)
{
    REG(DUMP_CTRL_REG) &= ~DUMP_CTRL_RUN;
    REG(DUMP_BANK_SELECT_REG) &= ~15u;
    REG(AGC_RX_FORCE_REG) &= ~0xC1u;
    REG(PBUS_STATUS_REG) &= ~0xCF00u;
    REG(BB_ENABLE_REG) &= ~2u;
}

// The gain stages back to the hardware
static bool release(void)
{
    park();
    if (!s_rx.owned) {
        return true;
    }
    bool ok = pbus_write(0, 1, 0) && pbus_write(1, 1, 0) && pbus_write(1, 2, 0);
    REG(PBUS_CTRL_REG) = s_rx.pbus_ctrl;
    REG(PBUS_MODE_REG) = s_rx.pbus_mode;
    REG(IQ_CORRECTION_REG) = s_rx.iq_register;
    s_rx.owned = false;
    return ok;
}

// The receive path (eSpDR configure_receiver): the width before the enable edge of the baseband, no AGC, the
// forced gain, the RC filter, the gain stages held through the PBUS, both transmit groups off, the dump format.
static bool configure(void)
{
    park();
    set_width(s_cfg.width);
    REG(BB_ENABLE_REG) |= 0x10000000u;
    REG(BB_ENABLE_REG) &= ~2u;
    esp_rom_delay_us(1);
    REG(BB_ENABLE_REG) |= 2u;
    REG(AGC_CTRL_REG) = (REG(AGC_CTRL_REG) & 0xFF00FFFFu) | 0x007F0000u;
    REG(AGC_DISABLE_REG) |= 0x80u;
    REG(AGC_RX_FORCE_REG) |= 1u;
    REG(AGC_GAIN_FORCE_REG) = (REG(AGC_GAIN_FORCE_REG) & 0x007FFFFFu) | ((uint32_t)s_cfg.gain << 24) | 0x00800000u;
    REG(PBUS_STATUS_REG) |= 0xC000u;

    set_width(s_cfg.width);
    unsigned f = s_cfg.width == 40 ? 6 : 4;
    an_write(I2C_BB_FILTER, f, s_cfg.filter & 63);
    an_write(I2C_BB_FILTER, f + 1, s_cfg.filter & 63);

    esp_rom_delay_us(100);
    unsigned bb = (REG(PBUS_BB_GAIN_REG) >> 9) & 511;
    unsigned rf = (REG(PBUS_RF_GAIN_REG) >> 18) & 511;
    s_rx.pbus_ctrl = REG(PBUS_CTRL_REG);
    s_rx.pbus_mode = REG(PBUS_MODE_REG);
    s_rx.iq_register = REG(IQ_CORRECTION_REG);
    REG(PBUS_MODE_REG) &= ~0x08000000u;
    REG(PBUS_CTRL_REG) |= 1u;
    s_rx.owned = true;
    REG(BB_ENABLE_REG) &= ~2u;

    bool ok = pbus_write(4, 1, 0) && pbus_write(5, 1, 0) && pbus_write(0, 1, 0x184) && pbus_write(1, 1, 0x189) &&
              pbus_write(1, 2, rf) && pbus_write(0, 1, bb);
    if (!ok) {
        return false;
    }
    REG(DUMP_CONFIG_REG) = DUMP_CONFIG_IQ;
    esp_rom_delay_us(100);
    return true;
}

static int apply(bool retune)
{
    if (!release()) {
        return RFN_E_PBUS;
    }
    if (retune && !tune_pll(s_cfg.lo_hz)) {
        return RFN_E_PLL;
    }
    if (!configure()) {
        return RFN_E_PBUS;
    }
    an_bits(I2C_CKGEN, 0, CKGEN_5_6_BIT, 0);  // the normal conversion
    esp_rom_delay_us(3000);
    return RFN_OK;
}

static bool cfg_ok(const rfn_cfg_t *c)
{
    return c && c->lo_hz >= PLL_MIN_HZ && c->lo_hz <= PLL_MAX_HZ && c->rate <= RFN_RATE_16M &&
           (c->width == 20 || c->width == 40) && c->gain <= 127 && c->filter <= 63;
}

bool rfn_available(void)
{
    return true;
}

int rfn_open(const rfn_cfg_t *cfg)
{
    if (!s_radio) {
        return RFN_E_NOTRADIO;
    }
    if (!cfg_ok(cfg)) {
        return RFN_E_ARG;
    }
    if (!s_open) {
        esp_wifi_bt_power_domain_on();
        wifi_bt_common_module_enable();
        wifi_module_enable();
        esp_phy_enable(PHY_MODEM_WIFI);
        REG(SYSCON_WIFI_CLK_EN_REG) |= 1u << 6;  // the WiFi MAC clock: the dump engine needs it
        memset(&s_rx, 0, sizeof(s_rx));
        s_open = true;
    }
    s_cfg = *cfg;
    int e = apply(true);
    ESP_LOGI(TAG, "open: LO %lu Hz, cap %u, %s", (unsigned long)s_rx.lo_hz, s_rx.pll_cap, rfn_error(e));
    return e;
}

int rfn_set(const rfn_cfg_t *cfg, uint32_t *lo_actual)
{
    if (!s_open) {
        return RFN_E_CLOSED;
    }
    if (!cfg_ok(cfg)) {
        return RFN_E_ARG;
    }
    rfn_cfg_t old = s_cfg;
    bool retune = cfg->lo_hz != s_cfg.lo_hz;
    s_cfg = *cfg;
    int e = apply(retune);
    if (e != RFN_OK) {
        s_cfg = old;
        apply(true);  // back to the last good setup
    }
    if (lo_actual) {
        *lo_actual = s_rx.lo_hz;
    }
    return e;
}

int rfn_tune(uint32_t lo_hz, uint32_t *lo_actual)
{
    if (!s_open) {
        return RFN_E_CLOSED;
    }
    if (lo_hz < PLL_MIN_HZ || lo_hz > PLL_MAX_HZ) {
        return RFN_E_ARG;
    }
    // a MHz that did not lock before: no new scan of 26 ms (a sweep asks for it in each frame)
    const uint16_t mhz = (uint16_t)((lo_hz + 500000u) / 1000000u);
    for (int i = 0; i < s_capn; i++) {
        if (s_capc[i].mhz == mhz && s_capc[i].cap == CAP_NOLOCK) {
            return RFN_E_PLL;
        }
    }
    s_cfg.lo_hz = lo_hz;
    int e = RFN_OK;
    if (!tune_fast(lo_hz)) {
        e = tune_pll(lo_hz) ? RFN_OK : RFN_E_PLL;
    }
    if (e == RFN_E_PLL) {
        cap_keep(lo_hz, CAP_NOLOCK);
    }
    if (lo_actual) {
        *lo_actual = s_rx.lo_hz;
    }
    return e;
}

int rfn_capture(int16_t *iq, int n, uint8_t *gain_idx)
{
    if (!s_open) {
        return RFN_E_CLOSED;
    }
    if (!iq || n < 1 || n > RFN_MAX_PAIRS) {
        return RFN_E_ARG;
    }
    volatile uint32_t *bank = (volatile uint32_t *)RF_BANK_ADDR;
    const uint32_t MARK = 0xA5A0055Au;
    bank[n - 1] = MARK;  // (the last word: a capture that stopped early keeps it)
    const uint32_t ctrl = DUMP_CTRL_RUN | (uint32_t)n | (s_cfg.rate == RFN_RATE_16M ? DUMP_CTRL_16MSPS : 0);
    // 16380 pairs at 16 Msps = 1 ms; the limit = 4 ms of cycles at 240 MHz
    const uint32_t limit = 4u * 240000u;
    bool done = false;
    // While the engine owns the bank, the CPU cannot read or write any of it: no interrupt on this core; the other
    // core has nothing in the block (it is not in the heap in a radio start).
    portENTER_CRITICAL(&s_mux);
    REG(DUMP_BANK_SELECT_REG) = (REG(DUMP_BANK_SELECT_REG) & ~15u) | DUMP_BANK_BIT;
    REG(DUMP_CONFIG_REG) = DUMP_CONFIG_IQ;
    REG(DUMP_CTRL_REG) = ctrl;
    REG(DUMP_CTRL_REG) = ctrl | DUMP_CTRL_TRIGGER;
    REG(DUMP_CTRL_REG) = ctrl;
    uint32_t t0 = esp_cpu_get_cycle_count();
    while (!(done = (REG(DUMP_CTRL_REG) & DUMP_CTRL_DONE) != 0)) {
        if (esp_cpu_get_cycle_count() - t0 > limit) {
            break;
        }
    }
    REG(DUMP_CTRL_REG) = 0;
    REG(DUMP_BANK_SELECT_REG) &= ~15u;
    portEXIT_CRITICAL(&s_mux);
    if (!done || bank[n - 1] == MARK) {
        return RFN_E_TIMEOUT;
    }
    if (gain_idx) {
        *gain_idx = (uint8_t)(bank[0] >> 20);
    }
    for (int k = 0; k < n; k++) {
        uint32_t w = bank[k];
        int i = (int)((w & 1023u) ^ 512u) - 512;
        int q = (int)(((w >> 10) & 1023u) ^ 512u) - 512;
        iq[2 * k] = (int16_t)i;
        iq[2 * k + 1] = (int16_t)-q;  // I + jQ of the engine = LO - RF: the conjugate = RF - LO
    }
    return n;
}

void rfn_close(void)
{
    if (!s_open) {
        return;
    }
    release();
    esp_phy_disable(PHY_MODEM_WIFI);
    s_open = false;
    ESP_LOGI(TAG, "closed");
}

void rfn_early(void)
{
    s_radio = s_req.magic == RF_BOOT_MAGIC && s_req.check == ~RF_BOOT_MAGIC;
    if (s_radio) {
        memcpy(s_app, s_req.app, sizeof(s_app));
        s_app[sizeof(s_app) - 1] = 0;
    }
    s_req.magic = 0;  // (used once: the next restart is a normal start)
    if (!s_radio) {
        esp_err_t e = heap_caps_add_region(RF_BANK_ADDR, RF_BANK_ADDR + RF_BANK_BYTES);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "the radio block to the heap: %s", esp_err_to_name(e));
        }
    } else {
        ESP_LOGI(TAG, "RADIO START: the block 0x%08X stays free, no WiFi, the app \"%s\"", (unsigned)RF_BANK_ADDR,
                 s_app);
    }
}

#else  // no native radio (the Tab5: an ESP32-P4; or the extra is off)

bool rfn_available(void)
{
    return false;
}
int rfn_open(const rfn_cfg_t *cfg)
{
    (void)cfg;
    return RFN_E_NOCHIP;
}
int rfn_set(const rfn_cfg_t *cfg, uint32_t *lo_actual)
{
    (void)cfg;
    (void)lo_actual;
    return RFN_E_NOCHIP;
}
int rfn_tune(uint32_t lo_hz, uint32_t *lo_actual)
{
    (void)lo_hz;
    (void)lo_actual;
    return RFN_E_NOCHIP;
}
int rfn_capture(int16_t *iq, int n, uint8_t *gain_idx)
{
    (void)iq;
    (void)n;
    (void)gain_idx;
    return RFN_E_NOCHIP;
}
void rfn_close(void)
{
}
void rfn_early(void)
{
    s_req.magic = 0;
}

#endif

bool rfn_radio_boot(void)
{
    return s_radio;
}
const char *rfn_boot_app(void)
{
    return s_app;
}

void rfn_restart(bool radio, const char *app)
{
    memset(&s_req, 0, sizeof(s_req));
    if (radio && rfn_available()) {
        if (app) {
            strncpy(s_req.app, app, sizeof(s_req.app) - 1);
        }
        s_req.check = ~RF_BOOT_MAGIC;
        s_req.magic = RF_BOOT_MAGIC;
    }
    ESP_LOGI(TAG, "restart: %s", radio ? "a radio start" : "a normal start");
    settings_save();  // (the volume and the brightness of the user)
    esp_restart();
}

uint32_t rfn_rate_hz(int rate)
{
    return rate == RFN_RATE_16M ? 16000000u : 80000000u;
}

const char *rfn_error(int e)
{
    switch (e) {
    case RFN_OK:
        return "ok";
    case RFN_E_NOCHIP:
        return "no native radio";
    case RFN_E_NOTRADIO:
        return "not a radio start";
    case RFN_E_PLL:
        return "the PLL did not lock";
    case RFN_E_PBUS:
        return "the receiver setup failed";
    case RFN_E_TIMEOUT:
        return "the capture did not end";
    case RFN_E_ARG:
        return "a bad value";
    case RFN_E_CLOSED:
        return "the radio is not open";
    default:
        return "?";
    }
}
