// Sound: a small synthesizer that feeds the ES8311 codec over I2S.
//
// - 32 voices: square, pulse, triangle, saw, noise; pitch sweep; attack;
//   a decay envelope (effects, drums) or a sustain envelope (pads);
//   a one-pole low-pass filter; voice groups that can be released together.
// - Sound effects come from other tasks through a queue.
// - A tick callback (music.c) runs in the audio task before each 5 ms block
//   and can start voices with sample-exact delays.
// - Two drone channels give continuous tones (engine, jetpack).
// - Sample voices play recorded sound at any pitch.
// - The TIA voice: the two sound channels of the Atari 2600 chip, with their register steps (JUNGLE RUN).
// - The microphone (ES7210 on the same I2S port, DIN 6) runs in its own task.
//
// I2S: MCLK 7, BCLK 15, WS 46, DOUT 45, DIN 6. ES8311 and ES7210 on I2C0.
// CoreS3-SE: the AW88298 amplifier in place of the ES8311, and the ES7210 (MIC1, MIC2).
// I2S: MCLK 0, BCLK 34, WS 33, DOUT 13, DIN 14.

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"  // (xTaskCreatePinnedToCoreWithCaps: the mic task)
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "board.h"
#include "audio.h"
#include "settings.h"

static const char *TAG = "audio";

// the I2S pins and the microphone of the board (boards/<kind>/board_caps.h): MIC_SHARED = the ES8311 of the speaker
// is the microphone too, MIC_ES7243 = an ES7243 (or ES7243E), else an ES7210 with the inputs MIC_SELECT
#define PIN_I2S_MCLK BOARD_I2S_MCLK
#define PIN_I2S_BCLK BOARD_I2S_BCLK
#define PIN_I2S_WS   BOARD_I2S_WS
#define PIN_I2S_DOUT BOARD_I2S_DOUT
#define PIN_I2S_DIN  BOARD_I2S_DIN
#define MIC_SHARED   BOARD_MIC_SHARED
#define MIC_ES7243   BOARD_MIC_ES7243
#define MIC_SELECT   BOARD_MIC_SELECT
#define MIC_FRAMES   240  // 10 ms

#define SAMPLE_RATE  AUDIO_SAMPLE_RATE
#define BLOCK_FRAMES AUDIO_BLOCK
#define MAX_VOICES   32
#define N_DRONES     2
#define QUEUE_LEN    24  // the messages of the queue

typedef struct {
    uint16_t delay_ms;
    uint16_t dur_ms;
    uint16_t f0;  // start frequency, Hz
    uint16_t f1;  // end frequency, Hz (sweep)
    uint8_t wave;
    uint8_t vol;  // 0..100
} note_t;

#define END {0, 0, 0, 0, 0, 0}

// note frequencies (Hz)
#define A4 440
#define B4 494
#define C5 523
#define D5 587
#define E5 659
#define G5 784
#define A5 880
#define C6 1047
#define E6 1319
#define G6 1568
#define C7 2093
#define E7 2637
#define G7 3136
#define G4 392
#define E4 330
#define C4 262
#define B5 988

static const note_t SFX_DEFS[SFX_COUNT][10] = {
        [SFX_CLICK] = {{0, 14, 2200, 1800, W_PULSE, 22}, END},
        [SFX_BACK] = {{0, 60, G5, G5, W_PULSE, 20}, {60, 90, C5, C5, W_PULSE, 20}, END},
        [SFX_JUMP] = {{0, 110, 280, 720, W_SQUARE, 20}, END},
        [SFX_JUMP2] = {{0, 110, 520, 1200, W_SQUARE, 18}, END},
        [SFX_COIN] = {{0, 60, B5, B5, W_PULSE, 22}, {60, 180, E6, E6, W_PULSE, 22}, END},
        [SFX_DIE] = {{0, 450, 900, 90, W_SQUARE, 26}, {0, 250, 3000, 500, W_NOISE, 30}, END},
        [SFX_STOMP] = {{0, 80, 520, 160, W_SQUARE, 24}, {0, 40, 2500, 800, W_NOISE, 20}, END},
        [SFX_SPRING] = {{0, 260, 180, 900, W_TRI, 80}, {0, 220, 300, 1300, W_PULSE, 14}, END},
        [SFX_POWERUP] = {{0, 45, C6, C6, W_PULSE, 22},
                         {45, 45, E6, E6, W_PULSE, 22},
                         {90, 45, G6, G6, W_PULSE, 22},
                         {135, 160, C7, C7, W_PULSE, 22},
                         END},
        [SFX_SHIELD_HIT] = {{0, 180, 1200, 200, W_SQUARE, 22}, {0, 140, 6000, 1000, W_NOISE, 30}, END},
        [SFX_CRUMBLE] = {{0, 260, 1400, 250, W_NOISE, 28}, {0, 200, 120, 60, W_TRI, 60}, END},
        [SFX_BUMP] = {{0, 35, 160, 90, W_TRI, 70}, END},
        [SFX_HOLE] = {{0, 600, 700, 60, W_TRI, 80}, {0, 600, 1400, 120, W_PULSE, 12}, END},
        [SFX_CHECKPOINT] = {{0, 80, E6, E6, W_TRI, 70}, {80, 160, G6, G6, W_TRI, 70}, END},
        [SFX_WIN] = {{0, 90, C6, C6, W_PULSE, 22},
                     {100, 90, E6, E6, W_PULSE, 22},
                     {200, 90, G6, G6, W_PULSE, 22},
                     {300, 400, C7, C7, W_PULSE, 22},
                     {300, 400, C5, C5, W_TRI, 70},
                     END},
        [SFX_PLACE] = {{0, 50, 240, 140, W_TRI, 80}, {0, 20, 4000, 4000, W_NOISE, 15}, END},
        [SFX_CUT] = {{0, 90, 6000, 800, W_NOISE, 30}, {0, 60, 200, 120, W_TRI, 60}, END},
        [SFX_MOVE] = {{0, 16, 1500, 1300, W_PULSE, 18}, END},
        [SFX_ROTATE] = {{0, 45, 620, 1250, W_SQUARE, 26}, END},
        [SFX_SOFT_DROP] = {{0, 12, 420, 300, W_TRI, 30}, END},
        [SFX_LOCK] = {{0, 60, 190, 90, W_TRI, 80}, {0, 25, 3000, 3000, W_NOISE, 18}, END},
        [SFX_HARD_DROP] = {{0, 120, 300, 45, W_SQUARE, 34},
                           {0, 90, 5000, 1500, W_NOISE, 40},
                           {0, 110, 110, 50, W_TRI, 90},
                           END},
        [SFX_LINE1] = {{0, 70, C6, C6, W_SQUARE, 26}, {70, 120, E6, E6, W_SQUARE, 26}, END},
        [SFX_LINE2] = {{0, 60, C6, C6, W_SQUARE, 26},
                       {60, 60, E6, E6, W_SQUARE, 26},
                       {120, 130, G6, G6, W_SQUARE, 26},
                       END},
        [SFX_LINE3] = {{0, 55, C6, C6, W_SQUARE, 26},
                       {55, 55, E6, E6, W_SQUARE, 26},
                       {110, 55, G6, G6, W_SQUARE, 26},
                       {165, 150, C7, C7, W_SQUARE, 24},
                       END},
        [SFX_4LINES] = {{0, 50, C6, C6, W_SQUARE, 26},
                        {50, 50, E6, E6, W_SQUARE, 26},
                        {100, 50, G6, G6, W_SQUARE, 26},
                        {150, 50, C7, C7, W_SQUARE, 24},
                        {200, 50, E7, E7, W_SQUARE, 22},
                        {250, 380, G7, G7, W_PULSE, 20},
                        {250, 380, C5, C5, W_TRI, 70},
                        {250, 380, 2000, 400, W_NOISE, 10},
                        END},
        [SFX_LEVEL_UP] = {{0, 60, G5, G5, W_PULSE, 24},
                          {60, 60, C6, C6, W_PULSE, 24},
                          {120, 60, E6, E6, W_PULSE, 24},
                          {180, 180, G6, G6, W_PULSE, 24},
                          {180, 180, G4, G4, W_TRI, 60},
                          END},
        [SFX_GAME_OVER] = {{0, 180, G5, G5, W_SQUARE, 26},
                           {200, 180, E5, E5, W_SQUARE, 26},
                           {400, 180, C5, C5, W_SQUARE, 26},
                           {600, 500, G4, 180, W_SQUARE, 26},
                           {600, 600, C4, 60, W_TRI, 80},
                           END},
        [SFX_START] =
                {// the start tune
                 {0, 140, E5, E5, W_PULSE, 24},
                 {150, 70, B4, B4, W_PULSE, 24},
                 {225, 70, C5, C5, W_PULSE, 24},
                 {300, 140, D5, D5, W_PULSE, 24},
                 {450, 70, C5, C5, W_PULSE, 24},
                 {525, 70, B4, B4, W_PULSE, 24},
                 {600, 200, A4, A4, W_PULSE, 24},
                 {0, 300, E4, E4, W_TRI, 50},
                 {300, 500, A4 / 2, A4 / 2, W_TRI, 50},
                 END},
        [SFX_BOING] = {{0, 140, 220, 660, W_TRI, 80}, {0, 90, 440, 1300, W_PULSE, 10}, END},
        [SFX_SHOOT] = {{0, 60, 1800, 600, W_PULSE, 18}, END},
        [SFX_FALL] = {{0, 900, 900, 80, W_SQUARE, 22}, END},
        [SFX_CRASH] = {{0, 350, 3000, 300, W_NOISE, 45}, {0, 250, 140, 40, W_TRI, 90}, END},
        [SFX_BEEP] = {{0, 120, A5, A5, W_PULSE, 22}, END},
        [SFX_GO] = {{0, 400, 1760, 1760, W_PULSE, 22}, {0, 400, A5, A5, W_TRI, 60}, END},
        [SFX_GIGGLE] = {{0, 60, 700, 900, W_TRI, 70},
                        {70, 60, 750, 950, W_TRI, 70},
                        {140, 80, 800, 1000, W_TRI, 70},
                        END},
        [SFX_WHOA] = {{0, 200, 300, 600, W_SQUARE, 18}, {200, 260, 600, 330, W_SQUARE, 18}, END},
        [SFX_SCREAM] = {{0, 500, 1400, 900, W_SAW, 20}, {0, 400, 5000, 2000, W_NOISE, 15}, END},
        [SFX_TICK] = {{0, 10, 3000, 3000, W_PULSE, 16}, END},
        [SFX_STAR] = {{0, 70, E6, E6, W_TRI, 70}, {60, 70, G6, G6, W_TRI, 70}, {120, 150, C7, C7, W_TRI, 60}, END},
        [SFX_SWITCH] = {{0, 150, 400, 1600, W_PULSE, 16}, {0, 150, 800, 3200, W_TRI, 40}, END},
        [SFX_FLAP] = {{0, 40, 500, 900, W_TRI, 60}, END},
};

