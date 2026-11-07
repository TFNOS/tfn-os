// SDRDSP: the plan of a measurement and the engine that does it (sdr_spec.h).
#include "sdr_spec.h"

#include <math.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
static double now_ms(void)
{
    return (double)esp_timer_get_time() / 1000.0;
}
#else
#include <time.h>
static double now_ms(void)
{
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
}
#endif

#define FFT_MIN  64
#define FFT_MAX  8192
#define NAVG_MAX 8

static int pow2_at_least(double v)
{
    int n = 1;
    while (n < v && n < (1 << 20)) {
        n <<= 1;
    }
    return n;
}

static int pow2_at_most(int v)
{
    int n = 1;
    while (n * 2 <= v) {
        n <<= 1;
    }
    return n;
}

// The band of the source that has the centre (or the nearest band)
static const rfsrc_band_t *pick_band(const rfsrc_caps_t *c, int64_t f)
{
    const rfsrc_band_t *best = &c->band[0];
    int64_t bd = INT64_MAX;
    for (int b = 0; b < c->nbands; b++) {
        const rfsrc_band_t *x = &c->band[b];
        int64_t d = f < x->lo_hz ? x->lo_hz - f : f > x->hi_hz ? f - x->hi_hz : 0;
        if (d < bd) {
            bd = d;
            best = x;
        }
    }
    return best;
}

// sdr_plan_make: the request in the limits (the bins, the gain, the window, the detector, the resolution)
static void plan_limits(const rfsrc_caps_t *c, sdr_req_t *r)
{
    if (r->bins < 16) {
        r->bins = 16;
    }
    if (r->bins > SDR_MAX_BINS) {
        r->bins = SDR_MAX_BINS;
    }
    if (r->gain >= c->ngains) {
        r->gain = c->ngains ? c->gain_def : 0;
    }
    if (r->window >= SDR_WIN_N) {
        r->window = SDR_WIN_HANN;
    }
    if (r->detector >= SDR_DET_N) {
        r->detector = SDR_DET_PEAK;
    }
    if (r->rbw >= SDR_RBW_N) {
        r->rbw = SDR_RBW_AUTO;
    }
}

// sdr_plan_make: the span limits of the source, the span and the centre in them, the first frequency and the step
static void plan_span(const rfsrc_caps_t *c, sdr_req_t *r, sdr_plan_t *p, const rfsrc_band_t *band)
{
    int64_t bw = band->hi_hz - band->lo_hz;
    if (c->kind == RFSRC_IQ) {
        uint32_t rmin = c->rate_hz[0], umax = 0;
        for (int i = 0; i < c->nrates; i++) {
            if (c->rate_hz[i] < rmin) {
                rmin = c->rate_hz[i];
            }
            if (c->usable_hz[i] > umax) {
                umax = c->usable_hz[i];
            }
        }
        p->span_min = (int64_t)((double)rmin / FFT_MAX * r->bins / 2);
        p->span_max = bw;
        // (one side of the LO for each segment, less the guard: about 0.9 of the usable band)
        if (p->span_max > (int64_t)(SDR_MAX_SEG * 0.9 * umax)) {
            p->span_max = (int64_t)(SDR_MAX_SEG * 0.9 * umax);
        }
    } else {
        p->span_min = (int64_t)c->step_hz * r->bins;
        p->span_max = bw;
    }
    if (p->span_min > p->span_max) {
        p->span_min = p->span_max;
    }
    if (r->span_hz < p->span_min) {
        r->span_hz = p->span_min;
    }
    if (r->span_hz > p->span_max) {
        r->span_hz = p->span_max;
    }
    if (r->center_hz - r->span_hz / 2 < band->lo_hz) {
        r->center_hz = band->lo_hz + r->span_hz / 2;
    }
    if (r->center_hz + r->span_hz / 2 > band->hi_hz) {
        r->center_hz = band->hi_hz - r->span_hz / 2;
    }
    p->f0_hz = r->center_hz - r->span_hz / 2;
    p->df_hz = (double)r->span_hz / r->bins;
}

