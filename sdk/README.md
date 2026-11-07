# TFN OS SDK

TFN OS is the system of the arcade boards (TFN: three four nine, the size of the first board). An app is a file that
the board installs and removes on demand: from the manager store, from an SD card, or from the board page. This
folder has all that you need to make one.

> Status: the ABI is version 1, revision 17 (about 1500 names). The docs, one template (HELLO) and five
> examples. [docs/howto.md](docs/howto.md) tells how to make each kind of game and utility.

## The first app in 10 minutes

1. Build a DEV firmware once (it gives the compile options of each board kind): `.\dev.ps1 -Kinds lcd349`.
2. Build the host tool for the icons once: `sh sdk/tools/host/build.sh lcd349` (clang; it uses the host harness in
   `tests/host/`). Build it again after a new app in `main/apps.json` or a change of `main/`.
3. Put the board in the developer mode (an app file with no signature can run): SETUP > DEVELOPER MODE on the board,
   then tap TURN ON with your finger, or the USB console `apps dev 1`. (The web cannot turn it on: the board page
   `http://<board>/apps` can only turn it off.)
4. Copy `sdk/templates/hello/` to your folder, then build, send and open it:

```
python sdk/tools/mk_app.py sdk/templates/hello/app_hello.c --sym app_hello --name HELLO --kinds lcd349 --nosign --push <board ip> --open
```

The app opens on the board in a few seconds. Change the code and run the same command again (the hot reload).

## Docs

| Document | What it has |
| --- | --- |
| [docs/app_repo.md](docs/app_repo.md), [docs/app_json.md](docs/app_json.md) | An app in its own folder, out of the repo: the SDK package, `app.json`, `tfn_app.py build`. |
| [docs/howto.md](docs/howto.md) | How to make each kind of app: the parts of every app, the work loop, a recipe for each graphics class of a game and each kind of utility, the sound, the SD data pack, the speed, the tests, the store, and the faults that we found. Start here. |
| [docs/api.md](docs/api.md) | The API reference: each firmware name that an app can call (the ABI), with its declaration and its comment, and the types and constants of the main headers. `tools/gen_api_doc.py` makes it. |
| [docs/visual.md](docs/visual.md) | The visual rules: the screens of each board kind, the coordinates, the frame, the fonts, the touch targets, the feel of a game, the speed, the menu icon, the pictures. |
| [docs/behave.md](docs/behave.md) | The behaviour rules: the `app_t`, the order of the calls, the input, the saves, the sound, the screen saver, the settings panel, the languages, the battery, the tasks, the memory, the checks of the board. |
| [docs/checklist.md](docs/checklist.md) | The checklist before an app goes to the store. |
| [docs/graphics.md](docs/graphics.md) | The graphics of the games: the 9 ways to make the pictures (code-drawn 2D up to a Blender stage with live 3D actors), the games of each class, the art tools, the costs on the board, the rules for a new look, and which class to use for a new game. |
| [docs/sound.md](docs/sound.md) | The MP3 sound effects of a game on the SD card: the file format, the CC0 licence rule, the data pack, and the library `libs/tfsfx`. |
| [docs/store.md](docs/store.md) | The store page of an app: the description in `store.json` (English and Hebrew), the rules of the text, `store_desc.py`, and the developer key with the trusted publishers of a board. No app goes into the store without a description. |
| [docs/tools.md](docs/tools.md) | The tools: `mk_app.py`, `abi_gen.py`, `gen_api_doc.py`, `icon_png.py`, the host tools, the board console and the board API. |
| [docs/raw.md](docs/raw.md) | Raw ESP-IDF functions in an app: the extra ABI names, the permission classes, the ABI revision, how to ask for a name that is not in the ABI, and the rules for raw code. |
| [docs/notify.md](docs/notify.md) | Notifications, the background checks of an app (`watch.elf`) and the secret store. |
| [docs/radio.md](docs/radio.md) | The radio sources, the SDR app, the pulse receivers on a pin and the IR receivers. |
| [docs/device-protection.md](docs/device-protection.md), [docs/device-protection-setup.md](docs/device-protection-setup.md) | The optional protected mode of a board (OFF by default): the behaviour, the setup steps, the recovery. |

## What is in this folder

