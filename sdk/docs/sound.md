# The sound effects of a game on the SD card (MP3)

A game of the store that has its data on the SD card uses real sound effects as MP3 files, not only the synthesized
voices and the MIDI music. The effects come from free libraries with the CC0 licence (public
domain). The library `libs/tfsfx` plays them.

## 1. The files

- The format: MP3, mono, 24000 Hz (the rate of the mixer, `AUDIO_SAMPLE_RATE`), 48 kbps CBR, no tags and no
  Xing/Info frame. An effect is 3 s or shorter (the library cuts a longer one).
- Make them with ffmpeg:
  ```
  ffmpeg -i in.wav -ac 1 -ar 24000 -b:a 48k -write_xing 0 -id3v2_version 0 -map_metadata -1 out.mp3
  ```
  Cut the silence at the start and keep the peaks near -1 dBFS (the same loudness in one game). The library also cuts
  a quiet start of up to 0.17 s (the delay of the MP3 coder), so a hit sounds at once.
- Keep the effects of the game in its folder: `apps/<app>/sfx/<name>.mp3` (small files: they are in git).
- On the card they go into the data folder of the app (for example `/sdcard/<pack>/sfx/`), as a part of its data pack:
  ```
  python sdk/tools/store_data.py <id> apps/<app>/sfx/crack.mp3=/<app>/sfx/crack.mp3 ...
  ```
  The keeper of the manager copies the missing files to each board that has the app and an SD card.

## 1b. Music as MP3

A game can also have its music as MP3 loops in its data pack (MP3 music gives a real orchestral sound;
space on the card is not a limit). Use a higher quality than the effects (for example stereo mixed to mono,
24000 Hz, 96-128 kbps), loops that join with no gap, and play them through a stream (`minimp3` in a task on core 0 that
fills a ring; one sample voice of the mixer plays the ring), not through `tfsfx` (it decodes each file whole into
PSRAM). Copy the stream of `apps/after_hours/ah_stream.inc` or `apps/turning_wood/tw_stream.inc`: it opens the file
again after a read fault of the card, and stops after 3 starts with no sample (a music task that spins stops the
board with the task watchdog).

## 2. The licence

- Use only effects with a clear CC0 (public domain) licence, for example the Kenney packs (kenney.nl). Keep the
  pack name and its page in `apps/<app>/sfx/SOURCES.md`, and add a line to `NOTICE.md`.
- Keep a local library of the packs that you use, with an index of each effect (its category, source pack, licence
  and length).

## 3. The library (libs/tfsfx)

In `main/apps.json`, the app entry gets `"uses": ["tfsfx"]`. The module is in the firmware of each board kind, and
its functions are ABI names (rev 22): the app file calls them, and it has no copy of the module. An app in its own
folder includes `"tfsfx/tfsfx.h"` (the SDK package has the header; [app_repo.md](app_repo.md)). A board with an older
firmware (ABI rev under 22) does not get the app.

```c
#include "tfsfx/tfsfx.h"

enum { FX_CRACK, FX_SHATTER, FX_THUD, FX_N };
static const char *const FX[FX_N] = {"crack", "shatter", "thud"};

static void enter(int64_t now) { tfsfx_load("/sdcard/mygame/sfx", FX, FX_N, 40); }  // a loader task decodes them
static void hit(float force) { tfsfx_play_var(FX_THUD, 40 + (int)(force * 60), 0.08f); }
static void leave(void) { midi_bg_stop(200); tfsfx_free(); }
```

- `tfsfx_load()` returns at once. A task decodes the files into PSRAM, one after the other (about 2 bytes of PSRAM for
  each sample: 48 KB for a second). An effect that is not ready is silent. `tfsfx_ready()` and `tfsfx_done()` tell the
  progress (a title screen can wait for them).
