# The TFN OS simulator

The simulator runs the firmware of this repo on a PC (Windows, Linux or macOS), in a window that shows the Waveshare
ESP32-S3-Touch-LCD-3.49, or with no window for the tests (CI). Use it to try a change before you build a DEV firmware
and install it on a board.

It is not an emulator of the ESP32-S3 CPU, and it does not run a `.bin` file. It compiles the C sources of the
firmware for the PC and runs them: `main/main.c` (the main loop), the touch engine, the synthesizer, the system layer,
the menu and each app of `main/apps.json`. Only the hardware is simulated (see "What is real").

## Start

Double-click **`simulator/Run Simulator.cmd`**, or:

```powershell
python simulator/run.py                  # build what changed, then start
python simulator/run.py --app CHESS      # open an app at the start
python simulator/run.py --apps slim      # only the apps of the slim firmware of the 3.49 (the menu of the board)
python simulator/run.py --fresh          # a new board: no saved settings, scores or files
```

You need Python 3.10 or later (with Tkinter for the window, as in the installer from python.org) and a C compiler.
On Linux and macOS the simulator uses the compiler of the PC: `clang` (else `cc`), or the compiler in `TFN_SIM_CC`
(for example `TFN_SIM_CC=clang-18`). Use clang: the golden frames (below) need it, because gcc calls the arguments of a
function in another order. On Windows the simulator uses Zig 0.14.1 (its clang and the MinGW libraries of Windows). It
finds `zig.exe` in `TFN_SIM_TOOLS`, in a `.tools/` folder next to the repo folder, in `simulator/.tools/`, or in
`PATH`. If there is none:

```powershell
python simulator/build.py --setup        # downloads Zig 0.14.1 (about 80 MB) into simulator/.tools and checks its SHA-256
```

The library is `tfnsim.dll` (Windows), `libtfnsim.so` (Linux) or `libtfnsim.dylib` (macOS). Linux and macOS have no
sound output yet (the sound is silent; the meters and the recorder work). No ESP-IDF, no packages and no admin rights
are necessary. The first build takes about 1 minute; after that only the
changed files compile again.

## The window

| Control | Action |
|---|---|
| Left mouse button | A finger: a click is a tap, a hold is a long press, a drag is a swipe |
| Right mouse button | A second finger (also while the left button is down) |
| A pull down from the top edge | The settings panel of the system layer (the real gesture) |
| Esc (or P), PWR button | PWR: a short push goes back; hold it 2 s to power off. When the board is off, PWR starts it again |
| B, BOOT button | BOOT: hold it 0.7 s to open CHAT; some apps (WRONG ROOM) use each push |
| Arrow keys, Tilt sliders | Tilt the board (left/right, forward/back, as you see the screen) |
| Space, Shake slider | Shake the board |
| Q / E, Dial slider | Turn the board (the dial and the turn rate of the gyro) |
| K, Knock; Flick buttons; Step | A knock on the board; a flick up, down, left or right; one step |
| Z / X | A finger on each end of the panel (the end buttons of the two-player games) |
| R, Turn board | Turn the board 90 degrees clockwise. The menu turns with it (AUTO ROTATE), a game stays in its orientation |
| Position of the board | The four positions. "Follow the app" (on at the start) keeps the board in the orientation of the app |
| Microphone | The level of the made-up microphone (noise), and a tone (for TUNER) |
| Power and card | The battery level, USB power, and the SD card in its slot |
| Sound of the PC | The sound of the board on the speakers of the PC, and the meter of the board |
| Open app | Open an app at once (as the web page does) |
| F5, Restart | Build the firmware again and start the new one (a new flash). A build error keeps the board that runs |
| Ctrl+S, Screenshot | A PNG of the panel (1 pixel for each pixel of the panel) in the data folder, `shots/` |

The bottom of the window shows the app, its orientation, the frames a second, the screen state, and the log of the
board (the serial log: `I (1234) main: open CHESS`). The console of `Run Simulator.cmd` shows the same log.

The screen saver of the firmware works as on the board (SETUP > SLEEP, 1 minute at the start): with no touch, key or
movement the screen dims, and 15 s later it goes off. A touch or a key wakes it (that touch does nothing else).

## Make a change and try it

1. Start the simulator.
2. Change the firmware (an app in `apps/<app>/`, TFN OS in `main/`).
3. Push **F5**. The simulator builds the changed files and starts again with the same data (settings, scores, files).
   A compile error shows in a message and in the log; the board that runs stays.
