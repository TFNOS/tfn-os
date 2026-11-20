// TUNER (UTIL): a chromatic tuner with the microphone.
// The task: the app task of TFN OS calls the hooks of app_tuner; the microphone task calls mic_cb (the ring).
// The memory: the file statics; the ring and the YIN buffers (PSRAM) come at enter and go at leave.
//
// - The microphone blocks (24 kHz, audio_mic_tap) go into a ring. Each 40 ms the YIN method finds the period in the
//   last 85 ms at 12 kHz (two samples -> one: 37 Hz .. 2 kHz; a check for the half and the third of the period stops
//   octave errors), then a search of the raw difference at 24 kHz around it gives the exact period (about 1 cent).
//   The median of the last 5 values and a level gate stop the jumps. The note and the cents come from A4 (430 ..
//   450 Hz, saved).
// - The look: a wide arc with a needle (+-50 cents; green in the middle), a strobe band that moves to the flat or
//   the sharp side, the note letter drawn with thick strokes, the frequency, and a trail of the last 4 s.
// - Instruments: CHROMATIC, GUITAR, BASS, UKULELE, VIOLIN. With an instrument the strings are buttons: a tap plays
//   the string note (2 s) and makes it the target; else the nearest string is the target.
// - A tap on the note card plays the reference tone of the note (a tap again stops it). While a reference plays,
//   the tuner does not listen (it would hear the speaker).
// - The round panel of the Watcher has its own layout and the wheel (see "the round layout" below).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "strokefont/strokefont.h"

#define FS      12000  // the coarse analysis rate (the microphone rate / 2)
#define FS2     24000  // the microphone rate (the fine search)
#define RING    8192   // samples at FS2 (0.34 s)
#define WIN     1024   // the YIN window at FS (2048 samples at FS2)
#define TAU_MIN 6      // 2 kHz
#define TAU_MAX 320    // 37.5 Hz
#define YIN_THR 0.13f
#define HIST_N  100  // the trail: one value each 40 ms (4 s)

#if BOARD_BAR_SCREEN
#define SCR_W 640
#define SCR_H 172
#elif BOARD_UI_TABLET
#define SCR_W 640  // (the Tab5: the 640 x 360 frame; a bigger card, meter and buttons)
#define SCR_H 360
#else
#define SCR_W 320
#define SCR_H 240
#endif

enum {
    I_CHROM,
    I_GUITAR,
    I_BASS,
    I_UKE,
    I_VIOLIN,
    N_INST
};
static const char *const INST_NAME[N_INST] = {"CHROMATIC", "GUITAR", "BASS", "UKULELE", "VIOLIN"};
static const uint8_t INST_N[N_INST] = {0, 6, 4, 4, 4};
static const uint8_t INST_NOTES[N_INST][6] = {
        {0}, {40, 45, 50, 55, 59, 64}, {28, 33, 38, 43}, {67, 60, 64, 69}, {55, 62, 69, 76},
};
static const char *const NOTE_L[12] = {"C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"};
static const bool NOTE_SHARP[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};

// the ring (PSRAM) and its write index (the microphone task writes, the app reads)
static int16_t *s_ring;
static float *s_x24, *s_x12, *s_d;  // the work buffers of the analysis (PSRAM)
static volatile uint32_t s_wpos;
static volatile int s_level;  // the peak of the last block (absolute value)

static int s_inst, s_a4 = 440, s_target = -1;  // s_target: a string index, -1 = the nearest
static float s_hz;                             // the detected pitch (0 = none)
static float s_med[5];
static int s_nmed;
static float s_note_f;   // the note as a float MIDI number (with the A4 reference)
static int s_note = -1;  // the shown note (MIDI), -1 = none
static float s_cents, s_needle, s_needle_v;
static int64_t s_heard_at, s_intune_since, s_last_an, s_ref_until, s_now;
static int s_ref_note = -1;
static float s_strobe;
static int8_t s_hist[HIST_N];  // cents, 127 = no note
static int s_hist_w;
static float s_glow;

// ---------------------------------------------------------------- the microphone

static void mic_cb(const int16_t *s, int n)
{
    if (!s_ring) {
        return;
    }
    uint32_t w = s_wpos;
    int peak = 0;
    for (int i = 0; i < n; i++) {
        s_ring[w & (RING - 1)] = s[i];
        w++;
        if (abs(s[i]) > peak) {
            peak = abs(s[i]);
        }
    }
    s_wpos = w;
    s_level = peak;
}

// YIN in x[0..n): the first dip of the cumulative mean normalized difference under thr between tmin and tmax
// (then the octave check), or 0. d: a work buffer of tmax + 2 values. *conf = 1 - the dip.
static float yin(const float *x, int n, int tmin, int tmax, float thr, float *d, float *conf)
{
    const int W = n - tmax - 1;
    for (int tau = 1; tau <= tmax + 1; tau++) {
        float sum = 0;
        for (int j = 0; j < W; j++) {
            float e = x[j] - x[j + tau];
            sum += e * e;
        }
        d[tau] = sum;
    }
    float run = 0;
    d[0] = 1;
    for (int tau = 1; tau <= tmax + 1; tau++) {
        run += d[tau];
        d[tau] = run > 0 ? d[tau] * tau / run : 1;
    }
    int t = -1;
    for (int tau = tmin; tau <= tmax; tau++) {
        if (d[tau] < thr) {
            while (tau + 1 <= tmax && d[tau + 1] < d[tau]) {
                tau++;
            }
            t = tau;
            break;
        }
    }
    *conf = 0;
    if (t < 0) {
        return 0;
    }
    // the octave check: a high note whose period is between two samples can miss the threshold at its own period
    // and take two periods; a dip at the half (or the third) of t is then the true period
    for (int k = 3; k >= 2; k--) {
        int c = (int)lroundf((float)t / k);
        if (c - 1 < tmin) {
            continue;
        }
        int best = c;
        for (int j = c - 1; j <= c + 1; j++) {
            if (d[j] < d[best]) {
                best = j;
            }
        }
        if (d[best] < 0.35f && d[best] < d[best - 1] && d[best] <= d[best + 1]) {
            t = best;
            break;
        }
    }
    *conf = 1 - d[t];
    return (float)t;
}

// the raw difference of x24 at the lags lo..hi (W samples each) and a parabola at the minimum; -1 = no minimum inside
static float dip(const float *x, int n, int lo, int hi)
{
    float d[16];
    if (hi - lo + 1 > 16) {
        hi = lo + 15;
    }
    if (lo < 2) {
        lo = 2;
    }
    int W = n - hi - 1;
    if (W > 1300) {
        W = 1300;
    }
    int m = hi - lo + 1, best = 0;
    for (int i = 0; i < m; i++) {
        float sum = 0;
        const float *p = x + (n - W - hi - 1), *q = p + lo + i;
        for (int j = 0; j < W; j++) {
            float e = p[j] - q[j];
            sum += e * e;
        }
        d[i] = sum;
        if (sum < d[best]) {
            best = i;
        }
    }
    if (best == 0 || best == m - 1) {
        return -1;
    }
    float a = d[best - 1], b = d[best], c = d[best + 1], den = a - 2 * b + c;
    float off = den > 1e-6f ? 0.5f * (a - c) / den : 0;
    return lo + best + (off > 0.5f ? 0.5f : off < -0.5f ? -0.5f : off);
}

