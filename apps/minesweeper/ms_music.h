#pragma once
#include "midi_bg/midi_bg.h"
// MINESWEEPER: the music (ms_music.c, made by apps/minesweeper/tools/music/ms_music.py; do not edit).
// Start: midi_bg_init(MS_TRACKS, MS_TRACK_COUNT, MS_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - MSM_TITLE: the title screen and the garden menu. midi_bg_play_track(MSM_TITLE, 0, true) plays it in
//   a seamless loop from bar 1 (4 soft bars with no drums, then the full theme).
// - MSM_DAY: the gardens 1-4 (spring meadow, sunflowers, beach, autumn). MSM_NIGHT: the gardens 5-8 (snow,
//   desert, night garden, volcano). When a garden starts: midi_bg_play_track(MSM_DAY, 0, true) (or
//   MSM_NIGHT). Each loop starts with 4 soft bars with no drums (the day loop has a short marimba figure
//   in bar 4), then the tune starts. The loops are calm, so the player can think. MSM_NIGHT is in 3/4.
// - A soft change from one loop to another: midi_bg_hold(false), then midi_bg_set_phase(p) (the phase p:
//   0 = MSM_TITLE, 1 = MSM_DAY, 2 = MSM_NIGHT). The new loop starts on the next 4-bar line, at its full
//   part. When midi_bg_state() shows the new track, call midi_bg_hold(true) to keep it in a loop.
// - The jingles play once: midi_bg_play_track(MSM_J_*, 0, false). At its end the player starts a jingle
//   again (with a crash), so the game starts the next track (or calls midi_bg_stop) MS_J_*_MS after the
//   start of the jingle, or a frame before. The last notes of each jingle end before MS_J_*_MS:
//   MSM_J_CLEAR when a garden is clear, MSM_J_BOOM when a mine goes off, MSM_J_ALL when all 8 gardens
//   are clear.
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for the sound effects.
enum {
    MSM_TITLE,       // MOLE HILL, 106 BPM, 72 s
    MSM_DAY,         // SUNNY PATCH, 94 BPM, 82 s
    MSM_NIGHT,       // MOON GARDEN, 84 BPM, 3/4, 77 s
    MSM_J_CLEAR,     // GARDEN CLEAR, 120 BPM, 3.0 s (play once)
    MSM_J_BOOM,      // OOPS, 120 BPM, 2.0 s (play once)
    MSM_J_ALL,       // ALL GARDENS, 120 BPM, 5.0 s (play once)
    MSM_COUNT
};
#define MS_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define MS_J_CLEAR_MS       3000     // the length of each jingle (ms): then start the next track
#define MS_J_BOOM_MS        2000
#define MS_J_ALL_MS         5000
extern const bg_track_t MS_TRACKS[];
extern const int MS_TRACK_COUNT;
