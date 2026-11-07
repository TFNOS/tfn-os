// RFSRC: the DEMO sources: made-up signals with a virtual clock (the same blocks on each run: the PC tests).
//   rfsrc_demo        an IQ source of the 2.4 GHz band: three WiFi networks with bursts, a Bluetooth LE advertiser
//                     that hops on 37/38/39, a steady carrier, a microwave oven that comes and goes, a spur, noise
//   rfsrc_demo_sweep  a SWEEP source of the 433 MHz band (the kind of a CC1101): a remote key, a sensor, a LoRa
//                     channel, levels in dBm
// The IQ demo makes each block in the FREQUENCY domain: each signal that is on puts random values with the shape of
// its spectrum (the steep mask of WiFi, the bell of Bluetooth) into the bins, then one inverse FFT gives the samples
// (sdr_fft.h: a leaf of the math that knows no source). The tones are added in time (an exact frequency).
#include "rfsrc.h"

#include <math.h>
#include <stdlib.h>
#include "sdr_fft.h"

#define MHZ      1000000LL
#define FS_COUNT 512  // the full scale of the demo samples (as the native radio: 10 bits)

enum {
    K_BAND,
    K_TONE,
    K_HOP,
    K_OVEN
};

typedef struct {
    uint8_t kind;
    int64_t f_hz;
    float bw_hz;        // K_BAND / K_HOP / K_OVEN: the width
    float db;           // the level: the power of the signal in dB of the full scale
    float duty;         // the share of the time slots that have the signal
    float slot_s;       // the length of a slot (the bursts)
    float on_s, off_s;  // a long cycle: on for on_s, then off for off_s (0 = always in the cycle)
} sig_t;

static const sig_t SIGS[] = {
        {K_BAND, 2412 * MHZ, 16.6e6f, -22, 0.35f, 0.0003f, 0, 0},      // WiFi channel 1, near
        {K_BAND, 2437 * MHZ, 16.6e6f, -28, 0.65f, 0.0002f, 0, 0},      // WiFi channel 6, busy
        {K_BAND, 2462 * MHZ, 16.6e6f, -38, 0.12f, 0.0005f, 0, 0},      // WiFi channel 11, far
        {K_HOP, 2402 * MHZ, 1.0e6f, -30, 0.20f, 0.0006f, 0, 0},        // Bluetooth LE: 37, 38, 39 in turn
        {K_TONE, 2450 * MHZ + 250000, 0, -52, 1.0f, 1.0f, 0, 0},       // a steady carrier
        {K_OVEN, 2455 * MHZ, 4.0e6f, -26, 0.5f, 0.0083f, 6.0f, 9.0f},  // a microwave oven (on for 6 s, off for 9 s)
        {K_TONE, 2440 * MHZ, 0, -66, 1.0f, 1.0f, 0, 0},                // a spur of the clock
};
#define NSIGS ((int)(sizeof(SIGS) / sizeof(SIGS[0])))

static const rfsrc_caps_t CAPS = {
        .id = "demo",
        .name = {"DEMO SIGNALS", "אותות לדוגמה"},
        .kind = RFSRC_IQ,
        .flags = RFSRC_F_DEMO,
        .nbands = 1,
        .band = {{2300 * MHZ, 2600 * MHZ}},
        .nrates = 2,
        .rate_hz = {80000000u, 16000000u},
        .usable_hz = {20000000u, 5000000u},
        .dc_hz = 60000,
        .max_pairs = 16380,
        .ngains = 3,
        .gain_def = 1,
        .gain_name = {{"LOW", "נמוכה"}, {"NORMAL", "רגילה"}, {"HIGH", "גבוהה"}},
        .gain_val = {0, 1, 2},
        .gain_db = {-12, 0, 12},
        .tune_us = 300,
        .full_scale_db = 0,
};

typedef struct {
    bool open;
    int64_t lo;
    int rate, gain;
    double t;  // the virtual clock (s)
    uint32_t rnd;
    float *x;  // the bins, then the samples (2 * 16384)
    sdr_fft_t fft;
} ctx_t;
static ctx_t s_ctx;

