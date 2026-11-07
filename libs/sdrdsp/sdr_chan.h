#pragma once
// SDRDSP: THE CHANNEL PLANS (data) and the power and the use (occupancy) of each channel (pure C).
// A plan is a list of named channels (a centre and a width). The CHANNELS view of the app shows the plans that touch
// the span now: the WiFi channels of 2.4 GHz, the Bluetooth LE channels, the Zigbee channels. A new radio band = a
// new plan in sdr_chan.c (for example the 433 / 868 MHz ISM channels for a sub-GHz radio).
#include <stdint.h>

typedef struct {
    const char *name;  // "1", "37", ...
    int64_t f_hz;
    uint32_t bw_hz;
} sdr_chan_t;

typedef struct {
    const char *name[2];  // English, Hebrew
    const sdr_chan_t *ch;
    int n;
    float busy_db;  // a channel is "in use" in a frame when its power is this much over the floor
} sdr_chplan_t;

// All plans (returns the count)
int sdr_chplans(const sdr_chplan_t **out, int max);
// The mean power (dB) of the points of a band on an axis (SDR_NODATA when the band is not on the axis)
float sdr_band_power(const float *db, int n, int64_t f0_hz, double df_hz, int64_t f_hz, uint32_t bw_hz);
