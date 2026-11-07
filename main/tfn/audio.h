#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    // the block game
    SFX_MOVE,
    SFX_ROTATE,
    SFX_SOFT_DROP,
    SFX_LOCK,
    SFX_HARD_DROP,
    SFX_LINE1,
    SFX_LINE2,
    SFX_LINE3,
    SFX_4LINES,
    SFX_LEVEL_UP,
    SFX_GAME_OVER,
    SFX_START,
    // menu
    SFX_CLICK,
    SFX_BACK,
    // runner
    SFX_JUMP,
    SFX_JUMP2,
    SFX_COIN,
    SFX_DIE,
    SFX_STOMP,
    SFX_SPRING,
    SFX_POWERUP,
    SFX_SHIELD_HIT,
    SFX_CRUMBLE,
    // tilt maze
    SFX_BUMP,
    SFX_HOLE,
    SFX_CHECKPOINT,
    SFX_WIN,
    // stack
    SFX_PLACE,
    SFX_CUT,
    // hop
    SFX_BOING,
    SFX_SHOOT,
    SFX_FALL,
    // drive
    SFX_CRASH,
    SFX_BEEP,
    SFX_GO,
    // shake
    SFX_GIGGLE,
    SFX_WHOA,
    SFX_SCREAM,
    SFX_TICK,
    // prism
    SFX_STAR,
    SFX_SWITCH,
    SFX_FLAP,
    SFX_COUNT,
} sfx_t;

typedef enum {
    W_SQUARE,
    W_PULSE,
    W_TRI,
    W_NOISE,
    W_SAW,
    W_SINE,
    W_SAMPLE
} wave_t;

#define AUDIO_SAMPLE_RATE 24000
#define AUDIO_BLOCK       120  // frames per block (5 ms)

// Queue a sound effect. Safe to call from any task. Does nothing when muted.
void sfx_play(sfx_t sfx);
// Queue one tone with a pitch sweep from f0 to f1 (Hz).
void sfx_tone(uint16_t f0, uint16_t f1, uint16_t dur_ms, wave_t wave, uint8_t vol);
// A sound of the system UI (the menu ticks, the clicks of the buttons, the back sound): a quarter of the
// level of sfx_play, and nothing when UI SOUNDS is off in SETUP (so a player can have game sound with no menu
// sounds). The games use sfx_play.
void sfx_ui(sfx_t sfx);

bool audio_muted(void);

// Continuous tone (engine, jetpack...). Safe from any task. ch 0..1, vol 0..100 (0 = off).
// Pitch and volume glide to the new values.
void audio_drone(int ch, float hz, int vol, wave_t wave);

// ---- music engine (see music.c) ----

enum {
    AUDIO_PERC,
    AUDIO_SUSTAIN
};

typedef struct {
    uint32_t delay;  // samples from the start of the current block
    uint32_t dur;    // samples
    float f0, f1;    // Hz, sweep from f0 to f1
    uint8_t wave;    // wave_t
    uint8_t vol;     // 0..100
    uint16_t attack_ms;
    uint8_t shape;  // AUDIO_PERC: decay to 0; AUDIO_SUSTAIN: hold, fade at the end
    uint8_t lp;     // low-pass: 0 = off, 1 = very dark .. 255 = open
    uint8_t group;  // 0 = none; a group can be released together
    uint8_t hp;     // high-pass: 0 = off, 1 = low cut .. 255 = only the highest
    // W_SAMPLE: mono 16-bit samples at AUDIO_SAMPLE_RATE; rate 1.0 = the original pitch
    const int16_t *sample;
    uint32_t sample_len;
    float rate;
    bool loop;
} audio_voice_t;

// Only from the tick callback (audio task).
void audio_voice(const audio_voice_t *v);
void audio_release_group(uint8_t group, uint16_t ms);

// Called by the audio task before each block (AUDIO_BLOCK frames). NULL = none. From another task (ABI rev 24 and
// later): when it returns, the old tick is not in a call and gets no more calls, so its data can go.
// The tick must not wait: no lock, no queue, no other task, no sleep. From ABI rev 24 the audio task calls it with the
// lock of the sounds, and a free of an app file can wait for that lock: a tick that waits for a task in a free stops
// both tasks.
void audio_set_tick(void (*cb)(void));

// Start a voice from any task (it goes through the queue; delay counts from the next block).
void audio_play(const audio_voice_t *v);