typedef struct {
    bool on;
    uint8_t wave, shape, group;
    uint32_t delay;  // samples before start
    uint32_t len;    // samples
    uint32_t pos;
    uint32_t attack;   // samples
    uint32_t release;  // samples (sustain shape: the fade at the end)
    // No division in the sample loop: the progress (0..1 = 0..2^32) goes up by
    // prog_step each sample; att_k and rel_k are 2^32 / attack and 2^32 / release.
    uint32_t prog, prog_step;
    uint32_t att_k, rel_k;
    uint32_t phase;  // 32-bit phase accumulator
    uint32_t inc0, inc1;
    int32_t amp;   // peak amplitude
    int32_t lp_a;  // 0 = off, else filter factor / 65536
    int32_t lp_y;
    int32_t hp_a;  // 0 = off
    int32_t hp_y;
    int16_t noise;
    const int16_t *smp;
    uint32_t smp_len;
    uint32_t smp_idx;   // the sample (a full 32-bit index: a clip can be longer than 65536 samples = 2.7 s)
    uint32_t smp_frac;  // the part between smp_idx and the next sample, 0..65535
    uint32_t smp_inc;   // 16.16
    bool smp_loop;
} voice_t;

typedef struct {
    float hz, hz_target;
    float vol, vol_target;
    uint8_t wave;
    uint32_t phase;
    int32_t lp_y;
    int16_t noise;
} drone_t;

typedef struct {
    uint8_t id;  // sfx_t, SFX_COUNT for a free tone, SFX_COUNT + 1 for a full voice,
                 // SFX_COUNT + 2 for a tune (NULL = stop), SFX_COUNT + 3 to stop a group
                 // (tune_n = the group), SFX_COUNT + 4 for the TIA voice
    note_t tone;
    union {  // (the TIA command shares the space of the voice: the queue items keep their size)
        audio_voice_t voice;
        struct {
            const tia_step_t *steps;
            uint32_t skip;
            uint16_t n;
            uint8_t ch;     // 0, 1; 0xFF = stop both
            uint8_t times;  // 0 = play one time (always a new start), else a loop
        } tia;
    };
    const tune_note_t *tune;
    uint16_t tune_n;
} sfx_msg_t;

static esp_codec_dev_handle_t s_dev;
static QueueHandle_t s_queue;
static TaskHandle_t s_audio_task;
static volatile bool s_sleep_want, s_asleep;  // audio_sleep(); the codec is closed (the audio task sets it)
static volatile bool s_mic_open;              // (the mic task, below)
static int s_vol = -1;                        // the last codec volume 0..100 (it goes again to the codec after a sleep)
static volatile int32_t s_boost_q8 = 256;     // the gain of a VOLUME over 100 (Q8; audio_set_volume)
static volatile uint32_t s_peak_max;          // the highest peak of the mix before the boost (audio_peak_take)
static voice_t s_voices[MAX_VOICES];
static drone_t s_drone[N_DRONES];
static volatile bool s_muted;
static int16_t *volatile s_cap_buf;
static volatile uint32_t s_load_us;   // synth time of the last blocks (filtered)
static volatile uint32_t s_blocks;    // blocks made (audio_kill_group waits on it)
static volatile uint32_t s_underrun;  // the underruns of the speaker DMA (on_send_q_ovf)
static int64_t s_ret;                 // the time of the last block write (0 after a sleep of the codec)
static volatile uint32_t s_cap_max, s_cap_n, s_cap_skip;
static uint32_t s_lfsr = 0xACE1u;
static void (*volatile s_tick)(void);
// the tune that plays now (audio task only)
static const tune_note_t *s_tune;
static int s_tune_n;
static uint32_t s_tune_pos;  // samples from the start of the tune to this block
static int s_tune_k = 100;   // the volume of the notes of this tune, percent (tune_scale)

// THE LOCK of the sounds that read or write the memory of an app: the audio task holds it while it makes a block (the
// reads of the queue, the tune, the tick, the mix of the voices, the copy to the capture buffer); the mic task holds it
// while it copies into the record buffer. Neither task waits while it holds the lock (not on the codec, not in the
// sleep of the sound), so a task that takes it waits one block at most. audio_stop_mem takes it to stop the sounds
// that use a memory block; audio_set_tick and the stops of a capture or a record take it as a barrier. (So a tick must
// not wait for a task: that task can wait for this lock in a free.)
static SemaphoreHandle_t s_lock;
// the messages in the queue that point into memory (a sample voice, a tune, TIA steps): each one counts from its send
// to its use, and [s_q_lo, s_q_hi) holds the memory of all of them (empty again when the count is 0). So
// audio_mem_check takes the lock only when such a message can point into the block or a sound uses it. s_q_mux keeps
// the three values together (a short spin lock: a send from any task, the use in the audio task, a check).
static portMUX_TYPE s_q_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_q_refs;
static uintptr_t s_q_lo = UINTPTR_MAX, s_q_hi;

static bool in_audio_task(void)
{
    return s_audio_task && xTaskGetCurrentTaskHandle() == s_audio_task;
}
static void lock_take(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}
static void lock_give(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}
// the lock for a call from any task: true = taken here (give it with lock_out). The audio task gets false: it calls
// in from a tick, with the lock already in its hold.
static bool lock_in(void)
{
    if (!s_lock || in_audio_task()) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    return true;
}
static void lock_out(bool held)
{
    if (held) {
        xSemaphoreGive(s_lock);
    }
}
// a barrier: when it returns, the audio task and the mic task are out of their part with the lock, so a value that the
// caller changed before it is the value that they use from now on (the audio task itself has the lock: no wait)
static void lock_barrier(void)
{
    lock_out(lock_in());
}

// The peak of a tune: the most volume that sounds at one time (the notes that overlap; a held note to its end, a
// decay note for its first quarter). One voice gives up to 400 a volume step at the output (after the x 2 of the mix),
// about 270 in a real chord (the waves do not peak at the same time), and the output limits softly above 24000.
// (Measured peaks in the simulator: the opening chord of SUNSET 131 after the 4/5, the FIGHT tunes 160, the alarm
// chime 180. A louder start sounds distorted.) A tune above
// TUNE_PEAK plays softer, all its notes by the same part, so it stays clean.
#define TUNE_PEAK 85
static int tune_scale(const tune_note_t *t, int n)
{
    int peak = 0;
    for (int i = 0; i < n; i++) {
        int sum = 0;
        for (int j = 0; j < n; j++) {
            int d = (t[j].wave & TUNE_HOLD) ? t[j].dur : t[j].dur / 4;
            if (d < 30) {
                d = 30;
            }
            if (t[j].t <= t[i].t && t[i].t < t[j].t + d) {
                sum += t[j].vol * 4 / 5;
            }
        }
        if (sum > peak) {
            peak = sum;
        }
    }
    return peak > TUNE_PEAK ? TUNE_PEAK * 100 / peak : 100;
}

static uint32_t hz_to_inc(float hz)
{
    if (hz < 0) {
        hz = 0;
    }
    return (uint32_t)(hz * (4294967296.0f / SAMPLE_RATE));
}

static voice_t *alloc_voice(void)
{
    // a free voice, or the voice nearest to its end
    voice_t *v = NULL;
    uint32_t best_left = UINT32_MAX;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!s_voices[i].on) {
            return &s_voices[i];
        }
        uint32_t left = s_voices[i].delay + s_voices[i].len - s_voices[i].pos;
        if (left < best_left) {
            best_left = left;
            v = &s_voices[i];
        }
    }
    return v;
}

void audio_voice(const audio_voice_t *c)
{
    if (s_muted || c->dur == 0) {
        return;
    }
    voice_t *v = alloc_voice();
    memset(v, 0, sizeof(*v));
    v->on = true;
    v->wave = c->wave;
    v->shape = c->shape;
    v->group = c->group;
    v->delay = c->delay;
    v->len = c->dur;
    v->attack = (uint32_t)c->attack_ms * SAMPLE_RATE / 1000;
    if (v->attack < SAMPLE_RATE / 500) {
        v->attack = SAMPLE_RATE / 500;
    }
    v->release = c->dur / 4 < SAMPLE_RATE / 12 ? c->dur / 4 : SAMPLE_RATE / 12;
    if (v->len < 2) {
        v->len = 2;
    }
    if (v->release < 2) {
        v->release = 2;
    }
    v->prog_step = (uint32_t)((1ull << 32) / v->len);
    v->att_k = (uint32_t)((1ull << 32) / v->attack);
    v->rel_k = (uint32_t)((1ull << 32) / v->release);
    v->inc0 = hz_to_inc(c->f0);
    v->inc1 = hz_to_inc(c->f1);
    // 100 -> 16000 peak = 32000 after the x 2 of the mix = 98 % of full scale: one note at its full volume is not over
    // 100 % (a higher amp, for example 20000 = 122 % of full scale, makes the limiter work on each loud note)
    v->amp = (c->vol > 100 ? 100 : c->vol) * 160;
    v->lp_a = c->lp ? (c->lp * c->lp + 1) : 0;  // square: a finer control at the dark end
    v->hp_a = c->hp ? (c->hp * c->hp + 1) : 0;
    if (c->wave == W_SAMPLE) {
        if (!c->sample || c->sample_len < 2) {
            v->on = false;
            return;
        }
        v->smp = c->sample;
        v->smp_len = c->sample_len;
        v->smp_inc = (uint32_t)((c->rate > 0 ? c->rate : 1.0f) * 65536.0f);
        v->smp_loop = c->loop;
    }
}

