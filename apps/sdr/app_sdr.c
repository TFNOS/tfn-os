// SDR: a spectrum analyzer of the radio of the board, in the style of an SDR program.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_sdr at the end of this file);
// the engine task "sdr" (core 1) runs from enter to leave.
// The memory: the file statics; the traces, the waterfall, the persistence and the frame buffers (PSRAM) come at
// enter and go at leave (bufs_free); the lock s_lock stays while the app file is loaded.
//
// The spectrum with the LIVE, MAX HOLD, MIN HOLD and AVERAGE traces, the waterfall, the persistence, the use of the
// channels, the I/Q scope; the centre, the span, the zoom, the pan, the markers, the peaks.
//
// THE LAYERS (each one knows only the layer under it):
//   libs/rfsrc   the radio sources: the contract (rfsrc.h) and the sources (the native radio of the ESP32-S3, the
//                demo). A new radio (a sub-GHz chip on the pins) = one new source file; this app does not change.
//   libs/sdrdsp  the measurement (the plan of a sweep, the FFT, the detector), the traces, the waterfall, the channel
//                plans. Pure C: the PC tests use the same code.
//   this app     THE MODEL (s_m: what the user sets), THE ENGINE TASK (it does the plan of the model on the
//                source: one frame each about 33 ms, on core 1), THE VIEWS (sdr_draw.inc: they only read the traces
//                and the history) and THE CONTROLS (the touch, the strip of buttons, the settings panel).
// The UI writes only the model (and its generation number s_gen); the engine reads the model, makes a new plan when
// the generation changes, and writes the traces under the lock s_lock; the views read them under the same lock.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif
#include "tfdraw/tfdraw.h"
// THE ROUND FRAME (APP_ROUND_HI): the name of the app in the top cap of the circle; the layout of the flat frame in the
// middle band of the circle (the width of the chord at its top, about 0.82 x 0.53 of gfx_w): rl_origin.h moves each
// draw call by (rl_ox, rl_oy), input() takes the origin off each touch point. The readout (the frequency, the span,
// the marker) in the top cap, the 5 keys in a row in the bottom cap, the state under them; the Rubik faces are KF_TEXT
// at the least (rl_big 2), the axis in the bigger mono face.
#include "rndlay/rndlay.h"
#include "rndlay/rl_origin.h"
#include "rfsrc/rfsrc.h"
#include "sdrdsp/sdr_chan.h"
#include "sdrdsp/sdr_spec.h"
#include "sdrdsp/sdr_trace.h"
#include "sdrdsp/sdr_wf.h"

static bool s_he;  // the language: false English, true Hebrew
#include "sdr_text.inc"
#include "sdr_fonts.inc"

#define APP_NAME "SDR"
#define MHZ      1000000LL

// ================================================================ the layout
static int s_W, s_H;
static int s_TB, s_AX, s_BW;            // the top bar, the frequency axis, the strip of buttons
static int s_PX0, s_PX1, s_PY0, s_PY1;  // the plot box
static int s_bins;                      // = the width of the plot (one point a pixel)
#define BAR  BOARD_BAR_SCREEN
#define NBTN (BAR ? 4 : 5)
enum {
    B_IN,
    B_OUT,
    B_VIEW,
    B_CLEAR,
    B_FREEZE
};

// ================================================================ the sources
#define NSRC_MAX 4
static rfsrc_t s_src[NSRC_MAX];
static int s_nsrc;
static const char *s_src_vals[2 * NSRC_MAX];                // the names of the sources for the panel (English, Hebrew)
static const char *s_gain_vals[2 * (RFSRC_MAX_GAINS + 1)];  // AUTO, then the steps of the source

// ================================================================ the model (the UI writes it; the engine reads it)
enum {
    V_SPLIT,
    V_SPEC,
    V_WF,
    V_PERS,
    V_CHAN,
    V_SCOPE,
    V_N
};
enum {
    MK_OFF,
    MK_PEAK,
    MK_FIXED
};
typedef struct {
    int src;
    int64_t center, span;
    uint8_t gain, win, det, rbw;
    uint8_t trc;  // the trace preset (TRC_*)
    uint8_t avg, hold, cmap, rng, wfs, pk, mk;
    uint8_t view;
    float ref;  // the top of the plot (dB)
    bool frozen;
    bool agc;  // AUTO GAIN: the app chooses the gain (an overload: a lower step)
} model_t;
static model_t s_m;
static volatile uint32_t s_gen;
static double s_mk_hz;       // the fixed marker (MK_FIXED)
static uint32_t s_auto_seq;  // the frame number of the next AUTO LEVEL (0: none)
static int64_t s_agc_t;      // AUTO GAIN: the time of the last step

static const int AVG_N[3] = {4, 16, 64};
static const float RANGE_DB[4] = {40, 60, 80, 100};
static const int WF_EVERY[3] = {1, 2, 4};  // the frames in one row of the waterfall
static const uint8_t WIN_OF[3] = {SDR_WIN_HANN, SDR_WIN_BH4, SDR_WIN_FLAT};
enum {
    TRC_LIVE_MAX,
    TRC_LIVE_AVG,
    TRC_ALL,
    TRC_LIVE
};
static bool trc_on(int t)
{
    switch (s_m.trc) {
    case TRC_LIVE_MAX:
        return t == SDR_TR_LIVE || t == SDR_TR_MAX;
    case TRC_LIVE_AVG:
        return t == SDR_TR_LIVE || t == SDR_TR_AVG;
    case TRC_ALL:
        return true;
    default:
        return t == SDR_TR_LIVE;
    }
}

