#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "export.h"

// A small band that follows a chord progression: pad, arpeggio, bass and drums.
// The app writes the "set" fields, the audio task reads them each 16th note.

#define MUSIC_SLOTS      8  // bars in the progression
#define MUSIC_BEATS      4  // cells in a bar: one chord for each beat
#define MUSIC_CELLS      (MUSIC_SLOTS * MUSIC_BEATS)
#define MUSIC_CHORDS     8   // chord pads
#define MUSIC_COLOR_NONE 99  // pcolor[]: the cell has no colour of its own (it uses g_music.color)

enum {
    ARP_UP,
    ARP_DOWN,
    ARP_UPDOWN,
    ARP_RANDOM,
    ARP_STRUM,
    ARP_MODES
};
enum {
    BEAT_OFF,
    BEAT_POP,
    BEAT_DISCO,
    BEAT_HIPHOP,
    BEAT_BOSSA,
    BEAT_MODES
};

typedef struct {
    // set by the app
    volatile bool playing;  // transport: drums, bass, progression
    volatile uint8_t bpm;   // 60..180
    volatile uint8_t key;   // 0..11 semitones above C
    volatile uint8_t arp_mode;
    volatile uint8_t beat;
    volatile int8_t prog[MUSIC_CELLS];    // chord 0..7 for each beat, -1 = keep the chord before it
    volatile int8_t pcolor[MUSIC_CELLS];  // the colour of the chord in a cell (REC), or MUSIC_COLOR_NONE
    volatile uint8_t loop_bars;           // 0 = to the last bar with a chord, else a fixed length 1..8
    volatile int8_t live;                 // chord of a held pad, -1 = none
    volatile int8_t color;                // -2 sus2, -1 sus4, 0 triad, 1 seventh, 2 ninth
    volatile uint8_t bright;              // 0..255: the filter cutoff of the pad and the arpeggio
    volatile uint8_t range;               // 0..255: arpeggio range 1..3 octaves (low: 8th notes)
    volatile uint8_t gate;                // 0..255: arpeggio note length
    volatile uint8_t mix;                 // 0 = pad only, 128 = both, 255 = arpeggio only
    volatile uint8_t count_in;            // one shot: beats of clicks before bar 1 when playing starts
    volatile bool fill;                   // one shot: drum fill, then a crash
    // read by the app
    volatile int8_t bar;          // slot that plays now
    volatile int8_t step;         // 16th note in the bar, 0..15
    volatile int8_t chord;        // chord that sounds now, -1 = none
    volatile int8_t chord_color;  // the colour that sounds now
    volatile int8_t counting;     // beats of the count-in still to come (0 = none)
    volatile uint32_t notes;      // +1 for each arpeggio note
    volatile uint8_t last_note;   // MIDI number of the last arpeggio note
    volatile uint32_t beats;      // +1 on each kick
} music_t;

extern music_t g_music;

extern const char *const MUSIC_ROMAN[MUSIC_CHORDS];
extern const char *const MUSIC_KEYS[12];
extern const char *const MUSIC_ARP_NAMES[ARP_MODES];
extern const char *const MUSIC_BEAT_NAMES[BEAT_MODES];

// Connect to / disconnect from the audio task.
void music_start(void);
void music_stop(void);

// Name of a chord in the current key with a colour, for example "Am7".
void music_chord_name(int chord, int color, char *buf, int len);

// Bars in the loop: up to the last bar that has a chord (1..MUSIC_SLOTS).
int music_loop_bars(void);
// Length of one loop in samples at the current tempo.
uint32_t music_loop_samples(void);

// Export: record the next full loop from bar 1. The audio goes into buf (n samples,
// normally music_loop_samples()), the notes into midi (GM channels: 0 pad, 1 arpeggio,
// 2 bass, 9 drums). The app must start the playback.
void music_export(int16_t *buf, uint32_t n, midi_t *midi);
void music_export_cancel(void);
enum {
    MEXP_OFF,
    MEXP_WAIT,
    MEXP_REC,
    MEXP_DONE
};
int music_export_state(void);