4. Try the change: the touches, the gestures, the sensors, the sound, both orientations.
5. Run the checks (below), then build a DEV firmware and install it (docs/tfn_tool.md).

If the firmware stops (an access violation, a division by zero, a failed `assert()`), the window shows the reason and
the app that was open. The log has the stack with the file names and the lines, for example:

```
E (15380) fault: THE FIRMWARE STOPPED: an access violation (a bad pointer) (0xc0000005), task main
E (15389) fault:   app_chess.c:812 chess_update (tfnsim.dll+0x5d4a14)
```

## What is real and what is simulated

| Part | In the simulator |
|---|---|
| The main loop, the app switch, the screen saver, PWR and BOOT (`main/main.c`) | Real |
| The touch engine: taps, long presses, the release debounce, two fingers, the end buttons, the pull zone of the settings panel (`main/input.c`) | Real (the window gives the points of the touch chip, polled each 10 ms as on the board) |
| The system layer: the settings panel, the questions, the messages (`main/sysui.c`) | Real (no start picture: the JPEG decoder of the ROM is not there) |
| The synthesizer, the tunes, the TIA voices, the sound effects (`main/audio.c`) | Real; the sound plays on the PC. The audio task keeps the pace of the I2S DMA of the board |
| The microphone | Made-up: noise at the level of the slider, and a tone |
| The settings, the scores, the favourites (NVS, `main/settings.c`) | Real code; the NVS is the file `nvs.txt` in the data folder |
| `/data` (FAT), the exports (`main/storage.c`, `main/export.c`) | Real code; `/data` is the folder `data/` (8.9 MB, as the slim flash layout) |
| The SD card (`/sdcard`) | The folder `sdcard/`; "SD card in the slot" takes it out |
| The menu, the store, FILES, SETUP and each app | Real |
| The LCD (172 x 640) | The frame of the firmware, turned to the panel as the board does; the backlight dims the picture |
| The motion sensor | Simulated from the position of the board and the controls (gravity, tilt, shake, knocks, flicks, steps, dial) |
| FreeRTOS | Each task is a thread of the PC; the queues, semaphores and notifications act as on the board |
| WiFi, the radio link, Bluetooth, the web page, OTA, the VPN | Not there: the stubs of the host fuzz (`tests/host/stubs.c`) answer, so the screens show (for example the WiFi list has test networks) |
| The installed apps of TFN OS (app files from the store) | Not there: an app file is Xtensa code. Each app of `main/apps.json` is built in instead. The memory calls of an app that a board loads as an app file go through `src/sim_app_mem.c`, as the loader of the board sends them through its own functions: the free first stops each sound that uses the block |
| CPU speed, memory limits, the flash layout, the power modes | Not simulated: the PC is much faster and has much more memory |

So the simulator finds faults of the logic, of the layout and of the touch handling early. A test on the board is
still necessary for the speed, the memory, the drivers, the sound quality and the radios.

## The app sets

`--apps` (of `run.py`, `build.py` and `smoke.py` as `--build-apps`) selects the apps, as `main/apps_gen.py` does:

| Set | Apps |
|---|---|
| `all` (default) | Each app of `main/apps.json`, also the apps that are off and the store apps (as the host fuzz) |
| `on` | The apps with `"on": true` |
| `slim` | The slim firmware of the 3.49: only the core apps (the menu of a board with no installed apps) |
| a JSON file | A selection file of `apps_gen.py` (`{"apps": [...]}`, `{"add": [...], "remove": [...]}`) |

Each set has its own build folder (`simulator/.build/<set>/`), so a change of the set does not compile all again.

## The data folder

`simulator/.data/lcd349/` keeps the state of the simulated board from one start to the next: `nvs.txt` (the NVS),
`data/` (`/data`), `sdcard/` (`/sdcard`) and `shots/` (the screenshots). `--fresh` removes it; `--data DIR` uses
another folder (for example one folder for each test).

## The checks

```powershell
python simulator/smoke.py                       # each app, about 2 s (3 minutes for all)
python simulator/smoke.py --apps DICE SIMON --seconds 5 --shots
python simulator/smoke.py --headless --seconds 1  # no sound card (as CI)
python tests/golden/run.py                      # the golden frames and tunes (some seconds)
python simulator/sound_check.py                 # no sound reads the memory of an app after its free (1 minute)
python -m unittest discover -s simulator -p test_sim.py -v
```