// the exact period at 24 kHz from a first value t: over 1 period, then 4, then as many as fit in 700 samples
static float refine(const float *x, int n, float t)
{
    static const int KS[3] = {1, 4, 0};
    for (int s = 0; s < 3; s++) {
        int k = KS[s] ? KS[s] : (int)(700 / t);
        if (k < 1 || (s > 0 && k <= KS[s - 1] && KS[s])) {
            continue;
        }
        if (s == 2 && k <= 4) {
            break;
        }
        float c = t * k;
        if (c + 4 > n - 600) {
            break;
        }
        int r = s == 0 ? 3 : 2;
        float v = dip(x, n, (int)c - r, (int)c + r + 1);
        if (v < 0) {
            break;
        }
        t = v / k;
    }
    return t;
}

static float median5(void)
{
    float v[5];
    int n = s_nmed < 5 ? s_nmed : 5;
    memcpy(v, s_med, sizeof v);
    for (int i = 1; i < n; i++) {
        float k = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > k) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = k;
    }
    return v[n / 2];
}

static float note_hz(int midi)
{
    return s_a4 * powf(2, (midi - 69) / 12.0f);
}

// the target note now: a string, the nearest string, or the nearest note
static int target_note(void)
{
    if (s_inst == I_CHROM) {
        return (int)lroundf(s_note_f);
    }
    int n = INST_N[s_inst];
    if (s_target >= 0 && s_target < n) {
        return INST_NOTES[s_inst][s_target];
    }
    int best = INST_NOTES[s_inst][0];
    for (int i = 1; i < n; i++) {
        if (fabsf(INST_NOTES[s_inst][i] - s_note_f) < fabsf(best - s_note_f)) {
            best = INST_NOTES[s_inst][i];
        }
    }
    return best;
}

static void analyse(int64_t now)
{
    uint32_t w = s_wpos;
    if (!s_x24 || w < 2 * WIN + 2) {
        return;
    }
    // the last 2048 samples at 24 kHz (mean removed), and a [1 2 1] low-pass copy at 12 kHz
    float mean = 0;
    for (int i = 0; i < 2 * WIN; i++) {
        s_x24[i] = s_ring[(w - 2 * WIN + i) & (RING - 1)];
        mean += s_x24[i];
    }
    mean /= 2 * WIN;
    float rms = 0;
    for (int i = 0; i < 2 * WIN; i++) {
        s_x24[i] -= mean;
        rms += s_x24[i] * s_x24[i];
    }
    rms = sqrtf(rms / (2 * WIN));
    if (rms < 70) {
        if (now - s_heard_at > 600) {
            s_nmed = 0;
            s_hz = 0;
        }
        return;
    }
    float conf = 0;
    // the high range first (400 .. 2400 Hz) at 24 kHz, on the last 1400 samples
    float t = yin(s_x24 + 2 * WIN - 1400, 1400, 10, 60, 0.12f, s_d, &conf);
    if (t <= 0 || conf < 0.8f) {
        for (int i = 0; i < WIN; i++) {
            int k = 2 * i;
            float pv = k > 0 ? s_x24[k - 1] : s_x24[k], nx = s_x24[k + 1];
            s_x12[i] = (pv + 2 * s_x24[k] + nx) * 0.25f;
        }
        t = yin(s_x12, WIN, TAU_MIN, TAU_MAX, YIN_THR, s_d, &conf) * 2;
    }
    if (t <= 0 || conf < 0.8f) {
        if (now - s_heard_at > 600) {
            s_nmed = 0;
            s_hz = 0;
        }
        return;
    }
    float hz = (float)FS2 / refine(s_x24, 2 * WIN, t);
    // a jump of more than 2 semitones restarts the median (a new note)
    if (s_nmed && fabsf(12 * log2f(hz / median5())) > 2) {
        s_nmed = 0;
    }
    memmove(s_med + 1, s_med, sizeof(s_med) - sizeof(s_med[0]));
    s_med[0] = hz;
    if (s_nmed < 5) {
        s_nmed++;
    }
    if (s_nmed < 2) {
        return;
    }
    s_hz = median5();
    s_note_f = 69 + 12 * log2f(s_hz / s_a4);
    s_heard_at = now;
}

// ---------------------------------------------------------------- the reference tone

static void ref_on(int midi, int ms)
{
    s_ref_note = midi;
    s_ref_until = s_now + ms;
    audio_drone(0, note_hz(midi), 45, W_TRI);
}

static void ref_off(void)
{
    if (s_ref_note >= 0) {
        audio_drone(0, note_hz(s_ref_note), 0, W_TRI);
    }
    s_ref_note = -1;
    s_ref_until = 0;
}

// a tap on the note: the reference tone of the note on or off
static void ref_toggle(void)
{
    if (s_ref_note >= 0) {
        ref_off();
    } else {
        ref_on(s_inst != I_CHROM ? (s_note >= 0 ? target_note() : INST_NOTES[s_inst][0]) : (s_note >= 0 ? s_note : 69),
               4000);
    }
}

// a tap on string i: it plays the string note and makes it the target (a second tap: back to the nearest string)
static void string_tap(int i)
{
    if (s_target == i && s_ref_note >= 0) {
        s_target = -1;
        ref_off();
    } else {
        s_target = i;
        ref_on(INST_NOTES[s_inst][i], 2000);
    }
}

// ---------------------------------------------------------------- the layout

typedef struct {
    int x, y, w, h;
} box_t;
#if BOARD_BAR_SCREEN
static const box_t CARD = {8, 8, 164, 156};
static const box_t METER = {184, 6, 336, 112};
static const box_t ROWB = {184, 124, 336, 42};
static const box_t INST = {530, 8, 102, 46};
static const box_t A4M = {530, 106, 42, 48}, A4P = {590, 106, 42, 48};
#elif BOARD_UI_TABLET
static const box_t CARD = {10, 8, 220, 170};
static const box_t METER = {10, 182, 620, 118};
static const box_t ROWB = {10, 304, 620, 52};
static const box_t INST = {242, 8, 388, 54};
static const box_t A4M = {242, 110, 72, 64}, A4P = {558, 110, 72, 64};
#else
static const box_t CARD = {6, 6, 120, 118};
static const box_t METER = {6, 128, 308, 58};
static const box_t ROWB = {6, 190, 308, 46};  // (for the thumb: a hit row of 58 px = 6.4 mm on the AMOLED)
static const box_t INST = {134, 6, 180, 40};
static const box_t A4M = {134, 82, 44, 40}, A4P = {270, 82, 44, 40};
#endif