- `tfsfx_load_mem(data, len, names, n, group)`: the same with the MP3 data in the app file (`data[i]`,
  `len[i]` for effect i). A board can have no card in its slot (a CoreS3 often has none): an app for all boards
  keeps its effects in its file. `python libs/tfsfx/mk_sfx_c.py <sfx folder> <out path without .c> <PREFIX> name1
  name2 ...` writes `<out>.c` and `<out>.h` with `<PREFIX>_SFX_N`, `<PREFIX>_SFX_DATA[]` and `<PREFIX>_SFX_LEN[]`
  (GUMMY SIEGE has its own tool: `apps/gummy_siege/tools/mk_data.py`).
- At most `TFSFX_MAX` (96) effects. `tfsfx_ms(id)`: the length of an effect in ms (0 = not ready).
- `tfsfx_play(id, vol, rate)`: vol 0..100, rate 1.0 = the pitch of the file (0.25..4). `tfsfx_play_var(id, vol, var)`: a random
  pitch 1 +- var and a volume of 85-100 %, for hits that repeat. The same effect does not start again within 25 ms
  (a physics step can give many contacts at once).
- The mixer has 32 voices for all sounds. Keep the effects short, and limit the hits of one frame.
- `tfsfx_free()` in `leave()`: it stops the effects with a short fade (the tick of the audio task counts the blocks
  of the fade: about 45 ms; 0.3 s at most when the audio task runs no block; call it after the app stops its own
  tick and its music) and frees the memory.
- The module has its own copy of minimp3 in the firmware, with names of its own (`tfsfx_mp3dec_*`; not ABI names).
  An app with MP3 music has its own copy of minimp3 in its app file, with its own names and its own work memory
  (`apps/turning_wood/tw_stream.inc`, ON AIR). The music decoder of the quests starts after `tfsfx_done()`: the
  card reads of the effects come first.
- The close of an app file calls `tfsfx_free()` when the app did not (the firmware notes the app of the last
  `tfsfx_load()`): the loader task of the firmware never reads the names or the data of an app that went.
- A board that is muted plays no effects.

## 4. The memory of a sample voice

A sample voice (`W_SAMPLE`: an effect, the ring of a stream) reads the memory of the app while it plays, also
during its fade. Free that memory (or fill it again) only after the voice ends. A fixed wait (`vTaskDelay`) is not
enough: the audio task can be late (a flash write on the board, a busy PC in the simulator), and the next block
then reads freed memory.

- `libs/sndrel/sndrel.h` (header only; each firmware version): the tick of the app calls `sndrel_tick()` first and
  releases its groups while it gives true; the leave calls `sndrel_wait()`, then `audio_set_tick(NULL)`, then frees
  the samples. To fill a ring again while the app stays open (a new track, a new place), call `sndrel_wait()`, fill
  the ring, then `sndrel_reset()` (ECHO KEYLLER, the stream of `apps/after_hours/ah_stream.inc`, DEMO, ON AIR,
  SOUND LENS).
- The wait counts the blocks of the audio task, so an audio task that is late between its blocks makes it longer. A
  time limit (`SNDREL_LIMIT_MS`, 300 ms with no block) ends the wait when the sound sleeps, when the board has no
  sound, or when the audio task stops for that long; then `sndrel_wait()` gives false. In the last case a voice can
  still play. On a firmware with ABI rev 24 or later the free of an app file then stops that voice (below); on an
  older firmware the voice reads the freed block until its end.
- `audio_stop_mem(p, n)` (ABI rev 24) stops at once each sound that uses the memory, with no fade. On a firmware
  with rev 24 or later, the free of an app file does this stop by itself (the log of the board shows each free that
  stops a sound), and `audio_set_tick(NULL)`, `audio_capture_stop()`, `audio_mic_record_stop()` and
  `audio_mic_tap()` wait until the audio task and the mic task do not use the old tick, buffer or tap function.
- The tick must not wait: no lock, no queue, no other task, no sleep. From rev 24 the audio task holds the lock of the
  sounds while it calls the tick, and a free of an app file can wait for that lock: a tick that waits for a task in a
  free stops both tasks.
- The simulator builds the apps in, and sends the memory calls of each app that a board loads as an app file through
  the same stop (`simulator/src/sim_app_mem.c`). `python simulator/sound_check.py` checks all of this: it leaves
  ECHO KEYLLER while its sound plays, with the audio task late up to 400 ms.
