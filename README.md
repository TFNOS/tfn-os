# TFN OS

**An operating system for small ESP32 boards with a touch screen: apps that install and go, a motion engine, sound,
and games for more than one board.**

TFN OS runs on ESP32-S3 and ESP32-P4 boards with ESP-IDF 6.1. Each app is a signed file that the board loads in some
milliseconds and removes again with one hold. The apps use the whole board: the touch screen, the motion sensor, the
speaker and the microphones, the SD card and the radio. A settings panel comes down from the top of the screen in
every app.

## What TFN OS gives you

| | |
|---|---|
| **Apps that come and go** | An app is an ELF file with a manifest and a signature. The loader links it only to the names of a fixed ABI. Install it to the board memory or to the SD card, and remove it again. When an app closes, its memory comes back. |
| **A store client** | The STORE app installs the apps of a store that you choose, with a description of each app in English and Hebrew. |
| **The board is the controller** | A 200 Hz motion engine gives tilt, turns, knocks, shakes and steps. The microphones hear your voice and your claps. |
| **Play together** | WALKIE and TALKIE carry your voice to other boards. 2 to 6 boards can share one game. |
| **Safe by default** | An app calls only the ABI names of the permission classes that you allow. An app that stops the board two times stays off until you install it again. A safe mode starts the board with no installed app. |
| **Made for makers** | An SDK with a template and examples, a desktop simulator of the 3.49 board, golden frames, and a host build that runs every app under AddressSanitizer. |

The optional features of the OS (Home Assistant, the web camera, the native radio receiver, the VPN) are in the
menu "TFN OS extras" of `idf.py menuconfig`. They are off by default. When an extra is off, its code is not in the
build.

## The boards

One source tree builds TFN OS for six board kinds. `boards/<kind>/board_caps.h` tells what each board has.

| Kind | Board | Chip | Screen |
|---|---|---|---|
| `lcd349` | Waveshare ESP32-S3-Touch-LCD-3.49 | ESP32-S3 | 640 x 172, a long bar |
| `cores3` | M5Stack CoreS3 and CoreS3-SE | ESP32-S3 | 320 x 240 |
| `am18` | Waveshare ESP32-S3-Touch-AMOLED-1.8 | ESP32-S3 | 368 x 448 AMOLED |
| `am175` | Waveshare ESP32-S3-Touch-AMOLED-1.75C | ESP32-S3 | 466 x 466 round AMOLED |
| `watcher` | Seeed SenseCAP Watcher | ESP32-S3 | 412 x 412 round, a wheel |
| `tab5` | M5Stack Tab5 | ESP32-P4 | 720 x 1280 |

[docs/headers.md](docs/headers.md) gives the extension connectors of each board. To add a board kind, read
[docs/board_guide.md](docs/board_guide.md).

## The apps in this repo

- **The system:** MENU, SETUP, FILES, STORE, GAMEPAD, WALKIE, TALKIE.
- **The tools:** CLOCK (alarms, timers, a stopwatch), LEVEL, TOUCH LAB, I2C SCANNER, SERIAL MONITOR, MQTT, WEATHER,
  MIDI.
- **The games:** DICE, STACK, SIMON.

`main/apps.json` is the one list of the apps. Each app has its folder in `apps/<app>/`, with a `store.json` (the
text of the store in English and Hebrew).

## Flash it from the browser

The web flasher in [`site/`](site/README.md) writes TFN OS to a board through the USB port, with Chrome or Edge on a
computer (Web Serial). It finds the board kind, and it can do an INSTALL (it erases all the flash) or an UPDATE (the
settings and the files stay). It needs the release files of each kind in `site/firmware/`. You can test it with no
board: `node site/test/flasher.test.js`.

## Build and flash with ESP-IDF

