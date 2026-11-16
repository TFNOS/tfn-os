#pragma once
#include "midi_bg/midi_bg.h"
// INK KARTS: the music (kt_music.c, made by apps/kart/tools/music/kt_music.py; do not edit).
// Start: midi_bg_init(KT_TRACKS, KT_TRACK_COUNT, KT_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - KTM_TITLE: the title screen and the results. midi_bg_play_track(KTM_TITLE, 0, true) plays it in a seamless
//   loop from bar 1 (4 light bars of the groove, then the theme).
// - KTM_RACE: the race, from GO. midi_bg_play_track(KTM_RACE, 0, true) plays it in a seamless loop from bar 1
//   (4 bars of the drive with no melody, then the hook).
// - The finish: midi_bg_stop(400); the MP3 jingle of the app plays (win or lose), then the title loop.
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for its sound effects and the motor drone.
enum {
    KTM_TITLE,       // INK KARTS, 150 BPM, 38 s
    KTM_RACE,        // INK RUSH, 168 BPM, 69 s
    KTM_COUNT
};
#define KT_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define KT_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: KTM_TITLE
#define KT_PHASE_RACE       1        // KTM_RACE
extern const bg_track_t KT_TRACKS[];
extern const int KT_TRACK_COUNT;