void audio_release_group(uint8_t group, uint16_t ms)
{
    uint32_t r = (uint32_t)ms * SAMPLE_RATE / 1000;
    if (r < 1) {
        r = 1;
    }
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &s_voices[i];
        if (!v->on || v->group != group) {
            continue;
        }
        if (v->delay) {
            v->on = false;  // not started yet
            continue;
        }
        if (v->len - v->pos > r) {
            if (r < 2) {
                r = 2;
            }
            v->len = v->pos + r;
            v->release = r;
            v->rel_k = (uint32_t)((1ull << 32) / r);
            // the sweep ends with the note: the rest of the progress in r samples
            v->prog_step = (uint32_t)((0x100000000ull - v->prog) / r);
            v->shape = AUDIO_SUSTAIN;
        }
    }
}

void audio_set_tick(void (*cb)(void))
{
    s_tick = cb;
    lock_barrier();  // (the old tick is not in a call now, and it gets no call after this)
}

void audio_tick_end(int timeout_ms)
{
    if (!s_tick) {
        return;
    }
    // (a last tick that clears itself does it in the next block: two blocks later each tick that stays is not one.
    // No wait while the sound sleeps: no tick runs, and the voices are off.)
    if (s_lock && !s_asleep && !in_audio_task()) {
        const uint32_t b0 = s_blocks;
        const int64_t end = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
        while (s_tick && s_blocks - b0 < 2 && esp_timer_get_time() < end) {
            vTaskDelay(1);
        }
    }
    audio_set_tick(NULL);
}

static void start_notes_q(const note_t *notes, int shift);
static void start_notes(const note_t *notes)
{
    start_notes_q(notes, 0);
}
// the notes at 1 / 2^shift of their volume (sfx_ui: shift 2)
static void start_notes_q(const note_t *notes, int shift)
{
    for (const note_t *n = notes; n->dur_ms; n++) {
        audio_voice_t c = {
                .delay = (uint32_t)n->delay_ms * SAMPLE_RATE / 1000,
                .dur = (uint32_t)n->dur_ms * SAMPLE_RATE / 1000,
                .f0 = n->f0,
                .f1 = n->f1,
                .wave = n->wave,
                .vol = (uint8_t)(n->vol >> shift ? n->vol >> shift : 1),
                .attack_ms = 2,
                .shape = AUDIO_PERC,
        };
        audio_voice(&c);
    }
}

// Start the notes of the tune that begin in this block, with their exact delay.
// The notes can be in any order (a tune has about 50 notes at most).
static void tune_block(void)
{
#ifdef TFN_SIMULATOR
    // (the simulator: the recordings of DEMO take the effects of an app without its tunes; sim_rec.c)
    extern volatile int g_sim_music_off;
    if (g_sim_music_off) {
        s_tune = NULL;
    }
#endif
    if (!s_tune) {
        return;
    }
    uint32_t end = s_tune_pos + BLOCK_FRAMES;
    bool more = false;
    for (int i = 0; i < s_tune_n; i++) {
        const tune_note_t *n = &s_tune[i];
        uint32_t at = (uint32_t)n->t * (SAMPLE_RATE / 1000);
        if (at >= end) {
            more = true;
            continue;
        }
        if (at < s_tune_pos) {
            continue;
        }
        float f0 = audio_midi_hz(n->note);
        audio_voice_t c = {
                .delay = at - s_tune_pos,
                .dur = (uint32_t)n->dur * (SAMPLE_RATE / 1000),
                .f0 = f0,
                .f1 = n->bend ? audio_midi_hz((float)n->note + n->bend) : f0,
                .wave = (uint8_t)(n->wave & ~TUNE_HOLD),
                .vol = (uint8_t)(n->vol * 4 / 5 * s_tune_k / 100 > 0 ? n->vol * 4 / 5 * s_tune_k / 100
                                                                     : 1),  // (tune_scale)
                .attack_ms = (n->wave & TUNE_HOLD) ? 8 : 2,
                .shape = (n->wave & TUNE_HOLD) ? AUDIO_SUSTAIN : AUDIO_PERC,
        };
        audio_voice(&c);
    }
    s_tune_pos = end;
    if (!more) {
        s_tune = NULL;
    }
}

static inline int32_t osc(uint8_t wave, uint32_t phase, uint32_t old, int16_t *noise)
{
    switch (wave) {
    case W_SQUARE:
        return (phase & 0x80000000u) ? 32767 : -32767;
    case W_PULSE:
        return (phase < 0x40000000u) ? 32767 : -32767;
    case W_TRI: {
        uint32_t p = phase >> 16;  // 0..65535
        return (p < 32768) ? (int32_t)p * 2 - 32767 : 32767 - ((int32_t)p - 32768) * 2;
    }
    case W_SAW:
        return (int32_t)(phase >> 16) - 32768;
    case W_SINE: {
        // parabola approximation of a sine
        int32_t p = (int32_t)(phase >> 16) - 32768;          // -32768..32767
        int32_t y = (p * (32768 - (p < 0 ? -p : p))) >> 13;  // 4 p (1 - |p|)
        return y > 32767 ? 32767 : (y < -32767 ? -32767 : y);
    }
    default:  // noise: a new value each time the phase wraps
        if (phase < old) {
            s_lfsr ^= s_lfsr << 13;
            s_lfsr ^= s_lfsr >> 17;
            s_lfsr ^= s_lfsr << 5;
            *noise = (int16_t)(s_lfsr & 0xFFFF);
        }
        return *noise;
    }
}

// Add n samples of a voice to mix[]. One voice for the whole block: the state
// stays in registers, and a voice that is off costs nothing.
// THE LIMITER (the levels of many MIDI pieces go above full scale after the x 2 of the mix: up to 59000 at the soft
// limit of the output). The soft limit bends each loud wave (distortion), so a gain turns the whole sound down for a
// loud part, and back up slowly. A lookahead of one block (5 ms):
// the block that goes out now is the block before the newest, so the gain is down before the peak comes. The gain
// changes in a straight line over each block (no click). It does nothing while the sound stays under LIM_T: all
// sounds that are clean stay the same, 5 ms later.
// (A low limit such as 22000 does not cure distortion at the codec: with +3.6 dB in the ES8311, each peak over 21700
// clips in the codec. VOLUME 100 = 0 dB in the codec on all boards (audio_init), and all boards use the same limit.)
#define LIM_T    30000  // the peak at most (the soft limit starts at LIM_SOFT: a safety only)
#define LIM_SOFT 31000
#define LIM_HOLD 20                       // blocks (100 ms) at the low gain before it goes up again
#define LIM_REL  0.03f                    // the part of the way back to full each block (about 170 ms)
static int32_t s_lim_prev[BLOCK_FRAMES];  // the block before (it goes out now)
static int32_t s_lim_tprev = 65536;       // the gain (Q16) that the block before needs at most
static int32_t s_lim_g = 65536;           // the gain at the end of the last block that went out
static int s_lim_hold;
static volatile uint32_t s_lim_min = 65536;  // the lowest gain since the last read (audio_limiter_take)

// acc: the new block (x 2 already). out: the block before, with the gain, then the soft limit as before (a safety).
static void limit_block(int32_t *acc, int16_t *buf)
{
    int32_t pk = 0;
    for (int i = 0; i < BLOCK_FRAMES; i++) {
        int32_t a = acc[i] < 0 ? -acc[i] : acc[i];
        if (a > pk) {
            pk = a;
        }
    }
    int32_t tcur = pk > LIM_T ? (int32_t)((int64_t)LIM_T * 65536 / pk) : 65536;
    int32_t g0 = s_lim_g, g1;
    if (tcur < g0 || s_lim_tprev < g0) {
        g1 = tcur < s_lim_tprev ? tcur : s_lim_tprev;  // down (the attack, before the peak)
        s_lim_hold = LIM_HOLD;
    } else if (s_lim_hold > 0) {
        s_lim_hold--;
        g1 = g0;
    } else {
        g1 = g0 + (int32_t)((65536 - g0) * LIM_REL);  // up again, slowly
        if (g1 > tcur) {
            g1 = tcur;
        }
        if (g1 > s_lim_tprev) {
            g1 = s_lim_tprev;
        }
    }
    if (g1 < (int32_t)s_lim_min) {
        s_lim_min = (uint32_t)g1;
    }
    for (int i = 0; i < BLOCK_FRAMES; i++) {
        int32_t g = g0 + (int32_t)((int64_t)(g1 - g0) * (i + 1) / BLOCK_FRAMES);
        int32_t mix = (int32_t)(((int64_t)s_lim_prev[i] * g) >> 16);
        // the soft limit near full scale (a safety: the gain keeps the sound under it; before the limiter it started
        // at 24000 and bent each loud wave)
        if (mix > LIM_SOFT) {
            mix = LIM_SOFT + (mix - LIM_SOFT) / 4;
        }
        if (mix < -LIM_SOFT) {
            mix = -LIM_SOFT + (mix + LIM_SOFT) / 4;
        }
        if (mix > 32767) {
            mix = 32767;
        }
        if (mix < -32767) {
            mix = -32767;
        }
        buf[2 * i] = (int16_t)mix;
        buf[2 * i + 1] = (int16_t)mix;
        s_lim_prev[i] = acc[i];
    }
    s_lim_g = g1;
    s_lim_tprev = tcur;
}

// the lowest gain of the limiter since the last call (percent; 100 = it did nothing): for tests and the console
int audio_limiter_take(void)
{
    uint32_t m = s_lim_min;
    s_lim_min = 65536;
    return (int)(m * 100 / 65536);
}