You need [ESP-IDF 6.1](https://docs.espressif.com/projects/esp-idf/). Give the defaults files of the board kind:

| Kind | `-D SDKCONFIG_DEFAULTS=` |
|---|---|
| `lcd349` | `sdkconfig.defaults` |
| `cores3` | `sdkconfig.defaults;sdkconfig.defaults.cores3se` |
| `am18` | `sdkconfig.defaults;sdkconfig.defaults.amoled18` |
| `am175` | `sdkconfig.defaults;sdkconfig.defaults.amoled175` |
| `watcher` | `sdkconfig.defaults;sdkconfig.defaults.watcher` |
| `tab5` | `sdkconfig.defaults;sdkconfig.defaults.tab5` (and `-D IDF_TARGET=esp32p4`) |

```sh
idf.py -B build_dev/lcd349 -D SDKCONFIG="$PWD/build_dev/lcd349/sdkconfig" -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
idf.py -B build_dev/lcd349 -p <port> flash monitor
```

`.github/workflows/firmware.yml` builds all kinds from a clean clone in the `espressif/idf:v6.1` container.

## Write your first app

An app is one C file (or a few) with an `app_t`: `enter()`, `update()`, `render()`, `leave()` and a `draw_icon()`
for its live menu icon. It includes only the public headers `main/tfn/*.h`.

1. Build the firmware of your board kind once into `build_dev/<kind>/` (see above). The SDK takes the compile options
   of the kind from it.
2. Build the host icon tool once: `sh sdk/tools/host/build.sh lcd349` (clang).
3. Put the board in the developer mode: SETUP > DEVELOPER MODE (then an app file with no signature can run).
4. Build the template, send it to the board and open it:

```sh
python sdk/tools/mk_app.py sdk/templates/hello/app_hello.c --sym app_hello --name HELLO --kinds lcd349 --nosign --push <board ip> --open
```

Change the code and run the same command again: the app opens again on the board in a few seconds.

| Read this | For |
|---|---|
| [sdk/README.md](sdk/README.md) | The SDK: the first app, the tools, the rules of the app files. |
| [sdk/docs/howto.md](sdk/docs/howto.md) | How to make each kind of app. |
| [sdk/docs/api.md](sdk/docs/api.md) | Each firmware name that an app can call (the ABI). |
| [sdk/docs/visual.md](sdk/docs/visual.md) and [sdk/docs/behave.md](sdk/docs/behave.md) | The screens, the fonts, the touch targets; the order of the calls, the input, the saves, the memory. |
| [sdk/docs/raw.md](sdk/docs/raw.md) | The raw ESP-IDF functions, the permission classes, the ABI request. |
| [sdk/docs/store.md](sdk/docs/store.md) and [sdk/docs/checklist.md](sdk/docs/checklist.md) | The store text of an app, and the checks before it goes to a store. |

## Test on the PC

- **The simulator:** `python simulator/run.py` runs the real firmware of the 3.49 in a window (Windows, Linux,
  macOS). `python simulator/smoke.py --headless` drives each app as a user does. See
  [simulator/README.md](simulator/README.md).
- **The golden frames:** `python tests/golden/run.py`.
- **The all-app fuzz:** each app with random touches and sensor values under AddressSanitizer and UBSan, on the 3.49,
  CoreS3 and Watcher frames:

  ```sh
  JOBS=8 sh tests/host/build.sh
  cd tests/host && ./fuzz349.exe 300 1 && ./fuzzs3.exe 300 1 && ./fuzzwt.exe 300 1
  ```

[CONTRIBUTING.md](CONTRIBUTING.md) lists all the checks of CI and the code standard.

## Inside the repo

| Folder | What is in it |
|---|---|
| `main/` | The OS: the main loop, graphics, sound, input, WiFi, the web pages of the board, the app runtime, the system apps, and `apps.json`. `main/tfn/` has the public API headers. |
| `boards/<kind>/` | The capabilities of each board kind (`board_caps.h`) and its own wiring. |
| `apps/<app>/` | One folder for each app. |
| `libs/<module>/` | The code that more than one app uses (the MIDI player, the 2-board link, the MP3 effects and more). |
| `components/` | The ESP-IDF components of the project (the ELF loader of the apps, WireGuard). |
| `sdk/` | The SDK of the app files: the docs, the template, the examples and `mk_app.py`. |
| `simulator/` | The desktop simulator of the 3.49. |
| `tests/` | The checks of CI: the contracts, the style, the licences, the unit tests, the golden frames, and the host build of the all-app fuzz (`tests/host/`). |
| `site/` | The web flasher. |
| `poolmgr/` | The pool manager: a pool of your boards on one computer ([docs/poolmgr.md](docs/poolmgr.md)). |
| `docs/` | The docs of the OS: [the user guide](docs/user_guide.md), [the architecture](docs/architecture.md), [the security model](docs/security.md). |

[AGENTS.md](AGENTS.md) has the rules for each developer and each coding agent.

## License

MIT for the code: see [LICENSE](LICENSE). [NOTICE.md](NOTICE.md) gives each third-party part and its licence.