static bool in_box(const box_t *b, int x, int y, int pad)
{
    return x >= b->x - pad && x < b->x + b->w + pad && y >= b->y - pad && y < b->y + b->h + pad;
}

static int str_btn_w(void)
{
    return ROWB.w / (INST_N[s_inst] ? INST_N[s_inst] : 1);
}

// ---------------------------------------------------------------- drawing helpers

static uint16_t mix(uint16_t a, uint16_t b, int k)  // k 0..255: a -> b (gfx colours)
{
    a = (uint16_t)((a >> 8) | (a << 8));
    b = (uint16_t)((b >> 8) | (b << 8));
    int r = ((a >> 11) * (255 - k) + (b >> 11) * k) / 255,
        g = (((a >> 5) & 63) * (255 - k) + ((b >> 5) & 63) * k) / 255;
    int bl = ((a & 31) * (255 - k) + (b & 31) * k) / 255;
    uint16_t c = (uint16_t)((r << 11) | (g << 5) | bl);
    return (uint16_t)((c >> 8) | (c << 8));
}

static void card_bg(const box_t *b, uint16_t c, uint16_t edge)
{
    gfx_fill_rect(b->x + 3, b->y, b->w - 6, b->h, c);
    gfx_fill_rect(b->x, b->y + 3, b->w, b->h - 6, c);
    gfx_fill_circle(b->x + 3, b->y + 3, 3, c);
    gfx_fill_circle(b->x + b->w - 4, b->y + 3, 3, c);
    gfx_fill_circle(b->x + 3, b->y + b->h - 4, 3, c);
    gfx_fill_circle(b->x + b->w - 4, b->y + b->h - 4, 3, c);
    gfx_fill_rect(b->x + 4, b->y, b->w - 8, 1, edge);
}

static void button(const box_t *b, const char *t, bool on)
{
    card_bg(b, on ? rgb(40, 120, 80) : rgb(30, 38, 66), on ? rgb(120, 240, 170) : rgb(70, 84, 130));
    const mfont_t *f = mf_width(&MF_N16, t, 1) <= b->w - 8 ? &MF_N16 : &MF_SMALL;
    mf_text_c(f, b->x + b->w / 2, b->y + b->h / 2 + (f == &MF_N16 ? 6 : 4), t, rgb(235, 240, 255), 255, 1);
}

// ---------------------------------------------------------------- the meter

static uint16_t cents_col(float c)
{
    float a = fabsf(c);
    if (a < 3) {
        return rgb(70, 240, 140);
    }
    if (a < 10) {
        return mix(rgb(70, 240, 140), rgb(250, 220, 80), (int)((a - 3) * 255 / 7));
    }
    return c < 0 ? rgb(90, 170, 255) : rgb(255, 150, 70);
}

static void draw_meter(int64_t now)
{
    const box_t *m = &METER;
    float cx = m->x + m->w / 2.0f;
#if BOARD_BAR_SCREEN
    float R = 330, cy = m->y + 26 + R, deg = 26;  // a wide flat arc; the pivot is under the screen
#elif BOARD_UI_TABLET
    float R = 700, cy = m->y + 18 + R, deg = 24;
#else
    float R = 300, cy = m->y + 18 + R, deg = 26;
#endif
    bool live = s_note >= 0 && s_ref_note < 0;
    // the green zone and the ticks
    for (int c = -50; c <= 50; c += 1) {
        float a = c / 50.0f * deg * 3.14159f / 180;
        float sx = fsin(a), cy0 = -fcos(a);
        bool big = c % 10 == 0, mid = c % 5 == 0;
        if (!big && !mid && abs(c) > 5) {
            continue;
        }
        float r0 = R + (big ? 0 : 6), r1 = R + (big ? 16 : 11);
        uint16_t col = abs(c) <= 5 ? rgb(60, 200, 120) : (big ? rgb(150, 160, 200) : rgb(80, 90, 130));
        stroke_line(cx + sx * r0, cy + cy0 * r0, cx + sx * r1, cy + cy0 * r1, big ? 1.2f : 0.6f, col);
    }
    // the labels
    char t[8];
    for (int c = -50; c <= 50; c += 25) {
        float a = c / 50.0f * deg * 3.14159f / 180;
        snprintf(t, sizeof t, c > 0 ? "+%d" : "%d", c);
        mf_text_c(&MF_TINY, (int)(cx + fsin(a) * (R + 26)), (int)(cy - fcos(a) * (R + 26)) + 2, t, rgb(120, 130, 170),
                  255, 0);
    }
    // the strobe band: stripes that move to the side of the error (they stop in tune)
    int sy = m->y + m->h - 16, sh = 10;
    for (int x = m->x; x < m->x + m->w; x += 2) {
        float u = (x - m->x) * 0.13f + s_strobe;
        bool on = fsin(u) > 0;
        uint16_t c = live ? (on ? mix(rgb(20, 26, 48), cents_col(s_cents), 150) : rgb(20, 26, 48)) : rgb(22, 28, 50);
        gfx_fill_rect(x, sy, 2, sh, c);
    }
    // the needle (only its visible part) with a glow
    float a = s_needle / 50.0f * deg * 3.14159f / 180;
    float sx = fsin(a), sy0 = -fcos(a);
    float r_in = cy - (m->y + m->h - 3), r_out = R + 18;  // (the needle ends at the bottom of the meter)
    uint16_t nc = live ? cents_col(s_cents) : rgb(70, 80, 110);
    if (live) {
        stroke_line(cx + sx * r_in, cy + sy0 * r_in, cx + sx * r_out, cy + sy0 * r_out, 4.5f,
                    mix(rgb(10, 14, 30), nc, 90));
        stroke_line(cx + sx * r_in, cy + sy0 * r_in, cx + sx * r_out, cy + sy0 * r_out, 1.6f, rgb(255, 255, 255));
    }
    gfx_fill_circle((int)(cx + sx * r_out), (int)(cy + sy0 * r_out), 4, nc);
    // the cents number in a pill near the needle tip
    if (live) {
        snprintf(t, sizeof t, "%+d", (int)lroundf(s_cents));
        int px = (int)(cx + sx * (R - 30));
        int pw = mf_width(&MF_N16, t, 1) + 14;
        if (px - pw / 2 < m->x) {
            px = m->x + pw / 2;
        }
        if (px + pw / 2 > m->x + m->w) {
            px = m->x + m->w - pw / 2;
        }
#if BOARD_BAR_SCREEN
        int py = sy - 28;  // (over the strobe band)
#else
        int py = sy - 25;
#endif
        box_t pill = {px - pw / 2, py, pw, 22};
        card_bg(&pill, rgb(16, 20, 40), nc);
        mf_text_c(&MF_N16, px, py + 18, t, nc, 255, 1);
    }
    mf_text(&MF_TINY, m->x + 2, sy + sh + 8, "FLAT", rgb(90, 140, 220), 255, 0);
    mf_text(&MF_TINY, m->x + m->w - mf_width(&MF_TINY, "SHARP", 0) - 2, sy + sh + 8, "SHARP", rgb(230, 140, 80), 255,
            0);
    (void)now;
}