static inline uint32_t xs(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static inline float uni(uint32_t *s)
{
    return (float)(xs(s) >> 8) * (1.0f / 16777216.0f);
}
// (a near Gauss of unit variance: the sum of two uniform values, a triangle)
static inline float gauss(uint32_t *s)
{
    return (uni(s) + uni(s) - 1.0f) * 2.4494897f;
}

// A hash of a slot of a signal: the same answer for the same slot on each run
static float slot_rand(int sig, int64_t slot)
{
    uint32_t h = (uint32_t)(slot * 2654435761u) ^ (uint32_t)(sig * 40503u + 977u) ^ (uint32_t)(slot >> 32);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

static bool sig_on(int k, double t)
{
    const sig_t *s = &SIGS[k];
    if (s->on_s > 0) {
        double cyc = fmod(t, (double)(s->on_s + s->off_s));
        if (cyc > s->on_s) {
            return false;
        }
    }
    if (s->duty >= 1.0f) {
        return true;
    }
    if (s->kind == K_OVEN) {
        return fmod(t, (double)s->slot_s * 2) < s->slot_s;  // the half wave of the mains
    }
    return slot_rand(k, (int64_t)floor(t / s->slot_s)) < s->duty;
}

// The shape of a signal at the offset d (Hz) from its middle: 1 in the band, 0 out of it
static float shape(const sig_t *s, double d)
{
    double a = fabs(d), h = s->bw_hz / 2;
    if (s->kind == K_BAND) {
        // the WiFi mask: flat to 0.9 of the half width, a cosine edge to 1.1, then a skirt 40 dB down that falls
        if (a < 0.9 * h) {
            return 1;
        }
        if (a < 1.1 * h) {
            return (float)(0.5 + 0.5 * cos(M_PI * (a - 0.9 * h) / (0.2 * h)));
        }
        return a < 2.4 * h ? 1e-4f * (float)(2.4 - a / h) : 0;
    }
    double z = d / (0.42 * s->bw_hz);  // Bluetooth and the oven: a bell
    return (float)exp(-0.5 * z * z);
}

static int d_state(void *c)
{
    (void)c;
    return RFSRC_READY;
}
static void d_claim(void *c, const char *app)
{
    (void)c;
    (void)app;
}
static void d_unclaim(void *c)
{
    (void)c;
}

static int d_open(void *c, const rfsrc_set_t *s)
{
    ctx_t *x = c;
    if (!x->x) {
        x->x = sdr_alloc(sizeof(float) * 2 * 16384);
    }
    if (!x->x) {
        return -1;
    }
    x->open = true;
    x->lo = s->center_hz;
    x->rate = s->rate;
    x->gain = s->gain;
    x->t = 0;
    x->rnd = 0x1234567u;
    return 0;
}

static int d_tune(void *c, const rfsrc_set_t *s, int64_t *center)
{
    ctx_t *x = c;
    x->lo = s->center_hz;
    x->rate = s->rate < CAPS.nrates ? s->rate : 0;
    x->gain = s->gain;
    if (center) {
        *center = x->lo;
    }
    return 0;
}

static int d_read(void *c, int16_t *iq, int n, rfsrc_meta_t *m)
{
    ctx_t *x = c;
    if (!x->open) {
        return -1;
    }
    if (n > CAPS.max_pairs) {
        n = CAPS.max_pairs;
    }
    int N = 64;
    while (N < n) {
        N <<= 1;
    }
    if (x->fft.n != N && (sdr_fft_free(&x->fft), !sdr_fft_init(&x->fft, N))) {
        return -1;
    }
    const double fs = CAPS.rate_hz[x->rate], bin = fs / N;
    const float gain = powf(10.0f, (x->gain - 1) * 12.0f / 20.0f);  // 12 dB a step
    float *X = x->x;
    // THE BINS. The variance of a bin = the power that it carries (the inverse FFT has no 1/N: a sample then has the
    // sum of the variances). The noise of the receiver: about 3.5 counts of 512 (it grows less than the signals).
    const float nrms = (2.0f + 1.5f * gain) / FS_COUNT;
    const float nb = nrms * sqrtf(1.0f / N) * 0.7071f;
    for (int k = 0; k < 2 * N; k++) {
        X[k] = gauss(&x->rnd) * nb;
    }
    const double tmid = x->t + 0.5 * n / fs;
    for (int g = 0; g < NSIGS; g++) {
        const sig_t *s = &SIGS[g];
        if (s->kind == K_TONE || !sig_on(g, tmid)) {
            continue;
        }
        double f = (double)s->f_hz;
        if (s->kind == K_HOP) {  // 37, 38, 39 one after the other in each event (each 0.4 ms)
            static const int64_t HOP[3] = {2402 * MHZ, 2426 * MHZ, 2480 * MHZ};
            f = (double)HOP[(int64_t)floor(tmid / 0.0004) % 3];
        }
        if (s->kind == K_OVEN) {
            f += 5e6 * sin(2 * M_PI * tmid / 0.02);  // the magnetron sweeps
        }
        const double off = f - (double)x->lo;
        // the bins of the signal (the analog filter of a real receiver: nothing from outside 0.45 of the rate)
        const double reach = 2.4 * s->bw_hz / 2 + 4 * bin;
        int k0 = (int)floor((off - reach) / bin), k1 = (int)ceil((off + reach) / bin);
        const int kl = -(int)(0.45 * N), kh = (int)(0.45 * N);
        if (k0 < kl) {
            k0 = kl;
        }
        if (k1 > kh) {
            k1 = kh;
        }
        if (k0 > k1) {
            continue;
        }
        // the power of the signal over its bins: P = 10^(db/10) = the sum of shape^2 * a^2
        double ssum = 0;
        for (int k = (int)floor(-reach / bin); k <= (int)ceil(reach / bin); k++) {
            float sh = shape(s, k * bin);
            ssum += (double)sh * sh;
        }
        if (ssum <= 0) {
            continue;
        }
        const float a = (float)sqrt(pow(10.0, s->db / 10.0) / ssum) * gain * 0.7071f;
        for (int k = k0; k <= k1; k++) {
            float sh = shape(s, k * bin - off) * a;
            if (sh <= 0) {
                continue;
            }
            int j = k & (N - 1);  // (a negative frequency: the top half of the bins)
            X[2 * j] += gauss(&x->rnd) * sh;
            X[2 * j + 1] += gauss(&x->rnd) * sh;
        }
    }
    // the inverse FFT: conj(FFT(conj(X)))
    for (int k = 0; k < N; k++) {
        X[2 * k + 1] = -X[2 * k + 1];
    }
    sdr_fft(&x->fft, X);
    for (int k = 0; k < N; k++) {
        X[2 * k + 1] = -X[2 * k + 1];
    }
    // the tones: in time, at their exact frequency
    for (int g = 0; g < NSIGS; g++) {
        const sig_t *s = &SIGS[g];
        if (s->kind != K_TONE || !sig_on(g, tmid)) {
            continue;
        }
        const double off = (double)(s->f_hz - x->lo);
        if (fabs(off) > 0.45 * fs) {
            continue;
        }
        const float a = sqrtf(powf(10.0f, s->db / 10.0f)) * gain;
        const double dph = 2 * M_PI * off / fs;
        double ph = fmod(x->t * off * 2 * M_PI, 2 * M_PI);
        float pr = a * cosf((float)ph), pi = a * sinf((float)ph);
        const float rr = cosf((float)dph), ri = sinf((float)dph);
        for (int k = 0; k < n; k++) {
            X[2 * k] += pr;
            X[2 * k + 1] += pi;
            float t = pr * rr - pi * ri;
            pi = pr * ri + pi * rr;
            pr = t;
        }
    }
    bool over = false;
    for (int k = 0; k < 2 * n; k++) {
        float v = X[k] * FS_COUNT;
        if (v > 511) {
            v = 511;
            over = true;
        }
        if (v < -512) {
            v = -512;
            over = true;
        }
        iq[k] = (int16_t)lrintf(v);
    }
    if (m) {
        m->center_hz = x->lo;
        m->rate_hz = (uint32_t)fs;
        m->overload = over;
    }
    x->t += 0.006 + 0.004 * uni(&x->rnd);  // (the next block: 6..10 ms later; not a fixed step that meets the
                                           // hops of Bluetooth at the same place each time)
    return n;
}

static int d_none_sweep(void *c, int64_t f0, uint32_t step, int n, float *db)
{
    (void)c;
    (void)f0;
    (void)step;
    (void)n;
    (void)db;
    return -1;
}

static void d_close(void *c)
{
    ctx_t *x = c;
    x->open = false;
    sdr_fft_free(&x->fft);
    sdr_free(x->x);
    x->x = NULL;
}

static const char *d_error(int e)
{
    return e < 0 ? "the demo failed" : "ok";
}

static const rfsrc_ops_t OPS = {d_state, d_claim, d_unclaim, d_open, d_tune, d_read, d_none_sweep, d_close, d_error};

bool rfsrc_demo(rfsrc_t *out)
{
    out->caps = &CAPS;
    out->ops = &OPS;
    out->ctx = &s_ctx;
    out->full_scale = FS_COUNT;
    return true;
}

// ---------------------------------------------------------------- the SWEEP demo (433 MHz)

typedef struct {
    int64_t f_hz;
    float bw_hz, dbm, duty;
} psig_t;

static const psig_t PSIGS[] = {
        {433920000LL, 40000, -58, 0.18f},   // a remote key
        {433420000LL, 30000, -74, 0.06f},   // a weather sensor
        {434500000LL, 125000, -88, 0.40f},  // a LoRa channel
        {435100000LL, 2000, -96, 1.0f},     // a steady weak carrier
};

static const rfsrc_caps_t PCAPS = {
        .id = "demo433",
        .name = {"DEMO 433 MHZ (SWEEP)", "433 מגה לדוגמה (סריקה)"},
        .kind = RFSRC_SWEEP,
        .flags = RFSRC_F_DEMO | RFSRC_F_DBM,
        .nbands = 1,
        .band = {{420 * MHZ, 450 * MHZ}},
        .step_hz = 5000,
        .point_bw_hz = 58000,
        .max_points = 1024,
        .ngains = 1,
        .gain_def = 0,
        .gain_name = {{"FIXED", "קבועה"}},
        .gain_val = {0},
        .tune_us = 40,
        .full_scale_db = -10,
};

static ctx_t s_pctx;

static int p_open(void *c, const rfsrc_set_t *s)
{
    ctx_t *x = c;
    (void)s;
    x->open = true;
    x->t = 0;
    x->rnd = 0x9E3779B9u;
    return 0;
}

static int p_tune(void *c, const rfsrc_set_t *s, int64_t *center)
{
    (void)c;
    if (center) {
        *center = s->center_hz;
    }
    return 0;
}

static int p_none_iq(void *c, int16_t *iq, int n, rfsrc_meta_t *m)
{
    (void)c;
    (void)iq;
    (void)n;
    (void)m;
    return -1;
}

static int p_sweep(void *c, int64_t f0, uint32_t step, int n, float *db)
{
    ctx_t *x = c;
    if (!x->open) {
        return -1;
    }
    if (n > (int)PCAPS.max_points) {
        n = (int)PCAPS.max_points;
    }
    const int ns = (int)(sizeof(PSIGS) / sizeof(PSIGS[0]));
    bool on[8];
    for (int g = 0; g < ns; g++) {
        on[g] = PSIGS[g].duty >= 1 || slot_rand(100 + g, (int64_t)floor(x->t / 0.05)) < PSIGS[g].duty;
    }
    for (int i = 0; i < n; i++) {
        double f = (double)f0 + (double)i * step;
        double p = pow(10.0, (-108.0 + gauss(&x->rnd) * 1.2) / 10.0);
        for (int g = 0; g < ns; g++) {
            if (!on[g]) {
                continue;
            }
            // the channel filter of the point (58 kHz) over the signal: a Gauss of the two widths
            double w = sqrt((double)PSIGS[g].bw_hz * PSIGS[g].bw_hz + 58000.0 * 58000.0) / 2.355;
            double d = (f - (double)PSIGS[g].f_hz) / w;
            p += pow(10.0, PSIGS[g].dbm / 10.0) * exp(-0.5 * d * d);
        }
        db[i] = (float)(10.0 * log10(p));
    }
    x->t += 0.033;
    return n;
}

static void p_close(void *c)
{
    ((ctx_t *)c)->open = false;
}

static const rfsrc_ops_t POPS = {d_state, d_claim, d_unclaim, p_open, p_tune, p_none_iq, p_sweep, p_close, d_error};

bool rfsrc_demo_sweep(rfsrc_t *out)
{
    out->caps = &PCAPS;
    out->ops = &POPS;
    out->ctx = &s_pctx;
    out->full_scale = 0;
    return true;
}