// sdr_plan_make: the FFT size and the guard of each rate (for the resolution target), and the rate of the plan
static int plan_rate(const rfsrc_caps_t *c, const sdr_req_t *r, double target, int *fft_of, double *guard_of)
{
    int nmax = pow2_at_most(c->max_pairs);
    if (nmax > FFT_MAX) {
        nmax = FFT_MAX;
    }
    for (int i = 0; i < c->nrates; i++) {
        // (AUTO: an FFT bin is about half a point: the nearest power of 2; the next one costs twice the time)
        int n = pow2_at_least(c->rate_hz[i] / target / 1.414);
        if (n > nmax) {
            n = nmax;
        }
        if (n < FFT_MIN) {
            n = FFT_MIN;
        }
        fft_of[i] = n;
        double g = 1.5 * c->dc_hz, gb = 3.0 * c->rate_hz[i] / n;
        guard_of[i] = g > gb ? g : gb;
    }
    // the rate: the lowest rate that gives the span in one segment (the best resolution); else the widest one
    int rate = -1;
    for (int i = 0; i < c->nrates; i++) {
        if (c->usable_hz[i] - guard_of[i] >= (double)r->span_hz && (rate < 0 || c->rate_hz[i] < c->rate_hz[rate])) {
            rate = i;
        }
    }
    if (rate < 0) {
        rate = 0;
        for (int i = 1; i < c->nrates; i++) {
            if (c->usable_hz[i] - guard_of[i] > c->usable_hz[rate] - guard_of[rate]) {
                rate = i;
            }
        }
    }
    return rate;
}

// sdr_plan_make: the points and the LO of each segment (the LO under the points, or over them at the bottom)
static void plan_segments(const sdr_req_t *r, sdr_plan_t *p, const rfsrc_band_t *band, int nseg, double guard)
{
    for (int k = 0; k < nseg; k++) {
        sdr_seg_t *s = &p->seg[k];
        s->i0 = (int)((int64_t)k * r->bins / nseg);
        s->i1 = (int)((int64_t)(k + 1) * r->bins / nseg);
        int64_t lo_edge = p->f0_hz + (int64_t)llround(s->i0 * p->df_hz);
        int64_t hi_edge = p->f0_hz + (int64_t)llround(s->i1 * p->df_hz);
        s->lo_hz = lo_edge - (int64_t)ceil(guard);  // the LO under the points
        if (s->lo_hz < band->lo_hz) {
            s->lo_hz = hi_edge + (int64_t)ceil(guard);  // (at the bottom: the LO over them)
        }
    }
}

// sdr_plan_make: the FFTs of a block, the resolution and the time of a frame
static void plan_frame(const rfsrc_caps_t *c, const sdr_req_t *r, sdr_plan_t *p, int n, int nseg, double fs)
{
    p->fft_n = n;
    p->navg = c->max_pairs / n;
    int budget = 32768 / (n * nseg);
    if (p->navg > budget) {
        p->navg = budget;
    }
    if (p->navg > NAVG_MAX) {
        p->navg = NAVG_MAX;
    }
    if (p->navg < 1) {
        p->navg = 1;
    }
    p->pairs = p->navg * n;
    // (the ENBW of the windows in bins: Hann 1.5, BH4 2.0, flat top 3.77, rect 1.0)
    static const float ENBW[SDR_WIN_N] = {1.5f, 2.004f, 3.77f, 1.0f};
    p->rbw_hz = (float)(ENBW[r->window] * fs / n);
    double per = c->tune_us / 1000.0 + p->pairs / fs * 1000.0 + p->pairs * 0.00012 +
                 p->navg * (double)n * log2((double)n) * 0.00004;
    p->frame_ms = (float)(nseg * per);
}

