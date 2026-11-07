#pragma once
// SDRDSP: THE TRACES of a spectrum (pure C). Each frame of the engine goes into all traces:
//   LIVE   the last frame
//   MAX    the max hold (HOLD: it stays; FALL: it falls decay_db a second, a "peak decay" trace)
//   MIN    the min hold
//   AVG    the mean POWER of the last avg_n frames (an exponential mean after the first avg_n frames)
// A point with no data yet is SDR_NODATA (the views do not draw it). When the axis changes (a pan, a zoom), the traces
// keep their holds: each new point takes the old points under its band (sdr_traces_axis); a new band gets SDR_NODATA.
// Also: the floor of the noise, and a search of the peaks.
#include <stdbool.h>
#include <stdint.h>

#define SDR_NODATA (-1000.0f)

enum {
    SDR_TR_LIVE,
    SDR_TR_MAX,
    SDR_TR_MIN,
    SDR_TR_AVG,
    SDR_TR_N
};

typedef struct {
    int n, nmax;
    float *v[SDR_TR_N];  // dB
    float *avg_p;        // the mean power (linear) of the AVG trace
    uint16_t *cnt;       // the frames in the mean of each point (up to avg_n)
    int avg_n;           // the length of the mean (frames)
    float decay_db;      // MAX: the fall in dB a second (0 = a true hold)
    int64_t f0_hz;       // the axis of the traces now
    double df_hz;
    uint32_t frames;  // the frames since the last reset
} sdr_traces_t;

bool sdr_traces_init(sdr_traces_t *t, int nmax);
void sdr_traces_free(sdr_traces_t *t);
// All traces empty
void sdr_traces_reset(sdr_traces_t *t);
// A new axis: n points, point i = the band f0 + i * df .. f0 + (i + 1) * df. The traces move to it.
void sdr_traces_axis(sdr_traces_t *t, int n, int64_t f0_hz, double df_hz);
// One frame (n points on the axis of the traces); dt_s = the time since the last frame (for the fall of MAX)
void sdr_traces_push(sdr_traces_t *t, const float *db, float dt_s);

// The floor of the noise: the 20th percentile of the points that have data (SDR_NODATA when none)
float sdr_floor(const float *db, int n);

typedef struct {
    int i;  // the point
    float db;
    float x;  // the point with a fraction (the parabola of the top)
} sdr_peak_t;
// The highest local maxima: at least rise_db over the floor and over the lowest point between them and a higher peak
// (the prominence), at least sep points from each other. Returns the count (at most max), the highest first.
int sdr_peaks(const float *db, int n, float floor_db, float rise_db, int sep, sdr_peak_t *out, int max);
