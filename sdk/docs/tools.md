# TFN OS tools

The tools of the SDK, the host picture tools, the board console and the board API. Run the commands from
`tfn-os/`.

## What you need first

| Need | How |
| --- | --- |
| A DEV build of each board kind | `.\dev.ps1 -Kinds lcd349` (PowerShell; more kinds: `-Kinds lcd349,cores3,am18,watcher,tab5`). It writes `build_dev/<kind>/`. `mk_app.py` takes the compile options of each kind from `build_dev/<kind>/compile_commands.json`, and skips a kind with no DEV build. Do not run two `dev.ps1` at the same time. |
| The host icon tool | `sh sdk/tools/host/build.sh [lcd349 cores3 am18 watcher]` (clang). It writes `sdk/tools/host/iconshot_<kind>.exe`, `iconlib_<kind>.a` and `iconflags_<kind>.txt` (for a source out of `main/`). It uses the host harness of `tests/host/` (`stubs.c`, `common/`, `inc349/`, `incs3/`, `incwt/`: in git). The `am18` kind also needs an include folder of its own frame (`incam/`), which is not in this repo. The Tab5 has no own host tool: it uses the icon of `cores3`. Run it again after a new app in `main/apps.json` or a change of `main/`. |
| A signature | The store key belongs to the person who runs the store and its boards. Sign with a local P-256 key (`--sign-key <pem>`): the boards must have its public key (`TFN_STORE_PUBKEY_BYTES` in `main/tfn_local.h`, see `main/tfn_local.example.h`). With no `--sign-key`, `mk_app.py` asks the signer of the store setup (`sdk/tools/store_conf.py`: an optional site module of the store; the public tree has none). With no key, use `--nosign` and the developer mode of the board. |

## mk_app.py: the package of an app

`python sdk/tools/mk_app.py <app> [options]`

`<app>` is the name or the symbol of an app of `main/apps.json` (`DIVE`, `app_dive`), or the path of a source file
(`.c` or `.cc`) out of `main/`.

| Option | What it does |
| --- | --- |
| `--kinds lcd349,cores3` | Only these board kinds. Default: each of the 5 kinds (`lcd349`, `cores3`, `am18`, `watcher`, `tab5`) that has a DEV build. A source out of `main/` cannot get a `tab5` package now (no `iconlib_tab5.a`): give `--kinds` with no `tab5` when `build_dev/tab5` exists. |
| `--nosign` | No signature (`sig`). A board runs the file only in the developer mode. |
| `--sign-key <pem>` | Sign with this local P-256 key (the private key of the store key of your boards). |
| `--dev-key <pem>`, `--dev-name <NAME>` | Sign also with your developer key: `publisher=` in the manifest, the file `sigs` and `out/<id>/publisher.txt` (the key file for SETUP > TRUSTED PUBLISHERS of a board). A board that does not trust the key as `test` shows NOT REVIEWED until the store signs the same manifest ([store.md](store.md), section 6). |
| `--password <pw>` | With `--push`: the web password of the board (default: the environment variable `TFN_BOARD_PASSWORD`). |
| `--firmware-build <dir>` | Use the compile options of this build folder, not of `build_dev/<kind>/` (one `--kinds` value only; for the protected builds of [device-protection.md](device-protection.md)). |
| `--push <ip>` | Send the package to the board at this address (`POST /api/apps`: `op=put` for each file, then `op=commit`). Before the first request, the tool checks the package: `manifest.txt` and each file that it names, with its SHA-256. A folder that is not complete (for example `app.elf` with no `manifest.txt`) never goes. Each file goes as `application/octet-stream`, and the tool checks the byte count in each answer. `--publish` does the same check. |
| `--card` | With `--push`: install to the SD card (`/sdcard/apps/<id>/`), not to the board storage (`/data/apps/<id>/`). |
| `--open` | With `--push`: go to the menu first, then open the app after the install (the hot reload: the board cannot replace an open app). |
| `--publish [url]` | Send the packages to the store of the manager (`api/store/upload`). With no url: the manager of the store setup (`sdk/tools/store_conf.py`: the environment variable `TFN_MANAGER`; the tool stops when there is none). It stops when the app has no English description ([store.md](store.md)). |
| `--notes "..."` | What is new in this version (one line). The manager sends it with the notice of the update. |
| `--watch <src>`, `--watch-sym <sym>`, `--watch-every <min>` | A source out of `main/`: the source and the `watch_t` of its background check, and the minutes between two checks (default 15). It gives the package a `watch.elf` ([notify.md](notify.md)). |
| `--policy <url or file>` | The published permission policy (see below). |
| `--desc "..."`, `--desc-he "..."` | The description of the store in English and in Hebrew, in place of the texts of the app's `store.json` ([store.md](store.md)). The tool checks the length (300 characters at most) and the characters (the board fonts), and the size of `manifest.txt` (4096 bytes at most: a board does not load a bigger one). |
| `--ver <x.y.z>` | The version in the manifest. Default: `version.txt`. |
| `--sym app_<id>` | A source out of `main/`: the symbol of its `app_t`. Needed. |
| `--name <NAME>` | A source out of `main/`: the menu name. Needed. |
| `--group GAMES` | A source out of `main/`: the menu group: `GAMES` (default), `UTIL` or `SETTINGS`. |
| `--noabi` | No ABI check (only for a test of the loader). Do not use it for a real app. |
| env `MK_APP_KEEP=1` | Keep the symbols of `app.elf` (no strip), for a backtrace. |