bool sdr_plan_make(const rfsrc_caps_t *c, const sdr_req_t *r0, sdr_plan_t *p)
{
    memset(p, 0, sizeof(*p));
    p->req = *r0;
    sdr_req_t *r = &p->req;
    if (!c || c->nbands < 1) {
        return false;
    }
    if (c->kind == RFSRC_IQ && c->nrates < 1) {
        return false;
    }
    plan_limits(c, r);
    p->kind = c->kind;

    const rfsrc_band_t *band = pick_band(c, r->center_hz);
    plan_span(c, r, p, band);

    if (c->kind == RFSRC_SWEEP) {
        p->nseg = 1;
        p->seg[0].lo_hz = r->center_hz;
        p->seg[0].i0 = 0;
        p->seg[0].i1 = r->bins;
        p->rbw_hz = (float)(c->point_bw_hz > p->df_hz ? c->point_bw_hz : p->df_hz);
        int np = r->bins < (int)c->max_points ? r->bins : (int)c->max_points;
        p->frame_ms = (float)np * c->tune_us / 1000.0f;
        return true;
    }

    // OFFSET TUNING: a segment uses ONE side of its LO, from LO + guard to LO + usable (or the mirror side at the top
    // of the band). The leak of the LO and the I/Q images of the other side stay out of the picture; no signal falls
    // into a hole. The guard: the leak (dc_hz) and 3 FFT bins.
    const double target = r->rbw == SDR_RBW_FINE     ? p->df_hz / 8
                          : r->rbw == SDR_RBW_COARSE ? p->df_hz * 2
                                                     : p->df_hz / 2;
    int fft_of[RFSRC_MAX_RATES];
    double guard_of[RFSRC_MAX_RATES];
    int rate = plan_rate(c, r, target, fft_of, guard_of);
    p->rate = (uint8_t)rate;
    const double fs = c->rate_hz[rate], guard = guard_of[rate];
    const int n = fft_of[rate];
    // the points that fit in one side of a block, then the segments (no segment has more points)
    int fit = (int)floor((c->usable_hz[rate] - guard) / p->df_hz + 1e-9);
    if (fit < 1) {
        fit = 1;
    }
    int nseg = (r->bins + fit - 1) / fit;
    if (nseg > SDR_MAX_SEG) {
        nseg = SDR_MAX_SEG;
    }
    p->nseg = nseg;
    plan_segments(r, p, band, nseg, guard);
    // the FFTs of a block: at most NAVG_MAX, and about 32 K points of FFT in a frame (the time of a frame)
    plan_frame(c, r, p, n, nseg, fs);
    return true;
}

bool sdr_plan_same_axis(const sdr_plan_t *a, const sdr_plan_t *b)
{
    return a->f0_hz == b->f0_hz && a->req.bins == b->req.bins && fabs(a->df_hz - b->df_hz) < 1e-6;
}

// ---------------------------------------------------------------- the engine

static bool engine_fft(sdr_engine_t *e, int n, int win)
{
    if (e->fft.n == n && e->win_type == win && e->win_n == n) {
        return true;
    }
    sdr_fft_free(&e->fft);
    sdr_free(e->win);
    sdr_free(e->x);
    sdr_free(e->acc);
    e->win = NULL;
    e->x = NULL;
    e->acc = NULL;
    e->win_n = 0;
    if (!sdr_fft_init(&e->fft, n)) {
        return false;
    }
    e->win = sdr_alloc(sizeof(float) * (size_t)n);
    e->x = sdr_alloc(sizeof(float) * 2 * (size_t)n);
    e->acc = sdr_alloc(sizeof(float) * (size_t)n);
    if (!e->win || !e->x || !e->acc) {
        return false;
    }
    sdr_window(win, n, e->win, &e->cg, &e->enbw);
    e->win_type = win;
    e->win_n = n;
    return true;
}

int sdr_engine_start(sdr_engine_t *e, const rfsrc_t *src, const sdr_plan_t *p)
{
    memset(e, 0, sizeof(*e));
    e->src = *src;
    e->win_type = -1;
    const rfsrc_caps_t *c = src->caps;
    if (c->kind == RFSRC_IQ) {
        e->iq = sdr_alloc(sizeof(int16_t) * 2 * (size_t)c->max_pairs);
        if (!e->iq) {
            return -100;
        }
    }
    rfsrc_set_t s = {.center_hz = p->seg[0].lo_hz, .rate = p->rate, .gain = p->req.gain};
    int r = src->ops->open(src->ctx, &s);
    if (r < 0) {
        sdr_free(e->iq);
        e->iq = NULL;
        return r;
    }
    e->open = true;
    e->cur_rate = p->rate;
    e->cur_gain = p->req.gain;
    e->cur_lo = p->seg[0].lo_hz;
    return 0;
}

void sdr_engine_stop(sdr_engine_t *e)
{
    if (e->open) {
        e->src.ops->close(e->src.ctx);
    }
    e->open = false;
    sdr_fft_free(&e->fft);
    sdr_free(e->win);
    sdr_free(e->x);
    sdr_free(e->acc);
    sdr_free(e->iq);
    e->win = NULL;
    e->x = NULL;
    e->acc = NULL;
    e->iq = NULL;
}

