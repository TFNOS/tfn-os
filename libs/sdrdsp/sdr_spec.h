#pragma once
// SDRDSP: THE MEASUREMENT OF A SPECTRUM from a source (libs/rfsrc). Two parts:
//   the PLAN (sdr_plan_make, pure): a request (the centre, the span, the points of the plot, the resolution) and the
//     capabilities of a source give the sample rate, the FFT size, the segments of a sweep (each segment one centre
//     of the source: a span wider than the usable band of one block needs more segments) and the axis of the output;
//   the ENGINE (sdr_engine_*): it does a plan on the source, one frame at a time: for each segment a tune, a block,
//     the window, the FFTs (averaged), the power in dB of the full scale, the hole of the LO, the map of the FFT bins
//     to the output points with a detector. A SWEEP source gives the points itself.
// The output of a frame: `bins` levels in dB, point i at the frequency f0 + (i + 0.5) * df (a point = a band df wide).
#include <stdbool.h>
#include <stdint.h>
#include "../rfsrc/rfsrc.h"
#include "sdr_fft.h"

enum {
    SDR_DET_PEAK,
    SDR_DET_AVG,
    SDR_DET_SAMPLE,
    SDR_DET_N
};
#define SDR_NODATA_SPEC (-1000.0f)  // a point with no data (the same value as SDR_NODATA of sdr_trace.h)
enum {
    SDR_RBW_AUTO,
    SDR_RBW_FINE,
    SDR_RBW_COARSE,
    SDR_RBW_N
};

typedef struct {
    int64_t center_hz, span_hz;
    int bins;          // the points of the output (the width of the plot in px)
    uint8_t gain;      // the index into caps.gain_val
    uint8_t window;    // SDR_WIN_*
    uint8_t detector;  // SDR_DET_*: how the FFT bins of one point make its level
    uint8_t rbw;       // SDR_RBW_*: AUTO = about 2 FFT bins a point, FINE = 4 times finer, COARSE = 4 times coarser
} sdr_req_t;

#define SDR_MAX_SEG  24
#define SDR_MAX_BINS 1024

typedef struct {
    int64_t lo_hz;  // the centre of the source for this segment
    int i0, i1;     // the output points that this segment gives (i0 <= i < i1)
} sdr_seg_t;

typedef struct {
    sdr_req_t req;           // the request after the clamps (the span and the centre that the source can do)
    uint8_t kind;            // RFSRC_IQ or RFSRC_SWEEP
    uint8_t rate;            // IQ: the index into caps.rate_hz
    int fft_n, navg, pairs;  // IQ: the FFT size, the FFTs of one block, the pairs of one block
    int nseg;
    sdr_seg_t seg[SDR_MAX_SEG];
    int64_t f0_hz;               // the lower edge of point 0
    double df_hz;                // the width of one point
    float rbw_hz;                // the real resolution bandwidth
    float frame_ms;              // about the time of one frame
    int64_t span_min, span_max;  // the limits of the span for this source
} sdr_plan_t;

// The plan of a request for a source. Returns false only for a source that cannot give a spectrum.
bool sdr_plan_make(const rfsrc_caps_t *c, const sdr_req_t *r, sdr_plan_t *p);
// The frequency of point i (its middle)
static inline double sdr_plan_freq(const sdr_plan_t *p, double i)
{
    return (double)p->f0_hz + (i + 0.5) * p->df_hz;
}
// The point of a frequency (can be outside 0 .. bins)
static inline double sdr_plan_point(const sdr_plan_t *p, double hz)
{
    return (hz - (double)p->f0_hz) / p->df_hz - 0.5;
}
// Two plans with the same axis (the same points at the same frequencies)
bool sdr_plan_same_axis(const sdr_plan_t *a, const sdr_plan_t *b);

typedef struct {
    bool overload;                           // a block had samples at the full scale
    float ms;                                // the time of the frame
    float ms_tune, ms_read, ms_fft, ms_map;  // the time of each step (the sum of the segments)
    int err;                                 // < 0: the source failed in all segments (its ops.error text)
    int bad_seg;                             // the segments that did not tune (their points are SDR_NODATA_SPEC)
} sdr_frame_t;

typedef struct {
    rfsrc_t src;
    bool open;
    int cur_rate, cur_gain;
    int64_t cur_lo;
    sdr_fft_t fft;
    int win_type, win_n;
    float *win, cg, enbw;
    float *x;     // the FFT buffer (2 * fft_n)
    float *acc;   // the mean power of each FFT bin (fft_n)
    int16_t *iq;  // one block (2 * max_pairs)
    int iq_n;     // the pairs of the last block (for the SCOPE view)
    int64_t iq_lo;
    uint32_t iq_rate;
} sdr_engine_t;

// Open the source with the first plan. Returns 0 or the error of the source.
int sdr_engine_start(sdr_engine_t *e, const rfsrc_t *src, const sdr_plan_t *p);
// One frame of the plan into out (p->req.bins levels in dB).
void sdr_engine_frame(sdr_engine_t *e, const sdr_plan_t *p, float *out, sdr_frame_t *info);
void sdr_engine_stop(sdr_engine_t *e);