The steps for each kind:

1. `app.elf`: the sources of the app (and of the apps in its `needs`, the app of its `with`, and the libraries of its
   `link`) with the compile options of the DEV build, plus
   `-fPIC` and `sdk/tools/host/elf_entry.c`, linked as a shared object with `sdk/tools/host/app.ld`, then stripped. The
   tool stops if the file has a section that the loader cannot place. The compiler also gets the include folders of
   each component of the firmware build (`sdk/out/_includes_<kind>.rsp`, a response file: about 380 folders), so an app
   can include any ESP-IDF header. The `uses` modules are NOT compiled into the app file: an app that
   uses a lib whose names are not in the ABI (`kscroll`) includes it itself. The modules `tfsfx`, `tfphys`, `tfnet`,
   `tfmqtt`, `zimread` and `zstd` are in the firmware: their names are ABI names ([sound.md](sound.md) section 3).
2. The ABI check: each name that `app.elf` needs must be in `main/tfn_abi.json` for this kind. Else the tool stops,
   gives the names, and writes the request for them: `sdk/out/<id>/abi_request.json` (see [raw.md](raw.md)).
3. `icon.bin` and the `app_t` flags: `iconshot_<kind>.exe` draws the icon with the `draw_icon()` of the app.
4. `manifest.txt` (id, name, ver, code, abi, rev, perms, kind, group, emoji, saves, the flags, bytes, ram, the hashes;
   also `new`, `notes`, `desc`, `desc_he`, and `watch`, `watch_bytes`, `watch_sha256` for an app with a background check),
   and `sig` (the ECDSA P-256 signature of the store key over the manifest). `rev` is the highest ABI revision of the
   names of the app; `perms` are the permission classes of its names (see [raw.md](raw.md)). The tool finds both from
   the names: you do not write them. The classes are the defaults of the firmware; `--policy <manager URL or file>`
   uses the published policy of the manager instead (for a file with no signature; the manager writes `perms=` and
   `rev=` again when it signs).

On Windows, a GCC command longer than 28000 characters uses a temporary response file. The tool quotes each
argument and removes the file after success or failure. This also applies to long source and include lists.

The output: `sdk/out/<id>/<kind>/`. The tool makes the new package in `sdk/out/<id>/<kind>/_new/` and moves its files into place after the last step: a build that stops keeps the old package complete. The tool prints one line for each kind: the size of `app.elf`, the RAM, the count
of ABI names, the frames and the colours of the icon. The manifest of STACK for the 3.49 (the hashes cut):

```
id=stack
name=STACK
ver=1.0.35
code=262911527
abi=1
rev=0
perms=
kind=lcd349
group=GAMES
emoji=🏗️
saves=stack_
landscape=0
native=0
round=0
round_fit=0
wheel=0
key=0
run=0
needs=0
bytes=6504
ram=10908
elf_sha256=...
icon_sha256=...
icon=128,16,125
```

