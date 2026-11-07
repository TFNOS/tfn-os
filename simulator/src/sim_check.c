// The simulator: the checks of the sound memory of main/audio.c (simulator/sound_check.py calls tfnsim_check_audio()
// while the menu is open). A task of the firmware does them with the calls of tfn/audio.h and the two counts of
// main/audio.h (audio_mem_check with stop = false, audio_queued_refs). sim_audio.c holds the audio task after a block
// when the messages must wait in the queue. Each check writes one line: "ok   <what>" or "FAIL <what>".
//
// The checks: audio_stop_mem stops a sample voice (in the queue and while it plays), a tune, TIA steps, a capture and a
// record; a drop from the queue and a count keep the order of the other messages, and the count of the messages that
// point into memory is right; after audio_capture_stop no sample goes into the buffer; audio_set_tick(NULL) and
// audio_mic_tap(NULL) wait for a call that runs, and the old function gets no more calls (a slow tick and a slow tap,
// five times each).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sim.h"

#define CK_GROUP 250      // the voice group of the checks
#define CK_SMP   4800     // the samples of the voice (0.2 s)
#define CK_CAP   48000    // the samples of the capture and of the record buffer (2 s)
#define CK_SLOW  8000     // the time of a slow tick or tap (us): longer than a block

static char s_out[6000];
static int s_out_n, s_fails;
static volatile bool s_done;

// the notes: the second note of each tune starts after 3 s, so the tune stays in place while the checks look at it
static const tune_note_t CK_TA[2] = {{0, 40, 60, 0, W_SINE, 1}, {3000, 40, 62, 0, W_SINE, 1}};
static const tune_note_t CK_TC[2] = {{0, 40, 64, 0, W_SINE, 1}, {3000, 40, 65, 0, W_SINE, 1}};
static const tia_step_t CK_TIA[2] = {{30000, 4, 10, 1}, {30000, 4, 12, 1}};

static void ck_line(const char *fmt, ...)
{
    if (s_out_n >= (int)sizeof(s_out) - 2) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(s_out + s_out_n, sizeof(s_out) - (size_t)s_out_n - 1, fmt, ap);
    va_end(ap);
    if (k > 0) {
        s_out_n += k;
        s_out_n = s_out_n < (int)sizeof(s_out) - 2 ? s_out_n : (int)sizeof(s_out) - 2;
        s_out[s_out_n++] = '\n';
        s_out[s_out_n] = 0;
    }
}

static void ck(bool ok, const char *what)
{
    if (!ok) {
        s_fails++;
    }
    ck_line("%s %s", ok ? "ok  " : "FAIL", what);
}

static void ck_ms(int ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
}

static int users(const void *p, size_t n)
{
    return audio_mem_check(p, n, false);
}

// the audio task waits after its block (true) or goes on (false)
static void ck_hold(bool on)
{
    sim_audio_hold(on);
    for (int i = 0; on && i < 400 && !sim_audio_parked(); i++) {
        ck_ms(5);
    }
}

static audio_voice_t ck_voice(const int16_t *smp)
{
    audio_voice_t v = {0};
    v.dur = 0xF0000000u;
    v.wave = W_SAMPLE;
    v.vol = 1;
    v.attack_ms = 2;
    v.shape = AUDIO_SUSTAIN;
    v.group = CK_GROUP;
    v.sample = smp;
    v.sample_len = CK_SMP;
    v.rate = 1.0f;
    v.loop = true;
    return v;
}

// the end of a hold: the audio task takes the messages of the queue, and the count goes back to r0
static void ck_release(uint32_t r0)
{
    ck_hold(false);
    for (int i = 0; i < 200 && audio_queued_refs() != r0; i++) {
        ck_ms(5);
    }
    ck(audio_queued_refs() == r0, "the audio task takes the messages: the count goes back");
}