static void voice_block(voice_t *v, int32_t *mix, int n)
{
    int i = 0;
    if (v->delay) {
        uint32_t d = v->delay < (uint32_t)n ? v->delay : (uint32_t)n;
        v->delay -= d;
        i = (int)d;
    }
    uint32_t pos = v->pos, prog = v->prog, phase = v->phase;
    int32_t lp_y = v->lp_y, hp_y = v->hp_y;
    uint32_t smp_idx = v->smp_idx, smp_frac = v->smp_frac;
    int16_t noise = v->noise;
    const uint32_t len = v->len, prog_step = v->prog_step, inc0 = v->inc0;
    const int64_t dinc = (int64_t)v->inc1 - (int64_t)v->inc0;
    const uint32_t attack = v->attack, release = v->release, att_k = v->att_k, rel_k = v->rel_k;
    const int32_t lp_a = v->lp_a, hp_a = v->hp_a, amp = v->amp;
    const uint8_t wave = v->wave;
    const bool perc = v->shape == AUDIO_PERC;
    uint32_t inc = inc0;
    for (; i < n; i++) {
        if (pos >= len) {
            v->on = false;
            break;
        }
        uint32_t pr = prog >> 16;  // 0..65535
        prog += prog_step;
        // a 64-bit multiply is slow here: the sweep changes each 8 samples (3 kHz)
        if (dinc && !(i & 7)) {
            inc = (uint32_t)((int64_t)inc0 + ((dinc * (int64_t)pr) >> 16));
        }
        uint32_t old = phase;
        phase += inc;

        int32_t env;
        if (perc) {
            env = (int32_t)(65536 - pr);
        } else {
            uint32_t left = len - pos;
            env = left < release ? (int32_t)(((uint64_t)left * rel_k) >> 16) : 65536;
        }
        if (pos < attack) {
            int32_t at = (int32_t)(((uint64_t)pos * att_k) >> 16);  // 0..65535
            env = (int32_t)(((int64_t)env * at) >> 16);
        }
        pos++;

        int32_t smp;
        if (wave == W_SAMPLE) {
            if (smp_idx + 1 >= v->smp_len) {
                if (!v->smp_loop) {
                    v->on = false;
                    break;
                }
                smp_idx = 0;
                smp_frac = 0;
            }
            int32_t a = v->smp[smp_idx], b = v->smp[smp_idx + 1];
            smp = a + (((b - a) * (int32_t)smp_frac) >> 16);
            smp_frac += v->smp_inc;
            smp_idx += smp_frac >> 16;
            smp_frac &= 0xFFFF;
        } else {
            smp = osc(wave, phase, old, &noise);
        }
        if (hp_a) {
            hp_y += (int32_t)(((int64_t)(smp - hp_y) * hp_a) >> 16);
            smp -= hp_y;
        }
        if (lp_a) {
            lp_y += (int32_t)(((int64_t)(smp - lp_y) * lp_a) >> 16);
            smp = lp_y;
        }
        // all in 32 bits: |smp| < 2^17, amp <= 20000, env <= 2^16
        int32_t y = (smp * amp) >> 15;
        mix[i] += (y * (env >> 1)) >> 15;
    }
    v->pos = pos;
    v->prog = prog;
    v->phase = phase;
    v->lp_y = lp_y;
    v->hp_y = hp_y;
    v->smp_idx = smp_idx;
    v->smp_frac = smp_frac;
    v->noise = noise;
}

void audio_drone(int ch, float hz, int vol, wave_t wave)
{
    if (ch < 0 || ch >= N_DRONES) {
        return;
    }
    s_drone[ch].hz_target = hz;
    s_drone[ch].vol_target = (float)(vol < 0 ? 0 : vol > 100 ? 100 : vol);
    s_drone[ch].wave = (uint8_t)wave;
}

// ---------------------------------------------------------------- the voice stream (WALKIE)
#define STREAM_N 12288  // about 0.5 s
static int16_t *s_stream;
static volatile uint32_t s_st_w, s_st_r;  // write and read counts (the ring: modulo STREAM_N)
static volatile int s_st_gain = 256;

void audio_stream_write(const int16_t *s, int n)
{
    if (!s_stream) {
        s_stream = heap_caps_calloc(STREAM_N, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_stream) {
            return;
        }
    }
    uint32_t w = s_st_w, r = s_st_r;
    for (int i = 0; i < n && w - r < STREAM_N; i++) {
        s_stream[w++ % STREAM_N] = s[i];
    }
    s_st_w = w;
}

int audio_stream_fill(void)
{
    return (int)(s_st_w - s_st_r);
}

void audio_stream_clear(void)
{
    s_st_r = s_st_w;
}

void audio_stream_gain(int q8)
{
    s_st_gain = q8;
}

static void stream_block(int32_t *acc, int n)
{
    uint32_t r = s_st_r, w = s_st_w;
    if (!s_stream || r == w) {
        return;
    }
    int g = s_st_gain;
    for (int i = 0; i < n && r != w; i++) {
        acc[i] += (s_stream[r++ % STREAM_N] * g) >> 8;
    }
    s_st_r = r;
}

// ---------------------------------------------------------------- the TIA voice
// The two sound channels of the Atari 2600 chip (the TIA), from its published behaviour (our own code). Each channel
// has a divider that gives a pulse each AUDF + 1 clocks (AUDC 12..15: then one more divide by 3). Each pulse moves
// the 5-bit pattern and the "divide by 31" count on. If AUDC bit 1 is set, only some pulses go on: where the 5-bit
// pattern is 1 (bit 0 set) or the two pulses of the divide by 31 (bit 0 clear). Then: bit 2 set = a flip-flop (a pure
// tone); else bit 3 set = the 9-bit pattern (AUDC 8) or the 5-bit pattern; else the 4-bit pattern. The level is AUDV
// when the output is 1; AUDC 0 and 11 give a constant level (no sound). The patterns are the shift registers
// x^4 + x^3 + 1, x^5 + x^3 + 1 and x^9 + x^5 + 1. The clock is 31,399.5 Hz (the NTSC colour clock / 114). Each output
// sample is the mean level over its time (a box filter), then a DC blocker (as the AC coupling of a TV set). The
// state is in PSRAM (no internal RAM); the voice costs nothing when it is off.
#define TIA_STEP16 85742u  // TIA clocks for each output sample, 16.16 (31399.5 / 24000 = 1.30831)
#define TIA_GAIN   1600    // the output for each step of the level (AUDV 4, the 4-bit pattern: about 3200 RMS)
#define TIA_K      ((int32_t)(((int64_t)TIA_GAIN << 16) / TIA_STEP16))  // (the mean of the box: sum * K >> 16)
#define TIA_IDLE   2400  // samples (0.1 s) with no steps and a quiet output: then the voice stops

typedef struct {
    const tia_step_t *steps;  // NULL = no steps (the registers stay)
    uint32_t left;            // the clocks left in the step now
    uint16_t n, i;            // the number of steps, the step now
    uint8_t loops;            // the loops still to start after this one
    uint8_t audc, audf, audv;
    uint8_t div, div3, p4, p5, c31, out;
    uint16_t p9;
} tia_ch_t;

static struct {
    tia_ch_t ch[2];
    uint32_t ph;     // the time in the TIA clock now (16.16)
    int32_t x1, y8;  // the DC blocker: the last input, the output x 256
    uint32_t quiet;  // samples with no steps and a quiet output
    bool on;
} s_tia EXT_RAM_BSS_ATTR;

// the next step of a channel (or the next loop, or the end: AUDC 0, the volume stays)
static void tia_next(tia_ch_t *c)
{
    for (int k = 0; k <= c->n; k++) {
        if (++c->i >= c->n) {
            if (!c->loops) {
                break;
            }
            c->loops--;
            c->i = 0;
        }
        const tia_step_t *s = &c->steps[c->i];
        c->audc = s->audc & 15;
        c->audf = s->audf & 31;
        c->audv = s->audv & 15;
        c->left = s->clocks;
        if (c->left) {
            return;
        }
    }
    c->steps = NULL;
    c->audc = 0;
}

// one TIA clock of a channel
static inline void tia_clock(tia_ch_t *c)
{
    if (c->steps && --c->left == 0) {
        tia_next(c);
    }
    if (c->div < c->audf) {
        c->div++;
        return;
    }
    c->div = 0;
    const uint8_t a = c->audc;
    if (a >= 12) {
        if (++c->div3 < 3) {
            return;
        }
        c->div3 = 0;
    }
    c->p5 = (uint8_t)((c->p5 >> 1) | (((c->p5 ^ (c->p5 >> 2)) & 1) << 4));
    if (++c->c31 >= 31) {
        c->c31 = 0;
    }
    if (a & 2) {
        const bool gate = (a & 1) ? (c->p5 & 1) : (c->c31 == 0 || c->c31 == 18);
        if (!gate) {
            return;
        }
    }
    if (a & 4) {
        c->out ^= 1;
    } else if (a & 8) {
        if (a == 8) {
            c->p9 = (uint16_t)((c->p9 >> 1) | (((c->p9 ^ (c->p9 >> 4)) & 1) << 8));
            c->out = c->p9 & 1;
        } else {
            c->out = c->p5 & 1;
        }
    } else {
        c->p4 = (uint8_t)((c->p4 >> 1) | (((c->p4 ^ (c->p4 >> 1)) & 1) << 3));
        c->out = c->p4 & 1;
    }
}

static inline int32_t tia_level(const tia_ch_t *c)
{
    return (c->audc == 0 || c->audc == 11 || c->out) ? c->audv : 0;
}

// stop both channels and the voice (muted, asleep)
static void tia_stop(void)
{
    for (int k = 0; k < 2; k++) {
        s_tia.ch[k].steps = NULL;
        s_tia.ch[k].audc = 0;
    }
    s_tia.y8 = 0;
    s_tia.on = false;
}

// a command from the queue (audio task)
static void tia_cmd(const sfx_msg_t *m)
{
    if (m->tia.ch > 1) {  // stop both channels (the voice ends after its DC blocker is quiet)
        for (int k = 0; k < 2; k++) {
            s_tia.ch[k].steps = NULL;
            s_tia.ch[k].audc = 0;
        }
        return;
    }
    if (s_muted || !m->tia.steps || !m->tia.n) {
        return;
    }
    tia_ch_t *c = &s_tia.ch[m->tia.ch];
    if (!c->p4) {  // (the first time: a shift register must not be 0)
        c->p4 = 15;
        c->p5 = 31;
        c->p9 = 511;
    }
    if (m->tia.times && c->steps == m->tia.steps && c->n == m->tia.n) {
        if (c->loops < m->tia.times - 1) {
            c->loops = (uint8_t)(m->tia.times - 1);  // a loop that plays: it goes on
        }
        return;
    }
    uint32_t skip = m->tia.skip;
    int i = 0;
    while (i < m->tia.n && skip >= m->tia.steps[i].clocks) {
        skip -= m->tia.steps[i++].clocks;
    }
    if (i >= m->tia.n) {  // (all of it is left out)
        c->steps = NULL;
        c->audc = 0;
        return;
    }
    const tia_step_t *s = &m->tia.steps[i];
    c->steps = m->tia.steps;
    c->n = m->tia.n;
    c->i = (uint16_t)i;
    c->loops = m->tia.times > 1 ? (uint8_t)(m->tia.times - 1) : 0;
    c->left = s->clocks - skip;
    c->audc = s->audc & 15;
    c->audf = s->audf & 31;
    c->audv = s->audv & 15;
    s_tia.on = true;
    s_tia.quiet = 0;
}

