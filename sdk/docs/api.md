# TFN OS API reference

> A tool makes this file. Do not change it by hand. To make it again: `python sdk/tools/gen_api_doc.py`.

**ABI version 1 rev 24: 1626 names.** An app file can call only these names. `sdk/tools/mk_app.py` stops when an app needs another name and writes a request for it ([raw.md](raw.md)). Inside one ABI version, the list only gets new names; each addition gives the next revision.

The source of each entry is the public header in `main/tfn/` and the comment above the declaration. A name that only some board kinds have shows the kinds. All 5 kinds have each other name (lcd349, cores3, am18, watcher, tab5).

The header helpers at the end (`static inline`, for example `rgb()` and `icon_time()`) are in the headers. They need no ABI name.

## Contents

- [App system](#app-system) (`app.h`)
- [Graphics](#graphics) (`gfx.h`)
- [Touch input](#touch-input) (`input.h`)
- [Board](#board) (`board.h`)
- [Motion sensor](#motion-sensor) (`imu.h`)
- [Sound](#sound) (`audio.h`)
- [Start tunes](#start-tunes) (`tunes.h`)
- [Settings and saves](#settings-and-saves) (`settings.h`)
- [Files](#files) (`storage.h`, `export.h`)
- [Fonts](#fonts) (`mfont.h`, `kfont.h`, `strokefont.h`)
- [UI kit](#ui-kit) (`uikit.h`)
- [System layer](#system-layer) (`sysui.h`)
- [Music engine](#music-engine) (`music.h`, `midi_bg.h`, `midifile.h`, `drumkit.h`, `instr.h`)
- [MIDI ports](#midi-ports) (`usb_midi.h`, `ble_midi.h`)
- [Network and radio](#network-and-radio) (`wifi.h`, `link.h`, `friends.h`, `net2.h`, `duo.h`, `duel.h`, `ha.h`)
- [Drawing modules](#drawing-modules) (`toon.h`, `scn.h`, `qr.h`)
- [Network and data modules](#network-and-data-modules) (`tfnet.h`, `tfmqtt.h`, `zimread.h`)
- [Other: alarm.h](#other-alarmh)
- [Other: bt_pad.h](#other-bt_padh)
- [Other: device_guard.h](#other-device_guardh)
- [Other: irdecode.h](#other-irdecodeh)
- [Other: kbd.h](#other-kbdh)
- [Other: notify.h](#other-notifyh)
- [Other: rf_native.h](#other-rf_nativeh)
- [Other: rfdecode.h](#other-rfdecodeh)
- [Other: rfp_board.h](#other-rfp_boardh)
- [Other: rfpulse.h](#other-rfpulseh)
- [Other: rfsrc.h](#other-rfsrch)
- [Other: sdr_chan.h](#other-sdr_chanh)
- [Other: sdr_fft.h](#other-sdr_ffth)
- [Other: sdr_spec.h](#other-sdr_spech)
- [Other: sdr_trace.h](#other-sdr_traceh)
- [Other: sdr_wf.h](#other-sdr_wfh)
- [Other: tfphys.h](#other-tfphysh)
- [Other: tfsfx.h](#other-tfsfxh)
- [Other: tz.h](#other-tzh)
- [Other: usb_audio.h](#other-usb_audioh)
- [Other: watch.h](#other-watchh)
- [Other: wifi_capture.h](#other-wifi_captureh)
- [Raw ESP-IDF: the extra names](#raw-esp-idf-the-extra-names)
- [Compiler runtime (automatic)](#compiler-runtime-automatic)
- [FreeRTOS](#freertos)
- [ESP-IDF](#esp-idf)
- [C library: memory](#c-library-memory)
- [C library: files](#c-library-files)
- [C library: text](#c-library-text)
- [C library: other](#c-library-other)
- [C library: math](#c-library-math)
- [Types and constants](#types-and-constants)
- [Header helpers](#header-helpers)

## App system

### `app.h`

#### `app_board_caps`

```c
uint8_t app_board_caps(void);
```

The needs that this board can give (APP_NEEDS_* bits).

#### `app_menu`

```c
extern const app_t app_menu;
```

The menu: an app goes back to it with app_open(&app_menu).

#### `app_open`

```c
void app_open(const app_t *app);
```

Open an app (in main.c). The switch happens before the next frame.

#### `app_launch_arg`

```c
const char *app_launch_arg(void);
```

(The comment of the declarations above applies.)

#### `app_screen_pocket`

```c
void app_screen_pocket(int ms);
```

THE POCKET MODE of an app (ON AIR: the user puts the board in a pocket and listens): ms > 0 = the screen goes off after ms with no touch or key, whatever SCREEN SLEEP says, and a movement or the music does not wake it (a pocket moves all the time); a touch or a key wakes it. 0 = the normal screen saver. An app change sets 0.

#### `app_screen_wake`

```c
void app_screen_wake(void);
```

The web page (the manager): the screen on now (as a touch), the screen state ("on", "dim", "off"), and the names of all apps (returns the count).

#### `app_screen_state`

```c
const char *app_screen_state(void);
```

(The comment of the declarations above applies.)

#### `app_standby_hold`

```c
void app_standby_hold(bool on);
```

The open app waits for a time (an alarm at a set time): on = STANDBY does not power the board off on the battery (the setting of SETUP), the light sleep stays. app_t.background still decides when update() runs (main.c asks it each 0.1 s with the screen off). A switch of the app turns it off.

#### `app_key_presses`

```c
int app_key_presses(void);
```

The BOOT key of the 3.49 for an app with app_t.key: the presses since the last call, and the key held now (0 and false on a board without the key, and in an app without app_t.key)

#### `app_key_down`

```c
bool app_key_down(void);
```

(The comment of the declarations above applies.)

## Graphics

### `gfx.h`

#### `g_fb`

```c
extern uint16_t *g_fb;
```

Frame buffer in the LOGICAL layout: gfx_w x gfx_h pixels, row after row (a landscape frame is 640 x 172), RGB565 with the bytes swapped for the SPI bus. The LCD task turns it to the physical layout while it sends it. There are two in PSRAM: the app draws into g_fb while the LCD task sends the other one.

#### `gfx_h`, `gfx_w`

```c
extern int gfx_w, gfx_h;
```

Logical size for the current orientation

#### `gfx_native`

```c
bool gfx_native(void);
```

(No comment in the header.)

#### `gfx_hi_want`

```c
void gfx_hi_want(bool on);
```

(No comment in the header.)

#### `gfx_hi`

```c
bool gfx_hi(void);
```

(No comment in the header.)

#### `fsin`

```c
float fsin(float x);
```

Fast sine and cosine from a table (for graphics and effects; about 1e-4 error).

#### `hsv`

```c
uint16_t hsv(int h, int s, int v);
```

hue 0..359, sat and val 0..255

#### `gfx_set_orient`

```c
void gfx_set_orient(orient_t o);
```

(No comment in the header.)

#### `gfx_orient`

```c
orient_t gfx_orient(void);
```

(No comment in the header.)

#### `gfx_touch_to_logical`

```c
void gfx_touch_to_logical(int px, int py, int *x, int *y);
```

Physical touch point to logical point

#### `gfx_clip_bottom`

```c
void gfx_clip_bottom(int y);
```

A bottom clip line: nothing is drawn at logical y >= y (reset: no clip).

#### `gfx_clip_reset`

```c
void gfx_clip_reset(void);
```

(The comment of the declarations above applies.)

#### `gfx_clip_box`

```c
void gfx_clip_box(int x0, int y0, int x1, int y1);
```

A clip box: nothing is drawn outside x0 <= x < x1, y0 <= y < y1. gfx_clip_reset() does not remove it (an app icon can use the bottom line inside the box of the menu).

#### `gfx_clip_box_off`

```c
void gfx_clip_box_off(void);
```

(The comment of the declarations above applies.)

#### `gfx_fill`

```c
void gfx_fill(uint16_t c);
```

(No comment in the header.)

#### `gfx_fill_rect`

```c
void gfx_fill_rect(int x, int y, int w, int h, uint16_t c);
```

(No comment in the header.)

#### `gfx_frame_rect`

```c
void gfx_frame_rect(int x, int y, int w, int h, int t, uint16_t c);
```

(No comment in the header.)

#### `gfx_darken_rect`

```c
void gfx_darken_rect(int x, int y, int w, int h, int shift);
```

Make an area darker: 0 = no change, 1 = half, 2 = quarter.

#### `gfx_fill_circle`

```c
void gfx_fill_circle(int cx, int cy, int r, uint16_t c);
```

(The comment of the declarations above applies.)

#### `gfx_fill_ellipse`

```c
void gfx_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c);
```

(The comment of the declarations above applies.)

#### `gfx_ring`

```c
void gfx_ring(int cx, int cy, int r_out, int r_in, uint16_t c);
```

(The comment of the declarations above applies.)

#### `gfx_spike`

```c
void gfx_spike(int x, int y, int w, int h, uint16_t c);
```

Filled triangle that points up: base from x to x+w at y+h, top at (x+w/2, y)

#### `gfx_text`

```c
void gfx_text(int x, int y, const char *s, int scale, uint16_t c);
```

5x7 font, 6 pixels of advance at scale 1. Letters, digits and a few signs.

#### `gfx_text_width`

```c
int gfx_text_width(const char *s, int scale);
```

(The comment of the declarations above applies.)

#### `gfx_text_center`

```c
void gfx_text_center(int y, const char *s, int scale, uint16_t c);
```

(The comment of the declarations above applies.)

#### `gfx_text_center_in`

```c
void gfx_text_center_in(int x, int w, int y, const char *s, int scale, uint16_t c);
```

Center the text in the band x .. x+w

## Touch input

### `input.h`

#### `input_tap_cancel`

```c
void input_tap_cancel(void);
```

The drag of an app wins (the tap rule is 1.8 mm): an app calls it in its input function when it starts its own drag (a scroll, a move) for this touch. Then the IN_TAP and the IN_LONG of this touch do not come out of input_get(). The next IN_DOWN clears it.

#### `input_multitouch`

```c
bool input_multitouch(void);
```

True after the panel reported two fingers at the same time. If it is false, the panel may see only one finger: a thumb that rests on the other button can hide a press.

#### `input_points`

```c
int input_points(int16_t *x, int16_t *y, int max);
```

The touch points now (physical coordinates, up to max, the panel gives 2 at most). For screens with many buttons at once (the gamepad). Returns the count.

#### `input_has_wheel`

```c
bool input_has_wheel(void);
```

the wheel (BOARD_WHEEL: the SenseCAP Watcher) An app with app_t.wheel reads these. For the other apps main.c makes the button a tap in the middle of the screen, and imu.c makes the wheel a left/right tilt. A hold of the button = back.

#### `input_wheel`

```c
int input_wheel(void);
```

(The comment of the declarations above applies.)

steps since the last call (+ = clockwise); 0 with no wheel

#### `input_wheel_presses`

```c
int input_wheel_presses(void);
```

(The comment of the declarations above applies.)

short pushes of the button since the last call

#### `input_wheel_down`

```c
bool input_wheel_down(void);
```

(The comment of the declarations above applies.)

the button is down now (after the power-on push)

## Board

### `board.h`

#### `board_backlight`

```c
void board_backlight(uint8_t level);
```

Backlight level 0..255 (0 = off).

#### `board_ohm_start`

```c
bool board_ohm_start(void);
```

The resistance meter of OHMS (the CoreS3 Port B): G9 is a high output that feeds the reference resistor, G8 reads the joint (reference from G9 to G8, the part from G8 to GND). board_ohm_start() returns false on a board with no free pins (the 3.49).

#### `board_ohm_stop`

```c
void board_ohm_stop(void);
```

(The comment of the declarations above applies.)

#### `board_ohm_read`

```c
bool board_ohm_read(int *mv);
```

The joint voltage in mV (the mean of 32 reads). Returns false when there is no reading.

#### `board_touch_log`

```c
void board_touch_log(bool on);
```

The reports of the touch chip for the test app TOUCH LAB (app_touchlab.c): each report as it came (the 3.49 driver; a board with no such log gives none: the weak versions of main/board_common.c). count = the count byte of the chip (no touch on the 3.49: 16); n = the points in the report; x, y = physical coordinates NOT clamped (a point off the panel shows a broken report); id, ev = the id and the event bits of the chip (0 down, 1 up, 2 contact).

start / stop the log (it is off at start)

#### `board_touch_reports`

```c
int board_touch_reports(touch_rep_t *out, int max);
```

(The comment of the declarations above applies.)

the reports since the last call (oldest first)

#### `board_touch_read_len_get`

```c
int board_touch_read_len_get(void);
```

The length of a touch read (the 3.49: 32 = up to 5 points, 14 = 2 points); 0 = no such setting on this board.

#### `board_sd_mount`

```c
bool board_sd_mount(void);
```

The microSD card (FAT) at BOARD_SD_ROOT. The 3.49: the TF slot, SDMMC 1-bit (CLK 41, CMD 39, D0 40). The CoreS3: the slot on the SPI bus of the LCD (CS 4, MISO 35 = the LCD DC line: a card command waits until the LCD is idle and turns GPIO35 to an input for that time). board_sd_mount() mounts the card if it is not mounted (no card: false, after about 0.2 s). board_sd_check() unmounts a card that does not answer (it was taken out); it returns the state. Safe from any task.

#### `board_sd_check`

```c
bool board_sd_check(void);
```

(The comment of the declarations above applies.)

#### `board_sd_mounted`

```c
bool board_sd_mounted(void);
```

(The comment of the declarations above applies.)

#### `board_sd_info`

```c
bool board_sd_info(uint32_t *total_kb, uint32_t *free_kb);
```

The size and the free space of the card in KB. False if there is no card.

## Motion sensor

### `imu.h`

#### `imu_ok`

```c
bool imu_ok(void);
```

(No comment in the header.)

#### `imu_gravity_phys`

```c
void imu_gravity_phys(float *gx, float *gy, float *gz);
```

Direction of gravity in units of g. Physical frame: +x = right, +y = down (portrait layout), +z = into the screen.

#### `imu_gravity_logical`

```c
void imu_gravity_logical(float *gx, float *gy);
```

Absolute gravity in the logical frame of the current orientation (+x right, +y down).

#### `imu_set_level`

```c
void imu_set_level(void);
```

Take the current board position as "level" (a virtual flat table). The Watcher (no sensor): the wheel position now.

#### `imu_tilt_logical`

```c
void imu_tilt_logical(float *tx, float *ty);
```

Tilt relative to the level position, logical frame, in g (0 when level). The Watcher: the wheel is a left/right tilt, but an app with app_t.wheel (it reads the wheel itself) always gets 0.

#### `imu_linear_logical`

```c
void imu_linear_logical(float *ax, float *ay, float *az);
```

Motion of the board without gravity, in g: logical x/y of the current orientation, z = into the screen.

#### `imu_shake`

```c
float imu_shake(void);
```

Shake level in g: the peak of the linear acceleration, it falls to a third in 0.25 s.

#### `imu_shake_ticks`

```c
void imu_shake_ticks(bool on);
```

Shake ticks: when on, the IMU task plays a short tick at the end of each stroke of a shake (the board stops hard and goes back), so down-up-down-up gives tick-tick-tick-tick. A harder stop gives a louder tick. main.c sets it to off before each app starts.

#### `imu_pop_hit`

```c
bool imu_pop_hit(imu_hit_t *h);
```

A hit: a sharp jerk of the board. x, y in the logical frame of the current orientation, z = into the screen; g = the peak size.

#### `imu_hits_clear`

```c
void imu_hits_clear(void);
```

(The comment of the declarations above applies.)

#### `imu_pop_snap`

```c
bool imu_pop_snap(imu_snap_t *s);
```

A snap: the board moves fast and then stops at once (like the flick of a towel). Loose things in the board then continue in the direction of the move. x, y: unit direction of the move in the logical frame; v: the top speed in m/s.

#### `imu_snaps_clear`

```c
void imu_snaps_clear(void);
```

(The comment of the declarations above applies.)

#### `imu_dial`

```c
float imu_dial(void);
```

The dial: the turn of the board around the screen normal, from the gyro (for example the board flat on a table, turned like the dial of a safe). In radians, + = clockwise as the user looks at the screen; it counts from the start-up and does not wrap. The gyro bias is removed while the board is still. imu_turn_rate() is the rate now (rad/s).

#### `imu_gyro_sum`

```c
void imu_gyro_sum(float a[3]);
```

The turn of the board about each physical axis since the start-up (rad), the bias removed while still.

#### `imu_steps`

```c
uint32_t imu_steps(void);
```

Steps of a person who walks with the board in the hand (a pedometer): the count since the start-up. Use the change of the count.

#### `imu_upright`

```c
orient_t imu_upright(bool landscape, orient_t current, float min_g);
```

Pick the upright orientation for a family (portrait or landscape). Returns `current` if the board is too flat to decide.

#### `imu_upright_any`

```c
orient_t imu_upright_any(orient_t current, float min_g);
```

Pick the upright orientation of the four (portrait or landscape, the menu). The family changes only when the other axis is clearly stronger. Returns `current` if the board is too flat to decide.

## Sound

### `audio.h`

#### `sfx_play`

```c
void sfx_play(sfx_t sfx);
```

Queue a sound effect. Safe to call from any task. Does nothing when muted.

#### `sfx_tone`

```c
void sfx_tone(uint16_t f0, uint16_t f1, uint16_t dur_ms, wave_t wave, uint8_t vol);
```

Queue one tone with a pitch sweep from f0 to f1 (Hz).

#### `sfx_ui`

```c
void sfx_ui(sfx_t sfx);
```

A sound of the system UI (the menu ticks, the clicks of the buttons, the back sound): a quarter of the level of sfx_play, and nothing when UI SOUNDS is off in SETUP (so a player can have game sound with no menu sounds). The games use sfx_play.

#### `audio_muted`

```c
bool audio_muted(void);
```

(No comment in the header.)

#### `audio_drone`

```c
void audio_drone(int ch, float hz, int vol, wave_t wave);
```

Continuous tone (engine, jetpack...). Safe from any task. ch 0..1, vol 0..100 (0 = off). Pitch and volume glide to the new values.

#### `audio_voice`

```c
void audio_voice(const audio_voice_t *v);
```

Only from the tick callback (audio task).

#### `audio_release_group`

```c
void audio_release_group(uint8_t group, uint16_t ms);
```

(The comment of the declarations above applies.)

#### `audio_set_tick`

```c
void audio_set_tick(void (*cb)(void));
```

Called by the audio task before each block (AUDIO_BLOCK frames). NULL = none. From another task (ABI rev 24 and later): when it returns, the old tick is not in a call and gets no more calls, so its data can go. The tick must not wait: no lock, no queue, no other task, no sleep. From ABI rev 24 the audio task calls it with the lock of the sounds, and a free of an app file can wait for that lock: a tick that waits for a task in a free stops both tasks.

#### `audio_play`

```c
void audio_play(const audio_voice_t *v);
```

Start a voice from any task (it goes through the queue; delay counts from the next block).

#### `audio_stop_mem`

```c
void audio_stop_mem(const void *p, size_t n);
```

Stop at once each sound that uses the n bytes at p: a sample voice (W_SAMPLE) that reads them, a tune or TIA steps in them, a capture or a record into them, and the messages for these sounds that wait in the queue. When it returns, no sound uses the memory, so it can be freed or filled again. Safe from any task: it waits for one block of the audio task at most, and never for a sleep or a wake of the sound. A voice that stops this way has no fade: for a soft end, release its group in the tick first. (ABI rev 24. The free of an app file does this stop by itself on a firmware with rev 24 or later.)

#### `audio_tune`

```c
void audio_tune(const tune_note_t *notes, int n);
```

Play a tune (the notes can be in any order). A new tune stops the old one. Safe from any task. Does nothing when muted. The notes must stay in memory (static const).

#### `audio_tune_stop`

```c
void audio_tune_stop(void);
```

Stop the tune (the notes that sound now end normally).

#### `audio_alert`

```c
void audio_alert(const tune_note_t *notes, int n, int vol);
```

An alarm: the tune plays also when the board is muted, at the volume vol (10..100; never softer than the volume of the user). After the tune the settings of the user come back (g_settings: the mute and the volume). Safe from any task. For an app file it needs the permission class sound.alert. The screen off: the app keeps the sound with app_t.background.

#### `audio_tia_play`

```c
void audio_tia_play(int ch, const tia_step_t *steps, int n, uint32_t skip);
```

Play the steps one time on channel ch (0 or 1). skip: the clocks to leave out at the start. A new call replaces the steps of the channel; after the last step the channel is silent. The steps must stay in memory (static const). Safe from any task. Does nothing when muted.

#### `audio_tia_loop`

```c
void audio_tia_loop(int ch, const tia_step_t *steps, int n, int times);
```

Play the steps 'times' times on channel ch. A call with the same steps while they play does not start them again: they only play on for 'times' more loops (a contact sound that a game sends in each frame has no break).

#### `audio_tia_off`

```c
void audio_tia_off(void);
```

Stop both channels at once (a pause, the end of a game).

#### `audio_mic_start`

```c
bool audio_mic_start(void);
```

microphone (ES7210, 2 mics mixed to mono, AUDIO_SAMPLE_RATE)

#### `audio_mic_stop`

```c
void audio_mic_stop(void);
```

(The comment of the declarations above applies.)

#### `audio_mic_on`

```c
bool audio_mic_on(void);
```

(The comment of the declarations above applies.)

#### `audio_mic_level`

```c
float audio_mic_level(void);
```

RMS level of the last 10 ms, 0..1 of full scale

#### `audio_mic_record`

```c
void audio_mic_record(int16_t *buf, uint32_t max);
```

Record mono samples into buf (up to max). Stops by itself when full. After audio_mic_record_stop or audio_mic_stop (ABI rev 24 and later) no sample goes into the buffer, so it can be freed.

#### `audio_mic_record_stop`

```c
void audio_mic_record_stop(void);
```

(The comment of the declarations above applies.)

#### `audio_mic_recording`

```c
bool audio_mic_recording(void);
```

(The comment of the declarations above applies.)

#### `audio_mic_recorded`

```c
uint32_t audio_mic_recorded(void);
```

(The comment of the declarations above applies.)

#### `audio_mic_tap`

```c
void audio_mic_tap(void (*cb)(const int16_t *s, int n));
```

The microphone tap: each block of mono samples (10 ms, AUDIO_SAMPLE_RATE) goes to cb, from the microphone task (keep it short). NULL = none. The microphone must be on (audio_mic_start). From ABI rev 24, when it returns, the old cb is not in a call and gets no more calls, so its data can go (not from cb itself).

#### `audio_midi_hz`

```c
float audio_midi_hz(float note);
```

MIDI note to Hz

#### `audio_load`

```c
int audio_load(void);
```

Synth time as percent of the 5 ms block (filtered), and the voices that play now.

#### `audio_capture`

```c
void audio_capture(int16_t *buf, uint32_t max, uint32_t skip);
```

Record the mixed output (mono) into buf, for export. skip: samples of the next block to leave out (from the tick callback: the delay of the first note). After audio_capture_stop (ABI rev 24 and later) no sample goes into the buffer, so it can be freed.

#### `audio_capture_stop`

```c
void audio_capture_stop(void);
```

(The comment of the declarations above applies.)

#### `audio_capturing`

```c
bool audio_capturing(void);
```

(The comment of the declarations above applies.)

#### `audio_captured`

```c
uint32_t audio_captured(void);
```

(The comment of the declarations above applies.)

## Start tunes

### `tunes.h`

#### `tune_play`

```c
void tune_play(tune_t t);
```

Play the start tune of a game. A new tune stops the old one. Safe from any task.

#### `tune_ms`

```c
int tune_ms(tune_t t);
```

The length of a tune in ms (the end of its last note).

## Settings and saves

### `settings.h`

#### `g_settings`

```c
extern settings_t g_settings;
```

(No comment in the header.)

#### `store_get`

```c
uint32_t store_get(const char *key, uint32_t def);
```

Small values in NVS (best scores and so on)

#### `store_set`

```c
void store_set(const char *key, uint32_t val);
```

(The comment of the declarations above applies.)

## Files

### `storage.h`

#### `storage_ok`

```c
bool storage_ok(void);
```

(No comment in the header.)

#### `storage_free`

```c
uint64_t storage_free(void);
```

Free space in bytes (0 if not mounted).

#### `storage_write`

```c
bool storage_write(const char *path, const void *data, size_t len);
```

Whole-file helpers. Return false on an error.

(no cut file: "<path>.tmp" first)

#### `storage_read`

```c
bool storage_read(const char *path, void *data, size_t max, size_t *len);
```

Reads up to max bytes; *len gets the size read.

### `export.h`

#### `export_next_name`

```c
void export_next_name(const char *prefix, char *out, int len);
```

Next free name, for example "/data/exports/pads_004" (no extension), in the place of SAVE TO.

#### `export_path`

```c
void export_path(const char *sub, const char *name, bool rd, char *out, int len);
```

The path <place>/<sub>/<name> (sub = "songs", "exports"). A write (rd false) makes the folder. A read (rd true) of a file that is only in the other place gives the path there (a song saved before a change of the place still loads).

#### `export_place_ask`

```c
bool export_place_ask(void);
```

true = a card is in and the user did not choose yet: the app asks SAVE TO: CARD / BOARD (sysui_ask) at the first save

#### `export_place_set`

```c
void export_place_set(bool card);
```

(The comment of the declarations above applies.)

#### `midi_begin`

```c
bool midi_begin(midi_t *m, int cap);
```

(No comment in the header.)

#### `midi_note`

```c
void midi_note(midi_t *m, uint32_t t0, uint32_t t1, int ch, int note, int vel);
```

A note from tick t0 to t1 on channel ch (0..15; 9 = drums).

#### `midi_program`

```c
void midi_program(midi_t *m, int ch, int program);
```

(The comment of the declarations above applies.)

#### `midi_end`

```c
void midi_end(midi_t *m);
```

(The comment of the declarations above applies.)

#### `export_save_async`

```c
bool export_save_async(const char *base, int16_t *samples, uint32_t n, uint32_t rate, midi_t *midi, int bpm);
```

Save base.wav (and base.mid if midi is not NULL) in a background task, because flash writes are slow. The task takes the buffers and frees them at the end. Returns false if a save runs now or there is no memory.

#### `export_progress`

```c
int export_progress(void);
```

-1 = the last save failed, 0..99 = saving now, 100 = done (or never started)

## Fonts

### `mfont.h`

#### `MF_LABEL`, `MF_N16`, `MF_N20`, `MF_N24`, `MF_N28`, `MF_N34`, `MF_SMALL`, `MF_TINY`

```c
extern const mfont_t MF_N34, MF_N28, MF_N24, MF_N20, MF_N16, MF_SMALL, MF_LABEL, MF_TINY;
```

asc: the height of the capitals over the baseline (about); line: the line height

#### `mf_glyph`

```c
const mglyph_t *mf_glyph(const mfont_t *f, int cp);
```

The simple drawer for the games. col is a gfx colour (rgb()), alpha 0..255. x is the left end, base the baseline. It writes g_fb directly: it obeys the screen edges, NOT the gfx clip box.

#### `mf_width`

```c
int mf_width(const mfont_t *f, const char *s, int spacing);
```

(The comment of the declarations above applies.)

#### `mf_text`

```c
void mf_text(const mfont_t *f, int x, int base, const char *s, uint16_t col, int alpha, int spacing);
```

(The comment of the declarations above applies.)

#### `mf_text_c`

```c
void mf_text_c(const mfont_t *f, int cx, int base, const char *s, uint16_t col, int alpha, int spacing);
```

(The comment of the declarations above applies.)

#### `mf_text_o`

```c
void mf_text_o(const mfont_t *f, int x, int base, const char *s, uint16_t col, uint16_t outline, int sh, int spacing);
```

The look of a game word: a shadow (+sh, +sh; 0 = none), a 1 px outline (4 offsets), then the colour.

#### `mf_text_oc`

```c
void mf_text_oc(const mfont_t *f, int cx, int base, const char *s, uint16_t col, uint16_t outline, int sh, int spacing);
```

(The comment of the declarations above applies.)

### `kfont.h`

#### `KF_BIG`, `KF_LABEL`, `KF_NAME`, `KF_TEXT`, `KF_TITLE`

```c
extern const kfont_t KF_LABEL, KF_TEXT, KF_NAME, KF_BIG, KF_TITLE;
```

(No comment in the header.)

### `strokefont.h`

#### `stroke_line`

```c
void stroke_line(float x0, float y0, float x1, float y1, float r, uint16_t c);
```

A line of round dots (radius r) from (x0, y0) to (x1, y1)

#### `stroke_char`

```c
void stroke_char(char ch, float x, float y, float h, float r, uint16_t c);
```

One character in a box of h px height (the width is about 0.6 h): 'A'..'G', '#', '0'..'9'. Others: nothing.

#### `stroke_text`

```c
float stroke_text(const char *s, float x, float y, float h, float r, uint16_t c);
```

A string of those characters; the advance is 0.78 h. Returns the width.

#### `stroke_width`

```c
float stroke_width(const char *s, float h);
```

(The comment of the declarations above applies.)

## UI kit

### `uikit.h`

#### `ui_mix`

```c
uint16_t ui_mix(uint16_t a, uint16_t b, int t);
```

a mix of two gfx colours (rgb()): t = 0 gives a, t = 255 gives b

#### `ui_rrect`

```c
void ui_rrect(int x, int y, int w, int h, int r, uint16_t c);
```

a filled box with round corners of radius r (soft edges)

#### `ui_rbox`

```c
void ui_rbox(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border, int t);
```

a box with a border of t px: the border colour, then the fill inside it

#### `ui_circle`

```c
void ui_circle(int cx, int cy, int r, uint16_t c, int a);
```

a filled circle (soft edge) at alpha a

#### `ui_ring`

```c
void ui_ring(int cx, int cy, int r, int t, uint16_t c, int a);
```

a ring of radius r and width t (soft edges) at alpha a

#### `ui_text_c`

```c
void ui_text_c(const kfont_t *f, int cx, int base, const char *utf8, uint16_t col);
```

Text: one line, x = the centre (ui_text_c) or the left end (ui_text); base = the baseline. A line with Hebrew is laid out from the right. ui_text_w: the width in px. ui_text_fit: the largest of the fonts (big to small) whose width of the text is w or less (the last one if none).

#### `ui_text_w`

```c
int ui_text_w(const kfont_t *f, const char *utf8);
```

(The comment of the declarations above applies.)

## System layer

### `sysui.h`

#### `sysui_ask`

```c
void sysui_ask(const char *title, const char *line, const char *yes, const char *no, int secs, void (*done)(int));
```

A question over any app (a WALKIE call): a title (UTF-8, for example a name), one line, two buttons; secs = 0: until an answer. done(1) = the first button, done(0) = the second, done(-1) = closed (sysui_ask_close, or the time). The app task only.

#### `sysui_ask_close`

```c
void sysui_ask_close(void);
```

(The comment of the declarations above applies.)

#### `sysui_active`

```c
bool sysui_active(void);
```

The layer is on the screen (a message, the locate sound or a question).

#### `sysui_panel_open`

```c
void sysui_panel_open(void);
```

the settings panel A pull down from the middle of the top edge (the zone and its guard: input.h) brings the panel down over any app: VOLUME and BRIGHTNESS (they change at once; the save comes at the close), RESUME and MENU (SETUP in the menu). On the Watcher a 600 ms hold of the wheel button also opens it (main.c). The grip (a small handle at the top middle) shows for 4 s when an app opens and after each close. While the panel shows, main.c freezes the app (no update, no render, its clock stops) or lets it run with no touches (app_t.run, a radio link). The app task only.

an app button opens the panel (it comes down in the next frames)

#### `sysui_panel_on`

```c
bool sysui_panel_on(void);
```

(The comment of the declarations above applies.)

the panel is on the screen (it moves, or it is open)

#### `sysui_panel_items`

```c
void sysui_panel_items(const pitems_t *set);
```

The app (in its enter(), or at any time): its items (NULL: none). An app change clears them (before the enter of the next app). The set must stay (a static const).

## Music engine

### `music.h`

#### `g_music`

```c
extern music_t g_music;
```

(No comment in the header.)

#### `MUSIC_ROMAN`

```c
extern const char *const MUSIC_ROMAN[MUSIC_CHORDS];
```

(No comment in the header.)

#### `MUSIC_KEYS`

```c
extern const char *const MUSIC_KEYS[12];
```

(No comment in the header.)

#### `MUSIC_ARP_NAMES`

```c
extern const char *const MUSIC_ARP_NAMES[ARP_MODES];
```

(No comment in the header.)

#### `MUSIC_BEAT_NAMES`

```c
extern const char *const MUSIC_BEAT_NAMES[BEAT_MODES];
```

(No comment in the header.)

#### `music_start`

```c
void music_start(void);
```

Connect to / disconnect from the audio task.

#### `music_stop`

```c
void music_stop(void);
```

(The comment of the declarations above applies.)

#### `music_chord_name`

```c
void music_chord_name(int chord, int color, char *buf, int len);
```

Name of a chord in the current key with a colour, for example "Am7".

#### `music_loop_bars`

```c
int music_loop_bars(void);
```

Bars in the loop: up to the last bar that has a chord (1..MUSIC_SLOTS).

#### `music_loop_samples`

```c
uint32_t music_loop_samples(void);
```

Length of one loop in samples at the current tempo.

#### `music_export`

```c
void music_export(int16_t *buf, uint32_t n, midi_t *midi);
```

Export: record the next full loop from bar 1. The audio goes into buf (n samples, normally music_loop_samples()), the notes into midi (GM channels: 0 pad, 1 arpeggio, 2 bass, 9 drums). The app must start the playback.

#### `music_export_cancel`

```c
void music_export_cancel(void);
```

(The comment of the declarations above applies.)

#### `music_export_state`

```c
int music_export_state(void);
```

(The comment of the declarations above applies.)

### `midi_bg.h`

#### `midi_bg_init`

```c
void midi_bg_init(const bg_track_t *tracks, int n, int voices);
```

Start: the tracks (the table stays in memory), and the voices that the music can use.

#### `midi_bg_start`

```c
void midi_bg_start(void);
```

Connect to the audio tick and play. stop: the held notes fade in fade_ms and the music is quiet (the tick stays connected; midi_bg_start plays again from the phase).

#### `midi_bg_stop`

```c
void midi_bg_stop(int fade_ms);
```

(The comment of the declarations above applies.)

#### `midi_bg_end`

```c
void midi_bg_end(void);
```

Stop at once (a short fade), disconnect the audio tick and free all the track memory. Call it from the app task before the app goes. It waits about 30 ms for the audio task.

#### `midi_bg_load_ms`

```c
int midi_bg_load_ms(void);
```

The time (ms) of the last track load (the inflate and the decode), for the log.

#### `midi_bg_set_phase`

```c
void midi_bg_set_phase(int phase);
```

The playlist: the tracks of this phase, one after the other. The first call starts the first track of the phase (in its full part); a later call changes the track on a phrase line.

#### `midi_bg_hold`

```c
void midi_bg_hold(bool hold);
```

Keep the track that plays now (it loops), or go on with the playlist.

#### `midi_bg_volume`

```c
void midi_bg_volume(int vol);
```

0..100: the master volume (up to 600: a quiet piece louder; each layer of a note stops at 255). duck: a lower level (0..100 of the master) for ms, then back.

#### `midi_bg_duck`

```c
void midi_bg_duck(int level, int ms);
```

THE NOTES OVER 100 %: the highest note volume since the last call, percent of what a voice (or a drum) can play. Over 100 = a note was cut (its dynamics are lost): the volume of the piece is too high (the console `snd`, tests).

#### `midi_bg_service`

```c
void midi_bg_service(void);
```

Call from the app task each frame: it loads the next track before it is needed.

#### `midi_bg_state`

```c
void midi_bg_state(midi_bg_state_t *st);
```

(No comment in the header.)

#### `midi_bg_play_track`

```c
bool midi_bg_play_track(int i, float at, bool hold);
```

Tests and a jukebox: play track i from beat `at` (in the app task; it loads the track).

### `midifile.h`

#### `midifile_load`

```c
bool midifile_load(const char *path, int melody_ch, midi_song_t *out, char *err, int errlen);
```

(No comment in the header.)

#### `midifile_free`

```c
void midifile_free(midi_song_t *s);
```

(No comment in the header.)

#### `midifile_note_name`

```c
void midifile_note_name(int pitch, char *buf, int len);
```

A note name, for example "C#4" (buf: 5 bytes or more).

#### `midifile_read_notes`

```c
bool midifile_read_notes(const char *path, midi_notes_t *out, char *err, int errlen);
```

Read all the notes of a file. On an error: false, and err has the reason.

#### `midifile_notes_free`

```c
void midifile_notes_free(midi_notes_t *m);
```

The same from a file in memory (MIDI_BG: a file that the ROM inflate made).

### `drumkit.h`

#### `DRUM_NAMES`

```c
extern const char *const DRUM_NAMES[DK_COUNT];
```

short names, 5 letters or less

#### `KIT_NAMES`

```c
extern const char *const KIT_NAMES[KIT_COUNT];
```

(No comment in the header.)

#### `drum_voice`

```c
void drum_voice(int kit, int drum, int vel, int tune, uint32_t delay);
```

vel 1..127. tune: semitones up or down (0 = normal). drum_voice: only from an audio tick callback; delay = samples from the block start.

#### `drum_play`

```c
void drum_play(int kit, int drum, int vel, int tune);
```

drum_play: from any task (through the audio queue).

#### `drum_gm_note`

```c
int drum_gm_note(int drum);
```

General MIDI note (channel 10) of a drum, for the MIDI export.

### `instr.h`

#### `instr_from_gm`

```c
int instr_from_gm(int prog, float mean_pitch);
```

A STUDIO instrument for a General MIDI program, from the program and the mean pitch of the channel (a low piano part becomes SUB BASS).

#### `instr_gm_drum`

```c
int instr_gm_drum(int note);
```

A drum of drumkit.h (DK_*) for a General MIDI drum note (channel 10).

#### `instr_gm`

```c
int instr_gm(int instr);
```

The General MIDI program of an instrument (for the MIDI export).

#### `instr_play`

```c
int instr_play(int instr, int midi, uint32_t delay, uint32_t dur, int vol, int tone, int max_layers, uint8_t group,
               bool short_perc, bool from_tick, uint32_t *len);
```

Play a note of an instrument. delay and dur in samples; vol 0..100; tone 0..100 (dark .. bright); max_layers 1..3; group: the voice group (audio_release_group). short_perc: a percussive instrument decays in its note length + 250 ms, not in its full decay (a fast part then does not hold many voices). from_tick: true in an audio tick callback (audio_voice), false from another task (audio_play). Returns the voices that it started, and *len (if not NULL) gets their length in samples. the highest layer volume of a note since the last call (percent; over 100 = a note was cut at the voice: tests)

## MIDI ports

### `usb_midi.h`

#### `usb_midi_open`

```c
void usb_midi_open(void);
```

(No comment in the header.)

#### `usb_midi_close`

```c
void usb_midi_close(void);
```

(No comment in the header.)

#### `usb_midi_state`

```c
um_state_t usb_midi_state(void);
```

(No comment in the header.)

#### `usb_midi_send`

```c
bool usb_midi_send(const uint8_t *msg, int len);
```

Send one channel message (2 or 3 bytes). Returns false if it is dropped (no link, or full).

#### `usb_midi_poll`

```c
void usb_midi_poll(void);
```

Read the messages from the computer (the MIDI clock). Call once per frame.

#### `usb_midi_sent`

```c
uint32_t usb_midi_sent(void);
```

(The comment of the declarations above applies.)

#### `usb_midi_clock_bpm`

```c
float usb_midi_clock_bpm(void);
```

The MIDI clock from the computer: the tempo in BPM, or 0 when no clock came in the last second. usb_midi_starts() counts Start and Continue messages.

#### `usb_midi_starts`

```c
uint32_t usb_midi_starts(void);
```

(The comment of the declarations above applies.)

### `ble_midi.h`

#### `ble_midi_open`

```c
void ble_midi_open(void);
```

The app opens and closes the MIDI device (start the stack, advertise as MIDI).

#### `ble_midi_close`

```c
void ble_midi_close(void);
```

(The comment of the declarations above applies.)

#### `ble_midi_state`

```c
bm_state_t ble_midi_state(void);
```

(The comment of the declarations above applies.)

#### `ble_midi_name`

```c
const char *ble_midi_name(void);
```

(The comment of the declarations above applies.)

the name that the computer shows

#### `ble_midi_peer`

```c
void ble_midi_peer(char *buf, int len);
```

(The comment of the declarations above applies.)

the address of the other side ("" if none)

#### `ble_midi_mtu` *(only: lcd349, tab5)*

```c
int ble_midi_mtu(void);
```

(The comment of the declarations above applies.)

the ATT MTU of the link (23 if none)

#### `ble_midi_disconnect`

```c
void ble_midi_disconnect(void);
```

(The comment of the declarations above applies.)

end the connection (for example a gamepad host)

#### `ble_midi_send`

```c
bool ble_midi_send(const uint8_t *msg, int len, uint32_t t_ms);
```

Queue one message (2 or 3 bytes: a channel message) with its time (ms, any clock that counts up). When the queue is nearly full, a new message takes the place of the older message of the same controller (the newest value wins). Returns false if it is dropped.

#### `ble_midi_flush`

```c
void ble_midi_flush(void);
```

Send the queued messages (call once per frame). Several messages go into one packet.

#### `ble_midi_sent`

```c
uint32_t ble_midi_sent(void);
```

(The comment of the declarations above applies.)

messages sent

#### `ble_midi_received` *(only: lcd349, tab5)*

```c
uint32_t ble_midi_received(void);
```

(The comment of the declarations above applies.)

messages received

#### `ble_midi_clock_bpm`

```c
float ble_midi_clock_bpm(void);
```

MIDI clock from the other side: the tempo in BPM, or 0 when no clock came in the last second. ble_midi_starts() counts Start and Continue messages (to align the beat).

#### `ble_midi_starts`

```c
uint32_t ble_midi_starts(void);
```

(The comment of the declarations above applies.)

## Network and radio

### `wifi.h`

#### `wifi_state`

```c
wifi_state_t wifi_state(void);
```

(No comment in the header.)

#### `wifi_ip`

```c
const char *wifi_ip(void);
```

"" if not connected

#### `wifi_hotspot_ssid`

```c
const char *wifi_hotspot_ssid(void);
```

for example "TFN-2F95"

#### `wifi_web_ip`

```c
const char *wifi_web_ip(void);
```

The address of the web page for the screens: the WiFi address, else the hotspot address, else NULL (no web page now).

### `link.h`

#### `link_start`

```c
bool link_start(void);
```

Start ESP-NOW (it starts the WiFi driver if it is off). Returns true when the link is ready. While the link is on, the WiFi power save is off, so that broadcasts come in fast.

#### `link_stop`

```c
void link_stop(void);
```

Stop ESP-NOW and put the WiFi power save back.

#### `link_send`

```c
bool link_send(uint8_t app, uint8_t kind, const void *data, int len);
```

Send a broadcast. Returns false if the link is off or the data is too long.

#### `link_recv`

```c
bool link_recv(link_msg_t *m);
```

The next received packet (no wait). Returns false if there is none.

#### `link_set_channel`

```c
bool link_set_channel(int ch);
```

The link finds the channel of the other board by itself: this call changes nothing (an app of an older firmware searched with it). Returns false if the link is off or ch is not 1..13.

#### `link_channel`

```c
int link_channel(void);
```

The WiFi channel now (0 = not known), and the MAC address of this board.

#### `link_my_mac`

```c
void link_my_mac(uint8_t mac[6]);
```

(The comment of the declarations above applies.)

### `friends.h`

#### `friends_find`

```c
int friends_find(const uint8_t mac[6]);
```

the index, or -1

#### `friends_beacon`

```c
void friends_beacon(int64_t now);
```

a lobby, each frame: sends the name beacon (each 1 s)

#### `friends_invite`

```c
void friends_invite(int64_t now);
```

a lobby that waits for a friend (2 BOARDS, an invite, an open table), each frame: the friends on the LAN and the VPN that have not got this app open get a notice "<name> WANTS TO PLAY WITH YOU" (lan.c; at most one for each friend and app in 90 s). A tap on it opens the app with the argument "friends" (app_launch_arg()).

#### `friends_hello_list`

```c
int friends_hello_list(uint8_t *out);
```

The HELLO of a lobby: the boards that this board accepts now ({n, n MACs}; n = 0 while the user must pick a friend). A board pairs only when the HELLO of the other board has its MAC. At most 8 MACs (the buffers of the app files): the friends in the game first, then the others.

returns the bytes

#### `friends_hello_ok`

```c
bool friends_hello_ok(const uint8_t *list, int len);
```

(The comment of the declarations above applies.)

#### `friends_name_of`

```c
const char *friends_name_of(const uint8_t mac[6]);
```

The name of a board for the screen: a friend's name, else "A BOARD"

### `net2.h`

#### `net2_open`

```c
void net2_open(uint8_t app_id, uint16_t info);
```

The app opens the lobby (app_id: one letter for each game; info: a number for the other board, for example the screen width) and closes the link (BYE when it is paired).

#### `net2_close`

```c
void net2_close(void);
```

(The comment of the declarations above applies.)

#### `net2_role`

```c
n2_role_t net2_role(void);
```

(The comment of the declarations above applies.)

#### `net2_poll`

```c
n2_event_t net2_poll(int64_t now, n2_rx_fn rx, n2_ev_fn ev);
```

Once per frame, before the game logic. Returns an event: paired, lost, or "the other board is in the lobby again" (it left the game: the app goes to its title or its start).

#### `net2_send_state`

```c
void net2_send_state(const void *data, int len, int64_t now);
```

HOST: the state of this frame (at most N2_STATE_MAX bytes); the events go with it.

#### `net2_send`

```c
void net2_send(uint8_t kind, const void *data, int len);
```

CLIENT: an input packet of the app (kind >= N2_K_APP).

#### `net2_local_ms`

```c
int64_t net2_local_ms(int64_t host_ms);
```

CLIENT: a time of the HOST in the local time (the same view: a state shows the HOST time when it came).

#### `net2_rec`

```c
void net2_rec(bool on);
```

HOST: the effects. While recording is on (in a game), each event goes to the CLIENT.

#### `net2_ev`

```c
void net2_ev(const uint8_t *b, int n);
```

(The comment of the declarations above applies.)

an event of the app: b[0] = its type (>= 16), n <= 24

#### `net2_sfx`

```c
void net2_sfx(sfx_t id);
```

(The comment of the declarations above applies.)

play here, and on the CLIENT

#### `net2_duo_send`

```c
void net2_duo_send(const duo_btn_t *mine);
```

The button of the other board in the 2-button games (duo.h). CLIENT: send the button of this board (each frame). HOST: put it into b (after duo_poll; b->cpu must be true, so that duo_poll does not read the touch for it).

#### `net2_duo_feed`

```c
void net2_duo_feed(duo_btn_t *b, int64_t now);
```

(The comment of the declarations above applies.)

#### `net2_draw_lobby`

```c
void net2_draw_lobby(const char *game);
```

Drawing (gfx): the lobby box and the message of a lost link (3 s).

#### `net2_lobby_tap`

```c
bool net2_lobby_tap(int x, int y);
```

A tap in the lobby: true if it chose a friend (the app then does nothing more with it).

#### `net2_lobby_accept`

```c
bool net2_lobby_accept(const uint8_t mac[6]);
```

For the own lobbies of PONG and FIGHT: may this board pair with mac now (the friend rule), and has the user a choice now (a friend, or the ADD badge of a new board; then net2_draw_lobby shows it)? Their lobbies also send the name beacon (friends_beacon).

#### `net2_lobby_choosing`

```c
bool net2_lobby_choosing(void);
```

(The comment of the declarations above applies.)

#### `net2_draw_lost`

```c
void net2_draw_lost(int64_t now);
```

(The comment of the declarations above applies.)

### `duo.h`

#### `duo_set_local`

```c
void duo_set_local(int p);
```

2 BOARDS: player p is this board, and both end buttons are its button (-1: the usual two players on one board). The other player gets its button from the radio (cpu = true).

#### `duo_reset`

```c
void duo_reset(duo_btn_t b[2], int64_t now);
```

At enter and at a new game: forget old presses. Keeps the cpu flags.

#### `duo_poll`

```c
void duo_poll(duo_btn_t b[2], int64_t now);
```

Once per frame, before the game logic.

#### `duo_cpu_press`

```c
void duo_cpu_press(duo_btn_t *b, int64_t now);
```

For a cpu player (call from update, before the game logic reads the button).

#### `duo_cpu_release`

```c
void duo_cpu_release(duo_btn_t *b, int64_t now);
```

(The comment of the declarations above applies.)

#### `duo_color`

```c
uint16_t duo_color(int player, int v);
```

Player colours: v = brightness 0..255.

#### `duo_draw_button`

```c
void duo_draw_button(int player, const duo_btn_t *b, const char *label, float glow);
```

A default end button: glow 0..1 (for example the power of a charge), label under it.

#### `duo_draw_net`

```c
void duo_draw_net(int local, const duo_btn_t *b, const char *label, float glow);
```

2 BOARDS: both end buttons of this board are the button of its player (local): both in its colour

#### `duo_multitouch`

```c
bool duo_multitouch(void);
```

True if the panel has shown that it sees two fingers at once. If not, the games tell the waiting player to lift the thumb.

### `duel.h`

#### `duel_open`

```c
void duel_open(uint8_t app);
```

the game opens (the link starts)

#### `duel_close`

```c
void duel_close(void);
```

the game closes (BYE, the link stops)

#### `duel_leave`

```c
void duel_leave(void);
```

the game closes, a standing match waits (no BYE)

#### `duel_invite`

```c
void duel_invite(void);
```

this board invites: its friends see it in their list

#### `duel_join`

```c
void duel_join(const uint8_t mac[6]);
```

join the invite of that friend

#### `duel_stop`

```c
void duel_stop(void);
```

the end of a match, or no more invite (BYE)

#### `duel_keep`

```c
void duel_keep(bool on);
```

a standing match (see above)

#### `duel_rejoin`

```c
void duel_rejoin(const uint8_t peer[6], uint32_t id);
```

back to the standing match id with that board (keep on)

#### `duel_poll`

```c
void duel_poll(int64_t now_ms);
```

each frame: the packets, the sends again, the time out

#### `duel_state`

```c
int duel_state(void);
```

DUEL_*

#### `duel_host`

```c
bool duel_host(void);
```

this board invited (in the match: it is the host)

#### `duel_peer`

```c
const uint8_t *duel_peer(void);
```

the other board

#### `duel_seed`

```c
uint32_t duel_seed(void);
```

the number of the match (the host makes it)

#### `duel_invites`

```c
int duel_invites(uint8_t mac[][6], int max);
```

the friends that invite this board now

#### `duel_send`

```c
bool duel_send(const void *data, int len);
```

false: the queue is full or the match is not on

#### `duel_recv`

```c
bool duel_recv(void *data, int *len);
```

the next message in order

#### `duel_fresh`

```c
bool duel_fresh(void);
```

true once after each time the line comes on (the app syncs then)

#### `duel_asked`

```c
bool duel_asked(uint8_t mac[6], uint32_t *id);
```

a friend wants back a match that this board has not now (once)

#### `duel_say_ended`

```c
void duel_say_ended(const uint8_t mac[6], uint32_t id, uint8_t why);
```

tell it: that match ended (why: app value)

#### `duel_ended`

```c
int duel_ended(void);
```

the standing match ended on the other board: its why + 1 (0: no)

### `ha.h`

#### `ha_items`

```c
int ha_items(ha_item_t *out, int max);
```

url: "" keeps it; token: "" keeps it. Returns false for a bad address.

a copy; returns how many

#### `ha_ready`

```c
bool ha_ready(void);
```

(The comment of the declarations above applies.)

an address and a token

#### `ha_template`

```c
int ha_template(const char *tpl, char *out, int max);
```

The calls. They return the HTTP status (200 = good) or -1 (no connection); out gets the body (cut at max - 1).

POST /api/template

#### `ha_call`

```c
int ha_call(const char *domain, const char *service, const char *entity, const char *extra_json);
```

(The comment of the declarations above applies.)

## Drawing modules

### `toon.h`

#### `T`

```c
extern tcanvas_t T;
```

(No comment in the header.)

#### `t_begin`

```c
void t_begin(tcol_t *buf, int w, int h);
```

(No comment in the header.)

#### `t_identity`

```c
void t_identity(void);
```

(No comment in the header.)

#### `t_translate`

```c
void t_translate(float x, float y);
```

(No comment in the header.)

#### `t_scale`

```c
void t_scale(float sx, float sy);
```

(No comment in the header.)

#### `t_rotate`

```c
void t_rotate(float a);
```

(No comment in the header.)

#### `t_push`

```c
void t_push(void);
```

(No comment in the header.)

#### `t_pop`

```c
void t_pop(void);
```

(No comment in the header.)

#### `t_poly`

```c
void t_poly(const float *xy, int n, tcol_t c);
```

any simple polygon (even-odd)

#### `t_rect`

```c
void t_rect(float x, float y, float w, float h, tcol_t c);
```

(No comment in the header.)

#### `t_ellipse`

```c
void t_ellipse(float cx, float cy, float rx, float ry, tcol_t c);
```

(No comment in the header.)

#### `t_capsule`

```c
void t_capsule(float ax, float ay, float bx, float by, float r1, float r2, tcol_t c);
```

(No comment in the header.)

#### `t_line`

```c
void t_line(float ax, float ay, float bx, float by, float w, tcol_t c);
```

(No comment in the header.)

#### `t_arc`

```c
void t_arc(float cx, float cy, float r, float a0, float a1, float w, tcol_t c);
```

(No comment in the header.)

#### `t_tri`

```c
void t_tri(float ax, float ay, float bx, float by, float cx, float cy, tcol_t c);
```

(No comment in the header.)

#### `t_srect`

```c
void t_srect(int x, int y, int w, int h, tcol_t c);
```

Screen-space helpers (no transform), in buffer pixels.

#### `t_text_w`

```c
int t_text_w(const char *s, int sc);
```

text (5 x 7 pixel font)

#### `t_text`

```c
void t_text(const char *s, int x, int y, int sc, trow_fn col, void *ctx, bool outline, tcol_t shadow);
```

Screen space, in buffer pixels. outline: 1 dark pixel around; shadow: a color or 0.

#### `t_text_xf`

```c
void t_text_xf(const char *s, float x, float y, float sc, trow_fn col, void *ctx, tcol_t shadow);
```

With the current transform (for rotated words). x, y = top-left in units.

#### `t_view_init`

```c
void t_view_init(tview_t *v, int W, int H, float ps);
```

camera and parallax

#### `t_layer`

```c
void t_layer(const tview_t *v, const tcam_t *cam, float p);
```

(The comment of the declarations above applies.)

p = parallax (0 far .. 1 the fight)

#### `t_proj`

```c
void t_proj(const tview_t *v, const tcam_t *cam, float p, float x, float y, float *sx, float *sy);
```

(The comment of the declarations above applies.)

#### `t_cam_clamp`

```c
void t_cam_clamp(const tview_t *v, tcam_t *cam, float half_w);
```

(The comment of the declarations above applies.)

#### `t_pose_blend`

```c
void t_pose_blend(const tpose_t *a, const tpose_t *b, float u, tpose_t *out);
```

puppets

#### `t_pose_hip`

```c
float t_pose_hip(const tpose_t *p);
```

The hip height that puts the feet on the floor for this pose (when not in the air).

#### `t_puppet_solve`

```c
void t_puppet_solve(tpuppet_t *pp, float vx, float vy, float t);
```

Make the joints. vx, vy: the speed of the hip (for the hair). t: seconds (the hair waves).

#### `t_puppet_joint`

```c
void t_puppet_joint(const tpuppet_t *pp, int j, float *wx, float *wy);
```

(The comment of the declarations above applies.)

#### `t_puppet_draw`

```c
void t_puppet_draw(const tpuppet_t *pp, float pxs);
```

(The comment of the declarations above applies.)

pxs = buffer pixels per unit now

#### `t_stage_draw`

```c
void t_stage_draw(const tview_t *v, const tcam_t *cam, const tstage_t *st);
```

stage (toon_stage.c)

#### `t_shadow`

```c
void t_shadow(float x, float h);
```

(The comment of the declarations above applies.)

#### `t_two_cores`

```c
void t_two_cores(bool on);
```

src: the low-resolution buffer (w x h); dst: the frame buffer (2w x 2h, byte-swapped). With t_two_cores(true), core 1 makes the odd rows (call it in enter, and false in leave).

#### `t_vhs_begin`

```c
void t_vhs_begin(const tcol_t *src, int w, int h, uint16_t *dst, const tvhs_t *o);
```

The same work in two parts: begin starts core 1 on the rows; draw the next frame on core 0 (into another buffer); then end makes the rows that are left and waits for core 1.

#### `t_vhs_end`

```c
void t_vhs_end(void);
```

(The comment of the declarations above applies.)

#### `t_post_clean`

```c
void t_post_clean(const tcol_t *src, int w, int h, uint16_t *dst);
```

Other looks for special moments (same buffers as t_vhs): the clean picture (each pixel 2 x 2), and black and white comic ink (halftone dots).

#### `t_post_ink`

```c
void t_post_ink(const tcol_t *src, int w, int h, uint16_t *dst);
```

(The comment of the declarations above applies.)

#### `t_post_fx`

```c
void t_post_fx(const tcol_t *src, int w, int h, uint16_t *dst, int kind, float t);
```

More looks (FIGHT: one for each fighter): 0 a speed smear, 1 old film (sepia, scratches), 2 an RGB split, 3 a heat camera, 4 black and white with the red kept, 5 cycling hues. t = seconds (for the motion).

#### `t_vhs`

```c
void t_vhs(const tcol_t *src, int w, int h, uint16_t *dst, const tvhs_t *o);
```

(The comment of the declarations above applies.)

### `scn.h`

#### `SCN_VW`

```c
extern const int SCN_VW[SCN_BOARDS], SCN_VH[SCN_BOARDS], SCN_VS[SCN_BOARDS];
```

320 x 86 (2x: the BIG PIXELS of the 3.49)

#### `sc_pack`

```c
scol sc_pack(float r, float g, float b);
```

colours

rounded and clamped, as PX2.pack

#### `sc_mix`

```c
scol sc_mix(scol a, scol b, float t);
```

(The comment of the declarations above applies.)

#### `sc_bay`

```c
float sc_bay(int x, int y);
```

sinf() and cosf() are slow library calls on the board: a fast sine (error < 0.001; a pixel position can move by one pixel where it is on the edge of a rounding)

the 4 x 4 Bayer level (0..1) of a pixel

#### `sc_h2u`

```c
uint32_t sc_h2u(int a, int b);
```

(The comment of the declarations above applies.)

#### `scn_alloc`

```c
void *scn_alloc(int bytes);
```

memory: everything a scene allocates goes to a pool (PSRAM); scn_pool_free() frees it all when the scene ends. A temporary buffer: scn_free() it at once.

zeroed; NULL when there is no memory (scn_oom is set)

#### `scn_free`

```c
void scn_free(void *p);
```

(The comment of the declarations above applies.)

#### `scn_pool_free`

```c
void scn_pool_free(void);
```

(The comment of the declarations above applies.)

#### `scn_oom`

```c
extern bool scn_oom;
```

(The comment of the declarations above applies.)

an allocation failed since the last scn_pool_free()

#### `scn_pool_bytes`

```c
extern int scn_pool_bytes, scn_pool_peak;
```

(The comment of the declarations above applies.)

the bytes in the pool now, and the most since scn_pool_free()

#### `scn_yield`

```c
void scn_yield(void);
```

a long build: call it now and then (scn_surf_new and scn_layer_make do); it sleeps 10 ms at most each 100 ms

#### `scn_surf_new`

```c
scn_surf *scn_surf_new(int w, int h);
```

surfaces

in the pool, all pixels 0; NULL = no memory

#### `scn_rect`

```c
void scn_rect(scn_surf *s, float x, float y, float w, float h, scol c);
```

(The comment of the declarations above applies.)

#### `scn_hl`

```c
void scn_hl(scn_surf *s, float x0, float x1, float y, scol c);
```

(The comment of the declarations above applies.)

#### `scn_vl`

```c
void scn_vl(scn_surf *s, float x, float y0, float y1, scol c);
```

(The comment of the declarations above applies.)

#### `scn_line`

```c
void scn_line(scn_surf *s, float x0, float y0, float x1, float y1, scol c);
```

(The comment of the declarations above applies.)

#### `scn_ell`

```c
void scn_ell(scn_surf *s, float cx, float cy, float rx, float ry, scol c);
```

(The comment of the declarations above applies.)

#### `scn_ring`

```c
void scn_ring(scn_surf *s, float cx, float cy, float r, scol c);
```

(The comment of the declarations above applies.)

#### `scn_poly`

```c
void scn_poly(scn_surf *s, const float *xy, int n, scol c);
```

(The comment of the declarations above applies.)

xy = x0, y0, x1, y1, ... (n points)

#### `scn_blit`

```c
void scn_blit(scn_surf *s, const scn_surf *src, float dx, float dy, bool flip);
```

copy a sprite (its origin is not used) with its top-left at world dx, dy; flip = mirror left-right

#### `scn_halo`

```c
void scn_halo(scn_surf *s, float cx, float cy, const float (*rings)[2], int n, scol col, float sy);
```

flat rings of light (pix.halo): rings = {radius, strength} pairs, the biggest first; sy = the y scale (0 = 1)

#### `scn_text3`

```c
int scn_text3(scn_surf *s, const char *str, float x, float y, scol c, int sc);
```

a 3 x 5 font (PX2.text3); returns the width

#### `scn_outline`

```c
void scn_outline(scn_surf *s, scol col);
```

a dark line round a sprite (on the free pixels next to it)

#### `scn_prof_us`

```c
extern int64_t scn_prof_us[SCN_PROF_N];
```

a profile of a frame (the speed work): scn_prof(i) adds the time since the last mark to slot i; the app prints the slots each 5 s

#### `scn_prof_name`

```c
extern const char *scn_prof_name[SCN_PROF_N];
```

(The comment of the declarations above applies.)

#### `scn_render`

```c
void scn_render(const scn_def_t *S, float t, float L, int board, uint16_t *band, int band_rows, uint16_t *fb, int fw,
                int fh, int fade, const uint16_t *freeze, int mix);
```

draw a frame of a scene into a frame buffer (fb, fw x fh, the LCD byte order): prep, then the bands in band (bw x bh pixels; internal RAM), each written once at scale sc (1 or 2). fade = 0..256 (256 = full light); mix > 0: the band is mixed from the frozen frame (the same size as fb) by mix / 256. board SCN_349B with a scene that has no big view: its SCN_349 camera, the middle 320 x 86 of it (build it for SCN_349)

#### `scn_render15` *(only: lcd349, tab5)*

```c
void scn_render15(const scn_def_t *S, float t, float L, int cx, int cy, uint16_t *buf, int buf_px, uint16_t *fb, int fw,
                  int fh, int fade);
```

a frame at 1.5x (the close view of STATION X on the 3.49): each 2 x 2 world pixels give 3 x 3 screen pixels, and the middle row and the middle column are the mean of their two neighbours (thus the dither of the light makes no stripes). The screen pixel (X, Y) shows the pixel (cx + X, cy + Y) of the whole world at 1.5x, so the camera moves in steps of one screen pixel. prep() is called as in scn_render, but its camera is not used. buf: buf_px pixels of work memory (internal RAM): two screen rows and the band (at least one row of fw * 2 / 3 + 4 pixels).

### `qr.h`

#### `qr_draw`

```c
int qr_draw(int x, int y, int px, const char *text);
```

QR codes on the screen (for example: join the hotspot, open the web page). Draw the code of text at (x, y): each module is px x px pixels, with a white border of 2 modules. Returns the side in pixels (0 = the text is too long). The code is kept for the same text, so a call each frame is cheap.

#### `qr_side`

```c
int qr_side(int px, const char *text);
```

The side in pixels, with no drawing (0 = the text is too long).

## Network and data modules

### `tfnet.h`

#### `tn_url_parse`

```c
bool tn_url_parse(const char *url, tn_url_t *u);
```

"http://host[:port]/path" or "https://..."; false = not a URL that tfnet can open

#### `tn_tr_open`

```c
void *tn_tr_open(const tn_url_t *u, int timeout_ms, volatile bool *cancel, int *err);
```

the transport (the socket, the TLS): tfnet.c on the board; a PC test gives its own (TN_FAKE)

#### `tn_tr_read`

```c
int tn_tr_read(void *t, void *b, int n, int timeout_ms);
```

(The comment of the declarations above applies.)

#### `tn_tr_write`

```c
int tn_tr_write(void *t, const void *b, int n);
```

(The comment of the declarations above applies.)

#### `tn_tr_close`

```c
void tn_tr_close(void *t);
```

(The comment of the declarations above applies.)

#### `tn_open`

```c
tn_conn_t *tn_open(const tn_url_t *u, int timeout_ms, volatile bool *cancel, int *err);
```

A connection to the host of u (TLS with the roots when u->tls). NULL = an error (*err = TN_E*).

#### `tn_write`

```c
int tn_write(tn_conn_t *c, const void *b, int n);
```

(The comment of the declarations above applies.)

#### `tn_read`

```c
int tn_read(tn_conn_t *c, void *b, int n, int timeout_ms);
```

Up to n bytes: > 0 = the bytes, 0 = the server closed, TN_ETIME = nothing in timeout_ms, < 0 = an error.

#### `tn_close`

```c
void tn_close(tn_conn_t *c);
```

(The comment of the declarations above applies.)

#### `tn_request`

```c
tn_conn_t *tn_request(const char *url, const char *extra, tn_head_t *h, int timeout_ms, volatile bool *cancel, int *err,
                      char *final);
```

A GET of url: the request, the head, the redirects (5 at most). extra: more lines of the request ("Icy-MetaData: 1\r\n") or NULL. The connection is then at the start of the body (NULL = an error, *err). The last URL (after the redirects) goes to final when it is not NULL (512 bytes).

#### `tn_get`

```c
int tn_get(const char *url, char *buf, int max, int timeout_ms, volatile bool *cancel, int *status);
```

The whole body of a GET into buf (a 0 at its end; a chunked body too). The length, or TN_E* (TN_EFULL: more than max - 1 bytes). *status = the HTTP status (NULL: not needed).

#### `tn_fetch`

```c
int tn_fetch(const char *method, const char *url, const char *extra, const void *body, int nbody, char *buf, int max,
             int timeout_ms, volatile bool *cancel, int *status);
```

The same with a method ("POST", "GET" ...), the extra lines of the head ("Authorization: ...\r\n"; a User-Agent line there takes the place of the one of tfnet) and a body (NULL: none; with a body: Content-Length)

#### `tn_fetch_ca`

```c
int tn_fetch_ca(const char *roots, const char *method, const char *url, const char *extra, const void *body, int nbody,
                char *buf, int max, int timeout_ms, volatile bool *cancel, int *status);
```

The same as tn_fetch, with the root certificates of the app for the TLS links (roots: as tn_url_t.roots; NULL = the shared list). The redirects use the same roots.

#### `tn_err_name`

```c
const char *tn_err_name(int err);
```

A short name of an error, for the screen (English)

#### `tn_js_root`

```c
tn_js_t tn_js_root(const char *s, int n);
```

the JSON reader It reads the text in place (no copy). A "span" is the value of a key: from its first character to the end of the value. The keys are searched only in the object that is given (not in the objects inside it).

#### `tn_js_key`

```c
tn_js_t tn_js_key(tn_js_t o, const char *key);
```

The value of key in the object o ({...}); .p = NULL when it is not there

#### `tn_js_at`

```c
tn_js_t tn_js_at(tn_js_t a, int i);
```

The element i of an array; .p = NULL when it is not there

#### `tn_js_len`

```c
int tn_js_len(tn_js_t a);
```

(The comment of the declarations above applies.)

the elements of an array

#### `tn_js_ok`

```c
bool tn_js_ok(tn_js_t v);
```

(The comment of the declarations above applies.)

a value that is there and is not null

#### `tn_js_num`

```c
double tn_js_num(tn_js_t v, double def);
```

(The comment of the declarations above applies.)

#### `tn_js_str`

```c
bool tn_js_str(tn_js_t v, char *out, int max);
```

A string value into out (UTF-8, the escapes \" \\ \/ \n \uXXXX done); false = not a string

#### `tn_js_text`

```c
bool tn_js_text(tn_js_t v, char *out, int max);
```

The same, with the line breaks (\n) kept (a text, not a name)

#### `tn_js_nums`

```c
int tn_js_nums(tn_js_t a, float *out, int max);
```

An array of numbers into out (null = NAN); the count

#### `tn_js_ints`

```c
int tn_js_ints(tn_js_t a, int64_t *out, int max);
```

The same, for whole numbers (a unix time) (null = 0)

#### `tn_js_first`

```c
tn_js_t tn_js_first(tn_js_t v);
```

The step to the next element of an array or an object: the first value inside v, and the value after it

#### `tn_js_next`

```c
tn_js_t tn_js_next(tn_js_t v, tn_js_t cur);
```

(The comment of the declarations above applies.)

### `tfmqtt.h`

#### `mq_rc_text`

```c
const char *mq_rc_text(int rc);
```

the CONNACK return codes

#### `mq_type_name`

```c
const char *mq_type_name(int type);
```

(The comment of the declarations above applies.)

#### `mq_rx_init`

```c
void mq_rx_init(mq_rx_t *r);
```

(No comment in the header.)

#### `mq_rx_feed`

```c
int mq_rx_feed(mq_rx_t *r, const void *d, int n);
```

Gives up to n bytes into the framer; returns the bytes that it took (it takes less when its buffer is full: call mq_rx_next, then give the rest again).

#### `mq_rx_next`

```c
bool mq_rx_next(mq_rx_t *r, mq_pkt_t *p);
```

The next whole packet: true and *p; false = no whole packet yet (or r->bad).

#### `mq_enc_connect`

```c
int mq_enc_connect(uint8_t *o, int max, const mq_connect_t *c);
```

================================================================ the encoders (> 0 = the bytes in o; 0 = no room)

#### `mq_enc_connack`

```c
int mq_enc_connack(uint8_t *o, int max, bool session, int rc);
```

(The comment of the declarations above applies.)

#### `mq_enc_publish`

```c
int mq_enc_publish(uint8_t *o, int max, const char *topic, const void *msg, int len, int qos, bool retain, bool dup,
                   uint16_t id);
```

(The comment of the declarations above applies.)

#### `mq_enc_ack`

```c
int mq_enc_ack(uint8_t *o, int max, int type, uint16_t id);
```

(The comment of the declarations above applies.)

PUBACK, PUBREC, PUBREL, PUBCOMP, UNSUBACK

#### `mq_enc_subscribe`

```c
int mq_enc_subscribe(uint8_t *o, int max, uint16_t id, const char *const *filters, const uint8_t *qos, int n);
```

(The comment of the declarations above applies.)

#### `mq_enc_suback`

```c
int mq_enc_suback(uint8_t *o, int max, uint16_t id, const uint8_t *codes, int n);
```

(The comment of the declarations above applies.)

#### `mq_enc_unsubscribe`

```c
int mq_enc_unsubscribe(uint8_t *o, int max, uint16_t id, const char *const *filters, int n);
```

(The comment of the declarations above applies.)

#### `mq_enc_simple`

```c
int mq_enc_simple(uint8_t *o, int max, int type);
```

(The comment of the declarations above applies.)

PINGREQ, PINGRESP, DISCONNECT

#### `mq_topic_ok`

```c
bool mq_topic_ok(const char *t);
```

================================================================ the topics

a topic of a PUBLISH: 1..MQ_TOPIC_MAX bytes, no '+' or '#'

#### `mq_filter_ok`

```c
bool mq_filter_ok(const char *f);
```

(The comment of the declarations above applies.)

a filter of a SUBSCRIBE: '+' = one whole level, '#' = the last level only

#### `mq_match`

```c
bool mq_match(const char *filter, const char *topic);
```

(The comment of the declarations above applies.)

(a filter that starts with '+' or '#' does not match "$...")

#### `mq_get_str`

```c
bool mq_get_str(const mq_pkt_t *p, int *at, char *out, int max);
```

A string of the packet: a 16-bit length, then the bytes. *at moves on. false = the packet ends first. out gets up to max-1 bytes and a 0 (a longer string is false).

#### `mq_get_u16`

```c
bool mq_get_u16(const mq_pkt_t *p, int *at, uint16_t *v);
```

(The comment of the declarations above applies.)

#### `mq_lost_text`

```c
const char *mq_lost_text(int why);
```

================================================================ the client

#### `mq_cli_init`

```c
void mq_cli_init(mq_cli_t *c);
```

(No comment in the header.)

#### `mq_cli_start`

```c
bool mq_cli_start(mq_cli_t *c, const mq_connect_t *k, int64_t now_ms);
```

The CONNECT (after the socket is up). false = the packet does not fit or the send failed.

#### `mq_cli_in`

```c
void mq_cli_in(mq_cli_t *c, const void *d, int n, int64_t now_ms);
```

The bytes from the socket.

#### `mq_cli_tick`

```c
void mq_cli_tick(mq_cli_t *c, int64_t now_ms);
```

Each 100 ms or so: the PINGREQ, the resends of QoS 1, the time limit of the CONNACK.

#### `mq_cli_publish`

```c
int mq_cli_publish(mq_cli_t *c, const char *topic, const void *msg, int len, int qos, bool retain, int64_t now_ms);
```

> 0 = the packet id (QoS 1), 0 = sent (QoS 0), < 0 = an error (-1 the topic, -2 too long, -3 full, -4 the link)

#### `mq_cli_subscribe`

```c
int mq_cli_subscribe(mq_cli_t *c, const char *filter, int qos);
```

(The comment of the declarations above applies.)

the packet id, or < 0

#### `mq_cli_unsubscribe`

```c
int mq_cli_unsubscribe(mq_cli_t *c, const char *filter);
```

(The comment of the declarations above applies.)

#### `mq_cli_stop`

```c
void mq_cli_stop(mq_cli_t *c);
```

(The comment of the declarations above applies.)

DISCONNECT (no will message), then free the state

#### `mq_cli_free`

```c
void mq_cli_free(mq_cli_t *c);
```

(The comment of the declarations above applies.)

the messages that wait (no packet)

#### `mq_gone_text`

```c
const char *mq_gone_text(int why);
```

(No comment in the header.)

#### `mq_brk_new`

```c
mq_brk_t *mq_brk_new(const mq_brk_cfg_t *cfg);
```

NULL = no memory

#### `mq_brk_free`

```c
void mq_brk_free(mq_brk_t *b);
```

(No comment in the header.)

#### `mq_brk_open`

```c
int mq_brk_open(mq_brk_t *b, int64_t now_ms);
```

A new socket: the slot (0 .. MQ_BRK_CLIENTS-1), or -1 = full (close the socket).

#### `mq_brk_in`

```c
void mq_brk_in(mq_brk_t *b, int slot, const void *d, int n, int64_t now_ms);
```

(The comment of the declarations above applies.)

#### `mq_brk_closed`

```c
void mq_brk_closed(mq_brk_t *b, int slot);
```

(The comment of the declarations above applies.)

the socket closed (or failed): the will message goes out

#### `mq_brk_tick`

```c
void mq_brk_tick(mq_brk_t *b, int64_t now_ms);
```

(The comment of the declarations above applies.)

the keep-alive limits (1.5 x) and the CONNECT time limit

#### `mq_brk_publish`

```c
int mq_brk_publish(mq_brk_t *b, const char *topic, const void *msg, int len, int qos, bool retain);
```

A message from the app itself (it is not a client): the number of subscribers that got it, or < 0.

#### `mq_brk_info`

```c
void mq_brk_info(const mq_brk_t *b, int slot, mq_brk_info_t *out);
```

(The comment of the declarations above applies.)

#### `mq_brk_retained`

```c
int mq_brk_retained(const mq_brk_t *b);
```

(The comment of the declarations above applies.)

#### `mq_brk_sub_at`

```c
int mq_brk_sub_at(const mq_brk_t *b, int slot, int i, char *out, int max);
```

(The comment of the declarations above applies.)

the filter i of a slot: its QoS, or -1

### `zimread.h`

#### `zim_open`

```c
zim_t *zim_open(const char *path, char *err, int errlen);
```

Open a ZIM file (path = name.zim; the parts name.zimaa ... are found when name.zim is not there). err gets a short English reason when the result is NULL.

#### `zim_close`

```c
void zim_close(zim_t *z);
```

(The comment of the declarations above applies.)

#### `zim_entry_count`

```c
uint32_t zim_entry_count(const zim_t *z);
```

(No comment in the header.)

#### `zim_main_page`

```c
uint32_t zim_main_page(const zim_t *z);
```

ZIM_NONE: no main page

#### `zim_mime`

```c
const char *zim_mime(const zim_t *z, uint16_t mime);
```

"" for a number that is not in the list

#### `zim_size`

```c
uint64_t zim_size(const zim_t *z);
```

(No comment in the header.)

#### `zim_parts`

```c
int zim_parts(const zim_t *z);
```

(No comment in the header.)

#### `zim_entry`

```c
bool zim_entry(zim_t *z, uint32_t idx, zim_entry_t *e);
```

One entry by its place in the URL list. false: a read fault.

#### `zim_follow`

```c
uint32_t zim_follow(zim_t *z, uint32_t idx);
```

The content entry at the end of the redirects (at most 8 steps). ZIM_NONE: a loop or a fault.

#### `zim_find_url`

```c
uint32_t zim_find_url(zim_t *z, char ns, const char *url);
```

The entry of a namespace + URL (a link of an article: "C" + "Tel_Aviv"). ZIM_NONE: not found.

#### `zim_title_count`

```c
uint32_t zim_title_count(zim_t *z);
```

THE TITLES: the count of the title listing (0: the file has none), the entry at place i of it, and the first place whose title is not less than prefix (a byte compare of UTF-8, as the listing is sorted).

#### `zim_title_entry`

```c
uint32_t zim_title_entry(zim_t *z, uint32_t i);
```

(The comment of the declarations above applies.)

#### `zim_title_lower_bound`

```c
uint32_t zim_title_lower_bound(zim_t *z, const char *prefix);
```

(The comment of the declarations above applies.)

#### `zim_blob`

```c
const uint8_t *zim_blob(zim_t *z, uint32_t cluster, uint32_t blob, uint32_t *len);
```

The content of a content entry: a pointer that stays valid until the next zim_blob() or zim_close(), and its length. NULL: a read fault, no memory, or a compression that this reader does not know (xz).

#### `zim_drop_cache`

```c
void zim_drop_cache(zim_t *z);
```

Free the decoded cluster (the app leaves, or needs the PSRAM).

#### `zim_stats`

```c
void zim_stats(const zim_t *z, zim_stats_t *s);
```

(for the tests and the log) the bytes read from the card, the clusters decoded, the time of the last decode in ms, the opens of a part file (one part is open at a time)

## Other: alarm.h

### `alarm.h`

#### `timer_get`

```c
int timer_get(ctimer_t *out, int max);
```

THE TIMERS (CLOCK > TIMER): up to TIMER_MAX count-downs. They ring as the alarms (the RING screen of CLOCK, also in another app or with the screen off); alarm_ringing() gives ALARM_MAX + the timer for a timer. They count on the clock of the board since its start (not the time of day), so they are not saved: a restart clears them.

the timers (always TIMER_MAX; a free one has state TIMER_FREE)

#### `timer_set`

```c
bool timer_set(const ctimer_t *t, int n);
```

(The comment of the declarations above applies.)

all the timers (n <= TIMER_MAX; the others turn free)

#### `alarm_get`

```c
int alarm_get(alarm_t *out, int max);
```

the alarms (the count)

#### `alarm_set`

```c
bool alarm_set(const alarm_t *a, int n);
```

all the alarms (saved at once)

#### `alarm_tz_get`

```c
void alarm_tz_get(char *out, int max);
```

the home zone, a POSIX TZ text ("IST-2IDT,M3.4.4/26,M10.5.0")

#### `alarm_tz_set`

```c
bool alarm_tz_set(const char *posix);
```

set and save the home zone (false = a bad or too long text)

#### `alarm_ringing`

```c
int alarm_ringing(void);
```

the alarm that rings now (its index), -1 = none

#### `alarm_stop`

```c
void alarm_stop(void);
```

stop (a once alarm turns off; a timer stays DONE)

#### `alarm_snooze`

```c
void alarm_snooze(void);
```

stop, and ring again after its snooze minutes (a timer: 1 minute more)

#### `alarm_next`

```c
int64_t alarm_next(int *idx);
```

the next ring (epoch s; a snooze too), 0 = none

#### `alarm_preview`

```c
void alarm_preview(int sound, int vol);
```

play a sound once (the edit page of CLOCK)

#### `clock_time_ok`

```c
bool clock_time_ok(void);
```

the board knows the time (the network or the clock chip)

#### `clock_sync`

```c
void clock_sync(void);
```

start SNTP (once; the time comes when the WiFi is up)

#### `clock_source`

```c
int clock_source(void);
```

0 none, 1 the clock chip, 2 the network

## Other: bt_pad.h

### `bt_pad.h`

#### `bt_pad_start`

```c
bool bt_pad_start(void);
```

Start the stack and advertise (from the app task). Returns false on an error. The open app holds the stack.

#### `bt_pad_state`

```c
pad_state_t bt_pad_state(void);
```

(The comment of the declarations above applies.)

#### `bt_pad_name`

```c
const char *bt_pad_name(void);
```

(The comment of the declarations above applies.)

the name the computer shows

#### `bt_pad_set_name`

```c
void bt_pad_set_name(const char *name);
```

Change the name (1..29 characters). Before the start, or at any time: then the board advertises with the new name. A computer that has the old name can keep it until it pairs again.

#### `bt_pad_sent`

```c
uint32_t bt_pad_sent(void);
```

Reports sent since the start (for the screen).

#### `bt_pad_forget`

```c
void bt_pad_forget(void);
```

Forget all paired computers (a new pairing is necessary).

#### `bt_kbd_report`

```c
void bt_kbd_report(uint8_t mods, const uint8_t keys[6]);
```

the state of the keyboard: the modifiers and up to 6 keys (HID usage IDs: a = 0x04 .. z = 0x1D, 1 = 0x1E .. 0 = 0x27, ENTER 0x28, ESC 0x29, BACKSPACE 0x2A, TAB 0x2B, SPACE 0x2C, F1 = 0x3A .. F12 = 0x45, RIGHT 0x4F, LEFT 0x50, DOWN 0x51, UP 0x52, DELETE 0x4C, HOME 0x4A, END 0x4D, PAGE UP 0x4B, PAGE DOWN 0x4E). keys NULL = no key.

#### `bt_kbd_tap`

```c
void bt_kbd_tap(uint8_t mods, uint8_t key);
```

a press and a release of one key (with the modifiers)

#### `bt_kbd_type`

```c
int bt_kbd_type(const char *text);
```

type ASCII text with the keys of the US layout (the computer uses its own layout); returns the characters queued

#### `bt_kbd_leds`

```c
uint8_t bt_kbd_leds(void);
```

the LEDs that the computer sets: NUM LOCK 1, CAPS LOCK 2, SCROLL LOCK 4

#### `bt_media_tap`

```c
void bt_media_tap(uint16_t usage);
```

a press and a release of a media key (consumer usage: PLAY/PAUSE 0xCD, NEXT 0xB5, PREVIOUS 0xB6, MUTE 0xE2, VOLUME UP 0xE9, VOLUME DOWN 0xEA)

#### `bt_mouse_move`

```c
void bt_mouse_move(int dx, int dy, int wheel);
```

the mouse: relative motion (it adds up and goes out each 8 ms), the buttons (bit 0 left, 1 right, 2 middle)

#### `bt_mouse_buttons`

```c
void bt_mouse_buttons(uint8_t buttons);
```

(The comment of the declarations above applies.)

#### `bt_pad_boot`

```c
bool bt_pad_boot(void);
```

true: the computer uses the boot protocol (a BIOS): only the keyboard and the mouse work then

## Other: device_guard.h

### `device_guard.h`

#### `device_guard_setup_allowed`

```c
bool device_guard_setup_allowed(void);
```

(No comment in the header.)

#### `device_guard_accounts_read`

```c
int device_guard_accounts_read(char *out, size_t cap);
```

-2 means an ordinary build (use the app's existing storage). Other negative values fail closed. Account text is never returned to a generic file viewer.

#### `device_guard_accounts_write`

```c
int device_guard_accounts_write(const char *text, size_t len);
```

(The comment of the declarations above applies.)

#### `device_guard_setup_allowed`

```c
static inline bool device_guard_setup_allowed(void);
```

(The comment of the declarations above applies.)

#### `device_guard_accounts_read`

```c
static inline int device_guard_accounts_read(char *out, size_t cap);
```

(The comment of the declarations above applies.)

#### `device_guard_accounts_write`

```c
static inline int device_guard_accounts_write(const char *text, size_t len);
```

(The comment of the declarations above applies.)

## Other: irdecode.h

### `irdecode.h`

#### `ird_slicer_init`

```c
void ird_slicer_init(ird_slicer_t *s, int min_pulse_us);
```

(No comment in the header.)

#### `ird_slicer_feed`

```c
void ird_slicer_feed(ird_slicer_t *s, const int32_t *d, int n, int64_t t_us);
```

The next level times; t_us = the time of the end of the last one

#### `ird_slicer_idle`

```c
void ird_slicer_idle(ird_slicer_t *s, int32_t quiet_us, int64_t t_us);
```

No edge for quiet_us and the line is a space: a frame in progress ends

#### `ird_slicer_pop`

```c
bool ird_slicer_pop(ird_slicer_t *s, ird_frame_t *out);
```

(The comment of the declarations above applies.)

#### `ird_decode`

```c
bool ird_decode(const int32_t *d, int n, bool raw_only, ird_code_t *c);
```

the decoders One frame: a known protocol, else the universal decoder, else UNKNOWN. raw_only: always UNKNOWN (the hash only). Returns false only for a frame with less than 3 level times (no code).

#### `ird_hash`

```c
uint32_t ird_hash(const int32_t *d, int n);
```

(The comment of the declarations above applies.)

#### `ird_name`

```c
const char *ird_name(int proto);
```

the text of a code

#### `ird_bit`

```c
int ird_bit(const ird_code_t *c, int i);
```

(The comment of the declarations above applies.)

#### `ird_raw_hex`

```c
void ird_raw_hex(const ird_code_t *c, char *out, int max);
```

The data as IRremote prints it (decodedRawData): the first bit on the air is bit 0 (the LSB first protocols) or the top bit (IRD_F_MSB); a code of more than 64 bits: 64-bit words, the first word first ("0x... 0x...")

#### `ird_binary`

```c
void ird_binary(const ird_code_t *c, char *out, int max);
```

The bits as 0/1 in the order on the air, in groups of 8

#### `ird_same`

```c
bool ird_same(const ird_code_t *a, const ird_code_t *b);
```

The same code: the same protocol, address, command and bits (the repeat flag and the toggle bit do not count)

#### `ird_has_fields`

```c
bool ird_has_fields(int proto);
```

A protocol with an address and a command (not the universal decoder, not UNKNOWN)

#### `ird_encode`

```c
int ird_encode(const ird_code_t *c, int32_t *out, int max);
```

the encoders (the IR LED, the demo and the tests) One frame of the code (no repeat frame, no gap after it). Returns the count of level times (0: UNKNOWN, or no room).

#### `ird_encode_press`

```c
int ird_encode_press(const ird_code_t *c, int32_t *out, int max);
```

One key press as a remote sends it: the frames of the protocol with their gaps (SONY 3 frames, DENON / SHARP the frame and the inverted frame, RC5 / RC6 the toggle bit as in c), the spaces between them as level times. Returns the count (0: UNKNOWN).

#### `ird_make`

```c
void ird_make(int proto, uint32_t address, uint32_t command, uint32_t extra, int nbits, ird_code_t *c);
```

A code of a protocol from its fields (the demo and the tests): the bits, the timing, the carrier

## Other: kbd.h

### `kbd.h`

#### `kbd_open`

```c
void kbd_open(const char *title, char *buf, int max, bool secret);
```

buf keeps the text (max - 1 characters). secret: show '*' until SHOW is on.

#### `kbd_set_ok_question`

```c
void kbd_set_ok_question(const char *q);
```

The question of OK (call it after kbd_open; the text is not copied). NULL: "USE THIS TEXT?". "": OK acts at once.

#### `kbd_set_hebrew`

```c
void kbd_set_hebrew(bool on);
```

The Hebrew letters on the SYM key (abc -> HEB -> #+=), and the keyboard opens with them (call it after kbd_open).

#### `kbd_input`

```c
int kbd_input(const input_event_t *ev);
```

Returns KBD_OK or KBD_CANCEL when the user is done (after YES on the question).

#### `kbd_wheel`

```c
int kbd_wheel(int steps, int presses);
```

The wheel (the Watcher): steps move the focus over the keys and the buttons; a push presses the key in focus. Returns as kbd_input().

#### `kbd_render`

```c
void kbd_render(int64_t now);
```

(The comment of the declarations above applies.)

#### `kbd_state`

```c
int kbd_state(void);
```

The state (for the tests and the callers): 0 = the keys, 1 = a finger on a key (its preview), 2 = the question of OK / CANCEL.

## Other: notify.h

### `notify.h`

#### `tfn_notify`

```c
uint32_t tfn_notify(const char *title, const char *text, const char *link);
```

A store app (the ABI name, the permission class "notify"): a notice of the app that runs now.

## Other: rf_native.h

### `rf_native.h`

#### `rfn_available`

```c
bool rfn_available(void);
```

true: the chip has the native radio (all ESP32-S3 kinds; not the Tab5).

#### `rfn_radio_boot`

```c
bool rfn_radio_boot(void);
```

true: this start is a radio start.

#### `rfn_restart`

```c
void rfn_restart(bool radio, const char *app);
```

Restart: radio = true: a radio start that opens the app `app` (its name; NULL = the menu); false: a normal start.

#### `rfn_open`

```c
int rfn_open(const rfn_cfg_t *cfg);
```

The receiver: power, PHY calibration, the first setup.

#### `rfn_set`

```c
int rfn_set(const rfn_cfg_t *cfg, uint32_t *lo_actual);
```

A new setup; *lo_actual (can be NULL) = the real LO (the step of the PLL is about 458 Hz).

#### `rfn_tune`

```c
int rfn_tune(uint32_t lo_hz, uint32_t *lo_actual);
```

Only a new centre (the gain and the filter stay): the fast way for a sweep (a MHz that locked once: about 0.1 ms; a new MHz: the scan of the PLL, about 26 ms).

#### `rfn_capture`

```c
int rfn_capture(int16_t *iq, int n, uint8_t *gain_idx);
```

One capture of n pairs (1..RFN_MAX_PAIRS) into iq (2 * n values). *gain_idx (can be NULL) = the gain index of the samples. Returns n, or RFN_E_*.

#### `rfn_close`

```c
void rfn_close(void);
```

The receiver off.

#### `rfn_rate_hz`

```c
uint32_t rfn_rate_hz(int rate);
```

The sample rate of a rate value (pairs a second).

#### `rfn_error`

```c
const char *rfn_error(int e);
```

(The comment of the declarations above applies.)

## Other: rfdecode.h

### `rfdecode.h`

#### `rfd_slicer_init`

```c
void rfd_slicer_init(rfd_slicer_t *s, int min_pulse_us, int gap_us);
```

(No comment in the header.)

#### `rfd_slicer_feed`

```c
void rfd_slicer_feed(rfd_slicer_t *s, const int32_t *d, int n, int64_t t_us);
```

The next level times; t_us = the time of the end of the last one (for the time of a packet)

#### `rfd_slicer_idle`

```c
void rfd_slicer_idle(rfd_slicer_t *s, int32_t quiet_us, int64_t t_us);
```

The line is low with no edge for quiet_us: a packet in progress ends (its gap_after = quiet_us)

#### `rfd_slicer_pop`

```c
bool rfd_slicer_pop(rfd_slicer_t *s, rfd_packet_t *out);
```

(The comment of the declarations above applies.)

#### `rfd_rcswitch`

```c
bool rfd_rcswitch(const rfd_packet_t *p, rfd_code_t *c);
```

the decoders

#### `rfd_generic`

```c
bool rfd_generic(const rfd_packet_t *p, int force, rfd_code_t *c);
```

force: RFD_K_NONE = the coding that fits best; else RFD_K_PWM, RFD_K_PPM, RFD_K_MANCH or RFD_K_NRZ

#### `rfd_raw`

```c
void rfd_raw(const rfd_packet_t *p, rfd_code_t *c);
```

The clusters of the times only (no bits)

#### `rfd_decode`

```c
bool rfd_decode(const rfd_packet_t *p, int mode, int coding, rfd_code_t *c);
```

One packet in a mode (RFD_MODE_*); coding: as force of rfd_generic. Returns true for a code (RAW: always).

#### `rfd_kind_name`

```c
const char *rfd_kind_name(int kind);
```

the text of a code

#### `rfd_bit`

```c
int rfd_bit(const rfd_code_t *c, int i);
```

(The comment of the declarations above applies.)

#### `rfd_hex`

```c
void rfd_hex(const rfd_code_t *c, char *out, int max);
```

The bits as hex (the first bits fill the top of the first digit when nbits is not a multiple of 4: the value)

#### `rfd_dec`

```c
void rfd_dec(const rfd_code_t *c, char *out, int max);
```

The value as a decimal number (only when nbits <= 64; else "")

#### `rfd_binary`

```c
void rfd_binary(const rfd_code_t *c, char *out, int max);
```

The bits as 0/1 in groups of 4 (at most max - 1 characters)

#### `rfd_tristate`

```c
bool rfd_tristate(const rfd_code_t *c, char *out, int max);
```

The tri-state code of the PT2262 family (each bit pair: 00 = 0, 11 = 1, 01 = F); false when a pair is 10

#### `rfd_same`

```c
bool rfd_same(const rfd_code_t *a, const rfd_code_t *b);
```

The same code (the same kind, protocol and bits)

#### `rfd_enc_rcswitch`

```c
int rfd_enc_rcswitch(int proto, uint64_t value, int nbits, int pulse_us, int32_t *out, int max);
```

the encoders (the demo and the tests) One repeat of the code with its sync (RCSWITCH) or its reset gap (the others). Returns the count of level times.

#### `rfd_enc_pwm`

```c
int rfd_enc_pwm(const uint8_t *bits, int nbits, int t_short, int t_long, int gap_us, int32_t *out, int max);
```

(The comment of the declarations above applies.)

#### `rfd_enc_ppm`

```c
int rfd_enc_ppm(const uint8_t *bits, int nbits, int pulse, int g_short, int g_long, int gap_us, int32_t *out, int max);
```

(The comment of the declarations above applies.)

#### `rfd_enc_manch`

```c
int rfd_enc_manch(const uint8_t *bits, int nbits, int t_half, int gap_us, int32_t *out, int max);
```

(The comment of the declarations above applies.)

## Other: rfp_board.h

### `rfp_board.h`

#### `rfp_board_conn`

```c
const rfp_conn_t *rfp_board_conn(void);
```

The connector of this board kind

#### `rfp_board_ir`

```c
const rfp_conn_t *rfp_board_ir(void);
```

The connector of this board kind with the pins of the infrared modules (RFP_PAD_DATA = the IR receiver, RFP_PAD_TX = the IR LED)

#### `rfp_board_uart`

```c
const rfp_conn_t *rfp_board_uart(void);
```

The connector with the pins of a serial device: RFP_PAD_RX (data_gpio), RFP_PAD_TX (tx_gpio), RFP_PAD_GND

#### `rfp_board_i2c`

```c
const rfp_i2c_t *rfp_board_i2c(int i);
```

The I2C bus i of this board kind (0 = the bus of the system); NULL after the last one

## Other: rfpulse.h

### `rfpulse.h`

#### `rfp_enum`

```c
int rfp_enum(rfp_src_t *out, int max, bool tests);
```

The receivers of this board: the one on the data pin (when the board has a free pin), then the demo. tests = true: also the receivers of the tests. Returns the count.

#### `rfp_gpio`

```c
bool rfp_gpio(rfp_src_t *out);
```

(The comment of the declarations above applies.)

rfp_gpio.c: a receiver module on the data pin of this board (rfp_board.h)

#### `rfp_demo`

```c
bool rfp_demo(rfp_src_t *out);
```

(The comment of the declarations above applies.)

rfp_demo.c: made-up remotes and sensors, and the noise of a receiver

#### `rfp_enum_ir`

```c
int rfp_enum_ir(rfp_src_t *out, int max);
```

THE INFRARED RECEIVER (the IR DECODER app): an IR receiver module (TSOP38238, VS1838B: it removes the 36..40 kHz carrier) on the IR pin of the board (rfp_board_ir()). Its output is LOW while it sees the carrier: this receiver gives the levels the other way round (> 0 = a mark), so the same contract applies. The pin has the pull-up (the output of the module is high when there is no light). The state of the line: QUIET = no light (or no module), HIGH = the mark stays on (the pin stays low: check the wires).

the IR receiver on the IR pin (when the board has one), then the demo

#### `rfp_ir`

```c
bool rfp_ir(rfp_src_t *out);
```

(The comment of the declarations above applies.)

rfp_gpio.c

#### `rfp_ir_demo`

```c
bool rfp_ir_demo(rfp_src_t *out);
```

(The comment of the declarations above applies.)

rfp_irdemo.c: made-up remote controls (the encoders of libs/irdecode)

#### `rfp_ir_send`

```c
int rfp_ir_send(const int32_t *d, int n, uint32_t carrier_hz);
```

THE INFRARED LED (rfp_irtx.c): the level times (> 0 = the carrier on, < 0 = off) on the TX pin of rfp_board_ir(), with the carrier (carrier_hz, a duty of 33 %), by the RMT peripheral. It waits for the end (at most 3 s). 0 = sent; < 0 = an error (rfp_ir_send_error)

#### `rfp_ir_send_error`

```c
const char *rfp_ir_send_error(int e);
```

(The comment of the declarations above applies.)

## Other: rfsrc.h

### `rfsrc.h`

#### `rfsrc_enum`

```c
int rfsrc_enum(rfsrc_t *out, int max, bool tests);
```

All sources of this board, the real ones first, then the demo. tests = true: also the sources of the tests (the SWEEP demo). Returns the count.

#### `rfsrc_find`

```c
bool rfsrc_find(const char *id, rfsrc_t *out);
```

The source with this id (from the list with the tests), or false.

#### `rfsrc_native`

```c
bool rfsrc_native(rfsrc_t *out);
```

The sources of this library (each file gives one; rfsrc_enum calls them)

rfsrc_native.c: the WiFi receiver of the ESP32-S3 (rf_native.h of TFN OS)

#### `rfsrc_demo`

```c
bool rfsrc_demo(rfsrc_t *out);
```

(The comment of the declarations above applies.)

rfsrc_demo.c: made-up signals (the PC tests, a board with no radio)

#### `rfsrc_demo_sweep`

```c
bool rfsrc_demo_sweep(rfsrc_t *out);
```

(The comment of the declarations above applies.)

rfsrc_demo.c: made-up signals as a SWEEP source (the test of that kind)

## Other: sdr_chan.h

### `sdr_chan.h`

#### `sdr_chplans`

```c
int sdr_chplans(const sdr_chplan_t **out, int max);
```

All plans (returns the count)

#### `sdr_band_power`

```c
float sdr_band_power(const float *db, int n, int64_t f0_hz, double df_hz, int64_t f_hz, uint32_t bw_hz);
```

The mean power (dB) of the points of a band on an axis (SDR_NODATA when the band is not on the axis)

## Other: sdr_fft.h

### `sdr_fft.h`

#### `sdr_alloc`

```c
void *sdr_alloc(size_t n);
```

The memory of sdrdsp: PSRAM on the board (the internal RAM is small in a radio start), calloc on the PC

#### `sdr_free`

```c
void sdr_free(void *p);
```

(The comment of the declarations above applies.)

#### `sdr_fft_init`

```c
bool sdr_fft_init(sdr_fft_t *f, int n);
```

n: a power of 2, 16 .. 16384. False when there is no memory.

#### `sdr_fft_free`

```c
void sdr_fft_free(sdr_fft_t *f);
```

(The comment of the declarations above applies.)

#### `sdr_fft`

```c
void sdr_fft(const sdr_fft_t *f, float *x);
```

The forward FFT in place: x = n complex values (re, im, re, im, ...)

#### `sdr_window`

```c
void sdr_window(int type, int n, float *w, float *cg, float *enbw);
```

The window of n points into w; *cg = the coherent gain (the mean of w), *enbw = the noise bandwidth in bins

## Other: sdr_spec.h

### `sdr_spec.h`

#### `sdr_plan_make`

```c
bool sdr_plan_make(const rfsrc_caps_t *c, const sdr_req_t *r, sdr_plan_t *p);
```

The plan of a request for a source. Returns false only for a source that cannot give a spectrum.

#### `sdr_plan_same_axis`

```c
bool sdr_plan_same_axis(const sdr_plan_t *a, const sdr_plan_t *b);
```

Two plans with the same axis (the same points at the same frequencies)

#### `sdr_engine_start`

```c
int sdr_engine_start(sdr_engine_t *e, const rfsrc_t *src, const sdr_plan_t *p);
```

Open the source with the first plan. Returns 0 or the error of the source.

#### `sdr_engine_frame`

```c
void sdr_engine_frame(sdr_engine_t *e, const sdr_plan_t *p, float *out, sdr_frame_t *info);
```

One frame of the plan into out (p->req.bins levels in dB).

#### `sdr_engine_stop`

```c
void sdr_engine_stop(sdr_engine_t *e);
```

(The comment of the declarations above applies.)

## Other: sdr_trace.h

### `sdr_trace.h`

#### `sdr_traces_init`

```c
bool sdr_traces_init(sdr_traces_t *t, int nmax);
```

(No comment in the header.)

#### `sdr_traces_free`

```c
void sdr_traces_free(sdr_traces_t *t);
```

(No comment in the header.)

#### `sdr_traces_reset`

```c
void sdr_traces_reset(sdr_traces_t *t);
```

All traces empty

#### `sdr_traces_axis`

```c
void sdr_traces_axis(sdr_traces_t *t, int n, int64_t f0_hz, double df_hz);
```

A new axis: n points, point i = the band f0 + i * df .. f0 + (i + 1) * df. The traces move to it.

#### `sdr_traces_push`

```c
void sdr_traces_push(sdr_traces_t *t, const float *db, float dt_s);
```

One frame (n points on the axis of the traces); dt_s = the time since the last frame (for the fall of MAX)

#### `sdr_floor`

```c
float sdr_floor(const float *db, int n);
```

The floor of the noise: the 20th percentile of the points that have data (SDR_NODATA when none)

#### `sdr_peaks`

```c
int sdr_peaks(const float *db, int n, float floor_db, float rise_db, int sep, sdr_peak_t *out, int max);
```

The highest local maxima: at least rise_db over the floor and over the lowest point between them and a higher peak (the prominence), at least sep points from each other. Returns the count (at most max), the highest first.

## Other: sdr_wf.h

### `sdr_wf.h`

#### `sdr_wf_init`

```c
bool sdr_wf_init(sdr_wf_t *h, int w, int rows);
```

(No comment in the header.)

#### `sdr_wf_free`

```c
void sdr_wf_free(sdr_wf_t *h);
```

(No comment in the header.)

#### `sdr_wf_clear`

```c
void sdr_wf_clear(sdr_wf_t *h);
```

(No comment in the header.)

#### `sdr_wf_push`

```c
void sdr_wf_push(sdr_wf_t *h, const float *db, int n, int64_t f0_hz, double df_hz, uint32_t t_ms);
```

One row (n <= w points) on the axis f0 / df. The SDR_NODATA points become 0.

#### `sdr_wf_row`

```c
const uint8_t *sdr_wf_row(const sdr_wf_t *h, int age, const sdr_wf_row_t **hdr);
```

The row of this age (0 = the newest), or NULL.

#### `sdr_persist_init`

```c
bool sdr_persist_init(sdr_persist_t *p, int w, int levels);
```

(No comment in the header.)

#### `sdr_persist_free`

```c
void sdr_persist_free(sdr_persist_t *p);
```

(No comment in the header.)

#### `sdr_persist_scale`

```c
void sdr_persist_scale(sdr_persist_t *p, float top_db, float range_db, int64_t f0_hz, double df_hz);
```

A new scale or a new axis clears the map

#### `sdr_persist_push`

```c
void sdr_persist_push(sdr_persist_t *p, const float *db, int n, int shift);
```

One frame: a fade of all cells (shift: 5 = a fade of 1/32 a frame), then +1 at each point's level

## Other: tfphys.h

### `tfphys.h`

#### `tp_world_defaults`

```c
tp_world_def_t tp_world_defaults(void);
```

the world

#### `tp_mem_size`

```c
size_t tp_mem_size(const tp_world_def_t *d);
```

(The comment of the declarations above applies.)

the bytes that tp_init needs for d

#### `tp_init`

```c
tp_world_t *tp_init(const tp_world_def_t *d);
```

(The comment of the declarations above applies.)

NULL = no memory (or a bad def)

#### `tp_free`

```c
void tp_free(tp_world_t *w);
```

(The comment of the declarations above applies.)

frees the memory (only the memory that tp_init got)

#### `tp_clear`

```c
void tp_clear(tp_world_t *w);
```

(The comment of the declarations above applies.)

removes all bodies, joints and particles (a new level)

#### `tp_step`

```c
void tp_step(tp_world_t *w);
```

(The comment of the declarations above applies.)

one fixed step of w->dt

#### `tp_body_def`

```c
tp_body_def_t tp_body_def(int kind, float x, float y, int mat);
```

the bodies

#### `tp_add_box`

```c
tp_id_t tp_add_box(tp_world_t *w, const tp_body_def_t *d, float hw, float hh);
```

(The comment of the declarations above applies.)

half width, half height

#### `tp_add_circle`

```c
tp_id_t tp_add_circle(tp_world_t *w, const tp_body_def_t *d, float r);
```

(The comment of the declarations above applies.)

#### `tp_add_poly`

```c
tp_id_t tp_add_poly(tp_world_t *w, const tp_body_def_t *d, const tp_vec_t *pts, int n);
```

a convex polygon: the convex hull of the n points (3 .. 8, round d->pos, turned by d->angle); TP_NONE = a bad shape

#### `tp_body`

```c
tp_body_t *tp_body(tp_world_t *w, tp_id_t id);
```

(The comment of the declarations above applies.)

NULL = the body went away

#### `tp_body_id`

```c
tp_id_t tp_body_id(const tp_world_t *w, int slot);
```

(The comment of the declarations above applies.)

the id of a live slot (TP_NONE for a free slot)

#### `tp_remove`

```c
void tp_remove(tp_world_t *w, tp_id_t id);
```

(The comment of the declarations above applies.)

removes the body and its joints now (no event)

#### `tp_wake`

```c
void tp_wake(tp_world_t *w, tp_id_t id);
```

(The comment of the declarations above applies.)

#### `tp_set_vel`

```c
void tp_set_vel(tp_world_t *w, tp_id_t id, tp_vec_t v, float spin);
```

(The comment of the declarations above applies.)

wakes the body

#### `tp_impulse`

```c
void tp_impulse(tp_world_t *w, tp_id_t id, tp_vec_t imp, tp_vec_t point);
```

(The comment of the declarations above applies.)

N s at a world point; wakes it

#### `tp_force`

```c
void tp_force(tp_world_t *w, tp_id_t id, tp_vec_t f, tp_vec_t point);
```

(The comment of the declarations above applies.)

N at a world point, for the next step

#### `tp_set_place`

```c
void tp_set_place(tp_world_t *w, tp_id_t id, tp_vec_t pos, float angle);
```

(The comment of the declarations above applies.)

a jump to a new place (the speed stays)

#### `tp_move_to`

```c
void tp_move_to(tp_world_t *w, tp_id_t id, tp_vec_t pos, float angle);
```

a kinematic body (a crane, a platform): it moves to pos, angle in the next step (the speed for it; it pushes the dynamic bodies). Call it in each frame for a smooth path.

#### `tp_break`

```c
bool tp_break(tp_world_t *w, tp_id_t id, tp_vec_t p, tp_vec_t dir);
```

break the body now at the world point p (dir: the direction of the hit, into the body; 0, 0 = from p to the centre); false = it cannot break (no material pattern, no place for the pieces)

#### `tp_angle`

```c
float tp_angle(const tp_body_t *b);
```

(The comment of the declarations above applies.)

the angle of the body (rad)

#### `tp_verts`

```c
int tp_verts(const tp_body_t *b, tp_vec_t *out);
```

(The comment of the declarations above applies.)

the corners in the world (TP_POLY; returns n)

#### `tp_world_point`

```c
tp_vec_t tp_world_point(const tp_body_t *b, tp_vec_t local);
```

(The comment of the declarations above applies.)

a local point (from the centre) in the world

#### `tp_local_point`

```c
tp_vec_t tp_local_point(const tp_body_t *b, tp_vec_t world);
```

(The comment of the declarations above applies.)

#### `tp_point_vel`

```c
tp_vec_t tp_point_vel(const tp_body_t *b, tp_vec_t world);
```

(The comment of the declarations above applies.)

the speed of the body at a world point

#### `tp_revolute_def`

```c
tp_joint_def_t tp_revolute_def(tp_id_t a, tp_id_t b, tp_vec_t anchor);
```

the joints

#### `tp_weld_def`

```c
tp_joint_def_t tp_weld_def(tp_id_t a, tp_id_t b, tp_vec_t anchor);
```

(The comment of the declarations above applies.)

#### `tp_distance_def`

```c
tp_joint_def_t tp_distance_def(tp_id_t a, tp_id_t b, tp_vec_t pa, tp_vec_t pb);
```

(The comment of the declarations above applies.)

rigid at the length now

#### `tp_rope_def`

```c
tp_joint_def_t tp_rope_def(tp_id_t a, tp_id_t b, tp_vec_t pa, tp_vec_t pb, float len);
```

(The comment of the declarations above applies.)

slack below len

#### `tp_add_joint`

```c
tp_id_t tp_add_joint(tp_world_t *w, const tp_joint_def_t *d);
```

(The comment of the declarations above applies.)

TP_NONE = no place or a bad def

#### `tp_joint`

```c
tp_joint_t *tp_joint(tp_world_t *w, tp_id_t id);
```

(The comment of the declarations above applies.)

NULL = the joint went away

#### `tp_joint_remove`

```c
void tp_joint_remove(tp_world_t *w, tp_id_t id);
```

(The comment of the declarations above applies.)

(no event)

#### `tp_motor`

```c
void tp_motor(tp_world_t *w, tp_id_t id, float speed, float torque);
```

(The comment of the declarations above applies.)

a revolute motor (speed rad/s); wakes it

#### `tp_joint_anchors`

```c
void tp_joint_anchors(const tp_world_t *w, const tp_joint_t *j, tp_vec_t *pa, tp_vec_t *pb);
```

(The comment of the declarations above applies.)

in the world

#### `tp_emit`

```c
tp_part_t *tp_emit(tp_world_t *w, int kind, tp_vec_t p, tp_vec_t v);
```

the particles (no forces between them)

NULL = full

#### `tp_spill`

```c
int tp_spill(tp_world_t *w, tp_id_t id, int n, int kind);
```

n particles at random points in the shape of the body, with its speed (the water of a tank spills)

#### `tp_ray`

```c
bool tp_ray(tp_world_t *w, tp_vec_t p0, tp_vec_t p1, uint16_t mask, tp_ray_t *out);
```

the first body on the line from p0 to p1 whose cat has a bit of mask (0xFFFF = all); false = none

#### `tp_point`

```c
tp_id_t tp_point(tp_world_t *w, tp_vec_t p, float r, uint16_t mask);
```

the body under a point (a finger), at most r from it: a dynamic or kinematic body first, then a static one

#### `tp_query_box`

```c
int tp_query_box(tp_world_t *w, tp_box_t box, uint16_t mask, tp_id_t *out, int max);
```

the bodies whose boxes touch the box: at most max ids into out; returns the count

## Other: tfsfx.h

### `tfsfx.h`

#### `tfsfx_load`

```c
void tfsfx_load(const char *dir, const char *const *names, int n, uint8_t group);
```

Start to load the effects: dir = their folder on the card ("/sdcard/glass/sfx"); names[i] = a file name with no ".mp3" (i is the id of the effect; the names stay valid until tfsfx_free). A missing or bad file: that id is silent (the log says why). group: the mixer group of the effects (1..255; not a group of the other sounds of the app).

#### `tfsfx_load_mem`

```c
void tfsfx_load_mem(const uint8_t *const *data, const uint32_t *len, const char *const *names, int n, uint8_t group);
```

The same with the MP3 data in memory (for an app that has its effects in its own file, on the boards with no SD card): data[i] and len[i] = effect i (a NULL or a 0: that id is silent); the data must stay until tfsfx_free.

#### `tfsfx_ready`

```c
int tfsfx_ready(void);
```

The count of the effects that are ready; tfsfx_done(): the loader ended (all the files are read).

#### `tfsfx_done`

```c
bool tfsfx_done(void);
```

(The comment of the declarations above applies.)

#### `tfsfx_play`

```c
void tfsfx_play(int id, int vol, float rate);
```

Play effect id: vol 0..100, rate 1.0 = the pitch of the file (0.25..4). Nothing when the effect is not ready, when the board is muted, or when the same effect started less than 25 ms ago (many hits in one frame).

#### `tfsfx_play_var`

```c
void tfsfx_play_var(int id, int vol, float var);
```

The same with a small random change: the pitch 1 +- var, the volume 85..100 % (a hit that repeats).

#### `tfsfx_ms`

```c
int tfsfx_ms(int id);
```

The length of effect id in ms (0 = not ready).

#### `tfsfx_free`

```c
void tfsfx_free(void);
```

Stop the effects (a short fade), stop the loader and free the memory (the leave() of the app). When an effect can still sound, it uses the tick of the audio task for the fade (audio_set_tick, then NULL): it waits for the blocks of the fade (about 45 ms), and 0.3 s at most when the audio task does not run its blocks. Then no sound reads the samples (audio_stop_mem) and the memory goes. Call it after the app stops its own tick and its music (midi_bg_stop).

## Other: tz.h

### `tz.h`

#### `tz_split`

```c
void tz_split(int64_t t, tz_tm_t *o);
```

a time (seconds, already moved to a zone) as its parts

#### `tz_parse`

```c
bool tz_parse(const char *s, tz_rule_t *z);
```

A local zone from a POSIX TZ text. Only the "Mm.w.d[/time]" rules (the rules of nearly all zones).

#### `tz_offset`

```c
int tz_offset(const tz_rule_t *z, int64_t utc, const char **name);
```

the offset east of UTC (seconds) at this time; *name = the short name of the zone then (NULL: not needed)

## Other: usb_audio.h

### `usb_audio.h`

#### `usb_audio_open`

```c
void usb_audio_open(int src);
```

(No comment in the header.)

#### `usb_audio_close`

```c
void usb_audio_close(void);
```

(No comment in the header.)

#### `usb_audio_set_src`

```c
void usb_audio_set_src(int src);
```

(No comment in the header.)

#### `usb_audio_src`

```c
int usb_audio_src(void);
```

(No comment in the header.)

#### `usb_audio_state`

```c
ua_state_t usb_audio_state(void);
```

(No comment in the header.)

#### `usb_audio_streaming`

```c
bool usb_audio_streaming(void);
```

true while the computer reads the sound (an app records, or "Listen to this device")

#### `usb_audio_level`

```c
float usb_audio_level(void);
```

the level of the sound that goes out (0 .. 1, the RMS of the last 10 ms)

#### `usb_audio_sent`

```c
uint32_t usb_audio_sent(void);
```

the samples that went out (48 kHz), and the samples that the buffer dropped (the computer did not read them)

#### `usb_audio_dropped`

```c
uint32_t usb_audio_dropped(void);
```

(The comment of the declarations above applies.)

#### `usb_audio_setting`

```c
bool usb_audio_setting(void);
```

true: the mode is 1 (a computer)

## Other: watch.h

### `watch.h`

#### `tfn_secret_get`

```c
int tfn_secret_get(const char *key, char *out, int max);
```

THE SECRET STORE (the permission class "secret", KEEPS A SECRET KEY ON THE BOARD): a key, a token or a password of the app, for the app and for its watcher. Each app reads only its own values (the NVS namespace "tfnsec": the console storeall does not show it). Up to 400 bytes a value. A full flash read by USB can still get it (no encryption).

the length of the value, -1 = no value

#### `tfn_secret_set`

```c
bool tfn_secret_set(const char *key, const char *value);
```

(The comment of the declarations above applies.)

NULL or "" = the value goes

## Other: wifi_capture.h

### `wifi_capture.h`

#### `wifi_capture_begin`

```c
int32_t wifi_capture_begin(const wifi_capture_config_t *config, wifi_capture_rx_cb_t callback, void *context);
```

Frame bytes exclude FCS; valid only during callback. Callback runs in the Wi-Fi task: copy to a bounded queue, never block/allocate/write files here.

#### `wifi_capture_end`

```c
int32_t wifi_capture_end(void);
```

Idempotent. Drains callbacks before returning; restores normal Wi-Fi intent.

#### `wifi_capture_deauth`

```c
int32_t wifi_capture_deauth(const uint8_t client[6]);
```

One request, one selected unicast client; no broadcast. Minimum 1 s spacing. Returns local TX result only, never a guarantee of over-air delivery/reconnect.

## Raw ESP-IDF: the extra names

ESP-IDF functions that no built-in app calls. They come from `main/tfn_abi_extra.json` (see [raw.md](raw.md)). Their declarations are in the ESP-IDF headers (for example `esp_wifi.h`). A name with a permission class links only for an app whose manifest has the class: `mk_app.py` writes it, and the STORE shows its text before the install. An app that calls a name of revision N runs only on a firmware with ABI rev N or newer.

| Name | Permission class | Rev | Why |
| --- | --- | --- | --- |
| `ZSTD_DCtx_reset` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_DCtx_setParameter` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_DStreamInSize` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_DStreamOutSize` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_createDCtx` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_createDStream` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_decompress` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_decompressDCtx` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_decompressStream` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_findFrameCompressedSize` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_freeDCtx` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_freeDStream` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_getErrorName` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_getFrameContentSize` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_initDStream` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_isError` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `ZSTD_versionNumber` | none | 22 | the module zstd (third_party/zstd/zstd.h, Zstandard 1.5.6): the stable decoder API (zimread uses it) |
| `__ledf2` | none | 11 | the compare of two doubles (a <= b): the code of gcc for any app with double math |
| `_uart_set_pin6` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `ceil` | none | 11 | the math of the C library (double): the SDR app |
| `esp_wifi_clear_ap_list` | `wifi.scan` (SCAN THE WIFI NETWORKS) | 1 | the WIFI SCAN example: frees the list of a scan that the app does not read |
| `esp_wifi_get_channel` | none | 1 | the WIFI SCAN example: the channel of the board now (only a read) |
| `esp_wifi_scan_get_ap_num` | `wifi.scan` (SCAN THE WIFI NETWORKS) | 1 | the WIFI SCAN example: the number of networks of the last scan |
| `esp_wifi_scan_get_ap_records` | `wifi.scan` (SCAN THE WIFI NETWORKS) | 1 | the WIFI SCAN example: the networks of the last scan |
| `esp_wifi_scan_start` | `wifi.scan` (SCAN THE WIFI NETWORKS) | 1 | the WIFI SCAN example: a scan of all channels or of one channel |
| `esp_wifi_scan_stop` | `wifi.scan` (SCAN THE WIFI NETWORKS) | 1 | the WIFI SCAN example: stops a scan in leave() |
| `esp_wifi_sta_get_ap_info` | `wifi.scan` (SCAN THE WIFI NETWORKS) | 1 | the WIFI SCAN example: the network that the board uses now |
| `exp` | none | 11 | the math of the C library (double) |
| `fabs` | none | 11 | the math of the C library (double): the SDR app |
| `i2c_del_master_bus` | `bus` (USE THE I2C AND SPI BUSES OF THE BOARD) | 15 | the I2C master driver of ESP-IDF: the I2C SCANNER app (a probe of each address; the bus of the system by its handle, or a bus of its own on a free port) |
| `i2c_master_get_bus_handle` | `bus` (USE THE I2C AND SPI BUSES OF THE BOARD) | 15 | the I2C master driver of ESP-IDF: the I2C SCANNER app (a probe of each address; the bus of the system by its handle, or a bus of its own on a free port) |
| `i2c_master_probe` | `bus` (USE THE I2C AND SPI BUSES OF THE BOARD) | 15 | the I2C master driver of ESP-IDF: the I2C SCANNER app (a probe of each address; the bus of the system by its handle, or a bus of its own on a free port) |
| `i2c_new_master_bus` | `bus` (USE THE I2C AND SPI BUSES OF THE BOARD) | 2 | the I2C master driver of ESP-IDF: the I2C SCANNER app (a probe of each address; the bus of the system by its handle, or a bus of its own on a free port) |
| `llround` | none | 11 | the math of the C library (double): the SDR app (the frequencies in Hz) |
| `log10` | none | 11 | the math of the C library (double): the SDR app (the frequency marks) |
| `lround` | none | 11 | the math of the C library (double): the SDR app (the points of the frequencies) |
| `pow` | none | 11 | the math of the C library (double): the SDR app (the frequency marks) |
| `uart_driver_delete` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_driver_install` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_flush_input` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_get_baudrate` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_get_buffered_data_len` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_is_driver_installed` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_param_config` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_read_bytes` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_set_baudrate` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_set_line_inverse` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_set_loop_back` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_set_parity` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_set_rx_timeout` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_set_stop_bits` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_set_word_length` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_wait_tx_done` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |
| `uart_write_bytes` | `gpio` (USE THE PINS OF THE BOARD) | 14 | the UART driver of ESP-IDF: the SERIAL MONITOR app (a serial device on the free pins; the app deletes the driver in leave) |

## Compiler runtime (automatic)

The compiler calls these for `double` and 64-bit arithmetic. Do not call them yourself. Use `float` where you can: the ESP32-S3 has a hardware `float` unit but no `double` unit.

- `__adddf3`
- `__divdf3`
- `__divdi3`
- `__divsf3`
- `__extendsfdf2`
- `__fixdfdi`
- `__fixdfsi`
- `__fixsfdi`
- `__fixunsdfsi`
- `__floatdidf`
- `__floatdisf`
- `__floatsidf`
- `__floatundisf`
- `__floatunsidf`
- `__gedf2`
- `__gtdf2`
- `__ltdf2`
- `__moddi3`
- `__muldf3`
- `__powisf2`
- `__subdf3`
- `__truncdfsf2`
- `__udivdi3`

## FreeRTOS

The task and queue functions of FreeRTOS. A task of an app must end before the app closes (see `behave.md`, "Tasks").

- `pvTaskGetThreadLocalStoragePointer`
- `uxQueueMessagesWaiting`
- `uxQueueSpacesAvailable`
- `uxTaskGetNumberOfTasks` *(only: am175)*
- `uxTaskGetStackHighWaterMark`
- `uxTaskGetSystemState` *(only: am175)*
- `uxTaskPriorityGet`
- `vPortExitCritical`
- `vPortExitCriticalMultiCore` *(only: tab5)*
- `vQueueDelete`
- `vQueueDeleteWithCaps`
- `vSemaphoreDeleteWithCaps`
- `vTaskDelay`
- `vTaskDelete`: the system notes the tasks of an app file; while one runs, the code of the app stays in memory
- `vTaskDeleteWithCaps`: the system notes the tasks of an app file; while one runs, the code of the app stays in memory
- `vTaskGetInfo` *(only: am175)*
- `vTaskSetTimeOutState` *(only: am175, am18, cores3, lcd349, tab5)*
- `vTaskSuspendAll`
- `xPortEnterCriticalTimeout`
- `xQueueCreateCountingSemaphore`
- `xQueueCreateCountingSemaphoreStatic`
- `xQueueCreateMutex`
- `xQueueCreateMutexStatic`
- `xQueueCreateWithCaps`
- `xQueueGenericCreate`
- `xQueueGenericCreateStatic`
- `xQueueGenericGetStaticBuffers`
- `xQueueGenericReset`
- `xQueueGenericSend`
- `xQueueGetMutexHolder`
- `xQueueGiveMutexRecursive`
- `xQueueReceive`
- `xQueueSemaphoreTake`
- `xQueueTakeMutexRecursive`
- `xSemaphoreCreateGenericWithCaps`
- `xTaskCheckForTimeOut`
- `xTaskCreatePinnedToCore`: the system notes the tasks of an app file; while one runs, the code of the app stays in memory
- `xTaskCreatePinnedToCoreWithCaps`: the system notes the tasks of an app file; while one runs, the code of the app stays in memory
- `xTaskDelayUntil`
- `xTaskGenericNotify`
- `xTaskGetCoreID`
- `xTaskGetCurrentTaskHandle`
- `xTaskGetCurrentTaskHandleForCore`
- `xTaskGetSchedulerState`
- `xTaskGetStaticBuffers`
- `xTaskGetTickCount`
- `xTaskResumeAll`

## ESP-IDF

The ESP-IDF functions that apps can call (time, log, random numbers, memory, the web server of the board, the JPEG decoder and the inflate of the ROM).

- `esp_efuse_mac_get_default`
- `esp_err_to_name`
- `esp_event_handler_register`
- `esp_event_handler_unregister`
- `esp_fill_random`
- `esp_get_free_heap_size`
- `esp_http_client_add_auth`
- `esp_http_client_cleanup`
- `esp_http_client_close`
- `esp_http_client_fetch_headers`
- `esp_http_client_get_content_length`
- `esp_http_client_get_status_code`
- `esp_http_client_init`
- `esp_http_client_perform`
- `esp_http_client_prepare`
- `esp_http_client_request_send`
- `esp_http_client_set_header`
- `esp_http_client_set_post_field`
- `esp_http_client_set_redirection`
- `esp_http_client_set_url`
- `esp_http_client_write`
- `esp_log`
- `esp_log_buffer_hex_internal` *(only: cores3, watcher)*
- `esp_log_timestamp`
- `esp_log_va`
- `esp_log_write`
- `esp_log_writev`
- `esp_netif_get_desc`
- `esp_netif_get_event_id`
- `esp_netif_get_flags`
- `esp_netif_get_handle_from_ifkey`
- `esp_netif_get_hostname`
- `esp_netif_get_ifkey`
- `esp_netif_get_io_driver`
- `esp_netif_get_ip_info`
- `esp_netif_get_nr_of_ifs`
- `esp_netif_get_old_ip_info`
- `esp_now_add_peer`
- `esp_now_is_peer_exist`
- `esp_now_send`
- `esp_random`
- `esp_read_mac`
- `esp_sntp_enabled`
- `esp_sntp_init`
- `esp_sntp_setoperatingmode`
- `esp_sntp_setservername`
- `esp_timer_create`
- `esp_timer_delete`
- `esp_timer_get_expiry_time`
- `esp_timer_get_time`
- `esp_timer_is_active`
- `esp_timer_start_once`
- `esp_timer_start_periodic`
- `esp_timer_stop`
- `esp_tls_conn_destroy`
- `esp_tls_conn_new_async`
- `esp_tls_conn_new_sync`
- `esp_tls_conn_read`
- `esp_tls_conn_write`
- `esp_tls_get_and_clear_error_type`
- `esp_tls_get_bytes_avail`
- `esp_tls_get_conn_sockfd`
- `esp_tls_get_error_handle`
- `esp_tls_get_platform_time`
- `esp_tls_init`
- `esp_tls_plain_tcp_connect`
- `esp_transport_capture_errno`
- `esp_transport_close`
- `esp_transport_connect`
- `esp_transport_connect_async`
- `esp_transport_destroy`
- `esp_transport_get_context_data`
- `esp_transport_get_errno`
- `esp_transport_get_error_handle`
- `esp_transport_get_socket`
- `esp_transport_init`
- `esp_transport_list_add`
- `esp_transport_list_destroy`
- `esp_transport_list_get_transport`
- `esp_transport_list_init`
- `esp_transport_poll_connection_closed`
- `esp_transport_poll_read`
- `esp_transport_poll_write`
- `esp_transport_read`
- `esp_transport_set_async_connect_func`
- `esp_transport_set_context_data`
- `esp_transport_set_default_port`
- `esp_transport_set_errors`
- `esp_transport_set_func`
- `esp_transport_set_parent_transport_func`
- `esp_transport_ssl_crt_bundle_attach`
- `esp_transport_ssl_init`
- `esp_transport_ssl_set_addr_family`
- `esp_transport_ssl_set_alpn_protocol`
- `esp_transport_ssl_set_cert_data`
- `esp_transport_ssl_set_cert_data_der`
- `esp_transport_ssl_set_client_cert_data`
- `esp_transport_ssl_set_client_cert_data_der`
- `esp_transport_ssl_set_client_key_config`
- `esp_transport_ssl_set_client_key_data`
- `esp_transport_ssl_set_client_key_data_der`
- `esp_transport_ssl_set_client_key_password`
- `esp_transport_ssl_set_common_name`
- `esp_transport_ssl_set_ds_data`
- `esp_transport_ssl_set_tls_version`
- `esp_transport_ssl_skip_common_name_check`
- `esp_transport_tcp_init`
- `esp_transport_tcp_set_interface_name`
- `esp_transport_tcp_set_keep_alive`
- `esp_transport_utils_ms_to_timeval`
- `esp_transport_write`
- `esp_transport_ws_get_read_opcode`
- `esp_transport_ws_get_read_payload_len`
- `esp_transport_ws_init`
- `esp_transport_ws_send_raw`
- `esp_transport_ws_set_config`
- `esp_transport_ws_set_header_hook`
- `esp_transport_ws_set_response_headers`
- `esp_wifi_80211_tx`
- `esp_wifi_get_config`
- `esp_wifi_get_mac`
- `esp_wifi_get_mode`
- `esp_wifi_get_promiscuous`
- `esp_wifi_get_promiscuous_filter`
- `esp_wifi_get_ps`
- `esp_wifi_set_channel`
- `esp_wifi_set_promiscuous`
- `esp_wifi_set_promiscuous_filter`
- `esp_wifi_set_ps`
- `heap_caps_calloc`: the system notes each block of an app file and frees it when the app closes
- `heap_caps_free`: the system notes each block of an app file and frees it when the app closes
- `heap_caps_get_allocated_size`
- `heap_caps_get_free_size`
- `heap_caps_get_info`
- `heap_caps_get_largest_free_block`
- `heap_caps_get_minimum_free_size`
- `heap_caps_get_total_size` *(only: am175, cores3, lcd349)*
- `heap_caps_malloc`: the system notes each block of an app file and frees it when the app closes
- `heap_caps_print_heap_info` *(only: am175)*
- `httpd_query_key_value`
- `httpd_register_uri_handler`
- `httpd_req_get_url_query_len`
- `httpd_req_get_url_query_str`
- `httpd_req_recv`
- `httpd_resp_send`
- `httpd_resp_send_chunk`
- `httpd_resp_set_hdr`
- `httpd_resp_set_status`
- `httpd_resp_set_type`
- `jd_decomp`
- `jd_prepare`
- `tinfl_decompress`

## C library: memory

The memory functions. For an app file, the system notes each block and frees it when the app closes.

- `calloc`: the system notes each block of an app file and frees it when the app closes
- `free`: the system notes each block of an app file and frees it when the app closes
- `malloc`: the system notes each block of an app file and frees it when the app closes
- `realloc`: the system notes each block of an app file and frees it when the app closes

## C library: files

The file functions. The board storage is at `/data`, the SD card at `/sdcard`. For an app file, the system closes the files that are open when the app closes.

- `closedir`
- `fclose`: the system closes the open files of an app file when the app closes
- `fopen`: the system closes the open files of an app file when the app closes
- `fread`
- `fseek`
- `ftell`
- `fwrite`
- `mkdir`
- `opendir`
- `readdir`
- `remove`
- `rename`
- `stat`
- `unlink`

## C library: text

The string, memory and number-text functions of the C library.

- `_ctype_b`
- `atof`
- `atoi`
- `memchr`
- `memcmp`
- `memcpy`
- `memmove`
- `memset`
- `snprintf`
- `sprintf`
- `strcasecmp`
- `strcat`
- `strchr`
- `strcmp`
- `strcpy`
- `strlen`
- `strncasecmp`
- `strncat`
- `strncmp`
- `strpbrk`
- `strrchr`
- `strstr`
- `strtof`
- `strtok`
- `strtol`
- `tolower`
- `toupper`

## C library: other

Other functions of the C library: `qsort` (a sort) and `time` (the clock in seconds; it is correct only after the board got the time from the network, see `esp_sntp_*`).

- `qsort`
- `time`

## C library: math

The math functions of the C library. Use the `float` versions (`sinf`, `sqrtf`) and `fsin()` / `fcos()` of `gfx.h` for graphics.

- `acosf`
- `adc_cali_create_scheme_curve_fitting` *(only: cores3, lcd349, watcher)*
- `adc_cali_delete_scheme_curve_fitting` *(only: cores3, lcd349)*
- `adc_cali_raw_to_voltage` *(only: cores3, lcd349, watcher)*
- `adc_oneshot_config_channel` *(only: cores3, lcd349, watcher)*
- `adc_oneshot_del_unit` *(only: cores3, lcd349)*
- `adc_oneshot_new_unit` *(only: cores3, lcd349, watcher)*
- `adc_oneshot_read` *(only: cores3, lcd349, watcher)*
- `asinf`
- `atan2f`
- `atanf`
- `ble_gap_adv_active`
- `ble_gap_adv_rsp_set_data`
- `ble_gap_adv_rsp_set_fields`
- `ble_gap_adv_set_data`
- `ble_gap_adv_set_fields`
- `ble_gap_adv_start`
- `ble_gap_adv_stop`
- `ble_gap_conn_active`
- `ble_gap_conn_find`
- `ble_gap_disc_active`
- `ble_gap_security_initiate`
- `ble_gap_terminate`
- `ble_gap_unpair`
- `ble_gap_update_params`
- `ble_gatts_chr_updated`
- `ble_gatts_indicate`
- `ble_gatts_indicate_custom`
- `ble_gatts_notify`
- `ble_gatts_notify_custom`
- `ble_hs_id_copy_addr`
- `ble_uuid_cmp`
- `ble_uuid_flat`
- `ble_uuid_init_from_buf`
- `ble_uuid_length`
- `ble_uuid_u16`
- `ceilf`
- `cos`
- `cosf`
- `expf`
- `floor`
- `floorf`
- `fmaxf`
- `fminf`
- `fmod`
- `fmodf`
- `fprintf` *(only: tab5)*
- `fputc` *(only: tab5)*
- `gpio_config`
- `gpio_config_as_analog` *(only: cores3, lcd349, watcher)*
- `gpio_func_sel`
- `gpio_get_level`
- `gpio_input_enable`
- `gpio_iomux_input`
- `gpio_iomux_output`
- `gpio_matrix_input`
- `gpio_matrix_output`
- `gpio_od_disable`
- `gpio_od_enable`
- `gpio_output_disable`
- `gpio_output_enable`
- `gpio_pulldown_dis`
- `gpio_pulldown_en`
- `gpio_pullup_dis`
- `gpio_pullup_en`
- `gpio_reset_pin`
- `gpio_set_direction` *(only: am175, am18, cores3, lcd349, tab5)*
- `gpio_set_drive_capability` *(only: am175, am18, cores3, lcd349, tab5)*
- `gpio_set_intr_type`
- `gpio_set_level`
- `gpio_set_pull_mode` *(only: am175, cores3, lcd349, tab5, watcher)*
- `hypotf`
- `i2c_master_bus_add_device`
- `i2c_master_bus_rm_device`
- `i2c_master_multi_buffer_transmit`
- `i2c_master_receive` *(only: am175, cores3, watcher)*
- `i2c_master_transmit`
- `i2c_master_transmit_receive`
- `ledc_bind_channel_timer` *(only: lcd349, tab5, watcher)*
- `ledc_channel_config` *(only: lcd349, tab5, watcher)*
- `ledc_set_duty` *(only: lcd349, tab5, watcher)*
- `ledc_set_duty_with_hpoint` *(only: lcd349, tab5, watcher)*
- `ledc_timer_config` *(only: lcd349, tab5, watcher)*
- `ledc_timer_resume` *(only: lcd349, tab5, watcher)*
- `ledc_timer_rst` *(only: lcd349, tab5, watcher)*
- `ledc_update_duty` *(only: lcd349, tab5, watcher)*
- `log10f`
- `log2f`
- `logf`
- `lrint`
- `lrintf`
- `lroundf`
- `lwip_accept`
- `lwip_bind`
- `lwip_close`
- `lwip_connect`
- `lwip_fcntl`
- `lwip_freeaddrinfo`
- `lwip_getaddrinfo`
- `lwip_getpeername`
- `lwip_getsockopt`
- `lwip_htonl`
- `lwip_htons`
- `lwip_inet_ntop`
- `lwip_inet_pton`
- `lwip_ioctl`
- `lwip_itoa`
- `lwip_listen`
- `lwip_read`
- `lwip_recv`
- `lwip_recvfrom`
- `lwip_select`
- `lwip_send`
- `lwip_sendto`
- `lwip_setsockopt`
- `lwip_shutdown`
- `lwip_socket`
- `lwip_standard_chksum`
- `lwip_strnicmp`
- `lwip_write`
- `mbedtls_aes_crypt_cbc`
- `mbedtls_aes_crypt_cfb128`
- `mbedtls_aes_crypt_ctr`
- `mbedtls_aes_crypt_ecb`
- `mbedtls_aes_crypt_ofb`
- `mbedtls_aes_crypt_xts`
- `mbedtls_aes_free`
- `mbedtls_aes_init`
- `mbedtls_aes_setkey_dec`
- `mbedtls_aes_setkey_enc`
- `mbedtls_aes_xts_free`
- `mbedtls_aes_xts_init`
- `mbedtls_aes_xts_setkey_dec`
- `mbedtls_aes_xts_setkey_enc`
- `mbedtls_asn1_free_named_data_list_shallow`
- `mbedtls_asn1_get_alg`
- `mbedtls_asn1_get_alg_null`
- `mbedtls_asn1_get_bitstring`
- `mbedtls_asn1_get_bitstring_null`
- `mbedtls_asn1_get_bool`
- `mbedtls_asn1_get_int`
- `mbedtls_asn1_get_len`
- `mbedtls_asn1_get_mpi`
- `mbedtls_asn1_get_sequence_of`
- `mbedtls_asn1_get_tag`
- `mbedtls_asn1_sequence_free`
- `mbedtls_asn1_traverse_sequence_of`
- `mbedtls_asn1_write_int`
- `mbedtls_asn1_write_len`
- `mbedtls_asn1_write_mpi`
- `mbedtls_asn1_write_tag`
- `mbedtls_base64_decode`
- `mbedtls_base64_encode`
- `mbedtls_calloc`
- `mbedtls_ccm_auth_decrypt`
- `mbedtls_ccm_encrypt_and_tag`
- `mbedtls_ccm_finish`
- `mbedtls_ccm_free`
- `mbedtls_ccm_init`
- `mbedtls_ccm_set_lengths`
- `mbedtls_ccm_setkey`
- `mbedtls_ccm_starts`
- `mbedtls_ccm_update`
- `mbedtls_ccm_update_ad`
- `mbedtls_cipher_cmac_finish`
- `mbedtls_cipher_cmac_starts`
- `mbedtls_cipher_cmac_update`
- `mbedtls_cipher_crypt`
- `mbedtls_cipher_finish_padded`
- `mbedtls_cipher_free`
- `mbedtls_cipher_info_from_psa`
- `mbedtls_cipher_info_from_type`
- `mbedtls_cipher_info_from_values`
- `mbedtls_cipher_init`
- `mbedtls_cipher_set_iv`
- `mbedtls_cipher_set_padding_mode`
- `mbedtls_cipher_setkey`
- `mbedtls_cipher_setup`
- `mbedtls_cipher_update`
- `mbedtls_cipher_values_from_psa`
- `mbedtls_ct_memcmp`
- `mbedtls_ct_memcpy_if`
- `mbedtls_ct_memcpy_offset`
- `mbedtls_ecc_group_from_psa`
- `mbedtls_ecc_group_to_psa`
- `mbedtls_ecdsa_can_do`
- `mbedtls_ecdsa_der_to_raw`
- `mbedtls_ecdsa_raw_to_der`
- `mbedtls_ecdsa_sign`
- `mbedtls_ecdsa_sign_det_ext`
- `mbedtls_ecdsa_sign_det_restartable`
- `mbedtls_ecdsa_sign_restartable`
- `mbedtls_ecdsa_verify`
- `mbedtls_ecdsa_verify_restartable`
- `mbedtls_ecp_check_privkey`
- `mbedtls_ecp_check_pubkey`
- `mbedtls_ecp_copy`
- `mbedtls_ecp_gen_privkey`
- `mbedtls_ecp_get_type`
- `mbedtls_ecp_group_free`
- `mbedtls_ecp_group_init`
- `mbedtls_ecp_group_load`
- `mbedtls_ecp_grp_id_list`
- `mbedtls_ecp_is_zero`
- `mbedtls_ecp_keypair_free`
- `mbedtls_ecp_keypair_init`
- `mbedtls_ecp_mul`
- `mbedtls_ecp_mul_restartable`
- `mbedtls_ecp_muladd`
- `mbedtls_ecp_muladd_restartable`
- `mbedtls_ecp_point_cmp`
- `mbedtls_ecp_point_free`
- `mbedtls_ecp_point_init`
- `mbedtls_ecp_point_read_binary`
- `mbedtls_ecp_point_write_binary`
- `mbedtls_ecp_read_key`
- `mbedtls_ecp_set_zero`
- `mbedtls_ecp_write_key_ext`
- `mbedtls_free`
- `mbedtls_gcm_auth_decrypt`
- `mbedtls_gcm_crypt_and_tag`
- `mbedtls_gcm_finish`
- `mbedtls_gcm_free`
- `mbedtls_gcm_init`
- `mbedtls_gcm_setkey`
- `mbedtls_gcm_starts`
- `mbedtls_gcm_update`
- `mbedtls_gcm_update_ad`
- `mbedtls_hmac_drbg_free`
- `mbedtls_hmac_drbg_init`
- `mbedtls_hmac_drbg_random`
- `mbedtls_hmac_drbg_random_with_add`
- `mbedtls_hmac_drbg_reseed`
- `mbedtls_hmac_drbg_seed_buf`
- `mbedtls_hmac_drbg_update`
- `mbedtls_md`
- `mbedtls_md_error_from_psa`
- `mbedtls_md_finish`
- `mbedtls_md_free`
- `mbedtls_md_get_size`
- `mbedtls_md_hmac_finish`
- `mbedtls_md_hmac_reset`
- `mbedtls_md_hmac_setup`
- `mbedtls_md_hmac_starts`
- `mbedtls_md_hmac_update`
- `mbedtls_md_info_from_type`
- `mbedtls_md_init`
- `mbedtls_md_setup`
- `mbedtls_md_starts`
- `mbedtls_md_update`
- `mbedtls_mpi_add_abs`
- `mbedtls_mpi_add_int`
- `mbedtls_mpi_add_mpi`
- `mbedtls_mpi_bitlen`
- `mbedtls_mpi_cmp_abs`
- `mbedtls_mpi_cmp_int`
- `mbedtls_mpi_cmp_mpi`
- `mbedtls_mpi_copy`
- `mbedtls_mpi_core_add`
- `mbedtls_mpi_core_add_if`
- `mbedtls_mpi_core_bigendian_to_host`
- `mbedtls_mpi_core_bitlen`
- `mbedtls_mpi_core_check_zero_ct`
- `mbedtls_mpi_core_cond_assign`
- `mbedtls_mpi_core_cond_swap`
- `mbedtls_mpi_core_exp_mod`
- `mbedtls_mpi_core_exp_mod_working_limbs`
- `mbedtls_mpi_core_fill_random`
- `mbedtls_mpi_core_from_mont_rep`
- `mbedtls_mpi_core_gcd_modinv_odd`
- `mbedtls_mpi_core_lt_ct`
- `mbedtls_mpi_core_mla`
- `mbedtls_mpi_core_montmul`
- `mbedtls_mpi_core_montmul_init`
- `mbedtls_mpi_core_random`
- `mbedtls_mpi_core_read_be`
- `mbedtls_mpi_core_read_le`
- `mbedtls_mpi_core_shift_l`
- `mbedtls_mpi_core_shift_r`
- `mbedtls_mpi_core_sub`
- `mbedtls_mpi_core_sub_int`
- `mbedtls_mpi_core_to_mont_rep`
- `mbedtls_mpi_core_uint_le_mpi`
- `mbedtls_mpi_core_write_be`
- `mbedtls_mpi_core_write_le`
- `mbedtls_mpi_div_mpi`
- `mbedtls_mpi_exp_mod`
- `mbedtls_mpi_exp_mod_soft`
- `mbedtls_mpi_fill_random`
- `mbedtls_mpi_free`
- `mbedtls_mpi_gcd`
- `mbedtls_mpi_gcd_modinv_odd`
- `mbedtls_mpi_gen_prime`
- `mbedtls_mpi_get_bit`
- `mbedtls_mpi_grow`
- `mbedtls_mpi_init`
- `mbedtls_mpi_inv_mod`
- `mbedtls_mpi_inv_mod_even_in_range`
- `mbedtls_mpi_inv_mod_odd`
- `mbedtls_mpi_is_prime_ext`
- `mbedtls_mpi_lsb`
- `mbedtls_mpi_lset`
- `mbedtls_mpi_mod_int`
- `mbedtls_mpi_mod_mpi`
- `mbedtls_mpi_mul_int`
- `mbedtls_mpi_mul_mpi`
- `mbedtls_mpi_random`
- `mbedtls_mpi_read_binary`
- `mbedtls_mpi_read_binary_le`
- `mbedtls_mpi_safe_cond_assign`
- `mbedtls_mpi_safe_cond_swap`
- `mbedtls_mpi_set_bit`
- `mbedtls_mpi_shift_l`
- `mbedtls_mpi_shift_r`
- `mbedtls_mpi_shrink`
- `mbedtls_mpi_size`
- `mbedtls_mpi_sub_abs`
- `mbedtls_mpi_sub_int`
- `mbedtls_mpi_sub_mpi`
- `mbedtls_mpi_swap`
- `mbedtls_mpi_write_binary`
- `mbedtls_mpi_write_binary_le`
- `mbedtls_ms_time`
- `mbedtls_mutex_free`
- `mbedtls_mutex_init`
- `mbedtls_mutex_lock`
- `mbedtls_mutex_unlock`
- `mbedtls_net_free`
- `mbedtls_net_init`
- `mbedtls_net_recv`
- `mbedtls_net_send`
- `mbedtls_oid_get_cipher_alg`
- `mbedtls_oid_get_ec_grp`
- `mbedtls_oid_get_ec_grp_algid`
- `mbedtls_oid_get_md_hmac`
- `mbedtls_oid_get_oid_by_md`
- `mbedtls_oid_get_pk_alg`
- `mbedtls_pem_free`
- `mbedtls_pem_init`
- `mbedtls_pem_read_buffer`
- `mbedtls_pk_can_do`
- `mbedtls_pk_can_do_psa`
- `mbedtls_pk_ecc_set_group`
- `mbedtls_pk_ecc_set_key`
- `mbedtls_pk_ecc_set_pubkey`
- `mbedtls_pk_free`
- `mbedtls_pk_get_bitlen`
- `mbedtls_pk_get_key_type`
- `mbedtls_pk_get_psa_attributes`
- `mbedtls_pk_get_type`
- `mbedtls_pk_import_into_psa`
- `mbedtls_pk_info_from_type`
- `mbedtls_pk_init`
- `mbedtls_pk_parse_key`
- `mbedtls_pk_parse_public_key`
- `mbedtls_pk_parse_subpubkey`
- `mbedtls_pk_psa_rsa_sign_ext`
- `mbedtls_pk_rsa_set_key`
- `mbedtls_pk_rsa_set_pubkey`
- `mbedtls_pk_set_pubkey_from_prv`
- `mbedtls_pk_setup`
- `mbedtls_pk_sign`
- `mbedtls_pk_sign_ext`
- `mbedtls_pk_sign_restartable`
- `mbedtls_pk_verify`
- `mbedtls_pk_verify_ext`
- `mbedtls_pk_verify_restartable`
- `mbedtls_pk_wrap_psa`
- `mbedtls_pkcs5_pbes2_ext`
- `mbedtls_pkcs5_pbkdf2_hmac_ext`
- `mbedtls_platform_zeroize`
- `mbedtls_psa_aead_abort`
- `mbedtls_psa_aead_decrypt`
- `mbedtls_psa_aead_encrypt`
- `mbedtls_psa_cipher_abort`
- `mbedtls_psa_cipher_decrypt_setup`
- `mbedtls_psa_cipher_encrypt_setup`
- `mbedtls_psa_cipher_finish`
- `mbedtls_psa_cipher_set_iv`
- `mbedtls_psa_cipher_update`
- `mbedtls_psa_crypto_free`
- `mbedtls_psa_ecdsa_sign_hash`
- `mbedtls_psa_ecdsa_verify_hash`
- `mbedtls_psa_ecp_export_key`
- `mbedtls_psa_ecp_export_public_key`
- `mbedtls_psa_ecp_generate_key`
- `mbedtls_psa_ecp_import_key`
- `mbedtls_psa_ecp_load_public_part`
- `mbedtls_psa_ecp_load_representation`
- `mbedtls_psa_external_get_random`
- `mbedtls_psa_ffdh_export_public_key`
- `mbedtls_psa_ffdh_generate_key`
- `mbedtls_psa_ffdh_import_key`
- `mbedtls_psa_ffdh_key_agreement`
- `mbedtls_psa_get_random`
- `mbedtls_psa_key_agreement_ecdh`
- `mbedtls_psa_mac_abort`
- `mbedtls_psa_mac_compute`
- `mbedtls_psa_mac_sign_finish`
- `mbedtls_psa_mac_sign_setup`
- `mbedtls_psa_mac_update`
- `mbedtls_psa_mac_verify_finish`
- `mbedtls_psa_mac_verify_setup`
- `mbedtls_psa_rsa_export_key`
- `mbedtls_psa_rsa_export_public_key`
- `mbedtls_psa_rsa_generate_key`
- `mbedtls_psa_rsa_import_key`
- `mbedtls_psa_rsa_load_representation`
- `mbedtls_psa_rsa_sign_hash`
- `mbedtls_psa_rsa_verify_hash`
- `mbedtls_rsa_check_privkey`
- `mbedtls_rsa_check_pubkey`
- `mbedtls_rsa_deduce_crt`
- `mbedtls_rsa_free`
- `mbedtls_rsa_gen_key`
- `mbedtls_rsa_get_len`
- `mbedtls_rsa_init`
- `mbedtls_rsa_parse_key`
- `mbedtls_rsa_parse_pubkey`
- `mbedtls_rsa_pkcs1_sign`
- `mbedtls_rsa_pkcs1_verify`
- `mbedtls_rsa_public`
- `mbedtls_rsa_rsassa_pkcs1_v15_sign`
- `mbedtls_rsa_rsassa_pkcs1_v15_verify`
- `mbedtls_rsa_rsassa_pss_sign_ext`
- `mbedtls_rsa_rsassa_pss_verify_ext`
- `mbedtls_rsa_set_padding`
- `mbedtls_rsa_validate_crt`
- `mbedtls_rsa_validate_params`
- `mbedtls_rsa_write_key`
- `mbedtls_rsa_write_pubkey`
- `mbedtls_sha256`
- `mbedtls_sha256_finish`
- `mbedtls_sha256_free`
- `mbedtls_sha256_init`
- `mbedtls_sha256_starts`
- `mbedtls_sha256_update`
- `mbedtls_ssl_add_hs_hdr_to_checksum`
- `mbedtls_ssl_check_cert_usage`
- `mbedtls_ssl_check_curve`
- `mbedtls_ssl_check_curve_tls_id`
- `mbedtls_ssl_check_timer`
- `mbedtls_ssl_cipher_to_psa`
- `mbedtls_ssl_ciphersuite_from_id`
- `mbedtls_ssl_ciphersuite_uses_ec`
- `mbedtls_ssl_conf_alpn_protocols`
- `mbedtls_ssl_conf_authmode`
- `mbedtls_ssl_conf_ca_cb`
- `mbedtls_ssl_conf_ca_chain`
- `mbedtls_ssl_conf_ciphersuites`
- `mbedtls_ssl_conf_own_cert`
- `mbedtls_ssl_conf_renegotiation`
- `mbedtls_ssl_conf_verify`
- `mbedtls_ssl_config_defaults`
- `mbedtls_ssl_config_free`
- `mbedtls_ssl_config_init`
- `mbedtls_ssl_decrypt_buf`
- `mbedtls_ssl_derive_keys`
- `mbedtls_ssl_encrypt_buf`
- `mbedtls_ssl_fetch_input`
- `mbedtls_ssl_finish_handshake_msg`
- `mbedtls_ssl_flush_output`
- `mbedtls_ssl_free`
- `mbedtls_ssl_get_bytes_avail`
- `mbedtls_ssl_get_ciphersuite_sig_alg`
- `mbedtls_ssl_get_ciphersuite_sig_pk_alg`
- `mbedtls_ssl_get_ciphersuite_sig_pk_psa_alg`
- `mbedtls_ssl_get_ciphersuite_sig_pk_psa_usage`
- `mbedtls_ssl_get_ecp_group_id_from_tls_id`
- `mbedtls_ssl_get_key_exchange_md_tls1_2`
- `mbedtls_ssl_get_max_out_record_payload`
- `mbedtls_ssl_get_mode_from_ciphersuite`
- `mbedtls_ssl_get_mode_from_transform`
- `mbedtls_ssl_get_output_max_frag_len`
- `mbedtls_ssl_get_peer_cert`
- `mbedtls_ssl_get_psa_curve_info_from_tls_id`
- `mbedtls_ssl_get_tls_id_from_ecp_group_id`
- `mbedtls_ssl_get_verify_result`
- `mbedtls_ssl_handle_message_type`
- `mbedtls_ssl_handle_pending_alert`
- `mbedtls_ssl_handshake`
- `mbedtls_ssl_handshake_client_step`
- `mbedtls_ssl_handshake_free`
- `mbedtls_ssl_handshake_server_step`
- `mbedtls_ssl_handshake_step`
- `mbedtls_ssl_handshake_wrapup`
- `mbedtls_ssl_handshake_wrapup_free_hs_transform`
- `mbedtls_ssl_hash_from_md_alg`
- `mbedtls_ssl_init`
- `mbedtls_ssl_list_ciphersuites`
- `mbedtls_ssl_md_alg_from_hash`
- `mbedtls_ssl_optimize_checksum`
- `mbedtls_ssl_parse_alpn_ext`
- `mbedtls_ssl_parse_certificate`
- `mbedtls_ssl_parse_change_cipher_spec`
- `mbedtls_ssl_parse_finished`
- `mbedtls_ssl_parse_server_name_ext`
- `mbedtls_ssl_parse_sig_alg_ext`
- `mbedtls_ssl_pend_fatal_alert`
- `mbedtls_ssl_pk_sig_alg_from_sig`
- `mbedtls_ssl_prepare_handshake_record`
- `mbedtls_ssl_read`
- `mbedtls_ssl_read_record`
- `mbedtls_ssl_read_version`
- `mbedtls_ssl_renegotiate`
- `mbedtls_ssl_reset_checksum`
- `mbedtls_ssl_reset_in_pointers`
- `mbedtls_ssl_reset_out_pointers`
- `mbedtls_ssl_send_alert_message`
- `mbedtls_ssl_session_free`
- `mbedtls_ssl_session_init`
- `mbedtls_ssl_set_bio`
- `mbedtls_ssl_set_calc_verify_md`
- `mbedtls_ssl_set_hostname`
- `mbedtls_ssl_set_timer`
- `mbedtls_ssl_setup`
- `mbedtls_ssl_sig_from_pk`
- `mbedtls_ssl_sig_from_pk_alg`
- `mbedtls_ssl_start_handshake_msg`
- `mbedtls_ssl_start_renegotiation`
- `mbedtls_ssl_tls12_get_preferred_hash_for_sig_alg`
- `mbedtls_ssl_tls12_write_client_hello_exts`
- `mbedtls_ssl_transform_free`
- `mbedtls_ssl_transform_init`
- `mbedtls_ssl_update_handshake_status`
- `mbedtls_ssl_update_in_pointers`
- `mbedtls_ssl_update_out_pointers`
- `mbedtls_ssl_validate_ciphersuite`
- `mbedtls_ssl_verify_certificate`
- `mbedtls_ssl_write`
- `mbedtls_ssl_write_alpn_ext`
- `mbedtls_ssl_write_certificate`
- `mbedtls_ssl_write_change_cipher_spec`
- `mbedtls_ssl_write_client_hello`
- `mbedtls_ssl_write_finished`
- `mbedtls_ssl_write_handshake_msg_ext`
- `mbedtls_ssl_write_record`
- `mbedtls_ssl_write_sig_alg_ext`
- `mbedtls_ssl_write_version`
- `mbedtls_to_psa_error`
- `mbedtls_x509_crt_check_extended_key_usage`
- `mbedtls_x509_crt_check_key_usage`
- `mbedtls_x509_crt_free`
- `mbedtls_x509_crt_init`
- `mbedtls_x509_crt_is_revoked`
- `mbedtls_x509_crt_parse`
- `mbedtls_x509_crt_parse_cn_inet_pton`
- `mbedtls_x509_crt_parse_der`
- `mbedtls_x509_crt_parse_der_nocopy`
- `mbedtls_x509_crt_verify_restartable`
- `mbedtls_x509_crt_verify_with_ca_cb`
- `mbedtls_x509_free_subject_alt_name`
- `mbedtls_x509_get_alg`
- `mbedtls_x509_get_ext`
- `mbedtls_x509_get_key_usage`
- `mbedtls_x509_get_name`
- `mbedtls_x509_get_ns_cert_type`
- `mbedtls_x509_get_rsassa_pss_params`
- `mbedtls_x509_get_serial`
- `mbedtls_x509_get_sig`
- `mbedtls_x509_get_sig_alg`
- `mbedtls_x509_get_subject_alt_name`
- `mbedtls_x509_get_subject_alt_name_ext`
- `mbedtls_x509_get_time`
- `mbedtls_x509_oid_get_md_alg`
- `mbedtls_x509_oid_get_sig_alg`
- `mbedtls_x509_oid_get_x509_ext_type`
- `mbedtls_x509_parse_subject_alt_name`
- `mbedtls_zeroize_and_free`
- `multi_heap_get_info`
- `nearbyintf`
- `nvs_close`
- `nvs_commit`
- `nvs_erase_all`
- `nvs_erase_key`
- `nvs_get_blob`
- `nvs_get_i8`
- `nvs_get_stats` *(only: am175)*
- `nvs_get_str`
- `nvs_get_u16`
- `nvs_get_u32`
- `nvs_get_u8`
- `nvs_open`
- `nvs_release_iterator`
- `nvs_set_blob`
- `nvs_set_i8`
- `nvs_set_str`
- `nvs_set_u16`
- `nvs_set_u32`
- `nvs_set_u8`
- `pcTaskGetName`
- `powf`
- `psa_aead_decrypt`
- `psa_aead_encrypt`
- `psa_cipher_abort`
- `psa_cipher_decrypt_setup`
- `psa_cipher_encrypt_setup`
- `psa_cipher_finish`
- `psa_cipher_set_iv`
- `psa_cipher_update`
- `psa_copy_key`
- `psa_destroy_key`
- `psa_export_key`
- `psa_export_public_key`
- `psa_generate_key`
- `psa_generate_random`
- `psa_get_key_attributes`
- `psa_hash_abort`
- `psa_hash_clone`
- `psa_hash_compute`
- `psa_hash_finish`
- `psa_hash_setup`
- `psa_hash_update`
- `psa_import_key`
- `psa_key_derivation_abort`
- `psa_key_derivation_input_bytes`
- `psa_key_derivation_input_key`
- `psa_key_derivation_output_bytes`
- `psa_key_derivation_set_capacity`
- `psa_key_derivation_setup`
- `psa_mac_abort`
- `psa_mac_compute`
- `psa_mac_sign_finish`
- `psa_mac_sign_setup`
- `psa_mac_update`
- `psa_mac_verify_finish`
- `psa_mac_verify_setup`
- `psa_raw_key_agreement`
- `psa_reset_key_attributes`
- `psa_sign_hash`
- `psa_verify_hash`
- `psa_verify_message`
- `roundf`
- `sin`
- `sinf`
- `sntp_enabled`
- `sntp_get_sync_interval`
- `spi_bus_add_device`
- `spi_bus_remove_device`
- `spi_device_acquire_bus`
- `spi_device_get_actual_freq` *(only: cores3, watcher)*
- `spi_device_get_trans_result`
- `spi_device_polling_end`
- `spi_device_polling_start`
- `spi_device_polling_transmit`
- `spi_device_queue_trans`
- `spi_device_release_bus`
- `spi_device_transmit` *(only: cores3, watcher)*
- `sqrt`
- `sqrtf`
- `stk_split`
- `stk_tz_offset`
- `stk_tz_parse`
- `tanf`
- `tanhf`
- `ulTaskGenericNotifyTake`
- `ulTaskGetIdleRunTimeCounterForCore`
- `vEventGroupDelete`
- `vRingbufferDelete`
- `vRingbufferDeleteWithCaps` *(only: am175, cores3, lcd349, watcher)*
- `vRingbufferGetInfo`
- `vRingbufferReturnItem`
- `vsnprintf` *(only: tab5)*
- `xEventGroupClearBits`
- `xEventGroupCreate`
- `xEventGroupSetBits`
- `xEventGroupWaitBits`
- `xRingbufferCreate` *(only: am175, am18, cores3, lcd349, tab5)*
- `xRingbufferCreateStatic` *(only: am175, cores3, lcd349, watcher)*
- `xRingbufferCreateWithCaps` *(only: am175, cores3, lcd349, watcher)*
- `xRingbufferGetCurFreeSize`
- `xRingbufferReceive` *(only: am175, cores3, lcd349)*
- `xRingbufferReceiveUpTo`
- `xRingbufferSend`
- `xTimerCreate` *(only: cores3)*
- `xTimerGenericCommand` *(only: cores3)*
- `xTimerIsTimerActive` *(only: cores3)*

## Types and constants

The types and the constants of the main headers, with their field comments. They are in the headers: they need no ABI name.

### `app.h`

One screen of the arcade: the menu, a game, or the settings.

```c
typedef struct app {
    const char *name;
    bool landscape;                          // 640x172 if true, else 172x640
    void (*init)(void);                      // once at start-up, optional
    void (*enter)(int64_t now);              // each time the app opens
    void (*leave)(void);                     // optional
    void (*input)(const input_event_t *ev);  // logical coordinates
    void (*update)(int64_t now);
    void (*render)(int64_t now);
    void (*draw_icon)(int x, int y, int size);  // menu icon, landscape logical coordinates
    bool (*awake)(void);                        // optional: true = keep the screen on (music plays...), at most
                                                // AWAKE_MAX_MS (main.c) after the last touch, key or movement
    bool (*background)(void);                   // optional: true = run update() while the screen is off (the
                                                // speaker and the radio stay on; no render)
    bool (*keep_on)(void);                      // optional: true = the screen saver never turns the screen off
                                                // (SCENES with its SCREEN SAVER set to OFF)
    bool native;                                // it has a layout for a big native panel (gfx_native(), the AMOLED)
    uint8_t round;                              // it has a layout for a round panel (gfx_round(), the Watcher):
                                                // APP_ROUND; APP_ROUND_HI = the frame of the panel (gfx_w x gfx_h:
                                                // 466 x 466 on the AMOLED-1.75C, no scale; 412 on the Watcher)
    bool round_fit;                             // a round panel, no round layout: show the whole CoreS3 frame at
                                                // 1:1 inside the circle (not the zoom that cuts its corners)
    bool wheel;                                 // it reads the wheel itself (input_wheel*); else the wheel
                                                // is a left/right tilt and its button a tap in the middle
    bool key;                                   // it reads the BOOT key of the 3.49 itself (app_key_*: a jump in
                                                // a platform game); the key opens no CHAT in it
    bool run;                                   // the settings panel (sysui.h): the app runs on under the panel
                                                // (a timer, a music tool, a call); false = it stops there, and its
                                                // clock stops too (also true while a radio link is on: main.c)
    uint8_t needs;                              // APP_NEEDS_*: the menu hides the app if the board has not got it
} app_t;
```

```c
#define strlcpy tfn_host_strlcpy
#define APP_ROUND    1
#define APP_ROUND_HI 2
#define APP_NEEDS_BAR 0x01    // the 172 x 640 bar screen (its layout is for that shape only)
#define APP_NEEDS_IMU 0x02    // the motion sensor (no use without it)
```

### `input.h`

```c
typedef enum {
    IN_DOWN,  // finger on the panel
    IN_MOVE,  // finger moved (sent each poll while it moves)
    IN_UP,    // finger off the panel
    IN_TAP,   // short touch with little travel (sent after IN_UP)
    IN_LONG,  // finger held still for LONG_PRESS_MS (sent while the finger is down)
    IN_KEY,   // a key of a keyboard (the Tab5 keyboard): x = the key (INK_*), y = the modifiers (INM_*);
              // an app that has no use for keys does nothing with it (the apps with app_t.wheel also get the arrows
              // as wheel steps and Enter as a wheel push)
} input_kind_t;
```

THE KEYS OF IN_KEY (x): a letter, a digit or a sign is its ASCII code (Shift already in it); then these

```c
enum {
    INK_BKSP = 8,
    INK_TAB = 9,
    INK_ENTER = 13,
    INK_ESC = 27,
    INK_DEL = 127,
    INK_UP = 0x101,
    INK_DOWN,
    INK_LEFT,
    INK_RIGHT,
    INK_HOME,
    INK_END,
    INK_PGUP,
    INK_PGDN,
    INK_INS,
    INK_F1 = 0x111,  // INK_F1 .. INK_F1 + 11 = F12
};
```

the modifiers of IN_KEY (y)

```c
enum {
    INM_SHIFT = 1,
    INM_CTRL = 2,
    INM_ALT = 4,
    INM_GUI = 8
};
```

```c
typedef struct {
    input_kind_t kind;
    int16_t x, y;  // physical coordinates in the queue; main.c changes them to logical
    int64_t t_ms;
} input_event_t;
```

```c
typedef struct {
    bool down;
    int64_t down_ms;   // time of the last press
    int64_t up_ms;     // time of the last release
    uint32_t presses;  // +1 for each press
} input_end_t;
```

```c
#define LONG_PRESS_MS 350
#define INPUT_END_PX 120
#define INPUT_MAX_POINTS 5
```

### `gfx.h`

Logical orientation. All drawing uses logical coordinates.

```c
typedef enum {
    ORIENT_PORTRAIT,       // 172 x 640, physical layout
    ORIENT_PORTRAIT_FLIP,  // 172 x 640, turned 180 degrees
    ORIENT_LAND_A,         // 640 x 172, logical down = physical -x
    ORIENT_LAND_B,         // 640 x 172, logical down = physical +x
} orient_t;
```

### `settings.h`

```c
typedef struct {
    uint8_t volume;      // 0..100
    uint8_t brightness;  // 10..255
    bool mute;
    bool auto_rotate;  // pick the upright side with the IMU
    uint8_t sleep;     // screensaver: index into SLEEP_SECONDS (0 = never)
} settings_t;
```

### `audio.h`

```c
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
```

```c
typedef enum {
    W_SQUARE,
    W_PULSE,
    W_TRI,
    W_NOISE,
    W_SAW,
    W_SINE,
    W_SAMPLE
} wave_t;
```

```c
enum {
    AUDIO_PERC,
    AUDIO_SUSTAIN
};
```

```c
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
```

```c
typedef struct {
    uint16_t t;    // start, ms from the start of the tune
    uint16_t dur;  // ms
    uint8_t note;  // MIDI note (60 = C4). W_NOISE: the noise rate as a note
    int8_t bend;   // pitch sweep over the note, semitones (0 = none)
    uint8_t wave;  // wave_t, + TUNE_HOLD
    uint8_t vol;   // 0..100
} tune_note_t;
```

```c
typedef struct {
    uint16_t clocks;
    uint8_t audc, audf, audv;
} tia_step_t;
```

```c
#define AUDIO_SAMPLE_RATE 24000
#define AUDIO_BLOCK       120    // frames per block (5 ms)
#define TUNE_HOLD 0x80    // add to .wave: the note holds and fades at its end (not a decay)
```

### `tunes.h`

```c
typedef enum {
    TUNE_RUN,
    TUNE_HOP,
    TUNE_KNIGHT,
    TUNE_DRIVE,
    TUNE_PONG,
    TUNE_STACK,
    TUNE_PRISM,
    TUNE_TILT,
    TUNE_SPLAT,
    TUNE_SHAKE,
    TUNE_RALLY,
    TUNE_ZAP,
    TUNE_ECHO,
    TUNE_WOBBLE,
    TUNE_KABOOM,
    TUNE_WHACK,
    TUNE_TILES,
    TUNE_MERGE,
    TUNE_BUBBLE,
    TUNE_BURGER,
    TUNE_PINBALL,
    TUNE_SAFE,
    TUNE_POUR,
    TUNE_EGG,
    TUNE_MORSE,
    TUNE_BRASS,
    TUNE_TENNIS,
    TUNE_BITS,
    TUNE_SIGHT,
    TUNE_SKY,
    TUNE_OHM,
    TUNE_ROPE,
    TUNE_CRUISE,
    TUNE_FIGHT,
    TUNE_QUEST,
    TUNE_FIGHT_GO,
    TUNE_FIGHT_KO,
    TUNE_FIGHT_WIN,
    TUNE_FIGHT_LOSE,
    TUNE_GRAINS,
    TUNE_WRONG,
    TUNE_FIGHT_VS,  // FIGHT: the VS screen
    TUNE_PIXEL_GO,  // PIXEL: a game or a review starts
    TUNE_WRONG_GO,  // WRONG ROOM: a level starts
    TUNE_COUNT,
} tune_t;
```

### `imu.h`

A hit: a sharp jerk of the board. x, y in the logical frame of the current orientation, z = into the screen; g = the peak size.

```c
typedef struct {
    float x, y, z, g;
    int64_t t_us;
} imu_hit_t;
```

A snap: the board moves fast and then stops at once (like the flick of a towel). Loose things in the board then continue in the direction of the move. x, y: unit direction of the move in the logical frame; v: the top speed in m/s.

```c
typedef struct {
    float x, y, v;
    int64_t t_us;
} imu_snap_t;
```

### `mfont.h`

```c
typedef struct {
    uint16_t cp;
    uint8_t adv4;
    int8_t ox, oy;
    uint8_t w, h;
    uint32_t off;
} mglyph_t;
```

```c
typedef struct {
    uint8_t asc, line;
    uint16_t n;
    const mglyph_t *g;
    const uint8_t *a4;
} mfont_t;
```

### `kfont.h`

```c
typedef struct {
    uint16_t cp;
    uint8_t adv4;
    int8_t ox, oy;
    uint8_t w, h;
    uint32_t off;
} kglyph_t;
```

```c
typedef struct {
    uint8_t asc, line;
    uint16_t n;
    const kglyph_t *g;
    const uint8_t *a4;
} kfont_t;
```

### `sysui.h`

the items of an app in the panel (the options of a game are ONLY in the panel). An app gives a short list of its own items; the panel shows them as chips under VOLUME and BRIGHTNESS (a page; more items than the chips of the layout: a MORE chip turns the pages). The app task only. PI_PICK  a choice of 2-4 values (a tap = the next value): ZOOM, LANGUAGE, TILT, LEVEL PI_ACT   a button that acts at once (a safe action), then the panel closes: THE TITLE of a game that waits PI_ACT2  a button with a loss (RESTART, QUIT, NEW GAME, RESIGN): a still hold of 1 s (a fill shows it) acts; a short tap only shows HOLD (a thumb that misses a neighbour, also twice, loses nothing). The Watcher wheel: two pushes (the first shows PUSH AGAIN). Then the panel closes. Each chip acts when the finger comes off the glass on it. Text: English and Hebrew (the panel speaks the language of the app: sysui.c app_hebrew()).

```c
enum {
    PI_PICK,
    PI_ACT,
    PI_ACT2
};
```

the items of an app in the panel (the options of a game are ONLY in the panel). An app gives a short list of its own items; the panel shows them as chips under VOLUME and BRIGHTNESS (a page; more items than the chips of the layout: a MORE chip turns the pages). The app task only. PI_PICK  a choice of 2-4 values (a tap = the next value): ZOOM, LANGUAGE, TILT, LEVEL PI_ACT   a button that acts at once (a safe action), then the panel closes: THE TITLE of a game that waits PI_ACT2  a button with a loss (RESTART, QUIT, NEW GAME, RESIGN): a still hold of 1 s (a fill shows it) acts; a short tap only shows HOLD (a thumb that misses a neighbour, also twice, loses nothing). The Watcher wheel: two pushes (the first shows PUSH AGAIN). Then the panel closes. Each chip acts when the finger comes off the glass on it. Text: English and Hebrew (the panel speaks the language of the app: sysui.c app_hebrew()).

```c
typedef struct {
    uint8_t kind;         // PI_*
    uint8_t id;           // the number of the item for the app (the callbacks get it)
    uint8_t n;            // PI_PICK: the number of values (2..4)
    uint8_t def;          // PI_PICK with a key: the value when the store has no key
    const char *name[2];  // the name: English, Hebrew (UTF-8)
    const char *const
            *vals;    // PI_PICK: the names of the values, two for each: vals[2 * v] English, vals[2 * v + 1] Hebrew
    const char *key;  // PI_PICK: the store key of the value (the panel reads it, and writes it before set()); NULL:
                      // the value comes from get()
} pitem_t;
```

the items of an app in the panel (the options of a game are ONLY in the panel). An app gives a short list of its own items; the panel shows them as chips under VOLUME and BRIGHTNESS (a page; more items than the chips of the layout: a MORE chip turns the pages). The app task only. PI_PICK  a choice of 2-4 values (a tap = the next value): ZOOM, LANGUAGE, TILT, LEVEL PI_ACT   a button that acts at once (a safe action), then the panel closes: THE TITLE of a game that waits PI_ACT2  a button with a loss (RESTART, QUIT, NEW GAME, RESIGN): a still hold of 1 s (a fill shows it) acts; a short tap only shows HOLD (a thumb that misses a neighbour, also twice, loses nothing). The Watcher wheel: two pushes (the first shows PUSH AGAIN). Then the panel closes. Each chip acts when the finger comes off the glass on it. Text: English and Hebrew (the panel speaks the language of the app: sysui.c app_hebrew()).

```c
typedef struct {
    const pitem_t *items;
    uint8_t n;
    bool (*shown)(int id);  // the items that show now (NULL: all), for example RESTART only in a level
    int (*get)(int id);     // the value of a PI_PICK with no key
    void (*set)(int id,
                int v);  // a PI_PICK has the value v (the store key is written already); a PI_ACT / PI_ACT2 acts (v 0)
    void (*opened)(void);  // the panel begins to show (NULL: nothing): for example a sound of the app stops
    void (*resume)(void);  // the panel went away (back to the app): for example a tilt game takes the zero again
} pitems_t;
```

### `storage.h`

```c
#define STORAGE_MIDI "/data/midi"    // MIDI files from the web page, for TILES
```

## Header helpers

These functions are `static inline` in the headers. An app can use them with no ABI name.

### `app.h`

```c
static inline size_t tfn_host_strlcpy(char *d, const char *s, size_t n);
```

strlcpy of its own: an app file can call only the names of the app ABI (strlcpy is not one), and the C library of the host tests on Windows has none

```c
static inline float icon_time(void);
```

The time for the animated menu icons, in seconds (it starts again after 1000 s).

### `gfx.h`

```c
static inline bool gfx_round(void);
```

A round layout now (BOARD_ROUND and the native frame): only the circle of diameter gfx_w (centre gfx_w / 2, gfx_h / 2) shows on the panel.

```c
static inline bool gfx_in_circle(int x, int y, int margin);
```

True when (x, y) is inside the round panel, at least margin px from its edge.

```c
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
```

A colour of the frame buffer from 8-bit red, green and blue (RGB565, the two bytes swapped for the SPI bus).

```c
static inline float fcos(float x);
```

The fast cosine: `fsin(x + pi / 2)`.

```c
static inline bool orient_is_landscape(orient_t o);
```

True for ORIENT_LAND_A and ORIENT_LAND_B.
