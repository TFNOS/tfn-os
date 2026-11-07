# TFN OS app behaviour

This document tells how an app works with the system: the `app_t`, the order of the calls, the input, the saves, the
sound, the screen saver, the settings panel, the language, the battery, the tasks and the memory. The rules for the
look are in [visual.md](visual.md). The reference of all functions is in [api.md](api.md). The recipes for each kind of
game and utility are in [howto.md](howto.md).

## 1. The app_t

An app is one `const app_t` (`main/app.h`). The symbol must be `app_<id>`: the id is the symbol with no `app_`, with
only `a-z`, `0-9` and `_`, and 20 characters at most. The name (`.name`) is the name in the menu, 23 characters at
most.

| Field | Need | What it does |
| --- | --- | --- |
| `name` | yes | The name in the menu and in the logs. Capitals, for example `"HELLO"`. |
| `landscape` | yes | `true`: the 3.49 frame is 640 x 172, the CoreS3 frame 320 x 240 and the Tab5 frame 640 x 360. `false`: 172 x 640, 240 x 320 and 360 x 640. Many apps use `!BOARD_BAR_SCREEN`: portrait on the 3.49, landscape on the CoreS3. |
| `init` | no | A built-in app: once at the start of the board. An installed app: after each load of its file, before `enter()`. The icon tool also calls it. |
| `enter(now)` | yes | Each time the app opens. Start the state here, and give the panel items (`sysui_panel_items()`). |
| `leave()` | no | When the app closes. Stop the sounds, the music and the tasks. Save what must stay. |
| `input(ev)` | yes | One call for each touch event, before `update()`. The points are logical. |
| `update(now)` | yes | Once each frame. The logic of the app. Do not draw here. |
| `render(now)` | yes | Once each frame, after `update()`. It draws the WHOLE frame. The board does not call it while a message of the system or the notification center covers the screen. |
| `draw_icon(x, y, s)` | yes for the store | The menu icon (visual.md, section 8). |
| `awake()` | no | `true` = keep the screen on (music plays, a level lies still). The limit: 2 min after the last touch, key or motion. |
| `background()` | no | `true` = `update()` runs also while the screen is off (a timer, a call). The speaker and the radio stay on. No `render()`. |
| `keep_on()` | no | `true` = the screen saver never turns the screen off (a reel with its screen saver set to OFF). |
| `native` | no | The AMOLED: the app has a layout for the full 368 x 448 panel (`gfx_native()`). |
| `round` | no | The Watcher: the app has a round layout of 412 x 412 (`gfx_round()`). |
| `round_fit` | no | The Watcher with no round layout: show the whole CoreS3 frame at 1:1 in the circle, not the zoom that cuts the corners. |
| `wheel` | no | The Watcher: the app reads the wheel itself (`input_wheel()`, `input_wheel_presses()`, `input_wheel_down()`). Else the wheel is a left / right tilt and its button a tap in the middle. |
| `key` | no | The 3.49: the app reads the BOOT key itself (`app_key_presses()`, `app_key_down()`). Then the key opens no CHAT. |
| `run` | no | `true` = the app runs on under the settings panel (a timer, a music tool, a call). `false` = it stops under the panel, and its clock stops too. |
| `needs` | no | `APP_NEEDS_IMU` (no use without the motion sensor), `APP_NEEDS_BAR` (a layout for the 3.49 bar only). The menu hides the app on a board that does not have it. |

An app with no `enter`, `input`, `update` or `render` does not open: the board shows `<NAME> GAVE NO APP`.

```c
const app_t app_hello = {
    .name = "HELLO",
    .landscape = true,
    .enter = enter,
    .leave = leave,
    .input = input,
    .update = update,
    .render = render,
    .draw_icon = icon,
    .round_fit = true,          // the Watcher: all of the 320 x 240 frame in the circle
};
```

## 2. The order of the calls

1. **The load** (an installed app). The board reads `app.elf`, checks its SHA-256, links it to the ABI names, runs the
   C++ static constructors and gets the `app_t`. Then it calls `init()`. A load takes 5-25 ms.
2. **`enter(now)`**. Before it, the board sets the orientation, clears the touch queue, stops the shake ticks and
   closes the settings panel.
3. **Each frame**: `input(ev)` for each event in the queue, then `update(now)`, then `render(now)`, then the frame goes
   to the screen.
4. **`leave()`**, when the user goes to the menu or another app opens.
5. **The unload** (an installed app). The board frees the memory of the app, closes its files and frees its code.

