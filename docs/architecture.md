# The architecture of TFN OS

This page tells how TFN OS is made: the layers of the code, the tasks and the CPU cores, the memory, the app loader
and the ABI, the store client and the update over the air (OTA). Each part names the files that do the work, so
that you can read the code next to this page.

TFN OS is an ESP-IDF 6.1 project. It runs on the ESP32-S3 boards and on one ESP32-P4 board (the M5Stack Tab5). One
source tree builds a firmware for each board kind. The [board guide](board_guide.md) tells how to add a kind.

## 1. The layers

```
apps/<app>/        the apps: one folder for each app (built in, or an app package of the store)
libs/<module>/     the code that more than one app uses
third_party/<name>/  the code of other projects, each with its licence file (NOTICE.md): zstd, minimp3, simplewebp,
                   TJpgDec, the BMI270 configuration, the JPEG decoder of Espressif
main/tfn/          the public API: the headers that an app includes ("tfn/gfx.h")
main/              the core of the OS: the main loop, the system apps, the services, the runtime of the app packages
main/board*.c      the board layer: one driver for each board kind, and the shared board code
boards/<kind>/     the capabilities of each board kind (board_caps.h)
components/        the vendored ESP-IDF components (elf_loader, esp_wireguard)
```

A layer uses only its own layer and the layers below it. An app or a lib includes only the public headers.

### 1.1 boards/: the capabilities

Each board kind has a folder `boards/<kind>/` with `board_caps.h`. This header gives a value to each capability
name: the frame of the apps (`LCD_W`, `LCD_H`), a round panel (`BOARD_ROUND`), the tablet UI (`BOARD_UI_TABLET`), the
motion sensor, the codecs, the camera, the size of the PSRAM and more. `main/tfn/board.h` includes the header of the
board of the build.

The shared code tests a capability, never the name of a board. The CI check `tests/style/check_board_if.py` stops a
change that adds a test of a board name to `main/`, `libs/` or `apps/`. The board layer is the only exception.

### 1.2 The board layer

The board layer has one driver file for each kind (it compiles only for its own kind) and the shared board code:

| File | What it does |
| --- | --- |
| `main/board.c` | The driver of the Waveshare ESP32-S3-Touch-LCD-3.49 (kind `lcd349`) |
| `main/board_cores3.c` | The driver of the M5Stack CoreS3 and CoreS3 SE (kind `cores3`) |
| `main/board_amoled.c` | The driver of the Waveshare ESP32-S3-Touch-AMOLED-1.8 (kind `am18`) |
| `main/board_am175.c` | The driver of the Waveshare ESP32-S3-Touch-AMOLED-1.75C (kind `am175`) |
| `main/board_watcher.c` | The driver of the Seeed SenseCAP Watcher (kind `watcher`) |
| `main/board_tab5.c` | The driver of the M5Stack Tab5, ESP32-P4 (kind `tab5`) |
| `main/board_common.c` | The weak default functions for the parts that a board does not have |
| `main/board_sd.c`, `main/board_sd.h` | The shared SD card code: the lock, the state, the mount and the check |
| `main/board_i2c.h` | Small helpers for the I2C registers of the chips |
| `main/pmic_axp2101.c` | The shared code of the AXP2101 power chip |

The API of the board layer is in `main/tfn/board.h` (the part that an app can use) and `main/board.h` (the part for
the OS): `board_init()`, `board_lcd_flush()`, `board_touch_points()`, `board_backlight()`, the keys, the battery,
the SD card and the power off.

### 1.3 main/: the core

The core has these groups of files:

