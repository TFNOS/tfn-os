// RFSRC: the WiFi receiver of the ESP32-S3 as an IQ source (the native radio of TFN OS, main/rf_native.h).
// The radio needs a RADIO START of the board (no WiFi): state() tells it, claim() restarts the board into it.
#include "rfsrc.h"

#ifdef ESP_PLATFORM
#include <stdlib.h>
#include "rf_native.h"

static const rfsrc_caps_t CAPS = {
        .id = "esp32",
        .name = {"ESP32-S3 RADIO", "הרדיו של ESP32-S3"},
        .kind = RFSRC_IQ,
        .flags = RFSRC_F_NOWIFI,
        .nbands = 1,
        .band = {{2215000000LL, 2775000000LL}},  // (the PLL range of the chip is about 2210..2780 MHz: 5 MHz margin)
        .nrates = 2,
        // (measured on a board: the noise is flat over +-20 MHz at 80 Msps (the 40 MHz filter bank) and over
        // +-5 MHz at 16 Msps (the 20 MHz bank))
        .rate_hz = {80000000u, 16000000u},
        .usable_hz = {20000000u, 5000000u},
        .dc_hz = 60000,
        .max_pairs = RFN_MAX_PAIRS,
        // (the gain table of the chip: about 1 dB a step from 35 to 76 (eSpDR); 70 clipped near an access point)
        .ngains = 3,
        .gain_def = 1,
        .gain_name = {{"LOW", "נמוכה"}, {"NORMAL", "רגילה"}, {"HIGH", "גבוהה"}},
        .gain_val = {30, 48, 62},
        .gain_db = {-18, 0, 14},
        .tune_us = 400,
        .full_scale_db = 0,
};

typedef struct {
    bool open;
    rfn_cfg_t cfg;
} ctx_t;
static ctx_t s_ctx;

static int st_state(void *c)
{
    (void)c;
    if (!rfn_available()) {
        return RFSRC_ABSENT;
    }
    return rfn_radio_boot() ? RFSRC_READY : RFSRC_NEEDS_START;
}

static void st_claim(void *c, const char *app)
{
    (void)c;
    rfn_restart(true, app);
}

static void st_unclaim(void *c)
{
    (void)c;
    if (rfn_radio_boot()) {
        rfn_restart(false, NULL);
    }
}

static void make_cfg(const rfsrc_set_t *s, rfn_cfg_t *cfg)
{
    cfg->lo_hz = (uint32_t)s->center_hz;
    cfg->rate = s->rate == 1 ? RFN_RATE_16M : RFN_RATE_80M;
    cfg->width = s->rate == 1 ? 20 : 40;
    cfg->gain = CAPS.gain_val[s->gain < CAPS.ngains ? s->gain : CAPS.gain_def];
    cfg->filter = 0;
}

static int st_open(void *c, const rfsrc_set_t *s)
{
    ctx_t *x = c;
    make_cfg(s, &x->cfg);
    int e = rfn_open(&x->cfg);
    x->open = e == RFN_OK;
    return e;
}

static int st_tune(void *c, const rfsrc_set_t *s, int64_t *center)
{
    ctx_t *x = c;
    rfn_cfg_t n;
    make_cfg(s, &n);
    uint32_t lo = 0;
    int e;
    if (n.rate == x->cfg.rate && n.gain == x->cfg.gain && n.width == x->cfg.width) {
        e = rfn_tune(n.lo_hz, &lo);
    } else {
        e = rfn_set(&n, &lo);
    }
    if (e == RFN_OK) {
        x->cfg = n;
    }
    if (center) {
        *center = lo ? (int64_t)lo : s->center_hz;
    }
    return e;
}

static int st_read(void *c, int16_t *iq, int n, rfsrc_meta_t *m)
{
    ctx_t *x = c;
    int r = rfn_capture(iq, n, NULL);
    if (r < 0) {
        return r;
    }
    if (m) {
        m->center_hz = x->cfg.lo_hz;
        m->rate_hz = rfn_rate_hz(x->cfg.rate);
        m->overload = false;
        for (int k = 0; k < 2 * r; k += 8) {  // (a quick look: each 4th pair)
            if (iq[k] >= 510 || iq[k] <= -511 || iq[k + 1] >= 510 || iq[k + 1] <= -511) {
                m->overload = true;
                break;
            }
        }
    }
    return r;
}

static int st_sweep(void *c, int64_t f0, uint32_t step, int n, float *db)
{
    (void)c;
    (void)f0;
    (void)step;
    (void)n;
    (void)db;
    return -1;
}

static void st_close(void *c)
{
    ctx_t *x = c;
    if (x->open) {
        rfn_close();
    }
    x->open = false;
}

static const rfsrc_ops_t OPS = {st_state, st_claim, st_unclaim, st_open,  st_tune,
                                st_read,  st_sweep, st_close,   rfn_error};

bool rfsrc_native(rfsrc_t *out)
{
    if (!rfn_available()) {
        return false;
    }
    out->caps = &CAPS;
    out->ops = &OPS;
    out->ctx = &s_ctx;
    out->full_scale = 512;
    return true;
}

#else  // the PC: no native radio

bool rfsrc_native(rfsrc_t *out)
{
    (void)out;
    return false;
}

#endif