`sound_check.py` checks the sound memory. First the checks of `main/audio.c` in a task of the firmware
(`src/sim_check.c`): `audio_stop_mem` stops each kind of sound that uses a block, the queue keeps its order and its
count after a drop, and `audio_set_tick(NULL)`, `audio_capture_stop()` and `audio_mic_tap(NULL)` are barriers. Then
ECHO KEYLLER leaves while a key loops its sound, with the audio task late 0, 45, 120 and 400 ms after each block
(`Board.audio_late()`), with the free hook of the loader and with no hook (an older firmware). The big blocks of the
app are in pages of their own (`Board.app_mem(guard_min=...)`), and a free makes their pages unusable, so a read after
the free stops the firmware at once. The last round has no hook and 400 ms: it must stop the firmware (the proof that
the check sees the fault).

`smoke.py` starts the firmware with an empty data folder and drives it as a user does: the menu at the start, a pull
from the top edge for the settings panel and BOOT to close it, then each app: it opens, gets the tap of its self-test
(`test349` in `main/apps.json`), random touches, tilts, shakes, knocks and microphone levels, and a short push of PWR
must go back to the menu. Then the NVS file, PWR held 2 s (power off), and a fault in a task (the board must stop, not
the program). The results are in `simulator/.build/smoke.json`; `--shots` keeps a picture of each app.

`tests/golden/run.py` opens each screen of `tests/golden/screens.json` with a fixed script and compares the frame
with the PNG in `tests/golden/<frame>/`, pixel for pixel; it also plays 5 start tunes and compares the output of the
mixer (`tests/golden/tunes.json`). After a change that must change a picture, look at `tests/golden/out/<name>.diff.png`
and run `python tests/golden/run.py --update`; commit the new PNG files with the change. The CI workflow
`.github/workflows/sim.yml` runs the build, the golden test and a short smoke test on Linux for each push.

The golden test uses the fixed clock of the simulator (`Board(fixed=seed)` in `tfnsim.py`): one task runs at a time
(`src/sim_sched.c`), the clock moves only in `Board.run(ms)`, `esp_random()` and `rand()` start from the seed, and
`time()` gives a fixed time of day. With the real clock (the window, `smoke.py`) the tasks are threads of the PC on
the clock of the PC, as before. So that the pixels are the same on each PC, `src/sim_det.h` sends `rand()`, `time()`
and the functions of `math.h` that the firmware uses (`sinf`, `atan2f`, `powf` ...) to the simulator
(`src/sim_math.c`), and `build.py` compiles with `-ffp-contract=off`.

`build.py --frame <board>` builds the firmware of another board (for the golden frames of that board; the window
shows only the 3.49). The capabilities of each board come from `boards/<kind>/board_caps.h`; `src/sim_board.c` puts
each frame on the panel as the driver of the board does:

| Frame | The panel | What the sim does |
|---|---|---|
| `lcd349` (default) | 172 x 640 | the frame turned to the panel |
| `cores3` | 240 x 320 | the frame turned to the panel |
| `watcher` | 412 x 412, round | no turn; a round layout 1:1; the 320 x 240 frame zoomed to the width of the circle (bilinear, as `board_watcher.c`), or 1:1 for `app_t.round_fit`; the touch points as logical points |
| `am175` | 466 x 466, round | the frames turn; the round frame 466 1:1, the native 412 frame scaled to the circle, the 320 x 240 frame at 280 x 372 with the ambient border (as `board_am175.c`); the touch points as logical points |
| `tab5` | 720 x 1280 | the tablet UI (`BOARD_UI_TABLET`); the 360 x 640 frame 2 x 2 (the CPU way of `board_tab5.c`), a HI frame 1:1 |

Not in the sim: the PPA of the Tab5 (the sim does only the CPU way of the board; the scale of the PPA is not checked
pixel for pixel),
the chunks of the round panels that the board does not send again (the same pixels), the border of the AMOLED-1.75C in
parts (the board sends a quarter of it in each frame; the sim sends all of it), the wheel of the Watcher (no input), the
keyboard and the event log of the Tab5, and the timing of each panel. The points of a golden script are points of the
view: a round board shows only the circle (black outside it).

`test_sim.py` checks the geometry (the same turns as `gfx_touch_to_logical()`), the build tools, a short smoke test,
and the window (`run.py --self-test`: Tk mouse and key events on the canvas). It is the test of this folder. The
all-app host fuzz (`tests/host/build.sh`) stays the test of the apps with ASan and UBSan.