A source out of `main/` gets its icon and its flags too: `mk_app.py` links the source with
`sdk/tools/host/iconlib_<kind>.a` (all the firmware code for the host, made by `build.sh`) into
`iconext_<kind>.exe`, and runs it. The app is the only app of that tool, so its `name` can be any name. Run
`sh sdk/tools/host/build.sh <kind>` again after a change of `main/`. The manifest takes `landscape` and the other flags
from the `app_t`. For an app that calls raw ESP-IDF names, the tool adds
`sdk/tools/host/raw/` (short PC copies of the ESP-IDF headers, such as `esp_wifi.h`) and `raw/raw_stubs.c` (a stub
for each extra ABI name). The PC only draws the icon: the stubs never run.

Examples:

```
python sdk/tools/mk_app.py sdk/templates/hello/app_hello.c --sym app_hello --name HELLO --kinds lcd349 --nosign --push <board ip> --open
python sdk/tools/mk_app.py DIVE --kinds lcd349 --push <board ip> --card
python sdk/tools/mk_app.py DIVE --publish
```

## sdk_pack.py, tfn_app.py, app_json.py: an app in its own folder

An app can build out of the repo, with the SDK package only ([app_repo.md](app_repo.md); the manifest:
[app_json.md](app_json.md)).

| Tool | What it does |
| --- | --- |
| `python sdk/tools/sdk_pack.py --build-root build_dev [--src .] [--kinds ...] [--out sdk/out/tfn-sdk]` | Makes the SDK package (a folder and a zip): the public headers `main/tfn/*.h`, `boards/<kind>/board_caps.h`, the header libs and the source libs, the entry of an app file, the link scripts, `tfn_abi.json`, and for each kind `kinds/<kind>.flags` (the compiler and the options of the DEV build, the ESP-IDF paths as `$IDF_PATH/...`, the smallest set of ESP-IDF include folders). |
| `python <sdk>/tools/tfn_app.py build <folder> [--kinds ...] [--sdk <package>] [--idf <esp-idf>] [--icon <png>] [--push <ip>]` | Builds the package of an app from its `app.json`, with the package only. The output format is the format of `mk_app.py`. `--list-includes` writes each header that the compiler opened. |
| `python sdk/tools/app_json.py <APP>` | Writes `app.json` for an app of the repo (from `main/apps.json` and its `store.json`), and lists the includes that stop a build out of the repo. `--src <file.c> --sym --name` does the same for a source out of `main/apps.json`. |

`mk_app.py` and `tfn_app.py` share the steps of `tfn_pkg.py`: the link and the checks of `app.elf`, the ABI check,
`icon.bin`, `manifest.txt`, the local signature, the push to a board.

## abi_gen.py: the ABI

`python sdk/tools/abi_gen.py [--check] [--major] [--kinds lcd349,...] [--extra-only [--extra FILE]]`

It finds the firmware names that the apps of `main/apps.json` call (from the objects of the DEV builds), adds the raw
ESP-IDF families of `main/tfn_abi_raw.json` (each global function of the firmware of a kind that matches a family and
no deny pattern; a new one gets the class of its family) and the extra names of `main/tfn_abi_extra.json`, and writes
`main/tfn_abi.json` and `main/tfn_abi.inc`. The table of `tfn_abi.inc` has no names: `ELFSYM(name, lo, hi)` is the
FNV-1a 64 hash of the name; two names with one hash stop the tool. `--extra-only` needs no DEV build: it adds only the
extra names (the names of the approved ABI requests, [raw.md](raw.md) section 6). The `core` apps (MENU, STORE, FILES and the other system apps) are not
units: their calls (for example `tfn_install()`) do not go into the ABI.

| Option | What it does |
| --- | --- |
| (none) | Check, then write the new lists. Inside one ABI version, names are only added. |
| `--check` | Only check. Exit 1 when a name is not in the ABI. |
| `--major` | A new ABI version: names can go, the revision starts at 0. The installed apps of the old version stop (`bad_abi`). |
| `--kinds` | Only these kinds. The other kinds keep their old lists (the extra names go to all kinds). |

