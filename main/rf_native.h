#pragma once

// THE NATIVE RADIO (TFN OS): the WiFi receiver of the ESP32-S3 as a receiver of raw I/Q samples, through its
// debug "dump" path (not documented by Espressif; found by the ESPARGOS ESP-SDR and eSpDR projects, 2026).
//
// The dump engine writes the samples into a FIXED internal RAM block (0x3FCD0000, 64 KB). The firmware keeps that
// block out of the heap at the start, and gives it to the heap again at once in a normal start. Only a RADIO START
// (rfn_restart(true, app)) keeps it free: that start has no WiFi (the receiver belongs to the radio) and opens the
// app that asked for it. The next restart is a normal start again (also after a crash: the request is used once).
//
// The samples: complex pairs of 10-bit signed values (-512..511), 80 or 16 million pairs a second. rfn_capture gives
// them as int16 I, Q pairs with the normal sign: a signal above the LO has a positive frequency.

#include <stdbool.h>
#include <stdint.h>

enum {
    RFN_RATE_80M = 0,
    RFN_RATE_16M = 1
};

typedef struct {
    uint32_t lo_hz;  // the centre (LO): 2210..2790 MHz (the PLL must lock: about 2.21..2.78 GHz)
    uint8_t rate;    // RFN_RATE_*
    uint8_t width;   // the analog filter bank: 20 or 40 (MHz)
    uint8_t gain;    // the index of the gain table: 0..127 (about 1 dB a step from 35 to 76; not in dB)
    uint8_t filter;  // the RC code of the filter bank: 0 (the widest) .. 63
} rfn_cfg_t;

#define RFN_MAX_PAIRS 16380  // the most pairs of one capture (the 64 KB block)

enum {
    RFN_OK = 0,
    RFN_E_NOCHIP = -1,    // no native radio on this chip
    RFN_E_NOTRADIO = -2,  // this start is not a radio start (the block belongs to the heap)
    RFN_E_PLL = -3,       // the PLL did not lock at this frequency
    RFN_E_PBUS = -4,      // the receiver setup failed
    RFN_E_TIMEOUT = -5,   // the capture did not end
    RFN_E_ARG = -6,       // a bad value
    RFN_E_CLOSED = -7,    // rfn_open() first
};

// main.c: the first call of app_main (the block to the heap, or the radio start).
void rfn_early(void);
// true: the chip has the native radio (all ESP32-S3 kinds; not the Tab5).
bool rfn_available(void);
// true: this start is a radio start.
bool rfn_radio_boot(void);
// The app to open in the radio start ("" when none).
const char *rfn_boot_app(void);
// Restart: radio = true: a radio start that opens the app `app` (its name; NULL = the menu); false: a normal start.
void rfn_restart(bool radio, const char *app);

// The receiver: power, PHY calibration, the first setup.
int rfn_open(const rfn_cfg_t *cfg);
// A new setup; *lo_actual (can be NULL) = the real LO (the step of the PLL is about 458 Hz).
int rfn_set(const rfn_cfg_t *cfg, uint32_t *lo_actual);
// Only a new centre (the gain and the filter stay): the fast way for a sweep (a MHz that locked once: about 0.1 ms;
// a new MHz: the scan of the PLL, about 26 ms).
int rfn_tune(uint32_t lo_hz, uint32_t *lo_actual);
// One capture of n pairs (1..RFN_MAX_PAIRS) into iq (2 * n values). *gain_idx (can be NULL) = the gain index of the
// samples. Returns n, or RFN_E_*.
int rfn_capture(int16_t *iq, int n, uint8_t *gain_idx);
// The receiver off.
void rfn_close(void);
// The sample rate of a rate value (pairs a second).
uint32_t rfn_rate_hz(int rate);
const char *rfn_error(int e);