// the string buttons, or the trail of the last 4 s
static void draw_row(void)
{
    const box_t *b = &ROWB;
    if (INST_N[s_inst]) {
        int n = INST_N[s_inst], w = str_btn_w(), tn = s_note >= 0 ? target_note() : -1;
        for (int i = 0; i < n; i++) {
            int note = INST_NOTES[s_inst][i];
            box_t k = {b->x + i * w + 2, b->y, w - 4, b->h};
            bool sel = s_target == i, hot = tn == note && s_note >= 0;
            card_bg(&k, sel ? rgb(60, 50, 110) : (hot ? rgb(28, 60, 50) : rgb(26, 32, 58)),
                    hot ? rgb(90, 230, 150) : rgb(70, 80, 130));
            char t[6];
            snprintf(t, sizeof t, "%s%s%d", NOTE_L[note % 12], NOTE_SHARP[note % 12] ? "#" : "", note / 12 - 1);
            mf_text_c(&MF_N16, k.x + k.w / 2, k.y + k.h / 2 + 6, t, rgb(230, 236, 255), 255, 1);
        }
        return;
    }
    // the trail: one dot each 40 ms, the green band = in tune
    gfx_fill_rect(b->x, b->y + b->h / 2 - 3, b->w, 6, rgb(22, 50, 40));
    gfx_fill_rect(b->x, b->y + b->h / 2, b->w, 1, rgb(50, 110, 80));
    for (int i = 0; i < HIST_N; i++) {
        int v = s_hist[(s_hist_w + i) % HIST_N];
        if (v == 127) {
            continue;
        }
        int x = b->x + i * b->w / HIST_N, y = b->y + b->h / 2 - v * (b->h / 2 - 3) / 50;
        gfx_fill_rect(x, y - 1, 3, 3, cents_col((float)v));
    }
    if (s_note < 0) {
        int tw = mf_width(&MF_SMALL, "PLAY A NOTE", 1) + 16;
        gfx_fill_rect(b->x + (b->w - tw) / 2, b->y + b->h / 2 - 8, tw, 17, rgb(12, 16, 32));
        mf_text_c(&MF_SMALL, b->x + b->w / 2, b->y + b->h / 2 + 5, "PLAY A NOTE", rgb(130, 140, 180), 255, 1);
    }
}

static void draw_card(int64_t now)
{
    const box_t *b = &CARD;
    bool live = s_note >= 0 && s_ref_note < 0;
    bool intune = live && s_intune_since && now - s_intune_since > 350;
    uint16_t bg = mix(rgb(20, 26, 50), rgb(20, 90, 60), (int)(s_glow * 255));
    card_bg(b, bg, intune ? rgb(120, 255, 170) : rgb(70, 84, 130));
    int note = s_ref_note >= 0 ? s_ref_note : s_note;
    if (s_inst != I_CHROM && note >= 0 && s_ref_note < 0) {
        note = target_note();
    }
#if BOARD_BAR_SCREEN
    float h = 82, r = 6, lx = b->x + 22, ly = b->y + 16;
#elif BOARD_UI_TABLET
    float h = 108, r = 8, lx = b->x + 26, ly = b->y + 16;
#else
    float h = 62, r = 4.5f, lx = b->x + 14, ly = b->y + 12;
#endif
    if (note >= 0) {
        uint16_t c = s_ref_note >= 0 ? rgb(255, 210, 120) : (intune ? rgb(140, 255, 190) : rgb(240, 244, 255));
        stroke_char(NOTE_L[note % 12][0], lx, ly, h, r, c);
        if (NOTE_SHARP[note % 12]) {
            stroke_char('#', lx + h * 0.7f, ly, h * 0.5f, r * 0.7f, c);
        }
        char t[12];
        snprintf(t, sizeof t, "%d", note / 12 - 1);
#if BOARD_UI_TABLET
        mf_text(&MF_N34, (int)(lx + h * 0.72f), (int)(ly + h), t, c, 255, 1);
#else
        mf_text(&MF_N24, (int)(lx + h * 0.72f), (int)(ly + h), t, c, 255, 1);
#endif
    } else {
        stroke_line(lx + h * 0.05f, ly + h * 0.5f, lx + h * 0.55f, ly + h * 0.5f, r, rgb(70, 80, 110));
    }
    char f[24];
    if (s_ref_note >= 0) {
        snprintf(f, sizeof f, "REFERENCE");
    } else if (live) {
        snprintf(f, sizeof f, "%.1f HZ", (double)s_hz);
    } else {
        snprintf(f, sizeof f, "TAP FOR A TONE");
    }
    mf_text_c(&MF_SMALL, b->x + b->w / 2, b->y + b->h - 9, f, rgb(170, 180, 220), 255, 1);
    if (intune) {
        int k = (int)((now - s_intune_since) % 900);
        if (k < 500) {
            gfx_ring(b->x + b->w - 18, b->y + 18, 8 + k / 60, 6 + k / 60, rgb(120, 255, 170));
        }
    }
}

#if BOARD_ROUND
// ---------------------------------------------------------------- the round layout (the Watcher: 412 x 412, a wheel)
// The cents scale follows the rim at the top (+-50 cents = +-60 degrees), with the strobe band under it and a marker
// that moves along it. The note letter is big in the middle. The instrument and A4 are round buttons at the left and
// at the right. The arc at the bottom has the strings, or the trail of the last 4 s (CHROMATIC), or the A4 keys.
// The wheel changes the instrument, or A4 when A4 is selected. A push of the wheel plays the reference tone (when A4
// is selected, the push ends the A4 change).
// The frame is the round frame of the panel (APP_ROUND_HI): 412 x 412 on the Watcher, 466 x 466 on the AMOLED-1.75C
// (no wheel: the taps only). The numbers are for 412; RI() and RF scale them (on the Watcher they stay the same).
#define RF    ((float)gfx_w / 412.0f)
#define RI(v) ((int)(v) * gfx_w / 412)
#define RC    (gfx_w / 2)  // the middle of the round frame
#define R_DEG 60.0f        // the half angle of the cents scale (degrees)
#define R_STR RI(150)      // the radius of the arc of the strings and of the trail
#define R_NY  RI(188)      // the middle of the note letter (y)

