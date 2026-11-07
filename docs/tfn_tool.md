# The `tfn` tool

`tfn.py` (at the root of the repo) is one tool for the developer work of TFN OS: the check of your computer, the
firmware build, the USB flash, the console, the host tests, the simulator and the apps. It runs on Windows, Linux and
macOS with Python 3.9 or newer. It uses only the standard library, plus `esptool` and `pyserial` for the commands that
use a serial port.

The facts of each board kind (the chip, the sdkconfig defaults, the partition table, the flash size, the USB ID of the
console, the frame, the known MD5 values, the flash plan) are in `boards.json`. `tests/boards/check_boards.py` checks
that file against the other files of the repo.

The Windows wrapper `dev.ps1` stays as it is. `tfn` does not replace it now.

## Start

```
pip install esptool pyserial
. $IDF_PATH/export.sh            (Windows: the ESP-IDF 6.1 PowerShell profile or export.ps1)
python tfn.py doctor
```

`tfn doctor` shows the Python, `esptool` and `pyserial`, the ESP-IDF (`IDF_PATH`, its version, its Python, the cross
compilers), git, the host compiler (clang or gcc), `sh`, `node`, `clang-format` 22.1.8 and, on Linux, the rights of
the serial ports. A line with FAIL stops the work. A line with WARN tells you which command needs that tool.

On Linux, your user must be in the group of the serial ports (`dialout` on most systems):
`sudo usermod -aG dialout $USER`, then log in again.

## The commands

| Command | What it does |
|---|---|
| `tfn doctor` | Checks the tools of this computer. |
| `tfn boards` | Shows the board kinds of `boards.json`. |
| `tfn ports [--mac] [--all]` | Shows the serial ports with their USB VID:PID and the kinds that can have that ID. It opens no port. `--mac` reads the MAC of each board with esptool: this resets the board. |
| `tfn build <kind> [--version X.Y.Z --store-pub K --ota-pub K [--ota-mode M]] [--dry-run]` | Builds the firmware with `idf.py` into `build_dev/<kind>`. The sdkconfig of the kind is `build_dev/<kind>/sdkconfig`. `--version` makes a release build (the default is a DEV build with the version `DEV-<time>-<random>`). |
| `tfn flash <kind> --port P [--first-time] [--dry-run]` | Writes the build to a board by USB. |
| `tfn monitor --port P` | Shows the console of a board. A line that you type goes to the console. DTR and RTS stay low, so the board does not reset. |
| `tfn test [--quick] [--only a,b] [--list]` | Runs the host checks of CI. `--quick` does not build the fuzz. |
| `tfn sim [--headless] [args]` | Starts the simulator (`simulator/run.py`). `--headless` runs the smoke test of all apps with no window and no sound (`simulator/smoke.py --headless`). The other arguments go to the script. |
| `tfn app new <id> [--dir D] [--name N]` | Makes a new app folder from `sdk/templates/hello`: `app_<id>.c` and `store.json`. |
| `tfn app build <folder or name> [args]` | Builds an app file with `sdk/tools/mk_app.py`. The other arguments (for example `--kinds lcd349 --nosign --push <board>`) go to `mk_app.py`. |

Each command that changes a board or runs a long tool has `--dry-run`: it shows the commands and does not run them.

## The build

`tfn build <kind>` runs:

```
idf.py -C <repo> -B build_dev/<kind> -D PROJECT_VER=<version>
       -D SDKCONFIG=build_dev/<kind>/sdkconfig -D SDKCONFIG_DEFAULTS=<the defaults of the kind>
       -D ARCADE_SELECT= -D ARCADE_DEVICE_PUBLIC_HEADER= [-D IDF_TARGET=esp32p4] build
```