| Group | Files |
| --- | --- |
| The start and the main loop | `main/main.c`, `main/main_saver.c`, `main/main_keys.c`, `main/main_selftest.c` |
| The picture and the text | `main/gfx.c`, `main/mfont.c`, `main/kfont.c`, `main/uikit.c` |
| The input and the motion | `main/input.c`, `main/input_fingers.inc`, `main/imu.c`, `main/kbd.c` |
| The sound | `main/audio.c` (the synthesizer, the mixer, the limiter), `main/music.c`, `main/midifile.c`, `main/tunes.c` |
| The settings and the files | `main/settings.c` (NVS), `main/storage.c` (the flash file system `/data`), `main/layout.c` |
| The system layer | `main/sysui.c`, `main/sysui_panel.c`, `main/sysui_layout.c` (the settings panel, the messages, the questions) |
| The notices | `main/notify.c`, `main/notify_page.c` |
| The network | `main/wifi.c`, `main/wifi_nets.c`, `main/wifi_web.c`, `main/web_server.c`, `main/web_auth.c`, `main/lan.c` |
| The radio between boards | `main/link.c` (ESP-NOW), `main/net2.c` (the 2-board games), `main/duel.h`, `main/friends.c` |
| The console | `main/console.c` (the command table) and `main/console_*.c` (the commands) |
| The update | `main/ota.c` |
| The runtime of the app packages | `main/tfn_app.c`, `main/tfn_load.c`, `main/tfn_install.c`, `main/tfn_scan.c`, `main/tfn_icons.c`, `main/tfn_watch.c`, `main/tfn_store.c`, `main/tfn_web.c` |
| The system apps | `main/app_menu.c` (with `main/menu_*.c`), `main/app_settings.c` (SETUP), `main/app_files.c` (FILES), `main/app_store.c` (STORE), `main/app_gamepad.c`, `main/app_walkie.c` |

`main/apps.json` is the one list of the apps and of the core files. `main/apps_gen.py` reads it at each CMake
configure and writes the lists of the build into the build folder. Do not edit the made lists by hand.

### 1.4 main/tfn/: the public API

The headers in `main/tfn/` are the public API. An app includes them by this name: `#include "tfn/gfx.h"`. Each
header in `main/` with the same name (for example `main/gfx.h`) is the view of the OS: it includes the public header
and adds the names that only the OS uses.

The CI check `tests/style/check_api.py` stops an app or a lib that includes a header of `main/` that is not public.
The colours of the system screens have names in `main/tfn/theme.h`.

### 1.5 libs/ and apps/

- `libs/<module>/` holds the code that more than one app uses: for example `libs/tfsfx` (the MP3 effects),
  `libs/tfnet` (HTTP and JSON for the apps), `libs/tfphys` (the physics), `libs/zimread` and `third_party/zstd` (the offline
  ZIM reader), `libs/kscroll` (the momentum scroll).
- `apps/<app>/` holds one app: its sources, its `store.json` (the text of the store) and its tools.

An app is built in (it is in the firmware) or it is an app package (the board installs it from the store or from a
file). The entry of the app in `main/apps.json` tells which. The `slim` part of `main/apps.json` lists the board kinds
whose firmware has only the core apps: all other apps come as packages.

### 1.6 The extras

The extras are optional features of the core. Each extra has a Kconfig option in the menu "TFN OS extras" of
`main/Kconfig.projbuild`:

| Option | The feature |
| --- | --- |
| `CONFIG_TFN_EXTRA_OREF` | HOME FRONT ALERTS: the alerts of Pikud HaOref in the background (`main/oref.c`) |
| `CONFIG_TFN_EXTRA_HA` | The Home Assistant client and its board web page (`main/ha.c`, `main/web_ha.c`) |
| `CONFIG_TFN_EXTRA_WEBCAM` | The web camera page (`main/webcam.c`) |
| `CONFIG_TFN_EXTRA_RF_NATIVE` | The WiFi receiver as a receiver of I/Q samples (`main/rf_native.c`, `main/rf_test.c`) |
| `CONFIG_TFN_EXTRA_WIFI_CAPTURE` | The WiFi capture of the installed apps (`main/wifi_capture.inc`) |
| `CONFIG_TFN_EXTRA_VPN` | A WireGuard tunnel to a hub (`main/vpn.c`, with `components/esp_wireguard`) |
| `CONFIG_TFN_EXTRA_STOCKS` | The stock quotes and alerts in the background (`main/stocks.c`) |
| `CONFIG_TFN_EXTRA_CHAT` | The voice assistant CHAT (`main/app_chat.c`, `main/web_chat.c`) |

The part `core.extras` of `main/apps.json` gives the files of each extra. When an option is off, `main/CMakeLists.txt`
gives it to `main/apps_gen.py` (`--extra-off`), and the files of the extra are not in the build. The hooks of an extra
in the core are empty inline functions in its header. Include `main/tfn_extras.h` before an `#if` on an extra option.
Now all the extras are on by default.

## 2. The tasks and the cores

The ESP32-S3 has two cores. TFN OS puts the main loop on core 0 and the time-critical drivers on core 1. The table
gives the tasks of the OS (the name, the core, the priority, the file):