// ================================================================ the engine (its task writes; the views read)
#ifdef ESP_PLATFORM
static SemaphoreHandle_t s_lock;
static void lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}
static void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}
static TaskHandle_t s_task;
#else
static void lock(void)
{
}
static void unlock(void)
{
}
#endif
static volatile bool s_quit, s_task_done;
static sdr_engine_t s_eng;
static int s_eng_src = -1;  // the source that the engine has open (-1: none)
static int s_eng_err;       // the last error of the source (0: none)
static bool s_eng_on;
static sdr_plan_t s_plan;
static uint32_t s_plan_gen = 0xFFFFFFFFu;
static sdr_traces_t s_tr;
static sdr_wf_t s_wf;
static sdr_persist_t s_ps;
static float *s_fr, *s_wacc;
static int s_wacc_n;
#define PK_MAX 6
static sdr_peak_t s_pk[PK_MAX];
static int s_npk;
static float s_floor = SDR_NODATA;
static int64_t s_over_t;  // the time of the last overload (ms)
static float s_frame_ms, s_fps;
static uint32_t s_seq;
static int64_t s_last_frame_t;
// the times of the work (the console log each 5 s: the speed on a board)
static struct {
    int n, rn;
    float frame, tune, read, fft, map, post, render;
    int64_t t0;
} s_prof;
#define SCOPE_N 2048
static int16_t *s_scope;
static int s_scope_n;
#define CH_MAX 64
static float s_busy[CH_MAX];   // the use of each channel (0..1, a mean of about 1 s)
static float s_chpow[CH_MAX];  // the mean power of each channel (dB)

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int cur_kind(void)
{
    return s_m.src < s_nsrc ? s_src[s_m.src].caps->kind : RFSRC_IQ;
}
static const rfsrc_caps_t *cur_caps(void)
{
    return s_m.src < s_nsrc ? s_src[s_m.src].caps : NULL;
}
static int src_state(int k)
{
    return k < s_nsrc ? s_src[k].ops->state(s_src[k].ctx) : RFSRC_ABSENT;
}

static void model_changed(void)
{
    s_gen++;
}

// The axis of the model: a new centre and span (the engine clamps them to the source, and gives them back)
static void model_axis(int64_t center, int64_t span)
{
    lock();
    s_m.center = center;
    s_m.span = span;
    model_changed();
    unlock();
}

static void engine_close(void)
{
    if (s_eng_on) {
        sdr_engine_stop(&s_eng);
    }
    s_eng_on = false;
    s_eng_src = -1;
}

static void make_req(const model_t *m, sdr_req_t *r)
{
    memset(r, 0, sizeof(*r));
    r->center_hz = m->center;
    r->span_hz = m->span;
    r->bins = s_bins;
    r->gain = m->gain;
    r->window = WIN_OF[m->win % 3];
    r->detector = m->det ? SDR_DET_AVG : SDR_DET_PEAK;
    r->rbw = m->rbw;
}

// The channel statistics of one frame (the plans that touch the axis)
static void chan_stats(const float *live)
{
    const sdr_chplan_t *pl[8];
    int np = sdr_chplans(pl, 8), g = 0;
    for (int k = 0; k < np; k++) {
        for (int c = 0; c < pl[k]->n && g < CH_MAX; c++, g++) {
            const sdr_chan_t *ch = &pl[k]->ch[c];
            float p = sdr_band_power(live, s_plan.req.bins, s_plan.f0_hz, s_plan.df_hz, ch->f_hz, ch->bw_hz);
            if (p <= SDR_NODATA) {
                continue;
            }
            bool busy = s_floor > SDR_NODATA && p > s_floor + pl[k]->busy_db;
            s_busy[g] += ((busy ? 1.0f : 0.0f) - s_busy[g]) * 0.04f;
            float pa = sdr_band_power(s_tr.v[SDR_TR_AVG], s_plan.req.bins, s_plan.f0_hz, s_plan.df_hz, ch->f_hz,
                                      ch->bw_hz);
            s_chpow[g] = pa;
        }
    }
}

