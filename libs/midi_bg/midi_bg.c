// MIDI_BG: background music from MIDI files (see midi_bg.h).
// The tracks are BGN data (bgn.h) or MIDI files, the playlists are bit masks (a track can be in
// more than one phase), and each note list is in the normal order of bgn_normalize() (no exact
// copies).
//
// Two slots: the track that plays, and the next track (loaded before it is needed).
// A loader task with a low priority loads (inflate + the MIDI reader) and frees, so a load
// (20-60 ms) never stops a frame of the game; the audio tick plays and switches. (Without the
// loader task, and on the host, the app task does it in midi_bg_service().)
// A slot goes EMPTY -> LOADING -> READY (app), READY -> PLAYING -> OLD (audio), OLD -> EMPTY
// (app). Only a compare-and-swap moves a READY slot, so the two tasks never take the same slot.
//
// Each audio block: the notes of the block start with their exact delay. A block can hold a
// boundary: the loop end (HOLD), a segue point (a change of the phase, on a 4-bar line) or the
// end of the track; the rest of the block then plays from the new place.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/audio.h"
#include "drumkit.h"
#include "tfn/midifile.h"
#include "bgn.h"
#include "instr.h"
#include "midi_bg.h"

#ifdef MIDI_BG_DEBUG
int g_dbg_notes;
#endif
#include "miniz.h"  // the inflate of the ESP32-S3 ROM (tinfl); a copy on the host
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#define PS_ALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM)
#else
#define PS_ALLOC(n) malloc(n)
#endif
#if defined(TFN_SIMULATOR) && !defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  // (the simulator has an audio task: midi_bg_end waits for its tick as on the board)
#endif

#ifndef MIDI_BG_HOST_DIR
#define MIDI_BG_HOST_DIR "src2"
#endif
#ifndef MIDI_BG_HOST_EXT
#define MIDI_BG_HOST_EXT ".mid"
#endif

#define G_BG          40  // voice groups: G_BG + 16 * slot + channel
#define N_VEND        48  // the music voices that the estimate follows
#define PHRASE_BARS   4
#define MAX_PHRASES   160  // 4-bar phrases: 640 bars
#define SEGUE_FADE_MS 900
#define CUT_FADE_MS   40
#define MAX_NOTE      (8 * AUDIO_SAMPLE_RATE)
// the voices and the length (ms) of each drum of drumkit.h (an estimate for the 808 and 909 kits)
static const uint8_t DRUM_VOICES[DK_COUNT] = {3, 3, 4, 1, 1, 2, 2, 1, 2, 2, 2, 1};
static const uint16_t DRUM_MS[DK_COUNT] = {300, 200, 200, 50, 380, 280, 230, 1600, 1100, 25, 200, 60};

enum {
    SL_EMPTY,
    SL_LOADING,
    SL_READY,
    SL_PLAYING,
    SL_OLD
};

typedef struct {
    volatile int state;
    int track;
    midi_notes_t m;
    double tpb, bar_beats, origin, bpm;
    double start_at;  // the beat where it starts (set at the load)
    bool drums;
    uint8_t instr[16], prio[16];
    uint16_t mute[MAX_PHRASES];  // bit c: channel c does not play in this phrase (the voice limit)
    float load;                  // the mean voices of the channels that play
} slot_t;

static const bg_track_t *s_tracks;
static int s_ntracks;
static int s_voices_max = 20;
static slot_t s_slot[2];

// written by the app task
static volatile int s_phase = -1;
static volatile bool s_hold;
static volatile bool s_run;
static volatile int s_vol = 70;
static volatile int s_duck = 100;
static volatile uint32_t s_duck_samples;  // the duck lasts this long (from the audio side)
static volatile int s_stop_fade = -1;     // >= 0: stop with this fade (the audio side does it)
static volatile int s_force = -1;         // a slot to start at once (midi_bg_play_track)
static int s_list_pos[MIDI_BG_PHASES];

// the audio side
static int s_cur = -1;
static double s_pos;             // beats from the start of the file
static int s_idx;                // the next note
static uint32_t s_clock;         // samples
static double s_switch_at = -1;  // a segue point in the current track (beats)
static uint32_t s_duck_end;
static uint32_t s_vend[N_VEND];  // the end (clock) of each music voice
static uint8_t s_vslot[N_VEND];  // the slot of each music voice
static volatile int s_passes, s_notes, s_dropped, s_thinned;
static volatile int s_over_drum;  // the highest drum velocity asked for, percent of 127 (midi_bg_over_take)
static bool s_tick_on;            // the tick is connected (midi_bg_start)
static uint32_t s_seen_tick = UINT32_MAX;
static uint16_t s_seen[128];  // bit c: channel c has this pitch at s_seen_tick
static int s_load_ms;

