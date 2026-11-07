#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/midifile.h.
#include "tfn/midifile.h"

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

#define MIDI_MAX_FILE     (256 * 1024)
#define MIDI_MAX_TILES    600
#define MIDI_MAX_ACC      6000
#define MIDI_MAX_TILE_LEN 8  // units

// melody_ch: the channel for the tiles (0..15), or -1 for the first channel with notes
// (not channel 10, the drums). On an error: false, and err has the reason.
bool midifile_parse(const uint8_t *data, size_t len, int melody_ch, midi_song_t *out, char *err, int errlen);

// One line for the web page, for example "melody ch 1: 214 notes, 3 more channels, 2:15".
// Returns false if the file is not a valid MIDI file (then out has the reason).
bool midifile_summary(const char *path, char *out, int len);
// The same from a file in memory.
bool midifile_summary_data(const uint8_t *data, size_t size, char *out, int len);

// ---- all the notes of a file (STUDIO) ----
