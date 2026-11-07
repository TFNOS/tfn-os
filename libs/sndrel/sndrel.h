// libs/sndrel: the end of the sample voices of an app before their memory goes. Header only (static inline, no object
// file); it uses only the audio API of each firmware, so an app with it runs also on an older firmware.
//
// A sample voice of the mixer (W_SAMPLE) reads the memory of the app while it plays, also during its fade. So the app
// must not free that memory (or fill it again) before the audio task ends the fade. The tick of the app releases its
// voice groups in each block of the release and counts the blocks; the app task waits for the count, not for a fixed
// time: an audio task that is late between its blocks (a flash write on the board, a busy PC) makes the wait longer.
// A time limit ends the wait when no block comes in limit_ms: the sound sleeps (the screen is off: no voice plays),
// the board has no sound, or the audio task stops for that long. In the last case a voice can still play: the time
// limit cannot tell the difference. Then a firmware with ABI rev 24 or later keeps the free safe: the free of an app
// file stops each sound that uses the block (audio_stop_mem), and the simulator does the same for the apps that it
// builds in. On an older firmware such a voice reads the freed block until its end.
//
// The tick of the app (the audio task, before each block), at its start:
//     if (sndrel_tick(&s_rel)) {
//         audio_release_group(MY_GROUP, MY_FADE_MS);  // (in each block: also a voice that the queue starts late)
//         return;                                      // (no new voice)
//     }
// The leave of the app (the app task):
//     sndrel_wait(&s_rel, MY_FADE_MS, SNDREL_LIMIT_MS);  // the voices of its groups are silent
//     audio_set_tick(NULL);
//     ... free the samples ...
// The tick holds the voices off until audio_set_tick(NULL); sndrel_reset() starts again (the enter of the app, or a
// new voice after a release while the app stays open).
// A firmware with ABI rev 24 or later also stops each sound that reads the memory of an app file when the app frees it
// (audio_stop_mem), and audio_set_tick(NULL) waits for a tick in a call; on an older firmware the count of the tick is
// the guard.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "tfn/audio.h"
#if defined(ESP_PLATFORM) || defined(TFN_SIMULATOR)
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define SNDREL_LIMIT_MS 300  // the usual time limit of sndrel_wait: no counted block in this time
#define SNDREL_BLOCK_MS (AUDIO_BLOCK * 1000 / AUDIO_SAMPLE_RATE)  // the time of one block of the audio task (5 ms)

typedef struct {
    volatile int blocks;  // the release: -1 = none; > 0 = the blocks that it still counts; 0 = done
} sndrel_t;
#define SNDREL_INIT {-1}

static inline void sndrel_reset(sndrel_t *r)
{
    r->blocks = -1;
}

// The tick (the audio task): true while a release runs or is done: the tick then releases its groups and starts no
// voice.
static inline bool sndrel_tick(sndrel_t *r)
{
    const int b = r->blocks;
    if (b < 0) {
        return false;
    }
    if (b > 0) {
        r->blocks = b - 1;
    }
    return true;
}

// The app task: start a release (fade_ms: the time of the release in the tick) and wait until the tick counts its
// blocks: the fade, one block for a voice that the queue starts after the first release, and one more. true = the
// voices of the groups are silent. false = the tick counted no block in limit_ms (the sound sleeps, no sound, or the
// audio task stopped for that time: see the top of this file). Each counted block starts the time limit again, so a
// late audio task that counts on makes the wait longer.
static inline bool sndrel_wait(sndrel_t *r, int fade_ms, int limit_ms)
{
    int left = (fade_ms + SNDREL_BLOCK_MS - 1) / SNDREL_BLOCK_MS + 3;
    r->blocks = left;
#if defined(ESP_PLATFORM) || defined(TFN_SIMULATOR)
    int64_t end = esp_timer_get_time() + (int64_t)limit_ms * 1000;
    while (r->blocks > 0) {
        const int now_left = r->blocks;
        if (now_left != left) {
            left = now_left;  // (a block came: the time limit starts again)
            end = esp_timer_get_time() + (int64_t)limit_ms * 1000;
        } else if (esp_timer_get_time() >= end) {
            return false;
        }
        vTaskDelay(1);
    }
#else
    (void)limit_ms;  // (the host tests: no audio task, and no voice reads the memory)
    r->blocks = 0;
#endif
    return true;
}