static bool cas(volatile int *p, int from, int to)
{
    int expect = from;
    return __atomic_compare_exchange_n((int *)p, &expect, to, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

// ---------------------------------------------------------------- loading (app task)

static bool in_phase(const bg_track_t *t, int phase)
{
    return phase >= 0 && (t->phases >> phase & 1);
}

// the data of a track: BGN (the compact form, see bgn.h) or a Standard MIDI file
static bool parse(const uint8_t *data, size_t len, midi_notes_t *m)
{
    char err[40];
    if (len >= 4 && !memcmp(data, BGN_MAGIC, 4)) {
        return bgn_decode(data, len, m);
    }
    return midifile_read_notes_data(data, len, m, err, sizeof(err));
}

static bool read_track(const bg_track_t *t, midi_notes_t *m)
{
    bool ok = false;
    if (t->z) {
        // tinfl_decompress_mem_to_mem() keeps its 11 KB state on the stack (too much for the app
        // task): this state is on the heap
        uint8_t *raw = PS_ALLOC(t->rawlen ? t->rawlen : 1);
        tinfl_decompressor *dec = PS_ALLOC(sizeof(tinfl_decompressor));
        if (raw && dec) {
            tinfl_init(dec);
            size_t in = t->zlen, out = t->rawlen;
            tinfl_status st = tinfl_decompress(dec, t->z, &in, raw, raw, &out,
                                               TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            ok = st == TINFL_STATUS_DONE && out == t->rawlen && parse(raw, out, m);
        }
        free(dec);
        free(raw);
    }
#ifndef ESP_PLATFORM
    else {
        // the host tools: the file <MIDI_BG_HOST_DIR>/<name><MIDI_BG_HOST_EXT> (.mid or .bgn)
        char path[160];
        snprintf(path, sizeof(path), "%s/%s%s", MIDI_BG_HOST_DIR, t->name, MIDI_BG_HOST_EXT);
        FILE *f = fopen(path, "rb");
        if (!f) {
            return false;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
        if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
            ok = parse(buf, (size_t)n, m);
        }
        free(buf);
        fclose(f);
    }
#endif
    // the normal order, and the exact copies go (BGN data is in this form already)
    if (ok) {
        bgn_normalize(m);
    }
    return ok;
}

// the instrument and the priority of each channel:
// 0 = the drums, 1 = the bass and the lead (the busiest high part), 2 = the rest, 3 = pads.
// Then for each 4-bar phrase the channels that play: from the most important, while the mean
// voices of the parts stay at LOAD_PART of the limit (the peaks are higher: the limiter thins
// them). A channel that does not fit is quiet for the whole phrase, so no part loses single
// notes (that sounds like a fault), and a thin part of the piece keeps all its parts.
#define LOAD_PART 0.65f

static void plan_channels(slot_t *sl)
{
    int count[16] = {0}, prog[16];
    float sum[16] = {0};
    for (int c = 0; c < 16; c++) {
        prog[c] = -1;
    }
    for (int k = 0; k < sl->m.n; k++) {
        const midi_note_t *a = &sl->m.notes[k];
        count[a->ch]++;
        sum[a->ch] += a->pitch;
        if (prog[a->ch] < 0) {
            prog[a->ch] = a->prog;
        }
    }
    int lead = -1, bass = -1;
    float low = 1e9f;
    for (int c = 0; c < 16; c++) {
        if (c == 9 || !count[c]) {
            continue;
        }
        float mean = sum[c] / count[c];
        sl->instr[c] = (uint8_t)instr_from_gm(prog[c], mean);
        if (mean >= 60 && (lead < 0 || count[c] > count[lead])) {
            lead = c;
        }
        if (mean < low) {
            low = mean;
            bass = c;
        }
    }
    for (int c = 0; c < 16; c++) {
        if (c == 9) {
            sl->prio[c] = 0;
        } else if (c == lead || c == bass || instr_is_bass(sl->instr[c])) {
            sl->prio[c] = 1;
        } else if (instr_is_pad(sl->instr[c])) {
            sl->prio[c] = 3;
        } else {
            sl->prio[c] = 2;
        }
    }
    sl->drums = count[9] > 0;

    // the voices of each channel in each phrase (4 bars), from the notes that start in it
    double sps = 60.0 / sl->bpm * AUDIO_SAMPLE_RATE / sl->tpb;  // samples for each tick
    double ph_ticks = PHRASE_BARS * sl->bar_beats * sl->tpb;
    double span = ph_ticks * sps;  // samples in a phrase
    static float vs[MAX_PHRASES][16];
    memset(vs, 0, sizeof(vs));
    for (int k = 0; k < sl->m.n; k++) {
        const midi_note_t *a = &sl->m.notes[k];
        int p = (int)((a->on - sl->origin * sl->tpb) / ph_ticks);
        if (p < 0) {
            p = 0;
        }
        if (p >= MAX_PHRASES) {
            p = MAX_PHRASES - 1;
        }
        if (a->ch == 9) {
            int dk = instr_gm_drum(a->pitch);
            vs[p][9] += (float)(DRUM_VOICES[dk] * DRUM_MS[dk] * (AUDIO_SAMPLE_RATE / 1000) / span);
        } else {
            uint32_t dur = (uint32_t)((a->off - a->on) * sps);
            if (dur > MAX_NOTE) {
                dur = MAX_NOTE;
            }
            vs[p][a->ch] += (float)(instr_note_len(sl->instr[a->ch], dur, true) *
                                    (double)instr_layers(sl->instr[a->ch]) / span);
        }
    }
    // the order: the priority, then the number of notes
    int order[16], n = 0;
    for (int c = 0; c < 16; c++) {
        if (count[c]) {
            order[n++] = c;
        }
    }
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0; j--) {
            int a = order[j - 1], b = order[j];
            bool swap = sl->prio[b] < sl->prio[a] || (sl->prio[b] == sl->prio[a] && count[b] > count[a]);
            if (!swap) {
                break;
            }
            order[j - 1] = b;
            order[j] = a;
        }
    }
    // each phrase: the channels from the most important while they fit
    float cap = s_voices_max * LOAD_PART, sum_load = 0;
    int phrases = 0;
    for (int p = 0; p < MAX_PHRASES; p++) {
        float load = 0;
        uint16_t mask = 0;
        for (int i = 0; i < n; i++) {
            int c = order[i];
            if (sl->prio[c] > 1 && load + vs[p][c] > cap) {
                mask |= (uint16_t)(1u << c);
                continue;
            }
            load += vs[p][c];
        }
        sl->mute[p] = mask;
        if (load > 0) {
            sum_load += load;
            phrases++;
        }
    }
    sl->load = phrases ? sum_load / phrases : 0;
}

// at < 0: from bar 1 (the first bar with a note)
static bool load_slot(int k, int track, double at)
{
    slot_t *sl = &s_slot[k];
    const bg_track_t *t = &s_tracks[track];
    memset(&sl->m, 0, sizeof(sl->m));
#ifdef ESP_PLATFORM
    int64_t t0 = esp_timer_get_time();
#endif
    if (!read_track(t, &sl->m)) {
        sl->state = SL_EMPTY;
        return false;
    }
    sl->track = track;
    sl->tpb = sl->m.tpb;
    sl->bpm = sl->m.bpm < 40 ? 40 : sl->m.bpm > 220 ? 220 : sl->m.bpm;
    sl->bar_beats = sl->m.ts_num * 4.0 / (sl->m.ts_den ? sl->m.ts_den : 4);
    double first = sl->m.notes[0].on / sl->tpb;
    sl->origin = floor(first / sl->bar_beats) * sl->bar_beats;
    sl->start_at = at < 0 ? sl->origin : at;
    plan_channels(sl);
#ifdef ESP_PLATFORM
    s_load_ms = (int)((esp_timer_get_time() - t0) / 1000);
#endif
    __atomic_store_n(&sl->state, SL_READY, __ATOMIC_RELEASE);
    return true;
}

static void free_slot(slot_t *sl)
{
    midifile_notes_free(&sl->m);
    __atomic_store_n(&sl->state, SL_EMPTY, __ATOMIC_RELEASE);
}

// the next track of a phase after the one that plays (-1: none, or only that one)
static int next_in_phase(int phase, int cur)
{
    int n = 0;
    for (int i = 0; i < s_ntracks; i++) {
        n += in_phase(&s_tracks[i], phase);
    }
    if (n == 0 || (n == 1 && cur >= 0 && in_phase(&s_tracks[cur], phase))) {
        return -1;
    }
    for (int step = 0; step < s_ntracks; step++) {
        int i = (s_list_pos[phase] + step) % s_ntracks;
        if (in_phase(&s_tracks[i], phase) && i != cur) {
            s_list_pos[phase] = (i + 1) % s_ntracks;
            return i;
        }
    }
    return -1;
}

static void service_body(void)
{
    for (int k = 0; k < 2; k++) {
        if (s_slot[k].state == SL_OLD) {
            free_slot(&s_slot[k]);
        }
    }
    int phase = s_phase;
    if (!s_run || phase < 0) {
        return;
    }
    // a next track of another phase (the phase changed): load a new one
    for (int k = 0; k < 2; k++) {
        if (s_slot[k].state == SL_READY && !in_phase(&s_tracks[s_slot[k].track], phase) &&
            cas(&s_slot[k].state, SL_READY, SL_LOADING)) {
            free_slot(&s_slot[k]);
        }
    }
    int free_k = -1, busy = 0;
    for (int k = 0; k < 2; k++) {
        int st = s_slot[k].state;
        if (st == SL_EMPTY && free_k < 0) {
            free_k = k;
        }
        if (st == SL_READY || st == SL_LOADING) {
            busy++;
        }
    }
    if (free_k < 0 || busy) {
        return;
    }
    int cur = s_cur >= 0 ? s_slot[s_cur].track : -1;
    int tr = next_in_phase(phase, cur);
    if (tr < 0) {
        return;
    }
    s_slot[free_k].state = SL_LOADING;
    load_slot(free_k, tr, s_tracks[tr].entry);  // a game starts in the full part of a track
}

#ifdef ESP_PLATFORM
// the loader task: its stack is in PSRAM (no internal RAM); it lives until the restart
static TaskHandle_t s_loader;
static SemaphoreHandle_t s_load_mx;  // service_body, midi_bg_end and the jukebox: one at a time

static void loader_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        xSemaphoreTake(s_load_mx, portMAX_DELAY);
        service_body();
        xSemaphoreGive(s_load_mx);
    }
}