Each run that adds names gives the ABI the next revision (`rev` in `tfn_abi.json`, `TFN_ABI_REV` in
`tfn_abi.inc`), and each new name that revision (`revs`). The permission classes (`perms`: the bit and the text of
the STORE) and the class of each extra name (`need`) also go into both files. The tool stops when a class changes its
bit or a name changes its class inside one version (installed apps would break). It warns when an extra name has no PC
stub in `sdk/tools/host/raw/raw_stubs.c`.

A new ABI name needs a new firmware on the boards before an app can use it. The store gives an app only to a
board whose firmware has its revision. A new name starts as an ABI request (a GitHub issue); [raw.md](raw.md)
section 6 has the full procedure.

## store_desc.py: the descriptions of the store

`python sdk/tools/store_desc.py [--manager URL] [--send]` reads each `store.json` of `apps/` and `sdk/examples/`
([store.md](store.md)) and the app list of the store of the manager. It lists each app of the store that has no
description, or a description with a fault (too long, a character that the board fonts do not have), and each app with
no Hebrew text. The exit code is 1 when an app has no description or a fault. `--send` sends all the good descriptions
to the manager (`POST api/store/desc`): the boards get them with their next catalog, with no new version of an app.

## gen_api_doc.py: the API reference

`python sdk/tools/gen_api_doc.py` writes [api.md](api.md) from `main/tfn_abi.json` and the headers of `main/`.
`--check` exits 1 when `api.md` is not current. Run it after each change of the ABI or of a header comment.

## tfn.py build --version: the firmware of a release

`python tfn.py build <kind>` gives each build the version text `DEV-<UTC time>-<8 random hex digits>`. The maintainer
of a release makes a release build with the version of the release:

```
python sdk/tools/release_cfg.py header --out main/tfn_release_keys.h --store-pub <hex> --ota-pub <hex>
python tfn.py build lcd349 --version 1.2.3 --build-dir build_rel/lcd349 --store-pub <hex> --ota-pub <hex>
```

- The version has the form of a release: `<major>.<minor>.<patch>`, with at most 4 digits for major and 5 digits for
  minor and patch (the tag `v1.2.3` of `.github/workflows/release.yml`). `tfn.py` stops at another form.
- The boards, the manager and a pool manager take an image with a release number as that release. So `tfn.py` stops
  before the build when the tree does not have `main/tfn_release_keys.h` (the public keys of the release, from
  `release_cfg.py header`), or when that file is not the header of the keys of `--store-pub`, `--ota-pub` and
  `--ota-pub2` (each key: 130 hex digits that start with `04`, or a file that holds them; a point of P-256). A file
  with TEST keys, a file of an earlier build, a key less or more, or other lines stop the build.
- A release build starts with no old sdkconfig, as the job `firmware` of `release.yml` does: `tfn.py` stops when the
  build folder has the sdkconfig or the CMake cache of another build (for example the DEV folder `build_dev/<kind>`).
  A folder that a release build made has the mark `tfn_release_folder.txt`: each later release build there removes
  the sdkconfig first.
- The tree must be a clean git clone. `tfn.py` stops when the tree has `main/tfn_local.h` (the setup file of a private
  tree: `main/tfn_defaults.h` reads it first, and its values would go into the image), or another file that git
  ignores in the source folders (`main/`, `apps/`, `libs/`, `components/`, `boards/`, `third_party/`): a build takes
  such a file too. Only the keys of the release, the caches of Python and the files of the file managers can be there.
- The tree must be the same as its commit. `tfn.py` stops at each change that `git status --untracked-files=all`
  shows: a changed, added or removed tracked file, or an untracked file that git does not ignore (only the keys of the
  release and the files of the caches and of the file managers can be untracked). It also stops when the build has
  `--select` or the environment variable `TFN_DEVICE_PUBLIC_HEADER`: they change the sources of the build.
