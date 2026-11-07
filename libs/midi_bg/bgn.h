#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tfn/midifile.h"

// BGN: a compact form of the notes of a MIDI file, for MIDI_BG.
//
// The MIDI files of the user have many tracks, many copies of the same events and many
// controllers that the player does not use. BGN keeps only what the player reads: the header
// of midi_notes_t and, for each note, the start, the length, the channel, the pitch, the
// velocity and the program.
//
// For a small deflate, the notes go channel by channel (each channel in time order: a part
// that the user copied many times gives the same bytes again), and each field has its own
// stream. The decode puts the notes back in the "normal order" (bgn_normalize), so the data
// gives exactly the same note list as a MIDI file after bgn_normalize().
//
// The layout (little endian):
//   "BGN2", u32 n, f32 tpb, f32 bpm, f32 seconds, u8 ts_num, u8 ts_den, u16 channels,
//   u32 the length of each of the 5 streams, then the 5 streams:
//   0 times:   for each channel of `channels` (0..15): a varint count, then for each note a
//              varint: the start - the start of the note before in the channel (ticks)
//   1 flags:   a byte for each note: bit 0 = a program byte follows (the first note of the
//              channel, or a new program); bit 1 = a velocity byte follows (else the velocity of
//              the note before in the channel); bit 2 = the same length as the note before in
//              the channel (else a varint in stream 4)
//   2 pitch:   a byte for each note
//   3 bytes:   the program and the velocity bytes that the flags ask for, in this order
//   4 lengths: the varint lengths (ticks)
// The firmware keeps the BGN data deflated (zlib); the ROM tinfl inflates it.

#define BGN_MAGIC  "BGN2"
#define BGN_HEADER 44

// Decode BGN data into out, in the normal order (the note list goes in PSRAM; free it with
// midifile_notes_free). False if the data is not BGN or is bad (all lengths are checked).
bool bgn_decode(const uint8_t *data, size_t len, midi_notes_t *out);

// Encode (host tools). Returns the bytes written, or 0 if cap is too small. The notes can be in
// any order; use bgn_normalize() first to remove the copies.
size_t bgn_encode(const midi_notes_t *m, uint8_t *buf, size_t cap);
// The worst case size of bgn_encode() for n notes.
#define BGN_MAX_SIZE(n) (BGN_HEADER + 16 * 6 + (size_t)(n) * 14)

// The normal order of a note list: by the start, then the top note first, then the channel, the
// end, the velocity and the program (a full order, so the result does not depend on the order
// before). Then the exact copies go: a note with the same start, channel and pitch as the note
// before it (two tracks with the same part; the player plays only one of them, so the sound
// does not change, but the voice plan then counts each note once).
// Returns the number of notes removed.
int bgn_normalize(midi_notes_t *m);