typedef struct {
    int x, y, r;
} rbtn_t;
#define R_INST ((rbtn_t){RI(66), RC, RI(44)})
#define R_A4   ((rbtn_t){RI(346), RC, RI(44)})
#define R_A4M  ((rbtn_t){RI(118), RI(320), RI(28)})
#define R_A4P  ((rbtn_t){RI(294), RI(320), RI(28)})
static bool s_r_a4;        // true: the wheel changes A4 (else the instrument)
static int64_t s_r_a4_at;  // the time of the last A4 change: the A4 mode ends 6 s after it
static int64_t s_r_save;   // 0, or the time to save the instrument and A4 (after the last wheel step)

// the point at the angle deg (clockwise from the top) and the radius r from the middle
static void r_pol(float deg, float r, float *x, float *y)
{
    float a = deg * 0.0174533f;
    *x = RC + fsin(a) * r;
    *y = RC - fcos(a) * r;
}

static bool r_hit(const rbtn_t *b, int x, int y, int pad)
{
    int dx = x - b->x, dy = y - b->y, r = b->r + pad;
    return dx * dx + dy * dy <= r * r;
}

// the button of string i of n on the bottom arc (the low string at the left)
static rbtn_t r_str_btn(int i, int n)
{
    float x, y;
    r_pol(180 + ((n - 1) * 0.5f - i) * (n > 4 ? 24 : 30), R_STR, &x, &y);
    rbtn_t b = {(int)lroundf(x), (int)lroundf(y), RI(26)};
    return b;
}

// all the rows of the frame: black corners, and the circle with a gradient from top to bot
static void r_bg(uint16_t top, uint16_t bot)
{
    for (int y = 0; y < 2 * RC; y++) {
        float d = y + 0.5f - RC;
        int hw = (int)sqrtf((float)(RC * RC) - d * d);
        gfx_fill_rect(0, y, RC - hw, 1, 0);
        gfx_fill_rect(RC - hw, y, 2 * hw, 1, mix(top, bot, y * 255 / (2 * RC)));
        gfx_fill_rect(RC + hw, y, RC - hw, 1, 0);
    }
}

static void r_button(const rbtn_t *b, uint16_t fill, uint16_t edge, bool sel)
{
    if (sel) {
        gfx_ring(b->x, b->y, b->r + RI(6), b->r + RI(3), rgb(255, 214, 110));
    }
    gfx_fill_circle(b->x, b->y, b->r, fill);
    gfx_ring(b->x, b->y, b->r, b->r - RI(2), edge);
}

static void r_save_now(void)
{
    s_r_save = 0;
    store_set("tn_inst", (uint32_t)s_inst);
    store_set("tn_a4", (uint32_t)s_a4);
}

static void r_a4_add(int d)
{
    s_a4 += d;
    if (s_a4 < 430) {
        s_a4 = 430;
    }
    if (s_a4 > 450) {
        s_a4 = 450;
    }
    s_r_a4_at = s_now;
    s_r_save = s_now + 800;
    sfx_play(SFX_TICK);
}

static void r_inst_add(int d)
{
    s_inst = ((s_inst + d) % N_INST + N_INST) % N_INST;
    s_target = -1;
    s_r_save = s_now + 800;
    sfx_play(SFX_CLICK);
}

// the wheel: each frame
static void r_wheel(int64_t now)
{
    int d = input_wheel();
    if (d && s_r_a4) {
        r_a4_add(d);
    } else if (d) {
        r_inst_add(d > 0 ? 1 : -1);
    }
    for (int n = input_wheel_presses(); n > 0; n--) {
        if (s_r_a4) {
            s_r_a4 = false;
            sfx_play(SFX_CLICK);
        } else {
            ref_toggle();
        }
    }
    if (s_r_a4 && now - s_r_a4_at > 6000) {
        s_r_a4 = false;
    }
    if (s_r_save && now >= s_r_save) {
        r_save_now();
    }
}

static void r_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (r_hit(&R_INST, x, y, 6)) {
        s_r_a4 = false;
        r_inst_add(1);
        return;
    }
    if (r_hit(&R_A4, x, y, 6)) {
        s_r_a4 = !s_r_a4;
        s_r_a4_at = s_now;
        sfx_play(SFX_CLICK);
        return;
    }
    if (s_r_a4 && (r_hit(&R_A4M, x, y, 8) || r_hit(&R_A4P, x, y, 8))) {
        r_a4_add(r_hit(&R_A4P, x, y, 8) ? 1 : -1);
        return;
    }
    for (int i = 0; !s_r_a4 && i < INST_N[s_inst]; i++) {
        rbtn_t k = r_str_btn(i, INST_N[s_inst]);
        if (r_hit(&k, x, y, 5)) {
            string_tap(i);
            return;
        }
    }
    int dx = x - RC, dy = y - R_NY;
    if (dx * dx + dy * dy <= RI(96) * RI(96)) {
        ref_toggle();
    }
}

// the cents scale along the rim, the strobe band, the marker and the cents number
static void r_scale(bool live)
{
    float x0, y0, x1, y1;
    // the green zone (+-5 cents), then the ticks: each 5 cents, long each 10
    for (int a = -6; a <= 6; a++) {
        r_pol((float)a, 190 * RF, &x0, &y0);
        gfx_fill_circle((int)x0, (int)y0, RI(6), live && fabsf(s_cents) < 5 ? rgb(40, 150, 90) : rgb(26, 80, 56));
    }
    for (int c = -50; c <= 50; c += 5) {
        bool big = c % 10 == 0;
        r_pol(c * R_DEG / 50, (big ? 176 : 183) * RF, &x0, &y0);
        r_pol(c * R_DEG / 50, 197 * RF, &x1, &y1);
        uint16_t col = abs(c) <= 5 ? rgb(90, 230, 150) : (big ? rgb(150, 160, 200) : rgb(80, 90, 130));
        stroke_line(x0, y0, x1, y1, (big ? 1.6f : 0.8f) * RF, col);
    }
    char t[8];
    for (int c = -50; c <= 50; c += 25) {
        r_pol(c * R_DEG / 50, 162 * RF, &x0, &y0);
        snprintf(t, sizeof t, c > 0 ? "+%d" : "%d", c);
        mf_text_c(&MF_TINY, (int)x0, (int)y0 + 5, t, rgb(130, 140, 180), 255, 0);
    }
    r_pol(-R_DEG, 162 * RF, &x0, &y0);
    mf_text_c(&MF_TINY, (int)x0, (int)y0 + RI(21), "FLAT", rgb(90, 140, 220), 255, 0);
    r_pol(R_DEG, 162 * RF, &x0, &y0);
    mf_text_c(&MF_TINY, (int)x0, (int)y0 + RI(21), "SHARP", rgb(230, 140, 80), 255, 0);
    // the strobe band: stripes on an arc that move to the side of the error (they stop in tune)
    for (int a = -60; a <= 60; a += 2) {
        bool on = fsin(a * 0.45f + s_strobe) > 0;
        uint16_t c = live ? (on ? mix(rgb(20, 26, 48), cents_col(s_cents), 150) : rgb(20, 26, 48)) : rgb(24, 30, 54);
        r_pol((float)a, 137 * RF, &x0, &y0);
        r_pol((float)a, 149 * RF, &x1, &y1);
        stroke_line(x0, y0, x1, y1, 2 * RF, c);
    }
    // the marker: a bar across the band and the ticks, with a glow
    float a = s_needle * R_DEG / 50;
    uint16_t nc = live ? cents_col(s_cents) : rgb(70, 80, 110);
    r_pol(a, 130 * RF, &x0, &y0);
    r_pol(a, 199 * RF, &x1, &y1);
    if (live) {
        stroke_line(x0, y0, x1, y1, 4.5f * RF, mix(rgb(10, 14, 30), nc, 90));
        stroke_line(x0, y0, x1, y1, 1.6f * RF, rgb(255, 255, 255));
    }
    r_pol(a, 186 * RF, &x0, &y0);
    gfx_fill_circle((int)x0, (int)y0, RI(6), nc);
    // the cents number in a pill under the marker
    if (live) {
        snprintf(t, sizeof t, "%+d", (int)lroundf(s_cents));
        r_pol(a < -46 ? -46 : a > 46 ? 46 : a, 116 * RF, &x0, &y0);
        int pw = mf_width(&MF_N16, t, 1) + 14;
        box_t pill = {(int)x0 - pw / 2, (int)y0 - 11, pw, 22};
        card_bg(&pill, rgb(16, 20, 40), nc);
        mf_text_c(&MF_N16, (int)x0, (int)y0 + 8, t, nc, 255, 1);
    }
}