| Task | Core | Priority | File | What it does |
| --- | --- | --- | --- | --- |
| main | 0 | 1 | `main/main.c` | The start, then the frame loop of the open app (ESP-IDF main task) |
| `lcd` | 1 | 7 | `main/board*.c` | The flush of a frame buffer to the panel |
| `audio` | 1 | 8 | `main/audio.c` | The synthesizer and the mixer: one block of 5 ms each time, to I2S |
| `mic` | 1 | 5 | `main/audio.c` | The microphone (its stack is in PSRAM) |
| `input` | 1 | 5 | `main/input.c` | The touch reads (about 100 Hz), the gestures, the pull zone of the panel |
| `imu` | 1 | 4 | `main/imu.c` | The motion sensor and the tilt estimate |
| httpd | 1 | 2 | `main/web_server.c` | The web server of the board |
| `console` | 0 | 2 | `main/console.c` | The USB serial console |
| `lan` | 0 | 6 | `main/lan.c` | The UDP beacons and the link packets on the WiFi network |
| `alarm` | 1 | 2 | `main/alarm.c` | The alarms and the timers of CLOCK |
| `watch` | 0 | 1 | `main/tfn_watch.c` | The background checks of the app packages |
| `store` | 1 | 2 | `main/app_store.c` | The network work of STORE (only while STORE is open) |
| `iconpre` | 0 | 1 | `main/tfn_app.c` | The read of all menu icons at the start (only with 32 MB of PSRAM) |
| `hid_tx` | any | 5 | `main/bt_pad.c` | The Bluetooth gamepad and keyboard reports |
| `usb_phones`, `usbh_lib` | 0 | 4, 5 | `main/usb_phones.c` | The USB host for USB headphones |
| `tab5kbd` | 0 | 4 | `main/tab5_kbd.c` | The Tab5 keyboard |
| `vpn` | 0 | 4 | `main/vpn.c` | The tunnel (the extra VPN) |

The WiFi, lwIP and NimBLE (Bluetooth) tasks of ESP-IDF use their ESP-IDF defaults.

### 2.1 The frame loop

`app_main()` in `main/main.c` starts the parts in this order: the picture, the settings, the web password, the
friends, the board (`board_init()`), the storage, the sound, the motion, the input, the `init()` of each built-in
app, the notices, the runtime of the app packages (`tfn_init()`), the console and the WiFi. Then it opens the menu and
runs `main_frame()` for ever.

Each frame of `main_frame()` does these steps:
1. The services of the frame: the screen saver, the OTA panel, the flash layout, the Bluetooth stop.
2. The settings panel and the clock of the app (`frame_panel()`): while the panel shows, the app stops, and its clock
   stops too.
3. The keys and the touches (`frame_keys()`, `frame_touches()`): the events go to `input()` of the app.
4. `update()` of the app, then `render()` of the app and the system layers (the panel, the banners, the messages).
5. `board_lcd_flush(gfx_swap(), ...)`: the main task gives the frame buffer that it drew to the `lcd` task, and draws
   the next frame into the other buffer.

There is no fixed frame rate. A frame takes the time of the app plus the wait for the last flush. With the screen
off, the loop runs the background work of the app (`app_t.background`) and waits 10 ms or 100 ms.

### 2.2 The display flush

The `lcd` task of each driver takes a frame from a queue of one item. It copies the rows of the frame (in the
orientation of the app) into DMA buffers in internal RAM, and sends them to the panel. On the 3.49, `main/board.c`
sends parts of 32 rows (`CHUNK_ROWS`). A round panel and the Tab5 also scale or turn the frame in the driver. The
flush and the app share the PSRAM bus, so a big panel can limit the frame rate.

### 2.3 The sound

`main/audio.c` has 32 synth voices, sample voices, two drone channels and a limiter. Other tasks send sound effects
through a queue. A tick function (`main/music.c`) runs in the `audio` task before each block, so the music has
sample-exact times. The I2S pins and the codec of each board come from `board_caps.h`. The VOLUME goes from 0 to 125:
100 is the full scale of the codec, and 101 to 125 is a gain of the mix before the limiter.

## 3. The memory

The ESP32-S3 boards have about 512 KB of internal RAM and 8 MB of PSRAM. The Tab5 has 32 MB of PSRAM. The rules:

- **Internal RAM is for the things that need it**: the task stacks of the drivers, the DMA buffers (the panel, the
  SD card, the I2S), the WiFi and the Bluetooth start, the interrupts. On the 3.49, about 57 KB of internal RAM are
  free at the menu. Under about 45 KB, the Bluetooth start can fail.