The times:

- `now` and `ev->t_ms` are in MILLISECONDS.
- While the settings panel shows and `run` is `false`, the board calls no `update()`, and the clock of the app stops:
  `now` does not count that time. The board calls `render()` one time when an item of the panel changes the app (the
  picture under the panel shows the change).
- When the user opens another app, the board calls `leave()` of the old app and frees it BEFORE it loads the new
  file.
- Use `now` for each motion. A frame can take more time than the last one: limit the step, for example
  `if (dt > 0.05f) dt = 0.05f;`.

## 3. The input

### Touch events

`input(const input_event_t *ev)` gets each event. `ev->x` and `ev->y` are logical points, `ev->t_ms` is the time.

| Kind | When |
| --- | --- |
| `IN_DOWN` | A finger comes down on the panel. |
| `IN_MOVE` | The finger moves (one event each poll while it moves). |
| `IN_UP` | The finger comes off the panel. |
| `IN_TAP` | A short touch with little travel. It comes after `IN_UP`. |
| `IN_LONG` | The finger stays still for `LONG_PRESS_MS` (350 ms). It comes while the finger is down. |

- A game action that needs speed acts on `IN_DOWN`. A precise target (a button, a cell) acts on `IN_TAP` or `IN_UP`.
- An app that starts its own drag (a scroll, a move) calls `input_tap_cancel()` in its `input()`. Then that touch
  gives no `IN_TAP` and no `IN_LONG`.
- More than one finger: `input_points(x, y, max)` gives the points now, in PHYSICAL coordinates (change them with
  `gfx_touch_to_logical()`). `input_multitouch()` tells if the panel reported two fingers at the same time.

```c
static void input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        if (s_state == ST_PLAY) jump(ev->t_ms);         // a game action: at once
        break;
    case IN_TAP:
        if (s_state == ST_TITLE && in_box(ev->x, ev->y, &s_start)) start(ev->t_ms);
        break;
    default:
        break;
    }
}
```

### Keys, the wheel and the motion sensor

- The BOOT key of the 3.49: set `.key = true`, then read `app_key_presses()` (the presses since the last call) and
  `app_key_down()`. On the other boards they give 0 and `false`.
- The wheel of the Watcher: set `.wheel = true`, then read `input_wheel()` (steps, + = clockwise),
  `input_wheel_presses()` and `input_wheel_down()`. `input_has_wheel()` tells if the board has a wheel.
- The motion sensor: `imu_ok()` tells if the board has one (the Watcher has none). `imu_set_level()` makes the board
  position now the zero. `imu_tilt_logical(&tx, &ty)` gives the tilt from the zero in g. `imu_gravity_logical()`,
  `imu_shake()`, `imu_pop_hit()`, `imu_pop_snap()`, `imu_dial()` and `imu_steps()` give more. Set
  `needs = APP_NEEDS_IMU` when the app has no use without it.

## 4. The saves

- An installed app keeps NOTHING in RAM for the next open. Its code and its memory go when it closes. Its static
  variables start again at the next load.
- Small values: `store_set(key, value)` and `store_get(key, default)` (`settings.h`). A value is a `uint32_t` in the
  NVS of the board. A key has 15 characters at most. When the NVS is full, the board writes a new key into the spill
  store (a file in `/data`); `store_get()` reads both.
- Start each key with the save prefix of the app: `<id>_`, the `saves` field of the manifest. For example the app
  `app_hello` uses `hello_best`, `hello_lang`.
- `store_set()` writes the flash only when the value changes. Do not call it in each frame: call it at a change or in
  `leave()`.
- A bigger save goes into a file in `/data` (the board storage): `storage_write(path, data, len)` and
  `storage_read(path, data, max, &len)`, or `fopen()`. Check `storage_ok()` first.

```c
static uint32_t s_best;

static void enter(int64_t now)
{
    s_best = store_get("hello_best", 0);
}

static void game_over(int score)
{
    if ((uint32_t)score > s_best) {
        s_best = (uint32_t)score;
        store_set("hello_best", s_best);
    }
}
```

## 5. Sound and music

- Each sound function obeys the mute of the board: when the user mutes the board, the app is silent. The one
  exception is `audio_alert(notes, n, vol)`: an alarm that plays also when the board is muted (the permission class
  `sound.alert`).
  `audio_muted()` tells if the user muted the board (then skip the work of a sound that nobody hears).
