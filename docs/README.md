# The docs of TFN OS

This folder has the docs of the OS. The docs of the app SDK are in `sdk/docs/` (start at `sdk/README.md`).

## For the users

| Page | What it tells |
| --- | --- |
| [user_guide.md](user_guide.md) | The first start, SETUP, the settings panel, the notices, FILES, the STORE, the web page of the board, friends and the games for 2 boards, the hotspot |
| [pools.md](pools.md) | The pools: JOIN with the 6 digits, LEAVE, RELAXED and STRICT, the beacons, the console `pool`, `/api/pool` |
| [poolmgr.md](poolmgr.md) | The pool manager `poolmgr` on one computer: install, `init`, `adopt`, the store, the updates, the backup and the recovery, `serve` and `service` |
| [security.md](security.md) | What TFN OS protects now, what it does not protect, and the target design (pools, keys, signed updates) |

## For the developers of the OS

| Page | What it tells |
| --- | --- |
| [architecture.md](architecture.md) | The layers of the code, the tasks and the cores, the memory, the app loader and the ABI, the store client, the OTA |
| [board_guide.md](board_guide.md) | How to add a board kind: `board_caps.h`, the driver, the defaults file, the partition table, the lists of the tools, the tests |
| [headers.md](headers.md) | The extension connectors of each board: each pin, its GPIO, its use, the free pins |

## For the developers of apps

| Page | What it tells |
| --- | --- |
| `sdk/README.md` | The SDK: your first app, the tools, the rules |
| `sdk/docs/howto.md` | How to make each kind of app |
| `sdk/docs/api.md` | The API reference (made from the headers of `main/tfn/`) |
| `sdk/docs/raw.md` | The raw ESP-IDF functions, the permission classes, the ABI request |
| `sdk/docs/store.md` | The text of the store |

## Other files

| Folder | Content |
| --- | --- |
| `licenses/` | The licence texts of the fonts and the sound bank |

The rules of the work (the branches, the checks, the code standard) are in `CONTRIBUTING.md`.
