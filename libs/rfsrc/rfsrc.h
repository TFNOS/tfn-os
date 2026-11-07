#pragma once
// RFSRC: THE CONTRACT OF A RADIO SOURCE. A source is a receiver that gives one of two kinds of data:
//   RFSRC_IQ     blocks of complex samples (I, Q) around a centre (LO): the native ESP32-S3 radio, a demo, later an
//                I/Q chip on the pins (SX1255 over I2S, ...). The spectrum comes from an FFT (libs/sdrdsp).
//   RFSRC_SWEEP  the power at a list of frequencies (an RSSI sweep): later a sub-GHz chip on the pins (CC1101,
//                SX127x, SX126x) that has no I/Q output.
// The user of a source (the SDR app, the planner of sdrdsp) knows only this header: the capabilities (caps) tell it
// the range, the rates, the usable band and the gain; the ops do the work. A new radio = one new file with one
// rfsrc_t, and one line in rfsrc_enum() (rfsrc.c). Nothing here draws or knows the screen.
#include <stdbool.h>
#include <stdint.h>

enum {
    RFSRC_IQ = 1,
    RFSRC_SWEEP = 2
};

// The state of a source (ops.state)
enum {
    RFSRC_READY = 0,        // open() can start it now
    RFSRC_NEEDS_START = 1,  // it needs a special start of the board first (ops.claim): the native radio (no WiFi)
    RFSRC_ABSENT = 2,       // the hardware is not there
};

// caps.flags
#define RFSRC_F_DEMO   0x01  // made-up signals (not a radio)
#define RFSRC_F_NOWIFI 0x02  // the WiFi is off while the source runs (the native radio)
#define RFSRC_F_DBM    0x04  // the levels are in dBm (calibrated); else in dBFS (dB of the full scale)

#define RFSRC_MAX_BANDS 4
#define RFSRC_MAX_RATES 4
#define RFSRC_MAX_GAINS 4

typedef struct {
    int64_t lo_hz, hi_hz;  // a range that the centre (IQ) or the points (SWEEP) can have
} rfsrc_band_t;

typedef struct {
    const char *id;       // a short fixed name for the store keys ("esp32", "demo")
    const char *name[2];  // the name shown on the screen: English, Hebrew
    uint8_t kind;         // RFSRC_IQ or RFSRC_SWEEP
    uint8_t flags;        // RFSRC_F_*
    uint8_t nbands;
    rfsrc_band_t band[RFSRC_MAX_BANDS];
    // IQ: the sample rates (complex pairs a second), and for each rate the usable part: the band LO +- usable_hz has
    // a flat filter and no alias. dc_hz: the band round the LO with the leak of the LO (0 = none).
    uint8_t nrates;
    uint32_t rate_hz[RFSRC_MAX_RATES];
    uint32_t usable_hz[RFSRC_MAX_RATES];
    uint32_t dc_hz;
    uint16_t max_pairs;  // the longest block of read_iq
    // SWEEP: the narrowest step and the width of one point (the channel filter of the chip)
    uint32_t step_hz, point_bw_hz;
    uint32_t max_points;  // the most points of one read_sweep
    // the gain: the steps for the user (the names) and the value of each step for tune()
    uint8_t ngains, gain_def;
    const char *gain_name[RFSRC_MAX_GAINS][2];
    uint8_t gain_val[RFSRC_MAX_GAINS];
    float gain_db[RFSRC_MAX_GAINS];  // about the dB of each step against the default step (the engine takes it out
                                     // of the levels: a new gain does not move the traces)
    uint32_t tune_us;                // about the time of one tune (the planner uses it to choose the fewest tunes)
    float full_scale_db;             // the level of a full-scale sine (0 for dBFS; the dBm for a calibrated source)
} rfsrc_caps_t;

// One setup of a source
typedef struct {
    int64_t center_hz;  // IQ: the LO; SWEEP: not used
    uint8_t rate;       // IQ: the index into caps.rate_hz
    uint8_t gain;       // the index into caps.gain_val
} rfsrc_set_t;

// What came with a block
typedef struct {
    int64_t center_hz;  // the real LO of the block
    uint32_t rate_hz;
    bool overload;  // samples at the full scale (the gain is too high)
} rfsrc_meta_t;

typedef struct {
    int (*state)(void *ctx);                                        // RFSRC_READY / NEEDS_START / ABSENT
    void (*claim)(void *ctx, const char *app);                      // NEEDS_START: do the start (it can restart the
                                                                    // board and open the app `app` again)
    void (*unclaim)(void *ctx);                                     // the app quits: end the special start (the native
                                                                    // radio: a normal restart, the WiFi comes back)
    int (*open)(void *ctx, const rfsrc_set_t *s);                   // 0 = ok, < 0 = an error
    int (*tune)(void *ctx, const rfsrc_set_t *s, int64_t *center);  // a new setup (a new centre is the usual change)
    // IQ: n pairs into iq (2 * n int16: I, Q; the full scale = 32767 / 64 = 512 counts for the native radio: the
    // meta tells nothing of the scale; caps.full_scale_db and the counts below do: see RFSRC_FS)
    int (*read_iq)(void *ctx, int16_t *iq, int n, rfsrc_meta_t *m);
    // SWEEP: the power (dB) of n points f0, f0 + step, ... into db; returns n or < 0
    int (*read_sweep)(void *ctx, int64_t f0_hz, uint32_t step_hz, int n, float *db);
    void (*close)(void *ctx);
    const char *(*error)(int e);  // the text of an error
} rfsrc_ops_t;

typedef struct {
    const rfsrc_caps_t *caps;
    const rfsrc_ops_t *ops;
    void *ctx;
    int16_t full_scale;  // IQ: the count of a full-scale sample (512 for the native radio: 10 bits)
} rfsrc_t;

// All sources of this board, the real ones first, then the demo. tests = true: also the sources of the tests (the
// SWEEP demo). Returns the count.
int rfsrc_enum(rfsrc_t *out, int max, bool tests);
// The source with this id (from the list with the tests), or false.
bool rfsrc_find(const char *id, rfsrc_t *out);
// The sources of this library (each file gives one; rfsrc_enum calls them)
bool rfsrc_native(rfsrc_t *out);      // rfsrc_native.c: the WiFi receiver of the ESP32-S3 (rf_native.h of TFN OS)
bool rfsrc_demo(rfsrc_t *out);        // rfsrc_demo.c: made-up signals (the PC tests, a board with no radio)
bool rfsrc_demo_sweep(rfsrc_t *out);  // rfsrc_demo.c: made-up signals as a SWEEP source (the test of that kind)
