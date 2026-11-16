#pragma once
#include "midi_bg/midi_bg.h"
// MURANO: the music (mu_music.c, made by apps/murano/tools/music/mu_music.py; do not edit).
// Start: midi_bg_init(MU_TRACKS, MU_TRACK_COUNT, MU_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - MUM_TITLE: the title screen. midi_bg_play_track(MUM_TITLE, 0, true): a seamless loop (2 bars of the harp,
//   then the siciliana).
// - MUM_WORK: an order (the glass on the pipe). midi_bg_play_track(MUM_WORK, 0, true): a seamless loop.
// - A piece is sold: midi_bg_play_track(MUM_J_SOLD, 0, false); MU_J_SOLD_MS later the next track.
// - A piece is lost (it bursts, falls or cracks): midi_bg_play_track(MUM_J_FAIL, 0, false); MU_J_FAIL_MS later
//   the work loop again.
// - The jingles play once: at its end the player starts a jingle again, so the game starts the next track (or
//   calls midi_bg_stop) MU_J_*_MS after the start of the jingle.
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for its sound effects.
enum {
    MUM_TITLE,       // SICILIANA, 78 BPM, 60 s
    MUM_WORK,        // ALLEGRO, 116 BPM, 70 s
    MUM_J_SOLD,      // SOLD, 92 BPM, 3.9 s (play once)
    MUM_J_FAIL,      // LOST, 72 BPM, 2.5 s (play once)
    MUM_COUNT
};
#define MU_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define MU_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: MUM_TITLE
#define MU_PHASE_WORK       1        // MUM_WORK
#define MU_J_SOLD_MS        3913     // the length of each jingle (ms): then start the next track
#define MU_J_FAIL_MS        2500
extern const bg_track_t MU_TRACKS[];
extern const int MU_TRACK_COUNT;