- Do not change the volume or the mute. The user sets them in the settings panel.
- The effects: `sfx_play(SFX_*)` (a set of ready sounds), `sfx_tone(f0, f1, ms, wave, vol)` (one tone with a sweep).
- A short tune: `audio_tune(notes, n)` with a `static const tune_note_t` table. `tune_play(TUNE_*)` plays a start tune
  of the firmware.
- A continuous tone (an engine, a wind): `audio_drone(ch, hz, vol, wave)`. Stop it in `leave()` with a volume of 0.
- Music from MIDI files: the background player `midi_bg_*` (`midi_bg.h`). Call `midi_bg_init()`, `midi_bg_start()`,
  `midi_bg_service()` in each frame, and `midi_bg_end()` in `leave()`. The tracks are MIDI or BGN data with the deflate compression, in the
  app.
- Do not write a new synth. Use these players: they share the voices with the effects of the game.
- MP3 effects and MP3 music (a game with an SD data pack): [sound.md](sound.md) and `libs/tfsfx`. Each shared module
  goes into `uses` of the entry of the app in `main/apps.json` (for example `"uses": ["midi_bg"]`).

```c
static void leave(void)
{
    audio_drone(0, 0, 0, W_NOISE);              // the drone stops
    midi_bg_end();                              // the music stops and frees its memory
}
```

## 6. The screen saver

The screen dims, and then goes off, after the time of SETTINGS > SLEEP with no touch. An app changes this only with
these functions of its `app_t`:

| Function | Use |
| --- | --- |
| `awake()` | `true` while the screen must stay on with no touch (music plays, a level lies still, a timer ends soon). The limit: 2 min after the last touch, key or motion. |
| `keep_on()` | `true` when the screen must never go off (only when the user turns it on in the app). |
| `background()` | `true` while `update()` must run with the screen off (a timer that rings, a call). The board then keeps the sound and the full WiFi on: it uses more power. |

Give `true` only while it is necessary. An app that keeps the screen on all the time empties the battery.

## 7. The settings panel

A pull down from the middle of the top edge brings the settings panel over any app: VOLUME, BRIGHTNESS, RESUME and
MENU. The Watcher opens it with a hold of the wheel button.

- By default the app stops under the panel. Set `.run = true` when it must run on.
- The options of an app (TILT, LANGUAGE, ZOOM, LEVEL, RESTART, QUIT) go ONLY into the panel, as items. Do not make a
  pause menu of your own. The title screen of a game keeps START, the modes and the levels.
- Give the items in `enter()` with `sysui_panel_items()`. The set must be `static const`.
- `PI_PICK`: a choice of 2-4 values. With a store `key`, the panel reads and writes the value itself.
- `PI_ACT`: a safe action. `PI_ACT2`: an action with a loss (RESTART, QUIT, NEW GAME): it needs a still hold of 1 s.
- `opened()`: the panel starts to show (stop a sound). `resume()`: the panel went away (a tilt game takes the zero
  again with `imu_set_level()`).
- A button of the app can open the panel: `sysui_panel_open()`.

```c
enum { HI_LANG, HI_AGAIN };
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const pitem_t ITEMS[] = {
    {PI_PICK, HI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_VALS, "hello_lang"},
    {PI_ACT2, HI_AGAIN, 0, 0, {"AGAIN", "שוב"}, NULL, NULL},
};
static void item_set(int id, int v)
{
    if (id == HI_LANG) s_hebrew = v == 1;       // (the panel wrote "hello_lang" already)
    if (id == HI_AGAIN) restart();
}
static void item_resume(void) { imu_set_level(); }
static const pitems_t PANEL = {ITEMS, 2, NULL, NULL, item_set, NULL, item_resume};

static void enter(int64_t now)
{
    s_hebrew = store_get("hello_lang", 0) == 1;
    sysui_panel_items(&PANEL);
}
```

A question over the app (two buttons): `sysui_ask(title, line, yes, no, secs, done)`, and `sysui_ask_close()`.
`sysui_active()` tells if the system layer (a message, a question) is on the screen.

## 8. English and Hebrew

- The firmware draws Hebrew with the Rubik faces (`KF_*`) and `ui_text_c()`: UTF-8 text, a Hebrew line from the right.
  The Bungee faces (`MF_*`) have no Hebrew letters.
- Keep the language in a store key of the app whose name ends with `_lang` (for example `<id>_lang`), and give it as a
  `PI_PICK` item of the panel. SETUP > APP LANGUAGE sets the language of all apps: a `*_lang` key that the app did not
  save gives that language, and a change there clears the `*_lang` keys of the apps.