// One step of the engine: the source, the plan, a frame, the traces and the history
static void eng_step(void)
{
    model_t m;
    uint32_t gen;
    lock();
    m = s_m;
    gen = s_gen;
    unlock();
    if (m.src != s_eng_src) {
        lock();
        engine_close();
        s_eng_err = 0;
        unlock();
        if (src_state(m.src) == RFSRC_READY) {
            sdr_req_t r;
            make_req(&m, &r);
            sdr_plan_t p;
            if (sdr_plan_make(s_src[m.src].caps, &r, &p)) {
                int e = sdr_engine_start(&s_eng, &s_src[m.src], &p);
                lock();
                if (e == 0) {
                    s_eng_on = true;
                    s_plan_gen = 0xFFFFFFFFu;
                    sdr_traces_reset(&s_tr);
                    sdr_wf_clear(&s_wf);
                } else {
                    s_eng_err = e;
                }
                unlock();
            }
        }
        s_eng_src = m.src;
    }
    if (!s_eng_on) {
#ifdef ESP_PLATFORM
        vTaskDelay(pdMS_TO_TICKS(50));
#endif
        return;
    }
    if (gen != s_plan_gen) {
        sdr_req_t r;
        make_req(&m, &r);
        sdr_plan_t p;
        sdr_plan_make(s_src[m.src].caps, &r, &p);
        lock();
        // a new gain: the noise of the receiver does not move with the gain as a signal does, so the corrected floor
        // moves: the holds (the MIN trace gives the floor) and the use of the channels start again
        if (p.req.gain != s_plan.req.gain) {
            sdr_traces_reset(&s_tr);
            s_tr.n = 0;
            for (int g = 0; g < CH_MAX; g++) {
                s_busy[g] = 0;
            }
        }
        s_plan = p;
        s_plan_gen = gen;
        sdr_traces_axis(&s_tr, p.req.bins, p.f0_hz, p.df_hz);
        s_tr.avg_n = AVG_N[m.avg % 3];
        s_tr.decay_db = m.hold ? 20.0f : 0.0f;
        s_wacc_n = 0;
        if (s_gen == gen) {  // (the real centre and span: the UI shows them)
            s_m.center = p.req.center_hz;
            s_m.span = p.req.span_hz;
        }
        unlock();
    }
    if (m.frozen) {
#ifdef ESP_PLATFORM
        vTaskDelay(pdMS_TO_TICKS(40));
#endif
        return;
    }
    sdr_frame_t fi;
    sdr_engine_frame(&s_eng, &s_plan, s_fr, &fi);
    const int64_t t = now_ms();
    float dt = s_last_frame_t ? (float)(t - s_last_frame_t) / 1000.0f : 0.033f;
    if (dt > 0.5f) {
        dt = 0.5f;
    }
    s_last_frame_t = t;
    lock();
    if (fi.err) {
        s_eng_err = fi.err;
        unlock();
        return;
    }
    s_eng_err = 0;
    // (each frame goes out: the plan belongs to this task, so the frame always fits the axis of the traces; a model
    // that changes during the frame gives the next plan. A frame must not go away when the model changes: a model
    // that changes on each update would then give no frame at all)
    {
        const int n = s_plan.req.bins;
        sdr_traces_push(&s_tr, s_fr, dt);
        for (int i = 0; i < n; i++) {
            if (!s_wacc_n || s_fr[i] > s_wacc[i]) {
                s_wacc[i] = s_fr[i];
            }
        }
        if (++s_wacc_n >= WF_EVERY[m.wfs % 3]) {
            sdr_wf_push(&s_wf, s_wacc, n, s_plan.f0_hz, s_plan.df_hz, (uint32_t)t);
            s_wacc_n = 0;
        }
        // (the work of a view only while it shows: the persistence map has 80 K cells, the channels many bands)
        if (s_m.view == V_PERS) {
            sdr_persist_scale(&s_ps, m.ref, RANGE_DB[m.rng % 4], s_plan.f0_hz, s_plan.df_hz);
            sdr_persist_push(&s_ps, s_fr, n, 5);
        }
        // the floor of the noise: from the MIN trace (a busy band has a signal at most points); the minimum of noise is
        // about 6 dB under its mean
        float fm = sdr_floor(s_tr.v[SDR_TR_MIN], n);
        s_floor = fm > SDR_NODATA ? fm + 6 : SDR_NODATA;
        const float *pkt = trc_on(SDR_TR_MAX) && m.trc != TRC_ALL ? s_tr.v[SDR_TR_MAX] : s_tr.v[SDR_TR_LIVE];
        s_npk = sdr_peaks(pkt, n, s_floor, 10.0f, n / 24 > 4 ? n / 24 : 4, s_pk, PK_MAX);
        if (s_m.view == V_CHAN) {
            chan_stats(s_fr);
        }
        if (s_eng.iq && s_eng.iq_n > 0) {
            s_scope_n = s_eng.iq_n < SCOPE_N ? s_eng.iq_n : SCOPE_N;
            memcpy(s_scope, s_eng.iq, sizeof(int16_t) * 2 * (size_t)s_scope_n);
        }
        if (fi.overload) {
            s_over_t = t;
        }
        s_frame_ms = fi.ms;
        s_fps += ((dt > 0 ? 1.0f / dt : 0) - s_fps) * 0.1f;
        s_seq++;
    }
    unlock();
    s_prof.n++;
    s_prof.frame += fi.ms;
    s_prof.tune += fi.ms_tune;
    s_prof.read += fi.ms_read;
    s_prof.fft += fi.ms_fft;
    s_prof.map += fi.ms_map;
    s_prof.post += (float)(now_ms() - t);
    if (!s_prof.t0) {
        s_prof.t0 = t;
    }
    if (t - s_prof.t0 >= 5000) {
#ifdef ESP_PLATFORM
        const float k = 1.0f / (float)s_prof.n;
        ESP_LOGI("sdr",
                 "%d frames in 5 s: frame %.1f ms (tune %.1f, read %.1f, fft %.1f, map %.1f), post %.1f; "
                 "%d renders, %.1f ms; %d seg, fft %d x %d",
                 s_prof.n, s_prof.frame * k, s_prof.tune * k, s_prof.read * k, s_prof.fft * k, s_prof.map * k,
                 s_prof.post * k, s_prof.rn, s_prof.rn ? s_prof.render / (float)s_prof.rn : 0.0f, s_plan.nseg,
                 s_plan.fft_n, s_plan.navg);
#endif
        memset(&s_prof, 0, sizeof(s_prof));
        s_prof.t0 = t;
    }
}

#ifdef ESP_PLATFORM
static void eng_task(void *arg)
{
    (void)arg;
    while (!s_quit) {
        int64_t t0 = now_ms();
        eng_step();
        int wait = 33 - (int)(now_ms() - t0);  // (about 30 frames a second at most)
        vTaskDelay(pdMS_TO_TICKS(wait < 2 ? 2 : wait));
    }
    lock();
    engine_close();
    unlock();
    s_task_done = true;
    vTaskDelete(NULL);
}
#endif

// ================================================================ the controls
static int64_t s_now, s_touch_t;
static char s_toast[80];
static int64_t s_toast_t;
static void toast(const char *s)
{
    snprintf(s_toast, sizeof(s_toast), "%s", s);
    s_toast_t = s_now;
}

static bool in_plot(int x, int y)
{
    return x >= s_PX0 && x < s_PX1 && y >= s_PY0 && y < s_PY1;
}
static double x_to_hz(double x)
{
    return (double)s_m.center + ((x - s_PX0) / s_bins - 0.5) * (double)s_m.span;
}

// The box of a button of the strip
static void btn_box(int b, int *x, int *y, int *w, int *h)
{
    if (gfx_round()) {  // (the round frame: a row in the bottom cap, the points of the box)
        rl_t_keyx(b, NBTN, x, w);
        *x -= rl_ox;
        *y = rl_t_key() - rl_oy;
        *h = rl_t_kh();
        return;
    }
    const int top = s_TB, hh = (s_H - s_TB) / NBTN;
    *x = s_W - s_BW;
    *y = top + b * hh;
    *w = s_BW;
    *h = hh;
}

static int btn_at(int x, int y)
{
    for (int b = 0; b < NBTN; b++) {
        int bx, by, bw, bh;
        btn_box(b, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            return b;
        }
    }
    return -1;
}

// The point of the frequency on the axis of the plan (the views); -1e9 when there is no plan
static double hz_to_x(double hz)
{
    if (s_plan.df_hz <= 0) {
        return -1e9;
    }
    return s_PX0 + sdr_plan_point(&s_plan, hz) + 0.5;
}

static void zoom(double factor, double at_hz)
{
    double span = (double)s_m.span * factor;
    double c = at_hz + ((double)s_m.center - at_hz) * factor;
    model_axis((int64_t)llround(c), (int64_t)llround(span));
}

