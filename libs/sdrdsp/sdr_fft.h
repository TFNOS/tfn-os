#pragma once
// SDRDSP: the FFT and the windows (pure C: no radio, no screen; the PC tests use the same code).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The memory of sdrdsp: PSRAM on the board (the internal RAM is small in a radio start), calloc on the PC
void *sdr_alloc(size_t n);
void sdr_free(void *p);

typedef struct {
    int n, log2n;
    float *tw;      // n / 2 twiddles: cos, sin (the forward sign)
    uint16_t *rev;  // the bit reversal of each index
} sdr_fft_t;

// n: a power of 2, 16 .. 16384. False when there is no memory.
bool sdr_fft_init(sdr_fft_t *f, int n);
void sdr_fft_free(sdr_fft_t *f);
// The forward FFT in place: x = n complex values (re, im, re, im, ...)
void sdr_fft(const sdr_fft_t *f, float *x);

enum {
    SDR_WIN_HANN,
    SDR_WIN_BH4,
    SDR_WIN_FLAT,
    SDR_WIN_RECT,
    SDR_WIN_N
};
// The window of n points into w; *cg = the coherent gain (the mean of w), *enbw = the noise bandwidth in bins
void sdr_window(int type, int n, float *w, float *cg, float *enbw);