- `tfn.py` does the steps of the job `firmware` of `release.yml`: `idf.py reconfigure`, then `release_cfg.py force`
  (the `force_cfg` and `release_cfg` values of `boards.json` and the OTA mode go into the sdkconfig of the build
  folder), the build, and `release_cfg.py check` of `config/sdkconfig.h`. A build that does not have these values
  stops: `tfn_build.json` is not written, and the image gets the name `tfn_os.bin.failed`. `--ota-mode enforce`
  is the default (the board installs only an update with a good signature); `--ota-mode report` is for a dry run with
  TEST keys.
- The image gets the version text (`PROJECT_VER`), and `tfn.py` checks it after the build. After the build,
  `tfn.py` also checks the tree again (no change, the same commit at `HEAD`) and that the image holds each public key
  (`release_cfg.py keys`): a failed check gives `tfn_os.bin.failed` and no `tfn_build.json`. `tfn_build.json` in
  the build folder has the channel `RELEASE` (`TEST` for `--ota-mode report`: such an image is not an image of a
  release), the OTA mode, the commit (`git rev-parse HEAD`) and the key IDs.
- A TEST build (`--ota-mode report`) has the version of the option in the image: it must never leave the computer of
  the developer. `tfn.py` stops when a TEST build has the version of a release tag (`v<version>`).
- All kinds of one release must have the same version: `sdk/tools/release_pack.py` stops when a kind has another
  version. `sdk/tools/fw_sign.py sign <image> --version 1.2.3` checks the version of the image before it signs it, and
  `release_cfg.py keys` checks that the signed image holds each public key of the release.

## icon_png.py: a picture of an icon

`python sdk/tools/icon_png.py sdk/out/<id>/<kind>/icon.bin icon.png` makes one PNG with all frames of the icon (8 in a
row). A hole shows as a check pattern.

## iconshot: the icon on the PC

`sdk/tools/host/iconshot_<kind>.exe "<APP NAME>" <size> <frames> <ms> <out.raw>`

`mk_app.py` runs it (the `tab5` kind uses `iconshot_cores3.exe`). It links all apps of `main/apps.json`, calls `init()`
of each app, and draws the icon of one app
two times for each frame (on black and on white: a pixel that is not the same is a hole). It prints the `app_t` flags
as manifest lines.

## tshot: pictures of the app screens on the PC

`tshot_<b>.exe "<APP NAME>" "<steps>" <folder>/<prefix>`

This host tool of the project is not in this repo. A developer with a clean clone uses the simulator
(`simulator/run.py`, `simulator/smoke.py`) and the board pictures (`shot`, `/api/screen`) instead. The tool builds
with clang, ASan and UBSan, for the frames `349`, `s3`, `am` and `wt` (no Tab5). `349` = the 3.49, `s3` =
the CoreS3 frame (the AMOLED shows the same frame x1.4), `am` = the native AMOLED frame (apps with `native`), `wt` =
the Watcher. It links the apps of `main/apps.json` only.

A `;` comes between two steps. The points are logical points of the app frame.

| Step | What it does |
| --- | --- |
| `k KEY VALUE` | Write a saved value (before the `init` of the apps). |
| `e` | Open the app (done before the first other step when the script has no `e`). |
| `f N` / `w MS` | N frames of 33 ms / frames for MS. |
| `t X Y` | A tap. |
| `T X Y MS` | A press of MS (an `IN_TAP` only when MS <= 300). |
| `l X Y` | A long press. |
| `d X Y`, `m X Y`, `u X Y` | `IN_DOWN`, `IN_MOVE`, `IN_UP`. |
| `g X Y MS` | A drag from the last point to X Y in MS. |
| `p X Y MS` | A finger in the touch points (for apps that read `input_points()`). |
| `P` / `R N` | A push of the wheel / N steps of the wheel (the Watcher). |
| `s NAME` | Write the frame now: `<prefix>_<NAME>_<W>x<H>.raw`. |
| `S` | Print the size of the frame. |

`python tests/host/png.py <folder>` makes PNG files of the `.raw` frames.

## The board console

The console is on the USB port of the board, at 115200 baud. A plain open of the port with pyserial resets the board:
open it with DTR and RTS low.