- Give each item name and each value in both languages (`pitem_t.name[2]`, `vals[2 * v]` and `vals[2 * v + 1]`).
- Check each screen in both languages: a Hebrew line is often wider or narrower than the English one.

## 9. Battery

The boards run on small batteries. An app that draws all the time empties the battery very fast.

- When nothing moves and nobody touches the screen, do not draw a new frame each 16 ms. Wait in `update()`. STOCKS
  waits 140 ms per frame when no touch came for 3 s and no animation runs:

```c
static void update(int64_t now)
{
    // ... the logic ...
    if (now - s_touch_ms > 3000 && !s_moves) vTaskDelay(pdMS_TO_TICKS(140));   // idle: about 7 frames a second
}
```

- Keep `awake()`, `keep_on()` and `background()` short (section 6).
- Stop the radio work, the microphone and the drones when the app does not need them.

## 10. Tasks

A check of the network in the background (with the app closed) is not a task of the app: it is a `watch.elf` part
([notify.md](notify.md)).

- An app can start a task with `xTaskCreatePinnedToCore()` or `xTaskCreatePinnedToCoreWithCaps()`.
- Each task of the app must end in `leave()`: tell it to stop, wait for it, and let it call `vTaskDelete(NULL)`. A task
  made with `xTaskCreatePinnedToCoreWithCaps()` must end with `vTaskDeleteWithCaps(NULL)`, or its stack stays in
  memory.
- The board registers a task of the app just after it starts. A task that ends at once can stay registered and keep
  the app in memory: let a short task wait about 20 ms before its end.
- While a task of the app runs, the code of the app stays in memory. The board then cannot free the app, and another
  app can fail to load ("THE BOARD IS BUSY").
- The internal RAM is short (about 12-18 KB free with the WiFi on). Make a task stack in internal RAM only when
  `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` shows enough. A task stack in PSRAM crashed during flash writes.

## 11. Memory

- The code and the static data of an installed app go to PSRAM. The `ram` field of the manifest gives the size.
- Big buffers: `heap_caps_malloc(n, MALLOC_CAP_SPIRAM)`. Keep `malloc()` for small blocks.
- The board frees the blocks of `malloc`, `calloc`, `realloc`, `heap_caps_malloc` and `heap_caps_calloc` and closes
  the files of `fopen` when the app closes. But free your buffers in `leave()` all the same: the same source can be a
  built-in app of the firmware, and there nothing frees them.
- Add up the big buffers of the app and compare them with the board kind that has the least free PSRAM. The console
  `mem` shows the free PSRAM; measure it at the menu of each kind. The free memory must stay the same after many opens
  and closes.
- C++: the static constructors run before the board gets the `app_t`. Destructors do not run.

## 12. The rules that the board checks

- **The ABI.** An app file calls only the names of the ABI (`main/tfn_abi.json`, see api.md). `mk_app.py` stops when
  the app needs another name, and writes a request for it. A new name must go into the ABI (`main/tfn_abi_extra.json`,
  `sdk/tools/abi_gen.py`) and into a new firmware first: see [raw.md](raw.md).
- **The revision.** The manifest has the ABI revision of the newest name of the app (`rev=`). A board with an older
  firmware does not get the app from the store, and a file that comes in another way gets the state `bad_abi`.
- **The permission classes.** The manifest has the classes of the names of the app (`perms=`, see raw.md). The STORE
  asks the user before the install. The loader links a name of a class only for an app with that class in its manifest.
- **The signature.** A board runs an app file only with a good signature of the store key, or in the developer mode. The
  developer mode turns on only at the board: SETUP > DEVELOPER MODE (then TURN ON with a finger on the glass), or the
  USB console `apps dev 1`. The web can only turn it off (`POST /api/apps?op=dev&on=0`). A file with no signature then
  has the state `dev`.
- **The crash guard.** The board keeps the id of the open app in RTC memory. A panic or a watchdog reset in the app
  counts one. Two counts with no clean close between them stop the app (the state `stopped`): it does not open again
  until you install it again. `/api/apps` shows the last crash (`crash`, `crash_reason`).
- **The safe mode.** Hold the BOOT key of the 3.49 while the board starts: no installed app loads. Then remove the app
  that stops the board. The other kinds have no such key: there the crash guard stops the app, and you remove it on
  the board page `/apps` or with the console `apps rm <id>`.
- **The kind and the ABI version.** A file for another board kind does not show. A file for another ABI version gets
  the state `bad_abi`.
