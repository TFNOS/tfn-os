#pragma once
#include "midi_bg/midi_bg.h"
// DEEP CURRENT: the music (dc_music.c, made by apps/deep_current/tools/music/dc_music.py; do not edit).
// Start: midi_bg_init(DC_TRACKS, DC_TRACK_COUNT, DC_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - DCM_TITLE: the title screen and the menus. midi_bg_play_track(DCM_TITLE, 0, true) plays it in a seamless
//   loop from bar 1 (4 soft bars with no drums, then the small theme).
// - DCM_Z1 .. DCM_Z4: the loop of each zone (THE SHALLOWS, THE TWILIGHT, THE MIDNIGHT, THE ABYSS); zone z
//   (0..3) is DCM_Z1 + z. Each loop starts with 4 bars of its bed (no melody), then the theme comes in.
//   midi_bg_play_track(DCM_Z1 + z, 0, true) plays it in a seamless loop from bar 1 (its bed).
// - The start of a dive: midi_bg_play_track(DCM_J_HATCH, 0, false); DC_J_HATCH_MS later,
//   midi_bg_play_track(DCM_Z1, 0, true).
// - The ride down the current to zone z + 1: midi_bg_play_track(DCM_J_DESCENT, 0, false); DC_J_DESCENT_MS
//   later, midi_bg_play_track(DCM_Z1 + z + 1, 0, true): the new zone starts with its bed.
// - A soft change from one zone loop to another with no jingle: midi_bg_hold(false), then
//   midi_bg_set_phase(DC_PHASE_Z1 + z). The new loop comes in on the next 4-bar line, at its theme. When
//   midi_bg_state() shows the new track, call midi_bg_hold(true) to keep it in a loop.
// - A new body part: midi_bg_state(&st) (keep st.beat), then midi_bg_play_track(DCM_J_GROW, 0, false);
//   DC_J_GROW_MS later, midi_bg_play_track(DCM_Z1 + z, 4.0f * (int)(st.beat / 4.0f), true): the zone loop
//   goes on from the start of the bar where it stopped.
// - The death: DCM_J_DEATH; DC_J_DEATH_MS later, DCM_TITLE (or midi_bg_stop). The win (the creature reaches
//   the deep): DCM_J_DEEP; DC_J_DEEP_MS later, DCM_TITLE (or midi_bg_stop).
// - The jingles play once: midi_bg_play_track(DCM_J_*, 0, false). At its end the player starts a jingle
//   again (with a crash), so the game starts the next track (or calls midi_bg_stop) DC_J_*_MS after the
//   start of the jingle, or a frame before. The last notes of each jingle end before DC_J_*_MS.
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for its sound effects.
enum {
    DCM_TITLE,       // DEEP CURRENT, 76 BPM, 76 s
    DCM_Z1,          // THE SHALLOWS, 96 BPM, 80 s
    DCM_Z2,          // THE TWILIGHT, 84 BPM, 80 s
    DCM_Z3,          // THE MIDNIGHT, 72 BPM, 80 s
    DCM_Z4,          // THE ABYSS, 64 BPM, 75 s
    DCM_J_HATCH,     // HATCH, 120 BPM, 2.0 s (play once)
    DCM_J_GROW,      // GROW, 120 BPM, 1.5 s (play once)
    DCM_J_DESCENT,   // DESCENT, 80 BPM, 3.0 s (play once)
    DCM_J_DEATH,     // DEATH, 96 BPM, 2.5 s (play once)
    DCM_J_DEEP,      // THE DEEP, 90 BPM, 4.0 s (play once)
    DCM_COUNT
};
#define DC_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define DC_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: DCM_TITLE
#define DC_PHASE_Z1         1        // DCM_Z1 (zone z: DC_PHASE_Z1 + z)
#define DC_PHASE_Z2         2        // DCM_Z2
#define DC_PHASE_Z3         3        // DCM_Z3
#define DC_PHASE_Z4         4        // DCM_Z4
#define DC_J_HATCH_MS       2000     // the length of each jingle (ms): then start the next track
#define DC_J_GROW_MS        1500
#define DC_J_DESCENT_MS     3000
#define DC_J_DEATH_MS       2500
#define DC_J_DEEP_MS        4000
extern const bg_track_t DC_TRACKS[];
extern const int DC_TRACK_COUNT;