// The power of one block into e->acc, in the order of the frequency (index N / 2 = the LO), 1.0 = a full-scale sine.
// (float and int only: the FPU of the ESP32-S3 has no double; a double is about 20 times slower)
static void block_power(sdr_engine_t *e, const sdr_plan_t *p, int pairs, float fs_count)
{
    const int n = p->fft_n;
    int32_t si = 0, sq = 0;  // (16380 pairs of 10 bits: no overflow)
    for (int k = 0; k < pairs; k++) {
        si += e->iq[2 * k];
        sq += e->iq[2 * k + 1];
    }
    const float mi = (float)si / pairs, mq = (float)sq / pairs;
    float *acc = e->acc;
    for (int k = 0; k < n; k++) {
        acc[k] = 0;
    }
    const float sc = 1.0f / fs_count;
    int navg = pairs / n;
    if (navg < 1) {
        navg = 1;
    }
    for (int a = 0; a < navg; a++) {
        const int16_t *s = e->iq + 2 * a * n;
        float *x = e->x;
        const float *w = e->win;
        for (int k = 0; k < n; k++) {
            float wk = w[k] * sc;
            x[2 * k] = ((float)s[2 * k] - mi) * wk;
            x[2 * k + 1] = ((float)s[2 * k + 1] - mq) * wk;
        }
        sdr_fft(&e->fft, x);
        const int h = n / 2;  // the shift: the LO in the middle
        for (int k = 0; k < h; k++) {
            acc[k + h] += x[2 * k] * x[2 * k] + x[2 * k + 1] * x[2 * k + 1];
        }
        for (int k = h; k < n; k++) {
            acc[k - h] += x[2 * k] * x[2 * k] + x[2 * k + 1] * x[2 * k + 1];
        }
    }
    // a full-scale sine of amplitude 1 gives |X| = n * cg
    const float ncg = (float)n * e->cg, norm = 1.0f / ((float)navg * ncg * ncg);
    for (int k = 0; k < n; k++) {
        acc[k] *= norm;
    }
}

// The hole of the LO: the bins within dc_hz of the LO get the line between the bins on each side of the hole
static void lo_hole(sdr_engine_t *e, const rfsrc_caps_t *c, double bin_hz, int n)
{
    if (!c->dc_hz) {
        return;
    }
    int h = (int)ceilf((float)(c->dc_hz / bin_hz));
    if (h < 1) {
        h = 1;
    }
    int a = n / 2 - h - 1, b = n / 2 + h + 1;
    if (a < 0 || b >= n) {
        return;
    }
    for (int k = a + 1; k < b; k++) {
        e->acc[k] = e->acc[a] + (e->acc[b] - e->acc[a]) * (float)(k - a) / (float)(b - a);
    }
}

static inline float to_db(float p, float fs_db)
{
    return 10.0f * log10f(p + 1e-20f) + fs_db;
}

// The output points i0 .. i1 from the bins of one block at the LO lo. The bin of a point: b0 + i * db (float)
static void map_points(const sdr_engine_t *e, const sdr_plan_t *p, const sdr_seg_t *s, int64_t lo, double bin_hz,
                       float fs_db, float *out)
{
    const int n = p->fft_n;
    const float *acc = e->acc;
    const float hw = (float)(p->df_hz / bin_hz / 2);  // the half width of a point, in bins
    const float db = (float)(p->df_hz / bin_hz);
    const float b0 = (float)(((double)p->f0_hz + 0.5 * p->df_hz - (double)lo) / bin_hz + n / 2);
    for (int i = s->i0; i < s->i1; i++) {
        const float b = b0 + (float)i * db;  // the bin of the middle of the point
        float v;
        if (hw < 0.5f) {
            int k = (int)floorf(b);
            float t = b - (float)k;
            if (k < 0) {
                k = 0;
                t = 0;
            }
            if (k >= n - 1) {
                k = n - 2;
                t = 1;
            }
            v = acc[k] * (1 - t) + acc[k + 1] * t;
        } else {
            int k0 = (int)ceilf(b - hw), k1 = (int)floorf(b + hw);
            if (k0 < 0) {
                k0 = 0;
            }
            if (k1 > n - 1) {
                k1 = n - 1;
            }
            if (k1 < k0) {
                k1 = k0;
            }
            if (p->req.detector == SDR_DET_SAMPLE) {
                int k = (int)lroundf(b);
                v = acc[k < 0 ? 0 : k > n - 1 ? n - 1 : k];
            } else if (p->req.detector == SDR_DET_AVG) {
                v = 0;
                for (int k = k0; k <= k1; k++) {
                    v += acc[k];
                }
                v /= (float)(k1 - k0 + 1);
            } else {
                v = 0;
                for (int k = k0; k <= k1; k++) {
                    if (acc[k] > v) {
                        v = acc[k];
                    }
                }
            }
        }
        out[i] = to_db(v, fs_db);
    }
}