- **PSRAM is for the big data**: the two frame buffers (`main/gfx.c`), the code and the data of an app package, the
  icons of the menu, the frozen frame under the settings panel, most of the buffers of the apps.
- `sdkconfig.defaults` sets `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=2048`: a `malloc()` of less than 2 KB comes from
  internal RAM, a bigger one from PSRAM. `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` puts the WiFi and lwIP buffers into
  PSRAM when possible.
- **The static state (.bss) of most files goes to PSRAM.** Each entry of `main/apps.json` (the core, a module or an
  app) has a `psram` list: the sources whose `.bss` goes to PSRAM. `main/apps_gen.py` writes these lines into
  `apps.lf` in the build folder, with the scheme of `main/linker.lf`. A new source with static state must go into
  its `psram` list, unless an interrupt or a DMA transfer uses that state.
- A file can also put one variable into PSRAM with `EXT_RAM_BSS_ATTR`.
- The SD card driver needs a DMA buffer in internal RAM for each read into PSRAM. `main/sd_dma.h` gives the driver one
  buffer of 1 KB for the life of the board, so a read does not fail when the WiFi uses the internal RAM.
- A task that is not time-critical can have its stack in PSRAM (`xTaskCreatePinnedToCoreWithCaps()` with
  `MALLOC_CAP_SPIRAM`). Such a task must end with `vTaskDeleteWithCaps()`.

To see the memory on a board, use the console command `mem` (also `mem tasks` and `mem heap`), or SETUP > FREE MEMORY.

## 4. The app loader and the ABI

### 4.1 An app package

An installed app is a folder: `/data/apps/<id>/` (the flash of the board) or `/sdcard/apps/<id>/` (the SD card). The
folder has:

| File | Content |
| --- | --- |
| `manifest.txt` | `key=value` lines: the id, the name, the version, the ABI version and revision, the board kind, the group, the `app_t` flags, the permission classes, the sizes, the SHA-256 of the other files |
| `app.elf` | The app for this board kind: an ELF shared object (`sdk/tools/mk_app.py` makes it) |
| `icon.bin` | The frames of the menu icon (the PC drew them with `draw_icon()` of the app) |
| `sig` | The ECDSA P-256 signature of the store key over `manifest.txt` (optional in the developer mode) |
| `watch.elf` | Optional: the background check of the app (`main/tfn/watch.h`) |

The manifest has the SHA-256 of `app.elf` and `icon.bin`, so the signature covers all the files.

### 4.2 The install

`tfn_install()` (`main/tfn_install.c`) takes a folder that a web upload, FILES or STORE made. It checks:
- the board kind of the manifest is the kind of this firmware;
- the signature (`sig`) with the store key; with no good signature, only the developer mode lets the app in;
- the ABI version and revision (a newer revision than the firmware: "needs a newer firmware");
- the SHA-256 of each file.
Then it moves the folder to its place and the menu gets the app at the next scan (`main/tfn_scan.c`).

### 4.3 The open and the close

The menu shows a stub `app_t` for each installed app that can run (its name, its flags, its icon). When the user opens
it, the loader (`main/tfn_load.c`, `components/elf_loader`) does these steps:
1. It copies the app into PSRAM.
2. It links each call of the app to a firmware name of the ABI table (`tfn_resolve_sym()` in `main/tfn_app.c`).
3. It runs the C++ constructors of the app.
4. It calls `app_main()` of the app, which gives its `app_t`.

The runtime notes the memory, the files and the tasks of the open app (`malloc` and `xTaskCreatePinnedToCore` go
through wrappers in `main/tfn_app.c`). At the close, it frees what the app did not free. Callbacks, timers and
handles of ESP-IDF are the work of the app: it must give them back in `leave()` (`sdk/docs/raw.md` section 5).

**The crash guard:** the id of the open app is in RTC memory. A panic or a watchdog reset in the app counts one. Two
counts with no clean close between them stop the app (the state `stopped`). **The safe mode:** hold the BOOT key of
the 3.49 at the start, and no installed app loads.

### 4.4 The ABI

The ABI is the list of the firmware names that an app package can call:

| File | What it holds |
| --- | --- |
| `main/tfn_abi.json` | The ABI version and revision, each name with the board kinds that have it (`symbols`), the revision that added it (`revs`), the permission classes (`perms`), the class of each name (`need`), and the Kconfig conditions (`conditions`) |
| `main/tfn_abi.inc` | The table that the firmware compiles: `ELFSYM(name, lo, hi)` for each name (the FNV-1a 64 hash; the table has no strings), the default classes and the default policy |
| `main/tfn_abi_raw.json` | The families of raw ESP-IDF functions in the ABI, and the deny patterns |
| `main/tfn_abi_extra.json` | The extra names: names that no built-in app calls, which an ABI request added |
| `main/tfn_abi_alias.h` | `TFN_ABI_ALIAS(old, new)`: the old name of a renamed function stays in the ABI as a GCC alias |