// The frequency of the highest point of a trace within r px of x (the marker of a tap)
static double peak_near(int x, int r)
{
    const float *t = trc_on(SDR_TR_MAX) && s_m.trc != TRC_ALL ? s_tr.v[SDR_TR_MAX] : s_tr.v[SDR_TR_LIVE];
    int i0 = x - s_PX0 - r, i1 = x - s_PX0 + r, best = -1;
    if (i0 < 0) {
        i0 = 0;
    }
    if (i1 > s_plan.req.bins - 1) {
        i1 = s_plan.req.bins - 1;
    }
    for (int i = i0; i <= i1; i++) {
        if (t[i] > SDR_NODATA && (best < 0 || t[i] > t[best])) {
            best = i;
        }
    }
    return best >= 0 ? sdr_plan_freq(&s_plan, best) : x_to_hz(x);
}

static void gain_item_update(const rfsrc_caps_t *c);

static void select_source(int k)
{
    if (k < 0 || k >= s_nsrc) {
        return;
    }
    lock();
    s_m.src = k;
    const rfsrc_caps_t *c = s_src[k].caps;
    if (s_m.gain >= c->ngains) {
        s_m.gain = c->gain_def;
    }
    // a centre out of the bands of the new source: the middle of its first band
    bool in = false;
    for (int b = 0; b < c->nbands; b++) {
        if (s_m.center >= c->band[b].lo_hz && s_m.center <= c->band[b].hi_hz) {
            in = true;
        }
    }
    if (!in) {
        s_m.center = (c->band[0].lo_hz + c->band[0].hi_hz) / 2;
        s_m.span = (c->band[0].hi_hz - c->band[0].lo_hz) / 4;
    }
    model_changed();
    unlock();
    if (s_m.view == V_SCOPE && c->kind != RFSRC_IQ) {
        s_m.view = V_SPLIT;
    }
    if (s_m.mk == MK_FIXED) {
        s_m.mk = MK_PEAK;  // (the marker of the old source means nothing here)
    }
    s_mk_hz = 0;
    s_auto_seq = s_seq + 15;  // AUTO LEVEL after the first 15 frames of the new source
    s_gain_vals[0] = "AUTO";
    s_gain_vals[1] = "אוטומטי";
    for (int g = 0; g < c->ngains && g < 3; g++) {
        s_gain_vals[2 * g + 2] = c->gain_name[g][0];
        s_gain_vals[2 * g + 3] = c->gain_name[g][1];
    }
    gain_item_update(c);
    store_set("sdr_src", (uint32_t)k);
}

// The full band of the source: the 2.4 GHz ISM band when the source has it, else its first band
static void full_band(void)
{
    const rfsrc_caps_t *c = cur_caps();
    if (!c) {
        return;
    }
    for (int b = 0; b < c->nbands; b++) {
        if (c->band[b].lo_hz <= 2400 * MHZ && c->band[b].hi_hz >= 2484 * MHZ) {
            model_axis(2441750000LL, 84 * MHZ);
            return;
        }
    }
    model_axis((c->band[0].lo_hz + c->band[0].hi_hz) / 2, c->band[0].hi_hz - c->band[0].lo_hz);
}

static void auto_level(void)
{
    lock();
    float mx = SDR_NODATA;
    for (int i = 0; i < s_tr.n; i++) {
        if (s_tr.v[SDR_TR_MAX][i] > mx) {
            mx = s_tr.v[SDR_TR_MAX][i];
        }
    }
    if (mx > SDR_NODATA) {
        float r = RANGE_DB[s_m.rng % 4];
        float top = ceilf((mx + 6) / 5) * 5;
        // the floor must stay in the picture: at least 8 dB over the bottom
        if (s_floor > SDR_NODATA && top - r > s_floor - 8) {
            top = ceilf((s_floor - 8 + r) / 5) * 5;
        }
        s_m.ref = top;
        model_changed();
    }
    unlock();
}

static void clear_holds(void)
{
    lock();
    sdr_traces_reset(&s_tr);
    sdr_traces_axis(&s_tr, s_plan.req.bins, s_plan.f0_hz, s_plan.df_hz);
    for (int g = 0; g < CH_MAX; g++) {
        s_busy[g] = 0;
    }
    unlock();
}

// the touch
static bool s_down, s_pan, s_refdrag, s_pinch;
static double s_pinch_last_s, s_pinch_last_c;  // the last span and centre that the pinch asked for
static int s_dx0, s_dy0;
static int64_t s_c0;
static float s_ref0;
static int64_t s_tap_t;
static int s_tap_x;
static int s_pressed = -1;  // the button under the finger
static double s_pinch_d0, s_pinch_f0;
static int64_t s_pinch_span0;
static bool s_need_card(void);
static int card_btn_at(int x, int y);

// ENTER A FREQUENCY: the keyboard of TFN OS. A number under 100 is GHz (2.437), up to 100000 MHz (2437.5), then kHz,
// then Hz (a frequency in any of the usual units).
static bool s_kbd;
static char s_kbuf[20];
static void kbd_start(void)
{
    s_kbuf[0] = 0;
    kbd_open(TX(T_ENTER), s_kbuf, sizeof(s_kbuf), false);
    s_kbd = true;
}
static void kbd_done(void)
{
    s_kbd = false;
    double v = atof(s_kbuf);
    if (v <= 0) {
        return;
    }
    double hz = v < 100 ? v * 1e9 : v < 100000 ? v * 1e6 : v < 1e8 ? v * 1e3 : v;
    const rfsrc_caps_t *c = cur_caps();
    bool in = false;
    for (int b = 0; c && b < c->nbands; b++) {
        if (hz >= (double)c->band[b].lo_hz && hz <= (double)c->band[b].hi_hz) {
            in = true;
        }
    }
    if (!in) {
        toast(TX(T_BADF));
        return;
    }
    model_axis((int64_t)llround(hz), s_m.span);
    store_set("sdr_c", (uint32_t)(hz / 1000));
}

