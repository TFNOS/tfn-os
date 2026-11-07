#pragma once
// libs/tfsfx: the MP3 sound effects of an app, from its data on the SD card (sdk/docs/sound.md).
// A game of the store keeps its effects as short MP3 files (mono, 24000 Hz, 48 kbps) in its data pack on the card.
// tfsfx_load() starts a task that decodes them into PSRAM, one after the other; tfsfx_play() plays one on a sample
// voice of the mixer (main/audio.h, W_SAMPLE). This module has its own copy of minimp3, with names of its own
// (tfsfx_mp3dec_*): an app with tfsfx can also have its own minimp3 for music.
// The module is in the firmware of each board kind: these functions are ABI names (ABI rev 22). An app includes this
// header only ("../../libs/tfsfx/tfsfx.h" in the repo, "tfsfx/tfsfx.h" with the SDK package) and has no copy of the
// module. The unload of an app file calls tfsfx_free() when the app did not.
#include <stdbool.h>
#include <stdint.h>

#define TFSFX_MAX 96

// Start to load the effects: dir = their folder on the card ("/sdcard/glass/sfx"); names[i] = a file name with no
// ".mp3" (i is the id of the effect; the names stay valid until tfsfx_free). A missing or bad file: that id is silent
// (the log says why). group: the mixer group of the effects (1..255; not a group of the other sounds of the app).
void tfsfx_load(const char *dir, const char *const *names, int n, uint8_t group);
// The same with the MP3 data in memory (for an app that has its effects in its own file, on the boards with
// no SD card): data[i] and len[i] = effect i (a NULL or a 0: that id is silent); the data must stay until tfsfx_free.
void tfsfx_load_mem(const uint8_t *const *data, const uint32_t *len, const char *const *names, int n, uint8_t group);
// The count of the effects that are ready; tfsfx_done(): the loader ended (all the files are read).
int tfsfx_ready(void);
bool tfsfx_done(void);
// Play effect id: vol 0..100, rate 1.0 = the pitch of the file (0.25..4). Nothing when the effect is not ready, when
// the board is muted, or when the same effect started less than 25 ms ago (many hits in one frame).
void tfsfx_play(int id, int vol, float rate);
// The same with a small random change: the pitch 1 +- var, the volume 85..100 % (a hit that repeats).
void tfsfx_play_var(int id, int vol, float var);
// The length of effect id in ms (0 = not ready).
int tfsfx_ms(int id);
// Stop the effects (a short fade), stop the loader and free the memory (the leave() of the app). When an effect can
// still sound, it uses the tick of the audio task for the fade (audio_set_tick, then NULL): it waits for the blocks of
// the fade (about 45 ms), and 0.3 s at most when the audio task does not run its blocks. Then no sound reads the
// samples (audio_stop_mem) and the memory goes. Call it after the app stops its own tick and its music (midi_bg_stop).
void tfsfx_free(void);