// add one block of the TIA voice to acc (audio task)
static void tia_block(int32_t *acc, int n)
{
    tia_ch_t *c0 = &s_tia.ch[0], *c1 = &s_tia.ch[1];
    uint32_t ph = s_tia.ph;
    int32_t x1 = s_tia.x1, y8 = s_tia.y8;
    int32_t lvl = tia_level(c0) + tia_level(c1);
    for (int k = 0; k < n; k++) {
        uint32_t rem = TIA_STEP16;
        int32_t sum = 0;  // the level x the time (16.16): 30 x 85742 at most
        while (rem) {
            const uint32_t room = 65536u - ph;
            const uint32_t take = rem < room ? rem : room;
            sum += lvl * (int32_t)take;
            ph += take;
            rem -= take;
            if (ph >= 65536u) {
                ph = 0;
                tia_clock(c0);
                tia_clock(c1);
                lvl = tia_level(c0) + tia_level(c1);
            }
        }
        const int32_t x = (int32_t)(((int64_t)sum * TIA_K) >> 16);
        y8 += (x - x1) * 256 - (y8 >> 8);  // the DC blocker: y = x - x1 + (1 - 1/256) y (15 Hz)
        x1 = x;
        acc[k] += y8 >> 8;
    }
    s_tia.ph = ph;
    s_tia.x1 = x1;
    s_tia.y8 = y8;
    if (!c0->steps && !c1->steps && y8 < (4 << 8) && y8 > -(4 << 8)) {
        s_tia.quiet += (uint32_t)n;
        if (s_tia.quiet >= TIA_IDLE) {
            s_tia.on = false;
        }
    } else {
        s_tia.quiet = 0;
    }
}

void audio_sleep(bool on)
{
    s_sleep_want = on;
    if (!on && s_audio_task) {
        xTaskNotifyGive(s_audio_task);
    }
}

// ---------------------------------------------------------------- the messages that point into memory
// (a sample voice, a tune, TIA steps: s_q_refs counts them from the send to the use; s_q_lo and s_q_hi hold their
// memory)

// the memory [lo, hi) that a message points into; false = none
static bool msg_span(const sfx_msg_t *m, uintptr_t *lo, uintptr_t *hi)
{
    const void *p = NULL;
    size_t n = 0;
    if (m->id == SFX_COUNT + 1 && m->voice.wave == W_SAMPLE) {
        p = m->voice.sample;
        n = (size_t)m->voice.sample_len * sizeof(int16_t);
    } else if (m->id == SFX_COUNT + 2) {
        p = m->tune;
        n = (size_t)m->tune_n * sizeof(tune_note_t);
    } else if (m->id == SFX_COUNT + 4 && m->tia.ch <= 1) {
        p = m->tia.steps;
        n = (size_t)m->tia.n * sizeof(tia_step_t);
    }
    *lo = (uintptr_t)p;
    *hi = *lo + n;
    return p != NULL;
}
// a message is used (or dropped): after its voice, tune or steps are in place
static void msg_done(const sfx_msg_t *m)
{
    uintptr_t lo, hi;
    if (!msg_span(m, &lo, &hi)) {
        return;
    }
    portENTER_CRITICAL(&s_q_mux);
    if (s_q_refs && --s_q_refs == 0) {
        s_q_lo = UINTPTR_MAX;  // (no message points into memory: the range is empty)
        s_q_hi = 0;
    }
    portEXIT_CRITICAL(&s_q_mux);
}
// send a message (no wait: a full queue drops it)
static void msg_send(const sfx_msg_t *m)
{
    uintptr_t lo, hi;
    const bool ref = msg_span(m, &lo, &hi);
    if (ref) {
        // (before the send: the audio task can take the message at once)
        portENTER_CRITICAL(&s_q_mux);
        s_q_refs++;
        s_q_lo = lo < s_q_lo ? lo : s_q_lo;
        s_q_hi = hi > s_q_hi ? hi : s_q_hi;
        portEXIT_CRITICAL(&s_q_mux);
    }
    if (xQueueSend(s_queue, m, 0) != pdTRUE && ref) {
        msg_done(m);
    }
}
// true when a message of the queue can point into [lo, hi)
static bool q_may_point(uintptr_t lo, uintptr_t hi)
{
    portENTER_CRITICAL(&s_q_mux);
    const bool may = s_q_refs && lo < s_q_hi && hi > s_q_lo;
    portEXIT_CRITICAL(&s_q_mux);
    return may;
}
uint32_t audio_queued_refs(void)
{
    portENTER_CRITICAL(&s_q_mux);
    const uint32_t n = s_q_refs;
    portEXIT_CRITICAL(&s_q_mux);
    return n;
}

// the screen is off (audio_sleep): close the codec, wait; drop the sounds, keep the tune state
static void sleep_block(void)
{
    if (!s_asleep) {
        esp_codec_dev_close(s_dev);
        lock_take();
        s_asleep = true;
        memset(s_voices, 0, sizeof(s_voices));
        tia_stop();
        s_st_r = s_st_w;
        lock_give();
        ESP_LOGI(TAG, "the sound sleeps");
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    if (!s_sleep_want) {
        return;  // (the wake: keep the sounds in the queue, e.g. the chime of a notice)
    }
    sfx_msg_t m;
    lock_take();
    while (xQueueReceive(s_queue, &m, 0) == pdTRUE) {
        if (m.id == SFX_COUNT + 2) {
            s_tune = m.tune;
            s_tune_n = m.tune_n;
            s_tune_pos = 0;
            s_tune_k = tune_scale(m.tune, m.tune_n);
        }
        msg_done(&m);
    }
    lock_give();
}

static void wake_codec(void)
{
    esp_codec_dev_sample_info_t fs = {.sample_rate = SAMPLE_RATE, .channel = 2, .bits_per_sample = 16};
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "the codec did not open again");
    }
    if (s_vol >= 0) {
        esp_codec_dev_set_out_vol(s_dev, s_vol);
    }
#if MIC_SHARED
    esp_codec_dev_set_in_gain(s_dev, 30.0f);
#endif
    s_asleep = false;
    s_ret = 0;
    ESP_LOGI(TAG, "the sound is on again");
}

static void alert_end_check(void);
// the tap of the output (USB AUDIO: the sound of the board to a computer) and the quiet speaker
static void (*volatile s_out_tap)(const int16_t *s, int n);
static volatile bool s_spk_off;
void audio_out_tap(void (*cb)(const int16_t *s, int n))
{
    s_out_tap = cb;
}
void audio_speaker_off(bool off)
{
    s_spk_off = off;
}

static void task_read_queue(void);
static void task_capture(const int16_t *buf);
static void task_out_tap(const int16_t *buf);
static void task_late_log(void);

static void audio_task(void *arg)
{
    static int16_t buf[BLOCK_FRAMES * 2];
    s_audio_task = xTaskGetCurrentTaskHandle();  // (also before xTaskCreatePinnedToCore writes it)
    for (;;) {
        alert_end_check();  // (the end of an audio_alert)
        if (s_sleep_want && !s_mic_open && !s_out_tap) {
            sleep_block();
            continue;
        }
        if (s_asleep) {
            wake_codec();
        }
        lock_take();  // (to the copy to the capture buffer: the part that reads or writes the memory of the apps)
        task_read_queue();
        tune_block();
        void (*tick)(void) = s_tick;
        if (tick) {
            tick();
        }
        if (s_muted) {
            memset(s_voices, 0, sizeof(s_voices));
            if (s_tia.on) {
                tia_stop();
            }
        }
        // drones glide to their targets once per block
        uint32_t dinc[N_DRONES];
        int32_t damp[N_DRONES];
        for (int d = 0; d < N_DRONES; d++) {
            drone_t *dr = &s_drone[d];
            dr->hz += (dr->hz_target - dr->hz) * 0.25f;
            dr->vol += ((s_muted ? 0 : dr->vol_target) - dr->vol) * 0.2f;
            dinc[d] = hz_to_inc(dr->hz);
            damp[d] = (int32_t)(dr->vol * 160);  // (the same scale as a voice: 100 = 98 % after the x 2)
        }

        int64_t t0 = esp_timer_get_time();
        int32_t acc[BLOCK_FRAMES];
        memset(acc, 0, sizeof(acc));
        for (int k = 0; k < MAX_VOICES; k++) {
            if (s_voices[k].on) {
                voice_block(&s_voices[k], acc, BLOCK_FRAMES);
            }
        }
        if (!s_muted) {
            stream_block(acc, BLOCK_FRAMES);
        } else if (s_st_r != s_st_w) {
            s_st_r = s_st_w;
        }
        for (int d = 0; d < N_DRONES; d++) {
            if (damp[d] < 20) {
                continue;
            }
            drone_t *dr = &s_drone[d];
            for (int i = 0; i < BLOCK_FRAMES; i++) {
                uint32_t old = dr->phase;
                dr->phase += dinc[d];
                int32_t s = osc(dr->wave, dr->phase, old, &dr->noise);
                dr->lp_y += (s - dr->lp_y) >> 2;  // soft edges
                acc[i] += (dr->lp_y * damp[d]) >> 15;
            }
        }
        if (s_tia.on) {
            tia_block(acc, BLOCK_FRAMES);
        }
        // x 2 (+6 dB on all boards, so the speakers are loud enough; one voice at 100 = 98 % of full scale),
        // the peak meter (audio_peak_take), then the boost of a VOLUME over 100 (audio_set_volume); the limiter keeps
        // the peaks
        const int32_t boost = s_boost_q8;
        int32_t pk = 0;
        for (int i = 0; i < BLOCK_FRAMES; i++) {
            int32_t a = acc[i] * 2;
            int32_t m = a < 0 ? -a : a;
            if (m > pk) {
                pk = m;
            }
            acc[i] = boost == 256 ? a : (int32_t)(((int64_t)a * boost) >> 8);
        }
        if ((uint32_t)pk > s_peak_max) {
            s_peak_max = (uint32_t)pk;
        }
        limit_block(acc, buf);
        task_capture(buf);
        lock_give();
        uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
        s_load_us = (s_load_us * 15 + us) / 16;
        task_out_tap(buf);
        if (s_spk_off) {
            memset(buf, 0, sizeof(buf));
        }
        task_late_log();
        // blocks until the DMA has space: this sets the pace of the task
        esp_codec_dev_write(s_dev, buf, sizeof(buf));
        s_ret = esp_timer_get_time();
        s_blocks++;
    }
}

