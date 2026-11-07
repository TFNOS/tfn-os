// Music engine for PADS. It runs in the audio task (tick callback before each
// 5 ms block) and starts voices with sample-exact delays, so the timing does not
// depend on the frame rate of the screen.
//
// Chords: 8 chords of a major key (I ii iii IV V vi, and bVII and iv from the
// parallel minor), so no chord is ever "wrong". The colour (from tilt) changes
// the chord type: sus2, sus4, triad, seventh, ninth.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "audio.h"
#include "music.h"

music_t g_music = {
        .bpm = 110,
        .prog = {0,  -1, -1, -1, 4,  -1, -1, -1, 5,  -1, -1, -1, 3,  -1, -1, -1,  // I V vi IV
                 -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        .live = -1,
        .bright = 140,
        .range = 128,
        .gate = 200,
        .mix = 128,
        .bar = 0,
        .chord = -1,
};

const char *const MUSIC_ROMAN[MUSIC_CHORDS] = {"I", "ii", "iii", "IV", "V", "vi", "bVII", "iv"};
const char *const MUSIC_KEYS[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const char *const MUSIC_ARP_NAMES[ARP_MODES] = {"UP", "DOWN", "UPDN", "RAND", "STRUM"};
const char *const MUSIC_BEAT_NAMES[BEAT_MODES] = {"OFF", "POP", "DISCO", "HIPHOP", "BOSSA"};

// chord root (semitones above the key) and minor flag
static const int8_t ROOT[MUSIC_CHORDS] = {0, 2, 4, 5, 7, 9, 10, 5};
static const bool MINOR[MUSIC_CHORDS] = {false, true, true, false, false, true, false, true};
// dominant seventh (b7) on V and bVII, major seventh on I and IV
static const bool DOM7[MUSIC_CHORDS] = {false, false, false, false, true, false, true, false};

// drum patterns: bit n = 16th note n
typedef struct {
    uint16_t kick, snare, hat, open, rim;
} beat_t;
static const beat_t BEATS[BEAT_MODES] = {
        [BEAT_OFF] = {0, 0, 0, 0, 0},
        [BEAT_POP] = {0x0101 | 0x0400, 0x1010, 0x5555, 0, 0},
        [BEAT_DISCO] = {0x1111, 0x1010, 0, 0x4444, 0},
        [BEAT_HIPHOP] = {0x0481, 0x1010, 0x5555, 0x0000, 0x2000},
        [BEAT_BOSSA] = {0x0909, 0, 0x5555, 0, 0x2448},
};

#define G_PAD  1
#define G_BASS 2

static uint32_t s_clock;      // samples since music_start
static uint32_t s_next_step;  // sample time of the next 16th
static int s_step = -1, s_bar;
static int s_pad_chord = -1, s_pad_color = 99;
static int s_arp_i;
static bool s_fill_bar, s_crash_next;
static bool s_prev_playing;
static int s_count;      // 16ths of the count-in still to come
static int s_count_all;  // 16ths of the whole count-in

// export
static volatile int s_exp_state = MEXP_OFF;
static int16_t *s_exp_buf;
static uint32_t s_exp_n, s_exp_t0;
static midi_t *s_exp_midi;
static uint32_t s_exp_bpm;
enum {
    CH_PAD = 0,
    CH_ARP = 1,
    CH_BASS = 2,
    CH_DRUMS = 9
};
enum {
    GM_KICK = 36,
    GM_RIM = 37,
    GM_SNARE = 38,
    GM_HAT = 42,
    GM_OPEN = 46,
    GM_CRASH = 49
};

static float midi_hz(int m)
{
    return 440.0f * powf(2.0f, (m - 69) / 12.0f);
}

// Chord tones as semitones above the chord root. Returns the count.
static int chord_tones(int chord, int color, int8_t *out)
{
    int n = 0;
    out[n++] = 0;
    switch (color) {
    case -2:
        out[n++] = 2;
        out[n++] = 7;
        break;  // sus2
    case -1:
        out[n++] = 5;
        out[n++] = 7;
        break;  // sus4
    default:
        out[n++] = MINOR[chord] ? 3 : 4;
        out[n++] = 7;
        if (color >= 1) {
            out[n++] = (MINOR[chord] || DOM7[chord]) ? 10 : 11;
        }
        if (color >= 2) {
            out[n++] = 14;
        }
        break;
    }
    return n;
}

void music_chord_name(int chord, int color, char *buf, int len)
{
    if (chord < 0) {
        snprintf(buf, len, "-");
        return;
    }
    const char *root = MUSIC_KEYS[(g_music.key + ROOT[chord]) % 12];
    static const char *const MAJ[5] = {"SUS2", "SUS4", "", "MAJ7", "MAJ9"};
    static const char *const DOM[5] = {"SUS2", "SUS4", "", "7", "9"};
    static const char *const MIN[5] = {"SUS2", "SUS4", "m", "m7", "m9"};
    int c = color < -2 ? 0 : color > 2 ? 4 : color + 2;
    const char *suffix = MINOR[chord] ? MIN[c] : (DOM7[chord] ? DOM[c] : MAJ[c]);
    snprintf(buf, len, "%s%s", root, suffix);
}

// A note for the MIDI export, if the export records now. d and dur are in samples.
static void mlog(int ch, int note, uint32_t d, uint32_t dur, int vel)
{
    if (s_exp_state != MEXP_REC || !s_exp_midi) {
        return;
    }
    uint32_t t = s_clock + d - s_exp_t0;
    if (t >= s_exp_n) {
        return;
    }
    uint32_t e = t + dur < s_exp_n ? t + dur : s_exp_n;
    // 96 ticks for each quarter note
    uint32_t k0 = (uint32_t)((uint64_t)t * s_exp_bpm * 96 / (60u * AUDIO_SAMPLE_RATE));
    uint32_t k1 = (uint32_t)((uint64_t)e * s_exp_bpm * 96 / (60u * AUDIO_SAMPLE_RATE));
    midi_note(s_exp_midi, k0, k1, ch, note, vel);
}

static void voice(uint32_t delay, uint32_t dur, float f0, float f1, uint8_t wave, uint8_t vol, uint16_t attack,
                  uint8_t shape, uint8_t lp, uint8_t group)
{
    audio_voice_t v = {
            .delay = delay,
            .dur = dur,
            .f0 = f0,
            .f1 = f1,
            .wave = wave,
            .vol = vol,
            .attack_ms = attack,
            .shape = shape,
            .lp = lp,
            .group = group,
    };
    audio_voice(&v);
}

static void drums(uint32_t d, int step, uint32_t step_len)
{
    const beat_t *b = &BEATS[g_music.beat];
    uint16_t bit = (uint16_t)(1u << step);
    bool fill = s_fill_bar && step >= 8;
    if (b->kick & bit) {
        voice(d, AUDIO_SAMPLE_RATE / 7, 150, 42, W_TRI, 95, 1, AUDIO_PERC, 0, 0);
        voice(d, AUDIO_SAMPLE_RATE / 200, 3000, 3000, W_NOISE, 25, 1, AUDIO_PERC, 0, 0);
        mlog(CH_DRUMS, GM_KICK, d, step_len, 110);
        g_music.beats++;
    }
    if ((b->snare & bit) || fill) {
        uint8_t vol = fill ? (uint8_t)(18 + (step - 8) * 3) : 34;
        voice(d, AUDIO_SAMPLE_RATE / 7, 5000, 2500, W_NOISE, vol, 1, AUDIO_PERC, 200, 0);
        voice(d, AUDIO_SAMPLE_RATE / 12, 200, 150, W_TRI, fill ? 30 : 45, 1, AUDIO_PERC, 0, 0);
        mlog(CH_DRUMS, GM_SNARE, d, step_len, fill ? 50 + (step - 8) * 8 : 100);
    }
    if (b->hat & bit) {
        voice(d, AUDIO_SAMPLE_RATE / 30, 9000, 9000, W_NOISE, (step & 2) ? 12 : 17, 1, AUDIO_PERC, 0, 0);
        mlog(CH_DRUMS, GM_HAT, d, step_len, (step & 2) ? 60 : 85);
    }
    if (b->open & bit) {
        voice(d, AUDIO_SAMPLE_RATE / 6, 8000, 7000, W_NOISE, 14, 1, AUDIO_PERC, 0, 0);
        mlog(CH_DRUMS, GM_OPEN, d, step_len, 80);
    }
    if (b->rim & bit) {
        voice(d, AUDIO_SAMPLE_RATE / 50, 1700, 1700, W_PULSE, 16, 1, AUDIO_PERC, 0, 0);
        mlog(CH_DRUMS, GM_RIM, d, step_len, 80);
    }
}

static void bass(uint32_t d, int step, int root_midi, uint32_t step_len)
{
    const beat_t *b = &BEATS[g_music.beat];
    bool hit;
    if (g_music.beat == BEAT_OFF) {
        hit = step == 0 || step == 8;
    } else {
        hit = (b->kick & (1u << step)) || step == 0;
    }
    if (g_music.beat == BEAT_DISCO && (step & 3) == 2) {
        // octave bass on the off-beats
        voice(d, step_len, midi_hz(root_midi + 12), midi_hz(root_midi + 12), W_SAW, 30, 3, AUDIO_PERC, 120, G_BASS);
        mlog(CH_BASS, root_midi + 12, d, step_len, 90);
        return;
    }
    if (hit) {
        voice(d, step_len * 3, midi_hz(root_midi), midi_hz(root_midi), W_SAW, 38, 4, AUDIO_PERC, 110, G_BASS);
        mlog(CH_BASS, root_midi, d, step_len * 2, 100);
    }
}

static void arp(uint32_t d, int step, int chord, int color, int root_midi, uint32_t step_len)
{
    int8_t t[6];
    int n = chord_tones(chord, color, t);
    int bright = g_music.bright, range = g_music.range;
    int octaves = 1 + range * 3 / 256;  // 1..3
    bool eighths = range < 50;
    if (eighths && (step & 1)) {
        return;
    }
    uint8_t lp = (uint8_t)(90 + bright * 165 / 255);
    // MIX: the arpeggio is full from the middle up, and fades to the pad end
    int mix = g_music.mix;
    int avol = mix >= 128 ? 20 : 20 * mix / 128;
    if (avol < 1) {
        return;
    }
    // GATE: the note length, 15 % .. 100 % of its step (per 1000)
    uint32_t gate_k = 150 + (uint32_t)g_music.gate * 850 / 255;
    int total = n * octaves;
    int base = root_midi + 12;

    if (g_music.arp_mode == ARP_STRUM) {
        // the whole chord on each beat, a little spread in time
        if ((step & 3) == 0) {
            for (int i = 0; i < n; i++) {
                int m = base + t[i];
                voice(d + i * (AUDIO_SAMPLE_RATE / 90), step_len * 3 * gate_k / 1000 + 1, midi_hz(m), midi_hz(m),
                      W_PULSE, (uint8_t)(avol * 14 / 20 + 1), 3, AUDIO_PERC, lp, 0);
                mlog(CH_ARP, m, d + i * (AUDIO_SAMPLE_RATE / 90), step_len * 2, 80);
            }
            g_music.notes++;
            g_music.last_note = (uint8_t)(base + t[0]);
        }
        return;
    }

    int idx;
    switch (g_music.arp_mode) {
    case ARP_DOWN:
        idx = total - 1 - (s_arp_i % total);
        s_arp_i++;
        break;
    case ARP_UPDOWN:
        if (total < 2) {
            idx = 0;
        } else {
            int period = 2 * total - 2;
            int p = s_arp_i % period;
            idx = p < total ? p : period - p;
        }
        s_arp_i++;
        break;
    case ARP_RANDOM:
        idx = (int)(esp_random() % (uint32_t)total);
        break;
    default:
        idx = s_arp_i % total;
        s_arp_i++;
        break;
    }
    int m = base + t[idx % n] + 12 * (idx / n);
    uint32_t dur = eighths ? step_len * 2 : step_len;
    voice(d, dur * gate_k / 1000 + 1, midi_hz(m), midi_hz(m), W_PULSE, (uint8_t)avol, 3, AUDIO_PERC, lp, 0);
    mlog(CH_ARP, m, d, dur * 3 / 4, 70 + bright / 5);
    g_music.notes++;
    g_music.last_note = (uint8_t)m;
}

static void pad(uint32_t d, int chord, int color, int root_midi, uint32_t bar_len)
{
    audio_release_group(G_PAD, 90);
    if (chord < 0) {
        return;
    }
    int8_t t[6];
    int n = chord_tones(chord, color, t);
    uint8_t lp = (uint8_t)(60 + g_music.bright * 70 / 255);
    // MIX: the pad is full from the middle down, and fades to the arpeggio end
    int mix = g_music.mix;
    int pvol = mix <= 128 ? 9 : 9 * (255 - mix) / 127;
    if (pvol < 1) {
        return;
    }
    for (int i = 0; i < n; i++) {
        int m = root_midi + t[i];
        // two saws a little apart: a wide, soft pad
        voice(d, bar_len, midi_hz(m) * 1.003f, midi_hz(m) * 1.003f, W_SAW, (uint8_t)pvol, 160, AUDIO_SUSTAIN, lp,
              G_PAD);
        voice(d, bar_len, midi_hz(m) * 0.997f, midi_hz(m) * 0.997f, W_SAW, (uint8_t)pvol, 160, AUDIO_SUSTAIN, lp,
              G_PAD);
        mlog(CH_PAD, m, d, bar_len, 70);
    }
}

// bars in the loop: up to the last bar that has a chord
static int loop_len(void)
{
    int fixed = g_music.loop_bars;
    if (fixed >= 1 && fixed <= MUSIC_SLOTS) {
        return fixed;
    }
    int n = 1;
    for (int i = 0; i < MUSIC_CELLS; i++) {
        if (g_music.prog[i] >= 0) {
            n = i / MUSIC_BEATS + 1;
        }
    }
    return n;
}

int music_loop_bars(void)
{
    return loop_len();
}

static uint32_t step_samples(void)
{
    uint32_t bpm = g_music.bpm < 60 ? 60 : g_music.bpm > 180 ? 180 : g_music.bpm;
    return AUDIO_SAMPLE_RATE * 60 / (bpm * 4);
}

uint32_t music_loop_samples(void)
{
    return step_samples() * 16 * (uint32_t)loop_len();
}

// the chord for this beat: a held pad first (with the colour now), else the progression
// (an empty cell keeps the chord before it; a cell with its own colour uses that colour)
static int current_chord(int *color)
{
    *color = g_music.color;
    if (g_music.live >= 0) {
        return g_music.live;
    }
    if (!g_music.playing) {
        return -1;
    }
    int cell = s_bar * MUSIC_BEATS + s_step / 4;
    int n = loop_len() * MUSIC_BEATS;
    for (int k = 0; k < n; k++) {
        int i = (cell - k + n) % n;
        if (g_music.prog[i] >= 0) {
            int c = g_music.pcolor[i];
            if (c >= -2 && c <= 2) {
                *color = c;
            }
            return g_music.prog[i];
        }
    }
    return -1;
}

static void do_step(uint32_t d, uint32_t step_len)
{
    bool playing = g_music.playing;
    if (playing && !s_prev_playing) {
        s_step = -1;  // start at bar 1, beat 1
        s_bar = 0;
        s_arp_i = 0;
        s_count = s_count_all = (g_music.count_in > 8 ? 8 : g_music.count_in) * 4;
        g_music.count_in = 0;
    }
    s_prev_playing = playing;
    if (!playing) {
        s_count = 0;
    }
    if (s_count > 0) {
        // the count-in: a click on each beat (a high one on the first), then bar 1
        if (s_count % 4 == 0) {
            float f = s_count == s_count_all ? 1760.0f : 1320.0f;
            voice(d, AUDIO_SAMPLE_RATE / 25, f, f, W_PULSE, 22, 1, AUDIO_PERC, 0, 0);
        }
        g_music.counting = (int8_t)((s_count + 3) / 4);
        s_count--;
        g_music.bar = -1;
        g_music.step = -1;
        return;
    }
    g_music.counting = 0;

    s_step++;
    if (s_step >= 16) {
        s_step = 0;
        if (playing) {
            s_bar = (s_bar + 1) % loop_len();
        }
        s_fill_bar = false;
    }
    if (g_music.fill) {
        g_music.fill = false;
        s_fill_bar = true;  // snare roll in the second half of this bar
        s_crash_next = true;
    }
    g_music.step = (int8_t)s_step;
    g_music.bar = (int8_t)s_bar;

    // export: start on beat 1 of bar 1, stop after one loop
    if (s_exp_state == MEXP_REC && s_clock + d - s_exp_t0 >= s_exp_n) {
        s_exp_state = MEXP_DONE;
    }
    if (s_exp_state == MEXP_WAIT && playing && s_bar == 0 && s_step == 0) {
        s_exp_t0 = s_clock + d;
        s_exp_bpm = g_music.bpm;
        audio_capture(s_exp_buf, s_exp_n, d);
        s_exp_state = MEXP_REC;
    }

    int color;
    int chord = current_chord(&color);
    g_music.chord = (int8_t)chord;
    g_music.chord_color = (int8_t)color;
    int root_midi = 48 + (g_music.key + (chord >= 0 ? ROOT[chord] : 0)) % 12;  // C3..B3

    // the pad starts again on each bar, and at once on a new chord or colour
    if (chord != s_pad_chord || color != s_pad_color || s_step == 0) {
        if (chord != s_pad_chord) {
            s_arp_i = 0;
        }
        pad(d, chord, color, root_midi, step_len * 16);
        s_pad_chord = chord;
        s_pad_color = color;
    }
    if (s_step == 0 && s_crash_next && !s_fill_bar) {
        s_crash_next = false;
        voice(d, AUDIO_SAMPLE_RATE, 7000, 3500, W_NOISE, 28, 2, AUDIO_PERC, 0, 0);
        mlog(CH_DRUMS, GM_CRASH, d, step_len * 4, 100);
    }
    if (playing) {
        drums(d, s_step, step_len);
        if (chord >= 0) {
            bass(d, s_step, root_midi - 12, step_len);
        }
    }
    if (chord >= 0) {
        arp(d, s_step, chord, color, root_midi, step_len);
    }
}

static void tick(void)
{
    uint32_t step_len = step_samples();
    uint32_t block_end = s_clock + AUDIO_BLOCK;
    while ((int32_t)(s_next_step - block_end) < 0) {
        uint32_t d = (int32_t)(s_next_step - s_clock) > 0 ? s_next_step - s_clock : 0;
        do_step(d, step_len);
        s_next_step += step_len;
    }
    s_clock = block_end;
}

void music_start(void)
{
    s_clock = 0;
    s_next_step = 0;
    s_step = -1;
    s_bar = 0;
    s_pad_chord = -1;
    s_pad_color = 99;
    s_prev_playing = false;
    audio_set_tick(tick);
}

// one last tick in the audio task: fade out the pad and the bass, then stop
static void stop_tick(void)
{
    audio_release_group(G_PAD, 250);
    audio_release_group(G_BASS, 100);
    audio_set_tick(NULL);
}

void music_export(int16_t *buf, uint32_t n, midi_t *midi)
{
    s_exp_buf = buf;
    s_exp_n = n;
    s_exp_midi = midi;
    if (midi) {
        midi_program(midi, CH_PAD, 89);   // warm pad
        midi_program(midi, CH_ARP, 80);   // square lead
        midi_program(midi, CH_BASS, 38);  // synth bass
    }
    s_exp_state = MEXP_WAIT;
}

void music_export_cancel(void)
{
    s_exp_state = MEXP_OFF;
    audio_capture_stop();
}

int music_export_state(void)
{
    return s_exp_state;
}

void music_stop(void)
{
    if (s_exp_state != MEXP_DONE) {
        music_export_cancel();
    }
    g_music.playing = false;
    g_music.live = -1;
    g_music.chord = -1;
    audio_set_tick(stop_tick);
}