| Path | What it is |
| --- | --- |
| `tools/mk_app.py` | Builds the package of an app for each board kind: `app.elf`, `icon.bin`, `manifest.txt`, `sig` (and `watch.elf` for an app with a background check). Checks the ABI. Can send the app to a board and open it. |
| `tools/sdk_pack.py` | Makes the SDK package (a folder and a zip): with it an app builds in its own folder, out of the repo ([docs/app_repo.md](docs/app_repo.md)). |
| `tools/tfn_app.py` | Builds the package of an app in its own folder from its `app.json`, with the SDK package only (the format of `mk_app.py`). |
| `tools/app_json.py` | Writes `app.json` for an app of the repo (from `main/apps.json` and its `store.json`). |
| `tools/tfn_pkg.py` | The shared steps of `mk_app.py` and `tfn_app.py` (the link and the checks, the ABI check, `icon.bin`, `manifest.txt`, the push). |
| `tools/abi_gen.py` | Makes the ABI (`main/tfn_abi.json`, `main/tfn_abi.inc`): the firmware functions that apps can call. |
| `tools/icon_png.py` | A picture of all frames of an `icon.bin` (a check of the icon). |
| `tools/store_desc.py` | Checks that each app of the store has a description, and sends the descriptions of all `store.json` files to the manager with no new package (`--send`). |
| `tools/gen_api_doc.py` | Makes `docs/api.md` from the ABI and the headers of `main/`. |
| `tools/store_conf.py` | The store setup of the tools: the URL of the manager (`TFN_MANAGER`) and the signer of the store key. |
| `tools/store_data.py` | Sends the data pack of an app (the files for the SD card) to the store of the manager. |
| `tools/store_index.py` | The static store (a web host with no server code): signs the packages and a JSON index (`make`), and checks a store as a person who does not trust the host (`verify`). [docs/store.md](docs/store.md) section 7. |
| `tools/release_pack.py`, `tools/release_check.py` | The files of a public GitHub release (`.github/workflows/release.yml`), and the check of a release and its web site as a stranger sees them (`.github/workflows/release-check.yml`). |
| `tools/release_cfg.py` | The public keys (`main/tfn_release_keys.h`) and the OTA mode of a public release build: ENFORCE with the OTA key of the release, REPORT in a dry run (`.github/workflows/release.yml`). |
| `tools/sd_usb.py` | Reads the files of a board (the card, `/data`) over the USB console. |
| `tools/elf_end_check.py` | Finds the app files that keep an address one past the end of a section (a loop end such as `s_blob + N`): an older loader made it 0 (SPLAT stopped a board). `mk_app.py` runs it on each package. |
| `tools/puzzle/solver.py` | The puzzle solver: it proves the rules of a quest before the build ([docs/tools.md](docs/tools.md)). |
| `tools/device_*.py`, `tools/usb_owner.py` | The tools of the optional protected mode ([docs/device-protection.md](docs/device-protection.md)). |
| `docs/` | The documents (see "Docs" above). |
| `tools/host/` | The host tool that draws the icon frames of an app on the PC, and the entry of each app file. |
| `templates/hello/` | The smallest complete app. |
| `examples/tapper/` | TAPPER, a tap game: the frame, the `MF_*` fonts, touch targets of 44 px or more, the sounds, the saves. |
| `examples/tilt/` | TILT BALL, a ball that rolls with the tilt: the zero rule, walls, a soft camera and a shake, panel items. |
| `examples/counter/` | COUNTER, a tally counter: big keys, RESET with a hold of 1 s, delayed saves, panel items, English and Hebrew. |
| `examples/wifiscan/` | WIFI SCAN, a WiFi scanner: raw ESP-IDF functions (`esp_wifi_scan_start()`), the permission class `wifi.scan`, a task of the app, a list that scrolls, a graph. Only in the store. |
| `examples/watchdemo/` | A background check (`watch.elf`) that sends notifications ([docs/notify.md](docs/notify.md)). |
| `tools/host/raw/` | The PC copies of the raw ESP-IDF headers and a stub of each extra ABI name (the PC draws the icon of an app with them). |
| `out/` | The packages that `mk_app.py` makes (not in git). |

## The rules that the board checks

- An app file calls only the firmware functions of the ABI: the functions of the system and about 800 raw ESP-IDF
  functions (sockets, HTTP, TLS, PSA crypto, NVS, pins, I2C, SPI, PWM, ADC, BLE, the WiFi radio, FreeRTOS). For a
  name that the firmware does not have, `mk_app.py` writes a request; the operator of the manager approves it, and the
  next release has it ([docs/raw.md](docs/raw.md)).
- A raw name that can do harm has a permission class. The STORE asks the user before it installs such an app, and the
  board links the name only for an app whose manifest has the class. The operator of the manager changes the classes in
  the tab PERMISSIONS, with no firmware (a signed policy that the boards take).
- The store gives an app only to a board whose firmware has the ABI revision of the app (`rev=` in the manifest).
- A board runs an app file only with the signature of the manager, or in the developer mode. A developer can also
  sign with an own key (`--dev-key`): the board shows such an app as NOT REVIEWED until the store signs it, or until
  the board owner trusts the key as `test` in SETUP > TRUSTED PUBLISHERS ([docs/store.md](docs/store.md), section 6).
- An app on the SD card is installed: put the package folder at `/apps/<id>/` of the card (manifest.txt, app.elf,
  icon.bin, and sig when it is signed). The board reads the card at the start, and when FILES mounts a card.
- To get a signature, send your package (made with `--nosign`) to the operator of the store that you use. The
  operator checks it, signs it with the key of that store, and gives it back (a .zip for the card) or puts it into the
  store. Use an
  id of your own: an app with the id of a store app is that store app for the manager.
- An app that stops the board two times without a clean close is stopped until it is installed again.
- When an app closes, the system frees the memory that the app took with `malloc` / `heap_caps_malloc` and closes its
  files. Keep nothing for the next open in memory: use `store_set()` / `store_get()` (the saves) for that.
- A task of the app must end in `leave()`. While a task of the app runs, its code stays in memory.