// the note letter in the middle, the frequency under it
static void r_note(int64_t now)
{
    bool live = s_note >= 0 && s_ref_note < 0;
    bool intune = live && s_intune_since && now - s_intune_since > 350;
    // the glow: a soft disk behind the note and the frequency, and a pulse ring at its edge
    if (s_glow > 0.02f) {
        gfx_fill_circle(RC, R_NY + RI(8), RI(88), mix(rgb(12, 18, 40), rgb(20, 90, 60), (int)(s_glow * 120)));
        gfx_fill_circle(RC, R_NY + RI(8), RI(82), mix(rgb(12, 18, 40), rgb(20, 90, 60), (int)(s_glow * 255)));
    }
    if (intune) {
        int k = (int)((now - s_intune_since) % 900);
        if (k < 500) {
            gfx_ring(RC, R_NY + RI(8), RI(88) + k / 80, RI(86) + k / 80, rgb(120, 255, 170));
        }
    }
    int note = s_ref_note >= 0 ? s_ref_note : s_note;
    if (s_inst != I_CHROM && note >= 0 && s_ref_note < 0) {
        note = target_note();
    }
    const float h = 104 * RF, r = 7.5f * RF;
    if (note >= 0) {
        uint16_t c = s_ref_note >= 0 ? rgb(255, 210, 120) : (intune ? rgb(140, 255, 190) : rgb(240, 244, 255));
        bool sh = NOTE_SHARP[note % 12];
        float gw = sh ? h : h * 0.72f + 20 * RF, lx = RC - gw / 2, ly = R_NY - h / 2 - 8 * RF;
        stroke_char(NOTE_L[note % 12][0], lx, ly, h, r, c);
        if (sh) {
            stroke_char('#', lx + h * 0.7f, ly, h * 0.5f, r * 0.7f, c);
        }
        char t[12];
        snprintf(t, sizeof t, "%d", note / 12 - 1);
        mf_text(&MF_N24, (int)(lx + h * 0.72f), (int)(ly + h), t, c, 255, 1);
    } else {
        stroke_line(RC - 28 * RF, R_NY - 8 * RF, RC + 28 * RF, R_NY - 8 * RF, r, rgb(70, 80, 110));
    }
    char f[24];
    if (s_ref_note >= 0) {
        snprintf(f, sizeof f, "REFERENCE");
    } else if (live) {
        snprintf(f, sizeof f, "%.1f HZ", (double)s_hz);
    } else {
        snprintf(f, sizeof f, "PLAY A NOTE");
    }
    mf_text_c(&MF_N20, RC, R_NY + RI(76), f, rgb(180, 190, 230), 255, 1);
    if (!live && s_ref_note < 0) {
        mf_text_c(&MF_SMALL, RC, R_NY + RI(96), BOARD_WHEEL ? "TAP OR PUSH FOR A TONE" : "TAP FOR A TONE",
                  rgb(110, 120, 160), 255, 1);
    }
}

// the bottom arc: the strings, or the trail (CHROMATIC), or the A4 keys
static void r_bottom(void)
{
    char t[8];
    if (s_r_a4) {
        r_button(&R_A4M, rgb(30, 38, 66), rgb(70, 84, 130), false);
        r_button(&R_A4P, rgb(30, 38, 66), rgb(70, 84, 130), false);
        mf_text_c(&MF_N28, R_A4M.x, R_A4M.y + 11, "-", rgb(235, 240, 255), 255, 1);
        mf_text_c(&MF_N28, R_A4P.x, R_A4P.y + 11, "+", rgb(235, 240, 255), 255, 1);
        snprintf(t, sizeof t, "%d", s_a4);
        mf_text_c(&MF_TINY, RC, RI(300), "A4 REFERENCE", rgb(150, 160, 200), 255, 0);
        mf_text_c(&MF_N34, RC, RI(338), t, rgb(255, 224, 140), 255, 1);
        mf_text_c(&MF_SMALL, RC, RI(358), "HZ", rgb(150, 160, 200), 255, 1);
        return;
    }
    int n = INST_N[s_inst];
    if (n) {
        int tn = s_note >= 0 ? target_note() : -1;
        for (int i = 0; i < n; i++) {
            int note = INST_NOTES[s_inst][i];
            rbtn_t k = r_str_btn(i, n);
            bool sel = s_target == i, hot = tn == note && s_note >= 0;
            r_button(&k, sel ? rgb(60, 50, 110) : (hot ? rgb(28, 60, 50) : rgb(26, 32, 58)),
                     hot ? rgb(90, 230, 150) : rgb(70, 80, 130), false);
            snprintf(t, sizeof t, "%s%s%d", NOTE_L[note % 12], NOTE_SHARP[note % 12] ? "#" : "", note / 12 - 1);
            mf_text_c(&MF_N16, k.x, k.y + 7, t, rgb(230, 236, 255), 255, 1);
        }
        return;
    }
    // the trail: the oldest value at the left; out = sharp, in = flat; the green band = in tune
    float x, y;
    for (int a = 124; a <= 236; a += 2) {
        r_pol((float)a, R_STR, &x, &y);
        gfx_fill_circle((int)x, (int)y, RI(4), rgb(22, 50, 40));
    }
    for (int a = 124; a <= 236; a += 2) {
        r_pol((float)a, R_STR, &x, &y);
        gfx_fill_rect((int)x, (int)y, 2, 1, rgb(50, 110, 80));
    }
    for (int i = 0; i < HIST_N; i++) {
        int v = s_hist[(s_hist_w + i) % HIST_N];
        if (v == 127) {
            continue;
        }
        r_pol(236 - i * 112.0f / (HIST_N - 1), R_STR + v * 24 / 50.0f * RF, &x, &y);
        gfx_fill_rect((int)x - 1, (int)y - 1, 3, 3, cents_col((float)v));
    }
}

