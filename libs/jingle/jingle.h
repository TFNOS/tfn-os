// libs/jingle: the note names and the macros of main/tunes.c, for a game that has its own opening tune in its own file
// (each game has its own opening, not the start tune of the firmware). A tune is an array
// of tune_note_t (audio.h); audio_tune(notes, n) plays it (it is in the ABI of the app files). Header only.
#pragma once
#include "tfn/audio.h"

// the note names (MIDI numbers, 60 = C4)
enum {
    J_C2 = 36,
    J_D2 = 38,
    J_Eb2 = 39,
    J_E2 = 40,
    J_F2 = 41,
    J_G2 = 43,
    J_A2 = 45,
    J_Bb2 = 46,
    J_B2 = 47,
    J_C3 = 48,
    J_Db3 = 49,
    J_D3 = 50,
    J_Eb3 = 51,
    J_E3 = 52,
    J_F3 = 53,
    J_Gb3 = 54,
    J_G3 = 55,
    J_Ab3 = 56,
    J_A3 = 57,
    J_Bb3 = 58,
    J_B3 = 59,
    J_C4 = 60,
    J_Db4 = 61,
    J_D4 = 62,
    J_Eb4 = 63,
    J_E4 = 64,
    J_F4 = 65,
    J_Gb4 = 66,
    J_G4 = 67,
    J_Ab4 = 68,
    J_A4 = 69,
    J_Bb4 = 70,
    J_B4 = 71,
    J_C5 = 72,
    J_Db5 = 73,
    J_D5 = 74,
    J_Eb5 = 75,
    J_E5 = 76,
    J_F5 = 77,
    J_Gb5 = 78,
    J_G5 = 79,
    J_Ab5 = 80,
    J_A5 = 81,
    J_Bb5 = 82,
    J_B5 = 83,
    J_C6 = 84,
    J_D6 = 86,
    J_E6 = 88,
    J_F6 = 89,
    J_G6 = 91,
    J_A6 = 93,
};

// a note: the time (ms), the length (ms), the note, the wave, the volume
#define JN(t, d, n, w, v) {(t), (d), (n), 0, (w), (v)}
// a note whose pitch slides b semitones
#define JSL(t, d, n, b, w, v) {(t), (d), (n), (b), (w), (v)}
// a note that holds and fades at its end (not a decay)
#define JHOLD(w) ((w) | TUNE_HOLD)
// the drums
#define JKICK(t)    JSL(t, 150, J_B2, -24, W_SINE, 70)
#define JSNARE(t)   JSL(t, 130, 100, -8, W_NOISE, 20), JSL(t, 80, J_G3, -7, W_TRI, 40)
#define JHAT(t)     JN(t, 30, 122, W_NOISE, 9)
#define JCRASH(t)   JN(t, 600, 112, W_NOISE, 14)
#define JTOM(t, n)  JSL(t, 160, n, -12, W_SINE, 60)
#define JPLAY(tune) audio_tune((tune), (int)(sizeof(tune) / sizeof((tune)[0])))
