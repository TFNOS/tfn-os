// TFN OS: the voice (an English voice that the board makes: tiny-voca, the store app VOICE). Only the boards with
// BOARD_TTS (an octal PSRAM and an SD card) have it; on the others speech_offer() gives false.
// The voice is a module: the watcher (watch.h) of the app VOICE gives its functions to TFN OS in its init(). TFN OS
// calls them on its speech task (the memory and the files of the calls go into the set of that watcher), one call at a
// time, and plays the pieces that the module gives. The setting READ NOTICES ALOUD (SETUP) uses it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// A piece of the voice: n samples, 16-bit mono at AUDIO_SAMPLE_RATE (tfn/audio.h). TFN OS copies it and plays it after
// the piece before it. Gives false: stop (the module ends its say() at once).
typedef bool (*speech_sink_fn)(void *user, const int16_t *pcm, int n);

typedef struct {
    // Make the voice of a text (UTF-8): split it into pieces, give each piece to sink when it is ready. Stop at the next
    // piece when *stop is true. 0 = done, < 0 = a fault (no data on the card, no memory). It may keep its model open
    // for the next call.
    int (*say)(const char *text, speech_sink_fn sink, void *user, volatile const bool *stop);
    // Free all memory (the model) and end the tasks of the voice. The next say() opens all again.
    void (*close)(void);
} speech_ops_t;

// A watcher gives its voice to TFN OS (call it in the init() of the watcher; TFN OS keeps a copy of ops). The voice
// stays until the watcher goes (a new version, a removal). False: this board has no voice, or the call is not from the
// init of a watcher.
bool speech_offer(const speech_ops_t *ops);

// For the apps: a voice is ready (the app VOICE is installed, its data is on the card), and a text to say (it waits in
// a queue of 8 texts; the setting READ NOTICES ALOUD does not matter here). A board with no voice: false / nothing.
bool speech_ready(void);
void speech_say(const char *text);
void speech_stop(void);  // stop the text of now and the queue (not the reader below)

// ---- VOICE FILES MADE AHEAD (a cache on the card). The voice is slower than real time (about 0.45 x on the 3.49), so
// a text that is known ahead (a line of a quest, an alarm) is made into a file before it is necessary, then played at
// once. The file (any name, the extension .tvc by custom) has a header with a check of its text.
// Queue the make of the voice of a text into a file (after the texts in the queue). False: no voice, a full queue.
bool speech_make(const char *text, const char *path);
// The file has the voice of this text (text NULL: of any text). It is false while the file is made.
bool speech_file_ok(const char *path, const char *text);
// Play a voice file after the voice that plays now. Its length in ms, or 0 (no file, no memory). The app can close:
// the sound plays to its end. It plays also when the board is muted (an alarm uses it).
int speech_play_file(const char *path);

// ---- THE READER: a long text (a book, an article) that the voice reads aloud. The text is a file on the card with
// one sentence (or a short part, at most 400 bytes) on each line. TFN OS makes the voice of the lines AHEAD of the
// play into the cache folder (one file each line, kept for the next time), and plays them in order. The play starts
// when the rest can follow with no stop (or when hold_s seconds are ready). One reader at a time; it goes on when the
// app closes (in the menu and with the screen off; it waits while a game shows).
// The voice of line L is the file <cache>/d<L / 100 in 3 digits>/<L in 5 digits>.tvc (for example
// /sdcard/x/c/d012/01234.tvc): an app can make all its lines ahead (play false, a big ahead_s) and later play one line
// with speech_play_file (a game, where the voice is not made).
typedef struct {
    const char *text;   // the text file (UTF-8, a line = a sentence)
    const char *cache;  // the folder of the voice files of this text (TFN OS makes it)
    int line;           // the first line to read (0 = the start)
    int hold_s;         // the play waits for at most this much voice ahead (0: plays at the first line)
    int ahead_s;        // make at most this much voice ahead of the play (0: 30 minutes)
    bool play;          // play when ready (false: only make ahead; speech_read_play(true) later)
} speech_read_cfg_t;
enum {
    SPR_OFF,    // no reader
    SPR_MAKE,   // it makes the voice ahead (the play is off)
    SPR_WAIT,   // the play waits for enough voice ahead (wait_ms)
    SPR_PLAY,   // it plays
    SPR_END,    // all lines played
    SPR_FAULT,  // no text file, no voice, no card
};
typedef struct {
    uint8_t state;  // SPR_*
    bool play;      // the play is on (speech_read_play)
    int lines;      // the lines of the text
    int line;       // the line that plays now (or plays next)
    int made;       // the first line from line on that has no voice yet (= lines: all made)
    int ahead_ms;   // the voice that is made and not yet played
    int wait_ms;    // SPR_WAIT: about this much time until the play starts
    int rate_pc;    // the speed of the voice in % of real time (made voice / time to make it)
} speech_read_t;
bool speech_read_open(const speech_read_cfg_t *c);  // a new reader (the old one stops); false: no voice, no text
void speech_read_play(bool on);                     // the play on / off (a pause; the make goes on)
void speech_read_close(void);                       // stop the reader (the cache stays)
void speech_read_get(speech_read_t *st);