// the instrument button (a small picture of the strings, or keys for CHROMATIC) and the A4 button
static void r_side(void)
{
    rbtn_t bi = R_INST, ba = R_A4;
    const rbtn_t *b = &bi;
    r_button(b, rgb(30, 38, 66), rgb(70, 84, 130), !s_r_a4);
    int n = INST_N[s_inst];
    if (n) {
        int x0 = b->x - (n - 1) * 3;
        for (int i = 0; i < n; i++) {
            gfx_fill_rect(x0 + i * 6, b->y - RI(30), 2, RI(22), rgb(210, 200, 160));
        }
        gfx_fill_rect(x0 - 4, b->y - RI(13), (n - 1) * 6 + 10, 3, rgb(160, 110, 70));
    } else {
        static const int8_t BK[5] = {0, 1, 3, 4, 5};
        for (int i = 0; i < 7; i++) {
            gfx_fill_rect(b->x - 21 + i * 6, b->y - RI(30), 5, RI(20), rgb(230, 232, 240));
        }
        for (int i = 0; i < 5; i++) {
            gfx_fill_rect(b->x - 21 + BK[i] * 6 + 4, b->y - RI(30), 3, RI(12), rgb(20, 24, 40));
        }
    }
    const char *t = INST_NAME[s_inst];
    const mfont_t *f = mf_width(&MF_LABEL, t, 1) <= RI(76) ? &MF_LABEL : &MF_TINY;
    mf_text_c(f, b->x, b->y + RI(16), t, rgb(235, 240, 255), 255, 1);
    b = &ba;
    r_button(b, s_r_a4 ? rgb(60, 50, 110) : rgb(30, 38, 66), s_r_a4 ? rgb(170, 150, 255) : rgb(70, 84, 130), s_r_a4);
    char a4[8];
    snprintf(a4, sizeof a4, "%d", s_a4);
    mf_text_c(&MF_LABEL, b->x, b->y - RI(12), "A4", rgb(150, 160, 200), 255, 1);
    mf_text_c(&MF_N24, b->x, b->y + RI(16), a4, rgb(235, 240, 255), 255, 1);
    mf_text_c(&MF_TINY, b->x, b->y + RI(31), "HZ", rgb(150, 160, 200), 255, 0);
}

static void r_render(int64_t now)
{
    r_bg(rgb(16, 22, 48), rgb(6, 10, 24));
    bool live = s_note >= 0 && s_ref_note < 0;
    r_scale(live);
    r_note(now);
    r_bottom();
    r_side();
}
#endif

// ---------------------------------------------------------------- the app

