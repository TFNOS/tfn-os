#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/audio.h.
#include "tfn/audio.h"

#include <stdbool.h>
#include <stdint.h>

// Start the ES8311 codec, the I2S output and the synthesizer task. Call after board_init().
void audio_init(void);

// UI SOUNDS of SETUP (the store key "ui_snd": 0 = on, the default)
bool audio_ui_sounds(void);
void audio_set_ui_sounds(bool on);

void audio_set_mute(bool mute);
// The screen is off and no app plays in the background (main.c): the codec closes and the audio task waits (no
// I2S, so the CPU can sleep). The sounds that come in that time are dropped (the amplifier is off). false = the
// codec opens again (some ms).
void audio_sleep(bool on);
// The VOLUME of the board, 0..AUDIO_VOL_MAX. 0..100 = the volume of the codec (100 = 0 dB: the real full scale, no
// clip in the codec). 101..AUDIO_VOL_MAX = the codec stays at 0 dB, and a gain before the limiter of the mix makes the
// sound louder (up to +6 dB at 125); the limiter keeps the peaks clean. The UI shows 100 % as the real
// full scale, and the user can go up to 125 % (with a gain).
#define AUDIO_VOL_MAX 125
void audio_set_volume(int vol);

// ---- music engine (see music.c) ----

// Stop the voices of a group (a 10 ms fade) from any task, and wait (about 20 ms) until they
// are silent: after it, their sample memory can be freed. Not from the audio task.
void audio_kill_group(uint8_t group);

// The loader of the app files, when an app left: a tick that is still set (the last tick of an app: it ends the notes
// and clears itself) gets two blocks to run (timeout_ms at most), then it goes. When it returns, no tick is in a call
// and none gets a call, so the code of the app can go.
void audio_tick_end(int timeout_ms);

// The sounds that use the n bytes at p now: the sample voices, the TIA steps, the tune, the capture buffer, the record
// buffer and the messages for them in the queue. The result: their count. stop = true: they stop (audio_stop_mem).
// The loader of the app files logs a free that stops a sound; the simulator counts them (simulator/src/sim_app_mem.c).
int audio_mem_check(const void *p, size_t n, bool stop);
// The messages in the queue that point into memory now (a sample voice, a tune, TIA steps; the checks of the simulator)
uint32_t audio_queued_refs(void);

// ---- the TIA voice: the two sound channels of the Atari 2600 chip (the TIA), at its clock ----
// A step sets the three registers of a channel and holds them for 'clocks' TIA clocks (AUDIO_TIA_HZ in 1 s).
// AUDC: the kind of wave (0 and 11 = a constant level: no sound; 1 = the 4-bit pattern; 4, 5 = a pure tone;
// 6, 10 = the divide by 31; 7 = the 5-bit pattern, divided by 2; 8 = white noise; 12..15 = 4..7 with one more
// divide by 3). AUDF: the pitch divider 0..31 (the wave moves on each AUDF + 1 clocks). AUDV: the volume 0..15.
#define AUDIO_TIA_HZ 31399.5f

// true: the speaker is quiet (the mix still goes to the tap)
void audio_speaker_off(bool off);

// ---- the voice stream (WALKIE): samples at AUDIO_SAMPLE_RATE, mixed into the output ----
// One writer (the app) and the audio task as the reader. What does not fit is dropped.
void audio_stream_write(const int16_t *s, int n);
int audio_stream_fill(void);  // samples that wait
void audio_stream_clear(void);
void audio_stream_gain(int q8);  // 256 = 1.0

int audio_limiter_take(void);  // the lowest gain of the output limiter since the last call, percent (tests)
// the highest peak of the mix before the VOLUME boost and the limiter since the last call, percent of full scale
// (100 = 32767; over 100 = the sum of the sounds is too loud and the limiter must work: the console `snd`, tests)
int audio_peak_take(void);
int audio_voices_on(void);
