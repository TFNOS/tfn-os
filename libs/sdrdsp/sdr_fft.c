// SDRDSP: the FFT (radix 2, decimation in time, the tables made once) and the windows.
#include "sdr_fft.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

void *sdr_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
#else
    return calloc(1, n);
#endif
}

void sdr_free(void *p)
{
    free(p);
}

bool sdr_fft_init(sdr_fft_t *f, int n)
{
    memset(f, 0, sizeof(*f));
    int lg = 0;
    while ((1 << lg) < n) {
        lg++;
    }
    if (n < 16 || n > 16384 || (1 << lg) != n) {
        return false;
    }
    f->tw = sdr_alloc(sizeof(float) * (size_t)n);
    f->rev = sdr_alloc(sizeof(uint16_t) * (size_t)n);
    if (!f->tw || !f->rev) {
        sdr_fft_free(f);
        return false;
    }
    f->n = n;
    f->log2n = lg;
    for (int k = 0; k < n / 2; k++) {
        double a = -2.0 * M_PI * k / n;
        f->tw[2 * k] = (float)cos(a);
        f->tw[2 * k + 1] = (float)sin(a);
    }
    for (int i = 0; i < n; i++) {
        unsigned r = 0;
        for (int b = 0; b < lg; b++) {
            if (i & (1 << b)) {
                r |= 1u << (lg - 1 - b);
            }
        }
        f->rev[i] = (uint16_t)r;
    }
    return true;
}

void sdr_fft_free(sdr_fft_t *f)
{
    sdr_free(f->tw);
    sdr_free(f->rev);
    memset(f, 0, sizeof(*f));
}

void sdr_fft(const sdr_fft_t *f, float *x)
{
    const int n = f->n;
    for (int i = 0; i < n; i++) {
        int j = f->rev[i];
        if (i < j) {
            float t = x[2 * i];
            x[2 * i] = x[2 * j];
            x[2 * j] = t;
            t = x[2 * i + 1];
            x[2 * i + 1] = x[2 * j + 1];
            x[2 * j + 1] = t;
        }
    }
    // the first two stages need no multiply: a radix-4 butterfly of each 4 points
    for (int i = 0; i < n; i += 4) {
        float *p = x + 2 * i;
        float ar = p[0] + p[2], ai = p[1] + p[3], br = p[0] - p[2], bi = p[1] - p[3];
        float cr = p[4] + p[6], ci = p[5] + p[7], dr = p[4] - p[6], di = p[5] - p[7];
        p[0] = ar + cr;
        p[1] = ai + ci;
        p[4] = ar - cr;
        p[5] = ai - ci;
        p[2] = br + di;
        p[3] = bi - dr;  // b + (-j) d
        p[6] = br - di;
        p[7] = bi + dr;
    }
    for (int len = 8; len <= n; len <<= 1) {
        const int half = len >> 1, step = n / len;
        for (int i = 0; i < n; i += len) {
            float *a = x + 2 * i, *b = x + 2 * (i + half);
            const float *w = f->tw;
            for (int k = 0; k < half; k++, w += 2 * step) {
                float wr = w[0], wi = w[1];
                float br = b[2 * k], bi = b[2 * k + 1];
                float tr = br * wr - bi * wi, ti = br * wi + bi * wr;
                float ar = a[2 * k], ai = a[2 * k + 1];
                b[2 * k] = ar - tr;
                b[2 * k + 1] = ai - ti;
                a[2 * k] = ar + tr;
                a[2 * k + 1] = ai + ti;
            }
        }
    }
}

void sdr_window(int type, int n, float *w, float *cg, float *enbw)
{
    double s = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        double t = 2.0 * M_PI * i / n, v;
        switch (type) {
        case SDR_WIN_BH4:  // Blackman-Harris, 4 terms: side lobes -92 dB
            v = 0.35875 - 0.48829 * cos(t) + 0.14128 * cos(2 * t) - 0.01168 * cos(3 * t);
            break;
        case SDR_WIN_FLAT:  // flat top: the level of a tone is right to 0.02 dB
            v = 0.21557895 - 0.41663158 * cos(t) + 0.277263158 * cos(2 * t) - 0.083578947 * cos(3 * t) +
                0.006947368 * cos(4 * t);
            break;
        case SDR_WIN_RECT:
            v = 1.0;
            break;
        default:  // Hann
            v = 0.5 - 0.5 * cos(t);
            break;
        }
        w[i] = (float)v;
        s += v;
        s2 += v * v;
    }
    if (cg) {
        *cg = (float)(s / n);
    }
    if (enbw) {
        *enbw = (float)(n * s2 / (s * s));
    }
}