static void press_button(int b)
{
    switch (b) {
    case B_IN:
        zoom(0.5, s_m.mk != MK_OFF && s_mk_hz > 0 ? s_mk_hz : (double)s_m.center);
        break;
    case B_OUT:
        zoom(2.0, (double)s_m.center);
        break;
    case B_VIEW: {
        int v = s_m.view;
        do {
            v = (v + 1) % V_N;
        } while (v == V_SCOPE && cur_kind() != RFSRC_IQ);
        s_m.view = (uint8_t)v;
        store_set("sdr_view", (uint32_t)v);
        toast(TX(T_V_SPLIT + v));
        break;
    }
    case B_CLEAR:
        clear_holds();
        break;
    case B_FREEZE:
        lock();
        s_m.frozen = !s_m.frozen;
        model_changed();
        unlock();
        break;
    }
}

// a tap on the card of the radio start: START (the source asks for its start) or DEMO (the first demo source)
static void card_tap(int x, int y)
{
    int b = card_btn_at(x, y);
    if (b == 0) {
        s_src[s_m.src].ops->claim(s_src[s_m.src].ctx, APP_NAME);
    }
    if (b == 1) {
        for (int k = 0; k < s_nsrc; k++) {
            if (s_src[k].caps->flags & RFSRC_F_DEMO) {
                select_source(k);
                break;
            }
        }
    }
}

static void input(const input_event_t *ev)
{
    s_touch_t = s_now;
    const int x = ev->x - rl_ox, y = ev->y - rl_oy;  // (the round frame: the point in the middle box)
    if (s_kbd) {
        int r = kbd_input(ev);
        if (r == KBD_OK) {
            kbd_done();
        } else if (r == KBD_CANCEL) {
            s_kbd = false;
        }
        return;
    }
    const bool top_tap = gfx_round() ? y < 0 : y < s_TB + 8 && x < (BAR ? 150 : 140);  // (the round frame: the cap)
    if (ev->kind == IN_TAP && top_tap && !s_need_card()) {
        kbd_start();
        return;
    }
    if (s_need_card()) {
        if (ev->kind == IN_TAP) {
            card_tap(x, y);
        }
        return;
    }
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_pan = s_refdrag = false;
        s_dx0 = x;
        s_dy0 = y;
        s_c0 = s_m.center;
        s_ref0 = s_m.ref;
        s_pressed = btn_at(x, y);
        break;
    case IN_MOVE:
        if (!s_down || s_pinch || s_pressed >= 0) {
            break;
        }
        if (!s_pan && !s_refdrag && in_plot(s_dx0, s_dy0)) {
            if (s_dx0 < s_PX0 + 34 && abs(y - s_dy0) > 8 && abs(y - s_dy0) > abs(x - s_dx0)) {
                s_refdrag = true;
                input_tap_cancel();
            } else if (abs(x - s_dx0) > 8) {
                s_pan = true;
                input_tap_cancel();
            }
        }
        if (s_pan) {
            double hz = (double)s_c0 - (double)(x - s_dx0) * (double)s_m.span / s_bins;
            lock();
            s_m.center = (int64_t)llround(hz);
            model_changed();
            unlock();
        } else if (s_refdrag) {
            float r = RANGE_DB[s_m.rng % 4];
            lock();
            s_m.ref = roundf(s_ref0 + (float)(y - s_dy0) * r / (s_PY1 - s_PY0));
            if (s_m.ref > 40) {
                s_m.ref = 40;
            }
            if (s_m.ref < -120) {
                s_m.ref = -120;
            }
            model_changed();
            unlock();
        }
        break;
    case IN_UP:
        s_down = false;
        if (s_pressed >= 0 && btn_at(x, y) == s_pressed) {
            press_button(s_pressed);
        }
        s_pressed = -1;
        if (s_pan || s_refdrag) {
            store_set("sdr_c", (uint32_t)(s_m.center / 1000));
            store_set("sdr_ref", (uint32_t)(s_m.ref + 200));
        }
        s_pan = s_refdrag = false;
        break;
    case IN_TAP:
        if (!in_plot(x, y)) {
            break;
        }
        if (s_now - s_tap_t < 350 && abs(x - s_tap_x) < 40) {  // a double tap: zoom in there
            zoom(0.5, x_to_hz(x));
            s_tap_t = 0;
            break;
        }
        s_tap_t = s_now;
        s_tap_x = x;
        if (s_m.view == V_SPLIT || s_m.view == V_SPEC || s_m.view == V_PERS || s_m.view == V_WF) {
            lock();
            s_mk_hz = peak_near(x, BAR ? 12 : 10);
            unlock();
            s_m.mk = MK_FIXED;
        }
        break;
    case IN_LONG:
        if (in_plot(x, y) && !s_pan && !s_refdrag) {
            s_m.mk = MK_OFF;  // a hold: no marker
            input_tap_cancel();
        }
        break;
    default:
        break;
    }
}

// The pinch (two fingers on the plot): the span changes, the frequency under the middle of the fingers stays
static void pinch_update(void)
{
    int16_t px[2], py[2];
    int n = input_points(px, py, 2);
    if (sysui_panel_on()) {
        n = 0;
    }
    if (n == 2) {
        int x0, y0, x1, y1;
        gfx_touch_to_logical(px[0], py[0], &x0, &y0);
        gfx_touch_to_logical(px[1], py[1], &x1, &y1);
        double d = fabs((double)(x1 - x0)), mid = (x0 + x1) * 0.5;
        if (d < 12) {
            d = 12;
        }
        if (!s_pinch) {
            if (!in_plot((int)mid, (y0 + y1) / 2)) {
                return;
            }
            s_pinch = true;
            s_pan = s_refdrag = false;
            s_pinch_d0 = d;
            s_pinch_f0 = x_to_hz(mid);
            s_pinch_span0 = s_m.span;
            s_pinch_last_s = (double)s_m.span;
            s_pinch_last_c = (double)s_m.center;
            input_tap_cancel();
            return;
        }
        double span = (double)s_pinch_span0 * s_pinch_d0 / d;
        double c = s_pinch_f0 - ((mid - s_PX0) / s_bins - 0.5) * span;
        // a change against the LAST REQUEST (not the model: the engine clamps the model to the source, and a clamped
        // value would ask again on each update: the model would change all the time and no frame would come out)
        if (fabs(span - s_pinch_last_s) > s_pinch_last_s * 0.01 || fabs(c - s_pinch_last_c) > s_pinch_last_s * 0.005) {
            s_pinch_last_s = span;
            s_pinch_last_c = c;
            model_axis((int64_t)llround(c), (int64_t)llround(span));
        }
    } else if (s_pinch) {
        s_pinch = false;
        s_down = false;
        input_tap_cancel();
        store_set("sdr_c", (uint32_t)(s_m.center / 1000));
        store_set("sdr_s", (uint32_t)(s_m.span / 1000));
    }
}

