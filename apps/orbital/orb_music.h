#pragma once
#include "midi_bg/midi_bg.h"
// ORBITAL: the music (orb_music.c, made by apps/orbital/tools/music/orb_music.py; do not edit).
// Start: midi_bg_init(OR_TRACKS, OR_TRACK_COUNT, OR_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - ORM_TITLE: the title screen and the mission menu. midi_bg_play_track(ORM_TITLE, 0, true) plays it in a
//   seamless loop from bar 1 (4 soft bars with no drums and no tune, then the theme).
// - ORM_COAST: the orbit view while the tug coasts (also in the time warp). Ambient, no drums.
// - ORM_CLOSE: the close approach, the tether and the tow. A heartbeat, ticks and a short motif.
// - ORM_AIR: the dive into the upper air (the aerobrake, the heat). Toms, a strong groove, a rising line.
// - A loop from bar 1: midi_bg_play_track(ORM_COAST, 0, true) (or another loop). Each loop starts with 4
//   thin bars (its intro), then its full part.
// - A soft change from one loop to another: midi_bg_hold(false), then midi_bg_set_phase(OR_PHASE_CLOSE)
//   (or another OR_PHASE_*). The new loop comes in on the next 4-bar line, at its full part. When
//   midi_bg_state() shows the new track, call midi_bg_hold(true) to keep it in a loop.
// - The jingles play once: midi_bg_play_track(ORM_J_*, 0, false). At its end the player starts a jingle
//   again (with a crash), so the game starts the next track (or calls midi_bg_stop) OR_J_*_MS after the
//   start of the jingle, or a frame before. The last notes of each jingle end before OR_J_*_MS:
//   ORM_J_START when a mission starts (then a loop), ORM_J_LOCK when the tether locks on (then the loop
//   again), ORM_J_DONE when a mission is complete, ORM_J_FAIL when a mission fails, ORM_J_ALL when all
//   the missions are done. Each loop can come after each jingle (ORM_J_START and ORM_J_LOCK have no third).
// Each phrase of each piece has a mean voice load of 10.4 or less (the plan of midi_bg.c with 16 voices):
// the game keeps the other 16 synth voices for the engine sound and the other effects.
enum {
    ORM_TITLE,       // ORBITAL, 84 BPM, 91 s
    ORM_COAST,       // QUIET ORBIT, 72 BPM, 80 s
    ORM_CLOSE,       // CLOSE APPROACH, 104 BPM, 83 s
    ORM_AIR,         // RE-ENTRY, 126 BPM, 84 s
    ORM_J_START,     // MISSION START, 100 BPM, 2.4 s (play once)
    ORM_J_LOCK,      // TETHER LOCK, 120 BPM, 1.2 s (play once)
    ORM_J_DONE,      // MISSION DONE, 104 BPM, 3.5 s (play once)
    ORM_J_FAIL,      // MISSION LOST, 84 BPM, 2.5 s (play once)
    ORM_J_ALL,       // ALL MISSIONS, 108 BPM, 5.0 s (play once)
    ORM_COUNT
};
#define OR_MUSIC_VOICES     16       // the music voices for midi_bg_init
#define OR_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: ORM_TITLE
#define OR_PHASE_COAST      1        // ORM_COAST
#define OR_PHASE_CLOSE      2        // ORM_CLOSE
#define OR_PHASE_AIR        3        // ORM_AIR
#define OR_J_START_MS       2400     // the length of each jingle (ms): then start the next track
#define OR_J_LOCK_MS        1250
#define OR_J_DONE_MS        3462
#define OR_J_FAIL_MS        2500
#define OR_J_ALL_MS         5000
extern const bg_track_t OR_TRACKS[];
extern const int OR_TRACK_COUNT;