static void loader_start(void)
{
    if (s_loader) {
        return;
    }
    if (!s_load_mx) {
        s_load_mx = xSemaphoreCreateMutex();
    }
    if (!s_load_mx) {
        return;
    }
    // core 1, priority 1: the app task (core 0, priority 1) never shares its core with a load; on
    // core 1 the render worker, the IMU and the audio task have higher priorities
    if (xTaskCreatePinnedToCoreWithCaps(loader_task, "bgload", 6144, NULL, 1, &s_loader, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_loader = NULL;
    }
}

static void lock(void)
{
    if (s_load_mx) {
        xSemaphoreTake(s_load_mx, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_load_mx) {
        xSemaphoreGive(s_load_mx);
    }
}
#else
static void lock(void)
{
}
static void unlock(void)
{
}
#endif

void midi_bg_service(void)
{
#ifdef ESP_PLATFORM
    if (s_loader) {
        xTaskNotifyGive(s_loader);
        return;
    }
#endif
    service_body();
}

// ---------------------------------------------------------------- the API (app task)

void midi_bg_init(const bg_track_t *tracks, int n, int voices)
{
    s_tracks = tracks;
    s_ntracks = n;
    s_voices_max = voices < 4 ? 4 : voices > N_VEND ? N_VEND : voices;
    memset(s_list_pos, 0, sizeof(s_list_pos));
}

void midi_bg_start(void)
{
#ifdef ESP_PLATFORM
    loader_start();
#endif
    s_stop_fade = -1;
    s_run = true;
    s_tick_on = true;
    audio_set_tick(midi_bg_tick);
}

void midi_bg_stop(int fade_ms)
{
    s_stop_fade = fade_ms < 1 ? 1 : fade_ms;
}

void midi_bg_set_phase(int phase)
{
    if (phase >= 0 && phase < MIDI_BG_PHASES) {
        s_phase = phase;
    }
}

void midi_bg_hold(bool hold)
{
    s_hold = hold;
}

int midi_bg_over_take(void)
{
    int a = instr_over_take(), b = s_over_drum;
    s_over_drum = 0;
    return a > b ? a : b;
}

void midi_bg_volume(int vol)
{
    s_vol = vol < 0 ? 0 : vol > 600 ? 600 : vol;  // (above 100: a quiet piece louder; SUNSET VIEW up to 300 % of a
                                                  // piece of 255 %; instr_play keeps each layer at 255 or less)
}

void midi_bg_duck(int level, int ms)
{
    s_duck_samples = (uint32_t)(ms < 0 ? 0 : ms) * (AUDIO_SAMPLE_RATE / 1000);
    s_duck = level < 0 ? 0 : level > 100 ? 100 : level;
}

static bool play_track(int i, float at, bool hold);

bool midi_bg_play_track(int i, float at, bool hold)
{
    lock();
    bool ok = play_track(i, at, hold);
    unlock();
    return ok;
}

static bool play_track(int i, float at, bool hold)
{
    if (i < 0 || i >= s_ntracks) {
        return false;
    }
    // a free slot (a waiting next track can go)
    int k = -1;
    for (int j = 0; j < 2 && k < 0; j++) {
        if (s_slot[j].state == SL_EMPTY) {
            k = j;
        } else if (s_slot[j].state == SL_OLD) {
            free_slot(&s_slot[j]);
            k = j;
        } else if (s_slot[j].state == SL_READY && cas(&s_slot[j].state, SL_READY, SL_LOADING)) {
            free_slot(&s_slot[j]);
            k = j;
        }
    }
    if (k < 0) {
        return false;
    }
    s_slot[k].state = SL_LOADING;
    if (!load_slot(k, i, at)) {
        return false;
    }
    s_hold = hold;
    // the first phase of the track
    for (int ph = 0; ph < MIDI_BG_PHASES; ph++) {
        if (in_phase(&s_tracks[i], ph)) {
            s_phase = ph;
            break;
        }
    }
    s_notes = s_dropped = s_thinned = 0;
    s_force = k;
    return true;
}

void midi_bg_end(void)
{
    if (s_tick_on) {
        s_stop_fade = CUT_FADE_MS;
#if defined(ESP_PLATFORM) || defined(TFN_SIMULATOR)
        // (the board and the simulator: the tick of the audio task starts the fade and confirms it)
        for (int i = 0; i < 20 && s_stop_fade >= 0; i++) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
#else
        midi_bg_tick();  // the host tests have no audio task
#endif
        audio_set_tick(NULL);  // (the tick is not in a call now and gets no call: the slots can go)
        s_tick_on = false;
    }
    lock();  // a load that runs now comes to its end first
    s_run = false;
    for (int k = 0; k < 2; k++) {
        if (s_slot[k].state != SL_EMPTY) {
            free_slot(&s_slot[k]);
        }
    }
    s_cur = -1;
    unlock();
    s_stop_fade = -1;
    s_force = -1;
    s_phase = -1;
    s_hold = false;
    s_switch_at = -1;
    s_duck_samples = 0;
    s_duck_end = s_clock;
    for (int i = 0; i < N_VEND; i++) {
        s_vend[i] = s_clock;
    }
    s_seen_tick = UINT32_MAX;
}

int midi_bg_load_ms(void)
{
    return s_load_ms;
}

void midi_bg_state(midi_bg_state_t *st)
{
    int c = s_cur;
    memset(st, 0, sizeof(*st));
    st->track = c >= 0 ? s_slot[c].track : -1;
    st->name = st->track >= 0 ? s_tracks[st->track].name : "";
    st->beat = (float)s_pos;
    st->bar = c >= 0 ? 1 + (int)floor((s_pos - s_slot[c].origin) / s_slot[c].bar_beats) : 0;
    st->passes = s_passes;
    int n = 0;
    for (int i = 0; i < N_VEND; i++) {
        n += (int32_t)(s_vend[i] - s_clock) > 0;
    }
    st->voices = n;
    st->notes = s_notes;
    if (c >= 0) {
        int p = (int)((s_pos - s_slot[c].origin) / (PHRASE_BARS * s_slot[c].bar_beats));
        st->muted = s_slot[c].mute[p < 0 ? 0 : p >= MAX_PHRASES ? MAX_PHRASES - 1 : p];
    }
    st->load = c >= 0 ? s_slot[c].load : 0;
    st->dropped = s_dropped;
    st->thinned = s_thinned;
}

// ---------------------------------------------------------------- playing (audio tick)

static int voices_on(uint32_t at)
{
    int n = 0;
    for (int i = 0; i < N_VEND; i++) {
        n += (int32_t)(s_vend[i] - at) > 0;
    }
    return n;
}

static void add_voices(int n, uint32_t start, uint32_t end, int k)
{
    for (int i = 0; i < N_VEND && n > 0; i++) {
        if ((int32_t)(s_vend[i] - start) <= 0) {
            s_vend[i] = end;
            s_vslot[i] = (uint8_t)k;
            n--;
        }
    }
}

static int level_now(void)
{
    if (s_duck_samples) {
        s_duck_end = s_clock + s_duck_samples;
        s_duck_samples = 0;
    }
    int duck = (int32_t)(s_duck_end - s_clock) > 0 ? s_duck : 100;
    return s_vol * duck / 100;
}

static void play_note(slot_t *sl, int k, const midi_note_t *a, uint32_t d, double bps, int level)
{
    const bg_track_t *t = &s_tracks[sl->track];
    int ch = a->ch;
    int p = (int)((a->on / sl->tpb - sl->origin) / (PHRASE_BARS * sl->bar_beats));
    if (p < 0) {
        p = 0;
    }
    if (p >= MAX_PHRASES) {
        p = MAX_PHRASES - 1;
    }
    if (sl->mute[p] & (1u << ch)) {
        return;
    }
    int vol = level * t->level / 100 * t->gain[ch] / 100 * (25 + a->vel * 75 / 127) / 100;
    if (vol <= 0) {
        return;
    }
    s_notes++;
    uint32_t at = s_clock + d;
    int on = voices_on(at), prio = sl->prio[ch], layers = 3;
#ifdef MIDI_BG_DEBUG
    if (g_dbg_notes) {
        printf("  beat %.2f ch %d pitch %d prio %d instr %d on %d\n", a->on / sl->tpb, ch + 1, a->pitch, prio,
               sl->instr[ch], on);
    }
#endif
    if (on >= s_voices_max + 2 && prio > 0) {
        // the hard limit: only the drums go on (the game keeps its voices)
        s_dropped++;
        return;
    }
    if (on >= s_voices_max) {
        if (prio >= 2) {
            s_dropped++;
            return;
        }
        layers = 1;
        s_thinned++;
    } else if (on >= s_voices_max * 3 / 4 && prio >= 2) {
        layers = 1;
        s_thinned++;
    } else if (on >= s_voices_max / 2 && prio >= 3) {
        layers = 1;
        s_thinned++;
    }
#ifdef TFN_SIMULATOR
    // (the simulator: the recordings of DEMO take the effects of an app without its music; sim_rec.c)
    extern volatile int g_sim_music_off;
    if (g_sim_music_off) {
        return;
    }
#endif
    if (ch == 9) {
        int dk = instr_gm_drum(a->pitch);
        if (on >= s_voices_max + 4 && dk != DK_KICK && dk != DK_SNARE && dk != DK_CLAP) {
            // the drums too have a limit: only the beat (kick, snare, clap) goes on
            s_dropped++;
            return;
        }
        int vel = a->vel * level * t->level / 100 * t->gain[9] / 10000;
        if (vel * 100 / 127 > s_over_drum) {
            s_over_drum = vel * 100 / 127;
        }
        drum_voice(t->kit, dk, vel < 1 ? 1 : vel > 127 ? 127 : vel, 0, d);
        add_voices(DRUM_VOICES[dk], at, at + DRUM_MS[dk] * (AUDIO_SAMPLE_RATE / 1000), k);
        return;
    }
    double len = (a->off - a->on) / sl->tpb / bps;
    uint32_t dur = len < AUDIO_SAMPLE_RATE / 100 ? AUDIO_SAMPLE_RATE / 100 : len > MAX_NOTE ? MAX_NOTE : (uint32_t)len;
    uint32_t vlen = 0;
    int n = instr_play(sl->instr[ch], a->pitch, d, dur, vol, 70, layers, (uint8_t)(G_BG + 16 * k + ch), true, true,
                       &vlen);
    add_voices(n, at, at + vlen, k);
}

// the notes that start in [from, to) beats; d0 = the samples of the block before `from`
// The same note (channel and pitch; for the drums the drum of the kit) more than once at the same
// time: many files have doubled parts (two tracks on channel 10). It plays once.

static bool doubled(const midi_note_t *a)
{
    if (a->on != s_seen_tick) {
        memset(s_seen, 0, sizeof(s_seen));
        s_seen_tick = a->on;
    }
    int key = a->ch == 9 ? instr_gm_drum(a->pitch) : a->pitch;
    uint16_t bit = (uint16_t)(1u << a->ch);
    if (s_seen[key & 127] & bit) {
        return true;
    }
    s_seen[key & 127] |= bit;
    return false;
}

static void play_span(int k, double from, double to, uint32_t d0, double bps)
{
    slot_t *sl = &s_slot[k];
    int level = level_now();
    while (s_idx < sl->m.n) {
        const midi_note_t *a = &sl->m.notes[s_idx];
        double b = a->on / sl->tpb;
        if (b >= to) {
            break;
        }
        s_idx++;
        if (b < from || doubled(a)) {
            continue;
        }
        uint32_t d = d0 + (uint32_t)((b - from) / bps);
        if (d >= AUDIO_BLOCK) {
            d = AUDIO_BLOCK - 1;
        }
        play_note(sl, k, a, d, bps, level);
    }
}

static void seek(int k, double beat)
{
    slot_t *sl = &s_slot[k];
    double tick = beat * sl->tpb;
    int lo = 0, hi = sl->m.n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (sl->m.notes[mid].on < tick) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    s_idx = lo;
    s_pos = beat;
}

static void release_slot(int k, int ms)
{
    for (int c = 0; c < 16; c++) {
        audio_release_group((uint8_t)(G_BG + 16 * k + c), (uint16_t)ms);
    }
    // the estimate: the voices of the slot now end in ms
    uint32_t end = s_clock + (uint32_t)ms * (AUDIO_SAMPLE_RATE / 1000);
    for (int i = 0; i < N_VEND; i++) {
        if (s_vslot[i] == k && (int32_t)(s_vend[i] - end) > 0) {
            s_vend[i] = end;
        }
    }
}

static void crash(int k, uint32_t d)
{
    slot_t *sl = &s_slot[k];
    if (!sl->drums) {
        return;
    }
    int vel = 100 * level_now() * s_tracks[sl->track].level / 100 * s_tracks[sl->track].gain[9] / 10000;
    drum_voice(s_tracks[sl->track].kit, DK_CRASH, vel < 1 ? 1 : vel > 127 ? 127 : vel, 0, d);
}

// a READY slot (want_phase >= 0: only a track of that phase)
static int take_ready(int want_phase)
{
    for (int k = 0; k < 2; k++) {
        if (k == s_cur || s_slot[k].state != SL_READY) {
            continue;
        }
        if (want_phase >= 0 && !in_phase(&s_tracks[s_slot[k].track], want_phase)) {
            continue;
        }
        if (cas(&s_slot[k].state, SL_READY, SL_PLAYING)) {
            return k;
        }
    }
    return -1;
}

// the new slot k starts at its start_at, d samples into the block; the old one fades
static void switch_to(int k, uint32_t d, int fade_ms, bool mark)
{
    if (s_cur >= 0) {
        release_slot(s_cur, fade_ms);
        __atomic_store_n(&s_slot[s_cur].state, SL_OLD, __ATOMIC_RELEASE);
    }
    s_cur = k;
    s_switch_at = -1;
    s_passes = 0;
    seek(k, s_slot[k].start_at);
    if (mark) {
        crash(k, d);
    }
}

void midi_bg_tick(void)
{
    if (s_stop_fade >= 0) {
        // stop: the notes fade, the slots go to the app to free
        for (int k = 0; k < 2; k++) {
            release_slot(k, s_stop_fade);
            if (s_slot[k].state == SL_PLAYING || s_slot[k].state == SL_READY) {
                s_slot[k].state = SL_OLD;
            }
        }
        s_cur = -1;
        s_run = false;
        s_stop_fade = -1;  // the last: midi_bg_end() waits for it
        return;
    }
    if (!s_run) {
        return;
    }
    int force = s_force;
    if (force >= 0 && s_slot[force].state == SL_READY && cas(&s_slot[force].state, SL_READY, SL_PLAYING)) {
        s_force = -1;
        switch_to(force, 0, CUT_FADE_MS, false);
    }
    if (s_cur < 0) {
        int k = take_ready(-1);
        if (k < 0) {
            s_clock += AUDIO_BLOCK;
            return;
        }
        switch_to(k, 0, CUT_FADE_MS, false);
    }
    uint32_t done = 0;
    for (int guard = 0; guard < 6 && done < AUDIO_BLOCK; guard++) {
        slot_t *sl = &s_slot[s_cur];
        const bg_track_t *t = &s_tracks[sl->track];
        double bps = sl->bpm / 60.0 / AUDIO_SAMPLE_RATE;  // beats for each sample
        bool hold = s_hold;
        // a change of the phase: a segue on the next phrase line (4 bars from bar 1); a track
        // that is also in the new phase goes on
        if (!hold && s_switch_at < 0 && !in_phase(t, s_phase)) {
            for (int k = 0; k < 2; k++) {
                if (k != s_cur && s_slot[k].state == SL_READY && in_phase(&s_tracks[s_slot[k].track], s_phase)) {
                    double ph = PHRASE_BARS * sl->bar_beats;
                    s_switch_at = sl->origin + ceil((s_pos - sl->origin) / ph + 1e-6) * ph;
                }
            }
        }
        double end = s_pos + (AUDIO_BLOCK - done) * bps, bnd = end;
        int what = 0;  // 1 loop, 2 segue, 3 the end of the track
        if (hold && t->loop_e > s_pos && t->loop_e <= bnd) {
            bnd = t->loop_e;
            what = 1;
        }
        if (!hold && s_switch_at >= 0 && s_switch_at <= bnd) {
            bnd = s_switch_at < s_pos ? s_pos : s_switch_at;
            what = 2;
        }
        if (t->end >= s_pos && t->end <= bnd) {
            bnd = t->end;
            what = 3;
            if (hold && t->loop_e >= s_pos && t->loop_e <= t->end) {
                what = 1, bnd = t->loop_e;
            }
        }
        play_span(s_cur, s_pos, bnd, done, bps);
        uint32_t used = (uint32_t)((bnd - s_pos) / bps + 0.5);
        done = done + used > AUDIO_BLOCK ? AUDIO_BLOCK : done + used;
        s_pos = bnd;
        if (what == 0) {
            break;
        }
        if (what == 1) {
            seek(s_cur, t->loop_s);
            s_passes++;
            if (!t->seamless) {
                crash(s_cur, done);
            }
            continue;
        }
        if (what == 3 && hold) {
            // HOLD after the loop end (the track started after it): from the loop start
            seek(s_cur, t->loop_s);
            s_passes++;
            if (!t->seamless) {
                crash(s_cur, done);
            }
            continue;
        }
        int k = take_ready(what == 2 ? s_phase : -1);
        if (k >= 0) {
            switch_to(k, done, SEGUE_FADE_MS, true);
        } else {
            // no next track yet: this one goes on (the loop, else from its full part)
            s_switch_at = -1;
            if (what == 3) {
                seek(s_cur, t->seamless ? t->loop_s : t->entry);
                s_passes++;
                crash(s_cur, done);
            }
        }
    }
    s_clock += AUDIO_BLOCK;
}

#ifdef MIDI_BG_DEBUG
// tests: the plan of the track that plays now: its 4-bar phrases with notes, and for each
// channel the phrases where the channel is quiet (the voice limit)
void midi_bg_debug_plan(int *phrases, int quiet[16])
{
    memset(quiet, 0, 16 * sizeof(int));
    *phrases = 0;
    if (s_cur < 0) {
        return;
    }
    const slot_t *sl = &s_slot[s_cur];
    double last = sl->m.n ? sl->m.notes[sl->m.n - 1].on / sl->tpb : 0;
    int n = (int)((last - sl->origin) / (PHRASE_BARS * sl->bar_beats)) + 1;
    if (n > MAX_PHRASES) {
        n = MAX_PHRASES;
    }
    *phrases = n;
    for (int p = 0; p < n; p++) {
        for (int c = 0; c < 16; c++) {
            quiet[c] += sl->mute[p] >> c & 1;
        }
    }
}

// tests: the estimated voices of each slot now
void midi_bg_debug_voices(int n[2])
{
    n[0] = n[1] = 0;
    for (int i = 0; i < N_VEND; i++) {
        if ((int32_t)(s_vend[i] - s_clock) > 0) {
            n[s_vslot[i] & 1]++;
        }
    }
}
#endif