// audio_task: one message of the queue (an effect, a tone, a tune, a voice, a group, the TIA)
static void task_msg(const sfx_msg_t *m)
{
    if (m->id == SFX_COUNT + 3) {
        audio_release_group((uint8_t)m->tune_n, 10);  // also when muted
        return;
    }
    if (m->id == SFX_COUNT + 4) {
        tia_cmd(m);  // (a stop also when muted)
        return;
    }
    if (s_muted) {
        return;
    }
    if (m->id < SFX_COUNT) {
        start_notes(SFX_DEFS[m->id]);
    } else if (m->id == SFX_COUNT + 5) {
        if (m->tune_n < SFX_COUNT) {
            start_notes_q(SFX_DEFS[m->tune_n], 2);  // (sfx_ui: a quarter)
        }
    } else if (m->id == SFX_COUNT) {
        note_t one[2] = {m->tone, END};
        start_notes(one);
    } else if (m->id == SFX_COUNT + 2) {
        s_tune = m->tune;
        s_tune_n = m->tune_n;
        s_tune_pos = 0;
        s_tune_k = tune_scale(m->tune, m->tune_n);
    } else {
        audio_voice(&m->voice);
    }
}

// audio_task: the messages of the queue
static void task_read_queue(void)
{
    sfx_msg_t m;
    while (xQueueReceive(s_queue, &m, 0) == pdTRUE) {
        task_msg(&m);
        msg_done(&m);
    }
}

// audio_task (with the lock): the copy of a block to the capture buffer
static void task_capture(const int16_t *buf)
{
    int16_t *cap = s_cap_buf;
    if (cap) {
        int i0 = s_cap_skip < BLOCK_FRAMES ? (int)s_cap_skip : BLOCK_FRAMES;
        s_cap_skip -= (uint32_t)i0;
        for (int i = i0; i < BLOCK_FRAMES && s_cap_n < s_cap_max; i++) {
            cap[s_cap_n++] = buf[2 * i];
        }
        if (s_cap_n >= s_cap_max) {
            s_cap_buf = NULL;
        }
    }
}

// audio_task (with no lock: the tap is a function of the OS, USB AUDIO): the copy of a block to the tap of the output
static void task_out_tap(const int16_t *buf)
{
    void (*otap)(const int16_t *, int) = s_out_tap;
    if (otap) {
        static int16_t mono[BLOCK_FRAMES];
        for (int i = 0; i < BLOCK_FRAMES; i++) {
            mono[i] = buf[2 * i];
        }
        otap(mono, BLOCK_FRAMES);
    }
}

// audio_task: a LATE BLOCK: the task came back after more time than the DMA queue holds (3 blocks of 5 ms), so the
// speaker played a gap (a choppy sound). The log line tells the time of the gap; at most one line each second.
static void task_late_log(void)
{
    static int64_t s_late_log;
    static uint32_t s_late_n, s_late_max;
    const int64_t t1 = esp_timer_get_time();
    if (s_ret && t1 - s_ret > 12000) {
        s_late_n++;
        if ((uint32_t)(t1 - s_ret) > s_late_max) {
            s_late_max = (uint32_t)(t1 - s_ret);
        }
        if (t1 - s_late_log > 1000000) {
            s_late_log = t1;
            ESP_LOGW(TAG, "late block: %u in all, the longest gap %u ms, %u underruns", (unsigned)s_late_n,
                     (unsigned)(s_late_max / 1000), (unsigned)s_underrun);
            s_late_max = 0;
        }
    }
}

// ---------------------------------------------------------------- microphone

static esp_codec_dev_handle_t s_mic;
static i2s_chan_handle_t s_i2s_rx;  // (the receive channel of the microphones; audio_init)
static volatile bool s_mic_want;
static volatile float s_mic_level;
static int16_t *volatile s_rec_buf;
static void (*volatile s_mic_tap)(const int16_t *s, int n);
static void (*volatile s_mic_tap_web)(const int16_t *s, int n);  // the web page (web_walkie.c)
#define MIC_WANT (s_mic_want || s_mic_tap_web)
static volatile uint32_t s_rec_max, s_rec_n;
static TaskHandle_t s_mic_task;
// the calls of the tap functions: the mic task adds 1 before it reads the two functions and 1 after their calls, so the
// count is odd while a tap function runs (tap_barrier). No lock: a tap function can be slow, and no task waits for it
// but the one that changes the function.
static volatile uint32_t s_tap_calls;

// the mic task: the taps of one block (mono samples)
static void mic_taps(const int16_t *mono)
{
    __atomic_fetch_add(&s_tap_calls, 1, __ATOMIC_SEQ_CST);  // (odd: before the reads of the functions)
    void (*tap)(const int16_t *, int) = s_mic_tap;
    void (*tap_web)(const int16_t *, int) = s_mic_tap_web;
    if (tap) {
        tap(mono, MIC_FRAMES);
    }
    if (tap_web) {
        tap_web(mono, MIC_FRAMES);
    }
    __atomic_fetch_add(&s_tap_calls, 1, __ATOMIC_SEQ_CST);  // (even: after the calls)
}

// a barrier after a change of a tap function: when it returns, the mic task is not in a call of the old function and
// gives it no more calls, so the caller can free the data of that function (not from a tap function itself)
static void tap_barrier(void)
{
    if (!s_mic_task || xTaskGetCurrentTaskHandle() == s_mic_task) {
        return;
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);  // (the new function is in place before the read of the count)
    const uint32_t c = __atomic_load_n(&s_tap_calls, __ATOMIC_SEQ_CST);
    if (c & 1) {
        while (__atomic_load_n(&s_tap_calls, __ATOMIC_SEQ_CST) == c) {
            vTaskDelay(1);  // (a call of a tap is short: the next look sees its end)
        }
    }
}

static void mic_task(void *arg)
{
    static int16_t buf[MIC_FRAMES * 2];
    s_mic_task = xTaskGetCurrentTaskHandle();
    for (;;) {
        if (MIC_SHARED && s_mic) {
            s_mic_open = MIC_WANT && !s_asleep;  // (the device is open while the sound is awake)
            if (!s_mic_open) {
                s_mic_level = 0;
            }
        } else if (MIC_WANT && !s_mic_open && s_mic) {
            esp_codec_dev_sample_info_t fs = {.sample_rate = SAMPLE_RATE, .channel = 2, .bits_per_sample = 16};
            // (The open stops the receive channel before it sets the format. When that channel is not
            // running (the first open; after a close while the sound slept), IDF logs "the channel has not been
            // enabled yet". Start it before each open; when it runs already, the start only fails, so
            // the log of the I2S driver is quiet for that one call.)
            if (s_i2s_rx) {
                const esp_log_level_t lv = esp_log_level_get("i2s_common");
                esp_log_level_set("i2s_common", ESP_LOG_NONE);
                i2s_channel_enable(s_i2s_rx);
                esp_log_level_set("i2s_common", lv);
            }
            if (esp_codec_dev_open(s_mic, &fs) == ESP_CODEC_DEV_OK) {
                esp_codec_dev_set_in_gain(s_mic, 30.0f);
                s_mic_open = true;
            } else {
                s_mic_want = false;
            }
        } else if (!MIC_WANT && s_mic_open) {
            esp_codec_dev_close(s_mic);
            s_mic_open = false;
            s_mic_level = 0;
        }
        if (!s_mic_open) {
            vTaskDelay(pdMS_TO_TICKS(s_sleep_want ? 200 : 20));  // (fewer CPU wakes with the screen off)
            continue;
        }
        if (esp_codec_dev_read(s_mic, buf, sizeof(buf)) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        int64_t sum = 0;
        lock_take();  // (the record buffer: audio_mic_record_stop and audio_stop_mem)
        int16_t *rec = s_rec_buf;
        for (int i = 0; i < MIC_FRAMES; i++) {
            int32_t m = ((int32_t)buf[2 * i] + buf[2 * i + 1]) / 2;
            sum += m * m;
            if (rec && s_rec_n < s_rec_max) {
                rec[s_rec_n++] = (int16_t)m;
            }
        }
        if (rec && s_rec_n >= s_rec_max) {
            s_rec_buf = NULL;
        }
        lock_give();
        if (s_mic_tap || s_mic_tap_web) {
            static int16_t mono[MIC_FRAMES];
            for (int i = 0; i < MIC_FRAMES; i++) {
                mono[i] = (int16_t)(((int32_t)buf[2 * i] + buf[2 * i + 1]) / 2);
            }
            mic_taps(mono);
        }
        s_mic_level = sqrtf((float)sum / MIC_FRAMES) / 32768.0f;
    }
}

bool audio_mic_start(void)
{
    if (!s_mic) {
        return false;
    }
    s_mic_want = true;
    return true;
}

void audio_mic_stop(void)
{
    s_mic_want = false;
    s_rec_buf = NULL;
    lock_barrier();  // (the mic task writes no more into the record buffer)
}

bool audio_mic_on(void)
{
    return s_mic_open;
}

float audio_mic_level(void)
{
    return s_mic_level;
}

void audio_mic_tap(void (*cb)(const int16_t *s, int n))
{
    s_mic_tap = cb;
    tap_barrier();  // (the old function gets no more calls: its data can go)
}

void audio_mic_web(void (*cb)(const int16_t *s, int n))
{
    s_mic_tap_web = cb;
    tap_barrier();
}

void audio_mic_record(int16_t *buf, uint32_t max)
{
    const bool held = lock_in();  // (the mic task changes to the new buffer between two copies)
    s_rec_n = 0;
    s_rec_max = max;
    s_rec_buf = buf;
    lock_out(held);
}

void audio_mic_record_stop(void)
{
    s_rec_buf = NULL;
    lock_barrier();  // (the mic task writes no more into the buffer: the caller can free it)
}

bool audio_mic_recording(void)
{
    return s_rec_buf != NULL;
}

uint32_t audio_mic_recorded(void)
{
    return s_rec_n;
}

void audio_capture(int16_t *buf, uint32_t max, uint32_t skip)
{
    const bool held = lock_in();  // (the audio task changes to the new buffer between two blocks)
    s_cap_n = 0;
    s_cap_skip = skip;
    s_cap_max = max;
    s_cap_buf = buf;
    lock_out(held);
}

void audio_capture_stop(void)
{
    s_cap_buf = NULL;
    lock_barrier();  // (the audio task writes no more into the buffer: the caller can free it)
}

bool audio_capturing(void)
{
    return s_cap_buf != NULL;
}

uint32_t audio_captured(void)
{
    return s_cap_n;
}

// ---------------------------------------------------------------- the stop of the sounds that use a memory block
// true when the n bytes at p and the block [lo, hi) overlap
static bool span_in(const void *p, size_t n, uintptr_t lo, uintptr_t hi)
{
    const uintptr_t a = (uintptr_t)p;
    return p && a < hi && a + n > lo;
}

// the sounds that use [lo, hi) now: the sample voices, the TIA steps, the tune, the capture buffer and the record
// buffer. stop = true (with the lock): they stop. The result: their count.
static int mem_sounds(uintptr_t lo, uintptr_t hi, bool stop)
{
    int k = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        voice_t *v = &s_voices[i];
        if (v->on && v->wave == W_SAMPLE && span_in(v->smp, (size_t)v->smp_len * sizeof(int16_t), lo, hi)) {
            k++;
            if (stop) {
                v->on = false;
            }
        }
    }
    for (int c = 0; c < 2; c++) {
        tia_ch_t *t = &s_tia.ch[c];
        if (span_in(t->steps, (size_t)t->n * sizeof(tia_step_t), lo, hi)) {
            k++;
            if (stop) {
                t->steps = NULL;
                t->audc = 0;
            }
        }
    }
    if (span_in(s_tune, (size_t)s_tune_n * sizeof(tune_note_t), lo, hi)) {
        k++;
        if (stop) {
            s_tune = NULL;
        }
    }
    if (span_in(s_cap_buf, (size_t)s_cap_max * sizeof(int16_t), lo, hi)) {
        k++;
        if (stop) {
            s_cap_buf = NULL;
        }
    }
    if (span_in(s_rec_buf, (size_t)s_rec_max * sizeof(int16_t), lo, hi)) {
        k++;
        if (stop) {
            s_rec_buf = NULL;
        }
    }
    return k;
}

