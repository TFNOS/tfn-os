#pragma once

#include <stdbool.h>
#include <stdint.h>

// Files for download (web page) in /data/exports, or /sdcard/exports (SAVE TO below).

// Next free name, for example "/data/exports/pads_004" (no extension), in the place of SAVE TO.
void export_next_name(const char *prefix, char *out, int len);

// The path <place>/<sub>/<name> (sub = "songs", "exports"). A write (rd false) makes the folder. A read (rd true) of a
// file that is only in the other place gives the path there (a song saved before a change of the place still loads).
void export_path(const char *sub, const char *name, bool rd, char *out, int len);
// true = a card is in and the user did not choose yet: the app asks SAVE TO: CARD / BOARD (sysui_ask) at the first save
bool export_place_ask(void);
void export_place_set(bool card);

// A simple MIDI file (format 0, 96 ticks per quarter note).
typedef struct {
    uint32_t tick;
    uint8_t status, d1, d2;
} midi_ev_t;

typedef struct {
    midi_ev_t *ev;
    int n, cap;
} midi_t;

bool midi_begin(midi_t *m, int cap);
// A note from tick t0 to t1 on channel ch (0..15; 9 = drums).
void midi_note(midi_t *m, uint32_t t0, uint32_t t1, int ch, int note, int vel);
void midi_program(midi_t *m, int ch, int program);
void midi_end(midi_t *m);

// Save base.wav (and base.mid if midi is not NULL) in a background task, because
// flash writes are slow. The task takes the buffers and frees them at the end.
// Returns false if a save runs now or there is no memory.
bool export_save_async(const char *base, int16_t *samples, uint32_t n, uint32_t rate, midi_t *midi, int bpm);
// -1 = the last save failed, 0..99 = saving now, 100 = done (or never started)
int export_progress(void);