// the queue: a drop, the order of the other messages, the count. (Each count with audio_mem_check also takes the
// messages out of the queue and puts them back: so each check of the order has one look at the queue only, and a
// look that changes the order cannot hide in a second one.)
static void check_queue(const int16_t *smp, uint32_t r0)
{
    const size_t sb = CK_SMP * sizeof(int16_t);
    const audio_voice_t v = ck_voice(smp);
    ck_hold(true);
    ck(sim_audio_parked(), "the audio task waits after its block (the hold of the checks)");
    audio_play(&v);
    ck(audio_queued_refs() == r0 + 1 && users(smp, sb) == 1, "a voice in the queue counts");
    audio_stop_mem(smp, sb);
    ck(audio_queued_refs() == r0 && users(smp, sb) == 0, "audio_stop_mem drops the voice from the queue");
    // the drop keeps the order of the other messages
    audio_tune(CK_TA, 2);
    audio_play(&v);
    audio_tune(CK_TC, 2);
    ck(audio_queued_refs() == r0 + 3, "three messages in the queue point into memory");
    audio_stop_mem(smp, sb);
    ck(audio_queued_refs() == r0 + 2, "the drop of the voice keeps the two tunes");
    ck_release(r0);
    ck(users(CK_TC, sizeof CK_TC) == 1 && users(CK_TA, sizeof CK_TA) == 0,
       "the order stays after a drop: the tune of the last message plays");
    audio_stop_mem(CK_TC, sizeof CK_TC);
    ck(users(CK_TC, sizeof CK_TC) == 0, "audio_stop_mem stops the tune");
    // a count keeps the order
    ck_hold(true);
    audio_tune(CK_TA, 2);
    audio_tune(CK_TC, 2);
    ck(users(CK_TA, sizeof CK_TA) == 1, "a tune in the queue counts");
    ck_release(r0);
    ck(users(CK_TC, sizeof CK_TC) == 1 && users(CK_TA, sizeof CK_TA) == 0,
       "the order stays after a count: the tune of the last message plays");
    audio_stop_mem(CK_TC, sizeof CK_TC);
}

// the sounds that play: a voice, TIA steps
static void check_play(const int16_t *smp, uint32_t r0)
{
    const size_t sb = CK_SMP * sizeof(int16_t);
    const audio_voice_t v = ck_voice(smp);
    audio_play(&v);
    for (int i = 0; i < 200 && (audio_queued_refs() != r0 || users(smp, sb) != 1); i++) {
        ck_ms(5);
    }
    ck(users(smp, sb) == 1 && audio_queued_refs() == r0, "the voice plays");
    audio_stop_mem(smp, sb);
    ck(users(smp, sb) == 0, "audio_stop_mem stops the voice");
    ck_ms(30);
    ck(users(smp, sb) == 0, "the voice does not come back");
    audio_tia_loop(0, CK_TIA, 2, 200);
    for (int i = 0; i < 200 && (audio_queued_refs() != r0 || users(CK_TIA, sizeof CK_TIA) != 1); i++) {
        ck_ms(5);
    }
    ck(users(CK_TIA, sizeof CK_TIA) == 1 && audio_queued_refs() == r0, "the TIA steps play");
    audio_stop_mem(CK_TIA, sizeof CK_TIA);
    ck(users(CK_TIA, sizeof CK_TIA) == 0, "audio_stop_mem stops the TIA steps");
    audio_tia_off();
}

static bool all_bytes(const int16_t *b, size_t n, uint8_t x)
{
    const uint8_t *p = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != x) {
            return false;
        }
    }
    return true;
}

