#pragma once

#include <stdbool.h>
#include <stdint.h>

// Drum sounds made from synth voices (no samples). Each drum uses 1 to 4 voices.

enum {
    DK_KICK,
    DK_SNARE,
    DK_CLAP,
    DK_CHAT,  // closed hi-hat
    DK_OHAT,  // open hi-hat
    DK_TOM_LO,
    DK_TOM_HI,
    DK_CRASH,
    DK_RIDE,
    DK_RIM,
    DK_COWBELL,
    DK_SHAKER,
    DK_COUNT,
};

enum {
    KIT_808,
    KIT_909,
    KIT_ROCK,
    KIT_LOFI,
    KIT_JUNK,
    KIT_COUNT
};

extern const char *const DRUM_NAMES[DK_COUNT];  // short names, 5 letters or less
extern const char *const KIT_NAMES[KIT_COUNT];

// vel 1..127. tune: semitones up or down (0 = normal).
// drum_voice: only from an audio tick callback; delay = samples from the block start.
void drum_voice(int kit, int drum, int vel, int tune, uint32_t delay);
// drum_play: from any task (through the audio queue).
void drum_play(int kit, int drum, int vel, int tune);

// General MIDI note (channel 10) of a drum, for the MIDI export.
int drum_gm_note(int drum);