| Command | What it does |
| --- | --- |
| `apps` | The installed apps (id, version, place, state, bytes), the ABI, the board kind, the developer mode, the safe mode. |
| `apps policy [<name> \| reset]` | The class of an ABI name on this board (free, a class, or "not in the ABI"); `reset` = the default policy. |
| `apps scan` | Read the app folders again (a card in, files copied by hand). |
| `apps rm <id>` | Remove an installed app. |
| `apps dev 0` / `apps dev 1` | The developer mode off / on. The USB cable counts as the finger of the consent: on the screen, SETUP > DEVELOPER MODE asks first, and the web can only turn it off. |
| `apps cat` | The catalog of this board from the manager store (id, version, bytes, store state). |
| `apps get <id> [card]` | Download an app from the manager store and install it. |
| `open <APP NAME>` | Open an app (no case in the name). |
| `menusel <APP NAME> [hold]` | The menu goes to the app; `hold` = the action of a hold (FAVORITE / REMOVE). |
| `shot` | The next frame: a line `SHOT <w> <h>`, then lines that start with `#S` (base64 of RGB565, high byte first; 1024 characters a line), then `SHOT END`. |
| `mem` | The free internal RAM (now, the least, the biggest block) and the free PSRAM. `mem tasks`: each task and its stack (internal RAM or PSRAM, the size, the bytes never used). `mem heap`: each region of the internal RAM. |
| `quiet 1` / `quiet 0` | The speaker off for a silent test / the settings again. ALWAYS end a test with `quiet 0`. |
| `store <key> [value]` | Read or write one saved value. A key that does not exist reads as 0. |
| `storeall` | All saved values that exist. |
| `storedel <key>` | Remove one saved value. |
| `tap x y` / `ltap x y` | A tap at a physical / logical point. A consent does not take it. The console command of each consent: ALLOW of a key = `pub allow <key ID>`, ALLOW of a pool JOIN = `pool allow <6 digits>`, LEAVE of a pool = `pool leave`, an app with a new permission = `apps get <id> [card]`, the developer mode on = `apps dev 1`. |
| `finger x y ms`, `fmove x y`, `finger up` | A virtual finger through the real touch path. A consent does not take its tap. |
| `panel` | The settings panel comes down. |
| `wake` | The screen on. Send it before each `shot`: with the screen off no new frame comes. |
| `ls [dir]`, `df` | The files with a CRC-32 (default `/data`), the free space. |
| `bat` | The battery. |
| `key [ms]` | The BOOT key of the 3.49 down for ms (default 80). |
| `finger2 x y ms`, `fmove2 x y` | A second virtual finger. |
| `orient [0..3]` | Set this screen turn (no auto turn until a restart; a test of the turned layouts); `orient` alone shows the turn now. |
| `menu` | Print the menu layout of the board (`open menu` opens the menu). `menu reset` puts the built-in layout back: it changes the board. |
| `sleepnow [off]` | The screen saver now (a test). |
| `nvs` | The NVS use, and the spill store of a full NVS. |
| `clock` | The time source, the time, the time of the clock chip. |
| `sdusb` | The USB file protocol of `sdk/tools/sd_usb.py`. |

The log shows `<APP NAME>: <x> fps` each 300 frames, and the lines of the loader:
`loaded <id> <ver> (board): <n> bytes in <ms> ms, PSRAM <n> bytes, internal RAM <n> bytes` and
`closed <id>, PSRAM free <n>`.

## The board API

