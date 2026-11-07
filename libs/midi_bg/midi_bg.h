#pragma once

#include <stdbool.h>
#include <stdint.h>

// MIDI_BG: background music for a game from Standard MIDI files, with the STUDIO instruments
// and the drum kit (the same sound as the MIDI stream of STUDIO).
//
// - The tracks are in flash as deflated BGN data (bgn.h; CRUISE: 23 KB for 9 pieces) or deflated
//   MIDI files. A track loads (the ROM inflate + the BGN decode of bgn.c, or the MIDI reader of
//   midifile.c) in the app task into PSRAM, before it is needed; the audio tick only plays notes.
// - A playlist for each "phase" (CRUISE: the 5 light phases). A track can be in more than one
//   playlist; at a change of the phase, a track that is also in the new phase goes on. A track plays from its start to
//   its end, with 2 bars of its ending at most, then the next track of the phase comes in on
//   the bar line: the held notes of the old track fade, a crash marks bar 1. Never silence.
// - A change of the phase: the next track comes in on the next 4-bar line (a phrase).
// - A track that comes from the playlist starts in its full part ("entry", after a thin intro),
//   so a game has music at once (midi_bg_play_track can start at bar 1).
// - HOLD: the track stays; at its loop end it goes back to its loop start (a seamless loop
//   from loops.py: the bars around the two points match). A mission can hold its music.
// - The music uses at most `voices` synth voices (default 20 of 32), so the sound effects
//   of the game always have voices. Under load, pads and the other parts lose layers first,
//   then notes; the drums, the bass and the lead stay.
// - volume and duck: the new notes get softer at once; held notes keep their level.
//
// The music owns the audio tick (audio_set_tick). A game that needs its own tick calls
// midi_bg_tick() from it. Before the app goes (leave), it must call midi_bg_end().

typedef struct {
    const char *name;
    uint8_t phases;        // the playlists that it belongs to: bit p = phase p (0..7)
    float loop_s, loop_e;  // the seamless loop for HOLD, beats from the start of the file
    uint8_t seamless;      // 0: the loop join is not seamless (a crash marks it)
    float entry;           // a segue in the middle of a ride starts here (beats)
    float end;             // the end of play (beats): the music + 2 bars of its ending
    uint8_t kit;           // the drum kit (drumkit.h KIT_*)
    uint8_t level;         // the loudness of the track, percent (the same rms for all)
    uint8_t gain[16];      // the mix of each MIDI channel, percent (CC7 x CC11 of the file)
    const uint8_t *z;      // the deflated BGN data or MIDI file (zlib); NULL on the host: a file
    uint32_t zlen, rawlen;
} bg_track_t;

#define MIDI_BG_PHASES 8

// Start: the tracks (the table stays in memory), and the voices that the music can use.
void midi_bg_init(const bg_track_t *tracks, int n, int voices);
// Connect to the audio tick and play. stop: the held notes fade in fade_ms and the music is
// quiet (the tick stays connected; midi_bg_start plays again from the phase).
void midi_bg_start(void);
void midi_bg_stop(int fade_ms);
// Stop at once (a short fade), disconnect the audio tick and free all the track memory. Call it
// from the app task before the app goes. It waits about 30 ms for the audio task.
void midi_bg_end(void);
// The time (ms) of the last track load (the inflate and the decode), for the log.
int midi_bg_load_ms(void);
// The playlist: the tracks of this phase, one after the other. The first call starts the
// first track of the phase (in its full part); a later call changes the track on a phrase line.
void midi_bg_set_phase(int phase);
// Keep the track that plays now (it loops), or go on with the playlist.
void midi_bg_hold(bool hold);
// 0..100: the master volume (up to 600: a quiet piece louder; each layer of a note stops at 255). duck: a lower level (0..100 of the master) for ms, then back.
void midi_bg_volume(int vol);
// THE NOTES OVER 100 %: the highest note volume since the last call, percent of what a voice (or a drum) can play.
// Over 100 = a note was cut (its dynamics are lost): the volume of the piece is too high (the console `snd`, tests).
int midi_bg_over_take(void);
void midi_bg_duck(int level, int ms);
// Call from the app task each frame: it loads the next track before it is needed.
void midi_bg_service(void);
// The audio tick (only when a game has its own tick; else midi_bg_start connects it).
void midi_bg_tick(void);

typedef struct {
    int track;  // the index in the table, -1 = none
    const char *name;
    float beat;      // the place in the file (beats)
    int bar;         // 1.. from the first bar with a note
    int passes;      // the loop jumps in HOLD
    int voices;      // the music voices that sound now (an estimate)
    int notes;       // notes played or tried (since the last midi_bg_play_track)
    int dropped;     // of them: notes left out for the voice limit
    int thinned;     // of them: notes with fewer layers for the voice limit
    uint16_t muted;  // bit c: channel c is quiet now (this phrase needs too many voices)
    float load;      // the mean voices of the parts that play
} midi_bg_state_t;
void midi_bg_state(midi_bg_state_t *st);

// Tests and a jukebox: play track i from beat `at` (in the app task; it loads the track).
bool midi_bg_play_track(int i, float at, bool hold);
