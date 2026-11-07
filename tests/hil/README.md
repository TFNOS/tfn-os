# The HIL runner (hardware in the loop)

`hil.py` does the board checks after a release with one command. It runs on the PC that has the boards on USB. It
is layer E of the test plan: the hardware smoke test on the PC of the board owner.

## The checks

| Step | What it checks |
|---|---|
| `versions` | The version and the signed-OTA fields of each board: `/api/devices` of the manager and `/api/boards` of each board (`ver`, `sig`, `minsv`, `own_sig`, `pend`, `self`). |
| `mem` | The free internal RAM at the menu after N minutes (the console command `mem`). The default limit is 45 KB. |
| `open-all` | Each app of `main/apps.json` that the board has opens with no fault. The tool does not open MIC, because MIC takes the USB for its sound device. |
| `pair` | The 2 BOARDS link test on two boards (PONG, ZAP, TENNIS). |
| `old-to-new` | A board that runs an old version gets the new version through the update queue of the manager. The tool watches the board until the update is complete. |
| `all` | `versions`, then `mem` and `open-all` on each `console` board, then `pair` on the two `pair` boards. |
| `ports` | The boards of the local file and the USB ports. The tool opens no port. |

## The setup

1. Copy `boards.example.json` to `boards.local.json` in this folder. Git ignores the local file.
2. Put your boards into the local file. Give each board its name, its console port, its MAC, its kind and its role.
3. Put each port that the tool must never open into `deny_ports`.
4. Give the URL of the manager in the local file, or with `--manager`, or in the environment variable `TFN_MANAGER`.
5. Install pyserial (`pip install pyserial`). The pictures of `pair --shots` need Pillow.

The roles:
- `console`: the tool does `mem` and `open-all` on this board.
- `pair`: one of the two boards of the link test.
- `net`: the tool reads this board through the web only (`versions`, `old-to-new`). This board needs no port.

## How to use it

```
python tests/hil/hil.py ports
python tests/hil/hil.py versions --want 1.0.102 --minsv 1
python tests/hil/hil.py mem --minutes 3
python tests/hil/hil.py open-all --board BOARD-A
python tests/hil/hil.py pair pong zap tennis
python tests/hil/hil.py old-to-new --board BOARD-A --from 1.0.60 --to 1.0.102 --expect-sig enforce --minsv 1 --go
python tests/hil/hil.py all --want 1.0.102
```

The tool writes the console logs, the pictures and `summary.json` into `tests/host/hil/<time>/`. Git ignores this
folder. `--out` gives another folder.

The exit code:
- 0: all checks are OK.
- 1: one or more checks found a fault.
- 2: the tool stopped (a safety rule, a bad option or a bad local file).

## The safety rules

The tool always obeys these rules:
- Before the tool opens a port, it reads the USB serial number of the port from the system. For the USB of an ESP32
  chip, the serial number is the MAC. If the serial number is not the MAC of the local file, the tool stops.
- The tool never opens a port of `deny_ports`.
- The tool opens each port with DTR and RTS low. Thus the board does not reset and does not go into the download mode.
- The tool sends `quiet 1` to each console at the start. At the end, it sends the menu and `quiet 0`. It does this
  also after a fault.
- The tool does not flash, does not erase and does not send an image to a board.
- `old-to-new` needs a board that already runs the old version. If the board runs another version, the tool stops.
  The tool only puts the update into the queue of the manager, as the release flow does. Without `--go`, the step is
  a dry run.

Before you use `old-to-new`, make sure that the new release can go to that board. The tool does not know the
partition table of the board.

## The test of the tool

`test_hil.py` tests the tool with fakes: a fake serial board, and a fake HTTP board and manager. It needs no board, no
port and no network.

```
python tests/hil/test_hil.py
```

The last line is `HIL TEST: OK` or `HIL TEST: FAULT`.
