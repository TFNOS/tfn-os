#pragma once
#include "midi_bg/midi_bg.h"
// GUMMY SIEGE: the music (gs_music.c, made by apps/gummy_siege/tools/music/gs_music.py; do not edit).
// Start: midi_bg_init(GS_TRACKS, GS_TRACK_COUNT, GS_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// Each track is a seamless loop: midi_bg_play_track(GSM_x, 0, true) plays it from bar 1 and joins its end
// to bar 1 with no seam. How the game uses each track:
// - GSM_TITLE: the title screen and the level map. 4 bars of the whistle call, then the theme.
// - GSM_PICNIC: the levels of world 1 (SUNSET PICNIC).
// - GSM_KITCHEN: the levels of world 2 (MIDNIGHT KITCHEN).
// - GSM_PARTY: the levels of world 3 (BIRTHDAY BASH).
// - At a level start and on the title: midi_bg_play_track(GSM_x, 0, true) for the track of that screen (the
//   old track fades in 40 ms). A call starts the track again from bar 1: make the call only when the track
//   changes (keep the track that plays, as GOLDEN HOUR keeps s_loop), so a level map after the title
//   does not start the title music again.
// - The end card of a level (the game plays its MP3 jingle): midi_bg_stop(fade_ms). The player is then
//   quiet; call midi_bg_start() before the next midi_bg_play_track().
// - A soft change from one loop to another (an option): midi_bg_hold(false), then
//   midi_bg_set_phase(GS_PHASE_*). The new loop comes in on the next 4-bar line, at its full part (bar 5).
//   When midi_bg_state() shows the new track, call midi_bg_hold(true) to keep it in a loop.
// Each phrase of each piece has a mean voice load of 9.1 or less (the plan of midi_bg.c with 14 voices):
// the game keeps the other 18 synth voices for its MP3 effects and the other sounds.
enum {
    GSM_TITLE,       // THE SIEGE, 100 BPM, 106 s
    GSM_PICNIC,      // SUNDOWN, 92 BPM, 78 s
    GSM_KITCHEN,     // MIDNIGHT, 80 BPM, 96 s
    GSM_PARTY,       // FIESTA, 120 BPM, 88 s
    GSM_COUNT
};
#define GS_MUSIC_VOICES     14       // the music voices for midi_bg_init
#define GS_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: GSM_TITLE
#define GS_PHASE_PICNIC     1        // GSM_PICNIC
#define GS_PHASE_KITCHEN    2        // GSM_KITCHEN
#define GS_PHASE_PARTY      3        // GSM_PARTY
extern const bg_track_t GS_TRACKS[];
extern const int GS_TRACK_COUNT;
