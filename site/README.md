# The web site of TFN OS

A static web site: the start page (`index.html`), the web flasher (`install.html`), the store page (`store.html`), the
licences (`licences.html`) and the test page of the flasher (`test.html`). It has no build step, no trackers and no
third-party requests: the fonts and esptool-js are in this folder. `.github/workflows/release.yml` puts it on GitHub
Pages with the firmware files and the store of each release.

## Serve only the firmware of the public source

Serve only release images that you build from the public source, with no files that you may not share (fonts with no
open licence, game data, your WiFi or home addresses).

## The folders

| Path | What it is |
|---|---|
| `index.html`, `css/site.css`, `js/site.js`, `js/theme.js` | the start page |
| `install.html`, `css/install.css`, `js/flasher/` | the web flasher |
| `store.html`, `css/store.css`, `js/store/` | the store page: a gallery of the apps of each board kind, from `store/index.json` (signed by a key of `STORE_KEYS` in `js/store/core.js`), with the previews of `media/apps/media.json`, and the install over USB (`js/store/usb.js`: Web Serial to the console of TFN OS: `sdusb put` of each file with a CRC check, then `apps commit`; the board checks the signature) |
| `js/boards.js` | the board kinds: chip, flash size, USB IDs, picture |
| `vendor/esptool-js-0.7.0/` | esptool-js 0.7.0 (`bundle.js` of the npm package, Apache 2.0, with its LICENSE) |
| `fonts/` | Bricolage Grotesque and Atkinson Hyperlegible Next (latin, woff2, SIL OFL 1.1) |
| `test/` | the flasher checks (`run.js`), for Node (`flasher.test.js`) and the browser (`page.js`) |
| `tools/fw_manifest.py` | the firmware files and the `manifest.json` of a kind from an ESP-IDF build folder, with checks |
| `tools/pages_build.py` | the folder of the web site: the pages, `firmware/`, `store/` and the board pictures |
| `firmware/` | NOT IN GIT: the release files and a `manifest.json` for each kind (`tools/fw_manifest.py`) |
| `store/` | NOT IN GIT: the signed static store (`sdk/tools/store_index.py`, `sdk/docs/store.md` section 7) |

## The web flasher

It works in Chrome and Edge on a computer (Web Serial). The steps:

1. **The console.** The port opens at 115200 with DTR and RTS low, so the board does not reset. The page sends the
   console command `ota`. A running TFN OS answers `board <kind>, firmware <version>, running <slot>`. So the page
   knows the kind and the version with no reset.
2. **The ROM bootloader** (esptool-js): the chip, the flash size and the MAC. With the USB IDs of the port, the page
   makes the short list of the boards that fit (`core.js` `shortlist`): the same chip, the same flash size, a fitting
   USB chip. The user confirms the board by its picture. The page never offers a kind of another chip. When the
   console told the kind, the chip and the flash must agree with it, or the page stops.
3. **The mode.**
   - INSTALL: erases the whole flash, then writes the bootloader (0x0), the partition table (0x8000), the app
     (0x10000) and the blank otadata (0x710000). All data on the board goes.
   - UPDATE (only when the console told a kind): reads the partition table of the board first and stops when it is
     not the table of the release. Then it writes the app (0x10000) and the blank otadata, so the board starts the
     new app from ota_0. The settings, the WiFi and the files stay.
4. **The checks.** The size and the SHA-256 of each file must be the values of the manifest before the write. After
   each write, the board reads the flash back and esptool-js compares the MD5.

An INSTALL can save a copy of the whole flash to a file first (the original firmware of a new board).

THE SOURCE OF THE FILES. By default the page reads `firmware/index.json` (the folder of `tools/fw_manifest.py`). A page with `<meta name="tfn-firmware-index" content="URL">` reads that index instead: for example `tfn-firmware.json` of a GitHub release (`sdk/tools/release_pack.py`), at `releases/latest/download/` behind a proxy of the same site (the GitHub download host sends no CORS header, and the page allows `connect-src` only to its own site). The path of each manifest is from the index, and the path of each file is from its manifest, so all files of an install come from one release.

The Tab5 has no web install: TFN OS runs there under a boot menu that is not in this repo. The page shows "Install
guide coming" for it.

## The firmware files

`tools/fw_manifest.py` makes the files of one kind from its ESP-IDF build folder: the bootloader, the partition
table, the app (with the OTA signature of `sdk/tools/fw_sign.py`, when you give `--app`) and the blank otadata, and a
`manifest.json` with the offset, the size and the SHA-256 of each file (`buildPlan` in `js/flasher/core.js` reads
it). It stops when the partition table is not the CSV file of the kind (`boards.json`), when an image is for another
chip, or when the otadata is not blank.

```sh
python site/tools/fw_manifest.py kind lcd349 --build build_rel/lcd349 --version 1.0.0 --app signed.bin --out fw
python site/tools/fw_manifest.py index --out fw
python site/tools/pages_build.py --out pages --firmware fw --store store --golden tests/golden
```

## The release and the check of a stranger

A tag `v<major>.<minor>.<patch>` starts `.github/workflows/release.yml`: the firmware of each kind with a web install,
the SDK package, the signed store, the GitHub release (`sdk/tools/release_pack.py`) and GitHub Pages. Then
`.github/workflows/release-check.yml` gets the files as a stranger does (no checkout) and runs
`sdk/tools/release_check.py` of the SDK package of the release: each file that the pages name, the manifests with the
logic of the install page, the store signatures, `SHA256SUMS` and the OTA signature of each firmware file. "Run
workflow" of `release.yml` is a dry run with TEST keys: it publishes nothing.

The firmware of a release is ENFORCE: a board installs only an update with a good signature of a key of the release
(the secret `TFN_OTA_KEY`; the optional variable `TFN_OTA_PUB` must be its public value). A dry run builds REPORT. The
public keys go into `main/tfn_release_keys.h` (`sdk/tools/release_cfg.py`; git does not track it), and the job stops
when a build does not have the OTA mode.

## The tests

```sh
node site/test/flasher.test.js               # FLASHER TESTS OK
node site/test/store.test.js                 # STORE PAGE TESTS OK
python tests/release/test_release.py         # RELEASE TOOLS TESTS OK (the package "cryptography", openssl)
python -m http.server 8377 --bind 127.0.0.1  # in site/, then open http://127.0.0.1:8377/test.html
```

`install.html?test=<scenario>` runs the real page with a fake port and a fake ROM bootloader (`js/flasher/fake.js`,
the scenarios in `js/flasher/scenarios.js`). It uses the real manifests in `firmware/` and writes nothing.
