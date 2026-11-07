#pragma once

#include <stdbool.h>
#include <stdint.h>

// The synth instruments of STUDIO (16 instruments of 1 to 3 voice layers) and the General MIDI
// maps, for all users: STUDIO (its tracks and its MIDI import) and MIDI_BG (the background music
// of CRUISE). The names of the instruments are ST_INSTR in studio.h.

#define INSTR_COUNT 16

// A STUDIO instrument for a General MIDI program, from the program and the mean pitch of the
// channel (a low piano part becomes SUB BASS).
int instr_from_gm(int prog, float mean_pitch);
// A drum of drumkit.h (DK_*) for a General MIDI drum note (channel 10).
int instr_gm_drum(int note);
// The General MIDI program of an instrument (for the MIDI export).
int instr_gm(int instr);
// Information on an instrument.
int instr_layers(int instr);
bool instr_is_pad(int instr);  // a slow pad or strings: the first to lose layers
bool instr_is_bass(int instr);

// The length in samples of the voices of a note of dur samples. A percussive instrument has a
// fixed decay; with short_perc it decays in the note length + 250 ms (if that is shorter).
uint32_t instr_note_len(int instr, uint32_t dur, bool short_perc);
// Play a note of an instrument. delay and dur in samples; vol 0..100; tone 0..100 (dark ..
// bright); max_layers 1..3; group: the voice group (audio_release_group).
// short_perc: a percussive instrument decays in its note length + 250 ms, not in its full decay
// (a fast part then does not hold many voices). from_tick: true in an audio tick callback
// (audio_voice), false from another task (audio_play).
// Returns the voices that it started, and *len (if not NULL) gets their length in samples.
// the highest layer volume of a note since the last call (percent; over 100 = a note was cut at the voice: tests)
int instr_over_take(void);
int instr_play(int instr, int midi, uint32_t delay, uint32_t dur, int vol, int tone, int max_layers, uint8_t group,
               bool short_perc, bool from_tick, uint32_t *len);
