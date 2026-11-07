// SDRDSP: the channel plans and the power of a band (sdr_chan.h).
#include "sdr_chan.h"

#include <math.h>
#include "sdr_trace.h"

#define MHZ 1000000LL

static const sdr_chan_t WIFI24[] = {
        {"1", 2412 * MHZ, 20000000},  {"2", 2417 * MHZ, 20000000},  {"3", 2422 * MHZ, 20000000},
        {"4", 2427 * MHZ, 20000000},  {"5", 2432 * MHZ, 20000000},  {"6", 2437 * MHZ, 20000000},
        {"7", 2442 * MHZ, 20000000},  {"8", 2447 * MHZ, 20000000},  {"9", 2452 * MHZ, 20000000},
        {"10", 2457 * MHZ, 20000000}, {"11", 2462 * MHZ, 20000000}, {"12", 2467 * MHZ, 20000000},
        {"13", 2472 * MHZ, 20000000}, {"14", 2484 * MHZ, 20000000},
};

// Bluetooth LE: 40 channels of 2 MHz; 37, 38 and 39 are the advertising channels (2402, 2426, 2480 MHz)
static const sdr_chan_t BLE_ADV[] = {
        {"37", 2402 * MHZ, 2000000},
        {"38", 2426 * MHZ, 2000000},
        {"39", 2480 * MHZ, 2000000},
};

// IEEE 802.15.4 (Zigbee, Thread): channels 11..26, 2405 + 5 (k - 11) MHz, 2 MHz wide
static const sdr_chan_t ZIGBEE[] = {
        {"11", 2405 * MHZ, 2000000}, {"12", 2410 * MHZ, 2000000}, {"13", 2415 * MHZ, 2000000},
        {"14", 2420 * MHZ, 2000000}, {"15", 2425 * MHZ, 2000000}, {"16", 2430 * MHZ, 2000000},
        {"17", 2435 * MHZ, 2000000}, {"18", 2440 * MHZ, 2000000}, {"19", 2445 * MHZ, 2000000},
        {"20", 2450 * MHZ, 2000000}, {"21", 2455 * MHZ, 2000000}, {"22", 2460 * MHZ, 2000000},
        {"23", 2465 * MHZ, 2000000}, {"24", 2470 * MHZ, 2000000}, {"25", 2475 * MHZ, 2000000},
        {"26", 2480 * MHZ, 2000000},
};

// The 433 MHz ISM band (Europe, 433.05-434.79 MHz): the usual remote keys and sensors (for a sub-GHz radio)
static const sdr_chan_t ISM433[] = {
        {"433.42", 433420000LL, 200000},
        {"433.92", 433920000LL, 200000},
        {"434.42", 434420000LL, 200000},
};

static const sdr_chplan_t PLANS[] = {
        {{"WIFI 2.4 GHZ", "WIFI 2.4 גיגה"}, WIFI24, 14, 8.0f},
        {{"BLUETOOTH LE ADVERTISING", "בלוטות' – פרסום"}, BLE_ADV, 3, 8.0f},
        {{"ZIGBEE / THREAD", "זיגבי / ת'רד"}, ZIGBEE, 16, 8.0f},
        {{"ISM 433 MHZ", "ISM 433 מגה"}, ISM433, 3, 8.0f},
};

int sdr_chplans(const sdr_chplan_t **out, int max)
{
    int n = (int)(sizeof(PLANS) / sizeof(PLANS[0]));
    for (int i = 0; i < n && i < max; i++) {
        out[i] = &PLANS[i];
    }
    return n < max ? n : max;
}

float sdr_band_power(const float *db, int n, int64_t f0_hz, double df_hz, int64_t f_hz, uint32_t bw_hz)
{
    double a = ((double)(f_hz - bw_hz / 2) - (double)f0_hz) / df_hz,
           b = ((double)(f_hz + bw_hz / 2) - (double)f0_hz) / df_hz;
    int i0 = (int)floor(a), i1 = (int)ceil(b) - 1;
    if (i1 < 0 || i0 >= n) {
        return SDR_NODATA;
    }
    if (i0 < 0) {
        i0 = 0;
    }
    if (i1 > n - 1) {
        i1 = n - 1;
    }
    float s = 0;  // (float: the FPU of the board has no double)
    int m = 0;
    for (int i = i0; i <= i1; i++) {
        if (db[i] <= SDR_NODATA) {
            continue;
        }
        s += powf(10.0f, db[i] * 0.1f);
        m++;
    }
    return m ? 10.0f * log10f(s / (float)m + 1e-20f) : SDR_NODATA;
}