static sfx_msg_t s_qf[QUEUE_LEN] EXT_RAM_BSS_ATTR;  // (queue_scan: the messages while it looks at them)

// (with the lock) the messages of the queue that point into [lo, hi): their count. drop = true: they go. The other
// messages go back in their order, in front of a message that comes in the meantime.
static int queue_scan(uintptr_t lo, uintptr_t hi, bool drop)
{
    int n = 0, hit = 0;
    for (int i = 0; i < 2 * QUEUE_LEN && n < QUEUE_LEN && xQueueReceive(s_queue, &s_qf[n], 0) == pdTRUE; i++) {
        uintptr_t a, b;
        if (msg_span(&s_qf[n], &a, &b) && a < hi && b > lo) {
            hit++;
            if (drop) {
                msg_done(&s_qf[n]);
                continue;
            }
        }
        n++;
    }
    while (n > 0) {
        n--;
        if (xQueueSendToFront(s_queue, &s_qf[n], 0) != pdTRUE) {
            msg_done(&s_qf[n]);  // (no room: messages came in the meantime, and this one goes)
        }
    }
    return hit;
}

int audio_mem_check(const void *p, size_t n, bool stop)
{
    if (!p || !n) {
        return 0;
    }
    const uintptr_t lo = (uintptr_t)p, hi = lo + n;
    // the usual case, with no lock: no message of the queue can point into the block, and no sound uses it. (The queue
    // first, then the sounds: the audio task puts the sound of a message in place before the message stops to count. A
    // voice that a tick starts comes from an app whose tick still runs: an app stops its tick before it frees the
    // memory of its sounds.)
    if (!q_may_point(lo, hi) && !mem_sounds(lo, hi, false)) {
        return 0;
    }
    const bool held = lock_in();
    int k = mem_sounds(lo, hi, stop);
    if (s_queue && q_may_point(lo, hi)) {
        k += queue_scan(lo, hi, stop);
    }
    lock_out(held);
    return k;
}

void audio_stop_mem(const void *p, size_t n)
{
    (void)audio_mem_check(p, n, true);
}

int audio_load(void)
{
    return (int)(s_load_us * 100 / 5000);
}

int audio_voices_on(void)
{
    int n = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        n += s_voices[i].on;
    }
    return n;
}

float audio_midi_hz(float note)
{
    return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f);
}

void audio_play(const audio_voice_t *v)
{
    if (s_queue && !s_muted) {
        sfx_msg_t m = {.id = SFX_COUNT + 1, .voice = *v};
        msg_send(&m);
    }
}

void audio_kill_group(uint8_t group)
{
    if (!s_queue) {
        return;
    }
    sfx_msg_t m = {.id = SFX_COUNT + 3, .tune_n = group};
    if (xQueueSend(s_queue, &m, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    // the message is done at the start of the next block; the release (10 ms) ends two blocks
    // later: wait for 4 blocks (or 200 ms if the audio task does not run)
    uint32_t b0 = s_blocks;
    for (int i = 0; i < 40 && s_blocks - b0 < 4; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void audio_tune(const tune_note_t *notes, int n)
{
    if (s_queue && !s_muted && notes && n > 0) {
        sfx_msg_t m = {.id = SFX_COUNT + 2, .tune = notes, .tune_n = (uint16_t)n};
        msg_send(&m);
    }
}

// audio_alert: the end of the alarm (esp_timer ms; 0 = none). The audio task then gives back the mute and the volume of
// the user (g_settings: a change on the settings panel during the alarm counts).
static volatile int64_t s_alert_end;

void audio_alert(const tune_note_t *notes, int n, int vol)
{
    if (!s_queue || !notes || n <= 0) {
        return;
    }
    int ms = 0;
    for (int i = 0; i < n; i++) {
        if (notes[i].t + notes[i].dur > ms) {
            ms = notes[i].t + notes[i].dur;
        }
    }
    vol = vol < 10 ? 10 : vol > 100 ? 100 : vol;
    if (!g_settings.mute && g_settings.volume > vol) {
        vol = g_settings.volume;
    }
    s_muted = false;
    audio_set_volume(vol);
    s_alert_end = esp_timer_get_time() / 1000 + ms + 400;
    sfx_msg_t m = {.id = SFX_COUNT + 2, .tune = notes, .tune_n = (uint16_t)n};
    msg_send(&m);
    ESP_LOGI(TAG, "alert: %d ms at volume %d", ms, vol);
}

static void alert_end_check(void)
{
    if (s_alert_end && esp_timer_get_time() / 1000 > s_alert_end) {
        s_alert_end = 0;
        audio_set_volume(g_settings.volume);
        s_muted = g_settings.mute;
    }
}

void audio_tune_stop(void)
{
    if (s_queue) {
        sfx_msg_t m = {.id = SFX_COUNT + 2, .tune = NULL, .tune_n = 0};
        xQueueSend(s_queue, &m, 0);
    }
}

static void tia_send(int ch, const tia_step_t *steps, int n, uint32_t skip, int times)
{
    if (!s_queue || s_muted || ch < 0 || ch > 1 || !steps || n <= 0) {
        return;
    }
    sfx_msg_t m = {.id = SFX_COUNT + 4};
    m.tia.steps = steps;
    m.tia.skip = skip;
    m.tia.n = (uint16_t)(n > 65535 ? 65535 : n);
    m.tia.ch = (uint8_t)ch;
    m.tia.times = (uint8_t)(times > 255 ? 255 : times);
    msg_send(&m);
}

void audio_tia_play(int ch, const tia_step_t *steps, int n, uint32_t skip)
{
    tia_send(ch, steps, n, skip, 0);
}

void audio_tia_loop(int ch, const tia_step_t *steps, int n, int times)
{
    tia_send(ch, steps, n, 0, times < 1 ? 1 : times);
}

void audio_tia_off(void)
{
    if (s_queue) {
        sfx_msg_t m = {.id = SFX_COUNT + 4};
        m.tia.ch = 0xFF;
        xQueueSend(s_queue, &m, 0);
    }
}

// an UNDERRUN: the DMA sent all blocks in its queue and the task had no new block (auto_clear: the speaker plays a gap)
static bool IRAM_ATTR on_send_q_ovf(i2s_chan_handle_t h, i2s_event_data_t *e, void *u)
{
    s_underrun++;
    return false;
}

// audio_init: the I2S channels in full duplex (the speaker and the mics), in the standard mode, and the speaker
// channel on
static void init_i2s(i2s_chan_handle_t *tx_out, i2s_chan_handle_t *rx_out)
{
    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = 3;  // short queue = low latency
    cc.dma_frame_num = BLOCK_FRAMES;
    cc.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&cc, &tx, &rx));  // full duplex: speaker and mics
    i2s_event_callbacks_t ecb = {.on_send_q_ovf = on_send_q_ovf};
    i2s_channel_register_event_callback(tx, &ecb, NULL);

    i2s_std_config_t sc = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg =
                    {
                            .mclk = PIN_I2S_MCLK,
                            .bclk = PIN_I2S_BCLK,
                            .ws = PIN_I2S_WS,
                            .dout = PIN_I2S_DOUT,
                            .din = PIN_I2S_DIN,
                    },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &sc));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &sc));
    s_i2s_rx = rx;
    // esp_codec_dev stops the channel before it sets the clock. It logs an error if
    // the channel is not running, so start it here.
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    *tx_out = tx;
    *rx_out = rx;
}

