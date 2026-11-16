#pragma once
#include "midi_bg/midi_bg.h"
// SUMMER CUP: the music (sc_music.c, made by apps/summer/tools/music/sc_music.py; do not edit).
// Start: midi_bg_init(SC_TRACKS, SC_TRACK_COUNT, SC_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - SCM_TITLE: the title screen. midi_bg_play_track(SCM_TITLE, 0, true): a seamless loop.
// - SCM_PLAY: the events. midi_bg_play_track(SCM_PLAY, 0, true): a seamless loop.
// - A medal: midi_bg_play_track(SCM_J_MEDAL, 0, false); SC_J_MEDAL_MS later the next track.
// - The jingle plays once: at its end the player starts it again, so the game starts the next track (or calls
//   midi_bg_stop) SC_J_MEDAL_MS after the start of the jingle.
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for its sound effects.
enum {
    SCM_TITLE,       // GOLDEN HOUR, 116 BPM, 58 s
    SCM_PLAY,        // LINEUP, 152 BPM, 44 s
    SCM_J_MEDAL,     // MEDAL, 120 BPM, 2.0 s (play once)
    SCM_COUNT
};
#define SC_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define SC_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: SCM_TITLE
#define SC_PHASE_PLAY       1        // SCM_PLAY
#define SC_J_MEDAL_MS       2000     // the length of the jingle (ms): then start the next track
#define SC_HAVE_MUSIC       1
extern const bg_track_t SC_TRACKS[];
extern const int SC_TRACK_COUNT;
