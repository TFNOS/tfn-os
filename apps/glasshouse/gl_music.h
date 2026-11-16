#pragma once
#include "midi_bg/midi_bg.h"
// GLASSHOUSE: the music (gl_music.c, made by apps/glasshouse/tools/music/gl_music.py; do not edit).
// Start: midi_bg_init(GL_TRACKS, GL_TRACK_COUNT, GL_MUSIC_VOICES), then midi_bg_start(). Call
// midi_bg_service() in each frame and midi_bg_end() before the app goes.
// How the game uses each track:
// - GLM_TITLE: the title and the level shelf. midi_bg_play_track(GLM_TITLE, 0, true) plays it in a
//   seamless loop from bar 1 (4 soft bars, then the waltz). It has no drums.
// - GLM_AIM: the play before and between the moves (the player looks at the miniature and thinks). When a
//   level starts: midi_bg_play_track(GLM_AIM, 0, true) (4 bars of the soft clock, then the tune). A soft
//   change from the title: midi_bg_hold(false), then midi_bg_set_phase(GL_PHASE_AIM); the new loop comes
//   in on the next 4-bar line, at its full part (no crash: GLM_TITLE and GLM_AIM have no drums). When
//   midi_bg_state() shows GLM_AIM, call midi_bg_hold(true) to keep it in a loop.
// - GLM_ACTION: after a move, while the structure falls. midi_bg_play_track(GLM_ACTION, 0, true) at the
//   move: the old loop fades in 40 ms and GLM_ACTION starts at bar 1 at once, with its full sound. A
//   second move in the same fall: let it go on. Do not start it with midi_bg_set_phase: it has soft
//   timpani, so the player marks a phase change into it with a crash cymbal.
// - The jingles play once: midi_bg_play_track(GLM_J_*, 0, false). At its end the player starts a jingle
//   again, so the game starts the next track (or calls midi_bg_stop) GL_J_*_MS after the start of the
//   jingle, or a frame before. The last notes of each jingle end before GL_J_*_MS.
//   GLM_J_WIN: the structure is still and the fragile object is whole; then GLM_AIM (the next level) or
//   GLM_TITLE (the shelf). GLM_J_LOST: the fragile object broke; then GLM_AIM (a new try). GLM_J_STAND:
//   the structure still stands and no move is left; then GLM_AIM (a new try). GLM_J_ALL: the last level
//   is done; then GLM_TITLE.
// Each phrase of each piece has a mean voice load of 9.1 or less (the plan of midi_bg.c with 14 voices):
// the game keeps the other 18 synth voices for the glass and the wood.
enum {
    GLM_TITLE,       // DUSK WALTZ, 80 BPM, 3/4, 72 s
    GLM_AIM,         // CLOCKWORK, 72 BPM, 80 s
    GLM_ACTION,      // THE FALL, 104 BPM, 55 s
    GLM_J_WIN,       // SAFE, 100 BPM, 3.00 s (play once)
    GLM_J_LOST,      // BROKEN, 80 BPM, 2.25 s (play once)
    GLM_J_STAND,     // STILL STANDING, 96 BPM, 1.88 s (play once)
    GLM_J_ALL,       // THE COLLECTION, 84 BPM, 3/4, 5.00 s (play once)
    GLM_COUNT
};
#define GL_MUSIC_VOICES     14       // the music voices for midi_bg_init
#define GL_PHASE_TITLE      0        // the phases (playlists) of midi_bg_set_phase: GLM_TITLE
#define GL_PHASE_AIM        1        // GLM_AIM
#define GL_PHASE_ACTION     2        // GLM_ACTION (start it with midi_bg_play_track, see above)
#define GL_J_WIN_MS         3000     // the length of each jingle (ms): then start the next track
#define GL_J_LOST_MS        2250
#define GL_J_STAND_MS       1875
#define GL_J_ALL_MS         5000
extern const bg_track_t GL_TRACKS[];
extern const int GL_TRACK_COUNT;