// audio_init: the codec of the speaker of this board; *ctrl_out gets its control interface
static const audio_codec_if_t *init_codec(const audio_codec_ctrl_if_t **ctrl_out)
{
#if BOARD_SPK_AW88298
    // board_init() keeps the AW88298 reset line (AW9523B P0_2) high and gives it power;
    // board_speaker() changes only its mute bit
    audio_codec_i2c_cfg_t i2c_cfg = {
            .port = I2C_NUM_0,
            .addr = AW88298_CODEC_DEFAULT_ADDR,
            .bus_handle = board_i2c0(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    aw88298_codec_cfg_t aw_cfg = {
            .ctrl_if = ctrl_if,
            .gpio_if = gpio_if,
            .reset_pin = -1,
            // (esp_codec_dev: the codec gets the volume in dB less 20 log10(dac / pa) + pa_gain. The same voltages = 0:
            // VOLUME 100 = 0 dB; before, 5.0 / 3.3 gave +3.6 dB, and the AW88298 cut it to 0 dB, so the top 7 % of the
            // VOLUME did nothing. A VOLUME over 100 is the gain of the mix, see audio_set_volume)
            .hw_gain =
                    {
                            .pa_voltage = 3.3,
                            .codec_dac_voltage = 3.3,
                    },
    };
    const audio_codec_if_t *codec_if = aw88298_codec_new(&aw_cfg);
#elif BOARD_SPK_ES8388
    audio_codec_i2c_cfg_t i2c_cfg = {
            .port = I2C_NUM_0,
            .addr = ES8388_CODEC_DEFAULT_ADDR,
            .bus_handle = board_i2c0(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    es8388_codec_cfg_t es_cfg = {
            .ctrl_if = ctrl_if,
            .gpio_if = gpio_if,
            .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
            .pa_pin = -1,  // the amplifier: SPK_EN of the IO expander 0x43 (board_speaker())
            .hw_gain =
                    {
                            // (VOLUME 100 = 0 dB in the codec: see the AW88298 above)
                            .pa_voltage = 3.3,
                            .codec_dac_voltage = 3.3,
                            .pa_gain = 0,
                    },
    };
    const audio_codec_if_t *codec_if = es8388_codec_new(&es_cfg);
#else
    audio_codec_i2c_cfg_t i2c_cfg = {
            .port = I2C_NUM_0,
            .addr = ES8311_CODEC_DEFAULT_ADDR,
            .bus_handle = board_i2c0(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es_cfg = {
            .ctrl_if = ctrl_if,
            .gpio_if = gpio_if,
            .codec_mode = MIC_SHARED ? ESP_CODEC_DEV_WORK_MODE_BOTH : ESP_CODEC_DEV_WORK_MODE_DAC,
            .pa_pin = -1,  // the amplifier: TCA9554 EXIO7 (3.49), GPIO46 (AMOLED), PCA9535 P1.4 (Watcher)
            .use_mclk = true,
            // (esp_codec_dev lowers the volume by 20 log10(dac / pa) + pa_gain. With pa 5.0 / dac 3.3 / pa_gain 0
            // the ES8311 gets +3.6 dB at VOLUME 100: each peak of the mix over 21700 clips IN THE CODEC, after the
            // limiter (distortion of the MIDI music at the top of the VOLUME). So VOLUME 100 must be
            // the real 100 %: the same voltages = 0 dB. A VOLUME over 100 (up to 125) is a clean gain of the mix, before the
            // limiter: audio_set_volume)
            .hw_gain =
                    {
                            .pa_voltage = 3.3,
                            .codec_dac_voltage = 3.3,
                            .pa_gain = 0,
                    },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
#endif
    *ctrl_out = ctrl_if;
    return codec_if;
}

// audio_init: the microphone device (s_mic) of this board
static void init_mic(const audio_codec_data_if_t *data_if)
{
#if MIC_SHARED
    // the microphone of the ES8311: the same device (open for both ways); the mic task reads it
    s_mic = s_dev;
    esp_codec_dev_set_in_gain(s_mic, 30.0f);
#elif MIC_ES7243
    // the microphone: an ES7243 at 0x13 or an ES7243E at 0x14 (the Seeed BSP checks both)
    const audio_codec_if_t *mic_if = NULL;
    if (i2c_master_probe(board_i2c0(), 0x13, 50) == ESP_OK) {
        audio_codec_i2c_cfg_t mic_i2c = {.port = I2C_NUM_0, .addr = 0x13 << 1, .bus_handle = board_i2c0()};
        es7243_codec_cfg_t c = {.ctrl_if = audio_codec_new_i2c_ctrl(&mic_i2c)};
        if (c.ctrl_if) {
            mic_if = es7243_codec_new(&c);
        }
    } else {
        audio_codec_i2c_cfg_t mic_i2c = {.port = I2C_NUM_0, .addr = 0x14 << 1, .bus_handle = board_i2c0()};
        es7243e_codec_cfg_t c = {.ctrl_if = audio_codec_new_i2c_ctrl(&mic_i2c)};
        if (c.ctrl_if) {
            mic_if = es7243e_codec_new(&c);
        }
    }
    if (mic_if) {
        esp_codec_dev_cfg_t mic_cfg = {
                .dev_type = ESP_CODEC_DEV_TYPE_IN,
                .codec_if = mic_if,
                .data_if = data_if,
        };
        s_mic = esp_codec_dev_new(&mic_cfg);
    }
#else
    // the microphones: ES7210 (MIC_SELECT), same data interface as the speaker
    audio_codec_i2c_cfg_t mic_i2c = {
            .port = I2C_NUM_0,
            .addr = ES7210_CODEC_DEFAULT_ADDR,
            .bus_handle = board_i2c0(),
    };
    const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&mic_i2c);
    es7210_codec_cfg_t es7210 = {
            .ctrl_if = mic_ctrl,
            .mic_selected = MIC_SELECT,
    };
    const audio_codec_if_t *mic_if = mic_ctrl ? es7210_codec_new(&es7210) : NULL;
    if (mic_if) {
        esp_codec_dev_cfg_t mic_cfg = {
                .dev_type = ESP_CODEC_DEV_TYPE_IN,
                .codec_if = mic_if,
                .data_if = data_if,
        };
        s_mic = esp_codec_dev_new(&mic_cfg);
    }
#endif
}

void audio_init(void)
{
    i2s_chan_handle_t tx = NULL, rx = NULL;
    init_i2s(&tx, &rx);

    audio_codec_i2s_cfg_t i2s_cfg = {
            .port = I2S_NUM_0,
            .tx_handle = tx,
            .rx_handle = rx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);

    const audio_codec_ctrl_if_t *ctrl_if = NULL;
    const audio_codec_if_t *codec_if = init_codec(&ctrl_if);
    if (!data_if || !ctrl_if || !codec_if) {
        ESP_LOGE(TAG, "codec setup failed, no sound");
        return;
    }

    esp_codec_dev_cfg_t dev_cfg = {
            .dev_type = MIC_SHARED ? ESP_CODEC_DEV_TYPE_IN_OUT : ESP_CODEC_DEV_TYPE_OUT,
            .codec_if = codec_if,
            .data_if = data_if,
    };
    s_dev = esp_codec_dev_new(&dev_cfg);

    esp_codec_dev_sample_info_t fs = {
            .sample_rate = SAMPLE_RATE,
            .channel = 2,
            .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_dev, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec open failed, no sound");
        s_dev = NULL;
        return;
    }

    s_queue = xQueueCreate(QUEUE_LEN, sizeof(sfx_msg_t));
    s_lock = xSemaphoreCreateMutex();  // (before the audio task and the mic task: they take it)
    // above the LCD task (7): the sound must never wait for the screen
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 8, &s_audio_task, 1);

    init_mic(data_if);
    // The stack of the microphone task is in PSRAM (this keeps 4 KB of internal RAM free for the BLE start of BT KEYS).
    // The task only reads the codec and calls the taps: it never writes the flash, so a PSRAM stack is safe.
    if (xTaskCreatePinnedToCoreWithCaps(mic_task, "mic", 4096, NULL, 5, NULL, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) !=
        pdPASS) {
        xTaskCreatePinnedToCore(mic_task, "mic", 4096, NULL, 5, NULL, 1);
    }
    ESP_LOGI(TAG, "sound on, mic %s", s_mic ? "found" : "not found");
}

void sfx_play(sfx_t sfx)
{
    if (s_queue && !s_muted) {
        sfx_msg_t m = {.id = (uint8_t)sfx};
        xQueueSend(s_queue, &m, 0);
    }
}

static int s_ui_snd = -1;  // UI SOUNDS: -1 = not read yet
bool audio_ui_sounds(void)
{
    if (s_ui_snd < 0) {
        s_ui_snd = store_get("ui_snd", 0) == 0;
    }
    return s_ui_snd != 0;
}
void audio_set_ui_sounds(bool on)
{
    s_ui_snd = on;
    store_set("ui_snd", on ? 0 : 1);
}
void sfx_ui(sfx_t sfx)
{
    if (s_queue && !s_muted && audio_ui_sounds()) {
        sfx_msg_t m = {.id = SFX_COUNT + 5};
        m.tune_n = (uint16_t)sfx;
        xQueueSend(s_queue, &m, 0);
    }
}

void sfx_tone(uint16_t f0, uint16_t f1, uint16_t dur_ms, wave_t wave, uint8_t vol)
{
    if (s_queue && !s_muted) {
        sfx_msg_t m = {
                .id = SFX_COUNT,
                .tone = {.delay_ms = 0, .dur_ms = dur_ms, .f0 = f0, .f1 = f1, .wave = (uint8_t)wave, .vol = vol},
        };
        xQueueSend(s_queue, &m, 0);
    }
}

void audio_set_volume(int vol)
{
    // 0..100: the codec (100 = 0 dB). Over 100: the codec at 0 dB and a gain before the limiter, 2 ^ ((vol - 100) / 25):
    // 110 = +2.4 dB, 125 = +6 dB (x 2). The limiter keeps the peaks under full scale: louder, but no clip.
    vol = vol < 0 ? 0 : vol > AUDIO_VOL_MAX ? AUDIO_VOL_MAX : vol;
    s_boost_q8 = vol <= 100 ? 256 : (int32_t)lroundf(256.0f * exp2f((float)(vol - 100) / 25.0f));
    if (vol > 100) {
        vol = 100;
    }
    s_vol = vol;
    if (s_dev && !s_asleep) {
        esp_codec_dev_set_out_vol(s_dev, vol);
    }
}

int audio_peak_take(void)
{
    uint32_t m = s_peak_max;
    s_peak_max = 0;
    return (int)(m * 100 / 32767);
}

void audio_set_mute(bool mute)
{
    s_muted = mute;
}

bool audio_muted(void)
{
    return s_muted;
}