// ================================================================ the panel
enum {
    PI_LANG,
    PI_SRC,
    PI_GAIN,
    PI_TRC,
    PI_AVG,
    PI_HOLD,
    PI_RBW,
    PI_WIN,
    PI_DET,
    PI_CMAP,
    PI_RNG,
    PI_WFS,
    PI_PK,
    PI_MK,
    PI_BAND,
    PI_AUTO,
    PI_FREEZE,
    PI_ENTER
};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const TRC_VALS[] = {"LIVE + MAX", "חי + מרבי", "LIVE + AVERAGE", "חי + ממוצע",
                                       "ALL",        "הכול",      "LIVE",           "חי"};
static const char *const AVG_VALS[] = {"4 FRAMES", "4 תמונות", "16 FRAMES", "16 תמונות", "64 FRAMES", "64 תמונות"};
static const char *const HOLD_VALS[] = {"HOLD", "נשאר", "FALL", "יורד"};
static const char *const RBW_VALS[] = {"AUTO", "אוטומטי", "FINE", "עדין", "COARSE", "גס"};
static const char *const WIN_VALS[] = {"HANN", "HANN", "BLACKMAN", "BLACKMAN", "FLAT TOP", "FLAT TOP"};
static const char *const DET_VALS[] = {"PEAK", "שיא", "AVERAGE", "ממוצע"};
static const char *const CMAP_VALS[] = {"TURBO", "TURBO", "CLASSIC", "קלאסי", "GRAY", "אפור", "EMBER", "גחלת"};
static const char *const RNG_VALS[] = {"40 dB", "40 dB", "60 dB", "60 dB", "80 dB", "80 dB", "100 dB", "100 dB"};
static const char *const WFS_VALS[] = {"FAST", "מהיר", "NORMAL", "רגיל", "SLOW", "איטי"};
static const char *const ONOFF_VALS[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const MK_VALS[] = {"OFF", "כבוי", "FOLLOW THE PEAK", "עוקב אחרי השיא", "WHERE I TAP", "איפה שנגעתי"};
static pitem_t s_items[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, "sdr_lang"},
        {PI_PICK, PI_SRC, 2, 0, {"SOURCE", "מקור"}, s_src_vals, NULL},
        {PI_PICK, PI_GAIN, 4, 0, {"GAIN", "הגבר"}, s_gain_vals, NULL},
        {PI_PICK, PI_TRC, 4, 0, {"TRACES", "קווים"}, TRC_VALS, "sdr_trc"},
        {PI_PICK, PI_AVG, 3, 1, {"AVERAGE", "ממוצע"}, AVG_VALS, "sdr_avg"},
        {PI_PICK, PI_HOLD, 2, 0, {"MAX HOLD", "החזקת מרבי"}, HOLD_VALS, "sdr_hold"},
        {PI_PICK, PI_RBW, 3, 0, {"RESOLUTION (RBW)", "רזולוציה RBW"}, RBW_VALS, "sdr_rbw"},
        {PI_PICK, PI_WIN, 3, 0, {"FFT WINDOW", "חלון FFT"}, WIN_VALS, "sdr_win"},
        {PI_PICK, PI_DET, 2, 0, {"DETECTOR", "גלאי"}, DET_VALS, "sdr_det"},
        {PI_PICK, PI_CMAP, 4, 0, {"COLOURS", "צבעים"}, CMAP_VALS, "sdr_cmap"},
        {PI_PICK, PI_RNG, 4, 2, {"RANGE", "טווח"}, RNG_VALS, "sdr_rng"},
        {PI_PICK, PI_WFS, 3, 1, {"WATERFALL SPEED", "מהירות המפל"}, WFS_VALS, "sdr_wfs"},
        {PI_PICK, PI_PK, 2, 0, {"PEAK LABELS", "תוויות שיא"}, ONOFF_VALS, "sdr_pk"},
        {PI_PICK, PI_MK, 3, 1, {"MARKER", "סמן"}, MK_VALS, NULL},
        {PI_ACT, PI_BAND, 0, 0, {"THE FULL BAND", "כל הפס"}, NULL, NULL},
        {PI_ACT, PI_AUTO, 0, 0, {"AUTO LEVEL", "רמה אוטומטית"}, NULL, NULL},
        {PI_ACT, PI_FREEZE, 0, 0, {"FREEZE / RUN", "הקפאה / הפעלה"}, NULL, NULL},
        {PI_ACT, PI_ENTER, 0, 0, {"ENTER A FREQUENCY", "הקלדת תדר"}, NULL, NULL},
};
#define NITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))
// GAIN: AUTO and the steps of the source (at most 3: the panel shows 4 values)
static void gain_item_update(const rfsrc_caps_t *c)
{
    s_items[2].n = (uint8_t)(1 + (c->ngains < 3 ? c->ngains : 3));
}
static bool item_shown(int id)
{
    if (id == PI_SRC) {
        return s_nsrc > 1;
    }
    if (id == PI_GAIN) {
        return cur_caps() && cur_caps()->ngains > 1;  // (AUTO and the steps)
    }
    if (id == PI_RBW || id == PI_WIN || id == PI_DET) {
        return cur_kind() == RFSRC_IQ;
    }
    return true;
}
static int item_get(int id)
{
    if (id == PI_SRC) {
        return s_m.src;
    }
    if (id == PI_GAIN) {
        return s_m.agc ? 0 : s_m.gain + 1;
    }
    if (id == PI_MK) {
        return s_m.mk;
    }
    return 0;
}
static void cmap_make(void);
static void item_set(int id, int v)
{
    switch (id) {
    case PI_LANG:
        s_he = v == 1;
        break;
    case PI_SRC:
        select_source(v);
        break;
    case PI_GAIN:
        lock();
        s_m.agc = v == 0;
        if (v > 0) {
            s_m.gain = (uint8_t)(v - 1);
        }
        model_changed();
        unlock();
        store_set("sdr_gain", (uint32_t)v);
        break;
    case PI_TRC:
        s_m.trc = (uint8_t)(v & 3);
        break;
    case PI_AVG:
        lock();
        s_m.avg = (uint8_t)(v % 3);
        model_changed();
        unlock();
        break;
    case PI_HOLD:
        lock();
        s_m.hold = (uint8_t)(v & 1);
        model_changed();
        unlock();
        break;
    case PI_RBW:
        lock();
        s_m.rbw = (uint8_t)(v % 3);
        model_changed();
        unlock();
        break;
    case PI_WIN:
        lock();
        s_m.win = (uint8_t)(v % 3);
        model_changed();
        unlock();
        break;
    case PI_DET:
        lock();
        s_m.det = (uint8_t)(v & 1);
        model_changed();
        unlock();
        break;
    case PI_CMAP:
        s_m.cmap = (uint8_t)(v & 3);
        cmap_make();
        break;
    case PI_RNG:
        lock();
        s_m.rng = (uint8_t)(v & 3);
        model_changed();
        unlock();
        break;
    case PI_WFS:
        s_m.wfs = (uint8_t)(v % 3);
        break;
    case PI_PK:
        s_m.pk = (uint8_t)(v & 1);
        break;
    case PI_MK:
        s_m.mk = (uint8_t)(v % 3);
        if (s_m.mk == MK_FIXED && s_mk_hz <= 0) {
            s_mk_hz = (double)s_m.center;
        }
        break;
    case PI_BAND:
        full_band();
        break;
    case PI_AUTO:
        auto_level();
        break;
    case PI_FREEZE:
        press_button(B_FREEZE);
        break;
    case PI_ENTER:
        kbd_start();
        break;
    }
}
// The panel takes the fingers while it shows: the app gets no IN_UP of a finger that was down (s_down would stay true:
// with no render limit, the LCD copy starves the engine). The panel opens: the touch of the app ends.
static void touch_reset(void)
{
    s_down = s_pan = s_refdrag = s_pinch = false;
    s_pressed = -1;
}
static void panel_opened(void)
{
    touch_reset();
}
static const pitems_t PANEL = {s_items, NITEMS, item_shown, item_get, item_set, panel_opened, panel_opened};

