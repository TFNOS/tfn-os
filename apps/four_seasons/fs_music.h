#pragma once
#include "midi_bg/midi_bg.h"
// FOUR SEASONS: the music (fs_music.c, made by apps/four_seasons/tools/music/fs_music.py; do not edit).
// ONE main theme (8 bars) is in all the tracks: each season loop is a variation of it, with its own key,
// metre, tempo and instruments. tools/music/MUSIC.md tells more.
// Start: midi_bg_init(FS_TRACKS, FS_TRACK_COUNT, FS_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - FSM_TITLE: the title screen. midi_bg_play_track(FSM_TITLE, 0, true) plays it in a seamless loop from
//   bar 1 (4 soft bars with no drums, then the theme).
// - FSM_SPRING, FSM_SUMMER, FSM_AUTUMN, FSM_WINTER: the season of the pictures. When the season stays the
//   same for about 1.5 s: midi_bg_play_track(FSM_<season>, 0, true). The old loop fades in 40 ms. The new
//   loop starts at bar 1 with 4 soft bars with no drums, and its end joins bar 1 with no seam (no drums at
//   the two sides of the seam). FSM_AUTUMN is a waltz in 3/4. FSM_WINTER has no drums.
// - A soft change on a phrase line: midi_bg_hold(false), then midi_bg_set_phase(FS_PHASE_*). The new loop
//   starts on the next 4-bar line, at its full part (bar 5). When midi_bg_state() shows the new track,
//   call midi_bg_hold(true) to keep it in a loop.
// - The jingles play once: midi_bg_play_track(FSM_J_*, 0, false). At the end of a jingle the player goes
//   on with the loop of the phase (at its bar 5, with a crash) or starts the jingle again, so the game
//   starts the next track (for example midi_bg_play_track(FSM_<season>, 0, true)) FS_J_*_MS after the
//   start of the jingle, or a frame before. The last notes of each jingle end before FS_J_*_MS:
//   FSM_J_FOUND when the player finds an item, FSM_J_PART when a part of the kite is complete.
// - FSM_J_END: the end of the game (the kite flies in the autumn wind and the valley goes through the
//   four seasons). It plays once: call midi_bg_stop(400) FS_J_END_MS after its start. The parts of it
//   (ms from its start): the wind 0, spring 2667, summer 8000, autumn 13333, winter 18667, the final chord 24000.
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for the sound effects.
enum {
    FSM_TITLE,       // FOUR SEASONS, 88 BPM, 76 s
    FSM_SPRING,      // SPRING, 100 BPM, 77 s
    FSM_SUMMER,      // SUMMER, 84 BPM, 69 s
    FSM_AUTUMN,      // AUTUMN, 90 BPM, 3/4, 64 s
    FSM_WINTER,      // WINTER, 70 BPM, 69 s
    FSM_J_FOUND,     // FOUND IT, 120 BPM, 1.5 s (play once)
    FSM_J_PART,      // KITE PART, 120 BPM, 3.0 s (play once)
    FSM_J_END,       // THE KITE FLIES, 90 BPM, 26.7 s (play once)
    FSM_COUNT
};
#define FS_PHASE_TITLE      0        // the phases of midi_bg_set_phase: one loop each
#define FS_PHASE_SPRING     1
#define FS_PHASE_SUMMER     2
#define FS_PHASE_AUTUMN     3
#define FS_PHASE_WINTER     4
#define FS_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define FS_J_FOUND_MS       1500     // the length of each play-once track (ms): then start the next track
#define FS_J_PART_MS        3000
#define FS_J_END_MS         26667
extern const bg_track_t FS_TRACKS[];
extern const int FS_TRACK_COUNT;