- The version text of the image (`PROJECT_VER`) is `DEV-<UTC time>-<8 random hex digits>`, a new one for each build.
  `--version X.Y.Z` makes a release build with the version of a release: `<major>.<minor>.<patch>`, with at most 4
  digits for major and 5 for minor and patch (the tag `v1.2.3` of `.github/workflows/release.yml`). All kinds of one
  release need the same version. The boards, the manager and a pool manager take an image with a release number as
  that release, so `tfn` makes such an image only as the job `firmware` of `release.yml` does:
  - The tree must be a clean git clone: `tfn` stops when the tree has `main/tfn_local.h` (the setup file of a private
    tree) or another file that git ignores in `main/`, `apps/`, `libs/`, `components/`, `boards/` or `third_party/`
    (a build takes such a file). Only `main/tfn_release_keys.h`, the caches of Python and the files of the file
    managers can be there.
  - The tree must be the same as its commit: `tfn` stops at each change that `git status --untracked-files=all` shows
    (a changed, added or removed tracked file, an untracked file that git does not ignore). Only
    `main/tfn_release_keys.h` and the files of the caches and of the file managers can be untracked.
  - `--select` and the environment variable `TFN_DEVICE_PUBLIC_HEADER` change the sources of a build: `tfn` stops when
    a build with `--version` has one of them.
  - The tree must have `main/tfn_release_keys.h`, the public keys of the release (`sdk/tools/release_cfg.py header`).
    Give the same public keys to `tfn` with `--store-pub` and `--ota-pub` (and `--ota-pub2` when the release has a
    second OTA key): 130 hex digits that start with `04`, or a file that holds them. Each key must be a point of the
    curve P-256. `tfn` stops when `main/tfn_release_keys.h` is not the text that `release_cfg.py header` writes for
    these keys: a file with the TEST keys of a dry run, a file of an earlier build, a key less or more, or other
    lines. The image trusts only the keys of that file.
  - A release build starts with no old sdkconfig (as `release.yml`, which builds in a new folder): `idf.py
    reconfigure` keeps each value of an old sdkconfig, so a menuconfig change of a DEV folder would go into the
    release. `tfn` stops when the build folder has the sdkconfig or the CMake cache of another build (for example
    `build_dev/<kind>`). Give a release build its own folder, for example `--build-dir build_rel/<kind>`. `tfn` puts
    the mark `tfn_release_folder.txt` into a folder of a release build, and each later release build in that folder
    removes its sdkconfig first.
  - `tfn` runs `idf.py reconfigure`, puts the values of `force_cfg` and `release_cfg` of `boards.json` and the OTA
    mode into the sdkconfig (`release_cfg.py force`), builds, and checks the build (`release_cfg.py check` of
    `build_dev/<kind>/config/sdkconfig.h`). A build that does not have these values stops with no `tfn_build.json`,
    and its image gets the name `tfn_os.bin.failed` (`tfn flash` does not take it).
  - After the build, `tfn` checks the tree again (the build takes some minutes, and a sync, an editor or another
    tool can change a source): the same checks as before the build, and the same commit at `HEAD`. It also checks
    that the image holds each public key of the options (`release_cfg.py keys`). When a check fails, the image gets
    the name `tfn_os.bin.failed`, and `tfn` writes no `tfn_build.json`.
  - `--ota-mode enforce` (the default: the board installs only an update with a good signature) or `report` (a dry
    run with TEST keys).
  - `tfn_build.json` gets the channel `RELEASE` (`TEST` for `--ota-mode report`: such an image is not an image of a
    release), the OTA mode, the commit (`git rev-parse HEAD`) and the key ID of each public key (the first 16 hex
    digits of its SHA-256). A DEV build gets the channel `DEV`.
  - A TEST build (`--ota-mode report`) has the version text of the option in the image, and the boards, the manager
    and a pool manager read the version from the image. So a TEST build must never leave the computer of the
    developer: do not flash it to the board of a user and do not publish it. `tfn` stops when a TEST build has the
    version of a release tag (`v<version>` in git). Give a TEST build a version that no release has.
  - The SDK docs (`sdk/docs/tools.md`) tell the other steps of a release build.
- Before the build, the values of `force_cfg` in `boards.json` go into the sdkconfig of the build folder (when it has
  the option). A release build uses the same values.
- After the build, `tfn` reads the image and checks that it tells the correct kind and version. It writes the facts to
  `build_dev/<kind>/tfn_build.json` (a build that fails removes the file of the build before).
- `--select <file>` gives an app selection file (`-D ARCADE_SELECT`). `--build-dir` gives another build folder.
- `tfn` stops when the build folder has the CMake cache of another source folder. `dev.ps1` builds a copy of the
  source (`build_dev/work`) into the same folders. Use `--build-dir` to keep the two apart.
- `tfn` stops when the ESP-IDF is not 6.1 (`--any-idf` goes on).

## The flash

The flash plan comes from `boards.json`:

- An update (the usual case): `tfn` reads the partition table of the board (this resets the board), finds the table
  in `boards.json`, and writes the app and the otadata. The place of the otadata comes from the table of the board.
  An unknown table, or a table that does not agree with the build, stops the flash.
- `--first-time` (a new board, also a board with its stock firmware): `tfn` erases the NVS and the file system, then
  writes the bootloader, the partition table, the app and the otadata. This erases the settings and the files of the
  board. `tfn` asks before it starts (`--yes` does not ask).
- Before each flash, `tfn` checks the build: the kind in the image, the MD5 of the bootloader (with no build date) and
  the MD5 of the partition table against the known values. A kind with no known bootloader MD5 needs `--force` for a
  first-time flash.
- The Tab5: the bootloader, the table and the boot menu belong to the Tab5Boot project. `tfn` erases the otadata and
  writes the app into the slot `tfn_a`. A first-time flash of the Tab5 is not possible with `tfn`.

## The tests

`tfn test` runs the same host checks as `.github/workflows/ci.yml`: the app list, `apps_all.inc`, the contracts, the
licences, `boards.json`, the web flasher test, the tests of the proof tool, the unit tests, the comments, the board
names, the public API, the function sizes, the format and the all-app fuzz (3.49, CoreS3 and Watcher frames, 300
frames for each app). A step stays SKIP when its tool is not on the computer (for example `node`, or `clang-format`
22.1.8: set `CLANG_FORMAT` to its path). `tfn test --list` shows the names of the steps.

## boards.json

| Field | Meaning |
|---|---|
| `chip` | The `IDF_TARGET`. |
| `sdkconfig_defaults` | The defaults files of the kind, in their order (a later line wins). |
| `dev_sdkconfig` | The name of the sdkconfig that `dev.ps1` uses in its work folder. |
| `partition_table` | The CSV of the build. |
| `flash_size`, `flash_chip_mb` | The flash size of the image header, and the size of the flash chip on the board. |
| `console_usb`, `console` | The USB VID:PID of the console port, and the console (USB Serial/JTAG or UART0). |
| `frame` | The frame of the apps (`LCD_W` x `LCD_H`), the native frame and the round HI frame of `board_caps.h`. |
| `boot_md5`, `table_md5` | The known MD5 values of the bootloader (with no build date) and of the partition table. |
| `flash` | The addresses of the flash plan, the baud rate and the reset after the write. |
| `tables` | The partition tables that a board can have, and the place of the otadata in each. |

A new board kind needs an entry in `boards.json`. Then run `python tests/boards/check_boards.py`.