`sdk/tools/abi_gen.py` makes `main/tfn_abi.json` and `main/tfn_abi.inc`. Inside one ABI version, names are only
added; each addition gives the next revision. An app package has the highest revision of its names in its manifest
(`rev=`), and a board with an older revision does not open it. A renamed function keeps its old name with
`TFN_ABI_ALIAS`, after the function in its own `.c` file. `tests/style/check_api.py` checks that both names are ABI
names. `sdk/docs/raw.md` tells how to ask for a new name.

**The permission classes:** a name that can do harm (the pins, the radio, the WiFi settings, the network, the
settings store) has a class. The STORE asks the user before the install, and the loader links a name of a class only
for an app whose manifest has that class. A manager can publish a newer policy, signed with the store key
(`/data/tfn_policy.txt`).

### 4.5 The background checks

An app package can have a second small part, `watch.elf`. The board keeps it in PSRAM and calls its `check()` in the
background rounds of the `watch` task (one wake of the radio for all the apps that are due). A check cannot draw,
play a sound or read the touch. It has about 8 s; after three faults in a row the board turns it off.
`main/tfn_watch.c` has this code and the secret store of the apps (`tfn_secret_get()`, `tfn_secret_set()`).

## 5. The store client

The STORE app (`main/app_store.c`) and its client (`main/tfn_store.c`) get the apps from a manager on the network.
The board does not go to the internet for the store.

1. The manager tells the board how to reach it (the request `op=mgr` of `POST /api/apps`): its address on the LAN
   (and, with the VPN extra, in the tunnel), a token for this board, and the apps that the profile of the board
   must have.
2. The board gets the catalog: `GET <manager>/api/store/catalog?mac=&kind=&abi=&rev=`. The catalog has only the apps
   that this board kind and this ABI revision can open.
3. The board gets each file of a package: `GET <manager>/api/store/file?id=&kind=&file=`. Then `tfn_install()` checks
   the package (section 4.2).

The token protects the removal of a required app and the developer mode OFF. A signed install needs no token, because
the board checks the signature of each package. The developer mode ON is a consent at the board (SETUP with a finger,
or the USB console): the web cannot turn it on.

With no manager, you install an app with a package on the SD card (FILES > INSTALL), with the board web page
`http://<board>/apps`, or with `sdk/tools/mk_app.py --push <board address>`.

## 6. The update over the air (OTA)

`main/ota.c` takes a new firmware image with `POST /api/ota` (the body is the app image of the build):
1. It reads the first block (the image header and the version), opens the menu and shows the update panel.
2. It erases the other app slot (`ota_0` or `ota_1`) and writes the image as the data comes.
3. It looks for the board tag in the image. An image for another board kind, or an image with no tag, is refused.
4. `esp_ota_end()` checks the checksum and the hash of the image. Then the board sets the new slot and restarts.
5. The rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`): the new firmware starts as "pending". After 20 s of normal
   run it marks itself good. If it restarts before that, the bootloader starts the old firmware again.

Only one update can run at a time: a second sender gets HTTP 409. The image has no signature now: a person who can
reach the web server of the board can send a firmware. Use the web password ([security.md](security.md)).

The flash layout: `partitions_slim.csv` (two app slots of 3.5 MB and a big `/data`) for most kinds,
`partitions_am175.csv` for the AMOLED-1.75C (32 MB of flash) and `partitions_tab5.csv` for the Tab5 (a boot menu
in the factory slot). `main/layout.c` can change an older layout of a board to the slim layout over the air.

## 7. The tests of the architecture

| Check | What it keeps |
| --- | --- |
| `tests/style/check_board_if.py` | No board name in the shared code |
| `tests/style/check_api.py` | Apps and libs include only the public API; the ABI aliases are ABI names |
| `tests/style/check_fsize.py` | No new function over 150 lines |
| `tests/contracts/check_all.py` | The ABI names, the NVS keys, the web routes and the console commands do not lose an item |
| `main/apps_gen.py --check` | The app list and its made files agree |
| `tests/golden/run.py` | The golden frames and tunes of the simulator |