void sdr_engine_frame(sdr_engine_t *e, const sdr_plan_t *p, float *out, sdr_frame_t *info)
{
    const double t0 = now_ms();
    memset(info, 0, sizeof(*info));
    const rfsrc_caps_t *c = e->src.caps;
    const rfsrc_ops_t *o = e->src.ops;
    const int bins = p->req.bins;
    if (!e->open) {
        info->err = -101;
        return;
    }
    if (c->kind == RFSRC_SWEEP) {
        if (p->req.gain != e->cur_gain) {
            rfsrc_set_t s = {.center_hz = p->req.center_hz, .rate = 0, .gain = p->req.gain};
            int64_t got;
            o->tune(e->src.ctx, &s, &got);
            e->cur_gain = p->req.gain;
        }
        int np = bins < (int)c->max_points ? bins : (int)c->max_points;
        if (np < 1) {
            np = 1;
        }
        double step = (double)p->req.span_hz / np;
        float *tmp = (float *)out;  // np <= bins: the points first, then spread out from the end
        int r = o->read_sweep(e->src.ctx, p->f0_hz + (int64_t)llround(step / 2), (uint32_t)llround(step), np, tmp);
        if (r < 0) {
            info->err = r;
            return;
        }
        if (np < bins) {
            for (int i = bins - 1; i >= 0; i--) {
                out[i] = tmp[(int)((sdr_plan_freq(p, i) - p->f0_hz) / step)];
            }
        }
        info->ms = info->ms_read = (float)(now_ms() - t0);
        return;
    }
    if (!engine_fft(e, p->fft_n, p->req.window)) {
        info->err = -100;
        return;
    }
    const double fs = c->rate_hz[p->rate];
    const double bin_hz = fs / p->fft_n;
    for (int k = 0; k < p->nseg; k++) {
        const sdr_seg_t *s = &p->seg[k];
        double ta = now_ms();
        if (s->lo_hz != e->cur_lo || p->rate != e->cur_rate || p->req.gain != e->cur_gain) {
            rfsrc_set_t set = {.center_hz = s->lo_hz, .rate = p->rate, .gain = p->req.gain};
            int64_t got = s->lo_hz;
            int r = o->tune(e->src.ctx, &set, &got);
            if (r < 0) {
                // (one centre that does not lock, for example at the edge of the PLL range: only the points of this
                // segment are empty; the frame goes on. The error counts only when no segment worked.)
                for (int i = s->i0; i < s->i1; i++) {
                    out[i] = SDR_NODATA_SPEC;
                }
                info->err = r;
                info->bad_seg++;
                e->cur_lo = 0;
                continue;
            }
            e->cur_lo = got;
            e->cur_rate = p->rate;
            e->cur_gain = p->req.gain;
        }
        double tb = now_ms();
        rfsrc_meta_t m = {0};
        int r = o->read_iq(e->src.ctx, e->iq, p->pairs, &m);
        if (r < 0) {
            info->err = r;
            return;
        }
        if (m.overload) {
            info->overload = true;
        }
        e->iq_n = r;
        e->iq_lo = e->cur_lo;
        e->iq_rate = (uint32_t)fs;
        double tc = now_ms();
        block_power(e, p, r, (float)e->src.full_scale);
        lo_hole(e, c, bin_hz, p->fft_n);
        double td = now_ms();
        const float gdb = p->req.gain < c->ngains ? c->gain_db[p->req.gain] : 0.0f;
        map_points(e, p, s, e->cur_lo, bin_hz, c->full_scale_db - gdb, out);
        double te = now_ms();
        info->ms_tune += (float)(tb - ta);
        info->ms_read += (float)(tc - tb);
        info->ms_fft += (float)(td - tc);
        info->ms_map += (float)(te - td);
    }
    if (info->bad_seg < p->nseg) {
        info->err = 0;
    }
    info->ms = (float)(now_ms() - t0);
}