// the buffers that the sound writes: the capture, the record
static void check_buffers(int16_t *buf)
{
    const size_t bb = CK_CAP * sizeof(int16_t);
    audio_capture(buf, CK_CAP, 0);
    ck(audio_capturing() && users(buf, bb) == 1, "a capture counts");
    audio_stop_mem(buf, bb);
    ck(!audio_capturing() && users(buf, bb) == 0, "audio_stop_mem stops the capture");
    memset(buf, 0x5A, bb);
    ck_ms(30);
    ck(all_bytes(buf, bb, 0x5A), "no sample goes into the buffer after the stop");
    audio_capture(buf, CK_CAP, 0);
    for (int i = 0; i < 200 && audio_captured() == 0; i++) {
        ck_ms(5);
    }
    audio_capture_stop();
    memset(buf, 0x5A, bb);
    ck_ms(30);
    ck(audio_captured() > 0 && all_bytes(buf, bb, 0x5A), "no sample goes into the buffer after audio_capture_stop");
    audio_mic_record(buf, CK_CAP);
    ck(audio_mic_recording() && users(buf, bb) == 1, "a record counts");
    audio_stop_mem(buf, bb);
    ck(!audio_mic_recording() && users(buf, bb) == 0, "audio_stop_mem stops the record");
}

static volatile int s_tk_in, s_tk_out, s_tp_in, s_tp_out;
static void slow_tick(void)
{
    s_tk_in++;
    sim_sleep_us(CK_SLOW);
    s_tk_out++;
}
static void slow_tap(const int16_t *s, int n)
{
    (void)s;
    (void)n;
    s_tp_in++;
    sim_sleep_us(CK_SLOW);
    s_tp_out++;
}

// the barriers: audio_set_tick(NULL) and audio_mic_tap(NULL) wait for a call that runs; no call comes after them
static void check_barriers(void)
{
    bool tick_ok = true, tap_ok = true;
    for (int r = 0; r < 5; r++) {
        const int start = s_tk_in;
        audio_set_tick(slow_tick);
        for (int i = 0; i < 200 && s_tk_in < start + 2; i++) {
            ck_ms(2);
        }
        audio_set_tick(NULL);
        const int in = s_tk_in, out = s_tk_out;
        ck_ms(30);
        tick_ok = tick_ok && in >= start + 2 && in == out && s_tk_in == in;
    }
    ck(tick_ok, "audio_set_tick(NULL) waits for the tick that runs, and the tick gets no more calls");
    audio_mic_start();
    for (int r = 0; r < 5; r++) {
        const int start = s_tp_in;
        audio_mic_tap(slow_tap);
        for (int i = 0; i < 300 && s_tp_in < start + 2; i++) {
            ck_ms(2);
        }
        audio_mic_tap(NULL);
        const int in = s_tp_in, out = s_tp_out;
        ck_ms(40);
        tap_ok = tap_ok && in >= start + 2 && in == out && s_tp_in == in;
    }
    audio_mic_stop();
    ck(tap_ok, "audio_mic_tap(NULL) waits for the tap that runs, and the tap gets no more calls");
}

static void check_task(void *arg)
{
    (void)arg;
    int16_t *smp = calloc(CK_SMP, sizeof(int16_t));
    int16_t *buf = calloc(CK_CAP, sizeof(int16_t));
    if (smp && buf) {
        for (int i = 0; i < CK_SMP; i++) {
            smp[i] = (int16_t)((i % 48) * 64 - 1536);  // (a quiet saw)
        }
        const uint32_t r0 = audio_queued_refs();
        check_queue(smp, r0);
        check_play(smp, r0);
        check_buffers(buf);
        check_barriers();
        ck(audio_queued_refs() == r0, "at the end the count of the messages is the same as at the start");
    } else {
        ck(false, "no memory for the checks");
    }
    sim_audio_hold(false);
    free(smp);
    free(buf);
    s_done = true;
    vTaskDelete(NULL);
}

// Runs the checks (at most 30 s); out gets the lines. The result: the count of the failed checks, -1 = no result.
SIM_API int tfnsim_check_audio(char *out, int cap)
{
    s_out_n = 0;
    s_out[0] = 0;
    s_fails = 0;
    s_done = false;
    if (xTaskCreate(check_task, "sim_check", 8192, NULL, 3, NULL) != pdPASS) {
        snprintf(out, (size_t)cap, "the task of the checks did not start");
        return -1;
    }
    for (int i = 0; i < 3000 && !s_done; i++) {
        sim_os_sleep_us(10000);
    }
    snprintf(out, (size_t)cap, "%s", s_out);
    return s_done ? s_fails : -1;
}
