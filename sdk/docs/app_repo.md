# Your first app in its own folder

An app of TFN OS does not have to be in the TFN OS repo. You can keep it in its own folder or in its own git repo. The
SDK package has all the files that the build needs: the public headers, the libs, the compile options of each board
kind and the ABI. The build reads only the package, ESP-IDF and the compiler. It reads no file of the TFN OS repo.

## 1. What you need

| Part | Where you get it |
| --- | --- |
| The SDK package `tfn-sdk.zip` | From the maintainer of TFN OS (section 9 tells how to make it). Put it in a folder of your choice and open it. |
| ESP-IDF, the version of the package (`idf` in `kinds/<kind>.flags`, now v6.1) | `git clone -b v6.1 --recursive https://github.com/espressif/esp-idf`. The build uses only its headers. You do not build a firmware. |
| The compilers: `xtensa-esp32s3-elf-gcc` (the ESP32-S3 kinds) and `riscv32-esp-elf-gcc` (the `tab5` kind), at the version of the package (`toolchain_version` in `kinds/<kind>.flags`) | The ESP-IDF install script: `install.sh esp32s3,esp32p4` (Linux, macOS) or `install.bat esp32s3,esp32p4` (Windows). |
| Python 3.8 or newer | Only the standard library. The package `cryptography` is necessary only for `--sign-key`. |
| A board in the developer mode | SETUP > DEVELOPER MODE on the board. Then the board runs an app file with no signature. |

The tool finds the compiler in `$IDF_TOOLS_PATH` (or `~/.espressif`), then on `PATH`. It accepts only the version of
the package. To use another folder, give `--toolchain <bin folder>`.

## 2. Make the folder

Copy the example `examples/counter/` of the package to a new folder, for example `my_counter/`:

```
my_counter/
  app.json          the manifest of the app
  app_counter.c     the source
```

Change the names in `app.json` and in the code together:

- `name`: the menu name. It must be the `.name` of the `app_t`.
- `sym`: the name of the `app_t` (`app_<id>`). The store id is `<id>`. The saves of the app start with `<id>_`.
- `srcs`: the sources of the app.

Use an id of your own. An app with the id of a store app is that store app for the manager.

[app_json.md](app_json.md) gives all fields of `app.json`: the group, the kinds, the store text, the icon, a background
check (`watch`), the ESP-IDF headers (`idf`).

## 3. Build

```
python <sdk>/tools/tfn_app.py build my_counter --kinds lcd349,cores3 --idf <esp-idf folder>
```

- `--idf` is not necessary when the environment variable `IDF_PATH` is the correct ESP-IDF.
- With no `--kinds`, the tool builds the kinds of `app.json` (`kinds`), else all kinds of the package.
- With no `--sdk`, the tool uses the package that holds it. To use another package, give `--sdk <folder>` or set
  `TFN_SDK`.

For each kind, the tool compiles the sources, links `app.elf`, checks it and checks each name that it calls against
`tfn_abi.json`. The package goes to `my_counter/out/<id>/<kind>/`:

| File | What it is |
| --- | --- |
| `app.elf` | The app file. |
| `icon.bin` | The menu icon. |
| `manifest.txt` | The id, the name, the version, the ABI revision, the permission classes, the `app_t` flags, the sizes and the hashes, the store text. |
| `sig` | The signature (only with `--sign-key`). |
| `sigs` | The developer signature (only with `--dev-key`; [store.md](store.md), section 6). |
| `watch.elf` | The background check (only when `app.json` has `watch`). |

This is the same format as the packages of `sdk/tools/mk_app.py`. For the same source, `app.elf` has the same bytes.

When a name is not in the ABI, the build stops. The request for the names is in `out/<id>/abi_request.json`
([raw.md](raw.md) tells how to ask for more names).

## 4. Send it to a board

```
python <sdk>/tools/tfn_app.py build my_counter --kinds lcd349 --push <board ip> --open
```

- `--push` installs the package of the kind on the board. Give one kind: the kind of that board.
- `--card` puts the app on the SD card of the board.
- `--open` opens the app after the install. Change the code and do the same command again (the hot reload).
- `--password` (or `TFN_BOARD_PASSWORD`) gives the web password of the board, when it has one.

A board runs an app file with no signature only in the developer mode. For the store, send the package to the owner
of the manager. The owner signs it in the manager tab APPS & SIGN.

To name yourself as the developer, build with `--dev-key <your pem> --dev-name <NAME>`: the package gets
`publisher=` and the file `sigs`, and `out/<id>/publisher.txt` is the key file for a board. A board owner who trusts
your key as `test` (SETUP > TRUSTED PUBLISHERS) runs the app with no store signature; the other boards show NOT
REVIEWED until the store signs it ([store.md](store.md), section 6).

## 5. The includes