| Request | What it does |
| --- | --- |
| `GET /api/apps` | JSON: `kind`, `abi`, `rev` (the ABI revision of the firmware), `dev`, `safe`, `policy`, `board` and `card` (the space), `mgr`, `profile`, `inst` and `max` (the installed apps and the limit, 160), `fw`, `layout`, `crash` and `crash_reason` (the last app that stopped the board), `apps` (id, name, ver, code, group, place, state, bytes, ram, loaded, req, rev, perms: the bits of the permission classes, form, watch). |
| `POST /api/apps?op=put&id=<id>&file=<name>[&place=card]` | The body is one file of the app (`manifest.txt`, `app.elf`, `icon.bin`, `sig`, `watch.elf`). It goes to `/data/apps/.tmp_w-<id>/` (with `place=card`: `/sdcard/apps/.tmp_w-<id>/`). Send `manifest.txt` first: it starts the upload, and it removes the files of an older one and the upload folder of the same id in the other place. The board refuses another file when no `manifest.txt` of this id and place came before it (since the start of the board), or when that `manifest.txt` is not in the folder now. The answer is `ok` only when the file is in the folder with all its bytes after the close (`"msg": "app.elf: 505764 bytes in 5300 ms"`). A put that fails removes the upload folder: send the package again from `manifest.txt` (a fault of `sigs` removes only that file). An upload with no put for 2 minutes ends, and so does the upload with the oldest put when 4 uploads run and a new one starts: the board removes its folder, and its next put or its commit tells the cause ("the upload of <id> ended (no put came for 120 s) ..."). Give each put and the commit the same `place`. |
| `POST /api/apps?op=commit&id=<id>&place=board\|card` | Check the files, then install them. An install that fails removes the upload folder. When `manifest.txt` is not there, the answer tells which files the folder has, or the folder of the other place that has a `manifest.txt`. A card commit takes the upload of the board storage when no upload of the id runs on the card (puts with no `place`). When another install, move or remove of the same app runs on the board now (the STORE, the console), the commit stops: "<id>: another install, move or remove of this app runs now: try again". |
| `POST /api/apps?op=move&id=<id>&place=board\|card` | Move an installed app to the other place. |
| `POST /api/apps?op=rm&id=<id>` | Remove an installed app (an app that the profile needs: with `tok=<the token>`). |
| `POST /api/apps?op=dev&on=0` | The developer mode off. It needs `tok=<the token>` when the manager set a token. `on=1` is refused: the developer mode turns on only at the board (SETUP > DEVELOPER MODE, then TURN ON with a finger) or with the USB console (`apps dev 1`). |
| `POST /api/apps?op=scan` | Read the app folders again. |
| `GET /apps` | The page of the installed apps: OPEN, MOVE, REMOVE, an upload, the developer mode off. |
| `GET /api/screen` | The next frame: RGB565, high byte first, `gfx_w` x `gfx_h` (the headers `X-W`, `X-H`). No body and `X-Screen: off` or `busy` when there is no frame. |
| `GET /api/ctl?open=<APP NAME>` | Open an app (and turn the screen on). Also `menu=1`, `screen=0\|1`, `quiet=0\|1`, `tap=x,y`, `long=x,y`, `swipe=x1,y1,x2,y2` (logical points). |

Each `POST /api/apps` answers `{"ok": true|false, "msg": "..."}`. A board with a web password asks for HTTP Basic
authentication (401 with no password, 429 after many wrong tries): `mk_app.py --password` sends it.

The states of an installed app (the `state` field):

| State | What it tells |
| --- | --- |
| `ok` | It has a good signature of the store key. It can run. |
| `dev` | It has no good signature, and the developer mode is on. It can run. |
| `bad_sig` | No good signature, and the developer mode is off. |
| `bad_abi` | It is for another ABI version, or it needs a newer firmware (its `rev` is higher than the ABI revision of the board, or it has a permission class that the firmware does not know). |
| `bad_file` | `app.elf` is not there, its size is wrong, or the manifest has no `elf_sha256`. |
| `stopped` | It stopped the board two times with no clean close. Install it again. |

A package for another board kind does not show in the list at all.

## Protected USB maintenance

Create a new owner key set with `python sdk/tools/device_keys.py --out <new-private-directory>`.
This creates files only and refuses existing directories. It does not enroll a
board. See [device-protection.md](device-protection.md) for the default behavior,
per-device setup, backup requirements and protected firmware update limits.
Use the [setup and integration checklist](device-protection-setup.md) before enrollment.

On a protected board, set `TFN_ADMIN_KEY` to the owner's P-256 private PEM, or
pass `--key`. This key stays on the PC. Example commands:

```
python sdk/tools/device_admin.py --port PORT --key owner.pem command apps
python sdk/tools/device_admin.py --port PORT --key owner.pem ls /data
python sdk/tools/device_admin.py --port PORT --key owner.pem get /data/home.txt --out home.txt
python sdk/tools/device_admin.py --port PORT --key owner.pem install PACKAGE --backup BACKUP --sign-key app-sign.pem
python sdk/tools/device_admin.py --port PORT --key owner.pem update signed-firmware.bin
python sdk/tools/device_admin.py --port PORT --key owner.pem flash-backup flash.bin
```

