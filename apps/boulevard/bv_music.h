#pragma once
#include "midi_bg/midi_bg.h"
// BOULEVARD: the music (bv_music.c, made by apps/boulevard/tools/music/bv_music.py; do not edit).
// Start: midi_bg_init(BV_TRACKS, BV_TRACK_COUNT, BV_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - BVM_TITLE: the title screen and the lobby (the friends join). midi_bg_play_track(BVM_TITLE, 0, true)
//   plays it in a seamless loop from bar 1 (4 soft bars with no drums, then the hook of the game).
// - BVM_DAY: the board in the day. BVM_EVENING: the board at sunset and at night (a warmer jazz lounge,
//   with a light swing). When the board shows: midi_bg_play_track(BVM_DAY, 0, true) (or BVM_EVENING). Each
//   loop starts with 4 soft bars with no drums, then the tune starts. The loops are calm, so the players
//   can think, and each has a part with no tune (an interlude).
// - A soft change from one loop to another (the sun goes down, or the title goes to the board):
//   midi_bg_hold(false), then midi_bg_set_phase(BV_PHASE_EVENING) (or BV_PHASE_DAY, BV_PHASE_TITLE). The
//   new loop comes in on the next 4-bar line, at its full part. When midi_bg_state() shows the new track,
//   call midi_bg_hold(true) to keep it in a loop.
// - The jingles play once: midi_bg_play_track(BVM_J_*, 0, false). At its end the player starts a jingle
//   again (with a crash), so the game starts the next track (or calls midi_bg_stop) BV_J_*_MS after the
//   start of the jingle, or a frame before. The last notes of each jingle end before BV_J_*_MS:
//   BVM_J_START when the game starts (then BVM_DAY), BVM_J_WIN for the winner (it ends with the head of
//   the hook), BVM_J_BROKE when a player goes bankrupt (then the loop of the board again).
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for the sound effects.
enum {
    BVM_TITLE,       // BOULEVARD, 104 BPM, 74 s
    BVM_DAY,         // GORDON BEACH, 94 BPM, 102 s
    BVM_EVENING,     // JAFFA LIGHTS, 82 BPM, swing, 105 s
    BVM_J_START,     // ROLL THE DICE, 120 BPM, 2.20 s (play once)
    BVM_J_WIN,       // TYCOON, 120 BPM, 4.50 s (play once)
    BVM_J_BROKE,     // BROKE, 100 BPM, 2.40 s (play once)
    BVM_COUNT
};
#define BV_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define BV_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: BVM_TITLE
#define BV_PHASE_DAY        1        // BVM_DAY
#define BV_PHASE_EVENING    2        // BVM_EVENING
#define BV_J_START_MS       2250     // the length of each jingle (ms): then start the next track
#define BV_J_WIN_MS         4500
#define BV_J_BROKE_MS       2400
extern const bg_track_t BV_TRACKS[];
extern const int BV_TRACK_COUNT;