static void tuner_enter(int64_t now)
{
    s_now = now;
    if (!s_ring) {
        s_ring = heap_caps_calloc(RING, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_x24) {
        s_x24 = heap_caps_calloc(2 * WIN, sizeof(float), MALLOC_CAP_SPIRAM);
    }
    if (!s_x12) {
        s_x12 = heap_caps_calloc(WIN, sizeof(float), MALLOC_CAP_SPIRAM);
    }
    if (!s_d) {
        s_d = heap_caps_calloc(TAU_MAX + 2, sizeof(float), MALLOC_CAP_SPIRAM);
    }
    s_wpos = 0;
    s_inst = (int)(store_get("tn_inst", 0) % N_INST);
    int a4 = (int)store_get("tn_a4", 440);
    s_a4 = a4 >= 430 && a4 <= 450 ? a4 : 440;
    s_target = -1;
    s_note = -1;
    s_hz = 0;
    s_nmed = 0;
    s_needle = s_needle_v = 0;
    s_intune_since = 0;
    s_heard_at = 0;
    memset(s_hist, 127, sizeof s_hist);
    s_ref_note = -1;
#if BOARD_ROUND
    s_r_a4 = false;
    s_r_save = 0;
    input_wheel();  // (drop the steps that wait)
    input_wheel_presses();
#endif
    audio_mic_start();
    audio_mic_tap(mic_cb);
}

static void tuner_leave(void)
{
#if BOARD_ROUND
    if (s_r_save) {
        r_save_now();
    }
#endif
    ref_off();
    audio_mic_tap(NULL);
    audio_mic_stop();
    free(s_ring);
    free(s_x24);
    free(s_x12);
    free(s_d);
    s_ring = NULL;
    s_x24 = s_x12 = s_d = NULL;
}

static void tuner_input(const input_event_t *ev)
{
#if BOARD_ROUND
    if (gfx_round()) {
        r_input(ev);
        return;
    }
#endif
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (in_box(&CARD, x, y, 4)) {
        ref_toggle();
        return;
    }
    if (in_box(&INST, x, y, 6)) {
        s_inst = (s_inst + 1) % N_INST;
        s_target = -1;
        store_set("tn_inst", (uint32_t)s_inst);
        sfx_play(SFX_CLICK);
        return;
    }
    if (in_box(&A4M, x, y, 6) || in_box(&A4P, x, y, 6)) {
        s_a4 += in_box(&A4P, x, y, 6) ? 1 : -1;
        if (s_a4 < 430) {
            s_a4 = 430;
        }
        if (s_a4 > 450) {
            s_a4 = 450;
        }
        store_set("tn_a4", (uint32_t)s_a4);
        sfx_play(SFX_TICK);
        return;
    }
    if (INST_N[s_inst] && in_box(&ROWB, x, y, 6)) {
        int i = (x - ROWB.x) / str_btn_w();
        if (i < 0) {
            i = 0;
        }
        if (i >= INST_N[s_inst]) {
            i = INST_N[s_inst] - 1;
        }
        string_tap(i);
    }
}

static void tuner_update(int64_t now)
{
    float dt = (now - s_now) / 1000.0f;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    s_now = now;
#if BOARD_ROUND
    if (gfx_round()) {
        r_wheel(now);
    }
#endif
    if (s_ref_note >= 0 && now > s_ref_until) {
        ref_off();
    }
    if (s_ref_note < 0 && now - s_last_an >= 40) {
        s_last_an = now;
        analyse(now);
        int8_t hv = 127;
        if (s_hz > 0) {
            int tn = target_note();
            s_cents = (s_note_f - tn) * 100;
            if (s_inst == I_CHROM) {
                s_note = tn;
            } else {
                s_note = (int)lroundf(s_note_f);
            }
            float c = s_cents < -50 ? -50 : s_cents > 50 ? 50 : s_cents;
            hv = (int8_t)lroundf(c);
            if (fabsf(s_cents) < 3) {
                if (!s_intune_since) {
                    s_intune_since = now;
                }
            } else {
                s_intune_since = 0;
            }
        } else if (now - s_heard_at > 1500) {
            s_note = -1;
            s_intune_since = 0;
        }
        s_hist[s_hist_w] = hv;
        s_hist_w = (s_hist_w + 1) % HIST_N;
    }
    // the needle: a spring toward the cents (it rests in the middle with no note)
    float goal = s_note >= 0 && s_ref_note < 0 ? (s_cents < -50 ? -50 : s_cents > 50 ? 50 : s_cents) : 0;
    s_needle_v += ((goal - s_needle) * 90 - s_needle_v * 16) * dt;
    s_needle += s_needle_v * dt;
    if (s_note >= 0 && s_ref_note < 0) {
        s_strobe += s_cents * dt * 0.25f;
    }
    bool intune = s_intune_since && now - s_intune_since > 350;
    s_glow += ((intune ? 1.0f : 0.0f) - s_glow) * (dt * 6 > 1 ? 1 : dt * 6);
}

static void tuner_render(int64_t now)
{
#if BOARD_ROUND
    if (gfx_round()) {
        r_render(now);
        return;
    }
#endif
    // the background: a deep blue with a soft light at the top
    for (int y = 0; y < SCR_H; y += 4) {
        int k = y * 255 / SCR_H;
        gfx_fill_rect(0, y, SCR_W, 4,
                      rgb((uint8_t)(14 - k * 6 / 255), (uint8_t)(18 - k * 8 / 255), (uint8_t)(40 - k * 18 / 255)));
    }
    draw_card(now);
    draw_meter(now);
    draw_row();
    // the instrument and the A4 reference
    button(&INST, INST_NAME[s_inst], false);
    char t[12];
    snprintf(t, sizeof t, "A4 %d", s_a4);
#if BOARD_BAR_SCREEN
    mf_text_c(&MF_N20, INST.x + INST.w / 2, 90, t, rgb(220, 226, 255), 255, 1);
    mf_text_c(&MF_TINY, INST.x + INST.w / 2, 70, "REFERENCE", rgb(120, 130, 170), 255, 0);
#elif BOARD_UI_TABLET
    mf_text_c(&MF_N28, (A4M.x + A4P.x + A4P.w) / 2, A4M.y + 44, t, rgb(220, 226, 255), 255, 1);
    if (s_note >= 0 && s_ref_note < 0) {
        char f[20];
        snprintf(f, sizeof f, "%.1f HZ  %+d", (double)s_hz, (int)lroundf(s_cents));
        mf_text_c(&MF_N16, INST.x + INST.w / 2, 92, f, cents_col(s_cents), 255, 1);
    }
#else
    mf_text_c(&MF_N20, (A4M.x + A4P.x + A4P.w) / 2, A4M.y + 28, t, rgb(220, 226, 255), 255, 1);
    if (s_note >= 0 && s_ref_note < 0) {
        char f[20];
        snprintf(f, sizeof f, "%.1f HZ  %+d", (double)s_hz, (int)lroundf(s_cents));
        mf_text_c(&MF_SMALL, INST.x + INST.w / 2, 66, f, cents_col(s_cents), 255, 1);
    }
#endif
    button(&A4M, "-", false);
    button(&A4P, "+", false);
}

static bool tuner_awake(void)
{
    return s_heard_at && s_now - s_heard_at < 15000;
}

#include "tficon/tficon.h"
// the menu icon: a tuner dial; the needle swings, settles in the middle and the middle lights green
static void tuner_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x7c5cff), tfi_hex(0x2a1588));
    float u = s / 100.0f, ph = tfi_ph(2);
    float cx = x + 50 * u, cy = y + 70 * u, rad = 36 * u;
    // the face of the dial
    tfi_drop(x + (int)(14 * u), y + (int)(21 * u), (int)(72 * u), (int)(60 * u), (int)(12 * u), (int)(3 * u), 70);
    tfi_rbox(x + (int)(14 * u), y + (int)(21 * u), (int)(72 * u), (int)(60 * u), (int)(12 * u), tfi_hex(0x241850),
             tfi_hex(0x0e0826), 256);
    tfi_arc(cx, cy, rad - 6 * u, 3.5f * u, -2.6f, -1.85f, tfi_hex(0xff5d5d), 256);
    tfi_arc(cx, cy, rad - 6 * u, 3.5f * u, -1.29f, -0.54f, tfi_hex(0xff5d5d), 256);
    tfi_arc(cx, cy, rad - 6 * u, 3.5f * u, -1.70f, -1.44f, tfi_hex(0x5dff8a), 256);
    // the needle: a swing that dies out, then the hold in the middle
    float t = ph / 0.65f, ang;
    if (t < 1) {
        ang = 1.1f * fsin(t * 3.14159f * 3) * (1 - t) * (1 - t);
    } else {
        ang = 0;
    }
    float ok = tfi_step(0.62f, 0.7f, ph) * (1 - tfi_step(0.94f, 1.0f, ph));
    if (ok > 0.02f) {
        tfd_glow(cx, cy - rad + 4 * u, 16 * u, tfi_hex(0x5dff8a), (int)(220 * ok));
    }
    float a = -1.5708f + ang;
    tfi_line(cx + 1.5f * u, cy + 1.5f * u, cx + 1.5f * u + fcos(a) * (rad - 4 * u),
             cy + 1.5f * u + fsin(a) * (rad - 4 * u), 3.4f * u, false, 0, 90);
    tfi_line(cx, cy, cx + fcos(a) * (rad - 4 * u), cy + fsin(a) * (rad - 4 * u), 3.4f * u, true,
             tfd_mix(rgb(255, 255, 255), tfi_hex(0x8dffa8), (int)(255 * ok)), 256);
    tfi_disc(cx, cy, 7 * u, tfi_hex(0xffd36b), tfi_hex(0xf07a12), 256);
    tfi_end();
}

const app_t app_tuner = {
        .name = "TUNER",
        .landscape = true,
        .enter = tuner_enter,
        .leave = tuner_leave,
        .input = tuner_input,
        .update = tuner_update,
        .render = tuner_render,
        .draw_icon = tuner_icon,
        .awake = tuner_awake,
        .round = APP_ROUND_HI,  // the round frame of the panel (466 on the AMOLED-1.75C, 412 on the Watcher)
        .wheel = true,
        .run = true,  // the settings panel: it listens on under it
};
