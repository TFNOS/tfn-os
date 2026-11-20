#pragma once
#include "midi_bg/midi_bg.h"
// CUBBY: the music (cb_music.c, made by apps/cubby/tools/music/cb_music.py; do not edit).
// Start: midi_bg_init(CB_TRACKS, CB_TRACK_COUNT, CB_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - CBM_TITLE: the title screen and the menus. midi_bg_play_track(CBM_TITLE, 0, true) plays it in a seamless
//   loop from bar 1 (4 light bars of the groove, then the theme of CUBBY).
// - CBM_ISLAND: the play on the island. midi_bg_play_track(CBM_ISLAND, 0, true) at the start of the play
//   plays it in a seamless loop from bar 1 (4 bars of the groove with no melody, then the theme). To start at
//   the theme at once: midi_bg_play_track(CBM_ISLAND, 16.0f, true) (the entry, beat 16).
// - CUBBY gets a SUNDROP (the first and the second): midi_bg_play_track(CBM_J_SUNDROP, 0, false);
//   CB_J_SUNDROP_MS later, the island loop again. All three SUNDROPS: CBM_J_WIN in place of CBM_J_SUNDROP;
//   CB_J_WIN_MS later, midi_bg_stop(300) or the title.
// - CUBBY falls into the sea: midi_bg_play_track(CBM_J_SPLASH, 0, false); CB_J_SPLASH_MS later, the island
//   loop again.
// - The island loop again after a jingle: from the start of the 4-bar phrase where it stopped. Before the
//   jingle, keep the beat of midi_bg_state(); after it, midi_bg_play_track(CBM_ISLAND, 16.0f * floorf(beat /
//   16.0f), true). A phrase is 16 beats, and each section of the island starts on a phrase line.
// - The jingles play once: midi_bg_play_track(CBM_J_*, 0, false). At its end the player starts a jingle
//   again (with a crash), so the game starts the next track (or calls midi_bg_stop) CB_J_*_MS after the
//   start of the jingle, or a frame before. The last notes of each jingle end before CB_J_*_MS.
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for its sound effects and the motor drone.
enum {
    CBM_TITLE,       // CUBBY, 156 BPM, 49 s
    CBM_ISLAND,      // SUNDROP ISLE, 160 BPM, 90 s
    CBM_J_SUNDROP,   // SUNDROP, 160 BPM, 3.0 s (play once)
    CBM_J_SPLASH,    // SPLASH, 160 BPM, 1.5 s (play once)
    CBM_J_WIN,       // WIN, 160 BPM, 4.5 s (play once)
    CBM_COUNT
};
#define CB_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define CB_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: CBM_TITLE
#define CB_PHASE_ISLAND     1        // CBM_ISLAND
#define CB_ISLAND_ENTRY     16.0f    // the beat where the theme of CBM_ISLAND starts (after the 4-bar groove)
#define CB_J_SUNDROP_MS     3000     // the length of each jingle (ms): then start the next track
#define CB_J_SPLASH_MS      1500
#define CB_J_WIN_MS         4500
extern const bg_track_t CB_TRACKS[];
extern const int CB_TRACK_COUNT;