// ================================================================ enter / leave
static void bufs_free(void)
{
    sdr_traces_free(&s_tr);
    sdr_wf_free(&s_wf);
    sdr_persist_free(&s_ps);
    free(s_fr);
    s_fr = NULL;
    free(s_wacc);
    s_wacc = NULL;
    free(s_scope);
    s_scope = NULL;
}
static void *ps(size_t n)
{
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}

static void layout(void)
{
    s_W = gfx_w;
    s_H = gfx_h;
    rl_ox = rl_oy = 0;
    rl_big = gfx_round() ? 2 : 0;
    s_TB = BAR ? 22 : 34;
    s_AX = BAR ? 12 : 13;
    s_BW = BAR ? 46 : 50;
    if (gfx_round()) {  // (the plot from the top cap to the keys, the width of the chord at its top)
        rl_oy = gfx_w * 21 / 100;
        rl_ox = gfx_w / 2 - rl_half(rl_oy + 6, 4);
        s_W = gfx_w - 2 * rl_ox;
        s_H = rl_t_bot() - rl_oy;
        s_TB = 0;
        s_AX = 18;
        s_BW = 0;
    }
    s_PX0 = 0;
    s_PX1 = s_W - s_BW;
    s_PY0 = s_TB;
    s_PY1 = s_H - s_AX;
    s_bins = s_PX1 - s_PX0;
    if (s_bins > SDR_MAX_BINS) {
        s_bins = SDR_MAX_BINS;
    }
}

