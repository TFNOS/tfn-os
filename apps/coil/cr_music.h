#pragma once
#include "midi_bg/midi_bg.h"
// COIL RIDER: the music (cr_music.c, made by apps/coil/tools/mk_music.py; do not edit).
// The MIDI pieces "newcool" (the title), "kentaury" and "fiction" (the ride), the BGN copies of CRUISE.
enum {
    CRM_TITLE,    // newcool
    CRM_RIDE1,    // kentaury
    CRM_RIDE2,    // fiction
    CRM_COUNT
};
static const uint8_t CR_VOL[CRM_COUNT] = {66, 44, 44};    // midi_bg_volume of each piece (no clip)
#define CR_MUSIC_VOICES 16
extern const bg_track_t CR_TRACKS[];
extern const int CR_TRACK_COUNT;
