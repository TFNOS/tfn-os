// The synth instruments of STUDIO and the General MIDI maps (see instr.h). STUDIO and MIDI_BG
// use the same table, so a MIDI file sounds the same in the two.

#include <stddef.h>
#include "tfn/audio.h"
#include "drumkit.h"
#include "instr.h"

typedef struct {
    uint8_t wave;
    float ratio;  // frequency ratio to the note (detune or octave)
    uint8_t vol;
} ilayer_t;

typedef struct {
    ilayer_t l[3];
    uint8_t nl;
    uint16_t attack;         // ms
    uint8_t shape;           // AUDIO_PERC or AUDIO_SUSTAIN
    uint16_t decay;          // PERC: fixed length in ms (0 = the note length)
    uint8_t lp_min, lp_max;  // filter range for tone 0..100 (lp_max 0 = no filter)
    uint8_t hp;
    float bend;  // start pitch ratio, glides to the note (1 = none)
    uint8_t gm;  // General MIDI program for the export
} instr_t;

// the order is the order of ST_INSTR (studio.c): SAW LEAD, SQUARE, SOFT, CHIP, PLUCK, BELL,
// KALIMBA, ORGAN, E-PIANO, STRINGS, WARM PAD, BRASS, SUB BASS, ACID, FUNK, 808 BASS
static const instr_t INSTR[INSTR_COUNT] = {
        {{{W_SAW, 1.0f, 22}, {W_SAW, 1.007f, 22}, {W_SAW, 0.5f, 10}}, 3, 8, AUDIO_SUSTAIN, 0, 120, 255, 0, 1.0f, 81},
        {{{W_SQUARE, 1.0f, 20}, {W_PULSE, 2.0f, 8}}, 2, 5, AUDIO_SUSTAIN, 0, 110, 255, 0, 1.0f, 80},
        {{{W_SINE, 1.0f, 45}, {W_TRI, 2.0f, 12}}, 2, 25, AUDIO_SUSTAIN, 0, 0, 0, 0, 1.0f, 79},
        {{{W_PULSE, 1.0f, 22}}, 1, 2, AUDIO_SUSTAIN, 0, 0, 0, 0, 1.0f, 80},
        {{{W_SAW, 1.0f, 30}, {W_PULSE, 1.003f, 16}}, 2, 2, AUDIO_PERC, 300, 90, 230, 0, 1.0f, 45},
        {{{W_SINE, 1.0f, 45}, {W_SINE, 2.76f, 15}, {W_SINE, 5.4f, 6}}, 3, 2, AUDIO_PERC, 900, 0, 0, 0, 1.0f, 14},
        {{{W_SINE, 1.0f, 50}, {W_TRI, 3.0f, 10}}, 2, 2, AUDIO_PERC, 450, 0, 0, 0, 1.0f, 108},
        {{{W_SINE, 1.0f, 30}, {W_SINE, 2.0f, 20}, {W_SINE, 3.0f, 12}}, 3, 8, AUDIO_SUSTAIN, 0, 0, 0, 0, 1.0f, 16},
        {{{W_SINE, 1.0f, 45}, {W_TRI, 2.0f, 14}}, 2, 3, AUDIO_PERC, 1100, 0, 0, 0, 1.0f, 4},
        {{{W_SAW, 1.0f, 14}, {W_SAW, 1.006f, 14}}, 2, 180, AUDIO_SUSTAIN, 0, 70, 170, 0, 1.0f, 48},
        {{{W_TRI, 1.0f, 30}, {W_SAW, 0.995f, 10}, {W_SAW, 2.004f, 5}}, 3, 350, AUDIO_SUSTAIN, 0, 60, 140, 0, 1.0f, 89},
        {{{W_SAW, 1.0f, 22}, {W_SAW, 1.004f, 18}}, 2, 45, AUDIO_SUSTAIN, 0, 100, 220, 0, 0.985f, 61},
        {{{W_SINE, 1.0f, 70}, {W_TRI, 2.0f, 10}}, 2, 5, AUDIO_SUSTAIN, 0, 0, 0, 0, 1.0f, 38},
        {{{W_SAW, 1.0f, 30}, {W_SQUARE, 0.5f, 10}}, 2, 2, AUDIO_PERC, 0, 60, 200, 0, 1.0f, 87},
        {{{W_PULSE, 1.0f, 30}, {W_TRI, 1.0f, 30}}, 2, 2, AUDIO_PERC, 350, 90, 200, 0, 1.0f, 36},
        {{{W_SINE, 1.0f, 80}}, 1, 3, AUDIO_PERC, 700, 0, 0, 0, 1.0f, 38},
};