// Stop at once each sound that uses the n bytes at p: a sample voice (W_SAMPLE) that reads them, a tune or TIA steps
// in them, a capture or a record into them, and the messages for these sounds that wait in the queue. When it returns,
// no sound uses the memory, so it can be freed or filled again. Safe from any task: it waits for one block of the
// audio task at most, and never for a sleep or a wake of the sound. A voice that stops this way has no fade: for a soft
// end, release its group in the tick first. (ABI rev 24. The free of an app file does this stop by itself on a firmware
// with rev 24 or later.)
void audio_stop_mem(const void *p, size_t n);

// ---- tunes (short start tunes of the games, see tunes.c) ----
// The audio task starts each note at its time, so a long tune uses only the voices
// that sound at that time.
#define TUNE_HOLD 0x80  // add to .wave: the note holds and fades at its end (not a decay)

typedef struct {
    uint16_t t;    // start, ms from the start of the tune
    uint16_t dur;  // ms
    uint8_t note;  // MIDI note (60 = C4). W_NOISE: the noise rate as a note
    int8_t bend;   // pitch sweep over the note, semitones (0 = none)
    uint8_t wave;  // wave_t, + TUNE_HOLD
    uint8_t vol;   // 0..100
} tune_note_t;

// Play a tune (the notes can be in any order). A new tune stops the old one. Safe
// from any task. Does nothing when muted. The notes must stay in memory (static const).
void audio_tune(const tune_note_t *notes, int n);
// Stop the tune (the notes that sound now end normally).
void audio_tune_stop(void);
// An alarm: the tune plays also when the board is muted, at the volume vol (10..100; never softer than the volume of
// the user). After the tune the settings of the user come back (g_settings: the mute and the volume). Safe from any
// task. For an app file it needs the permission class sound.alert. The screen off: the app keeps the sound with
// app_t.background.
void audio_alert(const tune_note_t *notes, int n, int vol);

typedef struct {
    uint16_t clocks;
    uint8_t audc, audf, audv;
} tia_step_t;
// Play the steps one time on channel ch (0 or 1). skip: the clocks to leave out at the start. A new call replaces
// the steps of the channel; after the last step the channel is silent. The steps must stay in memory (static
// const). Safe from any task. Does nothing when muted.
void audio_tia_play(int ch, const tia_step_t *steps, int n, uint32_t skip);
// Play the steps 'times' times on channel ch. A call with the same steps while they play does not start them
// again: they only play on for 'times' more loops (a contact sound that a game sends in each frame has no break).
void audio_tia_loop(int ch, const tia_step_t *steps, int n, int times);
// Stop both channels at once (a pause, the end of a game).
void audio_tia_off(void);

// ---- microphone (ES7210, 2 mics mixed to mono, AUDIO_SAMPLE_RATE) ----
bool audio_mic_start(void);
void audio_mic_stop(void);
bool audio_mic_on(void);
// RMS level of the last 10 ms, 0..1 of full scale
float audio_mic_level(void);
// Record mono samples into buf (up to max). Stops by itself when full. After audio_mic_record_stop or
// audio_mic_stop (ABI rev 24 and later) no sample goes into the buffer, so it can be freed.
void audio_mic_record(int16_t *buf, uint32_t max);
void audio_mic_record_stop(void);
bool audio_mic_recording(void);
uint32_t audio_mic_recorded(void);

// The microphone tap: each block of mono samples (10 ms, AUDIO_SAMPLE_RATE) goes to cb, from the
// microphone task (keep it short). NULL = none. The microphone must be on (audio_mic_start). From ABI rev 24, when it
// returns, the old cb is not in a call and gets no more calls, so its data can go (not from cb itself).
void audio_mic_tap(void (*cb)(const int16_t *s, int n));
// The tap of the web page (web_walkie.c): the same blocks; while it is set, the microphone is on
// (with no audio_mic_start). NULL = off. The same barrier as audio_mic_tap.
void audio_mic_web(void (*cb)(const int16_t *s, int n));

// ---- the output tap (USB AUDIO): each block of the mix (mono, AUDIO_BLOCK frames, AUDIO_SAMPLE_RATE) goes to cb,
// from the audio task (keep it short). NULL = none. While it is set, the sound does not sleep.
void audio_out_tap(void (*cb)(const int16_t *s, int n));

// MIDI note to Hz
float audio_midi_hz(float note);

// Synth time as percent of the 5 ms block (filtered), and the voices that play now.
int audio_load(void);

// Record the mixed output (mono) into buf, for export. skip: samples of the
// next block to leave out (from the tick callback: the delay of the first note). After audio_capture_stop (ABI rev 24
// and later) no sample goes into the buffer, so it can be freed.
void audio_capture(int16_t *buf, uint32_t max, uint32_t skip);
void audio_capture_stop(void);
bool audio_capturing(void);
uint32_t audio_captured(void);
