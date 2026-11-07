# WIFI SCAN: a WiFi scanner

NETWORKS shows each network that the board hears: its signal in dBm, its channel and its security, the strongest
first. CHANNELS shows the networks as curves over the 13 channels of 2.4 GHz. A tap on a network opens FOLLOW: the
live signal of that network, a graph of the last minute and a tick sound. Use it to find the best place for a board
or a router.

This app is in the store only (it is not built into the firmware). It needs a firmware with ABI rev 1 or newer.

## What it shows

| Topic | Where in `app_wifiscan.c` | Rule |
| --- | --- | --- |
| Raw ESP-IDF functions: `esp_wifi_scan_start()`, `esp_wifi_scan_get_ap_records()`, `esp_wifi_sta_get_ap_info()` and three more, with the real `esp_wifi.h` | `scan_step()` | [raw.md](../../docs/raw.md) |
| A permission class: `mk_app.py` writes `perms=wifi.scan` into the manifest, and the STORE asks the user before the install | (the tool does it) | raw.md section 3 |
| A blocking call in a task of the app; the task ends in `leave()` (`esp_wifi_scan_stop()`, then a wait) | `scan_task()`, `leave()` | raw.md section 6, [behave.md](../../docs/behave.md) |
| Data from the task to the app task with one buffer and a flag | `s_back`, `s_ready`, `update()` | raw.md section 6 |
| A firmware function next to the raw ones: no scan while the board connects | `wifi_state()` in `scan_step()` | raw.md section 6 |
| A list that scrolls with a drag (the drag cancels the tap), 2 columns on the 3.49 | `input()`, `draw_list()` | [visual.md](../../docs/visual.md) |
| A graph and curves from `gfx_fill_rect()` | `draw_channels()`, `draw_follow()` | visual.md |
| The screen stays on in FOLLOW (you walk with the board) | `awake()` | behave.md section 6 |
| The settings panel items: SORT and FOLLOW SOUND, with save keys | `ITEMS[]`, `PANEL` | behave.md section 7 |
| The menu icon: the WiFi fan; its arcs light up one by one | `icon()` | visual.md section 8 |

## Build

From `tfn-os/`:

```
python sdk/tools/mk_app.py sdk/examples/wifiscan/app_wifiscan.c --sym app_wifiscan --name "WIFI SCAN" --group UTIL --kinds lcd349,cores3 --nosign
```

Add `--push <board ip> --open` to send it to a board in the developer mode. Without `--nosign`, the package gets the
signature of the manager, and `--publish` sends it to the store. The output line shows `rev 1, perms wifi.scan`.

## Tests

The host tests of the project (not in this repo):

- `build.sh` and `shots.sh`: the app on the PC with made-up networks (`fake_wifi.c`): the
  pictures of each screen on the 3.49 and on the 320 x 240 frame.
- `board_wifiscan.py <port> <tag>`: on a board in the developer mode: the install, the scans,
  the pictures, the close, and the refusal of a file without `perms=`.
- `board_store_ws.py <port> <tag>`: the signed package from the STORE (the permission question).
