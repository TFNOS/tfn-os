#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Standard MIDI files for TILES: read a file, keep one melody channel as tiles, and keep
// the other channels as an accompaniment that plays in step with the player.
//
// Simple on purpose: the beat grid comes from the ticks (tempo changes do not move
// notes; the game sets its own playable speed), melody notes snap to a grid of 1/8 or
// 1/16 notes, a melody chord keeps its top note (two notes: a 2-tile row), and long rests
// become short. A bad file never crashes the reader: all lengths are checked and
// there are limits for the size and the number of events.

// one tile: the same fields as the notes of the built-in songs
typedef struct {
    uint8_t p;     // MIDI pitch
    uint8_t len;   // length in units (1 .. MIDI_MAX_TILE_LEN)
    uint8_t p2;    // the second note of a 2-tile row, 0 = none
    uint8_t bass;  // always 0 (the accompaniment has the bass)
} midi_tile_t;

enum {
    TIMBRE_PIANO,
    TIMBRE_ORGAN,
    TIMBRE_STRINGS,
    TIMBRE_BASS,
    TIMBRE_LEAD,
    TIMBRE_PLUCK,
    TIMBRE_DRUMS
};

// one note of the accompaniment, fixed to a melody tile
typedef struct {
    uint16_t seg;  // the melody tile that starts this note
    uint16_t at;   // where in that tile: 0..65535 = the start .. the end of the tile
    uint16_t dur;  // length in 1/48 beat (at most 8 beats)
    uint8_t ch;    // 0..15 (9 = drums)
    uint8_t pitch, vel;
    uint8_t timbre;  // TIMBRE_*
} midi_acc_t;

typedef struct {
    midi_tile_t *tiles;  // PSRAM
    uint16_t *pre;       // the start of each tile in units
    int ntiles;
    uint16_t units;    // the length of the song in units
    uint8_t upb;       // units in a beat: 2 (1/8 grid) or 4 (1/16 grid)
    uint8_t long_len;  // a single tile this long or longer is a long (hold) tile
    float speed;       // units per second at the start: a playable speed
    midi_acc_t *acc;   // sorted by seg, then at
    int nacc;
    int melody_ch;                 // 0..15
    uint16_t channels;             // bit c: channel c has notes
    uint16_t ch_notes[16];         // notes of each channel in the file
    uint8_t ch_lo[16], ch_hi[16];  // the lowest and the highest note of each channel
    int8_t suggest_ch;             // a channel that looks like a melody (high and busy), -1 = none
    int melody_notes;              // notes of the melody channel in the file
    float seconds;                 // the real length (with the tempo map)
} midi_song_t;

bool midifile_load(const char *path, int melody_ch, midi_song_t *out, char *err, int errlen);
void midifile_free(midi_song_t *s);

// A note name, for example "C#4" (buf: 5 bytes or more).
void midifile_note_name(int pitch, char *buf, int len);

// ---- all the notes of a file (STUDIO) ----

// one note of the file, in ticks
typedef struct {
    uint32_t on, off;
    uint8_t ch;  // 0..15 (9 = drums)
    uint8_t pitch, vel;
    uint8_t prog;  // the GM program of the channel when the note started
} midi_note_t;

typedef struct {
    midi_note_t *notes;  // PSRAM, sorted by the start (then the top note first)
    int n;
    float tpb;               // ticks in a beat
    float bpm;               // the first tempo (120 if the file has none)
    uint8_t ts_num, ts_den;  // the first time signature (4/4 if the file has none)
    uint16_t channels;       // bit c: channel c has notes
    float seconds;           // the real length (with the tempo map)
} midi_notes_t;

// Read all the notes of a file. On an error: false, and err has the reason.
bool midifile_read_notes(const char *path, midi_notes_t *out, char *err, int errlen);
// The same from a file in memory (MIDI_BG: a file that the ROM inflate made).
bool midifile_read_notes_data(const uint8_t *data, size_t len, midi_notes_t *out, char *err, int errlen);
void midifile_notes_free(midi_notes_t *m);