int instr_from_gm(int prog, float mean)
{
    if (prog < 0) {
        prog = 0;
    }
    prog &= 127;
    if (prog < 8) {
        return mean < 48 ? 12 : 8;  // piano: E-PIANO (a low part: SUB BASS)
    }
    // the pitched mallets (celesta, glockenspiel, music box, vibraphone ... dulcimer): KALIMBA. Not
    // BELL: its bar partials (x2.76, x5.4) are 42 and 20 cents off a semitone, and in a melody
    // they sound as off-key notes (the music box of sheni, the celesta of eever).
    if (prog < 16) {
        return 6;
    }
    if (prog < 24) {
        return 7;  // organs
    }
    if (prog < 32) {
        return 4;  // guitars: PLUCK
    }
    if (prog < 40) {
        return prog >= 38 ? 13 : (prog == 36 || prog == 37 ? 14 : 12);  // bass
    }
    if (prog == 45 || prog == 46) {
        return 4;  // pizzicato and the harp are plucked: PLUCK
    }
    if (prog < 56) {
        return 9;  // strings, ensembles, choirs
    }
    if (prog < 64) {
        return 11;  // brass
    }
    if (prog < 80) {
        return 2;  // reeds and pipes: SOFT
    }
    if (prog < 88) {
        return prog == 80 ? 1 : 0;  // synth leads: SQUARE, SAW LEAD
    }
    if (prog < 104) {
        return 10;  // pads and effects: WARM PAD
    }
    if (prog < 112) {
        return 6;  // ethnic: KALIMBA
    }
    if (prog < 120) {
        return 5;  // percussive: BELL
    }
    return 3;  // sound effects: CHIP
}

int instr_gm_drum(int note)
{
    switch (note) {
    case 35:
    case 36:
        return DK_KICK;
    case 37:
        return DK_RIM;
    case 38:
    case 40:
        return DK_SNARE;
    case 39:
        return DK_CLAP;
    case 42:
    case 44:
        return DK_CHAT;
    case 46:
        return DK_OHAT;
    case 41:
    case 43:
    case 45:
    case 47:
        return DK_TOM_LO;
    case 48:
    case 50:
        return DK_TOM_HI;
    case 49:
    case 52:
    case 55:
    case 57:
        return DK_CRASH;
    case 51:
    case 53:
    case 59:
        return DK_RIDE;
    case 54:
    case 69:
    case 70:
    case 82:
        return DK_SHAKER;
    case 56:
        return DK_COWBELL;
    case 60:
    case 62:
    case 65:
    case 67:
    case 76:
        return DK_TOM_HI;  // bongos, congas, blocks (high)
    case 61:
    case 63:
    case 64:
    case 66:
    case 68:
    case 77:
        return DK_TOM_LO;
    case 75:
        return DK_RIM;  // claves
    default:
        return DK_RIM;  // GS/XG notes 27..34 (clicks, sticks) are short: not a kick
    }
}

int instr_gm(int instr)
{
    return INSTR[(unsigned)instr % INSTR_COUNT].gm;
}

int instr_layers(int instr)
{
    return INSTR[(unsigned)instr % INSTR_COUNT].nl;
}

bool instr_is_pad(int instr)
{
    unsigned i = (unsigned)instr % INSTR_COUNT;
    return i == 9 || i == 10;  // STRINGS, WARM PAD
}

bool instr_is_bass(int instr)
{
    return (unsigned)instr % INSTR_COUNT >= 12;  // SUB BASS, ACID, FUNK, 808 BASS
}

uint32_t instr_note_len(int instr, uint32_t dur, bool short_perc)
{
    const instr_t *in = &INSTR[(unsigned)instr % INSTR_COUNT];
    if (in->shape != AUDIO_PERC || !in->decay) {
        return dur;
    }
    uint32_t full = (uint32_t)in->decay * AUDIO_SAMPLE_RATE / 1000;
    uint32_t cap = dur + AUDIO_SAMPLE_RATE / 4;
    return short_perc && cap < full ? cap : full;
}

static uint8_t tone_lp(const instr_t *in, int tone)
{
    if (!in->lp_max) {
        return 0;
    }
    int lp = in->lp_min + (in->lp_max - in->lp_min) * tone / 100;
    return (uint8_t)(lp < 1 ? 1 : lp > 255 ? 255 : lp);
}

// the highest layer volume that a note asked for since the last instr_over_take() (a voice takes 100 at most: more =
// a note over 100 %, cut at the voice)
static volatile int s_over;

int instr_over_take(void)
{
    int m = s_over;
    s_over = 0;
    return m;
}

int instr_play(int instr, int midi, uint32_t delay, uint32_t dur, int vol, int tone, int max_layers, uint8_t group,
               bool short_perc, bool from_tick, uint32_t *len_out)
{
    const instr_t *in = &INSTR[(unsigned)instr % INSTR_COUNT];
    float hz = audio_midi_hz((float)midi);
    uint32_t len = instr_note_len(instr, dur, short_perc);
    if (len_out) {
        *len_out = len;
    }
    int nl = in->nl < max_layers ? in->nl : max_layers;
    int started = 0;
    for (int i = 0; i < nl; i++) {
        const ilayer_t *l = &in->l[i];
        float f = hz * l->ratio;
        if (f > 18000) {
            continue;
        }
        if (l->vol * vol / 100 > s_over) {
            s_over = l->vol * vol / 100;
        }
        audio_voice_t v = {
                .delay = delay,
                .dur = len,
                .f0 = f * in->bend,
                .f1 = f,
                .wave = l->wave,
                .vol = (uint8_t)(l->vol * vol / 100 > 255
                                         ? 255
                                         : l->vol * vol / 100),  // (a loud note: no wrap to a quiet one)
                .attack_ms = in->attack,
                .shape = in->shape,
                .lp = tone_lp(in, tone),
                .hp = in->hp,
                .group = group,
        };
        if (!v.vol) {
            continue;
        }
        if (from_tick) {
            audio_voice(&v);
        } else {
            audio_play(&v);
        }
        started++;
    }
    return started;
}