An install backs up the old package first, uploads a staging package, verifies
its CRC, then uses the native signature/ABI/hash checks before replacement.
An update writes only the inactive OTA slot. Size, SHA-256, board identity and
native firmware signature checks must pass before boot selection changes.
An incomplete update is abandoned after 30 seconds. The active slot is kept.

`sd_usb.py --admin-key owner.pem` keeps the existing SD download commands usable. (`sd_usb.py` reads files of the
board over the USB console with no WiFi: `python sdk/tools/sd_usb.py --port <COM> ls|get|pull [path]`, options
`--baud`, `--timeout`, `--retries`; `pull` takes `--pattern`.)
The `maint` protocol uses USBFS1 framing and 512-byte chunks. File paths are
hex-encoded absolute UTF-8 paths below `/data` or `/sdcard`; traversal, embedded
NULs and other roots are rejected. All maintenance operations require owner
authorization. `admin challenge` returns a device MAC and a random nonce;
`admin auth` supplies raw P-256 `r || s` over `TFNADMIN1:<mac>:<nonce>` using
ECDSA-SHA256. Challenges expire after ten seconds and cannot be reused.

Offline checks are under `tests/device_guard`: `test.py` checks policy and
session limits, `test_host.py` checks real challenge signatures, and
`test_vault.py` drives the production C vault with real AES-GCM and file faults.
Pass `--cc PATH_TO_CC` (or `--cc PATH_TO_ZIG cc`) to C-based tests.

`device_build.py` makes a checked, owner-signed protected build from an immutable
source capture. `device_provision.py` implements `inspect`, `backup`, offline
`prepare`, virtual/physical `activate`, combined USB `enroll`, and final-receipt
`finish`. Use the exact commands and recovery limits in the owner guide. This
is an opt-in USB procedure for a stock-layout 16 MiB lcd349, never an OTA
enrollment procedure. `mk_app.py --firmware-build BUILD --kinds lcd349` uses that
build's compile commands and conditional ABI configuration for app packages.

Additional checks in `tests/device_guard`: `test_abi.py` checks all target
symbol sets and OFF fallbacks; `test_image.py` checks real IDF NVS/FAT and crypto
round trips; `test_enroll.py` drives official virtual eFuses through phase cuts;
`test_provision.py` checks inspection and final-receipt recovery.
`test_bundle.py` accepts a synthetic protected build, keys, separate key backup,
AI package and new output directory. It tests the full prepared image and
virtual enrollment without a mock of the migration verifier or any USB access.
`test_image.py` and `test_enroll.py` need the Python of ESP-IDF 6.1 (esptool 5 and
`esp-idf-nvs-partition-gen`): run them with that Python. CI does not run the tests
of this folder.

## The puzzle solver (`sdk/tools/puzzle/solver.py`)

A design tool for the puzzles of a game: it proves a design before the game is built. A puzzle is any object with
`start()`, `succ(state)` (each move: its cost, 0 = a tap that takes no time or 1 = a step, a label and the next
state) and `is_goal(state)`. The tools:

| Tool | What it gives |
| --- | --- |
| `solve(p)` | The cheapest solution (the taps cost 0, the steps cost 1). |
| `reachable(p)` | All the states that the player can come to. |
| `dead_ends(p)` | The states from which the goal cannot be reached (a player stuck for ever). A good design has none. |
| `necessary(p, labels)` | For each move: is the puzzle still solvable without it? (each puzzle of the design must be necessary) |
| `count_solutions(p, extra, cap)` | The count of the different solutions, up to `cap` (a puzzle with one way only can be too hard). |
| `report(p, labels)` | All of the above as text. |

`WalkPuzzle` is a base for the games where the player walks between screens: the paths with a test each, the taps
in a screen, a clock that each step moves (for example the seasons of a year) and the rules of each step. `State`
keeps the named fields of a state (`s.wind`, `s.set(wind=2)`). `python sdk/tools/puzzle/solver.py` runs the self test.