| Include | What it is |
| --- | --- |
| `#include "tfn/gfx.h"` (and `app.h`, `input.h`, `audio.h`, `settings.h`, `sysui.h`, `uikit.h` ...) | The public API of TFN OS ([api.md](api.md)). |
| `#include "tfn/board.h"` | The capabilities of the board of the kind (`BOARD_BAR_SCREEN`, `LCD_W`, `LCD_H` ...). |
| `#include "tfdraw/tfdraw.h"`, `"tficon/tficon.h"`, `"kscroll/kscroll.h"`, `"jingle/jingle.h"`, `"quest_tilt/quest_tilt.h"`, `"t5view/t5view.h"`, `"sndrel/sndrel.h"` | The header libs. |
| `#include "tfsfx/tfsfx.h"`, `"tfphys/tfphys.h"`, `"tfnet/tfnet.h"`, `"tfmqtt/tfmqtt.h"`, `"zimread/zimread.h"`, `"zstd/zstd.h"` | The module libs: their code is in the firmware (ABI names, rev 22). Include the header only. The network functions of `tfnet` need the permission `net`. |
| `#include "rfpulse/rfpulse.h"`, `"rfpulse/rfp_board.h"`, `"rfdecode/rfdecode.h"`, `"irdecode/irdecode.h"`, `"rfsrc/rfsrc.h"`, `"sdrdsp/sdr_spec.h"` (and the other `sdr_*.h`), `"strokefont/strokefont.h"`, `"drumkit/drumkit.h"` | More module libs of the firmware (the radio and pulse tools, the stroke font, the drums). Include the header only. `"rfpulse/rfp_sketch.inc"` is the wiring sketch of a pin connection: the app compiles it. |
| `#include "minimp3/minimp3.h"`, `"simplewebp/simplewebp.h"` | The source libs: the app compiles them (one source of the app defines the implementation). |
| `esp_heap_caps.h`, `esp_random.h`, `esp_timer.h`, `esp_log.h`, `esp_err.h`, `freertos/FreeRTOS.h`, `freertos/task.h`, `freertos/queue.h`, `freertos/semphr.h` | The usual ESP-IDF headers. The default include folders (`"idf": "min"`) have them. |
| Other ESP-IDF headers (`esp_wifi.h`, `driver/uart.h`, `lwip/sockets.h`, `esp_tls.h` ...) | Put `"idf": "all"` in `app.json`. Then the build has all include folders of ESP-IDF. Each raw ESP-IDF name must be in the ABI ([raw.md](raw.md)). |

The package does not have the OS headers (`main/<name>.h`) or the files of other apps. The apps of the repo use
the same form: the build of the repo has `libs/` as an include folder, and CI stops an app that includes a lib by
its path in the repo (`"../../libs/tfdraw/tfdraw.h"`) or by its bare name (`"tfdraw.h"`; `tests/style/check_api.py`).
`sdk/tools/app_json.py` lists the includes that stop a build out of the repo for an app of the repo.

## 6. The icon

- Give a PNG file: `"icon": "icon.png"` in `app.json`, or `--icon icon.png`. The best size is 128 x 128 px. The tool
  scales another size. A pixel with an alpha under 128 is a hole (the menu shows its background there).
- With no PNG file, the tool makes a plain tile: a rounded square in a colour from the id, with the first letter of
  the name.
- The icon of the package has one frame. An animated icon (the `draw_icon` of the `app_t`) needs the icon tool of the
  repo (`sdk/tools/host`, `mk_app.py`). That tool compiles the whole repo, so it cannot run out of the repo.

The `app_t` flags of `manifest.txt` (`landscape`, `native`, `round`, `round_fit`, `wheel`, `key`, `run`, `needs`) come
from the `app_t` in `app.elf` of each kind. They are the values of the board kind.

## 7. The differences from mk_app.py

| | `mk_app.py` (in the repo) | `tfn_app.py` (in its own folder) |
| --- | --- | --- |
| The app data | `main/apps.json` and `store.json` | `app.json` |
| The compile options | `build_dev/<kind>/compile_commands.json` of a DEV firmware build | `kinds/<kind>.flags` of the package |
| The headers | the repo and all include folders of the build | the package and ESP-IDF |
| `app.elf` | | the same bytes for the same source |
| The icon | the animated frames of `draw_icon` (the icon tool of the repo) | a PNG file or a plain tile, one frame |
| The `app_t` flags | the icon tool on the PC | the `app_t` in `app.elf` |
| The signature | `--sign-key`, or the signer of the store setup (`store_conf.py`) | only `--sign-key`; else none |
| The developer key | `--dev-key` | `--dev-key` (the same files) |
| The store | `--publish` | not yet (send the package to the owner of the manager) |

## 8. The limits

- The kinds have two CPUs: Xtensa (ESP32-S3: all kinds but `tab5`) and RISC-V (ESP32-P4: `tab5`). A prebuilt library
  (`link`) is for one CPU only.
- The ABI ties an app to the ESP-IDF version of the package. A raw ESP-IDF name can change with a new ESP-IDF version.
- There is no simulator for one app yet. Test the app on a board.

## 9. Make the SDK package (for the maintainer)

```
python sdk/tools/sdk_pack.py --build-root build_dev [--src .] [--kinds lcd349,cores3] [--out sdk/out/tfn-sdk]
```

- `--build-root` is the folder of the DEV builds (`build_dev/<kind>/compile_commands.json`). Build each kind once
  with `dev.ps1` first.
- `--src` is the source tree of that build. The tool stops when `tfn/board.h` of `--src` does not select
  `boards/<kind>/board_caps.h` with the `sdkconfig.h` of the kind (the source and the build do not fit).
- The tool writes the folder and `tfn-sdk.zip` next to it.

The tool does these steps for each kind:

1. It takes the options of a plain app from `compile_commands.json` (the same entry as `mk_app.py`). It writes the
   ESP-IDF paths as `$IDF_PATH/...` and the package paths as `$SDK/...`. It stops at a path of the build PC.
2. It copies `sdkconfig.h` and `picolibc.specs` of the kind.
3. It finds the smallest set of ESP-IDF include folders that gives the same preprocessed text as the full set, for
   the public headers, the header libs and the usual ESP-IDF headers. This set is `[include]` of the flags file
   (about 25 of about 350 folders). `[include_all]` is the full set.
4. It checks the `app_t` layout that `tfn_app.py` reads from `app.elf`.

At the end it stops when a file of the package has a path of the build PC, a private address or a private name. The
include folders of the build that are not ESP-IDF (the managed components, `elf_loader`, `esp_wireguard`, the made
`mbedtls` headers) are not in the package. `sdk.json` lists them.