static void enter(int64_t now)
{
    s_now = s_touch_t = now;
    layout();
    s_he = store_get("sdr_lang", 1) == 1;  // (Hebrew when nothing is saved)
    s_nsrc = rfsrc_enum(s_src, NSRC_MAX, false);
    for (int k = 0; k < s_nsrc; k++) {
        s_src_vals[2 * k] = s_src[k].caps->name[0];
        s_src_vals[2 * k + 1] = s_src[k].caps->name[1];
    }
    s_items[1].n = (uint8_t)(s_nsrc < 2 ? 2 : s_nsrc > 4 ? 4 : s_nsrc);
    memset(&s_m, 0, sizeof(s_m));
    int src = (int)store_get("sdr_src", 0);
    // a radio start opens the native radio (the start was for it)
    for (int k = 0; k < s_nsrc; k++) {
        if ((s_src[k].caps->flags & RFSRC_F_NOWIFI) && src_state(k) == RFSRC_READY) {
            src = k;
        }
    }
    if (src < 0 || src >= s_nsrc) {
        src = 0;
    }
    s_m.center = (int64_t)store_get("sdr_c", 2441750) * 1000;
    s_m.span = (int64_t)store_get("sdr_s", 84000) * 1000;
    {
        int g = (int)store_get("sdr_gain", 0);  // 0 = AUTO (the default), else the step + 1
        s_m.agc = g == 0;
        s_m.gain = (uint8_t)(g > 0 ? g - 1 : 1);
    }
    s_m.trc = (uint8_t)(store_get("sdr_trc", 0) & 3);
    s_m.avg = (uint8_t)(store_get("sdr_avg", 1) % 3);
    s_m.hold = (uint8_t)(store_get("sdr_hold", 0) & 1);
    s_m.rbw = (uint8_t)(store_get("sdr_rbw", 0) % 3);
    s_m.win = (uint8_t)(store_get("sdr_win", 0) % 3);
    s_m.det = (uint8_t)(store_get("sdr_det", 0) & 1);
    s_m.cmap = (uint8_t)(store_get("sdr_cmap", 0) & 3);
    s_m.rng = (uint8_t)(store_get("sdr_rng", 2) & 3);
    s_m.wfs = (uint8_t)(store_get("sdr_wfs", 1) % 3);
    s_m.pk = (uint8_t)(store_get("sdr_pk", 0) & 1);
    s_m.view = (uint8_t)(store_get("sdr_view", V_SPLIT) % V_N);
    s_m.ref = (float)store_get("sdr_ref", 200 - 20) - 200;
    if (s_m.ref > 40 || s_m.ref < -120) {  // (a damaged value: the default; the drag keeps -120 .. 40)
        s_m.ref = -20;
    }
    s_m.mk = MK_PEAK;
    s_mk_hz = 0;
    s_m.src = -1;
    s_nsrc ? select_source(src) : (void)0;
    s_toast[0] = 0;
    s_down = s_pan = s_refdrag = s_pinch = false;
    s_pressed = -1;
    s_tap_t = 0;
    s_kbd = false;
    s_agc_t = 0;
    cmap_make();
    tfd_screen();
    bool ok = sdr_traces_init(&s_tr, SDR_MAX_BINS) && sdr_wf_init(&s_wf, SDR_MAX_BINS, s_H) &&
              sdr_persist_init(&s_ps, s_bins, s_PY1 - s_PY0);
    s_fr = ps(sizeof(float) * SDR_MAX_BINS);
    s_wacc = ps(sizeof(float) * SDR_MAX_BINS);
    s_scope = ps(sizeof(int16_t) * 2 * SCOPE_N);
    ok = ok && s_fr && s_wacc && s_scope;
    s_eng_src = -1;
    s_eng_on = false;
    s_eng_err = 0;
    s_plan_gen = 0xFFFFFFFFu;
    memset(&s_plan, 0, sizeof(s_plan));
    s_npk = 0;
    s_floor = SDR_NODATA;
    s_over_t = -100000;
    s_fps = 0;
    s_seq = 0;
    s_auto_seq = 15;  // (AUTO LEVEL after the first frames)
    s_last_frame_t = 0;
    s_scope_n = 0;
    for (int g = 0; g < CH_MAX; g++) {
        s_busy[g] = 0;
        s_chpow[g] = SDR_NODATA;
    }
    sysui_panel_items(&PANEL);
    if (!ok) {
        toast("NO MEMORY");
        return;
    }
    s_quit = false;
    s_task_done = false;
#ifdef ESP_PLATFORM
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    if (xTaskCreatePinnedToCore(eng_task, "sdr", 8192, NULL, 3, &s_task, 1) != pdPASS) {
        s_task_done = true;
        toast("NO MEMORY");
    }
#else
    s_task_done = true;
#endif
}

static void leave(void)
{
    s_quit = true;
#ifdef ESP_PLATFORM
    for (int i = 0; i < 100 && !s_task_done; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#else
    engine_close();
#endif
    store_set("sdr_lang", s_he);
    store_set("sdr_c", (uint32_t)(s_m.center / 1000));
    store_set("sdr_s", (uint32_t)(s_m.span / 1000));
    store_set("sdr_ref", (uint32_t)(s_m.ref + 200));
    int k = s_m.src;
    bufs_free();
    // the native radio: the end of the radio start (a normal restart: the WiFi comes back)
    if (s_task_done && k >= 0 && k < s_nsrc && (s_src[k].caps->flags & RFSRC_F_NOWIFI) && src_state(k) == RFSRC_READY) {
        s_src[k].ops->unclaim(s_src[k].ctx);
    }
}

static uint32_t s_seq_drawn;  // the engine frame of the last render

static void update(int64_t now)
{
#ifdef ESP_PLATFORM
    // ONE RENDER A NEW FRAME: the LCD task copies each render to the screen on core 1, the core of the engine. With
    // no new frame and no finger, wait for the engine (at most about 50 ms: the clock and the toast go on).
    // (the fingers now from the touch task: a flag of the app can miss an IN_UP; no finger for a while = no touch)
    int16_t fx[2], fy[2];
    const bool finger = input_points(fx, fy, 2) > 0 && !sysui_panel_on();
    if (!finger && s_down && now - s_touch_t > 1000) {
        touch_reset();
    }
    for (int i = 0; i < 6 && s_seq == s_seq_drawn && !finger && now - s_touch_t > 250; i++) {
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    now = esp_timer_get_time() / 1000;
#endif
    s_now = now;
    pinch_update();
#ifndef ESP_PLATFORM
    eng_step();  // (the PC: one engine step a frame, no task)
#endif
    const rfsrc_caps_t *gc = cur_caps();
    if (s_m.agc && gc && gc->ngains > 1 && s_eng_on) {
        bool over = now - s_over_t < 400;
        if (over && s_m.gain > 0 && now - s_agc_t > 600) {
            lock();
            s_m.gain--;
            model_changed();
            unlock();
            s_agc_t = now;
        } else if (!over && s_m.gain + 1 < gc->ngains && now - s_over_t > 8000 && now - s_agc_t > 20000) {
            float mx = SDR_NODATA;
            lock();
            for (int i = 0; i < s_tr.n; i++) {
                if (s_tr.v[SDR_TR_LIVE][i] > mx) {
                    mx = s_tr.v[SDR_TR_LIVE][i];
                }
            }
            unlock();
            // (a step up gives about 14 dB: only when the strongest signal stays far under the full scale)
            if (mx > SDR_NODATA && mx + (gc->gain_db[s_m.gain + 1] - gc->gain_db[s_m.gain]) < -30) {
                lock();
                s_m.gain++;
                model_changed();
                unlock();
                s_agc_t = now;
            }
        }
    }
    if (s_auto_seq && s_seq >= s_auto_seq) {
        s_auto_seq = 0;
        auto_level();
    }
    // the marker that follows the highest peak
    if (s_m.mk == MK_PEAK && s_npk > 0 && s_plan.df_hz > 0) {
        lock();
        s_mk_hz = sdr_plan_freq(&s_plan, s_pk[0].x);
        unlock();
    }
}

static bool awake(void)
{
    return true;
}  // (a tool that one watches: the screen stays on)

#include "sdr_draw.inc"

const app_t app_sdr = {
        .name = APP_NAME,
        .landscape = true,
        .round = APP_ROUND_HI,  // (the round boards: the native round frame, the middle box of layout())
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
};