## How it is made

| File | What it does |
|---|---|
| `run.py` | The window (Tkinter) |
| `smoke.py`, `sound_check.py`, `test_sim.py` | The checks |
| `tfnsim.py` | The Python interface of the library (ctypes), the geometry of the panel, PNG files |
| `build.py` | The build: `main/apps_gen.py --host` gives the sources of the app set, the compiler (Zig on Windows, clang on Linux and macOS) compiles each file (in parallel, again only when it or one of its headers changed) and links the library |
| `src/sim_api.c` | The interface of the library; `tfnsim_start()` runs `app_main()` in a task; `tfnsim_options()` (headless, the fixed clock) and `tfnsim_run()` |
| `src/sim_os.h`, `src/sim_os_win.c`, `src/sim_os_posix.c` | The parts of the operating system: locks, threads, the clock, the files. The other files use only these |
| `src/sim_sched.c` | The clock, and the lockstep scheduler of the fixed clock |
| `src/sim_det.h`, `src/sim_math.c` | The same `rand()`, `time()` and math results on each PC |
| `src/sim_board.c` | `board.h`: the LCD, the touch points, the keys, the backlight, the battery, the SD card, the power off |
| `src/sim_imu.c` | `imu.h`: the motion sensor, with the axes of `main/imu.c` |
| `src/sim_audio.c` | The codec devices of `esp_codec_dev`: the sound card of the PC (or none: headless), the microphone; for the checks, the audio task can be late after each block (`tfnsim_audio_late`) or wait there (a hold of `sim_check.c`) |
| `src/sim_sound_win.c`, `src/sim_sound_posix.c` | The sound card of the PC: waveOut on Windows, none (silent) on Linux and macOS |
| `src/sim_rtos.c` | FreeRTOS, `esp_timer`, `esp_random`, the log |
| `src/sim_nvs.c`, `src/sim_fs.c` | The NVS file; the paths of the board in the data folder, the FAT partition |
| `src/sim_force.h` | Included before each C file of the firmware: `fopen("/data/x")` and the other file calls go to `sim_fs.c` |
| `src/sim_app_mem.h`, `src/sim_app_mem.c` | Included before each C source of an app that a board loads as an app file (`build.py`, `app_file_sources()`): `malloc`, `calloc`, `realloc` and `free` of the app (also `heap_caps_*`) go to `sim_app_mem.c`, whose free first stops each sound that uses the block (`audio_mem_check`), as the loader of the board does (`main/tfn_app.c`). For the checks: the hook off (an older firmware), and the guard (big blocks in pages of their own) |
| `src/sim_check.c` | The checks of the sound memory of `main/audio.c` (`tfnsim_check_audio()`, `sound_check.py`) |
| `src/sim_crash.c`, `src/sim_crash_posix.c` | A fault of the firmware stops the board, logs the stack (Windows: DbgHelp and `tfnsim.pdb`; Linux and macOS: `backtrace()`) and keeps the window |
| `src/sim_rec.c` | The recorder (for the video of DEMO, and the golden tunes): `tfnsim_rec(prefix)` writes each LCD frame and each block of the synthesizer, with one clock (the sound samples), to `<prefix>.vid` and `<prefix>.pcm`; `Board.rec()` in `tfnsim.py` |
| `src/sim_extra.c` | The parts with no PC version (the installed apps, OTA, the flash layout, the VPN, the power modes, the console) |
| `idf/` | The ESP-IDF headers that these parts need (FreeRTOS, NVS, the I2S and codec types, ...) |

`tests/host/stubs.c` (the stubs of the host fuzz) gives the other hardware functions. A function of it that a real file
of the simulator (`main.c`, `input.c`, `audio.c`, `settings.c`, `sysui.c`, `storage.c`, `export.c`) or `src/` also
defines gets another name when it compiles (`-D name=hoststub_name`; `build.py` finds these names). So a new stub in
`stubs.c` works in the simulator with no change here. A source of an app that only includes a file of the firmware
(`#include "kbd.c"`: TOY 3.11 puts system code into its app file) is not compiled when that file is in the build.

When `main/main.c` or `main/sysui.c` calls a new function of a part that the host fuzz does not compile (WiFi, OTA,
...), the link stops with `undefined symbol`: add the function to `src/sim_extra.c` (what the board does when that
part is idle).
